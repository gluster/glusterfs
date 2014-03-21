import sys
from gluster.glupy import *

class xlator (Translator):

    def __init__(self, c_this):
        Translator.__init__(self, c_this)

    def lookup_fop(self, frame, this, loc, xdata):
        print "Python xlator: Hello!"
        dl.wind_lookup(frame, POINTER(xlator_t)(), loc, xdata)
        return 0

    def lookup_cbk(self, frame, cookie, this, op_ret, op_errno, inode, buf,
                   xdata, postparent):
        print "Python xlator: Hello again!"
        dl.unwind_lookup(frame, cookie, this, op_ret, op_errno, inode, buf,
                         xdata, postparent)
        return 0
