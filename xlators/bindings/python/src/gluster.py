#   Copyright (c) 2007 Chris AtLee <chris@atlee.ca>
#   This file is part of GlusterFS.
#
#   GlusterFS is free software; you can redistribute it and/or modify
#   it under the terms of the GNU General Public License as published
#   by the Free Software Foundation; either version 3 of the License,
#   or (at your option) any later version.
#
#   GlusterFS is distributed in the hope that it will be useful, but
#   WITHOUT ANY WARRANTY; without even the implied warranty of
#   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
#   General Public License for more details.
#
#   You should have received a copy of the GNU General Public License
#   along with this program.  If not, see
#   <http://www.gnu.org/licenses/>.
from ctypes import *
from glustertypes import *
from glusterstack import *
import sys
import inspect

libglusterfs = CDLL("libglusterfs.so")
_gf_log = libglusterfs._gf_log
_gf_log.restype = c_int32
_gf_log.argtypes = [c_char_p, c_char_p, c_char_p, c_int32, c_int, c_char_p]

gf_log_loglevel = c_int.in_dll(libglusterfs, "gf_log_loglevel")

GF_LOG_NONE = 0
GF_LOG_CRITICAL = 1
GF_LOG_ERROR = 2
GF_LOG_WARNING = 3
GF_LOG_DEBUG = 4

def gf_log(module, level, fmt, *params):
    if level <= gf_log_loglevel:
        frame = sys._getframe(1)
        _gf_log(module, frame.f_code.co_filename, frame.f_code.co_name,
                frame.f_lineno, level, fmt, *params)

class ComplexTranslator(object):
    def __init__(self, xlator):
        self.xlator = xlator_t.from_address(xlator)

    def __getattr__(self, item):
        return getattr(self.xlator, item)
