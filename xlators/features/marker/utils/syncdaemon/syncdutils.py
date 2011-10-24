import os
import sys
import time
import fcntl
import shutil
import logging
from threading import Lock, Thread as baseThread
from errno import EACCES, EAGAIN, EINTR
from signal import SIGTERM, SIGKILL
from time import sleep
import select as oselect
from os import waitpid as owaitpid

from gconf import gconf

try:
    # py 3
    from urllib import parse as urllib
except ImportError:
    import urllib

def escape(s):
    return urllib.quote_plus(s)

def unescape(s):
    return urllib.unquote_plus(s)

def norm(s):
    if s:
        return s.replace('-', '_')

def update_file(path, updater, merger = lambda f: True):
    """update a file in a transaction-like manner"""

    fr = fw = None
    try:
        fd = os.open(path, os.O_CREAT|os.O_RDWR)
        try:
            fr = os.fdopen(fd, 'r+b')
        except:
            os.close(fd)
            raise
        fcntl.lockf(fr, fcntl.LOCK_EX)
        if not merger(fr):
            return

        tmpp = path + '.tmp.' + str(os.getpid())
        fd = os.open(tmpp, os.O_CREAT|os.O_EXCL|os.O_WRONLY)
        try:
            fw = os.fdopen(fd, 'wb', 0)
        except:
            os.close(fd)
            raise
        updater(fw)
        os.fsync(fd)
        os.rename(tmpp, path)
    finally:
        for fx in (fr, fw):
            if fx:
                fx.close()

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

final_lock = Lock()

def finalize(*a, **kw):
    final_lock.acquire()
    if getattr(gconf, 'pid_file', None):
        rm_pidf = gconf.pid_file_owned
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
    os._exit(kw.get('exval', 0))

def log_raise_exception(excont):
    exc = sys.exc_info()[1]
    if isinstance(exc, SystemExit):
        excont.exval = exc.code or 0
        raise
    else:
        logging.exception("FAIL: ")
        sys.stderr.write("failed with %s.\n" % type(exc).__name__)
        excont.exval = 1
        sys.exit(excont.exval)


class FreeObject(object):
    """wildcard class for which any attribute can be set"""

    def __init__(self, **kw):
        for k,v in kw.iteritems():
            setattr(self, k, v)

class Thread(baseThread):

    def __init__(self, *a, **kw):
        tf = kw.get('target')
        if tf:
            def twrap(*aa):
                excont = FreeObject(exval = 0)
                try:
                    tf(*aa)
                except:
                    try:
                        log_raise_exception(excont)
                    finally:
                        finalize(exval = excont.exval)
            kw['target'] = twrap
        baseThread.__init__(self, *a, **kw)
        self.setDaemon(True)

class GsyncdError(Exception):
    pass

def eintr_wrap(func, exc, *a):
    """
    wrapper around syscalls resilient to interrupt caused
    by signals
    """
    while True:
        try:
            return func(*a)
        except exc, ex:
            if not ex[0] == EINTR:
                raise GsyncdError(ex[1])

def select(*a):
    return eintr_wrap(oselect.select, oselect.error, *a)

def waitpid(*a):
    return eintr_wrap(owaitpid, OSError, *a)
