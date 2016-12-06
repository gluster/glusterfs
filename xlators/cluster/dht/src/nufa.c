/*
  Copyright (c) 2008-2012 Red Hat, Inc. <http://www.redhat.com>
  This file is part of GlusterFS.

  This file is licensed to you under your choice of the GNU Lesser
  General Public License, version 3 or any later version (LGPLv3 or
  later), or the GNU General Public License, version 2 (GPLv2), in all
  cases as published by the Free Software Foundation.
*/


#include "dht-common.h"

/* TODO: all 'TODO's in dht.c holds good */

extern struct volume_options options[];

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
        xlator_t     *prev        = NULL;
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

        is_linkfile = check_is_linkfile (inode, stbuf, xattr,
                                         conf->link_xattr_name);
        is_dir      = check_is_dir (inode, stbuf, xattr);

        if (!is_dir && !is_linkfile) {
                /* non-directory and not a linkfile */
                ret = dht_layout_preset (this, prev, inode);
                if (ret < 0) {
                        gf_msg_debug (this->name, 0,
                                      "could not set pre-set layout for subvol"
                                      " %s", prev->name);
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
                        STACK_WIND_COOKIE (frame, dht_lookup_dir_cbk,
                                           conf->subvolumes[i],
                                           conf->subvolumes[i],
                                           conf->subvolumes[i]->fops->lookup,
                                           &local->loc, local->xattr_req);
                }
        }

        if (is_linkfile) {
                subvol = dht_linkfile_subvol (this, inode, stbuf, xattr);

                if (!subvol) {
                        gf_msg_debug (this->name, 0,
                                      "linkfile has no link subvolume. path=%s",
                                      loc->path);
                        dht_lookup_everywhere (frame, this, loc);
                        return 0;
                }

                STACK_WIND_COOKIE (frame, dht_lookup_linkfile_cbk, subvol,
                                   subvol, subvol->fops->lookup,
                                   &local->loc, local->xattr_req);
        }

        return 0;

out:
        if (!local->hashed_subvol) {
                gf_msg_debug (this->name, 0,
                              "no subvolume in layout for path=%s",
                              local->loc.path);
                local->op_errno = ENOENT;
                dht_lookup_everywhere (frame, this, loc);
                return 0;
        }

        STACK_WIND_COOKIE (frame, dht_lookup_cbk, local->hashed_subvol,
                           local->hashed_subvol,
                           local->hashed_subvol->fops->lookup,
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
                        gf_msg_debug (this->name, 0,
                                      "revalidate lookup without cache. "
                                      "path=%s", loc->path);
                        op_errno = EINVAL;
                        goto err;
                }

                if (layout->gen && (layout->gen < conf->gen)) {
                        gf_msg_debug (this->name, 0,
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
                                       conf->xattr_name, 4 * 4);
                if (ret < 0) {
                        gf_msg (this->name, GF_LOG_ERROR, 0,
                                DHT_MSG_DICT_SET_FAILED,
                                "Failed to set dict value.");
                        op_errno = -1;
                        goto err;
                }

                for (i = 0; i < layout->cnt; i++) {
                        subvol = layout->list[i].xlator;

                        STACK_WIND_COOKIE (frame, dht_revalidate_cbk, subvol,
                                           subvol, subvol->fops->lookup,
                                           loc, local->xattr_req);

                        if (!--call_cnt)
                                break;
                }
        } else {
        do_fresh_lookup:
                ret = dict_set_uint32 (local->xattr_req,
                                       conf->xattr_name, 4 * 4);
                if (ret < 0) {
                        gf_msg (this->name, GF_LOG_ERROR, 0,
                                DHT_MSG_DICT_SET_FAILED,
                                "Failed to set dict value.");
                        op_errno = -1;
                        goto err;
                }

                ret = dict_set_uint32 (local->xattr_req,
                                       conf->link_xattr_name, 256);
                if (ret < 0) {
                        gf_msg (this->name, GF_LOG_ERROR, 0,
                                DHT_MSG_DICT_SET_FAILED,
                                "Failed to set dict value.");
                        op_errno = -1;
                        goto err;
                }

                /* Send it to only local volume */
                STACK_WIND_COOKIE (frame, nufa_local_lookup_cbk,
                                   ((xlator_t *)conf->private),
                                   ((xlator_t *)conf->private),
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

        STACK_WIND_COOKIE (frame, dht_create_cbk, local->cached_subvol,
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
                gf_msg_debug (this->name, 0,
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
                dht_linkfile_create (frame, nufa_create_linkfile_create_cbk,
                                     this, avail_subvol, subvol, loc);
                return 0;
        }

        gf_msg_trace (this->name, 0,
                      "creating %s on %s", loc->path, subvol->name);

        STACK_WIND_COOKIE (frame, dht_create_cbk, subvol,
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
                gf_msg_debug (this->name, 0,
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

                dht_linkfile_create (frame, nufa_mknod_linkfile_cbk, this,
                                     avail_subvol, subvol, loc);
                return 0;
        }

        gf_msg_trace (this->name, 0,
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

typedef struct nufa_args {
        xlator_t    *this;
        char        *volname;
        gf_boolean_t addr_match;
} nufa_args_t;

static void
nufa_find_local_brick (xlator_t *xl, void *data)
{
        nufa_args_t     *args = data;
        xlator_t        *this = args->this;
        char            *local_volname = args->volname;
        gf_boolean_t    addr_match = args->addr_match;
        char            *brick_host = NULL;
        dht_conf_t      *conf = this->private;
        int             ret = -1;

        /*This means a local subvol was already found. We pick the first brick
         * that is local*/
        if (conf->private)
                return;

        if (strcmp (xl->name, local_volname) == 0) {
                conf->private = xl;
                gf_msg (this->name, GF_LOG_INFO, 0,
                        DHT_MSG_SUBVOL_INFO,
                        "Using specified subvol %s",
                        local_volname);
                return;
        }

        if (!addr_match)
                return;

        ret = dict_get_str (xl->options, "remote-host", &brick_host);
        if ((ret == 0) &&
            (gf_is_same_address (local_volname, brick_host) ||
             gf_is_local_addr (brick_host))) {
                conf->private = xl;
                gf_msg (this->name, GF_LOG_INFO, 0,
                        DHT_MSG_SUBVOL_INFO, "Using the first local "
                        "subvol %s", xl->name);
                return;
        }

}

static void
nufa_to_dht (xlator_t *this)
{
        GF_ASSERT (this);
        GF_ASSERT (this->fops);

        this->fops->lookup = dht_lookup;
        this->fops->create = dht_create;
        this->fops->mknod  = dht_mknod;
}

int
nufa_find_local_subvol (xlator_t *this,
                        void (*fn) (xlator_t *each, void* data), void *data)
{
        int             ret = -1;
        dht_conf_t      *conf = this->private;
        xlator_list_t   *trav = NULL;
        xlator_t        *parent = NULL;
        xlator_t        *candidate = NULL;

        xlator_foreach_depth_first (this, fn, data);
        if (!conf->private) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        DHT_MSG_BRICK_ERROR, "Couldn't find a local "
                        "brick");
                return -1;
        }

        candidate = conf->private;
        trav = candidate->parents;
        while (trav) {

                parent = trav->xlator;
                if (strcmp (parent->type, "cluster/nufa") == 0) {
                        gf_msg (this->name, GF_LOG_INFO, 0,
                                DHT_MSG_SUBVOL_INFO, "Found local subvol, "
                                "%s", candidate->name);
                        ret = 0;
                        conf->private = candidate;
                        break;
                }

                candidate = parent;
                trav = parent->parents;
        }

        return ret;
}

int
nufa_init (xlator_t *this)
{
        data_t        *data = NULL;
        char          *local_volname = NULL;
        int            ret = -1;
        char           my_hostname[256];
        gf_boolean_t   addr_match = _gf_false;
        nufa_args_t    args = {0, };

        ret = dht_init(this);
        if (ret) {
                return ret;
        }

        if ((data = dict_get (this->options, "local-volume-name"))) {
                local_volname = data->data;

        } else {
                addr_match = _gf_true;
                local_volname = "localhost";
                ret = gethostname (my_hostname, 256);
                if (ret == 0)
                        local_volname = my_hostname;

                else
                        gf_msg (this->name, GF_LOG_WARNING, errno,
                                DHT_MSG_GET_HOSTNAME_FAILED,
                                "could not find hostname");

        }

        args.this = this;
        args.volname = local_volname;
        args.addr_match = addr_match;
        ret = nufa_find_local_subvol (this, nufa_find_local_brick, &args);
        if (ret) {
                gf_msg (this->name, GF_LOG_INFO, 0,
                        DHT_MSG_SUBVOL_INFO,
                        "Unable to find local subvolume, switching "
                        "to dht mode");
                nufa_to_dht (this);
        }
        return 0;
}

dht_methods_t dht_methods = {
        .migration_get_dst_subvol = dht_migration_get_dst_subvol,
        .migration_needed = dht_migration_needed,
        .layout_search   = dht_layout_search,
};

class_methods_t class_methods = {
        .init           = nufa_init,
        .fini           = dht_fini,
        .reconfigure    = dht_reconfigure,
        .notify         = dht_notify
};


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
