/*
  Copyright (c) 2008-2012 Red Hat, Inc. <http://www.redhat.com>
  This file is part of GlusterFS.

  This file is licensed to you under your choice of the GNU Lesser
  General Public License, version 3 or any later version (LGPLv3 or
  later), or the GNU General Public License, version 2 (GPLv2), in all
  cases as published by the Free Software Foundation.
*/


#include "tier.h"
#include "dht-common.h"
#include "xlator.h"
#include "syscall.h"
#include <signal.h>
#include <fnmatch.h>
#include <signal.h>
#include "events.h"


#define GF_DISK_SECTOR_SIZE             512
#define DHT_REBALANCE_PID               4242 /* Change it if required */
#define DHT_REBALANCE_BLKSIZE           (128 * 1024)
#define MAX_MIGRATE_QUEUE_COUNT         500
#define MIN_MIGRATE_QUEUE_COUNT         200

#ifndef MAX
#define MAX(a, b) (((a) > (b))?(a):(b))
#endif


#define GF_CRAWL_INDEX_MOVE(idx, sv_cnt)  {     \
                idx++;                          \
                idx %= sv_cnt;                  \
        }

#define GF_FREE_DIR_DFMETA(dir_dfmeta) {                        \
                if (dir_dfmeta) {                               \
                        GF_FREE (dir_dfmeta->head);             \
                        GF_FREE (dir_dfmeta->equeue);           \
                        GF_FREE (dir_dfmeta->iterator);         \
                        GF_FREE (dir_dfmeta->offset_var);       \
                        GF_FREE (dir_dfmeta->fetch_entries);    \
                        GF_FREE (dir_dfmeta);                   \
                }                                               \
        }                                                       \

uint64_t g_totalfiles = 0;


void
gf_defrag_free_container (struct dht_container *container)
{
        if (container) {
                gf_dirent_entry_free (container->df_entry);

                if (container->parent_loc) {
                        loc_wipe (container->parent_loc);
                }

                GF_FREE (container->parent_loc);

                GF_FREE (container);
        }
}

void
dht_set_global_defrag_error (gf_defrag_info_t *defrag, int ret)
{
        LOCK (&defrag->lock);
        {
                defrag->global_error = ret;
        }
        UNLOCK (&defrag->lock);
        return;
}


static gf_boolean_t
dht_is_tier_command (int cmd) {

        gf_boolean_t is_tier = _gf_false;

        switch (cmd) {
        case GF_DEFRAG_CMD_START_TIER:
        case GF_DEFRAG_CMD_STATUS_TIER:
        case GF_DEFRAG_CMD_START_DETACH_TIER:
        case GF_DEFRAG_CMD_STOP_DETACH_TIER:
        case GF_DEFRAG_CMD_PAUSE_TIER:
        case GF_DEFRAG_CMD_RESUME_TIER:
                is_tier = _gf_true;
                break;
        default:
                break;
        }
        return is_tier;

}


static int
dht_send_rebalance_event (xlator_t *this, int cmd, gf_defrag_status_t status)
{
        int ret = -1;
        char *volname = NULL;
        char *tmpstr  = NULL;
        char *ptr = NULL;
        char *suffix = "-dht";
        dht_conf_t   *conf = NULL;
        gf_defrag_info_t *defrag = NULL;
        int len = 0;

        eventtypes_t event = EVENT_LAST;

        switch (status) {
        case GF_DEFRAG_STATUS_COMPLETE:
                event = EVENT_VOLUME_REBALANCE_COMPLETE;
                break;
        case GF_DEFRAG_STATUS_FAILED:
                event = EVENT_VOLUME_REBALANCE_FAILED;
                break;
        case GF_DEFRAG_STATUS_STOPPED:
                event = EVENT_VOLUME_REBALANCE_STOP;
                break;
        default:
                break;

        }

        if (dht_is_tier_command (cmd)) {
                /* We should have the tier volume name*/
                conf = this->private;
                defrag = conf->defrag;
                volname = defrag->tier_conf.volname;
        } else {
                /* DHT volume */
                len = strlen (this->name);
                tmpstr = gf_strdup (this->name);
                if (tmpstr) {
                        ptr = tmpstr + (len - strlen (suffix));
                        if (!strcmp (ptr, suffix)) {
                                tmpstr[len - strlen (suffix)] = '\0';
                                volname = tmpstr;
                        }
                }
        }

        if (!volname) {
                /* Better than nothing */
                volname = this->name;
        }

        if (event != EVENT_LAST) {
                gf_event (event, "volume=%s", volname);
        }

        GF_FREE (tmpstr);
        return ret;
}





static int
dht_write_with_holes (xlator_t *to, fd_t *fd, struct iovec *vec, int count,
                      int32_t size, off_t offset, struct iobref *iobref)
{
        int i            = 0;
        int ret          = -1;
        int start_idx    = 0;
        int tmp_offset   = 0;
        int write_needed = 0;
        int buf_len      = 0;
        int size_pending = 0;
        char *buf        = NULL;

        /* loop through each vector */
        for (i = 0; i < count; i++) {
                buf = vec[i].iov_base;
                buf_len = vec[i].iov_len;

                for (start_idx = 0; (start_idx + GF_DISK_SECTOR_SIZE) <= buf_len;
                     start_idx += GF_DISK_SECTOR_SIZE) {

                        if (mem_0filled (buf + start_idx, GF_DISK_SECTOR_SIZE) != 0) {
                                write_needed = 1;
                                continue;
                        }

                        if (write_needed) {
                                ret = syncop_write (to, fd, (buf + tmp_offset),
                                                    (start_idx - tmp_offset),
                                                    (offset + tmp_offset),
                                                    iobref, 0, NULL, NULL);
                                /* 'path' will be logged in calling function */
                                if (ret < 0) {
                                        gf_log (THIS->name, GF_LOG_WARNING,
                                                "failed to write (%s)",
                                                strerror (-ret));
                                        ret = -1;
                                        goto out;
                                }

                                write_needed = 0;
                        }
                        tmp_offset = start_idx + GF_DISK_SECTOR_SIZE;
                }

                if ((start_idx < buf_len) || write_needed) {
                        /* This means, last chunk is not yet written.. write it */
                        ret = syncop_write (to, fd, (buf + tmp_offset),
                                            (buf_len - tmp_offset),
                                            (offset + tmp_offset), iobref, 0,
                                            NULL, NULL);
                        if (ret < 0) {
                                /* 'path' will be logged in calling function */
                                gf_log (THIS->name, GF_LOG_WARNING,
                                        "failed to write (%s)",
                                        strerror (-ret));
                                ret = -1;
                                goto out;
                        }
                }

                size_pending = (size - buf_len);
                if (!size_pending)
                        break;
        }

        ret = size;
out:
        return ret;

}

/*
   return values:
   -1 : failure
   -2 : success

Hard link migration is carried out in three stages.

(Say there are n hardlinks)
Stage 1: Setting the new hashed subvol information on the 1st hardlink
         encountered (linkto setxattr)

Stage 2: Creating hardlinks on new hashed subvol for the 2nd to (n-1)th
         hardlink

Stage 3: Physical migration of the data file for nth hardlink

Why to deem "-2" as success and not "0":

   dht_migrate_file expects return value "0" from _is_file_migratable if
the file has to be migrated.

   _is_file_migratable returns zero only when it is called with the
flag "GF_DHT_MIGRATE_HARDLINK_IN_PROGRESS".

   gf_defrag_handle_hardlink calls dht_migrate_file for physical migration
of the data file with the flag "GF_DHT_MIGRATE_HARDLINK_IN_PROGRESS"

Hence, gf_defrag_handle_hardlink returning "0" for success will force
"dht_migrate_file" to migrate each of the hardlink which is not intended.

For each of the three stage mentioned above "-2" will be returned and will
be converted to "0" in dht_migrate_file.

*/

int32_t
gf_defrag_handle_hardlink (xlator_t *this, loc_t *loc, dict_t  *xattrs,
                           struct iatt *stbuf)
{
        int32_t                 ret             = -1;
        xlator_t               *cached_subvol   = NULL;
        xlator_t               *hashed_subvol   = NULL;
        xlator_t               *linkto_subvol   = NULL;
        data_t                 *data            = NULL;
        struct iatt             iatt            = {0,};
        int32_t                 op_errno        = 0;
        dht_conf_t             *conf            = NULL;
        gf_loglevel_t          loglevel         = 0;
        dict_t                 *link_xattr      = NULL;

        GF_VALIDATE_OR_GOTO ("defrag", loc, out);
        GF_VALIDATE_OR_GOTO ("defrag", loc->name, out);
        GF_VALIDATE_OR_GOTO ("defrag", stbuf, out);
        GF_VALIDATE_OR_GOTO ("defrag", this, out);
        GF_VALIDATE_OR_GOTO ("defrag", xattrs, out);
        GF_VALIDATE_OR_GOTO ("defrag", this->private, out);

        conf = this->private;

        if (gf_uuid_is_null (loc->pargfid)) {
                gf_msg ("", GF_LOG_ERROR, 0,
                        DHT_MSG_MIGRATE_FILE_FAILED,
                        "Migrate file failed :"
                        "loc->pargfid is NULL for %s", loc->path);
                goto out;
        }

        if (gf_uuid_is_null (loc->gfid)) {
                gf_msg ("", GF_LOG_ERROR, 0,
                        DHT_MSG_MIGRATE_FILE_FAILED,
                        "Migrate file failed :"
                        "loc->gfid is NULL for %s", loc->path);
                goto out;
        }

        link_xattr = dict_new ();
        if (!link_xattr) {
                ret = -1;
                errno = ENOMEM;
                goto out;
        }

        /*
          Parallel migration can lead to migration of the hard link multiple
          times which can lead to data loss. Hence, adding a fresh lookup to
          decide whether migration is required or not.

          Elaborating the scenario for let say 10 hardlinks [link{1..10}]:
              Let say the first hard link "link1"  does the setxattr of the
          new hashed subvolume info on the cached file. As there are multiple
          threads working, we might have already all the links created on the
          new hashed by the time we reach hardlink let say link5. Now the
          number of links on hashed is equal to that of cached. Hence, file
          migration will happen for link6.

                 Cached                                 Hashed
          --------T link6                        rwxrwxrwx   link6

          Now post above state all the link file on the cached will be zero
          byte linkto files. Hence, if we still do migration for the following
          files link{7..10}, we will end up migrating 0 data leading to data
          loss.
                Hence, a lookup can make sure whether we need to migrate the
          file or not.
        */

        ret = syncop_lookup (this, loc, NULL, NULL,
                                             NULL, NULL);
        if (ret) {
                /*Ignore ENOENT and ESTALE as file might have been
                  migrated already*/
                if (-ret == ENOENT || -ret == ESTALE) {
                        ret = -2;
                        goto out;
                }
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        DHT_MSG_MIGRATE_FILE_FAILED,
                        "Migrate file failed:%s lookup failed with ret = %d",
                        loc->path, ret);
                ret = -1;
                goto out;
        }

        cached_subvol = dht_subvol_get_cached (this, loc->inode);
        if (!cached_subvol) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        DHT_MSG_MIGRATE_FILE_FAILED,
                        "Migrate file failed :"
                        "Failed to get cached subvol"
                        " for %s on %s", loc->name, this->name);
                goto out;
        }

        hashed_subvol = dht_subvol_get_hashed (this, loc);
        if (!hashed_subvol) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        DHT_MSG_MIGRATE_FILE_FAILED,
                        "Migrate file failed :"
                        "Failed to get hashed subvol"
                        " for %s on %s", loc->name, this->name);
                goto out;
        }

        if (hashed_subvol == cached_subvol) {
                ret = -2;
                goto out;
        }

        gf_log (this->name, GF_LOG_INFO, "Attempting to migrate hardlink %s "
                "with gfid %s from %s -> %s", loc->name, uuid_utoa (loc->gfid),
                cached_subvol->name, hashed_subvol->name);
        data = dict_get (xattrs, conf->link_xattr_name);
        /* set linkto on cached -> hashed if not present, else link it */
        if (!data) {
                ret = dict_set_str (link_xattr, conf->link_xattr_name,
                                    hashed_subvol->name);
                if (ret) {
                        gf_msg (this->name, GF_LOG_ERROR, 0,
                                DHT_MSG_MIGRATE_FILE_FAILED,
                                "Migrate file failed :"
                                "Failed to set dictionary value:"
                                " key = %s for %s",
                                conf->link_xattr_name, loc->name);
                        goto out;
                }

                ret = syncop_setxattr (cached_subvol, loc, link_xattr, 0, NULL,
                                       NULL);
                if (ret) {
                        gf_msg (this->name, GF_LOG_ERROR, 0,
                                DHT_MSG_MIGRATE_FILE_FAILED,
                                "Migrate file failed :"
                                "Linkto setxattr failed %s -> %s (%s)",
                                cached_subvol->name,
                                loc->name, strerror (-ret));
                        ret = -1;
                        goto out;
                }
                ret = -2;
                goto out;
        } else {
                linkto_subvol = dht_linkfile_subvol (this, NULL, NULL, xattrs);
                if (!linkto_subvol) {
                        gf_msg (this->name, GF_LOG_ERROR, 0,
                                DHT_MSG_SUBVOL_ERROR,
                                "Failed to get "
                                "linkto subvol for %s", loc->name);
                } else {
                        hashed_subvol = linkto_subvol;
                }

                ret = syncop_link (hashed_subvol, loc, loc, &iatt, NULL, NULL);
                if  (ret) {
                        op_errno = -ret;
                        ret = -1;

                        loglevel = (op_errno == EEXIST) ? GF_LOG_DEBUG : \
                                    GF_LOG_ERROR;
                        gf_msg (this->name, loglevel, op_errno,
                                DHT_MSG_MIGRATE_HARDLINK_FILE_FAILED,
                                "link of %s -> %s"
                                " failed on  subvol %s", loc->name,
                                uuid_utoa(loc->gfid),
                                hashed_subvol->name);
                        if (op_errno != EEXIST)
                                goto out;
                }
        }
        ret = syncop_lookup (hashed_subvol, loc, &iatt, NULL, NULL, NULL);
        if (ret) {
                gf_msg (this->name, GF_LOG_ERROR, -ret,
                        DHT_MSG_MIGRATE_FILE_FAILED,
                        "Migrate file failed :Failed lookup %s on %s ",
                        loc->name, hashed_subvol->name);

                ret = -1;
                goto out;
        }

        if (iatt.ia_nlink == stbuf->ia_nlink) {
                ret = dht_migrate_file (this, loc, cached_subvol, hashed_subvol,
                                        GF_DHT_MIGRATE_HARDLINK_IN_PROGRESS);
                if (ret)
                        goto out;
        }
        ret = -2;
out:
        if (link_xattr)
                dict_unref (link_xattr);
        return ret;
}



static int
__check_file_has_hardlink (xlator_t *this, loc_t *loc,
                      struct iatt *stbuf, dict_t *xattrs, int flags,
                                gf_defrag_info_t *defrag)
{
       int ret = 0;

       if (flags == GF_DHT_MIGRATE_HARDLINK_IN_PROGRESS) {
                ret = 0;
                return ret;
       }
       if (stbuf->ia_nlink > 1) {
                /* support for decomission */
                if (flags == GF_DHT_MIGRATE_HARDLINK) {
                        synclock_lock (&defrag->link_lock);
                        ret = gf_defrag_handle_hardlink
                                (this, loc, xattrs, stbuf);
                        synclock_unlock (&defrag->link_lock);
                        /*
                        Returning zero will force the file to be remigrated.
                        Checkout gf_defrag_handle_hardlink for more information.
                        */
                        if (ret && ret != -2) {
                                gf_msg (this->name, GF_LOG_WARNING, 0,
                                        DHT_MSG_MIGRATE_FILE_FAILED,
                                        "Migrate file failed:"
                                        "%s: failed to migrate file with link",
                                        loc->path);
                        }
                } else {
                        gf_msg (this->name, GF_LOG_WARNING, 0,
                                DHT_MSG_MIGRATE_FILE_FAILED,
                                "Migrate file failed:"
                                "%s: file has hardlinks", loc->path);
                        ret = -ENOTSUP;
                }
       }

       return ret;
}


/*
     return values
     0 : File will be migrated
    -2 : File will not be migrated
         (This is the return value from gf_defrag_handle_hardlink. Checkout
         gf_defrag_handle_hardlink for description of "returning -2")
    -1 : failure
*/
static int
__is_file_migratable (xlator_t *this, loc_t *loc,
                      struct iatt *stbuf, dict_t *xattrs, int flags,
                                gf_defrag_info_t *defrag)
{
        int ret = -1;
        int lock_count = 0;

        if (IA_ISDIR (stbuf->ia_type)) {
                gf_msg (this->name, GF_LOG_WARNING, 0,
                        DHT_MSG_MIGRATE_FILE_FAILED,
                        "Migrate file failed:"
                        "%s: migrate-file called on directory", loc->path);
                ret = -1;
                goto out;
        }

        if (!defrag->lock_migration_enabled) {
                ret = dict_get_int32 (xattrs, GLUSTERFS_POSIXLK_COUNT,
                                      &lock_count);
                if (ret) {
                        gf_msg (this->name, GF_LOG_WARNING, 0,
                                DHT_MSG_MIGRATE_FILE_FAILED,
                                "Migrate file failed:"
                                "%s: Unable to get lock count for file",
                                loc->path);
                        ret = -1;
                        goto out;
                }

                if (lock_count) {
                        gf_msg (this->name, GF_LOG_WARNING, 0,
                                DHT_MSG_MIGRATE_FILE_FAILED,
                                "Migrate file failed: %s: File has locks."
                                " Skipping file migration", loc->path);
                        ret = -1;
                        goto out;
                }
        }

        /* Check if file has hardlink*/
        ret = __check_file_has_hardlink (this, loc, stbuf, xattrs,
                                         flags, defrag);
out:
        return ret;
}


static int
__dht_rebalance_create_dst_file (xlator_t *to, xlator_t *from, loc_t *loc, struct iatt *stbuf,
                                 fd_t **dst_fd, dict_t *xattr)
{
        xlator_t    *this = NULL;
        int          ret  = -1;
        fd_t        *fd   = NULL;
        struct iatt  new_stbuf = {0,};
        struct iatt  check_stbuf= {0,};
        dht_conf_t  *conf = NULL;
        dict_t      *dict = NULL;

        this = THIS;
        conf = this->private;

        dict = dict_new ();
        if (!dict)
                goto out;

        ret = dict_set_static_bin (dict, "gfid-req", stbuf->ia_gfid, 16);
        if (ret) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        DHT_MSG_DICT_SET_FAILED,
                        "%s: failed to set dictionary value: key = gfid-req",
                        loc->path);
                goto out;
        }

        ret = dict_set_str (dict, conf->link_xattr_name, from->name);
        if (ret) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        DHT_MSG_DICT_SET_FAILED,
                        "%s: failed to set dictionary value: key = %s ",
                        loc->path, conf->link_xattr_name);
                goto out;
        }

        fd = fd_create (loc->inode, DHT_REBALANCE_PID);
        if (!fd) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        DHT_MSG_MIGRATE_FILE_FAILED,
                        "%s: fd create failed (destination) (%s)",
                        loc->path, strerror (errno));
                ret = -1;
                goto out;
        }

        ret = syncop_lookup (to, loc, &new_stbuf, NULL, NULL, NULL);
        if (!ret) {
                /* File exits in the destination, check if gfid matches */
                if (gf_uuid_compare (stbuf->ia_gfid, new_stbuf.ia_gfid) != 0) {
                        gf_msg (this->name, GF_LOG_ERROR, 0,
                                DHT_MSG_GFID_MISMATCH,
                                "file %s exists in %s with different gfid",
                                loc->path, to->name);
                        ret = -1;
                        goto out;
                }
        }
        if ((ret < 0) && (-ret != ENOENT)) {
                /* File exists in destination, but not accessible */
                gf_msg (THIS->name, GF_LOG_WARNING, 0,
                        DHT_MSG_MIGRATE_FILE_FAILED,
                        "%s: failed to lookup file (%s)",
                        loc->path, strerror (-ret));
                ret = -1;
                goto out;
        }

        /* Create the destination with LINKFILE mode, and linkto xattr,
           if the linkfile already exists, just open the file */
        if (!ret) {
                /*
                 * File already present, just open the file.
                 */
                ret = syncop_open (to, loc, O_RDWR, fd, NULL, NULL);
                 if (ret < 0) {
                        gf_msg (this->name, GF_LOG_ERROR, -ret,
                                DHT_MSG_MIGRATE_FILE_FAILED,
                                "failed to open %s on %s",
                                loc->path, to->name);
                        ret = -1;
                        goto out;
                 }
        } else {
                ret = syncop_create (to, loc, O_RDWR, DHT_LINKFILE_MODE, fd,
                                     &new_stbuf, dict, NULL);
                 if (ret < 0) {
                        gf_msg (this->name, GF_LOG_ERROR, -ret,
                                DHT_MSG_MIGRATE_FILE_FAILED,
                                "failed to create %s on %s",
                                loc->path, to->name);
                        ret = -1;
                        goto out;
                }

        }

        fd_bind (fd);

        /*Reason of doing lookup after create again:
         *In the create, there is some time-gap between opening fd at the
         *server (posix_layer) and binding it in server (incrementing fd count),
         *so if in that time-gap, if other process sends unlink considering it
         *as a linkto file, because inode->fd count will be 0, so file will be
         *unlinked at the backend. And because furthur operations are performed
         *on fd, so though migration will be done but will end with no file
         *at  the backend.
         */

        ret = syncop_lookup (to, loc, &check_stbuf, NULL, NULL, NULL);
        if (!ret) {

                if (gf_uuid_compare (stbuf->ia_gfid, check_stbuf.ia_gfid) != 0) {
                        gf_msg (this->name, GF_LOG_ERROR, 0,
                                DHT_MSG_GFID_MISMATCH,
                                "file %s exists in %s with different gfid,"
                                "found in lookup after create",
                                loc->path, to->name);
                        ret = -1;
                        goto out;
                }

        }

        if (-ret == ENOENT) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        DHT_MSG_MIGRATE_FILE_FAILED, "%s: file does not exists"
                        "on %s (%s)", loc->path, to->name, strerror (-ret));
                ret = -1;
                goto out;
        }

        ret = syncop_fsetxattr (to, fd, xattr, 0, NULL, NULL);
        if (ret < 0)
                gf_msg (this->name, GF_LOG_WARNING, 0,
                        DHT_MSG_MIGRATE_FILE_FAILED,
                        "%s: failed to set xattr on %s (%s)",
                        loc->path, to->name, strerror (-ret));

        ret = syncop_ftruncate (to, fd, stbuf->ia_size, NULL, NULL);
        if (ret < 0)
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        DHT_MSG_MIGRATE_FILE_FAILED,
                        "ftruncate failed for %s on %s (%s)",
                        loc->path, to->name, strerror (-ret));

        ret = syncop_fsetattr (to, fd, stbuf,
                               (GF_SET_ATTR_UID | GF_SET_ATTR_GID),
                                NULL, NULL, NULL, NULL);
        if (ret < 0)
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        DHT_MSG_MIGRATE_FILE_FAILED,
                        "chown failed for %s on %s (%s)",
                        loc->path, to->name, strerror (-ret));

        /* success */
        ret = 0;

        if (dst_fd)
                *dst_fd = fd;

out:
        if (ret) {
                if (fd) {
                        fd_unref (fd);
                }
        }
        if (dict)
                dict_unref (dict);

        return ret;
}

static int
__dht_check_free_space (xlator_t *to, xlator_t *from, loc_t *loc,
                        struct iatt *stbuf, int flag)
{
        struct statvfs  src_statfs = {0,};
        struct statvfs  dst_statfs = {0,};
        int             ret        = -1;
        xlator_t       *this       = NULL;
        dict_t         *xdata      = NULL;

        uint64_t        src_statfs_blocks = 1;
        uint64_t        dst_statfs_blocks = 1;

        this = THIS;

        xdata = dict_new ();
        if (!xdata) {
                errno = ENOMEM;
                gf_msg (this->name, GF_LOG_ERROR, ENOMEM,
                        DHT_MSG_NO_MEMORY,
                        "failed to allocate dictionary");
                goto out;
        }

        ret = dict_set_int8 (xdata, GF_INTERNAL_IGNORE_DEEM_STATFS, 1);
        if (ret) {
                gf_log (this->name, GF_LOG_ERROR,
                        "Failed to set "
                        GF_INTERNAL_IGNORE_DEEM_STATFS" in dict");
                ret = -1;
                goto out;
        }

        ret = syncop_statfs (from, loc, &src_statfs, xdata, NULL);
        if (ret) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        DHT_MSG_MIGRATE_FILE_FAILED,
                        "failed to get statfs of %s on %s (%s)",
                        loc->path, from->name, strerror (-ret));
                ret = -1;
                goto out;
        }

        ret = syncop_statfs (to, loc, &dst_statfs, xdata, NULL);
        if (ret) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        DHT_MSG_MIGRATE_FILE_FAILED,
                        "failed to get statfs of %s on %s (%s)",
                        loc->path, to->name, strerror (-ret));
                ret = -1;
                goto out;
        }

        /* if force option is given, do not check for space @ dst.
         * Check only if space is avail for the file */
        if (flag != GF_DHT_MIGRATE_DATA)
                goto check_avail_space;

        /* Check:
           During rebalance `migrate-data` - Destination subvol experiences
           a `reduction` in 'blocks' of free space, at the same time source
           subvol gains certain 'blocks' of free space. A valid check is
           necessary here to avoid errorneous move to destination where
           the space could be scantily available.
         */
        if (stbuf) {
                dst_statfs_blocks = ((dst_statfs.f_bavail *
                                      dst_statfs.f_bsize) /
                                     GF_DISK_SECTOR_SIZE);
                src_statfs_blocks = ((src_statfs.f_bavail *
                                      src_statfs.f_bsize) /
                                     GF_DISK_SECTOR_SIZE);
                if ((dst_statfs_blocks - stbuf->ia_blocks) <
                    (src_statfs_blocks + stbuf->ia_blocks)) {

                        gf_msg (this->name, GF_LOG_WARNING, 0,
                               DHT_MSG_MIGRATE_FILE_FAILED,
                               "data movement of file "
                               "{blocks:%"PRIu64" name:(%s) } would result in "
                               "dst node (%s:%"PRIu64") having lower disk "
                               "space then the source node (%s:%"PRIu64")"
                               ".Skipping file.", stbuf->ia_blocks, loc->path,
                               to->name, dst_statfs_blocks, from->name,
                               src_statfs_blocks);

                        /* this is not a 'failure', but we don't want to
                           consider this as 'success' too :-/ */
                        ret = -1;
                        goto out;
                }
        }
check_avail_space:
        if (((dst_statfs.f_bavail * dst_statfs.f_bsize) /
              GF_DISK_SECTOR_SIZE) < stbuf->ia_blocks) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        DHT_MSG_MIGRATE_FILE_FAILED,
                        "data movement attempted from node (%s) to node (%s) "
                        "which does not have required free space for (%s)",
                        from->name, to->name, loc->path);
                ret = -1;
                goto out;
        }

        ret = 0;
out:
        if (xdata)
                dict_unref (xdata);
        return ret;
}

static int
__dht_rebalance_migrate_data (xlator_t *from, xlator_t *to, fd_t *src, fd_t *dst,
                              uint64_t ia_size, int hole_exists)
{
        int            ret    = 0;
        int            count  = 0;
        off_t          offset = 0;
        struct iovec  *vector = NULL;
        struct iobref *iobref = NULL;
        uint64_t       total  = 0;
        size_t         read_size = 0;

        /* if file size is '0', no need to enter this loop */
        while (total < ia_size) {
                read_size = (((ia_size - total) > DHT_REBALANCE_BLKSIZE) ?
                             DHT_REBALANCE_BLKSIZE : (ia_size - total));

                ret = syncop_readv (from, src, read_size,
                                    offset, 0, &vector, &count, &iobref, NULL,
                                    NULL);
                if (!ret || (ret < 0)) {
                        break;
                }

                if (hole_exists)
                        ret = dht_write_with_holes (to, dst, vector, count,
                                                    ret, offset, iobref);
                else
                        ret = syncop_writev (to, dst, vector, count,
                                             offset, iobref, 0, NULL, NULL);
                if (ret < 0) {
                        break;
                }
                offset += ret;
                total += ret;

                GF_FREE (vector);
                if (iobref)
                        iobref_unref (iobref);
                iobref = NULL;
                vector = NULL;
        }
        if (iobref)
                iobref_unref (iobref);
        GF_FREE (vector);

        if (ret >= 0)
                ret = 0;
        else
                ret = -1;

        return ret;
}

static int
__tier_migrate_data (gf_defrag_info_t *defrag, xlator_t *from, xlator_t *to, fd_t *src, fd_t *dst,
                     uint64_t ia_size, int hole_exists)
{
        int            ret    = 0;
        int            count  = 0;
        off_t          offset = 0;
        struct iovec  *vector = NULL;
        struct iobref *iobref = NULL;
        uint64_t       total  = 0;
        size_t         read_size = 0;

        /* if file size is '0', no need to enter this loop */
        while (total < ia_size) {

                read_size = (((ia_size - total) > DHT_REBALANCE_BLKSIZE) ?
                             DHT_REBALANCE_BLKSIZE : (ia_size - total));

                ret = syncop_readv (from, src, read_size,
                                    offset, 0, &vector, &count, &iobref, NULL,
                                    NULL);
                if (!ret || (ret < 0)) {
                        break;
                }

                if (hole_exists)
                        ret = dht_write_with_holes (to, dst, vector, count,
                                                    ret, offset, iobref);
                else
                        ret = syncop_writev (to, dst, vector, count,
                                             offset, iobref, 0, NULL, NULL);
                if (gf_defrag_get_pause_state (&defrag->tier_conf) != TIER_RUNNING) {
                        gf_msg ("tier", GF_LOG_INFO, 0,
                                DHT_MSG_TIER_PAUSED,
                                "Migrate file paused");
                        ret = -1;
                }

                if (ret < 0) {
                        break;
                }
                offset += ret;
                total += ret;

                GF_FREE (vector);
                if (iobref)
                        iobref_unref (iobref);
                iobref = NULL;
                vector = NULL;
        }
        if (iobref)
                iobref_unref (iobref);
        GF_FREE (vector);

        if (ret >= 0)
                ret = 0;
        else
                ret = -1;

        return ret;
}


static int
__dht_rebalance_open_src_file (xlator_t *from, xlator_t *to, loc_t *loc,
                               struct iatt *stbuf, fd_t **src_fd,
                               gf_boolean_t *clean_src)
{
        int          ret  = 0;
        fd_t        *fd   = NULL;
        dict_t      *dict = NULL;
        xlator_t    *this = NULL;
        struct iatt  iatt = {0,};
        dht_conf_t  *conf = NULL;

        this = THIS;
        conf = this->private;

        *clean_src = _gf_false;

        fd = fd_create (loc->inode, DHT_REBALANCE_PID);
        if (!fd) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        DHT_MSG_MIGRATE_FILE_FAILED,
                        "%s: fd create failed (source)", loc->path);
                ret = -1;
                goto out;
        }

        ret = syncop_open (from, loc, O_RDWR, fd, NULL, NULL);
        if (ret < 0) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        DHT_MSG_MIGRATE_FILE_FAILED,
                        "failed to open file %s on %s (%s)",
                        loc->path, from->name, strerror (-ret));
                ret = -1;
                goto out;
        }

        fd_bind (fd);

        if (src_fd)
                *src_fd = fd;

        ret = -1;
        dict = dict_new ();
        if (!dict)
                goto out;

        ret = dict_set_str (dict, conf->link_xattr_name, to->name);
        if (ret) {
                gf_log (this->name, GF_LOG_ERROR,
                        "failed to set xattr in dict for %s (linkto:%s)",
                        loc->path, to->name);
                goto out;
        }

        /* Once the migration starts, the source should have 'linkto' key set
           to show which is the target, so other clients can work around it */
        ret = syncop_setxattr (from, loc, dict, 0, NULL, NULL);
        if (ret) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        DHT_MSG_MIGRATE_FILE_FAILED,
                        "failed to set xattr on %s in %s (%s)",
                        loc->path, from->name, strerror (-ret));
                ret = -1;
                goto out;
        }

        /* Reset source mode/xattr if migration fails*/
        *clean_src = _gf_true;

        /* mode should be (+S+T) to indicate migration is in progress */
        iatt.ia_prot = stbuf->ia_prot;
        iatt.ia_type = stbuf->ia_type;
        iatt.ia_prot.sticky = 1;
        iatt.ia_prot.sgid = 1;

        ret = syncop_setattr (from, loc, &iatt, GF_SET_ATTR_MODE, NULL, NULL,
                              NULL, NULL);
        if (ret) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        DHT_MSG_MIGRATE_FILE_FAILED,
                        "failed to set mode on %s in %s (%s)",
                        loc->path, from->name, strerror (-ret));
                ret = -1;
                goto out;
        }

        /* success */
        ret = 0;
out:
        if (dict)
                dict_unref (dict);

        return ret;
}

int
migrate_special_files (xlator_t *this, xlator_t *from, xlator_t *to, loc_t *loc,
                       struct iatt *buf)
{
        int          ret      = -1;
        dict_t      *rsp_dict = NULL;
        dict_t      *dict     = NULL;
        char        *link     = NULL;
        struct iatt  stbuf    = {0,};
        dht_conf_t  *conf     = this->private;

        dict = dict_new ();
        if (!dict)
                goto out;

        ret = dict_set_int32 (dict, conf->link_xattr_name, 256);
        if (ret) {
                gf_log (this->name, GF_LOG_ERROR,
                        "%s: failed to set 'linkto' key in dict", loc->path);
                goto out;
        }

        /* check in the destination if the file is link file */
        ret = syncop_lookup (to, loc, &stbuf, NULL, dict, &rsp_dict);
        if ((ret < 0) && (-ret != ENOENT)) {
                gf_msg (this->name, GF_LOG_WARNING, 0,
                        DHT_MSG_MIGRATE_FILE_FAILED,
                        "%s: lookup failed (%s)",
                        loc->path, strerror (-ret));
                ret = -1;
                goto out;
        }

        /* we no more require this key */
        dict_del (dict, conf->link_xattr_name);

        /* file exists in target node, only if it is 'linkfile' its valid,
           otherwise, error out */
        if (!ret) {
                if (!check_is_linkfile (loc->inode, &stbuf, rsp_dict,
                                        conf->link_xattr_name)) {
                        gf_msg (this->name, GF_LOG_WARNING, 0,
                                DHT_MSG_MIGRATE_FILE_FAILED,
                                "%s: file exists in destination", loc->path);
                        ret = -1;
                        goto out;
                }

                /* as file is linkfile, delete it */
                ret = syncop_unlink (to, loc, NULL, NULL);
                if (ret) {
                        gf_msg (this->name, GF_LOG_WARNING, 0,
                                DHT_MSG_MIGRATE_FILE_FAILED,
                                "%s: failed to delete the linkfile (%s)",
                                loc->path, strerror (-ret));
                        ret = -1;
                        goto out;
                }
        }

        /* Set the gfid of the source file in dict */
        ret = dict_set_static_bin (dict, "gfid-req", buf->ia_gfid, 16);
        if (ret) {
                gf_log (this->name, GF_LOG_ERROR,
                        "%s: failed to set gfid in dict for create", loc->path);
                goto out;
        }

        /* Create the file in target */
        if (IA_ISLNK (buf->ia_type)) {
                /* Handle symlinks separately */
                ret = syncop_readlink (from, loc, &link, buf->ia_size, NULL,
                                       NULL);
                if (ret < 0) {
                        gf_msg (this->name, GF_LOG_WARNING, 0,
                                DHT_MSG_MIGRATE_FILE_FAILED,
                                "%s: readlink on symlink failed (%s)",
                                loc->path, strerror (-ret));
                        ret = -1;
                        goto out;
                }

                ret = syncop_symlink (to, loc, link, 0, dict, NULL);
                if (ret) {
                        gf_msg (this->name, GF_LOG_WARNING, 0,
                                DHT_MSG_MIGRATE_FILE_FAILED,
                                "%s: creating symlink failed (%s)",
                                loc->path, strerror (-ret));
                        ret = -1;
                        goto out;
                }

                goto done;
        }

        ret = syncop_mknod (to, loc, st_mode_from_ia (buf->ia_prot,
                                                      buf->ia_type),
                            makedev (ia_major (buf->ia_rdev),
                                     ia_minor (buf->ia_rdev)), 0, dict, NULL);
        if (ret) {
                gf_msg (this->name, GF_LOG_WARNING, 0,
                        DHT_MSG_MIGRATE_FILE_FAILED,
                        "%s: mknod failed (%s)",
                        loc->path, strerror (-ret));
                ret = -1;
                goto out;
        }

done:
        ret = syncop_setattr (to, loc, buf,
                              (GF_SET_ATTR_MTIME |
                               GF_SET_ATTR_UID | GF_SET_ATTR_GID |
                               GF_SET_ATTR_MODE), NULL, NULL, NULL, NULL);
        if (ret) {
                gf_msg (this->name, GF_LOG_WARNING, 0,
                        DHT_MSG_MIGRATE_FILE_FAILED,
                        "%s: failed to perform setattr on %s (%s)",
                        loc->path, to->name, strerror (-ret));
                ret = -1;
        }

        ret = syncop_unlink (from, loc, NULL, NULL);
        if (ret) {
                gf_msg (this->name, GF_LOG_WARNING, 0,
                        DHT_MSG_MIGRATE_FILE_FAILED,
                        "%s: unlink failed (%s)",
                        loc->path, strerror (-ret));
                ret = -1;
        }

out:
        GF_FREE (link);
        if (dict)
                dict_unref (dict);

        if (rsp_dict)
                dict_unref (rsp_dict);

        return ret;
}

static int
__dht_migration_cleanup_src_file (xlator_t *this, loc_t *loc, fd_t *fd,
                                  xlator_t *from, ia_prot_t *src_ia_prot)
{
        int ret                       = -1;
        dht_conf_t     *conf          = NULL;
        struct iatt     new_stbuf     = {0,};

        if (!this || !fd || !from || !src_ia_prot) {
                goto out;
        }

        conf = this->private;

        /*Revert source mode and xattr changes*/
        ret = syncop_fstat (from, fd, &new_stbuf, NULL, NULL);
        if (ret < 0) {
                /* Failed to get the stat info */
                gf_msg (this->name, GF_LOG_ERROR, -ret,
                        DHT_MSG_MIGRATE_FILE_FAILED,
                        "Migrate file cleanup failed: failed to fstat "
                        "file %s on %s ", loc->path, from->name);
                ret = -1;
                goto out;
        }


        /* Remove the sticky bit and sgid bit set, reset it to 0*/
        if (!src_ia_prot->sticky)
                new_stbuf.ia_prot.sticky = 0;

        if (!src_ia_prot->sgid)
                new_stbuf.ia_prot.sgid = 0;

        ret = syncop_fsetattr (from, fd, &new_stbuf,
                               (GF_SET_ATTR_GID | GF_SET_ATTR_MODE),
                               NULL, NULL, NULL, NULL);

        if (ret) {
                gf_msg (this->name, GF_LOG_WARNING, -ret,
                        DHT_MSG_MIGRATE_FILE_FAILED,
                        "Migrate file cleanup failed:"
                        "%s: failed to perform fsetattr on %s ",
                        loc->path, from->name);
                ret = -1;
                goto out;
        }

        ret = syncop_fremovexattr (from, fd, conf->link_xattr_name, 0, NULL);
        if (ret) {
                gf_log (this->name, GF_LOG_WARNING,
                        "%s: failed to remove linkto xattr on %s (%s)",
                        loc->path, from->name, strerror (-ret));
                ret = -1;
                goto out;
        }

        ret = 0;

out:
        return ret;
}



/*
  return values:

   -1 : failure
    0 : successfully migrated data
    1 : not a failure, but we can't migrate data as of now
*/
int
dht_migrate_file (xlator_t *this, loc_t *loc, xlator_t *from, xlator_t *to,
                  int flag)
{
        int                     ret                     = -1;
        struct iatt             new_stbuf               = {0,};
        struct iatt             stbuf                   = {0,};
        struct iatt             empty_iatt              = {0,};
        ia_prot_t               src_ia_prot             = {0,};
        fd_t                    *src_fd                 = NULL;
        fd_t                    *dst_fd                 = NULL;
        dict_t                  *dict                   = NULL;
        dict_t                  *xattr                  = NULL;
        dict_t                  *xattr_rsp              = NULL;
        int                     file_has_holes          = 0;
        dht_conf_t              *conf                   = this->private;
        int                     rcvd_enoent_from_src    = 0;
        struct gf_flock         flock                   = {0, };
        struct gf_flock         plock                   = {0, };
        loc_t                   tmp_loc                 = {0, };
        gf_boolean_t            locked                  = _gf_false;
        gf_boolean_t            p_locked                = _gf_false;
        int                     lk_ret                  = -1;
        gf_defrag_info_t        *defrag                 =  NULL;
        gf_boolean_t            clean_src               = _gf_false;
        gf_boolean_t            clean_dst               = _gf_false;
        int                     log_level               = GF_LOG_INFO;
        gf_boolean_t            delete_src_linkto       = _gf_true;
        lock_migration_info_t   locklist;
        dict_t                  *meta_dict              = NULL;
        gf_boolean_t            meta_locked             = _gf_false;

        defrag = conf->defrag;
        if (!defrag)
                goto out;

        if (defrag->tier_conf.is_tier)
                log_level = GF_LOG_TRACE;

        gf_log (this->name,
                log_level, "%s: attempting to move from %s to %s",
                loc->path, from->name, to->name);

        dict = dict_new ();
        if (!dict)
                goto out;

        ret = dict_set_int32 (dict, conf->link_xattr_name, 256);
        if (ret) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        DHT_MSG_MIGRATE_FILE_FAILED,
                        "Migrate file failed:"
                        "%s: failed to set 'linkto' key in dict", loc->path);
                goto out;
        }


        /* Do not migrate file in case lock migration is not enabled on the
         * volume*/
        if (!defrag->lock_migration_enabled) {
                ret = dict_set_int32 (dict,
                                 GLUSTERFS_POSIXLK_COUNT, sizeof(int32_t));
                if (ret) {
                        gf_msg (this->name, GF_LOG_ERROR, 0,
                                DHT_MSG_MIGRATE_FILE_FAILED,
                                "Migrate file failed: %s: failed to "
                                "set "GLUSTERFS_POSIXLK_COUNT" key in dict",
                                loc->path);
                        goto out;
                }
        } else {
                gf_msg (this->name, GF_LOG_INFO, 0, 0, "locks will be migrated"
                        " for file: %s", loc->path);
        }

        flock.l_type = F_WRLCK;

        tmp_loc.inode = inode_ref (loc->inode);
        gf_uuid_copy (tmp_loc.gfid, loc->gfid);
        tmp_loc.path = gf_strdup(loc->path);

        ret = syncop_inodelk (from, DHT_FILE_MIGRATE_DOMAIN, &tmp_loc, F_SETLKW,
                              &flock, NULL, NULL);
        if (ret < 0) {
                gf_msg (this->name, GF_LOG_WARNING, 0,
                        DHT_MSG_MIGRATE_FILE_FAILED,
                        "migrate file failed: "
                        "%s: failed to lock file on %s (%s)",
                        loc->path, from->name, strerror (-ret));
                ret = -1;
                goto out;
        }

        locked = _gf_true;

        /* Phase 1 - Data migration is in progress from now on */
        ret = syncop_lookup (from, loc, &stbuf, NULL, dict, &xattr_rsp);
        if (ret) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        DHT_MSG_MIGRATE_FILE_FAILED,
                        "Migrate file failed:"
                        "%s: lookup failed on %s (%s)",
                        loc->path, from->name, strerror (-ret));
                ret = -1;
                goto out;
        }

        /* preserve source mode, so set the same to the destination */
        src_ia_prot = stbuf.ia_prot;

        /* Check if file can be migrated */
        ret = __is_file_migratable (this, loc, &stbuf, xattr_rsp, flag, defrag);
        if (ret) {
                if (ret == -2)
                        ret = 0;
                goto out;
        }

        /* Take care of the special files */
        if (!IA_ISREG (stbuf.ia_type)) {
                /* Special files */
                ret = migrate_special_files (this, from, to, loc, &stbuf);
                goto out;
        }


        /* TODO: move all xattr related operations to fd based operations */
        ret = syncop_listxattr (from, loc, &xattr, NULL, NULL);
        if (ret < 0) {
                gf_msg (this->name, GF_LOG_WARNING, 0,
                        DHT_MSG_MIGRATE_FILE_FAILED,
                        "Migrate file failed:"
                        "%s: failed to get xattr from %s (%s)",
                        loc->path, from->name, strerror (-ret));
                ret = -1;
        }

        /* create the destination, with required modes/xattr */
        ret = __dht_rebalance_create_dst_file (to, from, loc, &stbuf,
                                               &dst_fd, xattr);
        if (ret)
                goto out;

        clean_dst = _gf_true;

        ret = __dht_check_free_space (to, from, loc, &stbuf, flag);

        if (ret) {
                goto out;
        }

        /* Open the source, and also update mode/xattr */
        ret = __dht_rebalance_open_src_file (from, to, loc, &stbuf, &src_fd,
                                             &clean_src);
        if (ret) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        DHT_MSG_MIGRATE_FILE_FAILED,
                        "Migrate file failed: failed to open %s on %s",
                        loc->path, from->name);
                goto out;
        }
        if (xattr_rsp) {
                /* we no more require this key */
                dict_del (dict, conf->link_xattr_name);
                dict_unref (xattr_rsp);
        }

        ret = syncop_fstat (from, src_fd, &stbuf, dict, &xattr_rsp);
        if (ret) {
                gf_msg (this->name, GF_LOG_ERROR, -ret,
                        DHT_MSG_MIGRATE_FILE_FAILED,
                        "Migrate file failed:failed to lookup %s on %s ",
                        loc->path, from->name);
                ret = -1;
                goto out;
        }

        /* Check again if file has hardlink */
        ret = __check_file_has_hardlink (this, loc, &stbuf, xattr_rsp,
                                         flag, defrag);
        if (ret) {
                if (ret == -2)
                        ret = 0;
                goto out;
        }
        /* Try to preserve 'holes' while migrating data */
        if (stbuf.ia_size > (stbuf.ia_blocks * GF_DISK_SECTOR_SIZE))
                file_has_holes = 1;


        /* All I/O happens in this function */
        if (defrag->cmd == GF_DEFRAG_CMD_START_TIER) {
                ret = __tier_migrate_data (defrag, from, to, src_fd, dst_fd,
                                                    stbuf.ia_size, file_has_holes);
        } else {
                ret = __dht_rebalance_migrate_data (from, to, src_fd, dst_fd,
                                                    stbuf.ia_size, file_has_holes);
        }

        if (ret) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        DHT_MSG_MIGRATE_FILE_FAILED,
                        "Migrate file failed: %s: failed to migrate data",
                        loc->path);

                ret = -1;
                goto out;
        }

        /* TODO: Sync the locks */

        ret = syncop_fsync (to, dst_fd, 0, NULL, NULL);
        if (ret) {
                gf_log (this->name, GF_LOG_WARNING,
                        "%s: failed to fsync on %s (%s)",
                        loc->path, to->name, strerror (-ret));
                ret = -1;
        }


        /* Phase 2 - Data-Migration Complete, Housekeeping updates pending */

        ret = syncop_fstat (from, src_fd, &new_stbuf, NULL, NULL);
        if (ret < 0) {
                /* Failed to get the stat info */
                gf_msg ( this->name, GF_LOG_ERROR, -ret,
                        DHT_MSG_MIGRATE_FILE_FAILED,
                        "Migrate file failed: failed to fstat file %s on %s ",
                        loc->path, from->name);
                ret = -1;
                goto out;
        }

        /* Lock the entire source file to prevent clients from taking a
           lock on it as dht_lk does not handle file migration.

           This still leaves a small window where conflicting locks can
           be granted to different clients. If client1 requests a blocking
           lock on the src file, it will be granted after the migrating
           process releases its lock. If client2 requests a lock on the dst
           data file, it will also be granted, but all FOPs will be redirected
           to the dst data file.
        */

        /* Take meta lock  */

        if (defrag->lock_migration_enabled) {
                meta_dict = dict_new ();
                if (!meta_dict) {
                        gf_msg (this->name, GF_LOG_ERROR, 0,
                                DHT_MSG_MIGRATE_FILE_FAILED,
                                "Trace dict_new failed");

                        ret = -1;
                        goto out;
                }

                ret = dict_set_str (meta_dict, GLUSTERFS_INTERNAL_FOP_KEY, "yes");
                if (ret) {
                        gf_msg (this->name, GF_LOG_ERROR, 0,
                                DHT_MSG_DICT_SET_FAILED,
                                "Failed to set dictionary value: key = %s,"
                                " path = %s", GLUSTERFS_INTERNAL_FOP_KEY,
                                 loc->path);
                        ret = -1;
                        goto out;
                }

                ret = dict_set_int32 (meta_dict, GF_META_LOCK_KEY, 1);
                if (ret) {
                        gf_msg (this->name, GF_LOG_ERROR, 0,
                                DHT_MSG_MIGRATE_FILE_FAILED,
                                "Trace dict_set failed");

                        ret = -1;
                        goto out;
                }

                ret = syncop_setxattr (from, loc, meta_dict, 0, NULL, NULL);
                if (ret) {
                        gf_msg (this->name, GF_LOG_ERROR, 0,
                                DHT_MSG_MIGRATE_FILE_FAILED,
                                "Trace syncop_setxattr metalock failed");

                        ret = -1;
                        goto out;
                } else {
                        meta_locked = _gf_true;
                }
        }

        if (!defrag->lock_migration_enabled) {
                plock.l_type = F_WRLCK;
                plock.l_start = 0;
                plock.l_len = 0;
                plock.l_whence = SEEK_SET;

                ret = syncop_lk (from, src_fd, F_SETLK, &plock, NULL, NULL);
                if (ret) {
                        gf_msg (this->name, GF_LOG_ERROR, -ret,
                                DHT_MSG_MIGRATE_FILE_FAILED,
                                "Migrate file failed:"
                                "%s: Failed to lock on %s",
                                loc->path, from->name);
                        ret = -1;
                        goto out;
                }

                p_locked = _gf_true;

        } else {

                INIT_LIST_HEAD (&locklist.list);

                ret = syncop_getactivelk (from, loc, &locklist, NULL, NULL);
                if (ret == 0) {
                        gf_log (this->name, GF_LOG_INFO, "No active locks on:%s"
                                , loc->path);

                } else if (ret > 0) {

                        ret = syncop_setactivelk (to, loc, &locklist, NULL,
                                                  NULL);
                        if (ret) {
                                gf_msg (this->name, GF_LOG_ERROR, 0,
                                        DHT_MSG_LOCK_MIGRATION_FAILED,
                                        "write lock failed on:%s", loc->path);

                                ret = -1;
                                goto metaunlock;
                        }
                } else {
                        gf_msg (this->name, GF_LOG_ERROR, 0,
                                DHT_MSG_LOCK_MIGRATION_FAILED,
                                "getactivelk failed for file: %s", loc->path);
                }
        }


        /* source would have both sticky bit and sgid bit set, reset it to 0,
           and set the source permission on destination, if it was not set
           prior to setting rebalance-modes in source  */
        if (!src_ia_prot.sticky)
                new_stbuf.ia_prot.sticky = 0;

        if (!src_ia_prot.sgid)
                new_stbuf.ia_prot.sgid = 0;

        /* TODO: if the source actually had sticky bit, or sgid bit set,
           we are not handling it */

        ret = syncop_fsetattr (to, dst_fd, &new_stbuf,
                               (GF_SET_ATTR_UID | GF_SET_ATTR_GID |
                                GF_SET_ATTR_MODE), NULL, NULL, NULL, NULL);
        if (ret) {
                gf_msg (this->name, GF_LOG_WARNING, -ret,
                        DHT_MSG_MIGRATE_FILE_FAILED,
                        "Migrate file failed:"
                        "%s: failed to perform setattr on %s ",
                        loc->path, to->name);
                ret = -1;
                goto metaunlock;
        }

        /* Because 'futimes' is not portable */
        ret = syncop_setattr (to, loc, &new_stbuf,
                              (GF_SET_ATTR_MTIME | GF_SET_ATTR_ATIME),
                              NULL, NULL, NULL, NULL);
        if (ret) {
                gf_log (this->name, GF_LOG_WARNING,
                        "%s: failed to perform setattr on %s ",
                        loc->path, to->name);
                ret = -1;
        }


        clean_dst = _gf_false;

        /* Posix acls are not set on DHT linkto files as part of the initial
         * initial xattrs set on the dst file, so these need
         * to be set on the dst file after the linkto attrs are removed.
         * TODO: Optimize this.
         */
        if (xattr) {
                dict_unref (xattr);
                xattr = NULL;
        }

        ret = syncop_listxattr (from, loc, &xattr, NULL, NULL);
        if (ret < 0) {
                gf_msg (this->name, GF_LOG_WARNING, 0,
                        DHT_MSG_MIGRATE_FILE_FAILED,
                        "Migrate file failed:"
                        "%s: failed to get xattr from %s (%s)",
                        loc->path, from->name, strerror (-ret));
                ret = -1;
        } else {
                ret = syncop_setxattr (to, loc, xattr, 0, NULL, NULL);
                if (ret < 0) {
                        /* Potential problem here where Posix ACLs will
                         * not be set on the target file */

                        gf_msg (this->name, GF_LOG_WARNING, 0,
                                DHT_MSG_MIGRATE_FILE_FAILED,
                                "Migrate file failed:"
                                "%s: failed to set xattr on %s (%s)",
                                loc->path, to->name, strerror (-ret));
                        ret = -1;
                }
        }

        /* store size of previous migrated file  */
        if (defrag->tier_conf.is_tier) {
                if (from != TIER_HASHED_SUBVOL) {
                        defrag->tier_conf.st_last_promoted_size = stbuf.ia_size;
                } else {
                        /* Don't delete the linkto file on the hashed subvol */
                        delete_src_linkto = _gf_false;
                        defrag->tier_conf.st_last_demoted_size = stbuf.ia_size;
                }
        }

        /* The src file is being unlinked after this so we don't need
           to clean it up */
        clean_src = _gf_false;

        /* Make the source as a linkfile first before deleting it */
        empty_iatt.ia_prot.sticky = 1;
        ret = syncop_fsetattr (from, src_fd, &empty_iatt,
                               GF_SET_ATTR_MODE, NULL, NULL, NULL, NULL);
        if (ret) {
                gf_msg (this->name, GF_LOG_WARNING, -ret,
                        DHT_MSG_MIGRATE_FILE_FAILED,
                        "Migrate file failed:"
                        "%s: failed to perform setattr on %s ",
                        loc->path, from->name);
                ret = -1;
                goto metaunlock;
        }

       /* Free up the data blocks on the source node, as the whole
           file is migrated */
        ret = syncop_ftruncate (from, src_fd, 0, NULL, NULL);
        if (ret) {
                gf_log (this->name, GF_LOG_WARNING,
                        "%s: failed to perform truncate on %s (%s)",
                        loc->path, from->name, strerror (-ret));
                ret = -1;
        }

        /* remove the 'linkto' xattr from the destination */
        ret = syncop_fremovexattr (to, dst_fd, conf->link_xattr_name, 0, NULL);
        if (ret) {
                gf_log (this->name, GF_LOG_WARNING,
                        "%s: failed to perform removexattr on %s (%s)",
                        loc->path, to->name, strerror (-ret));
                ret = -1;
        }

        /* Do a stat and check the gfid before unlink */

        /*
         * Cached file changes its state from non-linkto to linkto file after
         * migrating data. If lookup from any other mount-point is performed,
         * converted-linkto-cached file will be treated as a stale and will be
         * unlinked. But by this time, file is already migrated. So further
         * failure because of ENOENT should  not be treated as error
         */

        ret = syncop_stat (from, loc, &empty_iatt, NULL, NULL);
        if (ret) {
                gf_msg (this->name, GF_LOG_WARNING, 0,
                        DHT_MSG_MIGRATE_FILE_FAILED,
                        "%s: failed to do a stat on %s (%s)",
                        loc->path, from->name, strerror (-ret));

                if (-ret != ENOENT) {
                        ret = -1;
                        goto metaunlock;
                }

                rcvd_enoent_from_src = 1;
        }


        if ((gf_uuid_compare (empty_iatt.ia_gfid, loc->gfid) == 0 ) &&
            (!rcvd_enoent_from_src) && delete_src_linkto) {
                /* take out the source from namespace */
                ret = syncop_unlink (from, loc, NULL, NULL);
                if (ret) {
                        gf_msg (this->name, GF_LOG_WARNING, 0,
                                DHT_MSG_MIGRATE_FILE_FAILED,
                                "%s: failed to perform unlink on %s (%s)",
                                loc->path, from->name, strerror (-ret));
                        ret = -1;
                        goto metaunlock;
                }
        }

        ret = syncop_lookup (this, loc, NULL, NULL, NULL, NULL);
        if (ret) {
                gf_msg_debug (this->name, 0,
                              "%s: failed to lookup the file on subvolumes (%s)",
                              loc->path, strerror (-ret));
                ret = -1;
        }

        gf_msg (this->name, log_level, 0,
                DHT_MSG_MIGRATE_FILE_COMPLETE,
                "completed migration of %s from subvolume %s to %s",
                loc->path, from->name, to->name);

        ret = 0;

metaunlock:

        if (defrag->lock_migration_enabled && meta_locked) {

                dict_del (meta_dict, GF_META_LOCK_KEY);

                ret = dict_set_int32 (meta_dict, GF_META_UNLOCK_KEY, 1);
                if (ret) {
                        gf_msg (this->name, GF_LOG_ERROR, 0,
                                DHT_MSG_MIGRATE_FILE_FAILED,
                                "Trace dict_set failed");

                        ret = -1;
                        goto out;
                }

                if (clean_dst == _gf_false)
                        ret = dict_set_int32 (meta_dict, "status", 1);
                else
                        ret = dict_set_int32 (meta_dict, "status", 0);

                if (ret) {
                        gf_msg (this->name, GF_LOG_ERROR, 0,
                                DHT_MSG_MIGRATE_FILE_FAILED,
                                "Trace dict_set failed");

                        ret = -1;
                        goto out;
                }

                ret = syncop_setxattr (from, loc, meta_dict, 0, NULL, NULL);
                if (ret) {
                        gf_msg (this->name, GF_LOG_ERROR, 0,
                                DHT_MSG_MIGRATE_FILE_FAILED,
                                "Trace syncop_setxattr meta unlock failed");

                        ret = -1;
                        goto out;
                }
        }

out:
        if (clean_src) {
                /* Revert source mode and xattr changes*/
                lk_ret = __dht_migration_cleanup_src_file (this, loc, src_fd,
                                                        from, &src_ia_prot);
                if (lk_ret) {
                        gf_msg (this->name, GF_LOG_WARNING, 0,
                                DHT_MSG_MIGRATE_FILE_FAILED,
                                "%s: failed to cleanup source file on %s",
                                loc->path, from->name);
                }
        }

        /* reset the destination back to 0 */
        if (clean_dst) {
                lk_ret = syncop_ftruncate (to, dst_fd, 0, NULL, NULL);
                if (lk_ret) {
                        gf_msg (this->name, GF_LOG_ERROR, -lk_ret,
                                DHT_MSG_MIGRATE_FILE_FAILED,
                                "Migrate file failed: "
                                "%s: failed to reset target size back to 0",
                                loc->path);
                }
        }

        if (locked) {
                flock.l_type = F_UNLCK;

                lk_ret = syncop_inodelk (from, DHT_FILE_MIGRATE_DOMAIN,
                                         &tmp_loc, F_SETLK, &flock, NULL, NULL);
                if (lk_ret < 0) {
                        gf_msg (this->name, GF_LOG_WARNING, 0,
                                DHT_MSG_MIGRATE_FILE_FAILED,
                                "%s: failed to unlock file on %s (%s)",
                                loc->path, from->name, strerror (-lk_ret));
                }
        }

        if (p_locked) {
                plock.l_type = F_UNLCK;
                lk_ret = syncop_lk (from, src_fd, F_SETLK, &plock, NULL, NULL);

                if (lk_ret < 0) {
                        gf_msg (this->name, GF_LOG_WARNING, -lk_ret,
                                DHT_MSG_MIGRATE_FILE_FAILED,
                                "%s: failed to unlock file on %s",
                                loc->path, from->name);
                }
        }

        if (dict)
                dict_unref (dict);

        if (xattr)
                dict_unref (xattr);
        if (xattr_rsp)
                dict_unref (xattr_rsp);

        if (dst_fd)
                syncop_close (dst_fd);
        if (src_fd)
                syncop_close (src_fd);

        loc_wipe (&tmp_loc);

        return ret;
}

static int
rebalance_task (void *data)
{
        int           ret   = -1;
        dht_local_t  *local = NULL;
        call_frame_t *frame = NULL;

        frame = data;

        local = frame->local;

        /* This function is 'synchrounous', hence if it returns,
           we are done with the task */
        ret = dht_migrate_file (THIS, &local->loc, local->rebalance.from_subvol,
                                local->rebalance.target_node, local->flags);

        return ret;
}

static int
rebalance_task_completion (int op_ret, call_frame_t *sync_frame, void *data)
{
        int32_t       op_errno   = EINVAL;

        if (op_ret == -1) {
                /* Failure of migration process, mostly due to write process.
                   as we can't preserve the exact errno, lets say there was
                   no space to migrate-data
                */
                op_errno = ENOSPC;
        }

        if (op_ret == 1) {
                /* migration didn't happen, but is not a failure, let the user
                   understand that he doesn't have permission to migrate the
                   file.
                */
                op_ret = -1;
                op_errno = EPERM;
        }

        DHT_STACK_UNWIND (setxattr, sync_frame, op_ret, op_errno, NULL);
        return 0;
}

int
dht_start_rebalance_task (xlator_t *this, call_frame_t *frame)
{
        int         ret     = -1;

        ret = synctask_new (this->ctx->env, rebalance_task,
                            rebalance_task_completion,
                            frame, frame);
        return ret;
}

int
gf_listener_stop (xlator_t *this)
{
        glusterfs_ctx_t  *ctx = NULL;
        cmd_args_t       *cmd_args = NULL;
        int              ret = 0;

        ctx = this->ctx;
        GF_ASSERT (ctx);
        cmd_args = &ctx->cmd_args;
        if (cmd_args->sock_file) {
                ret = sys_unlink (cmd_args->sock_file);
                if (ret && (ENOENT == errno)) {
                        ret = 0;
                }
        }

        if (ret) {
                gf_msg (this->name, GF_LOG_ERROR, errno,
                        DHT_MSG_SOCKET_ERROR,
                        "Failed to unlink listener "
                        "socket %s", cmd_args->sock_file);
        }
        return ret;
}

void
dht_build_root_inode (xlator_t *this, inode_t **inode)
{
        inode_table_t    *itable        = NULL;
        uuid_t            root_gfid     = {0, };

        itable = inode_table_new (0, this);
        if (!itable)
                return;

        root_gfid[15] = 1;
        *inode = inode_find (itable, root_gfid);
}

void
dht_build_root_loc (inode_t *inode, loc_t *loc)
{
        loc->path = "/";
        loc->inode = inode;
        loc->inode->ia_type = IA_IFDIR;
        memset (loc->gfid, 0, 16);
        loc->gfid[15] = 1;
}

/* return values: 1 -> error, bug ignore and continue
                  0 -> proceed
                 -1 -> error, handle it */
int32_t
gf_defrag_handle_migrate_error (int32_t op_errno, gf_defrag_info_t *defrag)
{
        /* if errno is not ENOSPC or ENOTCONN, we can still continue
           with rebalance process */
        if ((op_errno != ENOSPC) || (op_errno != ENOTCONN))
                return 1;

        if (op_errno == ENOTCONN) {
                /* Most probably mount point went missing (mostly due
                   to a brick down), say rebalance failure to user,
                   let him restart it if everything is fine */
                defrag->defrag_status = GF_DEFRAG_STATUS_FAILED;
                return -1;
        }

        if (op_errno == ENOSPC) {
                /* rebalance process itself failed, may be
                   remote brick went down, or write failed due to
                   disk full etc etc.. */
                defrag->defrag_status = GF_DEFRAG_STATUS_FAILED;
                return -1;
        }

        return 0;
}

static gf_boolean_t
gf_defrag_pattern_match (gf_defrag_info_t *defrag, char *name, uint64_t size)
{
        gf_defrag_pattern_list_t *trav = NULL;
        gf_boolean_t               match = _gf_false;
        gf_boolean_t               ret = _gf_false;

        GF_VALIDATE_OR_GOTO ("dht", defrag, out);

        trav = defrag->defrag_pattern;
        while (trav) {
                if (!fnmatch (trav->path_pattern, name, FNM_NOESCAPE)) {
                        match = _gf_true;
                        break;
                }
                trav = trav->next;
        }

        if ((match == _gf_true) && (size >= trav->size))
                ret = _gf_true;

 out:
        return ret;
}

int dht_dfreaddirp_done (dht_dfoffset_ctx_t *offset_var, int cnt) {

        int i;
        int result = 1;

        for (i = 0; i < cnt; i++) {
                if (offset_var[i].readdir_done == 0) {
                        result = 0;
                        break;
                }
        }
        return result;
}

int static
gf_defrag_ctx_subvols_init (dht_dfoffset_ctx_t *offset_var, xlator_t *this) {

        int i;
        dht_conf_t *conf = NULL;

        conf = this->private;

        if (!conf)
               return -1;

        for (i = 0; i < conf->local_subvols_cnt; i++) {
               offset_var[i].this = conf->local_subvols[i];
               offset_var[i].offset = (off_t) 0;
               offset_var[i].readdir_done = 0;
        }

        return 0;
}

int
gf_defrag_migrate_single_file (void *opaque)
{
        xlator_t                *this = NULL;
        dht_conf_t              *conf = NULL;
        gf_defrag_info_t        *defrag = NULL;
        int                     ret = 0;
        gf_dirent_t             *entry          = NULL;
        struct timeval           start          = {0,};
        loc_t                    entry_loc      = {0,};
        loc_t                   *loc            = NULL;
        struct iatt              iatt           = {0,};
        dict_t                  *migrate_data   = NULL;
        int32_t                  op_errno       = 0;
        struct timeval           end            = {0,};
        double                   elapsed        = {0,};
        struct dht_container    *rebal_entry    = NULL;
        inode_t                 *inode          = NULL;

        rebal_entry = (struct dht_container *)opaque;
        if (!rebal_entry) {
                gf_log (this->name, GF_LOG_ERROR, "rebal_entry is NULL");
                ret = -1;
                goto out;
        }

        this = rebal_entry->this;

        conf = this->private;

        defrag = conf->defrag;

        loc = rebal_entry->parent_loc;

        migrate_data = rebal_entry->migrate_data;

        entry = rebal_entry->df_entry;

        if (defrag->defrag_status != GF_DEFRAG_STATUS_STARTED) {
                ret = -1;
                goto out;
        }

        if (defrag->stats == _gf_true) {
                gettimeofday (&start, NULL);
        }

        if (defrag->defrag_pattern &&
            (gf_defrag_pattern_match (defrag, entry->d_name,
                                      entry->d_stat.ia_size) == _gf_false)) {
                gf_log (this->name, GF_LOG_ERROR, "pattern_match failed");
                goto out;
        }

        memset (&entry_loc, 0, sizeof (entry_loc));

        ret = dht_build_child_loc (this, &entry_loc, loc, entry->d_name);
        if (ret) {
                LOCK (&defrag->lock);
                {
                        defrag->total_failures += 1;
                }
                UNLOCK (&defrag->lock);

                ret = 0;

                gf_log (this->name, GF_LOG_ERROR, "Child loc build failed");

                goto out;
        }

        gf_uuid_copy (entry_loc.gfid, entry->d_stat.ia_gfid);

        gf_uuid_copy (entry_loc.pargfid, loc->gfid);

        ret = syncop_lookup (this, &entry_loc, &iatt, NULL, NULL, NULL);
        if (ret) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        DHT_MSG_MIGRATE_FILE_FAILED,
                        "Migrate file failed: %s lookup failed",
                        entry_loc.name);
                ret = 0;
                goto out;
        }

        inode = inode_link (entry_loc.inode, entry_loc.parent, entry->d_name, &iatt);
        inode_unref (entry_loc.inode);
        /* use the inode returned by inode_link */
        entry_loc.inode = inode;

        ret = syncop_setxattr (this, &entry_loc, migrate_data, 0, NULL, NULL);
        if (ret < 0) {
                op_errno = -ret;
                /* errno is overloaded. See
                 * rebalance_task_completion () */
                if (op_errno == ENOSPC) {
                        gf_msg_debug (this->name, 0, "migrate-data skipped for"
                                      " %s due to space constraints",
                                      entry_loc.path);
                        LOCK (&defrag->lock);
                        {
                                defrag->skipped += 1;
                        }
                        UNLOCK (&defrag->lock);
                } else if (op_errno == ENOTSUP) {
                        gf_msg_debug (this->name, 0, "migrate-data skipped for"
                                      " hardlink %s ", entry_loc.path);
                        LOCK (&defrag->lock);
                        {
                                defrag->skipped += 1;
                        }
                        UNLOCK (&defrag->lock);
                } else if (op_errno != EEXIST) {
                        gf_msg (this->name, GF_LOG_ERROR, 0,
                                DHT_MSG_MIGRATE_FILE_FAILED,
                                "migrate-data failed for %s", entry_loc.path);

                        LOCK (&defrag->lock);
                        {
                                defrag->total_failures += 1;
                        }
                        UNLOCK (&defrag->lock);

                }

                ret = gf_defrag_handle_migrate_error (op_errno, defrag);

                if (!ret) {
                        gf_msg(this->name, GF_LOG_ERROR, 0,
                               DHT_MSG_MIGRATE_FILE_FAILED,
                               "migrate-data on %s failed: %s", entry_loc.path,
                               strerror (op_errno));
                } else if (ret == 1) {
                        ret = 0;
                        goto out;
                } else if (ret == -1) {
                        goto out;
                }
        } else if (ret > 0) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        DHT_MSG_MIGRATE_FILE_FAILED,
                        "migrate-data failed for %s", entry_loc.path);
                ret = 0;
                LOCK (&defrag->lock);
                {
                        defrag->total_failures += 1;
                }
                UNLOCK (&defrag->lock);
        }

        LOCK (&defrag->lock);
        {
                defrag->total_files += 1;
                defrag->total_data += iatt.ia_size;
        }
        UNLOCK (&defrag->lock);

        if (defrag->stats == _gf_true) {
                gettimeofday (&end, NULL);
                elapsed = (end.tv_sec - start.tv_sec) * 1e6 +
                          (end.tv_usec - start.tv_usec);
                gf_log (this->name, GF_LOG_INFO, "Migration of "
                        "file:%s size:%"PRIu64" bytes took %.2f"
                        "secs and ret: %d", entry_loc.name,
                        iatt.ia_size, elapsed/1e6, ret);
        }

out:
        loc_wipe (&entry_loc);

        return ret;

}

void *
gf_defrag_task (void *opaque)
{
        struct list_head        *q_head         = NULL;
        struct dht_container    *iterator       = NULL;
        gf_defrag_info_t        *defrag         = NULL;
        int                      ret            = 0;


        defrag = (gf_defrag_info_t *)opaque;
        if (!defrag) {
                gf_msg ("dht", GF_LOG_ERROR, 0, 0, "defrag is NULL");
                goto out;
        }

        q_head = &(defrag->queue[0].list);

       /* The following while loop will dequeue one entry from the defrag->queue
          under lock. We will update the defrag->global_error only when there
          is an error which is critical to stop the rebalance process. The stop
          message will be intimated to other migrator threads by setting the
          defrag->defrag_status to GF_DEFRAG_STATUS_FAILED.

          In defrag->queue, a low watermark (MIN_MIGRATE_QUEUE_COUNT) is
          maintained so that crawler does not starve the file migration
          workers and a high watermark (MAX_MIGRATE_QUEUE_COUNT) so that
          crawler does not go far ahead in filling up the queue.
        */

        while (_gf_true) {

                if (defrag->defrag_status != GF_DEFRAG_STATUS_STARTED) {
                        pthread_cond_broadcast (
                                &defrag->rebalance_crawler_alarm);
                        pthread_cond_broadcast (
                                &defrag->parallel_migration_cond);
                        goto out;
                }

                pthread_mutex_lock (&defrag->dfq_mutex);
                {

                        /*Throttle down:
                          If the reconfigured count is less than current thread
                          count, then the current thread will sleep */

                        /*TODO: Need to refactor the following block to work
                         *under defrag->lock. For now access
                         * defrag->current_thread_count and rthcount under
                         * dfq_mutex lock */
                        while (!defrag->crawl_done &&
                              (defrag->recon_thread_count <
                                        defrag->current_thread_count)) {
                                defrag->current_thread_count--;
                                gf_log ("DHT", GF_LOG_INFO,
                                        "Thread sleeping. "
                                        "defrag->current_thread_count: %d",
                                         defrag->current_thread_count);

                                pthread_cond_wait (
                                           &defrag->df_wakeup_thread,
                                           &defrag->dfq_mutex);

                                defrag->current_thread_count++;

                                gf_log ("DHT", GF_LOG_INFO,
                                        "Thread wokeup. "
                                        "defrag->current_thread_count: %d",
                                         defrag->current_thread_count);
                        }

                        if (defrag->q_entry_count) {
                                iterator = list_entry (q_head->next,
                                                typeof(*iterator), list);

                                gf_msg_debug ("DHT", 0, "picking entry "
                                              "%s", iterator->df_entry->d_name);

                                list_del_init (&(iterator->list));

                                defrag->q_entry_count--;

                                if ((defrag->q_entry_count <
                                        MIN_MIGRATE_QUEUE_COUNT) &&
                                        defrag->wakeup_crawler) {
                                        pthread_cond_broadcast (
                                              &defrag->rebalance_crawler_alarm);
                                }
                                pthread_mutex_unlock (&defrag->dfq_mutex);
                                ret = gf_defrag_migrate_single_file
                                                        ((void *)iterator);

                                /*Critical errors: ENOTCONN and ENOSPACE*/
                                if (ret) {
                                        dht_set_global_defrag_error
                                                         (defrag, ret);

                                        defrag->defrag_status =
                                                       GF_DEFRAG_STATUS_FAILED;

                                        pthread_cond_broadcast (
                                             &defrag->rebalance_crawler_alarm);

                                        pthread_cond_broadcast (
                                             &defrag->parallel_migration_cond);

                                        goto out;
                                }

                                gf_defrag_free_container (iterator);

                                continue;
                        } else {

                        /* defrag->crawl_done flag is set means crawling
                         file system is done and hence a list_empty when
                         the above flag is set indicates there are no more
                         entries to be added to the queue and rebalance is
                         finished */

                                if (!defrag->crawl_done) {
                                        pthread_cond_wait (
                                           &defrag->parallel_migration_cond,
                                           &defrag->dfq_mutex);
                                }

                                if (defrag->crawl_done &&
                                                 !defrag->q_entry_count) {
                                        pthread_cond_broadcast (
                                             &defrag->parallel_migration_cond);
                                        goto unlock;
                                } else {
                                        pthread_mutex_unlock
                                                 (&defrag->dfq_mutex);
                                        continue;
                                }
                        }

                }
unlock:
                pthread_mutex_unlock (&defrag->dfq_mutex);
                break;
        }
out:
        return NULL;
}

int static
gf_defrag_get_entry (xlator_t *this, int i, struct dht_container **container,
                     loc_t *loc, dht_conf_t *conf, gf_defrag_info_t *defrag,
                     fd_t *fd, dict_t *migrate_data,
                     struct dir_dfmeta *dir_dfmeta, dict_t *xattr_req,
                     int *should_commit_hash, int *perrno)
{
        int                     ret             = -1;
        char                    is_linkfile     = 0;
        gf_dirent_t            *df_entry        = NULL;
        loc_t                   entry_loc       = {0,};
        dict_t                 *xattr_rsp       = NULL;
        struct iatt             iatt            = {0,};
        struct dht_container   *tmp_container   = NULL;
        xlator_t               *hashed_subvol   = NULL;
        xlator_t               *cached_subvol   = NULL;

        if (defrag->defrag_status != GF_DEFRAG_STATUS_STARTED) {
                ret = -1;
                goto out;
        }

        if (dir_dfmeta->offset_var[i].readdir_done == 1) {
                ret = 0;
                goto out;
        }

        if (dir_dfmeta->fetch_entries[i] == 1) {
                ret = syncop_readdirp (conf->local_subvols[i], fd, 131072,
                                       dir_dfmeta->offset_var[i].offset,
                                       &(dir_dfmeta->equeue[i]),
                                       NULL, NULL);
                if (ret == 0) {
                        dir_dfmeta->offset_var[i].readdir_done = 1;
                        ret = 0;
                        goto out;
                }

                if (ret < 0) {
                        gf_msg (this->name, GF_LOG_WARNING, -ret,
                                DHT_MSG_MIGRATE_DATA_FAILED,
                                "Readdirp failed. Aborting data migration for "
                                "directory: %s", loc->path);
                        *perrno = -ret;
                        ret = -1;
                        goto out;
                }

                if (list_empty (&(dir_dfmeta->equeue[i].list))) {
                        dir_dfmeta->offset_var[i].readdir_done = 1;
                        ret = 0;
                        goto out;
                }

                dir_dfmeta->fetch_entries[i] = 0;
        }

        while (1) {

                if (defrag->defrag_status != GF_DEFRAG_STATUS_STARTED) {
                        ret = -1;
                        goto out;
                }

                df_entry = list_entry (dir_dfmeta->iterator[i]->next,
                                       typeof (*df_entry), list);

                if (&df_entry->list == dir_dfmeta->head[i]) {
                        gf_dirent_free (&(dir_dfmeta->equeue[i]));
                        INIT_LIST_HEAD (&(dir_dfmeta->equeue[i].list));
                        dir_dfmeta->fetch_entries[i] = 1;
                        dir_dfmeta->iterator[i] = dir_dfmeta->head[i];
                        ret = 0;
                        goto out;
                }

                dir_dfmeta->iterator[i] = dir_dfmeta->iterator[i]->next;

                dir_dfmeta->offset_var[i].offset = df_entry->d_off;
                if (!strcmp (df_entry->d_name, ".") ||
                    !strcmp (df_entry->d_name, ".."))
                        continue;

                if (IA_ISDIR (df_entry->d_stat.ia_type))
                        continue;

                defrag->num_files_lookedup++;

                if (defrag->defrag_pattern &&
                    (gf_defrag_pattern_match (defrag, df_entry->d_name,
                                              df_entry->d_stat.ia_size)
                     == _gf_false)) {
                        continue;
                }

                loc_wipe (&entry_loc);
                ret = dht_build_child_loc (this, &entry_loc, loc,
                                          df_entry->d_name);
                if (ret) {
                        gf_log (this->name, GF_LOG_ERROR, "Child loc"
                                " build failed");
                        ret = -1;
                        goto out;
                }

                if (gf_uuid_is_null (df_entry->d_stat.ia_gfid)) {
                        gf_msg (this->name, GF_LOG_ERROR, 0,
                                DHT_MSG_GFID_NULL,
                                "%s/%s gfid not present", loc->path,
                                df_entry->d_name);
                        continue;
                }

                gf_uuid_copy (entry_loc.gfid, df_entry->d_stat.ia_gfid);

                if (gf_uuid_is_null (loc->gfid)) {
                        gf_msg (this->name, GF_LOG_ERROR, 0,
                                DHT_MSG_GFID_NULL,
                                "%s/%s gfid not present", loc->path,
                                df_entry->d_name);
                        continue;
                }

                gf_uuid_copy (entry_loc.pargfid, loc->gfid);

                entry_loc.inode->ia_type = df_entry->d_stat.ia_type;

                if (xattr_rsp) {
                        dict_unref (xattr_rsp);
                        xattr_rsp = NULL;
                }

                ret = syncop_lookup (conf->local_subvols[i], &entry_loc,
                                        &iatt, NULL, xattr_req, &xattr_rsp);
                if (ret) {
                        gf_msg (this->name, GF_LOG_ERROR, 0,
                                DHT_MSG_MIGRATE_FILE_FAILED,
                                "Migrate file failed:%s lookup failed",
                                entry_loc.path);

                        if (-ret != ENOENT && -ret != ESTALE) {

                                defrag->total_failures++;

                                if (conf->decommission_in_progress) {
                                        ret = -1;
                                        goto out;
                                } else {
                                       *should_commit_hash = 0;
                                        continue;
                                }
                        }

                        continue;
                }


                is_linkfile = check_is_linkfile (NULL, &iatt, xattr_rsp,
                                                conf->link_xattr_name);

                if (is_linkfile) {
                        /* No need to add linkto file to the queue for
                           migration. Only the actual data file need to
                           be checked for migration criteria.
                        */
                        gf_msg_debug (this->name, 0, "Skipping linkfile"
                                      " %s on subvol: %s", entry_loc.path,
                                      conf->local_subvols[i]->name);
                        continue;
                }


                ret = syncop_lookup (this, &entry_loc, NULL, NULL,
                                     NULL, NULL);
                if (ret) {
                        gf_msg (this->name, GF_LOG_WARNING, -ret,
                                DHT_MSG_MIGRATE_FILE_FAILED,
                                "lookup failed for file:%s",
                                entry_loc.path);

                        if (-ret != ENOENT && -ret != ESTALE) {

                                defrag->total_failures++;

                                if (conf->decommission_in_progress) {
                                        ret = -1;
                                        goto out;
                                } else {
                                        *should_commit_hash = 0;
                                        continue;
                                }
                        }

                        continue;
                }

               /* if distribute is present, it will honor this key.
                * -1, ENODATA is returned if distribute is not present
                * or file doesn't have a link-file. If file has
                * link-file, the path of link-file will be the value,
                * and also that guarantees that file has to be mostly
                * migrated */

                hashed_subvol = dht_subvol_get_hashed (this, &entry_loc);
                if (!hashed_subvol) {
                        gf_msg (this->name, GF_LOG_ERROR, 0,
                                DHT_MSG_HASHED_SUBVOL_GET_FAILED,
                                "Failed to get hashed subvol for %s",
                                loc->path);
                        continue;
                }

                cached_subvol = dht_subvol_get_cached (this, entry_loc.inode);
                if (!cached_subvol) {
                        gf_msg (this->name, GF_LOG_ERROR, 0,
                                DHT_MSG_CACHED_SUBVOL_GET_FAILED,
                                "Failed to get cached subvol for %s",
                                loc->path);

                        continue;
                }

                if (hashed_subvol == cached_subvol) {
                        continue;
                }

               /*Build Container Structure */

                tmp_container =  GF_CALLOC (1, sizeof(struct dht_container),
                                            gf_dht_mt_container_t);
                if (!tmp_container) {
                        gf_log (this->name, GF_LOG_ERROR, "Failed to allocate "
                                "memory for container");
                        ret = -1;
                        goto out;
                }
                tmp_container->df_entry = gf_dirent_for_name (df_entry->d_name);
                if (!tmp_container->df_entry) {
                        gf_log (this->name, GF_LOG_ERROR, "Failed to allocate "
                                "memory for df_entry");
                        ret = -1;
                        goto out;
                }

                tmp_container->df_entry->d_stat = df_entry->d_stat;

                tmp_container->df_entry->d_ino  = df_entry->d_ino;

                tmp_container->df_entry->d_type = df_entry->d_type;

                tmp_container->df_entry->d_len  = df_entry->d_len;

                tmp_container->parent_loc = GF_CALLOC(1, sizeof(*loc),
                                                      gf_dht_mt_loc_t);
                if (!tmp_container->parent_loc) {
                        gf_log (this->name, GF_LOG_ERROR, "Failed to allocate "
                                "memory for loc");
                        ret = -1;
                        goto out;
                }


                ret = loc_copy (tmp_container->parent_loc, loc);
                if (ret) {
                        gf_log (this->name, GF_LOG_ERROR,
                                "loc_copy failed");
                        ret = -1;
                        goto out;
                }

                tmp_container->migrate_data = migrate_data;

                tmp_container->this = this;

                if (df_entry->dict)
                        tmp_container->df_entry->dict =
                                        dict_ref (df_entry->dict);

               /*Build Container Structue >> END*/

                ret = 0;
                goto out;

        }

out:
        loc_wipe (&entry_loc);

        if (ret == 0) {
                *container = tmp_container;
        } else {
                if (tmp_container) {
                        gf_defrag_free_container (tmp_container);
                }
        }

        if (xattr_rsp)
                dict_unref (xattr_rsp);


        return ret;
}

int
gf_defrag_process_dir (xlator_t *this, gf_defrag_info_t *defrag, loc_t *loc,
                       dict_t *migrate_data, int *perrno)
{
        int                      ret               = -1;
        fd_t                    *fd                = NULL;
        dht_conf_t              *conf              = NULL;
        gf_dirent_t              entries;
        dict_t                  *dict              = NULL;
        dict_t                  *xattr_req         = NULL;
        struct timeval           dir_start         = {0,};
        struct timeval           end               = {0,};
        double                   elapsed           = {0,};
        int                      local_subvols_cnt = 0;
        int                      i                 = 0;
        int                      j                 = 0;
        struct  dht_container   *container         = NULL;
        int                      ldfq_count        = 0;
        int                      dfc_index         = 0;
        int                      throttle_up       = 0;
        struct dir_dfmeta       *dir_dfmeta        = NULL;
        int                      should_commit_hash = 1;

        gf_log (this->name, GF_LOG_INFO, "migrate data called on %s",
                loc->path);
        gettimeofday (&dir_start, NULL);

        conf = this->private;
        local_subvols_cnt = conf->local_subvols_cnt;

        if (!local_subvols_cnt) {
                ret = 0;
                goto out;
        }

        fd = fd_create (loc->inode, defrag->pid);
        if (!fd) {
                gf_log (this->name, GF_LOG_ERROR, "Failed to create fd");
                ret = -1;
                goto out;
        }

        ret = syncop_opendir (this, loc, fd, NULL, NULL);
        if (ret) {
                gf_msg (this->name, GF_LOG_WARNING, 0,
                        DHT_MSG_MIGRATE_DATA_FAILED,
                        "Migrate data failed: Failed to open dir %s",
                        loc->path);
                *perrno = -ret;
                ret = -1;
                goto out;
        }

        fd_bind (fd);
        dir_dfmeta = GF_CALLOC (1, sizeof (*dir_dfmeta),
                                                gf_common_mt_pointer);
        if (!dir_dfmeta) {
                gf_log (this->name, GF_LOG_ERROR, "dir_dfmeta is NULL");
                ret = -1;
                goto out;
        }


        dir_dfmeta->head = GF_CALLOC (local_subvols_cnt,
                                      sizeof (*(dir_dfmeta->head)),
                                      gf_common_mt_pointer);
        if (!dir_dfmeta->head) {
                gf_log (this->name, GF_LOG_ERROR, "dir_dfmeta->head is NULL");
                ret = -1;
                goto out;
        }

        dir_dfmeta->iterator = GF_CALLOC (local_subvols_cnt,
                                          sizeof (*(dir_dfmeta->iterator)),
                                          gf_common_mt_pointer);
        if (!dir_dfmeta->iterator) {
                gf_log (this->name, GF_LOG_ERROR,
                        "dir_dfmeta->iterator is NULL");
                ret = -1;
                goto out;
        }

        dir_dfmeta->equeue = GF_CALLOC (local_subvols_cnt, sizeof (entries),
                                        gf_dht_mt_dirent_t);
        if (!dir_dfmeta->equeue) {
                gf_log (this->name, GF_LOG_ERROR, "dir_dfmeta->equeue is NULL");
                ret = -1;
                goto out;
        }

        dir_dfmeta->offset_var = GF_CALLOC (local_subvols_cnt,
                                            sizeof (dht_dfoffset_ctx_t),
                                            gf_dht_mt_octx_t);
        if (!dir_dfmeta->offset_var) {
                gf_log (this->name, GF_LOG_ERROR,
                        "dir_dfmeta->offset_var is NULL");
                ret = -1;
                goto out;
        }
        ret = gf_defrag_ctx_subvols_init (dir_dfmeta->offset_var, this);
        if (ret) {
                gf_log (this->name, GF_LOG_ERROR, "dht_dfoffset_ctx_t"
                        "initialization failed");
                ret = -1;
                goto out;
        }

        dir_dfmeta->fetch_entries = GF_CALLOC (local_subvols_cnt,
                                               sizeof (int), gf_common_mt_int);
        if (!dir_dfmeta->fetch_entries) {
                gf_log (this->name, GF_LOG_ERROR,
                        "dir_dfmeta->fetch_entries is NULL");
                ret = -1;
                goto out;
        }

        for (i = 0; i < local_subvols_cnt ; i++) {
                INIT_LIST_HEAD (&(dir_dfmeta->equeue[i].list));
                dir_dfmeta->head[i]          = &(dir_dfmeta->equeue[i].list);
                dir_dfmeta->iterator[i]      = dir_dfmeta->head[i];
                dir_dfmeta->fetch_entries[i] = 1;
        }

        xattr_req = dict_new ();
        if (!xattr_req) {
               gf_log (this->name, GF_LOG_ERROR, "dict_new failed");
               ret = -1;
               goto out;
        }

        ret = dict_set_uint32 (xattr_req,
                               conf->link_xattr_name, 256);
        if (ret) {
               gf_log (this->name, GF_LOG_ERROR, "failed to set dict for "
                       "key: %s", conf->link_xattr_name);
               ret = -1;
               goto out;
        }

        /*
         Job: Read entries from each local subvol and store the entries
              in equeue array of linked list. Now pick one entry from the
              equeue array in a round robin basis and add them to defrag Queue.
        */

        while (!dht_dfreaddirp_done(dir_dfmeta->offset_var,
                                         local_subvols_cnt)) {

                pthread_mutex_lock (&defrag->dfq_mutex);
                {

                      /*Throttle up: If reconfigured count is higher than
                        current thread count, wake up the sleeping threads
                        TODO: Need to refactor this. Instead of making the
                        thread sleep and wake, we should terminate and spawn
                        threads on-demand*/

                        if (defrag->recon_thread_count >
                                         defrag->current_thread_count) {
                                throttle_up =
                                        (defrag->recon_thread_count -
                                            defrag->current_thread_count);
                                for (j = 0; j < throttle_up; j++) {
                                        pthread_cond_signal (
                                             &defrag->df_wakeup_thread);
                                }

                        }

                        while (defrag->q_entry_count >
                                        MAX_MIGRATE_QUEUE_COUNT) {
                                defrag->wakeup_crawler = 1;
                                pthread_cond_wait (
                                        &defrag->rebalance_crawler_alarm,
                                        &defrag->dfq_mutex);
                        }

                       ldfq_count = defrag->q_entry_count;

                       if (defrag->wakeup_crawler) {
                               defrag->wakeup_crawler = 0;
                       }

                }
                pthread_mutex_unlock (&defrag->dfq_mutex);

                while (ldfq_count <= MAX_MIGRATE_QUEUE_COUNT &&
                       !dht_dfreaddirp_done(dir_dfmeta->offset_var,
                                               local_subvols_cnt)) {

                        ret = gf_defrag_get_entry (this, dfc_index, &container,
                                                   loc, conf, defrag, fd,
                                                   migrate_data, dir_dfmeta,
                                                   xattr_req,
                                                   &should_commit_hash, perrno);

                        if (ret) {
                                gf_log (this->name, GF_LOG_WARNING, "Found "
                                        "error from gf_defrag_get_entry");

                                ret = -1;
                                goto out;
                        }

                        /* Check if we got an entry, else we need to move the
                           index to the next subvol */
                        if (!container) {
                                GF_CRAWL_INDEX_MOVE(dfc_index,
                                                    local_subvols_cnt);
                                continue;
                        }

                        /* Q this entry in the dfq */
                        pthread_mutex_lock (&defrag->dfq_mutex);
                        {
                                list_add_tail (&container->list,
                                        &(defrag->queue[0].list));
                                defrag->q_entry_count++;
                                ldfq_count = defrag->q_entry_count;

                                gf_msg_debug (this->name, 0, "added "
                                              "file:%s parent:%s to the queue ",
                                              container->df_entry->d_name,
                                              container->parent_loc->path);

                                pthread_cond_signal (
                                        &defrag->parallel_migration_cond);
                        }
                        pthread_mutex_unlock (&defrag->dfq_mutex);

                        GF_CRAWL_INDEX_MOVE(dfc_index, local_subvols_cnt);
                }
        }

        gettimeofday (&end, NULL);
        elapsed = (end.tv_sec - dir_start.tv_sec) * 1e6 +
                  (end.tv_usec - dir_start.tv_usec);
        gf_log (this->name, GF_LOG_INFO, "Migration operation on dir %s took "
                "%.2f secs", loc->path, elapsed/1e6);
        ret = 0;
out:

        GF_FREE_DIR_DFMETA (dir_dfmeta);

        if (dict)
                dict_unref(dict);

        if (xattr_req)
                dict_unref(xattr_req);

        if (fd)
                fd_unref (fd);

        if (ret == 0 && should_commit_hash == 0) {
                ret = 2;
        }

        return ret;
}
int
gf_defrag_settle_hash (xlator_t *this, gf_defrag_info_t *defrag,
                       loc_t *loc, dict_t *fix_layout)
{
        int     ret;
        dht_conf_t *conf = NULL;
        /*
         * Now we're ready to update the directory commit hash for the volume
         * root, so that hash miscompares and broadcast lookups can stop.
         * However, we want to skip that if fix-layout is all we did.  In
         * that case, we want the miscompares etc. to continue until a real
         * rebalance is complete.
         */
        if (defrag->cmd == GF_DEFRAG_CMD_START_LAYOUT_FIX
            || defrag->cmd == GF_DEFRAG_CMD_START_DETACH_TIER ||
            defrag->cmd == GF_DEFRAG_CMD_DETACH_START) {
                return 0;
        }

        conf = this->private;
        if (!conf) {
                /*Uh oh
                 */
                return -1;
        }

        if (conf->local_subvols_cnt == 0 || !conf->lookup_optimize) {
                /* Commit hash updates are only done on local subvolumes and
                 * only when lookup optmization is needed (for older client
                 * support)
                 */
                return 0;
        }

        ret = dict_set_uint32 (fix_layout, "new-commit-hash",
                               defrag->new_commit_hash);
        if (ret) {
                gf_log (this->name, GF_LOG_ERROR,
                        "Failed to set new-commit-hash");
                return -1;
        }

        ret = syncop_setxattr (this, loc, fix_layout, 0, NULL, NULL);
        if (ret) {
                gf_log (this->name, GF_LOG_ERROR,
                        "fix layout on %s failed", loc->path);
                return -1;
        }

        /* TBD: find more efficient solution than adding/deleting every time */
        dict_del(fix_layout, "new-commit-hash");

        return 0;
}



/* Function for doing a named lookup on file inodes during an attach tier
 * So that a hardlink lookup heal i.e gfid to parent gfid lookup heal
 * happens on pre-existing data. This is required so that the ctr database has
 * hardlinks of all the exisitng file in the volume. CTR xlator on the
 * brick/server side does db update/insert of the hardlink on a namelookup.
 * Currently the namedlookup is done synchronous to the fixlayout that is
 * triggered by attach tier. This is not performant, adding more time to
 * fixlayout. The performant approach is record the hardlinks on a compressed
 * datastore and then do the namelookup asynchronously later, giving the ctr db
 * eventual consistency
 * */
int
gf_fix_layout_tier_attach_lookup (xlator_t *this,
                                 loc_t *parent_loc,
                                 gf_dirent_t *file_dentry)
{
        int                      ret            = -1;
        dict_t                  *lookup_xdata   = NULL;
        dht_conf_t              *conf           = NULL;
        loc_t                    file_loc       = {0,};
        struct iatt              iatt           = {0,};

        GF_VALIDATE_OR_GOTO ("tier", this, out);

        GF_VALIDATE_OR_GOTO (this->name, parent_loc, out);

        GF_VALIDATE_OR_GOTO (this->name, file_dentry, out);

        GF_VALIDATE_OR_GOTO (this->name, this->private, out);

        if (!parent_loc->inode) {
                gf_msg (this->name, GF_LOG_ERROR, 0, DHT_MSG_LOG_TIER_ERROR,
                        "%s/%s parent is NULL", parent_loc->path,
                        file_dentry->d_name);
                goto out;
        }


        conf   = this->private;

        loc_wipe (&file_loc);

        if (gf_uuid_is_null (file_dentry->d_stat.ia_gfid)) {
                gf_msg (this->name, GF_LOG_ERROR, 0, DHT_MSG_LOG_TIER_ERROR,
                        "%s/%s gfid not present", parent_loc->path,
                        file_dentry->d_name);
                goto out;
        }

        gf_uuid_copy (file_loc.gfid, file_dentry->d_stat.ia_gfid);

        if (gf_uuid_is_null (parent_loc->gfid)) {
                gf_msg (this->name, GF_LOG_ERROR, 0, DHT_MSG_LOG_TIER_ERROR,
                        "%s/%s"
                        " gfid not present", parent_loc->path,
                        file_dentry->d_name);
                goto out;
        }

        gf_uuid_copy (file_loc.pargfid, parent_loc->gfid);


        ret = dht_build_child_loc (this, &file_loc, parent_loc,
                                                file_dentry->d_name);
        if (ret) {
                gf_msg (this->name, GF_LOG_ERROR, 0, DHT_MSG_LOG_TIER_ERROR,
                        "Child loc build failed");
                ret = -1;
                goto out;
        }

        lookup_xdata = dict_new ();
        if (!lookup_xdata) {
                gf_msg (this->name, GF_LOG_ERROR, 0, DHT_MSG_LOG_TIER_ERROR,
                        "Failed creating lookup dict for %s",
                        file_dentry->d_name);
                goto out;
        }

        ret = dict_set_int32 (lookup_xdata, CTR_ATTACH_TIER_LOOKUP, 1);
        if (ret) {
                gf_msg (this->name, GF_LOG_ERROR, 0, DHT_MSG_LOG_TIER_ERROR,
                        "Failed to set lookup flag");
                goto out;
        }

        gf_uuid_copy (file_loc.parent->gfid, parent_loc->gfid);

        /* Sending lookup to cold tier only */
        ret = syncop_lookup (conf->subvolumes[0], &file_loc, &iatt,
                        NULL, lookup_xdata, NULL);
        if (ret) {
                /* If the file does not exist on the cold tier than it must */
                /* have been discovered on the hot tier. This is not an error. */
                gf_msg (this->name, GF_LOG_INFO, 0, DHT_MSG_LOG_TIER_STATUS,
                        "%s lookup to cold tier on attach heal failed", file_loc.path);
                goto out;
        }

        ret = 0;

out:

        loc_wipe (&file_loc);

        if (lookup_xdata)
                dict_unref (lookup_xdata);

        return ret;
}

int
gf_defrag_fix_layout (xlator_t *this, gf_defrag_info_t *defrag, loc_t *loc,
                  dict_t *fix_layout, dict_t *migrate_data)
{
        int                      ret            = -1;
        loc_t                    entry_loc      = {0,};
        fd_t                    *fd             = NULL;
        gf_dirent_t              entries;
        gf_dirent_t             *tmp            = NULL;
        gf_dirent_t             *entry          = NULL;
        gf_boolean_t             free_entries   = _gf_false;
        off_t                    offset         = 0;
        struct iatt              iatt           = {0,};
        inode_t                 *linked_inode   = NULL, *inode = NULL;
        dht_conf_t              *conf           = NULL;
        int                      should_commit_hash = 1;
        int                      perrno         = 0;

        conf = this->private;
        if (!conf) {
                ret = -1;
                goto out;
        }

        ret = syncop_lookup (this, loc, &iatt, NULL, NULL, NULL);
        if (ret) {
                if (strcmp (loc->path, "/") == 0) {
                        gf_msg (this->name, GF_LOG_ERROR, -ret,
                                DHT_MSG_DIR_LOOKUP_FAILED,
                                "lookup failed for:%s", loc->path);

                        defrag->total_failures++;
                        ret = -1;
                        goto out;
                }

                if (-ret == ENOENT || -ret == ESTALE) {
                        gf_msg (this->name, GF_LOG_INFO, -ret,
                                DHT_MSG_DIR_LOOKUP_FAILED,
                                "Dir:%s renamed or removed. Skipping",
                                loc->path);
                                ret = 0;
                                goto out;
                } else {
                        gf_msg (this->name, GF_LOG_ERROR, -ret,
                                DHT_MSG_DIR_LOOKUP_FAILED,
                                "lookup failed for:%s", loc->path);

                        defrag->total_failures++;
                        goto out;
                }
        }

        if ((defrag->cmd != GF_DEFRAG_CMD_START_TIER) &&
            (defrag->cmd != GF_DEFRAG_CMD_START_LAYOUT_FIX)) {
                ret = gf_defrag_process_dir (this, defrag, loc, migrate_data,
                                             &perrno);

                if (ret && (ret != 2)) {
                        if (perrno == ENOENT || perrno == ESTALE) {
                                ret = 0;
                                goto out;
                        } else {

                                defrag->total_failures++;

                                gf_msg (this->name, GF_LOG_ERROR, 0,
                                        DHT_MSG_DEFRAG_PROCESS_DIR_FAILED,
                                        "gf_defrag_process_dir failed for "
                                        "directory: %s", loc->path);

                                if (conf->decommission_in_progress) {
                                        goto out;
                                }

                                should_commit_hash = 0;
                        }
                } else if (ret == 2) {
                        should_commit_hash = 0;
                }
        }

        gf_msg_trace (this->name, 0, "fix layout called on %s", loc->path);

        fd = fd_create (loc->inode, defrag->pid);
        if (!fd) {
                gf_log (this->name, GF_LOG_ERROR, "Failed to create fd");
                ret = -1;
                goto out;
        }

        ret = syncop_opendir (this, loc, fd, NULL, NULL);
        if (ret) {
                if (-ret == ENOENT || -ret == ESTALE) {
                        ret = 0;
                        goto out;
                }

                gf_log (this->name, GF_LOG_ERROR, "Failed to open dir %s, "
                        "err:%d", loc->path, -ret);

                ret = -1;
                goto out;
        }

        fd_bind (fd);
        INIT_LIST_HEAD (&entries.list);
        while ((ret = syncop_readdirp (this, fd, 131072, offset, &entries,
                                       NULL, NULL)) != 0)
        {

                if (ret < 0) {
                        if (-ret == ENOENT || -ret == ESTALE) {
                                ret = 0;
                                goto out;
                        }

                        gf_msg (this->name, GF_LOG_ERROR, -ret,
                                DHT_MSG_READDIR_ERROR, "readdirp failed for "
                                "path %s. Aborting fix-layout", loc->path);

                        ret = -1;
                        goto out;
                }

                if (list_empty (&entries.list))
                        break;

                free_entries = _gf_true;

                list_for_each_entry_safe (entry, tmp, &entries.list, list) {
                        if (defrag->defrag_status != GF_DEFRAG_STATUS_STARTED) {
                                ret = 1;
                                goto out;
                        }

                        offset = entry->d_off;

                        if (!strcmp (entry->d_name, ".") ||
                            !strcmp (entry->d_name, ".."))
                                continue;
                        if (!IA_ISDIR (entry->d_stat.ia_type)) {

                                /* If its a fix layout during the attach
                                 * tier operation do lookups on files
                                 * on cold subvolume so that there is a
                                 * CTR DB Lookup Heal triggered on existing
                                 * data.
                                 * */
                                if (defrag->cmd == GF_DEFRAG_CMD_START_TIER) {
                                        gf_fix_layout_tier_attach_lookup
                                                (this, loc, entry);
                                }

                                continue;
                        }
                        loc_wipe (&entry_loc);

                        ret = dht_build_child_loc (this, &entry_loc, loc,
                                                  entry->d_name);
                        if (ret) {
                                gf_log (this->name, GF_LOG_ERROR, "Child loc"
                                        " build failed for entry: %s",
                                        entry->d_name);

                                if (conf->decommission_in_progress) {
                                        defrag->defrag_status =
                                        GF_DEFRAG_STATUS_FAILED;

                                        goto out;
                                } else {
                                        should_commit_hash = 0;

                                        continue;
                                }
                        }

                        if (gf_uuid_is_null (entry->d_stat.ia_gfid)) {
                                gf_log (this->name, GF_LOG_ERROR, "%s/%s"
                                        " gfid not present", loc->path,
                                         entry->d_name);
                                continue;
                        }


                        gf_uuid_copy (entry_loc.gfid, entry->d_stat.ia_gfid);

                        /*In case the gfid stored in the inode by inode_link
                         * and the gfid obtained in the lookup differs, then
                         * client3_3_lookup_cbk will return ESTALE and proper
                         * error will be captured
                         */

                        linked_inode = inode_link (entry_loc.inode, loc->inode,
                                                   entry->d_name,
                                                   &entry->d_stat);

                        inode = entry_loc.inode;
                        entry_loc.inode = linked_inode;
                        inode_unref (inode);

                        if (gf_uuid_is_null (loc->gfid)) {
                                gf_log (this->name, GF_LOG_ERROR, "%s/%s"
                                        " gfid not present", loc->path,
                                         entry->d_name);
                                continue;
                        }

                        gf_uuid_copy (entry_loc.pargfid, loc->gfid);

                        ret = syncop_lookup (this, &entry_loc, &iatt, NULL,
                                             NULL, NULL);
                        if (ret) {
                                if (-ret == ENOENT || -ret == ESTALE) {
                                        gf_msg (this->name, GF_LOG_INFO, -ret,
                                                DHT_MSG_DIR_LOOKUP_FAILED,
                                                "Dir:%s renamed or removed. "
                                                "Skipping", loc->path);
                                                ret = 0;
                                        continue;
                                } else {
                                        gf_msg (this->name, GF_LOG_ERROR, -ret,
                                                DHT_MSG_DIR_LOOKUP_FAILED,
                                                "lookup failed for:%s",
                                                entry_loc.path);
                                        defrag->total_failures++;
                                        if (conf->decommission_in_progress) {
                                                defrag->defrag_status =
                                                GF_DEFRAG_STATUS_FAILED;
                                                ret = -1;
                                                goto out;
                                        } else {
                                                should_commit_hash = 0;
                                                continue;
                                        }
                                }
                        }

                        ret = syncop_setxattr (this, &entry_loc, fix_layout,
                                               0, NULL, NULL);
                        if (ret) {
                                if (-ret == ENOENT || -ret == ESTALE) {
                                        gf_msg (this->name, GF_LOG_INFO, -ret,
                                                DHT_MSG_LAYOUT_FIX_FAILED,
                                                "Setxattr failed. Dir %s "
                                                "renamed or removed",
                                                entry_loc.path);
                                        ret = 0;
                                        continue;
                                } else {

                                        gf_msg (this->name, GF_LOG_ERROR, -ret,
                                                DHT_MSG_LAYOUT_FIX_FAILED,
                                                "Setxattr failed for %s",
                                                entry_loc.path);

                                        defrag->total_failures++;

                                        if (conf->decommission_in_progress) {
                                                defrag->defrag_status =
                                                GF_DEFRAG_STATUS_FAILED;
                                                ret = -1;
                                                goto out;
                                        } else {
                                                continue;
                                        }
                                }
                        }

                        /* A return value of 2 means, either process_dir or
                         * lookup of a dir failed. Hence, don't commit hash
                         * for the current directory*/

                        ret = gf_defrag_fix_layout (this, defrag, &entry_loc,
                                                    fix_layout, migrate_data);

                        if (ret && ret != 2) {
                                gf_msg (this->name, GF_LOG_ERROR, 0,
                                        DHT_MSG_LAYOUT_FIX_FAILED,
                                        "Fix layout failed for %s",
                                        entry_loc.path);

                                defrag->total_failures++;

                                if (conf->decommission_in_progress) {
                                        defrag->defrag_status =
                                        GF_DEFRAG_STATUS_FAILED;

                                        goto out;
                                } else {
                                        /* Let's not commit-hash if
                                         * gf_defrag_fix_layout failed*/
                                        continue;
                                }
                        }

                        if (ret != 2 &&
                            gf_defrag_settle_hash (this, defrag, &entry_loc,
                            fix_layout) != 0) {
                                defrag->total_failures++;

                                gf_msg (this->name, GF_LOG_ERROR, 0,
                                        DHT_MSG_SETTLE_HASH_FAILED,
                                        "Settle hash failed for %s",
                                        entry_loc.path);

                                ret = -1;

                                if (conf->decommission_in_progress) {
                                        defrag->defrag_status =
                                        GF_DEFRAG_STATUS_FAILED;

                                        goto out;
                                }
                        }
                }
                gf_dirent_free (&entries);
                free_entries = _gf_false;
                INIT_LIST_HEAD (&entries.list);
        }

        ret = 0;
out:
        if (free_entries)
                gf_dirent_free (&entries);

        loc_wipe (&entry_loc);

        if (fd)
                fd_unref (fd);

        if (ret == 0 && should_commit_hash == 0) {
                ret = 2;
        }

        return ret;

}



/******************************************************************************
 *                      Tier background Fix layout functions
 ******************************************************************************/
/* This is the background tier fixlayout thread */
void *
gf_tier_do_fix_layout (void *args)
{
        gf_tier_fix_layout_arg_t *tier_fix_layout_arg   =  args;
        int                 ret                         = -1;
        xlator_t            *this                       = NULL;
        dht_conf_t          *conf                       = NULL;
        gf_defrag_info_t    *defrag                     = NULL;
        dict_t              *dict                       = NULL;
        loc_t               loc                         = {0,};
        struct iatt         iatt                        = {0,};
        struct iatt         parent                      = {0,};

        GF_VALIDATE_OR_GOTO ("tier", tier_fix_layout_arg, out);
        GF_VALIDATE_OR_GOTO ("tier", tier_fix_layout_arg->this, out);
        this = tier_fix_layout_arg->this;

        conf = this->private;
        GF_VALIDATE_OR_GOTO (this->name, conf, out);

        defrag = conf->defrag;
        GF_VALIDATE_OR_GOTO (this->name, defrag, out);
        GF_VALIDATE_OR_GOTO (this->name, defrag->root_inode, out);

        GF_VALIDATE_OR_GOTO (this->name, tier_fix_layout_arg->fix_layout, out);


        /* Get Root loc_t */
        dht_build_root_loc (defrag->root_inode, &loc);
        ret = syncop_lookup (this, &loc, &iatt, &parent, NULL, NULL);
        if (ret) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        DHT_MSG_REBALANCE_START_FAILED,
                        "Lookup on root failed.");
                ret = -1;
                goto out;
        }


        /* Start the crawl */
        gf_msg (this->name, GF_LOG_INFO, 0,
                        DHT_MSG_LOG_TIER_STATUS, "Tiering Fixlayout started");

        ret = gf_defrag_fix_layout (this, defrag, &loc,
                                    tier_fix_layout_arg->fix_layout, NULL);
        if (ret && ret != 2) {
                gf_msg (this->name, GF_LOG_ERROR, 0, DHT_MSG_REBALANCE_FAILED,
                        "Tiering fixlayout failed.");
                ret = -1;
                goto out;
        }

        if (ret != 2 && gf_defrag_settle_hash
                        (this, defrag, &loc,
                                tier_fix_layout_arg->fix_layout) != 0) {
                defrag->total_failures++;
                ret = -1;
                goto out;
        }

        dict = dict_new ();
        if (!dict) {
                ret = -1;
                goto out;
        }

        ret = dict_set_str (dict, GF_XATTR_TIER_LAYOUT_FIXED_KEY, "yes");
        if (ret) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        DHT_MSG_REBALANCE_FAILED,
                        "Failed to set dictionary value: key = %s",
                        GF_XATTR_TIER_LAYOUT_FIXED_KEY);
                ret = -1;
                goto out;
        }

        /* Marking the completion of tiering fix layout via a xattr on root */
        ret = syncop_setxattr (this, &loc, dict, 0, NULL, NULL);
        if (ret) {
                gf_log (this->name, GF_LOG_ERROR, "Failed to set tiering fix "
                        "layout completed xattr on %s", loc.path);
                ret = -1;
                goto out;
        }

        ret = 0;
out:
        if (ret)
                defrag->total_failures++;

        if (dict)
                dict_unref (dict);

        return NULL;
}

int
gf_tier_start_fix_layout (xlator_t *this,
                         loc_t *loc,
                         gf_defrag_info_t *defrag,
                         dict_t *fix_layout)
{
        int ret                                       = -1;
        dict_t  *tier_dict                            = NULL;
        gf_tier_fix_layout_arg_t *tier_fix_layout_arg = NULL;

        tier_dict = dict_new ();
        if (!tier_dict) {
                gf_log ("tier", GF_LOG_ERROR, "Tier fix layout failed :"
                        "Creation of tier_dict failed");
                ret = -1;
                goto out;
        }

        /* Check if layout is fixed already */
        ret = syncop_getxattr (this, loc, &tier_dict,
                                GF_XATTR_TIER_LAYOUT_FIXED_KEY,
                                NULL, NULL);
        if (ret != 0) {

                tier_fix_layout_arg = &defrag->tier_conf.tier_fix_layout_arg;

                /*Fill crawl arguments */
                tier_fix_layout_arg->this = this;
                tier_fix_layout_arg->fix_layout = fix_layout;

                /* Spawn the fix layout thread so that its done in the
                 * background */
                ret = pthread_create (&tier_fix_layout_arg->thread_id, NULL,
                                gf_tier_do_fix_layout, tier_fix_layout_arg);
                if (ret) {
                        gf_log ("tier", GF_LOG_ERROR, "Thread creation failed. "
                                "Background fix layout for tiering will not "
                                "work.");
                        defrag->total_failures++;
                        goto out;
                }
        }
        ret = 0;
out:
        if (tier_dict)
                dict_unref (tier_dict);

        return ret;
}

void
gf_tier_clear_fix_layout (xlator_t *this, loc_t *loc, gf_defrag_info_t *defrag)
{
        int ret         = -1;
        dict_t *dict    = NULL;

        GF_VALIDATE_OR_GOTO ("tier", this, out);
        GF_VALIDATE_OR_GOTO (this->name, loc, out);
        GF_VALIDATE_OR_GOTO (this->name, defrag, out);

        /* Check if background fixlayout is completed. This is not
         * multi-process safe i.e there is a possibility that by the time
         * we move to remove the xattr there it might have been cleared by some
         * other detach process from other node. We ignore the error if such
         * a thing happens */
        ret = syncop_getxattr (this, loc, &dict,
                        GF_XATTR_TIER_LAYOUT_FIXED_KEY, NULL, NULL);
        if (ret) {
                /* Background fixlayout not complete - nothing to clear*/
                gf_msg (this->name, GF_LOG_WARNING, -ret,
                        DHT_MSG_LOG_TIER_STATUS,
                        "Unable to retrieve fixlayout xattr."
                        "Assume background fix layout not complete");
                goto out;
        }

        ret = syncop_removexattr (this, loc, GF_XATTR_TIER_LAYOUT_FIXED_KEY,
                                  NULL, NULL);
        if (ret) {
                gf_msg (this->name, GF_LOG_WARNING, -ret,
                        DHT_MSG_LOG_TIER_STATUS,
                        "Failed removing tier fix layout "
                        "xattr from %s", loc->path);
                goto out;
        }
        ret = 0;
out:
        if (dict)
                dict_unref (dict);
}

void
gf_tier_wait_fix_lookup (gf_defrag_info_t *defrag) {
        if (defrag->tier_conf.tier_fix_layout_arg.thread_id) {
                pthread_join (defrag->tier_conf.tier_fix_layout_arg.thread_id,
                        NULL);
        }
}
/******************Tier background Fix layout functions END********************/


uint64_t gf_defrag_subvol_file_cnt (xlator_t *this, loc_t *root_loc)
{
        int ret = -1;
        struct statvfs buf = {0,};

        if (!this)
                return 0;

        ret = syncop_statfs (this, root_loc, &buf, NULL, NULL);
        if (ret) {
                /* Aargh! */
                return 0;
        }
        return (buf.f_files - buf.f_ffree);
}


int gf_defrag_total_file_cnt (xlator_t *this, loc_t *root_loc)
{
        dht_conf_t    *conf  = NULL;
        int            ret   = -1;
        int            i     = 0;
        uint64_t       num_files = 0;


        conf = this->private;
        if (!conf) {
                return ret;
        }

        for (i = 0 ; i < conf->local_subvols_cnt; i++) {
                num_files = gf_defrag_subvol_file_cnt (conf->local_subvols[i],
                                                       root_loc);
                g_totalfiles += num_files;
                gf_msg (this->name, GF_LOG_INFO, 0, 0, "local subvol: %s,"
                        "cnt = %"PRIu64, conf->local_subvols[i]->name,
                        num_files);
        }

        /* FIXFIXFIX: halve the number of files to negate .glusterfs contents
           We need a better way to figure this out */

        g_totalfiles = g_totalfiles/2;
        if (g_totalfiles > 20000)
                g_totalfiles += 10000;

        gf_msg (this->name, GF_LOG_INFO, 0, 0,
                "Total number of files = %"PRIu64, g_totalfiles);

        return 0;
}


int
gf_defrag_start_crawl (void *data)
{
        xlator_t                *this                   = NULL;
        dht_conf_t              *conf                   = NULL;
        gf_defrag_info_t        *defrag                 = NULL;
        int                      ret                    = -1;
        loc_t                    loc                    = {0,};
        struct iatt              iatt                   = {0,};
        struct iatt              parent                 = {0,};
        dict_t                  *fix_layout             = NULL;
        dict_t                  *migrate_data           = NULL;
        dict_t                  *status                 = NULL;
        dict_t                  *dict                   = NULL;
        glusterfs_ctx_t         *ctx                    = NULL;
        dht_methods_t           *methods                = NULL;
        int                      i                      = 0;
        int                     thread_index            = 0;
        int                     err                     = 0;
        int                     thread_spawn_count      = 0;
        pthread_t               *tid                    = NULL;
        gf_boolean_t            is_tier_detach          = _gf_false;


        this = data;
        if (!this)
                goto exit;

        ctx = this->ctx;
        if (!ctx)
                goto exit;

        conf = this->private;
        if (!conf)
                goto exit;

        defrag = conf->defrag;
        if (!defrag)
                goto exit;

        gettimeofday (&defrag->start_time, NULL);
        dht_build_root_inode (this, &defrag->root_inode);
        if (!defrag->root_inode)
                goto out;

        dht_build_root_loc (defrag->root_inode, &loc);

        /* fix-layout on '/' first */

        ret = syncop_lookup (this, &loc, &iatt, &parent, NULL, NULL);

        if (ret) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        DHT_MSG_REBALANCE_START_FAILED,
                        "Failed to start rebalance: look up on / failed");
                ret = -1;
                goto out;
        }

        fix_layout = dict_new ();
        if (!fix_layout) {
                ret = -1;
                goto out;
        }

        /*
         * Unfortunately, we can't do special xattrs (like fix.layout) and
         * real ones in the same call currently, and changing it seems
         * riskier than just doing two calls.
         */

        gf_log (this->name, GF_LOG_INFO, "%s using commit hash %u",
                __func__, conf->vol_commit_hash);

        ret = dict_set_uint32 (fix_layout, conf->commithash_xattr_name,
                               conf->vol_commit_hash);
        if (ret) {
                gf_log (this->name, GF_LOG_ERROR,
                        "Failed to set %s", conf->commithash_xattr_name);
                defrag->total_failures++;
                ret = -1;
                goto out;
        }

        ret = syncop_setxattr (this, &loc, fix_layout, 0, NULL, NULL);
        if (ret) {
                gf_log (this->name, GF_LOG_ERROR, "fix layout on %s failed",
                        loc.path);
                defrag->total_failures++;
                ret = -1;
                goto out;
        }

        /* We now return to our regularly scheduled program. */

        ret = dict_set_str (fix_layout, GF_XATTR_FIX_LAYOUT_KEY, "yes");
        if (ret) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        DHT_MSG_REBALANCE_START_FAILED,
                        "Failed to start rebalance:"
                        "Failed to set dictionary value: key = %s",
                        GF_XATTR_FIX_LAYOUT_KEY);
                defrag->total_failures++;
                ret = -1;
                goto out;
        }

        defrag->new_commit_hash = conf->vol_commit_hash;

        ret = syncop_setxattr (this, &loc, fix_layout, 0, NULL, NULL);
        if (ret) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        DHT_MSG_REBALANCE_FAILED,
                        "fix layout on %s failed",
                        loc.path);
                defrag->total_failures++;
                ret = -1;
                goto out;
        }

        if (defrag->cmd != GF_DEFRAG_CMD_START_LAYOUT_FIX) {
                migrate_data = dict_new ();
                if (!migrate_data) {
                        defrag->total_failures++;
                        ret = -1;
                        goto out;
                }
                ret = dict_set_str (migrate_data, GF_XATTR_FILE_MIGRATE_KEY,
                        (defrag->cmd == GF_DEFRAG_CMD_START_FORCE)
                        ?  "force" : "non-force");
                if (ret) {
                        defrag->total_failures++;
                        ret = -1;
                        goto out;
                }

                /* Find local subvolumes */
                ret = syncop_getxattr (this, &loc, &dict,
                                       GF_REBAL_FIND_LOCAL_SUBVOL,
                                       NULL, NULL);
                if (ret) {
                        gf_msg (this->name, GF_LOG_ERROR, 0, 0, "local "
                                "subvolume determination failed with error: %d",
                                -ret);
                        ret = -1;
                        goto out;
                }

                for (i = 0 ; i < conf->local_subvols_cnt; i++) {
                        gf_msg (this->name, GF_LOG_INFO, 0, 0, "local subvols "
                                "are %s", conf->local_subvols[i]->name);
                }

                ret = gf_defrag_total_file_cnt (this, &loc);
                if (!ret) {
                        gf_msg (this->name, GF_LOG_ERROR, 0, 0, "Failed to get "
                                "the total number of files. Unable to estimate "
                                "time to complete rebalance.");
                }

                /* Initialize global entry queue */
                defrag->queue = GF_CALLOC (1, sizeof (struct dht_container),
                                           gf_dht_mt_container_t);

                if (!defrag->queue) {
                        gf_log (this->name, GF_LOG_ERROR, "No memory for "
                                "queue");
                        ret = -1;
                        goto out;
                }

                INIT_LIST_HEAD (&(defrag->queue[0].list));

                thread_spawn_count = MAX ((sysconf(_SC_NPROCESSORS_ONLN) - 4), 4);

                gf_msg_debug (this->name, 0, "thread_spawn_count: %d",
                              thread_spawn_count);

                tid = GF_CALLOC (thread_spawn_count, sizeof (pthread_t),
                                 gf_common_mt_pthread_t);
                if (!tid) {
                        gf_log (this->name, GF_LOG_ERROR, "Insufficient memory "
                                "for tid");
                        ret = -1;
                        goto out;
                }

                defrag->current_thread_count = thread_spawn_count;

                /*Spawn Threads Here*/
                while (thread_index < thread_spawn_count) {
                        err = pthread_create(&(tid[thread_index]), NULL,
                                     &gf_defrag_task, (void *)defrag);
                        if (err != 0) {
                                gf_log ("DHT", GF_LOG_ERROR,
                                        "Thread[%d] creation failed. "
                                        "Aborting Rebalance",
                                         thread_index);
                                ret = -1;
                                goto out;
                        } else {
                                gf_log ("DHT", GF_LOG_INFO, "Thread[%d] "
                                        "creation successful", thread_index);
                        }
                        thread_index++;
                }
        }

        if (defrag->cmd == GF_DEFRAG_CMD_START_TIER) {
                /* Fix layout for attach tier */
                ret = gf_tier_start_fix_layout (this, &loc, defrag, fix_layout);
                if (ret) {
                        goto out;
                }

                methods = &(conf->methods);

                /* Calling tier_start of tier.c */
                methods->migration_other(this, defrag);
                if (defrag->cmd == GF_DEFRAG_CMD_START_DETACH_TIER ||
                    defrag->cmd == GF_DEFRAG_CMD_DETACH_START) {

                        ret = dict_set_str (migrate_data,
                                            GF_XATTR_FILE_MIGRATE_KEY,
                                            "force");
                        if (ret)
                                goto out;

                }
        } else {
                ret = gf_defrag_fix_layout (this, defrag, &loc, fix_layout,
                                    migrate_data);
                if (ret && ret != 2) {
                        defrag->total_failures++;
                        ret = -1;
                        goto out;
                }

                if (ret != 2 && gf_defrag_settle_hash
                        (this, defrag, &loc, fix_layout) != 0) {
                        defrag->total_failures++;
                        ret = -1;
                        goto out;
                }

                if (defrag->cmd == GF_DEFRAG_CMD_START_DETACH_TIER ||
                    defrag->cmd == GF_DEFRAG_CMD_DETACH_START)
                        is_tier_detach = _gf_true;

        }

        gf_log ("DHT", GF_LOG_INFO, "crawling file-system completed");
out:

        /* We are here means crawling the entire file system is done
           or something failed. Set defrag->crawl_done flag to intimate
           the migrator threads to exhaust the defrag->queue and terminate*/

        if (ret) {
                defrag->defrag_status = GF_DEFRAG_STATUS_FAILED;
        }

        pthread_mutex_lock (&defrag->dfq_mutex);
        {
                defrag->crawl_done = 1;

                pthread_cond_broadcast (
                        &defrag->parallel_migration_cond);
                pthread_cond_broadcast (
                        &defrag->df_wakeup_thread);
        }
        pthread_mutex_unlock (&defrag->dfq_mutex);

        /*Wait for all the threads to complete their task*/
        for (i = 0; i < thread_index; i++) {
                pthread_join (tid[i], NULL);
        }

        GF_FREE (tid);

        if (defrag->cmd == GF_DEFRAG_CMD_START_TIER) {
                /* Wait for the tier fixlayout to
                 * complete if its was started.*/
                 gf_tier_wait_fix_lookup (defrag);
        }

        if (is_tier_detach && ret == 0) {
                /* If it was a detach remove the tier fix-layout
                * xattr on root. Ignoring the failure, as nothing has to be
                * done, logging is done in gf_tier_clear_fix_layout */
                gf_tier_clear_fix_layout (this, &loc, defrag);
        }

        if (defrag->queue) {
                gf_dirent_free (defrag->queue[0].df_entry);
                INIT_LIST_HEAD (&(defrag->queue[0].list));
        }

        if ((defrag->defrag_status != GF_DEFRAG_STATUS_STOPPED) &&
            (defrag->defrag_status != GF_DEFRAG_STATUS_FAILED)) {
                defrag->defrag_status = GF_DEFRAG_STATUS_COMPLETE;
        }

        dht_send_rebalance_event (this, defrag->cmd, defrag->defrag_status);

        LOCK (&defrag->lock);
        {
                status = dict_new ();
                gf_defrag_status_get (defrag, status);
                if (ctx && ctx->notify)
                        ctx->notify (GF_EN_DEFRAG_STATUS, status);
                if (status)
                        dict_unref (status);
                defrag->is_exiting = 1;
        }
        UNLOCK (&defrag->lock);

        GF_FREE (defrag->queue);

        GF_FREE (defrag);
        conf->defrag = NULL;

        if (dict)
                dict_unref (dict);

        if (migrate_data)
                dict_unref (migrate_data);

exit:
        return ret;
}



static int
gf_defrag_done  (int ret, call_frame_t *sync_frame, void *data)
{
        gf_listener_stop (sync_frame->this);

        STACK_DESTROY (sync_frame->root);
        kill (getpid(), SIGTERM);
        return 0;
}

void *
gf_defrag_start (void *data)
{
        int                      ret    = -1;
        call_frame_t            *frame  = NULL;
        dht_conf_t              *conf   = NULL;
        gf_defrag_info_t        *defrag = NULL;
        xlator_t                *this  = NULL;
        xlator_t                *old_THIS = NULL;

        this = data;
        conf = this->private;
        if (!conf)
                goto out;

        defrag = conf->defrag;
        if (!defrag)
                goto out;

        frame = create_frame (this, this->ctx->pool);
        if (!frame)
                goto out;

        frame->root->pid = GF_CLIENT_PID_DEFRAG;

        defrag->pid = frame->root->pid;

        defrag->defrag_status = GF_DEFRAG_STATUS_STARTED;

        old_THIS = THIS;
        THIS = this;
        ret = synctask_new (this->ctx->env, gf_defrag_start_crawl,
                            gf_defrag_done, frame, this);

        if (ret)
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        DHT_MSG_REBALANCE_START_FAILED,
                        "Could not create task for rebalance");
        THIS = old_THIS;
out:
        return NULL;
}

int
gf_defrag_status_get (gf_defrag_info_t *defrag, dict_t *dict)
{
        int      ret    = 0;
        uint64_t files  = 0;
        uint64_t size   = 0;
        uint64_t lookup = 0;
        uint64_t failures = 0;
        uint64_t skipped = 0;
        uint64_t promoted = 0;
        uint64_t demoted = 0;
        char    *status = "";
        double   elapsed = 0;
        uint64_t time_left = 0;
        uint64_t time_to_complete = 0;
        double rate_lookedup = 0;
        struct timeval end = {0,};


        if (!defrag)
                goto out;

        ret = 0;
        if (defrag->defrag_status == GF_DEFRAG_STATUS_NOT_STARTED)
                goto out;

        files  = defrag->total_files;
        size   = defrag->total_data;
        lookup = defrag->num_files_lookedup;
        failures = defrag->total_failures;
        skipped = defrag->skipped;
        promoted = defrag->total_files_promoted;
        demoted = defrag->total_files_demoted;

        gettimeofday (&end, NULL);

        elapsed = end.tv_sec - defrag->start_time.tv_sec;

/*START */

/* rate at which files looked up */


        if ((defrag->cmd != GF_DEFRAG_CMD_START_TIER)
                && (defrag->defrag_status == GF_DEFRAG_STATUS_STARTED)
                && g_totalfiles) {

                rate_lookedup = (defrag->num_files_lookedup)/elapsed;
                if (defrag->num_files_lookedup > g_totalfiles)
                        g_totalfiles = defrag->num_files_lookedup + 10000;
                time_to_complete = (g_totalfiles)/rate_lookedup;
                time_left = time_to_complete - elapsed;

                gf_log (THIS->name, GF_LOG_INFO,
                        "TIME: num_files_lookedup=%"PRIu64",elapsed time = %f,"
                        "rate_lookedup=%f", defrag->num_files_lookedup, elapsed,
                        rate_lookedup);
                gf_log (THIS->name, GF_LOG_INFO,
                        "TIME: Estimated total time to complete = %"PRIu64
                        " seconds", time_to_complete);

                gf_log (THIS->name, GF_LOG_INFO,
                        "TIME: Seconds left = %"PRIu64" seconds", time_left);
        }

/*END */
        if (!dict)
                goto log;

        ret = dict_set_uint64 (dict, "promoted", promoted);
        if (ret)
                gf_log (THIS->name, GF_LOG_WARNING,
                        "failed to set promoted count");

        ret = dict_set_uint64 (dict, "demoted", demoted);
        if (ret)
                gf_log (THIS->name, GF_LOG_WARNING,
                        "failed to set demoted count");

        ret = dict_set_uint64 (dict, "files", files);
        if (ret)
                gf_log (THIS->name, GF_LOG_WARNING,
                        "failed to set file count");

        ret = dict_set_uint64 (dict, "size", size);
        if (ret)
                gf_log (THIS->name, GF_LOG_WARNING,
                        "failed to set size of xfer");

        ret = dict_set_uint64 (dict, "lookups", lookup);
        if (ret)
                gf_log (THIS->name, GF_LOG_WARNING,
                        "failed to set lookedup file count");


        ret = dict_set_int32 (dict, "status", defrag->defrag_status);
        if (ret)
                gf_log (THIS->name, GF_LOG_WARNING,
                        "failed to set status");

        ret = dict_set_double (dict, "run-time", elapsed);
        if (ret)
                gf_log (THIS->name, GF_LOG_WARNING,
                        "failed to set run-time");

        ret = dict_set_uint64 (dict, "failures", failures);
        if (ret)
                gf_log (THIS->name, GF_LOG_WARNING,
                        "failed to set failure count");

        ret = dict_set_uint64 (dict, "skipped", skipped);
        if (ret)
                gf_log (THIS->name, GF_LOG_WARNING,
                        "failed to set skipped file count");

        ret = dict_set_uint64 (dict, "time-left", time_left);
        if (ret)
                gf_log (THIS->name, GF_LOG_WARNING,
                        "failed to set time-left");

log:
        switch (defrag->defrag_status) {
        case GF_DEFRAG_STATUS_NOT_STARTED:
                status = "not started";
                break;
        case GF_DEFRAG_STATUS_STARTED:
                status = "in progress";
                break;
        case GF_DEFRAG_STATUS_STOPPED:
                status = "stopped";
                break;
        case GF_DEFRAG_STATUS_COMPLETE:
                status = "completed";
                break;
        case GF_DEFRAG_STATUS_FAILED:
                status = "failed";
                break;
        default:
                break;
        }

        gf_msg (THIS->name, GF_LOG_INFO, 0, DHT_MSG_REBALANCE_STATUS,
                "Rebalance is %s. Time taken is %.2f secs",
                status, elapsed);
        gf_msg (THIS->name, GF_LOG_INFO, 0, DHT_MSG_REBALANCE_STATUS,
                "Files migrated: %"PRIu64", size: %"
                PRIu64", lookups: %"PRIu64", failures: %"PRIu64", skipped: "
                "%"PRIu64, files, size, lookup, failures, skipped);


out:
        return 0;
}

void
gf_defrag_set_pause_state (gf_tier_conf_t *tier_conf, tier_pause_state_t state)
{
        pthread_mutex_lock (&tier_conf->pause_mutex);
        tier_conf->pause_state = state;
        pthread_mutex_unlock (&tier_conf->pause_mutex);
}

tier_pause_state_t
gf_defrag_get_pause_state (gf_tier_conf_t *tier_conf)
{
        int state;

        pthread_mutex_lock (&tier_conf->pause_mutex);
        state = tier_conf->pause_state;
        pthread_mutex_unlock (&tier_conf->pause_mutex);

        return state;
}

tier_pause_state_t
gf_defrag_check_pause_tier (gf_tier_conf_t *tier_conf)
{
        int woke = 0;
        int state  = -1;

        pthread_mutex_lock (&tier_conf->pause_mutex);

        if (tier_conf->pause_state == TIER_RUNNING)
                goto out;

        if (tier_conf->pause_state == TIER_PAUSED)
                goto out;

        if (tier_conf->promote_in_progress ||
            tier_conf->demote_in_progress)
                goto out;

        tier_conf->pause_state = TIER_PAUSED;

        if (tier_conf->pause_synctask) {
                synctask_wake (tier_conf->pause_synctask);
                tier_conf->pause_synctask = 0;
                woke = 1;
        }

        gf_msg ("tier", GF_LOG_DEBUG, 0,
                DHT_MSG_TIER_PAUSED,
                "woken %d", woke);

        gf_event (EVENT_TIER_PAUSE, "vol=%s", tier_conf->volname);
out:
        state = tier_conf->pause_state;

        pthread_mutex_unlock (&tier_conf->pause_mutex);

        return state;
}

void
gf_defrag_pause_tier_timeout (void *data)
{
        xlator_t         *this                  = NULL;
        dht_conf_t       *conf                  = NULL;
        gf_defrag_info_t *defrag                = NULL;

        this   = (xlator_t *) data;
        GF_VALIDATE_OR_GOTO ("tier", this, out);

        conf   = this->private;
        GF_VALIDATE_OR_GOTO (this->name, conf, out);

        defrag = conf->defrag;
        GF_VALIDATE_OR_GOTO (this->name, defrag, out);

        gf_msg (this->name, GF_LOG_DEBUG, 0,
                DHT_MSG_TIER_PAUSED,
                "Request pause timer timeout");

        gf_defrag_check_pause_tier (&defrag->tier_conf);

out:
        return;
}

int
gf_defrag_pause_tier (xlator_t *this, gf_defrag_info_t *defrag)
{
        int             ret         = 0;
        struct timespec delta       = {0,};
        int             delay       = 2;

        if (defrag->defrag_status != GF_DEFRAG_STATUS_STARTED)
                goto out;

        /*
         * Set flag requesting to pause tiering. Wait 'delay' seconds for
         * tiering to actually stop as indicated by the pause state
         * before returning success or failure.
         */
        gf_defrag_set_pause_state (&defrag->tier_conf, TIER_REQUEST_PAUSE);

        /*
         * If migration is not underway, can pause immediately.
         */
        gf_defrag_check_pause_tier (&defrag->tier_conf);
        if (gf_defrag_get_pause_state (&defrag->tier_conf) == TIER_PAUSED)
                goto out;

        gf_msg (this->name, GF_LOG_DEBUG, 0,
                DHT_MSG_TIER_PAUSED,
                "Request pause tier");

        defrag->tier_conf.pause_synctask = synctask_get ();
        delta.tv_sec  = delay;
        delta.tv_nsec = 0;
        defrag->tier_conf.pause_timer =
                gf_timer_call_after (this->ctx, delta,
                                     gf_defrag_pause_tier_timeout,
                                     this);

        synctask_yield (defrag->tier_conf.pause_synctask);

        if (gf_defrag_get_pause_state (&defrag->tier_conf) == TIER_PAUSED)
                goto out;

        gf_defrag_set_pause_state (&defrag->tier_conf, TIER_RUNNING);

        ret = -1;
out:

        gf_msg (this->name, GF_LOG_DEBUG, 0,
                DHT_MSG_TIER_PAUSED,
                "Pause tiering ret=%d", ret);

        return ret;
}

int
gf_defrag_resume_tier (xlator_t *this, gf_defrag_info_t *defrag)
{
        gf_msg (this->name, GF_LOG_DEBUG, 0,
                DHT_MSG_TIER_RESUME,
                "Pause end. Resume tiering");

        gf_defrag_set_pause_state (&defrag->tier_conf, TIER_RUNNING);

        gf_event (EVENT_TIER_RESUME, "vol=%s", defrag->tier_conf.volname);

        return 0;
}

int
gf_defrag_start_detach_tier (gf_defrag_info_t *defrag)
{
        defrag->cmd = GF_DEFRAG_CMD_START_DETACH_TIER;

        return 0;
}

int
gf_defrag_stop (gf_defrag_info_t *defrag, gf_defrag_status_t status,
                dict_t *output)
{
        /* TODO: set a variable 'stop_defrag' here, it should be checked
           in defrag loop */
        int     ret = -1;
        GF_ASSERT (defrag);

        if (defrag->defrag_status == GF_DEFRAG_STATUS_NOT_STARTED) {
                goto out;
        }

        gf_msg ("", GF_LOG_INFO, 0, DHT_MSG_REBALANCE_STOPPED,
                "Received stop command on rebalance");
        defrag->defrag_status = status;

        if (output)
                gf_defrag_status_get (defrag, output);
        ret = 0;
out:
        gf_msg_debug ("", 0, "Returning %d", ret);
        return ret;
}
