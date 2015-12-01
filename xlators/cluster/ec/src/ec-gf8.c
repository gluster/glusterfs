/*
  Copyright (c) 2015 DataLab, s.l. <http://www.datalab.es>
  This file is part of GlusterFS.

  This file is licensed to you under your choice of the GNU Lesser
  General Public License, version 3 or any later version (LGPLv3 or
  later), or the GNU General Public License, version 2 (GPLv2), in all
  cases as published by the Free Software Foundation.
*/

#include "ec-gf8.h"

static ec_gf_op_t ec_gf8_mul_00_ops[] = {
    { EC_GF_OP_END,    0,  0,  0 }
};

static ec_gf_mul_t ec_gf8_mul_00 = {
    0,
    { 0, },
    ec_gf8_mul_00_ops
};

static ec_gf_op_t ec_gf8_mul_01_ops[] = {
    { EC_GF_OP_END,    0,  0,  0 }
};

static ec_gf_mul_t ec_gf8_mul_01 = {
    8,
    { 0, 1, 2, 3, 4, 5, 6, 7, },
    ec_gf8_mul_01_ops
};

static ec_gf_op_t ec_gf8_mul_02_ops[] = {
    { EC_GF_OP_XOR2,   1,  7,  0 },
    { EC_GF_OP_XOR2,   2,  7,  0 },
    { EC_GF_OP_XOR2,   3,  7,  0 },
    { EC_GF_OP_END,    0,  0,  0 }
};

static ec_gf_mul_t ec_gf8_mul_02 = {
    8,
    { 7, 0, 1, 2, 3, 4, 5, 6, },
    ec_gf8_mul_02_ops
};

static ec_gf_op_t ec_gf8_mul_03_ops[] = {
    { EC_GF_OP_XOR2,   3,  7,  0 },
    { EC_GF_OP_COPY,   8,  3,  0 },
    { EC_GF_OP_XOR2,   3,  2,  0 },
    { EC_GF_OP_XOR2,   2,  1,  0 },
    { EC_GF_OP_XOR2,   1,  0,  0 },
    { EC_GF_OP_XOR2,   2,  7,  0 },
    { EC_GF_OP_XOR2,   0,  7,  0 },
    { EC_GF_OP_XOR2,   7,  6,  0 },
    { EC_GF_OP_XOR2,   6,  5,  0 },
    { EC_GF_OP_XOR2,   5,  4,  0 },
    { EC_GF_OP_XOR2,   4,  8,  0 },
    { EC_GF_OP_END,    0,  0,  0 }
};

static ec_gf_mul_t ec_gf8_mul_03 = {
    9,
    { 0, 1, 2, 3, 4, 5, 6, 7, 8, },
    ec_gf8_mul_03_ops
};

static ec_gf_op_t ec_gf8_mul_04_ops[] = {
    { EC_GF_OP_XOR3,   8,  6,  7 },
    { EC_GF_OP_XOR2,   2,  8,  0 },
    { EC_GF_OP_XOR2,   3,  7,  0 },
    { EC_GF_OP_XOR2,   1,  8,  0 },
    { EC_GF_OP_XOR2,   0,  6,  0 },
    { EC_GF_OP_END,    0,  0,  0 }
};

static ec_gf_mul_t ec_gf8_mul_04 = {
    9,
    { 6, 7, 0, 1, 2, 3, 4, 5, 8, },
    ec_gf8_mul_04_ops
};

static ec_gf_op_t ec_gf8_mul_05_ops[] = {
    { EC_GF_OP_XOR2,   4,  6,  0 },
    { EC_GF_OP_XOR2,   1,  7,  0 },
    { EC_GF_OP_XOR2,   5,  7,  0 },
    { EC_GF_OP_XOR2,   0,  6,  0 },
    { EC_GF_OP_XOR2,   7,  4,  0 },
    { EC_GF_OP_XOR2,   6,  3,  0 },
    { EC_GF_OP_XOR2,   7,  2,  0 },
    { EC_GF_OP_XOR2,   6,  1,  0 },
    { EC_GF_OP_XOR2,   3,  5,  0 },
    { EC_GF_OP_XOR2,   2,  0,  0 },
    { EC_GF_OP_END,    0,  0,  0 }
};

static ec_gf_mul_t ec_gf8_mul_05 = {
    8,
    { 0, 1, 2, 6, 7, 3, 4, 5, },
    ec_gf8_mul_05_ops
};

static ec_gf_op_t ec_gf8_mul_06_ops[] = {
    { EC_GF_OP_XOR2,   2,  6,  0 },
    { EC_GF_OP_COPY,   8,  2,  0 },
    { EC_GF_OP_XOR2,   8,  3,  0 },
    { EC_GF_OP_XOR2,   2,  1,  0 },
    { EC_GF_OP_XOR2,   3,  4,  0 },
    { EC_GF_OP_XOR2,   1,  0,  0 },
    { EC_GF_OP_XOR2,   3,  7,  0 },
    { EC_GF_OP_XOR2,   0,  7,  0 },
    { EC_GF_OP_XOR2,   7,  6,  0 },
    { EC_GF_OP_XOR2,   4,  5,  0 },
    { EC_GF_OP_XOR2,   1,  7,  0 },
    { EC_GF_OP_XOR2,   5,  6,  0 },
    { EC_GF_OP_END,    0,  0,  0 }
};

static ec_gf_mul_t ec_gf8_mul_06 = {
    9,
    { 7, 0, 1, 2, 8, 3, 4, 5, 6, },
    ec_gf8_mul_06_ops
};

static ec_gf_op_t ec_gf8_mul_07_ops[] = {
    { EC_GF_OP_XOR2,   5,  6,  0 },
    { EC_GF_OP_XOR2,   0,  7,  0 },
    { EC_GF_OP_XOR2,   3,  6,  0 },
    { EC_GF_OP_XOR2,   7,  5,  0 },
    { EC_GF_OP_XOR2,   6,  0,  0 },
    { EC_GF_OP_XOR2,   0,  1,  0 },
    { EC_GF_OP_XOR2,   5,  4,  0 },
    { EC_GF_OP_XOR2,   4,  3,  0 },
    { EC_GF_OP_XOR2,   1,  2,  0 },
    { EC_GF_OP_XOR2,   2,  4,  0 },
    { EC_GF_OP_XOR2,   3,  1,  0 },
    { EC_GF_OP_XOR2,   4,  7,  0 },
    { EC_GF_OP_XOR2,   1,  6,  0 },
    { EC_GF_OP_END,    0,  0,  0 }
};

static ec_gf_mul_t ec_gf8_mul_07 = {
    8,
    { 6, 0, 1, 3, 2, 4, 5, 7, },
    ec_gf8_mul_07_ops
};

static ec_gf_op_t ec_gf8_mul_08_ops[] = {
    { EC_GF_OP_XOR2,   0,  5,  0 },
    { EC_GF_OP_XOR2,   1,  5,  0 },
    { EC_GF_OP_XOR2,   0,  6,  0 },
    { EC_GF_OP_XOR3,   8,  6,  7 },
    { EC_GF_OP_XOR2,   1,  8,  0 },
    { EC_GF_OP_XOR2,   3,  7,  0 },
    { EC_GF_OP_XOR2,   7,  5,  0 },
    { EC_GF_OP_XOR2,   2,  8,  0 },
    { EC_GF_OP_END,    0,  0,  0 }
};

static ec_gf_mul_t ec_gf8_mul_08 = {
    9,
    { 5, 6, 7, 0, 1, 2, 3, 4, 8, },
    ec_gf8_mul_08_ops
};

static ec_gf_op_t ec_gf8_mul_09_ops[] = {
    { EC_GF_OP_XOR2,   2,  4,  0 },
    { EC_GF_OP_XOR2,   4,  7,  0 },
    { EC_GF_OP_XOR2,   0,  5,  0 },
    { EC_GF_OP_XOR2,   5,  4,  0 },
    { EC_GF_OP_XOR2,   3,  6,  0 },
    { EC_GF_OP_XOR2,   2,  5,  0 },
    { EC_GF_OP_XOR2,   1,  6,  0 },
    { EC_GF_OP_XOR2,   7,  3,  0 },
    { EC_GF_OP_XOR2,   6,  2,  0 },
    { EC_GF_OP_XOR2,   5,  1,  0 },
    { EC_GF_OP_XOR2,   3,  0,  0 },
    { EC_GF_OP_END,    0,  0,  0 }
};

static ec_gf_mul_t ec_gf8_mul_09 = {
    8,
    { 0, 1, 2, 3, 5, 6, 7, 4, },
    ec_gf8_mul_09_ops
};

static ec_gf_op_t ec_gf8_mul_0A_ops[] = {
    { EC_GF_OP_XOR2,   1,  5,  0 },
    { EC_GF_OP_XOR2,   4,  6,  0 },
    { EC_GF_OP_XOR2,   5,  7,  0 },
    { EC_GF_OP_XOR2,   7,  4,  0 },
    { EC_GF_OP_XOR2,   0,  6,  0 },
    { EC_GF_OP_XOR2,   7,  2,  0 },
    { EC_GF_OP_XOR2,   6,  1,  0 },
    { EC_GF_OP_XOR2,   2,  0,  0 },
    { EC_GF_OP_XOR2,   6,  3,  0 },
    { EC_GF_OP_XOR2,   2,  5,  0 },
    { EC_GF_OP_XOR2,   3,  5,  0 },
    { EC_GF_OP_END,    0,  0,  0 }
};

static ec_gf_mul_t ec_gf8_mul_0A = {
    8,
    { 5, 0, 1, 2, 6, 7, 3, 4, },
    ec_gf8_mul_0A_ops
};

static ec_gf_op_t ec_gf8_mul_0B_ops[] = {
    { EC_GF_OP_XOR2,   4,  7,  0 },
    { EC_GF_OP_XOR2,   7,  0,  0 },
    { EC_GF_OP_XOR2,   7,  5,  0 },
    { EC_GF_OP_XOR2,   3,  7,  0 },
    { EC_GF_OP_COPY,   9,  3,  0 },
    { EC_GF_OP_XOR2,   5,  2,  0 },
    { EC_GF_OP_XOR2,   3,  6,  0 },
    { EC_GF_OP_COPY,   8,  5,  0 },
    { EC_GF_OP_XOR2,   0,  3,  0 },
    { EC_GF_OP_XOR2,   5,  1,  0 },
    { EC_GF_OP_XOR2,   6,  4,  0 },
    { EC_GF_OP_XOR2,   1,  0,  0 },
    { EC_GF_OP_XOR2,   2,  3,  0 },
    { EC_GF_OP_XOR2,   4,  1,  0 },
    { EC_GF_OP_XOR3,   3,  8,  6 },
    { EC_GF_OP_XOR2,   1,  9,  0 },
    { EC_GF_OP_END,    0,  0,  0 }
};

static ec_gf_mul_t ec_gf8_mul_0B = {
    10,
    { 7, 1, 5, 2, 4, 3, 0, 6, 8, 9, },
    ec_gf8_mul_0B_ops
};

static ec_gf_op_t ec_gf8_mul_0C_ops[] = {
    { EC_GF_OP_XOR2,   1,  5,  0 },
    { EC_GF_OP_COPY,   8,  1,  0 },
    { EC_GF_OP_XOR2,   8,  2,  0 },
    { EC_GF_OP_XOR2,   2,  3,  0 },
    { EC_GF_OP_XOR2,   3,  4,  0 },
    { EC_GF_OP_XOR2,   0,  7,  0 },
    { EC_GF_OP_XOR2,   4,  5,  0 },
    { EC_GF_OP_XOR2,   3,  7,  0 },
    { EC_GF_OP_XOR2,   5,  6,  0 },
    { EC_GF_OP_XOR2,   1,  0,  0 },
    { EC_GF_OP_XOR2,   2,  6,  0 },
    { EC_GF_OP_XOR2,   7,  6,  0 },
    { EC_GF_OP_XOR2,   0,  5,  0 },
    { EC_GF_OP_END,    0,  0,  0 }
};

static ec_gf_mul_t ec_gf8_mul_0C = {
    9,
    { 5, 7, 0, 1, 8, 2, 3, 4, 6, },
    ec_gf8_mul_0C_ops
};

static ec_gf_op_t ec_gf8_mul_0D_ops[] = {
    { EC_GF_OP_XOR2,   5,  7,  0 },
    { EC_GF_OP_XOR2,   4,  5,  0 },
    { EC_GF_OP_XOR2,   5,  0,  0 },
    { EC_GF_OP_XOR2,   5,  1,  0 },
    { EC_GF_OP_XOR2,   1,  7,  0 },
    { EC_GF_OP_XOR2,   0,  3,  0 },
    { EC_GF_OP_XOR2,   6,  1,  0 },
    { EC_GF_OP_XOR2,   3,  5,  0 },
    { EC_GF_OP_XOR3,   8,  2,  4 },
    { EC_GF_OP_XOR2,   5,  6,  0 },
    { EC_GF_OP_XOR2,   2,  5,  0 },
    { EC_GF_OP_XOR2,   1,  8,  0 },
    { EC_GF_OP_XOR2,   0,  2,  0 },
    { EC_GF_OP_XOR2,   7,  2,  0 },
    { EC_GF_OP_XOR3,   2,  8,  0 },
    { EC_GF_OP_END,    0,  0,  0 }
};

static ec_gf_mul_t ec_gf8_mul_0D = {
    9,
    { 5, 6, 7, 3, 1, 0, 2, 4, 8, },
    ec_gf8_mul_0D_ops
};

static ec_gf_op_t ec_gf8_mul_0E_ops[] = {
    { EC_GF_OP_XOR2,   0,  5,  0 },
    { EC_GF_OP_XOR2,   5,  6,  0 },
    { EC_GF_OP_XOR2,   1,  0,  0 },
    { EC_GF_OP_XOR2,   7,  5,  0 },
    { EC_GF_OP_XOR2,   3,  6,  0 },
    { EC_GF_OP_XOR2,   0,  7,  0 },
    { EC_GF_OP_XOR2,   5,  4,  0 },
    { EC_GF_OP_XOR2,   6,  1,  0 },
    { EC_GF_OP_XOR2,   4,  3,  0 },
    { EC_GF_OP_XOR2,   1,  2,  0 },
    { EC_GF_OP_XOR2,   3,  0,  0 },
    { EC_GF_OP_XOR2,   2,  4,  0 },
    { EC_GF_OP_XOR2,   3,  1,  0 },
    { EC_GF_OP_XOR2,   4,  7,  0 },
    { EC_GF_OP_END,    0,  0,  0 }
};

static ec_gf_mul_t ec_gf8_mul_0E = {
    8,
    { 7, 0, 6, 1, 3, 2, 4, 5, },
    ec_gf8_mul_0E_ops
};

static ec_gf_op_t ec_gf8_mul_0F_ops[] = {
    { EC_GF_OP_XOR2,   2,  7,  0 },
    { EC_GF_OP_XOR2,   7,  1,  0 },
    { EC_GF_OP_XOR2,   7,  6,  0 },
    { EC_GF_OP_XOR2,   4,  0,  0 },
    { EC_GF_OP_XOR2,   0,  7,  0 },
    { EC_GF_OP_XOR2,   5,  0,  0 },
    { EC_GF_OP_XOR2,   6,  3,  0 },
    { EC_GF_OP_XOR2,   1,  5,  0 },
    { EC_GF_OP_XOR2,   5,  2,  0 },
    { EC_GF_OP_XOR2,   4,  1,  0 },
    { EC_GF_OP_XOR2,   3,  4,  0 },
    { EC_GF_OP_XOR2,   6,  5,  0 },
    { EC_GF_OP_XOR2,   2,  3,  0 },
    { EC_GF_OP_XOR2,   7,  2,  0 },
    { EC_GF_OP_END,    0,  0,  0 }
};

static ec_gf_mul_t ec_gf8_mul_0F = {
    8,
    { 1, 0, 5, 6, 7, 2, 3, 4, },
    ec_gf8_mul_0F_ops
};

static ec_gf_op_t ec_gf8_mul_10_ops[] = {
    { EC_GF_OP_XOR2,   3,  7,  0 },
    { EC_GF_OP_XOR2,   7,  6,  0 },
    { EC_GF_OP_XOR2,   2,  7,  0 },
    { EC_GF_OP_XOR2,   7,  5,  0 },
    { EC_GF_OP_XOR2,   6,  4,  0 },
    { EC_GF_OP_XOR2,   0,  5,  0 },
    { EC_GF_OP_XOR2,   1,  7,  0 },
    { EC_GF_OP_XOR2,   0,  6,  0 },
    { EC_GF_OP_XOR2,   7,  6,  0 },
    { EC_GF_OP_END,    0,  0,  0 }
};

static ec_gf_mul_t ec_gf8_mul_10 = {
    8,
    { 4, 5, 6, 7, 0, 1, 2, 3, },
    ec_gf8_mul_10_ops
};

static ec_gf_op_t ec_gf8_mul_11_ops[] = {
    { EC_GF_OP_XOR2,   1,  5,  0 },
    { EC_GF_OP_XOR2,   5,  6,  0 },
    { EC_GF_OP_XOR2,   6,  4,  0 },
    { EC_GF_OP_XOR2,   4,  0,  0 },
    { EC_GF_OP_XOR2,   0,  5,  0 },
    { EC_GF_OP_XOR2,   5,  7,  0 },
    { EC_GF_OP_XOR2,   7,  2,  0 },
    { EC_GF_OP_XOR2,   2,  6,  0 },
    { EC_GF_OP_XOR2,   6,  3,  0 },
    { EC_GF_OP_XOR2,   6,  5,  0 },
    { EC_GF_OP_XOR2,   5,  1,  0 },
    { EC_GF_OP_END,    0,  0,  0 }
};

static ec_gf_mul_t ec_gf8_mul_11 = {
    8,
    { 4, 1, 2, 6, 0, 5, 7, 3, },
    ec_gf8_mul_11_ops
};

static ec_gf_op_t ec_gf8_mul_12_ops[] = {
    { EC_GF_OP_XOR2,   7,  4,  0 },
    { EC_GF_OP_XOR2,   6,  7,  0 },
    { EC_GF_OP_XOR2,   2,  4,  0 },
    { EC_GF_OP_XOR2,   3,  6,  0 },
    { EC_GF_OP_XOR2,   4,  3,  0 },
    { EC_GF_OP_XOR2,   1,  6,  0 },
    { EC_GF_OP_XOR2,   2,  5,  0 },
    { EC_GF_OP_XOR2,   0,  5,  0 },
    { EC_GF_OP_XOR2,   6,  2,  0 },
    { EC_GF_OP_XOR2,   5,  1,  0 },
    { EC_GF_OP_XOR2,   3,  0,  0 },
    { EC_GF_OP_END,    0,  0,  0 }
};

static ec_gf_mul_t ec_gf8_mul_12 = {
    8,
    { 7, 0, 1, 2, 3, 5, 6, 4, },
    ec_gf8_mul_12_ops
};

static ec_gf_op_t ec_gf8_mul_13_ops[] = {
    { EC_GF_OP_XOR2,   4,  7,  0 },
    { EC_GF_OP_XOR2,   7,  5,  0 },
    { EC_GF_OP_XOR2,   3,  6,  0 },
    { EC_GF_OP_XOR2,   5,  1,  0 },
    { EC_GF_OP_XOR2,   6,  4,  0 },
    { EC_GF_OP_XOR2,   7,  2,  0 },
    { EC_GF_OP_XOR2,   4,  0,  0 },
    { EC_GF_OP_XOR2,   5,  0,  0 },
    { EC_GF_OP_XOR2,   1,  6,  0 },
    { EC_GF_OP_XOR3,   8,  3,  7 },
    { EC_GF_OP_XOR2,   0,  2,  0 },
    { EC_GF_OP_XOR2,   6,  8,  0 },
    { EC_GF_OP_XOR2,   2,  1,  0 },
    { EC_GF_OP_XOR2,   0,  8,  0 },
    { EC_GF_OP_END,    0,  0,  0 }
};

static ec_gf_mul_t ec_gf8_mul_13 = {
    9,
    { 4, 5, 2, 6, 0, 1, 7, 3, 8, },
    ec_gf8_mul_13_ops
};

static ec_gf_op_t ec_gf8_mul_14_ops[] = {
    { EC_GF_OP_XOR2,   6,  4,  0 },
    { EC_GF_OP_XOR2,   7,  5,  0 },
    { EC_GF_OP_XOR2,   5,  6,  0 },
    { EC_GF_OP_XOR2,   2,  7,  0 },
    { EC_GF_OP_XOR2,   0,  4,  0 },
    { EC_GF_OP_XOR2,   1,  5,  0 },
    { EC_GF_OP_XOR2,   5,  2,  0 },
    { EC_GF_OP_XOR2,   4,  3,  0 },
    { EC_GF_OP_XOR2,   2,  0,  0 },
    { EC_GF_OP_XOR2,   3,  7,  0 },
    { EC_GF_OP_XOR2,   4,  1,  0 },
    { EC_GF_OP_END,    0,  0,  0 }
};

static ec_gf_mul_t ec_gf8_mul_14 = {
    8,
    { 6, 7, 0, 1, 2, 4, 5, 3, },
    ec_gf8_mul_14_ops
};

static ec_gf_op_t ec_gf8_mul_15_ops[] = {
    { EC_GF_OP_COPY,   8,  0,  0 },
    { EC_GF_OP_XOR2,   0,  4,  0 },
    { EC_GF_OP_XOR2,   2,  0,  0 },
    { EC_GF_OP_XOR2,   0,  6,  0 },
    { EC_GF_OP_XOR2,   6,  1,  0 },
    { EC_GF_OP_XOR2,   1,  5,  0 },
    { EC_GF_OP_XOR2,   4,  5,  0 },
    { EC_GF_OP_XOR2,   1,  7,  0 },
    { EC_GF_OP_XOR2,   6,  3,  0 },
    { EC_GF_OP_XOR2,   7,  2,  0 },
    { EC_GF_OP_XOR2,   3,  5,  0 },
    { EC_GF_OP_XOR3,   5,  8,  7 },
    { EC_GF_OP_XOR2,   7,  4,  0 },
    { EC_GF_OP_XOR2,   4,  6,  0 },
    { EC_GF_OP_END,    0,  0,  0 }
};

static ec_gf_mul_t ec_gf8_mul_15 = {
    9,
    { 0, 1, 2, 4, 7, 6, 5, 3, 8, },
    ec_gf8_mul_15_ops
};

static ec_gf_op_t ec_gf8_mul_16_ops[] = {
    { EC_GF_OP_XOR2,   4,  7,  0 },
    { EC_GF_OP_XOR2,   3,  6,  0 },
    { EC_GF_OP_XOR2,   7,  5,  0 },
    { EC_GF_OP_XOR2,   6,  4,  0 },
    { EC_GF_OP_XOR2,   7,  0,  0 },
    { EC_GF_OP_XOR2,   2,  6,  0 },
    { EC_GF_OP_XOR2,   3,  7,  0 },
    { EC_GF_OP_XOR2,   4,  1,  0 },
    { EC_GF_OP_XOR2,   5,  2,  0 },
    { EC_GF_OP_XOR2,   4,  0,  0 },
    { EC_GF_OP_XOR2,   2,  3,  0 },
    { EC_GF_OP_XOR2,   0,  3,  0 },
    { EC_GF_OP_XOR2,   1,  5,  0 },
    { EC_GF_OP_XOR2,   3,  4,  0 },
    { EC_GF_OP_END,    0,  0,  0 }
};

static ec_gf_mul_t ec_gf8_mul_16 = {
    8,
    { 6, 7, 4, 1, 2, 3, 5, 0, },
    ec_gf8_mul_16_ops
};

static ec_gf_op_t ec_gf8_mul_17_ops[] = {
    { EC_GF_OP_XOR2,   3,  5,  0 },
    { EC_GF_OP_XOR2,   6,  3,  0 },
    { EC_GF_OP_XOR2,   3,  0,  0 },
    { EC_GF_OP_XOR2,   7,  5,  0 },
    { EC_GF_OP_XOR2,   3,  2,  0 },
    { EC_GF_OP_XOR2,   2,  7,  0 },
    { EC_GF_OP_XOR2,   4,  2,  0 },
    { EC_GF_OP_XOR2,   0,  1,  0 },
    { EC_GF_OP_XOR2,   5,  4,  0 },
    { EC_GF_OP_XOR2,   7,  0,  0 },
    { EC_GF_OP_XOR2,   0,  5,  0 },
    { EC_GF_OP_XOR2,   5,  6,  0 },
    { EC_GF_OP_XOR2,   1,  5,  0 },
    { EC_GF_OP_XOR2,   5,  3,  0 },
    { EC_GF_OP_XOR2,   2,  1,  0 },
    { EC_GF_OP_END,    0,  0,  0 }
};

static ec_gf_mul_t ec_gf8_mul_17 = {
    8,
    { 5, 7, 0, 1, 3, 2, 4, 6, },
    ec_gf8_mul_17_ops
};

static ec_gf_op_t ec_gf8_mul_18_ops[] = {
    { EC_GF_OP_XOR2,   7,  4,  0 },
    { EC_GF_OP_XOR2,   0,  7,  0 },
    { EC_GF_OP_COPY,   8,  0,  0 },
    { EC_GF_OP_XOR2,   0,  1,  0 },
    { EC_GF_OP_XOR2,   1,  2,  0 },
    { EC_GF_OP_XOR2,   4,  5,  0 },
    { EC_GF_OP_XOR2,   2,  3,  0 },
    { EC_GF_OP_XOR2,   1,  5,  0 },
    { EC_GF_OP_XOR2,   3,  7,  0 },
    { EC_GF_OP_XOR2,   5,  6,  0 },
    { EC_GF_OP_XOR2,   2,  6,  0 },
    { EC_GF_OP_XOR2,   7,  5,  0 },
    { EC_GF_OP_XOR2,   6,  8,  0 },
    { EC_GF_OP_END,    0,  0,  0 }
};

static ec_gf_mul_t ec_gf8_mul_18 = {
    9,
    { 4, 5, 7, 6, 0, 1, 2, 3, 8, },
    ec_gf8_mul_18_ops
};

static ec_gf_op_t ec_gf8_mul_19_ops[] = {
    { EC_GF_OP_XOR2,   7,  0,  0 },
    { EC_GF_OP_XOR2,   0,  4,  0 },
    { EC_GF_OP_XOR2,   6,  1,  0 },
    { EC_GF_OP_XOR2,   7,  1,  0 },
    { EC_GF_OP_XOR2,   4,  3,  0 },
    { EC_GF_OP_XOR2,   0,  5,  0 },
    { EC_GF_OP_XOR2,   5,  6,  0 },
    { EC_GF_OP_XOR2,   3,  2,  0 },
    { EC_GF_OP_XOR2,   1,  2,  0 },
    { EC_GF_OP_XOR2,   6,  7,  0 },
    { EC_GF_OP_XOR2,   2,  0,  0 },
    { EC_GF_OP_XOR2,   2,  6,  0 },
    { EC_GF_OP_XOR2,   6,  4,  0 },
    { EC_GF_OP_END,    0,  0,  0 }
};

static ec_gf_mul_t ec_gf8_mul_19 = {
    8,
    { 0, 5, 2, 6, 7, 1, 3, 4, },
    ec_gf8_mul_19_ops
};

static ec_gf_op_t ec_gf8_mul_1A_ops[] = {
    { EC_GF_OP_XOR2,   6,  5,  0 },
    { EC_GF_OP_XOR2,   5,  4,  0 },
    { EC_GF_OP_XOR2,   4,  0,  0 },
    { EC_GF_OP_XOR2,   7,  5,  0 },
    { EC_GF_OP_XOR2,   0,  6,  0 },
    { EC_GF_OP_XOR2,   4,  1,  0 },
    { EC_GF_OP_XOR2,   6,  3,  0 },
    { EC_GF_OP_XOR2,   3,  4,  0 },
    { EC_GF_OP_XOR2,   5,  2,  0 },
    { EC_GF_OP_XOR2,   4,  0,  0 },
    { EC_GF_OP_XOR2,   2,  6,  0 },
    { EC_GF_OP_XOR2,   1,  5,  0 },
    { EC_GF_OP_XOR2,   6,  7,  0 },
    { EC_GF_OP_XOR2,   5,  0,  0 },
    { EC_GF_OP_END,    0,  0,  0 }
};

static ec_gf_mul_t ec_gf8_mul_1A = {
    8,
    { 7, 0, 4, 5, 3, 1, 2, 6, },
    ec_gf8_mul_1A_ops
};

static ec_gf_op_t ec_gf8_mul_1B_ops[] = {
    { EC_GF_OP_XOR2,   1,  0,  0 },
    { EC_GF_OP_XOR2,   0,  2,  0 },
    { EC_GF_OP_XOR2,   2,  5,  0 },
    { EC_GF_OP_XOR2,   7,  2,  0 },
    { EC_GF_OP_XOR2,   2,  3,  0 },
    { EC_GF_OP_XOR2,   4,  0,  0 },
    { EC_GF_OP_XOR2,   3,  1,  0 },
    { EC_GF_OP_XOR2,   1,  4,  0 },
    { EC_GF_OP_XOR2,   7,  4,  0 },
    { EC_GF_OP_XOR2,   6,  1,  0 },
    { EC_GF_OP_XOR2,   5,  6,  0 },
    { EC_GF_OP_XOR2,   6,  3,  0 },
    { EC_GF_OP_XOR2,   4,  5,  0 },
    { EC_GF_OP_XOR2,   0,  6,  0 },
    { EC_GF_OP_END,    0,  0,  0 }
};

static ec_gf_mul_t ec_gf8_mul_1B = {
    8,
    { 7, 4, 5, 6, 3, 1, 2, 0, },
    ec_gf8_mul_1B_ops
};

static ec_gf_op_t ec_gf8_mul_1C_ops[] = {
    { EC_GF_OP_XOR2,   6,  4,  0 },
    { EC_GF_OP_XOR2,   5,  6,  0 },
    { EC_GF_OP_XOR2,   6,  3,  0 },
    { EC_GF_OP_XOR2,   3,  0,  0 },
    { EC_GF_OP_XOR2,   0,  1,  0 },
    { EC_GF_OP_XOR2,   2,  6,  0 },
    { EC_GF_OP_XOR2,   0,  4,  0 },
    { EC_GF_OP_XOR2,   7,  5,  0 },
    { EC_GF_OP_XOR2,   1,  2,  0 },
    { EC_GF_OP_XOR2,   4,  7,  0 },
    { EC_GF_OP_XOR2,   7,  1,  0 },
    { EC_GF_OP_XOR2,   6,  4,  0 },
    { EC_GF_OP_XOR2,   1,  3,  0 },
    { EC_GF_OP_XOR2,   3,  6,  0 },
    { EC_GF_OP_END,    0,  0,  0 }
};

static ec_gf_mul_t ec_gf8_mul_1C = {
    8,
    { 5, 4, 3, 0, 1, 7, 2, 6, },
    ec_gf8_mul_1C_ops
};

static ec_gf_op_t ec_gf8_mul_1D_ops[] = {
    { EC_GF_OP_XOR2,   7,  1,  0 },
    { EC_GF_OP_XOR2,   1,  0,  0 },
    { EC_GF_OP_XOR2,   3,  2,  0 },
    { EC_GF_OP_XOR2,   0,  4,  0 },
    { EC_GF_OP_XOR2,   2,  1,  0 },
    { EC_GF_OP_XOR2,   4,  3,  0 },
    { EC_GF_OP_XOR2,   3,  7,  0 },
    { EC_GF_OP_XOR3,   8,  4,  2 },
    { EC_GF_OP_XOR2,   2,  6,  0 },
    { EC_GF_OP_XOR2,   6,  5,  0 },
    { EC_GF_OP_XOR2,   5,  8,  0 },
    { EC_GF_OP_XOR2,   7,  6,  0 },
    { EC_GF_OP_XOR2,   0,  6,  0 },
    { EC_GF_OP_XOR2,   1,  5,  0 },
    { EC_GF_OP_XOR2,   5,  3,  0 },
    { EC_GF_OP_END,    0,  0,  0 }
};

static ec_gf_mul_t ec_gf8_mul_1D = {
    9,
    { 0, 7, 5, 8, 2, 3, 4, 1, 6, },
    ec_gf8_mul_1D_ops
};

static ec_gf_op_t ec_gf8_mul_1E_ops[] = {
    { EC_GF_OP_XOR2,   4,  0,  0 },
    { EC_GF_OP_XOR2,   0,  5,  0 },
    { EC_GF_OP_XOR2,   0,  6,  0 },
    { EC_GF_OP_XOR2,   1,  4,  0 },
    { EC_GF_OP_XOR2,   5,  1,  0 },
    { EC_GF_OP_XOR2,   2,  7,  0 },
    { EC_GF_OP_XOR2,   7,  0,  0 },
    { EC_GF_OP_XOR2,   1,  2,  0 },
    { EC_GF_OP_XOR2,   6,  3,  0 },
    { EC_GF_OP_XOR2,   4,  7,  0 },
    { EC_GF_OP_XOR2,   6,  1,  0 },
    { EC_GF_OP_XOR2,   3,  4,  0 },
    { EC_GF_OP_XOR2,   0,  6,  0 },
    { EC_GF_OP_XOR2,   2,  3,  0 },
    { EC_GF_OP_END,    0,  0,  0 }
};

static ec_gf_mul_t ec_gf8_mul_1E = {
    8,
    { 4, 7, 5, 1, 6, 0, 2, 3, },
    ec_gf8_mul_1E_ops
};

static ec_gf_op_t ec_gf8_mul_1F_ops[] = {
    { EC_GF_OP_XOR2,   5,  2,  0 },
    { EC_GF_OP_XOR2,   5,  4,  0 },
    { EC_GF_OP_XOR2,   3,  5,  0 },
    { EC_GF_OP_XOR2,   5,  0,  0 },
    { EC_GF_OP_XOR2,   5,  1,  0 },
    { EC_GF_OP_XOR2,   6,  3,  0 },
    { EC_GF_OP_XOR2,   7,  5,  0 },
    { EC_GF_OP_XOR2,   2,  6,  0 },
    { EC_GF_OP_XOR2,   7,  2,  0 },
    { EC_GF_OP_XOR3,   8,  3,  7 },
    { EC_GF_OP_XOR2,   4,  8,  0 },
    { EC_GF_OP_XOR2,   1,  8,  0 },
    { EC_GF_OP_XOR2,   6,  4,  0 },
    { EC_GF_OP_XOR2,   0,  6,  0 },
    { EC_GF_OP_END,    0,  0,  0 }
};

static ec_gf_mul_t ec_gf8_mul_1F = {
    9,
    { 1, 4, 5, 6, 7, 0, 3, 2, 8, },
    ec_gf8_mul_1F_ops
};

static ec_gf_op_t ec_gf8_mul_20_ops[] = {
    { EC_GF_OP_XOR2,   6,  7,  0 },
    { EC_GF_OP_XOR2,   1,  5,  0 },
    { EC_GF_OP_XOR2,   7,  3,  0 },
    { EC_GF_OP_XOR2,   1,  6,  0 },
    { EC_GF_OP_XOR2,   3,  4,  0 },
    { EC_GF_OP_XOR2,   2,  6,  0 },
    { EC_GF_OP_XOR2,   6,  3,  0 },
    { EC_GF_OP_XOR2,   3,  5,  0 },
    { EC_GF_OP_XOR2,   5,  7,  0 },
    { EC_GF_OP_XOR2,   0,  5,  0 },
    { EC_GF_OP_XOR2,   0,  6,  0 },
    { EC_GF_OP_END,    0,  0,  0 }
};

static ec_gf_mul_t ec_gf8_mul_20 = {
    8,
    { 7, 4, 5, 6, 3, 0, 1, 2, },
    ec_gf8_mul_20_ops
};

static ec_gf_op_t ec_gf8_mul_21_ops[] = {
    { EC_GF_OP_COPY,   9,  0,  0 },
    { EC_GF_OP_XOR2,   0,  3,  0 },
    { EC_GF_OP_XOR2,   5,  3,  0 },
    { EC_GF_OP_XOR2,   3,  1,  0 },
    { EC_GF_OP_XOR2,   1,  4,  0 },
    { EC_GF_OP_XOR2,   4,  6,  0 },
    { EC_GF_OP_XOR3,   8,  7,  5 },
    { EC_GF_OP_XOR2,   0,  7,  0 },
    { EC_GF_OP_XOR2,   6,  2,  0 },
    { EC_GF_OP_XOR2,   7,  4,  0 },
    { EC_GF_OP_XOR2,   3,  8,  0 },
    { EC_GF_OP_XOR2,   2,  8,  0 },
    { EC_GF_OP_XOR2,   4,  9,  0 },
    { EC_GF_OP_END,    0,  0,  0 }
};

static ec_gf_mul_t ec_gf8_mul_21 = {
    10,
    { 0, 1, 2, 7, 5, 4, 3, 6, 8, 9, },
    ec_gf8_mul_21_ops
};

static ec_gf_op_t ec_gf8_mul_22_ops[] = {
    { EC_GF_OP_XOR2,   0,  4,  0 },
    { EC_GF_OP_XOR2,   4,  6,  0 },
    { EC_GF_OP_XOR2,   6,  7,  0 },
    { EC_GF_OP_XOR2,   7,  2,  0 },
    { EC_GF_OP_XOR2,   2,  3,  0 },
    { EC_GF_OP_XOR2,   2,  4,  0 },
    { EC_GF_OP_XOR2,   4,  5,  0 },
    { EC_GF_OP_XOR2,   5,  3,  0 },
    { EC_GF_OP_XOR2,   5,  1,  0 },
    { EC_GF_OP_XOR2,   1,  6,  0 },
    { EC_GF_OP_XOR2,   6,  4,  0 },
    { EC_GF_OP_XOR2,   4,  0,  0 },
    { EC_GF_OP_END,    0,  0,  0 }
};

static ec_gf_mul_t ec_gf8_mul_22 = {
    8,
    { 3, 0, 5, 2, 6, 4, 1, 7, },
    ec_gf8_mul_22_ops
};

static ec_gf_op_t ec_gf8_mul_23_ops[] = {
    { EC_GF_OP_COPY,   8,  2,  0 },
    { EC_GF_OP_XOR2,   2,  4,  0 },
    { EC_GF_OP_XOR2,   2,  6,  0 },
    { EC_GF_OP_XOR2,   4,  0,  0 },
    { EC_GF_OP_XOR2,   6,  0,  0 },
    { EC_GF_OP_XOR2,   0,  3,  0 },
    { EC_GF_OP_XOR2,   3,  5,  0 },
    { EC_GF_OP_XOR2,   3,  8,  0 },
    { EC_GF_OP_XOR2,   4,  1,  0 },
    { EC_GF_OP_XOR2,   5,  7,  0 },
    { EC_GF_OP_XOR2,   3,  1,  0 },
    { EC_GF_OP_XOR2,   1,  7,  0 },
    { EC_GF_OP_END,    0,  0,  0 }
};

static ec_gf_mul_t ec_gf8_mul_23 = {
    9,
    { 0, 4, 3, 2, 5, 6, 1, 8, 7, },
    ec_gf8_mul_23_ops
};

static ec_gf_op_t ec_gf8_mul_24_ops[] = {
    { EC_GF_OP_XOR2,   6,  3,  0 },
    { EC_GF_OP_XOR2,   6,  7,  0 },
    { EC_GF_OP_XOR2,   5,  6,  0 },
    { EC_GF_OP_XOR2,   2,  0,  0 },
    { EC_GF_OP_XOR2,   0,  5,  0 },
    { EC_GF_OP_XOR2,   7,  4,  0 },
    { EC_GF_OP_XOR2,   3,  4,  0 },
    { EC_GF_OP_XOR2,   4,  0,  0 },
    { EC_GF_OP_XOR2,   1,  3,  0 },
    { EC_GF_OP_XOR2,   2,  4,  0 },
    { EC_GF_OP_XOR2,   5,  1,  0 },
    { EC_GF_OP_XOR2,   3,  2,  0 },
    { EC_GF_OP_END,    0,  0,  0 }
};

static ec_gf_mul_t ec_gf8_mul_24 = {
    8,
    { 6, 7, 0, 1, 2, 4, 5, 3, },
    ec_gf8_mul_24_ops
};

static ec_gf_op_t ec_gf8_mul_25_ops[] = {
    { EC_GF_OP_XOR2,   2,  5,  0 },
    { EC_GF_OP_XOR2,   6,  2,  0 },
    { EC_GF_OP_XOR2,   3,  7,  0 },
    { EC_GF_OP_XOR2,   3,  6,  0 },
    { EC_GF_OP_XOR2,   0,  3,  0 },
    { EC_GF_OP_XOR2,   1,  4,  0 },
    { EC_GF_OP_XOR2,   2,  0,  0 },
    { EC_GF_OP_XOR2,   7,  1,  0 },
    { EC_GF_OP_XOR2,   4,  2,  0 },
    { EC_GF_OP_XOR2,   5,  7,  0 },
    { EC_GF_OP_END,    0,  0,  0 }
};

static ec_gf_mul_t ec_gf8_mul_25 = {
    8,
    { 2, 7, 0, 1, 3, 4, 5, 6, },
    ec_gf8_mul_25_ops
};

static ec_gf_op_t ec_gf8_mul_26_ops[] = {
    { EC_GF_OP_XOR2,   1,  0,  0 },
    { EC_GF_OP_XOR2,   0,  7,  0 },
    { EC_GF_OP_XOR2,   4,  0,  0 },
    { EC_GF_OP_XOR2,   3,  6,  0 },
    { EC_GF_OP_XOR2,   6,  4,  0 },
    { EC_GF_OP_XOR2,   2,  5,  0 },
    { EC_GF_OP_XOR2,   7,  2,  0 },
    { EC_GF_OP_XOR2,   5,  3,  0 },
    { EC_GF_OP_XOR2,   2,  6,  0 },
    { EC_GF_OP_XOR2,   6,  1,  0 },
    { EC_GF_OP_XOR2,   1,  5,  0 },
    { EC_GF_OP_XOR2,   5,  0,  0 },
    { EC_GF_OP_XOR2,   0,  2,  0 },
    { EC_GF_OP_XOR2,   2,  1,  0 },
    { EC_GF_OP_END,    0,  0,  0 }
};

static ec_gf_mul_t ec_gf8_mul_26 = {
    8,
    { 3, 4, 1, 2, 0, 5, 6, 7, },
    ec_gf8_mul_26_ops
};

static ec_gf_op_t ec_gf8_mul_27_ops[] = {
    { EC_GF_OP_XOR2,   3,  0,  0 },
    { EC_GF_OP_XOR2,   4,  1,  0 },
    { EC_GF_OP_XOR2,   5,  2,  0 },
    { EC_GF_OP_XOR2,   4,  7,  0 },
    { EC_GF_OP_XOR2,   1,  5,  0 },
    { EC_GF_OP_XOR2,   3,  6,  0 },
    { EC_GF_OP_XOR2,   2,  4,  0 },
    { EC_GF_OP_XOR2,   0,  4,  0 },
    { EC_GF_OP_XOR2,   6,  5,  0 },
    { EC_GF_OP_XOR2,   1,  3,  0 },
    { EC_GF_OP_XOR2,   7,  3,  0 },
    { EC_GF_OP_END,    0,  0,  0 }
};

static ec_gf_mul_t ec_gf8_mul_27 = {
    8,
    { 3, 0, 1, 2, 6, 7, 4, 5, },
    ec_gf8_mul_27_ops
};

static ec_gf_op_t ec_gf8_mul_28_ops[] = {
    { EC_GF_OP_XOR2,   6,  4,  0 },
    { EC_GF_OP_XOR2,   5,  7,  0 },
    { EC_GF_OP_XOR2,   7,  6,  0 },
    { EC_GF_OP_XOR2,   4,  5,  0 },
    { EC_GF_OP_XOR2,   1,  3,  0 },
    { EC_GF_OP_XOR2,   5,  3,  0 },
    { EC_GF_OP_XOR2,   1,  7,  0 },
    { EC_GF_OP_XOR2,   0,  4,  0 },
    { EC_GF_OP_XOR2,   4,  1,  0 },
    { EC_GF_OP_XOR2,   7,  2,  0 },
    { EC_GF_OP_XOR2,   2,  0,  0 },
    { EC_GF_OP_XOR2,   0,  3,  0 },
    { EC_GF_OP_END,    0,  0,  0 }
};

static ec_gf_mul_t ec_gf8_mul_28 = {
    8,
    { 5, 6, 3, 0, 1, 2, 4, 7, },
    ec_gf8_mul_28_ops
};

static ec_gf_op_t ec_gf8_mul_29_ops[] = {
    { EC_GF_OP_XOR2,   6,  4,  0 },
    { EC_GF_OP_XOR2,   5,  3,  0 },
    { EC_GF_OP_XOR2,   4,  3,  0 },
    { EC_GF_OP_XOR2,   3,  2,  0 },
    { EC_GF_OP_XOR2,   7,  4,  0 },
    { EC_GF_OP_XOR2,   2,  6,  0 },
    { EC_GF_OP_XOR2,   6,  1,  0 },
    { EC_GF_OP_XOR2,   1,  5,  0 },
    { EC_GF_OP_XOR2,   0,  7,  0 },
    { EC_GF_OP_XOR2,   5,  0,  0 },
    { EC_GF_OP_XOR2,   7,  6,  0 },
    { EC_GF_OP_XOR2,   0,  3,  0 },
    { EC_GF_OP_XOR2,   4,  5,  0 },
    { EC_GF_OP_END,    0,  0,  0 }
};

static ec_gf_mul_t ec_gf8_mul_29 = {
    8,
    { 4, 6, 3, 5, 7, 0, 1, 2, },
    ec_gf8_mul_29_ops
};

static ec_gf_op_t ec_gf8_mul_2A_ops[] = {
    { EC_GF_OP_COPY,   8,  1,  0 },
    { EC_GF_OP_XOR2,   8,  0,  0 },
    { EC_GF_OP_XOR2,   4,  0,  0 },
    { EC_GF_OP_XOR2,   0,  2,  0 },
    { EC_GF_OP_XOR2,   1,  3,  0 },
    { EC_GF_OP_XOR2,   3,  5,  0 },
    { EC_GF_OP_XOR2,   0,  7,  0 },
    { EC_GF_OP_XOR2,   2,  3,  0 },
    { EC_GF_OP_XOR2,   7,  1,  0 },
    { EC_GF_OP_XOR2,   5,  0,  0 },
    { EC_GF_OP_XOR2,   2,  4,  0 },
    { EC_GF_OP_XOR2,   0,  4,  0 },
    { EC_GF_OP_XOR2,   1,  6,  0 },
    { EC_GF_OP_XOR2,   4,  6,  0 },
    { EC_GF_OP_XOR3,   6,  8,  4 },
    { EC_GF_OP_END,    0,  0,  0 }
};

static ec_gf_mul_t ec_gf8_mul_2A = {
    9,
    { 3, 4, 7, 2, 6, 5, 1, 0, 8, },
    ec_gf8_mul_2A_ops
};

static ec_gf_op_t ec_gf8_mul_2B_ops[] = {
    { EC_GF_OP_XOR2,   7,  2,  0 },
    { EC_GF_OP_XOR2,   2,  4,  0 },
    { EC_GF_OP_XOR2,   6,  1,  0 },
    { EC_GF_OP_XOR2,   4,  0,  0 },
    { EC_GF_OP_XOR2,   5,  0,  0 },
    { EC_GF_OP_XOR2,   4,  6,  0 },
    { EC_GF_OP_XOR2,   1,  3,  0 },
    { EC_GF_OP_XOR2,   0,  7,  0 },
    { EC_GF_OP_XOR2,   3,  5,  0 },
    { EC_GF_OP_XOR2,   7,  1,  0 },
    { EC_GF_OP_XOR2,   5,  2,  0 },
    { EC_GF_OP_END,    0,  0,  0 }
};

static ec_gf_mul_t ec_gf8_mul_2B = {
    8,
    { 3, 4, 7, 5, 6, 0, 1, 2, },
    ec_gf8_mul_2B_ops
};

static ec_gf_op_t ec_gf8_mul_2C_ops[] = {
    { EC_GF_OP_XOR2,   3,  4,  0 },
    { EC_GF_OP_XOR2,   4,  7,  0 },
    { EC_GF_OP_XOR2,   2,  1,  0 },
    { EC_GF_OP_XOR2,   6,  4,  0 },
    { EC_GF_OP_XOR2,   2,  3,  0 },
    { EC_GF_OP_XOR2,   3,  6,  0 },
    { EC_GF_OP_XOR2,   5,  3,  0 },
    { EC_GF_OP_XOR2,   1,  5,  0 },
    { EC_GF_OP_XOR2,   3,  0,  0 },
    { EC_GF_OP_XOR2,   4,  1,  0 },
    { EC_GF_OP_XOR2,   7,  3,  0 },
    { EC_GF_OP_XOR2,   1,  2,  0 },
    { EC_GF_OP_XOR2,   0,  4,  0 },
    { EC_GF_OP_XOR2,   3,  1,  0 },
    { EC_GF_OP_END,    0,  0,  0 }
};

static ec_gf_mul_t ec_gf8_mul_2C = {
    8,
    { 5, 6, 7, 0, 2, 3, 4, 1, },
    ec_gf8_mul_2C_ops
};

static ec_gf_op_t ec_gf8_mul_2D_ops[] = {
    { EC_GF_OP_XOR2,   3,  2,  0 },
    { EC_GF_OP_XOR2,   1,  3,  0 },
    { EC_GF_OP_XOR2,   3,  0,  0 },
    { EC_GF_OP_XOR2,   4,  3,  0 },
    { EC_GF_OP_XOR2,   3,  6,  0 },
    { EC_GF_OP_XOR2,   6,  1,  0 },
    { EC_GF_OP_XOR2,   2,  3,  0 },
    { EC_GF_OP_XOR3,   8,  4,  6 },
    { EC_GF_OP_XOR2,   5,  8,  0 },
    { EC_GF_OP_XOR2,   7,  8,  0 },
    { EC_GF_OP_XOR2,   2,  5,  0 },
    { EC_GF_OP_XOR2,   0,  7,  0 },
    { EC_GF_OP_XOR2,   6,  2,  0 },
    { EC_GF_OP_XOR2,   7,  2,  0 },
    { EC_GF_OP_END,    0,  0,  0 }
};

static ec_gf_mul_t ec_gf8_mul_2D = {
    9,
    { 7, 0, 3, 5, 1, 4, 2, 6, 8, },
    ec_gf8_mul_2D_ops
};

static ec_gf_op_t ec_gf8_mul_2E_ops[] = {
    { EC_GF_OP_XOR2,   4,  1,  0 },
    { EC_GF_OP_XOR2,   0,  1,  0 },
    { EC_GF_OP_COPY,   8,  4,  0 },
    { EC_GF_OP_XOR2,   3,  6,  0 },
    { EC_GF_OP_XOR2,   1,  5,  0 },
    { EC_GF_OP_XOR2,   4,  3,  0 },
    { EC_GF_OP_XOR2,   8,  7,  0 },
    { EC_GF_OP_XOR2,   5,  3,  0 },
    { EC_GF_OP_XOR2,   2,  8,  0 },
    { EC_GF_OP_XOR2,   3,  0,  0 },
    { EC_GF_OP_XOR2,   6,  8,  0 },
    { EC_GF_OP_XOR2,   1,  2,  0 },
    { EC_GF_OP_XOR2,   7,  3,  0 },
    { EC_GF_OP_XOR2,   0,  6,  0 },
    { EC_GF_OP_XOR2,   3,  1,  0 },
    { EC_GF_OP_XOR2,   6,  3,  0 },
    { EC_GF_OP_END,    0,  0,  0 }
};

static ec_gf_mul_t ec_gf8_mul_2E = {
    9,
    { 5, 0, 7, 3, 2, 6, 4, 1, 8, },
    ec_gf8_mul_2E_ops
};

static ec_gf_op_t ec_gf8_mul_2F_ops[] = {
    { EC_GF_OP_XOR2,   0,  2,  0 },
    { EC_GF_OP_XOR2,   0,  3,  0 },
    { EC_GF_OP_XOR2,   7,  1,  0 },
    { EC_GF_OP_XOR2,   6,  0,  0 },
    { EC_GF_OP_XOR2,   7,  2,  0 },
    { EC_GF_OP_XOR2,   3,  4,  0 },
    { EC_GF_OP_XOR3,   8,  7,  6 },
    { EC_GF_OP_XOR2,   1,  3,  0 },
    { EC_GF_OP_XOR2,   5,  2,  0 },
    { EC_GF_OP_XOR2,   3,  8,  0 },
    { EC_GF_OP_XOR2,   2,  8,  0 },
    { EC_GF_OP_XOR2,   4,  5,  0 },
    { EC_GF_OP_XOR2,   6,  5,  0 },
    { EC_GF_OP_XOR2,   5,  3,  0 },
    { EC_GF_OP_END,    0,  0,  0 }
};

static ec_gf_mul_t ec_gf8_mul_2F = {
    9,
    { 6, 3, 2, 5, 7, 0, 1, 4, 8, },
    ec_gf8_mul_2F_ops
};

static ec_gf_op_t ec_gf8_mul_30_ops[] = {
    { EC_GF_OP_COPY,   8,  0,  0 },
    { EC_GF_OP_XOR2,   8,  1,  0 },
    { EC_GF_OP_XOR2,   1,  2,  0 },
    { EC_GF_OP_XOR2,   7,  4,  0 },
    { EC_GF_OP_XOR2,   6,  3,  0 },
    { EC_GF_OP_XOR2,   3,  7,  0 },
    { EC_GF_OP_XOR2,   0,  6,  0 },
    { EC_GF_OP_XOR2,   1,  5,  0 },
    { EC_GF_OP_XOR2,   4,  5,  0 },
    { EC_GF_OP_XOR2,   2,  6,  0 },
    { EC_GF_OP_XOR2,   5,  6,  0 },
    { EC_GF_OP_XOR3,   6,  8,  7 },
    { EC_GF_OP_XOR2,   7,  5,  0 },
    { EC_GF_OP_END,    0,  0,  0 }
};

static ec_gf_mul_t ec_gf8_mul_30 = {
    9,
    { 3, 4, 7, 5, 0, 6, 1, 2, 8, },
    ec_gf8_mul_30_ops
};

static ec_gf_op_t ec_gf8_mul_31_ops[] = {
    { EC_GF_OP_XOR2,   3,  0,  0 },
    { EC_GF_OP_XOR2,   3,  4,  0 },
    { EC_GF_OP_XOR2,   4,  5,  0 },
    { EC_GF_OP_XOR2,   2,  1,  0 },
    { EC_GF_OP_XOR2,   1,  4,  0 },
    { EC_GF_OP_XOR2,   5,  6,  0 },
    { EC_GF_OP_XOR2,   6,  3,  0 },
    { EC_GF_OP_XOR2,   0,  7,  0 },
    { EC_GF_OP_XOR2,   2,  5,  0 },
    { EC_GF_OP_XOR2,   7,  3,  0 },
    { EC_GF_OP_XOR2,   0,  1,  0 },
    { EC_GF_OP_XOR2,   3,  2,  0 },
    { EC_GF_OP_XOR2,   3,  0,  0 },
    { EC_GF_OP_XOR2,   4,  3,  0 },
    { EC_GF_OP_END,    0,  0,  0 }
};

static ec_gf_mul_t ec_gf8_mul_31 = {
    8,
    { 7, 1, 4, 5, 6, 0, 2, 3, },
    ec_gf8_mul_31_ops
};

static ec_gf_op_t ec_gf8_mul_32_ops[] = {
    { EC_GF_OP_XOR2,   6,  5,  0 },
    { EC_GF_OP_XOR2,   5,  0,  0 },
    { EC_GF_OP_XOR2,   0,  1,  0 },
    { EC_GF_OP_XOR2,   0,  7,  0 },
    { EC_GF_OP_XOR2,   7,  6,  0 },
    { EC_GF_OP_XOR2,   6,  1,  0 },
    { EC_GF_OP_XOR2,   1,  2,  0 },
    { EC_GF_OP_XOR2,   2,  3,  0 },
    { EC_GF_OP_XOR2,   3,  4,  0 },
    { EC_GF_OP_XOR2,   4,  5,  0 },
    { EC_GF_OP_XOR2,   6,  3,  0 },
    { EC_GF_OP_XOR2,   5,  7,  0 },
    { EC_GF_OP_XOR2,   7,  2,  0 },
    { EC_GF_OP_END,    0,  0,  0 }
};

static ec_gf_mul_t ec_gf8_mul_32 = {
    8,
    { 3, 4, 6, 7, 5, 0, 1, 2, },
    ec_gf8_mul_32_ops
};

static ec_gf_op_t ec_gf8_mul_33_ops[] = {
    { EC_GF_OP_XOR2,   3,  2,  0 },
    { EC_GF_OP_XOR2,   2,  1,  0 },
    { EC_GF_OP_XOR2,   1,  0,  0 },
    { EC_GF_OP_XOR2,   1,  5,  0 },
    { EC_GF_OP_XOR2,   4,  1,  0 },
    { EC_GF_OP_XOR2,   6,  2,  0 },
    { EC_GF_OP_XOR2,   1,  7,  0 },
    { EC_GF_OP_XOR2,   2,  4,  0 },
    { EC_GF_OP_XOR2,   7,  3,  0 },
    { EC_GF_OP_XOR2,   0,  6,  0 },
    { EC_GF_OP_XOR2,   3,  2,  0 },
    { EC_GF_OP_XOR2,   5,  3,  0 },
    { EC_GF_OP_XOR2,   3,  0,  0 },
    { EC_GF_OP_XOR2,   0,  1,  0 },
    { EC_GF_OP_XOR2,   2,  0,  0 },
    { EC_GF_OP_END,    0,  0,  0 }
};

static ec_gf_mul_t ec_gf8_mul_33 = {
    8,
    { 5, 4, 3, 0, 2, 1, 6, 7, },
    ec_gf8_mul_33_ops
};

static ec_gf_op_t ec_gf8_mul_34_ops[] = {
    { EC_GF_OP_XOR2,   5,  4,  0 },
    { EC_GF_OP_XOR2,   4,  3,  0 },
    { EC_GF_OP_XOR2,   2,  5,  0 },
    { EC_GF_OP_XOR2,   6,  4,  0 },
    { EC_GF_OP_XOR2,   5,  7,  0 },
    { EC_GF_OP_XOR2,   4,  0,  0 },
    { EC_GF_OP_XOR2,   7,  6,  0 },
    { EC_GF_OP_XOR2,   0,  5,  0 },
    { EC_GF_OP_XOR2,   6,  2,  0 },
    { EC_GF_OP_XOR2,   4,  1,  0 },
    { EC_GF_OP_XOR2,   3,  0,  0 },
    { EC_GF_OP_XOR2,   1,  2,  0 },
    { EC_GF_OP_XOR2,   0,  4,  0 },
    { EC_GF_OP_XOR2,   2,  3,  0 },
    { EC_GF_OP_END,    0,  0,  0 }
};

static ec_gf_mul_t ec_gf8_mul_34 = {
    8,
    { 7, 5, 3, 0, 2, 4, 1, 6, },
    ec_gf8_mul_34_ops
};

static ec_gf_op_t ec_gf8_mul_35_ops[] = {
    { EC_GF_OP_XOR2,   0,  1,  0 },
    { EC_GF_OP_XOR2,   1,  4,  0 },
    { EC_GF_OP_XOR2,   7,  1,  0 },
    { EC_GF_OP_XOR2,   3,  7,  0 },
    { EC_GF_OP_XOR2,   0,  3,  0 },
    { EC_GF_OP_XOR2,   7,  5,  0 },
    { EC_GF_OP_XOR2,   2,  0,  0 },
    { EC_GF_OP_XOR2,   4,  7,  0 },
    { EC_GF_OP_XOR2,   5,  2,  0 },
    { EC_GF_OP_XOR2,   6,  0,  0 },
    { EC_GF_OP_XOR2,   1,  5,  0 },
    { EC_GF_OP_XOR2,   0,  4,  0 },
    { EC_GF_OP_XOR2,   1,  6,  0 },
    { EC_GF_OP_XOR2,   3,  1,  0 },
    { EC_GF_OP_END,    0,  0,  0 }
};

static ec_gf_mul_t ec_gf8_mul_35 = {
    8,
    { 6, 7, 5, 4, 2, 0, 1, 3, },
    ec_gf8_mul_35_ops
};

static ec_gf_op_t ec_gf8_mul_36_ops[] = {
    { EC_GF_OP_XOR2,   5,  2,  0 },
    { EC_GF_OP_XOR2,   2,  0,  0 },
    { EC_GF_OP_XOR2,   6,  4,  0 },
    { EC_GF_OP_XOR2,   4,  2,  0 },
    { EC_GF_OP_XOR2,   7,  4,  0 },
    { EC_GF_OP_XOR2,   0,  1,  0 },
    { EC_GF_OP_XOR2,   6,  3,  0 },
    { EC_GF_OP_XOR2,   7,  5,  0 },
    { EC_GF_OP_XOR2,   5,  3,  0 },
    { EC_GF_OP_XOR2,   3,  0,  0 },
    { EC_GF_OP_XOR2,   1,  5,  0 },
    { EC_GF_OP_XOR2,   0,  4,  0 },
    { EC_GF_OP_XOR2,   4,  1,  0 },
    { EC_GF_OP_END,    0,  0,  0 }
};

static ec_gf_mul_t ec_gf8_mul_36 = {
    8,
    { 6, 7, 4, 1, 2, 3, 0, 5, },
    ec_gf8_mul_36_ops
};

static ec_gf_op_t ec_gf8_mul_37_ops[] = {
    { EC_GF_OP_XOR2,   1,  2,  0 },
    { EC_GF_OP_XOR2,   0,  2,  0 },
    { EC_GF_OP_XOR2,   6,  1,  0 },
    { EC_GF_OP_XOR2,   0,  4,  0 },
    { EC_GF_OP_XOR2,   1,  5,  0 },
    { EC_GF_OP_XOR3,   8,  0,  1 },
    { EC_GF_OP_XOR2,   3,  8,  0 },
    { EC_GF_OP_XOR2,   7,  8,  0 },
    { EC_GF_OP_XOR2,   2,  3,  0 },
    { EC_GF_OP_XOR2,   3,  4,  0 },
    { EC_GF_OP_XOR2,   5,  2,  0 },
    { EC_GF_OP_XOR2,   4,  6,  0 },
    { EC_GF_OP_XOR2,   6,  5,  0 },
    { EC_GF_OP_XOR2,   5,  7,  0 },
    { EC_GF_OP_END,    0,  0,  0 }
};

static ec_gf_mul_t ec_gf8_mul_37 = {
    9,
    { 6, 7, 2, 1, 0, 3, 4, 5, 8, },
    ec_gf8_mul_37_ops
};

static ec_gf_op_t ec_gf8_mul_38_ops[] = {
    { EC_GF_OP_XOR2,   6,  4,  0 },
    { EC_GF_OP_XOR2,   5,  6,  0 },
    { EC_GF_OP_XOR2,   6,  3,  0 },
    { EC_GF_OP_XOR2,   3,  0,  0 },
    { EC_GF_OP_XOR2,   2,  6,  0 },
    { EC_GF_OP_XOR2,   7,  5,  0 },
    { EC_GF_OP_XOR2,   0,  1,  0 },
    { EC_GF_OP_XOR3,   8,  6,  7 },
    { EC_GF_OP_XOR2,   1,  2,  0 },
    { EC_GF_OP_XOR2,   7,  1,  0 },
    { EC_GF_OP_XOR2,   0,  8,  0 },
    { EC_GF_OP_XOR2,   4,  8,  0 },
    { EC_GF_OP_XOR2,   1,  3,  0 },
    { EC_GF_OP_END,    0,  0,  0 }
};

static ec_gf_mul_t ec_gf8_mul_38 = {
    9,
    { 4, 5, 6, 3, 0, 1, 7, 2, 8, },
    ec_gf8_mul_38_ops
};

static ec_gf_op_t ec_gf8_mul_39_ops[] = {
    { EC_GF_OP_XOR2,   5,  1,  0 },
    { EC_GF_OP_XOR2,   4,  5,  0 },
    { EC_GF_OP_XOR2,   6,  4,  0 },
    { EC_GF_OP_XOR2,   2,  0,  0 },
    { EC_GF_OP_XOR2,   2,  6,  0 },
    { EC_GF_OP_XOR2,   3,  0,  0 },
    { EC_GF_OP_XOR2,   5,  2,  0 },
    { EC_GF_OP_XOR2,   3,  5,  0 },
    { EC_GF_OP_XOR2,   7,  3,  0 },
    { EC_GF_OP_XOR2,   4,  7,  0 },
    { EC_GF_OP_XOR2,   5,  4,  0 },
    { EC_GF_OP_XOR2,   1,  5,  0 },
    { EC_GF_OP_END,    0,  0,  0 }
};

static ec_gf_mul_t ec_gf8_mul_39 = {
    8,
    { 1, 6, 3, 0, 5, 2, 4, 7, },
    ec_gf8_mul_39_ops
};

static ec_gf_op_t ec_gf8_mul_3A_ops[] = {
    { EC_GF_OP_XOR2,   6,  1,  0 },
    { EC_GF_OP_XOR2,   7,  3,  0 },
    { EC_GF_OP_XOR2,   1,  0,  0 },
    { EC_GF_OP_XOR2,   3,  4,  0 },
    { EC_GF_OP_XOR2,   0,  2,  0 },
    { EC_GF_OP_XOR2,   4,  6,  0 },
    { EC_GF_OP_XOR2,   2,  3,  0 },
    { EC_GF_OP_XOR2,   6,  0,  0 },
    { EC_GF_OP_XOR2,   3,  5,  0 },
    { EC_GF_OP_XOR2,   0,  7,  0 },
    { EC_GF_OP_XOR2,   5,  1,  0 },
    { EC_GF_OP_XOR2,   7,  4,  0 },
    { EC_GF_OP_XOR2,   1,  0,  0 },
    { EC_GF_OP_XOR2,   4,  5,  0 },
    { EC_GF_OP_END,    0,  0,  0 }
};

static ec_gf_mul_t ec_gf8_mul_3A = {
    8,
    { 3, 4, 7, 0, 5, 6, 1, 2, },
    ec_gf8_mul_3A_ops
};

static ec_gf_op_t ec_gf8_mul_3B_ops[] = {
    { EC_GF_OP_XOR2,   2,  0,  0 },
    { EC_GF_OP_XOR2,   0,  4,  0 },
    { EC_GF_OP_XOR2,   3,  0,  0 },
    { EC_GF_OP_XOR2,   7,  2,  0 },
    { EC_GF_OP_XOR3,   8,  7,  3 },
    { EC_GF_OP_XOR2,   1,  6,  0 },
    { EC_GF_OP_XOR2,   3,  5,  0 },
    { EC_GF_OP_XOR2,   5,  1,  0 },
    { EC_GF_OP_XOR2,   1,  8,  0 },
    { EC_GF_OP_XOR2,   0,  5,  0 },
    { EC_GF_OP_XOR2,   2,  5,  0 },
    { EC_GF_OP_XOR2,   4,  1,  0 },
    { EC_GF_OP_XOR2,   6,  0,  0 },
    { EC_GF_OP_END,    0,  0,  0 }
};

static ec_gf_mul_t ec_gf8_mul_3B = {
    9,
    { 3, 0, 1, 7, 6, 2, 4, 8, 5, },
    ec_gf8_mul_3B_ops
};

static ec_gf_op_t ec_gf8_mul_3C_ops[] = {
    { EC_GF_OP_XOR2,   7,  5,  0 },
    { EC_GF_OP_XOR2,   7,  4,  0 },
    { EC_GF_OP_XOR2,   1,  0,  0 },
    { EC_GF_OP_XOR2,   6,  7,  0 },
    { EC_GF_OP_XOR2,   0,  3,  0 },
    { EC_GF_OP_XOR2,   3,  6,  0 },
    { EC_GF_OP_XOR2,   5,  3,  0 },
    { EC_GF_OP_XOR2,   1,  5,  0 },
    { EC_GF_OP_XOR2,   2,  1,  0 },
    { EC_GF_OP_XOR2,   1,  4,  0 },
    { EC_GF_OP_XOR2,   7,  2,  0 },
    { EC_GF_OP_XOR2,   4,  0,  0 },
    { EC_GF_OP_XOR2,   5,  7,  0 },
    { EC_GF_OP_XOR2,   0,  5,  0 },
    { EC_GF_OP_XOR2,   5,  1,  0 },
    { EC_GF_OP_END,    0,  0,  0 }
};

static ec_gf_mul_t ec_gf8_mul_3C = {
    8,
    { 3, 6, 4, 1, 7, 2, 0, 5, },
    ec_gf8_mul_3C_ops
};

static ec_gf_op_t ec_gf8_mul_3D_ops[] = {
    { EC_GF_OP_XOR2,   2,  0,  0 },
    { EC_GF_OP_XOR2,   3,  2,  0 },
    { EC_GF_OP_XOR2,   5,  1,  0 },
    { EC_GF_OP_XOR2,   4,  3,  0 },
    { EC_GF_OP_XOR2,   5,  4,  0 },
    { EC_GF_OP_XOR2,   6,  5,  0 },
    { EC_GF_OP_XOR2,   7,  6,  0 },
    { EC_GF_OP_XOR2,   0,  7,  0 },
    { EC_GF_OP_XOR2,   3,  7,  0 },
    { EC_GF_OP_XOR2,   1,  0,  0 },
    { EC_GF_OP_XOR2,   2,  1,  0 },
    { EC_GF_OP_XOR2,   5,  1,  0 },
    { EC_GF_OP_END,    0,  0,  0 }
};

static ec_gf_mul_t ec_gf8_mul_3D = {
    8,
    { 2, 3, 4, 5, 6, 7, 0, 1, },
    ec_gf8_mul_3D_ops
};

static ec_gf_op_t ec_gf8_mul_3E_ops[] = {
    { EC_GF_OP_XOR2,   3,  5,  0 },
    { EC_GF_OP_XOR2,   4,  3,  0 },
    { EC_GF_OP_XOR2,   6,  4,  0 },
    { EC_GF_OP_XOR2,   4,  2,  0 },
    { EC_GF_OP_XOR2,   1,  7,  0 },
    { EC_GF_OP_XOR2,   1,  4,  0 },
    { EC_GF_OP_XOR2,   5,  1,  0 },
    { EC_GF_OP_XOR2,   0,  5,  0 },
    { EC_GF_OP_XOR2,   2,  0,  0 },
    { EC_GF_OP_XOR2,   0,  6,  0 },
    { EC_GF_OP_XOR2,   3,  0,  0 },
    { EC_GF_OP_XOR2,   7,  3,  0 },
    { EC_GF_OP_XOR2,   1,  7,  0 },
    { EC_GF_OP_END,    0,  0,  0 }
};

static ec_gf_mul_t ec_gf8_mul_3E = {
    8,
    { 6, 1, 2, 7, 0, 3, 5, 4, },
    ec_gf8_mul_3E_ops
};

static ec_gf_op_t ec_gf8_mul_3F_ops[] = {
    { EC_GF_OP_COPY,   8,  0,  0 },
    { EC_GF_OP_XOR2,   0,  1,  0 },
    { EC_GF_OP_COPY,  10,  4,  0 },
    { EC_GF_OP_XOR2,   0,  6,  0 },
    { EC_GF_OP_XOR2,   4,  5,  0 },
    { EC_GF_OP_XOR2,   4,  0,  0 },
    { EC_GF_OP_XOR2,   1,  3,  0 },
    { EC_GF_OP_COPY,   9,  2,  0 },
    { EC_GF_OP_XOR2,   1,  4,  0 },
    { EC_GF_OP_XOR2,   2,  0,  0 },
    { EC_GF_OP_XOR2,   7,  4,  0 },
    { EC_GF_OP_XOR3,   4,  9,  7 },
    { EC_GF_OP_XOR2,   3,  4,  0 },
    { EC_GF_OP_XOR2,   5,  3,  0 },
    { EC_GF_OP_XOR2,   0,  3,  0 },
    { EC_GF_OP_XOR2,   6,  5,  0 },
    { EC_GF_OP_XOR2,   3, 10,  0 },
    { EC_GF_OP_XOR2,   5,  8,  0 },
    { EC_GF_OP_END,    0,  0,  0 }
};

static ec_gf_mul_t ec_gf8_mul_3F = {
    11,
    { 1, 7, 6, 2, 4, 3, 5, 0, 8, 9, 10, },
    ec_gf8_mul_3F_ops
};

static ec_gf_op_t ec_gf8_mul_40_ops[] = {
    { EC_GF_OP_XOR2,   1,  2,  0 },
    { EC_GF_OP_XOR2,   2,  3,  0 },
    { EC_GF_OP_XOR2,   2,  4,  0 },
    { EC_GF_OP_XOR2,   4,  5,  0 },
    { EC_GF_OP_XOR2,   6,  4,  0 },
    { EC_GF_OP_XOR2,   0,  6,  0 },
    { EC_GF_OP_XOR2,   7,  3,  0 },
    { EC_GF_OP_XOR2,   6,  2,  0 },
    { EC_GF_OP_XOR2,   3,  4,  0 },
    { EC_GF_OP_XOR3,   8,  7,  6 },
    { EC_GF_OP_XOR2,   1,  8,  0 },
    { EC_GF_OP_XOR2,   5,  8,  0 },
    { EC_GF_OP_XOR2,   4,  8,  0 },
    { EC_GF_OP_END,    0,  0,  0 }
};

static ec_gf_mul_t ec_gf8_mul_40 = {
    9,
    { 5, 7, 4, 6, 2, 3, 0, 1, 8, },
    ec_gf8_mul_40_ops
};

static ec_gf_op_t ec_gf8_mul_41_ops[] = {
    { EC_GF_OP_COPY,   8,  0,  0 },
    { EC_GF_OP_XOR2,   8,  4,  0 },
    { EC_GF_OP_XOR2,   8,  5,  0 },
    { EC_GF_OP_XOR2,   5,  6,  0 },
    { EC_GF_OP_XOR2,   0,  2,  0 },
    { EC_GF_OP_XOR2,   6,  7,  0 },
    { EC_GF_OP_XOR2,   0,  6,  0 },
    { EC_GF_OP_XOR2,   7,  1,  0 },
    { EC_GF_OP_XOR2,   6,  4,  0 },
    { EC_GF_OP_XOR2,   1,  5,  0 },
    { EC_GF_OP_XOR2,   7,  3,  0 },
    { EC_GF_OP_XOR2,   4,  3,  0 },
    { EC_GF_OP_XOR2,   5,  2,  0 },
    { EC_GF_OP_XOR2,   3,  2,  0 },
    { EC_GF_OP_END,    0,  0,  0 }
};

static ec_gf_mul_t ec_gf8_mul_41 = {
    9,
    { 0, 7, 6, 5, 3, 4, 8, 1, 2, },
    ec_gf8_mul_41_ops
};

static ec_gf_op_t ec_gf8_mul_42_ops[] = {
    { EC_GF_OP_COPY,   8,  0,  0 },
    { EC_GF_OP_XOR2,   0,  2,  0 },
    { EC_GF_OP_XOR2,   8,  3,  0 },
    { EC_GF_OP_XOR2,   2,  6,  0 },
    { EC_GF_OP_XOR2,   3,  5,  0 },
    { EC_GF_OP_XOR2,   4,  2,  0 },
    { EC_GF_OP_XOR2,   5,  1,  0 },
    { EC_GF_OP_XOR2,   0,  4,  0 },
    { EC_GF_OP_XOR2,   6,  7,  0 },
    { EC_GF_OP_XOR2,   1,  4,  0 },
    { EC_GF_OP_XOR2,   5,  7,  0 },
    { EC_GF_OP_XOR2,   4,  6,  0 },
    { EC_GF_OP_XOR2,   7,  8,  0 },
    { EC_GF_OP_XOR2,   6,  3,  0 },
    { EC_GF_OP_END,    0,  0,  0 }
};

static ec_gf_mul_t ec_gf8_mul_42 = {
    9,
    { 2, 7, 1, 6, 4, 3, 0, 5, 8, },
    ec_gf8_mul_42_ops
};

static ec_gf_op_t ec_gf8_mul_43_ops[] = {
    { EC_GF_OP_XOR2,   5,  1,  0 },
    { EC_GF_OP_XOR2,   1,  6,  0 },
    { EC_GF_OP_XOR2,   6,  0,  0 },
    { EC_GF_OP_XOR2,   0,  4,  0 },
    { EC_GF_OP_XOR2,   4,  1,  0 },
    { EC_GF_OP_XOR2,   1,  7,  0 },
    { EC_GF_OP_XOR2,   7,  2,  0 },
    { EC_GF_OP_XOR2,   2,  6,  0 },
    { EC_GF_OP_XOR2,   6,  3,  0 },
    { EC_GF_OP_XOR2,   6,  1,  0 },
    { EC_GF_OP_XOR2,   1,  5,  0 },
    { EC_GF_OP_END,    0,  0,  0 }
};

static ec_gf_mul_t ec_gf8_mul_43 = {
    8,
    { 2, 6, 4, 1, 7, 3, 0, 5, },
    ec_gf8_mul_43_ops
};

static ec_gf_op_t ec_gf8_mul_44_ops[] = {
    { EC_GF_OP_XOR2,   1,  6,  0 },
    { EC_GF_OP_XOR2,   4,  7,  0 },
    { EC_GF_OP_XOR2,   6,  5,  0 },
    { EC_GF_OP_XOR2,   5,  4,  0 },
    { EC_GF_OP_XOR2,   4,  0,  0 },
    { EC_GF_OP_XOR2,   4,  2,  0 },
    { EC_GF_OP_XOR2,   0,  6,  0 },
    { EC_GF_OP_XOR2,   2,  7,  0 },
    { EC_GF_OP_XOR2,   6,  3,  0 },
    { EC_GF_OP_XOR2,   7,  1,  0 },
    { EC_GF_OP_XOR2,   1,  2,  0 },
    { EC_GF_OP_XOR2,   1,  6,  0 },
    { EC_GF_OP_XOR2,   6,  5,  0 },
    { EC_GF_OP_END,    0,  0,  0 }
};

static ec_gf_mul_t ec_gf8_mul_44 = {
    8,
    { 2, 3, 4, 1, 6, 5, 0, 7, },
    ec_gf8_mul_44_ops
};

static ec_gf_op_t ec_gf8_mul_45_ops[] = {
    { EC_GF_OP_XOR2,   2,  0,  0 },
    { EC_GF_OP_XOR2,   4,  7,  0 },
    { EC_GF_OP_XOR2,   2,  7,  0 },
    { EC_GF_OP_XOR2,   7,  3,  0 },
    { EC_GF_OP_XOR2,   7,  6,  0 },
    { EC_GF_OP_XOR2,   5,  0,  0 },
    { EC_GF_OP_XOR2,   6,  1,  0 },
    { EC_GF_OP_XOR2,   3,  1,  0 },
    { EC_GF_OP_XOR2,   0,  4,  0 },
    { EC_GF_OP_XOR2,   1,  5,  0 },
    { EC_GF_OP_XOR2,   1,  2,  0 },
    { EC_GF_OP_END,    0,  0,  0 }
};

static ec_gf_mul_t ec_gf8_mul_45 = {
    8,
    { 2, 3, 0, 1, 7, 4, 5, 6, },
    ec_gf8_mul_45_ops
};

static ec_gf_op_t ec_gf8_mul_46_ops[] = {
    { EC_GF_OP_XOR3,   8,  2,  4 },
    { EC_GF_OP_XOR2,   4,  6,  0 },
    { EC_GF_OP_XOR2,   8,  0,  0 },
    { EC_GF_OP_XOR2,   6,  0,  0 },
    { EC_GF_OP_XOR2,   0,  3,  0 },
    { EC_GF_OP_XOR2,   3,  1,  0 },
    { EC_GF_OP_XOR2,   3,  5,  0 },
    { EC_GF_OP_XOR2,   5,  7,  0 },
    { EC_GF_OP_XOR2,   7,  1,  0 },
    { EC_GF_OP_XOR2,   1,  8,  0 },
    { EC_GF_OP_END,    0,  0,  0 }
};

static ec_gf_mul_t ec_gf8_mul_46 = {
    9,
    { 2, 0, 1, 3, 4, 5, 6, 7, 8, },
    ec_gf8_mul_46_ops
};

static ec_gf_op_t ec_gf8_mul_47_ops[] = {
    { EC_GF_OP_XOR3,   8,  0,  1 },
    { EC_GF_OP_XOR2,   2,  0,  0 },
    { EC_GF_OP_XOR2,   3,  8,  0 },
    { EC_GF_OP_XOR2,   5,  1,  0 },
    { EC_GF_OP_XOR2,   4,  8,  0 },
    { EC_GF_OP_END,    0,  0,  0 }
};

static ec_gf_mul_t ec_gf8_mul_47 = {
    9,
    { 2, 3, 4, 5, 6, 7, 0, 1, 8, },
    ec_gf8_mul_47_ops
};

static ec_gf_op_t ec_gf8_mul_48_ops[] = {
    { EC_GF_OP_XOR2,   5,  2,  0 },
    { EC_GF_OP_XOR2,   5,  4,  0 },
    { EC_GF_OP_XOR2,   6,  5,  0 },
    { EC_GF_OP_XOR2,   7,  6,  0 },
    { EC_GF_OP_XOR2,   2,  3,  0 },
    { EC_GF_OP_XOR2,   4,  7,  0 },
    { EC_GF_OP_XOR2,   3,  7,  0 },
    { EC_GF_OP_XOR2,   1,  3,  0 },
    { EC_GF_OP_XOR2,   0,  2,  0 },
    { EC_GF_OP_XOR2,   5,  3,  0 },
    { EC_GF_OP_XOR2,   2,  1,  0 },
    { EC_GF_OP_XOR2,   7,  0,  0 },
    { EC_GF_OP_END,    0,  0,  0 }
};

static ec_gf_mul_t ec_gf8_mul_48 = {
    8,
    { 4, 5, 6, 0, 1, 3, 7, 2, },
    ec_gf8_mul_48_ops
};

static ec_gf_op_t ec_gf8_mul_49_ops[] = {
    { EC_GF_OP_XOR2,   3,  0,  0 },
    { EC_GF_OP_XOR2,   0,  2,  0 },
    { EC_GF_OP_XOR2,   6,  5,  0 },
    { EC_GF_OP_XOR3,   8,  0,  6 },
    { EC_GF_OP_XOR2,   3,  1,  0 },
    { EC_GF_OP_XOR2,   7,  8,  0 },
    { EC_GF_OP_XOR2,   1,  4,  0 },
    { EC_GF_OP_XOR2,   3,  7,  0 },
    { EC_GF_OP_XOR2,   4,  6,  0 },
    { EC_GF_OP_XOR2,   5,  3,  0 },
    { EC_GF_OP_XOR2,   6,  1,  0 },
    { EC_GF_OP_XOR2,   2,  5,  0 },
    { EC_GF_OP_XOR2,   5,  1,  0 },
    { EC_GF_OP_XOR3,   1,  8,  5 },
    { EC_GF_OP_END,    0,  0,  0 }
};

static ec_gf_mul_t ec_gf8_mul_49 = {
    9,
    { 7, 2, 4, 0, 3, 5, 1, 6, 8, },
    ec_gf8_mul_49_ops
};

static ec_gf_op_t ec_gf8_mul_4A_ops[] = {
    { EC_GF_OP_XOR2,   2,  6,  0 },
    { EC_GF_OP_XOR2,   1,  4,  0 },
    { EC_GF_OP_XOR2,   3,  7,  0 },
    { EC_GF_OP_XOR2,   5,  2,  0 },
    { EC_GF_OP_XOR2,   1,  5,  0 },
    { EC_GF_OP_XOR2,   0,  3,  0 },
    { EC_GF_OP_XOR2,   7,  1,  0 },
    { EC_GF_OP_XOR2,   6,  0,  0 },
    { EC_GF_OP_XOR2,   3,  5,  0 },
    { EC_GF_OP_XOR2,   2,  7,  0 },
    { EC_GF_OP_XOR2,   4,  6,  0 },
    { EC_GF_OP_END,    0,  0,  0 }
};

static ec_gf_mul_t ec_gf8_mul_4A = {
    8,
    { 5, 6, 7, 0, 1, 3, 4, 2, },
    ec_gf8_mul_4A_ops
};

static ec_gf_op_t ec_gf8_mul_4B_ops[] = {
    { EC_GF_OP_XOR2,   6,  7,  0 },
    { EC_GF_OP_XOR2,   7,  0,  0 },
    { EC_GF_OP_XOR2,   0,  1,  0 },
    { EC_GF_OP_XOR2,   1,  4,  0 },
    { EC_GF_OP_XOR3,   8,  3,  7 },
    { EC_GF_OP_XOR2,   3,  6,  0 },
    { EC_GF_OP_XOR2,   1,  5,  0 },
    { EC_GF_OP_XOR2,   4,  8,  0 },
    { EC_GF_OP_XOR2,   2,  3,  0 },
    { EC_GF_OP_XOR2,   5,  8,  0 },
    { EC_GF_OP_XOR2,   3,  0,  0 },
    { EC_GF_OP_XOR2,   6,  1,  0 },
    { EC_GF_OP_XOR2,   5,  2,  0 },
    { EC_GF_OP_XOR2,   0,  5,  0 },
    { EC_GF_OP_END,    0,  0,  0 }
};

static ec_gf_mul_t ec_gf8_mul_4B = {
    9,
    { 5, 3, 6, 7, 0, 2, 4, 1, 8, },
    ec_gf8_mul_4B_ops
};

static ec_gf_op_t ec_gf8_mul_4C_ops[] = {
    { EC_GF_OP_XOR2,   5,  2,  0 },
    { EC_GF_OP_XOR2,   2,  0,  0 },
    { EC_GF_OP_XOR2,   3,  6,  0 },
    { EC_GF_OP_XOR2,   2,  3,  0 },
    { EC_GF_OP_XOR2,   4,  5,  0 },
    { EC_GF_OP_XOR2,   1,  2,  0 },
    { EC_GF_OP_XOR2,   5,  7,  0 },
    { EC_GF_OP_XOR2,   6,  4,  0 },
    { EC_GF_OP_XOR2,   7,  1,  0 },
    { EC_GF_OP_XOR2,   4,  0,  0 },
    { EC_GF_OP_XOR2,   1,  6,  0 },
    { EC_GF_OP_XOR2,   2,  5,  0 },
    { EC_GF_OP_XOR2,   0,  1,  0 },
    { EC_GF_OP_XOR2,   1,  2,  0 },
    { EC_GF_OP_END,    0,  0,  0 }
};

static ec_gf_mul_t ec_gf8_mul_4C = {
    8,
    { 5, 3, 4, 7, 0, 6, 2, 1, },
    ec_gf8_mul_4C_ops
};

static ec_gf_op_t ec_gf8_mul_4D_ops[] = {
    { EC_GF_OP_COPY,   8,  3,  0 },
    { EC_GF_OP_XOR2,   3,  4,  0 },
    { EC_GF_OP_XOR2,   4,  6,  0 },
    { EC_GF_OP_XOR2,   0,  1,  0 },
    { EC_GF_OP_XOR2,   1,  4,  0 },
    { EC_GF_OP_XOR3,   9,  3,  1 },
    { EC_GF_OP_XOR2,   5,  9,  0 },
    { EC_GF_OP_XOR2,   4,  2,  0 },
    { EC_GF_OP_XOR2,   6,  5,  0 },
    { EC_GF_OP_XOR2,   0,  6,  0 },
    { EC_GF_OP_XOR2,   7,  0,  0 },
    { EC_GF_OP_XOR2,   3,  0,  0 },
    { EC_GF_OP_XOR2,   2,  7,  0 },
    { EC_GF_OP_XOR3,   0,  8,  2 },
    { EC_GF_OP_XOR2,   5,  2,  0 },
    { EC_GF_OP_END,    0,  0,  0 }
};

static ec_gf_mul_t ec_gf8_mul_4D = {
    10,
    { 0, 9, 3, 5, 6, 4, 7, 1, 2, 8, },
    ec_gf8_mul_4D_ops
};

static ec_gf_op_t ec_gf8_mul_4E_ops[] = {
    { EC_GF_OP_XOR2,   3,  0,  0 },
    { EC_GF_OP_XOR2,   4,  1,  0 },
    { EC_GF_OP_XOR2,   0,  2,  0 },
    { EC_GF_OP_XOR2,   4,  7,  0 },
    { EC_GF_OP_XOR2,   2,  5,  0 },
    { EC_GF_OP_XOR2,   5,  4,  0 },
    { EC_GF_OP_XOR2,   3,  6,  0 },
    { EC_GF_OP_XOR2,   0,  5,  0 },
    { EC_GF_OP_XOR2,   6,  2,  0 },
    { EC_GF_OP_XOR2,   7,  3,  0 },
    { EC_GF_OP_XOR2,   1,  3,  0 },
    { EC_GF_OP_END,    0,  0,  0 }
};

static ec_gf_mul_t ec_gf8_mul_4E = {
    8,
    { 2, 3, 0, 1, 5, 6, 7, 4, },
    ec_gf8_mul_4E_ops
};

static ec_gf_op_t ec_gf8_mul_4F_ops[] = {
    { EC_GF_OP_XOR2,   4,  1,  0 },
    { EC_GF_OP_XOR2,   1,  0,  0 },
    { EC_GF_OP_XOR2,   7,  0,  0 },
    { EC_GF_OP_XOR2,   0,  2,  0 },
    { EC_GF_OP_XOR2,   0,  5,  0 },
    { EC_GF_OP_XOR2,   2,  6,  0 },
    { EC_GF_OP_XOR2,   5,  7,  0 },
    { EC_GF_OP_XOR2,   6,  1,  0 },
    { EC_GF_OP_XOR2,   7,  3,  0 },
    { EC_GF_OP_XOR2,   1,  5,  0 },
    { EC_GF_OP_XOR2,   3,  6,  0 },
    { EC_GF_OP_XOR2,   5,  4,  0 },
    { EC_GF_OP_END,    0,  0,  0 }
};

static ec_gf_mul_t ec_gf8_mul_4F = {
    8,
    { 0, 3, 5, 6, 1, 2, 7, 4, },
    ec_gf8_mul_4F_ops
};

static ec_gf_op_t ec_gf8_mul_50_ops[] = {
    { EC_GF_OP_XOR2,   5,  3,  0 },
    { EC_GF_OP_XOR2,   4,  6,  0 },
    { EC_GF_OP_XOR2,   6,  5,  0 },
    { EC_GF_OP_XOR2,   5,  7,  0 },
    { EC_GF_OP_XOR2,   7,  2,  0 },
    { EC_GF_OP_XOR2,   0,  2,  0 },
    { EC_GF_OP_XOR2,   4,  7,  0 },
    { EC_GF_OP_XOR2,   0,  6,  0 },
    { EC_GF_OP_XOR2,   3,  4,  0 },
    { EC_GF_OP_XOR2,   6,  1,  0 },
    { EC_GF_OP_XOR2,   2,  3,  0 },
    { EC_GF_OP_XOR2,   1,  2,  0 },
    { EC_GF_OP_XOR2,   2,  0,  0 },
    { EC_GF_OP_END,    0,  0,  0 }
};

static ec_gf_mul_t ec_gf8_mul_50 = {
    8,
    { 4, 5, 7, 3, 0, 1, 2, 6, },
    ec_gf8_mul_50_ops
};

static ec_gf_op_t ec_gf8_mul_51_ops[] = {
    { EC_GF_OP_XOR2,   2,  1,  0 },
    { EC_GF_OP_XOR2,   3,  5,  0 },
    { EC_GF_OP_XOR2,   2,  3,  0 },
    { EC_GF_OP_XOR2,   3,  7,  0 },
    { EC_GF_OP_XOR2,   1,  3,  0 },
    { EC_GF_OP_XOR2,   6,  1,  0 },
    { EC_GF_OP_XOR2,   4,  6,  0 },
    { EC_GF_OP_XOR2,   2,  4,  0 },
    { EC_GF_OP_XOR2,   0,  2,  0 },
    { EC_GF_OP_XOR2,   5,  0,  0 },
    { EC_GF_OP_XOR2,   3,  0,  0 },
    { EC_GF_OP_END,    0,  0,  0 }
};

static ec_gf_mul_t ec_gf8_mul_51 = {
    8,
    { 0, 1, 7, 2, 3, 4, 5, 6, },
    ec_gf8_mul_51_ops
};

static ec_gf_op_t ec_gf8_mul_52_ops[] = {
    { EC_GF_OP_XOR2,   0,  2,  0 },
    { EC_GF_OP_XOR2,   0,  7,  0 },
    { EC_GF_OP_COPY,   8,  0,  0 },
    { EC_GF_OP_XOR2,   0,  4,  0 },
    { EC_GF_OP_XOR2,   4,  6,  0 },
    { EC_GF_OP_COPY,   9,  4,  0 },
    { EC_GF_OP_XOR2,   4,  3,  0 },
    { EC_GF_OP_XOR2,   3,  1,  0 },
    { EC_GF_OP_XOR2,   5,  3,  0 },
    { EC_GF_OP_XOR2,   6,  3,  0 },
    { EC_GF_OP_XOR2,   1,  2,  0 },
    { EC_GF_OP_XOR3,   3,  5,  8 },
    { EC_GF_OP_XOR2,   7,  6,  0 },
    { EC_GF_OP_XOR2,   2,  9,  0 },
    { EC_GF_OP_XOR2,   6,  3,  0 },
    { EC_GF_OP_XOR2,   3,  1,  0 },
    { EC_GF_OP_END,    0,  0,  0 }
};

static ec_gf_mul_t ec_gf8_mul_52 = {
    10,
    { 2, 3, 1, 4, 6, 7, 0, 5, 8, 9, },
    ec_gf8_mul_52_ops
};

static ec_gf_op_t ec_gf8_mul_53_ops[] = {
    { EC_GF_OP_XOR2,   2,  0,  0 },
    { EC_GF_OP_XOR2,   3,  1,  0 },
    { EC_GF_OP_XOR2,   4,  6,  0 },
    { EC_GF_OP_XOR2,   2,  4,  0 },
    { EC_GF_OP_XOR2,   5,  7,  0 },
    { EC_GF_OP_XOR2,   3,  5,  0 },
    { EC_GF_OP_XOR2,   7,  2,  0 },
    { EC_GF_OP_XOR2,   5,  2,  0 },
    { EC_GF_OP_XOR2,   6,  3,  0 },
    { EC_GF_OP_XOR2,   0,  3,  0 },
    { EC_GF_OP_END,    0,  0,  0 }
};

static ec_gf_mul_t ec_gf8_mul_53 = {
    8,
    { 2, 0, 1, 4, 5, 6, 7, 3, },
    ec_gf8_mul_53_ops
};

static ec_gf_op_t ec_gf8_mul_54_ops[] = {
    { EC_GF_OP_XOR2,   7,  2,  0 },
    { EC_GF_OP_XOR2,   0,  7,  0 },
    { EC_GF_OP_XOR2,   7,  4,  0 },
    { EC_GF_OP_XOR2,   4,  1,  0 },
    { EC_GF_OP_XOR2,   2,  3,  0 },
    { EC_GF_OP_XOR2,   1,  3,  0 },
    { EC_GF_OP_XOR2,   3,  5,  0 },
    { EC_GF_OP_XOR2,   1,  6,  0 },
    { EC_GF_OP_XOR2,   5,  0,  0 },
    { EC_GF_OP_XOR2,   0,  6,  0 },
    { EC_GF_OP_XOR2,   6,  4,  0 },
    { EC_GF_OP_XOR2,   4,  2,  0 },
    { EC_GF_OP_XOR2,   2,  5,  0 },
    { EC_GF_OP_END,    0,  0,  0 }
};

static ec_gf_mul_t ec_gf8_mul_54 = {
    8,
    { 7, 3, 0, 4, 2, 6, 5, 1, },
    ec_gf8_mul_54_ops
};

static ec_gf_op_t ec_gf8_mul_55_ops[] = {
    { EC_GF_OP_XOR2,   4,  2,  0 },
    { EC_GF_OP_XOR2,   7,  0,  0 },
    { EC_GF_OP_XOR2,   4,  1,  0 },
    { EC_GF_OP_XOR2,   3,  1,  0 },
    { EC_GF_OP_XOR2,   6,  7,  0 },
    { EC_GF_OP_XOR2,   2,  5,  0 },
    { EC_GF_OP_XOR2,   7,  4,  0 },
    { EC_GF_OP_XOR2,   5,  3,  0 },
    { EC_GF_OP_XOR2,   2,  6,  0 },
    { EC_GF_OP_XOR2,   1,  7,  0 },
    { EC_GF_OP_XOR2,   3,  6,  0 },
    { EC_GF_OP_XOR2,   7,  2,  0 },
    { EC_GF_OP_XOR2,   0,  3,  0 },
    { EC_GF_OP_XOR2,   3,  7,  0 },
    { EC_GF_OP_END,    0,  0,  0 }
};

static ec_gf_mul_t ec_gf8_mul_55 = {
    8,
    { 1, 5, 6, 4, 3, 7, 2, 0, },
    ec_gf8_mul_55_ops
};

static ec_gf_op_t ec_gf8_mul_56_ops[] = {
    { EC_GF_OP_XOR2,   5,  0,  0 },
    { EC_GF_OP_XOR2,   6,  1,  0 },
    { EC_GF_OP_XOR2,   0,  2,  0 },
    { EC_GF_OP_XOR2,   1,  3,  0 },
    { EC_GF_OP_XOR2,   2,  4,  0 },
    { EC_GF_OP_XOR2,   3,  5,  0 },
    { EC_GF_OP_XOR2,   4,  1,  0 },
    { EC_GF_OP_XOR2,   4,  7,  0 },
    { EC_GF_OP_XOR2,   7,  0,  0 },
    { EC_GF_OP_XOR2,   0,  6,  0 },
    { EC_GF_OP_END,    0,  0,  0 }
};

static ec_gf_mul_t ec_gf8_mul_56 = {
    8,
    { 2, 3, 0, 4, 5, 6, 7, 1, },
    ec_gf8_mul_56_ops
};

static ec_gf_op_t ec_gf8_mul_57_ops[] = {
    { EC_GF_OP_XOR2,   2,  0,  0 },
    { EC_GF_OP_XOR2,   5,  0,  0 },
    { EC_GF_OP_XOR2,   0,  1,  0 },
    { EC_GF_OP_XOR2,   0,  6,  0 },
    { EC_GF_OP_XOR2,   3,  1,  0 },
    { EC_GF_OP_XOR2,   6,  7,  0 },
    { EC_GF_OP_XOR2,   1,  4,  0 },
    { EC_GF_OP_XOR2,   6,  2,  0 },
    { EC_GF_OP_XOR2,   1,  7,  0 },
    { EC_GF_OP_XOR2,   2,  4,  0 },
    { EC_GF_OP_XOR2,   7,  3,  0 },
    { EC_GF_OP_XOR2,   4,  5,  0 },
    { EC_GF_OP_XOR2,   3,  5,  0 },
    { EC_GF_OP_XOR2,   5,  0,  0 },
    { EC_GF_OP_END,    0,  0,  0 }
};

static ec_gf_mul_t ec_gf8_mul_57 = {
    8,
    { 2, 3, 0, 1, 4, 5, 6, 7, },
    ec_gf8_mul_57_ops
};

static ec_gf_op_t ec_gf8_mul_58_ops[] = {
    { EC_GF_OP_XOR2,   3,  2,  0 },
    { EC_GF_OP_XOR2,   2,  5,  0 },
    { EC_GF_OP_XOR2,   4,  3,  0 },
    { EC_GF_OP_XOR2,   5,  0,  0 },
    { EC_GF_OP_XOR2,   0,  1,  0 },
    { EC_GF_OP_XOR2,   5,  4,  0 },
    { EC_GF_OP_XOR2,   3,  7,  0 },
    { EC_GF_OP_XOR2,   1,  4,  0 },
    { EC_GF_OP_XOR2,   7,  5,  0 },
    { EC_GF_OP_XOR2,   0,  3,  0 },
    { EC_GF_OP_XOR2,   6,  2,  0 },
    { EC_GF_OP_XOR2,   3,  6,  0 },
    { EC_GF_OP_XOR2,   6,  1,  0 },
    { EC_GF_OP_XOR2,   4,  3,  0 },
    { EC_GF_OP_END,    0,  0,  0 }
};

static ec_gf_mul_t ec_gf8_mul_58 = {
    8,
    { 4, 3, 2, 7, 0, 1, 5, 6, },
    ec_gf8_mul_58_ops
};

static ec_gf_op_t ec_gf8_mul_59_ops[] = {
    { EC_GF_OP_XOR2,   0,  1,  0 },
    { EC_GF_OP_XOR2,   1,  3,  0 },
    { EC_GF_OP_XOR2,   1,  5,  0 },
    { EC_GF_OP_XOR2,   2,  4,  0 },
    { EC_GF_OP_XOR2,   0,  6,  0 },
    { EC_GF_OP_XOR2,   2,  1,  0 },
    { EC_GF_OP_XOR2,   0,  2,  0 },
    { EC_GF_OP_XOR2,   3,  0,  0 },
    { EC_GF_OP_XOR2,   7,  3,  0 },
    { EC_GF_OP_XOR2,   6,  7,  0 },
    { EC_GF_OP_XOR2,   1,  6,  0 },
    { EC_GF_OP_XOR2,   3,  1,  0 },
    { EC_GF_OP_XOR2,   4,  3,  0 },
    { EC_GF_OP_END,    0,  0,  0 }
};

static ec_gf_mul_t ec_gf8_mul_59 = {
    8,
    { 7, 3, 5, 6, 1, 2, 0, 4, },
    ec_gf8_mul_59_ops
};

static ec_gf_op_t ec_gf8_mul_5A_ops[] = {
    { EC_GF_OP_XOR2,   5,  2,  0 },
    { EC_GF_OP_XOR2,   2,  1,  0 },
    { EC_GF_OP_XOR2,   4,  5,  0 },
    { EC_GF_OP_XOR2,   3,  2,  0 },
    { EC_GF_OP_XOR2,   6,  4,  0 },
    { EC_GF_OP_XOR2,   4,  3,  0 },
    { EC_GF_OP_XOR2,   2,  0,  0 },
    { EC_GF_OP_XOR2,   0,  4,  0 },
    { EC_GF_OP_XOR2,   1,  0,  0 },
    { EC_GF_OP_XOR2,   5,  1,  0 },
    { EC_GF_OP_XOR2,   7,  5,  0 },
    { EC_GF_OP_XOR2,   0,  7,  0 },
    { EC_GF_OP_XOR2,   7,  6,  0 },
    { EC_GF_OP_END,    0,  0,  0 }
};

static ec_gf_mul_t ec_gf8_mul_5A = {
    8,
    { 6, 7, 0, 1, 2, 3, 5, 4, },
    ec_gf8_mul_5A_ops
};

static ec_gf_op_t ec_gf8_mul_5B_ops[] = {
    { EC_GF_OP_XOR2,   0,  4,  0 },
    { EC_GF_OP_XOR2,   1,  5,  0 },
    { EC_GF_OP_XOR2,   4,  3,  0 },
    { EC_GF_OP_XOR2,   5,  0,  0 },
    { EC_GF_OP_XOR2,   3,  2,  0 },
    { EC_GF_OP_XOR2,   2,  5,  0 },
    { EC_GF_OP_XOR2,   0,  6,  0 },
    { EC_GF_OP_XOR2,   6,  2,  0 },
    { EC_GF_OP_XOR2,   7,  1,  0 },
    { EC_GF_OP_XOR2,   2,  1,  0 },
    { EC_GF_OP_XOR2,   1,  3,  0 },
    { EC_GF_OP_XOR2,   4,  7,  0 },
    { EC_GF_OP_XOR2,   3,  0,  0 },
    { EC_GF_OP_XOR2,   0,  4,  0 },
    { EC_GF_OP_END,    0,  0,  0 }
};

static ec_gf_mul_t ec_gf8_mul_5B = {
    8,
    { 6, 0, 7, 5, 2, 1, 3, 4, },
    ec_gf8_mul_5B_ops
};

static ec_gf_op_t ec_gf8_mul_5C_ops[] = {
    { EC_GF_OP_COPY,   8,  3,  0 },
    { EC_GF_OP_XOR2,   3,  6,  0 },
    { EC_GF_OP_XOR2,   4,  1,  0 },
    { EC_GF_OP_XOR2,   5,  3,  0 },
    { EC_GF_OP_XOR2,   1,  0,  0 },
    { EC_GF_OP_XOR2,   0,  5,  0 },
    { EC_GF_OP_XOR2,   1,  3,  0 },
    { EC_GF_OP_XOR2,   2,  0,  0 },
    { EC_GF_OP_XOR2,   3,  4,  0 },
    { EC_GF_OP_XOR2,   4,  2,  0 },
    { EC_GF_OP_XOR2,   6,  2,  0 },
    { EC_GF_OP_XOR2,   7,  4,  0 },
    { EC_GF_OP_XOR2,   2,  8,  0 },
    { EC_GF_OP_XOR2,   0,  7,  0 },
    { EC_GF_OP_XOR2,   7,  1,  0 },
    { EC_GF_OP_END,    0,  0,  0 }
};

static ec_gf_mul_t ec_gf8_mul_5C = {
    9,
    { 7, 5, 2, 4, 1, 0, 6, 3, 8, },
    ec_gf8_mul_5C_ops
};

static ec_gf_op_t ec_gf8_mul_5D_ops[] = {
    { EC_GF_OP_XOR2,   4,  1,  0 },
    { EC_GF_OP_XOR2,   1,  0,  0 },
    { EC_GF_OP_XOR2,   6,  0,  0 },
    { EC_GF_OP_XOR2,   4,  3,  0 },
    { EC_GF_OP_XOR2,   5,  6,  0 },
    { EC_GF_OP_XOR2,   6,  4,  0 },
    { EC_GF_OP_XOR2,   3,  5,  0 },
    { EC_GF_OP_XOR2,   7,  6,  0 },
    { EC_GF_OP_XOR2,   2,  3,  0 },
    { EC_GF_OP_XOR2,   0,  7,  0 },
    { EC_GF_OP_XOR2,   3,  1,  0 },
    { EC_GF_OP_XOR2,   7,  2,  0 },
    { EC_GF_OP_XOR2,   4,  2,  0 },
    { EC_GF_OP_XOR2,   1,  7,  0 },
    { EC_GF_OP_END,    0,  0,  0 }
};

static ec_gf_mul_t ec_gf8_mul_5D = {
    8,
    { 1, 3, 5, 4, 6, 7, 2, 0, },
    ec_gf8_mul_5D_ops
};

static ec_gf_op_t ec_gf8_mul_5E_ops[] = {
    { EC_GF_OP_XOR2,   6,  0,  0 },
    { EC_GF_OP_XOR2,   6,  5,  0 },
    { EC_GF_OP_XOR2,   1,  2,  0 },
    { EC_GF_OP_XOR2,   5,  2,  0 },
    { EC_GF_OP_XOR2,   2,  3,  0 },
    { EC_GF_OP_XOR2,   7,  1,  0 },
    { EC_GF_OP_XOR2,   3,  6,  0 },
    { EC_GF_OP_XOR2,   0,  2,  0 },
    { EC_GF_OP_XOR2,   6,  7,  0 },
    { EC_GF_OP_XOR2,   2,  4,  0 },
    { EC_GF_OP_XOR2,   4,  5,  0 },
    { EC_GF_OP_XOR2,   1,  2,  0 },
    { EC_GF_OP_XOR2,   5,  6,  0 },
    { EC_GF_OP_XOR2,   2,  6,  0 },
    { EC_GF_OP_END,    0,  0,  0 }
};

static ec_gf_mul_t ec_gf8_mul_5E = {
    8,
    { 4, 3, 6, 2, 5, 7, 0, 1, },
    ec_gf8_mul_5E_ops
};

static ec_gf_op_t ec_gf8_mul_5F_ops[] = {
    { EC_GF_OP_XOR2,   0,  3,  0 },
    { EC_GF_OP_XOR2,   1,  5,  0 },
    { EC_GF_OP_XOR2,   0,  6,  0 },
    { EC_GF_OP_XOR2,   1,  0,  0 },
    { EC_GF_OP_XOR2,   7,  1,  0 },
    { EC_GF_OP_XOR2,   3,  7,  0 },
    { EC_GF_OP_XOR2,   2,  0,  0 },
    { EC_GF_OP_XOR2,   4,  3,  0 },
    { EC_GF_OP_XOR2,   7,  2,  0 },
    { EC_GF_OP_XOR2,   5,  4,  0 },
    { EC_GF_OP_XOR2,   6,  5,  0 },
    { EC_GF_OP_XOR2,   0,  5,  0 },
    { EC_GF_OP_XOR2,   6,  7,  0 },
    { EC_GF_OP_END,    0,  0,  0 }
};

static ec_gf_mul_t ec_gf8_mul_5F = {
    8,
    { 6, 1, 3, 4, 5, 7, 2, 0, },
    ec_gf8_mul_5F_ops
};

static ec_gf_op_t ec_gf8_mul_60_ops[] = {
    { EC_GF_OP_XOR2,   5,  2,  0 },
    { EC_GF_OP_XOR2,   7,  4,  0 },
    { EC_GF_OP_XOR2,   4,  5,  0 },
    { EC_GF_OP_XOR2,   6,  3,  0 },
    { EC_GF_OP_XOR2,   2,  6,  0 },
    { EC_GF_OP_XOR2,   4,  6,  0 },
    { EC_GF_OP_XOR2,   3,  7,  0 },
    { EC_GF_OP_XOR2,   6,  0,  0 },
    { EC_GF_OP_XOR2,   0,  1,  0 },
    { EC_GF_OP_XOR2,   0,  7,  0 },
    { EC_GF_OP_XOR2,   1,  5,  0 },
    { EC_GF_OP_XOR2,   7,  5,  0 },
    { EC_GF_OP_END,    0,  0,  0 }
};

static ec_gf_mul_t ec_gf8_mul_60 = {
    8,
    { 2, 3, 4, 7, 5, 6, 0, 1, },
    ec_gf8_mul_60_ops
};

static ec_gf_op_t ec_gf8_mul_61_ops[] = {
    { EC_GF_OP_XOR2,   1,  2,  0 },
    { EC_GF_OP_XOR2,   6,  2,  0 },
    { EC_GF_OP_XOR2,   2,  5,  0 },
    { EC_GF_OP_XOR2,   4,  2,  0 },
    { EC_GF_OP_XOR2,   0,  3,  0 },
    { EC_GF_OP_XOR2,   3,  4,  0 },
    { EC_GF_OP_XOR2,   0,  6,  0 },
    { EC_GF_OP_XOR2,   7,  3,  0 },
    { EC_GF_OP_XOR2,   2,  0,  0 },
    { EC_GF_OP_XOR2,   6,  3,  0 },
    { EC_GF_OP_XOR2,   1,  7,  0 },
    { EC_GF_OP_XOR2,   5,  1,  0 },
    { EC_GF_OP_XOR2,   1,  2,  0 },
    { EC_GF_OP_XOR2,   3,  5,  0 },
    { EC_GF_OP_END,    0,  0,  0 }
};

static ec_gf_mul_t ec_gf8_mul_61 = {
    8,
    { 0, 5, 6, 7, 4, 2, 1, 3, },
    ec_gf8_mul_61_ops
};

static ec_gf_op_t ec_gf8_mul_62_ops[] = {
    { EC_GF_OP_XOR2,   3,  7,  0 },
    { EC_GF_OP_XOR2,   1,  2,  0 },
    { EC_GF_OP_XOR2,   2,  3,  0 },
    { EC_GF_OP_XOR2,   3,  4,  0 },
    { EC_GF_OP_XOR2,   4,  5,  0 },
    { EC_GF_OP_XOR2,   0,  3,  0 },
    { EC_GF_OP_XOR2,   5,  2,  0 },
    { EC_GF_OP_XOR2,   7,  0,  0 },
    { EC_GF_OP_XOR2,   2,  6,  0 },
    { EC_GF_OP_XOR2,   1,  5,  0 },
    { EC_GF_OP_XOR2,   6,  7,  0 },
    { EC_GF_OP_XOR2,   7,  1,  0 },
    { EC_GF_OP_XOR2,   1,  2,  0 },
    { EC_GF_OP_XOR2,   3,  1,  0 },
    { EC_GF_OP_END,    0,  0,  0 }
};

static ec_gf_mul_t ec_gf8_mul_62 = {
    8,
    { 2, 0, 3, 4, 5, 6, 7, 1, },
    ec_gf8_mul_62_ops
};

static ec_gf_op_t ec_gf8_mul_63_ops[] = {
    { EC_GF_OP_XOR2,   4,  3,  0 },
    { EC_GF_OP_XOR2,   5,  4,  0 },
    { EC_GF_OP_XOR2,   6,  5,  0 },
    { EC_GF_OP_XOR2,   1,  7,  0 },
    { EC_GF_OP_XOR2,   0,  6,  0 },
    { EC_GF_OP_XOR2,   6,  1,  0 },
    { EC_GF_OP_XOR2,   7,  2,  0 },
    { EC_GF_OP_XOR2,   3,  0,  0 },
    { EC_GF_OP_XOR2,   4,  6,  0 },
    { EC_GF_OP_XOR2,   7,  5,  0 },
    { EC_GF_OP_XOR2,   1,  3,  0 },
    { EC_GF_OP_XOR2,   2,  4,  0 },
    { EC_GF_OP_XOR2,   3,  7,  0 },
    { EC_GF_OP_XOR2,   4,  0,  0 },
    { EC_GF_OP_END,    0,  0,  0 }
};

static ec_gf_mul_t ec_gf8_mul_63 = {
    8,
    { 3, 4, 6, 5, 7, 0, 1, 2, },
    ec_gf8_mul_63_ops
};

static ec_gf_op_t ec_gf8_mul_64_ops[] = {
    { EC_GF_OP_COPY,   8,  1,  0 },
    { EC_GF_OP_XOR2,   8,  0,  0 },
    { EC_GF_OP_XOR2,   1,  2,  0 },
    { EC_GF_OP_XOR2,   8,  7,  0 },
    { EC_GF_OP_XOR2,   2,  3,  0 },
    { EC_GF_OP_XOR2,   3,  4,  0 },
    { EC_GF_OP_XOR2,   7,  6,  0 },
    { EC_GF_OP_XOR2,   4,  5,  0 },
    { EC_GF_OP_XOR2,   6,  1,  0 },
    { EC_GF_OP_XOR2,   5,  7,  0 },
    { EC_GF_OP_XOR2,   6,  4,  0 },
    { EC_GF_OP_XOR2,   7,  0,  0 },
    { EC_GF_OP_XOR2,   4,  2,  0 },
    { EC_GF_OP_XOR2,   4,  0,  0 },
    { EC_GF_OP_END,    0,  0,  0 }
};

static ec_gf_mul_t ec_gf8_mul_64 = {
    9,
    { 2, 3, 4, 6, 5, 7, 8, 1, 0, },
    ec_gf8_mul_64_ops
};

static ec_gf_op_t ec_gf8_mul_65_ops[] = {
    { EC_GF_OP_XOR2,   6,  7,  0 },
    { EC_GF_OP_XOR2,   7,  2,  0 },
    { EC_GF_OP_XOR2,   5,  6,  0 },
    { EC_GF_OP_XOR2,   2,  0,  0 },
    { EC_GF_OP_XOR2,   4,  5,  0 },
    { EC_GF_OP_XOR2,   2,  3,  0 },
    { EC_GF_OP_XOR2,   6,  1,  0 },
    { EC_GF_OP_XOR2,   3,  4,  0 },
    { EC_GF_OP_XOR2,   7,  1,  0 },
    { EC_GF_OP_XOR2,   6,  0,  0 },
    { EC_GF_OP_XOR2,   1,  3,  0 },
    { EC_GF_OP_XOR2,   0,  5,  0 },
    { EC_GF_OP_XOR2,   3,  7,  0 },
    { EC_GF_OP_XOR2,   5,  1,  0 },
    { EC_GF_OP_XOR2,   1,  6,  0 },
    { EC_GF_OP_END,    0,  0,  0 }
};

static ec_gf_mul_t ec_gf8_mul_65 = {
    8,
    { 2, 5, 1, 3, 4, 0, 6, 7, },
    ec_gf8_mul_65_ops
};

static ec_gf_op_t ec_gf8_mul_66_ops[] = {
    { EC_GF_OP_XOR2,   4,  0,  0 },
    { EC_GF_OP_XOR2,   0,  1,  0 },
    { EC_GF_OP_XOR2,   1,  2,  0 },
    { EC_GF_OP_XOR2,   2,  3,  0 },
    { EC_GF_OP_XOR2,   3,  4,  0 },
    { EC_GF_OP_XOR2,   5,  7,  0 },
    { EC_GF_OP_XOR2,   2,  7,  0 },
    { EC_GF_OP_XOR2,   0,  5,  0 },
    { EC_GF_OP_XOR2,   4,  6,  0 },
    { EC_GF_OP_XOR2,   5,  3,  0 },
    { EC_GF_OP_XOR2,   6,  1,  0 },
    { EC_GF_OP_XOR2,   7,  4,  0 },
    { EC_GF_OP_XOR2,   1,  5,  0 },
    { EC_GF_OP_XOR2,   4,  0,  0 },
    { EC_GF_OP_XOR2,   5,  7,  0 },
    { EC_GF_OP_END,    0,  0,  0 }
};

static ec_gf_mul_t ec_gf8_mul_66 = {
    8,
    { 2, 3, 1, 4, 5, 7, 0, 6, },
    ec_gf8_mul_66_ops
};

static ec_gf_op_t ec_gf8_mul_67_ops[] = {
    { EC_GF_OP_XOR2,   3,  0,  0 },
    { EC_GF_OP_XOR2,   3,  1,  0 },
    { EC_GF_OP_XOR2,   1,  4,  0 },
    { EC_GF_OP_XOR2,   4,  3,  0 },
    { EC_GF_OP_XOR2,   7,  4,  0 },
    { EC_GF_OP_XOR2,   5,  7,  0 },
    { EC_GF_OP_XOR2,   0,  5,  0 },
    { EC_GF_OP_XOR2,   6,  0,  0 },
    { EC_GF_OP_XOR2,   3,  6,  0 },
    { EC_GF_OP_XOR2,   1,  3,  0 },
    { EC_GF_OP_XOR2,   7,  1,  0 },
    { EC_GF_OP_XOR2,   2,  7,  0 },
    { EC_GF_OP_XOR2,   0,  2,  0 },
    { EC_GF_OP_XOR2,   2,  3,  0 },
    { EC_GF_OP_END,    0,  0,  0 }
};

static ec_gf_mul_t ec_gf8_mul_67 = {
    8,
    { 2, 4, 5, 6, 7, 3, 1, 0, },
    ec_gf8_mul_67_ops
};

static ec_gf_op_t ec_gf8_mul_68_ops[] = {
    { EC_GF_OP_XOR2,   5,  2,  0 },
    { EC_GF_OP_XOR2,   5,  4,  0 },
    { EC_GF_OP_XOR2,   4,  3,  0 },
    { EC_GF_OP_XOR2,   6,  4,  0 },
    { EC_GF_OP_XOR2,   7,  6,  0 },
    { EC_GF_OP_XOR2,   4,  0,  0 },
    { EC_GF_OP_XOR2,   2,  7,  0 },
    { EC_GF_OP_XOR2,   4,  1,  0 },
    { EC_GF_OP_XOR2,   0,  2,  0 },
    { EC_GF_OP_XOR2,   1,  5,  0 },
    { EC_GF_OP_XOR2,   3,  0,  0 },
    { EC_GF_OP_XOR2,   5,  6,  0 },
    { EC_GF_OP_XOR2,   0,  4,  0 },
    { EC_GF_OP_XOR2,   6,  3,  0 },
    { EC_GF_OP_END,    0,  0,  0 }
};

static ec_gf_mul_t ec_gf8_mul_68 = {
    8,
    { 5, 7, 2, 3, 0, 6, 4, 1, },
    ec_gf8_mul_68_ops
};

static ec_gf_op_t ec_gf8_mul_69_ops[] = {
    { EC_GF_OP_XOR2,   4,  6,  0 },
    { EC_GF_OP_XOR2,   4,  7,  0 },
    { EC_GF_OP_XOR2,   3,  4,  0 },
    { EC_GF_OP_XOR2,   2,  1,  0 },
    { EC_GF_OP_XOR2,   1,  3,  0 },
    { EC_GF_OP_XOR2,   4,  2,  0 },
    { EC_GF_OP_XOR2,   0,  1,  0 },
    { EC_GF_OP_XOR2,   5,  4,  0 },
    { EC_GF_OP_XOR2,   7,  0,  0 },
    { EC_GF_OP_XOR2,   6,  5,  0 },
    { EC_GF_OP_XOR2,   2,  0,  0 },
    { EC_GF_OP_XOR2,   5,  7,  0 },
    { EC_GF_OP_XOR2,   0,  6,  0 },
    { EC_GF_OP_END,    0,  0,  0 }
};

static ec_gf_mul_t ec_gf8_mul_69 = {
    8,
    { 0, 1, 3, 2, 4, 5, 7, 6, },
    ec_gf8_mul_69_ops
};

static ec_gf_op_t ec_gf8_mul_6A_ops[] = {
    { EC_GF_OP_XOR2,   7,  3,  0 },
    { EC_GF_OP_XOR2,   3,  5,  0 },
    { EC_GF_OP_XOR2,   0,  4,  0 },
    { EC_GF_OP_XOR2,   2,  6,  0 },
    { EC_GF_OP_XOR2,   6,  0,  0 },
    { EC_GF_OP_XOR2,   5,  2,  0 },
    { EC_GF_OP_XOR2,   1,  3,  0 },
    { EC_GF_OP_XOR2,   0,  1,  0 },
    { EC_GF_OP_XOR2,   1,  5,  0 },
    { EC_GF_OP_XOR2,   5,  7,  0 },
    { EC_GF_OP_XOR2,   4,  1,  0 },
    { EC_GF_OP_XOR2,   7,  6,  0 },
    { EC_GF_OP_XOR2,   3,  4,  0 },
    { EC_GF_OP_XOR2,   2,  7,  0 },
    { EC_GF_OP_END,    0,  0,  0 }
};

static ec_gf_mul_t ec_gf8_mul_6A = {
    8,
    { 5, 7, 4, 6, 1, 2, 0, 3, },
    ec_gf8_mul_6A_ops
};

static ec_gf_op_t ec_gf8_mul_6B_ops[] = {
    { EC_GF_OP_COPY,   8,  1,  0 },
    { EC_GF_OP_XOR2,   1,  2,  0 },
    { EC_GF_OP_XOR2,   1,  6,  0 },
    { EC_GF_OP_XOR2,   3,  4,  0 },
    { EC_GF_OP_XOR2,   3,  1,  0 },
    { EC_GF_OP_XOR2,   2,  3,  0 },
    { EC_GF_OP_XOR2,   0,  2,  0 },
    { EC_GF_OP_XOR2,   1,  5,  0 },
    { EC_GF_OP_XOR2,   7,  0,  0 },
    { EC_GF_OP_XOR2,   5,  0,  0 },
    { EC_GF_OP_XOR2,   1,  7,  0 },
    { EC_GF_OP_XOR2,   4,  1,  0 },
    { EC_GF_OP_XOR2,   6,  4,  0 },
    { EC_GF_OP_XOR2,   4,  0,  0 },
    { EC_GF_OP_XOR2,   0,  8,  0 },
    { EC_GF_OP_END,    0,  0,  0 }
};

static ec_gf_mul_t ec_gf8_mul_6B = {
    9,
    { 6, 7, 2, 0, 3, 1, 5, 4, 8, },
    ec_gf8_mul_6B_ops
};

static ec_gf_op_t ec_gf8_mul_6C_ops[] = {
    { EC_GF_OP_XOR2,   5,  2,  0 },
    { EC_GF_OP_XOR2,   4,  3,  0 },
    { EC_GF_OP_XOR2,   5,  3,  0 },
    { EC_GF_OP_XOR2,   2,  0,  0 },
    { EC_GF_OP_XOR2,   6,  4,  0 },
    { EC_GF_OP_XOR2,   4,  2,  0 },
    { EC_GF_OP_XOR2,   3,  1,  0 },
    { EC_GF_OP_XOR2,   3,  0,  0 },
    { EC_GF_OP_XOR2,   7,  4,  0 },
    { EC_GF_OP_XOR2,   4,  3,  0 },
    { EC_GF_OP_XOR2,   0,  4,  0 },
    { EC_GF_OP_END,    0,  0,  0 }
};

static ec_gf_mul_t ec_gf8_mul_6C = {
    8,
    { 5, 6, 7, 0, 1, 2, 3, 4, },
    ec_gf8_mul_6C_ops
};

static ec_gf_op_t ec_gf8_mul_6D_ops[] = {
    { EC_GF_OP_XOR2,   4,  1,  0 },
    { EC_GF_OP_XOR2,   1,  0,  0 },
    { EC_GF_OP_XOR2,   7,  4,  0 },
    { EC_GF_OP_XOR2,   0,  2,  0 },
    { EC_GF_OP_XOR2,   1,  3,  0 },
    { EC_GF_OP_XOR2,   2,  7,  0 },
    { EC_GF_OP_XOR3,   8,  3,  4 },
    { EC_GF_OP_XOR2,   7,  1,  0 },
    { EC_GF_OP_XOR2,   5,  0,  0 },
    { EC_GF_OP_XOR2,   1,  6,  0 },
    { EC_GF_OP_XOR2,   0,  8,  0 },
    { EC_GF_OP_XOR2,   3,  5,  0 },
    { EC_GF_OP_XOR2,   6,  8,  0 },
    { EC_GF_OP_END,    0,  0,  0 }
};

static ec_gf_mul_t ec_gf8_mul_6D = {
    9,
    { 3, 6, 7, 0, 4, 5, 1, 2, 8, },
    ec_gf8_mul_6D_ops
};

static ec_gf_op_t ec_gf8_mul_6E_ops[] = {
    { EC_GF_OP_XOR2,   3,  1,  0 },
    { EC_GF_OP_XOR2,   1,  2,  0 },
    { EC_GF_OP_XOR2,   2,  0,  0 },
    { EC_GF_OP_XOR2,   7,  3,  0 },
    { EC_GF_OP_XOR2,   0,  5,  0 },
    { EC_GF_OP_XOR2,   6,  1,  0 },
    { EC_GF_OP_XOR2,   0,  3,  0 },
    { EC_GF_OP_XOR2,   2,  4,  0 },
    { EC_GF_OP_XOR2,   1,  7,  0 },
    { EC_GF_OP_XOR2,   4,  6,  0 },
    { EC_GF_OP_XOR2,   3,  2,  0 },
    { EC_GF_OP_XOR2,   5,  1,  0 },
    { EC_GF_OP_XOR2,   6,  3,  0 },
    { EC_GF_OP_XOR2,   1,  3,  0 },
    { EC_GF_OP_END,    0,  0,  0 }
};

static ec_gf_mul_t ec_gf8_mul_6E = {
    8,
    { 5, 6, 3, 1, 7, 2, 0, 4, },
    ec_gf8_mul_6E_ops
};

static ec_gf_op_t ec_gf8_mul_6F_ops[] = {
    { EC_GF_OP_COPY,   8,  0,  0 },
    { EC_GF_OP_XOR2,   0,  1,  0 },
    { EC_GF_OP_XOR2,   0,  4,  0 },
    { EC_GF_OP_XOR2,   3,  0,  0 },
    { EC_GF_OP_XOR2,   2,  5,  0 },
    { EC_GF_OP_XOR2,   7,  3,  0 },
    { EC_GF_OP_XOR2,   2,  0,  0 },
    { EC_GF_OP_XOR2,   6,  3,  0 },
    { EC_GF_OP_XOR3,   0,  8,  7 },
    { EC_GF_OP_XOR2,   1,  2,  0 },
    { EC_GF_OP_XOR2,   5,  6,  0 },
    { EC_GF_OP_XOR2,   2,  0,  0 },
    { EC_GF_OP_XOR2,   4,  5,  0 },
    { EC_GF_OP_XOR2,   5,  2,  0 },
    { EC_GF_OP_END,    0,  0,  0 }
};

static ec_gf_mul_t ec_gf8_mul_6F = {
    9,
    { 2, 6, 3, 7, 0, 1, 4, 5, 8, },
    ec_gf8_mul_6F_ops
};

static ec_gf_op_t ec_gf8_mul_70_ops[] = {
    { EC_GF_OP_XOR2,   3,  2,  0 },
    { EC_GF_OP_XOR2,   5,  3,  0 },
    { EC_GF_OP_XOR2,   6,  4,  0 },
    { EC_GF_OP_XOR2,   4,  2,  0 },
    { EC_GF_OP_XOR2,   7,  5,  0 },
    { EC_GF_OP_XOR2,   0,  2,  0 },
    { EC_GF_OP_XOR2,   3,  6,  0 },
    { EC_GF_OP_XOR2,   4,  7,  0 },
    { EC_GF_OP_XOR2,   6,  0,  0 },
    { EC_GF_OP_XOR2,   7,  1,  0 },
    { EC_GF_OP_XOR2,   1,  6,  0 },
    { EC_GF_OP_XOR2,   0,  7,  0 },
    { EC_GF_OP_END,    0,  0,  0 }
};

static ec_gf_mul_t ec_gf8_mul_70 = {
    8,
    { 3, 4, 5, 2, 6, 0, 1, 7, },
    ec_gf8_mul_70_ops
};

static ec_gf_op_t ec_gf8_mul_71_ops[] = {
    { EC_GF_OP_XOR2,   6,  0,  0 },
    { EC_GF_OP_XOR2,   0,  1,  0 },
    { EC_GF_OP_XOR2,   1,  5,  0 },
    { EC_GF_OP_XOR2,   0,  4,  0 },
    { EC_GF_OP_XOR2,   5,  3,  0 },
    { EC_GF_OP_XOR2,   4,  3,  0 },
    { EC_GF_OP_XOR2,   6,  2,  0 },
    { EC_GF_OP_XOR2,   3,  2,  0 },
    { EC_GF_OP_XOR2,   7,  4,  0 },
    { EC_GF_OP_XOR2,   2,  0,  0 },
    { EC_GF_OP_XOR2,   4,  6,  0 },
    { EC_GF_OP_XOR2,   0,  7,  0 },
    { EC_GF_OP_XOR2,   7,  1,  0 },
    { EC_GF_OP_XOR2,   1,  3,  0 },
    { EC_GF_OP_END,    0,  0,  0 }
};

static ec_gf_mul_t ec_gf8_mul_71 = {
    8,
    { 4, 7, 5, 3, 6, 0, 2, 1, },
    ec_gf8_mul_71_ops
};

static ec_gf_op_t ec_gf8_mul_72_ops[] = {
    { EC_GF_OP_XOR2,   4,  0,  0 },
    { EC_GF_OP_XOR2,   3,  7,  0 },
    { EC_GF_OP_XOR2,   3,  4,  0 },
    { EC_GF_OP_XOR2,   5,  3,  0 },
    { EC_GF_OP_XOR2,   1,  5,  0 },
    { EC_GF_OP_XOR2,   4,  1,  0 },
    { EC_GF_OP_XOR2,   2,  4,  0 },
    { EC_GF_OP_XOR2,   6,  2,  0 },
    { EC_GF_OP_XOR2,   3,  6,  0 },
    { EC_GF_OP_XOR2,   4,  3,  0 },
    { EC_GF_OP_XOR2,   0,  4,  0 },
    { EC_GF_OP_END,    0,  0,  0 }
};

static ec_gf_mul_t ec_gf8_mul_72 = {
    8,
    { 0, 5, 2, 7, 4, 1, 3, 6, },
    ec_gf8_mul_72_ops
};

static ec_gf_op_t ec_gf8_mul_73_ops[] = {
    { EC_GF_OP_XOR2,   1,  5,  0 },
    { EC_GF_OP_XOR2,   2,  1,  0 },
    { EC_GF_OP_XOR2,   7,  3,  0 },
    { EC_GF_OP_XOR2,   1,  7,  0 },
    { EC_GF_OP_XOR2,   0,  4,  0 },
    { EC_GF_OP_XOR2,   6,  2,  0 },
    { EC_GF_OP_XOR2,   2,  0,  0 },
    { EC_GF_OP_XOR2,   3,  6,  0 },
    { EC_GF_OP_XOR2,   0,  1,  0 },
    { EC_GF_OP_XOR2,   6,  0,  0 },
    { EC_GF_OP_XOR2,   5,  0,  0 },
    { EC_GF_OP_XOR2,   4,  6,  0 },
    { EC_GF_OP_END,    0,  0,  0 }
};

static ec_gf_mul_t ec_gf8_mul_73 = {
    8,
    { 6, 0, 1, 7, 4, 5, 2, 3, },
    ec_gf8_mul_73_ops
};

static ec_gf_op_t ec_gf8_mul_74_ops[] = {
    { EC_GF_OP_XOR2,   3,  2,  0 },
    { EC_GF_OP_XOR2,   2,  5,  0 },
    { EC_GF_OP_XOR2,   5,  0,  0 },
    { EC_GF_OP_XOR2,   5,  1,  0 },
    { EC_GF_OP_XOR2,   1,  3,  0 },
    { EC_GF_OP_XOR2,   0,  7,  0 },
    { EC_GF_OP_XOR2,   6,  5,  0 },
    { EC_GF_OP_XOR2,   7,  1,  0 },
    { EC_GF_OP_XOR2,   1,  6,  0 },
    { EC_GF_OP_XOR2,   3,  4,  0 },
    { EC_GF_OP_XOR2,   6,  2,  0 },
    { EC_GF_OP_XOR2,   4,  0,  0 },
    { EC_GF_OP_XOR2,   2,  3,  0 },
    { EC_GF_OP_XOR2,   0,  6,  0 },
    { EC_GF_OP_END,    0,  0,  0 }
};

static ec_gf_mul_t ec_gf8_mul_74 = {
    8,
    { 3, 2, 1, 0, 4, 5, 6, 7, },
    ec_gf8_mul_74_ops
};

static ec_gf_op_t ec_gf8_mul_75_ops[] = {
    { EC_GF_OP_XOR2,   2,  0,  0 },
    { EC_GF_OP_XOR2,   3,  2,  0 },
    { EC_GF_OP_XOR2,   2,  1,  0 },
    { EC_GF_OP_XOR2,   1,  0,  0 },
    { EC_GF_OP_XOR2,   4,  3,  0 },
    { EC_GF_OP_XOR2,   3,  1,  0 },
    { EC_GF_OP_XOR2,   0,  7,  0 },
    { EC_GF_OP_XOR2,   6,  3,  0 },
    { EC_GF_OP_XOR2,   5,  2,  0 },
    { EC_GF_OP_XOR2,   7,  6,  0 },
    { EC_GF_OP_XOR2,   6,  5,  0 },
    { EC_GF_OP_XOR2,   5,  4,  0 },
    { EC_GF_OP_END,    0,  0,  0 }
};

static ec_gf_mul_t ec_gf8_mul_75 = {
    8,
    { 4, 5, 6, 7, 0, 1, 2, 3, },
    ec_gf8_mul_75_ops
};

static ec_gf_op_t ec_gf8_mul_76_ops[] = {
    { EC_GF_OP_XOR2,   7,  3,  0 },
    { EC_GF_OP_XOR2,   6,  1,  0 },
    { EC_GF_OP_XOR2,   2,  7,  0 },
    { EC_GF_OP_XOR3,   8,  6,  2 },
    { EC_GF_OP_XOR2,   0,  5,  0 },
    { EC_GF_OP_XOR2,   2,  4,  0 },
    { EC_GF_OP_XOR2,   4,  0,  0 },
    { EC_GF_OP_XOR2,   0,  8,  0 },
    { EC_GF_OP_XOR2,   3,  4,  0 },
    { EC_GF_OP_XOR2,   1,  4,  0 },
    { EC_GF_OP_XOR2,   7,  0,  0 },
    { EC_GF_OP_XOR2,   5,  3,  0 },
    { EC_GF_OP_END,    0,  0,  0 }
};

static ec_gf_mul_t ec_gf8_mul_76 = {
    9,
    { 2, 3, 0, 6, 5, 1, 7, 8, 4, },
    ec_gf8_mul_76_ops
};

static ec_gf_op_t ec_gf8_mul_77_ops[] = {
    { EC_GF_OP_XOR2,   7,  6,  0 },
    { EC_GF_OP_XOR2,   6,  1,  0 },
    { EC_GF_OP_XOR2,   1,  0,  0 },
    { EC_GF_OP_XOR2,   5,  1,  0 },
    { EC_GF_OP_XOR2,   6,  3,  0 },
    { EC_GF_OP_XOR2,   0,  3,  0 },
    { EC_GF_OP_XOR2,   1,  4,  0 },
    { EC_GF_OP_XOR2,   3,  5,  0 },
    { EC_GF_OP_XOR2,   4,  3,  0 },
    { EC_GF_OP_XOR2,   5,  2,  0 },
    { EC_GF_OP_XOR2,   3,  7,  0 },
    { EC_GF_OP_XOR2,   2,  6,  0 },
    { EC_GF_OP_XOR2,   7,  1,  0 },
    { EC_GF_OP_XOR2,   7,  2,  0 },
    { EC_GF_OP_END,    0,  0,  0 }
};

static ec_gf_mul_t ec_gf8_mul_77 = {
    8,
    { 7, 4, 3, 6, 0, 1, 5, 2, },
    ec_gf8_mul_77_ops
};

static ec_gf_op_t ec_gf8_mul_78_ops[] = {
    { EC_GF_OP_XOR2,   6,  5,  0 },
    { EC_GF_OP_XOR2,   6,  0,  0 },
    { EC_GF_OP_XOR2,   1,  0,  0 },
    { EC_GF_OP_XOR2,   7,  2,  0 },
    { EC_GF_OP_XOR2,   2,  6,  0 },
    { EC_GF_OP_XOR2,   0,  3,  0 },
    { EC_GF_OP_XOR2,   3,  7,  0 },
    { EC_GF_OP_XOR3,   8,  0,  2 },
    { EC_GF_OP_XOR2,   4,  8,  0 },
    { EC_GF_OP_XOR2,   1,  8,  0 },
    { EC_GF_OP_XOR2,   7,  4,  0 },
    { EC_GF_OP_XOR2,   5,  7,  0 },
    { EC_GF_OP_XOR2,   5,  1,  0 },
    { EC_GF_OP_XOR2,   0,  5,  0 },
    { EC_GF_OP_XOR2,   6,  0,  0 },
    { EC_GF_OP_END,    0,  0,  0 }
};

static ec_gf_mul_t ec_gf8_mul_78 = {
    9,
    { 4, 7, 3, 2, 5, 1, 6, 0, 8, },
    ec_gf8_mul_78_ops
};

static ec_gf_op_t ec_gf8_mul_79_ops[] = {
    { EC_GF_OP_XOR2,   7,  3,  0 },
    { EC_GF_OP_XOR3,   8,  4,  7 },
    { EC_GF_OP_XOR2,   0,  8,  0 },
    { EC_GF_OP_XOR2,   2,  1,  0 },
    { EC_GF_OP_XOR2,   6,  8,  0 },
    { EC_GF_OP_XOR2,   2,  0,  0 },
    { EC_GF_OP_XOR2,   1,  6,  0 },
    { EC_GF_OP_XOR2,   3,  2,  0 },
    { EC_GF_OP_XOR2,   5,  1,  0 },
    { EC_GF_OP_XOR2,   6,  3,  0 },
    { EC_GF_OP_XOR2,   3,  5,  0 },
    { EC_GF_OP_XOR2,   4,  3,  0 },
    { EC_GF_OP_XOR2,   1,  4,  0 },
    { EC_GF_OP_XOR2,   0,  1,  0 },
    { EC_GF_OP_END,    0,  0,  0 }
};

static ec_gf_mul_t ec_gf8_mul_79 = {
    9,
    { 4, 5, 7, 3, 1, 6, 2, 0, 8, },
    ec_gf8_mul_79_ops
};

static ec_gf_op_t ec_gf8_mul_7A_ops[] = {
    { EC_GF_OP_XOR2,   2,  1,  0 },
    { EC_GF_OP_XOR2,   3,  2,  0 },
    { EC_GF_OP_XOR2,   4,  0,  0 },
    { EC_GF_OP_XOR2,   4,  3,  0 },
    { EC_GF_OP_XOR2,   5,  4,  0 },
    { EC_GF_OP_XOR2,   6,  5,  0 },
    { EC_GF_OP_XOR2,   7,  6,  0 },
    { EC_GF_OP_XOR2,   0,  7,  0 },
    { EC_GF_OP_XOR2,   2,  7,  0 },
    { EC_GF_OP_XOR2,   1,  0,  0 },
    { EC_GF_OP_XOR2,   4,  0,  0 },
    { EC_GF_OP_END,    0,  0,  0 }
};

static ec_gf_mul_t ec_gf8_mul_7A = {
    8,
    { 1, 2, 3, 4, 5, 6, 7, 0, },
    ec_gf8_mul_7A_ops
};

static ec_gf_op_t ec_gf8_mul_7B_ops[] = {
    { EC_GF_OP_XOR2,   3,  1,  0 },
    { EC_GF_OP_XOR3,   8,  5,  3 },
    { EC_GF_OP_XOR2,   8,  0,  0 },
    { EC_GF_OP_COPY,   9,  4,  0 },
    { EC_GF_OP_XOR2,   8,  2,  0 },
    { EC_GF_OP_XOR2,   4,  6,  0 },
    { EC_GF_OP_XOR2,   4,  8,  0 },
    { EC_GF_OP_XOR2,   7,  4,  0 },
    { EC_GF_OP_XOR2,   5,  4,  0 },
    { EC_GF_OP_XOR2,   0,  4,  0 },
    { EC_GF_OP_XOR2,   2,  7,  0 },
    { EC_GF_OP_XOR3,   4,  1,  9 },
    { EC_GF_OP_XOR2,   6,  7,  0 },
    { EC_GF_OP_XOR2,   1,  7,  0 },
    { EC_GF_OP_XOR2,   4,  2,  0 },
    { EC_GF_OP_END,    0,  0,  0 }
};

static ec_gf_mul_t ec_gf8_mul_7B = {
    10,
    { 1, 2, 3, 4, 8, 5, 6, 0, 7, 9, },
    ec_gf8_mul_7B_ops
};

static ec_gf_op_t ec_gf8_mul_7C_ops[] = {
    { EC_GF_OP_XOR2,   5,  3,  0 },
    { EC_GF_OP_XOR2,   4,  5,  0 },
    { EC_GF_OP_XOR2,   2,  4,  0 },
    { EC_GF_OP_XOR2,   4,  6,  0 },
    { EC_GF_OP_XOR2,   3,  1,  0 },
    { EC_GF_OP_XOR2,   0,  4,  0 },
    { EC_GF_OP_XOR2,   7,  2,  0 },
    { EC_GF_OP_XOR2,   3,  0,  0 },
    { EC_GF_OP_XOR2,   7,  3,  0 },
    { EC_GF_OP_XOR2,   5,  7,  0 },
    { EC_GF_OP_XOR2,   1,  7,  0 },
    { EC_GF_OP_XOR2,   6,  5,  0 },
    { EC_GF_OP_XOR2,   0,  5,  0 },
    { EC_GF_OP_END,    0,  0,  0 }
};

static ec_gf_mul_t ec_gf8_mul_7C = {
    8,
    { 2, 4, 1, 6, 3, 5, 7, 0, },
    ec_gf8_mul_7C_ops
};

static ec_gf_op_t ec_gf8_mul_7D_ops[] = {
    { EC_GF_OP_XOR2,   2,  1,  0 },
    { EC_GF_OP_XOR2,   3,  2,  0 },
    { EC_GF_OP_XOR2,   2,  6,  0 },
    { EC_GF_OP_XOR2,   1,  0,  0 },
    { EC_GF_OP_XOR2,   0,  2,  0 },
    { EC_GF_OP_XOR2,   1,  5,  0 },
    { EC_GF_OP_XOR2,   7,  0,  0 },
    { EC_GF_OP_XOR2,   6,  1,  0 },
    { EC_GF_OP_XOR2,   4,  3,  0 },
    { EC_GF_OP_XOR2,   3,  7,  0 },
    { EC_GF_OP_XOR2,   1,  4,  0 },
    { EC_GF_OP_XOR2,   2,  3,  0 },
    { EC_GF_OP_XOR2,   0,  1,  0 },
    { EC_GF_OP_XOR2,   5,  2,  0 },
    { EC_GF_OP_END,    0,  0,  0 }
};

static ec_gf_mul_t ec_gf8_mul_7D = {
    8,
    { 1, 0, 3, 5, 6, 7, 2, 4, },
    ec_gf8_mul_7D_ops
};

static ec_gf_op_t ec_gf8_mul_7E_ops[] = {
    { EC_GF_OP_XOR2,   0,  5,  0 },
    { EC_GF_OP_COPY,   8,  0,  0 },
    { EC_GF_OP_XOR2,   0,  1,  0 },
    { EC_GF_OP_XOR2,   6,  3,  0 },
    { EC_GF_OP_XOR2,   6,  0,  0 },
    { EC_GF_OP_XOR2,   5,  6,  0 },
    { EC_GF_OP_XOR2,   6,  4,  0 },
    { EC_GF_OP_XOR2,   7,  6,  0 },
    { EC_GF_OP_XOR2,   1,  6,  0 },
    { EC_GF_OP_XOR3,   6,  2,  7 },
    { EC_GF_OP_XOR2,   3,  6,  0 },
    { EC_GF_OP_XOR2,   2,  5,  0 },
    { EC_GF_OP_XOR2,   4,  6,  0 },
    { EC_GF_OP_XOR2,   5,  3,  0 },
    { EC_GF_OP_XOR2,   6,  8,  0 },
    { EC_GF_OP_END,    0,  0,  0 }
};

static ec_gf_mul_t ec_gf8_mul_7E = {
    9,
    { 5, 1, 2, 0, 7, 3, 4, 6, 8, },
    ec_gf8_mul_7E_ops
};

static ec_gf_op_t ec_gf8_mul_7F_ops[] = {
    { EC_GF_OP_COPY,   8,  0,  0 },
    { EC_GF_OP_XOR2,   0,  1,  0 },
    { EC_GF_OP_XOR2,   0,  3,  0 },
    { EC_GF_OP_XOR2,   5,  0,  0 },
    { EC_GF_OP_XOR3,   9,  7,  5 },
    { EC_GF_OP_XOR2,   2,  9,  0 },
    { EC_GF_OP_XOR2,   1,  2,  0 },
    { EC_GF_OP_XOR2,   0,  6,  0 },
    { EC_GF_OP_XOR2,   4,  1,  0 },
    { EC_GF_OP_XOR2,   6,  9,  0 },
    { EC_GF_OP_XOR3,   9,  6,  4 },
    { EC_GF_OP_XOR2,   7,  9,  0 },
    { EC_GF_OP_XOR2,   3,  9,  0 },
    { EC_GF_OP_XOR2,   1,  7,  0 },
    { EC_GF_OP_XOR2,   7,  8,  0 },
    { EC_GF_OP_END,    0,  0,  0 }
};

static ec_gf_mul_t ec_gf8_mul_7F = {
    10,
    { 4, 1, 0, 5, 6, 7, 2, 3, 8, 9, },
    ec_gf8_mul_7F_ops
};

static ec_gf_op_t ec_gf8_mul_80_ops[] = {
    { EC_GF_OP_XOR2,   7,  3,  0 },
    { EC_GF_OP_XOR2,   3,  4,  0 },
    { EC_GF_OP_XOR2,   1,  2,  0 },
    { EC_GF_OP_XOR2,   2,  3,  0 },
    { EC_GF_OP_XOR2,   3,  5,  0 },
    { EC_GF_OP_XOR2,   5,  1,  0 },
    { EC_GF_OP_XOR2,   0,  1,  0 },
    { EC_GF_OP_XOR2,   4,  5,  0 },
    { EC_GF_OP_XOR2,   1,  7,  0 },
    { EC_GF_OP_XOR2,   6,  4,  0 },
    { EC_GF_OP_XOR2,   0,  6,  0 },
    { EC_GF_OP_XOR2,   6,  2,  0 },
    { EC_GF_OP_XOR2,   7,  6,  0 },
    { EC_GF_OP_XOR2,   5,  7,  0 },
    { EC_GF_OP_END,    0,  0,  0 }
};

static ec_gf_mul_t ec_gf8_mul_80 = {
    8,
    { 7, 5, 6, 4, 1, 2, 3, 0, },
    ec_gf8_mul_80_ops
};

static ec_gf_op_t ec_gf8_mul_81_ops[] = {
    { EC_GF_OP_XOR2,   3,  5,  0 },
    { EC_GF_OP_XOR2,   4,  6,  0 },
    { EC_GF_OP_XOR2,   3,  4,  0 },
    { EC_GF_OP_XOR2,   2,  3,  0 },
    { EC_GF_OP_XOR2,   6,  2,  0 },
    { EC_GF_OP_XOR2,   1,  6,  0 },
    { EC_GF_OP_XOR2,   7,  1,  0 },
    { EC_GF_OP_XOR2,   4,  1,  0 },
    { EC_GF_OP_XOR2,   5,  7,  0 },
    { EC_GF_OP_XOR2,   0,  5,  0 },
    { EC_GF_OP_XOR2,   7,  3,  0 },
    { EC_GF_OP_XOR2,   2,  0,  0 },
    { EC_GF_OP_XOR2,   0,  4,  0 },
    { EC_GF_OP_END,    0,  0,  0 }
};

static ec_gf_mul_t ec_gf8_mul_81 = {
    8,
    { 2, 7, 4, 1, 5, 6, 3, 0, },
    ec_gf8_mul_81_ops
};

static ec_gf_op_t ec_gf8_mul_82_ops[] = {
    { EC_GF_OP_XOR2,   7,  6,  0 },
    { EC_GF_OP_XOR2,   6,  1,  0 },
    { EC_GF_OP_COPY,   8,  6,  0 },
    { EC_GF_OP_XOR2,   6,  5,  0 },
    { EC_GF_OP_XOR2,   5,  4,  0 },
    { EC_GF_OP_XOR2,   4,  3,  0 },
    { EC_GF_OP_XOR2,   1,  2,  0 },
    { EC_GF_OP_XOR2,   3,  2,  0 },
    { EC_GF_OP_XOR2,   2,  0,  0 },
    { EC_GF_OP_XOR2,   2,  7,  0 },
    { EC_GF_OP_XOR2,   0,  5,  0 },
    { EC_GF_OP_XOR2,   7,  5,  0 },
    { EC_GF_OP_XOR3,   5,  8,  7 },
    { EC_GF_OP_XOR2,   7,  4,  0 },
    { EC_GF_OP_END,    0,  0,  0 }
};

static ec_gf_mul_t ec_gf8_mul_82 = {
    9,
    { 6, 2, 7, 5, 1, 3, 4, 0, 8, },
    ec_gf8_mul_82_ops
};

static ec_gf_op_t ec_gf8_mul_83_ops[] = {
    { EC_GF_OP_XOR2,   0,  1,  0 },
    { EC_GF_OP_XOR2,   1,  2,  0 },
    { EC_GF_OP_XOR2,   2,  3,  0 },
    { EC_GF_OP_XOR2,   0,  3,  0 },
    { EC_GF_OP_XOR2,   2,  5,  0 },
    { EC_GF_OP_XOR2,   7,  2,  0 },
    { EC_GF_OP_XOR2,   5,  0,  0 },
    { EC_GF_OP_XOR2,   3,  6,  0 },
    { EC_GF_OP_XOR2,   1,  4,  0 },
    { EC_GF_OP_XOR2,   6,  7,  0 },
    { EC_GF_OP_XOR2,   4,  3,  0 },
    { EC_GF_OP_XOR2,   7,  1,  0 },
    { EC_GF_OP_XOR2,   3,  5,  0 },
    { EC_GF_OP_XOR2,   0,  7,  0 },
    { EC_GF_OP_XOR2,   5,  6,  0 },
    { EC_GF_OP_END,    0,  0,  0 }
};

static ec_gf_mul_t ec_gf8_mul_83 = {
    8,
    { 3, 5, 6, 7, 1, 2, 4, 0, },
    ec_gf8_mul_83_ops
};

static ec_gf_op_t ec_gf8_mul_84_ops[] = {
    { EC_GF_OP_XOR2,   3,  5,  0 },
    { EC_GF_OP_XOR2,   7,  5,  0 },
    { EC_GF_OP_XOR2,   5,  6,  0 },
    { EC_GF_OP_XOR2,   6,  2,  0 },
    { EC_GF_OP_XOR2,   4,  6,  0 },
    { EC_GF_OP_XOR2,   2,  0,  0 },
    { EC_GF_OP_XOR2,   2,  4,  0 },
    { EC_GF_OP_XOR2,   4,  7,  0 },
    { EC_GF_OP_XOR2,   7,  1,  0 },
    { EC_GF_OP_XOR2,   1,  3,  0 },
    { EC_GF_OP_XOR2,   0,  1,  0 },
    { EC_GF_OP_XOR2,   1,  5,  0 },
    { EC_GF_OP_XOR2,   5,  4,  0 },
    { EC_GF_OP_END,    0,  0,  0 }
};

static ec_gf_mul_t ec_gf8_mul_84 = {
    8,
    { 7, 6, 0, 4, 1, 5, 3, 2, },
    ec_gf8_mul_84_ops
};

static ec_gf_op_t ec_gf8_mul_85_ops[] = {
    { EC_GF_OP_XOR2,   3,  6,  0 },
    { EC_GF_OP_XOR2,   7,  5,  0 },
    { EC_GF_OP_XOR2,   6,  2,  0 },
    { EC_GF_OP_XOR2,   5,  3,  0 },
    { EC_GF_OP_XOR2,   2,  7,  0 },
    { EC_GF_OP_XOR2,   4,  2,  0 },
    { EC_GF_OP_XOR2,   7,  0,  0 },
    { EC_GF_OP_XOR2,   3,  4,  0 },
    { EC_GF_OP_XOR2,   6,  1,  0 },
    { EC_GF_OP_XOR2,   7,  1,  0 },
    { EC_GF_OP_XOR2,   0,  6,  0 },
    { EC_GF_OP_XOR2,   1,  3,  0 },
    { EC_GF_OP_XOR2,   0,  5,  0 },
    { EC_GF_OP_XOR2,   2,  1,  0 },
    { EC_GF_OP_XOR2,   1,  0,  0 },
    { EC_GF_OP_END,    0,  0,  0 }
};

static ec_gf_mul_t ec_gf8_mul_85 = {
    8,
    { 7, 6, 0, 3, 2, 4, 5, 1, },
    ec_gf8_mul_85_ops
};

static ec_gf_op_t ec_gf8_mul_86_ops[] = {
    { EC_GF_OP_XOR2,   1,  5,  0 },
    { EC_GF_OP_XOR2,   5,  6,  0 },
    { EC_GF_OP_XOR2,   6,  0,  0 },
    { EC_GF_OP_XOR2,   0,  4,  0 },
    { EC_GF_OP_XOR2,   4,  5,  0 },
    { EC_GF_OP_XOR2,   5,  7,  0 },
    { EC_GF_OP_XOR2,   7,  2,  0 },
    { EC_GF_OP_XOR2,   2,  6,  0 },
    { EC_GF_OP_XOR2,   6,  3,  0 },
    { EC_GF_OP_XOR2,   6,  5,  0 },
    { EC_GF_OP_XOR2,   5,  1,  0 },
    { EC_GF_OP_END,    0,  0,  0 }
};

static ec_gf_mul_t ec_gf8_mul_86 = {
    8,
    { 1, 2, 6, 4, 5, 7, 3, 0, },
    ec_gf8_mul_86_ops
};

static ec_gf_op_t ec_gf8_mul_87_ops[] = {
    { EC_GF_OP_XOR2,   1,  0,  0 },
    { EC_GF_OP_COPY,   8,  1,  0 },
    { EC_GF_OP_XOR2,   8,  6,  0 },
    { EC_GF_OP_XOR2,   6,  3,  0 },
    { EC_GF_OP_XOR2,   3,  0,  0 },
    { EC_GF_OP_XOR2,   0,  4,  0 },
    { EC_GF_OP_XOR2,   1,  5,  0 },
    { EC_GF_OP_XOR2,   4,  5,  0 },
    { EC_GF_OP_XOR2,   0,  7,  0 },
    { EC_GF_OP_XOR2,   7,  5,  0 },
    { EC_GF_OP_XOR2,   4,  6,  0 },
    { EC_GF_OP_XOR3,   5,  8,  0 },
    { EC_GF_OP_XOR2,   7,  2,  0 },
    { EC_GF_OP_XOR2,   2,  8,  0 },
    { EC_GF_OP_XOR2,   3,  7,  0 },
    { EC_GF_OP_END,    0,  0,  0 }
};

static ec_gf_mul_t ec_gf8_mul_87 = {
    9,
    { 1, 2, 3, 4, 5, 7, 6, 0, 8, },
    ec_gf8_mul_87_ops
};

static ec_gf_op_t ec_gf8_mul_88_ops[] = {
    { EC_GF_OP_XOR2,   5,  6,  0 },
    { EC_GF_OP_XOR2,   6,  7,  0 },
    { EC_GF_OP_XOR2,   4,  6,  0 },
    { EC_GF_OP_XOR2,   6,  1,  0 },
    { EC_GF_OP_XOR2,   7,  2,  0 },
    { EC_GF_OP_XOR2,   1,  0,  0 },
    { EC_GF_OP_XOR2,   2,  3,  0 },
    { EC_GF_OP_XOR2,   1,  7,  0 },
    { EC_GF_OP_XOR2,   0,  5,  0 },
    { EC_GF_OP_XOR2,   2,  5,  0 },
    { EC_GF_OP_XOR2,   1,  4,  0 },
    { EC_GF_OP_XOR2,   5,  4,  0 },
    { EC_GF_OP_XOR2,   4,  3,  0 },
    { EC_GF_OP_XOR2,   3,  6,  0 },
    { EC_GF_OP_END,    0,  0,  0 }
};

static ec_gf_mul_t ec_gf8_mul_88 = {
    8,
    { 6, 7, 3, 1, 2, 4, 5, 0, },
    ec_gf8_mul_88_ops
};

static ec_gf_op_t ec_gf8_mul_89_ops[] = {
    { EC_GF_OP_XOR2,   7,  2,  0 },
    { EC_GF_OP_XOR2,   5,  1,  0 },
    { EC_GF_OP_XOR2,   1,  7,  0 },
    { EC_GF_OP_XOR2,   2,  0,  0 },
    { EC_GF_OP_XOR2,   5,  0,  0 },
    { EC_GF_OP_XOR2,   6,  1,  0 },
    { EC_GF_OP_XOR2,   2,  6,  0 },
    { EC_GF_OP_XOR3,   8,  5,  2 },
    { EC_GF_OP_XOR2,   4,  8,  0 },
    { EC_GF_OP_XOR2,   6,  3,  0 },
    { EC_GF_OP_XOR2,   0,  8,  0 },
    { EC_GF_OP_XOR2,   3,  4,  0 },
    { EC_GF_OP_XOR2,   7,  3,  0 },
    { EC_GF_OP_XOR2,   5,  7,  0 },
    { EC_GF_OP_END,    0,  0,  0 }
};

static ec_gf_mul_t ec_gf8_mul_89 = {
    9,
    { 2, 1, 6, 5, 7, 3, 4, 0, 8, },
    ec_gf8_mul_89_ops
};

static ec_gf_op_t ec_gf8_mul_8A_ops[] = {
    { EC_GF_OP_XOR2,   2,  0,  0 },
    { EC_GF_OP_XOR2,   5,  0,  0 },
    { EC_GF_OP_XOR2,   1,  6,  0 },
    { EC_GF_OP_XOR2,   3,  6,  0 },
    { EC_GF_OP_XOR2,   0,  1,  0 },
    { EC_GF_OP_XOR2,   6,  5,  0 },
    { EC_GF_OP_XOR2,   2,  7,  0 },
    { EC_GF_OP_XOR2,   4,  7,  0 },
    { EC_GF_OP_XOR2,   6,  2,  0 },
    { EC_GF_OP_XOR2,   7,  3,  0 },
    { EC_GF_OP_XOR2,   0,  4,  0 },
    { EC_GF_OP_END,    0,  0,  0 }
};

static ec_gf_mul_t ec_gf8_mul_8A = {
    8,
    { 1, 2, 3, 0, 6, 7, 4, 5, },
    ec_gf8_mul_8A_ops
};

static ec_gf_op_t ec_gf8_mul_8B_ops[] = {
    { EC_GF_OP_XOR2,   1,  0,  0 },
    { EC_GF_OP_XOR2,   3,  6,  0 },
    { EC_GF_OP_XOR2,   6,  1,  0 },
    { EC_GF_OP_XOR2,   1,  7,  0 },
    { EC_GF_OP_XOR2,   7,  5,  0 },
    { EC_GF_OP_XOR2,   4,  1,  0 },
    { EC_GF_OP_XOR2,   5,  2,  0 },
    { EC_GF_OP_XOR2,   0,  7,  0 },
    { EC_GF_OP_XOR2,   1,  2,  0 },
    { EC_GF_OP_XOR2,   7,  3,  0 },
    { EC_GF_OP_XOR2,   2,  3,  0 },
    { EC_GF_OP_XOR2,   3,  4,  0 },
    { EC_GF_OP_XOR2,   4,  6,  0 },
    { EC_GF_OP_XOR2,   5,  4,  0 },
    { EC_GF_OP_END,    0,  0,  0 }
};

static ec_gf_mul_t ec_gf8_mul_8B = {
    8,
    { 6, 1, 2, 3, 5, 7, 4, 0, },
    ec_gf8_mul_8B_ops
};

static ec_gf_op_t ec_gf8_mul_8C_ops[] = {
    { EC_GF_OP_XOR2,   1,  7,  0 },
    { EC_GF_OP_XOR2,   5,  7,  0 },
    { EC_GF_OP_XOR2,   7,  4,  0 },
    { EC_GF_OP_XOR2,   7,  2,  0 },
    { EC_GF_OP_XOR2,   4,  6,  0 },
    { EC_GF_OP_XOR2,   7,  0,  0 },
    { EC_GF_OP_XOR2,   6,  0,  0 },
    { EC_GF_OP_XOR2,   0,  3,  0 },
    { EC_GF_OP_XOR2,   3,  5,  0 },
    { EC_GF_OP_XOR2,   0,  1,  0 },
    { EC_GF_OP_END,    0,  0,  0 }
};

static ec_gf_mul_t ec_gf8_mul_8C = {
    8,
    { 1, 2, 0, 7, 3, 4, 5, 6, },
    ec_gf8_mul_8C_ops
};

static ec_gf_op_t ec_gf8_mul_8D_ops[] = {
    { EC_GF_OP_XOR2,   7,  0,  0 },
    { EC_GF_OP_XOR2,   0,  5,  0 },
    { EC_GF_OP_XOR2,   5,  4,  0 },
    { EC_GF_OP_XOR2,   5,  6,  0 },
    { EC_GF_OP_XOR2,   3,  7,  0 },
    { EC_GF_OP_XOR2,   6,  7,  0 },
    { EC_GF_OP_XOR2,   7,  1,  0 },
    { EC_GF_OP_XOR2,   4,  3,  0 },
    { EC_GF_OP_XOR2,   1,  2,  0 },
    { EC_GF_OP_XOR2,   2,  4,  0 },
    { EC_GF_OP_XOR2,   3,  1,  0 },
    { EC_GF_OP_XOR2,   4,  0,  0 },
    { EC_GF_OP_XOR2,   0,  6,  0 },
    { EC_GF_OP_END,    0,  0,  0 }
};

static ec_gf_mul_t ec_gf8_mul_8D = {
    8,
    { 7, 1, 3, 2, 4, 5, 0, 6, },
    ec_gf8_mul_8D_ops
};

static ec_gf_op_t ec_gf8_mul_8E_ops[] = {
    { EC_GF_OP_XOR2,   2,  0,  0 },
    { EC_GF_OP_XOR2,   3,  0,  0 },
    { EC_GF_OP_XOR2,   4,  0,  0 },
    { EC_GF_OP_END,    0,  0,  0 }
};

static ec_gf_mul_t ec_gf8_mul_8E = {
    8,
    { 1, 2, 3, 4, 5, 6, 7, 0, },
    ec_gf8_mul_8E_ops
};

static ec_gf_op_t ec_gf8_mul_8F_ops[] = {
    { EC_GF_OP_XOR2,   1,  0,  0 },
    { EC_GF_OP_XOR2,   3,  0,  0 },
    { EC_GF_OP_XOR2,   0,  7,  0 },
    { EC_GF_OP_XOR2,   7,  6,  0 },
    { EC_GF_OP_XOR2,   6,  5,  0 },
    { EC_GF_OP_XOR2,   5,  4,  0 },
    { EC_GF_OP_XOR2,   4,  3,  0 },
    { EC_GF_OP_XOR2,   3,  2,  0 },
    { EC_GF_OP_XOR2,   2,  1,  0 },
    { EC_GF_OP_END,    0,  0,  0 }
};

static ec_gf_mul_t ec_gf8_mul_8F = {
    8,
    { 1, 2, 3, 4, 5, 6, 7, 0, },
    ec_gf8_mul_8F_ops
};

static ec_gf_op_t ec_gf8_mul_90_ops[] = {
    { EC_GF_OP_XOR2,   2,  1,  0 },
    { EC_GF_OP_XOR2,   7,  2,  0 },
    { EC_GF_OP_XOR2,   6,  7,  0 },
    { EC_GF_OP_XOR2,   5,  1,  0 },
    { EC_GF_OP_XOR2,   5,  6,  0 },
    { EC_GF_OP_XOR2,   4,  5,  0 },
    { EC_GF_OP_XOR2,   3,  4,  0 },
    { EC_GF_OP_XOR2,   4,  2,  0 },
    { EC_GF_OP_XOR2,   1,  3,  0 },
    { EC_GF_OP_XOR2,   6,  3,  0 },
    { EC_GF_OP_XOR2,   0,  1,  0 },
    { EC_GF_OP_XOR2,   2,  0,  0 },
    { EC_GF_OP_END,    0,  0,  0 }
};

static ec_gf_mul_t ec_gf8_mul_90 = {
    8,
    { 4, 5, 6, 7, 0, 1, 3, 2, },
    ec_gf8_mul_90_ops
};

static ec_gf_op_t ec_gf8_mul_91_ops[] = {
    { EC_GF_OP_XOR2,   0,  3,  0 },
    { EC_GF_OP_XOR2,   3,  4,  0 },
    { EC_GF_OP_COPY,   9,  1,  0 },
    { EC_GF_OP_COPY,   8,  3,  0 },
    { EC_GF_OP_XOR2,   3,  5,  0 },
    { EC_GF_OP_XOR2,   1,  2,  0 },
    { EC_GF_OP_XOR2,   1,  3,  0 },
    { EC_GF_OP_XOR2,   7,  1,  0 },
    { EC_GF_OP_XOR2,   5,  7,  0 },
    { EC_GF_OP_XOR2,   7,  9,  0 },
    { EC_GF_OP_XOR2,   6,  5,  0 },
    { EC_GF_OP_XOR2,   4,  5,  0 },
    { EC_GF_OP_XOR2,   3,  6,  0 },
    { EC_GF_OP_XOR2,   0,  3,  0 },
    { EC_GF_OP_XOR3,   5,  8,  0 },
    { EC_GF_OP_XOR2,   2,  5,  0 },
    { EC_GF_OP_XOR2,   5,  4,  0 },
    { EC_GF_OP_END,    0,  0,  0 }
};

static ec_gf_mul_t ec_gf8_mul_91 = {
    10,
    { 2, 3, 1, 4, 0, 6, 7, 5, 8, 9, },
    ec_gf8_mul_91_ops
};

static ec_gf_op_t ec_gf8_mul_92_ops[] = {
    { EC_GF_OP_XOR2,   4,  1,  0 },
    { EC_GF_OP_XOR2,   5,  4,  0 },
    { EC_GF_OP_XOR2,   6,  5,  0 },
    { EC_GF_OP_XOR2,   2,  0,  0 },
    { EC_GF_OP_XOR2,   2,  6,  0 },
    { EC_GF_OP_XOR2,   3,  1,  0 },
    { EC_GF_OP_XOR2,   4,  2,  0 },
    { EC_GF_OP_XOR2,   3,  0,  0 },
    { EC_GF_OP_XOR2,   7,  4,  0 },
    { EC_GF_OP_XOR2,   3,  7,  0 },
    { EC_GF_OP_XOR2,   5,  3,  0 },
    { EC_GF_OP_XOR2,   4,  5,  0 },
    { EC_GF_OP_XOR2,   0,  4,  0 },
    { EC_GF_OP_END,    0,  0,  0 }
};

static ec_gf_mul_t ec_gf8_mul_92 = {
    8,
    { 6, 7, 0, 1, 2, 3, 5, 4, },
    ec_gf8_mul_92_ops
};

static ec_gf_op_t ec_gf8_mul_93_ops[] = {
    { EC_GF_OP_XOR2,   2,  7,  0 },
    { EC_GF_OP_XOR2,   4,  2,  0 },
    { EC_GF_OP_XOR2,   1,  3,  0 },
    { EC_GF_OP_XOR2,   3,  4,  0 },
    { EC_GF_OP_XOR2,   0,  2,  0 },
    { EC_GF_OP_XOR2,   5,  3,  0 },
    { EC_GF_OP_XOR2,   6,  1,  0 },
    { EC_GF_OP_XOR2,   0,  5,  0 },
    { EC_GF_OP_XOR2,   2,  6,  0 },
    { EC_GF_OP_XOR2,   6,  0,  0 },
    { EC_GF_OP_XOR2,   4,  6,  0 },
    { EC_GF_OP_XOR2,   7,  4,  0 },
    { EC_GF_OP_END,    0,  0,  0 }
};

static ec_gf_mul_t ec_gf8_mul_93 = {
    8,
    { 6, 4, 5, 1, 7, 2, 3, 0, },
    ec_gf8_mul_93_ops
};

static ec_gf_op_t ec_gf8_mul_94_ops[] = {
    { EC_GF_OP_XOR2,   0,  2,  0 },
    { EC_GF_OP_XOR2,   2,  6,  0 },
    { EC_GF_OP_XOR2,   7,  2,  0 },
    { EC_GF_OP_XOR2,   3,  7,  0 },
    { EC_GF_OP_XOR2,   0,  3,  0 },
    { EC_GF_OP_XOR2,   3,  5,  0 },
    { EC_GF_OP_XOR2,   1,  4,  0 },
    { EC_GF_OP_XOR2,   5,  2,  0 },
    { EC_GF_OP_XOR2,   4,  0,  0 },
    { EC_GF_OP_XOR2,   1,  5,  0 },
    { EC_GF_OP_XOR2,   7,  1,  0 },
    { EC_GF_OP_XOR2,   0,  7,  0 },
    { EC_GF_OP_XOR2,   6,  0,  0 },
    { EC_GF_OP_END,    0,  0,  0 }
};

static ec_gf_mul_t ec_gf8_mul_94 = {
    8,
    { 7, 5, 0, 2, 6, 1, 3, 4, },
    ec_gf8_mul_94_ops
};

static ec_gf_op_t ec_gf8_mul_95_ops[] = {
    { EC_GF_OP_XOR2,   3,  2,  0 },
    { EC_GF_OP_XOR2,   7,  3,  0 },
    { EC_GF_OP_XOR2,   3,  6,  0 },
    { EC_GF_OP_XOR2,   0,  3,  0 },
    { EC_GF_OP_XOR2,   4,  0,  0 },
    { EC_GF_OP_XOR2,   2,  4,  0 },
    { EC_GF_OP_XOR2,   4,  5,  0 },
    { EC_GF_OP_XOR2,   1,  4,  0 },
    { EC_GF_OP_XOR2,   5,  7,  0 },
    { EC_GF_OP_XOR2,   6,  1,  0 },
    { EC_GF_OP_XOR2,   7,  6,  0 },
    { EC_GF_OP_XOR2,   6,  2,  0 },
    { EC_GF_OP_XOR2,   0,  6,  0 },
    { EC_GF_OP_XOR2,   4,  0,  0 },
    { EC_GF_OP_END,    0,  0,  0 }
};

static ec_gf_mul_t ec_gf8_mul_95 = {
    8,
    { 7, 6, 1, 3, 0, 4, 5, 2, },
    ec_gf8_mul_95_ops
};

static ec_gf_op_t ec_gf8_mul_96_ops[] = {
    { EC_GF_OP_XOR2,   5,  1,  0 },
    { EC_GF_OP_XOR2,   4,  5,  0 },
    { EC_GF_OP_XOR2,   5,  6,  0 },
    { EC_GF_OP_XOR2,   6,  7,  0 },
    { EC_GF_OP_XOR3,   8,  0,  4 },
    { EC_GF_OP_XOR2,   3,  6,  0 },
    { EC_GF_OP_XOR2,   7,  8,  0 },
    { EC_GF_OP_XOR2,   0,  1,  0 },
    { EC_GF_OP_XOR2,   8,  3,  0 },
    { EC_GF_OP_XOR2,   3,  2,  0 },
    { EC_GF_OP_XOR2,   1,  8,  0 },
    { EC_GF_OP_XOR2,   2,  5,  0 },
    { EC_GF_OP_XOR2,   5,  8,  0 },
    { EC_GF_OP_XOR2,   0,  2,  0 },
    { EC_GF_OP_END,    0,  0,  0 }
};

static ec_gf_mul_t ec_gf8_mul_96 = {
    9,
    { 4, 0, 1, 6, 7, 2, 3, 5, 8, },
    ec_gf8_mul_96_ops
};

static ec_gf_op_t ec_gf8_mul_97_ops[] = {
    { EC_GF_OP_XOR2,   5,  0,  0 },
    { EC_GF_OP_COPY,   8,  2,  0 },
    { EC_GF_OP_XOR2,   0,  3,  0 },
    { EC_GF_OP_XOR2,   8,  6,  0 },
    { EC_GF_OP_XOR2,   5,  1,  0 },
    { EC_GF_OP_XOR2,   3,  7,  0 },
    { EC_GF_OP_XOR2,   1,  8,  0 },
    { EC_GF_OP_XOR2,   7,  5,  0 },
    { EC_GF_OP_XOR2,   2,  3,  0 },
    { EC_GF_OP_XOR2,   6,  3,  0 },
    { EC_GF_OP_XOR2,   0,  4,  0 },
    { EC_GF_OP_XOR2,   3,  1,  0 },
    { EC_GF_OP_XOR2,   4,  5,  0 },
    { EC_GF_OP_XOR2,   5,  8,  0 },
    { EC_GF_OP_XOR2,   3,  4,  0 },
    { EC_GF_OP_END,    0,  0,  0 }
};

static ec_gf_mul_t ec_gf8_mul_97 = {
    9,
    { 4, 5, 3, 6, 7, 1, 2, 0, 8, },
    ec_gf8_mul_97_ops
};

static ec_gf_op_t ec_gf8_mul_98_ops[] = {
    { EC_GF_OP_XOR2,   5,  7,  0 },
    { EC_GF_OP_XOR2,   4,  1,  0 },
    { EC_GF_OP_XOR2,   2,  5,  0 },
    { EC_GF_OP_XOR2,   4,  7,  0 },
    { EC_GF_OP_XOR2,   1,  2,  0 },
    { EC_GF_OP_XOR2,   3,  4,  0 },
    { EC_GF_OP_XOR2,   0,  1,  0 },
    { EC_GF_OP_XOR2,   4,  6,  0 },
    { EC_GF_OP_XOR2,   5,  3,  0 },
    { EC_GF_OP_XOR2,   6,  0,  0 },
    { EC_GF_OP_XOR2,   1,  4,  0 },
    { EC_GF_OP_XOR2,   0,  5,  0 },
    { EC_GF_OP_XOR2,   7,  0,  0 },
    { EC_GF_OP_XOR2,   0,  1,  0 },
    { EC_GF_OP_END,    0,  0,  0 }
};

static ec_gf_mul_t ec_gf8_mul_98 = {
    8,
    { 4, 2, 3, 6, 7, 5, 1, 0, },
    ec_gf8_mul_98_ops
};

static ec_gf_op_t ec_gf8_mul_99_ops[] = {
    { EC_GF_OP_XOR2,   0,  2,  0 },
    { EC_GF_OP_XOR2,   7,  1,  0 },
    { EC_GF_OP_XOR2,   0,  3,  0 },
    { EC_GF_OP_XOR2,   7,  2,  0 },
    { EC_GF_OP_XOR2,   3,  4,  0 },
    { EC_GF_OP_XOR2,   2,  5,  0 },
    { EC_GF_OP_XOR2,   1,  3,  0 },
    { EC_GF_OP_XOR2,   5,  7,  0 },
    { EC_GF_OP_XOR2,   4,  2,  0 },
    { EC_GF_OP_XOR2,   3,  7,  0 },
    { EC_GF_OP_XOR2,   6,  0,  0 },
    { EC_GF_OP_XOR2,   2,  6,  0 },
    { EC_GF_OP_XOR2,   6,  3,  0 },
    { EC_GF_OP_XOR2,   7,  2,  0 },
    { EC_GF_OP_END,    0,  0,  0 }
};

static ec_gf_mul_t ec_gf8_mul_99 = {
    8,
    { 6, 5, 3, 7, 0, 1, 4, 2, },
    ec_gf8_mul_99_ops
};

static ec_gf_op_t ec_gf8_mul_9A_ops[] = {
    { EC_GF_OP_XOR2,   5,  4,  0 },
    { EC_GF_OP_XOR2,   6,  4,  0 },
    { EC_GF_OP_XOR2,   4,  3,  0 },
    { EC_GF_OP_XOR2,   3,  2,  0 },
    { EC_GF_OP_XOR2,   2,  6,  0 },
    { EC_GF_OP_XOR2,   6,  1,  0 },
    { EC_GF_OP_XOR2,   1,  0,  0 },
    { EC_GF_OP_XOR2,   0,  5,  0 },
    { EC_GF_OP_XOR3,   8,  4,  0 },
    { EC_GF_OP_XOR2,   0,  6,  0 },
    { EC_GF_OP_XOR2,   7,  8,  0 },
    { EC_GF_OP_XOR2,   1,  8,  0 },
    { EC_GF_OP_XOR2,   3,  7,  0 },
    { EC_GF_OP_XOR2,   5,  3,  0 },
    { EC_GF_OP_END,    0,  0,  0 }
};

static ec_gf_mul_t ec_gf8_mul_9A = {
    9,
    { 6, 3, 4, 0, 5, 1, 2, 7, 8, },
    ec_gf8_mul_9A_ops
};

static ec_gf_op_t ec_gf8_mul_9B_ops[] = {
    { EC_GF_OP_XOR2,   6,  0,  0 },
    { EC_GF_OP_XOR2,   7,  2,  0 },
    { EC_GF_OP_COPY,   9,  5,  0 },
    { EC_GF_OP_XOR2,   7,  0,  0 },
    { EC_GF_OP_XOR2,   2,  4,  0 },
    { EC_GF_OP_XOR2,   6,  1,  0 },
    { EC_GF_OP_XOR2,   5,  1,  0 },
    { EC_GF_OP_XOR2,   4,  6,  0 },
    { EC_GF_OP_XOR3,   8,  3,  2 },
    { EC_GF_OP_XOR2,   1,  3,  0 },
    { EC_GF_OP_XOR2,   5,  7,  0 },
    { EC_GF_OP_XOR2,   3,  9,  0 },
    { EC_GF_OP_XOR2,   0,  3,  0 },
    { EC_GF_OP_XOR2,   6,  3,  0 },
    { EC_GF_OP_END,    0,  0,  0 }
};

static ec_gf_mul_t ec_gf8_mul_9B = {
    10,
    { 4, 5, 8, 6, 7, 1, 2, 0, 3, 9, },
    ec_gf8_mul_9B_ops
};

static ec_gf_op_t ec_gf8_mul_9C_ops[] = {
    { EC_GF_OP_XOR2,   3,  0,  0 },
    { EC_GF_OP_XOR2,   3,  6,  0 },
    { EC_GF_OP_XOR2,   7,  3,  0 },
    { EC_GF_OP_XOR2,   4,  7,  0 },
    { EC_GF_OP_XOR2,   1,  4,  0 },
    { EC_GF_OP_XOR2,   3,  1,  0 },
    { EC_GF_OP_XOR2,   2,  5,  0 },
    { EC_GF_OP_XOR2,   5,  3,  0 },
    { EC_GF_OP_XOR2,   6,  2,  0 },
    { EC_GF_OP_XOR2,   0,  2,  0 },
    { EC_GF_OP_END,    0,  0,  0 }
};

static ec_gf_mul_t ec_gf8_mul_9C = {
    8,
    { 3, 2, 1, 0, 4, 5, 6, 7, },
    ec_gf8_mul_9C_ops
};

static ec_gf_op_t ec_gf8_mul_9D_ops[] = {
    { EC_GF_OP_XOR2,   3,  0,  0 },
    { EC_GF_OP_XOR2,   4,  1,  0 },
    { EC_GF_OP_XOR2,   6,  3,  0 },
    { EC_GF_OP_XOR2,   5,  2,  0 },
    { EC_GF_OP_XOR2,   2,  6,  0 },
    { EC_GF_OP_XOR2,   4,  7,  0 },
    { EC_GF_OP_XOR2,   1,  5,  0 },
    { EC_GF_OP_XOR2,   3,  5,  0 },
    { EC_GF_OP_XOR2,   7,  6,  0 },
    { EC_GF_OP_XOR2,   0,  4,  0 },
    { EC_GF_OP_XOR2,   2,  4,  0 },
    { EC_GF_OP_END,    0,  0,  0 }
};

static ec_gf_mul_t ec_gf8_mul_9D = {
    8,
    { 0, 1, 2, 3, 7, 4, 5, 6, },
    ec_gf8_mul_9D_ops
};

static ec_gf_op_t ec_gf8_mul_9E_ops[] = {
    { EC_GF_OP_XOR2,   7,  0,  0 },
    { EC_GF_OP_COPY,   8,  7,  0 },
    { EC_GF_OP_XOR2,   8,  5,  0 },
    { EC_GF_OP_XOR2,   5,  2,  0 },
    { EC_GF_OP_XOR2,   2,  6,  0 },
    { EC_GF_OP_XOR2,   5,  0,  0 },
    { EC_GF_OP_XOR2,   6,  4,  0 },
    { EC_GF_OP_XOR2,   6,  0,  0 },
    { EC_GF_OP_XOR2,   7,  3,  0 },
    { EC_GF_OP_XOR2,   0,  1,  0 },
    { EC_GF_OP_XOR2,   4,  1,  0 },
    { EC_GF_OP_XOR2,   3,  6,  0 },
    { EC_GF_OP_XOR2,   0,  8,  0 },
    { EC_GF_OP_END,    0,  0,  0 }
};

static ec_gf_mul_t ec_gf8_mul_9E = {
    9,
    { 4, 5, 3, 8, 6, 0, 2, 7, 1, },
    ec_gf8_mul_9E_ops
};

static ec_gf_op_t ec_gf8_mul_9F_ops[] = {
    { EC_GF_OP_XOR3,   8,  1,  2 },
    { EC_GF_OP_XOR2,   8,  3,  0 },
    { EC_GF_OP_XOR2,   3,  0,  0 },
    { EC_GF_OP_XOR2,   4,  0,  0 },
    { EC_GF_OP_XOR2,   4,  1,  0 },
    { EC_GF_OP_XOR2,   5,  3,  0 },
    { EC_GF_OP_XOR2,   0,  6,  0 },
    { EC_GF_OP_XOR2,   1,  7,  0 },
    { EC_GF_OP_XOR2,   6,  4,  0 },
    { EC_GF_OP_XOR2,   7,  5,  0 },
    { EC_GF_OP_XOR2,   6,  8,  0 },
    { EC_GF_OP_XOR2,   5,  8,  0 },
    { EC_GF_OP_END,    0,  0,  0 }
};

static ec_gf_mul_t ec_gf8_mul_9F = {
    9,
    { 4, 5, 6, 7, 0, 1, 2, 3, 8, },
    ec_gf8_mul_9F_ops
};

static ec_gf_op_t ec_gf8_mul_A0_ops[] = {
    { EC_GF_OP_XOR2,   6,  1,  0 },
    { EC_GF_OP_XOR2,   5,  6,  0 },
    { EC_GF_OP_XOR2,   6,  7,  0 },
    { EC_GF_OP_XOR2,   0,  1,  0 },
    { EC_GF_OP_XOR2,   4,  6,  0 },
    { EC_GF_OP_XOR2,   1,  2,  0 },
    { EC_GF_OP_XOR2,   1,  4,  0 },
    { EC_GF_OP_XOR2,   4,  3,  0 },
    { EC_GF_OP_XOR2,   3,  5,  0 },
    { EC_GF_OP_XOR2,   2,  3,  0 },
    { EC_GF_OP_XOR2,   5,  1,  0 },
    { EC_GF_OP_XOR2,   7,  2,  0 },
    { EC_GF_OP_XOR2,   2,  0,  0 },
    { EC_GF_OP_XOR2,   0,  5,  0 },
    { EC_GF_OP_END,    0,  0,  0 }
};

static ec_gf_mul_t ec_gf8_mul_A0 = {
    8,
    { 3, 1, 6, 7, 5, 2, 4, 0, },
    ec_gf8_mul_A0_ops
};

static ec_gf_op_t ec_gf8_mul_A1_ops[] = {
    { EC_GF_OP_XOR2,   2,  6,  0 },
    { EC_GF_OP_XOR2,   1,  7,  0 },
    { EC_GF_OP_XOR2,   1,  2,  0 },
    { EC_GF_OP_XOR2,   5,  1,  0 },
    { EC_GF_OP_XOR2,   0,  3,  0 },
    { EC_GF_OP_XOR2,   6,  5,  0 },
    { EC_GF_OP_XOR2,   4,  1,  0 },
    { EC_GF_OP_XOR2,   0,  2,  0 },
    { EC_GF_OP_XOR2,   3,  4,  0 },
    { EC_GF_OP_XOR3,   8,  0,  6 },
    { EC_GF_OP_XOR2,   2,  3,  0 },
    { EC_GF_OP_XOR2,   7,  8,  0 },
    { EC_GF_OP_XOR2,   3,  8,  0 },
    { EC_GF_OP_END,    0,  0,  0 }
};

static ec_gf_mul_t ec_gf8_mul_A1 = {
    9,
    { 7, 4, 1, 5, 6, 0, 2, 3, 8, },
    ec_gf8_mul_A1_ops
};

static ec_gf_op_t ec_gf8_mul_A2_ops[] = {
    { EC_GF_OP_XOR2,   2,  1,  0 },
    { EC_GF_OP_XOR2,   2,  4,  0 },
    { EC_GF_OP_XOR2,   3,  5,  0 },
    { EC_GF_OP_XOR2,   1,  6,  0 },
    { EC_GF_OP_XOR2,   2,  3,  0 },
    { EC_GF_OP_XOR2,   3,  1,  0 },
    { EC_GF_OP_XOR2,   0,  2,  0 },
    { EC_GF_OP_XOR2,   7,  3,  0 },
    { EC_GF_OP_XOR2,   1,  0,  0 },
    { EC_GF_OP_XOR2,   0,  7,  0 },
    { EC_GF_OP_XOR2,   4,  7,  0 },
    { EC_GF_OP_XOR2,   5,  0,  0 },
    { EC_GF_OP_END,    0,  0,  0 }
};

static ec_gf_mul_t ec_gf8_mul_A2 = {
    8,
    { 7, 0, 6, 3, 2, 1, 4, 5, },
    ec_gf8_mul_A2_ops
};

static ec_gf_op_t ec_gf8_mul_A3_ops[] = {
    { EC_GF_OP_COPY,   8,  2,  0 },
    { EC_GF_OP_XOR2,   2,  6,  0 },
    { EC_GF_OP_XOR2,   0,  2,  0 },
    { EC_GF_OP_XOR2,   4,  0,  0 },
    { EC_GF_OP_XOR2,   1,  5,  0 },
    { EC_GF_OP_XOR2,   5,  4,  0 },
    { EC_GF_OP_XOR2,   6,  1,  0 },
    { EC_GF_OP_XOR2,   4,  3,  0 },
    { EC_GF_OP_XOR2,   7,  1,  0 },
    { EC_GF_OP_XOR2,   3,  8,  0 },
    { EC_GF_OP_XOR2,   0,  7,  0 },
    { EC_GF_OP_XOR2,   1,  3,  0 },
    { EC_GF_OP_XOR2,   7,  5,  0 },
    { EC_GF_OP_XOR2,   3,  0,  0 },
    { EC_GF_OP_XOR2,   0,  4,  0 },
    { EC_GF_OP_END,    0,  0,  0 }
};

static ec_gf_mul_t ec_gf8_mul_A3 = {
    9,
    { 3, 7, 2, 6, 1, 4, 0, 5, 8, },
    ec_gf8_mul_A3_ops
};

static ec_gf_op_t ec_gf8_mul_A4_ops[] = {
    { EC_GF_OP_XOR2,   0,  1,  0 },
    { EC_GF_OP_XOR2,   4,  2,  0 },
    { EC_GF_OP_XOR2,   5,  3,  0 },
    { EC_GF_OP_XOR2,   7,  0,  0 },
    { EC_GF_OP_XOR2,   2,  5,  0 },
    { EC_GF_OP_XOR2,   6,  4,  0 },
    { EC_GF_OP_XOR2,   5,  1,  0 },
    { EC_GF_OP_XOR2,   4,  7,  0 },
    { EC_GF_OP_XOR2,   3,  6,  0 },
    { EC_GF_OP_XOR2,   1,  4,  0 },
    { EC_GF_OP_XOR2,   3,  1,  0 },
    { EC_GF_OP_XOR2,   0,  3,  0 },
    { EC_GF_OP_XOR2,   3,  2,  0 },
    { EC_GF_OP_XOR2,   4,  3,  0 },
    { EC_GF_OP_END,    0,  0,  0 }
};

static ec_gf_mul_t ec_gf8_mul_A4 = {
    8,
    { 5, 6, 7, 2, 4, 3, 0, 1, },
    ec_gf8_mul_A4_ops
};

static ec_gf_op_t ec_gf8_mul_A5_ops[] = {
    { EC_GF_OP_XOR2,   7,  1,  0 },
    { EC_GF_OP_XOR2,   1,  5,  0 },
    { EC_GF_OP_XOR2,   7,  0,  0 },
    { EC_GF_OP_XOR2,   3,  0,  0 },
    { EC_GF_OP_XOR2,   6,  1,  0 },
    { EC_GF_OP_XOR2,   5,  2,  0 },
    { EC_GF_OP_XOR2,   0,  2,  0 },
    { EC_GF_OP_XOR2,   1,  3,  0 },
    { EC_GF_OP_XOR3,   8,  5,  6 },
    { EC_GF_OP_XOR2,   2,  7,  0 },
    { EC_GF_OP_XOR2,   0,  4,  0 },
    { EC_GF_OP_XOR2,   3,  7,  0 },
    { EC_GF_OP_XOR2,   4,  8,  0 },
    { EC_GF_OP_XOR2,   7,  8,  0 },
    { EC_GF_OP_END,    0,  0,  0 }
};

static ec_gf_mul_t ec_gf8_mul_A5 = {
    9,
    { 1, 4, 2, 5, 6, 7, 3, 0, 8, },
    ec_gf8_mul_A5_ops
};

static ec_gf_op_t ec_gf8_mul_A6_ops[] = {
    { EC_GF_OP_XOR2,   2,  0,  0 },
    { EC_GF_OP_XOR2,   4,  6,  0 },
    { EC_GF_OP_XOR2,   3,  5,  0 },
    { EC_GF_OP_XOR2,   2,  4,  0 },
    { EC_GF_OP_XOR2,   3,  7,  0 },
    { EC_GF_OP_XOR2,   7,  2,  0 },
    { EC_GF_OP_XOR2,   1,  3,  0 },
    { EC_GF_OP_XOR2,   5,  7,  0 },
    { EC_GF_OP_XOR2,   6,  1,  0 },
    { EC_GF_OP_XOR2,   4,  1,  0 },
    { EC_GF_OP_END,    0,  0,  0 }
};

static ec_gf_mul_t ec_gf8_mul_A6 = {
    8,
    { 1, 2, 0, 3, 4, 5, 6, 7, },
    ec_gf8_mul_A6_ops
};

static ec_gf_op_t ec_gf8_mul_A7_ops[] = {
    { EC_GF_OP_XOR2,   2,  0,  0 },
    { EC_GF_OP_XOR2,   3,  1,  0 },
    { EC_GF_OP_XOR2,   5,  7,  0 },
    { EC_GF_OP_XOR2,   4,  2,  0 },
    { EC_GF_OP_XOR2,   3,  5,  0 },
    { EC_GF_OP_XOR2,   4,  6,  0 },
    { EC_GF_OP_XOR2,   0,  3,  0 },
    { EC_GF_OP_XOR2,   6,  3,  0 },
    { EC_GF_OP_XOR2,   1,  4,  0 },
    { EC_GF_OP_XOR2,   7,  4,  0 },
    { EC_GF_OP_END,    0,  0,  0 }
};

static ec_gf_mul_t ec_gf8_mul_A7 = {
    8,
    { 0, 1, 2, 5, 6, 7, 3, 4, },
    ec_gf8_mul_A7_ops
};

static ec_gf_op_t ec_gf8_mul_A8_ops[] = {
    { EC_GF_OP_XOR2,   0,  2,  0 },
    { EC_GF_OP_XOR2,   0,  7,  0 },
    { EC_GF_OP_COPY,   8,  0,  0 },
    { EC_GF_OP_XOR2,   8,  1,  0 },
    { EC_GF_OP_XOR2,   1,  6,  0 },
    { EC_GF_OP_COPY,   9,  4,  0 },
    { EC_GF_OP_XOR2,   0,  5,  0 },
    { EC_GF_OP_XOR2,   4,  1,  0 },
    { EC_GF_OP_XOR2,   5,  1,  0 },
    { EC_GF_OP_XOR2,   8,  3,  0 },
    { EC_GF_OP_XOR2,   1,  3,  0 },
    { EC_GF_OP_XOR2,   3,  2,  0 },
    { EC_GF_OP_XOR2,   2,  9,  0 },
    { EC_GF_OP_XOR2,   3,  0,  0 },
    { EC_GF_OP_XOR2,   7,  2,  0 },
    { EC_GF_OP_XOR2,   6,  2,  0 },
    { EC_GF_OP_END,    0,  0,  0 }
};

static ec_gf_mul_t ec_gf8_mul_A8 = {
    10,
    { 1, 7, 5, 8, 6, 3, 4, 0, 2, 9, },
    ec_gf8_mul_A8_ops
};

static ec_gf_op_t ec_gf8_mul_A9_ops[] = {
    { EC_GF_OP_XOR2,   4,  1,  0 },
    { EC_GF_OP_XOR2,   1,  0,  0 },
    { EC_GF_OP_XOR2,   5,  0,  0 },
    { EC_GF_OP_XOR2,   0,  3,  0 },
    { EC_GF_OP_XOR2,   0,  7,  0 },
    { EC_GF_OP_XOR2,   5,  2,  0 },
    { EC_GF_OP_XOR2,   7,  2,  0 },
    { EC_GF_OP_XOR2,   2,  6,  0 },
    { EC_GF_OP_XOR2,   6,  1,  0 },
    { EC_GF_OP_XOR2,   1,  7,  0 },
    { EC_GF_OP_XOR2,   3,  6,  0 },
    { EC_GF_OP_XOR2,   7,  4,  0 },
    { EC_GF_OP_XOR2,   6,  5,  0 },
    { EC_GF_OP_END,    0,  0,  0 }
};

static ec_gf_mul_t ec_gf8_mul_A9 = {
    8,
    { 3, 7, 6, 1, 2, 0, 4, 5, },
    ec_gf8_mul_A9_ops
};

static ec_gf_op_t ec_gf8_mul_AA_ops[] = {
    { EC_GF_OP_XOR2,   5,  6,  0 },
    { EC_GF_OP_XOR2,   3,  0,  0 },
    { EC_GF_OP_XOR2,   4,  5,  0 },
    { EC_GF_OP_XOR2,   6,  3,  0 },
    { EC_GF_OP_XOR2,   5,  7,  0 },
    { EC_GF_OP_XOR2,   2,  0,  0 },
    { EC_GF_OP_XOR2,   7,  6,  0 },
    { EC_GF_OP_XOR2,   2,  5,  0 },
    { EC_GF_OP_XOR2,   0,  1,  0 },
    { EC_GF_OP_XOR2,   3,  1,  0 },
    { EC_GF_OP_XOR2,   0,  7,  0 },
    { EC_GF_OP_XOR2,   1,  4,  0 },
    { EC_GF_OP_XOR2,   7,  4,  0 },
    { EC_GF_OP_XOR2,   4,  2,  0 },
    { EC_GF_OP_XOR2,   6,  4,  0 },
    { EC_GF_OP_END,    0,  0,  0 }
};

static ec_gf_mul_t ec_gf8_mul_AA = {
    8,
    { 0, 4, 5, 3, 6, 7, 1, 2, },
    ec_gf8_mul_AA_ops
};

static ec_gf_op_t ec_gf8_mul_AB_ops[] = {
    { EC_GF_OP_COPY,   8,  0,  0 },
    { EC_GF_OP_XOR2,   0,  1,  0 },
    { EC_GF_OP_XOR2,   1,  4,  0 },
    { EC_GF_OP_XOR2,   1,  5,  0 },
    { EC_GF_OP_XOR2,   6,  1,  0 },
    { EC_GF_OP_XOR2,   2,  0,  0 },
    { EC_GF_OP_COPY,   9,  6,  0 },
    { EC_GF_OP_XOR2,   6,  2,  0 },
    { EC_GF_OP_XOR2,   4,  6,  0 },
    { EC_GF_OP_XOR2,   7,  4,  0 },
    { EC_GF_OP_XOR2,   8,  7,  0 },
    { EC_GF_OP_XOR2,   3,  8,  0 },
    { EC_GF_OP_XOR2,   5,  3,  0 },
    { EC_GF_OP_XOR2,   6,  3,  0 },
    { EC_GF_OP_XOR2,   2,  5,  0 },
    { EC_GF_OP_XOR3,   3,  9,  7 },
    { EC_GF_OP_END,    0,  0,  0 }
};

static ec_gf_mul_t ec_gf8_mul_AB = {
    10,
    { 2, 3, 8, 0, 5, 6, 1, 4, 7, 9, },
    ec_gf8_mul_AB_ops
};

static ec_gf_op_t ec_gf8_mul_AC_ops[] = {
    { EC_GF_OP_XOR2,   5,  0,  0 },
    { EC_GF_OP_XOR2,   0,  2,  0 },
    { EC_GF_OP_XOR2,   2,  4,  0 },
    { EC_GF_OP_XOR2,   4,  7,  0 },
    { EC_GF_OP_XOR2,   7,  0,  0 },
    { EC_GF_OP_XOR2,   0,  3,  0 },
    { EC_GF_OP_XOR2,   0,  6,  0 },
    { EC_GF_OP_XOR2,   3,  1,  0 },
    { EC_GF_OP_XOR2,   6,  1,  0 },
    { EC_GF_OP_XOR2,   1,  5,  0 },
    { EC_GF_OP_END,    0,  0,  0 }
};

static ec_gf_mul_t ec_gf8_mul_AC = {
    8,
    { 3, 2, 1, 0, 4, 5, 6, 7, },
    ec_gf8_mul_AC_ops
};

static ec_gf_op_t ec_gf8_mul_AD_ops[] = {
    { EC_GF_OP_XOR3,   8,  1,  2 },
    { EC_GF_OP_XOR2,   2,  0,  0 },
    { EC_GF_OP_XOR2,   3,  1,  0 },
    { EC_GF_OP_XOR2,   5,  0,  0 },
    { EC_GF_OP_XOR2,   3,  0,  0 },
    { EC_GF_OP_XOR2,   4,  8,  0 },
    { EC_GF_OP_XOR2,   5,  8,  0 },
    { EC_GF_OP_XOR2,   6,  2,  0 },
    { EC_GF_OP_END,    0,  0,  0 }
};

static ec_gf_mul_t ec_gf8_mul_AD = {
    9,
    { 3, 4, 5, 6, 7, 0, 1, 2, 8, },
    ec_gf8_mul_AD_ops
};

static ec_gf_op_t ec_gf8_mul_AE_ops[] = {
    { EC_GF_OP_XOR2,   6,  5,  0 },
    { EC_GF_OP_XOR2,   5,  0,  0 },
    { EC_GF_OP_COPY,   8,  5,  0 },
    { EC_GF_OP_XOR2,   5,  7,  0 },
    { EC_GF_OP_XOR2,   7,  1,  0 },
    { EC_GF_OP_XOR2,   0,  2,  0 },
    { EC_GF_OP_XOR2,   1,  6,  0 },
    { EC_GF_OP_XOR2,   7,  3,  0 },
    { EC_GF_OP_XOR2,   6,  5,  0 },
    { EC_GF_OP_XOR2,   2,  6,  0 },
    { EC_GF_OP_XOR2,   0,  4,  0 },
    { EC_GF_OP_XOR2,   6,  3,  0 },
    { EC_GF_OP_XOR2,   3,  4,  0 },
    { EC_GF_OP_XOR2,   4,  8,  0 },
    { EC_GF_OP_END,    0,  0,  0 }
};

static ec_gf_mul_t ec_gf8_mul_AE = {
    9,
    { 7, 0, 5, 6, 3, 4, 1, 2, 8, },
    ec_gf8_mul_AE_ops
};

static ec_gf_op_t ec_gf8_mul_AF_ops[] = {
    { EC_GF_OP_XOR2,   4,  0,  0 },
    { EC_GF_OP_XOR2,   6,  0,  0 },
    { EC_GF_OP_XOR2,   0,  1,  0 },
    { EC_GF_OP_XOR2,   0,  7,  0 },
    { EC_GF_OP_XOR2,   5,  1,  0 },
    { EC_GF_OP_XOR2,   7,  6,  0 },
    { EC_GF_OP_XOR2,   1,  2,  0 },
    { EC_GF_OP_XOR2,   6,  2,  0 },
    { EC_GF_OP_XOR2,   2,  5,  0 },
    { EC_GF_OP_XOR2,   1,  4,  0 },
    { EC_GF_OP_XOR2,   2,  0,  0 },
    { EC_GF_OP_XOR2,   0,  3,  0 },
    { EC_GF_OP_END,    0,  0,  0 }
};

static ec_gf_mul_t ec_gf8_mul_AF = {
    8,
    { 0, 1, 2, 7, 3, 4, 5, 6, },
    ec_gf8_mul_AF_ops
};

static ec_gf_op_t ec_gf8_mul_B0_ops[] = {
    { EC_GF_OP_XOR2,   4,  1,  0 },
    { EC_GF_OP_XOR2,   3,  6,  0 },
    { EC_GF_OP_XOR2,   7,  4,  0 },
    { EC_GF_OP_XOR2,   4,  3,  0 },
    { EC_GF_OP_XOR2,   2,  4,  0 },
    { EC_GF_OP_XOR2,   1,  0,  0 },
    { EC_GF_OP_XOR2,   4,  5,  0 },
    { EC_GF_OP_XOR2,   6,  2,  0 },
    { EC_GF_OP_XOR2,   1,  6,  0 },
    { EC_GF_OP_XOR2,   0,  4,  0 },
    { EC_GF_OP_XOR2,   5,  1,  0 },
    { EC_GF_OP_XOR2,   1,  7,  0 },
    { EC_GF_OP_XOR2,   0,  1,  0 },
    { EC_GF_OP_XOR2,   3,  1,  0 },
    { EC_GF_OP_END,    0,  0,  0 }
};

static ec_gf_mul_t ec_gf8_mul_B0 = {
    8,
    { 4, 0, 7, 2, 3, 1, 6, 5, },
    ec_gf8_mul_B0_ops
};

static ec_gf_op_t ec_gf8_mul_B1_ops[] = {
    { EC_GF_OP_XOR2,   4,  1,  0 },
    { EC_GF_OP_COPY,   8,  4,  0 },
    { EC_GF_OP_XOR2,   2,  7,  0 },
    { EC_GF_OP_XOR2,   4,  2,  0 },
    { EC_GF_OP_XOR2,   2,  3,  0 },
    { EC_GF_OP_XOR2,   6,  4,  0 },
    { EC_GF_OP_XOR2,   1,  0,  0 },
    { EC_GF_OP_XOR2,   2,  5,  0 },
    { EC_GF_OP_XOR2,   0,  6,  0 },
    { EC_GF_OP_XOR2,   7,  6,  0 },
    { EC_GF_OP_XOR2,   1,  2,  0 },
    { EC_GF_OP_XOR2,   6,  5,  0 },
    { EC_GF_OP_XOR2,   3,  7,  0 },
    { EC_GF_OP_XOR2,   2,  0,  0 },
    { EC_GF_OP_XOR3,   5,  8,  1 },
    { EC_GF_OP_END,    0,  0,  0 }
};

static ec_gf_mul_t ec_gf8_mul_B1 = {
    9,
    { 2, 6, 4, 7, 0, 1, 3, 5, 8, },
    ec_gf8_mul_B1_ops
};

static ec_gf_op_t ec_gf8_mul_B2_ops[] = {
    { EC_GF_OP_XOR2,   1,  3,  0 },
    { EC_GF_OP_XOR2,   2,  1,  0 },
    { EC_GF_OP_XOR2,   0,  2,  0 },
    { EC_GF_OP_XOR2,   6,  0,  0 },
    { EC_GF_OP_XOR2,   1,  6,  0 },
    { EC_GF_OP_XOR3,   8,  4,  5 },
    { EC_GF_OP_XOR2,   2,  8,  0 },
    { EC_GF_OP_XOR2,   8,  1,  0 },
    { EC_GF_OP_XOR2,   7,  8,  0 },
    { EC_GF_OP_XOR2,   3,  8,  0 },
    { EC_GF_OP_XOR2,   0,  7,  0 },
    { EC_GF_OP_XOR2,   5,  0,  0 },
    { EC_GF_OP_XOR2,   1,  5,  0 },
    { EC_GF_OP_END,    0,  0,  0 }
};

static ec_gf_mul_t ec_gf8_mul_B2 = {
    9,
    { 0, 7, 4, 5, 6, 1, 2, 3, 8, },
    ec_gf8_mul_B2_ops
};

static ec_gf_op_t ec_gf8_mul_B3_ops[] = {
    { EC_GF_OP_XOR2,   5,  0,  0 },
    { EC_GF_OP_COPY,   9,  5,  0 },
    { EC_GF_OP_XOR2,   4,  2,  0 },
    { EC_GF_OP_XOR3,   8,  6,  4 },
    { EC_GF_OP_XOR2,   5,  3,  0 },
    { EC_GF_OP_XOR2,   8,  5,  0 },
    { EC_GF_OP_XOR2,   0,  1,  0 },
    { EC_GF_OP_XOR2,   7,  8,  0 },
    { EC_GF_OP_XOR2,   0,  8,  0 },
    { EC_GF_OP_XOR2,   1,  7,  0 },
    { EC_GF_OP_XOR2,   2,  1,  0 },
    { EC_GF_OP_XOR2,   6,  1,  0 },
    { EC_GF_OP_XOR2,   3,  1,  0 },
    { EC_GF_OP_XOR2,   5,  2,  0 },
    { EC_GF_OP_XOR3,   1,  9,  0 },
    { EC_GF_OP_END,    0,  0,  0 }
};

static ec_gf_mul_t ec_gf8_mul_B3 = {
    10,
    { 2, 3, 4, 5, 1, 6, 0, 7, 8, 9, },
    ec_gf8_mul_B3_ops
};

static ec_gf_op_t ec_gf8_mul_B4_ops[] = {
    { EC_GF_OP_XOR2,   3,  1,  0 },
    { EC_GF_OP_XOR2,   4,  3,  0 },
    { EC_GF_OP_XOR2,   1,  0,  0 },
    { EC_GF_OP_XOR2,   3,  2,  0 },
    { EC_GF_OP_XOR2,   2,  1,  0 },
    { EC_GF_OP_XOR2,   5,  4,  0 },
    { EC_GF_OP_XOR2,   4,  2,  0 },
    { EC_GF_OP_XOR2,   6,  3,  0 },
    { EC_GF_OP_XOR2,   7,  4,  0 },
    { EC_GF_OP_XOR2,   0,  7,  0 },
    { EC_GF_OP_XOR2,   7,  6,  0 },
    { EC_GF_OP_XOR2,   6,  5,  0 },
    { EC_GF_OP_END,    0,  0,  0 }
};

static ec_gf_mul_t ec_gf8_mul_B4 = {
    8,
    { 5, 6, 7, 0, 1, 2, 3, 4, },
    ec_gf8_mul_B4_ops
};

static ec_gf_op_t ec_gf8_mul_B5_ops[] = {
    { EC_GF_OP_XOR2,   1,  0,  0 },
    { EC_GF_OP_XOR2,   0,  2,  0 },
    { EC_GF_OP_XOR2,   0,  3,  0 },
    { EC_GF_OP_XOR2,   6,  0,  0 },
    { EC_GF_OP_COPY,   8,  6,  0 },
    { EC_GF_OP_XOR2,   6,  1,  0 },
    { EC_GF_OP_XOR2,   1,  4,  0 },
    { EC_GF_OP_XOR2,   4,  2,  0 },
    { EC_GF_OP_XOR2,   5,  1,  0 },
    { EC_GF_OP_XOR2,   7,  4,  0 },
    { EC_GF_OP_XOR2,   3,  5,  0 },
    { EC_GF_OP_XOR2,   0,  7,  0 },
    { EC_GF_OP_XOR2,   5,  4,  0 },
    { EC_GF_OP_XOR2,   2,  0,  0 },
    { EC_GF_OP_XOR3,   4,  8,  3 },
    { EC_GF_OP_XOR2,   0,  6,  0 },
    { EC_GF_OP_END,    0,  0,  0 }
};

static ec_gf_mul_t ec_gf8_mul_B5 = {
    9,
    { 3, 4, 0, 7, 1, 5, 6, 2, 8, },
    ec_gf8_mul_B5_ops
};

static ec_gf_op_t ec_gf8_mul_B6_ops[] = {
    { EC_GF_OP_XOR2,   7,  1,  0 },
    { EC_GF_OP_XOR2,   0,  4,  0 },
    { EC_GF_OP_XOR2,   1,  2,  0 },
    { EC_GF_OP_XOR2,   4,  3,  0 },
    { EC_GF_OP_XOR2,   2,  3,  0 },
    { EC_GF_OP_XOR2,   7,  4,  0 },
    { EC_GF_OP_XOR2,   3,  5,  0 },
    { EC_GF_OP_XOR2,   5,  7,  0 },
    { EC_GF_OP_XOR2,   6,  0,  0 },
    { EC_GF_OP_XOR2,   7,  0,  0 },
    { EC_GF_OP_XOR2,   0,  1,  0 },
    { EC_GF_OP_XOR2,   2,  6,  0 },
    { EC_GF_OP_XOR2,   1,  3,  0 },
    { EC_GF_OP_XOR2,   3,  2,  0 },
    { EC_GF_OP_END,    0,  0,  0 }
};

static ec_gf_mul_t ec_gf8_mul_B6 = {
    8,
    { 5, 3, 6, 4, 7, 0, 1, 2, },
    ec_gf8_mul_B6_ops
};

static ec_gf_op_t ec_gf8_mul_B7_ops[] = {
    { EC_GF_OP_XOR2,   2,  1,  0 },
    { EC_GF_OP_XOR2,   0,  4,  0 },
    { EC_GF_OP_XOR2,   2,  6,  0 },
    { EC_GF_OP_XOR2,   0,  2,  0 },
    { EC_GF_OP_XOR2,   1,  0,  0 },
    { EC_GF_OP_XOR2,   0,  5,  0 },
    { EC_GF_OP_XOR2,   7,  1,  0 },
    { EC_GF_OP_XOR2,   3,  7,  0 },
    { EC_GF_OP_XOR2,   6,  0,  0 },
    { EC_GF_OP_XOR2,   2,  3,  0 },
    { EC_GF_OP_XOR2,   5,  2,  0 },
    { EC_GF_OP_XOR2,   7,  5,  0 },
    { EC_GF_OP_END,    0,  0,  0 }
};

static ec_gf_mul_t ec_gf8_mul_B7 = {
    8,
    { 5, 0, 1, 4, 2, 6, 7, 3, },
    ec_gf8_mul_B7_ops
};

static ec_gf_op_t ec_gf8_mul_B8_ops[] = {
    { EC_GF_OP_XOR2,   2,  5,  0 },
    { EC_GF_OP_XOR2,   7,  2,  0 },
    { EC_GF_OP_XOR2,   2,  0,  0 },
    { EC_GF_OP_XOR2,   0,  1,  0 },
    { EC_GF_OP_XOR2,   6,  3,  0 },
    { EC_GF_OP_XOR2,   3,  2,  0 },
    { EC_GF_OP_XOR2,   1,  4,  0 },
    { EC_GF_OP_XOR2,   0,  6,  0 },
    { EC_GF_OP_XOR2,   4,  7,  0 },
    { EC_GF_OP_XOR2,   5,  1,  0 },
    { EC_GF_OP_XOR2,   6,  1,  0 },
    { EC_GF_OP_XOR2,   7,  5,  0 },
    { EC_GF_OP_XOR2,   1,  3,  0 },
    { EC_GF_OP_END,    0,  0,  0 }
};

static ec_gf_mul_t ec_gf8_mul_B8 = {
    8,
    { 6, 4, 5, 1, 2, 0, 7, 3, },
    ec_gf8_mul_B8_ops
};

static ec_gf_op_t ec_gf8_mul_B9_ops[] = {
    { EC_GF_OP_COPY,   8,  0,  0 },
    { EC_GF_OP_XOR2,   0,  1,  0 },
    { EC_GF_OP_XOR2,   2,  5,  0 },
    { EC_GF_OP_XOR2,   0,  4,  0 },
    { EC_GF_OP_XOR2,   2,  0,  0 },
    { EC_GF_OP_XOR2,   3,  0,  0 },
    { EC_GF_OP_XOR3,   0,  8,  2 },
    { EC_GF_OP_XOR2,   6,  3,  0 },
    { EC_GF_OP_XOR2,   1,  2,  0 },
    { EC_GF_OP_XOR2,   7,  0,  0 },
    { EC_GF_OP_XOR2,   5,  6,  0 },
    { EC_GF_OP_XOR2,   3,  7,  0 },
    { EC_GF_OP_XOR2,   4,  5,  0 },
    { EC_GF_OP_XOR2,   5,  3,  0 },
    { EC_GF_OP_END,    0,  0,  0 }
};

static ec_gf_mul_t ec_gf8_mul_B9 = {
    9,
    { 6, 7, 0, 2, 1, 4, 5, 3, 8, },
    ec_gf8_mul_B9_ops
};

static ec_gf_op_t ec_gf8_mul_BA_ops[] = {
    { EC_GF_OP_XOR2,   2,  0,  0 },
    { EC_GF_OP_XOR2,   5,  7,  0 },
    { EC_GF_OP_XOR2,   4,  5,  0 },
    { EC_GF_OP_XOR2,   3,  2,  0 },
    { EC_GF_OP_XOR2,   2,  4,  0 },
    { EC_GF_OP_XOR2,   5,  3,  0 },
    { EC_GF_OP_XOR2,   1,  2,  0 },
    { EC_GF_OP_XOR2,   6,  5,  0 },
    { EC_GF_OP_XOR2,   0,  1,  0 },
    { EC_GF_OP_XOR2,   7,  6,  0 },
    { EC_GF_OP_XOR2,   3,  0,  0 },
    { EC_GF_OP_XOR2,   6,  0,  0 },
    { EC_GF_OP_XOR2,   1,  7,  0 },
    { EC_GF_OP_END,    0,  0,  0 }
};

static ec_gf_mul_t ec_gf8_mul_BA = {
    8,
    { 1, 2, 4, 3, 5, 6, 0, 7, },
    ec_gf8_mul_BA_ops
};

static ec_gf_op_t ec_gf8_mul_BB_ops[] = {
    { EC_GF_OP_XOR2,   3,  6,  0 },
    { EC_GF_OP_COPY,   8,  3,  0 },
    { EC_GF_OP_XOR2,   1,  0,  0 },
    { EC_GF_OP_XOR2,   3,  1,  0 },
    { EC_GF_OP_XOR2,   4,  3,  0 },
    { EC_GF_OP_XOR2,   8,  5,  0 },
    { EC_GF_OP_XOR2,   7,  4,  0 },
    { EC_GF_OP_XOR2,   5,  4,  0 },
    { EC_GF_OP_XOR2,   8,  7,  0 },
    { EC_GF_OP_XOR2,   2,  8,  0 },
    { EC_GF_OP_XOR2,   0,  2,  0 },
    { EC_GF_OP_XOR2,   1,  2,  0 },
    { EC_GF_OP_XOR2,   6,  0,  0 },
    { EC_GF_OP_XOR2,   4,  0,  0 },
    { EC_GF_OP_XOR2,   3,  6,  0 },
    { EC_GF_OP_END,    0,  0,  0 }
};

static ec_gf_mul_t ec_gf8_mul_BB = {
    9,
    { 7, 2, 1, 8, 3, 5, 6, 4, 0, },
    ec_gf8_mul_BB_ops
};

static ec_gf_op_t ec_gf8_mul_BC_ops[] = {
    { EC_GF_OP_COPY,   8,  1,  0 },
    { EC_GF_OP_XOR2,   8,  2,  0 },
    { EC_GF_OP_XOR2,   6,  0,  0 },
    { EC_GF_OP_XOR2,   2,  3,  0 },
    { EC_GF_OP_XOR2,   6,  7,  0 },
    { EC_GF_OP_XOR2,   0,  2,  0 },
    { EC_GF_OP_XOR2,   4,  2,  0 },
    { EC_GF_OP_XOR2,   1,  6,  0 },
    { EC_GF_OP_XOR2,   7,  8,  0 },
    { EC_GF_OP_XOR3,   2,  8,  4 },
    { EC_GF_OP_XOR2,   5,  6,  0 },
    { EC_GF_OP_XOR2,   4,  5,  0 },
    { EC_GF_OP_XOR2,   3,  4,  0 },
    { EC_GF_OP_XOR2,   6,  3,  0 },
    { EC_GF_OP_XOR2,   3,  7,  0 },
    { EC_GF_OP_END,    0,  0,  0 }
};

static ec_gf_mul_t ec_gf8_mul_BC = {
    9,
    { 2, 6, 3, 4, 5, 1, 7, 0, 8, },
    ec_gf8_mul_BC_ops
};

static ec_gf_op_t ec_gf8_mul_BD_ops[] = {
    { EC_GF_OP_XOR2,   3,  0,  0 },
    { EC_GF_OP_XOR2,   3,  1,  0 },
    { EC_GF_OP_XOR2,   1,  2,  0 },
    { EC_GF_OP_XOR2,   7,  1,  0 },
    { EC_GF_OP_XOR2,   1,  4,  0 },
    { EC_GF_OP_XOR2,   4,  3,  0 },
    { EC_GF_OP_XOR2,   5,  1,  0 },
    { EC_GF_OP_XOR2,   3,  7,  0 },
    { EC_GF_OP_XOR2,   0,  6,  0 },
    { EC_GF_OP_XOR2,   0,  5,  0 },
    { EC_GF_OP_XOR2,   6,  7,  0 },
    { EC_GF_OP_XOR2,   7,  0,  0 },
    { EC_GF_OP_XOR2,   2,  7,  0 },
    { EC_GF_OP_XOR2,   1,  2,  0 },
    { EC_GF_OP_END,    0,  0,  0 }
};

static ec_gf_mul_t ec_gf8_mul_BD = {
    8,
    { 4, 5, 0, 2, 7, 1, 6, 3, },
    ec_gf8_mul_BD_ops
};

static ec_gf_op_t ec_gf8_mul_BE_ops[] = {
    { EC_GF_OP_XOR2,   0,  3,  0 },
    { EC_GF_OP_XOR2,   0,  6,  0 },
    { EC_GF_OP_XOR2,   1,  5,  0 },
    { EC_GF_OP_XOR2,   5,  0,  0 },
    { EC_GF_OP_XOR2,   4,  5,  0 },
    { EC_GF_OP_XOR2,   3,  4,  0 },
    { EC_GF_OP_XOR2,   7,  3,  0 },
    { EC_GF_OP_XOR2,   3,  2,  0 },
    { EC_GF_OP_XOR2,   1,  7,  0 },
    { EC_GF_OP_XOR2,   2,  0,  0 },
    { EC_GF_OP_XOR2,   6,  3,  0 },
    { EC_GF_OP_XOR2,   0,  1,  0 },
    { EC_GF_OP_XOR2,   3,  1,  0 },
    { EC_GF_OP_END,    0,  0,  0 }
};

static ec_gf_mul_t ec_gf8_mul_BE = {
    8,
    { 0, 6, 7, 4, 5, 1, 3, 2, },
    ec_gf8_mul_BE_ops
};

static ec_gf_op_t ec_gf8_mul_BF_ops[] = {
    { EC_GF_OP_XOR2,   7,  1,  0 },
    { EC_GF_OP_XOR2,   6,  7,  0 },
    { EC_GF_OP_XOR2,   5,  6,  0 },
    { EC_GF_OP_XOR2,   4,  5,  0 },
    { EC_GF_OP_XOR2,   0,  4,  0 },
    { EC_GF_OP_XOR2,   1,  2,  0 },
    { EC_GF_OP_XOR2,   7,  0,  0 },
    { EC_GF_OP_XOR2,   2,  5,  0 },
    { EC_GF_OP_XOR2,   1,  0,  0 },
    { EC_GF_OP_XOR2,   6,  1,  0 },
    { EC_GF_OP_XOR2,   4,  6,  0 },
    { EC_GF_OP_XOR2,   4,  3,  0 },
    { EC_GF_OP_XOR2,   3,  7,  0 },
    { EC_GF_OP_XOR2,   5,  3,  0 },
    { EC_GF_OP_END,    0,  0,  0 }
};

static ec_gf_mul_t ec_gf8_mul_BF = {
    8,
    { 5, 6, 1, 7, 3, 0, 2, 4, },
    ec_gf8_mul_BF_ops
};

static ec_gf_op_t ec_gf8_mul_C0_ops[] = {
    { EC_GF_OP_XOR2,   4,  1,  0 },
    { EC_GF_OP_XOR2,   6,  3,  0 },
    { EC_GF_OP_XOR2,   7,  4,  0 },
    { EC_GF_OP_XOR2,   4,  6,  0 },
    { EC_GF_OP_XOR2,   5,  2,  0 },
    { EC_GF_OP_XOR2,   2,  6,  0 },
    { EC_GF_OP_XOR2,   3,  5,  0 },
    { EC_GF_OP_XOR2,   6,  0,  0 },
    { EC_GF_OP_XOR2,   1,  5,  0 },
    { EC_GF_OP_XOR2,   3,  7,  0 },
    { EC_GF_OP_XOR2,   0,  7,  0 },
    { EC_GF_OP_END,    0,  0,  0 }
};

static ec_gf_mul_t ec_gf8_mul_C0 = {
    8,
    { 1, 2, 3, 4, 7, 5, 6, 0, },
    ec_gf8_mul_C0_ops
};

static ec_gf_op_t ec_gf8_mul_C1_ops[] = {
    { EC_GF_OP_XOR3,   8,  1,  2 },
    { EC_GF_OP_XOR2,   8,  3,  0 },
    { EC_GF_OP_XOR2,   4,  1,  0 },
    { EC_GF_OP_XOR2,   3,  0,  0 },
    { EC_GF_OP_XOR2,   0,  4,  0 },
    { EC_GF_OP_XOR2,   1,  7,  0 },
    { EC_GF_OP_XOR2,   5,  3,  0 },
    { EC_GF_OP_XOR2,   7,  0,  0 },
    { EC_GF_OP_XOR2,   4,  6,  0 },
    { EC_GF_OP_XOR2,   7,  5,  0 },
    { EC_GF_OP_XOR2,   6,  8,  0 },
    { EC_GF_OP_XOR2,   5,  8,  0 },
    { EC_GF_OP_END,    0,  0,  0 }
};

static ec_gf_mul_t ec_gf8_mul_C1 = {
    9,
    { 5, 6, 7, 4, 1, 2, 3, 0, 8, },
    ec_gf8_mul_C1_ops
};

static ec_gf_op_t ec_gf8_mul_C2_ops[] = {
    { EC_GF_OP_XOR2,   0,  2,  0 },
    { EC_GF_OP_XOR2,   1,  3,  0 },
    { EC_GF_OP_XOR2,   0,  3,  0 },
    { EC_GF_OP_XOR2,   1,  4,  0 },
    { EC_GF_OP_XOR2,   6,  0,  0 },
    { EC_GF_OP_XOR2,   4,  2,  0 },
    { EC_GF_OP_XOR2,   2,  6,  0 },
    { EC_GF_OP_XOR2,   4,  5,  0 },
    { EC_GF_OP_XOR2,   5,  2,  0 },
    { EC_GF_OP_XOR2,   7,  1,  0 },
    { EC_GF_OP_XOR2,   3,  4,  0 },
    { EC_GF_OP_XOR2,   2,  7,  0 },
    { EC_GF_OP_XOR2,   7,  3,  0 },
    { EC_GF_OP_XOR2,   0,  2,  0 },
    { EC_GF_OP_END,    0,  0,  0 }
};

static ec_gf_mul_t ec_gf8_mul_C2 = {
    8,
    { 7, 6, 3, 0, 1, 4, 5, 2, },
    ec_gf8_mul_C2_ops
};

static ec_gf_op_t ec_gf8_mul_C3_ops[] = {
    { EC_GF_OP_COPY,   8,  0,  0 },
    { EC_GF_OP_XOR2,   0,  1,  0 },
    { EC_GF_OP_XOR2,   0,  2,  0 },
    { EC_GF_OP_XOR2,   6,  0,  0 },
    { EC_GF_OP_XOR2,   2,  4,  0 },
    { EC_GF_OP_XOR2,   7,  0,  0 },
    { EC_GF_OP_XOR3,   0,  2,  6 },
    { EC_GF_OP_XOR2,   6,  3,  0 },
    { EC_GF_OP_XOR3,   9,  1,  0 },
    { EC_GF_OP_XOR2,   1,  3,  0 },
    { EC_GF_OP_XOR2,   3,  5,  0 },
    { EC_GF_OP_XOR2,   5,  7,  0 },
    { EC_GF_OP_XOR2,   4,  3,  0 },
    { EC_GF_OP_XOR2,   7,  9,  0 },
    { EC_GF_OP_XOR2,   3,  8,  0 },
    { EC_GF_OP_END,    0,  0,  0 }
};

static ec_gf_mul_t ec_gf8_mul_C3 = {
    10,
    { 5, 6, 4, 7, 1, 2, 3, 0, 8, 9, },
    ec_gf8_mul_C3_ops
};

static ec_gf_op_t ec_gf8_mul_C4_ops[] = {
    { EC_GF_OP_XOR2,   3,  7,  0 },
    { EC_GF_OP_XOR2,   2,  3,  0 },
    { EC_GF_OP_XOR2,   3,  4,  0 },
    { EC_GF_OP_XOR2,   4,  5,  0 },
    { EC_GF_OP_XOR2,   5,  2,  0 },
    { EC_GF_OP_XOR2,   1,  0,  0 },
    { EC_GF_OP_XOR2,   2,  6,  0 },
    { EC_GF_OP_XOR2,   1,  4,  0 },
    { EC_GF_OP_XOR2,   6,  7,  0 },
    { EC_GF_OP_XOR2,   0,  3,  0 },
    { EC_GF_OP_XOR2,   7,  1,  0 },
    { EC_GF_OP_XOR2,   1,  2,  0 },
    { EC_GF_OP_XOR2,   6,  0,  0 },
    { EC_GF_OP_XOR2,   0,  1,  0 },
    { EC_GF_OP_XOR2,   4,  0,  0 },
    { EC_GF_OP_END,    0,  0,  0 }
};

static ec_gf_mul_t ec_gf8_mul_C4 = {
    8,
    { 0, 2, 1, 3, 4, 5, 6, 7, },
    ec_gf8_mul_C4_ops
};

static ec_gf_op_t ec_gf8_mul_C5_ops[] = {
    { EC_GF_OP_XOR2,   0,  4,  0 },
    { EC_GF_OP_XOR2,   5,  0,  0 },
    { EC_GF_OP_XOR2,   6,  1,  0 },
    { EC_GF_OP_XOR2,   0,  3,  0 },
    { EC_GF_OP_XOR2,   1,  5,  0 },
    { EC_GF_OP_XOR2,   6,  2,  0 },
    { EC_GF_OP_XOR2,   3,  7,  0 },
    { EC_GF_OP_XOR2,   5,  6,  0 },
    { EC_GF_OP_XOR2,   7,  4,  0 },
    { EC_GF_OP_XOR2,   2,  3,  0 },
    { EC_GF_OP_XOR2,   4,  5,  0 },
    { EC_GF_OP_XOR2,   3,  6,  0 },
    { EC_GF_OP_XOR2,   5,  2,  0 },
    { EC_GF_OP_END,    0,  0,  0 }
};

static ec_gf_mul_t ec_gf8_mul_C5 = {
    8,
    { 4, 3, 5, 7, 6, 2, 0, 1, },
    ec_gf8_mul_C5_ops
};

static ec_gf_op_t ec_gf8_mul_C6_ops[] = {
    { EC_GF_OP_XOR2,   4,  3,  0 },
    { EC_GF_OP_COPY,   8,  4,  0 },
    { EC_GF_OP_XOR2,   4,  2,  0 },
    { EC_GF_OP_XOR3,   9,  5,  4 },
    { EC_GF_OP_XOR2,   6,  9,  0 },
    { EC_GF_OP_XOR2,   0,  6,  0 },
    { EC_GF_OP_XOR2,   1,  7,  0 },
    { EC_GF_OP_XOR2,   2,  0,  0 },
    { EC_GF_OP_XOR2,   7,  9,  0 },
    { EC_GF_OP_XOR2,   6,  1,  0 },
    { EC_GF_OP_XOR2,   3,  2,  0 },
    { EC_GF_OP_XOR2,   5,  6,  0 },
    { EC_GF_OP_XOR2,   1,  3,  0 },
    { EC_GF_OP_XOR2,   6,  8,  0 },
    { EC_GF_OP_XOR2,   3,  7,  0 },
    { EC_GF_OP_END,    0,  0,  0 }
};

static ec_gf_mul_t ec_gf8_mul_C6 = {
    10,
    { 6, 3, 0, 4, 5, 7, 2, 1, 8, 9, },
    ec_gf8_mul_C6_ops
};

static ec_gf_op_t ec_gf8_mul_C7_ops[] = {
    { EC_GF_OP_XOR2,   5,  0,  0 },
    { EC_GF_OP_XOR2,   5,  3,  0 },
    { EC_GF_OP_XOR2,   2,  4,  0 },
    { EC_GF_OP_XOR2,   4,  5,  0 },
    { EC_GF_OP_XOR2,   1,  3,  0 },
    { EC_GF_OP_XOR2,   6,  4,  0 },
    { EC_GF_OP_XOR2,   7,  2,  0 },
    { EC_GF_OP_XOR2,   1,  6,  0 },
    { EC_GF_OP_XOR2,   3,  7,  0 },
    { EC_GF_OP_XOR2,   7,  1,  0 },
    { EC_GF_OP_XOR2,   5,  7,  0 },
    { EC_GF_OP_XOR2,   0,  5,  0 },
    { EC_GF_OP_END,    0,  0,  0 }
};

static ec_gf_mul_t ec_gf8_mul_C7 = {
    8,
    { 7, 0, 6, 2, 5, 3, 4, 1, },
    ec_gf8_mul_C7_ops
};

static ec_gf_op_t ec_gf8_mul_C8_ops[] = {
    { EC_GF_OP_XOR2,   6,  5,  0 },
    { EC_GF_OP_XOR2,   5,  0,  0 },
    { EC_GF_OP_XOR2,   0,  1,  0 },
    { EC_GF_OP_XOR2,   0,  7,  0 },
    { EC_GF_OP_XOR2,   7,  6,  0 },
    { EC_GF_OP_XOR2,   6,  4,  0 },
    { EC_GF_OP_XOR2,   4,  3,  0 },
    { EC_GF_OP_XOR2,   4,  1,  0 },
    { EC_GF_OP_XOR2,   3,  2,  0 },
    { EC_GF_OP_XOR2,   1,  2,  0 },
    { EC_GF_OP_XOR2,   2,  4,  0 },
    { EC_GF_OP_XOR2,   4,  5,  0 },
    { EC_GF_OP_XOR2,   5,  7,  0 },
    { EC_GF_OP_END,    0,  0,  0 }
};

static ec_gf_mul_t ec_gf8_mul_C8 = {
    8,
    { 1, 3, 2, 4, 6, 7, 5, 0, },
    ec_gf8_mul_C8_ops
};

static ec_gf_op_t ec_gf8_mul_C9_ops[] = {
    { EC_GF_OP_XOR2,   1,  0,  0 },
    { EC_GF_OP_XOR2,   3,  0,  0 },
    { EC_GF_OP_XOR2,   0,  7,  0 },
    { EC_GF_OP_XOR2,   7,  6,  0 },
    { EC_GF_OP_XOR2,   4,  1,  0 },
    { EC_GF_OP_XOR2,   6,  5,  0 },
    { EC_GF_OP_XOR2,   5,  4,  0 },
    { EC_GF_OP_XOR2,   2,  1,  0 },
    { EC_GF_OP_XOR2,   4,  3,  0 },
    { EC_GF_OP_XOR2,   3,  2,  0 },
    { EC_GF_OP_END,    0,  0,  0 }
};

static ec_gf_mul_t ec_gf8_mul_C9 = {
    8,
    { 2, 3, 4, 5, 6, 7, 0, 1, },
    ec_gf8_mul_C9_ops
};

static ec_gf_op_t ec_gf8_mul_CA_ops[] = {
    { EC_GF_OP_XOR2,   6,  7,  0 },
    { EC_GF_OP_XOR2,   7,  2,  0 },
    { EC_GF_OP_XOR2,   5,  6,  0 },
    { EC_GF_OP_XOR2,   2,  0,  0 },
    { EC_GF_OP_XOR2,   4,  5,  0 },
    { EC_GF_OP_XOR2,   2,  3,  0 },
    { EC_GF_OP_XOR2,   6,  1,  0 },
    { EC_GF_OP_XOR2,   3,  4,  0 },
    { EC_GF_OP_XOR2,   1,  7,  0 },
    { EC_GF_OP_XOR2,   6,  0,  0 },
    { EC_GF_OP_XOR2,   7,  3,  0 },
    { EC_GF_OP_XOR2,   0,  5,  0 },
    { EC_GF_OP_XOR2,   5,  7,  0 },
    { EC_GF_OP_XOR2,   7,  6,  0 },
    { EC_GF_OP_END,    0,  0,  0 }
};

static ec_gf_mul_t ec_gf8_mul_CA = {
    8,
    { 1, 2, 5, 7, 3, 4, 0, 6, },
    ec_gf8_mul_CA_ops
};

static ec_gf_op_t ec_gf8_mul_CB_ops[] = {
    { EC_GF_OP_XOR2,   1,  0,  0 },
    { EC_GF_OP_XOR2,   2,  1,  0 },
    { EC_GF_OP_XOR2,   1,  6,  0 },
    { EC_GF_OP_XOR2,   6,  3,  0 },
    { EC_GF_OP_XOR2,   3,  2,  0 },
    { EC_GF_OP_XOR2,   2,  7,  0 },
    { EC_GF_OP_XOR2,   4,  2,  0 },
    { EC_GF_OP_XOR2,   7,  5,  0 },
    { EC_GF_OP_XOR2,   5,  4,  0 },
    { EC_GF_OP_XOR2,   0,  7,  0 },
    { EC_GF_OP_XOR2,   4,  3,  0 },
    { EC_GF_OP_XOR2,   7,  6,  0 },
    { EC_GF_OP_XOR2,   6,  4,  0 },
    { EC_GF_OP_END,    0,  0,  0 }
};

static ec_gf_mul_t ec_gf8_mul_CB = {
    8,
    { 2, 3, 4, 5, 7, 6, 0, 1, },
    ec_gf8_mul_CB_ops
};

static ec_gf_op_t ec_gf8_mul_CC_ops[] = {
    { EC_GF_OP_XOR2,   7,  2,  0 },
    { EC_GF_OP_XOR2,   4,  7,  0 },
    { EC_GF_OP_XOR2,   0,  2,  0 },
    { EC_GF_OP_XOR2,   7,  3,  0 },
    { EC_GF_OP_XOR2,   2,  1,  0 },
    { EC_GF_OP_XOR2,   3,  5,  0 },
    { EC_GF_OP_XOR2,   1,  7,  0 },
    { EC_GF_OP_XOR2,   2,  6,  0 },
    { EC_GF_OP_XOR2,   5,  4,  0 },
    { EC_GF_OP_XOR2,   6,  3,  0 },
    { EC_GF_OP_XOR2,   4,  6,  0 },
    { EC_GF_OP_XOR2,   0,  4,  0 },
    { EC_GF_OP_XOR2,   3,  0,  0 },
    { EC_GF_OP_XOR2,   1,  3,  0 },
    { EC_GF_OP_XOR2,   4,  1,  0 },
    { EC_GF_OP_END,    0,  0,  0 }
};

static ec_gf_mul_t ec_gf8_mul_CC = {
    8,
    { 2, 7, 1, 0, 5, 6, 3, 4, },
    ec_gf8_mul_CC_ops
};

static ec_gf_op_t ec_gf8_mul_CD_ops[] = {
    { EC_GF_OP_XOR2,   4,  0,  0 },
    { EC_GF_OP_XOR2,   3,  6,  0 },
    { EC_GF_OP_XOR2,   0,  1,  0 },
    { EC_GF_OP_XOR2,   6,  2,  0 },
    { EC_GF_OP_XOR2,   1,  3,  0 },
    { EC_GF_OP_XOR2,   2,  5,  0 },
    { EC_GF_OP_XOR2,   1,  4,  0 },
    { EC_GF_OP_XOR2,   5,  0,  0 },
    { EC_GF_OP_XOR2,   4,  7,  0 },
    { EC_GF_OP_XOR2,   0,  6,  0 },
    { EC_GF_OP_XOR2,   7,  2,  0 },
    { EC_GF_OP_XOR2,   6,  4,  0 },
    { EC_GF_OP_XOR2,   2,  6,  0 },
    { EC_GF_OP_XOR2,   6,  1,  0 },
    { EC_GF_OP_END,    0,  0,  0 }
};

static ec_gf_mul_t ec_gf8_mul_CD = {
    8,
    { 0, 6, 1, 2, 7, 3, 4, 5, },
    ec_gf8_mul_CD_ops
};

static ec_gf_op_t ec_gf8_mul_CE_ops[] = {
    { EC_GF_OP_XOR2,   0,  2,  0 },
    { EC_GF_OP_XOR2,   3,  5,  0 },
    { EC_GF_OP_XOR2,   5,  0,  0 },
    { EC_GF_OP_XOR2,   7,  5,  0 },
    { EC_GF_OP_COPY,   8,  7,  0 },
    { EC_GF_OP_XOR2,   7,  3,  0 },
    { EC_GF_OP_XOR2,   3,  4,  0 },
    { EC_GF_OP_XOR2,   6,  3,  0 },
    { EC_GF_OP_XOR2,   2,  3,  0 },
    { EC_GF_OP_XOR3,   3,  6,  8 },
    { EC_GF_OP_XOR2,   0,  6,  0 },
    { EC_GF_OP_XOR3,   8,  2,  3 },
    { EC_GF_OP_XOR2,   1,  8,  0 },
    { EC_GF_OP_XOR2,   4,  8,  0 },
    { EC_GF_OP_XOR2,   5,  1,  0 },
    { EC_GF_OP_END,    0,  0,  0 }
};

static ec_gf_mul_t ec_gf8_mul_CE = {
    9,
    { 5, 7, 3, 0, 2, 6, 4, 1, 8, },
    ec_gf8_mul_CE_ops
};

static ec_gf_op_t ec_gf8_mul_CF_ops[] = {
    { EC_GF_OP_XOR2,   1,  2,  0 },
    { EC_GF_OP_XOR2,   5,  3,  0 },
    { EC_GF_OP_XOR2,   3,  6,  0 },
    { EC_GF_OP_XOR2,   2,  5,  0 },
    { EC_GF_OP_XOR2,   1,  4,  0 },
    { EC_GF_OP_XOR2,   4,  3,  0 },
    { EC_GF_OP_XOR2,   0,  4,  0 },
    { EC_GF_OP_XOR2,   7,  0,  0 },
    { EC_GF_OP_XOR2,   0,  2,  0 },
    { EC_GF_OP_XOR2,   6,  7,  0 },
    { EC_GF_OP_XOR2,   5,  6,  0 },
    { EC_GF_OP_XOR2,   6,  1,  0 },
    { EC_GF_OP_XOR2,   1,  0,  0 },
    { EC_GF_OP_XOR2,   3,  6,  0 },
    { EC_GF_OP_END,    0,  0,  0 }
};

static ec_gf_mul_t ec_gf8_mul_CF = {
    8,
    { 3, 6, 7, 0, 2, 4, 5, 1, },
    ec_gf8_mul_CF_ops
};

static ec_gf_op_t ec_gf8_mul_D0_ops[] = {
    { EC_GF_OP_XOR2,   5,  2,  0 },
    { EC_GF_OP_XOR2,   0,  3,  0 },
    { EC_GF_OP_XOR2,   3,  5,  0 },
    { EC_GF_OP_XOR2,   6,  3,  0 },
    { EC_GF_OP_XOR2,   4,  1,  0 },
    { EC_GF_OP_XOR2,   1,  6,  0 },
    { EC_GF_OP_XOR2,   5,  4,  0 },
    { EC_GF_OP_XOR2,   7,  1,  0 },
    { EC_GF_OP_XOR2,   4,  0,  0 },
    { EC_GF_OP_XOR2,   2,  7,  0 },
    { EC_GF_OP_XOR2,   0,  2,  0 },
    { EC_GF_OP_XOR2,   3,  2,  0 },
    { EC_GF_OP_XOR2,   1,  0,  0 },
    { EC_GF_OP_END,    0,  0,  0 }
};

static ec_gf_mul_t ec_gf8_mul_D0 = {
    8,
    { 5, 6, 7, 2, 0, 3, 1, 4, },
    ec_gf8_mul_D0_ops
};

static ec_gf_op_t ec_gf8_mul_D1_ops[] = {
    { EC_GF_OP_XOR2,   1,  2,  0 },
    { EC_GF_OP_XOR2,   0,  4,  0 },
    { EC_GF_OP_XOR2,   5,  1,  0 },
    { EC_GF_OP_XOR2,   4,  6,  0 },
    { EC_GF_OP_XOR2,   6,  5,  0 },
    { EC_GF_OP_XOR2,   5,  0,  0 },
    { EC_GF_OP_XOR2,   7,  6,  0 },
    { EC_GF_OP_XOR2,   6,  3,  0 },
    { EC_GF_OP_XOR2,   2,  7,  0 },
    { EC_GF_OP_XOR2,   0,  2,  0 },
    { EC_GF_OP_XOR2,   3,  2,  0 },
    { EC_GF_OP_XOR3,   8,  6,  0 },
    { EC_GF_OP_XOR2,   4,  8,  0 },
    { EC_GF_OP_XOR2,   1,  8,  0 },
    { EC_GF_OP_END,    0,  0,  0 }
};

static ec_gf_mul_t ec_gf8_mul_D1 = {
    9,
    { 5, 6, 3, 2, 0, 7, 4, 1, 8, },
    ec_gf8_mul_D1_ops
};

static ec_gf_op_t ec_gf8_mul_D2_ops[] = {
    { EC_GF_OP_XOR2,   3,  5,  0 },
    { EC_GF_OP_XOR2,   3,  6,  0 },
    { EC_GF_OP_XOR2,   2,  3,  0 },
    { EC_GF_OP_XOR2,   3,  0,  0 },
    { EC_GF_OP_XOR2,   0,  2,  0 },
    { EC_GF_OP_XOR2,   3,  1,  0 },
    { EC_GF_OP_XOR2,   7,  0,  0 },
    { EC_GF_OP_XOR2,   4,  3,  0 },
    { EC_GF_OP_XOR2,   1,  2,  0 },
    { EC_GF_OP_XOR2,   6,  7,  0 },
    { EC_GF_OP_XOR2,   5,  4,  0 },
    { EC_GF_OP_XOR2,   4,  6,  0 },
    { EC_GF_OP_XOR2,   7,  5,  0 },
    { EC_GF_OP_END,    0,  0,  0 }
};

static ec_gf_mul_t ec_gf8_mul_D2 = {
    8,
    { 7, 0, 2, 1, 3, 4, 6, 5, },
    ec_gf8_mul_D2_ops
};

static ec_gf_op_t ec_gf8_mul_D3_ops[] = {
    { EC_GF_OP_XOR2,   4,  7,  0 },
    { EC_GF_OP_COPY,   8,  4,  0 },
    { EC_GF_OP_XOR2,   2,  1,  0 },
    { EC_GF_OP_XOR2,   4,  2,  0 },
    { EC_GF_OP_XOR2,   5,  4,  0 },
    { EC_GF_OP_XOR2,   6,  5,  0 },
    { EC_GF_OP_XOR2,   8,  6,  0 },
    { EC_GF_OP_XOR2,   3,  8,  0 },
    { EC_GF_OP_XOR2,   2,  3,  0 },
    { EC_GF_OP_XOR2,   3,  0,  0 },
    { EC_GF_OP_XOR2,   1,  3,  0 },
    { EC_GF_OP_XOR2,   0,  5,  0 },
    { EC_GF_OP_XOR2,   7,  1,  0 },
    { EC_GF_OP_XOR2,   1,  5,  0 },
    { EC_GF_OP_XOR2,   4,  7,  0 },
    { EC_GF_OP_END,    0,  0,  0 }
};

static ec_gf_mul_t ec_gf8_mul_D3 = {
    9,
    { 0, 3, 2, 8, 4, 6, 7, 1, 5, },
    ec_gf8_mul_D3_ops
};

static ec_gf_op_t ec_gf8_mul_D4_ops[] = {
    { EC_GF_OP_XOR2,   1,  5,  0 },
    { EC_GF_OP_COPY,   8,  1,  0 },
    { EC_GF_OP_XOR2,   1,  2,  0 },
    { EC_GF_OP_XOR2,   0,  1,  0 },
    { EC_GF_OP_XOR2,   5,  3,  0 },
    { EC_GF_OP_XOR2,   6,  1,  0 },
    { EC_GF_OP_XOR2,   3,  0,  0 },
    { EC_GF_OP_XOR3,   1,  7,  8 },
    { EC_GF_OP_XOR2,   7,  3,  0 },
    { EC_GF_OP_XOR2,   3,  4,  0 },
    { EC_GF_OP_XOR2,   4,  6,  0 },
    { EC_GF_OP_XOR2,   2,  3,  0 },
    { EC_GF_OP_XOR2,   6,  5,  0 },
    { EC_GF_OP_XOR2,   3,  1,  0 },
    { EC_GF_OP_XOR2,   1,  6,  0 },
    { EC_GF_OP_END,    0,  0,  0 }
};

static ec_gf_mul_t ec_gf8_mul_D4 = {
    9,
    { 4, 1, 7, 5, 0, 6, 3, 2, 8, },
    ec_gf8_mul_D4_ops
};

static ec_gf_op_t ec_gf8_mul_D5_ops[] = {
    { EC_GF_OP_XOR2,   0,  4,  0 },
    { EC_GF_OP_XOR2,   1,  5,  0 },
    { EC_GF_OP_XOR2,   1,  0,  0 },
    { EC_GF_OP_XOR2,   2,  1,  0 },
    { EC_GF_OP_XOR2,   6,  2,  0 },
    { EC_GF_OP_XOR2,   0,  6,  0 },
    { EC_GF_OP_XOR2,   3,  0,  0 },
    { EC_GF_OP_XOR2,   7,  3,  0 },
    { EC_GF_OP_XOR2,   1,  7,  0 },
    { EC_GF_OP_XOR2,   0,  1,  0 },
    { EC_GF_OP_XOR2,   4,  0,  0 },
    { EC_GF_OP_END,    0,  0,  0 }
};

static ec_gf_mul_t ec_gf8_mul_D5 = {
    8,
    { 6, 7, 4, 5, 2, 3, 1, 0, },
    ec_gf8_mul_D5_ops
};

static ec_gf_op_t ec_gf8_mul_D6_ops[] = {
    { EC_GF_OP_COPY,   8,  0,  0 },
    { EC_GF_OP_XOR2,   0,  2,  0 },
    { EC_GF_OP_XOR2,   2,  1,  0 },
    { EC_GF_OP_XOR2,   2,  4,  0 },
    { EC_GF_OP_XOR2,   2,  6,  0 },
    { EC_GF_OP_XOR2,   3,  2,  0 },
    { EC_GF_OP_XOR2,   0,  3,  0 },
    { EC_GF_OP_XOR2,   5,  0,  0 },
    { EC_GF_OP_XOR2,   2,  5,  0 },
    { EC_GF_OP_XOR2,   7,  2,  0 },
    { EC_GF_OP_XOR2,   1,  7,  0 },
    { EC_GF_OP_XOR2,   4,  7,  0 },
    { EC_GF_OP_XOR2,   6,  7,  0 },
    { EC_GF_OP_XOR2,   0,  7,  0 },
    { EC_GF_OP_XOR2,   7,  8,  0 },
    { EC_GF_OP_END,    0,  0,  0 }
};

static ec_gf_mul_t ec_gf8_mul_D6 = {
    9,
    { 0, 6, 2, 7, 1, 3, 4, 5, 8, },
    ec_gf8_mul_D6_ops
};

static ec_gf_op_t ec_gf8_mul_D7_ops[] = {
    { EC_GF_OP_XOR2,   7,  2,  0 },
    { EC_GF_OP_XOR2,   5,  7,  0 },
    { EC_GF_OP_XOR2,   7,  0,  0 },
    { EC_GF_OP_XOR2,   0,  1,  0 },
    { EC_GF_OP_XOR3,   8,  3,  5 },
    { EC_GF_OP_XOR2,   0,  4,  0 },
    { EC_GF_OP_XOR2,   0,  8,  0 },
    { EC_GF_OP_XOR2,   6,  0,  0 },
    { EC_GF_OP_XOR2,   2,  6,  0 },
    { EC_GF_OP_XOR2,   1,  6,  0 },
    { EC_GF_OP_XOR2,   4,  6,  0 },
    { EC_GF_OP_XOR2,   3,  6,  0 },
    { EC_GF_OP_XOR3,   6,  7,  8 },
    { EC_GF_OP_XOR2,   7,  2,  0 },
    { EC_GF_OP_END,    0,  0,  0 }
};

static ec_gf_mul_t ec_gf8_mul_D7 = {
    9,
    { 3, 4, 6, 5, 0, 7, 1, 2, 8, },
    ec_gf8_mul_D7_ops
};

static ec_gf_op_t ec_gf8_mul_D8_ops[] = {
    { EC_GF_OP_XOR2,   3,  2,  0 },
    { EC_GF_OP_XOR2,   4,  1,  0 },
    { EC_GF_OP_XOR2,   5,  3,  0 },
    { EC_GF_OP_XOR2,   4,  2,  0 },
    { EC_GF_OP_XOR2,   3,  1,  0 },
    { EC_GF_OP_XOR2,   2,  0,  0 },
    { EC_GF_OP_XOR2,   6,  3,  0 },
    { EC_GF_OP_XOR2,   3,  2,  0 },
    { EC_GF_OP_XOR2,   7,  3,  0 },
    { EC_GF_OP_END,    0,  0,  0 }
};

static ec_gf_mul_t ec_gf8_mul_D8 = {
    8,
    { 4, 5, 6, 7, 0, 1, 2, 3, },
    ec_gf8_mul_D8_ops
};

static ec_gf_op_t ec_gf8_mul_D9_ops[] = {
    { EC_GF_OP_XOR2,   4,  0,  0 },
    { EC_GF_OP_XOR2,   0,  1,  0 },
    { EC_GF_OP_XOR2,   5,  1,  0 },
    { EC_GF_OP_XOR2,   1,  2,  0 },
    { EC_GF_OP_XOR2,   6,  1,  0 },
    { EC_GF_OP_XOR2,   7,  0,  0 },
    { EC_GF_OP_XOR2,   1,  4,  0 },
    { EC_GF_OP_XOR2,   2,  3,  0 },
    { EC_GF_OP_XOR2,   0,  6,  0 },
    { EC_GF_OP_XOR2,   3,  7,  0 },
    { EC_GF_OP_XOR2,   6,  2,  0 },
    { EC_GF_OP_XOR2,   2,  5,  0 },
    { EC_GF_OP_END,    0,  0,  0 }
};

static ec_gf_mul_t ec_gf8_mul_D9 = {
    8,
    { 1, 2, 6, 7, 4, 5, 0, 3, },
    ec_gf8_mul_D9_ops
};

static ec_gf_op_t ec_gf8_mul_DA_ops[] = {
    { EC_GF_OP_XOR2,   6,  2,  0 },
    { EC_GF_OP_XOR2,   2,  7,  0 },
    { EC_GF_OP_XOR2,   7,  3,  0 },
    { EC_GF_OP_XOR2,   0,  7,  0 },
    { EC_GF_OP_XOR3,   8,  2,  0 },
    { EC_GF_OP_XOR2,   7,  6,  0 },
    { EC_GF_OP_XOR2,   4,  1,  0 },
    { EC_GF_OP_XOR2,   1,  8,  0 },
    { EC_GF_OP_XOR2,   5,  8,  0 },
    { EC_GF_OP_XOR2,   2,  4,  0 },
    { EC_GF_OP_XOR2,   6,  1,  0 },
    { EC_GF_OP_XOR2,   3,  5,  0 },
    { EC_GF_OP_END,    0,  0,  0 }
};

static ec_gf_mul_t ec_gf8_mul_DA = {
    9,
    { 2, 5, 7, 1, 0, 4, 3, 6, 8, },
    ec_gf8_mul_DA_ops
};

static ec_gf_op_t ec_gf8_mul_DB_ops[] = {
    { EC_GF_OP_COPY,   8,  0,  0 },
    { EC_GF_OP_XOR2,   0,  1,  0 },
    { EC_GF_OP_XOR2,   2,  0,  0 },
    { EC_GF_OP_XOR2,   5,  2,  0 },
    { EC_GF_OP_XOR2,   1,  5,  0 },
    { EC_GF_OP_XOR2,   8,  4,  0 },
    { EC_GF_OP_XOR2,   5,  3,  0 },
    { EC_GF_OP_XOR2,   4,  2,  0 },
    { EC_GF_OP_XOR2,   3,  7,  0 },
    { EC_GF_OP_XOR2,   7,  4,  0 },
    { EC_GF_OP_XOR2,   4,  1,  0 },
    { EC_GF_OP_XOR2,   1,  6,  0 },
    { EC_GF_OP_XOR2,   6,  3,  0 },
    { EC_GF_OP_XOR2,   3,  8,  0 },
    { EC_GF_OP_XOR2,   0,  6,  0 },
    { EC_GF_OP_END,    0,  0,  0 }
};

static ec_gf_mul_t ec_gf8_mul_DB = {
    9,
    { 7, 5, 6, 2, 3, 4, 1, 0, 8, },
    ec_gf8_mul_DB_ops
};

static ec_gf_op_t ec_gf8_mul_DC_ops[] = {
    { EC_GF_OP_XOR2,   7,  3,  0 },
    { EC_GF_OP_XOR2,   3,  0,  0 },
    { EC_GF_OP_XOR2,   4,  2,  0 },
    { EC_GF_OP_XOR2,   0,  4,  0 },
    { EC_GF_OP_XOR2,   3,  1,  0 },
    { EC_GF_OP_XOR2,   6,  1,  0 },
    { EC_GF_OP_XOR2,   1,  7,  0 },
    { EC_GF_OP_XOR2,   4,  6,  0 },
    { EC_GF_OP_XOR2,   7,  2,  0 },
    { EC_GF_OP_XOR2,   6,  3,  0 },
    { EC_GF_OP_XOR2,   2,  3,  0 },
    { EC_GF_OP_XOR2,   3,  5,  0 },
    { EC_GF_OP_XOR2,   5,  7,  0 },
    { EC_GF_OP_XOR2,   7,  6,  0 },
    { EC_GF_OP_END,    0,  0,  0 }
};

static ec_gf_mul_t ec_gf8_mul_DC = {
    8,
    { 4, 5, 2, 6, 7, 1, 0, 3, },
    ec_gf8_mul_DC_ops
};

static ec_gf_op_t ec_gf8_mul_DD_ops[] = {
    { EC_GF_OP_XOR2,   3,  1,  0 },
    { EC_GF_OP_XOR2,   3,  0,  0 },
    { EC_GF_OP_XOR2,   5,  7,  0 },
    { EC_GF_OP_XOR2,   5,  3,  0 },
    { EC_GF_OP_XOR2,   4,  2,  0 },
    { EC_GF_OP_XOR2,   6,  0,  0 },
    { EC_GF_OP_XOR2,   0,  5,  0 },
    { EC_GF_OP_XOR2,   4,  6,  0 },
    { EC_GF_OP_XOR2,   2,  0,  0 },
    { EC_GF_OP_XOR2,   1,  4,  0 },
    { EC_GF_OP_XOR2,   7,  4,  0 },
    { EC_GF_OP_END,    0,  0,  0 }
};

static ec_gf_mul_t ec_gf8_mul_DD = {
    8,
    { 1, 2, 3, 6, 7, 0, 4, 5, },
    ec_gf8_mul_DD_ops
};

static ec_gf_op_t ec_gf8_mul_DE_ops[] = {
    { EC_GF_OP_XOR2,   2,  0,  0 },
    { EC_GF_OP_XOR2,   3,  7,  0 },
    { EC_GF_OP_XOR2,   2,  3,  0 },
    { EC_GF_OP_XOR2,   6,  2,  0 },
    { EC_GF_OP_XOR2,   1,  4,  0 },
    { EC_GF_OP_XOR2,   7,  6,  0 },
    { EC_GF_OP_XOR2,   5,  2,  0 },
    { EC_GF_OP_XOR2,   1,  3,  0 },
    { EC_GF_OP_XOR2,   0,  7,  0 },
    { EC_GF_OP_XOR2,   4,  5,  0 },
    { EC_GF_OP_XOR2,   0,  1,  0 },
    { EC_GF_OP_XOR2,   3,  4,  0 },
    { EC_GF_OP_XOR2,   4,  0,  0 },
    { EC_GF_OP_END,    0,  0,  0 }
};

static ec_gf_mul_t ec_gf8_mul_DE = {
    8,
    { 0, 5, 2, 6, 7, 1, 3, 4, },
    ec_gf8_mul_DE_ops
};

static ec_gf_op_t ec_gf8_mul_DF_ops[] = {
    { EC_GF_OP_COPY,   8,  0,  0 },
    { EC_GF_OP_XOR2,   0,  2,  0 },
    { EC_GF_OP_XOR2,   0,  4,  0 },
    { EC_GF_OP_XOR2,   8,  3,  0 },
    { EC_GF_OP_COPY,   9,  0,  0 },
    { EC_GF_OP_XOR2,   0,  6,  0 },
    { EC_GF_OP_XOR2,   8,  7,  0 },
    { EC_GF_OP_XOR2,   3,  0,  0 },
    { EC_GF_OP_XOR2,   7,  0,  0 },
    { EC_GF_OP_XOR2,   0,  5,  0 },
    { EC_GF_OP_XOR2,   4,  7,  0 },
    { EC_GF_OP_XOR2,   5,  1,  0 },
    { EC_GF_OP_XOR2,   7,  1,  0 },
    { EC_GF_OP_XOR2,   5,  8,  0 },
    { EC_GF_OP_XOR2,   2,  5,  0 },
    { EC_GF_OP_XOR2,   6,  5,  0 },
    { EC_GF_OP_XOR3,   1,  9,  2 },
    { EC_GF_OP_END,    0,  0,  0 }
};

static ec_gf_mul_t ec_gf8_mul_DF = {
    10,
    { 7, 2, 8, 4, 3, 1, 0, 6, 5, 9, },
    ec_gf8_mul_DF_ops
};

static ec_gf_op_t ec_gf8_mul_E0_ops[] = {
    { EC_GF_OP_XOR2,   4,  2,  0 },
    { EC_GF_OP_XOR2,   6,  4,  0 },
    { EC_GF_OP_XOR2,   5,  3,  0 },
    { EC_GF_OP_XOR2,   3,  6,  0 },
    { EC_GF_OP_XOR2,   4,  1,  0 },
    { EC_GF_OP_XOR2,   7,  1,  0 },
    { EC_GF_OP_XOR2,   5,  7,  0 },
    { EC_GF_OP_XOR2,   6,  0,  0 },
    { EC_GF_OP_XOR2,   2,  5,  0 },
    { EC_GF_OP_XOR2,   0,  5,  0 },
    { EC_GF_OP_XOR2,   1,  6,  0 },
    { EC_GF_OP_END,    0,  0,  0 }
};

static ec_gf_mul_t ec_gf8_mul_E0 = {
    8,
    { 2, 3, 4, 7, 5, 6, 0, 1, },
    ec_gf8_mul_E0_ops
};

static ec_gf_op_t ec_gf8_mul_E1_ops[] = {
    { EC_GF_OP_COPY,   8,  1,  0 },
    { EC_GF_OP_XOR2,   8,  7,  0 },
    { EC_GF_OP_XOR2,   3,  8,  0 },
    { EC_GF_OP_XOR3,   9,  5,  3 },
    { EC_GF_OP_XOR2,   0,  9,  0 },
    { EC_GF_OP_XOR2,   1,  4,  0 },
    { EC_GF_OP_XOR2,   7,  0,  0 },
    { EC_GF_OP_XOR2,   6,  0,  0 },
    { EC_GF_OP_XOR2,   4,  9,  0 },
    { EC_GF_OP_XOR2,   0,  2,  0 },
    { EC_GF_OP_XOR2,   2,  4,  0 },
    { EC_GF_OP_XOR2,   2,  6,  0 },
    { EC_GF_OP_XOR2,   5,  2,  0 },
    { EC_GF_OP_XOR2,   2,  8,  0 },
    { EC_GF_OP_XOR2,   7,  5,  0 },
    { EC_GF_OP_END,    0,  0,  0 }
};

static ec_gf_mul_t ec_gf8_mul_E1 = {
    10,
    { 0, 7, 1, 3, 4, 5, 6, 2, 8, 9, },
    ec_gf8_mul_E1_ops
};

static ec_gf_op_t ec_gf8_mul_E2_ops[] = {
    { EC_GF_OP_XOR2,   6,  0,  0 },
    { EC_GF_OP_XOR2,   5,  1,  0 },
    { EC_GF_OP_XOR2,   6,  2,  0 },
    { EC_GF_OP_XOR2,   1,  2,  0 },
    { EC_GF_OP_XOR2,   2,  3,  0 },
    { EC_GF_OP_XOR2,   0,  1,  0 },
    { EC_GF_OP_XOR2,   3,  4,  0 },
    { EC_GF_OP_XOR2,   7,  2,  0 },
    { EC_GF_OP_XOR2,   4,  0,  0 },
    { EC_GF_OP_XOR2,   2,  5,  0 },
    { EC_GF_OP_XOR2,   0,  7,  0 },
    { EC_GF_OP_XOR2,   7,  3,  0 },
    { EC_GF_OP_XOR2,   3,  6,  0 },
    { EC_GF_OP_END,    0,  0,  0 }
};

static ec_gf_mul_t ec_gf8_mul_E2 = {
    8,
    { 2, 3, 7, 1, 5, 6, 0, 4, },
    ec_gf8_mul_E2_ops
};

static ec_gf_op_t ec_gf8_mul_E3_ops[] = {
    { EC_GF_OP_XOR2,   7,  4,  0 },
    { EC_GF_OP_XOR2,   3,  1,  0 },
    { EC_GF_OP_XOR3,   8,  2,  7 },
    { EC_GF_OP_XOR2,   4,  5,  0 },
    { EC_GF_OP_XOR2,   2,  3,  0 },
    { EC_GF_OP_XOR2,   5,  0,  0 },
    { EC_GF_OP_XOR2,   5,  2,  0 },
    { EC_GF_OP_XOR2,   0,  1,  0 },
    { EC_GF_OP_XOR2,   6,  5,  0 },
    { EC_GF_OP_XOR2,   1,  4,  0 },
    { EC_GF_OP_XOR2,   0,  8,  0 },
    { EC_GF_OP_XOR2,   4,  6,  0 },
    { EC_GF_OP_XOR2,   3,  6,  0 },
    { EC_GF_OP_XOR3,   6,  8,  4 },
    { EC_GF_OP_END,    0,  0,  0 }
};

static ec_gf_mul_t ec_gf8_mul_E3 = {
    9,
    { 5, 4, 7, 2, 1, 3, 6, 0, 8, },
    ec_gf8_mul_E3_ops
};

static ec_gf_op_t ec_gf8_mul_E4_ops[] = {
    { EC_GF_OP_XOR2,   4,  0,  0 },
    { EC_GF_OP_XOR2,   2,  6,  0 },
    { EC_GF_OP_XOR2,   2,  4,  0 },
    { EC_GF_OP_XOR2,   1,  2,  0 },
    { EC_GF_OP_XOR2,   5,  1,  0 },
    { EC_GF_OP_XOR2,   4,  5,  0 },
    { EC_GF_OP_XOR2,   3,  4,  0 },
    { EC_GF_OP_XOR2,   7,  3,  0 },
    { EC_GF_OP_XOR2,   2,  7,  0 },
    { EC_GF_OP_XOR2,   4,  2,  0 },
    { EC_GF_OP_XOR2,   0,  4,  0 },
    { EC_GF_OP_END,    0,  0,  0 }
};

static ec_gf_mul_t ec_gf8_mul_E4 = {
    8,
    { 7, 0, 1, 6, 3, 4, 2, 5, },
    ec_gf8_mul_E4_ops
};

static ec_gf_op_t ec_gf8_mul_E5_ops[] = {
    { EC_GF_OP_XOR2,   7,  5,  0 },
    { EC_GF_OP_XOR2,   5,  0,  0 },
    { EC_GF_OP_XOR2,   0,  1,  0 },
    { EC_GF_OP_XOR2,   6,  3,  0 },
    { EC_GF_OP_XOR2,   0,  6,  0 },
    { EC_GF_OP_XOR2,   0,  4,  0 },
    { EC_GF_OP_COPY,   8,  0,  0 },
    { EC_GF_OP_XOR2,   0,  7,  0 },
    { EC_GF_OP_XOR2,   2,  0,  0 },
    { EC_GF_OP_XOR2,   5,  2,  0 },
    { EC_GF_OP_XOR2,   1,  2,  0 },
    { EC_GF_OP_XOR2,   4,  2,  0 },
    { EC_GF_OP_XOR2,   7,  5,  0 },
    { EC_GF_OP_XOR2,   2,  3,  0 },
    { EC_GF_OP_XOR2,   3,  8,  0 },
    { EC_GF_OP_END,    0,  0,  0 }
};

static ec_gf_mul_t ec_gf8_mul_E5 = {
    9,
    { 4, 5, 3, 6, 7, 1, 0, 2, 8, },
    ec_gf8_mul_E5_ops
};

static ec_gf_op_t ec_gf8_mul_E6_ops[] = {
    { EC_GF_OP_XOR2,   6,  2,  0 },
    { EC_GF_OP_XOR2,   1,  6,  0 },
    { EC_GF_OP_XOR2,   6,  7,  0 },
    { EC_GF_OP_XOR2,   0,  3,  0 },
    { EC_GF_OP_XOR2,   5,  1,  0 },
    { EC_GF_OP_XOR2,   0,  6,  0 },
    { EC_GF_OP_XOR2,   7,  5,  0 },
    { EC_GF_OP_XOR2,   4,  0,  0 },
    { EC_GF_OP_XOR2,   5,  3,  0 },
    { EC_GF_OP_XOR2,   3,  4,  0 },
    { EC_GF_OP_XOR2,   1,  4,  0 },
    { EC_GF_OP_XOR2,   2,  3,  0 },
    { EC_GF_OP_XOR2,   2,  7,  0 },
    { EC_GF_OP_END,    0,  0,  0 }
};

static ec_gf_mul_t ec_gf8_mul_E6 = {
    8,
    { 5, 4, 3, 6, 7, 0, 1, 2, },
    ec_gf8_mul_E6_ops
};

static ec_gf_op_t ec_gf8_mul_E7_ops[] = {
    { EC_GF_OP_COPY,   8,  6,  0 },
    { EC_GF_OP_XOR2,   3,  2,  0 },
    { EC_GF_OP_XOR2,   6,  7,  0 },
    { EC_GF_OP_XOR2,   6,  3,  0 },
    { EC_GF_OP_XOR3,   9,  0,  6 },
    { EC_GF_OP_XOR2,   4,  9,  0 },
    { EC_GF_OP_XOR2,   5,  9,  0 },
    { EC_GF_OP_XOR2,   3,  4,  0 },
    { EC_GF_OP_XOR2,   7,  5,  0 },
    { EC_GF_OP_XOR2,   4,  1,  0 },
    { EC_GF_OP_XOR2,   2,  4,  0 },
    { EC_GF_OP_XOR2,   1,  7,  0 },
    { EC_GF_OP_XOR2,   7,  2,  0 },
    { EC_GF_OP_XOR2,   0,  7,  0 },
    { EC_GF_OP_XOR2,   7,  8,  0 },
    { EC_GF_OP_END,    0,  0,  0 }
};

static ec_gf_mul_t ec_gf8_mul_E7 = {
    10,
    { 1, 4, 3, 6, 7, 5, 2, 0, 8, 9, },
    ec_gf8_mul_E7_ops
};

static ec_gf_op_t ec_gf8_mul_E8_ops[] = {
    { EC_GF_OP_XOR2,   1,  0,  0 },
    { EC_GF_OP_XOR2,   0,  4,  0 },
    { EC_GF_OP_XOR2,   4,  2,  0 },
    { EC_GF_OP_XOR2,   2,  5,  0 },
    { EC_GF_OP_XOR2,   4,  3,  0 },
    { EC_GF_OP_XOR2,   5,  1,  0 },
    { EC_GF_OP_XOR2,   3,  6,  0 },
    { EC_GF_OP_XOR2,   6,  5,  0 },
    { EC_GF_OP_XOR2,   0,  7,  0 },
    { EC_GF_OP_XOR2,   7,  6,  0 },
    { EC_GF_OP_XOR2,   1,  0,  0 },
    { EC_GF_OP_XOR2,   6,  2,  0 },
    { EC_GF_OP_XOR2,   2,  1,  0 },
    { EC_GF_OP_XOR2,   1,  4,  0 },
    { EC_GF_OP_END,    0,  0,  0 }
};

static ec_gf_mul_t ec_gf8_mul_E8 = {
    8,
    { 1, 4, 2, 7, 3, 0, 5, 6, },
    ec_gf8_mul_E8_ops
};

static ec_gf_op_t ec_gf8_mul_E9_ops[] = {
    { EC_GF_OP_XOR2,   1,  0,  0 },
    { EC_GF_OP_COPY,   8,  1,  0 },
    { EC_GF_OP_XOR2,   1,  6,  0 },
    { EC_GF_OP_XOR2,   6,  3,  0 },
    { EC_GF_OP_XOR2,   4,  6,  0 },
    { EC_GF_OP_XOR2,   2,  1,  0 },
    { EC_GF_OP_XOR2,   3,  7,  0 },
    { EC_GF_OP_XOR2,   0,  4,  0 },
    { EC_GF_OP_XOR2,   7,  2,  0 },
    { EC_GF_OP_XOR2,   5,  1,  0 },
    { EC_GF_OP_XOR2,   2,  0,  0 },
    { EC_GF_OP_XOR2,   6,  7,  0 },
    { EC_GF_OP_XOR2,   3,  5,  0 },
    { EC_GF_OP_XOR2,   0,  3,  0 },
    { EC_GF_OP_XOR3,   1,  8,  0 },
    { EC_GF_OP_END,    0,  0,  0 }
};

static ec_gf_mul_t ec_gf8_mul_E9 = {
    9,
    { 6, 2, 0, 3, 4, 1, 5, 7, 8, },
    ec_gf8_mul_E9_ops
};

static ec_gf_op_t ec_gf8_mul_EA_ops[] = {
    { EC_GF_OP_XOR2,   2,  1,  0 },
    { EC_GF_OP_XOR2,   3,  2,  0 },
    { EC_GF_OP_XOR2,   1,  0,  0 },
    { EC_GF_OP_XOR2,   2,  0,  0 },
    { EC_GF_OP_XOR2,   0,  7,  0 },
    { EC_GF_OP_XOR2,   5,  2,  0 },
    { EC_GF_OP_XOR2,   7,  6,  0 },
    { EC_GF_OP_XOR2,   4,  1,  0 },
    { EC_GF_OP_XOR2,   6,  5,  0 },
    { EC_GF_OP_XOR2,   5,  4,  0 },
    { EC_GF_OP_XOR2,   4,  3,  0 },
    { EC_GF_OP_END,    0,  0,  0 }
};

static ec_gf_mul_t ec_gf8_mul_EA = {
    8,
    { 3, 4, 5, 6, 7, 0, 1, 2, },
    ec_gf8_mul_EA_ops
};

static ec_gf_op_t ec_gf8_mul_EB_ops[] = {
    { EC_GF_OP_XOR2,   1,  0,  0 },
    { EC_GF_OP_XOR2,   2,  1,  0 },
    { EC_GF_OP_XOR2,   3,  2,  0 },
    { EC_GF_OP_XOR2,   2,  7,  0 },
    { EC_GF_OP_XOR2,   7,  5,  0 },
    { EC_GF_OP_XOR2,   5,  4,  0 },
    { EC_GF_OP_XOR2,   0,  7,  0 },
    { EC_GF_OP_XOR2,   1,  6,  0 },
    { EC_GF_OP_XOR2,   4,  3,  0 },
    { EC_GF_OP_XOR2,   6,  5,  0 },
    { EC_GF_OP_XOR2,   7,  6,  0 },
    { EC_GF_OP_XOR2,   6,  4,  0 },
    { EC_GF_OP_END,    0,  0,  0 }
};

static ec_gf_mul_t ec_gf8_mul_EB = {
    8,
    { 3, 4, 5, 6, 7, 0, 1, 2, },
    ec_gf8_mul_EB_ops
};

static ec_gf_op_t ec_gf8_mul_EC_ops[] = {
    { EC_GF_OP_XOR2,   0,  5,  0 },
    { EC_GF_OP_XOR2,   6,  1,  0 },
    { EC_GF_OP_XOR3,   8,  4,  0 },
    { EC_GF_OP_XOR2,   1,  8,  0 },
    { EC_GF_OP_XOR2,   7,  3,  0 },
    { EC_GF_OP_XOR2,   6,  2,  0 },
    { EC_GF_OP_XOR2,   3,  8,  0 },
    { EC_GF_OP_XOR2,   2,  7,  0 },
    { EC_GF_OP_XOR2,   7,  6,  0 },
    { EC_GF_OP_XOR2,   5,  3,  0 },
    { EC_GF_OP_XOR2,   4,  2,  0 },
    { EC_GF_OP_XOR2,   6,  0,  0 },
    { EC_GF_OP_XOR2,   3,  7,  0 },
    { EC_GF_OP_END,    0,  0,  0 }
};

static ec_gf_mul_t ec_gf8_mul_EC = {
    9,
    { 7, 4, 3, 0, 2, 5, 1, 6, 8, },
    ec_gf8_mul_EC_ops
};

static ec_gf_op_t ec_gf8_mul_ED_ops[] = {
    { EC_GF_OP_XOR2,   5,  3,  0 },
    { EC_GF_OP_XOR2,   0,  5,  0 },
    { EC_GF_OP_XOR2,   2,  4,  0 },
    { EC_GF_OP_XOR2,   4,  0,  0 },
    { EC_GF_OP_XOR2,   3,  1,  0 },
    { EC_GF_OP_XOR2,   6,  4,  0 },
    { EC_GF_OP_XOR2,   3,  6,  0 },
    { EC_GF_OP_XOR2,   7,  3,  0 },
    { EC_GF_OP_XOR2,   2,  7,  0 },
    { EC_GF_OP_XOR2,   6,  2,  0 },
    { EC_GF_OP_XOR2,   5,  2,  0 },
    { EC_GF_OP_XOR2,   1,  6,  0 },
    { EC_GF_OP_END,    0,  0,  0 }
};

static ec_gf_mul_t ec_gf8_mul_ED = {
    8,
    { 5, 6, 7, 0, 1, 4, 3, 2, },
    ec_gf8_mul_ED_ops
};

static ec_gf_op_t ec_gf8_mul_EE_ops[] = {
    { EC_GF_OP_XOR2,   5,  3,  0 },
    { EC_GF_OP_XOR2,   3,  0,  0 },
    { EC_GF_OP_XOR2,   0,  1,  0 },
    { EC_GF_OP_XOR3,   8,  2,  3 },
    { EC_GF_OP_XOR2,   0,  4,  0 },
    { EC_GF_OP_XOR2,   4,  8,  0 },
    { EC_GF_OP_XOR2,   6,  4,  0 },
    { EC_GF_OP_XOR2,   8,  5,  0 },
    { EC_GF_OP_XOR2,   4,  7,  0 },
    { EC_GF_OP_XOR2,   5,  6,  0 },
    { EC_GF_OP_XOR2,   1,  8,  0 },
    { EC_GF_OP_XOR2,   7,  8,  0 },
    { EC_GF_OP_XOR2,   6,  0,  0 },
    { EC_GF_OP_END,    0,  0,  0 }
};

static ec_gf_mul_t ec_gf8_mul_EE = {
    9,
    { 6, 4, 5, 7, 2, 3, 0, 1, 8, },
    ec_gf8_mul_EE_ops
};

static ec_gf_op_t ec_gf8_mul_EF_ops[] = {
    { EC_GF_OP_XOR2,   5,  1,  0 },
    { EC_GF_OP_XOR2,   1,  3,  0 },
    { EC_GF_OP_XOR2,   0,  1,  0 },
    { EC_GF_OP_COPY,   8,  0,  0 },
    { EC_GF_OP_XOR2,   8,  2,  0 },
    { EC_GF_OP_XOR2,   0,  5,  0 },
    { EC_GF_OP_XOR2,   2,  4,  0 },
    { EC_GF_OP_XOR2,   7,  8,  0 },
    { EC_GF_OP_XOR2,   3,  2,  0 },
    { EC_GF_OP_XOR2,   6,  8,  0 },
    { EC_GF_OP_XOR2,   4,  7,  0 },
    { EC_GF_OP_XOR2,   3,  6,  0 },
    { EC_GF_OP_XOR2,   7,  5,  0 },
    { EC_GF_OP_XOR2,   5,  3,  0 },
    { EC_GF_OP_XOR2,   1,  7,  0 },
    { EC_GF_OP_END,    0,  0,  0 }
};

static ec_gf_mul_t ec_gf8_mul_EF = {
    9,
    { 6, 4, 5, 7, 2, 0, 3, 1, 8, },
    ec_gf8_mul_EF_ops
};

static ec_gf_op_t ec_gf8_mul_F0_ops[] = {
    { EC_GF_OP_XOR2,   6,  1,  0 },
    { EC_GF_OP_XOR2,   6,  2,  0 },
    { EC_GF_OP_XOR2,   3,  0,  0 },
    { EC_GF_OP_XOR3,   8,  3,  6 },
    { EC_GF_OP_XOR2,   5,  8,  0 },
    { EC_GF_OP_XOR2,   8,  4,  0 },
    { EC_GF_OP_XOR2,   1,  5,  0 },
    { EC_GF_OP_XOR2,   7,  8,  0 },
    { EC_GF_OP_XOR2,   0,  1,  0 },
    { EC_GF_OP_XOR2,   2,  7,  0 },
    { EC_GF_OP_XOR2,   4,  0,  0 },
    { EC_GF_OP_XOR2,   1,  8,  0 },
    { EC_GF_OP_XOR2,   0,  2,  0 },
    { EC_GF_OP_XOR2,   3,  0,  0 },
    { EC_GF_OP_END,    0,  0,  0 }
};

static ec_gf_mul_t ec_gf8_mul_F0 = {
    9,
    { 3, 4, 6, 1, 2, 0, 5, 7, 8, },
    ec_gf8_mul_F0_ops
};

static ec_gf_op_t ec_gf8_mul_F1_ops[] = {
    { EC_GF_OP_XOR2,   3,  1,  0 },
    { EC_GF_OP_XOR2,   3,  5,  0 },
    { EC_GF_OP_COPY,   8,  3,  0 },
    { EC_GF_OP_XOR2,   3,  4,  0 },
    { EC_GF_OP_XOR2,   2,  3,  0 },
    { EC_GF_OP_COPY,   9,  2,  0 },
    { EC_GF_OP_XOR2,   2,  6,  0 },
    { EC_GF_OP_XOR2,   9,  0,  0 },
    { EC_GF_OP_XOR2,   6,  1,  0 },
    { EC_GF_OP_XOR2,   5,  2,  0 },
    { EC_GF_OP_XOR2,   7,  9,  0 },
    { EC_GF_OP_XOR2,   4,  9,  0 },
    { EC_GF_OP_XOR2,   0,  5,  0 },
    { EC_GF_OP_XOR3,   9,  8,  7 },
    { EC_GF_OP_XOR2,   1,  9,  0 },
    { EC_GF_OP_XOR2,   5,  9,  0 },
    { EC_GF_OP_END,    0,  0,  0 }
};

static ec_gf_mul_t ec_gf8_mul_F1 = {
    10,
    { 7, 2, 6, 3, 5, 1, 4, 0, 8, 9, },
    ec_gf8_mul_F1_ops
};

static ec_gf_op_t ec_gf8_mul_F2_ops[] = {
    { EC_GF_OP_XOR2,   0,  1,  0 },
    { EC_GF_OP_XOR2,   5,  4,  0 },
    { EC_GF_OP_XOR2,   1,  5,  0 },
    { EC_GF_OP_XOR2,   7,  2,  0 },
    { EC_GF_OP_XOR2,   0,  6,  0 },
    { EC_GF_OP_XOR2,   6,  7,  0 },
    { EC_GF_OP_XOR2,   4,  0,  0 },
    { EC_GF_OP_XOR2,   2,  3,  0 },
    { EC_GF_OP_XOR2,   7,  1,  0 },
    { EC_GF_OP_XOR3,   8,  6,  4 },
    { EC_GF_OP_XOR2,   1,  2,  0 },
    { EC_GF_OP_XOR2,   2,  0,  0 },
    { EC_GF_OP_XOR2,   3,  8,  0 },
    { EC_GF_OP_XOR2,   5,  8,  0 },
    { EC_GF_OP_XOR2,   0,  1,  0 },
    { EC_GF_OP_END,    0,  0,  0 }
};

static ec_gf_mul_t ec_gf8_mul_F2 = {
    9,
    { 1, 0, 6, 7, 4, 5, 2, 3, 8, },
    ec_gf8_mul_F2_ops
};

static ec_gf_op_t ec_gf8_mul_F3_ops[] = {
    { EC_GF_OP_XOR2,   1,  0,  0 },
    { EC_GF_OP_XOR2,   2,  1,  0 },
    { EC_GF_OP_XOR2,   3,  2,  0 },
    { EC_GF_OP_XOR2,   4,  3,  0 },
    { EC_GF_OP_XOR2,   2,  7,  0 },
    { EC_GF_OP_XOR2,   0,  7,  0 },
    { EC_GF_OP_XOR2,   5,  4,  0 },
    { EC_GF_OP_XOR2,   1,  6,  0 },
    { EC_GF_OP_XOR2,   7,  6,  0 },
    { EC_GF_OP_XOR2,   0,  5,  0 },
    { EC_GF_OP_XOR2,   6,  5,  0 },
    { EC_GF_OP_END,    0,  0,  0 }
};

static ec_gf_mul_t ec_gf8_mul_F3 = {
    8,
    { 5, 6, 7, 0, 1, 2, 3, 4, },
    ec_gf8_mul_F3_ops
};

static ec_gf_op_t ec_gf8_mul_F4_ops[] = {
    { EC_GF_OP_XOR2,   1,  0,  0 },
    { EC_GF_OP_XOR2,   2,  1,  0 },
    { EC_GF_OP_XOR2,   3,  2,  0 },
    { EC_GF_OP_XOR2,   4,  3,  0 },
    { EC_GF_OP_XOR2,   5,  4,  0 },
    { EC_GF_OP_XOR2,   6,  5,  0 },
    { EC_GF_OP_XOR2,   7,  6,  0 },
    { EC_GF_OP_XOR2,   0,  7,  0 },
    { EC_GF_OP_XOR2,   1,  7,  0 },
    { EC_GF_OP_XOR2,   3,  7,  0 },
    { EC_GF_OP_END,    0,  0,  0 }
};

static ec_gf_mul_t ec_gf8_mul_F4 = {
    8,
    { 0, 1, 2, 3, 4, 5, 6, 7, },
    ec_gf8_mul_F4_ops
};

static ec_gf_op_t ec_gf8_mul_F5_ops[] = {
    { EC_GF_OP_XOR2,   1,  0,  0 },
    { EC_GF_OP_XOR2,   2,  1,  0 },
    { EC_GF_OP_XOR2,   3,  2,  0 },
    { EC_GF_OP_XOR2,   4,  3,  0 },
    { EC_GF_OP_XOR2,   5,  4,  0 },
    { EC_GF_OP_XOR2,   6,  5,  0 },
    { EC_GF_OP_XOR2,   7,  6,  0 },
    { EC_GF_OP_XOR2,   0,  7,  0 },
    { EC_GF_OP_XOR2,   2,  7,  0 },
    { EC_GF_OP_END,    0,  0,  0 }
};

static ec_gf_mul_t ec_gf8_mul_F5 = {
    8,
    { 7, 0, 1, 2, 3, 4, 5, 6, },
    ec_gf8_mul_F5_ops
};

static ec_gf_op_t ec_gf8_mul_F6_ops[] = {
    { EC_GF_OP_XOR2,   3,  1,  0 },
    { EC_GF_OP_COPY,   8,  3,  0 },
    { EC_GF_OP_XOR2,   3,  5,  0 },
    { EC_GF_OP_XOR2,   2,  0,  0 },
    { EC_GF_OP_COPY,   9,  3,  0 },
    { EC_GF_OP_XOR2,   3,  2,  0 },
    { EC_GF_OP_XOR2,   2,  7,  0 },
    { EC_GF_OP_XOR2,   4,  2,  0 },
    { EC_GF_OP_XOR2,   9,  4,  0 },
    { EC_GF_OP_XOR2,   4,  1,  0 },
    { EC_GF_OP_XOR2,   6,  9,  0 },
    { EC_GF_OP_XOR2,   7,  6,  0 },
    { EC_GF_OP_XOR2,   0,  7,  0 },
    { EC_GF_OP_XOR2,   5,  7,  0 },
    { EC_GF_OP_XOR2,   6,  1,  0 },
    { EC_GF_OP_XOR3,   7,  8,  0 },
    { EC_GF_OP_END,    0,  0,  0 }
};

static ec_gf_mul_t ec_gf8_mul_F6 = {
    10,
    { 0, 6, 2, 7, 4, 3, 5, 9, 1, 8, },
    ec_gf8_mul_F6_ops
};

static ec_gf_op_t ec_gf8_mul_F7_ops[] = {
    { EC_GF_OP_XOR2,   1,  0,  0 },
    { EC_GF_OP_XOR2,   2,  1,  0 },
    { EC_GF_OP_XOR2,   3,  2,  0 },
    { EC_GF_OP_XOR2,   4,  3,  0 },
    { EC_GF_OP_XOR2,   5,  4,  0 },
    { EC_GF_OP_XOR2,   6,  5,  0 },
    { EC_GF_OP_XOR2,   0,  7,  0 },
    { EC_GF_OP_XOR2,   2,  7,  0 },
    { EC_GF_OP_XOR2,   1,  6,  0 },
    { EC_GF_OP_XOR2,   7,  6,  0 },
    { EC_GF_OP_END,    0,  0,  0 }
};

static ec_gf_mul_t ec_gf8_mul_F7 = {
    8,
    { 6, 7, 0, 1, 2, 3, 4, 5, },
    ec_gf8_mul_F7_ops
};

static ec_gf_op_t ec_gf8_mul_F8_ops[] = {
    { EC_GF_OP_XOR2,   4,  0,  0 },
    { EC_GF_OP_XOR2,   3,  5,  0 },
    { EC_GF_OP_XOR2,   6,  4,  0 },
    { EC_GF_OP_XOR2,   2,  0,  0 },
    { EC_GF_OP_XOR2,   4,  3,  0 },
    { EC_GF_OP_XOR2,   1,  6,  0 },
    { EC_GF_OP_XOR2,   2,  4,  0 },
    { EC_GF_OP_XOR2,   5,  1,  0 },
    { EC_GF_OP_XOR2,   7,  2,  0 },
    { EC_GF_OP_XOR2,   7,  5,  0 },
    { EC_GF_OP_XOR2,   3,  7,  0 },
    { EC_GF_OP_XOR2,   6,  7,  0 },
    { EC_GF_OP_XOR2,   0,  3,  0 },
    { EC_GF_OP_END,    0,  0,  0 }
};

static ec_gf_mul_t ec_gf8_mul_F8 = {
    8,
    { 6, 2, 0, 1, 4, 5, 3, 7, },
    ec_gf8_mul_F8_ops
};

static ec_gf_op_t ec_gf8_mul_F9_ops[] = {
    { EC_GF_OP_XOR2,   1,  5,  0 },
    { EC_GF_OP_XOR2,   5,  3,  0 },
    { EC_GF_OP_XOR2,   6,  1,  0 },
    { EC_GF_OP_XOR2,   0,  5,  0 },
    { EC_GF_OP_XOR2,   7,  6,  0 },
    { EC_GF_OP_XOR2,   6,  0,  0 },
    { EC_GF_OP_XOR2,   2,  6,  0 },
    { EC_GF_OP_XOR2,   6,  4,  0 },
    { EC_GF_OP_XOR2,   1,  2,  0 },
    { EC_GF_OP_XOR2,   3,  6,  0 },
    { EC_GF_OP_XOR3,   8,  7,  1 },
    { EC_GF_OP_XOR2,   1,  3,  0 },
    { EC_GF_OP_XOR2,   4,  8,  0 },
    { EC_GF_OP_XOR2,   5,  8,  0 },
    { EC_GF_OP_END,    0,  0,  0 }
};

static ec_gf_mul_t ec_gf8_mul_F9 = {
    9,
    { 4, 1, 7, 6, 0, 3, 5, 2, 8, },
    ec_gf8_mul_F9_ops
};

static ec_gf_op_t ec_gf8_mul_FA_ops[] = {
    { EC_GF_OP_XOR2,   1,  0,  0 },
    { EC_GF_OP_XOR2,   0,  4,  0 },
    { EC_GF_OP_XOR2,   2,  1,  0 },
    { EC_GF_OP_XOR2,   0,  7,  0 },
    { EC_GF_OP_XOR2,   7,  2,  0 },
    { EC_GF_OP_XOR2,   1,  5,  0 },
    { EC_GF_OP_XOR2,   3,  7,  0 },
    { EC_GF_OP_XOR2,   5,  0,  0 },
    { EC_GF_OP_XOR2,   7,  6,  0 },
    { EC_GF_OP_XOR2,   0,  3,  0 },
    { EC_GF_OP_XOR2,   6,  1,  0 },
    { EC_GF_OP_XOR2,   4,  7,  0 },
    { EC_GF_OP_XOR2,   1,  0,  0 },
    { EC_GF_OP_XOR2,   2,  6,  0 },
    { EC_GF_OP_END,    0,  0,  0 }
};

static ec_gf_mul_t ec_gf8_mul_FA = {
    8,
    { 0, 1, 2, 4, 5, 6, 7, 3, },
    ec_gf8_mul_FA_ops
};

static ec_gf_op_t ec_gf8_mul_FB_ops[] = {
    { EC_GF_OP_XOR2,   1,  0,  0 },
    { EC_GF_OP_XOR2,   2,  1,  0 },
    { EC_GF_OP_XOR2,   0,  5,  0 },
    { EC_GF_OP_XOR2,   3,  2,  0 },
    { EC_GF_OP_XOR2,   0,  7,  0 },
    { EC_GF_OP_XOR2,   2,  7,  0 },
    { EC_GF_OP_XOR2,   1,  6,  0 },
    { EC_GF_OP_XOR2,   7,  6,  0 },
    { EC_GF_OP_XOR2,   4,  3,  0 },
    { EC_GF_OP_XOR2,   6,  5,  0 },
    { EC_GF_OP_XOR2,   7,  4,  0 },
    { EC_GF_OP_XOR2,   5,  4,  0 },
    { EC_GF_OP_END,    0,  0,  0 }
};

static ec_gf_mul_t ec_gf8_mul_FB = {
    8,
    { 4, 5, 6, 7, 0, 1, 2, 3, },
    ec_gf8_mul_FB_ops
};

static ec_gf_op_t ec_gf8_mul_FC_ops[] = {
    { EC_GF_OP_XOR2,   7,  0,  0 },
    { EC_GF_OP_XOR2,   7,  4,  0 },
    { EC_GF_OP_XOR2,   5,  1,  0 },
    { EC_GF_OP_COPY,   9,  3,  0 },
    { EC_GF_OP_XOR3,   8,  5,  7 },
    { EC_GF_OP_XOR2,   3,  6,  0 },
    { EC_GF_OP_XOR2,   8,  3,  0 },
    { EC_GF_OP_XOR2,   2,  8,  0 },
    { EC_GF_OP_XOR2,   1,  2,  0 },
    { EC_GF_OP_XOR2,   4,  2,  0 },
    { EC_GF_OP_XOR2,   0,  1,  0 },
    { EC_GF_OP_XOR2,   3,  4,  0 },
    { EC_GF_OP_XOR2,   5,  0,  0 },
    { EC_GF_OP_XOR2,   6,  0,  0 },
    { EC_GF_OP_XOR3,   0,  9,  2 },
    { EC_GF_OP_END,    0,  0,  0 }
};

static ec_gf_mul_t ec_gf8_mul_FC = {
    10,
    { 5, 6, 3, 7, 1, 8, 0, 4, 2, 9, },
    ec_gf8_mul_FC_ops
};

static ec_gf_op_t ec_gf8_mul_FD_ops[] = {
    { EC_GF_OP_XOR2,   7,  1,  0 },
    { EC_GF_OP_COPY,   8,  7,  0 },
    { EC_GF_OP_XOR2,   5,  0,  0 },
    { EC_GF_OP_XOR2,   7,  5,  0 },
    { EC_GF_OP_XOR2,   4,  2,  0 },
    { EC_GF_OP_XOR2,   4,  7,  0 },
    { EC_GF_OP_XOR2,   5,  6,  0 },
    { EC_GF_OP_XOR2,   0,  4,  0 },
    { EC_GF_OP_XOR2,   3,  0,  0 },
    { EC_GF_OP_XOR2,   5,  3,  0 },
    { EC_GF_OP_XOR2,   2,  5,  0 },
    { EC_GF_OP_XOR2,   1,  2,  0 },
    { EC_GF_OP_XOR2,   0,  1,  0 },
    { EC_GF_OP_XOR2,   6,  1,  0 },
    { EC_GF_OP_XOR3,   1,  8,  0 },
    { EC_GF_OP_END,    0,  0,  0 }
};

static ec_gf_mul_t ec_gf8_mul_FD = {
    9,
    { 5, 3, 7, 6, 1, 2, 4, 0, 8, },
    ec_gf8_mul_FD_ops
};

static ec_gf_op_t ec_gf8_mul_FE_ops[] = {
    { EC_GF_OP_XOR2,   2,  0,  0 },
    { EC_GF_OP_COPY,   8,  2,  0 },
    { EC_GF_OP_XOR2,   2,  4,  0 },
    { EC_GF_OP_XOR2,   6,  2,  0 },
    { EC_GF_OP_XOR2,   8,  5,  0 },
    { EC_GF_OP_XOR2,   5,  6,  0 },
    { EC_GF_OP_XOR2,   6,  1,  0 },
    { EC_GF_OP_XOR2,   0,  6,  0 },
    { EC_GF_OP_XOR2,   6,  7,  0 },
    { EC_GF_OP_XOR2,   7,  3,  0 },
    { EC_GF_OP_XOR2,   7,  8,  0 },
    { EC_GF_OP_XOR2,   3,  0,  0 },
    { EC_GF_OP_XOR2,   4,  7,  0 },
    { EC_GF_OP_XOR2,   1,  7,  0 },
    { EC_GF_OP_XOR2,   0,  4,  0 },
    { EC_GF_OP_END,    0,  0,  0 }
};

static ec_gf_mul_t ec_gf8_mul_FE = {
    9,
    { 3, 4, 8, 2, 5, 0, 6, 1, 7, },
    ec_gf8_mul_FE_ops
};

static ec_gf_op_t ec_gf8_mul_FF_ops[] = {
    { EC_GF_OP_XOR2,   4,  7,  0 },
    { EC_GF_OP_COPY,   9,  0,  0 },
    { EC_GF_OP_COPY,   8,  4,  0 },
    { EC_GF_OP_XOR2,   9,  1,  0 },
    { EC_GF_OP_XOR2,   4,  2,  0 },
    { EC_GF_OP_XOR2,   9,  4,  0 },
    { EC_GF_OP_XOR2,   0,  5,  0 },
    { EC_GF_OP_XOR2,   2,  0,  0 },
    { EC_GF_OP_XOR2,   3,  9,  0 },
    { EC_GF_OP_XOR2,   7,  3,  0 },
    { EC_GF_OP_XOR2,   2,  6,  0 },
    { EC_GF_OP_XOR2,   5,  3,  0 },
    { EC_GF_OP_XOR2,   6,  7,  0 },
    { EC_GF_OP_XOR2,   1,  7,  0 },
    { EC_GF_OP_XOR3,   3,  8,  5 },
    { EC_GF_OP_XOR2,   4,  6,  0 },
    { EC_GF_OP_END,    0,  0,  0 }
};

static ec_gf_mul_t ec_gf8_mul_FF = {
    10,
    { 6, 5, 0, 1, 2, 4, 9, 3, 7, 8, },
    ec_gf8_mul_FF_ops
};

ec_gf_mul_t *ec_gf8_mul[] = {
    &ec_gf8_mul_00, &ec_gf8_mul_01, &ec_gf8_mul_02, &ec_gf8_mul_03,
    &ec_gf8_mul_04, &ec_gf8_mul_05, &ec_gf8_mul_06, &ec_gf8_mul_07,
    &ec_gf8_mul_08, &ec_gf8_mul_09, &ec_gf8_mul_0A, &ec_gf8_mul_0B,
    &ec_gf8_mul_0C, &ec_gf8_mul_0D, &ec_gf8_mul_0E, &ec_gf8_mul_0F,
    &ec_gf8_mul_10, &ec_gf8_mul_11, &ec_gf8_mul_12, &ec_gf8_mul_13,
    &ec_gf8_mul_14, &ec_gf8_mul_15, &ec_gf8_mul_16, &ec_gf8_mul_17,
    &ec_gf8_mul_18, &ec_gf8_mul_19, &ec_gf8_mul_1A, &ec_gf8_mul_1B,
    &ec_gf8_mul_1C, &ec_gf8_mul_1D, &ec_gf8_mul_1E, &ec_gf8_mul_1F,
    &ec_gf8_mul_20, &ec_gf8_mul_21, &ec_gf8_mul_22, &ec_gf8_mul_23,
    &ec_gf8_mul_24, &ec_gf8_mul_25, &ec_gf8_mul_26, &ec_gf8_mul_27,
    &ec_gf8_mul_28, &ec_gf8_mul_29, &ec_gf8_mul_2A, &ec_gf8_mul_2B,
    &ec_gf8_mul_2C, &ec_gf8_mul_2D, &ec_gf8_mul_2E, &ec_gf8_mul_2F,
    &ec_gf8_mul_30, &ec_gf8_mul_31, &ec_gf8_mul_32, &ec_gf8_mul_33,
    &ec_gf8_mul_34, &ec_gf8_mul_35, &ec_gf8_mul_36, &ec_gf8_mul_37,
    &ec_gf8_mul_38, &ec_gf8_mul_39, &ec_gf8_mul_3A, &ec_gf8_mul_3B,
    &ec_gf8_mul_3C, &ec_gf8_mul_3D, &ec_gf8_mul_3E, &ec_gf8_mul_3F,
    &ec_gf8_mul_40, &ec_gf8_mul_41, &ec_gf8_mul_42, &ec_gf8_mul_43,
    &ec_gf8_mul_44, &ec_gf8_mul_45, &ec_gf8_mul_46, &ec_gf8_mul_47,
    &ec_gf8_mul_48, &ec_gf8_mul_49, &ec_gf8_mul_4A, &ec_gf8_mul_4B,
    &ec_gf8_mul_4C, &ec_gf8_mul_4D, &ec_gf8_mul_4E, &ec_gf8_mul_4F,
    &ec_gf8_mul_50, &ec_gf8_mul_51, &ec_gf8_mul_52, &ec_gf8_mul_53,
    &ec_gf8_mul_54, &ec_gf8_mul_55, &ec_gf8_mul_56, &ec_gf8_mul_57,
    &ec_gf8_mul_58, &ec_gf8_mul_59, &ec_gf8_mul_5A, &ec_gf8_mul_5B,
    &ec_gf8_mul_5C, &ec_gf8_mul_5D, &ec_gf8_mul_5E, &ec_gf8_mul_5F,
    &ec_gf8_mul_60, &ec_gf8_mul_61, &ec_gf8_mul_62, &ec_gf8_mul_63,
    &ec_gf8_mul_64, &ec_gf8_mul_65, &ec_gf8_mul_66, &ec_gf8_mul_67,
    &ec_gf8_mul_68, &ec_gf8_mul_69, &ec_gf8_mul_6A, &ec_gf8_mul_6B,
    &ec_gf8_mul_6C, &ec_gf8_mul_6D, &ec_gf8_mul_6E, &ec_gf8_mul_6F,
    &ec_gf8_mul_70, &ec_gf8_mul_71, &ec_gf8_mul_72, &ec_gf8_mul_73,
    &ec_gf8_mul_74, &ec_gf8_mul_75, &ec_gf8_mul_76, &ec_gf8_mul_77,
    &ec_gf8_mul_78, &ec_gf8_mul_79, &ec_gf8_mul_7A, &ec_gf8_mul_7B,
    &ec_gf8_mul_7C, &ec_gf8_mul_7D, &ec_gf8_mul_7E, &ec_gf8_mul_7F,
    &ec_gf8_mul_80, &ec_gf8_mul_81, &ec_gf8_mul_82, &ec_gf8_mul_83,
    &ec_gf8_mul_84, &ec_gf8_mul_85, &ec_gf8_mul_86, &ec_gf8_mul_87,
    &ec_gf8_mul_88, &ec_gf8_mul_89, &ec_gf8_mul_8A, &ec_gf8_mul_8B,
    &ec_gf8_mul_8C, &ec_gf8_mul_8D, &ec_gf8_mul_8E, &ec_gf8_mul_8F,
    &ec_gf8_mul_90, &ec_gf8_mul_91, &ec_gf8_mul_92, &ec_gf8_mul_93,
    &ec_gf8_mul_94, &ec_gf8_mul_95, &ec_gf8_mul_96, &ec_gf8_mul_97,
    &ec_gf8_mul_98, &ec_gf8_mul_99, &ec_gf8_mul_9A, &ec_gf8_mul_9B,
    &ec_gf8_mul_9C, &ec_gf8_mul_9D, &ec_gf8_mul_9E, &ec_gf8_mul_9F,
    &ec_gf8_mul_A0, &ec_gf8_mul_A1, &ec_gf8_mul_A2, &ec_gf8_mul_A3,
    &ec_gf8_mul_A4, &ec_gf8_mul_A5, &ec_gf8_mul_A6, &ec_gf8_mul_A7,
    &ec_gf8_mul_A8, &ec_gf8_mul_A9, &ec_gf8_mul_AA, &ec_gf8_mul_AB,
    &ec_gf8_mul_AC, &ec_gf8_mul_AD, &ec_gf8_mul_AE, &ec_gf8_mul_AF,
    &ec_gf8_mul_B0, &ec_gf8_mul_B1, &ec_gf8_mul_B2, &ec_gf8_mul_B3,
    &ec_gf8_mul_B4, &ec_gf8_mul_B5, &ec_gf8_mul_B6, &ec_gf8_mul_B7,
    &ec_gf8_mul_B8, &ec_gf8_mul_B9, &ec_gf8_mul_BA, &ec_gf8_mul_BB,
    &ec_gf8_mul_BC, &ec_gf8_mul_BD, &ec_gf8_mul_BE, &ec_gf8_mul_BF,
    &ec_gf8_mul_C0, &ec_gf8_mul_C1, &ec_gf8_mul_C2, &ec_gf8_mul_C3,
    &ec_gf8_mul_C4, &ec_gf8_mul_C5, &ec_gf8_mul_C6, &ec_gf8_mul_C7,
    &ec_gf8_mul_C8, &ec_gf8_mul_C9, &ec_gf8_mul_CA, &ec_gf8_mul_CB,
    &ec_gf8_mul_CC, &ec_gf8_mul_CD, &ec_gf8_mul_CE, &ec_gf8_mul_CF,
    &ec_gf8_mul_D0, &ec_gf8_mul_D1, &ec_gf8_mul_D2, &ec_gf8_mul_D3,
    &ec_gf8_mul_D4, &ec_gf8_mul_D5, &ec_gf8_mul_D6, &ec_gf8_mul_D7,
    &ec_gf8_mul_D8, &ec_gf8_mul_D9, &ec_gf8_mul_DA, &ec_gf8_mul_DB,
    &ec_gf8_mul_DC, &ec_gf8_mul_DD, &ec_gf8_mul_DE, &ec_gf8_mul_DF,
    &ec_gf8_mul_E0, &ec_gf8_mul_E1, &ec_gf8_mul_E2, &ec_gf8_mul_E3,
    &ec_gf8_mul_E4, &ec_gf8_mul_E5, &ec_gf8_mul_E6, &ec_gf8_mul_E7,
    &ec_gf8_mul_E8, &ec_gf8_mul_E9, &ec_gf8_mul_EA, &ec_gf8_mul_EB,
    &ec_gf8_mul_EC, &ec_gf8_mul_ED, &ec_gf8_mul_EE, &ec_gf8_mul_EF,
    &ec_gf8_mul_F0, &ec_gf8_mul_F1, &ec_gf8_mul_F2, &ec_gf8_mul_F3,
    &ec_gf8_mul_F4, &ec_gf8_mul_F5, &ec_gf8_mul_F6, &ec_gf8_mul_F7,
    &ec_gf8_mul_F8, &ec_gf8_mul_F9, &ec_gf8_mul_FA, &ec_gf8_mul_FB,
    &ec_gf8_mul_FC, &ec_gf8_mul_FD, &ec_gf8_mul_FE, &ec_gf8_mul_FF
};
