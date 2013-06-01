import os
from ctypes import *
from ctypes.util import find_library

class Xattr(object):
    """singleton that wraps the extended attribues system
       interface for python using ctypes

       Just implement it to the degree we need it, in particular
       - we need just the l*xattr variants, ie. we never want symlinks to be
         followed
       - don't need size discovery for getxattr, as we always know the exact
         sizes we expect
    """

    libc = CDLL(find_library("libc"))

    @classmethod
    def geterrno(cls):
        return c_int.in_dll(cls.libc, 'errno').value

    @classmethod
    def raise_oserr(cls):
        errn = cls.geterrno()
        raise OSError(errn, os.strerror(errn))

    @classmethod
    def _query_xattr(cls, path, siz, syscall, *a):
        if siz:
            buf = create_string_buffer('\0' * siz)
        else:
            buf = None
        ret = getattr(cls.libc, syscall)(*((path,) + a + (buf, siz)))
        if ret == -1:
            cls.raise_oserr()
        if siz:
            return buf.raw[:ret]
        else:
            return ret

    @classmethod
    def lgetxattr(cls, path, attr, siz=0):
        return cls._query_xattr( path, siz, 'lgetxattr', attr)

    @classmethod
    def lgetxattr_buf(cls, path, attr):
        """lgetxattr variant with size discovery"""
        size = cls.lgetxattr(path, attr)
        if size == -1:
            cls.raise_oserr()
        if size == 0:
            return ''
        return cls.lgetxattr(path, attr, size)

    @classmethod
    def llistxattr(cls, path, siz=0):
        ret = cls._query_xattr(path, siz, 'llistxattr')
        if isinstance(ret, str):
            ret = ret.split('\0')
        return ret

    @classmethod
    def lsetxattr(cls, path, attr, val):
        ret = cls.libc.lsetxattr(path, attr, val, len(val), 0)
        if ret == -1:
            cls.raise_oserr()

    @classmethod
    def lsetxattr_l(cls, path, attr, val):
        """ lazy lsetxattr(): caller handles errno """
        cls.libc.lsetxattr(path, attr, val, len(val), 0)

    @classmethod
    def lremovexattr(cls, path, attr):
        ret = cls.libc.lremovexattr(path, attr)
        if ret == -1:
            cls.raise_oserr()

    @classmethod
    def llistxattr_buf(cls, path):
        """listxattr variant with size discovery"""
        size = cls.llistxattr(path)
        if size == -1:
            cls.raise_oserr()
        if size == 0:
            return []
        return cls.llistxattr(path, size)
