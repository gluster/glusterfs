/*
 *   Copyright (c) 2017 Red Hat, Inc. <http://www.redhat.com>
 *   This file is part of GlusterFS.
 *
 *   This file is licensed to you under your choice of the GNU Lesser
 *   General Public License, version 3 or any later version (LGPLv3 or
 *   later), or the GNU General Public License, version 2 (GPLv2), in all
 *   cases as published by the Free Software Foundation.
 */

#ifndef __DELAY_GEN_H__
#define __DELAY_GEN_H__

#include "delay-gen-mem-types.h"
#include "delay-gen-messages.h"

typedef struct {
    uint32_t delay_ppm;
    uint32_t delay_duration;
    bool enable[GF_FOP_MAXVALUE];
} dg_t;

#endif /* __DELAY_GEN_H__ */
