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
        EINTRNL   = 30800,               /* Internal Error */
        EANOTRANS = 30801,               /* Another Transaction in Progress */
};

#endif
