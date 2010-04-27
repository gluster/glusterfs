/*
  Copyright (c) 2007-2009 Gluster, Inc. <http://www.gluster.com>
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

/**
 * xlators/cluster/stripe:
 *    Stripe translator, stripes the data accross its child nodes,
 *    as per the options given in the volfile. The striping works
 *    fairly simple. It writes files at different offset as per
 *    calculation. So, 'ls -l' output at the real posix level will
 *    show file size bigger than the actual size. But when one does
 *    'df' or 'du <file>', real size of the file on the server is shown.
 *
 * WARNING:
 *  Stripe translator can't regenerate data if a child node gets disconnected.
 *  So, no 'self-heal' for stripe. Hence the advice, use stripe only when its
 *  very much necessary, or else, use it in combination with AFR, to have a
 *  backup copy.
 */

/* TODO:
 * 1. Implement basic self-heal ability to manage the basic backend
 *    layout missmatch.
 *
 */

#include "stripe.h"

/**
 * stripe_get_matching_bs - Get the matching block size for the given path.
 */
int32_t
stripe_get_matching_bs (const char *path, struct stripe_options *opts,
                        uint64_t default_bs)
{
        struct stripe_options *trav       = NULL;
        char                  *pathname   = NULL;
        uint64_t               block_size = 0;

        block_size = default_bs;
        pathname   = gf_strdup (path);
        trav       = opts;

        while (trav) {
                if (!fnmatch (trav->path_pattern, pathname, FNM_NOESCAPE)) {
                        block_size = trav->block_size;
                        break;
                }
                trav = trav->next;
        }

        GF_FREE (pathname);

        return block_size;
}


/*
 * stripe_common_remove_cbk -
 */
int32_t
stripe_common_remove_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                          int32_t op_ret, int32_t op_errno,
                          struct iatt *preparent, struct iatt *postparent)
{
        STACK_UNWIND (frame, op_ret, op_errno, preparent, postparent);
        return 0;
}


/**
 * stripe_common_cbk -  This function is used for all the _cbk without
 *     any extra arguments (other than the minimum given)
 * This is called from functions like fsync,unlink,rmdir etc.
 *
 */
int32_t
stripe_common_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                   int32_t op_ret, int32_t op_errno)
{
        int32_t         callcnt = 0;
        stripe_local_t *local   = NULL;
        call_frame_t   *prev = NULL;

        prev  = cookie;
        local = frame->local;

        LOCK (&frame->lock);
        {
                callcnt = --local->call_count;

                if (op_ret == -1) {
                        gf_log (this->name, GF_LOG_DEBUG,
                                "%s returned %s",
                                prev->this->name, strerror (op_errno));
                        local->op_errno = op_errno;
                        if ((op_errno != ENOENT) ||
                            (prev->this == FIRST_CHILD (this)))
                                local->failed = 1;
                }
                if (op_ret >= 0)
                        local->op_ret = op_ret;
        }
        UNLOCK (&frame->lock);

        if (!callcnt) {
                if (local->failed)
                        local->op_ret = -1;

                if (local->loc.path)
                        loc_wipe (&local->loc);
                if (local->loc2.path)
                        loc_wipe (&local->loc2);

                STACK_UNWIND (frame, local->op_ret, local->op_errno);
        }
        return 0;
}



int32_t
stripe_fsync_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                  int32_t op_ret, int32_t op_errno, struct iatt *prebuf,
                  struct iatt *postbuf)
{
        int32_t         callcnt = 0;
        stripe_local_t *local   = NULL;
        call_frame_t   *prev = NULL;

        prev = cookie;
        local = frame->local;

        LOCK (&frame->lock);
        {
                callcnt = --local->call_count;

                if (op_ret == -1) {
                        gf_log (this->name, GF_LOG_DEBUG,
                                "%s returned %s",
                                prev->this->name, strerror (op_errno));
                        local->op_errno = op_errno;
                        if ((op_errno != ENOENT) ||
                            (prev->this == FIRST_CHILD (this)))
                                local->failed = 1;
                }
                if (op_ret >= 0) {
                        local->op_ret = op_ret;
                        if (FIRST_CHILD(this) == prev->this) {
                                local->pre_buf  = *prebuf;
                                local->post_buf = *postbuf;
                        }
                        local->prebuf_blocks  += prebuf->ia_blocks;
                        local->postbuf_blocks += postbuf->ia_blocks;

                        if (local->prebuf_size < prebuf->ia_size)
                                local->prebuf_size = prebuf->ia_size;

                        if (local->postbuf_size < postbuf->ia_size)
                                local->postbuf_size = postbuf->ia_size;
                }
        }
        UNLOCK (&frame->lock);

        if (!callcnt) {
                if (local->failed)
                        local->op_ret = -1;

                if (local->loc.path)
                        loc_wipe (&local->loc);
                if (local->loc2.path)
                        loc_wipe (&local->loc2);

                if (local->op_ret != -1) {
                        local->pre_buf.ia_blocks  = local->prebuf_blocks;
                        local->pre_buf.ia_size    = local->prebuf_size;
                        local->post_buf.ia_blocks = local->postbuf_blocks;
                        local->post_buf.ia_size   = local->postbuf_size;
                }
                STACK_UNWIND (frame, local->op_ret, local->op_errno,
                              &local->pre_buf, &local->post_buf);
        }
        return 0;
}


/**
 * stripe_unlink_cbk -
 * This is called from functions like unlink,rmdir.
 *
 */
int32_t
stripe_unlink_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                   int32_t op_ret, int32_t op_errno, struct iatt *preparent,
                   struct iatt *postparent)
{
        int32_t         callcnt = 0;
        stripe_local_t *local   = NULL;
        call_frame_t   *prev = NULL;

        prev  = cookie;
        local = frame->local;

        LOCK (&frame->lock);
        {
                callcnt = --local->call_count;

                if (op_ret == -1) {
                        gf_log (this->name, GF_LOG_DEBUG,
                                "%s returned %s",
                                prev->this->name,
                                strerror (op_errno));
                        local->op_errno = op_errno;
                        if ((op_errno != ENOENT) ||
                            (prev->this == FIRST_CHILD (this)))
                                local->failed = 1;
                }
                if (op_ret >= 0) {
                        local->op_ret = op_ret;
                        if (FIRST_CHILD(this) == prev->this) {
                                local->preparent  = *preparent;
                                local->postparent = *postparent;
                        }
                        local->preparent_blocks  += preparent->ia_blocks;
                        local->postparent_blocks += postparent->ia_blocks;

                        if (local->preparent_size < preparent->ia_size)
                                local->preparent_size = preparent->ia_size;

                        if (local->postparent_size < postparent->ia_size)
                                local->postparent_size = postparent->ia_size;
                }
        }
        UNLOCK (&frame->lock);

        if (!callcnt) {
                if (local->failed)
                        local->op_ret = -1;

                if (local->loc.path)
                        loc_wipe (&local->loc);
                if (local->loc2.path)
                        loc_wipe (&local->loc2);

                if (local->op_ret != -1) {
                        local->preparent.ia_blocks  = local->preparent_blocks;
                        local->preparent.ia_size    = local->preparent_size;
                        local->postparent.ia_blocks = local->postparent_blocks;
                        local->postparent.ia_size   = local->postparent_size;
                }
                STACK_UNWIND (frame, local->op_ret, local->op_errno,
                              &local->preparent, &local->postparent);
        }
        return 0;
}

int32_t
stripe_truncate_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                     int32_t op_ret, int32_t op_errno, struct iatt *prebuf,
                     struct iatt *postbuf)
{
        int32_t         callcnt = 0;
        stripe_local_t *local = NULL;
        call_frame_t   *prev = NULL;

        prev  = cookie;
        local = frame->local;

        LOCK (&frame->lock);
        {
                callcnt = --local->call_count;

                if (op_ret == -1) {
                        gf_log (this->name, GF_LOG_DEBUG,
                                "%s returned error %s",
                                prev->this->name, strerror (op_errno));
                        local->op_errno = op_errno;
                        if ((op_errno != ENOENT) ||
                            (prev->this == FIRST_CHILD (this)))
                                local->failed = 1;
                }

                if (op_ret == 0) {
                        local->op_ret = 0;
                        if (FIRST_CHILD(this) == prev->this) {
                                local->pre_buf  = *prebuf;
                                local->post_buf = *postbuf;
                        }

                        local->prebuf_blocks  += prebuf->ia_blocks;
                        local->postbuf_blocks += postbuf->ia_blocks;

                        if (local->prebuf_size < prebuf->ia_size)
                                local->prebuf_size = prebuf->ia_size;

                        if (local->postbuf_size < postbuf->ia_size)
                                local->postbuf_size = postbuf->ia_size;
                }
        }
        UNLOCK (&frame->lock);

        if (!callcnt) {
                if (local->failed)
                        local->op_ret = -1;

                if (local->loc.path)
                        loc_wipe (&local->loc);
                if (local->loc2.path)
                        loc_wipe (&local->loc2);

                if (local->op_ret != -1) {
                        local->pre_buf.ia_blocks  = local->prebuf_blocks;
                        local->pre_buf.ia_size    = local->prebuf_size;
                        local->post_buf.ia_blocks = local->postbuf_blocks;
                        local->post_buf.ia_size   = local->postbuf_size;
                }

                STACK_UNWIND (frame, local->op_ret, local->op_errno,
                              &local->pre_buf, &local->post_buf);
        }

        return 0;
}


int32_t
stripe_common_buf_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                       int32_t op_ret, int32_t op_errno, struct iatt *buf)
{
        STACK_UNWIND (frame, op_ret, op_errno, buf);
        return 0;
}

/**
 * stripe_buf_cbk -  This function is used for all the _cbk with
 *    'struct iatt *buf' as extra argument (other than minimum)
 * This is called from functions like, chmod, fchmod, chown, fchown,
 * truncate, ftruncate, utimens etc.
 *
 * @cookie - this argument should be always 'xlator_t *' of child node
 */

int32_t
stripe_buf_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                int32_t op_ret, int32_t op_errno, struct iatt *buf)
{
        int32_t         callcnt = 0;
        stripe_local_t *local = NULL;
        call_frame_t   *prev = NULL;

        prev  = cookie;
        local = frame->local;

        LOCK (&frame->lock);
        {
                callcnt = --local->call_count;

                if (op_ret == -1) {
                        gf_log (this->name, GF_LOG_DEBUG,
                                "%s returned error %s",
                                prev->this->name, strerror (op_errno));
                        local->op_errno = op_errno;
                        if ((op_errno != ENOENT) ||
                            (prev->this == FIRST_CHILD (this)))
                                local->failed = 1;
                }

                if (op_ret == 0) {
                        local->op_ret = 0;

                        if (FIRST_CHILD(this) == prev->this) {
                                local->stbuf = *buf;
                        }

                        local->stbuf_blocks += buf->ia_blocks;
                        if (local->stbuf_size < buf->ia_size)
                                local->stbuf_size = buf->ia_size;
                }
        }
        UNLOCK (&frame->lock);

        if (!callcnt) {
                if (local->failed)
                        local->op_ret = -1;

                if (local->loc.path)
                        loc_wipe (&local->loc);
                if (local->loc2.path)
                        loc_wipe (&local->loc2);

                if (local->op_ret != -1) {
                        local->stbuf.ia_size   = local->stbuf_size;
                        local->stbuf.ia_blocks = local->stbuf_blocks;
                }

                STACK_UNWIND (frame, local->op_ret, local->op_errno,
                              &local->stbuf);
        }

        return 0;
}

/* In case of symlink, mknod, the file is created on just first node */
int32_t
stripe_common_inode_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                         int32_t op_ret, int32_t op_errno, inode_t *inode,
                         struct iatt *buf, struct iatt *preparent,
                         struct iatt *postparent)
{
        STACK_UNWIND (frame, op_ret, op_errno, inode, buf, preparent,
                      postparent);
        return 0;
}

/**
 * stripe_inode_cbk - This is called by the function like,
 *                   link (), symlink (), mkdir (), mknod ()
 *           This creates a inode for new inode. It keeps a list of all
 *           the inodes received from the child nodes. It is used while
 *           forwarding any fops to child nodes.
 *
 */
int32_t
stripe_inode_cbk (call_frame_t *frame, void *cookie,
                  xlator_t *this, int32_t op_ret,
                  int32_t op_errno, inode_t *inode,
                  struct iatt *buf, struct iatt *preparent,
                  struct iatt *postparent)
{
        int32_t         callcnt = 0;
        stripe_local_t  *local   = NULL;
        inode_t         *local_inode = NULL;
        call_frame_t    *prev = NULL;

        prev  = cookie;
        local = frame->local;

        LOCK (&frame->lock);
        {
                callcnt = --local->call_count;

                if (op_ret == -1) {
                        gf_log (this->name, GF_LOG_DEBUG,
                                "%s returned error %s",
                                prev->this->name, strerror (op_errno));
                        local->op_errno = op_errno;
                        if ((op_errno != ENOENT) ||
                            (prev->this == FIRST_CHILD (this)))
                                local->failed = 1;
                }

                if (op_ret >= 0) {
                        local->op_ret = 0;

                        if (FIRST_CHILD(this) == prev->this) {
                                local->inode      = inode_ref (inode);
                                local->stbuf      = *buf;
                                local->postparent = *postparent;
                                local->preparent  = *preparent;
                        }
                        local->stbuf_blocks      += buf->ia_blocks;
                        local->preparent_blocks  += preparent->ia_blocks;
                        local->postparent_blocks += postparent->ia_blocks;

                        if (local->stbuf_size < buf->ia_size)
                                local->stbuf_size = buf->ia_size;
                        if (local->preparent_size < preparent->ia_size)
                                local->preparent_size = preparent->ia_size;
                        if (local->postparent_size < postparent->ia_size)
                                local->postparent_size = postparent->ia_size;
                }
        }
        UNLOCK (&frame->lock);

        if (!callcnt) {
                if (local->failed)
                        local->op_ret = -1;

                local_inode = local->inode;

                if (local->op_ret != -1) {
                        local->preparent.ia_blocks  = local->preparent_blocks;
                        local->preparent.ia_size    = local->preparent_size;
                        local->postparent.ia_blocks = local->postparent_blocks;
                        local->postparent.ia_size   = local->postparent_size;
                        local->stbuf.ia_size        = local->stbuf_size;
                        local->stbuf.ia_blocks      = local->stbuf_blocks;
                }
                STACK_UNWIND (frame, local->op_ret, local->op_errno,
                              local->inode, &local->stbuf,
                              &local->preparent, &local->postparent);

                if (local_inode)
                        inode_unref (local_inode);
        }

        return 0;
}

int32_t
stripe_sh_chown_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                     int32_t op_ret, int32_t op_errno,
                     struct iatt *preop, struct iatt *postop)
{
        int             callcnt = -1;
        stripe_local_t *local   = NULL;

        local = frame->local;

        LOCK (&frame->lock);
        {
                callcnt = --local->call_count;
        }
        UNLOCK (&frame->lock);

        if (!callcnt) {
                loc_wipe (&local->loc);
                STACK_DESTROY (frame->root);
        }
        return 0;
}

int32_t
stripe_sh_make_entry_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                          int32_t op_ret, int32_t op_errno, inode_t *inode,
                          struct iatt *buf, struct iatt *preparent,
                          struct iatt *postparent)
{
        stripe_local_t *local = NULL;
        call_frame_t    *prev = NULL;

        prev  = cookie;
        local = frame->local;

        STACK_WIND (frame, stripe_sh_chown_cbk, prev->this,
                    prev->this->fops->setattr, &local->loc,
                    &local->stbuf, (GF_SET_ATTR_UID | GF_SET_ATTR_GID));

        return 0;
}

int32_t
stripe_entry_self_heal (call_frame_t *frame, xlator_t *this,
                        stripe_local_t *local)
{
        xlator_list_t    *trav   = NULL;
        call_frame_t     *rframe = NULL;
        stripe_local_t   *rlocal = NULL;
        stripe_private_t *priv   = NULL;

        if (!(IA_ISREG (local->stbuf.ia_type) ||
              IA_ISDIR (local->stbuf.ia_type)))
                return 0;

        priv = this->private;
        trav = this->children;
        rframe = copy_frame (frame);
        if (!rframe) {
                goto out;
        }
        rlocal = GF_CALLOC (1, sizeof (stripe_local_t),
                            gf_stripe_mt_stripe_local_t);
        if (!rlocal) {
                goto out;
        }
        rframe->local = rlocal;
        rlocal->call_count = priv->child_count;
        loc_copy (&rlocal->loc, &local->loc);
        memcpy (&rlocal->stbuf, &local->stbuf, sizeof (struct iatt)); /* "Before it was struct stat" */

        while (trav) {
                if (IA_ISREG (local->stbuf.ia_type)) {
                        STACK_WIND (rframe, stripe_sh_make_entry_cbk,
                                    trav->xlator, trav->xlator->fops->mknod,
                                    &local->loc,
                                    st_mode_from_ia (local->stbuf.ia_prot,
                                                     local->stbuf.ia_type), 0);
                }
                if (IA_ISREG (local->stbuf.ia_type)) {
                        STACK_WIND (rframe, stripe_sh_make_entry_cbk,
                                    trav->xlator, trav->xlator->fops->mkdir,
                                    &local->loc, st_mode_from_ia (local->stbuf.ia_prot,
                                                                  local->stbuf.ia_type));
                }
                trav = trav->next;
        }

out:
        return 0;
}

int32_t
stripe_lookup_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                   int32_t op_ret, int32_t op_errno, inode_t *inode,
                   struct iatt *buf, dict_t *dict, struct iatt *postparent)
{
        int32_t         callcnt = 0;
        dict_t         *tmp_dict = NULL;
        inode_t        *tmp_inode = NULL;
        stripe_local_t *local = NULL;
        call_frame_t   *prev = NULL;

        prev = cookie;
        local = frame->local;

        LOCK (&frame->lock);
        {
                callcnt = --local->call_count;

                if (op_ret == -1) {
                        if (op_errno != ENOENT)
                                gf_log (this->name, GF_LOG_DEBUG,
                                        "%s returned error %s",
                                        prev->this->name,
                                        strerror (op_errno));
                        if (local->op_errno != ESTALE)
                                local->op_errno = op_errno;
                        if ((op_errno != ENOENT) ||
                            (prev->this == FIRST_CHILD (this)))
                                local->failed = 1;
                        if (op_errno == ENOENT)
                                local->entry_self_heal_needed = 1;
                }

                if (op_ret >= 0) {
                        local->op_ret = 0;

                        if (FIRST_CHILD(this) == prev->this) {
                                local->stbuf      = *buf;
                                local->postparent = *postparent;
                                local->inode = inode_ref (inode);
                                local->dict = dict_ref (dict);
                        }
                        local->stbuf_blocks      += buf->ia_blocks;
                        local->postparent_blocks += postparent->ia_blocks;

                        if (local->stbuf_size < buf->ia_size)
                                local->stbuf_size = buf->ia_size;
                        if (local->postparent_size < postparent->ia_size)
                                local->postparent_size = postparent->ia_size;
                }
        }
        UNLOCK (&frame->lock);

        if (!callcnt) {
                if (local->op_ret == 0 && local->entry_self_heal_needed)
                        stripe_entry_self_heal (frame, this, local);

                if (local->failed)
                        local->op_ret = -1;

                tmp_dict  = local->dict;
                tmp_inode = local->inode;

                if (local->op_ret != -1) {
                        local->stbuf.ia_blocks      = local->stbuf_blocks;
                        local->stbuf.ia_size        = local->stbuf_size;
                        local->postparent.ia_blocks = local->postparent_blocks;
                        local->postparent.ia_size   = local->postparent_size;
                }

                loc_wipe (&local->loc);

                STACK_UNWIND (frame, local->op_ret, local->op_errno,
                              local->inode, &local->stbuf, local->dict,
                              &local->postparent);

                if (tmp_inode)
                        inode_unref (tmp_inode);
                if (tmp_dict)
                        dict_unref (tmp_dict);
        }

        return 0;
}

/**
 * stripe_lookup -
 */
int32_t
stripe_lookup (call_frame_t *frame, xlator_t *this, loc_t *loc,
               dict_t *xattr_req)
{
        stripe_local_t   *local = NULL;
        xlator_list_t    *trav = NULL;
        stripe_private_t *priv = NULL;
        int32_t           op_errno = 1;

        VALIDATE_OR_GOTO (frame, err);
        VALIDATE_OR_GOTO (this, err);
        VALIDATE_OR_GOTO (loc, err);
        VALIDATE_OR_GOTO (loc->path, err);
        VALIDATE_OR_GOTO (loc->inode, err);

        priv = this->private;
        trav = this->children;

        /* Initialization */
        local = GF_CALLOC (1, sizeof (stripe_local_t),
                           gf_stripe_mt_stripe_local_t);
        if (!local) {
                op_errno = ENOMEM;
                goto err;
        }
        local->op_ret = -1;
        frame->local = local;
        loc_copy (&local->loc, loc);

        /* Everytime in stripe lookup, all child nodes
           should be looked up */
        local->call_count = priv->child_count;
        while (trav) {
                STACK_WIND (frame, stripe_lookup_cbk, trav->xlator,
                            trav->xlator->fops->lookup,
                            loc, xattr_req);
                trav = trav->next;
        }

        return 0;
 err:
        STACK_UNWIND (frame, -1, op_errno, NULL, NULL, NULL);
        return 0;
}

/**
 * stripe_stat -
 */
int32_t
stripe_stat (call_frame_t *frame, xlator_t *this, loc_t *loc)
{
        xlator_list_t    *trav = NULL;
        stripe_local_t   *local = NULL;
        stripe_private_t *priv = NULL;
        int32_t           op_errno = 1;

        VALIDATE_OR_GOTO (frame, err);
        VALIDATE_OR_GOTO (this, err);
        VALIDATE_OR_GOTO (loc, err);
        VALIDATE_OR_GOTO (loc->path, err);
        VALIDATE_OR_GOTO (loc->inode, err);

        priv = this->private;
        trav = this->children;

        if (priv->first_child_down) {
                op_errno = ENOTCONN;
                goto err;
        }

        /* Initialization */
        local = GF_CALLOC (1, sizeof (stripe_local_t),
                           gf_stripe_mt_stripe_local_t);
        if (!local) {
                op_errno = ENOMEM;
                goto err;
        }
        local->op_ret = -1;
        frame->local = local;
        local->call_count = priv->child_count;

        while (trav) {
                STACK_WIND (frame, stripe_buf_cbk, trav->xlator,
                            trav->xlator->fops->stat, loc);
                trav = trav->next;
        }

        return 0;

err:
        STACK_UNWIND (frame, -1, op_errno, NULL);
        return 0;
}


/**
 * stripe_statfs_cbk -
 */
int32_t
stripe_statfs_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                   int32_t op_ret, int32_t op_errno, struct statvfs *stbuf)
{
        stripe_local_t *local = NULL;
        int32_t         callcnt = 0;

        local = frame->local;

        LOCK(&frame->lock);
        {
                callcnt = --local->call_count;

                if (op_ret && (op_errno != ENOTCONN)) {
                        local->op_errno = op_errno;
                }
                if (op_ret == 0) {
                        struct statvfs *dict_buf = &local->statvfs_buf;
                        dict_buf->f_bsize   = stbuf->f_bsize;
                        dict_buf->f_frsize  = stbuf->f_frsize;
                        dict_buf->f_blocks += stbuf->f_blocks;
                        dict_buf->f_bfree  += stbuf->f_bfree;
                        dict_buf->f_bavail += stbuf->f_bavail;
                        dict_buf->f_files  += stbuf->f_files;
                        dict_buf->f_ffree  += stbuf->f_ffree;
                        dict_buf->f_favail += stbuf->f_favail;
                        dict_buf->f_fsid    = stbuf->f_fsid;
                        dict_buf->f_flag    = stbuf->f_flag;
                        dict_buf->f_namemax = stbuf->f_namemax;
                        local->op_ret = 0;
                }
        }
        UNLOCK (&frame->lock);

        if (!callcnt) {
                STACK_UNWIND (frame, local->op_ret,
                              local->op_errno, &local->statvfs_buf);
        }

        return 0;
}


/**
 * stripe_statfs -
 */
int32_t
stripe_statfs (call_frame_t *frame, xlator_t *this, loc_t *loc)
{
        stripe_local_t   *local = NULL;
        xlator_list_t    *trav = NULL;
        stripe_private_t *priv = NULL;
        int32_t           op_errno = 1;

        trav = this->children;
        priv = this->private;

        /* Initialization */
        local = GF_CALLOC (1, sizeof (stripe_local_t),
                           gf_stripe_mt_stripe_local_t);
        if (!local) {
                op_errno = ENOMEM;
                goto err;
        }
        local->op_ret = -1;
        local->op_errno = ENOTCONN;
        frame->local = local;

        local->call_count = priv->child_count;
        while (trav) {
                STACK_WIND (frame, stripe_statfs_cbk, trav->xlator,
                            trav->xlator->fops->statfs, loc);
                trav = trav->next;
        }

        return 0;
 err:
        STACK_UNWIND (frame, -1, op_errno, NULL);
        return 0;
}


/**
 * stripe_truncate -
 */
int32_t
stripe_truncate (call_frame_t *frame, xlator_t *this, loc_t *loc, off_t offset)
{
        xlator_list_t    *trav = NULL;
        stripe_local_t   *local = NULL;
        stripe_private_t *priv = NULL;
        int32_t           op_errno = 1;

        VALIDATE_OR_GOTO (frame, err);
        VALIDATE_OR_GOTO (this, err);
        VALIDATE_OR_GOTO (loc, err);
        VALIDATE_OR_GOTO (loc->path, err);
        VALIDATE_OR_GOTO (loc->inode, err);

        priv = this->private;
        trav = this->children;

        if (priv->first_child_down) {
                op_errno = ENOTCONN;
                goto err;
        }

        /* Initialization */
        local = GF_CALLOC (1, sizeof (stripe_local_t),
                           gf_stripe_mt_stripe_local_t);
        if (!local) {
                op_errno = ENOMEM;
                goto err;
        }
        local->op_ret = -1;
        frame->local = local;
        local->call_count = priv->child_count;

        while (trav) {
                STACK_WIND (frame, stripe_truncate_cbk, trav->xlator,
                            trav->xlator->fops->truncate, loc, offset);
                trav = trav->next;
        }

        return 0;
 err:
        STACK_UNWIND (frame, -1, op_errno, NULL, NULL);
        return 0;
}


int32_t
stripe_setattr_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                    int32_t op_ret, int32_t op_errno,
                    struct iatt *preop, struct iatt *postop)
{
        int32_t         callcnt = 0;
        stripe_local_t *local = NULL;
        call_frame_t   *prev = NULL;

        prev  = cookie;
        local = frame->local;

        LOCK (&frame->lock);
        {
                callcnt = --local->call_count;

                if (op_ret == -1) {
                        gf_log (this->name, GF_LOG_DEBUG,
                                "%s returned error %s",
                                prev->this->name, strerror (op_errno));
                        local->op_errno = op_errno;
                        if ((op_errno != ENOENT) ||
                            (prev->this == FIRST_CHILD (this)))
                                local->failed = 1;
                }

                if (op_ret == 0) {
                        local->op_ret = 0;

                        if (FIRST_CHILD(this) == prev->this) {
                                local->pre_buf  = *preop;
                                local->post_buf = *postop;
                        }

                        local->prebuf_blocks  += preop->ia_blocks;
                        local->postbuf_blocks += postop->ia_blocks;

                        if (local->prebuf_size < preop->ia_size)
                                local->prebuf_size = preop->ia_size;
                        if (local->postbuf_size < postop->ia_size)
                                local->postbuf_size = postop->ia_size;
                }
        }
        UNLOCK (&frame->lock);

        if (!callcnt) {
                if (local->failed)
                        local->op_ret = -1;

                if (local->loc.path)
                        loc_wipe (&local->loc);
                if (local->loc2.path)
                        loc_wipe (&local->loc2);

                if (local->op_ret != -1) {
                        local->pre_buf.ia_blocks  = local->prebuf_blocks;
                        local->pre_buf.ia_size    = local->prebuf_size;
                        local->post_buf.ia_blocks = local->postbuf_blocks;
                        local->post_buf.ia_size   = local->postbuf_size;
                }

                STACK_UNWIND (frame, local->op_ret, local->op_errno,
                              &local->pre_buf, &local->post_buf);
        }

        return 0;
}


int32_t
stripe_setattr (call_frame_t *frame, xlator_t *this, loc_t *loc,
                struct iatt *stbuf, int32_t valid)
{
        xlator_list_t    *trav = NULL;
        stripe_local_t   *local = NULL;
        stripe_private_t *priv = NULL;
        int32_t           op_errno = 1;

        VALIDATE_OR_GOTO (frame, err);
        VALIDATE_OR_GOTO (this, err);
        VALIDATE_OR_GOTO (loc, err);
        VALIDATE_OR_GOTO (loc->path, err);
        VALIDATE_OR_GOTO (loc->inode, err);

        priv = this->private;
        trav = this->children;

        if (priv->first_child_down) {
                op_errno = ENOTCONN;
                goto err;
        }

        /* Initialization */
        local = GF_CALLOC (1, sizeof (stripe_local_t),
                           gf_stripe_mt_stripe_local_t);
        if (!local) {
                op_errno = ENOMEM;
                goto err;
        }
        local->op_ret = -1;
        frame->local = local;
        local->call_count = priv->child_count;

        while (trav) {
                STACK_WIND (frame, stripe_setattr_cbk,
                            trav->xlator, trav->xlator->fops->setattr,
                            loc, stbuf, valid);
                trav = trav->next;
        }

        return 0;
 err:
        STACK_UNWIND (frame, -1, op_errno, NULL, NULL);
        return 0;
}


int32_t
stripe_fsetattr (call_frame_t *frame, xlator_t *this, fd_t *fd,
                 struct iatt *stbuf, int32_t valid)
{
        stripe_local_t   *local = NULL;
        stripe_private_t *priv = NULL;
        xlator_list_t    *trav = NULL;
        int32_t           op_errno = 1;

        VALIDATE_OR_GOTO (frame, err);
        VALIDATE_OR_GOTO (this, err);
        VALIDATE_OR_GOTO (fd, err);
        VALIDATE_OR_GOTO (fd->inode, err);

        priv = this->private;
        trav = this->children;

        /* Initialization */
        local = GF_CALLOC (1, sizeof (stripe_local_t),
                           gf_stripe_mt_stripe_local_t);
        if (!local) {
                op_errno = ENOMEM;
                goto err;
        }
        local->op_ret = -1;
        frame->local = local;
        local->call_count = priv->child_count;

        while (trav) {
                STACK_WIND (frame, stripe_setattr_cbk, trav->xlator,
                            trav->xlator->fops->fsetattr, fd, stbuf, valid);
                trav = trav->next;
        }

        return 0;
 err:
        STACK_UNWIND (frame, -1, op_errno, NULL, NULL);
        return 0;
}

int32_t
stripe_stack_rename_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                         int32_t op_ret, int32_t op_errno, struct iatt *buf,
                         struct iatt *preoldparent, struct iatt *postoldparent,
                         struct iatt *prenewparent, struct iatt *postnewparent)
{
        int32_t         callcnt = 0;
        stripe_local_t *local = NULL;
        call_frame_t   *prev = NULL;

        prev  = cookie;
        local = frame->local;

        LOCK (&frame->lock);
        {
                callcnt = --local->call_count;

                if (op_ret == -1) {
                        gf_log (this->name, GF_LOG_DEBUG,
                                "%s returned error %s",
                                prev->this->name, strerror (op_errno));
                        local->op_errno = op_errno;
                        if ((op_errno != ENOENT) ||
                            (prev->this == FIRST_CHILD (this)))
                                local->failed = 1;
                }

                if (op_ret == 0) {
                        local->op_ret = 0;

                        local->stbuf.ia_blocks      += buf->ia_blocks;
                        local->preparent.ia_blocks  += preoldparent->ia_blocks;
                        local->postparent.ia_blocks += postoldparent->ia_blocks;
                        local->pre_buf.ia_blocks    += prenewparent->ia_blocks;
                        local->post_buf.ia_blocks   += postnewparent->ia_blocks;

                        if (local->stbuf.ia_size < buf->ia_size)
                                local->stbuf.ia_size =  buf->ia_size;

                        if (local->preparent.ia_size < preoldparent->ia_size)
                                local->preparent.ia_size = preoldparent->ia_size;

                        if (local->postparent.ia_size < postoldparent->ia_size)
                                local->postparent.ia_size = postoldparent->ia_size;

                        if (local->pre_buf.ia_size < prenewparent->ia_size)
                                local->pre_buf.ia_size = prenewparent->ia_size;

                        if (local->post_buf.ia_size < postnewparent->ia_size)
                                local->post_buf.ia_size = postnewparent->ia_size;
                }
        }
        UNLOCK (&frame->lock);

        if (!callcnt) {
                if (local->failed)
                        local->op_ret = -1;

                if (local->loc.path)
                        loc_wipe (&local->loc);
                if (local->loc2.path)
                        loc_wipe (&local->loc2);

                STACK_UNWIND (frame, local->op_ret, local->op_errno,
                              &local->stbuf, &local->preparent,
                              &local->postparent,  &local->pre_buf,
                              &local->post_buf);
        }

        return 0;
}

int32_t
stripe_first_rename_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                         int32_t op_ret, int32_t op_errno, struct iatt *buf,
                         struct iatt *preoldparent, struct iatt *postoldparent,
                         struct iatt *prenewparent, struct iatt *postnewparent)
{
        stripe_local_t *local = NULL;
        xlator_list_t  *trav = NULL;

        local = frame->local;
        trav = this->children;

        if (op_ret == -1) {
                goto unwind;
        }

        local->stbuf      = *buf;
        local->preparent  = *preoldparent;
        local->postparent = *postoldparent;
        local->pre_buf    = *prenewparent;
        local->post_buf   = *postnewparent;

        local->op_ret = 0;
        local->call_count--;

        trav = trav->next; /* Skip first child */
        while (trav) {
                STACK_WIND (frame, stripe_stack_rename_cbk,
                            trav->xlator, trav->xlator->fops->rename,
                            &local->loc, &local->loc2);
                trav = trav->next;
        }
        return 0;

 unwind:
        if (local->loc.path)
                loc_wipe (&local->loc);
        if (local->loc2.path)
                loc_wipe (&local->loc2);

       STACK_UNWIND (frame, op_ret, op_errno, buf, preoldparent,
                      postoldparent, prenewparent, postnewparent);
       return 0;
}
/**
 * stripe_rename -
 */
int32_t
stripe_rename (call_frame_t *frame, xlator_t *this, loc_t *oldloc,
               loc_t *newloc)
{
        stripe_private_t *priv = NULL;
        stripe_local_t   *local = NULL;
        xlator_list_t    *trav = NULL;
        int32_t           op_errno = 1;

        VALIDATE_OR_GOTO (frame, err);
        VALIDATE_OR_GOTO (this, err);
        VALIDATE_OR_GOTO (oldloc, err);
        VALIDATE_OR_GOTO (oldloc->path, err);
        VALIDATE_OR_GOTO (oldloc->inode, err);
        VALIDATE_OR_GOTO (newloc, err);

        priv = this->private;
        trav = this->children;

        /* If any one node is down, don't allow rename */
        if (priv->nodes_down) {
                op_errno = ENOTCONN;
                goto err;
        }

        /* Initialization */
        local = GF_CALLOC (1, sizeof (stripe_local_t),
                           gf_stripe_mt_stripe_local_t);
        if (!local) {
                op_errno = ENOMEM;
                goto err;
        }
        local->op_ret = -1;
        loc_copy (&local->loc, oldloc);
        loc_copy (&local->loc2, newloc);

        local->call_count = priv->child_count;

        frame->local = local;

        STACK_WIND (frame, stripe_first_rename_cbk, trav->xlator,
                    trav->xlator->fops->rename, oldloc, newloc);

        return 0;
 err:
        STACK_UNWIND (frame, -1, op_errno, NULL, NULL, NULL, NULL, NULL);
        return 0;
}


/**
 * stripe_unlink -
 */
int32_t
stripe_unlink (call_frame_t *frame, xlator_t *this, loc_t *loc)
{
        xlator_list_t    *trav = NULL;
        stripe_local_t   *local = NULL;
        stripe_private_t *priv = NULL;
        int32_t           op_errno = 1;

        VALIDATE_OR_GOTO (frame, err);
        VALIDATE_OR_GOTO (this, err);
        VALIDATE_OR_GOTO (loc, err);
        VALIDATE_OR_GOTO (loc->path, err);
        VALIDATE_OR_GOTO (loc->inode, err);

        priv = this->private;
        trav = this->children;

        if (priv->first_child_down) {
                op_errno = ENOTCONN;
                goto err;
        }

        /* Don't unlink a file if a node is down */
        if (priv->nodes_down) {
                op_errno = ENOTCONN;
                goto err;
        }

        /* Initialization */
        local = GF_CALLOC (1, sizeof (stripe_local_t),
                           gf_stripe_mt_stripe_local_t);
        if (!local) {
                op_errno = ENOMEM;
                goto err;
        }
        local->op_ret = -1;
        frame->local = local;
        local->call_count = priv->child_count;

        while (trav) {
                STACK_WIND (frame, stripe_unlink_cbk,
                            trav->xlator, trav->xlator->fops->unlink,
                            loc);
                trav = trav->next;
        }

        return 0;
 err:
        STACK_UNWIND (frame, -1, op_errno, NULL, NULL);
        return 0;
}


int32_t
stripe_first_rmdir_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                        int32_t op_ret, int32_t op_errno,struct iatt *preparent,
                        struct iatt *postparent)

{
        xlator_list_t *trav = NULL;
        stripe_local_t *local = NULL;

        if (op_ret == -1) {
                STACK_UNWIND (frame, op_ret, op_errno, NULL, NULL);
                return 0;
        }

        trav = this->children;
        local = frame->local;

        local->call_count--; /* First child successful */
        trav = trav->next; /* Skip first child */

        local->preparent  = *preparent;
        local->postparent = *postparent;
        local->preparent_blocks  += preparent->ia_blocks;
        local->postparent_blocks += postparent->ia_blocks;
        local->preparent_size  = preparent->ia_size;
        local->postparent_size = postparent->ia_size;

        while (trav) {
                STACK_WIND (frame, stripe_unlink_cbk, trav->xlator,
                            trav->xlator->fops->rmdir, &local->loc);
                trav = trav->next;
        }

        return 0;
}

/**
 * stripe_rmdir -
 */
int32_t
stripe_rmdir (call_frame_t *frame, xlator_t *this, loc_t *loc)
{
        xlator_list_t    *trav = NULL;
        stripe_local_t   *local = NULL;
        stripe_private_t *priv = NULL;
        int32_t           op_errno = 1;

        VALIDATE_OR_GOTO (frame, err);
        VALIDATE_OR_GOTO (this, err);
        VALIDATE_OR_GOTO (loc, err);
        VALIDATE_OR_GOTO (loc->path, err);
        VALIDATE_OR_GOTO (loc->inode, err);

        priv = this->private;
        trav = this->children;

        /* don't delete a directory if any of the subvolume is down */
        if (priv->nodes_down) {
                op_errno = ENOTCONN;
                goto err;
        }

        /* Initialization */
        local = GF_CALLOC (1, sizeof (stripe_local_t),
                           gf_stripe_mt_stripe_local_t);
        if (!local) {
                op_errno = ENOMEM;
                goto err;
        }
        local->op_ret = -1;
        frame->local = local;
        loc_copy (&local->loc, loc);
        local->call_count = priv->child_count;

        STACK_WIND (frame, stripe_first_rmdir_cbk,  trav->xlator,
                    trav->xlator->fops->rmdir, loc);

        return 0;
 err:
        STACK_UNWIND (frame, -1, op_errno, NULL, NULL);
        return 0;
}


int32_t
stripe_mknod_ifreg_fail_unlink_cbk (call_frame_t *frame, void *cookie,
                                    xlator_t *this, int32_t op_ret,
                                    int32_t op_errno, struct iatt *preparent,
                                    struct iatt *postparent)
{
        int32_t         callcnt = 0;
        stripe_local_t *local = NULL;

        local = frame->local;

        LOCK (&frame->lock);
        {
                callcnt = --local->call_count;
        }
        UNLOCK (&frame->lock);

        if (!callcnt) {
                loc_wipe (&local->loc);
                STACK_UNWIND (frame, local->op_ret, local->op_errno,
                              local->inode, &local->stbuf,
                              &local->preparent, &local->postparent);
        }

        return 0;
}


/**
 */
int32_t
stripe_mknod_ifreg_setxattr_cbk (call_frame_t *frame, void *cookie,
                                 xlator_t *this, int32_t op_ret,
                                 int32_t op_errno)
{
        int32_t           callcnt = 0;
        stripe_local_t   *local = NULL;
        stripe_private_t *priv = NULL;
        xlator_list_t    *trav = NULL;
        call_frame_t   *prev = NULL;

        prev  = cookie;
        priv = this->private;
        local = frame->local;

	LOCK (&frame->lock);
        {
                callcnt = --local->call_count;

                if (op_ret == -1) {
                        gf_log (this->name, GF_LOG_DEBUG,
                                "%s returned error %s",
                                prev->this->name, strerror (op_errno));
                        local->op_ret = -1;
                        local->op_errno = op_errno;
                }
        }
        UNLOCK (&frame->lock);

        if (!callcnt) {
                if (local->op_ret == -1) {
                        local->call_count = priv->child_count;
                        while (trav) {
                                STACK_WIND (frame,
                                            stripe_mknod_ifreg_fail_unlink_cbk,
                                            trav->xlator,
                                            trav->xlator->fops->unlink,
                                            &local->loc);
                                trav = trav->next;
                        }
                        return 0;
                }

                loc_wipe (&local->loc);
                STACK_UNWIND (frame, local->op_ret, local->op_errno,
                              local->inode, &local->stbuf,
                              &local->preparent, &local->postparent);
        }
        return 0;
}

/**
 */
int32_t
stripe_mknod_ifreg_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                        int32_t op_ret, int32_t op_errno, inode_t *inode,
                        struct iatt *buf, struct iatt *preparent,
                        struct iatt *postparent)
{
        int               ret = 0;
        int32_t           callcnt = 0;
        stripe_local_t   *local = NULL;
        xlator_list_t    *trav = NULL;
        stripe_private_t *priv = NULL;
        call_frame_t     *prev = NULL;

        prev  = cookie;
	priv  = this->private;
        local = frame->local;

        LOCK (&frame->lock);
        {
                callcnt = --local->call_count;

                if (op_ret == -1) {
                        gf_log (this->name, GF_LOG_DEBUG,
                                "%s returned error %s",
                                prev->this->name, strerror (op_errno));
                        if ((op_errno != ENOENT) ||
                            (prev->this == FIRST_CHILD (this)))
                                local->failed = 1;
                        local->op_errno = op_errno;
                }

                if (op_ret >= 0) {
                        local->op_ret = op_ret;

                        if (FIRST_CHILD(this) == prev->this) {
                                local->stbuf      = *buf;
                                local->preparent  = *preparent;
                                local->postparent = *postparent;
                        }

                        local->stbuf_blocks += buf->ia_blocks;
                        local->preparent_blocks  += preparent->ia_blocks;
                        local->postparent_blocks += postparent->ia_blocks;

                        if (local->stbuf_size < buf->ia_size)
                                local->stbuf_size = buf->ia_size;
                        if (local->preparent_size < preparent->ia_size)
                                local->preparent_size = preparent->ia_size;
                        if (local->postparent_size < postparent->ia_size)
                                local->postparent_size = postparent->ia_size;
                }
        }
        UNLOCK (&frame->lock);

        if (!callcnt) {
                if (local->failed)
                        local->op_ret = -1;

                if (local->op_ret != -1) {
                        local->preparent.ia_blocks  = local->preparent_blocks;
                        local->preparent.ia_size    = local->preparent_size;
                        local->postparent.ia_blocks = local->postparent_blocks;
                        local->postparent.ia_size   = local->postparent_size;
                        local->stbuf.ia_size        = local->stbuf_size;
                        local->stbuf.ia_blocks      = local->stbuf_blocks;
                }

                if ((local->op_ret != -1) && priv->xattr_supported) {
                        /* Send a setxattr request to nodes where the
                           files are created */
                        int32_t index = 0;
                        char size_key[256] = {0,};
                        char index_key[256] = {0,};
                        char count_key[256] = {0,};
                        dict_t *dict = NULL;

                        trav = this->children;
                        sprintf (size_key,
                                 "trusted.%s.stripe-size", this->name);
                        sprintf (count_key,
                                 "trusted.%s.stripe-count", this->name);
                        sprintf (index_key,
                                 "trusted.%s.stripe-index", this->name);

                        local->call_count = priv->child_count;

                        while (trav) {
                                dict = get_new_dict ();
                                dict_ref (dict);
                                /* TODO: check return value */
                                ret = dict_set_int64 (dict, size_key,
                                                      local->stripe_size);
                                ret = dict_set_int32 (dict, count_key,
                                                      priv->child_count);
                                ret = dict_set_int32 (dict, index_key, index);

                                STACK_WIND (frame,
                                            stripe_mknod_ifreg_setxattr_cbk,
                                            trav->xlator,
                                            trav->xlator->fops->setxattr,
                                            &local->loc, dict, 0);

                                dict_unref (dict);
                                index++;
                                trav = trav->next;
                        }
                } else {
                        /* Create itself has failed.. so return
                           without setxattring */
                        loc_wipe (&local->loc);
                        STACK_UNWIND (frame, local->op_ret, local->op_errno,
                                      local->inode, &local->stbuf,
                                      &local->preparent, &local->postparent);
                }
        }

        return 0;
}


/**
 * stripe_mknod -
 */
int32_t
stripe_mknod (call_frame_t *frame, xlator_t *this, loc_t *loc, mode_t mode,
              dev_t rdev)
{
        stripe_private_t *priv = NULL;
        stripe_local_t   *local = NULL;
        xlator_list_t    *trav = NULL;
        int32_t           op_errno = 1;

        VALIDATE_OR_GOTO (frame, err);
        VALIDATE_OR_GOTO (this, err);
        VALIDATE_OR_GOTO (loc, err);
        VALIDATE_OR_GOTO (loc->path, err);
        VALIDATE_OR_GOTO (loc->inode, err);

        priv = this->private;
        trav = this->children;

        if (priv->first_child_down) {
                op_errno = ENOTCONN;
                goto err;
        }

        if (S_ISREG(mode)) {
                /* NOTE: on older kernels (older than 2.6.9),
                   creat() fops is sent as mknod() + open(). Hence handling
                   S_IFREG files is necessary */
                if (priv->nodes_down) {
                        gf_log (this->name, GF_LOG_WARNING,
                                "Some node down, returning EIO");
                        op_errno = EIO;
                        goto err;
                }

                /* Initialization */
                local = GF_CALLOC (1, sizeof (stripe_local_t),
                                   gf_stripe_mt_stripe_local_t);
                if (!local) {
                        op_errno = ENOMEM;
                        goto err;
                }
                local->op_ret = -1;
                local->op_errno = ENOTCONN;
                local->stripe_size = stripe_get_matching_bs (loc->path,
                                                             priv->pattern,
                                                             priv->block_size);
                frame->local = local;
                local->inode = loc->inode;
                loc_copy (&local->loc, loc);

                /* Everytime in stripe lookup, all child nodes should
                   be looked up */
                local->call_count = priv->child_count;

                while (trav) {
                        STACK_WIND (frame, stripe_mknod_ifreg_cbk,
                                    trav->xlator, trav->xlator->fops->mknod,
                                    loc, mode, rdev);
                        trav = trav->next;
                }

                /* This case is handled, no need to continue further. */
                return 0;
        }


        STACK_WIND (frame, stripe_common_inode_cbk,
                    FIRST_CHILD(this), FIRST_CHILD(this)->fops->mknod,
                    loc, mode, rdev);

        return 0;
 err:
        STACK_UNWIND (frame, -1, op_errno, NULL, NULL, NULL, NULL);
        return 0;
}


/**
 * stripe_mkdir -
 */
int32_t
stripe_mkdir (call_frame_t *frame, xlator_t *this, loc_t *loc, mode_t mode)
{
        stripe_private_t *priv = NULL;
        stripe_local_t   *local = NULL;
        xlator_list_t    *trav = NULL;
        int32_t           op_errno = 1;

        VALIDATE_OR_GOTO (frame, err);
        VALIDATE_OR_GOTO (this, err);
        VALIDATE_OR_GOTO (loc, err);
        VALIDATE_OR_GOTO (loc->path, err);
        VALIDATE_OR_GOTO (loc->inode, err);

        priv = this->private;
        trav = this->children;

        if (priv->first_child_down) {
                op_errno = ENOTCONN;
                goto err;
        }

        /* Initialization */
        local = GF_CALLOC (1, sizeof (stripe_local_t),
                           gf_stripe_mt_stripe_local_t);
        if (!local) {
                op_errno = ENOMEM;
                goto err;
        }
        local->op_ret = -1;
        local->call_count = priv->child_count;
        frame->local = local;

        /* Everytime in stripe lookup, all child nodes should be looked up */
        while (trav) {
                STACK_WIND (frame, stripe_inode_cbk,
                            trav->xlator, trav->xlator->fops->mkdir,
                            loc, mode);
                trav = trav->next;
        }

        return 0;
 err:
        STACK_UNWIND (frame, -1, op_errno, NULL, NULL, NULL, NULL);
        return 0;
}


/**
 * stripe_link -
 */
int32_t
stripe_link (call_frame_t *frame, xlator_t *this, loc_t *oldloc, loc_t *newloc)
{
        xlator_list_t    *trav = NULL;
        stripe_local_t   *local = NULL;
        stripe_private_t *priv = NULL;
        int32_t           op_errno = 1;

        VALIDATE_OR_GOTO (frame, err);
        VALIDATE_OR_GOTO (this, err);
        VALIDATE_OR_GOTO (oldloc, err);
        VALIDATE_OR_GOTO (oldloc->path, err);
        VALIDATE_OR_GOTO (oldloc->inode, err);

        priv = this->private;
        trav = this->children;

        /* If any one node is down, don't allow link operation */
        if (priv->nodes_down) {
                op_errno = ENOTCONN;
                goto err;
        }

        /* Initialization */
        local = GF_CALLOC (1, sizeof (stripe_local_t),
                           gf_stripe_mt_stripe_local_t);
        if (!local) {
                op_errno = ENOMEM;
                goto err;
        }
        local->op_ret = -1;
        frame->local = local;
        local->call_count = priv->child_count;

        /* Everytime in stripe lookup, all child
           nodes should be looked up */
        while (trav) {
                STACK_WIND (frame, stripe_inode_cbk,
                            trav->xlator, trav->xlator->fops->link,
                            oldloc, newloc);
                trav = trav->next;
        }

        return 0;
 err:
        STACK_UNWIND (frame, -1, op_errno, NULL, NULL, NULL, NULL);
        return 0;
}

int32_t
stripe_create_fail_unlink_cbk (call_frame_t *frame, void *cookie,
                               xlator_t *this, int32_t op_ret,
                               int32_t op_errno, struct iatt *preparent,
                               struct iatt *postparent)
{
        int32_t         callcnt = 0;
        fd_t           *lfd = NULL;
        stripe_local_t *local = NULL;
        inode_t        *local_inode = NULL;

        local = frame->local;

        LOCK (&frame->lock);
        {
                callcnt = --local->call_count;
        }
        UNLOCK (&frame->lock);

        if (!callcnt) {
                local_inode = local->inode;
                lfd = local->fd;
                loc_wipe (&local->loc);

                STACK_UNWIND (frame, local->op_ret, local->op_errno,
                              local->fd, local->inode, &local->stbuf,
                              &local->preparent, &local->postparent);

                if (local_inode)
                        inode_unref (local_inode);
                if (lfd)
                        fd_unref (lfd);
        }
        return 0;
}


/**
 * stripe_create_setxattr_cbk -
 */
int32_t
stripe_create_setxattr_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                            int32_t op_ret, int32_t op_errno)
{
        inode_t          *local_inode = NULL;
        fd_t             *lfd = NULL;
        stripe_local_t   *local = NULL;
        stripe_private_t *priv = NULL;
        xlator_list_t    *trav = NULL;
        int32_t           callcnt = 0;
        call_frame_t   *prev = NULL;

        prev  = cookie;
	priv  = this->private;
        local = frame->local;

        LOCK (&frame->lock);
        {
                callcnt = --local->call_count;

                if (op_ret == -1) {
                        gf_log (this->name, GF_LOG_DEBUG,
                                "%s returned error %s",
                                prev->this->name, strerror (op_errno));
                        local->op_ret = -1;
                        local->op_errno = op_errno;
                }
        }
        UNLOCK (&frame->lock);

        if (!callcnt) {
                if (local->op_ret == -1) {
                        local->call_count = priv->child_count;
                        trav = this->children;
                        while (trav) {
                                STACK_WIND (frame,
                                            stripe_create_fail_unlink_cbk,
                                            trav->xlator,
                                            trav->xlator->fops->unlink,
                                            &local->loc);
                                trav = trav->next;
                        }

                        return 0;
                }

                lfd = local->fd;
                local_inode = local->inode;
                loc_wipe (&local->loc);

                STACK_UNWIND (frame, local->op_ret, local->op_errno,
                              local->fd, local->inode, &local->stbuf,
                              &local->preparent, &local->postparent);

                if (local_inode)
                        inode_unref (local_inode);
                if (lfd)
                        fd_unref (lfd);
        }

        return 0;
}

/**
 * stripe_create_cbk -
 */
int32_t
stripe_create_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                   int32_t op_ret, int32_t op_errno, fd_t *fd,
                   inode_t *inode, struct iatt *buf, struct iatt *preparent,
                   struct iatt *postparent)
{
        int32_t           callcnt = 0;
        stripe_local_t   *local = NULL;
        stripe_private_t *priv = NULL;
        fd_t             *lfd = NULL;
        stripe_fd_ctx_t  *fctx = NULL;
        inode_t          *local_inode = NULL;
        call_frame_t     *prev = NULL;

        prev  = cookie;
        priv  = this->private;
        local = frame->local;

        LOCK (&frame->lock);
        {
                callcnt = --local->call_count;

                if (op_ret == -1) {
                        gf_log (this->name, GF_LOG_DEBUG,
                                "%s returned error %s",
                                prev->this->name, strerror (op_errno));
                        if ((op_errno != ENOENT) ||
                            (prev->this == FIRST_CHILD (this)))
                                local->failed = 1;
                        local->op_errno = op_errno;
                }

                if (op_ret >= 0) {
                        local->op_ret = op_ret;
                        /* Get the mapping in inode private */
                        /* Get the stat buf right */
                        if (FIRST_CHILD(this) == prev->this) {
                                local->stbuf      = *buf;
                                local->preparent  = *preparent;
                                local->postparent = *postparent;
                        }

                        local->stbuf_blocks += buf->ia_blocks;
                        local->preparent_blocks  += preparent->ia_blocks;
                        local->postparent_blocks += postparent->ia_blocks;

                        if (local->stbuf_size < buf->ia_size)
                                local->stbuf_size = buf->ia_size;
                        if (local->preparent_size < preparent->ia_size)
                                local->preparent_size = preparent->ia_size;
                        if (local->postparent_size < postparent->ia_size)
                                local->postparent_size = postparent->ia_size;
                }
        }
        UNLOCK (&frame->lock);

        if (!callcnt) {
                if (local->failed)
                        local->op_ret = -1;

                if (local->op_ret != -1) {
                        local->preparent.ia_blocks  = local->preparent_blocks;
                        local->preparent.ia_size    = local->preparent_size;
                        local->postparent.ia_blocks = local->postparent_blocks;
                        local->postparent.ia_size   = local->postparent_size;
                        local->stbuf.ia_size        = local->stbuf_size;
                        local->stbuf.ia_blocks      = local->stbuf_blocks;
                }

                /* */
                if (local->op_ret >= 0) {
                        fctx = GF_CALLOC (1, sizeof (stripe_fd_ctx_t),
                                          gf_stripe_mt_stripe_fd_ctx_t);
                        if (fctx) {
                                fctx->stripe_size  = local->stripe_size;
                                fctx->stripe_count = priv->child_count;
                                fctx->static_array = 1;
                                fctx->xl_array = priv->xl_array;
                                fd_ctx_set (local->fd, this,
                                            (uint64_t)(long)fctx);
                        }
                }

                if ((local->op_ret != -1) &&
                    local->stripe_size && priv->xattr_supported) {
                        /* Send a setxattr request to nodes where
                           the files are created */
                        int            ret = 0;
                        int32_t        i = 0;
                        char           size_key[256] = {0,};
                        char           index_key[256] = {0,};
                        char           count_key[256] = {0,};
                        dict_t        *dict = NULL;

                        sprintf (size_key,
                                 "trusted.%s.stripe-size", this->name);
                        sprintf (count_key,
                                 "trusted.%s.stripe-count", this->name);
                        sprintf (index_key,
                                 "trusted.%s.stripe-index", this->name);

                        local->call_count = priv->child_count;

                        for (i = 0; i < priv->child_count; i++) {
                                dict = get_new_dict ();
                                dict_ref (dict);

                                /* TODO: check return values */
                                ret = dict_set_int64 (dict, size_key,
                                                      local->stripe_size);
                                ret = dict_set_int32 (dict, count_key,
                                                      priv->child_count);
                                ret = dict_set_int32 (dict, index_key, i);

                                STACK_WIND (frame, stripe_create_setxattr_cbk,
                                            priv->xl_array[i],
                                            priv->xl_array[i]->fops->setxattr,
                                            &local->loc, dict, 0);

                                dict_unref (dict);
                        }
                } else {
                        /* Create itself has failed.. so return
                           without setxattring */
                        lfd = local->fd;
                        local_inode = local->inode;
                        loc_wipe (&local->loc);

                        STACK_UNWIND (frame, local->op_ret, local->op_errno,
                                      local->fd, local->inode, &local->stbuf,
                                      &local->preparent, &local->postparent);

                        if (local_inode)
                                inode_unref (local_inode);
                        if (lfd)
                                fd_unref (lfd);
                }
        }

        return 0;
}


/**
 * stripe_create - If a block-size is specified for the 'name', create the
 *    file in all the child nodes. If not, create it in only first child.
 *
 * @name- complete path of the file to be created.
 */
int32_t
stripe_create (call_frame_t *frame, xlator_t *this, loc_t *loc,
               int32_t flags, mode_t mode, fd_t *fd)
{
        stripe_private_t *priv = NULL;
        stripe_local_t   *local = NULL;
        xlator_list_t    *trav = NULL;
        int32_t           op_errno = 1;

        priv = this->private;

        /* files created in O_APPEND mode does not allow lseek() on fd */
        flags &= ~O_APPEND;

        if (priv->first_child_down || priv->nodes_down) {
                gf_log (this->name, GF_LOG_DEBUG,
                        "First node down, returning EIO");
                op_errno = EIO;
                goto err;
        }

        /* Initialization */
        local = GF_CALLOC (1, sizeof (stripe_local_t),
                           gf_stripe_mt_stripe_local_t);
        if (!local) {
                op_errno = ENOMEM;
                goto err;
        }
        local->op_ret = -1;
        local->op_errno = ENOTCONN;
        local->stripe_size = stripe_get_matching_bs (loc->path,
                                                     priv->pattern,
                                                     priv->block_size);
        frame->local = local;
        local->inode = inode_ref (loc->inode);
        loc_copy (&local->loc, loc);
        local->fd = fd_ref (fd);

        local->call_count = priv->child_count;

        trav = this->children;
        while (trav) {
                STACK_WIND (frame, stripe_create_cbk, trav->xlator,
                            trav->xlator->fops->create, loc, flags, mode, fd);
                trav = trav->next;
        }

        return 0;
 err:
        STACK_UNWIND (frame, -1, op_errno, NULL, NULL, NULL, NULL, NULL);
        return 0;
}

/**
 * stripe_open_cbk -
 */
int32_t
stripe_open_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                 int32_t op_ret, int32_t op_errno, fd_t *fd)
{
        int32_t         callcnt = 0;
        stripe_local_t *local = NULL;
        fd_t           *lfd = NULL;
        call_frame_t   *prev = NULL;

        prev  = cookie;
        local = frame->local;

        LOCK (&frame->lock);
        {
                callcnt = --local->call_count;

                if (op_ret == -1) {

                        gf_log (this->name, GF_LOG_DEBUG,
                                "%s returned error %s",
                                prev->this->name, strerror (op_errno));
                        if ((op_errno != ENOENT) ||
                            (prev->this == FIRST_CHILD (this)))
                                local->failed = 1;
                        local->op_errno = op_errno;
                }

                if (op_ret >= 0)
                        local->op_ret = op_ret;
        }
        UNLOCK (&frame->lock);

        if (!callcnt) {
                if (local->failed)
                        local->op_ret = -1;

                if (local->op_ret == -1) {
                        if (local->fctx) {
                                if (!local->fctx->static_array)
                                        GF_FREE (local->fctx->xl_array);
                                GF_FREE (local->fctx);
                        }
                } else {
                        fd_ctx_set (local->fd, this,
                                    (uint64_t)(long)local->fctx);
                }

                lfd = local->fd;
                loc_wipe (&local->loc);
                STACK_UNWIND (frame, local->op_ret, local->op_errno,
                              local->fd);
                if (lfd)
                        fd_unref (lfd);

        }

        return 0;
}


/**
 * stripe_getxattr_cbk -
 */
int32_t
stripe_open_getxattr_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                          int32_t op_ret, int32_t op_errno, dict_t *dict)
{
        int32_t           index = 0;
        int32_t           callcnt = 0;
        char              key[256] = {0,};
        stripe_local_t   *local = NULL;
        xlator_list_t    *trav = NULL;
        stripe_private_t *priv = NULL;
        data_t           *data = NULL;
        call_frame_t     *prev = NULL;
        fd_t             *lfd = NULL;

        prev  = (call_frame_t *)cookie;
        priv  = this->private;
        local = frame->local;

        LOCK (&frame->lock);
        {
                callcnt = --local->call_count;

                if (op_ret == -1) {
                        gf_log (this->name, GF_LOG_DEBUG,
                                "%s returned error %s",
                                prev->this->name, strerror (op_errno));
                        local->op_ret = -1;
                        if (local->op_errno != EIO)
                                local->op_errno = op_errno;
                        if ((op_errno != ENOENT) ||
                            (prev->this == FIRST_CHILD (this)))
                                local->failed = 1;
                        goto unlock;
                }

                if (!local->fctx) {
                        local->fctx =  GF_CALLOC (1, sizeof (stripe_fd_ctx_t),
                                           gf_stripe_mt_stripe_fd_ctx_t);
                        if (!local->fctx) {
                                local->op_errno = ENOMEM;
                                local->op_ret = -1;
                                goto unlock;
                        }

                        local->fctx->static_array = 0;
                }
                /* Stripe block size */
                sprintf (key, "trusted.%s.stripe-size", this->name);
                data = dict_get (dict, key);
                if (!data) {
                        local->xattr_self_heal_needed = 1;
                } else {
                        if (!local->fctx->stripe_size) {
                                local->fctx->stripe_size =
                                        data_to_int64 (data);
                        }

                        if (local->fctx->stripe_size != data_to_int64 (data)) {
                                gf_log (this->name, GF_LOG_DEBUG,
                                        "stripe-size mismatch in blocks");
                                local->xattr_self_heal_needed = 1;
                        }
                }
                /* Stripe count */
                sprintf (key, "trusted.%s.stripe-count", this->name);
                data = dict_get (dict, key);
                if (!data) {
                        local->xattr_self_heal_needed = 1;
                        goto unlock;
                }
                if (!local->fctx->xl_array) {
                        local->fctx->stripe_count = data_to_int32 (data);
                        if (!local->fctx->stripe_count) {
                                gf_log (this->name, GF_LOG_ERROR,
                                        "error with stripe-count xattr");
                                local->op_ret   = -1;
                                local->op_errno = EIO;
                                goto unlock;
                        }

                        local->fctx->xl_array = 
                                GF_CALLOC (local->fctx->stripe_count, 
                                        sizeof (xlator_t *),
                                        gf_stripe_mt_xlator_t);
                }
                if (local->fctx->stripe_count != data_to_int32 (data)) {
                        gf_log (this->name, GF_LOG_ERROR,
                                "error with stripe-count xattr");
                        local->op_ret   = -1;
                        local->op_errno = EIO;
                        goto unlock;
                }

                /* index */
                sprintf (key, "trusted.%s.stripe-index", this->name);
                data = dict_get (dict, key);
                if (!data) {
                        local->xattr_self_heal_needed = 1;
                        goto unlock;
                }
                index = data_to_int32 (data);
                if (index > priv->child_count) {
                        gf_log (this->name, GF_LOG_ERROR,
                                "error with stripe-index xattr");
                        local->op_ret   = -1;
                        local->op_errno = EIO;
                        goto unlock;
                }
                if (local->fctx->xl_array) {
                        if (local->fctx->xl_array[index]) {
                                gf_log (this->name, GF_LOG_ERROR,
                                        "duplicate entry @ index (%d)", index);
                                local->op_ret   = -1;
                                local->op_errno = EIO;
                                goto unlock;
                        }
                        local->fctx->xl_array[index] = prev->this;
                }
                local->entry_count++;
                local->op_ret = 0;
        }
 unlock:
        UNLOCK (&frame->lock);

        if (!callcnt) {
                /* TODO: if self-heal flag is set, do it */
                if (local->xattr_self_heal_needed) {
                        gf_log (this->name, GF_LOG_DEBUG,
                                "%s: stripe info need to be healed",
                                local->loc.path);
                }

                if (local->failed)
                        local->op_ret = -1;

                if (local->op_ret)
                        goto err;

                if (local->entry_count != local->fctx->stripe_count) {
                        local->op_ret = -1;
                        local->op_errno = EIO;
                        goto err;
                }
                if (!local->fctx->stripe_size) {
                        local->op_ret = -1;
                        local->op_errno = EIO;
                        goto err;
                }

                local->call_count = local->fctx->stripe_count;

                trav = this->children;
                while (trav) {
                        STACK_WIND (frame, stripe_open_cbk, trav->xlator,
                                    trav->xlator->fops->open, &local->loc,
                                    local->flags, local->fd, 0);
                        trav = trav->next;
                }
        }

        return 0;
 err:
        lfd = local->fd;
        loc_wipe (&local->loc);
        STACK_UNWIND (frame, local->op_ret, local->op_errno, local->fd);
        if (lfd)
                fd_unref (lfd);

        return 0;
}

/**
 * stripe_open -
 */
int32_t
stripe_open (call_frame_t *frame, xlator_t *this, loc_t *loc,
             int32_t flags, fd_t *fd, int32_t wbflags)
{
        stripe_local_t   *local = NULL;
        stripe_private_t *priv = NULL;
        xlator_list_t    *trav = NULL;
        int32_t           op_errno = 1;

        VALIDATE_OR_GOTO (frame, err);
        VALIDATE_OR_GOTO (this, err);
        VALIDATE_OR_GOTO (loc, err);
        VALIDATE_OR_GOTO (loc->path, err);
        VALIDATE_OR_GOTO (loc->inode, err);

        priv = this->private;
        trav = this->children;

        if (priv->first_child_down) {
                op_errno = ENOTCONN;
                goto err;
        }

        /* Initialization */
        local = GF_CALLOC (1, sizeof (stripe_local_t),
                           gf_stripe_mt_stripe_local_t);
        if (!local) {
                op_errno = ENOMEM;
                goto err;
        }

        /* files opened in O_APPEND mode does not allow lseek() on fd */
        flags &= ~O_APPEND;

        local->fd = fd_ref (fd);
        frame->local = local;
        loc_copy (&local->loc, loc);

        /* Striped files */
        local->flags = flags;
        local->call_count = priv->child_count;
        local->stripe_size = stripe_get_matching_bs (loc->path,
                                                     priv->pattern,
                                                     priv->block_size);

        if (priv->xattr_supported) {
                while (trav) {
                        STACK_WIND (frame, stripe_open_getxattr_cbk,
                                    trav->xlator, trav->xlator->fops->getxattr,
                                    loc, NULL);
                        trav = trav->next;
                }
        } else {
                local->fctx =  GF_CALLOC (1, sizeof (stripe_fd_ctx_t),
                                          gf_stripe_mt_stripe_fd_ctx_t);
                if (!local->fctx) {
                        op_errno = ENOMEM;
                        goto err;
                }

                local->fctx->static_array = 1;
                local->fctx->stripe_size  = local->stripe_size;
                local->fctx->stripe_count = priv->child_count;
                local->fctx->xl_array     = priv->xl_array;

                while (trav) {
                        STACK_WIND (frame, stripe_open_cbk, trav->xlator,
                                    trav->xlator->fops->open,
                                    &local->loc, local->flags, local->fd,
                                    wbflags);
                        trav = trav->next;
                }
        }

        return 0;
 err:
        STACK_UNWIND (frame, -1, op_errno, NULL);
        return 0;
}

/**
 * stripe_opendir_cbk -
 */
int32_t
stripe_opendir_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                    int32_t op_ret, int32_t op_errno, fd_t *fd)
{
        int32_t         callcnt = 0;
        stripe_local_t *local = frame->local;
        fd_t           *local_fd = NULL;
        call_frame_t   *prev = NULL;

        prev  = cookie;
        LOCK (&frame->lock);
        {
                callcnt = --local->call_count;

                if (op_ret == -1) {
                        gf_log (this->name, GF_LOG_DEBUG,
                                "%s returned error %s",
                                prev->this->name, strerror (op_errno));
                        local->op_ret = -1;
                        local->op_errno = op_errno;
                }

                if (op_ret >= 0)
                        local->op_ret = op_ret;
        }
        UNLOCK (&frame->lock);

        if (!callcnt) {
                local_fd = local->fd;
                STACK_UNWIND (frame, local->op_ret, local->op_errno,
                              local->fd);
                if (local_fd)
                        fd_unref (local_fd);
        }

        return 0;
}


/**
 * stripe_opendir -
 */
int32_t
stripe_opendir (call_frame_t *frame, xlator_t *this, loc_t *loc, fd_t *fd)
{
        xlator_list_t    *trav = NULL;
        stripe_local_t   *local = NULL;
        stripe_private_t *priv = NULL;
        int32_t           op_errno = 1;

        VALIDATE_OR_GOTO (frame, err);
        VALIDATE_OR_GOTO (this, err);
        VALIDATE_OR_GOTO (loc, err);
        VALIDATE_OR_GOTO (loc->path, err);
        VALIDATE_OR_GOTO (loc->inode, err);

        priv = this->private;
        trav = this->children;

        if (priv->first_child_down) {
                op_errno = ENOTCONN;
                goto err;
        }

        /* Initialization */
        local = GF_CALLOC (1, sizeof (stripe_local_t),
                           gf_stripe_mt_stripe_local_t);
        if (!local) {
                op_errno = ENOMEM;
                goto err;
        }
        frame->local = local;
        local->call_count = priv->child_count;
        local->fd = fd_ref (fd);

        while (trav) {
                STACK_WIND (frame, stripe_opendir_cbk, trav->xlator,
                            trav->xlator->fops->opendir, loc, fd);
                trav = trav->next;
        }

        return 0;
 err:
        STACK_UNWIND (frame, -1, op_errno, NULL);
        return 0;
}


/**
 * stripe_lk_cbk -
 */
int32_t
stripe_lk_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
               int32_t op_ret, int32_t op_errno, struct flock *lock)
{
        int32_t         callcnt = 0;
        stripe_local_t *local = NULL;
        call_frame_t   *prev = NULL;

        prev  = cookie;
        local = frame->local;

        LOCK (&frame->lock);
        {
                callcnt = --local->call_count;
                if (op_ret == -1) {
                        gf_log (this->name, GF_LOG_DEBUG,
                                "%s returned error %s",
                                prev->this->name, strerror (op_errno));
                        local->op_errno = op_errno;
                        if ((op_errno != ENOENT) ||
                            (prev->this == FIRST_CHILD (this)))
                                local->failed = 1;
                }
                if (op_ret >= 0) {
                        if (FIRST_CHILD(this) == prev->this) {
                                /* First successful call, copy the *lock */
                                local->op_ret = op_ret;
                                local->lock = *lock;
                        }
                }
        }
        UNLOCK (&frame->lock);

        if (!callcnt) {
                if (local->failed)
                        local->op_ret = -1;
                STACK_UNWIND (frame, local->op_ret,
                              local->op_errno, &local->lock);
        }
        return 0;
}


/**
 * stripe_lk -
 */
int32_t
stripe_lk (call_frame_t *frame, xlator_t *this, fd_t *fd, int32_t cmd,
           struct flock *lock)
{
        stripe_local_t   *local = NULL;
        xlator_list_t    *trav = NULL;
        stripe_private_t *priv = NULL;
        int32_t           op_errno = 1;

        VALIDATE_OR_GOTO (frame, err);
        VALIDATE_OR_GOTO (this, err);
        VALIDATE_OR_GOTO (fd, err);
        VALIDATE_OR_GOTO (fd->inode, err);

        trav = this->children;
        priv = this->private;

        /* Initialization */
        local = GF_CALLOC (1, sizeof (stripe_local_t),
                           gf_stripe_mt_stripe_local_t);
        if (!local) {
                op_errno = ENOMEM;
                goto err;
        }
        local->op_ret = -1;
        frame->local = local;
        local->call_count = priv->child_count;

        while (trav) {
                STACK_WIND (frame, stripe_lk_cbk, trav->xlator,
                            trav->xlator->fops->lk, fd, cmd, lock);
                trav = trav->next;
        }

        return 0;
 err:
        STACK_UNWIND (frame, -1, op_errno, NULL);
        return 0;
}

/**
 * stripe_flush -
 */
int32_t
stripe_flush (call_frame_t *frame, xlator_t *this, fd_t *fd)
{
        stripe_local_t   *local = NULL;
        stripe_private_t *priv = NULL;
        xlator_list_t    *trav = NULL;
        int32_t           op_errno = 1;

        VALIDATE_OR_GOTO (frame, err);
        VALIDATE_OR_GOTO (this, err);
        VALIDATE_OR_GOTO (fd, err);
        VALIDATE_OR_GOTO (fd->inode, err);

        priv = this->private;
        trav = this->children;

        if (priv->first_child_down) {
                op_errno = ENOTCONN;
                goto err;
        }
        /* Initialization */
        local = GF_CALLOC (1, sizeof (stripe_local_t),
                           gf_stripe_mt_stripe_local_t);
        if (!local) {
                op_errno = ENOMEM;
                goto err;
        }
        local->op_ret = -1;
        frame->local = local;
        local->call_count = priv->child_count;

        while (trav) {
                STACK_WIND (frame, stripe_common_cbk,  trav->xlator,
                            trav->xlator->fops->flush, fd);
                trav = trav->next;
        }

        return 0;
 err:
        STACK_UNWIND (frame, -1, op_errno);
        return 0;
}


/**
 * stripe_fsync -
 */
int32_t
stripe_fsync (call_frame_t *frame, xlator_t *this, fd_t *fd, int32_t flags)
{
        stripe_local_t   *local = NULL;
        stripe_private_t *priv = NULL;
        xlator_list_t    *trav = NULL;
        int32_t           op_errno = 1;

        VALIDATE_OR_GOTO (frame, err);
        VALIDATE_OR_GOTO (this, err);
        VALIDATE_OR_GOTO (fd, err);
        VALIDATE_OR_GOTO (fd->inode, err);

        priv = this->private;
        trav = this->children;

        /* Initialization */
        local = GF_CALLOC (1, sizeof (stripe_local_t),
                           gf_stripe_mt_stripe_local_t);
        if (!local) {
                op_errno = ENOMEM;
                goto err;
        }
        local->op_ret = -1;
        frame->local = local;
        local->call_count = priv->child_count;

        while (trav) {
                STACK_WIND (frame, stripe_fsync_cbk, trav->xlator,
                            trav->xlator->fops->fsync, fd, flags);
                trav = trav->next;
        }

        return 0;
 err:
        STACK_UNWIND (frame, -1, op_errno, NULL, NULL);
        return 0;
}


/**
 * stripe_fstat -
 */
int32_t
stripe_fstat (call_frame_t *frame,
              xlator_t *this,
              fd_t *fd)
{
        stripe_local_t   *local = NULL;
        stripe_private_t *priv = NULL;
        xlator_list_t    *trav = NULL;
        int32_t           op_errno = 1;

        VALIDATE_OR_GOTO (frame, err);
        VALIDATE_OR_GOTO (this, err);
        VALIDATE_OR_GOTO (fd, err);
        VALIDATE_OR_GOTO (fd->inode, err);

        priv = this->private;
        trav = this->children;

        /* Initialization */
        local = GF_CALLOC (1, sizeof (stripe_local_t),
                           gf_stripe_mt_stripe_local_t);
        if (!local) {
                op_errno = ENOMEM;
                goto err;
        }
        local->op_ret = -1;
        frame->local = local;
        local->call_count = priv->child_count;

        while (trav) {
                STACK_WIND (frame, stripe_buf_cbk, trav->xlator,
                            trav->xlator->fops->fstat, fd);
                trav = trav->next;
        }

        return 0;
 err:
        STACK_UNWIND (frame, -1, op_errno, NULL);
        return 0;
}


/**
 * stripe_ftruncate -
 */
int32_t
stripe_ftruncate (call_frame_t *frame, xlator_t *this, fd_t *fd, off_t offset)
{
        stripe_local_t   *local = NULL;
        stripe_private_t *priv = NULL;
        xlator_list_t    *trav = NULL;
        int32_t           op_errno = 1;

        VALIDATE_OR_GOTO (frame, err);
        VALIDATE_OR_GOTO (this, err);
        VALIDATE_OR_GOTO (fd, err);
        VALIDATE_OR_GOTO (fd->inode, err);

        priv = this->private;
        trav = this->children;

        /* Initialization */
        local = GF_CALLOC (1, sizeof (stripe_local_t),
                           gf_stripe_mt_stripe_local_t);
        if (!local) {
                op_errno = ENOMEM;
                goto err;
        }
        local->op_ret = -1;
        frame->local = local;
        local->call_count = priv->child_count;

        while (trav) {
                STACK_WIND (frame, stripe_truncate_cbk, trav->xlator,
                            trav->xlator->fops->ftruncate, fd, offset);
                trav = trav->next;
        }

        return 0;
 err:
        STACK_UNWIND (frame, -1, op_errno);
        return 0;
}


/**
 * stripe_fsyncdir -
 */
int32_t
stripe_fsyncdir (call_frame_t *frame, xlator_t *this, fd_t *fd, int32_t flags)
{
        stripe_local_t   *local = NULL;
        stripe_private_t *priv = NULL;
        xlator_list_t    *trav = NULL;
        int32_t           op_errno = 1;

        VALIDATE_OR_GOTO (frame, err);
        VALIDATE_OR_GOTO (this, err);
        VALIDATE_OR_GOTO (fd, err);
        VALIDATE_OR_GOTO (fd->inode, err);

        priv = this->private;
        trav = this->children;

        /* Initialization */
        local = GF_CALLOC (1, sizeof (stripe_local_t),
                           gf_stripe_mt_stripe_local_t);
        if (!local) {
                op_errno = ENOMEM;
                goto err;
        }
        local->op_ret = -1;
        frame->local = local;
        local->call_count = priv->child_count;

        while (trav) {
                STACK_WIND (frame, stripe_common_cbk, trav->xlator,
                            trav->xlator->fops->fsyncdir, fd, flags);
                trav = trav->next;
        }

        return 0;
 err:
        STACK_UNWIND (frame, -1, op_errno, NULL, NULL);
        return 0;
}


int32_t
stripe_readv_fstat_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                        int32_t op_ret, int32_t op_errno, struct iatt *buf)
{
        int32_t         i = 0;
        int32_t         callcnt = 0;
        int32_t         count = 0;
        stripe_local_t *local = NULL;
        struct iovec   *vec = NULL;
        struct iatt     tmp_stbuf = {0,};
        struct iobref  *tmp_iobref = NULL;
        struct iobuf   *iobuf = NULL;

        local = frame->local;

        LOCK (&frame->lock);
        {
                callcnt = --local->call_count;
                if (op_ret != -1)
                        if (local->stbuf_size < buf->ia_size)
                                local->stbuf_size = buf->ia_size;
        }
        UNLOCK (&frame->lock);

        if (!callcnt) {
                op_ret = 0;

                /* Keep extra space for filling in '\0's */
                vec = GF_CALLOC ((local->count * 2), sizeof (struct iovec),
                                  gf_stripe_mt_iovec);
                if (!vec) {
                        op_ret = -1;
                        goto done;
                }

                for (i = 0; i < local->wind_count; i++) {
                        if (local->replies[i].op_ret) {
                                memcpy ((vec + count), local->replies[i].vector,
                                        (local->replies[i].count * sizeof (struct iovec)));
                                count +=  local->replies[i].count;
                                op_ret += local->replies[i].op_ret;
                        }
                        if ((local->replies[i].op_ret <
                             local->replies[i].requested_size) &&
                            (local->stbuf_size > (local->offset + op_ret))) {
                                /* Fill in 0s here */
                                vec[count].iov_len  =
                                        (local->replies[i].requested_size -
                                         local->replies[i].op_ret);
                                iobuf = iobuf_get (this->ctx->iobuf_pool);
                                if (!iobuf) {
                                        gf_log (this->name, GF_LOG_ERROR,
                                                "Out of memory.");
                                        op_ret = -1;
                                        op_errno = ENOMEM;
                                        goto done;
                                }
                                memset (iobuf->ptr, 0, vec[count].iov_len);
                                iobref_add (local->iobref, iobuf);
                                vec[count].iov_base = iobuf->ptr;

                                op_ret += vec[count].iov_len;
                                count++;
                        }
                        GF_FREE (local->replies[i].vector);
                }

                /* FIXME: notice that st_ino, and st_dev (gen) will be
                 * different than what inode will have. Make sure this doesn't
                 * cause any bugs at higher levels */
                memcpy (&tmp_stbuf, &local->replies[0].stbuf,
                        sizeof (struct iatt));
                tmp_stbuf.ia_size = local->stbuf_size;

        done:
                GF_FREE (local->replies);
                tmp_iobref = local->iobref;
                fd_unref (local->fd);
                STACK_UNWIND_STRICT (readv, frame, op_ret, op_errno, vec,
                                     count, &tmp_stbuf, tmp_iobref);

                iobref_unref (tmp_iobref);
                if (vec)
                        GF_FREE (vec);
        }
        return 0;
}

/**
 * stripe_readv_cbk - get all the striped reads, and order it properly, send it
 *        to above layer after putting it in a single vector.
 */
int32_t
stripe_readv_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                  int32_t op_ret, int32_t op_errno, struct iovec *vector,
                  int32_t count, struct iatt *stbuf, struct iobref *iobref)
{
        int32_t         index = 0;
        int32_t         callcnt = 0;
        int32_t         final_count = 0;
        int32_t         need_to_check_proper_size = 0;
        call_frame_t   *mframe = NULL;
        stripe_local_t *mlocal = NULL;
        stripe_local_t *local = NULL;
        struct iovec   *final_vec = NULL;
        struct iatt     tmp_stbuf = {0,};
        struct iobref  *tmp_iobref = NULL;
        stripe_fd_ctx_t  *fctx = NULL;

        local  = frame->local;
        index  = local->node_index;
        mframe = local->orig_frame;
        mlocal = mframe->local;
        fctx   = mlocal->fctx;

        LOCK (&mframe->lock);
        {
                mlocal->replies[index].op_ret = op_ret;
                mlocal->replies[index].op_errno = op_errno;
                mlocal->replies[index].requested_size = local->readv_size;
                if (op_ret >= 0) {
                        mlocal->replies[index].stbuf  = *stbuf;
                        mlocal->replies[index].count  = count;
                        mlocal->replies[index].vector = iov_dup (vector, count);

                        if (!mlocal->iobref)
                                mlocal->iobref = iobref_new ();
                        iobref_merge (mlocal->iobref, iobref);
                }
                callcnt = ++mlocal->call_count;
        }
        UNLOCK(&mframe->lock);

        if (callcnt == mlocal->wind_count) {
                op_ret = 0;

                for (index=0; index < mlocal->wind_count; index++) {
                        /* check whether each stripe returned
                         * 'expected' number of bytes */
                        if (mlocal->replies[index].op_ret == -1) {
                                op_ret = -1;
                                op_errno = mlocal->replies[index].op_errno;
                                break;
                        }
                        /* TODO: handle the 'holes' within the read range
                           properly */
                        if (mlocal->replies[index].op_ret <
                            mlocal->replies[index].requested_size) {
                                need_to_check_proper_size = 1;
                        }

                        op_ret       += mlocal->replies[index].op_ret;
                        mlocal->count += mlocal->replies[index].count;
                }
                if (op_ret == -1)
                        goto done;
                if (need_to_check_proper_size)
                        goto check_size;

                final_vec = GF_CALLOC (mlocal->count, sizeof (struct iovec),
                                       gf_stripe_mt_iovec);

                if (!final_vec) {
                        op_ret = -1;
                        goto done;
                }

                for (index = 0; index < mlocal->wind_count; index++) {
                        memcpy ((final_vec + final_count),
                                mlocal->replies[index].vector,
                                (mlocal->replies[index].count *
                                 sizeof (struct iovec)));
                        final_count +=  mlocal->replies[index].count;
                        GF_FREE (mlocal->replies[index].vector);
                }

                /* FIXME: notice that st_ino, and st_dev (gen) will be
                 * different than what inode will have. Make sure this doesn't
                 * cause any bugs at higher levels */
                memcpy (&tmp_stbuf, &mlocal->replies[0].stbuf,
                        sizeof (struct iatt));

        done:
                /* */
                GF_FREE (mlocal->replies);
                tmp_iobref = mlocal->iobref;
                fd_unref (mlocal->fd);
                STACK_UNWIND_STRICT (readv, mframe, op_ret, op_errno, final_vec,
                                     final_count, &tmp_stbuf, tmp_iobref);

                iobref_unref (tmp_iobref);
                if (final_vec)
                        GF_FREE (final_vec);
        }

                goto out;

        check_size:
                mlocal->call_count = fctx->stripe_count;

                for (index = 0; index < fctx->stripe_count; index++) {
                        STACK_WIND (mframe, stripe_readv_fstat_cbk,
                                    (fctx->xl_array[index]),
                                    (fctx->xl_array[index])->fops->fstat,
                                    mlocal->fd);
                }
        
out:
        STACK_DESTROY (frame->root);
        return 0;
}

/**
 * stripe_readv -
 */
int32_t
stripe_readv (call_frame_t *frame, xlator_t *this, fd_t *fd,
              size_t size, off_t offset)
{
        int32_t           op_errno = 1;
        int32_t           idx = 0;
        int32_t           index = 0;
        int32_t           num_stripe = 0;
        int32_t           off_index = 0;
        size_t            frame_size = 0;
        off_t             rounded_end = 0;
        uint64_t          tmp_fctx = 0;
        uint64_t          stripe_size = 0;
        off_t             rounded_start = 0;
        off_t             frame_offset = offset;
        stripe_local_t   *local = NULL;
        call_frame_t     *rframe = NULL;
        stripe_local_t   *rlocal = NULL;
        xlator_list_t    *trav = NULL;
        stripe_private_t *priv = NULL;
        stripe_fd_ctx_t  *fctx = NULL;

        trav = this->children;
        priv = this->private;

        fd_ctx_get (fd, this, &tmp_fctx);
        if (!tmp_fctx) {
                op_errno = EBADFD;
                goto err;
        }
        fctx = (stripe_fd_ctx_t *)(long)tmp_fctx;
        stripe_size = fctx->stripe_size;

        /* The file is stripe across the child nodes. Send the read request
         * to the child nodes appropriately after checking which region of
         * the file is in which child node. Always '0-<stripe_size>' part of
         * the file resides in the first child.
         */
        rounded_start = floor (offset, stripe_size);
        rounded_end = roof (offset+size, stripe_size);
        num_stripe = rounded_end/stripe_size - rounded_start/stripe_size;
        
        local = GF_CALLOC (1, sizeof (stripe_local_t),
                           gf_stripe_mt_stripe_local_t);
        if (!local) {
                op_errno = ENOMEM;
                goto err;
        }
        frame->local = local;

        /* This is where all the vectors should be copied. */
        local->replies = GF_CALLOC (num_stripe, sizeof (struct readv_replies),
                                    gf_stripe_mt_readv_replies);
        if (!local->replies) {
                op_errno = ENOMEM;
                goto err;
        }

        off_index = (offset / stripe_size) % fctx->stripe_count;
        local->wind_count = num_stripe;
        local->readv_size = size;
        local->offset     = offset;
        local->fd         = fd_ref (fd);
        local->fctx       = fctx;

        for (index = off_index; index < (num_stripe + off_index); index++) {
                rframe = copy_frame (frame);
                rlocal = GF_CALLOC (1, sizeof (stripe_local_t),
                                    gf_stripe_mt_stripe_local_t);
                if (!rlocal) {
                        op_errno = ENOMEM;
                        goto err;
                }

                frame_size = min (roof (frame_offset+1, stripe_size),
                                  (offset + size)) - frame_offset;

                rlocal->node_index = index - off_index;
                rlocal->orig_frame = frame;
                rlocal->readv_size = frame_size;
                rframe->local = rlocal;
                idx = (index % fctx->stripe_count);
                STACK_WIND (rframe, stripe_readv_cbk, fctx->xl_array[idx],
                            fctx->xl_array[idx]->fops->readv,
                            fd, frame_size, frame_offset);

                frame_offset += frame_size;
        }

        return 0;
 err:
        if (local->fd)
                fd_unref (local->fd);

        STACK_UNWIND_STRICT (readv, frame, -1, op_errno, NULL, 0, NULL, NULL);
        return 0;
}


/**
 * stripe_writev_cbk -
 */
int32_t
stripe_writev_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                   int32_t op_ret, int32_t op_errno, struct iatt *prebuf,
                   struct iatt *postbuf)
{
        int32_t         callcnt = 0;
        stripe_local_t *local = NULL;
        call_frame_t   *prev = NULL;

        prev  = cookie;
        local = frame->local;

        LOCK(&frame->lock);
        {
                callcnt = ++local->call_count;

                if (op_ret == -1) {
                        gf_log (this->name, GF_LOG_DEBUG,
                                "%s returned error %s",
                                prev->this->name, strerror (op_errno));
                        local->op_errno = op_errno;
                        local->op_ret = -1;
                }
                if (op_ret >= 0) {
                        local->op_ret += op_ret;
                        local->post_buf = *postbuf;
                        local->pre_buf = *prebuf;
                }
        }
        UNLOCK (&frame->lock);

        if ((callcnt == local->wind_count) && local->unwind) {
                STACK_UNWIND (frame, local->op_ret, local->op_errno,
                              &local->pre_buf, &local->post_buf);
        }
        return 0;
}


/**
 * stripe_single_writev_cbk -
 */
int32_t
stripe_single_writev_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                          int32_t op_ret, int32_t op_errno, struct iatt *stbuf)
{
        STACK_UNWIND (frame, op_ret, op_errno, stbuf);
        return 0;
}
/**
 * stripe_writev -
 */
int32_t
stripe_writev (call_frame_t *frame, xlator_t *this, fd_t *fd,
               struct iovec *vector, int32_t count, off_t offset,
               struct iobref *iobref)
{
        struct iovec     *tmp_vec = vector;
        stripe_private_t *priv = NULL;
        stripe_local_t   *local = NULL;
        xlator_list_t    *trav = NULL;
        stripe_fd_ctx_t  *fctx = NULL;
        int32_t           op_errno = 1;
        int32_t           idx = 0;
        int32_t           total_size = 0;
        int32_t           offset_offset = 0;
        int32_t           remaining_size = 0;
        int32_t           tmp_count = count;
        off_t             fill_size = 0;
        uint64_t          stripe_size = 0;
        uint64_t          tmp_fctx = 0;

        priv = this->private;

        fd_ctx_get (fd, this, &tmp_fctx);
        if (!tmp_fctx) {
                op_errno = EINVAL;
                goto err;
        }
        fctx = (stripe_fd_ctx_t *)(long)tmp_fctx;
        stripe_size = fctx->stripe_size;

        /* File has to be stripped across the child nodes */
        for (idx = 0; idx< count; idx ++) {
                total_size += tmp_vec[idx].iov_len;
        }
        remaining_size = total_size;

        local = GF_CALLOC (1, sizeof (stripe_local_t),
                           gf_stripe_mt_stripe_local_t);
        if (!local) {
                op_errno = ENOMEM;
                goto err;
        }
        frame->local = local;
        local->stripe_size = stripe_size;

        while (1) {
                /* Send striped chunk of the vector to child
                   nodes appropriately. */
                trav = this->children;

                idx = (((offset + offset_offset) /
                        local->stripe_size) % fctx->stripe_count);

                fill_size = (local->stripe_size -
                             ((offset + offset_offset) % local->stripe_size));
                if (fill_size > remaining_size)
                        fill_size = remaining_size;

                remaining_size -= fill_size;

                tmp_count = iov_subset (vector, count, offset_offset,
                                        offset_offset + fill_size, NULL);
                tmp_vec = GF_CALLOC (tmp_count, sizeof (struct iovec),
                                     gf_stripe_mt_iovec);
                if (!tmp_vec) {
                        op_errno = ENOMEM;
                        goto err;
                }
                tmp_count = iov_subset (vector, count, offset_offset,
                                        offset_offset + fill_size, tmp_vec);

                local->wind_count++;
                if (remaining_size == 0)
                        local->unwind = 1;

                STACK_WIND(frame, stripe_writev_cbk, fctx->xl_array[idx],
                           fctx->xl_array[idx]->fops->writev, fd, tmp_vec,
                           tmp_count, offset + offset_offset, iobref);
                GF_FREE (tmp_vec);
                offset_offset += fill_size;
                if (remaining_size == 0)
                        break;
        }

        return 0;
 err:
        STACK_UNWIND (frame, -1, op_errno, NULL);
        return 0;
}



/* Management operations */

int32_t
stripe_release (xlator_t *this, fd_t *fd)
{
        uint64_t          tmp_fctx = 0;
        stripe_fd_ctx_t  *fctx = NULL;

        fd_ctx_del (fd, this, &tmp_fctx);
        if (!tmp_fctx) {
                goto out;
        }

        fctx = (stripe_fd_ctx_t *)(long)tmp_fctx;

        if (!fctx->static_array)
                GF_FREE (fctx->xl_array);
        
        GF_FREE (fctx);
                
 out:
	return 0;
}

/**
 * notify
 */
int32_t
notify (xlator_t *this, int32_t event, void *data, ...)
{
        stripe_private_t *priv = NULL;
        int               down_client = 0;
        int               i = 0;

        priv = this->private;
        if (!priv)
                return 0;

        switch (event)
        {
        case GF_EVENT_CHILD_UP:
        {
                /* get an index number to set */
                for (i = 0; i < priv->child_count; i++) {
                        if (data == priv->xl_array[i])
                                break;
                }
                priv->state[i] = 1;
                for (i = 0; i < priv->child_count; i++) {
                        if (!priv->state[i])
                                down_client++;
                }

                LOCK (&priv->lock);
                {
                        priv->nodes_down = down_client;
                        if (data == FIRST_CHILD (this))
                                priv->first_child_down = 0;
                        if (!priv->nodes_down)
                                default_notify (this, event, data);
                }
                UNLOCK (&priv->lock);
        }
        break;
        case GF_EVENT_CHILD_DOWN:
        {
                /* get an index number to set */
                for (i = 0; i < priv->child_count; i++) {
                        if (data == priv->xl_array[i])
                                break;
                }
                priv->state[i] = 0;
                for (i = 0; i < priv->child_count; i++) {
                        if (!priv->state[i])
                                down_client++;
                }

                LOCK (&priv->lock);
                {
                        priv->nodes_down = down_client;

                        if (data == FIRST_CHILD (this))
                                priv->first_child_down = 1;
                        if (priv->nodes_down)
                                default_notify (this, event, data);
                }
                UNLOCK (&priv->lock);
        }
        break;

        default:
        {
                /* */
                default_notify (this, event, data);
        }
        break;
        }

        return 0;
}

int
set_stripe_block_size (xlator_t *this, stripe_private_t *priv, char *data)
{
        int                    ret = -1;
        char                  *tmp_str = NULL;
        char                  *tmp_str1 = NULL;
        char                  *dup_str = NULL;
        char                  *stripe_str = NULL;
        char                  *pattern = NULL;
        char                  *num = NULL;
        struct stripe_options *temp_stripeopt = NULL;
        struct stripe_options *stripe_opt = NULL;

        /* Get the pattern for striping.
           "option block-size *avi:10MB" etc */
        stripe_str = strtok_r (data, ",", &tmp_str);
        while (stripe_str) {
                dup_str = gf_strdup (stripe_str);
                stripe_opt = GF_CALLOC (1, sizeof (struct stripe_options),
                                        gf_stripe_mt_stripe_options);
                if (!stripe_opt) {
                        GF_FREE (dup_str);
                        goto out;
                }

                pattern = strtok_r (dup_str, ":", &tmp_str1);
                num = strtok_r (NULL, ":", &tmp_str1);
                if (!num) {
                        num = pattern;
                        pattern = "*";
                }
                if (gf_string2bytesize (num, &stripe_opt->block_size) != 0) {
                        gf_log (this->name, GF_LOG_ERROR,
                                "invalid number format \"%s\"", num);
                        goto out;
                }
                memcpy (stripe_opt->path_pattern, pattern, strlen (pattern));

                gf_log (this->name, GF_LOG_DEBUG,
                        "block-size : pattern %s : size %"PRId64,
                        stripe_opt->path_pattern, stripe_opt->block_size);

                if (!priv->pattern) {
                        priv->pattern = stripe_opt;
                } else {
                        temp_stripeopt = priv->pattern;
                        while (temp_stripeopt->next)
                                temp_stripeopt = temp_stripeopt->next;
                        temp_stripeopt->next = stripe_opt;
                }
                stripe_str = strtok_r (NULL, ",", &tmp_str);
                GF_FREE (dup_str);
        }

        ret = 0;
 out:
        return ret;
}

int32_t
mem_acct_init (xlator_t *this)
{
        int     ret = -1;

        if (!this)
                return ret;

        ret = xlator_mem_acct_init (this, gf_stripe_mt_end + 1);
        
        if (ret != 0) {
                gf_log (this->name, GF_LOG_ERROR, "Memory accounting init"
                                "failed");
                return ret;
        }

        return ret;
}

/**
 * init - This function is called when xlator-graph gets initialized.
 *     The option given in volfiles are parsed here.
 * @this -
 */
int32_t
init (xlator_t *this)
{
        stripe_private_t *priv = NULL;
        xlator_list_t    *trav = NULL;
        data_t           *data = NULL;
        int32_t           count = 0;
        int               ret = -1;

        trav = this->children;
        while (trav) {
                count++;
                trav = trav->next;
        }

        if (!count) {
                gf_log (this->name, GF_LOG_ERROR,
                        "stripe configured without \"subvolumes\" option. "
                        "exiting");
                goto out;
        }

        if (!this->parents) {
                gf_log (this->name, GF_LOG_WARNING,
                        "dangling volume. check volfile ");
        }

        if (count == 1) {
                gf_log (this->name, GF_LOG_ERROR,
                        "stripe configured with only one \"subvolumes\" option."
                        " please check the volume. exiting");
                goto out;
        }

        priv = GF_CALLOC (1, sizeof (stripe_private_t),
                          gf_stripe_mt_stripe_private_t);

        if (!priv)
                goto out;
        priv->xl_array = GF_CALLOC (count, sizeof (xlator_t *),
                                    gf_stripe_mt_xlator_t);
        if (!priv->xl_array)
                goto out;

        priv->state = GF_CALLOC (count, sizeof (int8_t),
                                 gf_stripe_mt_int8_t);
        if (!priv->state)
                goto out;

        priv->child_count = count;
        LOCK_INIT (&priv->lock);

        trav = this->children;
        count = 0;
        while (trav) {
                priv->xl_array[count++] = trav->xlator;
                trav = trav->next;
        }

        if (count > 256) {
                gf_log (this->name, GF_LOG_ERROR,
                        "maximum number of stripe subvolumes supported "
                        "is 256");
                goto out;
        }

        priv->block_size = (128 * GF_UNIT_KB);
        /* option stripe-pattern *avi:1GB,*pdf:4096 */
        data = dict_get (this->options, "block-size");
        if (!data) {
                gf_log (this->name, GF_LOG_DEBUG,
                        "No \"option block-size <x>\" given, defaulting "
                        "to 128KB");
        } else {
                ret = set_stripe_block_size (this, priv, data->data);
                if (ret)
                        goto out;
        }

        priv->xattr_supported = 1;
        data = dict_get (this->options, "use-xattr");
        if (data) {
                if (gf_string2boolean (data->data,
                                       &priv->xattr_supported) == -1) {
                        gf_log (this->name, GF_LOG_ERROR,
                                "error setting hard check for extended "
                                "attribute");
                        //return -1;
                }
        }

        /* notify related */
        priv->nodes_down = priv->child_count;
        this->private = priv;

        ret = 0;
 out:
        if (ret) {
                if (priv) {
                        if (priv->xl_array)
                                GF_FREE (priv->xl_array);
                        GF_FREE (priv);
                }
        }
        return ret;
}

/**
 * fini -   Free all the private variables
 * @this -
 */
void
fini (xlator_t *this)
{
        stripe_private_t      *priv = NULL;
        struct stripe_options *prev = NULL;
        struct stripe_options *trav = NULL;

        priv = this->private;
        if (priv) {
                if (priv->xl_array)
                        GF_FREE (priv->xl_array);

                trav = priv->pattern;
                while (trav) {
                        prev = trav;
                        trav = trav->next;
                        GF_FREE (prev);
                }
                LOCK_DESTROY (&priv->lock);
                GF_FREE (priv);
        }

        return;
}


struct xlator_fops fops = {
        .stat        = stripe_stat,
        .unlink      = stripe_unlink,
        .rename      = stripe_rename,
        .link        = stripe_link,
        .truncate    = stripe_truncate,
        .create      = stripe_create,
        .open        = stripe_open,
        .readv       = stripe_readv,
        .writev      = stripe_writev,
        .statfs      = stripe_statfs,
        .flush       = stripe_flush,
        .fsync       = stripe_fsync,
        .ftruncate   = stripe_ftruncate,
        .fstat       = stripe_fstat,
        .mkdir       = stripe_mkdir,
        .rmdir       = stripe_rmdir,
        .lk          = stripe_lk,
        .opendir     = stripe_opendir,
        .fsyncdir    = stripe_fsyncdir,
        .setattr     = stripe_setattr,
        .fsetattr    = stripe_fsetattr,
        .lookup      = stripe_lookup,
        .mknod       = stripe_mknod,
};

struct xlator_mops mops = {
};

struct xlator_cbks cbks = {
        .release = stripe_release,
};


struct volume_options options[] = {
        { .key  = {"block-size"},
          .type = GF_OPTION_TYPE_ANY
        },
        { .key  = {"use-xattr"},
          .type = GF_OPTION_TYPE_BOOL
        },
        { .key  = {NULL} },
};
