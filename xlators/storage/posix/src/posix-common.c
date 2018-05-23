/*
   Copyright (c) 2006-2017 Red Hat, Inc. <http://www.redhat.com>
   This file is part of GlusterFS.

   This file is licensed to you under your choice of the GNU Lesser
   General Public License, version 3 or any later version (LGPLv3 or
   later), or the GNU General Public License, version 2 (GPLv2), in all
   cases as published by the Free Software Foundation.
*/
#define __XOPEN_SOURCE 500

/* for SEEK_HOLE and SEEK_DATA */
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include <openssl/md5.h>
#include <stdint.h>
#include <sys/time.h>
#include <sys/resource.h>
#include <errno.h>
#include <libgen.h>
#include <pthread.h>
#include <ftw.h>
#include <sys/stat.h>
#include <signal.h>
#include <sys/uio.h>
#include <unistd.h>
#include <ftw.h>

#ifndef GF_BSD_HOST_OS
#include <alloca.h>
#endif /* GF_BSD_HOST_OS */

#ifdef HAVE_LINKAT
#include <fcntl.h>
#endif /* HAVE_LINKAT */

#include "glusterfs.h"
#include "checksum.h"
#include "dict.h"
#include "logging.h"
#include "posix.h"
#include "posix-inode-handle.h"
#include "xlator.h"
#include "defaults.h"
#include "common-utils.h"
#include "compat-errno.h"
#include "compat.h"
#include "byte-order.h"
#include "syscall.h"
#include "statedump.h"
#include "locking.h"
#include "timer.h"
#include "glusterfs3-xdr.h"
#include "hashfn.h"
#include "posix-aio.h"
#include "glusterfs-acl.h"
#include "posix-messages.h"
#include "events.h"
#include "posix-gfid-path.h"
#include "compat-uuid.h"

extern char *marker_xattrs[];
#define ALIGN_SIZE 4096

#undef HAVE_SET_FSID
#ifdef HAVE_SET_FSID

#define DECLARE_OLD_FS_ID_VAR uid_t old_fsuid; gid_t old_fsgid;

#define SET_FS_ID(uid, gid) do {                \
                old_fsuid = setfsuid (uid);     \
                old_fsgid = setfsgid (gid);     \
        } while (0)

#define SET_TO_OLD_FS_ID() do {                 \
                setfsuid (old_fsuid);           \
                setfsgid (old_fsgid);           \
        } while (0)

#else

#define DECLARE_OLD_FS_ID_VAR
#define SET_FS_ID(uid, gid)
#define SET_TO_OLD_FS_ID()

#endif

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

int32_t
posix_priv (xlator_t *this)
{
        struct posix_private *priv = NULL;
        char  key_prefix[GF_DUMP_MAX_BUF_LEN];

        (void) snprintf(key_prefix, GF_DUMP_MAX_BUF_LEN, "%s.%s",
                        this->type, this->name);
        gf_proc_dump_add_section(key_prefix);

        if (!this)
                return 0;

        priv = this->private;

        if (!priv)
                return 0;

        gf_proc_dump_write("base_path", "%s", priv->base_path);
        gf_proc_dump_write("base_path_length", "%d", priv->base_path_length);
        gf_proc_dump_write("max_read", "%d", priv->read_value);
        gf_proc_dump_write("max_write", "%d", priv->write_value);
        gf_proc_dump_write("nr_files", "%ld", priv->nr_files);

        return 0;
}

int32_t
posix_inode (xlator_t *this)
{
        return 0;
}

/**
 * notify - when parent sends PARENT_UP, send CHILD_UP event from here
 */
int32_t
posix_notify (xlator_t *this,
        int32_t event,
        void *data,
        ...)
{
        struct posix_private *priv = NULL;

        priv = this->private;
        switch (event)
        {
        case GF_EVENT_PARENT_UP:
        {
                /* Tell the parent that posix xlator is up */
                default_notify (this, GF_EVENT_CHILD_UP, data);
        }
        break;
        case GF_EVENT_CLEANUP:
                if (priv->health_check) {
                        priv->health_check_active = _gf_false;
                        pthread_cancel (priv->health_check);
                        priv->health_check = 0;
                }
                if (priv->disk_space_check) {
                        priv->disk_space_check_active = _gf_false;
                        pthread_cancel (priv->disk_space_check);
                        priv->disk_space_check = 0;
                }
                if (priv->janitor) {
                        (void) gf_thread_cleanup_xint (priv->janitor);
                        priv->janitor = 0;
                }
                if (priv->fsyncer) {
                        (void) gf_thread_cleanup_xint (priv->fsyncer);
                        priv->fsyncer = 0;
                }
                if (priv->mount_lock) {
                        (void) sys_closedir (priv->mount_lock);
                        priv->mount_lock = NULL;
                }

        break;
        default:
                /* */
                break;
        }
        return 0;
}

int32_t
mem_acct_init (xlator_t *this)
{
        int     ret = -1;

        if (!this)
                return ret;

        ret = xlator_mem_acct_init (this, gf_posix_mt_end + 1);

        if (ret != 0) {
                return ret;
        }

        return ret;
}

static int
posix_set_owner (xlator_t *this, uid_t uid, gid_t gid)
{
        struct posix_private *priv = NULL;
        int                   ret  = -1;
        struct stat st = {0,};

        priv = this->private;

        ret = sys_lstat (priv->base_path, &st);
        if (ret) {
                gf_msg (this->name, GF_LOG_ERROR, errno,
                        P_MSG_DIR_OPERATION_FAILED, "Failed to stat "
                        "brick path %s",
                        priv->base_path);
                return ret;
        }

        if ((uid == -1 || st.st_uid == uid) &&
            (gid == -1 || st.st_gid == gid))
                return 0;

        ret = sys_chown (priv->base_path, uid, gid);
        if (ret)
                gf_msg (this->name, GF_LOG_ERROR, errno,
                        P_MSG_DIR_OPERATION_FAILED, "Failed to set uid/gid for"
                        " brick path %s", priv->base_path);

        return ret;
}
static int
set_gfid2path_separator (struct posix_private *priv, const char *str)
{
        int str_len = 0;

        str_len = strlen(str);
        if (str_len > 0 && str_len < 8) {
                strcpy (priv->gfid2path_sep, str);
                return 0;
        }

        return -1;
}

static int
set_batch_fsync_mode (struct posix_private *priv, const char *str)
{
        if (strcmp (str, "none") == 0)
                priv->batch_fsync_mode = BATCH_NONE;
        else if (strcmp (str, "syncfs") == 0)
                priv->batch_fsync_mode = BATCH_SYNCFS;
        else if (strcmp (str, "syncfs-single-fsync") == 0)
                priv->batch_fsync_mode = BATCH_SYNCFS_SINGLE_FSYNC;
        else if (strcmp (str, "syncfs-reverse-fsync") == 0)
                priv->batch_fsync_mode = BATCH_SYNCFS_REVERSE_FSYNC;
        else if (strcmp (str, "reverse-fsync") == 0)
                priv->batch_fsync_mode = BATCH_REVERSE_FSYNC;
        else
                return -1;

        return 0;
}

#ifdef GF_DARWIN_HOST_OS
static int
set_xattr_user_namespace_mode (struct posix_private *priv, const char *str)
{
        if (strcmp (str, "none") == 0)
                priv->xattr_user_namespace = XATTR_NONE;
        else if (strcmp (str, "strip") == 0)
                priv->xattr_user_namespace = XATTR_STRIP;
        else if (strcmp (str, "append") == 0)
                priv->xattr_user_namespace = XATTR_APPEND;
        else if (strcmp (str, "both") == 0)
                priv->xattr_user_namespace = XATTR_BOTH;
        else
                return -1;
        return 0;
}
#endif

int
posix_reconfigure (xlator_t *this, dict_t *options)
{
        int                   ret = -1;
        struct posix_private *priv = NULL;
        int32_t               uid = -1;
        int32_t               gid = -1;
        char                 *batch_fsync_mode_str = NULL;
        char                 *gfid2path_sep = NULL;
        int32_t              force_create_mode = -1;
        int32_t              force_directory_mode = -1;
        int32_t              create_mask = -1;
        int32_t              create_directory_mask = -1;

        priv = this->private;

        GF_OPTION_RECONF ("brick-uid", uid, options, int32, out);
        GF_OPTION_RECONF ("brick-gid", gid, options, int32, out);
        if (uid != -1 || gid != -1)
                posix_set_owner (this, uid, gid);

        GF_OPTION_RECONF ("batch-fsync-delay-usec", priv->batch_fsync_delay_usec,
                          options, uint32, out);

        GF_OPTION_RECONF ("batch-fsync-mode", batch_fsync_mode_str,
                          options, str, out);

        if (set_batch_fsync_mode (priv, batch_fsync_mode_str) != 0) {
                gf_msg (this->name, GF_LOG_ERROR, 0, P_MSG_INVALID_ARGUMENT,
                        "Unknown mode string: %s", batch_fsync_mode_str);
                goto out;
        }

        GF_OPTION_RECONF ("gfid2path-separator", gfid2path_sep, options,
                          str, out);
        if (set_gfid2path_separator (priv, gfid2path_sep) != 0) {
                gf_msg (this->name, GF_LOG_ERROR, 0, P_MSG_INVALID_ARGUMENT,
                        "Length of separator exceeds 7: %s", gfid2path_sep);
                goto out;
        }

#ifdef GF_DARWIN_HOST_OS

        char   *xattr_user_namespace_mode_str = NULL;

        GF_OPTION_RECONF ("xattr-user-namespace-mode", xattr_user_namespace_mode_str,
                          options, str, out);

        if (set_xattr_user_namespace_mode (priv, xattr_user_namespace_mode_str) != 0) {
                gf_msg (this->name, GF_LOG_ERROR, 0, P_MSG_UNKNOWN_ARGUMENT,
                        "Unknown xattr user namespace mode string: %s",
                        xattr_user_namespace_mode_str);
                goto out;
        }

#endif

        GF_OPTION_RECONF ("linux-aio", priv->aio_configured,
                          options, bool, out);

        if (priv->aio_configured)
                posix_aio_on (this);
        else
                posix_aio_off (this);

        GF_OPTION_RECONF ("update-link-count-parent", priv->update_pgfid_nlinks,
                          options, bool, out);

        GF_OPTION_RECONF ("gfid2path", priv->gfid2path,
                          options, bool, out);

        GF_OPTION_RECONF ("node-uuid-pathinfo", priv->node_uuid_pathinfo,
                          options, bool, out);

        if (priv->node_uuid_pathinfo &&
                        (gf_uuid_is_null (priv->glusterd_uuid))) {
                gf_msg (this->name, GF_LOG_INFO, 0, P_MSG_UUID_NULL,
                        "glusterd uuid is NULL, pathinfo xattr would"
                        " fallback to <hostname>:<export>");
        }

        GF_OPTION_RECONF ("reserve", priv->disk_reserve,
                          options, uint32, out);
        if (priv->disk_reserve)
                posix_spawn_disk_space_check_thread (this);

        GF_OPTION_RECONF ("health-check-interval", priv->health_check_interval,
                          options, uint32, out);
        GF_OPTION_RECONF ("health-check-timeout", priv->health_check_timeout,
                          options, uint32, out);
        posix_spawn_health_check_thread (this);

        GF_OPTION_RECONF ("shared-brick-count", priv->shared_brick_count,
                          options, int32, out);

        GF_OPTION_RECONF ("disable-landfill-purge",
                          priv->disable_landfill_purge,
                          options, bool, out);
        if (priv->disable_landfill_purge) {
                gf_log (this->name, GF_LOG_WARNING,
                        "Janitor WILL NOT purge the landfill directory. "
                        "Your landfill directory"
                        " may fill up this brick.");
        } else {
                gf_msg_debug (this->name, 0, "Janitor will purge the landfill "
                              "directory, which is default behavior");
        }

        GF_OPTION_RECONF ("force-create-mode", force_create_mode,
                          options, int32, out);
        priv->force_create_mode = force_create_mode;

        GF_OPTION_RECONF ("force-directory-mode", force_directory_mode,
                          options, int32, out);
        priv->force_directory_mode = force_directory_mode;

        GF_OPTION_RECONF ("create-mask", create_mask,
                          options, int32, out);
        priv->create_mask = create_mask;

        GF_OPTION_RECONF ("create-directory-mask", create_directory_mask,
                          options, int32, out);
        priv->create_directory_mask = create_directory_mask;

        GF_OPTION_RECONF ("max-hardlinks", priv->max_hardlinks,
                          options, uint32, out);

        GF_OPTION_RECONF ("fips-mode-rchecksum", priv->fips_mode_rchecksum,
                          options, bool, out);

        GF_OPTION_RECONF ("ctime", priv->ctime, options, bool, out);

        ret = 0;
out:
        return ret;
}

int32_t
posix_delete_unlink_entry (const char *fpath, const struct stat *sb,
                   int typeflag, struct FTW *ftwbuf) {

        int    ret = 0;

        if (!fpath)
                goto out;

        switch (typeflag) {
        case FTW_SL:
        case FTW_NS:
        case FTW_F:
        case FTW_SLN:
                ret = sys_unlink(fpath);
                break;
        case FTW_D:
        case FTW_DP:
        case FTW_DNR:
                if (ftwbuf->level != 0) {
                        ret = sys_rmdir(fpath);
                }
                break;
        default:
                break;
        }
        if (ret) {
                gf_msg ("posix_delete_unlink_entry", GF_LOG_WARNING, errno,
                        P_MSG_HANDLE_CREATE,
                        "Deletion of entries %s failed"
                        "Please delete it manually",
                        fpath);
        }
out:
        return 0;
}

int32_t
posix_delete_unlink (const char *unlink_path) {

        int    ret = -1;
        int    flags = 0;

        flags |= (FTW_DEPTH | FTW_PHYS);

        ret = nftw(unlink_path, posix_delete_unlink_entry, 2, flags);
        if (ret) {
                gf_msg ("posix_delete_unlink", GF_LOG_ERROR, 0,
                        P_MSG_HANDLE_CREATE,
                        "Deleting files from  %s failed",
                        unlink_path);
        }
        return ret;
}

int32_t
posix_create_unlink_dir (xlator_t *this) {

        struct posix_private *priv = NULL;
        struct stat           stbuf;
        int                   ret = -1;
        uuid_t                gfid = {0};
        char                  gfid_str[64] = {0};
        char                  unlink_path[PATH_MAX] = {0,};
        char                  landfill_path[PATH_MAX] = {0,};

        priv = this->private;

        (void) snprintf (unlink_path, sizeof(unlink_path), "%s/%s",
                         priv->base_path, GF_UNLINK_PATH);

        gf_uuid_generate (gfid);
        uuid_utoa_r (gfid, gfid_str);

        (void) snprintf (landfill_path, sizeof(landfill_path), "%s/%s/%s",
                         priv->base_path, GF_LANDFILL_PATH, gfid_str);

        ret = sys_stat (unlink_path, &stbuf);
        switch (ret) {
        case -1:
                if (errno != ENOENT) {
                        gf_msg (this->name, GF_LOG_ERROR, errno,
                                P_MSG_HANDLE_CREATE,
                                "Checking for %s failed",
                                unlink_path);
                        return -1;
                }
                break;
        case 0:
                if (!S_ISDIR (stbuf.st_mode)) {
                        gf_msg (this->name, GF_LOG_ERROR, 0,
                                P_MSG_HANDLE_CREATE,
                                "Not a directory: %s",
                                unlink_path);
                        return -1;
                }
                ret = posix_delete_unlink (unlink_path);
                return 0;
        default:
                break;
        }
        ret = sys_mkdir (unlink_path, 0600);
        if (ret) {
                gf_msg (this->name, GF_LOG_ERROR, errno,
                        P_MSG_HANDLE_CREATE,
                        "Creating directory %s failed",
                        unlink_path);
                return -1;
        }

        return 0;
}

/**
 * init -
 */
int
posix_init (xlator_t *this)
{
        struct posix_private *_private      = NULL;
        data_t               *dir_data      = NULL;
        data_t               *tmp_data      = NULL;
        struct stat           buf           = {0,};
        gf_boolean_t          tmp_bool      = 0;
        int                   ret           = 0;
        int                   op_ret        = -1;
        int                   op_errno      = 0;
        ssize_t               size          = -1;
        uuid_t                old_uuid      = {0,};
        uuid_t                dict_uuid     = {0,};
        uuid_t                gfid          = {0,};
        uuid_t                rootgfid      = {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1};
        char                 *guuid         = NULL;
        int32_t               uid           = -1;
        int32_t               gid           = -1;
        char                 *batch_fsync_mode_str;
        char                 *gfid2path_sep = NULL;
        int                  force_create  = -1;
        int                  force_directory = -1;
        int                  create_mask  = -1;
        int                  create_directory_mask = -1;

        dir_data = dict_get (this->options, "directory");

        if (this->children) {
                gf_msg (this->name, GF_LOG_CRITICAL, 0, P_MSG_SUBVOLUME_ERROR,
                        "FATAL: storage/posix cannot have subvolumes");
                ret = -1;
                goto out;
        }

        if (!this->parents) {
                gf_msg (this->name, GF_LOG_WARNING, 0, P_MSG_VOLUME_DANGLING,
                        "Volume is dangling. Please check the volume file.");
        }

        if (!dir_data) {
                gf_msg (this->name, GF_LOG_CRITICAL, 0,
                        P_MSG_EXPORT_DIR_MISSING,
                        "Export directory not specified in volume file.");
                ret = -1;
                goto out;
        }

        umask (000); // umask `masking' is done at the client side

        /* Check whether the specified directory exists, if not log it. */
        op_ret = sys_stat (dir_data->data, &buf);
        if ((op_ret != 0) || !S_ISDIR (buf.st_mode)) {
                gf_msg (this->name, GF_LOG_ERROR, 0, P_MSG_DIR_OPERATION_FAILED,
                        "Directory '%s' doesn't exist, exiting.",
                        dir_data->data);
                ret = -1;
                goto out;
        }

        _private = GF_CALLOC (1, sizeof (*_private),
                              gf_posix_mt_posix_private);
        if (!_private) {
                ret = -1;
                goto out;
        }

        _private->base_path = gf_strdup (dir_data->data);
        _private->base_path_length = strlen (_private->base_path);

        ret = dict_get_str (this->options, "hostname", &_private->hostname);
        if (ret) {
                _private->hostname = GF_CALLOC (256, sizeof (char),
                                                gf_common_mt_char);
                if (!_private->hostname) {
                        goto out;
                }
                ret = gethostname (_private->hostname, 256);
                if (ret < 0) {
                        gf_msg (this->name, GF_LOG_WARNING, errno,
                                P_MSG_HOSTNAME_MISSING,
                                "could not find hostname ");
                }
        }

        /* Check for Extended attribute support, if not present, log it */
        op_ret = sys_lsetxattr (dir_data->data,
                                "trusted.glusterfs.test", "working", 8, 0);
        if (op_ret != -1) {
                sys_lremovexattr (dir_data->data, "trusted.glusterfs.test");
        } else {
                tmp_data = dict_get (this->options,
                                     "mandate-attribute");
                if (tmp_data) {
                        if (gf_string2boolean (tmp_data->data,
                                               &tmp_bool) == -1) {
                                gf_msg (this->name, GF_LOG_ERROR, 0,
                                        P_MSG_INVALID_OPTION,
                                        "wrong option provided for key "
                                        "\"mandate-attribute\"");
                                ret = -1;
                                goto out;
                        }
                        if (!tmp_bool) {
                                gf_msg (this->name, GF_LOG_WARNING, 0,
                                        P_MSG_XATTR_NOTSUP,
                                        "Extended attribute not supported, "
                                        "starting as per option");
                        } else {
                                gf_msg (this->name, GF_LOG_CRITICAL, 0,
                                        P_MSG_XATTR_NOTSUP,
                                        "Extended attribute not supported, "
                                        "exiting.");
                                ret = -1;
                                goto out;
                        }
                } else {
                        gf_msg (this->name, GF_LOG_CRITICAL, 0,
                                P_MSG_XATTR_NOTSUP,
                                "Extended attribute not supported, exiting.");
                        ret = -1;
                        goto out;
                }
        }

        tmp_data = dict_get (this->options, "volume-id");
        if (tmp_data) {
                op_ret = gf_uuid_parse (tmp_data->data, dict_uuid);
                if (op_ret < 0) {
                        gf_msg (this->name, GF_LOG_ERROR, 0,
                                P_MSG_INVALID_VOLUME_ID,
                                "wrong volume-id (%s) set"
                                " in volume file", tmp_data->data);
                        ret = -1;
                        goto out;
                }
                size = sys_lgetxattr (dir_data->data,
                                      "trusted.glusterfs.volume-id", old_uuid, 16);
                if (size == 16) {
                        if (gf_uuid_compare (old_uuid, dict_uuid)) {
                                gf_msg (this->name, GF_LOG_ERROR, 0,
                                        P_MSG_INVALID_VOLUME_ID,
                                        "mismatching volume-id (%s) received. "
                                        "already is a part of volume %s ",
                                        tmp_data->data, uuid_utoa (old_uuid));
                                gf_event (EVENT_POSIX_ALREADY_PART_OF_VOLUME,
                                        "volume-id=%s;brick=%s:%s",
                                        uuid_utoa (old_uuid),
                                       _private->hostname, _private->base_path);
                                ret = -1;
                                goto out;
                        }
                } else if ((size == -1) &&
                           (errno == ENODATA || errno == ENOATTR)) {
                                gf_msg (this->name, GF_LOG_ERROR, errno,
                                        P_MSG_VOLUME_ID_ABSENT,
                                        "Extended attribute trusted.glusterfs."
                                        "volume-id is absent");
                                gf_event (EVENT_POSIX_BRICK_NOT_IN_VOLUME,
                                        "brick=%s:%s",
                                       _private->hostname, _private->base_path);
                                ret = -1;
                                goto out;

                }  else if ((size == -1) && (errno != ENODATA) &&
                            (errno != ENOATTR)) {
                        /* Wrong 'volume-id' is set, it should be error */
                        gf_event (EVENT_POSIX_BRICK_VERIFICATION_FAILED,
                                "brick=%s:%s",
                                _private->hostname, _private->base_path);
                        gf_msg (this->name, GF_LOG_WARNING, errno,
                                P_MSG_VOLUME_ID_FETCH_FAILED,
                                "%s: failed to fetch volume-id",
                                dir_data->data);
                        ret = -1;
                        goto out;
                } else {
                        ret = -1;
                        gf_event (EVENT_POSIX_BRICK_VERIFICATION_FAILED,
                                "brick=%s:%s",
                                _private->hostname, _private->base_path);
                        gf_msg (this->name, GF_LOG_ERROR, 0,
                                P_MSG_VOLUME_ID_FETCH_FAILED,
                                "failed to fetch proper volume id from export");
                        goto out;
                }
        }

        /* Now check if the export directory has some other 'gfid',
           other than that of root '/' */
        size = sys_lgetxattr (dir_data->data, "trusted.gfid", gfid, 16);
        if (size == 16) {
                if (!__is_root_gfid (gfid)) {
                        gf_msg (this->name, GF_LOG_WARNING, errno,
                                P_MSG_GFID_SET_FAILED,
                                "%s: gfid (%s) is not that of glusterfs '/' ",
                                dir_data->data, uuid_utoa (gfid));
                        ret = -1;
                        goto out;
                }
        } else if (size != -1) {
                /* Wrong 'gfid' is set, it should be error */
                gf_msg (this->name, GF_LOG_WARNING, errno,
                        P_MSG_GFID_SET_FAILED,
                        "%s: wrong value set as gfid",
                        dir_data->data);
                ret = -1;
                goto out;
        } else if ((size == -1) && (errno != ENODATA) &&
                   (errno != ENOATTR)) {
                /* Wrong 'gfid' is set, it should be error */
                gf_msg (this->name, GF_LOG_WARNING, errno,
                        P_MSG_GFID_SET_FAILED,
                        "%s: failed to fetch gfid",
                        dir_data->data);
                ret = -1;
                goto out;
        } else {
                /* First time volume, set the GFID */
                size = sys_lsetxattr (dir_data->data, "trusted.gfid", rootgfid,
                                     16, XATTR_CREATE);
                if (size == -1) {
                        gf_msg (this->name, GF_LOG_ERROR, errno,
                                P_MSG_GFID_SET_FAILED,
                                "%s: failed to set gfid",
                                dir_data->data);
                        ret = -1;
                        goto out;
                }
        }

        ret = 0;

        size = sys_lgetxattr (dir_data->data, POSIX_ACL_ACCESS_XATTR,
                              NULL, 0);
        if ((size < 0) && (errno == ENOTSUP)) {
                gf_msg (this->name, GF_LOG_WARNING, errno,
                        P_MSG_ACL_NOTSUP,
                        "Posix access control list is not supported.");
                gf_event (EVENT_POSIX_ACL_NOT_SUPPORTED,
                        "brick=%s:%s", _private->hostname, _private->base_path);
        }

        /*
         * _XOPEN_PATH_MAX is the longest file path len we MUST
         * support according to POSIX standard. When prepended
         * by the brick base path it may exceed backed filesystem
         * capacity (which MAY be bigger than _XOPEN_PATH_MAX). If
         * this is the case, chdir() to the brick base path and
         * use relative paths when they are too long. See also
         * MAKE_REAL_PATH in posix-handle.h
          */
        _private->path_max = pathconf(_private->base_path, _PC_PATH_MAX);
        if (_private->path_max != -1 &&
            _XOPEN_PATH_MAX + _private->base_path_length > _private->path_max) {
                ret = chdir(_private->base_path);
                if (ret) {
                        gf_msg (this->name, GF_LOG_ERROR, 0,
                                P_MSG_BASEPATH_CHDIR_FAILED,
                                "chdir() to \"%s\" failed",
                                _private->base_path);
                        goto out;
                }
#ifdef __NetBSD__
                /*
                 * At least on NetBSD, the chdir() above uncovers a
                 * race condition which cause file lookup to fail
                 * with ENODATA for a few seconds. The volume quickly
                 * reaches a sane state, but regression tests are fast
                 * enough to choke on it. The reason is obscure (as
                 * often with race conditions), but sleeping here for
                 * a second seems to workaround the problem.
                 */
                sleep(1);
#endif
        }


        LOCK_INIT (&_private->lock);

        _private->export_statfs = 1;
        tmp_data = dict_get (this->options, "export-statfs-size");
        if (tmp_data) {
                if (gf_string2boolean (tmp_data->data,
                                       &_private->export_statfs) == -1) {
                        ret = -1;
                        gf_msg (this->name, GF_LOG_ERROR, 0,
                                P_MSG_INVALID_OPTION_VAL,
                                "'export-statfs-size' takes only boolean "
                                "options");
                        goto out;
                }
                if (!_private->export_statfs)
                        gf_msg_debug (this->name, 0,
                                "'statfs()' returns dummy size");
        }

        _private->background_unlink = 0;
        tmp_data = dict_get (this->options, "background-unlink");
        if (tmp_data) {
                if (gf_string2boolean (tmp_data->data,
                                       &_private->background_unlink) == -1) {
                        ret = -1;
                        gf_msg (this->name, GF_LOG_ERROR, 0,
                                P_MSG_INVALID_OPTION_VAL, "'background-unlink'"
                                " takes only boolean options");
                        goto out;
                }

                if (_private->background_unlink)
                        gf_msg_debug (this->name, 0,
                                "unlinks will be performed in background");
        }

        tmp_data = dict_get (this->options, "o-direct");
        if (tmp_data) {
                if (gf_string2boolean (tmp_data->data,
                                       &_private->o_direct) == -1) {
                        ret = -1;
                        gf_msg (this->name, GF_LOG_ERROR, 0,
                                P_MSG_INVALID_OPTION_VAL,
                                "wrong option provided for 'o-direct'");
                        goto out;
                }
                if (_private->o_direct)
                        gf_msg_debug (this->name, 0, "o-direct mode is enabled"
                                      " (O_DIRECT for every open)");
        }

        tmp_data = dict_get (this->options, "update-link-count-parent");
        if (tmp_data) {
                if (gf_string2boolean (tmp_data->data,
                                       &_private->update_pgfid_nlinks) == -1) {
                        ret = -1;
                        gf_msg (this->name, GF_LOG_ERROR, 0,
                                P_MSG_INVALID_OPTION, "wrong value provided "
                                "for 'update-link-count-parent'");
                        goto out;
                }
                if (_private->update_pgfid_nlinks)
                        gf_msg_debug (this->name, 0, "update-link-count-parent"
                                      " is enabled. Thus for each file an "
                                      "extended attribute representing the "
                                      "number of hardlinks for that file "
                                      "within the same parent directory is"
                                      " set.");
        }

        ret = dict_get_str (this->options, "glusterd-uuid", &guuid);
        if (!ret) {
                if (gf_uuid_parse (guuid, _private->glusterd_uuid))
                        gf_msg (this->name, GF_LOG_WARNING, 0,
                                P_MSG_INVALID_NODE_UUID, "Cannot parse "
                                "glusterd (node) UUID, node-uuid xattr "
                                "request would return - \"No such attribute\"");
        } else {
                gf_msg_debug (this->name, 0, "No glusterd (node) UUID passed -"
                              " node-uuid xattr request will return \"No such"
                              " attribute\"");
        }
        ret = 0;

        GF_OPTION_INIT ("janitor-sleep-duration",
                        _private->janitor_sleep_duration, int32, out);

        /* performing open dir on brick dir locks the brick dir
         * and prevents it from being unmounted
         */
        _private->mount_lock = sys_opendir (dir_data->data);
        if (!_private->mount_lock) {
                ret = -1;
                op_errno = errno;
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        P_MSG_DIR_OPERATION_FAILED,
                        "Could not lock brick directory (%s)",
                        strerror (op_errno));
                goto out;
        }
#ifndef GF_DARWIN_HOST_OS
        {
                struct rlimit lim;
                lim.rlim_cur = 1048576;
                lim.rlim_max = 1048576;

                if (setrlimit (RLIMIT_NOFILE, &lim) == -1) {
                        gf_msg (this->name, GF_LOG_WARNING, errno,
                                P_MSG_SET_ULIMIT_FAILED,
                                "Failed to set 'ulimit -n "
                                " 1048576'");
                        lim.rlim_cur = 65536;
                        lim.rlim_max = 65536;

                        if (setrlimit (RLIMIT_NOFILE, &lim) == -1) {
                                gf_msg (this->name, GF_LOG_WARNING, errno,
                                        P_MSG_SET_FILE_MAX_FAILED,
                                        "Failed to set maximum allowed open "
                                        "file descriptors to 64k");
                        }
                        else {
                                gf_msg (this->name, GF_LOG_INFO, 0,
                                        P_MSG_MAX_FILE_OPEN, "Maximum allowed "
                                        "open file descriptors set to 65536");
                        }
                }
        }
#endif
        _private->shared_brick_count = 1;
        ret = dict_get_int32 (this->options, "shared-brick-count",
                              &_private->shared_brick_count);
        if (ret == -1) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        P_MSG_INVALID_OPTION_VAL,
                        "'shared-brick-count' takes only integer "
                        "values");
                goto out;
        }

        this->private = (void *)_private;

        op_ret = posix_handle_init (this);
        if (op_ret == -1) {
                gf_msg (this->name, GF_LOG_ERROR, 0, P_MSG_HANDLE_CREATE,
                        "Posix handle setup failed");
                ret = -1;
                goto out;
        }

        op_ret = posix_handle_trash_init (this);
        if (op_ret < 0) {
                gf_msg (this->name, GF_LOG_ERROR, 0, P_MSG_HANDLE_CREATE_TRASH,
                        "Posix landfill setup failed");
                ret = -1;
                goto out;
        }

        op_ret = posix_create_unlink_dir (this);
        if (op_ret == -1) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        P_MSG_HANDLE_CREATE,
                        "Creation of unlink directory failed");
                ret = -1;
                goto out;
        }

        _private->aio_init_done = _gf_false;
        _private->aio_capable = _gf_false;

        GF_OPTION_INIT ("brick-uid", uid, int32, out);
        GF_OPTION_INIT ("brick-gid", gid, int32, out);
        if (uid != -1 || gid != -1)
                posix_set_owner (this, uid, gid);

        GF_OPTION_INIT ("linux-aio", _private->aio_configured, bool, out);

        if (_private->aio_configured) {
                op_ret = posix_aio_on (this);

                if (op_ret == -1) {
                        gf_msg (this->name, GF_LOG_ERROR, 0, P_MSG_POSIX_AIO,
                                "Posix AIO init failed");
                        ret = -1;
                        goto out;
                }
        }

        GF_OPTION_INIT ("node-uuid-pathinfo",
                        _private->node_uuid_pathinfo, bool, out);
        if (_private->node_uuid_pathinfo &&
            (gf_uuid_is_null (_private->glusterd_uuid))) {
                        gf_msg (this->name, GF_LOG_INFO, 0, P_MSG_UUID_NULL,
                                "glusterd uuid is NULL, pathinfo xattr would"
                                " fallback to <hostname>:<export>");
        }

        _private->disk_space_check_active = _gf_false;
        _private->disk_space_full          = 0;
        GF_OPTION_INIT ("reserve",
                        _private->disk_reserve, uint32, out);
        if (_private->disk_reserve)
                posix_spawn_disk_space_check_thread (this);

        _private->health_check_active = _gf_false;
        GF_OPTION_INIT ("health-check-interval",
                        _private->health_check_interval, uint32, out);
        GF_OPTION_INIT ("health-check-timeout",
                        _private->health_check_timeout, uint32, out);
        if (_private->health_check_interval)
                posix_spawn_health_check_thread (this);

        pthread_mutex_init (&_private->janitor_lock, NULL);
        pthread_cond_init (&_private->janitor_cond, NULL);
        INIT_LIST_HEAD (&_private->janitor_fds);

        posix_spawn_janitor_thread (this);

        pthread_mutex_init (&_private->fsync_mutex, NULL);
        pthread_cond_init (&_private->fsync_cond, NULL);
        INIT_LIST_HEAD (&_private->fsyncs);

        ret = gf_thread_create (&_private->fsyncer, NULL, posix_fsyncer, this,
                                "posixfsy");
        if (ret) {
                gf_msg (this->name, GF_LOG_ERROR, errno,
                        P_MSG_FSYNCER_THREAD_CREATE_FAILED,
                        "fsyncer thread creation failed");
                goto out;
        }

        GF_OPTION_INIT ("batch-fsync-mode", batch_fsync_mode_str, str, out);

        if (set_batch_fsync_mode (_private, batch_fsync_mode_str) != 0) {
                gf_msg (this->name, GF_LOG_ERROR, 0, P_MSG_INVALID_ARGUMENT,
                        "Unknown mode string: %s", batch_fsync_mode_str);
                goto out;
        }

        GF_OPTION_INIT ("gfid2path", _private->gfid2path, bool, out);

        GF_OPTION_INIT ("gfid2path-separator", gfid2path_sep, str, out);
        if (set_gfid2path_separator (_private, gfid2path_sep) != 0) {
                gf_msg (this->name, GF_LOG_ERROR, 0, P_MSG_INVALID_ARGUMENT,
                        "Length of separator exceeds 7: %s", gfid2path_sep);
                goto out;
        }

#ifdef GF_DARWIN_HOST_OS

        char  *xattr_user_namespace_mode_str = NULL;

        GF_OPTION_INIT ("xattr-user-namespace-mode",
                        xattr_user_namespace_mode_str, str, out);

        if (set_xattr_user_namespace_mode (_private,
                                           xattr_user_namespace_mode_str) != 0) {
                gf_msg (this->name, GF_LOG_ERROR, 0, P_MSG_INVALID_ARGUMENT,
                        "Unknown xattr user namespace mode string: %s",
                        xattr_user_namespace_mode_str);
                goto out;
        }
#endif

        GF_OPTION_INIT ("batch-fsync-delay-usec", _private->batch_fsync_delay_usec,
                        uint32, out);

        GF_OPTION_INIT ("disable-landfill-purge",
                        _private->disable_landfill_purge, bool, out);
        if (_private->disable_landfill_purge) {
                gf_msg (this->name, GF_LOG_WARNING, 0, 0,
                        "Janitor WILL NOT purge the landfill directory. "
                        "Your landfill directory"
                        " may fill up this brick.");
        }

        GF_OPTION_INIT ("force-create-mode", force_create, int32, out);
        _private->force_create_mode = force_create;

        GF_OPTION_INIT ("force-directory-mode", force_directory, int32, out);
        _private->force_directory_mode = force_directory;

        GF_OPTION_INIT ("create-mask",
                        create_mask, int32, out);
        _private->create_mask = create_mask;

        GF_OPTION_INIT ("create-directory-mask",
                        create_directory_mask, int32, out);
        _private->create_directory_mask = create_directory_mask;

        GF_OPTION_INIT ("max-hardlinks", _private->max_hardlinks, uint32, out);

        GF_OPTION_INIT ("fips-mode-rchecksum", _private->fips_mode_rchecksum,
                        bool, out);

        GF_OPTION_INIT ("ctime", _private->ctime, bool, out);
out:
        if (ret) {
                if (_private) {
                        GF_FREE (_private->base_path);

                        GF_FREE (_private->hostname);

                        GF_FREE (_private->trash_path);

                        GF_FREE (_private);
                }

                this->private = NULL;
        }
        return ret;
}

void
posix_fini (xlator_t *this)
{
        struct posix_private *priv = this->private;
        if (!priv)
                return;
        this->private = NULL;
        /*unlock brick dir*/
        if (priv->mount_lock)
                (void) sys_closedir (priv->mount_lock);

        GF_FREE (priv->base_path);
        GF_FREE (priv->hostname);
        GF_FREE (priv->trash_path);
        GF_FREE (priv);

        return;
}

struct volume_options options[] = {
        { .key  = {"o-direct"},
          .type = GF_OPTION_TYPE_BOOL },
        { .key  = {"directory"},
          .type = GF_OPTION_TYPE_PATH,
          .default_value = "{{brick.path}}"
        },
        { .key  = {"hostname"},
          .type = GF_OPTION_TYPE_ANY },
        { .key  = {"export-statfs-size"},
          .type = GF_OPTION_TYPE_BOOL },
        { .key  = {"mandate-attribute"},
          .type = GF_OPTION_TYPE_BOOL },
        { .key  = {"background-unlink"},
          .type = GF_OPTION_TYPE_BOOL },
        { .key  = {"janitor-sleep-duration"},
          .type = GF_OPTION_TYPE_INT,
          .min = 1,
          .validate = GF_OPT_VALIDATE_MIN,
          .default_value = "10",
          .description = "Interval (in seconds) between times the internal "
                         "'landfill' directory is emptied."
        },
        { .key  = {"volume-id"},
          .type = GF_OPTION_TYPE_ANY,
          .default_value = "{{brick.volumeid}}"
        },
        { .key  = {"glusterd-uuid"},
          .type = GF_OPTION_TYPE_STR },
        {
          .key  = {"linux-aio"},
          .type = GF_OPTION_TYPE_BOOL,
          .default_value = "off",
          .description = "Support for native Linux AIO",
          .op_version = {1},
          .flags = OPT_FLAG_SETTABLE | OPT_FLAG_DOC
        },
        {
          .key = {"brick-uid"},
          .type = GF_OPTION_TYPE_INT,
          .min = -1,
          .validate = GF_OPT_VALIDATE_MIN,
          .default_value = "-1",
          .description = "Support for setting uid of brick's owner",
          .op_version = {1},
          .flags = OPT_FLAG_SETTABLE | OPT_FLAG_DOC
        },
        {
          .key = {"brick-gid"},
          .type = GF_OPTION_TYPE_INT,
          .min = -1,
          .validate = GF_OPT_VALIDATE_MIN,
          .default_value = "-1",
          .description = "Support for setting gid of brick's owner",
          .op_version = {1},
          .flags = OPT_FLAG_SETTABLE | OPT_FLAG_DOC
        },
        { .key = {"node-uuid-pathinfo"},
          .type = GF_OPTION_TYPE_BOOL,
          .default_value = "off",
          .description = "return glusterd's node-uuid in pathinfo xattr"
                         " string instead of hostname",
          .op_version = {3},
          .flags = OPT_FLAG_SETTABLE | OPT_FLAG_DOC
        },
        {
          .key = {"health-check-interval"},
          .type = GF_OPTION_TYPE_INT,
          .min = 0,
          .default_value = "30",
          .validate = GF_OPT_VALIDATE_MIN,
          .description = "Interval in seconds for a filesystem health check, "
                         "set to 0 to disable",
          .op_version = {3},
          .flags = OPT_FLAG_SETTABLE | OPT_FLAG_DOC
        },
        {
          .key = {"health-check-timeout"},
          .type = GF_OPTION_TYPE_INT,
          .min = 0,
          .default_value = "10",
          .validate = GF_OPT_VALIDATE_MIN,
          .description = "Interval in seconds to wait aio_write finish for health check, "
                         "set to 0 to disable",
          .op_version = {GD_OP_VERSION_4_0_0},
          .flags = OPT_FLAG_SETTABLE | OPT_FLAG_DOC
        },
        {
          .key = {"reserve"},
          .type = GF_OPTION_TYPE_INT,
          .min = 0,
          .default_value = "1",
          .validate = GF_OPT_VALIDATE_MIN,
          .description = "Percentage of disk space to be reserved."
           " Set to 0 to disable",
          .op_version = {GD_OP_VERSION_3_13_0},
          .flags = OPT_FLAG_SETTABLE | OPT_FLAG_DOC
        },
        { .key = {"batch-fsync-mode"},
          .type = GF_OPTION_TYPE_STR,
          .default_value = "reverse-fsync",
          .description = "Possible values:\n"
          "\t- syncfs: Perform one syncfs() on behalf oa batch"
          "of fsyncs.\n"
          "\t- syncfs-single-fsync: Perform one syncfs() on behalf of a batch"
          " of fsyncs and one fsync() per batch.\n"
          "\t- syncfs-reverse-fsync: Preform one syncfs() on behalf of a batch"
          " of fsyncs and fsync() each file in the batch in reverse order.\n"
          " in reverse order.\n"
          "\t- reverse-fsync: Perform fsync() of each file in the batch in"
          " reverse order.",
          .op_version = {3},
          .flags = OPT_FLAG_SETTABLE | OPT_FLAG_DOC
        },
        { .key = {"batch-fsync-delay-usec"},
          .type = GF_OPTION_TYPE_INT,
          .default_value = "0",
          .description = "Num of usecs to wait for aggregating fsync"
          " requests",
          .op_version = {3},
          .flags = OPT_FLAG_SETTABLE | OPT_FLAG_DOC
        },
        { .key = {"update-link-count-parent"},
          .type = GF_OPTION_TYPE_BOOL,
          .default_value = "off",
          .description = "Enable placeholders for gfid to path conversion",
          .op_version = {GD_OP_VERSION_3_6_0},
          .flags = OPT_FLAG_SETTABLE | OPT_FLAG_DOC
        },
        { .key = {"gfid2path"},
          .type = GF_OPTION_TYPE_BOOL,
#ifdef __NetBSD__
          /*
           * NetBSD storage of extended attributes for UFS1 badly
           * scales when the list of extended attributes names rises.
           * This option can add as many extended attributes names
           * as we have files, hence we keep it disabled for performance
           * sake.
           */
          .default_value = "off",
#else
          .default_value = "on",
#endif
          .description = "Enable logging metadata for gfid to path conversion",
          .op_version = {GD_OP_VERSION_3_12_0},
          .flags = OPT_FLAG_SETTABLE
        },
        { .key = {"gfid2path-separator"},
          .type = GF_OPTION_TYPE_STR,
          .default_value = ":",
          .description = "Path separator for glusterfs.gfidtopath virt xattr",
          .op_version = {GD_OP_VERSION_3_12_0},
          .flags = OPT_FLAG_SETTABLE | OPT_FLAG_DOC
        },
#if GF_DARWIN_HOST_OS
        { .key = {"xattr-user-namespace-mode"},
          .type = GF_OPTION_TYPE_STR,
          .default_value = "none",
          .description = "Option to control XATTR user namespace on the raw filesystem: "
          "\t- None: Will use the user namespace, so files will be exchangable with Linux.\n"
          " The raw filesystem will not be compatible with OS X Finder.\n"
          "\t- Strip: Will strip the user namespace before setting. The raw filesystem will work in OS X.\n",
          .op_version = {GD_OP_VERSION_3_6_0},
          .flags = OPT_FLAG_SETTABLE | OPT_FLAG_DOC
        },
#endif
        { .key  = {"shared-brick-count"},
          .type = GF_OPTION_TYPE_INT,
          .default_value = "1",
          .description = "Number of bricks sharing the same backend export."
          " Useful for displaying the proper usable size through statvfs() "
          "call (df command)",
        },
        {
          .key = {"disable-landfill-purge"},
          .type = GF_OPTION_TYPE_BOOL,
          .default_value = "off",
          .description = "Disable glusterfs/landfill purges. "
          "WARNING: This can fill up a brick.",
          .op_version = {GD_OP_VERSION_4_0_0},
          .tags = {"diagnosis"},
        },
        { .key  = {"force-create-mode"},
          .type = GF_OPTION_TYPE_INT,
          .min = 0000,
          .max = 0777,
          .default_value = "0000",
          .validate = GF_OPT_VALIDATE_MIN,
          .validate = GF_OPT_VALIDATE_MAX,
          .description = "Mode bit permission that will always be set on a file."
        },
        { .key  = {"force-directory-mode"},
          .type = GF_OPTION_TYPE_INT,
          .min = 0000,
          .max = 0777,
          .default_value = "0000",
          .validate = GF_OPT_VALIDATE_MIN,
          .validate = GF_OPT_VALIDATE_MAX,
          .description = "Mode bit permission that will be always set on directory"
        },
        { .key  = {"create-mask"},
          .type = GF_OPTION_TYPE_INT,
          .min = 0000,
          .max = 0777,
          .default_value = "0777",
          .validate = GF_OPT_VALIDATE_MIN,
          .validate = GF_OPT_VALIDATE_MAX,
          .description = "Any bit not set here will be removed from the"
          "modes set on a file when it is created"
        },
        { .key  = {"create-directory-mask"},
          .type = GF_OPTION_TYPE_INT,
          .min = 0000,
          .max = 0777,
          .default_value = "0777",
          .validate = GF_OPT_VALIDATE_MIN,
          .validate = GF_OPT_VALIDATE_MAX,
          .description = "Any bit not set here will be removed from the"
          "modes set on a directory when it is created"
        },
        {
          .key = {"max-hardlinks"},
          .type = GF_OPTION_TYPE_INT,
          .min = 0,
          .default_value = "100",
          .op_version  = {GD_OP_VERSION_4_0_0},
          .flags = OPT_FLAG_SETTABLE | OPT_FLAG_DOC,
          .tags = {"posix"},
          .validate = GF_OPT_VALIDATE_MIN,
          .description = "max number of hardlinks allowed on any one inode.\n"
                         "0 is unlimited, 1 prevents any hardlinking at all."
        },
        {
          .key = {"fips-mode-rchecksum"},
          .type = GF_OPTION_TYPE_BOOL,
          .default_value = "off",
          .op_version  = {GD_OP_VERSION_4_0_0},
          .flags = OPT_FLAG_SETTABLE,
          .tags = {"posix"},
          .description = "If enabled, posix_rchecksum uses the FIPS compliant"
                         "SHA256 checksum. MD5 otherwise."
        },
        { .key = {"ctime"},
          .type = GF_OPTION_TYPE_BOOL,
          .default_value = "off",
          .flags = OPT_FLAG_SETTABLE | OPT_FLAG_DOC,
          .op_version = { GD_OP_VERSION_4_1_0 },
          .tags = { "ctime" },
          .description = "When this option is enabled, time attributes (ctime,mtime,atime) "
                         "are stored in xattr to keep it consistent across replica and "
                         "distribute set. The time attributes stored at the backend are "
                         "not considered "
        },
        { .key  = {NULL} }
};
