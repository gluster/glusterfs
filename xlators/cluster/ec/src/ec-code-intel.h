/*
  Copyright (c) 2015 DataLab, s.l. <http://www.datalab.es>
  This file is part of GlusterFS.

  This file is licensed to you under your choice of the GNU Lesser
  General Public License, version 3 or any later version (LGPLv3 or
  later), or the GNU General Public License, version 2 (GPLv2), in all
  cases as published by the Free Software Foundation.
*/

#ifndef __EC_CODE_INTEL_H__
#define __EC_CODE_INTEL_H__

#include "ec-code.h"

#define VEX_REG_NONE 0

enum _ec_code_intel_reg;
typedef enum _ec_code_intel_reg ec_code_intel_reg_t;

enum _ec_code_vex_prefix;
typedef enum _ec_code_vex_prefix ec_code_vex_prefix_t;

enum _ec_code_vex_opcode;
typedef enum _ec_code_vex_opcode ec_code_vex_opcode_t;

struct _ec_code_intel_buffer;
typedef struct _ec_code_intel_buffer ec_code_intel_buffer_t;

struct _ec_code_intel_sib;
typedef struct _ec_code_intel_sib ec_code_intel_sib_t;

struct _ec_code_intel_modrm;
typedef struct _ec_code_intel_modrm ec_code_intel_modrm_t;

struct _ec_code_intel_rex;
typedef struct _ec_code_intel_rex ec_code_intel_rex_t;

struct _ec_code_intel;
typedef struct _ec_code_intel ec_code_intel_t;

enum _ec_code_intel_reg {
    REG_NULL = -1,
    REG_AX,
    REG_CX,
    REG_DX,
    REG_BX,
    REG_SP,
    REG_BP,
    REG_SI,
    REG_DI,
    REG_8,
    REG_9,
    REG_10,
    REG_11,
    REG_12,
    REG_13,
    REG_14,
    REG_15
};

enum _ec_code_vex_prefix {
    VEX_PREFIX_NONE = 0,
    VEX_PREFIX_66,
    VEX_PREFIX_F3,
    VEX_PREFIX_F2
};

enum _ec_code_vex_opcode {
    VEX_OPCODE_NONE = 0,
    VEX_OPCODE_0F,
    VEX_OPCODE_0F_38,
    VEX_OPCODE_0F_3A
};

struct _ec_code_intel_buffer {
    uint32_t bytes;
    union {
        uint8_t  data[4];
        uint32_t value;
    };
};

struct _ec_code_intel_sib {
    gf_boolean_t present;
    uint32_t     base;
    uint32_t     index;
    uint32_t     scale;
};

struct _ec_code_intel_modrm {
    gf_boolean_t present;
    uint32_t     mod;
    uint32_t     rm;
    uint32_t     reg;
};

struct _ec_code_intel_rex {
    gf_boolean_t present;
    uint32_t     w;
    uint32_t     r;
    uint32_t     x;
    uint32_t     b;
};

struct _ec_code_intel {
    gf_boolean_t           invalid;
    ec_code_intel_buffer_t prefix;
    ec_code_intel_buffer_t opcode;
    ec_code_intel_buffer_t offset;
    ec_code_intel_buffer_t immediate;
    ec_code_intel_buffer_t vex;
    ec_code_intel_rex_t    rex;
    ec_code_intel_modrm_t  modrm;
    ec_code_intel_sib_t    sib;
    uint32_t               reg;
};

void ec_code_intel_op_push_r(ec_code_builder_t *builder,
                             ec_code_intel_reg_t reg);
void ec_code_intel_op_pop_r(ec_code_builder_t *builder,
                            ec_code_intel_reg_t reg);
void ec_code_intel_op_ret(ec_code_builder_t *builder, uint32_t size);

void ec_code_intel_op_mov_r2r(ec_code_builder_t *builder,
                              ec_code_intel_reg_t src,
                              ec_code_intel_reg_t dst);
void ec_code_intel_op_mov_r2m(ec_code_builder_t *builder,
                              ec_code_intel_reg_t src,
                              ec_code_intel_reg_t base,
                              ec_code_intel_reg_t index, uint32_t scale,
                              int32_t offset);
void ec_code_intel_op_mov_m2r(ec_code_builder_t *builder,
                              ec_code_intel_reg_t base,
                              ec_code_intel_reg_t index, uint32_t scale,
                              int32_t offset, ec_code_intel_reg_t dst);
void ec_code_intel_op_xor_r2r(ec_code_builder_t *builder,
                              ec_code_intel_reg_t src,
                              ec_code_intel_reg_t dst);
void ec_code_intel_op_xor_m2r(ec_code_builder_t *builder,
                              ec_code_intel_reg_t base,
                              ec_code_intel_reg_t index, uint32_t scale,
                              int32_t offset, ec_code_intel_reg_t dst);
void ec_code_intel_op_add_i2r(ec_code_builder_t *builder, int32_t value,
                              ec_code_intel_reg_t reg);
void ec_code_intel_op_test_i2r(ec_code_builder_t *builder, uint32_t value,
                               ec_code_intel_reg_t reg);
void ec_code_intel_op_jne(ec_code_builder_t *builder, uint32_t address);

void ec_code_intel_op_mov_sse2sse(ec_code_builder_t *builder, uint32_t src,
                                  uint32_t dst);
void ec_code_intel_op_mov_sse2m(ec_code_builder_t *builder, uint32_t src,
                                ec_code_intel_reg_t base,
                                ec_code_intel_reg_t index, uint32_t scale,
                                int32_t offset);
void ec_code_intel_op_mov_m2sse(ec_code_builder_t *builder,
                                ec_code_intel_reg_t base,
                                ec_code_intel_reg_t index, uint32_t scale,
                                int32_t offset, uint32_t dst);
void ec_code_intel_op_xor_sse2sse(ec_code_builder_t *builder, uint32_t src,
                                  uint32_t dst);
void ec_code_intel_op_xor_m2sse(ec_code_builder_t *builder,
                                ec_code_intel_reg_t base,
                                ec_code_intel_reg_t index, uint32_t scale,
                                int32_t offset, uint32_t dst);

void ec_code_intel_op_mov_avx2avx(ec_code_builder_t *builder, uint32_t src,
                                  uint32_t dst);
void ec_code_intel_op_mov_avx2m(ec_code_builder_t *builder, uint32_t src,
                                ec_code_intel_reg_t base,
                                ec_code_intel_reg_t index, uint32_t scale,
                                int32_t offset);
void ec_code_intel_op_mov_m2avx(ec_code_builder_t *builder,
                                ec_code_intel_reg_t base,
                                ec_code_intel_reg_t index, uint32_t scale,
                                int32_t offset, uint32_t dst);
void ec_code_intel_op_xor_avx2avx(ec_code_builder_t *builder, uint32_t src,
                                  uint32_t dst);
void ec_code_intel_op_xor_m2avx(ec_code_builder_t *builder,
                                ec_code_intel_reg_t base,
                                ec_code_intel_reg_t index, uint32_t scale,
                                int32_t offset, uint32_t dst);

#endif /* __EC_CODE_INTEL_H__ */
