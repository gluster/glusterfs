/*
  Copyright (c) 2015 DataLab, s.l. <http://www.datalab.es>
  This file is part of GlusterFS.

  This file is licensed to you under your choice of the GNU Lesser
  General Public License, version 3 or any later version (LGPLv3 or
  later), or the GNU General Public License, version 2 (GPLv2), in all
  cases as published by the Free Software Foundation.
*/

#ifndef __EC_CODE_H__
#define __EC_CODE_H__

#include "xlator.h"
#include "list.h"

#include "ec-types.h"
#include "ec-galois.h"

ec_code_gen_t *
ec_code_detect(xlator_t *xl, const char *def);

ec_code_t *
ec_code_create(ec_gf_t *gf, ec_code_gen_t *gen);

void
ec_code_destroy(ec_code_t *code);

ec_code_func_linear_t
ec_code_build_linear(ec_code_t *code, uint32_t width, uint32_t *values,
                     uint32_t count);
ec_code_func_interleaved_t
ec_code_build_interleaved(ec_code_t *code, uint32_t width, uint32_t *values,
                          uint32_t count);
void
ec_code_release(ec_code_t *code, ec_code_func_t *func);

void
ec_code_error(ec_code_builder_t *builder, int32_t error);

void
ec_code_emit(ec_code_builder_t *builder, uint8_t *bytes, uint32_t count);

#endif /* __EC_CODE_H__ */
