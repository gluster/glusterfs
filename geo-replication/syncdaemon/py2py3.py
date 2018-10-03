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

else:
    def pipe():
        (r, w) = os.pipe()
        return (r, w)

    # Raw conversion of bytearray to string
    def bytearray_to_str(byte_arr):
        return ''.join([b for b in byte_arr])

    # Raw conversion of string to bytearray
    def str_to_bytearray(string):
        return b"".join([c for c in string])
