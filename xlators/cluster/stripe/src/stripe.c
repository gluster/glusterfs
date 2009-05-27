/*
  Copyright (c) 2007-2009 Z RESEARCH, Inc. <http://www.zresearch.com>
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
        pathname   = strdup (path);
        trav       = opts;

        while (trav) {
                if (!fnmatch (trav->path_pattern, pathname, FNM_NOESCAPE)) {
                        block_size = trav->block_size;
                        break;
                }
                trav = trav->next;
        }
        free (pathname);
        
        return block_size;
}


/*
 * stripe_common_cbk -
 */
int32_t
stripe_common_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                   int32_t op_ret, int32_t op_errno)
{
        STACK_UNWIND (frame, op_ret, op_errno);
        return 0;
}

/**
 * stripe_stack_unwind_cbk -  This function is used for all the _cbk without 
 *     any extra arguments (other than the minimum given)
 * This is called from functions like fsync,unlink,rmdir etc.
 *
 */
int32_t 
stripe_stack_unwind_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                         int32_t op_ret, int32_t op_errno)
{
        int32_t         callcnt = 0;
        stripe_local_t *local   = NULL;

        local = frame->local;

        LOCK (&frame->lock);
        {
                callcnt = --local->call_count;

                if (op_ret == -1) {
                        gf_log (this->name, GF_LOG_DEBUG, 
                                "%s returned %s",
                                ((call_frame_t *)cookie)->this->name, 
                                strerror (op_errno));                   
                        local->op_errno = op_errno;
                        if (op_errno == ENOTCONN) 
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
stripe_common_buf_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                       int32_t op_ret, int32_t op_errno, struct stat *buf)
{
        STACK_UNWIND (frame, op_ret, op_errno, buf);
        return 0;
}

/**
 * stripe_stack_unwind_buf_cbk -  This function is used for all the _cbk with 
 *    'struct stat *buf' as extra argument (other than minimum)
 * This is called from functions like, chmod, fchmod, chown, fchown,
 * truncate, ftruncate, utimens etc.
 *
 * @cookie - this argument should be always 'xlator_t *' of child node 
 */
int32_t 
stripe_stack_unwind_buf_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                             int32_t op_ret, int32_t op_errno,
                             struct stat *buf)
{
        int32_t         callcnt = 0;
        stripe_local_t *local = NULL;

        local = frame->local;

        LOCK (&frame->lock);
        {
                callcnt = --local->call_count;

                if (op_ret == -1) {
                        gf_log (this->name, GF_LOG_DEBUG, 
                                "%s returned error %s",
                                ((call_frame_t *)cookie)->this->name, 
                                strerror (op_errno));
                        local->op_errno = op_errno;
                        if (op_errno == ENOTCONN)
                                local->failed = 1;
                }
    
                if (op_ret == 0) {
                        local->op_ret = 0;
                        if (local->stbuf.st_blksize == 0) {
                                local->stbuf = *buf;
                                /* Because st_blocks gets added again */
                                local->stbuf.st_blocks = 0;
                        }

                        if (FIRST_CHILD(this) == 
                            ((call_frame_t *)cookie)->this) {
                                /* Always, pass the inode number of 
                                   first child to the above layer */
                                local->stbuf.st_ino = buf->st_ino;
                                local->stbuf.st_mtime = buf->st_mtime;
                        }

                        local->stbuf.st_blocks += buf->st_blocks;
                        if (local->stbuf.st_size < buf->st_size)
                                local->stbuf.st_size = buf->st_size;
                        if (local->stbuf.st_blksize != buf->st_blksize) {
                                /* TODO: add to blocks in terms of 
                                   original block size */
                        }
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
                              &local->stbuf);
        }

        return 0;
}

/* In case of symlink, mknod, the file is created on just first node */
int32_t 
stripe_common_inode_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                         int32_t op_ret, int32_t op_errno, inode_t *inode,
                         struct stat *buf)
{
        STACK_UNWIND (frame, op_ret, op_errno, inode, buf);
        return 0;
}

/**
 * stripe_stack_unwind_inode_cbk - This is called by the function like, 
 *                   link (), symlink (), mkdir (), mknod () 
 *           This creates a inode for new inode. It keeps a list of all 
 *           the inodes received from the child nodes. It is used while 
 *           forwarding any fops to child nodes.
 *
 */
int32_t 
stripe_stack_unwind_inode_cbk (call_frame_t *frame, void *cookie,
                               xlator_t *this, int32_t op_ret, 
                               int32_t op_errno, inode_t *inode,
                               struct stat *buf)
{
        int32_t         callcnt = 0;
        stripe_local_t *local   = NULL;

        local = frame->local;

        LOCK (&frame->lock);
        {
                callcnt = --local->call_count;
    
                if (op_ret == -1) {
                        gf_log (this->name, GF_LOG_DEBUG, 
                                "%s returned error %s",
                                ((call_frame_t *)cookie)->this->name, 
                                strerror (op_errno));
                        local->op_errno = op_errno;
                        if (op_errno == ENOTCONN)
                                local->failed = 1;
                }
 
                if (op_ret >= 0) {
                        local->op_ret = 0;

                        if (local->stbuf.st_blksize == 0) {
                                local->inode = inode;
                                local->stbuf = *buf;
                                /* Because st_blocks gets added again */
                                local->stbuf.st_blocks = 0;
                        }
                        if (FIRST_CHILD(this) == 
                            ((call_frame_t *)cookie)->this) {
                                local->stbuf.st_ino = buf->st_ino;
                                local->stbuf.st_mtime = buf->st_mtime;
                        }

                        local->stbuf.st_blocks += buf->st_blocks;
                        if (local->stbuf.st_size < buf->st_size)
                                local->stbuf.st_size = buf->st_size;
                        if (local->stbuf.st_blksize != buf->st_blksize) {
                                /* TODO: add to blocks in terms of 
                                   original block size */
                        }
                }
        }
        UNLOCK (&frame->lock);

        if (!callcnt) {
                if (local->failed)
                        local->op_ret = -1;

                STACK_UNWIND (frame, local->op_ret, local->op_errno, 
                              local->inode, &local->stbuf);
        }

        return 0;
}

int32_t 
stripe_lookup_cbk (call_frame_t *frame, void *cookie, xlator_t *this, 
                   int32_t op_ret, int32_t op_errno, inode_t *inode,
                   struct stat *buf, dict_t *dict)
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
                        if ((op_errno == ENOTCONN) || (op_errno == ESTALE))
                                local->failed = 1;
                        /* TODO: bring in self-heal ability */
                        /* 
                         * if (local->op_ret == 0) {
                         *       if (S_ISREG (local->stbuf.st_mode) || 
                         *           S_ISDIR (local->stbuf.st_mode))
                         *               local->entry_self_heal_needed = 1;
                         * }
                         */
                }
 
                if (op_ret >= 0) {
                        local->op_ret = 0;

                        if (local->stbuf.st_blksize == 0) {
                                local->inode = inode_ref (inode);
                                local->stbuf = *buf;
                                /* Because st_blocks gets added again */
                                local->stbuf.st_blocks = 0;
                        }
                        if (FIRST_CHILD(this) == prev->this) {
                                local->stbuf.st_ino = buf->st_ino;
                                local->stbuf.st_mtime = buf->st_mtime;
                                if (local->dict)
                                        dict_unref (local->dict);
                                local->dict = dict_ref (dict);
                        } else {
                                if (!local->dict)
                                        local->dict = dict_ref (dict);
                        }
                        local->stbuf.st_blocks += buf->st_blocks;
                        if (local->stbuf.st_size < buf->st_size)
                                local->stbuf.st_size = buf->st_size;
                        if (local->stbuf.st_blksize != buf->st_blksize) {
                                /* TODO: add to blocks in terms of 
                                   original block size */
                        }
                }
        }
        UNLOCK (&frame->lock);

        if (!callcnt) {
                if (local->failed)
                        local->op_ret = -1;

                tmp_dict = local->dict;
                tmp_inode = local->inode;

                STACK_UNWIND (frame, local->op_ret, local->op_errno, 
                              local->inode, &local->stbuf, local->dict);

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
        char              send_lookup_to_all = 0;
        int32_t           op_errno = 1;

        VALIDATE_OR_GOTO (frame, err);
        VALIDATE_OR_GOTO (this, err);
        VALIDATE_OR_GOTO (loc, err);
        VALIDATE_OR_GOTO (loc->path, err);
        VALIDATE_OR_GOTO (loc->inode, err);

        priv = this->private;
        trav = this->children;

        /* Initialization */
        local = CALLOC (1, sizeof (stripe_local_t));
        if (!local) {
                op_errno = ENOMEM;
                goto err;
        }
        local->op_ret = -1;
        frame->local = local;

        if ((!loc->inode->st_mode) || S_ISDIR (loc->inode->st_mode) || 
            S_ISREG (loc->inode->st_mode)) {
                send_lookup_to_all = 1;
        }

        if (send_lookup_to_all) {
                /* Everytime in stripe lookup, all child nodes 
                   should be looked up */
                local->call_count = priv->child_count;
                while (trav) {
                        STACK_WIND (frame, stripe_lookup_cbk, trav->xlator, 
                                    trav->xlator->fops->lookup,
                                    loc, xattr_req);
                        trav = trav->next;
                }
        } else {
                local->call_count = 1;
                
                STACK_WIND (frame, stripe_lookup_cbk, FIRST_CHILD(this), 
                            FIRST_CHILD(this)->fops->lookup,
                            loc, xattr_req);
        }
  
        return 0;
 err:
        STACK_UNWIND (frame, -1, op_errno, NULL, NULL);
        return 0;
}

/**
 * stripe_stat -
 */
int32_t
stripe_stat (call_frame_t *frame, xlator_t *this, loc_t *loc)
{ 
        int               send_lookup_to_all = 0;
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

        if (S_ISDIR (loc->inode->st_mode) || S_ISREG (loc->inode->st_mode))
                send_lookup_to_all = 1;

        if (!send_lookup_to_all) {
                STACK_WIND (frame, stripe_common_buf_cbk, FIRST_CHILD(this), 
                            FIRST_CHILD(this)->fops->stat, loc);
        } else {
                /* Initialization */
                local = CALLOC (1, sizeof (stripe_local_t));
                if (!local) {
                        op_errno = ENOMEM;
                        goto err;
                }
                local->op_ret = -1;
                frame->local = local;
                local->inode = loc->inode;
                local->call_count = priv->child_count;
    
                while (trav) {
                        STACK_WIND (frame, stripe_stack_unwind_buf_cbk,
                                    trav->xlator, trav->xlator->fops->stat, 
                                    loc);
                        trav = trav->next;
                }
        }

        return 0;
 err:
        STACK_UNWIND (frame, -1, op_errno, NULL);
        return 0;
}


/**
 * stripe_chmod -
 */
int32_t
stripe_chmod (call_frame_t *frame, xlator_t *this, loc_t *loc, mode_t mode)
{
        int               send_fop_to_all = 0;
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

        if (S_ISDIR (loc->inode->st_mode) || S_ISREG (loc->inode->st_mode))
                send_fop_to_all = 1;

        if (!send_fop_to_all) {
                STACK_WIND (frame, stripe_common_buf_cbk, FIRST_CHILD(this),
                            FIRST_CHILD(this)->fops->chmod, loc, mode);
        } else {
                /* Initialization */
                local = CALLOC (1, sizeof (stripe_local_t));
                if (!local) {
                        op_errno = ENOMEM;
                        goto err;
                }
                local->op_ret = -1;
                frame->local = local;
                local->inode = loc->inode;
                local->call_count = priv->child_count;

                while (trav) {
                        STACK_WIND (frame, stripe_stack_unwind_buf_cbk,
                                    trav->xlator, trav->xlator->fops->chmod,
                                    loc, mode);
                        trav = trav->next;
                }
        }
        return 0;
 err:
        STACK_UNWIND (frame, -1, op_errno, NULL);
        return 0;
}


/**
 * stripe_chown - 
 */
int32_t
stripe_chown (call_frame_t *frame, xlator_t *this, loc_t *loc, uid_t uid,
              gid_t gid)
{
        int               send_fop_to_all = 0;
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

        if (S_ISDIR (loc->inode->st_mode) || S_ISREG (loc->inode->st_mode))
                send_fop_to_all = 1;

        if (!send_fop_to_all) {
                STACK_WIND (frame, stripe_common_buf_cbk, trav->xlator,
                            trav->xlator->fops->chown, loc, uid, gid);
        } else {
                /* Initialization */
                local = CALLOC (1, sizeof (stripe_local_t));
                if (!local) {
                        op_errno = ENOMEM;
                        goto err;
                }
                local->op_ret = -1;
                frame->local = local;
                local->inode = loc->inode;
                local->call_count = priv->child_count;

                while (trav) {
                        STACK_WIND (frame, stripe_stack_unwind_buf_cbk,
                                    trav->xlator, trav->xlator->fops->chown,
                                    loc, uid, gid);
                        trav = trav->next;
                }
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
        local = CALLOC (1, sizeof (stripe_local_t));
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
        int               send_fop_to_all = 0;
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

        if (S_ISDIR (loc->inode->st_mode) || S_ISREG (loc->inode->st_mode))
                send_fop_to_all = 1;

        if (!send_fop_to_all) {
                STACK_WIND (frame, stripe_common_buf_cbk, trav->xlator,
                            trav->xlator->fops->truncate, loc, offset);
        } else {
                /* Initialization */
                local = CALLOC (1, sizeof (stripe_local_t));
                if (!local) {
                        op_errno = ENOMEM;
                        goto err;
                }
                local->op_ret = -1;
                frame->local = local;
                local->inode = loc->inode;
                local->call_count = priv->child_count;
    
                while (trav) {
                        STACK_WIND (frame, stripe_stack_unwind_buf_cbk,
                                    trav->xlator, trav->xlator->fops->truncate,
                                    loc, offset);
                        trav = trav->next;
                }
        }

        return 0;
 err:
        STACK_UNWIND (frame, -1, op_errno, NULL);
        return 0;
}


/**
 * stripe_utimens - 
 */
int32_t 
stripe_utimens (call_frame_t *frame, xlator_t *this, loc_t *loc,
                struct timespec tv[2])
{
        int               send_fop_to_all = 0;
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

        if (S_ISDIR (loc->inode->st_mode) || S_ISREG (loc->inode->st_mode))
                send_fop_to_all = 1;

        if (!send_fop_to_all) {
                STACK_WIND (frame, stripe_common_buf_cbk, trav->xlator,
                            trav->xlator->fops->utimens, loc, tv);
        } else {
                /* Initialization */
                local = CALLOC (1, sizeof (stripe_local_t));
                if (!local) {
                        op_errno = ENOMEM;
                        goto err;
                }
                local->op_ret = -1;
                frame->local = local;
                local->inode = loc->inode;
                local->call_count = priv->child_count;
    
                while (trav) {
                        STACK_WIND (frame, stripe_stack_unwind_buf_cbk,
                                    trav->xlator, trav->xlator->fops->utimens,
                                    loc, tv);
                        trav = trav->next;
                }
        }

        return 0;
 err:
        STACK_UNWIND (frame, -1, op_errno, NULL);
        return 0;
}


int32_t 
stripe_first_rename_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                         int32_t op_ret, int32_t op_errno, struct stat *buf)
{
        stripe_local_t *local = NULL;
        xlator_list_t  *trav = NULL;

        if (op_ret == -1) {
                goto unwind;
        }

        local = frame->local;
        trav = this->children;

        local->op_ret = 0;
        local->stbuf = *buf;
        local->call_count--;
        trav = trav->next; /* Skip first child */

        while (trav) {
                STACK_WIND (frame, stripe_stack_unwind_buf_cbk,
                            trav->xlator, trav->xlator->fops->rename,
                            &local->loc, &local->loc2);
                trav = trav->next;
        }
        return 0;

 unwind:
        STACK_UNWIND (frame, op_ret, op_errno, buf);
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
        local = CALLOC (1, sizeof (stripe_local_t));
        if (!local) {
                op_errno = ENOMEM;
                goto err;
        }
        local->op_ret = -1;
        local->inode = oldloc->inode;
        loc_copy (&local->loc, oldloc);
        loc_copy (&local->loc2, newloc);

        local->call_count = priv->child_count;
  
        frame->local = local;

        STACK_WIND (frame, stripe_first_rename_cbk, trav->xlator,
                    trav->xlator->fops->rename, oldloc, newloc);

        return 0;
 err:
        STACK_UNWIND (frame, -1, op_errno, NULL);
        return 0;
}


/**
 * stripe_access - 
 */
int32_t
stripe_access (call_frame_t *frame, xlator_t *this, loc_t *loc, int32_t mask)
{
        int32_t op_errno = 1;

        VALIDATE_OR_GOTO (frame, err);
        VALIDATE_OR_GOTO (this, err);
        VALIDATE_OR_GOTO (loc, err);
        VALIDATE_OR_GOTO (loc->path, err);
        VALIDATE_OR_GOTO (loc->inode, err);

        STACK_WIND (frame, stripe_common_cbk, FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->access, loc, mask);

        return 0;
 err:
        STACK_UNWIND (frame, -1, op_errno);
        return 0;
}


/**
 * stripe_readlink_cbk - 
 */
int32_t 
stripe_readlink_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                     int32_t op_ret, int32_t op_errno, const char *path)
{
        STACK_UNWIND (frame, op_ret, op_errno, path);
        return 0;
}


/**
 * stripe_readlink - 
 */
int32_t
stripe_readlink (call_frame_t *frame, xlator_t *this, loc_t *loc, size_t size)
{
        int32_t op_errno = 1;

        VALIDATE_OR_GOTO (frame, err);
        VALIDATE_OR_GOTO (this, err);
        VALIDATE_OR_GOTO (loc, err);
        VALIDATE_OR_GOTO (loc->path, err);
        VALIDATE_OR_GOTO (loc->inode, err);

        STACK_WIND (frame, stripe_readlink_cbk, FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->readlink, loc, size);

        return 0;
 err:
        STACK_UNWIND (frame, -1, op_errno, NULL);
        return 0;
}


/**
 * stripe_unlink - 
 */
int32_t
stripe_unlink (call_frame_t *frame, xlator_t *this, loc_t *loc)
{
        int               send_fop_to_all = 0;
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
 
        if (S_ISREG (loc->inode->st_mode))
                send_fop_to_all = 1;

        if (!send_fop_to_all) {
                STACK_WIND (frame, stripe_common_cbk, trav->xlator,
                            trav->xlator->fops->unlink, loc);
        } else {
                /* Don't unlink a file if a node is down */
                if (priv->nodes_down) {
                        op_errno = ENOTCONN;
                        goto err;
                }

                /* Initialization */
                local = CALLOC (1, sizeof (stripe_local_t));
                if (!local) {
                        op_errno = ENOMEM;
                        goto err;
                }
                local->op_ret = -1;
                frame->local = local;
                local->call_count = priv->child_count;
    
                while (trav) {
                        STACK_WIND (frame, stripe_stack_unwind_cbk,
                                    trav->xlator, trav->xlator->fops->unlink,
                                    loc);
                        trav = trav->next;
                }
        }

        return 0;
 err:
        STACK_UNWIND (frame, -1, op_errno);
        return 0;
}


int32_t 
stripe_first_rmdir_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                        int32_t op_ret, int32_t op_errno)
{
        xlator_list_t *trav = NULL;
        stripe_local_t *local = NULL;

        if (op_ret == -1) {
                STACK_UNWIND (frame, op_ret, op_errno);
                return 0;
        }

        trav = this->children;
        local = frame->local;

        local->call_count--; /* First child successful */
        trav = trav->next; /* Skip first child */

        while (trav) {
                STACK_WIND (frame, stripe_stack_unwind_cbk, trav->xlator,
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
        local = CALLOC (1, sizeof (stripe_local_t));
        if (!local) {
                op_errno = ENOMEM;
                goto err;
        }
        local->op_ret = -1;
        frame->local = local;
        local->inode = loc->inode;
        loc_copy (&local->loc, loc);
        local->call_count = priv->child_count;
  
        STACK_WIND (frame, stripe_first_rmdir_cbk,  trav->xlator,
                    trav->xlator->fops->rmdir, loc);

        return 0;
 err:
        STACK_UNWIND (frame, -1, op_errno);
        return 0;
}


/**
 * stripe_setxattr - 
 */
int32_t
stripe_setxattr (call_frame_t *frame, xlator_t *this, loc_t *loc,
                 dict_t *dict, int32_t flags)
{
        stripe_private_t *priv = NULL;
        int32_t           op_errno = 1;

        VALIDATE_OR_GOTO (frame, err);
        VALIDATE_OR_GOTO (this, err);
        VALIDATE_OR_GOTO (loc, err);
        VALIDATE_OR_GOTO (loc->path, err);
        VALIDATE_OR_GOTO (loc->inode, err);

        priv = this->private;

        if (priv->first_child_down) {
                op_errno = ENOTCONN;
                goto err;
        }

        STACK_WIND (frame, stripe_common_cbk, FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->setxattr, loc, dict, flags);

        return 0;
 err:
        STACK_UNWIND (frame, -1, op_errno);
        return 0;
}


int32_t 
stripe_mknod_ifreg_fail_unlink_cbk (call_frame_t *frame, void *cookie,
                                    xlator_t *this, int32_t op_ret,
                                    int32_t op_errno)
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
                              local->inode, &local->stbuf);
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

        LOCK (&frame->lock);
        {
                callcnt = --local->call_count;
    
                if (op_ret == -1) {
                        gf_log (this->name, GF_LOG_DEBUG,
                                "%s returned error %s",
                                ((call_frame_t *)cookie)->this->name, 
                                strerror (op_errno));
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
                              local->inode, &local->stbuf);
        }
        return 0;
}

/**
 */
int32_t
stripe_mknod_ifreg_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                        int32_t op_ret, int32_t op_errno, inode_t *inode,
                        struct stat *buf)
{
        int               ret = 0;
        int32_t           callcnt = 0;
        stripe_local_t   *local = NULL;
        xlator_list_t    *trav = NULL;
        stripe_private_t *priv = NULL;

        local = frame->local;

        LOCK (&frame->lock);
        {
                callcnt = --local->call_count;
    
                if (op_ret == -1) {
                        gf_log (this->name, GF_LOG_DEBUG, 
                                "%s returned error %s",
                                ((call_frame_t *)cookie)->this->name, 
                                strerror (op_errno));
                        local->failed = 1;
                        local->op_errno = op_errno;
                }
    
                if (op_ret >= 0) {
                        local->op_ret = op_ret;
                        /* Get the mapping in inode private */
                        /* Get the stat buf right */
                        if (local->stbuf.st_blksize == 0) {
                                local->stbuf = *buf;
                                /* Because st_blocks gets added again */
                                local->stbuf.st_blocks = 0;
                        }

                        /* Always, pass the inode number of first child 
                           to the above layer */
                        if (FIRST_CHILD(this) == 
                            ((call_frame_t *)cookie)->this)
                                local->stbuf.st_ino = buf->st_ino;
      
                        local->stbuf.st_blocks += buf->st_blocks;
                        if (local->stbuf.st_size < buf->st_size)
                                local->stbuf.st_size = buf->st_size;
                        if (local->stbuf.st_blksize != buf->st_blksize) {
                                /* TODO: add to blocks in terms of
                                   original block size */
                        }
                }
        }
        UNLOCK (&frame->lock);

        if (!callcnt) {
                if (local->failed) 
                        local->op_ret = -1;

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
                                      local->inode, &local->stbuf);
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
                local = CALLOC (1, sizeof (stripe_local_t));
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
        STACK_UNWIND (frame, -1, op_errno, NULL, NULL);
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
        local = CALLOC (1, sizeof (stripe_local_t));
        if (!local) {
                op_errno = ENOMEM;
                goto err;
        }
        local->op_ret = -1;
        local->call_count = priv->child_count;
        frame->local = local;

        /* Everytime in stripe lookup, all child nodes should be looked up */
        while (trav) {
                STACK_WIND (frame, stripe_stack_unwind_inode_cbk,
                            trav->xlator, trav->xlator->fops->mkdir,
                            loc, mode);
                trav = trav->next;
        }

        return 0;
 err:
        STACK_UNWIND (frame, -1, op_errno, NULL, NULL);
        return 0;
}


/**
 * stripe_symlink - 
 */
int32_t
stripe_symlink (call_frame_t *frame, xlator_t *this, const char *linkpath,
                loc_t *loc)
{
        int32_t           op_errno = 1;
        stripe_private_t *priv = NULL;
  
        priv = this->private;

        if (priv->first_child_down) {
                op_errno = ENOTCONN;
                goto err;
        }

        /* send symlink to only first node */
        STACK_WIND (frame, stripe_common_inode_cbk, FIRST_CHILD(this), 
                    FIRST_CHILD(this)->fops->symlink, linkpath, loc);

        return 0;
 err:
        STACK_UNWIND (frame, -1, op_errno, NULL, NULL);

        return 0;
}

/**
 * stripe_link -
 */
int32_t
stripe_link (call_frame_t *frame, xlator_t *this, loc_t *oldloc, loc_t *newloc)
{
        int               send_fop_to_all = 0;
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

        if (S_ISREG (oldloc->inode->st_mode))
                send_fop_to_all = 1;

        if (!send_fop_to_all) {
                STACK_WIND (frame, stripe_common_inode_cbk,
                            trav->xlator, trav->xlator->fops->link,
                            oldloc, newloc);
        } else {
                /* Initialization */
                local = CALLOC (1, sizeof (stripe_local_t));
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
                        STACK_WIND (frame, stripe_stack_unwind_inode_cbk,
                                    trav->xlator, trav->xlator->fops->link,
                                    oldloc, newloc);
                        trav = trav->next;
                }
        }

        return 0;
 err:
        STACK_UNWIND (frame, -1, op_errno, NULL, NULL);
        return 0;
}

int32_t 
stripe_create_fail_unlink_cbk (call_frame_t *frame, void *cookie,
                               xlator_t *this, int32_t op_ret,
                               int32_t op_errno)
{
        int32_t         callcnt = 0;
        fd_t           *lfd = NULL;
        stripe_local_t *local = NULL;

        local = frame->local;

        LOCK (&frame->lock);
        {
                callcnt = --local->call_count;
        }
        UNLOCK (&frame->lock);

        if (!callcnt) {
                lfd = local->fd;
                loc_wipe (&local->loc);
                STACK_UNWIND (frame, local->op_ret, local->op_errno, 
                              local->fd, local->inode, &local->stbuf);
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
        fd_t             *lfd = NULL;
        stripe_local_t   *local = NULL;
        stripe_private_t *priv = NULL;
        xlator_list_t    *trav = NULL;
        int32_t           callcnt = 0;

        local = frame->local;

        LOCK (&frame->lock);
        {
                callcnt = --local->call_count;
    
                if (op_ret == -1) {
                        gf_log (this->name, GF_LOG_DEBUG,
                                "%s returned error %s",
                                ((call_frame_t *)cookie)->this->name, 
                                strerror (op_errno));
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
                loc_wipe (&local->loc);
                STACK_UNWIND (frame, local->op_ret, local->op_errno,
                              local->fd, local->inode, &local->stbuf);
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
                   inode_t *inode, struct stat *buf)
{
        int32_t           callcnt = 0;
        stripe_local_t   *local = NULL;
        stripe_private_t *priv = NULL;
        fd_t             *lfd = NULL;
        stripe_fd_ctx_t  *fctx = NULL;

        priv  = this->private;
        local = frame->local;

        LOCK (&frame->lock);
        {
                callcnt = --local->call_count;
    
                if (op_ret == -1) {
                        gf_log (this->name, GF_LOG_DEBUG,
                                "%s returned error %s",
                                ((call_frame_t *)cookie)->this->name, 
                                strerror (op_errno));
                        local->failed = 1;
                        local->op_errno = op_errno;
                }
    
                if (op_ret >= 0) {
                        local->op_ret = op_ret;
                        /* Get the mapping in inode private */
                        /* Get the stat buf right */
                        if (local->stbuf.st_blksize == 0) {
                                local->stbuf = *buf;
                                /* Because st_blocks gets added again */
                                local->stbuf.st_blocks = 0;
                        }
      
                        /* Always, pass the inode number of first
                           child to the above layer */
                        if (FIRST_CHILD(this) == 
                            ((call_frame_t *)cookie)->this)
                                local->stbuf.st_ino = buf->st_ino;
      
                        local->stbuf.st_blocks += buf->st_blocks;
                        if (local->stbuf.st_size < buf->st_size)
                                local->stbuf.st_size = buf->st_size;
                        if (local->stbuf.st_blksize != buf->st_blksize) {
                                /* TODO: add to blocks in terms of 
                                   original block size */
                        }
                }
        }
        UNLOCK (&frame->lock);

        if (!callcnt) {
                if (local->failed)
                        local->op_ret = -1;

                /* */
                if (local->op_ret >= 0) {
                        fctx = CALLOC (1, sizeof (stripe_fd_ctx_t));
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
                        loc_wipe (&local->loc);
                        STACK_UNWIND (frame, local->op_ret, local->op_errno, 
                                      local->fd, local->inode, &local->stbuf);
      
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
        local = CALLOC (1, sizeof (stripe_local_t));
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
        STACK_UNWIND (frame, -1, op_errno, NULL, NULL, NULL);
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

        local = frame->local;

        LOCK (&frame->lock);
        {
                callcnt = --local->call_count;

                if (op_ret == -1) {
                        local->failed = 1;
                        gf_log (this->name, GF_LOG_DEBUG, 
                                "%s returned error %s",
                                ((call_frame_t *)cookie)->this->name, 
                                strerror (op_errno));
                        local->op_ret = -1;
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
                                        FREE (local->fctx->xl_array);
                                FREE (local->fctx);
                        }
                } else {
                        fd_ctx_set (local->fd, this, 
                                    (uint64_t)(long)local->fctx);
                }

                lfd = local->fd;
                loc_wipe (&local->loc);
                STACK_UNWIND (frame, local->op_ret, local->op_errno, 
                              local->fd);
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
                                ((call_frame_t *)cookie)->this->name, 
                                strerror (op_errno));
                        local->op_ret = -1;
                        if (local->op_errno != EIO)
                                local->op_errno = op_errno;
                        if (op_errno == ENOTCONN)
                                local->failed = 1;
                        goto unlock;
                }

                if (!local->fctx) {
                        local->fctx =  CALLOC (1, sizeof (stripe_fd_ctx_t));
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
                                CALLOC (local->fctx->stripe_count, 
                                        sizeof (xlator_t *));
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
                if (local->fctx->xl_array)
                        local->fctx->xl_array[index] = prev->this;
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
                                    local->flags, local->fd);
                        trav = trav->next;
                }
        }

        return 0;
 err:
        lfd = local->fd;
        loc_wipe (&local->loc);
        STACK_UNWIND (frame, local->op_ret, local->op_errno, local->fd);
        fd_unref (lfd);
        
        return 0;
}

/**
 * stripe_open - 
 */
int32_t
stripe_open (call_frame_t *frame, xlator_t *this, loc_t *loc,
             int32_t flags, fd_t *fd)
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
        local = CALLOC (1, sizeof (stripe_local_t));
        if (!local) {
                op_errno = ENOMEM;
                goto err;
        }

        /* files opened in O_APPEND mode does not allow lseek() on fd */
        flags &= ~O_APPEND;

        local->fd = fd_ref (fd);
        frame->local = local;
        local->inode = loc->inode;
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
                local->fctx =  CALLOC (1, sizeof (stripe_fd_ctx_t));
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
                                    &local->loc, local->flags, local->fd);
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

        LOCK (&frame->lock);
        {
                callcnt = --local->call_count;

                if (op_ret == -1) {
                        gf_log (this->name, GF_LOG_DEBUG, 
                                "%s returned error %s",
                                ((call_frame_t *)cookie)->this->name, 
                                strerror (op_errno));
                        local->op_ret = -1;
                        local->failed = 1;
                        local->op_errno = op_errno;
                }
    
                if (op_ret >= 0) 
                        local->op_ret = op_ret;
        }
        UNLOCK (&frame->lock);

        if (!callcnt) {
                STACK_UNWIND (frame, local->op_ret, local->op_errno, 
                              local->fd);
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
        local = CALLOC (1, sizeof (stripe_local_t));
        if (!local) {
                op_errno = ENOMEM;
                goto err;
        }
        frame->local = local;
        local->inode = loc->inode;
        local->fd = fd;
        local->call_count = priv->child_count;

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
 * stripe_getxattr_cbk - 
 */
int32_t
stripe_getxattr_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                     int32_t op_ret, int32_t op_errno, dict_t *value)
{
        STACK_UNWIND (frame, op_ret, op_errno, value);
        return 0;
}


/**
 * stripe_getxattr - 
 */
int32_t
stripe_getxattr (call_frame_t *frame, xlator_t *this, loc_t *loc,
                 const char *name)
{
        int32_t op_errno = 1;

        VALIDATE_OR_GOTO (frame, err);
        VALIDATE_OR_GOTO (this, err);
        VALIDATE_OR_GOTO (loc, err);
        VALIDATE_OR_GOTO (loc->path, err);
        VALIDATE_OR_GOTO (loc->inode, err);

        STACK_WIND (frame, stripe_getxattr_cbk, FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->getxattr, loc, name);

        return 0;
 err:
        STACK_UNWIND (frame, -1, op_errno, NULL);
        return 0;
}

/**
 * stripe_removexattr - 
 */
int32_t
stripe_removexattr (call_frame_t *frame, xlator_t *this, loc_t *loc,
                    const char *name)
{
        int32_t op_errno = 1;

        VALIDATE_OR_GOTO (frame, err);
        VALIDATE_OR_GOTO (this, err);
        VALIDATE_OR_GOTO (loc, err);
        VALIDATE_OR_GOTO (loc->path, err);
        VALIDATE_OR_GOTO (loc->inode, err);

        STACK_WIND (frame, stripe_common_cbk, FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->removexattr, loc, name);

        return 0;
 err:
        STACK_UNWIND (frame, -1, op_errno);
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

        local = frame->local;

        LOCK (&frame->lock);
        {
                callcnt = --local->call_count;
                if (op_ret == -1) {
                        gf_log (this->name, GF_LOG_DEBUG, 
                                "%s returned error %s",
                                ((call_frame_t *)cookie)->this->name, 
                                strerror (op_errno));
                        local->op_errno = op_errno;
                        if (op_errno == ENOTCONN)
                                local->failed = 1;
                }
                if (op_ret == 0 && local->op_ret == -1) {
                        /* First successful call, copy the *lock */
                        local->op_ret = 0;
                        local->lock = *lock;
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
        local = CALLOC (1, sizeof (stripe_local_t));
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
 * stripe_writedir - 
 */
int32_t
stripe_setdents (call_frame_t *frame, xlator_t *this, fd_t *fd,
                 int32_t flags, dir_entry_t *entries, int32_t count)
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
        local = CALLOC (1, sizeof (stripe_local_t));
        if (!local) {
                op_errno = ENOMEM;
                goto err;
        }
        local->op_ret = -1;
        frame->local = local;
        local->call_count = priv->child_count;

        while (trav) {
                STACK_WIND (frame, stripe_stack_unwind_cbk, trav->xlator,
                            trav->xlator->fops->setdents, fd, flags, entries, 
                            count);
                trav = trav->next;
        }

        return 0;
 err:
        STACK_UNWIND (frame, -1, op_errno);
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
        local = CALLOC (1, sizeof (stripe_local_t));
        if (!local) {
                op_errno = ENOMEM;
                goto err;
        }
        local->op_ret = -1;
        frame->local = local;
        local->call_count = priv->child_count;
        
        while (trav) {
                STACK_WIND (frame, stripe_stack_unwind_cbk,  trav->xlator,
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
        local = CALLOC (1, sizeof (stripe_local_t));
        if (!local) {
                op_errno = ENOMEM;
                goto err;
        }
        local->op_ret = -1;
        frame->local = local;
        local->call_count = priv->child_count;
        
        while (trav) {
                STACK_WIND (frame, stripe_stack_unwind_cbk, trav->xlator,
                            trav->xlator->fops->fsync, fd, flags);
                trav = trav->next;
        }

        return 0;
 err:
        STACK_UNWIND (frame, -1, op_errno);
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
        local = CALLOC (1, sizeof (stripe_local_t));
        if (!local) {
                op_errno = ENOMEM;
                goto err;
        }
        local->op_ret = -1;
        frame->local = local;
        local->inode = fd->inode;
        local->call_count = priv->child_count;
        
        while (trav) {
                STACK_WIND (frame, stripe_stack_unwind_buf_cbk, trav->xlator,
                            trav->xlator->fops->fstat, fd);
                trav = trav->next;
        }

        return 0;
 err:
        STACK_UNWIND (frame, -1, op_errno, NULL);
        return 0;
}


/**
 * stripe_fchmod - 
 */
int32_t 
stripe_fchmod (call_frame_t *frame, xlator_t *this, fd_t *fd, mode_t mode)
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
        local = CALLOC (1, sizeof (stripe_local_t));
        if (!local) {
                op_errno = ENOMEM;
                goto err;
        }
        local->op_ret = -1;
        frame->local = local;
        local->inode = fd->inode;
        local->call_count = priv->child_count;
        
        while (trav) {
                STACK_WIND (frame, stripe_stack_unwind_buf_cbk, trav->xlator,
                            trav->xlator->fops->fchmod, fd, mode);
                trav = trav->next;
        }

        return 0;
 err:
        STACK_UNWIND (frame, -1, op_errno, NULL);
        return 0;
}


/**
 * stripe_fchown - 
 */
int32_t 
stripe_fchown (call_frame_t *frame, xlator_t *this, fd_t *fd, uid_t uid,
               gid_t gid)
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
        local = CALLOC (1, sizeof (stripe_local_t));
        if (!local) {
                op_errno = ENOMEM;
                goto err;
        }
        local->op_ret = -1;
        frame->local = local;
        local->inode = fd->inode;
        local->call_count = priv->child_count;
        
        while (trav) {
                STACK_WIND (frame, stripe_stack_unwind_buf_cbk, trav->xlator,
                            trav->xlator->fops->fchown, fd, uid, gid);
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
        local = CALLOC (1, sizeof (stripe_local_t));
        if (!local) {
                op_errno = ENOMEM;
                goto err;
        }
        local->op_ret = -1;
        frame->local = local;
        local->inode = fd->inode;
        local->call_count = priv->child_count;
        
        while (trav) {
                STACK_WIND (frame, stripe_stack_unwind_buf_cbk, trav->xlator,
                            trav->xlator->fops->ftruncate, fd, offset);
                trav = trav->next;
        }

        return 0;
 err:
        STACK_UNWIND (frame, -1, op_errno, NULL);
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
        local = CALLOC (1, sizeof (stripe_local_t));
        if (!local) {
                op_errno = ENOMEM;
                goto err;
        }
        local->op_ret = -1;
        frame->local = local;
        local->call_count = priv->child_count;

        while (trav) {
                STACK_WIND (frame, stripe_stack_unwind_cbk, trav->xlator,
                            trav->xlator->fops->fsyncdir, fd, flags);
                trav = trav->next;
        }

        return 0;
 err:
        STACK_UNWIND (frame, -1, op_errno, NULL);
        return 0;
}


/**
 * stripe_single_readv_cbk - This function is used as return fn, when the 
 *     file name doesn't match the pattern specified for striping.
 */
int32_t
stripe_single_readv_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                         int32_t op_ret, int32_t op_errno, 
                         struct iovec *vector, int32_t count,
                         struct stat *stbuf, struct iobref *iobref)
{
        STACK_UNWIND (frame, op_ret, op_errno, vector, count, stbuf, iobref);
        return 0;
}

/**
 * stripe_readv_cbk - get all the striped reads, and order it properly, send it
 *        to above layer after putting it in a single vector.
 */
int32_t
stripe_readv_cbk (call_frame_t *frame, void *cookie, xlator_t *this, 
                  int32_t op_ret, int32_t op_errno, struct iovec *vector,
                  int32_t count, struct stat *stbuf, struct iobref *iobref)
{
        int32_t         index = 0;
        int32_t         callcnt = 0;
        call_frame_t   *main_frame = NULL;
        stripe_local_t *main_local = NULL;
        stripe_local_t *local = frame->local;

        index = local->node_index;
        main_frame = local->orig_frame;
        main_local = main_frame->local;

        LOCK (&main_frame->lock);
        {
                main_local->replies[index].op_ret = op_ret;
                main_local->replies[index].op_errno = op_errno;
                if (op_ret >= 0) {
                        main_local->replies[index].stbuf  = *stbuf;
                        main_local->replies[index].count  = count;
                        main_local->replies[index].vector = 
                                iov_dup (vector, count);

                        if (!main_local->iobref)
                                main_local->iobref = iobref_new ();
                        iobref_merge (main_local->iobref, iobref);
                }
                callcnt = ++main_local->call_count;
        }
        UNLOCK(&main_frame->lock);

        if (callcnt == main_local->wind_count) {
                int32_t        final_count = 0;
                struct iovec  *final_vec = NULL;
                struct stat    tmp_stbuf = {0,};
                struct iobref *iobref = NULL;

                op_ret = 0;
                memcpy (&tmp_stbuf, &main_local->replies[0].stbuf, 
                        sizeof (struct stat));
                for (index=0; index < main_local->wind_count; index++) {
                        /* TODO: check whether each stripe returned 'expected'
                         * number of bytes 
                         */
                        if (main_local->replies[index].op_ret == -1) {
                                op_ret = -1;
                                op_errno = main_local->replies[index].op_errno;
                                break;
                        }
                        op_ret += main_local->replies[index].op_ret;
                        final_count += main_local->replies[index].count;
                        /* TODO: Do I need to send anything more in stbuf? */
                        if (tmp_stbuf.st_size < 
                            main_local->replies[index].stbuf.st_size) {
                                tmp_stbuf.st_size = 
                                        main_local->replies[index].stbuf.st_size;
                        }
                }
                if (op_ret != -1) {
                        final_vec = CALLOC (final_count, 
                                            sizeof (struct iovec));
                        if (!final_vec) {
                                op_ret = -1;
                                final_count = 0;
                                goto done;
                        }

                        final_count = 0;

                        for (index=0; 
                             index < main_local->wind_count; index++) {
                                memcpy (final_vec + final_count,
                                        main_local->replies[index].vector,
                                        (main_local->replies[index].count * 
                                         sizeof (struct iovec)));
                                final_count += 
                                        main_local->replies[index].count;

                                free (main_local->replies[index].vector);
                        }
                } else {
                        final_vec = NULL;
                        final_count = 0;
                }

        done:
                /* */
                FREE (main_local->replies);
                iobref = main_local->iobref;
                STACK_UNWIND (main_frame, op_ret, op_errno, 
                              final_vec, final_count, &tmp_stbuf, iobref);

                iobref_unref (iobref);
                if (final_vec)
                        FREE (final_vec);
        }

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
        num_stripe = (rounded_end - rounded_start) / stripe_size;
        
        local = CALLOC (1, sizeof (stripe_local_t));
        if (!local) {
                op_errno = ENOMEM;
                goto err;
        }
        local->wind_count = num_stripe;
        frame->local = local;
        
        /* This is where all the vectors should be copied. */
        local->replies = CALLOC (num_stripe, sizeof (struct readv_replies));
        if (!local->replies) {
                op_errno = ENOMEM;
                goto err;
        }
        
        off_index = (offset / stripe_size) % fctx->stripe_count;
    
        for (index = off_index; index < (num_stripe + off_index); index++) {
                rframe = copy_frame (frame);
                rlocal = CALLOC (1, sizeof (stripe_local_t));
                if (!rlocal) {
                        op_errno = ENOMEM;
                        goto err;
                }
                
                frame_size = min (roof (frame_offset+1, stripe_size),
                                  (offset + size)) - frame_offset;
                
                rlocal->node_index = index - off_index;
                rlocal->orig_frame = frame;
                rframe->local = rlocal;
                idx = (index % fctx->stripe_count);
                STACK_WIND (rframe, stripe_readv_cbk, fctx->xl_array[idx],
                            fctx->xl_array[idx]->fops->readv,
                            fd, frame_size, frame_offset);
      
                frame_offset += frame_size;
        }

        return 0;
 err:
        STACK_UNWIND (frame, -1, op_errno, NULL);
        return 0;
}


/**
 * stripe_writev_cbk - 
 */
int32_t
stripe_writev_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                   int32_t op_ret, int32_t op_errno, struct stat *stbuf)
{
        int32_t         callcnt = 0;
        stripe_local_t *local = NULL;

        local = frame->local;

        LOCK(&frame->lock);
        {
                callcnt = ++local->call_count;
    
                if (op_ret == -1) {
                        gf_log (this->name, GF_LOG_DEBUG, 
                                "%s returned error %s",
                                ((call_frame_t *)cookie)->this->name, 
                                strerror (op_errno));
                        local->op_errno = op_errno;
                        local->op_ret = -1;
                }
                if (op_ret >= 0) {
                        local->op_ret += op_ret;
                        local->stbuf = *stbuf;
                }
        }
        UNLOCK (&frame->lock);

        if ((callcnt == local->wind_count) && local->unwind) {
                STACK_UNWIND (frame, local->op_ret, 
                              local->op_errno, &local->stbuf);
        }
        return 0;
}


/**
 * stripe_single_writev_cbk - 
 */
int32_t
stripe_single_writev_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                          int32_t op_ret, int32_t op_errno, struct stat *stbuf)
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

        local = CALLOC (1, sizeof (stripe_local_t));
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
                tmp_vec = CALLOC (tmp_count, sizeof (struct iovec));
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
                FREE (tmp_vec);
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

/**
 * stripe_stats_cbk - Add all the fields received from different clients. 
 *    Once all the clients return, send stats to above layer.
 * 
 */
int32_t
stripe_stats_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                  int32_t op_ret, int32_t op_errno, struct xlator_stats *stats)
{
        int32_t         callcnt = 0;
        stripe_local_t *local = NULL;

        local = frame->local;

        LOCK(&frame->lock);
        {
                callcnt = --local->call_count;
    
                if (op_ret == -1) {
                        gf_log (this->name, GF_LOG_DEBUG,
                                "%s returned error %s",
                                ((call_frame_t *)cookie)->this->name, 
                                strerror (op_errno));
                        local->op_ret = -1;
                        local->op_errno = op_errno;
                }
                if (op_ret == 0) {
                        if (local->op_ret == -2) {
                                /* This is to make sure this is the 
                                   first time */
                                local->stats = *stats;
                                local->op_ret = 0;
                        } else {
                                local->stats.nr_files += stats->nr_files;
                                local->stats.free_disk += stats->free_disk;
                                local->stats.disk_usage += stats->disk_usage;
                                local->stats.nr_clients += stats->nr_clients;
                        }
                }
        }
        UNLOCK (&frame->lock);

        if (!callcnt) {
                STACK_UNWIND (frame, local->op_ret, local->op_errno,
                              &local->stats);
        }

        return 0;
}

/**
 * stripe_stats - 
 */
int32_t
stripe_stats (call_frame_t *frame, xlator_t *this, int32_t flags)
{
        stripe_local_t   *local = NULL;
        xlator_list_t    *trav = NULL;
        stripe_private_t *priv = NULL;
        int32_t           op_errno = 1;

        priv = this->private;
        trav = this->children;

        local = CALLOC (1, sizeof (stripe_local_t));
        if (!local) {
                op_errno = ENOMEM;
                goto err;
        }
        frame->local = local;
        local->op_ret = -2; /* to be used as a flag in _cbk */
        local->call_count = priv->child_count;

        while (trav) {
                STACK_WIND (frame, stripe_stats_cbk, trav->xlator,
                            trav->xlator->mops->stats, flags);
                trav = trav->next;
        }
        return 0;
 err:
        STACK_UNWIND (frame, -1, op_errno, NULL);
        return 0;
}

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
                FREE (fctx->xl_array);
        
        FREE (fctx);
                
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

                        if (data == FIRST_CHILD (this)) {
                                priv->first_child_down = 0;
                                default_notify (this, event, data);
                        }
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

                        if (data == FIRST_CHILD (this)) {
                                priv->first_child_down = 1;
                                default_notify (this, event, data);
                        }
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
                dup_str = strdup (stripe_str);
                stripe_opt = CALLOC (1, sizeof (struct stripe_options));
                if (!stripe_opt)
                        goto out;

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
        }

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
  
        priv = CALLOC (1, sizeof (stripe_private_t));
        if (!priv)
                goto out;
        priv->xl_array = CALLOC (count, sizeof (xlator_t *));
        if (!priv->xl_array)
                goto out;

        priv->state = CALLOC (count, sizeof (int8_t));
        if (!priv->xl_array)
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
                                FREE (priv->xl_array);
                        FREE (priv);
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
                        FREE (priv->xl_array);

                trav = priv->pattern;
                while (trav) {
                        prev = trav;
                        trav = trav->next;
                        FREE (prev);
                }
                LOCK_DESTROY (&priv->lock);
                FREE (priv);
        }

        return;
}


struct xlator_fops fops = {
        .stat        = stripe_stat,
        .unlink      = stripe_unlink,
        .symlink     = stripe_symlink,
        .rename      = stripe_rename,
        .link        = stripe_link,
        .chmod       = stripe_chmod,
        .chown       = stripe_chown,
        .truncate    = stripe_truncate,
        .utimens     = stripe_utimens,
        .create      = stripe_create,
        .open        = stripe_open,
        .readv       = stripe_readv,
        .writev      = stripe_writev,
        .statfs      = stripe_statfs,
        .flush       = stripe_flush,
        .fsync       = stripe_fsync,
        .setxattr    = stripe_setxattr,
        .getxattr    = stripe_getxattr,
        .removexattr = stripe_removexattr,
        .access      = stripe_access,
        .ftruncate   = stripe_ftruncate,
        .fstat       = stripe_fstat,
        .readlink    = stripe_readlink,
        .mkdir       = stripe_mkdir,
        .rmdir       = stripe_rmdir,
        .lk          = stripe_lk,
        .opendir     = stripe_opendir,
        .fsyncdir    = stripe_fsyncdir,
        .fchmod      = stripe_fchmod,
        .fchown      = stripe_fchown,
        .lookup      = stripe_lookup,
        .setdents    = stripe_setdents,
        .mknod       = stripe_mknod,
};

struct xlator_mops mops = {
        .stats  = stripe_stats,
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
