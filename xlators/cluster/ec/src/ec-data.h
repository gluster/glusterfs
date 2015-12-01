/*
  Copyright (c) 2012-2014 DataLab, s.l. <http://www.datalab.es>
  This file is part of GlusterFS.

  This file is licensed to you under your choice of the GNU Lesser
  General Public License, version 3 or any later version (LGPLv3 or
  later), or the GNU General Public License, version 2 (GPLv2), in all
  cases as published by the Free Software Foundation.
*/

#ifndef __EC_DATA_H__
#define __EC_DATA_H__

#include "ec-types.h"

ec_cbk_data_t * ec_cbk_data_allocate(call_frame_t * frame, xlator_t * this,
                                     ec_fop_data_t * fop, int32_t id,
                                     int32_t idx, int32_t op_ret,
                                     int32_t op_errno);
ec_fop_data_t * ec_fop_data_allocate(call_frame_t * frame, xlator_t * this,
                                     int32_t id, uint32_t flags,
                                     uintptr_t target, int32_t minimum,
                                     ec_wind_f wind, ec_handler_f handler,
                                     ec_cbk_t cbks, void * data);
void ec_fop_data_acquire(ec_fop_data_t * fop);
void ec_fop_data_release(ec_fop_data_t * fop);

void ec_fop_cleanup(ec_fop_data_t *fop);

void ec_pending_fops_completed(ec_t *ec);

#endif /* __EC_DATA_H__ */
