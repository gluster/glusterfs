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
#include "inode.h"
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

        /* seconds to sleep between health checks */
        uint32_t        health_check_interval;
        pthread_t       health_check;
        gf_boolean_t    health_check_active;

#ifdef GF_DARWIN_HOST_OS
        enum {
                XATTR_NONE = 0,
                XATTR_STRIP,
                XATTR_APPEND,
                XATTR_BOTH,
        } xattr_user_namespace;
#endif

};

typedef struct {
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
} posix_xattr_filler_t;

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
int posix_inode_ctx_get (inode_t *inode, xlator_t *this, uint64_t *ctx);
int posix_inode_ctx_set (inode_t *inode, xlator_t *this, uint64_t ctx);

int posix_gfid_set (xlator_t *this, const char *path, loc_t *loc,
                    dict_t *xattr_req);
int posix_fdstat (xlator_t *this, int fd, struct iatt *stbuf_p);
int posix_istat (xlator_t *this, uuid_t gfid, const char *basename,
                 struct iatt *iatt);
int posix_pstat (xlator_t *this, uuid_t gfid, const char *real_path,
                 struct iatt *iatt);
dict_t *posix_xattr_fill (xlator_t *this, const char *path, loc_t *loc,
                          fd_t *fd, int fdnum, dict_t *xattr, struct iatt *buf);
int posix_handle_pair (xlator_t *this, const char *real_path, char *key,
                       data_t *value, int flags, struct iatt *stbuf);
int posix_fhandle_pair (xlator_t *this, int fd, char *key, data_t *value,
                        int flags, struct iatt *stbuf);
void posix_spawn_janitor_thread (xlator_t *this);
int posix_get_file_contents (xlator_t *this, uuid_t pargfid,
                             const char *name, char **contents);
int posix_set_file_contents (xlator_t *this, const char *path, char *key,
                             data_t *value, int flags);
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

void *posix_fsyncer (void *);
int
posix_get_ancestry (xlator_t *this, inode_t *leaf_inode,
                    gf_dirent_t *head, char **path, int type, int32_t *op_errno,
                    dict_t *xdata);

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

#endif /* _POSIX_H */
