/*
  Copyright (c) 2012 DataLab, s.l. <http://www.datalab.es>

  This file is part of the cluster/ec translator for GlusterFS.

  The cluster/ec translator for GlusterFS is free software: you can
  redistribute it and/or modify it under the terms of the GNU General
  Public License as published by the Free Software Foundation, either
  version 3 of the License, or (at your option) any later version.

  The cluster/ec translator for GlusterFS is distributed in the hope
  that it will be useful, but WITHOUT ANY WARRANTY; without even the
  implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
  PURPOSE. See the GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with the cluster/ec translator for GlusterFS. If not, see
  <http://www.gnu.org/licenses/>.
*/

#ifndef __EC_DATA_H__
#define __EC_DATA_H__

#include "xlator.h"

#include "ec.h"

struct _ec_config;
typedef struct _ec_config ec_config_t;

struct _ec_fd;
typedef struct _ec_fd ec_fd_t;

struct _ec_inode;
typedef struct _ec_inode ec_inode_t;

union _ec_cbk;
typedef union _ec_cbk ec_cbk_t;

struct _ec_lock;
typedef struct _ec_lock ec_lock_t;

struct _ec_lock_link;
typedef struct _ec_lock_link ec_lock_link_t;

struct _ec_fop_data;
typedef struct _ec_fop_data ec_fop_data_t;

struct _ec_cbk_data;
typedef struct _ec_cbk_data ec_cbk_data_t;

struct _ec_heal;
typedef struct _ec_heal ec_heal_t;

typedef void (* ec_wind_f)(ec_t *, ec_fop_data_t *, int32_t);
typedef int32_t (* ec_handler_f)(ec_fop_data_t *, int32_t);
typedef void (* ec_resume_f)(ec_fop_data_t *, int32_t);

struct _ec_config
{
    uint32_t version;
    uint8_t  algorithm;
    uint8_t  gf_word_size;
    uint8_t  bricks;
    uint8_t  redundancy;
    uint32_t chunk_size;
};

struct _ec_fd
{
    uintptr_t bad;
    loc_t     loc;
    uintptr_t open;
    int32_t   flags;
};

struct _ec_inode
{
    uintptr_t  bad;
    ec_lock_t *entry_lock;
    ec_lock_t *inode_lock;
    ec_heal_t *heal;
};

typedef int32_t (* fop_heal_cbk_t)(call_frame_t *, void * cookie, xlator_t *,
                                   int32_t, int32_t, uintptr_t, uintptr_t,
                                   uintptr_t, dict_t *);
typedef int32_t (* fop_fheal_cbk_t)(call_frame_t *, void * cookie, xlator_t *,
                                    int32_t, int32_t, uintptr_t, uintptr_t,
                                    uintptr_t, dict_t *);


union _ec_cbk
{
    fop_access_cbk_t       access;
    fop_create_cbk_t       create;
    fop_discard_cbk_t      discard;
    fop_entrylk_cbk_t      entrylk;
    fop_fentrylk_cbk_t     fentrylk;
    fop_fallocate_cbk_t    fallocate;
    fop_flush_cbk_t        flush;
    fop_fsync_cbk_t        fsync;
    fop_fsyncdir_cbk_t     fsyncdir;
    fop_getxattr_cbk_t     getxattr;
    fop_fgetxattr_cbk_t    fgetxattr;
    fop_heal_cbk_t         heal;
    fop_fheal_cbk_t        fheal;
    fop_inodelk_cbk_t      inodelk;
    fop_finodelk_cbk_t     finodelk;
    fop_link_cbk_t         link;
    fop_lk_cbk_t           lk;
    fop_lookup_cbk_t       lookup;
    fop_mkdir_cbk_t        mkdir;
    fop_mknod_cbk_t        mknod;
    fop_open_cbk_t         open;
    fop_opendir_cbk_t      opendir;
    fop_readdir_cbk_t      readdir;
    fop_readdirp_cbk_t     readdirp;
    fop_readlink_cbk_t     readlink;
    fop_readv_cbk_t        readv;
    fop_removexattr_cbk_t  removexattr;
    fop_fremovexattr_cbk_t fremovexattr;
    fop_rename_cbk_t       rename;
    fop_rmdir_cbk_t        rmdir;
    fop_setattr_cbk_t      setattr;
    fop_fsetattr_cbk_t     fsetattr;
    fop_setxattr_cbk_t     setxattr;
    fop_fsetxattr_cbk_t    fsetxattr;
    fop_stat_cbk_t         stat;
    fop_fstat_cbk_t        fstat;
    fop_statfs_cbk_t       statfs;
    fop_symlink_cbk_t      symlink;
    fop_truncate_cbk_t     truncate;
    fop_ftruncate_cbk_t    ftruncate;
    fop_unlink_cbk_t       unlink;
    fop_writev_cbk_t       writev;
    fop_xattrop_cbk_t      xattrop;
    fop_fxattrop_cbk_t     fxattrop;
    fop_zerofill_cbk_t     zerofill;
};

struct _ec_lock
{
    ec_lock_t        **plock;
    gf_timer_t        *timer;
    struct list_head   waiting;
    uintptr_t          mask;
    uintptr_t          good_mask;
    int32_t            kind;
    int32_t            refs;
    int32_t            acquired;
    int32_t            have_size;
    uint64_t           size;
    uint64_t           size_delta;
    uint64_t           version;
    uint64_t           version_delta;
    ec_fop_data_t     *owner;
    loc_t              loc;
    union
    {
        entrylk_type     type;
        struct gf_flock  flock;
    };
};

struct _ec_lock_link
{
    ec_lock_t *      lock;
    ec_fop_data_t *  fop;
    struct list_head wait_list;
};

struct _ec_fop_data
{
    int32_t            id;
    int32_t            refs;
    int32_t            state;
    int32_t            minimum;
    int32_t            expected;
    int32_t            winds;
    int32_t            jobs;
    int32_t            error;
    ec_fop_data_t *    parent;
    xlator_t *         xl;
    call_frame_t *     req_frame;   // frame of the calling xlator
    call_frame_t *     frame;       // frame used by this fop
    struct list_head   cbk_list;    // sorted list of groups of answers
    struct list_head   answer_list; // list of answers
    ec_cbk_data_t *    answer;      // accepted answer
    int32_t            lock_count;
    int32_t            locked;
    ec_lock_link_t     locks[2];
    int32_t            locks_update;
    int32_t            have_size;
    uint64_t           pre_size;
    uint64_t           post_size;
    gf_lock_t          lock;
    ec_config_t        config;

    uint32_t           flags;
    uint32_t           first;
    uintptr_t          mask;
    uintptr_t          remaining;
    uintptr_t          good;
    uintptr_t          bad;

    ec_wind_f          wind;
    ec_handler_f       handler;
    ec_resume_f        resume;
    ec_cbk_t           cbks;
    void *             data;

    uint64_t           user_size;
    uint32_t           head;

    int32_t            use_fd;

    dict_t *           xdata;
    dict_t *           dict;
    int32_t            int32;
    uint32_t           uint32;
    uint64_t           size;
    off_t              offset;
    mode_t             mode[2];
    entrylk_cmd        entrylk_cmd;
    entrylk_type       entrylk_type;
    gf_xattrop_flags_t xattrop_flags;
    dev_t              dev;
    inode_t *          inode;
    fd_t *             fd;
    struct iatt        iatt;
    char *             str[2];
    loc_t              loc[2];
    struct gf_flock    flock;
    struct iovec *     vector;
    struct iobref *    buffers;
};

struct _ec_cbk_data
{
    struct list_head list;        // item in the sorted list of groups
    struct list_head answer_list; // item in the list of answers
    ec_fop_data_t *  fop;
    ec_cbk_data_t *  next;        // next answer in the same group
    int32_t          idx;
    int32_t          op_ret;
    int32_t          op_errno;
    int32_t          count;
    uintptr_t        mask;

    dict_t *         xdata;
    dict_t *         dict;
    int32_t          int32;
    uintptr_t        uintptr[3];
    uint64_t         size;
    uint64_t         version;
    inode_t *        inode;
    fd_t *           fd;
    struct statvfs   statvfs;
    struct iatt      iatt[5];
    struct gf_flock  flock;
    struct iovec *   vector;
    struct iobref *  buffers;
};

struct _ec_heal
{
    gf_lock_t       lock;
    xlator_t *      xl;
    ec_fop_data_t * fop;
    ec_fop_data_t * lookup;
    loc_t           loc;
    struct iatt     iatt;
    char *          symlink;
    fd_t *          fd;
    int32_t         partial;
    int32_t         done;
    uintptr_t       available;
    uintptr_t       good;
    uintptr_t       bad;
    uintptr_t       open;
    uintptr_t       fixed;
    uint64_t        offset;
    uint64_t        size;
    uint64_t        version;
    uint64_t        raw_size;
};

ec_cbk_data_t * ec_cbk_data_allocate(call_frame_t * frame, xlator_t * this,
                                     ec_fop_data_t * fop, int32_t id,
                                     int32_t idx, int32_t op_ret,
                                     int32_t op_errno);
ec_fop_data_t * ec_fop_data_allocate(call_frame_t * frame, xlator_t * this,
                                     int32_t id, uint32_t flags,
                                     uintptr_t target, int32_t minimum,
                                     ec_wind_f wind, ec_handler_f handler,
                                     ec_cbk_t cbks, void * data);
void ec_fop_data_acquire(ec_fop_data_t * fop);
void ec_fop_data_release(ec_fop_data_t * fop);

#endif /* __EC_DATA_H__ */
