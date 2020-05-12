/*
  Copyright (c) 2008-2012 Red Hat, Inc. <http://www.redhat.com>
  This file is part of GlusterFS.

  This file is licensed to you under your choice of the GNU Lesser
  General Public License, version 3 or any later version (LGPLv3 or
  later), or the GNU General Public License, version 2 (GPLv2), in all
  cases as published by the Free Software Foundation.
*/

#include "glusterfs/fd.h"
#include <errno.h>     // for EINVAL, errno, ENOMEM
#include <inttypes.h>  // for PRIu64
#include <stdint.h>    // for UINT32_MAX
#include <string.h>    // for NULL, memcpy, memset, size_t
#include "glusterfs/statedump.h"

static int
gf_fd_fdtable_expand(fdtable_t *fdtable, uint32_t nr);

fd_t *
__fd_ref(fd_t *fd);

static int
gf_fd_chain_fd_entries(fdentry_t *entries, uint32_t startidx, uint32_t endcount)
{
    uint32_t i = 0;

    if (!entries) {
        gf_msg_callingfn("fd", GF_LOG_WARNING, EINVAL, LG_MSG_INVALID_ARG,
                         "!entries");
        return -1;
    }

    /* Chain only till the second to last entry because we want to
     * ensure that the last entry has GF_FDTABLE_END.
     */
    for (i = startidx; i < (endcount - 1); i++)
        entries[i].next_free = i + 1;

    /* i has already been incremented up to the last entry. */
    entries[i].next_free = GF_FDTABLE_END;

    return 0;
}

static int
gf_fd_fdtable_expand(fdtable_t *fdtable, uint32_t nr)
{
    fdentry_t *oldfds = NULL;
    uint32_t oldmax_fds = -1;
    int ret = -1;

    if (fdtable == NULL || nr > UINT32_MAX) {
        gf_msg_callingfn("fd", GF_LOG_ERROR, EINVAL, LG_MSG_INVALID_ARG,
                         "invalid argument");
        ret = EINVAL;
        goto out;
    }

    nr /= (1024 / sizeof(fdentry_t));
    nr = gf_roundup_next_power_of_two(nr + 1);
    nr *= (1024 / sizeof(fdentry_t));

    oldfds = fdtable->fdentries;
    oldmax_fds = fdtable->max_fds;

    fdtable->fdentries = GF_CALLOC(nr, sizeof(fdentry_t),
                                   gf_common_mt_fdentry_t);
    if (!fdtable->fdentries) {
        ret = ENOMEM;
        goto out;
    }
    fdtable->max_fds = nr;

    if (oldfds) {
        uint32_t cpy = oldmax_fds * sizeof(fdentry_t);
        memcpy(fdtable->fdentries, oldfds, cpy);
    }

    gf_fd_chain_fd_entries(fdtable->fdentries, oldmax_fds, fdtable->max_fds);

    /* Now that expansion is done, we must update the fd list
     * head pointer so that the fd allocation functions can continue
     * using the expanded table.
     */
    fdtable->first_free = oldmax_fds;
    GF_FREE(oldfds);
    ret = 0;
out:
    return ret;
}

fdtable_t *
gf_fd_fdtable_alloc(void)
{
    fdtable_t *fdtable = NULL;

    fdtable = GF_CALLOC(1, sizeof(*fdtable), gf_common_mt_fdtable_t);
    if (!fdtable)
        return NULL;

    pthread_rwlock_init(&fdtable->lock, NULL);

    pthread_rwlock_wrlock(&fdtable->lock);
    {
        gf_fd_fdtable_expand(fdtable, 0);
    }
    pthread_rwlock_unlock(&fdtable->lock);

    return fdtable;
}

static fdentry_t *
__gf_fd_fdtable_get_all_fds(fdtable_t *fdtable, uint32_t *count)
{
    fdentry_t *fdentries = NULL;

    if (count == NULL) {
        gf_msg_callingfn("fd", GF_LOG_WARNING, EINVAL, LG_MSG_INVALID_ARG,
                         "!count");
        goto out;
    }

    fdentries = fdtable->fdentries;
    fdtable->fdentries = GF_CALLOC(fdtable->max_fds, sizeof(fdentry_t),
                                   gf_common_mt_fdentry_t);
    gf_fd_chain_fd_entries(fdtable->fdentries, 0, fdtable->max_fds);
    *count = fdtable->max_fds;

out:
    return fdentries;
}

fdentry_t *
gf_fd_fdtable_get_all_fds(fdtable_t *fdtable, uint32_t *count)
{
    fdentry_t *entries = NULL;

    if (fdtable) {
        pthread_rwlock_wrlock(&fdtable->lock);
        {
            entries = __gf_fd_fdtable_get_all_fds(fdtable, count);
        }
        pthread_rwlock_unlock(&fdtable->lock);
    }

    return entries;
}

static fdentry_t *
__gf_fd_fdtable_copy_all_fds(fdtable_t *fdtable, uint32_t *count)
{
    fdentry_t *fdentries = NULL;
    int i = 0;

    if (count == NULL) {
        gf_msg_callingfn("fd", GF_LOG_WARNING, EINVAL, LG_MSG_INVALID_ARG,
                         "!count");
        goto out;
    }

    fdentries = GF_CALLOC(fdtable->max_fds, sizeof(fdentry_t),
                          gf_common_mt_fdentry_t);
    if (fdentries == NULL) {
        goto out;
    }

    *count = fdtable->max_fds;

    for (i = 0; i < fdtable->max_fds; i++) {
        if (fdtable->fdentries[i].fd != NULL) {
            fdentries[i].fd = fd_ref(fdtable->fdentries[i].fd);
        }
    }

out:
    return fdentries;
}

fdentry_t *
gf_fd_fdtable_copy_all_fds(fdtable_t *fdtable, uint32_t *count)
{
    fdentry_t *entries = NULL;

    if (fdtable) {
        pthread_rwlock_rdlock(&fdtable->lock);
        {
            entries = __gf_fd_fdtable_copy_all_fds(fdtable, count);
        }
        pthread_rwlock_unlock(&fdtable->lock);
    }

    return entries;
}

void
gf_fd_fdtable_destroy(fdtable_t *fdtable)
{
    struct list_head list = {
        0,
    };
    fd_t *fd = NULL;
    fdentry_t *fdentries = NULL;
    uint32_t fd_count = 0;
    int32_t i = 0;

    INIT_LIST_HEAD(&list);

    if (!fdtable) {
        gf_msg_callingfn("fd", GF_LOG_WARNING, EINVAL, LG_MSG_INVALID_ARG,
                         "!fdtable");
        return;
    }

    pthread_rwlock_wrlock(&fdtable->lock);
    {
        fdentries = __gf_fd_fdtable_get_all_fds(fdtable, &fd_count);
        GF_FREE(fdtable->fdentries);
    }
    pthread_rwlock_unlock(&fdtable->lock);

    if (fdentries != NULL) {
        for (i = 0; i < fd_count; i++) {
            fd = fdentries[i].fd;
            if (fd != NULL) {
                fd_unref(fd);
            }
        }

        GF_FREE(fdentries);
        pthread_rwlock_destroy(&fdtable->lock);
        GF_FREE(fdtable);
    }
}

int
gf_fd_unused_get(fdtable_t *fdtable, fd_t *fdptr)
{
    int32_t fd = -1;
    fdentry_t *fde = NULL;
    int error;
    int alloc_attempts = 0;

    if (fdtable == NULL || fdptr == NULL) {
        gf_msg_callingfn("fd", GF_LOG_ERROR, EINVAL, LG_MSG_INVALID_ARG,
                         "invalid argument");
        return EINVAL;
    }

    pthread_rwlock_wrlock(&fdtable->lock);
    {
    fd_alloc_try_again:
        if (fdtable->first_free != GF_FDTABLE_END) {
            fde = &fdtable->fdentries[fdtable->first_free];
            fd = fdtable->first_free;
            fdtable->first_free = fde->next_free;
            fde->next_free = GF_FDENTRY_ALLOCATED;
            fde->fd = fdptr;
        } else {
            /* If this is true, there is something
             * seriously wrong with our data structures.
             */
            if (alloc_attempts >= 2) {
                gf_msg("fd", GF_LOG_ERROR, 0, LG_MSG_EXPAND_FD_TABLE_FAILED,
                       "multiple attempts to expand fd table"
                       " have failed.");
                goto out;
            }
            error = gf_fd_fdtable_expand(fdtable, fdtable->max_fds + 1);
            if (error) {
                gf_msg("fd", GF_LOG_ERROR, error, LG_MSG_EXPAND_FD_TABLE_FAILED,
                       "Cannot expand fdtable");
                goto out;
            }
            ++alloc_attempts;
            /* At this point, the table stands expanded
             * with the first_free referring to the first
             * free entry in the new set of fdentries that
             * have just been allocated. That means, the
             * above logic should just work.
             */
            goto fd_alloc_try_again;
        }
    }
out:
    pthread_rwlock_unlock(&fdtable->lock);

    return fd;
}

void
gf_fd_put(fdtable_t *fdtable, int32_t fd)
{
    fd_t *fdptr = NULL;
    fdentry_t *fde = NULL;

    if (fd == GF_ANON_FD_NO)
        return;

    if (fdtable == NULL || fd < 0) {
        gf_msg_callingfn("fd", GF_LOG_ERROR, EINVAL, LG_MSG_INVALID_ARG,
                         "invalid argument");
        return;
    }

    if (!(fd < fdtable->max_fds)) {
        gf_msg_callingfn("fd", GF_LOG_ERROR, EINVAL, LG_MSG_INVALID_ARG,
                         "invalid argument");
        return;
    }

    pthread_rwlock_wrlock(&fdtable->lock);
    {
        fde = &fdtable->fdentries[fd];
        /* If the entry is not allocated, put operation must return
         * without doing anything.
         * This has the potential of masking out any bugs in a user of
         * fd that ends up calling gf_fd_put twice for the same fd or
         * for an unallocated fd, but it is a price we have to pay for
         * ensuring sanity of our fd-table.
         */
        if (fde->next_free != GF_FDENTRY_ALLOCATED)
            goto unlock_out;
        fdptr = fde->fd;
        fde->fd = NULL;
        fde->next_free = fdtable->first_free;
        fdtable->first_free = fd;
    }
unlock_out:
    pthread_rwlock_unlock(&fdtable->lock);

    if (fdptr) {
        fd_unref(fdptr);
    }
}

void
gf_fdptr_put(fdtable_t *fdtable, fd_t *fd)
{
    fdentry_t *fde = NULL;
    int32_t i = 0;

    if ((fdtable == NULL) || (fd == NULL)) {
        gf_msg_callingfn("fd", GF_LOG_ERROR, EINVAL, LG_MSG_INVALID_ARG,
                         "invalid argument");
        return;
    }

    pthread_rwlock_wrlock(&fdtable->lock);
    {
        for (i = 0; i < fdtable->max_fds; i++) {
            if (fdtable->fdentries[i].fd == fd) {
                fde = &fdtable->fdentries[i];
                break;
            }
        }

        if (fde == NULL) {
            gf_msg_callingfn("fd", GF_LOG_WARNING, 0,
                             LG_MSG_FD_NOT_FOUND_IN_FDTABLE,
                             "fd (%p) is not present in fdtable", fd);
            goto unlock_out;
        }

        /* If the entry is not allocated, put operation must return
         * without doing anything.
         * This has the potential of masking out any bugs in a user of
         * fd that ends up calling gf_fd_put twice for the same fd or
         * for an unallocated fd, but it is a price we have to pay for
         * ensuring sanity of our fd-table.
         */
        if (fde->next_free != GF_FDENTRY_ALLOCATED)
            goto unlock_out;
        fde->fd = NULL;
        fde->next_free = fdtable->first_free;
        fdtable->first_free = i;
    }
unlock_out:
    pthread_rwlock_unlock(&fdtable->lock);

    if ((fd != NULL) && (fde != NULL)) {
        fd_unref(fd);
    }
}

fd_t *
gf_fd_fdptr_get(fdtable_t *fdtable, int64_t fd)
{
    fd_t *fdptr = NULL;

    if (fdtable == NULL || fd < 0) {
        gf_msg_callingfn("fd", GF_LOG_ERROR, EINVAL, LG_MSG_INVALID_ARG,
                         "invalid argument");
        errno = EINVAL;
        return NULL;
    }

    if (!(fd < fdtable->max_fds)) {
        gf_msg_callingfn("fd", GF_LOG_ERROR, EINVAL, LG_MSG_INVALID_ARG,
                         "invalid argument");
        errno = EINVAL;
        return NULL;
    }

    pthread_rwlock_rdlock(&fdtable->lock);
    {
        fdptr = fdtable->fdentries[fd].fd;
        if (fdptr) {
            fd_ref(fdptr);
        }
    }
    pthread_rwlock_unlock(&fdtable->lock);

    return fdptr;
}

fd_t *
__fd_ref(fd_t *fd)
{
    GF_ATOMIC_INC(fd->refcount);

    return fd;
}

fd_t *
fd_ref(fd_t *fd)
{
    if (!fd) {
        gf_msg_callingfn("fd", GF_LOG_ERROR, EINVAL, LG_MSG_INVALID_ARG,
                         "null fd");
        return NULL;
    }

    GF_ATOMIC_INC(fd->refcount);

    return fd;
}

static void
fd_destroy(fd_t *fd, gf_boolean_t bound)
{
    xlator_t *xl = NULL;
    int i = 0;
    xlator_t *old_THIS = NULL;

    if (fd == NULL) {
        gf_msg_callingfn("xlator", GF_LOG_ERROR, EINVAL, LG_MSG_INVALID_ARG,
                         "invalid argument");
        goto out;
    }

    if (fd->inode == NULL) {
        gf_msg_callingfn("xlator", GF_LOG_ERROR, 0, LG_MSG_FD_INODE_NULL,
                         "fd->inode is NULL");
        goto out;
    }
    if (!fd->_ctx)
        goto out;

    if (IA_ISDIR(fd->inode->ia_type)) {
        for (i = 0; i < fd->xl_count; i++) {
            if (fd->_ctx[i].key) {
                xl = fd->_ctx[i].xl_key;
                old_THIS = THIS;
                THIS = xl;
                if (!xl->call_cleanup && xl->cbks->releasedir)
                    xl->cbks->releasedir(xl, fd);
                THIS = old_THIS;
            }
        }
    } else {
        for (i = 0; i < fd->xl_count; i++) {
            if (fd->_ctx[i].key) {
                xl = fd->_ctx[i].xl_key;
                old_THIS = THIS;
                THIS = xl;
                if (!xl->call_cleanup && xl->cbks->release)
                    xl->cbks->release(xl, fd);
                THIS = old_THIS;
            }
        }
    }

    LOCK_DESTROY(&fd->lock);

    GF_FREE(fd->_ctx);
    if (bound) {
        /*Decrease the count only after close happens on file*/
        LOCK(&fd->inode->lock);
        {
            fd->inode->fd_count--;
        }
        UNLOCK(&fd->inode->lock);
    }
    inode_unref(fd->inode);
    fd->inode = NULL;
    fd_lk_ctx_unref(fd->lk_ctx);
    mem_put(fd);
out:
    return;
}

void
fd_close(fd_t *fd)
{
    xlator_t *xl, *old_THIS;

    old_THIS = THIS;

    for (xl = fd->inode->table->xl->graph->first; xl != NULL; xl = xl->next) {
        if (!xl->call_cleanup) {
            THIS = xl;

            if (IA_ISDIR(fd->inode->ia_type)) {
                if (xl->cbks->fdclosedir != NULL) {
                    xl->cbks->fdclosedir(xl, fd);
                }
            } else {
                if (xl->cbks->fdclose != NULL) {
                    xl->cbks->fdclose(xl, fd);
                }
            }
        }
    }

    THIS = old_THIS;
}

void
fd_unref(fd_t *fd)
{
    int32_t refcount = 0;
    gf_boolean_t bound = _gf_false;

    if (!fd) {
        gf_msg_callingfn("fd", GF_LOG_ERROR, EINVAL, LG_MSG_INVALID_ARG,
                         "fd is NULL");
        return;
    }

    LOCK(&fd->inode->lock);
    {
        refcount = GF_ATOMIC_DEC(fd->refcount);
        if (refcount == 0) {
            if (!list_empty(&fd->inode_list)) {
                list_del_init(&fd->inode_list);
                fd->inode->active_fd_count--;
                bound = _gf_true;
            }
        }
    }
    UNLOCK(&fd->inode->lock);

    if (refcount == 0) {
        fd_destroy(fd, bound);
    }

    return;
}

static fd_t *
__fd_bind(fd_t *fd)
{
    list_del_init(&fd->inode_list);
    list_add(&fd->inode_list, &fd->inode->fd_list);
    fd->inode->fd_count++;
    fd->inode->active_fd_count++;

    return fd;
}

fd_t *
fd_bind(fd_t *fd)
{
    if (!fd || !fd->inode) {
        gf_msg_callingfn("fd", GF_LOG_ERROR, EINVAL, LG_MSG_INVALID_ARG,
                         "!fd || !fd->inode");
        return NULL;
    }

    LOCK(&fd->inode->lock);
    {
        fd = __fd_bind(fd);
    }
    UNLOCK(&fd->inode->lock);

    return fd;
}

static fd_t *
fd_allocate(inode_t *inode, uint64_t pid)
{
    fd_t *fd;

    if (inode == NULL) {
        gf_msg_callingfn("fd", GF_LOG_ERROR, EINVAL, LG_MSG_INVALID_ARG,
                         "invalid argument");
        return NULL;
    }

    fd = mem_get0(inode->table->fd_mem_pool);
    if (fd == NULL) {
        return NULL;
    }

    fd->xl_count = inode->table->xl->graph->xl_count + 1;

    fd->_ctx = GF_CALLOC(1, (sizeof(struct _fd_ctx) * fd->xl_count),
                         gf_common_mt_fd_ctx);
    if (fd->_ctx == NULL) {
        goto failed;
    }

    fd->lk_ctx = fd_lk_ctx_create();
    if (fd->lk_ctx != NULL) {
        /* We need to take a reference from the inode, but we cannot do it
         * here because this function can be called with the inode lock taken
         * and inode_ref() takes the inode's table lock. This is the reverse
         * of the logical lock acquisition order and can cause a deadlock. So
         * we simply assign the inode here and we delefate the inode reference
         * responsibility to the caller (when this function succeeds and the
         * inode lock is released). This is safe because the caller must hold
         * a reference of the inode to use it, so it's guaranteed that the
         * number of references won't reach 0 before the caller finishes.
         *
         * TODO: minimize use of locks in favor of atomic operations to avoid
         *       these dependencies. */
        fd->inode = inode;
        fd->pid = pid;
        INIT_LIST_HEAD(&fd->inode_list);
        LOCK_INIT(&fd->lock);
        GF_ATOMIC_INIT(fd->refcount, 1);
        return fd;
    }

    GF_FREE(fd->_ctx);

failed:
    mem_put(fd);

    return NULL;
}

fd_t *
fd_create_uint64(inode_t *inode, uint64_t pid)
{
    fd_t *fd;

    fd = fd_allocate(inode, pid);
    if (fd != NULL) {
        /* fd_allocate() doesn't get a reference from the inode. We need to
         * take it here in case of success. */
        inode_ref(inode);
    }

    return fd;
}

fd_t *
fd_create(inode_t *inode, pid_t pid)
{
    return fd_create_uint64(inode, (uint64_t)pid);
}

static fd_t *
__fd_lookup(inode_t *inode, uint64_t pid)
{
    fd_t *iter_fd = NULL;
    fd_t *fd = NULL;

    if (list_empty(&inode->fd_list))
        return NULL;

    list_for_each_entry(iter_fd, &inode->fd_list, inode_list)
    {
        if (iter_fd->anonymous)
            /* If someone was interested in getting an
               anonymous fd (or was OK getting an anonymous fd),
               they can as well call fd_anonymous() directly */
            continue;

        if (!pid || iter_fd->pid == pid) {
            fd = __fd_ref(iter_fd);
            break;
        }
    }

    return fd;
}

fd_t *
fd_lookup(inode_t *inode, pid_t pid)
{
    fd_t *fd = NULL;

    if (!inode) {
        gf_msg_callingfn("fd", GF_LOG_WARNING, EINVAL, LG_MSG_INVALID_ARG,
                         "!inode");
        return NULL;
    }

    LOCK(&inode->lock);
    {
        fd = __fd_lookup(inode, (uint64_t)pid);
    }
    UNLOCK(&inode->lock);

    return fd;
}

fd_t *
fd_lookup_uint64(inode_t *inode, uint64_t pid)
{
    fd_t *fd = NULL;

    if (!inode) {
        gf_msg_callingfn("fd", GF_LOG_WARNING, EINVAL, LG_MSG_INVALID_ARG,
                         "!inode");
        return NULL;
    }

    LOCK(&inode->lock);
    {
        fd = __fd_lookup(inode, pid);
    }
    UNLOCK(&inode->lock);

    return fd;
}

static fd_t *
__fd_lookup_anonymous(inode_t *inode, int32_t flags)
{
    fd_t *iter_fd = NULL;
    fd_t *fd = NULL;

    if (list_empty(&inode->fd_list))
        return NULL;

    list_for_each_entry(iter_fd, &inode->fd_list, inode_list)
    {
        if ((iter_fd->anonymous) && (flags == iter_fd->flags)) {
            fd = __fd_ref(iter_fd);
            break;
        }
    }

    return fd;
}

fd_t *
fd_anonymous_with_flags(inode_t *inode, int32_t flags)
{
    fd_t *fd = NULL;
    bool ref = false;

    LOCK(&inode->lock);

    fd = __fd_lookup_anonymous(inode, flags);

    /* if (fd); then we already have increased the refcount in
       __fd_lookup_anonymous(), so no need of one more fd_ref().
       if (!fd); then both create and bind won't bump up the ref
       count, so we have to call fd_ref() after bind. */
    if (fd == NULL) {
        fd = fd_allocate(inode, 0);
        if (fd != NULL) {
            fd->anonymous = _gf_true;
            fd->flags = GF_ANON_FD_FLAGS | (flags & O_DIRECT);

            __fd_bind(fd);

            ref = true;
        }
    }

    UNLOCK(&inode->lock);

    if (ref) {
        /* fd_allocate() doesn't get a reference from the inode. We need to
         * take it here in case of success. */
        inode_ref(inode);
    }

    return fd;
}

fd_t *
fd_anonymous(inode_t *inode)
{
    return fd_anonymous_with_flags(inode, 0);
}

fd_t *
fd_lookup_anonymous(inode_t *inode, int32_t flags)
{
    fd_t *fd = NULL;

    if (!inode) {
        gf_msg_callingfn("fd", GF_LOG_WARNING, EINVAL, LG_MSG_INVALID_ARG,
                         "!inode");
        return NULL;
    }

    LOCK(&inode->lock);
    {
        fd = __fd_lookup_anonymous(inode, flags);
    }
    UNLOCK(&inode->lock);
    return fd;
}

gf_boolean_t
fd_is_anonymous(fd_t *fd)
{
    return (fd && fd->anonymous);
}

uint8_t
fd_list_empty(inode_t *inode)
{
    uint8_t empty = 0;

    LOCK(&inode->lock);
    {
        empty = list_empty(&inode->fd_list);
    }
    UNLOCK(&inode->lock);

    return empty;
}

int
__fd_ctx_set(fd_t *fd, xlator_t *xlator, uint64_t value)
{
    int index = 0, new_xl_count = 0;
    int ret = 0;
    int set_idx = -1;
    void *begin = NULL;
    size_t diff = 0;
    struct _fd_ctx *tmp = NULL;

    if (!fd || !xlator)
        return -1;

    for (index = 0; index < fd->xl_count; index++) {
        if (!fd->_ctx[index].key) {
            if (set_idx == -1)
                set_idx = index;
            /* don't break, to check if key already exists
               further on */
        }
        if (fd->_ctx[index].xl_key == xlator) {
            set_idx = index;
            break;
        }
    }

    if (set_idx == -1) {
        set_idx = fd->xl_count;

        new_xl_count = fd->xl_count + xlator->graph->xl_count;

        tmp = GF_REALLOC(fd->_ctx, (sizeof(struct _fd_ctx) * new_xl_count));
        if (tmp == NULL) {
            ret = -1;
            goto out;
        }

        fd->_ctx = tmp;

        begin = fd->_ctx;
        begin += (fd->xl_count * sizeof(struct _fd_ctx));

        diff = (new_xl_count - fd->xl_count) * sizeof(struct _fd_ctx);

        memset(begin, 0, diff);

        fd->xl_count = new_xl_count;
    }

    fd->_ctx[set_idx].xl_key = xlator;
    fd->_ctx[set_idx].value1 = value;

out:
    return ret;
}

int
fd_ctx_set(fd_t *fd, xlator_t *xlator, uint64_t value)
{
    int ret = 0;

    if (!fd || !xlator) {
        gf_msg_callingfn("fd", GF_LOG_WARNING, EINVAL, LG_MSG_INVALID_ARG,
                         "%p %p", fd, xlator);
        return -1;
    }

    LOCK(&fd->lock);
    {
        ret = __fd_ctx_set(fd, xlator, value);
    }
    UNLOCK(&fd->lock);

    return ret;
}

int
__fd_ctx_get(fd_t *fd, xlator_t *xlator, uint64_t *value)
{
    int index = 0;
    int ret = 0;

    if (!fd || !xlator)
        return -1;

    for (index = 0; index < fd->xl_count; index++) {
        if (fd->_ctx[index].xl_key == xlator)
            break;
    }

    if (index == fd->xl_count) {
        ret = -1;
        goto out;
    }

    if (value)
        *value = fd->_ctx[index].value1;

out:
    return ret;
}

int
fd_ctx_get(fd_t *fd, xlator_t *xlator, uint64_t *value)
{
    int ret = 0;

    if (!fd || !xlator)
        return -1;

    LOCK(&fd->lock);
    {
        ret = __fd_ctx_get(fd, xlator, value);
    }
    UNLOCK(&fd->lock);

    return ret;
}

int
__fd_ctx_del(fd_t *fd, xlator_t *xlator, uint64_t *value)
{
    int index = 0;
    int ret = 0;

    if (!fd || !xlator)
        return -1;

    for (index = 0; index < fd->xl_count; index++) {
        if (fd->_ctx[index].xl_key == xlator)
            break;
    }

    if (index == fd->xl_count) {
        ret = -1;
        goto out;
    }

    if (value)
        *value = fd->_ctx[index].value1;

    fd->_ctx[index].key = 0;
    fd->_ctx[index].value1 = 0;

out:
    return ret;
}

int
fd_ctx_del(fd_t *fd, xlator_t *xlator, uint64_t *value)
{
    int ret = 0;

    if (!fd || !xlator)
        return -1;

    LOCK(&fd->lock);
    {
        ret = __fd_ctx_del(fd, xlator, value);
    }
    UNLOCK(&fd->lock);

    return ret;
}

void
fd_dump(fd_t *fd, char *prefix)
{
    char key[GF_DUMP_MAX_BUF_LEN];

    if (!fd)
        return;

    gf_proc_dump_write("pid", "%" PRIu64, fd->pid);
    gf_proc_dump_write("refcount", "%" GF_PRI_ATOMIC,
                       GF_ATOMIC_GET(fd->refcount));
    gf_proc_dump_write("flags", "%d", fd->flags);

    if (fd->inode) {
        gf_proc_dump_build_key(key, "inode", NULL);
        gf_proc_dump_add_section("%s", key);
        inode_dump(fd->inode, key);
    }
}

void
fdentry_dump(fdentry_t *fdentry, char *prefix)
{
    if (!fdentry)
        return;

    if (GF_FDENTRY_ALLOCATED != fdentry->next_free)
        return;

    if (fdentry->fd)
        fd_dump(fdentry->fd, prefix);
}

void
fdtable_dump(fdtable_t *fdtable, char *prefix)
{
    char key[GF_DUMP_MAX_BUF_LEN];
    int i = 0;
    int ret = -1;

    if (!fdtable)
        return;

    ret = pthread_rwlock_tryrdlock(&fdtable->lock);
    if (ret)
        goto out;

    gf_proc_dump_build_key(key, prefix, "refcount");
    gf_proc_dump_write(key, "%d", fdtable->refcount);
    gf_proc_dump_build_key(key, prefix, "maxfds");
    gf_proc_dump_write(key, "%d", fdtable->max_fds);
    gf_proc_dump_build_key(key, prefix, "first_free");
    gf_proc_dump_write(key, "%d", fdtable->first_free);

    for (i = 0; i < fdtable->max_fds; i++) {
        if (GF_FDENTRY_ALLOCATED == fdtable->fdentries[i].next_free) {
            gf_proc_dump_build_key(key, prefix, "fdentry[%d]", i);
            gf_proc_dump_add_section("%s", key);
            fdentry_dump(&fdtable->fdentries[i], key);
        }
    }

    pthread_rwlock_unlock(&fdtable->lock);

out:
    if (ret != 0)
        gf_proc_dump_write("Unable to dump the fdtable",
                           "(Lock acquistion failed) %p", fdtable);
    return;
}

void
fd_ctx_dump(fd_t *fd, char *prefix)
{
    struct _fd_ctx *fd_ctx = NULL;
    xlator_t *xl = NULL;
    int i = 0;

    if ((fd == NULL) || (fd->_ctx == NULL)) {
        goto out;
    }

    LOCK(&fd->lock);
    {
        if (fd->_ctx != NULL) {
            fd_ctx = GF_CALLOC(fd->xl_count, sizeof(*fd_ctx),
                               gf_common_mt_fd_ctx);
            if (fd_ctx == NULL) {
                goto unlock;
            }

            for (i = 0; i < fd->xl_count; i++) {
                fd_ctx[i] = fd->_ctx[i];
            }
        }
    }
unlock:
    UNLOCK(&fd->lock);

    if (fd_ctx == NULL) {
        goto out;
    }

    for (i = 0; i < fd->xl_count; i++) {
        if (fd_ctx[i].xl_key) {
            xl = (xlator_t *)(long)fd_ctx[i].xl_key;
            if (xl->dumpops && xl->dumpops->fdctx)
                xl->dumpops->fdctx(xl, fd);
        }
    }

out:
    GF_FREE(fd_ctx);

    return;
}

void
fdentry_dump_to_dict(fdentry_t *fdentry, char *prefix, dict_t *dict,
                     int *openfds)
{
    char key[GF_DUMP_MAX_BUF_LEN] = {
        0,
    };
    int ret = -1;

    if (!fdentry)
        return;
    if (!dict)
        return;

    if (GF_FDENTRY_ALLOCATED != fdentry->next_free)
        return;

    if (fdentry->fd) {
        snprintf(key, sizeof(key), "%s.pid", prefix);
        ret = dict_set_uint64(dict, key, fdentry->fd->pid);
        if (ret)
            return;

        snprintf(key, sizeof(key), "%s.refcount", prefix);
        ret = dict_set_int32(dict, key, GF_ATOMIC_GET(fdentry->fd->refcount));
        if (ret)
            return;

        snprintf(key, sizeof(key), "%s.flags", prefix);
        ret = dict_set_int32(dict, key, fdentry->fd->flags);
        if (ret)
            return;

        (*openfds)++;
    }
    return;
}

void
fdtable_dump_to_dict(fdtable_t *fdtable, char *prefix, dict_t *dict)
{
    char key[GF_DUMP_MAX_BUF_LEN] = {
        0,
    };
    int i = 0;
    int openfds = 0;
    int ret = -1;

    if (!fdtable)
        return;
    if (!dict)
        return;

    ret = pthread_rwlock_tryrdlock(&fdtable->lock);
    if (ret)
        return;

    snprintf(key, sizeof(key), "%s.fdtable.refcount", prefix);
    ret = dict_set_int32(dict, key, fdtable->refcount);
    if (ret)
        goto out;

    snprintf(key, sizeof(key), "%s.fdtable.maxfds", prefix);
    ret = dict_set_uint32(dict, key, fdtable->max_fds);
    if (ret)
        goto out;

    snprintf(key, sizeof(key), "%s.fdtable.firstfree", prefix);
    ret = dict_set_int32(dict, key, fdtable->first_free);
    if (ret)
        goto out;

    for (i = 0; i < fdtable->max_fds; i++) {
        if (GF_FDENTRY_ALLOCATED == fdtable->fdentries[i].next_free) {
            snprintf(key, sizeof(key), "%s.fdtable.fdentry%d", prefix, i);
            fdentry_dump_to_dict(&fdtable->fdentries[i], key, dict, &openfds);
        }
    }

    snprintf(key, sizeof(key), "%s.fdtable.openfds", prefix);
    ret = dict_set_int32(dict, key, openfds);
    if (ret)
        goto out;

out:
    pthread_rwlock_unlock(&fdtable->lock);
    return;
}
