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
ec_code_avx_prolog(ec_code_builder_t *builder)
{
    builder->loop = builder->address;
}

static void
ec_code_avx_epilog(ec_code_builder_t *builder)
{
    ec_code_intel_op_add_i2r(builder, 32, REG_DX);
    ec_code_intel_op_add_i2r(builder, 32, REG_DI);
    ec_code_intel_op_test_i2r(builder, builder->width - 1, REG_DX);
    ec_code_intel_op_jne(builder, builder->loop);

    ec_code_intel_op_ret(builder, 0);
}

static void
ec_code_avx_load(ec_code_builder_t *builder, uint32_t dst, uint32_t idx,
                 uint32_t bit)
{
    if (builder->linear) {
        ec_code_intel_op_mov_m2avx(builder, REG_SI, REG_DX, 1,
                                   idx * builder->width * builder->bits +
                                   bit * builder->width,
                                   dst);
    } else {
        if (builder->base != idx) {
            ec_code_intel_op_mov_m2r(builder, REG_SI, REG_NULL, 0, idx * 8,
                                     REG_AX);
            builder->base = idx;
        }
        ec_code_intel_op_mov_m2avx(builder, REG_AX, REG_DX, 1,
                                   bit * builder->width, dst);
    }
}

static void
ec_code_avx_store(ec_code_builder_t *builder, uint32_t src, uint32_t bit)
{
    ec_code_intel_op_mov_avx2m(builder, src, REG_DI, REG_NULL, 0,
                               bit * builder->width);
}

static void
ec_code_avx_copy(ec_code_builder_t *builder, uint32_t dst, uint32_t src)
{
    ec_code_intel_op_mov_avx2avx(builder, src, dst);
}

static void
ec_code_avx_xor2(ec_code_builder_t *builder, uint32_t dst, uint32_t src)
{
    ec_code_intel_op_xor_avx2avx(builder, src, dst);
}

static void
ec_code_avx_xor3(ec_code_builder_t *builder, uint32_t dst, uint32_t src1,
                 uint32_t src2)
{
    ec_code_intel_op_mov_avx2avx(builder, src1, dst);
    ec_code_intel_op_xor_avx2avx(builder, src2, dst);
}

static void
ec_code_avx_xorm(ec_code_builder_t *builder, uint32_t dst, uint32_t idx,
                 uint32_t bit)
{
    if (builder->linear) {
        ec_code_intel_op_xor_m2avx(builder, REG_SI, REG_DX, 1,
                                   idx * builder->width * builder->bits +
                                   bit * builder->width,
                                   dst);
    } else {
        if (builder->base != idx) {
            ec_code_intel_op_mov_m2r(builder, REG_SI, REG_NULL, 0, idx * 8,
                                     REG_AX);
            builder->base = idx;
        }
        ec_code_intel_op_xor_m2avx(builder, REG_AX, REG_DX, 1,
                                   bit * builder->width, dst);
    }
}

static char *ec_code_avx_needed_flags[] = {
    "avx2",
    NULL
};

ec_code_gen_t ec_code_gen_avx = {
    .name   = "avx",
    .flags  = ec_code_avx_needed_flags,
    .width  = 32,
    .prolog = ec_code_avx_prolog,
    .epilog = ec_code_avx_epilog,
    .load   = ec_code_avx_load,
    .store  = ec_code_avx_store,
    .copy   = ec_code_avx_copy,
    .xor2   = ec_code_avx_xor2,
    .xor3   = ec_code_avx_xor3,
    .xorm   = ec_code_avx_xorm
};
