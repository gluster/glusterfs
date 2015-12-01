/*
  Copyright (c) 2015 DataLab, s.l. <http://www.datalab.es>
  This file is part of GlusterFS.

  This file is licensed to you under your choice of the GNU Lesser
  General Public License, version 3 or any later version (LGPLv3 or
  later), or the GNU General Public License, version 2 (GPLv2), in all
  cases as published by the Free Software Foundation.
*/

#ifndef __EC_GALOIS_H__
#define __EC_GALOIS_H__

#include <inttypes.h>

#include "ec-types.h"

ec_gf_t *ec_gf_prepare(uint32_t bits, uint32_t mod);
void ec_gf_destroy(ec_gf_t *gf);

uint32_t ec_gf_add(ec_gf_t *gf, uint32_t a, uint32_t b);
uint32_t ec_gf_mul(ec_gf_t *gf, uint32_t a, uint32_t b);
uint32_t ec_gf_div(ec_gf_t *gf, uint32_t a, uint32_t b);
uint32_t ec_gf_exp(ec_gf_t *gf, uint32_t a, uint32_t b);

#endif /* __EC_GALOIS_H__ */
