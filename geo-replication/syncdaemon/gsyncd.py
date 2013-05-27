#!/usr/bin/env python

import os
import os.path
import sys
import time
import logging
import signal
import optparse
import fcntl
import fnmatch
from optparse import OptionParser, SUPPRESS_HELP
from logging import Logger
from errno import EEXIST, ENOENT

from ipaddr import IPAddress, IPNetwork

from gconf import gconf
from syncdutils import FreeObject, norm, grabpidfile, finalize, log_raise_exception
from syncdutils import GsyncdError, select, set_term_handler, privileged
from configinterface import GConffile
import resource
from monitor import monitor

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
                'format': "[%(asctime)s.%(nsecs)d] %(lvlnam)s [%(module)s" + lbl + ":%(lineno)s:%(funcName)s] %(ctx)s: %(message)s"}
        lprm.update(kw)
        lvl = kw.get('level', logging.INFO)
        lprm['level'] = lvl
        logging.root = cls("root", lvl)
        logging.setLoggerClass(cls)
        logging.getLogger().handlers = []
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

def main():
    """main routine, signal/exception handling boilerplates"""
    gconf.starttime = time.time()
    set_term_handler()
    GLogger.setup()
    excont = FreeObject(exval = 0)
    try:
        try:
            main_i()
        except:
            log_raise_exception(excont)
    finally:
        finalize(exval = excont.exval)

def main_i():
    """internal main routine

    parse command line, decide what action will be taken;
    we can either:
    - query/manipulate configuration
    - format gsyncd urls using gsyncd's url parsing engine
    - start service in following modes, in given stages:
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
        return lambda o, oo, vx, p: store_local(o, oo, FreeObject(op=op, **dmake(vx)), p)

    op = OptionParser(usage="%prog [options...] <master> <slave>", version="%prog 0.0.1")
    op.add_option('--gluster-command-dir', metavar='DIR',   default='')
    op.add_option('--gluster-log-file',    metavar='LOGF',  default=os.devnull, type=str, action='callback', callback=store_abs)
    op.add_option('--gluster-log-level',   metavar='LVL')
    op.add_option('--gluster-params',      metavar='PRMS',  default='')
    op.add_option('--gluster-cli-options', metavar='OPTS',  default='--log-file=-')
    op.add_option('--mountbroker',         metavar='LABEL')
    op.add_option('-p', '--pid-file',      metavar='PIDF',  type=str, action='callback', callback=store_abs)
    op.add_option('-l', '--log-file',      metavar='LOGF',  type=str, action='callback', callback=store_abs)
    op.add_option('--log-file-mbr',        metavar='LOGF',  type=str, action='callback', callback=store_abs)
    op.add_option('--state-file',          metavar='STATF', type=str, action='callback', callback=store_abs)
    op.add_option('--ignore-deletes',      default=False, action='store_true')
    op.add_option('--use-rsync-xattrs',    default=False, action='store_true')
    op.add_option('-L', '--log-level',     metavar='LVL')
    op.add_option('-r', '--remote-gsyncd', metavar='CMD',   default=os.path.abspath(sys.argv[0]))
    op.add_option('--volume-id',           metavar='UUID')
    op.add_option('--session-owner',       metavar='ID')
    op.add_option('-s', '--ssh-command',   metavar='CMD',   default='ssh')
    op.add_option('--rsync-command',       metavar='CMD',   default='rsync')
    op.add_option('--rsync-options',       metavar='OPTS',  default='--sparse')
    op.add_option('--rsync-ssh-options',   metavar='OPTS',  default='--compress')
    op.add_option('--timeout',             metavar='SEC',   type=int, default=120)
    op.add_option('--connection-timeout',  metavar='SEC',   type=int, default=60, help=SUPPRESS_HELP)
    op.add_option('--sync-jobs',           metavar='N',     type=int, default=3)
    op.add_option('--turns',               metavar='N',     type=int, default=0, help=SUPPRESS_HELP)
    op.add_option('--allow-network',       metavar='IPS',   default='')
    op.add_option('--socketdir',           metavar='DIR')
    op.add_option('--state-socket-unencoded', metavar='SOCKF', type=str, action='callback', callback=store_abs)
    op.add_option('--checkpoint',          metavar='LABEL', default='')
    # tunables for failover/failback mechanism:
    # None   - gsyncd behaves as normal
    # blind  - gsyncd works with xtime pairs to identify
    #          candidates for synchronization
    # wrapup - same as normal mode but does not assign
    #          xtimes to orphaned files
    # see crawl() for usage of the above tunables
    op.add_option('--special-sync-mode', type=str, help=SUPPRESS_HELP)

    op.add_option('-c', '--config-file',   metavar='CONF',  type=str, action='callback', callback=store_local)
    # duh. need to specify dest or value will be mapped to None :S
    op.add_option('--monitor', dest='monitor', action='callback', callback=store_local_curry(True))
    op.add_option('--feedback-fd', dest='feedback_fd', type=int, help=SUPPRESS_HELP, action='callback', callback=store_local)
    op.add_option('--listen', dest='listen', help=SUPPRESS_HELP,      action='callback', callback=store_local_curry(True))
    op.add_option('-N', '--no-daemon', dest="go_daemon",    action='callback', callback=store_local_curry('dont'))
    op.add_option('--debug', dest="go_daemon",              action='callback', callback=lambda *a: (store_local_curry('dont')(*a),
                                                                                                    setattr(a[-1].values, 'log_file', '-'),
                                                                                                    setattr(a[-1].values, 'log_level', 'DEBUG'))),

    for a in ('check', 'get'):
        op.add_option('--config-' + a,      metavar='OPT',  type=str, dest='config', action='callback',
                      callback=store_local_obj(a, lambda vx: {'opt': vx}))
    op.add_option('--config-get-all', dest='config', action='callback', callback=store_local_obj('get', lambda vx: {'opt': None}))
    for m in ('', '-rx', '-glob'):
        # call this code 'Pythonic' eh?
        # have to define a one-shot local function to be able to inject (a value depending on the)
        # iteration variable into the inner lambda
        def conf_mod_opt_regex_variant(rx):
            op.add_option('--config-set' + m,   metavar='OPT VAL', type=str, nargs=2, dest='config', action='callback',
                          callback=store_local_obj('set', lambda vx: {'opt': vx[0], 'val': vx[1], 'rx': rx}))
            op.add_option('--config-del' + m,   metavar='OPT',  type=str, dest='config', action='callback',
                          callback=store_local_obj('del', lambda vx: {'opt': vx, 'rx': rx}))
        conf_mod_opt_regex_variant(m and m[1:] or False)

    op.add_option('--normalize-url',           dest='url_print', action='callback', callback=store_local_curry('normal'))
    op.add_option('--canonicalize-url',        dest='url_print', action='callback', callback=store_local_curry('canon'))
    op.add_option('--canonicalize-escape-url', dest='url_print', action='callback', callback=store_local_curry('canon_esc'))

    tunables = [ norm(o.get_opt_string()[2:]) for o in op.option_list if o.callback in (store_abs, 'store_true', None) and o.get_opt_string() not in ('--version', '--help') ]
    remote_tunables = [ 'listen', 'go_daemon', 'timeout', 'session_owner', 'config_file', 'use_rsync_xattrs' ]
    rq_remote_tunables = { 'listen': True }

    # precedence for sources of values: 1) commandline, 2) cfg file, 3) defaults
    # -- for this to work out we need to tell apart defaults from explicitly set
    # options... so churn out the defaults here and call the parser with virgin
    # values container.
    defaults = op.get_default_values()
    opts, args = op.parse_args(values=optparse.Values())
    confdata = rconf.get('config')
    if not (len(args) == 2 or \
            (len(args) == 1 and rconf.get('listen')) or \
            (len(args) <= 2 and confdata) or \
            rconf.get('url_print')):
        sys.stderr.write("error: incorrect number of arguments\n\n")
        sys.stderr.write(op.get_usage() + "\n")
        sys.exit(1)

    restricted = os.getenv('_GSYNCD_RESTRICTED_')

    if restricted:
        allopts = {}
        allopts.update(opts.__dict__)
        allopts.update(rconf)
        bannedtuns = set(allopts.keys()) - set(remote_tunables)
        if bannedtuns:
            raise GsyncdError('following tunables cannot be set with restricted SSH invocaton: ' + \
                              ', '.join(bannedtuns))
        for k, v in rq_remote_tunables.items():
            if not k in allopts or allopts[k] != v:
                raise GsyncdError('tunable %s is not set to value %s required for restricted SSH invocaton' % \
                                  (k, v))

    confrx = getattr(confdata, 'rx', None)
    if confrx:
        # peers are regexen, don't try to parse them
        if confrx == 'glob':
            args = [ '\A' + fnmatch.translate(a) for a in args ]
        canon_peers = args
        namedict = {}
    else:
        rscs = [resource.parse_url(u) for u in args]
        dc = rconf.get('url_print')
        if dc:
            for r in rscs:
                print(r.get_url(**{'normal': {},
                                   'canon': {'canonical': True},
                                   'canon_esc': {'canonical': True, 'escaped': True}}[dc]))
            return
        local = remote = None
        if rscs:
            local = rscs[0]
            if len(rscs) > 1:
                remote = rscs[1]
            if not local.can_connect_to(remote):
                raise GsyncdError("%s cannot work with %s" % (local.path, remote and remote.path))
        pa = ([], [], [])
        urlprms = ({}, {'canonical': True}, {'canonical': True, 'escaped': True})
        for x in rscs:
            for i in range(len(pa)):
                pa[i].append(x.get_url(**urlprms[i]))
        peers, canon_peers, canon_esc_peers = pa
        # creating the namedict, a dict representing various ways of referring to / repreenting
        # peers to be fillable in config templates
        mods = (lambda x: x, lambda x: x[0].upper() + x[1:], lambda x: 'e' + x[0].upper() + x[1:])
        if remote:
            rmap = { local: ('local', 'master'), remote: ('remote', 'slave') }
        else:
            rmap = { local: ('local', 'slave') }
        namedict = {}
        for i in range(len(rscs)):
            x = rscs[i]
            for name in rmap[x]:
                for j in range(3):
                    namedict[mods[j](name)] = pa[j][i]
                if x.scheme == 'gluster':
                    namedict[name + 'vol'] = x.volume
    if not 'config_file' in rconf:
        rconf['config_file'] = os.path.join(os.path.dirname(sys.argv[0]), "conf/gsyncd.conf")
    gcnf = GConffile(rconf['config_file'], canon_peers, defaults.__dict__, opts.__dict__, namedict)

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

    if restricted and gconf.allow_network:
        ssh_conn = os.getenv('SSH_CONNECTION')
        if not ssh_conn:
            #legacy env var
            ssh_conn = os.getenv('SSH_CLIENT')
        if ssh_conn:
            allowed_networks = [ IPNetwork(a) for a in gconf.allow_network.split(',') ]
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

    #normalize loglevel
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

    go_daemon = rconf['go_daemon']
    be_monitor = rconf.get('monitor')

    if not be_monitor and isinstance(remote, resource.SSH) and \
       go_daemon == 'should':
        go_daemon = 'postconn'
        log_file = None
    else:
        log_file = gconf.log_file
    if be_monitor:
        label = 'monitor'
    elif remote:
        #master
        label = ''
    else:
        label = 'slave'
    startup(go_daemon=go_daemon, log_file=log_file, label=label)

    if be_monitor:
        return monitor()

    logging.info("syncing: %s" % " -> ".join(peers))
    resource.Popen.init_errhandler()
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
