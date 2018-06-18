#!/usr/bin/python

import os
import sys

curdir = os.path.dirname(sys.argv[0])
gendir = os.path.join(curdir, '../../../../libglusterfs/src')
sys.path.append(gendir)
from generator import ops, fop_subs, generate

OP_FOP_TEMPLATE = """
int32_t
gf_utime_@NAME@ (call_frame_t *frame, xlator_t *this,
                @LONG_ARGS@);
"""

utime_ops = ['fallocate', 'zerofill', 'opendir', 'mknod', 'mkdir',
             'unlink', 'rmdir', 'symlink', 'rename', 'link', 'truncate',
             'ftruncate', 'create', 'open', 'removexattr', 'fremovexattr',
             'readv', 'writev', 'setattr', 'fsetattr']

def gen_defaults():
    for name, value in ops.iteritems():
        if name in utime_ops:
            print generate(OP_FOP_TEMPLATE, name, fop_subs)


for l in open(sys.argv[1], 'r').readlines():
    if l.find('#pragma generate') != -1:
        print "/* BEGIN GENERATED CODE - DO NOT MODIFY */"
        gen_defaults()
        print "/* END GENERATED CODE */"
    else:
        print l[:-1]
