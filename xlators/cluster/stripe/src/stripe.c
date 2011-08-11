/*
  Copyright (c) 2007-2011 Gluster, Inc. <http://www.gluster.com>
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

#include "stripe.h"
#include "libxlator.h"
#include "byte-order.h"
#include "statedump.h"

struct volume_options options[];

void
stripe_local_wipe (stripe_local_t *local)
{
        if (!local)
                goto out;

        loc_wipe (&local->loc);
        loc_wipe (&local->loc2);

        if (local->fd)
                fd_unref (local->fd);

        if (local->inode)
                inode_unref (local->inode);

        if (local->xattr)
                dict_unref (local->xattr);

        if (local->dict)
                dict_unref (local->dict);

out:
        return;
}

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

        if (!path || !opts)
                goto out;

        /* FIXME: is a strdup really necessary? */
        pathname = gf_strdup (path);
        if (!pathname)
                goto out;

        trav = opts;
        while (trav) {
                if (!fnmatch (trav->path_pattern, pathname, FNM_NOESCAPE)) {
                        block_size = trav->block_size;
                        break;
                }
                trav = trav->next;
        }

        GF_FREE (pathname);

out:
        return block_size;
}



int32_t
stripe_sh_chown_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                     int32_t op_ret, int32_t op_errno,
                     struct iatt *preop, struct iatt *postop)
{
        int             callcnt = -1;
        stripe_local_t *local   = NULL;

        if (!this || !frame || !frame->local) {
                gf_log ("stripe", GF_LOG_DEBUG, "possible NULL deref");
                goto out;
        }

        local = frame->local;

        LOCK (&frame->lock);
        {
                callcnt = --local->call_count;
        }
        UNLOCK (&frame->lock);

        if (!callcnt) {
                STRIPE_STACK_DESTROY (frame);
        }
out:
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

        if (!frame || !frame->local || !cookie || !this) {
                gf_log ("stripe", GF_LOG_DEBUG, "possible NULL deref");
                goto out;
        }

        prev  = cookie;
        local = frame->local;

        STACK_WIND (frame, stripe_sh_chown_cbk, prev->this,
                    prev->this->fops->setattr, &local->loc,
                    &local->stbuf, (GF_SET_ATTR_UID | GF_SET_ATTR_GID));

out:
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
        dict_t           *dict   = NULL;
        int               ret    = 0;

        if (!local || !this || !frame) {
                gf_log ("stripe", GF_LOG_DEBUG, "possible NULL deref");
                goto out;
        }

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
        memcpy (&rlocal->stbuf, &local->stbuf, sizeof (struct iatt));

        dict = dict_new ();
        if (!dict)
                goto out;

        ret = dict_set_static_bin (dict, "gfid-req", local->stbuf.ia_gfid, 16);
        if (ret)
                gf_log (this->name, GF_LOG_WARNING,
                        "failed to set gfid-req");

        while (trav) {
                if (IA_ISREG (local->stbuf.ia_type)) {
                        STACK_WIND (rframe, stripe_sh_make_entry_cbk,
                                    trav->xlator, trav->xlator->fops->mknod,
                                    &local->loc,
                                    st_mode_from_ia (local->stbuf.ia_prot,
                                                     local->stbuf.ia_type), 0,
                                    dict);
                }
                if (IA_ISDIR (local->stbuf.ia_type)) {
                        STACK_WIND (rframe, stripe_sh_make_entry_cbk,
                                    trav->xlator, trav->xlator->fops->mkdir,
                                    &local->loc, st_mode_from_ia (local->stbuf.ia_prot,
                                                                  local->stbuf.ia_type),
                                    dict);
                }
                trav = trav->next;
        }

        if (dict)
                dict_unref (dict);
        return 0;

out:
        if (rframe)
                STRIPE_STACK_DESTROY (rframe);
        if (dict)
                dict_unref (dict);

        return 0;
}


void
stripe_aggregate (dict_t *this, char *key, data_t *value, void *data)
{
        dict_t  *dst  = NULL;
        int64_t *ptr  = 0, *size = NULL;
        int32_t  ret  = -1;

        dst = data;

        if (strcmp (key, GF_XATTR_QUOTA_SIZE_KEY) == 0) {
                ret = dict_get_bin (dst, key, (void **)&size);
                if (ret < 0) {
                        size = GF_CALLOC (1, sizeof (int64_t),
                                          gf_common_mt_char);
                        if (size == NULL) {
                                gf_log ("stripe", GF_LOG_WARNING,
                                        "memory allocation failed");
                                goto out;
                        }
                        ret = dict_set_bin (dst, key, size, sizeof (int64_t));
                        if (ret < 0) {
                                gf_log ("stripe", GF_LOG_WARNING,
                                        "stripe aggregate dict set failed");
                                GF_FREE (size);
                                goto out;
                        }
                }

                ptr = data_to_bin (value);
                if (ptr == NULL) {
                        gf_log ("stripe", GF_LOG_WARNING, "data to bin failed");
                        goto out;
                }

                *size = hton64 (ntoh64 (*size) + ntoh64 (*ptr));
        } else if (strcmp (key, GF_CONTENT_KEY)) {
                /* No need to aggregate 'CONTENT' data */
                ret = dict_set (dst, key, value);
                if (ret)
                        gf_log ("stripe", GF_LOG_WARNING, "xattr dict set failed");
        }

out:
        return;
}


void
stripe_aggregate_xattr (dict_t *dst, dict_t *src)
{
        if ((dst == NULL) || (src == NULL)) {
                goto out;
        }

        dict_foreach (src, stripe_aggregate, dst);
out:
        return;
}


int32_t
stripe_lookup_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                   int32_t op_ret, int32_t op_errno, inode_t *inode,
                   struct iatt *buf, dict_t *dict, struct iatt *postparent)
{
        int32_t         callcnt = 0;
        stripe_local_t *local = NULL;
        call_frame_t   *prev = NULL;

        if (!this || !frame || !frame->local || !cookie) {
                gf_log ("stripe", GF_LOG_DEBUG, "possible NULL deref");
                goto out;
        }

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
                        if (((op_errno != ENOENT) && (op_errno != ENOTCONN)) ||
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
                                if (local->xattr) {
                                        stripe_aggregate_xattr (local->dict,
                                                                local->xattr);
                                        dict_unref (local->xattr);
                                        local->xattr = NULL;
                                }
                        }
                        if (!local->dict && !local->xattr) {
                                local->xattr = dict_ref (dict);
                        } else if (local->dict) {
                                stripe_aggregate_xattr (local->dict, dict);
                        } else if (local->xattr) {
                                stripe_aggregate_xattr (local->xattr, dict);
                        }

                        local->stbuf_blocks      += buf->ia_blocks;
                        local->postparent_blocks += postparent->ia_blocks;

                        if (local->stbuf_size < buf->ia_size)
                                local->stbuf_size = buf->ia_size;
                        if (local->postparent_size < postparent->ia_size)
                                local->postparent_size = postparent->ia_size;

                        if (uuid_is_null (local->ia_gfid))
                                uuid_copy (local->ia_gfid, buf->ia_gfid);

                        /* Make sure the gfid on all the nodes are same */
                        if (uuid_compare (local->ia_gfid, buf->ia_gfid)) {
                                gf_log (this->name, GF_LOG_WARNING,
                                        "%s: gfid different on subvolume %s",
                                        local->loc.path, prev->this->name);
                        }
                }
        }
        UNLOCK (&frame->lock);

        if (!callcnt) {
                if (local->op_ret == 0 && local->entry_self_heal_needed)
                        stripe_entry_self_heal (frame, this, local);

                if (local->failed)
                        local->op_ret = -1;

                if (local->op_ret != -1) {
                        local->stbuf.ia_blocks      = local->stbuf_blocks;
                        local->stbuf.ia_size        = local->stbuf_size;
                        local->postparent.ia_blocks = local->postparent_blocks;
                        local->postparent.ia_size   = local->postparent_size;
                }

                STRIPE_STACK_UNWIND (lookup, frame, local->op_ret,
                                     local->op_errno, local->inode,
                                     &local->stbuf, local->dict,
                                     &local->postparent);
        }
out:
        return 0;
}

int32_t
stripe_lookup (call_frame_t *frame, xlator_t *this, loc_t *loc,
               dict_t *xattr_req)
{
        stripe_local_t   *local = NULL;
        xlator_list_t    *trav = NULL;
        stripe_private_t *priv = NULL;
        int32_t           op_errno = EINVAL;
        int64_t           filesize = 0;
        int               ret = 0;

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

        if (xattr_req && dict_get (xattr_req, GF_CONTENT_KEY)) {
                ret = dict_get_int64 (xattr_req, GF_CONTENT_KEY, &filesize);
                if (!ret && (filesize > priv->block_size))
                        dict_del (xattr_req, GF_CONTENT_KEY);
        }

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
        STRIPE_STACK_UNWIND (lookup, frame, -1, op_errno, NULL, NULL, NULL, NULL);
        return 0;
}


int32_t
stripe_stat_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                 int32_t op_ret, int32_t op_errno, struct iatt *buf)
{
        int32_t         callcnt = 0;
        stripe_local_t *local = NULL;
        call_frame_t   *prev = NULL;

        if (!this || !frame || !frame->local || !cookie) {
                gf_log ("stripe", GF_LOG_DEBUG, "possible NULL deref");
                goto out;
        }
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

                if (local->op_ret != -1) {
                        local->stbuf.ia_size   = local->stbuf_size;
                        local->stbuf.ia_blocks = local->stbuf_blocks;
                }

                STRIPE_STACK_UNWIND (stat, frame, local->op_ret,
                                     local->op_errno, &local->stbuf);
        }
out:
        return 0;
}

int32_t
stripe_stat (call_frame_t *frame, xlator_t *this, loc_t *loc)
{
        xlator_list_t    *trav = NULL;
        stripe_local_t   *local = NULL;
        stripe_private_t *priv = NULL;
        int32_t           op_errno = EINVAL;

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
                STACK_WIND (frame, stripe_stat_cbk, trav->xlator,
                            trav->xlator->fops->stat, loc);
                trav = trav->next;
        }

        return 0;

err:
        STRIPE_STACK_UNWIND (stat, frame, -1, op_errno, NULL);
        return 0;
}


int32_t
stripe_statfs_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                   int32_t op_ret, int32_t op_errno, struct statvfs *stbuf)
{
        stripe_local_t *local = NULL;
        int32_t         callcnt = 0;

        if (!this || !frame || !frame->local) {
                gf_log ("stripe", GF_LOG_DEBUG, "possible NULL deref");
                goto out;
        }
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
                STRIPE_STACK_UNWIND (statfs, frame, local->op_ret,
                                     local->op_errno, &local->statvfs_buf);
        }
out:
        return 0;
}

int32_t
stripe_statfs (call_frame_t *frame, xlator_t *this, loc_t *loc)
{
        stripe_local_t   *local = NULL;
        xlator_list_t    *trav = NULL;
        stripe_private_t *priv = NULL;
        int32_t           op_errno = EINVAL;

        VALIDATE_OR_GOTO (frame, err);
        VALIDATE_OR_GOTO (this, err);
        VALIDATE_OR_GOTO (loc, err);

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
        STRIPE_STACK_UNWIND (statfs, frame, -1, op_errno, NULL);
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

        if (!this || !frame || !frame->local || !cookie) {
                gf_log ("stripe", GF_LOG_DEBUG, "possible NULL deref");
                goto out;
        }

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

                if (local->op_ret != -1) {
                        local->pre_buf.ia_blocks  = local->prebuf_blocks;
                        local->pre_buf.ia_size    = local->prebuf_size;
                        local->post_buf.ia_blocks = local->postbuf_blocks;
                        local->post_buf.ia_size   = local->postbuf_size;
                }

                STRIPE_STACK_UNWIND (truncate, frame, local->op_ret,
                                     local->op_errno, &local->pre_buf,
                                     &local->post_buf);
        }
out:
        return 0;
}

int32_t
stripe_truncate (call_frame_t *frame, xlator_t *this, loc_t *loc, off_t offset)
{
        xlator_list_t    *trav = NULL;
        stripe_local_t   *local = NULL;
        stripe_private_t *priv = NULL;
        int32_t           op_errno = EINVAL;

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
        STRIPE_STACK_UNWIND (truncate, frame, -1, op_errno, NULL, NULL);
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

        if (!this || !frame || !frame->local || !cookie) {
                gf_log ("stripe", GF_LOG_DEBUG, "possible NULL deref");
                goto out;
        }

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

                if (local->op_ret != -1) {
                        local->pre_buf.ia_blocks  = local->prebuf_blocks;
                        local->pre_buf.ia_size    = local->prebuf_size;
                        local->post_buf.ia_blocks = local->postbuf_blocks;
                        local->post_buf.ia_size   = local->postbuf_size;
                }

                STRIPE_STACK_UNWIND (setattr, frame, local->op_ret,
                                     local->op_errno, &local->pre_buf,
                                     &local->post_buf);
        }
out:
        return 0;
}


int32_t
stripe_setattr (call_frame_t *frame, xlator_t *this, loc_t *loc,
                struct iatt *stbuf, int32_t valid)
{
        xlator_list_t    *trav = NULL;
        stripe_local_t   *local = NULL;
        stripe_private_t *priv = NULL;
        int32_t           op_errno = EINVAL;

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
        STRIPE_STACK_UNWIND (setattr, frame, -1, op_errno, NULL, NULL);
        return 0;
}


int32_t
stripe_fsetattr (call_frame_t *frame, xlator_t *this, fd_t *fd,
                 struct iatt *stbuf, int32_t valid)
{
        stripe_local_t   *local = NULL;
        stripe_private_t *priv = NULL;
        xlator_list_t    *trav = NULL;
        int32_t           op_errno = EINVAL;

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
        STRIPE_STACK_UNWIND (fsetattr, frame, -1, op_errno, NULL, NULL);
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

        if (!this || !frame || !frame->local || !cookie) {
                gf_log ("stripe", GF_LOG_DEBUG, "possible NULL deref");
                goto out;
        }

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

                STRIPE_STACK_UNWIND (rename, frame, local->op_ret, local->op_errno,
                                     &local->stbuf, &local->preparent,
                                     &local->postparent,  &local->pre_buf,
                                     &local->post_buf);
        }
out:
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

        if (!this || !frame || !frame->local) {
                gf_log ("stripe", GF_LOG_DEBUG, "possible NULL deref");
                op_errno = EINVAL;
                goto unwind;
        }

        if (op_ret == -1) {
                goto unwind;
        }

        local = frame->local;
        trav = this->children;

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
        STRIPE_STACK_UNWIND (rename, frame, -1, op_errno, buf, preoldparent,
                             postoldparent, prenewparent, postnewparent);
        return 0;
}

int32_t
stripe_rename (call_frame_t *frame, xlator_t *this, loc_t *oldloc,
               loc_t *newloc)
{
        stripe_private_t *priv = NULL;
        stripe_local_t   *local = NULL;
        xlator_list_t    *trav = NULL;
        int32_t           op_errno = EINVAL;

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
        STRIPE_STACK_UNWIND (rename, frame, -1, op_errno, NULL, NULL, NULL,
                             NULL, NULL);
        return 0;
}



int32_t
stripe_unlink_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                   int32_t op_ret, int32_t op_errno, struct iatt *preparent,
                   struct iatt *postparent)
{
        int32_t         callcnt = 0;
        stripe_local_t *local   = NULL;
        call_frame_t   *prev = NULL;

        if (!this || !frame || !frame->local || !cookie) {
                gf_log ("stripe", GF_LOG_DEBUG, "possible NULL deref");
                goto out;
        }

        prev  = cookie;
        local = frame->local;

        LOCK (&frame->lock);
        {
                callcnt = --local->call_count;

                if (op_ret == -1) {
                        gf_log (this->name, GF_LOG_DEBUG, "%s returned %s",
                                prev->this->name, strerror (op_errno));
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

                if (local->op_ret != -1) {
                        local->preparent.ia_blocks  = local->preparent_blocks;
                        local->preparent.ia_size    = local->preparent_size;
                        local->postparent.ia_blocks = local->postparent_blocks;
                        local->postparent.ia_size   = local->postparent_size;
                }

                STRIPE_STACK_UNWIND (unlink, frame, local->op_ret,
                                     local->op_errno, &local->preparent,
                                     &local->postparent);
        }
out:
        return 0;
}

int32_t
stripe_unlink (call_frame_t *frame, xlator_t *this, loc_t *loc)
{
        xlator_list_t    *trav = NULL;
        stripe_local_t   *local = NULL;
        stripe_private_t *priv = NULL;
        int32_t           op_errno = EINVAL;

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
        STRIPE_STACK_UNWIND (unlink, frame, -1, op_errno, NULL, NULL);
        return 0;
}


int32_t
stripe_first_rmdir_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                        int32_t op_ret, int32_t op_errno,struct iatt *preparent,
                        struct iatt *postparent)

{
        xlator_list_t *trav = NULL;
        stripe_local_t *local = NULL;

        if (!this || !frame || !frame->local) {
                gf_log ("stripe", GF_LOG_DEBUG, "possible NULL deref");
                op_errno = EINVAL;
                goto err;
        }

        if (op_ret == -1) {
                goto err;
        }

        trav = this->children;
        local = frame->local;

        local->call_count--; /* First child successful */
        trav = trav->next; /* Skip first child */

        local->preparent  = *preparent;
        local->postparent = *postparent;
        local->preparent_size  = preparent->ia_size;
        local->postparent_size = postparent->ia_size;
        local->preparent_blocks  += preparent->ia_blocks;
        local->postparent_blocks += postparent->ia_blocks;

        while (trav) {
                STACK_WIND (frame, stripe_unlink_cbk, trav->xlator,
                            trav->xlator->fops->rmdir, &local->loc,
                            local->flags);
                trav = trav->next;
        }

        return 0;
err:
        STRIPE_STACK_UNWIND (rmdir, frame, op_ret, op_errno, NULL, NULL);
        return 0;

}

int32_t
stripe_rmdir (call_frame_t *frame, xlator_t *this, loc_t *loc, int flags)
{
        xlator_list_t    *trav = NULL;
        stripe_local_t   *local = NULL;
        stripe_private_t *priv = NULL;
        int32_t           op_errno = EINVAL;

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
        local->flags = flags;
        local->call_count = priv->child_count;

        STACK_WIND (frame, stripe_first_rmdir_cbk,  trav->xlator,
                    trav->xlator->fops->rmdir, loc, flags);

        return 0;
err:
        STRIPE_STACK_UNWIND (rmdir, frame, -1, op_errno, NULL, NULL);
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

        if (!this || !frame || !frame->local) {
                gf_log ("stripe", GF_LOG_DEBUG, "possible NULL deref");
                goto out;
        }

        local = frame->local;

        LOCK (&frame->lock);
        {
                callcnt = --local->call_count;
        }
        UNLOCK (&frame->lock);

        if (!callcnt) {
                STRIPE_STACK_UNWIND (mknod, frame, local->op_ret, local->op_errno,
                                     local->inode, &local->stbuf,
                                     &local->preparent, &local->postparent);
        }
out:
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
        call_frame_t     *prev = NULL;

        if (!this || !frame || !frame->local || !cookie) {
                gf_log ("stripe", GF_LOG_DEBUG, "possible NULL deref");
                goto out;
        }

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

                STRIPE_STACK_UNWIND (mknod, frame, local->op_ret, local->op_errno,
                                     local->inode, &local->stbuf,
                                     &local->preparent, &local->postparent);
        }
out:
        return 0;
}

int32_t
stripe_mknod_ifreg_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                        int32_t op_ret, int32_t op_errno, inode_t *inode,
                        struct iatt *buf, struct iatt *preparent,
                        struct iatt *postparent)
{
        int32_t           callcnt = 0;
        stripe_local_t   *local = NULL;
        stripe_private_t *priv = NULL;
        call_frame_t     *prev = NULL;
        xlator_list_t    *trav = NULL;

        if (!this || !frame || !frame->local || !cookie) {
                gf_log ("stripe", GF_LOG_DEBUG, "possible NULL deref");
                goto out;
        }

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

                        /* Can be used as a mechanism to understand if mknod
                           was successful in at least one place */
                        if (uuid_is_null (local->ia_gfid))
                                uuid_copy (local->ia_gfid, buf->ia_gfid);

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

                if ((local->op_ret == -1) && !uuid_is_null (local->ia_gfid)) {
                        /* ia_gfid set means, at least on one node 'mknod'
                           is successful */
                        local->call_count = priv->child_count;
                        trav = this->children;
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


                if (local->op_ret != -1) {
                        local->preparent.ia_blocks  = local->preparent_blocks;
                        local->preparent.ia_size    = local->preparent_size;
                        local->postparent.ia_blocks = local->postparent_blocks;
                        local->postparent.ia_size   = local->postparent_size;
                        local->stbuf.ia_size        = local->stbuf_size;
                        local->stbuf.ia_blocks      = local->stbuf_blocks;
                }

                /* Create itself has failed.. so return
                   without setxattring */
                STRIPE_STACK_UNWIND (mknod, frame, local->op_ret, local->op_errno,
                                     local->inode, &local->stbuf,
                                     &local->preparent, &local->postparent);
        }
out:
        return 0;
}


int32_t
stripe_single_mknod_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                         int32_t op_ret, int32_t op_errno, inode_t *inode,
                         struct iatt *buf, struct iatt *preparent,
                         struct iatt *postparent)
{
        STRIPE_STACK_UNWIND (mknod, frame, op_ret, op_errno, inode, buf,
                             preparent, postparent);
        return 0;
}


int
stripe_mknod (call_frame_t *frame, xlator_t *this, loc_t *loc, mode_t mode,
              dev_t rdev, dict_t *params)
{
        stripe_private_t *priv           = NULL;
        stripe_local_t   *local          = NULL;
        xlator_list_t    *trav           = NULL;
        int32_t           op_errno       = EINVAL;
        int32_t           i              = 0;
        char              size_key[256]  = {0,};
        char              index_key[256] = {0,};
        char              count_key[256] = {0,};
        dict_t           *dict           = NULL;
        int               ret            = 0;
        int               need_unref     = 0;

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
                local->inode = inode_ref (loc->inode);
                loc_copy (&local->loc, loc);

                /* Everytime in stripe lookup, all child nodes should
                   be looked up */
                local->call_count = priv->child_count;

                /* Send a setxattr request to nodes where the
                   files are created */
                sprintf (size_key,
                         "trusted.%s.stripe-size", this->name);
                sprintf (count_key,
                         "trusted.%s.stripe-count", this->name);
                sprintf (index_key,
                         "trusted.%s.stripe-index", this->name);

                while (trav) {
                        if (priv->xattr_supported) {
                                dict = dict_new ();
                                if (!dict) {
                                        gf_log (this->name, GF_LOG_ERROR,
                                                "failed to allocate dict %s", loc->path);
                                }
                                need_unref = 1;

                                dict_copy (params, dict);

                                ret = dict_set_int64 (dict, size_key,
                                                      local->stripe_size);
                                if (ret)
                                        gf_log (this->name, GF_LOG_ERROR,
                                                "%s: set stripe-size failed", loc->path);
                                ret = dict_set_int32 (dict, count_key,
                                                      priv->child_count);
                                if (ret)
                                        gf_log (this->name, GF_LOG_ERROR,
                                                "%s: set child_count failed",
                                                loc->path);
                                ret = dict_set_int32 (dict, index_key, i);
                                if (ret)
                                        gf_log (this->name, GF_LOG_ERROR,
                                                "%s: set stripe-index failed",
                                                loc->path);
                        } else {
                                dict = params;
                        }

                        STACK_WIND (frame, stripe_mknod_ifreg_cbk,
                                    trav->xlator, trav->xlator->fops->mknod,
                                    loc, mode, rdev, dict);
                        trav = trav->next;
                        i++;

                        if (dict && need_unref)
                                dict_unref (dict);
                }

                /* This case is handled, no need to continue further. */
                return 0;
        }

        STACK_WIND (frame, stripe_single_mknod_cbk,
                    FIRST_CHILD(this), FIRST_CHILD(this)->fops->mknod,
                    loc, mode, rdev, params);

        return 0;
err:
        STRIPE_STACK_UNWIND (mknod, frame, -1, op_errno, NULL, NULL, NULL, NULL);
        return 0;
}


int32_t
stripe_mkdir_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                  int32_t op_ret, int32_t op_errno, inode_t *inode,
                  struct iatt *buf, struct iatt *preparent,
                  struct iatt *postparent)
{
        int32_t         callcnt = 0;
        stripe_local_t  *local   = NULL;
        call_frame_t    *prev = NULL;

        if (!this || !frame || !frame->local || !cookie) {
                gf_log ("stripe", GF_LOG_DEBUG, "possible NULL deref");
                goto out;
        }

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

                if (local->op_ret != -1) {
                        local->preparent.ia_blocks  = local->preparent_blocks;
                        local->preparent.ia_size    = local->preparent_size;
                        local->postparent.ia_blocks = local->postparent_blocks;
                        local->postparent.ia_size   = local->postparent_size;
                        local->stbuf.ia_size        = local->stbuf_size;
                        local->stbuf.ia_blocks      = local->stbuf_blocks;
                }
                STRIPE_STACK_UNWIND (mkdir, frame, local->op_ret,
                                     local->op_errno, local->inode,
                                     &local->stbuf, &local->preparent,
                                     &local->postparent);
        }
out:
        return 0;
}


int
stripe_mkdir (call_frame_t *frame, xlator_t *this, loc_t *loc, mode_t mode,
              dict_t *params)
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
                STACK_WIND (frame, stripe_mkdir_cbk,
                            trav->xlator, trav->xlator->fops->mkdir,
                            loc, mode, params);
                trav = trav->next;
        }

        return 0;
err:
        STRIPE_STACK_UNWIND (mkdir, frame, -1, op_errno, NULL, NULL, NULL, NULL);
        return 0;
}


int32_t
stripe_link_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                 int32_t op_ret, int32_t op_errno, inode_t *inode,
                 struct iatt *buf, struct iatt *preparent,
                 struct iatt *postparent)
{
        int32_t         callcnt = 0;
        stripe_local_t  *local   = NULL;
        call_frame_t    *prev = NULL;

        if (!this || !frame || !frame->local || !cookie) {
                gf_log ("stripe", GF_LOG_DEBUG, "possible NULL deref");
                goto out;
        }

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

                if (local->op_ret != -1) {
                        local->preparent.ia_blocks  = local->preparent_blocks;
                        local->preparent.ia_size    = local->preparent_size;
                        local->postparent.ia_blocks = local->postparent_blocks;
                        local->postparent.ia_size   = local->postparent_size;
                        local->stbuf.ia_size        = local->stbuf_size;
                        local->stbuf.ia_blocks      = local->stbuf_blocks;
                }
                STRIPE_STACK_UNWIND (link, frame, local->op_ret,
                                     local->op_errno, local->inode,
                                     &local->stbuf, &local->preparent,
                                     &local->postparent);
        }
out:
        return 0;
}

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
                STACK_WIND (frame, stripe_link_cbk,
                            trav->xlator, trav->xlator->fops->link,
                            oldloc, newloc);
                trav = trav->next;
        }

        return 0;
err:
        STRIPE_STACK_UNWIND (link, frame, -1, op_errno, NULL, NULL, NULL, NULL);
        return 0;
}

int32_t
stripe_create_fail_unlink_cbk (call_frame_t *frame, void *cookie,
                               xlator_t *this, int32_t op_ret,
                               int32_t op_errno, struct iatt *preparent,
                               struct iatt *postparent)
{
        int32_t         callcnt = 0;
        stripe_local_t *local = NULL;

        if (!this || !frame || !frame->local) {
                gf_log ("stripe", GF_LOG_DEBUG, "possible NULL deref");
                goto out;
        }

        local = frame->local;

        LOCK (&frame->lock);
        {
                callcnt = --local->call_count;
        }
        UNLOCK (&frame->lock);

        if (!callcnt) {
                STRIPE_STACK_UNWIND (create, frame, local->op_ret, local->op_errno,
                                     local->fd, local->inode, &local->stbuf,
                                     &local->preparent, &local->postparent);
        }
out:
        return 0;
}


int32_t
stripe_create_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                   int32_t op_ret, int32_t op_errno, fd_t *fd,
                   inode_t *inode, struct iatt *buf, struct iatt *preparent,
                   struct iatt *postparent)
{
        int32_t           callcnt = 0;
        stripe_local_t   *local = NULL;
        stripe_private_t *priv = NULL;
        stripe_fd_ctx_t  *fctx = NULL;
        call_frame_t     *prev = NULL;
        xlator_list_t    *trav = NULL;

        if (!this || !frame || !frame->local || !cookie) {
                gf_log ("stripe", GF_LOG_DEBUG, "possible NULL deref");
                goto out;
        }

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

                if (local->op_ret >= 0) {
                        local->preparent.ia_blocks  = local->preparent_blocks;
                        local->preparent.ia_size    = local->preparent_size;
                        local->postparent.ia_blocks = local->postparent_blocks;
                        local->postparent.ia_size   = local->postparent_size;
                        local->stbuf.ia_size        = local->stbuf_size;
                        local->stbuf.ia_blocks      = local->stbuf_blocks;

                        fctx = GF_CALLOC (1, sizeof (stripe_fd_ctx_t),
                                          gf_stripe_mt_stripe_fd_ctx_t);
                        if (!fctx) {
                                local->op_ret = -1;
                                local->op_errno = ENOMEM;
                                goto unwind;
                        }

                        fctx->stripe_size  = local->stripe_size;
                        fctx->stripe_count = priv->child_count;
                        fctx->static_array = 1;
                        fctx->xl_array = priv->xl_array;
                        fd_ctx_set (local->fd, this,
                                    (uint64_t)(long)fctx);
                }

        unwind:
                /* Create itself has failed.. so return
                   without setxattring */
                STRIPE_STACK_UNWIND (create, frame, local->op_ret,
                                     local->op_errno, local->fd,
                                     local->inode, &local->stbuf,
                                     &local->preparent, &local->postparent);
        }

out:
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
               int32_t flags, mode_t mode, fd_t *fd, dict_t *params)
{
        stripe_private_t *priv = NULL;
        stripe_local_t   *local = NULL;
        xlator_list_t    *trav = NULL;
        int32_t           op_errno = EINVAL;
        int               ret            = 0;
        int               need_unref     = 0;
        int               i              = 0;
        char              size_key[256]  = {0,};
        char              index_key[256] = {0,};
        char              count_key[256] = {0,};
        dict_t           *dict           = NULL;

        VALIDATE_OR_GOTO (frame, err);
        VALIDATE_OR_GOTO (this, err);
        VALIDATE_OR_GOTO (loc, err);
        VALIDATE_OR_GOTO (loc->path, err);
        VALIDATE_OR_GOTO (loc->inode, err);

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
        /* Send a setxattr request to nodes where the
           files are created */
        sprintf (size_key, "trusted.%s.stripe-size", this->name);
        sprintf (count_key, "trusted.%s.stripe-count", this->name);
        sprintf (index_key, "trusted.%s.stripe-index", this->name);

        trav = this->children;
        while (trav) {
                if (priv->xattr_supported) {
                        dict = dict_new ();
                        if (!dict) {
                                gf_log (this->name, GF_LOG_ERROR,
                                        "failed to allocate dict %s", loc->path);
                        }
                        need_unref = 1;

                        dict_copy (params, dict);

                        ret = dict_set_int64 (dict, size_key,
                                              local->stripe_size);
                        if (ret)
                                gf_log (this->name, GF_LOG_ERROR,
                                        "%s: set stripe-size failed", loc->path);
                        ret = dict_set_int32 (dict, count_key,
                                              priv->child_count);
                        if (ret)
                                gf_log (this->name, GF_LOG_ERROR,
                                        "%s: set child_count failed",
                                        loc->path);
                        ret = dict_set_int32 (dict, index_key, i);
                        if (ret)
                                gf_log (this->name, GF_LOG_ERROR,
                                        "%s: set stripe-index failed",
                                        loc->path);
                } else {
                        dict = params;
                }

                STACK_WIND (frame, stripe_create_cbk, trav->xlator,
                            trav->xlator->fops->create, loc, flags,
                            mode, fd, dict);
                trav = trav->next;
                i++;

                if (need_unref && dict)
                        dict_unref (dict);

        }

        return 0;
err:
        STRIPE_STACK_UNWIND (create, frame, -1, op_errno, NULL, NULL, NULL,
                             NULL, NULL);
        return 0;
}

int32_t
stripe_open_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                 int32_t op_ret, int32_t op_errno, fd_t *fd)
{
        int32_t         callcnt = 0;
        stripe_local_t *local = NULL;
        call_frame_t   *prev = NULL;

        if (!this || !frame || !frame->local || !cookie) {
                gf_log ("stripe", GF_LOG_DEBUG, "possible NULL deref");
                goto out;
        }

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

                STRIPE_STACK_UNWIND (open, frame, local->op_ret,
                                     local->op_errno, local->fd);
        }
out:
        return 0;
}

int32_t
stripe_open_lookup_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                        int32_t op_ret, int32_t op_errno, inode_t *inode,
                        struct iatt *buf, dict_t *dict, struct iatt *postparent)
{
        int32_t           index = 0;
        int32_t           callcnt = 0;
        char              key[256] = {0,};
        stripe_local_t   *local = NULL;
        xlator_list_t    *trav = NULL;
        stripe_private_t *priv = NULL;
        data_t           *data = NULL;
        call_frame_t     *prev = NULL;

        if (!this || !frame || !frame->local || !cookie) {
                gf_log ("stripe", GF_LOG_DEBUG, "possible NULL deref");
                goto out;
        }

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

                if (!dict)
                        goto unlock;

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
                                gf_log (this->name, GF_LOG_WARNING,
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
                        if (!local->fctx->xl_array) {
                                local->op_errno = ENOMEM;
                                local->op_ret   = -1;
                                goto unlock;
                        }
                }
                if (local->fctx->stripe_count != data_to_int32 (data)) {
                        gf_log (this->name, GF_LOG_ERROR,
                                "error with stripe-count xattr (%d != %d)",
                                local->fctx->stripe_count, data_to_int32 (data));
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
                                "error with stripe-index xattr (%d)", index);
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
                        gf_log (this->name, GF_LOG_ERROR,
                                "entry-count (%d) != stripe-count (%d)",
                                local->entry_count, local->fctx->stripe_count);
                        local->op_ret = -1;
                        local->op_errno = EIO;
                        goto err;
                }
                if (!local->fctx->stripe_size) {
                        gf_log (this->name, GF_LOG_ERROR, "stripe size not set");
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
        STRIPE_STACK_UNWIND (open, frame, local->op_ret, local->op_errno,
                             local->fd);
out:
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
        dict_t           *dict = NULL;
        int               ret = 0;
        char              key[256] = {0,};

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
                dict = dict_new ();
                if (!dict)
                        goto err;

                sprintf (key, "trusted.%s.stripe-size", this->name);
                ret = dict_set_int64 (dict, key, 8);
                if (ret)
                        gf_log (this->name, GF_LOG_WARNING,
                                "failed to set %s in xattr_req dict", key);

                sprintf (key, "trusted.%s.stripe-count", this->name);
                ret = dict_set_int32 (dict, key, 4);
                if (ret)
                        gf_log (this->name, GF_LOG_WARNING,
                                "failed to set %s in xattr_req dict", key);

                sprintf (key, "trusted.%s.stripe-index", this->name);
                ret = dict_set_int32 (dict, key, 4);
                if (ret)
                        gf_log (this->name, GF_LOG_WARNING,
                                "failed to set %s in xattr_req dict", key);

                while (trav) {
                        STACK_WIND (frame, stripe_open_lookup_cbk,
                                    trav->xlator, trav->xlator->fops->lookup,
                                    loc, dict);
                        trav = trav->next;
                }
                if (dict)
                        dict_unref (dict);

                return 0;
        }
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
        return 0;
err:
        STRIPE_STACK_UNWIND (open, frame, -1, op_errno, NULL);
        return 0;
}


int32_t
stripe_opendir_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                    int32_t op_ret, int32_t op_errno, fd_t *fd)
{
        int32_t         callcnt = 0;
        stripe_local_t *local = NULL;
        call_frame_t   *prev = NULL;

        if (!this || !frame || !frame->local || !cookie) {
                gf_log ("stripe", GF_LOG_DEBUG, "possible NULL deref");
                goto out;
        }

        prev  = cookie;
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

                if (op_ret >= 0)
                        local->op_ret = op_ret;
        }
        UNLOCK (&frame->lock);

        if (!callcnt) {
                STRIPE_STACK_UNWIND (opendir, frame, local->op_ret,
                                     local->op_errno, local->fd);
        }
out:
        return 0;
}


int32_t
stripe_opendir (call_frame_t *frame, xlator_t *this, loc_t *loc, fd_t *fd)
{
        xlator_list_t    *trav = NULL;
        stripe_local_t   *local = NULL;
        stripe_private_t *priv = NULL;
        int32_t           op_errno = EINVAL;

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
        STRIPE_STACK_UNWIND (opendir, frame, -1, op_errno, NULL);
        return 0;
}

int32_t
stripe_lk_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
               int32_t op_ret, int32_t op_errno, struct gf_flock *lock)
{
        int32_t         callcnt = 0;
        stripe_local_t *local = NULL;
        call_frame_t   *prev = NULL;

        if (!this || !frame || !frame->local || !cookie) {
                gf_log ("stripe", GF_LOG_DEBUG, "possible NULL deref");
                goto out;
        }

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
                STRIPE_STACK_UNWIND (lk, frame, local->op_ret,
                                     local->op_errno, &local->lock);
        }
out:
        return 0;
}

int32_t
stripe_lk (call_frame_t *frame, xlator_t *this, fd_t *fd, int32_t cmd,
           struct gf_flock *lock)
{
        stripe_local_t   *local = NULL;
        xlator_list_t    *trav = NULL;
        stripe_private_t *priv = NULL;
        int32_t           op_errno = EINVAL;

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
        STRIPE_STACK_UNWIND (lk, frame, -1, op_errno, NULL);
        return 0;
}


int32_t
stripe_flush_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                  int32_t op_ret, int32_t op_errno)
{
        int32_t         callcnt = 0;
        stripe_local_t *local   = NULL;
        call_frame_t   *prev = NULL;

        if (!this || !frame || !frame->local || !cookie) {
                gf_log ("stripe", GF_LOG_DEBUG, "possible NULL deref");
                goto out;
        }

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

                STRIPE_STACK_UNWIND (flush, frame, local->op_ret,
                                     local->op_errno);
        }
out:
        return 0;
}

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
                STACK_WIND (frame, stripe_flush_cbk,  trav->xlator,
                            trav->xlator->fops->flush, fd);
                trav = trav->next;
        }

        return 0;
err:
        STRIPE_STACK_UNWIND (flush, frame, -1, op_errno);
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

        if (!this || !frame || !frame->local || !cookie) {
                gf_log ("stripe", GF_LOG_DEBUG, "possible NULL deref");
                goto out;
        }

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

                if (local->op_ret != -1) {
                        local->pre_buf.ia_blocks  = local->prebuf_blocks;
                        local->pre_buf.ia_size    = local->prebuf_size;
                        local->post_buf.ia_blocks = local->postbuf_blocks;
                        local->post_buf.ia_size   = local->postbuf_size;
                }

                STRIPE_STACK_UNWIND (fsync, frame, local->op_ret,
                                     local->op_errno, &local->pre_buf,
                                     &local->post_buf);
        }
out:
        return 0;
}

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
        STRIPE_STACK_UNWIND (fsync, frame, -1, op_errno, NULL, NULL);
        return 0;
}

int32_t
stripe_fstat_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                  int32_t op_ret, int32_t op_errno, struct iatt *buf)
{
        int32_t         callcnt = 0;
        stripe_local_t *local = NULL;
        call_frame_t   *prev = NULL;

        if (!this || !frame || !frame->local || !cookie) {
                gf_log ("stripe", GF_LOG_DEBUG, "possible NULL deref");
                goto out;
        }

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

                        if (FIRST_CHILD(this) == prev->this)
                                local->stbuf = *buf;

                        local->stbuf_blocks += buf->ia_blocks;
                        if (local->stbuf_size < buf->ia_size)
                                local->stbuf_size = buf->ia_size;
                }
        }
        UNLOCK (&frame->lock);

        if (!callcnt) {
                if (local->failed)
                        local->op_ret = -1;

                if (local->op_ret != -1) {
                        local->stbuf.ia_size   = local->stbuf_size;
                        local->stbuf.ia_blocks = local->stbuf_blocks;
                }

                STRIPE_STACK_UNWIND (fstat, frame, local->op_ret,
                                     local->op_errno, &local->stbuf);
        }

out:
        return 0;
}

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
                STACK_WIND (frame, stripe_fstat_cbk, trav->xlator,
                            trav->xlator->fops->fstat, fd);
                trav = trav->next;
        }

        return 0;
err:
        STRIPE_STACK_UNWIND (fstat, frame, -1, op_errno, NULL);
        return 0;
}


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
        STRIPE_STACK_UNWIND (ftruncate, frame, -1, op_errno, NULL, NULL);
        return 0;
}


int32_t
stripe_fsyncdir_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                     int32_t op_ret, int32_t op_errno)
{
        int32_t         callcnt = 0;
        stripe_local_t *local   = NULL;
        call_frame_t   *prev = NULL;

        if (!this || !frame || !frame->local || !cookie) {
                gf_log ("stripe", GF_LOG_DEBUG, "possible NULL deref");
                goto out;
        }

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

                STRIPE_STACK_UNWIND (fsyncdir, frame, local->op_ret,
                                     local->op_errno);
        }
out:
        return 0;
}

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
                STACK_WIND (frame, stripe_fsyncdir_cbk, trav->xlator,
                            trav->xlator->fops->fsyncdir, fd, flags);
                trav = trav->next;
        }

        return 0;
err:
        STRIPE_STACK_UNWIND (fsyncdir, frame, -1, op_errno);
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

        if (!this || !frame || !frame->local) {
                gf_log ("stripe", GF_LOG_DEBUG, "possible NULL deref");
                goto out;
        }

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
                STRIPE_STACK_UNWIND (readv, frame, op_ret, op_errno, vec,
                                     count, &tmp_stbuf, tmp_iobref);

                iobref_unref (tmp_iobref);
                if (vec)
                        GF_FREE (vec);
        }
out:
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

        if (!this || !frame || !frame->local || !cookie) {
                gf_log ("stripe", GF_LOG_DEBUG, "possible NULL deref");
                goto end;
        }

        local  = frame->local;
        index  = local->node_index;
        mframe = local->orig_frame;
        if (!mframe)
                goto out;

        mlocal = mframe->local;
        if (!mlocal)
                goto out;

        fctx = mlocal->fctx;

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
                STRIPE_STACK_UNWIND (readv, mframe, op_ret, op_errno, final_vec,
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
        STRIPE_STACK_DESTROY (frame);
end:
        return 0;
}


int32_t
stripe_readv (call_frame_t *frame, xlator_t *this, fd_t *fd,
              size_t size, off_t offset)
{
        int32_t           op_errno = EINVAL;
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
        stripe_fd_ctx_t  *fctx = NULL;

        VALIDATE_OR_GOTO (frame, err);
        VALIDATE_OR_GOTO (this, err);
        VALIDATE_OR_GOTO (fd, err);
        VALIDATE_OR_GOTO (fd->inode, err);

        fd_ctx_get (fd, this, &tmp_fctx);
        if (!tmp_fctx) {
                op_errno = EBADFD;
                goto err;
        }
        fctx = (stripe_fd_ctx_t *)(long)tmp_fctx;
        stripe_size = fctx->stripe_size;

        if (!stripe_size) {
                gf_log (this->name, GF_LOG_DEBUG,
                        "Wrong stripe size for the file");
                goto err;
        }
        /* The file is stripe across the child nodes. Send the read request
         * to the child nodes appropriately after checking which region of
         * the file is in which child node. Always '0-<stripe_size>' part of
         * the file resides in the first child.
         */
        rounded_start = floor (offset, stripe_size);
        rounded_end = roof (offset+size, stripe_size);
        num_stripe = (rounded_end- rounded_start)/stripe_size;

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
        if (rframe)
                STRIPE_STACK_DESTROY (rframe);

        STRIPE_STACK_UNWIND (readv, frame, -1, op_errno, NULL, 0, NULL, NULL);
        return 0;
}


int32_t
stripe_writev_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                   int32_t op_ret, int32_t op_errno, struct iatt *prebuf,
                   struct iatt *postbuf)
{
        int32_t         callcnt = 0;
        stripe_local_t *local = NULL;
        call_frame_t   *prev = NULL;

        if (!this || !frame || !frame->local || !cookie) {
                gf_log ("stripe", GF_LOG_DEBUG, "possible NULL deref");
                goto out;
        }

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
                STRIPE_STACK_UNWIND (writev, frame, local->op_ret,
                                     local->op_errno, &local->pre_buf,
                                     &local->post_buf);
        }
out:
        return 0;
}

int32_t
stripe_writev (call_frame_t *frame, xlator_t *this, fd_t *fd,
               struct iovec *vector, int32_t count, off_t offset,
               struct iobref *iobref)
{
        struct iovec     *tmp_vec = NULL;
        stripe_local_t   *local = NULL;
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

        VALIDATE_OR_GOTO (frame, err);
        VALIDATE_OR_GOTO (this, err);
        VALIDATE_OR_GOTO (fd, err);
        VALIDATE_OR_GOTO (fd->inode, err);

        fd_ctx_get (fd, this, &tmp_fctx);
        if (!tmp_fctx) {
                op_errno = EINVAL;
                goto err;
        }
        fctx = (stripe_fd_ctx_t *)(long)tmp_fctx;
        stripe_size = fctx->stripe_size;

        /* File has to be stripped across the child nodes */
        for (idx = 0; idx< count; idx ++) {
                total_size += vector[idx].iov_len;
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

                STACK_WIND (frame, stripe_writev_cbk, fctx->xl_array[idx],
                            fctx->xl_array[idx]->fops->writev, fd, tmp_vec,
                            tmp_count, offset + offset_offset, iobref);
                GF_FREE (tmp_vec);
                offset_offset += fill_size;
                if (remaining_size == 0)
                        break;
        }

        return 0;
err:
        STRIPE_STACK_UNWIND (writev, frame, -1, op_errno, NULL, NULL);
        return 0;
}


int32_t
stripe_release (xlator_t *this, fd_t *fd)
{
        uint64_t          tmp_fctx = 0;
        stripe_fd_ctx_t  *fctx = NULL;

        VALIDATE_OR_GOTO (this, err);
        VALIDATE_OR_GOTO (fd, err);

        fd_ctx_del (fd, this, &tmp_fctx);
        if (!tmp_fctx) {
                goto err;
        }

        fctx = (stripe_fd_ctx_t *)(long)tmp_fctx;

        if (!fctx->static_array)
                GF_FREE (fctx->xl_array);

        GF_FREE (fctx);

err:
	return 0;
}


int32_t
notify (xlator_t *this, int32_t event, void *data, ...)
{
        stripe_private_t *priv = NULL;
        int               down_client = 0;
        int               i = 0;

        if (!this)
                return 0;

        priv = this->private;
        if (!priv)
                return 0;

        switch (event)
        {
        case GF_EVENT_CHILD_UP:
        case GF_EVENT_CHILD_CONNECTING:
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

        if (!this || !priv || !data)
                goto out;

        /* Get the pattern for striping.
           "option block-size *avi:10MB" etc */
        stripe_str = strtok_r (data, ",", &tmp_str);
        while (stripe_str) {
                dup_str = gf_strdup (stripe_str);
                stripe_opt = CALLOC (1, sizeof (struct stripe_options));
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

                if (stripe_opt->block_size < 512) {
                        gf_log (this->name, GF_LOG_ERROR, "Invalid Block-size: "
                                "%s. Should be atleast 512 bytes", num);
                        goto out;
                }
                if (stripe_opt->block_size % 512) {
                        gf_log (this->name, GF_LOG_ERROR, "Block-size: %s should"
                                " be a multiple of 512 bytes", num);
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
stripe_iatt_merge (struct iatt *from, struct iatt *to)
{
        if (to->ia_size < from->ia_size)
                to->ia_size = from->ia_size;
        if (to->ia_mtime < from->ia_mtime)
                to->ia_mtime = from->ia_mtime;
        if (to->ia_ctime < from->ia_ctime)
                to->ia_ctime = from->ia_ctime;
        if (to->ia_atime < from->ia_atime)
                to->ia_atime = from->ia_atime;
        return 0;
}

int32_t
stripe_readdirp_entry_stat_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                                int32_t op_ret, int32_t op_errno, struct iatt *buf)
{
        gf_dirent_t    *entry = NULL;
        stripe_local_t *local = NULL;
        int32_t        done = 0;

        if (!this || !frame || !frame->local || !cookie) {
                gf_log (this->name, GF_LOG_DEBUG, "possible NULL deref");
                goto out;
        }
        entry = cookie;
        local = frame->local;
        LOCK (&frame->lock);
        {

                local->wind_count--;
                if (!local->wind_count)
                        done = 1;
                if (op_ret == -1) {
                        local->op_errno = op_errno;
                        local->op_ret = op_ret;
                        goto unlock;
                }
                stripe_iatt_merge (buf, &entry->d_stat);
        }
unlock:
        UNLOCK(&frame->lock);

        if (done) {
                frame->local = NULL;
                STRIPE_STACK_UNWIND (readdir, frame, local->op_ret,
                                     local->op_errno, &local->entries);

                gf_dirent_free (&local->entries);
                stripe_local_wipe (local);
                GF_FREE (local);
        }
out:
        return 0;

}

int32_t
stripe_readdirp_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                     int32_t op_ret, int32_t op_errno, gf_dirent_t *orig_entries)
{
        stripe_local_t *local = NULL;
        call_frame_t   *prev = NULL;
        gf_dirent_t    *local_entry = NULL;
        int32_t        ret = -1;
        gf_dirent_t    *tmp_entry = NULL;
        xlator_list_t  *trav = NULL;
        loc_t          loc = {0, };
        inode_t        *inode = NULL;
        char           *path;
        int32_t        count = 0;
        stripe_private_t *priv = NULL;
        int32_t        subvols = 0;

        if (!this || !frame || !frame->local || !cookie) {
                gf_log ("stripe", GF_LOG_DEBUG, "possible NULL deref");
                goto out;
        }
        prev  = cookie;
        local = frame->local;
        trav = this->children;
        priv = this->private;

        subvols = priv->child_count;

        LOCK (&frame->lock);
        {
                if (op_ret == -1) {
                        gf_log (this->name, GF_LOG_WARNING,
                                "%s returned error %s",
                                prev->this->name, strerror (op_errno));
                        local->op_errno = op_errno;
                        local->op_ret = op_ret;
                        goto unlock;
                } else {
                        local->op_ret = op_ret;
                        list_splice_init (&orig_entries->list,
                                          &local->entries.list);
                        local->wind_count = op_ret * subvols;
                }
        }
unlock:
        UNLOCK (&frame->lock);

        if (op_ret == -1)
                goto out;

        count = op_ret;
        ret = 0;
        list_for_each_entry_safe (local_entry, tmp_entry,
                                  (&local->entries.list), list) {

                if (!local_entry)
                        break;
                if (!IA_ISREG (local_entry->d_stat.ia_type)) {
                        LOCK (&frame->lock);
                        {
                                local->wind_count -= subvols;
                                count = local->wind_count;
                        }
                        UNLOCK (&frame->lock);
                        continue;
                }

                inode = inode_new (local->fd->inode->table);
                if (!inode)
                        goto out;

                loc.ino = inode->ino = local_entry->d_ino;
                loc.inode = inode;
                loc.parent = local->fd->inode;
                ret = inode_path (local->fd->inode, local_entry->d_name, &path);
                if (ret != -1) {
                        loc.path = path;
                } else  if (inode) {
                        ret = inode_path (inode, NULL, &path);
                        if (ret != -1) {
                                loc.path = path;
                        } else {
                                goto out;
                        }
                }

                loc.name = strrchr (loc.path, '/');
                loc.name++;
                trav = this->children;
                while (trav) {
                        STACK_WIND_COOKIE (frame, stripe_readdirp_entry_stat_cbk,
                                           local_entry, trav->xlator,
                                           trav->xlator->fops->stat, &loc);
                        trav = trav->next;
                }
                inode_unref (loc.inode);
        }
out:
        if (!count) {
                /* all entries are directories */
                frame->local = NULL;
                STRIPE_STACK_UNWIND (readdir, frame, local->op_ret,
                                     local->op_errno, &local->entries);
                gf_dirent_free (&local->entries);
                stripe_local_wipe (local);
                GF_FREE (local);
        }

        return 0;

}
int32_t
stripe_readdirp (call_frame_t *frame, xlator_t *this,
                 fd_t *fd, size_t size, off_t off)
{
        stripe_local_t  *local  = NULL;
        stripe_private_t *priv = NULL;
        xlator_list_t   *trav = NULL;
        int             op_errno = -1;


        VALIDATE_OR_GOTO (frame, err);
        VALIDATE_OR_GOTO (this, err);
        VALIDATE_OR_GOTO (fd, err);

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

        local->fd = fd_ref (fd);

        local->wind_count = 0;

        local->count = 0;
        local->op_ret = -1;
        INIT_LIST_HEAD(&local->entries);

        if (!trav)
                goto err;

        STACK_WIND (frame, stripe_readdirp_cbk, trav->xlator,
                    trav->xlator->fops->readdirp, fd, size, off);
        return 0;
err:
        op_errno = (op_errno == -1) ? errno : op_errno;
        STRIPE_STACK_UNWIND (readdir, frame, -1, op_errno, NULL);

        return 0;

}
int32_t
mem_acct_init (xlator_t *this)
{
        int     ret = -1;

        if (!this)
                goto out;

        ret = xlator_mem_acct_init (this, gf_stripe_mt_end + 1);

        if (ret != 0) {
                gf_log (this->name, GF_LOG_ERROR, "Memory accounting init"
                        "failed");
                goto out;
        }

out:
        return ret;
}


int
reconfigure (xlator_t *this, dict_t *options)
{

	stripe_private_t *priv = NULL;
	int		  ret = -1;

	priv = this->private;

        GF_OPTION_RECONF ("block-size", priv->block_size, options, size, out);

        ret = 0;
out:
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

        if (!this)
                goto out;

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


        GF_OPTION_INIT ("block-size", priv->block_size, size, out);

        /* option stripe-pattern *avi:1GB,*pdf:4096 */
        data = dict_get (this->options, "block-size");
        if (data) {
                ret = set_stripe_block_size (this, priv, data->data);
                if (ret)
                        goto out;
        }

        GF_OPTION_INIT ("use-xattr", priv->xattr_supported, bool, out);

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

        if (!this)
                goto out;

        priv = this->private;
        if (priv) {
                this->private = NULL;
                if (priv->xl_array)
                        GF_FREE (priv->xl_array);

                trav = priv->pattern;
                while (trav) {
                        prev = trav;
                        trav = trav->next;
                        FREE (prev);
                }
                LOCK_DESTROY (&priv->lock);
                GF_FREE (priv);
        }

out:
        return;
}

int32_t
stripe_getxattr_unwind (call_frame_t *frame,
                        int op_ret, int op_errno, dict_t *dict)

{
        STRIPE_STACK_UNWIND (getxattr, frame, op_ret, op_errno, dict);
        return 0;
}


int
stripe_getxattr_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                     int op_ret, int op_errno, dict_t *xattr)
{
        int                     call_cnt = 0;
        stripe_local_t         *local = NULL;

        VALIDATE_OR_GOTO (frame, out);
        VALIDATE_OR_GOTO (frame->local, out);

        local = frame->local;

        LOCK (&frame->lock);
        {
                call_cnt = --local->wind_count;
        }
        UNLOCK (&frame->lock);

        if (!xattr || (op_ret < 0))
                goto out;

        local->op_ret = 0;

        if (!local->xattr) {
                local->xattr = dict_ref (xattr);
        } else {
                stripe_aggregate_xattr (local->xattr, xattr);
        }

out:
        if (!call_cnt) {
                STRIPE_STACK_UNWIND (getxattr, frame, local->op_ret, op_errno,
                                     local->xattr);
        }

        return 0;
}

int32_t
stripe_pathinfo_aggregate (char *buffer, stripe_local_t *local, int32_t *total)
{
        int32_t              i     = 0;
        int32_t              ret   = -1;
        int32_t              len   = 0;
        char                *sbuf  = NULL;
        stripe_xattr_sort_t *xattr = NULL;

        if (!buffer || !local || !local->xattr_list)
                goto out;

        sbuf = buffer;

        for (i = 0; i < local->nallocs; i++) {
                xattr = local->xattr_list + i;
                len = xattr->pathinfo_len;

                if (len && xattr && xattr->pathinfo) {
                        memcpy (buffer, xattr->pathinfo, len);
                        buffer += len;
                        *buffer++ = ' ';
                }
        }

        *--buffer = '\0';
        if (total)
                *total = buffer - sbuf;
        ret = 0;

 out:
        return ret;
}

int32_t
stripe_free_pathinfo_str (stripe_local_t *local)
{
        int32_t              i     = 0;
        int32_t              ret   = -1;
        stripe_xattr_sort_t *xattr = NULL;

        if (!local || !local->xattr_list)
                goto out;

        for (i = 0; i < local->nallocs; i++) {
                xattr = local->xattr_list + i;

                if (xattr && xattr->pathinfo)
                        GF_FREE (xattr->pathinfo);
        }

        ret = 0;
 out:
        return ret;
}

int32_t
stripe_getxattr_pathinfo_cbk (call_frame_t *frame, void *cookie,
                             xlator_t *this, int32_t op_ret, int32_t op_errno,
                             dict_t *dict) {
        stripe_local_t      *local         = NULL;
        int32_t              callcnt       = 0;
        int32_t              ret           = -1;
        long                 cky           = 0;
        char                *pathinfo      = NULL;
        char                *pathinfo_serz = NULL;
        int32_t              padding       = 0;
        int32_t              tlen          = 0;
        char stripe_size_str[20]           = {0,};
        stripe_xattr_sort_t *xattr         = NULL;
        dict_t              *stripe_xattr  = NULL;

        if (!frame || !frame->local || !this) {
                gf_log (this->name, GF_LOG_ERROR, "Possible NULL deref");
                return ret;
        }

        local = frame->local;
        cky = (long) cookie;

        LOCK (&frame->lock);
        {
                callcnt = --local->wind_count;

                if (!dict || (op_ret < 0))
                        goto out;

                if (!local->xattr_list)
                        local->xattr_list = (stripe_xattr_sort_t *) GF_CALLOC (local->nallocs,
                                                                               sizeof (stripe_xattr_sort_t),
                                                                               gf_stripe_mt_xattr_sort_t);

                if (local->xattr_list) {
                        ret = dict_get_str (dict, GF_XATTR_PATHINFO_KEY, &pathinfo);
                        if (ret)
                                goto out;

                        xattr = local->xattr_list + (int32_t) cky;

                        pathinfo = gf_strdup (pathinfo);
                        xattr->pos = cky;
                        xattr->pathinfo = pathinfo;
                        xattr->pathinfo_len = strlen (pathinfo);

                        local->xattr_total_len += strlen (pathinfo) + 1;
                }
        }
 out:
        UNLOCK (&frame->lock);

        if (!callcnt) {
                if (!local->xattr_total_len)
                        goto unwind;

                stripe_xattr = dict_new ();
                if (!stripe_xattr)
                        goto unwind;

                snprintf (stripe_size_str, 20, "%ld", local->stripe_size);

                /* extra bytes for decorations (brackets and <>'s) */
                padding = strlen (this->name) + strlen (STRIPE_PATHINFO_HEADER)
                        + strlen (stripe_size_str) + 7;
                local->xattr_total_len += (padding + 2);

                pathinfo_serz = GF_CALLOC (local->xattr_total_len, sizeof (char),
                                           gf_common_mt_char);
                if (!pathinfo_serz)
                        goto unwind;

                /* xlator info */
                sprintf (pathinfo_serz, "(<"STRIPE_PATHINFO_HEADER"%s:[%s]> ", this->name, stripe_size_str);

                ret = stripe_pathinfo_aggregate (pathinfo_serz + padding, local, &tlen);
                if (ret) {
                        gf_log (this->name, GF_LOG_ERROR, "Cannot aggregate pathinfo list");
                        goto unwind;
                }

                *(pathinfo_serz + padding + tlen) = ')';
                *(pathinfo_serz + padding + tlen + 1) = '\0';

                ret = dict_set_dynstr (stripe_xattr, GF_XATTR_PATHINFO_KEY, pathinfo_serz);
                if (ret)
                        gf_log (this->name, GF_LOG_ERROR, "Cannot set pathinfo key in dict");

        unwind:
                STRIPE_STACK_UNWIND (getxattr, frame, op_ret, op_errno, stripe_xattr);

                ret = stripe_free_pathinfo_str (local);

                if (local->xattr_list)
                        GF_FREE (local->xattr_list);

                if (stripe_xattr)
                        dict_unref (stripe_xattr);
        }

        return ret;
}

int32_t
stripe_getxattr (call_frame_t *frame, xlator_t *this,
                 loc_t *loc, const char *name)
{
        stripe_local_t     *local = NULL;
        xlator_list_t      *trav = NULL;
        stripe_private_t   *priv = NULL;
        int32_t             op_errno = EINVAL;
        int                 i = 0;
        xlator_t          **sub_volumes;

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


        if (name && (strcmp (GF_XATTR_MARKER_KEY, name) == 0)
            && (-1 == frame->root->pid)) {
                local->marker.call_count = priv->child_count;

                sub_volumes = alloca ( priv->child_count *
                                       sizeof (xlator_t *));
                for (i = 0, trav = this->children; trav ;
                     trav = trav->next, i++) {

                        *(sub_volumes + i)  = trav->xlator;

                }

                if (cluster_getmarkerattr (frame, this, loc, name,
                                           local, stripe_getxattr_unwind,
                                           sub_volumes, priv->child_count,
                                           MARKER_UUID_TYPE, priv->vol_uuid)) {
                        op_errno = EINVAL;
                        goto err;
                }

                return 0;
        }

        if (name && strncmp (name, GF_XATTR_QUOTA_SIZE_KEY,
                             strlen (GF_XATTR_QUOTA_SIZE_KEY)) == 0) {
                local->wind_count = priv->child_count;

                for (i = 0, trav=this->children; i < priv->child_count; i++,
                             trav = trav->next) {
                        STACK_WIND (frame, stripe_getxattr_cbk,
                                    trav->xlator, trav->xlator->fops->getxattr,
                                    loc, name);
                }

                return 0;
        }

        if (name && (strncmp (name, GF_XATTR_PATHINFO_KEY,
                              strlen (GF_XATTR_PATHINFO_KEY)) == 0)) {
                local->stripe_size = stripe_get_matching_bs (loc->path,
                                                             priv->pattern,
                                                             priv->block_size);
                local->nallocs = local->wind_count = priv->child_count;

                for (i = 0, trav = this->children; i < priv->child_count; i++,
                     trav = trav->next) {
                        STACK_WIND_COOKIE (frame, stripe_getxattr_pathinfo_cbk,
                                           (void *) (long) i, trav->xlator,
                                           trav->xlator->fops->getxattr,
                                           loc, name);
                }

                return 0;
        }

        if (name &&(*priv->vol_uuid)) {
                if ((match_uuid_local (name, priv->vol_uuid) == 0)
                    && (-1 == frame->root->pid)) {
                        local->marker.call_count = priv->child_count;

                        sub_volumes = alloca ( priv->child_count *
                                               sizeof (xlator_t *));
                        for (i = 0, trav = this->children; trav ;
                             trav = trav->next, i++) {

                                *(sub_volumes + i) = trav->xlator;

                        }

                        if (cluster_getmarkerattr (frame, this, loc, name,
                                                   local, stripe_getxattr_unwind,
                                                   sub_volumes,
                                                   priv->child_count,
                                                   MARKER_XTIME_TYPE,
                                                   priv->vol_uuid)) {
                                op_errno = EINVAL;
                                goto err;
                        }
                        return 0;
                }
        }


        STACK_WIND (frame, default_getxattr_cbk, FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->getxattr, loc, name);

        return 0;

err:
        STRIPE_STACK_UNWIND (getxattr, frame, -1, op_errno, NULL);
        return 0;
}

int32_t
stripe_priv_dump (xlator_t *this)
{
        char                    key_prefix[GF_DUMP_MAX_BUF_LEN];
        char                    key[GF_DUMP_MAX_BUF_LEN];
        int                     i = 0;
        stripe_private_t       *priv = NULL;
        int                     ret = -1;
        struct stripe_options  *options = NULL;

        GF_VALIDATE_OR_GOTO ("stripe", this, out);

        priv = this->private;
        if (!priv)
                goto out;

        ret = TRY_LOCK (&priv->lock);
        if (ret != 0)
                goto out;

        gf_proc_dump_add_section("xlator.cluster.stripe.%s.priv", this->name);
        gf_proc_dump_build_key(key_prefix,"xlator.cluster.stripe","%s.priv",
                               this->name);
        gf_proc_dump_build_key(key, key_prefix, "child_count");
        gf_proc_dump_write(key,"%d", priv->child_count);

        for (i = 0; i < priv->child_count; i++) {
                gf_proc_dump_build_key (key, key_prefix, "subvolumes[%d]", i);
                gf_proc_dump_write (key, "%s.%s", priv->xl_array[i]->type,
                                    priv->xl_array[i]->name);
        }

        options = priv->pattern;
        while (options != NULL) {
                gf_proc_dump_build_key (key, key_prefix, "path_pattern");
                gf_proc_dump_write (key, "%s", priv->pattern->path_pattern);

                gf_proc_dump_build_key (key, key_prefix, "options_block_size");
                gf_proc_dump_write (key, "%ul", options->block_size);

                options = options->next;
        }

        gf_proc_dump_build_key (key, key_prefix, "block_size");
        gf_proc_dump_write (key, "%ul", priv->block_size);

        gf_proc_dump_build_key (key, key_prefix, "nodes_down");
        gf_proc_dump_write (key, "%d", priv->nodes_down);

        gf_proc_dump_build_key (key, key_prefix, "first_child_down");
        gf_proc_dump_write (key, "%d", priv->first_child_down);

        gf_proc_dump_build_key (key, key_prefix, "xatter_supported");
        gf_proc_dump_write (key, "%d", priv->xattr_supported);

        UNLOCK (&priv->lock);

out:
        return ret;
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

        .getxattr    = stripe_getxattr,
        .readdirp    = stripe_readdirp,
};

struct xlator_cbks cbks = {
        .release = stripe_release,
};

struct xlator_dumpops dumpops = {
        .priv = stripe_priv_dump,
};

struct volume_options options[] = {
        { .key  = {"block-size"},
          .type = GF_OPTION_TYPE_ANY,
          .default_value = "128KB",
          .description = "Size of the stripe unit that would be read "
                         "from or written to the striped servers."
        },
        { .key  = {"use-xattr"},
          .type = GF_OPTION_TYPE_BOOL,
          .default_value = "true"
        },
        { .key  = {NULL} },
};
