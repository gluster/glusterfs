#!/usr/bin/python

from __future__ import print_function
import os
import sys

curdir = os.path.dirname(sys.argv[0])
gendir = os.path.join(curdir, '../../../../libglusterfs/src')
sys.path.append(gendir)
from generator import ops, fop_subs, cbk_subs, generate

OP_FOP_TEMPLATE = """
int32_t
cs_@NAME@ (call_frame_t *frame, xlator_t *this,
                   @LONG_ARGS@);
"""

def gen_defaults():
    for name, value in ops.iteritems():
        if name == 'getspec':
            continue
        print(generate(OP_FOP_TEMPLATE, name, fop_subs))


for l in open(sys.argv[1], 'r').readlines():
    if l.find('#pragma generate') != -1:
        print("/* BEGIN GENERATED CODE - DO NOT MODIFY */")
        gen_defaults()
        print("/* END GENERATED CODE */")
    else:
        print(l[:-1])
