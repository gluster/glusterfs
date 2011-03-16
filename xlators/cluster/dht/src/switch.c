/*
  Copyright (c) 2010 Gluster, Inc. <http://www.gluster.com>
  This file is part of GlusterFS.

  GlusterFS is free software; you can redistribute it and/or modify
  it under the terms of the GNU Affero General Public License as published
  by the Free Software Foundation; either version 3 of the License,
  or (at your option) any later version.

  GlusterFS is distributed in the hope that it will be useful, but
  WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
  Affero General Public License for more details.

  You should have received a copy of the GNU Affero General Public License
  along with this program.  If not, see
  <http://www.gnu.org/licenses/>.
*/


#ifndef _CONFIG_H
#define _CONFIG_H
#include "config.h"
#endif

#include "dht-common.c"
#include "dht-mem-types.h"

#include <sys/time.h>
#include <stdlib.h>
#include <fnmatch.h>
#include <string.h>

struct switch_sched_array {
        xlator_t *xl;
        int32_t   eligible;
        int32_t   considered;
};

/* Select one of this struct based on the path's pattern match */
struct switch_struct {
        struct switch_struct      *next;
        struct switch_sched_array *array;
        int32_t                    node_index; /* Index of the node in
                                                  this pattern. */
        int32_t                    num_child;  /* Total num of child nodes
                                                  with this pattern. */
        char                       path_pattern[256];
};

/* TODO: all 'TODO's in dht.c holds good */
/* This function should return child node as '*:subvolumes' is inserterd */

static int32_t
gf_switch_valid_child (xlator_t *this, const char *child)
{
        xlator_list_t *children = NULL;
        int32_t        ret = 0;

        children = this->children;
        while (children) {
                if (!strcmp (child, children->xlator->name)) {
                        ret = 1;
                        break;
                }
                children = children->next;
        }

        return ret;
}

static xlator_t *
get_switch_matching_subvol (const char *path, dht_conf_t *conf,
                            xlator_t *hashed_subvol)
{
        struct switch_struct *cond      = NULL;
        struct switch_struct *trav      = NULL;
        char                 *pathname  = NULL;
        int                   idx     = 0;

        cond = conf->private;
        if (!cond)
                return hashed_subvol;

        trav = cond;
        pathname = gf_strdup (path);
        while (trav) {
                if (fnmatch (trav->path_pattern,
                             pathname, FNM_NOESCAPE) == 0) {
                        for (idx = 0; idx < trav->num_child; idx++) {
                                if (trav->array[idx].xl == hashed_subvol)
                                        return hashed_subvol;
                        }
                        idx = trav->node_index++;
                        trav->node_index %= trav->num_child;
                        return trav->array[idx].xl;
                }
                trav = trav->next;
        }
        GF_FREE (pathname);
        return hashed_subvol;
}


int
switch_local_lookup_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                         int op_ret, int op_errno,
                         inode_t *inode, struct iatt *stbuf, dict_t *xattr,
                         struct iatt *postparent)
{
        xlator_t     *subvol      = NULL;
        char          is_linkfile = 0;
        char          is_dir      = 0;
        dht_conf_t   *conf        = NULL;
        dht_local_t  *local       = NULL;
        loc_t        *loc         = NULL;
        int           i           = 0;
        call_frame_t *prev        = NULL;
        int           call_cnt    = 0;
        int           ret         = 0;

        conf  = this->private;

        prev  = cookie;
        local = frame->local;
        loc   = &local->loc;

        if (ENTRY_MISSING (op_ret, op_errno)) {
                if (conf->search_unhashed) {
                        local->op_errno = ENOENT;
                        dht_lookup_everywhere (frame, this, loc);
                        return 0;
                }
        }

        if (op_ret == -1)
                goto out;

        is_linkfile = check_is_linkfile (inode, stbuf, xattr);
        is_dir      = check_is_dir (inode, stbuf, xattr);

        if (!is_dir && !is_linkfile) {
                /* non-directory and not a linkfile */

                dht_itransform (this, prev->this, stbuf->ia_ino,
                                &stbuf->ia_ino);

                ret = dht_layout_preset (this, prev->this, inode);
                if (ret < 0) {
                        gf_log (this->name, GF_LOG_DEBUG,
                                "could not set pre-set layout for subvol %s",
                                prev->this->name);
                        op_ret   = -1;
                        op_errno = EINVAL;
                        goto err;
                }

                goto out;
        }

        if (is_dir) {
                call_cnt        = conf->subvolume_cnt;
                local->call_cnt = call_cnt;

                local->inode = inode_ref (inode);
                local->xattr = dict_ref (xattr);

                local->op_ret = 0;
                local->op_errno = 0;

                local->layout = dht_layout_new (this, conf->subvolume_cnt);
                if (!local->layout) {
                        op_ret   = -1;
                        op_errno = ENOMEM;
                        gf_log (this->name, GF_LOG_DEBUG,
                                "memory allocation failed :(");
                        goto err;
                }

                for (i = 0; i < call_cnt; i++) {
                        STACK_WIND (frame, dht_lookup_dir_cbk,
                                    conf->subvolumes[i],
                                    conf->subvolumes[i]->fops->lookup,
                                    &local->loc, local->xattr_req);
                }
        }

        if (is_linkfile) {
                subvol = dht_linkfile_subvol (this, inode, stbuf, xattr);

                if (!subvol) {
                        gf_log (this->name, GF_LOG_DEBUG,
                                "linkfile not having link subvolume. path=%s",
                                loc->path);
                        dht_lookup_everywhere (frame, this, loc);
                        return 0;
                }

                STACK_WIND (frame, dht_lookup_linkfile_cbk,
                            subvol, subvol->fops->lookup,
                            &local->loc, local->xattr_req);
        }

        return 0;

out:
        if (!local->hashed_subvol) {
                gf_log (this->name, GF_LOG_DEBUG,
                        "no subvolume in layout for path=%s",
                        local->loc.path);
                local->op_errno = ENOENT;
                dht_lookup_everywhere (frame, this, loc);
                return 0;
        }

        STACK_WIND (frame, dht_lookup_cbk,
                    local->hashed_subvol, local->hashed_subvol->fops->lookup,
                    &local->loc, local->xattr_req);

        return 0;

err:
        DHT_STACK_UNWIND (lookup, frame, op_ret, op_errno,
                          inode, stbuf, xattr, NULL);
        return 0;
}

int
switch_lookup (call_frame_t *frame, xlator_t *this,
               loc_t *loc, dict_t *xattr_req)
{
        xlator_t     *hashed_subvol = NULL;
        xlator_t     *cached_subvol = NULL;
        xlator_t     *subvol = NULL;
        dht_local_t  *local  = NULL;
        dht_conf_t   *conf = NULL;
        int           ret    = -1;
        int           op_errno = -1;
        dht_layout_t *layout = NULL;
        int           i = 0;
        int           call_cnt = 0;


        VALIDATE_OR_GOTO (frame, err);
        VALIDATE_OR_GOTO (this, err);
        VALIDATE_OR_GOTO (loc, err);
        VALIDATE_OR_GOTO (loc->inode, err);
        VALIDATE_OR_GOTO (loc->path, err);

        conf = this->private;

        local = dht_local_init (frame);
        if (!local) {
                op_errno = ENOMEM;
                goto err;
        }

        ret = loc_dup (loc, &local->loc);
        if (ret == -1) {
                op_errno = errno;
                gf_log (this->name, GF_LOG_DEBUG,
                        "copying location failed for path=%s",
                        loc->path);
                goto err;
        }

        if (xattr_req) {
                local->xattr_req = dict_ref (xattr_req);
        } else {
                local->xattr_req = dict_new ();
        }

        hashed_subvol = dht_subvol_get_hashed (this, &local->loc);
        cached_subvol = dht_subvol_get_cached (this, local->loc.inode);

        local->cached_subvol = cached_subvol;
        local->hashed_subvol = hashed_subvol;

        if (is_revalidate (loc)) {
                local->layout = layout = dht_layout_get (this, loc->inode);

                if (!layout) {
                        gf_log (this->name, GF_LOG_DEBUG,
                                "revalidate without cache. path=%s",
                                loc->path);
                        op_errno = EINVAL;
                        goto err;
                }

                if (layout->gen && (layout->gen < conf->gen)) {
                        gf_log (this->name, GF_LOG_DEBUG,
                                "incomplete layout failure for path=%s",
                                loc->path);
                        dht_layout_unref (this, local->layout);
                        goto do_fresh_lookup;
                }

                local->inode    = inode_ref (loc->inode);
                local->ia_ino   = loc->inode->ino;

                local->call_cnt = layout->cnt;
                call_cnt = local->call_cnt;

                /* NOTE: we don't require 'trusted.glusterfs.dht.linkto'
                 * attribute, revalidates directly go to the cached-subvolume.
                 */
                ret = dict_set_uint32 (local->xattr_req,
                                       "trusted.glusterfs.dht", 4 * 4);
                if (ret < 0)
                        gf_log (this->name, GF_LOG_WARNING,
                                "failed to set dict value for "
                                "trusted.glusterfs.dht");

                for (i = 0; i < layout->cnt; i++) {
                        subvol = layout->list[i].xlator;

                        STACK_WIND (frame, dht_revalidate_cbk,
                                    subvol, subvol->fops->lookup,
                                    loc, local->xattr_req);

                        if (!--call_cnt)
                                break;
                }
        } else {
        do_fresh_lookup:
                ret = dict_set_uint32 (local->xattr_req,
                                       "trusted.glusterfs.dht", 4 * 4);
                if (ret < 0)
                        gf_log (this->name, GF_LOG_WARNING,
                                "failed to set dict value for "
                                "trusted.glusterfs.dht");

                ret = dict_set_uint32 (local->xattr_req,
                                       "trusted.glusterfs.dht.linkto", 256);
                if (ret < 0)
                        gf_log (this->name, GF_LOG_WARNING,
                                "failed to set dict value for "
                                "trusted.glusterfs.dht.linkto");

                if (!hashed_subvol) {
                        gf_log (this->name, GF_LOG_DEBUG,
                                "no subvolume in layout for path=%s, "
                                "checking on all the subvols to see if "
                                "it is a directory", loc->path);
                        call_cnt        = conf->subvolume_cnt;
                        local->call_cnt = call_cnt;

                        local->layout = dht_layout_new (this,
                                                        conf->subvolume_cnt);
                        if (!local->layout) {
                                op_errno = ENOMEM;
                                goto err;
                        }

                        for (i = 0; i < call_cnt; i++) {
                                STACK_WIND (frame, dht_lookup_dir_cbk,
                                            conf->subvolumes[i],
                                            conf->subvolumes[i]->fops->lookup,
                                            &local->loc, local->xattr_req);
                        }
                        return 0;
                }

                /*  */
                cached_subvol = get_switch_matching_subvol (loc->path, conf,
                                                            hashed_subvol);
                if (cached_subvol == hashed_subvol) {
                        STACK_WIND (frame, dht_lookup_cbk,
                                    hashed_subvol,
                                    hashed_subvol->fops->lookup,
                                    loc, local->xattr_req);
                } else {
                        STACK_WIND (frame, switch_local_lookup_cbk,
                                    cached_subvol,
                                    cached_subvol->fops->lookup,
                                    loc, local->xattr_req);
                }
        }

        return 0;

err:
        op_errno = (op_errno == -1) ? errno : op_errno;
        DHT_STACK_UNWIND (lookup, frame, -1, op_errno, NULL, NULL, NULL, NULL);
        return 0;
}

int
switch_create_linkfile_create_cbk (call_frame_t *frame, void *cookie,
                                   xlator_t *this, int op_ret, int op_errno,
                                   inode_t *inode, struct iatt *stbuf,
                                   struct iatt *preparent,
                                   struct iatt *postparent)
{
        dht_local_t  *local = NULL;

        local = frame->local;

        if (op_ret == -1)
                goto err;

        STACK_WIND (frame, dht_create_cbk,
                    local->cached_subvol, local->cached_subvol->fops->create,
                    &local->loc, local->flags, local->mode, local->fd,
                    local->params);

        return 0;

err:
        DHT_STACK_UNWIND (create, frame, -1, op_errno,
                          NULL, NULL, NULL, NULL, NULL);
        return 0;
}

int
switch_create (call_frame_t *frame, xlator_t *this,
               loc_t *loc, int32_t flags, mode_t mode,
               fd_t *fd, dict_t *params)
{
        dht_local_t *local = NULL;
        dht_conf_t  *conf  = NULL;
        xlator_t    *subvol = NULL;
        xlator_t    *avail_subvol = NULL;
        int          op_errno = -1;
        int          ret = -1;

        VALIDATE_OR_GOTO (frame, err);
        VALIDATE_OR_GOTO (this, err);
        VALIDATE_OR_GOTO (loc, err);

        conf  = this->private;

        dht_get_du_info (frame, this, loc);

        local = dht_local_init (frame);
        if (!local) {
                op_errno = ENOMEM;
                goto err;
        }

        subvol = dht_subvol_get_hashed (this, loc);
        if (!subvol) {
                gf_log (this->name, GF_LOG_DEBUG,
                        "no subvolume in layout for path=%s",
                        loc->path);
                op_errno = ENOENT;
                goto err;
        }

        avail_subvol = get_switch_matching_subvol (loc->path, conf, subvol);
        if (dht_is_subvol_filled (this, avail_subvol)) {
                avail_subvol =
                        dht_free_disk_available_subvol (this, avail_subvol);
        }

        if (subvol != avail_subvol) {
                /* create a link file instead of actual file */
                ret = loc_copy (&local->loc, loc);
                if (ret == -1) {
                        op_errno = ENOMEM;
                        goto err;
                }

                local->fd = fd_ref (fd);
                local->mode = mode;
                local->flags = flags;

                local->cached_subvol = avail_subvol;
                dht_linkfile_create (frame,
                                     switch_create_linkfile_create_cbk,
                                     avail_subvol, subvol, loc);
                return 0;
        }

        gf_log (this->name, GF_LOG_TRACE,
                "creating %s on %s", loc->path, subvol->name);

        STACK_WIND (frame, dht_create_cbk,
                    subvol, subvol->fops->create,
                    loc, flags, mode, fd, params);

        return 0;

err:
        op_errno = (op_errno == -1) ? errno : op_errno;
        DHT_STACK_UNWIND (create, frame, -1, op_errno,
                          NULL, NULL, NULL, NULL, NULL);

        return 0;
}

int
switch_mknod_linkfile_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                           int op_ret, int op_errno, inode_t *inode,
                           struct iatt *stbuf, struct iatt *preparent,
                           struct iatt *postparent)
{
        dht_local_t  *local = NULL;

        local = frame->local;

        if (op_ret >= 0) {
                STACK_WIND (frame, dht_newfile_cbk,
                            local->cached_subvol,
                            local->cached_subvol->fops->mknod,
                            &local->loc, local->mode, local->rdev,
                            local->params);

                return 0;
        }

        DHT_STACK_UNWIND (link, frame, op_ret, op_errno,
                          inode, stbuf, preparent, postparent);
        return 0;
}


int
switch_mknod (call_frame_t *frame, xlator_t *this,
              loc_t *loc, mode_t mode, dev_t rdev, dict_t *params)
{
        dht_local_t *local = NULL;
        dht_conf_t  *conf  = NULL;
        xlator_t    *subvol = NULL;
        xlator_t    *avail_subvol = NULL;
        int          op_errno = -1;
        int          ret = -1;

        VALIDATE_OR_GOTO (frame, err);
        VALIDATE_OR_GOTO (this, err);
        VALIDATE_OR_GOTO (loc, err);

        conf  = this->private;

        dht_get_du_info (frame, this, loc);

        local = dht_local_init (frame);
        if (!local) {
                op_errno = ENOMEM;
                goto err;
        }

        subvol = dht_subvol_get_hashed (this, loc);
        if (!subvol) {
                gf_log (this->name, GF_LOG_DEBUG,
                        "no subvolume in layout for path=%s",
                        loc->path);
                op_errno = ENOENT;
                goto err;
        }

        /* Consider the disksize in consideration */
        avail_subvol = get_switch_matching_subvol (loc->path, conf, subvol);
        if (dht_is_subvol_filled (this, avail_subvol)) {
                avail_subvol =
                        dht_free_disk_available_subvol (this, avail_subvol);
        }

        if (avail_subvol != subvol) {
                /* Create linkfile first */
                ret = loc_copy (&local->loc, loc);
                if (ret == -1) {
                        op_errno = ENOMEM;
                        goto err;
                }

                local->params = dict_ref (params);
                local->mode = mode;
                local->rdev = rdev;
                local->cached_subvol = avail_subvol;

                dht_linkfile_create (frame, switch_mknod_linkfile_cbk,
                                     avail_subvol, subvol, loc);
                return 0;
        }

        gf_log (this->name, GF_LOG_TRACE,
                "creating %s on %s", loc->path, subvol->name);

        STACK_WIND (frame, dht_newfile_cbk,
                    subvol, subvol->fops->mknod,
                    loc, mode, rdev, params);

        return 0;

err:
        op_errno = (op_errno == -1) ? errno : op_errno;
        DHT_STACK_UNWIND (mknod, frame, -1, op_errno,
                          NULL, NULL, NULL, NULL);

        return 0;
}


int
notify (xlator_t *this, int event, void *data, ...)
{
        int ret = -1;

        ret = dht_notify (this, event, data);

        return ret;
}

void
fini (xlator_t *this)
{
        int                   i = 0;
        dht_conf_t           *conf = NULL;
        struct switch_struct *trav = NULL;
        struct switch_struct *prev = NULL;

        conf = this->private;

        if (conf) {
                trav = (struct switch_struct *)conf->private;
                conf->private = NULL;
                while (trav) {
                        if (trav->array)
                                GF_FREE (trav->array);
                        prev = trav;
                        trav = trav->next;
                        GF_FREE (prev);
                }

                if (conf->file_layouts) {
                        for (i = 0; i < conf->subvolume_cnt; i++) {
                                GF_FREE (conf->file_layouts[i]);
                        }
                        GF_FREE (conf->file_layouts);
                }

                if (conf->default_dir_layout)
                        GF_FREE (conf->default_dir_layout);

                if (conf->subvolumes)
                        GF_FREE (conf->subvolumes);

                if (conf->subvolume_status)
                        GF_FREE (conf->subvolume_status);

                GF_FREE (conf);
        }

        return;
}

int
set_switch_pattern (xlator_t *this, dht_conf_t *conf,
                    const char *pattern_str)
{
        int                         flag = 0;
        int                         idx = 0;
        int                         index = 0;
        int                         child_count = 0;
        char                       *tmp = NULL;
        char                       *tmp1 = NULL;
        char                       *child = NULL;
        char                       *tmp_str = NULL;
        char                       *tmp_str1 = NULL;
        char                       *dup_str = NULL;
        char                       *dup_childs = NULL;
        char                       *switch_str = NULL;
        char                       *pattern = NULL;
        char                       *childs = NULL;
        char                       *option_string = NULL;
        struct switch_struct        *switch_buf = NULL;
        struct switch_struct        *switch_opt = NULL;
        struct switch_struct        *trav = NULL;
        struct switch_sched_array  *switch_buf_array = NULL;
        xlator_list_t              *trav_xl = NULL;

        trav_xl = this->children;
        while (trav_xl) {
                index++;
                trav_xl = trav_xl->next;
        }
        child_count = index;
        switch_buf_array = GF_CALLOC ((index + 1),
                                      sizeof (struct switch_sched_array),
                                      gf_switch_mt_switch_sched_array);
        if (!switch_buf_array)
                goto err;

        trav_xl = this->children;
        index = 0;

        while (trav_xl) {
                switch_buf_array[index].xl = trav_xl->xlator;
                switch_buf_array[index].eligible = 1;
                trav_xl = trav_xl->next;
                index++;
        }

        /*  *jpg:child1,child2;*mpg:child3;*:child4,child5,child6 */

        /* Get the pattern for considering switch case.
           "option block-size *avi:10MB" etc */
        option_string = gf_strdup (pattern_str);
        switch_str = strtok_r (option_string, ";", &tmp_str);
        while (switch_str) {
                dup_str = gf_strdup (switch_str);
                switch_opt = GF_CALLOC (1, sizeof (struct switch_struct),
                                        gf_switch_mt_switch_struct);
                if (!switch_opt)
                        goto err;

                pattern = strtok_r (dup_str, ":", &tmp_str1);
                childs = strtok_r (NULL, ":", &tmp_str1);
                if (strncmp (pattern, "*", 2) == 0) {
                        gf_log ("switch", GF_LOG_NORMAL,
                                "'*' pattern will be taken by default "
                                "for all the unconfigured child nodes,"
                                " hence neglecting current option");
                        switch_str = strtok_r (NULL, ";", &tmp_str);
                        GF_FREE (dup_str);
                        continue;
                }
                memcpy (switch_opt->path_pattern, pattern, strlen (pattern));
                if (childs) {
                        dup_childs = gf_strdup (childs);
                        child = strtok_r (dup_childs, ",", &tmp);
                        while (child) {
                                if (gf_switch_valid_child (this, child)) {
                                        idx++;
                                        child = strtok_r (NULL, ",", &tmp);
                                } else {
                                        gf_log (this->name, GF_LOG_ERROR,
                                                "%s is not a subvolume of %s. "
                                                "pattern can only be scheduled "
                                                "only to a subvolume of %s",
                                                child, this->name, this->name);
                                        goto err;
                                }
                        }
                        GF_FREE (dup_childs);
                        child = strtok_r (childs, ",", &tmp1);
                        switch_opt->num_child = idx;
                        switch_opt->array = GF_CALLOC (1, (idx *
                                                           sizeof (struct switch_sched_array)),
                                                       gf_switch_mt_switch_sched_array);
                        if (!switch_opt->array)
                                goto err;
                        idx = 0;
                        while (child) {
                                for (index = 0; index < child_count; index++) {
                                        if (strcmp (switch_buf_array[index].xl->name,
                                                    child) == 0) {
                                                gf_log ("switch", GF_LOG_DEBUG,
                                                        "'%s' pattern will be "
                                                        "scheduled to \"%s\"",
                                                        switch_opt->path_pattern, child);
                                                /*
                                                  if (switch_buf_array[index-1].considered) {
                                                  gf_log ("switch", GF_LOG_DEBUG,
                                                  "ambiguity found, exiting");
                                                  return -1;
                                                  }
                                                */
                                                switch_opt->array[idx].xl = switch_buf_array[index].xl;
                                                switch_buf_array[index].considered = 1;
                                                idx++;
                                                break;
                                        }
                                }
                                child = strtok_r (NULL, ",", &tmp1);
                        }
                } else {
                        /* error */
                        gf_log ("switch", GF_LOG_ERROR,
                                "Check \"scheduler.switch.case\" "
                                "option in unify volume. Exiting");
                        goto err;
                }
                GF_FREE (dup_str);

                /* Link it to the main structure */
                if (switch_buf) {
                        /* there are already few entries */
                        trav = switch_buf;
                        while (trav->next)
                                trav = trav->next;
                        trav->next = switch_opt;
                } else {
                        /* First entry */
                        switch_buf = switch_opt;
                }
                switch_str = strtok_r (NULL, ";", &tmp_str);
        }

        /* Now, all the pattern based considerations done, so for all the
         * remaining pattern, '*' to all the remaining child nodes
         */
        {
                for (index=0; index < child_count; index++) {
                        /* check for considered flag */
                        if (switch_buf_array[index].considered)
                                continue;
                        flag++;
                }
                if (!flag) {
                        gf_log ("switch", GF_LOG_ERROR,
                                "No nodes left for pattern '*'. Exiting");
                        goto err;
                }
                switch_opt = GF_CALLOC (1, sizeof (struct switch_struct),
                                        gf_switch_mt_switch_struct);
                if (!switch_opt)
                        goto err;

                /* Add the '*' pattern to the array */
                memcpy (switch_opt->path_pattern, "*", 2);
                switch_opt->num_child = flag;
                switch_opt->array =
                        GF_CALLOC (1,
                                   flag * sizeof (struct switch_sched_array),
                                   gf_switch_mt_switch_sched_array);
                if (!switch_opt->array)
                        goto err;
                flag = 0;
                for (index=0; index < child_count; index++) {
                        /* check for considered flag */
                        if (switch_buf_array[index].considered)
                                continue;
                        gf_log ("switch", GF_LOG_DEBUG,
                                "'%s' pattern will be scheduled to \"%s\"",
                                switch_opt->path_pattern,
                                switch_buf_array[index].xl->name);
                        switch_opt->array[flag].xl =
                                switch_buf_array[index].xl;
                        switch_buf_array[index].considered = 1;
                        flag++;
                }
                if (switch_buf) {
                        /* there are already few entries */
                        trav = switch_buf;
                        while (trav->next)
                                trav = trav->next;
                        trav->next = switch_opt;
                } else {
                        /* First entry */
                        switch_buf = switch_opt;
                }
        }
        /* */
        conf->private = switch_buf;

        return 0;
err:
        if (switch_buf) {
                if (switch_buf_array)
                        GF_FREE (switch_buf_array);
                trav = switch_buf;
                while (trav) {
                        if (trav->array)
                                GF_FREE (trav->array);
                        switch_opt = trav;
                        trav = trav->next;
                        GF_FREE (switch_opt);
                }
        }
        return -1;
}


int
init (xlator_t *this)
{
        dht_conf_t            *conf = NULL;
        data_t                *data = NULL;
        char                  *temp_str = NULL;
        int                    ret = -1;
        int                    i = 0;
        uint32_t               temp_free_disk = 0;

        if (!this->children) {
                gf_log (this->name, GF_LOG_CRITICAL,
                        "SWITCH needs more than one subvolume");
                return -1;
        }

        if (!this->parents) {
                gf_log (this->name, GF_LOG_WARNING,
                        "dangling volume. check volfile");
        }

        conf = GF_CALLOC (1, sizeof (*conf), gf_switch_mt_dht_conf_t);
        if (!conf) {
                goto err;
        }

        conf->search_unhashed = GF_DHT_LOOKUP_UNHASHED_ON;
        if (dict_get_str (this->options, "lookup-unhashed", &temp_str) == 0) {
                /* If option is not "auto", other options _should_ be boolean */
                if (strcasecmp (temp_str, "auto"))
                        gf_string2boolean (temp_str, &conf->search_unhashed);
                else
                        conf->search_unhashed = GF_DHT_LOOKUP_UNHASHED_AUTO;
        }

        conf->unhashed_sticky_bit = 0;
        if (dict_get_str (this->options, "unhashed-sticky-bit",
                          &temp_str) == 0) {
                gf_string2boolean (temp_str, &conf->unhashed_sticky_bit);
        }

        conf->min_free_disk = 10;
        conf->disk_unit = 'p';

        if (dict_get_str (this->options, "min-free-disk",
                          &temp_str) == 0) {
                if (gf_string2percent (temp_str,
                                       &temp_free_disk) == 0) {
                        if (temp_free_disk > 100) {
                                gf_string2bytesize (temp_str,
                                                    &conf->min_free_disk);
                                conf->disk_unit = 'b';
                        } else {
                                conf->min_free_disk = (uint64_t)temp_free_disk;
                                conf->disk_unit = 'p';
                        }
                } else {
                        gf_string2bytesize (temp_str,
                                            &conf->min_free_disk);
                        conf->disk_unit = 'b';
                }
        }

        data = dict_get (this->options, "pattern.switch.case");
        if (data) {
                /* TODO: */
                ret = set_switch_pattern (this, conf, data->data);
                if (ret) {
                        goto err;
                }
        }

        ret = dht_init_subvolumes (this, conf);
        if (ret == -1) {
                goto err;
        }

        ret = dht_layouts_init (this, conf);
        if (ret == -1) {
                goto err;
        }

        LOCK_INIT (&conf->subvolume_lock);
        LOCK_INIT (&conf->layout_lock);

        conf->gen = 1;

        conf->du_stats = GF_CALLOC (conf->subvolume_cnt, sizeof (dht_du_t),
                                    gf_switch_mt_dht_du_t);
        if (!conf->du_stats) {
                goto err;
        }

        this->private = conf;

        return 0;

err:
        if (conf) {
                if (conf->file_layouts) {
                        for (i = 0; i < conf->subvolume_cnt; i++) {
                                GF_FREE (conf->file_layouts[i]);
                        }
                        GF_FREE (conf->file_layouts);
                }

                if (conf->default_dir_layout)
                        GF_FREE (conf->default_dir_layout);

                if (conf->subvolumes)
                        GF_FREE (conf->subvolumes);

                if (conf->subvolume_status)
                        GF_FREE (conf->subvolume_status);

                if (conf->du_stats)
                        GF_FREE (conf->du_stats);

                GF_FREE (conf);
        }

        return -1;
}


struct xlator_fops fops = {
        .lookup      = switch_lookup,
        .create      = switch_create,
        .mknod       = switch_mknod,

        .stat        = dht_stat,
        .fstat       = dht_fstat,
        .truncate    = dht_truncate,
        .ftruncate   = dht_ftruncate,
        .access      = dht_access,
        .readlink    = dht_readlink,
        .setxattr    = dht_setxattr,
        .getxattr    = dht_getxattr,
        .removexattr = dht_removexattr,
        .open        = dht_open,
        .readv       = dht_readv,
        .writev      = dht_writev,
        .flush       = dht_flush,
        .fsync       = dht_fsync,
        .statfs      = dht_statfs,
        .lk          = dht_lk,
        .opendir     = dht_opendir,
        .readdir     = dht_readdir,
        .readdirp    = dht_readdirp,
        .fsyncdir    = dht_fsyncdir,
        .symlink     = dht_symlink,
        .unlink      = dht_unlink,
        .link        = dht_link,
        .mkdir       = dht_mkdir,
        .rmdir       = dht_rmdir,
        .rename      = dht_rename,
        .inodelk     = dht_inodelk,
        .finodelk    = dht_finodelk,
        .entrylk     = dht_entrylk,
        .fentrylk    = dht_fentrylk,
        .xattrop     = dht_xattrop,
        .fxattrop    = dht_fxattrop,
        .setattr     = dht_setattr,
};


struct xlator_cbks cbks = {
        .forget     = dht_forget
};


struct volume_options options[] = {
        { .key  = {"lookup-unhashed"},
          .value = {"auto", "yes", "no", "enable", "disable", "1", "0",
                    "on", "off"},
          .type = GF_OPTION_TYPE_STR
        },
        { .key  = {"pattern.switch.case"},
          .type = GF_OPTION_TYPE_ANY
        },
        { .key  = {"min-free-disk"},
          .type = GF_OPTION_TYPE_PERCENT_OR_SIZET,
        },
        { .key  = {NULL} },
};
