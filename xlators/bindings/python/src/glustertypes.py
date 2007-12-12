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
import collections

#
# Forward declaration of some gluster types
#
class call_frame_t(Structure):
    pass

class call_ctx_t(Structure):
    pass

class call_pool_t(Structure):
    pass

class xlator_t(Structure):
    def _getFirstChild(self):
        return self.children[0].xlator
    firstChild = property(_getFirstChild)

class xlator_list_t(Structure):
    pass

class xlator_fops(Structure):
    pass

class xlator_mops(Structure):
    pass

class glusterfs_ctx_t(Structure):
    pass

class list_head(Structure):
    pass

class dict_t(Structure):
    pass

class inode_table_t(Structure):
    pass

class fd_t(Structure):
    pass

class iovec(Structure):
    _fields_ = [
            ("iov_base", c_void_p),
            ("iov_len", c_size_t),
            ]

    def __init__(self, s):
        self.iov_base = cast(c_char_p(s), c_void_p)
        self.iov_len = len(s)

    def getBytes(self):
        return string_at(self.iov_base, self.iov_len)

# This is a pthread_spinlock_t
# TODO: what happens to volatile-ness?
gf_lock_t = c_int

uid_t = c_uint32
gid_t = c_uint32
pid_t = c_int32

off_t = c_int64

#
# Function pointer types
#
ret_fn_t = CFUNCTYPE(c_int32, POINTER(call_frame_t), POINTER(call_frame_t),
                              POINTER(xlator_t), c_int32, c_int32)

fini_fn_t = CFUNCTYPE(None, POINTER(xlator_t))
init_fn_t = CFUNCTYPE(c_int32, POINTER(xlator_t))
event_notify_fn_t = CFUNCTYPE(c_int32, POINTER(xlator_t), c_int32, c_void_p)

list_head._fields_ = [
        ("next", POINTER(list_head)),
        ("prev", POINTER(list_head)),
        ]

call_frame_t._fields_ = [
        ("root", POINTER(call_ctx_t)),
        ("parent", POINTER(call_frame_t)),
        ("next", POINTER(call_frame_t)),
        ("prev", POINTER(call_frame_t)),
        ("local", c_void_p),
        ("this", POINTER(xlator_t)),
        ("ret", ret_fn_t),
        ("ref_count", c_int32),
        ("lock", gf_lock_t),
        ("cookie", c_void_p),
        ("op", c_int32),
        ("type", c_int8),
        ]

call_ctx_t._fields_ = [
        ("all_frames", list_head),
        ("trans", c_void_p),
        ("pool", call_pool_t),
        ("unique", c_uint64),
        ("state", c_void_p),
        ("uid", uid_t),
        ("gid", gid_t),
        ("pid", pid_t),
        ("frames", call_frame_t),
        ("req_refs", POINTER(dict_t)),
        ("rsp_refs", POINTER(dict_t)),
        ]

xlator_t._fields_ = [
        ("name", c_char_p),
        ("type", c_char_p),
        ("next", POINTER(xlator_t)),
        ("prev", POINTER(xlator_t)),
        ("parent", POINTER(xlator_t)),
        ("children", POINTER(xlator_list_t)),
        ("fops", POINTER(xlator_fops)),
        ("mops", POINTER(xlator_mops)),
        ("fini", fini_fn_t),
        ("init", init_fn_t),
        ("notify", event_notify_fn_t),
        ("options", POINTER(dict_t)),
        ("ctx", POINTER(glusterfs_ctx_t)),
        ("itable", POINTER(inode_table_t)),
        ("ready", c_char),
        ("private", c_void_p),
        ]

xlator_list_t._fields_ = [
        ("xlator", POINTER(xlator_t)),
        ("next", POINTER(xlator_list_t)),
        ]

fop_functions = collections.defaultdict(lambda: c_void_p)
fop_function_names = ['lookup', 'forget', 'stat', 'fstat', 'chmod', 'fchmod',
        'chown', 'fchown', 'truncate', 'ftruncate', 'utimens', 'access',
        'readlink', 'mknod', 'mkdir', 'unlink', 'rmdir', 'symlink',
        'rename', 'link', 'create', 'open', 'readv', 'writev', 'flush',
        'close', 'fsync', 'opendir', 'readdir', 'closedir', 'fsyncdir',
        'statfs', 'setxattr', 'getxattr', 'removexattr', 'lk', 'writedir',
        # TODO: Call backs?
        ]

fop_writev_t = CFUNCTYPE(c_int32, POINTER(call_frame_t), POINTER(xlator_t),
                                  POINTER(fd_t), POINTER(iovec), c_int32,
                                  off_t)

fop_functions['writev'] = fop_writev_t
xlator_fops._fields_ = [(f, fop_functions[f]) for f in fop_function_names]
