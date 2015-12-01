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

#include "ec-types.h"

int
ec_xl_op (xlator_t *this, dict_t *input, dict_t *output);

int
ec_selfheal_daemon_init (xlator_t *this);
void ec_selfheal_childup (ec_t *ec, int child);

#endif /* __EC_HEALD_H__ */
