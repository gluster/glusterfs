/*
   Copyright (c) 2015 Red Hat, Inc. <http://www.redhat.com>
   This file is part of GlusterFS.

   This file is licensed to you under your choice of the GNU Lesser
   General Public License, version 3 or any later version (LGPLv3 or
   later), or the GNU General Public License, version 2 (GPLv2), in all
   cases as published by the Free Software Foundation.
*/

#include "gfdb_sqlite3.h"
#include "ctr-helper.h"
#include "ctr-messages.h"

/*******************************************************************************
 *
 *                      Fill unwind into db record
 *
 ******************************************************************************/
int
fill_db_record_for_unwind(xlator_t              *this,
                          gf_ctr_local_t        *ctr_local,
                          gfdb_fop_type_t       fop_type,
                          gfdb_fop_path_t       fop_path)
{
        int ret                         = -1;
        gfdb_time_t *ctr_uwtime         = NULL;
        gf_ctr_private_t *_priv                 = NULL;

        GF_ASSERT (this);
        _priv = this->private;
        GF_ASSERT (_priv);

        GF_ASSERT(ctr_local);

        /*If not unwind path error*/
        if (!isunwindpath(fop_path)) {
                gf_msg (this->name, GF_LOG_ERROR, 0, CTR_MSG_WRONG_FOP_PATH,
                        "Wrong fop_path. Should be unwind");
                goto out;
        }

        ctr_uwtime = &CTR_DB_REC(ctr_local).gfdb_unwind_change_time;
        CTR_DB_REC(ctr_local).gfdb_fop_path = fop_path;
        CTR_DB_REC(ctr_local).gfdb_fop_type = fop_type;

        ret = gettimeofday (ctr_uwtime, NULL);
        if (ret == -1) {
                        gf_msg (this->name, GF_LOG_ERROR, errno,
                                CTR_MSG_FILL_UNWIND_TIME_REC_ERROR, "Error "
                                "filling unwind time record %s",
                                strerror(errno));
                        goto out;
                }

        /* Special case i.e if its a tier rebalance
         * + cold tier brick
         * + its a create/mknod FOP
         * we record unwind time as zero */
        if (ctr_local->client_pid == GF_CLIENT_PID_TIER_DEFRAG
                && (!_priv->ctr_hot_brick)
                && isdentrycreatefop(fop_type)) {
                memset(ctr_uwtime, 0, sizeof(*ctr_uwtime));
        }
        ret = 0;
out:
        return ret;
}


/*******************************************************************************
 *
 *                      Fill wind into db record
 *
 ******************************************************************************/
int
fill_db_record_for_wind (xlator_t               *this,
                        gf_ctr_local_t          *ctr_local,
                        gf_ctr_inode_context_t  *ctr_inode_cx)
{
        int ret                                 = -1;
        gfdb_time_t *ctr_wtime                  = NULL;
        gf_ctr_private_t *_priv                 = NULL;

        GF_ASSERT (this);
        _priv = this->private;
        GF_ASSERT (_priv);
        GF_ASSERT (ctr_local);
        IS_CTR_INODE_CX_SANE (ctr_inode_cx);

        /*if not wind path error!*/
        if (!iswindpath(ctr_inode_cx->fop_path)) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        CTR_MSG_WRONG_FOP_PATH,
                        "Wrong fop_path. Should be wind");
                goto out;
        }

        ctr_wtime = &CTR_DB_REC(ctr_local).gfdb_wind_change_time;
        CTR_DB_REC(ctr_local).gfdb_fop_path = ctr_inode_cx->fop_path;
        CTR_DB_REC(ctr_local).gfdb_fop_type = ctr_inode_cx->fop_type;
        CTR_DB_REC(ctr_local).link_consistency = _priv->ctr_link_consistency;

        ret = gettimeofday (ctr_wtime, NULL);
        if (ret) {
                        gf_msg (this->name, GF_LOG_ERROR, errno,
                                CTR_MSG_FILL_UNWIND_TIME_REC_ERROR,
                                "Error filling wind time record %s",
                                strerror(errno));
                        goto out;
        }

        /* Special case i.e if its a tier rebalance
         * + cold tier brick
         * + its a create/mknod FOP
         * we record wind time as zero */
        if (ctr_local->client_pid == GF_CLIENT_PID_TIER_DEFRAG
                && (!_priv->ctr_hot_brick)
                && isdentrycreatefop(ctr_inode_cx->fop_type)) {
                memset(ctr_wtime, 0, sizeof(*ctr_wtime));
        }

        /* Copy gfid into db record */
        gf_uuid_copy (CTR_DB_REC(ctr_local).gfid, *(ctr_inode_cx->gfid));

        /* Copy older gfid if any */
        if (ctr_inode_cx->old_gfid &&
                (!gf_uuid_is_null (*(ctr_inode_cx->old_gfid)))) {
                gf_uuid_copy (CTR_DB_REC(ctr_local).old_gfid,
                                *(ctr_inode_cx->old_gfid));
        }

        /*Hard Links*/
        if (isdentryfop(ctr_inode_cx->fop_type)) {
                /*new link fop*/
                if (NEW_LINK_CX(ctr_inode_cx)) {
                        gf_uuid_copy (CTR_DB_REC(ctr_local).pargfid,
                                *((NEW_LINK_CX(ctr_inode_cx))->pargfid));
                        strcpy (CTR_DB_REC(ctr_local).file_name,
                                NEW_LINK_CX(ctr_inode_cx)->basename);
                }
                /*rename fop*/
                if (OLD_LINK_CX(ctr_inode_cx)) {
                        gf_uuid_copy (CTR_DB_REC(ctr_local).old_pargfid,
                                *((OLD_LINK_CX(ctr_inode_cx))->pargfid));
                        strcpy (CTR_DB_REC(ctr_local).old_file_name,
                                OLD_LINK_CX(ctr_inode_cx)->basename);
                }
        }

        ret = 0;
out:
        /*On error roll back and clean the record*/
        if (ret == -1) {
                CLEAR_CTR_DB_RECORD (ctr_local);
        }
        return ret;
}


/******************************************************************************
 *
 *                      CTR xlator init related functions
 *
 *
 * ****************************************************************************/
static int
extract_sql_params(xlator_t *this, dict_t *params_dict)
{
        int ret                         = -1;
        char *db_path                   = NULL;
        char *db_name                   = NULL;
        char *db_full_path              = NULL;

        GF_ASSERT (this);
        GF_ASSERT (params_dict);

        /*Extract the path of the db*/
        db_path = NULL;
        GET_DB_PARAM_FROM_DICT_DEFAULT(this->name, this->options, "db-path",
                                        db_path, "/var/run/gluster/");

        /*Extract the name of the db*/
        db_name = NULL;
        GET_DB_PARAM_FROM_DICT_DEFAULT(this->name, this->options, "db-name",
                                        db_name, "gf_ctr_db.db");

        /*Construct full path of the db*/
        ret = gf_asprintf(&db_full_path, "%s/%s", db_path, db_name);
        if (ret < 0) {
                gf_msg (GFDB_DATA_STORE, GF_LOG_ERROR, 0,
                        CTR_MSG_CONSTRUCT_DB_PATH_FAILED,
                        "Construction of full db path failed!");
                goto out;
        }

        /*Setting the SQL DB Path*/
        SET_DB_PARAM_TO_DICT(this->name, params_dict, GFDB_SQL_PARAM_DBPATH,
                                db_full_path, ret, out);

        /*Extact rest of the sql params*/
        ret = gfdb_set_sql_params(this->name, this->options, params_dict);
        if (ret) {
                gf_msg (GFDB_DATA_STORE, GF_LOG_ERROR, 0,
                        CTR_MSG_SET_VALUE_TO_SQL_PARAM_FAILED,
                        "Failed setting values to sql param dict!");
        }

        ret = 0;

out:
        if (ret)
                GF_FREE (db_full_path);
        return ret;
}



int extract_db_params(xlator_t *this, dict_t *params_dict,
                                                gfdb_db_type_t db_type) {

        int ret = -1;

        GF_ASSERT (this);
        GF_ASSERT (params_dict);

        switch (db_type) {
        case GFDB_SQLITE3:
                ret = extract_sql_params(this, params_dict);
                if (ret)
                        goto out;
                break;
        case GFDB_ROCKS_DB:
        case GFDB_HYPERDEX:
        case GFDB_HASH_FILE_STORE:
        case GFDB_INVALID_DB:
        case GFDB_DB_END:
                ret = -1;
                break;
        }
        ret = 0;
out:
        return ret;
}

int extract_ctr_options (xlator_t *this, gf_ctr_private_t *_priv) {
        int ret         = -1;
        char *_val_str  = NULL;

        GF_ASSERT (this);
        GF_ASSERT (_priv);

        /*Checking if the CTR Translator is enabled. By default its disabled*/
        _priv->enabled = _gf_false;
        GF_OPTION_INIT ("ctr-enabled", _priv->enabled, bool, out);
        if (!_priv->enabled) {
                gf_msg (GFDB_DATA_STORE, GF_LOG_INFO, 0,
                        CTR_MSG_XLATOR_DISABLED,
                        "CTR Xlator is disabled.");
                ret = 0;
                goto out;
        }

        /*Extract db type*/
        GF_OPTION_INIT ("db-type", _val_str, str, out);
        _priv->gfdb_db_type = gf_string2gfdbdbtype(_val_str);

        /*Extract flag for record on wind*/
        GF_OPTION_INIT ("record-entry", _priv->ctr_record_wind, bool, out);

        /*Extract flag for record on unwind*/
        GF_OPTION_INIT ("record-exit", _priv->ctr_record_unwind, bool, out);

        /*Extract flag for record on counters*/
        GF_OPTION_INIT ("record-counters", _priv->ctr_record_counter, bool,
                        out);

        /* Extract flag for record metadata heat */
        GF_OPTION_INIT ("ctr-record-metadata-heat",
                        _priv->ctr_record_metadata_heat, bool,
                        out);

        /*Extract flag for link consistency*/
        GF_OPTION_INIT ("ctr_link_consistency", _priv->ctr_link_consistency,
                        bool, out);

        /*Extract ctr_lookupheal_inode_timeout */
        GF_OPTION_INIT ("ctr_lookupheal_inode_timeout",
                        _priv->ctr_lookupheal_inode_timeout,
                        uint64, out);

        /*Extract ctr_lookupheal_link_timeout*/
        GF_OPTION_INIT ("ctr_lookupheal_link_timeout",
                        _priv->ctr_lookupheal_link_timeout,
                        uint64, out);

        /*Extract flag for hot tier brick*/
        GF_OPTION_INIT ("hot-brick", _priv->ctr_hot_brick, bool, out);

        /*Extract flag for sync mode*/
        GF_OPTION_INIT ("db-sync", _val_str, str, out);
        _priv->gfdb_sync_type = gf_string2gfdbdbsync(_val_str);

        ret = 0;

out:
        return ret;
}
