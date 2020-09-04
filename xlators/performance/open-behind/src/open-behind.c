/*
  Copyright (c) 2013 Red Hat, Inc. <http://www.redhat.com>
  This file is part of GlusterFS.

  This file is licensed to you under your choice of the GNU Lesser
  General Public License, version 3 or any later version (LGPLv3 or
  later), or the GNU General Public License, version 2 (GPLv2), in all
  cases as published by the Free Software Foundation.
*/

#include "open-behind-mem-types.h"
#include <glusterfs/xlator.h>
#include <glusterfs/statedump.h>
#include <glusterfs/call-stub.h>
#include <glusterfs/defaults.h>
#include "open-behind-messages.h"
#include <glusterfs/glusterfs-acl.h>

/* Note: The initial design of open-behind was made to cover the simple case
 *       of open, read, close for small files. This pattern combined with
 *       quick-read can do the whole operation without a single request to the
 *       bricks (except the initial lookup).
 *
 *       The way to do this has been improved, but the logic remains the same.
 *       Basically, this means that any operation sent to the fd or the inode
 *       that it's not a read, causes the open request to be sent to the
 *       bricks, and all future operations will be executed synchronously,
 *       including opens (it's reset once all fd's are closed).
 */

typedef struct ob_conf {
    gf_boolean_t use_anonymous_fd; /* use anonymous FDs wherever safe
                                      e.g - fstat() readv()

                                      whereas for fops like writev(), lk(),
                                      the fd is important for side effects
                                      like mandatory locks
                                   */
    gf_boolean_t lazy_open;        /* delay backend open as much as possible */
    gf_boolean_t read_after_open;  /* instead of sending readvs on
                                           anonymous fds, open the file
                                           first and then send readv i.e
                                           similar to what writev does
                                        */
} ob_conf_t;

/* A negative state represents an errno value negated. In this case the
 * current operation cannot be processed. */
typedef enum _ob_state {
    /* There are no opens on the inode or the first open is already
     * completed. The current operation can be sent directly. */
    OB_STATE_READY = 0,

    /* There's an open pending and it has been triggered. The current
     * operation should be "stubbified" and processed with
     * ob_stub_dispatch(). */
    OB_STATE_OPEN_TRIGGERED,

    /* There's an open pending but it has not been triggered. The current
     * operation can be processed directly but using an anonymous fd. */
    OB_STATE_OPEN_PENDING,

    /* The current operation is the first open on the inode. */
    OB_STATE_FIRST_OPEN
} ob_state_t;

typedef struct ob_inode {
    /* List of stubs pending on the first open. Once the first open is
     * complete, all these stubs will be resubmitted, and dependencies
     * will be checked again. */
    struct list_head resume_fops;

    /* The inode this object references. */
    inode_t *inode;

    /* The fd from the first open sent to this inode. It will be set
     * from the moment the open is processed until the open if fully
     * executed or closed before actually opened. It's NULL in all
     * other cases. */
    fd_t *first_fd;

    /* The stub from the first open operation. When open fop starts
     * being processed, it's assigned the OB_OPEN_PREPARING value
     * until the actual stub is created. This is necessary to avoid
     * creating the stub inside a locked region. Once the stub is
     * successfully created, it's assigned here. This value is set
     * to NULL once the stub is resumed. */
    call_stub_t *first_open;

    /* The total number of currently open fd's on this inode. */
    int32_t open_count;

    /* This flag is set as soon as we know that the open will be
     * sent to the bricks, even before the stub is ready. */
    bool triggered;
} ob_inode_t;

/* Dummy pointer used temporarily while the actual open stub is being created */
#define OB_OPEN_PREPARING ((call_stub_t *)-1)

#define OB_POST_COMMON(_fop, _xl, _frame, _fd, _args...)                       \
    case OB_STATE_FIRST_OPEN:                                                  \
        gf_smsg((_xl)->name, GF_LOG_ERROR, EINVAL, OPEN_BEHIND_MSG_BAD_STATE,  \
                "fop=%s", #_fop, "state=%d", __ob_state, NULL);                \
        default_##_fop##_failure_cbk(_frame, EINVAL);                          \
        break;                                                                 \
    case OB_STATE_READY:                                                       \
        default_##_fop(_frame, _xl, ##_args);                                  \
        break;                                                                 \
    case OB_STATE_OPEN_TRIGGERED: {                                            \
        call_stub_t *__ob_stub = fop_##_fop##_stub(_frame, ob_##_fop,          \
                                                   ##_args);                   \
        if (__ob_stub != NULL) {                                               \
            ob_stub_dispatch(_xl, __ob_inode, _fd, __ob_stub);                 \
            break;                                                             \
        }                                                                      \
        __ob_state = -ENOMEM;                                                  \
    }                                                                          \
    default:                                                                   \
        gf_smsg((_xl)->name, GF_LOG_ERROR, -__ob_state,                        \
                OPEN_BEHIND_MSG_FAILED, "fop=%s", #_fop, NULL);                \
        default_##_fop##_failure_cbk(_frame, -__ob_state)

#define OB_POST_FD(_fop, _xl, _frame, _fd, _trigger, _args...)                 \
    do {                                                                       \
        ob_inode_t *__ob_inode;                                                \
        fd_t *__first_fd;                                                      \
        ob_state_t __ob_state = ob_open_and_resume_fd(                         \
            _xl, _fd, 0, true, _trigger, &__ob_inode, &__first_fd);            \
        switch (__ob_state) {                                                  \
            case OB_STATE_OPEN_PENDING:                                        \
                if (!(_trigger)) {                                             \
                    fd_t *__ob_fd = fd_anonymous_with_flags((_fd)->inode,      \
                                                            (_fd)->flags);     \
                    if (__ob_fd != NULL) {                                     \
                        default_##_fop(_frame, _xl, ##_args);                  \
                        fd_unref(__ob_fd);                                     \
                        break;                                                 \
                    }                                                          \
                    __ob_state = -ENOMEM;                                      \
                }                                                              \
                OB_POST_COMMON(_fop, _xl, _frame, __first_fd, ##_args);        \
        }                                                                      \
    } while (0)

#define OB_POST_FLUSH(_xl, _frame, _fd, _args...)                              \
    do {                                                                       \
        ob_inode_t *__ob_inode;                                                \
        fd_t *__first_fd;                                                      \
        ob_state_t __ob_state = ob_open_and_resume_fd(                         \
            _xl, _fd, 0, true, false, &__ob_inode, &__first_fd);               \
        switch (__ob_state) {                                                  \
            case OB_STATE_OPEN_PENDING:                                        \
                default_flush_cbk(_frame, NULL, _xl, 0, 0, NULL);              \
                break;                                                         \
                OB_POST_COMMON(flush, _xl, _frame, __first_fd, ##_args);       \
        }                                                                      \
    } while (0)

#define OB_POST_INODE(_fop, _xl, _frame, _inode, _trigger, _args...)           \
    do {                                                                       \
        ob_inode_t *__ob_inode;                                                \
        fd_t *__first_fd;                                                      \
        ob_state_t __ob_state = ob_open_and_resume_inode(                      \
            _xl, _inode, NULL, 0, true, _trigger, &__ob_inode, &__first_fd);   \
        switch (__ob_state) {                                                  \
            case OB_STATE_OPEN_PENDING:                                        \
                OB_POST_COMMON(_fop, _xl, _frame, __first_fd, ##_args);        \
        }                                                                      \
    } while (0)

static ob_inode_t *
ob_inode_get_locked(xlator_t *this, inode_t *inode)
{
    ob_inode_t *ob_inode = NULL;
    uint64_t value = 0;

    if ((__inode_ctx_get(inode, this, &value) == 0) && (value != 0)) {
        return (ob_inode_t *)(uintptr_t)value;
    }

    ob_inode = GF_CALLOC(1, sizeof(*ob_inode), gf_ob_mt_inode_t);
    if (ob_inode != NULL) {
        ob_inode->inode = inode;
        INIT_LIST_HEAD(&ob_inode->resume_fops);

        value = (uint64_t)(uintptr_t)ob_inode;
        if (__inode_ctx_set(inode, this, &value) < 0) {
            GF_FREE(ob_inode);
            ob_inode = NULL;
        }
    }

    return ob_inode;
}

static ob_state_t
ob_open_and_resume_inode(xlator_t *xl, inode_t *inode, fd_t *fd,
                         int32_t open_count, bool synchronous, bool trigger,
                         ob_inode_t **pob_inode, fd_t **pfd)
{
    ob_conf_t *conf;
    ob_inode_t *ob_inode;
    call_stub_t *open_stub;

    if (inode == NULL) {
        return OB_STATE_READY;
    }

    conf = xl->private;

    *pfd = NULL;

    LOCK(&inode->lock);
    {
        ob_inode = ob_inode_get_locked(xl, inode);
        if (ob_inode == NULL) {
            UNLOCK(&inode->lock);

            return -ENOMEM;
        }
        *pob_inode = ob_inode;

        ob_inode->open_count += open_count;

        /* If first_fd is not NULL, it means that there's a previous open not
         * yet completed. */
        if (ob_inode->first_fd != NULL) {
            *pfd = ob_inode->first_fd;
            /* If the current request doesn't trigger the open and it hasn't
             * been triggered yet, we can continue without issuing the open
             * only if the current request belongs to the same fd as the
             * first one. */
            if (!trigger && !ob_inode->triggered &&
                (ob_inode->first_fd == fd)) {
                UNLOCK(&inode->lock);

                return OB_STATE_OPEN_PENDING;
            }

            /* We need to issue the open. It could have already been triggered
             * before. In this case open_stub will be NULL. Or the initial open
             * may not be completely ready yet. In this case open_stub will be
             * OB_OPEN_PREPARING. */
            open_stub = ob_inode->first_open;
            ob_inode->first_open = NULL;
            ob_inode->triggered = true;

            UNLOCK(&inode->lock);

            if ((open_stub != NULL) && (open_stub != OB_OPEN_PREPARING)) {
                call_resume(open_stub);
            }

            return OB_STATE_OPEN_TRIGGERED;
        }

        /* There's no pending open. Only opens can be non synchronous, so all
         * regular fops will be processed directly. For non synchronous opens,
         * we'll still process them normally (i.e. synchornous) if there are
         * more file descriptors open. */
        if (synchronous || (ob_inode->open_count > open_count)) {
            UNLOCK(&inode->lock);

            return OB_STATE_READY;
        }

        *pfd = fd;

        /* This is the first open. We keep a reference on the fd and set
         * first_open stub to OB_OPEN_PREPARING until the actual stub can
         * be assigned (we don't create the stub here to avoid doing memory
         * allocations inside the mutex). */
        ob_inode->first_fd = __fd_ref(fd);
        ob_inode->first_open = OB_OPEN_PREPARING;

        /* If lazy_open is not set, we'll need to immediately send the open,
         * so we set triggered right now. */
        ob_inode->triggered = !conf->lazy_open;
    }
    UNLOCK(&inode->lock);

    return OB_STATE_FIRST_OPEN;
}

static ob_state_t
ob_open_and_resume_fd(xlator_t *xl, fd_t *fd, int32_t open_count,
                      bool synchronous, bool trigger, ob_inode_t **pob_inode,
                      fd_t **pfd)
{
    uint64_t err;

    if ((fd_ctx_get(fd, xl, &err) == 0) && (err != 0)) {
        return (ob_state_t)-err;
    }

    return ob_open_and_resume_inode(xl, fd->inode, fd, open_count, synchronous,
                                    trigger, pob_inode, pfd);
}

static ob_state_t
ob_open_behind(xlator_t *xl, fd_t *fd, int32_t flags, ob_inode_t **pob_inode,
               fd_t **pfd)
{
    bool synchronous;

    /* TODO: If O_CREAT, O_APPEND, O_WRONLY or O_DIRECT are specified, shouldn't
     *       we also execute this open synchronously ? */
    synchronous = (flags & O_TRUNC) != 0;

    return ob_open_and_resume_fd(xl, fd, 1, synchronous, true, pob_inode, pfd);
}

static int32_t
ob_stub_dispatch(xlator_t *xl, ob_inode_t *ob_inode, fd_t *fd,
                 call_stub_t *stub)
{
    LOCK(&ob_inode->inode->lock);
    {
        /* We only queue a stub if the open has not been completed or
         * cancelled. */
        if (ob_inode->first_fd == fd) {
            list_add_tail(&stub->list, &ob_inode->resume_fops);
            stub = NULL;
        }
    }
    UNLOCK(&ob_inode->inode->lock);

    if (stub != NULL) {
        call_resume(stub);
    }

    return 0;
}

static void
ob_open_destroy(call_stub_t *stub, fd_t *fd)
{
    stub->frame->local = NULL;
    STACK_DESTROY(stub->frame->root);
    call_stub_destroy(stub);
    fd_unref(fd);
}

static int32_t
ob_open_dispatch(xlator_t *xl, ob_inode_t *ob_inode, fd_t *fd,
                 call_stub_t *stub)
{
    bool closed;

    LOCK(&ob_inode->inode->lock);
    {
        closed = ob_inode->first_fd != fd;
        if (!closed) {
            if (ob_inode->triggered) {
                ob_inode->first_open = NULL;
            } else {
                ob_inode->first_open = stub;
                stub = NULL;
            }
        }
    }
    UNLOCK(&ob_inode->inode->lock);

    if (stub != NULL) {
        if (closed) {
            ob_open_destroy(stub, fd);
        } else {
            call_resume(stub);
        }
    }

    return 0;
}

static void
ob_resume_pending(struct list_head *list)
{
    call_stub_t *stub;

    while (!list_empty(list)) {
        stub = list_first_entry(list, call_stub_t, list);
        list_del_init(&stub->list);

        call_resume(stub);
    }
}

static void
ob_open_completed(xlator_t *xl, ob_inode_t *ob_inode, fd_t *fd, int32_t op_ret,
                  int32_t op_errno)
{
    struct list_head list;

    INIT_LIST_HEAD(&list);

    if (op_ret < 0) {
        fd_ctx_set(fd, xl, op_errno <= 0 ? EIO : op_errno);
    }

    LOCK(&ob_inode->inode->lock);
    {
        /* Only update the fields if the file has not been closed before
         * getting here. */
        if (ob_inode->first_fd == fd) {
            list_splice_init(&ob_inode->resume_fops, &list);
            ob_inode->first_fd = NULL;
            ob_inode->first_open = NULL;
            ob_inode->triggered = false;
        }
    }
    UNLOCK(&ob_inode->inode->lock);

    ob_resume_pending(&list);

    fd_unref(fd);
}

static int32_t
ob_open_cbk(call_frame_t *frame, void *cookie, xlator_t *xl, int32_t op_ret,
            int32_t op_errno, fd_t *fd, dict_t *xdata)
{
    ob_inode_t *ob_inode;

    ob_inode = frame->local;
    frame->local = NULL;

    ob_open_completed(xl, ob_inode, cookie, op_ret, op_errno);

    STACK_DESTROY(frame->root);

    return 0;
}

static int32_t
ob_open_resume(call_frame_t *frame, xlator_t *this, loc_t *loc, int flags,
               fd_t *fd, dict_t *xdata)
{
    STACK_WIND_COOKIE(frame, ob_open_cbk, fd, FIRST_CHILD(this),
                      FIRST_CHILD(this)->fops->open, loc, flags, fd, xdata);

    return 0;
}

static int32_t
ob_open(call_frame_t *frame, xlator_t *this, loc_t *loc, int flags, fd_t *fd,
        dict_t *xdata)
{
    ob_inode_t *ob_inode;
    call_frame_t *open_frame;
    call_stub_t *stub;
    fd_t *first_fd;
    ob_state_t state;

    state = ob_open_behind(this, fd, flags, &ob_inode, &first_fd);
    if (state == OB_STATE_READY) {
        /* There's no pending open, but there are other file descriptors opened
         * or the current flags require a synchronous open. */
        return default_open(frame, this, loc, flags, fd, xdata);
    }

    if (state == OB_STATE_OPEN_TRIGGERED) {
        /* The first open is in progress (either because it was already issued
         * or because this request triggered it). We try to create a new stub
         * to retry the operation once the initial open completes. */
        stub = fop_open_stub(frame, ob_open, loc, flags, fd, xdata);
        if (stub != NULL) {
            return ob_stub_dispatch(this, ob_inode, first_fd, stub);
        }

        state = -ENOMEM;
    }

    if (state == OB_STATE_FIRST_OPEN) {
        /* We try to create a stub for the new open. A new frame needs to be
         * used because the current one may be destroyed soon after sending
         * the open's reply. */
        open_frame = copy_frame(frame);
        if (open_frame != NULL) {
            stub = fop_open_stub(open_frame, ob_open_resume, loc, flags, fd,
                                 xdata);
            if (stub != NULL) {
                open_frame->local = ob_inode;

                /* TODO: Previous version passed xdata back to the caller, but
                 *       probably this doesn't make sense since it won't contain
                 *       any requested data. I think it would be better to pass
                 *       NULL for xdata. */
                default_open_cbk(frame, NULL, this, 0, 0, fd, xdata);

                return ob_open_dispatch(this, ob_inode, first_fd, stub);
            }

            STACK_DESTROY(open_frame->root);
        }

        /* In case of error, simulate a regular completion but with an error
         * code. */
        ob_open_completed(this, ob_inode, first_fd, -1, ENOMEM);

        state = -ENOMEM;
    }

    /* In case of failure we need to decrement the number of open files because
     * ob_fdclose() won't be called. */

    LOCK(&fd->inode->lock);
    {
        ob_inode->open_count--;
    }
    UNLOCK(&fd->inode->lock);

    gf_smsg(this->name, GF_LOG_ERROR, -state, OPEN_BEHIND_MSG_FAILED, "fop=%s",
            "open", "path=%s", loc->path, NULL);

    return default_open_failure_cbk(frame, -state);
}

static int32_t
ob_create(call_frame_t *frame, xlator_t *this, loc_t *loc, int flags,
          mode_t mode, mode_t umask, fd_t *fd, dict_t *xdata)
{
    ob_inode_t *ob_inode;
    call_stub_t *stub;
    fd_t *first_fd;
    ob_state_t state;

    /* Create requests are never delayed. We always send them synchronously. */
    state = ob_open_and_resume_fd(this, fd, 1, true, true, &ob_inode,
                                  &first_fd);
    if (state == OB_STATE_READY) {
        /* There's no pending open, but there are other file descriptors opened
         * so we simply forward the request synchronously. */
        return default_create(frame, this, loc, flags, mode, umask, fd, xdata);
    }

    if (state == OB_STATE_OPEN_TRIGGERED) {
        /* The first open is in progress (either because it was already issued
         * or because this request triggered it). We try to create a new stub
         * to retry the operation once the initial open completes. */
        stub = fop_create_stub(frame, ob_create, loc, flags, mode, umask, fd,
                               xdata);
        if (stub != NULL) {
            return ob_stub_dispatch(this, ob_inode, first_fd, stub);
        }

        state = -ENOMEM;
    }

    /* Since we forced a synchronous request, OB_STATE_FIRST_OPEN will never
     * be returned by ob_open_and_resume_fd(). If we are here it can only be
     * because there has been a problem. */

    /* In case of failure we need to decrement the number of open files because
     * ob_fdclose() won't be called. */

    LOCK(&fd->inode->lock);
    {
        ob_inode->open_count--;
    }
    UNLOCK(&fd->inode->lock);

    gf_smsg(this->name, GF_LOG_ERROR, -state, OPEN_BEHIND_MSG_FAILED, "fop=%s",
            "create", "path=%s", loc->path, NULL);

    return default_create_failure_cbk(frame, -state);
}

static int32_t
ob_readv(call_frame_t *frame, xlator_t *this, fd_t *fd, size_t size,
         off_t offset, uint32_t flags, dict_t *xdata)
{
    ob_conf_t *conf = this->private;
    bool trigger = conf->read_after_open || !conf->use_anonymous_fd;

    OB_POST_FD(readv, this, frame, fd, trigger, fd, size, offset, flags, xdata);

    return 0;
}

static int32_t
ob_writev(call_frame_t *frame, xlator_t *this, fd_t *fd, struct iovec *iov,
          int count, off_t offset, uint32_t flags, struct iobref *iobref,
          dict_t *xdata)
{
    OB_POST_FD(writev, this, frame, fd, true, fd, iov, count, offset, flags,
               iobref, xdata);

    return 0;
}

static int32_t
ob_fstat(call_frame_t *frame, xlator_t *this, fd_t *fd, dict_t *xdata)
{
    ob_conf_t *conf = this->private;
    bool trigger = !conf->use_anonymous_fd;

    OB_POST_FD(fstat, this, frame, fd, trigger, fd, xdata);

    return 0;
}

static int32_t
ob_seek(call_frame_t *frame, xlator_t *this, fd_t *fd, off_t offset,
        gf_seek_what_t what, dict_t *xdata)
{
    ob_conf_t *conf = this->private;
    bool trigger = !conf->use_anonymous_fd;

    OB_POST_FD(seek, this, frame, fd, trigger, fd, offset, what, xdata);

    return 0;
}

static int32_t
ob_flush(call_frame_t *frame, xlator_t *this, fd_t *fd, dict_t *xdata)
{
    OB_POST_FLUSH(this, frame, fd, fd, xdata);

    return 0;
}

static int32_t
ob_fsync(call_frame_t *frame, xlator_t *this, fd_t *fd, int flag, dict_t *xdata)
{
    OB_POST_FD(fsync, this, frame, fd, true, fd, flag, xdata);

    return 0;
}

static int32_t
ob_lk(call_frame_t *frame, xlator_t *this, fd_t *fd, int cmd,
      struct gf_flock *flock, dict_t *xdata)
{
    OB_POST_FD(lk, this, frame, fd, true, fd, cmd, flock, xdata);

    return 0;
}

static int32_t
ob_ftruncate(call_frame_t *frame, xlator_t *this, fd_t *fd, off_t offset,
             dict_t *xdata)
{
    OB_POST_FD(ftruncate, this, frame, fd, true, fd, offset, xdata);

    return 0;
}

static int32_t
ob_fsetxattr(call_frame_t *frame, xlator_t *this, fd_t *fd, dict_t *xattr,
             int flags, dict_t *xdata)
{
    OB_POST_FD(fsetxattr, this, frame, fd, true, fd, xattr, flags, xdata);

    return 0;
}

static int32_t
ob_fgetxattr(call_frame_t *frame, xlator_t *this, fd_t *fd, const char *name,
             dict_t *xdata)
{
    OB_POST_FD(fgetxattr, this, frame, fd, true, fd, name, xdata);

    return 0;
}

static int32_t
ob_fremovexattr(call_frame_t *frame, xlator_t *this, fd_t *fd, const char *name,
                dict_t *xdata)
{
    OB_POST_FD(fremovexattr, this, frame, fd, true, fd, name, xdata);

    return 0;
}

static int32_t
ob_finodelk(call_frame_t *frame, xlator_t *this, const char *volume, fd_t *fd,
            int cmd, struct gf_flock *flock, dict_t *xdata)
{
    OB_POST_FD(finodelk, this, frame, fd, true, volume, fd, cmd, flock, xdata);

    return 0;
}

static int32_t
ob_fentrylk(call_frame_t *frame, xlator_t *this, const char *volume, fd_t *fd,
            const char *basename, entrylk_cmd cmd, entrylk_type type,
            dict_t *xdata)
{
    OB_POST_FD(fentrylk, this, frame, fd, true, volume, fd, basename, cmd, type,
               xdata);

    return 0;
}

static int32_t
ob_fxattrop(call_frame_t *frame, xlator_t *this, fd_t *fd,
            gf_xattrop_flags_t optype, dict_t *xattr, dict_t *xdata)
{
    OB_POST_FD(fxattrop, this, frame, fd, true, fd, optype, xattr, xdata);

    return 0;
}

static int32_t
ob_fsetattr(call_frame_t *frame, xlator_t *this, fd_t *fd, struct iatt *iatt,
            int valid, dict_t *xdata)
{
    OB_POST_FD(fsetattr, this, frame, fd, true, fd, iatt, valid, xdata);

    return 0;
}

static int32_t
ob_fallocate(call_frame_t *frame, xlator_t *this, fd_t *fd, int32_t mode,
             off_t offset, size_t len, dict_t *xdata)
{
    OB_POST_FD(fallocate, this, frame, fd, true, fd, mode, offset, len, xdata);

    return 0;
}

static int32_t
ob_discard(call_frame_t *frame, xlator_t *this, fd_t *fd, off_t offset,
           size_t len, dict_t *xdata)
{
    OB_POST_FD(discard, this, frame, fd, true, fd, offset, len, xdata);

    return 0;
}

static int32_t
ob_zerofill(call_frame_t *frame, xlator_t *this, fd_t *fd, off_t offset,
            off_t len, dict_t *xdata)
{
    OB_POST_FD(zerofill, this, frame, fd, true, fd, offset, len, xdata);

    return 0;
}

static int32_t
ob_unlink(call_frame_t *frame, xlator_t *this, loc_t *loc, int xflags,
          dict_t *xdata)
{
    OB_POST_INODE(unlink, this, frame, loc->inode, true, loc, xflags, xdata);

    return 0;
}

static int32_t
ob_rename(call_frame_t *frame, xlator_t *this, loc_t *src, loc_t *dst,
          dict_t *xdata)
{
    OB_POST_INODE(rename, this, frame, dst->inode, true, src, dst, xdata);

    return 0;
}

static int32_t
ob_setattr(call_frame_t *frame, xlator_t *this, loc_t *loc, struct iatt *stbuf,
           int32_t valid, dict_t *xdata)
{
    OB_POST_INODE(setattr, this, frame, loc->inode, true, loc, stbuf, valid,
                  xdata);

    return 0;
}

static int32_t
ob_setxattr(call_frame_t *frame, xlator_t *this, loc_t *loc, dict_t *dict,
            int32_t flags, dict_t *xdata)
{
    if (dict_get(dict, POSIX_ACL_DEFAULT_XATTR) ||
        dict_get(dict, POSIX_ACL_ACCESS_XATTR) ||
        dict_get(dict, GF_SELINUX_XATTR_KEY)) {
        return default_setxattr(frame, this, loc, dict, flags, xdata);
    }

    OB_POST_INODE(setxattr, this, frame, loc->inode, true, loc, dict, flags,
                  xdata);

    return 0;
}

static void
ob_fdclose(xlator_t *this, fd_t *fd)
{
    struct list_head list;
    ob_inode_t *ob_inode;
    call_stub_t *stub;

    INIT_LIST_HEAD(&list);
    stub = NULL;

    LOCK(&fd->inode->lock);
    {
        ob_inode = ob_inode_get_locked(this, fd->inode);
        if (ob_inode != NULL) {
            ob_inode->open_count--;

            /* If this fd is the same as ob_inode->first_fd, it means that
             * the initial open has not fully completed. We'll try to cancel
             * it. */
            if (ob_inode->first_fd == fd) {
                if (ob_inode->first_open == OB_OPEN_PREPARING) {
                    /* In this case ob_open_dispatch() has not been called yet.
                     * We clear first_fd and first_open to allow that function
                     * to know that the open is not really needed. This also
                     * allows other requests to work as expected if they
                     * arrive before the dispatch function is called. If there
                     * are pending fops, we can directly process them here.
                     * (note that there shouldn't be any fd related fops, but
                     * if there are, it's fine if they fail). */
                    ob_inode->first_fd = NULL;
                    ob_inode->first_open = NULL;
                    ob_inode->triggered = false;
                    list_splice_init(&ob_inode->resume_fops, &list);
                } else if (!ob_inode->triggered) {
                    /* If the open has already been dispatched, we can only
                     * cancel it if it has not been triggered. Otherwise we
                     * simply wait until it completes. While it's not triggered,
                     * first_open must be a valid stub and there can't be any
                     * pending fops. */
                    GF_ASSERT((ob_inode->first_open != NULL) &&
                              list_empty(&ob_inode->resume_fops));

                    ob_inode->first_fd = NULL;
                    stub = ob_inode->first_open;
                    ob_inode->first_open = NULL;
                }
            }
        }
    }
    UNLOCK(&fd->inode->lock);

    if (stub != NULL) {
        ob_open_destroy(stub, fd);
    }

    ob_resume_pending(&list);
}

int
ob_forget(xlator_t *this, inode_t *inode)
{
    ob_inode_t *ob_inode;
    uint64_t value = 0;

    if ((inode_ctx_del(inode, this, &value) == 0) && (value != 0)) {
        ob_inode = (ob_inode_t *)(uintptr_t)value;
        GF_FREE(ob_inode);
    }

    return 0;
}

int
ob_priv_dump(xlator_t *this)
{
    ob_conf_t *conf = NULL;
    char key_prefix[GF_DUMP_MAX_BUF_LEN];

    conf = this->private;

    if (!conf)
        return -1;

    gf_proc_dump_build_key(key_prefix, "xlator.performance.open-behind",
                           "priv");

    gf_proc_dump_add_section("%s", key_prefix);

    gf_proc_dump_write("use_anonymous_fd", "%d", conf->use_anonymous_fd);

    gf_proc_dump_write("lazy_open", "%d", conf->lazy_open);

    return 0;
}

int
ob_fdctx_dump(xlator_t *this, fd_t *fd)
{
    char key_prefix[GF_DUMP_MAX_BUF_LEN] = {
        0,
    };
    uint64_t value = 0;
    int ret = 0, error = 0;

    ret = TRY_LOCK(&fd->lock);
    if (ret)
        return 0;

    if ((__fd_ctx_get(fd, this, &value) == 0) && (value != 0)) {
        error = (int32_t)value;
    }

    gf_proc_dump_build_key(key_prefix, "xlator.performance.open-behind",
                           "file");
    gf_proc_dump_add_section("%s", key_prefix);

    gf_proc_dump_write("fd", "%p", fd);

    gf_proc_dump_write("error", "%d", error);

    UNLOCK(&fd->lock);

    return 0;
}

int
mem_acct_init(xlator_t *this)
{
    int ret = -1;

    ret = xlator_mem_acct_init(this, gf_ob_mt_end + 1);

    if (ret)
        gf_msg(this->name, GF_LOG_ERROR, ENOMEM, OPEN_BEHIND_MSG_NO_MEMORY,
               "Memory accounting failed");

    return ret;
}

int
reconfigure(xlator_t *this, dict_t *options)
{
    ob_conf_t *conf = NULL;
    int ret = -1;

    conf = this->private;

    GF_OPTION_RECONF("use-anonymous-fd", conf->use_anonymous_fd, options, bool,
                     out);

    GF_OPTION_RECONF("lazy-open", conf->lazy_open, options, bool, out);

    GF_OPTION_RECONF("read-after-open", conf->read_after_open, options, bool,
                     out);

    GF_OPTION_RECONF("pass-through", this->pass_through, options, bool, out);
    ret = 0;
out:
    return ret;
}

int
init(xlator_t *this)
{
    ob_conf_t *conf = NULL;

    if (!this->children || this->children->next) {
        gf_msg(this->name, GF_LOG_ERROR, 0,
               OPEN_BEHIND_MSG_XLATOR_CHILD_MISCONFIGURED,
               "FATAL: volume (%s) not configured with exactly one "
               "child",
               this->name);
        return -1;
    }

    if (!this->parents)
        gf_msg(this->name, GF_LOG_WARNING, 0, OPEN_BEHIND_MSG_VOL_MISCONFIGURED,
               "dangling volume. check volfile ");

    conf = GF_CALLOC(1, sizeof(*conf), gf_ob_mt_conf_t);
    if (!conf)
        goto err;

    GF_OPTION_INIT("use-anonymous-fd", conf->use_anonymous_fd, bool, err);

    GF_OPTION_INIT("lazy-open", conf->lazy_open, bool, err);

    GF_OPTION_INIT("read-after-open", conf->read_after_open, bool, err);

    GF_OPTION_INIT("pass-through", this->pass_through, bool, err);

    this->private = conf;

    return 0;
err:
    if (conf)
        GF_FREE(conf);

    return -1;
}

void
fini(xlator_t *this)
{
    ob_conf_t *conf = NULL;

    conf = this->private;

    GF_FREE(conf);

    return;
}

struct xlator_fops fops = {
    .open = ob_open,
    .create = ob_create,
    .readv = ob_readv,
    .writev = ob_writev,
    .flush = ob_flush,
    .fsync = ob_fsync,
    .fstat = ob_fstat,
    .seek = ob_seek,
    .ftruncate = ob_ftruncate,
    .fsetxattr = ob_fsetxattr,
    .setxattr = ob_setxattr,
    .fgetxattr = ob_fgetxattr,
    .fremovexattr = ob_fremovexattr,
    .finodelk = ob_finodelk,
    .fentrylk = ob_fentrylk,
    .fxattrop = ob_fxattrop,
    .fsetattr = ob_fsetattr,
    .setattr = ob_setattr,
    .fallocate = ob_fallocate,
    .discard = ob_discard,
    .zerofill = ob_zerofill,
    .unlink = ob_unlink,
    .rename = ob_rename,
    .lk = ob_lk,
};

struct xlator_cbks cbks = {
    .fdclose = ob_fdclose,
    .forget = ob_forget,
};

struct xlator_dumpops dumpops = {
    .priv = ob_priv_dump,
    .fdctx = ob_fdctx_dump,
};

struct volume_options options[] = {
    {
        .key = {"open-behind"},
        .type = GF_OPTION_TYPE_BOOL,
        .default_value = "off",
        .description = "enable/disable open-behind",
        .op_version = {GD_OP_VERSION_6_0},
        .flags = OPT_FLAG_SETTABLE,
    },
    {
        .key = {"use-anonymous-fd"},
        .type = GF_OPTION_TYPE_BOOL,
        .default_value = "no",
        .description =
            "For read operations, use anonymous FD when "
            "original FD is open-behind and not yet opened in the backend.",
    },
    {
        .key = {"lazy-open"},
        .type = GF_OPTION_TYPE_BOOL,
        .default_value = "yes",
        .description =
            "Perform open in the backend only when a necessary "
            "FOP arrives (e.g writev on the FD, unlink of the file). When "
            "option "
            "is disabled, perform backend open right after unwinding open().",
        .op_version = {3},
        .flags = OPT_FLAG_SETTABLE | OPT_FLAG_CLIENT_OPT,
        .tags = {},
        /* option_validation_fn validate_fn; */
    },
    {
        .key = {"read-after-open"},
        .type = GF_OPTION_TYPE_BOOL,
        .default_value = "yes",
        .description = "read is sent only after actual open happens and real "
                       "fd is obtained, instead of doing on anonymous fd "
                       "(similar to write)",
        .op_version = {3},
        .flags = OPT_FLAG_SETTABLE | OPT_FLAG_CLIENT_OPT,
        .tags = {},
        /* option_validation_fn validate_fn; */
    },
    {.key = {"pass-through"},
     .type = GF_OPTION_TYPE_BOOL,
     .default_value = "false",
     .op_version = {GD_OP_VERSION_4_1_0},
     .flags = OPT_FLAG_SETTABLE | OPT_FLAG_DOC | OPT_FLAG_CLIENT_OPT,
     .tags = {"open-behind"},
     .description = "Enable/Disable open behind translator"},
    {.key = {NULL}}

};

xlator_api_t xlator_api = {
    .init = init,
    .fini = fini,
    .reconfigure = reconfigure,
    .mem_acct_init = mem_acct_init,
    .op_version = {1}, /* Present from the initial version */
    .dumpops = &dumpops,
    .fops = &fops,
    .cbks = &cbks,
    .options = options,
    .identifier = "open-behind",
    .category = GF_MAINTAINED,
};
