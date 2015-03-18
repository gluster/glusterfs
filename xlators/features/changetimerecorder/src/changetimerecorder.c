/*
   Copyright (c) 2015 Red Hat, Inc. <http://www.redhat.com>
   This file is part of GlusterFS.

   This file is licensed to you under your choice of the GNU Lesser
   General Public License, version 3 or any later version (LGPLv3 or
   later), or the GNU General Public License, version 2 (GPLv2), in all
   cases as published by the Free Software Foundation.
*/
#include <ctype.h>
#include <sys/uio.h>

#include "ctr-helper.h"

/****************************WRITEV********************************************/
int32_t
ctr_writev_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                    int32_t op_ret, int32_t op_errno, struct iatt *prebuf,
                    struct iatt *postbuf,
                    dict_t *xdata)
{
        int ret = -1;

        CTR_IS_DISABLED_THEN_GOTO(this, out);

        ret = ctr_insert_unwind(frame, this,
                        GFDB_FOP_INODE_WRITE, GFDB_FOP_UNWIND);
        if (ret) {
                gf_log (this->name, GF_LOG_ERROR,
                        "Failed inserting writev unwind");
        }


out:
        STACK_UNWIND_STRICT (writev, frame, op_ret, op_errno, prebuf,
                        postbuf, xdata);

        return 0;
}

int32_t
ctr_writev (call_frame_t *frame, xlator_t *this, fd_t *fd,
            struct iovec *vector, int32_t count, off_t off,
            uint32_t flags,
            struct iobref *iobref, dict_t *xdata)
{
        int ret = -1;
        gf_ctr_inode_context_t ctr_inode_cx;
        gf_ctr_inode_context_t *_inode_cx = &ctr_inode_cx;

        CTR_IS_DISABLED_THEN_GOTO(this, out);

        /*Fill ctr inode context*/
        FILL_CTR_INODE_CONTEXT(_inode_cx, fd->inode->ia_type,
                                fd->inode->gfid, NULL, NULL,
                                GFDB_FOP_INODE_WRITE, GFDB_FOP_WIND);

        /*record into the database*/
        ret = ctr_insert_wind(frame, this, _inode_cx);
        if (ret) {
                gf_log (this->name, GF_LOG_ERROR,
                                "Failed inserting writev wind");
        }

out:
        STACK_WIND (frame, ctr_writev_cbk, FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->writev, fd, vector, count,
                    off, flags, iobref, xdata);

        return 0;
}

/******************************setattr*****************************************/

int32_t
ctr_setattr_cbk (call_frame_t *frame,
                       void *cookie, xlator_t *this, int32_t op_ret,
                       int32_t op_errno, struct iatt *preop_stbuf,
                       struct iatt *postop_stbuf, dict_t *xdata)
{

        int ret = -1;

        CTR_IS_DISABLED_THEN_GOTO(this, out);

        ret = ctr_insert_unwind(frame, this,
                        GFDB_FOP_INODE_WRITE, GFDB_FOP_UNWIND);
        if (ret) {
                gf_log (this->name, GF_LOG_ERROR,
                        "Failed inserting setattr unwind");
        }

out:
        STACK_UNWIND_STRICT (setattr, frame, op_ret, op_errno, preop_stbuf,
                       postop_stbuf, xdata);

        return 0;
}

int32_t
ctr_setattr (call_frame_t *frame,
                   xlator_t *this, loc_t *loc,
                   struct iatt *stbuf, int32_t valid, dict_t *xdata)
{

        int ret = -1;
        gf_ctr_inode_context_t ctr_inode_cx;
        gf_ctr_inode_context_t *_inode_cx = &ctr_inode_cx;

        CTR_IS_DISABLED_THEN_GOTO(this, out);

         /*Fill ctr inode context*/
        FILL_CTR_INODE_CONTEXT(_inode_cx, loc->inode->ia_type,
                loc->inode->gfid, NULL, NULL, GFDB_FOP_INODE_WRITE,
                GFDB_FOP_WIND);

        /*record into the database*/
        ret = ctr_insert_wind(frame, this, _inode_cx);
        if (ret) {
                gf_log (this->name, GF_LOG_ERROR,
                                "Failed inserting setattr wind");
        }
out:

        STACK_WIND (frame, ctr_setattr_cbk, FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->setattr, loc, stbuf,
                    valid, xdata);

        return 0;
}

/****************************fremovexattr************************************/

int32_t
ctr_fremovexattr_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                          int32_t op_ret, int32_t op_errno, dict_t *xdata)
{
        int ret = -1;

        CTR_IS_DISABLED_THEN_GOTO(this, out);


        ret = ctr_insert_unwind(frame, this,
                        GFDB_FOP_INODE_WRITE, GFDB_FOP_UNWIND);
        if (ret) {
                gf_log (this->name, GF_LOG_ERROR,
                        "Failed inserting fremovexattr unwind");
        }

out:
        STACK_UNWIND_STRICT (fremovexattr, frame, op_ret, op_errno, xdata);

        return 0;
}

int32_t
ctr_fremovexattr (call_frame_t *frame, xlator_t *this, fd_t *fd,
                      const char *name, dict_t *xdata)
{
        int ret = -1;
        gf_ctr_inode_context_t ctr_inode_cx;
        gf_ctr_inode_context_t *_inode_cx = &ctr_inode_cx;

        CTR_IS_DISABLED_THEN_GOTO(this, out);


         /*Fill ctr inode context*/
        FILL_CTR_INODE_CONTEXT(_inode_cx, fd->inode->ia_type,
                fd->inode->gfid, NULL, NULL, GFDB_FOP_INODE_WRITE,
                GFDB_FOP_WIND);

        /*record into the database*/
        ret = ctr_insert_wind(frame, this, _inode_cx);
        if (ret) {
                gf_log (this->name, GF_LOG_ERROR,
                                "Failed inserting fremovexattr wind");
        }

out:
        STACK_WIND (frame, ctr_fremovexattr_cbk, FIRST_CHILD (this),
                    FIRST_CHILD (this)->fops->fremovexattr,
                    fd, name, xdata);
        return 0;
}

/****************************removexattr*************************************/

int32_t
ctr_removexattr_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                         int32_t op_ret, int32_t op_errno, dict_t *xdata)
{
        int ret = -1;

        CTR_IS_DISABLED_THEN_GOTO(this, out);


        ret = ctr_insert_unwind(frame, this,
                        GFDB_FOP_INODE_WRITE, GFDB_FOP_UNWIND);
        if (ret) {
                gf_log (this->name, GF_LOG_ERROR,
                        "Failed inserting removexattr unwind");
        }

out:
        STACK_UNWIND_STRICT (removexattr, frame, op_ret, op_errno, xdata);

        return 0;
}

int32_t
ctr_removexattr (call_frame_t *frame, xlator_t *this, loc_t *loc,
                     const char *name, dict_t *xdata)
{
        int ret = -1;
        gf_ctr_inode_context_t ctr_inode_cx;
        gf_ctr_inode_context_t *_inode_cx = &ctr_inode_cx;

        CTR_IS_DISABLED_THEN_GOTO(this, out);


         /*Fill ctr inode context*/
        FILL_CTR_INODE_CONTEXT(_inode_cx, loc->inode->ia_type,
                loc->inode->gfid, NULL, NULL, GFDB_FOP_INODE_WRITE,
                GFDB_FOP_WIND);

        /*record into the database*/
        ret = ctr_insert_wind(frame, this, _inode_cx);
        if (ret) {
                gf_log (this->name, GF_LOG_ERROR,
                                "Failed inserting removexattr wind");
        }

out:
        STACK_WIND (frame, ctr_removexattr_cbk, FIRST_CHILD (this),
                    FIRST_CHILD (this)->fops->removexattr,
                    loc, name, xdata);
        return 0;
}

/****************************truncate****************************************/

int32_t
ctr_truncate_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                      int32_t op_ret, int32_t op_errno, struct iatt *prebuf,
                      struct iatt *postbuf, dict_t *xdata)
{
        int ret = -1;

        CTR_IS_DISABLED_THEN_GOTO(this, out);


        ret = ctr_insert_unwind(frame, this,
                        GFDB_FOP_INODE_WRITE, GFDB_FOP_UNWIND);
        if (ret) {
                gf_log (this->name, GF_LOG_ERROR,
                        "Failed inserting truncate unwind");
        }


out:
        STACK_UNWIND_STRICT (truncate, frame, op_ret, op_errno, prebuf,
                      postbuf, xdata);

        return 0;
}

int32_t
ctr_truncate (call_frame_t *frame, xlator_t *this, loc_t *loc,
                  off_t offset, dict_t *xdata)
{
        int ret = -1;
        gf_ctr_inode_context_t ctr_inode_cx;
        gf_ctr_inode_context_t *_inode_cx = &ctr_inode_cx;

        CTR_IS_DISABLED_THEN_GOTO(this, out);

         /*Fill ctr inode context*/
        FILL_CTR_INODE_CONTEXT(_inode_cx, loc->inode->ia_type,
                loc->inode->gfid, NULL, NULL, GFDB_FOP_INODE_WRITE,
                GFDB_FOP_WIND);

        /*record into the database*/
        ret = ctr_insert_wind(frame, this, _inode_cx);
        if (ret) {
                gf_log (this->name, GF_LOG_ERROR,
                                "Failed inserting truncate wind");
        }
out:
        STACK_WIND (frame, ctr_truncate_cbk, FIRST_CHILD (this),
                    FIRST_CHILD (this)->fops->truncate,
                    loc, offset, xdata);
        return 0;
}

/****************************ftruncate***************************************/

int32_t
ctr_ftruncate_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                       int32_t op_ret, int32_t op_errno, struct iatt *prebuf,
                       struct iatt *postbuf, dict_t *xdata)
{
        int ret = -1;

        CTR_IS_DISABLED_THEN_GOTO(this, out);

        ret = ctr_insert_unwind(frame, this,
                        GFDB_FOP_INODE_WRITE, GFDB_FOP_UNWIND);
        if (ret) {
                gf_log (this->name, GF_LOG_ERROR,
                        "Failed inserting ftruncate unwind");
        }

out:
        STACK_UNWIND_STRICT (ftruncate, frame, op_ret, op_errno, prebuf,
                      postbuf, xdata);

        return 0;
}

int32_t
ctr_ftruncate (call_frame_t *frame, xlator_t *this, fd_t *fd,
                   off_t offset, dict_t *xdata)
{
        int ret = -1;
        gf_ctr_inode_context_t ctr_inode_cx;
        gf_ctr_inode_context_t *_inode_cx = &ctr_inode_cx;

        CTR_IS_DISABLED_THEN_GOTO(this, out);


         /*Fill ctr inode context*/
        FILL_CTR_INODE_CONTEXT(_inode_cx, fd->inode->ia_type,
                fd->inode->gfid, NULL, NULL, GFDB_FOP_INODE_WRITE,
                GFDB_FOP_WIND);

        /*record into the database*/
        ret = ctr_insert_wind(frame, this, _inode_cx);
        if (ret) {
                gf_log (this->name, GF_LOG_ERROR,
                                "Failed inserting ftruncate wind");
        }

out:
        STACK_WIND (frame, ctr_ftruncate_cbk, FIRST_CHILD (this),
                    FIRST_CHILD (this)->fops->ftruncate,
                    fd, offset, xdata);
        return 0;
}

/****************************rename******************************************/

int32_t
ctr_rename_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                    int32_t op_ret, int32_t op_errno, struct iatt *buf,
                    struct iatt *preoldparent, struct iatt *postoldparent,
                    struct iatt *prenewparent, struct iatt *postnewparent,
                    dict_t *xdata)
{
        int ret = -1;

        CTR_IS_DISABLED_THEN_GOTO(this, out);

        ret = ctr_insert_unwind(frame, this,
                        GFDB_FOP_DENTRY_WRITE, GFDB_FOP_UNWIND);
        if (ret) {
                gf_log (this->name, GF_LOG_ERROR,
                        "Failed inserting rename unwind");
        }

out:
        STACK_UNWIND_STRICT (rename, frame, op_ret, op_errno, buf,
                             preoldparent, postoldparent, prenewparent,
                             postnewparent,
                             xdata);

        return 0;
}

int32_t
ctr_rename (call_frame_t *frame, xlator_t *this, loc_t *oldloc,
                loc_t *newloc, dict_t *xdata)
{
        int ret = -1;
        gf_ctr_inode_context_t ctr_inode_cx;
        gf_ctr_inode_context_t *_inode_cx = &ctr_inode_cx;
        gf_ctr_link_context_t new_link_cx, old_link_cx;
        gf_ctr_link_context_t *_nlink_cx = &new_link_cx;
        gf_ctr_link_context_t *_olink_cx = &old_link_cx;

        CTR_IS_DISABLED_THEN_GOTO(this, out);

        /*Fill old link context*/
        FILL_CTR_LINK_CX(_olink_cx, oldloc->pargfid, oldloc->name,
                        oldloc->path);

        /*Fill new link context*/
        FILL_CTR_LINK_CX(_nlink_cx, newloc->pargfid, newloc->name,
                        newloc->path);

         /*Fill ctr inode context*/
        FILL_CTR_INODE_CONTEXT(_inode_cx, oldloc->inode->ia_type,
                oldloc->inode->gfid, _nlink_cx, _olink_cx,
                GFDB_FOP_DENTRY_WRITE, GFDB_FOP_WIND);

        /*record into the database*/
        ret = ctr_insert_wind(frame, this, _inode_cx);
        if (ret) {
                gf_log (this->name, GF_LOG_ERROR,
                                "Failed inserting rename wind");
        }

out:
        STACK_WIND (frame, ctr_rename_cbk, FIRST_CHILD (this),
                    FIRST_CHILD (this)->fops->rename,
                    oldloc, newloc, xdata);
        return 0;
}

/****************************unlink******************************************/
int32_t
ctr_unlink_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                    int32_t op_ret, int32_t op_errno, struct iatt *preparent,
                    struct iatt *postparent, dict_t *xdata)
{
        int ret                                 = -1;
        uint32_t remaining_links                = -1;

        CTR_IS_DISABLED_THEN_GOTO(this, out);

        if (!xdata)
                goto out;

        /*
         *
         * Extracting CTR_RESPONSE_LINK_COUNT_XDATA from POSIX Xlator
         *
         * */
        ret = dict_get_uint32 (xdata , CTR_RESPONSE_LINK_COUNT_XDATA,
                                &remaining_links);
        if (ret) {
                gf_log (this->name, GF_LOG_ERROR,
                        "Failed to getting CTR_RESPONSE_LINK_COUNT_XDATA");
                remaining_links = -1;
        }

        /*As the xdata is no more required by CTR Xlator.*/
        if (xdata) {
                dict_unref (xdata);
        }

        /*This is not the only link*/
        if (remaining_links != 1) {

                ret = ctr_insert_unwind(frame, this, GFDB_FOP_DENTRY_WRITE,
                                        GFDB_FOP_UNDEL);
                if (ret) {
                        gf_log (this->name, GF_LOG_ERROR,
                                "Failed inserting unlink unwind");
                }
        }
        /*Last link that was deleted*/
        else if (remaining_links == 1) {
                ret = ctr_insert_unwind(frame, this, GFDB_FOP_DENTRY_WRITE,
                                        GFDB_FOP_UNDEL_ALL);
                if (ret) {
                        gf_log (this->name, GF_LOG_ERROR,
                                "Failed inserting unlink unwind");
                }
        }



out:
        STACK_UNWIND_STRICT (unlink, frame, op_ret, op_errno, preparent,
                        postparent, NULL);

        return 0;
}

int32_t
ctr_unlink (call_frame_t *frame, xlator_t *this,
                loc_t *loc, int xflag, dict_t *xdata)
{
        int ret = -1;
        gf_ctr_inode_context_t ctr_inode_cx;
        gf_ctr_inode_context_t *_inode_cx = &ctr_inode_cx;
        gf_ctr_link_context_t ctr_link_cx;
        gf_ctr_link_context_t *_link_cx = &ctr_link_cx;

        CTR_IS_DISABLED_THEN_GOTO(this, out);

        /*Fill link context*/
        FILL_CTR_LINK_CX(_link_cx, loc->pargfid, loc->name, loc->path);

         /*Fill ctr inode context*/
        FILL_CTR_INODE_CONTEXT(_inode_cx, loc->inode->ia_type,
                loc->inode->gfid, _link_cx, NULL,
                GFDB_FOP_DENTRY_WRITE, GFDB_FOP_WDEL);

        /*record into the database*/
        ret = ctr_insert_wind(frame, this, _inode_cx);
        if (ret) {
                gf_log (this->name, GF_LOG_ERROR,
                                "Failed inserting unlink wind");
        }



        /*
         *
         * Sending CTR_REQUEST_LINK_COUNT_XDATA
         * to POSIX Xlator to send link count in unwind path
         *
         * */
        /*create xdata if NULL*/
        if (!xdata)
                xdata = dict_new();
        if (!xdata) {
                gf_log (this->name, GF_LOG_ERROR,
                                "xdata is NULL :"
                                "Cannot send CTR_REQUEST_LINK_COUNT_XDATA"
                                "to posix");
                goto out;
        }

        ret = dict_set_int32 (xdata, CTR_REQUEST_LINK_COUNT_XDATA, 1);
        if (ret) {
                gf_log (this->name, GF_LOG_ERROR,
                                "Failed setting CTR_REQUEST_LINK_COUNT_XDATA");
                if (xdata) {
                        dict_unref (xdata);
                }
                goto out;
        }



out:
        STACK_WIND (frame, ctr_unlink_cbk, FIRST_CHILD (this),
                    FIRST_CHILD (this)->fops->unlink,
                    loc, xflag, xdata);
        return 0;
}

/****************************fsync******************************************/
int32_t
ctr_fsync_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                   int32_t op_ret, int32_t op_errno, struct iatt *prebuf,
                   struct iatt *postbuf, dict_t *xdata)
{
        int ret = -1;

        CTR_IS_DISABLED_THEN_GOTO(this, out);

        ret = ctr_insert_unwind(frame, this, GFDB_FOP_INODE_WRITE,
                                GFDB_FOP_UNWIND);
        if (ret) {
                gf_log (this->name, GF_LOG_ERROR,
                        "Failed inserting fsync unwind");
        }

out:
        STACK_UNWIND_STRICT (fsync, frame, op_ret, op_errno, prebuf, postbuf,
                      xdata);

        return 0;
}

int32_t
ctr_fsync (call_frame_t *frame, xlator_t *this, fd_t *fd,
               int32_t flags, dict_t *xdata)
{
        int ret = -1;
        gf_ctr_inode_context_t ctr_inode_cx;
        gf_ctr_inode_context_t *_inode_cx = &ctr_inode_cx;

        CTR_IS_DISABLED_THEN_GOTO(this, out);

         /*Fill ctr inode context*/
        FILL_CTR_INODE_CONTEXT(_inode_cx, fd->inode->ia_type,
                fd->inode->gfid, NULL, NULL,
                GFDB_FOP_INODE_WRITE, GFDB_FOP_WIND);

        /*record into the database*/
        ret = ctr_insert_wind(frame, this, _inode_cx);
        if (ret) {
                gf_log (this->name, GF_LOG_ERROR,
                                "Failed inserting fsync wind");
        }

out:
        STACK_WIND (frame, ctr_fsync_cbk, FIRST_CHILD (this),
                    FIRST_CHILD (this)->fops->fsync,
                    fd, flags, xdata);
        return 0;
}

/****************************setxattr****************************************/

int
ctr_setxattr_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                  int32_t op_ret, int32_t op_errno, dict_t *xdata)
{
        int ret = -1;

        CTR_IS_DISABLED_THEN_GOTO(this, out);

        ret = ctr_insert_unwind(frame, this, GFDB_FOP_INODE_WRITE,
                                GFDB_FOP_UNWIND);
        if (ret) {
                gf_log (this->name, GF_LOG_ERROR,
                        "Failed inserting fsync unwind");
        }


out:
        STACK_UNWIND_STRICT (setxattr, frame, op_ret, op_errno, xdata);

        return 0;
}

int
ctr_setxattr (call_frame_t *frame, xlator_t *this,
              loc_t *loc, dict_t *xattr, int flags, dict_t *xdata)
{
        int ret = -1;
        gf_ctr_inode_context_t ctr_inode_cx;
        gf_ctr_inode_context_t *_inode_cx = &ctr_inode_cx;

        CTR_IS_DISABLED_THEN_GOTO(this, out);

         /*Fill ctr inode context*/
        FILL_CTR_INODE_CONTEXT(_inode_cx, loc->inode->ia_type,
                loc->inode->gfid, NULL, NULL,
                GFDB_FOP_INODE_WRITE, GFDB_FOP_WIND);

        /*record into the database*/
        ret = ctr_insert_wind(frame, this, _inode_cx);
        if (ret) {
                gf_log (this->name, GF_LOG_ERROR,
                                "Failed inserting setxattr wind");
        }

out:
        STACK_WIND (frame, ctr_setxattr_cbk, FIRST_CHILD (this),
                    FIRST_CHILD (this)->fops->setxattr,
                    loc, xattr, flags, xdata);
        return 0;
}

/****************************mknod*******************************************/


int32_t
ctr_mknod_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                 int32_t op_ret, int32_t op_errno, inode_t *inode,
                 struct iatt *buf, struct iatt *preparent,
                 struct iatt *postparent, dict_t *xdata)
{
        int ret = -1;

        CTR_IS_DISABLED_THEN_GOTO(this, out);

        ret = ctr_insert_unwind(frame, this, GFDB_FOP_CREATE_WRITE,
                                GFDB_FOP_UNWIND);
        if (ret) {
                gf_log (this->name, GF_LOG_ERROR,
                        "Failed inserting mknod unwind");
        }

out:
        STACK_UNWIND_STRICT (mknod, frame, op_ret, op_errno, inode, buf,
                preparent, postparent, xdata);

        return 0;
}


int
ctr_mknod (call_frame_t *frame, xlator_t *this,
           loc_t *loc, mode_t mode, dev_t rdev, mode_t umask, dict_t *xdata)
{
        int ret = -1;
        gf_ctr_inode_context_t ctr_inode_cx;
        gf_ctr_inode_context_t *_inode_cx = &ctr_inode_cx;
        gf_ctr_link_context_t  ctr_link_cx;
        gf_ctr_link_context_t  *_link_cx = &ctr_link_cx;
        void *uuid_req                          = NULL;
        uuid_t gfid                             = {0,};
        uuid_t *ptr_gfid                        = &gfid;

        CTR_IS_DISABLED_THEN_GOTO(this, out);

        GF_ASSERT(frame);
        GF_ASSERT(frame->root);
        CTR_IF_REBALANCE_FOP_THEN_GOTO (frame, out);
        CTR_IF_INTERNAL_FOP_THEN_GOTO (frame, xdata, out);

        /*get gfid from xdata dict*/
        ret = dict_get_ptr (xdata, "gfid-req", &uuid_req);
        if (ret) {
                gf_log (this->name, GF_LOG_DEBUG,
                        "failed to get gfid from dict");
                goto out;
        }
        uuid_copy (gfid, uuid_req);

        /*fill ctr link context*/
        FILL_CTR_LINK_CX (_link_cx, loc->pargfid, loc->name, loc->path);

         /*Fill ctr inode context*/
        FILL_CTR_INODE_CONTEXT (_inode_cx, loc->inode->ia_type,
                *ptr_gfid, _link_cx, NULL,
                GFDB_FOP_CREATE_WRITE, GFDB_FOP_WIND);

        /*record into the database*/
        ret = ctr_insert_wind(frame, this, _inode_cx);
        if (ret) {
                gf_log (this->name, GF_LOG_ERROR,
                                "Failed inserting mknod wind");
        }

out:
        STACK_WIND (frame, ctr_mknod_cbk, FIRST_CHILD (this),
                    FIRST_CHILD (this)->fops->mknod,
                    loc, mode, rdev, umask, xdata);
        return 0;
}

/****************************create******************************************/
int
ctr_create_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                int op_ret, int op_errno,
                fd_t *fd, inode_t *inode, struct iatt *stbuf,
                struct iatt *preparent, struct iatt *postparent,
                dict_t *xdata)
{
        int ret = -1;

        CTR_IS_DISABLED_THEN_GOTO(this, out);

        ret = ctr_insert_unwind(frame, this, GFDB_FOP_CREATE_WRITE,
                                GFDB_FOP_UNWIND);
        if (ret) {
                gf_log (this->name, GF_LOG_ERROR,
                        "Failed inserting create unwind");
        }

out:
        STACK_UNWIND_STRICT (create, frame, op_ret, op_errno, fd, inode,
                             stbuf,
                        preparent, postparent, xdata);

        return 0;
}

int
ctr_create (call_frame_t *frame, xlator_t *this,
            loc_t *loc, int32_t flags, mode_t mode,
            mode_t umask, fd_t *fd, dict_t *xdata)
{
        int ret = -1;
        gf_ctr_inode_context_t ctr_inode_cx;
        gf_ctr_inode_context_t *_inode_cx = &ctr_inode_cx;
        gf_ctr_link_context_t  ctr_link_cx;
        gf_ctr_link_context_t  *_link_cx = &ctr_link_cx;
        void             *uuid_req              = NULL;
        uuid_t            gfid                  = {0,};
        uuid_t            *ptr_gfid             = &gfid;

        CTR_IS_DISABLED_THEN_GOTO(this, out);

        /*Get GFID from Xdata dict*/
        ret = dict_get_ptr (xdata, "gfid-req", &uuid_req);
        if (ret) {
                gf_log (this->name, GF_LOG_ERROR,
                        "failed to get gfid from dict");
                goto out;
        }
        uuid_copy (gfid, uuid_req);

        /*fill ctr link context*/
        FILL_CTR_LINK_CX(_link_cx, loc->pargfid, loc->name, loc->path);

         /*Fill ctr inode context*/
        FILL_CTR_INODE_CONTEXT(_inode_cx, loc->inode->ia_type,
                *ptr_gfid, _link_cx, NULL,
                GFDB_FOP_CREATE_WRITE, GFDB_FOP_WIND);

        /*record into the database*/
        ret = ctr_insert_wind(frame, this, &ctr_inode_cx);
        if (ret) {
                gf_log (this->name, GF_LOG_ERROR,
                                "Failed inserting create wind");
        }
out:
        STACK_WIND (frame, ctr_create_cbk, FIRST_CHILD (this),
                    FIRST_CHILD (this)->fops->create,
                    loc, flags, mode, umask, fd, xdata);
        return 0;
}

/****************************link********************************************/

int
ctr_link_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
              int op_ret, int op_errno,
              inode_t *inode, struct iatt *stbuf, struct iatt *preparent,
              struct iatt *postparent, dict_t *xdata)
{
        int ret = -1;

        CTR_IS_DISABLED_THEN_GOTO(this, out);

        ret = ctr_insert_unwind(frame, this, GFDB_FOP_DENTRY_WRITE,
                                GFDB_FOP_UNWIND);
        if (ret) {
                gf_log (this->name, GF_LOG_ERROR,
                        "Failed inserting create unwind");
        }

out:
        STACK_UNWIND_STRICT (link, frame, op_ret, op_errno, inode, stbuf,
                       preparent, postparent, xdata);
        return 0;
}

int
ctr_link (call_frame_t *frame, xlator_t *this,
          loc_t *oldloc, loc_t *newloc, dict_t *xdata)
{
        int ret = -1;
        gf_ctr_inode_context_t ctr_inode_cx;
        gf_ctr_inode_context_t *_inode_cx = &ctr_inode_cx;
        gf_ctr_link_context_t  ctr_link_cx;
        gf_ctr_link_context_t  *_link_cx = &ctr_link_cx;

        CTR_IS_DISABLED_THEN_GOTO(this, out);

        CTR_IF_REBALANCE_FOP_THEN_GOTO (frame, out);

        /*fill ctr link context*/
        FILL_CTR_LINK_CX(_link_cx, newloc->pargfid, newloc->name,
                        newloc->path);

         /*Fill ctr inode context*/
        FILL_CTR_INODE_CONTEXT(_inode_cx, oldloc->inode->ia_type,
                oldloc->inode->gfid, _link_cx, NULL,
                GFDB_FOP_DENTRY_WRITE, GFDB_FOP_WIND);

        /*record into the database*/
        ret = ctr_insert_wind(frame, this, _inode_cx);
        if (ret) {
                gf_log (this->name, GF_LOG_ERROR,
                                "Failed inserting link wind");
        }

out:
        STACK_WIND (frame, ctr_link_cbk, FIRST_CHILD (this),
                    FIRST_CHILD (this)->fops->link,
                    oldloc, newloc, xdata);
        return 0;
}

/******************************readv*****************************************/
int ctr_readv_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
               int op_ret, int op_errno,
               struct iovec *vector, int count, struct iatt *stbuf,
               struct iobref *iobref, dict_t *xdata) {

        int ret = -1;

        CTR_IS_DISABLED_THEN_GOTO(this, out);

        ret = ctr_insert_unwind(frame, this, GFDB_FOP_INODE_READ,
                                GFDB_FOP_UNWIND);
        if (ret) {
                gf_log (this->name, GF_LOG_ERROR,
                        "Failed inserting create unwind");
        }

out:
        STACK_UNWIND_STRICT (readv, frame, op_ret, op_errno, vector, count,
                                stbuf, iobref, xdata);
        return 0;
}


int
ctr_readv (call_frame_t *frame, xlator_t *this,
           fd_t *fd, size_t size, off_t off, uint32_t flags, dict_t *xdata)
{
        int ret = -1;
        gf_ctr_inode_context_t ctr_inode_cx;
        gf_ctr_inode_context_t *_inode_cx = &ctr_inode_cx;

        CTR_IS_DISABLED_THEN_GOTO(this, out);

         /*Fill ctr inode context*/
        FILL_CTR_INODE_CONTEXT(_inode_cx, fd->inode->ia_type,
                fd->inode->gfid, NULL, NULL,
                GFDB_FOP_INODE_READ, GFDB_FOP_WIND);

        /*record into the database*/
        ret = ctr_insert_wind(frame, this, _inode_cx);
        if (ret) {
                gf_log (this->name, GF_LOG_ERROR,
                                "Failed inserting readv wind");
        }

out:
        STACK_WIND (frame, ctr_readv_cbk, FIRST_CHILD (this),
                    FIRST_CHILD (this)->fops->readv,
                    fd, size, off, flags, xdata);
        return 0;
}
/******************************************************************************/

int
reconfigure (xlator_t *this, dict_t *options)
{
        char *temp_str = NULL;
        int ret = 0;
        gf_ctr_private_t *_priv = NULL;

        _priv = this->private;
        if (dict_get_str(options, "changetimerecorder.frequency",
                         &temp_str)) {
                gf_log(this->name, GF_LOG_INFO, "set!");
        }

        GF_OPTION_RECONF ("ctr-enabled", _priv->enabled, options,
                          bool, out);

        GF_OPTION_RECONF ("record-counters", _priv->ctr_record_counter, options,
                          bool, out);

        GF_OPTION_RECONF ("record-exit", _priv->ctr_record_unwind, options,
                          bool, out);

        GF_OPTION_RECONF ("record-entry", _priv->ctr_record_wind, options,
                          bool, out);

out:

        return ret;
}

/****************************init********************************************/

int32_t
init (xlator_t *this)
{
        gf_ctr_private_t *_priv = NULL;
        int ret_db              = -1;
        dict_t *params_dict      = NULL;

        GF_VALIDATE_OR_GOTO ("ctr", this, error);

        if (!this->children || this->children->next) {
                gf_log (this->name, GF_LOG_ERROR,
                        "FATAL: ctr should have exactly one child");
                goto error;
        }

        if (!this->parents) {
                gf_log (this->name, GF_LOG_WARNING,
                        "dangling volume. check volfile ");
        }

        _priv = GF_CALLOC (1, sizeof (*_priv), gf_ctr_mt_private_t);
        if (!_priv) {
                gf_log (this->name, GF_LOG_ERROR,
                        "Calloc didnt work!!!");
                goto error;
        }

        /*Default values for the translator*/
        _priv->ctr_record_wind          = _gf_true;
        _priv->ctr_record_unwind        = _gf_false;
        _priv->ctr_hot_brick            = _gf_false;
        _priv->gfdb_db_type             = GFDB_SQLITE3;
        _priv->gfdb_sync_type           = GFDB_DB_SYNC;
        _priv->enabled                  = _gf_true;
        _priv->_db_conn                 = NULL;

        /*Extract ctr xlator options*/
        ret_db = extract_ctr_options (this, _priv);
        if (ret_db) {
                gf_log (this->name, GF_LOG_ERROR,
                        "Failed extracting ctr xlator options");
                goto error;
        }

        params_dict = dict_new ();
        if (!params_dict) {
                gf_log (this->name, GF_LOG_ERROR,
                        "DB Params cannot initialized!");
                goto error;
        }

        /*Extract db params options*/
        ret_db = extract_db_params(this, params_dict, _priv->gfdb_db_type);
        if (ret_db) {
                gf_log (this->name, GF_LOG_ERROR,
                        "Failed extracting db params options");
                goto error;
        }

        /*Create a memory pool for ctr xlator*/
        this->local_pool = mem_pool_new (gf_ctr_local_t, 64);
        if (!this->local_pool) {
                gf_log (this->name, GF_LOG_ERROR,
                        "failed to create local memory pool");
                goto error;
        }

        /*Initialize Database Connection*/
        _priv->_db_conn = init_db(params_dict, _priv->gfdb_db_type);
        if (!_priv->_db_conn) {
                gf_log (this->name, GF_LOG_ERROR,
                       "FATAL: Failed initializing data base");
                        goto error;
        }

        ret_db = 0;
        goto out;

/*Error handling */
error:

        if (this)
                mem_pool_destroy (this->local_pool);

        if (_priv) {
                GF_FREE (_priv->ctr_db_path);
        }
        GF_FREE (_priv);

        if (params_dict)
                dict_unref (params_dict);

        return -1;

out:

        if (params_dict)
                dict_unref (params_dict);

        this->private = (void *)_priv;
        return 0;
}

int32_t
mem_acct_init (xlator_t *this)
{
        int     ret = -1;

        GF_VALIDATE_OR_GOTO ("ctr", this, out);

        ret = xlator_mem_acct_init (this, gf_ctr_mt_end + 1);

        if (ret != 0) {
                gf_log (this->name, GF_LOG_ERROR, "Memory accounting init"
                        "failed");
                return ret;
        }
out:
        return ret;
}


void
fini (xlator_t *this)
{
        gf_ctr_private_t *priv = NULL;

        priv = this->private;

        if (priv) {
                if (fini_db (priv->_db_conn)) {
                        gf_log (this->name, GF_LOG_WARNING, "Failed closing "
                        "db connection");
                }
                GF_FREE (priv->ctr_db_path);
        }
        GF_FREE (priv);
        mem_pool_destroy (this->local_pool);

        return;
}

struct xlator_fops fops = {
        /*write fops */
        .mknod       = ctr_mknod,
        .create      = ctr_create,
        .truncate    = ctr_truncate,
        .ftruncate   = ctr_ftruncate,
        .setxattr    = ctr_setxattr,
        .removexattr = ctr_removexattr,
        .unlink      = ctr_unlink,
        .link        = ctr_link,
        .rename      = ctr_rename,
        .writev      = ctr_writev,
        .setattr      = ctr_setattr,
        /*read fops*/
        .readv       = ctr_readv
};

struct xlator_cbks cbks;

struct volume_options options[] = {
        { .key  = {"ctr-enabled",},
          .type = GF_OPTION_TYPE_BOOL,
          .value = {"on", "off"},
          .default_value = "off",
          .description = "Enables the CTR"
        },
        { .key  = {"record-entry"},
          .type = GF_OPTION_TYPE_BOOL,
          .value = {"on", "off"},
          .default_value = "on"
        },
        { .key  = {"record-exit"},
          .type = GF_OPTION_TYPE_BOOL,
          .value = {"on", "off"},
          .default_value = "off"
        },
        { .key  = {"record-counters"},
          .type = GF_OPTION_TYPE_BOOL,
          .value = {"on", "off"},
          .default_value = "off"
        },
        { .key  = {"hot-brick"},
          .type = GF_OPTION_TYPE_BOOL,
          .value = {"on", "off"},
          .default_value = "off"
        },
        { .key  = {"db-type"},
          .type = GF_OPTION_TYPE_STR,
          .value = {"hashfile", "rocksdb", "changelog", "sqlite3",
                    "hyperdex"},
          .default_value = "sqlite3"
        },
        { .key  = {"db-sync"},
          .type = GF_OPTION_TYPE_STR,
          .value = {"sync", "async"},
          .default_value = "sync"
        },
        { .key  = {"db-path"},
          .type = GF_OPTION_TYPE_PATH
        },
        { .key  = {"db-name"},
          .type = GF_OPTION_TYPE_STR
        },
        { .key  = {GFDB_SQL_PARAM_SYNC},
          .type = GF_OPTION_TYPE_STR,
          .value = {"off", "normal", "full"},
          .default_value = "normal"
        },
        { .key  = {GFDB_SQL_PARAM_JOURNAL_MODE},
          .type = GF_OPTION_TYPE_STR,
          .value = {"delete", "truncate", "persist", "memory", "wal", "off"},
          .default_value = "wal"
        },
        { .key  = {GFDB_SQL_PARAM_AUTO_VACUUM},
          .type = GF_OPTION_TYPE_STR,
          .value = {"off", "full", "incr"},
          .default_value = "off"
        },
        { .key  = {GFDB_SQL_PARAM_WAL_AUTOCHECK},
          .type = GF_OPTION_TYPE_INT,
          .default_value = "1000"
        },
        { .key  = {GFDB_SQL_PARAM_CACHE_SIZE},
          .type = GF_OPTION_TYPE_INT,
          .default_value = "1000"
        },
        { .key  = {GFDB_SQL_PARAM_PAGE_SIZE},
          .type = GF_OPTION_TYPE_INT,
          .default_value = "4096"
        },
        { .key  = {NULL} },
};
