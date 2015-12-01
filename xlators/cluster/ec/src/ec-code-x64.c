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

static ec_code_intel_reg_t ec_code_x64_regmap[] = {
    REG_AX, REG_CX, REG_BP, REG_8,  REG_9, REG_10,
    REG_11, REG_12, REG_13, REG_14, REG_15
};

static void
ec_code_x64_prolog(ec_code_builder_t *builder)
{
    uint32_t i;

    ec_code_intel_op_push_r(builder, REG_BP);
    if (!builder->linear) {
        ec_code_intel_op_push_r(builder, REG_BX);
    }
    if (builder->regs > 11) {
        ec_code_error(builder, EINVAL);
        return;
    }
    for (i = 7; i < builder->regs; i++) {
        ec_code_intel_op_push_r(builder, ec_code_x64_regmap[i]);
    }

    builder->loop = builder->address;
}

static void
ec_code_x64_epilog(ec_code_builder_t *builder)
{
    uint32_t i;

    ec_code_intel_op_add_i2r(builder, 8, REG_DX);
    ec_code_intel_op_add_i2r(builder, 8, REG_DI);
    ec_code_intel_op_test_i2r(builder, builder->width - 1, REG_DX);
    ec_code_intel_op_jne(builder, builder->loop);

    if (builder->regs > 11) {
        ec_code_error(builder, EINVAL);
    }
    for (i = builder->regs; i > 7; i--) {
        ec_code_intel_op_pop_r(builder, ec_code_x64_regmap[i - 1]);
    }
    if (!builder->linear) {
        ec_code_intel_op_pop_r(builder, REG_BX);
    }
    ec_code_intel_op_pop_r(builder, REG_BP);
    ec_code_intel_op_ret(builder, 0);
}

static void
ec_code_x64_load(ec_code_builder_t *builder, uint32_t dst, uint32_t idx,
                 uint32_t bit)
{
    dst = ec_code_x64_regmap[dst];

    if (builder->linear) {
        ec_code_intel_op_mov_m2r(builder, REG_SI, REG_DX, 1,
                                 idx * builder->width * builder->bits +
                                 bit * builder->width,
                                 dst);
    } else {
        if (builder->base != idx) {
            ec_code_intel_op_mov_m2r(builder, REG_SI, REG_NULL, 0, idx * 8,
                                     REG_BX);
            builder->base = idx;
        }
        ec_code_intel_op_mov_m2r(builder, REG_BX, REG_DX, 1,
                                 bit * builder->width, dst);
    }
}

static void
ec_code_x64_store(ec_code_builder_t *builder, uint32_t src, uint32_t bit)
{
    src = ec_code_x64_regmap[src];

    ec_code_intel_op_mov_r2m(builder, src, REG_DI, REG_NULL, 0,
                             bit * builder->width);
}

static void
ec_code_x64_copy(ec_code_builder_t *builder, uint32_t dst, uint32_t src)
{
    dst = ec_code_x64_regmap[dst];
    src = ec_code_x64_regmap[src];

    ec_code_intel_op_mov_r2r(builder, src, dst);
}

static void
ec_code_x64_xor2(ec_code_builder_t *builder, uint32_t dst, uint32_t src)
{
    dst = ec_code_x64_regmap[dst];
    src = ec_code_x64_regmap[src];

    ec_code_intel_op_xor_r2r(builder, src, dst);
}

static void
ec_code_x64_xorm(ec_code_builder_t *builder, uint32_t dst, uint32_t idx,
                 uint32_t bit)
{
    dst = ec_code_x64_regmap[dst];

    if (builder->linear) {
        ec_code_intel_op_xor_m2r(builder, REG_SI, REG_DX, 1,
                                 idx * builder->width * builder->bits +
                                 bit * builder->width,
                                 dst);
    } else {
        if (builder->base != idx) {
            ec_code_intel_op_mov_m2r(builder, REG_SI, REG_NULL, 0, idx * 8,
                                     REG_BX);
            builder->base = idx;
        }
        ec_code_intel_op_xor_m2r(builder, REG_BX, REG_DX, 1,
                                 bit * builder->width, dst);
    }
}

static char *ec_code_x64_needed_flags[] = {
    NULL
};

ec_code_gen_t ec_code_gen_x64 = {
    .name   = "x64",
    .flags  = ec_code_x64_needed_flags,
    .width  = sizeof(uint64_t),
    .prolog = ec_code_x64_prolog,
    .epilog = ec_code_x64_epilog,
    .load   = ec_code_x64_load,
    .store  = ec_code_x64_store,
    .copy   = ec_code_x64_copy,
    .xor2   = ec_code_x64_xor2,
    .xor3   = NULL,
    .xorm   = ec_code_x64_xorm
};
