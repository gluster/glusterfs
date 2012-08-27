"""
  Copyright (c) 2008-2012 Red Hat, Inc. <http://www.redhat.com>
  This file is part of GlusterFS.

  This file is licensed to you under your choice of the GNU Lesser
  General Public License, version 3 or any later version (LGPLv3 or
  later), or the GNU General Public License, version 2 (GPLv2), in all
  cases as published by the Free Software Foundation.
"""

"""
This is a test translator written in python.

Important things to note:
    This file must be import-able from glusterfsd.  This probably means
    setting PYTHONPATH to where this file is located.

    This file must have a top-level xlator class object that will be
    used to instantiate individual translators.
"""
from gluster import *

class MyXlator(ComplexTranslator):
    name = "MyXlator"
    def writev_cbk(self, frame, cookie, op_ret, op_errno, buf):
        stack_unwind(frame, op_ret, op_errno, buf)
        return 0

    def writev(self, frame, fd, vector, count, offset):
        gf_log(self.name, GF_LOG_WARNING, "writev %i bytes", vector.iov_len)
        # TODO: Use cookie to pass this to writev_cbk
        old_count = vector.iov_len

        data = vector.getBytes().encode("zlib")

        vector = iovec(data)
        gf_log(self.name, GF_LOG_WARNING, "writev %i bytes", vector.iov_len)

        @ret_fn_t
        def rfn(frame, prev, this, op_ret, op_errno, *params):
            if len(params) == 0:
                params = [0]
            return self.writev_cbk(frame, prev, old_count, op_errno, *params)

        stack_wind(frame, rfn, self.firstChild,
                self.firstChild[0].fops[0].writev, fd, vector, count, offset)
        return 0

xlator = MyXlator
