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

static int
_crawl_directory (loc_t *loc, pid_t pid);
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

inline void
afr_fill_loc_info (loc_t *loc, struct iatt *iatt, struct iatt *parent)
{
        afr_update_loc_gfids (loc, iatt, parent);
        uuid_copy (loc->inode->gfid, iatt->ia_gfid);
}

static int
_perform_self_heal (xlator_t *this, loc_t *parentloc, gf_dirent_t *entries,
                    off_t *offset, pid_t pid)
{
        gf_dirent_t      *entry = NULL;
        gf_dirent_t      *tmp = NULL;
        struct iatt      iatt = {0};
        struct iatt      parent = {0};;
        int              ret = 0;
        loc_t            entry_loc = {0};

        list_for_each_entry_safe (entry, tmp, &entries->list, list) {
                *offset = entry->d_off;
                if (IS_ENTRY_CWD (entry->d_name) ||
                    IS_ENTRY_PARENT (entry->d_name))
                        continue;
                if (uuid_is_null (entry->d_stat.ia_gfid)) {
                        gf_log (this->name, GF_LOG_WARNING, "%s/%s: No "
                                "gfid present skipping",
                                parentloc->path, entry->d_name);
                        continue;
                }

                loc_wipe (&entry_loc);
                ret = afr_build_child_loc (this, &entry_loc, parentloc,
                                           entry->d_name, entry->d_stat.ia_gfid);
                if (ret)
                        goto out;

                gf_log (this->name, GF_LOG_DEBUG, "lookup %s", entry_loc.path);

                ret = syncop_lookup (this, &entry_loc, NULL,
                                     &iatt, NULL, &parent);
                //Don't fail the crawl if lookup fails as it
                //could be because of split-brain
                if (ret || (!IA_ISDIR (iatt.ia_type)))
                        continue;
                afr_fill_loc_info (&entry_loc, &iatt, &parent);
                ret = _crawl_directory (&entry_loc, pid);
        }
        ret = 0;
out:
        if (entry_loc.path)
                loc_wipe (&entry_loc);
        return ret;
}

static int
_crawl_directory (loc_t *loc, pid_t pid)
{
        xlator_t        *this = NULL;
        afr_private_t   *priv = NULL;
        fd_t            *fd   = NULL;
        off_t           offset   = 0;
        gf_dirent_t     entries;
        struct iatt     iatt = {0};
        struct iatt     parent = {0};;
        int             ret = 0;
        gf_boolean_t    free_entries = _gf_false;

        INIT_LIST_HEAD (&entries.list);
        this = THIS;
        priv = this->private;

        GF_ASSERT (loc->inode);

        gf_log (this->name, GF_LOG_DEBUG, "crawling %s", loc->path);
        fd = fd_create (loc->inode, pid);
        if (!fd) {
                gf_log (this->name, GF_LOG_ERROR,
                        "Failed to create fd for %s", loc->path);
                goto out;
        }

        if (!loc->parent) {
                ret = syncop_lookup (this, loc, NULL,
                                     &iatt, NULL, &parent);
        }

        ret = syncop_opendir (this, loc, fd);
        if (ret < 0) {
                gf_log (this->name, GF_LOG_ERROR,
                        "opendir failed on %s", loc->path);
                goto out;
        }

        while (syncop_readdirp (this, fd, 131072, offset, &entries)) {
                ret = 0;
                free_entries = _gf_true;
                if (afr_up_children_count (priv->child_up,
                                           priv->child_count) < 2) {
                        gf_log (this->name, GF_LOG_ERROR, "Stopping crawl as "
                                "< 2 children are up");
                        ret = -1;
                        goto out;
                }

                if (list_empty (&entries.list))
                        goto out;

                ret = _perform_self_heal (this, loc, &entries, &offset, pid);
                gf_dirent_free (&entries);
                free_entries = _gf_false;
        }
        if (fd)
                fd_unref (fd);
        ret = 0;
out:
        if (free_entries)
                gf_dirent_free (&entries);
        return ret;
}

int
afr_find_child_position (xlator_t *this, int child)
{
        afr_private_t    *priv = NULL;
        dict_t           *xattr_rsp = NULL;
        loc_t            loc = {0};
        int              ret = 0;
        gf_boolean_t     local = _gf_false;
        char             *pathinfo = NULL;
        afr_child_pos_t  *pos = NULL;
        inode_table_t    *itable = NULL;

        priv = this->private;
        pos = &priv->shd.pos[child];

        if (*pos != AFR_POS_UNKNOWN) {
                goto out;
        }

        //TODO: Hack to make the root_loc hack work
        LOCK (&priv->lock);
        {
                if (!priv->root_inode) {
                        itable = inode_table_new (0, this);
                        if (!itable)
                                goto unlock;
                        priv->root_inode = inode_new (itable);
                        if (!priv->root_inode)
                                goto unlock;
                }
        }
unlock:
        UNLOCK (&priv->lock);

        if (!priv->root_inode) {
                ret = -1;
                goto out;
        }
        afr_build_root_loc (priv->root_inode, &loc);

        ret = syncop_getxattr (priv->children[child], &loc, &xattr_rsp,
                               GF_XATTR_PATHINFO_KEY);
        if (ret) {
                gf_log (this->name, GF_LOG_ERROR, "getxattr failed on child "
                        "%d", child);
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

        gf_log (this->name, GF_LOG_INFO, "child %d is %d", child, *pos);
out:
        return ret;
}

static int
afr_crawl_done  (int ret, call_frame_t *sync_frame, void *data)
{
        GF_FREE (data);
        STACK_DESTROY (sync_frame->root);
        return 0;
}

static int
afr_find_all_children_postions (xlator_t *this)
{
        int              ret = -1;
        int              i = 0;
        gf_boolean_t     succeeded = _gf_false;
        afr_private_t    *priv = NULL;

        priv = this->private;
        for (i = 0; i < priv->child_count; i++) {
                if (priv->child_up[i] != 1)
                        continue;
                ret = afr_find_child_position (this, i);
                if (ret) {
                        gf_log (this->name, GF_LOG_ERROR,
                                "Failed to determine if the "
                                "child %s is local.",
                                priv->children[i]->name);
                        continue;
                }
                succeeded = _gf_true;
        }
        if (succeeded)
                ret = 0;
        return ret;
}

static gf_boolean_t
afr_local_child_exists (afr_child_pos_t *pos, unsigned int child_count)
{
        int             i = 0;
        gf_boolean_t    local = _gf_false;

        for (i = 0; i < child_count; i++, pos++) {
                if (*pos == AFR_POS_LOCAL) {
                        local = _gf_true;
                        break;
                }
        }
        return local;
}

int
afr_init_child_position (xlator_t *this, int child)
{
        int     ret = 0;

        if (child == AFR_ALL_CHILDREN) {
                ret = afr_find_all_children_postions (this);
        } else {
                ret = afr_find_child_position (this, child);
        }
        return ret;
}

int
afr_is_local_child (afr_self_heald_t *shd, int child, unsigned int child_count)
{
        gf_boolean_t local = _gf_false;

        if (child == AFR_ALL_CHILDREN)
                local = afr_local_child_exists (shd->pos, child_count);
        else
                local = (shd->pos[child] == AFR_POS_LOCAL);

        return local;
}

static int
afr_crawl_directory (xlator_t *this, pid_t pid)
{
        afr_private_t    *priv = NULL;
        afr_self_heald_t *shd = NULL;
        loc_t            loc = {0};
        gf_boolean_t     crawl = _gf_false;
        int             ret = 0;

        priv = this->private;
        shd = &priv->shd;


        LOCK (&priv->lock);
        {
                if (shd->inprogress) {
                        shd->pending = _gf_true;
                } else {
                        shd->inprogress = _gf_true;
                        crawl = _gf_true;
                }
        }
        UNLOCK (&priv->lock);

        if (!priv->root_inode) {
                ret = -1;
                goto out;
        }

        if (!crawl)
                goto out;

        afr_build_root_loc (priv->root_inode, &loc);
        while (crawl) {
                ret = _crawl_directory (&loc, pid);
                if (ret)
                        gf_log (this->name, GF_LOG_ERROR, "Crawl failed");
                else
                        gf_log (this->name, GF_LOG_INFO, "Crawl completed");
                LOCK (&priv->lock);
                {
                        if (shd->pending) {
                                shd->pending = _gf_false;
                        } else {
                                shd->inprogress = _gf_false;
                                crawl = _gf_false;
                        }
                }
                UNLOCK (&priv->lock);
        }
out:
        return ret;
}

static int
afr_crawl (void *data)
{
        xlator_t         *this = NULL;
        afr_private_t    *priv = NULL;
        afr_self_heald_t *shd = NULL;
        int              ret = -1;
        afr_crawl_data_t *crawl_data = data;

        this = THIS;
        priv = this->private;
        shd = &priv->shd;

        ret = afr_init_child_position (this, crawl_data->child);
        if (ret)
                goto out;

        if (!afr_is_local_child (shd, crawl_data->child, priv->child_count))
                goto out;

        ret = afr_crawl_directory (this, crawl_data->pid);
out:
        return ret;
}

void
afr_proactive_self_heal (xlator_t *this, int idx)
{
        afr_private_t              *priv = NULL;
        afr_self_heald_t           *shd = NULL;
        call_frame_t               *frame = NULL;
        afr_crawl_data_t           *crawl_data = NULL;
        int                        ret = 0;

        priv = this->private;
        shd = &priv->shd;
        if (!shd->enabled)
                goto out;

        if ((idx != AFR_ALL_CHILDREN) &&
            (shd->pos[idx] == AFR_POS_REMOTE))
                goto out;

        frame = create_frame (this, this->ctx->pool);
        if (!frame)
                goto out;

        afr_set_lk_owner (frame, this);
        afr_set_low_priority (frame);
        crawl_data = GF_CALLOC (1, sizeof (*crawl_data),
                                gf_afr_mt_afr_crawl_data_t);
        if (!crawl_data)
                goto out;
        crawl_data->child = idx;
        crawl_data->pid = frame->root->pid;
        gf_log (this->name, GF_LOG_INFO, "starting crawl for %d", idx);
        ret = synctask_new (this->ctx->env, afr_crawl,
                            afr_crawl_done, frame, crawl_data);
        if (ret)
                gf_log (this->name, GF_LOG_ERROR, "Could not create the "
                        "task for %d ret %d", idx, ret);
out:
        return;
}

//TODO: This is a hack
void
afr_build_root_loc (inode_t *inode, loc_t *loc)
{
        loc->path = "/";
        loc->name = "";
        loc->inode = inode;
        loc->inode->ia_type = IA_IFDIR;
        memset (loc->inode->gfid, 0, 16);
        loc->inode->gfid[15] = 1;
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

