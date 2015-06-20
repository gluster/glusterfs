/*
   Copyright (c) 2015 Red Hat, Inc. <http://www.redhat.com>
   This file is part of GlusterFS.

   This file is licensed to you under your choice of the GNU Lesser
   General Public License, version 3 or any later version (LGPLv3 or
   later), or the GNU General Public License, version 2 (GPLv2), in all
   cases as published by the Free Software Foundation.
*/

#ifndef __CTR_HELPER_H
#define __CTR_HELPER_H


#include "xlator.h"
#include "ctr_mem_types.h"
#include "iatt.h"
#include "glusterfs.h"
#include "xlator.h"
#include "logging.h"
#include "common-utils.h"
#include <time.h>
#include <sys/time.h>

#include "gfdb_data_store.h"
#include "ctr-xlator-ctx.h"
#include "ctr-messages.h"

#define CTR_DEFAULT_HARDLINK_EXP_PERIOD 300  /* Five mins */
#define CTR_DEFAULT_INODE_EXP_PERIOD    300 /* Five mins */

/*CTR Xlator Private structure*/
typedef struct gf_ctr_private {
        gf_boolean_t                    enabled;
        char                            *ctr_db_path;
        gf_boolean_t                    ctr_hot_brick;
        gf_boolean_t                    ctr_record_wind;
        gf_boolean_t                    ctr_record_unwind;
        gf_boolean_t                    ctr_record_counter;
        gf_boolean_t                    ctr_link_consistency;
        gfdb_db_type_t                  gfdb_db_type;
        gfdb_sync_type_t                gfdb_sync_type;
        gfdb_conn_node_t                *_db_conn;
        uint64_t                        ctr_hardlink_heal_expire_period;
        uint64_t                        ctr_inode_heal_expire_period;
} gf_ctr_private_t;


/*
 * gf_ctr_local_t is the ctr xlator local data structure that is stored in
 * the call_frame of each FOP.
 *
 * gfdb_db_record: The gf_ctr_local contains a gfdb_db_record object, which is
 * used by the insert_record() api from the libgfdb. The gfdb_db_record object
 * will contain all the inode and hardlink(only for dentry fops: create,
 * mknod,link, unlink, rename).The ctr_local is keep alive till the unwind
 * call and will be release during the unwind. The same gfdb_db_record will
 * used for the unwind insert_record() api, to record unwind in the database.
 *
 * ia_inode_type in gf_ctr_local will tell the type of the inode. This is
 * important for during the unwind path. As we will not have the inode during
 * the unwind path. We would have include this in the gfdb_db_record itself
 * but currently we record only file inode information.
 *
 * is_internal_fop in gf_ctr_local will tell us if this is a internal fop and
 * take special/no action. We dont record change/acces times or increement heat
 * counter for internal fops from rebalancer.
 * */
typedef struct gf_ctr_local {
        gfdb_db_record_t        gfdb_db_record;
        ia_type_t               ia_inode_type;
        gf_boolean_t            is_internal_fop;
        gf_client_pid_t          client_pid;
} gf_ctr_local_t;
/*
 * Easy access of gfdb_db_record of ctr_local
 * */
#define CTR_DB_REC(ctr_local)\
        (ctr_local->gfdb_db_record)

/*Clear db record*/
#define CLEAR_CTR_DB_RECORD(ctr_local)\
do {\
        ctr_local->gfdb_db_record.gfdb_fop_path = GFDB_FOP_INVALID;\
        memset(&(ctr_local->gfdb_db_record.gfdb_wind_change_time),\
                0, sizeof(gfdb_time_t));\
        memset(&(ctr_local->gfdb_db_record.gfdb_unwind_change_time),\
                0, sizeof(gfdb_time_t));\
        gf_uuid_clear (ctr_local->gfdb_db_record.gfid);\
        gf_uuid_clear (ctr_local->gfdb_db_record.pargfid);\
        memset(ctr_local->gfdb_db_record.file_name, 0, PATH_MAX);\
        memset(ctr_local->gfdb_db_record.old_file_name, 0, PATH_MAX);\
        ctr_local->gfdb_db_record.gfdb_fop_type = GFDB_FOP_INVALID_OP;\
        ctr_local->ia_inode_type = IA_INVAL;\
} while (0)


static gf_ctr_local_t *
init_ctr_local_t (xlator_t *this) {

        gf_ctr_local_t  *ctr_local     = NULL;

        GF_ASSERT(this);

        ctr_local = mem_get0 (this->local_pool);
        if (!ctr_local) {
                gf_msg (GFDB_DATA_STORE, GF_LOG_ERROR, 0,
                        CTR_MSG_CREATE_CTR_LOCAL_ERROR_WIND,
                        "Error while creating ctr local");
                goto out;
        }

        CLEAR_CTR_DB_RECORD (ctr_local);
out:
        return ctr_local;
}

static void
free_ctr_local (gf_ctr_local_t *ctr_local)
{
        if (ctr_local)
                mem_put (ctr_local);
}



/******************************************************************************
 *
 *
 *                      Context Carrier Structures
 *
 *
 * ****************************************************************************/

/*
 * Context Carrier structures are used to carry relavent information about
 * inodes and links from the fops calls to the ctr_insert_wind.
 * These structure just have pointers to the original data and donot
 * do a deep copy of any data. This info is deep copied to
 * ctr_local->gfdb_db_record and passed to insert_record() api of libgfdb. This
 * info remains persistent for the unwind in  ctr_local->gfdb_db_record
 * and once used will be destroyed.
 *
 * gf_ctr_link_context_t : Context structure for hard links
 * gf_ctr_inode_context_t : Context structure for inodes
 *
 * */

 /*Context Carrier Structure for hard links*/
typedef struct gf_ctr_link_context {
        uuid_t                  *pargfid;
        const char              *basename;
        /*basepath is redundent. Will go off*/
        const char              *basepath;
} gf_ctr_link_context_t;

 /*Context Carrier Structure for inodes*/
typedef struct gf_ctr_inode_context {
        ia_type_t               ia_type;
        uuid_t                  *gfid;
        gf_ctr_link_context_t   *new_link_cx;
        gf_ctr_link_context_t   *old_link_cx;
        gfdb_fop_type_t         fop_type;
        gfdb_fop_path_t         fop_path;
        gf_boolean_t            is_internal_fop;
} gf_ctr_inode_context_t;


/*******************Util Macros for Context Carrier Structures*****************/

/*Checks if ctr_link_cx is sane!*/
#define IS_CTR_LINK_CX_SANE(ctr_link_cx)\
do {\
        if (ctr_link_cx) {\
                if (ctr_link_cx->pargfid)\
                        GF_ASSERT (*(ctr_link_cx->pargfid));\
                GF_ASSERT (ctr_link_cx->basename);\
                GF_ASSERT (ctr_link_cx->basepath);\
        };\
} while (0)

/*Clear and fill the ctr_link_context with values*/
#define FILL_CTR_LINK_CX(ctr_link_cx, _pargfid, _basename, _basepath)\
do {\
        GF_ASSERT (ctr_link_cx);\
        GF_ASSERT (_pargfid);\
        GF_ASSERT (_basename);\
        GF_ASSERT (_basepath);\
        memset (ctr_link_cx, 0, sizeof (*ctr_link_cx));\
        ctr_link_cx->pargfid = &_pargfid;\
        ctr_link_cx->basename = _basename;\
        ctr_link_cx->basepath = _basepath;\
} while (0)

#define NEW_LINK_CX(ctr_inode_cx)\
        ctr_inode_cx->new_link_cx\

#define OLD_LINK_CX(ctr_inode_cx)\
        ctr_inode_cx->old_link_cx\

/*Checks if ctr_inode_cx is sane!*/
#define IS_CTR_INODE_CX_SANE(ctr_inode_cx)\
do {\
        GF_ASSERT (ctr_inode_cx);\
        GF_ASSERT (ctr_inode_cx->gfid);\
        GF_ASSERT (*(ctr_inode_cx->gfid));\
        GF_ASSERT (ctr_inode_cx->fop_type != GFDB_FOP_INVALID_OP);\
        GF_ASSERT (ctr_inode_cx->fop_path != GFDB_FOP_INVALID);\
        IS_CTR_LINK_CX_SANE (NEW_LINK_CX(ctr_inode_cx));\
        IS_CTR_LINK_CX_SANE (OLD_LINK_CX(ctr_inode_cx));\
} while (0)

/*Clear and fill the ctr_inode_context with values*/
#define FILL_CTR_INODE_CONTEXT(ctr_inode_cx,\
                                _ia_type,\
                                _gfid,\
                                _new_link_cx,\
                                _old_link_cx,\
                                _fop_type,\
                                _fop_path)\
do {\
        GF_ASSERT(ctr_inode_cx);\
        GF_ASSERT(_gfid);\
        GF_ASSERT(_fop_type != GFDB_FOP_INVALID_OP);\
        GF_ASSERT(_fop_path != GFDB_FOP_INVALID);\
        memset(ctr_inode_cx, 0, sizeof(*ctr_inode_cx));\
        ctr_inode_cx->ia_type = _ia_type;\
        ctr_inode_cx->gfid = &_gfid;\
        IS_CTR_LINK_CX_SANE(NEW_LINK_CX(ctr_inode_cx));\
        if (_new_link_cx)\
                NEW_LINK_CX(ctr_inode_cx) = _new_link_cx;\
        IS_CTR_LINK_CX_SANE(OLD_LINK_CX(ctr_inode_cx));\
        if (_old_link_cx)\
                OLD_LINK_CX(ctr_inode_cx) = _old_link_cx;\
        ctr_inode_cx->fop_type = _fop_type;\
        ctr_inode_cx->fop_path = _fop_path;\
} while (0)

/******************************************************************************
 *
 *                      Util functions or macros used by
 *                      insert wind and insert unwind
 *
 * ****************************************************************************/

/*
 * If a rebalancer fop
 * */
#define REBALANCE_FOP(frame)\
        (frame->root->pid == GF_CLIENT_PID_DEFRAG)

/*
 * If its a tiering rebalancer fop
 * */
#define TIER_REBALANCE_FOP(frame)\
        (frame->root->pid == GF_CLIENT_PID_TIER_DEFRAG)

/*
 * If its a AFR SELF HEAL
 * */
 #define AFR_SELF_HEAL_FOP(frame)\
        (frame->root->pid == GF_CLIENT_PID_AFR_SELF_HEALD)

/*
 * if a rebalancer fop goto
 * */
#define CTR_IF_REBALANCE_FOP_THEN_GOTO(frame, label)\
do {\
        if (REBALANCE_FOP (frame))\
                goto label;\
} while (0)

/*
 * Internal fop
 *
 * */
#define CTR_IS_INTERNAL_FOP(frame, dict)\
        (AFR_SELF_HEAL_FOP (frame) \
        || REBALANCE_FOP (frame) \
        || TIER_REBALANCE_FOP (frame) \
        || (dict && \
        dict_get (dict, GLUSTERFS_INTERNAL_FOP_KEY)))

/**
 * ignore internal fops for all clients except AFR self-heal daemon
 */
#define CTR_IF_INTERNAL_FOP_THEN_GOTO(frame, dict, label)\
do {\
        GF_ASSERT(frame);\
        GF_ASSERT(frame->root);\
        if (CTR_IS_INTERNAL_FOP(frame, dict)) \
                        goto label; \
} while (0)


/*
 * IS CTR Xlator is disabled then goto to label
 * */
 #define CTR_IS_DISABLED_THEN_GOTO(this, label)\
 do {\
        gf_ctr_private_t *_priv = NULL;\
        GF_ASSERT (this);\
        GF_ASSERT (this->private);\
        _priv = this->private;\
        if (!_priv->enabled)\
                goto label;\
 } while (0)

int
fill_db_record_for_unwind (xlator_t              *this,
                          gf_ctr_local_t        *ctr_local,
                          gfdb_fop_type_t       fop_type,
                          gfdb_fop_path_t       fop_path);

int
fill_db_record_for_wind (xlator_t                *this,
                        gf_ctr_local_t          *ctr_local,
                        gf_ctr_inode_context_t  *ctr_inode_cx);

/*******************************************************************************
 *                              CTR INSERT WIND
 * *****************************************************************************
 * Function used to insert/update record into the database during a wind fop
 * This function creates ctr_local structure into the frame of the fop
 * call.
 * ****************************************************************************/
static inline int
ctr_insert_wind (call_frame_t                    *frame,
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

        GF_ASSERT(_priv->_db_conn);

        /*If record_wind option of CTR is on record wind for
         * regular files only*/
        if (_priv->ctr_record_wind && ctr_inode_cx->ia_type != IA_IFDIR) {
                frame->local = init_ctr_local_t (this);
                if (!frame->local) {
                        gf_msg (this->name, GF_LOG_ERROR, 0,
                                CTR_MSG_CREATE_CTR_LOCAL_ERROR_WIND,
                                "WIND: Error while creating ctr local");
                        goto out;
                };
                ctr_local = frame->local;
                ctr_local->client_pid = frame->root->pid;
                ctr_local->is_internal_fop = ctr_inode_cx->is_internal_fop;

                /* Decide whether to record counters or not */
                CTR_DB_REC(ctr_local).do_record_counters =
                                                _priv->ctr_record_counter &&
                                                !(ctr_local->is_internal_fop);

                /* Decide whether to record times or not
                 * For non internal FOPS record times as usual*/
                if (!ctr_local->is_internal_fop) {
                        CTR_DB_REC(ctr_local).do_record_times =
                                                (_priv->ctr_record_wind
                                                || _priv->ctr_record_unwind);
                }
                /* when its a internal FOPS*/
                else {
                        /* Record times only for create
                         * i.e when the inode is created */
                        CTR_DB_REC(ctr_local).do_record_times =
                                (isdentrycreatefop(ctr_inode_cx->fop_type)) ?
                                        _gf_true : _gf_false;
                }

                /*Fill the db record for insertion*/
                ret = fill_db_record_for_wind (this, ctr_local, ctr_inode_cx);
                if (ret) {
                        gf_msg (this->name, GF_LOG_ERROR, 0,
                                CTR_MSG_FILL_CTR_LOCAL_ERROR_WIND,
                                "WIND: Error filling  ctr local");
                        goto out;
                }

                /*Insert the db record*/
                ret = insert_record (_priv->_db_conn,
                                &ctr_local->gfdb_db_record);
                if (ret) {
                        gf_msg (this->name, GF_LOG_ERROR, 0,
                                CTR_MSG_INSERT_RECORD_WIND_FAILED,
                                "WIND: Inserting of record failed!");
                        goto out;
                }
        }
        ret = 0;
out:

        if (ret) {
                free_ctr_local (ctr_local);
                frame->local = NULL;
        }

        return ret;
}




/*******************************************************************************
 *                             CTR INSERT UNWIND
 * *****************************************************************************
 * Function used to insert/update record into the database during a unwind fop
 * This function destroys ctr_local structure into the frame of the fop
 * call at the end.
 * ****************************************************************************/
static inline int
ctr_insert_unwind (call_frame_t          *frame,
                  xlator_t              *this,
                  gfdb_fop_type_t       fop_type,
                  gfdb_fop_path_t       fop_path)
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

        if (ctr_local
            && (_priv->ctr_record_unwind || isdentryfop(fop_type))
            && (ctr_local->ia_inode_type != IA_IFDIR)) {

                CTR_DB_REC(ctr_local).do_record_uwind_time =
                                                _priv->ctr_record_unwind;

                ret = fill_db_record_for_unwind(this, ctr_local, fop_type,
                                               fop_path);
                if (ret == -1) {
                        gf_msg(this->name, GF_LOG_ERROR, 0,
                               CTR_MSG_FILL_CTR_LOCAL_ERROR_UNWIND,
                               "UNWIND: Error filling ctr local");
                        goto out;
                }

                ret = insert_record(_priv->_db_conn,
                                        &ctr_local->gfdb_db_record);
                if (ret == -1) {
                        gf_msg(this->name, GF_LOG_ERROR, 0,
                               CTR_MSG_FILL_CTR_LOCAL_ERROR_UNWIND,
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

/******************************* Hard link function ***************************/

static inline gf_boolean_t
__is_inode_expired (ctr_xlator_ctx_t *ctr_xlator_ctx,
                    gf_ctr_private_t *_priv,
                    gfdb_time_t *current_time)
{
        gf_boolean_t    ret       = _gf_false;
        uint64_t        time_diff = 0;

        GF_ASSERT (ctr_xlator_ctx);
        GF_ASSERT (_priv);
        GF_ASSERT (current_time);

        time_diff = current_time->tv_sec -
                        ctr_xlator_ctx->inode_heal_period;

        ret = (time_diff >= _priv->ctr_inode_heal_expire_period) ?
                        _gf_true : _gf_false;
        return ret;
}

static inline gf_boolean_t
__is_hardlink_expired (ctr_hard_link_t *ctr_hard_link,
                       gf_ctr_private_t *_priv,
                       gfdb_time_t *current_time)
{
        gf_boolean_t    ret       = _gf_false;
        uint64_t        time_diff = 0;

        GF_ASSERT (ctr_hard_link);
        GF_ASSERT (_priv);
        GF_ASSERT (current_time);

        time_diff = current_time->tv_sec -
                        ctr_hard_link->hardlink_heal_period;

        ret = ret || (time_diff >= _priv->ctr_hardlink_heal_expire_period) ?
                        _gf_true : _gf_false;

        return ret;
}


/* Return values of heal*/
typedef enum ctr_heal_ret_val {
        CTR_CTX_ERROR = -1,
        /* No healing required */
        CTR_TRY_NO_HEAL = 0,
        /* Try healing hard link */
        CTR_TRY_HARDLINK_HEAL = 1,
        /* Try healing inode */
        CTR_TRY_INODE_HEAL = 2,
} ctr_heal_ret_val_t;



/**
 * @brief Function to add hard link to the inode context variable.
 *        The inode context maintainences a in-memory list. This is used
 *        smart healing of database.
 * @param frame of the FOP
 * @param this is the Xlator instant
 * @param inode
 * @return Return ctr_heal_ret_val_t
 */

static inline ctr_heal_ret_val_t
add_hard_link_ctx (call_frame_t *frame,
                   xlator_t     *this,
                   inode_t      *inode)
{
        ctr_heal_ret_val_t ret_val = CTR_TRY_NO_HEAL;
        int ret = -1;
        gf_ctr_local_t   *ctr_local       = NULL;
        ctr_xlator_ctx_t *ctr_xlator_ctx  = NULL;
        ctr_hard_link_t  *ctr_hard_link   = NULL;
        gf_ctr_private_t *_priv           = NULL;
        gfdb_time_t       current_time    = {0};


        GF_ASSERT (frame);
        GF_ASSERT (this);
        GF_ASSERT (inode);
        GF_ASSERT (this->private);

        _priv = this->private;

        ctr_local = frame->local;
        if (!ctr_local) {
                goto out;
        }

        ctr_xlator_ctx  = init_ctr_xlator_ctx (this, inode);
        if (!ctr_xlator_ctx) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        CTR_MSG_ACCESS_CTR_INODE_CONTEXT_FAILED,
                        "Failed accessing ctr inode context");
                goto out;
        }

        LOCK (&ctr_xlator_ctx->lock);

        /* Check if the hard link already exists
         * in the ctr inode context*/
        ctr_hard_link = ctr_search_hard_link_ctx (this,
                                ctr_xlator_ctx,
                                CTR_DB_REC(ctr_local).pargfid,
                                CTR_DB_REC(ctr_local).file_name);
        /* if there then ignore */
        if (ctr_hard_link) {

                ret = gettimeofday (&current_time, NULL);
                if (ret == -1) {
                        gf_log (this->name, GF_LOG_ERROR,
                                "Failed to get current time");
                        ret_val = CTR_CTX_ERROR;
                        goto unlock;
                }

                if (__is_hardlink_expired (ctr_hard_link,
                                           _priv, &current_time)) {
                        ctr_hard_link->hardlink_heal_period =
                                        current_time.tv_sec;
                        ret_val = ret_val | CTR_TRY_HARDLINK_HEAL;
                }

                if (__is_inode_expired (ctr_xlator_ctx,
                                           _priv, &current_time)) {
                        ctr_xlator_ctx->inode_heal_period =
                                                current_time.tv_sec;
                        ret_val = ret_val | CTR_TRY_INODE_HEAL;
                }

                goto unlock;
        }

        /* Add the hard link to the list*/
        ret = ctr_add_hard_link (this, ctr_xlator_ctx,
                        CTR_DB_REC(ctr_local).pargfid,
                        CTR_DB_REC(ctr_local).file_name);
        if (ret) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        CTR_MSG_ADD_HARDLINK_TO_CTR_INODE_CONTEXT_FAILED,
                        "Failed to add hardlink to the ctr inode context");
                ret_val = CTR_CTX_ERROR;
                goto unlock;
        }

        ret_val = CTR_TRY_NO_HEAL;
unlock:
        UNLOCK (&ctr_xlator_ctx->lock);
out:
        return ret_val;
}

static inline int
delete_hard_link_ctx (call_frame_t *frame,
                      xlator_t     *this,
                      inode_t      *inode)
{
        int ret = -1;
        ctr_xlator_ctx_t *ctr_xlator_ctx  = NULL;
        gf_ctr_local_t   *ctr_local       = NULL;

        GF_ASSERT (frame);
        GF_ASSERT (this);
        GF_ASSERT (inode);

        ctr_local = frame->local;
        if (!ctr_local) {
                goto out;
        }

        ctr_xlator_ctx = get_ctr_xlator_ctx (this, inode);
        if (!ctr_xlator_ctx) {
                /* Since there is no ctr inode context so nothing more to do */
                ret = 0;
                goto out;
        }

        ret = ctr_delete_hard_link (this, ctr_xlator_ctx,
                        CTR_DB_REC(ctr_local).pargfid,
                        CTR_DB_REC(ctr_local).file_name);
        if (ret) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        CTR_MSG_DELETE_HARDLINK_FAILED,
                        "Failed to delete hard link");
                goto out;
        }

        ret = 0;

out:
        return ret;
}

static inline int
update_hard_link_ctx (call_frame_t *frame,
                      xlator_t     *this,
                      inode_t      *inode)
{
        int ret = -1;
        ctr_xlator_ctx_t *ctr_xlator_ctx  = NULL;
        gf_ctr_local_t   *ctr_local       = NULL;

        GF_ASSERT (frame);
        GF_ASSERT (this);
        GF_ASSERT (inode);

        ctr_local = frame->local;
        if (!ctr_local) {
                goto out;
        }

        ctr_xlator_ctx  = init_ctr_xlator_ctx (this, inode);
        if (!ctr_xlator_ctx) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        CTR_MSG_ACCESS_CTR_INODE_CONTEXT_FAILED,
                        "Failed accessing ctr inode context");
                goto out;
        }

        ret = ctr_update_hard_link (this, ctr_xlator_ctx,
                        CTR_DB_REC(ctr_local).pargfid,
                        CTR_DB_REC(ctr_local).file_name,
                        CTR_DB_REC(ctr_local).old_pargfid,
                        CTR_DB_REC(ctr_local).old_file_name);
        if (ret) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        CTR_MSG_DELETE_HARDLINK_FAILED,
                        "Failed to delete hard link");
                goto out;
        }

        ret = 0;

out:
        return ret;
}


/******************************************************************************
 *
 *                      CTR xlator init related functions
 *
 *
 * ****************************************************************************/
int
extract_db_params (xlator_t              *this,
                  dict_t                *params_dict,
                  gfdb_db_type_t        db_type);

int
extract_ctr_options (xlator_t            *this,
                    gf_ctr_private_t    *_priv);

#endif
