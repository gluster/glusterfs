#!/usr/bin/env python

import os
import os.path
import sys
import time
import logging
import signal
import select
import shutil
import optparse
import fcntl
from optparse import OptionParser, SUPPRESS_HELP
from logging import Logger
from errno import EEXIST, ENOENT, EACCES, EAGAIN

from gconf import gconf
from configinterface import GConffile
import resource

class GLogger(Logger):

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
        if kw.get('slave'):
            sls = "(slave)"
        else:
            sls = ""
        lprm = {'datefmt': "%Y-%m-%d %H:%M:%S",
                'format': "[%(asctime)s.%(nsecs)d] %(lvlnam)s [%(module)s" + sls + ":%(lineno)s:%(funcName)s] %(ctx)s: %(message)s"}
        lprm.update(kw)
        lvl = kw.get('level', logging.INFO)
        lprm['level'] = lvl
        logging.root = cls("root", lvl)
        logging.setLoggerClass(cls)
        logging.getLogger().handlers = []
        logging.basicConfig(**lprm)


def grabfile(fname, content=None):
    # damn those messy open() mode codes
    fd = os.open(fname, os.O_CREAT|os.O_RDWR)
    f = os.fdopen(fd, 'r+b', 0)
    try:
        fcntl.lockf(f, fcntl.LOCK_EX|fcntl.LOCK_NB)
    except:
        ex = sys.exc_info()[1]
        f.close()
        if isinstance(ex, IOError) and ex.errno in (EACCES, EAGAIN):
            # cannot grab, it's taken
            return
        raise
    if content:
        try:
            f.truncate()
            f.write(content)
        except:
            f.close()
            raise
    gconf.permanent_handles.append(f)
    return f

def grabpidfile(fname=None, setpid=True):
    if not fname:
        fname = gconf.pid_file
    content = None
    if setpid:
        content = str(os.getpid()) + '\n'
    return grabfile(fname, content=content)

def startup(**kw):
    if getattr(gconf, 'pid_file', None) and kw.get('go_daemon') != 'postconn':
        if not grabpidfile():
            sys.stderr.write("pidfile is taken, exiting.\n")
            exit(2)

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
                raise RuntimeError("cannot grap temporary pidfile")
            os.rename(gconf.pid_file + '.tmp', gconf.pid_file)
        # wait for parent to terminate
        # so we can start up with
        # no messing from the dirty
        # ol' bustard
        select.select((x,), (), ())
        os.close(x)

    lkw = {'level': gconf.log_level}
    if kw.get('log_file'):
        lkw['filename'] = kw['log_file']
    GLogger.setup(slave=kw.get('slave'), **lkw)

def finalize(*a):
    if getattr(gconf, 'pid_file', None):
        rm_pidf = True
        if gconf.cpid:
            # exit path from parent branch of daemonization
            rm_pidf = False
            while True:
                f = grabpidfile(setpid=False)
                if not f:
                    # child has already taken over pidfile
                    break
                if os.waitpid(gconf.cpid, os.WNOHANG)[0] == gconf.cpid:
                    # child has terminated
                    rm_pidf = True
                    break;
                time.sleep(0.1)
        if rm_pidf:
            try:
                os.unlink(gconf.pid_file)
            except:
                ex = sys.exc_info()[1]
                if ex.errno == ENOENT:
                    pass
                else:
                    raise
    if gconf.ssh_ctl_dir and not gconf.cpid:
        shutil.rmtree(gconf.ssh_ctl_dir)
    sys.stdout.flush()
    sys.stderr.flush()

def main():
    # ??? "finally" clause does not take effect with SIGTERM...
    # but this handler neither does
    # signal.signal(signal.SIGTERM, finalize)
    GLogger.setup()
    try:
        try:
            main_i()
        except:
            exc = sys.exc_info()[0]
            if exc != SystemExit:
                logging.exception("FAIL: ")
                sys.stderr.write("failed with %s.\n" % exc.__name__)
                sys.exit(1)
    finally:
        finalize()
        # force exit in non-main thread too
        os._exit(1)

def main_i():
    rconf = {'go_daemon': 'should'}

    def store_abs(opt, optstr, val, parser):
        setattr(parser.values, opt.dest, os.path.abspath(val))
    def store_local(opt, optstr, val, parser):
        rconf[opt.dest] = val
    def store_local_curry(val):
        return lambda o, oo, vx, p: store_local(o, oo, val, p)

    op = OptionParser(usage="%prog [options...] <master> <slave>", version="%prog 0.0.1")
    op.add_option('--gluster-command',     metavar='CMD',   default='glusterfs')
    op.add_option('--gluster-log-file',    metavar='LOGF',  default=os.devnull, type=str, action='callback', callback=store_abs)
    op.add_option('--gluster-log-level',   metavar='LVL')
    op.add_option('-p', '--pid-file',      metavar='PIDF',  type=str, action='callback', callback=store_abs)
    op.add_option('-l', '--log-file',      metavar='LOGF',  type=str, action='callback', callback=store_abs)
    op.add_option('-L', '--log-level',     metavar='LVL')
    op.add_option('-r', '--remote-gsyncd', metavar='CMD',   default='/usr/libexec/gsyncd')
    op.add_option('-s', '--ssh-command',   metavar='CMD',   default='ssh')
    op.add_option('--rsync-command',       metavar='CMD',   default='rsync')
    op.add_option('--rsync-extra',         metavar='ARGS',  default='-sS', help=SUPPRESS_HELP)
    op.add_option('--timeout',             metavar='SEC',   type=int, default=120)
    op.add_option('--sync-jobs',           metavar='N',     type=int, default=3)
    op.add_option('--turns',               metavar='N',     type=int, default=0, help=SUPPRESS_HELP)

    op.add_option('-c', '--config-file',   metavar='CONF',  type=str, action='callback', callback=store_local)
    # duh. need to specify dest or value will be mapped to None :S
    op.add_option('--listen', dest='listen', help=SUPPRESS_HELP,      action='callback', callback=store_local_curry(True))
    op.add_option('-N', '--no-daemon', dest="go_daemon",    action='callback', callback=store_local_curry('dont'))
    op.add_option('--debug', dest="go_daemon",              action='callback', callback=lambda *a: (store_local_curry('dont')(*a),
                                                                                                    a[-1].values.__dict__.get('log_level') or \
                                                                                                     a[-1].values.__dict__.update(log_level='DEBUG')))
    op.add_option('--config-get',           metavar='OPT',  type=str, dest='config', action='callback', callback=store_local)
    op.add_option('--config-get-all', dest='config', action='callback', callback=store_local_curry(True))
    op.add_option('--config-set',           metavar='OPT VAL', type=str, nargs=2, dest='config', action='callback',
                   callback=lambda o, oo, vx, p: store_local(o, oo, (vx[0], vx[1], False), p))
    op.add_option('--config-set-rx',        metavar='OPT VAL', type=str, nargs=2, dest='config', action='callback',
                   callback=lambda o, oo, vx, p: store_local(o, oo, (vx[0], vx[1], True), p))
    op.add_option('--config-del',           metavar='OPT',  type=str, dest='config', action='callback', callback=lambda o, oo, vx, p:
                                                                                                                    store_local(o, oo, (vx, False, False), p))
    op.add_option('--config-del-rx',        metavar='OPT',  type=str, dest='config', action='callback', callback=lambda o, oo, vx, p:
                                                                                                                    store_local(o, oo, (vx, False, True), p))
    op.add_option('--canonicalize-url',        dest='do_canon', action='callback', callback=store_local_curry('raw'))
    op.add_option('--canonicalize-escape-url', dest='do_canon', action='callback', callback=store_local_curry('escaped'))

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
            rconf.get('do_canon')):
        sys.stderr.write("error: incorrect number of arguments\n\n")
        sys.stderr.write(op.get_usage() + "\n")
        sys.exit(1)

    if confdata and isinstance(confdata, tuple) and confdata[2]:
        # peers are regexen, don't try to parse them
        canon_peers = args
    else:
        rscs = [resource.parse_url(u) for u in args]
        dc = rconf.get('do_canon')
        if dc:
            for r in rscs:
                print(r.get_url(canonical=True, escaped=(dc=='escaped')))
            return
        local = remote = None
        if rscs:
          local = rscs[0]
          if len(rscs) > 1:
              remote = rscs[1]
          if not local.can_connect_to(remote):
              raise RuntimeError("%s cannot work with %s" % (local.path, remote and remote.path))
        pa = ([], [])
        canon = [False, True]
        for x in (local, remote):
            if x:
                for i in range(2):
                    pa[i].append(x.get_url(canonical=canon[i]))
        peers, canon_peers = pa
    if not 'config_file' in rconf:
        rconf['config_file'] = os.path.join(os.path.dirname(sys.argv[0]), "conf/gsyncd.conf")
    gcnf = GConffile(rconf['config_file'], canon_peers)

    if confdata:
        if isinstance(confdata, tuple):
            if confdata[1]:
                gcnf.set(*confdata)
            else:
                gcnf.delete(confdata[0], confdata[1])
        else:
            if confdata == True:
                confdata = None
            gcnf.get(confdata)
        return

    gconf.__dict__.update(defaults.__dict__)
    gcnf.update_to(gconf.__dict__)
    gconf.__dict__.update(opts.__dict__)

    #normalize loglevel
    lvl0 = gconf.log_level
    if isinstance(lvl0, str):
        lvl1 = lvl0.upper()
        lvl2 = logging.getLevelName(lvl1)
        # I have _never_ _ever_ seen such an utterly braindead
        # error condition
        if lvl2 == "Level " + lvl1:
            raise RuntimeError('cannot recognize log level "%s"' % lvl0)
        gconf.log_level = lvl2

    go_daemon = rconf['go_daemon']

    if isinstance(remote, resource.SSH) and go_daemon == 'should':
        go_daemon = 'postconn'
        log_file = None
    else:
        log_file = gconf.log_file
    startup(go_daemon=go_daemon, log_file=log_file, slave=(not remote))

    logging.info("syncing: %s" % " -> ".join(peers))
    if remote:
        go_daemon = remote.connect_remote(go_daemon=go_daemon)
        if go_daemon:
            startup(go_daemon=go_daemon, log_file=gconf.log_file)
            # complete remote connection in child
            remote.connect_remote(go_daemon='done')
    local.connect()
    local.service_loop(*[r for r in [remote] if r])

    logging.info("exiting.")


if __name__ == "__main__":
    main()
