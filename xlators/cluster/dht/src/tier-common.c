/*
  Copyright (c) 2015 Red Hat, Inc. <http://www.redhat.com>
  This file is part of GlusterFS.

  This file is licensed to you under your choice of the GNU Lesser
  General Public License, version 3 or any later version (LGPLv3 or
  later), or the GNU General Public License, version 2 (GPLv2), in all
  cases as published by the Free Software Foundation.
*/

#include "glusterfs.h"
#include "xlator.h"
#include "libxlator.h"
#include "dht-common.h"
#include "defaults.h"
#include "tier-common.h"
#include "tier.h"

int
tier_readdir_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                 int op_ret, int op_errno, gf_dirent_t *orig_entries,
                 dict_t *xdata)
{
        dht_local_t  *local = NULL;
        gf_dirent_t   entries;
        gf_dirent_t  *orig_entry = NULL;
        gf_dirent_t  *entry = NULL;
        call_frame_t *prev = NULL;
        xlator_t     *next_subvol = NULL;
        off_t         next_offset = 0;
        int           count = 0;

        INIT_LIST_HEAD (&entries.list);
        prev = cookie;
        local = frame->local;

        if (op_ret < 0)
                goto done;

        list_for_each_entry (orig_entry, (&orig_entries->list), list) {
                next_offset = orig_entry->d_off;

                entry = gf_dirent_for_name (orig_entry->d_name);
                if (!entry) {
                        gf_msg (this->name, GF_LOG_ERROR, ENOMEM,
                                DHT_MSG_NO_MEMORY,
                                "Memory allocation failed ");
                        goto unwind;
                }

                entry->d_off  = orig_entry->d_off;
                entry->d_ino  = orig_entry->d_ino;
                entry->d_type = orig_entry->d_type;
                entry->d_len  = orig_entry->d_len;

                list_add_tail (&entry->list, &entries.list);
                count++;
        }
        op_ret = count;

done:
        if (count == 0) {
                /* non-zero next_offset means that
                   EOF is not yet hit on the current subvol
                */
                if (next_offset != 0) {
                        next_subvol = prev->this;
                } else {
                        goto unwind;
                }

                STACK_WIND (frame, tier_readdir_cbk,
                            next_subvol, next_subvol->fops->readdir,
                            local->fd, local->size, next_offset, NULL);
                return 0;
        }

unwind:
        if (op_ret < 0)
                op_ret = 0;

        DHT_STACK_UNWIND (readdir, frame, op_ret, op_errno, &entries, NULL);

        gf_dirent_free (&entries);

        return 0;
}

int
tier_readdirp_cbk (call_frame_t *frame, void *cookie, xlator_t *this, int op_ret,
                  int op_errno, gf_dirent_t *orig_entries, dict_t *xdata)
{
        dht_local_t  *local = NULL;
        gf_dirent_t   entries;
        gf_dirent_t  *orig_entry = NULL;
        gf_dirent_t  *entry = NULL;
        call_frame_t *prev = NULL;
        xlator_t     *next_subvol = NULL;
        off_t         next_offset = 0;
        int           count = 0;
        dht_conf_t   *conf   = NULL;
        int           ret    = 0;
        inode_table_t           *itable = NULL;
        inode_t                 *inode = NULL;

        INIT_LIST_HEAD (&entries.list);
        prev = cookie;
        local = frame->local;
        itable = local->fd ? local->fd->inode->table : NULL;

        conf  = this->private;
        GF_VALIDATE_OR_GOTO(this->name, conf, unwind);

        if (op_ret < 0)
                goto done;

        list_for_each_entry (orig_entry, (&orig_entries->list), list) {
                next_offset = orig_entry->d_off;

                if (IA_ISINVAL(orig_entry->d_stat.ia_type)) {
                        /*stat failed somewhere- ignore this entry*/
                        continue;
                }

                entry = gf_dirent_for_name (orig_entry->d_name);
                if (!entry) {

                        goto unwind;
                }

                entry->d_off  = orig_entry->d_off;
                entry->d_stat = orig_entry->d_stat;
                entry->d_ino  = orig_entry->d_ino;
                entry->d_type = orig_entry->d_type;
                entry->d_len  = orig_entry->d_len;

                if (orig_entry->dict)
                        entry->dict = dict_ref (orig_entry->dict);

                if (check_is_linkfile (NULL, (&orig_entry->d_stat),
                                       orig_entry->dict,
                                       conf->link_xattr_name)) {
                        inode = inode_find (itable,
                                            orig_entry->d_stat.ia_gfid);
                        if (inode) {
                                ret = dht_layout_preset
                                        (this, TIER_UNHASHED_SUBVOL,
                                         inode);
                                if (ret)
                                        gf_msg (this->name,
                                                GF_LOG_WARNING, 0,
                                                DHT_MSG_LAYOUT_SET_FAILED,
                                                "failed to link the layout"
                                                " in inode");
                                inode_unref (inode);
                                inode = NULL;
                        }

                } else if (IA_ISDIR(entry->d_stat.ia_type)) {
                        if (orig_entry->inode) {
                                dht_inode_ctx_time_update (orig_entry->inode,
                                                           this, &entry->d_stat,
                                                           1);
                        }
                } else {
                        if (orig_entry->inode) {
                                ret = dht_layout_preset (this, prev->this,
                                                         orig_entry->inode);
                                if (ret)
                                        gf_msg (this->name, GF_LOG_WARNING, 0,
                                                DHT_MSG_LAYOUT_SET_FAILED,
                                                "failed to link the layout "
                                                "in inode");

                                entry->inode = inode_ref (orig_entry->inode);
                        } else if (itable) {
                                /*
                                 * orig_entry->inode might be null if any upper
                                 * layer xlators below client set to null, to
                                 * force a lookup on the inode even if the inode
                                 * is present in the inode table. In that case
                                 * we just update the ctx to make sure we didn't
                                 * missed anything.
                                 */
                                inode = inode_find (itable,
                                                    orig_entry->d_stat.ia_gfid);
                                if (inode) {
                                        ret = dht_layout_preset
                                                (this, TIER_HASHED_SUBVOL,
                                                 inode);
                                        if (ret)
                                                gf_msg (this->name,
                                                     GF_LOG_WARNING, 0,
                                                     DHT_MSG_LAYOUT_SET_FAILED,
                                                     "failed to link the layout"
                                                     " in inode");
                                        inode_unref (inode);
                                        inode = NULL;
                                }
                        }
                }
                list_add_tail (&entry->list, &entries.list);
                count++;
        }
        op_ret = count;

done:
        if (count == 0) {
                /* non-zero next_offset means that
                   EOF is not yet hit on the current subvol
                */
                if (next_offset != 0) {
                        next_subvol = prev->this;
                } else {
                        goto unwind;
                }

                STACK_WIND (frame, tier_readdirp_cbk,
                            next_subvol, next_subvol->fops->readdirp,
                            local->fd, local->size, next_offset,
                            local->xattr);
                return 0;
        }

unwind:
        if (op_ret < 0)
                op_ret = 0;

        DHT_STACK_UNWIND (readdirp, frame, op_ret, op_errno, &entries, NULL);

        gf_dirent_free (&entries);

        return 0;
}

int
tier_do_readdir (call_frame_t *frame, xlator_t *this, fd_t *fd, size_t size,
                off_t yoff, int whichop, dict_t *dict)
{
        dht_local_t  *local         = NULL;
        int           op_errno      = -1;
        xlator_t     *hashed_subvol = NULL;
        int           ret           = 0;
        dht_conf_t   *conf          = NULL;

        VALIDATE_OR_GOTO (frame, err);
        VALIDATE_OR_GOTO (this, err);
        VALIDATE_OR_GOTO (fd, err);
        VALIDATE_OR_GOTO (this->private, err);

        conf = this->private;

        local = dht_local_init (frame, NULL, NULL, whichop);
        if (!local) {
                op_errno = ENOMEM;
                goto err;
        }

        local->fd = fd_ref (fd);
        local->size = size;
        local->xattr_req = (dict) ? dict_ref (dict) : NULL;

        hashed_subvol = TIER_HASHED_SUBVOL;


        /* TODO: do proper readdir */
        if (whichop == GF_FOP_READDIRP) {
                if (dict)
                        local->xattr = dict_ref (dict);
                else
                        local->xattr = dict_new ();

                if (local->xattr) {
                        ret = dict_set_uint32 (local->xattr,
                                               conf->link_xattr_name, 256);
                        if (ret)
                                gf_msg (this->name, GF_LOG_WARNING, 0,
                                        DHT_MSG_DICT_SET_FAILED,
                                        "Failed to set dictionary value"
                                        " : key = %s",
                                        conf->link_xattr_name);

                }

                STACK_WIND (frame, tier_readdirp_cbk, hashed_subvol,
                            hashed_subvol->fops->readdirp,
                            fd, size, yoff, local->xattr);

        } else {
                STACK_WIND (frame, tier_readdir_cbk, hashed_subvol,
                            hashed_subvol->fops->readdir,
                            fd, size, yoff, local->xattr);
        }

        return 0;

err:
        op_errno = (op_errno == -1) ? errno : op_errno;
        DHT_STACK_UNWIND (readdir, frame, -1, op_errno, NULL, NULL);

        return 0;
}

int
tier_readdir (call_frame_t *frame, xlator_t *this, fd_t *fd, size_t size,
             off_t yoff, dict_t *xdata)
{
        int          op = GF_FOP_READDIR;
        dht_conf_t  *conf = NULL;
        int          i = 0;

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
        tier_do_readdir (frame, this, fd, size, yoff, op, 0);
        return 0;
}

int
tier_readdirp (call_frame_t *frame, xlator_t *this, fd_t *fd, size_t size,
               off_t yoff, dict_t *dict)
{
        tier_do_readdir (frame, this, fd, size, yoff, GF_FOP_READDIRP, dict);
        return 0;
}
