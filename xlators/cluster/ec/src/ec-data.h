/*
  Copyright (c) 2012-2014 DataLab, s.l. <http://www.datalab.es>
  This file is part of GlusterFS.

  This file is licensed to you under your choice of the GNU Lesser
  General Public License, version 3 or any later version (LGPLv3 or
  later), or the GNU General Public License, version 2 (GPLv2), in all
  cases as published by the Free Software Foundation.
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
    loc_t     loc;
    uintptr_t open;
    int32_t   flags;
};

struct _ec_inode
{
    ec_lock_t        *inode_lock;
    gf_boolean_t      have_info;
    gf_boolean_t      have_config;
    gf_boolean_t      have_version;
    gf_boolean_t      have_size;
    ec_config_t       config;
    uint64_t          pre_version[2];
    uint64_t          post_version[2];
    uint64_t          pre_size;
    uint64_t          post_size;
    uint64_t          dirty[2];
    struct list_head  heal;
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
    fop_seek_cbk_t         seek;
};

struct _ec_lock
{
    ec_inode_t        *ctx;
    gf_timer_t        *timer;

    /* List of owners of this lock. All fops added to this list are running
     * concurrently. */
    struct list_head   owners;

    /* List of fops waiting to be an owner of the lock. Fops are added to this
     * list when the current owner has an incompatible access (shared vs
     * exclusive) or the lock is not acquired yet. */
    struct list_head   waiting;

    /* List of fops that will wait until the next unlock/lock cycle. This
     * happens when the currently acquired lock is decided to be released as
     * soon as possible. In this case, all frozen fops will be continued only
     * after the lock is reacquired. */
    struct list_head   frozen;

    int32_t            exclusive;
    uintptr_t          mask;
    uintptr_t          good_mask;
    uintptr_t          healing;
    uint32_t           refs_owners;  /* Refs for fops owning the lock */
    uint32_t           refs_pending; /* Refs assigned to fops being prepared */
    gf_boolean_t       acquired;
    gf_boolean_t       getting_size;
    gf_boolean_t       release;
    gf_boolean_t       query;
    fd_t              *fd;
    loc_t              loc;
    union
    {
        entrylk_type     type;
        struct gf_flock  flock;
    };
};

struct _ec_lock_link
{
    ec_lock_t        *lock;
    ec_fop_data_t    *fop;
    struct list_head  owner_list;
    struct list_head  wait_list;
    gf_boolean_t      update[2];
    loc_t            *base;
    uint64_t          size;
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
    ec_fop_data_t     *parent;
    xlator_t          *xl;
    call_frame_t      *req_frame;    /* frame of the calling xlator */
    call_frame_t      *frame;        /* frame used by this fop */
    struct list_head   cbk_list;     /* sorted list of groups of answers */
    struct list_head   answer_list;  /* list of answers */
    struct list_head   pending_list; /* member of ec_t.pending_fops */
    ec_cbk_data_t     *answer;       /* accepted answer */
    int32_t            lock_count;
    int32_t            locked;
    ec_lock_link_t     locks[2];
    int32_t            first_lock;
    gf_lock_t          lock;

    uint32_t           flags;
    uint32_t           first;
    uintptr_t          mask;
    uintptr_t          healing; /*Dispatch is done but call is successful only
                                  if fop->minimum number of subvolumes succeed
                                  which are not healing*/
    uintptr_t          remaining;
    uintptr_t          received; /* Mask of responses */
    uintptr_t          good;

    uid_t              uid;
    gid_t              gid;

    ec_wind_f          wind;
    ec_handler_f       handler;
    ec_resume_f        resume;
    ec_cbk_t           cbks;
    void              *data;
    ec_heal_t         *heal;
    struct list_head   healer;

    uint64_t           user_size;
    uint32_t           head;

    int32_t            use_fd;

    dict_t            *xdata;
    dict_t            *dict;
    int32_t            int32;
    uint32_t           uint32;
    uint64_t           size;
    off_t              offset;
    mode_t             mode[2];
    entrylk_cmd        entrylk_cmd;
    entrylk_type       entrylk_type;
    gf_xattrop_flags_t xattrop_flags;
    dev_t              dev;
    inode_t           *inode;
    fd_t              *fd;
    struct iatt        iatt;
    char              *str[2];
    loc_t              loc[2];
    struct gf_flock    flock;
    struct iovec      *vector;
    struct iobref     *buffers;
    gf_seek_what_t     seek;
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
    uint64_t         dirty[2];

    dict_t *         xdata;
    dict_t *         dict;
    int32_t          int32;
    uintptr_t        uintptr[3];
    uint64_t         size;
    uint64_t         version[2];
    inode_t *        inode;
    fd_t *           fd;
    struct statvfs   statvfs;
    struct iatt      iatt[5];
    struct gf_flock  flock;
    struct iovec *   vector;
    struct iobref *  buffers;
    char            *str;
    gf_dirent_t      entries;
    off_t            offset;
    gf_seek_what_t   what;
};

struct _ec_heal
{
    struct list_head  list;
    gf_lock_t         lock;
    xlator_t         *xl;
    ec_fop_data_t    *fop;
    void             *data;
    ec_fop_data_t    *lookup;
    loc_t             loc;
    struct iatt       iatt;
    char             *symlink;
    fd_t             *fd;
    int32_t           partial;
    int32_t           done;
    int32_t           error;
    gf_boolean_t      nameheal;
    uintptr_t         available;
    uintptr_t         good;
    uintptr_t         bad;
    uintptr_t         open;
    uintptr_t         fixed;
    uint64_t          offset;
    uint64_t          size;
    uint64_t          total_size;
    uint64_t          version[2];
    uint64_t          raw_size;
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

void ec_fop_cleanup(ec_fop_data_t *fop);

#endif /* __EC_DATA_H__ */
