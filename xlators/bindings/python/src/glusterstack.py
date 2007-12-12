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
from ctypes import *
from glustertypes import *

libc = CDLL("libc.so.6")
calloc = libc.calloc
calloc.argtypes = [c_int, c_int]
calloc.restype = c_void_p

# TODO: Can these be done in C somehow?
def stack_wind(frame, rfn, obj, fn, *params):
    """Frame is a frame object"""
    _new = cast(calloc(1, sizeof(call_frame_t)), POINTER(call_frame_t))
    _new[0].root = frame.root
    _new[0].next = frame.root[0].frames.next
    _new[0].prev = pointer(frame.root[0].frames)
    if frame.root[0].frames.next:
        frame.root[0].frames.next[0].prev = _new
    frame.root[0].frames.next = _new
    _new[0].this = obj
    # TODO: Type checking like tmp_cbk?
    _new[0].ret = rfn
    _new[0].parent = pointer(frame)
    _new[0].cookie = cast(_new, c_void_p)
    # TODO: Initialize lock
    #_new.lock.init()
    frame.ref_count += 1
    fn(_new, obj, *params)

def stack_unwind(frame, *params):
    """Frame is a frame object"""
    fn = frame[0].ret
    parent = frame[0].parent[0]
    parent.ref_count -= 1

    op_ret = params[0]
    op_err = params[1]
    params = params[2:]
    fn(parent, call_frame_t.from_address(frame[0].cookie), parent.this,
            op_ret, op_err, *params)
