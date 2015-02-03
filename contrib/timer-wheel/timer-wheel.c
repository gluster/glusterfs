#include <stdio.h>
#include <stdlib.h>
#include <sys/time.h>

#include "timer-wheel.h"

static inline void
__gf_tw_add_timer (struct tvec_base *base, struct gf_tw_timer_list *timer)
{
        int i;
        unsigned long idx;
        unsigned long expires;
        struct list_head *vec;

        expires = timer->expires;

        idx = expires - base->timer_sec;

        if (idx < TVR_SIZE) {
                i = expires & TVR_MASK;
                vec = base->tv1.vec + i;
        } else if (idx < 1 << (TVR_BITS + TVN_BITS)) {
                i = (expires >> TVR_BITS) & TVN_MASK;
                vec = base->tv2.vec + i;
        } else if (idx < 1 << (TVR_BITS + 2*TVN_BITS)) {
                i = (expires >> (TVR_BITS + TVN_BITS)) & TVN_MASK;
                vec = base->tv3.vec + i;
        } else if (idx < 1 << (TVR_BITS + 3*TVN_BITS)) {
                i = (expires >> (TVR_BITS + 2*TVN_BITS)) & TVN_MASK;
                vec = base->tv4.vec + i;
        } else if (idx < 0) {
                vec = base->tv1.vec + (base->timer_sec & TVR_MASK);
        } else {
                i = (expires >> (TVR_BITS + 3*TVN_BITS)) & TVN_MASK;
                vec = base->tv5.vec + i;
        }

        list_add_tail (&timer->entry, vec);
}

/* optimized find_last_bit() */
unsigned long gf_tw_find_last_bit(const unsigned long *, unsigned long);

static inline unsigned long
apply_slack(struct tvec_base *base, struct gf_tw_timer_list *timer)
{
        long delta;
        unsigned long mask, expires, expires_limit;

        expires = timer->expires;

        delta = expires - base->timer_sec;
        if (delta < 256)
                return expires;

        expires_limit = expires + delta / 256;
        mask = expires ^ expires_limit;
        if (mask == 0)
                return expires;

        int bit = gf_tw_find_last_bit (&mask, BITS_PER_LONG);
        mask = (1UL << bit) - 1;

        expires_limit = expires_limit & ~(mask);
        return expires_limit;
}

static inline void
gf_tw_detach_timer (struct gf_tw_timer_list *timer)
{
        struct list_head *entry = &timer->entry;

        list_del (entry);
}

static inline int
cascade (struct tvec_base *base, struct tvec *tv, int index)
{
        struct gf_tw_timer_list *timer, *tmp;
        struct list_head tv_list;

        list_replace_init (tv->vec + index, &tv_list);

        list_for_each_entry_safe (timer, tmp, &tv_list, entry) {
                __gf_tw_add_timer (base, timer);
        }

        return index;
}

#define INDEX(N)  ((base->timer_sec >> (TVR_BITS + N * TVN_BITS)) & TVN_MASK)

/**
 * run expired timers
 */
static inline void
run_timers (struct tvec_base *base)
{
        unsigned long index, call_time;
        struct gf_tw_timer_list *timer;

        struct list_head work_list;
        struct list_head *head = &work_list;

        pthread_spin_lock (&base->lock);
        {
                index  = base->timer_sec & TVR_MASK;

                if (!index &&
                    (!cascade (base, &base->tv2, INDEX(0))) &&
                    (!cascade (base, &base->tv3, INDEX(1))) &&
                    (!cascade (base, &base->tv4, INDEX(2))))
                        cascade (base, &base->tv5, INDEX(3));

                call_time = base->timer_sec++;
                list_replace_init (base->tv1.vec + index, head);
                while (!list_empty(head)) {
                        void (*fn)(struct gf_tw_timer_list *, void *, unsigned long);
                        void *data;

                        timer = list_first_entry (head, struct gf_tw_timer_list, entry);
                        fn = timer->function;
                        data = timer->data;

                        gf_tw_detach_timer (timer);
                        fn (timer, data, call_time);
                }
        }
        pthread_spin_unlock (&base->lock);

}

void *runner (void *arg)
{
        struct timeval tv = {0,};
        struct tvec_base *base = arg;

        while (1) {
                run_timers (base);

                tv.tv_sec  = 1;
                tv.tv_usec = 0;
                (void) select (0, NULL, NULL, NULL, &tv);
        }

        return NULL;

}

/* interface */

/**
 * Add a timer in the timer wheel
 */
void gf_tw_add_timer (struct tvec_base *base, struct gf_tw_timer_list *timer)
{
        pthread_spin_lock (&base->lock);
        {
                timer->expires += base->timer_sec;
                timer->expires = apply_slack (base, timer);
                __gf_tw_add_timer (base, timer);
        }
        pthread_spin_unlock (&base->lock);
}

/**
 * Remove a timer from the timer wheel
 */
void gf_tw_del_timer (struct gf_tw_timer_list *timer)
{
        gf_tw_detach_timer (timer);
}

int gf_tw_cleanup_timers (struct tvec_base *base)
{
        int ret = 0;
        void *res = NULL;

        /* terminate runner */
        ret = pthread_cancel (base->runner);
        if (ret != 0)
                goto error_return;
        ret = pthread_join (base->runner, &res);
        if (ret != 0)
                goto error_return;
        if (res != PTHREAD_CANCELED)
                goto error_return;

        /* destroy lock */
        ret = pthread_spin_destroy (&base->lock);
        if (ret != 0)
                goto error_return;

        /* deallocated timer base */
        free (base);
        return 0;

 error_return:
        return -1;
}

/**
 * Initialize various timer wheel lists and spawn a thread that
 * invokes run_timers()
 */
struct tvec_base *gf_tw_init_timers ()
{
        int               j    = 0;
        int               ret  = 0;
        struct timeval    tv   = {0,};
        struct tvec_base *base = NULL;

        base = malloc (sizeof (*base));
        if (!base)
                goto error_return;

        ret = pthread_spin_init (&base->lock, 0);
        if (ret != 0)
                goto error_dealloc;

        for (j = 0; j < TVN_SIZE; j++) {
                INIT_LIST_HEAD (base->tv5.vec + j);
                INIT_LIST_HEAD (base->tv4.vec + j);
                INIT_LIST_HEAD (base->tv3.vec + j);
                INIT_LIST_HEAD (base->tv2.vec + j);
        }

        for (j = 0; j < TVR_SIZE; j++) {
                INIT_LIST_HEAD (base->tv1.vec + j);
        }

        ret = gettimeofday (&tv, 0);
        if (ret < 0)
                goto destroy_lock;
        base->timer_sec = tv.tv_sec;

        ret = pthread_create (&base->runner, NULL, runner, base);
        if (ret != 0)
                goto destroy_lock;
        return base;

 destroy_lock:
        (void) pthread_spin_destroy (&base->lock);
 error_dealloc:
        free (base);
 error_return:
        return NULL;
}


