/*
   Copyright (c) 2015 Red Hat, Inc. <http://www.redhat.com>
   This file is part of GlusterFS.

   This file is licensed to you under your choice of the GNU Lesser
   General Public License, version 3 or any later version (LGPLv3 or
   later), or the GNU General Public License, version 2 (GPLv2), in all
   cases as published by the Free Software Foundation.
*/

#ifndef _GLUSTERD_RCU_H
#define _GLUSTERD_RCU_H

#include <urcu-bp.h>
#include <urcu/rculist.h>
#include <urcu/compiler.h>
#include <urcu/uatomic.h>
#include <urcu-call-rcu.h>

#ifdef URCU_OLD
#include "rculist-extra.h"
#endif

#include "xlator.h"

/* gd_rcu_head is a composite struct, composed of struct rcu_head and a this
 * pointer, which is used to pass the THIS pointer to call_rcu callbacks.
 *
 * Use this in place of struct rcu_head when embedding into another struct
 */
typedef struct glusterd_rcu_head_ {
        struct rcu_head head;
        xlator_t *this;
} gd_rcu_head;

#endif /* _GLUSTERD_RCU_H */
