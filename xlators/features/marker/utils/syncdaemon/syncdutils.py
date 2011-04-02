import os
import fcntl
try:
    # py 3
    from urllib import parse as urllib
except ImportError:
    import urllib

def escape(s):
    return urllib.quote_plus(s)

def unescape(s):
    return urllib.unquote_plus(s)

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
        merger(fr)

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
