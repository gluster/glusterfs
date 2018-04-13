#!/usr/bin/python

from __future__ import print_function
import os
import sys

curdir = os.path.dirname(sys.argv[0])
gendir = os.path.join(curdir, '../../../../libglusterfs/src')
sys.path.append(gendir)
from generator import ops, fop_subs, cbk_subs, generate

FD_DATA_MODIFYING_OP_FOP_TEMPLATE = """
int32_t
cs_@NAME@ (call_frame_t *frame, xlator_t *this,
           @LONG_ARGS@)
{
        int                         op_errno        = -1;
        cs_local_t                 *local           = NULL;
        int                         ret             = 0;
        cs_inode_ctx_t             *ctx             = NULL;
        gf_cs_obj_state             state           = -1;

        VALIDATE_OR_GOTO (frame, err);
        VALIDATE_OR_GOTO (this, err);
        VALIDATE_OR_GOTO (fd, err);

        local = cs_local_init (this, frame, NULL, fd, GF_FOP_@UPNAME@);
        if (!local) {

                gf_msg (this->name, GF_LOG_ERROR, 0, 0, "local init failed");
                op_errno = ENOMEM;
                goto err;
        }

        __cs_inode_ctx_get (this, fd->inode, &ctx);

        if (ctx)
                state = __cs_get_file_state (this, fd->inode, ctx);
        else
                state = GF_CS_LOCAL;

        local->xattr_req = xdata ? dict_ref (xdata) : (xdata = dict_new ());

        ret = dict_set_uint32 (local->xattr_req, GF_CS_OBJECT_STATUS, 1);
        if (ret) {
                gf_msg (this->name, GF_LOG_ERROR, 0, 0, "dict_set failed key:"
                        " %s", GF_CS_OBJECT_STATUS);
                goto err;
        }

        local->stub = fop_@NAME@_stub (frame, cs_resume_@NAME@,
                                       @SHORT_ARGS@);
        if (!local->stub) {
                gf_msg (this->name, GF_LOG_ERROR, 0, 0, "insufficient memory");
                op_errno = ENOMEM;
                goto err;
        }


        if (state == GF_CS_LOCAL) {
                STACK_WIND (frame, cs_@NAME@_cbk,
                            FIRST_CHILD(this), FIRST_CHILD(this)->fops->@NAME@,
                            @SHORT_ARGS@);
        } else {
                local->call_cnt++;
                ret = locate_and_execute (frame);
                if (ret) {
                        op_errno = ENOMEM;
                        goto err;
                }
        }

        return 0;

err:
        CS_STACK_UNWIND (@NAME@, frame, -1, op_errno, @CBK_ERROR_ARGS@);

        return 0;
}
"""

FD_DATA_MODIFYING_RESUME_OP_FOP_TEMPLATE = """
int32_t
cs_resume_@NAME@ (call_frame_t *frame, xlator_t *this,
                  @LONG_ARGS@)
{
        int              ret    = 0;

        ret = cs_resume_postprocess (this, frame, fd->inode);
        if (ret) {
                goto unwind;
        }

        cs_inodelk_unlock (frame);

        STACK_WIND (frame, cs_@NAME@_cbk,
                    FIRST_CHILD(this), FIRST_CHILD(this)->fops->@NAME@,
                    @SHORT_ARGS@);

        return 0;

unwind:

        cs_inodelk_unlock (frame);

        cs_common_cbk (frame);

        return 0;
}
"""
FD_DATA_MODIFYING_OP_FOP_CBK_TEMPLATE = """
int32_t
cs_@NAME@_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
               int32_t op_ret, int32_t op_errno,
               @LONG_ARGS@)
{
        cs_local_t      *local = NULL;
        int              ret = 0;
        uint64_t         val = 0;
        fd_t            *fd = NULL;

        local = frame->local;
        fd = local->fd;

        /* Do we need lock here? */
        local->call_cnt++;

        if (op_ret == -1) {
                ret = dict_get_uint64 (xdata, GF_CS_OBJECT_STATUS, &val);
                if (ret == 0) {
                        if (val == GF_CS_ERROR) {
                                gf_msg (this->name, GF_LOG_ERROR, 0, 0,
                                        "could not get file state, unwinding");
                                op_ret = -1;
                                op_errno = EIO;
                                goto unwind;
                        } else {
                                __cs_inode_ctx_update (this, fd->inode, val);
                                gf_msg (this->name, GF_LOG_INFO, 0, 0,
                                        " state = %ld", val);

                                if (local->call_cnt == 1 &&
                                    (val == GF_CS_REMOTE ||
                                     val == GF_CS_DOWNLOADING))  {
                                        gf_msg (this->name, GF_LOG_INFO, 0,
                                                0, " will repair and download "
                                                "the file, current state : %ld",
                                                val);
                                        goto repair;
                                } else {
                                        gf_msg (this->name, GF_LOG_ERROR, 0, 0,
                                                "second @NAME@, Unwinding");
                                        goto unwind;
                                }
                        }
                } else {
                        gf_msg (this->name, GF_LOG_ERROR, 0, 0, "file state "
                                "could not be figured, unwinding");
                        goto unwind;
                }
        } else {
                /* successful @NAME@ => file is local */
                __cs_inode_ctx_update (this, fd->inode, GF_CS_LOCAL);
                gf_msg (this->name, GF_LOG_INFO, 0, 0, "state : GF_CS_LOCAL"
                        ", @NAME@ successful");

                goto unwind;
        }

repair:
        ret = locate_and_execute (frame);
        if (ret) {
                goto unwind;
        }

        return 0;

unwind:
        CS_STACK_UNWIND (@NAME@, frame, op_ret, op_errno, @SHORT_ARGS@);

        return 0;
}
"""

LOC_STAT_OP_FOP_TEMPLATE = """
int32_t
cs_@NAME@ (call_frame_t *frame, xlator_t *this,
           @LONG_ARGS@)
{
        cs_local_t      *local = NULL;
        int              ret   = 0;

        local = cs_local_init (this, frame, loc, NULL, GF_FOP_@UPNAME@);
        if (!local) {
                gf_msg (this->name, GF_LOG_ERROR, 0, 0, "local is NULL");
                goto err;
        }

        if (loc->inode->ia_type == IA_IFDIR)
                goto wind;

        local->xattr_req = xdata ? dict_ref (xdata) : dict_new ();

        ret = dict_set_uint32 (local->xattr_req, GF_CS_OBJECT_STATUS, 1);
        if (ret) {
                gf_msg (this->name, GF_LOG_ERROR, 0, 0, "dict_set failed key:"
                        " %s", GF_CS_OBJECT_STATUS);
                goto err;
        }

wind:
        STACK_WIND (frame, cs_@NAME@_cbk, FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->@NAME@,
                    @SHORT_ARGS@);

        return 0;
err:
        CS_STACK_UNWIND (@NAME@, frame, -1, errno, @CBK_ERROR_ARGS@);

        return 0;
}
"""

LOC_STAT_OP_FOP_CBK_TEMPLATE = """
int32_t
cs_@NAME@_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
               int32_t op_ret, int32_t op_errno,
               @LONG_ARGS@)
{
        int              ret = 0;
        uint64_t         val = 0;
        loc_t           *loc = NULL;
        cs_local_t      *local = NULL;

        local = frame->local;

        loc = &local->loc;

        if (op_ret == 0) {
                ret = dict_get_uint64 (xdata, GF_CS_OBJECT_STATUS, &val);
                if (!ret) {
                        ret = __cs_inode_ctx_update (this, loc->inode, val);
                        if (ret) {
                                gf_msg (this->name, GF_LOG_ERROR, 0, 0,
                                        "ctx update failed");
                        }
                }
        } else {
                cs_inode_ctx_reset (this, loc->inode);
        }

        CS_STACK_UNWIND (@NAME@, frame, op_ret, op_errno, @SHORT_ARGS@);

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
fd_data_modify_op_fop_template = ['readv', 'writev', 'flush', 'fsync',
                                  'ftruncate', 'rchecksum', 'fallocate',
                                  'discard', 'zerofill', 'seek']

# The following list contains fops which are entry based that does not change
# data
loc_stat_op_fop_template = ['lookup', 'stat', 'discover', 'access', 'setattr',
                            'getattr']

# These fops need a separate implementation
special_fops = ['readdirp', 'statfs', 'setxattr', 'unlink', 'getxattr',
                'truncate', 'fstat']

def gen_defaults():
    for name in ops:
        if name in fd_data_modify_op_fop_template:
            print(generate(FD_DATA_MODIFYING_OP_FOP_CBK_TEMPLATE, name, cbk_subs))
            print(generate(FD_DATA_MODIFYING_RESUME_OP_FOP_TEMPLATE, name, fop_subs))
            print(generate(FD_DATA_MODIFYING_OP_FOP_TEMPLATE, name, fop_subs))
        elif name in loc_stat_op_fop_template:
            print(generate(LOC_STAT_OP_FOP_CBK_TEMPLATE, name, cbk_subs))
            print(generate(LOC_STAT_OP_FOP_TEMPLATE, name, fop_subs))

for l in open(sys.argv[1], 'r').readlines():
    if l.find('#pragma generate') != -1:
        print("/* BEGIN GENERATED CODE - DO NOT MODIFY */")
        gen_defaults()
        print("/* END GENERATED CODE */")
    else:
        print(l[:-1])
