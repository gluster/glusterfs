/*
   Copyright (c) 2006-2012 Red Hat, Inc. <http://www.redhat.com>
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

#include "trash.h"
#include "trash-mem-types.h"

int32_t
trash_ftruncate_readv_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                           int32_t op_ret, int32_t op_errno,
                           struct iovec *vector, int32_t count,
                           struct iatt *stbuf, struct iobref *iobuf);

int32_t
trash_truncate_writev_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                           int32_t op_ret, int32_t op_errno,
                           struct iatt *prebuf, struct iatt *postbuf);

int32_t
trash_truncate_mkdir_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                          int32_t op_ret, int32_t op_errno, inode_t *inode,
                          struct iatt *stbuf, struct iatt *preparent,
                          struct iatt *postparent);

int32_t
trash_unlink_rename_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                         int32_t op_ret, int32_t op_errno, struct iatt *buf,
                         struct iatt *preoldparent, struct iatt *postoldparent,
                         struct iatt *prenewparent, struct iatt *postnewparent);

void
trash_local_wipe (trash_local_t *local)
{
        if (!local)
                goto out;

        loc_wipe (&local->loc);
        loc_wipe (&local->newloc);

        if (local->fd)
                fd_unref (local->fd);

        if (local->newfd)
                fd_unref (local->newfd);

        mem_put (local);
out:
        return;
}

int32_t
trash_common_unwind_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                         int32_t op_ret, int32_t op_errno,
                         struct iatt *preparent, struct iatt *postparent)
{
        TRASH_STACK_UNWIND (unlink, frame, op_ret, op_errno, preparent, postparent);
        return 0;
}

int32_t
trash_unlink_mkdir_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                        int32_t op_ret, int32_t op_errno, inode_t *inode,
                        struct iatt *stbuf, struct iatt *preparent,
                        struct iatt *postparent)
{
        trash_local_t *local       = NULL;
        char          *tmp_str     = NULL;
        char          *tmp_path    = NULL;
        char          *tmp_dirname = NULL;
        char          *dir_name    = NULL;
        int32_t        count       = 0;
        int32_t        loop_count  = 0;
        int            i           = 0;
        loc_t          tmp_loc     = {0,};

        local   = frame->local;
        tmp_str = gf_strdup (local->newpath);
        if (!tmp_str) {
                gf_log (this->name, GF_LOG_ERROR, "out of memory");
                goto out;
        }
        loop_count = local->loop_count;

        if ((op_ret == -1) &&  (op_errno == ENOENT)) {
                tmp_dirname = strchr (tmp_str, '/');
                while (tmp_dirname) {
                        count = tmp_dirname - tmp_str;
                        if (count == 0)
                                count = 1;
                        i++;
                        if (i > loop_count)
                                break;
                        tmp_dirname = strchr (tmp_str + count + 1, '/');
                }
                tmp_path = memdup (local->newpath, count);
                if (!tmp_path) {
                        gf_log (this->name, GF_LOG_ERROR, "out of memory");
                        goto out;
                }

                tmp_loc.path = tmp_path;

                /* TODO:create the directory with proper permissions */
                STACK_WIND_COOKIE (frame, trash_unlink_mkdir_cbk, tmp_path,
                                   this->children->xlator,
                                   this->children->xlator->fops->mkdir,
                                   &tmp_loc, 0755, NULL);

                goto out;
        }

        if (op_ret == 0) {
                dir_name = dirname (tmp_str);
                if (strcmp((char*)cookie, dir_name) == 0) {
                        tmp_loc.path = local->newpath;
                        STACK_WIND (frame, trash_unlink_rename_cbk,
                                    this->children->xlator,
                                    this->children->xlator->fops->rename,
                                    &local->loc, &tmp_loc);
                        goto out;
                }
        }

        LOCK (&frame->lock);
        {
                loop_count = ++local->loop_count;
        }
        UNLOCK (&frame->lock);
        tmp_dirname = strchr (tmp_str, '/');
        while (tmp_dirname) {
                count = tmp_dirname - tmp_str;
                if (count == 0)
                        count = 1;
                i++;
                if ((i > loop_count) || (count > PATH_MAX))
                        break;
                tmp_dirname = strchr (tmp_str + count + 1, '/');
        }
        tmp_path = memdup (local->newpath, count);
        if (!tmp_path) {
                gf_log (this->name, GF_LOG_ERROR, "out of memory");
                goto out;
        }
        tmp_loc.path = tmp_path;

        STACK_WIND_COOKIE (frame, trash_unlink_mkdir_cbk, tmp_path,
                           this->children->xlator,
                           this->children->xlator->fops->mkdir,
                           &tmp_loc, 0755, NULL);

out:
        GF_FREE (cookie);
        GF_FREE (tmp_str);

        return 0;
}

int32_t
trash_rename_mkdir_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                        int32_t op_ret, int32_t op_errno, inode_t *inode,
                        struct iatt *stbuf, struct iatt *preparent,
                        struct iatt *postparent);

int32_t
trash_unlink_rename_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                         int32_t op_ret, int32_t op_errno, struct iatt *buf,
                         struct iatt *preoldparent, struct iatt *postoldparent,
                         struct iatt *prenewparent, struct iatt *postnewparent)
{
        trash_local_t   *local      = NULL;
        char            *tmp_str    = NULL;
        char            *dir_name   = NULL;
        char            *tmp_cookie = NULL;
        loc_t            tmp_loc    = {0,};

        local = frame->local;

        if ((op_ret == -1) && (op_errno == ENOENT)) {
                tmp_str = gf_strdup (local->newpath);
                if (!tmp_str) {
                        gf_log (this->name, GF_LOG_DEBUG, "out of memory");
                }
                dir_name = dirname (tmp_str);

                tmp_loc.path = dir_name;

                tmp_cookie = gf_strdup (dir_name);
                if (!tmp_cookie) {
                        gf_log (this->name, GF_LOG_DEBUG, "out of memory");
                }
                /* TODO: create the directory with proper permissions */
                STACK_WIND_COOKIE (frame, trash_unlink_mkdir_cbk, tmp_cookie,
                                   FIRST_CHILD(this),
                                   FIRST_CHILD(this)->fops->mkdir,
                                   &tmp_loc, 0755, NULL);

                GF_FREE (tmp_str);

                return 0;
        }

        if ((op_ret == -1) && (op_errno == ENOTDIR)) {

                gf_log (this->name, GF_LOG_DEBUG,
                        "target(%s) exists, cannot keep the copy, deleting",
                        local->newpath);

                STACK_WIND (frame, trash_common_unwind_cbk,
                            this->children->xlator,
                            this->children->xlator->fops->unlink, &local->loc);

                return 0;
        }

        if ((op_ret == -1) && (op_errno == EISDIR)) {
                gf_log (this->name, GF_LOG_DEBUG,
                        "target(%s) exists as directory, cannot keep copy, "
                        "deleting", local->newpath);

                STACK_WIND (frame, trash_common_unwind_cbk,
                            FIRST_CHILD(this),
                            FIRST_CHILD(this)->fops->unlink, &local->loc);
                return 0;
        }

        /* All other cases, unlink should return success */
        TRASH_STACK_UNWIND (unlink, frame, 0, op_errno, &local->preparent,
                            &local->postparent);

        return 0;
}



int32_t
trash_common_unwind_buf_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                             int32_t op_ret, int32_t op_errno,
                             struct iatt *prebuf, struct iatt *postbuf)
{
        TRASH_STACK_UNWIND (truncate, frame, op_ret, op_errno, prebuf, postbuf);
        return 0;
}

int
trash_common_rename_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                         int32_t op_ret, int32_t op_errno, struct iatt *stbuf,
                         struct iatt *preoldparent, struct iatt *postoldparent,
                         struct iatt *prenewparent, struct iatt *postnewparent)
{
        TRASH_STACK_UNWIND (rename, frame, op_ret, op_errno, stbuf, preoldparent,
                            postoldparent, prenewparent, postnewparent);
        return 0;
}


int32_t
trash_unlink_stat_cbk (call_frame_t *frame,  void *cookie, xlator_t *this,
                       int32_t op_ret, int32_t op_errno, struct iatt *buf)
{
        trash_private_t *priv    = NULL;
        trash_local_t   *local   = NULL;
        loc_t            new_loc = {0,};

        priv  = this->private;
        local = frame->local;

        if (-1 == op_ret) {
                gf_log (this->name, GF_LOG_DEBUG, "%s: %s",
                        local->loc.path, strerror (op_errno));
                goto fail;
        }

        if ((buf->ia_size == 0) ||
            (buf->ia_size > priv->max_trash_file_size)) {
                /* if the file is too big or zero, just unlink it */

                if (buf->ia_size > priv->max_trash_file_size) {
                        gf_log (this->name, GF_LOG_DEBUG,
                                "%s: file size too big (%"PRId64") to "
                                "move into trash directory",
                                local->loc.path, buf->ia_size);
                }

                STACK_WIND (frame, trash_common_unwind_cbk,
                            this->children->xlator,
                            this->children->xlator->fops->unlink, &local->loc);
                return 0;
        }

        new_loc.path = local->newpath;

        STACK_WIND (frame, trash_unlink_rename_cbk,
                    this->children->xlator,
                    this->children->xlator->fops->rename,
                    &local->loc, &new_loc);

        return 0;

fail:
        TRASH_STACK_UNWIND (unlink, frame, op_ret, op_errno, buf,
                            NULL);

        return 0;

}

int32_t
trash_rename_rename_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                         int32_t op_ret, int32_t op_errno, struct iatt *buf,
                         struct iatt *preoldparent, struct iatt *postoldparent,
                         struct iatt *prenewparent, struct iatt *postnewparent)
{
        trash_local_t *local    = NULL;
        char          *tmp_str  = NULL;
        char          *dir_name = NULL;
        char          *tmp_path = NULL;
        loc_t          tmp_loc  = {0,};

        local = frame->local;
        if ((op_ret == -1) && (op_errno == ENOENT)) {
                tmp_str  = gf_strdup (local->newpath);
                if (!tmp_str) {
                        gf_log (this->name, GF_LOG_DEBUG, "out of memory");
                }
                dir_name = dirname (tmp_str);

                /* check for the errno, if its ENOENT create directory and call
                 * rename later
                 */
                tmp_path = gf_strdup (dir_name);
                if (!tmp_path) {
                        gf_log (this->name, GF_LOG_DEBUG, "out of memory");
                }
                tmp_loc.path = tmp_path;

                /* TODO: create the directory with proper permissions */
                STACK_WIND_COOKIE (frame, trash_rename_mkdir_cbk, tmp_path,
                                   this->children->xlator,
                                   this->children->xlator->fops->mkdir,
                                   &tmp_loc, 0755, NULL);

                GF_FREE (tmp_str);
                return 0;
        }

        if ((op_ret == -1) && (op_errno == ENOTDIR)) {
                gf_log (this->name, GF_LOG_DEBUG,
                        "target(%s) exists, cannot keep the dest entry(%s): "
                        "renaming", local->newpath, local->origpath);
        } else if ((op_ret == -1) && (op_errno == EISDIR)) {
                gf_log (this->name, GF_LOG_DEBUG,
                        "target(%s) exists as a directory, cannot keep the "
                        "copy (%s), renaming", local->newpath, local->origpath);
        }

        STACK_WIND (frame, trash_common_rename_cbk,
                    this->children->xlator,
                    this->children->xlator->fops->rename, &local->loc,
                    &local->newloc);

        return 0;
}


int32_t
trash_rename_mkdir_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                        int32_t op_ret, int32_t op_errno, inode_t *inode,
                        struct iatt *stbuf, struct iatt *preparent,
                        struct iatt *postparent)
{
        trash_local_t *local = NULL;
        char          *tmp_str = NULL;
        char          *tmp_path = NULL;
        char          *tmp_dirname = NULL;
        char          *dir_name = NULL;
        int32_t        count = 0;
        loc_t          tmp_loc = {0,};

        local   = frame->local;
        tmp_str = gf_strdup (local->newpath);
        if (!tmp_str) {
                gf_log (this->name, GF_LOG_DEBUG, "out of memory");
                goto out;
        }

        if ((op_ret == -1) && (op_errno == ENOENT)) {
                tmp_dirname = strchr (tmp_str, '/');
                while (tmp_dirname) {
                        count = tmp_dirname - tmp_str;
                        if (count == 0)
                                count = 1;

                        tmp_dirname = strchr (tmp_str + count + 1, '/');

                        tmp_path = memdup (local->newpath, count);
                        if (!tmp_path) {
                                gf_log (this->name, GF_LOG_DEBUG, "out of memory");
                        }

                        tmp_loc.path = tmp_path;

                        /* TODO: create the directory with proper permissions */
                        STACK_WIND_COOKIE (frame, trash_rename_mkdir_cbk,
                                           tmp_path,  this->children->xlator,
                                           this->children->xlator->fops->mkdir,
                                           &tmp_loc, 0755, NULL);
                }

                goto out;
        }

        dir_name = dirname (tmp_str);
        if (strcmp ((char*)cookie, dir_name) == 0) {
                tmp_loc.path = local->newpath;

                STACK_WIND (frame, trash_rename_rename_cbk,
                            this->children->xlator,
                            this->children->xlator->fops->rename,
                            &local->newloc, &tmp_loc);
        }

out:
        GF_FREE (cookie); /* strdup (dir_name) was sent here :) */
        GF_FREE (tmp_str);

        return 0;
}

int32_t
trash_rename_lookup_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                         int32_t op_ret, int32_t op_errno, inode_t *inode,
                         struct iatt *buf, dict_t *xattr,
                         struct iatt *postparent)
{
        trash_private_t *priv = NULL;
        trash_local_t   *local = NULL;
        loc_t            tmp_loc = {0,};

        local = frame->local;
        priv  = this->private;

        if (op_ret == -1) {
                STACK_WIND (frame, trash_common_rename_cbk,
                            this->children->xlator,
                            this->children->xlator->fops->rename,
                            &local->loc, &local->newloc);
                return 0;
        }
        if ((buf->ia_size == 0) ||
            (buf->ia_size > priv->max_trash_file_size)) {
                /* if the file is too big or zero, just unlink it */

                if (buf->ia_size > priv->max_trash_file_size) {
                        gf_log (this->name, GF_LOG_DEBUG,
                                "%s: file size too big (%"PRId64") to "
                                "move into trash directory",
                                local->newloc.path, buf->ia_size);
                }

                STACK_WIND (frame, trash_common_rename_cbk,
                            this->children->xlator,
                            this->children->xlator->fops->rename,
                            &local->loc, &local->newloc);
                return 0;
        }

        tmp_loc.path = local->newpath;

        STACK_WIND (frame, trash_rename_rename_cbk,
                    this->children->xlator,
                    this->children->xlator->fops->rename,
                    &local->newloc, &tmp_loc);

        return 0;
}


int32_t
trash_rename (call_frame_t *frame, xlator_t *this, loc_t *oldloc,
              loc_t *newloc)
{
        trash_elim_pattern_t  *trav  = NULL;
        trash_private_t *priv = NULL;
        trash_local_t   *local = NULL;
        char             timestr[64] = {0,};
        int32_t          match = 0;

        priv = this->private;
        if (priv->eliminate) {
                trav = priv->eliminate;
                while (trav) {
                        if (fnmatch(trav->pattern, newloc->name, 0) == 0) {
                                match++;
                                break;
                        }
                        trav = trav->next;
                }
        }

        if ((strncmp (oldloc->path, priv->trash_dir,
                      strlen (priv->trash_dir)) == 0) || match) {
                /* Trying to rename from the trash dir,
                   do the actual rename */
                STACK_WIND (frame, trash_common_rename_cbk,
                            this->children->xlator,
                            this->children->xlator->fops->rename,
                            oldloc, newloc);

                return 0;
        }

        local = mem_get0 (this->local_pool);
        if (!local) {
                gf_log (this->name, GF_LOG_ERROR, "out of memory");
                TRASH_STACK_UNWIND (rename, frame, -1, ENOMEM,
                                    NULL, NULL, NULL, NULL, NULL);
                return 0;
        }

        frame->local = local;
        loc_copy (&local->loc, oldloc);

        loc_copy (&local->newloc, newloc);

        strcpy (local->origpath, newloc->path);
        strcpy (local->newpath, priv->trash_dir);
        strcat (local->newpath, newloc->path);

        {
                /* append timestamp to file name */
                /* TODO: can we make it optional? */
                gf_time_ftm (timestr, sizeof timestr, time (NULL),
                             gf_timefmt_F_HMS);
                strcat (local->newpath, timestr);
        }

        /* Send a lookup call on newloc, to ensure we are not
           overwriting */
        STACK_WIND (frame, trash_rename_lookup_cbk,
                    this->children->xlator,
                    this->children->xlator->fops->lookup, newloc, 0);

        return 0;
}

int32_t
trash_unlink (call_frame_t *frame, xlator_t *this, loc_t *loc)
{
        trash_elim_pattern_t  *trav  = NULL;
        trash_private_t *priv = NULL;
        trash_local_t   *local = NULL;
        char             timestr[64] = {0,};
        int32_t          match = 0;

        priv = this->private;

        if (priv->eliminate) {
                trav = priv->eliminate;
                while (trav) {
                        if (fnmatch(trav->pattern, loc->name, 0) == 0) {
                                match++;
                                break;
                        }
                        trav = trav->next;
                }
        }

        if ((strncmp (loc->path, priv->trash_dir,
                      strlen (priv->trash_dir)) == 0) || (match)) {
                if (match) {
                        gf_log (this->name, GF_LOG_DEBUG,
                                "%s: file matches eliminate pattern, "
                                "not moved to trash", loc->name);
                } else {
                        /* unlink from the trash-dir, not keeping any copy */
                        ;
                }

                STACK_WIND (frame, trash_common_unwind_cbk,
                            this->children->xlator,
                            this->children->xlator->fops->unlink, loc);
                return 0;
        }

        local = mem_get0 (this->local_pool);
        if (!local) {
                gf_log (this->name, GF_LOG_DEBUG, "out of memory");
                TRASH_STACK_UNWIND (unlink, frame, -1, ENOMEM, NULL, NULL);
                return 0;
        }
        frame->local = local;
        loc_copy (&local->loc, loc);

        strcpy (local->origpath, loc->path);
        strcpy (local->newpath, priv->trash_dir);
        strcat (local->newpath, loc->path);

        {
                /* append timestamp to file name */
                /* TODO: can we make it optional? */
                gf_time_fmt (timestr, sizeof timestr, time (NULL),
                             gf_timefmt_F_HMS);
                strcat (local->newpath, timestr);
        }

        LOCK_INIT (&frame->lock);

        STACK_WIND (frame, trash_unlink_stat_cbk,
                    this->children->xlator,
                    this->children->xlator->fops->stat, loc);

        return 0;
}

int32_t
trash_truncate_unlink_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                           int32_t op_ret, int32_t op_errno,
                           struct iatt *preparent, struct iatt *postparent)
{
        /* use this Function when a failure occurs, and
           delete the newly created file. */
        trash_local_t *local = NULL;

        local = frame->local;

        if (op_ret == -1) {
                gf_log (this->name, GF_LOG_DEBUG,
                        "deleting the newly created file: %s",
                        strerror (op_errno));
        }

        STACK_WIND (frame, trash_common_unwind_buf_cbk,
                    FIRST_CHILD(this), FIRST_CHILD(this)->fops->truncate,
                    &local->loc, local->fop_offset);

        return 0;
}

int32_t
trash_truncate_readv_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                          int32_t op_ret, int32_t op_errno,
                          struct iovec *vector, int32_t count,
                          struct iatt *stbuf, struct iobref *iobuf)
{
        trash_local_t *local = NULL;

        local = frame->local;

        if (op_ret == -1) {
                gf_log (this->name, GF_LOG_DEBUG,
                        "readv on the existing file failed: %s",
                        strerror (op_errno));

                STACK_WIND (frame, trash_truncate_unlink_cbk,
                            FIRST_CHILD(this), FIRST_CHILD(this)->fops->unlink,
                            &local->newloc);
                goto out;
        }

        local->fsize = stbuf->ia_size;
        STACK_WIND (frame, trash_truncate_writev_cbk, FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->writev,
                    local->newfd, vector, count, local->cur_offset, 0, iobuf);

out:
        return 0;

}

int32_t
trash_truncate_writev_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                           int32_t op_ret, int32_t op_errno,
                           struct iatt *prebuf, struct iatt *postbuf)
{
        trash_local_t *local = NULL;

        local = frame->local;

        if (op_ret == -1) {
                /* Let truncate work, but previous copy is not preserved. */
                gf_log (this->name, GF_LOG_DEBUG,
                        "writev on the existing file failed: %s",
                        strerror (op_errno));

                STACK_WIND (frame, trash_truncate_unlink_cbk, FIRST_CHILD(this),
                            FIRST_CHILD(this)->fops->unlink, &local->newloc);
                goto out;
        }

        if (local->cur_offset < local->fsize) {
                local->cur_offset += GF_BLOCK_READV_SIZE;
                /* Loop back and Read the contents again. */
                STACK_WIND (frame, trash_truncate_readv_cbk,
                            FIRST_CHILD(this), FIRST_CHILD(this)->fops->readv,
                            local->fd, (size_t)GF_BLOCK_READV_SIZE,
                            local->cur_offset, 0);
                goto out;
        }


        /* OOFH.....Finally calling Truncate. */
        STACK_WIND (frame, trash_common_unwind_buf_cbk, FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->truncate, &local->loc,
                    local->fop_offset);

out:
        return 0;
}



int32_t
trash_truncate_open_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                         int32_t op_ret, int32_t op_errno, fd_t *fd)
{
        trash_local_t *local = NULL;

        local = frame->local;

        if (op_ret == -1) {
                //Let truncate work, but previous copy is not preserved.
                gf_log (this->name, GF_LOG_DEBUG,
                        "open on the existing file failed: %s",
                        strerror (op_errno));

                STACK_WIND (frame, trash_truncate_unlink_cbk,
                            FIRST_CHILD(this), FIRST_CHILD(this)->fops->unlink,
                            &local->newloc);
                goto out;
        }

        local->cur_offset = local->fop_offset;

        STACK_WIND (frame, trash_truncate_readv_cbk,
                    FIRST_CHILD (this), FIRST_CHILD (this)->fops->readv,
                    local->fd, (size_t)GF_BLOCK_READV_SIZE, local->cur_offset, 0);

out:
        return 0;
}


int32_t
trash_truncate_create_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                           int32_t op_ret, int32_t op_errno, fd_t *fd,
                           inode_t *inode, struct iatt *buf,
                           struct iatt *preparent, struct iatt *postparent)
{
        trash_local_t       *local    = NULL;
        char                *tmp_str  = NULL;
        char                *dir_name = NULL;
        char                *tmp_path = NULL;
        int32_t              flags    = 0;
        loc_t                tmp_loc  = {0,};

        local = frame->local;

        if ((op_ret == -1) && (op_errno == ENOENT)) {
                //Creating the directory structure here.
                tmp_str = gf_strdup (local->newpath);
                if (!tmp_str) {
                        gf_log (this->name, GF_LOG_DEBUG, "out of memory");
                }
                dir_name = dirname (tmp_str);

                tmp_path = gf_strdup (dir_name);
                if (!tmp_path) {
                        gf_log (this->name, GF_LOG_DEBUG, "out of memory");
                }
                tmp_loc.path = tmp_path;

                /* TODO: create the directory with proper permissions */
                STACK_WIND_COOKIE (frame, trash_truncate_mkdir_cbk,
                                   tmp_path, FIRST_CHILD(this),
                                   FIRST_CHILD(this)->fops->mkdir,
                                   &tmp_loc, 0755, NULL);
                GF_FREE (tmp_str);
                goto out;
        }

        if (op_ret == -1) {
                //Let truncate work, but previous copy is not preserved.
                //Deleting the newly created copy.
                gf_log (this->name, GF_LOG_DEBUG,
                        "creation of new file in trash-dir failed, "
                        "when truncate was called: %s", strerror (op_errno));

                STACK_WIND (frame, trash_common_unwind_buf_cbk,
                            FIRST_CHILD(this),
                            FIRST_CHILD(this)->fops->truncate, &local->loc,
                            local->fop_offset);
                goto out;
        }

        flags = O_RDONLY;

        local->fd = fd_create (local->loc.inode, frame->root->pid);

        STACK_WIND (frame, trash_truncate_open_cbk,  FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->open, &local->loc, flags,
                    local->fd, 0);
out:
        return 0;
}

int32_t
trash_truncate_mkdir_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                          int32_t op_ret, int32_t op_errno, inode_t *inode,
                          struct iatt *stbuf, struct iatt *preparent,
                          struct iatt *postparent)
{
        trash_local_t       *local = NULL;
        char                *tmp_str = NULL;
        char                *tmp_path = NULL;
        char                *tmp_dirname = NULL;
        char                *dir_name = NULL;
        int32_t              count = 0;
        int32_t              flags = 0;
        int32_t              loop_count = 0;
        int                  i = 0;
        loc_t                tmp_loc = {0,};

        local   = frame->local;
        if (!local)
                goto out;

        loop_count = local->loop_count;

        tmp_str = gf_strdup (local->newpath);
        if (!tmp_str) {
                gf_log (this->name, GF_LOG_DEBUG, "out of memory");
                goto out;
        }

        if ((op_ret == -1) && (op_errno == ENOENT)) {
                tmp_dirname = strchr (tmp_str, '/');
                while (tmp_dirname) {
                        count = tmp_dirname - tmp_str;
                        if (count == 0)
                                count = 1;
                        i++;
                        if (i > loop_count)
                                break;
                        tmp_dirname = strchr (tmp_str + count + 1, '/');
                }
                tmp_path = memdup (local->newpath, count);
                if (!tmp_path) {
                        gf_log (this->name, GF_LOG_DEBUG, "out of memory");
                }
                tmp_loc.path = tmp_path;
                STACK_WIND_COOKIE (frame, trash_truncate_mkdir_cbk,
                                   tmp_path, this->children->xlator,
                                   this->children->xlator->fops->mkdir,
                                   &tmp_loc, 0755, NULL);

                goto out;
        }

        if (op_ret == 0) {
                dir_name = dirname (tmp_str);
                if (strcmp ((char*)cookie, dir_name) == 0) {
                        flags = O_CREAT|O_EXCL|O_WRONLY;
                        ia_prot_t prot = {0, };

                        //Call create again once directory structure is created.
                        STACK_WIND (frame, trash_truncate_create_cbk,
                                    FIRST_CHILD(this), FIRST_CHILD(this)->fops->create,
                                    &local->newloc, flags,
                                    st_mode_from_ia (prot, local->loc.inode->ia_type),
                                    local->newfd, NULL);
                        goto out;
                }
        }

        LOCK (&frame->lock);
        {
                loop_count = ++local->loop_count;
        }
        UNLOCK (&frame->lock);

        tmp_dirname = strchr (tmp_str, '/');
        while (tmp_dirname) {
                count = tmp_dirname - tmp_str;
                if (count == 0)
                        count = 1;

                i++;
                if ((i > loop_count) || (count > PATH_MAX))
                        break;
                tmp_dirname = strchr (tmp_str + count + 1, '/');
        }
        tmp_path = memdup (local->newpath, count);
        if (!tmp_path) {
                gf_log (this->name, GF_LOG_DEBUG, "out of memory");
        }
        tmp_loc.path = tmp_path;

        STACK_WIND_COOKIE (frame, trash_truncate_mkdir_cbk, tmp_path,
                           this->children->xlator,
                           this->children->xlator->fops->mkdir,
                           &tmp_loc, 0755, NULL);

out:
        GF_FREE (cookie); /* strdup (dir_name) was sent here :) */
        GF_FREE (tmp_str);

        return 0;
}


int32_t
trash_truncate_stat_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                         int32_t op_ret, int32_t op_errno, struct iatt *buf)
{
        trash_private_t     *priv  = NULL;
        trash_local_t       *local = NULL;
        char                 timestr[64] = {0,};
        char                 loc_newname[PATH_MAX] = {0,};
        int32_t              flags = 0;

        priv = this->private;
        local = frame->local;

        if (op_ret == -1) {
                gf_log (this->name, GF_LOG_DEBUG,
                        "fstat on the file failed: %s",
                        strerror (op_errno));

                TRASH_STACK_UNWIND (truncate, frame, op_ret, op_errno, buf, NULL);
                return 0;
        }

        if ((buf->ia_size == 0) || (buf->ia_size > priv->max_trash_file_size)) {
                // If the file is too big, just unlink it.
                if (buf->ia_size > priv->max_trash_file_size)
                        gf_log (this->name, GF_LOG_DEBUG, "%s: file too big, "
                                "not moving to trash", local->loc.path);

                STACK_WIND (frame,  trash_common_unwind_buf_cbk,
                            this->children->xlator,
                            this->children->xlator->fops->truncate,
                            &local->loc, local->fop_offset);
                return 0;
        }

        strcpy (local->newpath, priv->trash_dir);
        strcat (local->newpath, local->loc.path);

        {
                gf_time_fmt (timestr, sizeof timestr, time (NULL),
                             gf_timefmt_F_HMS);
                strcat (local->newpath, timestr);
        }
        strcpy (loc_newname,local->loc.name);
        strcat (loc_newname,timestr);

        local->newloc.name = gf_strdup (loc_newname);
        local->newloc.path = gf_strdup (local->newpath);
        local->newloc.inode = inode_new (local->loc.inode->table);
        local->newfd = fd_create (local->newloc.inode, frame->root->pid);

        flags = O_CREAT|O_EXCL|O_WRONLY;

        STACK_WIND (frame, trash_truncate_create_cbk,
                    FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->create,
                    &local->newloc, flags,
                    st_mode_from_ia (buf->ia_prot, local->loc.inode->ia_type),
                    local->newfd, NULL);

        return 0;
}

int32_t
trash_truncate (call_frame_t *frame, xlator_t *this, loc_t *loc,
                off_t offset)
{
        trash_elim_pattern_t *trav = NULL;
        trash_private_t      *priv = NULL;
        trash_local_t        *local = NULL;
        int32_t               match = 0;

        priv  = this->private;
        if (priv->eliminate) {
                trav = priv->eliminate;
                while (trav) {
                        if (fnmatch(trav->pattern, loc->name, 0) == 0) {
                                match++;
                                break;
                        }
                        trav = trav->next;
                }
        }

        if ((strncmp (loc->path, priv->trash_dir,
                      strlen (priv->trash_dir)) == 0) || (offset) || (match)) {
                if (match) {
                        gf_log (this->name, GF_LOG_DEBUG,
                                "%s: file not moved to trash as per option "
                                "'eliminate'", loc->path);
                }

                // Trying to truncate from the trash can dir,
                //   do the actual truncate without moving to trash-dir.
                STACK_WIND (frame, trash_common_unwind_buf_cbk,
                            FIRST_CHILD(this),
                            FIRST_CHILD(this)->fops->truncate, loc, offset);
                goto out;
        }

        LOCK_INIT (&frame->lock);

        local = mem_get0 (this->local_pool);
        if (!local) {
                gf_log (this->name, GF_LOG_DEBUG, "out of memory");
                TRASH_STACK_UNWIND (truncate, frame, -1, ENOMEM, NULL, NULL);
                return 0;
        }

        loc_copy (&local->loc, loc);

        local->fop_offset = offset;

        frame->local = local;

        STACK_WIND (frame, trash_truncate_stat_cbk,
                    this->children->xlator,
                    this->children->xlator->fops->stat, loc);

out:
        return 0;
}

int32_t
trash_ftruncate_unlink_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                            int32_t op_ret, int32_t op_errno,
                            struct iatt *preparent, struct iatt *postparent)
{
        trash_local_t *local = NULL;

        local = frame->local;

        if (op_ret == -1) {
                gf_log (this->name, GF_LOG_DEBUG,
                        "%s: failed to unlink new file: %s",
                        local->newloc.path, strerror(op_errno));

        }

        STACK_WIND (frame, trash_common_unwind_buf_cbk,
                    FIRST_CHILD(this), FIRST_CHILD(this)->fops->ftruncate,
                    local->fd, local->fop_offset);

        return 0;
}

int32_t
trash_ftruncate_writev_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                            int32_t op_ret, int32_t op_errno,
                            struct iatt *prebuf, struct iatt *postbuf)
{
        trash_local_t *local = NULL;

        local = frame->local;

        if (op_ret == -1) {
                STACK_WIND (frame, trash_ftruncate_unlink_cbk,
                            FIRST_CHILD(this), FIRST_CHILD(this)->fops->unlink,
                            &local->newloc);
                return 0;
        }

        if (local->cur_offset < local->fsize) {
                local->cur_offset += GF_BLOCK_READV_SIZE;
                STACK_WIND (frame, trash_ftruncate_readv_cbk,
                            FIRST_CHILD(this), FIRST_CHILD(this)->fops->readv,
                            local->fd, (size_t)GF_BLOCK_READV_SIZE,
                            local->cur_offset, 0);
                return 0;
        }

        STACK_WIND (frame, trash_common_unwind_buf_cbk, FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->ftruncate, local->fd,
                    local->fop_offset);

        return 0;
}


int32_t
trash_ftruncate_readv_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                           int32_t op_ret, int32_t op_errno,
                           struct iovec *vector, int32_t count,
                           struct iatt *stbuf, struct iobref *iobuf)
{
        trash_local_t *local = NULL;

        local = frame->local;
        local->fsize = stbuf->ia_size;

        if (op_ret == -1) {
                STACK_WIND (frame, trash_ftruncate_unlink_cbk,
                            FIRST_CHILD(this), FIRST_CHILD(this)->fops->unlink,
                            &local->newloc);
                return 0;
        }

        STACK_WIND (frame, trash_ftruncate_writev_cbk,
                    FIRST_CHILD(this), FIRST_CHILD(this)->fops->writev,
                    local->newfd, vector, count, local->cur_offset, 0, NULL);

        return 0;
}


int32_t
trash_ftruncate_create_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                            int32_t op_ret, int32_t op_errno, fd_t *fd,
                            inode_t *inode, struct iatt *buf,
                            struct iatt *preparent, struct iatt *postparent)
{
        trash_local_t *local = NULL;
        char          *tmp_str = NULL;
        char          *dir_name = NULL;
        char          *tmp_path = NULL;
        loc_t          tmp_loc = {0,};

        local = frame->local;

        if ((op_ret == -1) && (op_errno == ENOENT)) {
                tmp_str = gf_strdup (local->newpath);
                if (!tmp_str) {
                        gf_log (this->name, GF_LOG_DEBUG, "out of memory");
                }
                dir_name = dirname (tmp_str);

                tmp_path = gf_strdup (dir_name);
                if (!tmp_path) {
                        gf_log (this->name, GF_LOG_DEBUG, "out of memory");
                }
                tmp_loc.path = tmp_path;

                /* TODO: create the directory with proper permissions */
                STACK_WIND_COOKIE (frame, trash_truncate_mkdir_cbk,
                                   tmp_path, FIRST_CHILD(this),
                                   FIRST_CHILD(this)->fops->mkdir,
                                   &tmp_loc, 0755, NULL);
                GF_FREE (tmp_str);
                return 0;
        }

        if (op_ret == -1) {
                STACK_WIND (frame, trash_common_unwind_buf_cbk,
                            FIRST_CHILD(this),
                            FIRST_CHILD(this)->fops->ftruncate,
                            local->fd, local->fop_offset);
                return 0;
        }

        STACK_WIND (frame, trash_ftruncate_readv_cbk, FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->readv, local->fd,
                    (size_t)GF_BLOCK_READV_SIZE, local->cur_offset, 0);

        return 0;
}


int32_t
trash_ftruncate_mkdir_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                           int32_t op_ret, int32_t op_errno, inode_t *inode,
                           struct iatt *stbuf, struct iatt *preparent,
                           struct iatt *postparent)
{
        trash_local_t       *local = NULL;
        char                *tmp_str = NULL;
        char                *tmp_path = NULL;
        char                *tmp_dirname = NULL;
        char                *dir_name = NULL;
        int32_t              count = 0;
        int32_t              flags = 0;
        int32_t              loop_count = 0;
        int                  i = 0;
        loc_t                tmp_loc = {0,};

        local   = frame->local;
        if (!local)
                goto out;

        loop_count = local->loop_count;

        tmp_str = gf_strdup (local->newpath);
        if (!tmp_str) {
                gf_log (this->name, GF_LOG_DEBUG, "out of memory");
                goto out;
        }

        if ((op_ret == -1) && (op_errno == ENOENT)) {
                tmp_dirname = strchr (tmp_str, '/');
                while (tmp_dirname) {
                        count = tmp_dirname - tmp_str;
                        if (count == 0)
                                count = 1;
                        i++;
                        if (i > loop_count)
                                break;
                        tmp_dirname = strchr (tmp_str + count + 1, '/');
                }
                tmp_path = memdup (local->newpath, count);
                if (!tmp_path) {
                        gf_log (this->name, GF_LOG_DEBUG, "out of memory");
                }
                tmp_loc.path = tmp_path;
                STACK_WIND_COOKIE (frame, trash_ftruncate_mkdir_cbk,
                                   tmp_path, this->children->xlator,
                                   this->children->xlator->fops->mkdir,
                                   &tmp_loc, 0755, NULL);

                goto out;
        }

        if (op_ret == 0) {
                dir_name = dirname (tmp_str);
                if (strcmp ((char*)cookie, dir_name) == 0) {
                        ia_prot_t prot = {0, };
                        flags = O_CREAT|O_EXCL|O_WRONLY;

                        //Call create again once directory structure is created.
                        STACK_WIND (frame, trash_ftruncate_create_cbk,
                                    FIRST_CHILD(this),
                                    FIRST_CHILD(this)->fops->create,
                                    &local->newloc, flags,
                                    st_mode_from_ia (prot, local->loc.inode->ia_type),
                                    local->newfd, NULL);
                        goto out;
                }
        }

        LOCK (&frame->lock);
        {
                loop_count = ++local->loop_count;
        }
        UNLOCK (&frame->lock);
        tmp_dirname = strchr (tmp_str, '/');
        while (tmp_dirname) {
                count = tmp_dirname - tmp_str;
                if (count == 0)
                        count = 1;

                i++;
                if ((i > loop_count) || (count > PATH_MAX))
                        break;
                tmp_dirname = strchr (tmp_str + count + 1, '/');
        }
        tmp_path = memdup (local->newpath, count);
        if (!tmp_path) {
                gf_log (this->name, GF_LOG_DEBUG, "out of memory");
        }
        tmp_loc.path = tmp_path;

        STACK_WIND_COOKIE (frame, trash_ftruncate_mkdir_cbk, tmp_path,
                           this->children->xlator,
                           this->children->xlator->fops->mkdir,
                           &tmp_loc, 0755, NULL);

out:
        GF_FREE (cookie); /* strdup (dir_name) was sent here :) */
        GF_FREE (tmp_str);

        return 0;
}


int32_t
trash_ftruncate_fstat_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                           int32_t op_ret, int32_t op_errno, struct iatt *buf)
{
        trash_private_t *priv  = NULL;
        trash_local_t   *local = NULL;

        priv  = this->private;
        local = frame->local;

        if (op_ret == -1) {
                gf_log (this->name, GF_LOG_DEBUG,
                        "%s: %s",local->newloc.path, strerror(op_errno));

                TRASH_STACK_UNWIND (ftruncate, frame, -1, op_errno, buf, NULL);
                return 0;
        }
        if ((buf->ia_size == 0) || (buf->ia_size > priv->max_trash_file_size))
        {
                STACK_WIND (frame, trash_common_unwind_buf_cbk,
                            this->children->xlator,
                            this->children->xlator->fops->ftruncate,
                            local->fd, local->fop_offset);
                return 0;
        }


        STACK_WIND (frame, trash_ftruncate_create_cbk, FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->create, &local->newloc,
                    ( O_CREAT | O_EXCL | O_WRONLY ),
                    st_mode_from_ia (buf->ia_prot, local->loc.inode->ia_type),
                    local->newfd, NULL);

        return 0;
}

int32_t
trash_ftruncate (call_frame_t *frame, xlator_t *this, fd_t *fd, off_t offset)
{
        trash_elim_pattern_t  *trav = NULL;
        trash_private_t       *priv = NULL;
        trash_local_t         *local = NULL;
        dentry_t              *dir_entry = NULL;
        char                  *pathbuf = NULL;
        inode_t               *newinode = NULL;
        char                   timestr[64];
        int32_t                retval = 0;
        int32_t                match = 0;

        priv = this->private;

        dir_entry = __dentry_search_arbit (fd->inode);
        retval = inode_path (fd->inode, NULL, &pathbuf);

        if (priv->eliminate) {
                trav = priv->eliminate;
                while (trav) {
                        if (fnmatch(trav->pattern, dir_entry->name, 0) == 0) {
                                match++;
                                break;
                        }
                        trav = trav->next;
                }
        }

        if ((strncmp (pathbuf, priv->trash_dir,
                      strlen (priv->trash_dir)) == 0) ||
            (offset >= priv->max_trash_file_size) ||
            (!retval) ||
            match) {
                STACK_WIND (frame, trash_common_unwind_buf_cbk,
                            FIRST_CHILD(this),
                            FIRST_CHILD(this)->fops->ftruncate,
                            fd, offset);
                return 0;
        }

        local = mem_get0 (this->local_pool);
        if (!local) {
                gf_log (this->name, GF_LOG_DEBUG, "out of memory");
                TRASH_STACK_UNWIND (ftruncate, frame, -1, ENOMEM, NULL, NULL);
                return 0;
        }

        gf_time_fmt (timestr, sizeof timestr, time (NULL), gf_timefmt_F_HMS);
        strcpy (local->newpath, priv->trash_dir);
        strcat (local->newpath, pathbuf);
        strcat (local->newpath, timestr);

        local->fd = fd_ref (fd);
        newinode = inode_new (fd->inode->table);
        local->newfd = fd_create (newinode, frame->root->pid);
        frame->local=local;

        local->newloc.inode = newinode;
        local->newloc.path = local->newpath;

        local->loc.inode = inode_ref (fd->inode);
        local->loc.path  = pathbuf;

        local->fop_offset = offset;
        local->cur_offset = offset;

        STACK_WIND (frame, trash_ftruncate_fstat_cbk, this->children->xlator,
                    this->children->xlator->fops->fstat, fd);

        return 0;
}

/**
 * trash_init -
 */
int32_t
init (xlator_t *this)
{
        data_t                *data  = NULL;
        trash_private_t       *_priv = NULL;
        trash_elim_pattern_t  *trav  = NULL;
        char                  *tmp_str = NULL;
        char                  *strtokptr = NULL;
        char                  *component = NULL;
        char                   trash_dir[PATH_MAX] = {0,};
        uint64_t               max_trash_file_size64 = 0;

        /* Create .trashcan directory in init */
        if (!this->children || this->children->next) {
                gf_log (this->name, GF_LOG_ERROR,
                        "not configured with exactly one child. exiting");
                return -1;
        }

        if (!this->parents) {
                gf_log (this->name, GF_LOG_WARNING,
                        "dangling volume. check volfile ");
        }

        _priv = GF_CALLOC (1, sizeof (*_priv), gf_trash_mt_trash_private_t);
        if (!_priv) {
                gf_log (this->name, GF_LOG_ERROR, "out of memory");
                return -1;
        }

        data = dict_get (this->options, "trash-dir");
        if (!data) {
                gf_log (this->name, GF_LOG_INFO,
                        "no option specified for 'trash-dir', "
                        "using \"/.trashcan/\"");
                _priv->trash_dir = gf_strdup ("/.trashcan");
        } else {
                /* Need a path with '/' as the first char, if not
                   given, append it */
                if (data->data[0] == '/') {
                        _priv->trash_dir = gf_strdup (data->data);
                } else {
                        /* TODO: Make sure there is no ".." in the path */
                        strcpy (trash_dir, "/");
                        strcat (trash_dir, data->data);
                        _priv->trash_dir = gf_strdup (trash_dir);
                }
        }

        data = dict_get (this->options, "eliminate-pattern");
        if (!data) {
                gf_log (this->name, GF_LOG_TRACE,
                        "no option specified for 'eliminate', using NULL");
        } else {
                tmp_str = gf_strdup (data->data);
                if (!tmp_str) {
                        gf_log (this->name, GF_LOG_DEBUG, "out of memory");
                }

                /*  Match Filename to option specified in eliminate. */
                component = strtok_r (tmp_str, "|", &strtokptr);
                while (component) {
                        trav = GF_CALLOC (1, sizeof (*trav),
                                          gf_trash_mt_trash_elim_pattern_t);
                        if (!trav) {
                                gf_log (this->name, GF_LOG_DEBUG, "out of memory");
                                break;
                        }
                        trav->pattern = component;
                        trav->next = _priv->eliminate;
                        _priv->eliminate = trav;

                        component = strtok_r (NULL, "|", &strtokptr);
                }
        }

        /* TODO: do gf_string2sizet () */
        data = dict_get (this->options, "max-trashable-file-size");
        if (!data) {
                gf_log (this->name, GF_LOG_DEBUG,
                        "no option specified for 'max-trashable-file-size', "
                        "using default = %lld MB",
                        GF_DEFAULT_MAX_FILE_SIZE / GF_UNIT_MB);
                _priv->max_trash_file_size = GF_DEFAULT_MAX_FILE_SIZE;
        } else {
                (void)gf_string2bytesize (data->data,
                                          &max_trash_file_size64);
                if( max_trash_file_size64 > GF_ALLOWED_MAX_FILE_SIZE ) {
                        gf_log (this->name, GF_LOG_DEBUG,
                                "Size specified for max-size(in MB) is too "
                                "large so using 1GB as max-size (NOT IDEAL)");
                        _priv->max_trash_file_size = GF_ALLOWED_MAX_FILE_SIZE;
                } else
                        _priv->max_trash_file_size = max_trash_file_size64;
                gf_log (this->name, GF_LOG_DEBUG, "%"GF_PRI_SIZET" max-size",
                        _priv->max_trash_file_size);
        }

        this->local_pool = mem_pool_new (trash_local_t, 64);
        if (!this->local_pool) {
                gf_log (this->name, GF_LOG_ERROR,
                        "failed to create local_t's memory pool");
                return -1;
        }


        this->private = (void *)_priv;
        return 0;
}

void
fini (xlator_t *this)
{
        trash_private_t *priv = NULL;

        priv = this->private;
        GF_FREE (priv);

        return;
}

struct xlator_fops fops = {
        .unlink    = trash_unlink,
        .rename    = trash_rename,
        .truncate  = trash_truncate,
        .ftruncate = trash_ftruncate,
};

struct xlator_cbks cbks = {
};

struct volume_options options[] = {
        { .key  = { "trash-directory" },
          .type = GF_OPTION_TYPE_PATH,
        },
        { .key  = { "eliminate-pattern" },
          .type = GF_OPTION_TYPE_STR,
        },
        { .key  = { "max-trashable-file-size" },
          .type = GF_OPTION_TYPE_SIZET,
        },
        { .key  = {NULL} },
};
