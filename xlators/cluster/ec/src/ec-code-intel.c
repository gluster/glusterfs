/*
  Copyright (c) 2015 DataLab, s.l. <http://www.datalab.es>
  This file is part of GlusterFS.

  This file is licensed to you under your choice of the GNU Lesser
  General Public License, version 3 or any later version (LGPLv3 or
  later), or the GNU General Public License, version 2 (GPLv2), in all
  cases as published by the Free Software Foundation.
*/

#include <inttypes.h>
#include <string.h>
#include <errno.h>

#include "ec-code-intel.h"

static void
ec_code_intel_init(ec_code_intel_t *intel)
{
    memset(intel, 0, sizeof(ec_code_intel_t));
}

static void
ec_code_intel_prefix(ec_code_intel_t *intel, uint8_t prefix)
{
    intel->prefix.data[intel->prefix.bytes++] = prefix;
}

static void
ec_code_intel_rex(ec_code_intel_t *intel, gf_boolean_t w)
{
    gf_boolean_t present = _gf_false;

    if (w) {
        intel->rex.w = 1;
        present = _gf_true;
    }
    if (intel->modrm.present) {
        if (intel->modrm.reg > 7) {
            intel->modrm.reg &= 7;
            intel->rex.r = 1;
            present = _gf_true;
        }
        if (intel->sib.present) {
            if (intel->sib.index > 7) {
                intel->sib.index &= 7;
                intel->rex.x = 1;
                present = _gf_true;
            }
            if (intel->sib.base > 7) {
                intel->sib.base &= 7;
                intel->rex.b = 1;
                present = _gf_true;
            }
        } else if (intel->modrm.rm > 7) {
            intel->modrm.rm &= 7;
            intel->rex.b = 1;
            present = _gf_true;
        }
    } else if (intel->reg > 7) {
        intel->reg &= 7;
        intel->rex.b = 1;
        present = _gf_true;
    }
    intel->rex.present = present;
}

static void
ec_code_intel_vex(ec_code_intel_t *intel, gf_boolean_t w, gf_boolean_t l,
                  ec_code_vex_opcode_t opcode, ec_code_vex_prefix_t prefix,
                  uint32_t reg)
{
    ec_code_intel_rex(intel, w);
    if (((intel->rex.w == 1) ||
         (intel->rex.x == 0) ||
         (intel->rex.b == 0)) ||
        ((opcode != VEX_OPCODE_NONE) && (opcode != VEX_OPCODE_0F))) {
        intel->rex.present = _gf_false;

        intel->vex.bytes = 3;
        intel->vex.data[0] = 0xC4;
        intel->vex.data[1] = ((intel->rex.r << 7) | (intel->rex.x << 6) |
                              (intel->rex.b << 5) | opcode) ^ 0xE0;
        intel->vex.data[2] = (intel->rex.w << 7) | ((~reg & 0x0F) << 3) |
                             (l ? 0x04 : 0x00) | prefix;
    } else {
        intel->vex.bytes = 2;
        intel->vex.data[0] = 0xC5;
        intel->vex.data[1] = (intel->rex.r << 7) | ((~reg & 0x0F) << 3) |
                             (l ? 0x04 : 0x00) | prefix;
    }
}

static void
ec_code_intel_modrm_reg(ec_code_intel_t *intel, uint32_t rm, uint32_t reg)
{
    intel->modrm.present = _gf_true;
    intel->modrm.mod = 3;
    intel->modrm.rm = rm;
    intel->modrm.reg = reg;
}

static void
ec_code_intel_modrm_mem(ec_code_intel_t *intel, uint32_t reg,
                        ec_code_intel_reg_t base, ec_code_intel_reg_t index,
                        uint32_t scale, int32_t offset)
{
    if (index == REG_SP) {
        intel->invalid = _gf_true;
        return;
    }
    if ((index != REG_NULL) && (scale != 1) && (scale != 2) && (scale != 4) &&
        (scale != 8)) {
        intel->invalid = _gf_true;
        return;
    }
    scale >>= 1;
    if (scale == 4) {
        scale = 3;
    }

    intel->modrm.present = _gf_true;
    intel->modrm.reg = reg;

    intel->offset.value = offset;
    if ((offset == 0) && (base != REG_BP)) {
        intel->modrm.mod = 0;
        intel->offset.bytes = 0;
    } else if ((offset >= -128) && (offset <= 127)) {
        intel->modrm.mod = 1;
        intel->offset.bytes = 1;
    } else {
        intel->modrm.mod = 2;
        intel->offset.bytes = 4;
    }

    intel->modrm.rm = base;
    if ((index != REG_NULL) || (base == REG_SP)) {
        intel->modrm.rm = 4;
        intel->sib.present = _gf_true;
        intel->sib.index = index;
        if (index == REG_NULL) {
            intel->sib.index = 4;
        }
        intel->sib.scale = scale;
        intel->sib.base = base;
        if (base == REG_NULL) {
            intel->sib.base = 5;
            intel->modrm.mod = 0;
            intel->offset.bytes = 4;
        }
    } else if (base == REG_NULL) {
        intel->modrm.mod = 0;
        intel->modrm.rm = 5;
        intel->offset.bytes = 4;
    }
}

static void
ec_code_intel_op_1(ec_code_intel_t *intel, uint8_t opcode, uint32_t reg)
{
    intel->reg = reg;
    intel->opcode.bytes = 1;
    intel->opcode.data[0] = opcode;
}

static void
ec_code_intel_op_2(ec_code_intel_t *intel, uint8_t opcode1, uint8_t opcode2,
                   uint32_t reg)
{
    intel->reg = reg;
    intel->opcode.bytes = 2;
    intel->opcode.data[0] = opcode1;
    intel->opcode.data[1] = opcode2;
}

static void
ec_code_intel_immediate_1(ec_code_intel_t *intel, uint32_t value)
{
    intel->immediate.bytes = 1;
    intel->immediate.value = value;
}

static void
ec_code_intel_immediate_2(ec_code_intel_t *intel, uint32_t value)
{
    intel->immediate.bytes = 2;
    intel->immediate.value = value;
}

static void
ec_code_intel_immediate_4(ec_code_intel_t *intel, uint32_t value)
{
    intel->immediate.bytes = 4;
    intel->immediate.value = value;
}

static void
ec_code_intel_emit(ec_code_builder_t *builder, ec_code_intel_t *intel)
{
    uint8_t insn[15];
    uint32_t i, count;

    if (intel->invalid) {
        ec_code_error(builder, EINVAL);
        return;
    }

    count = 0;
    for (i = 0; i < intel->prefix.bytes; i++) {
        insn[count++] = intel->prefix.data[i];
    }
    for (i = 0; i < intel->vex.bytes; i++) {
        insn[count++] = intel->vex.data[i];
    }
    if (intel->rex.present) {
        insn[count++] = 0x40 |
                        (intel->rex.w << 3) |
                        (intel->rex.r << 2) |
                        (intel->rex.x << 1) |
                        (intel->rex.b << 0);
    }
    for (i = 0; i < intel->opcode.bytes; i++) {
        insn[count++] = intel->opcode.data[i];
    }
    if (intel->modrm.present) {
        insn[count++] = (intel->modrm.mod << 6) |
                        (intel->modrm.reg << 3) |
                        (intel->modrm.rm << 0);
        if (intel->sib.present) {
            insn[count++] = (intel->sib.scale << 6) |
                            (intel->sib.index << 3) |
                            (intel->sib.base << 0);
        }
    }
    for (i = 0; i < intel->offset.bytes; i++) {
        insn[count++] = intel->offset.data[i];
    }
    for (i = 0; i < intel->immediate.bytes; i++) {
        insn[count++] = intel->immediate.data[i];
    }

    ec_code_emit(builder, insn, count);
}

void
ec_code_intel_op_push_r(ec_code_builder_t *builder, ec_code_intel_reg_t reg)
{
    ec_code_intel_t intel;

    ec_code_intel_init(&intel);

    ec_code_intel_op_1(&intel, 0x50 | (reg & 7), reg);
    ec_code_intel_rex(&intel, _gf_false);

    ec_code_intel_emit(builder, &intel);
}

void
ec_code_intel_op_pop_r(ec_code_builder_t *builder, ec_code_intel_reg_t reg)
{
    ec_code_intel_t intel;

    ec_code_intel_init(&intel);

    ec_code_intel_op_1(&intel, 0x58 | (reg & 7), reg);
    ec_code_intel_rex(&intel, _gf_false);

    ec_code_intel_emit(builder, &intel);
}

void
ec_code_intel_op_ret(ec_code_builder_t *builder, uint32_t size)
{
    ec_code_intel_t intel;

    ec_code_intel_init(&intel);

    if (size == 0) {
        ec_code_intel_op_1(&intel, 0xC3, 0);
    } else {
        ec_code_intel_immediate_2(&intel, size);
        ec_code_intel_op_1(&intel, 0xC2, 0);
    }
    ec_code_intel_rex(&intel, _gf_false);

    ec_code_intel_emit(builder, &intel);
}

void
ec_code_intel_op_mov_r2r(ec_code_builder_t *builder, ec_code_intel_reg_t src,
                         ec_code_intel_reg_t dst)
{
    ec_code_intel_t intel;

    ec_code_intel_init(&intel);

    ec_code_intel_modrm_reg(&intel, dst, src);
    ec_code_intel_op_1(&intel, 0x89, 0);
    ec_code_intel_rex(&intel, _gf_true);

    ec_code_intel_emit(builder, &intel);
}

void
ec_code_intel_op_mov_r2m(ec_code_builder_t *builder, ec_code_intel_reg_t src,
                         ec_code_intel_reg_t base, ec_code_intel_reg_t index,
                         uint32_t scale, int32_t offset)
{
    ec_code_intel_t intel;

    ec_code_intel_init(&intel);

    ec_code_intel_modrm_mem(&intel, src, base, index, scale, offset);
    ec_code_intel_op_1(&intel, 0x89, 0);
    ec_code_intel_rex(&intel, _gf_true);

    ec_code_intel_emit(builder, &intel);
}

void
ec_code_intel_op_mov_m2r(ec_code_builder_t *builder, ec_code_intel_reg_t base,
                         ec_code_intel_reg_t index, uint32_t scale,
                         int32_t offset, ec_code_intel_reg_t dst)
{
    ec_code_intel_t intel;

    ec_code_intel_init(&intel);

    ec_code_intel_modrm_mem(&intel, dst, base, index, scale, offset);
    ec_code_intel_op_1(&intel, 0x8B, 0);
    ec_code_intel_rex(&intel, _gf_true);

    ec_code_intel_emit(builder, &intel);
}

void
ec_code_intel_op_xor_r2r(ec_code_builder_t *builder, ec_code_intel_reg_t src,
                         ec_code_intel_reg_t dst)
{
    ec_code_intel_t intel;

    ec_code_intel_init(&intel);

    ec_code_intel_modrm_reg(&intel, dst, src);
    ec_code_intel_op_1(&intel, 0x31, 0);
    ec_code_intel_rex(&intel, _gf_true);

    ec_code_intel_emit(builder, &intel);
}

void
ec_code_intel_op_xor_m2r(ec_code_builder_t *builder, ec_code_intel_reg_t base,
                         ec_code_intel_reg_t index, uint32_t scale,
                         int32_t offset, ec_code_intel_reg_t dst)
{
    ec_code_intel_t intel;

    ec_code_intel_init(&intel);

    ec_code_intel_modrm_mem(&intel, dst, base, index, scale, offset);
    ec_code_intel_op_1(&intel, 0x33, 0);
    ec_code_intel_rex(&intel, _gf_true);

    ec_code_intel_emit(builder, &intel);
}

void
ec_code_intel_op_add_i2r(ec_code_builder_t *builder, int32_t value,
                         ec_code_intel_reg_t reg)
{
    ec_code_intel_t intel;

    ec_code_intel_init(&intel);

    if ((value >= -128) && (value < 128)) {
        ec_code_intel_modrm_reg(&intel, reg, 0);
        ec_code_intel_op_1(&intel, 0x83, 0);
        ec_code_intel_immediate_1(&intel, value);
    } else {
        if (reg == REG_AX) {
            ec_code_intel_op_1(&intel, 0x05, reg);
        } else {
            ec_code_intel_modrm_reg(&intel, reg, 0);
            ec_code_intel_op_1(&intel, 0x81, 0);
        }
        ec_code_intel_immediate_4(&intel, value);
    }
    ec_code_intel_rex(&intel, _gf_true);

    ec_code_intel_emit(builder, &intel);
}

void
ec_code_intel_op_test_i2r(ec_code_builder_t *builder, uint32_t value,
                          ec_code_intel_reg_t reg)
{
    ec_code_intel_t intel;

    ec_code_intel_init(&intel);

    if (reg == REG_AX) {
        ec_code_intel_op_1(&intel, 0xA9, reg);
    } else {
        ec_code_intel_modrm_reg(&intel, reg, 0);
        ec_code_intel_op_1(&intel, 0xF7, 0);
    }
    ec_code_intel_immediate_4(&intel, value);
    ec_code_intel_rex(&intel, _gf_true);

    ec_code_intel_emit(builder, &intel);
}

void
ec_code_intel_op_jne(ec_code_builder_t *builder, uint32_t address)
{
    ec_code_intel_t intel;
    int32_t rel;

    ec_code_intel_init(&intel);

    rel = address - builder->address - 2;
    if ((rel >= -128) && (rel < 128)) {
        ec_code_intel_op_1(&intel, 0x75, 0);
        ec_code_intel_immediate_1(&intel, rel);
    } else {
        rel -= 4;
        ec_code_intel_op_2(&intel, 0x0F, 0x85, 0);
        ec_code_intel_immediate_4(&intel, rel);
    }
    ec_code_intel_rex(&intel, _gf_false);

    ec_code_intel_emit(builder, &intel);
}

void
ec_code_intel_op_mov_sse2sse(ec_code_builder_t *builder, uint32_t src,
                             uint32_t dst)
{
    ec_code_intel_t intel;

    ec_code_intel_init(&intel);

    ec_code_intel_prefix(&intel, 0x66);
    ec_code_intel_modrm_reg(&intel, src, dst);
    ec_code_intel_op_2(&intel, 0x0F, 0x6F, 0);
    ec_code_intel_rex(&intel, _gf_false);

    ec_code_intel_emit(builder, &intel);
}

void
ec_code_intel_op_mov_sse2m(ec_code_builder_t *builder, uint32_t src,
                           ec_code_intel_reg_t base, ec_code_intel_reg_t index,
                           uint32_t scale, int32_t offset)
{
    ec_code_intel_t intel;

    ec_code_intel_init(&intel);

    ec_code_intel_prefix(&intel, 0x66);
    ec_code_intel_modrm_mem(&intel, src, base, index, scale, offset);
    ec_code_intel_op_2(&intel, 0x0F, 0x7F, 0);
    ec_code_intel_rex(&intel, _gf_false);

    ec_code_intel_emit(builder, &intel);
}

void
ec_code_intel_op_mov_m2sse(ec_code_builder_t *builder,
                           ec_code_intel_reg_t base, ec_code_intel_reg_t index,
                           uint32_t scale, int32_t offset, uint32_t dst)
{
    ec_code_intel_t intel;

    ec_code_intel_init(&intel);

    ec_code_intel_prefix(&intel, 0x66);
    ec_code_intel_modrm_mem(&intel, dst, base, index, scale, offset);
    ec_code_intel_op_2(&intel, 0x0F, 0x6F, 0);
    ec_code_intel_rex(&intel, _gf_false);

    ec_code_intel_emit(builder, &intel);
}

void
ec_code_intel_op_xor_sse2sse(ec_code_builder_t *builder, uint32_t src,
                             uint32_t dst)
{
    ec_code_intel_t intel;

    ec_code_intel_init(&intel);

    ec_code_intel_prefix(&intel, 0x66);
    ec_code_intel_modrm_reg(&intel, src, dst);
    ec_code_intel_op_2(&intel, 0x0F, 0xEF, 0);
    ec_code_intel_rex(&intel, _gf_false);

    ec_code_intel_emit(builder, &intel);
}

void
ec_code_intel_op_xor_m2sse(ec_code_builder_t *builder,
                           ec_code_intel_reg_t base, ec_code_intel_reg_t index,
                           uint32_t scale, int32_t offset, uint32_t dst)
{
    ec_code_intel_t intel;

    ec_code_intel_init(&intel);

    ec_code_intel_prefix(&intel, 0x66);
    ec_code_intel_modrm_mem(&intel, dst, base, index, scale, offset);
    ec_code_intel_op_2(&intel, 0x0F, 0xEF, 0);
    ec_code_intel_rex(&intel, _gf_false);

    ec_code_intel_emit(builder, &intel);
}

void
ec_code_intel_op_mov_avx2avx(ec_code_builder_t *builder, uint32_t src,
                             uint32_t dst)
{
    ec_code_intel_t intel;

    ec_code_intel_init(&intel);

    ec_code_intel_modrm_reg(&intel, src, dst);
    ec_code_intel_op_1(&intel, 0x6F, 0);
    ec_code_intel_vex(&intel, _gf_false, _gf_true, VEX_OPCODE_0F,
                      VEX_PREFIX_66, VEX_REG_NONE);

    ec_code_intel_emit(builder, &intel);
}

void
ec_code_intel_op_mov_avx2m(ec_code_builder_t *builder, uint32_t src,
                           ec_code_intel_reg_t base, ec_code_intel_reg_t index,
                           uint32_t scale, int32_t offset)
{
    ec_code_intel_t intel;

    ec_code_intel_init(&intel);

    ec_code_intel_modrm_mem(&intel, src, base, index, scale, offset);
    ec_code_intel_op_1(&intel, 0x7F, 0);
    ec_code_intel_vex(&intel, _gf_false, _gf_true, VEX_OPCODE_0F,
                      VEX_PREFIX_66, VEX_REG_NONE);

    ec_code_intel_emit(builder, &intel);
}

void
ec_code_intel_op_mov_m2avx(ec_code_builder_t *builder,
                           ec_code_intel_reg_t base, ec_code_intel_reg_t index,
                           uint32_t scale, int32_t offset, uint32_t dst)
{
    ec_code_intel_t intel;

    ec_code_intel_init(&intel);

    ec_code_intel_modrm_mem(&intel, dst, base, index, scale, offset);
    ec_code_intel_op_1(&intel, 0x6F, 0);
    ec_code_intel_vex(&intel, _gf_false, _gf_true, VEX_OPCODE_0F,
                      VEX_PREFIX_66, VEX_REG_NONE);

    ec_code_intel_emit(builder, &intel);
}

void
ec_code_intel_op_xor_avx2avx(ec_code_builder_t *builder, uint32_t src,
                             uint32_t dst)
{
    ec_code_intel_t intel;

    ec_code_intel_init(&intel);

    ec_code_intel_modrm_reg(&intel, src, dst);
    ec_code_intel_op_1(&intel, 0xEF, 0);
    ec_code_intel_vex(&intel, _gf_false, _gf_true, VEX_OPCODE_0F,
                      VEX_PREFIX_66, dst);

    ec_code_intel_emit(builder, &intel);
}

void
ec_code_intel_op_xor_m2avx(ec_code_builder_t *builder,
                           ec_code_intel_reg_t base, ec_code_intel_reg_t index,
                           uint32_t scale, int32_t offset, uint32_t dst)
{
    ec_code_intel_t intel;

    ec_code_intel_init(&intel);

    ec_code_intel_modrm_mem(&intel, dst, base, index, scale, offset);
    ec_code_intel_op_1(&intel, 0xEF, 0);
    ec_code_intel_vex(&intel, _gf_false, _gf_true, VEX_OPCODE_0F,
                      VEX_PREFIX_66, dst);

    ec_code_intel_emit(builder, &intel);
}
