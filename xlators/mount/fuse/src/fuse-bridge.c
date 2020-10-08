/*
  Copyright (c) 2006-2012 Red Hat, Inc. <http://www.redhat.com>
  This file is part of GlusterFS.

  This file is licensed to you under your choice of the GNU Lesser
  General Public License, version 3 or any later version (LGPLv3 or
  later), or the GNU General Public License, version 2 (GPLv2), in all
  cases as published by the Free Software Foundation.
*/

#include <config.h>

#include <sys/wait.h>
#include "fuse-bridge.h"
#include <glusterfs/glusterfs.h>
#include <glusterfs/byte-order.h>
#include <glusterfs/compat-errno.h>
#include <glusterfs/glusterfs-acl.h>
#include <glusterfs/syscall.h>
#include <glusterfs/timespec.h>
#include <glusterfs/async.h>

#ifdef __NetBSD__
#undef open /* in perfuse.h, pulled from mount-gluster-compat.h */
#endif
typedef struct _fuse_async {
    struct iobuf *iobuf;
    fuse_in_header_t *finh;
    void *msg;
    gf_async_t async;
} fuse_async_t;

static int gf_fuse_xattr_enotsup_log;

void
fini(xlator_t *this_xl);

static int32_t
fuse_invalidate_inode(xlator_t *this, uint64_t fuse_ino);

/*
 * Send an invalidate notification up to fuse to purge the file from local
 * page cache.
 */

static int32_t
fuse_invalidate(xlator_t *this, inode_t *inode)
{
    fuse_private_t *priv = this->private;
    uint64_t nodeid;

    /*
     * NOTE: We only invalidate at the moment if fopen_keep_cache is
     * enabled because otherwise this is a departure from default
     * behavior. Specifically, the performance/write-behind xlator
     * causes unconditional invalidations on write requests.
     */
    if (!priv->fopen_keep_cache)
        return 0;

    nodeid = inode_to_fuse_nodeid(inode);
    gf_log(this->name, GF_LOG_DEBUG, "Invalidate inode id %" GF_PRI_INODE ".",
           nodeid);
    fuse_log_eh(this, "Sending invalidate inode id: %" GF_PRI_INODE " gfid: %s",
                nodeid, uuid_utoa(inode->gfid));
    fuse_invalidate_inode(this, nodeid);

    return 0;
}

static int32_t
fuse_forget_cbk(xlator_t *this, inode_t *inode)
{
    // Nothing to free in inode ctx, hence return.
    return 0;
}

fuse_fd_ctx_t *
__fuse_fd_ctx_check_n_create(xlator_t *this, fd_t *fd)
{
    uint64_t val = 0;
    int32_t ret = 0;
    fuse_fd_ctx_t *fd_ctx = NULL;

    ret = __fd_ctx_get(fd, this, &val);

    fd_ctx = (fuse_fd_ctx_t *)(unsigned long)val;

    if (fd_ctx == NULL) {
        fd_ctx = GF_CALLOC(1, sizeof(*fd_ctx), gf_fuse_mt_fd_ctx_t);
        if (!fd_ctx) {
            goto out;
        }
        ret = __fd_ctx_set(fd, this, (uint64_t)(unsigned long)fd_ctx);
        if (ret < 0) {
            gf_log("glusterfs-fuse", GF_LOG_DEBUG, "fd-ctx-set failed");
            GF_FREE(fd_ctx);
            fd_ctx = NULL;
        }
    }
out:
    return fd_ctx;
}

fuse_fd_ctx_t *
fuse_fd_ctx_check_n_create(xlator_t *this, fd_t *fd)
{
    fuse_fd_ctx_t *fd_ctx = NULL;

    if ((fd == NULL) || (this == NULL)) {
        goto out;
    }

    LOCK(&fd->lock);
    {
        fd_ctx = __fuse_fd_ctx_check_n_create(this, fd);
    }
    UNLOCK(&fd->lock);

out:
    return fd_ctx;
}

static void
fuse_fd_ctx_destroy(xlator_t *this, fd_t *fd)
{
    fd_t *activefd = NULL;
    uint64_t val = 0;
    int ret = 0;
    fuse_fd_ctx_t *fdctx = NULL;

    ret = fd_ctx_del(fd, this, &val);
    if (!ret) {
        fdctx = (fuse_fd_ctx_t *)(unsigned long)val;
        if (fdctx) {
            activefd = fdctx->activefd;
            if (activefd) {
                fd_unref(activefd);
            }

            GF_FREE(fdctx);
        }
    }
}

fuse_fd_ctx_t *
fuse_fd_ctx_get(xlator_t *this, fd_t *fd)
{
    fuse_fd_ctx_t *fdctx = NULL;
    uint64_t value = 0;
    int ret = 0;

    ret = fd_ctx_get(fd, this, &value);
    if (ret < 0) {
        goto out;
    }

    fdctx = (fuse_fd_ctx_t *)(unsigned long)value;

out:
    return fdctx;
}

struct fusedump_timespec {
    uint32_t len;
    uint64_t sec;
    uint32_t nsec;
} __attribute__((packed));

struct fusedump_signature {
    uint32_t len;
    char sig[8];
} __attribute__((packed));

static void
fusedump_gettime(struct fusedump_timespec *fts)
{
    struct timespec ts = {
        0,
    };

    timespec_now_realtime(&ts);

    fts->sec = ts.tv_sec;
    fts->nsec = ts.tv_nsec;
}

static void
fusedump_setup_meta(struct iovec *iovs, char *dir,
                    uint32_t *fusedump_item_count,
                    struct fusedump_timespec *fts,
                    struct fusedump_signature *fsig)
{
    char glustersig[8] = {'G', 'L', 'U', 'S', 'T', 'E', 'R', 0xF5};

    *fusedump_item_count = 3;
    fts->len = sizeof(*fts);
    fusedump_gettime(fts);
    fsig->len = sizeof(*fsig);
    memcpy(fsig->sig, glustersig, 8);

    iovs[0] = (struct iovec){dir, sizeof(*dir)};
    iovs[1] = (struct iovec){fusedump_item_count, sizeof(*fusedump_item_count)};
    iovs[2] = (struct iovec){fts, fts->len};
    iovs[3] = (struct iovec){fsig, fsig->len};
}

static int
check_and_dump_fuse_W(fuse_private_t *priv, struct iovec *iov_out, int count,
                      ssize_t res, errnomask_t errnomask)
{
    char w = 'W';
    struct iovec diov[4] = {
        {
            0,
        },
    };
    uint32_t fusedump_item_count = 3;
    struct fusedump_timespec fts = {
        0,
    };
    struct fusedump_signature fsig = {
        0,
    };
    struct fuse_out_header *fouh = NULL;

    if (res == -1) {
        const char *errdesc = NULL;
        gf_loglevel_t loglevel = GF_LOG_ERROR;
        gf_boolean_t errno_degraded = _gf_false;
        gf_boolean_t errno_promoted = _gf_false;

#define ACCOUNT_ERRNO(eno)                                                     \
    do {                                                                       \
        if (errno_degraded) {                                                  \
            pthread_mutex_lock(&priv->fusedev_errno_cnt_mutex);                \
            {                                                                  \
                if (!++priv->fusedev_errno_cnt[FUSEDEV_##eno])                 \
                    errno_promoted = _gf_true;                                 \
            }                                                                  \
            pthread_mutex_unlock(&priv->fusedev_errno_cnt_mutex);              \
        }                                                                      \
    } while (0)

        /* If caller masked the errno, then it
         * does not indicate an error at the application
         * level, so we degrade the log severity to DEBUG.
         */
        if (errnomask && errno < ERRNOMASK_MAX &&
            GET_ERRNO_MASK(errnomask, errno)) {
            loglevel = GF_LOG_DEBUG;
            errno_degraded = _gf_true;
        }

        switch (errno) {
            /* The listed errnos are FUSE status indicators,
             * not legit values according to POSIX (see write(3p)),
             * so resolving them according to the standard
             * POSIX interpretation would be misleading.
             */
            case ENOENT:
                errdesc = "ENOENT";
                ACCOUNT_ERRNO(ENOENT);
                break;
            case ENOTDIR:
                errdesc = "ENOTDIR";
                ACCOUNT_ERRNO(ENOTDIR);
                break;
            case ENODEV:
                errdesc = "ENODEV";
                ACCOUNT_ERRNO(ENODEV);
                break;
            case EPERM:
                errdesc = "EPERM";
                ACCOUNT_ERRNO(EPERM);
                break;
            case ENOMEM:
                errdesc = "ENOMEM";
                ACCOUNT_ERRNO(ENOMEM);
                break;
            case ENOTCONN:
                errdesc = "ENOTCONN";
                ACCOUNT_ERRNO(ENOTCONN);
                break;
            case ECONNREFUSED:
                errdesc = "ECONNREFUSED";
                ACCOUNT_ERRNO(ECONNREFUSED);
                break;
            case EOVERFLOW:
                errdesc = "EOVERFLOW";
                ACCOUNT_ERRNO(EOVERFLOW);
                break;
            case EBUSY:
                errdesc = "EBUSY";
                ACCOUNT_ERRNO(EBUSY);
                break;
            case ENOTEMPTY:
                errdesc = "ENOTEMPTY";
                ACCOUNT_ERRNO(ENOTEMPTY);
                break;
            default:
                errdesc = strerror(errno);
        }

        gf_log_callingfn("glusterfs-fuse", loglevel,
                         "writing to fuse device failed: %s", errdesc);
        if (errno_promoted)
            gf_log("glusterfs-fuse", GF_LOG_WARNING,
                   "writing to fuse device yielded %s %d times", errdesc,
                   UINT8_MAX + 1);
        return errno;

#undef ACCOUNT_ERRNO
    }

    fouh = iov_out[0].iov_base;
    if (res != fouh->len) {
        gf_log("glusterfs-fuse", GF_LOG_ERROR,
               "inconsistent write to fuse device: "
               "written %zd, expectd %d",
               res, fouh->len);
        return EINVAL;
    }

    if (priv->fuse_dump_fd == -1)
        return 0;

    fusedump_setup_meta(diov, &w, &fusedump_item_count, &fts, &fsig);

    pthread_mutex_lock(&priv->fuse_dump_mutex);
    res = sys_writev(priv->fuse_dump_fd, diov, sizeof(diov) / sizeof(diov[0]));
    if (res != -1)
        res = sys_writev(priv->fuse_dump_fd, iov_out, count);
    pthread_mutex_unlock(&priv->fuse_dump_mutex);

    if (res == -1)
        gf_log("glusterfs-fuse", GF_LOG_ERROR,
               "failed to dump fuse message (W): %s", strerror(errno));

    /*
     * Return value reflects check on write to /dev/fuse,
     * so ignore issues with dumping.
     */

    return 0;
}

/*
 * iov_out should contain a fuse_out_header at zeroth position.
 * The error value of this header is sent to kernel.
 */
static int
send_fuse_iov(xlator_t *this, fuse_in_header_t *finh, struct iovec *iov_out,
              int count)
{
    fuse_private_t *priv = NULL;
    struct fuse_out_header *fouh = NULL;
    int res, i;

    if (!this || !finh || !iov_out) {
        gf_log("send_fuse_iov", GF_LOG_ERROR, "Invalid arguments");
        return EINVAL;
    }
    priv = this->private;

    fouh = iov_out[0].iov_base;
    iov_out[0].iov_len = sizeof(*fouh);
    fouh->len = 0;
    for (i = 0; i < count; i++)
        fouh->len += iov_out[i].iov_len;
    fouh->unique = finh->unique;

    res = sys_writev(priv->fd, iov_out, count);
    gf_log("glusterfs-fuse", GF_LOG_TRACE, "writev() result %d/%d %s", res,
           fouh->len, res == -1 ? strerror(errno) : "");

    return check_and_dump_fuse_W(priv, iov_out, count, res, NULL);
}

static int
send_fuse_data(xlator_t *this, fuse_in_header_t *finh, void *data, size_t size)
{
    struct fuse_out_header fouh = {
        0,
    };
    struct iovec iov_out[2];
    int ret = 0;

    fouh.error = 0;
    iov_out[0].iov_base = &fouh;
    iov_out[1].iov_base = data;
    iov_out[1].iov_len = size;

    ret = send_fuse_iov(this, finh, iov_out, 2);
    if (ret != 0)
        gf_log("glusterfs-fuse", GF_LOG_ERROR,
               "send_fuse_iov() "
               "failed: %s",
               strerror(ret));

    return ret;
}

#define send_fuse_obj(this, finh, obj)                                         \
    send_fuse_data(this, finh, obj, sizeof(*(obj)))

static int32_t
fuse_invalidate_entry(xlator_t *this, uint64_t fuse_ino)
{
#if (FUSE_KERNEL_MINOR_VERSION >= 11 && defined(HAVE_FUSE_NOTIFICATIONS))
    struct fuse_out_header *fouh = NULL;
    struct fuse_notify_inval_entry_out *fnieo = NULL;
    fuse_private_t *priv = NULL;
    dentry_t *dentry = NULL;
    dentry_t *tmp = NULL;
    inode_t *inode = NULL;
    size_t nlen = 0;
    fuse_invalidate_node_t *node = NULL;
    char gfid_str[UUID_CANONICAL_FORM_LEN + 1];

    priv = this->private;
    if (!priv->reverse_fuse_thread_started)
        return -1;

    if (priv->invalidate_limit &&
        (priv->invalidate_count >= priv->invalidate_limit)) {
        return -1;
    }

    inode = (inode_t *)(unsigned long)fuse_ino;
    if (inode == NULL)
        return -1;

    list_for_each_entry_safe(dentry, tmp, &inode->dentry_list, inode_list)
    {
        node = GF_CALLOC(1, sizeof(*node), gf_fuse_mt_invalidate_node_t);
        if (node == NULL)
            return -1;

        INIT_LIST_HEAD(&node->next);

        fouh = (struct fuse_out_header *)node->inval_buf;
        fnieo = (struct fuse_notify_inval_entry_out *)(fouh + 1);

        fouh->unique = 0;
        fouh->error = FUSE_NOTIFY_INVAL_ENTRY;

        if (ENOENT < ERRNOMASK_MAX)
            MASK_ERRNO(node->errnomask, ENOENT);
        if (ENOTDIR < ERRNOMASK_MAX)
            MASK_ERRNO(node->errnomask, ENOTDIR);
        if (EBUSY < ERRNOMASK_MAX)
            MASK_ERRNO(node->errnomask, EBUSY);
        if (ENOTEMPTY < ERRNOMASK_MAX)
            MASK_ERRNO(node->errnomask, ENOTEMPTY);

        if (dentry->name) {
            nlen = strlen(dentry->name);
            fouh->len = sizeof(*fouh) + sizeof(*fnieo) + nlen + 1;
            fnieo->parent = inode_to_fuse_nodeid(dentry->parent);

            fnieo->namelen = nlen;
            strcpy((node->inval_buf + sizeof(*fouh) + sizeof(*fnieo)),
                   dentry->name);
        }

        gf_log("glusterfs-fuse", GF_LOG_TRACE,
               "INVALIDATE entry: %" PRIu64 "/%s (gfid:%s)", fnieo->parent,
               dentry->name, uuid_utoa(inode->gfid));

        if (dentry->parent) {
            fuse_log_eh(this, "Invalidated entry %s (parent: %s) gfid:%s",
                        dentry->name, uuid_utoa(dentry->parent->gfid),
                        uuid_utoa_r(inode->gfid, gfid_str));
        } else {
            fuse_log_eh(this,
                        "Invalidated entry %s(nodeid: %" PRIu64 ") gfid:%s",
                        dentry->name, fnieo->parent, uuid_utoa(inode->gfid));
        }

        pthread_mutex_lock(&priv->invalidate_mutex);
        {
            list_add_tail(&node->next, &priv->invalidate_list);
            priv->invalidate_count++;
            pthread_cond_signal(&priv->invalidate_cond);
        }
        pthread_mutex_unlock(&priv->invalidate_mutex);
    }

#endif
    return 0;
}

/*
 * Send an inval inode notification to fuse. This causes an invalidation of the
 * entire page cache mapping on the inode.
 */
static int32_t
fuse_invalidate_inode(xlator_t *this, uint64_t fuse_ino)
{
#if (FUSE_KERNEL_MINOR_VERSION >= 11 && defined(HAVE_FUSE_NOTIFICATIONS))
    struct fuse_out_header *fouh = NULL;
    struct fuse_notify_inval_inode_out *fniio = NULL;
    fuse_private_t *priv = NULL;
    fuse_invalidate_node_t *node = NULL;
    inode_t *inode = NULL;

    priv = this->private;

    if (!priv->reverse_fuse_thread_started)
        return -1;

    if (priv->invalidate_limit &&
        (priv->invalidate_count >= priv->invalidate_limit)) {
        return -1;
    }

    inode = (inode_t *)(unsigned long)fuse_ino;
    if (inode == NULL)
        return -1;

    node = GF_CALLOC(1, sizeof(*node), gf_fuse_mt_invalidate_node_t);
    if (node == NULL)
        return -1;

    INIT_LIST_HEAD(&node->next);

    fouh = (struct fuse_out_header *)node->inval_buf;
    fniio = (struct fuse_notify_inval_inode_out *)(fouh + 1);

    fouh->unique = 0;
    fouh->error = FUSE_NOTIFY_INVAL_INODE;
    fouh->len = sizeof(struct fuse_out_header) +
                sizeof(struct fuse_notify_inval_inode_out);

    /* inval the entire mapping until we learn how to be more granular */
    fniio->ino = fuse_ino;
    fniio->off = 0;
    fniio->len = -1;

    if (ENOENT < ERRNOMASK_MAX)
        MASK_ERRNO(node->errnomask, ENOENT);

    fuse_log_eh(this, "Invalidated inode %" PRIu64 " (gfid: %s)", fuse_ino,
                uuid_utoa(inode->gfid));
    gf_log("glusterfs-fuse", GF_LOG_TRACE,
           "INVALIDATE inode: %" PRIu64 "(gfid:%s)", fuse_ino,
           uuid_utoa(inode->gfid));

    pthread_mutex_lock(&priv->invalidate_mutex);
    {
        list_add_tail(&node->next, &priv->invalidate_list);
        priv->invalidate_count++;
        pthread_cond_signal(&priv->invalidate_cond);
    }
    pthread_mutex_unlock(&priv->invalidate_mutex);

#else
    gf_log("glusterfs-fuse", GF_LOG_WARNING,
           "fuse_invalidate_inode not implemented on this system");
#endif
    return 0;
}

#if FUSE_KERNEL_MINOR_VERSION >= 11
/* Need this function for the signature (inode_t *, instead of uint64_t) */
static int32_t
fuse_inode_invalidate_fn(xlator_t *this, inode_t *inode)
{
    int32_t ret = 0;
    ret = fuse_invalidate_entry(this, (uint64_t)(uintptr_t)inode);
    return ret;
}
#endif

static fuse_timed_message_t *
fuse_timed_message_new(void)
{
    fuse_timed_message_t *dmsg = NULL;

    dmsg = GF_MALLOC(sizeof(*dmsg), gf_fuse_mt_timed_message_t);
    if (!dmsg) {
        return NULL;
    }

    /* should be NULL if not set */
    dmsg->fuse_message_body = NULL;
    INIT_LIST_HEAD(&dmsg->next);
    memset(dmsg->errnomask, 0, sizeof(dmsg->errnomask));

    return dmsg;
}

static void
fuse_timed_message_free(fuse_timed_message_t *dmsg)
{
    GF_FREE(dmsg->fuse_message_body);
    GF_FREE(dmsg);
}

static void
send_fuse_timed(xlator_t *this, fuse_timed_message_t *dmsg)
{
    fuse_private_t *priv = NULL;

    priv = this->private;

    if (!priv->timed_response_fuse_thread_started) {
        return;
    }

    pthread_mutex_lock(&priv->timed_mutex);
    {
        list_add_tail(&dmsg->next, &priv->timed_list);
        pthread_cond_signal(&priv->timed_cond);
    }
    pthread_mutex_unlock(&priv->timed_mutex);
}

fuse_interrupt_record_t *
fuse_interrupt_record_new(fuse_in_header_t *finh,
                          fuse_interrupt_handler_t handler)
{
    fuse_interrupt_record_t *fir = NULL;

    fir = GF_MALLOC(sizeof(*fir), gf_fuse_mt_interrupt_record_t);
    if (!fir) {
        return NULL;
    }

    fir->hit = _gf_false;
    fir->interrupt_state = INTERRUPT_NONE;
    fir->data = NULL;

    fir->interrupt_handler = handler;
    memcpy(&fir->fuse_in_header, finh, sizeof(*finh));
    pthread_cond_init(&fir->handler_cond, NULL);
    pthread_mutex_init(&fir->handler_mutex, NULL);
    INIT_LIST_HEAD(&fir->next);

    return fir;
}

static void
fuse_interrupt_record_free(fuse_interrupt_record_t *fir, void **datap)
{
    /*
     * If caller wishes, we give back the private data to let them deal with it
     * however they want; otherwise we take care of freeing it.
     */
    if (datap) {
        *datap = fir->data;
    } else {
        GF_FREE(fir->data);
    }

    GF_FREE(fir);
}

void
fuse_interrupt_record_insert(xlator_t *this, fuse_interrupt_record_t *fir)
{
    fuse_private_t *priv = NULL;

    priv = this->private;
    pthread_mutex_lock(&priv->interrupt_mutex);
    {
        list_add_tail(&fir->next, &priv->interrupt_list);
    }
    pthread_mutex_unlock(&priv->interrupt_mutex);
}

static fuse_interrupt_record_t *
fuse_interrupt_record_fetch(xlator_t *this, uint64_t unique, gf_boolean_t reap)
{
    fuse_interrupt_record_t *fir = NULL;
    gf_boolean_t found = _gf_false;
    fuse_private_t *priv = NULL;

    priv = this->private;
    pthread_mutex_lock(&priv->interrupt_mutex);
    {
        list_for_each_entry(fir, &priv->interrupt_list, next)
        {
            if (fir->fuse_in_header.unique == unique) {
                /*
                 * If we are to reap, we do it regardless the
                 * hit flag; otherwise we take the record only
                 * hasn't yet flagged hit.
                 */
                if (reap || !fir->hit) {
                    found = _gf_true;
                }
                /*
                 * If we are not reaping (coming from handler
                 * context), we set the hit flag.
                 */
                if (!reap) {
                    fir->hit = _gf_true;
                }
                break;
            }
        }
        if (found && reap) {
            list_del(&fir->next);
        }
    }
    pthread_mutex_unlock(&priv->interrupt_mutex);

    if (found) {
        return fir;
    }
    return NULL;
}

static fuse_interrupt_record_t *
fuse_interrupt_record_get(xlator_t *this, uint64_t unique)
{
    return fuse_interrupt_record_fetch(this, unique, _gf_false);
}

static fuse_interrupt_record_t *
fuse_interrupt_record_reap(xlator_t *this, uint64_t unique)
{
    return fuse_interrupt_record_fetch(this, unique, _gf_true);
}

static void
fuse_interrupt(xlator_t *this, fuse_in_header_t *finh, void *msg,
               struct iobuf *iobuf)
{
    struct fuse_interrupt_in *fii = msg;
    fuse_interrupt_record_t *fir = NULL;

    gf_log("glusterfs-fuse", GF_LOG_TRACE,
           "unique %" PRIu64 " INTERRUPT for %" PRIu64, finh->unique,
           fii->unique);

    fir = fuse_interrupt_record_get(this, fii->unique);
    if (fir) {
        gf_log("glusterfs-fuse", GF_LOG_DEBUG,
               "unique %" PRIu64 " INTERRUPT for %" PRIu64
               ": handler triggered",
               finh->unique, fii->unique);

        fir->interrupt_handler(this, fir);
    } else {
        fuse_timed_message_t *dmsg = NULL;

        /*
         * No record found for this interrupt request.
         *
         * It's either because the handler for the interrupted message
         * does not want to handle interrupt, or this interrupt
         * message beat the interrupted which hasn't yet added a record
         * to the interrupt queue. Either case we reply with error
         * EAGAIN with some (0.01 sec) delay. That will have this
         * interrupt request resent, unless the interrupted message
         * has been already answered.
         *
         * So effectively we are looping in between kernel and
         * userspace, which will be exited either when the interrupted
         * message handler has added an interrupt record, or has
         * replied to kernel. See
         *
         * https://git.kernel.org/pub/scm/linux/kernel/git/torvalds/
         * linux.git/tree/Documentation/filesystems/fuse.txt?h=v4.18#n148
         */

        gf_log("glusterfs-fuse", GF_LOG_DEBUG,
               "unique %" PRIu64 " INTERRUPT for %" PRIu64 ": no handler found",
               finh->unique, fii->unique);

        dmsg = fuse_timed_message_new();
        if (!dmsg) {
            gf_log("glusterfs-fuse", GF_LOG_ERROR,
                   "unique %" PRIu64 " INTERRUPT for %" PRIu64
                   ":"
                   " failed to allocate timed message",
                   finh->unique, fii->unique);

            goto out;
        }

        dmsg->fuse_out_header.unique = finh->unique;
        dmsg->fuse_out_header.len = sizeof(dmsg->fuse_out_header);
        dmsg->fuse_out_header.error = -EAGAIN;
        if (ENOENT < ERRNOMASK_MAX)
            MASK_ERRNO(dmsg->errnomask, ENOENT);
        timespec_now(&dmsg->scheduled_ts);
        timespec_adjust_delta(&dmsg->scheduled_ts,
                              (struct timespec){0, 10000000});

        send_fuse_timed(this, dmsg);
    }

out:
    GF_FREE(finh);
}

/*
 * Function to be called in fop cbk context (if the fop engages
 * with interrupt handling).
 */
gf_boolean_t
fuse_interrupt_finish_fop(call_frame_t *frame, xlator_t *this,
                          gf_boolean_t sync, void **datap)
{
    fuse_interrupt_record_t *fir = NULL;
    fuse_state_t *state = frame->root->state;
    fuse_in_header_t *finh = state->finh;
    gf_boolean_t hit = _gf_false;
    gf_boolean_t handled = _gf_false;
    fuse_interrupt_state_t intstat_orig = INTERRUPT_NONE;

    fir = fuse_interrupt_record_reap(this, finh->unique);
    if (!fir) {
        /*
         * No interrupt record was inserted (however, caller would usually know
         * about that and there is no point then in calling this function).
         */
        return _gf_false;
    }

    /*
     * The interrupt handler (if finds the record) modifies fir->hit; however,
     * that could have occurred only before fuse_interrupt_record_reap(), so
     * we are safe here with a lock-free access.
     */
    hit = fir->hit;
    if (hit) {
        pthread_mutex_lock(&fir->handler_mutex);
        {
            intstat_orig = fir->interrupt_state;
            if (fir->interrupt_state == INTERRUPT_NONE) {
                if (sync) {
                    fir->interrupt_state = INTERRUPT_WAITING_HANDLER;
                    while (fir->interrupt_state != INTERRUPT_SQUELCHED) {
                        pthread_cond_wait(&fir->handler_cond,
                                          &fir->handler_mutex);
                    }
                } else
                    fir->interrupt_state = INTERRUPT_SQUELCHED;
            }
        }
        pthread_mutex_unlock(&fir->handler_mutex);
    }

    GF_ASSERT(intstat_orig == INTERRUPT_NONE ||
              intstat_orig == INTERRUPT_HANDLED ||
              intstat_orig == INTERRUPT_SQUELCHED);
    gf_log("glusterfs-fuse", GF_LOG_DEBUG, "intstat_orig=%d", intstat_orig);

    /*
     * From this on fir can only be referred under the conditions that imply
     * we are to free it (otherwise interrupt handler might have already freed
     * it).
     */

    if (/* there was no interrupt */
        !hit ||
        /* lost the race against interrupt handler */
        intstat_orig != INTERRUPT_NONE ||
        /* we took cleaning up on us */
        sync) {
        /* cleaning up */
        fuse_interrupt_record_free(fir, datap);
    } else if (datap) {
        *datap = NULL;
    }

    handled = (intstat_orig == INTERRUPT_HANDLED);
    if (handled) {
        /*
         * Fuse request was answered already from interrupt context, we can do
         * away with the stack.
         */
        free_fuse_state(state);
        STACK_DESTROY(frame->root);
    }

    /*
     * Let caller know if they have to answer the fuse request.
     */
    return handled;
}

/*
 * Function to be called in interrupt handler context.
 */
void
fuse_interrupt_finish_interrupt(xlator_t *this, fuse_interrupt_record_t *fir,
                                fuse_interrupt_state_t intstat,
                                gf_boolean_t sync, void **datap)
{
    fuse_in_header_t finh = {
        0,
    };
    fuse_interrupt_state_t intstat_orig = INTERRUPT_NONE;

    GF_ASSERT(intstat == INTERRUPT_HANDLED || intstat == INTERRUPT_SQUELCHED);

    pthread_mutex_lock(&fir->handler_mutex);
    {
        intstat_orig = fir->interrupt_state;
        switch (intstat_orig) {
            case INTERRUPT_NONE:
                fir->interrupt_state = intstat;
                break;
            case INTERRUPT_WAITING_HANDLER:
                fir->interrupt_state = INTERRUPT_SQUELCHED;
                pthread_cond_signal(&fir->handler_cond);
                break;
            default:
                break;
        }
        finh = fir->fuse_in_header;
    }
    pthread_mutex_unlock(&fir->handler_mutex);

    GF_ASSERT(intstat_orig == INTERRUPT_NONE ||
              (sync && intstat_orig == INTERRUPT_WAITING_HANDLER) ||
              (!sync && intstat_orig == INTERRUPT_SQUELCHED));
    gf_log("glusterfs-fuse", GF_LOG_DEBUG, "intstat_orig=%d", intstat_orig);

    /*
     * From this on fir can only be referred under the conditions that imply
     * we are to free it (otherwise fop handler might have already freed it).
     */

    if (/* we won the race, response is up to us */
        intstat_orig == INTERRUPT_NONE &&
        /* interrupt handling was successful, let the kernel know */
        intstat == INTERRUPT_HANDLED) {
        send_fuse_err(this, &finh, EINTR);
    }

    if (/* lost the race ... */
        intstat_orig != INTERRUPT_NONE &&
        /*
         * ... and there is no contract with fop handler that it does the
         * cleanup ...
         */
        !sync) {
        /* ... so we do! */
        fuse_interrupt_record_free(fir, datap);
    } else if (datap) {
        *datap = NULL;
    }
}

int
send_fuse_err(xlator_t *this, fuse_in_header_t *finh, int error)
{
    struct fuse_out_header fouh = {
        0,
    };
    struct iovec iov_out;
    inode_t *inode = NULL;

    fouh.error = -error;
    iov_out.iov_base = &fouh;

    inode = fuse_ino_to_inode(finh->nodeid, this);

    // filter out ENOENT
    if (error != ENOENT) {
        if (inode) {
            fuse_log_eh(this,
                        "Sending %s for operation %d on "
                        "inode %s",
                        strerror(error), finh->opcode, uuid_utoa(inode->gfid));
        } else {
            fuse_log_eh(this,
                        "Sending %s for operation %d on "
                        "inode %" GF_PRI_INODE,
                        strerror(error), finh->opcode, finh->nodeid);
        }
    }

    if (inode)
        inode_unref(inode);

    return send_fuse_iov(this, finh, &iov_out, 1);
}

static int
fuse_entry_cbk(call_frame_t *frame, void *cookie, xlator_t *this,
               int32_t op_ret, int32_t op_errno, inode_t *inode,
               struct iatt *buf, dict_t *xdata)
{
    fuse_state_t *state = NULL;
    fuse_in_header_t *finh = NULL;
    struct fuse_entry_out feo = {
        0,
    };
    fuse_private_t *priv = NULL;
    inode_t *linked_inode = NULL;
    uint64_t ctx_value = LOOKUP_NOT_NEEDED;

    priv = this->private;
    state = frame->root->state;
    finh = state->finh;

    if (op_ret == 0) {
        if (__is_root_gfid(state->loc.inode->gfid))
            buf->ia_ino = 1;
        if (gf_uuid_is_null(buf->ia_gfid)) {
            /* With a NULL gfid inode linking is
               not possible. Let's not pretend this
               call was a "success".
            */
            gf_log("glusterfs-fuse", GF_LOG_WARNING,
                   "Received NULL gfid for %s. Forcing EIO", state->loc.path);
            op_ret = -1;
            op_errno = EIO;
        }
    }

    /* log into the event-history after the null uuid check is done, since
     * the op_ret and op_errno are being changed if the gfid is NULL.
     */
    fuse_log_eh(
        this,
        "op_ret: %d op_errno: %d "
        "%" PRIu64 ": %s() %s => %s",
        op_ret, op_errno, frame->root->unique, gf_fop_list[frame->root->op],
        state->loc.path,
        (op_ret == 0) ? uuid_utoa(buf->ia_gfid) : uuid_utoa(state->loc.gfid));

    if (op_ret == 0) {
        gf_log("glusterfs-fuse", GF_LOG_TRACE,
               "%" PRIu64 ": %s() %s => %" PRIu64, frame->root->unique,
               gf_fop_list[frame->root->op], state->loc.path, buf->ia_ino);

        buf->ia_blksize = this->ctx->page_size;
        gf_fuse_stat2attr(buf, &feo.attr, priv->enable_ino32);

        if (!buf->ia_ino) {
            gf_log("glusterfs-fuse", GF_LOG_WARNING,
                   "%" PRIu64 ": %s() %s returning inode 0",
                   frame->root->unique, gf_fop_list[frame->root->op],
                   state->loc.path);
        }

        linked_inode = inode_link(inode, state->loc.parent, state->loc.name,
                                  buf);

        if (linked_inode == inode) {
            inode_ctx_set(linked_inode, this, &ctx_value);
        }

        inode_lookup(linked_inode);

        feo.nodeid = inode_to_fuse_nodeid(linked_inode);

        inode_unref(linked_inode);

        feo.entry_valid = calc_timeout_sec(priv->entry_timeout);
        feo.entry_valid_nsec = calc_timeout_nsec(priv->entry_timeout);
        feo.attr_valid = calc_timeout_sec(priv->attribute_timeout);
        feo.attr_valid_nsec = calc_timeout_nsec(priv->attribute_timeout);

#if FUSE_KERNEL_MINOR_VERSION >= 9
        priv->proto_minor >= 9
            ? send_fuse_obj(this, finh, &feo)
            : send_fuse_data(this, finh, &feo, FUSE_COMPAT_ENTRY_OUT_SIZE);
#else
        send_fuse_obj(this, finh, &feo);
#endif
    } else {
        gf_log("glusterfs-fuse",
               (op_errno == ENOENT ? GF_LOG_TRACE : GF_LOG_WARNING),
               "%" PRIu64 ": %s() %s => -1 (%s)", frame->root->unique,
               gf_fop_list[frame->root->op], state->loc.path,
               strerror(op_errno));

        if ((op_errno == ENOENT) && (priv->negative_timeout != 0)) {
            feo.entry_valid = calc_timeout_sec(priv->negative_timeout);
            feo.entry_valid_nsec = calc_timeout_nsec(priv->negative_timeout);
            send_fuse_obj(this, finh, &feo);
        } else {
            send_fuse_err(this, state->finh, op_errno);
        }
    }

    free_fuse_state(state);
    STACK_DESTROY(frame->root);
    return 0;
}

static int
fuse_newentry_cbk(call_frame_t *frame, void *cookie, xlator_t *this,
                  int32_t op_ret, int32_t op_errno, inode_t *inode,
                  struct iatt *buf, struct iatt *preparent,
                  struct iatt *postparent, dict_t *xdata)
{
    /* facilitate retry of link from VFS */
    if (op_errno == ENOENT)
        op_errno = ESTALE;

    fuse_entry_cbk(frame, cookie, this, op_ret, op_errno, inode, buf, xdata);
    return 0;
}

static int
fuse_lookup_cbk(call_frame_t *frame, void *cookie, xlator_t *this,
                int32_t op_ret, int32_t op_errno, inode_t *inode,
                struct iatt *stat, dict_t *dict, struct iatt *postparent)
{
    fuse_state_t *state = NULL;
    call_frame_t *prev = NULL;
    inode_table_t *itable = NULL;

    state = frame->root->state;
    prev = cookie;

    if (op_ret == -1 && state->is_revalidate == 1) {
        itable = state->itable;
        /*
         * A stale mapping might exist for a dentry/inode that has been
         * removed from another client.
         */
        if (op_errno == ENOENT)
            inode_unlink(state->loc.inode, state->loc.parent, state->loc.name);
        inode_unref(state->loc.inode);
        state->loc.inode = inode_new(itable);
        state->is_revalidate = 2;
        if (gf_uuid_is_null(state->gfid))
            gf_uuid_generate(state->gfid);
        fuse_gfid_set(state);

        STACK_WIND(frame, fuse_lookup_cbk, prev->this, prev->this->fops->lookup,
                   &state->loc, state->xdata);
        return 0;
    }

    fuse_entry_cbk(frame, cookie, this, op_ret, op_errno, inode, stat, dict);
    return 0;
}

void
fuse_fop_resume(fuse_state_t *state)
{
    fuse_resume_fn_t fn = NULL;

    /*
     * Fail fd resolution failures right away.
     */
    if (state->resolve.fd && state->resolve.op_ret < 0) {
        send_fuse_err(state->this, state->finh, state->resolve.op_errno);
        free_fuse_state(state);
        return;
    }

    fn = state->resume_fn;
    fn(state);
}

void
fuse_lookup_resume(fuse_state_t *state)
{
    if (!state->loc.parent && !state->loc.inode) {
        gf_log("fuse", GF_LOG_ERROR, "failed to resolve path %s",
               state->loc.path);
        send_fuse_err(state->this, state->finh, state->resolve.op_errno);
        free_fuse_state(state);
        return;
    }

    /* parent was resolved, entry could not, may be a missing gfid?
     * Hence try to do a regular lookup
     */
    if ((state->resolve.op_ret == -1) && (state->resolve.op_errno == ENODATA)) {
        state->resolve.op_ret = 0;
    }

    if (state->loc.inode) {
        gf_log("glusterfs-fuse", GF_LOG_TRACE, "%" PRIu64 ": LOOKUP %s(%s)",
               state->finh->unique, state->loc.path,
               uuid_utoa(state->loc.inode->gfid));
        state->is_revalidate = 1;
    } else {
        gf_log("glusterfs-fuse", GF_LOG_TRACE, "%" PRIu64 ": LOOKUP %s",
               state->finh->unique, state->loc.path);
        state->loc.inode = inode_new(state->loc.parent->table);
        if (gf_uuid_is_null(state->gfid))
            gf_uuid_generate(state->gfid);
        fuse_gfid_set(state);
    }

    FUSE_FOP(state, fuse_lookup_cbk, GF_FOP_LOOKUP, lookup, &state->loc,
             state->xdata);
}

static void
fuse_lookup(xlator_t *this, fuse_in_header_t *finh, void *msg,
            struct iobuf *iobuf)
{
    char *name = msg;
    fuse_state_t *state = NULL;

    GET_STATE(this, finh, state);

    (void)fuse_resolve_entry_init(state, &state->resolve, finh->nodeid, name);

    fuse_resolve_and_resume(state, fuse_lookup_resume);

    return;
}

static void
do_forget(xlator_t *this, uint64_t unique, uint64_t nodeid, uint64_t nlookup)
{
    inode_t *fuse_inode = fuse_ino_to_inode(nodeid, this);

    gf_log("fuse", GF_LOG_TRACE,
           "%" PRIu64 ": FORGET %" PRIu64 "/%" PRIu64 " gfid: (%s)", unique,
           nodeid, nlookup, uuid_utoa(fuse_inode->gfid));

    fuse_log_eh(this, "%" PRIu64 ": FORGET %" PRIu64 "/%" PRIu64 " gfid: (%s)",
                unique, nodeid, nlookup, uuid_utoa(fuse_inode->gfid));

    inode_forget_with_unref(fuse_inode, nlookup);
}

static void
fuse_forget(xlator_t *this, fuse_in_header_t *finh, void *msg,
            struct iobuf *iobuf)

{
    struct fuse_forget_in *ffi = msg;

    if (finh->nodeid == 1) {
        GF_FREE(finh);
        return;
    }

    do_forget(this, finh->unique, finh->nodeid, ffi->nlookup);

    GF_FREE(finh);
}

#if FUSE_KERNEL_MINOR_VERSION >= 16
static void
fuse_batch_forget(xlator_t *this, fuse_in_header_t *finh, void *msg,
                  struct iobuf *iobuf)
{
    struct fuse_batch_forget_in *fbfi = msg;
    struct fuse_forget_one *ffo = (struct fuse_forget_one *)(fbfi + 1);
    int i;

    gf_log("glusterfs-fuse", GF_LOG_TRACE,
           "%" PRIu64 ": BATCH_FORGET %" PRIu64 "/%" PRIu32, finh->unique,
           finh->nodeid, fbfi->count);

    for (i = 0; i < fbfi->count; i++) {
        if (ffo[i].nodeid == 1)
            continue;
        do_forget(this, finh->unique, ffo[i].nodeid, ffo[i].nlookup);
    }
    GF_FREE(finh);
}
#endif

static int
fuse_truncate_cbk(call_frame_t *frame, void *cookie, xlator_t *this,
                  int32_t op_ret, int32_t op_errno, struct iatt *prebuf,
                  struct iatt *postbuf, dict_t *xdata)
{
    fuse_state_t *state;
    fuse_in_header_t *finh;
    fuse_private_t *priv = NULL;
    struct fuse_attr_out fao;

    priv = this->private;
    state = frame->root->state;
    finh = state->finh;

    fuse_log_eh_fop(this, state, frame, op_ret, op_errno);

    if (op_ret == 0) {
        gf_log("glusterfs-fuse", GF_LOG_TRACE,
               "%" PRIu64 ": %s() %s => %" PRIu64, frame->root->unique,
               gf_fop_list[frame->root->op],
               state->loc.path ? state->loc.path : "ERR", prebuf->ia_ino);

        postbuf->ia_blksize = this->ctx->page_size;
        gf_fuse_stat2attr(postbuf, &fao.attr, priv->enable_ino32);

        fao.attr_valid = calc_timeout_sec(priv->attribute_timeout);
        fao.attr_valid_nsec = calc_timeout_nsec(priv->attribute_timeout);

#if FUSE_KERNEL_MINOR_VERSION >= 9
        priv->proto_minor >= 9
            ? send_fuse_obj(this, finh, &fao)
            : send_fuse_data(this, finh, &fao, FUSE_COMPAT_ATTR_OUT_SIZE);
#else
        send_fuse_obj(this, finh, &fao);
#endif
    } else {
        gf_log("glusterfs-fuse", GF_LOG_WARNING,
               "%" PRIu64 ": %s() %s => -1 (%s)", frame->root->unique,
               gf_fop_list[frame->root->op],
               state->loc.path ? state->loc.path : "ERR", strerror(op_errno));

        /* facilitate retry from VFS */
        if ((state->fd == NULL) && (op_errno == ENOENT))
            op_errno = ESTALE;

        send_fuse_err(this, finh, op_errno);
    }

    free_fuse_state(state);
    STACK_DESTROY(frame->root);

    return 0;
}

static int
fuse_root_lookup_cbk(call_frame_t *frame, void *cookie, xlator_t *this,
                     int32_t op_ret, int32_t op_errno, inode_t *inode,
                     struct iatt *stat, dict_t *dict, struct iatt *postparent);

static int
fuse_attr_cbk(call_frame_t *frame, void *cookie, xlator_t *this, int32_t op_ret,
              int32_t op_errno, struct iatt *buf, dict_t *xdata)
{
    int32_t ret = 0;
    fuse_state_t *state;
    fuse_in_header_t *finh;
    fuse_private_t *priv = NULL;
    struct fuse_attr_out fao;

    priv = this->private;
    state = frame->root->state;
    finh = state->finh;

    fuse_log_eh(this,
                "op_ret: %d, op_errno: %d, %" PRIu64
                ": %s() %s => "
                "gfid: %s",
                op_ret, op_errno, frame->root->unique,
                gf_fop_list[frame->root->op], state->loc.path,
                state->loc.inode ? uuid_utoa(state->loc.inode->gfid) : "");
    if (op_ret == 0) {
        gf_log("glusterfs-fuse", GF_LOG_TRACE,
               "%" PRIu64 ": %s() %s => %" PRIu64, frame->root->unique,
               gf_fop_list[frame->root->op],
               state->loc.path ? state->loc.path : "ERR", buf->ia_ino);

        buf->ia_blksize = this->ctx->page_size;
        gf_fuse_stat2attr(buf, &fao.attr, priv->enable_ino32);

        fao.attr_valid = calc_timeout_sec(priv->attribute_timeout);
        fao.attr_valid_nsec = calc_timeout_nsec(priv->attribute_timeout);

#if FUSE_KERNEL_MINOR_VERSION >= 9
        priv->proto_minor >= 9
            ? send_fuse_obj(this, finh, &fao)
            : send_fuse_data(this, finh, &fao, FUSE_COMPAT_ATTR_OUT_SIZE);
#else
        send_fuse_obj(this, finh, &fao);
#endif
    } else {
        /* This is moved here from fuse_getattr(). It makes sense as
           in few cases, like the self-heal processes, some
           translators expect a lookup() to come on root inode
           (inode number 1). This will make sure we don't fail in any
           case, but the positive path will get better performance,
           by following common path for all the cases */
        if ((finh->nodeid == 1) && (state->gfid[15] != 1)) {
            /* The 'state->gfid[15]' check is added to prevent the
               infinite recursions */
            state->gfid[15] = 1;

            ret = fuse_loc_fill(&state->loc, state, finh->nodeid, 0, NULL);
            if (ret < 0) {
                gf_log("glusterfs-fuse", GF_LOG_WARNING,
                       "%" PRIu64 ": loc_fill() on / failed", finh->unique);
                send_fuse_err(this, finh, ENOENT);
                free_fuse_state(state);
                return 0;
            }

            fuse_gfid_set(state);

            FUSE_FOP(state, fuse_root_lookup_cbk, GF_FOP_LOOKUP, lookup,
                     &state->loc, state->xdata);

            return 0;
        }

        /* facilitate retry from VFS */
        if ((state->fd == NULL) && (op_errno == ENOENT))
            op_errno = ESTALE;

        gf_log("glusterfs-fuse", GF_LOG_WARNING,
               "%" PRIu64
               ": %s() "
               "%s => -1 (%s)",
               frame->root->unique, gf_fop_list[frame->root->op],
               state->loc.path ? state->loc.path : "ERR", strerror(op_errno));

        send_fuse_err(this, finh, op_errno);
    }

    free_fuse_state(state);
    STACK_DESTROY(frame->root);

    return 0;
}

static int
fuse_root_lookup_cbk(call_frame_t *frame, void *cookie, xlator_t *this,
                     int32_t op_ret, int32_t op_errno, inode_t *inode,
                     struct iatt *stat, dict_t *dict, struct iatt *postparent)
{
    fuse_attr_cbk(frame, cookie, this, op_ret, op_errno, stat, dict);

    return 0;
}

void
fuse_getattr_resume(fuse_state_t *state)
{
    if (!state->loc.inode && !(state->fd && state->fd->inode)) {
        gf_log("glusterfs-fuse", GF_LOG_ERROR,
               "%" PRIu64 ": GETATTR %" PRIu64 " (%s) resolution failed",
               state->finh->unique, state->finh->nodeid,
               uuid_utoa(state->resolve.gfid));

        /* facilitate retry from VFS */
        if ((state->fd == NULL) && (state->resolve.op_errno == ENOENT))
            state->resolve.op_errno = ESTALE;

        send_fuse_err(state->this, state->finh, state->resolve.op_errno);
        free_fuse_state(state);
        return;
    }

    if (state->fd == NULL && !IA_ISDIR(state->loc.inode->ia_type)) {
        state->fd = fd_lookup(state->loc.inode, state->finh->pid);

        if (state->fd == NULL)
            state->fd = fd_lookup(state->loc.inode, 0);
    }

    if (!state->fd) {
        gf_log("glusterfs-fuse", GF_LOG_TRACE,
               "%" PRIu64 ": GETATTR %" PRIu64 " (%s)", state->finh->unique,
               state->finh->nodeid, state->loc.path);

        FUSE_FOP(state, fuse_attr_cbk, GF_FOP_STAT, stat, &state->loc,
                 state->xdata);
    } else {
        gf_log("glusterfs-fuse", GF_LOG_TRACE,
               "%" PRIu64 ": FGETATTR %" PRIu64 " (%s/%p)", state->finh->unique,
               state->finh->nodeid, state->loc.path, state->fd);

        FUSE_FOP(state, fuse_attr_cbk, GF_FOP_FSTAT, fstat, state->fd,
                 state->xdata);
    }
}

static void
fuse_getattr(xlator_t *this, fuse_in_header_t *finh, void *msg,
             struct iobuf *iobuf)
{
#if FUSE_KERNEL_MINOR_VERSION >= 9
    struct fuse_getattr_in *fgi = msg;
    fuse_private_t *priv = NULL;
#endif
    fuse_state_t *state;
    int ret = -1;

    GET_STATE(this, finh, state);
#if FUSE_KERNEL_MINOR_VERSION >= 9
    priv = this->private;
    if (priv->proto_minor >= 9 && fgi->getattr_flags & FUSE_GETATTR_FH)
        state->fd = fd_ref((fd_t *)(uintptr_t)fgi->fh);
#endif
    if (finh->nodeid == 1) {
        state->gfid[15] = 1;

        ret = fuse_loc_fill(&state->loc, state, finh->nodeid, 0, NULL);
        if (ret < 0) {
            gf_log("glusterfs-fuse", GF_LOG_WARNING,
                   "%" PRIu64 ": GETATTR on / (fuse_loc_fill() failed)",
                   finh->unique);
            send_fuse_err(this, finh, ESTALE);
            free_fuse_state(state);
            return;
        }

        fuse_gfid_set(state);

        FUSE_FOP(state, fuse_root_lookup_cbk, GF_FOP_LOOKUP, lookup,
                 &state->loc, state->xdata);
        return;
    }

    if (state->fd)
        fuse_resolve_fd_init(state, &state->resolve, state->fd);
    else
        fuse_resolve_inode_init(state, &state->resolve, state->finh->nodeid);

    fuse_resolve_and_resume(state, fuse_getattr_resume);
}

static int32_t
fuse_fd_inherit_directio(xlator_t *this, fd_t *fd, struct fuse_open_out *foo)
{
    int32_t ret = 0;
    fuse_fd_ctx_t *fdctx = NULL, *tmp_fdctx = NULL;
    fd_t *tmp_fd = NULL;

    GF_VALIDATE_OR_GOTO_WITH_ERROR("glusterfs-fuse", this, out, ret, -EINVAL);
    GF_VALIDATE_OR_GOTO_WITH_ERROR("glusterfs-fuse", fd, out, ret, -EINVAL);
    GF_VALIDATE_OR_GOTO_WITH_ERROR("glusterfs-fuse", foo, out, ret, -EINVAL);

    fdctx = fuse_fd_ctx_get(this, fd);
    if (!fdctx) {
        ret = -ENOMEM;
        goto out;
    }

    tmp_fd = fd_lookup(fd->inode, 0);
    if (tmp_fd) {
        tmp_fdctx = fuse_fd_ctx_get(this, tmp_fd);
        if (tmp_fdctx) {
            foo->open_flags &= ~FOPEN_DIRECT_IO;
            foo->open_flags |= (tmp_fdctx->open_flags & FOPEN_DIRECT_IO);
        }
    }

    fdctx->open_flags |= (foo->open_flags & FOPEN_DIRECT_IO);

    if (tmp_fd != NULL) {
        fd_unref(tmp_fd);
    }

    ret = 0;
out:
    return ret;
}

gf_boolean_t
direct_io_mode(dict_t *xdata)
{
    if (xdata && dict_get(xdata, "direct-io-mode"))
        return _gf_true;
    return _gf_false;
}

static int
fuse_fd_cbk(call_frame_t *frame, void *cookie, xlator_t *this, int32_t op_ret,
            int32_t op_errno, fd_t *fd, dict_t *xdata)
{
    fuse_state_t *state = NULL;
    fuse_in_header_t *finh = NULL;
    fuse_private_t *priv = NULL;
    int32_t ret = 0;
    struct fuse_open_out foo = {
        0,
    };

    priv = this->private;
    state = frame->root->state;
    finh = state->finh;

    fuse_log_eh_fop(this, state, frame, op_ret, op_errno);

    if (op_ret >= 0) {
        foo.fh = (uintptr_t)fd;
        foo.open_flags = 0;

        if (!IA_ISDIR(fd->inode->ia_type)) {
            if (((priv->direct_io_mode == 2) &&
                 ((state->flags & O_ACCMODE) != O_RDONLY)) ||
                (priv->direct_io_mode == 1) || (direct_io_mode(xdata)))
                foo.open_flags |= FOPEN_DIRECT_IO;
#ifdef GF_DARWIN_HOST_OS
            /* In Linux: by default, buffer cache
             * is purged upon open, setting
             * FOPEN_KEEP_CACHE implies no-purge
             *
             * In MacFUSE: by default, buffer cache
             * is left intact upon open, setting
             * FOPEN_PURGE_UBC implies purge
             *
             * [[Interesting...]]
             */
            if (!priv->fopen_keep_cache)
                foo.open_flags |= FOPEN_PURGE_UBC;
#else
            /*
             * If fopen-keep-cache is enabled, we set the associated
             * flag here such that files are not invalidated on open.
             * File invalidations occur either in fuse or explicitly
             * when the cache is set invalid on the inode.
             */
            if (priv->fopen_keep_cache)
                foo.open_flags |= FOPEN_KEEP_CACHE;
#endif
        }

        gf_log("glusterfs-fuse", GF_LOG_TRACE, "%" PRIu64 ": %s() %s => %p",
               frame->root->unique, gf_fop_list[frame->root->op],
               state->loc.path, fd);

        ret = fuse_fd_inherit_directio(this, fd, &foo);
        if (ret < 0) {
            op_errno = -ret;
            gf_log("glusterfs-fuse", GF_LOG_WARNING,
                   "cannot inherit direct-io values for fd "
                   "(ptr:%p inode-gfid:%s) from fds already "
                   "opened",
                   fd, uuid_utoa(fd->inode->gfid));
            goto err;
        }

        if (send_fuse_obj(this, finh, &foo) == ENOENT) {
            gf_log("glusterfs-fuse", GF_LOG_DEBUG, "open(%s) got EINTR",
                   state->loc.path);
            gf_fd_put(priv->fdtable, state->fd_no);
            goto out;
        }

        fd_bind(fd);
    } else {
    err:
        /* OPEN(DIR) being an operation on inode should never fail with
         * ENOENT. If gfid is not present, the appropriate error is
         * ESTALE.
         */
        if (op_errno == ENOENT)
            op_errno = ESTALE;

        gf_log("glusterfs-fuse", GF_LOG_WARNING,
               "%" PRIu64 ": %s() %s => -1 (%s)", frame->root->unique,
               gf_fop_list[frame->root->op], state->loc.path,
               strerror(op_errno));

        send_fuse_err(this, finh, op_errno);
        gf_fd_put(priv->fdtable, state->fd_no);
    }
out:
    free_fuse_state(state);
    STACK_DESTROY(frame->root);
    return 0;
}

static void
fuse_do_truncate(fuse_state_t *state)
{
    if (state->fd) {
        FUSE_FOP(state, fuse_truncate_cbk, GF_FOP_FTRUNCATE, ftruncate,
                 state->fd, state->off, state->xdata);
    } else {
        FUSE_FOP(state, fuse_truncate_cbk, GF_FOP_TRUNCATE, truncate,
                 &state->loc, state->off, state->xdata);
    }

    return;
}

static int
fuse_setattr_cbk(call_frame_t *frame, void *cookie, xlator_t *this,
                 int32_t op_ret, int32_t op_errno, struct iatt *statpre,
                 struct iatt *statpost, dict_t *xdata)
{
    fuse_state_t *state;
    fuse_in_header_t *finh;
    fuse_private_t *priv = NULL;
    struct fuse_attr_out fao;

    int op_done = 0;

    priv = this->private;
    state = frame->root->state;
    finh = state->finh;

    fuse_log_eh(this,
                "op_ret: %d, op_errno: %d, %" PRIu64
                ", %s() %s => "
                "gfid: %s",
                op_ret, op_errno, frame->root->unique,
                gf_fop_list[frame->root->op], state->loc.path,
                state->loc.inode ? uuid_utoa(state->loc.inode->gfid) : "");

    if (op_ret == 0) {
        gf_log("glusterfs-fuse", GF_LOG_TRACE,
               "%" PRIu64 ": %s() %s => %" PRIu64, frame->root->unique,
               gf_fop_list[frame->root->op],
               state->loc.path ? state->loc.path : "ERR", statpost->ia_ino);

        statpost->ia_blksize = this->ctx->page_size;
        gf_fuse_stat2attr(statpost, &fao.attr, priv->enable_ino32);

        fao.attr_valid = calc_timeout_sec(priv->attribute_timeout);
        fao.attr_valid_nsec = calc_timeout_nsec(priv->attribute_timeout);

        if (state->truncate_needed) {
            fuse_do_truncate(state);
        } else {
#if FUSE_KERNEL_MINOR_VERSION >= 9
            priv->proto_minor >= 9
                ? send_fuse_obj(this, finh, &fao)
                : send_fuse_data(this, finh, &fao, FUSE_COMPAT_ATTR_OUT_SIZE);
#else
            send_fuse_obj(this, finh, &fao);
#endif
            op_done = 1;
        }
    } else {
        gf_log("glusterfs-fuse", GF_LOG_WARNING,
               "%" PRIu64 ": %s() %s => -1 (%s)", frame->root->unique,
               gf_fop_list[frame->root->op],
               state->loc.path ? state->loc.path : "ERR", strerror(op_errno));

        /* facilitate retry from VFS */
        if ((state->fd == NULL) && (op_errno == ENOENT))
            op_errno = ESTALE;

        send_fuse_err(this, finh, op_errno);
        op_done = 1;
    }

    if (op_done) {
        free_fuse_state(state);
    }

    STACK_DESTROY(frame->root);

    return 0;
}

static int32_t
fattr_to_gf_set_attr(int32_t valid)
{
    int32_t gf_valid = 0;

    if (valid & FATTR_MODE)
        gf_valid |= GF_SET_ATTR_MODE;

    if (valid & FATTR_UID)
        gf_valid |= GF_SET_ATTR_UID;

    if (valid & FATTR_GID)
        gf_valid |= GF_SET_ATTR_GID;

    if (valid & FATTR_ATIME)
        gf_valid |= GF_SET_ATTR_ATIME;

    if (valid & FATTR_MTIME)
        gf_valid |= GF_SET_ATTR_MTIME;

#if FUSE_KERNEL_MINOR_VERSION >= 23
    if (valid & FATTR_CTIME)
        gf_valid |= GF_SET_ATTR_CTIME;
#endif

#if FUSE_KERNEL_MINOR_VERSION >= 9
    if (valid & FATTR_ATIME_NOW)
        gf_valid |= GF_ATTR_ATIME_NOW;

    if (valid & FATTR_MTIME_NOW)
        gf_valid |= GF_ATTR_MTIME_NOW;
#endif

    if (valid & FATTR_SIZE)
        gf_valid |= GF_SET_ATTR_SIZE;

    return gf_valid;
}

#define FATTR_MASK                                                             \
    (FATTR_SIZE | FATTR_UID | FATTR_GID | FATTR_ATIME | FATTR_MTIME |          \
     FATTR_MODE)

void
fuse_setattr_resume(fuse_state_t *state)
{
    if (!state->fd && !state->loc.inode) {
        gf_log("glusterfs-fuse", GF_LOG_ERROR,
               "%" PRIu64 ": SETATTR %" PRIu64 " (%s) resolution failed",
               state->finh->unique, state->finh->nodeid,
               uuid_utoa(state->resolve.gfid));

        /* facilitate retry from VFS */
        if ((state->fd == NULL) && (state->resolve.op_errno == ENOENT))
            state->resolve.op_errno = ESTALE;

        send_fuse_err(state->this, state->finh, state->resolve.op_errno);
        free_fuse_state(state);
        return;
    }

    gf_log("glusterfs-fuse", GF_LOG_TRACE,
           "%" PRIu64 ": SETATTR (%" PRIu64 ")%s", state->finh->unique,
           state->finh->nodeid, state->loc.path);

#ifdef GF_TEST_FFOP
    /* this is for calls like 'fchmod()' */
    if (!state->fd)
        state->fd = fd_lookup(state->loc.inode, state->finh->pid);
#endif /* GF_TEST_FFOP */

    if ((state->valid & (FATTR_MASK)) != FATTR_SIZE) {
        if (state->fd &&
            !((state->valid & FATTR_ATIME) || (state->valid & FATTR_MTIME)
#if FUSE_KERNEL_MINOR_VERSION >= 23
              || (state->valid & FATTR_CTIME)
#endif
                  )) {
            /*
                there is no "futimes" call, so don't send
                fsetattr if ATIME or MTIME is set
             */

            FUSE_FOP(state, fuse_setattr_cbk, GF_FOP_FSETATTR, fsetattr,
                     state->fd, &state->attr,
                     fattr_to_gf_set_attr(state->valid), state->xdata);
        } else {
            FUSE_FOP(state, fuse_setattr_cbk, GF_FOP_SETATTR, setattr,
                     &state->loc, &state->attr,
                     fattr_to_gf_set_attr(state->valid), state->xdata);
        }
    } else {
        fuse_do_truncate(state);
    }
}

static void
fuse_setattr(xlator_t *this, fuse_in_header_t *finh, void *msg,
             struct iobuf *iobuf)
{
    struct fuse_setattr_in *fsi = msg;

#if FUSE_KERNEL_MINOR_VERSION >= 9
    fuse_private_t *priv = NULL;
#endif
    fuse_state_t *state = NULL;

    GET_STATE(this, finh, state);

    if (fsi->valid & FATTR_FH && !(fsi->valid & (FATTR_ATIME | FATTR_MTIME))) {
        /* We need no loc if kernel sent us an fd and
         * we are not fiddling with times */
        state->fd = FH_TO_FD(fsi->fh);
        fuse_resolve_fd_init(state, &state->resolve, state->fd);
    } else {
        fuse_resolve_inode_init(state, &state->resolve, finh->nodeid);
    }

    /*
     * This is just stub code demonstrating how to retrieve
     * lock_owner in setattr, according to the FUSE proto.
     * We do not make use of ATM. Its purpose is supporting
     * mandatory locking, but getting that right is further
     * down the road. Cf.
     *
     * http://thread.gmane.org/gmane.comp.file-systems.fuse.devel/
     * 4962/focus=4982
     *
     * http://git.kernel.org/?p=linux/kernel/git/torvalds/
     * linux-2.6.git;a=commit;h=v2.6.23-5896-gf333211
     */
#if FUSE_KERNEL_MINOR_VERSION >= 9
    priv = this->private;
    if (priv->proto_minor >= 9 && fsi->valid & FATTR_LOCKOWNER)
        state->lk_owner = fsi->lock_owner;
#endif

    state->valid = fsi->valid;

    if ((fsi->valid & (FATTR_MASK)) != FATTR_SIZE) {
        if (fsi->valid & FATTR_SIZE) {
            state->off = fsi->size;
            state->truncate_needed = _gf_true;
        }

        state->attr.ia_size = fsi->size;
        state->attr.ia_atime = fsi->atime;
        state->attr.ia_mtime = fsi->mtime;
#if FUSE_KERNEL_MINOR_VERSION >= 23
        state->attr.ia_ctime = fsi->ctime;
#endif
        state->attr.ia_atime_nsec = fsi->atimensec;
        state->attr.ia_mtime_nsec = fsi->mtimensec;
#if FUSE_KERNEL_MINOR_VERSION >= 23
        state->attr.ia_ctime_nsec = fsi->ctimensec;
#endif

        state->attr.ia_prot = ia_prot_from_st_mode(fsi->mode);
        state->attr.ia_uid = fsi->uid;
        state->attr.ia_gid = fsi->gid;
    } else {
        state->off = fsi->size;
    }

    fuse_resolve_and_resume(state, fuse_setattr_resume);
}

static int
fuse_removexattr_cbk(call_frame_t *frame, void *cookie, xlator_t *this,
                     int32_t op_ret, int32_t op_errno, dict_t *xdata)
{
    fuse_state_t *state = NULL;
    fuse_in_header_t *finh = NULL;

    GF_ASSERT(frame);
    GF_ASSERT(frame->root);

    state = frame->root->state;
    finh = state->finh;

    fuse_log_eh_fop(this, state, frame, op_ret, op_errno);

    if (op_ret == 0) {
        gf_log("glusterfs-fuse", GF_LOG_TRACE, "%" PRIu64 ": %s() %s => 0",
               frame->root->unique, gf_fop_list[frame->root->op],
               state->loc.path ? state->loc.path : "ERR");

        send_fuse_err(this, finh, 0);
    } else {
        gf_log("glusterfs-fuse",
               (ENODATA == op_errno) ? GF_LOG_DEBUG : GF_LOG_WARNING,
               "%" PRIu64 ": %s() of %s on %s => -1 (%s)", frame->root->unique,
               gf_fop_list[frame->root->op], state->name ? state->name : "",
               state->loc.path ? state->loc.path : "ERR", strerror(op_errno));

        /* facilitate retry from VFS */
        if ((state->fd == NULL) && (op_errno == ENOENT))
            op_errno = ESTALE;

        send_fuse_err(this, finh, op_errno);
    }

    free_fuse_state(state);
    STACK_DESTROY(frame->root);

    return 0;
}

static int
fuse_err_cbk(call_frame_t *frame, void *cookie, xlator_t *this, int32_t op_ret,
             int32_t op_errno, dict_t *xdata)
{
    fuse_state_t *state = frame->root->state;
    fuse_in_header_t *finh = state->finh;

    fuse_log_eh_fop(this, state, frame, op_ret, op_errno);

    if (op_ret == 0) {
        gf_log("glusterfs-fuse", GF_LOG_TRACE, "%" PRIu64 ": %s() %s => 0",
               frame->root->unique, gf_fop_list[frame->root->op],
               state->loc.path ? state->loc.path : "ERR");

        send_fuse_err(this, finh, 0);
    } else {
        if (GF_IGNORE_IF_GSYNCD_SAFE_ERROR(frame, op_errno)) {
            gf_log("glusterfs-fuse", GF_LOG_WARNING,
                   "%" PRIu64 ": %s() %s => -1 (%s)", frame->root->unique,
                   gf_fop_list[frame->root->op],
                   state->loc.path ? state->loc.path : "ERR",
                   strerror(op_errno));
        }

        /* facilitate retry from VFS */
        if ((state->fd == NULL) && (op_errno == ENOENT))
            op_errno = ESTALE;

        send_fuse_err(this, finh, op_errno);
    }

    free_fuse_state(state);
    STACK_DESTROY(frame->root);

    return 0;
}

static int
fuse_flush_cbk(call_frame_t *frame, void *cookie, xlator_t *this,
               int32_t op_ret, int32_t op_errno, dict_t *xdata)
{
    fuse_private_t *priv = this->private;

    if (priv->flush_handle_interrupt) {
        if (fuse_interrupt_finish_fop(frame, this, _gf_false, NULL)) {
            return 0;
        }
    }

    return fuse_err_cbk(frame, cookie, this, op_ret, op_errno, xdata);
}

static int
fuse_fsync_cbk(call_frame_t *frame, void *cookie, xlator_t *this,
               int32_t op_ret, int32_t op_errno, struct iatt *prebuf,
               struct iatt *postbuf, dict_t *xdata)
{
    return fuse_err_cbk(frame, cookie, this, op_ret, op_errno, xdata);
}

static int
fuse_setxattr_cbk(call_frame_t *frame, void *cookie, xlator_t *this,
                  int32_t op_ret, int32_t op_errno, dict_t *xdata)
{
    if (op_ret == -1 && op_errno == ENOTSUP)
        GF_LOG_OCCASIONALLY(gf_fuse_xattr_enotsup_log, "glusterfs-fuse",
                            GF_LOG_CRITICAL,
                            "extended attribute not supported "
                            "by the backend storage");

    return fuse_err_cbk(frame, cookie, this, op_ret, op_errno, xdata);
}

static int
fuse_unlink_cbk(call_frame_t *frame, void *cookie, xlator_t *this,
                int32_t op_ret, int32_t op_errno, struct iatt *preparent,
                struct iatt *postparent, dict_t *xdata)
{
    fuse_state_t *state = NULL;
    fuse_in_header_t *finh = NULL;

    state = frame->root->state;
    finh = state->finh;

    fuse_log_eh(this,
                "op_ret: %d, op_errno: %d, %" PRIu64
                ": %s() %s => "
                "gfid: %s",
                op_ret, op_errno, frame->root->unique,
                gf_fop_list[frame->root->op], state->loc.path,
                state->loc.inode ? uuid_utoa(state->loc.inode->gfid) : "");

    if (op_ret == 0) {
        inode_unlink(state->loc.inode, state->loc.parent, state->loc.name);
        gf_log("glusterfs-fuse", GF_LOG_TRACE, "%" PRIu64 ": %s() %s => 0",
               frame->root->unique, gf_fop_list[frame->root->op],
               state->loc.path);

        send_fuse_err(this, finh, 0);
    } else {
        if (GF_IGNORE_IF_GSYNCD_SAFE_ERROR(frame, op_errno)) {
            gf_log("glusterfs-fuse",
                   op_errno == ENOTEMPTY ? GF_LOG_DEBUG : GF_LOG_WARNING,
                   "%" PRIu64 ": %s() %s => -1 (%s)", frame->root->unique,
                   gf_fop_list[frame->root->op], state->loc.path,
                   strerror(op_errno));
        }
        send_fuse_err(this, finh, op_errno);
    }

    free_fuse_state(state);
    STACK_DESTROY(frame->root);

    return 0;
}

void
fuse_access_resume(fuse_state_t *state)
{
    if (!state->loc.inode) {
        gf_log("glusterfs-fuse", GF_LOG_ERROR,
               "%" PRIu64 ": ACCESS %" PRIu64 " (%s) resolution failed",
               state->finh->unique, state->finh->nodeid,
               uuid_utoa(state->resolve.gfid));

        /* facilitate retry from VFS */
        if (state->resolve.op_errno == ENOENT)
            state->resolve.op_errno = ESTALE;

        send_fuse_err(state->this, state->finh, state->resolve.op_errno);
        free_fuse_state(state);
        return;
    }

    gf_log("glusterfs-fuse", GF_LOG_TRACE,
           "%" PRIu64 " ACCESS %s/%" PRIu64 " mask=%d", state->finh->unique,
           state->loc.path, state->finh->nodeid, state->mask);

    FUSE_FOP(state, fuse_err_cbk, GF_FOP_ACCESS, access, &state->loc,
             state->mask, state->xdata);
}

static void
fuse_access(xlator_t *this, fuse_in_header_t *finh, void *msg,
            struct iobuf *iobuf)
{
    struct fuse_access_in *fai = msg;
    fuse_state_t *state = NULL;

    GET_STATE(this, finh, state);

    fuse_resolve_inode_init(state, &state->resolve, finh->nodeid);

    state->mask = fai->mask;

    fuse_resolve_and_resume(state, fuse_access_resume);

    return;
}

static int
fuse_readlink_cbk(call_frame_t *frame, void *cookie, xlator_t *this,
                  int32_t op_ret, int32_t op_errno, const char *linkname,
                  struct iatt *buf, dict_t *xdata)
{
    fuse_state_t *state = NULL;
    fuse_in_header_t *finh = NULL;

    state = frame->root->state;
    finh = state->finh;

    fuse_log_eh(this,
                "op_ret: %d, op_errno: %d %" PRIu64
                ": %s() => %s"
                " linkname: %s, gfid: %s",
                op_ret, op_errno, frame->root->unique,
                gf_fop_list[frame->root->op], state->loc.gfid, linkname,
                uuid_utoa(state->loc.gfid));

    if (op_ret > 0) {
        gf_log("glusterfs-fuse", GF_LOG_TRACE,
               "%" PRIu64 ": %s => %s (size:%d)", frame->root->unique,
               state->loc.path, linkname, op_ret);
        send_fuse_data(this, finh, (void *)linkname, op_ret);
    } else {
        /* facilitate retry from VFS */
        if (op_errno == ENOENT)
            op_errno = ESTALE;

        gf_log("glusterfs-fuse", GF_LOG_WARNING, "%" PRIu64 ": %s => -1 (%s)",
               frame->root->unique, state->loc.path, strerror(op_errno));

        send_fuse_err(this, finh, op_errno);
    }

    free_fuse_state(state);
    STACK_DESTROY(frame->root);

    return 0;
}

void
fuse_readlink_resume(fuse_state_t *state)
{
    if (!state->loc.inode) {
        gf_log("glusterfs-fuse", GF_LOG_ERROR,
               "READLINK %" PRIu64 " (%s) resolution failed",
               state->finh->unique, uuid_utoa(state->resolve.gfid));

        /* facilitate retry from VFS */
        if (state->resolve.op_errno == ENOENT)
            state->resolve.op_errno = ESTALE;

        send_fuse_err(state->this, state->finh, state->resolve.op_errno);
        free_fuse_state(state);
        return;
    }

    gf_log("glusterfs-fuse", GF_LOG_TRACE, "%" PRIu64 " READLINK %s/%s",
           state->finh->unique, state->loc.path,
           uuid_utoa(state->loc.inode->gfid));

    FUSE_FOP(state, fuse_readlink_cbk, GF_FOP_READLINK, readlink, &state->loc,
             4096, state->xdata);
}

static void
fuse_readlink(xlator_t *this, fuse_in_header_t *finh, void *msg,
              struct iobuf *iobuf)
{
    fuse_state_t *state = NULL;

    GET_STATE(this, finh, state);

    fuse_resolve_inode_init(state, &state->resolve, finh->nodeid);

    fuse_resolve_and_resume(state, fuse_readlink_resume);

    return;
}

void
fuse_mknod_resume(fuse_state_t *state)
{
    if (!state->loc.parent) {
        gf_log("glusterfs-fuse", GF_LOG_ERROR,
               "MKNOD %" PRIu64 "/%s (%s/%s) resolution failed",
               state->finh->nodeid, state->resolve.bname,
               uuid_utoa(state->resolve.gfid), state->resolve.bname);

        /* facilitate retry from VFS */
        if (state->resolve.op_errno == ENOENT)
            state->resolve.op_errno = ESTALE;

        send_fuse_err(state->this, state->finh, state->resolve.op_errno);
        free_fuse_state(state);
        return;
    }

    if (state->resolve.op_errno == ENOENT) {
        state->resolve.op_ret = 0;
        state->resolve.op_errno = 0;
    }

    if (state->loc.inode) {
        gf_log(state->this->name, GF_LOG_DEBUG, "inode already present");
        inode_unref(state->loc.inode);
        state->loc.inode = NULL;
    }

    state->loc.inode = inode_new(state->loc.parent->table);

    gf_log("glusterfs-fuse", GF_LOG_TRACE, "%" PRIu64 ": MKNOD %s",
           state->finh->unique, state->loc.path);

    FUSE_FOP(state, fuse_newentry_cbk, GF_FOP_MKNOD, mknod, &state->loc,
             state->mode, state->rdev, state->umask, state->xdata);
}

static void
fuse_mknod(xlator_t *this, fuse_in_header_t *finh, void *msg,
           struct iobuf *iobuf)
{
    struct fuse_mknod_in *fmi = msg;
    char *name = (char *)(fmi + 1);

    fuse_state_t *state = NULL;
#if FUSE_KERNEL_MINOR_VERSION >= 12
    fuse_private_t *priv = NULL;

    priv = this->private;
    if (priv->proto_minor < 12)
        name = (char *)msg + FUSE_COMPAT_MKNOD_IN_SIZE;
#endif

    GET_STATE(this, finh, state);

    gf_uuid_generate(state->gfid);

    fuse_resolve_entry_init(state, &state->resolve, finh->nodeid, name);

    state->mode = fmi->mode;
    state->rdev = fmi->rdev;

#if FUSE_KERNEL_MINOR_VERSION >= 12
    priv = this->private;
    FUSE_ENTRY_CREATE(this, priv, finh, state, fmi, "MKNOD");
#endif

    fuse_resolve_and_resume(state, fuse_mknod_resume);

    return;
}

void
fuse_mkdir_resume(fuse_state_t *state)
{
    if (!state->loc.parent) {
        gf_log("glusterfs-fuse", GF_LOG_ERROR,
               "MKDIR %" PRIu64 " (%s/%s) resolution failed",
               state->finh->nodeid, uuid_utoa(state->resolve.gfid),
               state->resolve.bname);

        /* facilitate retry from VFS */
        if (state->resolve.op_errno == ENOENT)
            state->resolve.op_errno = ESTALE;

        send_fuse_err(state->this, state->finh, state->resolve.op_errno);
        free_fuse_state(state);
        return;
    }

    if (state->resolve.op_errno == ENOENT) {
        state->resolve.op_ret = 0;
        state->resolve.op_errno = 0;
    }

    if (state->loc.inode) {
        gf_log(state->this->name, GF_LOG_DEBUG, "inode already present");
        inode_unref(state->loc.inode);
        state->loc.inode = NULL;
    }

    state->loc.inode = inode_new(state->loc.parent->table);

    gf_log("glusterfs-fuse", GF_LOG_TRACE, "%" PRIu64 ": MKDIR %s",
           state->finh->unique, state->loc.path);

    FUSE_FOP(state, fuse_newentry_cbk, GF_FOP_MKDIR, mkdir, &state->loc,
             state->mode, state->umask, state->xdata);
}

static void
fuse_mkdir(xlator_t *this, fuse_in_header_t *finh, void *msg,
           struct iobuf *iobuf)
{
    struct fuse_mkdir_in *fmi = msg;
    char *name = (char *)(fmi + 1);
#if FUSE_KERNEL_MINOR_VERSION >= 12
    fuse_private_t *priv = NULL;
#endif

    fuse_state_t *state;

    GET_STATE(this, finh, state);

    gf_uuid_generate(state->gfid);

    fuse_resolve_entry_init(state, &state->resolve, finh->nodeid, name);

    state->mode = fmi->mode;

#if FUSE_KERNEL_MINOR_VERSION >= 12
    priv = this->private;
    FUSE_ENTRY_CREATE(this, priv, finh, state, fmi, "MKDIR");
#endif

    fuse_resolve_and_resume(state, fuse_mkdir_resume);

    return;
}

void
fuse_unlink_resume(fuse_state_t *state)
{
    if (!state->loc.parent || !state->loc.inode) {
        gf_log("glusterfs-fuse", GF_LOG_ERROR,
               "UNLINK %" PRIu64 " (%s/%s) resolution failed",
               state->finh->nodeid, uuid_utoa(state->resolve.gfid),
               state->resolve.bname);
        send_fuse_err(state->this, state->finh, state->resolve.op_errno);
        free_fuse_state(state);
        return;
    }

    gf_log("glusterfs-fuse", GF_LOG_TRACE, "%" PRIu64 ": UNLINK %s",
           state->finh->unique, state->loc.path);

    FUSE_FOP(state, fuse_unlink_cbk, GF_FOP_UNLINK, unlink, &state->loc, 0,
             state->xdata);
}

static void
fuse_unlink(xlator_t *this, fuse_in_header_t *finh, void *msg,
            struct iobuf *iobuf)
{
    char *name = msg;
    fuse_state_t *state = NULL;

    GET_STATE(this, finh, state);

    fuse_resolve_entry_init(state, &state->resolve, finh->nodeid, name);

    fuse_resolve_and_resume(state, fuse_unlink_resume);

    return;
}

void
fuse_rmdir_resume(fuse_state_t *state)
{
    if (!state->loc.parent || !state->loc.inode) {
        gf_log("glusterfs-fuse", GF_LOG_ERROR,
               "RMDIR %" PRIu64 " (%s/%s) resolution failed",
               state->finh->nodeid, uuid_utoa(state->resolve.gfid),
               state->resolve.bname);
        send_fuse_err(state->this, state->finh, state->resolve.op_errno);
        free_fuse_state(state);
        return;
    }

    gf_log("glusterfs-fuse", GF_LOG_TRACE, "%" PRIu64 ": RMDIR %s",
           state->finh->unique, state->loc.path);

    FUSE_FOP(state, fuse_unlink_cbk, GF_FOP_RMDIR, rmdir, &state->loc, 0,
             state->xdata);
}

static void
fuse_rmdir(xlator_t *this, fuse_in_header_t *finh, void *msg,
           struct iobuf *iobuf)
{
    char *name = msg;
    fuse_state_t *state = NULL;

    GET_STATE(this, finh, state);

    fuse_resolve_entry_init(state, &state->resolve, finh->nodeid, name);

    fuse_resolve_and_resume(state, fuse_rmdir_resume);

    return;
}

void
fuse_symlink_resume(fuse_state_t *state)
{
    if (!state->loc.parent) {
        gf_log("glusterfs-fuse", GF_LOG_ERROR,
               "SYMLINK %" PRIu64 " (%s/%s) -> %s resolution failed",
               state->finh->nodeid, uuid_utoa(state->resolve.gfid),
               state->resolve.bname, state->name);

        /* facilitate retry from VFS */
        if (state->resolve.op_errno == ENOENT)
            state->resolve.op_errno = ESTALE;

        send_fuse_err(state->this, state->finh, state->resolve.op_errno);
        free_fuse_state(state);
        return;
    }

    if (state->resolve.op_errno == ENOENT) {
        state->resolve.op_ret = 0;
        state->resolve.op_errno = 0;
    }

    if (state->loc.inode) {
        gf_log(state->this->name, GF_LOG_DEBUG, "inode already present");
        inode_unref(state->loc.inode);
        state->loc.inode = NULL;
    }

    state->loc.inode = inode_new(state->loc.parent->table);

    gf_log("glusterfs-fuse", GF_LOG_TRACE, "%" PRIu64 ": SYMLINK %s -> %s",
           state->finh->unique, state->loc.path, state->name);

    FUSE_FOP(state, fuse_newentry_cbk, GF_FOP_SYMLINK, symlink, state->name,
             &state->loc, state->umask, state->xdata);
}

static void
fuse_symlink(xlator_t *this, fuse_in_header_t *finh, void *msg,
             struct iobuf *iobuf)
{
    char *name = msg;
    char *linkname = name + strlen(name) + 1;
    fuse_state_t *state = NULL;

    GET_STATE(this, finh, state);

    gf_uuid_generate(state->gfid);

    fuse_resolve_entry_init(state, &state->resolve, finh->nodeid, name);

    state->name = gf_strdup(linkname);

    fuse_resolve_and_resume(state, fuse_symlink_resume);

    return;
}

int
fuse_rename_cbk(call_frame_t *frame, void *cookie, xlator_t *this,
                int32_t op_ret, int32_t op_errno, struct iatt *buf,
                struct iatt *preoldparent, struct iatt *postoldparent,
                struct iatt *prenewparent, struct iatt *postnewparent,
                dict_t *xdata)
{
    fuse_state_t *state = NULL;
    fuse_in_header_t *finh = NULL;
    char loc_uuid_str[64] = {0}, loc2_uuid_str[64] = {0};

    state = frame->root->state;
    finh = state->finh;

    fuse_log_eh(
        this,
        "op_ret: %d, op_errno: %d, %" PRIu64
        ": %s() "
        "path: %s parent: %s ==> path: %s parent: %s"
        "gfid: %s",
        op_ret, op_errno, frame->root->unique, gf_fop_list[frame->root->op],
        state->loc.path,
        (state->loc.parent ? uuid_utoa_r(state->loc.parent->gfid, loc_uuid_str)
                           : ""),
        state->loc2.path,
        (state->loc2.parent
             ? uuid_utoa_r(state->loc2.parent->gfid, loc2_uuid_str)
             : ""),
        state->loc.inode ? uuid_utoa(state->loc.inode->gfid) : "");

    /* need to check for loc->parent to keep clang-scan happy.
       It gets dereferenced below, and is checked for NULL above. */
    if ((op_ret == 0) && (state->loc.parent) && (state->loc.inode)) {
        gf_log("glusterfs-fuse", GF_LOG_TRACE,
               "%" PRIu64 ": %s -> %s => 0 (buf->ia_ino=%" PRIu64 ")",
               frame->root->unique, state->loc.path, state->loc2.path,
               buf->ia_ino);

        {
            /* ugly ugly - to stay blind to situation where
               rename happens on a new inode
            */
            buf->ia_type = state->loc.inode->ia_type;
        }
        buf->ia_blksize = this->ctx->page_size;

        inode_rename(state->loc.parent->table, state->loc.parent,
                     state->loc.name, state->loc2.parent, state->loc2.name,
                     state->loc.inode, buf);

        send_fuse_err(this, finh, 0);
    } else {
        gf_log("glusterfs-fuse", GF_LOG_WARNING,
               "%" PRIu64 ": %s -> %s => -1 (%s)", frame->root->unique,
               state->loc.path, state->loc2.path, strerror(op_errno));
        send_fuse_err(this, finh, op_errno);
    }

    free_fuse_state(state);
    STACK_DESTROY(frame->root);
    return 0;
}

void
fuse_rename_resume(fuse_state_t *state)
{
    char loc_uuid[64] = {
        0,
    };
    char loc2_uuid[64] = {
        0,
    };

    if (!state->loc.parent || !state->loc.inode) {
        gf_log("glusterfs-fuse", GF_LOG_ERROR,
               "RENAME %" PRIu64 " %s/%s -> %s/%s src resolution failed",
               state->finh->unique, uuid_utoa_r(state->resolve.gfid, loc_uuid),
               state->resolve.bname,
               uuid_utoa_r(state->resolve2.gfid, loc2_uuid),
               state->resolve2.bname);

        /* facilitate retry from VFS */
        if ((!state->loc.inode) && (state->resolve.op_errno == ENOENT))
            state->resolve.op_errno = ESTALE;

        send_fuse_err(state->this, state->finh, state->resolve.op_errno);
        free_fuse_state(state);
        return;
    }

    if (!state->loc2.parent) {
        gf_log("glusterfs-fuse", GF_LOG_ERROR,
               "RENAME %" PRIu64 " %s/%s -> %s/%s dst resolution failed",
               state->finh->unique, uuid_utoa_r(state->resolve.gfid, loc_uuid),
               state->resolve.bname,
               uuid_utoa_r(state->resolve2.gfid, loc2_uuid),
               state->resolve2.bname);

        send_fuse_err(state->this, state->finh, ESTALE);
        free_fuse_state(state);
        return;
    }

    state->resolve.op_ret = 0;
    state->resolve2.op_ret = 0;

    gf_log("glusterfs-fuse", GF_LOG_TRACE,
           "%" PRIu64 ": RENAME `%s (%s)' -> `%s (%s)'", state->finh->unique,
           state->loc.path, loc_uuid, state->loc2.path, loc2_uuid);

    FUSE_FOP(state, fuse_rename_cbk, GF_FOP_RENAME, rename, &state->loc,
             &state->loc2, state->xdata);
}

static void
fuse_rename(xlator_t *this, fuse_in_header_t *finh, void *msg,
            struct iobuf *iobuf)
{
    struct fuse_rename_in *fri = msg;
    char *oldname = (char *)(fri + 1);
    char *newname = oldname + strlen(oldname) + 1;
    fuse_state_t *state = NULL;

    GET_STATE(this, finh, state);

    fuse_resolve_entry_init(state, &state->resolve, finh->nodeid, oldname);

    fuse_resolve_entry_init(state, &state->resolve2, fri->newdir, newname);

    fuse_resolve_and_resume(state, fuse_rename_resume);

    return;
}

void
fuse_link_resume(fuse_state_t *state)
{
    if (!state->loc2.inode || !state->loc.parent) {
        gf_log("glusterfs-fuse", GF_LOG_WARNING,
               "fuse_loc_fill() failed %" PRIu64 ": LINK %s %s",
               state->finh->unique, state->loc2.path, state->loc.path);

        /* facilitate retry from VFS */
        if (!state->loc2.inode && (state->resolve.op_errno == ENOENT))
            state->resolve.op_errno = ESTALE;

        send_fuse_err(state->this, state->finh, state->resolve.op_errno);
        free_fuse_state(state);
        return;
    }

    state->resolve.op_ret = 0;
    state->resolve2.op_ret = 0;

    if (state->loc.inode) {
        inode_unref(state->loc.inode);
        state->loc.inode = NULL;
    }
    state->loc.inode = inode_ref(state->loc2.inode);

    gf_log("glusterfs-fuse", GF_LOG_TRACE, "%" PRIu64 ": LINK() %s -> %s",
           state->finh->unique, state->loc2.path, state->loc.path);

    FUSE_FOP(state, fuse_newentry_cbk, GF_FOP_LINK, link, &state->loc2,
             &state->loc, state->xdata);
}

static void
fuse_link(xlator_t *this, fuse_in_header_t *finh, void *msg,
          struct iobuf *iobuf)
{
    struct fuse_link_in *fli = msg;
    char *name = (char *)(fli + 1);
    fuse_state_t *state = NULL;

    GET_STATE(this, finh, state);

    fuse_resolve_inode_init(state, &state->resolve2, fli->oldnodeid);

    fuse_resolve_entry_init(state, &state->resolve, finh->nodeid, name);

    fuse_resolve_and_resume(state, fuse_link_resume);

    return;
}

static int
fuse_create_cbk(call_frame_t *frame, void *cookie, xlator_t *this,
                int32_t op_ret, int32_t op_errno, fd_t *fd, inode_t *inode,
                struct iatt *buf, struct iatt *preparent,
                struct iatt *postparent, dict_t *xdata)
{
    fuse_state_t *state = NULL;
    fuse_in_header_t *finh = NULL;
    fuse_private_t *priv = NULL;
    struct fuse_out_header fouh = {
        0,
    };
    struct fuse_entry_out feo = {
        0,
    };
    struct fuse_open_out foo = {
        0,
    };
    struct iovec iov_out[3];
    inode_t *linked_inode = NULL;
    uint64_t ctx_value = LOOKUP_NOT_NEEDED;

    state = frame->root->state;
    priv = this->private;
    finh = state->finh;
    foo.open_flags = 0;

    fuse_log_eh_fop(this, state, frame, op_ret, op_errno);

    if (op_ret >= 0) {
        foo.fh = (uintptr_t)fd;

        if (((priv->direct_io_mode == 2) &&
             ((state->flags & O_ACCMODE) != O_RDONLY)) ||
            (priv->direct_io_mode == 1) || direct_io_mode(xdata))
            foo.open_flags |= FOPEN_DIRECT_IO;

        gf_log("glusterfs-fuse", GF_LOG_TRACE,
               "%" PRIu64 ": %s() %s => %p (ino=%" PRIu64 ")",
               frame->root->unique, gf_fop_list[frame->root->op],
               state->loc.path, fd, buf->ia_ino);

        buf->ia_blksize = this->ctx->page_size;
        gf_fuse_stat2attr(buf, &feo.attr, priv->enable_ino32);

        linked_inode = inode_link(inode, state->loc.parent, state->loc.name,
                                  buf);

        if (linked_inode != inode) {
            /*
               VERY racy code (if used anywhere else)
               -- don't do this without understanding
            */
            inode_unref(fd->inode);
            fd->inode = inode_ref(linked_inode);
        } else {
            inode_ctx_set(linked_inode, this, &ctx_value);
        }

        inode_lookup(linked_inode);

        inode_unref(linked_inode);

        feo.nodeid = inode_to_fuse_nodeid(linked_inode);

        feo.entry_valid = calc_timeout_sec(priv->entry_timeout);
        feo.entry_valid_nsec = calc_timeout_nsec(priv->entry_timeout);
        feo.attr_valid = calc_timeout_sec(priv->attribute_timeout);
        feo.attr_valid_nsec = calc_timeout_nsec(priv->attribute_timeout);

        fouh.error = 0;
        iov_out[0].iov_base = &fouh;
        iov_out[1].iov_base = &feo;
#if FUSE_KERNEL_MINOR_VERSION >= 9
        iov_out[1].iov_len = priv->proto_minor >= 9
                                 ? sizeof(feo)
                                 : FUSE_COMPAT_ENTRY_OUT_SIZE;
#else
        iov_out[1].iov_len = sizeof(feo);
#endif
        iov_out[2].iov_base = &foo;
        iov_out[2].iov_len = sizeof(foo);

        if (send_fuse_iov(this, finh, iov_out, 3) == ENOENT) {
            gf_log("glusterfs-fuse", GF_LOG_DEBUG, "create(%s) got EINTR",
                   state->loc.path);
            inode_forget(inode, 1);
            gf_fd_put(priv->fdtable, state->fd_no);
            goto out;
        }

        fd_bind(fd);
    } else {
        /* facilitate retry from VFS */
        if (op_errno == ENOENT)
            op_errno = ESTALE;

        gf_log("glusterfs-fuse", GF_LOG_WARNING, "%" PRIu64 ": %s => -1 (%s)",
               finh->unique, state->loc.path, strerror(op_errno));

        send_fuse_err(this, finh, op_errno);
        gf_fd_put(priv->fdtable, state->fd_no);
    }
out:
    free_fuse_state(state);
    STACK_DESTROY(frame->root);

    return 0;
}

void
fuse_create_resume(fuse_state_t *state)
{
    fd_t *fd = NULL;
    fuse_private_t *priv = NULL;
    fuse_fd_ctx_t *fdctx = NULL;

    if (!state->loc.parent) {
        gf_log("glusterfs-fuse", GF_LOG_WARNING,
               "%" PRIu64 " CREATE %s/%s resolution failed",
               state->finh->unique, uuid_utoa(state->resolve.gfid),
               state->resolve.bname);

        /* facilitate retry from VFS */
        if (state->resolve.op_errno == ENOENT)
            state->resolve.op_errno = ESTALE;

        send_fuse_err(state->this, state->finh, state->resolve.op_errno);
        free_fuse_state(state);
        return;
    }

    if (state->resolve.op_errno == ENOENT) {
        state->resolve.op_ret = 0;
        state->resolve.op_errno = 0;
    }

    if (state->loc.inode) {
        gf_log(state->this->name, GF_LOG_DEBUG, "inode already present");
        inode_unref(state->loc.inode);
    }

    state->loc.inode = inode_new(state->loc.parent->table);

    fd = fd_create(state->loc.inode, state->finh->pid);
    if (fd == NULL) {
        gf_log("glusterfs-fuse", GF_LOG_WARNING,
               "%" PRIu64 " CREATE cannot create a new fd",
               state->finh->unique);
        send_fuse_err(state->this, state->finh, ENOMEM);
        free_fuse_state(state);
        return;
    }

    fdctx = fuse_fd_ctx_check_n_create(state->this, fd);
    if (fdctx == NULL) {
        gf_log("glusterfs-fuse", GF_LOG_WARNING,
               "%" PRIu64 " CREATE creation of fdctx failed",
               state->finh->unique);
        fd_unref(fd);
        send_fuse_err(state->this, state->finh, ENOMEM);
        free_fuse_state(state);
        return;
    }

    priv = state->this->private;

    state->fd_no = gf_fd_unused_get(priv->fdtable, fd);

    state->fd = fd_ref(fd);
    fd->flags = state->flags;

    gf_log("glusterfs-fuse", GF_LOG_TRACE, "%" PRIu64 ": CREATE %s",
           state->finh->unique, state->loc.path);

    FUSE_FOP(state, fuse_create_cbk, GF_FOP_CREATE, create, &state->loc,
             state->flags, state->mode, state->umask, fd, state->xdata);
}

static void
fuse_create(xlator_t *this, fuse_in_header_t *finh, void *msg,
            struct iobuf *iobuf)
{
#if FUSE_KERNEL_MINOR_VERSION >= 12
    struct fuse_create_in *fci = msg;
    fuse_private_t *priv = NULL;
#else
    struct fuse_open_in *fci = msg;
#endif
    char *name = (char *)(fci + 1);

    fuse_state_t *state = NULL;

#if FUSE_KERNEL_MINOR_VERSION >= 12
    priv = this->private;
    if (priv->proto_minor < 12)
        name = (char *)((struct fuse_open_in *)msg + 1);
#endif

    GET_STATE(this, finh, state);

    gf_uuid_generate(state->gfid);

    fuse_resolve_entry_init(state, &state->resolve, finh->nodeid, name);

    state->mode = fci->mode;
    state->flags = fci->flags;

#if FUSE_KERNEL_MINOR_VERSION >= 12
    priv = this->private;
    FUSE_ENTRY_CREATE(this, priv, finh, state, fci, "CREATE");
#endif
    fuse_resolve_and_resume(state, fuse_create_resume);

    return;
}

void
fuse_open_resume(fuse_state_t *state)
{
    fd_t *fd = NULL;
    fuse_private_t *priv = NULL;
    fuse_fd_ctx_t *fdctx = NULL;

    if (!state->loc.inode) {
        gf_log("glusterfs-fuse", GF_LOG_ERROR,
               "%" PRIu64 ": OPEN %s resolution failed", state->finh->unique,
               uuid_utoa(state->resolve.gfid));

        /* facilitate retry from VFS */
        if (state->resolve.op_errno == ENOENT)
            state->resolve.op_errno = ESTALE;

        send_fuse_err(state->this, state->finh, state->resolve.op_errno);
        free_fuse_state(state);
        return;
    }

    fd = fd_create(state->loc.inode, state->finh->pid);
    if (!fd) {
        gf_log("fuse", GF_LOG_ERROR, "fd is NULL");
        send_fuse_err(state->this, state->finh, ENOENT);
        free_fuse_state(state);
        return;
    }

    fdctx = fuse_fd_ctx_check_n_create(state->this, fd);
    if (fdctx == NULL) {
        gf_log("glusterfs-fuse", GF_LOG_WARNING,
               "%" PRIu64 ": OPEN creation of fdctx failed",
               state->finh->unique);
        fd_unref(fd);
        send_fuse_err(state->this, state->finh, ENOMEM);
        free_fuse_state(state);
        return;
    }

    priv = state->this->private;

    state->fd_no = gf_fd_unused_get(priv->fdtable, fd);
    state->fd = fd_ref(fd);
    fd->flags = state->flags;

    gf_log("glusterfs-fuse", GF_LOG_TRACE, "%" PRIu64 ": OPEN %s",
           state->finh->unique, state->loc.path);

    FUSE_FOP(state, fuse_fd_cbk, GF_FOP_OPEN, open, &state->loc, state->flags,
             fd, state->xdata);
}

static void
fuse_open(xlator_t *this, fuse_in_header_t *finh, void *msg,
          struct iobuf *iobuf)
{
    struct fuse_open_in *foi = msg;
    fuse_state_t *state = NULL;

    GET_STATE(this, finh, state);

    fuse_resolve_inode_init(state, &state->resolve, finh->nodeid);

    state->flags = foi->flags;

    fuse_resolve_and_resume(state, fuse_open_resume);

    return;
}

static int
fuse_readv_cbk(call_frame_t *frame, void *cookie, xlator_t *this,
               int32_t op_ret, int32_t op_errno, struct iovec *vector,
               int32_t count, struct iatt *stbuf, struct iobref *iobref,
               dict_t *xdata)
{
    fuse_state_t *state = NULL;
    fuse_in_header_t *finh = NULL;
    struct fuse_out_header fouh = {
        0,
    };
    struct iovec *iov_out = NULL;

    state = frame->root->state;
    finh = state->finh;

    fuse_log_eh_fop(this, state, frame, op_ret, op_errno);

    if (op_ret >= 0) {
        gf_log("glusterfs-fuse", GF_LOG_TRACE,
               "%" PRIu64 ": READ => %d/%" GF_PRI_SIZET ",%" PRId64 "/%" PRIu64,
               frame->root->unique, op_ret, state->size, state->off,
               stbuf->ia_size);

        iov_out = GF_CALLOC(count + 1, sizeof(*iov_out), gf_fuse_mt_iovec);
        if (iov_out) {
            fouh.error = 0;
            iov_out[0].iov_base = &fouh;
            memcpy(iov_out + 1, vector, count * sizeof(*iov_out));
            send_fuse_iov(this, finh, iov_out, count + 1);
            GF_FREE(iov_out);
        } else
            send_fuse_err(this, finh, ENOMEM);
    } else {
        gf_log("glusterfs-fuse", GF_LOG_WARNING,
               "%" PRIu64 ": READ => %d gfid=%s fd=%p (%s)",
               frame->root->unique, op_ret,
               (state->fd && state->fd->inode)
                   ? uuid_utoa(state->fd->inode->gfid)
                   : "nil",
               state->fd, strerror(op_errno));

        send_fuse_err(this, finh, op_errno);
    }

    free_fuse_state(state);
    STACK_DESTROY(frame->root);

    return 0;
}

void
fuse_readv_resume(fuse_state_t *state)
{
    gf_log("glusterfs-fuse", GF_LOG_TRACE,
           "%" PRIu64 ": READ (%p, size=%zu, offset=%" PRIu64 ")",
           state->finh->unique, state->fd, state->size, state->off);

    FUSE_FOP(state, fuse_readv_cbk, GF_FOP_READ, readv, state->fd, state->size,
             state->off, state->io_flags, state->xdata);
}

static void
fuse_readv(xlator_t *this, fuse_in_header_t *finh, void *msg,
           struct iobuf *iobuf)
{
    struct fuse_read_in *fri = msg;

#if FUSE_KERNEL_MINOR_VERSION >= 9
    fuse_private_t *priv = NULL;
#endif
    fuse_state_t *state = NULL;
    fd_t *fd = NULL;

    GET_STATE(this, finh, state);

    fd = FH_TO_FD(fri->fh);
    state->fd = fd;

    fuse_resolve_fd_init(state, &state->resolve, fd);

    /* See comment by similar code in fuse_settatr */
#if FUSE_KERNEL_MINOR_VERSION >= 9
    priv = this->private;
    if (priv->proto_minor >= 9 && fri->read_flags & FUSE_READ_LOCKOWNER)
        state->lk_owner = fri->lock_owner;
#endif

    state->size = fri->size;
    state->off = fri->offset;
    /* lets ignore 'fri->read_flags', but just consider 'fri->flags' */
#if FUSE_KERNEL_MINOR_VERSION >= 9
    state->io_flags = fri->flags;
#endif
    fuse_resolve_and_resume(state, fuse_readv_resume);
}

static int
fuse_writev_cbk(call_frame_t *frame, void *cookie, xlator_t *this,
                int32_t op_ret, int32_t op_errno, struct iatt *stbuf,
                struct iatt *postbuf, dict_t *xdata)
{
    fuse_state_t *state = NULL;
    fuse_in_header_t *finh = NULL;
    struct fuse_write_out fwo = {
        0,
    };

    state = frame->root->state;
    finh = state->finh;

    fuse_log_eh_fop(this, state, frame, op_ret, op_errno);

    if (op_ret >= 0) {
        gf_log("glusterfs-fuse", GF_LOG_TRACE,
               "%" PRIu64 ": WRITE => %d/%" GF_PRI_SIZET ",%" PRId64
               "/%" PRIu64,
               frame->root->unique, op_ret, state->size, state->off,
               stbuf->ia_size);

        fwo.size = op_ret;
        send_fuse_obj(this, finh, &fwo);
    } else {
        gf_log(
            "glusterfs-fuse", GF_LOG_WARNING,
            "%" PRIu64 ": WRITE => -1 gfid=%s fd=%p (%s)", frame->root->unique,
            (state->fd && state->fd->inode) ? uuid_utoa(state->fd->inode->gfid)
                                            : "nil",
            state->fd, strerror(op_errno));

        send_fuse_err(this, finh, op_errno);
    }

    free_fuse_state(state);
    STACK_DESTROY(frame->root);

    return 0;
}

void
fuse_write_resume(fuse_state_t *state)
{
    struct iobref *iobref = NULL;

    iobref = iobref_new();
    if (!iobref) {
        gf_log("glusterfs-fuse", GF_LOG_ERROR,
               "%" PRIu64 ": WRITE iobref allocation failed",
               state->finh->unique);
        send_fuse_err(state->this, state->finh, ENOMEM);

        free_fuse_state(state);
        return;
    }

    iobref_add(iobref, state->iobuf);

    gf_log("glusterfs-fuse", GF_LOG_TRACE,
           "%" PRIu64 ": WRITE (%p, size=%" GF_PRI_SIZET ", offset=%" PRId64
           ")",
           state->finh->unique, state->fd, state->size, state->off);

    FUSE_FOP(state, fuse_writev_cbk, GF_FOP_WRITE, writev, state->fd,
             &state->vector, 1, state->off, state->io_flags, iobref,
             state->xdata);

    iobref_unref(iobref);
}

static void
fuse_write(xlator_t *this, fuse_in_header_t *finh, void *msg,
           struct iobuf *iobuf)
{
    /* WRITE is special, metadata is attached to in_header,
     * and msg is the payload as-is.
     */
    struct fuse_write_in *fwi = (struct fuse_write_in *)(finh + 1);

    fuse_state_t *state = NULL;
    fd_t *fd = NULL;
#if FUSE_KERNEL_MINOR_VERSION >= 9
    fuse_private_t *priv = NULL;
    priv = this->private;
#endif

    GET_STATE(this, finh, state);
    fd = FH_TO_FD(fwi->fh);
    state->fd = fd;
    state->size = fwi->size;
    state->off = fwi->offset;

    /* lets ignore 'fwi->write_flags', but just consider 'fwi->flags' */
#if FUSE_KERNEL_MINOR_VERSION >= 9
    state->io_flags = fwi->flags;
#else
    state->io_flags = fwi->write_flags;
#endif
    /* TODO: may need to handle below flag
       (fwi->write_flags & FUSE_WRITE_CACHE);
    */

    fuse_resolve_fd_init(state, &state->resolve, fd);

    /* See comment by similar code in fuse_settatr */
#if FUSE_KERNEL_MINOR_VERSION >= 9
    priv = this->private;
    if (priv->proto_minor >= 9 && fwi->write_flags & FUSE_WRITE_LOCKOWNER)
        state->lk_owner = fwi->lock_owner;
#endif

    state->vector.iov_base = msg;
    state->vector.iov_len = fwi->size;
    state->iobuf = iobuf;

    fuse_resolve_and_resume(state, fuse_write_resume);

    return;
}

#if FUSE_KERNEL_MINOR_VERSION >= 28
static int
fuse_copy_file_range_cbk(call_frame_t *frame, void *cookie, xlator_t *this,
                         int32_t op_ret, int32_t op_errno, struct iatt *stbuf,
                         struct iatt *prebuf_dst, struct iatt *postbuf_dst,
                         dict_t *xdata)
{
    fuse_state_t *state = NULL;
    fuse_in_header_t *finh = NULL;
    /*
     * Fuse kernel module uses fuse_write_out itself as the
     * output collector. In fact, fuse_kernel.h in the upstream
     * kernel just defines the input structure fuse_copy_file_range_in
     * for the fop. So, just use the fuse_write_out to send the
     * response back to the kernel.
     */
    struct fuse_write_out fcfro = {
        0,
    };

    char src_gfid[GF_UUID_BUF_SIZE] = {0};
    char dst_gfid[GF_UUID_BUF_SIZE] = {0};

    state = frame->root->state;
    finh = state->finh;

    fuse_log_eh_fop(this, state, frame, op_ret, op_errno);

    if (op_ret >= 0) {
        gf_log("glusterfs-fuse", GF_LOG_TRACE,
               "%" PRIu64 ": WRITE => %d/%" GF_PRI_SIZET ",%" PRIu64
               " , %" PRIu64 " ,%" PRIu64 ",%" PRIu64,
               frame->root->unique, op_ret, state->size, state->off_in,
               state->off_out, stbuf->ia_size, postbuf_dst->ia_size);

        fcfro.size = op_ret;
        send_fuse_obj(this, finh, &fcfro);
    } else {
        if (state->fd && state->fd->inode)
            uuid_utoa_r(state->fd->inode->gfid, src_gfid);
        else
            snprintf(src_gfid, sizeof(src_gfid), "nil");

        if (state->fd_dst && state->fd_dst->inode)
            uuid_utoa_r(state->fd_dst->inode->gfid, dst_gfid);
        else
            snprintf(dst_gfid, sizeof(dst_gfid), "nil");

        gf_log("glusterfs-fuse", GF_LOG_WARNING,
               "%" PRIu64
               ": COPY_FILE_RANGE => -1 gfid_in=%s fd_in=%p "
               "gfid_out=%s fd_out=%p (%s)",
               frame->root->unique, src_gfid, state->fd, dst_gfid,
               state->fd_dst, strerror(op_errno));

        send_fuse_err(this, finh, op_errno);
    }

    free_fuse_state(state);
    STACK_DESTROY(frame->root);

    return 0;
}

void
fuse_copy_file_range_resume(fuse_state_t *state)
{
    char fd_uuid_str[64] = {0}, fd_dst_uuid_str[64] = {0};

    gf_log("glusterfs-fuse", GF_LOG_TRACE,
           "%" PRIu64
           ": COPY_FILE_RANGE "
           "(input fd: %p (gfid: %s), "
           "output fd: %p (gfid: %s) size=%zu, "
           "offset_in=%" PRIu64 ", offset_out=%" PRIu64 ")",
           state->finh->unique, state->fd,
           uuid_utoa_r(state->fd->inode->gfid, fd_uuid_str), state->fd_dst,
           uuid_utoa_r(state->fd_dst->inode->gfid, fd_dst_uuid_str),
           state->size, state->off_in, state->off_out);

    FUSE_FOP(state, fuse_copy_file_range_cbk, GF_FOP_COPY_FILE_RANGE,
             copy_file_range, state->fd, state->off_in, state->fd_dst,
             state->off_out, state->size, state->io_flags, state->xdata);
}

static void
fuse_copy_file_range(xlator_t *this, fuse_in_header_t *finh, void *msg,
                     struct iobuf *iobuf)
{
    struct fuse_copy_file_range_in *fcfri = msg;
    fuse_state_t *state = NULL;
    fd_t *fd_in = NULL;
    fd_t *fd_out = NULL;

    GET_STATE(this, finh, state);

    fd_in = FH_TO_FD(fcfri->fh_in);
    fd_out = FH_TO_FD(fcfri->fh_out);
    state->fd = fd_in;
    state->fd_dst = fd_out;

    fuse_resolve_fd_init(state, &state->resolve, fd_in);
    fuse_resolve_fd_init(state, &state->resolve2, fd_out);

    state->size = fcfri->len;
    state->off_in = fcfri->off_in;
    state->off_out = fcfri->off_out;
    state->io_flags = fcfri->flags;

    fuse_resolve_and_resume(state, fuse_copy_file_range_resume);
}
#endif /* FUSE_KERNEL_MINOR_VERSION >= 28 */

#if FUSE_KERNEL_MINOR_VERSION >= 24 && HAVE_SEEK_HOLE
static int
fuse_lseek_cbk(call_frame_t *frame, void *cookie, xlator_t *this,
               int32_t op_ret, int32_t op_errno, off_t offset, dict_t *xdata)
{
    fuse_state_t *state = frame->root->state;
    fuse_in_header_t *finh = state->finh;
    struct fuse_lseek_out flo = {
        0,
    };

    fuse_log_eh_fop(this, state, frame, op_ret, op_errno);

    if (op_ret >= 0) {
        flo.offset = offset;
        send_fuse_obj(this, finh, &flo);
    } else {
        send_fuse_err(this, finh, op_errno);
    }

    free_fuse_state(state);
    STACK_DESTROY(frame->root);

    return 0;
}

static void
fuse_lseek_resume(fuse_state_t *state)
{
    FUSE_FOP(state, fuse_lseek_cbk, GF_FOP_SEEK, seek, state->fd, state->off,
             state->whence, state->xdata);
}

static void
fuse_lseek(xlator_t *this, fuse_in_header_t *finh, void *msg,
           struct iobuf *iobuf)
{
    struct fuse_lseek_in *ffi = msg;
    fuse_state_t *state = NULL;

    GET_STATE(this, finh, state);
    state->fd = FH_TO_FD(ffi->fh);
    state->off = ffi->offset;

    switch (ffi->whence) {
        case SEEK_DATA:
            state->whence = GF_SEEK_DATA;
            break;
        case SEEK_HOLE:
            state->whence = GF_SEEK_HOLE;
            break;
        default:
            /* fuse should handle other whence internally */
            send_fuse_err(this, finh, EINVAL);
            free_fuse_state(state);
            return;
    }

    fuse_resolve_fd_init(state, &state->resolve, state->fd);
    fuse_resolve_and_resume(state, fuse_lseek_resume);
}
#endif /* FUSE_KERNEL_MINOR_VERSION >= 24 && HAVE_SEEK_HOLE */

void
fuse_flush_resume(fuse_state_t *state)
{
    FUSE_FOP(state, fuse_flush_cbk, GF_FOP_FLUSH, flush, state->fd,
             state->xdata);
}

static void
fuse_flush_interrupt_handler(xlator_t *this, fuse_interrupt_record_t *fir)
{
    gf_log("glusterfs-fuse", GF_LOG_DEBUG,
           "FLUSH unique %" PRIu64 ": interrupt handler triggered",
           fir->fuse_in_header.unique);

    fuse_interrupt_finish_interrupt(this, fir, INTERRUPT_HANDLED, _gf_false,
                                    NULL);
}

static void
fuse_flush(xlator_t *this, fuse_in_header_t *finh, void *msg,
           struct iobuf *iobuf)
{
    struct fuse_flush_in *ffi = msg;
    fuse_private_t *priv = NULL;

    fuse_state_t *state = NULL;
    fd_t *fd = NULL;

    GET_STATE(this, finh, state);
    fd = FH_TO_FD(ffi->fh);
    state->fd = fd;

    priv = this->private;
    if (priv->flush_handle_interrupt) {
        fuse_interrupt_record_t *fir = NULL;

        fir = fuse_interrupt_record_new(finh, fuse_flush_interrupt_handler);
        if (!fir) {
            send_fuse_err(this, finh, ENOMEM);

            gf_log("glusterfs-fuse", GF_LOG_ERROR,
                   "FLUSH unique %" PRIu64
                   ":"
                   " interrupt record allocation failed",
                   finh->unique);
            free_fuse_state(state);

            return;
        }
        fuse_interrupt_record_insert(this, fir);
    }

    fuse_resolve_fd_init(state, &state->resolve, fd);

    state->lk_owner = ffi->lock_owner;

    gf_log("glusterfs-fuse", GF_LOG_TRACE, "%" PRIu64 ": FLUSH %p",
           finh->unique, fd);

    fuse_resolve_and_resume(state, fuse_flush_resume);

    return;
}

int
fuse_internal_release(xlator_t *this, fd_t *fd)
{
    /* This is important we cleanup our context here to avoid a leak
       in case an error occurs and we get cleanup up by
       call_unwind_error->...->args_wipe instead of the normal path.
    */
    fuse_fd_ctx_destroy(this, fd);

    return 0;
}

static void
fuse_release(xlator_t *this, fuse_in_header_t *finh, void *msg,
             struct iobuf *iobuf)
{
    struct fuse_release_in *fri = msg;
    fd_t *fd = NULL;
    fuse_state_t *state = NULL;
    fuse_private_t *priv = NULL;

    GET_STATE(this, finh, state);
    fd = FH_TO_FD(fri->fh);
    if (!fd)
        goto out;

    state->fd = fd;

    priv = this->private;

    fuse_log_eh(this, "RELEASE(): finh->unique: %" PRIu64 ":, fd: %p, gfid: %s",
                finh->unique, fd, uuid_utoa(fd->inode->gfid));

    gf_log("glusterfs-fuse", GF_LOG_TRACE,
           "finh->unique: %" PRIu64 ": RELEASE %p", finh->unique, state->fd);

    fd_close(state->fd);

    fuse_fd_ctx_destroy(this, state->fd);
    fd_unref(fd);

    gf_fdptr_put(priv->fdtable, fd);

    state->fd = NULL;

out:
    send_fuse_err(this, finh, 0);

    free_fuse_state(state);
    return;
}

void
fuse_fsync_resume(fuse_state_t *state)
{
    gf_log("glusterfs-fuse", GF_LOG_TRACE, "%" PRIu64 ": FSYNC %p",
           state->finh->unique, state->fd);

    /* fsync_flags: 1 means "datasync" (no defines for this) */
    FUSE_FOP(state, fuse_fsync_cbk, GF_FOP_FSYNC, fsync, state->fd,
             (state->flags & 1), state->xdata);
}

static void
fuse_fsync(xlator_t *this, fuse_in_header_t *finh, void *msg,
           struct iobuf *iobuf)
{
    struct fuse_fsync_in *fsi = msg;

    fuse_state_t *state = NULL;
    fd_t *fd = NULL;

    GET_STATE(this, finh, state);
    fd = FH_TO_FD(fsi->fh);
    state->fd = fd;

    fuse_resolve_fd_init(state, &state->resolve, fd);

    state->flags = fsi->fsync_flags;
    fuse_resolve_and_resume(state, fuse_fsync_resume);
    return;
}

void
fuse_opendir_resume(fuse_state_t *state)
{
    fd_t *fd = NULL;
    fuse_private_t *priv = NULL;
    fuse_fd_ctx_t *fdctx = NULL;

    priv = state->this->private;

    if (!state->loc.inode) {
        gf_log("glusterfs-fuse", GF_LOG_WARNING,
               "%" PRIu64 ": OPENDIR (%s) resolution failed",
               state->finh->unique, uuid_utoa(state->resolve.gfid));

        /* facilitate retry from VFS */
        if (state->resolve.op_errno == ENOENT)
            state->resolve.op_errno = ESTALE;

        send_fuse_err(state->this, state->finh, state->resolve.op_errno);
        free_fuse_state(state);
        return;
    }

    fd = fd_create(state->loc.inode, state->finh->pid);
    if (fd == NULL) {
        gf_log("glusterfs-fuse", GF_LOG_WARNING,
               "%" PRIu64 ": OPENDIR fd creation failed", state->finh->unique);
        send_fuse_err(state->this, state->finh, ENOMEM);
        free_fuse_state(state);
        return;
    }

    fdctx = fuse_fd_ctx_check_n_create(state->this, fd);
    if (fdctx == NULL) {
        gf_log("glusterfs-fuse", GF_LOG_WARNING,
               "%" PRIu64 ": OPENDIR creation of fdctx failed",
               state->finh->unique);
        fd_unref(fd);
        send_fuse_err(state->this, state->finh, ENOMEM);
        free_fuse_state(state);
        return;
    }

    state->fd = fd_ref(fd);
    state->fd_no = gf_fd_unused_get(priv->fdtable, fd);

    gf_log("glusterfs-fuse", GF_LOG_TRACE, "%" PRIu64 ": OPENDIR %s",
           state->finh->unique, state->loc.path);

    FUSE_FOP(state, fuse_fd_cbk, GF_FOP_OPENDIR, opendir, &state->loc, fd,
             state->xdata);
}

static void
fuse_opendir(xlator_t *this, fuse_in_header_t *finh, void *msg,
             struct iobuf *iobuf)
{
    /*
    struct fuse_open_in *foi = msg;
     */

    fuse_state_t *state = NULL;

    GET_STATE(this, finh, state);

    fuse_resolve_inode_init(state, &state->resolve, finh->nodeid);

    fuse_resolve_and_resume(state, fuse_opendir_resume);
}

unsigned char
d_type_from_stat(struct iatt *buf)
{
    unsigned char d_type;

    if (IA_ISLNK(buf->ia_type)) {
        d_type = DT_LNK;

    } else if (IA_ISDIR(buf->ia_type)) {
        d_type = DT_DIR;

    } else if (IA_ISFIFO(buf->ia_type)) {
        d_type = DT_FIFO;

    } else if (IA_ISSOCK(buf->ia_type)) {
        d_type = DT_SOCK;

    } else if (IA_ISCHR(buf->ia_type)) {
        d_type = DT_CHR;

    } else if (IA_ISBLK(buf->ia_type)) {
        d_type = DT_BLK;

    } else if (IA_ISREG(buf->ia_type)) {
        d_type = DT_REG;

    } else {
        d_type = DT_UNKNOWN;
    }

    return d_type;
}

static int
fuse_readdir_cbk(call_frame_t *frame, void *cookie, xlator_t *this,
                 int32_t op_ret, int32_t op_errno, gf_dirent_t *entries,
                 dict_t *xdata)
{
    fuse_state_t *state = NULL;
    fuse_in_header_t *finh = NULL;
    size_t size = 0;
    size_t max_size = 0;
    char *buf = NULL;
    gf_dirent_t *entry = NULL;
    struct fuse_dirent *fde = NULL;
    fuse_private_t *priv = NULL;

    state = frame->root->state;
    finh = state->finh;
    priv = state->this->private;

    fuse_log_eh_fop(this, state, frame, op_ret, op_errno);

    if (op_ret < 0) {
        gf_log("glusterfs-fuse", GF_LOG_WARNING,
               "%" PRIu64 ": READDIR => -1 (%s)", frame->root->unique,
               strerror(op_errno));

        send_fuse_err(this, finh, op_errno);
        goto out;
    }

    gf_log("glusterfs-fuse", GF_LOG_TRACE,
           "%" PRIu64 ": READDIR => %d/%" GF_PRI_SIZET ",%" PRId64,
           frame->root->unique, op_ret, state->size, state->off);

    list_for_each_entry(entry, &entries->list, list)
    {
        size_t fde_size = FUSE_DIRENT_ALIGN(FUSE_NAME_OFFSET +
                                            strlen(entry->d_name));
        max_size += fde_size;

        if (max_size > state->size) {
            /* we received too many entries to fit in the reply */
            max_size -= fde_size;
            break;
        }
    }

    if (max_size == 0) {
        send_fuse_data(this, finh, 0, 0);
        goto out;
    }

    buf = GF_CALLOC(1, max_size, gf_fuse_mt_char);
    if (!buf) {
        gf_log("glusterfs-fuse", GF_LOG_DEBUG,
               "%" PRIu64 ": READDIR => -1 (%s)", frame->root->unique,
               strerror(ENOMEM));
        send_fuse_err(this, finh, ENOMEM);
        goto out;
    }

    size = 0;
    list_for_each_entry(entry, &entries->list, list)
    {
        fde = (struct fuse_dirent *)(buf + size);
        gf_fuse_fill_dirent(entry, fde, priv->enable_ino32);
        size += FUSE_DIRENT_SIZE(fde);

        if (size == max_size)
            break;
    }

    send_fuse_data(this, finh, buf, size);

    /* TODO: */
    /* gf_link_inodes_from_dirent (this, state->fd->inode, entries); */

out:
    free_fuse_state(state);
    STACK_DESTROY(frame->root);
    GF_FREE(buf);
    return 0;
}

void
fuse_readdir_resume(fuse_state_t *state)
{
    gf_log("glusterfs-fuse", GF_LOG_TRACE,
           "%" PRIu64 ": READDIR (%p, size=%" GF_PRI_SIZET ", offset=%" PRId64
           ")",
           state->finh->unique, state->fd, state->size, state->off);

    FUSE_FOP(state, fuse_readdir_cbk, GF_FOP_READDIR, readdir, state->fd,
             state->size, state->off, state->xdata);
}

static void
fuse_readdir(xlator_t *this, fuse_in_header_t *finh, void *msg,
             struct iobuf *iobuf)
{
    struct fuse_read_in *fri = msg;

    fuse_state_t *state = NULL;
    fd_t *fd = NULL;

    GET_STATE(this, finh, state);
    state->size = fri->size;
    state->off = fri->offset;
    fd = FH_TO_FD(fri->fh);
    state->fd = fd;

    fuse_resolve_fd_init(state, &state->resolve, fd);

    fuse_resolve_and_resume(state, fuse_readdir_resume);
}

#if FUSE_KERNEL_MINOR_VERSION >= 20
static int
fuse_readdirp_cbk(call_frame_t *frame, void *cookie, xlator_t *this,
                  int32_t op_ret, int32_t op_errno, gf_dirent_t *entries,
                  dict_t *xdata)
{
    fuse_state_t *state = NULL;
    fuse_in_header_t *finh = NULL;
    size_t max_size = 0;
    size_t size = 0;
    char *buf = NULL;
    gf_dirent_t *entry = NULL;
    struct fuse_direntplus *fde = NULL;
    struct fuse_entry_out *feo = NULL;
    fuse_private_t *priv = NULL;

    state = frame->root->state;
    finh = state->finh;
    priv = this->private;

    if (op_ret < 0) {
        gf_log("glusterfs-fuse", GF_LOG_WARNING,
               "%" PRIu64 ": READDIRP => -1 (%s)", frame->root->unique,
               strerror(op_errno));

        send_fuse_err(this, finh, op_errno);
        goto out;
    }

    gf_log("glusterfs-fuse", GF_LOG_TRACE,
           "%" PRIu64 ": READDIRP => %d/%" GF_PRI_SIZET ",%" PRId64,
           frame->root->unique, op_ret, state->size, state->off);

    list_for_each_entry(entry, &entries->list, list)
    {
        size_t fdes = FUSE_DIRENT_ALIGN(FUSE_NAME_OFFSET_DIRENTPLUS +
                                        strlen(entry->d_name));
        max_size += fdes;

        if (max_size > state->size) {
            /* we received too many entries to fit in the reply */
            max_size -= fdes;
            break;
        }
    }

    if (max_size == 0) {
        send_fuse_data(this, finh, 0, 0);
        goto out;
    }

    buf = GF_CALLOC(1, max_size, gf_fuse_mt_char);
    if (!buf) {
        gf_log("glusterfs-fuse", GF_LOG_DEBUG,
               "%" PRIu64 ": READDIRP => -1 (%s)", frame->root->unique,
               strerror(ENOMEM));
        send_fuse_err(this, finh, ENOMEM);
        goto out;
    }

    size = 0;
    list_for_each_entry(entry, &entries->list, list)
    {
        inode_t *linked_inode;

        fde = (struct fuse_direntplus *)(buf + size);
        feo = &fde->entry_out;

        if (priv->enable_ino32)
            fde->dirent.ino = GF_FUSE_SQUASH_INO(entry->d_ino);
        else
            fde->dirent.ino = entry->d_ino;

        fde->dirent.off = entry->d_off;
        fde->dirent.type = entry->d_type;
        fde->dirent.namelen = strlen(entry->d_name);
        (void)memcpy(fde->dirent.name, entry->d_name, fde->dirent.namelen);
        size += FUSE_DIRENTPLUS_SIZE(fde);

        if (!entry->inode)
            goto next_entry;

        entry->d_stat.ia_blksize = this->ctx->page_size;
        gf_fuse_stat2attr(&entry->d_stat, &feo->attr, priv->enable_ino32);

        linked_inode = inode_link(entry->inode, state->fd->inode, entry->d_name,
                                  &entry->d_stat);
        if (!linked_inode)
            goto next_entry;

        if (entry->inode != linked_inode) {
            memset(&entry->d_stat, 0, sizeof(entry->d_stat));
        }

        feo->nodeid = inode_to_fuse_nodeid(linked_inode);

        if (!((strcmp(entry->d_name, ".") == 0) ||
              (strcmp(entry->d_name, "..") == 0))) {
            inode_lookup(linked_inode);
        }

        inode_unref(linked_inode);

        feo->entry_valid = calc_timeout_sec(priv->entry_timeout);
        feo->entry_valid_nsec = calc_timeout_nsec(priv->entry_timeout);

        if (entry->d_stat.ia_ctime) {
            feo->attr_valid = calc_timeout_sec(priv->attribute_timeout);
            feo->attr_valid_nsec = calc_timeout_nsec(priv->attribute_timeout);
        } else {
            feo->attr_valid = feo->attr_valid_nsec = 0;
        }

    next_entry:
        if (size == max_size)
            break;
    }

    send_fuse_data(this, finh, buf, size);
out:
    free_fuse_state(state);
    STACK_DESTROY(frame->root);
    GF_FREE(buf);
    return 0;
}

void
fuse_readdirp_resume(fuse_state_t *state)
{
    gf_log("glusterfs-fuse", GF_LOG_TRACE,
           "%" PRIu64 ": READDIRP (%p, size=%" GF_PRI_SIZET ", offset=%" PRId64
           ")",
           state->finh->unique, state->fd, state->size, state->off);

    FUSE_FOP(state, fuse_readdirp_cbk, GF_FOP_READDIRP, readdirp, state->fd,
             state->size, state->off, state->xdata);
}

static void
fuse_readdirp(xlator_t *this, fuse_in_header_t *finh, void *msg,
              struct iobuf *iobuf)
{
    struct fuse_read_in *fri = msg;

    fuse_state_t *state = NULL;
    fd_t *fd = NULL;

    GET_STATE(this, finh, state);
    state->size = fri->size;
    state->off = fri->offset;
    fd = FH_TO_FD(fri->fh);
    state->fd = fd;

    fuse_resolve_fd_init(state, &state->resolve, fd);

    fuse_resolve_and_resume(state, fuse_readdirp_resume);
}
#endif

#if FUSE_KERNEL_MINOR_VERSION >= 19
#ifdef FALLOC_FL_KEEP_SIZE
static int
fuse_fallocate_cbk(call_frame_t *frame, void *cookie, xlator_t *this,
                   int32_t op_ret, int32_t op_errno, struct iatt *prebuf,
                   struct iatt *postbuf, dict_t *xdata)
{
    return fuse_err_cbk(frame, cookie, this, op_ret, op_errno, xdata);
}

static void
fuse_fallocate_resume(fuse_state_t *state)
{
    gf_log(
        "glusterfs-fuse", GF_LOG_TRACE,
        "%" PRIu64 ": FALLOCATE (%p, flags=%d, size=%zu, offset=%" PRId64 ")",
        state->finh->unique, state->fd, state->flags, state->size, state->off);

    if (state->flags & FALLOC_FL_PUNCH_HOLE)
        FUSE_FOP(state, fuse_fallocate_cbk, GF_FOP_DISCARD, discard, state->fd,
                 state->off, state->size, state->xdata);
    else
        FUSE_FOP(state, fuse_fallocate_cbk, GF_FOP_FALLOCATE, fallocate,
                 state->fd, (state->flags & FALLOC_FL_KEEP_SIZE), state->off,
                 state->size, state->xdata);
}

static void
fuse_fallocate(xlator_t *this, fuse_in_header_t *finh, void *msg,
               struct iobuf *iobuf)
{
    struct fuse_fallocate_in *ffi = msg;
    fuse_state_t *state = NULL;

    GET_STATE(this, finh, state);
    state->off = ffi->offset;
    state->size = ffi->length;
    state->flags = ffi->mode;
    state->fd = FH_TO_FD(ffi->fh);

    fuse_resolve_fd_init(state, &state->resolve, state->fd);
    fuse_resolve_and_resume(state, fuse_fallocate_resume);
}
#endif /* FALLOC_FL_KEEP_SIZE */
#endif /* FUSE minor version >= 19 */

static void
fuse_releasedir(xlator_t *this, fuse_in_header_t *finh, void *msg,
                struct iobuf *iobuf)
{
    struct fuse_release_in *fri = msg;
    fuse_state_t *state = NULL;
    fuse_private_t *priv = NULL;

    GET_STATE(this, finh, state);
    state->fd = FH_TO_FD(fri->fh);
    if (!state->fd)
        goto out;

    priv = this->private;

    fuse_log_eh(this,
                "RELEASEDIR (): finh->unique: %" PRIu64 ": fd: %p, gfid: %s",
                finh->unique, state->fd, uuid_utoa(state->fd->inode->gfid));

    gf_log("glusterfs-fuse", GF_LOG_TRACE,
           "finh->unique: %" PRIu64 ": RELEASEDIR %p", finh->unique, state->fd);

    fuse_fd_ctx_destroy(this, state->fd);
    fd_unref(state->fd);

    gf_fdptr_put(priv->fdtable, state->fd);

    state->fd = NULL;

out:
    send_fuse_err(this, finh, 0);

    free_fuse_state(state);

    return;
}

void
fuse_fsyncdir_resume(fuse_state_t *state)
{
    FUSE_FOP(state, fuse_err_cbk, GF_FOP_FSYNCDIR, fsyncdir, state->fd,
             (state->flags & 1), state->xdata);
}

static void
fuse_fsyncdir(xlator_t *this, fuse_in_header_t *finh, void *msg,
              struct iobuf *iobuf)
{
    struct fuse_fsync_in *fsi = msg;

    fuse_state_t *state = NULL;
    fd_t *fd = NULL;

    fd = FH_TO_FD(fsi->fh);

    GET_STATE(this, finh, state);
    state->fd = fd;

    fuse_resolve_fd_init(state, &state->resolve, fd);

    state->flags = fsi->fsync_flags;
    fuse_resolve_and_resume(state, fuse_fsyncdir_resume);

    return;
}

static int
fuse_statfs_cbk(call_frame_t *frame, void *cookie, xlator_t *this,
                int32_t op_ret, int32_t op_errno, struct statvfs *buf,
                dict_t *xdata)
{
    fuse_state_t *state = NULL;
    fuse_in_header_t *finh = NULL;
    fuse_private_t *priv = NULL;
    struct fuse_statfs_out fso = {
        {
            0,
        },
    };

    state = frame->root->state;
    priv = this->private;
    finh = state->finh;

    fuse_log_eh(this, "op_ret: %d, op_errno: %d, %" PRIu64 ": %s()", op_ret,
                op_errno, frame->root->unique, gf_fop_list[frame->root->op]);

    if (op_ret == 0) {
        fso.st.bsize = buf->f_bsize;
        fso.st.frsize = buf->f_frsize;
        fso.st.blocks = buf->f_blocks;
        fso.st.bfree = buf->f_bfree;
        fso.st.bavail = buf->f_bavail;
        fso.st.files = buf->f_files;
        fso.st.ffree = buf->f_ffree;
        fso.st.namelen = buf->f_namemax;

        priv->proto_minor >= 4
            ? send_fuse_obj(this, finh, &fso)
            : send_fuse_data(this, finh, &fso, FUSE_COMPAT_STATFS_SIZE);
    } else {
        /* facilitate retry from VFS */
        if (op_errno == ENOENT)
            op_errno = ESTALE;

        gf_log("glusterfs-fuse", GF_LOG_WARNING, "%" PRIu64 ": ERR => -1 (%s)",
               frame->root->unique, strerror(op_errno));

        send_fuse_err(this, finh, op_errno);
    }

    free_fuse_state(state);
    STACK_DESTROY(frame->root);

    return 0;
}

void
fuse_statfs_resume(fuse_state_t *state)
{
    if (!state->loc.inode) {
        gf_log("glusterfs-fuse", GF_LOG_WARNING,
               "%" PRIu64 ": STATFS (%s) resolution fail", state->finh->unique,
               uuid_utoa(state->resolve.gfid));

        /* facilitate retry from VFS */
        if (state->resolve.op_errno == ENOENT)
            state->resolve.op_errno = ESTALE;

        send_fuse_err(state->this, state->finh, state->resolve.op_errno);
        free_fuse_state(state);
        return;
    }

    gf_log("glusterfs-fuse", GF_LOG_TRACE, "%" PRIu64 ": STATFS",
           state->finh->unique);

    FUSE_FOP(state, fuse_statfs_cbk, GF_FOP_STATFS, statfs, &state->loc,
             state->xdata);
}

static void
fuse_statfs(xlator_t *this, fuse_in_header_t *finh, void *msg,
            struct iobuf *iobuf)
{
    fuse_state_t *state = NULL;

    GET_STATE(this, finh, state);

    fuse_resolve_inode_init(state, &state->resolve, finh->nodeid);

    fuse_resolve_and_resume(state, fuse_statfs_resume);
}

void
fuse_setxattr_resume(fuse_state_t *state)
{
    if (!state->loc.inode) {
        gf_log("glusterfs-fuse", GF_LOG_WARNING,
               "%" PRIu64 ": SETXATTR %s/%" PRIu64
               " (%s) "
               "resolution failed",
               state->finh->unique, uuid_utoa(state->resolve.gfid),
               state->finh->nodeid, state->name);

        /* facilitate retry from VFS */
        if (state->resolve.op_errno == ENOENT)
            state->resolve.op_errno = ESTALE;

        send_fuse_err(state->this, state->finh, state->resolve.op_errno);
        free_fuse_state(state);
        return;
    }

#ifdef GF_TEST_FFOP
    state->fd = fd_lookup(state->loc.inode, state->finh->pid);
#endif /* GF_TEST_FFOP */

    if (state->fd) {
        gf_log("glusterfs-fuse", GF_LOG_TRACE,
               "%" PRIu64 ": SETXATTR %p/%" PRIu64 " (%s)", state->finh->unique,
               state->fd, state->finh->nodeid, state->name);

        FUSE_FOP(state, fuse_setxattr_cbk, GF_FOP_FSETXATTR, fsetxattr,
                 state->fd, state->xattr, state->flags, state->xdata);
    } else {
        gf_log("glusterfs-fuse", GF_LOG_TRACE,
               "%" PRIu64 ": SETXATTR %s/%" PRIu64 " (%s)", state->finh->unique,
               state->loc.path, state->finh->nodeid, state->name);

        FUSE_FOP(state, fuse_setxattr_cbk, GF_FOP_SETXATTR, setxattr,
                 &state->loc, state->xattr, state->flags, state->xdata);
    }
}

static void
fuse_setxattr(xlator_t *this, fuse_in_header_t *finh, void *msg,
              struct iobuf *iobuf)
{
    struct fuse_setxattr_in *fsi = msg;
    char *name = (char *)(fsi + 1);
    char *value = name + strlen(name) + 1;
    struct fuse_private *priv = NULL;

    fuse_state_t *state = NULL;
    char *dict_value = NULL;
    int32_t ret = -1;
    int32_t op_errno = 0;
    char *newkey = NULL;

    priv = this->private;

    GET_STATE(this, finh, state);

#ifdef GF_DARWIN_HOST_OS
    if (fsi->position) {
        gf_log("glusterfs-fuse", GF_LOG_WARNING,
               "%" PRIu64 ": SETXATTR %s/%" PRIu64
               " (%s):"
               "refusing positioned setxattr",
               finh->unique, state->loc.path, finh->nodeid, name);
        op_errno = EINVAL;
        goto done;
    }
#endif

    if (fuse_ignore_xattr_set(priv, name)) {
        goto done;
    }

    if (!priv->acl) {
        if ((strcmp(name, POSIX_ACL_ACCESS_XATTR) == 0) ||
            (strcmp(name, POSIX_ACL_DEFAULT_XATTR) == 0)) {
            op_errno = EOPNOTSUPP;
            goto done;
        }
    }

    ret = fuse_check_selinux_cap_xattr(priv, name);
    if (ret) {
        op_errno = EOPNOTSUPP;
        goto done;
    }

    /* Check if the command is for changing the log
       level of process or specific xlator */
    ret = is_gf_log_command(this, name, value, fsi->size);
    if (ret >= 0) {
        op_errno = ret;
        goto done;
    }

    if (!strcmp("inode-invalidate", name)) {
        gf_log("fuse", GF_LOG_TRACE, "got request to invalidate %" PRIu64,
               finh->nodeid);
#if FUSE_KERNEL_MINOR_VERSION >= 11
        ret = fuse_invalidate_entry(this, finh->nodeid);
        if (ret)
            op_errno = EBUSY;
#endif
        goto done;
    }

    if (!strcmp(GFID_XATTR_KEY, name) || !strcmp(GF_XATTR_VOL_ID_KEY, name)) {
        op_errno = EPERM;
        goto done;
    }

    state->size = fsi->size;

    fuse_resolve_inode_init(state, &state->resolve, finh->nodeid);

    state->xattr = dict_new();
    if (!state->xattr) {
        gf_log("glusterfs-fuse", GF_LOG_ERROR,
               "%" PRIu64 ": SETXATTR dict allocation failed", finh->unique);
        op_errno = ENOMEM;
        goto done;
    }

    ret = fuse_flip_xattr_ns(priv, name, &newkey);
    if (ret) {
        op_errno = ENOMEM;
        goto done;
    }

    if (fsi->size > 0) {
        /*
         * Many translators expect setxattr values to be strings, but
         * neither dict_get_str nor data_to_str do any checking or
         * fixups to make sure that's the case.  To avoid nasty
         * surprises, allocate an extra byte and add a NUL here.
         */
        dict_value = GF_MALLOC(fsi->size + 1, gf_common_mt_char);
        if (dict_value == NULL) {
            gf_log("glusterfs-fuse", GF_LOG_ERROR,
                   "%" PRIu64 ": SETXATTR value allocation failed",
                   finh->unique);
            op_errno = ENOMEM;
            GF_FREE(newkey);
            goto done;
        }
        memcpy(dict_value, value, fsi->size);
        dict_value[fsi->size] = '\0';
    }
    ret = dict_set_dynptr(state->xattr, newkey, dict_value, fsi->size);
    if (ret < 0) {
        op_errno = -ret;
        GF_FREE(dict_value);
        GF_FREE(newkey);
        goto done;
    }

    state->flags = fsi->flags;
    state->name = newkey;

    fuse_resolve_and_resume(state, fuse_setxattr_resume);

    return;

done:
    send_fuse_err(this, finh, op_errno);
    free_fuse_state(state);
}

static void
send_fuse_xattr(xlator_t *this, fuse_in_header_t *finh, const char *value,
                size_t size, size_t expected)
{
    struct fuse_getxattr_out fgxo;

    /* linux kernel limits the size of xattr value to 64k */
    if (size > GLUSTERFS_XATTR_LEN_MAX)
        send_fuse_err(this, finh, E2BIG);
    else if (expected) {
        /* if callback for getxattr and asks for value */
        if (size > expected)
            /* reply would be bigger than
             * what was asked by kernel */
            send_fuse_err(this, finh, ERANGE);
        else
            send_fuse_data(this, finh, (void *)value, size);
    } else {
        fgxo.size = size;
        send_fuse_obj(this, finh, &fgxo);
    }
}

/* filter out xattrs that need not be visible on the
 * mount point. this is _specifically_ for geo-rep
 * as of now, to prevent Rsync from crying out loud
 * when it tries to setxattr() for selinux xattrs
 */
static int
fuse_filter_xattr(char *key)
{
    int need_filter = 0;
    struct fuse_private *priv = THIS->private;

    if ((priv->client_pid == GF_CLIENT_PID_GSYNCD) &&
        fnmatch("*.selinux*", key, FNM_PERIOD) == 0)
        need_filter = 1;

    return need_filter;
}

static int
fuse_xattr_cbk(call_frame_t *frame, void *cookie, xlator_t *this,
               int32_t op_ret, int32_t op_errno, dict_t *dict, dict_t *xdata)
{
    char *value = "";
    fuse_state_t *state = NULL;
    fuse_in_header_t *finh = NULL;
    data_t *value_data = NULL;
    int ret = -1;
    int32_t len = 0;
    int32_t len_next = 0;

    state = frame->root->state;
    finh = state->finh;

    fuse_log_eh_fop(this, state, frame, op_ret, op_errno);

    if (op_ret >= 0) {
        gf_log("glusterfs-fuse", GF_LOG_TRACE, "%" PRIu64 ": %s() %s => %d",
               frame->root->unique, gf_fop_list[frame->root->op],
               state->loc.path, op_ret);

        /* if successful */
        if (state->name) {
            /* if callback for getxattr */
            value_data = dict_get(dict, state->name);
            if (value_data) {
                ret = value_data->len; /* Don't return the value for '\0' */
                value = value_data->data;

                send_fuse_xattr(this, finh, value, ret, state->size);
                /* if(ret >...)...else if...else */
            } else {
                send_fuse_err(this, finh, ENODATA);
            } /* if(value_data)...else */
        } else {
            /* if callback for listxattr */
            /* we need to invoke fuse_filter_xattr() twice. Once
             * while counting size and then while filling buffer
             */
            len = dict_keys_join(NULL, 0, dict, fuse_filter_xattr);
            if (len < 0)
                goto out;

            value = alloca(len + 1);
            if (!value)
                goto out;

            len_next = dict_keys_join(value, len, dict, fuse_filter_xattr);
            if (len_next != len)
                gf_log(THIS->name, GF_LOG_ERROR, "sizes not equal %d != %d",
                       len, len_next);

            send_fuse_xattr(this, finh, value, len, state->size);
        } /* if(state->name)...else */
    } else {
        /* facilitate retry from VFS */
        if ((state->fd == NULL) && (op_errno == ENOENT)) {
            op_errno = ESTALE;
        }

        /* if failure - no need to check if listxattr or getxattr */
        if (op_errno != ENODATA && op_errno != ENOATTR) {
            if (op_errno == ENOTSUP) {
                GF_LOG_OCCASIONALLY(gf_fuse_xattr_enotsup_log, "glusterfs-fuse",
                                    GF_LOG_ERROR,
                                    "extended attribute not "
                                    "supported by the backend "
                                    "storage");
            } else {
                gf_log("glusterfs-fuse", GF_LOG_WARNING,
                       "%" PRIu64 ": %s(%s) %s => -1 (%s)", frame->root->unique,
                       gf_fop_list[frame->root->op], state->name,
                       state->loc.path, strerror(op_errno));
            }
        } else {
            gf_log("glusterfs-fuse", GF_LOG_DEBUG,
                   "%" PRIu64 ": %s(%s) %s => -1 (%s)", frame->root->unique,
                   gf_fop_list[frame->root->op], state->name, state->loc.path,
                   strerror(op_errno));
        } /* if(op_errno!= ENODATA)...else */

        send_fuse_err(this, finh, op_errno);
    } /* if(op_ret>=0)...else */

out:
    free_fuse_state(state);
    STACK_DESTROY(frame->root);

    return 0;
}

void
fuse_getxattr_resume(fuse_state_t *state)
{
    char *value = NULL;

    if (!state->loc.inode) {
        gf_log("glusterfs-fuse", GF_LOG_WARNING,
               "%" PRIu64 ": GETXATTR %s/%" PRIu64
               " (%s) "
               "resolution failed",
               state->finh->unique, uuid_utoa(state->resolve.gfid),
               state->finh->nodeid, state->name);

        /* facilitate retry from VFS */
        if (state->resolve.op_errno == ENOENT)
            state->resolve.op_errno = ESTALE;

        send_fuse_err(state->this, state->finh, state->resolve.op_errno);
        free_fuse_state(state);
        return;
    }

#ifdef GF_TEST_FFOP
    state->fd = fd_lookup(state->loc.inode, state->finh->pid);
#endif /* GF_TEST_FFOP */

    if (state->name && (strcmp(state->name, VIRTUAL_GFID_XATTR_KEY) == 0)) {
        /* send glusterfs gfid in binary form */

        value = GF_MALLOC(16 + 1, gf_common_mt_char);
        if (!value) {
            send_fuse_err(state->this, state->finh, ENOMEM);
            goto internal_out;
        }
        memcpy(value, state->loc.inode->gfid, 16);
        value[16] = '\0';

        send_fuse_xattr(THIS, state->finh, value, 16, state->size);
        GF_FREE(value);
    internal_out:
        free_fuse_state(state);
        return;
    }

    if (state->name && (strcmp(state->name, VIRTUAL_GFID_XATTR_KEY_STR) == 0)) {
        /* transform binary gfid to canonical form */

        value = GF_CALLOC(UUID_CANONICAL_FORM_LEN + 1, sizeof(char),
                          gf_common_mt_char);
        if (!value) {
            send_fuse_err(state->this, state->finh, ENOMEM);
            goto internal_out1;
        }
        uuid_utoa_r(state->loc.inode->gfid, value);

        send_fuse_xattr(THIS, state->finh, value, UUID_CANONICAL_FORM_LEN,
                        state->size);
        GF_FREE(value);
    internal_out1:
        free_fuse_state(state);
        return;
    }

    if (state->fd) {
        gf_log("glusterfs-fuse", GF_LOG_TRACE,
               "%" PRIu64 ": GETXATTR %p/%" PRIu64 " (%s)", state->finh->unique,
               state->fd, state->finh->nodeid, state->name);

        FUSE_FOP(state, fuse_xattr_cbk, GF_FOP_FGETXATTR, fgetxattr, state->fd,
                 state->name, state->xdata);
    } else {
        gf_log("glusterfs-fuse", GF_LOG_TRACE,
               "%" PRIu64 ": GETXATTR %s/%" PRIu64 " (%s)", state->finh->unique,
               state->loc.path, state->finh->nodeid, state->name);

        FUSE_FOP(state, fuse_xattr_cbk, GF_FOP_GETXATTR, getxattr, &state->loc,
                 state->name, state->xdata);
    }
}

static void
fuse_getxattr(xlator_t *this, fuse_in_header_t *finh, void *msg,
              struct iobuf *iobuf)
{
    struct fuse_getxattr_in *fgxi = msg;
    char *name = (char *)(fgxi + 1);
    fuse_state_t *state = NULL;
    struct fuse_private *priv = NULL;
    int rv = 0;
    int op_errno = EINVAL;
    char *newkey = NULL;
    int ret = 0;

    priv = this->private;
    GET_STATE(this, finh, state);

#ifdef GF_DARWIN_HOST_OS
    if (fgxi->position) {
        /* position can be used only for
         * resource fork queries which we
         * don't support anyway... so handling
         * it separately is just sort of a
         * matter of aesthetics, not strictly
         * necessary.
         */

        gf_log("glusterfs-fuse", GF_LOG_WARNING,
               "%" PRIu64 ": GETXATTR %s/%" PRIu64
               " (%s):"
               "refusing positioned getxattr",
               finh->unique, state->loc.path, finh->nodeid, name);
        op_errno = EINVAL;
        goto err;
    }
#endif

    if (!priv->acl) {
        if ((strcmp(name, POSIX_ACL_ACCESS_XATTR) == 0) ||
            (strcmp(name, POSIX_ACL_DEFAULT_XATTR) == 0)) {
            op_errno = ENOTSUP;
            goto err;
        }
    }

    ret = fuse_check_selinux_cap_xattr(priv, name);
    if (ret) {
        op_errno = ENODATA;
        goto err;
    }

    fuse_resolve_inode_init(state, &state->resolve, finh->nodeid);

    rv = fuse_flip_xattr_ns(priv, name, &newkey);
    if (rv) {
        op_errno = ENOMEM;
        goto err;
    }

    state->size = fgxi->size;
    state->name = newkey;

    fuse_resolve_and_resume(state, fuse_getxattr_resume);

    return;
err:
    send_fuse_err(this, finh, op_errno);
    free_fuse_state(state);
    return;
}

void
fuse_listxattr_resume(fuse_state_t *state)
{
    if (!state->loc.inode) {
        gf_log("glusterfs-fuse", GF_LOG_WARNING,
               "%" PRIu64 ": LISTXATTR %s/%" PRIu64 "resolution failed",
               state->finh->unique, uuid_utoa(state->resolve.gfid),
               state->finh->nodeid);

        /* facilitate retry from VFS */
        if (state->resolve.op_errno == ENOENT)
            state->resolve.op_errno = ESTALE;

        send_fuse_err(state->this, state->finh, state->resolve.op_errno);
        free_fuse_state(state);
        return;
    }

#ifdef GF_TEST_FFOP
    state->fd = fd_lookup(state->loc.inode, state->finh->pid);
#endif /* GF_TEST_FFOP */

    if (state->fd) {
        gf_log("glusterfs-fuse", GF_LOG_TRACE,
               "%" PRIu64 ": LISTXATTR %p/%" PRIu64, state->finh->unique,
               state->fd, state->finh->nodeid);

        FUSE_FOP(state, fuse_xattr_cbk, GF_FOP_FGETXATTR, fgetxattr, state->fd,
                 NULL, state->xdata);
    } else {
        gf_log("glusterfs-fuse", GF_LOG_TRACE,
               "%" PRIu64 ": LISTXATTR %s/%" PRIu64, state->finh->unique,
               state->loc.path, state->finh->nodeid);

        FUSE_FOP(state, fuse_xattr_cbk, GF_FOP_GETXATTR, getxattr, &state->loc,
                 NULL, state->xdata);
    }
}

static void
fuse_listxattr(xlator_t *this, fuse_in_header_t *finh, void *msg,
               struct iobuf *iobuf)
{
    struct fuse_getxattr_in *fgxi = msg;
    fuse_state_t *state = NULL;

    GET_STATE(this, finh, state);

    fuse_resolve_inode_init(state, &state->resolve, finh->nodeid);

    state->size = fgxi->size;

    fuse_resolve_and_resume(state, fuse_listxattr_resume);

    return;
}

void
fuse_removexattr_resume(fuse_state_t *state)
{
    if (!state->loc.inode) {
        gf_log("glusterfs-fuse", GF_LOG_DEBUG,
               "%" PRIu64 ": REMOVEXATTR %s/%" PRIu64
               " (%s) "
               "resolution failed",
               state->finh->unique, uuid_utoa(state->resolve.gfid),
               state->finh->nodeid, state->name);

        /* facilitate retry from VFS */
        if (state->resolve.op_errno == ENOENT)
            state->resolve.op_errno = ESTALE;

        send_fuse_err(state->this, state->finh, state->resolve.op_errno);
        free_fuse_state(state);
        return;
    }

#ifdef GF_TEST_FFOP
    state->fd = fd_lookup(state->loc.inode, state->finh->pid);
#endif /* GF_TEST_FFOP */

    if (state->fd) {
        gf_log("glusterfs-fuse", GF_LOG_TRACE,
               "%" PRIu64 ": REMOVEXATTR %p/%" PRIu64 " (%s)",
               state->finh->unique, state->fd, state->finh->nodeid,
               state->name);

        FUSE_FOP(state, fuse_removexattr_cbk, GF_FOP_FREMOVEXATTR, fremovexattr,
                 state->fd, state->name, state->xdata);
    } else {
        gf_log("glusterfs-fuse", GF_LOG_TRACE,
               "%" PRIu64 ": REMOVEXATTR %s/%" PRIu64 " (%s)",
               state->finh->unique, state->loc.path, state->finh->nodeid,
               state->name);

        FUSE_FOP(state, fuse_removexattr_cbk, GF_FOP_REMOVEXATTR, removexattr,
                 &state->loc, state->name, state->xdata);
    }
}

static void
fuse_removexattr(xlator_t *this, fuse_in_header_t *finh, void *msg,
                 struct iobuf *iobuf)
{
    char *name = msg;

    fuse_state_t *state = NULL;
    fuse_private_t *priv = NULL;
    int32_t ret = -1;
    char *newkey = NULL;

    if (!strcmp(GFID_XATTR_KEY, name) || !strcmp(GF_XATTR_VOL_ID_KEY, name)) {
        send_fuse_err(this, finh, EPERM);
        GF_FREE(finh);
        return;
    }

    priv = this->private;

    GET_STATE(this, finh, state);

    fuse_resolve_inode_init(state, &state->resolve, finh->nodeid);

    ret = fuse_flip_xattr_ns(priv, name, &newkey);
    if (ret) {
        send_fuse_err(this, finh, ENOMEM);
        free_fuse_state(state);
        return;
    }

    state->name = newkey;

    fuse_resolve_and_resume(state, fuse_removexattr_resume);
    return;
}

static int gf_fuse_lk_enosys_log;

static int
fuse_getlk_cbk(call_frame_t *frame, void *cookie, xlator_t *this,
               int32_t op_ret, int32_t op_errno, struct gf_flock *lock,
               dict_t *xdata)
{
    fuse_state_t *state = NULL;

    state = frame->root->state;
    struct fuse_lk_out flo = {
        {
            0,
        },
    };

    fuse_log_eh_fop(this, state, frame, op_ret, op_errno);

    if (op_ret == 0) {
        gf_log("glusterfs-fuse", GF_LOG_TRACE, "%" PRIu64 ": ERR => 0",
               frame->root->unique);
        flo.lk.type = lock->l_type;
        flo.lk.pid = lock->l_pid;
        if (lock->l_type == F_UNLCK)
            flo.lk.start = flo.lk.end = 0;
        else {
            flo.lk.start = lock->l_start;
            flo.lk.end = lock->l_len ? (lock->l_start + lock->l_len - 1)
                                     : OFFSET_MAX;
        }
        send_fuse_obj(this, state->finh, &flo);
    } else {
        if (op_errno == ENOSYS) {
            gf_fuse_lk_enosys_log++;
            if (!(gf_fuse_lk_enosys_log % GF_UNIVERSAL_ANSWER)) {
                gf_log("glusterfs-fuse", GF_LOG_ERROR,
                       "GETLK not supported. loading "
                       "'features/posix-locks' on server side "
                       "will add GETLK support.");
            }
        } else {
            gf_log("glusterfs-fuse", GF_LOG_WARNING,
                   "%" PRIu64 ": ERR => -1 (%s)", frame->root->unique,
                   strerror(op_errno));
        }
        send_fuse_err(this, state->finh, op_errno);
    }

    free_fuse_state(state);
    STACK_DESTROY(frame->root);

    return 0;
}

void
fuse_getlk_resume(fuse_state_t *state)
{
    gf_log("glusterfs-fuse", GF_LOG_TRACE, "%" PRIu64 ": GETLK %p",
           state->finh->unique, state->fd);

    FUSE_FOP(state, fuse_getlk_cbk, GF_FOP_LK, lk, state->fd, F_GETLK,
             &state->lk_lock, state->xdata);
}

static void
fuse_getlk(xlator_t *this, fuse_in_header_t *finh, void *msg,
           struct iobuf *iobuf)
{
    struct fuse_lk_in *fli = msg;

    fuse_state_t *state = NULL;
    fd_t *fd = NULL;

    fd = FH_TO_FD(fli->fh);
    GET_STATE(this, finh, state);
    state->fd = fd;

    fuse_resolve_fd_init(state, &state->resolve, fd);

    convert_fuse_file_lock(&fli->lk, &state->lk_lock, fli->owner);

    state->lk_owner = fli->owner;

    fuse_resolve_and_resume(state, fuse_getlk_resume);

    return;
}

static int
fuse_setlk_cbk(call_frame_t *frame, void *cookie, xlator_t *this,
               int32_t op_ret, int32_t op_errno, struct gf_flock *lock,
               dict_t *xdata)
{
    uint32_t op = 0;
    fuse_state_t *state = NULL;
    int ret = 0;

    ret = fuse_interrupt_finish_fop(frame, this, _gf_true, (void **)&state);
    GF_FREE(state->name);
    dict_unref(state->xdata);
    GF_FREE(state);
    if (ret) {
        return 0;
    }

    state = frame->root->state;
    op = state->finh->opcode;

    fuse_log_eh_fop(this, state, frame, op_ret, op_errno);

    if (op_ret == 0) {
        gf_log("glusterfs-fuse", GF_LOG_TRACE, "%" PRIu64 ": ERR => 0",
               frame->root->unique);
        fd_lk_insert_and_merge(state->fd,
                               (op == FUSE_SETLK) ? F_SETLK : F_SETLKW,
                               &state->lk_lock);

        send_fuse_err(this, state->finh, 0);
    } else {
        if (op_errno == ENOSYS) {
            gf_fuse_lk_enosys_log++;
            if (!(gf_fuse_lk_enosys_log % GF_UNIVERSAL_ANSWER)) {
                gf_log("glusterfs-fuse", GF_LOG_ERROR,
                       "SETLK not supported. loading "
                       "'features/posix-locks' on server side "
                       "will add SETLK support.");
            }
        } else if (op_errno == EAGAIN) {
            gf_log("glusterfs-fuse", GF_LOG_DEBUG,
                   "Returning EAGAIN Flock: "
                   "start=%llu, len=%llu, pid=%llu, lk-owner=%s",
                   (unsigned long long)state->lk_lock.l_start,
                   (unsigned long long)state->lk_lock.l_len,
                   (unsigned long long)state->lk_lock.l_pid,
                   lkowner_utoa(&frame->root->lk_owner));
        } else {
            gf_log("glusterfs-fuse", GF_LOG_WARNING,
                   "%" PRIu64 ": ERR => -1 (%s)", frame->root->unique,
                   strerror(op_errno));
        }

        send_fuse_err(this, state->finh, op_errno);
    }

    free_fuse_state(state);
    STACK_DESTROY(frame->root);

    return 0;
}

static int
fuse_setlk_interrupt_handler_cbk(call_frame_t *frame, void *cookie,
                                 xlator_t *this, int32_t op_ret,
                                 int32_t op_errno, dict_t *dict, dict_t *xdata)
{
    fuse_interrupt_state_t intstat = INTERRUPT_NONE;
    fuse_interrupt_record_t *fir = cookie;

    intstat = op_ret >= 0 ? INTERRUPT_HANDLED : INTERRUPT_SQUELCHED;

    fuse_interrupt_finish_interrupt(this, fir, intstat, _gf_true, NULL);

    STACK_DESTROY(frame->root);

    return 0;
}

static void
fuse_setlk_interrupt_handler(xlator_t *this, fuse_interrupt_record_t *fir)
{
    fuse_state_t *state = NULL;
    call_frame_t *frame = NULL;
    char *xattr_name = NULL;
    int ret = 0;

    gf_log("glusterfs-fuse", GF_LOG_DEBUG,
           "SETLK%s unique %" PRIu64 ": interrupt handler triggered",
           fir->fuse_in_header.opcode == FUSE_SETLK ? "" : "W",
           fir->fuse_in_header.unique);

    state = fir->data;

    ret = gf_asprintf(
        &xattr_name, GF_XATTR_CLRLK_CMD ".tposix.kblocked.%hd,%jd-%jd",
        state->lk_lock.l_whence, state->lk_lock.l_start, state->lk_lock.l_len);
    if (ret == -1) {
        xattr_name = NULL;
        goto err;
    }

    frame = get_call_frame_for_req(state);
    if (!frame) {
        goto err;
    }
    frame->root->state = state;
    frame->root->op = GF_FOP_GETXATTR;
    frame->op = GF_FOP_GETXATTR;
    state->name = xattr_name;

    STACK_WIND_COOKIE(frame, fuse_setlk_interrupt_handler_cbk, fir,
                      state->active_subvol,
                      state->active_subvol->fops->fgetxattr, state->fd,
                      xattr_name, state->xdata);

    return;

err:
    GF_FREE(xattr_name);
    fuse_interrupt_finish_interrupt(this, fir, INTERRUPT_SQUELCHED, _gf_false,
                                    (void **)&state);
    if (state) {
        dict_unref(state->xdata);
        GF_FREE(state);
    }
}

void
fuse_setlk_resume(fuse_state_t *state)
{
    fuse_interrupt_record_t *fir = NULL;
    fuse_state_t *state_clone = NULL;

    fir = fuse_interrupt_record_new(state->finh, fuse_setlk_interrupt_handler);
    state_clone = gf_memdup(state, sizeof(*state));
    if (state_clone) {
        state_clone->xdata = dict_new();
    }

    if (!fir || !state_clone || !state_clone->xdata) {
        if (fir) {
            GF_FREE(fir);
        }
        if (state_clone) {
            GF_FREE(state_clone);
        }
        send_fuse_err(state->this, state->finh, ENOMEM);

        gf_log("glusterfs-fuse", GF_LOG_ERROR,
               "SETLK%s unique %" PRIu64
               ":"
               " interrupt record allocation failed",
               state->finh->opcode == FUSE_SETLK ? "" : "W",
               state->finh->unique);
        free_fuse_state(state);

        return;
    }
    state_clone->name = NULL;
    fir->data = state_clone;
    fuse_interrupt_record_insert(state->this, fir);

    gf_log("glusterfs-fuse", GF_LOG_TRACE, "%" PRIu64 ": SETLK%s %p",
           state->finh->unique, state->finh->opcode == FUSE_SETLK ? "" : "W",
           state->fd);

    FUSE_FOP(state, fuse_setlk_cbk, GF_FOP_LK, lk, state->fd,
             state->finh->opcode == FUSE_SETLK ? F_SETLK : F_SETLKW,
             &state->lk_lock, state->xdata);
}

static void
fuse_setlk(xlator_t *this, fuse_in_header_t *finh, void *msg,
           struct iobuf *iobuf)
{
    struct fuse_lk_in *fli = msg;

    fuse_state_t *state = NULL;
    fd_t *fd = NULL;

    fd = FH_TO_FD(fli->fh);
    GET_STATE(this, finh, state);
    state->finh = finh;
    state->fd = fd;

    fuse_resolve_fd_init(state, &state->resolve, fd);

    convert_fuse_file_lock(&fli->lk, &state->lk_lock, fli->owner);

    state->lk_owner = fli->owner;

    fuse_resolve_and_resume(state, fuse_setlk_resume);

    return;
}

#if FUSE_KERNEL_MINOR_VERSION >= 11 && defined(HAVE_FUSE_NOTIFICATIONS)
static void *
notify_kernel_loop(void *data)
{
    uint32_t len = 0;
    ssize_t rv = 0;
    xlator_t *this = NULL;
    fuse_private_t *priv = NULL;
    fuse_invalidate_node_t *node = NULL;
    fuse_invalidate_node_t *tmp = NULL;
    struct fuse_out_header *pfoh = NULL;
    struct iovec iov_out = {
        0,
    };

    this = data;
    priv = this->private;

    for (;;) {
        pthread_mutex_lock(&priv->invalidate_mutex);
        {
            while (list_empty(&priv->invalidate_list))
                pthread_cond_wait(&priv->invalidate_cond,
                                  &priv->invalidate_mutex);

            node = list_entry(priv->invalidate_list.next,
                              fuse_invalidate_node_t, next);

            list_del_init(&node->next);
            priv->invalidate_count--;
        }
        pthread_mutex_unlock(&priv->invalidate_mutex);

        pfoh = (struct fuse_out_header *)node->inval_buf;
        memcpy(&len, &pfoh->len, sizeof(len));
        /*
         * a simple
         *         len = pfoh->len;
         * works on x86, but takes a multiple insn cycle hit
         * when pfoh->len is not correctly aligned, possibly
         * even stalling the insn pipeline.
         * Other architectures will not be so forgiving. If
         * we're lucky the memcpy will be inlined by the
         * compiler, and might be as fast or faster without
         * the risk of stalling the insn pipeline.
         */

        iov_out.iov_base = node->inval_buf;
        iov_out.iov_len = len;
        rv = sys_writev(priv->fd, &iov_out, 1);
        check_and_dump_fuse_W(priv, &iov_out, 1, rv, node->errnomask);

        GF_FREE(node);

        if (rv == -1 && errno == EBADF)
            break;

        if (rv != len && !(rv == -1 && errno == ENOENT)) {
            gf_log("glusterfs-fuse", GF_LOG_INFO, "len: %u, rv: %zd, errno: %d",
                   len, rv, errno);
        }
    }

    gf_log("glusterfs-fuse", GF_LOG_ERROR, "kernel notifier loop terminated");

    pthread_mutex_lock(&priv->invalidate_mutex);
    {
        priv->reverse_fuse_thread_started = _gf_false;
        list_for_each_entry_safe(node, tmp, &priv->invalidate_list, next)
        {
            list_del_init(&node->next);
            GF_FREE(node);
        }
        priv->invalidate_count = 0;
    }
    pthread_mutex_unlock(&priv->invalidate_mutex);

    return NULL;
}
#endif

static void *
timed_response_loop(void *data)
{
    ssize_t rv = 0;
    size_t len = 0;
    xlator_t *this = NULL;
    fuse_private_t *priv = NULL;
    fuse_timed_message_t *dmsg = NULL;
    fuse_timed_message_t *tmp = NULL;
    struct timespec now = {
        0,
    };
    struct timespec delta = {
        0,
    };
    struct iovec iovs[2] = {
        {
            0,
        },
    };

    this = data;
    priv = this->private;

    for (;;) {
        pthread_mutex_lock(&priv->timed_mutex);
        {
            while (list_empty(&priv->timed_list)) {
                pthread_cond_wait(&priv->timed_cond, &priv->timed_mutex);
            }

            dmsg = list_entry(priv->timed_list.next, fuse_timed_message_t,
                              next);
            list_for_each_entry(tmp, &priv->timed_list, next)
            {
                if (timespec_cmp(&tmp->scheduled_ts, &dmsg->scheduled_ts) < 0) {
                    dmsg = tmp;
                }
            }

            list_del_init(&dmsg->next);
        }
        pthread_mutex_unlock(&priv->timed_mutex);

        timespec_now(&now);
        if (timespec_cmp(&now, &dmsg->scheduled_ts) < 0) {
            timespec_sub(&now, &dmsg->scheduled_ts, &delta);
            nanosleep(&delta, NULL);
        }

        gf_log("glusterfs-fuse", GF_LOG_TRACE,
               "sending timed "
               "message of unique %" PRIu64,
               dmsg->fuse_out_header.unique);

        len = dmsg->fuse_out_header.len;
        iovs[0] = (struct iovec){&dmsg->fuse_out_header,
                                 sizeof(struct fuse_out_header)};
        iovs[1] = (struct iovec){dmsg->fuse_message_body,
                                 len - sizeof(struct fuse_out_header)};
        rv = sys_writev(priv->fd, iovs, 2);
        check_and_dump_fuse_W(priv, iovs, 2, rv, dmsg->errnomask);

        fuse_timed_message_free(dmsg);

        if (rv == -1 && errno == EBADF) {
            break;
        }

        if (rv != len && !(rv == -1 && errno == ENOENT)) {
            gf_log("glusterfs-fuse", GF_LOG_INFO,
                   "len: %zu, rv: %zd, errno: %d", len, rv, errno);
        }
    }

    gf_log("glusterfs-fuse", GF_LOG_ERROR, "timed response loop terminated");

    pthread_mutex_lock(&priv->timed_mutex);
    {
        priv->timed_response_fuse_thread_started = _gf_false;
        list_for_each_entry_safe(dmsg, tmp, &priv->timed_list, next)
        {
            list_del_init(&dmsg->next);
            fuse_timed_message_free(dmsg);
        }
    }
    pthread_mutex_unlock(&priv->timed_mutex);

    return NULL;
}

static void
fuse_init(xlator_t *this, fuse_in_header_t *finh, void *msg,
          struct iobuf *iobuf)
{
    struct fuse_init_in *fini = msg;
    struct fuse_init_out fino = {
        0,
    };
    fuse_private_t *priv = NULL;
    size_t size = 0;
    int ret = 0;
#if FUSE_KERNEL_MINOR_VERSION >= 9
    pthread_t messenger;
#endif
    pthread_t delayer;

    priv = this->private;

    if (priv->init_recvd) {
        gf_log("glusterfs-fuse", GF_LOG_ERROR, "got INIT after first message");

        sys_close(priv->fd);
        goto out;
    }

    priv->init_recvd = 1;

    if (fini->major != FUSE_KERNEL_VERSION) {
        gf_log("glusterfs-fuse", GF_LOG_ERROR,
               "unsupported FUSE protocol version %d.%d", fini->major,
               fini->minor);

        sys_close(priv->fd);
        goto out;
    }
    priv->proto_minor = fini->minor;

    fino.major = FUSE_KERNEL_VERSION;
    fino.minor = FUSE_KERNEL_MINOR_VERSION;
    fino.max_readahead = 1 << 17;
    fino.max_write = 1 << 17;
    fino.flags = FUSE_ASYNC_READ | FUSE_POSIX_LOCKS;
#if FUSE_KERNEL_MINOR_VERSION >= 17
    if (fini->minor >= 17)
        fino.flags |= FUSE_FLOCK_LOCKS;
#endif
#if FUSE_KERNEL_MINOR_VERSION >= 12
    if (fini->minor >= 12) {
        /* let fuse leave the umask processing to us, so that it does not
         * break extended POSIX ACL defaults on server */
        fino.flags |= FUSE_DONT_MASK;
    }
#endif
#if FUSE_KERNEL_MINOR_VERSION >= 9
    if (fini->minor >= 6 /* fuse_init_in has flags */ &&
        fini->flags & FUSE_BIG_WRITES) {
        /* no need for direct I/O mode by default if big writes are supported */
        if (priv->direct_io_mode == 2)
            priv->direct_io_mode = 0;
        fino.flags |= FUSE_BIG_WRITES;
    }

    /* Start the thread processing timed responses */
    ret = gf_thread_create(&delayer, NULL, timed_response_loop, this,
                           "fusedlyd");
    if (ret != 0) {
        gf_log("glusterfs-fuse", GF_LOG_ERROR,
               "failed to start timed response thread (%s)", strerror(errno));

        sys_close(priv->fd);
        goto out;
    }
    priv->timed_response_fuse_thread_started = _gf_true;

    /* Used for 'reverse invalidation of inode' */
#ifdef HAVE_FUSE_NOTIFICATIONS
    if (fini->minor >= 12) {
        ret = gf_thread_create(&messenger, NULL, notify_kernel_loop, this,
                               "fusenoti");
        if (ret != 0) {
            gf_log("glusterfs-fuse", GF_LOG_ERROR,
                   "failed to start messenger daemon (%s)", strerror(errno));

            sys_close(priv->fd);
            goto out;
        }
        priv->reverse_fuse_thread_started = _gf_true;
    } else
#endif
    {
        /*
         * FUSE minor < 12 does not implement invalidate notifications.
         * This mechanism is required for fopen-keep-cache to operate
         * correctly. Disable and warn the user.
         */
        if (priv->fopen_keep_cache) {
            gf_log("glusterfs-fuse", GF_LOG_WARNING,
                   "FUSE version "
                   "%d.%d does not support inval notifications. "
                   "fopen-keep-cache disabled.",
                   fini->major, fini->minor);
            priv->fopen_keep_cache = 0;
        }
    }

    if (fini->minor >= 13) {
        fino.max_background = priv->background_qlen;
        fino.congestion_threshold = priv->congestion_threshold;
    }
    if (fini->minor < 9)
        *priv->msg0_len_p = sizeof(*finh) + FUSE_COMPAT_WRITE_IN_SIZE;

    if (priv->use_readdirp) {
        if (fini->flags & FUSE_DO_READDIRPLUS)
            fino.flags |= FUSE_DO_READDIRPLUS;
    }
#endif
    if (priv->fopen_keep_cache == 2) {
        /* If user did not explicitly set --fopen-keep-cache[=off],
           then check if kernel support FUSE_AUTO_INVAL_DATA and ...
        */

        priv->fopen_keep_cache = 1;

#if FUSE_KERNEL_MINOR_VERSION >= 20
        if (fini->flags & FUSE_AUTO_INVAL_DATA) {
            /* ... enable fopen_keep_cache mode if supported.
             */
            gf_log("glusterfs-fuse", GF_LOG_DEBUG,
                   "Detected "
                   "support for FUSE_AUTO_INVAL_DATA. Enabling "
                   "fopen_keep_cache automatically.");

            if (priv->fuse_auto_inval)
                fino.flags |= FUSE_AUTO_INVAL_DATA;
        } else
#endif
        {
            if (priv->fuse_auto_inval) {
                gf_log("glusterfs-fuse", GF_LOG_DEBUG,
                       "No support for FUSE_AUTO_INVAL_DATA. Disabling "
                       "fopen_keep_cache.");
                /* ... else disable. */
                priv->fopen_keep_cache = 0;
            }
        }
    } else if (priv->fopen_keep_cache == 1) {
        /* If user explicitly set --fopen-keep-cache[=on],
           then enable FUSE_AUTO_INVAL_DATA if possible.
        */
#if FUSE_KERNEL_MINOR_VERSION >= 20
        if (priv->fuse_auto_inval && (fini->flags & FUSE_AUTO_INVAL_DATA)) {
            gf_log("glusterfs-fuse", GF_LOG_DEBUG,
                   "fopen_keep_cache "
                   "is explicitly set. Enabling FUSE_AUTO_INVAL_DATA");
            fino.flags |= FUSE_AUTO_INVAL_DATA;
        } else
#endif
        {
            gf_log("glusterfs-fuse", GF_LOG_WARNING,
                   "fopen_keep_cache "
                   "is explicitly set. Support for "
                   "FUSE_AUTO_INVAL_DATA is missing");
        }
    }

#if FUSE_KERNEL_MINOR_VERSION >= 22
    if (fini->flags & FUSE_ASYNC_DIO)
        fino.flags |= FUSE_ASYNC_DIO;
#endif

    size = sizeof(fino);
#if FUSE_KERNEL_MINOR_VERSION >= 23
    /* FUSE 7.23 and newer added attributes to the fuse_init_out struct */
    if (fini->minor < 23) {
        /* reduce the size, chop off unused attributes from &fino */
        size = FUSE_COMPAT_22_INIT_OUT_SIZE;
    }

    /* Writeback cache support */
    if (fini->minor >= 23) {
        if (priv->kernel_writeback_cache)
            fino.flags |= FUSE_WRITEBACK_CACHE;
        fino.time_gran = priv->attr_times_granularity;
    }
#endif

    ret = send_fuse_data(this, finh, &fino, size);
    if (ret == 0)
        gf_log("glusterfs-fuse", GF_LOG_INFO,
               "FUSE inited with protocol versions:"
               " glusterfs %d.%d kernel %d.%d",
               FUSE_KERNEL_VERSION, FUSE_KERNEL_MINOR_VERSION, fini->major,
               fini->minor);
    else {
        gf_log("glusterfs-fuse", GF_LOG_ERROR, "FUSE init failed (%s)",
               strerror(ret));

        sys_close(priv->fd);
    }

out:
    GF_FREE(finh);
}

static void
fuse_enosys(xlator_t *this, fuse_in_header_t *finh, void *msg,
            struct iobuf *iobuf)
{
    send_fuse_err(this, finh, ENOSYS);

    GF_FREE(finh);
}

static void
fuse_destroy(xlator_t *this, fuse_in_header_t *finh, void *msg,
             struct iobuf *iobuf)
{
    send_fuse_err(this, finh, 0);

    GF_FREE(finh);
}

int
fuse_first_lookup(xlator_t *this)
{
    fuse_private_t *priv = NULL;
    loc_t loc = {
        0,
    };
    xlator_t *xl = NULL;
    dict_t *dict = NULL;
    static uuid_t gfid = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1};
    int ret = -1;
    struct iatt iatt = {
        0,
    };

    priv = this->private;

    loc.path = "/";
    loc.name = "";
    loc.inode = fuse_ino_to_inode(1, this);
    gf_uuid_copy(loc.gfid, loc.inode->gfid);
    loc.parent = NULL;

    dict = dict_new();

    xl = priv->active_subvol;

    ret = dict_set_gfuuid(dict, "gfid-req", gfid, true);
    if (ret) {
        gf_log(xl->name, GF_LOG_ERROR, "failed to set 'gfid-req'");
        goto out;
    }

    ret = syncop_lookup(xl, &loc, &iatt, NULL, dict, NULL);
    DECODE_SYNCOP_ERR(ret);
    if (ret < 0) {
        gf_log(this->name, GF_LOG_ERROR, "first lookup on root failed (%s)",
               strerror(errno));
        /* NOTE: Treat it as an error case. */
        /* goto out; */ /* commented for preventing coverity warning */
    }
    /* Remove comment of above goto statement if you are adding any
       more code here, before 'out' label */

out:
    dict_unref(dict);
    inode_unref(loc.inode);

    return ret;
}

int
fuse_nameless_lookup(xlator_t *this, xlator_t *xl, uuid_t gfid, loc_t *loc)
{
    int ret = -1;
    dict_t *xattr_req = NULL;
    struct iatt iatt = {
        0,
    };
    inode_t *linked_inode = NULL;
    uint64_t ctx_value = LOOKUP_NOT_NEEDED;

    if ((loc == NULL) || (xl == NULL)) {
        ret = -EINVAL;
        goto out;
    }

    if (loc->inode == NULL) {
        loc->inode = inode_new(xl->itable);
        if (loc->inode == NULL) {
            ret = -ENOMEM;
            goto out;
        }
    }

    gf_uuid_copy(loc->gfid, gfid);

    xattr_req = dict_new();
    if (xattr_req == NULL) {
        ret = -ENOMEM;
        goto out;
    }

    ret = syncop_lookup(xl, loc, &iatt, NULL, xattr_req, NULL);
    if (ret < 0)
        goto out;

    linked_inode = inode_link(loc->inode, NULL, NULL, &iatt);
    if (linked_inode == loc->inode)
        inode_ctx_set(linked_inode, this, &ctx_value);

    inode_unref(loc->inode);
    loc->inode = linked_inode;

    ret = 0;
out:
    if (xattr_req != NULL) {
        dict_unref(xattr_req);
    }

    return ret;
}

int
fuse_migrate_fd_open(xlator_t *this, fd_t *basefd, fd_t *oldfd,
                     xlator_t *old_subvol, xlator_t *new_subvol)
{
    loc_t loc = {
        0,
    };
    fd_t *newfd = NULL, *old_activefd = NULL;
    fuse_fd_ctx_t *basefd_ctx = NULL;
    fuse_fd_ctx_t *newfd_ctx = NULL;
    int ret = 0, flags = 0;

    ret = inode_path(basefd->inode, NULL, (char **)&loc.path);
    if (ret < 0) {
        gf_log("glusterfs-fuse", GF_LOG_WARNING,
               "cannot construct path of gfid (%s) failed"
               "(old-subvolume:%s-%d new-subvolume:%s-%d)",
               uuid_utoa(basefd->inode->gfid), old_subvol->name,
               old_subvol->graph->id, new_subvol->name, new_subvol->graph->id);
        goto out;
    }

    gf_uuid_copy(loc.gfid, basefd->inode->gfid);

    loc.inode = inode_find(new_subvol->itable, basefd->inode->gfid);

    if (loc.inode == NULL) {
        ret = fuse_nameless_lookup(this, new_subvol, basefd->inode->gfid, &loc);
        if (ret < 0) {
            gf_log("glusterfs-fuse", GF_LOG_WARNING,
                   "name-less lookup of gfid (%s) failed (%s)"
                   "(old-subvolume:%s-%d new-subvolume:%s-%d)",
                   uuid_utoa(basefd->inode->gfid), strerror(-ret),
                   old_subvol->name, old_subvol->graph->id, new_subvol->name,
                   new_subvol->graph->id);
            ret = -1;
            goto out;
        }
    }

    basefd_ctx = fuse_fd_ctx_get(this, basefd);
    GF_VALIDATE_OR_GOTO("glusterfs-fuse", basefd_ctx, out);

    newfd = fd_create(loc.inode, basefd->pid);
    if (newfd == NULL) {
        gf_log("glusterfs-fuse", GF_LOG_WARNING,
               "cannot create new fd, hence not migrating basefd "
               "(ptr:%p inode-gfid:%s) "
               "(old-subvolume:%s-%d new-subvolume:%s-%d)",
               basefd, uuid_utoa(loc.inode->gfid), old_subvol->name,
               old_subvol->graph->id, new_subvol->name, new_subvol->graph->id);
        ret = -1;
        goto out;
    }

    newfd->flags = basefd->flags;
    if (newfd->lk_ctx)
        fd_lk_ctx_unref(newfd->lk_ctx);

    newfd->lk_ctx = fd_lk_ctx_ref(oldfd->lk_ctx);

    newfd_ctx = fuse_fd_ctx_check_n_create(this, newfd);
    GF_VALIDATE_OR_GOTO("glusterfs-fuse", newfd_ctx, out);

    if (IA_ISDIR(basefd->inode->ia_type)) {
        ret = syncop_opendir(new_subvol, &loc, newfd, NULL, NULL);
    } else {
        flags = basefd->flags & ~(O_CREAT | O_EXCL | O_TRUNC);
        ret = syncop_open(new_subvol, &loc, flags, newfd, NULL, NULL);
    }

    if (ret < 0) {
        gf_log("glusterfs-fuse", GF_LOG_WARNING,
               "open on basefd (ptr:%p inode-gfid:%s) failed (%s)"
               "(old-subvolume:%s-%d new-subvolume:%s-%d)",
               basefd, uuid_utoa(basefd->inode->gfid), strerror(-ret),
               old_subvol->name, old_subvol->graph->id, new_subvol->name,
               new_subvol->graph->id);
        ret = -1;
        goto out;
    }

    fd_bind(newfd);

    LOCK(&basefd->lock);
    {
        if (basefd_ctx->activefd != NULL) {
            old_activefd = basefd_ctx->activefd;
        }

        basefd_ctx->activefd = newfd;
    }
    UNLOCK(&basefd->lock);

    if (old_activefd != NULL) {
        fd_unref(old_activefd);
    }

    gf_log("glusterfs-fuse", GF_LOG_INFO,
           "migrated basefd (%p) to newfd (%p) (inode-gfid:%s)"
           "(old-subvolume:%s-%d new-subvolume:%s-%d)",
           basefd, newfd, uuid_utoa(basefd->inode->gfid), old_subvol->name,
           old_subvol->graph->id, new_subvol->name, new_subvol->graph->id);

    ret = 0;

out:
    loc_wipe(&loc);

    return ret;
}

int
fuse_migrate_locks(xlator_t *this, fd_t *basefd, fd_t *oldfd,
                   xlator_t *old_subvol, xlator_t *new_subvol)
{
    int ret = -1;
    dict_t *lockinfo = NULL;
    void *ptr = NULL;
    fd_t *newfd = NULL;
    fuse_fd_ctx_t *basefd_ctx = NULL;

    if (!oldfd->lk_ctx || fd_lk_ctx_empty(oldfd->lk_ctx))
        return 0;

    basefd_ctx = fuse_fd_ctx_get(this, basefd);
    GF_VALIDATE_OR_GOTO("glusterfs-fuse", basefd_ctx, out);

    LOCK(&basefd->lock);
    {
        newfd = fd_ref(basefd_ctx->activefd);
    }
    UNLOCK(&basefd->lock);

    ret = syncop_fgetxattr(old_subvol, oldfd, &lockinfo, GF_XATTR_LOCKINFO_KEY,
                           NULL, NULL);
    if (ret < 0) {
        gf_log(this->name, GF_LOG_WARNING,
               "getting lockinfo failed while migrating locks"
               "(oldfd:%p newfd:%p inode-gfid:%s)"
               "(old-subvol:%s-%d new-subvol:%s-%d)",
               oldfd, newfd, uuid_utoa(newfd->inode->gfid), old_subvol->name,
               old_subvol->graph->id, new_subvol->name, new_subvol->graph->id);
        ret = -1;
        goto out;
    }

    ret = dict_get_ptr(lockinfo, GF_XATTR_LOCKINFO_KEY, &ptr);
    if (ptr == NULL) {
        ret = 0;
        gf_log(this->name, GF_LOG_INFO,
               "No lockinfo present on any of the bricks "
               "(oldfd: %p newfd:%p inode-gfid:%s) "
               "(old-subvol:%s-%d new-subvol:%s-%d)",
               oldfd, newfd, uuid_utoa(newfd->inode->gfid), old_subvol->name,
               old_subvol->graph->id, new_subvol->name, new_subvol->graph->id);

        goto out;
    }

    ret = syncop_fsetxattr(new_subvol, newfd, lockinfo, 0, NULL, NULL);
    if (ret < 0) {
        gf_log(this->name, GF_LOG_WARNING,
               "migrating locks failed (oldfd:%p newfd:%p "
               "inode-gfid:%s) (old-subvol:%s-%d new-subvol:%s-%d)",
               oldfd, newfd, uuid_utoa(newfd->inode->gfid), old_subvol->name,
               old_subvol->graph->id, new_subvol->name, new_subvol->graph->id);
        ret = -1;
        goto out;
    }

out:
    if (newfd)
        fd_unref(newfd);

    if (lockinfo != NULL) {
        dict_unref(lockinfo);
    }

    return ret;
}

int
fuse_migrate_fd(xlator_t *this, fd_t *basefd, xlator_t *old_subvol,
                xlator_t *new_subvol)
{
    int ret = -1;
    char create_in_progress = 0;
    fuse_fd_ctx_t *basefd_ctx = NULL;
    fd_t *oldfd = NULL;
    dict_t *xdata = NULL;

    basefd_ctx = fuse_fd_ctx_get(this, basefd);
    GF_VALIDATE_OR_GOTO("glusterfs-fuse", basefd_ctx, out);

    LOCK(&basefd->lock);
    {
        oldfd = basefd_ctx->activefd ? basefd_ctx->activefd : basefd;
        fd_ref(oldfd);
    }
    UNLOCK(&basefd->lock);

    LOCK(&oldfd->inode->lock);
    {
        if (gf_uuid_is_null(oldfd->inode->gfid)) {
            create_in_progress = 1;
        } else {
            create_in_progress = 0;
        }
    }
    UNLOCK(&oldfd->inode->lock);

    if (create_in_progress) {
        gf_log("glusterfs-fuse", GF_LOG_INFO,
               "create call on fd (%p) is in progress "
               "(basefd-ptr:%p basefd-inode.gfid:%s), "
               "hence deferring migration till application does an "
               "fd based operation on this fd"
               "(old-subvolume:%s-%d, new-subvolume:%s-%d)",
               oldfd, basefd, uuid_utoa(basefd->inode->gfid), old_subvol->name,
               old_subvol->graph->id, new_subvol->name, new_subvol->graph->id);

        ret = 0;
        goto out;
    }

    if (oldfd->inode->table->xl == old_subvol) {
        if (IA_ISDIR(oldfd->inode->ia_type)) {
            ret = syncop_fsyncdir(old_subvol, oldfd, 0, NULL, NULL);
        } else {
            xdata = dict_new();
            if (!xdata || dict_set_int8(xdata, "last-fsync", 1)) {
                gf_log("glusterfs-fuse", GF_LOG_WARNING,
                       "last-fsync set failed (%s) on fd (%p)"
                       "(basefd:%p basefd-inode.gfid:%s) "
                       "(old-subvolume:%s-%d new-subvolume:%s-%d)",
                       strerror(ENOMEM), oldfd, basefd,
                       uuid_utoa(basefd->inode->gfid), old_subvol->name,
                       old_subvol->graph->id, new_subvol->name,
                       new_subvol->graph->id);
            }

            ret = syncop_fsync(old_subvol, oldfd, 0, NULL, NULL, xdata, NULL);
        }

        if (ret < 0) {
            gf_log("glusterfs-fuse", GF_LOG_WARNING,
                   "syncop_fsync(dir) failed (%s) on fd (%p)"
                   "(basefd:%p basefd-inode.gfid:%s) "
                   "(old-subvolume:%s-%d new-subvolume:%s-%d)",
                   strerror(-ret), oldfd, basefd,
                   uuid_utoa(basefd->inode->gfid), old_subvol->name,
                   old_subvol->graph->id, new_subvol->name,
                   new_subvol->graph->id);
        }
    } else {
        gf_log("glusterfs-fuse", GF_LOG_WARNING,
               "basefd (ptr:%p inode-gfid:%s) was not "
               "migrated during previous graph switch"
               "(old-subvolume:%s-%d new-subvolume: %s-%d)",
               basefd, basefd->inode->gfid, old_subvol->name,
               old_subvol->graph->id, new_subvol->name, new_subvol->graph->id);
    }

    ret = fuse_migrate_fd_open(this, basefd, oldfd, old_subvol, new_subvol);
    if (ret < 0) {
        gf_log(this->name, GF_LOG_WARNING,
               "open corresponding to "
               "basefd (ptr:%p inode-gfid:%s) in new graph failed "
               "(old-subvolume:%s-%d new-subvolume:%s-%d)",
               basefd, uuid_utoa(basefd->inode->gfid), old_subvol->name,
               old_subvol->graph->id, new_subvol->name, new_subvol->graph->id);
        goto out;
    }

    ret = fuse_migrate_locks(this, basefd, oldfd, old_subvol, new_subvol);
    if (ret < 0) {
        gf_log(this->name, GF_LOG_WARNING,
               "migrating locks from old-subvolume (%s-%d) to "
               "new-subvolume (%s-%d) failed (inode-gfid:%s oldfd:%p "
               "basefd:%p)",
               old_subvol->name, old_subvol->graph->id, new_subvol->name,
               new_subvol->graph->id, uuid_utoa(basefd->inode->gfid), oldfd,
               basefd);
    }
out:
    if (ret < 0) {
        gf_log(this->name, GF_LOG_WARNING,
               "migration of basefd "
               "(ptr:%p inode-gfid:%s) failed"
               "(old-subvolume:%s-%d new-subvolume:%s-%d)",
               basefd, oldfd ? uuid_utoa(oldfd->inode->gfid) : NULL,
               old_subvol->name, old_subvol->graph->id, new_subvol->name,
               new_subvol->graph->id);
    }

    fd_unref(oldfd);

    if (xdata)
        dict_unref(xdata);

    return ret;
}

int
fuse_handle_opened_fds(xlator_t *this, xlator_t *old_subvol,
                       xlator_t *new_subvol)
{
    fuse_private_t *priv = NULL;
    fdentry_t *fdentries = NULL;
    uint32_t count = 0;
    fdtable_t *fdtable = NULL;
    int i = 0;
    fd_t *fd = NULL;
    int32_t ret = 0;
    fuse_fd_ctx_t *fdctx = NULL;

    priv = this->private;

    fdtable = priv->fdtable;

    fdentries = gf_fd_fdtable_copy_all_fds(fdtable, &count);
    if (fdentries != NULL) {
        for (i = 0; i < count; i++) {
            fd = fdentries[i].fd;
            if (fd == NULL)
                continue;

            ret = fuse_migrate_fd(this, fd, old_subvol, new_subvol);

            fdctx = fuse_fd_ctx_get(this, fd);
            if (fdctx) {
                LOCK(&fd->lock);
                {
                    if (ret < 0) {
                        fdctx->migration_failed = 1;
                    } else {
                        fdctx->migration_failed = 0;
                    }
                }
                UNLOCK(&fd->lock);
            }
        }

        for (i = 0; i < count; i++) {
            fd = fdentries[i].fd;
            if (fd)
                fd_unref(fd);
        }

        GF_FREE(fdentries);
    }

    return 0;
}

static int
fuse_handle_blocked_locks(xlator_t *this, xlator_t *old_subvol,
                          xlator_t *new_subvol)
{
    return 0;
}

static int
fuse_graph_switch_task(void *data)
{
    fuse_graph_switch_args_t *args = NULL;

    args = data;
    if (args == NULL) {
        goto out;
    }

    /* don't change the order of handling open fds and blocked locks, since
     * the act of opening files also reacquires granted locks in new graph.
     */
    fuse_handle_opened_fds(args->this, args->old_subvol, args->new_subvol);

    fuse_handle_blocked_locks(args->this, args->old_subvol, args->new_subvol);

out:
    return 0;
}

fuse_graph_switch_args_t *
fuse_graph_switch_args_alloc(void)
{
    fuse_graph_switch_args_t *args = NULL;

    args = GF_CALLOC(1, sizeof(*args), gf_fuse_mt_graph_switch_args_t);
    if (args == NULL) {
        goto out;
    }

out:
    return args;
}

void
fuse_graph_switch_args_destroy(fuse_graph_switch_args_t *args)
{
    if (args == NULL) {
        goto out;
    }

    GF_FREE(args);
out:
    return;
}

int
fuse_handle_graph_switch(xlator_t *this, xlator_t *old_subvol,
                         xlator_t *new_subvol)
{
    call_frame_t *frame = NULL;
    int32_t ret = -1;
    fuse_graph_switch_args_t *args = NULL;

    frame = create_frame(this, this->ctx->pool);
    if (frame == NULL) {
        goto out;
    }

    args = fuse_graph_switch_args_alloc();
    if (args == NULL) {
        goto out;
    }

    args->this = this;
    args->old_subvol = old_subvol;
    args->new_subvol = new_subvol;

    ret = synctask_new(this->ctx->env, fuse_graph_switch_task, NULL, frame,
                       args);
    if (ret == -1) {
        gf_log(this->name, GF_LOG_WARNING,
               "starting sync-task to "
               "handle graph switch failed");
        goto out;
    }

    ret = 0;
out:
    if (args != NULL) {
        fuse_graph_switch_args_destroy(args);
    }

    if (frame != NULL) {
        STACK_DESTROY(frame->root);
    }

    return ret;
}

int
fuse_graph_sync(xlator_t *this)
{
    fuse_private_t *priv = NULL;
    int need_first_lookup = 0;
    int ret = 0;
    int new_graph_id = 0;
    xlator_t *old_subvol = NULL, *new_subvol = NULL;
    uint64_t winds_on_old_subvol = 0;

    priv = this->private;

    pthread_mutex_lock(&priv->sync_mutex);
    {
        if (!priv->next_graph)
            goto unlock;

        old_subvol = priv->active_subvol;
        new_subvol = priv->active_subvol = priv->next_graph->top;
        new_graph_id = priv->next_graph->id;
        priv->next_graph = NULL;
        need_first_lookup = 1;
        if (old_subvol) {
            priv->handle_graph_switch = _gf_true;
        }

        while (!priv->event_recvd) {
            ret = pthread_cond_wait(&priv->sync_cond, &priv->sync_mutex);
            if (ret != 0) {
                gf_log(this->name, GF_LOG_DEBUG,
                       "timedwait returned non zero value "
                       "ret: %d errno: %d",
                       ret, errno);
                break;
            }
        }
    }
unlock:
    pthread_mutex_unlock(&priv->sync_mutex);

    if (need_first_lookup) {
        gf_log("fuse", GF_LOG_INFO, "switched to graph %d", new_graph_id);
        fuse_first_lookup(this);
    }

    if ((old_subvol != NULL) && (new_subvol != NULL)) {
        fuse_handle_graph_switch(this, old_subvol, new_subvol);

        pthread_mutex_lock(&priv->sync_mutex);
        {
            old_subvol->switched = 1;
            winds_on_old_subvol = old_subvol->winds;
            priv->handle_graph_switch = _gf_false;
            pthread_cond_broadcast(&priv->migrate_cond);
        }
        pthread_mutex_unlock(&priv->sync_mutex);

        if (winds_on_old_subvol == 0) {
            xlator_notify(old_subvol, GF_EVENT_PARENT_DOWN, old_subvol, NULL);
        }
    }

    return 0;
}

int
fuse_get_mount_status(xlator_t *this)
{
    int kid_status = -1;
    fuse_private_t *priv = this->private;

    if (sys_read(priv->status_pipe[0], &kid_status, sizeof(kid_status)) < 0) {
        gf_log(this->name, GF_LOG_ERROR, "could not get mount status");
        kid_status = -1;
    }
    gf_log(this->name, GF_LOG_DEBUG, "mount status is %d", kid_status);

    sys_close(priv->status_pipe[0]);
    sys_close(priv->status_pipe[1]);
    return kid_status;
}

static void
fuse_dispatch(xlator_t *xl, gf_async_t *async)
{
    fuse_async_t *fasync;
    fuse_private_t *priv;
    fuse_in_header_t *finh;
    struct iobuf *iobuf;

    priv = xl->private;
    fasync = caa_container_of(async, fuse_async_t, async);
    finh = fasync->finh;
    iobuf = fasync->iobuf;

    priv->fuse_ops[finh->opcode](xl, finh, fasync->msg, iobuf);

    iobuf_unref(iobuf);
}

/* We need 512 extra buffer size for BATCH_FORGET fop. By tests, it is
 * found to be reduces 'REALLOC()' in the loop */
#define FUSE_EXTRA_ALLOC 512

static void *
fuse_thread_proc(void *data)
{
    char *mount_point = NULL;
    xlator_t *this = NULL;
    fuse_private_t *priv = NULL;
    ssize_t res = 0;
    struct iobuf *iobuf = NULL;
    fuse_in_header_t *finh = NULL;
    struct iovec iov_in[2] = {
        {
            0,
        },
    };

    void *msg = NULL;
    size_t msg0_size = sizeof(*finh) + sizeof(struct fuse_write_in);
    fuse_async_t *fasync;
    struct pollfd pfd[2] = {{
        0,
    }};
    uint32_t psize;

    this = data;
    priv = this->private;

    THIS = this;

    psize = ((struct iobuf_pool *)this->ctx->iobuf_pool)->default_page_size;
    priv->msg0_len_p = &msg0_size;

    for (;;) {
        /* THIS has to be reset here */
        THIS = this;

        pthread_mutex_lock(&priv->sync_mutex);
        {
            if (!priv->mount_finished) {
                memset(pfd, 0, sizeof(pfd));
                pfd[0].fd = priv->status_pipe[0];
                pfd[0].events = POLLIN | POLLHUP | POLLERR;
                pfd[1].fd = priv->fd;
                pfd[1].events = POLLIN | POLLHUP | POLLERR;
                if (poll(pfd, 2, -1) < 0) {
                    gf_log(this->name, GF_LOG_ERROR, "poll error %s",
                           strerror(errno));
                    pthread_mutex_unlock(&priv->sync_mutex);
                    break;
                }
                if (pfd[0].revents & POLLIN) {
                    if (fuse_get_mount_status(this) != 0) {
                        pthread_mutex_unlock(&priv->sync_mutex);
                        break;
                    }
                    priv->mount_finished = _gf_true;
                } else if (pfd[0].revents) {
                    gf_log(this->name, GF_LOG_ERROR,
                           "mount pipe closed without status");
                    pthread_mutex_unlock(&priv->sync_mutex);
                    break;
                }
                if (!pfd[1].revents) {
                    pthread_mutex_unlock(&priv->sync_mutex);
                    continue;
                }
            }
        }
        pthread_mutex_unlock(&priv->sync_mutex);

        /*
         * We don't want to block on readv while we're still waiting
         * for mount status.  That means we only want to get here if
         * mount_status is true (meaning that our wait completed
         * already) or if we already called poll(2) on priv->fd to
         * make sure it's ready.
         */

        if (priv->init_recvd)
            fuse_graph_sync(this);

        /* TODO: This place should always get maximum supported buffer
           size from 'fuse', which is as of today 128KB. If we bring in
           support for higher block sizes support, then we should be
           changing this one too */
        iobuf = iobuf_get(this->ctx->iobuf_pool);

        /* Add extra 512 byte to the first iov so that it can
         * accommodate "ordinary" non-write requests. It's not
         * guaranteed to be big enough, as SETXATTR and namespace
         * operations with very long names may grow behind it,
         * but it's good enough in most cases (and we can handle
         * rest via realloc). */
        iov_in[0].iov_base = GF_MALLOC(
            sizeof(fuse_async_t) + msg0_size + FUSE_EXTRA_ALLOC,
            gf_fuse_mt_iov_base);

        if (!iobuf || !iov_in[0].iov_base) {
            gf_log(this->name, GF_LOG_ERROR, "Out of memory");
            if (iobuf)
                iobuf_unref(iobuf);
            GF_FREE(iov_in[0].iov_base);
            sleep(10);
            continue;
        }

        iov_in[1].iov_base = iobuf->ptr;

        iov_in[0].iov_len = msg0_size;
        iov_in[1].iov_len = psize;

        res = sys_readv(priv->fd, iov_in, 2);

        if (res == -1) {
            if (errno == ENODEV || errno == EBADF) {
                gf_log("glusterfs-fuse", GF_LOG_DEBUG,
                       "terminating upon getting %s when "
                       "reading /dev/fuse",
                       errno == ENODEV ? "ENODEV" : "EBADF");
                fuse_log_eh(this,
                            "glusterfs-fuse: terminating"
                            " upon getting %s when "
                            "reading /dev/fuse",
                            errno == ENODEV ? "ENODEV" : "EBADF");
                break;
            }
            if (errno != EINTR) {
                gf_log("glusterfs-fuse", GF_LOG_WARNING,
                       "read from /dev/fuse returned -1 (%s)", strerror(errno));
                fuse_log_eh(this,
                            "glusterfs-fuse: read from "
                            "/dev/fuse returned -1 (%s)",
                            strerror(errno));
                if (errno == EPERM) {
                    /*
                     * sleep a while to avoid busy looping
                     * on EPERM condition
                     */
                    nanosleep(
                        &(struct timespec){0,
                                           priv->fuse_dev_eperm_ratelimit_ns},
                        NULL);
                }
            }

            goto cont_err;
        }
        if (res < sizeof(*finh)) {
            gf_log("glusterfs-fuse", GF_LOG_WARNING, "short read on /dev/fuse");
            fuse_log_eh(this,
                        "glusterfs-fuse: short read on "
                        "/dev/fuse");
            break;
        }

        finh = (fuse_in_header_t *)iov_in[0].iov_base;

        if (res != finh->len
#ifdef GF_DARWIN_HOST_OS
            /* work around fuse4bsd/MacFUSE msg size miscalculation bug,
             * that is, payload size is not taken into account for
             * buffered writes
             */
            && !(finh->opcode == FUSE_WRITE &&
                 finh->len == sizeof(*finh) + sizeof(struct fuse_write_in) &&
                 res == finh->len + ((struct fuse_write_in *)(finh + 1))->size)
#endif
        ) {
            gf_log("glusterfs-fuse", GF_LOG_WARNING,
                   "inconsistent read on /dev/fuse");
            fuse_log_eh(this,
                        "glusterfs-fuse: inconsistent read "
                        "on /dev/fuse");
            break;
        }

        /*
         * This can be moved around a bit, but it's important to do it
         * *after* the readv.  Otherwise, a graph switch could occur
         * while we're in readv and we'll process the next request on
         * the old graph before we come to the part of the loop above
         * readv and check again.  That would be wrong.
         */
        if (priv->init_recvd)
            fuse_graph_sync(this);

        if (finh->opcode == FUSE_WRITE)
            msg = iov_in[1].iov_base;
        else {
            if (res > msg0_size + FUSE_EXTRA_ALLOC) {
                void *b = GF_REALLOC(iov_in[0].iov_base,
                                     sizeof(fuse_async_t) + res);
                if (b) {
                    iov_in[0].iov_base = b;
                    finh = (fuse_in_header_t *)iov_in[0].iov_base;
                } else {
                    gf_log("glusterfs-fuse", GF_LOG_ERROR, "Out of memory");
                    send_fuse_err(this, finh, ENOMEM);

                    goto cont_err;
                }
            }

            if (res > iov_in[0].iov_len) {
                memcpy(iov_in[0].iov_base + iov_in[0].iov_len,
                       iov_in[1].iov_base, res - iov_in[0].iov_len);
                iov_in[0].iov_len = res;
            }

            msg = finh + 1;
        }
        if (priv->uid_map_root && finh->uid == priv->uid_map_root)
            finh->uid = 0;

        if (finh->opcode >= FUSE_OP_HIGH) {
            /* turn down MacFUSE specific messages */
            fuse_enosys(this, finh, msg, NULL);
            iobuf_unref(iobuf);
        } else {
            fasync = iov_in[0].iov_base + iov_in[0].iov_len;
            fasync->finh = finh;
            fasync->msg = msg;
            fasync->iobuf = iobuf;
            gf_async(&fasync->async, this, fuse_dispatch);
        }

        continue;

    cont_err:
        iobuf_unref(iobuf);
        GF_FREE(iov_in[0].iov_base);
        iov_in[0].iov_base = NULL;
    }

    if (iov_in[0].iov_base)
        GF_FREE(iov_in[0].iov_base);

    /*
     * We could be in all sorts of states with respect to iobuf and iov_in
     * by the time we get here, and it's just not worth untangling them if
     * we're about to kill ourselves anyway.
     */

    if (dict_get(this->options, ZR_MOUNTPOINT_OPT))
        mount_point = data_to_str(dict_get(this->options, ZR_MOUNTPOINT_OPT));
    if (mount_point) {
        gf_log(this->name, GF_LOG_INFO, "initiating unmount of %s",
               mount_point);
    }

    /* Kill the whole process, not just this thread. */
    kill(getpid(), SIGTERM);
    return NULL;
}

int32_t
fuse_itable_dump(xlator_t *this)
{
    fuse_private_t *priv = NULL;

    if (!this)
        return -1;

    priv = this->private;

    if (priv && priv->active_subvol) {
        gf_proc_dump_add_section("xlator.mount.fuse.itable");
        inode_table_dump(priv->active_subvol->itable,
                         "xlator.mount.fuse.itable");
    }
    return 0;
}

int32_t
fuse_priv_dump(xlator_t *this)
{
    fuse_private_t *private = NULL;

    if (!this)
        return -1;

   private
    = this->private;

    if (!private)
        return -1;

    gf_proc_dump_add_section("xlator.mount.fuse.priv");

    gf_proc_dump_write("fd", "%d", private->fd);
    gf_proc_dump_write("proto_minor", "%u", private->proto_minor);
    gf_proc_dump_write("volfile", "%s",
                       private->volfile ? private->volfile : "None");
    gf_proc_dump_write("volfile_size", "%" GF_PRI_SIZET, private->volfile_size);
    gf_proc_dump_write("mount_point", "%s", private->mount_point);
    gf_proc_dump_write("fuse_thread_started", "%d",
                       (int)private->fuse_thread_started);
    gf_proc_dump_write("direct_io_mode", "%d", private->direct_io_mode);
    gf_proc_dump_write("entry_timeout", "%lf", private->entry_timeout);
    gf_proc_dump_write("attribute_timeout", "%lf", private->attribute_timeout);
    gf_proc_dump_write("init_recvd", "%d", (int)private->init_recvd);
    gf_proc_dump_write("strict_volfile_check", "%d",
                       (int)private->strict_volfile_check);
    gf_proc_dump_write("timed_response_thread_started", "%d",
                       (int)private->timed_response_fuse_thread_started);
    gf_proc_dump_write("reverse_thread_started", "%d",
                       (int)private->reverse_fuse_thread_started);
    gf_proc_dump_write("invalidate_limit", "%u", private->invalidate_limit);
    gf_proc_dump_write("invalidate_queue_length", "%" PRIu64,
                       private->invalidate_count);
    gf_proc_dump_write("use_readdirp", "%d", private->use_readdirp);

    return 0;
}

int
fuse_history_dump(xlator_t *this)
{
    int ret = -1;
    char key_prefix[GF_DUMP_MAX_BUF_LEN] = {
        0,
    };
    fuse_private_t *priv = this->private;

    GF_VALIDATE_OR_GOTO("fuse", this, out);

    if (!priv->event_history)
        goto out;

    GF_VALIDATE_OR_GOTO(this->name, this->history, out);

    gf_proc_dump_build_key(key_prefix, "xlator.mount.fuse", "history");
    gf_proc_dump_add_section("%s", key_prefix);
    eh_dump(this->history, NULL, dump_history_fuse);

    ret = 0;
out:
    return ret;
}

int
dump_history_fuse(circular_buffer_t *cb, void *data)
{
    char timestr[GF_TIMESTR_SIZE] = {
        0,
    };

    gf_time_fmt_tv(timestr, sizeof timestr, &cb->tv, gf_timefmt_F_HMS);

    gf_proc_dump_write("TIME", "%s", timestr);

    gf_proc_dump_write("message", "%s\n", (char *)cb->data);

    return 0;
}

int
fuse_graph_setup(xlator_t *this, glusterfs_graph_t *graph)
{
    inode_table_t *itable = NULL;
    int ret = 0, winds = 0;
    fuse_private_t *priv = NULL;
    glusterfs_graph_t *prev_graph = NULL;

    priv = this->private;

    pthread_mutex_lock(&priv->sync_mutex);
    {
        /* 1. handle the case of more than one CHILD_UP on same graph.
         * 2. make sure graph is newer than current active_subvol.
         */
        if ((priv->active_subvol == graph->top) || graph->used ||
            ((priv->active_subvol) &&
             (priv->active_subvol->graph->id > graph->id))) {
            goto unlock;
        }

#if FUSE_KERNEL_MINOR_VERSION >= 11
        itable = inode_table_with_invalidator(
            priv->lru_limit, graph->top, fuse_inode_invalidate_fn, this, 0, 0);
#else
        itable = inode_table_new(0, graph->top, 0, 0);
#endif
        if (!itable) {
            ret = -1;
            goto unlock;
        }

        ((xlator_t *)graph->top)->itable = itable;

        prev_graph = priv->next_graph;

        if ((prev_graph != NULL) && (prev_graph->id > graph->id)) {
            /* there was a race and an old graph was initialised
             * before new one.
             */
            prev_graph = graph;
        } else {
            priv->next_graph = graph;
            priv->event_recvd = 0;
        }

        if (prev_graph != NULL)
            winds = ((xlator_t *)prev_graph->top)->winds;

        /* set post initializing next_graph i to preserve
         * critical section update and bails on error */
        graph->used = 1;
    }
    pthread_mutex_unlock(&priv->sync_mutex);

    if ((prev_graph != NULL) && (winds == 0)) {
        xlator_notify(prev_graph->top, GF_EVENT_PARENT_DOWN, prev_graph->top,
                      NULL);
    }

    return ret;
unlock:
    pthread_mutex_unlock(&priv->sync_mutex);

    return ret;
}

int
notify(xlator_t *this, int32_t event, void *data, ...)
{
    int i = 0;
    int32_t ret = 0;
    fuse_private_t *private = NULL;
    gf_boolean_t start_thread = _gf_false;
    glusterfs_graph_t *graph = NULL;
    struct pollfd pfd = {0};

   private
    = this->private;

    graph = data;

    gf_log("fuse", GF_LOG_DEBUG, "got event %d on graph %d", event,
           ((graph) ? graph->id : 0));

    switch (event) {
        case GF_EVENT_GRAPH_NEW:
            break;

        case GF_EVENT_CHILD_UP:
        case GF_EVENT_CHILD_DOWN:
        case GF_EVENT_CHILD_CONNECTING: {
            if (graph) {
                ret = fuse_graph_setup(this, graph);
                if (ret)
                    gf_log(this->name, GF_LOG_WARNING,
                           "failed to setup the graph");
            }

            if ((event == GF_EVENT_CHILD_UP) ||
                (event == GF_EVENT_CHILD_DOWN)) {
                pthread_mutex_lock(&private->sync_mutex);
                {
                   private
                    ->event_recvd = 1;
                    pthread_cond_broadcast(&private->sync_cond);
                }
                pthread_mutex_unlock(&private->sync_mutex);
            }

            pthread_mutex_lock(&private->sync_mutex);
            {
                if (!private->fuse_thread_started) {
                   private
                    ->fuse_thread_started = 1;
                    start_thread = _gf_true;
                }
            }
            pthread_mutex_unlock(&private->sync_mutex);

            if (start_thread) {
               private
                ->fuse_thread = GF_CALLOC(private->reader_thread_count,
                                          sizeof(pthread_t),
                                          gf_fuse_mt_pthread_t);
                for (i = 0; i < private->reader_thread_count; i++) {
                    ret = gf_thread_create(&private->fuse_thread[i], NULL,
                                           fuse_thread_proc, this, "fuseproc");
                    if (ret != 0) {
                        gf_log(this->name, GF_LOG_DEBUG,
                               "pthread_create() failed (%s)", strerror(errno));
                        break;
                    }
                }
            }

            break;
        }

        case GF_EVENT_AUTH_FAILED: {
            /* Authentication failure is an error and glusterfs should stop */
            gf_log(this->name, GF_LOG_ERROR,
                   "Server authenication failed. Shutting down.");
            pthread_mutex_lock(&private->sync_mutex);
            {
                /*Wait for mount to finish*/
                if (!private->mount_finished) {
                    pfd.fd = private->status_pipe[0];
                    pfd.events = POLLIN | POLLHUP | POLLERR;
                    if (poll(&pfd, 1, -1) < 0) {
                        gf_log(this->name, GF_LOG_ERROR, "poll error %s",
                               strerror(errno));
                        goto auth_fail_unlock;
                    }
                    if (pfd.revents & POLLIN) {
                        if (fuse_get_mount_status(this) != 0) {
                            goto auth_fail_unlock;
                        }
                       private
                        ->mount_finished = _gf_true;
                    } else if (pfd.revents) {
                        gf_log(this->name, GF_LOG_ERROR,
                               "mount pipe closed without status");
                        goto auth_fail_unlock;
                    }
                }
            }
        auth_fail_unlock:
            pthread_mutex_unlock(&private->sync_mutex);
            fini(this);
            break;
        }

        default:
            break;
    }

    return ret;
}

int32_t
mem_acct_init(xlator_t *this)
{
    int ret = -1;

    if (!this)
        return ret;

    ret = xlator_mem_acct_init(this, gf_fuse_mt_end + 1);

    if (ret != 0) {
        gf_log(this->name, GF_LOG_ERROR,
               "Memory accounting init"
               "failed");
        return ret;
    }

    return ret;
}

static fuse_handler_t *fuse_std_ops[FUSE_OP_HIGH] = {
    [FUSE_LOOKUP] = fuse_lookup,
    [FUSE_FORGET] = fuse_forget,
    [FUSE_GETATTR] = fuse_getattr,
    [FUSE_SETATTR] = fuse_setattr,
    [FUSE_READLINK] = fuse_readlink,
    [FUSE_SYMLINK] = fuse_symlink,
    [FUSE_MKNOD] = fuse_mknod,
    [FUSE_MKDIR] = fuse_mkdir,
    [FUSE_UNLINK] = fuse_unlink,
    [FUSE_RMDIR] = fuse_rmdir,
    [FUSE_RENAME] = fuse_rename,
    [FUSE_LINK] = fuse_link,
    [FUSE_OPEN] = fuse_open,
    [FUSE_READ] = fuse_readv,
    [FUSE_WRITE] = fuse_write,
    [FUSE_STATFS] = fuse_statfs,
    [FUSE_RELEASE] = fuse_release,
    [FUSE_FSYNC] = fuse_fsync,
    [FUSE_SETXATTR] = fuse_setxattr,
    [FUSE_GETXATTR] = fuse_getxattr,
    [FUSE_LISTXATTR] = fuse_listxattr,
    [FUSE_REMOVEXATTR] = fuse_removexattr,
    [FUSE_FLUSH] = fuse_flush,
    [FUSE_INIT] = fuse_init,
    [FUSE_OPENDIR] = fuse_opendir,
    [FUSE_READDIR] = fuse_readdir,
    [FUSE_RELEASEDIR] = fuse_releasedir,
    [FUSE_FSYNCDIR] = fuse_fsyncdir,
    [FUSE_GETLK] = fuse_getlk,
    [FUSE_SETLK] = fuse_setlk,
    [FUSE_SETLKW] = fuse_setlk,
    [FUSE_ACCESS] = fuse_access,
    [FUSE_CREATE] = fuse_create,
    [FUSE_INTERRUPT] = fuse_interrupt,
    /* [FUSE_BMAP] */
    [FUSE_DESTROY] = fuse_destroy,
/* [FUSE_IOCTL] */
/* [FUSE_POLL] */
/* [FUSE_NOTIFY_REPLY] */

#if FUSE_KERNEL_MINOR_VERSION >= 16
    [FUSE_BATCH_FORGET] = fuse_batch_forget,
#endif

#if FUSE_KERNEL_MINOR_VERSION >= 19
#ifdef FALLOC_FL_KEEP_SIZE
    [FUSE_FALLOCATE] = fuse_fallocate,
#endif /* FALLOC_FL_KEEP_SIZE */
#endif

#if FUSE_KERNEL_MINOR_VERSION >= 21
    [FUSE_READDIRPLUS] = fuse_readdirp,
#endif

#if FUSE_KERNEL_MINOR_VERSION >= 24 && HAVE_SEEK_HOLE
    [FUSE_LSEEK] = fuse_lseek,
#endif

#if FUSE_KERNEL_MINOR_VERSION >= 28
    [FUSE_COPY_FILE_RANGE] = fuse_copy_file_range,
#endif
};

static fuse_handler_t *fuse_dump_ops[FUSE_OP_HIGH];

static void
fuse_dumper(xlator_t *this, fuse_in_header_t *finh, void *msg,
            struct iobuf *iobuf)
{
    fuse_private_t *priv = NULL;
    struct iovec diov[6] = {
        {
            0,
        },
    };
    char r = 'R';
    uint32_t fusedump_item_count = 3;
    struct fusedump_timespec fts = {
        0,
    };
    struct fusedump_signature fsig = {
        0,
    };

    int ret = 0;

    priv = this->private;

    fusedump_setup_meta(diov, &r, &fusedump_item_count, &fts, &fsig);
    diov[4] = (struct iovec){finh, sizeof(*finh)};
    if (finh->opcode == FUSE_WRITE) {
        /* WRITE has special data alignment, see comment in
           fuse_write(). */
        diov[4].iov_len += sizeof(struct fuse_write_in);
    }
    diov[5] = (struct iovec){msg, finh->len - diov[4].iov_len};

    pthread_mutex_lock(&priv->fuse_dump_mutex);
    ret = sys_writev(priv->fuse_dump_fd, diov, sizeof(diov) / sizeof(diov[0]));
    pthread_mutex_unlock(&priv->fuse_dump_mutex);
    if (ret == -1)
        gf_log("glusterfs-fuse", GF_LOG_ERROR,
               "failed to dump fuse message (R): %s", strerror(errno));

    priv->fuse_ops0[finh->opcode](this, finh, msg, NULL);
}

int
init(xlator_t *this_xl)
{
    int ret = 0;
    dict_t *options = NULL;
    char *value_string = NULL;
    cmd_args_t *cmd_args = NULL;
    char *fsname = NULL;
    fuse_private_t *priv = NULL;
    struct stat stbuf = {
        0,
    };
    int i = 0;
    int xl_name_allocated = 0;
    int fsname_allocated = 0;
    glusterfs_ctx_t *ctx = NULL;
    gf_boolean_t sync_to_mount = _gf_false;
    gf_boolean_t fopen_keep_cache = _gf_false;
    char *mnt_args = NULL;
    eh_t *event = NULL;

    if (this_xl == NULL)
        return -1;

    if (this_xl->options == NULL)
        return -1;

    ctx = this_xl->ctx;
    if (!ctx)
        return -1;

    options = this_xl->options;

    if (this_xl->name == NULL) {
        this_xl->name = gf_strdup("fuse");
        if (!this_xl->name) {
            gf_log("glusterfs-fuse", GF_LOG_ERROR, "Out of memory");

            goto cleanup_exit;
        }
        xl_name_allocated = 1;
    }

    priv = GF_CALLOC(1, sizeof(*priv), gf_fuse_mt_fuse_private_t);
    if (!priv) {
        gf_log("glusterfs-fuse", GF_LOG_ERROR, "Out of memory");

        goto cleanup_exit;
    }
    this_xl->private = (void *)priv;
    priv->mount_point = NULL;
    priv->fd = -1;

    INIT_LIST_HEAD(&priv->invalidate_list);
    pthread_cond_init(&priv->invalidate_cond, NULL);
    pthread_mutex_init(&priv->invalidate_mutex, NULL);

    INIT_LIST_HEAD(&priv->timed_list);
    pthread_cond_init(&priv->timed_cond, NULL);
    pthread_mutex_init(&priv->timed_mutex, NULL);

    INIT_LIST_HEAD(&priv->interrupt_list);
    pthread_mutex_init(&priv->interrupt_mutex, NULL);

    pthread_mutex_init(&priv->fusedev_errno_cnt_mutex, NULL);

    /* get options from option dictionary */
    ret = dict_get_str(options, ZR_MOUNTPOINT_OPT, &value_string);
    if (ret == -1 || value_string == NULL) {
        gf_log("fuse", GF_LOG_ERROR,
               "Mandatory option 'mountpoint' is not specified.");
        goto cleanup_exit;
    }

    if (sys_stat(value_string, &stbuf) != 0) {
        if (errno == ENOENT) {
            gf_log(this_xl->name, GF_LOG_ERROR, "%s %s does not exist",
                   ZR_MOUNTPOINT_OPT, value_string);
        } else if (errno == ENOTCONN) {
            gf_log(this_xl->name, GF_LOG_ERROR,
                   "Mountpoint %s seems to have a stale "
                   "mount, run 'umount %s' and try again.",
                   value_string, value_string);
        } else {
            gf_log(this_xl->name, GF_LOG_DEBUG, "%s %s : stat returned %s",
                   ZR_MOUNTPOINT_OPT, value_string, strerror(errno));
        }
        goto cleanup_exit;
    }

    if (S_ISDIR(stbuf.st_mode) == 0) {
        gf_log(this_xl->name, GF_LOG_ERROR, "%s %s is not a directory",
               ZR_MOUNTPOINT_OPT, value_string);
        goto cleanup_exit;
    }
    priv->mount_point = gf_strdup(value_string);
    if (!priv->mount_point) {
        gf_log("glusterfs-fuse", GF_LOG_ERROR, "Out of memory");

        goto cleanup_exit;
    }

    GF_OPTION_INIT(ZR_ATTR_TIMEOUT_OPT, priv->attribute_timeout, double,
                   cleanup_exit);

    GF_OPTION_INIT("reader-thread-count", priv->reader_thread_count, uint32,
                   cleanup_exit);

    GF_OPTION_INIT("auto-invalidation", priv->fuse_auto_inval, bool,
                   cleanup_exit);
    GF_OPTION_INIT(ZR_ENTRY_TIMEOUT_OPT, priv->entry_timeout, double,
                   cleanup_exit);

    GF_OPTION_INIT(ZR_NEGATIVE_TIMEOUT_OPT, priv->negative_timeout, double,
                   cleanup_exit);

    GF_OPTION_INIT("client-pid", priv->client_pid, int32, cleanup_exit);
    /* have to check & register the presence of client-pid manually */
    priv->client_pid_set = !!dict_get(this_xl->options, "client-pid");

    GF_OPTION_INIT("uid-map-root", priv->uid_map_root, uint32, cleanup_exit);

    priv->direct_io_mode = 2;
    ret = dict_get_str(options, ZR_DIRECT_IO_OPT, &value_string);
    if (ret == 0) {
        gf_boolean_t direct_io_mode_bool;
        ret = gf_string2boolean(value_string, &direct_io_mode_bool);
        if (ret == 0) {
            priv->direct_io_mode = direct_io_mode_bool ? 1 : 0;
        }
    }

    GF_OPTION_INIT(ZR_STRICT_VOLFILE_CHECK, priv->strict_volfile_check, bool,
                   cleanup_exit);

    GF_OPTION_INIT("acl", priv->acl, bool, cleanup_exit);

    if (priv->uid_map_root)
        priv->acl = 1;

    GF_OPTION_INIT("selinux", priv->selinux, bool, cleanup_exit);

    GF_OPTION_INIT("capability", priv->capability, bool, cleanup_exit);

    GF_OPTION_INIT("read-only", priv->read_only, bool, cleanup_exit);

    GF_OPTION_INIT("enable-ino32", priv->enable_ino32, bool, cleanup_exit);

    GF_OPTION_INIT("use-readdirp", priv->use_readdirp, bool, cleanup_exit);

    priv->fuse_dump_fd = -1;
    ret = dict_get_str(options, "dump-fuse", &value_string);
    if (ret == 0) {
        ret = sys_unlink(value_string);
        if (ret == -1 && errno != ENOENT) {
            gf_log("glusterfs-fuse", GF_LOG_ERROR,
                   "failed to remove old fuse dump file %s: %s", value_string,
                   strerror(errno));

            goto cleanup_exit;
        }
        ret = open(value_string, O_RDWR | O_CREAT | O_EXCL, S_IRUSR | S_IWUSR);
        if (ret == -1) {
            gf_log("glusterfs-fuse", GF_LOG_ERROR,
                   "failed to open fuse dump file %s: %s", value_string,
                   strerror(errno));

            goto cleanup_exit;
        }
        priv->fuse_dump_fd = ret;
    }

    sync_to_mount = _gf_false;
    ret = dict_get_str(options, "sync-to-mount", &value_string);
    if (ret == 0) {
        ret = gf_string2boolean(value_string, &sync_to_mount);
        GF_ASSERT(ret == 0);
    }

    priv->fopen_keep_cache = 2;
    if (dict_get(options, "fopen-keep-cache")) {
        GF_OPTION_INIT("fopen-keep-cache", fopen_keep_cache, bool,
                       cleanup_exit);
        priv->fopen_keep_cache = fopen_keep_cache;
    }

    GF_OPTION_INIT("gid-timeout", priv->gid_cache_timeout, int32, cleanup_exit);

    GF_OPTION_INIT("fuse-mountopts", priv->fuse_mountopts, str, cleanup_exit);

    if (gid_cache_init(&priv->gid_cache, priv->gid_cache_timeout) < 0) {
        gf_log("glusterfs-fuse", GF_LOG_ERROR,
               "Failed to initialize "
               "group cache.");
        goto cleanup_exit;
    }

    GF_OPTION_INIT("resolve-gids", priv->resolve_gids, bool, cleanup_exit);

    /* default values seemed to work fine during testing */
    GF_OPTION_INIT("background-qlen", priv->background_qlen, int32,
                   cleanup_exit);
    GF_OPTION_INIT("congestion-threshold", priv->congestion_threshold, int32,
                   cleanup_exit);

    GF_OPTION_INIT("no-root-squash", priv->no_root_squash, bool, cleanup_exit);
    /* change the client_pid to no-root-squash pid only if the
       client is neither defrag process or gsyncd process.
    */
    if (!priv->client_pid_set) {
        if (priv->no_root_squash == _gf_true) {
            priv->client_pid_set = _gf_true;
            priv->client_pid = GF_CLIENT_PID_NO_ROOT_SQUASH;
        }
    }

    GF_OPTION_INIT("lru-limit", priv->lru_limit, uint32, cleanup_exit);

    GF_OPTION_INIT("invalidate-limit", priv->invalidate_limit, uint32,
                   cleanup_exit);

    GF_OPTION_INIT("event-history", priv->event_history, bool, cleanup_exit);

    GF_OPTION_INIT("thin-client", priv->thin_client, bool, cleanup_exit);

    /* Writeback cache support */
    GF_OPTION_INIT("kernel-writeback-cache", priv->kernel_writeback_cache, bool,
                   cleanup_exit);
    GF_OPTION_INIT("attr-times-granularity", priv->attr_times_granularity,
                   int32, cleanup_exit);

    GF_OPTION_INIT("flush-handle-interrupt", priv->flush_handle_interrupt, bool,
                   cleanup_exit);

    GF_OPTION_INIT("fuse-dev-eperm-ratelimit-ns",
                   priv->fuse_dev_eperm_ratelimit_ns, uint32, cleanup_exit);

    /* user has set only background-qlen, not congestion-threshold,
       use the fuse kernel driver formula to set congestion. ie, 75% */
    if (dict_get(this_xl->options, "background-qlen") &&
        !dict_get(this_xl->options, "congestion-threshold")) {
        priv->congestion_threshold = (priv->background_qlen * 3) / 4;
        gf_log(this_xl->name, GF_LOG_INFO,
               "setting congestion control as 75%% of "
               "background-queue length (ie, (.75 * %d) = %d",
               priv->background_qlen, priv->congestion_threshold);
    }

    /* congestion should not be higher than background queue length */
    if (priv->congestion_threshold > priv->background_qlen) {
        gf_log(this_xl->name, GF_LOG_INFO,
               "setting congestion control same as "
               "background-queue length (%d)",
               priv->background_qlen);
        priv->congestion_threshold = priv->background_qlen;
    }

    cmd_args = &this_xl->ctx->cmd_args;
    fsname = cmd_args->volfile;
    if (!fsname && cmd_args->volfile_server) {
        if (cmd_args->volfile_id) {
            int dir_len = 0;
            if (cmd_args->subdir_mount)
                dir_len = strlen(cmd_args->subdir_mount) + 1;
            fsname = GF_MALLOC(strlen(cmd_args->volfile_server) + 1 +
                                   strlen(cmd_args->volfile_id) + 1 + dir_len,
                               gf_fuse_mt_fuse_private_t);
            if (!fsname) {
                gf_log("glusterfs-fuse", GF_LOG_ERROR, "Out of memory");
                goto cleanup_exit;
            }
            fsname_allocated = 1;
            strcpy(fsname, cmd_args->volfile_server);
            strcat(fsname, ":");
            strcat(fsname, cmd_args->volfile_id);
            if (dir_len)
                strcat(fsname, cmd_args->subdir_mount);
        } else
            fsname = cmd_args->volfile_server;
    }
    if (!fsname)
        fsname = "glusterfs";

    priv->fdtable = gf_fd_fdtable_alloc();
    if (priv->fdtable == NULL) {
        gf_log("glusterfs-fuse", GF_LOG_ERROR, "Out of memory");
        goto cleanup_exit;
    }

    gf_asprintf(&mnt_args, "%s%s%s%sallow_other,max_read=131072",
                priv->acl ? "" : "default_permissions,",
                priv->read_only ? "ro," : "",
                priv->fuse_mountopts ? priv->fuse_mountopts : "",
                priv->fuse_mountopts ? "," : "");
    if (!mnt_args)
        goto cleanup_exit;

    {
        char *mnt_tok = NULL;
        token_iter_t tit = {
            0,
        };
        gf_boolean_t iter_end = _gf_false;

        for (mnt_tok = token_iter_init(mnt_args, ',', &tit);;) {
            iter_end = next_token(&mnt_tok, &tit);

            if (strcmp(mnt_tok, "auto_unmount") == 0) {
                priv->auto_unmount = _gf_true;
                drop_token(mnt_tok, &tit);
            }

            if (iter_end)
                break;
        }
    }

    if (pipe(priv->status_pipe) < 0) {
        gf_log(this_xl->name, GF_LOG_ERROR,
               "could not create pipe to separate mount process");
        goto cleanup_exit;
    }

    priv->fd = gf_fuse_mount(priv->mount_point, fsname, mnt_args,
                             sync_to_mount ? &ctx->mnt_pid : NULL,
                             priv->status_pipe[1]);
    if (priv->fd == -1)
        goto cleanup_exit;
    if (priv->auto_unmount) {
        ret = gf_fuse_unmount_daemon(priv->mount_point, priv->fd);
        if (ret == -1)
            goto cleanup_exit;
    }

    if (priv->event_history) {
        event = eh_new(FUSE_EVENT_HISTORY_SIZE, _gf_false, NULL);
        if (!event) {
            gf_log(this_xl->name, GF_LOG_ERROR,
                   "could not create a new event history");
            goto cleanup_exit;
        }

        this_xl->history = event;
    }

    pthread_mutex_init(&priv->fuse_dump_mutex, NULL);
    pthread_cond_init(&priv->sync_cond, NULL);
    pthread_cond_init(&priv->migrate_cond, NULL);
    pthread_mutex_init(&priv->sync_mutex, NULL);
    priv->event_recvd = 0;

    for (i = 0; i < FUSE_OP_HIGH; i++) {
        if (!fuse_std_ops[i])
            fuse_std_ops[i] = fuse_enosys;
        if (!fuse_dump_ops[i])
            fuse_dump_ops[i] = fuse_dumper;
    }
    priv->fuse_ops = fuse_std_ops;
    if (priv->fuse_dump_fd != -1) {
        priv->fuse_ops0 = priv->fuse_ops;
        priv->fuse_ops = fuse_dump_ops;
    }

    if (fsname_allocated)
        GF_FREE(fsname);
    GF_FREE(mnt_args);
    return 0;

cleanup_exit:
    if (xl_name_allocated)
        GF_FREE(this_xl->name);
    if (fsname_allocated)
        GF_FREE(fsname);
    if (priv) {
        GF_FREE(priv->mount_point);
        if (priv->fd != -1)
            sys_close(priv->fd);
        if (priv->fuse_dump_fd != -1)
            sys_close(priv->fuse_dump_fd);
        GF_FREE(priv);
    }
    GF_FREE(mnt_args);
    return -1;
}

void
fini(xlator_t *this_xl)
{
    fuse_private_t *priv = NULL;
    char *mount_point = NULL;

    if (this_xl == NULL)
        return;

    if ((priv = this_xl->private) == NULL)
        return;

    pthread_mutex_lock(&priv->sync_mutex);
    {
        if (!(priv->fini_invoked)) {
            priv->fini_invoked = _gf_true;
        } else {
            pthread_mutex_unlock(&priv->sync_mutex);
            return;
        }
    }
    pthread_mutex_unlock(&priv->sync_mutex);

    if (dict_get(this_xl->options, ZR_MOUNTPOINT_OPT))
        mount_point = data_to_str(
            dict_get(this_xl->options, ZR_MOUNTPOINT_OPT));
    if (mount_point != NULL) {
        if (!priv->auto_unmount) {
            gf_log(this_xl->name, GF_LOG_INFO, "Unmounting '%s'.", mount_point);
            gf_fuse_unmount(mount_point, priv->fd);
        }

        gf_log(this_xl->name, GF_LOG_INFO, "Closing fuse connection to '%s'.",
               mount_point);

        sys_close(priv->fuse_dump_fd);
        dict_del(this_xl->options, ZR_MOUNTPOINT_OPT);
    }
    /* Process should terminate once fuse xlator is finished.
     * Required for AUTH_FAILED event.
     */
    kill(getpid(), SIGTERM);
}

struct xlator_fops fops;

struct xlator_cbks cbks = {.invalidate = fuse_invalidate,
                           .forget = fuse_forget_cbk,
                           .release = fuse_internal_release};

struct xlator_dumpops dumpops = {
    .priv = fuse_priv_dump,
    .inode = fuse_itable_dump,
    .history = fuse_history_dump,
};

struct volume_options options[] = {
    {.key = {"direct-io-mode"}, .type = GF_OPTION_TYPE_BOOL},
    {.key = {ZR_MOUNTPOINT_OPT, "mount-point"}, .type = GF_OPTION_TYPE_PATH},
    {.key = {ZR_DUMP_FUSE, "fuse-dumpfile"}, .type = GF_OPTION_TYPE_PATH},
    {.key = {ZR_ATTR_TIMEOUT_OPT},
     .type = GF_OPTION_TYPE_DOUBLE,
     .default_value = "1.0"},
    {.key = {ZR_ENTRY_TIMEOUT_OPT},
     .type = GF_OPTION_TYPE_DOUBLE,
     .default_value = "1.0"},
    {.key = {ZR_NEGATIVE_TIMEOUT_OPT},
     .type = GF_OPTION_TYPE_DOUBLE,
     .default_value = "0.0"},
    {.key = {ZR_STRICT_VOLFILE_CHECK},
     .type = GF_OPTION_TYPE_BOOL,
     .default_value = "false"},
    {.key = {"client-pid"}, .type = GF_OPTION_TYPE_INT},
    {.key = {"uid-map-root"}, .type = GF_OPTION_TYPE_INT},
    {.key = {"sync-to-mount"}, .type = GF_OPTION_TYPE_BOOL},
    {.key = {"read-only"}, .type = GF_OPTION_TYPE_BOOL},
    {.key = {"fopen-keep-cache"},
     .type = GF_OPTION_TYPE_BOOL,
     .default_value = "false"},
    {.key = {"gid-timeout"},
     .type = GF_OPTION_TYPE_INT,
     .default_value = "300"},
    {.key = {"resolve-gids"},
     .type = GF_OPTION_TYPE_BOOL,
     .default_value = "false"},
    {.key = {"acl"}, .type = GF_OPTION_TYPE_BOOL, .default_value = "false"},
    {.key = {"selinux"}, .type = GF_OPTION_TYPE_BOOL, .default_value = "false"},
    {.key = {"enable-ino32"},
     .type = GF_OPTION_TYPE_BOOL,
     .default_value = "false"},
    {
        .key = {"background-qlen"},
        .type = GF_OPTION_TYPE_INT,
        .default_value = "64",
        .min = 16,
        .max = (64 * GF_UNIT_KB),
    },
    {
        .key = {"congestion-threshold"},
        .type = GF_OPTION_TYPE_INT,
        .default_value = "48",
        .min = 12,
        .max = (64 * GF_UNIT_KB),
    },
    {.key = {"fuse-mountopts"}, .type = GF_OPTION_TYPE_STR},
    {.key = {"use-readdirp"},
     .type = GF_OPTION_TYPE_BOOL,
     .default_value = "yes"},
    {
        .key = {"no-root-squash"},
        .type = GF_OPTION_TYPE_BOOL,
        .default_value = "false",
        .description =
            "This is the mount option for disabling the "
            "root squash for the client irrespective of whether the "
            "root-squash "
            "option for the volume is set or not. But this option is honoured "
            "only for the trusted clients. For non trusted clients this value "
            "does not have any affect and the volume option for root-squash is "
            "honoured.",
    },
    {.key = {"capability"},
     .type = GF_OPTION_TYPE_BOOL,
     .default_value = "false"},
    {
        .key = {"event-history"},
        .type = GF_OPTION_TYPE_BOOL,
        .default_value = "false",
        .description = "This option can be used to enable or disable fuse "
                       "event history.",
    },
    {
        .key = {"thin-client"},
        .type = GF_OPTION_TYPE_BOOL,
        .default_value = "false",
        .description = "Enables thin mount and connects via gfproxyd daemon.",
    },
    {
        .key = {"reader-thread-count"},
        .type = GF_OPTION_TYPE_INT,
        .default_value = "1",
        .min = 1,
        .max = 64,
        .description = "Sets fuse reader thread count.",
    },
    {
        .key = {"kernel-writeback-cache"},
        .type = GF_OPTION_TYPE_BOOL,
        .default_value = "false",
        .description = "Enables fuse in-kernel writeback cache.",
    },
    {
        .key = {"attr-times-granularity"},
        .type = GF_OPTION_TYPE_INT,
        .default_value = "0",
        .min = 0,
        .max = 1000000000,
        .description = "Supported granularity of file attribute times.",
    },
    {
        .key = {"flush-handle-interrupt"},
        .type = GF_OPTION_TYPE_BOOL,
        .default_value = "false",
        .description =
            "Handle iterrupts in FLUSH handler (for testing purposes).",
    },
    {
        .key = {"lru-limit"},
        .type = GF_OPTION_TYPE_INT,
        .default_value = "65536",
        .min = 0,
        .description = "makes glusterfs invalidate kernel inodes after "
                       "reaching this limit (0 means 'unlimited')",
    },
    {
        .key = {"invalidate-limit"},
        .type = GF_OPTION_TYPE_INT,
        .default_value = "0",
        .min = 0,
        .description = "suspend invalidations as of 'lru-limit' if the number "
                       "of outstanding invalidations reaches this limit "
                       "(0 means 'unlimited')",
    },
    {
        .key = {"auto-invalidation"},
        .type = GF_OPTION_TYPE_BOOL,
        .default_value = "true",
        .description = "controls whether fuse-kernel can auto-invalidate "
                       "attribute, dentry and page-cache. Disable this only "
                       "if same files/directories are not accessed across "
                       "two different mounts concurrently",
    },
    {
        .key = {"fuse-dev-eperm-ratelimit-ns"},
        .type = GF_OPTION_TYPE_INT,
        .default_value = "10000000", /* 0.01 sec */
        .min = 0,
        .max = 1000000000,
        .description = "Rate limit reading from fuse device upon EPERM "
                       "failure.",
    },
    {.key = {NULL}},
};

xlator_api_t xlator_api = {
    .init = init,
    .fini = fini,
    .notify = notify,
    .dumpops = &dumpops,
    .mem_acct_init = mem_acct_init,
    .op_version = {1}, /* Present from the initial version */
    .fops = &fops,
    .cbks = &cbks,
    .options = options,
    .identifier = "fuse",
    .category = GF_MAINTAINED,
};
