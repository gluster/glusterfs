/*
  Copyright (c) 2012-2014 DataLab, s.l. <http://www.datalab.es>
  This file is part of GlusterFS.

  This file is licensed to you under your choice of the GNU Lesser
  General Public License, version 3 or any later version (LGPLv3 or
  later), or the GNU General Public License, version 2 (GPLv2), in all
  cases as published by the Free Software Foundation.
*/

#ifndef __EC_METHOD_H__
#define __EC_METHOD_H__

#include "ec-gf.h"

/* Determines the maximum size of the matrix used to encode/decode data */
#define EC_METHOD_MAX_FRAGMENTS 16
/* Determines the maximum number of usable elements in the Galois Field */
#define EC_METHOD_MAX_NODES     (EC_GF_SIZE - 1)

#define EC_METHOD_WORD_SIZE 64

#define EC_METHOD_CHUNK_SIZE (EC_METHOD_WORD_SIZE * EC_GF_BITS)
#define EC_METHOD_WIDTH (EC_METHOD_WORD_SIZE / EC_GF_WORD_SIZE)

void ec_method_initialize(void);
size_t ec_method_encode(size_t size, uint32_t columns, uint32_t row,
                        uint8_t * in, uint8_t * out);
size_t ec_method_decode(size_t size, uint32_t columns, uint32_t * rows,
                        uint8_t ** in, uint8_t * out);

#endif /* __EC_METHOD_H__ */
