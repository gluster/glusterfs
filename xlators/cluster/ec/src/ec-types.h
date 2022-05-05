/*
  Copyright (c) 2015 DataLab, s.l. <http://www.datalab.es>
  This file is part of GlusterFS.

  This file is licensed to you under your choice of the GNU Lesser
  General Public License, version 3 or any later version (LGPLv3 or
  later), or the GNU General Public License, version 2 (GPLv2), in all
  cases as published by the Free Software Foundation.
*/

#ifndef __EC_TYPES_H__
#define __EC_TYPES_H__

#include <glusterfs/timer.h>
#include <glusterfs/syncop.h>
#include "libxlator.h"
#include <glusterfs/atomic.h>

#define EC_GF_MAX_REGS 16

enum _ec_heal_need;
typedef enum _ec_heal_need ec_heal_need_t;

enum _ec_stripe_part;
typedef enum _ec_stripe_part ec_stripe_part_t;

enum _ec_read_policy;
typedef enum _ec_read_policy ec_read_policy_t;

struct _ec_config;
typedef struct _ec_config ec_config_t;

struct _ec_fd;
typedef struct _ec_fd ec_fd_t;

struct _ec_fragment_range;
typedef struct _ec_fragment_range ec_fragment_range_t;

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

enum _ec_gf_opcode;
typedef enum _ec_gf_opcode ec_gf_opcode_t;

struct _ec_gf_op;
typedef struct _ec_gf_op ec_gf_op_t;

struct _ec_gf_mul;
typedef struct _ec_gf_mul ec_gf_mul_t;

struct _ec_gf;
typedef struct _ec_gf ec_gf_t;

struct _ec_code_gen;
typedef struct _ec_code_gen ec_code_gen_t;

struct _ec_code;
typedef struct _ec_code ec_code_t;

struct _ec_code_arg;
typedef struct _ec_code_arg ec_code_arg_t;

struct _ec_code_op;
typedef struct _ec_code_op ec_code_op_t;

struct _ec_code_builder;
typedef struct _ec_code_builder ec_code_builder_t;

struct _ec_code_chunk;
typedef struct _ec_code_chunk ec_code_chunk_t;

struct _ec_stripe;
typedef struct _ec_stripe ec_stripe_t;

struct _ec_stripe_list;
typedef struct _ec_stripe_list ec_stripe_list_t;

struct _ec_code_space;
typedef struct _ec_code_space ec_code_space_t;

typedef void (*ec_code_func_linear_t)(void *dst, void *src, uint64_t offset,
                                      uint32_t *values, uint32_t count);

typedef void (*ec_code_func_interleaved_t)(void *dst, void **src,
                                           uint64_t offset, uint32_t *values,
                                           uint32_t count);

union _ec_code_func;
typedef union _ec_code_func ec_code_func_t;

struct _ec_matrix_row;
typedef struct _ec_matrix_row ec_matrix_row_t;

struct _ec_matrix;
typedef struct _ec_matrix ec_matrix_t;

struct _ec_matrix_list;
typedef struct _ec_matrix_list ec_matrix_list_t;

struct _ec_heal;
typedef struct _ec_heal ec_heal_t;

struct _ec_self_heald;
typedef struct _ec_self_heald ec_self_heald_t;

struct _ec_statistics;
typedef struct _ec_statistics ec_statistics_t;

struct _ec;
typedef struct _ec ec_t;

typedef void (*ec_wind_f)(ec_t *, ec_fop_data_t *, int32_t);
typedef int32_t (*ec_handler_f)(ec_fop_data_t *, int32_t);
typedef void (*ec_resume_f)(ec_fop_data_t *, int32_t);

enum _ec_read_policy { EC_ROUND_ROBIN, EC_GFID_HASH, EC_READ_POLICY_MAX };

enum _ec_heal_need {
    EC_HEAL_NONEED,
    EC_HEAL_MAYBE,
    EC_HEAL_MUST,
    EC_HEAL_PURGE_INDEX
};

enum _ec_stripe_part { EC_STRIPE_HEAD, EC_STRIPE_TAIL };

/* Enumartions to indicate FD status. */
typedef enum { EC_FD_NOT_OPENED, EC_FD_OPENED, EC_FD_OPENING } ec_fd_status_t;

struct _ec_config {
    uint32_t version;
    uint8_t algorithm;
    uint8_t gf_word_size;
    uint8_t bricks;
    uint8_t redundancy;
    uint32_t chunk_size;
};

struct _ec_fd {
    loc_t loc;
    uintptr_t open;
    int32_t flags;
    uint64_t bad_version;
    ec_fd_status_t fd_status[0];
};

struct _ec_stripe {
    struct list_head lru; /* LRU list member */
    uint64_t frag_offset; /* Fragment offset of this stripe */
    char data[];          /* Contents of the stripe */
};

struct _ec_stripe_list {
    struct list_head lru;
    uint32_t count;
    uint32_t max;
};

struct _ec_inode {
    ec_lock_t *inode_lock;
    gf_boolean_t have_info;
    gf_boolean_t have_config;
    gf_boolean_t have_version;
    gf_boolean_t have_size;
    int32_t heal_count;
    ec_config_t config;
    uint64_t pre_version[2];
    uint64_t post_version[2];
    uint64_t pre_size;
    uint64_t post_size;
    uint64_t dirty[2];
    struct list_head heal;
    ec_stripe_list_t stripe_cache;
    uint64_t bad_version;
};

typedef int32_t (*fop_heal_cbk_t)(call_frame_t *, void *, xlator_t *, int32_t,
                                  int32_t, uintptr_t, uintptr_t, uintptr_t,
                                  uint32_t, dict_t *);
typedef int32_t (*fop_fheal_cbk_t)(call_frame_t *, void *, xlator_t *, int32_t,
                                   int32_t, uintptr_t, uintptr_t, uintptr_t,
                                   uint32_t, dict_t *);

union _ec_cbk {
    fop_access_cbk_t access;
    fop_create_cbk_t create;
    fop_discard_cbk_t discard;
    fop_entrylk_cbk_t entrylk;
    fop_fentrylk_cbk_t fentrylk;
    fop_fallocate_cbk_t fallocate;
    fop_flush_cbk_t flush;
    fop_fsync_cbk_t fsync;
    fop_fsyncdir_cbk_t fsyncdir;
    fop_getxattr_cbk_t getxattr;
    fop_fgetxattr_cbk_t fgetxattr;
    fop_heal_cbk_t heal;
    fop_fheal_cbk_t fheal;
    fop_inodelk_cbk_t inodelk;
    fop_finodelk_cbk_t finodelk;
    fop_link_cbk_t link;
    fop_lk_cbk_t lk;
    fop_lookup_cbk_t lookup;
    fop_mkdir_cbk_t mkdir;
    fop_mknod_cbk_t mknod;
    fop_open_cbk_t open;
    fop_opendir_cbk_t opendir;
    fop_readdir_cbk_t readdir;
    fop_readdirp_cbk_t readdirp;
    fop_readlink_cbk_t readlink;
    fop_readv_cbk_t readv;
    fop_removexattr_cbk_t removexattr;
    fop_fremovexattr_cbk_t fremovexattr;
    fop_rename_cbk_t rename;
    fop_rmdir_cbk_t rmdir;
    fop_setattr_cbk_t setattr;
    fop_fsetattr_cbk_t fsetattr;
    fop_setxattr_cbk_t setxattr;
    fop_fsetxattr_cbk_t fsetxattr;
    fop_stat_cbk_t stat;
    fop_fstat_cbk_t fstat;
    fop_statfs_cbk_t statfs;
    fop_symlink_cbk_t symlink;
    fop_truncate_cbk_t truncate;
    fop_ftruncate_cbk_t ftruncate;
    fop_unlink_cbk_t unlink;
    fop_writev_cbk_t writev;
    fop_xattrop_cbk_t xattrop;
    fop_fxattrop_cbk_t fxattrop;
    fop_zerofill_cbk_t zerofill;
    fop_seek_cbk_t seek;
    fop_ipc_cbk_t ipc;
};

struct _ec_lock {
    ec_inode_t *ctx;
    gf_timer_t *timer;

    /* List of owners of this lock. All fops added to this list are running
     * concurrently. */
    struct list_head owners;

    /* List of fops waiting to be an owner of the lock. Fops are added to this
     * list when the current owner has an incompatible access (conflicting lock)
     * or the lock is not acquired yet. */
    struct list_head waiting;

    /* List of fops that will wait until the next unlock/lock cycle. This
     * happens when the currently acquired lock is decided to be released as
     * soon as possible. In this case, all frozen fops will be continued only
     * after the lock is reacquired. */
    struct list_head frozen;

    uintptr_t mask;
    uintptr_t good_mask;
    uintptr_t healing;
    uint32_t refs_owners;   /* Refs for fops owning the lock */
    uint32_t refs_pending;  /* Refs assigned to fops being prepared */
    uint32_t waiting_flags; /*Track xattrop/dirty marking*/
    gf_boolean_t acquired;
    gf_boolean_t contention;
    gf_boolean_t unlock_now;
    gf_boolean_t release;
    gf_boolean_t query;
    fd_t *fd;
    loc_t loc;
    union {
        entrylk_type type;
        struct gf_flock flock;
    };
};

struct _ec_lock_link {
    ec_lock_t *lock;
    ec_fop_data_t *fop;
    struct list_head owner_list;
    struct list_head wait_list;
    gf_boolean_t update[2];
    gf_boolean_t dirty[2];
    gf_boolean_t optimistic_changelog;
    loc_t *base;
    uint64_t size;
    uint32_t waiting_flags;
    off_t fl_start;
    off_t fl_end;
};

/* This structure keeps a range of fragment offsets affected by a fop. Since
 * real file offsets can be difficult to handle correctly because of overflows,
 * we use the 'scaled' offset, which corresponds to the offset of the fragment
 * seen by the bricks, which is always smaller and cannot overflow. */
struct _ec_fragment_range {
    uint64_t first; /* Address of the first affected fragment as seen by the
                       bricks (offset on brick) */
    uint64_t last;  /* Address of the first non affected fragment as seen by
                       the bricks (offset on brick) */
};

/* EC xlator data structure to collect all the data required to perform
 * the file operation.*/
struct _ec_fop_data {
    int32_t id; /* ID of the file operation */
    int32_t refs;
    int32_t state;
    uint32_t minimum; /* Minimum number of successful
                         operation required to conclude a
                         fop as successful */
    int32_t expected;
    int32_t winds;
    int32_t jobs;
    int32_t error;
    ec_fop_data_t *parent;
    xlator_t *xl;                  /* points to EC xlator */
    call_frame_t *req_frame;       /* frame of the calling xlator */
    call_frame_t *frame;           /* frame used by this fop */
    struct list_head cbk_list;     /* sorted list of groups of answers */
    struct list_head answer_list;  /* list of answers */
    struct list_head pending_list; /* member of ec_t.pending_fops */
    ec_cbk_data_t *answer;         /* accepted answer */
    int32_t lock_count;
    int32_t locked;
    gf_lock_t lock;
    ec_lock_link_t locks[2];
    int32_t first_lock;

    uint32_t fop_flags; /* Flags passed by the caller. */
    uint32_t flags;     /* Internal flags. */
    uint32_t first;
    uintptr_t mask;
    uintptr_t healing; /*Dispatch is done but call is successful only
                         if fop->minimum number of subvolumes succeed
                         which are not healing*/
    uintptr_t remaining;
    uintptr_t received; /* Mask of responses */
    uintptr_t good;

    uid_t uid;
    gid_t gid;

    ec_wind_f wind;       /* Function to wind to */
    ec_handler_f handler; /* FOP manager function */
    ec_resume_f resume;
    ec_cbk_t cbks; /* Callback function for this FOP */
    void *data;
    ec_heal_t *heal;
    struct list_head healer;

    uint64_t user_size;
    uint32_t head;

    int32_t use_fd; /* Indicates whether this FOP uses FD or
                       not */

    dict_t *xdata;
    dict_t *dict;
    int32_t int32;
    uint32_t uint32;
    uint64_t size;
    off_t offset;
    mode_t mode[2];
    entrylk_cmd entrylk_cmd;
    entrylk_type entrylk_type;
    gf_xattrop_flags_t xattrop_flags;
    dev_t dev;
    inode_t *inode;
    fd_t *fd; /* FD of the file on which FOP is
                 being carried upon */
    struct iatt iatt;
    char *str[2];
    loc_t loc[2]; /* Holds the location details for
                     the file */
    struct gf_flock flock;
    struct iovec *vector;
    struct iobref *buffers;
    gf_seek_what_t seek;
    ec_fragment_range_t frag_range; /* This will hold the range of stripes
                                        affected by the fop. */
    char *errstr;                   /*String of fop name, path and gfid
                                     to be used in gf_msg. */
};

struct _ec_cbk_data {
    struct list_head list;        /* item in the sorted list of groups */
    struct list_head answer_list; /* item in the list of answers */
    ec_fop_data_t *fop;
    ec_cbk_data_t *next; /* next answer in the same group */
    uint32_t idx;
    int32_t op_ret;
    int32_t op_errno;
    int32_t count;
    uintptr_t mask;

    dict_t *xdata;
    dict_t *dict;
    int32_t int32;
    uintptr_t uintptr[3];
    uint64_t size;
    uint64_t version[2];
    inode_t *inode;
    fd_t *fd;
    struct statvfs statvfs;
    struct iatt iatt[5];
    struct gf_flock flock;
    struct iovec *vector;
    struct iobref *buffers;
    char *str;
    gf_dirent_t entries;
    off_t offset;
    gf_seek_what_t what;
};

enum _ec_gf_opcode {
    EC_GF_OP_LOAD,
    EC_GF_OP_STORE,
    EC_GF_OP_COPY,
    EC_GF_OP_XOR2,
    EC_GF_OP_XOR3,
    EC_GF_OP_XORM,
    EC_GF_OP_END
};

struct _ec_gf_op {
    ec_gf_opcode_t op;
    uint32_t arg1;
    uint32_t arg2;
    uint32_t arg3;
};

struct _ec_gf_mul {
    uint32_t regs;
    uint32_t map[EC_GF_MAX_REGS];
    ec_gf_op_t *ops;
};

struct _ec_gf {
    uint32_t bits;
    uint32_t size;
    uint32_t mod;
    uint32_t min_ops;
    uint32_t max_ops;
    uint32_t avg_ops;
    uint32_t *log;
    uint32_t *pow;
    ec_gf_mul_t **table;
};

struct _ec_code_gen {
    char *name;
    char **flags;
    uint32_t width;

    void (*prolog)(ec_code_builder_t *builder);
    void (*epilog)(ec_code_builder_t *builder);
    void (*load)(ec_code_builder_t *builder, uint32_t reg, uint32_t offset,
                 uint32_t bit);
    void (*store)(ec_code_builder_t *builder, uint32_t reg, uint32_t bit);
    void (*copy)(ec_code_builder_t *builder, uint32_t dst, uint32_t src);
    void (*xor2)(ec_code_builder_t *builder, uint32_t dst, uint32_t src);
    void (*xor3)(ec_code_builder_t *builder, uint32_t dst, uint32_t src1,
                 uint32_t src2);
    void (*xorm)(ec_code_builder_t *builder, uint32_t dst, uint32_t offset,
                 uint32_t bit);
};

struct _ec_code {
    gf_lock_t lock;
    struct list_head spaces;
    ec_gf_t *gf;
    ec_code_gen_t *gen;
};

struct _ec_code_arg {
    uint32_t value;
};

struct _ec_code_op {
    ec_gf_opcode_t op;
    ec_code_arg_t arg1;
    ec_code_arg_t arg2;
    ec_code_arg_t arg3;
};

struct _ec_code_builder {
    ec_code_t *code;
    uint64_t address;
    uint8_t *data;
    uint32_t size;
    int32_t error;
    uint32_t regs;
    uint32_t bits;
    uint32_t width;
    uint32_t count;
    uint32_t base;
    uint32_t map[EC_GF_MAX_REGS];
    gf_boolean_t linear;
    uint64_t loop;
    ec_code_op_t ops[0];
};

struct _ec_code_chunk {
    struct list_head list;
    size_t size;
    ec_code_space_t *space;
};

struct _ec_code_space {
    struct list_head list;
    struct list_head chunks;
    ec_code_t *code;
    void *exec;
    size_t size;
};

union _ec_code_func {
    ec_code_func_linear_t linear;
    ec_code_func_interleaved_t interleaved;
};

struct _ec_matrix_row {
    ec_code_func_t func;
    uint32_t *values;
};

struct _ec_matrix {
    struct list_head lru;
    uint32_t refs;
    uint32_t columns;
    uint32_t rows;
    uintptr_t mask;
    ec_code_t *code;
    uint32_t *values;
    ec_matrix_row_t row_data[0];
};

struct _ec_matrix_list {
    struct list_head lru;
    gf_lock_t lock;
    uint32_t columns;
    uint32_t rows;
    uint32_t max;
    uint32_t count;
    uint32_t stripe;
    struct mem_pool *pool;
    ec_gf_t *gf;
    ec_code_t *code;
    ec_matrix_t *encode;
    ec_matrix_t **objects;
};

struct _ec_heal {
    gf_lock_t lock;
    xlator_t *xl;
    ec_fop_data_t *fop;
    syncbarrier_t barrier;
    loc_t loc;
    ia_type_t ia_type;
    fd_t *fd;
    gf_boolean_t done;
    int32_t error;
    uintptr_t good;
    uintptr_t bad;
    uintptr_t open;
    uint64_t offset;
    uint64_t size;
    uint64_t total_size;
};

struct subvol_healer {
    xlator_t *this;
    int subvol;
    gf_boolean_t running;
    gf_boolean_t rerun;
    pthread_mutex_t mutex;
    pthread_cond_t cond;
    pthread_t thread;
};

struct _ec_self_heald {
    gf_boolean_t iamshd;
    gf_boolean_t enabled;
    time_t timeout;
    uint32_t max_threads;
    uint32_t wait_qlength;
    struct subvol_healer *index_healers;
    struct subvol_healer *full_healers;
};

struct _ec_statistics {
    struct {
        gf_atomic_t hits;    /* Cache hits. */
        gf_atomic_t misses;  /* Cache misses. */
        gf_atomic_t updates; /* Number of times an existing stripe has
                                been updated with new content. */
        gf_atomic_t invals;  /* Number of times an existing stripe has
                                been invalidated because of truncates
                                or discards. */
        gf_atomic_t evicts;  /* Number of times that an existing entry
                                has been evicted to make room for newer
                                entries. */
        gf_atomic_t allocs;  /* Number of memory allocations made to
                                store stripes. */
        gf_atomic_t errors;  /* Number of errors that have caused extra
                                requests. (Basically memory allocation
                                errors). */
    } stripe_cache;
    struct {
        gf_atomic_t attempted; /*Number of heals attempted on
                                files/directories*/
        gf_atomic_t completed; /*Number of heals complted on files/directories*/
    } shd;
};

struct _ec {
    xlator_t *xl;
    int32_t healers;
    int32_t heal_waiters;
    int32_t nodes; /* Total number of bricks(n) */
    int32_t bits_for_nodes;
    int32_t fragments;      /* Data bricks(k) */
    int32_t redundancy;     /* Redundant bricks(m) */
    uint32_t fragment_size; /* Size of fragment/chunk on a
                               brick. */
    uint32_t stripe_size;   /* (fragment_size * fragments)
                               maximum size of user data
                               stored in one stripe. */
    int32_t up;             /* Represents whether EC volume is
                               up or not. */
    uint32_t idx;
    uint32_t xl_up_count;     /* Number of UP bricks. */
    uintptr_t xl_up;          /* Bit flag representing UP
                                 bricks */
    uint32_t xl_notify_count; /* Number of notifications. */
    uintptr_t xl_notify;      /* Bit flag representing
                                 notification for bricks. */
    uintptr_t node_mask;
    uintptr_t read_mask;         /*Stores user defined read-mask*/
    gf_atomic_t async_fop_count; /* Number of on going asynchronous fops. */
    xlator_t **xl_list;
    gf_lock_t lock;
    gf_timer_t *timer;
    gf_boolean_t shutdown;
    gf_boolean_t eager_lock;
    gf_boolean_t other_eager_lock;
    gf_boolean_t optimistic_changelog;
    gf_boolean_t parallel_writes;
    uint32_t stripe_cache;
    uint32_t quorum_count;
    uint32_t background_heals;
    uint32_t heal_wait_qlen;
    uint32_t self_heal_window_size; /* max size of read/writes */
    time_t eager_lock_timeout;
    time_t other_eager_lock_timeout;
    struct list_head pending_fops;
    struct list_head heal_waiting;
    struct list_head healing;
    struct mem_pool *fop_pool;
    struct mem_pool *cbk_pool;
    struct mem_pool *lock_pool;
    ec_self_heald_t shd;
    char vol_uuid[GF_UUID_BUF_SIZE];
    dict_t *leaf_to_subvolid;
    ec_read_policy_t read_policy;
    ec_matrix_list_t matrix;
    ec_statistics_t stats;
};

#endif /* __EC_TYPES_H__ */
