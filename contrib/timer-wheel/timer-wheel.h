/*
  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation; either version 2 of the License, or
  (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License along
  with this program; if not, write to the Free Software Foundation, Inc.,
  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
*/

#ifndef __TIMER_WHEEL_H
#define __TIMER_WHEEL_H

#include "locking.h"

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
int gf_tw_del_timer (struct tvec_base *, struct gf_tw_timer_list *);

int gf_tw_mod_timer_pending (struct tvec_base *,
                             struct gf_tw_timer_list *, unsigned long);

int gf_tw_mod_timer (struct tvec_base *,
                     struct gf_tw_timer_list *, unsigned long);

#endif
