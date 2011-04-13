import os
import fcntl
from threading import Thread as baseThread
from signal import SIGTERM

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
                try:
                    tf(*aa)
                except:
                    try:
                        raise
                    finally:
                        os.kill(os.getpid(), SIGTERM)
            kw['target'] = twrap
        baseThread.__init__(self, *a, **kw)
        self.setDaemon(True)
