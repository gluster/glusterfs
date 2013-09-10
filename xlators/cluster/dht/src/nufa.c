/*
  Copyright (c) 2008-2012 Red Hat, Inc. <http://www.redhat.com>
  This file is part of GlusterFS.

  This file is licensed to you under your choice of the GNU Lesser
  General Public License, version 3 or any later version (LGPLv3 or
  later), or the GNU General Public License, version 2 (GPLv2), in all
  cases as published by the Free Software Foundation.
*/


#ifndef _CONFIG_H
#define _CONFIG_H
#include "config.h"
#endif

#include "dht-common.h"

/* TODO: all 'TODO's in dht.c holds good */

int
nufa_local_lookup_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
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
                          inode, stbuf, xattr, postparent);
        return 0;
}

int
nufa_lookup (call_frame_t *frame, xlator_t *this,
             loc_t *loc, dict_t *xattr_req)
{
        xlator_t     *hashed_subvol = NULL;
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

        local = dht_local_init (frame, loc, NULL, GF_FOP_LOOKUP);
        if (!local) {
                op_errno = ENOMEM;
                goto err;
        }

        if (xattr_req) {
                local->xattr_req = dict_ref (xattr_req);
        } else {
                local->xattr_req = dict_new ();
        }

        hashed_subvol = dht_subvol_get_hashed (this, &local->loc);

        local->hashed_subvol = hashed_subvol;

        if (is_revalidate (loc)) {
                layout = local->layout;
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

                local->inode = inode_ref (loc->inode);

                local->call_cnt = layout->cnt;
                call_cnt = local->call_cnt;

                /* NOTE: we don't require 'trusted.glusterfs.dht.linkto' attribute,
                 *       revalidates directly go to the cached-subvolume.
                 */
                ret = dict_set_uint32 (local->xattr_req,
                                       "trusted.glusterfs.dht", 4 * 4);
                if (ret < 0) {
                        gf_log (this->name, GF_LOG_ERROR,
                                "Failed to set dict value.");
                        op_errno = -1;
                        goto err;
                }

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
                if (ret < 0) {
                        gf_log (this->name, GF_LOG_ERROR,
                                "Failed to set dict value.");
                        op_errno = -1;
                        goto err;
                }

                ret = dict_set_uint32 (local->xattr_req,
                                       "trusted.glusterfs.dht.linkto", 256);
                if (ret < 0) {
                        gf_log (this->name, GF_LOG_ERROR,
                                "Failed to set dict value.");
                        op_errno = -1;
                        goto err;
                }

                /* Send it to only local volume */
                STACK_WIND (frame, nufa_local_lookup_cbk,
                            (xlator_t *)conf->private,
                            ((xlator_t *)conf->private)->fops->lookup,
                            loc, local->xattr_req);
        }

        return 0;

err:
        op_errno = (op_errno == -1) ? errno : op_errno;
        DHT_STACK_UNWIND (lookup, frame, -1, op_errno, NULL, NULL, NULL,
                          NULL);
        return 0;
}

int
nufa_create_linkfile_create_cbk (call_frame_t *frame, void *cookie,
                                 xlator_t *this, int op_ret, int op_errno,
                                 inode_t *inode, struct iatt *stbuf,
                                 struct iatt *preparent,
                                 struct iatt *postparent, dict_t *xdata)
{
        dht_local_t  *local = NULL;

        local = frame->local;

        if (op_ret == -1)
                goto err;

        STACK_WIND (frame, dht_create_cbk,
                    local->cached_subvol, local->cached_subvol->fops->create,
                    &local->loc, local->flags, local->mode, local->umask,
                    local->fd, local->params);

        return 0;

err:
        DHT_STACK_UNWIND (create, frame, -1, op_errno,
                          NULL, NULL, NULL, NULL, NULL, NULL);
        return 0;
}

int
nufa_create (call_frame_t *frame, xlator_t *this,
             loc_t *loc, int32_t flags, mode_t mode,
             mode_t umask, fd_t *fd, dict_t *params)
{
        dht_local_t *local = NULL;
        dht_conf_t  *conf  = NULL;
        xlator_t    *subvol = NULL;
        xlator_t    *avail_subvol = NULL;
        int          op_errno = -1;

        VALIDATE_OR_GOTO (frame, err);
        VALIDATE_OR_GOTO (this, err);
        VALIDATE_OR_GOTO (loc, err);

        conf  = this->private;

        dht_get_du_info (frame, this, loc);

        local = dht_local_init (frame, loc, fd, GF_FOP_CREATE);
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

        avail_subvol = conf->private;
        if (dht_is_subvol_filled (this, (xlator_t *)conf->private)) {
                avail_subvol =
                        dht_free_disk_available_subvol (this,
                                                        (xlator_t *)conf->private,
                                                        local);
        }

        if (subvol != avail_subvol) {
                /* create a link file instead of actual file */
                local->params = dict_ref (params);
                local->mode = mode;
                local->flags = flags;
                local->umask = umask;
                local->cached_subvol = avail_subvol;
                dht_linkfile_create (frame,
                                     nufa_create_linkfile_create_cbk,
                                     avail_subvol, subvol, loc);
                return 0;
        }

        gf_log (this->name, GF_LOG_TRACE,
                "creating %s on %s", loc->path, subvol->name);

        STACK_WIND (frame, dht_create_cbk,
                    subvol, subvol->fops->create,
                    loc, flags, mode, umask, fd, params);

        return 0;

err:
        op_errno = (op_errno == -1) ? errno : op_errno;
        DHT_STACK_UNWIND (create, frame, -1, op_errno,
                          NULL, NULL, NULL, NULL, NULL, NULL);

        return 0;
}

int
nufa_mknod_linkfile_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                         int op_ret, int op_errno, inode_t *inode,
                         struct iatt *stbuf, struct iatt *preparent,
                         struct iatt *postparent, dict_t *xdata)
{
        dht_local_t  *local = NULL;

        local = frame->local;
        if (!local || !local->cached_subvol) {
                op_errno = EINVAL;
                op_ret = -1;
                goto err;
        }

        if (op_ret >= 0) {
                STACK_WIND_COOKIE (frame, dht_newfile_cbk,
                            (void *)local->cached_subvol, local->cached_subvol,
                            local->cached_subvol->fops->mknod,
                            &local->loc, local->mode, local->rdev,
                            local->umask, local->params);

                return 0;
        }
err:
        WIPE (postparent);
        WIPE (preparent);

        DHT_STACK_UNWIND (link, frame, op_ret, op_errno,
                          inode, stbuf, preparent, postparent, xdata);
        return 0;
}


int
nufa_mknod (call_frame_t *frame, xlator_t *this,
            loc_t *loc, mode_t mode, dev_t rdev, mode_t umask, dict_t *params)
{
        dht_local_t *local = NULL;
        dht_conf_t  *conf  = NULL;
        xlator_t    *subvol = NULL;
        xlator_t    *avail_subvol = NULL;
        int          op_errno = -1;

        VALIDATE_OR_GOTO (frame, err);
        VALIDATE_OR_GOTO (this, err);
        VALIDATE_OR_GOTO (loc, err);

        conf  = this->private;

        dht_get_du_info (frame, this, loc);

        local = dht_local_init (frame, loc, NULL, GF_FOP_MKNOD);
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
        avail_subvol = conf->private;
        if (dht_is_subvol_filled (this, (xlator_t *)conf->private)) {
                avail_subvol =
                        dht_free_disk_available_subvol (this,
                                                        (xlator_t *)conf->private,
                                                        local);
        }

        if (avail_subvol != subvol) {
                /* Create linkfile first */

                local->params = dict_ref (params);
                local->mode = mode;
                local->umask = umask;
                local->rdev = rdev;
                local->cached_subvol = avail_subvol;

                dht_linkfile_create (frame, nufa_mknod_linkfile_cbk,
                                     avail_subvol, subvol, loc);
                return 0;
        }

        gf_log (this->name, GF_LOG_TRACE,
                "creating %s on %s", loc->path, subvol->name);

        STACK_WIND_COOKIE (frame, dht_newfile_cbk, (void *)subvol, subvol,
                           subvol->fops->mknod, loc, mode, rdev, umask,
                           params);

        return 0;

err:
        op_errno = (op_errno == -1) ? errno : op_errno;
        DHT_STACK_UNWIND (mknod, frame, -1, op_errno,
                          NULL, NULL, NULL, NULL, NULL);

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
        int         i = 0;
        dht_conf_t *conf = NULL;

        conf = this->private;

        if (conf) {
                if (conf->file_layouts) {
                        for (i = 0; i < conf->subvolume_cnt; i++) {
                                GF_FREE (conf->file_layouts[i]);
                        }
                        GF_FREE (conf->file_layouts);
                }

                GF_FREE (conf->subvolumes);

                GF_FREE (conf->subvolume_status);

                GF_FREE (conf);
        }

        return;
}

gf_boolean_t
same_first_part (char *str1, char term1, char *str2, char term2)
{
        gf_boolean_t    ended1;
        gf_boolean_t    ended2;

        for (;;) {
                ended1 = ((*str1 == '\0') || (*str1 == term1));
                ended2 = ((*str2 == '\0') || (*str2 == term2));
                if (ended1 && ended2) {
                        return _gf_true;
                }
                if (ended1 || ended2 || (*str1 != *str2)) {
                        return _gf_false;
                }
                ++str1;
                ++str2;
        }
}

int
init (xlator_t *this)
{
        dht_conf_t    *conf = NULL;
        xlator_list_t *trav = NULL;
        data_t        *data = NULL;
        char          *local_volname = NULL;
        char          *temp_str = NULL;
        int            ret = -1;
        int            i = 0;
        char           my_hostname[256];
        double         temp_free_disk = 0;
        uint64_t       size = 0;
        xlator_t      *local_subvol = NULL;
        char          *brick_host = NULL;
        xlator_t      *kid = NULL;

        if (!this->children) {
                gf_log (this->name, GF_LOG_CRITICAL,
                        "NUFA needs more than one subvolume");
                return -1;
        }

        if (!this->parents) {
                gf_log (this->name, GF_LOG_WARNING,
                        "dangling volume. check volfile");
        }

        conf = GF_CALLOC (1, sizeof (*conf),
                          gf_dht_mt_dht_conf_t);
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

        local_volname = "localhost";
        ret = gethostname (my_hostname, 256);
        if (ret < 0) {
                gf_log (this->name, GF_LOG_WARNING,
                        "could not find hostname (%s)",
                        strerror (errno));
        }

        if (ret == 0)
                local_volname = my_hostname;

        data = dict_get (this->options, "local-volume-name");
        if (data) {
                local_volname = data->data;
        }

        for (trav = this->children; trav; trav = trav->next) {
                if (strcmp (trav->xlator->name, local_volname) == 0)
                        break;
                if (local_subvol) {
                        continue;
                }
                kid = trav->xlator;
                for (;;) {
                        if (dict_get_str(trav->xlator->options,"remote-host",
                                         &brick_host) == 0) {
                                /* Found it. */
                                break;
                        }
                        if (!kid->children) {
                                /* Nowhere further to look. */
                                gf_log (this->name, GF_LOG_ERROR,
                                        "could not get remote-host");
                                goto err;
                        }
                        if (kid->children->next) {
                                /* Multiple choices, can't/shouldn't decide. */
                                gf_log (this->name, GF_LOG_ERROR,
                                        "NUFA found fan-out (type %s) volume",
                                        kid->type);
                                goto err;
                        }
                        /* One-to-one xlators are OK, try the next one. */
                        kid = kid->children->xlator;
                }
                if (same_first_part(my_hostname,'.',brick_host,'.')) {
                        local_subvol = trav->xlator;
                }
        }

        if (trav) {
                gf_log (this->name, GF_LOG_INFO,
                        "Using specified subvol %s", local_volname);
                conf->private = trav->xlator;
        }
        else if (local_subvol) {
                gf_log (this->name, GF_LOG_INFO,
                        "Using first local subvol %s", local_subvol->name);
                conf->private = local_subvol;
        }
        else {
                gf_log (this->name, GF_LOG_ERROR,
                        "Could not find specified or local subvol");
                goto err;

        }

        /* The volume specified exists */

        conf->min_free_disk = 10;
        conf->disk_unit = 'p';

        if (dict_get_str (this->options, "min-free-disk",
                          &temp_str) == 0) {
                if (gf_string2percent (temp_str,
                                       &temp_free_disk) == 0) {
                        if (temp_free_disk > 100) {
                                gf_string2bytesize (temp_str, &size);
                                conf->min_free_disk = size;
                                conf->disk_unit = 'b';
                        } else {
                                conf->min_free_disk = temp_free_disk;
                                conf->disk_unit = 'p';
                        }
                } else {
                        gf_string2bytesize (temp_str, &size);
                        conf->min_free_disk = size;
                        conf->disk_unit = 'b';
                }
        }

        conf->du_stats = GF_CALLOC (conf->subvolume_cnt, sizeof (dht_du_t),
                                    gf_dht_mt_dht_du_t);
        if (!conf->du_stats) {
                goto err;
        }

        this->local_pool = mem_pool_new (dht_local_t, 128);
        if (!this->local_pool) {
                gf_log (this->name, GF_LOG_ERROR,
                        "failed to create local_t's memory pool");
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

                GF_FREE (conf->subvolumes);

                GF_FREE (conf->subvolume_status);

                GF_FREE (conf->du_stats);

                GF_FREE (conf);
        }

        return -1;
}


struct xlator_fops fops = {
        .lookup      = nufa_lookup,
        .create      = nufa_create,
        .mknod       = nufa_mknod,

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
        { .key  = {"local-volume-name"},
          .type = GF_OPTION_TYPE_XLATOR
        },
        { .key  = {"min-free-disk"},
          .type = GF_OPTION_TYPE_PERCENT_OR_SIZET,
        },
        { .key  = {NULL} },
};
