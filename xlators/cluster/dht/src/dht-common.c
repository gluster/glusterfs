/*
  Copyright (c) 2008-2012 Red Hat, Inc. <http://www.redhat.com>
  This file is part of GlusterFS.

  This file is licensed to you under your choice of the GNU Lesser
  General Public License, version 3 or any later version (LGPLv3 or
  later), or the GNU General Public License, version 2 (GPLv2), in all
  cases as published by the Free Software Foundation.
*/

/* TODO: add NS locking */

#include "libxlator.h"
#include "dht-common.h"
#include "dht-lock.h"
#include <glusterfs/byte-order.h>
#include <glusterfs/quota-common-utils.h>
#include <glusterfs/upcall-utils.h>
#include "glusterfs/compat-errno.h"  // for ENODATA on BSD
#include <glusterfs/common-utils.h>

#include <sys/time.h>
#include <libgen.h>
#include <signal.h>

static int
dht_rmdir_readdirp_cbk(call_frame_t *frame, void *cookie, xlator_t *this,
                       int op_ret, int op_errno, gf_dirent_t *entries,
                       dict_t *xdata);

static int
dht_link2(xlator_t *this, xlator_t *subvol, call_frame_t *frame, int ret);

static int
dht_set_dir_xattr_req(xlator_t *this, loc_t *loc, dict_t *xattr_req);

static int
dht_lookup_everywhere_done(call_frame_t *frame, xlator_t *this);

static int
dht_common_mark_mdsxattr_cbk(call_frame_t *frame, void *cookie, xlator_t *this,
                             int op_ret, int op_errno, dict_t *xdata);

static int
dht_rmdir_unlock(call_frame_t *frame, xlator_t *this);

static const char *dht_dbg_vxattrs[] = {DHT_DBG_HASHED_SUBVOL_PATTERN, NULL};

/* Check the xdata to make sure EBADF has been set by client xlator */
int32_t
dht_check_remote_fd_failed_error(dht_local_t *local, int op_ret, int op_errno)
{
    if (op_ret == -1 && (op_errno == EBADF || op_errno == EBADFD) &&
        !(local->fd_checked)) {
        return 1;
    }
    return 0;
}

/* Sets the blocks and size values to fixed values. This is to be called
 * only for dirs. The caller is responsible for checking the type
 */
int32_t
dht_set_fixed_dir_stat(struct iatt *stat)
{
    if (stat) {
        stat->ia_blocks = DHT_DIR_STAT_BLOCKS;
        stat->ia_size = DHT_DIR_STAT_SIZE;
        return 0;
    }
    return -1;
}

/* Return true if key exists in array
 */
static gf_boolean_t
dht_match_xattr(const char *key)
{
    char **xattrs_to_heal = get_xattrs_to_heal();

    return gf_get_index_by_elem(xattrs_to_heal, (char *)key) >= 0;
}

static int
dht_aggregate_quota_xattr(dict_t *dst, char *key, data_t *value)
{
    int ret = -1;
    quota_meta_t *meta_dst = NULL;
    quota_meta_t *meta_src = NULL;
    int64_t *size = NULL;
    int64_t dst_dir_count = 0;
    int64_t src_dir_count = 0;

    if (value == NULL) {
        gf_msg("dht", GF_LOG_WARNING, 0, DHT_MSG_DATA_NULL,
               "data value is NULL");
        ret = -1;
        goto out;
    }

    ret = dict_get_bin(dst, key, (void **)&meta_dst);
    if (ret < 0) {
        meta_dst = GF_CALLOC(1, sizeof(quota_meta_t), gf_common_quota_meta_t);
        if (meta_dst == NULL) {
            gf_msg("dht", GF_LOG_WARNING, ENOMEM, DHT_MSG_NO_MEMORY,
                   "Memory allocation failed");
            ret = -1;
            goto out;
        }
        ret = dict_set_bin(dst, key, meta_dst, sizeof(quota_meta_t));
        if (ret < 0) {
            gf_msg("dht", GF_LOG_WARNING, EINVAL, DHT_MSG_DICT_SET_FAILED,
                   "dht aggregate dict set failed");
            GF_FREE(meta_dst);
            ret = -1;
            goto out;
        }
    }

    if (value->len > sizeof(int64_t)) {
        meta_src = data_to_bin(value);

        meta_dst->size = hton64(ntoh64(meta_dst->size) +
                                ntoh64(meta_src->size));
        meta_dst->file_count = hton64(ntoh64(meta_dst->file_count) +
                                      ntoh64(meta_src->file_count));

        if (value->len > (2 * sizeof(int64_t))) {
            dst_dir_count = ntoh64(meta_dst->dir_count);
            src_dir_count = ntoh64(meta_src->dir_count);

            if (src_dir_count > dst_dir_count)
                meta_dst->dir_count = meta_src->dir_count;
        } else {
            meta_dst->dir_count = 0;
        }
    } else {
        size = data_to_bin(value);
        meta_dst->size = hton64(ntoh64(meta_dst->size) + ntoh64(*size));
    }

    ret = 0;
out:
    return ret;
}

static int
add_opt(char **optsp, const char *opt)
{
    char *newopts = NULL;
    unsigned oldsize = 0;
    unsigned newsize = 0;

    if (*optsp == NULL)
        newopts = gf_strdup(opt);
    else {
        oldsize = strlen(*optsp);
        newsize = oldsize + 1 + strlen(opt) + 1;
        newopts = GF_REALLOC(*optsp, newsize);
        if (newopts)
            sprintf(newopts + oldsize, ",%s", opt);
    }
    if (newopts == NULL) {
        gf_msg("dht", GF_LOG_WARNING, 0, DHT_MSG_NO_MEMORY,
               "Error to add choices in buffer in add_opt");
        return -1;
    }
    *optsp = newopts;
    return 0;
}

/* Return Choice list from Split brain status */
static char *
getChoices(const char *value)
{
    int i = 0;
    char *ptr = NULL;
    char *tok = NULL;
    char *result = NULL;
    char *newval = NULL;

    ptr = strstr(value, "Choices:");
    if (!ptr) {
        result = ptr;
        goto out;
    }

    newval = gf_strdup(ptr);
    if (!newval) {
        result = newval;
        goto out;
    }

    tok = strtok(newval, ":");
    if (!tok) {
        result = tok;
        goto out;
    }

    while (tok) {
        i++;
        if (i == 2)
            break;
        tok = strtok(NULL, ":");
    }

    result = gf_strdup(tok);

out:
    if (newval)
        GF_FREE(newval);

    return result;
}

/* This function prepare a list of choices for key
   (replica.split-brain-status) in   case of metadata split brain
   only on the basis of key-value passed to this function.
   After prepare the list of choices it update the same key in dict
   with this value to reflect the same in
   replica.split-brain-status attr for file.

*/

static int
dht_aggregate_split_brain_xattr(dict_t *dst, char *key, data_t *value)
{
    int ret = 0;
    char *oldvalue = NULL;
    char *old_choice = NULL;
    char *new_choice = NULL;
    char *full_choice = NULL;
    char *status = NULL;

    if (value == NULL) {
        gf_msg("dht", GF_LOG_WARNING, 0, DHT_MSG_DATA_NULL,
               "GF_AFR_SBRAIN_STATUS value is NULL");
        ret = -1;
        goto out;
    }

    ret = dict_get_str(dst, key, &oldvalue);
    if (ret)
        goto out;

    /* skip code that is irrelevant if !oldvalue */
    if (!oldvalue)
        goto out;

    if (strstr(oldvalue, "not")) {
        gf_msg_debug("dht", 0, "Need to update split-brain status in dict");
        ret = -1;
        goto out;
    }
    if (strstr(oldvalue, "metadata-split-brain:yes") &&
        (strstr(oldvalue, "data-split-brain:no"))) {
        if (strstr(value->data, "not")) {
            gf_msg_debug("dht", 0, "No need to update split-brain status");
            ret = 0;
            goto out;
        }
        if (strstr(value->data, "yes") &&
            (strncmp(oldvalue, value->data, strlen(oldvalue)))) {
            old_choice = getChoices(oldvalue);
            if (!old_choice) {
                gf_msg("dht", GF_LOG_WARNING, 0, DHT_MSG_NO_MEMORY,
                       "Error to get choices");
                ret = -1;
                goto out;
            }

            ret = add_opt(&full_choice, old_choice);
            if (ret) {
                gf_msg("dht", GF_LOG_WARNING, 0, DHT_MSG_NO_MEMORY,
                       "Error to add choices");
                ret = -1;
                goto out;
            }

            new_choice = getChoices(value->data);
            if (!new_choice) {
                gf_msg("dht", GF_LOG_WARNING, 0, DHT_MSG_NO_MEMORY,
                       "Error to get choices");
                ret = -1;
                goto out;
            }

            ret = add_opt(&full_choice, new_choice);
            if (ret) {
                gf_msg("dht", GF_LOG_WARNING, 0, DHT_MSG_NO_MEMORY,
                       "Error to add choices ");
                ret = -1;
                goto out;
            }
            ret = gf_asprintf(&status,
                              "data-split-brain:%s    "
                              "metadata-split-brain:%s   Choices:%s",
                              "no", "yes", full_choice);

            if (-1 == ret) {
                gf_msg("dht", GF_LOG_WARNING, 0, DHT_MSG_NO_MEMORY,
                       "Error to prepare status ");
                goto out;
            }
            ret = dict_set_dynstr(dst, key, status);
            if (ret) {
                gf_msg("dht", GF_LOG_WARNING, 0, DHT_MSG_DICT_SET_FAILED,
                       "Failed to set full choice");
            }
        }
    }

out:
    if (old_choice)
        GF_FREE(old_choice);
    if (new_choice)
        GF_FREE(new_choice);
    if (full_choice)
        GF_FREE(full_choice);

    return ret;
}

static int
dht_aggregate(dict_t *this, char *key, data_t *value, void *data)
{
    dict_t *dst = NULL;
    int32_t ret = -1;
    data_t *dict_data = NULL;

    dst = data;

    /* compare split brain xattr only */
    if (strcmp(key, GF_AFR_SBRAIN_STATUS) == 0) {
        ret = dht_aggregate_split_brain_xattr(dst, key, value);
        if (!ret)
            goto out;
    } else if (strcmp(key, QUOTA_SIZE_KEY) == 0) {
        ret = dht_aggregate_quota_xattr(dst, key, value);
        if (ret) {
            gf_msg("dht", GF_LOG_WARNING, 0,
                   DHT_MSG_AGGREGATE_QUOTA_XATTR_FAILED,
                   "Failed to aggregate quota xattr");
        }
        goto out;
    } else if (fnmatch(GF_XATTR_STIME_PATTERN, key, FNM_NOESCAPE) == 0) {
        ret = gf_get_min_stime(THIS, dst, key, value);
        goto out;
    } else {
        /* compare user xattrs only */
        if (!strncmp(key, "user.", SLEN("user."))) {
            ret = dict_lookup(dst, key, &dict_data);
            if (!ret && dict_data && value) {
                ret = is_data_equal(dict_data, value);
                if (!ret)
                    gf_msg_debug("dht", 0, "xattr mismatch for %s", key);
            }
        }
    }

    ret = dict_set(dst, key, value);
    if (ret) {
        gf_msg("dht", GF_LOG_WARNING, 0, DHT_MSG_DICT_SET_FAILED,
               "Failed to set dictionary value: key = %s", key);
    }

out:
    return ret;
}

static void
dht_aggregate_xattr(dict_t *dst, dict_t *src)
{
    if ((dst == NULL) || (src == NULL)) {
        goto out;
    }

    dict_foreach(src, dht_aggregate, dst);
out:
    return;
}

/* Code to save hashed subvol on inode ctx as a mds subvol
 */
int
dht_inode_ctx_mdsvol_set(inode_t *inode, xlator_t *this, xlator_t *mds_subvol)
{
    dht_inode_ctx_t *ctx = NULL;
    int ret = -1;
    uint64_t ctx_int = 0;
    gf_boolean_t ctx_free = _gf_false;

    LOCK(&inode->lock);
    {
        ret = __inode_ctx_get(inode, this, &ctx_int);
        if (ctx_int) {
            ctx = (dht_inode_ctx_t *)(uintptr_t)ctx_int;
            ctx->mds_subvol = mds_subvol;
        } else {
            ctx = GF_CALLOC(1, sizeof(*ctx), gf_dht_mt_inode_ctx_t);
            if (!ctx)
                goto unlock;
            ctx->mds_subvol = mds_subvol;
            ctx_free = _gf_true;
            ctx_int = (long)ctx;
            ret = __inode_ctx_set(inode, this, &ctx_int);
        }
    }
unlock:
    UNLOCK(&inode->lock);
    if (ret && ctx_free)
        GF_FREE(ctx);
    return ret;
}

/*Code to get mds subvol from inode ctx */

int
dht_inode_ctx_mdsvol_get(inode_t *inode, xlator_t *this, xlator_t **mdsvol)
{
    dht_inode_ctx_t *ctx = NULL;
    int ret = -1;

    if (!mdsvol)
        return ret;

    if (__is_root_gfid(inode->gfid)) {
        (*mdsvol) = FIRST_CHILD(this);
        return 0;
    }

    ret = dht_inode_ctx_get(inode, this, &ctx);

    if (!ret && ctx) {
        if (ctx->mds_subvol) {
            *mdsvol = ctx->mds_subvol;
            ret = 0;
        } else {
            ret = -1;
        }
    }

    return ret;
}

/* TODO:
   - use volumename in xattr instead of "dht"
   - use NS locks
   - handle all cases in self heal layout reconstruction
   - complete linkfile selfheal
*/

static int
dht_lookup_selfheal_cbk(call_frame_t *frame, void *cookie, xlator_t *this,
                        int op_ret, int op_errno, dict_t *xdata)
{
    dht_local_t *local = NULL;
    dht_layout_t *layout = NULL;
    dht_conf_t *conf = NULL;
    int ret = -1;

    GF_VALIDATE_OR_GOTO("dht", frame, out);
    GF_VALIDATE_OR_GOTO("dht", this, out);
    GF_VALIDATE_OR_GOTO("dht", frame->local, out);

    local = frame->local;
    conf = this->private;
    ret = op_ret;

    FRAME_SU_UNDO(frame, dht_local_t);

    if (ret == 0) {
        layout = local->selfheal.layout;
        ret = dht_layout_set(this, local->inode, layout);
    }

    dht_inode_ctx_time_update(local->inode, this, &local->stbuf, 1);
    if (local->loc.parent) {
        dht_inode_ctx_time_update(local->loc.parent, this, &local->postparent,
                                  1);
    }

    DHT_STRIP_PHASE1_FLAGS(&local->stbuf);
    dht_set_fixed_dir_stat(&local->postparent);
    /* Delete mds xattr at the time of STACK UNWIND */
    GF_REMOVE_INTERNAL_XATTR(conf->mds_xattr_key, local->xattr);

    DHT_STACK_UNWIND(lookup, frame, ret, local->op_errno, local->inode,
                     &local->stbuf, local->xattr, &local->postparent);

out:
    return ret;
}

static int
dht_discover_complete(xlator_t *this, call_frame_t *discover_frame)
{
    dht_local_t *local = NULL;
    dht_local_t *heal_local = NULL;
    call_frame_t *main_frame = NULL;
    call_frame_t *heal_frame = NULL;
    int op_errno = 0;
    int ret = -1;
    dht_layout_t *layout = NULL;
    dht_conf_t *conf = NULL;
    uint32_t vol_commit_hash = 0;
    xlator_t *source = NULL;
    int heal_path = 0;
    int error_while_marking_mds = 0;
    int i = 0;
    loc_t loc = {0};
    int8_t is_read_only = 0, layout_anomalies = 0;
    char gfid_local[GF_UUID_BUF_SIZE] = {0};

    local = discover_frame->local;
    layout = local->layout;
    conf = this->private;
    gf_uuid_unparse(local->gfid, gfid_local);

    LOCK(&discover_frame->lock);
    {
        main_frame = local->main_frame;
        local->main_frame = NULL;
    }
    UNLOCK(&discover_frame->lock);

    if (!main_frame)
        return 0;

    /* Code to update all extended attributed from
       subvol to local->xattr on that internal xattr has found
    */
    if (conf->subvolume_cnt == 1)
        local->need_xattr_heal = 0;
    if (local->need_xattr_heal && (local->mds_xattr)) {
        dht_dir_set_heal_xattr(this, local, local->xattr, local->mds_xattr,
                               NULL, NULL);
        dict_unref(local->mds_xattr);
        local->mds_xattr = NULL;
    }

    ret = dict_get_int8(local->xattr_req, QUOTA_READ_ONLY_KEY, &is_read_only);
    if (ret < 0)
        gf_msg_debug(this->name, 0, "key = %s not present in dict",
                     QUOTA_READ_ONLY_KEY);

    if (local->file_count && local->dir_count) {
        gf_msg(this->name, GF_LOG_ERROR, 0, DHT_MSG_FILE_TYPE_MISMATCH,
               "path %s exists as a file on one subvolume "
               "and directory on another. "
               "Please fix it manually",
               local->loc.path);
        op_errno = EIO;
        goto out;
    }

    if (local->cached_subvol) {
        ret = dht_layout_preset(this, local->cached_subvol, local->inode);
        if (ret < 0) {
            gf_msg(this->name, GF_LOG_WARNING, 0, DHT_MSG_LAYOUT_SET_FAILED,
                   "failed to set layout for subvolume %s",
                   local->cached_subvol ? local->cached_subvol->name : "<nil>");
            op_errno = EINVAL;
            goto out;
        }
    } else {
        ret = dht_layout_normalize(this, &local->loc, layout);
        if ((ret < 0) || ((ret > 0) && (local->op_ret != 0))) {
            /* either the layout is incorrect or the directory is
             * not found even in one subvolume.
             */
            gf_msg_debug(this->name, 0,
                         "normalizing failed on %s "
                         "(overlaps/holes present: %s, "
                         "ENOENT errors: %d)",
                         local->loc.path, (ret < 0) ? "yes" : "no",
                         (ret > 0) ? ret : 0);
            layout_anomalies = 1;
        } else if (local->inode) {
            dht_layout_set(this, local->inode, layout);
        }
    }

    if (!conf->vch_forced) {
        ret = dict_get_uint32(local->xattr, conf->commithash_xattr_name,
                              &vol_commit_hash);
        if (ret == 0) {
            conf->vol_commit_hash = vol_commit_hash;
        }
    }

    if (IA_ISDIR(local->stbuf.ia_type) && !is_read_only) {
        for (i = 0; i < layout->cnt; i++) {
            if (!source && !layout->list[i].err)
                source = layout->list[i].xlator;
            if (layout->list[i].err == ENOENT ||
                layout->list[i].err == ESTALE) {
                heal_path = 1;
            }

            if (source && heal_path)
                break;
        }
    }

    if (IA_ISDIR(local->stbuf.ia_type)) {
        /* Call function to save hashed subvol on inode ctx if
           internal mds xattr is not present and all subvols are up
        */
        if (!local->op_ret && !__is_root_gfid(local->stbuf.ia_gfid))
            (void)dht_common_mark_mdsxattr(discover_frame,
                                           &error_while_marking_mds, 1);

        if (local->need_xattr_heal && !heal_path) {
            local->need_xattr_heal = 0;
            ret = dht_dir_xattr_heal(this, local, &op_errno);
            if (ret) {
                gf_msg(this->name, GF_LOG_ERROR, op_errno,
                       DHT_MSG_DIR_XATTR_HEAL_FAILED,
                       "xattr heal failed for "
                       "directory  gfid is %s ",
                       gfid_local);
            }
        }
    }

    if (source && (heal_path || layout_anomalies || error_while_marking_mds)) {
        gf_uuid_copy(loc.gfid, local->gfid);
        if (gf_uuid_is_null(loc.gfid)) {
            goto done;
        }

        if (local->inode)
            loc.inode = inode_ref(local->inode);
        else
            goto done;

        heal_frame = create_frame(this, this->ctx->pool);
        if (heal_frame) {
            heal_local = dht_local_init(heal_frame, &loc, NULL, 0);
            if (!heal_local)
                goto cleanup;

            gf_uuid_copy(heal_local->gfid, local->gfid);
            heal_frame->cookie = source;
            heal_local->xattr = dict_ref(local->xattr);
            heal_local->stbuf = local->stbuf;
            heal_local->postparent = local->postparent;
            heal_local->inode = inode_ref(loc.inode);
            heal_local->main_frame = main_frame;
            FRAME_SU_DO(heal_frame, dht_local_t);
            ret = synctask_new(this->ctx->env, dht_heal_full_path,
                               dht_heal_full_path_done, heal_frame, heal_frame);
            if (!ret) {
                loc_wipe(&loc);
                return 0;
            }
            /*
             * Failed to spawn the synctask. Returning
             * with out doing heal.
             */
        cleanup:
            loc_wipe(&loc);
            DHT_STACK_DESTROY(heal_frame);
        }
    }
done:
    dht_set_fixed_dir_stat(&local->postparent);
    /* Delete mds xattr at the time of STACK UNWIND */
    if (local->xattr)
        GF_REMOVE_INTERNAL_XATTR(conf->mds_xattr_key, local->xattr);

    DHT_STACK_UNWIND(lookup, main_frame, local->op_ret, local->op_errno,
                     local->inode, &local->stbuf, local->xattr,
                     &local->postparent);
    return 0;

out:
    DHT_STACK_UNWIND(lookup, main_frame, -1, op_errno, NULL, NULL, NULL, NULL);

    return ret;
}

static int
dht_common_mark_mdsxattr_cbk(call_frame_t *frame, void *cookie, xlator_t *this,
                             int op_ret, int op_errno, dict_t *xdata)
{
    dht_local_t *local = NULL;
    xlator_t *prev = cookie;
    int ret = -1;
    dht_conf_t *conf = 0;
    dht_layout_t *layout = NULL;
    int32_t mds_heal_fresh_lookup = 0;

    GF_VALIDATE_OR_GOTO(this->name, frame, out);
    GF_VALIDATE_OR_GOTO(this->name, frame->local, out);

    local = frame->local;
    conf = this->private;
    layout = local->selfheal.layout;
    mds_heal_fresh_lookup = local->mds_heal_fresh_lookup;

    if (op_ret) {
        gf_msg_debug(this->name, op_ret,
                     "Failed to set %s on the MDS %s for path %s. ",
                     conf->mds_xattr_key, prev->name, local->loc.path);
    } else {
        /* Save mds subvol on inode ctx */
        ret = dht_inode_ctx_mdsvol_set(local->inode, this, prev);
        if (ret) {
            gf_msg(this->name, GF_LOG_ERROR, 0, DHT_MSG_SET_INODE_CTX_FAILED,
                   "Failed to set mds subvol on inode ctx"
                   " %s for %s ",
                   prev->name, local->loc.path);
        }
    }
    if (!local->mds_heal_fresh_lookup && layout) {
        dht_selfheal_dir_setattr(frame, &local->loc, &local->stbuf, 0xffffffff,
                                 layout);
    }
out:
    if (mds_heal_fresh_lookup)
        DHT_STACK_DESTROY(frame);
    return 0;
}

static xlator_t *
dht_inode_get_hashed_subvol(inode_t *inode, xlator_t *this, loc_t *loc)
{
    char *path = NULL;
    loc_t populate_loc = {
        0,
    };
    char *name = NULL;
    xlator_t *hash_subvol = NULL;

    if (!inode)
        return hash_subvol;

    if (loc && loc->parent && loc->path) {
        if (!loc->name) {
            name = strrchr(loc->path, '/');
            if (name) {
                loc->name = name + 1;
            } else {
                goto out;
            }
        }
        hash_subvol = dht_subvol_get_hashed(this, loc);
        goto out;
    }

    if (!gf_uuid_is_null(inode->gfid)) {
        populate_loc.inode = inode_ref(inode);
        populate_loc.parent = inode_parent(populate_loc.inode, NULL, NULL);
        inode_path(populate_loc.inode, NULL, &path);

        if (!path)
            goto out;

        populate_loc.path = path;
        if (!populate_loc.name && populate_loc.path) {
            name = strrchr(populate_loc.path, '/');
            if (name) {
                populate_loc.name = name + 1;

            } else {
                goto out;
            }
        }
        hash_subvol = dht_subvol_get_hashed(this, &populate_loc);
    }
out:
    if (populate_loc.inode)
        loc_wipe(&populate_loc);
    return hash_subvol;
}

/* Common function call by revalidate/selfheal code path to populate
   internal xattr if it is not present, mark_during_fresh_lookup value
   determines either function is call by revalidate_cbk(discover_complete)
   or call by selfheal code path while fresh lookup.
   Here we do wind a call serially in case of fresh lookup and
   for other lookup code path we do wind a call parallel.The reason
   to wind a call serially is at the time of fresh lookup directory is not
   discovered and at the time of revalidate_lookup directory is
   already discovered. So, revalidate codepath can race with setxattr
   codepath and can get into spurious heals because of an ongoing setxattr.
   This can slow down revalidates, if healing happens in foreground.
   However, if healing happens in background, there is no direct performance
   penalty.
*/
int
dht_common_mark_mdsxattr(call_frame_t *frame, int *errst,
                         int mark_during_fresh_lookup)
{
    dht_local_t *local = NULL;
    xlator_t *this = NULL;
    xlator_t *hashed_subvol = NULL;
    int ret = 0;
    int i = 0;
    dict_t *xattrs = NULL;
    char gfid_local[GF_UUID_BUF_SIZE] = {
        0,
    };
    int32_t zero[1] = {0};
    dht_conf_t *conf = 0;
    dht_layout_t *layout = NULL;
    dht_local_t *copy_local = NULL;
    call_frame_t *xattr_frame = NULL;
    gf_boolean_t vol_down = _gf_false;

    GF_VALIDATE_OR_GOTO("dht", frame, out);
    this = frame->this;
    GF_VALIDATE_OR_GOTO("dht", this, out);
    GF_VALIDATE_OR_GOTO(this->name, frame->local, out);
    GF_VALIDATE_OR_GOTO(this->name, this->private, out);

    local = frame->local;
    conf = this->private;
    layout = local->selfheal.layout;
    local->mds_heal_fresh_lookup = mark_during_fresh_lookup;

    gf_uuid_unparse(local->gfid, gfid_local);

    /* Code to update hashed subvol consider as a mds subvol
       and wind a setxattr call on hashed subvol to update
       internal xattr
    */
    if (!local->xattr || !dict_get(local->xattr, conf->mds_xattr_key)) {
        /* It means no internal MDS xattr has been set yet
         */
        /* Check the status of all subvol are up while call
           this function call by lookup code path
        */
        if (mark_during_fresh_lookup) {
            for (i = 0; i < conf->subvolume_cnt; i++) {
                if (!conf->subvolume_status[i]) {
                    vol_down = _gf_true;
                    break;
                }
            }
            if (vol_down) {
                gf_msg_debug(this->name, 0,
                             "subvol %s is down. Unable to "
                             " save mds subvol on inode for "
                             " path %s gfid is %s ",
                             conf->subvolumes[i]->name, local->loc.path,
                             gfid_local);
                goto out;
            }
        }

        /* Calculate hashed subvol based on inode and parent node
         */
        hashed_subvol = dht_inode_get_hashed_subvol(local->inode, this,
                                                    &local->loc);
        if (!hashed_subvol) {
            gf_msg(this->name, GF_LOG_DEBUG, 0,
                   DHT_MSG_HASHED_SUBVOL_GET_FAILED,
                   "Failed to get hashed subvol for path %s"
                   "gfid is %s ",
                   local->loc.path, gfid_local);
            if (errst)
                (*errst) = 1;
            ret = -1;
            goto out;
        }
        xattrs = dict_new();
        if (!xattrs) {
            gf_msg(this->name, GF_LOG_ERROR, ENOMEM, DHT_MSG_NO_MEMORY,
                   "dict_new failed");
            ret = -1;
            goto out;
        }
        /* Add internal MDS xattr on disk for hashed subvol
         */
        ret = dht_dict_set_array(xattrs, conf->mds_xattr_key, zero, 1);
        if (ret) {
            gf_msg(this->name, GF_LOG_WARNING, ENOMEM, DHT_MSG_DICT_SET_FAILED,
                   "Failed to set dictionary"
                   "  value:key = %s for "
                   "path %s",
                   conf->mds_xattr_key, local->loc.path);
            ret = -1;
            goto out;
        }
        /* Create a new frame to wind a call only while
           this function call by revalidate_cbk code path
           To wind a call parallel need to create a new frame
        */
        if (mark_during_fresh_lookup) {
            xattr_frame = create_frame(this, this->ctx->pool);
            if (!xattr_frame) {
                ret = -1;
                goto out;
            }
            copy_local = dht_local_init(xattr_frame, &(local->loc), NULL, 0);
            if (!copy_local) {
                ret = -1;
                DHT_STACK_DESTROY(xattr_frame);
                goto out;
            }
            copy_local->stbuf = local->stbuf;
            copy_local->mds_heal_fresh_lookup = mark_during_fresh_lookup;
            if (!copy_local->inode)
                copy_local->inode = inode_ref(local->inode);
            gf_uuid_copy(copy_local->loc.gfid, local->gfid);
            FRAME_SU_DO(xattr_frame, dht_local_t);
            STACK_WIND_COOKIE(xattr_frame, dht_common_mark_mdsxattr_cbk,
                              hashed_subvol, hashed_subvol,
                              hashed_subvol->fops->setxattr, &local->loc,
                              xattrs, 0, NULL);
        } else {
            STACK_WIND_COOKIE(frame, dht_common_mark_mdsxattr_cbk,
                              (void *)hashed_subvol, hashed_subvol,
                              hashed_subvol->fops->setxattr, &local->loc,
                              xattrs, 0, NULL);
        }
    } else {
        gf_msg_debug(this->name, 0,
                     "internal xattr %s is present on subvol"
                     "on path %s gfid is %s ",
                     conf->mds_xattr_key, local->loc.path, gfid_local);
        if (!mark_during_fresh_lookup)
            dht_selfheal_dir_setattr(frame, &local->loc, &local->stbuf,
                                     0xffffffff, layout);
    }

out:
    if (xattrs)
        dict_unref(xattrs);
    return ret;
}

/* Get the value of key from dict in the bytewise and save in array after
   convert from network byte order to host byte order
*/
static int32_t
dht_dict_get_array(dict_t *dict, char *key, int32_t value[], int32_t size,
                   int *errst)
{
    void *ptr = NULL;
    int32_t len = -1;
    int32_t vindex = -1;
    int32_t err = -1;
    int ret = 0;

    if (dict == NULL) {
        (*errst) = -1;
        return -EINVAL;
    }
    err = dict_get_ptr_and_len(dict, key, &ptr, &len);
    if (err != 0) {
        (*errst) = -1;
        return err;
    }

    if (len != (size * sizeof(int32_t))) {
        (*errst) = -1;
        return -EINVAL;
    }

    for (vindex = 0; vindex < size; vindex++) {
        value[vindex] = ntoh32(*((int32_t *)ptr + vindex));
        if (value[vindex] < 0)
            ret = -1;
    }

    return ret;
}

static int
dht_discover_cbk(call_frame_t *frame, void *cookie, xlator_t *this, int op_ret,
                 int op_errno, inode_t *inode, struct iatt *stbuf,
                 dict_t *xattr, struct iatt *postparent)
{
    dht_local_t *local = NULL;
    int this_call_cnt = 0;
    xlator_t *prev = NULL;
    dht_layout_t *layout = NULL;
    int ret = -1;
    int is_dir = 0;
    int32_t check_mds = 0;
    int is_linkfile = 0;
    int attempt_unwind = 0;
    dht_conf_t *conf = 0;
    char gfid_local[GF_UUID_BUF_SIZE] = {0};
    char gfid_node[GF_UUID_BUF_SIZE] = {0};
    int32_t mds_xattr_val[1] = {0};
    int errst = 0;

    GF_VALIDATE_OR_GOTO("dht", frame, out);
    GF_VALIDATE_OR_GOTO("dht", this, out);
    GF_VALIDATE_OR_GOTO("dht", frame->local, out);
    GF_VALIDATE_OR_GOTO("dht", this->private, out);
    GF_VALIDATE_OR_GOTO("dht", cookie, out);

    local = frame->local;
    prev = cookie;
    conf = this->private;

    layout = local->layout;

    /* Check if the gfid is different for file from other node */
    if (!op_ret && gf_uuid_compare(local->gfid, stbuf->ia_gfid)) {
        gf_uuid_unparse(stbuf->ia_gfid, gfid_node);
        gf_uuid_unparse(local->gfid, gfid_local);

        gf_msg(this->name, GF_LOG_WARNING, 0, DHT_MSG_GFID_MISMATCH,
               "%s: gfid different on %s, gfid local = %s"
               "gfid other = %s",
               local->loc.path, prev->name, gfid_local, gfid_node);
    }

    LOCK(&frame->lock);
    {
        /* TODO: assert equal mode on stbuf->st_mode and
           local->stbuf->st_mode

           else mkdir/chmod/chown and fix
        */

        ret = dht_layout_merge(this, layout, prev, op_ret, op_errno, xattr);
        if (ret)
            gf_msg(this->name, GF_LOG_WARNING, 0, DHT_MSG_LAYOUT_MERGE_FAILED,
                   "%s: failed to merge layouts for subvol %s", local->loc.path,
                   prev->name);

        if (op_ret == -1) {
            local->op_errno = op_errno;
            gf_msg_debug(this->name, op_errno,
                         "lookup of %s on %s returned error", local->loc.path,
                         prev->name);

            goto unlock;
        }

        is_linkfile = check_is_linkfile(inode, stbuf, xattr,
                                        conf->link_xattr_name);
        is_dir = check_is_dir(inode, stbuf, xattr);

        if (is_dir) {
            local->dir_count++;
        } else {
            local->file_count++;

            if (!is_linkfile && !local->cached_subvol) {
                /* real file */
                /* Ok, we somehow managed to find a file on
                 * more than one subvol. ignore this or we
                 * will end up overwriting information while a
                 * a thread is potentially unwinding from
                 * dht_discover_complete
                 */
                local->cached_subvol = prev;
                attempt_unwind = 1;
            } else {
                goto unlock;
            }
        }

        local->op_ret = 0;

        if (local->xattr == NULL) {
            local->xattr = dict_ref(xattr);
        } else {
            /* Don't aggregate for files. See BZ#1484709 */
            if (is_dir)
                dht_aggregate_xattr(local->xattr, xattr);
        }

        if (local->inode == NULL)
            local->inode = inode_ref(inode);

        dht_iatt_merge(this, &local->stbuf, stbuf);
        dht_iatt_merge(this, &local->postparent, postparent);

        if (!dict_get(xattr, conf->mds_xattr_key)) {
            goto unlock;
        } else {
            gf_msg_debug(this->name, 0,
                         "internal xattr %s is present on subvol"
                         "on path %s gfid is %s ",
                         conf->mds_xattr_key, local->loc.path, gfid_local);
        }
        check_mds = dht_dict_get_array(xattr, conf->mds_xattr_key,
                                       mds_xattr_val, 1, &errst);
        /* save mds subvol on inode ctx */
        ret = dht_inode_ctx_mdsvol_set(local->inode, this, prev);
        if (ret) {
            gf_msg(this->name, GF_LOG_ERROR, 0, DHT_MSG_SET_INODE_CTX_FAILED,
                   "Failed to set hashed subvol for %s vol is %s",
                   local->loc.path, prev->name);
        }

        if ((check_mds < 0) && !errst) {
            local->mds_xattr = dict_ref(xattr);
            gf_msg_debug(this->name, 0,
                         "Value of %s is not zero on mds subvol"
                         "so xattr needs to be healed on non mds"
                         " path is %s and vol name is %s "
                         " gfid is %s",
                         conf->mds_xattr_key, local->loc.path, prev->name,
                         gfid_local);
            local->need_xattr_heal = 1;
            local->mds_subvol = prev;
        }
    }
unlock:
    UNLOCK(&frame->lock);
out:
    /* Make sure, the thread executing dht_discover_complete is the one
     * which calls STACK_DESTROY (frame). In the case of "attempt_unwind",
     * this makes sure that the thread don't call dht_frame_return, till
     * call to dht_discover_complete is done.
     */
    if (attempt_unwind) {
        dht_discover_complete(this, frame);
    }

    this_call_cnt = dht_frame_return(frame);

    if (is_last_call(this_call_cnt) && !attempt_unwind) {
        dht_discover_complete(this, frame);
    }

    if (is_last_call(this_call_cnt))
        DHT_STACK_DESTROY(frame);

    return 0;
}

static int
dht_set_file_xattr_req(xlator_t *this, loc_t *loc, dict_t *xattr_req)
{
    int ret = -EINVAL;
    dht_conf_t *conf = NULL;

    conf = this->private;
    if (!conf) {
        goto err;
    }

    if (!xattr_req) {
        goto err;
    }

    /* Used to check whether this is a linkto file.
     */
    ret = dict_set_uint32(xattr_req, conf->link_xattr_name, 256);
    if (ret < 0) {
        gf_msg(this->name, GF_LOG_WARNING, ENOMEM, DHT_MSG_DICT_SET_FAILED,
               "Failed to set dictionary value:key = %s for "
               "path %s",
               conf->link_xattr_name, loc->path);
        goto err;
    }

    /* This is used to make sure we don't unlink linkto files
     * which are the target of an ongoing file migration.
     */
    ret = dict_set_uint32(xattr_req, GLUSTERFS_OPEN_FD_COUNT, 4);
    if (ret) {
        gf_msg(this->name, GF_LOG_WARNING, ENOMEM, DHT_MSG_DICT_SET_FAILED,
               "Failed to set dictionary value:key = %s for "
               "path %s",
               GLUSTERFS_OPEN_FD_COUNT, loc->path);
        goto err;
    }

    ret = 0;
err:
    return ret;
}

/* This is a gfid based nameless lookup. Without a name, the hashed subvol
 * cannot be calculated so a lookup is sent to all subvols.
 */
static int
dht_do_discover(call_frame_t *frame, xlator_t *this, loc_t *loc)
{
    int ret;
    dht_local_t *local = NULL;
    dht_conf_t *conf = NULL;
    int call_cnt = 0;
    int op_errno = EINVAL;
    int i = 0;
    call_frame_t *discover_frame = NULL;

    conf = this->private;
    local = frame->local;

    /* As we do not know if this is a file or directory, request
     * both file and directory xattrs
     */
    ret = dht_set_file_xattr_req(this, loc, local->xattr_req);
    if (ret) {
        goto err;
    }

    ret = dht_set_dir_xattr_req(this, loc, local->xattr_req);
    if (ret) {
        goto err;
    }

    if (loc_is_root(loc)) {
        /* Request the DHT commit hash xattr (trusted.glusterfs.dht.commithash)
         * set on the brick root.
         */
        ret = dict_set_uint32(local->xattr_req, conf->commithash_xattr_name,
                              sizeof(uint32_t));
    }

    call_cnt = conf->subvolume_cnt;
    local->call_cnt = call_cnt;

    local->layout = dht_layout_new(this, conf->subvolume_cnt);

    if (!local->layout) {
        op_errno = ENOMEM;
        goto err;
    }

    gf_uuid_copy(local->gfid, loc->gfid);

    discover_frame = copy_frame(frame);
    if (!discover_frame) {
        op_errno = ENOMEM;
        goto err;
    }

    discover_frame->local = local;
    frame->local = NULL;
    local->main_frame = frame;

    for (i = 0; i < call_cnt; i++) {
        STACK_WIND_COOKIE(discover_frame, dht_discover_cbk, conf->subvolumes[i],
                          conf->subvolumes[i],
                          conf->subvolumes[i]->fops->lookup, &local->loc,
                          local->xattr_req);
    }

    return 0;

err:
    DHT_STACK_UNWIND(lookup, frame, -1, op_errno, NULL, NULL, NULL, NULL);

    return 0;
}

/* Code to call syntask to heal custom xattr from hashed subvol
   to non hashed subvol
*/
int
dht_dir_xattr_heal(xlator_t *this, dht_local_t *local, int *op_errno)
{
    dht_local_t *copy_local = NULL;
    call_frame_t *copy = NULL;
    int ret = -1;
    char gfid_local[GF_UUID_BUF_SIZE] = {0};

    if (gf_uuid_is_null(local->gfid)) {
        gf_msg(this->name, GF_LOG_ERROR, 0, DHT_MSG_DIR_XATTR_HEAL_FAILED,
               "No gfid exists for path %s "
               "so healing xattr is not possible",
               local->loc.path);
        *op_errno = EIO;
        goto out;
    }

    gf_uuid_unparse(local->gfid, gfid_local);
    copy = create_frame(this, this->ctx->pool);
    if (copy) {
        copy_local = dht_local_init(copy, &(local->loc), NULL, 0);
        if (!copy_local) {
            gf_msg(this->name, GF_LOG_ERROR, ENOMEM,
                   DHT_MSG_DIR_XATTR_HEAL_FAILED,
                   "Memory allocation failed "
                   "for path %s gfid %s ",
                   local->loc.path, gfid_local);
            *op_errno = ENOMEM;
            DHT_STACK_DESTROY(copy);
        } else {
            copy_local->stbuf = local->stbuf;
            gf_uuid_copy(copy_local->loc.gfid, local->gfid);
            copy_local->mds_subvol = local->mds_subvol;
            FRAME_SU_DO(copy, dht_local_t);
            ret = synctask_new(this->ctx->env, dht_dir_heal_xattrs,
                               dht_dir_heal_xattrs_done, copy, copy);
            if (ret) {
                gf_msg(this->name, GF_LOG_ERROR, ENOMEM,
                       DHT_MSG_DIR_XATTR_HEAL_FAILED,
                       "Synctask creation failed to heal xattr "
                       "for path %s gfid %s ",
                       local->loc.path, gfid_local);
                *op_errno = ENOMEM;
                DHT_STACK_DESTROY(copy);
            }
        }
    }
out:
    return ret;
}

static int
dht_needs_selfheal(call_frame_t *frame, xlator_t *this)
{
    dht_local_t *local = NULL;
    dht_layout_t *layout = NULL;
    int needs_selfheal = 0;
    int ret = 0;

    local = frame->local;
    layout = local->layout;

    if (local->need_attrheal || local->need_xattr_heal ||
        local->need_selfheal) {
        needs_selfheal = 1;
    }

    ret = dht_layout_normalize(this, &local->loc, layout);

    if (ret != 0) {
        gf_msg_debug(this->name, 0, "fixing assignment on %s", local->loc.path);
        needs_selfheal = 1;
    }
    return needs_selfheal;
}

static int
is_permission_different(ia_prot_t *prot1, ia_prot_t *prot2)
{
    if ((prot1->owner.read != prot2->owner.read) ||
        (prot1->owner.write != prot2->owner.write) ||
        (prot1->owner.exec != prot2->owner.exec) ||
        (prot1->group.read != prot2->group.read) ||
        (prot1->group.write != prot2->group.write) ||
        (prot1->group.exec != prot2->group.exec) ||
        (prot1->other.read != prot2->other.read) ||
        (prot1->other.write != prot2->other.write) ||
        (prot1->other.exec != prot2->other.exec) ||
        (prot1->suid != prot2->suid) || (prot1->sgid != prot2->sgid) ||
        (prot1->sticky != prot2->sticky)) {
        return 1;
    } else {
        return 0;
    }
}

int
dht_lookup_dir_cbk(call_frame_t *frame, void *cookie, xlator_t *this,
                   int op_ret, int op_errno, inode_t *inode, struct iatt *stbuf,
                   dict_t *xattr, struct iatt *postparent)
{
    dht_local_t *local = NULL;
    dht_conf_t *conf = NULL;
    int this_call_cnt = 0;
    xlator_t *prev = NULL;
    dht_layout_t *layout = NULL;
    int ret = -1;
    int is_dir = 0;
    int32_t check_mds = 0;
    int errst = 0;
    char gfid_local[GF_UUID_BUF_SIZE] = {0};
    char gfid_node[GF_UUID_BUF_SIZE] = {0};
    int32_t mds_xattr_val[1] = {0};

    GF_VALIDATE_OR_GOTO("dht", frame, out);
    GF_VALIDATE_OR_GOTO("dht", this, out);
    GF_VALIDATE_OR_GOTO("dht", frame->local, out);
    GF_VALIDATE_OR_GOTO("dht", this->private, out);
    GF_VALIDATE_OR_GOTO("dht", cookie, out);

    local = frame->local;
    prev = cookie;
    conf = this->private;

    layout = local->layout;
    gf_msg_debug(this->name, op_errno,
                 "%s: lookup on %s returned with op_ret = %d, op_errno = %d",
                 local->loc.path, prev->name, op_ret, op_errno);

    /* The first successful lookup*/
    if (!op_ret && gf_uuid_is_null(local->gfid)) {
        memcpy(local->gfid, stbuf->ia_gfid, 16);
    }
    if (!gf_uuid_is_null(local->gfid)) {
        gf_uuid_unparse(local->gfid, gfid_local);
    }

    /* Check if the gfid is different for file from other node */
    if (!op_ret && gf_uuid_compare(local->gfid, stbuf->ia_gfid)) {
        gf_uuid_unparse(stbuf->ia_gfid, gfid_node);

        gf_msg(this->name, GF_LOG_WARNING, 0, DHT_MSG_GFID_MISMATCH,
               "%s: gfid different on %s."
               " gfid local = %s, gfid subvol = %s",
               local->loc.path, prev->name, gfid_local, gfid_node);
    }

    LOCK(&frame->lock);
    {
        /* TODO: assert equal mode on stbuf->st_mode and
           local->stbuf->st_mode
           else mkdir/chmod/chown and fix
        */
        ret = dht_layout_merge(this, layout, prev, op_ret, op_errno, xattr);

        if (op_ret == -1) {
            local->op_errno = op_errno;

            /* The GFID is missing on this subvol. Force a heal. */
            if (op_errno == ENODATA) {
                local->need_lookup_everywhere = 1;
            }
            goto unlock;
        }

        is_dir = check_is_dir(inode, stbuf, xattr);
        if (!is_dir) {
            gf_msg_debug(this->name, 0,
                         "%s: lookup on %s returned non dir 0%o"
                         "calling lookup_everywhere",
                         local->loc.path, prev->name, stbuf->ia_type);

            local->need_lookup_everywhere = 1;
            goto unlock;
        }

        local->op_ret = 0;
        if (local->xattr == NULL) {
            local->xattr = dict_ref(xattr);
        } else {
            dht_aggregate_xattr(local->xattr, xattr);
        }

        if (__is_root_gfid(stbuf->ia_gfid)) {
            ret = dht_dir_has_layout(xattr, conf->xattr_name);
            if (ret >= 0) {
                if (is_greater_time(local->prebuf.ia_ctime,
                                    local->prebuf.ia_ctime_nsec,
                                    stbuf->ia_ctime, stbuf->ia_ctime_nsec)) {
                    /* Choose source */
                    local->prebuf.ia_gid = stbuf->ia_gid;
                    local->prebuf.ia_uid = stbuf->ia_uid;

                    local->prebuf.ia_ctime = stbuf->ia_ctime;
                    local->prebuf.ia_ctime_nsec = stbuf->ia_ctime_nsec;
                    local->prebuf.ia_prot = stbuf->ia_prot;
                }
            }
        }

        if (local->stbuf.ia_type != IA_INVAL) {
            /* This is not the first subvol to respond
             * Compare values to see if attrs need to be healed
             */
            if ((local->stbuf.ia_gid != stbuf->ia_gid) ||
                (local->stbuf.ia_uid != stbuf->ia_uid) ||
                (is_permission_different(&local->stbuf.ia_prot,
                                         &stbuf->ia_prot))) {
                local->need_attrheal = 1;
            }
        }

        if (local->inode == NULL)
            local->inode = inode_ref(inode);

        dht_iatt_merge(this, &local->stbuf, stbuf);
        dht_iatt_merge(this, &local->postparent, postparent);

        if (!dict_get(xattr, conf->mds_xattr_key)) {
            gf_msg_debug(this->name, 0,
                         "%s: mds xattr %s is not present "
                         "on %s(gfid = %s)",
                         local->loc.path, conf->mds_xattr_key, prev->name,
                         gfid_local);
            goto unlock;
        }

        /* Save the mds subvol info and stbuf. This is the value that will
         * be used for healing
         */
        local->mds_subvol = prev;
        local->mds_stbuf = *stbuf;

        /* Save mds subvol on inode ctx */

        ret = dht_inode_ctx_mdsvol_set(local->inode, this, prev);
        if (ret) {
            gf_msg(this->name, GF_LOG_ERROR, 0, DHT_MSG_SET_INODE_CTX_FAILED,
                   "%s: Failed to set mds (%s)", local->loc.path, prev->name);
        }
        check_mds = dht_dict_get_array(xattr, conf->mds_xattr_key,
                                       mds_xattr_val, 1, &errst);
        if ((check_mds < 0) && !errst) {
            /* Check if xattrs need to be healed on the directories */
            local->mds_xattr = dict_ref(xattr);
            gf_msg_debug(this->name, 0,
                         "%s: %s is not zero on %s. Xattrs need to be healed."
                         "(gfid = %s)",
                         local->loc.path, conf->mds_xattr_key, prev->name,
                         gfid_local);
            local->need_xattr_heal = 1;
        }
    }

unlock:
    UNLOCK(&frame->lock);

    this_call_cnt = dht_frame_return(frame);

    if (is_last_call(this_call_cnt)) {
        /* If the mds subvol is not set correctly*/
        if (!__is_root_gfid(local->gfid) &&
            (!dict_get(local->xattr, conf->mds_xattr_key))) {
            local->need_selfheal = 1;
        }

        /* No need to call xattr heal code if volume count is 1
         */
        if (conf->subvolume_cnt == 1) {
            local->need_xattr_heal = 0;
        }

        if (local->need_selfheal || local->need_lookup_everywhere) {
            /* Set the gfid-req so posix will set the GFID*/
            if (!gf_uuid_is_null(local->gfid)) {
                /* Ok, this should _never_ happen */
                ret = dict_set_static_bin(local->xattr_req, "gfid-req",
                                          local->gfid, 16);
            } else {
                if (!gf_uuid_is_null(local->gfid_req))
                    ret = dict_set_static_bin(local->xattr_req, "gfid-req",
                                              local->gfid_req, 16);
            }
        }

        if (local->need_lookup_everywhere) {
            local->need_lookup_everywhere = 0;
            dht_lookup_everywhere(frame, this, &local->loc);
            return 0;
        }

        if (local->op_ret == 0) {
            if (dht_needs_selfheal(frame, this)) {
                goto selfheal;
            }

            dht_layout_set(this, local->inode, layout);
            if (local->inode) {
                dht_inode_ctx_time_update(local->inode, this, &local->stbuf, 1);
            }

            if (local->loc.parent) {
                dht_inode_ctx_time_update(local->loc.parent, this,
                                          &local->postparent, 1);
            }
        }

        DHT_STRIP_PHASE1_FLAGS(&local->stbuf);
        dht_set_fixed_dir_stat(&local->postparent);
        /* Delete mds xattr at the time of STACK UNWIND */
        if (local->xattr)
            GF_REMOVE_INTERNAL_XATTR(conf->mds_xattr_key, local->xattr);

        DHT_STACK_UNWIND(lookup, frame, local->op_ret, local->op_errno,
                         local->inode, &local->stbuf, local->xattr,
                         &local->postparent);
    }

    return 0;

selfheal:
    FRAME_SU_DO(frame, dht_local_t);
    ret = dht_selfheal_directory(frame, dht_lookup_selfheal_cbk, &local->loc,
                                 layout);
out:
    return ret;
}

static int
dht_lookup_directory(call_frame_t *frame, xlator_t *this, loc_t *loc)
{
    int call_cnt = 0;
    int i = 0;
    dht_conf_t *conf = NULL;
    dht_local_t *local = NULL;
    int ret = 0;

    GF_VALIDATE_OR_GOTO("dht", frame, out);
    GF_VALIDATE_OR_GOTO("dht", this, unwind);
    GF_VALIDATE_OR_GOTO("dht", frame->local, unwind);
    GF_VALIDATE_OR_GOTO("dht", this->private, unwind);
    GF_VALIDATE_OR_GOTO("dht", loc, unwind);

    conf = this->private;
    local = frame->local;

    call_cnt = conf->subvolume_cnt;
    local->call_cnt = call_cnt;

    local->layout = dht_layout_new(this, conf->subvolume_cnt);
    if (!local->layout) {
        goto unwind;
    }

    if (local->xattr != NULL) {
        dict_unref(local->xattr);
        local->xattr = NULL;
    }

    if (!gf_uuid_is_null(local->gfid)) {
        /* use this gfid in order to heal any missing ones */
        ret = dict_set_gfuuid(local->xattr_req, "gfid-req", local->gfid, true);
        if (ret)
            gf_msg(this->name, GF_LOG_WARNING, 0, DHT_MSG_DICT_SET_FAILED,
                   "%s: Failed to set dictionary value:"
                   " key = gfid-req",
                   local->loc.path);
    }

    for (i = 0; i < call_cnt; i++) {
        STACK_WIND_COOKIE(
            frame, dht_lookup_dir_cbk, conf->subvolumes[i], conf->subvolumes[i],
            conf->subvolumes[i]->fops->lookup, &local->loc, local->xattr_req);
    }
    return 0;
unwind:
    DHT_STACK_UNWIND(lookup, frame, -1, ENOMEM, NULL, NULL, NULL, NULL);
out:
    return 0;
}

int
dht_revalidate_cbk(call_frame_t *frame, void *cookie, xlator_t *this,
                   int op_ret, int op_errno, inode_t *inode, struct iatt *stbuf,
                   dict_t *xattr, struct iatt *postparent)
{
    dht_local_t *local = NULL;
    int this_call_cnt = 0;
    xlator_t *prev = NULL;
    dht_layout_t *layout = NULL;
    dht_conf_t *conf = NULL;
    int ret = -1;
    int is_dir = 0;
    int is_linkfile = 0;
    int follow_link = 0;
    char gfid[GF_UUID_BUF_SIZE] = {0};
    uint32_t vol_commit_hash = 0;
    xlator_t *subvol = NULL;
    int32_t check_mds = 0;
    int errst = 0, i = 0;
    int32_t mds_xattr_val[1] = {0};

    GF_VALIDATE_OR_GOTO("dht", frame, err);
    GF_VALIDATE_OR_GOTO("dht", this, err);
    GF_VALIDATE_OR_GOTO("dht", frame->local, err);
    GF_VALIDATE_OR_GOTO("dht", cookie, err);
    GF_VALIDATE_OR_GOTO("dht", this->private, err);

    local = frame->local;
    prev = cookie;
    conf = this->private;

    if (!conf->vch_forced) {
        /* Update the commithash value if available
         */
        ret = dict_get_uint32(xattr, conf->commithash_xattr_name,
                              &vol_commit_hash);
        if (ret == 0) {
            conf->vol_commit_hash = vol_commit_hash;
        }
    }

    gf_uuid_unparse(local->loc.gfid, gfid);

    gf_msg_debug(this->name, op_errno,
                 "%s: revalidate lookup on %s returned op_ret %d",
                 local->loc.path, prev->name, op_ret);

    LOCK(&frame->lock);
    {
        if (gf_uuid_is_null(local->gfid)) {
            memcpy(local->gfid, local->loc.gfid, 16);
        }

        if (op_ret == -1) {
            local->op_errno = op_errno;

            if ((op_errno != ENOTCONN) && (op_errno != ENOENT) &&
                (op_errno != ESTALE)) {
                gf_msg(this->name, GF_LOG_INFO, op_errno,
                       DHT_MSG_REVALIDATE_CBK_INFO,
                       "Revalidate: subvolume %s for %s "
                       "(gfid = %s) returned -1",
                       prev->name, local->loc.path, gfid);
            }
            if (op_errno == ESTALE) {
                /* propagate the ESTALE to parent.
                 * setting local->return_estale would send
                 * ESTALE to parent. */
                local->return_estale = 1;
            }

            /* if it is ENOENT, we may have to do a
             * 'lookup_everywhere()' to make sure
             * the file is not migrated */
            if (op_errno == ENOENT) {
                if (IA_ISREG(local->loc.inode->ia_type)) {
                    gf_msg_debug(this->name, 0,
                                 "found ENOENT for %s. "
                                 "Setting "
                                 "need_lookup_everywhere"
                                 " flag to 1",
                                 local->loc.path);

                    local->need_lookup_everywhere = 1;
                } else if (IA_ISDIR(local->loc.inode->ia_type)) {
                    layout = local->layout;
                    for (i = 0; i < layout->cnt; i++) {
                        if (layout->list[i].xlator == prev) {
                            layout->list[i].err = op_errno;
                            break;
                        }
                    }

                    local->need_selfheal = 1;
                }
            }

            /* The GFID is missing on this subvol. Lookup everywhere to force a
             * gfid heal
             */
            if ((op_errno == ENODATA) &&
                (IA_ISDIR(local->loc.inode->ia_type))) {
                local->need_lookup_everywhere = 1;
            }

            goto unlock;
        }

        if ((!IA_ISINVAL(local->inode->ia_type)) &&
            stbuf->ia_type != local->inode->ia_type) {
            gf_msg(this->name, GF_LOG_WARNING, 0, DHT_MSG_FILE_TYPE_MISMATCH,
                   "mismatching filetypes 0%o v/s 0%o for %s,"
                   " gfid = %s",
                   (stbuf->ia_type), (local->inode->ia_type), local->loc.path,
                   gfid);

            local->op_ret = -1;
            local->op_errno = EINVAL;

            goto unlock;
        }

        layout = local->layout;

        is_dir = check_is_dir(inode, stbuf, xattr);
        is_linkfile = check_is_linkfile(inode, stbuf, xattr,
                                        conf->link_xattr_name);
        if (is_linkfile) {
            follow_link = 1;
            goto unlock;
        }
        if (is_dir) {
            ret = dht_dir_has_layout(xattr, conf->xattr_name);
            if (ret >= 0) {
                if (is_greater_time(local->prebuf.ia_ctime,
                                    local->prebuf.ia_ctime_nsec,
                                    stbuf->ia_ctime, stbuf->ia_ctime_nsec)) {
                    /* Choose source */
                    local->prebuf.ia_gid = stbuf->ia_gid;
                    local->prebuf.ia_uid = stbuf->ia_uid;

                    local->prebuf.ia_ctime = stbuf->ia_ctime;
                    local->prebuf.ia_ctime_nsec = stbuf->ia_ctime_nsec;

                    if (__is_root_gfid(stbuf->ia_gfid))
                        local->prebuf.ia_prot = stbuf->ia_prot;
                }
            }

            if (local->stbuf.ia_type != IA_INVAL) {
                if ((local->stbuf.ia_gid != stbuf->ia_gid) ||
                    (local->stbuf.ia_uid != stbuf->ia_uid) ||
                    is_permission_different(&local->stbuf.ia_prot,
                                            &stbuf->ia_prot)) {
                    local->need_attrheal = 1;
                }
            }

            if (!dict_get(xattr, conf->mds_xattr_key)) {
                gf_msg_debug(this->name, 0,
                             "%s: internal xattr %s is not present"
                             " on subvol %s(gfid is %s)",
                             local->loc.path, conf->mds_xattr_key, prev->name,
                             gfid);
            } else {
                check_mds = dht_dict_get_array(xattr, conf->mds_xattr_key,
                                               mds_xattr_val, 1, &errst);
                local->mds_subvol = prev;
                local->mds_stbuf.ia_gid = stbuf->ia_gid;
                local->mds_stbuf.ia_uid = stbuf->ia_uid;
                local->mds_stbuf.ia_prot = stbuf->ia_prot;

                /* save mds subvol on inode ctx */
                ret = dht_inode_ctx_mdsvol_set(local->inode, this, prev);
                if (ret) {
                    gf_msg(this->name, GF_LOG_ERROR, 0,
                           DHT_MSG_SET_INODE_CTX_FAILED,
                           "Failed to set MDS subvol for %s vol is %s",
                           local->loc.path, prev->name);
                }
                if ((check_mds < 0) && !errst) {
                    /* Check if xattrs need to be healed on the directory
                     */
                    local->mds_xattr = dict_ref(xattr);
                    gf_msg_debug(this->name, 0,
                                 "Value of %s is not zero on "
                                 "hashed subvol so xattr needs to"
                                 " be healed on non hashed"
                                 " path is %s and vol name is %s "
                                 " gfid is %s",
                                 conf->mds_xattr_key, local->loc.path,
                                 prev->name, gfid);
                    local->need_xattr_heal = 1;
                }
            }
            ret = dht_layout_dir_mismatch(this, layout, prev, &local->loc,
                                          xattr);
            if (ret != 0) {
                /* In memory layout does not match on-disk layout.
                 */
                gf_msg(this->name, GF_LOG_INFO, 0, DHT_MSG_LAYOUT_MISMATCH,
                       "Mismatching layouts for %s, gfid = %s", local->loc.path,
                       gfid);

                local->layout_mismatch = 1;

                goto unlock;
            }
        }

        gf_uuid_copy(local->stbuf.ia_gfid, stbuf->ia_gfid);
        dht_iatt_merge(this, &local->stbuf, stbuf);
        dht_iatt_merge(this, &local->postparent, postparent);

        local->op_ret = 0;

        if (!local->xattr) {
            local->xattr = dict_ref(xattr);
        } else if (is_dir) {
            dht_aggregate_xattr(local->xattr, xattr);
        }
    }
unlock:
    UNLOCK(&frame->lock);

    if (follow_link) {
        /* Found a linkto file. Follow it to see if the target file exists
         */
        gf_uuid_copy(local->gfid, stbuf->ia_gfid);

        subvol = dht_linkfile_subvol(this, inode, stbuf, xattr);
        if (!subvol) {
            op_errno = ESTALE;
            local->op_ret = -1;
        } else {
            STACK_WIND_COOKIE(frame, dht_lookup_linkfile_cbk, subvol, subvol,
                              subvol->fops->lookup, &local->loc,
                              local->xattr_req);
            return 0;
        }
    }

    this_call_cnt = dht_frame_return(frame);

    if (is_last_call(this_call_cnt)) {
        if (!IA_ISDIR(local->stbuf.ia_type) &&
            (local->hashed_subvol != local->cached_subvol) &&
            (local->stbuf.ia_nlink == 1) &&
            (conf && conf->unhashed_sticky_bit)) {
            local->stbuf.ia_prot.sticky = 1;
        }
        /* No need to call heal code if volume count is 1
         */
        if (conf->subvolume_cnt == 1)
            local->need_xattr_heal = 0;

        if (IA_ISDIR(local->stbuf.ia_type)) {
            /* No mds xattr found. Trigger a heal to set it */
            if (!__is_root_gfid(local->loc.inode->gfid) &&
                (!dict_get(local->xattr, conf->mds_xattr_key)))
                local->need_selfheal = 1;

            if (dht_needs_selfheal(frame, this)) {
                if (!__is_root_gfid(local->loc.inode->gfid)) {
                    if (local->mds_subvol) {
                        local->stbuf.ia_gid = local->mds_stbuf.ia_gid;
                        local->stbuf.ia_uid = local->mds_stbuf.ia_uid;
                        local->stbuf.ia_prot = local->mds_stbuf.ia_prot;
                    }
                } else {
                    local->stbuf.ia_gid = local->prebuf.ia_gid;
                    local->stbuf.ia_uid = local->prebuf.ia_uid;
                    local->stbuf.ia_prot = local->prebuf.ia_prot;
                }

                layout = local->layout;
                dht_selfheal_directory(frame, dht_lookup_selfheal_cbk,
                                       &local->loc, layout);
                return 0;
            }
        }

        if (local->layout_mismatch) {
            /* Found layout mismatch in the directory, need to
               fix this in the inode context */
            dht_layout_unref(this, local->layout);
            local->layout = NULL;
            dht_lookup_directory(frame, this, &local->loc);
            return 0;
        }

        if (local->need_lookup_everywhere) {
            /* As the current layout gave ENOENT error, we would
               need a new layout */
            dht_layout_unref(this, local->layout);
            local->layout = NULL;

            /* We know that current cached subvol is no longer
               valid, get the new one */
            local->cached_subvol = NULL;
            if (local->xattr_req) {
                if (!gf_uuid_is_null(local->gfid)) {
                    ret = dict_set_static_bin(local->xattr_req, "gfid-req",
                                              local->gfid, 16);
                }
            }

            dht_lookup_everywhere(frame, this, &local->loc);
            return 0;
        }
        if (local->return_estale) {
            local->op_ret = -1;
            local->op_errno = ESTALE;
        }

        if (local->loc.parent) {
            dht_inode_ctx_time_update(local->loc.parent, this,
                                      &local->postparent, 1);
        }

        DHT_STRIP_PHASE1_FLAGS(&local->stbuf);
        dht_set_fixed_dir_stat(&local->postparent);

        /* local->stbuf is updated only from subvols which have a layout
         * The reason is to avoid choosing attr heal source from newly
         * added bricks. In case e.g we have only one subvol and for
         * some reason layout is not present on it, then local->stbuf
         * will be EINVAL. This is an indication that the subvols
         * active in the cluster do not have layouts on disk.
         * Unwind with ESTALE to trigger a fresh lookup */
        if (is_dir && local->stbuf.ia_type == IA_INVAL) {
            local->op_ret = -1;
            local->op_errno = ESTALE;
        }
        /* Delete mds xattr at the time of STACK UNWIND */
        if (local->xattr)
            GF_REMOVE_INTERNAL_XATTR(conf->mds_xattr_key, local->xattr);

        DHT_STACK_UNWIND(lookup, frame, local->op_ret, local->op_errno,
                         local->inode, &local->stbuf, local->xattr,
                         &local->postparent);
    }

err:
    return ret;
}

static int
dht_lookup_linkfile_create_cbk(call_frame_t *frame, void *cooie, xlator_t *this,
                               int32_t op_ret, int32_t op_errno, inode_t *inode,
                               struct iatt *stbuf, struct iatt *preparent,
                               struct iatt *postparent, dict_t *xdata)
{
    dht_local_t *local = NULL;
    xlator_t *cached_subvol = NULL;
    dht_conf_t *conf = NULL;
    int ret = -1;
    char gfid[GF_UUID_BUF_SIZE] = {0};

    GF_VALIDATE_OR_GOTO("dht", frame, out);
    GF_VALIDATE_OR_GOTO("dht", this, out);
    GF_VALIDATE_OR_GOTO("dht", frame->local, out);
    GF_VALIDATE_OR_GOTO("dht", this->private, out);

    local = frame->local;
    cached_subvol = local->cached_subvol;
    conf = this->private;

    gf_uuid_unparse(local->loc.gfid, gfid);

    if (local->locked)
        dht_unlock_namespace(frame, &local->lock[0]);

    ret = dht_layout_preset(this, local->cached_subvol, local->loc.inode);
    if (ret < 0) {
        gf_msg_debug(this->name, EINVAL,
                     "Failed to set layout for subvolume %s, "
                     "(gfid = %s)",
                     cached_subvol ? cached_subvol->name : "<nil>", gfid);
        local->op_ret = -1;
        local->op_errno = EINVAL;
        goto unwind;
    }

    local->op_ret = 0;
    if ((local->stbuf.ia_nlink == 1) && (conf && conf->unhashed_sticky_bit)) {
        local->stbuf.ia_prot.sticky = 1;
    }

    if (local->loc.parent) {
        dht_inode_ctx_time_update(local->loc.parent, this, postparent, 1);
    }

unwind:
    gf_msg_debug(this->name, 0,
                 "creation of linkto on hashed subvol:%s, "
                 "returned with op_ret %d and op_errno %d: %s",
                 local->hashed_subvol->name, op_ret, op_errno,
                 uuid_utoa(local->loc.gfid));

    if (local->linked == _gf_true)
        dht_linkfile_attr_heal(frame, this);

    dht_set_fixed_dir_stat(&local->postparent);

    DHT_STRIP_PHASE1_FLAGS(&local->stbuf);
    DHT_STACK_UNWIND(lookup, frame, local->op_ret, local->op_errno,
                     local->inode, &local->stbuf, local->xattr,
                     &local->postparent);
out:
    return ret;
}

static int
dht_lookup_unlink_cbk(call_frame_t *frame, void *cookie, xlator_t *this,
                      int op_ret, int op_errno, struct iatt *preparent,
                      struct iatt *postparent, dict_t *xdata)
{
    int this_call_cnt = 0;
    dht_local_t *local = NULL;
    const char *path = NULL;

    local = (dht_local_t *)frame->local;
    path = local->loc.path;
    FRAME_SU_UNDO(frame, dht_local_t);

    gf_msg(this->name, GF_LOG_INFO, 0, DHT_MSG_UNLINK_LOOKUP_INFO,
           "lookup_unlink returned with "
           "op_ret -> %d and op-errno -> %d for %s",
           op_ret, op_errno, ((path == NULL) ? "null" : path));

    this_call_cnt = dht_frame_return(frame);
    if (is_last_call(this_call_cnt)) {
        dht_lookup_everywhere_done(frame, this);
    }

    return 0;
}

static int
dht_lookup_unlink_of_false_linkto_cbk(call_frame_t *frame, void *cookie,
                                      xlator_t *this, int op_ret, int op_errno,
                                      struct iatt *preparent,
                                      struct iatt *postparent, dict_t *xdata)
{
    int this_call_cnt = 0;
    dht_local_t *local = NULL;
    const char *path = NULL;

    local = (dht_local_t *)frame->local;
    path = local->loc.path;

    FRAME_SU_UNDO(frame, dht_local_t);

    gf_msg(this->name, GF_LOG_INFO, 0, DHT_MSG_UNLINK_LOOKUP_INFO,
           "lookup_unlink returned with "
           "op_ret -> %d and op-errno -> %d for %s",
           op_ret, op_errno, ((path == NULL) ? "null" : path));

    this_call_cnt = dht_frame_return(frame);
    if (is_last_call(this_call_cnt)) {
        if ((op_ret == 0) || ((op_errno != EBUSY) && (op_errno != ENOTCONN))) {
            dht_lookup_everywhere_done(frame, this);
        } else {
            /*When dht_lookup_everywhere is performed, one cached
             *and one hashed file was found and hashed file does
             *not point to the above mentioned cached node. So it
             *was considered as stale and an unlink was performed.
             *But unlink fails. So may be rebalance is in progress.
             *now ideally we have two data-files. One obtained during
             *lookup_everywhere and one where unlink-failed. So
             *at this point in time we cannot decide which one to
             *choose because there are chances of first cached
             *file is truncated after rebalance and if it is chosen
             *as cached node, application will fail. So return EIO.*/

            if (op_errno == EBUSY) {
                gf_msg(this->name, GF_LOG_ERROR, op_errno,
                       DHT_MSG_UNLINK_FAILED,
                       "Could not unlink the linkto file as "
                       "either fd is open and/or linkto xattr "
                       "is set for %s",
                       ((path == NULL) ? "null" : path));
            }
            DHT_STACK_UNWIND(lookup, frame, -1, EIO, NULL, NULL, NULL, NULL);
        }
    }

    return 0;
}

static int
dht_lookup_unlink_stale_linkto_cbk(call_frame_t *frame, void *cookie,
                                   xlator_t *this, int op_ret, int op_errno,
                                   struct iatt *preparent,
                                   struct iatt *postparent, dict_t *xdata)
{
    dht_local_t *local = NULL;
    const char *path = NULL;

    /* NOTE:
     * If stale file unlink fails either there is an open-fd or is not an
     * dht-linkto-file then posix_unlink returns EBUSY, which is overwritten
     *  to ENOENT
     */

    local = frame->local;

    if (local) {
        FRAME_SU_UNDO(frame, dht_local_t);
        if (local->loc.path)
            path = local->loc.path;
    }

    gf_msg(this->name, GF_LOG_INFO, 0, DHT_MSG_UNLINK_LOOKUP_INFO,
           "Returned with op_ret %d and "
           "op_errno %d for %s",
           op_ret, op_errno, ((path == NULL) ? "null" : path));

    DHT_STACK_UNWIND(lookup, frame, -1, ENOENT, NULL, NULL, NULL, NULL);

    return 0;
}

static int
dht_fill_dict_to_avoid_unlink_of_migrating_file(dict_t *dict)
{
    int ret = 0;

    ret = dict_set_int32_sizen(dict, DHT_SKIP_NON_LINKTO_UNLINK, 1);

    if (ret)
        return -1;

    ret = dict_set_int32_sizen(dict, DHT_SKIP_OPEN_FD_UNLINK, 1);

    if (ret)
        return -1;

    return 0;
}

static int32_t
dht_linkfile_create_lookup_cbk(call_frame_t *frame, void *cookie,
                               xlator_t *this, int32_t op_ret, int32_t op_errno,
                               inode_t *inode, struct iatt *buf, dict_t *xdata,
                               struct iatt *postparent)
{
    dht_local_t *local = NULL;
    int call_cnt = 0, ret = 0;
    xlator_t *subvol = NULL;
    uuid_t gfid = {
        0,
    };
    char gfid_str[GF_UUID_BUF_SIZE] = {0};

    subvol = cookie;
    local = frame->local;

    if (subvol == local->hashed_subvol) {
        if ((op_ret == 0) || (op_errno != ENOENT))
            local->dont_create_linkto = _gf_true;
    } else {
        if (gf_uuid_is_null(local->gfid))
            gf_uuid_copy(gfid, local->loc.gfid);
        else
            gf_uuid_copy(gfid, local->gfid);

        if ((op_ret == 0) && gf_uuid_compare(gfid, buf->ia_gfid)) {
            gf_uuid_unparse(gfid, gfid_str);
            gf_msg_debug(this->name, 0,
                         "gfid (%s) different on cached subvol "
                         "(%s) and looked up inode (%s), not "
                         "creating linkto",
                         uuid_utoa(buf->ia_gfid), subvol->name, gfid_str);
            local->dont_create_linkto = _gf_true;
        } else if (op_ret == -1) {
            local->dont_create_linkto = _gf_true;
        }
    }

    call_cnt = dht_frame_return(frame);
    if (is_last_call(call_cnt)) {
        if (local->dont_create_linkto)
            goto no_linkto;
        else {
            gf_msg_debug(this->name, 0,
                         "Creating linkto file on %s(hash) to "
                         "%s on %s (gfid = %s)",
                         local->hashed_subvol->name, local->loc.path,
                         local->cached_subvol->name, gfid_str);

            ret = dht_linkfile_create(frame, dht_lookup_linkfile_create_cbk,
                                      this, local->cached_subvol,
                                      local->hashed_subvol, &local->loc);

            if (ret < 0)
                goto no_linkto;
        }
    }

    return 0;

no_linkto:
    gf_msg_debug(this->name, 0,
                 "skipped linkto creation (path:%s) (gfid:%s) "
                 "(hashed-subvol:%s) (cached-subvol:%s)",
                 local->loc.path, gfid_str, local->hashed_subvol->name,
                 local->cached_subvol->name);

    dht_lookup_linkfile_create_cbk(frame, NULL, this, 0, 0, local->loc.inode,
                                   &local->stbuf, &local->preparent,
                                   &local->postparent, local->xattr);
    return 0;
}

static int32_t
dht_call_lookup_linkfile_create(call_frame_t *frame, void *cookie,
                                xlator_t *this, int32_t op_ret,
                                int32_t op_errno, dict_t *xdata)
{
    dht_local_t *local = NULL;
    char gfid[GF_UUID_BUF_SIZE] = {0};
    int i = 0;
    xlator_t *subvol = NULL;

    local = frame->local;
    if (gf_uuid_is_null(local->gfid))
        gf_uuid_unparse(local->loc.gfid, gfid);
    else
        gf_uuid_unparse(local->gfid, gfid);

    if (op_ret < 0) {
        gf_log(this->name, GF_LOG_WARNING,
               "protecting namespace failed, skipping linkto "
               "creation (path:%s)(gfid:%s)(hashed-subvol:%s)"
               "(cached-subvol:%s)",
               local->loc.path, gfid, local->hashed_subvol->name,
               local->cached_subvol->name);
        goto err;
    }

    local->locked = _gf_true;

    local->call_cnt = 2;

    for (i = 0; i < 2; i++) {
        subvol = (subvol == NULL) ? local->hashed_subvol : local->cached_subvol;

        STACK_WIND_COOKIE(frame, dht_linkfile_create_lookup_cbk, subvol, subvol,
                          subvol->fops->lookup, &local->loc, NULL);
    }

    return 0;

err:
    dht_lookup_linkfile_create_cbk(frame, NULL, this, 0, 0, local->loc.inode,
                                   &local->stbuf, &local->preparent,
                                   &local->postparent, local->xattr);
    return 0;
}

/* Rebalance is performed from cached_node to hashed_node. Initial cached_node
 * contains a non-linkto file. After migration it is converted to linkto and
 * then unlinked. And at hashed_subvolume, first a linkto file is present,
 * then after migration it is converted to a non-linkto file.
 *
 * Lets assume a file is present on cached subvolume and a new brick is added
 * and new brick is the new_hashed subvolume. So fresh lookup on newly added
 * hashed subvolume will fail and dht_lookup_everywhere gets called.  If just
 * before sending the dht_lookup_everywhere request rebalance is in progress,
 *
 * from cached subvolume it may see: Nonlinkto or linkto or No file
 * from hashed subvolume it may see: No file or linkto file or non-linkto file
 *
 * So this boils down to 9 cases:
 *   at cached_subvol            at hashed_subvol
 *   ----------------           -----------------
 *
 *a)   No file                     No file
 *    [request reached after    [Request reached before
 *       migration]                Migration]
 *
 *b)   No file                     Linkto File
 *
 *c)   No file                     Non-Linkto File
 *
 *d)   Linkto                      No-File
 *
 *e)   Linkto                      Linkto
 *
 *f)   Linkto                      Non-Linkto
 *
 *g)   NonLinkto                   No-File
 *
 *h)   NonLinkto                   Linkto
 *
 *i)   NonLinkto                   NonLinkto
 *
 * dht_lookup_everywhere_done takes decision based on any of the above case
 */

static int
dht_lookup_everywhere_done(call_frame_t *frame, xlator_t *this)
{
    int ret = 0;
    dht_local_t *local = NULL;
    xlator_t *hashed_subvol = NULL;
    xlator_t *cached_subvol = NULL;
    dht_layout_t *layout = NULL;
    char gfid[GF_UUID_BUF_SIZE] = {0};
    gf_boolean_t found_non_linkto_on_hashed = _gf_false;

    local = frame->local;
    hashed_subvol = local->hashed_subvol;
    cached_subvol = local->cached_subvol;

    gf_uuid_unparse(local->loc.gfid, gfid);

    if (local->file_count && local->dir_count) {
        gf_msg(this->name, GF_LOG_ERROR, 0, DHT_MSG_FILE_TYPE_MISMATCH,
               "path %s (gfid = %s)exists as a file on one "
               "subvolume and directory on another. "
               "Please fix it manually",
               local->loc.path, gfid);
        DHT_STACK_UNWIND(lookup, frame, -1, EIO, NULL, NULL, NULL, NULL);
        return 0;
    }
    if (local->op_ret && local->gfid_missing) {
        if (gf_uuid_is_null(local->gfid_req)) {
            DHT_STACK_UNWIND(lookup, frame, -1, ENODATA, NULL, NULL, NULL,
                             NULL);
            return 0;
        }
        /* A hack */
        dht_lookup_directory(frame, this, &local->loc);
        return 0;
    }

    if (local->dir_count) {
        dht_lookup_directory(frame, this, &local->loc);
        return 0;
    }

    gf_msg_debug(this->name, 0,
                 "STATUS: hashed_subvol %s "
                 "cached_subvol %s",
                 (hashed_subvol == NULL) ? "null" : hashed_subvol->name,
                 (cached_subvol == NULL) ? "null" : cached_subvol->name);

    if (!cached_subvol) {
        if (local->skip_unlink.handle_valid_link && hashed_subvol) {
            /*Purpose of "DHT_SKIP_NON_LINKTO_UNLINK":
             * If this lookup is performed by rebalance and this
             * rebalance process detected hashed file and by
             * the time it sends the lookup request to cached node,
             * file got migrated and now at initial hashed_node,
             * final migrated file is present. With current logic,
             * because this process fails to find the cached_node,
             * it will unlink the file at initial hashed_node.
             *
             * So we avoid this by setting key, and checking at the
             * posix_unlink that unlink the file only if file is a
             * linkto file and not a migrated_file.
             */

            ret = dht_fill_dict_to_avoid_unlink_of_migrating_file(
                local->xattr_req);

            if (ret) {
                /* If for some reason, setting key in the dict
                 * fails, return with ENOENT, as with respect to
                 * this process, it detected only a stale link
                 * file.
                 *
                 * Next lookup will delete it.
                 *
                 * Performing deletion of stale link file when
                 * setting key in dict fails, may cause the data
                 * loss because of the above mentioned race.
                 */

                DHT_STACK_UNWIND(lookup, frame, -1, ENOENT, NULL, NULL, NULL,
                                 NULL);
            } else {
                local->skip_unlink.handle_valid_link = _gf_false;

                gf_msg_debug(this->name, 0,
                             "No Cached was found and "
                             "unlink on hashed was skipped"
                             " so performing now: %s",
                             local->loc.path);
                FRAME_SU_DO(frame, dht_local_t);
                STACK_WIND(frame, dht_lookup_unlink_stale_linkto_cbk,
                           hashed_subvol, hashed_subvol->fops->unlink,
                           &local->loc, 0, local->xattr_req);
            }

        } else {
            gf_msg_debug(this->name, 0,
                         "There was no cached file and  "
                         "unlink on hashed is not skipped %s",
                         local->loc.path);

            DHT_STACK_UNWIND(lookup, frame, -1, ENOENT, NULL, NULL, NULL, NULL);
        }
        return 0;
    }

    /* At the time of dht_lookup, no file was found on hashed and that is
     * why dht_lookup_everywhere is called, but by the time
     * dht_lookup_everywhere
     * reached to server, file might have already migrated. In that case we
     * will find a migrated file at the hashed_node. In this case store the
     * layout in context and return successfully.
     */

    if (hashed_subvol || local->need_lookup_everywhere) {
        if (local->need_lookup_everywhere) {
            found_non_linkto_on_hashed = _gf_true;

        } else if ((local->file_count == 1) &&
                   (hashed_subvol == cached_subvol)) {
            gf_msg_debug(this->name, 0,
                         "found cached file on hashed subvolume "
                         "so store in context and return for %s",
                         local->loc.path);

            found_non_linkto_on_hashed = _gf_true;
        }

        if (found_non_linkto_on_hashed)
            goto preset_layout;
    }

    if (hashed_subvol) {
        if (local->skip_unlink.handle_valid_link == _gf_true) {
            if (cached_subvol == local->skip_unlink.hash_links_to) {
                if (gf_uuid_compare(local->skip_unlink.cached_gfid,
                                    local->skip_unlink.hashed_gfid)) {
                    /*GFID different, return error*/
                    DHT_STACK_UNWIND(lookup, frame, -1, ESTALE, NULL, NULL,
                                     NULL, NULL);

                    return 0;
                }

                ret = dht_layout_preset(this, cached_subvol, local->loc.inode);
                if (ret) {
                    gf_msg(this->name, GF_LOG_INFO, 0,
                           DHT_MSG_LAYOUT_PRESET_FAILED,
                           "Could not set pre-set layout "
                           "for subvolume %s",
                           cached_subvol->name);
                }

                local->op_ret = (ret == 0) ? ret : -1;
                local->op_errno = (ret == 0) ? ret : EINVAL;

                /* Presence of local->cached_subvol validates
                 * that lookup from cached node is successful
                 */

                if (!local->op_ret && local->loc.parent) {
                    dht_inode_ctx_time_update(local->loc.parent, this,
                                              &local->postparent, 1);
                }

                gf_msg_debug(this->name, 0,
                             "Skipped unlinking linkto file "
                             "on the hashed subvolume. "
                             "Returning success as it is a "
                             "valid linkto file. Path:%s",
                             local->loc.path);

                goto unwind_hashed_and_cached;
            } else {
                local->skip_unlink.handle_valid_link = _gf_false;

                gf_msg_debug(this->name, 0,
                             "Linkto file found on hashed "
                             "subvol "
                             "and data file found on cached "
                             "subvolume. But linkto points to "
                             "different cached subvolume (%s) "
                             "path %s",
                             (local->skip_unlink.hash_links_to
                                  ? local->skip_unlink.hash_links_to->name
                                  : " <nil>"),
                             local->loc.path);

                if (local->skip_unlink.opend_fd_count == 0) {
                    ret = dht_fill_dict_to_avoid_unlink_of_migrating_file(
                        local->xattr_req);

                    if (ret) {
                        DHT_STACK_UNWIND(lookup, frame, -1, EIO, NULL, NULL,
                                         NULL, NULL);
                    } else {
                        local->call_cnt = 1;
                        FRAME_SU_DO(frame, dht_local_t);
                        STACK_WIND(frame, dht_lookup_unlink_of_false_linkto_cbk,
                                   hashed_subvol, hashed_subvol->fops->unlink,
                                   &local->loc, 0, local->xattr_req);
                    }

                    return 0;
                }
            }
        }
    }

preset_layout:

    if (found_non_linkto_on_hashed) {
        if (local->need_lookup_everywhere) {
            if (gf_uuid_compare(local->gfid, local->inode->gfid)) {
                /* GFID different, return error */
                DHT_STACK_UNWIND(lookup, frame, -1, ENOENT, NULL, NULL, NULL,
                                 NULL);
                return 0;
            }
        }

        local->op_ret = 0;
        local->op_errno = 0;
        layout = dht_layout_for_subvol(this, cached_subvol);
        if (!layout) {
            gf_msg(this->name, GF_LOG_INFO, 0, DHT_MSG_SUBVOL_INFO,
                   "%s: no pre-set layout for subvolume %s,"
                   " gfid = %s",
                   local->loc.path,
                   (cached_subvol ? cached_subvol->name : "<nil>"), gfid);
        }

        ret = dht_layout_set(this, local->inode, layout);
        if (ret < 0) {
            gf_msg(this->name, GF_LOG_INFO, 0, DHT_MSG_SUBVOL_INFO,
                   "%s: failed to set layout for subvol %s, "
                   "gfid = %s",
                   local->loc.path,
                   (cached_subvol ? cached_subvol->name : "<nil>"), gfid);
        }

        if (local->loc.parent) {
            dht_inode_ctx_time_update(local->loc.parent, this,
                                      &local->postparent, 1);
        }

        DHT_STRIP_PHASE1_FLAGS(&local->stbuf);
        dht_set_fixed_dir_stat(&local->postparent);
        DHT_STACK_UNWIND(lookup, frame, local->op_ret, local->op_errno,
                         local->inode, &local->stbuf, local->xattr,
                         &local->postparent);
        return 0;
    }

    if (!hashed_subvol) {
        gf_msg_debug(this->name, 0,
                     "Cannot create linkfile for %s on %s: "
                     "hashed subvolume cannot be found, gfid = %s.",
                     local->loc.path, cached_subvol->name, gfid);

        local->op_ret = 0;
        local->op_errno = 0;

        ret = dht_layout_preset(frame->this, cached_subvol, local->inode);
        if (ret < 0) {
            gf_msg(this->name, GF_LOG_INFO, 0, DHT_MSG_LAYOUT_PRESET_FAILED,
                   "Failed to set layout for subvol %s"
                   ", gfid = %s",
                   cached_subvol ? cached_subvol->name : "<nil>", gfid);
            local->op_ret = -1;
            local->op_errno = EINVAL;
        }

        if (local->loc.parent) {
            dht_inode_ctx_time_update(local->loc.parent, this,
                                      &local->postparent, 1);
        }

        DHT_STRIP_PHASE1_FLAGS(&local->stbuf);
        dht_set_fixed_dir_stat(&local->postparent);
        DHT_STACK_UNWIND(lookup, frame, local->op_ret, local->op_errno,
                         local->inode, &local->stbuf, local->xattr,
                         &local->postparent);
        return 0;
    }

    if (frame->root->op != GF_FOP_RENAME) {
        local->current = &local->lock[0];
        ret = dht_protect_namespace(frame, &local->loc, hashed_subvol,
                                    &local->current->ns,
                                    dht_call_lookup_linkfile_create);
    } else {
        gf_msg_debug(this->name, 0,
                     "Creating linkto file on %s(hash) to %s on %s "
                     "(gfid = %s)",
                     hashed_subvol->name, local->loc.path, cached_subvol->name,
                     gfid);

        ret = dht_linkfile_create(frame, dht_lookup_linkfile_create_cbk, this,
                                  cached_subvol, hashed_subvol, &local->loc);
    }

    return ret;

unwind_hashed_and_cached:
    DHT_STRIP_PHASE1_FLAGS(&local->stbuf);
    dht_set_fixed_dir_stat(&local->postparent);
    DHT_STACK_UNWIND(lookup, frame, local->op_ret, local->op_errno,
                     local->inode, &local->stbuf, local->xattr,
                     &local->postparent);
    return 0;
}

static int
dht_lookup_everywhere_cbk(call_frame_t *frame, void *cookie, xlator_t *this,
                          int32_t op_ret, int32_t op_errno, inode_t *inode,
                          struct iatt *buf, dict_t *xattr,
                          struct iatt *postparent)
{
    dht_local_t *local = NULL;
    int this_call_cnt = 0;
    xlator_t *prev = NULL;
    int is_linkfile = 0;
    int is_dir = 0;
    loc_t *loc = NULL;
    xlator_t *link_subvol = NULL;
    int ret = -1;
    int32_t fd_count = 0;
    dht_conf_t *conf = NULL;
    char gfid[GF_UUID_BUF_SIZE] = {0};
    dict_t *dict_req = {0};

    GF_VALIDATE_OR_GOTO("dht", frame, out);
    GF_VALIDATE_OR_GOTO("dht", this, out);
    GF_VALIDATE_OR_GOTO("dht", frame->local, out);
    GF_VALIDATE_OR_GOTO("dht", cookie, out);
    GF_VALIDATE_OR_GOTO("dht", this->private, out);

    local = frame->local;
    loc = &local->loc;
    conf = this->private;

    prev = cookie;

    gf_msg_debug(this->name, 0,
                 "returned with op_ret %d and op_errno %d (%s) "
                 "from subvol %s",
                 op_ret, op_errno, loc->path, prev->name);

    LOCK(&frame->lock);
    {
        if (op_ret == -1) {
            if (op_errno != ENOENT)
                local->op_errno = op_errno;
            if (op_errno == ENODATA)
                local->gfid_missing = _gf_true;
            goto unlock;
        }

        if (gf_uuid_is_null(local->gfid))
            gf_uuid_copy(local->gfid, buf->ia_gfid);

        gf_uuid_unparse(local->gfid, gfid);

        if (gf_uuid_compare(local->gfid, buf->ia_gfid)) {
            gf_msg(this->name, GF_LOG_WARNING, 0, DHT_MSG_GFID_MISMATCH,
                   "%s: gfid differs on subvolume %s,"
                   " gfid local = %s, gfid node = %s",
                   loc->path, prev->name, gfid, uuid_utoa(buf->ia_gfid));
        }

        is_linkfile = check_is_linkfile(inode, buf, xattr,
                                        conf->link_xattr_name);

        if (is_linkfile) {
            link_subvol = dht_linkfile_subvol(this, inode, buf, xattr);
            gf_msg_debug(this->name, 0, "found on %s linkfile %s (-> %s)",
                         prev->name, loc->path,
                         link_subvol ? link_subvol->name : "''");
            goto unlock;
        }

        is_dir = check_is_dir(inode, buf, xattr);

        /* non linkfile GFID takes precedence but don't overwrite
         gfid if we have already found a cached file*/
        if (!local->cached_subvol)
            gf_uuid_copy(local->gfid, buf->ia_gfid);

        if (is_dir) {
            local->dir_count++;

            gf_msg_debug(this->name, 0, "found on %s directory %s", prev->name,
                         loc->path);
        } else {
            local->file_count++;

            gf_msg_debug(this->name, 0, "found cached file on %s for %s",
                         prev->name, loc->path);

            if (!local->cached_subvol) {
                /* found one file */
                dht_iatt_merge(this, &local->stbuf, buf);

                local->xattr = dict_ref(xattr);
                local->cached_subvol = prev;

                gf_msg_debug(this->name, 0,
                             "storing cached on %s file"
                             " %s",
                             prev->name, loc->path);

                dht_iatt_merge(this, &local->postparent, postparent);

                gf_uuid_copy(local->skip_unlink.cached_gfid, buf->ia_gfid);
            } else {
                /* This is where we need 'rename' both entries logic */
                gf_msg(this->name, GF_LOG_WARNING, 0,
                       DHT_MSG_FILE_ON_MULT_SUBVOL,
                       "multiple subvolumes (%s and %s) have "
                       "file %s (preferably rename the file "
                       "in the backend,and do a fresh lookup)",
                       local->cached_subvol->name, prev->name, local->loc.path);
            }
        }
    }
unlock:
    UNLOCK(&frame->lock);

    if (is_linkfile) {
        ret = dict_get_int32(xattr, GLUSTERFS_OPEN_FD_COUNT, &fd_count);

        /*  Any linkto file found on the non-hashed subvolume should
         *  be unlinked (performed in the "else if" block below)
         *
         *  But if a linkto file is found on hashed subvolume, it may be
         *  pointing to valid cached node. So unlinking of linkto
         *  file on hashed subvolume is skipped and inside
         *  dht_lookup_everywhere_done, checks are performed. If this
         *  linkto file is found as stale linkto file, it is deleted
         *  otherwise unlink is skipped.
         */

        if (local->hashed_subvol && local->hashed_subvol == prev) {
            local->skip_unlink.handle_valid_link = _gf_true;
            local->skip_unlink.opend_fd_count = fd_count;
            local->skip_unlink.hash_links_to = link_subvol;
            gf_uuid_copy(local->skip_unlink.hashed_gfid, buf->ia_gfid);

            gf_msg_debug(this->name, 0,
                         "Found"
                         " one linkto file on hashed subvol %s "
                         "for %s: Skipping unlinking till "
                         "everywhere_done",
                         prev->name, loc->path);

        } else if (!ret && (fd_count == 0)) {
            dict_req = dict_new();

            ret = dht_fill_dict_to_avoid_unlink_of_migrating_file(dict_req);

            if (ret) {
                /* Skip unlinking for dict_failure
                 *File is found as a linkto file on non-hashed,
                 *subvolume. In the current implementation,
                 *finding a linkto-file on non-hashed does not
                 *always implies that it is stale. So deletion
                 *of file should be done only when both fd is
                 *closed and linkto-xattr is set. In case of
                 *dict_set failure, avoid skipping of file.
                 *NOTE: dht_frame_return should get called for
                 *      this block.
                 */

                dict_unref(dict_req);

            } else {
                gf_msg(this->name, GF_LOG_INFO, 0, DHT_MSG_SUBVOL_INFO,
                       "attempting deletion of stale linkfile "
                       "%s on %s (hashed subvol is %s)",
                       loc->path, prev->name,
                       (local->hashed_subvol ? local->hashed_subvol->name
                                             : "<null>"));
                /* *
                 * These stale files may be created using root
                 * user. Hence deletion will work only with
                 * root.
                 */
                FRAME_SU_DO(frame, dht_local_t);
                STACK_WIND(frame, dht_lookup_unlink_cbk, prev,
                           prev->fops->unlink, loc, 0, dict_req);

                dict_unref(dict_req);

                return 0;
            }
        }
    }

    this_call_cnt = dht_frame_return(frame);
    if (is_last_call(this_call_cnt)) {
        dht_lookup_everywhere_done(frame, this);
    }

out:
    return ret;
}

int
dht_lookup_everywhere(call_frame_t *frame, xlator_t *this, loc_t *loc)
{
    dht_conf_t *conf = NULL;
    dht_local_t *local = NULL;
    int i = 0;
    int call_cnt = 0;

    GF_VALIDATE_OR_GOTO("dht", frame, err);
    GF_VALIDATE_OR_GOTO("dht", this, out);
    GF_VALIDATE_OR_GOTO("dht", frame->local, out);
    GF_VALIDATE_OR_GOTO("dht", this->private, out);
    GF_VALIDATE_OR_GOTO("dht", loc, out);

    conf = this->private;
    local = frame->local;

    call_cnt = conf->subvolume_cnt;
    local->call_cnt = call_cnt;

    if (!local->inode)
        local->inode = inode_ref(loc->inode);

    gf_msg_debug(this->name, 0, "winding lookup call to %d subvols", call_cnt);

    for (i = 0; i < call_cnt; i++) {
        STACK_WIND_COOKIE(frame, dht_lookup_everywhere_cbk, conf->subvolumes[i],
                          conf->subvolumes[i],
                          conf->subvolumes[i]->fops->lookup, loc,
                          local->xattr_req);
    }

    return 0;
out:
    DHT_STACK_UNWIND(lookup, frame, -1, EINVAL, NULL, NULL, NULL, NULL);
err:
    return -1;
}

int
dht_lookup_linkfile_cbk(call_frame_t *frame, void *cookie, xlator_t *this,
                        int op_ret, int op_errno, inode_t *inode,
                        struct iatt *stbuf, dict_t *xattr,
                        struct iatt *postparent)
{
    xlator_t *prev = NULL;
    dht_local_t *local = NULL;
    xlator_t *subvol = NULL;
    loc_t *loc = NULL;
    dht_conf_t *conf = NULL;
    int ret = 0;
    char gfid[GF_UUID_BUF_SIZE] = {0};

    GF_VALIDATE_OR_GOTO("dht", frame, out);
    GF_VALIDATE_OR_GOTO("dht", this, unwind);
    GF_VALIDATE_OR_GOTO("dht", frame->local, unwind);
    GF_VALIDATE_OR_GOTO("dht", this->private, unwind);
    GF_VALIDATE_OR_GOTO("dht", cookie, unwind);

    prev = cookie;
    subvol = prev;
    conf = this->private;
    local = frame->local;
    loc = &local->loc;

    gf_uuid_unparse(loc->gfid, gfid);

    if (op_ret == -1) {
        gf_msg(this->name, GF_LOG_INFO, op_errno, DHT_MSG_LINK_FILE_LOOKUP_INFO,
               "Lookup of %s on %s (following linkfile) failed "
               ",gfid = %s",
               local->loc.path, subvol->name, gfid);

        /* If cached subvol returned ENOTCONN, do not do
        lookup_everywhere. We need to make sure linkfile does not get
        removed, which can take away the namespace, and subvol is
        anyways down. */

        local->cached_subvol = NULL;
        if (op_errno != ENOTCONN)
            goto err;
        else
            goto unwind;
    }

    if (check_is_dir(inode, stbuf, xattr)) {
        gf_msg(this->name, GF_LOG_INFO, 0, DHT_MSG_LINK_FILE_LOOKUP_INFO,
               "Lookup of %s on %s (following linkfile) reached dir,"
               " gfid = %s",
               local->loc.path, subvol->name, gfid);
        goto err;
    }

    if (check_is_linkfile(inode, stbuf, xattr, conf->link_xattr_name)) {
        gf_msg(this->name, GF_LOG_INFO, 0, DHT_MSG_LINK_FILE_LOOKUP_INFO,
               "lookup of %s on %s (following linkfile) reached link,"
               "gfid = %s",
               local->loc.path, subvol->name, gfid);
        goto err;
    }

    if (gf_uuid_compare(local->gfid, stbuf->ia_gfid)) {
        gf_msg(this->name, GF_LOG_WARNING, 0, DHT_MSG_GFID_MISMATCH,
               "%s: gfid different on data file on %s,"
               " gfid local = %s, gfid node = %s ",
               local->loc.path, subvol->name, gfid, uuid_utoa(stbuf->ia_gfid));
        goto err;
    }

    if ((stbuf->ia_nlink == 1) && (conf && conf->unhashed_sticky_bit)) {
        stbuf->ia_prot.sticky = 1;
    }

    ret = dht_layout_preset(this, prev, inode);
    if (ret < 0) {
        gf_msg(this->name, GF_LOG_INFO, 0, DHT_MSG_LAYOUT_PRESET_FAILED,
               "Failed to set layout for subvolume %s,"
               "gfid = %s",
               prev->name, gfid);
        op_ret = -1;
        op_errno = EINVAL;
    }

    if (local->loc.parent) {
        dht_inode_ctx_time_update(local->loc.parent, this, postparent, 1);
    }

unwind:
    DHT_STRIP_PHASE1_FLAGS(stbuf);
    dht_set_fixed_dir_stat(postparent);
    DHT_STACK_UNWIND(lookup, frame, op_ret, op_errno, inode, stbuf, xattr,
                     postparent);

    return 0;

err:
    dht_lookup_everywhere(frame, this, loc);
out:
    return 0;
}

/* Code to get hashed subvol based on inode and loc
   First it check if loc->parent and loc->path exist then it get
   hashed subvol based on loc.
*/

static gf_boolean_t
dht_should_lookup_everywhere(xlator_t *this, dht_conf_t *conf, loc_t *loc)
{
    dht_layout_t *parent_layout = NULL;
    int ret = 0;
    gf_boolean_t lookup_everywhere = _gf_true;

    /* lookup-optimize supersedes lookup-unhashed settings.
     * If it is set, do not process search_unhashed
     * If lookup-optimize if enabled, lookup everywhere if:
     *  - this is the rebalance daemon.
     *  - loc->parent is unavailable.
     *  - parent_layout is unavailable
     *  - parent_layout->commit_hash != conf->vol_commit_hash
     */

    if (conf->lookup_optimize) {
        if (!conf->defrag && loc->parent) {
            ret = dht_inode_ctx_layout_get(loc->parent, this, &parent_layout);
            if (!ret && parent_layout &&
                (parent_layout->commit_hash == conf->vol_commit_hash)) {
                lookup_everywhere = _gf_false;
            }
        }
        goto out;
    } else {
        if (conf->search_unhashed == GF_DHT_LOOKUP_UNHASHED_AUTO) {
            if (loc->parent) {
                ret = dht_inode_ctx_layout_get(loc->parent, this,
                                               &parent_layout);
                if (ret || !parent_layout ||
                    (!parent_layout->search_unhashed)) {
                    lookup_everywhere = _gf_false;
                }
            } else {
                lookup_everywhere = _gf_false;
            }

            goto out;
        }
    }
out:
    return lookup_everywhere;
}

int
dht_lookup_cbk(call_frame_t *frame, void *cookie, xlator_t *this, int op_ret,
               int op_errno, inode_t *inode, struct iatt *stbuf, dict_t *xattr,
               struct iatt *postparent)
{
    char is_linkfile = 0;
    char is_dir = 0;
    xlator_t *subvol = NULL;
    dht_conf_t *conf = NULL;
    dht_local_t *local = NULL;
    loc_t *loc = NULL;
    xlator_t *prev = NULL;
    int ret = 0;
    uint32_t vol_commit_hash = 0;

    GF_VALIDATE_OR_GOTO("dht", frame, err);
    GF_VALIDATE_OR_GOTO("dht", this, out);
    GF_VALIDATE_OR_GOTO("dht", frame->local, out);
    GF_VALIDATE_OR_GOTO("dht", cookie, out);
    GF_VALIDATE_OR_GOTO("dht", this->private, out);

    conf = this->private;

    prev = cookie;
    local = frame->local;
    loc = &local->loc;

    gf_msg_debug(this->name, op_errno,
                 "%s: fresh_lookup on %s returned with op_ret %d", loc->path,
                 prev->name, op_ret);

    if (op_ret == -1) {
        if (ENTRY_MISSING(op_ret, op_errno)) {
            if (1 == conf->subvolume_cnt) {
                /* No need to lookup again */
                goto out;
            }

            gf_msg_debug(this->name, 0, "Entry %s missing on subvol %s",
                         loc->path, prev->name);

            if (dht_should_lookup_everywhere(this, conf, loc)) {
                local->op_errno = ENOENT;
                dht_lookup_everywhere(frame, this, loc);
                return 0;
            }

        } else {
            /* posix returns ENODATA if the gfid is not set but the client and
             * server protocol layers do not send the stbuf. We need to
             * heal this so check if this is a directory on the other subvols.
             */
            if ((op_errno == ENOTCONN) || (op_errno == ENODATA)) {
                dht_lookup_directory(frame, this, &local->loc);
                return 0;
            }
        }
        gf_msg_debug(this->name, op_errno, "%s: Lookup on subvolume %s failed",
                     loc->path, prev->name);
        goto out;
    }

    /* Lookup succeeded - op_ret = 0 */

    /* This is required for handling stale linkfile deletion,
     * or any more call which happens from this 'loc'.
     */
    if (gf_uuid_is_null(local->gfid)) {
        /*This is set from the first successful response*/
        memcpy(local->gfid, stbuf->ia_gfid, 16);
    }

    if (!conf->vch_forced) {
        /* Update the commit hash in conf if it is found */
        ret = dict_get_uint32(xattr, conf->commithash_xattr_name,
                              &vol_commit_hash);
        if (ret == 0) {
            conf->vol_commit_hash = vol_commit_hash;
        }
    }

    is_dir = check_is_dir(inode, stbuf, xattr);
    if (is_dir) {
        /* A directory is present on all subvols, send the lookup to
         * all subvols now */
        local->inode = inode_ref(inode);
        local->xattr = dict_ref(xattr);
        dht_lookup_directory(frame, this, &local->loc);
        return 0;
    }

    is_linkfile = check_is_linkfile(inode, stbuf, xattr, conf->link_xattr_name);

    if (!is_linkfile) {
        /* non-directory and not a linkto file. This is a data file
         * Update the layout to point to the cached subvol
         */

        ret = dht_layout_preset(this, prev, inode);
        if (ret < 0) {
            gf_msg(this->name, GF_LOG_INFO, 0, DHT_MSG_LAYOUT_PRESET_FAILED,
                   "%s: could not set pre-set layout for subvolume %s",
                   loc->path, prev->name);
            op_ret = -1;
            op_errno = EINVAL;
            goto out;
        }
        goto out;
    }

    /* This is a linkto file. Get the value of the target subvol from the
     * linkto xattr and lookup there to see if the file exists
     */
    subvol = dht_linkfile_subvol(this, inode, stbuf, xattr);
    if (!subvol) {
        gf_msg(this->name, GF_LOG_INFO, 0, DHT_MSG_SUBVOL_INFO,
               "%s: No link subvol for linkto", loc->path);
        dht_lookup_everywhere(frame, this, loc);
        return 0;
    }

    gf_msg_debug(this->name, 0, "%s: Calling lookup on linkto target %s",
                 loc->path, subvol->name);

    STACK_WIND_COOKIE(frame, dht_lookup_linkfile_cbk, subvol, subvol,
                      subvol->fops->lookup, &local->loc, local->xattr_req);

    return 0;

out:
    /*
     * FIXME: postparent->ia_size and postparent->st_blocks do not have
     * correct values. since, postparent corresponds to a directory these
     * two members should have values equal to sum of corresponding values
     * from each of the subvolume. See dht_iatt_merge for reference.
     */

    if (!op_ret && local && local->loc.parent) {
        dht_inode_ctx_time_update(local->loc.parent, this, postparent, 1);
    }

    DHT_STRIP_PHASE1_FLAGS(stbuf);
    dht_set_fixed_dir_stat(postparent);
    DHT_STACK_UNWIND(lookup, frame, op_ret, op_errno, inode, stbuf, xattr,
                     postparent);
err:
    return 0;
}

/* For directories, check if acl xattrs have been requested (by the acl
 * xlator), if not, request for them. These xattrs are needed for dht dir
 * self-heal to perform proper self-healing of dirs
 */
static void
dht_check_and_set_acl_xattr_req(xlator_t *this, dict_t *xattr_req)
{
    int ret = 0;

    GF_ASSERT(xattr_req);

    if (!dict_get(xattr_req, POSIX_ACL_ACCESS_XATTR)) {
        ret = dict_set_int8(xattr_req, POSIX_ACL_ACCESS_XATTR, 0);
        if (ret)
            gf_msg(this->name, GF_LOG_WARNING, -ret, DHT_MSG_DICT_SET_FAILED,
                   "Failed to set dictionary value:key = %s",
                   POSIX_ACL_ACCESS_XATTR);
    }

    if (!dict_get(xattr_req, POSIX_ACL_DEFAULT_XATTR)) {
        ret = dict_set_int8(xattr_req, POSIX_ACL_DEFAULT_XATTR, 0);
        if (ret)
            gf_msg(this->name, GF_LOG_WARNING, -ret, DHT_MSG_DICT_SET_FAILED,
                   "Failed to set dictionary value:key = %s",
                   POSIX_ACL_DEFAULT_XATTR);
    }

    return;
}

/* for directories, we need the following info:
 * the layout : trusted.glusterfs.dht
 * the mds information : trusted.glusterfs.dht.mds
 * the acl info: See above
 */
static int
dht_set_dir_xattr_req(xlator_t *this, loc_t *loc, dict_t *xattr_req)
{
    int ret = -EINVAL;
    dht_conf_t *conf = NULL;

    conf = this->private;
    if (!conf) {
        goto err;
    }

    if (!xattr_req) {
        goto err;
    }

    /* Xattr to get the layout for a directory
     */
    ret = dict_set_uint32(xattr_req, conf->xattr_name, 4 * 4);
    if (ret) {
        gf_msg(this->name, GF_LOG_WARNING, ENOMEM, DHT_MSG_DICT_SET_FAILED,
               "Failed to set dictionary value:key = %s for "
               "path %s",
               conf->xattr_name, loc->path);
        goto err;
    }

    /*Non-fatal failure */
    ret = dict_set_uint32(xattr_req, conf->mds_xattr_key, 4);
    if (ret) {
        gf_msg(this->name, GF_LOG_WARNING, ENOMEM, DHT_MSG_DICT_SET_FAILED,
               "Failed to set dictionary value:key = %s for "
               "path %s",
               conf->mds_xattr_key, loc->path);
    }

    dht_check_and_set_acl_xattr_req(this, xattr_req);
    ret = 0;
err:
    return ret;
}

/* If the hashed subvol is present, send the lookup to only that subvol first.
 * If no hashed subvol, send a lookup to all subvols and proceed based on the
 * responses.
 */
static int
dht_do_fresh_lookup(call_frame_t *frame, xlator_t *this, loc_t *loc)
{
    int ret = -1;
    dht_conf_t *conf = NULL;
    xlator_t *hashed_subvol = NULL;
    dht_local_t *local = NULL;
    int op_errno = -1;
    int call_cnt = 0;
    int i = 0;

    conf = this->private;
    if (!conf) {
        op_errno = EINVAL;
        goto err;
    }

    local = frame->local;
    if (!local) {
        op_errno = EINVAL;
        goto err;
    }

    /* Since we don't know whether this is a file or a directory,
     * request all xattrs*/
    ret = dht_set_file_xattr_req(this, loc, local->xattr_req);
    if (ret) {
        op_errno = -ret;
        goto err;
    }

    ret = dht_set_dir_xattr_req(this, loc, local->xattr_req);
    if (ret) {
        op_errno = -ret;
        goto err;
    }

    /* Fuse sets a random value in gfid-req. If the gfid is missing
     * on one or more subvols, posix will set the gfid to this value,
     * causing GFID mismatches for directories. Remove the value fuse
     * has sent before sending the lookup.
     */
    ret = dict_get_gfuuid(local->xattr_req, "gfid-req", &local->gfid_req);
    if (ret) {
        gf_msg_debug(this->name, 0, "%s: No gfid-req available", loc->path);
    } else {
        dict_del(local->xattr_req, "gfid-req");
    }
    /* This should have been set in dht_lookup */
    hashed_subvol = local->hashed_subvol;

    if (!hashed_subvol) {
        gf_msg_debug(this->name, 0,
                     "%s: no subvolume in layout for path, "
                     "checking on all the subvols to see if "
                     "it is a directory",
                     loc->path);

        call_cnt = conf->subvolume_cnt;
        local->call_cnt = call_cnt;

        /* Allocate a layout. This will be populated and saved in
         * the dht inode_ctx on successful lookup
         */
        local->layout = dht_layout_new(this, conf->subvolume_cnt);
        if (!local->layout) {
            op_errno = ENOMEM;
            goto err;
        }

        gf_msg_debug(this->name, 0,
                     "%s: Found null hashed subvol. Calling lookup"
                     " on all nodes.",
                     loc->path);

        for (i = 0; i < call_cnt; i++) {
            STACK_WIND_COOKIE(frame, dht_lookup_dir_cbk, conf->subvolumes[i],
                              conf->subvolumes[i],
                              conf->subvolumes[i]->fops->lookup, &local->loc,
                              local->xattr_req);
        }
        return 0;
    }

    /* if the hashed_subvol is non-null, send the lookup there first so
     * as to see whether we have a file or a directory */
    gf_msg_debug(this->name, 0, "%s: Calling fresh lookup on %s", loc->path,
                 hashed_subvol->name);

    STACK_WIND_COOKIE(frame, dht_lookup_cbk, hashed_subvol, hashed_subvol,
                      hashed_subvol->fops->lookup, loc, local->xattr_req);
    return 0;
err:
    op_errno = (op_errno == -1) ? errno : op_errno;
    DHT_STACK_UNWIND(lookup, frame, -1, op_errno, NULL, NULL, NULL, NULL);
    return 0;
}

static int
dht_do_revalidate(call_frame_t *frame, xlator_t *this, loc_t *loc)
{
    xlator_t *subvol = NULL;
    xlator_t *mds_subvol = NULL;
    dht_local_t *local = NULL;
    dht_conf_t *conf = NULL;
    int ret = -1;
    int op_errno = -1;
    dht_layout_t *layout = NULL;
    int i = 0;
    int call_cnt = 0;
    int gen = 0;

    conf = this->private;
    if (!conf) {
        op_errno = EINVAL;
        goto err;
    }

    local = frame->local;
    if (!local) {
        op_errno = EINVAL;
        goto err;
    }

    layout = local->layout;
    if (!layout) {
        gf_msg_debug(this->name, 0,
                     "path = %s. No layout found in the inode ctx.", loc->path);
        op_errno = EINVAL;
        goto err;
    }

    /* Generation number has changed. This layout may be stale. */
    if (layout->gen && (layout->gen < conf->gen)) {
        gen = layout->gen;
        dht_layout_unref(this, local->layout);
        local->layout = NULL;
        local->cached_subvol = NULL;

        gf_msg_debug(this->name, 0,
                     "path = %s. In memory layout may be stale."
                     "(layout->gen (%d) is less than "
                     "conf->gen (%d)). Calling fresh lookup.",
                     loc->path, gen, conf->gen);

        dht_do_fresh_lookup(frame, this, loc);
        return 0;
    }

    local->inode = inode_ref(loc->inode);

    /* Since we don't know whether this has changed,
     * request all xattrs*/
    ret = dht_set_file_xattr_req(this, loc, local->xattr_req);
    if (ret) {
        op_errno = -ret;
        goto err;
    }

    ret = dht_set_dir_xattr_req(this, loc, local->xattr_req);
    if (ret) {
        op_errno = -ret;
        goto err;
    }

    if (IA_ISDIR(local->inode->ia_type)) {
        ret = dht_inode_ctx_mdsvol_get(local->inode, this, &mds_subvol);
        if (ret || !mds_subvol) {
            gf_msg_debug(this->name, 0, "path = %s. No mds subvol in inode ctx",
                         local->loc.path);
        }
        local->mds_subvol = mds_subvol;
        local->call_cnt = conf->subvolume_cnt;

        /* local->call_cnt will change as responses are processed. Always use a
         * local copy to loop through the STACK_WIND calls
         */

        call_cnt = local->call_cnt;

        for (i = 0; i < call_cnt; i++) {
            STACK_WIND_COOKIE(frame, dht_revalidate_cbk, conf->subvolumes[i],
                              conf->subvolumes[i],
                              conf->subvolumes[i]->fops->lookup, loc,
                              local->xattr_req);
        }
        return 0;
    }

    /* If not a dir, this should be 1 */
    local->call_cnt = layout->cnt;
    call_cnt = local->call_cnt;

    for (i = 0; i < call_cnt; i++) {
        subvol = layout->list[i].xlator;

        gf_msg_debug(this->name, 0,
                     "path = %s. Calling "
                     "revalidate lookup on %s",
                     loc->path, subvol->name);

        STACK_WIND_COOKIE(frame, dht_revalidate_cbk, subvol, subvol,
                          subvol->fops->lookup, &local->loc, local->xattr_req);
    }
    return 0;
err:
    op_errno = (op_errno == -1) ? errno : op_errno;
    DHT_STACK_UNWIND(lookup, frame, -1, op_errno, NULL, NULL, NULL, NULL);
    return 0;
}

/* Depending on the input, decide if this is a:
 * fresh-lookup: loc->name is provided but no dht inode ctx
 * revalidation: loc->name is provided, dht inode ctx is present
 * discover: gfid based nameless lookup.
 */

int
dht_lookup(call_frame_t *frame, xlator_t *this, loc_t *loc, dict_t *xattr_req)
{
    xlator_t *hashed_subvol = NULL;
    dht_local_t *local = NULL;
    dht_conf_t *conf = NULL;
    int ret = -1;
    int op_errno = -1;
    loc_t new_loc = {
        0,
    };

    VALIDATE_OR_GOTO(frame, err);
    VALIDATE_OR_GOTO(this, err);
    VALIDATE_OR_GOTO(loc, err);
    VALIDATE_OR_GOTO(loc->inode, err);

    conf = this->private;
    if (!conf)
        goto err;

    local = dht_local_init(frame, loc, NULL, GF_FOP_LOOKUP);
    if (!local) {
        op_errno = ENOMEM;
        goto err;
    }

    ret = dht_filter_loc_subvol_key(this, loc, &new_loc, &hashed_subvol);
    if (ret) {
        loc_wipe(&local->loc);
        ret = loc_dup(&new_loc, &local->loc);

        /* we no longer need 'new_loc' entries */
        loc_wipe(&new_loc);

        /* check if loc_dup() is successful */
        if (ret == -1) {
            op_errno = errno;
            gf_msg_debug(this->name, errno,
                         "copying location failed for path=%s", loc->path);
            goto err;
        }
    }

    if (xattr_req) {
        local->xattr_req = dict_ref(xattr_req);
    } else {
        local->xattr_req = dict_new();
    }

    /* Nameless lookup */

    /* This is usually sent by NFS. Lookups are done based on the gfid and
     * no name information is available. Without the name, dht cannot calculate
     * the hash and has to send a lookup to all subvols.
     */
    if (gf_uuid_is_null(loc->pargfid) && !gf_uuid_is_null(loc->gfid) &&
        !__is_root_gfid(loc->inode->gfid)) {
        local->cached_subvol = NULL;
        dht_do_discover(frame, this, loc);
        return 0;
    }

    if (loc_is_root(loc)) {
        /* Request the DHT commit hash xattr (trusted.glusterfs.dht.commithash)
         * set on the brick root.
         */
        ret = dict_set_uint32(local->xattr_req, conf->commithash_xattr_name,
                              sizeof(uint32_t));
    }

    if (!hashed_subvol)
        hashed_subvol = dht_subvol_get_hashed(this, loc);
    local->hashed_subvol = hashed_subvol;

    if (is_revalidate(loc)) {
        /* The entry has been looked up before and has a dht inode_ctx
         */
        dht_do_revalidate(frame, this, loc);
        return 0;
    } else {
        /* Entry has not been looked up before
         */
        dht_do_fresh_lookup(frame, this, loc);
        return 0;
    }

    return 0;
err:
    op_errno = (op_errno == -1) ? errno : op_errno;
    DHT_STACK_UNWIND(lookup, frame, -1, op_errno, NULL, NULL, NULL, NULL);
    return 0;
}

static int
dht_unlink_linkfile_cbk(call_frame_t *frame, void *cookie, xlator_t *this,
                        int op_ret, int op_errno, struct iatt *preparent,
                        struct iatt *postparent, dict_t *xdata)
{
    dht_local_t *local = NULL;
    xlator_t *prev = NULL;

    local = frame->local;
    prev = cookie;

    LOCK(&frame->lock);
    {
        if ((op_ret == -1) &&
            !((op_errno == ENOENT) || (op_errno == ENOTCONN))) {
            local->op_errno = op_errno;
            UNLOCK(&frame->lock);
            gf_msg_debug(this->name, op_errno,
                         "Unlink link: subvolume %s returned -1", prev->name);
            goto post_unlock;
        }

        local->op_ret = 0;
    }
    UNLOCK(&frame->lock);
post_unlock:
    dht_set_fixed_dir_stat(&local->preparent);
    dht_set_fixed_dir_stat(&local->postparent);
    DHT_STACK_UNWIND(unlink, frame, local->op_ret, local->op_errno,
                     &local->preparent, &local->postparent, xdata);

    return 0;
}

static int
dht_unlink_cbk(call_frame_t *frame, void *cookie, xlator_t *this, int op_ret,
               int op_errno, struct iatt *preparent, struct iatt *postparent,
               dict_t *xdata)
{
    dht_local_t *local = NULL;
    xlator_t *prev = NULL;
    xlator_t *hashed_subvol = NULL;

    local = frame->local;
    prev = cookie;

    LOCK(&frame->lock);
    {
        if (op_ret == -1) {
            if (op_errno != ENOENT) {
                local->op_ret = -1;
                local->op_errno = op_errno;
            } else {
                local->op_ret = 0;
            }
            UNLOCK(&frame->lock);
            gf_msg_debug(this->name, op_errno,
                         "Unlink: subvolume %s returned -1", prev->name);
            goto post_unlock;
        }

        local->op_ret = 0;

        local->postparent = *postparent;
        local->preparent = *preparent;

        if (local->loc.parent) {
            dht_inode_ctx_time_update(local->loc.parent, this,
                                      &local->preparent, 0);
            dht_inode_ctx_time_update(local->loc.parent, this,
                                      &local->postparent, 1);
        }
    }
    UNLOCK(&frame->lock);
post_unlock:
    if (!local->op_ret) {
        hashed_subvol = dht_subvol_get_hashed(this, &local->loc);
        if (hashed_subvol && hashed_subvol != local->cached_subvol) {
            /*
             * If hashed and cached are different, then we need
             * to unlink linkfile from hashed subvol if data
             * file is deleted successfully
             */
            STACK_WIND_COOKIE(frame, dht_unlink_linkfile_cbk, hashed_subvol,
                              hashed_subvol, hashed_subvol->fops->unlink,
                              &local->loc, local->flags, xdata);
            return 0;
        }
    }

    dht_set_fixed_dir_stat(&local->preparent);
    dht_set_fixed_dir_stat(&local->postparent);
    DHT_STACK_UNWIND(unlink, frame, local->op_ret, local->op_errno,
                     &local->preparent, &local->postparent, xdata);

    return 0;
}

static int
dht_common_setxattr_cbk(call_frame_t *frame, void *cookie, xlator_t *this,
                        int32_t op_ret, int32_t op_errno, dict_t *xdata)
{
    DHT_STACK_UNWIND(setxattr, frame, op_ret, op_errno, xdata);
    return 0;
}

static int
dht_fix_layout_setxattr_cbk(call_frame_t *frame, void *cookie, xlator_t *this,
                            int32_t op_ret, int32_t op_errno, dict_t *xdata)
{
    dht_local_t *local = NULL;
    dht_layout_t *layout = NULL;

    if (op_ret == 0) {
        /* update the layout in the inode ctx */
        local = frame->local;
        layout = local->selfheal.layout;

        dht_layout_set(this, local->loc.inode, layout);
    }

    DHT_STACK_UNWIND(setxattr, frame, op_ret, op_errno, xdata);
    return 0;
}

static int
dht_err_cbk(call_frame_t *frame, void *cookie, xlator_t *this, int op_ret,
            int op_errno, dict_t *xdata)
{
    dht_local_t *local = NULL;
    int this_call_cnt = 0;
    xlator_t *prev = NULL;

    local = frame->local;
    prev = cookie;

    LOCK(&frame->lock);
    {
        if (op_ret == -1) {
            local->op_errno = op_errno;
            UNLOCK(&frame->lock);
            gf_msg_debug(this->name, op_errno, "subvolume %s returned -1",
                         prev->name);
            goto post_unlock;
        }

        local->op_ret = 0;
    }
    UNLOCK(&frame->lock);
post_unlock:
    this_call_cnt = dht_frame_return(frame);
    if (is_last_call(this_call_cnt)) {
        if ((local->fop == GF_FOP_SETXATTR) ||
            (local->fop == GF_FOP_FSETXATTR)) {
            DHT_STACK_UNWIND(setxattr, frame, local->op_ret, local->op_errno,
                             NULL);
            /* 'local' itself may not be valid after this */
            goto out;
        }
        if ((local->fop == GF_FOP_REMOVEXATTR) ||
            (local->fop == GF_FOP_FREMOVEXATTR)) {
            DHT_STACK_UNWIND(removexattr, frame, local->op_ret, local->op_errno,
                             NULL);
        }
    }

out:
    return 0;
}

/* Set the value[] of key into dict after convert from
   host byte order to network byte order
*/
int32_t
dht_dict_set_array(dict_t *dict, char *key, int32_t value[], int32_t size)
{
    int ret = -1;
    int32_t *ptr = NULL;
    int32_t vindex;

    if (value == NULL) {
        return -EINVAL;
    }

    ptr = GF_MALLOC(sizeof(int32_t) * size, gf_common_mt_char);
    if (ptr == NULL) {
        return -ENOMEM;
    }
    for (vindex = 0; vindex < size; vindex++) {
        ptr[vindex] = hton32(value[vindex]);
    }
    ret = dict_set_bin(dict, key, ptr, sizeof(int32_t) * size);
    if (ret)
        GF_FREE(ptr);
    return ret;
}

static int
dht_common_mds_xattrop_cbk(call_frame_t *frame, void *cookie, xlator_t *this,
                           int32_t op_ret, int32_t op_errno, dict_t *dict,
                           dict_t *xdata)
{
    dht_local_t *local = NULL;
    call_frame_t *prev = cookie;

    local = frame->local;

    if (op_ret)
        gf_msg_debug(this->name, op_errno, "subvolume %s returned -1",
                     prev->this->name);

    if (local->fop == GF_FOP_SETXATTR) {
        DHT_STACK_UNWIND(setxattr, frame, 0, op_errno, local->xdata);
        /* 'local' itself may not be valid after this */
        goto out;
    }

    if (local->fop == GF_FOP_FSETXATTR) {
        DHT_STACK_UNWIND(fsetxattr, frame, 0, op_errno, local->xdata);
        /* 'local' itself may not be valid after this */
        goto out;
    }

    if (local->fop == GF_FOP_REMOVEXATTR) {
        DHT_STACK_UNWIND(removexattr, frame, 0, op_errno, NULL);
        /* 'local' itself may not be valid after this */
        goto out;
    }

    if (local->fop == GF_FOP_FREMOVEXATTR) {
        DHT_STACK_UNWIND(fremovexattr, frame, 0, op_errno, NULL);
    }

out:
    return 0;
}

/* Code to wind a xattrop call to add 1 on current mds internal xattr
   value
*/
static int
dht_setxattr_non_mds_cbk(call_frame_t *frame, void *cookie, xlator_t *this,
                         int op_ret, int op_errno, dict_t *xdata)
{
    dht_local_t *local = NULL;
    int this_call_cnt = 0;
    int ret = 0;
    dict_t *xattrop = NULL;
    int32_t addone[1] = {1};
    call_frame_t *prev = NULL;
    dht_conf_t *conf = NULL;

    local = frame->local;
    prev = cookie;
    conf = this->private;

    LOCK(&frame->lock);
    {
        if (op_ret && !local->op_ret) {
            local->op_ret = op_ret;
            local->op_errno = op_errno;
            UNLOCK(&frame->lock);
            gf_msg_debug(this->name, op_errno, "subvolume %s returned -1",
                         prev->this->name);
            goto post_unlock;
        }
    }
    UNLOCK(&frame->lock);
post_unlock:
    this_call_cnt = dht_frame_return(frame);

    if (is_last_call(this_call_cnt)) {
        if (!local->op_ret) {
            xattrop = dict_new();
            if (!xattrop) {
                gf_msg(this->name, GF_LOG_ERROR, DHT_MSG_NO_MEMORY, 0,
                       "dictionary creation failed");
                ret = -1;
                goto out;
            }
            ret = dht_dict_set_array(xattrop, conf->mds_xattr_key, addone, 1);
            if (ret != 0) {
                gf_msg(this->name, GF_LOG_ERROR, 0, DHT_MSG_DICT_SET_FAILED,
                       "dictionary set array failed ");
                ret = -1;
                goto out;
            }
            if ((local->fop == GF_FOP_SETXATTR) ||
                (local->fop == GF_FOP_REMOVEXATTR)) {
                STACK_WIND(frame, dht_common_mds_xattrop_cbk, local->mds_subvol,
                           local->mds_subvol->fops->xattrop, &local->loc,
                           GF_XATTROP_ADD_ARRAY, xattrop, NULL);
            } else {
                STACK_WIND(frame, dht_common_mds_xattrop_cbk, local->mds_subvol,
                           local->mds_subvol->fops->fxattrop, local->fd,
                           GF_XATTROP_ADD_ARRAY, xattrop, NULL);
            }
        } else {
            if (local->fop == GF_FOP_SETXATTR) {
                DHT_STACK_UNWIND(setxattr, frame, 0, 0, local->xdata);
                /* 'local' itself may not be valid after this */
                goto just_return;
            }

            if (local->fop == GF_FOP_FSETXATTR) {
                DHT_STACK_UNWIND(fsetxattr, frame, 0, 0, local->xdata);
                /* 'local' itself may not be valid after this */
                goto just_return;
            }

            if (local->fop == GF_FOP_REMOVEXATTR) {
                DHT_STACK_UNWIND(removexattr, frame, 0, 0, NULL);
                /* 'local' itself may not be valid after this */
                goto just_return;
            }

            if (local->fop == GF_FOP_FREMOVEXATTR) {
                DHT_STACK_UNWIND(fremovexattr, frame, 0, 0, NULL);
                /* 'local' itself may not be valid after this */
                goto just_return;
            }
        }
    }
out:
    if (ret) {
        if (local->fop == GF_FOP_SETXATTR) {
            DHT_STACK_UNWIND(setxattr, frame, 0, 0, local->xdata);
            /* 'local' itself may not be valid after this */
            goto just_return;
        }

        if (local->fop == GF_FOP_FSETXATTR) {
            DHT_STACK_UNWIND(fsetxattr, frame, 0, 0, local->xdata);
            /* 'local' itself may not be valid after this */
            goto just_return;
        }

        if (local->fop == GF_FOP_REMOVEXATTR) {
            DHT_STACK_UNWIND(removexattr, frame, 0, 0, NULL);
            /* 'local' itself may not be valid after this */
            goto just_return;
        }

        if (local->fop == GF_FOP_FREMOVEXATTR) {
            DHT_STACK_UNWIND(fremovexattr, frame, 0, 0, NULL);
        }
    }
just_return:
    if (xattrop)
        dict_unref(xattrop);
    return 0;
}

static int
dht_setxattr_mds_cbk(call_frame_t *frame, void *cookie, xlator_t *this,
                     int op_ret, int op_errno, dict_t *xdata)
{
    dht_local_t *local = NULL;
    dht_conf_t *conf = NULL;
    call_frame_t *prev = NULL;
    xlator_t *mds_subvol = NULL;
    int i = 0;

    local = frame->local;
    prev = cookie;
    conf = this->private;
    mds_subvol = local->mds_subvol;

    if (op_ret == -1) {
        local->op_ret = op_ret;
        local->op_errno = op_errno;
        gf_msg_debug(this->name, op_errno, "subvolume %s returned -1",
                     prev->this->name);
        goto out;
    }

    local->op_ret = 0;
    local->call_cnt = conf->subvolume_cnt - 1;
    local->xdata = dict_ref(xdata);

    for (i = 0; i < conf->subvolume_cnt; i++) {
        if (mds_subvol && (mds_subvol == conf->subvolumes[i]))
            continue;
        if (local->fop == GF_FOP_SETXATTR) {
            STACK_WIND(frame, dht_setxattr_non_mds_cbk, conf->subvolumes[i],
                       conf->subvolumes[i]->fops->setxattr, &local->loc,
                       local->xattr, local->flags, local->xattr_req);
        }

        if (local->fop == GF_FOP_FSETXATTR) {
            STACK_WIND(frame, dht_setxattr_non_mds_cbk, conf->subvolumes[i],
                       conf->subvolumes[i]->fops->fsetxattr, local->fd,
                       local->xattr, local->flags, local->xattr_req);
        }

        if (local->fop == GF_FOP_REMOVEXATTR) {
            STACK_WIND(frame, dht_setxattr_non_mds_cbk, conf->subvolumes[i],
                       conf->subvolumes[i]->fops->removexattr, &local->loc,
                       local->key, local->xattr_req);
        }

        if (local->fop == GF_FOP_FREMOVEXATTR) {
            STACK_WIND(frame, dht_setxattr_non_mds_cbk, conf->subvolumes[i],
                       conf->subvolumes[i]->fops->fremovexattr, local->fd,
                       local->key, local->xattr_req);
        }
    }

    return 0;
out:
    if (local->fop == GF_FOP_SETXATTR) {
        DHT_STACK_UNWIND(setxattr, frame, local->op_ret, local->op_errno,
                         xdata);
        /* 'local' itself may not be valid after this */
        goto just_return;
    }

    if (local->fop == GF_FOP_FSETXATTR) {
        DHT_STACK_UNWIND(fsetxattr, frame, local->op_ret, local->op_errno,
                         xdata);
        /* 'local' itself may not be valid after this */
        goto just_return;
    }

    if (local->fop == GF_FOP_REMOVEXATTR) {
        DHT_STACK_UNWIND(removexattr, frame, local->op_ret, local->op_errno,
                         NULL);
        /* 'local' itself may not be valid after this */
        goto just_return;
    }

    if (local->fop == GF_FOP_FREMOVEXATTR) {
        DHT_STACK_UNWIND(fremovexattr, frame, local->op_ret, local->op_errno,
                         NULL);
    }

just_return:
    return 0;
}

static int
dht_xattrop_mds_cbk(call_frame_t *frame, void *cookie, xlator_t *this,
                    int op_ret, int op_errno, dict_t *dict, dict_t *xdata)
{
    dht_local_t *local = NULL;
    call_frame_t *prev = NULL;

    local = frame->local;
    prev = cookie;

    if (op_ret == -1) {
        local->op_errno = op_errno;
        local->op_ret = op_ret;
        gf_msg_debug(this->name, op_errno, "subvolume %s returned -1",
                     prev->this->name);
        goto out;
    }

    if (local->fop == GF_FOP_SETXATTR) {
        STACK_WIND(frame, dht_setxattr_mds_cbk, local->mds_subvol,
                   local->mds_subvol->fops->setxattr, &local->loc, local->xattr,
                   local->flags, local->xattr_req);
    }

    if (local->fop == GF_FOP_FSETXATTR) {
        STACK_WIND(frame, dht_setxattr_mds_cbk, local->mds_subvol,
                   local->mds_subvol->fops->fsetxattr, local->fd, local->xattr,
                   local->flags, local->xattr_req);
    }

    if (local->fop == GF_FOP_REMOVEXATTR) {
        STACK_WIND(frame, dht_setxattr_mds_cbk, local->mds_subvol,
                   local->mds_subvol->fops->removexattr, &local->loc,
                   local->key, local->xattr_req);
    }

    if (local->fop == GF_FOP_FREMOVEXATTR) {
        STACK_WIND(frame, dht_setxattr_mds_cbk, local->mds_subvol,
                   local->mds_subvol->fops->fremovexattr, local->fd, local->key,
                   local->xattr_req);
    }

    return 0;
out:
    if (local->fop == GF_FOP_SETXATTR) {
        DHT_STACK_UNWIND(setxattr, frame, local->op_ret, local->op_errno,
                         xdata);
        /* 'local' itself may not be valid after this */
        goto just_return;
    }

    if (local->fop == GF_FOP_FSETXATTR) {
        DHT_STACK_UNWIND(fsetxattr, frame, local->op_ret, local->op_errno,
                         xdata);
        /* 'local' itself may not be valid after this */
        goto just_return;
    }

    if (local->fop == GF_FOP_REMOVEXATTR) {
        DHT_STACK_UNWIND(removexattr, frame, local->op_ret, local->op_errno,
                         NULL);
        /* 'local' itself may not be valid after this */
        goto just_return;
    }

    if (local->fop == GF_FOP_FREMOVEXATTR) {
        DHT_STACK_UNWIND(fremovexattr, frame, local->op_ret, local->op_errno,
                         NULL);
    }

just_return:
    return 0;
}

static void
fill_layout_info(dht_layout_t *layout, char *buf)
{
    int i = 0;
    char tmp_buf[128] = {
        0,
    };

    for (i = 0; i < layout->cnt; i++) {
        snprintf(tmp_buf, sizeof(tmp_buf), "(%s %u %u)",
                 layout->list[i].xlator->name, layout->list[i].start,
                 layout->list[i].stop);
        if (i)
            strcat(buf, " ");
        strcat(buf, tmp_buf);
    }
}

static void
dht_fill_pathinfo_xattr(xlator_t *this, dht_local_t *local, char *xattr_buf,
                        int32_t alloc_len, int flag, char *layout_buf)
{
    if (flag) {
        if (local->xattr_val) {
            snprintf(xattr_buf, alloc_len,
                     "((<" DHT_PATHINFO_HEADER "%s> %s) (%s-layout %s))",
                     this->name, local->xattr_val, this->name, layout_buf);
        } else {
            snprintf(xattr_buf, alloc_len, "(%s-layout %s)", this->name,
                     layout_buf);
        }
    } else if (local->xattr_val) {
        snprintf(xattr_buf, alloc_len, "(<" DHT_PATHINFO_HEADER "%s> %s)",
                 this->name, local->xattr_val);
    } else {
        xattr_buf[0] = '\0';
    }
}

static int
dht_vgetxattr_alloc_and_fill(dht_local_t *local, dict_t *xattr, xlator_t *this,
                             int op_errno)
{
    int ret = -1;
    char *value = NULL;

    ret = dict_get_str(xattr, local->xsel, &value);
    if (ret) {
        gf_msg(this->name, GF_LOG_ERROR, op_errno, DHT_MSG_GET_XATTR_FAILED,
               "Subvolume %s returned -1", this->name);
        local->op_ret = -1;
        local->op_errno = op_errno;
        goto out;
    }

    local->alloc_len += strlen(value);

    if (!local->xattr_val) {
        local->alloc_len += (SLEN(DHT_PATHINFO_HEADER) + 10);
        local->xattr_val = GF_MALLOC(local->alloc_len, gf_common_mt_char);
        if (!local->xattr_val) {
            ret = -1;
            goto out;
        }
        local->xattr_val[0] = '\0';
    }

    int plen = strlen(local->xattr_val);
    if (plen) {
        /* extra byte(s) for \0 to be safe */
        local->alloc_len += (plen + 2);
        local->xattr_val = GF_REALLOC(local->xattr_val, local->alloc_len);
        if (!local->xattr_val) {
            ret = -1;
            goto out;
        }
    }

    (void)strcat(local->xattr_val, value);
    (void)strcat(local->xattr_val, " ");
    local->op_ret = 0;

    ret = 0;

out:
    return ret;
}

static int
dht_vgetxattr_fill_and_set(dht_local_t *local, dict_t **dict, xlator_t *this,
                           gf_boolean_t flag)
{
    int ret = -1;
    char *xattr_buf = NULL;
    char layout_buf[8192] = {
        0,
    };

    if (flag)
        fill_layout_info(local->layout, layout_buf);

    *dict = dict_new();
    if (!*dict)
        goto out;

    local->xattr_val[strlen(local->xattr_val) - 1] = '\0';

    /* we would need max this many bytes to create xattr string
     * extra 40 bytes is just an estimated amount of additional
     * space required as we include translator name and some
     * spaces, brackets etc. when forming the pathinfo string.
     *
     * For node-uuid we just don't have all the pretty formatting,
     * but since this is a generic routine for pathinfo & node-uuid
     * we don't have conditional space allocation and try to be
     * generic
     */
    local->alloc_len += (2 * strlen(this->name)) + strlen(layout_buf) + 40;
    xattr_buf = GF_MALLOC(local->alloc_len, gf_common_mt_char);
    if (!xattr_buf)
        goto out;

    if (XATTR_IS_PATHINFO(local->xsel)) {
        (void)dht_fill_pathinfo_xattr(this, local, xattr_buf, local->alloc_len,
                                      flag, layout_buf);
    } else if ((XATTR_IS_NODE_UUID(local->xsel)) ||
               (XATTR_IS_NODE_UUID_LIST(local->xsel))) {
        (void)snprintf(xattr_buf, local->alloc_len, "%s", local->xattr_val);
    } else {
        gf_msg(this->name, GF_LOG_WARNING, 0, DHT_MSG_GET_XATTR_FAILED,
               "Unknown local->xsel (%s)", local->xsel);
        GF_FREE(xattr_buf);
        goto out;
    }

    ret = dict_set_dynstr(*dict, local->xsel, xattr_buf);
    if (ret)
        GF_FREE(xattr_buf);
    GF_FREE(local->xattr_val);

out:
    return ret;
}

static int
dht_find_local_subvol_cbk(call_frame_t *frame, void *cookie, xlator_t *this,
                          int op_ret, int op_errno, dict_t *xattr,
                          dict_t *xdata)
{
    dht_local_t *local = NULL;
    dht_conf_t *conf = NULL;
    xlator_t *prev = NULL;
    int this_call_cnt = 0;
    int ret = 0;
    char *uuid_str = NULL;
    char *uuid_list = NULL;
    char *next_uuid_str = NULL;
    char *saveptr = NULL;
    uuid_t node_uuid = {
        0,
    };
    char *uuid_list_copy = NULL;
    int count = 0;
    int i = 0;
    int index = 0;
    int found = 0;
    nodeuuid_info_t *tmp_ptr = NULL;

    VALIDATE_OR_GOTO(frame, out);
    VALIDATE_OR_GOTO(frame->local, out);

    local = frame->local;
    prev = cookie;
    conf = this->private;

    VALIDATE_OR_GOTO(conf->defrag, out);

    gf_msg_debug(this->name, 0, "subvol %s returned", prev->name);

    LOCK(&frame->lock);
    {
        this_call_cnt = --local->call_cnt;
        if (op_ret < 0) {
            local->op_ret = -1;
            local->op_errno = op_errno;
            UNLOCK(&frame->lock);
            if (op_errno == ENODATA)
                gf_msg_debug(this->name, 0, "failed to get node-uuid");
            else
                gf_msg(this->name, GF_LOG_ERROR, op_errno,
                       DHT_MSG_GET_XATTR_FAILED, "failed to get node-uuid");
            goto post_unlock;
        }

        ret = dict_get_str(xattr, local->xsel, &uuid_list);

        if (ret < 0) {
            gf_msg(this->name, GF_LOG_ERROR, 0, DHT_MSG_DICT_GET_FAILED,
                   "Failed to get %s", local->xsel);
            local->op_ret = -1;
            local->op_errno = EINVAL;
            goto unlock;
        }

        /* As DHT will not know details of its child xlators
         * we need to parse this twice to get the count first
         * and allocate memory later.
         */
        count = 0;
        index = conf->local_subvols_cnt;

        uuid_list_copy = gf_strdup(uuid_list);
        if (!uuid_list_copy)
            goto unlock;

        for (uuid_str = strtok_r(uuid_list, " ", &saveptr); uuid_str;
             uuid_str = next_uuid_str) {
            next_uuid_str = strtok_r(NULL, " ", &saveptr);
            if (gf_uuid_parse(uuid_str, node_uuid)) {
                local->op_ret = -1;
                local->op_errno = EINVAL;
                UNLOCK(&frame->lock);
                gf_msg(this->name, GF_LOG_ERROR, 0, DHT_MSG_UUID_PARSE_ERROR,
                       "Failed to parse uuid for %s", prev->name);
                goto post_unlock;
            }

            count++;
            if (gf_uuid_compare(node_uuid, conf->defrag->node_uuid)) {
                gf_msg_debug(this->name, 0,
                             "subvol %s does not"
                             "belong to this node",
                             prev->name);
            } else {
                /* handle multiple bricks of the same replica
                 * on the same node */
                if (found)
                    continue;
                conf->local_subvols[(conf->local_subvols_cnt)++] = prev;
                found = 1;
                gf_msg_debug(this->name, 0,
                             "subvol %s belongs to"
                             " this node",
                             prev->name);
            }
        }

        if (!found) {
            local->op_ret = 0;
            goto unlock;
        }

        conf->local_nodeuuids[index].count = count;
        conf->local_nodeuuids[index].elements = GF_CALLOC(
            count, sizeof(nodeuuid_info_t), 1);

        /* The node-uuids are guaranteed to be returned in the same
         * order as the bricks
         * A null node-uuid is returned for a brick that is down.
         */

        saveptr = NULL;
        i = 0;

        for (uuid_str = strtok_r(uuid_list_copy, " ", &saveptr); uuid_str;
             uuid_str = next_uuid_str) {
            next_uuid_str = strtok_r(NULL, " ", &saveptr);
            tmp_ptr = &(conf->local_nodeuuids[index].elements[i]);
            gf_uuid_parse(uuid_str, tmp_ptr->uuid);

            if (!gf_uuid_compare(tmp_ptr->uuid, conf->defrag->node_uuid)) {
                tmp_ptr->info = REBAL_NODEUUID_MINE;
            }
            i++;
            tmp_ptr = NULL;
        }
    }

    local->op_ret = 0;
unlock:
    UNLOCK(&frame->lock);
post_unlock:
    if (!is_last_call(this_call_cnt))
        goto out;

    if (local->op_ret == -1) {
        goto unwind;
    }

    DHT_STACK_UNWIND(getxattr, frame, 0, 0, xattr, xdata);
    goto out;

unwind:

    GF_FREE(conf->local_nodeuuids[index].elements);
    conf->local_nodeuuids[index].elements = NULL;

    DHT_STACK_UNWIND(getxattr, frame, -1, local->op_errno, NULL, xdata);
out:
    GF_FREE(uuid_list_copy);
    return 0;
}

static int
dht_vgetxattr_dir_cbk(call_frame_t *frame, void *cookie, xlator_t *this,
                      int op_ret, int op_errno, dict_t *xattr, dict_t *xdata)
{
    int ret = 0;
    dht_local_t *local = NULL;
    int this_call_cnt = 0;
    dict_t *dict = NULL;

    VALIDATE_OR_GOTO(frame, out);
    VALIDATE_OR_GOTO(frame->local, out);

    local = frame->local;

    LOCK(&frame->lock);
    {
        this_call_cnt = --local->call_cnt;
        if (op_ret < 0) {
            if (op_errno != ENOTCONN) {
                local->op_ret = -1;
                local->op_errno = op_errno;
                UNLOCK(&frame->lock);
                gf_msg(this->name, GF_LOG_ERROR, op_errno,
                       DHT_MSG_GET_XATTR_FAILED, "getxattr err for dir");
                goto post_unlock;
            }

            goto unlock;
        }

        ret = dht_vgetxattr_alloc_and_fill(local, xattr, this, op_errno);
        if (ret) {
            UNLOCK(&frame->lock);
            gf_msg(this->name, GF_LOG_ERROR, op_errno, DHT_MSG_DICT_SET_FAILED,
                   "alloc or fill failure");
            goto post_unlock;
        }
    }
unlock:
    UNLOCK(&frame->lock);
post_unlock:
    if (!is_last_call(this_call_cnt))
        goto out;

    /* -- last call: do patch ups -- */

    if (local->op_ret == -1) {
        goto unwind;
    }

    ret = dht_vgetxattr_fill_and_set(local, &dict, this, _gf_true);
    if (ret)
        goto unwind;

    DHT_STACK_UNWIND(getxattr, frame, 0, 0, dict, xdata);
    goto cleanup;

unwind:
    DHT_STACK_UNWIND(getxattr, frame, -1, local->op_errno, NULL, NULL);
cleanup:
    if (dict)
        dict_unref(dict);
out:
    return 0;
}

static int
dht_vgetxattr_cbk(call_frame_t *frame, void *cookie, xlator_t *this, int op_ret,
                  int op_errno, dict_t *xattr, dict_t *xdata)
{
    dht_local_t *local = NULL;
    int ret = 0;
    dict_t *dict = NULL;
    xlator_t *prev = NULL;
    gf_boolean_t flag = _gf_true;

    local = frame->local;
    prev = cookie;

    if (op_ret < 0) {
        local->op_ret = -1;
        local->op_errno = op_errno;
        gf_msg(this->name, GF_LOG_ERROR, op_errno, DHT_MSG_GET_XATTR_FAILED,
               "vgetxattr: Subvolume %s returned -1", prev->name);
        goto unwind;
    }

    ret = dht_vgetxattr_alloc_and_fill(local, xattr, this, op_errno);
    if (ret) {
        gf_msg(this->name, GF_LOG_ERROR, 0, DHT_MSG_NO_MEMORY,
               "Allocation or fill failure");
        goto unwind;
    }

    flag = (local->layout->cnt > 1) ? _gf_true : _gf_false;

    ret = dht_vgetxattr_fill_and_set(local, &dict, this, flag);
    if (ret)
        goto unwind;

    DHT_STACK_UNWIND(getxattr, frame, 0, 0, dict, xdata);
    goto cleanup;

unwind:
    DHT_STACK_UNWIND(getxattr, frame, -1, local->op_errno, NULL, NULL);
cleanup:
    if (dict)
        dict_unref(dict);

    return 0;
}

static int
dht_linkinfo_getxattr_cbk(call_frame_t *frame, void *cookie, xlator_t *this,
                          int op_ret, int op_errno, dict_t *xattr,
                          dict_t *xdata)
{
    int ret = 0;
    char *value = NULL;

    if (op_ret != -1) {
        ret = dict_get_str(xattr, GF_XATTR_PATHINFO_KEY, &value);
        if (!ret) {
            ret = dict_set_str(xattr, GF_XATTR_LINKINFO_KEY, value);
            if (!ret)
                gf_msg_trace(this->name, 0, "failed to set linkinfo");
        }
    }

    DHT_STACK_UNWIND(getxattr, frame, op_ret, op_errno, xattr, xdata);

    return 0;
}

static int
dht_mds_getxattr_cbk(call_frame_t *frame, void *cookie, xlator_t *this,
                     int op_ret, int op_errno, dict_t *xattr, dict_t *xdata)
{
    dht_local_t *local = NULL;
    dht_conf_t *conf = NULL;

    VALIDATE_OR_GOTO(frame, err);
    VALIDATE_OR_GOTO(frame->local, err);
    VALIDATE_OR_GOTO(this->private, err);

    conf = this->private;
    local = frame->local;

    if (!xattr || (op_ret == -1)) {
        local->op_ret = op_ret;
        goto out;
    }
    dict_del(xattr, conf->xattr_name);
    local->op_ret = 0;

    if (!local->xattr) {
        local->xattr = dict_copy_with_ref(xattr, NULL);
    }

out:
    DHT_STACK_UNWIND(getxattr, frame, local->op_ret, op_errno, local->xattr,
                     xdata);
    return 0;
err:
    DHT_STACK_UNWIND(getxattr, frame, -1, EINVAL, NULL, NULL);
    return 0;
}

int
dht_getxattr_cbk(call_frame_t *frame, void *cookie, xlator_t *this, int op_ret,
                 int op_errno, dict_t *xattr, dict_t *xdata)
{
    int this_call_cnt = 0;
    dht_local_t *local = NULL;
    dht_conf_t *conf = NULL;
    int ret = 0;

    VALIDATE_OR_GOTO(frame, err);
    VALIDATE_OR_GOTO(frame->local, err);
    VALIDATE_OR_GOTO(this->private, err);

    conf = this->private;
    local = frame->local;

    if (dht_check_remote_fd_failed_error(local, op_ret, op_errno)) {
        ret = dht_check_and_open_fd_on_subvol(this, frame);
        if (ret)
            goto err;
        return 0;
    }

    LOCK(&frame->lock);
    {
        if (!xattr || (op_ret == -1)) {
            local->op_ret = op_ret;
            goto unlock;
        }

        dict_del(xattr, conf->xattr_name);
        dict_del(xattr, conf->mds_xattr_key);

        dict_del(xattr, conf->commithash_xattr_name);

        if (frame->root->pid >= 0) {
            GF_REMOVE_INTERNAL_XATTR("trusted.glusterfs.quota*", xattr);
            GF_REMOVE_INTERNAL_XATTR("trusted.pgfid*", xattr);
        }

        local->op_ret = 0;

        if (!local->xattr) {
            local->xattr = dict_copy_with_ref(xattr, NULL);
        } else {
            dht_aggregate_xattr(local->xattr, xattr);
        }

        if (!local->xdata) {
            local->xdata = dict_ref(xdata);
        } else if ((local->inode && IA_ISDIR(local->inode->ia_type)) ||
                   (local->fd && IA_ISDIR(local->fd->inode->ia_type))) {
            dht_aggregate_xattr(local->xdata, xdata);
        }
    }
unlock:
    UNLOCK(&frame->lock);

    this_call_cnt = dht_frame_return(frame);
    if (is_last_call(this_call_cnt)) {
        /* If we have a valid xattr received from any one of the
         * subvolume, let's return it */
        if (local->xattr) {
            local->op_ret = 0;
        }

        DHT_STACK_UNWIND(getxattr, frame, local->op_ret, op_errno, local->xattr,
                         local->xdata);
    }
    return 0;
err:
    DHT_STACK_UNWIND(getxattr, frame, -1, EINVAL, NULL, NULL);
    return 0;
}

static int32_t
dht_getxattr_unwind(call_frame_t *frame, int op_ret, int op_errno, dict_t *dict,
                    dict_t *xdata)
{
    DHT_STACK_UNWIND(getxattr, frame, op_ret, op_errno, dict, xdata);
    return 0;
}

static int
dht_getxattr_get_real_filename_cbk(call_frame_t *frame, void *cookie,
                                   xlator_t *this, int op_ret, int op_errno,
                                   dict_t *xattr, dict_t *xdata)
{
    int this_call_cnt = 0;
    dht_local_t *local = NULL;

    local = frame->local;

    LOCK(&frame->lock);
    {
        if (local->op_errno == EOPNOTSUPP) {
            /* Nothing to do here, we have already found
             * a subvol which does not have the get_real_filename
             * optimization. If condition is for simple logic.
             */
            goto unlock;
        }

        if (op_ret == -1) {
            if (op_errno == EOPNOTSUPP) {
                /* This subvol does not have the optimization.
                 * Better let the user know we don't support it.
                 * Remove previous results if any.
                 */

                if (local->xattr) {
                    dict_unref(local->xattr);
                    local->xattr = NULL;
                }

                if (local->xattr_req) {
                    dict_unref(local->xattr_req);
                    local->xattr_req = NULL;
                }

                local->op_ret = op_ret;
                local->op_errno = op_errno;
                UNLOCK(&frame->lock);
                gf_msg(this->name, GF_LOG_WARNING, op_errno,
                       DHT_MSG_UPGRADE_BRICKS,
                       "At least "
                       "one of the bricks does not support "
                       "this operation. Please upgrade all "
                       "bricks.");
                goto post_unlock;
            }

            if (op_errno == ENOATTR) {
                /* Do nothing, our defaults are set to this.
                 */
                goto unlock;
            }

            /* This is a place holder for every other error
             * case. I am not sure of how to interpret
             * ENOTCONN etc. As of now, choosing to ignore
             * down subvol and return a good result(if any)
             * from other subvol.
             */
            UNLOCK(&frame->lock);
            gf_msg(this->name, GF_LOG_WARNING, op_errno,
                   DHT_MSG_GET_XATTR_FAILED, "Failed to get real filename.");
            goto post_unlock;
        }

        /* This subvol has the required file.
         * There could be other subvols which have returned
         * success already, choosing to return the latest good
         * result.
         */
        if (local->xattr)
            dict_unref(local->xattr);
        local->xattr = dict_ref(xattr);

        if (local->xattr_req) {
            dict_unref(local->xattr_req);
            local->xattr_req = NULL;
        }
        if (xdata)
            local->xattr_req = dict_ref(xdata);

        local->op_ret = op_ret;
        local->op_errno = 0;
        UNLOCK(&frame->lock);
        gf_msg_debug(this->name, 0, "Found a matching file.");
        goto post_unlock;
    }
unlock:
    UNLOCK(&frame->lock);
post_unlock:
    this_call_cnt = dht_frame_return(frame);
    if (is_last_call(this_call_cnt)) {
        DHT_STACK_UNWIND(getxattr, frame, local->op_ret, local->op_errno,
                         local->xattr, local->xattr_req);
    }

    return 0;
}

static int
dht_getxattr_get_real_filename(call_frame_t *frame, xlator_t *this, loc_t *loc,
                               const char *key, dict_t *xdata)
{
    dht_local_t *local = NULL;
    int i = 0;
    dht_layout_t *layout = NULL;
    int cnt = 0;
    xlator_t *subvol = NULL;

    local = frame->local;
    layout = local->layout;

    cnt = local->call_cnt = layout->cnt;

    local->op_ret = -1;
    local->op_errno = ENOATTR;

    for (i = 0; i < cnt; i++) {
        subvol = layout->list[i].xlator;
        STACK_WIND(frame, dht_getxattr_get_real_filename_cbk, subvol,
                   subvol->fops->getxattr, loc, key, xdata);
    }

    return 0;
}

static int
dht_marker_populate_args(call_frame_t *frame, int type, int *gauge,
                         xlator_t **subvols)
{
    dht_local_t *local = NULL;
    int i = 0;
    dht_layout_t *layout = NULL;

    local = frame->local;
    layout = local->layout;

    for (i = 0; i < layout->cnt; i++)
        subvols[i] = layout->list[i].xlator;

    return layout->cnt;
}

static int
dht_is_debug_xattr_key(const char **array, char *key)
{
    int i = 0;

    for (i = 0; array[i]; i++) {
        if (fnmatch(array[i], key, FNM_NOESCAPE) == 0)
            return i;
    }

    return -1;
}

/* Note we already have frame->local initialised here*/

static int
dht_handle_debug_getxattr(call_frame_t *frame, xlator_t *this, loc_t *loc,
                          const char *key)
{
    dht_local_t *local = NULL;
    int ret = -1;
    int op_errno = ENODATA;
    char *value = NULL;
    loc_t file_loc = {0};
    const char *name = NULL;

    local = frame->local;

    if (dht_is_debug_xattr_key(dht_dbg_vxattrs, (char *)key) == -1) {
        goto out;
    }

    local->xattr = dict_new();
    if (!local->xattr) {
        op_errno = ENOMEM;
        goto out;
    }

    if (strncmp(key, DHT_DBG_HASHED_SUBVOL_KEY,
                SLEN(DHT_DBG_HASHED_SUBVOL_KEY)) == 0) {
        name = key + strlen(DHT_DBG_HASHED_SUBVOL_KEY);
        if (strlen(name) == 0) {
            op_errno = EINVAL;
            goto out;
        }

        ret = dht_build_child_loc(this, &file_loc, loc, (char *)name);
        if (ret) {
            op_errno = ENOMEM;
            goto out;
        }

        local->hashed_subvol = dht_subvol_get_hashed(this, &file_loc);
        if (local->hashed_subvol == NULL) {
            op_errno = ENODATA;
            goto out;
        }

        value = gf_strdup(local->hashed_subvol->name);
        if (!value) {
            op_errno = ENOMEM;
            goto out;
        }

        ret = dict_set_dynstr(local->xattr, (char *)key, value);
        if (ret < 0) {
            op_errno = -ret;
            ret = -1;
            goto out;
        }
        ret = 0;
        goto out;
    }

out:
    loc_wipe(&file_loc);
    DHT_STACK_UNWIND(getxattr, frame, ret, op_errno, local->xattr, NULL);
    return 0;
}

/* Virtual Xattr which returns 1 if all subvols are up,
   else returns 0. Geo-rep then uses this virtual xattr
   after a fresh mount and starts the I/O.
*/

enum dht_vxattr_subvol {
    DHT_VXATTR_SUBVOLS_UP = 1,
    DHT_VXATTR_SUBVOLS_DOWN = 0,
};

int
dht_vgetxattr_subvol_status(call_frame_t *frame, xlator_t *this,
                            const char *key)
{
    dht_local_t *local = NULL;
    int ret = -1;
    int op_errno = ENODATA;
    int value = DHT_VXATTR_SUBVOLS_UP;
    int i = 0;
    dht_conf_t *conf = NULL;

    conf = this->private;
    local = frame->local;

    if (!key) {
        op_errno = EINVAL;
        goto out;
    }
    local->xattr = dict_new();
    if (!local->xattr) {
        op_errno = ENOMEM;
        goto out;
    }
    for (i = 0; i < conf->subvolume_cnt; i++) {
        if (!conf->subvolume_status[i]) {
            value = DHT_VXATTR_SUBVOLS_DOWN;
            gf_msg_debug(this->name, 0, "subvol %s is down ",
                         conf->subvolumes[i]->name);
            break;
        }
    }
    ret = dict_set_int8(local->xattr, (char *)key, value);
    if (ret < 0) {
        op_errno = -ret;
        ret = -1;
        goto out;
    }
    ret = 0;

out:
    DHT_STACK_UNWIND(getxattr, frame, ret, op_errno, local->xattr, NULL);
    return 0;
}

int
dht_getxattr(call_frame_t *frame, xlator_t *this, loc_t *loc, const char *key,
             dict_t *xdata)
#define DHT_IS_DIR(layout) (layout->cnt > 1)
{
    xlator_t *subvol = NULL;
    xlator_t *hashed_subvol = NULL;
    xlator_t *mds_subvol = NULL;
    xlator_t *cached_subvol = NULL;
    dht_conf_t *conf = NULL;
    dht_local_t *local = NULL;
    dht_layout_t *layout = NULL;
    int op_errno = -1;
    int i = 0;
    int cnt = 0;
    char *node_uuid_key = NULL;
    int ret = -1;

    GF_CHECK_XATTR_KEY_AND_GOTO(key, IO_THREADS_QUEUE_SIZE_KEY, op_errno, err);
    VALIDATE_OR_GOTO(frame, err);
    VALIDATE_OR_GOTO(this, err);
    VALIDATE_OR_GOTO(loc, err);
    VALIDATE_OR_GOTO(loc->inode, err);
    VALIDATE_OR_GOTO(this->private, err);

    conf = this->private;

    local = dht_local_init(frame, loc, NULL, GF_FOP_GETXATTR);
    if (!local) {
        op_errno = ENOMEM;

        goto err;
    }

    layout = local->layout;
    if (!layout) {
        gf_msg(this->name, GF_LOG_ERROR, 0, DHT_MSG_LAYOUT_NULL,
               "Layout is NULL");
        op_errno = ENOENT;
        goto err;
    }

    /* skip over code which is irrelevant without a valid key */
    if (!key)
        goto no_key;

    local->key = gf_strdup(key);
    if (!local->key) {
        op_errno = ENOMEM;
        goto err;
    }

    if (strncmp(key, conf->mds_xattr_key, strlen(key)) == 0) {
        op_errno = ENOTSUP;
        goto err;
    }

    if (strncmp(key, DHT_SUBVOL_STATUS_KEY, SLEN(DHT_SUBVOL_STATUS_KEY)) == 0) {
        dht_vgetxattr_subvol_status(frame, this, key);
        return 0;
    }

    /* skip over code which is irrelevant if !DHT_IS_DIR(layout) */
    if (!DHT_IS_DIR(layout))
        goto no_dht_is_dir;

    if ((strncmp(key, GF_XATTR_GET_REAL_FILENAME_KEY,
                 SLEN(GF_XATTR_GET_REAL_FILENAME_KEY)) == 0) &&
        DHT_IS_DIR(layout)) {
        dht_getxattr_get_real_filename(frame, this, loc, key, xdata);
        return 0;
    }

    if (!strcmp(key, GF_REBAL_FIND_LOCAL_SUBVOL)) {
        ret = gf_asprintf(&node_uuid_key, "%s", GF_XATTR_LIST_NODE_UUIDS_KEY);
        if (ret == -1 || !node_uuid_key) {
            gf_msg(this->name, GF_LOG_ERROR, 0, DHT_MSG_NO_MEMORY,
                   "Failed to copy node uuid key");
            op_errno = ENOMEM;
            goto err;
        }
        (void)snprintf(local->xsel, sizeof(local->xsel), "%s", node_uuid_key);
        cnt = local->call_cnt = conf->subvolume_cnt;
        for (i = 0; i < cnt; i++) {
            STACK_WIND_COOKIE(frame, dht_find_local_subvol_cbk,
                              conf->subvolumes[i], conf->subvolumes[i],
                              conf->subvolumes[i]->fops->getxattr, loc,
                              node_uuid_key, xdata);
        }
        if (node_uuid_key)
            GF_FREE(node_uuid_key);
        return 0;
    }

    if (!strcmp(key, GF_REBAL_OLD_FIND_LOCAL_SUBVOL)) {
        ret = gf_asprintf(&node_uuid_key, "%s", GF_XATTR_NODE_UUID_KEY);
        if (ret == -1 || !node_uuid_key) {
            gf_msg(this->name, GF_LOG_ERROR, 0, DHT_MSG_NO_MEMORY,
                   "Failed to copy node uuid key");
            op_errno = ENOMEM;
            goto err;
        }
        (void)snprintf(local->xsel, sizeof(local->xsel), "%s", node_uuid_key);
        cnt = local->call_cnt = conf->subvolume_cnt;
        for (i = 0; i < cnt; i++) {
            STACK_WIND_COOKIE(frame, dht_find_local_subvol_cbk,
                              conf->subvolumes[i], conf->subvolumes[i],
                              conf->subvolumes[i]->fops->getxattr, loc,
                              node_uuid_key, xdata);
        }
        if (node_uuid_key)
            GF_FREE(node_uuid_key);
        return 0;
    }

    /* for file use cached subvolume (obviously!): see if {}
     * below
     * for directory:
     *  wind to all subvolumes and exclude subvolumes which
     *  return ENOTCONN (in callback)
     *
     * NOTE: Don't trust inode here, as that may not be valid
     *       (until inode_link() happens)
     */

    if (XATTR_IS_PATHINFO(key) || (strcmp(key, GF_XATTR_NODE_UUID_KEY) == 0) ||
        (strcmp(key, GF_XATTR_LIST_NODE_UUIDS_KEY) == 0)) {
        (void)snprintf(local->xsel, sizeof(local->xsel), "%s", key);
        cnt = local->call_cnt = layout->cnt;
        for (i = 0; i < cnt; i++) {
            subvol = layout->list[i].xlator;
            STACK_WIND(frame, dht_vgetxattr_dir_cbk, subvol,
                       subvol->fops->getxattr, loc, key, xdata);
        }
        return 0;
    }

no_dht_is_dir:
    /* node-uuid or pathinfo for files */
    if (XATTR_IS_PATHINFO(key) || (strcmp(key, GF_XATTR_NODE_UUID_KEY) == 0)) {
        cached_subvol = local->cached_subvol;
        (void)snprintf(local->xsel, sizeof(local->xsel), "%s", key);
        local->call_cnt = 1;
        STACK_WIND_COOKIE(frame, dht_vgetxattr_cbk, cached_subvol,
                          cached_subvol, cached_subvol->fops->getxattr, loc,
                          key, xdata);

        return 0;
    }

    if (strcmp(key, GF_XATTR_LINKINFO_KEY) == 0) {
        hashed_subvol = dht_subvol_get_hashed(this, loc);
        if (!hashed_subvol) {
            gf_msg(this->name, GF_LOG_ERROR, 0,
                   DHT_MSG_HASHED_SUBVOL_GET_FAILED,
                   "Failed to get hashed subvol for %s", loc->path);
            op_errno = EINVAL;
            goto err;
        }

        cached_subvol = dht_subvol_get_cached(this, loc->inode);
        if (!cached_subvol) {
            gf_msg(this->name, GF_LOG_ERROR, 0,
                   DHT_MSG_CACHED_SUBVOL_GET_FAILED,
                   "Failed to get cached subvol for %s", loc->path);
            op_errno = EINVAL;
            goto err;
        }

        if (hashed_subvol == cached_subvol) {
            op_errno = ENODATA;
            goto err;
        }

        STACK_WIND(frame, dht_linkinfo_getxattr_cbk, hashed_subvol,
                   hashed_subvol->fops->getxattr, loc, GF_XATTR_PATHINFO_KEY,
                   xdata);
        return 0;
    }

    if (dht_is_debug_xattr_key(dht_dbg_vxattrs, (char *)key) >= 0) {
        dht_handle_debug_getxattr(frame, this, loc, key);
        return 0;
    }

no_key:
    if (cluster_handle_marker_getxattr(frame, loc, key, conf->vol_uuid,
                                       dht_getxattr_unwind,
                                       dht_marker_populate_args) == 0)
        return 0;

    if (DHT_IS_DIR(layout)) {
        local->call_cnt = conf->subvolume_cnt;
        cnt = conf->subvolume_cnt;
        ret = dht_inode_ctx_mdsvol_get(loc->inode, this, &mds_subvol);
        if (!mds_subvol) {
            gf_msg(this->name, GF_LOG_INFO, 0, DHT_MSG_HASHED_SUBVOL_GET_FAILED,
                   "Cannot determine MDS, fetching xattr %s randomly"
                   " from a subvol for path %s ",
                   key, loc->path);
        } else {
            /* TODO need to handle it, As of now we are
               choosing availability instead of chossing
               consistencty, in case of mds_subvol is
               down winding a getxattr call on other subvol
               and return xattr
            */
            local->mds_subvol = mds_subvol;
            for (i = 0; i < cnt; i++) {
                if (conf->subvolumes[i] == mds_subvol) {
                    if (!conf->subvolume_status[i]) {
                        gf_msg(this->name, GF_LOG_INFO, 0,
                               DHT_MSG_HASHED_SUBVOL_DOWN,
                               "MDS %s is down for path"
                               " path %s so fetching xattr "
                               "%s randomly from a subvol ",
                               local->mds_subvol->name, loc->path, key);
                        ret = 1;
                    }
                }
            }
        }

        if (!ret && key && local->mds_subvol && dht_match_xattr(key)) {
            STACK_WIND(frame, dht_mds_getxattr_cbk, local->mds_subvol,
                       local->mds_subvol->fops->getxattr, loc, key, xdata);

            return 0;
        }
    } else {
        cnt = local->call_cnt = 1;
    }

    for (i = 0; i < cnt; i++) {
        subvol = layout->list[i].xlator;
        STACK_WIND(frame, dht_getxattr_cbk, subvol, subvol->fops->getxattr, loc,
                   key, xdata);
    }
    return 0;

err:
    op_errno = (op_errno == -1) ? errno : op_errno;
    DHT_STACK_UNWIND(getxattr, frame, -1, op_errno, NULL, NULL);

    return 0;
}
#undef DHT_IS_DIR

int
dht_fgetxattr(call_frame_t *frame, xlator_t *this, fd_t *fd, const char *key,
              dict_t *xdata)
{
    xlator_t *subvol = NULL;
    dht_local_t *local = NULL;
    dht_layout_t *layout = NULL;
    int op_errno = -1;
    int i = 0;
    int cnt = 0;
    xlator_t *mds_subvol = NULL;
    int ret = -1;
    dht_conf_t *conf = NULL;
    char gfid[GF_UUID_BUF_SIZE] = {0};

    VALIDATE_OR_GOTO(frame, err);
    VALIDATE_OR_GOTO(this, err);
    VALIDATE_OR_GOTO(fd, err);
    VALIDATE_OR_GOTO(fd->inode, err);
    VALIDATE_OR_GOTO(this->private, err);

    conf = this->private;

    local = dht_local_init(frame, NULL, fd, GF_FOP_FGETXATTR);
    if (!local) {
        op_errno = ENOMEM;

        goto err;
    }

    layout = local->layout;
    if (!layout) {
        gf_msg(this->name, GF_LOG_ERROR, 0, DHT_MSG_LAYOUT_NULL,
               "Layout is NULL");
        op_errno = ENOENT;
        goto err;
    }

    if (key) {
        local->key = gf_strdup(key);
        if (!local->key) {
            op_errno = ENOMEM;
            goto err;
        }
    }

    gf_uuid_unparse(fd->inode->gfid, gfid);

    if ((fd->inode->ia_type == IA_IFDIR) && key &&
        (strncmp(key, GF_XATTR_LOCKINFO_KEY, SLEN(GF_XATTR_LOCKINFO_KEY)) !=
         0)) {
        local->call_cnt = conf->subvolume_cnt;
        cnt = conf->subvolume_cnt;
        ret = dht_inode_ctx_mdsvol_get(fd->inode, this, &mds_subvol);

        if (!mds_subvol) {
            gf_msg(this->name, GF_LOG_ERROR, 0,
                   DHT_MSG_HASHED_SUBVOL_GET_FAILED,
                   "cannot determine MDS, fetching xattr %s "
                   " randomly from a subvol for gfid %s ",
                   key, gfid);
        } else {
            /* TODO need to handle it, As of now we are
               choosing availability instead of chossing
               consistencty, in case of hashed_subvol is
               down winding a getxattr call on other subvol
               and return xattr
            */
            local->mds_subvol = mds_subvol;
            for (i = 0; i < cnt; i++) {
                if (conf->subvolumes[i] == mds_subvol) {
                    if (!conf->subvolume_status[i]) {
                        gf_msg(this->name, GF_LOG_WARNING, 0,
                               DHT_MSG_HASHED_SUBVOL_DOWN,
                               "MDS subvolume %s is down"
                               " for gfid %s so fetching xattr "
                               " %s randomly from a subvol ",
                               local->mds_subvol->name, gfid, key);
                        ret = 1;
                    }
                }
            }
        }

        if (!ret && key && local->mds_subvol && dht_match_xattr(key)) {
            STACK_WIND(frame, dht_mds_getxattr_cbk, local->mds_subvol,
                       local->mds_subvol->fops->fgetxattr, fd, key, NULL);

            return 0;
        }

    } else {
        cnt = local->call_cnt = 1;
    }

    for (i = 0; i < cnt; i++) {
        subvol = layout->list[i].xlator;
        STACK_WIND(frame, dht_getxattr_cbk, subvol, subvol->fops->fgetxattr, fd,
                   key, NULL);
    }
    return 0;

err:
    op_errno = (op_errno == -1) ? errno : op_errno;
    DHT_STACK_UNWIND(fgetxattr, frame, -1, op_errno, NULL, NULL);

    return 0;
}

static int
dht_setxattr2(xlator_t *this, xlator_t *subvol, call_frame_t *frame, int ret)
{
    dht_local_t *local = NULL;
    int op_errno = EINVAL;

    if (!frame || !frame->local)
        goto err;

    local = frame->local;
    op_errno = local->op_errno;

    if (we_are_not_migrating(ret)) {
        /* This dht xlator is not migrating the file. Unwind and
         * pass on the original mode bits so the higher DHT layer
         * can handle this.
         */
        DHT_STACK_UNWIND(setxattr, frame, local->op_ret, local->op_errno,
                         local->rebalance.xdata);
        return 0;
    }

    if (subvol == NULL)
        goto err;

    local->call_cnt = 2; /* This is the second attempt */

    if (local->fop == GF_FOP_SETXATTR) {
        STACK_WIND_COOKIE(frame, dht_file_setxattr_cbk, subvol, subvol,
                          subvol->fops->setxattr, &local->loc,
                          local->rebalance.xattr, local->rebalance.flags,
                          local->xattr_req);
    } else {
        STACK_WIND_COOKIE(frame, dht_file_setxattr_cbk, subvol, subvol,
                          subvol->fops->fsetxattr, local->fd,
                          local->rebalance.xattr, local->rebalance.flags,
                          local->xattr_req);
    }

    return 0;

err:
    DHT_STACK_UNWIND(setxattr, frame, (local ? local->op_ret : -1), op_errno,
                     NULL);
    return 0;
}

int
dht_file_setxattr_cbk(call_frame_t *frame, void *cookie, xlator_t *this,
                      int op_ret, int op_errno, dict_t *xdata)
{
    int ret = -1;
    dht_local_t *local = NULL;
    xlator_t *prev = NULL;
    struct iatt *stbuf = NULL;
    inode_t *inode = NULL;
    xlator_t *subvol1 = NULL, *subvol2 = NULL;

    local = frame->local;
    prev = cookie;

    local->op_errno = op_errno;

    if ((local->fop == GF_FOP_FSETXATTR) &&
        dht_check_remote_fd_failed_error(local, op_ret, op_errno)) {
        ret = dht_check_and_open_fd_on_subvol(this, frame);
        if (ret)
            goto out;
        return 0;
    }

    if ((op_ret == -1) && !dht_inode_missing(op_errno)) {
        gf_msg_debug(this->name, op_errno, "subvolume %s returned -1.",
                     prev->name);
        goto out;
    }

    if (local->call_cnt != 1)
        goto out;

    ret = dict_get_bin(xdata, DHT_IATT_IN_XDATA_KEY, (void **)&stbuf);

    if ((!op_ret) && !stbuf) {
        goto out;
    }

    local->op_ret = op_ret;
    local->rebalance.target_op_fn = dht_setxattr2;
    if (xdata)
        local->rebalance.xdata = dict_ref(xdata);

    /* Phase 2 of migration */
    if ((op_ret == -1) || IS_DHT_MIGRATION_PHASE2(stbuf)) {
        ret = dht_rebalance_complete_check(this, frame);
        if (!ret)
            return 0;
    }

    /* Phase 1 of migration */
    if (IS_DHT_MIGRATION_PHASE1(stbuf)) {
        inode = (local->fd) ? local->fd->inode : local->loc.inode;

        ret = dht_inode_ctx_get_mig_info(this, inode, &subvol1, &subvol2);
        if (!dht_mig_info_is_invalid(local->cached_subvol, subvol1, subvol2)) {
            dht_setxattr2(this, subvol2, frame, 0);
            return 0;
        }

        ret = dht_rebalance_in_progress_check(this, frame);
        if (!ret)
            return 0;
    }

out:

    if (local->fop == GF_FOP_SETXATTR) {
        DHT_STACK_UNWIND(setxattr, frame, op_ret, op_errno, xdata);
    } else {
        DHT_STACK_UNWIND(fsetxattr, frame, op_ret, op_errno, xdata);
    }

    return 0;
}

/* Function is call by dict_foreach_fnmatch if key is match with
   user.* and set boolean flag to true
*/
static int
dht_is_user_xattr(dict_t *this, char *key, data_t *value, void *data)
{
    gf_boolean_t *user_xattr_found = data;
    *user_xattr_found = _gf_true;
    return 0;
}

/* Common code to wind a (f)(set|remove)xattr call to set xattr on directory
 */
static int
dht_dir_common_set_remove_xattr(call_frame_t *frame, xlator_t *this, loc_t *loc,
                                fd_t *fd, dict_t *xattr, int flags,
                                dict_t *xdata, int *op_errno)

{
    dict_t *xattrop = NULL;
    int32_t subone[1] = {-1};
    gf_boolean_t uxattr_key_found = _gf_false;
    xlator_t *mds_subvol = NULL;
    xlator_t *travvol = NULL;
    dht_conf_t *conf = NULL;
    int ret = -1;
    int i = 0;
    int call_cnt = 0;
    dht_local_t *local = NULL;
    char gfid_local[GF_UUID_BUF_SIZE] = {0};
    char **xattrs_to_heal;

    conf = this->private;
    local = frame->local;
    call_cnt = conf->subvolume_cnt;
    local->flags = flags;
    xattrs_to_heal = get_xattrs_to_heal();

    if (!gf_uuid_is_null(local->gfid)) {
        gf_uuid_unparse(local->gfid, gfid_local);
    }

    if ((local->fop == GF_FOP_SETXATTR) || (local->fop == GF_FOP_FSETXATTR)) {
        /* Check if any user xattr present in xattr
         */
        dict_foreach_fnmatch(xattr, "user*", dht_is_user_xattr,
                             &uxattr_key_found);

        /* Check if any custom key xattr present in dict xattr
           and start index from 1 because user xattr already
           checked in previous line
        */
        for (i = 1; xattrs_to_heal[i]; i++)
            if (dict_get(xattr, xattrs_to_heal[i]))
                uxattr_key_found = _gf_true;
    }

    if ((local->fop == GF_FOP_REMOVEXATTR) ||
        (local->fop == GF_FOP_FREMOVEXATTR)) {
        /* Check if any custom key xattr present in local->key
         */
        for (i = 0; xattrs_to_heal[i]; i++)
            if (strstr(local->key, xattrs_to_heal[i]))
                uxattr_key_found = _gf_true;
    }

    /* If there is no custom key xattr present or gfid is root
       or call_cnt is 1 then wind a (f)setxattr call on all subvols
    */
    if (!uxattr_key_found || __is_root_gfid(local->gfid) || call_cnt == 1) {
        for (i = 0; i < conf->subvolume_cnt; i++) {
            travvol = conf->subvolumes[i];
            if ((local->fop == GF_FOP_SETXATTR) ||
                (local->fop == GF_FOP_FSETXATTR)) {
                if (fd) {
                    STACK_WIND_COOKIE(frame, dht_err_cbk, travvol, travvol,
                                      travvol->fops->fsetxattr, fd, xattr,
                                      flags, xdata);
                } else {
                    STACK_WIND_COOKIE(frame, dht_err_cbk, travvol, travvol,
                                      travvol->fops->setxattr, loc, xattr,
                                      flags, xdata);
                }
            }

            if ((local->fop == GF_FOP_REMOVEXATTR) ||
                (local->fop == GF_FOP_FREMOVEXATTR)) {
                if (fd) {
                    STACK_WIND_COOKIE(frame, dht_err_cbk, travvol, travvol,
                                      travvol->fops->fremovexattr, fd,
                                      local->key, local->xattr_req);
                } else {
                    STACK_WIND_COOKIE(frame, dht_err_cbk, travvol, travvol,
                                      travvol->fops->removexattr, loc,
                                      local->key, local->xattr_req);
                }
            }
        }

        return 0;
    }

    /* Calculate hash subvol based on inode and parent inode
     */
    if (fd) {
        ret = dht_inode_ctx_mdsvol_get(fd->inode, this, &mds_subvol);
    } else {
        ret = dht_inode_ctx_mdsvol_get(loc->inode, this, &mds_subvol);
    }
    if (ret || !mds_subvol) {
        if (fd) {
            gf_msg(this->name, GF_LOG_ERROR, 0,
                   DHT_MSG_HASHED_SUBVOL_GET_FAILED,
                   "Failed to get mds subvol for fd %p"
                   "gfid is %s ",
                   fd, gfid_local);
        } else {
            gf_msg(this->name, GF_LOG_ERROR, 0,
                   DHT_MSG_HASHED_SUBVOL_GET_FAILED,
                   "%s: Failed to get mds subvol. (gfid is %s)", loc->path,
                   gfid_local);
        }
        (*op_errno) = ENOENT;
        goto err;
    }

    local->mds_subvol = mds_subvol;

    for (i = 0; i < conf->subvolume_cnt; i++) {
        if (conf->subvolumes[i] == mds_subvol) {
            if (!conf->subvolume_status[i]) {
                gf_msg(this->name, GF_LOG_WARNING, 0,
                       DHT_MSG_HASHED_SUBVOL_DOWN,
                       "MDS subvol is down for path "
                       " %s gfid is %s Unable to set xattr ",
                       local->loc.path, gfid_local);
                (*op_errno) = ENOTCONN;
                goto err;
            }
        }
    }

    if (uxattr_key_found) {
        xattrop = dict_new();
        if (!xattrop) {
            gf_msg(this->name, GF_LOG_ERROR, DHT_MSG_NO_MEMORY, 0,
                   "dictionary creation failed for path %s "
                   "for gfid is %s ",
                   local->loc.path, gfid_local);
            (*op_errno) = ENOMEM;
            goto err;
        }
        local->xattr = dict_ref(xattr);
        /* Subtract current MDS xattr value to -1 , value of MDS
           xattr represents no. of times xattr modification failed
           on non MDS subvols.
        */
        ret = dht_dict_set_array(xattrop, conf->mds_xattr_key, subone, 1);
        if (ret != 0) {
            gf_msg(this->name, GF_LOG_ERROR, 0, DHT_MSG_DICT_SET_FAILED,
                   "dictionary set array failed for path %s "
                   "for gfid is %s ",
                   local->loc.path, gfid_local);
            if (xattrop)
                dict_unref(xattrop);
            (*op_errno) = ret;
            goto err;
        }
        /* Wind a xattrop call to use ref counting approach
           update mds xattr to -1 before update xattr on
           hashed subvol and update mds xattr to +1 after update
           xattr on all non hashed subvol
        */
        if (fd) {
            STACK_WIND(frame, dht_xattrop_mds_cbk, local->mds_subvol,
                       local->mds_subvol->fops->fxattrop, fd,
                       GF_XATTROP_ADD_ARRAY, xattrop, NULL);
        } else {
            STACK_WIND(frame, dht_xattrop_mds_cbk, local->mds_subvol,
                       local->mds_subvol->fops->xattrop, loc,
                       GF_XATTROP_ADD_ARRAY, xattrop, NULL);
        }
        if (xattrop)
            dict_unref(xattrop);
    }

    return 0;
err:
    return -1;
}

int
dht_fsetxattr(call_frame_t *frame, xlator_t *this, fd_t *fd, dict_t *xattr,
              int flags, dict_t *xdata)
{
    xlator_t *subvol = NULL;
    dht_local_t *local = NULL;
    int op_errno = EINVAL;
    dht_conf_t *conf = NULL;
    dht_layout_t *layout = NULL;
    int ret = -1;
    int call_cnt = 0;

    VALIDATE_OR_GOTO(frame, err);
    VALIDATE_OR_GOTO(this, err);
    VALIDATE_OR_GOTO(fd, err);
    VALIDATE_OR_GOTO(fd->inode, err);
    VALIDATE_OR_GOTO(this->private, err);

    conf = this->private;

    if (!conf->defrag)
        GF_IF_INTERNAL_XATTR_GOTO(conf->wild_xattr_name, xattr, op_errno, err);

    local = dht_local_init(frame, NULL, fd, GF_FOP_FSETXATTR);
    if (!local) {
        op_errno = ENOMEM;
        goto err;
    }

    subvol = local->cached_subvol;
    if (!subvol) {
        gf_msg_debug(this->name, 0, "no cached subvolume for fd=%p", fd);
        op_errno = EINVAL;
        goto err;
    }

    layout = local->layout;
    if (!layout) {
        gf_msg_debug(this->name, 0, "no layout for fd=%p", fd);
        op_errno = EINVAL;
        goto err;
    }

    local->xattr_req = xdata ? dict_ref(xdata) : dict_new();
    local->call_cnt = call_cnt = layout->cnt;

    if (IA_ISDIR(fd->inode->ia_type)) {
        local->hashed_subvol = NULL;
        ret = dht_dir_common_set_remove_xattr(frame, this, NULL, fd, xattr,
                                              flags, xdata, &op_errno);
        if (ret)
            goto err;
    } else {
        local->call_cnt = 1;
        local->rebalance.xattr = dict_ref(xattr);
        local->rebalance.flags = flags;

        ret = dict_set_int8(local->xattr_req, DHT_IATT_IN_XDATA_KEY, 1);
        if (ret) {
            gf_msg_debug(this->name, 0,
                         "Failed to set dictionary key %s for fd=%p",
                         DHT_IATT_IN_XDATA_KEY, fd);
        }

        STACK_WIND_COOKIE(frame, dht_file_setxattr_cbk, subvol, subvol,
                          subvol->fops->fsetxattr, fd, xattr, flags,
                          local->xattr_req);
    }
    return 0;

err:
    op_errno = (op_errno == -1) ? errno : op_errno;
    DHT_STACK_UNWIND(fsetxattr, frame, -1, op_errno, NULL);

    return 0;
}

static int
dht_checking_pathinfo_cbk(call_frame_t *frame, void *cookie, xlator_t *this,
                          int op_ret, int op_errno, dict_t *xattr,
                          dict_t *xdata)
{
    int i = -1;
    int ret = -1;
    char *value = NULL;
    dht_local_t *local = NULL;
    dht_conf_t *conf = NULL;
    xlator_t *prev = NULL;
    int this_call_cnt = 0;

    local = frame->local;
    prev = cookie;
    conf = this->private;

    if (op_ret == -1)
        goto out;

    ret = dict_get_str(xattr, GF_XATTR_PATHINFO_KEY, &value);
    if (ret)
        goto out;

    if (!strcmp(value, local->key)) {
        for (i = 0; i < conf->subvolume_cnt; i++) {
            if (conf->subvolumes[i] == prev)
                conf->decommissioned_bricks[i] = prev;
        }
    }

out:
    this_call_cnt = dht_frame_return(frame);
    if (is_last_call(this_call_cnt)) {
        DHT_STACK_UNWIND(setxattr, frame, local->op_ret, ENOTSUP, NULL);
    }
    return 0;
}

static int
dht_nuke_dir_cbk(call_frame_t *frame, void *cookie, xlator_t *this,
                 int32_t op_ret, int32_t op_errno, struct iatt *preparent,
                 struct iatt *postparent, dict_t *xdata)
{
    STACK_UNWIND_STRICT(setxattr, frame, op_ret, op_errno, NULL);
    return 0;
}

static int
dht_nuke_dir(call_frame_t *frame, xlator_t *this, loc_t *loc, data_t *tmp)
{
    if (!IA_ISDIR(loc->inode->ia_type)) {
        DHT_STACK_UNWIND(setxattr, frame, -1, ENOTSUP, NULL);
        return 0;
    }

    /* Setxattr didn't need the parent, but rmdir does. */
    loc->parent = inode_parent(loc->inode, NULL, NULL);
    if (!loc->parent) {
        DHT_STACK_UNWIND(setxattr, frame, -1, ENOENT, NULL);
        return 0;
    }
    gf_uuid_copy(loc->pargfid, loc->parent->gfid);

    if (!loc->name && loc->path) {
        loc->name = strrchr(loc->path, '/');
        if (loc->name) {
            ++(loc->name);
        }
    }

    /*
     * We do this instead of calling dht_rmdir_do directly for two reasons.
     * The first is that we want to reuse all of the initialization that
     * dht_rmdir does, so if it ever changes we'll just follow along.  The
     * second (i.e. why we don't use STACK_WIND_TAIL) is so that we don't
     * obscure the fact that we came in via this path instead of a genuine
     * rmdir.  That makes debugging just a tiny bit easier.
     */
    STACK_WIND(frame, dht_nuke_dir_cbk, this, this->fops->rmdir, loc, 1, NULL);

    return 0;
}

int
dht_setxattr(call_frame_t *frame, xlator_t *this, loc_t *loc, dict_t *xattr,
             int flags, dict_t *xdata)
{
    xlator_t *subvol = NULL;
    dht_local_t *local = NULL;
    dht_conf_t *conf = NULL;
    dht_methods_t *methods = NULL;
    dht_layout_t *layout = NULL;
    int i = 0;
    int op_errno = EINVAL;
    int ret = -1;
    data_t *tmp = NULL;
    uint32_t dir_spread = 0;
    char value[4096] = {
        0,
    };
    gf_dht_migrate_data_type_t forced_rebalance = GF_DHT_MIGRATE_DATA;
    int call_cnt = 0;
    uint32_t new_hash = 0;

    VALIDATE_OR_GOTO(frame, err);
    VALIDATE_OR_GOTO(this, err);
    VALIDATE_OR_GOTO(loc, err);
    VALIDATE_OR_GOTO(loc->inode, err);

    conf = this->private;
    GF_VALIDATE_OR_GOTO(this->name, conf, err);

    methods = &(conf->methods);

    /* Rebalance daemon is allowed to set internal keys */
    if (!conf->defrag)
        GF_IF_INTERNAL_XATTR_GOTO(conf->wild_xattr_name, xattr, op_errno, err);

    local = dht_local_init(frame, loc, NULL, GF_FOP_SETXATTR);
    if (!local) {
        op_errno = ENOMEM;
        goto err;
    }

    subvol = local->cached_subvol;
    if (!subvol) {
        gf_msg_debug(this->name, 0, "no cached subvolume for path=%s",
                     loc->path);
        op_errno = EINVAL;
        goto err;
    }

    layout = local->layout;
    if (!layout) {
        gf_msg_debug(this->name, 0, "no layout for path=%s", loc->path);
        op_errno = EINVAL;
        goto err;
    }

    local->call_cnt = call_cnt = layout->cnt;
    tmp = dict_get(xattr, conf->mds_xattr_key);
    if (tmp) {
        op_errno = ENOTSUP;
        goto err;
    }

    tmp = dict_get(xattr, GF_XATTR_FILE_MIGRATE_KEY);
    if (tmp) {
        if (IA_ISDIR(loc->inode->ia_type)) {
            op_errno = ENOTSUP;
            goto err;
        }

        /* TODO: need to interpret the 'value' for more meaning
           (ie, 'target' subvolume given there, etc) */
        memcpy(value, tmp->data, tmp->len);
        if (strcmp(value, "force") == 0)
            forced_rebalance = GF_DHT_MIGRATE_DATA_EVEN_IF_LINK_EXISTS;

        if (conf->decommission_in_progress)
            forced_rebalance = GF_DHT_MIGRATE_HARDLINK;

        if (!loc->path) {
            op_errno = EINVAL;
            goto err;
        }

        if (!local->loc.name)
            local->loc.name = strrchr(local->loc.path, '/') + 1;

        if (!local->loc.parent)
            local->loc.parent = inode_parent(local->loc.inode, NULL, NULL);

        if ((!local->loc.name) || (!local->loc.parent)) {
            op_errno = EINVAL;
            goto err;
        }

        if (gf_uuid_is_null(local->loc.pargfid))
            gf_uuid_copy(local->loc.pargfid, local->loc.parent->gfid);

        methods->migration_get_dst_subvol(this, local);

        if (!local->rebalance.target_node) {
            gf_msg(this->name, GF_LOG_ERROR, 0,
                   DHT_MSG_HASHED_SUBVOL_GET_FAILED,
                   "Failed to get hashed subvol for %s", loc->path);
            op_errno = EINVAL;
            goto err;
        }

        local->rebalance.from_subvol = local->cached_subvol;

        if (local->rebalance.target_node == local->rebalance.from_subvol) {
            op_errno = EEXIST;
            goto err;
        }
        if (local->rebalance.target_node) {
            local->flags = forced_rebalance;

            ret = dht_start_rebalance_task(this, frame);
            if (!ret)
                return 0;

            gf_msg(this->name, GF_LOG_ERROR, 0, DHT_MSG_REBALANCE_START_FAILED,
                   "%s: failed to create a new rebalance synctask", loc->path);
        }
        op_errno = EINVAL;
        goto err;
    }

    tmp = dict_get(xattr, "decommission-brick");
    if (tmp) {
        /* This operation should happen only on '/' */
        if (!__is_root_gfid(loc->inode->gfid)) {
            op_errno = ENOTSUP;
            goto err;
        }

        memcpy(value, tmp->data, min(tmp->len, 4095));
        local->key = gf_strdup(value);
        local->call_cnt = conf->subvolume_cnt;

        for (i = 0; i < conf->subvolume_cnt; i++) {
            /* Get the pathinfo, and then compare */
            STACK_WIND_COOKIE(frame, dht_checking_pathinfo_cbk,
                              conf->subvolumes[i], conf->subvolumes[i],
                              conf->subvolumes[i]->fops->getxattr, loc,
                              GF_XATTR_PATHINFO_KEY, NULL);
        }
        return 0;
    }

    tmp = dict_get(xattr, GF_XATTR_FIX_LAYOUT_KEY);
    if (tmp) {
        ret = dict_get_uint32(xattr, "new-commit-hash", &new_hash);
        if (ret == 0) {
            gf_msg_debug(this->name, 0,
                         "updating commit hash for %s from %u to %u",
                         uuid_utoa(loc->gfid), layout->commit_hash, new_hash);
            layout->commit_hash = new_hash;

            ret = dht_update_commit_hash_for_layout(frame);
            if (ret) {
                op_errno = ENOTCONN;
                goto err;
            }
            return ret;
        }

        gf_msg(this->name, GF_LOG_INFO, 0, DHT_MSG_FIX_LAYOUT_INFO,
               "fixing the layout of %s", loc->path);

        ret = dht_fix_directory_layout(frame, dht_fix_layout_setxattr_cbk,
                                       layout);
        if (ret) {
            op_errno = ENOTCONN;
            goto err;
        }
        return ret;
    }

    tmp = dict_get(xattr, "distribute.directory-spread-count");
    if (tmp) {
        /* Setxattr value is packed as 'binary', not string */
        memcpy(value, tmp->data, min(tmp->len, 4095));
        ret = gf_string2uint32(value, &dir_spread);
        if (!ret && ((dir_spread <= conf->subvolume_cnt) && (dir_spread > 0))) {
            layout->spread_cnt = dir_spread;

            ret = dht_fix_directory_layout(frame, dht_common_setxattr_cbk,
                                           layout);
            if (ret) {
                op_errno = ENOTCONN;
                goto err;
            }
            return ret;
        }
        gf_msg(this->name, GF_LOG_ERROR, 0, DHT_MSG_OPERATION_NOT_SUP,
               "wrong 'directory-spread-count' value (%s)", value);
        op_errno = ENOTSUP;
        goto err;
    }

    tmp = dict_get(xattr, "glusterfs.dht.nuke");
    if (tmp) {
        return dht_nuke_dir(frame, this, loc, tmp);
    }
    local->xattr_req = xdata ? dict_ref(xdata) : dict_new();

    if (IA_ISDIR(loc->inode->ia_type)) {
        local->hashed_subvol = NULL;
        ret = dht_dir_common_set_remove_xattr(frame, this, loc, NULL, xattr,
                                              flags, xdata, &op_errno);
        if (ret)
            goto err;
    } else {
        local->rebalance.xattr = dict_ref(xattr);
        local->rebalance.flags = flags;
        local->call_cnt = 1;

        ret = dict_set_int8(local->xattr_req, DHT_IATT_IN_XDATA_KEY, 1);

        STACK_WIND_COOKIE(frame, dht_file_setxattr_cbk, subvol, subvol,
                          subvol->fops->setxattr, loc, xattr, flags,
                          local->xattr_req);
    }

    return 0;

err:
    op_errno = (op_errno == -1) ? errno : op_errno;
    DHT_STACK_UNWIND(setxattr, frame, -1, op_errno, NULL);

    return 0;
}

static int
dht_removexattr2(xlator_t *this, xlator_t *subvol, call_frame_t *frame, int ret)
{
    dht_local_t *local = NULL;
    int op_errno = EINVAL;

    if (!frame || !frame->local)
        goto err;

    local = frame->local;
    op_errno = local->op_errno;

    local->call_cnt = 2; /* This is the second attempt */

    if (we_are_not_migrating(ret)) {
        /* This dht xlator is not migrating the file. Unwind and
         * pass on the original mode bits so the higher DHT layer
         * can handle this.
         */
        DHT_STACK_UNWIND(removexattr, frame, local->op_ret, local->op_errno,
                         local->rebalance.xdata);
        return 0;
    }

    if (subvol == NULL)
        goto err;

    if (local->fop == GF_FOP_REMOVEXATTR) {
        STACK_WIND_COOKIE(frame, dht_file_removexattr_cbk, subvol, subvol,
                          subvol->fops->removexattr, &local->loc, local->key,
                          local->xattr_req);
    } else {
        STACK_WIND_COOKIE(frame, dht_file_removexattr_cbk, subvol, subvol,
                          subvol->fops->fremovexattr, local->fd, local->key,
                          local->xattr_req);
    }

    return 0;

err:
    DHT_STACK_UNWIND(removexattr, frame, -1, op_errno, NULL);
    return 0;
}

int
dht_file_removexattr_cbk(call_frame_t *frame, void *cookie, xlator_t *this,
                         int op_ret, int op_errno, dict_t *xdata)
{
    int ret = -1;
    dht_local_t *local = NULL;
    xlator_t *prev = NULL;
    struct iatt *stbuf = NULL;
    inode_t *inode = NULL;
    xlator_t *subvol1 = NULL, *subvol2 = NULL;

    local = frame->local;
    prev = cookie;

    local->op_errno = op_errno;

    if ((local->fop == GF_FOP_FREMOVEXATTR) &&
        dht_check_remote_fd_failed_error(local, op_ret, op_errno)) {
        ret = dht_check_and_open_fd_on_subvol(this, frame);
        if (ret)
            goto out;
        return 0;
    }

    if ((op_ret == -1) && !dht_inode_missing(op_errno)) {
        gf_msg_debug(this->name, op_errno, "subvolume %s returned -1",
                     prev->name);
        goto out;
    }

    if (local->call_cnt != 1)
        goto out;

    ret = dict_get_bin(xdata, DHT_IATT_IN_XDATA_KEY, (void **)&stbuf);

    if ((!op_ret) && !stbuf) {
        goto out;
    }

    local->op_ret = 0;

    local->rebalance.target_op_fn = dht_removexattr2;
    if (xdata)
        local->rebalance.xdata = dict_ref(xdata);

    /* Phase 2 of migration */
    if ((op_ret == -1) || IS_DHT_MIGRATION_PHASE2(stbuf)) {
        ret = dht_rebalance_complete_check(this, frame);
        if (!ret)
            return 0;
    }

    /* Phase 1 of migration */
    if (IS_DHT_MIGRATION_PHASE1(stbuf)) {
        inode = (local->fd) ? local->fd->inode : local->loc.inode;

        ret = dht_inode_ctx_get_mig_info(this, inode, &subvol1, &subvol2);
        if (!dht_mig_info_is_invalid(local->cached_subvol, subvol1, subvol2)) {
            dht_removexattr2(this, subvol2, frame, 0);
            return 0;
        }

        ret = dht_rebalance_in_progress_check(this, frame);
        if (!ret)
            return 0;
    }

out:
    if (local->fop == GF_FOP_REMOVEXATTR) {
        DHT_STACK_UNWIND(removexattr, frame, op_ret, op_errno, xdata);
    } else {
        DHT_STACK_UNWIND(fremovexattr, frame, op_ret, op_errno, xdata);
    }
    return 0;
}

int
dht_removexattr(call_frame_t *frame, xlator_t *this, loc_t *loc,
                const char *key, dict_t *xdata)
{
    xlator_t *subvol = NULL;
    int op_errno = -1;
    dht_local_t *local = NULL;
    dht_layout_t *layout = NULL;
    int call_cnt = 0;
    dht_conf_t *conf = NULL;
    int ret = 0;

    VALIDATE_OR_GOTO(this, err);
    VALIDATE_OR_GOTO(this->private, err);

    conf = this->private;

    GF_IF_NATIVE_XATTR_GOTO(conf->wild_xattr_name, key, op_errno, err);

    VALIDATE_OR_GOTO(frame, err);
    VALIDATE_OR_GOTO(loc, err);
    VALIDATE_OR_GOTO(loc->inode, err);

    local = dht_local_init(frame, loc, NULL, GF_FOP_REMOVEXATTR);
    if (!local) {
        op_errno = ENOMEM;
        goto err;
    }

    subvol = local->cached_subvol;
    if (!subvol) {
        gf_msg_debug(this->name, 0, "no cached subvolume for path=%s",
                     loc->path);
        op_errno = EINVAL;
        goto err;
    }

    layout = local->layout;
    if (!local->layout) {
        gf_msg_debug(this->name, 0, "no layout for path=%s", loc->path);
        op_errno = EINVAL;
        goto err;
    }
    local->xattr_req = (xdata) ? dict_ref(xdata) : dict_new();

    local->call_cnt = call_cnt = layout->cnt;
    local->key = gf_strdup(key);

    if (key && (strncmp(key, conf->mds_xattr_key, strlen(key)) == 0)) {
        op_errno = ENOTSUP;
        goto err;
    }

    if (IA_ISDIR(loc->inode->ia_type)) {
        local->hashed_subvol = NULL;
        ret = dht_dir_common_set_remove_xattr(frame, this, loc, NULL, NULL, 0,
                                              local->xattr_req, &op_errno);
        if (ret)
            goto err;

    } else {
        local->call_cnt = 1;
        ret = dict_set_int8(local->xattr_req, DHT_IATT_IN_XDATA_KEY, 1);
        if (ret) {
            gf_msg(this->name, GF_LOG_ERROR, ENOMEM, DHT_MSG_DICT_SET_FAILED,
                   "Failed to "
                   "set dictionary key %s for %s",
                   DHT_IATT_IN_XDATA_KEY, loc->path);
        }

        STACK_WIND_COOKIE(frame, dht_file_removexattr_cbk, subvol, subvol,
                          subvol->fops->removexattr, loc, key,
                          local->xattr_req);
    }

    return 0;

err:
    op_errno = (op_errno == -1) ? errno : op_errno;
    DHT_STACK_UNWIND(removexattr, frame, -1, op_errno, NULL);

    return 0;
}

int
dht_fremovexattr(call_frame_t *frame, xlator_t *this, fd_t *fd, const char *key,
                 dict_t *xdata)
{
    xlator_t *subvol = NULL;
    int op_errno = -1;
    dht_local_t *local = NULL;
    dht_layout_t *layout = NULL;
    int call_cnt = 0;
    dht_conf_t *conf = 0;
    int ret = 0;

    VALIDATE_OR_GOTO(this, err);
    VALIDATE_OR_GOTO(this->private, err);

    conf = this->private;

    GF_IF_NATIVE_XATTR_GOTO(conf->wild_xattr_name, key, op_errno, err);

    VALIDATE_OR_GOTO(frame, err);

    local = dht_local_init(frame, NULL, fd, GF_FOP_FREMOVEXATTR);
    if (!local) {
        op_errno = ENOMEM;
        goto err;
    }

    subvol = local->cached_subvol;
    if (!subvol) {
        gf_msg_debug(this->name, 0, "no cached subvolume for inode=%s",
                     uuid_utoa(fd->inode->gfid));
        op_errno = EINVAL;
        goto err;
    }

    layout = local->layout;
    if (!local->layout) {
        gf_msg_debug(this->name, 0, "no layout for inode=%s",
                     uuid_utoa(fd->inode->gfid));
        op_errno = EINVAL;
        goto err;
    }
    local->xattr_req = xdata ? dict_ref(xdata) : dict_new();

    local->call_cnt = call_cnt = layout->cnt;
    local->key = gf_strdup(key);

    if (IA_ISDIR(fd->inode->ia_type)) {
        local->hashed_subvol = NULL;
        ret = dht_dir_common_set_remove_xattr(frame, this, NULL, fd, NULL, 0,
                                              local->xattr_req, &op_errno);
        if (ret)
            goto err;

    } else {
        local->call_cnt = 1;
        ret = dict_set_int8(local->xattr_req, DHT_IATT_IN_XDATA_KEY, 1);
        if (ret) {
            gf_msg(this->name, GF_LOG_ERROR, ENOMEM, DHT_MSG_DICT_SET_FAILED,
                   "Failed to "
                   "set dictionary key %s for fd=%p",
                   DHT_IATT_IN_XDATA_KEY, fd);
        }

        STACK_WIND_COOKIE(frame, dht_file_removexattr_cbk, subvol, subvol,
                          subvol->fops->fremovexattr, fd, key,
                          local->xattr_req);
    }

    return 0;

err:
    op_errno = (op_errno == -1) ? errno : op_errno;
    DHT_STACK_UNWIND(fremovexattr, frame, -1, op_errno, NULL);

    return 0;
}

int
dht_fd_cbk(call_frame_t *frame, void *cookie, xlator_t *this, int op_ret,
           int op_errno, fd_t *fd, dict_t *xdata)
{
    dht_local_t *local = NULL;
    int this_call_cnt = 0;
    xlator_t *prev = NULL;

    local = frame->local;
    prev = cookie;

    LOCK(&frame->lock);
    {
        if (op_ret == -1) {
            local->op_errno = op_errno;
            UNLOCK(&frame->lock);
            gf_msg_debug(this->name, op_errno, "subvolume %s returned -1",
                         prev->name);
            goto post_unlock;
        }

        local->op_ret = 0;
    }
    UNLOCK(&frame->lock);
post_unlock:
    this_call_cnt = dht_frame_return(frame);
    if (is_last_call(this_call_cnt))
        DHT_STACK_UNWIND(open, frame, local->op_ret, local->op_errno, local->fd,
                         NULL);

    return 0;
}

/*
 * dht_normalize_stats -
 */
static void
dht_normalize_stats(struct statvfs *buf, unsigned long bsize,
                    unsigned long frsize)
{
    double factor = 0;

    if (buf->f_bsize != bsize) {
        buf->f_bsize = bsize;
    }

    if (buf->f_frsize != frsize) {
        factor = ((double)buf->f_frsize) / frsize;
        buf->f_frsize = frsize;
        buf->f_blocks = (fsblkcnt_t)(factor * buf->f_blocks);
        buf->f_bfree = (fsblkcnt_t)(factor * buf->f_bfree);
        buf->f_bavail = (fsblkcnt_t)(factor * buf->f_bavail);
    }
}

static int
dht_statfs_cbk(call_frame_t *frame, void *cookie, xlator_t *this, int op_ret,
               int op_errno, struct statvfs *statvfs, dict_t *xdata)
{
    gf_boolean_t event = _gf_false;
    qdstatfs_action_t action = qdstatfs_action_OFF;
    dht_local_t *local = NULL;
    int this_call_cnt = 0;
    int bsize = 0;
    int frsize = 0;
    GF_UNUSED int ret = 0;
    unsigned long new_usage = 0;
    unsigned long cur_usage = 0;

    local = frame->local;
    GF_ASSERT(local);

    if (xdata)
        ret = dict_get_int8(xdata, "quota-deem-statfs", (int8_t *)&event);

    LOCK(&frame->lock);
    {
        if (op_ret == -1) {
            local->op_errno = op_errno;
            goto unlock;
        }
        if (!statvfs) {
            op_errno = EINVAL;
            local->op_ret = -1;
            goto unlock;
        }
        local->op_ret = 0;

        if (local->quota_deem_statfs) {
            if (event == _gf_true) {
                action = qdstatfs_action_COMPARE;
            } else {
                action = qdstatfs_action_NEGLECT;
            }
        } else {
            if (event == _gf_true) {
                action = qdstatfs_action_REPLACE;
                local->quota_deem_statfs = _gf_true;
            }
        }

        if (local->quota_deem_statfs) {
            switch (action) {
                case qdstatfs_action_NEGLECT:
                    goto unlock;

                case qdstatfs_action_REPLACE:
                    local->statvfs = *statvfs;
                    goto unlock;

                case qdstatfs_action_COMPARE:
                    new_usage = statvfs->f_blocks - statvfs->f_bfree;
                    cur_usage = local->statvfs.f_blocks -
                                local->statvfs.f_bfree;

                    /* Take the max of the usage from subvols */
                    if (new_usage >= cur_usage)
                        local->statvfs = *statvfs;
                    goto unlock;

                default:
                    break;
            }
        }

        if (local->statvfs.f_bsize != 0) {
            bsize = max(local->statvfs.f_bsize, statvfs->f_bsize);
            frsize = max(local->statvfs.f_frsize, statvfs->f_frsize);
            dht_normalize_stats(&local->statvfs, bsize, frsize);
            dht_normalize_stats(statvfs, bsize, frsize);
        } else {
            local->statvfs.f_bsize = statvfs->f_bsize;
            local->statvfs.f_frsize = statvfs->f_frsize;
        }

        local->statvfs.f_blocks += statvfs->f_blocks;
        local->statvfs.f_bfree += statvfs->f_bfree;
        local->statvfs.f_bavail += statvfs->f_bavail;
        local->statvfs.f_files += statvfs->f_files;
        local->statvfs.f_ffree += statvfs->f_ffree;
        local->statvfs.f_favail += statvfs->f_favail;
        local->statvfs.f_fsid = statvfs->f_fsid;
        local->statvfs.f_flag = statvfs->f_flag;
        local->statvfs.f_namemax = statvfs->f_namemax;
    }
unlock:
    UNLOCK(&frame->lock);

    this_call_cnt = dht_frame_return(frame);
    if (is_last_call(this_call_cnt))
        DHT_STACK_UNWIND(statfs, frame, local->op_ret, local->op_errno,
                         &local->statvfs, xdata);

    return 0;
}

int
dht_statfs(call_frame_t *frame, xlator_t *this, loc_t *loc, dict_t *xdata)
{
    dht_local_t *local = NULL;
    dht_conf_t *conf = NULL;
    int op_errno = -1;
    int i = -1;
    inode_t *inode = NULL;
    inode_table_t *itable = NULL;
    static uuid_t root_gfid = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1};
    loc_t newloc = {
        0,
    };

    VALIDATE_OR_GOTO(frame, err);
    VALIDATE_OR_GOTO(this, err);
    VALIDATE_OR_GOTO(loc, err);
    VALIDATE_OR_GOTO(this->private, err);

    conf = this->private;

    local = dht_local_init(frame, NULL, NULL, GF_FOP_STATFS);
    if (!local) {
        op_errno = ENOMEM;
        goto err;
    }

    if (loc->inode && !IA_ISDIR(loc->inode->ia_type)) {
        itable = loc->inode->table;
        if (!itable) {
            op_errno = EINVAL;
            goto err;
        }

        loc = &local->loc2;

        inode = inode_find(itable, root_gfid);
        if (!inode) {
            op_errno = EINVAL;
            goto err;
        }

        dht_build_root_loc(inode, &newloc);
        loc = &newloc;
    }

    local->call_cnt = conf->subvolume_cnt;

    for (i = 0; i < conf->subvolume_cnt; i++) {
        STACK_WIND(frame, dht_statfs_cbk, conf->subvolumes[i],
                   conf->subvolumes[i]->fops->statfs, loc, xdata);
    }
    return 0;

err:
    op_errno = (op_errno == -1) ? errno : op_errno;
    DHT_STACK_UNWIND(statfs, frame, -1, op_errno, NULL, NULL);

    return 0;
}

int
dht_opendir(call_frame_t *frame, xlator_t *this, loc_t *loc, fd_t *fd,
            dict_t *xdata)
{
    dht_local_t *local = NULL;
    dht_conf_t *conf = NULL;
    int op_errno = -1;
    int i = -1;
    int ret = 0;
    gf_boolean_t new_xdata = _gf_false;
    xlator_t **subvolumes = NULL;
    int call_count = 0;

    VALIDATE_OR_GOTO(frame, err);
    VALIDATE_OR_GOTO(this, err);
    VALIDATE_OR_GOTO(fd, err);
    VALIDATE_OR_GOTO(this->private, err);

    conf = this->private;

    local = dht_local_init(frame, loc, fd, GF_FOP_OPENDIR);
    if (!local) {
        op_errno = ENOMEM;
        goto err;
    }
    local->first_up_subvol = dht_first_up_subvol(this);

    if (!xdata) {
        xdata = dict_new();
        if (!xdata) {
            op_errno = ENOMEM;
            goto err;
        }
        new_xdata = _gf_true;
    }

    ret = dict_set_uint32(xdata, conf->link_xattr_name, 256);
    if (ret)
        gf_msg(this->name, GF_LOG_WARNING, 0, DHT_MSG_DICT_SET_FAILED,
               "Failed to set dictionary value : key = %s",
               conf->link_xattr_name);

    /* dht_readdirp will wind to all subvols so open has to be sent to
     * all subvols whether or not conf->local_subvols is set */

    call_count = local->call_cnt = conf->subvolume_cnt;
    subvolumes = conf->subvolumes;

    /* In case of parallel-readdir, the readdir-ahead will be loaded
     * below dht, in this case, if we want to enable or disable SKIP_DIRs
     * it has to be done in opendir, so that prefetching logic in
     * readdir-ahead, honors it */
    for (i = 0; i < call_count; i++) {
        if (conf->readdir_optimize == _gf_true) {
            if (subvolumes[i] != local->first_up_subvol) {
                ret = dict_set_int32(xdata, GF_READDIR_SKIP_DIRS, 1);
                if (ret)
                    gf_msg(this->name, GF_LOG_ERROR, 0, DHT_MSG_DICT_SET_FAILED,
                           "Failed to set dictionary"
                           " value :key = %s, ret:%d",
                           GF_READDIR_SKIP_DIRS, ret);
            }
        }

        STACK_WIND_COOKIE(frame, dht_fd_cbk, subvolumes[i], subvolumes[i],
                          subvolumes[i]->fops->opendir, loc, fd, xdata);
        dict_del(xdata, GF_READDIR_SKIP_DIRS);
    }

    if (new_xdata)
        dict_unref(xdata);

    return 0;

err:
    op_errno = (op_errno == -1) ? errno : op_errno;
    DHT_STACK_UNWIND(opendir, frame, -1, op_errno, NULL, NULL);

    return 0;
}

/* dht_readdirp_cbk creates a new dentry and dentry->inode is not assigned.
   This functions assigns an inode if all of the following conditions are
   true:

   * DHT has only one child. In this case the entire layout is present on
   this single child and hence we can set complete layout in inode.
   * backend has complete layout and there are no anomalies in it and from
   this information layout can be constructed and set in inode.
*/

static void
dht_populate_inode_for_dentry(xlator_t *this, xlator_t *subvol,
                              gf_dirent_t *entry, gf_dirent_t *orig_entry)
{
    dht_layout_t *layout = NULL;
    int ret = 0;
    loc_t loc = {
        0,
    };

    if (gf_uuid_is_null(orig_entry->d_stat.ia_gfid)) {
        /* this skips the '..' entry for the root of the volume */
        return;
    }

    gf_uuid_copy(loc.gfid, orig_entry->d_stat.ia_gfid);
    loc.inode = inode_ref(orig_entry->inode);

    if (is_revalidate(&loc)) {
        goto out;
    }

    layout = dht_layout_new(this, 1);
    if (!layout)
        goto out;

    ret = dht_layout_merge(this, layout, subvol, 0, 0, orig_entry->dict);
    if (!ret) {
        ret = dht_layout_normalize(this, &loc, layout);
        if (ret == 0) {
            dht_layout_set(this, orig_entry->inode, layout);
            entry->inode = inode_ref(orig_entry->inode);
            layout = NULL;
        }
    }

    if (layout)
        dht_layout_unref(this, layout);

out:
    loc_wipe(&loc);
    return;
}

/* Posix returns op_errno = ENOENT to indicate that there are no more
 * entries
 */
static int
dht_readdirp_cbk(call_frame_t *frame, void *cookie, xlator_t *this, int op_ret,
                 int op_errno, gf_dirent_t *orig_entries, dict_t *xdata)
{
    dht_local_t *local = NULL;
    gf_dirent_t entries;
    gf_dirent_t *orig_entry = NULL;
    gf_dirent_t *entry = NULL;
    xlator_t *prev = NULL;
    xlator_t *next_subvol = NULL;
    off_t next_offset = 0;
    int count = 0;
    dht_layout_t *layout = NULL;
    dht_conf_t *conf = NULL;
    dht_methods_t *methods = NULL;
    xlator_t *subvol = 0;
    xlator_t *hashed_subvol = 0;
    int ret = 0;
    int readdir_optimize = 0;
    inode_table_t *itable = NULL;
    inode_t *inode = NULL;
    gf_boolean_t skip_hashed_check = _gf_false;

    INIT_LIST_HEAD(&entries.list);

    prev = cookie;
    local = frame->local;
    GF_VALIDATE_OR_GOTO(this->name, local->fd, unwind);

    itable = local->fd->inode->table;

    conf = this->private;
    GF_VALIDATE_OR_GOTO(this->name, conf, unwind);

    methods = &(conf->methods);

    if (op_ret <= 0) {
        goto done;
    }

    /* Why aren't we skipping DHT entirely in case of a single subvol?
     * Because if this was a larger volume earlier and all but one subvol
     * was removed, there might be stale linkto files on the subvol.
     */
    if (conf->subvolume_cnt == 1) {
        /* return all directory and file entries except
         * linkto files for a single child DHT
         */
        skip_hashed_check = _gf_true;
    }

    if (!local->layout)
        local->layout = dht_layout_get(this, local->fd->inode);

    layout = local->layout;

    /* This will skip the entries on the subvol without a layout,
     * hence preventing the crash but rmdir might fail with
     * "directory not empty" errors*/

    if (layout == NULL)
        goto done;

    if (conf->readdir_optimize == _gf_true)
        readdir_optimize = 1;

    gf_msg_debug(this->name, 0, "Processing entries from %s", prev->name);

    list_for_each_entry(orig_entry, (&orig_entries->list), list)
    {
        next_offset = orig_entry->d_off;

        gf_msg_debug(this->name, 0, "%s: entry = %s, type = %d", prev->name,
                     orig_entry->d_name, orig_entry->d_type);

        if (IA_ISINVAL(orig_entry->d_stat.ia_type)) {
            /*stat failed somewhere- display this entry but the data may
             * be inaccurate.
             */
            gf_msg_debug(this->name, EINVAL, "Invalid stat for %s (gfid %s)",
                         orig_entry->d_name,
                         uuid_utoa(orig_entry->d_stat.ia_gfid));
        }

        if (check_is_linkfile(NULL, (&orig_entry->d_stat), orig_entry->dict,
                              conf->link_xattr_name)) {
            gf_msg_debug(this->name, 0, "%s: %s is a linkto file", prev->name,
                         orig_entry->d_name);
            continue;
        }

        if (skip_hashed_check) {
            goto list;
        }

        if (check_is_dir(NULL, (&orig_entry->d_stat), NULL)) {
            /*Directory entries filtering :
             * a) If rebalance is running, pick from first_up_subvol
             * b) (rebalance not running)hashed subvolume is NULL or
             * down then filter in first_up_subvolume. Other wise the
             * corresponding hashed subvolume will take care of the
             * directory entry.
             */
            if (readdir_optimize) {
                if (prev == local->first_up_subvol)
                    goto list;
                else
                    continue;
            }

            hashed_subvol = methods->layout_search(this, layout,
                                                   orig_entry->d_name);

            if (prev == hashed_subvol)
                goto list;
            if ((hashed_subvol && dht_subvol_status(conf, hashed_subvol)) ||
                (prev != local->first_up_subvol))
                continue;

            goto list;
        }

    list:
        entry = gf_dirent_for_name(orig_entry->d_name);
        if (!entry) {
            goto unwind;
        }

        /* Do this if conf->search_unhashed is set to "auto" */
        if (conf->search_unhashed == GF_DHT_LOOKUP_UNHASHED_AUTO) {
            subvol = methods->layout_search(this, layout, orig_entry->d_name);
            if (!subvol || (subvol != prev)) {
                /* TODO: Count the number of entries which need
                   linkfile to prove its existence in fs */
                layout->search_unhashed++;
            }
        }

        entry->d_off = orig_entry->d_off;
        entry->d_stat = orig_entry->d_stat;
        entry->d_ino = orig_entry->d_ino;
        entry->d_type = orig_entry->d_type;
        entry->d_len = orig_entry->d_len;

        if (orig_entry->dict)
            entry->dict = dict_ref(orig_entry->dict);

        /* making sure we set the inode ctx right with layout,
           currently possible only for non-directories, so for
           directories don't set entry inodes */
        if (IA_ISDIR(entry->d_stat.ia_type)) {
            entry->d_stat.ia_blocks = DHT_DIR_STAT_BLOCKS;
            entry->d_stat.ia_size = DHT_DIR_STAT_SIZE;
            if (orig_entry->inode) {
                dht_inode_ctx_time_update(orig_entry->inode, this,
                                          &entry->d_stat, 1);

                if (conf->subvolume_cnt == 1) {
                    dht_populate_inode_for_dentry(this, prev, entry,
                                                  orig_entry);
                }
            }
        } else {
            if (orig_entry->dict &&
                dict_get(orig_entry->dict, conf->link_xattr_name)) {
                /* Strip out the S and T flags set by rebalance*/
                DHT_STRIP_PHASE1_FLAGS(&entry->d_stat);
            }

            if (orig_entry->inode) {
                ret = dht_layout_preset(this, prev, orig_entry->inode);
                if (ret)
                    gf_msg(this->name, GF_LOG_WARNING, 0,
                           DHT_MSG_LAYOUT_SET_FAILED,
                           "failed to link the layout "
                           "in inode for %s",
                           orig_entry->d_name);

                entry->inode = inode_ref(orig_entry->inode);
            } else if (itable) {
                /*
                 * orig_entry->inode might be null if any upper
                 * layer xlators below client set to null, to
                 * force a lookup on the inode even if the inode
                 * is present in the inode table. In that case
                 * we just update the ctx to make sure we didn't
                 * missed anything.
                 */
                inode = inode_find(itable, orig_entry->d_stat.ia_gfid);
                if (inode) {
                    ret = dht_layout_preset(this, prev, inode);
                    if (ret)
                        gf_msg(this->name, GF_LOG_WARNING, 0,
                               DHT_MSG_LAYOUT_SET_FAILED,
                               "failed to link the layout"
                               " in inode for %s",
                               orig_entry->d_name);
                    inode_unref(inode);
                    inode = NULL;
                }
            }
        }

        gf_msg_debug(this->name, 0, "%s: Adding entry = %s", prev->name,
                     entry->d_name);

        list_add_tail(&entry->list, &entries.list);
        count++;
    }

done:

    /* We need to ensure that only the last subvolume's end-of-directory
     * notification is respected so that directory reading does not stop
     * before all subvolumes have been read. That could happen because the
     * posix for each subvolume sends a ENOENT on end-of-directory but in
     * distribute we're not concerned only with a posix's view of the
     * directory but the aggregated namespace' view of the directory.
     * Possible values:
     * op_ret == 0 and op_errno != 0
     *   if op_errno != ENOENT : Error.Unwind.
     *   if op_errno == ENOENT : There are no more entries on this subvol.
     *                           Move to the next one.
     * op_ret > 0 and count == 0 :
     *    The subvol returned entries to dht but all were stripped out.
     *    For example, if they were linkto files or dirs where
     *    hashed_subvol != prev. Try to get some entries by winding
     *    to the next subvol. This can be dangerous if parallel readdir
     *    is enabled as it grows the stack.
     *
     * op_ret > 0 and count > 0:
     *   We found some entries. Unwind even if the buffer is not full.
     *
     */

    op_ret = count;
    if (count == 0) {
        /* non-zero next_offset means that
         * EOF is not yet hit on the current subvol
         */
        if ((next_offset == 0) || (op_errno == ENOENT)) {
            next_offset = 0;
            next_subvol = dht_subvol_next(this, prev);
        } else {
            next_subvol = prev;
        }

        if (!next_subvol) {
            goto unwind;
        }

        if (conf->readdir_optimize == _gf_true) {
            if (next_subvol != local->first_up_subvol) {
                ret = dict_set_int32(local->xattr, GF_READDIR_SKIP_DIRS, 1);
                if (ret)
                    gf_msg(this->name, GF_LOG_ERROR, 0, DHT_MSG_DICT_SET_FAILED,
                           "Failed to set dictionary value"
                           ":key = %s",
                           GF_READDIR_SKIP_DIRS);
            } else {
                dict_del(local->xattr, GF_READDIR_SKIP_DIRS);
            }
        }

        STACK_WIND_COOKIE(frame, dht_readdirp_cbk, next_subvol, next_subvol,
                          next_subvol->fops->readdirp, local->fd, local->size,
                          next_offset, local->xattr);
        return 0;
    }

unwind:
    /* We need to ensure that only the last subvolume's end-of-directory
     * notification is respected so that directory reading does not stop
     * before all subvolumes have been read. That could happen because the
     * posix for each subvolume sends a ENOENT on end-of-directory but in
     * distribute we're not concerned only with a posix's view of the
     * directory but the aggregated namespace' view of the directory.
     */
    if (op_ret < 0)
        op_ret = 0;

    if (prev != dht_last_up_subvol(this))
        op_errno = 0;

    DHT_STACK_UNWIND(readdirp, frame, op_ret, op_errno, &entries, NULL);

    gf_dirent_free(&entries);
    return 0;
}

static int
dht_readdir_cbk(call_frame_t *frame, void *cookie, xlator_t *this, int op_ret,
                int op_errno, gf_dirent_t *orig_entries, dict_t *xdata)
{
    dht_local_t *local = NULL;
    gf_dirent_t entries;
    gf_dirent_t *orig_entry = NULL;
    gf_dirent_t *entry = NULL;
    xlator_t *prev = NULL;
    xlator_t *next_subvol = NULL;
    off_t next_offset = 0;
    int count = 0;
    dht_layout_t *layout = 0;
    xlator_t *subvol = 0;
    dht_conf_t *conf = NULL;
    dht_methods_t *methods = NULL;
    gf_boolean_t skip_hashed_check = _gf_false;

    INIT_LIST_HEAD(&entries.list);

    prev = cookie;
    local = frame->local;

    conf = this->private;
    GF_VALIDATE_OR_GOTO(this->name, conf, done);

    methods = &(conf->methods);

    if (op_ret <= 0)
        goto done;

    if (!local->layout)
        local->layout = dht_layout_get(this, local->fd->inode);

    layout = local->layout;

    gf_msg_debug(this->name, 0, "Processing entries from %s", prev->name);

    if (conf->subvolume_cnt == 1) {
        /*return everything*/
        skip_hashed_check = _gf_true;
        count = op_ret;
        goto done;
    }

    list_for_each_entry(orig_entry, (&orig_entries->list), list)
    {
        next_offset = orig_entry->d_off;

        gf_msg_debug(this->name, 0, "%s: entry = %s, type = %d", prev->name,
                     orig_entry->d_name, orig_entry->d_type);

        subvol = methods->layout_search(this, layout, orig_entry->d_name);

        if (!subvol || (subvol == prev)) {
            entry = gf_dirent_for_name(orig_entry->d_name);
            if (!entry) {
                gf_msg(this->name, GF_LOG_ERROR, ENOMEM, DHT_MSG_NO_MEMORY,
                       "Memory allocation failed ");
                goto unwind;
            }

            entry->d_off = orig_entry->d_off;
            entry->d_ino = orig_entry->d_ino;
            entry->d_type = orig_entry->d_type;
            entry->d_len = orig_entry->d_len;

            gf_msg_debug(this->name, 0, "%s: Adding = entry %s", prev->name,
                         entry->d_name);

            list_add_tail(&entry->list, &entries.list);
            count++;
        }
    }
done:
    op_ret = count;
    /* We need to ensure that only the last subvolume's end-of-directory
     * notification is respected so that directory reading does not stop
     * before all subvolumes have been read. That could happen because the
     * posix for each subvolume sends a ENOENT on end-of-directory but in
     * distribute we're not concerned only with a posix's view of the
     * directory but the aggregated namespace' view of the directory.
     */
    if (count == 0) {
        if ((next_offset == 0) || (op_errno == ENOENT)) {
            next_offset = 0;
            next_subvol = dht_subvol_next(this, prev);
        } else {
            next_subvol = prev;
        }

        if (!next_subvol) {
            goto unwind;
        }

        STACK_WIND_COOKIE(frame, dht_readdir_cbk, next_subvol, next_subvol,
                          next_subvol->fops->readdir, local->fd, local->size,
                          next_offset, NULL);
        return 0;
    }

unwind:
    /* We need to ensure that only the last subvolume's end-of-directory
     * notification is respected so that directory reading does not stop
     * before all subvolumes have been read. That could happen because the
     * posix for each subvolume sends a ENOENT on end-of-directory but in
     * distribute we're not concerned only with a posix's view of the
     * directory but the aggregated namespace' view of the directory.
     */

    if (prev != dht_last_up_subvol(this))
        op_errno = 0;

    if (!skip_hashed_check) {
        DHT_STACK_UNWIND(readdir, frame, op_ret, op_errno, &entries, NULL);
        gf_dirent_free(&entries);

    } else {
        DHT_STACK_UNWIND(readdir, frame, op_ret, op_errno, orig_entries, NULL);
    }
    return 0;
}

static int
dht_do_readdir(call_frame_t *frame, xlator_t *this, fd_t *fd, size_t size,
               off_t yoff, int whichop, dict_t *dict)
{
    dht_local_t *local = NULL;
    int op_errno = -1;
    xlator_t *xvol = NULL;
    int ret = 0;
    dht_conf_t *conf = NULL;

    VALIDATE_OR_GOTO(frame, err);
    VALIDATE_OR_GOTO(this, err);
    VALIDATE_OR_GOTO(fd, err);
    VALIDATE_OR_GOTO(this->private, err);

    conf = this->private;

    local = dht_local_init(frame, NULL, NULL, whichop);
    if (!local) {
        op_errno = ENOMEM;
        goto err;
    }

    local->fd = fd_ref(fd);
    local->size = size;
    local->xattr_req = (dict) ? dict_ref(dict) : NULL;
    local->first_up_subvol = dht_first_up_subvol(this);
    local->op_ret = -1;

    dht_deitransform(this, yoff, &xvol);

    /* TODO: do proper readdir */
    if (whichop == GF_FOP_READDIRP) {
        if (dict)
            local->xattr = dict_ref(dict);
        else
            local->xattr = dict_new();

        if (local->xattr) {
            ret = dict_set_uint32(local->xattr, conf->link_xattr_name, 256);
            if (ret)
                gf_msg(this->name, GF_LOG_WARNING, 0, DHT_MSG_DICT_SET_FAILED,
                       "Failed to set dictionary value"
                       " : key = %s",
                       conf->link_xattr_name);

            if (conf->readdir_optimize == _gf_true) {
                if (xvol != local->first_up_subvol) {
                    ret = dict_set_int32(local->xattr, GF_READDIR_SKIP_DIRS, 1);
                    if (ret)
                        gf_msg(this->name, GF_LOG_ERROR, 0,
                               DHT_MSG_DICT_SET_FAILED,
                               "Failed to set "
                               "dictionary value: "
                               "key = %s",
                               GF_READDIR_SKIP_DIRS);
                } else {
                    dict_del(local->xattr, GF_READDIR_SKIP_DIRS);
                }
            }

            if (conf->subvolume_cnt == 1) {
                ret = dict_set_uint32(local->xattr, conf->xattr_name, 4 * 4);
                if (ret) {
                    gf_msg(this->name, GF_LOG_WARNING, ENOMEM,
                           DHT_MSG_DICT_SET_FAILED,
                           "Failed to set dictionary "
                           "value:key = %s ",
                           conf->xattr_name);
                }
            }
        }

        STACK_WIND_COOKIE(frame, dht_readdirp_cbk, xvol, xvol,
                          xvol->fops->readdirp, fd, size, yoff, local->xattr);
    } else {
        STACK_WIND_COOKIE(frame, dht_readdir_cbk, xvol, xvol,
                          xvol->fops->readdir, fd, size, yoff, local->xattr);
    }

    return 0;

err:
    op_errno = (op_errno == -1) ? errno : op_errno;
    DHT_STACK_UNWIND(readdir, frame, -1, op_errno, NULL, NULL);

    return 0;
}

int
dht_readdir(call_frame_t *frame, xlator_t *this, fd_t *fd, size_t size,
            off_t yoff, dict_t *xdata)
{
    int op = GF_FOP_READDIR;
    dht_conf_t *conf = NULL;
    int i = 0;

    conf = this->private;
    if (!conf)
        goto out;

    for (i = 0; i < conf->subvolume_cnt; i++) {
        if (!conf->subvolume_status[i]) {
            op = GF_FOP_READDIRP;
            break;
        }
    }

    if (conf->use_readdirp)
        op = GF_FOP_READDIRP;

out:
    dht_do_readdir(frame, this, fd, size, yoff, op, 0);
    return 0;
}

int
dht_readdirp(call_frame_t *frame, xlator_t *this, fd_t *fd, size_t size,
             off_t yoff, dict_t *dict)
{
    dht_do_readdir(frame, this, fd, size, yoff, GF_FOP_READDIRP, dict);
    return 0;
}

static int
dht_fsyncdir_cbk(call_frame_t *frame, void *cookie, xlator_t *this, int op_ret,
                 int op_errno, dict_t *xdata)
{
    dht_local_t *local = NULL;
    int this_call_cnt = 0;

    local = frame->local;

    LOCK(&frame->lock);
    {
        if (op_ret == -1)
            local->op_errno = op_errno;
        else if (op_ret == 0)
            local->op_ret = 0;
    }
    UNLOCK(&frame->lock);
    this_call_cnt = dht_frame_return(frame);
    if (is_last_call(this_call_cnt))
        DHT_STACK_UNWIND(fsyncdir, frame, local->op_ret, local->op_errno,
                         xdata);

    return 0;
}

int
dht_fsyncdir(call_frame_t *frame, xlator_t *this, fd_t *fd, int datasync,
             dict_t *xdata)
{
    dht_local_t *local = NULL;
    dht_conf_t *conf = NULL;
    int op_errno = -1;
    int i = -1;

    VALIDATE_OR_GOTO(frame, err);
    VALIDATE_OR_GOTO(this, err);
    VALIDATE_OR_GOTO(fd, err);
    VALIDATE_OR_GOTO(this->private, err);

    conf = this->private;

    local = dht_local_init(frame, NULL, NULL, GF_FOP_FSYNCDIR);
    if (!local) {
        op_errno = ENOMEM;
        goto err;
    }

    local->fd = fd_ref(fd);
    local->call_cnt = conf->subvolume_cnt;

    for (i = 0; i < conf->subvolume_cnt; i++) {
        STACK_WIND(frame, dht_fsyncdir_cbk, conf->subvolumes[i],
                   conf->subvolumes[i]->fops->fsyncdir, fd, datasync, xdata);
    }

    return 0;

err:
    op_errno = (op_errno == -1) ? errno : op_errno;
    DHT_STACK_UNWIND(fsyncdir, frame, -1, op_errno, NULL);

    return 0;
}

int
dht_newfile_cbk(call_frame_t *frame, void *cookie, xlator_t *this, int op_ret,
                int op_errno, inode_t *inode, struct iatt *stbuf,
                struct iatt *preparent, struct iatt *postparent, dict_t *xdata)
{
    xlator_t *prev = NULL;
    int ret = -1;
    dht_local_t *local = NULL;

    if (op_ret == -1)
        goto out;

    local = frame->local;
    if (!local) {
        op_ret = -1;
        op_errno = EINVAL;
        goto out;
    }

    prev = cookie;

    if (local->loc.parent) {
        dht_inode_ctx_time_update(local->loc.parent, this, preparent, 0);
        dht_inode_ctx_time_update(local->loc.parent, this, postparent, 1);
    }

    ret = dht_layout_preset(this, prev, inode);
    if (ret < 0) {
        gf_msg_debug(this->name, EINVAL,
                     "could not set pre-set layout for subvolume %s",
                     prev ? prev->name : NULL);
        op_ret = -1;
        op_errno = EINVAL;
        goto out;
    }
    if (local->linked == _gf_true)
        dht_linkfile_attr_heal(frame, this);
out:
    /*
     * FIXME: ia_size and st_blocks of preparent and postparent do not have
     * correct values. since, preparent and postparent buffers correspond
     * to a directory these two members should have values equal to sum of
     * corresponding values from each of the subvolume.
     * See dht_iatt_merge for reference.
     */
    DHT_STRIP_PHASE1_FLAGS(stbuf);
    dht_set_fixed_dir_stat(postparent);
    dht_set_fixed_dir_stat(preparent);

    if (local && local->lock[0].layout.parent_layout.locks) {
        /* store op_errno for failure case*/
        local->op_errno = op_errno;
        local->refresh_layout_unlock(frame, this, op_ret, 1);

        if (op_ret == 0) {
            DHT_STACK_UNWIND(mknod, frame, op_ret, op_errno, inode, stbuf,
                             preparent, postparent, xdata);
        }
    } else {
        DHT_STACK_UNWIND(mknod, frame, op_ret, op_errno, inode, stbuf,
                         preparent, postparent, xdata);
    }

    return 0;
}

static int
dht_mknod_linkfile_create_cbk(call_frame_t *frame, void *cookie, xlator_t *this,
                              int32_t op_ret, int32_t op_errno, inode_t *inode,
                              struct iatt *stbuf, struct iatt *preparent,
                              struct iatt *postparent, dict_t *xdata)
{
    dht_local_t *local = NULL;
    xlator_t *cached_subvol = NULL;
    dht_conf_t *conf = NULL;

    local = frame->local;

    if (!local || !local->cached_subvol) {
        op_errno = EINVAL;
        goto err;
    }

    if (op_ret == -1) {
        local->op_errno = op_errno;
        goto err;
    }

    conf = this->private;
    if (!conf) {
        local->op_errno = EINVAL;
        op_errno = EINVAL;
        goto err;
    }

    cached_subvol = local->cached_subvol;

    if (local->params) {
        dict_del(local->params, conf->link_xattr_name);
        dict_del(local->params, GLUSTERFS_INTERNAL_FOP_KEY);
    }

    STACK_WIND_COOKIE(frame, dht_newfile_cbk, (void *)cached_subvol,
                      cached_subvol, cached_subvol->fops->mknod, &local->loc,
                      local->mode, local->rdev, local->umask, local->params);

    return 0;
err:
    if (local && local->lock[0].layout.parent_layout.locks) {
        local->refresh_layout_unlock(frame, this, -1, 1);
    } else {
        DHT_STACK_UNWIND(mknod, frame, -1, op_errno, NULL, NULL, NULL, NULL,
                         NULL);
    }
    return 0;
}

static int
dht_mknod_wind_to_avail_subvol(call_frame_t *frame, xlator_t *this,
                               xlator_t *subvol, loc_t *loc, dev_t rdev,
                               mode_t mode, mode_t umask, dict_t *params)
{
    dht_local_t *local = NULL;
    xlator_t *avail_subvol = NULL;

    local = frame->local;

    if (!dht_is_subvol_filled(this, subvol)) {
        gf_msg_debug(this->name, 0, "creating %s on %s", loc->path,
                     subvol->name);

        STACK_WIND_COOKIE(frame, dht_newfile_cbk, (void *)subvol, subvol,
                          subvol->fops->mknod, loc, mode, rdev, umask, params);
    } else {
        avail_subvol = dht_free_disk_available_subvol(this, subvol, local);

        if (avail_subvol != subvol) {
            local->params = dict_ref(params);
            local->rdev = rdev;
            local->mode = mode;
            local->umask = umask;
            local->cached_subvol = avail_subvol;
            local->hashed_subvol = subvol;

            gf_msg_debug(this->name, 0, "creating %s on %s (link at %s)",
                         loc->path, avail_subvol->name, subvol->name);

            dht_linkfile_create(frame, dht_mknod_linkfile_create_cbk, this,
                                avail_subvol, subvol, loc);

            goto out;
        }

        gf_msg_debug(this->name, 0, "creating %s on %s", loc->path,
                     subvol->name);

        STACK_WIND_COOKIE(frame, dht_newfile_cbk, (void *)subvol, subvol,
                          subvol->fops->mknod, loc, mode, rdev, umask, params);
    }
out:
    return 0;
}

static int32_t
dht_mknod_do(call_frame_t *frame)
{
    dht_local_t *local = NULL;
    dht_layout_t *refreshed = NULL;
    xlator_t *subvol = NULL;
    xlator_t *this = NULL;
    dht_conf_t *conf = NULL;
    dht_methods_t *methods = NULL;

    local = frame->local;

    this = THIS;

    conf = this->private;

    GF_VALIDATE_OR_GOTO(this->name, conf, err);

    methods = &(conf->methods);

    /* We don't need parent_loc anymore */
    loc_wipe(&local->loc);

    loc_copy(&local->loc, &local->loc2);

    loc_wipe(&local->loc2);

    refreshed = local->selfheal.refreshed_layout;

    subvol = methods->layout_search(this, refreshed, local->loc.name);

    if (!subvol) {
        gf_msg(this->name, GF_LOG_ERROR, 0, DHT_MSG_HASHED_SUBVOL_GET_FAILED,
               "no subvolume in "
               "layout for path=%s",
               local->loc.path);
        local->op_errno = ENOENT;
        goto err;
    }

    dht_mknod_wind_to_avail_subvol(frame, this, subvol, &local->loc,
                                   local->rdev, local->mode, local->umask,
                                   local->params);
    return 0;
err:
    local->refresh_layout_unlock(frame, this, -1, 1);

    return 0;
}

static int32_t
dht_mknod_unlock_cbk(call_frame_t *frame, void *cookie, xlator_t *this,
                     int32_t op_ret, int32_t op_errno, dict_t *xdata)
{
    DHT_STACK_DESTROY(frame);
    return 0;
}

static int32_t
dht_mknod_finish(call_frame_t *frame, xlator_t *this, int op_ret,
                 int invoke_cbk)
{
    dht_local_t *local = NULL, *lock_local = NULL;
    call_frame_t *lock_frame = NULL;
    int lock_count = 0;

    local = frame->local;
    lock_count = dht_lock_count(local->lock[0].layout.parent_layout.locks,
                                local->lock[0].layout.parent_layout.lk_count);
    if (lock_count == 0)
        goto done;

    lock_frame = copy_frame(frame);
    if (lock_frame == NULL) {
        goto done;
    }

    lock_local = dht_local_init(lock_frame, &local->loc, NULL,
                                lock_frame->root->op);
    if (lock_local == NULL) {
        goto done;
    }

    lock_local->lock[0]
        .layout.parent_layout.locks = local->lock[0].layout.parent_layout.locks;
    lock_local->lock[0].layout.parent_layout.lk_count =
        local->lock[0].layout.parent_layout.lk_count;

    local->lock[0].layout.parent_layout.locks = NULL;
    local->lock[0].layout.parent_layout.lk_count = 0;

    dht_unlock_inodelk(lock_frame,
                       lock_local->lock[0].layout.parent_layout.locks,
                       lock_local->lock[0].layout.parent_layout.lk_count,
                       dht_mknod_unlock_cbk);
    lock_frame = NULL;

done:
    if (lock_frame != NULL) {
        DHT_STACK_DESTROY(lock_frame);
    }

    if (op_ret == 0)
        return 0;

    DHT_STACK_UNWIND(mknod, frame, op_ret, local->op_errno, NULL, NULL, NULL,
                     NULL, NULL);
    return 0;
}

static int32_t
dht_mknod_lock_cbk(call_frame_t *frame, void *cookie, xlator_t *this,
                   int32_t op_ret, int32_t op_errno, dict_t *xdata)
{
    dht_local_t *local = NULL;

    local = frame->local;

    if (!local) {
        goto err;
    }

    if (op_ret < 0) {
        gf_msg("DHT", GF_LOG_ERROR, 0, DHT_MSG_INODE_LK_ERROR,
               "mknod lock failed for file: %s", local->loc2.name);

        local->op_errno = op_errno;

        goto err;
    }

    local->refresh_layout_unlock = dht_mknod_finish;

    local->refresh_layout_done = dht_mknod_do;

    dht_refresh_layout(frame);

    return 0;
err:
    if (local)
        dht_mknod_finish(frame, this, -1, 0);
    else
        DHT_STACK_UNWIND(mknod, frame, -1, EINVAL, NULL, NULL, NULL, NULL,
                         NULL);
    return 0;
}

static int32_t
dht_mknod_lock(call_frame_t *frame, xlator_t *subvol)
{
    dht_local_t *local = NULL;
    int count = 1, ret = -1;
    dht_lock_t **lk_array = NULL;

    GF_VALIDATE_OR_GOTO("dht", frame, err);
    GF_VALIDATE_OR_GOTO(frame->this->name, frame->local, err);

    local = frame->local;

    lk_array = GF_CALLOC(count, sizeof(*lk_array), gf_common_mt_pointer);

    if (lk_array == NULL)
        goto err;

    lk_array[0] = dht_lock_new(frame->this, subvol, &local->loc, F_RDLCK,
                               DHT_LAYOUT_HEAL_DOMAIN, NULL,
                               IGNORE_ENOENT_ESTALE);

    if (lk_array[0] == NULL)
        goto err;

    local->lock[0].layout.parent_layout.locks = lk_array;
    local->lock[0].layout.parent_layout.lk_count = count;

    ret = dht_blocking_inodelk(frame, lk_array, count, dht_mknod_lock_cbk);

    if (ret < 0) {
        local->lock[0].layout.parent_layout.locks = NULL;
        local->lock[0].layout.parent_layout.lk_count = 0;
        goto err;
    }

    return 0;
err:
    if (lk_array != NULL) {
        dht_lock_array_free(lk_array, count);
        GF_FREE(lk_array);
    }

    return -1;
}

static int
dht_refresh_parent_layout_resume(call_frame_t *frame, xlator_t *this, int ret,
                                 int invoke_cbk)
{
    dht_local_t *local = NULL, *parent_local = NULL;
    call_stub_t *stub = NULL;
    call_frame_t *parent_frame = NULL;

    local = frame->local;

    stub = local->stub;
    local->stub = NULL;

    parent_frame = stub->frame;
    parent_local = parent_frame->local;

    if (ret < 0) {
        parent_local->op_ret = -1;
        parent_local->op_errno = local->op_errno ? local->op_errno : EIO;
    } else {
        parent_local->op_ret = 0;
    }

    call_resume(stub);

    DHT_STACK_DESTROY(frame);

    return 0;
}

static int
dht_refresh_parent_layout_done(call_frame_t *frame)
{
    dht_local_t *local = NULL;
    int ret = 0;

    local = frame->local;

    if (local->op_ret < 0) {
        ret = -1;
        goto resume;
    }

    dht_layout_set(frame->this, local->loc.inode,
                   local->selfheal.refreshed_layout);

resume:
    dht_refresh_parent_layout_resume(frame, frame->this, ret, 1);
    return 0;
}

static int
dht_handle_parent_layout_change(xlator_t *this, call_stub_t *stub)
{
    call_frame_t *refresh_frame = NULL, *frame = NULL;
    dht_local_t *refresh_local = NULL, *local = NULL;

    frame = stub->frame;
    local = frame->local;

    refresh_frame = copy_frame(frame);
    if (!refresh_frame) {
        gf_msg(this->name, GF_LOG_ERROR, ENOMEM, DHT_MSG_NO_MEMORY,
               "mem allocation failed for refresh_frame");
        return -1;
    }

    refresh_local = dht_local_init(refresh_frame, NULL, NULL, stub->fop);
    if (!refresh_local) {
        gf_msg(this->name, GF_LOG_ERROR, ENOMEM, DHT_MSG_NO_MEMORY,
               "mem allocation failed for refresh_local");
        return -1;
    }

    refresh_local->loc.inode = inode_ref(local->loc.parent);
    gf_uuid_copy(refresh_local->loc.gfid, local->loc.parent->gfid);

    refresh_local->stub = stub;

    refresh_local->refresh_layout_unlock = dht_refresh_parent_layout_resume;
    refresh_local->refresh_layout_done = dht_refresh_parent_layout_done;

    dht_refresh_layout(refresh_frame);
    return 0;
}

static int32_t
dht_call_mkdir_stub(call_frame_t *frame, void *cookie, xlator_t *this,
                    int32_t op_ret, int32_t op_errno, dict_t *xdata)
{
    dht_local_t *local = NULL;
    call_stub_t *stub = NULL;

    local = frame->local;
    stub = local->stub;
    local->stub = NULL;

    if (op_ret < 0) {
        local->op_ret = -1;
        local->op_errno = op_errno;
    } else {
        local->op_ret = 0;
    }

    call_resume(stub);

    return 0;
}

static int32_t
dht_guard_parent_layout_and_namespace(xlator_t *subvol, call_stub_t *stub)
{
    dht_local_t *local = NULL;
    int ret = -1;
    loc_t *loc = NULL;
    xlator_t *hashed_subvol = NULL, *this = NULL;
    ;
    call_frame_t *frame = NULL;
    char pgfid[GF_UUID_BUF_SIZE] = {0};
    int32_t *parent_disk_layout = NULL;
    dht_layout_t *parent_layout = NULL;
    dht_conf_t *conf = NULL;

    GF_VALIDATE_OR_GOTO("dht", stub, err);

    frame = stub->frame;
    this = frame->this;

    conf = this->private;

    local = frame->local;

    local->stub = stub;

    /* TODO: recheck whether we should lock on src or dst if we do similar
     * stale layout checks for rename.
     */
    loc = &stub->args.loc;

    gf_uuid_unparse(loc->parent->gfid, pgfid);

    if (local->params == NULL) {
        local->params = dict_new();
        if (local->params == NULL) {
            local->op_errno = ENOMEM;
            gf_msg(this->name, GF_LOG_WARNING, local->op_errno,
                   DHT_MSG_PARENT_LAYOUT_CHANGED,
                   "%s (%s/%s) (path: %s): "
                   "dict allocation failed",
                   gf_fop_list[stub->fop], pgfid, loc->name, loc->path);
            goto err;
        }
    }

    hashed_subvol = dht_subvol_get_hashed(this, loc);
    if (hashed_subvol == NULL) {
        local->op_errno = EINVAL;

        gf_msg(this->name, GF_LOG_WARNING, local->op_errno,
               DHT_MSG_PARENT_LAYOUT_CHANGED,
               "%s (%s/%s) (path: %s): "
               "hashed subvolume not found",
               gf_fop_list[stub->fop], pgfid, loc->name, loc->path);
        goto err;
    }

    parent_layout = dht_layout_get(this, loc->parent);

    ret = dht_disk_layout_extract_for_subvol(this, parent_layout, hashed_subvol,
                                             &parent_disk_layout);
    if (ret == -1) {
        local->op_errno = EINVAL;
        gf_msg(this->name, GF_LOG_WARNING, local->op_errno,
               DHT_MSG_PARENT_LAYOUT_CHANGED,
               "%s (%s/%s) (path: %s): "
               "extracting in-memory layout of parent failed. ",
               gf_fop_list[stub->fop], pgfid, loc->name, loc->path);
        goto err;
    }

    memcpy((void *)local->parent_disk_layout, (void *)parent_disk_layout,
           sizeof(local->parent_disk_layout));

    dht_layout_unref(this, parent_layout);
    parent_layout = NULL;

    ret = dict_set_str(local->params, GF_PREOP_PARENT_KEY, conf->xattr_name);
    if (ret < 0) {
        local->op_errno = -ret;
        gf_msg(this->name, GF_LOG_WARNING, local->op_errno,
               DHT_MSG_PARENT_LAYOUT_CHANGED,
               "%s (%s/%s) (path: %s): "
               "setting %s key in params dictionary failed. ",
               gf_fop_list[stub->fop], pgfid, loc->name, loc->path,
               GF_PREOP_PARENT_KEY);
        goto err;
    }

    ret = dict_set_bin(local->params, conf->xattr_name, parent_disk_layout,
                       4 * 4);
    if (ret < 0) {
        local->op_errno = -ret;
        gf_msg(this->name, GF_LOG_WARNING, local->op_errno,
               DHT_MSG_PARENT_LAYOUT_CHANGED,
               "%s (%s/%s) (path: %s): "
               "setting parent-layout in params dictionary failed. ",
               gf_fop_list[stub->fop], pgfid, loc->name, loc->path);
        goto err;
    }

    parent_disk_layout = NULL;
    local->hashed_subvol = hashed_subvol;

    local->current = &local->lock[0];
    ret = dht_protect_namespace(frame, loc, hashed_subvol, &local->current->ns,
                                dht_call_mkdir_stub);
    if (ret < 0)
        goto err;

    return 0;
err:

    if (parent_disk_layout != NULL)
        GF_FREE(parent_disk_layout);

    if (parent_layout != NULL)
        dht_layout_unref(this, parent_layout);

    return -1;
}

int
dht_mknod(call_frame_t *frame, xlator_t *this, loc_t *loc, mode_t mode,
          dev_t rdev, mode_t umask, dict_t *params)
{
    xlator_t *subvol = NULL;
    int op_errno = -1;
    int i = 0;
    int ret = 0;
    dht_local_t *local = NULL;
    dht_conf_t *conf = NULL;

    VALIDATE_OR_GOTO(frame, err);
    VALIDATE_OR_GOTO(this, err);
    VALIDATE_OR_GOTO(loc, err);

    conf = this->private;

    dht_get_du_info(frame, this, loc);

    local = dht_local_init(frame, loc, NULL, GF_FOP_MKNOD);
    if (!local) {
        op_errno = ENOMEM;
        goto err;
    }

    subvol = dht_subvol_get_hashed(this, loc);
    if (!subvol) {
        gf_msg_debug(this->name, 0, "no subvolume in layout for path=%s",
                     loc->path);
        op_errno = EIO;
        goto err;
    }

    /* Post remove-brick, the client layout may not be in sync with
     * disk layout because of lack of lookup. Hence,a mknod call
     * may fall on the decommissioned brick.  Hence, if the
     * hashed_subvol is part of decommissioned bricks  list, do a
     * lookup on parent dir. If a fix-layout is already done by the
     * remove-brick process, the parent directory layout will be in
     * sync with that of the disk. If fix-layout is still ending
     * on the parent directory, we can let the file get created on
     * the decommissioned brick which will be eventually migrated to
     * non-decommissioned brick based on the new layout.
     */

    if (conf->decommission_subvols_cnt) {
        for (i = 0; i < conf->subvolume_cnt; i++) {
            if (conf->decommissioned_bricks[i] &&
                conf->decommissioned_bricks[i] == subvol) {
                gf_msg_debug(this->name, 0,
                             "hashed subvol:%s is "
                             "part of decommission brick list for "
                             "file: %s",
                             subvol->name, loc->path);

                /* dht_refresh_layout needs directory info in
                 * local->loc. Hence, storing the parent_loc in
                 * local->loc and storing the create context in
                 * local->loc2. We will restore this information
                 * in dht_creation do */

                ret = loc_copy(&local->loc2, &local->loc);
                if (ret) {
                    gf_msg(this->name, GF_LOG_ERROR, ENOMEM, DHT_MSG_NO_MEMORY,
                           "loc_copy failed %s", loc->path);

                    goto err;
                }

                local->params = dict_ref(params);
                local->rdev = rdev;
                local->mode = mode;
                local->umask = umask;

                loc_wipe(&local->loc);

                ret = dht_build_parent_loc(this, &local->loc, loc, &op_errno);

                if (ret) {
                    gf_msg(this->name, GF_LOG_ERROR, ENOMEM, DHT_MSG_LOC_FAILED,
                           "parent loc build failed");
                    goto err;
                }

                ret = dht_mknod_lock(frame, subvol);

                if (ret < 0) {
                    gf_msg(this->name, GF_LOG_ERROR, 0, DHT_MSG_INODE_LK_ERROR,
                           "locking parent failed");
                    goto err;
                }

                goto done;
            }
        }
    }

    dht_mknod_wind_to_avail_subvol(frame, this, subvol, loc, rdev, mode, umask,
                                   params);

done:
    return 0;

err:
    op_errno = (op_errno == -1) ? errno : op_errno;
    DHT_STACK_UNWIND(mknod, frame, -1, op_errno, NULL, NULL, NULL, NULL, NULL);

    return 0;
}

int
dht_symlink(call_frame_t *frame, xlator_t *this, const char *linkname,
            loc_t *loc, mode_t umask, dict_t *params)
{
    xlator_t *subvol = NULL;
    int op_errno = -1;
    dht_local_t *local = NULL;

    VALIDATE_OR_GOTO(frame, err);
    VALIDATE_OR_GOTO(this, err);
    VALIDATE_OR_GOTO(loc, err);

    local = dht_local_init(frame, loc, NULL, GF_FOP_SYMLINK);
    if (!local) {
        op_errno = ENOMEM;
        goto err;
    }

    subvol = dht_subvol_get_hashed(this, loc);
    if (!subvol) {
        gf_msg_debug(this->name, 0, "no subvolume in layout for path=%s",
                     loc->path);
        op_errno = EIO;
        goto err;
    }

    gf_msg_trace(this->name, 0, "creating %s on %s", loc->path, subvol->name);

    STACK_WIND_COOKIE(frame, dht_newfile_cbk, (void *)subvol, subvol,
                      subvol->fops->symlink, linkname, loc, umask, params);

    return 0;

err:
    op_errno = (op_errno == -1) ? errno : op_errno;
    DHT_STACK_UNWIND(link, frame, -1, op_errno, NULL, NULL, NULL, NULL, NULL);

    return 0;
}

int
dht_unlink(call_frame_t *frame, xlator_t *this, loc_t *loc, int xflag,
           dict_t *xdata)
{
    xlator_t *cached_subvol = NULL;
    int op_errno = -1;
    dht_local_t *local = NULL;

    VALIDATE_OR_GOTO(frame, err);
    VALIDATE_OR_GOTO(this, err);
    VALIDATE_OR_GOTO(loc, err);

    local = dht_local_init(frame, loc, NULL, GF_FOP_UNLINK);
    if (!local) {
        op_errno = ENOMEM;

        goto err;
    }

    cached_subvol = local->cached_subvol;
    if (!cached_subvol) {
        gf_msg_debug(this->name, 0, "no cached subvolume for path=%s",
                     loc->path);
        op_errno = EINVAL;
        goto err;
    }

    local->flags = xflag;
    STACK_WIND_COOKIE(frame, dht_unlink_cbk, cached_subvol, cached_subvol,
                      cached_subvol->fops->unlink, loc, xflag, xdata);

    return 0;
err:
    op_errno = (op_errno == -1) ? errno : op_errno;
    DHT_STACK_UNWIND(unlink, frame, -1, op_errno, NULL, NULL, NULL);

    return 0;
}

static int
dht_remove_stale_linkto_cbk(int ret, call_frame_t *sync_frame, void *data)
{
    DHT_STACK_DESTROY(sync_frame);
    return 0;
}

static int
dht_remove_stale_linkto(void *data)
{
    call_frame_t *frame = NULL;
    dht_local_t *local = NULL;
    xlator_t *this = NULL;
    dict_t *xdata_in = NULL;
    int ret = 0;

    GF_VALIDATE_OR_GOTO("dht", data, out);

    frame = data;
    local = frame->local;
    this = frame->this;
    GF_VALIDATE_OR_GOTO("dht", this, out);
    GF_VALIDATE_OR_GOTO("dht", local, out);
    GF_VALIDATE_OR_GOTO("dht", local->link_subvol, out);

    xdata_in = dict_new();
    if (!xdata_in)
        goto out;

    ret = dht_fill_dict_to_avoid_unlink_of_migrating_file(xdata_in);
    if (ret) {
        gf_msg(this->name, GF_LOG_WARNING, -ret, 0,
               "Failed to set keys for stale linkto"
               "deletion on path %s",
               local->loc.path);
        goto out;
    }

    ret = syncop_unlink(local->link_subvol, &local->loc, xdata_in, NULL);
    if (ret) {
        gf_msg(this->name, GF_LOG_WARNING, -ret, 0,
               "Removal of linkto failed"
               " on path %s at subvol %s",
               local->loc.path, local->link_subvol->name);
    }
out:
    if (xdata_in)
        dict_unref(xdata_in);
    return ret;
}

static int
dht_link_cbk(call_frame_t *frame, void *cookie, xlator_t *this, int op_ret,
             int op_errno, inode_t *inode, struct iatt *stbuf,
             struct iatt *preparent, struct iatt *postparent, dict_t *xdata)
{
    dht_local_t *local = NULL;
    int ret = -1;
    gf_boolean_t stbuf_merged = _gf_false;
    xlator_t *subvol = NULL;
    call_frame_t *cleanup_frame = NULL;
    dht_local_t *cleanup_local = NULL;

    local = frame->local;

    if (op_ret == -1) {
        /* Remove the linkto if exists */
        if (local->linked) {
            cleanup_frame = create_frame(this, this->ctx->pool);
            if (cleanup_frame) {
                cleanup_local = dht_local_init(cleanup_frame, &local->loc2,
                                               NULL, 0);
                if (!cleanup_local || !local->link_subvol) {
                    DHT_STACK_DESTROY(cleanup_frame);
                    goto out;
                }
                cleanup_local->link_subvol = local->link_subvol;
                FRAME_SU_DO(cleanup_frame, dht_local_t);
                ret = synctask_new(this->ctx->env, dht_remove_stale_linkto,
                                   dht_remove_stale_linkto_cbk, cleanup_frame,
                                   cleanup_frame);
            }
        }
        /* No continuation on DHT inode missing errors, as we should
         * then have a good stbuf that states P2 happened. We would
         * get inode missing if, the file completed migrated between
         * the lookup and the link call */
        goto out;
    }

    /* Update parent on success, even if P1/2 checks are positive.
     * The second call on success will further update the parent */
    if (local->loc.parent) {
        dht_inode_ctx_time_update(local->loc.parent, this, preparent, 0);
        dht_inode_ctx_time_update(local->loc.parent, this, postparent, 1);
    }

    /* Update linkto attrs, if this is the first call and non-P2,
     * if we detect P2 then we need to trust the attrs from the
     * second call, not the first */
    if (local->linked == _gf_true &&
        ((local->call_cnt == 1 && !IS_DHT_MIGRATION_PHASE2(stbuf)) ||
         (local->call_cnt != 1 && IS_DHT_MIGRATION_PHASE2(&local->stbuf)))) {
        dht_iatt_merge(this, &local->stbuf, stbuf);
        stbuf_merged = _gf_true;
        dht_linkfile_attr_heal(frame, this);
    }

    /* No further P1/2 checks if we are in the second iteration of
     * the call */
    if (local->call_cnt != 1) {
        goto out;
    } else {
        /* Preserve the return values, in case the migration decides
         * to recreate the link on the same subvol that the current
         * hased for the link was created on. */
        dht_iatt_merge(this, &local->preparent, preparent);
        dht_iatt_merge(this, &local->postparent, postparent);
        if (!stbuf_merged) {
            dht_iatt_merge(this, &local->stbuf, stbuf);
            stbuf_merged = _gf_true;
        }

        local->inode = inode_ref(inode);
    }

    local->op_ret = op_ret;
    local->op_errno = op_errno;
    local->rebalance.target_op_fn = dht_link2;
    dht_set_local_rebalance(this, local, stbuf, preparent, postparent, xdata);

    /* Check if the rebalance phase2 is true */
    if (IS_DHT_MIGRATION_PHASE2(stbuf)) {
        ret = dht_inode_ctx_get_mig_info(this, local->loc.inode, NULL, &subvol);
        if (!subvol) {
            /* Phase 2 of migration */
            ret = dht_rebalance_complete_check(this, frame);
            if (!ret)
                return 0;
        } else {
            dht_link2(this, subvol, frame, 0);
            return 0;
        }
    }

    /* Check if the rebalance phase1 is true */
    if (IS_DHT_MIGRATION_PHASE1(stbuf)) {
        ret = dht_inode_ctx_get_mig_info(this, local->loc.inode, NULL, &subvol);
        if (subvol) {
            dht_link2(this, subvol, frame, 0);
            return 0;
        }
        ret = dht_rebalance_in_progress_check(this, frame);
        if (!ret)
            return 0;
    }
out:
    DHT_STRIP_PHASE1_FLAGS(stbuf);

    dht_set_fixed_dir_stat(preparent);
    dht_set_fixed_dir_stat(postparent);
    DHT_STACK_UNWIND(link, frame, op_ret, op_errno, inode, stbuf, preparent,
                     postparent, NULL);

    return 0;
}

static int
dht_link2(xlator_t *this, xlator_t *subvol, call_frame_t *frame, int ret)
{
    dht_local_t *local = NULL;
    int op_errno = EINVAL;

    local = frame->local;
    if (!local)
        goto err;

    op_errno = local->op_errno;

    if (we_are_not_migrating(ret)) {
        /* This dht xlator is not migrating the file. Unwind and
         * pass on the original mode bits so the higher DHT layer
         * can handle this.
         */
        dht_set_fixed_dir_stat(&local->preparent);
        dht_set_fixed_dir_stat(&local->postparent);

        DHT_STACK_UNWIND(link, frame, local->op_ret, op_errno, local->inode,
                         &local->stbuf, &local->preparent, &local->postparent,
                         NULL);
        return 0;
    }

    if (subvol == NULL) {
        op_errno = EINVAL;
        goto err;
    }

    /* Second call to create link file could result in EEXIST as the
     * first call created the linkto in the currently
     * migrating subvol, which could be the new hashed subvol */
    if (local->link_subvol == subvol) {
        DHT_STRIP_PHASE1_FLAGS(&local->stbuf);
        dht_set_fixed_dir_stat(&local->preparent);
        dht_set_fixed_dir_stat(&local->postparent);
        DHT_STACK_UNWIND(link, frame, 0, 0, local->inode, &local->stbuf,
                         &local->preparent, &local->postparent, NULL);

        return 0;
    }

    local->call_cnt = 2;

    STACK_WIND(frame, dht_link_cbk, subvol, subvol->fops->link, &local->loc,
               &local->loc2, local->xattr_req);

    return 0;
err:
    DHT_STACK_UNWIND(link, frame, -1, op_errno, NULL, NULL, NULL, NULL, NULL);

    return 0;
}

static int
dht_link_linkfile_cbk(call_frame_t *frame, void *cookie, xlator_t *this,
                      int op_ret, int op_errno, inode_t *inode,
                      struct iatt *stbuf, struct iatt *preparent,
                      struct iatt *postparent, dict_t *xdata)
{
    dht_local_t *local = NULL;
    xlator_t *srcvol = NULL;

    if (op_ret == -1)
        goto err;

    local = frame->local;
    srcvol = local->linkfile.srcvol;

    STACK_WIND(frame, dht_link_cbk, srcvol, srcvol->fops->link, &local->loc,
               &local->loc2, local->xattr_req);

    return 0;

err:
    DHT_STRIP_PHASE1_FLAGS(stbuf);
    dht_set_fixed_dir_stat(preparent);
    dht_set_fixed_dir_stat(postparent);
    DHT_STACK_UNWIND(link, frame, op_ret, op_errno, inode, stbuf, preparent,
                     postparent, xdata);

    return 0;
}

int
dht_link(call_frame_t *frame, xlator_t *this, loc_t *oldloc, loc_t *newloc,
         dict_t *xdata)
{
    xlator_t *cached_subvol = NULL;
    xlator_t *hashed_subvol = NULL;
    int op_errno = -1;
    int ret = -1;
    dht_local_t *local = NULL;

    VALIDATE_OR_GOTO(frame, err);
    VALIDATE_OR_GOTO(this, err);
    VALIDATE_OR_GOTO(oldloc, err);
    VALIDATE_OR_GOTO(newloc, err);

    local = dht_local_init(frame, oldloc, NULL, GF_FOP_LINK);
    if (!local) {
        op_errno = ENOMEM;

        goto err;
    }
    local->call_cnt = 1;

    cached_subvol = local->cached_subvol;
    if (!cached_subvol) {
        gf_msg_debug(this->name, 0, "no cached subvolume for path=%s",
                     oldloc->path);
        op_errno = ENOENT;
        goto err;
    }

    hashed_subvol = dht_subvol_get_hashed(this, newloc);
    if (!hashed_subvol) {
        gf_msg_debug(this->name, 0, "no subvolume in layout for path=%s",
                     newloc->path);
        op_errno = EIO;
        goto err;
    }

    ret = loc_copy(&local->loc2, newloc);
    if (ret == -1) {
        op_errno = ENOMEM;
        goto err;
    }
    if (xdata)
        local->xattr_req = dict_ref(xdata);

    if (hashed_subvol != cached_subvol) {
        gf_uuid_copy(local->gfid, oldloc->inode->gfid);
        dht_linkfile_create(frame, dht_link_linkfile_cbk, this, cached_subvol,
                            hashed_subvol, newloc);
    } else {
        STACK_WIND(frame, dht_link_cbk, cached_subvol,
                   cached_subvol->fops->link, oldloc, newloc, xdata);
    }

    return 0;

err:
    op_errno = (op_errno == -1) ? errno : op_errno;
    DHT_STACK_UNWIND(link, frame, -1, op_errno, NULL, NULL, NULL, NULL, NULL);

    return 0;
}

int
dht_create_cbk(call_frame_t *frame, void *cookie, xlator_t *this, int op_ret,
               int op_errno, fd_t *fd, inode_t *inode, struct iatt *stbuf,
               struct iatt *preparent, struct iatt *postparent, dict_t *xdata)
{
    xlator_t *prev = NULL;
    int ret = -1;
    dht_local_t *local = NULL;
    gf_boolean_t parent_layout_changed = _gf_false;
    char pgfid[GF_UUID_BUF_SIZE] = {0};
    xlator_t *subvol = NULL;

    local = frame->local;

    local = frame->local;
    if (!local) {
        op_ret = -1;
        op_errno = EINVAL;
        goto out;
    }

    if (op_ret == -1) {
        local->op_errno = op_errno;
        parent_layout_changed = (xdata &&
                                 dict_get(xdata, GF_PREOP_CHECK_FAILED))
                                    ? _gf_true
                                    : _gf_false;

        if (parent_layout_changed) {
            if (local && local->lock[0].layout.parent_layout.locks) {
                /* Returning failure as the layout could not be fixed even under
                 * the lock */
                goto out;
            }

            gf_uuid_unparse(local->loc.parent->gfid, pgfid);
            gf_msg(this->name, GF_LOG_INFO, 0, DHT_MSG_PARENT_LAYOUT_CHANGED,
                   "create (%s/%s) (path: %s): parent layout "
                   "changed. Attempting a layout refresh and then a "
                   "retry",
                   pgfid, local->loc.name, local->loc.path);

            /*
              dht_refresh_layout needs directory info in local->loc.Hence,
              storing the parent_loc in local->loc and storing the create
              context in local->loc2. We will restore this information in
              dht_creation_do.
             */

            loc_wipe(&local->loc2);

            ret = loc_copy(&local->loc2, &local->loc);
            if (ret) {
                gf_msg(this->name, GF_LOG_ERROR, ENOMEM, DHT_MSG_NO_MEMORY,
                       "loc_copy failed %s", local->loc.path);

                goto out;
            }

            loc_wipe(&local->loc);

            ret = dht_build_parent_loc(this, &local->loc, &local->loc2,
                                       &op_errno);

            if (ret) {
                gf_msg(this->name, GF_LOG_ERROR, ENOMEM, DHT_MSG_LOC_FAILED,
                       "parent loc build failed");
                goto out;
            }

            subvol = dht_subvol_get_hashed(this, &local->loc2);

            ret = dht_create_lock(frame, subvol);
            if (ret < 0) {
                gf_msg(this->name, GF_LOG_ERROR, 0, DHT_MSG_INODE_LK_ERROR,
                       "locking parent failed");
                goto out;
            }

            return 0;
        }

        goto out;
    }

    prev = cookie;

    if (local->loc.parent) {
        dht_inode_ctx_time_update(local->loc.parent, this, preparent, 0);

        dht_inode_ctx_time_update(local->loc.parent, this, postparent, 1);
    }

    ret = dht_fd_ctx_set(this, fd, prev);
    if (ret != 0) {
        gf_msg_debug(this->name, 0,
                     "Possible fd leak. "
                     "Could not set fd ctx for subvol %s",
                     prev->name);
    }

    ret = dht_layout_preset(this, prev, inode);
    if (ret != 0) {
        gf_msg_debug(this->name, 0, "could not set preset layout for subvol %s",
                     prev->name);
        op_ret = -1;
        op_errno = EINVAL;
        goto out;
    }

    local->op_errno = op_errno;

    if (local->linked == _gf_true) {
        local->stbuf = *stbuf;
        dht_linkfile_attr_heal(frame, this);
    }
out:

    DHT_STRIP_PHASE1_FLAGS(stbuf);
    dht_set_fixed_dir_stat(preparent);
    dht_set_fixed_dir_stat(postparent);

    if (local && local->lock[0].layout.parent_layout.locks) {
        /* store op_errno for failure case*/
        local->op_errno = op_errno;
        local->refresh_layout_unlock(frame, this, op_ret, 1);

        if (op_ret == 0) {
            DHT_STACK_UNWIND(create, frame, op_ret, op_errno, fd, inode, stbuf,
                             preparent, postparent, xdata);
        }
    } else {
        DHT_STACK_UNWIND(create, frame, op_ret, op_errno, fd, inode, stbuf,
                         preparent, postparent, xdata);
    }
    return 0;
}

static int
dht_create_linkfile_create_cbk(call_frame_t *frame, void *cookie,
                               xlator_t *this, int32_t op_ret, int32_t op_errno,
                               inode_t *inode, struct iatt *stbuf,
                               struct iatt *preparent, struct iatt *postparent,
                               dict_t *xdata)
{
    dht_local_t *local = NULL;
    xlator_t *cached_subvol = NULL;
    dht_conf_t *conf = NULL;

    local = frame->local;
    if (!local) {
        op_errno = EINVAL;
        goto err;
    }

    if (op_ret == -1) {
        local->op_errno = op_errno;
        goto err;
    }

    conf = this->private;
    if (!conf) {
        local->op_errno = EINVAL;
        op_errno = EINVAL;
        goto err;
    }

    cached_subvol = local->cached_subvol;

    if (local->params) {
        dict_del(local->params, conf->link_xattr_name);
        dict_del(local->params, GLUSTERFS_INTERNAL_FOP_KEY);
    }

    STACK_WIND_COOKIE(frame, dht_create_cbk, cached_subvol, cached_subvol,
                      cached_subvol->fops->create, &local->loc, local->flags,
                      local->mode, local->umask, local->fd, local->params);

    return 0;
err:
    if (local && local->lock[0].layout.parent_layout.locks) {
        local->refresh_layout_unlock(frame, this, -1, 1);
    } else {
        DHT_STACK_UNWIND(create, frame, -1, op_errno, NULL, NULL, NULL, NULL,
                         NULL, NULL);
    }
    return 0;
}

static int
dht_create_wind_to_avail_subvol(call_frame_t *frame, xlator_t *this,
                                xlator_t *subvol, loc_t *loc, int32_t flags,
                                mode_t mode, mode_t umask, fd_t *fd,
                                dict_t *params)
{
    dht_local_t *local = NULL;
    xlator_t *avail_subvol = NULL;

    local = frame->local;

    if (!dht_is_subvol_filled(this, subvol)) {
        gf_msg_debug(this->name, 0, "creating %s on %s", loc->path,
                     subvol->name);

        dht_set_parent_layout_in_dict(loc, this, local);

        STACK_WIND_COOKIE(frame, dht_create_cbk, subvol, subvol,
                          subvol->fops->create, loc, flags, mode, umask, fd,
                          params);

    } else {
        avail_subvol = dht_free_disk_available_subvol(this, subvol, local);

        if (avail_subvol != subvol) {
            local->cached_subvol = avail_subvol;
            local->hashed_subvol = subvol;

            gf_msg_debug(this->name, 0, "creating %s on %s (link at %s)",
                         loc->path, avail_subvol->name, subvol->name);

            dht_linkfile_create(frame, dht_create_linkfile_create_cbk, this,
                                avail_subvol, subvol, loc);

            goto out;
        }

        gf_msg_debug(this->name, 0, "creating %s on %s", loc->path,
                     subvol->name);

        dht_set_parent_layout_in_dict(loc, this, local);

        STACK_WIND_COOKIE(frame, dht_create_cbk, subvol, subvol,
                          subvol->fops->create, loc, flags, mode, umask, fd,
                          params);
    }
out:
    return 0;
}

int
dht_build_parent_loc(xlator_t *this, loc_t *parent, loc_t *child,
                     int32_t *op_errno)
{
    inode_table_t *table = NULL;
    int ret = -1;

    if (!parent || !child) {
        if (op_errno)
            *op_errno = EINVAL;
        goto out;
    }

    if (child->parent) {
        parent->inode = inode_ref(child->parent);
        if (!parent->inode) {
            if (op_errno)
                *op_errno = EINVAL;
            goto out;
        }

        gf_uuid_copy(parent->gfid, child->pargfid);

        ret = 0;

        goto out;
    } else {
        if (gf_uuid_is_null(child->pargfid)) {
            if (op_errno)
                *op_errno = EINVAL;
            goto out;
        }

        table = this->itable;

        if (!table) {
            if (op_errno) {
                *op_errno = EINVAL;
                goto out;
            }
        }

        parent->inode = inode_find(table, child->pargfid);

        if (!parent->inode) {
            if (op_errno) {
                *op_errno = EINVAL;
                goto out;
            }
        }

        gf_uuid_copy(parent->gfid, child->pargfid);

        ret = 0;
    }

out:
    return ret;
}

static int32_t
dht_create_do(call_frame_t *frame)
{
    dht_local_t *local = NULL;
    dht_layout_t *refreshed = NULL;
    xlator_t *subvol = NULL;
    xlator_t *this = NULL;
    dht_conf_t *conf = NULL;
    dht_methods_t *methods = NULL;

    local = frame->local;

    this = THIS;

    conf = this->private;

    GF_VALIDATE_OR_GOTO(this->name, conf, err);

    methods = &(conf->methods);

    /* We don't need parent_loc anymore */
    loc_wipe(&local->loc);

    loc_copy(&local->loc, &local->loc2);

    loc_wipe(&local->loc2);

    refreshed = local->selfheal.refreshed_layout;

    subvol = methods->layout_search(this, refreshed, local->loc.name);

    if (!subvol) {
        gf_msg(this->name, GF_LOG_ERROR, 0, DHT_MSG_HASHED_SUBVOL_GET_FAILED,
               "no subvolume in "
               "layout for path=%s",
               local->loc.path);
        local->op_errno = ENOENT;
        goto err;
    }

    dht_create_wind_to_avail_subvol(frame, this, subvol, &local->loc,
                                    local->flags, local->mode, local->umask,
                                    local->fd, local->params);
    return 0;
err:
    local->refresh_layout_unlock(frame, this, -1, 1);

    return 0;
}

static int32_t
dht_create_unlock_cbk(call_frame_t *frame, void *cookie, xlator_t *this,
                      int32_t op_ret, int32_t op_errno, dict_t *xdata)
{
    DHT_STACK_DESTROY(frame);
    return 0;
}

static int32_t
dht_create_finish(call_frame_t *frame, xlator_t *this, int op_ret,
                  int invoke_cbk)
{
    dht_local_t *local = NULL, *lock_local = NULL;
    call_frame_t *lock_frame = NULL;
    int lock_count = 0;

    local = frame->local;
    lock_count = dht_lock_count(local->lock[0].layout.parent_layout.locks,
                                local->lock[0].layout.parent_layout.lk_count);
    if (lock_count == 0)
        goto done;

    lock_frame = copy_frame(frame);
    if (lock_frame == NULL) {
        goto done;
    }

    lock_local = dht_local_init(lock_frame, &local->loc, NULL,
                                lock_frame->root->op);
    if (lock_local == NULL) {
        goto done;
    }

    lock_local->lock[0]
        .layout.parent_layout.locks = local->lock[0].layout.parent_layout.locks;
    lock_local->lock[0].layout.parent_layout.lk_count =
        local->lock[0].layout.parent_layout.lk_count;

    local->lock[0].layout.parent_layout.locks = NULL;
    local->lock[0].layout.parent_layout.lk_count = 0;

    dht_unlock_inodelk(lock_frame,
                       lock_local->lock[0].layout.parent_layout.locks,
                       lock_local->lock[0].layout.parent_layout.lk_count,
                       dht_create_unlock_cbk);
    lock_frame = NULL;

done:
    if (lock_frame != NULL) {
        DHT_STACK_DESTROY(lock_frame);
    }

    if (op_ret == 0)
        return 0;

    DHT_STACK_UNWIND(create, frame, op_ret, local->op_errno, NULL, NULL, NULL,
                     NULL, NULL, NULL);
    return 0;
}

static int32_t
dht_create_lock_cbk(call_frame_t *frame, void *cookie, xlator_t *this,
                    int32_t op_ret, int32_t op_errno, dict_t *xdata)
{
    dht_local_t *local = NULL;

    local = frame->local;

    if (!local) {
        goto err;
    }

    if (op_ret < 0) {
        gf_msg("DHT", GF_LOG_ERROR, 0, DHT_MSG_INODE_LK_ERROR,
               "Create lock failed for file: %s", local->loc2.name);

        local->op_errno = op_errno;

        goto err;
    }

    local->refresh_layout_unlock = dht_create_finish;

    local->refresh_layout_done = dht_create_do;

    dht_refresh_layout(frame);

    return 0;
err:
    if (local)
        dht_create_finish(frame, this, -1, 0);
    else
        DHT_STACK_UNWIND(create, frame, -1, EINVAL, NULL, NULL, NULL, NULL,
                         NULL, NULL);
    return 0;
}

int32_t
dht_create_lock(call_frame_t *frame, xlator_t *subvol)
{
    dht_local_t *local = NULL;
    int count = 1, ret = -1;
    dht_lock_t **lk_array = NULL;

    GF_VALIDATE_OR_GOTO("dht", frame, err);
    GF_VALIDATE_OR_GOTO(frame->this->name, frame->local, err);

    local = frame->local;

    lk_array = GF_CALLOC(count, sizeof(*lk_array), gf_common_mt_pointer);

    if (lk_array == NULL)
        goto err;

    lk_array[0] = dht_lock_new(frame->this, subvol, &local->loc, F_RDLCK,
                               DHT_LAYOUT_HEAL_DOMAIN, NULL,
                               IGNORE_ENOENT_ESTALE);

    if (lk_array[0] == NULL)
        goto err;

    local->lock[0].layout.parent_layout.locks = lk_array;
    local->lock[0].layout.parent_layout.lk_count = count;

    ret = dht_blocking_inodelk(frame, lk_array, count, dht_create_lock_cbk);

    if (ret < 0) {
        local->lock[0].layout.parent_layout.locks = NULL;
        local->lock[0].layout.parent_layout.lk_count = 0;
        goto err;
    }

    return 0;
err:
    if (lk_array != NULL) {
        dht_lock_array_free(lk_array, count);
        GF_FREE(lk_array);
    }

    return -1;
}

int
dht_set_parent_layout_in_dict(loc_t *loc, xlator_t *this, dht_local_t *local)
{
    dht_conf_t *conf = this->private;
    dht_layout_t *parent_layout = NULL;
    int *parent_disk_layout = NULL;
    xlator_t *hashed_subvol = NULL;
    char pgfid[GF_UUID_BUF_SIZE] = {0};
    int ret = 0;

    gf_uuid_unparse(loc->parent->gfid, pgfid);

    parent_layout = dht_layout_get(this, loc->parent);
    hashed_subvol = dht_subvol_get_hashed(this, loc);

    ret = dht_disk_layout_extract_for_subvol(this, parent_layout, hashed_subvol,
                                             &parent_disk_layout);
    if (ret == -1) {
        gf_msg(this->name, GF_LOG_WARNING, local->op_errno,
               DHT_MSG_PARENT_LAYOUT_CHANGED,
               "%s (%s/%s) (path: %s): "
               "extracting in-memory layout of parent failed. ",
               gf_fop_list[local->fop], pgfid, loc->name, loc->path);
        goto err;
    }

    ret = dict_set_str_sizen(local->params, GF_PREOP_PARENT_KEY,
                             conf->xattr_name);
    if (ret < 0) {
        gf_msg(this->name, GF_LOG_WARNING, local->op_errno,
               DHT_MSG_PARENT_LAYOUT_CHANGED,
               "%s (%s/%s) (path: %s): "
               "setting %s key in params dictionary failed. ",
               gf_fop_list[local->fop], pgfid, loc->name, loc->path,
               GF_PREOP_PARENT_KEY);
        goto err;
    }

    ret = dict_set_bin(local->params, conf->xattr_name, parent_disk_layout,
                       4 * 4);
    if (ret < 0) {
        gf_msg(this->name, GF_LOG_WARNING, local->op_errno,
               DHT_MSG_PARENT_LAYOUT_CHANGED,
               "%s (%s/%s) (path: %s): "
               "setting parent-layout in params dictionary failed. ",
               gf_fop_list[local->fop], pgfid, loc->name, loc->path);
        goto err;
    }

err:
    dht_layout_unref(this, parent_layout);
    return ret;
}

int
dht_create(call_frame_t *frame, xlator_t *this, loc_t *loc, int32_t flags,
           mode_t mode, mode_t umask, fd_t *fd, dict_t *params)
{
    int op_errno = -1;
    xlator_t *subvol = NULL;
    xlator_t *hashed_subvol = NULL;
    dht_local_t *local = NULL;
    int i = 0;
    dht_conf_t *conf = NULL;
    int ret = 0;

    VALIDATE_OR_GOTO(frame, err);
    VALIDATE_OR_GOTO(this, err);
    VALIDATE_OR_GOTO(loc, err);

    conf = this->private;

    dht_get_du_info(frame, this, loc);

    local = dht_local_init(frame, loc, fd, GF_FOP_CREATE);
    if (!local) {
        op_errno = ENOMEM;
        goto err;
    }

    local->params = dict_ref(params);
    local->flags = flags;
    local->mode = mode;
    local->umask = umask;

    if (dht_filter_loc_subvol_key(this, loc, &local->loc, &subvol)) {
        gf_msg(this->name, GF_LOG_INFO, 0, DHT_MSG_SUBVOL_INFO,
               "creating %s on %s (got create on %s)", local->loc.path,
               subvol->name, loc->path);

        /* Since lookup-optimize is enabled by default, we need
         * to create the linkto file if required.
         * Note this does not check for decommisioned bricks
         * and min-free-disk limits as this is a debugging tool
         * and not expected to be used in production.
         */
        hashed_subvol = dht_subvol_get_hashed(this, &local->loc);

        if (hashed_subvol && (hashed_subvol != subvol)) {
            /* Create the linkto file and then the data file */
            local->cached_subvol = subvol;
            local->hashed_subvol = hashed_subvol;

            dht_linkfile_create(frame, dht_create_linkfile_create_cbk, this,
                                subvol, hashed_subvol, &local->loc);
            goto done;
        }
        /* We either don't have a hashed subvol or the hashed subvol is
         * the same as the one specified. No need to create the linkto
         * file as we expect a lookup everywhere if there are problems
         * with the parent layout
         */

        dht_set_parent_layout_in_dict(loc, this, local);

        STACK_WIND_COOKIE(frame, dht_create_cbk, subvol, subvol,
                          subvol->fops->create, &local->loc, flags, mode, umask,
                          fd, params);
        goto done;
    }

    subvol = dht_subvol_get_hashed(this, loc);
    if (!subvol) {
        gf_msg(this->name, GF_LOG_ERROR, 0, DHT_MSG_HASHED_SUBVOL_GET_FAILED,
               "no subvolume in layout for path=%s", loc->path);

        op_errno = EIO;
        goto err;
    }

    /* Post remove-brick, the client layout may not be in sync with
     * disk layout because of lack of lookup. Hence,a create call
     * may fall on the decommissioned brick.  Hence, if the
     * hashed_subvol is part of decommissioned bricks  list, do a
     * lookup on parent dir. If a fix-layout is already done by the
     * remove-brick process, the parent directory layout will be in
     * sync with that of the disk. If fix-layout is still ending
     * on the parent directory, we can let the file get created on
     * the decommissioned brick which will be eventually migrated to
     * non-decommissioned brick based on the new layout.
     */

    if (conf->decommission_subvols_cnt) {
        for (i = 0; i < conf->subvolume_cnt; i++) {
            if (conf->decommissioned_bricks[i] &&
                conf->decommissioned_bricks[i] == subvol) {
                gf_msg_debug(this->name, 0,
                             "hashed subvol:%s is "
                             "part of decommission brick list for "
                             "file: %s",
                             subvol->name, loc->path);

                /* dht_refresh_layout needs directory info in
                 * local->loc. Hence, storing the parent_loc in
                 * local->loc and storing the create context in
                 * local->loc2. We will restore this information
                 * in dht_creation do */

                ret = loc_copy(&local->loc2, &local->loc);
                if (ret) {
                    gf_msg(this->name, GF_LOG_ERROR, ENOMEM, DHT_MSG_NO_MEMORY,
                           "loc_copy failed %s", loc->path);

                    goto err;
                }

                loc_wipe(&local->loc);

                ret = dht_build_parent_loc(this, &local->loc, loc, &op_errno);

                if (ret) {
                    gf_msg(this->name, GF_LOG_ERROR, ENOMEM, DHT_MSG_LOC_FAILED,
                           "parent loc build failed");
                    goto err;
                }

                ret = dht_create_lock(frame, subvol);

                if (ret < 0) {
                    gf_msg(this->name, GF_LOG_ERROR, 0, DHT_MSG_INODE_LK_ERROR,
                           "locking parent failed");
                    goto err;
                }

                goto done;
            }
        }
    }

    dht_create_wind_to_avail_subvol(frame, this, subvol, loc, flags, mode,
                                    umask, fd, params);
done:
    return 0;

err:

    op_errno = (op_errno == -1) ? errno : op_errno;
    DHT_STACK_UNWIND(create, frame, -1, op_errno, NULL, NULL, NULL, NULL, NULL,
                     NULL);

    return 0;
}

static int
dht_mkdir_selfheal_cbk(call_frame_t *frame, void *cookie, xlator_t *this,
                       int32_t op_ret, int32_t op_errno, dict_t *xdata)
{
    dht_local_t *local = NULL;
    dht_layout_t *layout = NULL;

    local = frame->local;
    layout = local->selfheal.layout;

    FRAME_SU_UNDO(frame, dht_local_t);
    dht_set_fixed_dir_stat(&local->preparent);
    dht_set_fixed_dir_stat(&local->postparent);

    if (op_ret == 0) {
        dht_layout_set(this, local->inode, layout);

        dht_inode_ctx_time_update(local->inode, this, &local->stbuf, 1);
        if (local->loc.parent) {
            dht_inode_ctx_time_update(local->loc.parent, this,
                                      &local->preparent, 0);

            dht_inode_ctx_time_update(local->loc.parent, this,
                                      &local->postparent, 1);
        }
    }

    DHT_STACK_UNWIND(mkdir, frame, op_ret, op_errno, local->inode,
                     &local->stbuf, &local->preparent, &local->postparent,
                     NULL);

    return 0;
}

static int
dht_mkdir_cbk(call_frame_t *frame, void *cookie, xlator_t *this, int op_ret,
              int op_errno, inode_t *inode, struct iatt *stbuf,
              struct iatt *preparent, struct iatt *postparent, dict_t *xdata)
{
    dht_local_t *local = NULL;
    int this_call_cnt = 0;
    int ret = -1;
    gf_boolean_t subvol_filled = _gf_false;
    gf_boolean_t dir_exists = _gf_false;
    xlator_t *prev = NULL;
    dht_layout_t *layout = NULL;

    local = frame->local;
    prev = cookie;
    layout = local->layout;

    subvol_filled = dht_is_subvol_filled(this, prev);

    LOCK(&frame->lock);
    {
        if (subvol_filled && (op_ret != -1)) {
            ret = dht_layout_merge(this, layout, prev, -1, ENOSPC, NULL);
        } else {
            if (op_ret == -1 && op_errno == EEXIST) {
                /* Very likely just a race between mkdir and
                   self-heal (from lookup of a concurrent mkdir
                   attempt).
                   Ignore error for now. layout setting will
                   anyways fail if this was a different (old)
                   pre-existing different directory.
                */
                op_ret = 0;
                dir_exists = _gf_true;
            }
            ret = dht_layout_merge(this, layout, prev, op_ret, op_errno, NULL);
        }
        if (ret)
            gf_msg(this->name, GF_LOG_WARNING, 0, DHT_MSG_LAYOUT_MERGE_FAILED,
                   "%s: failed to merge layouts for subvol %s", local->loc.path,
                   prev->name);

        if (op_ret == -1) {
            local->op_errno = op_errno;
            goto unlock;
        }

        if (dir_exists)
            goto unlock;

        dht_iatt_merge(this, &local->stbuf, stbuf);
        dht_iatt_merge(this, &local->preparent, preparent);
        dht_iatt_merge(this, &local->postparent, postparent);
    }
unlock:
    UNLOCK(&frame->lock);

    this_call_cnt = dht_frame_return(frame);
    if (is_last_call(this_call_cnt)) {
        /*Unlock entrylk and inodelk once mkdir is done on all subvols*/
        dht_unlock_namespace(frame, &local->lock[0]);
        FRAME_SU_DO(frame, dht_local_t);
        dht_selfheal_new_directory(frame, dht_mkdir_selfheal_cbk, layout);
    }

    return 0;
}

static int
dht_mkdir_hashed_cbk(call_frame_t *frame, void *cookie, xlator_t *this,
                     int op_ret, int op_errno, inode_t *inode,
                     struct iatt *stbuf, struct iatt *preparent,
                     struct iatt *postparent, dict_t *xdata);

static int
dht_mkdir_helper(call_frame_t *frame, xlator_t *this, loc_t *loc, mode_t mode,
                 mode_t umask, dict_t *params)
{
    dht_local_t *local = NULL;
    dht_conf_t *conf = NULL;
    int op_errno = -1, ret = -1;
    xlator_t *hashed_subvol = NULL;
    int32_t *parent_disk_layout = NULL;
    dht_layout_t *parent_layout = NULL;
    char pgfid[GF_UUID_BUF_SIZE] = {0};

    VALIDATE_OR_GOTO(frame, err);
    VALIDATE_OR_GOTO(this, err);
    VALIDATE_OR_GOTO(loc, err);
    VALIDATE_OR_GOTO(loc->inode, err);
    VALIDATE_OR_GOTO(loc->path, err);
    VALIDATE_OR_GOTO(this->private, err);

    gf_uuid_unparse(loc->parent->gfid, pgfid);

    conf = this->private;
    local = frame->local;

    if (local->op_ret == -1) {
        gf_msg(this->name, GF_LOG_WARNING, local->op_errno,
               DHT_MSG_PARENT_LAYOUT_CHANGED,
               "mkdir (%s/%s) (path: %s): refreshing parent layout "
               "failed.",
               pgfid, loc->name, loc->path);

        op_errno = local->op_errno;
        goto err;
    }

    local->op_ret = -1;

    hashed_subvol = dht_subvol_get_hashed(this, loc);
    if (hashed_subvol == NULL) {
        gf_msg_debug(this->name, 0,
                     "mkdir (%s/%s) (path: %s): hashed subvol not "
                     "found",
                     pgfid, loc->name, loc->path);
        op_errno = ENOENT;
        goto err;
    }

    local->hashed_subvol = hashed_subvol;

    parent_layout = dht_layout_get(this, loc->parent);

    ret = dht_disk_layout_extract_for_subvol(this, parent_layout, hashed_subvol,
                                             &parent_disk_layout);
    if (ret == -1) {
        gf_msg(this->name, GF_LOG_WARNING, EIO, DHT_MSG_PARENT_LAYOUT_CHANGED,
               "mkdir (%s/%s) (path: %s): "
               "extracting in-memory layout of parent failed. ",
               pgfid, loc->name, loc->path);
        goto err;
    }

    if (memcmp(local->parent_disk_layout, parent_disk_layout,
               sizeof(local->parent_disk_layout)) == 0) {
        gf_msg(this->name, GF_LOG_WARNING, EIO, DHT_MSG_PARENT_LAYOUT_CHANGED,
               "mkdir (%s/%s) (path: %s): loop detected. "
               "parent layout didn't change even though "
               "previous attempt of mkdir failed because of "
               "in-memory layout not matching with that on disk.",
               pgfid, loc->name, loc->path);
        op_errno = EIO;
        goto err;
    }

    memcpy((void *)local->parent_disk_layout, (void *)parent_disk_layout,
           sizeof(local->parent_disk_layout));

    dht_layout_unref(this, parent_layout);
    parent_layout = NULL;

    ret = dict_set_str(params, GF_PREOP_PARENT_KEY, conf->xattr_name);
    if (ret < 0) {
        local->op_errno = -ret;
        gf_msg(this->name, GF_LOG_WARNING, local->op_errno,
               DHT_MSG_PARENT_LAYOUT_CHANGED,
               "mkdir (%s/%s) (path: %s): "
               "setting %s key in params dictionary failed. ",
               pgfid, loc->name, loc->path, GF_PREOP_PARENT_KEY);
        goto err;
    }

    ret = dict_set_bin(params, conf->xattr_name, parent_disk_layout, 4 * 4);
    if (ret < 0) {
        local->op_errno = -ret;
        gf_msg(this->name, GF_LOG_WARNING, local->op_errno,
               DHT_MSG_PARENT_LAYOUT_CHANGED,
               "setting parent-layout in params dictionary failed. "
               "mkdir (%s/%s) (path: %s)",
               pgfid, loc->name, loc->path);
        goto err;
    }

    parent_disk_layout = NULL;

    STACK_WIND_COOKIE(frame, dht_mkdir_hashed_cbk, hashed_subvol, hashed_subvol,
                      hashed_subvol->fops->mkdir, loc, mode, umask, params);

    return 0;

err:
    dht_unlock_namespace(frame, &local->lock[0]);

    op_errno = local ? local->op_errno : op_errno;
    DHT_STACK_UNWIND(mkdir, frame, -1, op_errno, NULL, NULL, NULL, NULL, NULL);

    if (parent_disk_layout != NULL)
        GF_FREE(parent_disk_layout);

    if (parent_layout != NULL)
        dht_layout_unref(this, parent_layout);

    return 0;
}

static int
dht_mkdir_hashed_cbk(call_frame_t *frame, void *cookie, xlator_t *this,
                     int op_ret, int op_errno, inode_t *inode,
                     struct iatt *stbuf, struct iatt *preparent,
                     struct iatt *postparent, dict_t *xdata)
{
    dht_local_t *local = NULL;
    int ret = -1;
    xlator_t *prev = NULL;
    dht_layout_t *layout = NULL;
    dht_conf_t *conf = NULL;
    int i = 0;
    xlator_t *hashed_subvol = NULL;
    char pgfid[GF_UUID_BUF_SIZE] = {0};
    gf_boolean_t parent_layout_changed = _gf_false;
    call_stub_t *stub = NULL;

    local = frame->local;
    prev = cookie;
    layout = local->layout;
    conf = this->private;
    hashed_subvol = local->hashed_subvol;

    gf_uuid_unparse(local->loc.parent->gfid, pgfid);

    if (gf_uuid_is_null(local->loc.gfid) && !op_ret)
        gf_uuid_copy(local->loc.gfid, stbuf->ia_gfid);

    if (op_ret == -1) {
        local->op_errno = op_errno;

        parent_layout_changed = (xdata &&
                                 dict_get(xdata, GF_PREOP_CHECK_FAILED))
                                    ? 1
                                    : 0;
        if (parent_layout_changed) {
            gf_msg(this->name, GF_LOG_INFO, 0, DHT_MSG_PARENT_LAYOUT_CHANGED,
                   "mkdir (%s/%s) (path: %s): parent layout "
                   "changed. Attempting a refresh and then a "
                   "retry",
                   pgfid, local->loc.name, local->loc.path);

            stub = fop_mkdir_stub(frame, dht_mkdir_helper, &local->loc,
                                  local->mode, local->umask, local->params);
            if (stub == NULL) {
                goto err;
            }

            ret = dht_handle_parent_layout_change(this, stub);
            if (ret) {
                goto err;
            }

            stub = NULL;

            return 0;
        }

        goto err;
    }

    dict_del(local->params, GF_PREOP_PARENT_KEY);
    dict_del(local->params, conf->xattr_name);

    if (dht_is_subvol_filled(this, hashed_subvol))
        ret = dht_layout_merge(this, layout, prev, -1, ENOSPC, NULL);
    else
        ret = dht_layout_merge(this, layout, prev, op_ret, op_errno, NULL);

    /* TODO: we may have to return from the function
       if layout merge fails. For now, lets just log an error */
    if (ret)
        gf_msg(this->name, GF_LOG_WARNING, 0, DHT_MSG_LAYOUT_MERGE_FAILED,
               "%s: failed to merge layouts for subvol %s", local->loc.path,
               prev->name);

    local->op_ret = 0;

    dht_iatt_merge(this, &local->stbuf, stbuf);
    dht_iatt_merge(this, &local->preparent, preparent);
    dht_iatt_merge(this, &local->postparent, postparent);

    local->call_cnt = conf->subvolume_cnt - 1;
    /* Delete internal mds xattr from params dict to avoid store
      internal mds xattr on other subvols
    */
    dict_del(local->params, conf->mds_xattr_key);

    if (gf_uuid_is_null(local->loc.gfid))
        gf_uuid_copy(local->loc.gfid, stbuf->ia_gfid);

    /* Set hashed subvol as a mds subvol on inode ctx */
    /*if (!local->inode)
            local->inode  = inode_ref (inode);
    */
    ret = dht_inode_ctx_mdsvol_set(local->inode, this, hashed_subvol);
    if (ret) {
        gf_msg(this->name, GF_LOG_ERROR, 0, DHT_MSG_SET_INODE_CTX_FAILED,
               "Failed to set hashed subvol for %s on inode vol is %s",
               local->loc.path, hashed_subvol->name);
    }

    if (local->call_cnt == 0) {
        /*Unlock namespace lock once mkdir is done on all subvols*/
        dht_unlock_namespace(frame, &local->lock[0]);
        FRAME_SU_DO(frame, dht_local_t);
        dht_selfheal_directory(frame, dht_mkdir_selfheal_cbk, &local->loc,
                               layout);
        return 0;
    }

    for (i = 0; i < conf->subvolume_cnt; i++) {
        if (conf->subvolumes[i] == hashed_subvol)
            continue;
        STACK_WIND_COOKIE(frame, dht_mkdir_cbk, conf->subvolumes[i],
                          conf->subvolumes[i], conf->subvolumes[i]->fops->mkdir,
                          &local->loc, local->mode, local->umask,
                          local->params);
    }

    return 0;
err:
    if (local->op_ret != 0) {
        dht_unlock_namespace(frame, &local->lock[0]);
    }

    DHT_STACK_UNWIND(mkdir, frame, -1, op_errno, NULL, NULL, NULL, NULL, NULL);

    return 0;
}

static int
dht_mkdir_guard_parent_layout_cbk(call_frame_t *frame, xlator_t *this,
                                  loc_t *loc, mode_t mode, mode_t umask,
                                  dict_t *params)
{
    dht_local_t *local = NULL;
    dht_conf_t *conf = 0;
    char pgfid[GF_UUID_BUF_SIZE] = {0};
    int ret = -1;
    int32_t zero[1] = {0};

    local = frame->local;
    conf = this->private;

    gf_uuid_unparse(loc->parent->gfid, pgfid);

    if (local->op_ret < 0) {
        gf_msg(this->name, GF_LOG_WARNING, local->op_errno,
               DHT_MSG_PARENT_LAYOUT_CHANGED,
               "mkdir (%s/%s) (path: %s): "
               "Acquiring lock on parent to guard against "
               "layout-change failed.",
               pgfid, loc->name, loc->path);
        goto err;
    }

    local->op_ret = -1;
    /* Add internal MDS xattr on disk for hashed subvol
     */
    ret = dht_dict_set_array(params, conf->mds_xattr_key, zero, 1);
    if (ret) {
        gf_msg(this->name, GF_LOG_WARNING, ENOMEM, DHT_MSG_DICT_SET_FAILED,
               "Failed to set dictionary value:key = %s for "
               "path %s",
               conf->mds_xattr_key, loc->path);
    }

    STACK_WIND_COOKIE(frame, dht_mkdir_hashed_cbk, local->hashed_subvol,
                      local->hashed_subvol, local->hashed_subvol->fops->mkdir,
                      loc, mode, umask, params);

    return 0;
err:
    DHT_STACK_UNWIND(mkdir, frame, -1, local->op_errno, NULL, NULL, NULL, NULL,
                     NULL);

    return 0;
}

int
dht_mkdir(call_frame_t *frame, xlator_t *this, loc_t *loc, mode_t mode,
          mode_t umask, dict_t *params)
{
    dht_local_t *local = NULL;
    dht_conf_t *conf = NULL;
    int op_errno = EINVAL, ret = -1;
    xlator_t *hashed_subvol = NULL;
    char pgfid[GF_UUID_BUF_SIZE] = {0};
    call_stub_t *stub = NULL;

    VALIDATE_OR_GOTO(frame, err);
    VALIDATE_OR_GOTO(this, err);
    VALIDATE_OR_GOTO(loc, err);
    VALIDATE_OR_GOTO(loc->inode, err);
    VALIDATE_OR_GOTO(loc->path, err);
    VALIDATE_OR_GOTO(this->private, err);

    gf_uuid_unparse(loc->parent->gfid, pgfid);

    conf = this->private;

    if (!params || !dict_get(params, "gfid-req")) {
        op_errno = EPERM;
        gf_msg_callingfn(this->name, GF_LOG_WARNING, op_errno,
                         DHT_MSG_GFID_NULL,
                         "mkdir: %s is received "
                         "without gfid-req %p",
                         loc->path, params);
        goto err;
    }

    dht_get_du_info(frame, this, loc);

    local = dht_local_init(frame, loc, NULL, GF_FOP_MKDIR);
    if (!local) {
        op_errno = ENOMEM;
        goto err;
    }

    hashed_subvol = dht_subvol_get_hashed(this, loc);
    if (hashed_subvol == NULL) {
        gf_msg_debug(this->name, 0, "hashed subvol not found for %s",
                     loc->path);
        local->op_errno = EIO;
        goto err;
    }

    local->hashed_subvol = hashed_subvol;
    local->mode = mode;
    local->umask = umask;
    if (params)
        local->params = dict_ref(params);

    local->inode = inode_ref(loc->inode);

    local->layout = dht_layout_new(this, conf->subvolume_cnt);
    if (!local->layout) {
        op_errno = ENOMEM;
        goto err;
    }

    /* set the newly created directory hash to the commit hash
     * if the configuration option is set. If configuration option
     * is not set, the older clients may still be connecting to the
     * volume and hence we need to preserve the 1 in disk[0] part of the
     * layout xattr */
    if (conf->lookup_optimize)
        local->layout->commit_hash = conf->vol_commit_hash;
    else
        local->layout->commit_hash = DHT_LAYOUT_HASH_INVALID;

    stub = fop_mkdir_stub(frame, dht_mkdir_guard_parent_layout_cbk, loc, mode,
                          umask, params);
    if (stub == NULL) {
        gf_msg(this->name, GF_LOG_WARNING, ENOMEM,
               DHT_MSG_PARENT_LAYOUT_CHANGED,
               "mkdir (%s/%s) (path: %s): "
               "creating stub failed.",
               pgfid, loc->name, loc->path);
        local->op_errno = ENOMEM;
        goto err;
    }

    ret = dht_guard_parent_layout_and_namespace(this, stub);
    if (ret < 0) {
        gf_msg(this->name, GF_LOG_WARNING, 0, DHT_MSG_PARENT_LAYOUT_CHANGED,
               "mkdir (%s/%s) (path: %s) cannot wind lock request to "
               "guard parent layout",
               pgfid, loc->name, loc->path);
        goto err;
    }

    return 0;

err:
    op_errno = local ? local->op_errno : op_errno;
    DHT_STACK_UNWIND(mkdir, frame, -1, op_errno, NULL, NULL, NULL, NULL, NULL);

    return 0;
}

static int
dht_rmdir_selfheal_cbk(call_frame_t *heal_frame, void *cookie, xlator_t *this,
                       int op_ret, int op_errno, dict_t *xdata)
{
    dht_local_t *local = NULL;
    dht_local_t *heal_local = NULL;
    call_frame_t *main_frame = NULL;

    heal_local = heal_frame->local;
    main_frame = heal_local->main_frame;
    local = main_frame->local;

    DHT_STACK_DESTROY(heal_frame);
    dht_set_fixed_dir_stat(&local->preparent);
    dht_set_fixed_dir_stat(&local->postparent);

    DHT_STACK_UNWIND(rmdir, main_frame, local->op_ret, local->op_errno,
                     &local->preparent, &local->postparent, NULL);

    return 0;
}

static int
dht_rmdir_hashed_subvol_cbk(call_frame_t *frame, void *cookie, xlator_t *this,
                            int op_ret, int op_errno, struct iatt *preparent,
                            struct iatt *postparent, dict_t *xdata)
{
    dht_local_t *local = NULL;
    dht_local_t *heal_local = NULL;
    call_frame_t *heal_frame = NULL;
    dht_conf_t *conf = NULL;
    int this_call_cnt = 0;
    xlator_t *prev = NULL;
    char gfid[GF_UUID_BUF_SIZE] = {0};

    local = frame->local;
    prev = cookie;
    conf = this->private;

    gf_uuid_unparse(local->loc.gfid, gfid);

    LOCK(&frame->lock);
    {
        if (op_ret == -1) {
            local->op_errno = op_errno;
            local->op_ret = -1;
            if (conf->subvolume_cnt != 1) {
                if (op_errno != ENOENT && op_errno != EACCES &&
                    op_errno != ESTALE) {
                    local->need_selfheal = 1;
                }
            }

            gf_msg_debug(this->name, op_errno,
                         "rmdir on %s for %s failed "
                         "(gfid = %s)",
                         prev->name, local->loc.path, gfid);
            goto unlock;
        }

        dht_iatt_merge(this, &local->preparent, preparent);
        dht_iatt_merge(this, &local->postparent, postparent);
    }
unlock:
    UNLOCK(&frame->lock);

    this_call_cnt = dht_frame_return(frame);
    if (is_last_call(this_call_cnt)) {
        if (local->need_selfheal) {
            dht_rmdir_unlock(frame, this);
            local->layout = dht_layout_get(this, local->loc.inode);

            /* TODO: neater interface needed below */
            local->stbuf.ia_type = local->loc.inode->ia_type;

            gf_uuid_copy(local->gfid, local->loc.inode->gfid);

            /* Use a different frame or else the rmdir op_ret is
             * overwritten by that of the selfheal */

            heal_frame = copy_frame(frame);

            if (heal_frame == NULL) {
                goto err;
            }

            heal_local = dht_local_init(heal_frame, &local->loc, NULL, 0);
            if (!heal_local) {
                DHT_STACK_DESTROY(heal_frame);
                goto err;
            }

            heal_local->inode = inode_ref(local->loc.inode);
            heal_local->main_frame = frame;
            gf_uuid_copy(heal_local->gfid, local->loc.inode->gfid);

            dht_selfheal_restore(heal_frame, dht_rmdir_selfheal_cbk,
                                 &heal_local->loc, heal_local->layout);
            return 0;
        } else {
            if (local->loc.parent) {
                dht_inode_ctx_time_update(local->loc.parent, this,
                                          &local->preparent, 0);

                dht_inode_ctx_time_update(local->loc.parent, this,
                                          &local->postparent, 1);
            }

            dht_set_fixed_dir_stat(&local->preparent);
            dht_set_fixed_dir_stat(&local->postparent);

            dht_rmdir_unlock(frame, this);
            DHT_STACK_UNWIND(rmdir, frame, local->op_ret, local->op_errno,
                             &local->preparent, &local->postparent, NULL);
        }
    }

    return 0;

err:
    DHT_STACK_UNWIND(rmdir, frame, local->op_ret, local->op_errno, NULL, NULL,
                     NULL);
    return 0;
}

static int
dht_rmdir_unlock_cbk(call_frame_t *frame, void *cookie, xlator_t *this,
                     int32_t op_ret, int32_t op_errno, dict_t *xdata)
{
    DHT_STACK_DESTROY(frame);
    return 0;
}

static int
dht_rmdir_unlock(call_frame_t *frame, xlator_t *this)
{
    dht_local_t *local = NULL, *lock_local = NULL;
    call_frame_t *lock_frame = NULL;
    int lock_count = 0;

    local = frame->local;

    /* Unlock entrylk */
    dht_unlock_entrylk_wrapper(frame, &local->lock[0].ns.directory_ns);

    /* Unlock inodelk */
    lock_count = dht_lock_count(local->lock[0].ns.parent_layout.locks,
                                local->lock[0].ns.parent_layout.lk_count);

    if (lock_count == 0)
        goto done;

    lock_frame = copy_frame(frame);
    if (lock_frame == NULL)
        goto done;

    lock_local = dht_local_init(lock_frame, &local->loc, NULL,
                                lock_frame->root->op);
    if (lock_local == NULL)
        goto done;

    lock_local->lock[0].ns.parent_layout.locks = local->lock[0]
                                                     .ns.parent_layout.locks;
    lock_local->lock[0]
        .ns.parent_layout.lk_count = local->lock[0].ns.parent_layout.lk_count;

    local->lock[0].ns.parent_layout.locks = NULL;
    local->lock[0].ns.parent_layout.lk_count = 0;
    dht_unlock_inodelk(lock_frame, lock_local->lock[0].ns.parent_layout.locks,
                       lock_local->lock[0].ns.parent_layout.lk_count,
                       dht_rmdir_unlock_cbk);
    lock_frame = NULL;

done:
    if (lock_frame != NULL) {
        DHT_STACK_DESTROY(lock_frame);
    }

    return 0;
}

static int
dht_rmdir_cbk(call_frame_t *frame, void *cookie, xlator_t *this, int op_ret,
              int op_errno, struct iatt *preparent, struct iatt *postparent,
              dict_t *xdata)
{
    dht_local_t *local = NULL;
    int this_call_cnt = 0;
    xlator_t *prev = NULL;
    int done = 0;
    char gfid[GF_UUID_BUF_SIZE] = {0};
    dht_local_t *heal_local = NULL;
    call_frame_t *heal_frame = NULL;
    int ret = -1;

    local = frame->local;
    prev = cookie;

    LOCK(&frame->lock);
    {
        if (op_ret == -1) {
            if ((op_errno != ENOENT) && (op_errno != ESTALE)) {
                local->op_errno = op_errno;
                local->op_ret = -1;

                if (op_errno != EACCES)
                    local->need_selfheal = 1;
            }

            gf_uuid_unparse(local->loc.gfid, gfid);

            gf_msg_debug(this->name, op_errno,
                         "rmdir on %s for %s failed."
                         "(gfid = %s)",
                         prev->name, local->loc.path, gfid);
            goto unlock;
        }

        /* Track if rmdir succeeded on at least one subvol*/
        local->fop_succeeded = 1;
        dht_iatt_merge(this, &local->preparent, preparent);
        dht_iatt_merge(this, &local->postparent, postparent);
    }
unlock:
    UNLOCK(&frame->lock);

    this_call_cnt = dht_frame_return(frame);

    /* if local->hashed_subvol, we are yet to wind to hashed_subvol. */
    if (local->hashed_subvol && (this_call_cnt == 1)) {
        done = 1;
    } else if (!local->hashed_subvol && !this_call_cnt) {
        done = 1;
    }

    if (done) {
        if (local->need_selfheal && local->fop_succeeded) {
            dht_rmdir_unlock(frame, this);
            local->layout = dht_layout_get(this, local->loc.inode);

            /* TODO: neater interface needed below */
            local->stbuf.ia_type = local->loc.inode->ia_type;

            gf_uuid_copy(local->gfid, local->loc.inode->gfid);
            heal_frame = copy_frame(frame);
            if (heal_frame == NULL) {
                goto err;
            }

            heal_local = dht_local_init(heal_frame, &local->loc, NULL, 0);
            if (!heal_local) {
                DHT_STACK_DESTROY(heal_frame);
                goto err;
            }

            heal_local->inode = inode_ref(local->loc.inode);
            heal_local->main_frame = frame;
            gf_uuid_copy(heal_local->gfid, local->loc.inode->gfid);
            ret = dht_selfheal_restore(heal_frame, dht_rmdir_selfheal_cbk,
                                       &heal_local->loc, heal_local->layout);
            if (ret) {
                DHT_STACK_DESTROY(heal_frame);
                goto err;
            }

        } else if (this_call_cnt) {
            /* If non-hashed subvol's have responded, proceed */
            if (local->op_ret == 0) {
                /* Delete the dir from the hashed subvol if:
                 * The fop succeeded on at least one subvol
                 *  and did not fail on any
                 *  or
                 *  The fop failed with ENOENT/ESTALE on
                 *  all subvols */

                STACK_WIND_COOKIE(frame, dht_rmdir_hashed_subvol_cbk,
                                  local->hashed_subvol, local->hashed_subvol,
                                  local->hashed_subvol->fops->rmdir,
                                  &local->loc, local->flags, NULL);
            } else {
                /* hashed-subvol was non-NULL and rmdir failed on
                 * all non hashed-subvols. Unwind rmdir with
                 * local->op_ret and local->op_errno. */
                dht_rmdir_unlock(frame, this);
                DHT_STACK_UNWIND(rmdir, frame, local->op_ret, local->op_errno,
                                 &local->preparent, &local->postparent, NULL);

                return 0;
            }
        } else if (!this_call_cnt) {
            /* All subvol's have responded, proceed */

            if (local->loc.parent) {
                dht_inode_ctx_time_update(local->loc.parent, this,
                                          &local->preparent, 0);

                dht_inode_ctx_time_update(local->loc.parent, this,
                                          &local->postparent, 1);
            }

            dht_set_fixed_dir_stat(&local->preparent);
            dht_set_fixed_dir_stat(&local->postparent);

            dht_rmdir_unlock(frame, this);
            DHT_STACK_UNWIND(rmdir, frame, local->op_ret, local->op_errno,
                             &local->preparent, &local->postparent, NULL);
        }
    }

    return 0;

err:
    DHT_STACK_UNWIND(rmdir, frame, -1, local->op_errno, NULL, NULL, NULL);
    return 0;
}

static int
dht_rmdir_lock_cbk(call_frame_t *frame, void *cookie, xlator_t *this,
                   int32_t op_ret, int32_t op_errno, dict_t *xdata)
{
    dht_local_t *local = NULL;
    dht_conf_t *conf = NULL;
    int i = 0;
    xlator_t *hashed_subvol;

    conf = this->private;
    local = frame->local;

    if (op_ret < 0) {
        gf_msg(this->name, GF_LOG_WARNING, op_errno, DHT_MSG_INODE_LK_ERROR,
               "acquiring entrylk after inodelk failed rmdir for %s)",
               local->loc.path);

        local->op_ret = -1;
        local->op_errno = op_errno;
        goto err;
    }

    hashed_subvol = local->hashed_subvol;
    for (i = 0; i < conf->subvolume_cnt; i++) {
        if (hashed_subvol && (hashed_subvol == conf->subvolumes[i]))
            continue;

        STACK_WIND_COOKIE(frame, dht_rmdir_cbk, conf->subvolumes[i],
                          conf->subvolumes[i], conf->subvolumes[i]->fops->rmdir,
                          &local->loc, local->flags, NULL);
    }

    return 0;

err:
    DHT_STACK_UNWIND(rmdir, frame, local->op_ret, local->op_errno,
                     &local->preparent, &local->postparent, NULL);

    return 0;
}

static int
dht_rmdir_do(call_frame_t *frame, xlator_t *this)
{
    dht_local_t *local = NULL;
    dht_conf_t *conf = NULL;
    int ret = -1;
    xlator_t *hashed_subvol = NULL;
    char gfid[GF_UUID_BUF_SIZE] = {0};

    VALIDATE_OR_GOTO(frame->local, err);
    local = frame->local;
    VALIDATE_OR_GOTO(this->private, out);
    conf = this->private;

    if (local->op_ret == -1)
        goto out;

    local->call_cnt = conf->subvolume_cnt;

    /* first remove from non-hashed_subvol */
    hashed_subvol = dht_subvol_get_hashed(this, &local->loc);

    if (!hashed_subvol) {
        gf_uuid_unparse(local->loc.gfid, gfid);

        gf_msg(this->name, GF_LOG_WARNING, 0, DHT_MSG_HASHED_SUBVOL_GET_FAILED,
               "Failed to get hashed subvol for %s (gfid = %s)",
               local->loc.path, gfid);
    } else {
        local->hashed_subvol = hashed_subvol;
    }

    /* When DHT has only 1 child */
    if (conf->subvolume_cnt == 1) {
        STACK_WIND_COOKIE(frame, dht_rmdir_hashed_subvol_cbk,
                          conf->subvolumes[0], conf->subvolumes[0],
                          conf->subvolumes[0]->fops->rmdir, &local->loc,
                          local->flags, NULL);
        return 0;
    }

    local->current = &local->lock[0];
    ret = dht_protect_namespace(frame, &local->loc, local->hashed_subvol,
                                &local->current->ns, dht_rmdir_lock_cbk);
    if (ret < 0) {
        local->op_ret = -1;
        local->op_errno = errno ? errno : EINVAL;
        goto out;
    }

    return 0;

out:
    dht_set_fixed_dir_stat(&local->preparent);
    dht_set_fixed_dir_stat(&local->postparent);

    DHT_STACK_UNWIND(rmdir, frame, local->op_ret, local->op_errno,
                     &local->preparent, &local->postparent, NULL);
    return 0;
err:
    DHT_STACK_UNWIND(rmdir, frame, -1, EINVAL, NULL, NULL, NULL);
    return 0;
}

static void
dht_rmdir_readdirp_done(call_frame_t *readdirp_frame, xlator_t *this)
{
    call_frame_t *main_frame = NULL;
    dht_local_t *main_local = NULL;
    dht_local_t *local = NULL;
    int this_call_cnt = 0;

    local = readdirp_frame->local;
    main_frame = local->main_frame;
    main_local = main_frame->local;

    /* At least one readdirp failed.
     * This is a bit hit or miss - if readdirp failed on more than
     * one subvol, we don't know which error is returned.
     */
    if (local->op_ret == -1) {
        main_local->op_ret = local->op_ret;
        main_local->op_errno = local->op_errno;
    }

    this_call_cnt = dht_frame_return(main_frame);

    if (is_last_call(this_call_cnt))
        dht_rmdir_do(main_frame, this);

    DHT_STACK_DESTROY(readdirp_frame);
}

/* Keep sending readdirp on the subvol until it returns no more entries
 * It is possible that not all entries will fit in a single readdirp in
 * which case the rmdir will keep failing with ENOTEMPTY
 */

static int
dht_rmdir_readdirp_do(call_frame_t *readdirp_frame, xlator_t *this)
{
    dht_local_t *local = NULL;

    local = readdirp_frame->local;

    if (local->op_ret == -1) {
        /* there is no point doing another readdirp on this
         * subvol . */
        dht_rmdir_readdirp_done(readdirp_frame, this);
        return 0;
    }

    STACK_WIND_COOKIE(readdirp_frame, dht_rmdir_readdirp_cbk,
                      local->hashed_subvol, local->hashed_subvol,
                      local->hashed_subvol->fops->readdirp, local->fd, 4096, 0,
                      local->xattr);

    return 0;
}

static int
dht_rmdir_linkfile_unlink_cbk(call_frame_t *frame, void *cookie, xlator_t *this,
                              int op_ret, int op_errno, struct iatt *preparent,
                              struct iatt *postparent, dict_t *xdata)
{
    dht_local_t *local = NULL;
    xlator_t *prev = NULL;
    xlator_t *src = NULL;
    call_frame_t *readdirp_frame = NULL;
    dht_local_t *readdirp_local = NULL;
    int this_call_cnt = 0;
    char gfid[GF_UUID_BUF_SIZE] = {0};

    local = frame->local;
    prev = cookie;
    src = prev;

    readdirp_frame = local->main_frame;
    readdirp_local = readdirp_frame->local;

    gf_uuid_unparse(local->loc.gfid, gfid);

    if (op_ret == 0) {
        gf_msg_trace(this->name, 0, "Unlinked linkfile %s on %s, gfid = %s",
                     local->loc.path, src->name, gfid);
    } else {
        if (op_errno != ENOENT) {
            readdirp_local->op_ret = -1;
            readdirp_local->op_errno = op_errno;
        }
        gf_msg_debug(this->name, op_errno,
                     "Unlink of %s on %s failed. (gfid = %s)", local->loc.path,
                     src->name, gfid);
    }

    this_call_cnt = dht_frame_return(readdirp_frame);

    if (is_last_call(this_call_cnt))
        dht_rmdir_readdirp_do(readdirp_frame, this);

    DHT_STACK_DESTROY(frame);
    return 0;
}

static int
dht_rmdir_lookup_cbk(call_frame_t *frame, void *cookie, xlator_t *this,
                     int op_ret, int op_errno, inode_t *inode,
                     struct iatt *stbuf, dict_t *xattr, struct iatt *parent)
{
    dht_local_t *local = NULL;
    xlator_t *prev = NULL;
    xlator_t *src = NULL;
    call_frame_t *readdirp_frame = NULL;
    dht_local_t *readdirp_local = NULL;
    int this_call_cnt = 0;
    dht_conf_t *conf = this->private;
    char gfid[GF_UUID_BUF_SIZE] = {0};

    local = frame->local;
    prev = cookie;
    src = prev;

    gf_msg_debug(this->name, 0, "dht_rmdir_lookup_cbk %s", local->loc.path);

    readdirp_frame = local->main_frame;
    readdirp_local = readdirp_frame->local;

    if (op_ret != 0) {
        gf_msg(this->name, GF_LOG_WARNING, op_errno, DHT_MSG_FILE_LOOKUP_FAILED,
               "lookup failed for %s on %s", local->loc.path, src->name);
        goto err;
    }

    if (!check_is_linkfile(inode, stbuf, xattr, conf->link_xattr_name)) {
        readdirp_local->op_ret = -1;
        readdirp_local->op_errno = ENOTEMPTY;

        gf_uuid_unparse(local->loc.gfid, gfid);

        gf_msg(this->name, GF_LOG_WARNING, 0, DHT_MSG_NOT_LINK_FILE_ERROR,
               "%s on %s is not a linkfile (type=0%o, gfid = %s)",
               local->loc.path, src->name, stbuf->ia_type, gfid);
        goto err;
    }

    STACK_WIND_COOKIE(frame, dht_rmdir_linkfile_unlink_cbk, src, src,
                      src->fops->unlink, &local->loc, 0, NULL);
    return 0;
err:

    this_call_cnt = dht_frame_return(readdirp_frame);
    if (is_last_call(this_call_cnt)) {
        dht_rmdir_readdirp_do(readdirp_frame, this);
    }

    DHT_STACK_DESTROY(frame);
    return 0;
}

static int
dht_rmdir_cached_lookup_cbk(call_frame_t *frame, void *cookie, xlator_t *this,
                            int op_ret, int op_errno, inode_t *inode,
                            struct iatt *stbuf, dict_t *xattr,
                            struct iatt *parent)
{
    dht_local_t *local = NULL;
    xlator_t *src = NULL;
    call_frame_t *readdirp_frame = NULL;
    dht_local_t *readdirp_local = NULL;
    int this_call_cnt = 0;
    dht_conf_t *conf = this->private;
    dict_t *xattrs = NULL;
    int ret = 0;

    local = frame->local;
    src = local->hashed_subvol;

    /* main_frame here is the readdirp_frame */

    readdirp_frame = local->main_frame;
    readdirp_local = readdirp_frame->local;

    gf_msg_debug(this->name, 0, "returning for %s ", local->loc.path);

    if (op_ret == 0) {
        readdirp_local->op_ret = -1;
        readdirp_local->op_errno = ENOTEMPTY;

        gf_msg(this->name, GF_LOG_WARNING, 0, DHT_MSG_SUBVOL_ERROR,
               "%s found on cached subvol %s", local->loc.path, src->name);
        goto err;
    } else if (op_errno != ENOENT) {
        readdirp_local->op_ret = -1;
        readdirp_local->op_errno = op_errno;

        gf_msg(this->name, GF_LOG_WARNING, op_errno, DHT_MSG_SUBVOL_ERROR,
               "%s not found on cached subvol %s", local->loc.path, src->name);
        goto err;
    }

    xattrs = dict_new();
    if (!xattrs) {
        gf_msg(this->name, GF_LOG_ERROR, ENOMEM, DHT_MSG_NO_MEMORY,
               "dict_new failed");
        goto err;
    }

    ret = dict_set_uint32(xattrs, conf->link_xattr_name, 256);
    if (ret) {
        gf_msg(this->name, GF_LOG_ERROR, 0, DHT_MSG_DICT_SET_FAILED,
               "Failed to set dictionary value: key = %s",
               conf->link_xattr_name);
        if (xattrs)
            dict_unref(xattrs);
        goto err;
    }
    STACK_WIND_COOKIE(frame, dht_rmdir_lookup_cbk, src, src, src->fops->lookup,
                      &local->loc, xattrs);
    if (xattrs)
        dict_unref(xattrs);

    return 0;
err:

    this_call_cnt = dht_frame_return(readdirp_frame);

    /* Once all the lookups/unlinks etc have returned, proceed to wind
     * readdirp on the subvol again until no entries are returned.
     * This is required if there are more entries than can be returned
     * in a single readdirp call.
     */

    if (is_last_call(this_call_cnt))
        dht_rmdir_readdirp_do(readdirp_frame, this);

    DHT_STACK_DESTROY(frame);
    return 0;
}

static int
dht_rmdir_is_subvol_empty(call_frame_t *frame, xlator_t *this,
                          gf_dirent_t *entries, xlator_t *src)
{
    int ret = 0;
    int build_ret = 0;
    gf_dirent_t *trav = NULL;
    call_frame_t *lookup_frame = NULL;
    dht_local_t *lookup_local = NULL;
    dht_local_t *local = NULL;
    dict_t *xattrs = NULL;
    dht_conf_t *conf = this->private;
    xlator_t *subvol = NULL;
    char gfid[GF_UUID_BUF_SIZE] = {0};
    int count = 0;
    gf_boolean_t unwind = _gf_false;

    local = frame->local;

    list_for_each_entry(trav, &entries->list, list)
    {
        if (strcmp(trav->d_name, ".") == 0)
            continue;
        if (strcmp(trav->d_name, "..") == 0)
            continue;
        if (check_is_linkfile(NULL, (&trav->d_stat), trav->dict,
                              conf->link_xattr_name)) {
            count++;
            continue;
        }

        /* this entry is either a directory which is neither "." nor "..",
           or a non directory which is not a linkfile. the directory is to
           be treated as non-empty
        */
        return 0;
    }

    xattrs = dict_new();
    if (!xattrs) {
        gf_msg(this->name, GF_LOG_ERROR, ENOMEM, DHT_MSG_NO_MEMORY,
               "dict_new failed");
        return -1;
    }

    ret = dict_set_uint32(xattrs, conf->link_xattr_name, 256);
    if (ret) {
        gf_msg(this->name, GF_LOG_ERROR, 0, DHT_MSG_DICT_SET_FAILED,
               "Failed to set dictionary value: key = %s",
               conf->link_xattr_name);

        if (xattrs)
            dict_unref(xattrs);
        return -1;
    }

    local->call_cnt = count;
    ret = 0;

    list_for_each_entry(trav, &entries->list, list)
    {
        if (strcmp(trav->d_name, ".") == 0)
            continue;
        if (strcmp(trav->d_name, "..") == 0)
            continue;

        lookup_frame = copy_frame(frame);

        if (!lookup_frame) {
            /* out of memory, let the rmdir fail
               (as non-empty, unfortunately) */
            goto err;
        }

        lookup_local = dht_local_init(lookup_frame, NULL, NULL, GF_FOP_LOOKUP);
        if (!lookup_local) {
            goto err;
        }

        lookup_frame->local = lookup_local;
        lookup_local->main_frame = frame;
        lookup_local->hashed_subvol = src;

        build_ret = dht_build_child_loc(this, &lookup_local->loc, &local->loc,
                                        trav->d_name);
        if (build_ret != 0)
            goto err;

        gf_uuid_copy(lookup_local->loc.gfid, trav->d_stat.ia_gfid);

        gf_uuid_unparse(lookup_local->loc.gfid, gfid);

        gf_msg_trace(this->name, 0, "looking up %s on subvolume %s, gfid = %s",
                     lookup_local->loc.path, src->name, gfid);

        subvol = dht_linkfile_subvol(this, NULL, &trav->d_stat, trav->dict);
        if (!subvol || (subvol == src)) {
            /* we need to delete the linkto file if it does not have a
             * valid subvol or it points to itself.
             */
            gf_msg(this->name, GF_LOG_INFO, 0, DHT_MSG_INVALID_LINKFILE,
                   "Linkfile does not have link subvolume. "
                   "path = %s, gfid = %s",
                   lookup_local->loc.path, gfid);

            gf_msg_debug(this->name, 0, "looking up %s on subvol %s, gfid = %s",
                         lookup_local->loc.path, src->name, gfid);

            STACK_WIND_COOKIE(lookup_frame, dht_rmdir_lookup_cbk, src, src,
                              src->fops->lookup, &lookup_local->loc, xattrs);
        } else {
            gf_msg_debug(this->name, 0,
                         "Looking up linkfile target %s on "
                         " subvol %s, gfid = %s",
                         lookup_local->loc.path, subvol->name, gfid);

            STACK_WIND(lookup_frame, dht_rmdir_cached_lookup_cbk, subvol,
                       subvol->fops->lookup, &lookup_local->loc, xattrs);
        }
        ret++;

        lookup_frame = NULL;
        lookup_local = NULL;
    }

    if (xattrs)
        dict_unref(xattrs);

    return ret;
err:
    if (xattrs)
        dict_unref(xattrs);

    if (lookup_frame)
        DHT_STACK_DESTROY(lookup_frame);

    /* Handle the case where the wound calls have unwound before the
     * loop processing is done
     */

    LOCK(&frame->lock);
    {
        local->op_ret = -1;
        local->op_errno = ENOTEMPTY;

        local->call_cnt -= (count - ret);
        if (!local->call_cnt)
            unwind = _gf_true;
    }
    UNLOCK(&frame->lock);

    if (!unwind) {
        return ret;
    }
    return 0;
}

/*
 * No more entries on this subvol. Proceed to the actual rmdir operation.
 */

static int
dht_rmdir_readdirp_cbk(call_frame_t *frame, void *cookie, xlator_t *this,
                       int op_ret, int op_errno, gf_dirent_t *entries,
                       dict_t *xdata)
{
    dht_local_t *local = NULL;
    xlator_t *prev = NULL;
    xlator_t *src = NULL;
    int ret = 0;
    char *path = NULL;

    local = frame->local;
    prev = cookie;
    src = prev;

    if (op_ret > 2) {
        /* dht_rmdir_is_subvol_empty() may free the frame,
         * copy path for logging.
         */
        path = gf_strdup(local->loc.path);

        ret = dht_rmdir_is_subvol_empty(frame, this, entries, src);

        switch (ret) {
            case 0: /* non linkfiles exist */
                gf_msg_trace(this->name, 0,
                             "readdir on %s for %s returned %d "
                             "entries",
                             prev->name, local->loc.path, op_ret);
                local->op_ret = -1;
                local->op_errno = ENOTEMPTY;
                break;
            default:
                /* @ret number of linkfiles are getting unlinked */
                gf_msg_trace(this->name, 0,
                             "readdir on %s for %s found %d "
                             "linkfiles",
                             prev->name, path, ret);
                break;
        }
    }

    /* readdirp failed or no linkto files were found on this subvol */
    if (!ret)
        dht_rmdir_readdirp_done(frame, this);

    GF_FREE(path);
    return 0;
}

static int
dht_rmdir_opendir_cbk(call_frame_t *frame, void *cookie, xlator_t *this,
                      int op_ret, int op_errno, fd_t *fd, dict_t *xdata)
{
    dht_local_t *local = NULL;
    int this_call_cnt = -1;
    xlator_t *prev = NULL;
    int ret = 0;
    dht_conf_t *conf = this->private;
    dict_t *dict = NULL;
    int i = 0;
    char gfid[GF_UUID_BUF_SIZE] = {0};
    dht_local_t *readdirp_local = NULL;
    call_frame_t *readdirp_frame = NULL;
    int cnt = 0;

    local = frame->local;
    prev = cookie;

    this_call_cnt = dht_frame_return(frame);
    if (op_ret == -1) {
        gf_uuid_unparse(local->loc.gfid, gfid);

        gf_msg_debug(this->name, op_errno,
                     "opendir on %s for %s failed, "
                     "gfid = %s,",
                     prev->name, local->loc.path, gfid);
        if ((op_errno != ENOENT) && (op_errno != ESTALE)) {
            local->op_ret = -1;
            local->op_errno = op_errno;
        }
        goto err;
    }

    if (!is_last_call(this_call_cnt))
        return 0;

    if (local->op_ret == -1)
        goto err;

    fd_bind(fd);

    dict = dict_new();
    if (!dict) {
        local->op_ret = -1;
        local->op_errno = ENOMEM;
        goto err;
    }

    ret = dict_set_uint32(dict, conf->link_xattr_name, 256);
    if (ret)
        gf_msg(this->name, GF_LOG_WARNING, 0, DHT_MSG_DICT_SET_FAILED,
               "%s: Failed to set dictionary value:key = %s", local->loc.path,
               conf->link_xattr_name);

    cnt = local->call_cnt = conf->subvolume_cnt;

    /* Create a separate frame per subvol as we might need
     * to resend readdirp multiple times to get all the
     * entries.
     */

    for (i = 0; i < conf->subvolume_cnt; i++) {
        readdirp_frame = copy_frame(frame);

        if (!readdirp_frame) {
            cnt--;
            /* Reduce the local->call_cnt as well */
            (void)dht_frame_return(frame);
            continue;
        }

        readdirp_local = dht_local_init(readdirp_frame, &local->loc, local->fd,
                                        0);

        if (!readdirp_local) {
            DHT_STACK_DESTROY(readdirp_frame);
            cnt--;
            /* Reduce the local->call_cnt as well */
            dht_frame_return(frame);
            continue;
        }
        readdirp_local->main_frame = frame;
        readdirp_local->op_ret = 0;
        readdirp_local->xattr = dict_ref(dict);
        /* overload this field to save the subvol info */
        readdirp_local->hashed_subvol = conf->subvolumes[i];

        STACK_WIND_COOKIE(readdirp_frame, dht_rmdir_readdirp_cbk,
                          conf->subvolumes[i], conf->subvolumes[i],
                          conf->subvolumes[i]->fops->readdirp,
                          readdirp_local->fd, 4096, 0, readdirp_local->xattr);
    }

    if (dict)
        dict_unref(dict);

    /* Could not wind readdirp to any subvol */

    if (!cnt)
        goto err;

    return 0;

err:
    if (is_last_call(this_call_cnt)) {
        dht_rmdir_do(frame, this);
    }

    return 0;
}

int
dht_rmdir(call_frame_t *frame, xlator_t *this, loc_t *loc, int flags,
          dict_t *xdata)
{
    dht_local_t *local = NULL;
    dht_conf_t *conf = NULL;
    int op_errno = -1;
    int i = -1;
    int ret = -1;
    dict_t *xattr_req = NULL;

    VALIDATE_OR_GOTO(frame, err);
    VALIDATE_OR_GOTO(this, err);
    VALIDATE_OR_GOTO(loc, err);
    VALIDATE_OR_GOTO(loc->inode, err);
    VALIDATE_OR_GOTO(loc->path, err);
    VALIDATE_OR_GOTO(this->private, err);

    conf = this->private;

    local = dht_local_init(frame, loc, NULL, GF_FOP_RMDIR);
    if (!local) {
        op_errno = ENOMEM;
        goto err;
    }

    local->call_cnt = conf->subvolume_cnt;
    local->op_ret = 0;
    local->fop_succeeded = 0;

    local->flags = flags;

    local->fd = fd_create(local->loc.inode, frame->root->pid);
    if (!local->fd) {
        op_errno = ENOMEM;
        goto err;
    }

    if (flags) {
        return dht_rmdir_do(frame, this);
    }
    if (xdata) {
        xattr_req = dict_ref(xdata);
    } else {
        xattr_req = dict_new();
    }
    if (xattr_req) {
        ret = dict_set_uint32(xattr_req, conf->link_xattr_name, 256);
        /* If parallel-readdir is enabled, this is required
         * to handle stale linkto files in the directory
         * being deleted. If this fails, log an error but
         * do not prevent the operation.
         */
        if (ret) {
            gf_msg(this->name, GF_LOG_ERROR, 0, 0, "%s: failed to set key %s",
                   loc->path, conf->link_xattr_name);
        }
    } else {
        gf_msg(this->name, GF_LOG_ERROR, 0, 0, "%s: failed to set key %s",
               loc->path, conf->link_xattr_name);
    }

    for (i = 0; i < conf->subvolume_cnt; i++) {
        STACK_WIND_COOKIE(frame, dht_rmdir_opendir_cbk, conf->subvolumes[i],
                          conf->subvolumes[i],
                          conf->subvolumes[i]->fops->opendir, loc, local->fd,
                          xattr_req);
    }

    if (xattr_req) {
        dict_unref(xattr_req);
    }
    return 0;

err:
    op_errno = (op_errno == -1) ? errno : op_errno;
    DHT_STACK_UNWIND(rmdir, frame, -1, op_errno, NULL, NULL, NULL);

    return 0;
}

static int
dht_entrylk_cbk(call_frame_t *frame, void *cookie, xlator_t *this,
                int32_t op_ret, int32_t op_errno, dict_t *xdata)

{
    DHT_STACK_UNWIND(entrylk, frame, op_ret, op_errno, xdata);
    return 0;
}

/* TODO
 * Sending entrylk to cached subvol can result in stale lock
 * as described in the bug 1311002.
 */
int
dht_entrylk(call_frame_t *frame, xlator_t *this, const char *volume, loc_t *loc,
            const char *basename, entrylk_cmd cmd, entrylk_type type,
            dict_t *xdata)
{
    xlator_t *subvol = NULL;
    int op_errno = -1;
    dht_local_t *local = NULL;
    char gfid[GF_UUID_BUF_SIZE] = {0};

    VALIDATE_OR_GOTO(frame, err);
    VALIDATE_OR_GOTO(this, err);
    VALIDATE_OR_GOTO(loc, err);
    VALIDATE_OR_GOTO(loc->inode, err);

    local = dht_local_init(frame, loc, NULL, GF_FOP_ENTRYLK);
    if (!local) {
        op_errno = ENOMEM;
        goto err;
    }

    subvol = local->cached_subvol;
    if (!subvol) {
        gf_uuid_unparse(loc->gfid, gfid);

        gf_msg_debug(this->name, 0,
                     "no cached subvolume for path=%s, "
                     "gfid = %s",
                     loc->path, gfid);
        op_errno = EINVAL;
        goto err;
    }

    local->call_cnt = 1;

    STACK_WIND(frame, dht_entrylk_cbk, subvol, subvol->fops->entrylk, volume,
               loc, basename, cmd, type, xdata);

    return 0;

err:
    op_errno = (op_errno == -1) ? errno : op_errno;
    DHT_STACK_UNWIND(entrylk, frame, -1, op_errno, NULL);

    return 0;
}

static int
dht_fentrylk_cbk(call_frame_t *frame, void *cookie, xlator_t *this,
                 int32_t op_ret, int32_t op_errno, dict_t *xdata)

{
    DHT_STACK_UNWIND(fentrylk, frame, op_ret, op_errno, NULL);
    return 0;
}

int
dht_fentrylk(call_frame_t *frame, xlator_t *this, const char *volume, fd_t *fd,
             const char *basename, entrylk_cmd cmd, entrylk_type type,
             dict_t *xdata)
{
    xlator_t *subvol = NULL;
    int op_errno = -1;
    char gfid[GF_UUID_BUF_SIZE] = {0};

    VALIDATE_OR_GOTO(frame, err);
    VALIDATE_OR_GOTO(this, err);
    VALIDATE_OR_GOTO(fd, err);
    VALIDATE_OR_GOTO(fd->inode, err);

    gf_uuid_unparse(fd->inode->gfid, gfid);

    subvol = dht_subvol_get_cached(this, fd->inode);
    if (!subvol) {
        gf_msg_debug(this->name, 0,
                     "No cached subvolume for fd=%p,"
                     " gfid = %s",
                     fd, gfid);
        op_errno = EINVAL;
        goto err;
    }

    STACK_WIND(frame, dht_fentrylk_cbk, subvol, subvol->fops->fentrylk, volume,
               fd, basename, cmd, type, xdata);

    return 0;

err:
    op_errno = (op_errno == -1) ? errno : op_errno;
    DHT_STACK_UNWIND(fentrylk, frame, -1, op_errno, NULL);

    return 0;
}

static int32_t
dht_ipc_cbk(call_frame_t *frame, void *cookie, xlator_t *this, int32_t op_ret,
            int32_t op_errno, dict_t *xdata)
{
    dht_local_t *local = NULL;
    int this_call_cnt = 0;

    GF_VALIDATE_OR_GOTO("dht", frame, out);
    GF_VALIDATE_OR_GOTO("dht", this, out);
    GF_VALIDATE_OR_GOTO("dht", frame->local, out);

    local = frame->local;

    LOCK(&frame->lock);
    {
        if (op_ret < 0 && op_errno != ENOTCONN) {
            local->op_errno = op_errno;
            goto unlock;
        }
        local->op_ret = 0;
    }
unlock:
    UNLOCK(&frame->lock);

    this_call_cnt = dht_frame_return(frame);
    if (is_last_call(this_call_cnt)) {
        DHT_STACK_UNWIND(ipc, frame, local->op_ret, local->op_errno, NULL);
    }

out:
    return 0;
}

int32_t
dht_ipc(call_frame_t *frame, xlator_t *this, int32_t op, dict_t *xdata)
{
    dht_local_t *local = NULL;
    int op_errno = EINVAL;
    dht_conf_t *conf = NULL;
    int call_cnt = 0;
    int i = 0;

    VALIDATE_OR_GOTO(frame, err);
    VALIDATE_OR_GOTO(this, err);

    if (op != GF_IPC_TARGET_UPCALL)
        goto wind_default;

    VALIDATE_OR_GOTO(this->private, err);
    conf = this->private;

    local = dht_local_init(frame, NULL, NULL, GF_FOP_IPC);
    if (!local) {
        op_errno = ENOMEM;
        goto err;
    }

    call_cnt = conf->subvolume_cnt;
    local->call_cnt = call_cnt;

    if (xdata) {
        if (dict_set_int8(xdata, conf->xattr_name, 0) < 0)
            goto err;
    }

    for (i = 0; i < call_cnt; i++) {
        STACK_WIND(frame, dht_ipc_cbk, conf->subvolumes[i],
                   conf->subvolumes[i]->fops->ipc, op, xdata);
    }

    return 0;

err:
    DHT_STACK_UNWIND(ipc, frame, -1, op_errno, NULL);

    return 0;

wind_default:
    STACK_WIND(frame, default_ipc_cbk, FIRST_CHILD(this),
               FIRST_CHILD(this)->fops->ipc, op, xdata);
    return 0;
}

int
dht_forget(xlator_t *this, inode_t *inode)
{
    uint64_t ctx_int = 0;
    dht_inode_ctx_t *ctx = NULL;
    dht_layout_t *layout = NULL;

    inode_ctx_del(inode, this, &ctx_int);

    if (!ctx_int)
        return 0;

    ctx = (dht_inode_ctx_t *)(long)ctx_int;

    layout = ctx->layout;
    ctx->layout = NULL;
    dht_layout_unref(this, layout);
    GF_FREE(ctx);

    return 0;
}

int
dht_notify(xlator_t *this, int event, void *data, ...)
{
    xlator_t *subvol = NULL;
    int cnt = -1;
    int i = -1;
    dht_conf_t *conf = NULL;
    int ret = -1;
    int propagate = 0;

    int had_heard_from_all = 0;
    int have_heard_from_all = 0;
    gf_defrag_info_t *defrag = NULL;
    dict_t *dict = NULL;
    gf_defrag_type cmd = 0;
    dict_t *output = NULL;
    va_list ap;
    struct gf_upcall *up_data = NULL;
    struct gf_upcall_cache_invalidation *up_ci = NULL;

    conf = this->private;
    GF_VALIDATE_OR_GOTO(this->name, conf, out);

    /* had all subvolumes reported status once till now? */
    had_heard_from_all = 1;
    for (i = 0; i < conf->subvolume_cnt; i++) {
        if (!conf->last_event[i]) {
            had_heard_from_all = 0;
        }
    }

    switch (event) {
        case GF_EVENT_CHILD_UP:
            subvol = data;

            conf->gen++;

            for (i = 0; i < conf->subvolume_cnt; i++) {
                if (subvol == conf->subvolumes[i]) {
                    cnt = i;
                    break;
                }
            }

            if (cnt == -1) {
                gf_msg_debug(this->name, 0,
                             "got GF_EVENT_CHILD_UP bad "
                             "subvolume %s",
                             subvol->name);
                break;
            }

            LOCK(&conf->subvolume_lock);
            {
                conf->subvolume_status[cnt] = 1;
                conf->last_event[cnt] = event;
                conf->subvol_up_time[cnt] = gf_time();
            }
            UNLOCK(&conf->subvolume_lock);

            /* one of the node came back up, do a stat update */
            dht_get_du_info_for_subvol(this, cnt);

            break;

        case GF_EVENT_SOME_DESCENDENT_UP:
            subvol = data;
            conf->gen++;
            propagate = 1;

            break;

        case GF_EVENT_SOME_DESCENDENT_DOWN:
            subvol = data;
            propagate = 1;

            break;

        case GF_EVENT_CHILD_DOWN:
            subvol = data;

            if (conf->assert_no_child_down) {
                gf_msg(this->name, GF_LOG_WARNING, 0, DHT_MSG_CHILD_DOWN,
                       "Received CHILD_DOWN. Exiting");
                if (conf->defrag) {
                    gf_defrag_stop(conf, GF_DEFRAG_STATUS_FAILED, NULL);
                } else {
                    kill(getpid(), SIGTERM);
                }
            }

            for (i = 0; i < conf->subvolume_cnt; i++) {
                if (subvol == conf->subvolumes[i]) {
                    cnt = i;
                    break;
                }
            }

            if (cnt == -1) {
                gf_msg_debug(this->name, 0,
                             "got GF_EVENT_CHILD_DOWN bad "
                             "subvolume %s",
                             subvol->name);
                break;
            }

            LOCK(&conf->subvolume_lock);
            {
                conf->subvolume_status[cnt] = 0;
                conf->last_event[cnt] = event;
                conf->subvol_up_time[cnt] = 0;
            }
            UNLOCK(&conf->subvolume_lock);

            for (i = 0; i < conf->subvolume_cnt; i++)
                if (conf->last_event[i] != event)
                    event = GF_EVENT_SOME_DESCENDENT_DOWN;
            break;

        case GF_EVENT_CHILD_CONNECTING:
            subvol = data;

            for (i = 0; i < conf->subvolume_cnt; i++) {
                if (subvol == conf->subvolumes[i]) {
                    cnt = i;
                    break;
                }
            }

            if (cnt == -1) {
                gf_msg_debug(this->name, 0,
                             "got GF_EVENT_CHILD_CONNECTING"
                             " bad subvolume %s",
                             subvol->name);
                break;
            }

            LOCK(&conf->subvolume_lock);
            {
                conf->last_event[cnt] = event;
            }
            UNLOCK(&conf->subvolume_lock);

            break;
        case GF_EVENT_VOLUME_DEFRAG: {
            if (!conf->defrag) {
                return ret;
            }
            defrag = conf->defrag;

            dict = data;
            va_start(ap, data);
            output = va_arg(ap, dict_t *);

            ret = dict_get_int32(dict, "rebalance-command", (int32_t *)&cmd);
            if (ret) {
                va_end(ap);
                return ret;
            }
            LOCK(&defrag->lock);
            {
                if (defrag->is_exiting)
                    goto unlock;
                if ((cmd == GF_DEFRAG_CMD_STATUS) ||
                    (cmd == GF_DEFRAG_CMD_DETACH_STATUS))
                    gf_defrag_status_get(conf, output, _gf_false);
                else if (cmd == GF_DEFRAG_CMD_DETACH_START)
                    defrag->cmd = GF_DEFRAG_CMD_DETACH_START;
                else if (cmd == GF_DEFRAG_CMD_STOP ||
                         cmd == GF_DEFRAG_CMD_DETACH_STOP)
                    gf_defrag_stop(conf, GF_DEFRAG_STATUS_STOPPED, output);
            }
        unlock:
            UNLOCK(&defrag->lock);
            va_end(ap);
            return ret;
            break;
        }
        case GF_EVENT_UPCALL:
            up_data = (struct gf_upcall *)data;
            if (up_data->event_type != GF_UPCALL_CACHE_INVALIDATION)
                break;
            up_ci = (struct gf_upcall_cache_invalidation *)up_data->data;

            /* Since md-cache will be aggressively filtering lookups,
             * the stale layout issue will be more pronounced. Hence
             * when a layout xattr is changed by the rebalance process
             * notify all the md-cache clients to invalidate the existing
             * stat cache and send the lookup next time*/
            if (up_ci->dict && dict_get(up_ci->dict, conf->xattr_name))
                up_ci->flags |= UP_EXPLICIT_LOOKUP;

            /* TODO: Instead of invalidating iatt, update the new
             * hashed/cached subvolume in dht inode_ctx */
            if (IS_DHT_LINKFILE_MODE(&up_ci->stat))
                up_ci->flags |= UP_EXPLICIT_LOOKUP;

            propagate = 1;
            break;
        default:
            propagate = 1;
            break;
    }

    /* have all subvolumes reported status once by now? */
    have_heard_from_all = 1;
    for (i = 0; i < conf->subvolume_cnt; i++) {
        if (!conf->last_event[i])
            have_heard_from_all = 0;
    }

    /* if all subvols have reported status, no need to hide anything
       or wait for anything else. Just propagate blindly */
    if (have_heard_from_all) {
        propagate = 1;
    }

    if (!had_heard_from_all && have_heard_from_all) {
        static int run_defrag = 0;
        /* This is the first event which completes aggregation
           of events from all subvolumes. If at least one subvol
           had come up, propagate CHILD_UP, but only this time
        */
        event = GF_EVENT_CHILD_DOWN;

        for (i = 0; i < conf->subvolume_cnt; i++) {
            if (conf->last_event[i] == GF_EVENT_CHILD_UP) {
                event = GF_EVENT_CHILD_UP;
                break;
            }

            if (conf->last_event[i] == GF_EVENT_CHILD_CONNECTING) {
                event = GF_EVENT_CHILD_CONNECTING;
                /* continue to check other events for CHILD_UP */
            }
        }

        /* Rebalance is started with assert_no_child_down. So we do
         * not need to handle CHILD_DOWN event here.
         *
         * If there is a graph switch, we should not restart the
         * rebalance daemon. Use 'run_defrag' to indicate if the
         * thread has already started.
         */
        if (conf->defrag && !run_defrag) {
            run_defrag = 1;
            ret = gf_thread_create(&conf->defrag->th, NULL, gf_defrag_start,
                                   this, "dhtdg");
            if (ret) {
                GF_FREE(conf->defrag);
                conf->defrag = NULL;
                kill(getpid(), SIGTERM);
            }
        }
    }

    ret = 0;
    if (propagate)
        ret = default_notify(this, event, data);
out:
    return ret;
}

int
dht_inode_ctx_layout_get(inode_t *inode, xlator_t *this, dht_layout_t **layout)
{
    dht_inode_ctx_t *ctx = NULL;
    int ret = -1;

    ret = dht_inode_ctx_get(inode, this, &ctx);

    if (!ret && ctx) {
        if (ctx->layout) {
            if (layout)
                *layout = ctx->layout;
            ret = 0;
        } else {
            ret = -1;
        }
    }

    return ret;
}

void
dht_log_new_layout_for_dir_selfheal(xlator_t *this, loc_t *loc,
                                    dht_layout_t *layout)
{
    char string[2048] = {0};
    char *output_string = NULL;
    int len = 0;
    int off = 0;
    int i = 0;
    gf_loglevel_t log_level = gf_log_get_loglevel();
    int ret = 0;

    if (log_level < GF_LOG_INFO)
        return;

    if (!layout)
        return;

    if (!layout->cnt)
        return;

    if (!loc)
        return;

    if (!loc->path)
        return;

    ret = snprintf(string, sizeof(string), "Setting layout of %s with ",
                   loc->path);

    if (ret < 0)
        return;

    len += ret;

    /* Calculation  of total length of the string required to calloc
     * output_string. Log includes subvolume-name, start-range, end-range
     * and err value.
     *
     * This log will help to debug cases where:
     * a) Different processes set different layout of a directory.
     * b) Error captured in lookup, which will be filled in layout->err
     * (like ENOENT, ESTALE etc)
     */

    for (i = 0; i < layout->cnt; i++) {
        ret = snprintf(string, sizeof(string),
                       "[Subvol_name: %s, Err: %d , Start: "
                       "0x%x, Stop: 0x%x, Hash: 0x%x], ",
                       layout->list[i].xlator->name, layout->list[i].err,
                       layout->list[i].start, layout->list[i].stop,
                       layout->list[i].commit_hash);

        if (ret < 0)
            return;

        len += ret;
    }

    len++;

    output_string = GF_MALLOC(len + 1, gf_common_mt_char);

    if (!output_string)
        return;

    ret = snprintf(output_string, len + 1, "Setting layout of %s with ",
                   loc->path);

    if (ret < 0)
        goto err;

    off += ret;

    for (i = 0; i < layout->cnt; i++) {
        ret = snprintf(output_string + off, len - off,
                       "[Subvol_name: %s, Err: %d , Start: "
                       "0x%x, Stop: 0x%x, Hash: 0x%x], ",
                       layout->list[i].xlator->name, layout->list[i].err,
                       layout->list[i].start, layout->list[i].stop,
                       layout->list[i].commit_hash);

        if (ret < 0)
            goto err;

        off += ret;
    }

    gf_msg(this->name, GF_LOG_DEBUG, 0, DHT_MSG_LOG_FIXED_LAYOUT, "%s",
           output_string);

err:
    GF_FREE(output_string);
}

int32_t
dht_migration_get_dst_subvol(xlator_t *this, dht_local_t *local)
{
    int ret = -1;

    if (!local)
        goto out;

    local->rebalance.target_node = dht_subvol_get_hashed(this, &local->loc);

    if (local->rebalance.target_node)
        ret = 0;

out:
    return ret;
}

/*
This function should not be called more then once during a FOP
handling path. It is valid only for for ops on files
*/
int32_t
dht_set_local_rebalance(xlator_t *this, dht_local_t *local, struct iatt *stbuf,
                        struct iatt *prebuf, struct iatt *postbuf,
                        dict_t *xdata)
{
    if (!local)
        return -1;

    if (local->rebalance.set) {
        gf_msg(this->name, GF_LOG_WARNING, 0, DHT_MSG_REBAL_STRUCT_SET,
               "local->rebalance already set");
    }

    if (stbuf)
        memcpy(&local->rebalance.stbuf, stbuf, sizeof(struct iatt));

    if (prebuf)
        memcpy(&local->rebalance.prebuf, prebuf, sizeof(struct iatt));

    if (postbuf)
        memcpy(&local->rebalance.postbuf, postbuf, sizeof(struct iatt));

    if (xdata)
        local->rebalance.xdata = dict_ref(xdata);

    local->rebalance.set = 1;

    return 0;
}

int32_t
dht_release(xlator_t *this, fd_t *fd)
{
    return dht_fd_ctx_destroy(this, fd);
}

static int
dht_pt_mkdir_cbk(call_frame_t *frame, void *cookie, xlator_t *this, int op_ret,
                 int op_errno, inode_t *inode, struct iatt *stbuf,
                 struct iatt *preparent, struct iatt *postparent, dict_t *xdata)
{
    dht_local_t *local = NULL;

    local = frame->local;

    if (!op_ret) {
        dht_layout_set(this, inode, local->layout);
    }

    DHT_STACK_UNWIND(mkdir, frame, op_ret, op_errno, inode, stbuf, preparent,
                     postparent, NULL);

    return 0;
}

int32_t
dht_pt_mkdir(call_frame_t *frame, xlator_t *this, loc_t *loc, mode_t mode,
             mode_t umask, dict_t *xdata)
{
    dht_layout_t *layout = NULL;
    dht_conf_t *conf = NULL;
    dht_local_t *local = NULL;
    bool free_xdata = false;
    int ret = 0;
    int op_errno = 0;
    int32_t *disk_layout_p = NULL;

    conf = this->private;

    local = dht_local_init(frame, loc, NULL, GF_FOP_MKDIR);
    if (!local) {
        op_errno = ENOMEM;
        goto err;
    }

    layout = dht_layout_new(this, conf->subvolume_cnt);
    if (!layout)
        goto wind;

    local->layout = layout;

    if (!xdata) {
        xdata = dict_new();
        if (!xdata)
            goto wind;
        free_xdata = true;
    }

    /*Set the xlator or the following will crash*/
    layout->list[0].xlator = conf->subvolumes[0];

    dht_selfheal_layout_new_directory(frame, loc, layout);

    dht_disk_layout_extract(this, layout, 0, &disk_layout_p);

    ret = dict_set_bin(xdata, conf->xattr_name, disk_layout_p, 4 * 4);
    if (ret) {
        gf_msg("dht", GF_LOG_DEBUG, EINVAL, DHT_MSG_DICT_SET_FAILED,
               "dht layout dict set failed");
    }
wind:
    STACK_WIND(frame, dht_pt_mkdir_cbk, FIRST_CHILD(this),
               FIRST_CHILD(this)->fops->mkdir, loc, mode, umask, xdata);
    if (free_xdata)
        dict_unref(xdata);
    return 0;

err:
    op_errno = local ? local->op_errno : op_errno;
    DHT_STACK_UNWIND(mkdir, frame, -1, op_errno, NULL, NULL, NULL, NULL, NULL);

    return 0;
}

static int
dht_pt_getxattr_cbk(call_frame_t *frame, void *cookie, xlator_t *this,
                    int op_ret, int op_errno, dict_t *xattr, dict_t *xdata)
{
    dht_conf_t *conf = NULL;

    conf = this->private;
    dict_del(xattr, conf->xattr_name);
    dict_del(xattr, conf->mds_xattr_key);
    dict_del(xattr, conf->commithash_xattr_name);

    if (frame->root->pid >= 0) {
        GF_REMOVE_INTERNAL_XATTR("trusted.glusterfs.quota*", xattr);
        GF_REMOVE_INTERNAL_XATTR("trusted.pgfid*", xattr);
    }

    DHT_STACK_UNWIND(getxattr, frame, op_ret, op_errno, xattr, xdata);
    return 0;
}

int
dht_pt_getxattr(call_frame_t *frame, xlator_t *this, loc_t *loc,
                const char *key, dict_t *xdata)
{
    int op_errno = -1;
    dht_local_t *local = NULL;

    VALIDATE_OR_GOTO(frame, err);
    VALIDATE_OR_GOTO(this, err);
    VALIDATE_OR_GOTO(loc, err);
    VALIDATE_OR_GOTO(loc->inode, err);
    VALIDATE_OR_GOTO(this->private, err);

    local = dht_local_init(frame, loc, NULL, GF_FOP_GETXATTR);
    if (!local) {
        op_errno = ENOMEM;
        goto err;
    }

    if (key &&
        strncmp(key, DHT_SUBVOL_STATUS_KEY, SLEN(DHT_SUBVOL_STATUS_KEY)) == 0) {
        dht_vgetxattr_subvol_status(frame, this, key);
        return 0;
    }

    STACK_WIND(frame, dht_pt_getxattr_cbk, FIRST_CHILD(this),
               FIRST_CHILD(this)->fops->getxattr, loc, key, xdata);
    return 0;
err:
    DHT_STACK_UNWIND(getxattr, frame, -1, op_errno, NULL, NULL);
    return 0;
}

static int
dht_pt_fgetxattr_cbk(call_frame_t *frame, void *cookie, xlator_t *this,
                     int op_ret, int op_errno, dict_t *xattr, dict_t *xdata)
{
    dht_conf_t *conf = NULL;

    conf = this->private;
    dict_del(xattr, conf->xattr_name);

    if (frame->root->pid >= 0) {
        GF_REMOVE_INTERNAL_XATTR("trusted.glusterfs.quota*", xattr);
        GF_REMOVE_INTERNAL_XATTR("trusted.pgfid*", xattr);
    }

    DHT_STACK_UNWIND(fgetxattr, frame, op_ret, op_errno, xattr, xdata);
    return 0;
}

int
dht_pt_fgetxattr(call_frame_t *frame, xlator_t *this, fd_t *fd, const char *key,
                 dict_t *xdata)
{
    STACK_WIND(frame, dht_pt_fgetxattr_cbk, FIRST_CHILD(this),
               FIRST_CHILD(this)->fops->fgetxattr, fd, key, xdata);
    return 0;
}

/* The job of this function is to check if all the xlators have updated
 * error in the layout. */
int
dht_dir_layout_error_check(xlator_t *this, inode_t *inode)
{
    dht_layout_t *layout = NULL;
    int i = 0;

    layout = dht_layout_get(this, inode);
    for (i = 0; i < layout->cnt; i++) {
        if (layout->list[i].err == 0) {
            return 0;
        }
    }

    /* Returning the first xlator error as all xlators have errors */
    return layout->list[0].err;
}
