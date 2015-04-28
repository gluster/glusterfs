/*
  Copyright (c) 2008-2015 Red Hat, Inc. <http://www.redhat.com>
  This file is part of GlusterFS.

  This file is licensed to you under your choice of the GNU Lesser
  General Public License, version 3 or any later version (LGPLv3 or
  later), or the GNU General Public License, version 2 (GPLv2), in all
  cases as published by the Free Software Foundation.
*/

#include "tw.h"
#include "timer-wheel.h"

int
glusterfs_global_timer_wheel_init (glusterfs_ctx_t *ctx)
{
        ctx->timer_wheel = gf_tw_init_timers();
        return ctx->timer_wheel ? 0 : -1;
}

struct tvec_base *
glusterfs_global_timer_wheel (xlator_t *this)
{
        return this->ctx->timer_wheel;
}
