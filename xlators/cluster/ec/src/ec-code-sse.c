/*
  Copyright (c) 2015 DataLab, s.l. <http://www.datalab.es>
  This file is part of GlusterFS.

  This file is licensed to you under your choice of the GNU Lesser
  General Public License, version 3 or any later version (LGPLv3 or
  later), or the GNU General Public License, version 2 (GPLv2), in all
  cases as published by the Free Software Foundation.
*/

#include <errno.h>

#include "ec-code-intel.h"

static void
ec_code_sse_prolog(ec_code_builder_t *builder)
{
    builder->loop = builder->address;
}

static void
ec_code_sse_epilog(ec_code_builder_t *builder)
{
    ec_code_intel_op_add_i2r(builder, 16, REG_DX);
    ec_code_intel_op_add_i2r(builder, 16, REG_DI);
    ec_code_intel_op_test_i2r(builder, builder->width - 1, REG_DX);
    ec_code_intel_op_jne(builder, builder->loop);

    ec_code_intel_op_ret(builder, 0);
}

static void
ec_code_sse_load(ec_code_builder_t *builder, uint32_t dst, uint32_t idx,
                 uint32_t bit)
{
    if (builder->linear) {
        ec_code_intel_op_mov_m2sse(builder, REG_SI, REG_DX, 1,
                                   idx * builder->width * builder->bits +
                                   bit * builder->width,
                                   dst);
    } else {
        if (builder->base != idx) {
            ec_code_intel_op_mov_m2r(builder, REG_SI, REG_NULL, 0, idx * 8,
                                     REG_AX);
            builder->base = idx;
        }
        ec_code_intel_op_mov_m2sse(builder, REG_AX, REG_DX, 1,
                                   bit * builder->width, dst);
    }
}

static void
ec_code_sse_store(ec_code_builder_t *builder, uint32_t src, uint32_t bit)
{
    ec_code_intel_op_mov_sse2m(builder, src, REG_DI, REG_NULL, 0,
                               bit * builder->width);
}

static void
ec_code_sse_copy(ec_code_builder_t *builder, uint32_t dst, uint32_t src)
{
    ec_code_intel_op_mov_sse2sse(builder, src, dst);
}

static void
ec_code_sse_xor2(ec_code_builder_t *builder, uint32_t dst, uint32_t src)
{
    ec_code_intel_op_xor_sse2sse(builder, src, dst);
}

static void
ec_code_sse_xorm(ec_code_builder_t *builder, uint32_t dst, uint32_t idx,
                 uint32_t bit)
{
    if (builder->linear) {
       ec_code_intel_op_xor_m2sse(builder, REG_SI, REG_DX, 1,
                                  idx * builder->width * builder->bits +
                                  bit * builder->width,
                                  dst);
    } else {
        if (builder->base != idx) {
            ec_code_intel_op_mov_m2r(builder, REG_SI, REG_NULL, 0, idx * 8,
                                     REG_AX);
            builder->base = idx;
        }
        ec_code_intel_op_xor_m2sse(builder, REG_AX, REG_DX, 1,
                                   bit * builder->width, dst);
    }
}

static char *ec_code_sse_needed_flags[] = {
    "sse2",
    NULL
};

ec_code_gen_t ec_code_gen_sse = {
    .name   = "sse",
    .flags  = ec_code_sse_needed_flags,
    .width  = 16,
    .prolog = ec_code_sse_prolog,
    .epilog = ec_code_sse_epilog,
    .load   = ec_code_sse_load,
    .store  = ec_code_sse_store,
    .copy   = ec_code_sse_copy,
    .xor2   = ec_code_sse_xor2,
    .xor3   = NULL,
    .xorm   = ec_code_sse_xorm
};
