/*
  Copyright (c) 2016 Red Hat, Inc. <http://www.redhat.com>
  This file is part of GlusterFS.

  This file is licensed to you under your choice of the GNU Lesser
  General Public License, version 3 or any later version (LGPLv3 or
  later), or the GNU General Public License, version 2 (GPLv2), in all
  cases as published by the Free Software Foundation.
*/

/* File: layout.h
 * Provides the layout object abstraction base.
 * Used to extend with different layout types that can govern distribution.
 */

#ifndef _LAYOUT_H_
#define _LAYOUT_H_

#include "dict.h"
#include "xlator.h"
#include "rio-common.h"

/* Needed interface by consumers */
/* TODO: So ideally this section can be a separate header */
#define LAYOUT_INODEHASH_BUCKET "inodehash-bucket"
#define LAYOUT_STATIC_BUCKET "static-bucket"

struct layout;
typedef struct layout layout_t;

struct layout *layout_init (char *, int, struct rio_subvol *, dict_t *);
void layout_destroy (struct layout *);

/* END: needed interface by consumers */

struct layout_ops;

struct layout {
        struct layout_ops *layou_ops; /* operations */
        xlator_t **layou_buckets; /* private for bucket based layouts */
};

struct layout_ops {
        struct layout *(*laops_init)(char *, int, struct rio_subvol *,
                                     dict_t *);
        void (*laops_destroy)(struct layout *layout);
};

#endif /* _EVENT_H_ */
