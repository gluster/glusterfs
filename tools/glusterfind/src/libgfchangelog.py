# -*- coding: utf-8 -*-

# Copyright (c) 2015 Red Hat, Inc. <http://www.redhat.com/>
# This file is part of GlusterFS.
#
# This file is licensed to you under your choice of the GNU Lesser
# General Public License, version 3 or any later version (LGPLv3 or
# later), or the GNU General Public License, version 2 (GPLv2), in all
# cases as published by the Free Software Foundation.

import os
from ctypes import CDLL, RTLD_GLOBAL, get_errno, create_string_buffer, c_ulong, byref
from ctypes.util import find_library
from gfind_py2py3 import bytearray_to_str, gf_create_string_buffer
from gfind_py2py3 import gfind_history_changelog, gfind_changelog_register
from gfind_py2py3 import gfind_history_changelog_done


class ChangelogException(OSError):
    pass

libgfc = CDLL(find_library("gfchangelog"), mode=RTLD_GLOBAL, use_errno=True)


def raise_oserr(prefix=None):
    errn = get_errno()
    prefix_or_empty = prefix + ": " if prefix else ""
    raise ChangelogException(errn, prefix_or_empty + os.strerror(errn))


def cl_init():
    ret = libgfc.gf_changelog_init(None)
    if ret == -1:
        raise_oserr(prefix="gf_changelog_init")


def cl_register(brick, path, log_file, log_level, retries=0):
    ret = gfind_changelog_register(libgfc, brick, path, log_file,log_level, retries)
    if ret == -1:
        raise_oserr(prefix="gf_changelog_register")


def cl_history_scan():
    ret = libgfc.gf_history_changelog_scan()
    if ret == -1:
        raise_oserr(prefix="gf_history_changelog_scan")

    return ret


def cl_history_changelog(changelog_path, start, end, num_parallel):
    actual_end = c_ulong()
    ret = gfind_history_changelog(libgfc,changelog_path, start, end,
                                      num_parallel,
                                      byref(actual_end))
    if ret == -1:
        raise_oserr(prefix="gf_history_changelog")

    return actual_end.value


def cl_history_startfresh():
    ret = libgfc.gf_history_changelog_start_fresh()
    if ret == -1:
        raise_oserr(prefix="gf_history_changelog_start_fresh")


def cl_history_getchanges():
    """ remove hardcoding for path name length """
    def clsort(f):
        return f.split('.')[-1]

    changes = []
    buf = gf_create_string_buffer(4096)

    while True:
        ret = libgfc.gf_history_changelog_next_change(buf, 4096)
        if ret in (0, -1):
            break
        # py2 and py3 compatibility
        result = bytearray_to_str(buf.raw[:ret - 1])
        changes.append(result)
    if ret == -1:
        raise_oserr(prefix="gf_history_changelog_next_change")

    return sorted(changes, key=clsort)


def cl_history_done(clfile):
    ret = gfind_history_changelog_done(libgfc, clfile)
    if ret == -1:
        raise_oserr(prefix="gf_history_changelog_done")
