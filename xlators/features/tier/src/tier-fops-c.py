#!/usr/bin/python3

#
# Copyright (c) 2021 Pavilion Data Systems, Inc. <http://www.pavilion.io>
#

from __future__ import print_function
import os
import sys

curdir = os.path.dirname(sys.argv[0])
gendir = os.path.join(curdir, '../../../../libglusterfs/src')
sys.path.append(gendir)
from generator import ops, fop_subs, cbk_subs, generate

FD_DATA_MODIFYING_OP_FOP_TEMPLATE = """
int32_t
tier_@NAME@ (call_frame_t *frame, xlator_t *this,
             @LONG_ARGS@)
{
        int                         op_errno        = EINVAL ;
        tier_local_t                 *local           = NULL;
        int                         ret             = 0;

        local = tier_local_init (this, frame, NULL, fd, GF_FOP_@UPNAME@);
        if (!local) {
                gf_msg (this->name, GF_LOG_ERROR, 0, 0, "local init failed");
                op_errno = ENOMEM;
                goto err;
        }

        if (local->state == GF_TIER_LOCAL) {
                STACK_WIND (frame, tier_@NAME@_cbk,
                            FIRST_CHILD(this), FIRST_CHILD(this)->fops->@NAME@,
                            @SHORT_ARGS@);
                return 0;
        }
        local->stub = fop_@NAME@_stub (frame, tier_resume_@NAME@,
                                       @SHORT_ARGS@);
        if (!local->stub) {
              gf_msg (this->name, GF_LOG_ERROR, 0, 0, "insufficient memory");
              op_errno = ENOMEM;
              goto err;
        }

        local->call_cnt++;
        ret = locate_and_execute (frame);
        if (ret) {
            op_errno = ENOMEM;
            goto err;
        }

        return 0;

err:
        TIER_STACK_UNWIND (@NAME@, frame, -1, op_errno, @CBK_ERROR_ARGS@);

        return 0;
}
"""

FD_DATA_MODIFYING_RESUME_OP_FOP_TEMPLATE = """
int32_t
tier_resume_@NAME@ (call_frame_t *frame, xlator_t *this,
                    @LONG_ARGS@)
{
        int              ret    = 0;

        ret = tier_resume_postprocess (this, frame, fd->inode);
        if (ret) {
                goto unwind;
        }

        STACK_WIND (frame, tier_@NAME@_cbk,
                    FIRST_CHILD(this), FIRST_CHILD(this)->fops->@NAME@,
                    @SHORT_ARGS@);

        return 0;

unwind:
        tier_common_cbk(frame);

        return 0;
}
"""
FD_DATA_MODIFYING_OP_FOP_CBK_TEMPLATE = """
int32_t
tier_@NAME@_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                int32_t op_ret, int32_t op_errno,
                @LONG_ARGS@)
{
        TIER_STACK_UNWIND(@NAME@, frame, op_ret, op_errno, @SHORT_ARGS@);

        return 0;
}
"""


# All xlator FOPs are covered in the following section just to create a clarity
# The lists themselves are not used.
entry_ops = ['mknod', 'mkdir', 'unlink', 'rmdir', 'symlink', 'rename', 'link',
             'create']
special_ops = ['statfs', 'lookup', 'ipc', 'compound', 'icreate', 'namelink']
ignored_ops = ['getspec']
inode_ops = ['stat', 'readlink', 'truncate', 'open', 'setxattr', 'getxattr',
             'removexattr', 'opendir', 'access', 'inodelk', 'entrylk',
             'xattrop', 'setattr', 'lease', 'getactivelk', 'setactivelk',
             'discover']
fd_ops = ['readv', 'writev', 'flush', 'fsync', 'fsyncdir', 'ftruncate',
          'fstat', 'lk', 'readdir', 'finodelk', 'fentrylk', 'fxattrop',
          'fsetxattr', 'fgetxattr', 'rchecksum', 'fsetattr', 'readdirp',
          'fremovexattr', 'fallocate', 'discard', 'zerofill', 'seek']


# These are the current actual lists used to generate the code

# The following list contains fops which are fd based that modifies data
fd_data_modify_op_fop_template = ['rchecksum', 'fallocate',
                                  'discard', 'zerofill']


# These fops need a separate implementation
special_fops = ['statfs', 'setxattr', 'unlink', 'getxattr',
                'truncate', 'fstat', 'readv', 'readdirp']

def gen_defaults():
    for name in ops:
        if name in fd_data_modify_op_fop_template:
            print(generate(FD_DATA_MODIFYING_OP_FOP_CBK_TEMPLATE, name, cbk_subs))
            print(generate(FD_DATA_MODIFYING_RESUME_OP_FOP_TEMPLATE, name, fop_subs))
            print(generate(FD_DATA_MODIFYING_OP_FOP_TEMPLATE, name, fop_subs))

for l in open(sys.argv[1], 'r').readlines():
    if l.find('#pragma generate') != -1:
        print("/* BEGIN GENERATED CODE - DO NOT MODIFY */")
        gen_defaults()
        print("/* END GENERATED CODE */")
    else:
        print(l[:-1])
