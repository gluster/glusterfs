##
## Copyright (c) 2006-2014 Red Hat, Inc. <http://www.redhat.com>
## This file is part of GlusterFS.
##
## This file is licensed to you under your choice of the GNU Lesser
## General Public License, version 3 or any later version (LGPLv3 or
## later), or the GNU General Public License, version 2 (GPLv2), in all
## cases as published by the Free Software Foundation.
##

import sys
import os
from ctypes import *

dl = CDLL(os.getenv("PATH_GLUSTERFS_GLUPY_MODULE", ""),RTLD_GLOBAL)


class call_frame_t (Structure):
        pass

class dev_t (Structure):
        pass


class dict_t (Structure):
        pass


class gf_dirent_t (Structure):
        pass


class iobref_t (Structure):
	pass


class iovec_t (Structure):
        pass


class list_head (Structure):
        pass

list_head._fields_ = [
                ("next", POINTER(list_head)),
                ("prev", POINTER(list_head))
        ]


class rwxperm_t (Structure):
        _fields_ = [
                ("read", c_uint8, 1),
                ("write", c_uint8, 1),
                ("execn", c_uint8, 1)
        ]


class statvfs_t (Structure):
        pass


class xlator_t (Structure):
        pass


class ia_prot_t (Structure):
        _fields_ = [
                ("suid", c_uint8, 1),
                ("sgid", c_uint8, 1),
                ("sticky", c_uint8, 1),
                ("owner", rwxperm_t),
                ("group", rwxperm_t),
                ("other", rwxperm_t)
        ]

# For checking file type.
(IA_INVAL, IA_IFREG, IA_IFDIR, IA_IFLNK, IA_IFBLK, IA_IFCHR, IA_IFIFO,
 IA_IFSOCK) = xrange(8)


class iatt_t (Structure):
        _fields_ = [
                ("ia_no", c_uint64),
                ("ia_gfid", c_ubyte * 16),
                ("ia_dev", c_uint64),
                ("ia_type", c_uint),
                ("ia_prot", ia_prot_t),
                ("ia_nlink", c_uint32),
                ("ia_uid", c_uint32),
                ("ia_gid", c_uint32),
                ("ia_rdev", c_uint64),
                ("ia_size", c_uint64),
                ("ia_blksize", c_uint32),
                ("ia_blocks", c_uint64),
                ("ia_atime", c_uint32 ),
                ("ia_atime_nsec", c_uint32),
                ("ia_mtime", c_uint32),
                ("ia_mtime_nsec", c_uint32),
                ("ia_ctime", c_uint32),
                ("ia_ctime_nsec", c_uint32)
        ]


class mem_pool (Structure):
        _fields_ = [
                ("list", list_head),
                ("hot_count", c_int),
                ("cold_count", c_int),
                ("lock", c_void_p),
                ("padded_sizeof_type", c_ulong),
                ("pool", c_void_p),
                ("pool_end", c_void_p),
                ("real_sizeof_type", c_int),
                ("alloc_count", c_uint64),
                ("pool_misses", c_uint64),
                ("max_alloc", c_int),
                ("curr_stdalloc", c_int),
                ("max_stdalloc", c_int),
                ("name", c_char_p),
                ("global_list", list_head)
        ]


class U_ctx_key_inode (Union):
        _fields_ = [
                ("key", c_uint64),
                ("xl_key", POINTER(xlator_t))
        ]


class U_ctx_value1 (Union):
        _fields_ = [
                ("value1", c_uint64),
                ("ptr1", c_void_p)
        ]


class U_ctx_value2 (Union):
        _fields_ = [
                ("value2", c_uint64),
                ("ptr2", c_void_p)
        ]

class inode_ctx (Structure):
        _anonymous_ = ("u_key","u_value1","u_value2",)
        _fields_ = [
                ("u_key", U_ctx_key_inode),
                ("u_value1", U_ctx_value1),
                ("u_value2", U_ctx_value2)
        ]

class inode_t (Structure):
        pass

class inode_table_t (Structure):
        _fields_ = [
                ("lock", c_void_p),
                ("hashsize", c_size_t),
                ("name", c_char_p),
                ("root", POINTER(inode_t)),
                ("xl", POINTER(xlator_t)),
                ("lru_limit", c_uint32),
                ("inode_hash", POINTER(list_head)),
                ("name_hash", POINTER(list_head)),
                ("active", list_head),
                ("active_size", c_uint32),
                ("lru", list_head),
                ("lru_size", c_uint32),
                ("purge", list_head),
                ("purge_size", c_uint32),
                ("inode_pool", POINTER(mem_pool)),
                ("dentry_pool", POINTER(mem_pool)),
                ("fd_mem_pool", POINTER(mem_pool))
        ]

inode_t._fields_ = [
                ("table", POINTER(inode_table_t)),
                ("gfid", c_ubyte * 16),
                ("lock", c_void_p),
                ("nlookup", c_uint64),
                ("fd_count", c_uint32),
                ("ref", c_uint32),
                ("ia_type", c_uint),
                ("fd_list", list_head),
                ("dentry_list", list_head),
                ("hashv", list_head),
                ("listv", list_head),
                ("ctx", POINTER(inode_ctx))
        ]



class U_ctx_key_fd (Union):
        _fields_ = [
                ("key", c_uint64),
                ("xl_key", c_void_p)
        ]

class fd_lk_ctx (Structure):
        _fields_ = [
                ("lk_list", list_head),
                ("ref", c_int),
                ("lock", c_void_p)
        ]

class fd_ctx (Structure):
        _anonymous_ = ("u_key","u_value1")
        _fields_ = [
                ("u_key", U_ctx_key_fd),
                ("u_value1", U_ctx_value1)
        ]

class fd_t (Structure):
        _fields_ = [
                ("pid", c_uint64),
                ("flags", c_int32),
                ("refcount", c_int32),
                ("inode_list", list_head),
                ("inode", POINTER(inode_t)),
                ("lock", c_void_p),
                ("ctx", POINTER(fd_ctx)),
                ("xl_count", c_int),
                ("lk_ctx", POINTER(fd_lk_ctx)),
                ("anonymous", c_uint)
        ]

class loc_t (Structure):
        _fields_ = [
                ("path", c_char_p),
                ("name", c_char_p),
                ("inode", POINTER(inode_t)),
                ("parent", POINTER(inode_t)),
                ("gfid", c_ubyte * 16),
                ("pargfid", c_ubyte * 16),
        ]



def _init_op (a_class, fop, cbk, wind, unwind):
        # Decorators, used by translators. We could pass the signatures as
        # parameters, but it's actually kind of nice to keep them around for
        # inspection.
        a_class.fop_type = apply(CFUNCTYPE,a_class.fop_sig)
        a_class.cbk_type = apply(CFUNCTYPE,a_class.cbk_sig)
        # Dispatch-function registration.
        fop.restype = None
        fop.argtypes = [ c_long, a_class.fop_type ]
        # Callback-function registration.
        cbk.restype = None
        cbk.argtypes = [ c_long, a_class.cbk_type ]
        # STACK_WIND function.
        wind.restype = None
        wind.argtypes = list(a_class.fop_sig[1:])
        # STACK_UNWIND function.
        unwind.restype = None
        unwind.argtypes = list(a_class.cbk_sig[1:])

class OpLookup:
        fop_sig = (c_int, POINTER(call_frame_t), POINTER(xlator_t),
                   POINTER(loc_t), POINTER(dict_t))
        cbk_sig = (c_int, POINTER(call_frame_t), c_long, POINTER(xlator_t),
                   c_int, c_int, POINTER(inode_t), POINTER(iatt_t),
                   POINTER(dict_t), POINTER(iatt_t))
_init_op (OpLookup, dl.set_lookup_fop, dl.set_lookup_cbk,
                    dl.wind_lookup,    dl.unwind_lookup)

class OpCreate:
        fop_sig = (c_int, POINTER(call_frame_t), POINTER(xlator_t),
                   POINTER(loc_t), c_int, c_uint, c_uint, POINTER(fd_t),
                   POINTER(dict_t))
        cbk_sig = (c_int, POINTER(call_frame_t), c_long, POINTER(xlator_t),
                   c_int, c_int, POINTER(fd_t), POINTER(inode_t),
                   POINTER(iatt_t), POINTER(iatt_t), POINTER(iatt_t),
                   POINTER(dict_t))
_init_op (OpCreate, dl.set_create_fop, dl.set_create_cbk,
                    dl.wind_create,    dl.unwind_create)

class OpOpen:
        fop_sig = (c_int, POINTER(call_frame_t), POINTER(xlator_t),
                   POINTER(loc_t), c_int, POINTER(fd_t), POINTER(dict_t))
        cbk_sig = (c_int, POINTER(call_frame_t), c_long, POINTER(xlator_t),
                   c_int, c_int, POINTER(fd_t), POINTER(dict_t))
_init_op (OpOpen, dl.set_open_fop, dl.set_open_cbk,
                  dl.wind_open,    dl.unwind_open)

class OpReadv:
        fop_sig = (c_int, POINTER(call_frame_t), POINTER(xlator_t),
                   POINTER(fd_t), c_size_t, c_long, c_uint32, POINTER(dict_t))
        cbk_sig = (c_int, POINTER(call_frame_t), c_long, POINTER(xlator_t),
                   c_int, c_int, POINTER(iovec_t), c_int, POINTER(iatt_t),
                   POINTER(iobref_t), POINTER(dict_t))
_init_op (OpReadv, dl.set_readv_fop, dl.set_readv_cbk,
                   dl.wind_readv,    dl.unwind_readv)
class OpWritev:
        fop_sig = (c_int, POINTER(call_frame_t), POINTER(xlator_t),
                   POINTER(fd_t), POINTER(iovec_t), c_int, c_long, c_uint32,
                   POINTER(iobref_t), POINTER(dict_t))
        cbk_sig = (c_int, POINTER(call_frame_t), c_long, POINTER(xlator_t),
                   c_int, c_int, POINTER(iatt_t), POINTER(iatt_t),
                   POINTER(dict_t))
_init_op (OpWritev, dl.set_writev_fop, dl.set_writev_cbk,
                    dl.wind_writev,    dl.unwind_writev)

class OpOpendir:
        fop_sig = (c_int, POINTER(call_frame_t), POINTER(xlator_t),
                   POINTER(loc_t), POINTER(fd_t) ,POINTER(dict_t))
        cbk_sig = (c_int, POINTER(call_frame_t), c_long, POINTER(xlator_t),
                   c_int, c_int, POINTER(fd_t), POINTER(dict_t))
_init_op (OpOpendir, dl.set_opendir_fop, dl.set_opendir_cbk,
                     dl.wind_opendir,    dl.unwind_opendir)

class OpReaddir:
        fop_sig = (c_int, POINTER(call_frame_t), POINTER(xlator_t),
                   POINTER(fd_t), c_size_t, c_long, POINTER(dict_t))
        cbk_sig = (c_int, POINTER(call_frame_t), c_long, POINTER(xlator_t),
                   c_int, c_int, POINTER(gf_dirent_t), POINTER(dict_t))
_init_op (OpReaddir, dl.set_readdir_fop, dl.set_readdir_cbk,
                     dl.wind_readdir,    dl.unwind_readdir)

class OpReaddirp:
        fop_sig = (c_int, POINTER(call_frame_t), POINTER(xlator_t),
                   POINTER(fd_t), c_size_t, c_long, POINTER(dict_t))
        cbk_sig = (c_int, POINTER(call_frame_t), c_long, POINTER(xlator_t),
                   c_int, c_int, POINTER(gf_dirent_t), POINTER(dict_t))
_init_op (OpReaddirp, dl.set_readdirp_fop, dl.set_readdirp_cbk,
                      dl.wind_readdirp,    dl.unwind_readdirp)

class OpStat:
        fop_sig = (c_int, POINTER(call_frame_t), POINTER(xlator_t),
                   POINTER(loc_t), POINTER(dict_t))
        cbk_sig = (c_int, POINTER(call_frame_t), c_long, POINTER(xlator_t),
                   c_int, c_int, POINTER(iatt_t), POINTER(dict_t))
_init_op (OpStat, dl.set_stat_fop, dl.set_stat_cbk,
                  dl.wind_stat,    dl.unwind_stat)

class OpFstat:
        fop_sig = (c_int, POINTER(call_frame_t), POINTER(xlator_t),
                   POINTER(fd_t), POINTER(dict_t))
        cbk_sig = (c_int, POINTER(call_frame_t), c_long, POINTER(xlator_t),
                   c_int, c_int, POINTER(iatt_t), POINTER(dict_t))
_init_op (OpFstat, dl.set_fstat_fop, dl.set_fstat_cbk,
                   dl.wind_fstat,    dl.unwind_fstat)

class OpStatfs:
        fop_sig = (c_int, POINTER(call_frame_t), POINTER(xlator_t),
                   POINTER(loc_t), POINTER(dict_t))
        cbk_sig = (c_int, POINTER(call_frame_t), c_long, POINTER(xlator_t),
                   c_int, c_int, POINTER(statvfs_t), POINTER(dict_t))
_init_op (OpStatfs, dl.set_statfs_fop, dl.set_statfs_cbk,
                    dl.wind_statfs,    dl.unwind_statfs)


class OpSetxattr:
        fop_sig = (c_int, POINTER(call_frame_t), POINTER(xlator_t),
                   POINTER(loc_t), POINTER(dict_t), c_int32,
                   POINTER (dict_t))
        cbk_sig = (c_int, POINTER(call_frame_t), c_long, POINTER(xlator_t),
                   c_int, c_int, POINTER(dict_t))
_init_op (OpSetxattr, dl.set_setxattr_fop, dl.set_setxattr_cbk,
                      dl.wind_setxattr,    dl.unwind_setxattr)

class OpGetxattr:
        fop_sig = (c_int, POINTER(call_frame_t), POINTER(xlator_t),
                   POINTER(loc_t), c_char_p, POINTER (dict_t))
        cbk_sig = (c_int, POINTER(call_frame_t), c_long, POINTER(xlator_t),
                   c_int, c_int, POINTER(dict_t), POINTER(dict_t))
_init_op (OpGetxattr, dl.set_getxattr_fop, dl.set_getxattr_cbk,
                      dl.wind_getxattr,    dl.unwind_getxattr)

class OpFsetxattr:
        fop_sig = (c_int, POINTER(call_frame_t), POINTER(xlator_t),
                   POINTER(fd_t), POINTER(dict_t), c_int32,
                   POINTER (dict_t))
        cbk_sig = (c_int, POINTER(call_frame_t), c_long, POINTER(xlator_t),
                   c_int, c_int, POINTER(dict_t))
_init_op (OpFsetxattr, dl.set_fsetxattr_fop, dl.set_fsetxattr_cbk,
                       dl.wind_fsetxattr,    dl.unwind_fsetxattr)

class OpFgetxattr:
        fop_sig = (c_int, POINTER(call_frame_t), POINTER(xlator_t),
                   POINTER(fd_t), c_char_p, POINTER (dict_t))
        cbk_sig = (c_int, POINTER(call_frame_t), c_long, POINTER(xlator_t),
                   c_int, c_int, POINTER(dict_t), POINTER(dict_t))
_init_op (OpFgetxattr, dl.set_fgetxattr_fop, dl.set_fgetxattr_cbk,
                       dl.wind_fgetxattr,    dl.unwind_fgetxattr)

class OpRemovexattr:
        fop_sig = (c_int, POINTER(call_frame_t), POINTER(xlator_t),
                   POINTER(loc_t), c_char_p, POINTER(dict_t))
        cbk_sig = (c_int, POINTER(call_frame_t), c_long, POINTER(xlator_t),
                   c_int, c_int, POINTER(dict_t))
_init_op (OpRemovexattr, dl.set_removexattr_fop, dl.set_removexattr_cbk,
                         dl.wind_removexattr,    dl.unwind_removexattr)


class OpFremovexattr:
        fop_sig = (c_int, POINTER(call_frame_t), POINTER(xlator_t),
                   POINTER(fd_t), c_char_p, POINTER(dict_t))
        cbk_sig = (c_int, POINTER(call_frame_t), c_long, POINTER(xlator_t),
                   c_int, c_int, POINTER(dict_t))
_init_op (OpFremovexattr, dl.set_fremovexattr_fop, dl.set_fremovexattr_cbk,
                          dl.wind_fremovexattr,    dl.unwind_fremovexattr)

class OpLink:
        fop_sig = (c_int, POINTER(call_frame_t), POINTER(xlator_t),
                   POINTER(loc_t), POINTER(loc_t), POINTER(dict_t))
        cbk_sig = (c_int, POINTER(call_frame_t), c_long, POINTER(xlator_t),
                   c_int, c_int, POINTER(inode_t), POINTER(iatt_t),
                   POINTER(iatt_t), POINTER(iatt_t), POINTER(dict_t))
_init_op (OpLink, dl.set_link_fop, dl.set_link_cbk,
                  dl.wind_link,    dl.unwind_link)

class OpSymlink:
        fop_sig = (c_int, POINTER(call_frame_t), POINTER(xlator_t),
                   c_char_p, POINTER(loc_t), c_uint, POINTER(dict_t))
        cbk_sig = (c_int, POINTER(call_frame_t), c_long, POINTER(xlator_t),
                   c_int, c_int, POINTER(inode_t), POINTER(iatt_t),
                   POINTER(iatt_t), POINTER(iatt_t), POINTER(dict_t))
_init_op (OpSymlink, dl.set_symlink_fop, dl.set_symlink_cbk,
                     dl.wind_symlink,    dl.unwind_symlink)

class OpUnlink:
        fop_sig = (c_int, POINTER(call_frame_t), POINTER(xlator_t),
                   POINTER(loc_t), c_int, POINTER(dict_t))
        cbk_sig = (c_int, POINTER(call_frame_t), c_long, POINTER(xlator_t),
                   c_int, c_int, POINTER(iatt_t), POINTER(iatt_t),
                   POINTER(dict_t))
_init_op (OpUnlink, dl.set_unlink_fop, dl.set_unlink_cbk,
                    dl.wind_unlink,    dl.unwind_unlink)

class OpReadlink:
        fop_sig = (c_int, POINTER(call_frame_t), POINTER(xlator_t),
                   POINTER(loc_t), c_size_t, POINTER(dict_t))
        cbk_sig = (c_int, POINTER(call_frame_t), c_long, POINTER(xlator_t),
                   c_int, c_int, c_char_p, POINTER(iatt_t), POINTER(dict_t))
_init_op (OpReadlink, dl.set_readlink_fop, dl.set_readlink_cbk,
                      dl.wind_readlink,    dl.unwind_readlink)

class OpMkdir:
        fop_sig = (c_int, POINTER(call_frame_t), POINTER(xlator_t),
                   POINTER(loc_t), c_uint, c_uint, POINTER(dict_t))
        cbk_sig = (c_int, POINTER(call_frame_t), c_long, POINTER(xlator_t),
                   c_int, c_int, POINTER(inode_t), POINTER(iatt_t),
                   POINTER(iatt_t), POINTER(iatt_t), POINTER(dict_t))
_init_op (OpMkdir, dl.set_mkdir_fop, dl.set_mkdir_cbk,
                   dl.wind_mkdir,    dl.unwind_mkdir)

class OpRmdir:
        fop_sig = (c_int, POINTER(call_frame_t), POINTER(xlator_t),
                   POINTER(loc_t), c_int, POINTER(dict_t))
        cbk_sig = (c_int, POINTER(call_frame_t), c_long, POINTER(xlator_t),
                   c_int, c_int, POINTER(iatt_t), POINTER(iatt_t),
                   POINTER(dict_t))
_init_op (OpRmdir, dl.set_rmdir_fop, dl.set_rmdir_cbk,
                   dl.wind_rmdir,    dl.unwind_rmdir)


class Translator:
        def __init__ (self, c_this):
                # This is only here to keep references to the stubs we create,
                # because ctypes doesn't and glupy.so can't because it doesn't
                # get a pointer to the actual Python object. It's a dictionary
                # instead of a list in case we ever allow changing fops/cbks
                # after initialization and need to look them up.
                self.stub_refs = {}
                funcs = dir(self.__class__)
                if "lookup_fop" in funcs:
                        @OpLookup.fop_type
                        def stub (frame, this, loc, xdata, s=self):
                                return s.lookup_fop (frame, this, loc, xdata)
                        self.stub_refs["lookup_fop"] = stub
                        dl.set_lookup_fop(c_this,stub)
                if "lookup_cbk" in funcs:
                        @OpLookup.cbk_type
                        def stub (frame, cookie, this, op_ret, op_errno, inode,
                                  buf, xdata, postparent, s=self):
                                return s.lookup_cbk(frame, cookie, this, op_ret,
                                                    op_errno, inode, buf, xdata,
                                                    postparent)
                        self.stub_refs["lookup_cbk"] = stub
                        dl.set_lookup_cbk(c_this,stub)
                if "create_fop" in funcs:
                        @OpCreate.fop_type
                        def stub (frame, this, loc, flags, mode, umask, fd,
                                  xdata, s=self):
                                return s.create_fop (frame, this, loc, flags,
                                                     mode, umask, fd, xdata)
                        self.stub_refs["create_fop"] = stub
                        dl.set_create_fop(c_this,stub)
                if "create_cbk" in funcs:
                        @OpCreate.cbk_type
                        def stub (frame, cookie, this, op_ret, op_errno, fd,
                                  inode, buf, preparent, postparent, xdata,
                                  s=self):
                                return s.create_cbk (frame, cookie, this,
                                                     op_ret, op_errno, fd,
                                                     inode, buf, preparent,
                                                     postparent, xdata)
                        self.stub_refs["create_cbk"] = stub
                        dl.set_create_cbk(c_this,stub)
                if "open_fop" in funcs:
                        @OpOpen.fop_type
                        def stub (frame, this, loc, flags, fd,
                                  xdata, s=self):
                                return s.open_fop (frame, this, loc, flags,
                                                   fd, xdata)
                        self.stub_refs["open_fop"] = stub
                        dl.set_open_fop(c_this,stub)
                if "open_cbk" in funcs:
                        @OpOpen.cbk_type
                        def stub (frame, cookie, this, op_ret, op_errno, fd,
                                  xdata, s=self):
                                return s.open_cbk (frame, cookie, this,
                                                   op_ret, op_errno, fd,
                                                   xdata)
                        self.stub_refs["open_cbk"] = stub
                        dl.set_open_cbk(c_this,stub)
                if "readv_fop" in funcs:
                        @OpReadv.fop_type
                        def stub (frame, this, fd, size, offset, flags,
                                  xdata, s=self):
                                return s.readv_fop (frame, this, fd, size,
                                                    offset, flags, xdata)
                        self.stub_refs["readv_fop"] = stub
                        dl.set_readv_fop(c_this,stub)
                if "readv_cbk" in funcs:
                        @OpReadv.cbk_type
                        def stub (frame, cookie, this, op_ret, op_errno,
                                  vector, count, stbuf, iobref, xdata,
                                  s=self):
                                return s.readv_cbk (frame, cookie, this,
                                                    op_ret, op_errno, vector,
                                                    count, stbuf, iobref,
                                                    xdata)
                        self.stub_refs["readv_cbk"] = stub
                        dl.set_readv_cbk(c_this,stub)
                if "writev_fop" in funcs:
                        @OpWritev.fop_type
                        def stub (frame, this, fd, vector, count,
                                  offset, flags, iobref, xdata, s=self):
                                return s.writev_fop (frame, this, fd, vector,
                                                     count, offset, flags,
                                                     iobref, xdata)
                        self.stub_refs["writev_fop"] = stub
                        dl.set_writev_fop(c_this,stub)
                if "writev_cbk" in funcs:
                        @OpWritev.cbk_type
                        def stub (frame, cookie, this, op_ret, op_errno,
                                  prebuf, postbuf, xdata, s=self):
                                return s.writev_cbk (frame, cookie, this,
                                                     op_ret, op_errno, prebuf,
                                                     postbuf, xdata)
                        self.stub_refs["writev_cbk"] = stub
                        dl.set_writev_cbk(c_this,stub)
                if "opendir_fop" in funcs:
                        @OpOpendir.fop_type
                        def stub (frame, this, loc, fd, xdata, s=self):
                                return s.opendir_fop (frame, this, loc, fd,
                                                      xdata)
                        self.stub_refs["opendir_fop"] = stub
                        dl.set_opendir_fop(c_this,stub)
                if "opendir_cbk" in funcs:
                        @OpOpendir.cbk_type
                        def stub (frame, cookie, this, op_ret, op_errno, fd,
                                  xdata, s=self):
                                return s.opendir_cbk(frame, cookie, this,
                                                     op_ret, op_errno, fd,
                                                     xdata)
                        self.stub_refs["opendir_cbk"] = stub
                        dl.set_opendir_cbk(c_this,stub)
                if "readdir_fop" in funcs:
                        @OpReaddir.fop_type
                        def stub (frame, this, fd, size, offset, xdata, s=self):
                                return s.readdir_fop (frame, this, fd, size,
                                                      offset, xdata)
                        self.stub_refs["readdir_fop"] = stub
                        dl.set_readdir_fop(c_this,stub)
                if "readdir_cbk" in funcs:
                        @OpReaddir.cbk_type
                        def stub (frame, cookie, this, op_ret, op_errno,
                                  entries, xdata, s=self):
                                return s.readdir_cbk(frame, cookie, this,
                                                     op_ret, op_errno, entries,
                                                     xdata)
                        self.stub_refs["readdir_cbk"] = stub
                        dl.set_readdir_cbk(c_this,stub)
                if "readdirp_fop" in funcs:
                        @OpReaddirp.fop_type
                        def stub (frame, this, fd, size, offset, xdata, s=self):
                                return s.readdirp_fop (frame, this, fd, size,
                                                       offset, xdata)
                        self.stub_refs["readdirp_fop"] = stub
                        dl.set_readdirp_fop(c_this,stub)
                if "readdirp_cbk" in funcs:
                        @OpReaddirp.cbk_type
                        def stub (frame, cookie, this, op_ret, op_errno,
                                  entries, xdata, s=self):
                                return s.readdirp_cbk (frame, cookie, this,
                                                       op_ret, op_errno,
                                                       entries, xdata)
                        self.stub_refs["readdirp_cbk"] = stub
                        dl.set_readdirp_cbk(c_this,stub)
                if "stat_fop" in funcs:
                        @OpStat.fop_type
                        def stub (frame, this, loc, xdata, s=self):
                                return s.stat_fop (frame, this, loc, xdata)
                        self.stub_refs["stat_fop"] = stub
                        dl.set_stat_fop(c_this,stub)
                if "stat_cbk" in funcs:
                        @OpStat.cbk_type
                        def stub (frame, cookie, this, op_ret, op_errno, buf,
                                  xdata, s=self):
                                return s.stat_cbk(frame, cookie, this, op_ret,
                                                  op_errno, buf, xdata)
                        self.stub_refs["stat_cbk"] = stub
                        dl.set_stat_cbk(c_this,stub)
                if "fstat_fop" in funcs:
                        @OpFstat.fop_type
                        def stub (frame, this, fd, xdata, s=self):
                                return s.fstat_fop (frame, this, fd, xdata)
                        self.stub_refs["fstat_fop"] = stub
                        dl.set_fstat_fop(c_this,stub)
                if "fstat_cbk" in funcs:
                        @OpFstat.cbk_type
                        def stub (frame, cookie, this, op_ret, op_errno, buf,
                                  xdata, s=self):
                                return s.fstat_cbk(frame, cookie, this, op_ret,
                                                   op_errno, buf, xdata)
                        self.stub_refs["fstat_cbk"] = stub
                        dl.set_fstat_cbk(c_this,stub)
                if "statfs_fop" in funcs:
                        @OpStatfs.fop_type
                        def stub (frame, this, loc, xdata, s=self):
                                return s.statfs_fop (frame, this, loc, xdata)
                        self.stub_refs["statfs_fop"] = stub
                        dl.set_statfs_fop(c_this,stub)
                if "statfs_cbk" in funcs:
                        @OpStatfs.cbk_type
                        def stub (frame, cookie, this, op_ret, op_errno, buf,
                                  xdata, s=self):
                                return s.statfs_cbk (frame, cookie, this,
                                                     op_ret, op_errno, buf,
                                                     xdata)
                        self.stub_refs["statfs_cbk"] = stub
                        dl.set_statfs_cbk(c_this,stub)
                if "setxattr_fop" in funcs:
                        @OpSetxattr.fop_type
                        def stub (frame, this, loc, dictionary, flags, xdata,
                                  s=self):
                                return s.setxattr_fop (frame, this, loc,
                                                       dictionary, flags,
                                                       xdata)
                        self.stub_refs["setxattr_fop"] = stub
                        dl.set_setxattr_fop(c_this,stub)
                if "setxattr_cbk" in funcs:
                        @OpSetxattr.cbk_type
                        def stub (frame, cookie, this, op_ret, op_errno, xdata,
                                  s=self):
                                return s.setxattr_cbk(frame, cookie, this,
                                                      op_ret, op_errno, xdata)
                        self.stub_refs["setxattr_cbk"] = stub
                        dl.set_setxattr_cbk(c_this,stub)
                if "getxattr_fop" in funcs:
                        @OpGetxattr.fop_type
                        def stub (frame, this, loc, name, xdata, s=self):
                                return s.getxattr_fop (frame, this, loc, name,
                                                       xdata)
                        self.stub_refs["getxattr_fop"] = stub
                        dl.set_getxattr_fop(c_this,stub)
                if "getxattr_cbk" in funcs:
                        @OpGetxattr.cbk_type
                        def stub (frame, cookie, this, op_ret, op_errno,
                                  dictionary, xdata, s=self):
                                return s.getxattr_cbk(frame, cookie, this,
                                                      op_ret, op_errno,
                                                      dictionary, xdata)
                        self.stub_refs["getxattr_cbk"] = stub
                        dl.set_getxattr_cbk(c_this,stub)
                if "fsetxattr_fop" in funcs:
                        @OpFsetxattr.fop_type
                        def stub (frame, this, fd, dictionary, flags, xdata,
                                  s=self):
                                return s.fsetxattr_fop (frame, this, fd,
                                                        dictionary, flags,
                                                        xdata)
                        self.stub_refs["fsetxattr_fop"] = stub
                        dl.set_fsetxattr_fop(c_this,stub)
                if "fsetxattr_cbk" in funcs:
                        @OpFsetxattr.cbk_type
                        def stub (frame, cookie, this, op_ret, op_errno, xdata,
                                  s=self):
                                return s.fsetxattr_cbk(frame, cookie, this,
                                                       op_ret, op_errno, xdata)
                        self.stub_refs["fsetxattr_cbk"] = stub
                        dl.set_fsetxattr_cbk(c_this,stub)
                if "fgetxattr_fop" in funcs:
                        @OpFgetxattr.fop_type
                        def stub (frame, this, fd, name, xdata, s=self):
                                return s.fgetxattr_fop (frame, this, fd, name,
                                                        xdata)
                        self.stub_refs["fgetxattr_fop"] = stub
                        dl.set_fgetxattr_fop(c_this,stub)
                if "fgetxattr_cbk" in funcs:
                        @OpFgetxattr.cbk_type
                        def stub (frame, cookie, this, op_ret, op_errno,
                                  dictionary, xdata, s=self):
                                return s.fgetxattr_cbk(frame, cookie, this,
                                                       op_ret, op_errno,
                                                       dictionary, xdata)
                        self.stub_refs["fgetxattr_cbk"] = stub
                        dl.set_fgetxattr_cbk(c_this,stub)
                if "removexattr_fop" in funcs:
                        @OpRemovexattr.fop_type
                        def stub (frame, this, loc, name, xdata, s=self):
                                return s.removexattr_fop (frame, this, loc,
                                                          name, xdata)
                        self.stub_refs["removexattr_fop"] = stub
                        dl.set_removexattr_fop(c_this,stub)
                if "removexattr_cbk" in funcs:
                        @OpRemovexattr.cbk_type
                        def stub (frame, cookie, this, op_ret, op_errno,
                                  xdata, s=self):
                                return s.removexattr_cbk(frame, cookie, this,
                                                         op_ret, op_errno,
                                                         xdata)
                        self.stub_refs["removexattr_cbk"] = stub
                        dl.set_removexattr_cbk(c_this,stub)
                if "fremovexattr_fop" in funcs:
                        @OpFremovexattr.fop_type
                        def stub (frame, this, fd, name, xdata, s=self):
                                return s.fremovexattr_fop (frame, this, fd,
                                                           name, xdata)
                        self.stub_refs["fremovexattr_fop"] = stub
                        dl.set_fremovexattr_fop(c_this,stub)
                if "fremovexattr_cbk" in funcs:
                        @OpFremovexattr.cbk_type
                        def stub (frame, cookie, this, op_ret, op_errno,
                                  xdata, s=self):
                                return s.fremovexattr_cbk(frame, cookie, this,
                                                          op_ret, op_errno,
                                                          xdata)
                        self.stub_refs["fremovexattr_cbk"] = stub
                        dl.set_fremovexattr_cbk(c_this,stub)
                if "link_fop" in funcs:
                        @OpLink.fop_type
                        def stub (frame, this, oldloc, newloc,
                                  xdata, s=self):
                                return s.link_fop (frame, this, oldloc,
                                                   newloc, xdata)
                        self.stub_refs["link_fop"] = stub
                        dl.set_link_fop(c_this,stub)
                if "link_cbk" in funcs:
                        @OpLink.cbk_type
                        def stub (frame, cookie, this, op_ret, op_errno,
                                  inode, buf, preparent, postparent, xdata,
                                  s=self):
                                return s.link_cbk (frame, cookie, this,
                                                   op_ret, op_errno, inode,
                                                   buf, preparent,
                                                   postparent, xdata)
                        self.stub_refs["link_cbk"] = stub
                        dl.set_link_cbk(c_this,stub)
                if "symlink_fop" in funcs:
                        @OpSymlink.fop_type
                        def stub (frame, this, linkname, loc,
                                  umask, xdata, s=self):
                                return s.symlink_fop (frame, this, linkname,
                                                      loc, umask, xdata)
                        self.stub_refs["symlink_fop"] = stub
                        dl.set_symlink_fop(c_this,stub)
                if "symlink_cbk" in funcs:
                        @OpSymlink.cbk_type
                        def stub (frame, cookie, this, op_ret, op_errno,
                                  inode, buf, preparent, postparent, xdata,
                                  s=self):
                                return s.symlink_cbk (frame, cookie, this,
                                                      op_ret, op_errno, inode,
                                                      buf, preparent,
                                                      postparent, xdata)
                        self.stub_refs["symlink_cbk"] = stub
                        dl.set_symlink_cbk(c_this,stub)
                if "unlink_fop" in funcs:
                        @OpUnlink.fop_type
                        def stub (frame, this, loc, xflags,
                                  xdata, s=self):
                                return s.unlink_fop (frame, this, loc,
                                                     xflags, xdata)
                        self.stub_refs["unlink_fop"] = stub
                        dl.set_unlink_fop(c_this,stub)
                if "unlink_cbk" in funcs:
                        @OpUnlink.cbk_type
                        def stub (frame, cookie, this, op_ret, op_errno,
                                  preparent, postparent, xdata, s=self):
                                return s.unlink_cbk (frame, cookie, this,
                                                     op_ret, op_errno,
                                                     preparent, postparent,
                                                     xdata)
                        self.stub_refs["unlink_cbk"] = stub
                        dl.set_unlink_cbk(c_this,stub)
                if "readlink_fop" in funcs:
                        @OpReadlink.fop_type
                        def stub (frame, this, loc, size,
                                  xdata, s=self):
                                return s.readlink_fop (frame, this, loc,
                                                       size, xdata)
                        self.stub_refs["readlink_fop"] = stub
                        dl.set_readlink_fop(c_this,stub)
                if "readlink_cbk" in funcs:
                        @OpReadlink.cbk_type
                        def stub (frame, cookie, this, op_ret, op_errno,
                                  path, buf, xdata, s=self):
                                return s.readlink_cbk (frame, cookie, this,
                                                       op_ret, op_errno,
                                                       path, buf, xdata)
                        self.stub_refs["readlink_cbk"] = stub
                        dl.set_readlink_cbk(c_this,stub)
                if "mkdir_fop" in funcs:
                        @OpMkdir.fop_type
                        def stub (frame, this, loc, mode, umask, xdata,
                                  s=self):
                                return s.mkdir_fop (frame, this, loc, mode,
                                                    umask, xdata)
                        self.stub_refs["mkdir_fop"] = stub
                        dl.set_mkdir_fop(c_this,stub)
                if "mkdir_cbk" in funcs:
                        @OpMkdir.cbk_type
                        def stub (frame, cookie, this, op_ret, op_errno, inode,
                                  buf, preparent, postparent, xdata, s=self):
                                return s.mkdir_cbk (frame, cookie, this,
                                                    op_ret, op_errno, inode,
                                                    buf, preparent,
                                                    postparent, xdata)
                        self.stub_refs["mkdir_cbk"] = stub
                        dl.set_mkdir_cbk(c_this,stub)
                if "rmdir_fop" in funcs:
                        @OpRmdir.fop_type
                        def stub (frame, this, loc, xflags,
                                  xdata, s=self):
                                return s.rmdir_fop (frame, this, loc,
                                                    xflags, xdata)
                        self.stub_refs["rmdir_fop"] = stub
                        dl.set_rmdir_fop(c_this,stub)
                if "rmdir_cbk" in funcs:
                        @OpRmdir.cbk_type
                        def stub (frame, cookie, this, op_ret, op_errno,
                                  preparent, postparent, xdata, s=self):
                                return s.rmdir_cbk (frame, cookie, this,
                                                    op_ret, op_errno,
                                                    preparent, postparent,
                                                    xdata)
                        self.stub_refs["rmdir_cbk"] = stub
                        dl.set_rmdir_cbk(c_this,stub)
