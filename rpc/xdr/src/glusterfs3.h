/*
  Copyright (c) 2007-2012 Red Hat, Inc. <http://www.redhat.com>
  This file is part of GlusterFS.

  This file is licensed to you under your choice of the GNU Lesser
  General Public License, version 3 or any later version (LGPLv3 or
  later), or the GNU General Public License, version 2 (GPLv2), in all
  cases as published by the Free Software Foundation.
*/

#ifndef _GLUSTERFS3_H
#define _GLUSTERFS3_H

#include <sys/uio.h>

#include "xdr-generic.h"
#include "glusterfs3-xdr.h"
#include "iatt.h"
#include "protocol-common.h"
#include "upcall-utils.h"

#define xdr_decoded_remaining_addr(xdr)        ((&xdr)->x_private)
#define xdr_decoded_remaining_len(xdr)         ((&xdr)->x_handy)
#define xdr_encoded_length(xdr) (((size_t)(&xdr)->x_private) - ((size_t)(&xdr)->x_base))
#define xdr_decoded_length(xdr) (((size_t)(&xdr)->x_private) - ((size_t)(&xdr)->x_base))


#define GF_O_ACCMODE           003
#define GF_O_RDONLY             00
#define GF_O_WRONLY             01
#define GF_O_RDWR               02
#define GF_O_CREAT            0100
#define GF_O_EXCL             0200
#define GF_O_NOCTTY           0400
#define GF_O_TRUNC           01000
#define GF_O_APPEND          02000
#define GF_O_NONBLOCK        04000
#define GF_O_SYNC           010000
#define GF_O_ASYNC          020000

#define GF_O_DIRECT         040000
#define GF_O_DIRECTORY     0200000
#define GF_O_NOFOLLOW      0400000
#define GF_O_NOATIME      01000000
#define GF_O_CLOEXEC      02000000

#define GF_O_LARGEFILE     0100000

#define GF_O_FMODE_EXEC        040

#define XLATE_BIT(from, to, bit)    do {                \
                if (from & bit)                         \
                        to = to | GF_##bit;             \
        } while (0)

#define UNXLATE_BIT(from, to, bit)  do {                \
                if (from & GF_##bit)                    \
                        to = to | bit;                  \
        } while (0)

#define XLATE_ACCESSMODE(from, to) do {                 \
                switch (from & O_ACCMODE) {             \
                case O_RDONLY: to |= GF_O_RDONLY;       \
                        break;                          \
                case O_WRONLY: to |= GF_O_WRONLY;       \
                        break;                          \
                case O_RDWR: to |= GF_O_RDWR;           \
                        break;                          \
                }                                       \
        } while (0)

#define UNXLATE_ACCESSMODE(from, to) do {               \
                switch (from & GF_O_ACCMODE) {          \
                case GF_O_RDONLY: to |= O_RDONLY;       \
                        break;                          \
                case GF_O_WRONLY: to |= O_WRONLY;       \
                        break;                          \
                case GF_O_RDWR: to |= O_RDWR;           \
                        break;                          \
                }                                       \
        } while (0)

static inline uint32_t
gf_flags_from_flags (uint32_t flags)
{
        uint32_t gf_flags = 0;

        XLATE_ACCESSMODE (flags, gf_flags);

        XLATE_BIT (flags, gf_flags, O_CREAT);
        XLATE_BIT (flags, gf_flags, O_EXCL);
        XLATE_BIT (flags, gf_flags, O_NOCTTY);
        XLATE_BIT (flags, gf_flags, O_TRUNC);
        XLATE_BIT (flags, gf_flags, O_APPEND);
        XLATE_BIT (flags, gf_flags, O_NONBLOCK);
        XLATE_BIT (flags, gf_flags, O_SYNC);
        XLATE_BIT (flags, gf_flags, O_ASYNC);

        XLATE_BIT (flags, gf_flags, O_DIRECT);
        XLATE_BIT (flags, gf_flags, O_DIRECTORY);
        XLATE_BIT (flags, gf_flags, O_NOFOLLOW);
#ifdef O_NOATIME
        XLATE_BIT (flags, gf_flags, O_NOATIME);
#endif
#ifdef O_CLOEXEC
        XLATE_BIT (flags, gf_flags, O_CLOEXEC);
#endif
        XLATE_BIT (flags, gf_flags, O_LARGEFILE);
        XLATE_BIT (flags, gf_flags, O_FMODE_EXEC);

        return gf_flags;
}

static inline uint32_t
gf_flags_to_flags (uint32_t gf_flags)
{
        uint32_t flags = 0;

        UNXLATE_ACCESSMODE (gf_flags, flags);

        UNXLATE_BIT (gf_flags, flags, O_CREAT);
        UNXLATE_BIT (gf_flags, flags, O_EXCL);
        UNXLATE_BIT (gf_flags, flags, O_NOCTTY);
        UNXLATE_BIT (gf_flags, flags, O_TRUNC);
        UNXLATE_BIT (gf_flags, flags, O_APPEND);
        UNXLATE_BIT (gf_flags, flags, O_NONBLOCK);
        UNXLATE_BIT (gf_flags, flags, O_SYNC);
        UNXLATE_BIT (gf_flags, flags, O_ASYNC);

        UNXLATE_BIT (gf_flags, flags, O_DIRECT);
        UNXLATE_BIT (gf_flags, flags, O_DIRECTORY);
        UNXLATE_BIT (gf_flags, flags, O_NOFOLLOW);
#ifdef O_NOATIME
        UNXLATE_BIT (gf_flags, flags, O_NOATIME);
#endif
#ifdef O_CLOEXEC
        UNXLATE_BIT (gf_flags, flags, O_CLOEXEC);
#endif
        UNXLATE_BIT (gf_flags, flags, O_LARGEFILE);
        UNXLATE_BIT (gf_flags, flags, O_FMODE_EXEC);

        return flags;
}


static inline void
gf_statfs_to_statfs (struct gf_statfs *gf_stat, struct statvfs *stat)
{
        if (!stat || !gf_stat)
                return;

	stat->f_bsize   =  (gf_stat->bsize);
	stat->f_frsize  =  (gf_stat->frsize);
	stat->f_blocks  =  (gf_stat->blocks);
	stat->f_bfree   =  (gf_stat->bfree);
	stat->f_bavail  =  (gf_stat->bavail);
	stat->f_files   =  (gf_stat->files);
	stat->f_ffree   =  (gf_stat->ffree);
	stat->f_favail  =  (gf_stat->favail);
	stat->f_fsid    =  (gf_stat->fsid);
	stat->f_flag    =  (gf_stat->flag);
	stat->f_namemax =  (gf_stat->namemax);
}


static inline void
gf_statfs_from_statfs (struct gf_statfs *gf_stat, struct statvfs *stat)
{
        if (!stat || !gf_stat)
                return;

	gf_stat->bsize   = stat->f_bsize;
	gf_stat->frsize  = stat->f_frsize;
	gf_stat->blocks  = stat->f_blocks;
	gf_stat->bfree   = stat->f_bfree;
	gf_stat->bavail  = stat->f_bavail;
	gf_stat->files   = stat->f_files;
	gf_stat->ffree   = stat->f_ffree;
	gf_stat->favail  = stat->f_favail;
	gf_stat->fsid    = stat->f_fsid;
	gf_stat->flag    = stat->f_flag;
	gf_stat->namemax = stat->f_namemax;
}

static inline void
gf_proto_lease_to_lease (struct gf_proto_lease *gf_proto_lease, struct gf_lease *gf_lease)
{
        if (!gf_lease || !gf_proto_lease)
                return;

        gf_lease->cmd        = gf_proto_lease->cmd;
        gf_lease->lease_type = gf_proto_lease->lease_type;
        memcpy (gf_lease->lease_id, gf_proto_lease->lease_id, LEASE_ID_SIZE);
}

static inline void
gf_proto_lease_from_lease (struct gf_proto_lease *gf_proto_lease, struct gf_lease *gf_lease)
{
        if (!gf_lease || !gf_proto_lease)
                return;

        gf_proto_lease->cmd  = gf_lease->cmd;
        gf_proto_lease->lease_type = gf_lease->lease_type;
        memcpy (gf_proto_lease->lease_id, gf_lease->lease_id, LEASE_ID_SIZE);
}

static inline int
gf_proto_recall_lease_to_upcall (struct gfs3_recall_lease_req *recall_lease,
                                 struct gf_upcall *gf_up_data)
{
        struct gf_upcall_recall_lease *tmp = NULL;
        int    ret                         = 0;

        GF_VALIDATE_OR_GOTO(THIS->name, recall_lease, out);
        GF_VALIDATE_OR_GOTO(THIS->name, gf_up_data, out);

        tmp = (struct gf_upcall_recall_lease *)gf_up_data->data;
        tmp->lease_type = recall_lease->lease_type;
        memcpy (gf_up_data->gfid, recall_lease->gfid, 16);
        memcpy (tmp->tid, recall_lease->tid, 16);

        GF_PROTOCOL_DICT_UNSERIALIZE (THIS, tmp->dict,
                                      (recall_lease->xdata).xdata_val,
                                      (recall_lease->xdata).xdata_len, ret,
                                      errno, out);
out:
        return ret;

}

static inline int
gf_proto_recall_lease_from_upcall (xlator_t *this,
                                   struct gfs3_recall_lease_req *recall_lease,
                                   struct gf_upcall *gf_up_data)
{
        struct gf_upcall_recall_lease *tmp = NULL;
        int    ret                         = 0;

        GF_VALIDATE_OR_GOTO(this->name, recall_lease, out);
        GF_VALIDATE_OR_GOTO(this->name, gf_up_data, out);

        tmp = (struct gf_upcall_recall_lease *)gf_up_data->data;
        recall_lease->lease_type = tmp->lease_type;
        memcpy (recall_lease->gfid, gf_up_data->gfid, 16);
        memcpy (recall_lease->tid, tmp->tid, 16);

        GF_PROTOCOL_DICT_SERIALIZE (this, tmp->dict,
                                    &(recall_lease->xdata).xdata_val,
                                    (recall_lease->xdata).xdata_len, ret, out);
out:
        return ret;

}

static inline void
gf_proto_flock_to_flock (struct gf_proto_flock *gf_proto_flock, struct gf_flock *gf_flock)
{
        if (!gf_flock || !gf_proto_flock)
                return;

	gf_flock->l_type     = gf_proto_flock->type;
	gf_flock->l_whence   = gf_proto_flock->whence;
	gf_flock->l_start    = gf_proto_flock->start;
	gf_flock->l_len      = gf_proto_flock->len;
	gf_flock->l_pid      = gf_proto_flock->pid;
        gf_flock->l_owner.len = gf_proto_flock->lk_owner.lk_owner_len;
        if (gf_flock->l_owner.len &&
            (gf_flock->l_owner.len < GF_MAX_LOCK_OWNER_LEN))
                memcpy (gf_flock->l_owner.data, gf_proto_flock->lk_owner.lk_owner_val,
                        gf_flock->l_owner.len);
}


static inline void
gf_proto_flock_from_flock (struct gf_proto_flock *gf_proto_flock, struct gf_flock *gf_flock)
{
        if (!gf_flock || !gf_proto_flock)
                return;

	gf_proto_flock->type     =  (gf_flock->l_type);
	gf_proto_flock->whence   =  (gf_flock->l_whence);
	gf_proto_flock->start    =  (gf_flock->l_start);
	gf_proto_flock->len      =  (gf_flock->l_len);
	gf_proto_flock->pid      =  (gf_flock->l_pid);
	gf_proto_flock->lk_owner.lk_owner_len =  gf_flock->l_owner.len;
        if (gf_flock->l_owner.len)
                gf_proto_flock->lk_owner.lk_owner_val = gf_flock->l_owner.data;
}

static inline void
gf_stat_to_iatt (struct gf_iatt *gf_stat, struct iatt *iatt)
{
        if (!iatt || !gf_stat)
                return;

        memcpy (iatt->ia_gfid, gf_stat->ia_gfid, 16);
	iatt->ia_ino = gf_stat->ia_ino ;
	iatt->ia_dev = gf_stat->ia_dev ;
	iatt->ia_type = ia_type_from_st_mode (gf_stat->mode) ;
	iatt->ia_prot = ia_prot_from_st_mode (gf_stat->mode) ;
	iatt->ia_nlink = gf_stat->ia_nlink ;
	iatt->ia_uid = gf_stat->ia_uid ;
	iatt->ia_gid = gf_stat->ia_gid ;
	iatt->ia_rdev = gf_stat->ia_rdev ;
	iatt->ia_size = gf_stat->ia_size ;
	iatt->ia_blksize = gf_stat->ia_blksize ;
	iatt->ia_blocks = gf_stat->ia_blocks ;
	iatt->ia_atime = gf_stat->ia_atime ;
	iatt->ia_atime_nsec = gf_stat->ia_atime_nsec ;
	iatt->ia_mtime = gf_stat->ia_mtime ;
	iatt->ia_mtime_nsec = gf_stat->ia_mtime_nsec ;
	iatt->ia_ctime = gf_stat->ia_ctime ;
	iatt->ia_ctime_nsec = gf_stat->ia_ctime_nsec ;
}


static inline void
gf_stat_from_iatt (struct gf_iatt *gf_stat, struct iatt *iatt)
{
        if (!iatt || !gf_stat)
                return;

        memcpy (gf_stat->ia_gfid, iatt->ia_gfid, 16);
	gf_stat->ia_ino = iatt->ia_ino ;
	gf_stat->ia_dev = iatt->ia_dev ;
	gf_stat->mode   = st_mode_from_ia (iatt->ia_prot, iatt->ia_type);
	gf_stat->ia_nlink = iatt->ia_nlink ;
	gf_stat->ia_uid = iatt->ia_uid ;
	gf_stat->ia_gid = iatt->ia_gid ;
	gf_stat->ia_rdev = iatt->ia_rdev ;
	gf_stat->ia_size = iatt->ia_size ;
	gf_stat->ia_blksize = iatt->ia_blksize ;
	gf_stat->ia_blocks = iatt->ia_blocks ;
	gf_stat->ia_atime = iatt->ia_atime ;
	gf_stat->ia_atime_nsec = iatt->ia_atime_nsec ;
	gf_stat->ia_mtime = iatt->ia_mtime ;
	gf_stat->ia_mtime_nsec = iatt->ia_mtime_nsec ;
	gf_stat->ia_ctime = iatt->ia_ctime ;
	gf_stat->ia_ctime_nsec = iatt->ia_ctime_nsec ;
}

static inline int
gf_proto_cache_invalidation_from_upcall (xlator_t *this,
                                         gfs3_cbk_cache_invalidation_req *gf_c_req,
                                         struct gf_upcall *gf_up_data)
{
        struct gf_upcall_cache_invalidation *gf_c_data = NULL;
        int    is_cache_inval                          = 0;
        int    ret                                     = -1;

        GF_VALIDATE_OR_GOTO(this->name, gf_c_req, out);
        GF_VALIDATE_OR_GOTO(this->name, gf_up_data, out);

        is_cache_inval = ((gf_up_data->event_type ==
                          GF_UPCALL_CACHE_INVALIDATION) ? 1 : 0);
        GF_VALIDATE_OR_GOTO(this->name, is_cache_inval, out);

        gf_c_data = (struct gf_upcall_cache_invalidation *)gf_up_data->data;
        GF_VALIDATE_OR_GOTO(this->name, gf_c_data, out);

        gf_c_req->gfid = uuid_utoa (gf_up_data->gfid);
        gf_c_req->event_type       = gf_up_data->event_type;
        gf_c_req->flags            = gf_c_data->flags;
        gf_c_req->expire_time_attr = gf_c_data->expire_time_attr;
        gf_stat_from_iatt (&gf_c_req->stat, &gf_c_data->stat);
        gf_stat_from_iatt (&gf_c_req->parent_stat, &gf_c_data->p_stat);
        gf_stat_from_iatt (&gf_c_req->oldparent_stat, &gf_c_data->oldp_stat);

        ret = 0;
        GF_PROTOCOL_DICT_SERIALIZE (this, gf_c_data->dict, &(gf_c_req->xdata).xdata_val,
                                    (gf_c_req->xdata).xdata_len, ret, out);
out:
        return ret;
}

static inline int
gf_proto_cache_invalidation_to_upcall (xlator_t *this,
                                       gfs3_cbk_cache_invalidation_req *gf_c_req,
                                       struct gf_upcall *gf_up_data)
{
        struct gf_upcall_cache_invalidation *gf_c_data = NULL;
        int    ret                                     = -1;

        GF_VALIDATE_OR_GOTO(this->name, gf_c_req, out);
        GF_VALIDATE_OR_GOTO(this->name, gf_up_data, out);

        gf_c_data = (struct gf_upcall_cache_invalidation *)gf_up_data->data;
        GF_VALIDATE_OR_GOTO(this->name, gf_c_data, out);

        ret = gf_uuid_parse (gf_c_req->gfid, gf_up_data->gfid);
        if (ret) {
                gf_log (this->name, GF_LOG_WARNING, "gf_uuid_parse(%s) failed",
                        gf_c_req->gfid);
                gf_up_data->event_type = GF_UPCALL_EVENT_NULL;
                goto out;
        }

        gf_up_data->event_type      = gf_c_req->event_type;

        gf_c_data->flags            = gf_c_req->flags;
        gf_c_data->expire_time_attr = gf_c_req->expire_time_attr;
        gf_stat_to_iatt (&gf_c_req->stat, &gf_c_data->stat);
        gf_stat_to_iatt (&gf_c_req->parent_stat, &gf_c_data->p_stat);
        gf_stat_to_iatt (&gf_c_req->oldparent_stat, &gf_c_data->oldp_stat);

        ret = 0;
        GF_PROTOCOL_DICT_UNSERIALIZE (this, gf_c_data->dict,
                                      (gf_c_req->xdata).xdata_val,
                                      (gf_c_req->xdata).xdata_len, ret,
                                      ret, out);

        /* If no dict was sent, create an empty dict, so that each xlator
         * need not check if empty then create new dict. Will be unref'd by the
         * caller */
        if (!gf_c_data->dict)
                gf_c_data->dict = dict_new ();
  out:
        return ret;
}
#endif /* !_GLUSTERFS3_H */
