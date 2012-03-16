/*
   Copyright (c) 2010-2011 Gluster, Inc. <http://www.gluster.com>
   This file is part of GlusterFS.

   GlusterFS is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published
   by the Free Software Foundation; either version 3 of the License,
   or (at your option) any later version.

   GlusterFS is distributed in the hope that it will be useful, but
   WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program.  If not, see
   <http://www.gnu.org/licenses/>.
*/

#ifndef _CONFIG_H
#define _CONFIG_H
#include "config.h"
#endif
#include "afr.h"
#include "syncop.h"
#include "afr-self-heald.h"
#include "afr-self-heal-common.h"
#include "protocol-common.h"
#include "event-history.h"

#define AFR_POLL_TIMEOUT 600

typedef enum {
        STOP_CRAWL_ON_SINGLE_SUBVOL = 1
} afr_crawl_flags_t;

typedef enum {
        HEAL = 1,
        INFO
} shd_crawl_op;

typedef struct shd_dump {
        dict_t   *dict;
        time_t   sh_time;
        xlator_t *this;
        int      child;
} shd_dump_t;

typedef struct shd_event_ {
        int     child;
        char    *path;
} shd_event_t;

typedef struct shd_pos_ {
        int     child;
        xlator_t *this;
        afr_child_pos_t pos;
} shd_pos_t;

typedef int
(*afr_crawl_done_cbk_t)  (int ret, call_frame_t *sync_frame, void *crawl_data);

void
afr_start_crawl (xlator_t *this, int idx, afr_crawl_type_t crawl,
                 process_entry_cbk_t process_entry, void *op_data,
                 gf_boolean_t exclusive, int crawl_flags,
                 afr_crawl_done_cbk_t crawl_done);

static int
_crawl_directory (fd_t *fd, loc_t *loc, afr_crawl_data_t *crawl_data);

int
afr_syncop_find_child_position (void *data);

void
shd_cleanup_event (void *event)
{
        shd_event_t *shd_event = event;

        if (!shd_event)
                goto out;
        if (shd_event->path)
                GF_FREE (shd_event->path);
        GF_FREE (shd_event);
out:
        return;
}

int
afr_get_local_child (afr_self_heald_t *shd, unsigned int child_count)
{
        int i = 0;
        int ret = -1;
        for (i = 0; i < child_count; i++) {
                if (shd->pos[i] == AFR_POS_LOCAL) {
                        ret = i;
                        break;
                }
        }
        return ret;
}

static int
_build_index_loc (xlator_t *this, loc_t *loc, char *name, loc_t *parent)
{
        int             ret = 0;

        uuid_copy (loc->pargfid, parent->inode->gfid);
        loc->path = "";
        loc->name = name;
        loc->parent = inode_ref (parent->inode);
        if (!loc->parent) {
                loc->path = NULL;
                loc_wipe (loc);
                ret = -1;
        }
        return ret;
}

int
_add_str_to_dict (xlator_t *this, dict_t *output, int child, char *str,
                  gf_boolean_t dyn)
{
        //subkey not used for now
        int             ret = -1;
        uint64_t        count = 0;
        char            key[256] = {0};
        int             xl_id = 0;

        ret = dict_get_int32 (output, this->name, &xl_id);
        if (ret) {
                gf_log (this->name, GF_LOG_ERROR, "xl does not have id");
                goto out;
        }

        snprintf (key, sizeof (key), "%d-%d-count", xl_id, child);
        ret = dict_get_uint64 (output, key, &count);

        snprintf (key, sizeof (key), "%d-%d-%"PRIu64, xl_id, child, count);
        if (dyn)
                ret = dict_set_dynstr (output, key, str);
        else
                ret = dict_set_str (output, key, str);
        if (ret) {
                gf_log (this->name, GF_LOG_ERROR, "%s: Could not add to output",
                        str);
                goto out;
        }

        snprintf (key, sizeof (key), "%d-%d-count", xl_id, child);
        ret = dict_set_uint64 (output, key, count + 1);
        if (ret) {
                gf_log (this->name, GF_LOG_ERROR, "Could not increment count");
                goto out;
        }
        ret = 0;
out:
        return ret;
}

int
_get_path_from_gfid_loc (xlator_t *this, xlator_t *readdir_xl, loc_t *child,
                         char **fpath)
{
        dict_t          *xattr = NULL;
        char            *path = NULL;
        int             ret = -1;

        ret = syncop_getxattr (readdir_xl, child, &xattr,
                               GFID_TO_PATH_KEY);
        if (ret)
                goto out;
        ret = dict_get_str (xattr, GFID_TO_PATH_KEY, &path);
        if (ret) {
                gf_log (this->name, GF_LOG_DEBUG, "Failed to get path for "
                        "gfid %s", uuid_utoa (child->gfid));
                goto out;
        }
        path = gf_strdup (path);
        if (!path) {
                ret = -1;
                goto out;
        }
        ret = 0;
out:
        if (!ret)
                *fpath = path;
        if (xattr)
                dict_unref (xattr);
        return ret;
}

int
_add_event_to_dict (circular_buffer_t *cb, void *data)
{
        int               ret = 0;
        shd_dump_t        *dump_data = NULL;
        shd_event_t       *shd_event = NULL;

        dump_data = data;
        shd_event = cb->data;
        if (shd_event->child != dump_data->child)
                goto out;
        if (cb->tv.tv_sec >= dump_data->sh_time)
                ret = _add_str_to_dict (dump_data->this, dump_data->dict,
                                        dump_data->child, shd_event->path,
                                        _gf_false);
out:
        return ret;
}

int
_add_eh_to_dict (xlator_t *this, eh_t *eh, dict_t *dict, time_t sh_time,
                 int child)
{
        shd_dump_t dump_data = {0};

        dump_data.this = this;
        dump_data.dict = dict;
        dump_data.sh_time = sh_time;
        dump_data.child = child;
        eh_dump (eh, &dump_data, _add_event_to_dict);
        return 0;
}

int
_add_summary_to_dict (xlator_t *this, afr_crawl_data_t *crawl_data,
                      gf_dirent_t *entry,
                      loc_t *childloc, loc_t *parentloc, struct iatt *iattr)
{
        dict_t          *output = NULL;
        xlator_t        *readdir_xl = NULL;
        int             ret = -1;
        char            *path = NULL;

        if (uuid_is_null (childloc->gfid))
                goto out;

        output = crawl_data->op_data;
        readdir_xl = crawl_data->readdir_xl;

        ret = _get_path_from_gfid_loc (this, readdir_xl, childloc, &path);
        if (ret)
                goto out;

        ret = _add_str_to_dict (this, output, crawl_data->child, path,
                                _gf_true);
out:
        if (ret && path)
                GF_FREE (path);
        return ret;
}

void
_remove_stale_index (xlator_t *this, xlator_t *readdir_xl,
                     loc_t *parent, char *fname)
{
        int              ret = 0;
        loc_t            index_loc = {0};

        ret = _build_index_loc (this, &index_loc, fname, parent);
        if (ret)
                goto out;
        gf_log (this->name, GF_LOG_INFO, "Removing stale index "
                "for %s on %s", index_loc.name, readdir_xl->name);
        ret = syncop_unlink (readdir_xl, &index_loc);
        if (ret) {
                gf_log (this->name, GF_LOG_ERROR, "%s: Failed to remove"
                        " index on %s - %s", index_loc.name,
                        readdir_xl->name, strerror (errno));
        }
        index_loc.path = NULL;
        loc_wipe (&index_loc);
out:
        return;
}

void
_crawl_post_sh_action (xlator_t *this, loc_t *parent, loc_t *child,
                       int32_t op_ret, int32_t op_errno,
                       afr_crawl_data_t *crawl_data)
{
        int              ret = 0;
        afr_private_t    *priv = NULL;
        afr_self_heald_t *shd = NULL;
        eh_t             *eh = NULL;
        char             *path = NULL;
        shd_event_t      *event = NULL;

        priv = this->private;
        shd  = &priv->shd;
        if (crawl_data->crawl == INDEX) {
                if ((op_ret < 0) && (op_errno == ENOENT)) {
                        _remove_stale_index (this, crawl_data->readdir_xl,
                                             parent, uuid_utoa (child->gfid));
                        goto out;
                }
                ret = _get_path_from_gfid_loc (this, crawl_data->readdir_xl,
                                               child, &path);
                if (ret)
                        goto out;
        } else {
                path = gf_strdup (child->path);
                if (!path) {
                        ret = -1;
                        goto out;
                }
        }

        if (op_ret < 0 && op_errno == EIO)
                eh = shd->split_brain;
        else if (op_ret < 0)
                eh = shd->heal_failed;
        else
                eh = shd->healed;
        ret = -1;
        event = GF_CALLOC (1, sizeof (*event), gf_afr_mt_shd_event_t);
        if (!event)
                goto out;
        event->child = crawl_data->child;
        event->path = path;
        ret = eh_save_history (eh, event);
        if (ret < 0) {
                gf_log (this->name, GF_LOG_ERROR, "%s:Failed to save to "
                        "eh, (%d, %s)", path, op_ret, strerror (op_errno));
                goto out;
        }
        ret = 0;
out:
        if (ret && path)
                GF_FREE (path);
        return;
}

int
_self_heal_entry (xlator_t *this, afr_crawl_data_t *crawl_data, gf_dirent_t *entry,
                  loc_t *child, loc_t *parent, struct iatt *iattr)
{
        struct iatt      parentbuf = {0};
        int              ret = 0;

        if (uuid_is_null (child->gfid))
                gf_log (this->name, GF_LOG_DEBUG, "lookup %s", child->path);
        else
                gf_log (this->name, GF_LOG_DEBUG, "lookup %s",
                        uuid_utoa (child->gfid));

        ret = syncop_lookup (this, child, NULL,
                             iattr, NULL, &parentbuf);
        _crawl_post_sh_action (this, parent, child, ret, errno, crawl_data);
        return ret;
}

static int
afr_crawl_done  (int ret, call_frame_t *sync_frame, void *data)
{
        GF_FREE (data);
        STACK_DESTROY (sync_frame->root);
        return 0;
}

void
_do_self_heal_on_subvol (xlator_t *this, int child, afr_crawl_type_t crawl)
{
        afr_private_t   *priv = NULL;
        afr_self_heald_t *shd = NULL;

        priv = this->private;
        shd = &priv->shd;

        time (&shd->sh_times[child]);
        afr_start_crawl (this, child, crawl, _self_heal_entry,
                         NULL, _gf_true, STOP_CRAWL_ON_SINGLE_SUBVOL,
                         afr_crawl_done);
}

gf_boolean_t
_crawl_proceed (xlator_t *this, int child, int crawl_flags, char **reason)
{
        afr_private_t           *priv = NULL;
        afr_self_heald_t        *shd = NULL;
        gf_boolean_t            proceed = _gf_false;
        char                    *msg = NULL;

        priv = this->private;
        shd  = &priv->shd;
        if (!shd->enabled) {
                msg = "Self-heal daemon is not enabled";
                gf_log (this->name, GF_LOG_ERROR, msg);
                goto out;
        }
        if (!priv->child_up[child]) {
                gf_log (this->name, GF_LOG_ERROR, "Stopping crawl for %s , "
                        "subvol went down", priv->children[child]->name);
                msg = "Brick is Not connected";
                goto out;
        }

        if (crawl_flags & STOP_CRAWL_ON_SINGLE_SUBVOL) {
                if (afr_up_children_count (priv->child_up,
                                           priv->child_count) < 2) {
                        gf_log (this->name, GF_LOG_ERROR, "Stopping crawl as "
                                "< 2 children are up");
                        msg = "< 2 bricks in replica are running";
                        goto out;
                }
        }
        proceed = _gf_true;
out:
        if (reason)
                *reason = msg;
        return proceed;
}

int
_do_crawl_op_on_local_subvols (xlator_t *this, afr_crawl_type_t crawl,
                               shd_crawl_op op, dict_t *output)
{
        afr_private_t       *priv = NULL;
        char                *status = NULL;
        char                *subkey = NULL;
        char                key[256] = {0};
        shd_pos_t           pos_data = {0};
        int                 op_ret = -1;
        int                 xl_id = -1;
        int                 i = 0;
        int                 ret = 0;
        int                 crawl_flags = 0;

        priv = this->private;
        if (op == HEAL)
                crawl_flags |= STOP_CRAWL_ON_SINGLE_SUBVOL;

        ret = dict_get_int32 (output, this->name, &xl_id);
        if (ret) {
                gf_log (this->name, GF_LOG_ERROR, "Invalid input, "
                        "translator-id is not available");
                goto out;
        }
        pos_data.this = this;
        subkey = "status";
        for (i = 0; i < priv->child_count; i++) {
                if (_crawl_proceed (this, i, crawl_flags, &status)) {
                        pos_data.child = i;
                        ret = synctask_new (this->ctx->env,
                                            afr_syncop_find_child_position,
                                            NULL, NULL, &pos_data);
                        if (ret) {
                                status = "Not able to find brick location";
                        } else if (pos_data.pos == AFR_POS_REMOTE) {
                                status = "brick is remote";
                        } else {
                                op_ret = 0;
                                if (op == HEAL) {
                                        status = "Started self-heal";
                                        _do_self_heal_on_subvol (this, i,
                                                                 crawl);
                                } else {
                                        status = "";
                                        afr_start_crawl (this, i, INDEX,
                                                         _add_summary_to_dict,
                                                         output, _gf_false, 0,
                                                         NULL);
                                }
                        }
                        snprintf (key, sizeof (key), "%d-%d-%s", xl_id,
                                  i, subkey);
                        ret = dict_set_str (output, key, status);
                        if (!op_ret && (crawl == FULL))
                                break;
                }
                snprintf (key, sizeof (key), "%d-%d-%s", xl_id, i, subkey);
                ret = dict_set_str (output, key, status);
        }
out:
        return op_ret;
}

int
_do_self_heal_on_local_subvols (xlator_t *this, afr_crawl_type_t crawl,
                                dict_t *output)
{
        return _do_crawl_op_on_local_subvols (this, crawl, HEAL, output);
}

int
_get_index_summary_on_local_subvols (xlator_t *this, dict_t *output)
{
        return _do_crawl_op_on_local_subvols (this, INDEX, INFO, output);
}

int
_add_all_subvols_eh_to_dict (xlator_t *this, eh_t *eh, dict_t *dict)
{
        afr_private_t           *priv = NULL;
        afr_self_heald_t        *shd = NULL;
        int                     i = 0;

        priv = this->private;
        shd = &priv->shd;

        for (i = 0; i < priv->child_count; i++) {
                if (shd->pos[i] != AFR_POS_LOCAL)
                        continue;
                _add_eh_to_dict (this, eh, dict, shd->sh_times[i], i);
        }
        return 0;
}

int
afr_xl_op (xlator_t *this, dict_t *input, dict_t *output)
{
        gf_xl_afr_op_t   op = GF_AFR_OP_INVALID;
        int              ret = 0;
        afr_private_t    *priv = NULL;
        afr_self_heald_t *shd = NULL;
        int              xl_id = 0;

        priv = this->private;
        shd = &priv->shd;

        ret = dict_get_int32 (input, "xl-op", (int32_t*)&op);
        if (ret)
                goto out;
        ret = dict_get_int32 (input, this->name, &xl_id);
        if (ret)
                goto out;
        ret = dict_set_int32 (output, this->name, xl_id);
        if (ret)
                goto out;
        switch (op) {
        case GF_AFR_OP_HEAL_INDEX:
                ret = _do_self_heal_on_local_subvols (this, INDEX, output);
                break;
        case GF_AFR_OP_HEAL_FULL:
                ret = _do_self_heal_on_local_subvols (this, FULL, output);
                break;
        case GF_AFR_OP_INDEX_SUMMARY:
                ret = _get_index_summary_on_local_subvols (this, output);
                break;
        case GF_AFR_OP_HEALED_FILES:
                ret = _add_all_subvols_eh_to_dict (this, shd->healed, output);
                break;
        case GF_AFR_OP_HEAL_FAILED_FILES:
                ret = _add_all_subvols_eh_to_dict (this, shd->heal_failed,
                                                   output);
                break;
        case GF_AFR_OP_SPLIT_BRAIN_FILES:
                ret = _add_all_subvols_eh_to_dict (this, shd->split_brain,
                                                   output);
                break;
        default:
                gf_log (this->name, GF_LOG_ERROR, "Unknown set op %d", op);
                break;
        }
out:
        dict_del (output, this->name);
        return ret;
}

void
afr_poll_self_heal (void *data)
{
        afr_private_t    *priv = NULL;
        afr_self_heald_t *shd = NULL;
        struct timeval   timeout = {0};
        xlator_t         *this = NULL;
        long             child = (long)data;
        gf_timer_t       *old_timer = NULL;
        gf_timer_t       *new_timer = NULL;

        this = THIS;
        priv = this->private;
        shd = &priv->shd;

        _do_self_heal_on_subvol (this, child, INDEX);
        timeout.tv_sec = AFR_POLL_TIMEOUT;
        timeout.tv_usec = 0;
        //notify and previous timer should be synchronized.
        LOCK (&priv->lock);
        {
                old_timer = shd->timer[child];
                shd->timer[child] = gf_timer_call_after (this->ctx, timeout,
                                                         afr_poll_self_heal,
                                                         data);
                new_timer = shd->timer[child];
        }
        UNLOCK (&priv->lock);

        if (old_timer)
                gf_timer_call_cancel (this->ctx, old_timer);
        if (!new_timer) {
                gf_log (this->name, GF_LOG_WARNING,
                        "Could not create self-heal polling timer for %s",
                        priv->children[child]->name);
        }
        return;
}

static int
afr_local_child_poll_self_heal  (int ret, call_frame_t *sync_frame, void *data)
{
        afr_self_heald_t *shd = NULL;
        shd_pos_t        *pos_data = data;
        afr_private_t    *priv = NULL;

        if (ret)
                goto out;

        priv = pos_data->this->private;
        shd = &priv->shd;
        shd->pos[pos_data->child] = pos_data->pos;
        if (pos_data->pos == AFR_POS_LOCAL)
                afr_poll_self_heal ((void*)(long)pos_data->child);
out:
        GF_FREE (data);
        return 0;
}

void
afr_proactive_self_heal (void *data)
{
        xlator_t         *this = NULL;
        long             child = (long)data;
        shd_pos_t        *pos_data = NULL;
        int              ret = 0;

        this = THIS;

        //Position of brick could have changed and it could be local now.
        //Compute the position again
        pos_data = GF_CALLOC (1, sizeof (*pos_data), gf_afr_mt_pos_data_t);
        if (!pos_data)
                goto out;
        pos_data->this = this;
        pos_data->child = child;
        ret = synctask_new (this->ctx->env, afr_syncop_find_child_position,
                            afr_local_child_poll_self_heal, NULL, pos_data);
        if (ret)
                goto out;
out:
        return;
}

static int
get_pathinfo_host (char *pathinfo, char *hostname, size_t size)
{
        char    *start = NULL;
        char    *end = NULL;
        int     ret  = -1;
        int     i    = 0;

        if (!pathinfo)
                goto out;

        start = strchr (pathinfo, ':');
        if (!start)
                goto out;
        end = strrchr (pathinfo, ':');
        if (start == end)
                goto out;

        memset (hostname, 0, size);
        i = 0;
        while (++start != end)
                hostname[i++] = *start;
        ret = 0;
out:
        return ret;
}

int
afr_local_pathinfo (char *pathinfo, gf_boolean_t *local)
{
        int             ret   = 0;
        char            pathinfohost[1024] = {0};
        char            localhost[1024] = {0};
        xlator_t        *this = THIS;

        *local = _gf_false;
        ret = get_pathinfo_host (pathinfo, pathinfohost, sizeof (pathinfohost));
        if (ret) {
                gf_log (this->name, GF_LOG_ERROR, "Invalid pathinfo: %s",
                        pathinfo);
                goto out;
        }

        ret = gethostname (localhost, sizeof (localhost));
        if (ret) {
                gf_log (this->name, GF_LOG_ERROR, "gethostname() failed, "
                        "reason: %s", strerror (errno));
                goto out;
        }

        if (!strcmp (localhost, pathinfohost))
                *local = _gf_true;
out:
        return ret;
}


int
afr_crawl_build_start_loc (xlator_t *this, afr_crawl_data_t *crawl_data,
                           loc_t *dirloc)
{
        afr_private_t *priv = NULL;
        dict_t        *xattr = NULL;
        void          *index_gfid = NULL;
        loc_t         rootloc = {0};
        struct iatt   iattr = {0};
        struct iatt   parent = {0};
        int           ret = 0;
        xlator_t      *readdir_xl = crawl_data->readdir_xl;

        priv = this->private;
        if (crawl_data->crawl == FULL) {
                afr_build_root_loc (this, dirloc);
        } else {
                afr_build_root_loc (this, &rootloc);
                ret = syncop_getxattr (readdir_xl, &rootloc, &xattr,
                                       GF_XATTROP_INDEX_GFID);
                if (ret < 0)
                        goto out;
                ret = dict_get_ptr (xattr, GF_XATTROP_INDEX_GFID, &index_gfid);
                if (ret < 0) {
                        gf_log (this->name, GF_LOG_ERROR, "failed to get index "
                                "dir gfid on %s", readdir_xl->name);
                        goto out;
                }
                if (!index_gfid) {
                        gf_log (this->name, GF_LOG_ERROR, "index gfid empty "
                                "on %s", readdir_xl->name);
                        ret = -1;
                        goto out;
                }
                uuid_copy (dirloc->gfid, index_gfid);
                dirloc->path = "";
                dirloc->inode = inode_new (priv->root_inode->table);
                ret = syncop_lookup (readdir_xl, dirloc, NULL,
                                     &iattr, NULL, &parent);
                if (ret < 0) {
                        gf_log (this->name, GF_LOG_ERROR, "lookup failed on "
                                "index dir on %s", readdir_xl->name);
                        goto out;
                }
                inode_link (dirloc->inode, NULL, NULL, &iattr);
        }
        ret = 0;
out:
        if (xattr)
                dict_unref (xattr);
        loc_wipe (&rootloc);
        return ret;
}

int
afr_crawl_opendir (xlator_t *this, afr_crawl_data_t *crawl_data, fd_t **dirfd,
                   loc_t *dirloc)
{
        fd_t          *fd   = NULL;
        int           ret = 0;

        if (crawl_data->crawl == FULL) {
                fd = fd_create (dirloc->inode, crawl_data->pid);
                if (!fd) {
                        gf_log (this->name, GF_LOG_ERROR,
                                "Failed to create fd for %s", dirloc->path);
                        ret = -1;
                        goto out;
                }

                ret = syncop_opendir (crawl_data->readdir_xl, dirloc, fd);
                if (ret < 0) {
                        gf_log (this->name, GF_LOG_ERROR,
                                "opendir failed on %s", dirloc->path);
                        goto out;
                }
        } else {
                fd = fd_anonymous (dirloc->inode);
        }
        ret = 0;
out:
        if (!ret)
                *dirfd = fd;
        return ret;
}

xlator_t*
afr_crawl_readdir_xl_get (xlator_t *this, afr_crawl_data_t *crawl_data)
{
        afr_private_t *priv = this->private;

        if (crawl_data->crawl == FULL) {
                return this;
        } else {
                return priv->children[crawl_data->child];
        }
        return NULL;
}

int
afr_crawl_build_child_loc (xlator_t *this, loc_t *child, loc_t *parent,
                           gf_dirent_t *entry, afr_crawl_data_t *crawl_data)
{
        int           ret = 0;
        afr_private_t *priv = NULL;

        priv = this->private;
        if (crawl_data->crawl == FULL) {
                ret = afr_build_child_loc (this, child, parent, entry->d_name);
        } else {
                child->path = "";
                child->inode = inode_new (priv->root_inode->table);
                uuid_parse (entry->d_name, child->gfid);
        }
        return ret;
}

static int
_process_entries (xlator_t *this, loc_t *parentloc, gf_dirent_t *entries,
                  off_t *offset, afr_crawl_data_t *crawl_data)
{
        gf_dirent_t      *entry = NULL;
        gf_dirent_t      *tmp = NULL;
        int              ret = 0;
        loc_t            entry_loc = {0};
        fd_t             *fd = NULL;
        struct iatt      iattr = {0};
        inode_t          *link_inode = NULL;

        list_for_each_entry_safe (entry, tmp, &entries->list, list) {
                if (!_crawl_proceed (this, crawl_data->child,
                                     crawl_data->crawl_flags, NULL)) {
                        ret = -1;
                        goto out;
                }
                *offset = entry->d_off;
                if (IS_ENTRY_CWD (entry->d_name) ||
                    IS_ENTRY_PARENT (entry->d_name))
                        continue;
                if ((crawl_data->crawl == FULL) &&
                     uuid_is_null (entry->d_stat.ia_gfid)) {
                        gf_log (this->name, GF_LOG_WARNING, "%s/%s: No "
                                "gfid present skipping",
                                parentloc->path, entry->d_name);
                        continue;
                }

                if (crawl_data->crawl == INDEX)
                        entry_loc.path = NULL;//HACK
                loc_wipe (&entry_loc);
                ret = afr_crawl_build_child_loc (this, &entry_loc, parentloc,
                                                 entry, crawl_data);
                if (ret)
                        goto out;

                ret = crawl_data->process_entry (this, crawl_data, entry,
                                                 &entry_loc, parentloc, &iattr);

                if (crawl_data->crawl == INDEX)
                        continue;

                if (ret || !IA_ISDIR (iattr.ia_type))
                        continue;

                link_inode = inode_link (entry_loc.inode, NULL, NULL, &iattr);
                if (link_inode == NULL) {
                        char uuidbuf[64];
                        gf_log (this->name, GF_LOG_ERROR, "inode link failed "
                                "on the inode (%s)",
                                uuid_utoa_r (entry_loc.gfid, uuidbuf));
                        ret = -1;
                        goto out;
                }

                fd = NULL;
                ret = afr_crawl_opendir (this, crawl_data, &fd, &entry_loc);
                if (ret)
                        continue;
                ret = _crawl_directory (fd, &entry_loc, crawl_data);
                if (fd)
                        fd_unref (fd);
        }
        ret = 0;
out:
        if (crawl_data->crawl == INDEX)
                entry_loc.path = NULL;
        if (entry_loc.path)
                loc_wipe (&entry_loc);
        return ret;
}

static int
_crawl_directory (fd_t *fd, loc_t *loc, afr_crawl_data_t *crawl_data)
{
        xlator_t        *this = NULL;
        off_t           offset   = 0;
        gf_dirent_t     entries;
        int             ret = 0;
        gf_boolean_t    free_entries = _gf_false;
        xlator_t        *readdir_xl = crawl_data->readdir_xl;

        INIT_LIST_HEAD (&entries.list);
        this = THIS;

        GF_ASSERT (loc->inode);

        if (loc->path)
                gf_log (this->name, GF_LOG_DEBUG, "crawling %s", loc->path);
        else
                gf_log (this->name, GF_LOG_DEBUG, "crawling %s",
                        uuid_utoa (loc->gfid));

        while (1) {
                if (crawl_data->crawl == FULL)
                        ret = syncop_readdirp (readdir_xl, fd, 131072, offset,
                                               NULL, &entries);
                else
                        ret = syncop_readdir (readdir_xl, fd, 131072, offset,
                                              &entries);
                if (ret <= 0)
                        break;
                ret = 0;
                free_entries = _gf_true;

                if (!_crawl_proceed (this, crawl_data->child,
                                     crawl_data->crawl_flags, NULL)) {
                        ret = -1;
                        goto out;
                }
                if (list_empty (&entries.list))
                        goto out;

                ret = _process_entries (this, loc, &entries, &offset,
                                        crawl_data);
                gf_dirent_free (&entries);
                free_entries = _gf_false;
        }
        ret = 0;
out:
        if (free_entries)
                gf_dirent_free (&entries);
        return ret;
}

static char*
position_str_get (afr_child_pos_t pos)
{
        switch (pos) {
        case AFR_POS_UNKNOWN:
                return "unknown";
        case AFR_POS_LOCAL:
                return "local";
        case AFR_POS_REMOTE:
                return "remote";
        }
        return NULL;
}

int
afr_find_child_position (xlator_t *this, int child, afr_child_pos_t *pos)
{
        afr_private_t    *priv = NULL;
        dict_t           *xattr_rsp = NULL;
        loc_t            loc = {0};
        int              ret = 0;
        gf_boolean_t     local = _gf_false;
        char             *pathinfo = NULL;

        priv = this->private;

        afr_build_root_loc (this, &loc);

        ret = syncop_getxattr (priv->children[child], &loc, &xattr_rsp,
                               GF_XATTR_PATHINFO_KEY);
        if (ret) {
                gf_log (this->name, GF_LOG_ERROR, "getxattr failed on %s",
                        priv->children[child]->name);
                goto out;
        }

        ret = dict_get_str (xattr_rsp, GF_XATTR_PATHINFO_KEY, &pathinfo);
        if (ret) {
                gf_log (this->name, GF_LOG_ERROR, "Pathinfo key not found on "
                        "child %d", child);
                goto out;
        }

        ret = afr_local_pathinfo (pathinfo, &local);
        if (ret)
                goto out;
        if (local)
                *pos = AFR_POS_LOCAL;
        else
                *pos = AFR_POS_REMOTE;

        gf_log (this->name, GF_LOG_INFO, "child %s is %s",
                priv->children[child]->name, position_str_get (*pos));
out:
        if (ret)
                *pos = AFR_POS_UNKNOWN;
        loc_wipe (&loc);
        return ret;
}

int
afr_syncop_find_child_position (void *data)
{
        shd_pos_t *pos_data = data;
        int       ret = 0;

        ret = afr_find_child_position (pos_data->this, pos_data->child,
                                       &pos_data->pos);
        return ret;
}

static int
afr_dir_crawl (void *data)
{
        xlator_t            *this = NULL;
        int                 ret = -1;
        xlator_t            *readdir_xl = NULL;
        fd_t                *fd = NULL;
        loc_t               dirloc = {0};
        afr_crawl_data_t    *crawl_data = data;

        this = THIS;

        if (!_crawl_proceed (this, crawl_data->child, crawl_data->crawl_flags,
                             NULL))
                goto out;

        readdir_xl = afr_crawl_readdir_xl_get (this, crawl_data);
        if (!readdir_xl)
                goto out;
        crawl_data->readdir_xl = readdir_xl;

        ret = afr_crawl_build_start_loc (this, crawl_data, &dirloc);
        if (ret)
                goto out;

        ret = afr_crawl_opendir (this, crawl_data, &fd, &dirloc);
        if (ret)
                goto out;

        ret = _crawl_directory (fd, &dirloc, crawl_data);
        if (ret)
                gf_log (this->name, GF_LOG_ERROR, "Crawl failed on %s",
                        readdir_xl->name);
        else
                gf_log (this->name, GF_LOG_INFO, "Crawl completed "
                        "on %s", readdir_xl->name);
        if (crawl_data->crawl == INDEX)
                dirloc.path = NULL;
out:
        if (fd)
                fd_unref (fd);
        if (crawl_data->crawl == INDEX)
                dirloc.path = NULL;
        loc_wipe (&dirloc);
        return ret;
}

static int
afr_dir_exclusive_crawl (void *data)
{
        afr_private_t    *priv = NULL;
        afr_self_heald_t *shd = NULL;
        gf_boolean_t     crawl = _gf_false;
        int              ret = 0;
        int              child = -1;
        xlator_t         *this = NULL;
        afr_crawl_data_t *crawl_data = data;

        this = THIS;
        priv = this->private;
        shd = &priv->shd;
        child = crawl_data->child;

        LOCK (&priv->lock);
        {
                if (shd->inprogress[child]) {
                        if (shd->pending[child] != FULL)
                                shd->pending[child] = crawl_data->crawl;
                } else {
                        shd->inprogress[child] = _gf_true;
                        crawl = _gf_true;
                }
        }
        UNLOCK (&priv->lock);

        if (!crawl) {
                gf_log (this->name, GF_LOG_INFO, "Another crawl is in progress "
                        "for %s", priv->children[child]->name);
                goto out;
        }

        do {
                afr_dir_crawl (data);
                LOCK (&priv->lock);
                {
                        if (shd->pending[child] != NONE) {
                                crawl_data->crawl = shd->pending[child];
                                shd->pending[child] = NONE;
                        } else {
                                shd->inprogress[child] = _gf_false;
                                crawl = _gf_false;
                        }
                }
                UNLOCK (&priv->lock);
        } while (crawl);
out:
        return ret;
}

void
afr_start_crawl (xlator_t *this, int idx, afr_crawl_type_t crawl,
                 process_entry_cbk_t process_entry, void *op_data,
                 gf_boolean_t exclusive, int crawl_flags,
                 afr_crawl_done_cbk_t crawl_done)
{
        afr_private_t              *priv = NULL;
        call_frame_t               *frame = NULL;
        afr_crawl_data_t           *crawl_data = NULL;
        int                        ret = 0;
        int (*crawler) (void*) = NULL;

        priv = this->private;

        frame = create_frame (this, this->ctx->pool);
        if (!frame)
                goto out;

        afr_set_lk_owner (frame, this, frame->root);
        afr_set_low_priority (frame);
        crawl_data = GF_CALLOC (1, sizeof (*crawl_data),
                                gf_afr_mt_crawl_data_t);
        if (!crawl_data)
                goto out;
        crawl_data->process_entry = process_entry;
        crawl_data->child = idx;
        crawl_data->pid = frame->root->pid;
        crawl_data->crawl = crawl;
        crawl_data->op_data = op_data;
        crawl_data->crawl_flags = crawl_flags;
        gf_log (this->name, GF_LOG_INFO, "starting crawl %d for %s",
                crawl_data->crawl, priv->children[idx]->name);

        if (exclusive)
                crawler = afr_dir_exclusive_crawl;
        else
                crawler = afr_dir_crawl;
        ret = synctask_new (this->ctx->env, crawler,
                            crawl_done, frame, crawl_data);
        if (ret)
                gf_log (this->name, GF_LOG_ERROR, "Could not create the "
                        "task for %d ret %d", idx, ret);
out:
        return;
}

void
afr_build_root_loc (xlator_t *this, loc_t *loc)
{
        afr_private_t   *priv = NULL;

        priv = this->private;
        loc->path = gf_strdup ("/");
        loc->name = "";
        loc->inode = inode_ref (priv->root_inode);
        uuid_copy (loc->gfid, loc->inode->gfid);
}

int
afr_set_root_gfid (dict_t *dict)
{
        uuid_t gfid;
        int ret = 0;

        memset (gfid, 0, 16);
        gfid[15] = 1;

        ret = afr_set_dict_gfid (dict, gfid);

        return ret;
}

