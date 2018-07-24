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

#ifdef linux
#ifdef __GLIBC__
#include <sys/fsuid.h>
#else
#include <unistd.h>
#endif
#endif

#ifdef HAVE_SYS_XATTR_H
#include <sys/xattr.h>
#endif

#ifdef HAVE_SYS_EXTATTR_H
#include <sys/extattr.h>
#endif

#include "xlator.h"
#include "compat.h"
#include "timer.h"
#include "posix-mem-types.h"
#include "posix-handle.h"
#include "call-stub.h"

#ifdef HAVE_LIBAIO
#include <libaio.h>
#include "posix-aio.h"
#endif

#define VECTOR_SIZE 64 * 1024 /* vector size 64KB*/
#define MAX_NO_VECT 1024

#define XATTR_KEY_BUF_SIZE 4096
#define XATTR_VAL_BUF_SIZE 8192

#define ACL_BUFFER_MAX 4096 /* size of character buffer */

#define DHT_LINKTO "trusted.glusterfs.dht.linkto"
/*
 * TIER_MODE need to be changed when we stack tiers
 */
#define TIER_LINKTO "trusted.tier.tier-dht.linkto"

#define POSIX_GFID_HANDLE_SIZE(base_path_len) (base_path_len + SLEN("/") \
                                               + SLEN(GF_HIDDEN_PATH) + SLEN("/") \
                                               + SLEN("00/")            \
                                               + SLEN("00/") + SLEN(UUID0_STR) + 1) /* '\0' */;
#define GF_UNLINK_TRUE 0x0000000000000001
#define GF_UNLINK_FALSE 0x0000000000000000

#define DISK_SPACE_CHECK_AND_GOTO(frame, priv, xdata, op_ret, op_errno, out)  do {   \
               if (frame->root->pid >= 0 && priv->disk_space_full &&          \
                   !dict_get (xdata, GLUSTERFS_INTERNAL_FOP_KEY)) {          \
                        op_ret = -1;                                          \
                        op_errno = ENOSPC;                                    \
                        gf_msg_debug ("posix", ENOSPC,                        \
                                      "disk space utilization reached limits" \
                                      " for path %s ",  priv->base_path);     \
                        goto out;                                             \
               }                                                              \
        } while (0)

/* Setting microseconds or nanoseconds depending on what's supported:
   The passed in `tv` can be
       struct timespec
   if supported (better, because it supports nanosecond resolution) or
       struct timeval
   otherwise. */
#if HAVE_UTIMENSAT
#define SET_TIMESPEC_NSEC_OR_TIMEVAL_USEC(tv, nanosecs) \
        tv.tv_nsec = nanosecs
#define PATH_SET_TIMESPEC_OR_TIMEVAL(path, tv) \
        (sys_utimensat (AT_FDCWD, path, tv, AT_SYMLINK_NOFOLLOW))
#else
#define SET_TIMESPEC_NSEC_OR_TIMEVAL_USEC(tv, nanosecs) \
        tv.tv_usec = nanosecs / 1000
#define PATH_SET_TIMESPEC_OR_TIMEVAL(path, tv) \
        (lutimes (path, tv))
#endif

#define GFID_NULL_CHECK_AND_GOTO(frame, this, loc, xattr_req, op_ret,         \
                                 op_errno, out)                               \
        do {                                                                  \
                uuid_t _uuid_req;                                             \
                int _ret = 0;                                                 \
                /* TODO: Remove pid check once trash implements client side   \
                 * logic to assign gfid for entry creations inside .trashcan  \
                 */                                                           \
                if (frame->root->pid == GF_SERVER_PID_TRASH)                  \
                        break;                                                \
                _ret = dict_get_gfuuid (xattr_req, "gfid-req", &_uuid_req);   \
                if (_ret) {                                                   \
                        gf_msg (this->name, GF_LOG_ERROR, EINVAL,             \
                               P_MSG_NULL_GFID, "failed to get the gfid from" \
                               " dict for %s", loc->path);                    \
                        op_ret = -1;                                          \
                        op_errno = EINVAL;                                    \
                        goto out;                                             \
                }                                                             \
                if (gf_uuid_is_null (_uuid_req)) {                            \
                        gf_msg (this->name, GF_LOG_ERROR, EINVAL,             \
                                P_MSG_NULL_GFID, "gfid is null for %s",       \
                                loc->path);                                   \
                        op_ret = -1;                                          \
                        op_errno = EINVAL;                                    \
                        goto out;                                             \
                }                                                             \
        } while (0)


/**
 * posix_fd - internal structure common to file and directory fd's
 */

struct posix_fd {
	int     fd;      /* fd returned by the kernel */
	int32_t flags;   /* flags for open/creat      */
	DIR *   dir;     /* handle returned by the kernel */
	off_t   dir_eof; /* offset at dir EOF */
        int     odirect;
        struct list_head list; /* to add to the janitor list */
};


struct posix_private {
	char   *base_path;
	int32_t base_path_length;
	int32_t path_max;

        gf_lock_t lock;

        char   *hostname;
        /* Statistics, provides activity of the server */

	struct timeval prev_fetch_time;
	struct timeval init_time;

        time_t last_landfill_check;
        int32_t janitor_sleep_duration;
        struct list_head janitor_fds;
        pthread_cond_t janitor_cond;
        pthread_mutex_t janitor_lock;

	int64_t read_value;    /* Total read, from init */
	int64_t write_value;   /* Total write, from init */
        int64_t nr_files;
/*
   In some cases, two exported volumes may reside on the same
   partition on the server. Sending statvfs info for both
   the volumes will lead to erroneous df output at the client,
   since free space on the partition will be counted twice.

   In such cases, user can disable exporting statvfs info
   on one of the volumes by setting this option.
*/
	gf_boolean_t    export_statfs;

	gf_boolean_t    o_direct;     /* always open files in O_DIRECT mode */


/*
   decide whether posix_unlink does open (file), unlink (file), close (fd)
   instead of just unlink (file). with the former approach there is no lockout
   of access to parent directory during removal of very large files for the
   entire duration of freeing of data blocks.
*/
        gf_boolean_t    background_unlink;

/* janitor thread which cleans up /.trash (created by replicate) */
        pthread_t       janitor;
        gf_boolean_t    janitor_present;
        char *          trash_path;
/* lock for brick dir */
        DIR     *mount_lock;

        struct stat     handledir;

/* uuid of glusterd that swapned the brick process */
        uuid_t glusterd_uuid;

	gf_boolean_t    aio_configured;
	gf_boolean_t    aio_init_done;
	gf_boolean_t    aio_capable;
#ifdef HAVE_LIBAIO
        io_context_t    ctxp;
        pthread_t       aiothread;
#endif

        /* node-uuid in pathinfo xattr */
        gf_boolean_t  node_uuid_pathinfo;

	pthread_t         fsyncer;
	struct list_head  fsyncs;
	pthread_mutex_t   fsync_mutex;
	pthread_cond_t    fsync_cond;
	int               fsync_queue_count;

	enum {
		BATCH_NONE = 0,
		BATCH_SYNCFS,
		BATCH_SYNCFS_SINGLE_FSYNC,
		BATCH_REVERSE_FSYNC,
		BATCH_SYNCFS_REVERSE_FSYNC
	}               batch_fsync_mode;

	uint32_t        batch_fsync_delay_usec;
        gf_boolean_t    update_pgfid_nlinks;
        gf_boolean_t    gfid2path;
        char            gfid2path_sep[8];

        /* seconds to sleep between health checks */
        uint32_t        health_check_interval;
        /* seconds to sleep to wait for aio write finish for health checks */
        uint32_t        health_check_timeout;
        pthread_t       health_check;
        gf_boolean_t    health_check_active;

        uint32_t        disk_reserve;
        uint32_t        disk_space_full;
        pthread_t       disk_space_check;
        gf_boolean_t    disk_space_check_active;

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

        /* This option is used for either to call a landfill_purge or not */
        gf_boolean_t disable_landfill_purge;

        /*Option to set mode bit permission that will always be set on
          file/directory. */
        mode_t          force_create_mode;
        mode_t          force_directory_mode;
        mode_t          create_mask;
        mode_t          create_directory_mask;
        uint32_t max_hardlinks;

        gf_boolean_t fips_mode_rchecksum;
        gf_boolean_t ctime;
};

typedef struct {
        call_frame_t *frame;
        xlator_t    *this;
        const char  *real_path;
        dict_t      *xattr;
        struct iatt *stbuf;
        loc_t       *loc;
        inode_t     *inode; /* for all do_xattrop() key handling */
        fd_t        *fd;
        int          fdnum;
        int          flags;
        int32_t     op_errno;
        char        *list;
        size_t       list_size;
} posix_xattr_filler_t;

typedef struct {
        uint64_t unlink_flag;
        pthread_mutex_t xattrop_lock;
        pthread_mutex_t write_atomic_lock;
        pthread_mutex_t pgfid_lock;
} posix_inode_ctx_t;

#define POSIX_BASE_PATH(this) (((struct posix_private *)this->private)->base_path)

#define POSIX_BASE_PATH_LEN(this) (((struct posix_private *)this->private)->base_path_length)

#define POSIX_PATH_MAX(this) (((struct posix_private *)this->private)->path_max)

#define POSIX_GET_FILE_UNLINK_PATH(base_path, gfid, unlink_path)                           \
        do {                                                                               \
                int  path_len = 0;                                                         \
                char gfid_str[64] = {0};                                                   \
                uuid_utoa_r (gfid, gfid_str);                                              \
                path_len = strlen (base_path) + 1 +                                        \
                          strlen (GF_UNLINK_PATH) + 1 +                                    \
                          strlen (gfid_str) + 1;                                           \
                unlink_path = alloca (path_len);                                           \
                if (!unlink_path) {                                                        \
                        gf_msg ("posix", GF_LOG_ERROR, ENOMEM,                             \
                                P_MSG_UNLINK_FAILED,                                       \
                                "Failed to get unlink_path");                              \
                        break;                                                             \
                }                                                                          \
                sprintf (unlink_path, "%s/%s/%s",                                          \
                         base_path, GF_UNLINK_PATH, gfid_str);                             \
         } while (0)


/* Helper functions */
int posix_inode_ctx_set_unlink_flag (inode_t *inode, xlator_t *this,
                                     uint64_t ctx);

int posix_inode_ctx_get_all (inode_t *inode, xlator_t *this,
                             posix_inode_ctx_t **ctx);

int __posix_inode_ctx_set_unlink_flag (inode_t *inode, xlator_t *this,
                                       uint64_t ctx);

int __posix_inode_ctx_get_all (inode_t *inode, xlator_t *this,
                               posix_inode_ctx_t **ctx);

int posix_gfid_set (xlator_t *this, const char *path, loc_t *loc,
                    dict_t *xattr_req);
int posix_fdstat (xlator_t *this, inode_t *inode, int fd, struct iatt *stbuf_p);
int posix_istat (xlator_t *this, inode_t *inode, uuid_t gfid,
                 const char *basename, struct iatt *iatt);
int posix_pstat (xlator_t *this, inode_t *inode, uuid_t gfid,
                 const char *real_path, struct iatt *iatt,
                 gf_boolean_t inode_locked);
dict_t *posix_xattr_fill (xlator_t *this, const char *path, loc_t *loc,
                          fd_t *fd, int fdnum, dict_t *xattr, struct iatt *buf);
int posix_handle_pair (xlator_t *this, const char *real_path, char *key,
                       data_t *value, int flags, struct iatt *stbuf);
int posix_fhandle_pair (call_frame_t *frame, xlator_t *this, int fd, char *key,
                        data_t *value, int flags, struct iatt *stbuf,
                        fd_t *_fd);
void posix_spawn_janitor_thread (xlator_t *this);
int posix_acl_xattr_set (xlator_t *this, const char *path, dict_t *xattr_req);
int posix_gfid_heal (xlator_t *this, const char *path, loc_t *loc, dict_t *xattr_req);
int posix_entry_create_xattr_set (xlator_t *this, const char *path,
                                  dict_t *dict);

int posix_fd_ctx_get (fd_t *fd, xlator_t *this, struct posix_fd **pfd,
                      int *op_errno);
void posix_fill_ino_from_gfid (xlator_t *this, struct iatt *buf);

gf_boolean_t posix_special_xattr (char **pattern, char *key);

void
__posix_fd_set_odirect (fd_t *fd, struct posix_fd *pfd, int opflags,
			off_t offset, size_t size);
void posix_spawn_health_check_thread (xlator_t *this);

void posix_spawn_disk_space_check_thread (xlator_t *this);

void *posix_fsyncer (void *);
int
posix_get_ancestry (xlator_t *this, inode_t *leaf_inode,
                    gf_dirent_t *head, char **path, int type, int32_t *op_errno,
                    dict_t *xdata);
int
posix_handle_mdata_xattr (call_frame_t *frame, const char *name, int *op_errno);
int
posix_handle_georep_xattrs (call_frame_t *, const char *, int *, gf_boolean_t);
int32_t
posix_resolve_dirgfid_to_path (const uuid_t dirgfid, const char *brick_path,
                               const char *bname, char **path);
void
posix_gfid_unset (xlator_t *this, dict_t *xdata);

int
posix_pacl_set (const char *path, const char *key, const char *acl_s);

int
posix_pacl_get (const char *path, const char *key, char **acl_s);

int32_t
posix_get_objectsignature (char *, dict_t *);

int32_t
posix_fdget_objectsignature (int, dict_t *);

gf_boolean_t
posix_is_bulk_removexattr (char *name, dict_t *dict);

int32_t
posix_set_iatt_in_dict (dict_t *, struct iatt *);

mode_t
posix_override_umask (mode_t , mode_t);

int32_t
posix_priv (xlator_t *this);

int32_t
posix_inode (xlator_t *this);

void
posix_fini (xlator_t *this);

int
posix_init (xlator_t *this);

int
posix_reconfigure (xlator_t *this, dict_t *options);

int32_t
posix_notify (xlator_t *this, int32_t event, void *data, ...);

/* posix-entry-ops.c FOP signatures */
int32_t
posix_lookup (call_frame_t *frame, xlator_t *this,
              loc_t *loc, dict_t *xdata);

int
posix_create (call_frame_t *frame, xlator_t *this,
              loc_t *loc, int32_t flags, mode_t mode,
              mode_t umask, fd_t *fd, dict_t *xdata);

int
posix_symlink (call_frame_t *frame, xlator_t *this,
               const char *linkname, loc_t *loc, mode_t umask, dict_t *xdata);

int
posix_rename (call_frame_t *frame, xlator_t *this,
              loc_t *oldloc, loc_t *newloc, dict_t *xdata);

int
posix_link (call_frame_t *frame, xlator_t *this,
            loc_t *oldloc, loc_t *newloc, dict_t *xdata);

int
posix_mknod (call_frame_t *frame, xlator_t *this,
             loc_t *loc, mode_t mode, dev_t dev, mode_t umask, dict_t *xdata);

int
posix_mkdir (call_frame_t *frame, xlator_t *this,
             loc_t *loc, mode_t mode, mode_t umask, dict_t *xdata);

int32_t
posix_unlink (call_frame_t *frame, xlator_t *this,
              loc_t *loc, int xflag, dict_t *xdata);

int
posix_rmdir (call_frame_t *frame, xlator_t *this,
             loc_t *loc, int flags, dict_t *xdata);

/* posix-inode-fs-ops.c FOP signatures */
int
posix_forget (xlator_t *this, inode_t *inode);

int32_t
posix_discover (call_frame_t *frame, xlator_t *this,
                loc_t *loc, dict_t *xdata);

int32_t
posix_stat (call_frame_t *frame, xlator_t *this, loc_t *loc, dict_t *xdata);

int
posix_setattr (call_frame_t *frame, xlator_t *this,
               loc_t *loc, struct iatt *stbuf, int32_t valid, dict_t *xdata);

int
posix_fsetattr (call_frame_t *frame, xlator_t *this,
                fd_t *fd, struct iatt *stbuf, int32_t valid, dict_t *xdata);

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
posix_ipc (call_frame_t *frame, xlator_t *this, int32_t op, dict_t *xdata);

int32_t
posix_seek (call_frame_t *frame, xlator_t *this, fd_t *fd, off_t offset,
            gf_seek_what_t what, dict_t *xdata);

int32_t
posix_opendir (call_frame_t *frame, xlator_t *this,
               loc_t *loc, fd_t *fd, dict_t *xdata);

int32_t
posix_releasedir (xlator_t *this,
                  fd_t *fd);

int32_t
posix_readlink (call_frame_t *frame, xlator_t *this,
                loc_t *loc, size_t size, dict_t *xdata);

int32_t
posix_truncate (call_frame_t *frame, xlator_t *this, loc_t *loc, off_t offset,
                dict_t *xdata);

int32_t
posix_open (call_frame_t *frame, xlator_t *this,
            loc_t *loc, int32_t flags, fd_t *fd, dict_t *xdata);

int
posix_readv (call_frame_t *frame, xlator_t *this, fd_t *fd, size_t size,
             off_t offset, uint32_t flags, dict_t *xdata);

int32_t
posix_writev (call_frame_t *frame, xlator_t *this, fd_t *fd,
              struct iovec *vector, int32_t count, off_t offset,
              uint32_t flags, struct iobref *iobref, dict_t *xdata);

int32_t
posix_statfs (call_frame_t *frame, xlator_t *this,
              loc_t *loc, dict_t *xdata);

int32_t
posix_flush (call_frame_t *frame, xlator_t *this,
             fd_t *fd, dict_t *xdata);

int32_t
posix_release (xlator_t *this, fd_t *fd);

int32_t
posix_fsync (call_frame_t *frame, xlator_t *this,
             fd_t *fd, int32_t datasync, dict_t *xdata);

int32_t
posix_setxattr (call_frame_t *frame, xlator_t *this,
                loc_t *loc, dict_t *dict, int flags, dict_t *xdata);

int
posix_get_ancestry_non_directory (xlator_t *this, inode_t *leaf_inode,
                                  gf_dirent_t *head, char **path, int type,
                                  int32_t *op_errno, dict_t *xdata);

int
posix_get_ancestry (xlator_t *this, inode_t *leaf_inode,
                    gf_dirent_t *head, char **path, int type, int32_t *op_errno,
                    dict_t *xdata);

int32_t
posix_getxattr (call_frame_t *frame, xlator_t *this,
                loc_t *loc, const char *name, dict_t *xdata);

int32_t
posix_fgetxattr (call_frame_t *frame, xlator_t *this,
                 fd_t *fd, const char *name, dict_t *xdata);

int32_t
posix_fsetxattr (call_frame_t *frame, xlator_t *this,
                 fd_t *fd, dict_t *dict, int flags, dict_t *xdata);

int32_t
posix_removexattr (call_frame_t *frame, xlator_t *this,
                   loc_t *loc, const char *name, dict_t *xdata);

int32_t
posix_fremovexattr (call_frame_t *frame, xlator_t *this,
                    fd_t *fd, const char *name, dict_t *xdata);

int32_t
posix_fsyncdir (call_frame_t *frame, xlator_t *this,
                fd_t *fd, int datasync, dict_t *xdata);

int
posix_xattrop (call_frame_t *frame, xlator_t *this, loc_t *loc,
               gf_xattrop_flags_t optype, dict_t *xattr, dict_t *xdata);

int
posix_fxattrop (call_frame_t *frame, xlator_t *this, fd_t *fd,
                gf_xattrop_flags_t optype, dict_t *xattr, dict_t *xdata);

int
posix_access (call_frame_t *frame, xlator_t *this,
              loc_t *loc, int32_t mask, dict_t *xdata);

int32_t
posix_ftruncate (call_frame_t *frame, xlator_t *this,
                 fd_t *fd, off_t offset, dict_t *xdata);

int32_t
posix_fstat (call_frame_t *frame, xlator_t *this,
             fd_t *fd, dict_t *xdata);

int32_t
posix_lease (call_frame_t *frame, xlator_t *this,
             loc_t *loc, struct gf_lease *lease, dict_t *xdata);

int32_t
posix_lk (call_frame_t *frame, xlator_t *this,
          fd_t *fd, int32_t cmd, struct gf_flock *lock, dict_t *xdata);

int32_t
posix_inodelk (call_frame_t *frame, xlator_t *this,
               const char *volume, loc_t *loc, int32_t cmd,
               struct gf_flock *lock, dict_t *xdata);

int32_t
posix_finodelk (call_frame_t *frame, xlator_t *this,
                const char *volume, fd_t *fd, int32_t cmd,
                struct gf_flock *lock, dict_t *xdata);

int32_t
posix_entrylk (call_frame_t *frame, xlator_t *this,
               const char *volume, loc_t *loc, const char *basename,
               entrylk_cmd cmd, entrylk_type type, dict_t *xdata);

int32_t
posix_fentrylk (call_frame_t *frame, xlator_t *this,
                const char *volume, fd_t *fd, const char *basename,
                entrylk_cmd cmd, entrylk_type type, dict_t *xdata);

int32_t
posix_readdir (call_frame_t *frame, xlator_t *this,
               fd_t *fd, size_t size, off_t off, dict_t *xdata);

int32_t
posix_readdirp (call_frame_t *frame, xlator_t *this,
                fd_t *fd, size_t size, off_t off, dict_t *dict);

int32_t
posix_rchecksum (call_frame_t *frame, xlator_t *this,
                 fd_t *fd, off_t offset, int32_t len, dict_t *xdata);

int32_t
posix_put (call_frame_t *frame, xlator_t *this, loc_t *loc,
           mode_t mode, mode_t umask, uint32_t flags,
           struct iovec *vector, int32_t count, off_t offset,
           struct iobref *iobref, dict_t *xattr, dict_t *xdata);

int32_t
posix_set_mode_in_dict (dict_t *in_dict, dict_t *out_dict,
                        struct iatt *in_stbuf);

gf_cs_obj_state
posix_cs_check_status (xlator_t *this, const char *realpath, int *fd,
                       struct iatt *buf);

int
posix_cs_set_state (xlator_t *this, dict_t **rsp, gf_cs_obj_state state,
                    char const *path, int *fd);

gf_cs_obj_state
posix_cs_heal_state (xlator_t *this, const char *path, int *fd,
                     struct iatt *stbuf);
int
posix_cs_maintenance (xlator_t *this, fd_t *fd, loc_t *loc, int *pfd,
                   struct iatt *buf, const char *realpath, dict_t *xattr_req,
                   dict_t **xattr_rsp, gf_boolean_t ignore_failure);

#endif /* _POSIX_H */
