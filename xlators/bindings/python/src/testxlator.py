#   Copyright (c) 2007 Chris AtLee <chris@atlee.ca>
#   This file is part of GlusterFS.
#
#   GlusterFS is free software; you can redistribute it and/or modify
#   it under the terms of the GNU General Public License as published
#   by the Free Software Foundation; either version 3 of the License,
#   or (at your option) any later version.
#
#   GlusterFS is distributed in the hope that it will be useful, but
#   WITHOUT ANY WARRANTY; without even the implied warranty of
#   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
#   General Public License for more details.
#
#   You should have received a copy of the GNU General Public License
#   along with this program.  If not, see
#   <http://www.gnu.org/licenses/>.

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
