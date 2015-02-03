#ifndef __TIMER_WHEEL_H
#define __TIMER_WHEEL_H

#include <pthread.h>

#include "list.h"

#define TVR_BITS  8
#define TVN_BITS  6
#define TVR_SIZE  (1 << TVR_BITS)
#define TVN_SIZE  (1 << TVN_BITS)
#define TVR_MASK  (TVR_SIZE - 1)
#define TVN_MASK  (TVN_SIZE - 1)

#define BITS_PER_LONG  64

struct tvec {
        struct list_head vec[TVN_SIZE];
};

struct tvec_root {
        struct list_head vec[TVR_SIZE];
};

struct tvec_base {
        pthread_spinlock_t lock;      /* base lock */

        pthread_t runner;             /* run_timer() */

        unsigned long timer_sec;      /* time counter */

        struct tvec_root tv1;
        struct tvec tv2;
        struct tvec tv3;
        struct tvec tv4;
        struct tvec tv5;
};

struct gf_tw_timer_list {
        void *data;
        unsigned long expires;

        /** callback routine */
        void (*function)(struct gf_tw_timer_list *, void *, unsigned long);

        struct list_head entry;
};

/** The API! */
struct tvec_base *gf_tw_init_timers ();
int gf_tw_cleanup_timers (struct tvec_base *);
void gf_tw_add_timer (struct tvec_base *, struct gf_tw_timer_list *);
void gf_tw_del_timer (struct gf_tw_timer_list *);

#endif
