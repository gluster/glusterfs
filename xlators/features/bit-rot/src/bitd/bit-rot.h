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

#include "glusterfs.h"
#include "logging.h"
#include "dict.h"
#include "xlator.h"
#include "defaults.h"
#include "syncop.h"
#include "syncop-utils.h"
#include "changelog.h"
#include "timer-wheel.h"

#include "throttle-tbf.h"
#include "bit-rot-ssm.h"

#include "bit-rot-common.h"
#include "bit-rot-stub-mem-types.h"
#include "bit-rot-scrub-status.h"

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

typedef enum scrub_freq {
        BR_FSSCRUB_FREQ_HOURLY = 1,
        BR_FSSCRUB_FREQ_DAILY,
        BR_FSSCRUB_FREQ_WEEKLY,
        BR_FSSCRUB_FREQ_BIWEEKLY,
        BR_FSSCRUB_FREQ_MONTHLY,
        BR_FSSCRUB_FREQ_MINUTE,
        BR_FSSCRUB_FREQ_STALLED,
} scrub_freq_t;

#define signature_size(hl) (sizeof (br_isignature_t) + hl + 1)

struct br_scanfs {
        gf_lock_t entrylock;

        pthread_mutex_t waitlock;
        pthread_cond_t  waitcond;

        unsigned int     entries;
        struct list_head queued;
        struct list_head ready;
};

/* just need three states to track child status */
typedef enum br_child_state {
        BR_CHILD_STATE_CONNECTED = 1,
        BR_CHILD_STATE_INITIALIZING,
        BR_CHILD_STATE_CONNFAILED,
        BR_CHILD_STATE_DISCONNECTED,
} br_child_state_t;

struct br_child {
        pthread_mutex_t lock;         /* protects child state */
        char witnessed;               /* witnessed at least one succesfull
                                         connection */
        br_child_state_t c_state;     /* current state of this child */

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

        gf_boolean_t active_scrubbing; /* Actively scrubbing or not */
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

        /**
         * frequency of scanning for this subvolume. this should
         * normally be per-child, but since all childs follow the
         * same frequency for a volume, this option ends up here
         * instead of br_child_t.
         */
        scrub_freq_t frequency;

        gf_boolean_t frequency_reconf;
        gf_boolean_t throttle_reconf;

        pthread_mutex_t mutex;
        pthread_cond_t  cond;

        unsigned int nr_scrubbers;
        struct list_head scrubbers;

        /**
         * list of "rotatable" subvolume(s) undergoing scrubbing
         */
        struct list_head scrublist;
};

struct br_monitor {
        gf_lock_t lock;
        pthread_t thread;         /* Monitor thread */

        gf_boolean_t  inited;
        pthread_mutex_t mutex;
        pthread_cond_t cond;      /* Thread starts and will be waiting on cond.
                                     First child which is up wakes this up */

        xlator_t *this;
        /* scheduler */
        uint32_t boot;

        int32_t active_child_count; /* Number of children currently scrubbing */
        gf_boolean_t kick;          /* This variable tracks the scrubber is
                                     * kicked or not. Both 'kick' and
                                     * 'active_child_count' uses the same pair
                                     * of mutex-cond variable, i.e, wakelock and
                                     * wakecond. */

        pthread_mutex_t wakelock;
        pthread_cond_t  wakecond;

        gf_boolean_t done;
        pthread_mutex_t donelock;
        pthread_cond_t  donecond;

        struct gf_tw_timer_list *timer;
        br_scrub_state_t state;   /* current scrub state */
};

typedef struct br_obj_n_workers br_obj_n_workers_t;

typedef struct br_private br_private_t;

typedef void (*br_scrubbed_file_update) (br_private_t *priv);

struct br_private {
        pthread_mutex_t lock;

        struct list_head bricks;          /* list of bricks from which enents
                                             have been received */

        struct list_head signing;

        pthread_cond_t object_cond;       /* handling signing of objects */
        int child_count;
        br_child_t *children;             /* list of subvolumes */
        int up_children;

        pthread_cond_t cond;              /* handling CHILD_UP notifications */
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

        uint32_t expiry_time;              /* objects "wait" time */

        tbf_t *tbf;                    /* token bucket filter */

        gf_boolean_t iamscrubber;         /* function as a fs scrubber */

        struct br_scrub_stats scrub_stat; /* statistics of scrub*/

        struct br_scrubber fsscrub;       /* scrubbers for this subvolume */

        struct br_monitor scrub_monitor;  /* scrubber monitor */
};

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
typedef int32_t (br_scrub_ssm_call) (xlator_t *);

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

static inline void
_br_set_child_state (br_child_t *child, br_child_state_t state)
{
        child->c_state = state;
}

static inline int
_br_is_child_connected (br_child_t *child)
{
        return (child->c_state == BR_CHILD_STATE_CONNECTED);
}

static inline int
_br_is_child_scrub_active (br_child_t *child)
{
        return child->active_scrubbing;
}

static inline int
_br_child_failed_conn (br_child_t *child)
{
        return (child->c_state == BR_CHILD_STATE_CONNFAILED);
}

static inline int
_br_child_witnessed_connection (br_child_t *child)
{
        return (child->witnessed == 1);
}

/* scrub state */
static inline void
_br_monitor_set_scrub_state (struct br_monitor *scrub_monitor,
                           br_scrub_state_t state)
{
        scrub_monitor->state = state;
}

static inline br_scrub_event_t
_br_child_get_scrub_event (struct br_scrubber *fsscrub)
{
        return (fsscrub->frequency == BR_FSSCRUB_FREQ_STALLED)
             ? BR_SCRUB_EVENT_PAUSE : BR_SCRUB_EVENT_SCHEDULE;
}

int32_t
br_get_bad_objects_list (xlator_t *this, dict_t **dict);


#endif /* __BIT_ROT_H__ */
