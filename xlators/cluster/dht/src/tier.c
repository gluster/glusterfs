/*
  Copyright (c) 2015 Red Hat, Inc. <http://www.redhat.com>
  This file is part of GlusterFS.

  This file is licensed to you under your choice of the GNU Lesser
  General Public License, version 3 or any later version (LGPLv3 or
  later), or the GNU General Public License, version 2 (GPLv2), in all
  cases as published by the Free Software Foundation.
*/

#include <dlfcn.h>

#include "dht-common.h"
#include "tier.h"
#include "tier-common.h"
#include "syscall.h"
#include "events.h"
#include "tier-ctr-interface.h"

/*Hard coded DB info*/
static gfdb_db_type_t dht_tier_db_type = GFDB_SQLITE3;
/*Hard coded DB info*/

/*Mutex for updating the data movement stats*/
static pthread_mutex_t dm_stat_mutex = PTHREAD_MUTEX_INITIALIZER;

/* Stores the path location of promotion query files */
static char *promotion_qfile;
/* Stores the path location of demotion query files */
static char *demotion_qfile;

static void *libhandle;
static gfdb_methods_t gfdb_methods;

#define DB_QUERY_RECORD_SIZE 4096

/*
 * Closes all the fds and frees the qfile_array
 * */
static void
qfile_array_free (tier_qfile_array_t *qfile_array)
{
        ssize_t i = 0;

        if (qfile_array) {
                if (qfile_array->fd_array) {
                        for (i = 0; i < qfile_array->array_size; i++) {
                                if (qfile_array->fd_array[i] != -1) {
                                        sys_close (qfile_array->fd_array[i]);
                                }
                        }
                }
                GF_FREE (qfile_array->fd_array);
        }
        GF_FREE (qfile_array);
}


/* Create a new query file list with given size */
static tier_qfile_array_t *
qfile_array_new (ssize_t array_size)
{
        int ret                         = -1;
        tier_qfile_array_t *qfile_array   = NULL;
        ssize_t i                       = 0;

        GF_VALIDATE_OR_GOTO ("tier", (array_size > 0), out);

        qfile_array = GF_CALLOC (1, sizeof (tier_qfile_array_t),
                                gf_tier_mt_qfile_array_t);
        if (!qfile_array) {
                gf_msg ("tier", GF_LOG_ERROR, 0, DHT_MSG_LOG_TIER_ERROR,
                        "Failed to allocate memory for tier_qfile_array_t");
                goto out;
        }

        qfile_array->fd_array = GF_CALLOC (array_size, sizeof (int),
                                        gf_dht_mt_int32_t);
        if (!qfile_array->fd_array) {
                gf_msg ("tier", GF_LOG_ERROR, 0, DHT_MSG_LOG_TIER_ERROR,
                        "Failed to allocate memory for "
                        "tier_qfile_array_t->fd_array");
                goto out;
        }

        /* Init all the fds to -1 */
        for (i = 0; i < array_size; i++) {
                qfile_array->fd_array[i] = -1;
        }

        qfile_array->array_size = array_size;
        qfile_array->next_index = 0;

        /* Set exhausted count to list size as the list is empty */
        qfile_array->exhausted_count = qfile_array->array_size;

        ret = 0;
out:
        if (ret) {
                qfile_array_free (qfile_array);
                qfile_array = NULL;
        }
        return qfile_array;
}


/* Checks if the query file list is empty or totally exhausted. */
static gf_boolean_t
is_qfile_array_empty (tier_qfile_array_t *qfile_array)
{
        return (qfile_array->exhausted_count == qfile_array->array_size) ?
                _gf_true : _gf_false;
}


/* Shifts the next_fd pointer to the next available fd in the list */
static void
shift_next_index (tier_qfile_array_t *qfile_array)
{
        int qfile_fd = 0;
        int spin_count = 0;

        if (is_qfile_array_empty (qfile_array)) {
                return;
        }

        do {
                /* change next_index in a rotional manner */
                (qfile_array->next_index == (qfile_array->array_size - 1)) ?
                qfile_array->next_index = 0 : qfile_array->next_index++;

                qfile_fd = (qfile_array->fd_array[qfile_array->next_index]);

                spin_count++;

        } while ((qfile_fd == -1) && (spin_count < qfile_array->array_size));

}

/*
 * This is a non-thread safe function to read query records
 * from a list of query files in a Round-Robin manner.
 * As in when the query files get exhuasted they are closed.
 * Returns:
 * 0   if all the query records in all the query files of the list are
 *     exhausted.
 * > 0 if a query record is successfully read. Indicates the size of the query
 *     record read.
 * < 0  if there was failure
 * */
static int
read_query_record_list (tier_qfile_array_t *qfile_array,
                          gfdb_query_record_t **query_record)
{
        int ret = -1;
        int qfile_fd = 0;

        GF_VALIDATE_OR_GOTO ("tier", qfile_array, out);
        GF_VALIDATE_OR_GOTO ("tier", qfile_array->fd_array, out);

        do {
                if (is_qfile_array_empty (qfile_array)) {
                        ret = 0;
                        break;
                }

                qfile_fd = qfile_array->fd_array[qfile_array->next_index];
                ret = gfdb_methods.gfdb_read_query_record
                        (qfile_fd, query_record);
                if (ret <= 0) {
                        /*The qfile_fd has reached EOF or
                         * there was an error.
                         * 1. Close the exhausted fd
                         * 2. increment the exhausted count
                         * 3. shift next_qfile to next qfile
                         **/
                         sys_close (qfile_fd);
                         qfile_array->fd_array[qfile_array->next_index] = -1;
                         qfile_array->exhausted_count++;
                         /* shift next_qfile to next qfile */
                         shift_next_index (qfile_array);
                         continue;
                } else {
                        /* shift next_qfile to next qfile */
                        shift_next_index (qfile_array);
                        break;
                }
        } while (1);
out:
        return ret;
}


/* Check and update the watermark every WM_INTERVAL seconds */
#define WM_INTERVAL            5
#define WM_INTERVAL_EMERG      1

static int
tier_check_same_node (xlator_t *this, loc_t *loc, gf_defrag_info_t *defrag)
{
        int     ret            = -1;
        dict_t *dict           = NULL;
        char   *uuid_str       = NULL;
        uuid_t  node_uuid      = {0,};

        GF_VALIDATE_OR_GOTO ("tier", this, out);
        GF_VALIDATE_OR_GOTO (this->name, loc, out);
        GF_VALIDATE_OR_GOTO (this->name, defrag, out);

        if (syncop_getxattr (this, loc, &dict, GF_XATTR_NODE_UUID_KEY,
                             NULL, NULL)) {
                gf_msg (this->name, GF_LOG_ERROR, 0, DHT_MSG_LOG_TIER_ERROR,
                        "Unable to get NODE_UUID_KEY %s %s\n",
                        loc->name, loc->path);
                goto out;
        }

        if (dict_get_str (dict, GF_XATTR_NODE_UUID_KEY, &uuid_str) < 0) {
                gf_msg (this->name, GF_LOG_ERROR, 0, DHT_MSG_LOG_TIER_ERROR,
                        "Failed to get node-uuid for %s", loc->path);
                goto out;
        }

        if (gf_uuid_parse (uuid_str, node_uuid)) {
                gf_msg (this->name, GF_LOG_ERROR, 0, DHT_MSG_LOG_TIER_ERROR,
                        "uuid_parse failed for %s", loc->path);
                goto out;
        }

        if (gf_uuid_compare (node_uuid, defrag->node_uuid)) {
                gf_msg_debug (this->name, 0,
                              "%s does not belong to this node", loc->path);
                ret = 1;
                goto out;
        }

        ret = 0;
out:
        if (dict)
                dict_unref(dict);

        return ret;
}

int
tier_get_fs_stat (xlator_t *this, loc_t *root_loc)
{
        int                     ret = 0;
        gf_defrag_info_t       *defrag = NULL;
        dht_conf_t             *conf   = NULL;
        dict_t                 *xdata  = NULL;
        struct statvfs          statfs = {0, };
        gf_tier_conf_t         *tier_conf = NULL;


        conf = this->private;
        if (!conf) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        DHT_MSG_LOG_TIER_STATUS,
                        "conf is NULL");
                ret = -1;
                goto exit;
        }

        defrag = conf->defrag;
        if (!defrag) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        DHT_MSG_LOG_TIER_STATUS,
                        "defrag is NULL");
                ret = -1;
                goto exit;
        }

        tier_conf = &defrag->tier_conf;

        xdata = dict_new ();
        if (!xdata) {
                gf_msg (this->name, GF_LOG_ERROR, ENOMEM,
                        DHT_MSG_NO_MEMORY,
                        "failed to allocate dictionary");
                ret = -1;
                goto exit;
        }

        ret = dict_set_int8 (xdata, GF_INTERNAL_IGNORE_DEEM_STATFS, 1);
        if (ret) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        DHT_MSG_DICT_SET_FAILED,
                        "Failed to set "
                        GF_INTERNAL_IGNORE_DEEM_STATFS" in dict");
                ret = -1;
                goto exit;
        }

        /* Find how much free space is on the hot subvolume.
         * Then see if that value */
        /* is less than or greater than user defined watermarks.
         * Stash results in */
        /* the tier_conf data structure. */

        ret = syncop_statfs (conf->subvolumes[1], root_loc, &statfs,
                             xdata, NULL);
        if (ret) {
                gf_msg (this->name, GF_LOG_ERROR, -ret,
                        DHT_MSG_LOG_TIER_STATUS,
                        "Unable to obtain statfs.");
                goto exit;
        }

        pthread_mutex_lock (&dm_stat_mutex);

        tier_conf->block_size = statfs.f_bsize;
        tier_conf->blocks_total = statfs.f_blocks;
        tier_conf->blocks_used = statfs.f_blocks - statfs.f_bfree;

        tier_conf->percent_full = GF_PERCENTAGE(tier_conf->blocks_used,
                                                statfs.f_blocks);
        pthread_mutex_unlock (&dm_stat_mutex);

exit:
        if (xdata)
                dict_unref (xdata);
        return ret;
}

static void
tier_send_watermark_event (const char *volname,
                           tier_watermark_op_t old_wm,
                           tier_watermark_op_t new_wm)
{
        if (old_wm == TIER_WM_LOW || old_wm == TIER_WM_NONE) {
                if (new_wm == TIER_WM_MID) {
                        gf_event (EVENT_TIER_WATERMARK_RAISED_TO_MID,
                                  "vol=%s", volname);
                } else if (new_wm == TIER_WM_HI) {
                        gf_event (EVENT_TIER_WATERMARK_HI, "vol=%s", volname);
                }
        } else if (old_wm == TIER_WM_MID) {
                if (new_wm == TIER_WM_LOW) {
                        gf_event (EVENT_TIER_WATERMARK_DROPPED_TO_LOW,
                                  "vol=%s", volname);
                } else if (new_wm == TIER_WM_HI) {
                        gf_event (EVENT_TIER_WATERMARK_HI, "vol=%s", volname);
                }
        } else if (old_wm == TIER_WM_HI) {
                if (new_wm == TIER_WM_MID) {
                        gf_event (EVENT_TIER_WATERMARK_DROPPED_TO_MID,
                                  "vol=%s", volname);
                } else if (new_wm == TIER_WM_LOW) {
                        gf_event (EVENT_TIER_WATERMARK_DROPPED_TO_LOW,
                                  "vol=%s", volname);
                }
        }
}

int
tier_check_watermark (xlator_t *this)
{
        int                     ret       = -1;
        gf_defrag_info_t       *defrag    = NULL;
        dht_conf_t             *conf      = NULL;
        gf_tier_conf_t         *tier_conf = NULL;
        tier_watermark_op_t     wm        = TIER_WM_NONE;

        conf = this->private;
        if (!conf)
                goto exit;

        defrag = conf->defrag;
        if (!defrag)
                goto exit;

        tier_conf = &defrag->tier_conf;

        if (tier_conf->percent_full < tier_conf->watermark_low) {
                wm = TIER_WM_LOW;

        } else if (tier_conf->percent_full < tier_conf->watermark_hi) {
                wm = TIER_WM_MID;

        } else {
                wm = TIER_WM_HI;
        }

        if (wm != tier_conf->watermark_last) {

                tier_send_watermark_event (tier_conf->volname,
                                           tier_conf->watermark_last,
                                           wm);

                tier_conf->watermark_last = wm;
                gf_msg (this->name, GF_LOG_INFO, 0,
                        DHT_MSG_LOG_TIER_STATUS,
                        "Tier watermark now %d", wm);
        }

        ret = 0;

exit:
        return ret;
}


static gf_boolean_t
is_hot_tier_full (gf_tier_conf_t *tier_conf)
{
        if (tier_conf && (tier_conf->mode == TIER_MODE_WM) &&
           (tier_conf->watermark_last == TIER_WM_HI))
                return _gf_true;

        return _gf_false;
}

int
tier_do_migration (xlator_t *this, int promote)
{
        gf_defrag_info_t       *defrag = NULL;
        dht_conf_t             *conf   = NULL;
        long                    rand = 0;
        int                     migrate = 0;
        gf_tier_conf_t         *tier_conf = NULL;

        conf = this->private;
        if (!conf)
                goto exit;

        defrag = conf->defrag;
        if (!defrag)
                goto exit;

        if (tier_check_watermark (this) != 0) {
                gf_msg (this->name, GF_LOG_CRITICAL, errno,
                        DHT_MSG_LOG_TIER_ERROR,
                        "Failed to get watermark");
                goto exit;
        }

        tier_conf = &defrag->tier_conf;

        switch (tier_conf->watermark_last) {
        case TIER_WM_LOW:
                migrate = promote ? 1 : 0;
                break;
        case TIER_WM_HI:
                migrate = promote ? 0 : 1;
                break;
        case TIER_WM_MID:
                rand = random() % 100;
                if (promote) {
                        migrate = (rand > tier_conf->percent_full);
                } else {
                        migrate = (rand <= tier_conf->percent_full);
                }
                break;
        }

exit:
        return migrate;
}

int
tier_migrate (xlator_t *this, int is_promotion, dict_t *migrate_data,
              loc_t *loc, gf_tier_conf_t *tier_conf)
{
        int ret = -1;

        pthread_mutex_lock (&tier_conf->pause_mutex);
        if (is_promotion)
                tier_conf->promote_in_progress = 1;
        else
                tier_conf->demote_in_progress = 1;
        pthread_mutex_unlock (&tier_conf->pause_mutex);

        /* Data migration */
        ret = syncop_setxattr (this, loc, migrate_data, 0,
                               NULL, NULL);

        pthread_mutex_lock (&tier_conf->pause_mutex);
        if (is_promotion)
                tier_conf->promote_in_progress = 0;
        else
                tier_conf->demote_in_progress = 0;
        pthread_mutex_unlock (&tier_conf->pause_mutex);

        return ret;
}

/* returns  _gf_true: if file can be promoted
 * returns _gf_false: if file cannot be promoted
 */
static gf_boolean_t
tier_can_promote_file (xlator_t *this, char const *file_name,
                       struct iatt *current, gf_defrag_info_t *defrag)
{
        gf_boolean_t ret = _gf_false;
        fsblkcnt_t estimated_usage = 0;

        if (defrag->tier_conf.tier_max_promote_size &&
            (current->ia_size > defrag->tier_conf.tier_max_promote_size)) {
                gf_msg (this->name, GF_LOG_INFO, 0,
                        DHT_MSG_LOG_TIER_STATUS,
                        "File %s (gfid:%s) with size (%lu) exceeds maxsize "
                        "(%d) for promotion. File will not be promoted.",
                        file_name,
                        uuid_utoa(current->ia_gfid),
                        current->ia_size,
                        defrag->tier_conf.tier_max_promote_size);
                goto err;
        }

        /* bypass further validations for TEST mode */
        if (defrag->tier_conf.mode != TIER_MODE_WM) {
                ret = _gf_true;
                goto err;
        }

        /* convert the file size to blocks as per the block size of the
         * destination tier
         * NOTE: add (block_size - 1) to get the correct block size when
         *       there is a remainder after a modulo
         */
        estimated_usage = ((current->ia_size + defrag->tier_conf.block_size - 1) /
                                defrag->tier_conf.block_size) +
                                defrag->tier_conf.blocks_used;

        /* test if the estimated block usage goes above HI watermark */
        if (GF_PERCENTAGE (estimated_usage, defrag->tier_conf.blocks_total) >=
                        defrag->tier_conf.watermark_hi) {
                gf_msg (this->name, GF_LOG_INFO, 0,
                        DHT_MSG_LOG_TIER_STATUS,
                        "Estimated block count consumption on "
                        "hot tier (%lu) exceeds hi watermark (%d%%). "
                        "File will not be promoted.",
                        estimated_usage,
                        defrag->tier_conf.watermark_hi);
                goto err;
        }
        ret = _gf_true;
err:
        return ret;
}

static int
tier_set_migrate_data (dict_t *migrate_data)
{
        int    failed = 1;


        failed = dict_set_str (migrate_data, GF_XATTR_FILE_MIGRATE_KEY, "force");
        if (failed) {
                goto bail_out;
        }

        /* Flag to suggest the xattr call is from migrator */
        failed = dict_set_str (migrate_data, "from.migrator", "yes");
        if (failed) {
                goto bail_out;
        }

        /* Flag to suggest its a tiering migration
         * The reason for this dic key-value is that
         * promotions and demotions are multithreaded
         * so the original frame from gf_defrag_start()
         * is not carried. A new frame will be created when
         * we do syncop_setxattr(). This doesnot have the
         * frame->root->pid of the original frame. So we pass
         * this dic key-value when we do syncop_setxattr() to do
         * data migration and set the frame->root->pid to
         * GF_CLIENT_PID_TIER_DEFRAG in dht_setxattr() just before
         * calling dht_start_rebalance_task() */
        failed = dict_set_str (migrate_data, TIERING_MIGRATION_KEY, "yes");
        if (failed) {
                goto bail_out;
        }

        failed = 0;

bail_out:
        return failed;
}

static char *
tier_get_parent_path (xlator_t          *this,
                      loc_t             *p_loc,
                      struct iatt       *par_stbuf,
                      int               *per_link_status)
{
        int     ret             = -1;
        char    *parent_path    = NULL;
        dict_t  *xdata_request  = NULL;
        dict_t  *xdata_response = NULL;


        xdata_request = dict_new ();
        if (!xdata_request) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        DHT_MSG_LOG_TIER_ERROR,
                        "Failed to create xdata_request dict");
                goto err;
        }
        ret = dict_set_int32 (xdata_request,
                              GET_ANCESTRY_PATH_KEY, 42);
        if (ret) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        DHT_MSG_LOG_TIER_ERROR,
                        "Failed to set value to dict : key %s \n",
                        GET_ANCESTRY_PATH_KEY);
                goto err;
        }

        ret = syncop_lookup (this, p_loc, par_stbuf, NULL,
                             xdata_request, &xdata_response);
        /* When the parent gfid is a stale entry, the lookup
         * will fail and stop the demotion process.
         * The parent gfid can be stale when a huge folder is
         * deleted while the files within it are being migrated
         */
        if (ret == -ESTALE) {
                gf_msg (this->name, GF_LOG_WARNING, -ret,
                        DHT_MSG_STALE_LOOKUP,
                        "Stale entry in parent lookup for %s",
                        uuid_utoa (p_loc->gfid));
                *per_link_status = 1;
                goto err;
        } else if (ret) {
                gf_msg (this->name, GF_LOG_ERROR, -ret,
                        DHT_MSG_LOG_TIER_ERROR,
                        "Error in parent lookup for %s",
                        uuid_utoa (p_loc->gfid));
                *per_link_status = -1;
                goto err;
        }
        ret = dict_get_str (xdata_response, GET_ANCESTRY_PATH_KEY, &parent_path);
        if (ret || !parent_path) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        DHT_MSG_LOG_TIER_ERROR,
                        "Failed to get parent path for %s",
                        uuid_utoa (p_loc->gfid));
                *per_link_status = -1;
                goto err;
        }

err:
        if (xdata_request) {
                dict_unref (xdata_request);
        }

        if (xdata_response) {
                dict_unref (xdata_response);
                xdata_response = NULL;
        }

        return parent_path;
}

static int
tier_get_file_name_and_path (xlator_t                   *this,
                             uuid_t                     gfid,
                             gfdb_link_info_t           *link_info,
                             char const                 *parent_path,
                             loc_t                      *loc,
                             int                        *per_link_status)
{
        int             ret     = -1;

        loc->name = gf_strdup (link_info->file_name);
        if (!loc->name) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        DHT_MSG_LOG_TIER_ERROR, "Memory "
                        "allocation failed for %s",
                        uuid_utoa (gfid));
                *per_link_status = -1;
                goto err;
        }
        ret = gf_asprintf((char **)&(loc->path), "%s/%s", parent_path, loc->name);
        if (ret < 0) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        DHT_MSG_LOG_TIER_ERROR, "Failed to "
                        "construct file path for %s %s\n",
                        parent_path, loc->name);
                *per_link_status = -1;
                goto err;
        }

        ret = 0;

err:
        return ret;
}

static int
tier_lookup_file (xlator_t    *this,
                  loc_t       *p_loc,
                  loc_t       *loc,
                  struct iatt *current,
                  int         *per_link_status)
{
        int     ret = -1;

        ret = syncop_lookup (this, loc, current, NULL, NULL, NULL);

        /* The file may be deleted even when the parent
         * is available and the lookup will
         * return a stale entry which would stop the
         * migration. so if its a stale entry, then skip
         * the file and keep migrating.
         */
        if (ret == -ESTALE) {
                gf_msg (this->name, GF_LOG_WARNING, -ret,
                        DHT_MSG_STALE_LOOKUP,
                        "Stale lookup for %s",
                        uuid_utoa (p_loc->gfid));
                *per_link_status = 1;
                goto err;
        } else if (ret) {
                gf_msg (this->name, GF_LOG_ERROR, -ret,
                        DHT_MSG_LOG_TIER_ERROR, "Failed to "
                        "lookup file %s\n", loc->name);
                *per_link_status = -1;
                goto err;
        }
        ret = 0;

err:
        return ret;
}

static gf_boolean_t
tier_is_file_already_at_destination (xlator_t           *src_subvol,
                                     query_cbk_args_t   *query_cbk_args,
                                     dht_conf_t         *conf,
                                     int                *per_link_status)
{
        gf_boolean_t    at_destination = _gf_true;

        if (src_subvol == NULL) {
                *per_link_status = 1;
                goto err;
        }
        if (query_cbk_args->is_promotion &&
            src_subvol == conf->subvolumes[1]) {
                *per_link_status = 1;
                goto err;
        }

        if (!query_cbk_args->is_promotion &&
            src_subvol == conf->subvolumes[0]) {
                *per_link_status = 1;
                goto err;
        }
        at_destination = _gf_false;

err:
        return at_destination;
}

static void
tier_update_migration_counters (query_cbk_args_t        *query_cbk_args,
                                gf_defrag_info_t        *defrag,
                                uint64_t                *total_migrated_bytes,
                                int                     *total_files)
{
        if (query_cbk_args->is_promotion) {
                defrag->total_files_promoted++;
                *total_migrated_bytes +=
                        defrag->tier_conf.st_last_promoted_size;
                pthread_mutex_lock (&dm_stat_mutex);
                defrag->tier_conf.blocks_used +=
                        defrag->tier_conf.st_last_promoted_size;
                pthread_mutex_unlock (&dm_stat_mutex);
        } else {
                defrag->total_files_demoted++;
                *total_migrated_bytes +=
                        defrag->tier_conf.st_last_demoted_size;
                pthread_mutex_lock (&dm_stat_mutex);
                defrag->tier_conf.blocks_used -=
                        defrag->tier_conf.st_last_demoted_size;
                pthread_mutex_unlock (&dm_stat_mutex);
        }
        if (defrag->tier_conf.blocks_total) {
                pthread_mutex_lock (&dm_stat_mutex);
                defrag->tier_conf.percent_full =
                        GF_PERCENTAGE (defrag->tier_conf.blocks_used,
                                        defrag->tier_conf.blocks_total);
                pthread_mutex_unlock (&dm_stat_mutex);
        }

        (*total_files)++;
}

static int
tier_migrate_link (xlator_t            *this,
                   dht_conf_t          *conf,
                   uuid_t              gfid,
                   gfdb_link_info_t    *link_info,
                   gf_defrag_info_t    *defrag,
                   query_cbk_args_t    *query_cbk_args,
                   dict_t              *migrate_data,
                   int                 *per_link_status,
                   int                 *total_files,
                   uint64_t            *total_migrated_bytes)
{
        int             ret             = -1;
        struct iatt     current         = {0,};
        struct iatt     par_stbuf       = {0,};
        loc_t           p_loc           = {0,};
        loc_t           loc             = {0,};
        xlator_t        *src_subvol     = NULL;
        inode_t         *linked_inode   = NULL;
        char            *parent_path    = NULL;


        /* Lookup for parent and get the path of parent */
        gf_uuid_copy (p_loc.gfid, link_info->pargfid);
        p_loc.inode = inode_new (defrag->root_inode->table);
        if (!p_loc.inode) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        DHT_MSG_LOG_TIER_ERROR,
                        "Failed to create reference to inode"
                        " for %s", uuid_utoa (p_loc.gfid));

                *per_link_status = -1;
                goto err;
        }

        parent_path = tier_get_parent_path (this, &p_loc, &par_stbuf,
                                            per_link_status);
        if (!parent_path) {
                goto err;
        }

        linked_inode = inode_link (p_loc.inode, NULL, NULL, &par_stbuf);
        inode_unref (p_loc.inode);
        p_loc.inode = linked_inode;


        /* Preparing File Inode */
        gf_uuid_copy (loc.gfid, gfid);
        loc.inode = inode_new (defrag->root_inode->table);
        gf_uuid_copy (loc.pargfid, link_info->pargfid);
        loc.parent = inode_ref (p_loc.inode);

        /* Get filename and Construct file path */
        if (tier_get_file_name_and_path (this, gfid, link_info,
                                         parent_path, &loc, per_link_status) != 0) {
                goto err;
        }
        gf_uuid_copy (loc.parent->gfid, link_info->pargfid);

        /* lookup file inode */
        if (tier_lookup_file (this, &p_loc, &loc,
                              &current, per_link_status) != 0) {
                goto err;
        }

        if (query_cbk_args->is_promotion) {
                if (!tier_can_promote_file (this,
                                            link_info->file_name,
                                            &current,
                                            defrag)) {
                        *per_link_status = 1;
                        goto err;
                }
        }

        linked_inode = inode_link (loc.inode, NULL, NULL, &current);
        inode_unref (loc.inode);
        loc.inode = linked_inode;


        /*
         * Do not promote/demote if file already is where it
         * should be. It means another brick moved the file
         * so is not an error. So we set per_link_status = 1
         * so that we ignore counting this.
         */
        src_subvol = dht_subvol_get_cached (this, loc.inode);

        if (tier_is_file_already_at_destination (src_subvol, query_cbk_args,
                                                 conf, per_link_status)) {
                goto err;
        }

        gf_msg_debug (this->name, 0, "Tier %s: src_subvol %s file %s",
                      (query_cbk_args->is_promotion ?  "promote" : "demote"),
                      src_subvol->name,
                      loc.path);


        ret = tier_check_same_node (this, &loc, defrag);
        if (ret != 0) {
                if (ret < 0) {
                        *per_link_status = -1;
                        goto err;
                }
                ret = 0;
                /* By setting per_link_status to 1 we are
                 * ignoring this status and will not be counting
                 * this file for migration */
                *per_link_status = 1;
                goto err;
        }

        gf_uuid_copy (loc.gfid, loc.inode->gfid);

        if (gf_defrag_get_pause_state (&defrag->tier_conf) != TIER_RUNNING) {
                gf_msg (this->name, GF_LOG_INFO, 0,
                        DHT_MSG_LOG_TIER_STATUS,
                        "Tiering paused. "
                        "Exiting tier_migrate_link");
                goto err;
        }

        ret = tier_migrate (this, query_cbk_args->is_promotion,
                            migrate_data, &loc, &defrag->tier_conf);

        if (ret) {
                gf_msg (this->name, GF_LOG_ERROR, -ret,
                        DHT_MSG_LOG_TIER_ERROR, "Failed to "
                        "migrate %s ", loc.path);
                *per_link_status = -1;
                goto err;
        }

        tier_update_migration_counters (query_cbk_args, defrag,
                                        total_migrated_bytes, total_files);

        ret = 0;

err:
        GF_FREE ((char *) loc.name);
        loc.name = NULL;
        loc_wipe (&loc);
        loc_wipe (&p_loc);

        if ((*total_files >= defrag->tier_conf.max_migrate_files)
            || (*total_migrated_bytes > defrag->tier_conf.max_migrate_bytes)) {
                gf_msg (this->name, GF_LOG_INFO, 0,
                        DHT_MSG_LOG_TIER_STATUS,
                        "Reached cycle migration limit."
                        "migrated bytes %"PRId64" files %d",
                        *total_migrated_bytes,
                        *total_files);
                ret = -1;
        }

        return ret;
}


static int
tier_migrate_using_query_file (void *_args)
{
        int ret                                 = -1;
        query_cbk_args_t *query_cbk_args        = (query_cbk_args_t *) _args;
        xlator_t *this                          = NULL;
        gf_defrag_info_t *defrag                = NULL;
        gfdb_query_record_t *query_record       = NULL;
        gfdb_link_info_t *link_info             = NULL;
        dict_t *migrate_data                    = NULL;
        /*
         * per_file_status and per_link_status
         *  0  : success
         * -1 : failure
         *  1  : ignore the status and dont count for migration
         * */
        int per_file_status                     = 0;
        int per_link_status                     = 0;
        int total_status                        = 0;
        dht_conf_t   *conf                      = NULL;
        uint64_t total_migrated_bytes           = 0;
        int total_files                         = 0;
        loc_t root_loc                          = { 0 };
        gfdb_time_t  start_time                 = { 0 };
        gfdb_time_t  current_time               = { 0 };
        int total_time                          = 0;
        int max_time                            = 0;
        gf_boolean_t emergency_demote_mode      = _gf_false;


        GF_VALIDATE_OR_GOTO ("tier", query_cbk_args, out);
        GF_VALIDATE_OR_GOTO ("tier", query_cbk_args->this, out);
        this = query_cbk_args->this;
        GF_VALIDATE_OR_GOTO (this->name, query_cbk_args->defrag, out);
        GF_VALIDATE_OR_GOTO (this->name, query_cbk_args->qfile_array, out);
        GF_VALIDATE_OR_GOTO (this->name, this->private, out);

        conf = this->private;

        defrag = query_cbk_args->defrag;
        migrate_data = dict_new ();
        if (!migrate_data)
                goto out;

        emergency_demote_mode = (!query_cbk_args->is_promotion &&
                                 is_hot_tier_full(&defrag->tier_conf));

        if (tier_set_migrate_data (migrate_data) != 0) {
                goto out;
        }

        dht_build_root_loc (defrag->root_inode, &root_loc);

        ret = gettimeofday (&start_time, NULL);
        if (query_cbk_args->is_promotion) {
                max_time = defrag->tier_conf.tier_promote_frequency;
        } else {
                max_time = defrag->tier_conf.tier_demote_frequency;
        }

        /* Per file */
        while ((ret = read_query_record_list (query_cbk_args->qfile_array,
              &query_record)) != 0) {

                if (ret < 0) {
                        gf_msg (this->name, GF_LOG_ERROR, 0,
                                DHT_MSG_LOG_TIER_ERROR,
                                "Failed to fetch query record "
                                "from query file");
                        goto out;
                }

                if (defrag->defrag_status != GF_DEFRAG_STATUS_STARTED) {
                        ret = -1;
                        gf_msg (this->name, GF_LOG_ERROR, 0,
                                DHT_MSG_LOG_TIER_ERROR,
                                "Exiting tier migration as"
                                "defrag status is not started");
                        goto out;
                }

                ret = gettimeofday (&current_time, NULL);
                if (ret < 0) {
                        gf_msg (this->name, GF_LOG_ERROR, 0,
                                DHT_MSG_LOG_TIER_ERROR,
                                "Could not get current time.");
                        goto out;
                }

                total_time = current_time.tv_sec - start_time.tv_sec;
                if (total_time > max_time) {
                        gf_msg (this->name, GF_LOG_INFO, 0,
                                DHT_MSG_LOG_TIER_STATUS,
                                "Max cycle time reached. Exiting migration.");
                        goto out;
                }

                per_file_status      = 0;
                per_link_status      = 0;

                if (gf_defrag_get_pause_state (&defrag->tier_conf)
                    != TIER_RUNNING) {
                        gf_msg (this->name, GF_LOG_INFO, 0,
                                DHT_MSG_LOG_TIER_STATUS,
                                "Tiering paused. "
                                "Exiting tier_migrate_using_query_file");
                        break;
                }

                if (defrag->tier_conf.mode == TIER_MODE_WM) {
                        ret = tier_get_fs_stat (this, &root_loc);
                        if (ret != 0) {
                                gfdb_methods.gfdb_query_record_free (query_record);
                                query_record = NULL;
                                gf_msg (this->name, GF_LOG_ERROR, 0,
                                        DHT_MSG_LOG_TIER_STATUS,
                                        "tier_get_fs_stat() FAILED ... "
                                        "skipping file migrations until next cycle");
                                break;
                        }

                        if (!tier_do_migration (this, query_cbk_args->is_promotion)) {
                                gfdb_methods.gfdb_query_record_free (query_record);
                                query_record = NULL;

                                /* We have crossed the high watermark. Stop processing
                                 * files if this is a promotion cycle so demotion gets
                                 * a chance to start if not already running*/

                                if (query_cbk_args->is_promotion &&
                                    is_hot_tier_full (&defrag->tier_conf)) {

                                        gf_msg (this->name, GF_LOG_INFO, 0,
                                                DHT_MSG_LOG_TIER_STATUS,
                                                "High watermark crossed during "
                                                "promotion. Exiting "
                                                "tier_migrate_using_query_file");
                                        break;
                                }
                                continue;
                        }
                }

                per_link_status = 0;

                /* For now we only support single link migration. And we will
                 * ignore other hard links in the link info list of query record
                 * TODO: Multiple hard links migration */
                if (!list_empty (&query_record->link_list)) {
                        link_info = list_first_entry (&query_record->link_list,
                                                      gfdb_link_info_t, list);
                }
                if (link_info != NULL) {
                        if (tier_migrate_link (this,
                                               conf,
                                               query_record->gfid,
                                               link_info,
                                               defrag,
                                               query_cbk_args,
                                               migrate_data,
                                               &per_link_status,
                                               &total_files,
                                               &total_migrated_bytes) != 0) {
                                gf_msg (this->name, GF_LOG_INFO, 0,
                                        DHT_MSG_LOG_TIER_STATUS,
                                        "%s failed for %s(gfid:%s)",
                                        (query_cbk_args->is_promotion ?
                                         "Promotion" : "Demotion"),
                                        link_info->file_name,
                                        uuid_utoa (query_record->gfid));
                        }
                }
                per_file_status = per_link_status;

                if (per_file_status < 0) {/* Failure */
                        pthread_mutex_lock (&dm_stat_mutex);
                        defrag->total_failures++;
                        pthread_mutex_unlock (&dm_stat_mutex);
                } else if (per_file_status == 0) {/* Success */
                        pthread_mutex_lock (&dm_stat_mutex);
                        defrag->total_files++;
                        pthread_mutex_unlock (&dm_stat_mutex);
                } else if (per_file_status == 1) {/* Ignore */
                        per_file_status = 0;
                        /* Since this attempt was ignored we
                         * decrement the lookup count*/
                        pthread_mutex_lock (&dm_stat_mutex);
                        defrag->num_files_lookedup--;
                        pthread_mutex_unlock (&dm_stat_mutex);
                }
                total_status = total_status + per_file_status;
                per_link_status = 0;
                per_file_status = 0;

                gfdb_methods.gfdb_query_record_free (query_record);
                query_record = NULL;

                /* If we are demoting and the entry watermark was HI, then
                 * we are done with emergency demotions if the current
                 * watermark has fallen below hi-watermark level
                 */
                if (emergency_demote_mode) {
                        if (tier_check_watermark (this) == 0) {
                                if (!is_hot_tier_full (&defrag->tier_conf)) {
                                        break;
                                }
                        }
                }
        }

out:
        if (migrate_data)
                dict_unref (migrate_data);

        gfdb_methods.gfdb_query_record_free (query_record);
        query_record = NULL;

        return total_status;
}


/* This is the call back function per record/file from data base */
static int
tier_gf_query_callback (gfdb_query_record_t *gfdb_query_record,
                        void *_args) {
        int ret = -1;
        query_cbk_args_t *query_cbk_args = _args;

        GF_VALIDATE_OR_GOTO ("tier", query_cbk_args, out);
        GF_VALIDATE_OR_GOTO ("tier", query_cbk_args->defrag, out);
        GF_VALIDATE_OR_GOTO ("tier", (query_cbk_args->query_fd > 0), out);

        ret = gfdb_methods.gfdb_write_query_record (query_cbk_args->query_fd,
                                                        gfdb_query_record);
        if (ret) {
                gf_msg ("tier", GF_LOG_ERROR, 0, DHT_MSG_LOG_TIER_ERROR,
                        "Failed writing query record to query file");
                goto out;
        }

        pthread_mutex_lock (&dm_stat_mutex);
        query_cbk_args->defrag->num_files_lookedup++;
        pthread_mutex_unlock (&dm_stat_mutex);

        ret = 0;
out:
        return ret;
}




/* Create query file in tier process */
static int
tier_process_self_query (tier_brick_list_t *local_brick, void *args)
{
        int ret                                         = -1;
        char *db_path                                   = NULL;
        query_cbk_args_t *query_cbk_args                = NULL;
        xlator_t *this                                  = NULL;
        gfdb_conn_node_t *conn_node                     = NULL;
        dict_t *params_dict                             = NULL;
        dict_t *ctr_ipc_dict                            = NULL;
        gfdb_brick_info_t *gfdb_brick_info              = args;

        /*Init of all the essentials*/
        GF_VALIDATE_OR_GOTO ("tier", gfdb_brick_info , out);
        query_cbk_args = gfdb_brick_info->_query_cbk_args;

        GF_VALIDATE_OR_GOTO ("tier", query_cbk_args->this, out);
        this = query_cbk_args->this;

        GF_VALIDATE_OR_GOTO (this->name,
                             gfdb_brick_info->_query_cbk_args, out);

        GF_VALIDATE_OR_GOTO (this->name, local_brick, out);

        GF_VALIDATE_OR_GOTO (this->name, local_brick->xlator, out);

        GF_VALIDATE_OR_GOTO (this->name, local_brick->brick_db_path, out);

        db_path = local_brick->brick_db_path;

        /*Preparing DB parameters before init_db i.e getting db connection*/
        params_dict = dict_new ();
        if (!params_dict) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        DHT_MSG_LOG_TIER_ERROR,
                        "DB Params cannot initialized");
                goto out;
        }
        SET_DB_PARAM_TO_DICT(this->name, params_dict,
                             (char *) gfdb_methods.get_db_path_key(),
                             db_path, ret, out);

        /*Get the db connection*/
        conn_node = gfdb_methods.init_db ((void *)params_dict, dht_tier_db_type);
        if (!conn_node) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        DHT_MSG_LOG_TIER_ERROR,
                        "FATAL: Failed initializing db operations");
                         goto out;
        }

        /* Query for eligible files from db */
        query_cbk_args->query_fd = open (local_brick->qfile_path,
                        O_WRONLY | O_CREAT | O_APPEND,
                        S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);
        if (query_cbk_args->query_fd < 0) {
                gf_msg (this->name, GF_LOG_ERROR, errno,
                        DHT_MSG_LOG_TIER_ERROR,
                        "Failed to open query file %s",
                        local_brick->qfile_path);
                goto out;
        }
        if (!gfdb_brick_info->_gfdb_promote) {
                if (query_cbk_args->defrag->tier_conf.watermark_last ==
                        TIER_WM_HI) {
                        /* emergency demotion mode */
                        ret = gfdb_methods.find_all (conn_node,
                                tier_gf_query_callback,
                                (void *)query_cbk_args,
                                query_cbk_args->defrag->tier_conf.
                                        query_limit);
                } else {
                        if (query_cbk_args->defrag->write_freq_threshold == 0 &&
                            query_cbk_args->defrag->read_freq_threshold == 0) {
                                ret = gfdb_methods.find_unchanged_for_time (
                                                conn_node,
                                                tier_gf_query_callback,
                                                (void *)query_cbk_args,
                                                gfdb_brick_info->time_stamp);
                        } else {
                                ret = gfdb_methods.find_unchanged_for_time_freq (
                                        conn_node,
                                        tier_gf_query_callback,
                                        (void *)query_cbk_args,
                                        gfdb_brick_info->time_stamp,
                                        query_cbk_args->defrag->
                                                        write_freq_threshold,
                                        query_cbk_args->defrag->
                                                        read_freq_threshold,
                                        _gf_false);
                        }
                }
        } else {
                if (query_cbk_args->defrag->write_freq_threshold == 0 &&
                        query_cbk_args->defrag->read_freq_threshold == 0) {
                        ret = gfdb_methods.find_recently_changed_files (
                                conn_node,
                                tier_gf_query_callback,
                                (void *)query_cbk_args,
                                gfdb_brick_info->time_stamp);
                } else {
                        ret = gfdb_methods.find_recently_changed_files_freq (
                                conn_node,
                                tier_gf_query_callback,
                                (void *)query_cbk_args,
                                gfdb_brick_info->time_stamp,
                                query_cbk_args->defrag->write_freq_threshold,
                                query_cbk_args->defrag->read_freq_threshold,
                                _gf_false);
                }
        }
        if (ret) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                                DHT_MSG_LOG_TIER_ERROR,
                                "FATAL: query from db failed");
                        goto out;
        }

        /*Clear the heat on the DB entries*/
        /*Preparing ctr_ipc_dict*/
        ctr_ipc_dict = dict_new ();
        if (!ctr_ipc_dict) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        DHT_MSG_LOG_TIER_ERROR,
                        "ctr_ipc_dict cannot initialized");
                goto out;
        }

        SET_DB_PARAM_TO_DICT(this->name, ctr_ipc_dict,
                             GFDB_IPC_CTR_KEY, GFDB_IPC_CTR_CLEAR_OPS,
                             ret, out);

        ret = syncop_ipc (local_brick->xlator, GF_IPC_TARGET_CTR, ctr_ipc_dict,
                                                                        NULL);
        if (ret) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        DHT_MSG_LOG_TIER_ERROR, "Failed clearing the heat "
                        "on db %s error %d", local_brick->brick_db_path, ret);
                goto out;
        }

        ret = 0;
out:
        if (params_dict) {
                dict_unref (params_dict);
                params_dict = NULL;
        }

        if (ctr_ipc_dict) {
                dict_unref (ctr_ipc_dict);
                ctr_ipc_dict = NULL;
        }

        if (query_cbk_args && query_cbk_args->query_fd >= 0) {
                sys_close (query_cbk_args->query_fd);
                query_cbk_args->query_fd = -1;
        }
        gfdb_methods.fini_db (conn_node);

        return ret;
}





/*Ask CTR to create the query file*/
static int
tier_process_ctr_query (tier_brick_list_t *local_brick, void *args)
{
        int ret                                         = -1;
        query_cbk_args_t *query_cbk_args                = NULL;
        xlator_t *this                                  = NULL;
        dict_t *ctr_ipc_in_dict                         = NULL;
        dict_t *ctr_ipc_out_dict                        = NULL;
        gfdb_brick_info_t *gfdb_brick_info              = args;
        gfdb_ipc_ctr_params_t *ipc_ctr_params           = NULL;
        int count                                       = 0;

        /*Init of all the essentials*/
        GF_VALIDATE_OR_GOTO ("tier", gfdb_brick_info , out);
        query_cbk_args = gfdb_brick_info->_query_cbk_args;

        GF_VALIDATE_OR_GOTO ("tier", query_cbk_args->this, out);
        this = query_cbk_args->this;

        GF_VALIDATE_OR_GOTO (this->name,
                             gfdb_brick_info->_query_cbk_args, out);

        GF_VALIDATE_OR_GOTO (this->name, local_brick, out);

        GF_VALIDATE_OR_GOTO (this->name, local_brick->xlator, out);

        GF_VALIDATE_OR_GOTO (this->name, local_brick->brick_db_path, out);


        /*Preparing ctr_ipc_in_dict*/
        ctr_ipc_in_dict = dict_new ();
        if (!ctr_ipc_in_dict) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        DHT_MSG_LOG_TIER_ERROR,
                        "ctr_ipc_in_dict cannot initialized");
                goto out;
        }

        ipc_ctr_params = GF_CALLOC (1, sizeof (gfdb_ipc_ctr_params_t),
                                        gf_tier_mt_ipc_ctr_params_t);
        if (!ipc_ctr_params) {
                goto out;
        }

        /* set all the query params*/
        ipc_ctr_params->is_promote = gfdb_brick_info->_gfdb_promote;

        ipc_ctr_params->write_freq_threshold =
                query_cbk_args->defrag->write_freq_threshold;

        ipc_ctr_params->read_freq_threshold =
                query_cbk_args->defrag->read_freq_threshold;

        ipc_ctr_params->query_limit =
                query_cbk_args->defrag->tier_conf.query_limit;

        ipc_ctr_params->emergency_demote =
                (!gfdb_brick_info->_gfdb_promote &&
                 query_cbk_args->defrag->tier_conf.watermark_last ==
                        TIER_WM_HI);

        memcpy (&ipc_ctr_params->time_stamp,
                gfdb_brick_info->time_stamp,
                sizeof (gfdb_time_t));

        SET_DB_PARAM_TO_DICT (this->name, ctr_ipc_in_dict,
                              GFDB_IPC_CTR_KEY, GFDB_IPC_CTR_QUERY_OPS,
                              ret, out);


        SET_DB_PARAM_TO_DICT (this->name, ctr_ipc_in_dict,
                              GFDB_IPC_CTR_GET_QFILE_PATH,
                              local_brick->qfile_path,
                              ret, out);

        ret = dict_set_bin (ctr_ipc_in_dict, GFDB_IPC_CTR_GET_QUERY_PARAMS,
                                ipc_ctr_params, sizeof (*ipc_ctr_params));
        if (ret) {
                gf_msg (this->name, GF_LOG_ERROR, 0, LG_MSG_SET_PARAM_FAILED,
                        "Failed setting %s to params dictionary",
                        GFDB_IPC_CTR_GET_QUERY_PARAMS);
                GF_FREE (ipc_ctr_params);
                goto out;
        }

        ret = syncop_ipc (local_brick->xlator, GF_IPC_TARGET_CTR,
                                ctr_ipc_in_dict, &ctr_ipc_out_dict);
        if (ret) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        DHT_MSG_LOG_IPC_TIER_ERROR, "Failed query on %s ret %d",
                        local_brick->brick_db_path, ret);
                goto out;
        }

        ret = dict_get_int32(ctr_ipc_out_dict, GFDB_IPC_CTR_RET_QUERY_COUNT,
                                &count);
        if (ret) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        DHT_MSG_LOG_TIER_ERROR, "Failed getting count "
                        "of records on %s",
                        local_brick->brick_db_path);
                goto out;
        }

        if (count < 0) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        DHT_MSG_LOG_TIER_ERROR, "Failed query on %s",
                        local_brick->brick_db_path);
                ret = -1;
                goto out;
        }

        pthread_mutex_lock (&dm_stat_mutex);
        query_cbk_args->defrag->num_files_lookedup = count;
        pthread_mutex_unlock (&dm_stat_mutex);

        ret = 0;
out:

        if (ctr_ipc_in_dict) {
                dict_unref(ctr_ipc_in_dict);
                ctr_ipc_in_dict = NULL;
        }

        if (ctr_ipc_out_dict) {
                dict_unref(ctr_ipc_out_dict);
                ctr_ipc_out_dict = NULL;
        }

        return ret;
}




/* This is the call back function for each brick from hot/cold bricklist
 * It picks up each bricks db and queries for eligible files for migration.
 * The list of eligible files are populated in appropriate query files*/
static int
tier_process_brick (tier_brick_list_t *local_brick, void *args) {
        int ret = -1;
        dict_t *ctr_ipc_in_dict = NULL;
        dict_t *ctr_ipc_out_dict = NULL;
        char *strval = NULL;

        GF_VALIDATE_OR_GOTO ("tier", local_brick, out);

        GF_VALIDATE_OR_GOTO ("tier", local_brick->xlator, out);

        if (dht_tier_db_type == GFDB_SQLITE3) {

                /*Preparing ctr_ipc_in_dict*/
                ctr_ipc_in_dict = dict_new ();
                if (!ctr_ipc_in_dict) {
                        gf_msg ("tier", GF_LOG_ERROR, 0,
                                DHT_MSG_LOG_TIER_ERROR,
                                "ctr_ipc_in_dict cannot initialized");
                        goto out;
                }

                ret = dict_set_str (ctr_ipc_in_dict, GFDB_IPC_CTR_KEY,
                                    GFDB_IPC_CTR_GET_DB_PARAM_OPS);
                if (ret) {
                        gf_msg ("tier", GF_LOG_ERROR, 0,\
                                LG_MSG_SET_PARAM_FAILED, "Failed to set %s "
                                "to params dictionary", GFDB_IPC_CTR_KEY);
                        goto out;
                }

                ret = dict_set_str (ctr_ipc_in_dict,
                                    GFDB_IPC_CTR_GET_DB_PARAM_OPS, "");
                if (ret) {
                        gf_msg ("tier", GF_LOG_ERROR, 0,\
                                LG_MSG_SET_PARAM_FAILED, "Failed to set %s "
                                "to params dictionary",
                                GFDB_IPC_CTR_GET_DB_PARAM_OPS);
                        goto out;
                }

                ret = dict_set_str (ctr_ipc_in_dict,
                                    GFDB_IPC_CTR_GET_DB_KEY, "journal_mode");
                if (ret) {
                        gf_msg ("tier", GF_LOG_ERROR, 0,
                                LG_MSG_SET_PARAM_FAILED, "Failed to set %s "
                                "to params dictionary",
                                GFDB_IPC_CTR_GET_DB_KEY);
                        goto out;
                }



                ret = syncop_ipc (local_brick->xlator, GF_IPC_TARGET_CTR,
                                ctr_ipc_in_dict, &ctr_ipc_out_dict);
                if (ret || ctr_ipc_out_dict == NULL) {
                        gf_msg ("tier", GF_LOG_ERROR, 0,
                                DHT_MSG_LOG_TIER_ERROR, "Failed to get "
                                "journal_mode of sql db %s",
                                local_brick->brick_db_path);
                        goto out;
                }

                ret = dict_get_str (ctr_ipc_out_dict, "journal_mode", &strval);
                if (ret) {
                        gf_msg ("tier", GF_LOG_ERROR, 0,
                                LG_MSG_GET_PARAM_FAILED, "Failed to get %s "
                                "from params dictionary"
                                "journal_mode", strval);
                        goto out;
                }

                if (strval && (strncmp(strval, "wal", strlen ("wal")) == 0)) {
                        ret = tier_process_self_query (local_brick, args);
                        if (ret) {
                                goto out;
                        }
                } else {
                        ret = tier_process_ctr_query (local_brick, args);
                        if (ret) {
                                goto out;
                        }
                }
                ret = 0;

        } else {
                ret = tier_process_self_query (local_brick, args);
                if (ret) {
                        goto out;
                }
        }

        ret = 0;
out:
        if  (ctr_ipc_in_dict)
                dict_unref (ctr_ipc_in_dict);

        if  (ctr_ipc_out_dict)
                dict_unref (ctr_ipc_out_dict);

        return ret;
}




static int
tier_build_migration_qfile (migration_args_t *args,
                            query_cbk_args_t *query_cbk_args,
                            gf_boolean_t is_promotion)
{
        gfdb_time_t               current_time;
        gfdb_brick_info_t         gfdb_brick_info;
        gfdb_time_t               time_in_past;
        int                       ret = -1;
        tier_brick_list_t         *local_brick = NULL;
        int                       i = 0;
        time_in_past.tv_sec = args->freq_time;
        time_in_past.tv_usec = 0;

        ret = gettimeofday (&current_time, NULL);
        if (ret == -1) {
                gf_msg (args->this->name, GF_LOG_ERROR, errno,
                        DHT_MSG_SYS_CALL_GET_TIME_FAILED,
                        "Failed to get current time");
                goto out;
        }
        time_in_past.tv_sec = current_time.tv_sec - time_in_past.tv_sec;

        /* The migration daemon may run a varying numberof usec after the */
        /* sleep call triggers. A file may be registered in CTR some number */
        /* of usec X after the daemon started and missed in the subsequent */
        /* cycle if the daemon starts Y usec after the period in seconds */
        /* where Y>X. Normalize away this problem by always setting usec */
        /* to 0. */
        time_in_past.tv_usec = 0;

        gfdb_brick_info.time_stamp = &time_in_past;
        gfdb_brick_info._gfdb_promote = is_promotion;
        gfdb_brick_info._query_cbk_args = query_cbk_args;

        list_for_each_entry (local_brick, args->brick_list, list) {

                /* Construct query file path for this brick
                 * i.e
                 * /var/run/gluster/xlator_name/
                 * {promote/demote}-brickname-indexinbricklist
                 * So that no two query files will have same path even
                 * bricks have the same name
                 * */
                snprintf (local_brick->qfile_path, PATH_MAX , "%s-%s-%d",
                        GET_QFILE_PATH (gfdb_brick_info._gfdb_promote),
                        local_brick->brick_name, i);

                /* Delete any old query files for this brick */
                sys_unlink (local_brick->qfile_path);

                ret = tier_process_brick (local_brick,
                                          &gfdb_brick_info);
                if (ret) {
                        gf_msg (args->this->name, GF_LOG_ERROR, 0,
                                DHT_MSG_BRICK_QUERY_FAILED,
                                "Brick %s query failed\n",
                                local_brick->brick_db_path);
                }
                i++;
        }
        ret = 0;
out:
        return ret;
}

static int
tier_migrate_files_using_qfile (migration_args_t *comp,
                                query_cbk_args_t *query_cbk_args)
{
        int ret                                 = -1;
        tier_brick_list_t *local_brick          = NULL;
        tier_brick_list_t *temp                 = NULL;
        char query_file_path_err[PATH_MAX]      = "";
        struct tm tm                            = {0};
        gfdb_time_t current_time                = {0};
        char time_str[256]                      = {0};
        char time_format[20]                    = "%Y-%m-%d-%H-%M-%S";
        ssize_t qfile_array_size                = 0;
        int count                               = 0;
        int temp_fd                             = 0;
        gf_tier_conf_t  *tier_conf              = NULL;

        tier_conf = &(query_cbk_args->defrag->tier_conf);

        /* Time format for error query files */
        gettimeofday (&current_time, NULL);
        gmtime_r (&current_time.tv_sec, &tm);
        strftime (time_str, 256, time_format, &tm);

        /* Build the qfile list */
        list_for_each_entry_safe (local_brick, temp, comp->brick_list, list) {
                qfile_array_size++;
        }
        query_cbk_args->qfile_array = qfile_array_new (qfile_array_size);
        if (!query_cbk_args->qfile_array) {
                gf_msg ("tier", GF_LOG_ERROR, 0,
                        DHT_MSG_LOG_TIER_ERROR, "Failed to create new "
                        "qfile_array");
                goto out;
        }

        /*Open all qfiles*/
        count = 0;
        query_cbk_args->qfile_array->exhausted_count = 0;
        list_for_each_entry_safe (local_brick, temp, comp->brick_list, list) {
                temp_fd = query_cbk_args->qfile_array->fd_array[count];
                temp_fd = open (local_brick->qfile_path, O_RDONLY,
                                S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);
                if (temp_fd < 0) {
                        gf_msg ("tier", GF_LOG_ERROR, errno,
                                DHT_MSG_LOG_TIER_ERROR, "Failed to open "
                                "%s to the query file",
                                local_brick->qfile_path);
                        query_cbk_args->qfile_array->exhausted_count++;
                }
                query_cbk_args->qfile_array->fd_array[count] = temp_fd;
                count++;
        }

        /* Moving the query file index to the next, so that we won't the same
         * query file every cycle as the first one */
        query_cbk_args->qfile_array->next_index =
                        (query_cbk_args->is_promotion) ?
                        tier_conf->last_promote_qfile_index :
                        tier_conf->last_demote_qfile_index;
        shift_next_index (query_cbk_args->qfile_array);
        if (query_cbk_args->is_promotion) {
                tier_conf->last_promote_qfile_index =
                        query_cbk_args->qfile_array->next_index;
        } else {
                tier_conf->last_demote_qfile_index =
                        query_cbk_args->qfile_array->next_index;
        }

        /* Migrate files using query file list */
        ret = tier_migrate_using_query_file ((void *)query_cbk_args);
out:
        qfile_array_free (query_cbk_args->qfile_array);

        /* If there is an error rename all the query files to .err files
         * with a timestamp for better debugging */
        if (ret) {
                list_for_each_entry_safe (local_brick, temp, comp->brick_list,
                                        list) {
                        /* rename error qfile*/
                        snprintf (query_file_path_err, PATH_MAX, "%s-%s.err",
                                local_brick->qfile_path, time_str);
                        sys_rename (local_brick->qfile_path,
                                query_file_path_err);
                }
        }

        query_cbk_args->qfile_array = NULL;

        return ret;
}



int
tier_demote (migration_args_t *demotion_args)
{
        query_cbk_args_t query_cbk_args;
        int ret = -1;

        GF_VALIDATE_OR_GOTO ("tier", demotion_args, out);
        GF_VALIDATE_OR_GOTO ("tier", demotion_args->this, out);
        GF_VALIDATE_OR_GOTO (demotion_args->this->name,
                             demotion_args->brick_list, out);
        GF_VALIDATE_OR_GOTO (demotion_args->this->name,
                             demotion_args->defrag, out);

        THIS = demotion_args->this;

        query_cbk_args.this = demotion_args->this;
        query_cbk_args.defrag = demotion_args->defrag;
        query_cbk_args.is_promotion = 0;

        /*Build the query file using bricklist*/
        ret = tier_build_migration_qfile (demotion_args, &query_cbk_args,
                                          _gf_false);
        if (ret)
                goto out;

        /* Migrate files using the query file */
        ret = tier_migrate_files_using_qfile (demotion_args,
                                              &query_cbk_args);
        if (ret)
                goto out;

out:
        demotion_args->return_value = ret;
        return ret;
}


int
tier_promote (migration_args_t *promotion_args)
{
        int ret = -1;
        query_cbk_args_t query_cbk_args;

        GF_VALIDATE_OR_GOTO ("tier", promotion_args->this, out);
        GF_VALIDATE_OR_GOTO (promotion_args->this->name,
                             promotion_args->brick_list, out);
        GF_VALIDATE_OR_GOTO (promotion_args->this->name,
                             promotion_args->defrag, out);

        THIS = promotion_args->this;

        query_cbk_args.this = promotion_args->this;
        query_cbk_args.defrag = promotion_args->defrag;
        query_cbk_args.is_promotion = 1;

        /*Build the query file using bricklist*/
        ret = tier_build_migration_qfile (promotion_args, &query_cbk_args,
                                          _gf_true);
        if (ret)
                goto out;

        /* Migrate files using the query file */
        ret = tier_migrate_files_using_qfile (promotion_args, &query_cbk_args);
        if (ret)
                goto out;

out:
        promotion_args->return_value = ret;
        return ret;
}


/*
 * Command the CTR on a brick to compact the local database using an IPC
 */
static int
tier_process_self_compact (tier_brick_list_t *local_brick, void *args)
{
        int ret                                         = -1;
        char *db_path                                   = NULL;
        query_cbk_args_t *query_cbk_args                = NULL;
        xlator_t *this                                  = NULL;
        gfdb_conn_node_t *conn_node                     = NULL;
        dict_t *params_dict                             = NULL;
        dict_t *ctr_ipc_dict                            = NULL;
        gfdb_brick_info_t *gfdb_brick_info              = args;

        /*Init of all the essentials*/
        GF_VALIDATE_OR_GOTO ("tier", gfdb_brick_info , out);
        query_cbk_args = gfdb_brick_info->_query_cbk_args;

        GF_VALIDATE_OR_GOTO ("tier", query_cbk_args->this, out);
        this = query_cbk_args->this;

        GF_VALIDATE_OR_GOTO (this->name,
                             gfdb_brick_info->_query_cbk_args, out);

        GF_VALIDATE_OR_GOTO (this->name, local_brick, out);

        GF_VALIDATE_OR_GOTO (this->name, local_brick->xlator, out);

        GF_VALIDATE_OR_GOTO (this->name, local_brick->brick_db_path, out);

        db_path = local_brick->brick_db_path;

        /*Preparing DB parameters before init_db i.e getting db connection*/
        params_dict = dict_new ();
        if (!params_dict) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        DHT_MSG_LOG_TIER_ERROR,
                        "DB Params cannot initialized");
                goto out;
        }
        SET_DB_PARAM_TO_DICT (this->name, params_dict,
                             (char *) gfdb_methods.get_db_path_key(), db_path,
                             ret, out);

        /*Get the db connection*/
        conn_node = gfdb_methods.init_db ((void *)params_dict,
                                          dht_tier_db_type);
        if (!conn_node) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        DHT_MSG_LOG_TIER_ERROR,
                        "FATAL: Failed initializing db operations");
                         goto out;
        }

        ret = 0;

        /*Preparing ctr_ipc_dict*/
        ctr_ipc_dict = dict_new ();
        if (!ctr_ipc_dict) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        DHT_MSG_LOG_TIER_ERROR,
                        "ctr_ipc_dict cannot initialized");
                goto out;
        }

        ret = dict_set_int32 (ctr_ipc_dict, "compact_active",
                              query_cbk_args->defrag->
                              tier_conf.compact_active);

        if (ret) {
                gf_msg ("tier", GF_LOG_ERROR, 0,
                        LG_MSG_SET_PARAM_FAILED, "Failed to set %s "
                        "to params dictionary",
                        "compact_active");
                goto out;
        }

        ret = dict_set_int32 (ctr_ipc_dict, "compact_mode_switched",
                              query_cbk_args->defrag->
                              tier_conf.compact_mode_switched);

        if (ret) {
                gf_msg ("tier", GF_LOG_ERROR, 0,
                        LG_MSG_SET_PARAM_FAILED, "Failed to set %s "
                        "to params dictionary",
                        "compact_mode_switched");
                goto out;
        }

        SET_DB_PARAM_TO_DICT(this->name, ctr_ipc_dict,
                             GFDB_IPC_CTR_KEY, GFDB_IPC_CTR_SET_COMPACT_PRAGMA,
                             ret, out);

        gf_msg (this->name, GF_LOG_TRACE, 0, DHT_MSG_LOG_TIER_STATUS,
                "Starting Compaction IPC");

        ret = syncop_ipc (local_brick->xlator, GF_IPC_TARGET_CTR, ctr_ipc_dict,
                          NULL);

        gf_msg (this->name, GF_LOG_TRACE, 0, DHT_MSG_LOG_TIER_STATUS,
                "Ending Compaction IPC");

        if (ret) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        DHT_MSG_LOG_TIER_ERROR, "Failed compaction "
                        "on db %s error %d", local_brick->brick_db_path, ret);
                goto out;
        }

        gf_msg (this->name, GF_LOG_TRACE, 0, DHT_MSG_LOG_TIER_STATUS,
                "SUCCESS: %s Compaction", local_brick->brick_name);

        ret = 0;
out:
        if (params_dict) {
                dict_unref (params_dict);
                params_dict = NULL;
        }

        if (ctr_ipc_dict) {
                dict_unref (ctr_ipc_dict);
                ctr_ipc_dict = NULL;
        }

        gfdb_methods.fini_db (conn_node);

        return ret;
}

/*
 * This is the call back function for each brick from hot/cold bricklist.
 * It determines the database type on each brick and calls the corresponding
 * function to prepare the compaction IPC.
 */
static int
tier_compact_db_brick (tier_brick_list_t *local_brick, void *args)
{
        int ret = -1;

        GF_VALIDATE_OR_GOTO ("tier", local_brick, out);

        GF_VALIDATE_OR_GOTO ("tier", local_brick->xlator, out);

        ret = tier_process_self_compact (local_brick, args);
        if (ret) {
                gf_msg ("tier", GF_LOG_INFO, 0,
                        DHT_MSG_LOG_TIER_STATUS,
                        "Brick %s did not compact",
                        local_brick->brick_name);
                goto out;
        }

        ret = 0;

out:

        return ret;
}

static int
tier_send_compact (migration_args_t *args,
                   query_cbk_args_t *query_cbk_args)
{
        gfdb_time_t               current_time;
        gfdb_brick_info_t         gfdb_brick_info;
        gfdb_time_t               time_in_past;
        int                       ret = -1;
        tier_brick_list_t         *local_brick = NULL;

        time_in_past.tv_sec = args->freq_time;
        time_in_past.tv_usec = 0;

        ret = gettimeofday (&current_time, NULL);
        if (ret == -1) {
                gf_msg (args->this->name, GF_LOG_ERROR, errno,
                        DHT_MSG_SYS_CALL_GET_TIME_FAILED,
                        "Failed to get current time");
                goto out;
        }
        time_in_past.tv_sec = current_time.tv_sec - time_in_past.tv_sec;

        /* The migration daemon may run a varying numberof usec after the sleep
           call triggers. A file may be registered in CTR some number of usec X
           after the daemon started and missed in the subsequent cycle if the
           daemon starts Y usec after the period in seconds where Y>X. Normalize
           away this problem by always setting usec to 0. */
        time_in_past.tv_usec = 0;

        gfdb_brick_info.time_stamp = &time_in_past;

        /* This is meant to say we are always compacting at this point */
        /* We simply borrow the promotion flag to do this */
        gfdb_brick_info._gfdb_promote = 1;

        gfdb_brick_info._query_cbk_args = query_cbk_args;

        list_for_each_entry (local_brick, args->brick_list, list) {

                gf_msg (args->this->name, GF_LOG_TRACE, 0,
                        DHT_MSG_LOG_TIER_STATUS,
                        "Start compaction for %s",
                        local_brick->brick_name);

                ret = tier_compact_db_brick (local_brick,
                                             &gfdb_brick_info);
                if (ret) {
                        gf_msg (args->this->name, GF_LOG_ERROR, 0,
                                DHT_MSG_BRICK_QUERY_FAILED,
                                "Brick %s compaction failed\n",
                                local_brick->brick_db_path);
                }

                gf_msg (args->this->name, GF_LOG_TRACE, 0,
                        DHT_MSG_LOG_TIER_STATUS,
                        "End compaction for %s",
                        local_brick->brick_name);

        }
        ret = 0;
out:
        return ret;
}

static int
tier_compact (void *args)
{
        int ret = -1;
        query_cbk_args_t query_cbk_args;
        migration_args_t *compaction_args = args;

        GF_VALIDATE_OR_GOTO ("tier", compaction_args->this, out);
        GF_VALIDATE_OR_GOTO (compaction_args->this->name,
                             compaction_args->brick_list, out);
        GF_VALIDATE_OR_GOTO (compaction_args->this->name,
                             compaction_args->defrag, out);

        THIS = compaction_args->this;

        query_cbk_args.this = compaction_args->this;
        query_cbk_args.defrag = compaction_args->defrag;
        query_cbk_args.is_compaction = 1;

        /* Send the compaction pragma out to all the bricks on the bricklist. */
        /* tier_get_bricklist ensures all bricks on the list are local to */
        /* this node. */
        ret = tier_send_compact (compaction_args, &query_cbk_args);
        if (ret)
                goto out;

        ret = 0;
out:
        compaction_args->return_value = ret;
        return ret;
 }

static int
tier_get_bricklist (xlator_t *xl, struct list_head *local_bricklist_head)
{
        xlator_list_t           *child = NULL;
        char                    *rv        = NULL;
        char                    *rh        = NULL;
        char                    localhost[256] = {0};
        char                    *brickname = NULL;
        char                    db_name[PATH_MAX] = "";
        int                     ret = 0;
        tier_brick_list_t       *local_brick = NULL;

        GF_VALIDATE_OR_GOTO ("tier", xl, out);
        GF_VALIDATE_OR_GOTO ("tier", local_bricklist_head, out);

        gethostname (localhost, sizeof (localhost));

        /*
         * This function obtains remote subvolumes and filters out only
         * those running on the same node as the tier daemon.
         */
        if (strcmp(xl->type, "protocol/client") == 0) {
                ret = dict_get_str (xl->options, "remote-host", &rh);
                if (ret < 0)
                        goto out;

                if (gf_is_local_addr (rh)) {

                       local_brick = GF_CALLOC (1, sizeof(tier_brick_list_t),
                                                gf_tier_mt_bricklist_t);
                        if (!local_brick) {
                                goto out;
                        }

                        ret = dict_get_str (xl->options, "remote-subvolume",
                                           &rv);
                        if (ret < 0)
                                goto out;

                        brickname = strrchr(rv, '/') + 1;
                        snprintf(db_name, sizeof(db_name), "%s.db",
                                 brickname);

                        local_brick->brick_db_path =
                                GF_CALLOC (PATH_MAX, 1, gf_common_mt_char);
                        if (!local_brick->brick_db_path) {
                                gf_msg ("tier", GF_LOG_ERROR, 0,
                                        DHT_MSG_LOG_TIER_STATUS,
                                        "Failed to allocate memory for"
                                        " bricklist.");
                                goto out;
                        }

                        snprintf(local_brick->brick_db_path,
                                PATH_MAX, "%s/%s/%s", rv,
                                GF_HIDDEN_PATH, db_name);

                        local_brick->xlator = xl;

                        snprintf (local_brick->brick_name,
                                NAME_MAX, "%s", brickname);

                        list_add_tail (&(local_brick->list),
                                        local_bricklist_head);

                        ret = 0;
                        goto out;
                }
        }

        for (child = xl->children; child; child = child->next) {
                ret = tier_get_bricklist (child->xlator, local_bricklist_head);
                if (ret) {
                        goto out;
                }
        }

        ret = 0;
out:

        if (ret) {
                if (local_brick) {
                        GF_FREE (local_brick->brick_db_path);
                }
                GF_FREE (local_brick);
        }

        return ret;
}

int
tier_get_freq_demote (gf_tier_conf_t *tier_conf)
{
        if ((tier_conf->mode == TIER_MODE_WM) &&
            (tier_conf->watermark_last == TIER_WM_HI))
                return DEFAULT_DEMOTE_DEGRADED;
        else
                return tier_conf->tier_demote_frequency;
}

int
tier_get_freq_promote (gf_tier_conf_t *tier_conf)
{
        return tier_conf->tier_promote_frequency;
}

int
tier_get_freq_compact_hot (gf_tier_conf_t *tier_conf)
{
        return tier_conf->tier_compact_hot_frequency;
}

int
tier_get_freq_compact_cold (gf_tier_conf_t *tier_conf)
{
        return tier_conf->tier_compact_cold_frequency;
}

static int
tier_check_demote (gfdb_time_t  current_time, int freq)
{
        return ((current_time.tv_sec % freq) == 0) ?
                _gf_true : _gf_false;
}

static gf_boolean_t
tier_check_promote (gf_tier_conf_t   *tier_conf,
                    gfdb_time_t  current_time,
                    int freq)
{
        if ((tier_conf->mode == TIER_MODE_WM) &&
            (tier_conf->watermark_last == TIER_WM_HI))
                return _gf_false;

        else
                return ((current_time.tv_sec % freq) == 0) ?
                        _gf_true : _gf_false;
}

static gf_boolean_t
tier_check_compact (gf_tier_conf_t   *tier_conf,
                    gfdb_time_t  current_time,
                    int freq_compact)
{

        if (!(tier_conf->compact_active ||
              tier_conf->compact_mode_switched))
                return _gf_false;


        return ((current_time.tv_sec % freq_compact) == 0) ?
                _gf_true : _gf_false;
}


void
clear_bricklist (struct list_head *brick_list)
{
        tier_brick_list_t  *local_brick      = NULL;
        tier_brick_list_t  *temp             = NULL;

        if (list_empty(brick_list)) {
                return;
        }

        list_for_each_entry_safe (local_brick, temp, brick_list, list) {
                list_del (&local_brick->list);
                GF_FREE (local_brick->brick_db_path);
                GF_FREE (local_brick);
        }
}


static void
set_brick_list_qpath (struct list_head *brick_list, gf_boolean_t is_cold)
{

        tier_brick_list_t *local_brick = NULL;
        int i                          = 0;

        GF_VALIDATE_OR_GOTO ("tier", brick_list, out);

        list_for_each_entry (local_brick, brick_list, list) {

                /* Construct query file path for this brick
                 * i.e
                 * /var/run/gluster/xlator_name/
                 * {promote/demote}-brickname-indexinbricklist
                 * So that no two query files will have same path even
                 * bricks have the same name
                 * */
                snprintf (local_brick->qfile_path, PATH_MAX , "%s-%s-%d",
                        GET_QFILE_PATH (is_cold),
                        local_brick->brick_name, i);
                i++;
        }
out:
        return;
}

static int
tier_prepare_compact (migration_args_t *args, gfdb_time_t current_time)
{
        xlator_t *this = NULL;
        dht_conf_t *conf                        = NULL;
        gf_defrag_info_t *defrag                = NULL;
        gf_tier_conf_t *tier_conf               = NULL;
        gf_boolean_t is_hot_tier = _gf_false;
        int freq = 0;
        int ret = -1;
        const char *tier_type = is_hot_tier ? "hot" : "cold";

        this = args->this;

        conf = this->private;

        defrag = conf->defrag;

        tier_conf = &defrag->tier_conf;

        is_hot_tier = args->is_hot_tier;

        freq = is_hot_tier ? tier_get_freq_compact_hot (tier_conf) :
                tier_get_freq_compact_cold (tier_conf);

        defrag->tier_conf.compact_mode_switched = is_hot_tier ?
                defrag->tier_conf.compact_mode_switched_hot :
                defrag->tier_conf.compact_mode_switched_cold;

        gf_msg(this->name, GF_LOG_TRACE, 0,
               DHT_MSG_LOG_TIER_STATUS,
               "Compact mode %i",
               defrag->tier_conf.compact_mode_switched);

        if (tier_check_compact (tier_conf, current_time,
                                freq)) {
                gf_msg (this->name, GF_LOG_INFO, 0,
                        DHT_MSG_LOG_TIER_STATUS,
                        "Start compaction on %s tier",
                        tier_type);

                args->freq_time = freq;
                ret = tier_compact (args);
                if (ret) {
                        gf_msg (this->name, GF_LOG_ERROR, 0,
                                DHT_MSG_LOG_TIER_ERROR, "Compaction failed on "
                                "%s tier", tier_type);
                        goto out;
                }

                gf_msg (this->name, GF_LOG_INFO, 0, DHT_MSG_LOG_TIER_STATUS,
                        "End compaction on %s tier", tier_type);

                if (is_hot_tier) {
                        defrag->tier_conf.compact_mode_switched_hot =
                                _gf_false;
                } else {
                        defrag->tier_conf.compact_mode_switched_cold =
                                _gf_false;
                }
        }

out:
        return ret;
}

static int
tier_get_wm_interval(tier_mode_t mode, tier_watermark_op_t wm)
{
        if (mode == TIER_MODE_WM && wm == TIER_WM_HI)
                return WM_INTERVAL_EMERG;

        return WM_INTERVAL;
}

/*
 * Main tiering loop. This is called from the promotion and the
 * demotion threads spawned in tier_start().
 *
 * Every second, wake from sleep to perform tasks.
 * 1. Check trigger to migrate data.
 * 2. Check for state changes (pause, unpause, stop).
 */
static void
*tier_run (void *in_args)
{
        dht_conf_t *conf                        = NULL;
        gfdb_time_t  current_time               = { 0 };
        int freq                                = 0;
        int ret                                 = 0;
        xlator_t *any                           = NULL;
        xlator_t *xlator                        = NULL;
        gf_tier_conf_t *tier_conf               = NULL;
        loc_t root_loc                          = { 0 };
        int check_watermark                     = 0;
        gf_defrag_info_t *defrag                = NULL;
        xlator_t  *this                         = NULL;
        migration_args_t *args = in_args;
        GF_VALIDATE_OR_GOTO ("tier", args, out);
        GF_VALIDATE_OR_GOTO ("tier", args->brick_list, out);

        this = args->this;
        GF_VALIDATE_OR_GOTO ("tier", this, out);

        conf   = this->private;
        GF_VALIDATE_OR_GOTO ("tier", conf, out);

        defrag = conf->defrag;
        GF_VALIDATE_OR_GOTO ("tier", defrag, out);

        if (list_empty (args->brick_list)) {
                gf_msg (this->name, GF_LOG_INFO, 0,
                        DHT_MSG_LOG_TIER_ERROR,
                        "Brick list for tier is empty. Exiting.");
                goto out;
        }

        defrag->defrag_status = GF_DEFRAG_STATUS_STARTED;
        tier_conf = &defrag->tier_conf;

        dht_build_root_loc (defrag->root_inode, &root_loc);

        while (1) {

                /*
                 * Check if a graph switch occured. If so, stop migration
                 * thread. It will need to be restarted manually.
                 */
                any = THIS->ctx->active->first;
                xlator = xlator_search_by_name (any, this->name);

                if (xlator != this) {
                        gf_msg (this->name, GF_LOG_INFO, 0,
                                DHT_MSG_LOG_TIER_STATUS,
                                "Detected graph switch. Exiting migration "
                                "daemon.");
                        goto out;
                }

                gf_defrag_check_pause_tier (tier_conf);

                sleep(1);

                if (defrag->defrag_status != GF_DEFRAG_STATUS_STARTED) {
                        ret = 1;
                        gf_msg (this->name, GF_LOG_ERROR, 0,
                                DHT_MSG_LOG_TIER_ERROR,
                                "defrag->defrag_status != "
                                "GF_DEFRAG_STATUS_STARTED");
                        goto out;
                }

                if (defrag->cmd == GF_DEFRAG_CMD_START_DETACH_TIER ||
                    defrag->cmd == GF_DEFRAG_CMD_DETACH_START) {
                        ret = 0;
                        defrag->defrag_status =
                                GF_DEFRAG_STATUS_COMPLETE;
                        gf_msg (this->name, GF_LOG_DEBUG, 0,
                                DHT_MSG_LOG_TIER_ERROR,
                                "defrag->defrag_cmd == "
                                "GF_DEFRAG_CMD_START_DETACH_TIER");
                        goto out;
                }

                if (gf_defrag_get_pause_state (&defrag->tier_conf) !=
                    TIER_RUNNING)
                        continue;

                /* To have proper synchronization amongst all
                 * brick holding nodes, so that promotion and demotions
                 * start atomicly w.r.t promotion/demotion frequency
                 * period, all nodes should have thier system time
                 * in-sync with each other either manually set or
                 * using a NTP server*/
                ret = gettimeofday (&current_time, NULL);
                if (ret == -1) {
                        gf_msg (this->name, GF_LOG_ERROR, errno,
                                DHT_MSG_SYS_CALL_GET_TIME_FAILED,
                                "Failed to get current time");
                        goto out;
                }

                check_watermark++;

                /* emergency demotion requires frequent watermark monitoring */
                if (check_watermark >=
                        tier_get_wm_interval(tier_conf->mode,
                                             tier_conf->watermark_last)) {
                        check_watermark = 0;
                        if (tier_conf->mode == TIER_MODE_WM) {
                                ret = tier_get_fs_stat (this, &root_loc);
                                if (ret != 0) {
                                        continue;
                                }
                                ret = tier_check_watermark (this);
                                if (ret != 0) {
                                        gf_msg (this->name, GF_LOG_CRITICAL, errno,
                                                DHT_MSG_LOG_TIER_ERROR,
                                                "Failed to get watermark");
                                        continue;
                                }
                        }
                }

                if (args->is_promotion) {
                        freq = tier_get_freq_promote (tier_conf);

                        if (tier_check_promote (tier_conf, current_time, freq)) {
                                args->freq_time = freq;
                                ret = tier_promote (args);
                                if (ret) {
                                        gf_msg (this->name, GF_LOG_ERROR, 0,
                                                DHT_MSG_LOG_TIER_ERROR,
                                                "Promotion failed");
                                }
                        }
                } else if (args->is_compaction) {
                        tier_prepare_compact (args, current_time);
                } else {
                        freq = tier_get_freq_demote (tier_conf);

                        if (tier_check_demote (current_time, freq)) {
                                args->freq_time = freq;
                                ret = tier_demote (args);
                                if (ret) {
                                        gf_msg (this->name,
                                                GF_LOG_ERROR,
                                                0,
                                                DHT_MSG_LOG_TIER_ERROR,
                                                "Demotion failed");
                                }
                        }
                }

                /* Check the statfs immediately after the processing threads
                   return */
                check_watermark = WM_INTERVAL;
        }

        ret = 0;
out:

        args->return_value = ret;

        return NULL;
}

int
tier_start (xlator_t *this, gf_defrag_info_t *defrag)
{
        pthread_t promote_thread;
        pthread_t demote_thread;
        pthread_t hot_compact_thread;
        pthread_t cold_compact_thread;
        int ret = -1;
        struct list_head bricklist_hot          = { 0 };
        struct list_head bricklist_cold         = { 0 };
        migration_args_t promotion_args         = { 0 };
        migration_args_t demotion_args          = { 0 };
        migration_args_t hot_compaction_args    = { 0 };
        migration_args_t cold_compaction_args   = { 0 };
        dht_conf_t *conf                        = NULL;

        INIT_LIST_HEAD ((&bricklist_hot));
        INIT_LIST_HEAD ((&bricklist_cold));

        conf   = this->private;

        tier_get_bricklist (conf->subvolumes[1], &bricklist_hot);
        set_brick_list_qpath (&bricklist_hot, _gf_false);

        demotion_args.this = this;
        demotion_args.brick_list = &bricklist_hot;
        demotion_args.defrag = defrag;
        demotion_args.is_promotion = _gf_false;
        demotion_args.is_compaction = _gf_false;

        ret = pthread_create (&demote_thread,
                              NULL, &tier_run,
                              &demotion_args);
        if (ret) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        DHT_MSG_LOG_TIER_ERROR,
                        "Failed to start demotion thread.");
                defrag->defrag_status = GF_DEFRAG_STATUS_FAILED;
                goto cleanup;
        }

        tier_get_bricklist (conf->subvolumes[0], &bricklist_cold);
        set_brick_list_qpath (&bricklist_cold, _gf_true);

        promotion_args.this = this;
        promotion_args.brick_list = &bricklist_cold;
        promotion_args.defrag = defrag;
        promotion_args.is_promotion = _gf_true;

        ret = pthread_create (&promote_thread,
                              NULL, &tier_run,
                              &promotion_args);
        if (ret) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        DHT_MSG_LOG_TIER_ERROR,
                        "Failed to start promotion thread.");
                defrag->defrag_status = GF_DEFRAG_STATUS_FAILED;
                goto waitforspawned;
        }

        hot_compaction_args.this = this;
        hot_compaction_args.brick_list = &bricklist_hot;
        hot_compaction_args.defrag = defrag;
        hot_compaction_args.is_promotion = _gf_false;
        hot_compaction_args.is_compaction = _gf_true;
        hot_compaction_args.is_hot_tier = _gf_true;

        ret = pthread_create (&hot_compact_thread,
                              NULL, &tier_run,
                              &hot_compaction_args);
        if (ret) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        DHT_MSG_LOG_TIER_ERROR,
                        "Failed to start compaction thread.");
                defrag->defrag_status = GF_DEFRAG_STATUS_FAILED;
                goto waitforspawnedpromote;
        }

        cold_compaction_args.this = this;
        cold_compaction_args.brick_list = &bricklist_cold;
        cold_compaction_args.defrag = defrag;
        cold_compaction_args.is_promotion = _gf_false;
        cold_compaction_args.is_compaction = _gf_true;
        cold_compaction_args.is_hot_tier = _gf_false;

        ret = pthread_create (&cold_compact_thread,
                              NULL, &tier_run,
                              &cold_compaction_args);
        if (ret) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        DHT_MSG_LOG_TIER_ERROR,
                        "Failed to start compaction thread.");
                defrag->defrag_status = GF_DEFRAG_STATUS_FAILED;
                goto waitforspawnedhotcompact;
        }
        pthread_join (cold_compact_thread, NULL);

waitforspawnedhotcompact:
        pthread_join (hot_compact_thread, NULL);

waitforspawnedpromote:
        pthread_join (promote_thread, NULL);

waitforspawned:
        pthread_join (demote_thread, NULL);

cleanup:
        clear_bricklist (&bricklist_cold);
        clear_bricklist (&bricklist_hot);
        return ret;
}

int32_t
tier_migration_needed (xlator_t *this)
{
        gf_defrag_info_t        *defrag = NULL;
        dht_conf_t              *conf   = NULL;
        int                      ret = 0;

        conf = this->private;

        GF_VALIDATE_OR_GOTO (this->name, conf, out);
        GF_VALIDATE_OR_GOTO (this->name, conf->defrag, out);

        defrag = conf->defrag;

        if ((defrag->cmd == GF_DEFRAG_CMD_START_TIER) ||
            (defrag->cmd == GF_DEFRAG_CMD_START_DETACH_TIER))
                ret = 1;
out:
        return ret;
}

int32_t
tier_migration_get_dst (xlator_t *this, dht_local_t *local)
{
        dht_conf_t              *conf   = NULL;
        int32_t                  ret = -1;
        gf_defrag_info_t        *defrag = NULL;

        GF_VALIDATE_OR_GOTO ("tier", this, out);
        GF_VALIDATE_OR_GOTO (this->name, this->private, out);

        conf = this->private;

        defrag = conf->defrag;

        if (defrag && defrag->cmd == GF_DEFRAG_CMD_START_DETACH_TIER) {
                local->rebalance.target_node = conf->subvolumes[0];

        } else if (conf->subvolumes[0] == local->cached_subvol)
                local->rebalance.target_node =
                        conf->subvolumes[1];
        else
                local->rebalance.target_node =
                        conf->subvolumes[0];

        if (local->rebalance.target_node)
                ret = 0;

out:
        return ret;
}

xlator_t *
tier_search (xlator_t *this, dht_layout_t *layout, const char *name)
{
        xlator_t                *subvol = NULL;
        dht_conf_t              *conf   = NULL;

        GF_VALIDATE_OR_GOTO ("tier", this, out);
        GF_VALIDATE_OR_GOTO (this->name, this->private, out);

        conf = this->private;

        subvol = TIER_HASHED_SUBVOL;

 out:
        return subvol;
}


static int
tier_load_externals (xlator_t *this)
{
        int               ret            = -1;
        char *libpathfull = (LIBDIR "/libgfdb.so.0");
        get_gfdb_methods_t get_gfdb_methods;

        GF_VALIDATE_OR_GOTO ("this", this, out);

        libhandle = dlopen (libpathfull, RTLD_NOW);
        if (!libhandle) {
                gf_msg(this->name, GF_LOG_ERROR, 0,
                       DHT_MSG_LOG_TIER_ERROR,
                       "Error loading libgfdb.so %s\n", dlerror());
                ret = -1;
                goto out;
        }

        get_gfdb_methods = dlsym (libhandle, "get_gfdb_methods");
        if (!get_gfdb_methods) {
                gf_msg(this->name, GF_LOG_ERROR, 0,
                       DHT_MSG_LOG_TIER_ERROR,
                       "Error loading get_gfdb_methods()");
                ret = -1;
                goto out;
        }

        get_gfdb_methods (&gfdb_methods);

        ret = 0;

out:
        if (ret && libhandle)
                dlclose (libhandle);

        return ret;
}

static int
tier_validate_mode (char *mode)
{
        int ret = -1;

        if (strcmp (mode, "test") == 0) {
                ret = TIER_MODE_TEST;
        } else {
                ret = TIER_MODE_WM;
        }

        return ret;
}

static gf_boolean_t
tier_validate_compact_mode (char *mode)
{
        gf_boolean_t ret = _gf_false;

        gf_msg ("tier", GF_LOG_INFO, 0, DHT_MSG_LOG_TIER_STATUS,
                "tier_validate_compact_mode: mode = %s", mode);

        if (!strcmp (mode, "on")) {
                ret = _gf_true;
        } else {
                ret = _gf_false;
        }

        gf_msg ("tier", GF_LOG_ERROR, 0,
                DHT_MSG_LOG_TIER_STATUS,
                "tier_validate_compact_mode: ret = %i", ret);

        return ret;
}

int
tier_init_methods (xlator_t *this)
{
        int ret                  = -1;
        dht_conf_t      *conf    = NULL;
        dht_methods_t   *methods = NULL;

        GF_VALIDATE_OR_GOTO ("tier", this, err);

        conf = this->private;

        methods = &(conf->methods);

        methods->migration_get_dst_subvol = tier_migration_get_dst;
        methods->migration_other   = tier_start;
        methods->migration_needed  = tier_migration_needed;
        methods->layout_search     = tier_search;

        ret = 0;
err:
        return ret;
}


static void
tier_save_vol_name (xlator_t *this)
{
        dht_conf_t       *conf           = NULL;
        gf_defrag_info_t *defrag         = NULL;
        char             *suffix         = NULL;
        int               name_len       = 0;


        conf = this->private;
        defrag = conf->defrag;

        suffix = strstr (this->name, "-tier-dht");

        if (suffix)
                name_len = suffix - this->name;
        else
                name_len = strlen (this->name);

        if (name_len > GD_VOLUME_NAME_MAX)
                name_len = GD_VOLUME_NAME_MAX;

        strncpy (defrag->tier_conf.volname, this->name, name_len);
        defrag->tier_conf.volname[name_len] = 0;
}

int
tier_init (xlator_t *this)
{
        int               ret            = -1;
        int               freq           = 0;
        int               maxsize        = 0;
        dht_conf_t       *conf           = NULL;
        gf_defrag_info_t *defrag         = NULL;
        char             *voldir         = NULL;
        char             *mode           = NULL;
        char             *paused         = NULL;

        ret = dht_init (this);
        if (ret) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        DHT_MSG_LOG_TIER_ERROR,
                        "tier_init failed");
                goto out;
        }

        conf = this->private;

        ret = tier_init_methods (this);
        if (ret) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        DHT_MSG_LOG_TIER_ERROR,
                        "tier_init_methods failed");
                goto out;
        }

        if (conf->subvolume_cnt != 2) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        DHT_MSG_LOG_TIER_ERROR,
                        "Invalid number of subvolumes %d", conf->subvolume_cnt);
                goto out;
        }

        /* if instatiated from client side initialization is complete. */
        if (!conf->defrag) {
                ret = 0;
                goto out;
        }

        /* if instatiated from server side, load db libraries */
        ret = tier_load_externals (this);
        if (ret) {
                gf_msg(this->name, GF_LOG_ERROR, 0,
                       DHT_MSG_LOG_TIER_ERROR,
                       "Could not load externals. Aborting");
                goto out;
        }

        defrag = conf->defrag;

        defrag->tier_conf.last_demote_qfile_index = 0;
        defrag->tier_conf.last_promote_qfile_index = 0;

        defrag->tier_conf.is_tier = 1;

        ret = dict_get_int32 (this->options,
                              "tier-max-promote-file-size", &maxsize);
        if (ret) {
                maxsize = 0;
        }

        defrag->tier_conf.tier_max_promote_size = maxsize;

        ret = dict_get_int32 (this->options,
                              "tier-promote-frequency", &freq);
        if (ret) {
                freq = DEFAULT_PROMOTE_FREQ_SEC;
        }

        defrag->tier_conf.tier_promote_frequency = freq;

        ret = dict_get_int32 (this->options,
                              "tier-demote-frequency", &freq);
        if (ret) {
                freq = DEFAULT_DEMOTE_FREQ_SEC;
        }

        defrag->tier_conf.tier_demote_frequency = freq;

        ret = dict_get_int32 (this->options,
                              "tier-hot-compact-frequency", &freq);
        if (ret) {
                freq = DEFAULT_HOT_COMPACT_FREQ_SEC;
        }

        defrag->tier_conf.tier_compact_hot_frequency = freq;

        ret = dict_get_int32 (this->options,
                              "tier-cold-compact-frequency", &freq);
        if (ret) {
                freq = DEFAULT_COLD_COMPACT_FREQ_SEC;
        }

        defrag->tier_conf.tier_compact_cold_frequency = freq;

        ret = dict_get_int32 (this->options,
                              "watermark-hi", &freq);
        if (ret) {
                freq = DEFAULT_WM_HI;
        }

        defrag->tier_conf.watermark_hi = freq;

        ret = dict_get_int32 (this->options,
                              "watermark-low", &freq);
        if (ret) {
                freq = DEFAULT_WM_LOW;
        }

        defrag->tier_conf.watermark_low = freq;

        ret = dict_get_int32 (this->options,
                              "write-freq-threshold", &freq);
        if (ret) {
                freq = DEFAULT_WRITE_FREQ_SEC;
        }

        defrag->write_freq_threshold = freq;

        ret = dict_get_int32 (this->options,
                              "read-freq-threshold", &freq);
        if (ret) {
                freq = DEFAULT_READ_FREQ_SEC;
        }

        defrag->read_freq_threshold = freq;

        ret = dict_get_int32 (this->options,
                              "tier-max-mb", &freq);
        if (ret) {
                freq = DEFAULT_TIER_MAX_MIGRATE_MB;
        }

        defrag->tier_conf.max_migrate_bytes = (uint64_t) freq * 1024 * 1024;

        ret = dict_get_int32 (this->options,
                              "tier-max-files", &freq);
        if (ret) {
                freq = DEFAULT_TIER_MAX_MIGRATE_FILES;
        }

        defrag->tier_conf.max_migrate_files = freq;


        ret = dict_get_int32 (this->options,
                              "tier-query-limit",
                              &(defrag->tier_conf.query_limit));
        if (ret) {
                defrag->tier_conf.query_limit =
                        DEFAULT_TIER_QUERY_LIMIT;
        }

        ret = dict_get_str (this->options,
                            "tier-compact", &mode);

        if (ret) {
                defrag->tier_conf.compact_active = DEFAULT_COMP_MODE;
        } else {
                ret = tier_validate_compact_mode (mode);
                if (ret < 0) {
                        gf_msg(this->name, GF_LOG_ERROR, 0,
                               DHT_MSG_LOG_TIER_ERROR,
                               "tier_init failed - invalid compaction mode");
                        goto out;
                }

                /* If compaction is now active, we need to inform the bricks on
                   the hot and cold tier of this. See dht-common.h for more. */
                defrag->tier_conf.compact_active = ret;
                if (ret) {
                        defrag->tier_conf.compact_mode_switched_hot = _gf_true;
                        defrag->tier_conf.compact_mode_switched_cold = _gf_true;
                }
        }

        ret = dict_get_str (this->options,
                            "tier-mode", &mode);
        if (ret) {
                defrag->tier_conf.mode = DEFAULT_TIER_MODE;
        } else {
                ret = tier_validate_mode (mode);
                if (ret < 0) {
                        gf_msg(this->name, GF_LOG_ERROR, 0,
                               DHT_MSG_LOG_TIER_ERROR,
                               "tier_init failed - invalid mode");
                        goto out;
                }
                defrag->tier_conf.mode = ret;
        }

        pthread_mutex_init (&defrag->tier_conf.pause_mutex, 0);

        gf_defrag_set_pause_state (&defrag->tier_conf, TIER_RUNNING);

        ret = dict_get_str (this->options,
                              "tier-pause", &paused);

        if (paused && strcmp (paused, "on") == 0)
                gf_defrag_set_pause_state (&defrag->tier_conf,
                                           TIER_REQUEST_PAUSE);

        ret = gf_asprintf(&voldir, "%s/%s",
                          DEFAULT_VAR_RUN_DIRECTORY,
                          this->name);
        if (ret < 0)
                goto out;

        ret = mkdir_p(voldir, 0777, _gf_true);
        if (ret == -1 && errno != EEXIST) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                       DHT_MSG_LOG_TIER_ERROR,
                       "tier_init failed");

                GF_FREE(voldir);
                goto out;
        }

        GF_FREE(voldir);

        ret = gf_asprintf (&promotion_qfile, "%s/%s/promote",
                           DEFAULT_VAR_RUN_DIRECTORY,
                           this->name);
        if (ret < 0)
                goto out;

        ret = gf_asprintf (&demotion_qfile, "%s/%s/demote",
                           DEFAULT_VAR_RUN_DIRECTORY,
                           this->name);
        if (ret < 0) {
                GF_FREE (promotion_qfile);
                goto out;
        }

        gf_msg (this->name, GF_LOG_INFO, 0,
                DHT_MSG_LOG_TIER_STATUS,
               "Promote/demote frequency %d/%d "
               "Write/Read freq thresholds %d/%d",
               defrag->tier_conf.tier_promote_frequency,
               defrag->tier_conf.tier_demote_frequency,
               defrag->write_freq_threshold,
               defrag->read_freq_threshold);

        tier_save_vol_name (this);

        ret = 0;

out:

        return ret;
}

int
tier_cli_pause_done (int op_ret, call_frame_t *sync_frame, void *data)
{
        gf_msg ("tier", GF_LOG_INFO, 0,
                DHT_MSG_TIER_PAUSED,
                "Migrate file paused with op_ret %d", op_ret);

        return op_ret;
}

int
tier_cli_pause (void *data)
{
        gf_defrag_info_t        *defrag         = NULL;
        xlator_t                *this           = NULL;
        dht_conf_t              *conf           = NULL;
        int                      ret            = -1;

        this = data;

        conf = this->private;
        GF_VALIDATE_OR_GOTO (this->name, conf, exit);

        defrag = conf->defrag;
        GF_VALIDATE_OR_GOTO (this->name, defrag, exit);

        gf_defrag_pause_tier (this, defrag);

        ret = 0;
exit:
        return ret;
}

int
tier_reconfigure (xlator_t *this, dict_t *options)
{
        dht_conf_t       *conf            = NULL;
        gf_defrag_info_t *defrag          = NULL;
        char             *mode            = NULL;
        int               migrate_mb      = 0;
        gf_boolean_t      req_pause       = _gf_false;
        int               ret             = 0;
        call_frame_t            *frame    = NULL;
        gf_boolean_t last_compact_setting = _gf_false;

        conf = this->private;

        if (conf->defrag) {
                defrag = conf->defrag;
                GF_OPTION_RECONF ("tier-max-promote-file-size",
                                  defrag->tier_conf.tier_max_promote_size,
                                  options, int32, out);

                GF_OPTION_RECONF ("tier-promote-frequency",
                                  defrag->tier_conf.tier_promote_frequency,
                                  options, int32, out);

                GF_OPTION_RECONF ("tier-demote-frequency",
                                  defrag->tier_conf.tier_demote_frequency,
                                  options, int32, out);

                GF_OPTION_RECONF ("write-freq-threshold",
                                  defrag->write_freq_threshold, options,
                                  int32, out);

                GF_OPTION_RECONF ("read-freq-threshold",
                                  defrag->read_freq_threshold, options,
                                  int32, out);

                GF_OPTION_RECONF ("watermark-hi",
                                  defrag->tier_conf.watermark_hi, options,
                                  int32, out);

                GF_OPTION_RECONF ("watermark-low",
                                  defrag->tier_conf.watermark_low, options,
                                  int32, out);

                last_compact_setting = defrag->tier_conf.compact_active;

                GF_OPTION_RECONF ("tier-compact",
                                  defrag->tier_conf.compact_active, options,
                                  bool, out);

                if (last_compact_setting != defrag->tier_conf.compact_active) {
                        defrag->tier_conf.compact_mode_switched_hot = _gf_true;
                        defrag->tier_conf.compact_mode_switched_cold = _gf_true;
                        gf_msg (this->name, GF_LOG_INFO, 0,
                                DHT_MSG_LOG_TIER_STATUS,
                                "compact mode switched");
                }

                GF_OPTION_RECONF ("tier-hot-compact-frequency",
                                  defrag->tier_conf.tier_compact_hot_frequency,
                                  options, int32, out);

                GF_OPTION_RECONF ("tier-cold-compact-frequency",
                                  defrag->tier_conf.tier_compact_cold_frequency,
                                  options, int32, out);

                GF_OPTION_RECONF ("tier-mode",
                                  mode, options,
                                  str, out);
                defrag->tier_conf.mode = tier_validate_mode (mode);

                GF_OPTION_RECONF ("tier-max-mb",
                                  migrate_mb, options,
                                  int32, out);
                defrag->tier_conf.max_migrate_bytes = (uint64_t) migrate_mb *
                                                        1024 * 1024;

                GF_OPTION_RECONF ("tier-max-files",
                                  defrag->tier_conf.max_migrate_files, options,
                                  int32, out);

                GF_OPTION_RECONF ("tier-query-limit",
                                  defrag->tier_conf.query_limit,
                                  options, int32, out);

                GF_OPTION_RECONF ("tier-pause",
                                  req_pause, options,
                                  bool, out);

                if (req_pause == _gf_true) {

                        frame = create_frame (this, this->ctx->pool);
                        if (!frame)
                                goto out;

                        frame->root->pid = GF_CLIENT_PID_DEFRAG;

                        ret = synctask_new (this->ctx->env, tier_cli_pause,
                                            tier_cli_pause_done, frame, this);

                        if (ret) {
                                gf_msg (this->name, GF_LOG_ERROR, 0,
                                        DHT_MSG_LOG_TIER_ERROR,
                                        "pause tier failed on reconfigure");
                        }
                } else {
                        ret = gf_defrag_resume_tier (this, defrag);
                        if (ret) {
                                gf_msg (this->name, GF_LOG_ERROR, 0,
                                        DHT_MSG_LOG_TIER_ERROR,
                                        "resume tier failed on reconfigure");
                        }
                }

        }

out:
        return dht_reconfigure (this, options);
}

void
tier_fini (xlator_t *this)
{
        if (libhandle)
                dlclose (libhandle);

        GF_FREE (demotion_qfile);
        GF_FREE (promotion_qfile);

        dht_fini(this);
}

class_methods_t class_methods = {
        .init           = tier_init,
        .fini           = tier_fini,
        .reconfigure    = tier_reconfigure,
        .notify         = dht_notify
};

struct xlator_fops fops = {

        .lookup      = dht_lookup,
        .create      = tier_create,
        .mknod       = dht_mknod,

        .open        = dht_open,
        .statfs      = tier_statfs,
        .opendir     = dht_opendir,
        .readdir     = tier_readdir,
        .readdirp    = tier_readdirp,
        .fsyncdir    = dht_fsyncdir,
        .symlink     = dht_symlink,
        .unlink      = tier_unlink,
        .link        = tier_link,
        .mkdir       = dht_mkdir,
        .rmdir       = dht_rmdir,
        .rename      = dht_rename,
        .entrylk     = dht_entrylk,
        .fentrylk    = dht_fentrylk,

        /* Inode read operations */
        .stat        = dht_stat,
        .fstat       = dht_fstat,
        .access      = dht_access,
        .readlink    = dht_readlink,
        .getxattr    = dht_getxattr,
        .fgetxattr    = dht_fgetxattr,
        .readv       = dht_readv,
        .flush       = dht_flush,
        .fsync       = dht_fsync,
        .inodelk     = dht_inodelk,
        .finodelk    = dht_finodelk,
        .lk          = dht_lk,

        /* Inode write operations */
        .fremovexattr = dht_fremovexattr,
        .removexattr = dht_removexattr,
        .setxattr    = dht_setxattr,
        .fsetxattr   = dht_fsetxattr,
        .truncate    = dht_truncate,
        .ftruncate   = dht_ftruncate,
        .writev      = dht_writev,
        .xattrop     = dht_xattrop,
        .fxattrop    = dht_fxattrop,
        .setattr     = dht_setattr,
        .fsetattr    = dht_fsetattr,
        .fallocate   = dht_fallocate,
        .discard     = dht_discard,
        .zerofill    = dht_zerofill,
};

struct xlator_cbks cbks = {
        .release    = dht_release,
        .forget     = dht_forget
};
