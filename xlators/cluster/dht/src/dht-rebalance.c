/*
  Copyright (c) 2008-2012 Red Hat, Inc. <http://www.redhat.com>
  This file is part of GlusterFS.

  This file is licensed to you under your choice of the GNU Lesser
  General Public License, version 3 or any later version (LGPLv3 or
  later), or the GNU General Public License, version 2 (GPLv2), in all
  cases as published by the Free Software Foundation.
*/


#ifndef _CONFIG_H
#define _CONFIG_H
#include "config.h"
#endif

#include "dht-common.h"
#include "xlator.h"
#include <signal.h>
#include <fnmatch.h>

#define GF_DISK_SECTOR_SIZE             512
#define DHT_REBALANCE_PID               4242 /* Change it if required */
#define DHT_REBALANCE_BLKSIZE           (128 * 1024)

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
                                                    iobref, 0);
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
                                            (offset + tmp_offset), iobref, 0);
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

        GF_VALIDATE_OR_GOTO ("defrag", loc, out);
        GF_VALIDATE_OR_GOTO ("defrag", loc->name, out);
        GF_VALIDATE_OR_GOTO ("defrag", stbuf, out);
        GF_VALIDATE_OR_GOTO ("defrag", this, out);
        GF_VALIDATE_OR_GOTO ("defrag", xattrs, out);
        GF_VALIDATE_OR_GOTO ("defrag", this->private, out);

        conf = this->private;

        if (uuid_is_null (loc->pargfid)) {
                gf_log ("", GF_LOG_ERROR, "loc->pargfid is NULL for "
                        "%s", loc->path);
                goto out;
        }

        if (uuid_is_null (loc->gfid)) {
                gf_log ("", GF_LOG_ERROR, "loc->gfid is NULL for "
                        "%s", loc->path);
                goto out;
        }

        cached_subvol = dht_subvol_get_cached (this, loc->inode);
        if (!cached_subvol) {
                gf_log (this->name, GF_LOG_ERROR, "Failed to get cached subvol"
                        " for %s on %s", loc->name, this->name);
                goto out;
        }

        hashed_subvol = dht_subvol_get_hashed (this, loc);
        if (!hashed_subvol) {
                gf_log (this->name, GF_LOG_ERROR, "Failed to get hashed subvol"
                        " for %s on %s", loc->name, this->name);
                goto out;
        }

        gf_log (this->name, GF_LOG_INFO, "Attempting to migrate hardlink %s "
                "with gfid %s from %s -> %s", loc->name, uuid_utoa (loc->gfid),
                cached_subvol->name, hashed_subvol->name);
        data = dict_get (xattrs, conf->link_xattr_name);
        /* set linkto on cached -> hashed if not present, else link it */
        if (!data) {
                ret = dict_set_str (xattrs, conf->link_xattr_name,
                                    hashed_subvol->name);
                if (ret) {
                        gf_log (this->name, GF_LOG_ERROR, "Failed to set "
                                "linkto xattr in dict for %s", loc->name);
                        goto out;
                }

                ret = syncop_setxattr (cached_subvol, loc, xattrs, 0);
                if (ret) {
                        gf_log (this->name, GF_LOG_ERROR, "Linkto setxattr "
                                "failed %s -> %s (%s)", cached_subvol->name,
                                loc->name, strerror (-ret));
                        ret = -1;
                        goto out;
                }
                goto out;
        } else {
                linkto_subvol = dht_linkfile_subvol (this, NULL, NULL, xattrs);
                if (!linkto_subvol) {
                        gf_log (this->name, GF_LOG_ERROR, "Failed to get "
                                "linkto subvol for %s", loc->name);
                } else {
                        hashed_subvol = linkto_subvol;
                }

                ret = syncop_link (hashed_subvol, loc, loc);
                if  (ret) {
                        op_errno = -ret;
                        ret = -1;
                        gf_log (this->name, GF_LOG_ERROR, "link of %s -> %s"
                                " failed on  subvol %s (%s)", loc->name,
                                uuid_utoa(loc->gfid),
                                hashed_subvol->name, strerror (op_errno));
                        if (op_errno != EEXIST)
                                goto out;
                }
        }
        ret = syncop_lookup (hashed_subvol, loc, NULL, &iatt, NULL, NULL);
        if (ret) {
                gf_log (this->name, GF_LOG_ERROR, "Failed lookup %s on %s (%s)"
                        , loc->name, hashed_subvol->name, strerror (-ret));
                ret = -1;
                goto out;
        }

        if (iatt.ia_nlink == stbuf->ia_nlink) {
                ret = dht_migrate_file (this, loc, cached_subvol, hashed_subvol,
                                        GF_DHT_MIGRATE_HARDLINK_IN_PROGRESS);
                if (ret)
                        goto out;
        }
        ret = 0;
out:
        return ret;
}


static inline int
__is_file_migratable (xlator_t *this, loc_t *loc,
                      struct iatt *stbuf, dict_t *xattrs, int flags)
{
        int ret = -1;

        if (IA_ISDIR (stbuf->ia_type)) {
                gf_log (this->name, GF_LOG_WARNING,
                        "%s: migrate-file called on directory", loc->path);
                ret = -1;
                goto out;
        }

        if (flags == GF_DHT_MIGRATE_HARDLINK_IN_PROGRESS) {
                ret = 0;
                goto out;
        }
        if (stbuf->ia_nlink > 1) {
                /* support for decomission */
                if (flags == GF_DHT_MIGRATE_HARDLINK) {
                        ret = gf_defrag_handle_hardlink (this, loc,
                                                         xattrs, stbuf);
                        if (ret) {
                                gf_log (this->name, GF_LOG_WARNING,
                                        "%s: failed to migrate file with link",
                                        loc->path);
                        }
                } else {
                        gf_log (this->name, GF_LOG_WARNING,
                                "%s: file has hardlinks", loc->path);
                }
                ret = ENOTSUP;
                goto out;
        }

        ret = 0;

out:
        return ret;
}

static inline int
__dht_rebalance_create_dst_file (xlator_t *to, xlator_t *from, loc_t *loc, struct iatt *stbuf,
                                 dict_t *dict, fd_t **dst_fd, dict_t *xattr)
{
        xlator_t    *this = NULL;
        int          ret  = -1;
        fd_t        *fd   = NULL;
        struct iatt  new_stbuf = {0,};
        dht_conf_t  *conf = NULL;

        this = THIS;
        conf = this->private;

        ret = dict_set_static_bin (dict, "gfid-req", stbuf->ia_gfid, 16);
        if (ret) {
                gf_log (this->name, GF_LOG_ERROR,
                        "%s: failed to set gfid in dict for create", loc->path);
                goto out;
        }

        ret = dict_set_str (dict, conf->link_xattr_name, from->name);
        if (ret) {
                gf_log (this->name, GF_LOG_ERROR,
                        "%s: failed to set gfid in dict for create", loc->path);
                goto out;
        }

        fd = fd_create (loc->inode, DHT_REBALANCE_PID);
        if (!fd) {
                gf_log (this->name, GF_LOG_ERROR,
                        "%s: fd create failed (destination) (%s)",
                        loc->path, strerror (errno));
                ret = -1;
                goto out;
        }

        ret = syncop_lookup (to, loc, NULL, &new_stbuf, NULL, NULL);
        if (!ret) {
                /* File exits in the destination, check if gfid matches */
                if (uuid_compare (stbuf->ia_gfid, new_stbuf.ia_gfid) != 0) {
                        gf_log (this->name, GF_LOG_ERROR,
                                "file %s exits in %s with different gfid",
                                loc->path, to->name);
                        fd_unref (fd);
                        goto out;
                }
        }
        if ((ret < 0) && (-ret != ENOENT)) {
                /* File exists in destination, but not accessible */
                gf_log (THIS->name, GF_LOG_WARNING,
                        "%s: failed to lookup file (%s)",
                        loc->path, strerror (-ret));
                ret = -1;
                goto out;
        }

        /* Create the destination with LINKFILE mode, and linkto xattr,
           if the linkfile already exists, it will just open the file */
        ret = syncop_create (to, loc, O_RDWR, DHT_LINKFILE_MODE, fd,
                             dict, &new_stbuf);
        if (ret < 0) {
                gf_log (this->name, GF_LOG_ERROR,
                        "failed to create %s on %s (%s)",
                        loc->path, to->name, strerror (-ret));
                ret = -1;
                goto out;
        }

        ret = syncop_fsetxattr (to, fd, xattr, 0);
        if (ret < 0)
                gf_log (this->name, GF_LOG_WARNING,
                        "%s: failed to set xattr on %s (%s)",
                        loc->path, to->name, strerror (-ret));

        ret = syncop_ftruncate (to, fd, stbuf->ia_size);
        if (ret < 0)
                gf_log (this->name, GF_LOG_ERROR,
                        "ftruncate failed for %s on %s (%s)",
                        loc->path, to->name, strerror (-ret));

        ret = syncop_fsetattr (to, fd, stbuf,
                               (GF_SET_ATTR_UID | GF_SET_ATTR_GID),
                                NULL, NULL);
        if (ret < 0)
                gf_log (this->name, GF_LOG_ERROR,
                        "chown failed for %s on %s (%s)",
                        loc->path, to->name, strerror (-ret));

        if (dst_fd)
                *dst_fd = fd;

        /* success */
        ret = 0;

out:
        return ret;
}

static inline int
__dht_check_free_space (xlator_t *to, xlator_t *from, loc_t *loc,
                        struct iatt *stbuf, int flag)
{
        struct statvfs  src_statfs = {0,};
        struct statvfs  dst_statfs = {0,};
        int             ret        = -1;
        xlator_t       *this       = NULL;

        uint64_t        src_statfs_blocks = 1;
        uint64_t        dst_statfs_blocks = 1;

        this = THIS;

        ret = syncop_statfs (from, loc, &src_statfs);
        if (ret) {
                gf_log (this->name, GF_LOG_ERROR,
                        "failed to get statfs of %s on %s (%s)",
                        loc->path, from->name, strerror (-ret));
                ret = -1;
                goto out;
        }

        ret = syncop_statfs (to, loc, &dst_statfs);
        if (ret) {
                gf_log (this->name, GF_LOG_ERROR,
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
                        gf_log (this->name, GF_LOG_WARNING,
                                "data movement attempted from node (%s) with"
                                " higher disk space to a node (%s) with "
                                "lesser disk space (%s)", from->name,
                                to->name, loc->path);

                        /* this is not a 'failure', but we don't want to
                           consider this as 'success' too :-/ */
                        ret = 1;
                        goto out;
                }
        }
check_avail_space:
        if (((dst_statfs.f_bavail * dst_statfs.f_bsize) /
              GF_DISK_SECTOR_SIZE) < stbuf->ia_blocks) {
                gf_log (this->name, GF_LOG_ERROR,
                        "data movement attempted from node (%s) with "
                        "to node (%s) which does not have required free space"
                        " for %s", from->name, to->name, loc->path);
                ret = 1;
                goto out;
        }

        ret = 0;
out:
        return ret;
}

static inline int
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
                                    offset, 0, &vector, &count, &iobref);
                if (!ret || (ret < 0)) {
                        break;
                }

                if (hole_exists)
                        ret = dht_write_with_holes (to, dst, vector, count,
                                                    ret, offset, iobref);
                else
                        ret = syncop_writev (to, dst, vector, count,
                                             offset, iobref, 0);
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


static inline int
__dht_rebalance_open_src_file (xlator_t *from, xlator_t *to, loc_t *loc,
                               struct iatt *stbuf, fd_t **src_fd)
{
        int          ret  = 0;
        fd_t        *fd   = NULL;
        dict_t      *dict = NULL;
        xlator_t    *this = NULL;
        struct iatt  iatt = {0,};
        dht_conf_t  *conf = NULL;

        this = THIS;
        conf = this->private;

        fd = fd_create (loc->inode, DHT_REBALANCE_PID);
        if (!fd) {
                gf_log (this->name, GF_LOG_ERROR,
                        "%s: fd create failed (source)", loc->path);
                ret = -1;
                goto out;
        }

        ret = syncop_open (from, loc, O_RDWR, fd);
        if (ret < 0) {
                gf_log (this->name, GF_LOG_ERROR,
                        "failed to open file %s on %s (%s)",
                        loc->path, from->name, strerror (-ret));
                ret = -1;
                goto out;
        }

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
        ret = syncop_setxattr (from, loc, dict, 0);
        if (ret) {
                gf_log (this->name, GF_LOG_ERROR,
                        "failed to set xattr on %s in %s (%s)",
                        loc->path, from->name, strerror (-ret));
                ret = -1;
                goto out;
        }

        /* mode should be (+S+T) to indicate migration is in progress */
        iatt.ia_prot = stbuf->ia_prot;
        iatt.ia_type = stbuf->ia_type;
        iatt.ia_prot.sticky = 1;
        iatt.ia_prot.sgid = 1;

        ret = syncop_setattr (from, loc, &iatt, GF_SET_ATTR_MODE, NULL, NULL);
        if (ret) {
                gf_log (this->name, GF_LOG_ERROR,
                        "failed to set mode on %s in %s (%s)",
                        loc->path, from->name, strerror (-ret));
                ret = -1;
                goto out;
        }

        if (src_fd)
                *src_fd = fd;

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
        ret = syncop_lookup (to, loc, dict, &stbuf, &rsp_dict, NULL);
        if ((ret < 0) && (-ret != ENOENT)) {
                gf_log (this->name, GF_LOG_WARNING, "%s: lookup failed (%s)",
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
                        gf_log (this->name, GF_LOG_WARNING,
                                "%s: file exists in destination", loc->path);
                        ret = -1;
                        goto out;
                }

                /* as file is linkfile, delete it */
                ret = syncop_unlink (to, loc);
                if (ret) {
                        gf_log (this->name, GF_LOG_WARNING,
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
                ret = syncop_readlink (from, loc, &link, buf->ia_size);
                if (ret < 0) {
                        gf_log (this->name, GF_LOG_WARNING,
                                "%s: readlink on symlink failed (%s)",
                                loc->path, strerror (-ret));
                        ret = -1;
                        goto out;
                }

                ret = syncop_symlink (to, loc, link, dict, 0);
                if (ret) {
                        gf_log (this->name, GF_LOG_WARNING,
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
                                     ia_minor (buf->ia_rdev)), dict, 0);
        if (ret) {
                gf_log (this->name, GF_LOG_WARNING, "%s: mknod failed (%s)",
                        loc->path, strerror (-ret));
                ret = -1;
                goto out;
        }

done:
        ret = syncop_setattr (to, loc, buf,
                              (GF_SET_ATTR_UID | GF_SET_ATTR_GID |
                               GF_SET_ATTR_MODE), NULL, NULL);
        if (ret) {
                gf_log (this->name, GF_LOG_WARNING,
                        "%s: failed to perform setattr on %s (%s)",
                        loc->path, to->name, strerror (-ret));
                ret = -1;
        }

        ret = syncop_unlink (from, loc);
        if (ret) {
                gf_log (this->name, GF_LOG_WARNING, "%s: unlink failed (%s)",
                        loc->path, strerror (-ret));
                ret = -1;
        }

out:
        if (dict)
                dict_unref (dict);

        if (rsp_dict)
                dict_unref (rsp_dict);

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
        int             ret            = -1;
        struct iatt     new_stbuf      = {0,};
        struct iatt     stbuf          = {0,};
        struct iatt     empty_iatt     = {0,};
        ia_prot_t       src_ia_prot    = {0,};
        fd_t           *src_fd         = NULL;
        fd_t           *dst_fd         = NULL;
        dict_t         *dict           = NULL;
        dict_t         *xattr          = NULL;
        dict_t         *xattr_rsp      = NULL;
        int             file_has_holes = 0;
        dht_conf_t     *conf           = this->private;

        gf_log (this->name, GF_LOG_INFO, "%s: attempting to move from %s to %s",
                loc->path, from->name, to->name);

        dict = dict_new ();
        if (!dict)
                goto out;

        ret = dict_set_int32 (dict, conf->link_xattr_name, 256);
        if (ret) {
                gf_log (this->name, GF_LOG_ERROR,
                        "%s: failed to set 'linkto' key in dict", loc->path);
                goto out;
        }

        /* Phase 1 - Data migration is in progress from now on */
        ret = syncop_lookup (from, loc, dict, &stbuf, &xattr_rsp, NULL);
        if (ret) {
                gf_log (this->name, GF_LOG_ERROR, "%s: lookup failed on %s (%s)",
                        loc->path, from->name, strerror (-ret));
                ret = -1;
                goto out;
        }

        /* we no more require this key */
        dict_del (dict, conf->link_xattr_name);

        /* preserve source mode, so set the same to the destination */
        src_ia_prot = stbuf.ia_prot;

        /* Check if file can be migrated */
        ret = __is_file_migratable (this, loc, &stbuf, xattr_rsp, flag);
        if (ret)
                goto out;

        /* Take care of the special files */
        if (!IA_ISREG (stbuf.ia_type)) {
                /* Special files */
                ret = migrate_special_files (this, from, to, loc, &stbuf);
                goto out;
        }

        /* TODO: move all xattr related operations to fd based operations */
        ret = syncop_listxattr (from, loc, &xattr);
        if (ret < 0) {
                gf_log (this->name, GF_LOG_WARNING,
                        "%s: failed to get xattr from %s (%s)",
                        loc->path, from->name, strerror (-ret));
                ret = -1;
        }

        /* create the destination, with required modes/xattr */
        ret = __dht_rebalance_create_dst_file (to, from, loc, &stbuf,
                                               dict, &dst_fd, xattr);
        if (ret)
                goto out;

        ret = __dht_check_free_space (to, from, loc, &stbuf, flag);
        if (ret) {
                goto out;
        }

        /* Open the source, and also update mode/xattr */
        ret = __dht_rebalance_open_src_file (from, to, loc, &stbuf, &src_fd);
        if (ret) {
                gf_log (this->name, GF_LOG_ERROR, "failed to open %s on %s",
                        loc->path, from->name);
                goto out;
        }


        ret = syncop_fstat (from, src_fd, &stbuf);
        if (ret) {
                gf_log (this->name, GF_LOG_ERROR, "failed to lookup %s on %s (%s)",
                        loc->path, from->name, strerror (-ret));
                ret = -1;
                goto out;
        }

        /* Try to preserve 'holes' while migrating data */
        if (stbuf.ia_size > (stbuf.ia_blocks * GF_DISK_SECTOR_SIZE))
                file_has_holes = 1;

        /* All I/O happens in this function */
        ret = __dht_rebalance_migrate_data (from, to, src_fd, dst_fd,
					    stbuf.ia_size, file_has_holes);
        if (ret) {
                gf_log (this->name, GF_LOG_ERROR, "%s: failed to migrate data",
                        loc->path);
                /* reset the destination back to 0 */
                ret = syncop_ftruncate (to, dst_fd, 0);
                if (ret) {
                        gf_log (this->name, GF_LOG_ERROR,
                                "%s: failed to reset target size back to 0 (%s)",
                                loc->path, strerror (-ret));
                }

                ret = -1;
                goto out;
        }

        /* TODO: Sync the locks */

        ret = syncop_fsync (to, dst_fd, 0);
        if (ret) {
                gf_log (this->name, GF_LOG_WARNING,
                        "%s: failed to fsync on %s (%s)",
                        loc->path, to->name, strerror (-ret));
                ret = -1;
        }


        /* Phase 2 - Data-Migration Complete, Housekeeping updates pending */

        ret = syncop_fstat (from, src_fd, &new_stbuf);
        if (ret < 0) {
                /* Failed to get the stat info */
                gf_log (this->name, GF_LOG_ERROR,
                        "failed to fstat file %s on %s (%s)",
                        loc->path, from->name, strerror (-ret));
                ret = -1;
                goto out;
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
                                GF_SET_ATTR_MODE), NULL, NULL);
        if (ret) {
                gf_log (this->name, GF_LOG_WARNING,
                        "%s: failed to perform setattr on %s (%s)",
                        loc->path, to->name, strerror (-ret));
                ret = -1;
                goto out;
        }

        /* Because 'futimes' is not portable */
        ret = syncop_setattr (to, loc, &new_stbuf,
                              (GF_SET_ATTR_MTIME | GF_SET_ATTR_ATIME),
                              NULL, NULL);
        if (ret) {
                gf_log (this->name, GF_LOG_WARNING,
                        "%s: failed to perform setattr on %s (%s)",
                        loc->path, to->name, strerror (-ret));
                ret = -1;
        }

        /* Make the source as a linkfile first before deleting it */
        empty_iatt.ia_prot.sticky = 1;
        ret = syncop_fsetattr (from, src_fd, &empty_iatt,
                               GF_SET_ATTR_MODE, NULL, NULL);
        if (ret) {
                gf_log (this->name, GF_LOG_WARNING,             \
                        "%s: failed to perform setattr on %s (%s)",
                        loc->path, from->name, strerror (-ret));
                ret = -1;
                goto out;
        }

       /* Free up the data blocks on the source node, as the whole
           file is migrated */
        ret = syncop_ftruncate (from, src_fd, 0);
        if (ret) {
                gf_log (this->name, GF_LOG_WARNING,
                        "%s: failed to perform truncate on %s (%s)",
                        loc->path, from->name, strerror (-ret));
                ret = -1;
        }

        /* remove the 'linkto' xattr from the destination */
        ret = syncop_fremovexattr (to, dst_fd, conf->link_xattr_name);
        if (ret) {
                gf_log (this->name, GF_LOG_WARNING,
                        "%s: failed to perform removexattr on %s (%s)",
                        loc->path, to->name, strerror (-ret));
                ret = -1;
        }

        /* Do a stat and check the gfid before unlink */
        ret = syncop_stat (from, loc, &empty_iatt);
        if (ret) {
                gf_log (this->name, GF_LOG_WARNING,
                        "%s: failed to do a stat on %s (%s)",
                        loc->path, from->name, strerror (-ret));
                ret = -1;
                goto out;
        }

        if (uuid_compare (empty_iatt.ia_gfid, loc->gfid) == 0) {
                /* take out the source from namespace */
                ret = syncop_unlink (from, loc);
                if (ret) {
                        gf_log (this->name, GF_LOG_WARNING,
                                "%s: failed to perform unlink on %s (%s)",
                                loc->path, from->name, strerror (-ret));
                        ret = -1;
                        goto out;
                }
        }

        ret = syncop_lookup (this, loc, NULL, NULL, NULL, NULL);
        if (ret) {
                gf_log (this->name, GF_LOG_DEBUG,
                        "%s: failed to lookup the file on subvolumes (%s)",
                        loc->path, strerror (-ret));
                ret = -1;
        }

        gf_log (this->name, GF_LOG_INFO,
                "completed migration of %s from subvolume %s to %s",
                loc->path, from->name, to->name);

        ret = 0;
out:
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
        int           ret        = -1;
        uint64_t      layout_int = 0;
        dht_layout_t *layout     = 0;
        xlator_t     *this       = NULL;
        dht_local_t  *local      = NULL;
        int32_t       op_errno   = EINVAL;

        this = THIS;
        local = sync_frame->local;

        if (!op_ret) {
                /* Make sure we have valid 'layout' in inode ctx
                   after the operation */
                ret = inode_ctx_del (local->loc.inode, this, &layout_int);
                if (!ret && layout_int) {
                        layout = (dht_layout_t *)(long)layout_int;
                        dht_layout_unref (this, layout);
                }

                ret = dht_layout_preset (this, local->rebalance.target_node,
                                         local->loc.inode);
                if (ret)
                        gf_log (this->name, GF_LOG_WARNING,
                                "%s: failed to set inode ctx", local->loc.path);
        }

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
                ret = unlink (cmd_args->sock_file);
                if (ret && (ENOENT == errno)) {
                        ret = 0;
                }
        }

        if (ret) {
                gf_log (this->name, GF_LOG_ERROR, "Failed to unlink listener "
                        "socket %s, error: %s", cmd_args->sock_file,
                        strerror (errno));
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

/* We do a depth first traversal of directories. But before we move into
 * subdirs, we complete the data migration of those directories whose layouts
 * have been fixed
 */

int
gf_defrag_migrate_data (xlator_t *this, gf_defrag_info_t *defrag, loc_t *loc,
                        dict_t *migrate_data)
{
        int                      ret            = -1;
        loc_t                    entry_loc      = {0,};
        fd_t                    *fd             = NULL;
        gf_dirent_t              entries;
        gf_dirent_t             *tmp            = NULL;
        gf_dirent_t             *entry          = NULL;
        gf_boolean_t             free_entries   = _gf_false;
        off_t                    offset         = 0;
        dict_t                  *dict           = NULL;
        struct iatt              iatt           = {0,};
        int32_t                  op_errno       = 0;
        char                    *uuid_str       = NULL;
        uuid_t                   node_uuid      = {0,};
        struct timeval           dir_start      = {0,};
        struct timeval           end            = {0,};
        double                   elapsed        = {0,};
        struct timeval           start          = {0,};
        int32_t                  err            = 0;
        int                      loglevel       = GF_LOG_TRACE;

        gf_log (this->name, GF_LOG_INFO, "migrate data called on %s",
                loc->path);
        gettimeofday (&dir_start, NULL);

        fd = fd_create (loc->inode, defrag->pid);
        if (!fd) {
                gf_log (this->name, GF_LOG_ERROR, "Failed to create fd");
                goto out;
        }

        ret = syncop_opendir (this, loc, fd);
        if (ret) {
                gf_log (this->name, GF_LOG_ERROR, "Failed to open dir %s",
                        loc->path);
                ret = -1;
                goto out;
        }

        INIT_LIST_HEAD (&entries.list);

        while ((ret = syncop_readdirp (this, fd, 131072, offset, NULL,
                                       &entries)) != 0) {

                if (ret < 0) {

                        gf_log (this->name, GF_LOG_ERROR, "Readdir returned %s."
                                " Aborting migrate-data",
                                strerror(-ret));
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

                        if (IA_ISDIR (entry->d_stat.ia_type))
                                continue;

                        defrag->num_files_lookedup++;
                        if (defrag->stats == _gf_true) {
                                gettimeofday (&start, NULL);
                        }
                        if (defrag->defrag_pattern &&
                            (gf_defrag_pattern_match (defrag, entry->d_name,
                                                      entry->d_stat.ia_size)
                             == _gf_false)) {
                                continue;
                        }
                        loc_wipe (&entry_loc);
                        ret =dht_build_child_loc (this, &entry_loc, loc,
                                                  entry->d_name);
                        if (ret) {
                                gf_log (this->name, GF_LOG_ERROR, "Child loc"
                                        " build failed");
                                goto out;
                        }

                        if (uuid_is_null (entry->d_stat.ia_gfid)) {
                                gf_log (this->name, GF_LOG_ERROR, "%s/%s"
                                        " gfid not present", loc->path,
                                         entry->d_name);
                                continue;
                        }

                        uuid_copy (entry_loc.gfid, entry->d_stat.ia_gfid);

                        if (uuid_is_null (loc->gfid)) {
                                gf_log (this->name, GF_LOG_ERROR, "%s/%s"
                                        " gfid not present", loc->path,
                                         entry->d_name);
                                continue;
                        }

                        uuid_copy (entry_loc.pargfid, loc->gfid);

                        entry_loc.inode->ia_type = entry->d_stat.ia_type;

                        ret = syncop_lookup (this, &entry_loc, NULL, &iatt,
                                             NULL, NULL);
                        if (ret) {
                                gf_log (this->name, GF_LOG_ERROR, "%s"
                                        " lookup failed", entry_loc.path);
                                ret = -1;
                                continue;
                        }

                        ret = syncop_getxattr (this, &entry_loc, &dict,
                                               GF_XATTR_NODE_UUID_KEY);
                        if(ret < 0) {
                                gf_log (this->name, GF_LOG_ERROR, "Failed to "
                                        "get node-uuid for %s", entry_loc.path);
                                ret = -1;
                                continue;
                        }

                        ret = dict_get_str (dict, GF_XATTR_NODE_UUID_KEY,
                                            &uuid_str);
                        if(ret < 0) {
                                gf_log (this->name, GF_LOG_ERROR, "Failed to "
                                        "get node-uuid from dict for %s",
                                        entry_loc.path);
                                ret = -1;
                                continue;
                        }

                        if (uuid_parse (uuid_str, node_uuid)) {
                                gf_log (this->name, GF_LOG_ERROR, "uuid_parse "
                                        "failed for %s", entry_loc.path);
                                continue;
                        }

                        /* if file belongs to different node, skip migration
                         * the other node will take responsibility of migration
                         */
                        if (uuid_compare (node_uuid, defrag->node_uuid)) {
                                gf_log (this->name, GF_LOG_TRACE, "%s does not"
                                        "belong to this node", entry_loc.path);
                                continue;
                        }

                        uuid_str = NULL;

                        dict_del (dict, GF_XATTR_NODE_UUID_KEY);


                        /* if distribute is present, it will honor this key.
                         * -1, ENODATA is returned if distribute is not present
                         * or file doesn't have a link-file. If file has
                         * link-file, the path of link-file will be the value,
                         * and also that guarantees that file has to be mostly
                         * migrated */

                        ret = syncop_getxattr (this, &entry_loc, &dict,
                                               GF_XATTR_LINKINFO_KEY);
                        if (ret < 0) {
                                if (-ret != ENODATA) {
                                        loglevel = GF_LOG_ERROR;
                                        defrag->total_failures += 1;
                                } else {
                                        loglevel = GF_LOG_TRACE;
                                }
                                gf_log (this->name, loglevel, "%s: failed to "
                                        "get "GF_XATTR_LINKINFO_KEY" key - %s",
                                        entry_loc.path, strerror (-ret));
                                ret = -1;
                                continue;
                        }

                        ret = syncop_setxattr (this, &entry_loc, migrate_data,
                                               0);
                        if (ret) {
                                err = op_errno;
                                /* errno is overloaded. See
                                 * rebalance_task_completion () */
                                if (err != ENOSPC) {
                                        gf_log (this->name, GF_LOG_DEBUG,
                                                "migrate-data skipped for %s"
                                                " due to space constraints",
                                                entry_loc.path);
                                        defrag->skipped +=1;
                                } else{
                                        gf_log (this->name, GF_LOG_ERROR,
                                                "migrate-data failed for %s",
                                                entry_loc.path);
                                        defrag->total_failures +=1;
                                }
                        }

                        if (ret < 0) {
                                op_errno = -ret;
                                ret = gf_defrag_handle_migrate_error (op_errno,
                                                                      defrag);

                                if (!ret)
                                        gf_log (this->name, GF_LOG_DEBUG,
                                                "migrate-data on %s failed: %s",
                                                entry_loc.path,
                                                strerror (op_errno));
                                else if (ret == 1)
                                        continue;
                                else if (ret == -1)
                                        goto out;
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
                                        "secs", entry_loc.path, iatt.ia_size,
                                         elapsed/1e6);
                        }
                }

                gf_dirent_free (&entries);
                free_entries = _gf_false;
                INIT_LIST_HEAD (&entries.list);
        }

        gettimeofday (&end, NULL);
        elapsed = (end.tv_sec - dir_start.tv_sec) * 1e6 +
                  (end.tv_usec - dir_start.tv_usec);
        gf_log (this->name, GF_LOG_INFO, "Migration operation on dir %s took "
                "%.2f secs", loc->path, elapsed/1e6);
        ret = 0;
out:
        if (free_entries)
                gf_dirent_free (&entries);

        loc_wipe (&entry_loc);

        if (dict)
                dict_unref(dict);

        if (fd)
                fd_unref (fd);
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
        dict_t                  *dict           = NULL;
        off_t                    offset         = 0;
        struct iatt              iatt           = {0,};

        ret = syncop_lookup (this, loc, NULL, &iatt, NULL, NULL);
        if (ret) {
                gf_log (this->name, GF_LOG_ERROR, "Lookup failed on %s",
                        loc->path);
                ret = -1;
                goto out;
        }

        if (defrag->cmd != GF_DEFRAG_CMD_START_LAYOUT_FIX) {
                ret = gf_defrag_migrate_data (this, defrag, loc, migrate_data);
                if (ret)
                        goto out;
        }

        gf_log (this->name, GF_LOG_TRACE, "fix layout called on %s", loc->path);

        fd = fd_create (loc->inode, defrag->pid);
        if (!fd) {
                gf_log (this->name, GF_LOG_ERROR, "Failed to create fd");
                ret = -1;
                goto out;
        }

        ret = syncop_opendir (this, loc, fd);
        if (ret) {
                gf_log (this->name, GF_LOG_ERROR, "Failed to open dir %s",
                        loc->path);
                ret = -1;
                goto out;
        }

        INIT_LIST_HEAD (&entries.list);
        while ((ret = syncop_readdirp (this, fd, 131072, offset, NULL,
                &entries)) != 0)
        {

                if (ret < 0) {
                        gf_log (this->name, GF_LOG_ERROR, "Readdir returned %s"
                                ". Aborting fix-layout",strerror(-ret));
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

                        if (!IA_ISDIR (entry->d_stat.ia_type))
                                continue;

                        loc_wipe (&entry_loc);
                        ret =dht_build_child_loc (this, &entry_loc, loc,
                                                  entry->d_name);
                        if (ret) {
                                gf_log (this->name, GF_LOG_ERROR, "Child loc"
                                        " build failed");
                                goto out;
                        }

                        if (uuid_is_null (entry->d_stat.ia_gfid)) {
                                gf_log (this->name, GF_LOG_ERROR, "%s/%s"
                                        " gfid not present", loc->path,
                                         entry->d_name);
                                continue;
                        }

                        entry_loc.inode->ia_type = entry->d_stat.ia_type;

                        uuid_copy (entry_loc.gfid, entry->d_stat.ia_gfid);
                        if (uuid_is_null (loc->gfid)) {
                                gf_log (this->name, GF_LOG_ERROR, "%s/%s"
                                        " gfid not present", loc->path,
                                         entry->d_name);
                                continue;
                        }

                        uuid_copy (entry_loc.pargfid, loc->gfid);

                        ret = syncop_lookup (this, &entry_loc, NULL, &iatt,
                                             NULL, NULL);
                        if (ret) {
                                gf_log (this->name, GF_LOG_ERROR, "%s"
                                        " lookup failed", entry_loc.path);
                                ret = -1;
                                continue;
                        }

                        ret = syncop_setxattr (this, &entry_loc, fix_layout,
                                               0);
                        if (ret) {
                                gf_log (this->name, GF_LOG_ERROR, "Setxattr "
                                        "failed for %s", entry_loc.path);
                                defrag->defrag_status =
                                GF_DEFRAG_STATUS_FAILED;
                                defrag->total_failures ++;
                                ret = -1;
                                goto out;
                        }
                        ret = gf_defrag_fix_layout (this, defrag, &entry_loc,
                                                    fix_layout, migrate_data);

                        if (ret) {
                                gf_log (this->name, GF_LOG_ERROR, "Fix layout "
                                        "failed for %s", entry_loc.path);
                                defrag->total_failures++;
                                goto out;
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

        if (dict)
                dict_unref(dict);

        if (fd)
                fd_unref (fd);

        return ret;

}


int
gf_defrag_start_crawl (void *data)
{
        xlator_t                *this   = NULL;
        dht_conf_t              *conf   = NULL;
        gf_defrag_info_t        *defrag = NULL;
        int                      ret    = -1;
        loc_t                    loc    = {0,};
        struct iatt              iatt   = {0,};
        struct iatt              parent = {0,};
        dict_t                  *fix_layout = NULL;
        dict_t                  *migrate_data = NULL;
        dict_t                  *status = NULL;
        glusterfs_ctx_t         *ctx = NULL;

        this = data;
        if (!this)
                goto out;

        ctx = this->ctx;
        if (!ctx)
                goto out;

        conf = this->private;
        if (!conf)
                goto out;

        defrag = conf->defrag;
        if (!defrag)
                goto out;

        gettimeofday (&defrag->start_time, NULL);
        dht_build_root_inode (this, &defrag->root_inode);
        if (!defrag->root_inode)
                goto out;

        dht_build_root_loc (defrag->root_inode, &loc);

        /* fix-layout on '/' first */

        ret = syncop_lookup (this, &loc, NULL, &iatt, NULL, &parent);

        if (ret) {
                gf_log (this->name, GF_LOG_ERROR, "look up on / failed");
                ret = -1;
                goto out;
        }

        fix_layout = dict_new ();
        if (!fix_layout) {
                ret = -1;
                goto out;
        }

        ret = dict_set_str (fix_layout, GF_XATTR_FIX_LAYOUT_KEY, "yes");
        if (ret) {
                gf_log (this->name, GF_LOG_ERROR, "Failed to set dict str");
                goto out;
        }

        ret = syncop_setxattr (this, &loc, fix_layout, 0);
        if (ret) {
                gf_log (this->name, GF_LOG_ERROR, "fix layout on %s failed",
                        loc.path);
                defrag->total_failures++;
                ret = -1;
                goto out;
        }

        if (defrag->cmd != GF_DEFRAG_CMD_START_LAYOUT_FIX) {
                migrate_data = dict_new ();
                if (!migrate_data) {
                        ret = -1;
                        goto out;
                }
                if (defrag->cmd == GF_DEFRAG_CMD_START_FORCE)
                        ret = dict_set_str (migrate_data,
                                            "distribute.migrate-data", "force");
                else
                        ret = dict_set_str (migrate_data,
                                            "distribute.migrate-data",
                                            "non-force");
                if (ret)
                        goto out;
        }
        ret = gf_defrag_fix_layout (this, defrag, &loc, fix_layout,
                                    migrate_data);
        if ((defrag->defrag_status != GF_DEFRAG_STATUS_STOPPED) &&
            (defrag->defrag_status != GF_DEFRAG_STATUS_FAILED)) {
                defrag->defrag_status = GF_DEFRAG_STATUS_COMPLETE;
        }



out:
        LOCK (&defrag->lock);
        {
                status = dict_new ();
                gf_defrag_status_get (defrag, status);
                if (ctx->notify)
                        ctx->notify (GF_EN_DEFRAG_STATUS, status);
                if (status)
                        dict_unref (status);
                defrag->is_exiting = 1;
        }
        UNLOCK (&defrag->lock);

        if (defrag) {
                GF_FREE (defrag);
                conf->defrag = NULL;
        }

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

        ret = synctask_new (this->ctx->env, gf_defrag_start_crawl,
                            gf_defrag_done, frame, this);

        if (ret)
                gf_log (this->name, GF_LOG_ERROR, "Could not create"
                        " task for rebalance");
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
        char     *status = "";
        double   elapsed = 0;
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

        gettimeofday (&end, NULL);

        elapsed = end.tv_sec - defrag->start_time.tv_sec;

        if (!dict)
                goto log;

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
        if (elapsed) {
                ret = dict_set_double (dict, "run-time", elapsed);
                if (ret)
                        gf_log (THIS->name, GF_LOG_WARNING,
                                "failed to set run-time");
        }

        ret = dict_set_uint64 (dict, "failures", failures);
        if (ret)
                gf_log (THIS->name, GF_LOG_WARNING,
                        "failed to set failure count");

        ret = dict_set_uint64 (dict, "skipped", skipped);
        if (ret)
                gf_log (THIS->name, GF_LOG_WARNING,
                        "failed to set skipped file count");
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

        gf_log (THIS->name, GF_LOG_INFO, "Rebalance is %s. Time taken is %.2f "
                "secs", status, elapsed);
        gf_log (THIS->name, GF_LOG_INFO, "Files migrated: %"PRIu64", size: %"
                PRIu64", lookups: %"PRIu64", failures: %"PRIu64", skipped: "
                "%"PRIu64, files, size, lookup, failures, skipped);


out:
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

        gf_log ("", GF_LOG_INFO, "Received stop command on rebalance");
        defrag->defrag_status = status;

        if (output)
                gf_defrag_status_get (defrag, output);
        ret = 0;
out:
        gf_log ("", GF_LOG_DEBUG, "Returning %d", ret);
        return ret;
}
