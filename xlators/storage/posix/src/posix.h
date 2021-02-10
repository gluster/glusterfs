/*
   Copyright (c) 2006-2012 Red Hat, Inc. <http://www.redhat.com>
   This file is part of GlusterFS.

   This file is licensed to you under your choice of the GNU Lesser
   General Public License, version 3 or any later version (LGPLv3 or
   later), or the GNU General Public License, version 2 (GPLv2), in all
   cases as published by the Free Software Foundation.
*/
#ifndef _POSIX_H
#define _POSIX_H

#include <stdio.h>
#include <unistd.h>
#include <sys/types.h>
#include <dirent.h>
#include <time.h>

#ifdef HAVE_SET_FSID
#include <sys/fsuid.h>
#endif

#ifdef HAVE_SYS_XATTR_H
#include <sys/xattr.h>
#endif

#ifdef HAVE_SYS_EXTATTR_H
#include <sys/extattr.h>
#endif

#include <glusterfs/compat.h>
#include <glusterfs/timer.h>
#include "posix-mem-types.h"
#include <glusterfs/call-stub.h>

#ifdef HAVE_LIBAIO
#include <libaio.h>
#include "posix-aio.h"
#endif

#ifdef HAVE_LIBURING
#include <liburing.h>
#include "posix-io-uring.h"
#endif

#define VECTOR_SIZE 64 * 1024 /* vector size 64KB*/
#define MAX_NO_VECT 1024

#define XATTR_KEY_BUF_SIZE 4096
#define XATTR_VAL_BUF_SIZE 8192

#define ACL_BUFFER_MAX 4096 /* size of character buffer */

#define DHT_LINKTO "trusted.glusterfs.dht.linkto"

#define POSIX_GFID_HANDLE_SIZE(base_path_len)                                  \
    (base_path_len + SLEN("/") + SLEN(GF_HIDDEN_PATH) + SLEN("/") +            \
     SLEN("00/") + SLEN("00/") + SLEN(UUID0_STR) + 1) /* '\0' */;

#define POSIX_GFID_HANDLE_RELSIZE                                              \
    SLEN("../") + SLEN("../") + SLEN("00/") + SLEN("00/") + SLEN(UUID0_STR) + 1;

#define GF_UNLINK_TRUE 0x0000000000000001
#define GF_UNLINK_FALSE 0x0000000000000000

#define DISK_SPACE_CHECK_AND_GOTO(frame, priv, xdata, op_ret, op_errno, out)   \
    do {                                                                       \
        if (frame->root->pid >= 0 && priv->disk_space_full &&                  \
            !dict_get_sizen(xdata, GLUSTERFS_INTERNAL_FOP_KEY)) {              \
            op_ret = -1;                                                       \
            op_errno = ENOSPC;                                                 \
            gf_msg_debug("posix", ENOSPC,                                      \
                         "disk space utilization reached limits"               \
                         " for path %s ",                                      \
                         priv->base_path);                                     \
            goto out;                                                          \
        }                                                                      \
    } while (0)

/* Setting microseconds or nanoseconds depending on what's supported:
   The passed in `tv` can be
       struct timespec
   if supported (better, because it supports nanosecond resolution) or
       struct timeval
   otherwise. */
#if HAVE_UTIMENSAT
#define SET_TIMESPEC_NSEC_OR_TIMEVAL_USEC(tv, nanosecs) tv.tv_nsec = nanosecs
#define PATH_SET_TIMESPEC_OR_TIMEVAL(path, tv)                                 \
    (sys_utimensat(AT_FDCWD, path, tv, AT_SYMLINK_NOFOLLOW))
#else
#define SET_TIMESPEC_NSEC_OR_TIMEVAL_USEC(tv, nanosecs)                        \
    tv.tv_usec = nanosecs / 1000
#define PATH_SET_TIMESPEC_OR_TIMEVAL(path, tv) (lutimes(path, tv))
#endif

#define GFID_NULL_CHECK_AND_GOTO(frame, this, loc, xattr_req, op_ret,          \
                                 op_errno, _uuid_req, out)                     \
    do {                                                                       \
        int _ret = 0;                                                          \
        /* TODO: Remove pid check once trash implements client side            \
         * logic to assign gfid for entry creations inside .trashcan           \
         */                                                                    \
        if (frame->root->pid == GF_SERVER_PID_TRASH)                           \
            break;                                                             \
        _ret = dict_get_gfuuid(xattr_req, "gfid-req", &_uuid_req);             \
        if (_ret) {                                                            \
            gf_msg(this->name, GF_LOG_ERROR, EINVAL, P_MSG_NULL_GFID,          \
                   "failed to get the gfid from dict for %s", loc->path);      \
            op_ret = -1;                                                       \
            op_errno = EINVAL;                                                 \
            goto out;                                                          \
        }                                                                      \
        if (gf_uuid_is_null(_uuid_req)) {                                      \
            gf_msg(this->name, GF_LOG_ERROR, EINVAL, P_MSG_NULL_GFID,          \
                   "gfid is null for %s", loc->path);                          \
            op_ret = -1;                                                       \
            op_errno = EINVAL;                                                 \
            goto out;                                                          \
        }                                                                      \
    } while (0)

/**
 * posix_fd - internal structure common to file and directory fd's
 */

struct posix_fd {
    int fd;                /* fd returned by the kernel */
    int32_t flags;         /* flags for open/creat      */
    DIR *dir;              /* handle returned by the kernel */
    off_t dir_eof;         /* offset at dir EOF */
    struct list_head list; /* to add to the janitor list */
    int odirect;
    xlator_t *xl;
    char _pad[4]; /* manual padding */
};

struct posix_diskxl {
    pthread_cond_t cond;
    struct list_head list;
    xlator_t *xl;
    gf_boolean_t detach_notify;
    gf_boolean_t is_use;
};

struct posix_private {
    char *base_path;
    int32_t base_path_length;
    int32_t path_max;

    gf_lock_t lock;

    char *hostname;

    time_t last_landfill_check;

    gf_atomic_t read_value;  /* Total read, from init */
    gf_atomic_t write_value; /* Total write, from init */

    /* janitor task which cleans up /.trash (created by replicate) */
    struct gf_tw_timer_list *janitor;

    char *trash_path;
    /* lock for brick dir */
    int mount_lock;

    struct stat handledir;

    /* uuid of glusterd that swapned the brick process */
    uuid_t glusterd_uuid;

#ifdef HAVE_LIBAIO
    io_context_t ctxp;
    pthread_t aiothread;
#endif

    pthread_t fsyncer;
    struct list_head fsyncs;
    pthread_mutex_t fsync_mutex;
    pthread_cond_t fsync_cond;
    pthread_mutex_t janitor_mutex;
    pthread_cond_t janitor_cond;
    pthread_cond_t fd_cond;
    pthread_cond_t disk_cond;
    int fsync_queue_count;
    int32_t janitor_sleep_duration;

    enum {
        BATCH_NONE = 0,
        BATCH_SYNCFS,
        BATCH_SYNCFS_SINGLE_FSYNC,
        BATCH_REVERSE_FSYNC,
        BATCH_SYNCFS_REVERSE_FSYNC
    } batch_fsync_mode;

    uint32_t batch_fsync_delay_usec;
    char gfid2path_sep[8];

    /* seconds to sleep between health checks */
    uint32_t health_check_interval;
    /* seconds to sleep to wait for aio write finish for health checks */
    uint32_t health_check_timeout;
    pthread_t health_check;

    double disk_reserve;
    pthread_t disk_space_check;
    uint32_t disk_space_full;

#ifdef GF_DARWIN_HOST_OS
    enum {
        XATTR_NONE = 0,
        XATTR_STRIP,
        XATTR_APPEND,
        XATTR_BOTH,
    } xattr_user_namespace;
#endif

    /* Option to handle the cases of multiple bricks exported from
       same backend. Very much usable in brick-splitting feature. */
    int32_t shared_brick_count;

    /*Option to set mode bit permission that will always be set on
      file/directory. */
    mode_t force_create_mode;
    mode_t force_directory_mode;
    mode_t create_mask;
    mode_t create_directory_mask;
    uint32_t max_hardlinks;
    int32_t arrdfd[256];
    int dirfd;

    /* This option is used for either to call a landfill_purge or not */
    gf_boolean_t disable_landfill_purge;

    gf_boolean_t fips_mode_rchecksum;
    gf_boolean_t ctime;
    gf_boolean_t janitor_task_stop;

    char disk_unit;
    gf_boolean_t health_check_active;
    gf_boolean_t update_pgfid_nlinks;
    gf_boolean_t gfid2path;
    /* node-uuid in pathinfo xattr */
    gf_boolean_t node_uuid_pathinfo;
    /*
       In some cases, two exported volumes may reside on the same
       partition on the server. Sending statvfs info for both
       the volumes will lead to erroneous df output at the client,
       since free space on the partition will be counted twice.

       In such cases, user can disable exporting statvfs info
       on one of the volumes by setting this option.
    */
    gf_boolean_t export_statfs;

    gf_boolean_t o_direct; /* always open files in O_DIRECT mode */

    /*
       decide whether posix_unlink does open (file), unlink (file), close (fd)
       instead of just unlink (file). with the former approach there is no
       lockout of access to parent directory during removal of very large files
       for the entire duration of freeing of data blocks.
    */
    gf_boolean_t background_unlink;
    gf_boolean_t aio_configured;
    gf_boolean_t aio_init_done;
    gf_boolean_t aio_capable;
    uint32_t rel_fdcount;

    /*io_uring related.*/
    gf_boolean_t io_uring_configured;
#ifdef HAVE_LIBURING
    struct io_uring ring;
    gf_boolean_t io_uring_init_done;
    gf_boolean_t io_uring_capable;
    gf_boolean_t uring_thread_exit;
    pthread_t uring_thread;
    pthread_mutex_t sq_mutex;
    pthread_mutex_t cq_mutex;
#endif
    void *pxl;
};

typedef struct {
    call_frame_t *frame;
    xlator_t *this;
    const char *real_path;
    dict_t *xattr;
    struct iatt *stbuf;
    loc_t *loc;
    inode_t *inode; /* for all do_xattrop() key handling */
    fd_t *fd;
    int fdnum;
    int flags;
    char *list;
    size_t list_size;
    int32_t op_errno;

    char _pad[4]; /* manual padding */
} posix_xattr_filler_t;

typedef struct {
    uint64_t unlink_flag;
    pthread_mutex_t xattrop_lock;
    pthread_mutex_t write_atomic_lock;
    pthread_mutex_t pgfid_lock;
} posix_inode_ctx_t;

#define POSIX_BASE_PATH(this)                                                  \
    (((struct posix_private *)this->private)->base_path)

#define POSIX_BASE_PATH_LEN(this)                                              \
    (((struct posix_private *)this->private)->base_path_length)

#define POSIX_PATH_MAX(this) (((struct posix_private *)this->private)->path_max)

#define POSIX_GET_FILE_UNLINK_PATH(base_path, gfid, unlink_path)               \
    do {                                                                       \
        int path_len = 0;                                                      \
        char gfid_str[64] = {0};                                               \
        uuid_utoa_r(gfid, gfid_str);                                           \
        path_len = strlen(base_path) + 1 + SLEN(GF_UNLINK_PATH) + 1 +          \
                   UUID_CANONICAL_FORM_LEN + 1;                                \
        unlink_path = alloca(path_len);                                        \
        if (!unlink_path) {                                                    \
            gf_msg("posix", GF_LOG_ERROR, ENOMEM, P_MSG_UNLINK_FAILED,         \
                   "Failed to get unlink_path");                               \
            break;                                                             \
        }                                                                      \
        sprintf(unlink_path, "%s/%s/%s", base_path, GF_UNLINK_PATH, gfid_str); \
    } while (0)

/* Helper functions */
int
posix_inode_ctx_set_unlink_flag(inode_t *inode, xlator_t *this, uint64_t ctx);

int
posix_inode_ctx_get_all(inode_t *inode, xlator_t *this,
                        posix_inode_ctx_t **ctx);

int
__posix_inode_ctx_set_unlink_flag(inode_t *inode, xlator_t *this, uint64_t ctx);

int
__posix_inode_ctx_get_all(inode_t *inode, xlator_t *this,
                          posix_inode_ctx_t **ctx);

int
posix_gfid_set(xlator_t *this, const char *path, loc_t *loc, dict_t *xattr_req,
               pid_t pid, int *op_errno);
int
posix_fdstat(xlator_t *this, inode_t *inode, int fd, struct iatt *stbuf_p);
int
posix_istat(xlator_t *this, inode_t *inode, uuid_t gfid, const char *basename,
            struct iatt *iatt);
int
posix_pstat(xlator_t *this, inode_t *inode, uuid_t gfid, const char *real_path,
            struct iatt *iatt, gf_boolean_t inode_locked);

dict_t *
posix_xattr_fill(xlator_t *this, const char *path, loc_t *loc, fd_t *fd,
                 int fdnum, dict_t *xattr, struct iatt *buf);
int
posix_handle_pair(xlator_t *this, loc_t *loc, const char *real_path, char *key,
                  data_t *value, int flags, struct iatt *stbuf);
int
posix_fhandle_pair(call_frame_t *frame, xlator_t *this, int fd, char *key,
                   data_t *value, int flags, struct iatt *stbuf, fd_t *_fd);
void
posix_janitor_timer_start(xlator_t *this);
int
posix_acl_xattr_set(xlator_t *this, const char *path, dict_t *xattr_req);
int
posix_gfid_heal(xlator_t *this, const char *path, loc_t *loc,
                dict_t *xattr_req);
int
posix_entry_create_xattr_set(xlator_t *this, loc_t *loc, const char *path,
                             dict_t *dict);

int
posix_fd_ctx_get(fd_t *fd, xlator_t *this, struct posix_fd **pfd,
                 int *op_errno);
void
posix_fill_ino_from_gfid(xlator_t *this, struct iatt *buf);

gf_boolean_t
posix_special_xattr(char **pattern, char *key);

void
__posix_fd_set_odirect(fd_t *fd, struct posix_fd *pfd, int opflags,
                       off_t offset, size_t size);
int
posix_spawn_health_check_thread(xlator_t *this);

int
posix_spawn_disk_space_check_thread(xlator_t *this);

void *
posix_fsyncer(void *);
int
posix_get_ancestry(xlator_t *this, inode_t *leaf_inode, gf_dirent_t *head,
                   char **path, int type, int32_t *op_errno, dict_t *xdata);
int
posix_handle_mdata_xattr(call_frame_t *frame, const char *name, int *op_errno);
int
posix_handle_georep_xattrs(call_frame_t *, const char *, int *, gf_boolean_t);
int32_t
posix_resolve_dirgfid_to_path(const uuid_t dirgfid, const char *brick_path,
                              const char *bname, char **path);
void
posix_gfid_unset(xlator_t *this, dict_t *xdata);

int
posix_pacl_get(const char *path, int fdnum, const char *key, char **acl_s);

int32_t
posix_get_objectsignature(char *, dict_t *);

int32_t
posix_fdget_objectsignature(int, dict_t *);

gf_boolean_t
posix_is_bulk_removexattr(char *name, dict_t *dict);

int32_t
posix_set_iatt_in_dict(dict_t *, struct iatt *, struct iatt *);

mode_t posix_override_umask(mode_t, mode_t);

int32_t
posix_priv(xlator_t *this);

int32_t
posix_inode(xlator_t *this);

void
posix_fini(xlator_t *this);

int
posix_init(xlator_t *this);

int
posix_reconfigure(xlator_t *this, dict_t *options);

int32_t
posix_notify(xlator_t *this, int32_t event, void *data, ...);

/* posix-entry-ops.c FOP signatures */
int32_t
posix_lookup(call_frame_t *frame, xlator_t *this, loc_t *loc, dict_t *xdata);

int
posix_create(call_frame_t *frame, xlator_t *this, loc_t *loc, int32_t flags,
             mode_t mode, mode_t umask, fd_t *fd, dict_t *xdata);

int
posix_symlink(call_frame_t *frame, xlator_t *this, const char *linkname,
              loc_t *loc, mode_t umask, dict_t *xdata);

int
posix_rename(call_frame_t *frame, xlator_t *this, loc_t *oldloc, loc_t *newloc,
             dict_t *xdata);

int
posix_link(call_frame_t *frame, xlator_t *this, loc_t *oldloc, loc_t *newloc,
           dict_t *xdata);

int
posix_mknod(call_frame_t *frame, xlator_t *this, loc_t *loc, mode_t mode,
            dev_t dev, mode_t umask, dict_t *xdata);

int
posix_mkdir(call_frame_t *frame, xlator_t *this, loc_t *loc, mode_t mode,
            mode_t umask, dict_t *xdata);

int32_t
posix_unlink(call_frame_t *frame, xlator_t *this, loc_t *loc, int xflag,
             dict_t *xdata);

int
posix_rmdir(call_frame_t *frame, xlator_t *this, loc_t *loc, int flags,
            dict_t *xdata);

/* posix-inode-fs-ops.c FOP signatures */
int
posix_forget(xlator_t *this, inode_t *inode);

int32_t
posix_discover(call_frame_t *frame, xlator_t *this, loc_t *loc, dict_t *xdata);

int32_t
posix_stat(call_frame_t *frame, xlator_t *this, loc_t *loc, dict_t *xdata);

int
posix_setattr(call_frame_t *frame, xlator_t *this, loc_t *loc,
              struct iatt *stbuf, int32_t valid, dict_t *xdata);

int
posix_fsetattr(call_frame_t *frame, xlator_t *this, fd_t *fd,
               struct iatt *stbuf, int32_t valid, dict_t *xdata);

int32_t
posix_discard(call_frame_t *frame, xlator_t *this, fd_t *fd, off_t offset,
              size_t len, dict_t *xdata);

int32_t
posix_zerofill(call_frame_t *frame, xlator_t *this, fd_t *fd, off_t offset,
               off_t len, dict_t *xdata);

int32_t
posix_glfallocate(call_frame_t *frame, xlator_t *this, fd_t *fd,
                  int32_t keep_size, off_t offset, size_t len, dict_t *xdata);

int32_t
posix_ipc(call_frame_t *frame, xlator_t *this, int32_t op, dict_t *xdata);

int32_t
posix_seek(call_frame_t *frame, xlator_t *this, fd_t *fd, off_t offset,
           gf_seek_what_t what, dict_t *xdata);

int32_t
posix_opendir(call_frame_t *frame, xlator_t *this, loc_t *loc, fd_t *fd,
              dict_t *xdata);

int32_t
posix_releasedir(xlator_t *this, fd_t *fd);

int32_t
posix_readlink(call_frame_t *frame, xlator_t *this, loc_t *loc, size_t size,
               dict_t *xdata);

int32_t
posix_truncate(call_frame_t *frame, xlator_t *this, loc_t *loc, off_t offset,
               dict_t *xdata);

int32_t
posix_open(call_frame_t *frame, xlator_t *this, loc_t *loc, int32_t flags,
           fd_t *fd, dict_t *xdata);

int
posix_readv(call_frame_t *frame, xlator_t *this, fd_t *fd, size_t size,
            off_t offset, uint32_t flags, dict_t *xdata);

int32_t
posix_writev(call_frame_t *frame, xlator_t *this, fd_t *fd,
             struct iovec *vector, int32_t count, off_t offset, uint32_t flags,
             struct iobref *iobref, dict_t *xdata);

int32_t
posix_statfs(call_frame_t *frame, xlator_t *this, loc_t *loc, dict_t *xdata);

int32_t
posix_flush(call_frame_t *frame, xlator_t *this, fd_t *fd, dict_t *xdata);

int32_t
posix_release(xlator_t *this, fd_t *fd);

int32_t
posix_fsync(call_frame_t *frame, xlator_t *this, fd_t *fd, int32_t datasync,
            dict_t *xdata);

int32_t
posix_setxattr(call_frame_t *frame, xlator_t *this, loc_t *loc, dict_t *dict,
               int flags, dict_t *xdata);

int
posix_get_ancestry_non_directory(xlator_t *this, inode_t *leaf_inode,
                                 gf_dirent_t *head, char **path, int type,
                                 int32_t *op_errno, dict_t *xdata);

int
posix_get_ancestry(xlator_t *this, inode_t *leaf_inode, gf_dirent_t *head,
                   char **path, int type, int32_t *op_errno, dict_t *xdata);

int32_t
posix_getxattr(call_frame_t *frame, xlator_t *this, loc_t *loc,
               const char *name, dict_t *xdata);

int32_t
posix_fgetxattr(call_frame_t *frame, xlator_t *this, fd_t *fd, const char *name,
                dict_t *xdata);

int32_t
posix_fsetxattr(call_frame_t *frame, xlator_t *this, fd_t *fd, dict_t *dict,
                int flags, dict_t *xdata);

int32_t
posix_removexattr(call_frame_t *frame, xlator_t *this, loc_t *loc,
                  const char *name, dict_t *xdata);

int32_t
posix_fremovexattr(call_frame_t *frame, xlator_t *this, fd_t *fd,
                   const char *name, dict_t *xdata);

int32_t
posix_fsyncdir(call_frame_t *frame, xlator_t *this, fd_t *fd, int datasync,
               dict_t *xdata);

int
posix_xattrop(call_frame_t *frame, xlator_t *this, loc_t *loc,
              gf_xattrop_flags_t optype, dict_t *xattr, dict_t *xdata);

int
posix_fxattrop(call_frame_t *frame, xlator_t *this, fd_t *fd,
               gf_xattrop_flags_t optype, dict_t *xattr, dict_t *xdata);

int
posix_access(call_frame_t *frame, xlator_t *this, loc_t *loc, int32_t mask,
             dict_t *xdata);

int32_t
posix_ftruncate(call_frame_t *frame, xlator_t *this, fd_t *fd, off_t offset,
                dict_t *xdata);

int32_t
posix_fstat(call_frame_t *frame, xlator_t *this, fd_t *fd, dict_t *xdata);

int32_t
posix_lease(call_frame_t *frame, xlator_t *this, loc_t *loc,
            struct gf_lease *lease, dict_t *xdata);

int32_t
posix_lk(call_frame_t *frame, xlator_t *this, fd_t *fd, int32_t cmd,
         struct gf_flock *lock, dict_t *xdata);

int32_t
posix_inodelk(call_frame_t *frame, xlator_t *this, const char *volume,
              loc_t *loc, int32_t cmd, struct gf_flock *lock, dict_t *xdata);

int32_t
posix_finodelk(call_frame_t *frame, xlator_t *this, const char *volume,
               fd_t *fd, int32_t cmd, struct gf_flock *lock, dict_t *xdata);

int32_t
posix_entrylk(call_frame_t *frame, xlator_t *this, const char *volume,
              loc_t *loc, const char *basename, entrylk_cmd cmd,
              entrylk_type type, dict_t *xdata);

int32_t
posix_fentrylk(call_frame_t *frame, xlator_t *this, const char *volume,
               fd_t *fd, const char *basename, entrylk_cmd cmd,
               entrylk_type type, dict_t *xdata);

int32_t
posix_readdir(call_frame_t *frame, xlator_t *this, fd_t *fd, size_t size,
              off_t off, dict_t *xdata);

int32_t
posix_readdirp(call_frame_t *frame, xlator_t *this, fd_t *fd, size_t size,
               off_t off, dict_t *dict);

int32_t
posix_rchecksum(call_frame_t *frame, xlator_t *this, fd_t *fd, off_t offset,
                int32_t len, dict_t *xdata);

int32_t
posix_put(call_frame_t *frame, xlator_t *this, loc_t *loc, mode_t mode,
          mode_t umask, uint32_t flags, struct iovec *vector, int32_t count,
          off_t offset, struct iobref *iobref, dict_t *xattr, dict_t *xdata);

int32_t
posix_copy_file_range(call_frame_t *frame, xlator_t *this, fd_t *fd_in,
                      off64_t off_in, fd_t *fd_out, off64_t off_out, size_t len,
                      uint32_t flags, dict_t *xdata);

int32_t
posix_set_mode_in_dict(dict_t *in_dict, dict_t *out_dict,
                       struct iatt *in_stbuf);

gf_cs_obj_state
posix_cs_check_status(xlator_t *this, const char *realpath, int *fd,
                      struct iatt *buf);

int
posix_cs_set_state(xlator_t *this, dict_t **rsp, gf_cs_obj_state state,
                   char const *path, int *fd);

gf_cs_obj_state
posix_cs_heal_state(xlator_t *this, const char *path, int *fd,
                    struct iatt *stbuf);
int
posix_cs_maintenance(xlator_t *this, fd_t *fd, loc_t *loc, int *pfd,
                     struct iatt *buf, const char *realpath, dict_t *xattr_req,
                     dict_t **xattr_rsp, gf_boolean_t ignore_failure);
int
posix_check_dev_file(xlator_t *this, inode_t *inode, char *fop, int *op_errno);

int
posix_spawn_ctx_janitor_thread(xlator_t *this);

void
posix_update_iatt_buf(struct iatt *buf, int fd, char *loc, dict_t *xdata);

gf_boolean_t
posix_is_layout_stale(dict_t *xdata, char *par_path, xlator_t *this);

int
posix_delete_user_xattr(dict_t *dict, char *k, data_t *v, void *data);

#endif /* _POSIX_H */
