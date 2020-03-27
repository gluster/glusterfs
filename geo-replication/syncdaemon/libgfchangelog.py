#
# Copyright (c) 2011-2014 Red Hat, Inc. <http://www.redhat.com>
# This file is part of GlusterFS.

# This file is licensed to you under your choice of the GNU Lesser
# General Public License, version 3 or any later version (LGPLv3 or
# later), or the GNU General Public License, version 2 (GPLv2), in all
# cases as published by the Free Software Foundation.
#

import os
from ctypes import CDLL, RTLD_GLOBAL, get_errno, byref, c_ulong
from ctypes.util import find_library
from syncdutils import ChangelogException, ChangelogHistoryNotAvailable
from py2py3 import (gr_cl_history_changelog, gr_cl_done,
                    gr_create_string_buffer, gr_cl_register,
                    gr_cl_history_done, bytearray_to_str)


libgfc = CDLL(
    find_library("gfchangelog"),
    mode=RTLD_GLOBAL,
    use_errno=True
)


def _raise_changelog_err():
    errn = get_errno()
    raise ChangelogException(errn, os.strerror(errn))


def _init():
    if libgfc.gf_changelog_init(None) == -1:
        _raise_changelog_err()


def register(brick, path, log_file, log_level, retries=0):
    _init()

    ret = gr_cl_register(libgfc, brick, path, log_file, log_level, retries)

    if ret == -1:
        _raise_changelog_err()


def scan():
    ret = libgfc.gf_changelog_scan()
    if ret == -1:
        _raise_changelog_err()


def startfresh():
    ret = libgfc.gf_changelog_start_fresh()
    if ret == -1:
        _raise_changelog_err()


def getchanges():
    def clsort(cfile):
        return cfile.split('.')[-1]

    changes = []
    buf = gr_create_string_buffer(4096)
    call = libgfc.gf_changelog_next_change

    while True:
        ret = call(buf, 4096)
        if ret in (0, -1):
            break

        # py2 and py3 compatibility
        result = bytearray_to_str(buf.raw[:ret - 1])
        changes.append(result)

    if ret == -1:
        _raise_changelog_err()

    # cleanup tracker
    startfresh()

    return sorted(changes, key=clsort)


def done(clfile):
    ret = gr_cl_done(libgfc, clfile)
    if ret == -1:
        _raise_changelog_err()


def history_scan():
    ret = libgfc.gf_history_changelog_scan()
    if ret == -1:
        _raise_changelog_err()

    return ret


def history_changelog(changelog_path, start, end, num_parallel):
    actual_end = c_ulong()
    ret = gr_cl_history_changelog(libgfc, changelog_path, start, end,
                                  num_parallel, byref(actual_end))
    if ret == -1:
        _raise_changelog_err()

    if ret == -2:
        raise ChangelogHistoryNotAvailable()

    return (ret, actual_end.value)


def history_startfresh():
    ret = libgfc.gf_history_changelog_start_fresh()
    if ret == -1:
        _raise_changelog_err()


def history_getchanges():
    def clsort(cfile):
        return cfile.split('.')[-1]

    changes = []
    buf = gr_create_string_buffer(4096)
    call = libgfc.gf_history_changelog_next_change

    while True:
        ret = call(buf, 4096)
        if ret in (0, -1):
            break

        # py2 and py3 compatibility
        result = bytearray_to_str(buf.raw[:ret - 1])
        changes.append(result)

    if ret == -1:
        _raise_changelog_err()

    return sorted(changes, key=clsort)


def history_done(clfile):
    ret = gr_cl_history_done(libgfc, clfile)
    if ret == -1:
        _raise_changelog_err()
