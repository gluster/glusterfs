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

#include "gfdb_sqlite3.h"
#include "ctr-helper.h"
#include "ctr-messages.h"
#include "syscall.h"

#include "changetimerecorder.h"
#include "tier-ctr-interface.h"

/*******************************inode forget***********************************/

int
ctr_forget (xlator_t *this, inode_t *inode)
{
        fini_ctr_xlator_ctx (this, inode);
        return 0;
}

/************************** Look up heal **************************************/
/*
Problem: The CTR xlator records file meta (heat/hardlinks)
into the data. This works fine for files which are created
after ctr xlator is switched ON. But for files which were
created before CTR xlator is ON, CTR xlator is not able to
record either of the meta i.e heat or hardlinks. Thus making
those files immune to promotions/demotions.

Solution: The solution that is implemented in this patch is
do ctr-db heal of all those pre-existent files, using named lookup.
For this purpose we use the inode-xlator context variable option
in gluster.
The inode-xlator context variable for ctr xlator will have the
following,
    a. A Lock for the context variable
    b. A hardlink list: This list represents the successful looked
       up hardlinks.
These are the scenarios when the hardlink list is updated:
1) Named-Lookup: Whenever a named lookup happens on a file, in the
   wind path we copy all required hardlink and inode information to
   ctr_db_record structure, which resides in the frame->local variable.
   We dont update the database in wind. During the unwind, we read the
   information from the ctr_db_record and ,
   Check if the inode context variable is created, if not we create it.
   Check if the hard link is there in the hardlink list.
      If its not there we add it to the list and send a update to the
      database using libgfdb.
      Please note: The database transaction can fail(and we ignore) as there
      already might be a record in the db. This update to the db is to heal
      if its not there.
      If its there in the list we ignore it.
2) Inode Forget: Whenever an inode forget hits we clear the hardlink list in
   the inode context variable and delete the inode context variable.
   Please note: An inode forget may happen for two reason,
   a. when the inode is delete.
   b. the in-memory inode is evicted from the inode table due to cache limits.
3) create: whenever a create happens we create the inode context variable and
   add the hardlink. The database updation is done as usual by ctr.
4) link: whenever a hardlink is created for the inode, we create the inode
 context variable, if not present, and add the hardlink to the list.
5) unlink: whenever a unlink happens we delete the hardlink from the list.
6) mknod: same as create.
7) rename: whenever a rename happens we update the hardlink in list. if the
   hardlink was not present for updation, we add the hardlink to the list.

What is pending:
1) This solution will only work for named lookups.
2) We dont track afr-self-heal/dht-rebalancer traffic for healing.

*/


/* This function doesnot write anything to the db,
 * just created the local variable
 * for the frame and sets values for the ctr_db_record */
static int
ctr_lookup_wind(call_frame_t                    *frame,
                xlator_t                        *this,
                gf_ctr_inode_context_t          *ctr_inode_cx)
{
        int ret                         = -1;
        gf_ctr_private_t *_priv         = NULL;
        gf_ctr_local_t *ctr_local       = NULL;

        GF_ASSERT(frame);
        GF_ASSERT(frame->root);
        GF_ASSERT(this);
        IS_CTR_INODE_CX_SANE(ctr_inode_cx);

        _priv = this->private;
        GF_ASSERT (_priv);

        if (_priv->ctr_record_wind && ctr_inode_cx->ia_type != IA_IFDIR) {

                frame->local = init_ctr_local_t (this);
                if (!frame->local) {
                        gf_msg (this->name, GF_LOG_ERROR, 0,
                                CTR_MSG_CREATE_CTR_LOCAL_ERROR_WIND,
                                "WIND: Error while creating ctr local");
                        goto out;
                };
                ctr_local = frame->local;
                /*Definately no internal fops will reach here*/
                ctr_local->is_internal_fop = _gf_false;
                /*Dont record counters*/
                CTR_DB_REC(ctr_local).do_record_counters = _gf_false;
                /*Don't record time at all*/
                CTR_DB_REC(ctr_local).do_record_times = _gf_false;

                /* Copy gfid into db record*/
                gf_uuid_copy (CTR_DB_REC(ctr_local).gfid,
                                *(ctr_inode_cx->gfid));

                /* Set fop_path and fop_type, required by libgfdb to make
                * decision while inserting the record */
                CTR_DB_REC(ctr_local).gfdb_fop_path = ctr_inode_cx->fop_path;
                CTR_DB_REC(ctr_local).gfdb_fop_type = ctr_inode_cx->fop_type;

                /* Copy hard link info*/
                gf_uuid_copy (CTR_DB_REC(ctr_local).pargfid,
                        *((NEW_LINK_CX(ctr_inode_cx))->pargfid));
                strncpy (CTR_DB_REC(ctr_local).file_name,
                         NEW_LINK_CX(ctr_inode_cx)->basename,
                         sizeof(CTR_DB_REC(ctr_local).file_name));

                /* Since we are in lookup we can ignore errors while
                 * Inserting in the DB, because there may be many
                 * to write to the DB attempts for healing.
                 * We dont want to log all failed attempts and
                 * bloat the log*/
                 ctr_local->gfdb_db_record.ignore_errors = _gf_true;
        }

        ret = 0;

out:

        if (ret) {
                free_ctr_local (ctr_local);
                frame->local = NULL;
        }

        return ret;
}


/* This function inserts the ctr_db_record populated by ctr_lookup_wind
 * in to the db. It also destroys the frame->local created by ctr_lookup_wind */
static int
ctr_lookup_unwind (call_frame_t          *frame,
                   xlator_t              *this)
{
        int ret = -1;
        gf_ctr_private_t *_priv         = NULL;
        gf_ctr_local_t *ctr_local       = NULL;

        GF_ASSERT(frame);
        GF_ASSERT(this);

        _priv = this->private;
        GF_ASSERT (_priv);

        GF_ASSERT(_priv->_db_conn);

        ctr_local = frame->local;

        if (ctr_local && (ctr_local->ia_inode_type != IA_IFDIR)) {

                ret = insert_record(_priv->_db_conn,
                                &ctr_local->gfdb_db_record);
                if (ret == -1) {
                        gf_msg (this->name,
                                _gfdb_log_level (GF_LOG_ERROR,
                                        ctr_local->
                                        gfdb_db_record.ignore_errors),
                                0, CTR_MSG_FILL_CTR_LOCAL_ERROR_UNWIND,
                               "UNWIND: Error filling ctr local");
                        goto out;
                }
        }
        ret = 0;
out:
        free_ctr_local (ctr_local);
        frame->local = NULL;
        return ret;
}

/******************************************************************************
 *
 *                        FOPS HANDLING BELOW
 *
 * ***************************************************************************/

/****************************LOOKUP********************************************/


int32_t
ctr_lookup_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                   int32_t op_ret, int32_t op_errno, inode_t *inode,
                   struct iatt *buf, dict_t *dict, struct iatt *postparent)
{
        int ret                                 = -1;
        ctr_xlator_ctx_t *ctr_xlator_ctx        = NULL;
        gf_ctr_local_t *ctr_local               = NULL;
        ctr_heal_ret_val_t  ret_val             = CTR_CTX_ERROR;
        gf_boolean_t    _is_heal_needed         = _gf_false;

        CTR_IS_DISABLED_THEN_GOTO(this, out);

        /* if the lookup failed lookup dont do anything*/
        if (op_ret == -1) {
                gf_msg_trace (this->name, 0, "lookup failed with %s",
                              strerror (op_errno));
                goto out;
        }

        /* Ignore directory lookups */
        if (inode->ia_type == IA_IFDIR) {
                goto out;
        }

        /* if frame local was not set by the ctr_lookup()
         * so dont so anything*/
        if (!frame->local) {
                goto out;
        }

        /* if the lookup is for dht link donot record*/
        if (dht_is_linkfile (buf, dict)) {
                gf_msg_trace (this->name, 0, "Ignoring Lookup "
                                "for dht link file");
                goto out;
        }

        ctr_local = frame->local;
        /*Assign the proper inode type*/
        ctr_local->ia_inode_type = inode->ia_type;

        /* Copy gfid directly from inode */
        gf_uuid_copy (CTR_DB_REC(ctr_local).gfid, inode->gfid);

        /* Checking if gfid and parent gfid is valid */
        if (gf_uuid_is_null(CTR_DB_REC(ctr_local).gfid) ||
                gf_uuid_is_null(CTR_DB_REC(ctr_local).pargfid)) {
                gf_msg_trace (this->name, 0,
                        "Invalid GFID");
                goto out;
        }

        /* if its a first entry
         * then mark the ctr_record for create
         * A create will attempt a file and a hard link created in the db*/
        ctr_xlator_ctx = get_ctr_xlator_ctx (this, inode);
        if (!ctr_xlator_ctx) {
                 /* This marks inode heal */
                CTR_DB_REC(ctr_local).gfdb_fop_type = GFDB_FOP_CREATE_WRITE;
                _is_heal_needed = _gf_true;
        }

        /* Copy the correct gfid from resolved inode */
        gf_uuid_copy (CTR_DB_REC(ctr_local).gfid, inode->gfid);

        /* Add hard link to the list */
        ret_val = add_hard_link_ctx (frame, this, inode);
        if (ret_val == CTR_CTX_ERROR) {
                gf_msg_trace (this->name, 0,
                        "Failed adding hardlink to list");
                goto out;
        }
        /* If inode needs healing then heal the hardlink also */
        else if (ret_val & CTR_TRY_INODE_HEAL) {
                /* This marks inode heal */
                CTR_DB_REC(ctr_local).gfdb_fop_type = GFDB_FOP_CREATE_WRITE;
                _is_heal_needed = _gf_true;
        }
        /* If hardlink needs healing */
        else if (ret_val & CTR_TRY_HARDLINK_HEAL) {
                _is_heal_needed = _gf_true;
        }

        /* If lookup heal needed */
        if (!_is_heal_needed)
                goto out;

        /* FINALLY HEAL : Inserts the ctr_db_record populated by ctr_lookup_wind
        * in to the db. It also destroys the frame->local
        * created by ctr_lookup_wind */
        ret = ctr_lookup_unwind(frame, this);
        if (ret) {
                gf_msg_trace (this->name, 0,
                        "Failed healing/inserting link");
        }


out:
        free_ctr_local ((gf_ctr_local_t *)frame->local);
        frame->local = NULL;

        STACK_UNWIND_STRICT (lookup, frame, op_ret, op_errno, inode, buf,
                             dict, postparent);

        return 0;
}



int32_t
ctr_lookup (call_frame_t *frame, xlator_t *this,
              loc_t *loc, dict_t *xdata)
{
        gf_ctr_inode_context_t ctr_inode_cx;
        gf_ctr_inode_context_t *_inode_cx       = &ctr_inode_cx;
        gf_ctr_link_context_t  ctr_link_cx;
        gf_ctr_link_context_t  *_link_cx        = &ctr_link_cx;
        int ret                                 = -1;

        CTR_IS_DISABLED_THEN_GOTO(this, out);
        CTR_IF_INTERNAL_FOP_THEN_GOTO (frame, xdata, out);

        GF_ASSERT(frame);
        GF_ASSERT(frame->root);

        /* Dont handle nameless lookups*/
        if (!loc->parent || !loc->name)
                goto out;

        /*fill ctr link context*/
        FILL_CTR_LINK_CX(_link_cx, loc->parent->gfid, loc->name, out);

         /* Fill ctr inode context*/
         /* IA_IFREG : We assume its a file in the wind
          * but in the unwind we are sure what the inode is a file
          * or directory
          * gfid: we are just filling loc->gfid which is not correct.
          * In unwind we fill the correct gfid for successful lookup*/
        FILL_CTR_INODE_CONTEXT(_inode_cx, IA_IFREG,
                loc->gfid, _link_cx, NULL,
                GFDB_FOP_DENTRY_WRITE, GFDB_FOP_WIND);

        /* Create the frame->local and populate ctr_db_record
         * No writing to the db yet */
        ret = ctr_lookup_wind(frame, this, _inode_cx);
        if (ret) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        CTR_MSG_INSERT_LINK_WIND_FAILED,
                        "Failed to insert link wind");
        }

out:
        STACK_WIND (frame, ctr_lookup_cbk, FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->lookup, loc, xdata);
        return 0;
}




/****************************WRITEV********************************************/
int32_t
ctr_writev_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                    int32_t op_ret, int32_t op_errno, struct iatt *prebuf,
                    struct iatt *postbuf,
                    dict_t *xdata)
{
        int ret = -1;

        CTR_IS_DISABLED_THEN_GOTO (this, out);
        CTR_IF_FOP_FAILED_THEN_GOTO (this, op_ret, op_errno, out);

        ret = ctr_insert_unwind (frame, this,
                        GFDB_FOP_INODE_WRITE, GFDB_FOP_UNWIND);
        if (ret) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        CTR_MSG_INSERT_WRITEV_UNWIND_FAILED,
                        "Failed to insert writev unwind");
        }


out:
        ctr_free_frame_local (frame);

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
        CTR_IF_INTERNAL_FOP_THEN_GOTO (frame, xdata, out);

        /*Fill ctr inode context*/
        FILL_CTR_INODE_CONTEXT(_inode_cx, fd->inode->ia_type,
                                fd->inode->gfid, NULL, NULL,
                                GFDB_FOP_INODE_WRITE, GFDB_FOP_WIND);

        /*record into the database*/
        ret = ctr_insert_wind(frame, this, _inode_cx);
        if (ret) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        CTR_MSG_INSERT_WRITEV_WIND_FAILED,
                        "Failed to insert writev wind");
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
        CTR_IF_FOP_FAILED_THEN_GOTO (this, op_ret, op_errno, out);

        ret = ctr_insert_unwind(frame, this,
                        GFDB_FOP_INODE_WRITE, GFDB_FOP_UNWIND);
        if (ret) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        CTR_MSG_INSERT_SETATTR_UNWIND_FAILED,
                        "Failed to insert setattr unwind");
        }

out:
        ctr_free_frame_local (frame);

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
        CTR_IF_INTERNAL_FOP_THEN_GOTO (frame, xdata, out);
        CTR_RECORD_METADATA_HEAT_IS_DISABLED_THEN_GOTO (this, out);

         /*Fill ctr inode context*/
        FILL_CTR_INODE_CONTEXT(_inode_cx, loc->inode->ia_type,
                loc->inode->gfid, NULL, NULL, GFDB_FOP_INODE_WRITE,
                GFDB_FOP_WIND);

        /*record into the database*/
        ret = ctr_insert_wind(frame, this, _inode_cx);
        if (ret) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        CTR_MSG_INSERT_SETATTR_WIND_FAILED,
                        "Failed to insert setattr wind");
        }
out:

        STACK_WIND (frame, ctr_setattr_cbk, FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->setattr, loc, stbuf,
                    valid, xdata);

        return 0;
}

/*************************** fsetattr ***************************************/
int32_t
ctr_fsetattr_cbk (call_frame_t *frame,
                        void *cookie, xlator_t *this, int32_t op_ret,
                        int32_t op_errno, struct iatt *preop_stbuf,
                        struct iatt *postop_stbuf, dict_t *xdata)
{
        int ret = -1;

        CTR_IS_DISABLED_THEN_GOTO(this, out);
        CTR_IF_FOP_FAILED_THEN_GOTO (this, op_ret, op_errno, out);

        ret = ctr_insert_unwind(frame, this,
                        GFDB_FOP_INODE_WRITE, GFDB_FOP_UNWIND);
        if (ret) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        CTR_MSG_INSERT_SETATTR_UNWIND_FAILED,
                        "Failed to insert fsetattr unwind");
        }

out:
        ctr_free_frame_local (frame);

        STACK_UNWIND_STRICT (fsetattr, frame, op_ret, op_errno,
                                preop_stbuf, postop_stbuf, xdata);

        return 0;
}


int32_t
ctr_fsetattr (call_frame_t *frame,
                    xlator_t *this, fd_t *fd,
                    struct iatt *stbuf, int32_t valid, dict_t *xdata)
{
        int ret = -1;
        gf_ctr_inode_context_t ctr_inode_cx;
        gf_ctr_inode_context_t *_inode_cx = &ctr_inode_cx;

        CTR_IS_DISABLED_THEN_GOTO(this, out);
        CTR_IF_INTERNAL_FOP_THEN_GOTO (frame, xdata, out);
        CTR_RECORD_METADATA_HEAT_IS_DISABLED_THEN_GOTO (this, out);

         /*Fill ctr inode context*/
        FILL_CTR_INODE_CONTEXT(_inode_cx, fd->inode->ia_type,
                fd->inode->gfid, NULL, NULL, GFDB_FOP_INODE_WRITE,
                GFDB_FOP_WIND);

        /*record into the database*/
        ret = ctr_insert_wind(frame, this, _inode_cx);
        if (ret) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        CTR_MSG_INSERT_SETATTR_WIND_FAILED,
                        "Failed to insert fsetattr wind");
        }
out:
        STACK_WIND (frame, ctr_fsetattr_cbk,
                    FIRST_CHILD (this), FIRST_CHILD (this)->fops->fsetattr,
                    fd, stbuf, valid, xdata);

        return 0;
}
/****************************fremovexattr************************************/

int32_t
ctr_fremovexattr_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                          int32_t op_ret, int32_t op_errno, dict_t *xdata)
{
        int ret = -1;

        CTR_IS_DISABLED_THEN_GOTO(this, out);
        CTR_IF_FOP_FAILED_THEN_GOTO (this, op_ret, op_errno, out);

        ret = ctr_insert_unwind(frame, this,
                        GFDB_FOP_INODE_WRITE, GFDB_FOP_UNWIND);
        if (ret) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        CTR_MSG_INSERT_FREMOVEXATTR_UNWIND_FAILED,
                        "Failed to insert fremovexattr unwind");
        }

out:
        ctr_free_frame_local (frame);

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
        CTR_IF_INTERNAL_FOP_THEN_GOTO (frame, xdata, out);
        CTR_RECORD_METADATA_HEAT_IS_DISABLED_THEN_GOTO (this, out);

         /*Fill ctr inode context*/
        FILL_CTR_INODE_CONTEXT(_inode_cx, fd->inode->ia_type,
                fd->inode->gfid, NULL, NULL, GFDB_FOP_INODE_WRITE,
                GFDB_FOP_WIND);

        /*record into the database*/
        ret = ctr_insert_wind(frame, this, _inode_cx);
        if (ret) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        CTR_MSG_INSERT_FREMOVEXATTR_WIND_FAILED,
                        "Failed to insert fremovexattr wind");
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
        CTR_IF_FOP_FAILED_THEN_GOTO (this, op_ret, op_errno, out);
        CTR_IF_INTERNAL_FOP_THEN_GOTO (frame, xdata, out);


        ret = ctr_insert_unwind(frame, this,
                        GFDB_FOP_INODE_WRITE, GFDB_FOP_UNWIND);
        if (ret) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        CTR_MSG_INSERT_REMOVEXATTR_UNWIND_FAILED,
                        "Failed to insert removexattr unwind");
        }

out:
        ctr_free_frame_local (frame);

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
        CTR_IF_INTERNAL_FOP_THEN_GOTO (frame, xdata, out);
        CTR_RECORD_METADATA_HEAT_IS_DISABLED_THEN_GOTO (this, out);

         /*Fill ctr inode context*/
        FILL_CTR_INODE_CONTEXT(_inode_cx, loc->inode->ia_type,
                loc->inode->gfid, NULL, NULL, GFDB_FOP_INODE_WRITE,
                GFDB_FOP_WIND);

        /*record into the database*/
        ret = ctr_insert_wind(frame, this, _inode_cx);
        if (ret) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        CTR_MSG_INSERT_REMOVEXATTR_WIND_FAILED,
                        "Failed to insert removexattr wind");
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
        CTR_IF_FOP_FAILED_THEN_GOTO (this, op_ret, op_errno, out);

        ret = ctr_insert_unwind(frame, this,
                        GFDB_FOP_INODE_WRITE, GFDB_FOP_UNWIND);
        if (ret) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        CTR_MSG_INSERT_TRUNCATE_UNWIND_FAILED,
                        "Failed to insert truncate unwind");
        }


out:
        ctr_free_frame_local (frame);

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
        CTR_IF_INTERNAL_FOP_THEN_GOTO (frame, xdata, out);

         /*Fill ctr inode context*/
        FILL_CTR_INODE_CONTEXT(_inode_cx, loc->inode->ia_type,
                loc->inode->gfid, NULL, NULL, GFDB_FOP_INODE_WRITE,
                GFDB_FOP_WIND);

        /*record into the database*/
        ret = ctr_insert_wind(frame, this, _inode_cx);
        if (ret) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        CTR_MSG_INSERT_TRUNCATE_WIND_FAILED,
                        "Failed to insert truncate wind");
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
        CTR_IF_FOP_FAILED_THEN_GOTO (this, op_ret, op_errno, out);

        ret = ctr_insert_unwind(frame, this,
                        GFDB_FOP_INODE_WRITE, GFDB_FOP_UNWIND);
        if (ret) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        CTR_MSG_INSERT_FTRUNCATE_UNWIND_FAILED,
                        "Failed to insert ftruncate unwind");
        }

out:
        ctr_free_frame_local (frame);

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
        CTR_IF_INTERNAL_FOP_THEN_GOTO (frame, xdata, out);

         /*Fill ctr inode context*/
        FILL_CTR_INODE_CONTEXT(_inode_cx, fd->inode->ia_type,
                fd->inode->gfid, NULL, NULL, GFDB_FOP_INODE_WRITE,
                GFDB_FOP_WIND);

        /*record into the database*/
        ret = ctr_insert_wind(frame, this, _inode_cx);
        if (ret) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        CTR_MSG_INSERT_FTRUNCATE_WIND_FAILED,
                        "Failed to insert ftruncate wind");
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
        int ret                         = -1;
        uint32_t remaining_links        = -1;
        gf_ctr_local_t *ctr_local       = NULL;
        gfdb_fop_type_t fop_type        = GFDB_FOP_INVALID_OP;
        gfdb_fop_path_t fop_path        = GFDB_FOP_INVALID;

        GF_ASSERT(frame);
        GF_ASSERT(this);

        CTR_IS_DISABLED_THEN_GOTO (this, out);
        CTR_IF_FOP_FAILED_THEN_GOTO (this, op_ret, op_errno, out);

        ret = ctr_insert_unwind (frame, this,
                        GFDB_FOP_DENTRY_WRITE, GFDB_FOP_UNWIND);
        if (ret) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        CTR_MSG_INSERT_RENAME_UNWIND_FAILED,
                        "Failed to insert rename unwind");
                goto out;
        }

        if (!xdata)
                goto out;
        /*
         *
         * Extracting GF_RESPONSE_LINK_COUNT_XDATA from POSIX Xlator
         * This is only set when we are overwriting hardlinks.
         *
         * */
        ret = dict_get_uint32 (xdata , GF_RESPONSE_LINK_COUNT_XDATA,
                                &remaining_links);
        if (ret) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        CTR_MSG_GET_CTR_RESPONSE_LINK_COUNT_XDATA_FAILED,
                        "Failed to getting GF_RESPONSE_LINK_COUNT_XDATA");
                remaining_links = -1;
                goto out;
        }

        ctr_local = frame->local;
        if (!ctr_local) {
                gf_msg (this->name, GF_LOG_ERROR, 0, CTR_MSG_NULL_LOCAL,
                        "ctr_local is NULL.");
                goto out;
        }

        /* This is not the only link */
        if (remaining_links > 1) {
                fop_type = GFDB_FOP_DENTRY_WRITE;
                fop_path = GFDB_FOP_UNDEL;
        }
        /* Last link that was deleted */
        else if (remaining_links == 1) {
                fop_type = GFDB_FOP_DENTRY_WRITE;
                fop_path = GFDB_FOP_UNDEL_ALL;
        } else {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        CTR_MSG_INSERT_RENAME_UNWIND_FAILED,
                        "Invalid link count from posix");
                goto out;
        }

        ret = ctr_delete_hard_link_from_db (this,
                                    CTR_DB_REC(ctr_local).old_gfid,
                                    CTR_DB_REC(ctr_local).pargfid,
                                    CTR_DB_REC(ctr_local).file_name,
                                    fop_type, fop_path);
        if (ret) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        CTR_MSG_INSERT_UNLINK_UNWIND_FAILED,
                        "Failed to delete records of %s",
                        CTR_DB_REC(ctr_local).old_file_name);
        }

out:
        ctr_free_frame_local (frame);

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
        int ret                                         = -1;
        gf_ctr_inode_context_t ctr_inode_cx;
        gf_ctr_inode_context_t *_inode_cx               = &ctr_inode_cx;
        gf_ctr_link_context_t new_link_cx, old_link_cx;
        gf_ctr_link_context_t *_nlink_cx                = &new_link_cx;
        gf_ctr_link_context_t *_olink_cx                = &old_link_cx;
        int is_dict_created                             = 0;
        ctr_xlator_ctx_t *ctr_xlator_ctx                = NULL;

        CTR_IS_DISABLED_THEN_GOTO(this, out);
        CTR_IF_INTERNAL_FOP_THEN_GOTO (frame, xdata, out);

        /*Fill old link context*/
        FILL_CTR_LINK_CX(_olink_cx, oldloc->pargfid, oldloc->name, out);

        /*Fill new link context*/
        FILL_CTR_LINK_CX(_nlink_cx, newloc->pargfid, newloc->name, out);

         /*Fill ctr inode context*/
        FILL_CTR_INODE_CONTEXT(_inode_cx, oldloc->inode->ia_type,
                oldloc->inode->gfid, _nlink_cx, _olink_cx,
                GFDB_FOP_DENTRY_WRITE, GFDB_FOP_WIND);


        /* If the rename is a overwrite of hardlink
         * rename ("file1", "file2")
         * file1 is hardlink for gfid say 00000000-0000-0000-0000-00000000000A
         * file2 is hardlink for gfid say 00000000-0000-0000-0000-00000000000B
         * so we are saving file2 gfid in old_gfid so that we delete entries
         * from the db during rename callback if the fop is successful
         * */
        if (newloc->inode) {
                /* This is the GFID from where the newloc hardlink will be
                 * unlinked */
                _inode_cx->old_gfid = &newloc->inode->gfid;
        }

        /* Is a metatdata fop */
        _inode_cx->is_metadata_fop = _gf_true;

        /*record into the database*/
        ret = ctr_insert_wind(frame, this, _inode_cx);
        if (ret) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        CTR_MSG_INSERT_RENAME_WIND_FAILED,
                        "Failed to insert rename wind");
        } else {
                /* We are doing updation of hard link in inode context in wind
                 * As we dont get the "inode" in the call back for rename */
                ret = update_hard_link_ctx (frame, this, oldloc->inode);
                if (ret) {
                        gf_msg (this->name, GF_LOG_ERROR, 0,
                                CTR_MSG_UPDATE_HARDLINK_FAILED, "Failed "
                                "updating hard link in ctr inode context");
                        goto out;
                }

                /* If the newloc has an inode. i.e aquiring hardlink of an
                 * exisitng file i.e overwritting a file.
                 * */
                if (newloc->inode) {

                        /* Getting the ctr inode context variable for
                         * inode whose hardlink will be aquired during
                         * the rename
                         * */
                        ctr_xlator_ctx = get_ctr_xlator_ctx (this,
                                                                newloc->inode);
                        if (!ctr_xlator_ctx) {
                                /* Since there is no ctr inode context
                                 * so nothing more to do */
                                ret = 0;
                                goto out;
                        }

                        /* Deleting hardlink from context variable */
                        ret = ctr_delete_hard_link (this, ctr_xlator_ctx,
                                                newloc->pargfid, newloc->name);
                        if (ret) {
                                gf_msg (this->name, GF_LOG_ERROR, 0,
                                        CTR_MSG_DELETE_HARDLINK_FAILED,
                                        "Failed to delete hard link");
                                goto out;
                        }

                        /* Requesting for number of hardlinks on the newloc
                         * inode from POSIX.
                         * */
                        is_dict_created = set_posix_link_request (this, &xdata);
                        if (is_dict_created == -1) {
                                ret = -1;
                                goto out;
                        }
                }
        }

out:
        STACK_WIND (frame, ctr_rename_cbk, FIRST_CHILD (this),
                    FIRST_CHILD (this)->fops->rename,
                    oldloc, newloc, xdata);

        if (is_dict_created == 1) {
                dict_unref (xdata);
        }

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
        CTR_IF_FOP_FAILED_THEN_GOTO (this, op_ret, op_errno, out);

        if (!xdata)
                goto out;

        /*
         *
         * Extracting GF_RESPONSE_LINK_COUNT_XDATA from POSIX Xlator
         *
         * */
        ret = dict_get_uint32 (xdata , GF_RESPONSE_LINK_COUNT_XDATA,
                                &remaining_links);
        if (ret) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        CTR_MSG_GET_CTR_RESPONSE_LINK_COUNT_XDATA_FAILED,
                        "Failed to getting GF_RESPONSE_LINK_COUNT_XDATA");
                remaining_links = -1;
        }

        /*This is not the only link*/
        if (remaining_links != 1) {

                ret = ctr_insert_unwind(frame, this, GFDB_FOP_DENTRY_WRITE,
                                        GFDB_FOP_UNDEL);
                if (ret) {
                        gf_msg (this->name, GF_LOG_ERROR, 0,
                                CTR_MSG_INSERT_UNLINK_UNWIND_FAILED,
                                "Failed to insert unlink unwind");
                }
        }
        /*Last link that was deleted*/
        else if (remaining_links == 1) {

                ret = ctr_insert_unwind(frame, this, GFDB_FOP_DENTRY_WRITE,
                                        GFDB_FOP_UNDEL_ALL);
                if (ret) {
                        gf_msg (this->name, GF_LOG_ERROR, 0,
                                CTR_MSG_INSERT_UNLINK_UNWIND_FAILED,
                                "Failed to insert unlink unwind");
                }
        }

out:
        ctr_free_frame_local (frame);

        STACK_UNWIND_STRICT (unlink, frame, op_ret, op_errno, preparent,
                             postparent, xdata);

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
        gf_boolean_t is_xdata_created = _gf_false;
        struct iatt dummy_stat        = {0};

        GF_ASSERT (frame);

        CTR_IS_DISABLED_THEN_GOTO(this, out);

        /*Fill link context*/
        FILL_CTR_LINK_CX(_link_cx, loc->pargfid, loc->name, out);

         /*Fill ctr inode context*/
        FILL_CTR_INODE_CONTEXT(_inode_cx, loc->inode->ia_type,
                loc->inode->gfid, _link_cx, NULL,
                GFDB_FOP_DENTRY_WRITE, GFDB_FOP_WDEL);

        /*Internal FOP*/
        _inode_cx->is_internal_fop = is_internal_fop (frame, xdata);

        /* Is a metadata FOP */
        _inode_cx->is_metadata_fop = _gf_true;

        /* If its a internal FOP and dht link file donot record*/
        if (_inode_cx->is_internal_fop &&
                        dht_is_linkfile (&dummy_stat, xdata)) {
                goto out;
        }

        /*record into the database*/
        ret = ctr_insert_wind(frame, this, _inode_cx);
        if (ret) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        CTR_MSG_INSERT_UNLINK_UNWIND_FAILED,
                        "Failed to insert unlink wind");
        } else {
                /* We are doing delete of hard link in inode context in wind
                 * As we dont get the "inode" in the call back for rename */
                ret = delete_hard_link_ctx (frame, this, loc->inode);
                if (ret) {
                        gf_msg (this->name, GF_LOG_ERROR, 0,
                                CTR_MSG_DELETE_HARDLINK_FAILED, "Failed "
                                "deleting hard link from ctr inode context");
                }
        }

        /*
         *
         * Sending GF_REQUEST_LINK_COUNT_XDATA
         * to POSIX Xlator to send link count in unwind path
         *
         * */
        /*create xdata if NULL*/
        if (!xdata) {
                xdata = dict_new();
                is_xdata_created = (xdata) ? _gf_true : _gf_false;
        }
        if (!xdata) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        CTR_MSG_XDATA_NULL, "xdata is NULL :Cannot send "
                        "GF_REQUEST_LINK_COUNT_XDATA to posix");
                goto out;
        }

        ret = dict_set_int32 (xdata, GF_REQUEST_LINK_COUNT_XDATA, 1);
        if (ret) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        CTR_MSG_SET_CTR_RESPONSE_LINK_COUNT_XDATA_FAILED,
                        "Failed setting GF_REQUEST_LINK_COUNT_XDATA");
                if (is_xdata_created) {
                        dict_unref (xdata);
                }
                goto out;
        }

out:
        STACK_WIND (frame, ctr_unlink_cbk, FIRST_CHILD (this),
                    FIRST_CHILD (this)->fops->unlink,
                    loc, xflag, xdata);

        if (is_xdata_created)
                dict_unref (xdata);

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
        CTR_IF_FOP_FAILED_THEN_GOTO (this, op_ret, op_errno, out);

        ret = ctr_insert_unwind(frame, this, GFDB_FOP_INODE_WRITE,
                                GFDB_FOP_UNWIND);
        if (ret) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        CTR_MSG_INSERT_FSYNC_UNWIND_FAILED,
                        "Failed to insert fsync unwind");
        }

out:
        ctr_free_frame_local (frame);

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
        CTR_IF_INTERNAL_FOP_THEN_GOTO (frame, xdata, out);

         /*Fill ctr inode context*/
        FILL_CTR_INODE_CONTEXT(_inode_cx, fd->inode->ia_type,
                fd->inode->gfid, NULL, NULL,
                GFDB_FOP_INODE_WRITE, GFDB_FOP_WIND);

        /*record into the database*/
        ret = ctr_insert_wind(frame, this, _inode_cx);
        if (ret) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        CTR_MSG_INSERT_FSYNC_WIND_FAILED,
                        "Failed to insert fsync wind");
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
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        CTR_MSG_INSERT_FSYNC_UNWIND_FAILED,
                        "Failed to insert setxattr unwind");
        }

out:
        ctr_free_frame_local (frame);

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
        CTR_IF_INTERNAL_FOP_THEN_GOTO (frame, xdata, out);
        CTR_RECORD_METADATA_HEAT_IS_DISABLED_THEN_GOTO (this, out);

         /*Fill ctr inode context*/
        FILL_CTR_INODE_CONTEXT(_inode_cx, loc->inode->ia_type,
                loc->inode->gfid, NULL, NULL,
                GFDB_FOP_INODE_WRITE, GFDB_FOP_WIND);

        /*record into the database*/
        ret = ctr_insert_wind(frame, this, _inode_cx);
        if (ret) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        CTR_MSG_INSERT_SETATTR_WIND_FAILED,
                        "Failed to insert setxattr wind");
        }

out:
        STACK_WIND (frame, ctr_setxattr_cbk, FIRST_CHILD (this),
                    FIRST_CHILD (this)->fops->setxattr,
                    loc, xattr, flags, xdata);
        return 0;
}
/**************************** fsetxattr *************************************/
int32_t
ctr_fsetxattr_cbk (call_frame_t *frame,
                         void *cookie, xlator_t *this, int32_t op_ret,
                         int32_t op_errno, dict_t *xdata)
{
        int ret = -1;

        CTR_IS_DISABLED_THEN_GOTO(this, out);
        CTR_IF_FOP_FAILED_THEN_GOTO (this, op_ret, op_errno, out);

        ret = ctr_insert_unwind(frame, this, GFDB_FOP_INODE_WRITE,
                                GFDB_FOP_UNWIND);
        if (ret) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        CTR_MSG_INSERT_FSYNC_UNWIND_FAILED,
                        "Failed to insert fsetxattr unwind");
        }

out:
        ctr_free_frame_local (frame);

        STACK_UNWIND_STRICT (fsetxattr, frame, op_ret, op_errno, xdata);

        return 0;
}

int32_t
ctr_fsetxattr (call_frame_t *frame, xlator_t *this, fd_t *fd, dict_t *dict,
               int32_t flags, dict_t *xdata)
{
        int ret = -1;
        gf_ctr_inode_context_t ctr_inode_cx;
        gf_ctr_inode_context_t *_inode_cx = &ctr_inode_cx;

        CTR_IS_DISABLED_THEN_GOTO(this, out);
        CTR_IF_INTERNAL_FOP_THEN_GOTO (frame, xdata, out);
        CTR_RECORD_METADATA_HEAT_IS_DISABLED_THEN_GOTO (this, out);

         /*Fill ctr inode context*/
        FILL_CTR_INODE_CONTEXT(_inode_cx, fd->inode->ia_type,
                fd->inode->gfid, NULL, NULL,
                GFDB_FOP_INODE_WRITE, GFDB_FOP_WIND);

        /*record into the database*/
        ret = ctr_insert_wind(frame, this, _inode_cx);
        if (ret) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        CTR_MSG_INSERT_SETATTR_WIND_FAILED,
                        "Failed to insert fsetxattr wind");
        }

out:
        STACK_WIND (frame, ctr_fsetxattr_cbk,
                    FIRST_CHILD (this), FIRST_CHILD (this)->fops->fsetxattr,
                    fd, dict, flags, xdata);
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
        ctr_heal_ret_val_t ret_val = CTR_CTX_ERROR;

        CTR_IS_DISABLED_THEN_GOTO(this, out);
        CTR_IF_FOP_FAILED_THEN_GOTO (this, op_ret, op_errno, out);

        /* Add hard link to the list */
        ret_val = add_hard_link_ctx (frame, this, inode);
        if (ret_val == CTR_CTX_ERROR) {
                gf_msg_trace (this->name, 0, "Failed adding hard link");
        }

        ret = ctr_insert_unwind(frame, this, GFDB_FOP_CREATE_WRITE,
                                GFDB_FOP_UNWIND);
        if (ret) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        CTR_MSG_INSERT_MKNOD_UNWIND_FAILED,
                        "Failed to insert mknod unwind");
        }

out:
        ctr_free_frame_local (frame);

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
        CTR_IF_INTERNAL_FOP_THEN_GOTO (frame, xdata, out);

        GF_ASSERT(frame);
        GF_ASSERT(frame->root);

        /*get gfid from xdata dict*/
        ret = dict_get_ptr (xdata, "gfid-req", &uuid_req);
        if (ret) {
                gf_msg_debug (this->name, 0, "failed to get gfid from dict");
                goto out;
        }
        gf_uuid_copy (gfid, uuid_req);

        /*fill ctr link context*/
        FILL_CTR_LINK_CX (_link_cx, loc->pargfid, loc->name, out);

         /*Fill ctr inode context*/
        FILL_CTR_INODE_CONTEXT (_inode_cx, loc->inode->ia_type,
                *ptr_gfid, _link_cx, NULL,
                GFDB_FOP_CREATE_WRITE, GFDB_FOP_WIND);

        /*record into the database*/
        ret = ctr_insert_wind(frame, this, _inode_cx);
        if (ret) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        CTR_MSG_INSERT_MKNOD_WIND_FAILED,
                        "Failed to insert mknod wind");
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
        CTR_IF_FOP_FAILED_THEN_GOTO (this, op_ret, op_errno, out);

        ret = add_hard_link_ctx (frame, this, inode);
        if (ret) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        CTR_MSG_ADD_HARDLINK_FAILED,
                        "Failed adding hard link");
        }

        ret = ctr_insert_unwind(frame, this, GFDB_FOP_CREATE_WRITE,
                                GFDB_FOP_UNWIND);
        if (ret) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        CTR_MSG_INSERT_CREATE_UNWIND_FAILED,
                        "Failed to insert create unwind");
        }

out:
        ctr_free_frame_local (frame);

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
        struct iatt dummy_stat                  = {0};

        CTR_IS_DISABLED_THEN_GOTO(this, out);

        GF_ASSERT(frame);
        GF_ASSERT(frame->root);

        /*Get GFID from Xdata dict*/
        ret = dict_get_ptr (xdata, "gfid-req", &uuid_req);
        if (ret) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        CTR_MSG_GET_GFID_FROM_DICT_FAILED,
                        "failed to get gfid from dict");
                goto out;
        }
        gf_uuid_copy (gfid, uuid_req);

        /*fill ctr link context*/
        FILL_CTR_LINK_CX(_link_cx, loc->pargfid, loc->name, out);

         /*Fill ctr inode context*/
        FILL_CTR_INODE_CONTEXT(_inode_cx, loc->inode->ia_type,
                *ptr_gfid, _link_cx, NULL,
                GFDB_FOP_CREATE_WRITE, GFDB_FOP_WIND);

        /*Internal FOP*/
        _inode_cx->is_internal_fop = is_internal_fop (frame, xdata);

        /* If its a internal FOP and dht link file donot record*/
        if (_inode_cx->is_internal_fop &&
                        dht_is_linkfile (&dummy_stat, xdata)) {
                goto out;
        }

        /*record into the database*/
        ret = ctr_insert_wind(frame, this, &ctr_inode_cx);
        if (ret) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        CTR_MSG_INSERT_CREATE_WIND_FAILED,
                        "Failed to insert create wind");
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
        CTR_IF_FOP_FAILED_THEN_GOTO (this, op_ret, op_errno, out);

        /* Add hard link to the list */
        ret = add_hard_link_ctx (frame, this, inode);
        if (ret) {
                gf_msg_trace (this->name, 0, "Failed adding hard link");
        }

        ret = ctr_insert_unwind(frame, this, GFDB_FOP_DENTRY_WRITE,
                                GFDB_FOP_UNWIND);
        if (ret) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        CTR_MSG_INSERT_CREATE_UNWIND_FAILED,
                        "Failed to insert create unwind");
        }

out:
        ctr_free_frame_local (frame);

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
        struct iatt dummy_stat          = {0};

        CTR_IS_DISABLED_THEN_GOTO(this, out);

        GF_ASSERT(frame);
        GF_ASSERT(frame->root);

        /*fill ctr link context*/
        FILL_CTR_LINK_CX(_link_cx, newloc->pargfid, newloc->name, out);

         /*Fill ctr inode context*/
        FILL_CTR_INODE_CONTEXT(_inode_cx, oldloc->inode->ia_type,
                oldloc->inode->gfid, _link_cx, NULL,
                GFDB_FOP_DENTRY_WRITE, GFDB_FOP_WIND);

        /*Internal FOP*/
        _inode_cx->is_internal_fop = is_internal_fop (frame, xdata);

        /* Is a metadata fop */
        _inode_cx->is_metadata_fop = _gf_true;

        /* If its a internal FOP and dht link file donot record*/
        if (_inode_cx->is_internal_fop &&
                        dht_is_linkfile (&dummy_stat, xdata)) {
                goto out;
        }


        /*record into the database*/
        ret = ctr_insert_wind(frame, this, _inode_cx);
        if (ret) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        CTR_MSG_INSERT_LINK_WIND_FAILED,
                        "Failed to insert link wind");
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
        CTR_IF_FOP_FAILED_THEN_GOTO (this, op_ret, op_errno, out);

        ret = ctr_insert_unwind(frame, this, GFDB_FOP_INODE_READ,
                                GFDB_FOP_UNWIND);
        if (ret) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        CTR_MSG_INSERT_CREATE_UNWIND_FAILED,
                        "Failed to insert create unwind");
        }

out:
        ctr_free_frame_local (frame);

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
        CTR_IF_INTERNAL_FOP_THEN_GOTO (frame, xdata, out);

         /*Fill ctr inode context*/
        FILL_CTR_INODE_CONTEXT(_inode_cx, fd->inode->ia_type,
                fd->inode->gfid, NULL, NULL,
                GFDB_FOP_INODE_READ, GFDB_FOP_WIND);

        /*record into the database*/
        ret = ctr_insert_wind(frame, this, _inode_cx);
        if (ret) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        CTR_MSG_INSERT_READV_WIND_FAILED,
                        "Failed to insert readv wind");
        }

out:
        STACK_WIND (frame, ctr_readv_cbk, FIRST_CHILD (this),
                    FIRST_CHILD (this)->fops->readv,
                    fd, size, off, flags, xdata);
        return 0;
}

/*******************************ctr_ipc****************************************/

/*This is the call back function per record/file from data base*/
static int
ctr_db_query_callback (gfdb_query_record_t *gfdb_query_record,
                        void *args) {
        int ret = -1;
        ctr_query_cbk_args_t *query_cbk_args = args;

        GF_VALIDATE_OR_GOTO ("ctr", query_cbk_args, out);

        ret = gfdb_write_query_record (query_cbk_args->query_fd,
                                       gfdb_query_record);
        if (ret) {
                gf_msg ("ctr", GF_LOG_ERROR, 0,
                        CTR_MSG_FATAL_ERROR,
                        "Failed to write to query file");
                goto out;
        }

        query_cbk_args->count++;

        ret = 0;
out:
        return ret;
}

/* This function does all the db queries related to tiering and
 * generates/populates new/existing query file
 * inputs:
 * xlator_t *this : CTR Translator
 * void *conn_node : Database connection
 * char *query_file: the query file that needs to be updated
 * gfdb_ipc_ctr_params_t *ipc_ctr_params: the query parameters
 * Return:
 * On success 0
 * On failure -1
 * */
int
ctr_db_query (xlator_t *this,
              void *conn_node,
              char *query_file,
              gfdb_ipc_ctr_params_t *ipc_ctr_params)
{
        int ret = -1;
        ctr_query_cbk_args_t query_cbk_args = {0};

        GF_VALIDATE_OR_GOTO ("ctr", this, out);
        GF_VALIDATE_OR_GOTO (this->name, conn_node, out);
        GF_VALIDATE_OR_GOTO (this->name, query_file, out);
        GF_VALIDATE_OR_GOTO (this->name, ipc_ctr_params, out);

        /*Query for eligible files from db*/
        query_cbk_args.query_fd = open (query_file,
                        O_WRONLY | O_CREAT | O_APPEND,
                        S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);
        if (query_cbk_args.query_fd < 0) {
                gf_msg (this->name, GF_LOG_ERROR, errno,
                        CTR_MSG_FATAL_ERROR,
                        "Failed to open query file %s", query_file);
                goto out;
        }
        if (!ipc_ctr_params->is_promote) {
                if (ipc_ctr_params->emergency_demote) {
                        /* emergency demotion mode */
                        ret = find_all (conn_node,
                                ctr_db_query_callback,
                                (void *)&query_cbk_args,
                                ipc_ctr_params->query_limit);
                } else {
                        if (ipc_ctr_params->write_freq_threshold == 0 &&
                                ipc_ctr_params->read_freq_threshold == 0) {
                                ret = find_unchanged_for_time (
                                        conn_node,
                                        ctr_db_query_callback,
                                        (void *)&query_cbk_args,
                                        &ipc_ctr_params->time_stamp);
                        } else {
                                ret = find_unchanged_for_time_freq (
                                        conn_node,
                                        ctr_db_query_callback,
                                        (void *)&query_cbk_args,
                                        &ipc_ctr_params->time_stamp,
                                        ipc_ctr_params->write_freq_threshold,
                                        ipc_ctr_params->read_freq_threshold,
                                        _gf_false);
                        }
                }
        } else {
                if (ipc_ctr_params->write_freq_threshold == 0 &&
                        ipc_ctr_params->read_freq_threshold == 0) {
                        ret = find_recently_changed_files (
                                conn_node,
                                ctr_db_query_callback,
                                (void *)&query_cbk_args,
                                &ipc_ctr_params->time_stamp);
                } else {
                        ret = find_recently_changed_files_freq (
                                conn_node,
                                ctr_db_query_callback,
                                (void *)&query_cbk_args,
                                &ipc_ctr_params->time_stamp,
                                ipc_ctr_params->write_freq_threshold,
                                ipc_ctr_params->read_freq_threshold,
                                _gf_false);
                }
        }
        if (ret) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        CTR_MSG_FATAL_ERROR,
                        "FATAL: query from db failed");
                        goto out;
        }

        ret = clear_files_heat (conn_node);
        if (ret) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        CTR_MSG_FATAL_ERROR,
                        "FATAL: Failed to clear db entries");
                        goto out;
        }

        ret = 0;
out:

        if (!ret)
                ret = query_cbk_args.count;

        if (query_cbk_args.query_fd >= 0) {
                sys_close (query_cbk_args.query_fd);
                query_cbk_args.query_fd = -1;
        }

        return ret;
}

void *
ctr_compact_thread (void *args)
{
        int ret = -1;
        void *db_conn = NULL;

        xlator_t *this = NULL;
        gf_ctr_private_t *priv = NULL;
        gf_boolean_t compact_active = _gf_false;
        gf_boolean_t compact_mode_switched = _gf_false;

        this = (xlator_t *)args;

        GF_VALIDATE_OR_GOTO("ctr", this, out);

        priv = this->private;

        db_conn = priv->_db_conn;
        compact_active = priv->compact_active;
        compact_mode_switched = priv->compact_mode_switched;

        gf_msg ("ctr-compact", GF_LOG_INFO, 0, CTR_MSG_SET,
                "Starting compaction");

        ret = compact_db(db_conn, compact_active,
                         compact_mode_switched);

        if (ret) {
                gf_msg ("ctr-compact", GF_LOG_ERROR, 0, CTR_MSG_SET,
                        "Failed to perform the compaction");
        }

        ret = pthread_mutex_lock (&priv->compact_lock);

        if (ret) {
                gf_msg (this->name, GF_LOG_ERROR, 0, CTR_MSG_SET,
                        "Failed to acquire lock");
                goto out;
        }

        /* We are done compaction on this brick. Set all flags to false */
        priv->compact_active = _gf_false;
        priv->compact_mode_switched = _gf_false;

        ret = pthread_mutex_unlock (&priv->compact_lock);

        if (ret) {
                gf_msg (this->name, GF_LOG_ERROR, 0, CTR_MSG_SET,
                        "Failed to release lock");
                goto out;
        }

out:
        return NULL;
}

int
ctr_ipc_helper (xlator_t *this, dict_t *in_dict,
                dict_t *out_dict)
{
        int ret = -1;
        char *ctr_ipc_ops = NULL;
        gf_ctr_private_t *priv = NULL;
        char *db_version = NULL;
        char *db_param_key = NULL;
        char *db_param = NULL;
        char *query_file = NULL;
        gfdb_ipc_ctr_params_t *ipc_ctr_params = NULL;
        int result = 0;
        pthread_t compact_thread;

        GF_VALIDATE_OR_GOTO ("ctr", this, out);
        GF_VALIDATE_OR_GOTO (this->name, this->private, out);
        priv = this->private;
        GF_VALIDATE_OR_GOTO (this->name, priv->_db_conn, out);
        GF_VALIDATE_OR_GOTO (this->name, in_dict, out);
        GF_VALIDATE_OR_GOTO (this->name, out_dict, out);

        GET_DB_PARAM_FROM_DICT(this->name, in_dict, GFDB_IPC_CTR_KEY,
                                ctr_ipc_ops, out);

        /*if its a db clear operation */
        if (strncmp (ctr_ipc_ops, GFDB_IPC_CTR_CLEAR_OPS,
                        strlen (GFDB_IPC_CTR_CLEAR_OPS)) == 0) {

                ret = clear_files_heat (priv->_db_conn);
                if (ret)
                        goto out;

        } /* if its a query operation, in  which case its query + clear db*/
        else if (strncmp (ctr_ipc_ops, GFDB_IPC_CTR_QUERY_OPS,
                                strlen (GFDB_IPC_CTR_QUERY_OPS)) == 0) {

                ret = dict_get_str (in_dict, GFDB_IPC_CTR_GET_QFILE_PATH,
                                                                &query_file);
                if (ret) {
                        gf_msg(this->name, GF_LOG_ERROR, 0, CTR_MSG_SET,
                                        "Failed extracting query file path");
                        goto out;
                }

                ret = dict_get_bin (in_dict, GFDB_IPC_CTR_GET_QUERY_PARAMS,
                                   (void *)&ipc_ctr_params);
                if (ret) {
                        gf_msg(this->name, GF_LOG_ERROR, 0, CTR_MSG_SET,
                                        "Failed extracting query parameters");
                        goto out;
                }

                ret = ctr_db_query (this, priv->_db_conn, query_file,
                                ipc_ctr_params);

                ret = dict_set_int32 (out_dict,
                                      GFDB_IPC_CTR_RET_QUERY_COUNT, ret);
                if (ret) {
                        gf_msg(this->name, GF_LOG_ERROR, 0, CTR_MSG_SET,
                                        "Failed setting query reply");
                        goto out;
                }

        } /* if its a query for db version */
        else if (strncmp (ctr_ipc_ops, GFDB_IPC_CTR_GET_DB_VERSION_OPS,
                        strlen (GFDB_IPC_CTR_GET_DB_VERSION_OPS)) == 0) {

                ret = get_db_version (priv->_db_conn, &db_version);
                if (ret == -1 || !db_version) {
                        gf_msg(this->name, GF_LOG_ERROR, 0, CTR_MSG_SET,
                                        "Failed extracting db version ");
                        goto out;
                }

                SET_DB_PARAM_TO_DICT(this->name, out_dict,
                                        GFDB_IPC_CTR_RET_DB_VERSION,
                                        db_version, ret, error);

        } /* if its a query for a db setting */
        else if (strncmp (ctr_ipc_ops, GFDB_IPC_CTR_GET_DB_PARAM_OPS,
                                strlen (GFDB_IPC_CTR_GET_DB_PARAM_OPS)) == 0) {

                ret = dict_get_str (in_dict, GFDB_IPC_CTR_GET_DB_KEY,
                                &db_param_key);
                if (ret) {
                        gf_msg(this->name, GF_LOG_ERROR, 0, CTR_MSG_SET,
                                        "Failed extracting db param key");
                        goto out;
                }

                ret = get_db_params (priv->_db_conn, db_param_key, &db_param);
                if (ret == -1 || !db_param) {
                        goto out;
                }

                SET_DB_PARAM_TO_DICT(this->name, out_dict,
                                        db_param_key,
                                        db_param, ret, error);
        } /* if its an attempt to compact the database */
        else if (strncmp (ctr_ipc_ops, GFDB_IPC_CTR_SET_COMPACT_PRAGMA,
                          strlen (GFDB_IPC_CTR_SET_COMPACT_PRAGMA)) == 0) {

                ret = pthread_mutex_lock (&priv->compact_lock);
                if (ret) {
                        gf_msg (this->name, GF_LOG_ERROR, 0, CTR_MSG_SET,
                                "Failed to acquire lock for compaction");
                        goto out;
                }

                if ((priv->compact_active || priv->compact_mode_switched)) {
                        /* Compaction in progress. LEAVE */
                        gf_msg (this->name, GF_LOG_ERROR, 0, CTR_MSG_SET,
                               "Compaction already in progress.");
                        pthread_mutex_unlock (&priv->compact_lock);
                        goto out;
                }
                /* At this point, we should be the only one on the brick */
                /* compacting */

                /* Grab the arguments from the dictionary */
                ret = dict_get_int32 (in_dict, "compact_active", &result);
                if (ret) {
                        gf_msg (this->name, GF_LOG_ERROR, 0, CTR_MSG_SET,
                               "Failed to get compaction type");
                        goto out;
                }

                if (result) {
                        priv->compact_active = _gf_true;
                }

                ret = dict_get_int32 (in_dict, "compact_mode_switched"
                                     , &result);
                if (ret) {
                        gf_msg (this->name, GF_LOG_ERROR, 0, CTR_MSG_SET,
                               "Failed to see if compaction switched");
                        goto out;
                }

                if (result) {
                        priv->compact_mode_switched = _gf_true;
                        gf_msg ("ctr-compact", GF_LOG_TRACE, 0, CTR_MSG_SET,
                                "Pre-thread: Compact mode switch is true");
                } else {
                        gf_msg ("ctr-compact", GF_LOG_TRACE, 0, CTR_MSG_SET,
                                "Pre-thread: Compact mode switch is false");
                }

                ret = pthread_mutex_unlock (&priv->compact_lock);
                if (ret) {
                        gf_msg (this->name, GF_LOG_ERROR, 0, CTR_MSG_SET,
                                "Failed to release lock for compaction");
                        goto out;
                }

                ret = pthread_create (&compact_thread, NULL, ctr_compact_thread,
                                      (void *)this);

                if (ret) {
                        gf_msg (this->name, GF_LOG_ERROR, 0, CTR_MSG_SET,
                                "Failed to spawn compaction thread");
                        goto out;
                }

                goto out;
        } /* default case */
        else {
                goto out;
        }

        ret = 0;
        goto out;
error:
        GF_FREE (db_param_key);
        GF_FREE (db_param);
        GF_FREE (db_version);
out:
        return ret;
}


/* IPC Call from tier migrator to clear the heat on the DB */
int32_t
ctr_ipc (call_frame_t *frame, xlator_t *this, int32_t op,
        dict_t *in_dict)
{
        int ret                         = -1;
        gf_ctr_private_t *priv         = NULL;
        dict_t *out_dict                = NULL;

        GF_ASSERT(this);
        priv = this->private;
        GF_ASSERT (priv);
        GF_ASSERT(priv->_db_conn);
        GF_VALIDATE_OR_GOTO (this->name, in_dict, wind);


        if (op != GF_IPC_TARGET_CTR)
                goto wind;

        out_dict = dict_new();
        if (!out_dict) {
                goto out;
        }

        ret = ctr_ipc_helper (this, in_dict, out_dict);
        if (ret) {
                gf_msg(this->name, GF_LOG_ERROR, 0, CTR_MSG_SET,
                "Failed in ctr_ipc_helper");
        }
out:

        STACK_UNWIND_STRICT (ipc, frame, ret, 0, out_dict);

        if (out_dict)
                dict_unref(out_dict);

        return 0;

 wind:
        STACK_WIND (frame, default_ipc_cbk, FIRST_CHILD (this),
                    FIRST_CHILD (this)->fops->ipc, op, in_dict);



        return 0;
}


/******************************************************************************/
int
reconfigure (xlator_t *this, dict_t *options)
{
        char *temp_str = NULL;
        int ret = 0;
        gf_ctr_private_t *priv = NULL;

        priv = this->private;
        if (dict_get_str(options, "changetimerecorder.frequency",
                         &temp_str)) {
                gf_msg(this->name, GF_LOG_TRACE, 0, CTR_MSG_SET, "set");
        }

        GF_OPTION_RECONF ("ctr-enabled", priv->enabled, options,
                          bool, out);

        GF_OPTION_RECONF ("record-counters", priv->ctr_record_counter, options,
                          bool, out);

        GF_OPTION_RECONF ("ctr-record-metadata-heat",
                          priv->ctr_record_metadata_heat, options,
                          bool, out);

        GF_OPTION_RECONF ("ctr_link_consistency", priv->ctr_link_consistency,
                        options, bool, out);

        GF_OPTION_RECONF ("ctr_lookupheal_inode_timeout",
                                priv->ctr_lookupheal_inode_timeout,
                                options, uint64, out);

        GF_OPTION_RECONF ("ctr_lookupheal_link_timeout",
                                priv->ctr_lookupheal_link_timeout,
                                options, uint64, out);

        GF_OPTION_RECONF ("record-exit", priv->ctr_record_unwind, options,
                          bool, out);

        GF_OPTION_RECONF ("record-entry", priv->ctr_record_wind, options,
                          bool, out);




        /* If database is sqlite */
        if (priv->gfdb_db_type == GFDB_SQLITE3) {

                /* AUTOCHECKPOINT */
                if (dict_get_str (options, GFDB_SQL_PARAM_WAL_AUTOCHECK,
                                &temp_str) == 0) {
                        ret = set_db_params (priv->_db_conn,
                                        "wal_autocheckpoint", temp_str);
                        if (ret) {
                                gf_msg (this->name, GF_LOG_ERROR, 0,
                                        CTR_MSG_SET_VALUE_TO_SQL_PARAM_FAILED,
                                        "Failed  to set %s",
                                        GFDB_SQL_PARAM_WAL_AUTOCHECK);
                        }
                }

                /* CACHE_SIZE */
                if (dict_get_str (options, GFDB_SQL_PARAM_CACHE_SIZE, &temp_str)
                                == 0) {
                        ret = set_db_params (priv->_db_conn, "cache_size",
                                        temp_str);
                        if (ret) {
                                gf_msg (this->name, GF_LOG_ERROR, 0,
                                        CTR_MSG_SET_VALUE_TO_SQL_PARAM_FAILED,
                                        "Failed  to set %s",
                                        GFDB_SQL_PARAM_CACHE_SIZE);
                        }
                }
        }

        ret = 0;

out:

        return ret;
}

/****************************init********************************************/

int32_t
init (xlator_t *this)
{
        gf_ctr_private_t *priv = NULL;
        int ret_db              = -1;
        dict_t *params_dict      = NULL;

        GF_VALIDATE_OR_GOTO ("ctr", this, error);

        if (!this->children || this->children->next) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        CTR_MSG_FATAL_ERROR,
                        "FATAL: ctr should have exactly one child");
                goto error;
        }

        if (!this->parents) {
                gf_msg (this->name, GF_LOG_WARNING, 0,
                        CTR_MSG_DANGLING_VOLUME,
                        "dangling volume. check volfile ");
        }

        priv = GF_CALLOC (1, sizeof (*priv), gf_ctr_mt_private_t);
        if (!priv) {
                gf_msg (this->name, GF_LOG_ERROR, ENOMEM,
                        CTR_MSG_CALLOC_FAILED,
                        "Calloc did not work!!!");
                goto error;
        }

        /*Default values for the translator*/
        priv->ctr_record_wind          = _gf_true;
        priv->ctr_record_unwind        = _gf_false;
        priv->ctr_hot_brick            = _gf_false;
        priv->gfdb_db_type             = GFDB_SQLITE3;
        priv->gfdb_sync_type           = GFDB_DB_SYNC;
        priv->enabled                  = _gf_true;
        priv->_db_conn                 = NULL;
        priv->ctr_lookupheal_link_timeout =
                                CTR_DEFAULT_HARDLINK_EXP_PERIOD;
        priv->ctr_lookupheal_inode_timeout =
                                CTR_DEFAULT_INODE_EXP_PERIOD;

        /* For compaction */
        priv->compact_active = _gf_false;
        priv->compact_mode_switched = _gf_false;
        ret_db = pthread_mutex_init (&priv->compact_lock, NULL);

        if (ret_db) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        CTR_MSG_FATAL_ERROR,
                        "FATAL: Failed initializing compaction mutex");
                goto error;
        }

        /*Extract ctr xlator options*/
        ret_db = extract_ctr_options (this, priv);
        if (ret_db) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        CTR_MSG_EXTRACT_CTR_XLATOR_OPTIONS_FAILED,
                        "Failed extracting ctr xlator options");
                goto error;
        }

        params_dict = dict_new ();
        if (!params_dict) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        CTR_MSG_INIT_DB_PARAMS_FAILED,
                        "DB Params cannot initialized!");
                goto error;
        }

        /*Extract db params options*/
        ret_db = extract_db_params(this, params_dict, priv->gfdb_db_type);
        if (ret_db) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        CTR_MSG_EXTRACT_DB_PARAM_OPTIONS_FAILED,
                        "Failed extracting db params options");
                goto error;
        }

        /*Create a memory pool for ctr xlator*/
        this->local_pool = mem_pool_new (gf_ctr_local_t, 64);
        if (!this->local_pool) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        CTR_MSG_CREATE_LOCAL_MEMORY_POOL_FAILED,
                        "failed to create local memory pool");
                goto error;
        }

        /*Initialize Database Connection*/
        priv->_db_conn = init_db(params_dict, priv->gfdb_db_type);
        if (!priv->_db_conn) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                       CTR_MSG_FATAL_ERROR,
                       "FATAL: Failed initializing data base");
                        goto error;
        }


        ret_db = 0;
        goto out;

/*Error handling */
error:

        if (this)
                mem_pool_destroy (this->local_pool);

        if (priv) {
                GF_FREE (priv->ctr_db_path);
        }
        GF_FREE (priv);

        if (params_dict)
                dict_unref (params_dict);

        return -1;

out:

        if (params_dict)
                dict_unref (params_dict);

        this->private = (void *)priv;
        return 0;
}

int32_t
mem_acct_init (xlator_t *this)
{
        int     ret = -1;

        GF_VALIDATE_OR_GOTO ("ctr", this, out);

        ret = xlator_mem_acct_init (this, gf_ctr_mt_end + 1);

        if (ret != 0) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        CTR_MSG_MEM_ACC_INIT_FAILED, "Memory accounting init"
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
                        gf_msg (this->name, GF_LOG_WARNING, 0,
                                CTR_MSG_CLOSE_DB_CONN_FAILED, "Failed closing "
                                "db connection");
                }
                GF_FREE (priv->ctr_db_path);
                if (pthread_mutex_destroy (&priv->compact_lock)) {
                        gf_msg (this->name, GF_LOG_WARNING, 0,
                                CTR_MSG_CLOSE_DB_CONN_FAILED, "Failed to "
                                "destroy the compaction mutex");
                }
        }
        GF_FREE (priv);
        mem_pool_destroy (this->local_pool);

        return;
}

struct xlator_fops fops = {
        /*lookup*/
        .lookup         = ctr_lookup,
        /*write fops */
        .mknod          = ctr_mknod,
        .create         = ctr_create,
        .truncate       = ctr_truncate,
        .ftruncate      = ctr_ftruncate,
        .setxattr       = ctr_setxattr,
        .fsetxattr      = ctr_fsetxattr,
        .removexattr    = ctr_removexattr,
        .fremovexattr   = ctr_fremovexattr,
        .unlink         = ctr_unlink,
        .link           = ctr_link,
        .rename         = ctr_rename,
        .writev         = ctr_writev,
        .setattr        = ctr_setattr,
        .fsetattr       = ctr_fsetattr,
        /*read fops*/
        .readv          = ctr_readv,
        /* IPC call*/
        .ipc            = ctr_ipc
};

struct xlator_cbks cbks = {
        .forget = ctr_forget
};

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
        { .key  = {"ctr-record-metadata-heat"},
          .type = GF_OPTION_TYPE_BOOL,
          .value = {"on", "off"},
          .default_value = "off"
        },
        { .key  = {"ctr_link_consistency"},
          .type = GF_OPTION_TYPE_BOOL,
          .value = {"on", "off"},
          .default_value = "off"
        },
        { .key  = {"ctr_lookupheal_link_timeout"},
          .type = GF_OPTION_TYPE_INT,
          .default_value = "300"
        },
        { .key  = {"ctr_lookupheal_inode_timeout"},
          .type = GF_OPTION_TYPE_INT,
          .default_value = "300"
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
          .default_value = "25000"
        },
        { .key  = {GFDB_SQL_PARAM_CACHE_SIZE},
          .type = GF_OPTION_TYPE_INT,
          .default_value = "12500"
        },
        { .key  = {GFDB_SQL_PARAM_PAGE_SIZE},
          .type = GF_OPTION_TYPE_INT,
          .default_value = "4096"
        },
        { .key  = {NULL} },
};
