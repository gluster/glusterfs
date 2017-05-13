# -*- coding: utf-8 -*-

# Copyright (c) 2015 Red Hat, Inc. <http://www.redhat.com/>
# This file is part of GlusterFS.
#
# This file is licensed to you under your choice of the GNU Lesser
# General Public License, version 3 or any later version (LGPLv3 or
# later), or the GNU General Public License, version 2 (GPLv2), in all
# cases as published by the Free Software Foundation.

import os
from ctypes import CDLL, get_errno, create_string_buffer, c_ulong, byref
from ctypes import RTLD_GLOBAL


class ChangelogException(OSError):
    pass


libgfc = CDLL("libgfchangelog.so", use_errno=True, mode=RTLD_GLOBAL)


def raise_oserr(prefix=None):
    errn = get_errno()
    prefix_or_empty = prefix + ": " if prefix else ""
    raise ChangelogException(errn, prefix_or_empty + os.strerror(errn))


def cl_init():
    ret = libgfc.gf_changelog_init(None)
    if ret == -1:
        raise_oserr(prefix="gf_changelog_init")


def cl_register(brick, path, log_file, log_level, retries=0):
    ret = libgfc.gf_changelog_register(brick, path, log_file,
                                       log_level, retries)
    if ret == -1:
        raise_oserr(prefix="gf_changelog_register")


def cl_history_scan():
    ret = libgfc.gf_history_changelog_scan()
    if ret == -1:
        raise_oserr(prefix="gf_history_changelog_scan")

    return ret


def cl_history_changelog(changelog_path, start, end, num_parallel):
    actual_end = c_ulong()
    ret = libgfc.gf_history_changelog(changelog_path, start, end,
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
    buf = create_string_buffer('\0', 4096)

    while True:
        ret = libgfc.gf_history_changelog_next_change(buf, 4096)
        if ret in (0, -1):
            break
        changes.append(buf.raw[:ret - 1])
    if ret == -1:
        raise_oserr(prefix="gf_history_changelog_next_change")

    return sorted(changes, key=clsort)


def cl_history_done(clfile):
    ret = libgfc.gf_history_changelog_done(clfile)
    if ret == -1:
        raise_oserr(prefix="gf_history_changelog_done")
