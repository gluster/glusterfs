/*
  Copyright (c) 2017 Red Hat, Inc. <http://www.redhat.com>
  This file is part of GlusterFS.

  This file is licensed to you under your choice of the GNU Lesser
  General Public License, version 3 or any later version (LGPLv3 or
  later), or the GNU General Public License, version 2 (GPLv2), in all
  cases as published by the Free Software Foundation.
*/

/* File: rio-server-fops.c
 * This file contains the RIO MDC/server FOP entry and exit points.
 */

#include "xlator.h"

#include "rio-common.h"
#include "defaults.h"
#include "rio-server-fops.h"

/* LOOKUP Callback
If lookup returns an inode, the resolution is complete and we need the
checks as follows,
        - dirty inode handling
        - xaction handling
        - layout unlocking
If lookup returns EREMOTE, then only layout unlocking has to happen, as other
actions are done when we have the actual inode being processed.
*/
int32_t rio_server_lookup_cbk (call_frame_t *frame, void *cookie,
                               xlator_t *this, int32_t op_ret, int32_t op_errno,
                               inode_t *inode, struct iatt *buf, dict_t *xdata,
                               struct iatt *postparent)
{
        /* TODO: dirty inode handling
        If inode is dirty, then we need to fetch further stat from DS,
        or trust upcall/mdcache? */

        /* TODO: xaction handling
        If inode is part of an ongoing xaction, we need saved/stored
        iatt information that we will respond with
                - inode->xactioninprogress
                - inode->savedlinkcount */

        /* TODO: layout unlocks */

        /* NOTE: Yes, if we are just unwinding, use STACK_WIND_TAIL, but we
        are sure to expand this in the future to address the above TODOs hence
        using this WIND/UNWIND scheme. NOTE will be removed as function
        expands. */
        STACK_UNWIND_STRICT (lookup, frame, op_ret, op_errno, inode, buf,
                             xdata, postparent);
        return 0;
}

/* LOOKUP Call
Lookup on MDS is passed directly to POSIX, which will return all information
or EREMOTE if inode is not local. Client handles EREMOTE resolution with the
correct MDS further.

Generic structure:
        - lock the layout
        - check if layout version is in agreement between client and MDS
        - mark the layout as in use (to prevent changes to the layout when
        FOPs are being processed)
        - WIND to the next xlator
*/
int32_t rio_server_lookup (call_frame_t *frame, xlator_t *this, loc_t *loc,
                           dict_t *xdata)
{
        xlator_t *subvol;
        struct rio_conf *conf;

        VALIDATE_OR_GOTO (this, error);
        conf = this->private;
        VALIDATE_OR_GOTO (conf, error);

        /* TODO: layout checks and locks */

        subvol = conf->riocnf_server_local_xlator;

        STACK_WIND (frame, rio_server_lookup_cbk, subvol, subvol->fops->lookup,
                    loc, xdata);
        return 0;

error:
        STACK_UNWIND (lookup, frame, -1, errno, NULL, NULL, NULL, NULL);
        return 0;
}

