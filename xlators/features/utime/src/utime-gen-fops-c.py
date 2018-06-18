#!/usr/bin/python

import os
import sys

curdir = os.path.dirname(sys.argv[0])
gendir = os.path.join(curdir, '../../../../libglusterfs/src')
sys.path.append(gendir)
from generator import ops, fop_subs, cbk_subs, generate

FOPS_COMMON_TEMPLATE = """
int32_t
gf_utime_@NAME@ (call_frame_t *frame, xlator_t *this,
                @LONG_ARGS@)
{
        gl_timespec_get(&frame->root->ctime);

        (void) utime_update_attribute_flags(frame, GF_FOP_@UPNAME@);
        STACK_WIND (frame, gf_utime_@NAME@_cbk, FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->@NAME@, @SHORT_ARGS@);
        return 0;
}
"""

FOPS_CBK_COMMON_TEMPLATE = """
int32_t
gf_utime_@NAME@_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                     int32_t op_ret, int32_t op_errno,
                    @LONG_ARGS@)
{
        STACK_UNWIND_STRICT (@NAME@, frame, op_ret, op_errno, @SHORT_ARGS@);
        return 0;
}
"""

FOPS_READ_TEMPLATE = """
int32_t
gf_utime_@NAME@ (call_frame_t *frame, xlator_t *this,
                @LONG_ARGS@)
{
        gl_timespec_get(&frame->root->ctime);

        (void) utime_update_attribute_flags(frame, GF_FOP_READ);
        STACK_WIND (frame, gf_utime_@NAME@_cbk, FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->@NAME@, @SHORT_ARGS@);
        return 0;
}
"""

FOPS_WRITE_TEMPLATE = """
int32_t
gf_utime_@NAME@ (call_frame_t *frame, xlator_t *this,
                @LONG_ARGS@)
{
        gl_timespec_get(&frame->root->ctime);

        (void) utime_update_attribute_flags(frame, GF_FOP_WRITE);
        STACK_WIND (frame, gf_utime_@NAME@_cbk, FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->@NAME@, @SHORT_ARGS@);
        return 0;
}
"""

FOPS_SETATTR_TEMPLATE = """
int32_t
gf_utime_@NAME@ (call_frame_t *frame, xlator_t *this,
             @LONG_ARGS@)
{
        gl_timespec_get(&frame->root->ctime);

        if (!valid) {
                frame->root->flags |= MDATA_CTIME;
        }

        if (valid & (GF_SET_ATTR_UID | GF_SET_ATTR_GID)) {
                frame->root->flags |= MDATA_CTIME;
        }

        if (valid & GF_SET_ATTR_MODE) {
                frame->root->flags |= MDATA_CTIME;
        }

        STACK_WIND (frame, gf_utime_@NAME@_cbk, FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->@NAME@, @SHORT_ARGS@);
        return 0;
}
"""

utime_ops = ['fallocate', 'zerofill', 'opendir', 'mknod', 'mkdir',
             'unlink', 'rmdir', 'symlink', 'rename', 'link', 'truncate',
             'ftruncate', 'create', 'open', 'removexattr', 'fremovexattr']

utime_read_op = ['readv']
utime_write_op = ['writev']
utime_setattr_ops = ['setattr', 'fsetattr']

def gen_defaults():
    for name in ops:
        if name in utime_ops:
            print generate(FOPS_CBK_COMMON_TEMPLATE, name, cbk_subs)
            print generate(FOPS_COMMON_TEMPLATE, name, fop_subs)
        if name in utime_read_op:
            print generate(FOPS_CBK_COMMON_TEMPLATE, name, cbk_subs)
            print generate(FOPS_READ_TEMPLATE, name, fop_subs)
        if name in utime_write_op:
            print generate(FOPS_CBK_COMMON_TEMPLATE, name, cbk_subs)
            print generate(FOPS_WRITE_TEMPLATE, name, fop_subs)
        if name in utime_setattr_ops:
            print generate(FOPS_CBK_COMMON_TEMPLATE, name, cbk_subs)
            print generate(FOPS_SETATTR_TEMPLATE, name, fop_subs)

for l in open(sys.argv[1], 'r').readlines():
    if l.find('#pragma generate') != -1:
        print "/* BEGIN GENERATED CODE - DO NOT MODIFY */"
        gen_defaults()
        print "/* END GENERATED CODE */"
    else:
        print l[:-1]
