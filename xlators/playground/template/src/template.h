/*
   Copyright (c) 2013-2018 Red Hat, Inc. <http://www.redhat.com>
   This file is part of GlusterFS.

   This file is licensed to you under your choice of the GNU Lesser
   General Public License, version 3 or any later version (LGPLv3 or
   later), or the GNU General Public License, version 2 (GPLv2), in all
   cases as published by the Free Software Foundation.
*/
#ifndef __TEMPLATE_H__
#define __TEMPLATE_H__

#include <glusterfs/glusterfs.h>
#include <glusterfs/logging.h>
#include <glusterfs/dict.h>
#include <glusterfs/xlator.h>
#include <glusterfs/defaults.h>

struct template_private {
    /* Add all the relevant fields you need here */
    int32_t dummy;
};

typedef struct template_private template_private_t;

/* Below section goes to template-mem-types.h */
#include <glusterfs/mem-types.h>

enum gf_template_mem_types_ {
    gf_template_mt_private_t = gf_common_mt_end + 1,
    gf_template_mt_end,
};

/* This normally goes to another file 'template-messages.h",
   required for 'gf_msg()'.
   NOTE: make sure you have added your component (in this case,
   TEMPLATE) in `libglusterfs/src/glfs-message-id.h`.
 */
#include <glusterfs/glfs-message-id.h>

GLFS_MSGID(TEMPLATE, TEMPLATE_MSG_NO_MEMORY, TEMPLATE_MSG_NO_GRAPH);

#endif /* __TEMPLATE_H__ */
