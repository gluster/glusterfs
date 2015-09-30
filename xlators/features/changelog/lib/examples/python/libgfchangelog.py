import os
from ctypes import *
from ctypes.util import find_library

class Changes(object):
    libgfc = CDLL(find_library("gfchangelog"), use_errno=True)

    @classmethod
    def geterrno(cls):
        return get_errno()

    @classmethod
    def raise_oserr(cls):
        errn = cls.geterrno()
        raise OSError(errn, os.strerror(errn))

    @classmethod
    def _get_api(cls, call):
        return getattr(cls.libgfc, call)

    @classmethod
    def cl_register(cls, brick, path, log_file, log_level, retries = 0):
        ret = cls._get_api('gf_changelog_register')(brick, path,
                                                    log_file, log_level, retries)
        if ret == -1:
            cls.raise_oserr()

    @classmethod
    def cl_scan(cls):
        ret = cls._get_api('gf_changelog_scan')()
        if ret == -1:
            cls.raise_oserr()

    @classmethod
    def cl_startfresh(cls):
        ret = cls._get_api('gf_changelog_start_fresh')()
        if ret == -1:
            cls.raise_oserr()

    @classmethod
    def cl_getchanges(cls):
        """ remove hardcoding for path name length """
        def clsort(f):
            return f.split('.')[-1]
        changes = []
        buf = create_string_buffer('\0', 4096)
        call = cls._get_api('gf_changelog_next_change')

        while True:
            ret = call(buf, 4096)
            if ret in (0, -1):
                break;
            changes.append(buf.raw[:ret-1])
        if ret == -1:
            cls.raise_oserr()
        # cleanup tracker
        cls.cl_startfresh()
        return sorted(changes, key=clsort)

    @classmethod
    def cl_done(cls, clfile):
        ret = cls._get_api('gf_changelog_done')(clfile)
        if ret == -1:
            cls.raise_oserr()
