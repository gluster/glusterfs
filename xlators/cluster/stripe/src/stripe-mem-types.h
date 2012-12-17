/*
  Copyright (c) 2008-2012 Red Hat, Inc. <http://www.redhat.com>
  This file is part of GlusterFS.

  This file is licensed to you under your choice of the GNU Lesser
  General Public License, version 3 or any later version (LGPLv3 or
  later), or the GNU General Public License, version 2 (GPLv2), in all
  cases as published by the Free Software Foundation.
*/


#ifndef __STRIPE_MEM_TYPES_H__
#define __STRIPE_MEM_TYPES_H__

#include "mem-types.h"

enum gf_stripe_mem_types_ {
        gf_stripe_mt_iovec = gf_common_mt_end + 1,
        gf_stripe_mt_stripe_replies,
        gf_stripe_mt_stripe_fd_ctx_t,
        gf_stripe_mt_char,
        gf_stripe_mt_int8_t,
        gf_stripe_mt_int32_t,
        gf_stripe_mt_xlator_t,
        gf_stripe_mt_stripe_private_t,
        gf_stripe_mt_stripe_options,
        gf_stripe_mt_xattr_sort_t,
        gf_stripe_mt_end
};
#endif

