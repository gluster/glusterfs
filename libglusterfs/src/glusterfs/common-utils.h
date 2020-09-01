/*
  Copyright (c) 2008-2012 Red Hat, Inc. <http://www.redhat.com>
  This file is part of GlusterFS.

  This file is licensed to you under your choice of the GNU Lesser
  General Public License, version 3 or any later version (LGPLv3 or
  later), or the GNU General Public License, version 2 (GPLv2), in all
  cases as published by the Free Software Foundation.
*/

#ifndef _COMMON_UTILS_H
#define _COMMON_UTILS_H

#include <stdint.h>
#include <sys/uio.h>
#include <netdb.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <pthread.h>
#include <unistd.h>
#include <openssl/md5.h>
#ifndef GF_BSD_HOST_OS
#include <alloca.h>
#endif
#include <limits.h>
#include <fnmatch.h>
#include <uuid/uuid.h>

/* FreeBSD, etc. */
#ifndef __BITS_PER_LONG
#define __BITS_PER_LONG (CHAR_BIT * (sizeof(long)))
#endif

#ifndef ffsll
#define ffsll(x) __builtin_ffsll(x)
#endif

void
trap(void);

#define GF_UNIVERSAL_ANSWER 42 /* :O */

/* To solve type punned error */
#define VOID(ptr) ((void **)((void *)ptr))

#include "glusterfs/mem-pool.h"
#include "glusterfs/compat-uuid.h"
#include "glusterfs/iatt.h"
#include "glusterfs/libglusterfs-messages.h"

#define STRINGIFY(val) #val
#define TOSTRING(val) STRINGIFY(val)

#define alloca0(size)                                                          \
    ({                                                                         \
        void *__ptr;                                                           \
        __ptr = alloca(size);                                                  \
        memset(__ptr, 0, size);                                                \
        __ptr;                                                                 \
    })

#define min(a, b) ((a) < (b) ? (a) : (b))
#define max(a, b) ((a) > (b) ? (a) : (b))
#define gf_roof(a, b) ((((a) + (b)-1) / ((b != 0) ? (b) : 1)) * (b))
#define gf_floor(a, b) (((a) / ((b != 0) ? (b) : 1)) * (b))

#define IPv4_ADDR_SIZE 32

#define GF_UNIT_KB 1024ULL
#define GF_UNIT_MB 1048576ULL
#define GF_UNIT_GB 1073741824ULL
#define GF_UNIT_TB 1099511627776ULL
#define GF_UNIT_PB 1125899906842624ULL

#define GF_UNIT_B_STRING "B"
#define GF_UNIT_KB_STRING "KB"
#define GF_UNIT_MB_STRING "MB"
#define GF_UNIT_GB_STRING "GB"
#define GF_UNIT_TB_STRING "TB"
#define GF_UNIT_PB_STRING "PB"

#define GF_UNIT_PERCENT_STRING "%"

#define GEOREP "geo-replication"
#define GLUSTERD_NAME "glusterd"

#define GF_SELINUX_XATTR_KEY "security.selinux"

#define WIPE(statp)                                                            \
    do {                                                                       \
        typeof(*statp) z = {                                                   \
            0,                                                                 \
        };                                                                     \
        if (statp)                                                             \
            *statp = z;                                                        \
    } while (0)

#define IS_EXT_FS(fs_name)                                                     \
    (!strcmp(fs_name, "ext2") || !strcmp(fs_name, "ext3") ||                   \
     !strcmp(fs_name, "ext4"))

/* process mode definitions */
#define GF_SERVER_PROCESS 0
#define GF_CLIENT_PROCESS 1
#define GF_GLUSTERD_PROCESS 2

/* Defining this here as it is needed by glusterd for setting
 * nfs port in volume status.
 */
#define GF_NFS3_PORT 2049

#define GF_CLIENT_PORT_CEILING 1024
#define GF_IANA_PRIV_PORTS_START 49152 /* RFC 6335 */
#define GF_CLNT_INSECURE_PORT_CEILING (GF_IANA_PRIV_PORTS_START - 1)
#define GF_PORT_MAX 65535
#define GF_PORT_ARRAY_SIZE ((GF_PORT_MAX + 7) / 8)
#define GF_LOCK_TIMER 180
#define GF_MINUTE_IN_SECONDS 60
#define GF_HOUR_IN_SECONDS (60 * 60)
#define GF_DAY_IN_SECONDS (24 * 60 * 60)
#define GF_WEEK_IN_SECONDS (7 * 24 * 60 * 60)
#define GF_SEC_IN_NS 1000000000
#define GF_MS_IN_NS 1000000
#define GF_US_IN_NS 1000

/* Default timeout for both barrier and changelog translator */
#define BARRIER_TIMEOUT "120"

/* Default value of signing waiting time to sign a file for bitrot */
#define SIGNING_TIMEOUT "120"
#define BR_WORKERS "4"

/* xxhash */
#define GF_XXH64_DIGEST_LENGTH 8
#define GF_XXHSUM64_DEFAULT_SEED 0

/* Shard */
#define GF_XATTR_SHARD_FILE_SIZE "trusted.glusterfs.shard.file-size"
#define SHARD_ROOT_GFID "be318638-e8a0-4c6d-977d-7a937aa84806"
#define DOT_SHARD_REMOVE_ME_GFID "77dd5a45-dbf5-4592-b31b-b440382302e9"

/* Lease: buffer length for stringified lease id
 * Format: 4hexnum-4hexnum-4hexnum-4hexnum-4hexnum-4hexnum-4hexnum-4hexnum
 * Eg:6c69-6431-2d63-6c6e-7431-0000-0000-0000
 */
#define GF_LEASE_ID_BUF_SIZE ((LEASE_ID_SIZE * 2) + (LEASE_ID_SIZE / 2))

#define GF_PERCENTAGE(val, total) (((val)*100) / (total))

/* pthread related */
/* as per the man page, thread-name should be at max 16 bytes */
/* with prefix of 'glfs_' (5), we are left with 11 more bytes */
#define GF_THREAD_NAME_LIMIT 16
#define GF_THREAD_NAME_PREFIX "glfs_"

/* Advisory buffer size for formatted timestamps (see gf_time_fmt) */
#define GF_TIMESTR_SIZE 256

/*
 * we could have initialized these as +ve values and treated
 * them as negative while comparing etc.. (which would have
 * saved us with the pain of assigning values), but since we
 * only have a few clients that use this feature, it's okay.
 */
enum _gf_special_pid {
    GF_CLIENT_PID_MAX = 0,
    GF_CLIENT_PID_GSYNCD = -1,
    GF_CLIENT_PID_HADOOP = -2,
    GF_CLIENT_PID_DEFRAG = -3,
    GF_CLIENT_PID_NO_ROOT_SQUASH = -4,
    GF_CLIENT_PID_QUOTA_MOUNT = -5,
    GF_CLIENT_PID_SELF_HEALD = -6,
    GF_CLIENT_PID_GLFS_HEAL = -7,
    GF_CLIENT_PID_BITD = -8,
    GF_CLIENT_PID_SCRUB = -9,
    GF_CLIENT_PID_TIER_DEFRAG = -10,
    GF_SERVER_PID_TRASH = -11,
    GF_CLIENT_PID_ADD_REPLICA_MOUNT = -12,
    GF_CLIENT_PID_SET_UTIME = -13,
};

enum _gf_xlator_ipc_targets {
    GF_IPC_TARGET_CHANGELOG = 0,
    GF_IPC_TARGET_CTR = 1,
    GF_IPC_TARGET_UPCALL = 2
};

typedef enum _gf_special_pid gf_special_pid_t;
typedef enum _gf_xlator_ipc_targets _gf_xlator_ipc_targets_t;

/* Array to hold custom xattr keys */
extern char *xattrs_to_heal[];

char **
get_xattrs_to_heal();

/* The DHT file rename operation is not a straightforward rename.
 * It involves creating linkto and linkfiles, and can unlink or rename the
 * source file depending on the hashed and cached subvols for the source
 * and target files. this makes it difficult for geo-rep to figure out that
 * a rename operation has taken place.
 *
 * We now send a special key and the values of the source and target pargfids
 * and basenames to indicate to changelog that the operation in question
 * should be treated as a rename. We are explicitly filling and sending this
 * as a binary value in the dictionary as the unlink op will not have the
 * source file information. The lengths of the src and target basenames
 * are used to calculate where to start reading the names in the structure.
 * XFS allows a max of 255 chars for filenames but other file systems might
 * not have such restrictions
 */
typedef struct dht_changelog_rename_info {
    uuid_t old_pargfid;
    uuid_t new_pargfid;
    int32_t oldname_len;
    int32_t newname_len;
    char buffer[1];
} dht_changelog_rename_info_t;

typedef int (*gf_cmp)(void *, void *);

struct _dict;

struct dnscache {
    struct _dict *cache_dict;
    time_t ttl;
};

struct dnscache_entry {
    char *ip;
    char *fqdn;
    time_t timestamp;
};

struct dnscache6 {
    struct addrinfo *first;
    struct addrinfo *next;
};

struct list_node {
    void *ptr;
    struct list_head list;
};

extern char *vol_type_str[];

struct list_node *
list_node_add(void *ptr, struct list_head *list);
struct list_node *
list_node_add_order(void *ptr, struct list_head *list,
                    int (*compare)(struct list_head *, struct list_head *));
void
list_node_del(struct list_node *node);

struct dnscache *
gf_dnscache_init(time_t ttl);
void
gf_dnscache_deinit(struct dnscache *cache);
struct dnscache_entry *
gf_dnscache_entry_init(void);
void
gf_dnscache_entry_deinit(struct dnscache_entry *entry);
char *
gf_rev_dns_lookup_cached(const char *ip, struct dnscache *dnscache);

char *
gf_resolve_path_parent(const char *path);

void
gf_global_variable_init(void);

int32_t
gf_resolve_ip6(const char *hostname, uint16_t port, int family, void **dnscache,
               struct addrinfo **addr_info);

void
gf_log_dump_graph(FILE *specfp, glusterfs_graph_t *graph);
void
gf_print_trace(int32_t signal, glusterfs_ctx_t *ctx);
int
gf_set_log_file_path(cmd_args_t *cmd_args, glusterfs_ctx_t *ctx);
int
gf_set_log_ident(cmd_args_t *cmd_args);

int
gf_process_getspec_servers_list(cmd_args_t *cmd_args, const char *servers_list);
int
gf_set_volfile_server_common(cmd_args_t *cmd_args, const char *host,
                             const char *transport, int port);

static inline void
BIT_SET(unsigned char *array, unsigned int index)
{
    unsigned int offset = index / 8;
    unsigned int shift = index % 8;

    array[offset] |= (1 << shift);
}

static inline void
BIT_CLEAR(unsigned char *array, unsigned int index)
{
    unsigned int offset = index / 8;
    unsigned int shift = index % 8;

    array[offset] &= ~(1 << shift);
}

static inline unsigned int
BIT_VALUE(unsigned char *array, unsigned int index)
{
    unsigned int offset = index / 8;
    unsigned int shift = index % 8;

    return (array[offset] >> shift) & 0x1;
}

#define VECTORSIZE(count) (count * (sizeof(struct iovec)))

#define STRLEN_0(str) (strlen(str) + 1)

#define VALIDATE_OR_GOTO(arg, label)                                           \
    do {                                                                       \
        if (!arg) {                                                            \
            errno = EINVAL;                                                    \
            gf_msg_callingfn((this ? (this->name) : "(Govinda! Govinda!)"),    \
                             GF_LOG_WARNING, EINVAL, LG_MSG_INVALID_ARG,       \
                             "invalid argument: " #arg);                       \
            goto label;                                                        \
        }                                                                      \
    } while (0)

#define GF_VALIDATE_OR_GOTO(name, arg, label)                                  \
    do {                                                                       \
        if (!arg) {                                                            \
            errno = EINVAL;                                                    \
            gf_msg_callingfn(name, GF_LOG_ERROR, errno, LG_MSG_INVALID_ARG,    \
                             "invalid argument: " #arg);                       \
            goto label;                                                        \
        }                                                                      \
    } while (0)

#define GF_VALIDATE_OR_GOTO_WITH_ERROR(name, arg, label, errno, error)         \
    do {                                                                       \
        if (!arg) {                                                            \
            errno = error;                                                     \
            gf_msg_callingfn(name, GF_LOG_ERROR, EINVAL, LG_MSG_INVALID_ARG,   \
                             "invalid argument: " #arg);                       \
            goto label;                                                        \
        }                                                                      \
    } while (0)

#define GF_CHECK_ALLOC(arg, retval, label)                                     \
    do {                                                                       \
        if (!(arg)) {                                                          \
            retval = -ENOMEM;                                                  \
            goto label;                                                        \
        }                                                                      \
    } while (0)

#define GF_CHECK_ALLOC_AND_LOG(name, item, retval, msg, errlabel)              \
    do {                                                                       \
        if (!(item)) {                                                         \
            (retval) = -ENOMEM;                                                \
            gf_msg(name, GF_LOG_CRITICAL, ENOMEM, LG_MSG_NO_MEMORY, (msg));    \
            goto errlabel;                                                     \
        }                                                                      \
    } while (0)

#define GF_ASSERT_AND_GOTO_WITH_ERROR(name, arg, label, errno, error)          \
    do {                                                                       \
        if (!arg) {                                                            \
            GF_ASSERT(0);                                                      \
            errno = error;                                                     \
            goto label;                                                        \
        }                                                                      \
    } while (0)

#define GF_VALIDATE_ABSOLUTE_PATH_OR_GOTO(name, arg, label)                    \
    do {                                                                       \
        GF_VALIDATE_OR_GOTO(name, arg, label);                                 \
        if ((arg[0]) != '/') {                                                 \
            errno = EINVAL;                                                    \
            gf_msg_callingfn(name, GF_LOG_ERROR, EINVAL, LG_MSG_INVALID_ARG,   \
                             "invalid argument: " #arg);                       \
            goto label;                                                        \
        }                                                                      \
    } while (0)

#define GF_REMOVE_SLASH_FROM_PATH(path, string)                                \
    do {                                                                       \
        int i = 0;                                                             \
        for (i = 1; i < strlen(path); i++) {                                   \
            string[i - 1] = path[i];                                           \
            if (string[i - 1] == '/')                                          \
                string[i - 1] = '-';                                           \
        }                                                                      \
    } while (0)

#define GF_REMOVE_INTERNAL_XATTR(pattern, dict)                                \
    do {                                                                       \
        if (!dict) {                                                           \
            gf_msg(this->name, GF_LOG_ERROR, 0, LG_MSG_DICT_NULL,              \
                   "dict is null");                                            \
            break;                                                             \
        }                                                                      \
        dict_foreach_fnmatch(dict, pattern, dict_remove_foreach_fn, NULL);     \
    } while (0)

#define GF_IF_INTERNAL_XATTR_GOTO(pattern, dict, op_errno, label)              \
    do {                                                                       \
        if (!dict) {                                                           \
            gf_msg(this->name, GF_LOG_ERROR, 0, LG_MSG_DICT_NULL,              \
                   "setxattr dict is null");                                   \
            goto label;                                                        \
        }                                                                      \
        if (dict_foreach_fnmatch(dict, pattern, dict_null_foreach_fn, NULL) >  \
            0) {                                                               \
            op_errno = EPERM;                                                  \
            gf_msg(this->name, GF_LOG_ERROR, op_errno, LG_MSG_NO_PERM,         \
                   "attempt to set internal"                                   \
                   " xattr: %s",                                               \
                   pattern);                                                   \
            goto label;                                                        \
        }                                                                      \
    } while (0)

#define GF_IF_NATIVE_XATTR_GOTO(pattern, key, op_errno, label)                 \
    do {                                                                       \
        if (!key) {                                                            \
            gf_msg(this->name, GF_LOG_ERROR, 0, LG_MSG_NO_KEY,                 \
                   "no key for removexattr");                                  \
            goto label;                                                        \
        }                                                                      \
        if (!fnmatch(pattern, key, 0)) {                                       \
            op_errno = EPERM;                                                  \
            gf_msg(this->name, GF_LOG_ERROR, op_errno, LG_MSG_NO_PERM,         \
                   "attempt to remove internal "                               \
                   "xattr: %s",                                                \
                   key);                                                       \
            goto label;                                                        \
        }                                                                      \
    } while (0)

#ifdef DEBUG
#define GF_ASSERT(x) assert(x);
#else
#define GF_ASSERT(x)                                                           \
    do {                                                                       \
        if (!(x)) {                                                            \
            gf_msg_callingfn("", GF_LOG_ERROR, 0, LG_MSG_ASSERTION_FAILED,     \
                             "Assertion failed: " #x);                         \
        }                                                                      \
    } while (0)
#endif

/* Compile-time assert, borrowed from Linux kernel. */
#ifdef HAVE_STATIC_ASSERT
#define GF_STATIC_ASSERT(expr, ...)                                            \
    __gf_static_assert(expr, ##__VA_ARGS__, #expr)
#define __gf_static_assert(expr, msg, ...) _Static_assert(expr, msg)
#else
#define GF_STATIC_ASSERT(expr, ...)
#endif

#define GF_ABORT(msg...)                                                       \
    do {                                                                       \
        gf_msg_callingfn("", GF_LOG_CRITICAL, 0, LG_MSG_ASSERTION_FAILED,      \
                         "Assertion failed: " msg);                            \
        abort();                                                               \
    } while (0)

#define GF_UUID_ASSERT(u)                                                      \
    if (gf_uuid_is_null(u))                                                    \
        GF_ASSERT(!"uuid null");

#define GF_IGNORE_IF_GSYNCD_SAFE_ERROR(frame, op_errno)                        \
    (((frame->root->pid == GF_CLIENT_PID_GSYNCD) &&                            \
      (op_errno == EEXIST || op_errno == ENOENT))                              \
         ? 0                                                                   \
         : 1)

union gf_sock_union {
    struct sockaddr_storage storage;
    struct sockaddr_in6 sin6;
    struct sockaddr_in sin;
    struct sockaddr sa;
};

#define GF_HIDDEN_PATH ".glusterfs"
#define GF_UNLINK_PATH GF_HIDDEN_PATH "/unlink"
#define GF_LANDFILL_PATH GF_HIDDEN_PATH "/landfill"

#define IOV_MIN(n) min(IOV_MAX, n)

static inline gf_boolean_t
gf_irrelevant_entry(struct dirent *entry)
{
    GF_ASSERT(entry);

    return (!strcmp(entry->d_name, ".") ||
            !fnmatch("*.tmp", entry->d_name, 0) ||
            !strcmp(entry->d_name, ".."));
}

static inline void
iov_free(struct iovec *vector, int count)
{
    int i;

    for (i = 0; i < count; i++)
        FREE(vector[i].iov_base);

    GF_FREE(vector);
}

static inline int
iov_length(const struct iovec *vector, int count)
{
    int i = 0;
    size_t size = 0;

    for (i = 0; i < count; i++)
        size += vector[i].iov_len;

    return size;
}

static inline struct iovec *
iov_dup(const struct iovec *vector, int count)
{
    int bytecount = 0;
    struct iovec *newvec = NULL;

    bytecount = (count * sizeof(struct iovec));
    newvec = GF_MALLOC(bytecount, gf_common_mt_iovec);
    if (newvec != NULL) {
        memcpy(newvec, vector, bytecount);
    }

    return newvec;
}

typedef struct _iov_iter {
    const struct iovec *iovec;
    void *ptr;
    uint32_t len;
    uint32_t count;
} iov_iter_t;

static inline bool
iov_iter_init(iov_iter_t *iter, const struct iovec *iovec, uint32_t count,
              uint32_t offset)
{
    uint32_t len;

    while (count > 0) {
        count--;
        len = iovec->iov_len;
        if (offset < len) {
            iter->ptr = iovec->iov_base + offset;
            iter->len = len - offset;
            iter->iovec = iovec + 1;
            iter->count = count;

            return true;
        }
        offset -= len;
    }

    memset(iter, 0, sizeof(*iter));

    return false;
}

static inline bool
iov_iter_end(iov_iter_t *iter)
{
    return iter->count == 0;
}

static inline bool
iov_iter_next(iov_iter_t *iter, uint32_t size)
{
    GF_ASSERT(size <= iter->len);

    if (iter->len > size) {
        iter->len -= size;
        iter->ptr += size;

        return true;
    }
    if (iter->count > 0) {
        iter->count--;
        iter->ptr = iter->iovec->iov_base;
        iter->len = iter->iovec->iov_len;
        iter->iovec++;

        return true;
    }

    memset(iter, 0, sizeof(*iter));

    return false;
}

static inline uint32_t
iov_iter_copy(iov_iter_t *dst, iov_iter_t *src, uint32_t size)
{
    uint32_t len;

    len = src->len;
    if (len > dst->len) {
        len = dst->len;
    }
    if (len > size) {
        len = size;
    }
    memcpy(dst->ptr, src->ptr, len);

    return len;
}

static inline uint32_t
iov_iter_to_iovec(iov_iter_t *iter, struct iovec *iovec, int32_t idx,
                  uint32_t size)
{
    uint32_t len;

    len = iter->len;
    if (len > size) {
        len = size;
    }
    iovec[idx].iov_base = iter->ptr;
    iovec[idx].iov_len = len;

    return len;
}

static inline int
iov_subset(struct iovec *src, int src_count, uint32_t start, uint32_t size,
           struct iovec **dst, int32_t dst_count)
{
    struct iovec iovec[src_count];
    iov_iter_t iter;
    uint32_t len;
    int32_t idx;

    if ((size == 0) || !iov_iter_init(&iter, src, src_count, start)) {
        return 0;
    }

    idx = 0;
    do {
        len = iov_iter_to_iovec(&iter, iovec, idx, size);
        idx++;
        size -= len;
    } while ((size > 0) && iov_iter_next(&iter, len));

    if (*dst == NULL) {
        *dst = iov_dup(iovec, idx);
        if (*dst == NULL) {
            return -1;
        }
    } else if (idx > dst_count) {
        return -1;
    } else {
        memcpy(*dst, iovec, idx * sizeof(struct iovec));
    }

    return idx;
}

static inline int
iov_skip(struct iovec *iovec, uint32_t count, uint32_t size)
{
    uint32_t len, idx;

    idx = 0;
    while ((size > 0) && (idx < count)) {
        len = iovec[idx].iov_len;
        if (len > size) {
            iovec[idx].iov_len -= size;
            iovec[idx].iov_base += size;
            break;
        }
        idx++;
        size -= len;
    }

    if (idx > 0) {
        memmove(iovec, iovec + idx, (count - idx) * sizeof(struct iovec));
    }

    return count - idx;
}

static inline size_t
iov_range_copy(const struct iovec *dst, uint32_t dst_count, uint32_t dst_offset,
               const struct iovec *src, uint32_t src_count, uint32_t src_offset,
               uint32_t size)
{
    iov_iter_t src_iter, dst_iter;
    uint32_t len, total;

    if ((size == 0) || !iov_iter_init(&src_iter, src, src_count, src_offset) ||
        !iov_iter_init(&dst_iter, dst, dst_count, dst_offset)) {
        return 0;
    }

    total = 0;
    do {
        len = iov_iter_copy(&dst_iter, &src_iter, size);
        total += len;
        size -= len;
    } while ((size > 0) && iov_iter_next(&src_iter, len) &&
             iov_iter_next(&dst_iter, len));

    return total;
}

static inline void
iov_unload(char *buf, const struct iovec *vector, int count)
{
    int i;
    int copied = 0;

    for (i = 0; i < count; i++) {
        memcpy(buf + copied, vector[i].iov_base, vector[i].iov_len);
        copied += vector[i].iov_len;
    }
}

static inline size_t
iov_load(const struct iovec *vector, int count, char *buf, int size)
{
    size_t left = size;
    size_t cp = 0;
    int ret = 0;
    int i = 0;

    while (left && i < count) {
        cp = min(vector[i].iov_len, left);
        if (vector[i].iov_base != buf + (size - left))
            memcpy(vector[i].iov_base, buf + (size - left), cp);
        ret += cp;
        left -= cp;
        if (left)
            i++;
    }

    return ret;
}

static inline size_t
iov_copy(const struct iovec *dst, int dcnt, const struct iovec *src, int scnt)
{
    return iov_range_copy(dst, dcnt, 0, src, scnt, 0, UINT32_MAX);
}

/* based on the amusing discussion @ https://rusty.ozlabs.org/?p=560 */
static bool
memeqzero(const void *data, size_t length)
{
    const unsigned char *p = data;
    size_t len;

    /* Check first 16 bytes manually */
    for (len = 0; len < 16; len++) {
        if (!length)
            return true;
        if (*p)
            return false;
        p++;
        length--;
    }

    /* Now we know that's zero, memcmp with self. */
    return memcmp(data, p, length) == 0;
}

static inline int
mem_0filled(const char *buf, size_t size)
{
    return !memeqzero(buf, size);
}

static inline int
iov_0filled(const struct iovec *vector, int count)
{
    int i = 0;
    int ret = 0;

    for (i = 0; i < count; i++) {
        ret = mem_0filled(vector[i].iov_base, vector[i].iov_len);
        if (ret)
            break;
    }

    return ret;
}

typedef enum {
    gf_timefmt_default = 0,
    gf_timefmt_FT = 0, /* YYYY-MM-DD hh:mm:ss */
    gf_timefmt_Ymd_T,  /* YYYY/MM-DD-hh:mm:ss */
    gf_timefmt_bdT,    /* MMM DD hh:mm:ss */
    gf_timefmt_F_HMS,  /* YYYY-MM-DD hhmmss */
    gf_timefmt_dirent,
    gf_timefmt_s,
    gf_timefmt_last
} gf_timefmts;

static inline char *
gf_time_fmt_tv(char *dst, size_t sz_dst, struct timeval *tv, unsigned int fmt)
{
    extern void _gf_timestuff(const char ***, const char ***);
    static gf_timefmts timefmt_last = (gf_timefmts)-1;
    static const char **fmts;
    static const char **zeros;
    struct tm tm, *res;
    int localtime = 0;
    int len = 0;
    int pos = 0;

    if (timefmt_last == ((gf_timefmts)-1)) {
        _gf_timestuff(&fmts, &zeros);
        timefmt_last = gf_timefmt_last;
    }
    if (timefmt_last <= fmt) {
        fmt = gf_timefmt_default;
    }
    localtime = gf_log_get_localtime();
    res = localtime ? localtime_r(&tv->tv_sec, &tm)
                    : gmtime_r(&tv->tv_sec, &tm);
    if (tv->tv_sec && (res != NULL)) {
        len = strftime(dst, sz_dst, fmts[fmt], &tm);
        if (len == 0)
            return dst;
        pos += len;
        if (tv->tv_usec >= 0) {
            len = snprintf(dst + pos, sz_dst - pos, ".%" GF_PRI_SUSECONDS,
                           tv->tv_usec);
            if (len >= sz_dst - pos)
                return dst;
            pos += len;
        }
        strftime(dst + pos, sz_dst - pos, " %z", &tm);
    } else {
        strncpy(dst, "N/A", sz_dst);
    }
    return dst;
}

static inline char *
gf_time_fmt(char *dst, size_t sz_dst, time_t utime, unsigned int fmt)
{
    struct timeval tv = {utime, -1};

    return gf_time_fmt_tv(dst, sz_dst, &tv, fmt);
}

/* This function helps us use gfid (unique identity) to generate inode's unique
 * number in glusterfs.
 */
ino_t
gfid_to_ino(uuid_t gfid);

int
mkdir_p(char *path, mode_t mode, gf_boolean_t allow_symlinks);
/*
 * rounds up nr to power of two. If nr is already a power of two, just returns
 * nr
 */

int
gf_lstat_dir(const char *path, struct stat *stbuf_in);

int32_t
gf_roundup_power_of_two(int32_t nr);

/*
 * rounds up nr to next power of two. If nr is already a power of two, next
 * power of two is returned.
 */

int32_t
gf_roundup_next_power_of_two(int32_t nr);

char *
gf_trim(char *string);
int
gf_volume_name_validate(const char *volume_name);

int
gf_string2long(const char *str, long *n);
int
gf_string2ulong(const char *str, unsigned long *n);
int
gf_string2int(const char *str, int *n);
int
gf_string2uint(const char *str, unsigned int *n);
int
gf_string2double(const char *str, double *n);
int
gf_string2longlong(const char *str, long long *n);
int
gf_string2ulonglong(const char *str, unsigned long long *n);

int
gf_string2int8(const char *str, int8_t *n);
int
gf_string2int16(const char *str, int16_t *n);
int
gf_string2int32(const char *str, int32_t *n);
int
gf_string2int64(const char *str, int64_t *n);
int
gf_string2uint8(const char *str, uint8_t *n);
int
gf_string2uint16(const char *str, uint16_t *n);
int
gf_string2uint32(const char *str, uint32_t *n);
int
gf_string2uint64(const char *str, uint64_t *n);

int
gf_strstr(const char *str, const char *delim, const char *match);

int
gf_string2ulong_base10(const char *str, unsigned long *n);
int
gf_string2uint_base10(const char *str, unsigned int *n);
int
gf_string2uint8_base10(const char *str, uint8_t *n);
int
gf_string2uint16_base10(const char *str, uint16_t *n);
int
gf_string2uint32_base10(const char *str, uint32_t *n);
int
gf_string2uint64_base10(const char *str, uint64_t *n);
int
gf_string2bytesize_uint64(const char *str, uint64_t *n);
int
gf_string2bytesize_int64(const char *str, int64_t *n);
int
gf_string2percent_or_bytesize(const char *str, double *n,
                              gf_boolean_t *is_percent);

int
gf_string2boolean(const char *str, gf_boolean_t *b);
int
gf_strn2boolean(const char *str, const int len, gf_boolean_t *b);
int
gf_string2percent(const char *str, double *n);
int
gf_string2time(const char *str, uint32_t *n);

int
gf_lockfd(int fd);
int
gf_unlockfd(int fd);

int
get_checksum_for_file(int fd, uint32_t *checksum, int op_version);
int
log_base2(unsigned long x);

int
get_checksum_for_path(char *path, uint32_t *checksum, int op_version);
int
get_file_mtime(const char *path, time_t *stamp);
char *
gf_resolve_path_parent(const char *path);

char *
strtail(char *str, const char *pattern);
void
skipwhite(char **s);
char *
nwstrtail(char *str, char *pattern);
/* returns a new string with nth word of given string. n>=1 */

typedef struct token_iter {
    char *end;
    char sep;
} token_iter_t;
char *
token_iter_init(char *str, char sep, token_iter_t *tit);
gf_boolean_t
next_token(char **tokenp, token_iter_t *tit);
void
drop_token(char *token, token_iter_t *tit);

gf_boolean_t
mask_match(const uint32_t a, const uint32_t b, const uint32_t m);
gf_boolean_t
gf_is_ip_in_net(const char *network, const char *ip_str);
char
valid_host_name(char *address, int length);
char
valid_ipv4_address(char *address, int length, gf_boolean_t wildcard_acc);
char
valid_ipv6_address(char *address, int length, gf_boolean_t wildcard_acc);
char
valid_internet_address(char *address, gf_boolean_t wildcard_acc,
                       gf_boolean_t cidr);
gf_boolean_t
valid_mount_auth_address(char *address);
gf_boolean_t
valid_ipv4_subnetwork(const char *address);
gf_boolean_t
gf_sock_union_equal_addr(union gf_sock_union *a, union gf_sock_union *b);
char *
gf_rev_dns_lookup(const char *ip);

char *
uuid_utoa(uuid_t uuid);
char *
uuid_utoa_r(uuid_t uuid, char *dst);
char *
lkowner_utoa(gf_lkowner_t *lkowner);
char *
lkowner_utoa_r(gf_lkowner_t *lkowner, char *dst, int len);
char *
leaseid_utoa(const char *lease_id);
gf_boolean_t
is_valid_lease_id(const char *lease_id);
char *
gf_leaseid_get(void);
char *
gf_existing_leaseid(void);

void
gf_array_insertionsort(void *a, int l, int r, size_t elem_size, gf_cmp cmp);
int
gf_is_str_int(const char *value);

char *gf_uint64_2human_readable(uint64_t);
int
validate_brick_name(char *brick);
char *
get_host_name(char *word, char **host);
char *
get_path_name(char *word, char **path);
void
gf_path_strip_trailing_slashes(char *path);
uint64_t
get_mem_size(void);
int
gf_strip_whitespace(char *str, int len);
int
gf_canonicalize_path(char *path);
char *
generate_glusterfs_ctx_id(void);
char *
gf_get_reserved_ports(void);
int
gf_process_reserved_ports(unsigned char *ports, uint32_t ceiling);
gf_boolean_t
gf_ports_reserved(char *blocked_port, unsigned char *ports, uint32_t ceiling);
int
gf_get_hostname_from_ip(char *client_ip, char **hostname);
gf_boolean_t
gf_is_local_addr(char *hostname);
gf_boolean_t
gf_is_same_address(char *host1, char *host2);
void
gf_xxh64_wrapper(const unsigned char *data, size_t const len,
                 unsigned long long const seed, char *xxh64);
int
gf_gfid_generate_from_xxh64(uuid_t gfid, char *key);

int
gf_set_timestamp(const char *src, const char *dest);

int
gf_thread_create(pthread_t *thread, const pthread_attr_t *attr,
                 void *(*start_routine)(void *), void *arg, const char *name,
                 ...) __attribute__((__format__(__printf__, 5, 6)));

int
gf_thread_vcreate(pthread_t *thread, const pthread_attr_t *attr,
                  void *(*start_routine)(void *), void *arg, const char *name,
                  va_list args);
int
gf_thread_create_detached(pthread_t *thread, void *(*start_routine)(void *),
                          void *arg, const char *name, ...)
    __attribute__((__format__(__printf__, 4, 5)));

void
gf_thread_set_name(pthread_t thread, const char *name, ...)
    __attribute__((__format__(__printf__, 2, 3)));

void
gf_thread_set_vname(pthread_t thread, const char *name, va_list args);
gf_boolean_t
gf_is_pid_running(int pid);
gf_boolean_t
gf_is_service_running(char *pidfile, int *pid);
gf_boolean_t
gf_valid_pid(const char *pid, int length);
int
gf_skip_header_section(int fd, int header_len);

struct iatt;
struct _dict;

gf_boolean_t
dht_is_linkfile(struct iatt *buf, struct _dict *dict);

int
gf_check_log_format(const char *value);

int
gf_check_logger(const char *value);

gf_boolean_t
gf_compare_sockaddr(const struct sockaddr *addr1, const struct sockaddr *addr2);

char *
gf_backtrace_save(char *buf);

void
gf_backtrace_done(char *buf);

gf_loglevel_t
fop_log_level(glusterfs_fop_t fop, int op_errno);

int32_t
gf_build_absolute_path(char *current_path, char *relative_path, char **path);

int
recursive_rmdir(const char *delete_path);

int
gf_get_index_by_elem(char **array, char *elem);

int
glusterfs_is_local_pathinfo(char *pathinfo, gf_boolean_t *local);

int
gf_thread_cleanup_xint(pthread_t thread);

ssize_t
gf_nread(int fd, void *buf, size_t count);

ssize_t
gf_nwrite(int fd, const void *buf, size_t count);

void
_mask_cancellation(void);
void
_unmask_cancellation(void);

gf_boolean_t
gf_is_zero_filled_stat(struct iatt *buf);

void
gf_zero_fill_stat(struct iatt *buf);

gf_boolean_t
gf_is_valid_xattr_namespace(char *k);

const char *
gf_inode_type_to_str(ia_type_t type);

int32_t
gf_bits_count(uint64_t n);

int32_t
gf_bits_index(uint64_t n);

const char *
gf_fop_string(glusterfs_fop_t fop);

int
gf_fop_int(char *fop);

char *
get_ip_from_addrinfo(struct addrinfo *addr, char **ip);

int
close_fds_except(int *fdv, size_t count);

int
gf_getgrouplist(const char *user, gid_t group, gid_t **groups);

int
glusterfs_compute_sha256(const unsigned char *content, size_t size,
                         char *sha256_hash);

char *
gf_strncpy(char *dest, const char *src, const size_t dest_size);

void
gf_strTrim(char **s);

int
gf_replace_old_iatt_in_dict(struct _dict *);

int
gf_replace_new_iatt_in_dict(struct _dict *);

xlator_cmdline_option_t *
find_xlator_option_in_cmd_args_t(const char *option_name, cmd_args_t *args);

int
gf_d_type_from_ia_type(ia_type_t type);

int
gf_syncfs(int fd);

int
gf_nanosleep(uint64_t nsec);

static inline time_t
gf_time(void)
{
    return time(NULL);
}

/* Return delta value in microseconds. */

static inline double
gf_tvdiff(struct timeval *start, struct timeval *end)
{
    struct timeval t;

    if (start->tv_usec > end->tv_usec)
        t.tv_sec = end->tv_sec - 1, t.tv_usec = end->tv_usec + 1000000;
    else
        t.tv_sec = end->tv_sec, t.tv_usec = end->tv_usec;

    return (double)(t.tv_sec - start->tv_sec) * 1e6 +
           (double)(t.tv_usec - start->tv_usec);
}

/* Return delta value in nanoseconds. */

static inline double
gf_tsdiff(struct timespec *start, struct timespec *end)
{
    struct timespec t;

    if (start->tv_nsec > end->tv_nsec)
        t.tv_sec = end->tv_sec - 1, t.tv_nsec = end->tv_nsec + 1000000000;
    else
        t.tv_sec = end->tv_sec, t.tv_nsec = end->tv_nsec;

    return (double)(t.tv_sec - start->tv_sec) * 1e9 +
           (double)(t.tv_nsec - start->tv_nsec);
}

#endif /* _COMMON_UTILS_H */
