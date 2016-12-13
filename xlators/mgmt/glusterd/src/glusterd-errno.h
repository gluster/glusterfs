/*
   Copyright (c) 2015 Red Hat, Inc. <http://www.redhat.com>
   This file is part of GlusterFS.

   This file is licensed to you under your choice of the GNU Lesser
   General Public License, version 3 or any later version (LGPLv3 or
   later), or the GNU General Public License, version 2 (GPLv2), in all
   cases as published by the Free Software Foundation.
*/
#ifndef _GLUSTERD_ERRNO_H
#define _GLUSTERD_ERRNO_H

enum glusterd_op_errno {
        EG_INTRNL      = 30800,          /* Internal Error                    */
        EG_OPNOTSUP    = 30801,          /* Gluster Op Not Supported          */
        EG_ANOTRANS    = 30802,          /* Another Transaction in Progress   */
        EG_BRCKDWN     = 30803,          /* One or more brick is down         */
        EG_NODEDWN     = 30804,          /* One or more node is down          */
        EG_HRDLMT      = 30805,          /* Hard Limit is reached             */
        EG_NOVOL       = 30806,          /* Volume does not exist             */
        EG_NOSNAP      = 30807,          /* Snap does not exist               */
        EG_RBALRUN     = 30808,          /* Rebalance is running              */
        EG_VOLRUN      = 30809,          /* Volume is running                 */
        EG_VOLSTP      = 30810,          /* Volume is not running             */
        EG_VOLEXST     = 30811,          /* Volume exists                     */
        EG_SNAPEXST    = 30812,          /* Snapshot exists                   */
        EG_ISSNAP      = 30813,          /* Volume is a snap volume           */
        EG_GEOREPRUN   = 30814,          /* Geo-Replication is running        */
        EG_NOTTHINP    = 30815,          /* Bricks are not thinly provisioned */
        EG_NOGANESHA   = 30816,          /* Global nfs-ganesha is not enabled */
};

#endif
