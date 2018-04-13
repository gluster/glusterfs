#!/usr/bin/python2

from __future__ import print_function
import os
import re
import sys
import string
import time
path = os.path.abspath(os.path.dirname(__file__)) + '/../../libglusterfs/src'
sys.path.append(path)
from generator import ops, xlator_cbks, xlator_dumpops

MAKEFILE_FMT = """
xlator_LTLIBRARIES = @XL_NAME@.la
xlatordir = $(libdir)/glusterfs/$(PACKAGE_VERSION)/xlator/@XL_TYPE@
@XL_NAME_NO_HYPHEN@_la_LDFLAGS = -module $(GF_XLATOR_DEFAULT_LDFLAGS)
@XL_NAME_NO_HYPHEN@_la_SOURCES = @XL_NAME@.c
@XL_NAME_NO_HYPHEN@_la_LIBADD = $(top_builddir)/libglusterfs/src/libglusterfs.la
noinst_HEADERS = @XL_NAME@.h @XL_NAME@-mem-types.h @XL_NAME@-messages.h
AM_CPPFLAGS = $(GF_CPPFLAGS) -I$(top_srcdir)/libglusterfs/src \
           -I$(top_srcdir)/rpc/xdr/src -I$(top_builddir)/rpc/xdr/src
AM_CFLAGS = -Wall -fno-strict-aliasing $(GF_CFLAGS)
CLEANFILES =
"""

fop_subs = {}
cbk_subs = {}
fn_subs = {}


def get_error_arg(type_str):
        if type_str.find(" *") != -1:
                return "NULL"
        return "-1"


def get_param(names, types):
        # Convert two separate tuples to one of (name, type) sub-tuples.
        as_tuples = zip(types, names)
        # Convert each sub-tuple into a "type name" string.
        as_strings = map(string.join, as_tuples)
        # Join all of those into one big string.
        return string.join(as_strings, ",\n\t")


def generate(tmpl, name, table):
        w_arg_names = [a[1] for a in table[name] if a[0] == 'fop-arg']
        w_arg_types = [a[2] for a in table[name] if a[0] == 'fop-arg']
        u_arg_names = [a[1] for a in table[name] if a[0] == 'cbk-arg']
        u_arg_types = [a[2] for a in table[name] if a[0] == 'cbk-arg']
        fn_arg_names = [a[1] for a in table[name] if a[0] == 'fn-arg']
        fn_arg_types = [a[2] for a in table[name] if a[0] == 'fn-arg']
        ret_type = [a[1] for a in table[name] if a[0] == 'ret-val']
        ret_var = [a[2] for a in table[name] if a[0] == 'ret-val']

        sdict = {}
        #Parameters are (t1, var1), (t2, var2)...
        #Args are (var1, var2,...)
        sdict["@WIND_ARGS@"] = string.join(w_arg_names, ", ")
        sdict["@UNWIND_ARGS@"] = string.join(u_arg_names, ", ")
        sdict["@ERROR_ARGS@"] = string.join(map(get_error_arg, u_arg_types), ", ")
        sdict["@WIND_PARAMS@"] = get_param(w_arg_names, w_arg_types)
        sdict["@UNWIND_PARAMS@"] = get_param(u_arg_names, u_arg_types)
        sdict["@FUNC_PARAMS@"] = get_param(fn_arg_names, fn_arg_types)
        sdict["@NAME@"] = name
        sdict["@FOP_PREFIX@"] = fop_prefix
        sdict["@RET_TYPE@"] = string.join(ret_type, "")
        sdict["@RET_VAR@"] = string.join(ret_var, "")

        for old, new in sdict.iteritems():
                tmpl = tmpl.replace(old, new)
        # TBD: reindent/reformat the result for maximum readability.
        return tmpl


def gen_xlator():
        xl = open(src_dir_path+"/"+xl_name+".c", 'w+')

        print(COPYRIGHT, file=xl)
        print(fragments["INCLUDE_IN_SRC_FILE"].replace("@XL_NAME@",
                                                              xl_name), file=xl)

        #Generate cbks and fops
        for fop in ops:
                print(generate(fragments["CBK_TEMPLATE"], fop, ops), file=xl)
                print(generate(fragments["FOP_TEMPLATE"], fop, ops), file=xl)

        for cbk in xlator_cbks:
                print(generate(fragments["FUNC_TEMPLATE"], cbk,
                                      xlator_cbks), file=xl)

        for dops in xlator_dumpops:
                print(generate(fragments["FUNC_TEMPLATE"], dops,
                                      xlator_dumpops), file=xl)

        print(fragments["XLATOR_METHODS"], file=xl)

        #Generate fop table
        print("struct xlator_fops fops = {", file=xl)
        for fop in ops:
                print("        .{0:20} = {1}_{2},".format(fop, fop_prefix, fop), file=xl)
        print("};", file=xl)

        #Generate xlator_cbks table
        print("struct xlator_cbks cbks = {", file=xl)
        for cbk in xlator_cbks:
                print("        .{0:20} = {1}_{2},".format(cbk, fop_prefix, cbk), file=xl)
        print("};", file=xl)

        #Generate xlator_dumpops table
        print("struct xlator_dumpops dumpops = {", file=xl)
        for dops in xlator_dumpops:
                print("        .{0:20} = {1}_{2},".format(dops, fop_prefix, dops), file=xl)
        print("};", file=xl)

        xl.close()


def create_dir_struct():
        if not os.path.exists(dir_path+"/src"):
                os.makedirs(dir_path+"/src")


def gen_header_files():
        upname = xl_name_no_hyphen.upper()
        h = open(src_dir_path+"/"+xl_name+".h", 'w+')
        print(COPYRIGHT, file=h)
        txt = fragments["HEADER_FMT"].replace("@HFL_NAME@", upname)
        txt2 = fragments["INCLUDE_IN_HEADER_FILE"].replace("@XL_NAME@", xl_name)
        txt = txt.replace("@INCLUDE_SECT@",txt2)
        print(txt, file=h)
        h.close()

        h = open(src_dir_path+"/"+xl_name+"-mem-types.h", 'w+')
        print(COPYRIGHT, file=h)
        txt = fragments["HEADER_FMT"].replace("@HFL_NAME@", upname+"_MEM_TYPES")
        txt = txt.replace("@INCLUDE_SECT@", '#include "mem-types.h"')
        print(txt, file=h)
        h.close()

        h = open(src_dir_path+"/"+xl_name+"-messages.h", 'w+')
        print(COPYRIGHT, file=h)
        txt = fragments["HEADER_FMT"].replace("@HFL_NAME@", upname+"_MESSAGES")
        txt = txt.replace("@INCLUDE_SECT@", '')
        print(txt, file=h)
        h.close()


def gen_makefiles():
        m = open(dir_path+"/Makefile.am", 'w+')
        print("SUBDIRS = src\n\nCLEANFILES =", file=m)
        m.close()

        m = open(src_dir_path+"/Makefile.am", 'w+')
        txt = MAKEFILE_FMT.replace("@XL_NAME@", xl_name)
        txt = txt.replace("@XL_NAME_NO_HYPHEN@", xl_name_no_hyphen)
        txt = txt.replace("@XL_TYPE@",xlator_type)
        print(txt, file=m)
        m.close()

def get_copyright ():
        return fragments["CP"].replace("@CURRENT_YEAR@",
                                       time.strftime("%Y"))

def load_fragments ():
        pragma_re = re.compile('pragma fragment (.*)')
        cur_symbol = None
        cur_value = ""
        result = {}
        basepath = os.path.abspath(os.path.dirname(__file__))
        fragpath = basepath + "/new-xlator-tmpl.c"
        for line in open(fragpath,"r").readlines():
                m = pragma_re.search(line)
                if m:
                        if cur_symbol:
                                result[cur_symbol] = cur_value
                        cur_symbol = m.group(1)
                        cur_value = ""
                else:
                        cur_value += line
        if cur_symbol:
                result[cur_symbol] = cur_value
        return result

if __name__ == '__main__':

        if len(sys.argv) < 3:
                print("USAGE: ./gen_xlator <XLATOR_DIR> <XLATOR_NAME> <FOP_PREFIX>")
                sys.exit(0)

        xl_name = sys.argv[2]
        xl_name_no_hyphen = xl_name.replace("-", "_")
        if sys.argv[1].endswith('/'):
                dir_path = sys.argv[1] + xl_name
        else:
                dir_path = sys.argv[1] + "/" + xl_name
        xlator_type = os.path.basename(sys.argv[1])
        fop_prefix = sys.argv[3]
        src_dir_path = dir_path + "/src"

        fragments = load_fragments()

        COPYRIGHT = get_copyright()
        create_dir_struct()
        gen_xlator()
        gen_header_files()
        gen_makefiles()
