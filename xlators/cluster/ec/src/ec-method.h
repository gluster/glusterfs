/*
  Copyright (c) 2012-2015 DataLab, s.l. <http://www.datalab.es>
  This file is part of GlusterFS.

  This file is licensed to you under your choice of the GNU Lesser
  General Public License, version 3 or any later version (LGPLv3 or
  later), or the GNU General Public License, version 2 (GPLv2), in all
  cases as published by the Free Software Foundation.
*/

#ifndef __EC_METHOD_H__
#define __EC_METHOD_H__

#include "xlator.h"

#include "ec-types.h"
#include "ec-galois.h"

#define EC_GF_BITS 8
#define EC_GF_MOD 0x11D

#define EC_GF_SIZE (1 << EC_GF_BITS)

/* Determines the maximum size of the matrix used to encode/decode data */
#define EC_METHOD_MAX_FRAGMENTS 16
/* Determines the maximum number of usable elements in the Galois Field */
#define EC_METHOD_MAX_NODES     (EC_GF_SIZE - 1)

#define EC_METHOD_WORD_SIZE 64

#define EC_METHOD_CHUNK_SIZE (EC_METHOD_WORD_SIZE * EC_GF_BITS)

int32_t
ec_method_init(xlator_t *xl, ec_matrix_list_t *list, uint32_t columns,
               uint32_t rows, uint32_t max, const char *gen);

void ec_method_fini(ec_matrix_list_t *list);

int32_t
ec_method_update(xlator_t *xl, ec_matrix_list_t *list, const char *gen);

void
ec_method_encode(ec_matrix_list_t *list, size_t size, void *in, void **out);

int32_t
ec_method_decode(ec_matrix_list_t *list, size_t size, uintptr_t mask,
                 uint32_t *rows, void **in, void *out);

#endif /* __EC_METHOD_H__ */
