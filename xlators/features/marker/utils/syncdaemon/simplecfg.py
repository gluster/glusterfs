import re
import tempfile
import os

CommentRe = re.compile('\s*(#|$)')
ParseRe = re.compile('\s*(\S+):\s+(.*\S)\s+$')

class SimpleCfgError(Exception):
    pass

class SimpleCfg(dict):
    """
    Read/write support for a simple config file format.
    Entries can be of the form "key: value".
    "#" comments are supported. Whitespace-only lines are ignored.
    """

    def __init__(self, *a, **kw):
        dict.__init__(self, *a, **kw)
        self.klist =  dict.keys(self)

    def __setitem__(self, k, v):
        k = k.replace('-', '_')
        if not k in self:
            self.klist.append(k)
        dict.__setitem__(self, k, v)

    def __iter__(self):
        return self.klist.__iter__()

    def keys(self):
        return self.klist

    def pop(self, key, *a):
        e = dict.pop(self, key, *a)
        self.klist.remove(key)
        return e

    def readstream(self, s):
        while True:
            l = s.readline()
            if not l:
                break
            m = ParseRe.match(l)
            if m:
                k, v = m.groups()
                self[k] = v
            elif not CommentRe.match(l):
                raise SimpleCfgError('syntax error')

    def writestream(self, s):
        for k in self:
            s.write('%s: %s\n' % (k, self[k]))

    def read(self, file):
        f = None
        try:
            f = open(file)
            self.readstream(f)
        finally:
            if f:
                f.close()

    def write(self, file):
        tfd = None
        tfil = None
        try:
            tfd, tname = tempfile.mkstemp(dir=os.path.dirname(file))
            tfil, tfd = os.fdopen(tfd, 'w'), None
            self.writestream(tfil)
            os.fsync(tfil.fileno())
            os.rename(tname, file)
        finally:
            if tfd != None:
                os.close(tfd)
            if tfil != None:
                tfil.close()
