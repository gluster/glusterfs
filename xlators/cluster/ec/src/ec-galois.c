/*
  Copyright (c) 2015 DataLab, s.l. <http://www.datalab.es>
  This file is part of GlusterFS.

  This file is licensed to you under your choice of the GNU Lesser
  General Public License, version 3 or any later version (LGPLv3 or
  later), or the GNU General Public License, version 2 (GPLv2), in all
  cases as published by the Free Software Foundation.
*/

#include <string.h>

#include "mem-pool.h"
#include "list.h"

#include "ec-mem-types.h"
#include "ec-gf8.h"
#include "ec-helpers.h"

static ec_gf_t *
ec_gf_alloc(uint32_t bits, uint32_t mod)
{
    ec_gf_t *gf;

    gf = GF_MALLOC(sizeof(ec_gf_t), ec_mt_ec_gf_t);
    if (gf == NULL) {
        goto failed;
    }

    gf->bits = bits;
    gf->size = 1 << bits;
    gf->mod = mod;

    gf->log = GF_MALLOC(sizeof(uint32_t) * (gf->size * 2 - 1),
                        gf_common_mt_int);
    if (gf->log == NULL) {
        goto failed_gf;
    }
    gf->pow = GF_MALLOC(sizeof(uint32_t) * (gf->size * 2 - 1),
                        gf_common_mt_int);
    if (gf->pow == NULL) {
        goto failed_log;
    }

    return gf;

failed_log:
    GF_FREE(gf->log);
failed_gf:
    GF_FREE(gf);
failed:
    return EC_ERR(ENOMEM);
}

static void
ec_gf_init_tables(ec_gf_t *gf)
{
    uint32_t i, tmp;

    memset(gf->log, -1, sizeof(uint32_t) * gf->size);

    gf->pow[0] = 1;
    gf->log[0] = gf->size;
    gf->log[1] = 0;
    for (i = 1; i < gf->size; i++) {
        tmp = gf->pow[i - 1] << 1;
        if (tmp >= gf->size) {
            tmp ^= gf->mod;
        }
        gf->pow[i + gf->size - 1] = gf->pow[i] = tmp;
        gf->log[tmp + gf->size - 1] = gf->log[tmp] = i;
    }
}

ec_gf_t *
ec_gf_prepare(uint32_t bits, uint32_t mod)
{
    ec_gf_mul_t **tbl;
    ec_gf_t *gf;
    uint32_t i, j;

    if (bits != 8) {
        return EC_ERR(EINVAL);
    }

    tbl = ec_gf8_mul;
    if (mod == 0) {
        mod = 0x11d;
    }

    gf = ec_gf_alloc(bits, mod);
    if (EC_IS_ERR(gf)) {
        return gf;
    }
    ec_gf_init_tables(gf);

    gf->table = tbl;
    gf->min_ops = bits * bits;
    gf->max_ops = 0;
    gf->avg_ops = 0;
    for (i = 1; i < gf->size; i++) {
        for (j = 0; tbl[i]->ops[j].op != EC_GF_OP_END; j++) {
        }
        if (gf->max_ops < j) {
            gf->max_ops = j;
        }
        if (gf->min_ops > j) {
            gf->min_ops = j;
        }
        gf->avg_ops += j;
    }
    gf->avg_ops /= gf->size;

    return gf;
}

void
ec_gf_destroy(ec_gf_t *gf)
{
    GF_FREE(gf->pow);
    GF_FREE(gf->log);
    GF_FREE(gf);
}

uint32_t
ec_gf_add(ec_gf_t *gf, uint32_t a, uint32_t b)
{
    if ((a >= gf->size) || (b >= gf->size)) {
        return gf->size;
    }

    return a ^ b;
}

uint32_t
ec_gf_mul(ec_gf_t *gf, uint32_t a, uint32_t b)
{
    if ((a >= gf->size) || (b >= gf->size)) {
        return gf->size;
    }

    if ((a != 0) && (b != 0)) {
        return gf->pow[gf->log[a] + gf->log[b]];
    }

    return 0;
}

uint32_t
ec_gf_div(ec_gf_t *gf, uint32_t a, uint32_t b)
{
    if ((a >= gf->size) || (b >= gf->size)) {
        return gf->size;
    }

    if (b != 0) {
        if (a != 0) {
            return gf->pow[gf->size - 1 + gf->log[a] - gf->log[b]];
        }

        return 0;
    }

    return gf->size;
}

uint32_t
ec_gf_exp(ec_gf_t *gf, uint32_t a, uint32_t b)
{
    uint32_t r;

    if ((a >= gf->size) || ((a == 0) && (b == 0))) {
        return gf->size;
    }

    r = 1;
    while (b != 0) {
        if ((b & 1) != 0) {
            r = ec_gf_mul(gf, r, a);
        }
        a = ec_gf_mul(gf, a, a);
        b >>= 1;
    }

    return r;
}
