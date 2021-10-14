/*
  Copyright (c) 2021 Red Hat, Inc. <https://www.redhat.com>
  This file is part of GlusterFS.

  This file is licensed to you under your choice of the GNU Lesser
  General Public License, version 3 or any later version (LGPLv3 or
  later), or the GNU General Public License, version 2 (GPLv2), in all
  cases as published by the Free Software Foundation.
*/

#include <signal.h>
#include <sys/mman.h>
#include <syscall.h>
#include <sys/mount.h>
#include <urcu/uatomic.h>
#include <poll.h>

#include <glusterfs/compat-io_uring.h>

#include <glusterfs/gf-io-uring.h>

/* Only needed for 'global_ctx' */
#include <glusterfs/globals.h>

/* Only needed for gf_event_dispatch(). */
#include <glusterfs/gf-event.h>

/* Log message definitions. */

#define LG_MSG_IO_URING_NOT_SUPPORTED_LVL(_res) GF_LOG_WARNING
#define LG_MSG_IO_URING_NOT_SUPPORTED_FMT                                      \
    "Current kernel doesn't support I/O URing interface."

#define LG_MSG_IO_URING_INVALID_LVL(_res) GF_LOG_WARNING
#define LG_MSG_IO_URING_INVALID_FMT                                            \
    "Kernel's I/O URing implementation doesn't support given data."

#define LG_MSG_IO_URING_MISSING_FEAT_LVL(_res) GF_LOG_WARNING
#define LG_MSG_IO_URING_MISSING_FEAT_FMT                                       \
    "Kernel's I/O URing implementation doesn't support required features."

#define LG_MSG_IO_URING_TOO_SMALL_LVL(_res) GF_LOG_WARNING
#define LG_MSG_IO_URING_TOO_SMALL_FMT                                          \
    "Maximum allowed SQ size is too small (%u)"

#define LG_MSG_IO_URING_ENTER_FAILED_LVL(_res) GF_LOG_CRITICAL
#define LG_MSG_IO_URING_ENTER_FAILED_FMT                                       \
    "io_uring_enter() failed with an unrecoverable error. This could mean "    \
    "a critical bug or a memory corruption. The process cannot continue in "   \
    "this state."

/* Required features from kernel io_uring. */
#define GF_IO_URING_REQUIRED_FEATS                                             \
    (IORING_FEAT_NODROP | IORING_FEAT_SUBMIT_STABLE)

/* Private flag to distinguish between regular requests from timer requests.
 * This is needed when 'cancel' command is executed since io_uring uses
 * different operations to cancel a normal operation or a timer. */
#define GF_IO_URING_FLAG_TIMER GF_IO_ID_FLAG_1

/* Helper macro to define names of bits. */
#define GF_IO_BITNAME(_prefix, _name) { _prefix##_##_name, #_name }

/* Structure to keep names of bits. */
typedef struct _gf_io_uring_bitname {
    /* Bitmask of the bit/field. */
    uint64_t bit;

    /* Name of the bit/field. */
    const char *name;
} gf_io_uring_bitname_t;

/* Completion queue management structure. */
typedef struct _gf_io_uring_cq {
    uint32_t *head;
    uint32_t *tail;
    uint32_t *overflow;
    struct io_uring_cqe *cqes;
    uint32_t mask;
    uint32_t entries;

    void *ring;
    size_t size;
} gf_io_uring_cq_t;

/* Submission queue management structure. */
typedef struct _gf_io_uring_sq {
    uint32_t *head;
    uint32_t *tail;
    uint32_t *flags;
    uint32_t *dropped;
    uint32_t *array;
    struct io_uring_sqe *sqes;
    uint32_t mask;
    uint32_t entries;

    void *ring;
    size_t size;
    size_t sqes_size;
} gf_io_uring_sq_t;

/* Structure to keep io_uring state. */
typedef struct _gf_io_uring {
    gf_io_uring_sq_t sq;
    gf_io_uring_cq_t cq;
    struct io_uring_params params;
    uint32_t fd;
} gf_io_uring_t;

/* Global io_uring state. */
static gf_io_uring_t gf_io_uring = {};

/* io_uring_setup() system call. */
static int32_t
io_uring_setup(uint32_t entries, struct io_uring_params *params)
{
    return syscall(SYS_io_uring_setup, entries, params);
}

/* io_uring_enter() system call. */
static int32_t
io_uring_enter(uint32_t fd, uint32_t to_submit, uint32_t min_complete,
               uint32_t flags, void *arg, size_t size)
{
    return syscall(SYS_io_uring_enter, fd, to_submit, min_complete, flags, arg,
                   size);
}

/* io_uring_register() system call. */
static int32_t
io_uring_register(uint32_t fd, uint32_t opcode, void *arg, uint32_t nr_args)
{
    return syscall(SYS_io_uring_register, fd, opcode, arg, nr_args);
}

/* Build a string containing the names of bits present in a bitmap. The
 * caller must ensure that the buffer size will be enough in all cases. */
static void
gf_io_uring_name_list(char *buffer, gf_io_uring_bitname_t *names,
                      uint64_t bitmap)
{
    char *ptr;
    gf_io_uring_bitname_t *bn;

    ptr = buffer;
    for (bn = names; bn->name != NULL; bn++) {
        if ((bitmap & bn->bit) != 0) {
            bitmap ^= bn->bit;
            ptr += sprintf(ptr, "%s(%" PRIx64 ") ", bn->name, bn->bit);
        }
    }
    if (bitmap != 0) {
        sprintf(ptr, "?(%" PRIx64 ")", bitmap);
    } else if (ptr == buffer) {
        strcpy(buffer, "<none>");
    } else {
        ptr[-1] = 0;
    }
}

/* Logs the configuration and features of an io_uring instance. */
static void
gf_io_uring_dump_params(struct io_uring_params *params)
{
    static gf_io_uring_bitname_t flag_names[] = {
        GF_IO_BITNAME(IORING_SETUP, IOPOLL),
        GF_IO_BITNAME(IORING_SETUP, SQPOLL),
        GF_IO_BITNAME(IORING_SETUP, SQ_AFF),
        GF_IO_BITNAME(IORING_SETUP, CQSIZE),
        GF_IO_BITNAME(IORING_SETUP, CLAMP),
        GF_IO_BITNAME(IORING_SETUP, ATTACH_WQ),
        GF_IO_BITNAME(IORING_SETUP, R_DISABLED),
        {}
    };
    static gf_io_uring_bitname_t feature_names[] = {
        GF_IO_BITNAME(IORING_FEAT, SINGLE_MMAP),
        GF_IO_BITNAME(IORING_FEAT, NODROP),
        GF_IO_BITNAME(IORING_FEAT, SUBMIT_STABLE),
        GF_IO_BITNAME(IORING_FEAT, RW_CUR_POS),
        GF_IO_BITNAME(IORING_FEAT, CUR_PERSONALITY),
        GF_IO_BITNAME(IORING_FEAT, FAST_POLL),
        GF_IO_BITNAME(IORING_FEAT, POLL_32BITS),
        GF_IO_BITNAME(IORING_FEAT, SQPOLL_NONFIXED),
        GF_IO_BITNAME(IORING_FEAT, EXT_ARG),
        GF_IO_BITNAME(IORING_FEAT, NATIVE_WORKERS),
        {}
    };

    char names[256];

    gf_io_debug(0, "I/O URing: SQEs=%u, CQEs=%u, CPU=%u, Idle=%u",
                params->sq_entries, params->cq_entries, params->sq_thread_cpu,
                params->sq_thread_idle);

    gf_io_uring_name_list(names, flag_names, params->flags);
    gf_io_debug(0, "I/O URing: Flags: %s", names);

    gf_io_uring_name_list(names, feature_names, params->features);
    gf_io_debug(0, "I/O URing: Features: %s", names);
}

/* Logs the list of supported operations. */
static void
gf_io_uring_dump_ops(struct io_uring_probe *probe)
{
    static const char *op_names[] = {
        [IORING_OP_NOP] = "NOP",
        [IORING_OP_READV] = "READV",
        [IORING_OP_WRITEV] = "WRITEV",
        [IORING_OP_FSYNC] = "FSYNC",
        [IORING_OP_READ_FIXED] = "READ_FIXED",
        [IORING_OP_WRITE_FIXED] = "WRITE_FIXED",
        [IORING_OP_POLL_ADD] = "POLL_ADD",
        [IORING_OP_POLL_REMOVE] = "POLL_REMOVE",
        [IORING_OP_SYNC_FILE_RANGE] = "SYNC_FILE_RANGE",
        [IORING_OP_SENDMSG] = "SENDMSG",
        [IORING_OP_RECVMSG] = "RECVMSG",
        [IORING_OP_TIMEOUT] = "TIMEOUT",
        [IORING_OP_TIMEOUT_REMOVE] = "TIMEOUT_REMOVE",
        [IORING_OP_ACCEPT] = "ACCEPT",
        [IORING_OP_ASYNC_CANCEL] = "ASYNC_CANCEL",
        [IORING_OP_LINK_TIMEOUT] = "LINK_TIMEOUT",
        [IORING_OP_CONNECT] = "CONNECT",
        [IORING_OP_FALLOCATE] = "FALLOCATE",
        [IORING_OP_OPENAT] = "OPENAT",
        [IORING_OP_CLOSE] = "CLOSE",
        [IORING_OP_FILES_UPDATE] = "FILES_UPDATE",
        [IORING_OP_STATX] = "STATX",
        [IORING_OP_READ] = "READ",
        [IORING_OP_WRITE] = "WRITE",
        [IORING_OP_FADVISE] = "FADVISE",
        [IORING_OP_MADVISE] = "MADVISE",
        [IORING_OP_SEND] = "SEND",
        [IORING_OP_RECV] = "RECV",
        [IORING_OP_OPENAT2] = "OPENAT2",
        [IORING_OP_EPOLL_CTL] = "EPOLL_CTL",
        [IORING_OP_SPLICE] = "SPLICE",
        [IORING_OP_PROVIDE_BUFFERS] = "PROVIDE_BUFFERS",
        [IORING_OP_REMOVE_BUFFERS] = "REMOVE_BUFFERS",
        [IORING_OP_TEE] = "TEE",
        [IORING_OP_SHUTDOWN] = "SHUTDOWN",
        [IORING_OP_RENAMEAT] = "RENAMEAT",
        [IORING_OP_UNLINKAT] = "UNLINKAT"
    };

    char names[4096];
    char *ptr;
    const char *name;
    uint32_t i, op;

    gf_io_debug(0, "I/O URing: Max opcode = %u", probe->last_op);

    ptr = names;
    for (i = 0; i < probe->ops_len; i++) {
        if ((probe->ops[i].flags & IO_URING_OP_SUPPORTED) != 0) {
            op = probe->ops[i].op;
            name = "?";
            if ((op < CAA_ARRAY_SIZE(op_names)) && (op_names[op] != NULL)) {
                name = op_names[op];
            }
            ptr += sprintf(ptr, "%s(%u) ", name, op);
        }
    }
    if (ptr == names) {
        strcpy(names, "<none>");
    } else {
        ptr[-1] = 0;
    }

    gf_io_debug(0, "I/O URing: Ops: %s", names);
}

/* mmap a region of the io_uring shared memory. */
static int32_t
gf_io_uring_mmap(void **ring, uint32_t fd, size_t size, off_t offset)
{
    void *ptr;
    int32_t res;

    ptr = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_POPULATE,
               fd, offset);
    if (caa_unlikely(ptr == MAP_FAILED)) {
        res = -errno;
        gf_io_log(res, LG_MSG_IO_CALL_FAILED, "mmap");

        return res;
    }

    res = gf_io_call_errno0(madvise, ptr, size, MADV_DONTFORK);
    if (caa_unlikely(res < 0)) {
        gf_io_call_errno0(munmap, ptr, size);

        return res;
    }

    *ring = ptr;

    return 0;
}

/* Destroy the CQ management structures. */
static void
gf_io_uring_cq_fini(void)
{
    gf_io_call_errno0(munmap, gf_io_uring.cq.ring, gf_io_uring.cq.size);
}

/* Initialize the CQ management structures. */
static int32_t
gf_io_uring_cq_init(uint32_t fd, struct io_uring_params *params)
{
    void *ring;
    size_t size;
    int32_t res;

    size = params->cq_off.cqes +
           params->cq_entries * sizeof(struct io_uring_cqe);

    res = gf_io_uring_mmap(&ring, fd, size, IORING_OFF_CQ_RING);
    if (caa_unlikely(res < 0)) {
        return res;
    }

    gf_io_uring.cq.ring = ring;
    gf_io_uring.cq.size = size;

    gf_io_uring.cq.head = ring + params->cq_off.head;
    gf_io_uring.cq.tail = ring + params->cq_off.tail;
    gf_io_uring.cq.mask = *(uint32_t *)(ring + params->cq_off.ring_mask);
    gf_io_uring.cq.entries = *(uint32_t *)(ring + params->cq_off.ring_entries);
    gf_io_uring.cq.overflow = ring + params->cq_off.overflow;
    gf_io_uring.cq.cqes = ring + params->cq_off.cqes;

    return 0;
}

/* Destroy the SQ management structures. */
static void
gf_io_uring_sq_fini(void)
{
    gf_io_call_errno0(munmap, gf_io_uring.sq.sqes, gf_io_uring.sq.sqes_size);
    gf_io_call_errno0(munmap, gf_io_uring.sq.ring, gf_io_uring.sq.size);
}

/* Initialize the SQ management structures. */
static int32_t
gf_io_uring_sq_init(uint32_t fd, struct io_uring_params *params)
{
    void *ring, *sqes;
    size_t ring_size, sqes_size;
    int32_t res;

    ring_size = params->sq_off.array + params->sq_entries * sizeof(uint32_t);
    sqes_size = params->sq_entries * sizeof(struct io_uring_sqe);

    res = gf_io_uring_mmap(&ring, fd, ring_size, IORING_OFF_SQ_RING);
    if (caa_unlikely(res < 0)) {
        return res;
    }

    res = gf_io_uring_mmap(&sqes, fd, sqes_size, IORING_OFF_SQES);
    if (caa_unlikely(res < 0)) {
        gf_io_call_errno0(munmap, ring, ring_size);

        return res;
    }

    gf_io_uring.sq.ring = ring;
    gf_io_uring.sq.size = ring_size;

    gf_io_uring.sq.head = ring + params->sq_off.head;
    gf_io_uring.sq.tail = ring + params->sq_off.tail;
    gf_io_uring.sq.mask = *(uint32_t *)(ring + params->sq_off.ring_mask);
    gf_io_uring.sq.entries = *(uint32_t *)(ring + params->sq_off.ring_entries);
    gf_io_uring.sq.flags = ring + params->sq_off.flags;
    gf_io_uring.sq.dropped = ring + params->sq_off.dropped;
    gf_io_uring.sq.array = ring + params->sq_off.array;

    gf_io_uring.sq.sqes = sqes;
    gf_io_uring.sq.sqes_size = sqes_size;

    return 0;
}

/* Try to initialize the io_uring engine. */
static int32_t
gf_io_uring_setup(void)
{
    struct io_uring_probe *probe;
    uint32_t i, count, feats;
    int32_t fd, res;

    memset(&gf_io_uring.params, 0, sizeof(gf_io_uring.params));
    gf_io_uring.params.flags = IORING_SETUP_CLAMP;

    /* TODO: IORING_SETUP_SQPOLL cannot be used without registering all
     *       fds used in the requests. This is not feasible right now, but
     *       it could be an interesting feature to try once all the code
     *       is based on the I/O framework. Then it could be easier to
     *       control which fds are used and prepare them for SQPOLL. */

    fd = io_uring_setup(GF_IO_URING_QUEUE_SIZE, &gf_io_uring.params);
    if (caa_unlikely(fd < 0)) {
        res = -errno;
        if ((res == -ENOSYS) || (res == -ENOTSUP)) {
            gf_io_log(res, LG_MSG_IO_URING_NOT_SUPPORTED);
        } else if (res == -EINVAL) {
            gf_io_log(res, LG_MSG_IO_URING_INVALID);
        } else {
            gf_io_log(res, LG_MSG_IO_CALL_FAILED, "io_uring_setup");
        }
        return res;
    }
    gf_io_uring_dump_params(&gf_io_uring.params);

    feats = gf_io_uring.params.features & GF_IO_URING_REQUIRED_FEATS;
    if (feats != GF_IO_URING_REQUIRED_FEATS) {
        res = -ENOTSUP;
        gf_io_log(res, LG_MSG_IO_URING_MISSING_FEAT);
        goto failed_close;
    }

    if (gf_io_uring.params.sq_entries < GF_IO_URING_QUEUE_MIN) {
        res = -ENOBUFS;
        gf_io_log(res, LG_MSG_IO_URING_TOO_SMALL,
                  gf_io_uring.params.sq_entries);
        goto failed_close;
    }

    res = gf_io_uring_sq_init(fd, &gf_io_uring.params);
    if (caa_unlikely(res < 0)) {
        goto failed_close;
    }

    res = gf_io_uring_cq_init(fd, &gf_io_uring.params);
    if (caa_unlikely(res < 0)) {
        goto failed_sq;
    }

    probe = (struct io_uring_probe *)gf_io_uring.sq.sqes;
    /* The opcode is an 8 bit integer, so the maximum number of entries will
     * be 256. We have ensured that there are at least GF_IO_URING_QUEUE_MIN
     * SQEs, so the size of the SQE's memory area should be enough to hold
     * the io_uring_probe structure with 256 entries. This way we avoid an
     * unnecessary memory allocation. */
    count = 256;

    res = gf_io_call_errno0(io_uring_register, fd, IORING_REGISTER_PROBE, probe,
                            count);
    if (caa_unlikely(res < 0)) {
        goto failed_cq;
    }
    gf_io_uring_dump_ops(probe);

    /* TODO: we may check if the system supports the required subset of
     *       operations. */

    gf_io_uring.fd = fd;

    /* Preinitialize the SQ array. The mapping with SQEs is fixed. */
    for (i = 0; i < gf_io_uring.params.sq_entries; i++) {
        gf_io_uring.sq.array[i] = i;
    }

    memset(gf_io_uring.sq.sqes, 0, gf_io_uring.sq.sqes_size);

    /* Return the number of threads to start. */
    return GF_IO_URING_WORKER_THREADS;

failed_cq:
    gf_io_uring_cq_fini();
failed_sq:
    gf_io_uring_sq_fini();
failed_close:
    gf_io_call_errno0(sys_close, fd);

    return res;
}

/* Cleanup io_uring engine. */
static void
gf_io_uring_cleanup(void)
{
    gf_io_uring_sq_fini();
    gf_io_uring_cq_fini();

    gf_io_call_errno0(sys_close, gf_io_uring.fd);
}

static int32_t
gf_io_uring_wait(void)
{
    /* This is needed until the events module get migrated to the I/O
     * framework. */
    return gf_event_dispatch(global_ctx->event_pool);
}

/* Check is the kernel's SQ thread needs to be awakened. */
static bool
gf_io_uring_needs_wake(void)
{
    uint32_t flags;

    flags = CMM_LOAD_SHARED(*gf_io_uring.sq.flags);

    return (flags & IORING_SQ_NEED_WAKEUP) != 0;
}

/* Communicate with kernel through io_uring. */
static int32_t
gf_io_uring_enter(uint32_t submit, bool wait)
{
    uint32_t flags;
    int32_t res, recv;

    flags = 0;
    recv = 0;

    if (wait) {
        flags = IORING_ENTER_GETEVENTS;
        recv = 1;
    }

    if (submit > 0) {
        if ((gf_io_uring.params.flags & IORING_SETUP_SQPOLL) != 0) {
            flags |= IORING_ENTER_SQ_WAKEUP;
            if (!wait) {
                wait = gf_io_uring_needs_wake();
            }
        } else {
            wait = true;
        }
    }

    if (!wait) {
        return 0;
    }

    do {
        res = io_uring_enter(gf_io_uring.fd, submit, recv, flags, NULL, 0);
        if (caa_likely(res >= 0)) {
            return submit - res;
        }

        res = -errno;
    } while (res == -EINTR);

    if (caa_unlikely((res != -EAGAIN) && (res != -EBUSY) && (res != -ENOMEM))) {
        /* This should never happen. Any other error here means a bug in
         * the GlusterFS' io_uring framework or a memory corruption. It's
         * highly unlikely that this situation can be recovered by itself,
         * and given that io_uring expects a sequential stream of requests,
         * we cannot do anything else if the current requests cannot be
         * sent. At this point it's better to crash and generate a coredump
         * so that it can be analyzed instead of trying to continue, which
         * will surely cause hangs and many other problems. */
        gf_io_log(res, LG_MSG_IO_URING_ENTER_FAILED);
        GF_ABORT();
    }

    return res;
}

/* Tries to process an entry from CQ. Returns true if a CQE has been
 * processed. */
static bool
gf_io_uring_cq_process(gf_io_worker_t *worker)
{
    struct io_uring_cqe cqe;
    uint32_t current, head;

    current = CMM_LOAD_SHARED(*gf_io_uring.cq.head);
    while (caa_likely(CMM_LOAD_SHARED(*gf_io_uring.cq.tail) != current)) {
        cqe = gf_io_uring.cq.cqes[current & gf_io_uring.cq.mask];

        head = uatomic_cmpxchg(gf_io_uring.cq.head, current, current + 1);
        if (caa_likely(head == current)) {
            gf_io_cbk(worker, current, cqe.user_data, cqe.res);

            return true;
        }

        current = head;
    }

    return false;
}

/* Make sure there's progress in the CQ by processing some entries or
 * checking that other threads are doing so. */
static void
gf_io_uring_cq_process_some(gf_io_worker_t *worker, uint32_t nr)
{
    struct pollfd fds;
    uint32_t current, retries;
    int32_t res;

    fds.fd = gf_io_uring.fd;
    fds.events = POLL_IN;
    fds.revents = 0;

    current = CMM_LOAD_SHARED(*gf_io_uring.cq.head);

    retries = 0;
    while (!gf_io_uring_cq_process(worker)) {
        res = gf_io_call_errno(poll, &fds, 1, 1);
        if (caa_likely(res > 0)) {
            if (gf_io_uring_cq_process(worker) ||
                (current != CMM_LOAD_SHARED(*gf_io_uring.cq.head))) {
                break;
            }
        }

        if (caa_unlikely(++retries >= GF_IO_URING_MAX_RETRIES)) {
            GF_ABORT();
        }
    }

    while (--nr > 0) {
        if (!gf_io_uring_cq_process(worker)) {
            break;
        }
    }
}

/* Mark some SQ entries to be processed. */
static void
gf_io_uring_sq_commit(uint32_t idx, uint32_t nr)
{
    cmm_smp_wmb();
    CMM_STORE_SHARED(gf_io_uring.sq.sqes[idx].__pad2[2], nr);
}

/* Read the number of SQ entries to process. */
static uint32_t
gf_io_uring_sq_length(uint32_t idx)
{
    uint32_t nr;

    nr = (uint32_t)CMM_LOAD_SHARED(gf_io_uring.sq.sqes[idx].__pad2[2]);
    cmm_smp_rmb();

    return nr;
}

/* Get the number of SQ entries to process and reset it to 0. */
static uint32_t
gf_io_uring_sq_consume(uint32_t tail)
{
    uint32_t idx, nr;

    idx = tail & gf_io_uring.sq.mask;
    nr = gf_io_uring_sq_length(idx);
    if (nr != 0) {
        gf_io_uring_sq_commit(idx, 0);
    }

    return nr;
}

/* Get the number of SQ entries to process and reset it to 0 in a thread-safe
 * way. */
static uint32_t
gf_io_uring_sq_consume_shared(uint32_t tail)
{
    uint32_t idx, nr;

    idx = tail & gf_io_uring.sq.mask;
    nr = gf_io_uring_sq_length(idx);
    if (nr != 0) {
        nr = (uint32_t)uatomic_xchg(&gf_io_uring.sq.sqes[idx].__pad2[2], 0);
    }

    return nr;
}

/* Collect the total number of pending requests and update SQ tail. */
static uint32_t
gf_io_uring_sq_flush(void)
{
    uint32_t nr, flushed, tail;

    flushed = 0;
    tail = CMM_LOAD_SHARED(*gf_io_uring.sq.tail);
    while ((nr = gf_io_uring_sq_consume_shared(tail + flushed)) != 0) {
        do {
            flushed += nr;
        } while ((nr = gf_io_uring_sq_consume(tail + flushed)) != 0);

        cmm_smp_wmb();
        CMM_STORE_SHARED(*gf_io_uring.sq.tail, tail + flushed);
    }

    return flushed;
}

/* Dispatch all pending SQ entries to the kernel. */
static void
gf_io_uring_dispatch(gf_io_worker_t *worker, bool wait)
{
    uint32_t submit;
    int32_t res;

    submit = gf_io_uring_sq_flush();
    while (caa_unlikely((res = gf_io_uring_enter(submit, wait)) != 0)) {
        if (res > 0) {
            submit = res;
        }

        gf_io_uring_cq_process_some(worker, submit);

        wait = false;
    }
}

static void
gf_io_uring_dispatch_no_process(void)
{
    struct pollfd fds;
    uint32_t submit, retries;
    int32_t res;

    fds.fd = gf_io_uring.fd;
    fds.events = POLL_OUT;
    fds.revents = 0;

    retries = 0;
    submit = gf_io_uring_sq_flush();
    while (caa_unlikely((res = gf_io_uring_enter(submit, false)) != 0)) {
        if (caa_unlikely(++retries >= GF_IO_URING_MAX_RETRIES)) {
            GF_ABORT();
        }

        if (res > 0) {
            if (res < submit) {
                retries = 0;
            }
            submit = res;
        }

        gf_io_call_errno(poll, &fds, 1, 1);
    }
}

static int32_t
gf_io_uring_worker_setup(gf_io_worker_t *worker)
{
    return 0;
}

static void
gf_io_uring_worker_cleanup(gf_io_worker_t *worker)
{
}

static void
gf_io_uring_worker_stop(gf_io_worker_t *worker)
{
}

static int32_t
gf_io_uring_worker(gf_io_worker_t *worker)
{
    bool wait;

    wait = !gf_io_uring_cq_process(worker);
    gf_io_uring_dispatch(worker, wait);

    return 0;
}

static void
gf_io_uring_flush(void)
{
    gf_io_worker_t *worker;

    worker = gf_io_worker_get();
    if (worker == NULL) {
        gf_io_uring_dispatch_no_process();
    } else {
        gf_io_uring_dispatch(worker, false);
    }
}

static bool
gf_io_uring_is_available(uint32_t seq)
{
    seq -= CMM_LOAD_SHARED(*gf_io_uring.sq.head);

    return seq < gf_io_uring.sq.entries;
}

static struct io_uring_sqe *
gf_io_uring_get(uint32_t seq)
{
    while (caa_unlikely(!gf_io_uring_is_available(seq))) {
        gf_io_uring_flush();
    }

    return &gf_io_uring.sq.sqes[seq & gf_io_uring.sq.mask];
}

static uint64_t
gf_io_uring_common(uint64_t seq, uint64_t id, struct io_uring_sqe *sqe,
                   uint32_t count)
{
    sqe->user_data = id;
    sqe->__pad2[0] = sqe->__pad2[1] = 0;

    if ((id & GF_IO_ID_FLAG_CHAIN) == 0) {
        gf_io_uring_sq_commit(seq & gf_io_uring.sq.mask, count);
    }

    return id;
}

static uint64_t
gf_io_uring_cancel(uint64_t seq, uint64_t id, gf_io_op_t *op, uint32_t count)
{
    struct io_uring_sqe *sqe;
    uint64_t ref;

    sqe = gf_io_uring_get(seq + count - 1);

    ref = op->cancel.id;
    if ((ref & GF_IO_URING_FLAG_TIMER) == 0) {
        sqe->opcode = IORING_OP_ASYNC_CANCEL;
    } else {
        sqe->opcode = IORING_OP_TIMEOUT_REMOVE;
    }
    sqe->flags = 0;
    sqe->ioprio = 0;
    sqe->fd = -1;
    sqe->off = 0;
    sqe->addr = ref;
    sqe->len = 0;
    sqe->timeout_flags = 0;

    return gf_io_uring_common(seq, id, sqe, count);
}

static uint64_t
gf_io_uring_callback(uint64_t seq, uint64_t id, gf_io_op_t *op, uint32_t count)
{
    struct io_uring_sqe *sqe;

    sqe = gf_io_uring_get(seq + count - 1);

    sqe->opcode = IORING_OP_NOP;
    sqe->flags = 0;
    sqe->ioprio = 0;
    sqe->fd = -1;
    sqe->off = 0;
    sqe->addr = 0;
    sqe->len = 0;
    sqe->rw_flags = 0;

    return gf_io_uring_common(seq, id, sqe, count);
}

const gf_io_engine_t gf_io_engine_io_uring = {
    .name = "io_uring",
    .mode = GF_IO_MODE_IO_URING,

    .setup = gf_io_uring_setup,
    .cleanup = gf_io_uring_cleanup,
    .wait = gf_io_uring_wait,

    .worker_setup = gf_io_uring_worker_setup,
    .worker_cleanup = gf_io_uring_worker_cleanup,
    .worker_stop = gf_io_uring_worker_stop,
    .worker = gf_io_uring_worker,

    .flush = gf_io_uring_flush,

    .cancel = gf_io_uring_cancel,
    .callback = gf_io_uring_callback
};
