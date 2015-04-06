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


#ifndef _CONFIG_H
#define _CONFIG_H
#include "config.h"
#endif

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

/*CTR Xlator Private structure*/
typedef struct gf_ctr_private {
        gf_boolean_t                    enabled;
        char                            *ctr_db_path;
        gf_boolean_t                    ctr_hot_brick;
        gf_boolean_t                    ctr_record_wind;
        gf_boolean_t                    ctr_record_unwind;
        gf_boolean_t                    ctr_record_counter;
        gfdb_db_type_t                  gfdb_db_type;
        gfdb_sync_type_t                gfdb_sync_type;
        gfdb_conn_node_t                *_db_conn;
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
 * NOTE: This piece is broken with the addition of frequency counters.
 * Any rebalancer or tiering will cause the files to get the files heated.
 * We would require seperate identifiers for tiering FOPS.
 * The QE have noted this issue and will raise a bug as this patch gets merged.
 * We will fix this as a bug fix.
 *
 * */
typedef struct gf_ctr_local {
        gfdb_db_record_t        gfdb_db_record;
        ia_type_t               ia_inode_type;
        gf_boolean_t            is_internal_fop;
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
                gf_log (GFDB_DATA_STORE, GF_LOG_ERROR,
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
#define CTR_IS_INTERNAL_FOP(frame, priv)\
        (REBALANCE_FOP(frame) && (!priv->ctr_hot_brick))

/**
 * ignore internal fops for all clients except AFR self-heal daemon
 */
#define CTR_IF_INTERNAL_FOP_THEN_GOTO(frame, dict, label)\
do {\
                if ((frame->root->pid != GF_CLIENT_PID_AFR_SELF_HEALD)  \
                    && dict                                             \
                    && dict_get (dict, GLUSTERFS_INTERNAL_FOP_KEY))     \
                        goto label;                                     \
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
fill_db_record_for_unwind(gf_ctr_local_t        *ctr_local,
                          gfdb_fop_type_t       fop_type,
                          gfdb_fop_path_t       fop_path);

int
fill_db_record_for_wind(gf_ctr_local_t          *ctr_local,
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
                        gf_log (this->name, GF_LOG_ERROR,
                                "WIND: Error while creating ctr local");
                        goto out;
                };
                ctr_local = frame->local;

                /*Broken please refer gf_ctr_local_t documentation*/
                ctr_local->is_internal_fop = CTR_IS_INTERNAL_FOP(frame, _priv);

                /*Broken please refer gf_ctr_local_t documentation*/
                CTR_DB_REC(ctr_local).do_record_counters =
                                                _priv->ctr_record_counter;

                /*Fill the db record for insertion*/
                ret = fill_db_record_for_wind (ctr_local, ctr_inode_cx);
                if (ret) {
                        gf_log (this->name, GF_LOG_ERROR,
                                "WIND: Error filling  ctr local");
                        goto out;
                }
                /*Insert the db record*/
                ret = insert_record (_priv->_db_conn,
                                        &ctr_local->gfdb_db_record);
                if (ret) {
                        gf_log (this->name, GF_LOG_ERROR,
                                "WIND: Inserting of record failed!");
                        goto out;
                }
        }
        ret = 0;
out:

        if (ret) {
                free_ctr_local (ctr_local);
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

                ret = fill_db_record_for_unwind(ctr_local, fop_type, fop_path);
                if (ret == -1) {
                        gf_log(this->name, GF_LOG_ERROR, "UNWIND: Error"
                                "filling ctr local");
                        goto out;
                }

                ret = insert_record(_priv->_db_conn,
                                        &ctr_local->gfdb_db_record);
                if (ret == -1) {
                        gf_log(this->name, GF_LOG_ERROR, "UNWIND: Error"
                                "filling ctr local");
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
