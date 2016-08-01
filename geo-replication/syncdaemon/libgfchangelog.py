#
# Copyright (c) 2011-2014 Red Hat, Inc. <http://www.redhat.com>
# This file is part of GlusterFS.

# This file is licensed to you under your choice of the GNU Lesser
# General Public License, version 3 or any later version (LGPLv3 or
# later), or the GNU General Public License, version 2 (GPLv2), in all
# cases as published by the Free Software Foundation.
#

import os
from ctypes import CDLL, RTLD_GLOBAL, create_string_buffer, get_errno, byref, c_ulong
from ctypes.util import find_library
from syncdutils import ChangelogException, ChangelogHistoryNotAvailable


class Changes(object):
    libgfc = CDLL(find_library("gfchangelog"), mode=RTLD_GLOBAL, use_errno=True)

    @classmethod
    def geterrno(cls):
        return get_errno()

    @classmethod
    def raise_changelog_err(cls):
        errn = cls.geterrno()
        raise ChangelogException(errn, os.strerror(errn))

    @classmethod
    def _get_api(cls, call):
        return getattr(cls.libgfc, call)

    @classmethod
    def cl_init(cls):
        ret = cls._get_api('gf_changelog_init')(None)
        if ret == -1:
            cls.raise_changelog_err()

    @classmethod
    def cl_register(cls, brick, path, log_file, log_level, retries=0):
        ret = cls._get_api('gf_changelog_register')(brick, path,
                                                    log_file,
                                                    log_level, retries)
        if ret == -1:
            cls.raise_changelog_err()

    @classmethod
    def cl_scan(cls):
        ret = cls._get_api('gf_changelog_scan')()
        if ret == -1:
            cls.raise_changelog_err()

    @classmethod
    def cl_startfresh(cls):
        ret = cls._get_api('gf_changelog_start_fresh')()
        if ret == -1:
            cls.raise_changelog_err()

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
                break
            changes.append(buf.raw[:ret - 1])
        if ret == -1:
            cls.raise_changelog_err()
        # cleanup tracker
        cls.cl_startfresh()
        return sorted(changes, key=clsort)

    @classmethod
    def cl_done(cls, clfile):
        ret = cls._get_api('gf_changelog_done')(clfile)
        if ret == -1:
            cls.raise_changelog_err()

    @classmethod
    def cl_history_scan(cls):
        ret = cls._get_api('gf_history_changelog_scan')()
        if ret == -1:
            cls.raise_changelog_err()

        return ret

    @classmethod
    def cl_history_changelog(cls, changelog_path, start, end, num_parallel):
        actual_end = c_ulong()
        ret = cls._get_api('gf_history_changelog')(changelog_path, start, end,
                                                   num_parallel,
                                                   byref(actual_end))
        if ret == -1:
            cls.raise_changelog_err()

        if ret == -2:
            raise ChangelogHistoryNotAvailable()

        return (ret, actual_end.value)

    @classmethod
    def cl_history_startfresh(cls):
        ret = cls._get_api('gf_history_changelog_start_fresh')()
        if ret == -1:
            cls.raise_changelog_err()

    @classmethod
    def cl_history_getchanges(cls):
        """ remove hardcoding for path name length """
        def clsort(f):
            return f.split('.')[-1]

        changes = []
        buf = create_string_buffer('\0', 4096)
        call = cls._get_api('gf_history_changelog_next_change')

        while True:
            ret = call(buf, 4096)
            if ret in (0, -1):
                break
            changes.append(buf.raw[:ret - 1])
        if ret == -1:
            cls.raise_changelog_err()

        return sorted(changes, key=clsort)

    @classmethod
    def cl_history_done(cls, clfile):
        ret = cls._get_api('gf_history_changelog_done')(clfile)
        if ret == -1:
            cls.raise_changelog_err()
