#!/usr/bin/env python
#
# Copyright (c) 2011-2014 Red Hat, Inc. <http://www.redhat.com>
# This file is part of GlusterFS.

# This file is licensed to you under your choice of the GNU Lesser
# General Public License, version 3 or any later version (LGPLv3 or
# later), or the GNU General Public License, version 2 (GPLv2), in all
# cases as published by the Free Software Foundation.
#

import os
import os.path
import glob
import sys
import time
import logging
import shutil
import optparse
import fcntl
import fnmatch
from optparse import OptionParser, SUPPRESS_HELP
from logging import Logger, handlers
from errno import ENOENT

from ipaddr import IPAddress, IPNetwork

from gconf import gconf
from syncdutils import FreeObject, norm, grabpidfile, finalize
from syncdutils import log_raise_exception, privileged, boolify
from syncdutils import GsyncdError, select, set_term_handler
from configinterface import GConffile, upgrade_config_file, TMPL_CONFIG_FILE
import resource
from monitor import monitor
import xml.etree.ElementTree as XET
from subprocess import PIPE
import subprocess
from changelogagent import agent, Changelog
from gsyncdstatus import set_monitor_status, GeorepStatus
from libcxattr import Xattr
import struct

ParseError = XET.ParseError if hasattr(XET, 'ParseError') else SyntaxError


class GLogger(Logger):

    """Logger customizations for gsyncd.

    It implements a log format similar to that of glusterfs.
    """

    def makeRecord(self, name, level, *a):
        rv = Logger.makeRecord(self, name, level, *a)
        rv.nsecs = (rv.created - int(rv.created)) * 1000000
        fr = sys._getframe(4)
        callee = fr.f_locals.get('self')
        if callee:
            ctx = str(type(callee)).split("'")[1].split('.')[-1]
        else:
            ctx = '<top>'
        if not hasattr(rv, 'funcName'):
            rv.funcName = fr.f_code.co_name
        rv.lvlnam = logging.getLevelName(level)[0]
        rv.ctx = ctx
        return rv

    @classmethod
    def setup(cls, **kw):
        lbl = kw.get('label', "")
        if lbl:
            lbl = '(' + lbl + ')'
        lprm = {'datefmt': "%Y-%m-%d %H:%M:%S",
                'format': "[%(asctime)s.%(nsecs)d] %(lvlnam)s [%(module)s" +
                lbl + ":%(lineno)s:%(funcName)s] %(ctx)s: %(message)s"}
        lprm.update(kw)
        lvl = kw.get('level', logging.INFO)
        lprm['level'] = lvl
        logging.root = cls("root", lvl)
        logging.setLoggerClass(cls)
        logging.getLogger().handlers = []
        logging.getLogger().setLevel(lprm['level'])
        logging.Formatter.converter = time.gmtime  # Log in GMT/UTC time

        if 'filename' in lprm:
            try:
                logging_handler = handlers.WatchedFileHandler(lprm['filename'])
                formatter = logging.Formatter(fmt=lprm['format'],
                                              datefmt=lprm['datefmt'])
                logging_handler.setFormatter(formatter)
                logging.getLogger().addHandler(logging_handler)
            except AttributeError:
                # Python version < 2.6 will not have WatchedFileHandler
                # so fallback to logging without any handler.
                # Note: logrotate will not work if Python version is < 2.6
                logging.basicConfig(**lprm)
        else:
            # If filename not passed(not available in lprm) then it may be
            # streaming.(Ex: {"stream": "/dev/stdout"})
            logging.basicConfig(**lprm)

    @classmethod
    def _gsyncd_loginit(cls, **kw):
        lkw = {}
        if gconf.log_level:
            lkw['level'] = gconf.log_level
        if kw.get('log_file'):
            if kw['log_file'] in ('-', '/dev/stderr'):
                lkw['stream'] = sys.stderr
            elif kw['log_file'] == '/dev/stdout':
                lkw['stream'] = sys.stdout
            else:
                lkw['filename'] = kw['log_file']

        cls.setup(label=kw.get('label'), **lkw)

        lkw.update({'saved_label': kw.get('label')})
        gconf.log_metadata = lkw
        gconf.log_exit = True


# Given slave host and its volume name, get corresponding volume uuid
def slave_vol_uuid_get(host, vol):
    po = subprocess.Popen(['gluster', '--xml', '--remote-host=' + host,
                           'volume', 'info', vol], bufsize=0,
                          stdin=None, stdout=PIPE, stderr=PIPE)
    vix, err = po.communicate()
    if po.returncode != 0:
        logging.info("Volume info failed, unable to get "
                     "volume uuid of %s present in %s,"
                     "returning empty string: %s" %
                     (vol, host, po.returncode))
        return ""
    vi = XET.fromstring(vix)
    if vi.find('opRet').text != '0':
        logging.info("Unable to get volume uuid of %s, "
                     "present in %s returning empty string: %s" %
                     (vol, host, vi.find('opErrstr').text))
        return ""

    try:
        voluuid = vi.find("volInfo/volumes/volume/id").text
    except (ParseError, AttributeError, ValueError) as e:
        logging.info("Parsing failed to volume uuid of %s, "
                     "present in %s returning empty string: %s" %
                     (vol, host, e))
        voluuid = ""

    return voluuid


def startup(**kw):
    """set up logging, pidfile grabbing, daemonization"""
    if getattr(gconf, 'pid_file', None) and kw.get('go_daemon') != 'postconn':
        if not grabpidfile():
            sys.stderr.write("pidfile is taken, exiting.\n")
            sys.exit(2)
        gconf.pid_file_owned = True

    if kw.get('go_daemon') == 'should':
        x, y = os.pipe()
        gconf.cpid = os.fork()
        if gconf.cpid:
            os.close(x)
            sys.exit()
        os.close(y)
        os.setsid()
        dn = os.open(os.devnull, os.O_RDWR)
        for f in (sys.stdin, sys.stdout, sys.stderr):
            os.dup2(dn, f.fileno())
        if getattr(gconf, 'pid_file', None):
            if not grabpidfile(gconf.pid_file + '.tmp'):
                raise GsyncdError("cannot grab temporary pidfile")
            os.rename(gconf.pid_file + '.tmp', gconf.pid_file)
        # wait for parent to terminate
        # so we can start up with
        # no messing from the dirty
        # ol' bustard
        select((x,), (), ())
        os.close(x)

    GLogger._gsyncd_loginit(**kw)


def _unlink(path):
    try:
        os.unlink(path)
    except (OSError, IOError):
        if sys.exc_info()[1].errno == ENOENT:
            pass
        else:
            raise GsyncdError('Unlink error: %s' % path)


def main():
    """main routine, signal/exception handling boilerplates"""
    gconf.starttime = time.time()
    set_term_handler()
    GLogger.setup()
    excont = FreeObject(exval=0)
    try:
        try:
            main_i()
        except:
            log_raise_exception(excont)
    finally:
        finalize(exval=excont.exval)


def main_i():
    """internal main routine

    parse command line, decide what action will be taken;
    we can either:
    - query/manipulate configuration
    - format gsyncd urls using gsyncd's url parsing engine
    - start service in following modes, in given stages:
      - agent: startup(), ChangelogAgent()
      - monitor: startup(), monitor()
      - master: startup(), connect_remote(), connect(), service_loop()
      - slave: startup(), connect(), service_loop()
    """
    rconf = {'go_daemon': 'should'}

    def store_abs(opt, optstr, val, parser):
        if val and val != '-':
            val = os.path.abspath(val)
        setattr(parser.values, opt.dest, val)

    def store_local(opt, optstr, val, parser):
        rconf[opt.dest] = val

    def store_local_curry(val):
        return lambda o, oo, vx, p: store_local(o, oo, val, p)

    def store_local_obj(op, dmake):
        return lambda o, oo, vx, p: store_local(
            o, oo, FreeObject(op=op, **dmake(vx)), p)

    op = OptionParser(
        usage="%prog [options...] <master> <slave>", version="%prog 0.0.1")
    op.add_option('--gluster-command-dir', metavar='DIR', default='')
    op.add_option('--gluster-log-file', metavar='LOGF',
                  default=os.devnull, type=str, action='callback',
                  callback=store_abs)
    op.add_option('--gluster-log-level', metavar='LVL')
    op.add_option('--changelog-log-level', metavar='LVL', default="INFO")
    op.add_option('--gluster-params', metavar='PRMS', default='')
    op.add_option(
        '--glusterd-uuid', metavar='UUID', type=str, default='',
        help=SUPPRESS_HELP)
    op.add_option(
        '--gluster-cli-options', metavar='OPTS', default='--log-file=-')
    op.add_option('--mountbroker', metavar='LABEL')
    op.add_option('-p', '--pid-file', metavar='PIDF', type=str,
                  action='callback', callback=store_abs)
    op.add_option('-l', '--log-file', metavar='LOGF', type=str,
                  action='callback', callback=store_abs)
    op.add_option('--iprefix', metavar='LOGD', type=str,
                  action='callback', callback=store_abs)
    op.add_option('--changelog-log-file', metavar='LOGF', type=str,
                  action='callback', callback=store_abs)
    op.add_option('--log-file-mbr', metavar='LOGF', type=str,
                  action='callback', callback=store_abs)
    op.add_option('--state-file', metavar='STATF', type=str,
                  action='callback', callback=store_abs)
    op.add_option('--state-detail-file', metavar='STATF',
                  type=str, action='callback', callback=store_abs)
    op.add_option('--georep-session-working-dir', metavar='STATF',
                  type=str, action='callback', callback=store_abs)
    op.add_option('--ignore-deletes', default=False, action='store_true')
    op.add_option('--isolated-slave', default=False, action='store_true')
    op.add_option('--use-rsync-xattrs', default=False, action='store_true')
    op.add_option('--sync-xattrs', default=True, action='store_true')
    op.add_option('--sync-acls', default=True, action='store_true')
    op.add_option('--log-rsync-performance', default=False,
                  action='store_true')
    op.add_option('--pause-on-start', default=False, action='store_true')
    op.add_option('-L', '--log-level', metavar='LVL')
    op.add_option('-r', '--remote-gsyncd', metavar='CMD',
                  default=os.path.abspath(sys.argv[0]))
    op.add_option('--volume-id', metavar='UUID')
    op.add_option('--slave-id', metavar='ID')
    op.add_option('--session-owner', metavar='ID')
    op.add_option('--local-id', metavar='ID', help=SUPPRESS_HELP, default='')
    op.add_option(
        '--local-path', metavar='PATH', help=SUPPRESS_HELP, default='')
    op.add_option('-s', '--ssh-command', metavar='CMD', default='ssh')
    op.add_option('--ssh-port', metavar='PORT', type=int, default=22)
    op.add_option('--ssh-command-tar', metavar='CMD', default='ssh')
    op.add_option('--rsync-command', metavar='CMD', default='rsync')
    op.add_option('--rsync-options', metavar='OPTS', default='')
    op.add_option('--rsync-ssh-options', metavar='OPTS', default='--compress')
    op.add_option('--timeout', metavar='SEC', type=int, default=120)
    op.add_option('--connection-timeout', metavar='SEC',
                  type=int, default=60, help=SUPPRESS_HELP)
    op.add_option('--sync-jobs', metavar='N', type=int, default=3)
    op.add_option('--replica-failover-interval', metavar='N',
                  type=int, default=1)
    op.add_option('--changelog-archive-format', metavar='N',
                  type=str, default="%Y%m")
    op.add_option('--use-meta-volume', default=False, action='store_true')
    op.add_option('--meta-volume-mnt', metavar='N',
                  type=str, default="/var/run/gluster/shared_storage")
    op.add_option(
        '--turns', metavar='N', type=int, default=0, help=SUPPRESS_HELP)
    op.add_option('--allow-network', metavar='IPS', default='')
    op.add_option('--socketdir', metavar='DIR')
    op.add_option('--state-socket-unencoded', metavar='SOCKF',
                  type=str, action='callback', callback=store_abs)
    op.add_option('--checkpoint', metavar='LABEL', default='0')

    # tunables for failover/failback mechanism:
    # None   - gsyncd behaves as normal
    # blind  - gsyncd works with xtime pairs to identify
    #          candidates for synchronization
    # wrapup - same as normal mode but does not assign
    #          xtimes to orphaned files
    # see crawl() for usage of the above tunables
    op.add_option('--special-sync-mode', type=str, help=SUPPRESS_HELP)

    # changelog or xtime? (TODO: Change the default)
    op.add_option(
        '--change-detector', metavar='MODE', type=str, default='xtime')
    # sleep interval for change detection (xtime crawl uses a hardcoded 1
    # second sleep time)
    op.add_option('--change-interval', metavar='SEC', type=int, default=3)
    # working directory for changelog based mechanism
    op.add_option('--working-dir', metavar='DIR', type=str,
                  action='callback', callback=store_abs)
    op.add_option('--use-tarssh', default=False, action='store_true')

    op.add_option('-c', '--config-file', metavar='CONF',
                  type=str, action='callback', callback=store_local)
    # duh. need to specify dest or value will be mapped to None :S
    op.add_option('--monitor', dest='monitor', action='callback',
                  callback=store_local_curry(True))
    op.add_option('--agent', dest='agent', action='callback',
                  callback=store_local_curry(True))
    op.add_option('--resource-local', dest='resource_local',
                  type=str, action='callback', callback=store_local)
    op.add_option('--resource-remote', dest='resource_remote',
                  type=str, action='callback', callback=store_local)
    op.add_option('--feedback-fd', dest='feedback_fd', type=int,
                  help=SUPPRESS_HELP, action='callback', callback=store_local)
    op.add_option('--rpc-fd', dest='rpc_fd', type=str, help=SUPPRESS_HELP)
    op.add_option('--subvol-num', dest='subvol_num', type=str,
                  help=SUPPRESS_HELP)
    op.add_option('--listen', dest='listen', help=SUPPRESS_HELP,
                  action='callback', callback=store_local_curry(True))
    op.add_option('-N', '--no-daemon', dest="go_daemon",
                  action='callback', callback=store_local_curry('dont'))
    op.add_option('--verify', type=str, dest="verify",
                  action='callback', callback=store_local)
    op.add_option('--slavevoluuid-get', type=str, dest="slavevoluuid_get",
                  action='callback', callback=store_local)
    op.add_option('--create', type=str, dest="create",
                  action='callback', callback=store_local)
    op.add_option('--delete', dest='delete', action='callback',
                  callback=store_local_curry(True))
    op.add_option('--path-list', dest='path_list', action='callback',
                  type=str, callback=store_local)
    op.add_option('--reset-sync-time', default=False, action='store_true')
    op.add_option('--status-get', dest='status_get', action='callback',
                  callback=store_local_curry(True))
    op.add_option('--debug', dest="go_daemon", action='callback',
                  callback=lambda *a: (store_local_curry('dont')(*a),
                                       setattr(
                                           a[-1].values, 'log_file', '-'),
                                       setattr(a[-1].values, 'log_level',
                                               'DEBUG'),
                                       setattr(a[-1].values,
                                               'changelog_log_file', '-')))
    op.add_option('--path', type=str, action='append')

    for a in ('check', 'get'):
        op.add_option('--config-' + a, metavar='OPT', type=str, dest='config',
                      action='callback',
                      callback=store_local_obj(a, lambda vx: {'opt': vx}))
    op.add_option('--config-get-all', dest='config', action='callback',
                  callback=store_local_obj('get', lambda vx: {'opt': None}))
    for m in ('', '-rx', '-glob'):
        # call this code 'Pythonic' eh?
        # have to define a one-shot local function to be able
        # to inject (a value depending on the)
        # iteration variable into the inner lambda
        def conf_mod_opt_regex_variant(rx):
            op.add_option('--config-set' + m, metavar='OPT VAL', type=str,
                          nargs=2, dest='config', action='callback',
                          callback=store_local_obj('set', lambda vx: {
                              'opt': vx[0], 'val': vx[1], 'rx': rx}))
            op.add_option('--config-del' + m, metavar='OPT', type=str,
                          dest='config', action='callback',
                          callback=store_local_obj('del', lambda vx: {
                              'opt': vx, 'rx': rx}))
        conf_mod_opt_regex_variant(m and m[1:] or False)

    op.add_option('--normalize-url', dest='url_print',
                  action='callback', callback=store_local_curry('normal'))
    op.add_option('--canonicalize-url', dest='url_print',
                  action='callback', callback=store_local_curry('canon'))
    op.add_option('--canonicalize-escape-url', dest='url_print',
                  action='callback', callback=store_local_curry('canon_esc'))
    op.add_option('--is-hottier', default=False, action='store_true')

    tunables = [norm(o.get_opt_string()[2:])
                for o in op.option_list
                if (o.callback in (store_abs, 'store_true', None) and
                    o.get_opt_string() not in ('--version', '--help'))]
    remote_tunables = ['listen', 'go_daemon', 'timeout',
                       'session_owner', 'config_file', 'use_rsync_xattrs']
    rq_remote_tunables = {'listen': True}

    # precedence for sources of values: 1) commandline, 2) cfg file, 3)
    # defaults for this to work out we need to tell apart defaults from
    # explicitly set options... so churn out the defaults here and call
    # the parser with virgin values container.
    defaults = op.get_default_values()
    opts, args = op.parse_args(values=optparse.Values())
    # slave url cleanup, if input comes with vol uuid as follows
    # 'ssh://fvm1::gv2:07dfddca-94bb-4841-a051-a7e582811467'
    temp_args = []
    for arg in args:
        # Split based on ::
        data = arg.split("::")
        if len(data)>1:
            slavevol_name = data[1].split(":")[0]
            temp_args.append("%s::%s" % (data[0], slavevol_name))
        else:
            temp_args.append(data[0])
    args = temp_args
    args_orig = args[:]

    voluuid_get = rconf.get('slavevoluuid_get')
    if voluuid_get:
        slave_host, slave_vol = voluuid_get.split("::")
        svol_uuid = slave_vol_uuid_get(slave_host, slave_vol)
        print svol_uuid
        return

    r = rconf.get('resource_local')
    if r:
        if len(args) == 0:
            args.append(None)
        args[0] = r
    r = rconf.get('resource_remote')
    if r:
        if len(args) == 0:
            raise GsyncdError('local resource unspecfied')
        elif len(args) == 1:
            args.append(None)
        args[1] = r
    confdata = rconf.get('config')
    if not (len(args) == 2 or
            (len(args) == 1 and rconf.get('listen')) or
            (len(args) <= 2 and confdata) or
            rconf.get('url_print')):
        sys.stderr.write("error: incorrect number of arguments\n\n")
        sys.stderr.write(op.get_usage() + "\n")
        sys.exit(1)

    verify = rconf.get('verify')
    if verify:
        logging.info(verify)
        logging.info("Able to spawn gsyncd.py")
        return

    restricted = os.getenv('_GSYNCD_RESTRICTED_')

    if restricted:
        allopts = {}
        allopts.update(opts.__dict__)
        allopts.update(rconf)
        bannedtuns = set(allopts.keys()) - set(remote_tunables)
        if bannedtuns:
            raise GsyncdError('following tunables cannot be set with '
                              'restricted SSH invocaton: ' +
                              ', '.join(bannedtuns))
        for k, v in rq_remote_tunables.items():
            if not k in allopts or allopts[k] != v:
                raise GsyncdError('tunable %s is not set to value %s required '
                                  'for restricted SSH invocaton' %
                                  (k, v))

    confrx = getattr(confdata, 'rx', None)

    def makersc(aa, check=True):
        if not aa:
            return ([], None, None)
        ra = [resource.parse_url(u) for u in aa]
        local = ra[0]
        remote = None
        if len(ra) > 1:
            remote = ra[1]
        if check and not local.can_connect_to(remote):
            raise GsyncdError("%s cannot work with %s" %
                              (local.path, remote and remote.path))
        return (ra, local, remote)
    if confrx:
        # peers are regexen, don't try to parse them
        if confrx == 'glob':
            args = ['\A' + fnmatch.translate(a) for a in args]
        canon_peers = args
        namedict = {}
    else:
        dc = rconf.get('url_print')
        rscs, local, remote = makersc(args_orig, not dc)
        if dc:
            for r in rscs:
                print(r.get_url(**{'normal': {},
                                   'canon': {'canonical': True},
                                   'canon_esc': {'canonical': True,
                                                 'escaped': True}}[dc]))
            return
        pa = ([], [], [])
        urlprms = (
            {}, {'canonical': True}, {'canonical': True, 'escaped': True})
        for x in rscs:
            for i in range(len(pa)):
                pa[i].append(x.get_url(**urlprms[i]))
        _, canon_peers, canon_esc_peers = pa
        # creating the namedict, a dict representing various ways of referring
        # to / repreenting peers to be fillable in config templates
        mods = (lambda x: x, lambda x: x[
                0].upper() + x[1:], lambda x: 'e' + x[0].upper() + x[1:])
        if remote:
            rmap = {local: ('local', 'master'), remote: ('remote', 'slave')}
        else:
            rmap = {local: ('local', 'slave')}
        namedict = {}
        for i in range(len(rscs)):
            x = rscs[i]
            for name in rmap[x]:
                for j in range(3):
                    namedict[mods[j](name)] = pa[j][i]
                namedict[name + 'vol'] = x.volume
                if name == 'remote':
                    namedict['remotehost'] = x.remotehost
    if not 'config_file' in rconf:
        rconf['config_file'] = TMPL_CONFIG_FILE

    # Upgrade Config File only if it is session conf file
    if rconf['config_file'] != TMPL_CONFIG_FILE:
        upgrade_config_file(rconf['config_file'], confdata)

    gcnf = GConffile(
        rconf['config_file'], canon_peers, confdata,
        defaults.__dict__, opts.__dict__, namedict)

    checkpoint_change = False
    if confdata:
        opt_ok = norm(confdata.opt) in tunables + [None]
        if confdata.op == 'check':
            if opt_ok:
                sys.exit(0)
            else:
                sys.exit(1)
        elif not opt_ok:
            raise GsyncdError("not a valid option: " + confdata.opt)
        if confdata.op == 'get':
            gcnf.get(confdata.opt)
        elif confdata.op == 'set':
            gcnf.set(confdata.opt, confdata.val, confdata.rx)
        elif confdata.op == 'del':
            gcnf.delete(confdata.opt, confdata.rx)
        # when modifying checkpoint, it's important to make a log
        # of that, so in that case we go on to set up logging even
        # if its just config invocation
        if confdata.opt == 'checkpoint' and confdata.op in ('set', 'del') and \
           not confdata.rx:
            checkpoint_change = True
        if not checkpoint_change:
            return

    gconf.__dict__.update(defaults.__dict__)
    gcnf.update_to(gconf.__dict__)
    gconf.__dict__.update(opts.__dict__)
    gconf.configinterface = gcnf

    delete = rconf.get('delete')
    if delete:
        logging.info('geo-replication delete')
        # remove the stime xattr from all the brick paths so that
        # a re-create of a session will start sync all over again
        stime_xattr_name = getattr(gconf, 'master.stime_xattr_name', None)

        # Delete pid file, status file, socket file
        cleanup_paths = []
        if getattr(gconf, 'pid_file', None):
            cleanup_paths.append(gconf.pid_file)

        if getattr(gconf, 'state_file', None):
            cleanup_paths.append(gconf.state_file)

        if getattr(gconf, 'state_detail_file', None):
            cleanup_paths.append(gconf.state_detail_file)

        if getattr(gconf, 'state_socket_unencoded', None):
            cleanup_paths.append(gconf.state_socket_unencoded)

        cleanup_paths.append(rconf['config_file'][:-11] + "*")

        # Cleanup changelog working dirs
        if getattr(gconf, 'working_dir', None):
            try:
                shutil.rmtree(gconf.working_dir)
            except (IOError, OSError):
                if sys.exc_info()[1].errno == ENOENT:
                    pass
                else:
                    raise GsyncdError(
                        'Error while removing working dir: %s' %
                        gconf.working_dir)

        for path in cleanup_paths:
            # To delete temp files
            for f in glob.glob(path + "*"):
                _unlink(f)

        reset_sync_time = boolify(gconf.reset_sync_time)
        if reset_sync_time and stime_xattr_name:
            path_list = rconf.get('path_list')
            paths = []
            for p in path_list.split('--path='):
                stripped_path = p.strip()
                if stripped_path != "":
                    # set stime to (0,0) to trigger full volume content resync
                    # to slave on session recreation
                    # look at master.py::Xcrawl   hint: zero_zero
                    Xattr.lsetxattr(stripped_path, stime_xattr_name,
                                    struct.pack("!II", 0, 0))

        return

    if restricted and gconf.allow_network:
        ssh_conn = os.getenv('SSH_CONNECTION')
        if not ssh_conn:
            # legacy env var
            ssh_conn = os.getenv('SSH_CLIENT')
        if ssh_conn:
            allowed_networks = [IPNetwork(a)
                                for a in gconf.allow_network.split(',')]
            client_ip = IPAddress(ssh_conn.split()[0])
            allowed = False
            for nw in allowed_networks:
                if client_ip in nw:
                    allowed = True
                    break
            if not allowed:
                raise GsyncdError("client IP address is not allowed")

    ffd = rconf.get('feedback_fd')
    if ffd:
        fcntl.fcntl(ffd, fcntl.F_SETFD, fcntl.FD_CLOEXEC)

    # normalize loglevel
    lvl0 = gconf.log_level
    if isinstance(lvl0, str):
        lvl1 = lvl0.upper()
        lvl2 = logging.getLevelName(lvl1)
        # I have _never_ _ever_ seen such an utterly braindead
        # error condition
        if lvl2 == "Level " + lvl1:
            raise GsyncdError('cannot recognize log level "%s"' % lvl0)
        gconf.log_level = lvl2

    if not privileged() and gconf.log_file_mbr:
        gconf.log_file = gconf.log_file_mbr

    if checkpoint_change:
        try:
            GLogger._gsyncd_loginit(log_file=gconf.log_file, label='conf')
            if confdata.op == 'set':
                logging.info('checkpoint %s set' % confdata.val)
            elif confdata.op == 'del':
                logging.info('checkpoint info was reset')
        except IOError:
            if sys.exc_info()[1].errno == ENOENT:
                # directory of log path is not present,
                # which happens if we get here from
                # a peer-multiplexed "config-set checkpoint"
                # (as that directory is created only on the
                # original node)
                pass
            else:
                raise
        return

    create = rconf.get('create')
    if create:
        if getattr(gconf, 'state_file', None):
            set_monitor_status(gconf.state_file, create)
        return

    go_daemon = rconf['go_daemon']
    be_monitor = rconf.get('monitor')
    be_agent = rconf.get('agent')

    rscs, local, remote = makersc(args)

    status_get = rconf.get('status_get')
    if status_get:
        for brick in gconf.path:
            brick_status = GeorepStatus(gconf.state_file, brick,
                                        getattr(gconf, "pid_file", None))
            checkpoint_time = int(getattr(gconf, "checkpoint", "0"))
            brick_status.print_status(checkpoint_time=checkpoint_time)
        return

    if not be_monitor and isinstance(remote, resource.SSH) and \
       go_daemon == 'should':
        go_daemon = 'postconn'
        log_file = None
    else:
        log_file = gconf.log_file
    if be_monitor:
        label = 'monitor'
    elif be_agent:
        label = 'agent'
    elif remote:
        # master
        label = gconf.local_path
    else:
        label = 'slave'
    startup(go_daemon=go_daemon, log_file=log_file, label=label)
    resource.Popen.init_errhandler()

    if be_agent:
        os.setsid()
        logging.debug('rpc_fd: %s' % repr(gconf.rpc_fd))
        return agent(Changelog(), gconf.rpc_fd)

    if be_monitor:
        return monitor(*rscs)

    logging.info("syncing: %s" % " -> ".join(r.url for r in rscs))
    if remote:
        go_daemon = remote.connect_remote(go_daemon=go_daemon)
        if go_daemon:
            startup(go_daemon=go_daemon, log_file=gconf.log_file)
            # complete remote connection in child
            remote.connect_remote(go_daemon='done')
    local.connect()
    if ffd:
        os.close(ffd)
    local.service_loop(*[r for r in [remote] if r])


if __name__ == "__main__":
    main()
