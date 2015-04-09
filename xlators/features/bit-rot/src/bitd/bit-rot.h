/*
   Copyright (c) 2015 Red Hat, Inc. <http://www.redhat.com>
   This file is part of GlusterFS.

   This file is licensed to you under your choice of the GNU Lesser
   General Public License, version 3 or any later version (LGPLv3 or
   later), or the GNU General Public License, version 2 (GPLv2), in all
   cases as published by the Free Software Foundation.
*/
#ifndef __BIT_ROT_H__
#define __BIT_ROT_H__

#ifndef _CONFIG_H
#define _CONFIG_H
#include "config.h"
#endif

#include "glusterfs.h"
#include "logging.h"
#include "dict.h"
#include "xlator.h"
#include "defaults.h"
#include "syncop.h"
#include "syncop-utils.h"
#include "changelog.h"
#include "timer-wheel.h"

#include "bit-rot-tbf.h"

#include "bit-rot-common.h"
#include "bit-rot-stub-mem-types.h"

#include <openssl/sha.h>

/**
 * TODO: make this configurable. As a best practice, set this to the
 * number of processor cores.
 */
#define BR_WORKERS 4

typedef enum scrub_throttle {
        BR_SCRUB_THROTTLE_VOID       = -1,
        BR_SCRUB_THROTTLE_LAZY       = 0,
        BR_SCRUB_THROTTLE_NORMAL     = 1,
        BR_SCRUB_THROTTLE_AGGRESSIVE = 2,
        BR_SCRUB_THROTTLE_STALLED    = 3,
} scrub_throttle_t;

#define signature_size(hl) (sizeof (br_isignature_t) + hl + 1)

struct br_scanfs {
        gf_lock_t entrylock;

        pthread_mutex_t waitlock;
        pthread_cond_t  waitcond;

        unsigned int     entries;
        struct list_head queued;
        struct list_head ready;
};

struct br_child {
        char child_up;                /* Indicates whether this child is
                                         up or not */
        xlator_t *xl;                 /* client xlator corresponding to
                                         this child */
        inode_table_t *table;         /* inode table for this child */
        char brick_path[PATH_MAX];    /* brick export directory of this
                                         child */
        struct list_head list;        /* hook to attach to the list of
                                         UP children */
        xlator_t *this;               /* Bit rot xlator */

        pthread_t thread;             /* initial crawler for unsigned
                                         object(s) or scrub crawler */
        int threadrunning;            /* active thread */

        struct mem_pool *timer_pool;  /* timer-wheel's timer mem-pool */

        struct timeval tv;

        struct br_scanfs fsscan;      /* per subvolume FS scanner */
};

typedef struct br_child br_child_t;

struct br_obj_n_workers {
        struct list_head objects;         /* queue of objects expired from the
                                             timer wheel and ready to be picked
                                             up for signing */
        pthread_t workers[BR_WORKERS];    /* Threads which pick up the objects
                                             from the above queue and start
                                             signing each object */
};

struct br_scrubber {
        xlator_t *this;

        scrub_throttle_t throttle;

        pthread_mutex_t mutex;
        pthread_cond_t  cond;

        unsigned int nr_scrubbers;
        struct list_head scrubbers;

        /*
         * list of "rotatable" subvolume(s) undergoing scrubbing
         */
        struct list_head scrublist;
};

typedef struct br_obj_n_workers br_obj_n_workers_t;

struct br_private {
        pthread_mutex_t lock;

        struct list_head bricks;          /* list of bricks from which CHILD_UP
                                             has been received */

        pthread_cond_t cond;              /* handling CHILD_UP notifications */
        pthread_cond_t object_cond;       /* handling signing of objects */
        int child_count;
        br_child_t *children;             /* list of subvolumes */
        int up_children;
        pthread_t thread;                 /* thread for connecting each UP
                                             child with changelog */
        struct tvec_base *timer_wheel;    /* timer wheel where the objects which
                                             changelog has sent sits and waits
                                             for expiry */
        br_obj_n_workers_t *obj_queue;    /* place holder for all the objects
                                             that are expired from timer wheel
                                             and ready to be picked up for
                                             signing and the workers which sign
                                             the objects */
        int32_t expiry_time;              /* objects "wait" time */

        br_tbf_t *tbf;                    /* token bucket filter */

        gf_boolean_t iamscrubber;         /* function as a fs scrubber */
        struct br_scrubber fsscrub;       /* scrubbers for this subvolume */
};

typedef struct br_private br_private_t;

struct br_object {
        xlator_t *this;

        uuid_t gfid;

        unsigned long signedversion;    /* version aginst which this object will
                                           be signed */
        br_child_t *child;              /* object's subvolume */

        int sign_info;

        struct list_head list;          /* hook to add to the queue once the
                                           object is expired from timer wheel */
        void *data;
};

typedef struct br_object br_object_t;

void
br_log_object (xlator_t *, char *, uuid_t, int32_t);

void
br_log_object_path (xlator_t *, char *, const char *, int32_t);

int32_t
br_calculate_obj_checksum (unsigned char *,
                           br_child_t *, fd_t *, struct iatt *);

int32_t
br_prepare_loc (xlator_t *, br_child_t *, loc_t *, gf_dirent_t *, loc_t *);

gf_boolean_t
bitd_is_bad_file (xlator_t *, br_child_t *, loc_t *, fd_t *);

void
br_trigger_sign (xlator_t *this, br_child_t *child, inode_t *linked_inode,
                 loc_t *loc, dict_t *xdata);

#endif /* __BIT_ROT_H__ */
