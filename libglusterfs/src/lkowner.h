/*
  Copyright (c) 2008-2012 Red Hat, Inc. <http://www.redhat.com>
  This file is part of GlusterFS.

  This file is licensed to you under your choice of the GNU Lesser
  General Public License, version 3 or any later version (LGPLv3 or
  later), or the GNU General Public License, version 2 (GPLv2), in all
  cases as published by the Free Software Foundation.
*/

#ifndef _LK_OWNER_H
#define _LK_OWNER_H

#ifndef _CONFIG_H
#define _CONFIG_H
#include "config.h"
#endif

#define GF_MAX_LOCK_OWNER_LEN 1024 /* 1kB as per NLM */

/* 16strings-16strings-... */
#define GF_LKOWNER_BUF_SIZE  ((GF_MAX_LOCK_OWNER_LEN * 2) +     \
                              (GF_MAX_LOCK_OWNER_LEN / 8))

typedef struct gf_lkowner_ {
        int  len;
        char data[GF_MAX_LOCK_OWNER_LEN];
} gf_lkowner_t;


/* LKOWNER to string functions */
static inline void
lkowner_unparse (gf_lkowner_t *lkowner, char *buf, int buf_len)
{
        int i = 0;
        int j = 0;

        for (i = 0; i < lkowner->len; i++) {
                if (i && !(i % 8)) {
                        buf[j] = '-';
                        j++;
                }
                sprintf (&buf[j], "%02hhx", lkowner->data[i]);
                j += 2;
                if (j == buf_len)
                        break;
        }
        if (j < buf_len)
                buf[j] = '\0';
}

static inline void
set_lk_owner_from_ptr (gf_lkowner_t *lkowner, void *data)
{
        int i = 0;
        int j = 0;

        lkowner->len = sizeof (unsigned long);
        for (i = 0, j = 0; i < lkowner->len; i++, j += 8) {
                lkowner->data[i] =  (char)((((unsigned long)data) >> j) & 0xff);
        }
}

static inline void
set_lk_owner_from_uint64 (gf_lkowner_t *lkowner, uint64_t data)
{
        int i = 0;
        int j = 0;

        lkowner->len = 8;
        for (i = 0, j = 0; i < lkowner->len; i++, j += 8) {
                lkowner->data[i] =  (char)((data >> j) & 0xff);
        }
}

/* Return true if the locks have the same owner */
static inline int
is_same_lkowner (gf_lkowner_t *l1, gf_lkowner_t *l2)
{
        return ((l1->len == l2->len) && !memcmp(l1->data, l2->data, l1->len));
}

#endif /* _LK_OWNER_H */
