/*
  Copyright (c) 2015 DataLab, s.l. <http://www.datalab.es>
  This file is part of GlusterFS.

  This file is licensed to you under your choice of the GNU Lesser
  General Public License, version 3 or any later version (LGPLv3 or
  later), or the GNU General Public License, version 2 (GPLv2), in all
  cases as published by the Free Software Foundation.
*/

#ifndef __EC_CODE_C_H__
#define __EC_CODE_C_H__

#include "ec-types.h"

void ec_code_c_prepare(ec_gf_t *gf, uint32_t *values, uint32_t count);

void ec_code_c_linear(void *dst, void *src, uint64_t offset, uint32_t *values,
                      uint32_t count);

void ec_code_c_interleaved(void *dst, void **src, uint64_t offset,
                           uint32_t *values, uint32_t count);

#endif /* __EC_CODE_C_H__ */
