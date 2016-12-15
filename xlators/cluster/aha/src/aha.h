#ifndef _AHA_H
#define _AHA_H

#ifndef _CONFIG_H
#define _CONFIG_H
#include "config.h"
#endif

#include "xlator.h"
#include "statedump.h"
#include "call-stub.h"
#include "defaults.h"
#include "list.h"
#include "timer.h"

#include "aha-mem-types.h"

/* new() and destroy() functions for all structs can be found in
 * aha-helpers.c
 */
struct aha_conf {
        xlator_t *this;
        uint8_t child_up;
        gf_lock_t lock;
        struct list_head failed;
        gf_timer_t *timer;
        gf_boolean_t timer_expired;
        uint64_t server_wait_timeout;
};

struct aha_fop {
        call_stub_t *stub;      /* Only used to store function arguments */
        call_frame_t *frame;    /* Frame corresponding to this fop */
        uint64_t tries;
        struct list_head list;
};

enum {
        AHA_CHILD_STATUS_DOWN = 0,
        AHA_CHILD_STATUS_UP = 1,
        AHA_CHILD_STATUS_MAX
};

gf_boolean_t aha_is_timer_expired (struct aha_conf *conf);

#endif
