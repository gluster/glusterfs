#
# Copyright (c) 2018 Red Hat, Inc. <http://www.redhat.com>
# This file is part of GlusterFS.

# This file is licensed to you under your choice of the GNU Lesser
# General Public License, version 3 or any later version (LGPLv3 or
# later), or the GNU General Public License, version 2 (GPLv2), in all
# cases as published by the Free Software Foundation.
#

# All python2/python3 compatibility routines

import os
import sys
from ctypes import create_string_buffer

if sys.version_info >= (3,):

    # Raw conversion of bytearray to string. Used in the cases where
    # buffer is created by create_string_buffer which is a 8-bit char
    # array and passed to syscalls to fetch results. Using encode/decode
    # doesn't work as it converts to string altering the size.
    # def bytearray_to_str(byte_arr):
    def bytearray_to_str(byte_arr):
        return ''.join([chr(b) for b in byte_arr])

    def gf_create_string_buffer(size):
        return create_string_buffer(b'\0', size)

    def gfind_history_changelog(libgfc, changelog_path, start, end, num_parallel,
                                actual_end):
        return libgfc.gf_history_changelog(changelog_path.encode(), start, end, num_parallel,
                       actual_end)

    def gfind_changelog_register(libgfc, brick, path, log_file, log_level,
                                 retries):
        return libgfc.gf_changelog_register(brick.encode(), path.encode(), log_file.encode(),
                       log_level, retries)

    def gfind_history_changelog_done(libgfc, clfile):
        return libgfc.gf_history_changelog_done(clfile.encode())

    def gfind_write_row(f, row, field_separator, p_rep, row_2_rep):
        f.write(u"{0}{1}{2}{3}{4}\n".format(row,
                                            field_separator,
                                            p_rep,
                                            field_separator,
                                            row_2_rep))

    def gfind_write(f, row, field_separator, p_rep):
        f.write(u"{0}{1}{2}\n".format(row,
                                      field_separator,
                                      p_rep))


else:

    # Raw conversion of bytearray to string
    def bytearray_to_str(byte_arr):
        return byte_arr

    def gf_create_string_buffer(size):
        return create_string_buffer('\0', size)

    def gfind_history_changelog(libgfc, changelog_path, start, end, num_parallel,
                                actual_end):
        return libgfc.gf_history_changelog(changelog_path, start, end,
                                                    num_parallel, actual_end)

    def gfind_changelog_register(libgfc, brick, path, log_file, log_level,
                                 retries):
        return libgfc.gf_changelog_register(brick, path, log_file,
                                                     log_level, retries)

    def gfind_history_changelog_done(libgfc, clfile):
        return libgfc.gf_history_changelog_done(clfile)

    def gfind_write_row(f, row, field_separator, p_rep, row_2_rep):
        f.write(u"{0}{1}{2}{3}{4}\n".format(row,
                                            field_separator,
                                            p_rep,
                                            field_separator,
                                            row_2_rep).encode())

    def gfind_write(f, row, field_separator, p_rep):
        f.write(u"{0}{1}{2}\n".format(row,
                                      field_separator,
                                      p_rep).encode())
