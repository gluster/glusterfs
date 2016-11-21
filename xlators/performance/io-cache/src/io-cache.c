/*
  Copyright (c) 2008-2012 Red Hat, Inc. <http://www.redhat.com>
  This file is part of GlusterFS.

  This file is licensed to you under your choice of the GNU Lesser
  General Public License, version 3 or any later version (LGPLv3 or
  later), or the GNU General Public License, version 2 (GPLv2), in all
  cases as published by the Free Software Foundation.
*/

#include "glusterfs.h"
#include "logging.h"
#include "dict.h"
#include "xlator.h"
#include "io-cache.h"
#include "ioc-mem-types.h"
#include "statedump.h"
#include <assert.h>
#include <sys/time.h>
#include "io-cache-messages.h"
int ioc_log2_page_size;

uint32_t
ioc_get_priority (ioc_table_t *table, const char *path);

struct volume_options options[];


static uint32_t
ioc_hashfn (void *data, int len)
{
        off_t offset;

        offset = *(off_t *) data;

        return (offset >> ioc_log2_page_size);
}

/* TODO: This function is not used, uncomment when we find a
         usage for this function.

static ioc_inode_t *
ioc_inode_reupdate (ioc_inode_t *ioc_inode)
{
        ioc_table_t *table = NULL;

        table = ioc_inode->table;

        list_add_tail (&ioc_inode->inode_lru,
                       &table->inode_lru[ioc_inode->weight]);

        return ioc_inode;
}


static ioc_inode_t *
ioc_get_inode (dict_t *dict, char *name)
{
        ioc_inode_t *ioc_inode      = NULL;
        data_t      *ioc_inode_data = NULL;
        ioc_table_t *table          = NULL;

        ioc_inode_data = dict_get (dict, name);
        if (ioc_inode_data) {
                ioc_inode = data_to_ptr (ioc_inode_data);
                table = ioc_inode->table;

                ioc_table_lock (table);
                {
                        if (list_empty (&ioc_inode->inode_lru)) {
                                ioc_inode = ioc_inode_reupdate (ioc_inode);
                        }
                }
                ioc_table_unlock (table);
        }

        return ioc_inode;
}
*/

int32_t
ioc_inode_need_revalidate (ioc_inode_t *ioc_inode)
{
        int8_t          need_revalidate = 0;
        struct timeval  tv              = {0,};
        ioc_table_t    *table           = NULL;

        table = ioc_inode->table;

        gettimeofday (&tv, NULL);

        if (time_elapsed (&tv, &ioc_inode->cache.tv) >= table->cache_timeout)
                need_revalidate = 1;

        return need_revalidate;
}

/*
 * __ioc_inode_flush - flush all the cached pages of the given inode
 *
 * @ioc_inode:
 *
 * assumes lock is held
 */
int64_t
__ioc_inode_flush (ioc_inode_t *ioc_inode)
{
        ioc_page_t *curr         = NULL, *next = NULL;
        int64_t     destroy_size = 0;
        int64_t     ret          = 0;

        list_for_each_entry_safe (curr, next, &ioc_inode->cache.page_lru,
                                  page_lru) {
                ret = __ioc_page_destroy (curr);

                if (ret != -1)
                        destroy_size += ret;
        }

        return destroy_size;
}

void
ioc_inode_flush (ioc_inode_t *ioc_inode)
{
        int64_t destroy_size = 0;

        ioc_inode_lock (ioc_inode);
        {
                destroy_size = __ioc_inode_flush (ioc_inode);
        }
        ioc_inode_unlock (ioc_inode);

        if (destroy_size) {
                ioc_table_lock (ioc_inode->table);
                {
                        ioc_inode->table->cache_used -= destroy_size;
                }
                ioc_table_unlock (ioc_inode->table);
        }

        return;
}

int32_t
ioc_setattr_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                 int32_t op_ret, int32_t op_errno,
                 struct iatt *preop, struct iatt *postop, dict_t *xdata)
{
        STACK_UNWIND_STRICT (setattr, frame, op_ret, op_errno, preop, postop,
                             xdata);
        return 0;
}

int32_t
ioc_setattr (call_frame_t *frame, xlator_t *this, loc_t *loc,
             struct iatt *stbuf, int32_t valid, dict_t *xdata)
{
        uint64_t ioc_inode = 0;

        inode_ctx_get (loc->inode, this, &ioc_inode);

        if (ioc_inode
            && ((valid & GF_SET_ATTR_ATIME)
                || (valid & GF_SET_ATTR_MTIME)))
                ioc_inode_flush ((ioc_inode_t *)(long)ioc_inode);

        STACK_WIND (frame, ioc_setattr_cbk, FIRST_CHILD (this),
                    FIRST_CHILD (this)->fops->setattr, loc, stbuf, valid, xdata);

        return 0;
}

int32_t
ioc_lookup_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                int32_t op_ret,	int32_t op_errno, inode_t *inode,
                struct iatt *stbuf, dict_t *xdata, struct iatt *postparent)
{
        ioc_inode_t *ioc_inode         = NULL;
        ioc_table_t *table             = NULL;
        uint8_t      cache_still_valid = 0;
        uint64_t     tmp_ioc_inode     = 0;
        uint32_t     weight            = 0xffffffff;
        const char  *path              = NULL;
        ioc_local_t *local             = NULL;

        if (op_ret != 0)
                goto out;

        local = frame->local;
        if (local == NULL) {
                op_ret = -1;
                op_errno = EINVAL;
                goto out;
        }

        if (!this || !this->private) {
                op_ret = -1;
                op_errno = EINVAL;
                goto out;
        }

        table = this->private;

        path = local->file_loc.path;

        LOCK (&inode->lock);
        {
                __inode_ctx_get (inode, this, &tmp_ioc_inode);
                ioc_inode = (ioc_inode_t *)(long)tmp_ioc_inode;

                if (!ioc_inode) {
                        weight = ioc_get_priority (table, path);

                        ioc_inode = ioc_inode_update (table, inode,
                                                      weight);

                        __inode_ctx_put (inode, this,
                                         (uint64_t)(long)ioc_inode);
                }
        }
        UNLOCK (&inode->lock);

        ioc_inode_lock (ioc_inode);
        {
                if (ioc_inode->cache.mtime == 0) {
                        ioc_inode->cache.mtime = stbuf->ia_mtime;
                        ioc_inode->cache.mtime_nsec = stbuf->ia_mtime_nsec;
                }

                ioc_inode->ia_size = stbuf->ia_size;
        }
        ioc_inode_unlock (ioc_inode);

        cache_still_valid = ioc_cache_still_valid (ioc_inode,
                                                   stbuf);

        if (!cache_still_valid) {
                ioc_inode_flush (ioc_inode);
        }

        ioc_table_lock (ioc_inode->table);
        {
                list_move_tail (&ioc_inode->inode_lru,
                                &table->inode_lru[ioc_inode->weight]);
        }
        ioc_table_unlock (ioc_inode->table);

out:
        if (frame->local != NULL) {
                local = frame->local;
                loc_wipe (&local->file_loc);
        }

        STACK_UNWIND_STRICT (lookup, frame, op_ret, op_errno, inode, stbuf,
                             xdata, postparent);
        return 0;
}

int32_t
ioc_lookup (call_frame_t *frame, xlator_t *this, loc_t *loc,
            dict_t *xdata)
{
        ioc_local_t *local    = NULL;
        int32_t      op_errno = -1, ret = -1;

        local = mem_get0 (this->local_pool);
        if (local == NULL) {
                op_errno = ENOMEM;
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        IO_CACHE_MSG_NO_MEMORY, "out of memory");
                goto unwind;
        }

        ret = loc_copy (&local->file_loc, loc);
        if (ret != 0) {
                op_errno = ENOMEM;
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        IO_CACHE_MSG_NO_MEMORY, "out of memory");
                goto unwind;
        }

        frame->local = local;

        STACK_WIND (frame, ioc_lookup_cbk, FIRST_CHILD (this),
                    FIRST_CHILD (this)->fops->lookup, loc, xdata);

        return 0;

unwind:
        STACK_UNWIND_STRICT (lookup, frame, -1, op_errno, NULL, NULL,
                             NULL, NULL);

        return 0;
}

/*
 * ioc_forget -
 *
 * @frame:
 * @this:
 * @inode:
 *
 */
int32_t
ioc_forget (xlator_t *this, inode_t *inode)
{
        uint64_t ioc_inode = 0;

        inode_ctx_get (inode, this, &ioc_inode);

        if (ioc_inode)
                ioc_inode_destroy ((ioc_inode_t *)(long)ioc_inode);

        return 0;
}

static int32_t
ioc_invalidate(xlator_t *this, inode_t *inode)
{
	uint64_t     ioc_addr = 0;
	ioc_inode_t *ioc_inode = NULL;

	inode_ctx_get(inode, this, (uint64_t *) &ioc_addr);
	ioc_inode = (void *) ioc_addr;

	if (ioc_inode)
		ioc_inode_flush(ioc_inode);

	return 0;
}

/*
 * ioc_cache_validate_cbk -
 *
 * @frame:
 * @cookie:
 * @this:
 * @op_ret:
 * @op_errno:
 * @buf
 *
 */
int32_t
ioc_cache_validate_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                        int32_t op_ret, int32_t op_errno, struct iatt *stbuf,
                        dict_t *xdata)
{
        ioc_local_t *local        = NULL;
        ioc_inode_t *ioc_inode    = NULL;
        size_t       destroy_size = 0;
        struct iatt *local_stbuf  = NULL;

        local = frame->local;
        ioc_inode = local->inode;
        local_stbuf = stbuf;

        if ((op_ret == -1) ||
            ((op_ret >= 0) && !ioc_cache_still_valid(ioc_inode, stbuf))) {
                gf_msg_debug (ioc_inode->table->xl->name, 0,
                              "cache for inode(%p) is invalid. flushing all pages",
                              ioc_inode);
                /* NOTE: only pages with no waiting frames are flushed by
                 * ioc_inode_flush. page_fault will be generated for all
                 * the pages which have waiting frames by ioc_inode_wakeup()
                 */
                ioc_inode_lock (ioc_inode);
                {
                        destroy_size = __ioc_inode_flush (ioc_inode);
                        if (op_ret >= 0) {
                                ioc_inode->cache.mtime = stbuf->ia_mtime;
                                ioc_inode->cache.mtime_nsec
                                        = stbuf->ia_mtime_nsec;
                        }
                }
                ioc_inode_unlock (ioc_inode);
                local_stbuf = NULL;
        }

        if (destroy_size) {
                ioc_table_lock (ioc_inode->table);
                {
                        ioc_inode->table->cache_used -= destroy_size;
                }
                ioc_table_unlock (ioc_inode->table);
        }

        if (op_ret < 0)
                local_stbuf = NULL;

        ioc_inode_lock (ioc_inode);
        {
                gettimeofday (&ioc_inode->cache.tv, NULL);
        }
        ioc_inode_unlock (ioc_inode);

        ioc_inode_wakeup (frame, ioc_inode, local_stbuf);

        /* any page-fault initiated by ioc_inode_wakeup() will have its own
         * fd_ref on fd, safe to unref validate frame's private copy
         */
        fd_unref (local->fd);

        STACK_DESTROY (frame->root);

        return 0;
}

int32_t
ioc_wait_on_inode (ioc_inode_t *ioc_inode, ioc_page_t *page)
{
        ioc_waitq_t *waiter     = NULL, *trav = NULL;
        uint32_t     page_found = 0;
        int32_t      ret        = 0;

        trav = ioc_inode->waitq;

        while (trav) {
                if (trav->data == page) {
                        page_found = 1;
                        break;
                }
                trav = trav->next;
        }

        if (!page_found) {
                waiter = GF_CALLOC (1, sizeof (ioc_waitq_t),
                                    gf_ioc_mt_ioc_waitq_t);
                if (waiter == NULL) {
                        gf_msg (ioc_inode->table->xl->name, GF_LOG_ERROR,
                                ENOMEM, IO_CACHE_MSG_NO_MEMORY,
                                "out of memory");
                        ret = -ENOMEM;
                        goto out;
                }

                waiter->data = page;
                waiter->next = ioc_inode->waitq;
                ioc_inode->waitq = waiter;
        }

out:
        return ret;
}

/*
 * ioc_cache_validate -
 *
 * @frame:
 * @ioc_inode:
 * @fd:
 *
 */
int32_t
ioc_cache_validate (call_frame_t *frame, ioc_inode_t *ioc_inode, fd_t *fd,
                    ioc_page_t *page)
{
        call_frame_t *validate_frame = NULL;
        ioc_local_t  *validate_local = NULL;
        ioc_local_t  *local          = NULL;
        int32_t       ret            = 0;

        local = frame->local;
        validate_local = mem_get0 (THIS->local_pool);
        if (validate_local == NULL) {
                ret = -1;
                local->op_ret = -1;
                local->op_errno = ENOMEM;
                gf_msg (ioc_inode->table->xl->name, GF_LOG_ERROR,
                        0, IO_CACHE_MSG_NO_MEMORY, "out of memory");
                goto out;
        }

        validate_frame = copy_frame (frame);
        if (validate_frame == NULL) {
                ret = -1;
                local->op_ret = -1;
                local->op_errno = ENOMEM;
                mem_put (validate_local);
                gf_msg (ioc_inode->table->xl->name, GF_LOG_ERROR,
                        0, IO_CACHE_MSG_NO_MEMORY, "out of memory");
                goto out;
        }

        validate_local->fd = fd_ref (fd);
        validate_local->inode = ioc_inode;
        validate_frame->local = validate_local;

        STACK_WIND (validate_frame, ioc_cache_validate_cbk,
                    FIRST_CHILD (frame->this),
                    FIRST_CHILD (frame->this)->fops->fstat, fd, NULL);

out:
        return ret;
}

static uint32_t
is_match (const char *path, const char *pattern)
{
        int32_t ret = 0;

        ret = fnmatch (pattern, path, FNM_NOESCAPE);

        return (ret == 0);
}

uint32_t
ioc_get_priority (ioc_table_t *table, const char *path)
{
        uint32_t             priority = 1;
        struct ioc_priority *curr     = NULL;

        if (list_empty(&table->priority_list))
                return priority;

        priority = 0;
        list_for_each_entry (curr, &table->priority_list, list) {
                if (is_match (path, curr->pattern))
                        priority = curr->priority;
        }

        return priority;
}

/*
 * ioc_open_cbk - open callback for io cache
 *
 * @frame: call frame
 * @cookie:
 * @this:
 * @op_ret:
 * @op_errno:
 * @fd:
 *
 */
int32_t
ioc_open_cbk (call_frame_t *frame, void *cookie, xlator_t *this, int32_t op_ret,
              int32_t op_errno, fd_t *fd, dict_t *xdata)
{
        uint64_t     tmp_ioc_inode = 0;
        ioc_local_t *local         = NULL;
        ioc_table_t *table         = NULL;
        ioc_inode_t *ioc_inode     = NULL;
        uint32_t     weight        = 0xffffffff;

        local = frame->local;
        if (!this || !this->private) {
                op_ret = -1;
                op_errno = EINVAL;
                goto out;
        }

        table = this->private;

        if (op_ret != -1) {
                inode_ctx_get (fd->inode, this, &tmp_ioc_inode);
                ioc_inode = (ioc_inode_t *)(long)tmp_ioc_inode;

                //TODO: see why inode context is NULL and handle it.
                if (!ioc_inode) {
                        gf_msg (this->name, GF_LOG_ERROR,
                                EINVAL, IO_CACHE_MSG_ENFORCEMENT_FAILED,
                                "inode context is NULL (%s)",
                                uuid_utoa (fd->inode->gfid));
                        goto out;
                }

                ioc_table_lock (ioc_inode->table);
                {
                        list_move_tail (&ioc_inode->inode_lru,
                                        &table->inode_lru[ioc_inode->weight]);
                }
                ioc_table_unlock (ioc_inode->table);

                ioc_inode_lock (ioc_inode);
                {
                        if ((table->min_file_size > ioc_inode->ia_size)
                            || ((table->max_file_size > 0)
                                && (table->max_file_size < ioc_inode->ia_size))) {
                                fd_ctx_set (fd, this, 1);
                        }
                }
                ioc_inode_unlock (ioc_inode);

                /* If O_DIRECT open, we disable caching on it */
                if ((local->flags & O_DIRECT)){
                        /* O_DIRECT is only for one fd, not the inode
                         * as a whole
                         */
                        fd_ctx_set (fd, this, 1);
                }

                /* weight = 0, we disable caching on it */
                if (weight == 0) {
                        /* we allow a pattern-matched cache disable this way
                         */
                        fd_ctx_set (fd, this, 1);
                }
        }

out:
        mem_put (local);
        frame->local = NULL;

        STACK_UNWIND_STRICT (open, frame, op_ret, op_errno, fd, xdata);

        return 0;
}

/*
 * ioc_create_cbk - create callback for io cache
 *
 * @frame: call frame
 * @cookie:
 * @this:
 * @op_ret:
 * @op_errno:
 * @fd:
 * @inode:
 * @buf:
 *
 */
int32_t
ioc_create_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                int32_t op_ret,	int32_t op_errno, fd_t *fd,
                inode_t *inode,	struct iatt *buf, struct iatt *preparent,
                struct iatt *postparent, dict_t *xdata)
{
        ioc_local_t *local     = NULL;
        ioc_table_t *table     = NULL;
        ioc_inode_t *ioc_inode = NULL;
        uint32_t     weight    = 0xffffffff;
        const char  *path      = NULL;
        int          ret       = -1;

        local = frame->local;
        if (!this || !this->private) {
                op_ret = -1;
                op_errno = EINVAL;
                goto out;
        }

        table = this->private;
        path = local->file_loc.path;

        if (op_ret != -1) {
                /* assign weight */
                weight = ioc_get_priority (table, path);

                ioc_inode = ioc_inode_update (table, inode, weight);

                ioc_inode_lock (ioc_inode);
                {
                        ioc_inode->cache.mtime = buf->ia_mtime;
                        ioc_inode->cache.mtime_nsec = buf->ia_mtime_nsec;
                        ioc_inode->ia_size = buf->ia_size;

                        if ((table->min_file_size > ioc_inode->ia_size)
                            || ((table->max_file_size > 0)
                                && (table->max_file_size < ioc_inode->ia_size))) {
                                ret = fd_ctx_set (fd, this, 1);
                                if (ret)
                                        gf_msg (this->name, GF_LOG_WARNING,
                                                ENOMEM, IO_CACHE_MSG_NO_MEMORY,
                                                "%s: failed to set fd ctx",
                                                local->file_loc.path);
                        }
                }
                ioc_inode_unlock (ioc_inode);

                inode_ctx_put (fd->inode, this,
                               (uint64_t)(long)ioc_inode);

                /* If O_DIRECT open, we disable caching on it */
                if (local->flags & O_DIRECT) {
                        /*
                         * O_DIRECT is only for one fd, not the inode
                         * as a whole */
                        ret = fd_ctx_set (fd, this, 1);
                        if (ret)
                                gf_msg (this->name, GF_LOG_WARNING,
                                        ENOMEM, IO_CACHE_MSG_NO_MEMORY,
                                        "%s: failed to set fd ctx",
                                        local->file_loc.path);
                }

                /* if weight == 0, we disable caching on it */
                if (!weight) {
                        /* we allow a pattern-matched cache disable this way */
                        ret = fd_ctx_set (fd, this, 1);
                        if (ret)
                                gf_msg (this->name, GF_LOG_WARNING,
                                        ENOMEM, IO_CACHE_MSG_NO_MEMORY,
                                        "%s: failed to set fd ctx",
                                        local->file_loc.path);
                }

        }

out:
        frame->local = NULL;
        mem_put (local);

        STACK_UNWIND_STRICT (create, frame, op_ret, op_errno, fd, inode, buf,
                             preparent, postparent, xdata);

        return 0;
}


int32_t
ioc_mknod_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
               int32_t op_ret, int32_t op_errno, inode_t *inode,
               struct iatt *buf, struct iatt *preparent,
               struct iatt *postparent, dict_t *xdata)
{
        ioc_local_t *local     = NULL;
        ioc_table_t *table     = NULL;
        ioc_inode_t *ioc_inode = NULL;
        uint32_t     weight    = 0xffffffff;
        const char  *path      = NULL;

        local = frame->local;
        if (!this || !this->private) {
                op_ret = -1;
                op_errno = EINVAL;
                goto out;
        }

        table = this->private;
        path = local->file_loc.path;

        if (op_ret != -1) {
                /* assign weight */
                weight = ioc_get_priority (table, path);

                ioc_inode = ioc_inode_update (table, inode, weight);

                ioc_inode_lock (ioc_inode);
                {
                        ioc_inode->cache.mtime = buf->ia_mtime;
                        ioc_inode->cache.mtime_nsec = buf->ia_mtime_nsec;
                        ioc_inode->ia_size = buf->ia_size;
                }
                ioc_inode_unlock (ioc_inode);

                inode_ctx_put (inode, this,
                               (uint64_t)(long)ioc_inode);
        }

out:
        frame->local = NULL;

        loc_wipe (&local->file_loc);
        mem_put (local);

        STACK_UNWIND_STRICT (mknod, frame, op_ret, op_errno, inode, buf,
                             preparent, postparent, xdata);
        return 0;
}


int
ioc_mknod (call_frame_t *frame, xlator_t *this, loc_t *loc, mode_t mode,
           dev_t rdev, mode_t umask, dict_t *xdata)
{
        ioc_local_t *local    = NULL;
        int32_t      op_errno = -1, ret = -1;

        local = mem_get0 (this->local_pool);
        if (local == NULL) {
                op_errno = ENOMEM;
                gf_msg (this->name, GF_LOG_ERROR,
                        0, IO_CACHE_MSG_NO_MEMORY, "out of memory");
                goto unwind;
        }

        ret = loc_copy (&local->file_loc, loc);
        if (ret != 0) {
                op_errno = ENOMEM;
                gf_msg (this->name, GF_LOG_ERROR,
                        0, IO_CACHE_MSG_NO_MEMORY, "out of memory");
                goto unwind;
        }

        frame->local = local;

        STACK_WIND (frame, ioc_mknod_cbk,
                    FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->mknod,
                    loc, mode, rdev, umask, xdata);
        return 0;

unwind:
        if (local != NULL) {
                loc_wipe (&local->file_loc);
                mem_put (local);
        }

        STACK_UNWIND_STRICT (mknod, frame, -1, op_errno, NULL, NULL,
                             NULL, NULL, NULL);

        return 0;
}


/*
 * ioc_open - open fop for io cache
 * @frame:
 * @this:
 * @loc:
 * @flags:
 *
 */
int32_t
ioc_open (call_frame_t *frame, xlator_t *this, loc_t *loc, int32_t flags,
          fd_t *fd, dict_t *xdata)
{

        ioc_local_t *local = NULL;

        local = mem_get0 (this->local_pool);
        if (local == NULL) {
                gf_msg (this->name, GF_LOG_ERROR,
                        ENOMEM, IO_CACHE_MSG_NO_MEMORY, "out of memory");
                STACK_UNWIND_STRICT (open, frame, -1, ENOMEM, NULL, NULL);
                return 0;
        }

        local->flags = flags;
        local->file_loc.path = loc->path;
        local->file_loc.inode = loc->inode;

        frame->local = local;

        STACK_WIND (frame, ioc_open_cbk, FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->open, loc, flags, fd,
                    xdata);

        return 0;
}

/*
 * ioc_create - create fop for io cache
 *
 * @frame:
 * @this:
 * @pathname:
 * @flags:
 * @mode:
 *
 */
int32_t
ioc_create (call_frame_t *frame, xlator_t *this, loc_t *loc, int32_t flags,
            mode_t mode, mode_t umask, fd_t *fd, dict_t *xdata)
{
        ioc_local_t *local = NULL;

        local = mem_get0 (this->local_pool);
        if (local == NULL) {
                gf_msg (this->name, GF_LOG_ERROR,
                        ENOMEM, IO_CACHE_MSG_NO_MEMORY, "out of memory");
                STACK_UNWIND_STRICT (create, frame, -1, ENOMEM, NULL, NULL,
                                     NULL, NULL, NULL, NULL);
                return 0;
        }

        local->flags = flags;
        local->file_loc.path = loc->path;
        frame->local = local;

        STACK_WIND (frame, ioc_create_cbk, FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->create, loc, flags, mode,
                    umask, fd, xdata);

        return 0;
}




/*
 * ioc_release - release fop for io cache
 *
 * @frame:
 * @this:
 * @fd:
 *
 */
int32_t
ioc_release (xlator_t *this, fd_t *fd)
{
        return 0;
}

int32_t
ioc_need_prune (ioc_table_t *table)
{
        int64_t cache_difference = 0;

        ioc_table_lock (table);
        {
                cache_difference = table->cache_used - table->cache_size;
        }
        ioc_table_unlock (table);

        if (cache_difference > 0)
                return 1;
        else
                return 0;
}

/*
 * ioc_dispatch_requests -
 *
 * @frame:
 * @inode:
 *
 *
 */
void
ioc_dispatch_requests (call_frame_t *frame, ioc_inode_t *ioc_inode, fd_t *fd,
                       off_t offset, size_t size)
{
        ioc_local_t *local               = NULL;
        ioc_table_t *table               = NULL;
        ioc_page_t  *trav                = NULL;
        ioc_waitq_t *waitq               = NULL;
        off_t        rounded_offset      = 0;
        off_t        rounded_end         = 0;
        off_t        trav_offset         = 0;
        int32_t      fault               = 0;
        size_t       trav_size           = 0;
        off_t        local_offset        = 0;
        int32_t      ret                 = -1;
        int8_t       need_validate       = 0;
        int8_t       might_need_validate = 0;  /*
                                                * if a page exists, do we need
                                                * to validate it?
                                                */
        local = frame->local;
        table = ioc_inode->table;

        rounded_offset = floor (offset, table->page_size);
        rounded_end = roof (offset + size, table->page_size);
        trav_offset = rounded_offset;

        /* once a frame does read, it should be waiting on something */
        local->wait_count++;

        /* Requested region can fall in three different pages,
         * 1. Ready - region is already in cache, we just have to serve it.
         * 2. In-transit - page fault has been generated on this page, we need
         *    to wait till the page is ready
         * 3. Fault - page is not in cache, we have to generate a page fault
         */

        might_need_validate = ioc_inode_need_revalidate (ioc_inode);

        while (trav_offset < rounded_end) {
                ioc_inode_lock (ioc_inode);
                {
                        /* look for requested region in the cache */
                        trav = __ioc_page_get (ioc_inode, trav_offset);

                        local_offset = max (trav_offset, offset);
                        trav_size = min (((offset+size) - local_offset),
                                         table->page_size);

                        if (!trav) {
                                /* page not in cache, we need to generate page
                                 * fault
                                 */
                                trav = __ioc_page_create (ioc_inode,
                                                          trav_offset);
                                fault = 1;
                                if (!trav) {
                                        gf_msg (frame->this->name,
                                                GF_LOG_CRITICAL,
                                                ENOMEM, IO_CACHE_MSG_NO_MEMORY,
                                                "out of memory");
                                        local->op_ret = -1;
                                        local->op_errno = ENOMEM;
                                        ioc_inode_unlock (ioc_inode);
                                        goto out;
                                }
                        }

                        __ioc_wait_on_page (trav, frame, local_offset,
                                            trav_size);

                        if (trav->ready) {
                                /* page found in cache */
                                if (!might_need_validate && !ioc_inode->waitq) {
                                        /* fresh enough */
                                        gf_msg_trace (frame->this->name, 0,
                                                      "cache hit for "
                                                      "trav_offset=%"
                                                      PRId64"/local_"
                                                      "offset=%"PRId64"",
                                                      trav_offset,
                                                      local_offset);
                                        waitq = __ioc_page_wakeup (trav,
                                                                   trav->op_errno);
                                } else {
                                        /* if waitq already exists, fstat
                                         * revalidate is
                                         * already on the way
                                         */
                                        if (!ioc_inode->waitq) {
                                                need_validate = 1;
                                        }

                                        ret = ioc_wait_on_inode (ioc_inode,
                                                                 trav);
                                        if (ret < 0) {
                                                local->op_ret = -1;
                                                local->op_errno = -ret;
                                                need_validate = 0;

                                                waitq = __ioc_page_wakeup (trav,
                                                                           trav->op_errno);
                                                ioc_inode_unlock (ioc_inode);

                                                ioc_waitq_return (waitq);
                                                waitq = NULL;
                                                goto out;
                                        }
                                }
                        }

                }
                ioc_inode_unlock (ioc_inode);

                ioc_waitq_return (waitq);
                waitq = NULL;

                if (fault) {
                        fault = 0;
                        /* new page created, increase the table->cache_used */
                        ioc_page_fault (ioc_inode, frame, fd, trav_offset);
                }

                if (need_validate) {
                        need_validate = 0;
                        gf_msg_trace (frame->this->name, 0,
                                      "sending validate request for "
                                      "inode(%s) at offset=%"PRId64"",
                                      uuid_utoa (fd->inode->gfid), trav_offset);
                        ret = ioc_cache_validate (frame, ioc_inode, fd, trav);
                        if (ret == -1) {
                                ioc_inode_lock (ioc_inode);
                                {
                                        waitq = __ioc_page_wakeup (trav,
                                                                   trav->op_errno);
                                }
                                ioc_inode_unlock (ioc_inode);

                                ioc_waitq_return (waitq);
                                waitq = NULL;
                                goto out;
                        }
                }

                trav_offset += table->page_size;
        }

out:
        ioc_frame_return (frame);

        if (ioc_need_prune (ioc_inode->table)) {
                ioc_prune (ioc_inode->table);
        }

        return;
}


/*
 * ioc_readv -
 *
 * @frame:
 * @this:
 * @fd:
 * @size:
 * @offset:
 *
 */
int32_t
ioc_readv (call_frame_t *frame, xlator_t *this, fd_t *fd,
           size_t size, off_t offset, uint32_t flags, dict_t *xdata)
{
        uint64_t     tmp_ioc_inode = 0;
        ioc_inode_t *ioc_inode     = NULL;
        ioc_local_t *local         = NULL;
        uint32_t     weight        = 0;
        ioc_table_t *table         = NULL;
        int32_t      op_errno      = -1;

        if (!this) {
                goto out;
        }

        inode_ctx_get (fd->inode, this, &tmp_ioc_inode);
        ioc_inode = (ioc_inode_t *)(long)tmp_ioc_inode;
        if (!ioc_inode) {
                /* caching disabled, go ahead with normal readv */
                STACK_WIND_TAIL (frame, FIRST_CHILD (this),
                                 FIRST_CHILD (this)->fops->readv, fd,
                                 size, offset, flags, xdata);
                return 0;
        }

        if (flags & O_DIRECT) {
                /* disable caching for this fd, if O_DIRECT is used */
                STACK_WIND_TAIL (frame, FIRST_CHILD (this),
                                 FIRST_CHILD (this)->fops->readv, fd,
                                 size, offset, flags, xdata);
                return 0;
        }


        table = this->private;

        if (!table) {
                gf_msg (this->name, GF_LOG_ERROR, EINVAL,
                        IO_CACHE_MSG_ENFORCEMENT_FAILED, "table is null");
                op_errno = EINVAL;
                goto out;
        }

        ioc_inode_lock (ioc_inode);
        {
                if (!ioc_inode->cache.page_table) {
                        ioc_inode->cache.page_table
                                = rbthash_table_init
                                (IOC_PAGE_TABLE_BUCKET_COUNT,
                                 ioc_hashfn, NULL, 0,
                                 table->mem_pool);

                        if (ioc_inode->cache.page_table == NULL) {
                                op_errno = ENOMEM;
                                ioc_inode_unlock (ioc_inode);
                                goto out;
                        }
                }
        }
        ioc_inode_unlock (ioc_inode);

        if (!fd_ctx_get (fd, this, NULL)) {
                /* disable caching for this fd, go ahead with normal readv */
                STACK_WIND_TAIL (frame, FIRST_CHILD (this),
                                 FIRST_CHILD (this)->fops->readv, fd,
                                 size, offset, flags, xdata);
                return 0;
        }

        local = mem_get0 (this->local_pool);
        if (local == NULL) {
                gf_msg (this->name, GF_LOG_ERROR,
                        ENOMEM, IO_CACHE_MSG_NO_MEMORY, "out of memory");
                op_errno = ENOMEM;
                goto out;
        }

        INIT_LIST_HEAD (&local->fill_list);

        frame->local = local;
        local->pending_offset = offset;
        local->pending_size = size;
        local->offset = offset;
        local->size = size;
        local->inode = ioc_inode;

        gf_msg_trace (this->name, 0,
                      "NEW REQ (%p) offset "
                      "= %"PRId64" && size = %"GF_PRI_SIZET"",
                      frame, offset, size);

        weight = ioc_inode->weight;

        ioc_table_lock (ioc_inode->table);
        {
                list_move_tail (&ioc_inode->inode_lru,
                                &ioc_inode->table->inode_lru[weight]);
        }
        ioc_table_unlock (ioc_inode->table);

        ioc_dispatch_requests (frame, ioc_inode, fd, offset, size);
        return 0;

out:
        STACK_UNWIND_STRICT (readv, frame, -1, op_errno, NULL, 0, NULL, NULL,
                             NULL);
        return 0;
}

/*
 * ioc_writev_cbk -
 *
 * @frame:
 * @cookie:
 * @this:
 * @op_ret:
 * @op_errno:
 *
 */
int32_t
ioc_writev_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                int32_t op_ret,	int32_t op_errno, struct iatt *prebuf,
                struct iatt *postbuf, dict_t *xdata)
{
        ioc_local_t *local     = NULL;
        uint64_t     ioc_inode = 0;

        local = frame->local;
        inode_ctx_get (local->fd->inode, this, &ioc_inode);

        if (ioc_inode)
                ioc_inode_flush ((ioc_inode_t *)(long)ioc_inode);

        STACK_UNWIND_STRICT (writev, frame, op_ret, op_errno, prebuf, postbuf,
                             xdata);
        return 0;
}

/*
 * ioc_writev
 *
 * @frame:
 * @this:
 * @fd:
 * @vector:
 * @count:
 * @offset:
 *
 */
int32_t
ioc_writev (call_frame_t *frame, xlator_t *this, fd_t *fd,
            struct iovec *vector, int32_t count, off_t offset,
            uint32_t flags, struct iobref *iobref, dict_t *xdata)
{
        ioc_local_t *local     = NULL;
        uint64_t     ioc_inode = 0;

        local = mem_get0 (this->local_pool);
        if (local == NULL) {
                gf_msg (this->name, GF_LOG_ERROR,
                        ENOMEM, IO_CACHE_MSG_NO_MEMORY, "out of memory");

                STACK_UNWIND_STRICT (writev, frame, -1, ENOMEM, NULL, NULL, NULL);
                return 0;
        }

        /* TODO: why is it not fd_ref'ed */
        local->fd = fd;
        frame->local = local;

        inode_ctx_get (fd->inode, this, &ioc_inode);
        if (ioc_inode)
                ioc_inode_flush ((ioc_inode_t *)(long)ioc_inode);

        STACK_WIND (frame, ioc_writev_cbk, FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->writev, fd, vector, count, offset,
                    flags, iobref, xdata);

        return 0;
}

/*
 * ioc_truncate_cbk -
 *
 * @frame:
 * @cookie:
 * @this:
 * @op_ret:
 * @op_errno:
 * @buf:
 *
 */
int32_t
ioc_truncate_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                  int32_t op_ret, int32_t op_errno, struct iatt *prebuf,
                  struct iatt *postbuf, dict_t *xdata)
{

        STACK_UNWIND_STRICT (truncate, frame, op_ret, op_errno, prebuf,
                             postbuf, xdata);
        return 0;
}


/*
 * ioc_ftruncate_cbk -
 *
 * @frame:
 * @cookie:
 * @this:
 * @op_ret:
 * @op_errno:
 * @buf:
 *
 */
int32_t
ioc_ftruncate_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                   int32_t op_ret, int32_t op_errno, struct iatt *prebuf,
                   struct iatt *postbuf, dict_t *xdata)
{

        STACK_UNWIND_STRICT (ftruncate, frame, op_ret, op_errno, prebuf,
                             postbuf, xdata);
        return 0;
}


/*
 * ioc_truncate -
 *
 * @frame:
 * @this:
 * @loc:
 * @offset:
 *
 */
int32_t
ioc_truncate (call_frame_t *frame, xlator_t *this, loc_t *loc, off_t offset,
              dict_t *xdata)
{
        uint64_t ioc_inode = 0;

        inode_ctx_get (loc->inode, this, &ioc_inode);

        if (ioc_inode)
                ioc_inode_flush ((ioc_inode_t *)(long)ioc_inode);

        STACK_WIND (frame, ioc_truncate_cbk, FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->truncate, loc, offset, xdata);
        return 0;
}

/*
 * ioc_ftruncate -
 *
 * @frame:
 * @this:
 * @fd:
 * @offset:
 *
 */
int32_t
ioc_ftruncate (call_frame_t *frame, xlator_t *this, fd_t *fd, off_t offset,
               dict_t *xdata)
{
        uint64_t ioc_inode = 0;

        inode_ctx_get (fd->inode, this, &ioc_inode);

        if (ioc_inode)
                ioc_inode_flush ((ioc_inode_t *)(long)ioc_inode);

        STACK_WIND (frame, ioc_ftruncate_cbk, FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->ftruncate, fd, offset, xdata);
        return 0;
}

int32_t
ioc_lk_cbk (call_frame_t *frame, void *cookie, xlator_t *this, int32_t op_ret,
            int32_t op_errno, struct gf_flock *lock, dict_t *xdata)
{
        STACK_UNWIND_STRICT (lk, frame, op_ret, op_errno, lock, xdata);
        return 0;
}

int32_t
ioc_lk (call_frame_t *frame, xlator_t *this, fd_t *fd, int32_t cmd,
        struct gf_flock *lock, dict_t *xdata)
{
        ioc_inode_t *ioc_inode = NULL;
        uint64_t     tmp_inode = 0;

        inode_ctx_get (fd->inode, this, &tmp_inode);
        ioc_inode = (ioc_inode_t *)(long)tmp_inode;
        if (!ioc_inode) {
                gf_msg_debug (this->name, EBADFD,
                              "inode context is NULL: returning EBADFD");
                STACK_UNWIND_STRICT (lk, frame, -1, EBADFD, NULL, NULL);
                return 0;
        }

        ioc_inode_lock (ioc_inode);
        {
                gettimeofday (&ioc_inode->cache.tv, NULL);
        }
        ioc_inode_unlock (ioc_inode);

        STACK_WIND (frame, ioc_lk_cbk, FIRST_CHILD (this),
                    FIRST_CHILD (this)->fops->lk, fd, cmd, lock, xdata);

        return 0;
}

int
ioc_readdirp_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                  int op_ret, int op_errno, gf_dirent_t *entries, dict_t *xdata)
{
        gf_dirent_t *entry = NULL;

        if (op_ret <= 0)
                goto unwind;

        list_for_each_entry (entry, &entries->list, list) {
                /* TODO: fill things */
        }

unwind:
        STACK_UNWIND_STRICT (readdirp, frame, op_ret, op_errno, entries, xdata);

        return 0;
}
int
ioc_readdirp (call_frame_t *frame, xlator_t *this, fd_t *fd, size_t size,
              off_t offset, dict_t *dict)
{
        STACK_WIND (frame, ioc_readdirp_cbk,
                    FIRST_CHILD(this), FIRST_CHILD(this)->fops->readdirp,
                    fd, size, offset, dict);

        return 0;
}

static int32_t
ioc_discard_cbk(call_frame_t *frame, void *cookie, xlator_t *this,
		int32_t op_ret, int32_t op_errno, struct iatt *pre,
		struct iatt *post, dict_t *xdata)
{
	STACK_UNWIND_STRICT(discard, frame, op_ret, op_errno, pre, post, xdata);
	return 0;
}

static int32_t
ioc_discard(call_frame_t *frame, xlator_t *this, fd_t *fd, off_t offset,
	    size_t len, dict_t *xdata)
{
	uint64_t ioc_inode = 0;

	inode_ctx_get (fd->inode, this, &ioc_inode);

	if (ioc_inode)
		ioc_inode_flush ((ioc_inode_t *)(long)ioc_inode);

	STACK_WIND(frame, ioc_discard_cbk, FIRST_CHILD(this),
		   FIRST_CHILD(this)->fops->discard, fd, offset, len, xdata);
       return 0;
}

static int32_t
ioc_zerofill_cbk(call_frame_t *frame, void *cookie, xlator_t *this,
                int32_t op_ret, int32_t op_errno, struct iatt *pre,
                struct iatt *post, dict_t *xdata)
{
        STACK_UNWIND_STRICT(zerofill, frame, op_ret,
                            op_errno, pre, post, xdata);
        return 0;
}

static int32_t
ioc_zerofill(call_frame_t *frame, xlator_t *this, fd_t *fd, off_t offset,
            off_t len, dict_t *xdata)
{
        uint64_t ioc_inode = 0;

        inode_ctx_get (fd->inode, this, &ioc_inode);

        if (ioc_inode)
                ioc_inode_flush ((ioc_inode_t *)(long)ioc_inode);

        STACK_WIND(frame, ioc_zerofill_cbk, FIRST_CHILD(this),
                   FIRST_CHILD(this)->fops->zerofill, fd, offset, len, xdata);
       return 0;
}


int32_t
ioc_get_priority_list (const char *opt_str, struct list_head *first)
{
        int32_t              max_pri    = 1;
        char                *tmp_str    = NULL;
        char                *tmp_str1   = NULL;
        char                *tmp_str2   = NULL;
        char                *dup_str    = NULL;
        char                *stripe_str = NULL;
        char                *pattern    = NULL;
        char                *priority   = NULL;
        char                *string     = NULL;
        struct ioc_priority *curr       = NULL, *tmp = NULL;

        string = gf_strdup (opt_str);
        if (string == NULL) {
                max_pri = -1;
                goto out;
        }

        /* Get the pattern for cache priority.
         * "option priority *.jpg:1,abc*:2" etc
         */
        /* TODO: inode_lru in table is statically hard-coded to 5,
         * should be changed to run-time configuration
         */
        stripe_str = strtok_r (string, ",", &tmp_str);
        while (stripe_str) {
                curr = GF_CALLOC (1, sizeof (struct ioc_priority),
                                  gf_ioc_mt_ioc_priority);
                if (curr == NULL) {
                        max_pri = -1;
                        goto out;
                }

                list_add_tail (&curr->list, first);

                dup_str = gf_strdup (stripe_str);
                if (dup_str == NULL) {
                        max_pri = -1;
                        goto out;
                }

                pattern = strtok_r (dup_str, ":", &tmp_str1);
                if (!pattern) {
                        max_pri = -1;
                        goto out;
                }

                priority = strtok_r (NULL, ":", &tmp_str1);
                if (!priority) {
                        max_pri = -1;
                        goto out;
                }

                gf_msg_trace ("io-cache", 0,
                              "ioc priority : pattern %s : priority %s",
                              pattern, priority);

                curr->pattern = gf_strdup (pattern);
                if (curr->pattern == NULL) {
                        max_pri = -1;
                        goto out;
                }

                curr->priority = strtol (priority, &tmp_str2, 0);
                if (tmp_str2 && (*tmp_str2)) {
                        max_pri = -1;
                        goto out;
                } else {
                        max_pri = max (max_pri, curr->priority);
                }

                GF_FREE (dup_str);
                dup_str = NULL;

                stripe_str = strtok_r (NULL, ",", &tmp_str);
        }
out:
        GF_FREE (string);

        GF_FREE (dup_str);

        if (max_pri == -1) {
                list_for_each_entry_safe (curr, tmp, first, list) {
                        list_del_init (&curr->list);
                        GF_FREE (curr->pattern);
                        GF_FREE (curr);
                }
        }

        return max_pri;
}

int32_t
mem_acct_init (xlator_t *this)
{
        int     ret = -1;

        if (!this)
                return ret;

        ret = xlator_mem_acct_init (this, gf_ioc_mt_end + 1);

        if (ret != 0) {
                gf_msg (this->name, GF_LOG_ERROR,
                        ENOMEM, IO_CACHE_MSG_NO_MEMORY,
                        "Memory accounting init failed");
                return ret;
        }

        return ret;
}


static gf_boolean_t
check_cache_size_ok (xlator_t *this, uint64_t cache_size)
{
        gf_boolean_t            ret = _gf_true;
        uint64_t                total_mem = 0;
        uint64_t                max_cache_size = 0;
        volume_option_t         *opt = NULL;

        GF_ASSERT (this);
        opt = xlator_volume_option_get (this, "cache-size");
        if (!opt) {
                ret = _gf_false;
                gf_msg (this->name, GF_LOG_ERROR,
                        EINVAL, IO_CACHE_MSG_ENFORCEMENT_FAILED,
                        "could not get cache-size option");
                goto out;
        }

        total_mem = get_mem_size ();
        if (-1 == total_mem)
                max_cache_size = opt->max;
        else
                max_cache_size = total_mem;

        gf_msg_debug (this->name, 0, "Max cache size is %"PRIu64,
                      max_cache_size);

        if (cache_size > max_cache_size) {
                ret = _gf_false;
                gf_msg (this->name, GF_LOG_ERROR,
                        0, IO_CACHE_MSG_INVALID_ARGUMENT,
                        "Cache size %"PRIu64
                        " is greater than the max size of %"PRIu64,
                        cache_size, max_cache_size);
                goto out;
        }
out:
        return ret;
}

int
reconfigure (xlator_t *this, dict_t *options)
{
        data_t      *data              = NULL;
        ioc_table_t *table             = NULL;
        int          ret               = -1;
        uint64_t      cache_size_new    = 0;
        if (!this || !this->private)
                goto out;

        table = this->private;

        ioc_table_lock (table);
        {
                GF_OPTION_RECONF ("cache-timeout", table->cache_timeout,
                                  options, int32, unlock);

                data = dict_get (options, "priority");
                if (data) {
                        char *option_list = data_to_str (data);

                        gf_msg_trace (this->name, 0,
                                      "option path %s", option_list);
                        /* parse the list of pattern:priority */
                        table->max_pri = ioc_get_priority_list (option_list,
                                                                &table->priority_list);

                        if (table->max_pri == -1) {
                                goto unlock;
                        }
                        table->max_pri ++;
                }

                GF_OPTION_RECONF ("max-file-size", table->max_file_size,
                                  options, size_uint64, unlock);

                GF_OPTION_RECONF ("min-file-size", table->min_file_size,
                                  options, size_uint64, unlock);

                if ((table->max_file_size <= UINT64_MAX) &&
                    (table->min_file_size > table->max_file_size)) {
                        gf_msg (this->name, GF_LOG_ERROR, 0,
                                IO_CACHE_MSG_INVALID_ARGUMENT, "minimum size (%"
                                PRIu64") of a file that can be cached is "
                                "greater than maximum size (%"PRIu64"). "
                                "Hence Defaulting to old value",
                                table->min_file_size, table->max_file_size);
                        goto unlock;
                }

                GF_OPTION_RECONF ("cache-size", cache_size_new,
                                  options, size_uint64, unlock);
                if (!check_cache_size_ok (this, cache_size_new)) {
                        ret = -1;
                        gf_msg (this->name, GF_LOG_ERROR,
                                0, IO_CACHE_MSG_INVALID_ARGUMENT,
                                "Not reconfiguring cache-size");
                        goto unlock;
                }
                table->cache_size = cache_size_new;

                ret = 0;
        }
unlock:
        ioc_table_unlock (table);
out:
        return ret;
}


/*
 * init -
 * @this:
 *
 */
int32_t
init (xlator_t *this)
{
        ioc_table_t     *table             = NULL;
        dict_t          *xl_options        = NULL;
        uint32_t         index             = 0;
        int32_t          ret               = -1;
        glusterfs_ctx_t *ctx               = NULL;
        data_t          *data              = 0;
        uint32_t         num_pages         = 0;

        xl_options = this->options;

        if (!this->children || this->children->next) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        IO_CACHE_MSG_XLATOR_CHILD_MISCONFIGURED,
                        "FATAL: io-cache not configured with exactly "
                        "one child");
                goto out;
        }

        if (!this->parents) {
                gf_msg (this->name, GF_LOG_WARNING, 0,
                        IO_CACHE_MSG_VOL_MISCONFIGURED,
                        "dangling volume. check volfile ");
        }

        table = (void *) GF_CALLOC (1, sizeof (*table), gf_ioc_mt_ioc_table_t);
        if (table == NULL) {
                gf_msg (this->name, GF_LOG_ERROR, ENOMEM,
                        IO_CACHE_MSG_NO_MEMORY, "out of memory");
                goto out;
        }

        table->xl = this;
        table->page_size = this->ctx->page_size;

        GF_OPTION_INIT ("cache-size", table->cache_size, size_uint64, out);

        GF_OPTION_INIT ("cache-timeout", table->cache_timeout, int32, out);

        GF_OPTION_INIT ("min-file-size", table->min_file_size, size_uint64, out);

        GF_OPTION_INIT ("max-file-size", table->max_file_size, size_uint64, out);

        if  (!check_cache_size_ok (this, table->cache_size)) {
                ret = -1;
                goto out;
        }

        INIT_LIST_HEAD (&table->priority_list);
        table->max_pri = 1;
        data = dict_get (xl_options, "priority");
        if (data) {
                char *option_list = data_to_str (data);
                gf_msg_trace (this->name, 0,
                              "option path %s", option_list);
                /* parse the list of pattern:priority */
                table->max_pri = ioc_get_priority_list (option_list,
                                                        &table->priority_list);

                if (table->max_pri == -1) {
                        goto out;
                }
        }
        table->max_pri ++;

        INIT_LIST_HEAD (&table->inodes);

        if ((table->max_file_size <= UINT64_MAX)
            && (table->min_file_size > table->max_file_size)) {
                gf_msg ("io-cache", GF_LOG_ERROR, 0,
                        IO_CACHE_MSG_INVALID_ARGUMENT, "minimum size (%"
                        PRIu64") of a file that can be cached is "
                        "greater than maximum size (%"PRIu64")",
                        table->min_file_size, table->max_file_size);
                goto out;
        }

        table->inode_lru = GF_CALLOC (table->max_pri,
                                      sizeof (struct list_head),
                                      gf_ioc_mt_list_head);
        if (table->inode_lru == NULL) {
                goto out;
        }

        for (index = 0; index < (table->max_pri); index++)
                INIT_LIST_HEAD (&table->inode_lru[index]);

        this->local_pool = mem_pool_new (ioc_local_t, 64);
        if (!this->local_pool) {
                ret = -1;
                gf_msg (this->name, GF_LOG_ERROR,
                        ENOMEM, IO_CACHE_MSG_NO_MEMORY,
                        "failed to create local_t's memory pool");
                goto out;
        }

        pthread_mutex_init (&table->table_lock, NULL);
        this->private = table;

        num_pages = (table->cache_size / table->page_size)
                + ((table->cache_size % table->page_size)
                   ? 1 : 0);

        table->mem_pool = mem_pool_new (rbthash_entry_t, num_pages);
        if (!table->mem_pool) {
                gf_msg (this->name, GF_LOG_ERROR, ENOMEM,
                        IO_CACHE_MSG_NO_MEMORY, "Unable to allocate mem_pool");
                goto out;
        }

        ret = 0;

        ctx = this->ctx;
        ioc_log2_page_size = log_base2 (ctx->page_size);

out:
        if (ret == -1) {
                if (table != NULL) {
                        GF_FREE (table->inode_lru);
                        GF_FREE (table);
                }
        }

        return ret;
}

void
ioc_page_waitq_dump (ioc_page_t *page, char *prefix)
{
        ioc_waitq_t  *trav                     = NULL;
        call_frame_t *frame                    = NULL;
        int32_t       i                        = 0;
        char          key[GF_DUMP_MAX_BUF_LEN] = {0, };

        trav = page->waitq;

        while (trav) {
                frame = trav->data;
                sprintf (key, "waitq.frame[%d]", i++);
                gf_proc_dump_write (key, "%"PRId64, frame->root->unique);

                trav = trav->next;
        }
}

void
__ioc_inode_waitq_dump (ioc_inode_t *ioc_inode, char *prefix)
{
        ioc_waitq_t *trav                     = NULL;
        ioc_page_t  *page                     = NULL;
        int32_t      i                        = 0;
        char         key[GF_DUMP_MAX_BUF_LEN] = {0, };

        trav = ioc_inode->waitq;

        while (trav) {
                page = trav->data;

                sprintf (key, "cache-validation-waitq.page[%d].offset", i++);
                gf_proc_dump_write (key, "%"PRId64, page->offset);

                trav = trav->next;
        }
}

void
__ioc_page_dump (ioc_page_t *page, char *prefix)
{

        int    ret = -1;

        if (!page)
                return;
        /* ioc_page_lock can be used to hold the mutex. But in statedump
         * its better to use trylock to avoid deadlocks.
         */
        ret = pthread_mutex_trylock (&page->page_lock);
        if (ret)
                goto out;
        {
                gf_proc_dump_write ("offset", "%"PRId64, page->offset);
                gf_proc_dump_write ("size", "%"PRId64, page->size);
                gf_proc_dump_write ("dirty", "%s", page->dirty ? "yes" : "no");
                gf_proc_dump_write ("ready", "%s", page->ready ? "yes" : "no");
                ioc_page_waitq_dump (page, prefix);
        }
        pthread_mutex_unlock (&page->page_lock);

out:
        if (ret && page)
                gf_proc_dump_write ("Unable to dump the page information",
                                    "(Lock acquisition failed) %p", page);

        return;
}

void
__ioc_cache_dump (ioc_inode_t *ioc_inode, char *prefix)
{
        off_t        offset                   = 0;
        ioc_table_t *table                    = NULL;
        ioc_page_t  *page                     = NULL;
        int          i                        = 0;
        char         key[GF_DUMP_MAX_BUF_LEN] = {0, };
        char         timestr[256]             = {0, };

        if ((ioc_inode == NULL) || (prefix == NULL)) {
                goto out;
        }

        table = ioc_inode->table;

        if (ioc_inode->cache.tv.tv_sec) {
                gf_time_fmt (timestr, sizeof timestr,
                             ioc_inode->cache.tv.tv_sec, gf_timefmt_FT);
                snprintf (timestr + strlen (timestr), sizeof timestr - strlen (timestr),
                          ".%"GF_PRI_SUSECONDS, ioc_inode->cache.tv.tv_usec);

                gf_proc_dump_write ("last-cache-validation-time", "%s",
                                    timestr);
        }

        for (offset = 0; offset < ioc_inode->ia_size;
             offset += table->page_size) {
                page = __ioc_page_get (ioc_inode, offset);
                if (page == NULL) {
                        continue;
                }

                sprintf (key, "inode.cache.page[%d]", i++);
                __ioc_page_dump (page, key);
        }
out:
        return;
}


int
ioc_inode_dump (xlator_t *this, inode_t *inode)
{

        char         *path                            = NULL;
        int           ret                             = -1;
        char          key_prefix[GF_DUMP_MAX_BUF_LEN] = {0, };
        uint64_t      tmp_ioc_inode                   = 0;
        ioc_inode_t  *ioc_inode                       = NULL;
        gf_boolean_t  section_added                   = _gf_false;
        char          uuid_str[64]                    = {0,};

        if (this == NULL || inode == NULL)
                goto out;

        gf_proc_dump_build_key (key_prefix, "io-cache", "inode");

        inode_ctx_get (inode, this, &tmp_ioc_inode);
        ioc_inode = (ioc_inode_t *)(long)tmp_ioc_inode;
        if (ioc_inode == NULL)
                goto out;

        /* Similar to ioc_page_dump function its better to use
         * pthread_mutex_trylock and not to use gf_log in statedump
         * to avoid deadlocks.
         */
        ret = pthread_mutex_trylock (&ioc_inode->inode_lock);
        if (ret)
                goto out;

        {
                if (gf_uuid_is_null (ioc_inode->inode->gfid))
                        goto unlock;

                gf_proc_dump_add_section (key_prefix);
                section_added = _gf_true;

                __inode_path (ioc_inode->inode, NULL, &path);

                gf_proc_dump_write ("inode.weight", "%d", ioc_inode->weight);

                if (path) {
                        gf_proc_dump_write ("path", "%s", path);
                        GF_FREE (path);
                }

                gf_proc_dump_write ("uuid", "%s", uuid_utoa_r
                                    (ioc_inode->inode->gfid, uuid_str));
                __ioc_cache_dump (ioc_inode, key_prefix);
                __ioc_inode_waitq_dump (ioc_inode, key_prefix);
        }
unlock:
        pthread_mutex_unlock (&ioc_inode->inode_lock);

out:
        if (ret && ioc_inode) {
                if (section_added == _gf_false)
                        gf_proc_dump_add_section (key_prefix);
                gf_proc_dump_write ("Unable to print the status of ioc_inode",
                                    "(Lock acquisition failed) %s",
                                    uuid_utoa (inode->gfid));
        }
        return ret;
}

int
ioc_priv_dump (xlator_t *this)
{
        ioc_table_t *priv                            = NULL;
        char         key_prefix[GF_DUMP_MAX_BUF_LEN] = {0, };
        int          ret                             = -1;
        gf_boolean_t add_section                     = _gf_false;

        if (!this || !this->private)
                goto out;

        priv = this->private;

        gf_proc_dump_build_key (key_prefix, "io-cache", "priv");
        gf_proc_dump_add_section (key_prefix);
        add_section = _gf_true;

        ret = pthread_mutex_trylock (&priv->table_lock);
        if (ret)
                goto out;
        {
                gf_proc_dump_write ("page_size", "%ld", priv->page_size);
                gf_proc_dump_write ("cache_size", "%ld", priv->cache_size);
                gf_proc_dump_write ("cache_used", "%ld", priv->cache_used);
                gf_proc_dump_write ("inode_count", "%u", priv->inode_count);
                gf_proc_dump_write ("cache_timeout", "%u", priv->cache_timeout);
                gf_proc_dump_write ("min-file-size", "%u", priv->min_file_size);
                gf_proc_dump_write ("max-file-size", "%u", priv->max_file_size);
        }
        pthread_mutex_unlock (&priv->table_lock);
out:
        if (ret && priv) {
                if (!add_section) {
                        gf_proc_dump_build_key (key_prefix, "xlator."
                                                "performance.io-cache", "priv");
                        gf_proc_dump_add_section (key_prefix);
                }
                gf_proc_dump_write ("Unable to dump the state of private "
                                    "structure of io-cache xlator", "(Lock "
                                    "acquisition failed) %s", this->name);
        }

        return 0;
}

/*
 * fini -
 *
 * @this:
 *
 */
void
fini (xlator_t *this)
{
        ioc_table_t         *table = NULL;
        struct ioc_priority *curr  = NULL, *tmp = NULL;

        table = this->private;

        if (table == NULL)
                return;

        this->private = NULL;

        if (table->mem_pool != NULL) {
                mem_pool_destroy (table->mem_pool);
                table->mem_pool = NULL;
        }

        list_for_each_entry_safe (curr, tmp, &table->priority_list, list) {
                list_del_init (&curr->list);
                GF_FREE (curr->pattern);
                GF_FREE (curr);
        }

        /* inode_lru and inodes list can be empty in case fini() is
         * called soon after init()? Hence commenting the below asserts.
         */
        /*for (i = 0; i < table->max_pri; i++) {
                GF_ASSERT (list_empty (&table->inode_lru[i]));
        }

        GF_ASSERT (list_empty (&table->inodes));
        */
        pthread_mutex_destroy (&table->table_lock);
        GF_FREE (table);

        this->private = NULL;
        return;
}

struct xlator_fops fops = {
        .open        = ioc_open,
        .create      = ioc_create,
        .readv       = ioc_readv,
        .writev      = ioc_writev,
        .truncate    = ioc_truncate,
        .ftruncate   = ioc_ftruncate,
        .lookup      = ioc_lookup,
        .lk          = ioc_lk,
        .setattr     = ioc_setattr,
        .mknod       = ioc_mknod,

        .readdirp    = ioc_readdirp,
	.discard     = ioc_discard,
        .zerofill    = ioc_zerofill,
};


struct xlator_dumpops dumpops = {
        .priv        = ioc_priv_dump,
        .inodectx    = ioc_inode_dump,
};

struct xlator_cbks cbks = {
        .forget      = ioc_forget,
        .release     = ioc_release,
	.invalidate  = ioc_invalidate,
};

struct volume_options options[] = {
        { .key  = {"priority"},
          .type = GF_OPTION_TYPE_PRIORITY_LIST,
          .default_value = "",
          .description = "Assigns priority to filenames with specific "
          "patterns so that when a page needs to be ejected "
          "out of the cache, the page of a file whose "
          "priority is the lowest will be ejected earlier"
        },
        { .key  = {"cache-timeout", "force-revalidate-timeout"},
          .type = GF_OPTION_TYPE_INT,
          .min  = 0,
          .max  = 60,
          .default_value = "1",
          .description = "The cached data for a file will be retained till "
          "'cache-refresh-timeout' seconds, after which data "
          "re-validation is performed."
        },
        { .key  = {"cache-size"},
          .type = GF_OPTION_TYPE_SIZET,
          .min  = 4 * GF_UNIT_MB,
          .max  = 32 * GF_UNIT_GB,
          .default_value = "32MB",
          .description = "Size of the read cache."
        },
        { .key  = {"min-file-size"},
          .type = GF_OPTION_TYPE_SIZET,
          .default_value = "0",
          .description = "Minimum file size which would be cached by the "
          "io-cache translator."
        },
        { .key  = {"max-file-size"},
          .type = GF_OPTION_TYPE_SIZET,
          .default_value = "0",
          .description = "Maximum file size which would be cached by the "
          "io-cache translator."
        },
        { .key = {NULL} },
};
