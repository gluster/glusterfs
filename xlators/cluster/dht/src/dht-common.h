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

#include <regex.h>
#include <signal.h>

#include "dht-mem-types.h"
#include "dht-messages.h"
#include "libxlator.h"
#include "syncop.h"

#ifndef _DHT_H
#define _DHT_H

#define GF_XATTR_FIX_LAYOUT_KEY     "distribute.fix.layout"
#define GF_XATTR_FILE_MIGRATE_KEY   "trusted.distribute.migrate-data"
#define GF_DHT_LOOKUP_UNHASHED_ON   1
#define GF_DHT_LOOKUP_UNHASHED_AUTO 2
#define DHT_PATHINFO_HEADER         "DISTRIBUTE:"
#define DHT_FILE_MIGRATE_DOMAIN     "dht.file.migrate"
#define DHT_LAYOUT_HEAL_DOMAIN      "dht.layout.heal"

#include <fnmatch.h>

typedef int (*dht_selfheal_dir_cbk_t) (call_frame_t *frame, void *cookie,
                                       xlator_t     *this,
                                       int32_t       op_ret, int32_t op_errno,
                                       dict_t *xdata);
typedef int (*dht_defrag_cbk_fn_t) (xlator_t        *this, call_frame_t *frame,
                                    int              ret);


struct dht_layout {
        int                spread_cnt;  /* layout spread count per directory,
                                           is controlled by 'setxattr()' with
                                           special key */
        int                cnt;
        int                preset;
        int                gen;
        int                type;
        int                ref; /* use with dht_conf_t->layout_lock */
        gf_boolean_t       search_unhashed;
        struct {
                int        err;   /* 0 = normal
                                     -1 = dir exists and no xattr
                                     >0 = dir lookup failed with errno
                                  */
                uint32_t   start;
                uint32_t   stop;
                xlator_t  *xlator;
        } list[];
};
typedef struct dht_layout  dht_layout_t;

struct dht_stat_time {
        uint32_t        atime;
        uint32_t        atime_nsec;
        uint32_t        ctime;
        uint32_t        ctime_nsec;
        uint32_t        mtime;
        uint32_t        mtime_nsec;
};

typedef struct dht_stat_time dht_stat_time_t;

struct dht_inode_ctx {
        dht_layout_t    *layout;
        dht_stat_time_t  time;
};

typedef struct dht_inode_ctx dht_inode_ctx_t;


typedef enum {
        DHT_HASH_TYPE_DM,
        DHT_HASH_TYPE_DM_USER,
} dht_hashfn_type_t;

/* rebalance related */
struct dht_rebalance_ {
        xlator_t            *from_subvol;
        xlator_t            *target_node;
        off_t                offset;
        size_t               size;
        int32_t              flags;
        int                  count;
        struct iobref       *iobref;
        struct iovec        *vector;
        struct iatt          stbuf;
        dht_defrag_cbk_fn_t  target_op_fn;
        dict_t              *xdata;
};

/**
 * Enum to store decided action based on the qdstatfs (quota-deem-statfs)
 * events
 **/
typedef enum {
        qdstatfs_action_OFF = 0,
        qdstatfs_action_REPLACE,
        qdstatfs_action_NEGLECT,
        qdstatfs_action_COMPARE,
} qdstatfs_action_t;

struct dht_skip_linkto_unlink {

        gf_boolean_t    handle_valid_link;
        int             opend_fd_count;
        xlator_t        *hash_links_to;
        uuid_t          cached_gfid;
        uuid_t          hashed_gfid;
};

typedef struct {
        xlator_t     *xl;
        loc_t         loc;     /* contains/points to inode to lock on. */
        short         type;    /* read/write lock.                     */
        char         *domain;  /* Only locks within a single domain
                                * contend with each other
                                */
        gf_lkowner_t  lk_owner;
        gf_boolean_t  locked;
} dht_lock_t;

typedef
int (*dht_selfheal_layout_t)(call_frame_t *frame, loc_t *loc,
                             dht_layout_t *layout);

typedef
gf_boolean_t (*dht_need_heal_t)(call_frame_t *frame, dht_layout_t **inmem,
                                dht_layout_t **ondisk);

struct dht_local {
        int                      call_cnt;
        loc_t                    loc;
        loc_t                    loc2;
        int                      op_ret;
        int                      op_errno;
        int                      layout_mismatch;
        /* Use stbuf as the postbuf, when we require both
         * pre and post attrs */
        struct iatt              stbuf;
        struct iatt              prebuf;
        struct iatt              preoldparent;
        struct iatt              postoldparent;
        struct iatt              preparent;
        struct iatt              postparent;
        struct statvfs           statvfs;
        fd_t                    *fd;
        inode_t                 *inode;
        dict_t                  *params;
        dict_t                  *xattr;
        dict_t                  *xattr_req;
        dht_layout_t            *layout;
        size_t                   size;
        ino_t                    ia_ino;
        xlator_t                *src_hashed, *src_cached;
        xlator_t                *dst_hashed, *dst_cached;
        xlator_t                *cached_subvol;
        xlator_t                *hashed_subvol;
        char                     need_selfheal;
        int                      file_count;
        int                      dir_count;
        call_frame_t            *main_frame;
        int                      fop_succeeded;
        struct {
                fop_mknod_cbk_t  linkfile_cbk;
                struct iatt      stbuf;
                loc_t            loc;
                inode_t         *inode;
                dict_t          *xattr;
                xlator_t        *srcvol;
        } linkfile;
        struct {
                uint32_t                hole_cnt;
                uint32_t                overlaps_cnt;
                uint32_t                down;
                uint32_t                misc;
                dht_selfheal_dir_cbk_t  dir_cbk;
                dht_selfheal_layout_t   healer;
                dht_need_heal_t         should_heal;
                gf_boolean_t            force_mkdir;
                dht_layout_t           *layout, *refreshed_layout;
        } selfheal;
        uint32_t                 uid;
        uint32_t                 gid;

        /* needed by nufa */
        int32_t flags;
        mode_t  mode;
        dev_t   rdev;
        mode_t  umask;

        /* need for file-info */
        char   *xattr_val;
        char   *key;

        /* which xattr request? */
        char xsel[256];
        int32_t alloc_len;

        char   *newpath;

        /* gfid related */
        uuid_t  gfid;

        /*Marker Related*/
        struct marker_str    marker;

        /* flag used to make sure we need to return estale in
           {lookup,revalidate}_cbk */
        char return_estale;
        char need_lookup_everywhere;

        glusterfs_fop_t      fop;

        gf_boolean_t     linked;
        xlator_t        *link_subvol;

        struct dht_rebalance_ rebalance;
        xlator_t        *first_up_subvol;

        gf_boolean_t     quota_deem_statfs;

        gf_boolean_t     added_link;
        gf_boolean_t     is_linkfile;

        struct dht_skip_linkto_unlink  skip_unlink;

        struct {
                fop_inodelk_cbk_t   inodelk_cbk;
                dht_lock_t        **locks;
                int                 lk_count;

                /* whether locking failed on _any_ of the "locks" above */
                int                 op_ret;
                int                 op_errno;
        } lock;
};
typedef struct dht_local dht_local_t;

/* du - disk-usage */
struct dht_du {
        double   avail_percent;
	double   avail_inodes;
        uint64_t avail_space;
        uint32_t log;
        uint32_t chunks;
};
typedef struct dht_du dht_du_t;

enum gf_defrag_type {
        GF_DEFRAG_CMD_START = 1,
        GF_DEFRAG_CMD_STOP = 1 + 1,
        GF_DEFRAG_CMD_STATUS = 1 + 2,
        GF_DEFRAG_CMD_START_LAYOUT_FIX = 1 + 3,
        GF_DEFRAG_CMD_START_FORCE = 1 + 4,
};
typedef enum gf_defrag_type gf_defrag_type;

enum gf_defrag_status_t {
        GF_DEFRAG_STATUS_NOT_STARTED,
        GF_DEFRAG_STATUS_STARTED,
        GF_DEFRAG_STATUS_STOPPED,
        GF_DEFRAG_STATUS_COMPLETE,
        GF_DEFRAG_STATUS_FAILED,
        GF_DEFRAG_STATUS_LAYOUT_FIX_STARTED,
        GF_DEFRAG_STATUS_LAYOUT_FIX_STOPPED,
        GF_DEFRAG_STATUS_LAYOUT_FIX_COMPLETE,
        GF_DEFRAG_STATUS_LAYOUT_FIX_FAILED,
};
typedef enum gf_defrag_status_t gf_defrag_status_t;

typedef struct gf_defrag_pattern_list gf_defrag_pattern_list_t;

struct gf_defrag_pattern_list {
        char                       path_pattern[256];
        uint64_t                   size;
        gf_defrag_pattern_list_t  *next;
};

struct gf_defrag_info_ {
        uint64_t                     total_files;
        uint64_t                     total_data;
        uint64_t                     num_files_lookedup;
        uint64_t                     total_failures;
        uint64_t                     skipped;
        gf_lock_t                    lock;
        int                          cmd;
        pthread_t                    th;
        gf_defrag_status_t           defrag_status;
        struct rpc_clnt             *rpc;
        uint32_t                     connected;
        uint32_t                     is_exiting;
        pid_t                        pid;
        inode_t                     *root_inode;
        uuid_t                       node_uuid;
        struct timeval               start_time;
        gf_boolean_t                 stats;
        gf_defrag_pattern_list_t    *defrag_pattern;
};

typedef struct gf_defrag_info_ gf_defrag_info_t;

struct dht_conf {
        gf_lock_t      subvolume_lock;
        int            subvolume_cnt;
        xlator_t     **subvolumes;
        char          *subvolume_status;
        int           *last_event;
        dht_layout_t **file_layouts;
        dht_layout_t **dir_layouts;
        gf_boolean_t   search_unhashed;
        int            gen;
        dht_du_t      *du_stats;
        double         min_free_disk;
        double         min_free_inodes;
        char           disk_unit;
        int32_t        refresh_interval;
        gf_boolean_t   unhashed_sticky_bit;
        struct timeval last_stat_fetch;
        gf_lock_t      layout_lock;
        void          *private;     /* Can be used by wrapper xlators over
                                       dht */
        gf_boolean_t   use_readdirp;
        char           vol_uuid[UUID_SIZE + 1];
        gf_boolean_t   assert_no_child_down;
        time_t        *subvol_up_time;

        /* This is the count used as the distribute layout for a directory */
        /* Will be a global flag to control the layout spread count */
        uint32_t       dir_spread_cnt;

        /* to keep track of nodes which are decomissioned */
        xlator_t     **decommissioned_bricks;
        int            decommission_in_progress;
        int            decommission_subvols_cnt;

        /* defrag related */
        gf_defrag_info_t *defrag;

        /* Request to filter directory entries in readdir request */

        gf_boolean_t    readdir_optimize;

        /* Support regex-based name reinterpretation. */
        regex_t         rsync_regex;
        gf_boolean_t    rsync_regex_valid;
        regex_t         extra_regex;
        gf_boolean_t    extra_regex_valid;

        /* Support variable xattr names. */
        char            *xattr_name;
        char            *link_xattr_name;
        char            *wild_xattr_name;

        /* Support size-weighted rebalancing (heterogeneous bricks). */
        gf_boolean_t    do_weighting;
        gf_boolean_t    randomize_by_gfid;

        struct mem_pool *lock_pool;
};
typedef struct dht_conf dht_conf_t;


struct dht_disk_layout {
        uint32_t           cnt;
        uint32_t           type;
        struct {
                uint32_t   start;
                uint32_t   stop;
        } list[1];
};
typedef struct dht_disk_layout dht_disk_layout_t;

typedef enum {
        GF_DHT_MIGRATE_DATA,
        GF_DHT_MIGRATE_DATA_EVEN_IF_LINK_EXISTS,
        GF_DHT_MIGRATE_HARDLINK,
        GF_DHT_MIGRATE_HARDLINK_IN_PROGRESS
} gf_dht_migrate_data_type_t;

#define ENTRY_MISSING(op_ret, op_errno) (op_ret == -1 && op_errno == ENOENT)

#define is_revalidate(loc) (dht_inode_ctx_layout_get (loc->inode, this, NULL) == 0)

#define is_last_call(cnt) (cnt == 0)

#define DHT_MIGRATION_IN_PROGRESS 1
#define DHT_MIGRATION_COMPLETED   2

#define check_is_linkfile(i,s,x,n) (IS_DHT_LINKFILE_MODE (s) && dict_get (x, n))

#define IS_DHT_MIGRATION_PHASE2(buf)  (                                 \
                IA_ISREG ((buf)->ia_type) &&                            \
                ((st_mode_from_ia ((buf)->ia_prot, (buf)->ia_type) &    \
                  ~S_IFMT) == DHT_LINKFILE_MODE))

#define IS_DHT_MIGRATION_PHASE1(buf)  (                                 \
                IA_ISREG ((buf)->ia_type) &&                            \
                ((buf)->ia_prot.sticky == 1) &&                         \
                ((buf)->ia_prot.sgid == 1))

#define DHT_STRIP_PHASE1_FLAGS(buf)  do {                       \
                if ((buf) && IS_DHT_MIGRATION_PHASE1(buf)) {    \
                        (buf)->ia_prot.sticky = 0;              \
                        (buf)->ia_prot.sgid = 0;                \
                }                                               \
        } while (0)

#define dht_inode_missing(op_errno) (op_errno == ENOENT || op_errno == ESTALE)

#define check_is_dir(i,s,x) (IA_ISDIR(s->ia_type))

#define layout_is_sane(layout) ((layout) && (layout->cnt > 0))

#define DHT_STACK_UNWIND(fop, frame, params ...) do {           \
                dht_local_t *__local = NULL;                    \
                xlator_t    *__xl    = NULL;                    \
                if (frame) {                                    \
                        __xl         = frame->this;             \
                        __local      = frame->local;            \
                        frame->local = NULL;                    \
                }                                               \
                STACK_UNWIND_STRICT (fop, frame, params);       \
                dht_local_wipe (__xl, __local);                 \
        } while (0)

#define DHT_STACK_DESTROY(frame) do {                   \
                dht_local_t *__local = NULL;            \
                xlator_t    *__xl    = NULL;            \
                __xl                 = frame->this;     \
                __local              = frame->local;    \
                frame->local         = NULL;            \
                STACK_DESTROY (frame->root);            \
                dht_local_wipe (__xl, __local);         \
        } while (0)

#define DHT_UPDATE_TIME(ctx_sec, ctx_nsec, new_sec, new_nsec, inode, post) do {\
                LOCK (&inode->lock);                                    \
                {                                                       \
                        if (ctx_sec == new_sec)                         \
                                new_nsec = max (new_nsec, ctx_nsec);    \
                        else if (ctx_sec > new_sec) {                   \
                                new_sec = ctx_sec;                      \
                                new_nsec = ctx_nsec;                    \
                        }                                               \
                        if (post) {                                     \
                                ctx_sec = new_sec;                      \
                                ctx_nsec = new_nsec;                    \
                        }                                               \
                }                                                       \
                UNLOCK (&inode->lock);                                  \
        } while (0)

#define is_greater_time(a, an, b, bn) (((a) < (b)) || (((a) == (b)) && ((an) < (bn))))
dht_layout_t                            *dht_layout_new (xlator_t *this, int cnt);
dht_layout_t                            *dht_layout_get (xlator_t *this, inode_t *inode);
dht_layout_t                            *dht_layout_for_subvol (xlator_t *this, xlator_t *subvol);
xlator_t *dht_layout_search (xlator_t   *this, dht_layout_t *layout,
                             const char *name);
int                                      dht_layout_normalize (xlator_t *this, loc_t *loc, dht_layout_t *layout);
int dht_layout_anomalies (xlator_t      *this, loc_t *loc, dht_layout_t *layout,
                          uint32_t      *holes_p, uint32_t *overlaps_p,
                          uint32_t      *missing_p, uint32_t *down_p,
                          uint32_t      *misc_p, uint32_t *no_space_p);
int dht_layout_dir_mismatch (xlator_t   *this, dht_layout_t *layout,
                             xlator_t   *subvol, loc_t *loc, dict_t *xattr);

xlator_t *dht_linkfile_subvol (xlator_t *this, inode_t *inode,
                               struct iatt *buf, dict_t *xattr);
int dht_linkfile_unlink (call_frame_t      *frame, xlator_t *this,
                         xlator_t          *subvol, loc_t *loc);

int dht_layouts_init (xlator_t *this, dht_conf_t *conf);
int dht_layout_merge (xlator_t *this, dht_layout_t *layout, xlator_t *subvol,
                      int       op_ret, int op_errno, dict_t *xattr);

int dht_disk_layout_extract (xlator_t *this, dht_layout_t *layout,
                             int       pos, int32_t **disk_layout_p);
int dht_disk_layout_merge (xlator_t   *this, dht_layout_t *layout,
                           int         pos, void *disk_layout_raw, int disk_layout_len);


int dht_frame_return (call_frame_t *frame);

int                             dht_itransform (xlator_t *this, xlator_t *subvol, uint64_t x, uint64_t *y);
int dht_deitransform (xlator_t *this, uint64_t y, xlator_t **subvol,
                      uint64_t *x);

void dht_local_wipe (xlator_t *this, dht_local_t *local);
dht_local_t *dht_local_init (call_frame_t    *frame, loc_t *loc, fd_t *fd,
                             glusterfs_fop_t  fop);
int dht_iatt_merge (xlator_t                 *this, struct iatt *to, struct iatt *from,
                    xlator_t                 *subvol);

xlator_t *dht_subvol_get_hashed (xlator_t *this, loc_t *loc);
xlator_t *dht_subvol_get_cached (xlator_t *this, inode_t *inode);
xlator_t *dht_subvol_next (xlator_t *this, xlator_t *prev);
xlator_t *dht_subvol_next_available (xlator_t *this, xlator_t *prev);
int       dht_subvol_cnt (xlator_t *this, xlator_t *subvol);

int dht_hash_compute (xlator_t *this, int type, const char *name, uint32_t *hash_p);

int dht_linkfile_create (call_frame_t    *frame, fop_mknod_cbk_t linkfile_cbk,
                         xlator_t        *this, xlator_t *tovol,
                         xlator_t        *fromvol, loc_t *loc);
int                                       dht_lookup_directory (call_frame_t *frame, xlator_t *this, loc_t *loc);
int                                       dht_lookup_everywhere (call_frame_t *frame, xlator_t *this, loc_t *loc);
int
dht_selfheal_directory (call_frame_t     *frame, dht_selfheal_dir_cbk_t cbk,
                        loc_t            *loc, dht_layout_t *layout);

int
dht_selfheal_directory_for_nameless_lookup (call_frame_t  *frame,
                                            dht_selfheal_dir_cbk_t cbk,
                                            loc_t  *loc, dht_layout_t *layout);

int
dht_selfheal_new_directory (call_frame_t *frame, dht_selfheal_dir_cbk_t cbk,
                            dht_layout_t *layout);
int
dht_selfheal_restore (call_frame_t       *frame, dht_selfheal_dir_cbk_t cbk,
                      loc_t              *loc, dht_layout_t *layout);
int
dht_layout_sort_volname (dht_layout_t *layout);

int dht_get_du_info (call_frame_t *frame, xlator_t *this, loc_t *loc);

gf_boolean_t dht_is_subvol_filled (xlator_t *this, xlator_t *subvol);
xlator_t *dht_free_disk_available_subvol (xlator_t *this, xlator_t *subvol,
                                          dht_local_t *layout);
int       dht_get_du_info_for_subvol (xlator_t *this, int subvol_idx);

int dht_layout_preset (xlator_t *this, xlator_t *subvol, inode_t *inode);
int           dht_layout_set (xlator_t *this, inode_t *inode, dht_layout_t *layout);;
void          dht_layout_unref (xlator_t *this, dht_layout_t *layout);
dht_layout_t *dht_layout_ref (xlator_t *this, dht_layout_t *layout);
xlator_t     *dht_first_up_subvol (xlator_t *this);
xlator_t     *dht_last_up_subvol (xlator_t *this);

int dht_build_child_loc (xlator_t *this, loc_t *child, loc_t *parent, char *name);

int dht_filter_loc_subvol_key (xlator_t  *this, loc_t *loc, loc_t *new_loc,
                               xlator_t **subvol);

int                                     dht_rename_cleanup (call_frame_t *frame);
int dht_rename_link_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                         int32_t           op_ret, int32_t op_errno,
                         inode_t          *inode, struct iatt *stbuf,
                         struct iatt      *preparent, struct iatt *postparent,
                         dict_t *xdata);

int dht_fix_directory_layout (call_frame_t *frame,
                              dht_selfheal_dir_cbk_t  dir_cbk,
                              dht_layout_t           *layout);

int dht_init_subvolumes (xlator_t *this, dht_conf_t *conf);

/* migration/rebalance */
int dht_start_rebalance_task (xlator_t *this, call_frame_t *frame);

int dht_rebalance_in_progress_check (xlator_t *this, call_frame_t *frame);
int dht_rebalance_complete_check (xlator_t *this, call_frame_t *frame);


/* FOPS */
int32_t dht_lookup (call_frame_t *frame,
                    xlator_t *this,
                    loc_t    *loc,
                    dict_t   *xattr_req);

int32_t dht_stat (call_frame_t *frame,
                  xlator_t *this,
                  loc_t    *loc, dict_t *xdata);

int32_t dht_fstat (call_frame_t *frame,
                   xlator_t *this,
                   fd_t     *fd, dict_t *xdata);

int32_t dht_truncate (call_frame_t *frame,
                      xlator_t *this,
                      loc_t    *loc,
                      off_t     offset, dict_t *xdata);

int32_t dht_ftruncate (call_frame_t *frame,
                       xlator_t *this,
                       fd_t     *fd,
                       off_t     offset, dict_t *xdata);

int32_t dht_access (call_frame_t *frame,
                    xlator_t *this,
                    loc_t    *loc,
                    int32_t   mask, dict_t *xdata);

int32_t dht_readlink (call_frame_t *frame,
                      xlator_t *this,
                      loc_t    *loc,
                      size_t    size, dict_t *xdata);

int32_t dht_mknod (call_frame_t *frame, xlator_t *this, loc_t *loc,
                   mode_t mode, dev_t rdev, mode_t umask, dict_t *xdata);

int32_t dht_mkdir (call_frame_t *frame, xlator_t *this,
                   loc_t *loc, mode_t mode, mode_t umask, dict_t *xdata);

int32_t dht_unlink (call_frame_t *frame,
                    xlator_t *this,
                    loc_t    *loc, int xflag, dict_t *xdata);

int32_t dht_rmdir (call_frame_t *frame, xlator_t *this,
                   loc_t    *loc, int flags, dict_t *xdata);

int32_t dht_symlink (call_frame_t   *frame, xlator_t *this,
                     const char *linkpath, loc_t *loc, mode_t umask,
                     dict_t *xdata);

int32_t dht_rename (call_frame_t *frame,
                    xlator_t *this,
                    loc_t    *oldloc,
                    loc_t    *newloc, dict_t *xdata);

int32_t dht_link (call_frame_t *frame,
                  xlator_t *this,
                  loc_t    *oldloc,
                  loc_t    *newloc, dict_t *xdata);

int32_t dht_create (call_frame_t *frame, xlator_t *this,
                    loc_t    *loc, int32_t flags, mode_t mode,
                    mode_t umask, fd_t     *fd, dict_t *params);

int32_t dht_open (call_frame_t *frame,
                  xlator_t *this,
                  loc_t    *loc,
                  int32_t   flags, fd_t *fd, dict_t *xdata);

int32_t dht_readv (call_frame_t *frame,
                   xlator_t *this,
                   fd_t     *fd,
                   size_t    size,
                   off_t     offset, uint32_t flags, dict_t *xdata);

int32_t dht_writev (call_frame_t      *frame,
                    xlator_t      *this,
                    fd_t          *fd,
                    struct iovec  *vector,
                    int32_t        count,
                    off_t          offset,
                    uint32_t       flags,
                    struct iobref *iobref, dict_t *xdata);

int32_t dht_flush (call_frame_t *frame,
                   xlator_t *this,
                   fd_t     *fd, dict_t *xdata);

int32_t dht_fsync (call_frame_t *frame,
                   xlator_t *this,
                   fd_t     *fd,
                   int32_t   datasync, dict_t *xdata);

int32_t dht_opendir (call_frame_t *frame,
                     xlator_t *this,
                     loc_t    *loc, fd_t *fd, dict_t *xdata);

int32_t dht_fsyncdir (call_frame_t *frame,
                      xlator_t *this,
                      fd_t     *fd,
                      int32_t   datasync, dict_t *xdata);

int32_t dht_statfs (call_frame_t *frame,
                    xlator_t *this,
                    loc_t    *loc, dict_t *xdata);

int32_t dht_setxattr (call_frame_t *frame,
                      xlator_t *this,
                      loc_t    *loc,
                      dict_t   *dict,
                      int32_t   flags, dict_t *xdata);

int32_t dht_getxattr (call_frame_t   *frame,
                      xlator_t   *this,
                      loc_t      *loc,
                      const char *name, dict_t *xdata);

int32_t dht_fsetxattr (call_frame_t *frame,
                       xlator_t *this,
                       fd_t     *fd,
                       dict_t   *dict,
                       int32_t   flags, dict_t *xdata);

int32_t dht_fgetxattr (call_frame_t   *frame,
                       xlator_t   *this,
                       fd_t       *fd,
                       const char *name, dict_t *xdata);

int32_t dht_removexattr (call_frame_t   *frame,
                         xlator_t   *this,
                         loc_t      *loc,
                         const char *name, dict_t *xdata);
int32_t dht_fremovexattr (call_frame_t   *frame,
                          xlator_t   *this,
                          fd_t      *fd,
                          const char *name, dict_t *xdata);

int32_t dht_lk (call_frame_t        *frame,
                xlator_t        *this,
                fd_t            *fd,
                int32_t          cmd,
                struct gf_flock *flock, dict_t *xdata);

int32_t dht_inodelk (call_frame_t *frame, xlator_t *this,
                     const char      *volume, loc_t *loc, int32_t cmd,
                     struct gf_flock *flock, dict_t *xdata);

int32_t dht_finodelk (call_frame_t        *frame, xlator_t *this,
                      const char      *volume, fd_t *fd, int32_t cmd,
                      struct gf_flock *flock, dict_t *xdata);

int32_t dht_entrylk (call_frame_t    *frame, xlator_t *this,
                     const char  *volume, loc_t *loc, const char *basename,
                     entrylk_cmd  cmd, entrylk_type type, dict_t *xdata);

int32_t dht_fentrylk (call_frame_t    *frame, xlator_t *this,
                      const char  *volume, fd_t *fd, const char *basename,
                      entrylk_cmd  cmd, entrylk_type type, dict_t *xdata);

int32_t dht_readdir (call_frame_t  *frame,
                     xlator_t *this,
                     fd_t     *fd,
                     size_t    size, off_t off, dict_t *xdata);

int32_t dht_readdirp (call_frame_t *frame,
                      xlator_t *this,
                      fd_t     *fd,
                      size_t    size, off_t off, dict_t *dict);

int32_t dht_xattrop (call_frame_t           *frame,
                     xlator_t           *this,
                     loc_t              *loc,
                     gf_xattrop_flags_t  flags,
                     dict_t             *dict, dict_t *xdata);

int32_t dht_fxattrop (call_frame_t *frame,
                      xlator_t           *this,
                      fd_t               *fd,
                      gf_xattrop_flags_t  flags,
                      dict_t             *dict, dict_t *xdata);

int32_t dht_forget (xlator_t *this, inode_t *inode);
int32_t dht_setattr (call_frame_t  *frame, xlator_t *this, loc_t *loc,
                     struct iatt   *stbuf, int32_t valid, dict_t *xdata);
int32_t dht_fsetattr (call_frame_t *frame, xlator_t *this, fd_t *fd,
                      struct iatt  *stbuf, int32_t valid, dict_t *xdata);
int32_t dht_fallocate(call_frame_t *frame, xlator_t *this, fd_t *fd,
		      int32_t mode, off_t offset, size_t len, dict_t *xdata);
int32_t dht_discard(call_frame_t *frame, xlator_t *this, fd_t *fd,
		    off_t offset, size_t len, dict_t *xdata);
int32_t dht_zerofill(call_frame_t *frame, xlator_t *this, fd_t *fd,
                    off_t offset, off_t len, dict_t *xdata);

int32_t dht_init (xlator_t *this);
void    dht_fini (xlator_t *this);
int     dht_reconfigure (xlator_t *this, dict_t *options);
int32_t dht_notify (xlator_t *this, int32_t event, void *data, ...);

/* definitions for nufa/switch */
int dht_revalidate_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                        int op_ret, int op_errno, inode_t *inode,
                        struct iatt *stbuf, dict_t *xattr,
                        struct iatt *postparent);
int dht_lookup_dir_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                        int op_ret, int op_errno, inode_t *inode,
                        struct iatt *stbuf, dict_t *xattr,
                        struct iatt *postparent);
int dht_lookup_linkfile_cbk (call_frame_t *frame, void *cookie,
                             xlator_t *this, int op_ret, int op_errno,
                             inode_t *inode, struct iatt *stbuf, dict_t *xattr,
                             struct iatt *postparent);
int dht_lookup_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                    int op_ret, int op_errno,
                    inode_t *inode, struct iatt *stbuf, dict_t *xattr,
                    struct iatt *postparent);
int dht_create_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                    int op_ret, int op_errno,
                    fd_t *fd, inode_t *inode, struct iatt *stbuf,
                    struct iatt *preparent, struct iatt *postparent,
                    dict_t *xdata);
int dht_newfile_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                     int op_ret, int op_errno,
                     inode_t *inode, struct iatt *stbuf, struct iatt *preparent,
                     struct iatt *postparent, dict_t *xdata);

int
gf_defrag_status_get (gf_defrag_info_t *defrag, dict_t *dict);

int
gf_defrag_stop (gf_defrag_info_t *defrag, gf_defrag_status_t status,
                dict_t *output);

void*
gf_defrag_start (void *this);

int32_t
gf_defrag_handle_hardlink (xlator_t *this, loc_t *loc, dict_t  *xattrs,
                           struct iatt *stbuf);
int
dht_migrate_file (xlator_t *this, loc_t *loc, xlator_t *from, xlator_t *to,
                 int flag);
int
dht_inode_ctx_layout_get (inode_t *inode, xlator_t *this,
                          dht_layout_t **layout_int);
int
dht_inode_ctx_layout_set (inode_t *inode, xlator_t *this,
                          dht_layout_t* layout_int);
int
dht_inode_ctx_time_update (inode_t *inode, xlator_t *this, struct iatt *stat,
                           int32_t update_ctx);
void dht_inode_ctx_time_set (inode_t *inode, xlator_t *this, struct iatt *stat);

int dht_inode_ctx_get (inode_t *inode, xlator_t *this, dht_inode_ctx_t **ctx);
int dht_inode_ctx_set (inode_t *inode, xlator_t *this, dht_inode_ctx_t *ctx);
int
dht_dir_attr_heal (void *data);
int
dht_dir_attr_heal_done (int ret, call_frame_t *sync_frame, void *data);
int
dht_dir_has_layout (dict_t *xattr, char *name);
gf_boolean_t
dht_is_subvol_in_layout (dht_layout_t *layout, xlator_t *xlator);
xlator_t *
dht_subvol_with_free_space_inodes (xlator_t *this, xlator_t *subvol,
                                   dht_layout_t *layout);
xlator_t *
dht_subvol_maxspace_nonzeroinode (xlator_t *this, xlator_t *subvol,
                                  dht_layout_t *layout);
int
dht_linkfile_attr_heal (call_frame_t *frame, xlator_t *this);

void
dht_layout_dump (dht_layout_t  *layout, const char *prefix);
int32_t
dht_priv_dump (xlator_t *this);
int32_t
dht_inodectx_dump (xlator_t *this, inode_t *inode);

int
dht_inode_ctx_get1 (xlator_t *this, inode_t *inode, xlator_t **subvol);

int
dht_subvol_status (dht_conf_t *conf, xlator_t *subvol);

void
dht_log_new_layout_for_dir_selfheal (xlator_t *this, loc_t *loc,
                                     dht_layout_t *layout);
int
dht_lookup_everywhere_done (call_frame_t *frame, xlator_t *this);

int
dht_fill_dict_to_avoid_unlink_of_migrating_file (dict_t *dict);


/* Acquire non-blocking inodelk on a list of xlators.
 *
 * @lk_array: array of lock requests lock on.
 *
 * @lk_count: number of locks in @lk_array
 *
 * @inodelk_cbk: will be called after inodelk replies are received
 *
 * @retval: -1 if stack_winding inodelk fails. 0 otherwise.
 *          inodelk_cbk is called with appropriate error on errors.
 *          On failure to acquire lock on all members of list, successful
 *          locks are unlocked before invoking cbk.
 */

int
dht_nonblocking_inodelk (call_frame_t *frame, dht_lock_t **lk_array,
                         int lk_count, fop_inodelk_cbk_t inodelk_cbk);

/* same as dht_nonblocking_inodelk, but issues sequential blocking locks on
 * @lk_array directly. locks are issued on some order which remains same
 * for a list of xlators (irrespective of order of xlators within list).
 */
int
dht_blocking_inodelk (call_frame_t *frame, dht_lock_t **lk_array,
                      int lk_count, fop_inodelk_cbk_t inodelk_cbk);

int32_t
dht_unlock_inodelk (call_frame_t *frame, dht_lock_t **lk_array, int lk_count,
                    fop_inodelk_cbk_t inodelk_cbk);

dht_lock_t *
dht_lock_new (xlator_t *this, xlator_t *xl, loc_t *loc, short type,
              const char *domain);
void
dht_lock_array_free (dht_lock_t **lk_array, int count);

int32_t
dht_lock_count (dht_lock_t **lk_array, int lk_count);

int
dht_layout_sort (dht_layout_t *layout);

int
dht_layout_missing_dirs (dht_layout_t *layout);

#endif/* _DHT_H */
