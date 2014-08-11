/*
  Copyright (c) 2008-2014 Red Hat, Inc. <http://www.redhat.com>
  This file is part of GlusterFS.

  This file is licensed to you under your choice of the GNU Lesser
  General Public License, version 3 or any later version (LGPLv3 or
  later), or the GNU General Public License, version 2 (GPLv2), in all
  cases as published by the Free Software Foundation.
*/
#ifndef _DHT_HELPER_H
#define _DHT_HELPER_H

int
dht_lock_order_requests (dht_lock_t **lk_array, int count);

void
dht_blocking_inodelk_rec (call_frame_t *frame, int i);

#endif
