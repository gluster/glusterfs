/*
  Copyright (c) 2016 Red Hat, Inc. <http://www.redhat.com>
  This file is part of GlusterFS.

  This file is licensed to you under your choice of the GNU Lesser
  General Public License, version 3 or any later version (LGPLv3 or
  later), or the GNU General Public License, version 2 (GPLv2), in all
  cases as published by the Free Software Foundation.
*/

/* File: dht2-common-map.c
 * This file contains helper routines to store, consult, the volume map
 * for subvolume to GFID relations.
 * The entire functionality including comments is TODO.
 */

#include "glusterfs.h"
#include "logging.h"
#include "statedump.h"
