/*
   Copyright (c) 2017 Red Hat, Inc. <http://www.redhat.com>
   This file is part of GlusterFS.

   This file is licensed to you under your choice of the GNU Lesser
   General Public License, version 3 or any later version (LGPLv3 or
   later), or the GNU General Public License, version 2 (GPLv2), in all
   cases as published by the Free Software Foundation.
*/

#include "xlator.h"
#include "defaults.h"

int32_t
init (xlator_t *this)
{
        return 0;
}

void
fini (xlator_t *this)
{
        return;
}

/*
 * notify - when parent sends PARENT_UP, send CHILD_UP event from here
 */
int32_t
notify (xlator_t *this, int32_t event, void *data, ...)
{
        switch (event) {
        case GF_EVENT_PARENT_UP:
                /* Tell the parent that this xlator is up */
                default_notify (this, GF_EVENT_CHILD_UP, data);
                break;
        case GF_EVENT_PARENT_DOWN:
                /* Tell the parent that this xlator is down */
                default_notify (this, GF_EVENT_CHILD_DOWN, data);
                break;
        default:
                break;
        }

        return 0;
}

/*
 * A lookup on "/" is done while mounting or glfs_init() is performed. This
 * needs to return a valid directory for the root of the mountpoint.
 *
 * In case this xlator is used for more advanced debugging, it will need to be
 * extended to support different LOOKUPs too.
 */
static int32_t
sink_lookup (call_frame_t *frame, xlator_t *this, loc_t *loc, dict_t *xdata)
{
        struct iatt stbuf = { 0, };
        struct iatt postparent = { 0, };

        /* the root of the volume always need to be a directory */
        stbuf.ia_type = IA_IFDIR;

        STACK_UNWIND_STRICT (lookup, frame, 0, 0, loc ? loc->inode : NULL,
                             &stbuf, xdata, &postparent);

        return 0;
}

struct xlator_fops fops = {
        .lookup = sink_lookup,
};

struct xlator_cbks cbks = {
};

struct volume_options options[] = {
        { .key  = {NULL} },
};
