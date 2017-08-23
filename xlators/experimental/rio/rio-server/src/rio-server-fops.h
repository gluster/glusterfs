/*
  Copyright (c) 2017 Red Hat, Inc. <http://www.redhat.com>
  This file is part of GlusterFS.

  This file is licensed to you under your choice of the GNU Lesser
  General Public License, version 3 or any later version (LGPLv3 or
  later), or the GNU General Public License, version 2 (GPLv2), in all
  cases as published by the Free Software Foundation.
*/

/* File: rio-server-fops.h
 * This file contains entry points for all RIO server/MDS FOPs
 */

#ifndef _RIO_SERVER_FOPS_H
#define _RIO_SERVER_FOPS_H

int32_t rio_server_lookup (call_frame_t *, xlator_t *, loc_t *, dict_t *);

#endif /* _RIO_SERVER_FOPS_H */
