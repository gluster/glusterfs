import os
import sys
import pwd
import time
import fcntl
import shutil
import logging
from threading import Lock, Thread as baseThread
from errno import EACCES, EAGAIN, EPIPE, ENOTCONN, ECONNABORTED, EINTR, errorcode
from signal import signal, SIGTERM, SIGKILL
from time import sleep
import select as oselect
from os import waitpid as owaitpid
try:
    from cPickle import PickleError
except ImportError:
    # py 3
    from pickle import PickleError

from gconf import gconf

try:
    # py 3
    from urllib import parse as urllib
except ImportError:
    import urllib

def escape(s):
    """the chosen flavor of string escaping, used all over
       to turn whatever data to creatable representation"""
    return urllib.quote_plus(s)

def unescape(s):
    """inverse of .escape"""
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
    """open @fname + contest for its fcntl lock

    @content: if given, set the file content to it
    """
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
    """.grabfile customization for pid files"""
    if not fname:
        fname = gconf.pid_file
    content = None
    if setpid:
        content = str(os.getpid()) + '\n'
    return grabfile(fname, content=content)

final_lock = Lock()

def finalize(*a, **kw):
    """all those messy final steps we go trough upon termination

    Do away with pidfile, ssh control dir and logging.
    """
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
    if getattr(gconf, 'state_socket', None):
        try:
            os.unlink(gconf.state_socket)
        except:
            if sys.exc_info()[0] == OSError:
                pass
    if gconf.log_exit:
        logging.info("exiting.")
    sys.stdout.flush()
    sys.stderr.flush()
    os._exit(kw.get('exval', 0))

def log_raise_exception(excont):
    """top-level exception handler

    Try to some fancy things to cover up we face with an error.
    Translate some weird sounding but well understood exceptions
    into human-friendly lingo
    """
    is_filelog = False
    for h in logging.getLogger().handlers:
        fno = getattr(getattr(h, 'stream', None), 'fileno', None)
        if fno and not os.isatty(fno()):
            is_filelog = True

    exc = sys.exc_info()[1]
    if isinstance(exc, SystemExit):
        excont.exval = exc.code or 0
        raise
    else:
        logtag = None
        if isinstance(exc, GsyncdError):
            if is_filelog:
                logging.error(exc.args[0])
            sys.stderr.write('failure: ' + exc.args[0] + '\n')
        elif isinstance(exc, PickleError) or isinstance(exc, EOFError) or \
             ((isinstance(exc, OSError) or isinstance(exc, IOError)) and \
              exc.errno == EPIPE):
            logging.error('connection to peer is broken')
            if hasattr(gconf, 'transport'):
                gconf.transport.wait()
                gconf.transport.terminate_geterr()
        elif isinstance(exc, OSError) and exc.errno in (ENOTCONN, ECONNABORTED):
            logging.error('glusterfs session went down [%s]', errorcode[exc.errno])
        else:
            logtag = "FAIL"
        if not logtag and logging.getLogger().isEnabledFor(logging.DEBUG):
            logtag = "FULL EXCEPTION TRACE"
        if logtag:
            logging.exception(logtag + ": ")
            sys.stderr.write("failed with %s.\n" % type(exc).__name__)
        excont.exval = 1
        sys.exit(excont.exval)


class FreeObject(object):
    """wildcard class for which any attribute can be set"""

    def __init__(self, **kw):
        for k,v in kw.items():
            setattr(self, k, v)

class Thread(baseThread):
    """thread class flavor for gsyncd

    - always a daemon thread
    - force exit for whole program if thread
      function coughs up an exception
    """
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

def getusername(uid = None):
    if uid == None:
        uid = os.geteuid()
    return pwd.getpwuid(uid).pw_name

def privileged():
    return os.geteuid() == 0

def boolify(s):
    """
    Generic string to boolean converter

    return
    - Quick return if string 's' is of type bool
    - True if it's in true_list
    - False if it's in false_list
    - Warn if it's not present in either and return False
    """
    true_list  = ['true', 'yes', '1', 'on']
    false_list = ['false', 'no', '0', 'off']

    if isinstance(s, bool):
        return s

    rv = False
    lstr = s.lower()
    if lstr in true_list:
        rv = True
    elif not lstr in false_list:
        logging.warn("Unknown string (%s) in string to boolean conversion defaulting to False\n" % (s))

    return rv

def eintr_wrap(func, exc, *a):
    """
    wrapper around syscalls resilient to interrupt caused
    by signals
    """
    while True:
        try:
            return func(*a)
        except exc:
            ex = sys.exc_info()[1]
            if not ex.args[0] == EINTR:
                raise

def select(*a):
    return eintr_wrap(oselect.select, oselect.error, *a)

def waitpid (*a):
    return eintr_wrap(owaitpid, OSError, *a)

def set_term_handler(hook=lambda *a: finalize(*a, **{'exval': 1})):
    signal(SIGTERM, hook)
