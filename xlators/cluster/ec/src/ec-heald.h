/*
  Copyright (c) 2015 Red Hat, Inc. <http://www.redhat.com>
  This file is part of GlusterFS.

  This file is licensed to you under your choice of the GNU Lesser
  General Public License, version 3 or any later version (LGPLv3 or
  later), or the GNU General Public License, version 2 (GPLv2), in all
  cases as published by the Free Software Foundation.
*/

#ifndef __EC_HEALD_H__
#define __EC_HEALD_H__

#include "xlator.h"

struct _ec;
typedef struct _ec ec_t;

struct subvol_healer {
        xlator_t        *this;
        int              subvol;
        gf_boolean_t     local;
        gf_boolean_t     running;
        gf_boolean_t     rerun;
        pthread_mutex_t  mutex;
        pthread_cond_t   cond;
        pthread_t        thread;
};

struct _ec_self_heald;
typedef struct _ec_self_heald ec_self_heald_t;

struct _ec_self_heald {
        gf_boolean_t            iamshd;
        gf_boolean_t            enabled;
        int                     timeout;
        struct subvol_healer   *index_healers;
        struct subvol_healer   *full_healers;
};

int
ec_xl_op (xlator_t *this, dict_t *input, dict_t *output);

int
ec_selfheal_daemon_init (xlator_t *this);
void ec_selfheal_childup (ec_t *ec, int child);
#endif /* __EC_HEALD_H__ */
