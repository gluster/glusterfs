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

/* LKOWNER to string functions */
static inline void
lkowner_unparse(gf_lkowner_t *lkowner, char *buf, int buf_len)
{
    int i = 0;
    int j = 0;
    uint8_t nibble;

    /* the common case for lkowner is 8 bytes long
       (a pointer) which will require 16 bytes (+ a NULL termination char)
       long buffer to convert into.
    */
    if (lkowner->len >= 8 && buf_len > 16) {
        for (i = 0; i < 8; i++) {
            nibble = (lkowner->data[i] & 0xF0) >> 4;
            if (nibble < 10)
                buf[j] = '0' + nibble;
            else
                buf[j] = 'a' + nibble - 10;
            j++;
            nibble = lkowner->data[i] & 0x0F;
            if (nibble < 10)
                buf[j] = '0' + nibble;
            else
                buf[j] = 'a' + nibble - 10;
            j++;
        }
    }
    while (i < lkowner->len) {
        if (i && !(i % 8)) {
            buf[j] = '-';
            j++;
        }
        sprintf(&buf[j], "%02hhx", lkowner->data[i]);
        j += 2;
        if (j == buf_len)
            break;
        i++;
    }

    if (j < buf_len)
        buf[j] = '\0';
}

static inline void
set_lk_owner_from_ptr(gf_lkowner_t *lkowner, void *data)
{
    int i = 0;
    int j = 0;

    lkowner->len = sizeof(unsigned long);
    for (i = 0, j = 0; i < sizeof(unsigned long); i++, j += 8) {
        lkowner->data[i] = (char)((((unsigned long)data) >> j) & 0xff);
    }
}

static inline void
set_lk_owner_from_uint64(gf_lkowner_t *lkowner, uint64_t data)
{
    int i = 0;
    int j = 0;

    lkowner->len = 8;
    for (i = 0, j = 0; i < 8; i++, j += 8) {
        lkowner->data[i] = (char)((data >> j) & 0xff);
    }
}

/* Return true if the locks have the same owner */
static inline int
is_same_lkowner(gf_lkowner_t *l1, gf_lkowner_t *l2)
{
    return ((l1->len == l2->len) && !memcmp(l1->data, l2->data, l1->len));
}

static inline int
is_lk_owner_null(gf_lkowner_t *lkowner)
{
    int i;

    if (lkowner == NULL || lkowner->len == 0)
        return 1;

    for (i = 0; i < lkowner->len; i++) {
        if (lkowner->data[i] != 0) {
            return 0;
        }
    }

    return 1;
}

static inline void
lk_owner_copy(gf_lkowner_t *dst, gf_lkowner_t *src)
{
    dst->len = src->len;
    if (src->len)
        memcpy(dst->data, src->data, src->len);
}
#endif /* _LK_OWNER_H */
