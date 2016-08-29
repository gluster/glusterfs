/*
  Copyright (c) 2016 Red Hat, Inc. <http://www.redhat.com>
  This file is part of GlusterFS.

  This file is licensed to you under your choice of the GNU Lesser
  General Public License, version 3 or any later version (LGPLv3 or
  later), or the GNU General Public License, version 2 (GPLv2), in all
  cases as published by the Free Software Foundation.
*/

#ifndef __COMPOUND_FOP_UTILS_H__
#define __COMPOUND_FOP_UTILS_H__

#include "defaults.h"
#include "default-args.h"
#include "mem-types.h"
#include "dict.h"

#define COMPOUND_PACK_ARGS(fop, fop_enum, args, counter, params ...) do {    \
        args->enum_list[counter] = fop_enum;                                 \
        args_##fop##_store (&args->req_list[counter], params);               \
} while (0)

compound_args_t*
compound_fop_alloc (int length, glusterfs_compound_fop_t fop, dict_t *xdata);

void
compound_args_cleanup (compound_args_t *args);

void
compound_args_cbk_cleanup (compound_args_cbk_t *args_cbk);

compound_args_cbk_t*
compound_args_cbk_alloc (int length, dict_t *xdata);
#endif /* __COMPOUND_FOP_UTILS_H__ */
