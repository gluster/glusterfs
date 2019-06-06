#
# Copyright (c) 2018 Red Hat, Inc. <http://www.redhat.com>
# This file is part of GlusterFS.

# This file is licensed to you under your choice of the GNU Lesser
# General Public License, version 3 or any later version (LGPLv3 or
# later), or the GNU General Public License, version 2 (GPLv2), in all
# cases as published by the Free Software Foundation.
#

# All python2/python3 compatibility routines

import sys
import os
import stat
import struct
from ctypes import create_string_buffer

def umask():
    return os.umask(0)

if sys.version_info >= (3,):
    def pipe():
        (r, w) = os.pipe()
        os.set_inheritable(r, True)
        os.set_inheritable(w, True)
        return (r, w)

    # Raw conversion of bytearray to string. Used in the cases where
    # buffer is created by create_string_buffer which is a 8-bit char
    # array and passed to syscalls to fetch results. Using encode/decode
    # doesn't work as it converts to string altering the size.
    def bytearray_to_str(byte_arr):
        return ''.join([chr(b) for b in byte_arr])

    # Raw conversion of string to bytes. This is required to convert
    # back the string into bytearray(c char array) to use in struc
    # pack/unpacking. Again encode/decode can't be used as it
    # converts it alters size.
    def str_to_bytearray(string):
        return bytes([ord(c) for c in string])

    def gr_create_string_buffer(size):
        return create_string_buffer(b'\0', size)

    def gr_query_xattr(cls, path, size, syscall, attr=None):
        if attr:
            return cls._query_xattr(path.encode(), size, syscall,
                                    attr.encode())
        else:
            return cls._query_xattr(path.encode(), size, syscall)

    def gr_lsetxattr(cls, path, attr, val):
        return cls.libc.lsetxattr(path.encode(), attr.encode(), val,
                                  len(val), 0)

    def gr_lremovexattr(cls, path, attr):
        return cls.libc.lremovexattr(path.encode(), attr.encode())

    def gr_cl_register(cls, brick, path, log_file, log_level, retries):
        return cls._get_api('gf_changelog_register')(brick.encode(),
                                                     path.encode(),
                                                     log_file.encode(),
                                                     log_level, retries)

    def gr_cl_done(cls, clfile):
        return cls._get_api('gf_changelog_done')(clfile.encode())

    def gr_cl_history_changelog(cls, changelog_path, start, end, num_parallel,
                                actual_end):
        return cls._get_api('gf_history_changelog')(changelog_path.encode(),
                                                    start, end, num_parallel,
                                                    actual_end)

    def gr_cl_history_done(cls, clfile):
        return cls._get_api('gf_history_changelog_done')(clfile.encode())

    # regular file

    def entry_pack_reg(cls, gf, bn, mo, uid, gid):
        bn_encoded = bn.encode()
        blen = len(bn_encoded)
        return struct.pack(cls._fmt_mknod(blen),
                           uid, gid, gf.encode(), mo, bn_encoded,
                           stat.S_IMODE(mo), 0, umask())

    def entry_pack_reg_stat(cls, gf, bn, st):
        bn_encoded = bn.encode()
        blen = len(bn_encoded)
        mo = st['mode']
        return struct.pack(cls._fmt_mknod(blen),
                           st['uid'], st['gid'],
                           gf.encode(), mo, bn_encoded,
                           stat.S_IMODE(mo), 0, umask())
    # mkdir

    def entry_pack_mkdir(cls, gf, bn, mo, uid, gid):
        bn_encoded = bn.encode()
        blen = len(bn_encoded)
        return struct.pack(cls._fmt_mkdir(blen),
                           uid, gid, gf.encode(), mo, bn_encoded,
                           stat.S_IMODE(mo), umask())
    # symlink

    def entry_pack_symlink(cls, gf, bn, lnk, st):
        bn_encoded = bn.encode()
        blen = len(bn_encoded)
        lnk_encoded = lnk.encode()
        llen = len(lnk_encoded)
        return struct.pack(cls._fmt_symlink(blen, llen),
                           st['uid'], st['gid'],
                           gf.encode(), st['mode'], bn_encoded,
                           lnk_encoded)
else:
    def pipe():
        (r, w) = os.pipe()
        return (r, w)

    # Raw conversion of bytearray to string
    def bytearray_to_str(byte_arr):
        return byte_arr

    # Raw conversion of string to bytearray
    def str_to_bytearray(string):
        return string

    def gr_create_string_buffer(size):
        return create_string_buffer('\0', size)

    def gr_query_xattr(cls, path, size, syscall, attr=None):
        if attr:
            return cls._query_xattr(path, size, syscall, attr)
        else:
            return cls._query_xattr(path, size, syscall)

    def gr_lsetxattr(cls, path, attr, val):
        return cls.libc.lsetxattr(path, attr, val, len(val), 0)

    def gr_lremovexattr(cls, path, attr):
        return cls.libc.lremovexattr(path, attr)

    def gr_cl_register(cls, brick, path, log_file, log_level, retries):
        return cls._get_api('gf_changelog_register')(brick, path, log_file,
                                                     log_level, retries)

    def gr_cl_done(cls, clfile):
        return cls._get_api('gf_changelog_done')(clfile)

    def gr_cl_history_changelog(cls, changelog_path, start, end, num_parallel,
                                actual_end):
        return cls._get_api('gf_history_changelog')(changelog_path, start, end,
                                                    num_parallel, actual_end)

    def gr_cl_history_done(cls, clfile):
        return cls._get_api('gf_history_changelog_done')(clfile)

    # regular file

    def entry_pack_reg(cls, gf, bn, mo, uid, gid):
        blen = len(bn)
        return struct.pack(cls._fmt_mknod(blen),
                           uid, gid, gf, mo, bn,
                           stat.S_IMODE(mo), 0, umask())

    def entry_pack_reg_stat(cls, gf, bn, st):
        blen = len(bn)
        mo = st['mode']
        return struct.pack(cls._fmt_mknod(blen),
                           st['uid'], st['gid'],
                           gf, mo, bn,
                           stat.S_IMODE(mo), 0, umask())
    # mkdir

    def entry_pack_mkdir(cls, gf, bn, mo, uid, gid):
        blen = len(bn)
        return struct.pack(cls._fmt_mkdir(blen),
                           uid, gid, gf, mo, bn,
                           stat.S_IMODE(mo), umask())
    # symlink

    def entry_pack_symlink(cls, gf, bn, lnk, st):
        blen = len(bn)
        llen = len(lnk)
        return struct.pack(cls._fmt_symlink(blen, llen),
                           st['uid'], st['gid'],
                           gf, st['mode'], bn, lnk)
