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

#include "ec-method.h"
#include "ec-code-c.h"

#define WIDTH (EC_METHOD_WORD_SIZE / sizeof(uint64_t))

static void gf8_muladd_00(void *out, void *in)
{
    memcpy(out, in, EC_METHOD_WORD_SIZE * 8);
}

static void gf8_muladd_01(void *out, void *in)
{
    unsigned int i;
    uint64_t *in_ptr = (uint64_t *)in;
    uint64_t *out_ptr = (uint64_t *)out;

    for (i = 0; i < WIDTH; i++) {
        out_ptr[0] ^= in_ptr[0];
        out_ptr[WIDTH] ^= in_ptr[WIDTH];
        out_ptr[WIDTH * 2] ^= in_ptr[WIDTH * 2];
        out_ptr[WIDTH * 3] ^= in_ptr[WIDTH * 3];
        out_ptr[WIDTH * 4] ^= in_ptr[WIDTH * 4];
        out_ptr[WIDTH * 5] ^= in_ptr[WIDTH * 5];
        out_ptr[WIDTH * 6] ^= in_ptr[WIDTH * 6];
        out_ptr[WIDTH * 7] ^= in_ptr[WIDTH * 7];

        in_ptr++;
        out_ptr++;
    }
}

static void gf8_muladd_02(void *out, void *in)
{
    unsigned int i;
    uint64_t *in_ptr = (uint64_t *)in;
    uint64_t *out_ptr = (uint64_t *)out;

    for (i = 0; i < WIDTH; i++) {
        uint64_t out0, out1, out2, out3, out4, out5, out6, out7;

        uint64_t in0 = out_ptr[0];
        uint64_t in1 = out_ptr[WIDTH];
        uint64_t in2 = out_ptr[WIDTH * 2];
        uint64_t in3 = out_ptr[WIDTH * 3];
        uint64_t in4 = out_ptr[WIDTH * 4];
        uint64_t in5 = out_ptr[WIDTH * 5];
        uint64_t in6 = out_ptr[WIDTH * 6];
        uint64_t in7 = out_ptr[WIDTH * 7];

        out0 = in7;
        out1 = in0;
        out7 = in6;
        out5 = in4;
        out6 = in5;
        out3 = in2 ^ in7;
        out4 = in3 ^ in7;
        out2 = in1 ^ in7;

        out_ptr[0] = out0 ^ in_ptr[0];
        out_ptr[WIDTH] = out1 ^ in_ptr[WIDTH];
        out_ptr[WIDTH * 2] = out2 ^ in_ptr[WIDTH * 2];
        out_ptr[WIDTH * 3] = out3 ^ in_ptr[WIDTH * 3];
        out_ptr[WIDTH * 4] = out4 ^ in_ptr[WIDTH * 4];
        out_ptr[WIDTH * 5] = out5 ^ in_ptr[WIDTH * 5];
        out_ptr[WIDTH * 6] = out6 ^ in_ptr[WIDTH * 6];
        out_ptr[WIDTH * 7] = out7 ^ in_ptr[WIDTH * 7];

        in_ptr++;
        out_ptr++;
    }
}

static void gf8_muladd_03(void *out, void *in)
{
    unsigned int i;
    uint64_t *in_ptr = (uint64_t *)in;
    uint64_t *out_ptr = (uint64_t *)out;

    for (i = 0; i < WIDTH; i++) {
        uint64_t out0, out1, out2, out3, out4, out5, out6, out7;
        uint64_t tmp0;

        uint64_t in0 = out_ptr[0];
        uint64_t in1 = out_ptr[WIDTH];
        uint64_t in2 = out_ptr[WIDTH * 2];
        uint64_t in3 = out_ptr[WIDTH * 3];
        uint64_t in4 = out_ptr[WIDTH * 4];
        uint64_t in5 = out_ptr[WIDTH * 5];
        uint64_t in6 = out_ptr[WIDTH * 6];
        uint64_t in7 = out_ptr[WIDTH * 7];

        out0 = in0 ^ in7;
        tmp0 = in2 ^ in7;
        out1 = in0 ^ in1;
        out7 = in6 ^ in7;
        out5 = in4 ^ in5;
        out6 = in5 ^ in6;
        out4 = in3 ^ in4 ^ in7;
        out2 = tmp0 ^ in1;
        out3 = tmp0 ^ in3;

        out_ptr[0] = out0 ^ in_ptr[0];
        out_ptr[WIDTH] = out1 ^ in_ptr[WIDTH];
        out_ptr[WIDTH * 2] = out2 ^ in_ptr[WIDTH * 2];
        out_ptr[WIDTH * 3] = out3 ^ in_ptr[WIDTH * 3];
        out_ptr[WIDTH * 4] = out4 ^ in_ptr[WIDTH * 4];
        out_ptr[WIDTH * 5] = out5 ^ in_ptr[WIDTH * 5];
        out_ptr[WIDTH * 6] = out6 ^ in_ptr[WIDTH * 6];
        out_ptr[WIDTH * 7] = out7 ^ in_ptr[WIDTH * 7];

        in_ptr++;
        out_ptr++;
    }
}

static void gf8_muladd_04(void *out, void *in)
{
    unsigned int i;
    uint64_t *in_ptr = (uint64_t *)in;
    uint64_t *out_ptr = (uint64_t *)out;

    for (i = 0; i < WIDTH; i++) {
        uint64_t out0, out1, out2, out3, out4, out5, out6, out7;
        uint64_t tmp0;

        uint64_t in0 = out_ptr[0];
        uint64_t in1 = out_ptr[WIDTH];
        uint64_t in2 = out_ptr[WIDTH * 2];
        uint64_t in3 = out_ptr[WIDTH * 3];
        uint64_t in4 = out_ptr[WIDTH * 4];
        uint64_t in5 = out_ptr[WIDTH * 5];
        uint64_t in6 = out_ptr[WIDTH * 6];
        uint64_t in7 = out_ptr[WIDTH * 7];

        out0 = in6;
        out1 = in7;
        out7 = in5;
        out6 = in4;
        tmp0 = in6 ^ in7;
        out2 = in0 ^ in6;
        out5 = in3 ^ in7;
        out3 = tmp0 ^ in1;
        out4 = tmp0 ^ in2;

        out_ptr[0] = out0 ^ in_ptr[0];
        out_ptr[WIDTH] = out1 ^ in_ptr[WIDTH];
        out_ptr[WIDTH * 2] = out2 ^ in_ptr[WIDTH * 2];
        out_ptr[WIDTH * 3] = out3 ^ in_ptr[WIDTH * 3];
        out_ptr[WIDTH * 4] = out4 ^ in_ptr[WIDTH * 4];
        out_ptr[WIDTH * 5] = out5 ^ in_ptr[WIDTH * 5];
        out_ptr[WIDTH * 6] = out6 ^ in_ptr[WIDTH * 6];
        out_ptr[WIDTH * 7] = out7 ^ in_ptr[WIDTH * 7];

        in_ptr++;
        out_ptr++;
    }
}

static void gf8_muladd_05(void *out, void *in)
{
    unsigned int i;
    uint64_t *in_ptr = (uint64_t *)in;
    uint64_t *out_ptr = (uint64_t *)out;

    for (i = 0; i < WIDTH; i++) {
        uint64_t out0, out1, out2, out3, out4, out5, out6, out7;

        uint64_t in0 = out_ptr[0];
        uint64_t in1 = out_ptr[WIDTH];
        uint64_t in2 = out_ptr[WIDTH * 2];
        uint64_t in3 = out_ptr[WIDTH * 3];
        uint64_t in4 = out_ptr[WIDTH * 4];
        uint64_t in5 = out_ptr[WIDTH * 5];
        uint64_t in6 = out_ptr[WIDTH * 6];
        uint64_t in7 = out_ptr[WIDTH * 7];

        out0 = in0 ^ in6;
        out1 = in1 ^ in7;
        out7 = in5 ^ in7;
        out6 = in4 ^ in6;
        out2 = out0 ^ in2;
        out3 = out1 ^ in3 ^ in6;
        out5 = out7 ^ in3;
        out4 = out6 ^ in2 ^ in7;

        out_ptr[0] = out0 ^ in_ptr[0];
        out_ptr[WIDTH] = out1 ^ in_ptr[WIDTH];
        out_ptr[WIDTH * 2] = out2 ^ in_ptr[WIDTH * 2];
        out_ptr[WIDTH * 3] = out3 ^ in_ptr[WIDTH * 3];
        out_ptr[WIDTH * 4] = out4 ^ in_ptr[WIDTH * 4];
        out_ptr[WIDTH * 5] = out5 ^ in_ptr[WIDTH * 5];
        out_ptr[WIDTH * 6] = out6 ^ in_ptr[WIDTH * 6];
        out_ptr[WIDTH * 7] = out7 ^ in_ptr[WIDTH * 7];

        in_ptr++;
        out_ptr++;
    }
}

static void gf8_muladd_06(void *out, void *in)
{
    unsigned int i;
    uint64_t *in_ptr = (uint64_t *)in;
    uint64_t *out_ptr = (uint64_t *)out;

    for (i = 0; i < WIDTH; i++) {
        uint64_t out0, out1, out2, out3, out4, out5, out6, out7;
        uint64_t tmp0;

        uint64_t in0 = out_ptr[0];
        uint64_t in1 = out_ptr[WIDTH];
        uint64_t in2 = out_ptr[WIDTH * 2];
        uint64_t in3 = out_ptr[WIDTH * 3];
        uint64_t in4 = out_ptr[WIDTH * 4];
        uint64_t in5 = out_ptr[WIDTH * 5];
        uint64_t in6 = out_ptr[WIDTH * 6];
        uint64_t in7 = out_ptr[WIDTH * 7];

        out0 = in6 ^ in7;
        tmp0 = in1 ^ in6;
        out1 = in0 ^ in7;
        out7 = in5 ^ in6;
        out6 = in4 ^ in5;
        out4 = in2 ^ in3 ^ in6;
        out5 = in3 ^ in4 ^ in7;
        out3 = tmp0 ^ in2;
        out2 = tmp0 ^ out1;

        out_ptr[0] = out0 ^ in_ptr[0];
        out_ptr[WIDTH] = out1 ^ in_ptr[WIDTH];
        out_ptr[WIDTH * 2] = out2 ^ in_ptr[WIDTH * 2];
        out_ptr[WIDTH * 3] = out3 ^ in_ptr[WIDTH * 3];
        out_ptr[WIDTH * 4] = out4 ^ in_ptr[WIDTH * 4];
        out_ptr[WIDTH * 5] = out5 ^ in_ptr[WIDTH * 5];
        out_ptr[WIDTH * 6] = out6 ^ in_ptr[WIDTH * 6];
        out_ptr[WIDTH * 7] = out7 ^ in_ptr[WIDTH * 7];

        in_ptr++;
        out_ptr++;
    }
}

static void gf8_muladd_07(void *out, void *in)
{
    unsigned int i;
    uint64_t *in_ptr = (uint64_t *)in;
    uint64_t *out_ptr = (uint64_t *)out;

    for (i = 0; i < WIDTH; i++) {
        uint64_t out0, out1, out2, out3, out4, out5, out6, out7;
        uint64_t tmp0, tmp1, tmp2, tmp3;

        uint64_t in0 = out_ptr[0];
        uint64_t in1 = out_ptr[WIDTH];
        uint64_t in2 = out_ptr[WIDTH * 2];
        uint64_t in3 = out_ptr[WIDTH * 3];
        uint64_t in4 = out_ptr[WIDTH * 4];
        uint64_t in5 = out_ptr[WIDTH * 5];
        uint64_t in6 = out_ptr[WIDTH * 6];
        uint64_t in7 = out_ptr[WIDTH * 7];

        tmp0 = in2 ^ in6;
        tmp1 = in5 ^ in6;
        tmp2 = in0 ^ in7;
        tmp3 = tmp0 ^ in3;
        out6 = tmp1 ^ in4;
        out7 = tmp1 ^ in7;
        out0 = tmp2 ^ in6;
        out1 = tmp2 ^ in1;
        out3 = tmp3 ^ in1;
        out4 = tmp3 ^ in4;
        out5 = out4 ^ out7 ^ in2;
        out2 = tmp0 ^ out1;

        out_ptr[0] = out0 ^ in_ptr[0];
        out_ptr[WIDTH] = out1 ^ in_ptr[WIDTH];
        out_ptr[WIDTH * 2] = out2 ^ in_ptr[WIDTH * 2];
        out_ptr[WIDTH * 3] = out3 ^ in_ptr[WIDTH * 3];
        out_ptr[WIDTH * 4] = out4 ^ in_ptr[WIDTH * 4];
        out_ptr[WIDTH * 5] = out5 ^ in_ptr[WIDTH * 5];
        out_ptr[WIDTH * 6] = out6 ^ in_ptr[WIDTH * 6];
        out_ptr[WIDTH * 7] = out7 ^ in_ptr[WIDTH * 7];

        in_ptr++;
        out_ptr++;
    }
}

static void gf8_muladd_08(void *out, void *in)
{
    unsigned int i;
    uint64_t *in_ptr = (uint64_t *)in;
    uint64_t *out_ptr = (uint64_t *)out;

    for (i = 0; i < WIDTH; i++) {
        uint64_t out0, out1, out2, out3, out4, out5, out6, out7;

        uint64_t in0 = out_ptr[0];
        uint64_t in1 = out_ptr[WIDTH];
        uint64_t in2 = out_ptr[WIDTH * 2];
        uint64_t in3 = out_ptr[WIDTH * 3];
        uint64_t in4 = out_ptr[WIDTH * 4];
        uint64_t in5 = out_ptr[WIDTH * 5];
        uint64_t in6 = out_ptr[WIDTH * 6];
        uint64_t in7 = out_ptr[WIDTH * 7];

        out0 = in5;
        out1 = in6;
        out7 = in4;
        out6 = in3 ^ in7;
        out3 = in0 ^ in5 ^ in6;
        out5 = in2 ^ in6 ^ in7;
        out2 = in5 ^ in7;
        out4 = out2 ^ in1 ^ in6;

        out_ptr[0] = out0 ^ in_ptr[0];
        out_ptr[WIDTH] = out1 ^ in_ptr[WIDTH];
        out_ptr[WIDTH * 2] = out2 ^ in_ptr[WIDTH * 2];
        out_ptr[WIDTH * 3] = out3 ^ in_ptr[WIDTH * 3];
        out_ptr[WIDTH * 4] = out4 ^ in_ptr[WIDTH * 4];
        out_ptr[WIDTH * 5] = out5 ^ in_ptr[WIDTH * 5];
        out_ptr[WIDTH * 6] = out6 ^ in_ptr[WIDTH * 6];
        out_ptr[WIDTH * 7] = out7 ^ in_ptr[WIDTH * 7];

        in_ptr++;
        out_ptr++;
    }
}

static void gf8_muladd_09(void *out, void *in)
{
    unsigned int i;
    uint64_t *in_ptr = (uint64_t *)in;
    uint64_t *out_ptr = (uint64_t *)out;

    for (i = 0; i < WIDTH; i++) {
        uint64_t out0, out1, out2, out3, out4, out5, out6, out7;
        uint64_t tmp0;

        uint64_t in0 = out_ptr[0];
        uint64_t in1 = out_ptr[WIDTH];
        uint64_t in2 = out_ptr[WIDTH * 2];
        uint64_t in3 = out_ptr[WIDTH * 3];
        uint64_t in4 = out_ptr[WIDTH * 4];
        uint64_t in5 = out_ptr[WIDTH * 5];
        uint64_t in6 = out_ptr[WIDTH * 6];
        uint64_t in7 = out_ptr[WIDTH * 7];

        out0 = in0 ^ in5;
        tmp0 = in3 ^ in6;
        out1 = in1 ^ in6;
        out7 = in4 ^ in7;
        out2 = in2 ^ in5 ^ in7;
        out3 = tmp0 ^ out0;
        out6 = tmp0 ^ in7;
        out4 = out1 ^ out7 ^ in5;
        out5 = out2 ^ in6;

        out_ptr[0] = out0 ^ in_ptr[0];
        out_ptr[WIDTH] = out1 ^ in_ptr[WIDTH];
        out_ptr[WIDTH * 2] = out2 ^ in_ptr[WIDTH * 2];
        out_ptr[WIDTH * 3] = out3 ^ in_ptr[WIDTH * 3];
        out_ptr[WIDTH * 4] = out4 ^ in_ptr[WIDTH * 4];
        out_ptr[WIDTH * 5] = out5 ^ in_ptr[WIDTH * 5];
        out_ptr[WIDTH * 6] = out6 ^ in_ptr[WIDTH * 6];
        out_ptr[WIDTH * 7] = out7 ^ in_ptr[WIDTH * 7];

        in_ptr++;
        out_ptr++;
    }
}

static void gf8_muladd_0A(void *out, void *in)
{
    unsigned int i;
    uint64_t *in_ptr = (uint64_t *)in;
    uint64_t *out_ptr = (uint64_t *)out;

    for (i = 0; i < WIDTH; i++) {
        uint64_t out0, out1, out2, out3, out4, out5, out6, out7;

        uint64_t in0 = out_ptr[0];
        uint64_t in1 = out_ptr[WIDTH];
        uint64_t in2 = out_ptr[WIDTH * 2];
        uint64_t in3 = out_ptr[WIDTH * 3];
        uint64_t in4 = out_ptr[WIDTH * 4];
        uint64_t in5 = out_ptr[WIDTH * 5];
        uint64_t in6 = out_ptr[WIDTH * 6];
        uint64_t in7 = out_ptr[WIDTH * 7];

        out0 = in5 ^ in7;
        out1 = in0 ^ in6;
        out7 = in4 ^ in6;
        out2 = in1 ^ in5;
        out6 = out0 ^ in3;
        out3 = out0 ^ out1 ^ in2;
        out5 = out7 ^ in2 ^ in7;
        out4 = out2 ^ in3 ^ in6;

        out_ptr[0] = out0 ^ in_ptr[0];
        out_ptr[WIDTH] = out1 ^ in_ptr[WIDTH];
        out_ptr[WIDTH * 2] = out2 ^ in_ptr[WIDTH * 2];
        out_ptr[WIDTH * 3] = out3 ^ in_ptr[WIDTH * 3];
        out_ptr[WIDTH * 4] = out4 ^ in_ptr[WIDTH * 4];
        out_ptr[WIDTH * 5] = out5 ^ in_ptr[WIDTH * 5];
        out_ptr[WIDTH * 6] = out6 ^ in_ptr[WIDTH * 6];
        out_ptr[WIDTH * 7] = out7 ^ in_ptr[WIDTH * 7];

        in_ptr++;
        out_ptr++;
    }
}

static void gf8_muladd_0B(void *out, void *in)
{
    unsigned int i;
    uint64_t *in_ptr = (uint64_t *)in;
    uint64_t *out_ptr = (uint64_t *)out;

    for (i = 0; i < WIDTH; i++) {
        uint64_t out0, out1, out2, out3, out4, out5, out6, out7;
        uint64_t tmp0, tmp1, tmp2;

        uint64_t in0 = out_ptr[0];
        uint64_t in1 = out_ptr[WIDTH];
        uint64_t in2 = out_ptr[WIDTH * 2];
        uint64_t in3 = out_ptr[WIDTH * 3];
        uint64_t in4 = out_ptr[WIDTH * 4];
        uint64_t in5 = out_ptr[WIDTH * 5];
        uint64_t in6 = out_ptr[WIDTH * 6];
        uint64_t in7 = out_ptr[WIDTH * 7];

        tmp0 = in2 ^ in5;
        tmp1 = in0 ^ in6;
        tmp2 = in4 ^ in7;
        out0 = in0 ^ in5 ^ in7;
        out2 = tmp0 ^ in1;
        out1 = tmp1 ^ in1;
        out6 = tmp1 ^ out0 ^ in3;
        out7 = tmp2 ^ in6;
        out4 = tmp2 ^ out6 ^ in1;
        out3 = out6 ^ in0 ^ in2;
        out5 = tmp0 ^ out7;

        out_ptr[0] = out0 ^ in_ptr[0];
        out_ptr[WIDTH] = out1 ^ in_ptr[WIDTH];
        out_ptr[WIDTH * 2] = out2 ^ in_ptr[WIDTH * 2];
        out_ptr[WIDTH * 3] = out3 ^ in_ptr[WIDTH * 3];
        out_ptr[WIDTH * 4] = out4 ^ in_ptr[WIDTH * 4];
        out_ptr[WIDTH * 5] = out5 ^ in_ptr[WIDTH * 5];
        out_ptr[WIDTH * 6] = out6 ^ in_ptr[WIDTH * 6];
        out_ptr[WIDTH * 7] = out7 ^ in_ptr[WIDTH * 7];

        in_ptr++;
        out_ptr++;
    }
}

static void gf8_muladd_0C(void *out, void *in)
{
    unsigned int i;
    uint64_t *in_ptr = (uint64_t *)in;
    uint64_t *out_ptr = (uint64_t *)out;

    for (i = 0; i < WIDTH; i++) {
        uint64_t out0, out1, out2, out3, out4, out5, out6, out7;
        uint64_t tmp0, tmp1;

        uint64_t in0 = out_ptr[0];
        uint64_t in1 = out_ptr[WIDTH];
        uint64_t in2 = out_ptr[WIDTH * 2];
        uint64_t in3 = out_ptr[WIDTH * 3];
        uint64_t in4 = out_ptr[WIDTH * 4];
        uint64_t in5 = out_ptr[WIDTH * 5];
        uint64_t in6 = out_ptr[WIDTH * 6];
        uint64_t in7 = out_ptr[WIDTH * 7];

        out0 = in5 ^ in6;
        out1 = in6 ^ in7;
        out7 = in4 ^ in5;
        tmp0 = in1 ^ in5;
        tmp1 = in0 ^ in7;
        out5 = in2 ^ in3 ^ in6;
        out6 = in3 ^ in4 ^ in7;
        out2 = tmp1 ^ out0;
        out4 = tmp0 ^ in2;
        out3 = tmp0 ^ tmp1;

        out_ptr[0] = out0 ^ in_ptr[0];
        out_ptr[WIDTH] = out1 ^ in_ptr[WIDTH];
        out_ptr[WIDTH * 2] = out2 ^ in_ptr[WIDTH * 2];
        out_ptr[WIDTH * 3] = out3 ^ in_ptr[WIDTH * 3];
        out_ptr[WIDTH * 4] = out4 ^ in_ptr[WIDTH * 4];
        out_ptr[WIDTH * 5] = out5 ^ in_ptr[WIDTH * 5];
        out_ptr[WIDTH * 6] = out6 ^ in_ptr[WIDTH * 6];
        out_ptr[WIDTH * 7] = out7 ^ in_ptr[WIDTH * 7];

        in_ptr++;
        out_ptr++;
    }
}

static void gf8_muladd_0D(void *out, void *in)
{
    unsigned int i;
    uint64_t *in_ptr = (uint64_t *)in;
    uint64_t *out_ptr = (uint64_t *)out;

    for (i = 0; i < WIDTH; i++) {
        uint64_t out0, out1, out2, out3, out4, out5, out6, out7;
        uint64_t tmp0, tmp1, tmp2;

        uint64_t in0 = out_ptr[0];
        uint64_t in1 = out_ptr[WIDTH];
        uint64_t in2 = out_ptr[WIDTH * 2];
        uint64_t in3 = out_ptr[WIDTH * 3];
        uint64_t in4 = out_ptr[WIDTH * 4];
        uint64_t in5 = out_ptr[WIDTH * 5];
        uint64_t in6 = out_ptr[WIDTH * 6];
        uint64_t in7 = out_ptr[WIDTH * 7];

        tmp0 = in4 ^ in5;
        tmp1 = in5 ^ in6;
        out1 = in1 ^ in6 ^ in7;
        out7 = tmp0 ^ in7;
        out4 = tmp0 ^ in1 ^ in2;
        out0 = tmp1 ^ in0;
        tmp2 = tmp1 ^ in3;
        out6 = tmp2 ^ out7;
        out2 = out0 ^ in2 ^ in7;
        out3 = out0 ^ out1 ^ in3;
        out5 = tmp2 ^ in2;

        out_ptr[0] = out0 ^ in_ptr[0];
        out_ptr[WIDTH] = out1 ^ in_ptr[WIDTH];
        out_ptr[WIDTH * 2] = out2 ^ in_ptr[WIDTH * 2];
        out_ptr[WIDTH * 3] = out3 ^ in_ptr[WIDTH * 3];
        out_ptr[WIDTH * 4] = out4 ^ in_ptr[WIDTH * 4];
        out_ptr[WIDTH * 5] = out5 ^ in_ptr[WIDTH * 5];
        out_ptr[WIDTH * 6] = out6 ^ in_ptr[WIDTH * 6];
        out_ptr[WIDTH * 7] = out7 ^ in_ptr[WIDTH * 7];

        in_ptr++;
        out_ptr++;
    }
}

static void gf8_muladd_0E(void *out, void *in)
{
    unsigned int i;
    uint64_t *in_ptr = (uint64_t *)in;
    uint64_t *out_ptr = (uint64_t *)out;

    for (i = 0; i < WIDTH; i++) {
        uint64_t out0, out1, out2, out3, out4, out5, out6, out7;
        uint64_t tmp0, tmp1, tmp2, tmp3;

        uint64_t in0 = out_ptr[0];
        uint64_t in1 = out_ptr[WIDTH];
        uint64_t in2 = out_ptr[WIDTH * 2];
        uint64_t in3 = out_ptr[WIDTH * 3];
        uint64_t in4 = out_ptr[WIDTH * 4];
        uint64_t in5 = out_ptr[WIDTH * 5];
        uint64_t in6 = out_ptr[WIDTH * 6];
        uint64_t in7 = out_ptr[WIDTH * 7];

        tmp0 = in0 ^ in1;
        tmp1 = in2 ^ in5;
        tmp2 = in5 ^ in6;
        out1 = in0 ^ in6 ^ in7;
        out3 = tmp0 ^ tmp1;
        out2 = tmp0 ^ tmp2;
        tmp3 = tmp1 ^ in3;
        out7 = tmp2 ^ in4;
        out0 = tmp2 ^ in7;
        out4 = tmp3 ^ in1 ^ in7;
        out5 = tmp3 ^ out7;
        out6 = out0 ^ out5 ^ in2;

        out_ptr[0] = out0 ^ in_ptr[0];
        out_ptr[WIDTH] = out1 ^ in_ptr[WIDTH];
        out_ptr[WIDTH * 2] = out2 ^ in_ptr[WIDTH * 2];
        out_ptr[WIDTH * 3] = out3 ^ in_ptr[WIDTH * 3];
        out_ptr[WIDTH * 4] = out4 ^ in_ptr[WIDTH * 4];
        out_ptr[WIDTH * 5] = out5 ^ in_ptr[WIDTH * 5];
        out_ptr[WIDTH * 6] = out6 ^ in_ptr[WIDTH * 6];
        out_ptr[WIDTH * 7] = out7 ^ in_ptr[WIDTH * 7];

        in_ptr++;
        out_ptr++;
    }
}

static void gf8_muladd_0F(void *out, void *in)
{
    unsigned int i;
    uint64_t *in_ptr = (uint64_t *)in;
    uint64_t *out_ptr = (uint64_t *)out;

    for (i = 0; i < WIDTH; i++) {
        uint64_t out0, out1, out2, out3, out4, out5, out6, out7;
        uint64_t tmp0, tmp1, tmp2, tmp3;

        uint64_t in0 = out_ptr[0];
        uint64_t in1 = out_ptr[WIDTH];
        uint64_t in2 = out_ptr[WIDTH * 2];
        uint64_t in3 = out_ptr[WIDTH * 3];
        uint64_t in4 = out_ptr[WIDTH * 4];
        uint64_t in5 = out_ptr[WIDTH * 5];
        uint64_t in6 = out_ptr[WIDTH * 6];
        uint64_t in7 = out_ptr[WIDTH * 7];

        tmp0 = in6 ^ in7;
        tmp1 = tmp0 ^ in1;
        tmp2 = tmp0 ^ in5;
        out1 = tmp1 ^ in0;
        out7 = tmp2 ^ in4;
        out0 = tmp2 ^ in0;
        out6 = out7 ^ in3;
        out5 = out6 ^ in2 ^ in7;
        tmp3 = tmp1 ^ out0 ^ in2;
        out4 = tmp1 ^ out5;
        out2 = tmp3 ^ in6;
        out3 = tmp3 ^ in3;

        out_ptr[0] = out0 ^ in_ptr[0];
        out_ptr[WIDTH] = out1 ^ in_ptr[WIDTH];
        out_ptr[WIDTH * 2] = out2 ^ in_ptr[WIDTH * 2];
        out_ptr[WIDTH * 3] = out3 ^ in_ptr[WIDTH * 3];
        out_ptr[WIDTH * 4] = out4 ^ in_ptr[WIDTH * 4];
        out_ptr[WIDTH * 5] = out5 ^ in_ptr[WIDTH * 5];
        out_ptr[WIDTH * 6] = out6 ^ in_ptr[WIDTH * 6];
        out_ptr[WIDTH * 7] = out7 ^ in_ptr[WIDTH * 7];

        in_ptr++;
        out_ptr++;
    }
}

static void gf8_muladd_10(void *out, void *in)
{
    unsigned int i;
    uint64_t *in_ptr = (uint64_t *)in;
    uint64_t *out_ptr = (uint64_t *)out;

    for (i = 0; i < WIDTH; i++) {
        uint64_t out0, out1, out2, out3, out4, out5, out6, out7;
        uint64_t tmp0, tmp1;

        uint64_t in0 = out_ptr[0];
        uint64_t in1 = out_ptr[WIDTH];
        uint64_t in2 = out_ptr[WIDTH * 2];
        uint64_t in3 = out_ptr[WIDTH * 3];
        uint64_t in4 = out_ptr[WIDTH * 4];
        uint64_t in5 = out_ptr[WIDTH * 5];
        uint64_t in6 = out_ptr[WIDTH * 6];
        uint64_t in7 = out_ptr[WIDTH * 7];

        out0 = in4;
        out1 = in5;
        out7 = in3 ^ in7;
        tmp0 = in6 ^ in7;
        out2 = in4 ^ in6;
        tmp1 = out2 ^ in5;
        out6 = tmp0 ^ in2;
        out3 = tmp0 ^ tmp1;
        out5 = out2 ^ out3 ^ in1;
        out4 = tmp1 ^ in0;

        out_ptr[0] = out0 ^ in_ptr[0];
        out_ptr[WIDTH] = out1 ^ in_ptr[WIDTH];
        out_ptr[WIDTH * 2] = out2 ^ in_ptr[WIDTH * 2];
        out_ptr[WIDTH * 3] = out3 ^ in_ptr[WIDTH * 3];
        out_ptr[WIDTH * 4] = out4 ^ in_ptr[WIDTH * 4];
        out_ptr[WIDTH * 5] = out5 ^ in_ptr[WIDTH * 5];
        out_ptr[WIDTH * 6] = out6 ^ in_ptr[WIDTH * 6];
        out_ptr[WIDTH * 7] = out7 ^ in_ptr[WIDTH * 7];

        in_ptr++;
        out_ptr++;
    }
}

static void gf8_muladd_11(void *out, void *in)
{
    unsigned int i;
    uint64_t *in_ptr = (uint64_t *)in;
    uint64_t *out_ptr = (uint64_t *)out;

    for (i = 0; i < WIDTH; i++) {
        uint64_t out0, out1, out2, out3, out4, out5, out6, out7;

        uint64_t in0 = out_ptr[0];
        uint64_t in1 = out_ptr[WIDTH];
        uint64_t in2 = out_ptr[WIDTH * 2];
        uint64_t in3 = out_ptr[WIDTH * 3];
        uint64_t in4 = out_ptr[WIDTH * 4];
        uint64_t in5 = out_ptr[WIDTH * 5];
        uint64_t in6 = out_ptr[WIDTH * 6];
        uint64_t in7 = out_ptr[WIDTH * 7];

        out7 = in3;
        out0 = in0 ^ in4;
        out1 = in1 ^ in5;
        out6 = in2 ^ in7;
        out4 = in0 ^ in5 ^ in6;
        out5 = in1 ^ in6 ^ in7;
        out2 = in2 ^ in4 ^ in6;
        out3 = in3 ^ in4 ^ in5 ^ in7;

        out_ptr[0] = out0 ^ in_ptr[0];
        out_ptr[WIDTH] = out1 ^ in_ptr[WIDTH];
        out_ptr[WIDTH * 2] = out2 ^ in_ptr[WIDTH * 2];
        out_ptr[WIDTH * 3] = out3 ^ in_ptr[WIDTH * 3];
        out_ptr[WIDTH * 4] = out4 ^ in_ptr[WIDTH * 4];
        out_ptr[WIDTH * 5] = out5 ^ in_ptr[WIDTH * 5];
        out_ptr[WIDTH * 6] = out6 ^ in_ptr[WIDTH * 6];
        out_ptr[WIDTH * 7] = out7 ^ in_ptr[WIDTH * 7];

        in_ptr++;
        out_ptr++;
    }
}

static void gf8_muladd_12(void *out, void *in)
{
    unsigned int i;
    uint64_t *in_ptr = (uint64_t *)in;
    uint64_t *out_ptr = (uint64_t *)out;

    for (i = 0; i < WIDTH; i++) {
        uint64_t out0, out1, out2, out3, out4, out5, out6, out7;
        uint64_t tmp0, tmp1;

        uint64_t in0 = out_ptr[0];
        uint64_t in1 = out_ptr[WIDTH];
        uint64_t in2 = out_ptr[WIDTH * 2];
        uint64_t in3 = out_ptr[WIDTH * 3];
        uint64_t in4 = out_ptr[WIDTH * 4];
        uint64_t in5 = out_ptr[WIDTH * 5];
        uint64_t in6 = out_ptr[WIDTH * 6];
        uint64_t in7 = out_ptr[WIDTH * 7];

        out0 = in4 ^ in7;
        out1 = in0 ^ in5;
        out3 = in2 ^ in4 ^ in5;
        tmp0 = out0 ^ in6;
        out2 = tmp0 ^ in1;
        tmp1 = tmp0 ^ in3;
        out6 = tmp0 ^ out3;
        out5 = out2 ^ in5;
        out7 = tmp1 ^ in4;
        out4 = tmp1 ^ out1;

        out_ptr[0] = out0 ^ in_ptr[0];
        out_ptr[WIDTH] = out1 ^ in_ptr[WIDTH];
        out_ptr[WIDTH * 2] = out2 ^ in_ptr[WIDTH * 2];
        out_ptr[WIDTH * 3] = out3 ^ in_ptr[WIDTH * 3];
        out_ptr[WIDTH * 4] = out4 ^ in_ptr[WIDTH * 4];
        out_ptr[WIDTH * 5] = out5 ^ in_ptr[WIDTH * 5];
        out_ptr[WIDTH * 6] = out6 ^ in_ptr[WIDTH * 6];
        out_ptr[WIDTH * 7] = out7 ^ in_ptr[WIDTH * 7];

        in_ptr++;
        out_ptr++;
    }
}

static void gf8_muladd_13(void *out, void *in)
{
    unsigned int i;
    uint64_t *in_ptr = (uint64_t *)in;
    uint64_t *out_ptr = (uint64_t *)out;

    for (i = 0; i < WIDTH; i++) {
        uint64_t out0, out1, out2, out3, out4, out5, out6, out7;
        uint64_t tmp0, tmp1;

        uint64_t in0 = out_ptr[0];
        uint64_t in1 = out_ptr[WIDTH];
        uint64_t in2 = out_ptr[WIDTH * 2];
        uint64_t in3 = out_ptr[WIDTH * 3];
        uint64_t in4 = out_ptr[WIDTH * 4];
        uint64_t in5 = out_ptr[WIDTH * 5];
        uint64_t in6 = out_ptr[WIDTH * 6];
        uint64_t in7 = out_ptr[WIDTH * 7];

        out7 = in3 ^ in6;
        tmp0 = in0 ^ in5;
        tmp1 = in4 ^ in7;
        out6 = in2 ^ in5 ^ in7;
        out4 = tmp0 ^ out7 ^ in7;
        out1 = tmp0 ^ in1;
        out0 = tmp1 ^ in0;
        out5 = tmp1 ^ in1 ^ in6;
        out3 = tmp1 ^ out6 ^ in3;
        out2 = out5 ^ in2;

        out_ptr[0] = out0 ^ in_ptr[0];
        out_ptr[WIDTH] = out1 ^ in_ptr[WIDTH];
        out_ptr[WIDTH * 2] = out2 ^ in_ptr[WIDTH * 2];
        out_ptr[WIDTH * 3] = out3 ^ in_ptr[WIDTH * 3];
        out_ptr[WIDTH * 4] = out4 ^ in_ptr[WIDTH * 4];
        out_ptr[WIDTH * 5] = out5 ^ in_ptr[WIDTH * 5];
        out_ptr[WIDTH * 6] = out6 ^ in_ptr[WIDTH * 6];
        out_ptr[WIDTH * 7] = out7 ^ in_ptr[WIDTH * 7];

        in_ptr++;
        out_ptr++;
    }
}

static void gf8_muladd_14(void *out, void *in)
{
    unsigned int i;
    uint64_t *in_ptr = (uint64_t *)in;
    uint64_t *out_ptr = (uint64_t *)out;

    for (i = 0; i < WIDTH; i++) {
        uint64_t out0, out1, out2, out3, out4, out5, out6, out7;
        uint64_t tmp0, tmp1;

        uint64_t in0 = out_ptr[0];
        uint64_t in1 = out_ptr[WIDTH];
        uint64_t in2 = out_ptr[WIDTH * 2];
        uint64_t in3 = out_ptr[WIDTH * 3];
        uint64_t in4 = out_ptr[WIDTH * 4];
        uint64_t in5 = out_ptr[WIDTH * 5];
        uint64_t in6 = out_ptr[WIDTH * 6];
        uint64_t in7 = out_ptr[WIDTH * 7];

        out0 = in4 ^ in6;
        out1 = in5 ^ in7;
        out2 = in0 ^ in4;
        tmp0 = out0 ^ in5;
        out7 = out1 ^ in3;
        tmp1 = out1 ^ in2;
        out3 = tmp0 ^ in1;
        out6 = tmp0 ^ tmp1;
        out4 = tmp1 ^ out2;
        out5 = out3 ^ in3 ^ in4;

        out_ptr[0] = out0 ^ in_ptr[0];
        out_ptr[WIDTH] = out1 ^ in_ptr[WIDTH];
        out_ptr[WIDTH * 2] = out2 ^ in_ptr[WIDTH * 2];
        out_ptr[WIDTH * 3] = out3 ^ in_ptr[WIDTH * 3];
        out_ptr[WIDTH * 4] = out4 ^ in_ptr[WIDTH * 4];
        out_ptr[WIDTH * 5] = out5 ^ in_ptr[WIDTH * 5];
        out_ptr[WIDTH * 6] = out6 ^ in_ptr[WIDTH * 6];
        out_ptr[WIDTH * 7] = out7 ^ in_ptr[WIDTH * 7];

        in_ptr++;
        out_ptr++;
    }
}

static void gf8_muladd_15(void *out, void *in)
{
    unsigned int i;
    uint64_t *in_ptr = (uint64_t *)in;
    uint64_t *out_ptr = (uint64_t *)out;

    for (i = 0; i < WIDTH; i++) {
        uint64_t out0, out1, out2, out3, out4, out5, out6, out7;
        uint64_t tmp0;

        uint64_t in0 = out_ptr[0];
        uint64_t in1 = out_ptr[WIDTH];
        uint64_t in2 = out_ptr[WIDTH * 2];
        uint64_t in3 = out_ptr[WIDTH * 3];
        uint64_t in4 = out_ptr[WIDTH * 4];
        uint64_t in5 = out_ptr[WIDTH * 5];
        uint64_t in6 = out_ptr[WIDTH * 6];
        uint64_t in7 = out_ptr[WIDTH * 7];

        out7 = in3 ^ in5;
        tmp0 = in0 ^ in4;
        out1 = in1 ^ in5 ^ in7;
        out5 = in1 ^ in3 ^ in6;
        out0 = tmp0 ^ in6;
        out2 = tmp0 ^ in2;
        out3 = out5 ^ in4 ^ in5;
        out6 = out2 ^ in0 ^ in7;
        out4 = tmp0 ^ out6 ^ in5;

        out_ptr[0] = out0 ^ in_ptr[0];
        out_ptr[WIDTH] = out1 ^ in_ptr[WIDTH];
        out_ptr[WIDTH * 2] = out2 ^ in_ptr[WIDTH * 2];
        out_ptr[WIDTH * 3] = out3 ^ in_ptr[WIDTH * 3];
        out_ptr[WIDTH * 4] = out4 ^ in_ptr[WIDTH * 4];
        out_ptr[WIDTH * 5] = out5 ^ in_ptr[WIDTH * 5];
        out_ptr[WIDTH * 6] = out6 ^ in_ptr[WIDTH * 6];
        out_ptr[WIDTH * 7] = out7 ^ in_ptr[WIDTH * 7];

        in_ptr++;
        out_ptr++;
    }
}

static void gf8_muladd_16(void *out, void *in)
{
    unsigned int i;
    uint64_t *in_ptr = (uint64_t *)in;
    uint64_t *out_ptr = (uint64_t *)out;

    for (i = 0; i < WIDTH; i++) {
        uint64_t out0, out1, out2, out3, out4, out5, out6, out7;
        uint64_t tmp0, tmp1, tmp2, tmp3;

        uint64_t in0 = out_ptr[0];
        uint64_t in1 = out_ptr[WIDTH];
        uint64_t in2 = out_ptr[WIDTH * 2];
        uint64_t in3 = out_ptr[WIDTH * 3];
        uint64_t in4 = out_ptr[WIDTH * 4];
        uint64_t in5 = out_ptr[WIDTH * 5];
        uint64_t in6 = out_ptr[WIDTH * 6];
        uint64_t in7 = out_ptr[WIDTH * 7];

        tmp0 = in0 ^ in5;
        tmp1 = in4 ^ in7;
        tmp2 = in2 ^ in3 ^ in4;
        out1 = tmp0 ^ in7;
        out4 = tmp0 ^ tmp2;
        out0 = tmp1 ^ in6;
        tmp3 = tmp1 ^ in1;
        out6 = out0 ^ in2 ^ in5;
        out2 = tmp3 ^ in0;
        out3 = out6 ^ in1;
        out7 = tmp2 ^ out6;
        out5 = tmp3 ^ out7;

        out_ptr[0] = out0 ^ in_ptr[0];
        out_ptr[WIDTH] = out1 ^ in_ptr[WIDTH];
        out_ptr[WIDTH * 2] = out2 ^ in_ptr[WIDTH * 2];
        out_ptr[WIDTH * 3] = out3 ^ in_ptr[WIDTH * 3];
        out_ptr[WIDTH * 4] = out4 ^ in_ptr[WIDTH * 4];
        out_ptr[WIDTH * 5] = out5 ^ in_ptr[WIDTH * 5];
        out_ptr[WIDTH * 6] = out6 ^ in_ptr[WIDTH * 6];
        out_ptr[WIDTH * 7] = out7 ^ in_ptr[WIDTH * 7];

        in_ptr++;
        out_ptr++;
    }
}

static void gf8_muladd_17(void *out, void *in)
{
    unsigned int i;
    uint64_t *in_ptr = (uint64_t *)in;
    uint64_t *out_ptr = (uint64_t *)out;

    for (i = 0; i < WIDTH; i++) {
        uint64_t out0, out1, out2, out3, out4, out5, out6, out7;
        uint64_t tmp0, tmp1, tmp2, tmp3;

        uint64_t in0 = out_ptr[0];
        uint64_t in1 = out_ptr[WIDTH];
        uint64_t in2 = out_ptr[WIDTH * 2];
        uint64_t in3 = out_ptr[WIDTH * 3];
        uint64_t in4 = out_ptr[WIDTH * 4];
        uint64_t in5 = out_ptr[WIDTH * 5];
        uint64_t in6 = out_ptr[WIDTH * 6];
        uint64_t in7 = out_ptr[WIDTH * 7];

        tmp0 = in2 ^ in5;
        tmp1 = in3 ^ in6;
        tmp2 = tmp0 ^ in4;
        out4 = tmp0 ^ in0 ^ in3;
        out7 = tmp1 ^ in5;
        tmp3 = tmp1 ^ in1;
        out6 = tmp2 ^ in7;
        out5 = tmp3 ^ in4;
        out3 = tmp3 ^ out6;
        out0 = out3 ^ out4 ^ in1;
        out2 = out3 ^ out7 ^ in0;
        out1 = tmp2 ^ out2;

        out_ptr[0] = out0 ^ in_ptr[0];
        out_ptr[WIDTH] = out1 ^ in_ptr[WIDTH];
        out_ptr[WIDTH * 2] = out2 ^ in_ptr[WIDTH * 2];
        out_ptr[WIDTH * 3] = out3 ^ in_ptr[WIDTH * 3];
        out_ptr[WIDTH * 4] = out4 ^ in_ptr[WIDTH * 4];
        out_ptr[WIDTH * 5] = out5 ^ in_ptr[WIDTH * 5];
        out_ptr[WIDTH * 6] = out6 ^ in_ptr[WIDTH * 6];
        out_ptr[WIDTH * 7] = out7 ^ in_ptr[WIDTH * 7];

        in_ptr++;
        out_ptr++;
    }
}

static void gf8_muladd_18(void *out, void *in)
{
    unsigned int i;
    uint64_t *in_ptr = (uint64_t *)in;
    uint64_t *out_ptr = (uint64_t *)out;

    for (i = 0; i < WIDTH; i++) {
        uint64_t out0, out1, out2, out3, out4, out5, out6, out7;
        uint64_t tmp0, tmp1;

        uint64_t in0 = out_ptr[0];
        uint64_t in1 = out_ptr[WIDTH];
        uint64_t in2 = out_ptr[WIDTH * 2];
        uint64_t in3 = out_ptr[WIDTH * 3];
        uint64_t in4 = out_ptr[WIDTH * 4];
        uint64_t in5 = out_ptr[WIDTH * 5];
        uint64_t in6 = out_ptr[WIDTH * 6];
        uint64_t in7 = out_ptr[WIDTH * 7];

        out0 = in4 ^ in5;
        out1 = in5 ^ in6;
        tmp0 = in4 ^ in7;
        out5 = in1 ^ in2 ^ in5;
        out6 = in2 ^ in3 ^ in6;
        out2 = tmp0 ^ out1;
        out7 = tmp0 ^ in3;
        tmp1 = tmp0 ^ in0;
        out3 = tmp1 ^ in6;
        out4 = tmp1 ^ in1;

        out_ptr[0] = out0 ^ in_ptr[0];
        out_ptr[WIDTH] = out1 ^ in_ptr[WIDTH];
        out_ptr[WIDTH * 2] = out2 ^ in_ptr[WIDTH * 2];
        out_ptr[WIDTH * 3] = out3 ^ in_ptr[WIDTH * 3];
        out_ptr[WIDTH * 4] = out4 ^ in_ptr[WIDTH * 4];
        out_ptr[WIDTH * 5] = out5 ^ in_ptr[WIDTH * 5];
        out_ptr[WIDTH * 6] = out6 ^ in_ptr[WIDTH * 6];
        out_ptr[WIDTH * 7] = out7 ^ in_ptr[WIDTH * 7];

        in_ptr++;
        out_ptr++;
    }
}

static void gf8_muladd_19(void *out, void *in)
{
    unsigned int i;
    uint64_t *in_ptr = (uint64_t *)in;
    uint64_t *out_ptr = (uint64_t *)out;

    for (i = 0; i < WIDTH; i++) {
        uint64_t out0, out1, out2, out3, out4, out5, out6, out7;
        uint64_t tmp0, tmp1;

        uint64_t in0 = out_ptr[0];
        uint64_t in1 = out_ptr[WIDTH];
        uint64_t in2 = out_ptr[WIDTH * 2];
        uint64_t in3 = out_ptr[WIDTH * 3];
        uint64_t in4 = out_ptr[WIDTH * 4];
        uint64_t in5 = out_ptr[WIDTH * 5];
        uint64_t in6 = out_ptr[WIDTH * 6];
        uint64_t in7 = out_ptr[WIDTH * 7];

        out5 = in1 ^ in2;
        out7 = in3 ^ in4;
        tmp0 = in0 ^ in7;
        out6 = in2 ^ in3;
        out1 = in1 ^ in5 ^ in6;
        out0 = in0 ^ in4 ^ in5;
        out4 = tmp0 ^ in1;
        tmp1 = tmp0 ^ in6;
        out2 = tmp1 ^ out0 ^ in2;
        out3 = tmp1 ^ out7;

        out_ptr[0] = out0 ^ in_ptr[0];
        out_ptr[WIDTH] = out1 ^ in_ptr[WIDTH];
        out_ptr[WIDTH * 2] = out2 ^ in_ptr[WIDTH * 2];
        out_ptr[WIDTH * 3] = out3 ^ in_ptr[WIDTH * 3];
        out_ptr[WIDTH * 4] = out4 ^ in_ptr[WIDTH * 4];
        out_ptr[WIDTH * 5] = out5 ^ in_ptr[WIDTH * 5];
        out_ptr[WIDTH * 6] = out6 ^ in_ptr[WIDTH * 6];
        out_ptr[WIDTH * 7] = out7 ^ in_ptr[WIDTH * 7];

        in_ptr++;
        out_ptr++;
    }
}

static void gf8_muladd_1A(void *out, void *in)
{
    unsigned int i;
    uint64_t *in_ptr = (uint64_t *)in;
    uint64_t *out_ptr = (uint64_t *)out;

    for (i = 0; i < WIDTH; i++) {
        uint64_t out0, out1, out2, out3, out4, out5, out6, out7;
        uint64_t tmp0, tmp1, tmp2, tmp3;

        uint64_t in0 = out_ptr[0];
        uint64_t in1 = out_ptr[WIDTH];
        uint64_t in2 = out_ptr[WIDTH * 2];
        uint64_t in3 = out_ptr[WIDTH * 3];
        uint64_t in4 = out_ptr[WIDTH * 4];
        uint64_t in5 = out_ptr[WIDTH * 5];
        uint64_t in6 = out_ptr[WIDTH * 6];
        uint64_t in7 = out_ptr[WIDTH * 7];

        tmp0 = in4 ^ in5;
        tmp1 = in5 ^ in6;
        tmp2 = tmp0 ^ in1;
        out0 = tmp0 ^ in7;
        out1 = tmp1 ^ in0;
        tmp3 = tmp1 ^ in3;
        out5 = tmp2 ^ in2;
        out2 = tmp2 ^ in6;
        out7 = tmp3 ^ out0;
        out6 = tmp3 ^ in2;
        out4 = tmp3 ^ out2 ^ in0;
        out3 = tmp0 ^ out1 ^ in2;

        out_ptr[0] = out0 ^ in_ptr[0];
        out_ptr[WIDTH] = out1 ^ in_ptr[WIDTH];
        out_ptr[WIDTH * 2] = out2 ^ in_ptr[WIDTH * 2];
        out_ptr[WIDTH * 3] = out3 ^ in_ptr[WIDTH * 3];
        out_ptr[WIDTH * 4] = out4 ^ in_ptr[WIDTH * 4];
        out_ptr[WIDTH * 5] = out5 ^ in_ptr[WIDTH * 5];
        out_ptr[WIDTH * 6] = out6 ^ in_ptr[WIDTH * 6];
        out_ptr[WIDTH * 7] = out7 ^ in_ptr[WIDTH * 7];

        in_ptr++;
        out_ptr++;
    }
}

static void gf8_muladd_1B(void *out, void *in)
{
    unsigned int i;
    uint64_t *in_ptr = (uint64_t *)in;
    uint64_t *out_ptr = (uint64_t *)out;

    for (i = 0; i < WIDTH; i++) {
        uint64_t out0, out1, out2, out3, out4, out5, out6, out7;
        uint64_t tmp0, tmp1, tmp2, tmp3, tmp4;

        uint64_t in0 = out_ptr[0];
        uint64_t in1 = out_ptr[WIDTH];
        uint64_t in2 = out_ptr[WIDTH * 2];
        uint64_t in3 = out_ptr[WIDTH * 3];
        uint64_t in4 = out_ptr[WIDTH * 4];
        uint64_t in5 = out_ptr[WIDTH * 5];
        uint64_t in6 = out_ptr[WIDTH * 6];
        uint64_t in7 = out_ptr[WIDTH * 7];

        tmp0 = in2 ^ in4;
        tmp1 = in2 ^ in5;
        tmp2 = in3 ^ in6;
        out5 = tmp0 ^ in1;
        tmp3 = tmp0 ^ in0;
        out6 = tmp1 ^ in3;
        out0 = tmp1 ^ tmp3 ^ in7;
        out7 = tmp2 ^ in4;
        tmp4 = out5 ^ in6;
        out3 = tmp2 ^ tmp3;
        out2 = tmp4 ^ in5;
        out4 = tmp4 ^ out3;
        out1 = tmp3 ^ out2;

        out_ptr[0] = out0 ^ in_ptr[0];
        out_ptr[WIDTH] = out1 ^ in_ptr[WIDTH];
        out_ptr[WIDTH * 2] = out2 ^ in_ptr[WIDTH * 2];
        out_ptr[WIDTH * 3] = out3 ^ in_ptr[WIDTH * 3];
        out_ptr[WIDTH * 4] = out4 ^ in_ptr[WIDTH * 4];
        out_ptr[WIDTH * 5] = out5 ^ in_ptr[WIDTH * 5];
        out_ptr[WIDTH * 6] = out6 ^ in_ptr[WIDTH * 6];
        out_ptr[WIDTH * 7] = out7 ^ in_ptr[WIDTH * 7];

        in_ptr++;
        out_ptr++;
    }
}

static void gf8_muladd_1C(void *out, void *in)
{
    unsigned int i;
    uint64_t *in_ptr = (uint64_t *)in;
    uint64_t *out_ptr = (uint64_t *)out;

    for (i = 0; i < WIDTH; i++) {
        uint64_t out0, out1, out2, out3, out4, out5, out6, out7;
        uint64_t tmp0, tmp1, tmp2, tmp3, tmp4;

        uint64_t in0 = out_ptr[0];
        uint64_t in1 = out_ptr[WIDTH];
        uint64_t in2 = out_ptr[WIDTH * 2];
        uint64_t in3 = out_ptr[WIDTH * 3];
        uint64_t in4 = out_ptr[WIDTH * 4];
        uint64_t in5 = out_ptr[WIDTH * 5];
        uint64_t in6 = out_ptr[WIDTH * 6];
        uint64_t in7 = out_ptr[WIDTH * 7];

        tmp0 = in2 ^ in3;
        tmp1 = in4 ^ in6;
        tmp2 = in5 ^ in7;
        out6 = tmp0 ^ tmp1;
        out0 = tmp1 ^ in5;
        out1 = tmp2 ^ in6;
        tmp3 = tmp2 ^ in1;
        tmp4 = tmp2 ^ in4;
        out2 = tmp4 ^ in0;
        out7 = tmp4 ^ in3;
        out5 = tmp0 ^ tmp3;
        out3 = tmp3 ^ out2;
        out4 = out3 ^ in2 ^ in6;

        out_ptr[0] = out0 ^ in_ptr[0];
        out_ptr[WIDTH] = out1 ^ in_ptr[WIDTH];
        out_ptr[WIDTH * 2] = out2 ^ in_ptr[WIDTH * 2];
        out_ptr[WIDTH * 3] = out3 ^ in_ptr[WIDTH * 3];
        out_ptr[WIDTH * 4] = out4 ^ in_ptr[WIDTH * 4];
        out_ptr[WIDTH * 5] = out5 ^ in_ptr[WIDTH * 5];
        out_ptr[WIDTH * 6] = out6 ^ in_ptr[WIDTH * 6];
        out_ptr[WIDTH * 7] = out7 ^ in_ptr[WIDTH * 7];

        in_ptr++;
        out_ptr++;
    }
}

static void gf8_muladd_1D(void *out, void *in)
{
    unsigned int i;
    uint64_t *in_ptr = (uint64_t *)in;
    uint64_t *out_ptr = (uint64_t *)out;

    for (i = 0; i < WIDTH; i++) {
        uint64_t out0, out1, out2, out3, out4, out5, out6, out7;
        uint64_t tmp0, tmp1, tmp2, tmp3, tmp4;

        uint64_t in0 = out_ptr[0];
        uint64_t in1 = out_ptr[WIDTH];
        uint64_t in2 = out_ptr[WIDTH * 2];
        uint64_t in3 = out_ptr[WIDTH * 3];
        uint64_t in4 = out_ptr[WIDTH * 4];
        uint64_t in5 = out_ptr[WIDTH * 5];
        uint64_t in6 = out_ptr[WIDTH * 6];
        uint64_t in7 = out_ptr[WIDTH * 7];

        tmp0 = in1 ^ in3;
        tmp1 = in0 ^ in4;
        tmp2 = in3 ^ in4;
        tmp3 = in2 ^ in7;
        out3 = tmp0 ^ tmp1;
        out5 = tmp0 ^ tmp3;
        tmp4 = tmp1 ^ in5;
        out6 = tmp2 ^ in2;
        out7 = tmp2 ^ in5;
        out2 = tmp3 ^ tmp4;
        out4 = out3 ^ out6 ^ in6;
        out0 = tmp4 ^ in6;
        out1 = out2 ^ out4 ^ in4;

        out_ptr[0] = out0 ^ in_ptr[0];
        out_ptr[WIDTH] = out1 ^ in_ptr[WIDTH];
        out_ptr[WIDTH * 2] = out2 ^ in_ptr[WIDTH * 2];
        out_ptr[WIDTH * 3] = out3 ^ in_ptr[WIDTH * 3];
        out_ptr[WIDTH * 4] = out4 ^ in_ptr[WIDTH * 4];
        out_ptr[WIDTH * 5] = out5 ^ in_ptr[WIDTH * 5];
        out_ptr[WIDTH * 6] = out6 ^ in_ptr[WIDTH * 6];
        out_ptr[WIDTH * 7] = out7 ^ in_ptr[WIDTH * 7];

        in_ptr++;
        out_ptr++;
    }
}

static void gf8_muladd_1E(void *out, void *in)
{
    unsigned int i;
    uint64_t *in_ptr = (uint64_t *)in;
    uint64_t *out_ptr = (uint64_t *)out;

    for (i = 0; i < WIDTH; i++) {
        uint64_t out0, out1, out2, out3, out4, out5, out6, out7;
        uint64_t tmp0, tmp1, tmp2, tmp3;

        uint64_t in0 = out_ptr[0];
        uint64_t in1 = out_ptr[WIDTH];
        uint64_t in2 = out_ptr[WIDTH * 2];
        uint64_t in3 = out_ptr[WIDTH * 3];
        uint64_t in4 = out_ptr[WIDTH * 4];
        uint64_t in5 = out_ptr[WIDTH * 5];
        uint64_t in6 = out_ptr[WIDTH * 6];
        uint64_t in7 = out_ptr[WIDTH * 7];

        tmp0 = in0 ^ in4;
        tmp1 = in2 ^ in7;
        tmp2 = tmp0 ^ in1;
        out3 = tmp1 ^ tmp2;
        out2 = tmp2 ^ in5;
        out4 = out3 ^ in3 ^ in6;
        tmp3 = out4 ^ in7;
        out6 = tmp3 ^ out2 ^ in4;
        out7 = tmp1 ^ out6;
        out0 = out7 ^ in3;
        out1 = tmp0 ^ out0;
        out5 = tmp3 ^ out1;

        out_ptr[0] = out0 ^ in_ptr[0];
        out_ptr[WIDTH] = out1 ^ in_ptr[WIDTH];
        out_ptr[WIDTH * 2] = out2 ^ in_ptr[WIDTH * 2];
        out_ptr[WIDTH * 3] = out3 ^ in_ptr[WIDTH * 3];
        out_ptr[WIDTH * 4] = out4 ^ in_ptr[WIDTH * 4];
        out_ptr[WIDTH * 5] = out5 ^ in_ptr[WIDTH * 5];
        out_ptr[WIDTH * 6] = out6 ^ in_ptr[WIDTH * 6];
        out_ptr[WIDTH * 7] = out7 ^ in_ptr[WIDTH * 7];

        in_ptr++;
        out_ptr++;
    }
}

static void gf8_muladd_1F(void *out, void *in)
{
    unsigned int i;
    uint64_t *in_ptr = (uint64_t *)in;
    uint64_t *out_ptr = (uint64_t *)out;

    for (i = 0; i < WIDTH; i++) {
        uint64_t out0, out1, out2, out3, out4, out5, out6, out7;
        uint64_t tmp0, tmp1;

        uint64_t in0 = out_ptr[0];
        uint64_t in1 = out_ptr[WIDTH];
        uint64_t in2 = out_ptr[WIDTH * 2];
        uint64_t in3 = out_ptr[WIDTH * 3];
        uint64_t in4 = out_ptr[WIDTH * 4];
        uint64_t in5 = out_ptr[WIDTH * 5];
        uint64_t in6 = out_ptr[WIDTH * 6];
        uint64_t in7 = out_ptr[WIDTH * 7];

        tmp0 = in4 ^ in6;
        tmp1 = tmp0 ^ in5;
        out7 = tmp1 ^ in3;
        out0 = tmp1 ^ in0 ^ in7;
        out6 = out7 ^ in2 ^ in6;
        out1 = out0 ^ in1 ^ in4;
        out4 = out0 ^ out6 ^ in1;
        out3 = tmp0 ^ out4;
        out2 = out4 ^ out7 ^ in7;
        out5 = out3 ^ in0;

        out_ptr[0] = out0 ^ in_ptr[0];
        out_ptr[WIDTH] = out1 ^ in_ptr[WIDTH];
        out_ptr[WIDTH * 2] = out2 ^ in_ptr[WIDTH * 2];
        out_ptr[WIDTH * 3] = out3 ^ in_ptr[WIDTH * 3];
        out_ptr[WIDTH * 4] = out4 ^ in_ptr[WIDTH * 4];
        out_ptr[WIDTH * 5] = out5 ^ in_ptr[WIDTH * 5];
        out_ptr[WIDTH * 6] = out6 ^ in_ptr[WIDTH * 6];
        out_ptr[WIDTH * 7] = out7 ^ in_ptr[WIDTH * 7];

        in_ptr++;
        out_ptr++;
    }
}

static void gf8_muladd_20(void *out, void *in)
{
    unsigned int i;
    uint64_t *in_ptr = (uint64_t *)in;
    uint64_t *out_ptr = (uint64_t *)out;

    for (i = 0; i < WIDTH; i++) {
        uint64_t out0, out1, out2, out3, out4, out5, out6, out7;
        uint64_t tmp0, tmp1;

        uint64_t in0 = out_ptr[0];
        uint64_t in1 = out_ptr[WIDTH];
        uint64_t in2 = out_ptr[WIDTH * 2];
        uint64_t in3 = out_ptr[WIDTH * 3];
        uint64_t in4 = out_ptr[WIDTH * 4];
        uint64_t in5 = out_ptr[WIDTH * 5];
        uint64_t in6 = out_ptr[WIDTH * 6];
        uint64_t in7 = out_ptr[WIDTH * 7];

        out1 = in4;
        out0 = in3 ^ in7;
        tmp0 = in3 ^ in4;
        tmp1 = in6 ^ in7;
        out2 = out0 ^ in5;
        out4 = tmp0 ^ in5;
        out3 = tmp0 ^ tmp1;
        out7 = tmp1 ^ in2;
        out6 = tmp1 ^ in1 ^ in5;
        out5 = out2 ^ out3 ^ in0;

        out_ptr[0] = out0 ^ in_ptr[0];
        out_ptr[WIDTH] = out1 ^ in_ptr[WIDTH];
        out_ptr[WIDTH * 2] = out2 ^ in_ptr[WIDTH * 2];
        out_ptr[WIDTH * 3] = out3 ^ in_ptr[WIDTH * 3];
        out_ptr[WIDTH * 4] = out4 ^ in_ptr[WIDTH * 4];
        out_ptr[WIDTH * 5] = out5 ^ in_ptr[WIDTH * 5];
        out_ptr[WIDTH * 6] = out6 ^ in_ptr[WIDTH * 6];
        out_ptr[WIDTH * 7] = out7 ^ in_ptr[WIDTH * 7];

        in_ptr++;
        out_ptr++;
    }
}

static void gf8_muladd_21(void *out, void *in)
{
    unsigned int i;
    uint64_t *in_ptr = (uint64_t *)in;
    uint64_t *out_ptr = (uint64_t *)out;

    for (i = 0; i < WIDTH; i++) {
        uint64_t out0, out1, out2, out3, out4, out5, out6, out7;
        uint64_t tmp0;

        uint64_t in0 = out_ptr[0];
        uint64_t in1 = out_ptr[WIDTH];
        uint64_t in2 = out_ptr[WIDTH * 2];
        uint64_t in3 = out_ptr[WIDTH * 3];
        uint64_t in4 = out_ptr[WIDTH * 4];
        uint64_t in5 = out_ptr[WIDTH * 5];
        uint64_t in6 = out_ptr[WIDTH * 6];
        uint64_t in7 = out_ptr[WIDTH * 7];

        out1 = in1 ^ in4;
        tmp0 = in4 ^ in6;
        out4 = in3 ^ in5;
        out7 = in2 ^ in6;
        out0 = in0 ^ in3 ^ in7;
        out6 = in1 ^ in5 ^ in7;
        out3 = tmp0 ^ in7;
        out5 = tmp0 ^ in0;
        out2 = out4 ^ in2 ^ in7;

        out_ptr[0] = out0 ^ in_ptr[0];
        out_ptr[WIDTH] = out1 ^ in_ptr[WIDTH];
        out_ptr[WIDTH * 2] = out2 ^ in_ptr[WIDTH * 2];
        out_ptr[WIDTH * 3] = out3 ^ in_ptr[WIDTH * 3];
        out_ptr[WIDTH * 4] = out4 ^ in_ptr[WIDTH * 4];
        out_ptr[WIDTH * 5] = out5 ^ in_ptr[WIDTH * 5];
        out_ptr[WIDTH * 6] = out6 ^ in_ptr[WIDTH * 6];
        out_ptr[WIDTH * 7] = out7 ^ in_ptr[WIDTH * 7];

        in_ptr++;
        out_ptr++;
    }
}

static void gf8_muladd_22(void *out, void *in)
{
    unsigned int i;
    uint64_t *in_ptr = (uint64_t *)in;
    uint64_t *out_ptr = (uint64_t *)out;

    for (i = 0; i < WIDTH; i++) {
        uint64_t out0, out1, out2, out3, out4, out5, out6, out7;

        uint64_t in0 = out_ptr[0];
        uint64_t in1 = out_ptr[WIDTH];
        uint64_t in2 = out_ptr[WIDTH * 2];
        uint64_t in3 = out_ptr[WIDTH * 3];
        uint64_t in4 = out_ptr[WIDTH * 4];
        uint64_t in5 = out_ptr[WIDTH * 5];
        uint64_t in6 = out_ptr[WIDTH * 6];
        uint64_t in7 = out_ptr[WIDTH * 7];

        out0 = in3;
        out1 = in0 ^ in4;
        out7 = in2 ^ in7;
        out4 = in4 ^ in5 ^ in7;
        out5 = in0 ^ in5 ^ in6;
        out6 = in1 ^ in6 ^ in7;
        out3 = in2 ^ in3 ^ in4 ^ in6;
        out2 = in1 ^ in3 ^ in5;

        out_ptr[0] = out0 ^ in_ptr[0];
        out_ptr[WIDTH] = out1 ^ in_ptr[WIDTH];
        out_ptr[WIDTH * 2] = out2 ^ in_ptr[WIDTH * 2];
        out_ptr[WIDTH * 3] = out3 ^ in_ptr[WIDTH * 3];
        out_ptr[WIDTH * 4] = out4 ^ in_ptr[WIDTH * 4];
        out_ptr[WIDTH * 5] = out5 ^ in_ptr[WIDTH * 5];
        out_ptr[WIDTH * 6] = out6 ^ in_ptr[WIDTH * 6];
        out_ptr[WIDTH * 7] = out7 ^ in_ptr[WIDTH * 7];

        in_ptr++;
        out_ptr++;
    }
}

static void gf8_muladd_23(void *out, void *in)
{
    unsigned int i;
    uint64_t *in_ptr = (uint64_t *)in;
    uint64_t *out_ptr = (uint64_t *)out;

    for (i = 0; i < WIDTH; i++) {
        uint64_t out0, out1, out2, out3, out4, out5, out6, out7;

        uint64_t in0 = out_ptr[0];
        uint64_t in1 = out_ptr[WIDTH];
        uint64_t in2 = out_ptr[WIDTH * 2];
        uint64_t in3 = out_ptr[WIDTH * 3];
        uint64_t in4 = out_ptr[WIDTH * 4];
        uint64_t in5 = out_ptr[WIDTH * 5];
        uint64_t in6 = out_ptr[WIDTH * 6];
        uint64_t in7 = out_ptr[WIDTH * 7];

        out7 = in2;
        out0 = in0 ^ in3;
        out4 = in5 ^ in7;
        out5 = in0 ^ in6;
        out6 = in1 ^ in7;
        out3 = in2 ^ in4 ^ in6;
        out1 = in0 ^ in1 ^ in4;
        out2 = out4 ^ out6 ^ in2 ^ in3;

        out_ptr[0] = out0 ^ in_ptr[0];
        out_ptr[WIDTH] = out1 ^ in_ptr[WIDTH];
        out_ptr[WIDTH * 2] = out2 ^ in_ptr[WIDTH * 2];
        out_ptr[WIDTH * 3] = out3 ^ in_ptr[WIDTH * 3];
        out_ptr[WIDTH * 4] = out4 ^ in_ptr[WIDTH * 4];
        out_ptr[WIDTH * 5] = out5 ^ in_ptr[WIDTH * 5];
        out_ptr[WIDTH * 6] = out6 ^ in_ptr[WIDTH * 6];
        out_ptr[WIDTH * 7] = out7 ^ in_ptr[WIDTH * 7];

        in_ptr++;
        out_ptr++;
    }
}

static void gf8_muladd_24(void *out, void *in)
{
    unsigned int i;
    uint64_t *in_ptr = (uint64_t *)in;
    uint64_t *out_ptr = (uint64_t *)out;

    for (i = 0; i < WIDTH; i++) {
        uint64_t out0, out1, out2, out3, out4, out5, out6, out7;
        uint64_t tmp0, tmp1;

        uint64_t in0 = out_ptr[0];
        uint64_t in1 = out_ptr[WIDTH];
        uint64_t in2 = out_ptr[WIDTH * 2];
        uint64_t in3 = out_ptr[WIDTH * 3];
        uint64_t in4 = out_ptr[WIDTH * 4];
        uint64_t in5 = out_ptr[WIDTH * 5];
        uint64_t in6 = out_ptr[WIDTH * 6];
        uint64_t in7 = out_ptr[WIDTH * 7];

        out1 = in4 ^ in7;
        tmp0 = in3 ^ in4;
        out0 = in3 ^ in6 ^ in7;
        out3 = tmp0 ^ in1;
        tmp1 = out0 ^ in5;
        out6 = tmp1 ^ out3;
        out2 = tmp1 ^ in0;
        out7 = tmp1 ^ in2 ^ in3;
        out5 = out2 ^ in4;
        out4 = tmp0 ^ out7;

        out_ptr[0] = out0 ^ in_ptr[0];
        out_ptr[WIDTH] = out1 ^ in_ptr[WIDTH];
        out_ptr[WIDTH * 2] = out2 ^ in_ptr[WIDTH * 2];
        out_ptr[WIDTH * 3] = out3 ^ in_ptr[WIDTH * 3];
        out_ptr[WIDTH * 4] = out4 ^ in_ptr[WIDTH * 4];
        out_ptr[WIDTH * 5] = out5 ^ in_ptr[WIDTH * 5];
        out_ptr[WIDTH * 6] = out6 ^ in_ptr[WIDTH * 6];
        out_ptr[WIDTH * 7] = out7 ^ in_ptr[WIDTH * 7];

        in_ptr++;
        out_ptr++;
    }
}

static void gf8_muladd_25(void *out, void *in)
{
    unsigned int i;
    uint64_t *in_ptr = (uint64_t *)in;
    uint64_t *out_ptr = (uint64_t *)out;

    for (i = 0; i < WIDTH; i++) {
        uint64_t out0, out1, out2, out3, out4, out5, out6, out7;
        uint64_t tmp0;

        uint64_t in0 = out_ptr[0];
        uint64_t in1 = out_ptr[WIDTH];
        uint64_t in2 = out_ptr[WIDTH * 2];
        uint64_t in3 = out_ptr[WIDTH * 3];
        uint64_t in4 = out_ptr[WIDTH * 4];
        uint64_t in5 = out_ptr[WIDTH * 5];
        uint64_t in6 = out_ptr[WIDTH * 6];
        uint64_t in7 = out_ptr[WIDTH * 7];

        out3 = in1 ^ in4;
        tmp0 = in2 ^ in5;
        out1 = out3 ^ in7;
        out7 = tmp0 ^ in6;
        out6 = out1 ^ in5;
        out4 = out7 ^ in3 ^ in7;
        out2 = out4 ^ in0;
        out0 = tmp0 ^ out2;
        out5 = out0 ^ in4;

        out_ptr[0] = out0 ^ in_ptr[0];
        out_ptr[WIDTH] = out1 ^ in_ptr[WIDTH];
        out_ptr[WIDTH * 2] = out2 ^ in_ptr[WIDTH * 2];
        out_ptr[WIDTH * 3] = out3 ^ in_ptr[WIDTH * 3];
        out_ptr[WIDTH * 4] = out4 ^ in_ptr[WIDTH * 4];
        out_ptr[WIDTH * 5] = out5 ^ in_ptr[WIDTH * 5];
        out_ptr[WIDTH * 6] = out6 ^ in_ptr[WIDTH * 6];
        out_ptr[WIDTH * 7] = out7 ^ in_ptr[WIDTH * 7];

        in_ptr++;
        out_ptr++;
    }
}

static void gf8_muladd_26(void *out, void *in)
{
    unsigned int i;
    uint64_t *in_ptr = (uint64_t *)in;
    uint64_t *out_ptr = (uint64_t *)out;

    for (i = 0; i < WIDTH; i++) {
        uint64_t out0, out1, out2, out3, out4, out5, out6, out7;
        uint64_t tmp0, tmp1, tmp2;

        uint64_t in0 = out_ptr[0];
        uint64_t in1 = out_ptr[WIDTH];
        uint64_t in2 = out_ptr[WIDTH * 2];
        uint64_t in3 = out_ptr[WIDTH * 3];
        uint64_t in4 = out_ptr[WIDTH * 4];
        uint64_t in5 = out_ptr[WIDTH * 5];
        uint64_t in6 = out_ptr[WIDTH * 6];
        uint64_t in7 = out_ptr[WIDTH * 7];

        out0 = in3 ^ in6;
        tmp0 = in4 ^ in7;
        out7 = in2 ^ in5 ^ in7;
        tmp1 = out0 ^ in0 ^ in5;
        out1 = tmp0 ^ in0;
        tmp2 = tmp0 ^ in6;
        out2 = tmp1 ^ in1;
        out5 = tmp1 ^ in7;
        out6 = tmp2 ^ in1;
        out4 = tmp2 ^ out7;
        out3 = out0 ^ out6 ^ in2;

        out_ptr[0] = out0 ^ in_ptr[0];
        out_ptr[WIDTH] = out1 ^ in_ptr[WIDTH];
        out_ptr[WIDTH * 2] = out2 ^ in_ptr[WIDTH * 2];
        out_ptr[WIDTH * 3] = out3 ^ in_ptr[WIDTH * 3];
        out_ptr[WIDTH * 4] = out4 ^ in_ptr[WIDTH * 4];
        out_ptr[WIDTH * 5] = out5 ^ in_ptr[WIDTH * 5];
        out_ptr[WIDTH * 6] = out6 ^ in_ptr[WIDTH * 6];
        out_ptr[WIDTH * 7] = out7 ^ in_ptr[WIDTH * 7];

        in_ptr++;
        out_ptr++;
    }
}

static void gf8_muladd_27(void *out, void *in)
{
    unsigned int i;
    uint64_t *in_ptr = (uint64_t *)in;
    uint64_t *out_ptr = (uint64_t *)out;

    for (i = 0; i < WIDTH; i++) {
        uint64_t out0, out1, out2, out3, out4, out5, out6, out7;

        uint64_t in0 = out_ptr[0];
        uint64_t in1 = out_ptr[WIDTH];
        uint64_t in2 = out_ptr[WIDTH * 2];
        uint64_t in3 = out_ptr[WIDTH * 3];
        uint64_t in4 = out_ptr[WIDTH * 4];
        uint64_t in5 = out_ptr[WIDTH * 5];
        uint64_t in6 = out_ptr[WIDTH * 6];
        uint64_t in7 = out_ptr[WIDTH * 7];

        out7 = in2 ^ in5;
        out0 = in0 ^ in3 ^ in6;
        out6 = in1 ^ in4 ^ in7;
        out4 = out7 ^ in6;
        out2 = out0 ^ out7 ^ in1;
        out5 = out0 ^ in7;
        out1 = out6 ^ in0;
        out3 = out6 ^ in2;

        out_ptr[0] = out0 ^ in_ptr[0];
        out_ptr[WIDTH] = out1 ^ in_ptr[WIDTH];
        out_ptr[WIDTH * 2] = out2 ^ in_ptr[WIDTH * 2];
        out_ptr[WIDTH * 3] = out3 ^ in_ptr[WIDTH * 3];
        out_ptr[WIDTH * 4] = out4 ^ in_ptr[WIDTH * 4];
        out_ptr[WIDTH * 5] = out5 ^ in_ptr[WIDTH * 5];
        out_ptr[WIDTH * 6] = out6 ^ in_ptr[WIDTH * 6];
        out_ptr[WIDTH * 7] = out7 ^ in_ptr[WIDTH * 7];

        in_ptr++;
        out_ptr++;
    }
}

static void gf8_muladd_28(void *out, void *in)
{
    unsigned int i;
    uint64_t *in_ptr = (uint64_t *)in;
    uint64_t *out_ptr = (uint64_t *)out;

    for (i = 0; i < WIDTH; i++) {
        uint64_t out0, out1, out2, out3, out4, out5, out6, out7;
        uint64_t tmp0, tmp1, tmp2;

        uint64_t in0 = out_ptr[0];
        uint64_t in1 = out_ptr[WIDTH];
        uint64_t in2 = out_ptr[WIDTH * 2];
        uint64_t in3 = out_ptr[WIDTH * 3];
        uint64_t in4 = out_ptr[WIDTH * 4];
        uint64_t in5 = out_ptr[WIDTH * 5];
        uint64_t in6 = out_ptr[WIDTH * 6];
        uint64_t in7 = out_ptr[WIDTH * 7];

        out2 = in3;
        out1 = in4 ^ in6;
        out0 = in3 ^ in5 ^ in7;
        tmp0 = out1 ^ in7;
        tmp1 = out0 ^ in4;
        out7 = tmp0 ^ in2;
        tmp2 = tmp0 ^ in1;
        out3 = tmp1 ^ in0;
        out6 = tmp1 ^ tmp2;
        out4 = tmp2 ^ in3;
        out5 = out3 ^ in2 ^ in3;

        out_ptr[0] = out0 ^ in_ptr[0];
        out_ptr[WIDTH] = out1 ^ in_ptr[WIDTH];
        out_ptr[WIDTH * 2] = out2 ^ in_ptr[WIDTH * 2];
        out_ptr[WIDTH * 3] = out3 ^ in_ptr[WIDTH * 3];
        out_ptr[WIDTH * 4] = out4 ^ in_ptr[WIDTH * 4];
        out_ptr[WIDTH * 5] = out5 ^ in_ptr[WIDTH * 5];
        out_ptr[WIDTH * 6] = out6 ^ in_ptr[WIDTH * 6];
        out_ptr[WIDTH * 7] = out7 ^ in_ptr[WIDTH * 7];

        in_ptr++;
        out_ptr++;
    }
}

static void gf8_muladd_29(void *out, void *in)
{
    unsigned int i;
    uint64_t *in_ptr = (uint64_t *)in;
    uint64_t *out_ptr = (uint64_t *)out;

    for (i = 0; i < WIDTH; i++) {
        uint64_t out0, out1, out2, out3, out4, out5, out6, out7;
        uint64_t tmp0, tmp1, tmp2;

        uint64_t in0 = out_ptr[0];
        uint64_t in1 = out_ptr[WIDTH];
        uint64_t in2 = out_ptr[WIDTH * 2];
        uint64_t in3 = out_ptr[WIDTH * 3];
        uint64_t in4 = out_ptr[WIDTH * 4];
        uint64_t in5 = out_ptr[WIDTH * 5];
        uint64_t in6 = out_ptr[WIDTH * 6];
        uint64_t in7 = out_ptr[WIDTH * 7];

        out2 = in2 ^ in3;
        tmp0 = in1 ^ in3;
        tmp1 = in4 ^ in6;
        tmp2 = in0 ^ in4 ^ in7;
        out6 = tmp0 ^ in5;
        out4 = tmp0 ^ in6 ^ in7;
        out1 = tmp1 ^ in1;
        out7 = tmp1 ^ in2;
        out3 = tmp2 ^ in5;
        out5 = tmp2 ^ in2;
        out0 = out3 ^ in3 ^ in4;

        out_ptr[0] = out0 ^ in_ptr[0];
        out_ptr[WIDTH] = out1 ^ in_ptr[WIDTH];
        out_ptr[WIDTH * 2] = out2 ^ in_ptr[WIDTH * 2];
        out_ptr[WIDTH * 3] = out3 ^ in_ptr[WIDTH * 3];
        out_ptr[WIDTH * 4] = out4 ^ in_ptr[WIDTH * 4];
        out_ptr[WIDTH * 5] = out5 ^ in_ptr[WIDTH * 5];
        out_ptr[WIDTH * 6] = out6 ^ in_ptr[WIDTH * 6];
        out_ptr[WIDTH * 7] = out7 ^ in_ptr[WIDTH * 7];

        in_ptr++;
        out_ptr++;
    }
}

static void gf8_muladd_2A(void *out, void *in)
{
    unsigned int i;
    uint64_t *in_ptr = (uint64_t *)in;
    uint64_t *out_ptr = (uint64_t *)out;

    for (i = 0; i < WIDTH; i++) {
        uint64_t out0, out1, out2, out3, out4, out5, out6, out7;
        uint64_t tmp0, tmp1;

        uint64_t in0 = out_ptr[0];
        uint64_t in1 = out_ptr[WIDTH];
        uint64_t in2 = out_ptr[WIDTH * 2];
        uint64_t in3 = out_ptr[WIDTH * 3];
        uint64_t in4 = out_ptr[WIDTH * 4];
        uint64_t in5 = out_ptr[WIDTH * 5];
        uint64_t in6 = out_ptr[WIDTH * 6];
        uint64_t in7 = out_ptr[WIDTH * 7];

        out0 = in3 ^ in5;
        tmp0 = in1 ^ in3;
        tmp1 = in0 ^ in4;
        out7 = in2 ^ in4 ^ in7;
        out3 = tmp1 ^ out0 ^ in2;
        out2 = tmp0 ^ in7;
        out6 = tmp0 ^ in6;
        out1 = tmp1 ^ in6;
        out5 = tmp1 ^ out7 ^ in5;
        out4 = out1 ^ in0 ^ in1;

        out_ptr[0] = out0 ^ in_ptr[0];
        out_ptr[WIDTH] = out1 ^ in_ptr[WIDTH];
        out_ptr[WIDTH * 2] = out2 ^ in_ptr[WIDTH * 2];
        out_ptr[WIDTH * 3] = out3 ^ in_ptr[WIDTH * 3];
        out_ptr[WIDTH * 4] = out4 ^ in_ptr[WIDTH * 4];
        out_ptr[WIDTH * 5] = out5 ^ in_ptr[WIDTH * 5];
        out_ptr[WIDTH * 6] = out6 ^ in_ptr[WIDTH * 6];
        out_ptr[WIDTH * 7] = out7 ^ in_ptr[WIDTH * 7];

        in_ptr++;
        out_ptr++;
    }
}

static void gf8_muladd_2B(void *out, void *in)
{
    unsigned int i;
    uint64_t *in_ptr = (uint64_t *)in;
    uint64_t *out_ptr = (uint64_t *)out;

    for (i = 0; i < WIDTH; i++) {
        uint64_t out0, out1, out2, out3, out4, out5, out6, out7;
        uint64_t tmp0, tmp1;

        uint64_t in0 = out_ptr[0];
        uint64_t in1 = out_ptr[WIDTH];
        uint64_t in2 = out_ptr[WIDTH * 2];
        uint64_t in3 = out_ptr[WIDTH * 3];
        uint64_t in4 = out_ptr[WIDTH * 4];
        uint64_t in5 = out_ptr[WIDTH * 5];
        uint64_t in6 = out_ptr[WIDTH * 6];
        uint64_t in7 = out_ptr[WIDTH * 7];

        out4 = in1 ^ in6;
        out7 = in2 ^ in4;
        tmp0 = in0 ^ in5;
        tmp1 = in2 ^ in7;
        out6 = in1 ^ in3;
        out1 = out4 ^ in0 ^ in4;
        out3 = tmp0 ^ out7;
        out0 = tmp0 ^ in3;
        out5 = tmp1 ^ in0;
        out2 = tmp1 ^ out6;

        out_ptr[0] = out0 ^ in_ptr[0];
        out_ptr[WIDTH] = out1 ^ in_ptr[WIDTH];
        out_ptr[WIDTH * 2] = out2 ^ in_ptr[WIDTH * 2];
        out_ptr[WIDTH * 3] = out3 ^ in_ptr[WIDTH * 3];
        out_ptr[WIDTH * 4] = out4 ^ in_ptr[WIDTH * 4];
        out_ptr[WIDTH * 5] = out5 ^ in_ptr[WIDTH * 5];
        out_ptr[WIDTH * 6] = out6 ^ in_ptr[WIDTH * 6];
        out_ptr[WIDTH * 7] = out7 ^ in_ptr[WIDTH * 7];

        in_ptr++;
        out_ptr++;
    }
}

static void gf8_muladd_2C(void *out, void *in)
{
    unsigned int i;
    uint64_t *in_ptr = (uint64_t *)in;
    uint64_t *out_ptr = (uint64_t *)out;

    for (i = 0; i < WIDTH; i++) {
        uint64_t out0, out1, out2, out3, out4, out5, out6, out7;
        uint64_t tmp0, tmp1, tmp2, tmp3;

        uint64_t in0 = out_ptr[0];
        uint64_t in1 = out_ptr[WIDTH];
        uint64_t in2 = out_ptr[WIDTH * 2];
        uint64_t in3 = out_ptr[WIDTH * 3];
        uint64_t in4 = out_ptr[WIDTH * 4];
        uint64_t in5 = out_ptr[WIDTH * 5];
        uint64_t in6 = out_ptr[WIDTH * 6];
        uint64_t in7 = out_ptr[WIDTH * 7];

        tmp0 = in2 ^ in5;
        tmp1 = in2 ^ in3 ^ in4;
        tmp2 = tmp0 ^ in6;
        out4 = tmp1 ^ in1;
        out5 = tmp1 ^ in0 ^ in5;
        tmp3 = tmp2 ^ in4;
        out6 = tmp2 ^ out4;
        out7 = tmp3 ^ in7;
        out2 = tmp3 ^ out5;
        out3 = out6 ^ in0;
        out0 = tmp1 ^ out7;
        out1 = tmp0 ^ out7;

        out_ptr[0] = out0 ^ in_ptr[0];
        out_ptr[WIDTH] = out1 ^ in_ptr[WIDTH];
        out_ptr[WIDTH * 2] = out2 ^ in_ptr[WIDTH * 2];
        out_ptr[WIDTH * 3] = out3 ^ in_ptr[WIDTH * 3];
        out_ptr[WIDTH * 4] = out4 ^ in_ptr[WIDTH * 4];
        out_ptr[WIDTH * 5] = out5 ^ in_ptr[WIDTH * 5];
        out_ptr[WIDTH * 6] = out6 ^ in_ptr[WIDTH * 6];
        out_ptr[WIDTH * 7] = out7 ^ in_ptr[WIDTH * 7];

        in_ptr++;
        out_ptr++;
    }
}

static void gf8_muladd_2D(void *out, void *in)
{
    unsigned int i;
    uint64_t *in_ptr = (uint64_t *)in;
    uint64_t *out_ptr = (uint64_t *)out;

    for (i = 0; i < WIDTH; i++) {
        uint64_t out0, out1, out2, out3, out4, out5, out6, out7;
        uint64_t tmp0, tmp1, tmp2, tmp3;

        uint64_t in0 = out_ptr[0];
        uint64_t in1 = out_ptr[WIDTH];
        uint64_t in2 = out_ptr[WIDTH * 2];
        uint64_t in3 = out_ptr[WIDTH * 3];
        uint64_t in4 = out_ptr[WIDTH * 4];
        uint64_t in5 = out_ptr[WIDTH * 5];
        uint64_t in6 = out_ptr[WIDTH * 6];
        uint64_t in7 = out_ptr[WIDTH * 7];

        tmp0 = in2 ^ in3;
        out4 = tmp0 ^ in1;
        tmp1 = tmp0 ^ in0;
        out2 = tmp1 ^ in6;
        out5 = tmp1 ^ in4;
        tmp2 = out2 ^ in2;
        tmp3 = tmp2 ^ in5;
        out0 = tmp3 ^ in7;
        out7 = tmp3 ^ out5;
        out6 = out4 ^ out7 ^ in6;
        out3 = tmp2 ^ out6;
        out1 = out0 ^ out6 ^ in0;

        out_ptr[0] = out0 ^ in_ptr[0];
        out_ptr[WIDTH] = out1 ^ in_ptr[WIDTH];
        out_ptr[WIDTH * 2] = out2 ^ in_ptr[WIDTH * 2];
        out_ptr[WIDTH * 3] = out3 ^ in_ptr[WIDTH * 3];
        out_ptr[WIDTH * 4] = out4 ^ in_ptr[WIDTH * 4];
        out_ptr[WIDTH * 5] = out5 ^ in_ptr[WIDTH * 5];
        out_ptr[WIDTH * 6] = out6 ^ in_ptr[WIDTH * 6];
        out_ptr[WIDTH * 7] = out7 ^ in_ptr[WIDTH * 7];

        in_ptr++;
        out_ptr++;
    }
}

static void gf8_muladd_2E(void *out, void *in)
{
    unsigned int i;
    uint64_t *in_ptr = (uint64_t *)in;
    uint64_t *out_ptr = (uint64_t *)out;

    for (i = 0; i < WIDTH; i++) {
        uint64_t out0, out1, out2, out3, out4, out5, out6, out7;
        uint64_t tmp0, tmp1, tmp2;

        uint64_t in0 = out_ptr[0];
        uint64_t in1 = out_ptr[WIDTH];
        uint64_t in2 = out_ptr[WIDTH * 2];
        uint64_t in3 = out_ptr[WIDTH * 3];
        uint64_t in4 = out_ptr[WIDTH * 4];
        uint64_t in5 = out_ptr[WIDTH * 5];
        uint64_t in6 = out_ptr[WIDTH * 6];
        uint64_t in7 = out_ptr[WIDTH * 7];

        tmp0 = in4 ^ in7;
        out0 = in3 ^ in5 ^ in6;
        tmp1 = tmp0 ^ in0;
        tmp2 = tmp0 ^ in2;
        out1 = tmp1 ^ in6;
        out4 = tmp2 ^ in1;
        out7 = tmp2 ^ in5;
        out3 = out0 ^ out4 ^ in0;
        out2 = out3 ^ out7 ^ in7;
        out6 = tmp1 ^ out2;
        out5 = tmp1 ^ out7 ^ in3;

        out_ptr[0] = out0 ^ in_ptr[0];
        out_ptr[WIDTH] = out1 ^ in_ptr[WIDTH];
        out_ptr[WIDTH * 2] = out2 ^ in_ptr[WIDTH * 2];
        out_ptr[WIDTH * 3] = out3 ^ in_ptr[WIDTH * 3];
        out_ptr[WIDTH * 4] = out4 ^ in_ptr[WIDTH * 4];
        out_ptr[WIDTH * 5] = out5 ^ in_ptr[WIDTH * 5];
        out_ptr[WIDTH * 6] = out6 ^ in_ptr[WIDTH * 6];
        out_ptr[WIDTH * 7] = out7 ^ in_ptr[WIDTH * 7];

        in_ptr++;
        out_ptr++;
    }
}

static void gf8_muladd_2F(void *out, void *in)
{
    unsigned int i;
    uint64_t *in_ptr = (uint64_t *)in;
    uint64_t *out_ptr = (uint64_t *)out;

    for (i = 0; i < WIDTH; i++) {
        uint64_t out0, out1, out2, out3, out4, out5, out6, out7;
        uint64_t tmp0, tmp1, tmp2;

        uint64_t in0 = out_ptr[0];
        uint64_t in1 = out_ptr[WIDTH];
        uint64_t in2 = out_ptr[WIDTH * 2];
        uint64_t in3 = out_ptr[WIDTH * 3];
        uint64_t in4 = out_ptr[WIDTH * 4];
        uint64_t in5 = out_ptr[WIDTH * 5];
        uint64_t in6 = out_ptr[WIDTH * 6];
        uint64_t in7 = out_ptr[WIDTH * 7];

        tmp0 = in0 ^ in3;
        tmp1 = in2 ^ in5;
        out4 = in1 ^ in2 ^ in7;
        out6 = in1 ^ in3 ^ in4;
        out5 = tmp0 ^ in2;
        tmp2 = tmp0 ^ in6;
        out7 = tmp1 ^ in4;
        out0 = tmp2 ^ in5;
        out2 = tmp2 ^ out4;
        out1 = tmp2 ^ out6 ^ in7;
        out3 = tmp1 ^ out1;

        out_ptr[0] = out0 ^ in_ptr[0];
        out_ptr[WIDTH] = out1 ^ in_ptr[WIDTH];
        out_ptr[WIDTH * 2] = out2 ^ in_ptr[WIDTH * 2];
        out_ptr[WIDTH * 3] = out3 ^ in_ptr[WIDTH * 3];
        out_ptr[WIDTH * 4] = out4 ^ in_ptr[WIDTH * 4];
        out_ptr[WIDTH * 5] = out5 ^ in_ptr[WIDTH * 5];
        out_ptr[WIDTH * 6] = out6 ^ in_ptr[WIDTH * 6];
        out_ptr[WIDTH * 7] = out7 ^ in_ptr[WIDTH * 7];

        in_ptr++;
        out_ptr++;
    }
}

static void gf8_muladd_30(void *out, void *in)
{
    unsigned int i;
    uint64_t *in_ptr = (uint64_t *)in;
    uint64_t *out_ptr = (uint64_t *)out;

    for (i = 0; i < WIDTH; i++) {
        uint64_t out0, out1, out2, out3, out4, out5, out6, out7;
        uint64_t tmp0, tmp1;

        uint64_t in0 = out_ptr[0];
        uint64_t in1 = out_ptr[WIDTH];
        uint64_t in2 = out_ptr[WIDTH * 2];
        uint64_t in3 = out_ptr[WIDTH * 3];
        uint64_t in4 = out_ptr[WIDTH * 4];
        uint64_t in5 = out_ptr[WIDTH * 5];
        uint64_t in6 = out_ptr[WIDTH * 6];
        uint64_t in7 = out_ptr[WIDTH * 7];

        out1 = in4 ^ in5;
        tmp0 = in3 ^ in6;
        tmp1 = in4 ^ in7;
        out6 = in1 ^ in2 ^ in5;
        out3 = tmp0 ^ in5;
        out4 = tmp0 ^ in0;
        out7 = tmp0 ^ in2;
        out0 = tmp1 ^ in3;
        out2 = tmp1 ^ out3;
        out5 = tmp1 ^ in0 ^ in1;

        out_ptr[0] = out0 ^ in_ptr[0];
        out_ptr[WIDTH] = out1 ^ in_ptr[WIDTH];
        out_ptr[WIDTH * 2] = out2 ^ in_ptr[WIDTH * 2];
        out_ptr[WIDTH * 3] = out3 ^ in_ptr[WIDTH * 3];
        out_ptr[WIDTH * 4] = out4 ^ in_ptr[WIDTH * 4];
        out_ptr[WIDTH * 5] = out5 ^ in_ptr[WIDTH * 5];
        out_ptr[WIDTH * 6] = out6 ^ in_ptr[WIDTH * 6];
        out_ptr[WIDTH * 7] = out7 ^ in_ptr[WIDTH * 7];

        in_ptr++;
        out_ptr++;
    }
}

static void gf8_muladd_31(void *out, void *in)
{
    unsigned int i;
    uint64_t *in_ptr = (uint64_t *)in;
    uint64_t *out_ptr = (uint64_t *)out;

    for (i = 0; i < WIDTH; i++) {
        uint64_t out0, out1, out2, out3, out4, out5, out6, out7;
        uint64_t tmp0, tmp1, tmp2;

        uint64_t in0 = out_ptr[0];
        uint64_t in1 = out_ptr[WIDTH];
        uint64_t in2 = out_ptr[WIDTH * 2];
        uint64_t in3 = out_ptr[WIDTH * 3];
        uint64_t in4 = out_ptr[WIDTH * 4];
        uint64_t in5 = out_ptr[WIDTH * 5];
        uint64_t in6 = out_ptr[WIDTH * 6];
        uint64_t in7 = out_ptr[WIDTH * 7];

        out3 = in5 ^ in6;
        tmp0 = in4 ^ in5;
        tmp1 = in0 ^ in3 ^ in4;
        tmp2 = out3 ^ in2;
        out1 = tmp0 ^ in1;
        out0 = tmp1 ^ in7;
        out4 = tmp1 ^ in6;
        out6 = tmp2 ^ in1;
        out2 = tmp2 ^ out0 ^ in0;
        out5 = out1 ^ in0 ^ in7;
        out7 = tmp0 ^ out2;

        out_ptr[0] = out0 ^ in_ptr[0];
        out_ptr[WIDTH] = out1 ^ in_ptr[WIDTH];
        out_ptr[WIDTH * 2] = out2 ^ in_ptr[WIDTH * 2];
        out_ptr[WIDTH * 3] = out3 ^ in_ptr[WIDTH * 3];
        out_ptr[WIDTH * 4] = out4 ^ in_ptr[WIDTH * 4];
        out_ptr[WIDTH * 5] = out5 ^ in_ptr[WIDTH * 5];
        out_ptr[WIDTH * 6] = out6 ^ in_ptr[WIDTH * 6];
        out_ptr[WIDTH * 7] = out7 ^ in_ptr[WIDTH * 7];

        in_ptr++;
        out_ptr++;
    }
}

static void gf8_muladd_32(void *out, void *in)
{
    unsigned int i;
    uint64_t *in_ptr = (uint64_t *)in;
    uint64_t *out_ptr = (uint64_t *)out;

    for (i = 0; i < WIDTH; i++) {
        uint64_t out0, out1, out2, out3, out4, out5, out6, out7;
        uint64_t tmp0, tmp1;

        uint64_t in0 = out_ptr[0];
        uint64_t in1 = out_ptr[WIDTH];
        uint64_t in2 = out_ptr[WIDTH * 2];
        uint64_t in3 = out_ptr[WIDTH * 3];
        uint64_t in4 = out_ptr[WIDTH * 4];
        uint64_t in5 = out_ptr[WIDTH * 5];
        uint64_t in6 = out_ptr[WIDTH * 6];
        uint64_t in7 = out_ptr[WIDTH * 7];

        out0 = in3 ^ in4;
        out7 = in2 ^ in3;
        tmp0 = in5 ^ in6;
        tmp1 = in0 ^ in7;
        out6 = in1 ^ in2;
        out1 = in0 ^ in4 ^ in5;
        out2 = tmp0 ^ out0 ^ in1;
        out3 = tmp0 ^ out7 ^ in7;
        out4 = tmp1 ^ in6;
        out5 = tmp1 ^ in1;

        out_ptr[0] = out0 ^ in_ptr[0];
        out_ptr[WIDTH] = out1 ^ in_ptr[WIDTH];
        out_ptr[WIDTH * 2] = out2 ^ in_ptr[WIDTH * 2];
        out_ptr[WIDTH * 3] = out3 ^ in_ptr[WIDTH * 3];
        out_ptr[WIDTH * 4] = out4 ^ in_ptr[WIDTH * 4];
        out_ptr[WIDTH * 5] = out5 ^ in_ptr[WIDTH * 5];
        out_ptr[WIDTH * 6] = out6 ^ in_ptr[WIDTH * 6];
        out_ptr[WIDTH * 7] = out7 ^ in_ptr[WIDTH * 7];

        in_ptr++;
        out_ptr++;
    }
}

static void gf8_muladd_33(void *out, void *in)
{
    unsigned int i;
    uint64_t *in_ptr = (uint64_t *)in;
    uint64_t *out_ptr = (uint64_t *)out;

    for (i = 0; i < WIDTH; i++) {
        uint64_t out0, out1, out2, out3, out4, out5, out6, out7;
        uint64_t tmp0, tmp1, tmp2, tmp3, tmp4;

        uint64_t in0 = out_ptr[0];
        uint64_t in1 = out_ptr[WIDTH];
        uint64_t in2 = out_ptr[WIDTH * 2];
        uint64_t in3 = out_ptr[WIDTH * 3];
        uint64_t in4 = out_ptr[WIDTH * 4];
        uint64_t in5 = out_ptr[WIDTH * 5];
        uint64_t in6 = out_ptr[WIDTH * 6];
        uint64_t in7 = out_ptr[WIDTH * 7];

        tmp0 = in2 ^ in3;
        tmp1 = in0 ^ in4;
        tmp2 = in1 ^ in5;
        out6 = in1 ^ in2 ^ in6;
        out7 = tmp0 ^ in7;
        out0 = tmp1 ^ in3;
        out1 = tmp1 ^ tmp2;
        tmp3 = tmp2 ^ in7;
        tmp4 = tmp2 ^ in4 ^ in6;
        out5 = tmp3 ^ in0;
        out3 = tmp3 ^ out6;
        out4 = tmp4 ^ out5;
        out2 = tmp0 ^ tmp4;

        out_ptr[0] = out0 ^ in_ptr[0];
        out_ptr[WIDTH] = out1 ^ in_ptr[WIDTH];
        out_ptr[WIDTH * 2] = out2 ^ in_ptr[WIDTH * 2];
        out_ptr[WIDTH * 3] = out3 ^ in_ptr[WIDTH * 3];
        out_ptr[WIDTH * 4] = out4 ^ in_ptr[WIDTH * 4];
        out_ptr[WIDTH * 5] = out5 ^ in_ptr[WIDTH * 5];
        out_ptr[WIDTH * 6] = out6 ^ in_ptr[WIDTH * 6];
        out_ptr[WIDTH * 7] = out7 ^ in_ptr[WIDTH * 7];

        in_ptr++;
        out_ptr++;
    }
}

static void gf8_muladd_34(void *out, void *in)
{
    unsigned int i;
    uint64_t *in_ptr = (uint64_t *)in;
    uint64_t *out_ptr = (uint64_t *)out;

    for (i = 0; i < WIDTH; i++) {
        uint64_t out0, out1, out2, out3, out4, out5, out6, out7;
        uint64_t tmp0, tmp1, tmp2, tmp3, tmp4;

        uint64_t in0 = out_ptr[0];
        uint64_t in1 = out_ptr[WIDTH];
        uint64_t in2 = out_ptr[WIDTH * 2];
        uint64_t in3 = out_ptr[WIDTH * 3];
        uint64_t in4 = out_ptr[WIDTH * 4];
        uint64_t in5 = out_ptr[WIDTH * 5];
        uint64_t in6 = out_ptr[WIDTH * 6];
        uint64_t in7 = out_ptr[WIDTH * 7];

        tmp0 = in3 ^ in4;
        tmp1 = in4 ^ in5;
        tmp2 = tmp0 ^ in1;
        tmp3 = tmp0 ^ in6;
        out1 = tmp1 ^ in7;
        tmp4 = tmp1 ^ in2;
        out5 = tmp2 ^ in0;
        out3 = tmp2 ^ out1;
        out0 = tmp3 ^ in7;
        out7 = tmp3 ^ tmp4;
        out6 = tmp4 ^ in1;
        out2 = out3 ^ out5 ^ in3;
        out4 = tmp4 ^ out2;

        out_ptr[0] = out0 ^ in_ptr[0];
        out_ptr[WIDTH] = out1 ^ in_ptr[WIDTH];
        out_ptr[WIDTH * 2] = out2 ^ in_ptr[WIDTH * 2];
        out_ptr[WIDTH * 3] = out3 ^ in_ptr[WIDTH * 3];
        out_ptr[WIDTH * 4] = out4 ^ in_ptr[WIDTH * 4];
        out_ptr[WIDTH * 5] = out5 ^ in_ptr[WIDTH * 5];
        out_ptr[WIDTH * 6] = out6 ^ in_ptr[WIDTH * 6];
        out_ptr[WIDTH * 7] = out7 ^ in_ptr[WIDTH * 7];

        in_ptr++;
        out_ptr++;
    }
}

static void gf8_muladd_35(void *out, void *in)
{
    unsigned int i;
    uint64_t *in_ptr = (uint64_t *)in;
    uint64_t *out_ptr = (uint64_t *)out;

    for (i = 0; i < WIDTH; i++) {
        uint64_t out0, out1, out2, out3, out4, out5, out6, out7;
        uint64_t tmp0, tmp1, tmp2;

        uint64_t in0 = out_ptr[0];
        uint64_t in1 = out_ptr[WIDTH];
        uint64_t in2 = out_ptr[WIDTH * 2];
        uint64_t in3 = out_ptr[WIDTH * 3];
        uint64_t in4 = out_ptr[WIDTH * 4];
        uint64_t in5 = out_ptr[WIDTH * 5];
        uint64_t in6 = out_ptr[WIDTH * 6];
        uint64_t in7 = out_ptr[WIDTH * 7];

        tmp0 = in2 ^ in6;
        tmp1 = in5 ^ in7;
        out7 = tmp0 ^ tmp1 ^ in3;
        out3 = tmp1 ^ in1;
        out1 = out3 ^ in4;
        tmp2 = out1 ^ in7;
        out5 = tmp2 ^ in0 ^ in3;
        out6 = tmp0 ^ tmp2;
        out0 = out3 ^ out5 ^ in6;
        out4 = tmp0 ^ out0;
        out2 = out4 ^ in5;

        out_ptr[0] = out0 ^ in_ptr[0];
        out_ptr[WIDTH] = out1 ^ in_ptr[WIDTH];
        out_ptr[WIDTH * 2] = out2 ^ in_ptr[WIDTH * 2];
        out_ptr[WIDTH * 3] = out3 ^ in_ptr[WIDTH * 3];
        out_ptr[WIDTH * 4] = out4 ^ in_ptr[WIDTH * 4];
        out_ptr[WIDTH * 5] = out5 ^ in_ptr[WIDTH * 5];
        out_ptr[WIDTH * 6] = out6 ^ in_ptr[WIDTH * 6];
        out_ptr[WIDTH * 7] = out7 ^ in_ptr[WIDTH * 7];

        in_ptr++;
        out_ptr++;
    }
}

static void gf8_muladd_36(void *out, void *in)
{
    unsigned int i;
    uint64_t *in_ptr = (uint64_t *)in;
    uint64_t *out_ptr = (uint64_t *)out;

    for (i = 0; i < WIDTH; i++) {
        uint64_t out0, out1, out2, out3, out4, out5, out6, out7;
        uint64_t tmp0, tmp1;

        uint64_t in0 = out_ptr[0];
        uint64_t in1 = out_ptr[WIDTH];
        uint64_t in2 = out_ptr[WIDTH * 2];
        uint64_t in3 = out_ptr[WIDTH * 3];
        uint64_t in4 = out_ptr[WIDTH * 4];
        uint64_t in5 = out_ptr[WIDTH * 5];
        uint64_t in6 = out_ptr[WIDTH * 6];
        uint64_t in7 = out_ptr[WIDTH * 7];

        out4 = in0 ^ in2;
        tmp0 = in1 ^ in3;
        out0 = in3 ^ in4 ^ in6;
        out6 = in1 ^ in2 ^ in4;
        out5 = tmp0 ^ in0;
        tmp1 = out5 ^ in5;
        out2 = tmp1 ^ in4;
        out3 = tmp1 ^ out4;
        out1 = tmp0 ^ out2 ^ in7;
        out7 = out3 ^ in1;

        out_ptr[0] = out0 ^ in_ptr[0];
        out_ptr[WIDTH] = out1 ^ in_ptr[WIDTH];
        out_ptr[WIDTH * 2] = out2 ^ in_ptr[WIDTH * 2];
        out_ptr[WIDTH * 3] = out3 ^ in_ptr[WIDTH * 3];
        out_ptr[WIDTH * 4] = out4 ^ in_ptr[WIDTH * 4];
        out_ptr[WIDTH * 5] = out5 ^ in_ptr[WIDTH * 5];
        out_ptr[WIDTH * 6] = out6 ^ in_ptr[WIDTH * 6];
        out_ptr[WIDTH * 7] = out7 ^ in_ptr[WIDTH * 7];

        in_ptr++;
        out_ptr++;
    }
}

static void gf8_muladd_37(void *out, void *in)
{
    unsigned int i;
    uint64_t *in_ptr = (uint64_t *)in;
    uint64_t *out_ptr = (uint64_t *)out;

    for (i = 0; i < WIDTH; i++) {
        uint64_t out0, out1, out2, out3, out4, out5, out6, out7;
        uint64_t tmp0, tmp1, tmp2, tmp3;

        uint64_t in0 = out_ptr[0];
        uint64_t in1 = out_ptr[WIDTH];
        uint64_t in2 = out_ptr[WIDTH * 2];
        uint64_t in3 = out_ptr[WIDTH * 3];
        uint64_t in4 = out_ptr[WIDTH * 4];
        uint64_t in5 = out_ptr[WIDTH * 5];
        uint64_t in6 = out_ptr[WIDTH * 6];
        uint64_t in7 = out_ptr[WIDTH * 7];

        tmp0 = in1 ^ in2;
        tmp1 = in2 ^ in4;
        tmp2 = tmp0 ^ in6;
        out3 = tmp0 ^ in5;
        out4 = tmp1 ^ in0;
        out6 = tmp2 ^ in4;
        out1 = out3 ^ out4 ^ in7;
        tmp3 = out4 ^ in1 ^ in3;
        out7 = tmp3 ^ out1;
        out2 = tmp3 ^ in5;
        out5 = tmp1 ^ out2;
        out0 = tmp2 ^ tmp3;

        out_ptr[0] = out0 ^ in_ptr[0];
        out_ptr[WIDTH] = out1 ^ in_ptr[WIDTH];
        out_ptr[WIDTH * 2] = out2 ^ in_ptr[WIDTH * 2];
        out_ptr[WIDTH * 3] = out3 ^ in_ptr[WIDTH * 3];
        out_ptr[WIDTH * 4] = out4 ^ in_ptr[WIDTH * 4];
        out_ptr[WIDTH * 5] = out5 ^ in_ptr[WIDTH * 5];
        out_ptr[WIDTH * 6] = out6 ^ in_ptr[WIDTH * 6];
        out_ptr[WIDTH * 7] = out7 ^ in_ptr[WIDTH * 7];

        in_ptr++;
        out_ptr++;
    }
}

static void gf8_muladd_38(void *out, void *in)
{
    unsigned int i;
    uint64_t *in_ptr = (uint64_t *)in;
    uint64_t *out_ptr = (uint64_t *)out;

    for (i = 0; i < WIDTH; i++) {
        uint64_t out0, out1, out2, out3, out4, out5, out6, out7;
        uint64_t tmp0, tmp1, tmp2;

        uint64_t in0 = out_ptr[0];
        uint64_t in1 = out_ptr[WIDTH];
        uint64_t in2 = out_ptr[WIDTH * 2];
        uint64_t in3 = out_ptr[WIDTH * 3];
        uint64_t in4 = out_ptr[WIDTH * 4];
        uint64_t in5 = out_ptr[WIDTH * 5];
        uint64_t in6 = out_ptr[WIDTH * 6];
        uint64_t in7 = out_ptr[WIDTH * 7];

        out3 = in0 ^ in3;
        tmp0 = in3 ^ in4;
        tmp1 = in5 ^ in7;
        tmp2 = out3 ^ in1;
        out2 = tmp0 ^ in6;
        out0 = tmp0 ^ tmp1;
        out4 = tmp1 ^ tmp2;
        out7 = out2 ^ in2;
        out1 = out2 ^ in3 ^ in5;
        out6 = out4 ^ in0 ^ in2;
        out5 = tmp2 ^ out7;

        out_ptr[0] = out0 ^ in_ptr[0];
        out_ptr[WIDTH] = out1 ^ in_ptr[WIDTH];
        out_ptr[WIDTH * 2] = out2 ^ in_ptr[WIDTH * 2];
        out_ptr[WIDTH * 3] = out3 ^ in_ptr[WIDTH * 3];
        out_ptr[WIDTH * 4] = out4 ^ in_ptr[WIDTH * 4];
        out_ptr[WIDTH * 5] = out5 ^ in_ptr[WIDTH * 5];
        out_ptr[WIDTH * 6] = out6 ^ in_ptr[WIDTH * 6];
        out_ptr[WIDTH * 7] = out7 ^ in_ptr[WIDTH * 7];

        in_ptr++;
        out_ptr++;
    }
}

static void gf8_muladd_39(void *out, void *in)
{
    unsigned int i;
    uint64_t *in_ptr = (uint64_t *)in;
    uint64_t *out_ptr = (uint64_t *)out;

    for (i = 0; i < WIDTH; i++) {
        uint64_t out0, out1, out2, out3, out4, out5, out6, out7;
        uint64_t tmp0, tmp1, tmp2;

        uint64_t in0 = out_ptr[0];
        uint64_t in1 = out_ptr[WIDTH];
        uint64_t in2 = out_ptr[WIDTH * 2];
        uint64_t in3 = out_ptr[WIDTH * 3];
        uint64_t in4 = out_ptr[WIDTH * 4];
        uint64_t in5 = out_ptr[WIDTH * 5];
        uint64_t in6 = out_ptr[WIDTH * 6];
        uint64_t in7 = out_ptr[WIDTH * 7];

        out3 = in0;
        tmp0 = in1 ^ in5;
        tmp1 = tmp0 ^ in4;
        out1 = tmp1 ^ in6;
        out5 = out1 ^ in0 ^ in2;
        tmp2 = tmp0 ^ out5;
        out2 = tmp2 ^ in0 ^ in3;
        out7 = out2 ^ in7;
        out6 = tmp1 ^ out7;
        out4 = tmp2 ^ out6;
        out0 = out4 ^ in1;

        out_ptr[0] = out0 ^ in_ptr[0];
        out_ptr[WIDTH] = out1 ^ in_ptr[WIDTH];
        out_ptr[WIDTH * 2] = out2 ^ in_ptr[WIDTH * 2];
        out_ptr[WIDTH * 3] = out3 ^ in_ptr[WIDTH * 3];
        out_ptr[WIDTH * 4] = out4 ^ in_ptr[WIDTH * 4];
        out_ptr[WIDTH * 5] = out5 ^ in_ptr[WIDTH * 5];
        out_ptr[WIDTH * 6] = out6 ^ in_ptr[WIDTH * 6];
        out_ptr[WIDTH * 7] = out7 ^ in_ptr[WIDTH * 7];

        in_ptr++;
        out_ptr++;
    }
}

static void gf8_muladd_3A(void *out, void *in)
{
    unsigned int i;
    uint64_t *in_ptr = (uint64_t *)in;
    uint64_t *out_ptr = (uint64_t *)out;

    for (i = 0; i < WIDTH; i++) {
        uint64_t out0, out1, out2, out3, out4, out5, out6, out7;
        uint64_t tmp0, tmp1, tmp2, tmp3, tmp4, tmp5;

        uint64_t in0 = out_ptr[0];
        uint64_t in1 = out_ptr[WIDTH];
        uint64_t in2 = out_ptr[WIDTH * 2];
        uint64_t in3 = out_ptr[WIDTH * 3];
        uint64_t in4 = out_ptr[WIDTH * 4];
        uint64_t in5 = out_ptr[WIDTH * 5];
        uint64_t in6 = out_ptr[WIDTH * 6];
        uint64_t in7 = out_ptr[WIDTH * 7];

        tmp0 = in0 ^ in1;
        tmp1 = in0 ^ in2;
        tmp2 = in3 ^ in4;
        tmp3 = in1 ^ in6;
        tmp4 = in3 ^ in7;
        out4 = tmp0 ^ in5;
        out5 = tmp1 ^ tmp3;
        out3 = tmp1 ^ tmp4;
        out0 = tmp2 ^ in5;
        out7 = tmp2 ^ in2;
        tmp5 = tmp3 ^ in4;
        out2 = tmp4 ^ tmp5;
        out1 = tmp5 ^ out4;
        out6 = tmp0 ^ out3;

        out_ptr[0] = out0 ^ in_ptr[0];
        out_ptr[WIDTH] = out1 ^ in_ptr[WIDTH];
        out_ptr[WIDTH * 2] = out2 ^ in_ptr[WIDTH * 2];
        out_ptr[WIDTH * 3] = out3 ^ in_ptr[WIDTH * 3];
        out_ptr[WIDTH * 4] = out4 ^ in_ptr[WIDTH * 4];
        out_ptr[WIDTH * 5] = out5 ^ in_ptr[WIDTH * 5];
        out_ptr[WIDTH * 6] = out6 ^ in_ptr[WIDTH * 6];
        out_ptr[WIDTH * 7] = out7 ^ in_ptr[WIDTH * 7];

        in_ptr++;
        out_ptr++;
    }
}

static void gf8_muladd_3B(void *out, void *in)
{
    unsigned int i;
    uint64_t *in_ptr = (uint64_t *)in;
    uint64_t *out_ptr = (uint64_t *)out;

    for (i = 0; i < WIDTH; i++) {
        uint64_t out0, out1, out2, out3, out4, out5, out6, out7;
        uint64_t tmp0, tmp1, tmp2;

        uint64_t in0 = out_ptr[0];
        uint64_t in1 = out_ptr[WIDTH];
        uint64_t in2 = out_ptr[WIDTH * 2];
        uint64_t in3 = out_ptr[WIDTH * 3];
        uint64_t in4 = out_ptr[WIDTH * 4];
        uint64_t in5 = out_ptr[WIDTH * 5];
        uint64_t in6 = out_ptr[WIDTH * 6];
        uint64_t in7 = out_ptr[WIDTH * 7];

        tmp0 = in1 ^ in6;
        tmp1 = in2 ^ in7;
        tmp2 = tmp0 ^ in3;
        out3 = tmp1 ^ in0;
        out6 = tmp1 ^ tmp2;
        out2 = out6 ^ in4;
        out7 = tmp0 ^ out2;
        out0 = out3 ^ out7 ^ in5;
        out5 = out0 ^ out2 ^ in7;
        out1 = tmp2 ^ out0;
        out4 = out1 ^ in6;

        out_ptr[0] = out0 ^ in_ptr[0];
        out_ptr[WIDTH] = out1 ^ in_ptr[WIDTH];
        out_ptr[WIDTH * 2] = out2 ^ in_ptr[WIDTH * 2];
        out_ptr[WIDTH * 3] = out3 ^ in_ptr[WIDTH * 3];
        out_ptr[WIDTH * 4] = out4 ^ in_ptr[WIDTH * 4];
        out_ptr[WIDTH * 5] = out5 ^ in_ptr[WIDTH * 5];
        out_ptr[WIDTH * 6] = out6 ^ in_ptr[WIDTH * 6];
        out_ptr[WIDTH * 7] = out7 ^ in_ptr[WIDTH * 7];

        in_ptr++;
        out_ptr++;
    }
}

static void gf8_muladd_3C(void *out, void *in)
{
    unsigned int i;
    uint64_t *in_ptr = (uint64_t *)in;
    uint64_t *out_ptr = (uint64_t *)out;

    for (i = 0; i < WIDTH; i++) {
        uint64_t out0, out1, out2, out3, out4, out5, out6, out7;
        uint64_t tmp0, tmp1, tmp2;

        uint64_t in0 = out_ptr[0];
        uint64_t in1 = out_ptr[WIDTH];
        uint64_t in2 = out_ptr[WIDTH * 2];
        uint64_t in3 = out_ptr[WIDTH * 3];
        uint64_t in4 = out_ptr[WIDTH * 4];
        uint64_t in5 = out_ptr[WIDTH * 5];
        uint64_t in6 = out_ptr[WIDTH * 6];
        uint64_t in7 = out_ptr[WIDTH * 7];

        tmp0 = in0 ^ in3;
        tmp1 = in2 ^ in7;
        tmp2 = in1 ^ in6 ^ in7;
        out2 = tmp0 ^ in4;
        out3 = tmp0 ^ tmp2;
        out4 = tmp1 ^ out3 ^ in5;
        out5 = tmp2 ^ out2 ^ in2;
        out1 = out4 ^ out5 ^ in6;
        out0 = out1 ^ in3;
        out7 = tmp1 ^ out0;
        out6 = tmp2 ^ out7;

        out_ptr[0] = out0 ^ in_ptr[0];
        out_ptr[WIDTH] = out1 ^ in_ptr[WIDTH];
        out_ptr[WIDTH * 2] = out2 ^ in_ptr[WIDTH * 2];
        out_ptr[WIDTH * 3] = out3 ^ in_ptr[WIDTH * 3];
        out_ptr[WIDTH * 4] = out4 ^ in_ptr[WIDTH * 4];
        out_ptr[WIDTH * 5] = out5 ^ in_ptr[WIDTH * 5];
        out_ptr[WIDTH * 6] = out6 ^ in_ptr[WIDTH * 6];
        out_ptr[WIDTH * 7] = out7 ^ in_ptr[WIDTH * 7];

        in_ptr++;
        out_ptr++;
    }
}

static void gf8_muladd_3D(void *out, void *in)
{
    unsigned int i;
    uint64_t *in_ptr = (uint64_t *)in;
    uint64_t *out_ptr = (uint64_t *)out;

    for (i = 0; i < WIDTH; i++) {
        uint64_t out0, out1, out2, out3, out4, out5, out6, out7;
        uint64_t tmp0, tmp1, tmp2;

        uint64_t in0 = out_ptr[0];
        uint64_t in1 = out_ptr[WIDTH];
        uint64_t in2 = out_ptr[WIDTH * 2];
        uint64_t in3 = out_ptr[WIDTH * 3];
        uint64_t in4 = out_ptr[WIDTH * 4];
        uint64_t in5 = out_ptr[WIDTH * 5];
        uint64_t in6 = out_ptr[WIDTH * 6];
        uint64_t in7 = out_ptr[WIDTH * 7];

        tmp0 = in0 ^ in2;
        tmp1 = tmp0 ^ in3;
        out2 = tmp1 ^ in4;
        tmp2 = out2 ^ in5;
        out4 = tmp2 ^ in1 ^ in6;
        out5 = out4 ^ in7;
        out6 = out5 ^ in0;
        out7 = out6 ^ in1;
        out0 = tmp0 ^ out7;
        out1 = tmp1 ^ out5;
        out3 = tmp2 ^ out6;

        out_ptr[0] = out0 ^ in_ptr[0];
        out_ptr[WIDTH] = out1 ^ in_ptr[WIDTH];
        out_ptr[WIDTH * 2] = out2 ^ in_ptr[WIDTH * 2];
        out_ptr[WIDTH * 3] = out3 ^ in_ptr[WIDTH * 3];
        out_ptr[WIDTH * 4] = out4 ^ in_ptr[WIDTH * 4];
        out_ptr[WIDTH * 5] = out5 ^ in_ptr[WIDTH * 5];
        out_ptr[WIDTH * 6] = out6 ^ in_ptr[WIDTH * 6];
        out_ptr[WIDTH * 7] = out7 ^ in_ptr[WIDTH * 7];

        in_ptr++;
        out_ptr++;
    }
}

static void gf8_muladd_3E(void *out, void *in)
{
    unsigned int i;
    uint64_t *in_ptr = (uint64_t *)in;
    uint64_t *out_ptr = (uint64_t *)out;

    for (i = 0; i < WIDTH; i++) {
        uint64_t out0, out1, out2, out3, out4, out5, out6, out7;
        uint64_t tmp0, tmp1;

        uint64_t in0 = out_ptr[0];
        uint64_t in1 = out_ptr[WIDTH];
        uint64_t in2 = out_ptr[WIDTH * 2];
        uint64_t in3 = out_ptr[WIDTH * 3];
        uint64_t in4 = out_ptr[WIDTH * 4];
        uint64_t in5 = out_ptr[WIDTH * 5];
        uint64_t in6 = out_ptr[WIDTH * 6];
        uint64_t in7 = out_ptr[WIDTH * 7];

        tmp0 = in3 ^ in5;
        tmp1 = tmp0 ^ in4;
        out0 = tmp1 ^ in6;
        out7 = tmp1 ^ in2;
        out6 = out7 ^ in1 ^ in5 ^ in7;
        out2 = out6 ^ in0 ^ in2;
        out4 = out0 ^ out6 ^ in0;
        out5 = tmp0 ^ out4;
        out3 = out5 ^ in7;
        out1 = out3 ^ out6 ^ in5;

        out_ptr[0] = out0 ^ in_ptr[0];
        out_ptr[WIDTH] = out1 ^ in_ptr[WIDTH];
        out_ptr[WIDTH * 2] = out2 ^ in_ptr[WIDTH * 2];
        out_ptr[WIDTH * 3] = out3 ^ in_ptr[WIDTH * 3];
        out_ptr[WIDTH * 4] = out4 ^ in_ptr[WIDTH * 4];
        out_ptr[WIDTH * 5] = out5 ^ in_ptr[WIDTH * 5];
        out_ptr[WIDTH * 6] = out6 ^ in_ptr[WIDTH * 6];
        out_ptr[WIDTH * 7] = out7 ^ in_ptr[WIDTH * 7];

        in_ptr++;
        out_ptr++;
    }
}

static void gf8_muladd_3F(void *out, void *in)
{
    unsigned int i;
    uint64_t *in_ptr = (uint64_t *)in;
    uint64_t *out_ptr = (uint64_t *)out;

    for (i = 0; i < WIDTH; i++) {
        uint64_t out0, out1, out2, out3, out4, out5, out6, out7;
        uint64_t tmp0, tmp1, tmp2;

        uint64_t in0 = out_ptr[0];
        uint64_t in1 = out_ptr[WIDTH];
        uint64_t in2 = out_ptr[WIDTH * 2];
        uint64_t in3 = out_ptr[WIDTH * 3];
        uint64_t in4 = out_ptr[WIDTH * 4];
        uint64_t in5 = out_ptr[WIDTH * 5];
        uint64_t in6 = out_ptr[WIDTH * 6];
        uint64_t in7 = out_ptr[WIDTH * 7];

        tmp0 = in0 ^ in1;
        out3 = tmp0 ^ in2 ^ in6;
        tmp1 = out3 ^ in5 ^ in7;
        out4 = tmp1 ^ in4;
        out5 = tmp1 ^ in3;
        out1 = out4 ^ in2;
        out7 = out1 ^ out3 ^ in3;
        out2 = tmp0 ^ out7 ^ in5;
        tmp2 = out2 ^ in0;
        out6 = tmp2 ^ in6;
        out0 = tmp1 ^ tmp2;

        out_ptr[0] = out0 ^ in_ptr[0];
        out_ptr[WIDTH] = out1 ^ in_ptr[WIDTH];
        out_ptr[WIDTH * 2] = out2 ^ in_ptr[WIDTH * 2];
        out_ptr[WIDTH * 3] = out3 ^ in_ptr[WIDTH * 3];
        out_ptr[WIDTH * 4] = out4 ^ in_ptr[WIDTH * 4];
        out_ptr[WIDTH * 5] = out5 ^ in_ptr[WIDTH * 5];
        out_ptr[WIDTH * 6] = out6 ^ in_ptr[WIDTH * 6];
        out_ptr[WIDTH * 7] = out7 ^ in_ptr[WIDTH * 7];

        in_ptr++;
        out_ptr++;
    }
}

static void gf8_muladd_40(void *out, void *in)
{
    unsigned int i;
    uint64_t *in_ptr = (uint64_t *)in;
    uint64_t *out_ptr = (uint64_t *)out;

    for (i = 0; i < WIDTH; i++) {
        uint64_t out0, out1, out2, out3, out4, out5, out6, out7;
        uint64_t tmp0, tmp1;

        uint64_t in0 = out_ptr[0];
        uint64_t in1 = out_ptr[WIDTH];
        uint64_t in2 = out_ptr[WIDTH * 2];
        uint64_t in3 = out_ptr[WIDTH * 3];
        uint64_t in4 = out_ptr[WIDTH * 4];
        uint64_t in5 = out_ptr[WIDTH * 5];
        uint64_t in6 = out_ptr[WIDTH * 6];
        uint64_t in7 = out_ptr[WIDTH * 7];

        out1 = in3 ^ in7;
        tmp0 = in3 ^ in4;
        tmp1 = in6 ^ in7;
        out4 = tmp0 ^ in2;
        out5 = tmp0 ^ in5;
        out0 = tmp1 ^ in2;
        out7 = tmp1 ^ in1 ^ in5;
        out2 = out0 ^ in4;
        out3 = out2 ^ out5 ^ in7;
        out6 = out3 ^ out4 ^ in0;

        out_ptr[0] = out0 ^ in_ptr[0];
        out_ptr[WIDTH] = out1 ^ in_ptr[WIDTH];
        out_ptr[WIDTH * 2] = out2 ^ in_ptr[WIDTH * 2];
        out_ptr[WIDTH * 3] = out3 ^ in_ptr[WIDTH * 3];
        out_ptr[WIDTH * 4] = out4 ^ in_ptr[WIDTH * 4];
        out_ptr[WIDTH * 5] = out5 ^ in_ptr[WIDTH * 5];
        out_ptr[WIDTH * 6] = out6 ^ in_ptr[WIDTH * 6];
        out_ptr[WIDTH * 7] = out7 ^ in_ptr[WIDTH * 7];

        in_ptr++;
        out_ptr++;
    }
}

static void gf8_muladd_41(void *out, void *in)
{
    unsigned int i;
    uint64_t *in_ptr = (uint64_t *)in;
    uint64_t *out_ptr = (uint64_t *)out;

    for (i = 0; i < WIDTH; i++) {
        uint64_t out0, out1, out2, out3, out4, out5, out6, out7;
        uint64_t tmp0, tmp1;

        uint64_t in0 = out_ptr[0];
        uint64_t in1 = out_ptr[WIDTH];
        uint64_t in2 = out_ptr[WIDTH * 2];
        uint64_t in3 = out_ptr[WIDTH * 3];
        uint64_t in4 = out_ptr[WIDTH * 4];
        uint64_t in5 = out_ptr[WIDTH * 5];
        uint64_t in6 = out_ptr[WIDTH * 6];
        uint64_t in7 = out_ptr[WIDTH * 7];

        out4 = in2 ^ in3;
        tmp0 = in5 ^ in6;
        tmp1 = in6 ^ in7;
        out5 = in3 ^ in4;
        out1 = in1 ^ in3 ^ in7;
        out6 = in0 ^ in4 ^ in5;
        out3 = tmp0 ^ in2;
        out7 = tmp0 ^ in1;
        out2 = tmp1 ^ in4;
        out0 = tmp1 ^ in0 ^ in2;

        out_ptr[0] = out0 ^ in_ptr[0];
        out_ptr[WIDTH] = out1 ^ in_ptr[WIDTH];
        out_ptr[WIDTH * 2] = out2 ^ in_ptr[WIDTH * 2];
        out_ptr[WIDTH * 3] = out3 ^ in_ptr[WIDTH * 3];
        out_ptr[WIDTH * 4] = out4 ^ in_ptr[WIDTH * 4];
        out_ptr[WIDTH * 5] = out5 ^ in_ptr[WIDTH * 5];
        out_ptr[WIDTH * 6] = out6 ^ in_ptr[WIDTH * 6];
        out_ptr[WIDTH * 7] = out7 ^ in_ptr[WIDTH * 7];

        in_ptr++;
        out_ptr++;
    }
}

static void gf8_muladd_42(void *out, void *in)
{
    unsigned int i;
    uint64_t *in_ptr = (uint64_t *)in;
    uint64_t *out_ptr = (uint64_t *)out;

    for (i = 0; i < WIDTH; i++) {
        uint64_t out0, out1, out2, out3, out4, out5, out6, out7;

        uint64_t in0 = out_ptr[0];
        uint64_t in1 = out_ptr[WIDTH];
        uint64_t in2 = out_ptr[WIDTH * 2];
        uint64_t in3 = out_ptr[WIDTH * 3];
        uint64_t in4 = out_ptr[WIDTH * 4];
        uint64_t in5 = out_ptr[WIDTH * 5];
        uint64_t in6 = out_ptr[WIDTH * 6];
        uint64_t in7 = out_ptr[WIDTH * 7];

        out0 = in2 ^ in6;
        out5 = in3 ^ in5;
        out1 = in0 ^ in3 ^ in7;
        out7 = in1 ^ in5 ^ in7;
        out4 = in2 ^ in4 ^ in7;
        out6 = in0 ^ in4 ^ in6;
        out2 = out0 ^ in1 ^ in4;
        out3 = out5 ^ in6 ^ in7;

        out_ptr[0] = out0 ^ in_ptr[0];
        out_ptr[WIDTH] = out1 ^ in_ptr[WIDTH];
        out_ptr[WIDTH * 2] = out2 ^ in_ptr[WIDTH * 2];
        out_ptr[WIDTH * 3] = out3 ^ in_ptr[WIDTH * 3];
        out_ptr[WIDTH * 4] = out4 ^ in_ptr[WIDTH * 4];
        out_ptr[WIDTH * 5] = out5 ^ in_ptr[WIDTH * 5];
        out_ptr[WIDTH * 6] = out6 ^ in_ptr[WIDTH * 6];
        out_ptr[WIDTH * 7] = out7 ^ in_ptr[WIDTH * 7];

        in_ptr++;
        out_ptr++;
    }
}

static void gf8_muladd_43(void *out, void *in)
{
    unsigned int i;
    uint64_t *in_ptr = (uint64_t *)in;
    uint64_t *out_ptr = (uint64_t *)out;

    for (i = 0; i < WIDTH; i++) {
        uint64_t out0, out1, out2, out3, out4, out5, out6, out7;

        uint64_t in0 = out_ptr[0];
        uint64_t in1 = out_ptr[WIDTH];
        uint64_t in2 = out_ptr[WIDTH * 2];
        uint64_t in3 = out_ptr[WIDTH * 3];
        uint64_t in4 = out_ptr[WIDTH * 4];
        uint64_t in5 = out_ptr[WIDTH * 5];
        uint64_t in6 = out_ptr[WIDTH * 6];
        uint64_t in7 = out_ptr[WIDTH * 7];

        out5 = in3;
        out7 = in1 ^ in5;
        out4 = in2 ^ in7;
        out6 = in0 ^ in4;
        out0 = in0 ^ in2 ^ in6;
        out3 = in5 ^ in6 ^ in7;
        out2 = in1 ^ in4 ^ in6;
        out1 = in0 ^ in1 ^ in3 ^ in7;

        out_ptr[0] = out0 ^ in_ptr[0];
        out_ptr[WIDTH] = out1 ^ in_ptr[WIDTH];
        out_ptr[WIDTH * 2] = out2 ^ in_ptr[WIDTH * 2];
        out_ptr[WIDTH * 3] = out3 ^ in_ptr[WIDTH * 3];
        out_ptr[WIDTH * 4] = out4 ^ in_ptr[WIDTH * 4];
        out_ptr[WIDTH * 5] = out5 ^ in_ptr[WIDTH * 5];
        out_ptr[WIDTH * 6] = out6 ^ in_ptr[WIDTH * 6];
        out_ptr[WIDTH * 7] = out7 ^ in_ptr[WIDTH * 7];

        in_ptr++;
        out_ptr++;
    }
}

static void gf8_muladd_44(void *out, void *in)
{
    unsigned int i;
    uint64_t *in_ptr = (uint64_t *)in;
    uint64_t *out_ptr = (uint64_t *)out;

    for (i = 0; i < WIDTH; i++) {
        uint64_t out0, out1, out2, out3, out4, out5, out6, out7;
        uint64_t tmp0;

        uint64_t in0 = out_ptr[0];
        uint64_t in1 = out_ptr[WIDTH];
        uint64_t in2 = out_ptr[WIDTH * 2];
        uint64_t in3 = out_ptr[WIDTH * 3];
        uint64_t in4 = out_ptr[WIDTH * 4];
        uint64_t in5 = out_ptr[WIDTH * 5];
        uint64_t in6 = out_ptr[WIDTH * 6];
        uint64_t in7 = out_ptr[WIDTH * 7];

        out1 = in3;
        out0 = in2 ^ in7;
        tmp0 = in4 ^ in7;
        out7 = in1 ^ in6 ^ in7;
        out6 = in0 ^ in5 ^ in6;
        out4 = tmp0 ^ in3 ^ in6;
        out3 = out0 ^ in1 ^ in3 ^ in5;
        out2 = out0 ^ in0 ^ in4;
        out5 = tmp0 ^ in5;

        out_ptr[0] = out0 ^ in_ptr[0];
        out_ptr[WIDTH] = out1 ^ in_ptr[WIDTH];
        out_ptr[WIDTH * 2] = out2 ^ in_ptr[WIDTH * 2];
        out_ptr[WIDTH * 3] = out3 ^ in_ptr[WIDTH * 3];
        out_ptr[WIDTH * 4] = out4 ^ in_ptr[WIDTH * 4];
        out_ptr[WIDTH * 5] = out5 ^ in_ptr[WIDTH * 5];
        out_ptr[WIDTH * 6] = out6 ^ in_ptr[WIDTH * 6];
        out_ptr[WIDTH * 7] = out7 ^ in_ptr[WIDTH * 7];

        in_ptr++;
        out_ptr++;
    }
}

static void gf8_muladd_45(void *out, void *in)
{
    unsigned int i;
    uint64_t *in_ptr = (uint64_t *)in;
    uint64_t *out_ptr = (uint64_t *)out;

    for (i = 0; i < WIDTH; i++) {
        uint64_t out0, out1, out2, out3, out4, out5, out6, out7;

        uint64_t in0 = out_ptr[0];
        uint64_t in1 = out_ptr[WIDTH];
        uint64_t in2 = out_ptr[WIDTH * 2];
        uint64_t in3 = out_ptr[WIDTH * 3];
        uint64_t in4 = out_ptr[WIDTH * 4];
        uint64_t in5 = out_ptr[WIDTH * 5];
        uint64_t in6 = out_ptr[WIDTH * 6];
        uint64_t in7 = out_ptr[WIDTH * 7];

        out1 = in1 ^ in3;
        out7 = in1 ^ in6;
        out5 = in4 ^ in7;
        out6 = in0 ^ in5;
        out0 = in0 ^ in2 ^ in7;
        out4 = in3 ^ in6 ^ in7;
        out2 = out5 ^ in0;
        out3 = out0 ^ out6 ^ in1;

        out_ptr[0] = out0 ^ in_ptr[0];
        out_ptr[WIDTH] = out1 ^ in_ptr[WIDTH];
        out_ptr[WIDTH * 2] = out2 ^ in_ptr[WIDTH * 2];
        out_ptr[WIDTH * 3] = out3 ^ in_ptr[WIDTH * 3];
        out_ptr[WIDTH * 4] = out4 ^ in_ptr[WIDTH * 4];
        out_ptr[WIDTH * 5] = out5 ^ in_ptr[WIDTH * 5];
        out_ptr[WIDTH * 6] = out6 ^ in_ptr[WIDTH * 6];
        out_ptr[WIDTH * 7] = out7 ^ in_ptr[WIDTH * 7];

        in_ptr++;
        out_ptr++;
    }
}

static void gf8_muladd_46(void *out, void *in)
{
    unsigned int i;
    uint64_t *in_ptr = (uint64_t *)in;
    uint64_t *out_ptr = (uint64_t *)out;

    for (i = 0; i < WIDTH; i++) {
        uint64_t out0, out1, out2, out3, out4, out5, out6, out7;

        uint64_t in0 = out_ptr[0];
        uint64_t in1 = out_ptr[WIDTH];
        uint64_t in2 = out_ptr[WIDTH * 2];
        uint64_t in3 = out_ptr[WIDTH * 3];
        uint64_t in4 = out_ptr[WIDTH * 4];
        uint64_t in5 = out_ptr[WIDTH * 5];
        uint64_t in6 = out_ptr[WIDTH * 6];
        uint64_t in7 = out_ptr[WIDTH * 7];

        out0 = in2;
        out1 = in0 ^ in3;
        out7 = in1 ^ in7;
        out4 = in4 ^ in6;
        out5 = in5 ^ in7;
        out6 = in0 ^ in6;
        out3 = in1 ^ in3 ^ in5;
        out2 = out4 ^ out6 ^ in1 ^ in2;

        out_ptr[0] = out0 ^ in_ptr[0];
        out_ptr[WIDTH] = out1 ^ in_ptr[WIDTH];
        out_ptr[WIDTH * 2] = out2 ^ in_ptr[WIDTH * 2];
        out_ptr[WIDTH * 3] = out3 ^ in_ptr[WIDTH * 3];
        out_ptr[WIDTH * 4] = out4 ^ in_ptr[WIDTH * 4];
        out_ptr[WIDTH * 5] = out5 ^ in_ptr[WIDTH * 5];
        out_ptr[WIDTH * 6] = out6 ^ in_ptr[WIDTH * 6];
        out_ptr[WIDTH * 7] = out7 ^ in_ptr[WIDTH * 7];

        in_ptr++;
        out_ptr++;
    }
}

static void gf8_muladd_47(void *out, void *in)
{
    unsigned int i;
    uint64_t *in_ptr = (uint64_t *)in;
    uint64_t *out_ptr = (uint64_t *)out;

    for (i = 0; i < WIDTH; i++) {
        uint64_t out0, out1, out2, out3, out4, out5, out6, out7;
        uint64_t tmp0;

        uint64_t in0 = out_ptr[0];
        uint64_t in1 = out_ptr[WIDTH];
        uint64_t in2 = out_ptr[WIDTH * 2];
        uint64_t in3 = out_ptr[WIDTH * 3];
        uint64_t in4 = out_ptr[WIDTH * 4];
        uint64_t in5 = out_ptr[WIDTH * 5];
        uint64_t in6 = out_ptr[WIDTH * 6];
        uint64_t in7 = out_ptr[WIDTH * 7];

        out4 = in6;
        out7 = in1;
        out5 = in7;
        out6 = in0;
        tmp0 = in0 ^ in1;
        out3 = in1 ^ in5;
        out0 = in0 ^ in2;
        out1 = tmp0 ^ in3;
        out2 = tmp0 ^ in4;

        out_ptr[0] = out0 ^ in_ptr[0];
        out_ptr[WIDTH] = out1 ^ in_ptr[WIDTH];
        out_ptr[WIDTH * 2] = out2 ^ in_ptr[WIDTH * 2];
        out_ptr[WIDTH * 3] = out3 ^ in_ptr[WIDTH * 3];
        out_ptr[WIDTH * 4] = out4 ^ in_ptr[WIDTH * 4];
        out_ptr[WIDTH * 5] = out5 ^ in_ptr[WIDTH * 5];
        out_ptr[WIDTH * 6] = out6 ^ in_ptr[WIDTH * 6];
        out_ptr[WIDTH * 7] = out7 ^ in_ptr[WIDTH * 7];

        in_ptr++;
        out_ptr++;
    }
}

static void gf8_muladd_48(void *out, void *in)
{
    unsigned int i;
    uint64_t *in_ptr = (uint64_t *)in;
    uint64_t *out_ptr = (uint64_t *)out;

    for (i = 0; i < WIDTH; i++) {
        uint64_t out0, out1, out2, out3, out4, out5, out6, out7;
        uint64_t tmp0, tmp1;

        uint64_t in0 = out_ptr[0];
        uint64_t in1 = out_ptr[WIDTH];
        uint64_t in2 = out_ptr[WIDTH * 2];
        uint64_t in3 = out_ptr[WIDTH * 3];
        uint64_t in4 = out_ptr[WIDTH * 4];
        uint64_t in5 = out_ptr[WIDTH * 5];
        uint64_t in6 = out_ptr[WIDTH * 6];
        uint64_t in7 = out_ptr[WIDTH * 7];

        tmp0 = in2 ^ in3;
        out1 = in3 ^ in6 ^ in7;
        out3 = tmp0 ^ in0;
        out0 = tmp0 ^ out1 ^ in5;
        tmp1 = out0 ^ in4;
        out2 = tmp1 ^ in7;
        out5 = tmp1 ^ in3;
        out4 = out5 ^ in1;
        out7 = tmp0 ^ out4;
        out6 = tmp1 ^ out3;

        out_ptr[0] = out0 ^ in_ptr[0];
        out_ptr[WIDTH] = out1 ^ in_ptr[WIDTH];
        out_ptr[WIDTH * 2] = out2 ^ in_ptr[WIDTH * 2];
        out_ptr[WIDTH * 3] = out3 ^ in_ptr[WIDTH * 3];
        out_ptr[WIDTH * 4] = out4 ^ in_ptr[WIDTH * 4];
        out_ptr[WIDTH * 5] = out5 ^ in_ptr[WIDTH * 5];
        out_ptr[WIDTH * 6] = out6 ^ in_ptr[WIDTH * 6];
        out_ptr[WIDTH * 7] = out7 ^ in_ptr[WIDTH * 7];

        in_ptr++;
        out_ptr++;
    }
}

static void gf8_muladd_49(void *out, void *in)
{
    unsigned int i;
    uint64_t *in_ptr = (uint64_t *)in;
    uint64_t *out_ptr = (uint64_t *)out;

    for (i = 0; i < WIDTH; i++) {
        uint64_t out0, out1, out2, out3, out4, out5, out6, out7;
        uint64_t tmp0, tmp1;

        uint64_t in0 = out_ptr[0];
        uint64_t in1 = out_ptr[WIDTH];
        uint64_t in2 = out_ptr[WIDTH * 2];
        uint64_t in3 = out_ptr[WIDTH * 3];
        uint64_t in4 = out_ptr[WIDTH * 4];
        uint64_t in5 = out_ptr[WIDTH * 5];
        uint64_t in6 = out_ptr[WIDTH * 6];
        uint64_t in7 = out_ptr[WIDTH * 7];

        out3 = in0 ^ in2;
        tmp0 = in2 ^ in5;
        out2 = in4 ^ in5 ^ in6;
        tmp1 = tmp0 ^ out2 ^ in3;
        out7 = out2 ^ in1;
        out5 = tmp1 ^ in7;
        out4 = out5 ^ out7 ^ in6;
        out1 = tmp0 ^ out4;
        out6 = out1 ^ out7 ^ in0;
        out0 = tmp1 ^ out6;

        out_ptr[0] = out0 ^ in_ptr[0];
        out_ptr[WIDTH] = out1 ^ in_ptr[WIDTH];
        out_ptr[WIDTH * 2] = out2 ^ in_ptr[WIDTH * 2];
        out_ptr[WIDTH * 3] = out3 ^ in_ptr[WIDTH * 3];
        out_ptr[WIDTH * 4] = out4 ^ in_ptr[WIDTH * 4];
        out_ptr[WIDTH * 5] = out5 ^ in_ptr[WIDTH * 5];
        out_ptr[WIDTH * 6] = out6 ^ in_ptr[WIDTH * 6];
        out_ptr[WIDTH * 7] = out7 ^ in_ptr[WIDTH * 7];

        in_ptr++;
        out_ptr++;
    }
}

static void gf8_muladd_4A(void *out, void *in)
{
    unsigned int i;
    uint64_t *in_ptr = (uint64_t *)in;
    uint64_t *out_ptr = (uint64_t *)out;

    for (i = 0; i < WIDTH; i++) {
        uint64_t out0, out1, out2, out3, out4, out5, out6, out7;
        uint64_t tmp0, tmp1;

        uint64_t in0 = out_ptr[0];
        uint64_t in1 = out_ptr[WIDTH];
        uint64_t in2 = out_ptr[WIDTH * 2];
        uint64_t in3 = out_ptr[WIDTH * 3];
        uint64_t in4 = out_ptr[WIDTH * 4];
        uint64_t in5 = out_ptr[WIDTH * 5];
        uint64_t in6 = out_ptr[WIDTH * 6];
        uint64_t in7 = out_ptr[WIDTH * 7];

        tmp0 = in2 ^ in6;
        tmp1 = in3 ^ in7;
        out0 = tmp0 ^ in5;
        out3 = tmp1 ^ in0;
        out5 = tmp1 ^ out0;
        out4 = out0 ^ in1 ^ in4;
        out1 = out3 ^ in6;
        out2 = out4 ^ in7;
        out6 = out1 ^ in4;
        out7 = tmp0 ^ out2;

        out_ptr[0] = out0 ^ in_ptr[0];
        out_ptr[WIDTH] = out1 ^ in_ptr[WIDTH];
        out_ptr[WIDTH * 2] = out2 ^ in_ptr[WIDTH * 2];
        out_ptr[WIDTH * 3] = out3 ^ in_ptr[WIDTH * 3];
        out_ptr[WIDTH * 4] = out4 ^ in_ptr[WIDTH * 4];
        out_ptr[WIDTH * 5] = out5 ^ in_ptr[WIDTH * 5];
        out_ptr[WIDTH * 6] = out6 ^ in_ptr[WIDTH * 6];
        out_ptr[WIDTH * 7] = out7 ^ in_ptr[WIDTH * 7];

        in_ptr++;
        out_ptr++;
    }
}

static void gf8_muladd_4B(void *out, void *in)
{
    unsigned int i;
    uint64_t *in_ptr = (uint64_t *)in;
    uint64_t *out_ptr = (uint64_t *)out;

    for (i = 0; i < WIDTH; i++) {
        uint64_t out0, out1, out2, out3, out4, out5, out6, out7;
        uint64_t tmp0, tmp1, tmp2, tmp3;

        uint64_t in0 = out_ptr[0];
        uint64_t in1 = out_ptr[WIDTH];
        uint64_t in2 = out_ptr[WIDTH * 2];
        uint64_t in3 = out_ptr[WIDTH * 3];
        uint64_t in4 = out_ptr[WIDTH * 4];
        uint64_t in5 = out_ptr[WIDTH * 5];
        uint64_t in6 = out_ptr[WIDTH * 6];
        uint64_t in7 = out_ptr[WIDTH * 7];

        out3 = in0 ^ in7;
        tmp0 = in1 ^ in5;
        tmp1 = in2 ^ in6;
        tmp2 = out3 ^ in3;
        out7 = tmp0 ^ in4;
        out4 = tmp0 ^ tmp1;
        tmp3 = tmp1 ^ in0;
        out6 = tmp2 ^ in4;
        out5 = tmp2 ^ tmp3;
        out1 = tmp2 ^ in1 ^ in6;
        out2 = out7 ^ in6 ^ in7;
        out0 = tmp3 ^ in5;

        out_ptr[0] = out0 ^ in_ptr[0];
        out_ptr[WIDTH] = out1 ^ in_ptr[WIDTH];
        out_ptr[WIDTH * 2] = out2 ^ in_ptr[WIDTH * 2];
        out_ptr[WIDTH * 3] = out3 ^ in_ptr[WIDTH * 3];
        out_ptr[WIDTH * 4] = out4 ^ in_ptr[WIDTH * 4];
        out_ptr[WIDTH * 5] = out5 ^ in_ptr[WIDTH * 5];
        out_ptr[WIDTH * 6] = out6 ^ in_ptr[WIDTH * 6];
        out_ptr[WIDTH * 7] = out7 ^ in_ptr[WIDTH * 7];

        in_ptr++;
        out_ptr++;
    }
}

static void gf8_muladd_4C(void *out, void *in)
{
    unsigned int i;
    uint64_t *in_ptr = (uint64_t *)in;
    uint64_t *out_ptr = (uint64_t *)out;

    for (i = 0; i < WIDTH; i++) {
        uint64_t out0, out1, out2, out3, out4, out5, out6, out7;
        uint64_t tmp0, tmp1, tmp2;

        uint64_t in0 = out_ptr[0];
        uint64_t in1 = out_ptr[WIDTH];
        uint64_t in2 = out_ptr[WIDTH * 2];
        uint64_t in3 = out_ptr[WIDTH * 3];
        uint64_t in4 = out_ptr[WIDTH * 4];
        uint64_t in5 = out_ptr[WIDTH * 5];
        uint64_t in6 = out_ptr[WIDTH * 6];
        uint64_t in7 = out_ptr[WIDTH * 7];

        out1 = in3 ^ in6;
        tmp0 = in2 ^ in5;
        tmp1 = out1 ^ in5 ^ in7;
        out0 = tmp0 ^ in7;
        tmp2 = tmp0 ^ in4;
        out6 = tmp1 ^ in0;
        out2 = tmp2 ^ in0;
        out5 = tmp2 ^ in6;
        out3 = tmp0 ^ out6 ^ in1;
        out7 = out0 ^ out5 ^ in1;
        out4 = tmp1 ^ out7;

        out_ptr[0] = out0 ^ in_ptr[0];
        out_ptr[WIDTH] = out1 ^ in_ptr[WIDTH];
        out_ptr[WIDTH * 2] = out2 ^ in_ptr[WIDTH * 2];
        out_ptr[WIDTH * 3] = out3 ^ in_ptr[WIDTH * 3];
        out_ptr[WIDTH * 4] = out4 ^ in_ptr[WIDTH * 4];
        out_ptr[WIDTH * 5] = out5 ^ in_ptr[WIDTH * 5];
        out_ptr[WIDTH * 6] = out6 ^ in_ptr[WIDTH * 6];
        out_ptr[WIDTH * 7] = out7 ^ in_ptr[WIDTH * 7];

        in_ptr++;
        out_ptr++;
    }
}

static void gf8_muladd_4D(void *out, void *in)
{
    unsigned int i;
    uint64_t *in_ptr = (uint64_t *)in;
    uint64_t *out_ptr = (uint64_t *)out;

    for (i = 0; i < WIDTH; i++) {
        uint64_t out0, out1, out2, out3, out4, out5, out6, out7;
        uint64_t tmp0, tmp1, tmp2;

        uint64_t in0 = out_ptr[0];
        uint64_t in1 = out_ptr[WIDTH];
        uint64_t in2 = out_ptr[WIDTH * 2];
        uint64_t in3 = out_ptr[WIDTH * 3];
        uint64_t in4 = out_ptr[WIDTH * 4];
        uint64_t in5 = out_ptr[WIDTH * 5];
        uint64_t in6 = out_ptr[WIDTH * 6];
        uint64_t in7 = out_ptr[WIDTH * 7];

        tmp0 = in0 ^ in5;
        tmp1 = in1 ^ in6;
        out4 = in1 ^ in3 ^ in5;
        tmp2 = tmp0 ^ in7;
        out2 = tmp0 ^ in4;
        out1 = tmp1 ^ in3;
        out7 = tmp1 ^ in4;
        out0 = tmp2 ^ in2;
        out6 = tmp2 ^ in3;
        out5 = out7 ^ in1 ^ in2;
        out3 = tmp1 ^ out0 ^ in5;

        out_ptr[0] = out0 ^ in_ptr[0];
        out_ptr[WIDTH] = out1 ^ in_ptr[WIDTH];
        out_ptr[WIDTH * 2] = out2 ^ in_ptr[WIDTH * 2];
        out_ptr[WIDTH * 3] = out3 ^ in_ptr[WIDTH * 3];
        out_ptr[WIDTH * 4] = out4 ^ in_ptr[WIDTH * 4];
        out_ptr[WIDTH * 5] = out5 ^ in_ptr[WIDTH * 5];
        out_ptr[WIDTH * 6] = out6 ^ in_ptr[WIDTH * 6];
        out_ptr[WIDTH * 7] = out7 ^ in_ptr[WIDTH * 7];

        in_ptr++;
        out_ptr++;
    }
}

static void gf8_muladd_4E(void *out, void *in)
{
    unsigned int i;
    uint64_t *in_ptr = (uint64_t *)in;
    uint64_t *out_ptr = (uint64_t *)out;

    for (i = 0; i < WIDTH; i++) {
        uint64_t out0, out1, out2, out3, out4, out5, out6, out7;

        uint64_t in0 = out_ptr[0];
        uint64_t in1 = out_ptr[WIDTH];
        uint64_t in2 = out_ptr[WIDTH * 2];
        uint64_t in3 = out_ptr[WIDTH * 3];
        uint64_t in4 = out_ptr[WIDTH * 4];
        uint64_t in5 = out_ptr[WIDTH * 5];
        uint64_t in6 = out_ptr[WIDTH * 6];
        uint64_t in7 = out_ptr[WIDTH * 7];

        out0 = in2 ^ in5;
        out7 = in1 ^ in4 ^ in7;
        out1 = in0 ^ in3 ^ in6;
        out5 = out0 ^ in6;
        out4 = out7 ^ in5;
        out3 = out1 ^ in1;
        out6 = out1 ^ in7;
        out2 = out4 ^ in0 ^ in2;

        out_ptr[0] = out0 ^ in_ptr[0];
        out_ptr[WIDTH] = out1 ^ in_ptr[WIDTH];
        out_ptr[WIDTH * 2] = out2 ^ in_ptr[WIDTH * 2];
        out_ptr[WIDTH * 3] = out3 ^ in_ptr[WIDTH * 3];
        out_ptr[WIDTH * 4] = out4 ^ in_ptr[WIDTH * 4];
        out_ptr[WIDTH * 5] = out5 ^ in_ptr[WIDTH * 5];
        out_ptr[WIDTH * 6] = out6 ^ in_ptr[WIDTH * 6];
        out_ptr[WIDTH * 7] = out7 ^ in_ptr[WIDTH * 7];

        in_ptr++;
        out_ptr++;
    }
}

static void gf8_muladd_4F(void *out, void *in)
{
    unsigned int i;
    uint64_t *in_ptr = (uint64_t *)in;
    uint64_t *out_ptr = (uint64_t *)out;

    for (i = 0; i < WIDTH; i++) {
        uint64_t out0, out1, out2, out3, out4, out5, out6, out7;

        uint64_t in0 = out_ptr[0];
        uint64_t in1 = out_ptr[WIDTH];
        uint64_t in2 = out_ptr[WIDTH * 2];
        uint64_t in3 = out_ptr[WIDTH * 3];
        uint64_t in4 = out_ptr[WIDTH * 4];
        uint64_t in5 = out_ptr[WIDTH * 5];
        uint64_t in6 = out_ptr[WIDTH * 6];
        uint64_t in7 = out_ptr[WIDTH * 7];

        out5 = in2 ^ in6;
        out7 = in1 ^ in4;
        out3 = in0 ^ in1 ^ in6;
        out4 = in1 ^ in5 ^ in7;
        out0 = in0 ^ in2 ^ in5;
        out6 = in0 ^ in3 ^ in7;
        out1 = out3 ^ in3;
        out2 = out4 ^ in0 ^ in4;

        out_ptr[0] = out0 ^ in_ptr[0];
        out_ptr[WIDTH] = out1 ^ in_ptr[WIDTH];
        out_ptr[WIDTH * 2] = out2 ^ in_ptr[WIDTH * 2];
        out_ptr[WIDTH * 3] = out3 ^ in_ptr[WIDTH * 3];
        out_ptr[WIDTH * 4] = out4 ^ in_ptr[WIDTH * 4];
        out_ptr[WIDTH * 5] = out5 ^ in_ptr[WIDTH * 5];
        out_ptr[WIDTH * 6] = out6 ^ in_ptr[WIDTH * 6];
        out_ptr[WIDTH * 7] = out7 ^ in_ptr[WIDTH * 7];

        in_ptr++;
        out_ptr++;
    }
}

static void gf8_muladd_50(void *out, void *in)
{
    unsigned int i;
    uint64_t *in_ptr = (uint64_t *)in;
    uint64_t *out_ptr = (uint64_t *)out;

    for (i = 0; i < WIDTH; i++) {
        uint64_t out0, out1, out2, out3, out4, out5, out6, out7;
        uint64_t tmp0, tmp1, tmp2;

        uint64_t in0 = out_ptr[0];
        uint64_t in1 = out_ptr[WIDTH];
        uint64_t in2 = out_ptr[WIDTH * 2];
        uint64_t in3 = out_ptr[WIDTH * 3];
        uint64_t in4 = out_ptr[WIDTH * 4];
        uint64_t in5 = out_ptr[WIDTH * 5];
        uint64_t in6 = out_ptr[WIDTH * 6];
        uint64_t in7 = out_ptr[WIDTH * 7];

        out2 = in2 ^ in7;
        tmp0 = in3 ^ in5;
        out0 = out2 ^ in4 ^ in6;
        out1 = tmp0 ^ in7;
        tmp1 = tmp0 ^ in6;
        out3 = out0 ^ in3;
        out7 = tmp1 ^ in1;
        tmp2 = tmp1 ^ in0;
        out5 = out3 ^ in1 ^ in2;
        out4 = tmp2 ^ in2;
        out6 = tmp2 ^ out3;

        out_ptr[0] = out0 ^ in_ptr[0];
        out_ptr[WIDTH] = out1 ^ in_ptr[WIDTH];
        out_ptr[WIDTH * 2] = out2 ^ in_ptr[WIDTH * 2];
        out_ptr[WIDTH * 3] = out3 ^ in_ptr[WIDTH * 3];
        out_ptr[WIDTH * 4] = out4 ^ in_ptr[WIDTH * 4];
        out_ptr[WIDTH * 5] = out5 ^ in_ptr[WIDTH * 5];
        out_ptr[WIDTH * 6] = out6 ^ in_ptr[WIDTH * 6];
        out_ptr[WIDTH * 7] = out7 ^ in_ptr[WIDTH * 7];

        in_ptr++;
        out_ptr++;
    }
}

static void gf8_muladd_51(void *out, void *in)
{
    unsigned int i;
    uint64_t *in_ptr = (uint64_t *)in;
    uint64_t *out_ptr = (uint64_t *)out;

    for (i = 0; i < WIDTH; i++) {
        uint64_t out0, out1, out2, out3, out4, out5, out6, out7;

        uint64_t in0 = out_ptr[0];
        uint64_t in1 = out_ptr[WIDTH];
        uint64_t in2 = out_ptr[WIDTH * 2];
        uint64_t in3 = out_ptr[WIDTH * 3];
        uint64_t in4 = out_ptr[WIDTH * 4];
        uint64_t in5 = out_ptr[WIDTH * 5];
        uint64_t in6 = out_ptr[WIDTH * 6];
        uint64_t in7 = out_ptr[WIDTH * 7];

        out2 = in7;
        out3 = in2 ^ in4 ^ in6 ^ in7;
        out0 = out3 ^ in0;
        out6 = out0 ^ in5;
        out4 = out6 ^ in3 ^ in7;
        out1 = out0 ^ out4 ^ in1;
        out7 = out1 ^ in6;
        out5 = out7 ^ in4;

        out_ptr[0] = out0 ^ in_ptr[0];
        out_ptr[WIDTH] = out1 ^ in_ptr[WIDTH];
        out_ptr[WIDTH * 2] = out2 ^ in_ptr[WIDTH * 2];
        out_ptr[WIDTH * 3] = out3 ^ in_ptr[WIDTH * 3];
        out_ptr[WIDTH * 4] = out4 ^ in_ptr[WIDTH * 4];
        out_ptr[WIDTH * 5] = out5 ^ in_ptr[WIDTH * 5];
        out_ptr[WIDTH * 6] = out6 ^ in_ptr[WIDTH * 6];
        out_ptr[WIDTH * 7] = out7 ^ in_ptr[WIDTH * 7];

        in_ptr++;
        out_ptr++;
    }
}

static void gf8_muladd_52(void *out, void *in)
{
    unsigned int i;
    uint64_t *in_ptr = (uint64_t *)in;
    uint64_t *out_ptr = (uint64_t *)out;

    for (i = 0; i < WIDTH; i++) {
        uint64_t out0, out1, out2, out3, out4, out5, out6, out7;
        uint64_t tmp0, tmp1, tmp2, tmp3;

        uint64_t in0 = out_ptr[0];
        uint64_t in1 = out_ptr[WIDTH];
        uint64_t in2 = out_ptr[WIDTH * 2];
        uint64_t in3 = out_ptr[WIDTH * 3];
        uint64_t in4 = out_ptr[WIDTH * 4];
        uint64_t in5 = out_ptr[WIDTH * 5];
        uint64_t in6 = out_ptr[WIDTH * 6];
        uint64_t in7 = out_ptr[WIDTH * 7];

        out2 = in1 ^ in2;
        tmp0 = in2 ^ in4;
        tmp1 = in3 ^ in5;
        tmp2 = in3 ^ in6;
        tmp3 = in0 ^ in7;
        out0 = tmp0 ^ in6;
        out6 = tmp0 ^ tmp3;
        out7 = tmp1 ^ in1;
        out1 = tmp1 ^ tmp3;
        out3 = tmp2 ^ in4;
        out5 = tmp2 ^ in1 ^ in7;
        out4 = tmp2 ^ out1 ^ in2;

        out_ptr[0] = out0 ^ in_ptr[0];
        out_ptr[WIDTH] = out1 ^ in_ptr[WIDTH];
        out_ptr[WIDTH * 2] = out2 ^ in_ptr[WIDTH * 2];
        out_ptr[WIDTH * 3] = out3 ^ in_ptr[WIDTH * 3];
        out_ptr[WIDTH * 4] = out4 ^ in_ptr[WIDTH * 4];
        out_ptr[WIDTH * 5] = out5 ^ in_ptr[WIDTH * 5];
        out_ptr[WIDTH * 6] = out6 ^ in_ptr[WIDTH * 6];
        out_ptr[WIDTH * 7] = out7 ^ in_ptr[WIDTH * 7];

        in_ptr++;
        out_ptr++;
    }
}

static void gf8_muladd_53(void *out, void *in)
{
    unsigned int i;
    uint64_t *in_ptr = (uint64_t *)in;
    uint64_t *out_ptr = (uint64_t *)out;

    for (i = 0; i < WIDTH; i++) {
        uint64_t out0, out1, out2, out3, out4, out5, out6, out7;

        uint64_t in0 = out_ptr[0];
        uint64_t in1 = out_ptr[WIDTH];
        uint64_t in2 = out_ptr[WIDTH * 2];
        uint64_t in3 = out_ptr[WIDTH * 3];
        uint64_t in4 = out_ptr[WIDTH * 4];
        uint64_t in5 = out_ptr[WIDTH * 5];
        uint64_t in6 = out_ptr[WIDTH * 6];
        uint64_t in7 = out_ptr[WIDTH * 7];

        out2 = in1;
        out3 = in4 ^ in6;
        out0 = out3 ^ in0 ^ in2;
        out6 = out0 ^ in7;
        out4 = out6 ^ in5;
        out7 = out0 ^ out4 ^ in1 ^ in3;
        out1 = out7 ^ in0;
        out5 = out7 ^ in6;

        out_ptr[0] = out0 ^ in_ptr[0];
        out_ptr[WIDTH] = out1 ^ in_ptr[WIDTH];
        out_ptr[WIDTH * 2] = out2 ^ in_ptr[WIDTH * 2];
        out_ptr[WIDTH * 3] = out3 ^ in_ptr[WIDTH * 3];
        out_ptr[WIDTH * 4] = out4 ^ in_ptr[WIDTH * 4];
        out_ptr[WIDTH * 5] = out5 ^ in_ptr[WIDTH * 5];
        out_ptr[WIDTH * 6] = out6 ^ in_ptr[WIDTH * 6];
        out_ptr[WIDTH * 7] = out7 ^ in_ptr[WIDTH * 7];

        in_ptr++;
        out_ptr++;
    }
}

static void gf8_muladd_54(void *out, void *in)
{
    unsigned int i;
    uint64_t *in_ptr = (uint64_t *)in;
    uint64_t *out_ptr = (uint64_t *)out;

    for (i = 0; i < WIDTH; i++) {
        uint64_t out0, out1, out2, out3, out4, out5, out6, out7;
        uint64_t tmp0, tmp1, tmp2, tmp3;

        uint64_t in0 = out_ptr[0];
        uint64_t in1 = out_ptr[WIDTH];
        uint64_t in2 = out_ptr[WIDTH * 2];
        uint64_t in3 = out_ptr[WIDTH * 3];
        uint64_t in4 = out_ptr[WIDTH * 4];
        uint64_t in5 = out_ptr[WIDTH * 5];
        uint64_t in6 = out_ptr[WIDTH * 6];
        uint64_t in7 = out_ptr[WIDTH * 7];

        out1 = in3 ^ in5;
        tmp0 = in1 ^ in3;
        tmp1 = in2 ^ in4;
        tmp2 = in0 ^ in7;
        out5 = in1 ^ in4 ^ in6;
        out4 = tmp2 ^ out1;
        out7 = tmp0 ^ in6;
        out3 = tmp0 ^ tmp1;
        out0 = tmp1 ^ in7;
        tmp3 = tmp2 ^ in2;
        out2 = tmp3 ^ in6;
        out6 = tmp3 ^ in5;

        out_ptr[0] = out0 ^ in_ptr[0];
        out_ptr[WIDTH] = out1 ^ in_ptr[WIDTH];
        out_ptr[WIDTH * 2] = out2 ^ in_ptr[WIDTH * 2];
        out_ptr[WIDTH * 3] = out3 ^ in_ptr[WIDTH * 3];
        out_ptr[WIDTH * 4] = out4 ^ in_ptr[WIDTH * 4];
        out_ptr[WIDTH * 5] = out5 ^ in_ptr[WIDTH * 5];
        out_ptr[WIDTH * 6] = out6 ^ in_ptr[WIDTH * 6];
        out_ptr[WIDTH * 7] = out7 ^ in_ptr[WIDTH * 7];

        in_ptr++;
        out_ptr++;
    }
}

static void gf8_muladd_55(void *out, void *in)
{
    unsigned int i;
    uint64_t *in_ptr = (uint64_t *)in;
    uint64_t *out_ptr = (uint64_t *)out;

    for (i = 0; i < WIDTH; i++) {
        uint64_t out0, out1, out2, out3, out4, out5, out6, out7;
        uint64_t tmp0, tmp1, tmp2;

        uint64_t in0 = out_ptr[0];
        uint64_t in1 = out_ptr[WIDTH];
        uint64_t in2 = out_ptr[WIDTH * 2];
        uint64_t in3 = out_ptr[WIDTH * 3];
        uint64_t in4 = out_ptr[WIDTH * 4];
        uint64_t in5 = out_ptr[WIDTH * 5];
        uint64_t in6 = out_ptr[WIDTH * 6];
        uint64_t in7 = out_ptr[WIDTH * 7];

        tmp0 = in1 ^ in3;
        tmp1 = in1 ^ in4;
        tmp2 = in6 ^ in7;
        out7 = tmp0 ^ tmp2;
        out1 = tmp0 ^ in5;
        out3 = tmp1 ^ in2;
        out5 = tmp1 ^ in5 ^ in6;
        out2 = tmp2 ^ in0;
        out4 = out5 ^ out7 ^ in0;
        out6 = out2 ^ in2 ^ in5;
        out0 = out5 ^ out6 ^ in1;

        out_ptr[0] = out0 ^ in_ptr[0];
        out_ptr[WIDTH] = out1 ^ in_ptr[WIDTH];
        out_ptr[WIDTH * 2] = out2 ^ in_ptr[WIDTH * 2];
        out_ptr[WIDTH * 3] = out3 ^ in_ptr[WIDTH * 3];
        out_ptr[WIDTH * 4] = out4 ^ in_ptr[WIDTH * 4];
        out_ptr[WIDTH * 5] = out5 ^ in_ptr[WIDTH * 5];
        out_ptr[WIDTH * 6] = out6 ^ in_ptr[WIDTH * 6];
        out_ptr[WIDTH * 7] = out7 ^ in_ptr[WIDTH * 7];

        in_ptr++;
        out_ptr++;
    }
}

static void gf8_muladd_56(void *out, void *in)
{
    unsigned int i;
    uint64_t *in_ptr = (uint64_t *)in;
    uint64_t *out_ptr = (uint64_t *)out;

    for (i = 0; i < WIDTH; i++) {
        uint64_t out0, out1, out2, out3, out4, out5, out6, out7;
        uint64_t tmp0;

        uint64_t in0 = out_ptr[0];
        uint64_t in1 = out_ptr[WIDTH];
        uint64_t in2 = out_ptr[WIDTH * 2];
        uint64_t in3 = out_ptr[WIDTH * 3];
        uint64_t in4 = out_ptr[WIDTH * 4];
        uint64_t in5 = out_ptr[WIDTH * 5];
        uint64_t in6 = out_ptr[WIDTH * 6];
        uint64_t in7 = out_ptr[WIDTH * 7];

        out0 = in2 ^ in4;
        tmp0 = in0 ^ in2;
        out4 = in0 ^ in5;
        out7 = in1 ^ in3;
        out5 = in1 ^ in6;
        out6 = tmp0 ^ in7;
        out2 = tmp0 ^ out5;
        out1 = out4 ^ in3;
        out3 = out7 ^ in4 ^ in7;

        out_ptr[0] = out0 ^ in_ptr[0];
        out_ptr[WIDTH] = out1 ^ in_ptr[WIDTH];
        out_ptr[WIDTH * 2] = out2 ^ in_ptr[WIDTH * 2];
        out_ptr[WIDTH * 3] = out3 ^ in_ptr[WIDTH * 3];
        out_ptr[WIDTH * 4] = out4 ^ in_ptr[WIDTH * 4];
        out_ptr[WIDTH * 5] = out5 ^ in_ptr[WIDTH * 5];
        out_ptr[WIDTH * 6] = out6 ^ in_ptr[WIDTH * 6];
        out_ptr[WIDTH * 7] = out7 ^ in_ptr[WIDTH * 7];

        in_ptr++;
        out_ptr++;
    }
}

static void gf8_muladd_57(void *out, void *in)
{
    unsigned int i;
    uint64_t *in_ptr = (uint64_t *)in;
    uint64_t *out_ptr = (uint64_t *)out;

    for (i = 0; i < WIDTH; i++) {
        uint64_t out0, out1, out2, out3, out4, out5, out6, out7;
        uint64_t tmp0, tmp1;

        uint64_t in0 = out_ptr[0];
        uint64_t in1 = out_ptr[WIDTH];
        uint64_t in2 = out_ptr[WIDTH * 2];
        uint64_t in3 = out_ptr[WIDTH * 3];
        uint64_t in4 = out_ptr[WIDTH * 4];
        uint64_t in5 = out_ptr[WIDTH * 5];
        uint64_t in6 = out_ptr[WIDTH * 6];
        uint64_t in7 = out_ptr[WIDTH * 7];

        tmp0 = in0 ^ in5;
        tmp1 = in1 ^ in7;
        out0 = in0 ^ in2 ^ in4;
        out5 = in1 ^ in5 ^ in6;
        out4 = tmp0 ^ in4;
        out1 = tmp0 ^ in1 ^ in3;
        out2 = tmp0 ^ out5;
        out3 = tmp1 ^ in4;
        out7 = tmp1 ^ in3;
        out6 = tmp1 ^ out2 ^ in2;

        out_ptr[0] = out0 ^ in_ptr[0];
        out_ptr[WIDTH] = out1 ^ in_ptr[WIDTH];
        out_ptr[WIDTH * 2] = out2 ^ in_ptr[WIDTH * 2];
        out_ptr[WIDTH * 3] = out3 ^ in_ptr[WIDTH * 3];
        out_ptr[WIDTH * 4] = out4 ^ in_ptr[WIDTH * 4];
        out_ptr[WIDTH * 5] = out5 ^ in_ptr[WIDTH * 5];
        out_ptr[WIDTH * 6] = out6 ^ in_ptr[WIDTH * 6];
        out_ptr[WIDTH * 7] = out7 ^ in_ptr[WIDTH * 7];

        in_ptr++;
        out_ptr++;
    }
}

static void gf8_muladd_58(void *out, void *in)
{
    unsigned int i;
    uint64_t *in_ptr = (uint64_t *)in;
    uint64_t *out_ptr = (uint64_t *)out;

    for (i = 0; i < WIDTH; i++) {
        uint64_t out0, out1, out2, out3, out4, out5, out6, out7;
        uint64_t tmp0, tmp1;

        uint64_t in0 = out_ptr[0];
        uint64_t in1 = out_ptr[WIDTH];
        uint64_t in2 = out_ptr[WIDTH * 2];
        uint64_t in3 = out_ptr[WIDTH * 3];
        uint64_t in4 = out_ptr[WIDTH * 4];
        uint64_t in5 = out_ptr[WIDTH * 5];
        uint64_t in6 = out_ptr[WIDTH * 6];
        uint64_t in7 = out_ptr[WIDTH * 7];

        out2 = in2 ^ in5;
        tmp0 = in2 ^ in3 ^ in4;
        out5 = tmp0 ^ in1;
        out6 = tmp0 ^ in0 ^ in5;
        out3 = out6 ^ in7;
        tmp1 = out2 ^ out5;
        out7 = tmp1 ^ in6;
        out4 = tmp1 ^ out3 ^ in3;
        out0 = out4 ^ out7 ^ in0;
        out1 = tmp0 ^ out0;

        out_ptr[0] = out0 ^ in_ptr[0];
        out_ptr[WIDTH] = out1 ^ in_ptr[WIDTH];
        out_ptr[WIDTH * 2] = out2 ^ in_ptr[WIDTH * 2];
        out_ptr[WIDTH * 3] = out3 ^ in_ptr[WIDTH * 3];
        out_ptr[WIDTH * 4] = out4 ^ in_ptr[WIDTH * 4];
        out_ptr[WIDTH * 5] = out5 ^ in_ptr[WIDTH * 5];
        out_ptr[WIDTH * 6] = out6 ^ in_ptr[WIDTH * 6];
        out_ptr[WIDTH * 7] = out7 ^ in_ptr[WIDTH * 7];

        in_ptr++;
        out_ptr++;
    }
}

static void gf8_muladd_59(void *out, void *in)
{
    unsigned int i;
    uint64_t *in_ptr = (uint64_t *)in;
    uint64_t *out_ptr = (uint64_t *)out;

    for (i = 0; i < WIDTH; i++) {
        uint64_t out0, out1, out2, out3, out4, out5, out6, out7;
        uint64_t tmp0, tmp1;

        uint64_t in0 = out_ptr[0];
        uint64_t in1 = out_ptr[WIDTH];
        uint64_t in2 = out_ptr[WIDTH * 2];
        uint64_t in3 = out_ptr[WIDTH * 3];
        uint64_t in4 = out_ptr[WIDTH * 4];
        uint64_t in5 = out_ptr[WIDTH * 5];
        uint64_t in6 = out_ptr[WIDTH * 6];
        uint64_t in7 = out_ptr[WIDTH * 7];

        out2 = in5;
        tmp0 = in0 ^ in5 ^ in7;
        out3 = tmp0 ^ in2 ^ in4;
        out0 = out3 ^ in6;
        tmp1 = out0 ^ in7;
        out6 = tmp1 ^ in3;
        out5 = out6 ^ in0 ^ in1 ^ in6;
        out4 = tmp0 ^ out5;
        out1 = tmp1 ^ out4;
        out7 = out1 ^ in4;

        out_ptr[0] = out0 ^ in_ptr[0];
        out_ptr[WIDTH] = out1 ^ in_ptr[WIDTH];
        out_ptr[WIDTH * 2] = out2 ^ in_ptr[WIDTH * 2];
        out_ptr[WIDTH * 3] = out3 ^ in_ptr[WIDTH * 3];
        out_ptr[WIDTH * 4] = out4 ^ in_ptr[WIDTH * 4];
        out_ptr[WIDTH * 5] = out5 ^ in_ptr[WIDTH * 5];
        out_ptr[WIDTH * 6] = out6 ^ in_ptr[WIDTH * 6];
        out_ptr[WIDTH * 7] = out7 ^ in_ptr[WIDTH * 7];

        in_ptr++;
        out_ptr++;
    }
}

static void gf8_muladd_5A(void *out, void *in)
{
    unsigned int i;
    uint64_t *in_ptr = (uint64_t *)in;
    uint64_t *out_ptr = (uint64_t *)out;

    for (i = 0; i < WIDTH; i++) {
        uint64_t out0, out1, out2, out3, out4, out5, out6, out7;
        uint64_t tmp0, tmp1, tmp2;

        uint64_t in0 = out_ptr[0];
        uint64_t in1 = out_ptr[WIDTH];
        uint64_t in2 = out_ptr[WIDTH * 2];
        uint64_t in3 = out_ptr[WIDTH * 3];
        uint64_t in4 = out_ptr[WIDTH * 4];
        uint64_t in5 = out_ptr[WIDTH * 5];
        uint64_t in6 = out_ptr[WIDTH * 6];
        uint64_t in7 = out_ptr[WIDTH * 7];

        tmp0 = in1 ^ in2;
        tmp1 = in2 ^ in5;
        out5 = tmp0 ^ in3;
        out4 = tmp0 ^ in0;
        tmp2 = tmp1 ^ in4;
        out2 = tmp1 ^ in1 ^ in7;
        out7 = tmp2 ^ out5;
        out6 = out4 ^ out7 ^ in5;
        out0 = tmp2 ^ in6;
        out1 = out0 ^ out6 ^ in7;
        out3 = tmp1 ^ out6;

        out_ptr[0] = out0 ^ in_ptr[0];
        out_ptr[WIDTH] = out1 ^ in_ptr[WIDTH];
        out_ptr[WIDTH * 2] = out2 ^ in_ptr[WIDTH * 2];
        out_ptr[WIDTH * 3] = out3 ^ in_ptr[WIDTH * 3];
        out_ptr[WIDTH * 4] = out4 ^ in_ptr[WIDTH * 4];
        out_ptr[WIDTH * 5] = out5 ^ in_ptr[WIDTH * 5];
        out_ptr[WIDTH * 6] = out6 ^ in_ptr[WIDTH * 6];
        out_ptr[WIDTH * 7] = out7 ^ in_ptr[WIDTH * 7];

        in_ptr++;
        out_ptr++;
    }
}

static void gf8_muladd_5B(void *out, void *in)
{
    unsigned int i;
    uint64_t *in_ptr = (uint64_t *)in;
    uint64_t *out_ptr = (uint64_t *)out;

    for (i = 0; i < WIDTH; i++) {
        uint64_t out0, out1, out2, out3, out4, out5, out6, out7;
        uint64_t tmp0, tmp1, tmp2, tmp3, tmp4;

        uint64_t in0 = out_ptr[0];
        uint64_t in1 = out_ptr[WIDTH];
        uint64_t in2 = out_ptr[WIDTH * 2];
        uint64_t in3 = out_ptr[WIDTH * 3];
        uint64_t in4 = out_ptr[WIDTH * 4];
        uint64_t in5 = out_ptr[WIDTH * 5];
        uint64_t in6 = out_ptr[WIDTH * 6];
        uint64_t in7 = out_ptr[WIDTH * 7];

        tmp0 = in2 ^ in3;
        tmp1 = in0 ^ in4;
        tmp2 = in1 ^ in5;
        out5 = tmp0 ^ tmp2;
        tmp3 = tmp1 ^ in6;
        out3 = tmp1 ^ in5;
        out2 = tmp2 ^ in7;
        tmp4 = out3 ^ in2;
        out7 = out2 ^ in3 ^ in4;
        out0 = tmp4 ^ in6;
        out6 = tmp0 ^ tmp3;
        out4 = tmp2 ^ tmp4;
        out1 = tmp3 ^ out7;

        out_ptr[0] = out0 ^ in_ptr[0];
        out_ptr[WIDTH] = out1 ^ in_ptr[WIDTH];
        out_ptr[WIDTH * 2] = out2 ^ in_ptr[WIDTH * 2];
        out_ptr[WIDTH * 3] = out3 ^ in_ptr[WIDTH * 3];
        out_ptr[WIDTH * 4] = out4 ^ in_ptr[WIDTH * 4];
        out_ptr[WIDTH * 5] = out5 ^ in_ptr[WIDTH * 5];
        out_ptr[WIDTH * 6] = out6 ^ in_ptr[WIDTH * 6];
        out_ptr[WIDTH * 7] = out7 ^ in_ptr[WIDTH * 7];

        in_ptr++;
        out_ptr++;
    }
}

static void gf8_muladd_5C(void *out, void *in)
{
    unsigned int i;
    uint64_t *in_ptr = (uint64_t *)in;
    uint64_t *out_ptr = (uint64_t *)out;

    for (i = 0; i < WIDTH; i++) {
        uint64_t out0, out1, out2, out3, out4, out5, out6, out7;
        uint64_t tmp0, tmp1, tmp2;

        uint64_t in0 = out_ptr[0];
        uint64_t in1 = out_ptr[WIDTH];
        uint64_t in2 = out_ptr[WIDTH * 2];
        uint64_t in3 = out_ptr[WIDTH * 3];
        uint64_t in4 = out_ptr[WIDTH * 4];
        uint64_t in5 = out_ptr[WIDTH * 5];
        uint64_t in6 = out_ptr[WIDTH * 6];
        uint64_t in7 = out_ptr[WIDTH * 7];

        tmp0 = in3 ^ in6;
        tmp1 = in0 ^ in2 ^ in5;
        out1 = tmp0 ^ in5;
        tmp2 = tmp0 ^ in1;
        out2 = tmp1 ^ in6;
        out6 = tmp1 ^ in3;
        out4 = tmp2 ^ in0;
        out7 = tmp2 ^ in4;
        out3 = tmp1 ^ out7;
        out0 = out3 ^ out4 ^ in7;
        out5 = out0 ^ in1 ^ in5;

        out_ptr[0] = out0 ^ in_ptr[0];
        out_ptr[WIDTH] = out1 ^ in_ptr[WIDTH];
        out_ptr[WIDTH * 2] = out2 ^ in_ptr[WIDTH * 2];
        out_ptr[WIDTH * 3] = out3 ^ in_ptr[WIDTH * 3];
        out_ptr[WIDTH * 4] = out4 ^ in_ptr[WIDTH * 4];
        out_ptr[WIDTH * 5] = out5 ^ in_ptr[WIDTH * 5];
        out_ptr[WIDTH * 6] = out6 ^ in_ptr[WIDTH * 6];
        out_ptr[WIDTH * 7] = out7 ^ in_ptr[WIDTH * 7];

        in_ptr++;
        out_ptr++;
    }
}

static void gf8_muladd_5D(void *out, void *in)
{
    unsigned int i;
    uint64_t *in_ptr = (uint64_t *)in;
    uint64_t *out_ptr = (uint64_t *)out;

    for (i = 0; i < WIDTH; i++) {
        uint64_t out0, out1, out2, out3, out4, out5, out6, out7;
        uint64_t tmp0, tmp1, tmp2, tmp3, tmp4;

        uint64_t in0 = out_ptr[0];
        uint64_t in1 = out_ptr[WIDTH];
        uint64_t in2 = out_ptr[WIDTH * 2];
        uint64_t in3 = out_ptr[WIDTH * 3];
        uint64_t in4 = out_ptr[WIDTH * 4];
        uint64_t in5 = out_ptr[WIDTH * 5];
        uint64_t in6 = out_ptr[WIDTH * 6];
        uint64_t in7 = out_ptr[WIDTH * 7];

        tmp0 = in0 ^ in1;
        tmp1 = in0 ^ in6;
        out2 = tmp1 ^ in5;
        tmp2 = out2 ^ in3;
        out6 = tmp2 ^ in2;
        out1 = tmp0 ^ tmp2;
        tmp3 = out1 ^ in4 ^ in5;
        out4 = tmp3 ^ in0;
        out7 = tmp3 ^ in7;
        tmp4 = out4 ^ out6;
        out5 = tmp4 ^ in7;
        out0 = tmp0 ^ out5;
        out3 = tmp1 ^ tmp4;

        out_ptr[0] = out0 ^ in_ptr[0];
        out_ptr[WIDTH] = out1 ^ in_ptr[WIDTH];
        out_ptr[WIDTH * 2] = out2 ^ in_ptr[WIDTH * 2];
        out_ptr[WIDTH * 3] = out3 ^ in_ptr[WIDTH * 3];
        out_ptr[WIDTH * 4] = out4 ^ in_ptr[WIDTH * 4];
        out_ptr[WIDTH * 5] = out5 ^ in_ptr[WIDTH * 5];
        out_ptr[WIDTH * 6] = out6 ^ in_ptr[WIDTH * 6];
        out_ptr[WIDTH * 7] = out7 ^ in_ptr[WIDTH * 7];

        in_ptr++;
        out_ptr++;
    }
}

static void gf8_muladd_5E(void *out, void *in)
{
    unsigned int i;
    uint64_t *in_ptr = (uint64_t *)in;
    uint64_t *out_ptr = (uint64_t *)out;

    for (i = 0; i < WIDTH; i++) {
        uint64_t out0, out1, out2, out3, out4, out5, out6, out7;
        uint64_t tmp0, tmp1, tmp2, tmp3, tmp4;

        uint64_t in0 = out_ptr[0];
        uint64_t in1 = out_ptr[WIDTH];
        uint64_t in2 = out_ptr[WIDTH * 2];
        uint64_t in3 = out_ptr[WIDTH * 3];
        uint64_t in4 = out_ptr[WIDTH * 4];
        uint64_t in5 = out_ptr[WIDTH * 5];
        uint64_t in6 = out_ptr[WIDTH * 6];
        uint64_t in7 = out_ptr[WIDTH * 7];

        tmp0 = in2 ^ in5;
        tmp1 = in3 ^ in5;
        tmp2 = in1 ^ in7;
        out7 = in1 ^ in3 ^ in4;
        out0 = tmp0 ^ in4;
        tmp3 = tmp1 ^ in0;
        out5 = tmp2 ^ in2;
        out1 = tmp3 ^ in6;
        out6 = tmp0 ^ tmp3;
        tmp4 = tmp2 ^ out1;
        out3 = tmp4 ^ in4;
        out4 = tmp1 ^ tmp4;
        out2 = tmp0 ^ out4;

        out_ptr[0] = out0 ^ in_ptr[0];
        out_ptr[WIDTH] = out1 ^ in_ptr[WIDTH];
        out_ptr[WIDTH * 2] = out2 ^ in_ptr[WIDTH * 2];
        out_ptr[WIDTH * 3] = out3 ^ in_ptr[WIDTH * 3];
        out_ptr[WIDTH * 4] = out4 ^ in_ptr[WIDTH * 4];
        out_ptr[WIDTH * 5] = out5 ^ in_ptr[WIDTH * 5];
        out_ptr[WIDTH * 6] = out6 ^ in_ptr[WIDTH * 6];
        out_ptr[WIDTH * 7] = out7 ^ in_ptr[WIDTH * 7];

        in_ptr++;
        out_ptr++;
    }
}

static void gf8_muladd_5F(void *out, void *in)
{
    unsigned int i;
    uint64_t *in_ptr = (uint64_t *)in;
    uint64_t *out_ptr = (uint64_t *)out;

    for (i = 0; i < WIDTH; i++) {
        uint64_t out0, out1, out2, out3, out4, out5, out6, out7;
        uint64_t tmp0, tmp1, tmp2, tmp3;

        uint64_t in0 = out_ptr[0];
        uint64_t in1 = out_ptr[WIDTH];
        uint64_t in2 = out_ptr[WIDTH * 2];
        uint64_t in3 = out_ptr[WIDTH * 3];
        uint64_t in4 = out_ptr[WIDTH * 4];
        uint64_t in5 = out_ptr[WIDTH * 5];
        uint64_t in6 = out_ptr[WIDTH * 6];
        uint64_t in7 = out_ptr[WIDTH * 7];

        tmp0 = in1 ^ in5;
        tmp1 = in0 ^ in6;
        tmp2 = tmp0 ^ in7;
        tmp3 = tmp1 ^ in3;
        out2 = tmp1 ^ tmp2;
        out5 = tmp2 ^ in2;
        out6 = tmp3 ^ in2;
        out3 = out2 ^ in4;
        out4 = out3 ^ in5;
        out1 = tmp0 ^ tmp3;
        out7 = tmp3 ^ out4;
        out0 = out4 ^ out5 ^ in6;

        out_ptr[0] = out0 ^ in_ptr[0];
        out_ptr[WIDTH] = out1 ^ in_ptr[WIDTH];
        out_ptr[WIDTH * 2] = out2 ^ in_ptr[WIDTH * 2];
        out_ptr[WIDTH * 3] = out3 ^ in_ptr[WIDTH * 3];
        out_ptr[WIDTH * 4] = out4 ^ in_ptr[WIDTH * 4];
        out_ptr[WIDTH * 5] = out5 ^ in_ptr[WIDTH * 5];
        out_ptr[WIDTH * 6] = out6 ^ in_ptr[WIDTH * 6];
        out_ptr[WIDTH * 7] = out7 ^ in_ptr[WIDTH * 7];

        in_ptr++;
        out_ptr++;
    }
}

static void gf8_muladd_60(void *out, void *in)
{
    unsigned int i;
    uint64_t *in_ptr = (uint64_t *)in;
    uint64_t *out_ptr = (uint64_t *)out;

    for (i = 0; i < WIDTH; i++) {
        uint64_t out0, out1, out2, out3, out4, out5, out6, out7;
        uint64_t tmp0, tmp1;

        uint64_t in0 = out_ptr[0];
        uint64_t in1 = out_ptr[WIDTH];
        uint64_t in2 = out_ptr[WIDTH * 2];
        uint64_t in3 = out_ptr[WIDTH * 3];
        uint64_t in4 = out_ptr[WIDTH * 4];
        uint64_t in5 = out_ptr[WIDTH * 5];
        uint64_t in6 = out_ptr[WIDTH * 6];
        uint64_t in7 = out_ptr[WIDTH * 7];

        out4 = in2 ^ in5;
        tmp0 = in3 ^ in6;
        out1 = in3 ^ in4 ^ in7;
        out7 = out4 ^ in1;
        tmp1 = out4 ^ in4;
        out0 = tmp0 ^ in2;
        out5 = tmp0 ^ in0;
        out2 = tmp0 ^ tmp1;
        out3 = tmp1 ^ in7;
        out6 = out3 ^ out7 ^ in0;

        out_ptr[0] = out0 ^ in_ptr[0];
        out_ptr[WIDTH] = out1 ^ in_ptr[WIDTH];
        out_ptr[WIDTH * 2] = out2 ^ in_ptr[WIDTH * 2];
        out_ptr[WIDTH * 3] = out3 ^ in_ptr[WIDTH * 3];
        out_ptr[WIDTH * 4] = out4 ^ in_ptr[WIDTH * 4];
        out_ptr[WIDTH * 5] = out5 ^ in_ptr[WIDTH * 5];
        out_ptr[WIDTH * 6] = out6 ^ in_ptr[WIDTH * 6];
        out_ptr[WIDTH * 7] = out7 ^ in_ptr[WIDTH * 7];

        in_ptr++;
        out_ptr++;
    }
}

static void gf8_muladd_61(void *out, void *in)
{
    unsigned int i;
    uint64_t *in_ptr = (uint64_t *)in;
    uint64_t *out_ptr = (uint64_t *)out;

    for (i = 0; i < WIDTH; i++) {
        uint64_t out0, out1, out2, out3, out4, out5, out6, out7;
        uint64_t tmp0, tmp1;

        uint64_t in0 = out_ptr[0];
        uint64_t in1 = out_ptr[WIDTH];
        uint64_t in2 = out_ptr[WIDTH * 2];
        uint64_t in3 = out_ptr[WIDTH * 3];
        uint64_t in4 = out_ptr[WIDTH * 4];
        uint64_t in5 = out_ptr[WIDTH * 5];
        uint64_t in6 = out_ptr[WIDTH * 6];
        uint64_t in7 = out_ptr[WIDTH * 7];

        tmp0 = in2 ^ in5;
        out4 = tmp0 ^ in4;
        tmp1 = out4 ^ in3;
        out3 = tmp1 ^ in7;
        out2 = tmp1 ^ in2 ^ in6;
        out1 = tmp0 ^ out3 ^ in1;
        out0 = out2 ^ out4 ^ in0;
        out7 = tmp1 ^ out1;
        out6 = out0 ^ out1 ^ in2;
        out5 = tmp0 ^ out0;

        out_ptr[0] = out0 ^ in_ptr[0];
        out_ptr[WIDTH] = out1 ^ in_ptr[WIDTH];
        out_ptr[WIDTH * 2] = out2 ^ in_ptr[WIDTH * 2];
        out_ptr[WIDTH * 3] = out3 ^ in_ptr[WIDTH * 3];
        out_ptr[WIDTH * 4] = out4 ^ in_ptr[WIDTH * 4];
        out_ptr[WIDTH * 5] = out5 ^ in_ptr[WIDTH * 5];
        out_ptr[WIDTH * 6] = out6 ^ in_ptr[WIDTH * 6];
        out_ptr[WIDTH * 7] = out7 ^ in_ptr[WIDTH * 7];

        in_ptr++;
        out_ptr++;
    }
}

static void gf8_muladd_62(void *out, void *in)
{
    unsigned int i;
    uint64_t *in_ptr = (uint64_t *)in;
    uint64_t *out_ptr = (uint64_t *)out;

    for (i = 0; i < WIDTH; i++) {
        uint64_t out0, out1, out2, out3, out4, out5, out6, out7;
        uint64_t tmp0, tmp1, tmp2, tmp3;

        uint64_t in0 = out_ptr[0];
        uint64_t in1 = out_ptr[WIDTH];
        uint64_t in2 = out_ptr[WIDTH * 2];
        uint64_t in3 = out_ptr[WIDTH * 3];
        uint64_t in4 = out_ptr[WIDTH * 4];
        uint64_t in5 = out_ptr[WIDTH * 5];
        uint64_t in6 = out_ptr[WIDTH * 6];
        uint64_t in7 = out_ptr[WIDTH * 7];

        out3 = in4 ^ in5;
        tmp0 = in0 ^ in3 ^ in4;
        out1 = tmp0 ^ in7;
        out5 = tmp0 ^ in6;
        tmp1 = out1 ^ in0;
        tmp2 = tmp1 ^ out3;
        out4 = tmp2 ^ in2;
        tmp3 = tmp2 ^ in1;
        out0 = out4 ^ in5 ^ in6;
        out7 = tmp3 ^ out0;
        out6 = tmp0 ^ tmp3;
        out2 = tmp1 ^ out7;

        out_ptr[0] = out0 ^ in_ptr[0];
        out_ptr[WIDTH] = out1 ^ in_ptr[WIDTH];
        out_ptr[WIDTH * 2] = out2 ^ in_ptr[WIDTH * 2];
        out_ptr[WIDTH * 3] = out3 ^ in_ptr[WIDTH * 3];
        out_ptr[WIDTH * 4] = out4 ^ in_ptr[WIDTH * 4];
        out_ptr[WIDTH * 5] = out5 ^ in_ptr[WIDTH * 5];
        out_ptr[WIDTH * 6] = out6 ^ in_ptr[WIDTH * 6];
        out_ptr[WIDTH * 7] = out7 ^ in_ptr[WIDTH * 7];

        in_ptr++;
        out_ptr++;
    }
}

static void gf8_muladd_63(void *out, void *in)
{
    unsigned int i;
    uint64_t *in_ptr = (uint64_t *)in;
    uint64_t *out_ptr = (uint64_t *)out;

    for (i = 0; i < WIDTH; i++) {
        uint64_t out0, out1, out2, out3, out4, out5, out6, out7;
        uint64_t tmp0, tmp1, tmp2, tmp3, tmp4;

        uint64_t in0 = out_ptr[0];
        uint64_t in1 = out_ptr[WIDTH];
        uint64_t in2 = out_ptr[WIDTH * 2];
        uint64_t in3 = out_ptr[WIDTH * 3];
        uint64_t in4 = out_ptr[WIDTH * 4];
        uint64_t in5 = out_ptr[WIDTH * 5];
        uint64_t in6 = out_ptr[WIDTH * 6];
        uint64_t in7 = out_ptr[WIDTH * 7];

        tmp0 = in3 ^ in4;
        tmp1 = in1 ^ in7;
        out3 = tmp0 ^ in5;
        tmp2 = out3 ^ in6;
        out4 = out3 ^ in2 ^ in7;
        out5 = tmp2 ^ in0;
        tmp3 = out5 ^ in3;
        out0 = tmp3 ^ out4;
        out2 = tmp1 ^ tmp2;
        out6 = tmp1 ^ tmp3;
        tmp4 = tmp0 ^ out2;
        out1 = tmp4 ^ out5;
        out7 = tmp4 ^ in2;

        out_ptr[0] = out0 ^ in_ptr[0];
        out_ptr[WIDTH] = out1 ^ in_ptr[WIDTH];
        out_ptr[WIDTH * 2] = out2 ^ in_ptr[WIDTH * 2];
        out_ptr[WIDTH * 3] = out3 ^ in_ptr[WIDTH * 3];
        out_ptr[WIDTH * 4] = out4 ^ in_ptr[WIDTH * 4];
        out_ptr[WIDTH * 5] = out5 ^ in_ptr[WIDTH * 5];
        out_ptr[WIDTH * 6] = out6 ^ in_ptr[WIDTH * 6];
        out_ptr[WIDTH * 7] = out7 ^ in_ptr[WIDTH * 7];

        in_ptr++;
        out_ptr++;
    }
}

static void gf8_muladd_64(void *out, void *in)
{
    unsigned int i;
    uint64_t *in_ptr = (uint64_t *)in;
    uint64_t *out_ptr = (uint64_t *)out;

    for (i = 0; i < WIDTH; i++) {
        uint64_t out0, out1, out2, out3, out4, out5, out6, out7;
        uint64_t tmp0, tmp1;

        uint64_t in0 = out_ptr[0];
        uint64_t in1 = out_ptr[WIDTH];
        uint64_t in2 = out_ptr[WIDTH * 2];
        uint64_t in3 = out_ptr[WIDTH * 3];
        uint64_t in4 = out_ptr[WIDTH * 4];
        uint64_t in5 = out_ptr[WIDTH * 5];
        uint64_t in6 = out_ptr[WIDTH * 6];
        uint64_t in7 = out_ptr[WIDTH * 7];

        out0 = in2 ^ in3;
        out1 = in3 ^ in4;
        out7 = in1 ^ in2;
        tmp0 = in4 ^ in5;
        tmp1 = in0 ^ in7;
        out4 = in5 ^ in6 ^ in7;
        out2 = tmp0 ^ out0 ^ in0;
        out3 = tmp0 ^ out7 ^ in6;
        out5 = tmp1 ^ in6;
        out6 = tmp1 ^ in1;

        out_ptr[0] = out0 ^ in_ptr[0];
        out_ptr[WIDTH] = out1 ^ in_ptr[WIDTH];
        out_ptr[WIDTH * 2] = out2 ^ in_ptr[WIDTH * 2];
        out_ptr[WIDTH * 3] = out3 ^ in_ptr[WIDTH * 3];
        out_ptr[WIDTH * 4] = out4 ^ in_ptr[WIDTH * 4];
        out_ptr[WIDTH * 5] = out5 ^ in_ptr[WIDTH * 5];
        out_ptr[WIDTH * 6] = out6 ^ in_ptr[WIDTH * 6];
        out_ptr[WIDTH * 7] = out7 ^ in_ptr[WIDTH * 7];

        in_ptr++;
        out_ptr++;
    }
}

static void gf8_muladd_65(void *out, void *in)
{
    unsigned int i;
    uint64_t *in_ptr = (uint64_t *)in;
    uint64_t *out_ptr = (uint64_t *)out;

    for (i = 0; i < WIDTH; i++) {
        uint64_t out0, out1, out2, out3, out4, out5, out6, out7;
        uint64_t tmp0, tmp1, tmp2, tmp3;

        uint64_t in0 = out_ptr[0];
        uint64_t in1 = out_ptr[WIDTH];
        uint64_t in2 = out_ptr[WIDTH * 2];
        uint64_t in3 = out_ptr[WIDTH * 3];
        uint64_t in4 = out_ptr[WIDTH * 4];
        uint64_t in5 = out_ptr[WIDTH * 5];
        uint64_t in6 = out_ptr[WIDTH * 6];
        uint64_t in7 = out_ptr[WIDTH * 7];

        tmp0 = in0 ^ in3;
        tmp1 = in4 ^ in5;
        tmp2 = in6 ^ in7;
        out7 = in1 ^ in2 ^ in7;
        out1 = in1 ^ in3 ^ in4;
        out0 = tmp0 ^ in2;
        out2 = tmp0 ^ tmp1;
        out4 = tmp1 ^ tmp2;
        tmp3 = tmp2 ^ in0;
        out3 = out4 ^ out7 ^ in3;
        out5 = tmp3 ^ in5;
        out6 = tmp3 ^ in1;

        out_ptr[0] = out0 ^ in_ptr[0];
        out_ptr[WIDTH] = out1 ^ in_ptr[WIDTH];
        out_ptr[WIDTH * 2] = out2 ^ in_ptr[WIDTH * 2];
        out_ptr[WIDTH * 3] = out3 ^ in_ptr[WIDTH * 3];
        out_ptr[WIDTH * 4] = out4 ^ in_ptr[WIDTH * 4];
        out_ptr[WIDTH * 5] = out5 ^ in_ptr[WIDTH * 5];
        out_ptr[WIDTH * 6] = out6 ^ in_ptr[WIDTH * 6];
        out_ptr[WIDTH * 7] = out7 ^ in_ptr[WIDTH * 7];

        in_ptr++;
        out_ptr++;
    }
}

static void gf8_muladd_66(void *out, void *in)
{
    unsigned int i;
    uint64_t *in_ptr = (uint64_t *)in;
    uint64_t *out_ptr = (uint64_t *)out;

    for (i = 0; i < WIDTH; i++) {
        uint64_t out0, out1, out2, out3, out4, out5, out6, out7;
        uint64_t tmp0, tmp1, tmp2, tmp3, tmp4;

        uint64_t in0 = out_ptr[0];
        uint64_t in1 = out_ptr[WIDTH];
        uint64_t in2 = out_ptr[WIDTH * 2];
        uint64_t in3 = out_ptr[WIDTH * 3];
        uint64_t in4 = out_ptr[WIDTH * 4];
        uint64_t in5 = out_ptr[WIDTH * 5];
        uint64_t in6 = out_ptr[WIDTH * 6];
        uint64_t in7 = out_ptr[WIDTH * 7];

        tmp0 = in1 ^ in2;
        tmp1 = in2 ^ in3;
        tmp2 = in0 ^ in4;
        out7 = tmp0 ^ in6;
        out0 = tmp1 ^ in7;
        out1 = tmp2 ^ in3;
        tmp3 = tmp2 ^ in6;
        tmp4 = out1 ^ in5;
        out5 = tmp3 ^ in7;
        out4 = tmp3 ^ tmp4;
        out2 = tmp0 ^ tmp4 ^ in7;
        out6 = tmp1 ^ out2 ^ in4;
        out3 = tmp3 ^ out6;

        out_ptr[0] = out0 ^ in_ptr[0];
        out_ptr[WIDTH] = out1 ^ in_ptr[WIDTH];
        out_ptr[WIDTH * 2] = out2 ^ in_ptr[WIDTH * 2];
        out_ptr[WIDTH * 3] = out3 ^ in_ptr[WIDTH * 3];
        out_ptr[WIDTH * 4] = out4 ^ in_ptr[WIDTH * 4];
        out_ptr[WIDTH * 5] = out5 ^ in_ptr[WIDTH * 5];
        out_ptr[WIDTH * 6] = out6 ^ in_ptr[WIDTH * 6];
        out_ptr[WIDTH * 7] = out7 ^ in_ptr[WIDTH * 7];

        in_ptr++;
        out_ptr++;
    }
}

static void gf8_muladd_67(void *out, void *in)
{
    unsigned int i;
    uint64_t *in_ptr = (uint64_t *)in;
    uint64_t *out_ptr = (uint64_t *)out;

    for (i = 0; i < WIDTH; i++) {
        uint64_t out0, out1, out2, out3, out4, out5, out6, out7;
        uint64_t tmp0, tmp1, tmp2, tmp3;

        uint64_t in0 = out_ptr[0];
        uint64_t in1 = out_ptr[WIDTH];
        uint64_t in2 = out_ptr[WIDTH * 2];
        uint64_t in3 = out_ptr[WIDTH * 3];
        uint64_t in4 = out_ptr[WIDTH * 4];
        uint64_t in5 = out_ptr[WIDTH * 5];
        uint64_t in6 = out_ptr[WIDTH * 6];
        uint64_t in7 = out_ptr[WIDTH * 7];

        tmp0 = in0 ^ in3;
        tmp1 = tmp0 ^ in1;
        tmp2 = tmp0 ^ in7;
        out1 = tmp1 ^ in4;
        out0 = tmp2 ^ in2;
        tmp3 = out1 ^ in7;
        out2 = tmp3 ^ in5;
        out3 = out2 ^ in0 ^ in6;
        out7 = tmp1 ^ out0 ^ in6;
        out5 = tmp1 ^ out3;
        out4 = tmp2 ^ out5;
        out6 = tmp3 ^ out4;

        out_ptr[0] = out0 ^ in_ptr[0];
        out_ptr[WIDTH] = out1 ^ in_ptr[WIDTH];
        out_ptr[WIDTH * 2] = out2 ^ in_ptr[WIDTH * 2];
        out_ptr[WIDTH * 3] = out3 ^ in_ptr[WIDTH * 3];
        out_ptr[WIDTH * 4] = out4 ^ in_ptr[WIDTH * 4];
        out_ptr[WIDTH * 5] = out5 ^ in_ptr[WIDTH * 5];
        out_ptr[WIDTH * 6] = out6 ^ in_ptr[WIDTH * 6];
        out_ptr[WIDTH * 7] = out7 ^ in_ptr[WIDTH * 7];

        in_ptr++;
        out_ptr++;
    }
}

static void gf8_muladd_68(void *out, void *in)
{
    unsigned int i;
    uint64_t *in_ptr = (uint64_t *)in;
    uint64_t *out_ptr = (uint64_t *)out;

    for (i = 0; i < WIDTH; i++) {
        uint64_t out0, out1, out2, out3, out4, out5, out6, out7;
        uint64_t tmp0, tmp1, tmp2, tmp3;

        uint64_t in0 = out_ptr[0];
        uint64_t in1 = out_ptr[WIDTH];
        uint64_t in2 = out_ptr[WIDTH * 2];
        uint64_t in3 = out_ptr[WIDTH * 3];
        uint64_t in4 = out_ptr[WIDTH * 4];
        uint64_t in5 = out_ptr[WIDTH * 5];
        uint64_t in6 = out_ptr[WIDTH * 6];
        uint64_t in7 = out_ptr[WIDTH * 7];

        tmp0 = in3 ^ in4;
        tmp1 = in2 ^ in3 ^ in5;
        tmp2 = tmp0 ^ in1;
        tmp3 = tmp0 ^ in6;
        out0 = tmp1 ^ in6;
        out6 = tmp2 ^ in0;
        out7 = tmp1 ^ tmp2;
        out1 = tmp3 ^ in7;
        out2 = out1 ^ in2;
        out4 = tmp2 ^ out2;
        out3 = out4 ^ out6 ^ in3;
        out5 = tmp3 ^ out3;

        out_ptr[0] = out0 ^ in_ptr[0];
        out_ptr[WIDTH] = out1 ^ in_ptr[WIDTH];
        out_ptr[WIDTH * 2] = out2 ^ in_ptr[WIDTH * 2];
        out_ptr[WIDTH * 3] = out3 ^ in_ptr[WIDTH * 3];
        out_ptr[WIDTH * 4] = out4 ^ in_ptr[WIDTH * 4];
        out_ptr[WIDTH * 5] = out5 ^ in_ptr[WIDTH * 5];
        out_ptr[WIDTH * 6] = out6 ^ in_ptr[WIDTH * 6];
        out_ptr[WIDTH * 7] = out7 ^ in_ptr[WIDTH * 7];

        in_ptr++;
        out_ptr++;
    }
}

static void gf8_muladd_69(void *out, void *in)
{
    unsigned int i;
    uint64_t *in_ptr = (uint64_t *)in;
    uint64_t *out_ptr = (uint64_t *)out;

    for (i = 0; i < WIDTH; i++) {
        uint64_t out0, out1, out2, out3, out4, out5, out6, out7;
        uint64_t tmp0;

        uint64_t in0 = out_ptr[0];
        uint64_t in1 = out_ptr[WIDTH];
        uint64_t in2 = out_ptr[WIDTH * 2];
        uint64_t in3 = out_ptr[WIDTH * 3];
        uint64_t in4 = out_ptr[WIDTH * 4];
        uint64_t in5 = out_ptr[WIDTH * 5];
        uint64_t in6 = out_ptr[WIDTH * 6];
        uint64_t in7 = out_ptr[WIDTH * 7];

        tmp0 = in6 ^ in7;
        out2 = tmp0 ^ in3 ^ in4;
        out1 = out2 ^ in1;
        out3 = out2 ^ in0 ^ in2;
        out4 = out1 ^ in2 ^ in3;
        out6 = out1 ^ in0 ^ in7;
        out7 = out4 ^ in5 ^ in6;
        out5 = out4 ^ out6 ^ in5;
        out0 = tmp0 ^ out5;

        out_ptr[0] = out0 ^ in_ptr[0];
        out_ptr[WIDTH] = out1 ^ in_ptr[WIDTH];
        out_ptr[WIDTH * 2] = out2 ^ in_ptr[WIDTH * 2];
        out_ptr[WIDTH * 3] = out3 ^ in_ptr[WIDTH * 3];
        out_ptr[WIDTH * 4] = out4 ^ in_ptr[WIDTH * 4];
        out_ptr[WIDTH * 5] = out5 ^ in_ptr[WIDTH * 5];
        out_ptr[WIDTH * 6] = out6 ^ in_ptr[WIDTH * 6];
        out_ptr[WIDTH * 7] = out7 ^ in_ptr[WIDTH * 7];

        in_ptr++;
        out_ptr++;
    }
}

static void gf8_muladd_6A(void *out, void *in)
{
    unsigned int i;
    uint64_t *in_ptr = (uint64_t *)in;
    uint64_t *out_ptr = (uint64_t *)out;

    for (i = 0; i < WIDTH; i++) {
        uint64_t out0, out1, out2, out3, out4, out5, out6, out7;
        uint64_t tmp0, tmp1, tmp2;

        uint64_t in0 = out_ptr[0];
        uint64_t in1 = out_ptr[WIDTH];
        uint64_t in2 = out_ptr[WIDTH * 2];
        uint64_t in3 = out_ptr[WIDTH * 3];
        uint64_t in4 = out_ptr[WIDTH * 4];
        uint64_t in5 = out_ptr[WIDTH * 5];
        uint64_t in6 = out_ptr[WIDTH * 6];
        uint64_t in7 = out_ptr[WIDTH * 7];

        tmp0 = in2 ^ in6;
        out3 = in0 ^ in4 ^ in6;
        tmp1 = tmp0 ^ in3;
        out4 = tmp1 ^ in1;
        tmp2 = tmp1 ^ in7;
        out2 = out4 ^ in4;
        out0 = tmp2 ^ in5;
        out5 = tmp2 ^ out3;
        out7 = out2 ^ in3 ^ in5;
        out1 = tmp0 ^ out5;
        out6 = tmp1 ^ out7 ^ in0;

        out_ptr[0] = out0 ^ in_ptr[0];
        out_ptr[WIDTH] = out1 ^ in_ptr[WIDTH];
        out_ptr[WIDTH * 2] = out2 ^ in_ptr[WIDTH * 2];
        out_ptr[WIDTH * 3] = out3 ^ in_ptr[WIDTH * 3];
        out_ptr[WIDTH * 4] = out4 ^ in_ptr[WIDTH * 4];
        out_ptr[WIDTH * 5] = out5 ^ in_ptr[WIDTH * 5];
        out_ptr[WIDTH * 6] = out6 ^ in_ptr[WIDTH * 6];
        out_ptr[WIDTH * 7] = out7 ^ in_ptr[WIDTH * 7];

        in_ptr++;
        out_ptr++;
    }
}

static void gf8_muladd_6B(void *out, void *in)
{
    unsigned int i;
    uint64_t *in_ptr = (uint64_t *)in;
    uint64_t *out_ptr = (uint64_t *)out;

    for (i = 0; i < WIDTH; i++) {
        uint64_t out0, out1, out2, out3, out4, out5, out6, out7;
        uint64_t tmp0, tmp1;

        uint64_t in0 = out_ptr[0];
        uint64_t in1 = out_ptr[WIDTH];
        uint64_t in2 = out_ptr[WIDTH * 2];
        uint64_t in3 = out_ptr[WIDTH * 3];
        uint64_t in4 = out_ptr[WIDTH * 4];
        uint64_t in5 = out_ptr[WIDTH * 5];
        uint64_t in6 = out_ptr[WIDTH * 6];
        uint64_t in7 = out_ptr[WIDTH * 7];

        tmp0 = in4 ^ in6;
        out2 = tmp0 ^ in1 ^ in3;
        out4 = out2 ^ in2;
        tmp1 = out2 ^ in0;
        out7 = out4 ^ in3 ^ in5 ^ in7;
        out1 = tmp1 ^ in7;
        out3 = tmp1 ^ in1;
        out6 = tmp1 ^ in5;
        out0 = tmp1 ^ out7 ^ in6;
        out5 = tmp0 ^ out0;

        out_ptr[0] = out0 ^ in_ptr[0];
        out_ptr[WIDTH] = out1 ^ in_ptr[WIDTH];
        out_ptr[WIDTH * 2] = out2 ^ in_ptr[WIDTH * 2];
        out_ptr[WIDTH * 3] = out3 ^ in_ptr[WIDTH * 3];
        out_ptr[WIDTH * 4] = out4 ^ in_ptr[WIDTH * 4];
        out_ptr[WIDTH * 5] = out5 ^ in_ptr[WIDTH * 5];
        out_ptr[WIDTH * 6] = out6 ^ in_ptr[WIDTH * 6];
        out_ptr[WIDTH * 7] = out7 ^ in_ptr[WIDTH * 7];

        in_ptr++;
        out_ptr++;
    }
}

static void gf8_muladd_6C(void *out, void *in)
{
    unsigned int i;
    uint64_t *in_ptr = (uint64_t *)in;
    uint64_t *out_ptr = (uint64_t *)out;

    for (i = 0; i < WIDTH; i++) {
        uint64_t out0, out1, out2, out3, out4, out5, out6, out7;
        uint64_t tmp0, tmp1;

        uint64_t in0 = out_ptr[0];
        uint64_t in1 = out_ptr[WIDTH];
        uint64_t in2 = out_ptr[WIDTH * 2];
        uint64_t in3 = out_ptr[WIDTH * 3];
        uint64_t in4 = out_ptr[WIDTH * 4];
        uint64_t in5 = out_ptr[WIDTH * 5];
        uint64_t in6 = out_ptr[WIDTH * 6];
        uint64_t in7 = out_ptr[WIDTH * 7];

        out4 = in1;
        tmp0 = in2 ^ in3;
        out5 = in0 ^ in2;
        out1 = in3 ^ in4 ^ in6;
        tmp1 = out5 ^ in1;
        out0 = tmp0 ^ in5;
        out6 = tmp0 ^ tmp1;
        out3 = tmp1 ^ in4;
        out7 = out3 ^ in0;
        out2 = out6 ^ out7 ^ in7;

        out_ptr[0] = out0 ^ in_ptr[0];
        out_ptr[WIDTH] = out1 ^ in_ptr[WIDTH];
        out_ptr[WIDTH * 2] = out2 ^ in_ptr[WIDTH * 2];
        out_ptr[WIDTH * 3] = out3 ^ in_ptr[WIDTH * 3];
        out_ptr[WIDTH * 4] = out4 ^ in_ptr[WIDTH * 4];
        out_ptr[WIDTH * 5] = out5 ^ in_ptr[WIDTH * 5];
        out_ptr[WIDTH * 6] = out6 ^ in_ptr[WIDTH * 6];
        out_ptr[WIDTH * 7] = out7 ^ in_ptr[WIDTH * 7];

        in_ptr++;
        out_ptr++;
    }
}

static void gf8_muladd_6D(void *out, void *in)
{
    unsigned int i;
    uint64_t *in_ptr = (uint64_t *)in;
    uint64_t *out_ptr = (uint64_t *)out;

    for (i = 0; i < WIDTH; i++) {
        uint64_t out0, out1, out2, out3, out4, out5, out6, out7;
        uint64_t tmp0, tmp1;

        uint64_t in0 = out_ptr[0];
        uint64_t in1 = out_ptr[WIDTH];
        uint64_t in2 = out_ptr[WIDTH * 2];
        uint64_t in3 = out_ptr[WIDTH * 3];
        uint64_t in4 = out_ptr[WIDTH * 4];
        uint64_t in5 = out_ptr[WIDTH * 5];
        uint64_t in6 = out_ptr[WIDTH * 6];
        uint64_t in7 = out_ptr[WIDTH * 7];

        out4 = in1 ^ in4;
        tmp0 = in0 ^ in2;
        tmp1 = out4 ^ in3;
        out7 = out4 ^ in2 ^ in7;
        out5 = tmp0 ^ in5;
        out3 = tmp0 ^ tmp1;
        out1 = tmp1 ^ in6;
        out0 = out5 ^ in3;
        out2 = out3 ^ out7 ^ in4;
        out6 = out1 ^ in0 ^ in4;

        out_ptr[0] = out0 ^ in_ptr[0];
        out_ptr[WIDTH] = out1 ^ in_ptr[WIDTH];
        out_ptr[WIDTH * 2] = out2 ^ in_ptr[WIDTH * 2];
        out_ptr[WIDTH * 3] = out3 ^ in_ptr[WIDTH * 3];
        out_ptr[WIDTH * 4] = out4 ^ in_ptr[WIDTH * 4];
        out_ptr[WIDTH * 5] = out5 ^ in_ptr[WIDTH * 5];
        out_ptr[WIDTH * 6] = out6 ^ in_ptr[WIDTH * 6];
        out_ptr[WIDTH * 7] = out7 ^ in_ptr[WIDTH * 7];

        in_ptr++;
        out_ptr++;
    }
}

static void gf8_muladd_6E(void *out, void *in)
{
    unsigned int i;
    uint64_t *in_ptr = (uint64_t *)in;
    uint64_t *out_ptr = (uint64_t *)out;

    for (i = 0; i < WIDTH; i++) {
        uint64_t out0, out1, out2, out3, out4, out5, out6, out7;
        uint64_t tmp0, tmp1, tmp2;

        uint64_t in0 = out_ptr[0];
        uint64_t in1 = out_ptr[WIDTH];
        uint64_t in2 = out_ptr[WIDTH * 2];
        uint64_t in3 = out_ptr[WIDTH * 3];
        uint64_t in4 = out_ptr[WIDTH * 4];
        uint64_t in5 = out_ptr[WIDTH * 5];
        uint64_t in6 = out_ptr[WIDTH * 6];
        uint64_t in7 = out_ptr[WIDTH * 7];

        tmp0 = in1 ^ in3;
        tmp1 = in0 ^ in4;
        out4 = tmp0 ^ in7;
        out6 = tmp0 ^ in0 ^ in5;
        out5 = tmp1 ^ in2;
        tmp2 = tmp1 ^ in3;
        out3 = tmp2 ^ out4;
        out1 = tmp2 ^ in6;
        out2 = tmp0 ^ out5;
        out0 = out2 ^ out3 ^ in5;
        out7 = out1 ^ out2 ^ in4;

        out_ptr[0] = out0 ^ in_ptr[0];
        out_ptr[WIDTH] = out1 ^ in_ptr[WIDTH];
        out_ptr[WIDTH * 2] = out2 ^ in_ptr[WIDTH * 2];
        out_ptr[WIDTH * 3] = out3 ^ in_ptr[WIDTH * 3];
        out_ptr[WIDTH * 4] = out4 ^ in_ptr[WIDTH * 4];
        out_ptr[WIDTH * 5] = out5 ^ in_ptr[WIDTH * 5];
        out_ptr[WIDTH * 6] = out6 ^ in_ptr[WIDTH * 6];
        out_ptr[WIDTH * 7] = out7 ^ in_ptr[WIDTH * 7];

        in_ptr++;
        out_ptr++;
    }
}

static void gf8_muladd_6F(void *out, void *in)
{
    unsigned int i;
    uint64_t *in_ptr = (uint64_t *)in;
    uint64_t *out_ptr = (uint64_t *)out;

    for (i = 0; i < WIDTH; i++) {
        uint64_t out0, out1, out2, out3, out4, out5, out6, out7;
        uint64_t tmp0, tmp1, tmp2;

        uint64_t in0 = out_ptr[0];
        uint64_t in1 = out_ptr[WIDTH];
        uint64_t in2 = out_ptr[WIDTH * 2];
        uint64_t in3 = out_ptr[WIDTH * 3];
        uint64_t in4 = out_ptr[WIDTH * 4];
        uint64_t in5 = out_ptr[WIDTH * 5];
        uint64_t in6 = out_ptr[WIDTH * 6];
        uint64_t in7 = out_ptr[WIDTH * 7];

        tmp0 = in3 ^ in7;
        tmp1 = tmp0 ^ in4;
        tmp2 = tmp0 ^ in0 ^ in2;
        out4 = tmp1 ^ in1;
        out0 = tmp2 ^ in5;
        out3 = out4 ^ in0;
        out2 = out3 ^ in7;
        out1 = out2 ^ in6;
        out6 = out1 ^ in4 ^ in5;
        out7 = tmp2 ^ out1;
        out5 = tmp1 ^ out0;

        out_ptr[0] = out0 ^ in_ptr[0];
        out_ptr[WIDTH] = out1 ^ in_ptr[WIDTH];
        out_ptr[WIDTH * 2] = out2 ^ in_ptr[WIDTH * 2];
        out_ptr[WIDTH * 3] = out3 ^ in_ptr[WIDTH * 3];
        out_ptr[WIDTH * 4] = out4 ^ in_ptr[WIDTH * 4];
        out_ptr[WIDTH * 5] = out5 ^ in_ptr[WIDTH * 5];
        out_ptr[WIDTH * 6] = out6 ^ in_ptr[WIDTH * 6];
        out_ptr[WIDTH * 7] = out7 ^ in_ptr[WIDTH * 7];

        in_ptr++;
        out_ptr++;
    }
}

static void gf8_muladd_70(void *out, void *in)
{
    unsigned int i;
    uint64_t *in_ptr = (uint64_t *)in;
    uint64_t *out_ptr = (uint64_t *)out;

    for (i = 0; i < WIDTH; i++) {
        uint64_t out0, out1, out2, out3, out4, out5, out6, out7;
        uint64_t tmp0, tmp1, tmp2;

        uint64_t in0 = out_ptr[0];
        uint64_t in1 = out_ptr[WIDTH];
        uint64_t in2 = out_ptr[WIDTH * 2];
        uint64_t in3 = out_ptr[WIDTH * 3];
        uint64_t in4 = out_ptr[WIDTH * 4];
        uint64_t in5 = out_ptr[WIDTH * 5];
        uint64_t in6 = out_ptr[WIDTH * 6];
        uint64_t in7 = out_ptr[WIDTH * 7];

        out3 = in2;
        tmp0 = in2 ^ in4;
        out2 = in2 ^ in3 ^ in5;
        tmp1 = tmp0 ^ in6;
        tmp2 = out2 ^ in7;
        out0 = tmp1 ^ in3;
        out4 = tmp1 ^ in0;
        out7 = tmp2 ^ in1;
        out6 = out4 ^ in1;
        out5 = out7 ^ in0 ^ in2;
        out1 = tmp0 ^ tmp2;

        out_ptr[0] = out0 ^ in_ptr[0];
        out_ptr[WIDTH] = out1 ^ in_ptr[WIDTH];
        out_ptr[WIDTH * 2] = out2 ^ in_ptr[WIDTH * 2];
        out_ptr[WIDTH * 3] = out3 ^ in_ptr[WIDTH * 3];
        out_ptr[WIDTH * 4] = out4 ^ in_ptr[WIDTH * 4];
        out_ptr[WIDTH * 5] = out5 ^ in_ptr[WIDTH * 5];
        out_ptr[WIDTH * 6] = out6 ^ in_ptr[WIDTH * 6];
        out_ptr[WIDTH * 7] = out7 ^ in_ptr[WIDTH * 7];

        in_ptr++;
        out_ptr++;
    }
}

static void gf8_muladd_71(void *out, void *in)
{
    unsigned int i;
    uint64_t *in_ptr = (uint64_t *)in;
    uint64_t *out_ptr = (uint64_t *)out;

    for (i = 0; i < WIDTH; i++) {
        uint64_t out0, out1, out2, out3, out4, out5, out6, out7;
        uint64_t tmp0, tmp1, tmp2;

        uint64_t in0 = out_ptr[0];
        uint64_t in1 = out_ptr[WIDTH];
        uint64_t in2 = out_ptr[WIDTH * 2];
        uint64_t in3 = out_ptr[WIDTH * 3];
        uint64_t in4 = out_ptr[WIDTH * 4];
        uint64_t in5 = out_ptr[WIDTH * 5];
        uint64_t in6 = out_ptr[WIDTH * 6];
        uint64_t in7 = out_ptr[WIDTH * 7];

        out2 = in3 ^ in5;
        out3 = in2 ^ in3;
        tmp0 = in0 ^ in2;
        tmp1 = out2 ^ in1;
        out4 = tmp0 ^ in6;
        tmp2 = tmp0 ^ in1;
        out7 = tmp1 ^ in2;
        out1 = tmp1 ^ in4 ^ in7;
        out0 = out4 ^ in3 ^ in4;
        out6 = tmp2 ^ in4;
        out5 = tmp2 ^ out3 ^ in7;

        out_ptr[0] = out0 ^ in_ptr[0];
        out_ptr[WIDTH] = out1 ^ in_ptr[WIDTH];
        out_ptr[WIDTH * 2] = out2 ^ in_ptr[WIDTH * 2];
        out_ptr[WIDTH * 3] = out3 ^ in_ptr[WIDTH * 3];
        out_ptr[WIDTH * 4] = out4 ^ in_ptr[WIDTH * 4];
        out_ptr[WIDTH * 5] = out5 ^ in_ptr[WIDTH * 5];
        out_ptr[WIDTH * 6] = out6 ^ in_ptr[WIDTH * 6];
        out_ptr[WIDTH * 7] = out7 ^ in_ptr[WIDTH * 7];

        in_ptr++;
        out_ptr++;
    }
}

static void gf8_muladd_72(void *out, void *in)
{
    unsigned int i;
    uint64_t *in_ptr = (uint64_t *)in;
    uint64_t *out_ptr = (uint64_t *)out;

    for (i = 0; i < WIDTH; i++) {
        uint64_t out0, out1, out2, out3, out4, out5, out6, out7;
        uint64_t tmp0, tmp1, tmp2;

        uint64_t in0 = out_ptr[0];
        uint64_t in1 = out_ptr[WIDTH];
        uint64_t in2 = out_ptr[WIDTH * 2];
        uint64_t in3 = out_ptr[WIDTH * 3];
        uint64_t in4 = out_ptr[WIDTH * 4];
        uint64_t in5 = out_ptr[WIDTH * 5];
        uint64_t in6 = out_ptr[WIDTH * 6];
        uint64_t in7 = out_ptr[WIDTH * 7];

        out3 = in7;
        tmp0 = in0 ^ in4;
        tmp1 = tmp0 ^ in3 ^ in7;
        out1 = tmp1 ^ in5;
        out5 = out1 ^ in1;
        tmp2 = tmp0 ^ out5;
        out2 = tmp2 ^ in2;
        out7 = out2 ^ in6;
        out6 = tmp1 ^ out7;
        out4 = tmp2 ^ out6;
        out0 = out4 ^ in0;

        out_ptr[0] = out0 ^ in_ptr[0];
        out_ptr[WIDTH] = out1 ^ in_ptr[WIDTH];
        out_ptr[WIDTH * 2] = out2 ^ in_ptr[WIDTH * 2];
        out_ptr[WIDTH * 3] = out3 ^ in_ptr[WIDTH * 3];
        out_ptr[WIDTH * 4] = out4 ^ in_ptr[WIDTH * 4];
        out_ptr[WIDTH * 5] = out5 ^ in_ptr[WIDTH * 5];
        out_ptr[WIDTH * 6] = out6 ^ in_ptr[WIDTH * 6];
        out_ptr[WIDTH * 7] = out7 ^ in_ptr[WIDTH * 7];

        in_ptr++;
        out_ptr++;
    }
}

static void gf8_muladd_73(void *out, void *in)
{
    unsigned int i;
    uint64_t *in_ptr = (uint64_t *)in;
    uint64_t *out_ptr = (uint64_t *)out;

    for (i = 0; i < WIDTH; i++) {
        uint64_t out0, out1, out2, out3, out4, out5, out6, out7;

        uint64_t in0 = out_ptr[0];
        uint64_t in1 = out_ptr[WIDTH];
        uint64_t in2 = out_ptr[WIDTH * 2];
        uint64_t in3 = out_ptr[WIDTH * 3];
        uint64_t in4 = out_ptr[WIDTH * 4];
        uint64_t in5 = out_ptr[WIDTH * 5];
        uint64_t in6 = out_ptr[WIDTH * 6];
        uint64_t in7 = out_ptr[WIDTH * 7];

        out3 = in3 ^ in7;
        out2 = out3 ^ in1 ^ in5;
        out1 = out2 ^ in0 ^ in4;
        out5 = out1 ^ in5;
        out6 = out1 ^ out3 ^ in2;
        out0 = out2 ^ out6 ^ in6;
        out7 = out0 ^ out1 ^ in3;
        out4 = out0 ^ in4;

        out_ptr[0] = out0 ^ in_ptr[0];
        out_ptr[WIDTH] = out1 ^ in_ptr[WIDTH];
        out_ptr[WIDTH * 2] = out2 ^ in_ptr[WIDTH * 2];
        out_ptr[WIDTH * 3] = out3 ^ in_ptr[WIDTH * 3];
        out_ptr[WIDTH * 4] = out4 ^ in_ptr[WIDTH * 4];
        out_ptr[WIDTH * 5] = out5 ^ in_ptr[WIDTH * 5];
        out_ptr[WIDTH * 6] = out6 ^ in_ptr[WIDTH * 6];
        out_ptr[WIDTH * 7] = out7 ^ in_ptr[WIDTH * 7];

        in_ptr++;
        out_ptr++;
    }
}

static void gf8_muladd_74(void *out, void *in)
{
    unsigned int i;
    uint64_t *in_ptr = (uint64_t *)in;
    uint64_t *out_ptr = (uint64_t *)out;

    for (i = 0; i < WIDTH; i++) {
        uint64_t out0, out1, out2, out3, out4, out5, out6, out7;
        uint64_t tmp0, tmp1;

        uint64_t in0 = out_ptr[0];
        uint64_t in1 = out_ptr[WIDTH];
        uint64_t in2 = out_ptr[WIDTH * 2];
        uint64_t in3 = out_ptr[WIDTH * 3];
        uint64_t in4 = out_ptr[WIDTH * 4];
        uint64_t in5 = out_ptr[WIDTH * 5];
        uint64_t in6 = out_ptr[WIDTH * 6];
        uint64_t in7 = out_ptr[WIDTH * 7];

        tmp0 = in3 ^ in4;
        tmp1 = in1 ^ in2 ^ in6;
        out4 = in0 ^ in4 ^ in7;
        out5 = in0 ^ in1 ^ in5;
        out0 = tmp0 ^ in2;
        out1 = tmp0 ^ in5;
        out3 = tmp1 ^ in7;
        out6 = tmp1 ^ in0;
        out2 = tmp1 ^ out5 ^ in3;
        out7 = out3 ^ in3 ^ in6;

        out_ptr[0] = out0 ^ in_ptr[0];
        out_ptr[WIDTH] = out1 ^ in_ptr[WIDTH];
        out_ptr[WIDTH * 2] = out2 ^ in_ptr[WIDTH * 2];
        out_ptr[WIDTH * 3] = out3 ^ in_ptr[WIDTH * 3];
        out_ptr[WIDTH * 4] = out4 ^ in_ptr[WIDTH * 4];
        out_ptr[WIDTH * 5] = out5 ^ in_ptr[WIDTH * 5];
        out_ptr[WIDTH * 6] = out6 ^ in_ptr[WIDTH * 6];
        out_ptr[WIDTH * 7] = out7 ^ in_ptr[WIDTH * 7];

        in_ptr++;
        out_ptr++;
    }
}

static void gf8_muladd_75(void *out, void *in)
{
    unsigned int i;
    uint64_t *in_ptr = (uint64_t *)in;
    uint64_t *out_ptr = (uint64_t *)out;

    for (i = 0; i < WIDTH; i++) {
        uint64_t out0, out1, out2, out3, out4, out5, out6, out7;
        uint64_t tmp0, tmp1, tmp2;

        uint64_t in0 = out_ptr[0];
        uint64_t in1 = out_ptr[WIDTH];
        uint64_t in2 = out_ptr[WIDTH * 2];
        uint64_t in3 = out_ptr[WIDTH * 3];
        uint64_t in4 = out_ptr[WIDTH * 4];
        uint64_t in5 = out_ptr[WIDTH * 5];
        uint64_t in6 = out_ptr[WIDTH * 6];
        uint64_t in7 = out_ptr[WIDTH * 7];

        out4 = in0 ^ in7;
        tmp0 = in1 ^ in3;
        out5 = in0 ^ in1;
        out7 = tmp0 ^ in2;
        tmp1 = tmp0 ^ in4;
        out6 = out5 ^ in2;
        tmp2 = out7 ^ in6;
        out1 = tmp1 ^ in5;
        out0 = tmp1 ^ out6;
        out3 = tmp2 ^ in7;
        out2 = tmp2 ^ out6 ^ in5;

        out_ptr[0] = out0 ^ in_ptr[0];
        out_ptr[WIDTH] = out1 ^ in_ptr[WIDTH];
        out_ptr[WIDTH * 2] = out2 ^ in_ptr[WIDTH * 2];
        out_ptr[WIDTH * 3] = out3 ^ in_ptr[WIDTH * 3];
        out_ptr[WIDTH * 4] = out4 ^ in_ptr[WIDTH * 4];
        out_ptr[WIDTH * 5] = out5 ^ in_ptr[WIDTH * 5];
        out_ptr[WIDTH * 6] = out6 ^ in_ptr[WIDTH * 6];
        out_ptr[WIDTH * 7] = out7 ^ in_ptr[WIDTH * 7];

        in_ptr++;
        out_ptr++;
    }
}

static void gf8_muladd_76(void *out, void *in)
{
    unsigned int i;
    uint64_t *in_ptr = (uint64_t *)in;
    uint64_t *out_ptr = (uint64_t *)out;

    for (i = 0; i < WIDTH; i++) {
        uint64_t out0, out1, out2, out3, out4, out5, out6, out7;
        uint64_t tmp0, tmp1, tmp2, tmp3;

        uint64_t in0 = out_ptr[0];
        uint64_t in1 = out_ptr[WIDTH];
        uint64_t in2 = out_ptr[WIDTH * 2];
        uint64_t in3 = out_ptr[WIDTH * 3];
        uint64_t in4 = out_ptr[WIDTH * 4];
        uint64_t in5 = out_ptr[WIDTH * 5];
        uint64_t in6 = out_ptr[WIDTH * 6];
        uint64_t in7 = out_ptr[WIDTH * 7];

        out3 = in1 ^ in6;
        tmp0 = in0 ^ in5;
        tmp1 = in3 ^ in7;
        tmp2 = tmp0 ^ in4;
        tmp3 = tmp1 ^ in2;
        out5 = tmp2 ^ in1;
        out1 = tmp2 ^ in3;
        out0 = tmp3 ^ in4;
        out4 = out1 ^ in5;
        out7 = tmp3 ^ out3;
        out2 = tmp0 ^ out7;
        out6 = tmp1 ^ out2;

        out_ptr[0] = out0 ^ in_ptr[0];
        out_ptr[WIDTH] = out1 ^ in_ptr[WIDTH];
        out_ptr[WIDTH * 2] = out2 ^ in_ptr[WIDTH * 2];
        out_ptr[WIDTH * 3] = out3 ^ in_ptr[WIDTH * 3];
        out_ptr[WIDTH * 4] = out4 ^ in_ptr[WIDTH * 4];
        out_ptr[WIDTH * 5] = out5 ^ in_ptr[WIDTH * 5];
        out_ptr[WIDTH * 6] = out6 ^ in_ptr[WIDTH * 6];
        out_ptr[WIDTH * 7] = out7 ^ in_ptr[WIDTH * 7];

        in_ptr++;
        out_ptr++;
    }
}

static void gf8_muladd_77(void *out, void *in)
{
    unsigned int i;
    uint64_t *in_ptr = (uint64_t *)in;
    uint64_t *out_ptr = (uint64_t *)out;

    for (i = 0; i < WIDTH; i++) {
        uint64_t out0, out1, out2, out3, out4, out5, out6, out7;
        uint64_t tmp0, tmp1, tmp2, tmp3;

        uint64_t in0 = out_ptr[0];
        uint64_t in1 = out_ptr[WIDTH];
        uint64_t in2 = out_ptr[WIDTH * 2];
        uint64_t in3 = out_ptr[WIDTH * 3];
        uint64_t in4 = out_ptr[WIDTH * 4];
        uint64_t in5 = out_ptr[WIDTH * 5];
        uint64_t in6 = out_ptr[WIDTH * 6];
        uint64_t in7 = out_ptr[WIDTH * 7];

        out4 = in0 ^ in3;
        tmp0 = in1 ^ in4;
        tmp1 = in1 ^ in6;
        tmp2 = out4 ^ in5;
        out5 = tmp0 ^ in0;
        out1 = tmp0 ^ tmp2;
        out3 = tmp1 ^ in3;
        out2 = tmp1 ^ tmp2 ^ in7;
        out7 = out3 ^ in2;
        tmp3 = out7 ^ in6;
        out6 = tmp2 ^ tmp3;
        out0 = tmp3 ^ out5 ^ in7;

        out_ptr[0] = out0 ^ in_ptr[0];
        out_ptr[WIDTH] = out1 ^ in_ptr[WIDTH];
        out_ptr[WIDTH * 2] = out2 ^ in_ptr[WIDTH * 2];
        out_ptr[WIDTH * 3] = out3 ^ in_ptr[WIDTH * 3];
        out_ptr[WIDTH * 4] = out4 ^ in_ptr[WIDTH * 4];
        out_ptr[WIDTH * 5] = out5 ^ in_ptr[WIDTH * 5];
        out_ptr[WIDTH * 6] = out6 ^ in_ptr[WIDTH * 6];
        out_ptr[WIDTH * 7] = out7 ^ in_ptr[WIDTH * 7];

        in_ptr++;
        out_ptr++;
    }
}

static void gf8_muladd_78(void *out, void *in)
{
    unsigned int i;
    uint64_t *in_ptr = (uint64_t *)in;
    uint64_t *out_ptr = (uint64_t *)out;

    for (i = 0; i < WIDTH; i++) {
        uint64_t out0, out1, out2, out3, out4, out5, out6, out7;
        uint64_t tmp0, tmp1, tmp2;

        uint64_t in0 = out_ptr[0];
        uint64_t in1 = out_ptr[WIDTH];
        uint64_t in2 = out_ptr[WIDTH * 2];
        uint64_t in3 = out_ptr[WIDTH * 3];
        uint64_t in4 = out_ptr[WIDTH * 4];
        uint64_t in5 = out_ptr[WIDTH * 5];
        uint64_t in6 = out_ptr[WIDTH * 6];
        uint64_t in7 = out_ptr[WIDTH * 7];

        tmp0 = in0 ^ in3;
        tmp1 = in2 ^ in7;
        tmp2 = in0 ^ in5 ^ in6;
        out2 = tmp1 ^ in3;
        out3 = tmp2 ^ in2;
        out5 = out3 ^ in1 ^ in3;
        out0 = tmp0 ^ out3 ^ in4;
        out1 = tmp1 ^ out0;
        out4 = out1 ^ out5 ^ in5;
        out7 = tmp0 ^ out4;
        out6 = tmp2 ^ out7;

        out_ptr[0] = out0 ^ in_ptr[0];
        out_ptr[WIDTH] = out1 ^ in_ptr[WIDTH];
        out_ptr[WIDTH * 2] = out2 ^ in_ptr[WIDTH * 2];
        out_ptr[WIDTH * 3] = out3 ^ in_ptr[WIDTH * 3];
        out_ptr[WIDTH * 4] = out4 ^ in_ptr[WIDTH * 4];
        out_ptr[WIDTH * 5] = out5 ^ in_ptr[WIDTH * 5];
        out_ptr[WIDTH * 6] = out6 ^ in_ptr[WIDTH * 6];
        out_ptr[WIDTH * 7] = out7 ^ in_ptr[WIDTH * 7];

        in_ptr++;
        out_ptr++;
    }
}

static void gf8_muladd_79(void *out, void *in)
{
    unsigned int i;
    uint64_t *in_ptr = (uint64_t *)in;
    uint64_t *out_ptr = (uint64_t *)out;

    for (i = 0; i < WIDTH; i++) {
        uint64_t out0, out1, out2, out3, out4, out5, out6, out7;
        uint64_t tmp0, tmp1, tmp2, tmp3;

        uint64_t in0 = out_ptr[0];
        uint64_t in1 = out_ptr[WIDTH];
        uint64_t in2 = out_ptr[WIDTH * 2];
        uint64_t in3 = out_ptr[WIDTH * 3];
        uint64_t in4 = out_ptr[WIDTH * 4];
        uint64_t in5 = out_ptr[WIDTH * 5];
        uint64_t in6 = out_ptr[WIDTH * 6];
        uint64_t in7 = out_ptr[WIDTH * 7];

        out2 = in3 ^ in7;
        tmp0 = in3 ^ in4;
        tmp1 = in1 ^ in5;
        tmp2 = tmp1 ^ in2;
        out4 = tmp2 ^ in0 ^ in7;
        tmp3 = out4 ^ in5;
        out5 = tmp3 ^ out2 ^ in6;
        out7 = tmp0 ^ tmp2;
        out6 = tmp0 ^ tmp3;
        out3 = tmp1 ^ out5;
        out0 = out3 ^ in4;
        out1 = tmp3 ^ out0;

        out_ptr[0] = out0 ^ in_ptr[0];
        out_ptr[WIDTH] = out1 ^ in_ptr[WIDTH];
        out_ptr[WIDTH * 2] = out2 ^ in_ptr[WIDTH * 2];
        out_ptr[WIDTH * 3] = out3 ^ in_ptr[WIDTH * 3];
        out_ptr[WIDTH * 4] = out4 ^ in_ptr[WIDTH * 4];
        out_ptr[WIDTH * 5] = out5 ^ in_ptr[WIDTH * 5];
        out_ptr[WIDTH * 6] = out6 ^ in_ptr[WIDTH * 6];
        out_ptr[WIDTH * 7] = out7 ^ in_ptr[WIDTH * 7];

        in_ptr++;
        out_ptr++;
    }
}

static void gf8_muladd_7A(void *out, void *in)
{
    unsigned int i;
    uint64_t *in_ptr = (uint64_t *)in;
    uint64_t *out_ptr = (uint64_t *)out;

    for (i = 0; i < WIDTH; i++) {
        uint64_t out0, out1, out2, out3, out4, out5, out6, out7;
        uint64_t tmp0, tmp1;

        uint64_t in0 = out_ptr[0];
        uint64_t in1 = out_ptr[WIDTH];
        uint64_t in2 = out_ptr[WIDTH * 2];
        uint64_t in3 = out_ptr[WIDTH * 3];
        uint64_t in4 = out_ptr[WIDTH * 4];
        uint64_t in5 = out_ptr[WIDTH * 5];
        uint64_t in6 = out_ptr[WIDTH * 6];
        uint64_t in7 = out_ptr[WIDTH * 7];

        tmp0 = in1 ^ in2;
        out2 = tmp0 ^ in3;
        tmp1 = out2 ^ in4;
        out4 = tmp1 ^ in0 ^ in5;
        out5 = out4 ^ in6;
        out6 = out5 ^ in7;
        out7 = out6 ^ in0;
        out0 = out7 ^ in1;
        out1 = tmp0 ^ out6;
        out3 = tmp1 ^ out6;

        out_ptr[0] = out0 ^ in_ptr[0];
        out_ptr[WIDTH] = out1 ^ in_ptr[WIDTH];
        out_ptr[WIDTH * 2] = out2 ^ in_ptr[WIDTH * 2];
        out_ptr[WIDTH * 3] = out3 ^ in_ptr[WIDTH * 3];
        out_ptr[WIDTH * 4] = out4 ^ in_ptr[WIDTH * 4];
        out_ptr[WIDTH * 5] = out5 ^ in_ptr[WIDTH * 5];
        out_ptr[WIDTH * 6] = out6 ^ in_ptr[WIDTH * 6];
        out_ptr[WIDTH * 7] = out7 ^ in_ptr[WIDTH * 7];

        in_ptr++;
        out_ptr++;
    }
}

static void gf8_muladd_7B(void *out, void *in)
{
    unsigned int i;
    uint64_t *in_ptr = (uint64_t *)in;
    uint64_t *out_ptr = (uint64_t *)out;

    for (i = 0; i < WIDTH; i++) {
        uint64_t out0, out1, out2, out3, out4, out5, out6, out7;
        uint64_t tmp0, tmp1, tmp2;

        uint64_t in0 = out_ptr[0];
        uint64_t in1 = out_ptr[WIDTH];
        uint64_t in2 = out_ptr[WIDTH * 2];
        uint64_t in3 = out_ptr[WIDTH * 3];
        uint64_t in4 = out_ptr[WIDTH * 4];
        uint64_t in5 = out_ptr[WIDTH * 5];
        uint64_t in6 = out_ptr[WIDTH * 6];
        uint64_t in7 = out_ptr[WIDTH * 7];

        out2 = in1 ^ in3;
        tmp0 = in0 ^ in5;
        out4 = tmp0 ^ out2 ^ in2;
        tmp1 = out4 ^ in4;
        out6 = tmp1 ^ in7;
        out5 = tmp1 ^ in5 ^ in6;
        out0 = out6 ^ in1 ^ in6;
        tmp2 = out0 ^ in2;
        out1 = tmp2 ^ in1;
        out3 = tmp2 ^ in4;
        out7 = tmp0 ^ out5;

        out_ptr[0] = out0 ^ in_ptr[0];
        out_ptr[WIDTH] = out1 ^ in_ptr[WIDTH];
        out_ptr[WIDTH * 2] = out2 ^ in_ptr[WIDTH * 2];
        out_ptr[WIDTH * 3] = out3 ^ in_ptr[WIDTH * 3];
        out_ptr[WIDTH * 4] = out4 ^ in_ptr[WIDTH * 4];
        out_ptr[WIDTH * 5] = out5 ^ in_ptr[WIDTH * 5];
        out_ptr[WIDTH * 6] = out6 ^ in_ptr[WIDTH * 6];
        out_ptr[WIDTH * 7] = out7 ^ in_ptr[WIDTH * 7];

        in_ptr++;
        out_ptr++;
    }
}

static void gf8_muladd_7C(void *out, void *in)
{
    unsigned int i;
    uint64_t *in_ptr = (uint64_t *)in;
    uint64_t *out_ptr = (uint64_t *)out;

    for (i = 0; i < WIDTH; i++) {
        uint64_t out0, out1, out2, out3, out4, out5, out6, out7;
        uint64_t tmp0, tmp1;

        uint64_t in0 = out_ptr[0];
        uint64_t in1 = out_ptr[WIDTH];
        uint64_t in2 = out_ptr[WIDTH * 2];
        uint64_t in3 = out_ptr[WIDTH * 3];
        uint64_t in4 = out_ptr[WIDTH * 4];
        uint64_t in5 = out_ptr[WIDTH * 5];
        uint64_t in6 = out_ptr[WIDTH * 6];
        uint64_t in7 = out_ptr[WIDTH * 7];

        tmp0 = in3 ^ in5;
        tmp1 = tmp0 ^ in4;
        out0 = tmp1 ^ in2;
        out1 = tmp1 ^ in6;
        out7 = out0 ^ in1 ^ in5 ^ in7;
        out5 = out1 ^ out7 ^ in0;
        out3 = out5 ^ in6;
        out6 = tmp0 ^ out5;
        out2 = out6 ^ in1;
        out4 = out2 ^ out7 ^ in5;

        out_ptr[0] = out0 ^ in_ptr[0];
        out_ptr[WIDTH] = out1 ^ in_ptr[WIDTH];
        out_ptr[WIDTH * 2] = out2 ^ in_ptr[WIDTH * 2];
        out_ptr[WIDTH * 3] = out3 ^ in_ptr[WIDTH * 3];
        out_ptr[WIDTH * 4] = out4 ^ in_ptr[WIDTH * 4];
        out_ptr[WIDTH * 5] = out5 ^ in_ptr[WIDTH * 5];
        out_ptr[WIDTH * 6] = out6 ^ in_ptr[WIDTH * 6];
        out_ptr[WIDTH * 7] = out7 ^ in_ptr[WIDTH * 7];

        in_ptr++;
        out_ptr++;
    }
}

static void gf8_muladd_7D(void *out, void *in)
{
    unsigned int i;
    uint64_t *in_ptr = (uint64_t *)in;
    uint64_t *out_ptr = (uint64_t *)out;

    for (i = 0; i < WIDTH; i++) {
        uint64_t out0, out1, out2, out3, out4, out5, out6, out7;
        uint64_t tmp0, tmp1, tmp2, tmp3;

        uint64_t in0 = out_ptr[0];
        uint64_t in1 = out_ptr[WIDTH];
        uint64_t in2 = out_ptr[WIDTH * 2];
        uint64_t in3 = out_ptr[WIDTH * 3];
        uint64_t in4 = out_ptr[WIDTH * 4];
        uint64_t in5 = out_ptr[WIDTH * 5];
        uint64_t in6 = out_ptr[WIDTH * 6];
        uint64_t in7 = out_ptr[WIDTH * 7];

        tmp0 = in1 ^ in2;
        tmp1 = tmp0 ^ in3;
        tmp2 = tmp0 ^ in6;
        out7 = tmp1 ^ in4;
        tmp3 = tmp2 ^ in0;
        out5 = tmp3 ^ in7;
        out4 = tmp3 ^ in2 ^ in5;
        out2 = tmp1 ^ out5;
        out6 = tmp2 ^ out2;
        out0 = out4 ^ out7 ^ in6;
        out1 = tmp3 ^ out0;
        out3 = out6 ^ in5;

        out_ptr[0] = out0 ^ in_ptr[0];
        out_ptr[WIDTH] = out1 ^ in_ptr[WIDTH];
        out_ptr[WIDTH * 2] = out2 ^ in_ptr[WIDTH * 2];
        out_ptr[WIDTH * 3] = out3 ^ in_ptr[WIDTH * 3];
        out_ptr[WIDTH * 4] = out4 ^ in_ptr[WIDTH * 4];
        out_ptr[WIDTH * 5] = out5 ^ in_ptr[WIDTH * 5];
        out_ptr[WIDTH * 6] = out6 ^ in_ptr[WIDTH * 6];
        out_ptr[WIDTH * 7] = out7 ^ in_ptr[WIDTH * 7];

        in_ptr++;
        out_ptr++;
    }
}

static void gf8_muladd_7E(void *out, void *in)
{
    unsigned int i;
    uint64_t *in_ptr = (uint64_t *)in;
    uint64_t *out_ptr = (uint64_t *)out;

    for (i = 0; i < WIDTH; i++) {
        uint64_t out0, out1, out2, out3, out4, out5, out6, out7;
        uint64_t tmp0, tmp1, tmp2;

        uint64_t in0 = out_ptr[0];
        uint64_t in1 = out_ptr[WIDTH];
        uint64_t in2 = out_ptr[WIDTH * 2];
        uint64_t in3 = out_ptr[WIDTH * 3];
        uint64_t in4 = out_ptr[WIDTH * 4];
        uint64_t in5 = out_ptr[WIDTH * 5];
        uint64_t in6 = out_ptr[WIDTH * 6];
        uint64_t in7 = out_ptr[WIDTH * 7];

        tmp0 = in3 ^ in4;
        tmp1 = in0 ^ in5;
        out1 = tmp0 ^ tmp1 ^ in6;
        out3 = tmp1 ^ in1;
        out4 = out1 ^ in1 ^ in7;
        tmp2 = out4 ^ in3;
        out5 = tmp2 ^ in2;
        out6 = tmp0 ^ out5;
        out7 = tmp1 ^ out4 ^ in2;
        out2 = out6 ^ in5 ^ in7;
        out0 = tmp2 ^ out2;

        out_ptr[0] = out0 ^ in_ptr[0];
        out_ptr[WIDTH] = out1 ^ in_ptr[WIDTH];
        out_ptr[WIDTH * 2] = out2 ^ in_ptr[WIDTH * 2];
        out_ptr[WIDTH * 3] = out3 ^ in_ptr[WIDTH * 3];
        out_ptr[WIDTH * 4] = out4 ^ in_ptr[WIDTH * 4];
        out_ptr[WIDTH * 5] = out5 ^ in_ptr[WIDTH * 5];
        out_ptr[WIDTH * 6] = out6 ^ in_ptr[WIDTH * 6];
        out_ptr[WIDTH * 7] = out7 ^ in_ptr[WIDTH * 7];

        in_ptr++;
        out_ptr++;
    }
}

static void gf8_muladd_7F(void *out, void *in)
{
    unsigned int i;
    uint64_t *in_ptr = (uint64_t *)in;
    uint64_t *out_ptr = (uint64_t *)out;

    for (i = 0; i < WIDTH; i++) {
        uint64_t out0, out1, out2, out3, out4, out5, out6, out7;
        uint64_t tmp0, tmp1, tmp2, tmp3;

        uint64_t in0 = out_ptr[0];
        uint64_t in1 = out_ptr[WIDTH];
        uint64_t in2 = out_ptr[WIDTH * 2];
        uint64_t in3 = out_ptr[WIDTH * 3];
        uint64_t in4 = out_ptr[WIDTH * 4];
        uint64_t in5 = out_ptr[WIDTH * 5];
        uint64_t in6 = out_ptr[WIDTH * 6];
        uint64_t in7 = out_ptr[WIDTH * 7];

        tmp0 = in2 ^ in7;
        tmp1 = tmp0 ^ in3 ^ in5;
        tmp2 = tmp1 ^ in0;
        out0 = tmp2 ^ in4;
        out6 = tmp2 ^ in1;
        out3 = tmp0 ^ out6;
        tmp3 = out3 ^ in6;
        out1 = tmp3 ^ in4;
        out2 = tmp3 ^ in5;
        out4 = tmp3 ^ in7;
        out5 = tmp1 ^ out1;
        out7 = out0 ^ out4 ^ in3;

        out_ptr[0] = out0 ^ in_ptr[0];
        out_ptr[WIDTH] = out1 ^ in_ptr[WIDTH];
        out_ptr[WIDTH * 2] = out2 ^ in_ptr[WIDTH * 2];
        out_ptr[WIDTH * 3] = out3 ^ in_ptr[WIDTH * 3];
        out_ptr[WIDTH * 4] = out4 ^ in_ptr[WIDTH * 4];
        out_ptr[WIDTH * 5] = out5 ^ in_ptr[WIDTH * 5];
        out_ptr[WIDTH * 6] = out6 ^ in_ptr[WIDTH * 6];
        out_ptr[WIDTH * 7] = out7 ^ in_ptr[WIDTH * 7];

        in_ptr++;
        out_ptr++;
    }
}

static void gf8_muladd_80(void *out, void *in)
{
    unsigned int i;
    uint64_t *in_ptr = (uint64_t *)in;
    uint64_t *out_ptr = (uint64_t *)out;

    for (i = 0; i < WIDTH; i++) {
        uint64_t out0, out1, out2, out3, out4, out5, out6, out7;
        uint64_t tmp0, tmp1, tmp2;

        uint64_t in0 = out_ptr[0];
        uint64_t in1 = out_ptr[WIDTH];
        uint64_t in2 = out_ptr[WIDTH * 2];
        uint64_t in3 = out_ptr[WIDTH * 3];
        uint64_t in4 = out_ptr[WIDTH * 4];
        uint64_t in5 = out_ptr[WIDTH * 5];
        uint64_t in6 = out_ptr[WIDTH * 6];
        uint64_t in7 = out_ptr[WIDTH * 7];

        tmp0 = in2 ^ in3;
        tmp1 = in4 ^ in5;
        out1 = in2 ^ in6 ^ in7;
        out5 = tmp0 ^ in4;
        tmp2 = tmp0 ^ in1;
        out6 = tmp1 ^ in3;
        out7 = tmp1 ^ in0 ^ in6;
        out4 = tmp2 ^ in7;
        out3 = tmp2 ^ out6;
        out2 = out3 ^ out5 ^ in6;
        out0 = out2 ^ in3 ^ in7;

        out_ptr[0] = out0 ^ in_ptr[0];
        out_ptr[WIDTH] = out1 ^ in_ptr[WIDTH];
        out_ptr[WIDTH * 2] = out2 ^ in_ptr[WIDTH * 2];
        out_ptr[WIDTH * 3] = out3 ^ in_ptr[WIDTH * 3];
        out_ptr[WIDTH * 4] = out4 ^ in_ptr[WIDTH * 4];
        out_ptr[WIDTH * 5] = out5 ^ in_ptr[WIDTH * 5];
        out_ptr[WIDTH * 6] = out6 ^ in_ptr[WIDTH * 6];
        out_ptr[WIDTH * 7] = out7 ^ in_ptr[WIDTH * 7];

        in_ptr++;
        out_ptr++;
    }
}

static void gf8_muladd_81(void *out, void *in)
{
    unsigned int i;
    uint64_t *in_ptr = (uint64_t *)in;
    uint64_t *out_ptr = (uint64_t *)out;

    for (i = 0; i < WIDTH; i++) {
        uint64_t out0, out1, out2, out3, out4, out5, out6, out7;
        uint64_t tmp0, tmp1;

        uint64_t in0 = out_ptr[0];
        uint64_t in1 = out_ptr[WIDTH];
        uint64_t in2 = out_ptr[WIDTH * 2];
        uint64_t in3 = out_ptr[WIDTH * 3];
        uint64_t in4 = out_ptr[WIDTH * 4];
        uint64_t in5 = out_ptr[WIDTH * 5];
        uint64_t in6 = out_ptr[WIDTH * 6];
        uint64_t in7 = out_ptr[WIDTH * 7];

        tmp0 = in4 ^ in6;
        tmp1 = tmp0 ^ in3;
        out6 = tmp1 ^ in5;
        out5 = out6 ^ in2 ^ in6;
        out3 = out5 ^ in1;
        out2 = tmp0 ^ out3;
        out1 = out3 ^ out6 ^ in7;
        out4 = tmp1 ^ out1;
        out7 = out2 ^ out4 ^ in0;
        out0 = out7 ^ in1 ^ in4;

        out_ptr[0] = out0 ^ in_ptr[0];
        out_ptr[WIDTH] = out1 ^ in_ptr[WIDTH];
        out_ptr[WIDTH * 2] = out2 ^ in_ptr[WIDTH * 2];
        out_ptr[WIDTH * 3] = out3 ^ in_ptr[WIDTH * 3];
        out_ptr[WIDTH * 4] = out4 ^ in_ptr[WIDTH * 4];
        out_ptr[WIDTH * 5] = out5 ^ in_ptr[WIDTH * 5];
        out_ptr[WIDTH * 6] = out6 ^ in_ptr[WIDTH * 6];
        out_ptr[WIDTH * 7] = out7 ^ in_ptr[WIDTH * 7];

        in_ptr++;
        out_ptr++;
    }
}

static void gf8_muladd_82(void *out, void *in)
{
    unsigned int i;
    uint64_t *in_ptr = (uint64_t *)in;
    uint64_t *out_ptr = (uint64_t *)out;

    for (i = 0; i < WIDTH; i++) {
        uint64_t out0, out1, out2, out3, out4, out5, out6, out7;
        uint64_t tmp0;

        uint64_t in0 = out_ptr[0];
        uint64_t in1 = out_ptr[WIDTH];
        uint64_t in2 = out_ptr[WIDTH * 2];
        uint64_t in3 = out_ptr[WIDTH * 3];
        uint64_t in4 = out_ptr[WIDTH * 4];
        uint64_t in5 = out_ptr[WIDTH * 5];
        uint64_t in6 = out_ptr[WIDTH * 6];
        uint64_t in7 = out_ptr[WIDTH * 7];

        out4 = in1 ^ in2;
        tmp0 = in6 ^ in7;
        out5 = in2 ^ in3;
        out6 = in3 ^ in4;
        out7 = in0 ^ in4 ^ in5;
        out0 = in1 ^ in5 ^ in6;
        out1 = tmp0 ^ in0 ^ in2;
        out2 = tmp0 ^ in3 ^ in5;
        out3 = tmp0 ^ out0 ^ in4;

        out_ptr[0] = out0 ^ in_ptr[0];
        out_ptr[WIDTH] = out1 ^ in_ptr[WIDTH];
        out_ptr[WIDTH * 2] = out2 ^ in_ptr[WIDTH * 2];
        out_ptr[WIDTH * 3] = out3 ^ in_ptr[WIDTH * 3];
        out_ptr[WIDTH * 4] = out4 ^ in_ptr[WIDTH * 4];
        out_ptr[WIDTH * 5] = out5 ^ in_ptr[WIDTH * 5];
        out_ptr[WIDTH * 6] = out6 ^ in_ptr[WIDTH * 6];
        out_ptr[WIDTH * 7] = out7 ^ in_ptr[WIDTH * 7];

        in_ptr++;
        out_ptr++;
    }
}

static void gf8_muladd_83(void *out, void *in)
{
    unsigned int i;
    uint64_t *in_ptr = (uint64_t *)in;
    uint64_t *out_ptr = (uint64_t *)out;

    for (i = 0; i < WIDTH; i++) {
        uint64_t out0, out1, out2, out3, out4, out5, out6, out7;
        uint64_t tmp0, tmp1, tmp2, tmp3, tmp4;

        uint64_t in0 = out_ptr[0];
        uint64_t in1 = out_ptr[WIDTH];
        uint64_t in2 = out_ptr[WIDTH * 2];
        uint64_t in3 = out_ptr[WIDTH * 3];
        uint64_t in4 = out_ptr[WIDTH * 4];
        uint64_t in5 = out_ptr[WIDTH * 5];
        uint64_t in6 = out_ptr[WIDTH * 6];
        uint64_t in7 = out_ptr[WIDTH * 7];

        tmp0 = in0 ^ in1;
        tmp1 = in2 ^ in5;
        tmp2 = in3 ^ in6;
        out4 = in1 ^ in2 ^ in4;
        out0 = tmp0 ^ in5 ^ in6;
        out5 = tmp1 ^ in3;
        tmp3 = tmp1 ^ in7;
        out6 = tmp2 ^ in4;
        out2 = tmp2 ^ tmp3;
        tmp4 = tmp3 ^ out4;
        out1 = tmp3 ^ out0;
        out3 = tmp4 ^ in3;
        out7 = tmp0 ^ tmp4;

        out_ptr[0] = out0 ^ in_ptr[0];
        out_ptr[WIDTH] = out1 ^ in_ptr[WIDTH];
        out_ptr[WIDTH * 2] = out2 ^ in_ptr[WIDTH * 2];
        out_ptr[WIDTH * 3] = out3 ^ in_ptr[WIDTH * 3];
        out_ptr[WIDTH * 4] = out4 ^ in_ptr[WIDTH * 4];
        out_ptr[WIDTH * 5] = out5 ^ in_ptr[WIDTH * 5];
        out_ptr[WIDTH * 6] = out6 ^ in_ptr[WIDTH * 6];
        out_ptr[WIDTH * 7] = out7 ^ in_ptr[WIDTH * 7];

        in_ptr++;
        out_ptr++;
    }
}

static void gf8_muladd_84(void *out, void *in)
{
    unsigned int i;
    uint64_t *in_ptr = (uint64_t *)in;
    uint64_t *out_ptr = (uint64_t *)out;

    for (i = 0; i < WIDTH; i++) {
        uint64_t out0, out1, out2, out3, out4, out5, out6, out7;

        uint64_t in0 = out_ptr[0];
        uint64_t in1 = out_ptr[WIDTH];
        uint64_t in2 = out_ptr[WIDTH * 2];
        uint64_t in3 = out_ptr[WIDTH * 3];
        uint64_t in4 = out_ptr[WIDTH * 4];
        uint64_t in5 = out_ptr[WIDTH * 5];
        uint64_t in6 = out_ptr[WIDTH * 6];
        uint64_t in7 = out_ptr[WIDTH * 7];

        out1 = in2 ^ in6;
        out6 = in3 ^ in5;
        out0 = in1 ^ in5 ^ in7;
        out7 = in0 ^ in4 ^ in6;
        out4 = in1 ^ in3 ^ in6;
        out5 = in2 ^ in4 ^ in7;
        out2 = out6 ^ in0 ^ in1;
        out3 = out5 ^ in5 ^ in6;

        out_ptr[0] = out0 ^ in_ptr[0];
        out_ptr[WIDTH] = out1 ^ in_ptr[WIDTH];
        out_ptr[WIDTH * 2] = out2 ^ in_ptr[WIDTH * 2];
        out_ptr[WIDTH * 3] = out3 ^ in_ptr[WIDTH * 3];
        out_ptr[WIDTH * 4] = out4 ^ in_ptr[WIDTH * 4];
        out_ptr[WIDTH * 5] = out5 ^ in_ptr[WIDTH * 5];
        out_ptr[WIDTH * 6] = out6 ^ in_ptr[WIDTH * 6];
        out_ptr[WIDTH * 7] = out7 ^ in_ptr[WIDTH * 7];

        in_ptr++;
        out_ptr++;
    }
}

static void gf8_muladd_85(void *out, void *in)
{
    unsigned int i;
    uint64_t *in_ptr = (uint64_t *)in;
    uint64_t *out_ptr = (uint64_t *)out;

    for (i = 0; i < WIDTH; i++) {
        uint64_t out0, out1, out2, out3, out4, out5, out6, out7;
        uint64_t tmp0, tmp1, tmp2, tmp3;

        uint64_t in0 = out_ptr[0];
        uint64_t in1 = out_ptr[WIDTH];
        uint64_t in2 = out_ptr[WIDTH * 2];
        uint64_t in3 = out_ptr[WIDTH * 3];
        uint64_t in4 = out_ptr[WIDTH * 4];
        uint64_t in5 = out_ptr[WIDTH * 5];
        uint64_t in6 = out_ptr[WIDTH * 6];
        uint64_t in7 = out_ptr[WIDTH * 7];

        tmp0 = in1 ^ in6;
        tmp1 = in3 ^ in6;
        tmp2 = tmp0 ^ in4;
        out1 = tmp0 ^ in2;
        out6 = tmp1 ^ in5;
        out4 = tmp2 ^ in3;
        tmp3 = out1 ^ out6;
        out2 = tmp3 ^ in0;
        out3 = tmp2 ^ tmp3 ^ in7;
        out7 = out2 ^ out3 ^ in1;
        out5 = tmp1 ^ out3;
        out0 = tmp2 ^ out7 ^ in5;

        out_ptr[0] = out0 ^ in_ptr[0];
        out_ptr[WIDTH] = out1 ^ in_ptr[WIDTH];
        out_ptr[WIDTH * 2] = out2 ^ in_ptr[WIDTH * 2];
        out_ptr[WIDTH * 3] = out3 ^ in_ptr[WIDTH * 3];
        out_ptr[WIDTH * 4] = out4 ^ in_ptr[WIDTH * 4];
        out_ptr[WIDTH * 5] = out5 ^ in_ptr[WIDTH * 5];
        out_ptr[WIDTH * 6] = out6 ^ in_ptr[WIDTH * 6];
        out_ptr[WIDTH * 7] = out7 ^ in_ptr[WIDTH * 7];

        in_ptr++;
        out_ptr++;
    }
}

static void gf8_muladd_86(void *out, void *in)
{
    unsigned int i;
    uint64_t *in_ptr = (uint64_t *)in;
    uint64_t *out_ptr = (uint64_t *)out;

    for (i = 0; i < WIDTH; i++) {
        uint64_t out0, out1, out2, out3, out4, out5, out6, out7;

        uint64_t in0 = out_ptr[0];
        uint64_t in1 = out_ptr[WIDTH];
        uint64_t in2 = out_ptr[WIDTH * 2];
        uint64_t in3 = out_ptr[WIDTH * 3];
        uint64_t in4 = out_ptr[WIDTH * 4];
        uint64_t in5 = out_ptr[WIDTH * 5];
        uint64_t in6 = out_ptr[WIDTH * 6];
        uint64_t in7 = out_ptr[WIDTH * 7];

        out6 = in3;
        out7 = in0 ^ in4;
        out0 = in1 ^ in5;
        out5 = in2 ^ in7;
        out3 = in4 ^ in5 ^ in6;
        out1 = in0 ^ in2 ^ in6;
        out4 = in1 ^ in6 ^ in7;
        out2 = in0 ^ in3 ^ in5 ^ in7;

        out_ptr[0] = out0 ^ in_ptr[0];
        out_ptr[WIDTH] = out1 ^ in_ptr[WIDTH];
        out_ptr[WIDTH * 2] = out2 ^ in_ptr[WIDTH * 2];
        out_ptr[WIDTH * 3] = out3 ^ in_ptr[WIDTH * 3];
        out_ptr[WIDTH * 4] = out4 ^ in_ptr[WIDTH * 4];
        out_ptr[WIDTH * 5] = out5 ^ in_ptr[WIDTH * 5];
        out_ptr[WIDTH * 6] = out6 ^ in_ptr[WIDTH * 6];
        out_ptr[WIDTH * 7] = out7 ^ in_ptr[WIDTH * 7];

        in_ptr++;
        out_ptr++;
    }
}

static void gf8_muladd_87(void *out, void *in)
{
    unsigned int i;
    uint64_t *in_ptr = (uint64_t *)in;
    uint64_t *out_ptr = (uint64_t *)out;

    for (i = 0; i < WIDTH; i++) {
        uint64_t out0, out1, out2, out3, out4, out5, out6, out7;
        uint64_t tmp0, tmp1;

        uint64_t in0 = out_ptr[0];
        uint64_t in1 = out_ptr[WIDTH];
        uint64_t in2 = out_ptr[WIDTH * 2];
        uint64_t in3 = out_ptr[WIDTH * 3];
        uint64_t in4 = out_ptr[WIDTH * 4];
        uint64_t in5 = out_ptr[WIDTH * 5];
        uint64_t in6 = out_ptr[WIDTH * 6];
        uint64_t in7 = out_ptr[WIDTH * 7];

        out6 = in3 ^ in6;
        tmp0 = in0 ^ in1;
        out7 = in0 ^ in4 ^ in7;
        out5 = in2 ^ in5 ^ in7;
        out3 = out6 ^ in4 ^ in5;
        out0 = tmp0 ^ in5;
        tmp1 = tmp0 ^ in6;
        out2 = out5 ^ in0 ^ in3;
        out1 = tmp1 ^ in2;
        out4 = tmp1 ^ out7;

        out_ptr[0] = out0 ^ in_ptr[0];
        out_ptr[WIDTH] = out1 ^ in_ptr[WIDTH];
        out_ptr[WIDTH * 2] = out2 ^ in_ptr[WIDTH * 2];
        out_ptr[WIDTH * 3] = out3 ^ in_ptr[WIDTH * 3];
        out_ptr[WIDTH * 4] = out4 ^ in_ptr[WIDTH * 4];
        out_ptr[WIDTH * 5] = out5 ^ in_ptr[WIDTH * 5];
        out_ptr[WIDTH * 6] = out6 ^ in_ptr[WIDTH * 6];
        out_ptr[WIDTH * 7] = out7 ^ in_ptr[WIDTH * 7];

        in_ptr++;
        out_ptr++;
    }
}

static void gf8_muladd_88(void *out, void *in)
{
    unsigned int i;
    uint64_t *in_ptr = (uint64_t *)in;
    uint64_t *out_ptr = (uint64_t *)out;

    for (i = 0; i < WIDTH; i++) {
        uint64_t out0, out1, out2, out3, out4, out5, out6, out7;
        uint64_t tmp0, tmp1;

        uint64_t in0 = out_ptr[0];
        uint64_t in1 = out_ptr[WIDTH];
        uint64_t in2 = out_ptr[WIDTH * 2];
        uint64_t in3 = out_ptr[WIDTH * 3];
        uint64_t in4 = out_ptr[WIDTH * 4];
        uint64_t in5 = out_ptr[WIDTH * 5];
        uint64_t in6 = out_ptr[WIDTH * 6];
        uint64_t in7 = out_ptr[WIDTH * 7];

        out1 = in2 ^ in7;
        tmp0 = in5 ^ in6;
        out0 = in1 ^ in6 ^ in7;
        out6 = in4 ^ in5 ^ in7;
        out3 = out0 ^ out1 ^ in0 ^ in4;
        out7 = tmp0 ^ in0;
        tmp1 = tmp0 ^ in3;
        out2 = out0 ^ in3;
        out4 = tmp1 ^ in2;
        out5 = tmp1 ^ out6;

        out_ptr[0] = out0 ^ in_ptr[0];
        out_ptr[WIDTH] = out1 ^ in_ptr[WIDTH];
        out_ptr[WIDTH * 2] = out2 ^ in_ptr[WIDTH * 2];
        out_ptr[WIDTH * 3] = out3 ^ in_ptr[WIDTH * 3];
        out_ptr[WIDTH * 4] = out4 ^ in_ptr[WIDTH * 4];
        out_ptr[WIDTH * 5] = out5 ^ in_ptr[WIDTH * 5];
        out_ptr[WIDTH * 6] = out6 ^ in_ptr[WIDTH * 6];
        out_ptr[WIDTH * 7] = out7 ^ in_ptr[WIDTH * 7];

        in_ptr++;
        out_ptr++;
    }
}

static void gf8_muladd_89(void *out, void *in)
{
    unsigned int i;
    uint64_t *in_ptr = (uint64_t *)in;
    uint64_t *out_ptr = (uint64_t *)out;

    for (i = 0; i < WIDTH; i++) {
        uint64_t out0, out1, out2, out3, out4, out5, out6, out7;
        uint64_t tmp0, tmp1, tmp2;

        uint64_t in0 = out_ptr[0];
        uint64_t in1 = out_ptr[WIDTH];
        uint64_t in2 = out_ptr[WIDTH * 2];
        uint64_t in3 = out_ptr[WIDTH * 3];
        uint64_t in4 = out_ptr[WIDTH * 4];
        uint64_t in5 = out_ptr[WIDTH * 5];
        uint64_t in6 = out_ptr[WIDTH * 6];
        uint64_t in7 = out_ptr[WIDTH * 7];

        tmp0 = in0 ^ in7;
        tmp1 = in2 ^ in7;
        tmp2 = tmp0 ^ in6;
        out1 = tmp1 ^ in1;
        out7 = tmp2 ^ in5;
        out0 = tmp2 ^ in1;
        out2 = out1 ^ in3 ^ in6;
        out6 = out7 ^ in0 ^ in4;
        out5 = out6 ^ in3;
        out3 = tmp0 ^ out2 ^ in4;
        out4 = tmp1 ^ out5;

        out_ptr[0] = out0 ^ in_ptr[0];
        out_ptr[WIDTH] = out1 ^ in_ptr[WIDTH];
        out_ptr[WIDTH * 2] = out2 ^ in_ptr[WIDTH * 2];
        out_ptr[WIDTH * 3] = out3 ^ in_ptr[WIDTH * 3];
        out_ptr[WIDTH * 4] = out4 ^ in_ptr[WIDTH * 4];
        out_ptr[WIDTH * 5] = out5 ^ in_ptr[WIDTH * 5];
        out_ptr[WIDTH * 6] = out6 ^ in_ptr[WIDTH * 6];
        out_ptr[WIDTH * 7] = out7 ^ in_ptr[WIDTH * 7];

        in_ptr++;
        out_ptr++;
    }
}

static void gf8_muladd_8A(void *out, void *in)
{
    unsigned int i;
    uint64_t *in_ptr = (uint64_t *)in;
    uint64_t *out_ptr = (uint64_t *)out;

    for (i = 0; i < WIDTH; i++) {
        uint64_t out0, out1, out2, out3, out4, out5, out6, out7;

        uint64_t in0 = out_ptr[0];
        uint64_t in1 = out_ptr[WIDTH];
        uint64_t in2 = out_ptr[WIDTH * 2];
        uint64_t in3 = out_ptr[WIDTH * 3];
        uint64_t in4 = out_ptr[WIDTH * 4];
        uint64_t in5 = out_ptr[WIDTH * 5];
        uint64_t in6 = out_ptr[WIDTH * 6];
        uint64_t in7 = out_ptr[WIDTH * 7];

        out0 = in1 ^ in6;
        out7 = in0 ^ in5;
        out2 = in3 ^ in6;
        out6 = in4 ^ in7;
        out1 = in0 ^ in2 ^ in7;
        out3 = out0 ^ out6 ^ in0;
        out4 = out1 ^ out7 ^ in6;
        out5 = out2 ^ in7;

        out_ptr[0] = out0 ^ in_ptr[0];
        out_ptr[WIDTH] = out1 ^ in_ptr[WIDTH];
        out_ptr[WIDTH * 2] = out2 ^ in_ptr[WIDTH * 2];
        out_ptr[WIDTH * 3] = out3 ^ in_ptr[WIDTH * 3];
        out_ptr[WIDTH * 4] = out4 ^ in_ptr[WIDTH * 4];
        out_ptr[WIDTH * 5] = out5 ^ in_ptr[WIDTH * 5];
        out_ptr[WIDTH * 6] = out6 ^ in_ptr[WIDTH * 6];
        out_ptr[WIDTH * 7] = out7 ^ in_ptr[WIDTH * 7];

        in_ptr++;
        out_ptr++;
    }
}

static void gf8_muladd_8B(void *out, void *in)
{
    unsigned int i;
    uint64_t *in_ptr = (uint64_t *)in;
    uint64_t *out_ptr = (uint64_t *)out;

    for (i = 0; i < WIDTH; i++) {
        uint64_t out0, out1, out2, out3, out4, out5, out6, out7;
        uint64_t tmp0, tmp1, tmp2, tmp3, tmp4;

        uint64_t in0 = out_ptr[0];
        uint64_t in1 = out_ptr[WIDTH];
        uint64_t in2 = out_ptr[WIDTH * 2];
        uint64_t in3 = out_ptr[WIDTH * 3];
        uint64_t in4 = out_ptr[WIDTH * 4];
        uint64_t in5 = out_ptr[WIDTH * 5];
        uint64_t in6 = out_ptr[WIDTH * 6];
        uint64_t in7 = out_ptr[WIDTH * 7];

        tmp0 = in0 ^ in1;
        tmp1 = in3 ^ in6;
        tmp2 = in5 ^ in7;
        tmp3 = tmp0 ^ in7;
        out0 = tmp0 ^ in6;
        out2 = tmp1 ^ in2;
        out5 = tmp1 ^ tmp2;
        out7 = tmp2 ^ in0;
        tmp4 = tmp3 ^ in4;
        out1 = tmp3 ^ in2;
        out6 = tmp4 ^ out0;
        out4 = out6 ^ in2 ^ in5;
        out3 = tmp1 ^ tmp4;

        out_ptr[0] = out0 ^ in_ptr[0];
        out_ptr[WIDTH] = out1 ^ in_ptr[WIDTH];
        out_ptr[WIDTH * 2] = out2 ^ in_ptr[WIDTH * 2];
        out_ptr[WIDTH * 3] = out3 ^ in_ptr[WIDTH * 3];
        out_ptr[WIDTH * 4] = out4 ^ in_ptr[WIDTH * 4];
        out_ptr[WIDTH * 5] = out5 ^ in_ptr[WIDTH * 5];
        out_ptr[WIDTH * 6] = out6 ^ in_ptr[WIDTH * 6];
        out_ptr[WIDTH * 7] = out7 ^ in_ptr[WIDTH * 7];

        in_ptr++;
        out_ptr++;
    }
}

static void gf8_muladd_8C(void *out, void *in)
{
    unsigned int i;
    uint64_t *in_ptr = (uint64_t *)in;
    uint64_t *out_ptr = (uint64_t *)out;

    for (i = 0; i < WIDTH; i++) {
        uint64_t out0, out1, out2, out3, out4, out5, out6, out7;

        uint64_t in0 = out_ptr[0];
        uint64_t in1 = out_ptr[WIDTH];
        uint64_t in2 = out_ptr[WIDTH * 2];
        uint64_t in3 = out_ptr[WIDTH * 3];
        uint64_t in4 = out_ptr[WIDTH * 4];
        uint64_t in5 = out_ptr[WIDTH * 5];
        uint64_t in6 = out_ptr[WIDTH * 6];
        uint64_t in7 = out_ptr[WIDTH * 7];

        out1 = in2;
        out0 = in1 ^ in7;
        out7 = in0 ^ in6;
        out5 = in4 ^ in6;
        out6 = in5 ^ in7;
        out2 = out0 ^ in0 ^ in3;
        out3 = out5 ^ out7 ^ in2 ^ in7;
        out4 = out6 ^ in3;

        out_ptr[0] = out0 ^ in_ptr[0];
        out_ptr[WIDTH] = out1 ^ in_ptr[WIDTH];
        out_ptr[WIDTH * 2] = out2 ^ in_ptr[WIDTH * 2];
        out_ptr[WIDTH * 3] = out3 ^ in_ptr[WIDTH * 3];
        out_ptr[WIDTH * 4] = out4 ^ in_ptr[WIDTH * 4];
        out_ptr[WIDTH * 5] = out5 ^ in_ptr[WIDTH * 5];
        out_ptr[WIDTH * 6] = out6 ^ in_ptr[WIDTH * 6];
        out_ptr[WIDTH * 7] = out7 ^ in_ptr[WIDTH * 7];

        in_ptr++;
        out_ptr++;
    }
}

static void gf8_muladd_8D(void *out, void *in)
{
    unsigned int i;
    uint64_t *in_ptr = (uint64_t *)in;
    uint64_t *out_ptr = (uint64_t *)out;

    for (i = 0; i < WIDTH; i++) {
        uint64_t out0, out1, out2, out3, out4, out5, out6, out7;
        uint64_t tmp0;

        uint64_t in0 = out_ptr[0];
        uint64_t in1 = out_ptr[WIDTH];
        uint64_t in2 = out_ptr[WIDTH * 2];
        uint64_t in3 = out_ptr[WIDTH * 3];
        uint64_t in4 = out_ptr[WIDTH * 4];
        uint64_t in5 = out_ptr[WIDTH * 5];
        uint64_t in6 = out_ptr[WIDTH * 6];
        uint64_t in7 = out_ptr[WIDTH * 7];

        out1 = in1 ^ in2;
        tmp0 = in6 ^ in7;
        out0 = in0 ^ in1 ^ in7;
        out5 = in4 ^ in5 ^ in6;
        out6 = tmp0 ^ in5;
        out7 = tmp0 ^ in0;
        out4 = tmp0 ^ out5 ^ in3;
        out2 = out0 ^ in2 ^ in3;
        out3 = out2 ^ in1 ^ in4;

        out_ptr[0] = out0 ^ in_ptr[0];
        out_ptr[WIDTH] = out1 ^ in_ptr[WIDTH];
        out_ptr[WIDTH * 2] = out2 ^ in_ptr[WIDTH * 2];
        out_ptr[WIDTH * 3] = out3 ^ in_ptr[WIDTH * 3];
        out_ptr[WIDTH * 4] = out4 ^ in_ptr[WIDTH * 4];
        out_ptr[WIDTH * 5] = out5 ^ in_ptr[WIDTH * 5];
        out_ptr[WIDTH * 6] = out6 ^ in_ptr[WIDTH * 6];
        out_ptr[WIDTH * 7] = out7 ^ in_ptr[WIDTH * 7];

        in_ptr++;
        out_ptr++;
    }
}

static void gf8_muladd_8E(void *out, void *in)
{
    unsigned int i;
    uint64_t *in_ptr = (uint64_t *)in;
    uint64_t *out_ptr = (uint64_t *)out;

    for (i = 0; i < WIDTH; i++) {
        uint64_t out0, out1, out2, out3, out4, out5, out6, out7;

        uint64_t in0 = out_ptr[0];
        uint64_t in1 = out_ptr[WIDTH];
        uint64_t in2 = out_ptr[WIDTH * 2];
        uint64_t in3 = out_ptr[WIDTH * 3];
        uint64_t in4 = out_ptr[WIDTH * 4];
        uint64_t in5 = out_ptr[WIDTH * 5];
        uint64_t in6 = out_ptr[WIDTH * 6];
        uint64_t in7 = out_ptr[WIDTH * 7];

        out0 = in1;
        out4 = in5;
        out7 = in0;
        out5 = in6;
        out6 = in7;
        out3 = in0 ^ in4;
        out1 = in0 ^ in2;
        out2 = in0 ^ in3;

        out_ptr[0] = out0 ^ in_ptr[0];
        out_ptr[WIDTH] = out1 ^ in_ptr[WIDTH];
        out_ptr[WIDTH * 2] = out2 ^ in_ptr[WIDTH * 2];
        out_ptr[WIDTH * 3] = out3 ^ in_ptr[WIDTH * 3];
        out_ptr[WIDTH * 4] = out4 ^ in_ptr[WIDTH * 4];
        out_ptr[WIDTH * 5] = out5 ^ in_ptr[WIDTH * 5];
        out_ptr[WIDTH * 6] = out6 ^ in_ptr[WIDTH * 6];
        out_ptr[WIDTH * 7] = out7 ^ in_ptr[WIDTH * 7];

        in_ptr++;
        out_ptr++;
    }
}

static void gf8_muladd_8F(void *out, void *in)
{
    unsigned int i;
    uint64_t *in_ptr = (uint64_t *)in;
    uint64_t *out_ptr = (uint64_t *)out;

    for (i = 0; i < WIDTH; i++) {
        uint64_t out0, out1, out2, out3, out4, out5, out6, out7;
        uint64_t tmp0;

        uint64_t in0 = out_ptr[0];
        uint64_t in1 = out_ptr[WIDTH];
        uint64_t in2 = out_ptr[WIDTH * 2];
        uint64_t in3 = out_ptr[WIDTH * 3];
        uint64_t in4 = out_ptr[WIDTH * 4];
        uint64_t in5 = out_ptr[WIDTH * 5];
        uint64_t in6 = out_ptr[WIDTH * 6];
        uint64_t in7 = out_ptr[WIDTH * 7];

        out0 = in0 ^ in1;
        tmp0 = in0 ^ in3;
        out4 = in4 ^ in5;
        out7 = in0 ^ in7;
        out5 = in5 ^ in6;
        out6 = in6 ^ in7;
        out1 = out0 ^ in2;
        out2 = tmp0 ^ in2;
        out3 = tmp0 ^ in4;

        out_ptr[0] = out0 ^ in_ptr[0];
        out_ptr[WIDTH] = out1 ^ in_ptr[WIDTH];
        out_ptr[WIDTH * 2] = out2 ^ in_ptr[WIDTH * 2];
        out_ptr[WIDTH * 3] = out3 ^ in_ptr[WIDTH * 3];
        out_ptr[WIDTH * 4] = out4 ^ in_ptr[WIDTH * 4];
        out_ptr[WIDTH * 5] = out5 ^ in_ptr[WIDTH * 5];
        out_ptr[WIDTH * 6] = out6 ^ in_ptr[WIDTH * 6];
        out_ptr[WIDTH * 7] = out7 ^ in_ptr[WIDTH * 7];

        in_ptr++;
        out_ptr++;
    }
}

static void gf8_muladd_90(void *out, void *in)
{
    unsigned int i;
    uint64_t *in_ptr = (uint64_t *)in;
    uint64_t *out_ptr = (uint64_t *)out;

    for (i = 0; i < WIDTH; i++) {
        uint64_t out0, out1, out2, out3, out4, out5, out6, out7;
        uint64_t tmp0, tmp1, tmp2;

        uint64_t in0 = out_ptr[0];
        uint64_t in1 = out_ptr[WIDTH];
        uint64_t in2 = out_ptr[WIDTH * 2];
        uint64_t in3 = out_ptr[WIDTH * 3];
        uint64_t in4 = out_ptr[WIDTH * 4];
        uint64_t in5 = out_ptr[WIDTH * 5];
        uint64_t in6 = out_ptr[WIDTH * 6];
        uint64_t in7 = out_ptr[WIDTH * 7];

        tmp0 = in1 ^ in2;
        tmp1 = in2 ^ in6 ^ in7;
        out3 = tmp0 ^ in7;
        out1 = tmp1 ^ in5;
        tmp2 = out1 ^ in4;
        out6 = tmp2 ^ in3;
        out5 = out6 ^ in1;
        out4 = out5 ^ in0;
        out0 = tmp0 ^ tmp2;
        out7 = tmp0 ^ out4;
        out2 = tmp1 ^ out5;

        out_ptr[0] = out0 ^ in_ptr[0];
        out_ptr[WIDTH] = out1 ^ in_ptr[WIDTH];
        out_ptr[WIDTH * 2] = out2 ^ in_ptr[WIDTH * 2];
        out_ptr[WIDTH * 3] = out3 ^ in_ptr[WIDTH * 3];
        out_ptr[WIDTH * 4] = out4 ^ in_ptr[WIDTH * 4];
        out_ptr[WIDTH * 5] = out5 ^ in_ptr[WIDTH * 5];
        out_ptr[WIDTH * 6] = out6 ^ in_ptr[WIDTH * 6];
        out_ptr[WIDTH * 7] = out7 ^ in_ptr[WIDTH * 7];

        in_ptr++;
        out_ptr++;
    }
}

static void gf8_muladd_91(void *out, void *in)
{
    unsigned int i;
    uint64_t *in_ptr = (uint64_t *)in;
    uint64_t *out_ptr = (uint64_t *)out;

    for (i = 0; i < WIDTH; i++) {
        uint64_t out0, out1, out2, out3, out4, out5, out6, out7;
        uint64_t tmp0, tmp1, tmp2, tmp3;

        uint64_t in0 = out_ptr[0];
        uint64_t in1 = out_ptr[WIDTH];
        uint64_t in2 = out_ptr[WIDTH * 2];
        uint64_t in3 = out_ptr[WIDTH * 3];
        uint64_t in4 = out_ptr[WIDTH * 4];
        uint64_t in5 = out_ptr[WIDTH * 5];
        uint64_t in6 = out_ptr[WIDTH * 6];
        uint64_t in7 = out_ptr[WIDTH * 7];

        tmp0 = in2 ^ in4;
        tmp1 = tmp0 ^ in3 ^ in5;
        out2 = tmp1 ^ in1;
        out6 = tmp1 ^ in7;
        tmp2 = out2 ^ in5 ^ in7;
        out3 = tmp2 ^ in4;
        out5 = tmp2 ^ in6;
        out1 = tmp1 ^ out5 ^ in2;
        tmp3 = out1 ^ in0;
        out4 = tmp3 ^ in3;
        out0 = tmp0 ^ tmp3;
        out7 = tmp2 ^ tmp3;

        out_ptr[0] = out0 ^ in_ptr[0];
        out_ptr[WIDTH] = out1 ^ in_ptr[WIDTH];
        out_ptr[WIDTH * 2] = out2 ^ in_ptr[WIDTH * 2];
        out_ptr[WIDTH * 3] = out3 ^ in_ptr[WIDTH * 3];
        out_ptr[WIDTH * 4] = out4 ^ in_ptr[WIDTH * 4];
        out_ptr[WIDTH * 5] = out5 ^ in_ptr[WIDTH * 5];
        out_ptr[WIDTH * 6] = out6 ^ in_ptr[WIDTH * 6];
        out_ptr[WIDTH * 7] = out7 ^ in_ptr[WIDTH * 7];

        in_ptr++;
        out_ptr++;
    }
}

static void gf8_muladd_92(void *out, void *in)
{
    unsigned int i;
    uint64_t *in_ptr = (uint64_t *)in;
    uint64_t *out_ptr = (uint64_t *)out;

    for (i = 0; i < WIDTH; i++) {
        uint64_t out0, out1, out2, out3, out4, out5, out6, out7;
        uint64_t tmp0, tmp1;

        uint64_t in0 = out_ptr[0];
        uint64_t in1 = out_ptr[WIDTH];
        uint64_t in2 = out_ptr[WIDTH * 2];
        uint64_t in3 = out_ptr[WIDTH * 3];
        uint64_t in4 = out_ptr[WIDTH * 4];
        uint64_t in5 = out_ptr[WIDTH * 5];
        uint64_t in6 = out_ptr[WIDTH * 6];
        uint64_t in7 = out_ptr[WIDTH * 7];

        out3 = in1;
        tmp0 = in4 ^ in5;
        tmp1 = tmp0 ^ in1;
        out2 = tmp0 ^ in3 ^ in7;
        out0 = tmp1 ^ in6;
        out7 = out2 ^ in0;
        out4 = out0 ^ in0 ^ in2;
        out5 = out4 ^ out7 ^ in5;
        out6 = tmp1 ^ out5;
        out1 = out6 ^ out7 ^ in7;

        out_ptr[0] = out0 ^ in_ptr[0];
        out_ptr[WIDTH] = out1 ^ in_ptr[WIDTH];
        out_ptr[WIDTH * 2] = out2 ^ in_ptr[WIDTH * 2];
        out_ptr[WIDTH * 3] = out3 ^ in_ptr[WIDTH * 3];
        out_ptr[WIDTH * 4] = out4 ^ in_ptr[WIDTH * 4];
        out_ptr[WIDTH * 5] = out5 ^ in_ptr[WIDTH * 5];
        out_ptr[WIDTH * 6] = out6 ^ in_ptr[WIDTH * 6];
        out_ptr[WIDTH * 7] = out7 ^ in_ptr[WIDTH * 7];

        in_ptr++;
        out_ptr++;
    }
}

static void gf8_muladd_93(void *out, void *in)
{
    unsigned int i;
    uint64_t *in_ptr = (uint64_t *)in;
    uint64_t *out_ptr = (uint64_t *)out;

    for (i = 0; i < WIDTH; i++) {
        uint64_t out0, out1, out2, out3, out4, out5, out6, out7;
        uint64_t tmp0, tmp1, tmp2;

        uint64_t in0 = out_ptr[0];
        uint64_t in1 = out_ptr[WIDTH];
        uint64_t in2 = out_ptr[WIDTH * 2];
        uint64_t in3 = out_ptr[WIDTH * 3];
        uint64_t in4 = out_ptr[WIDTH * 4];
        uint64_t in5 = out_ptr[WIDTH * 5];
        uint64_t in6 = out_ptr[WIDTH * 6];
        uint64_t in7 = out_ptr[WIDTH * 7];

        out3 = in1 ^ in3;
        tmp0 = in2 ^ in7;
        tmp1 = out3 ^ in6;
        tmp2 = tmp0 ^ in4;
        out5 = tmp0 ^ tmp1;
        out6 = tmp2 ^ in3;
        out2 = out6 ^ in5;
        out0 = out2 ^ out5 ^ in0;
        out7 = tmp1 ^ out0;
        out1 = tmp2 ^ out0;
        out4 = out1 ^ in7;

        out_ptr[0] = out0 ^ in_ptr[0];
        out_ptr[WIDTH] = out1 ^ in_ptr[WIDTH];
        out_ptr[WIDTH * 2] = out2 ^ in_ptr[WIDTH * 2];
        out_ptr[WIDTH * 3] = out3 ^ in_ptr[WIDTH * 3];
        out_ptr[WIDTH * 4] = out4 ^ in_ptr[WIDTH * 4];
        out_ptr[WIDTH * 5] = out5 ^ in_ptr[WIDTH * 5];
        out_ptr[WIDTH * 6] = out6 ^ in_ptr[WIDTH * 6];
        out_ptr[WIDTH * 7] = out7 ^ in_ptr[WIDTH * 7];

        in_ptr++;
        out_ptr++;
    }
}

static void gf8_muladd_94(void *out, void *in)
{
    unsigned int i;
    uint64_t *in_ptr = (uint64_t *)in;
    uint64_t *out_ptr = (uint64_t *)out;

    for (i = 0; i < WIDTH; i++) {
        uint64_t out0, out1, out2, out3, out4, out5, out6, out7;
        uint64_t tmp0;

        uint64_t in0 = out_ptr[0];
        uint64_t in1 = out_ptr[WIDTH];
        uint64_t in2 = out_ptr[WIDTH * 2];
        uint64_t in3 = out_ptr[WIDTH * 3];
        uint64_t in4 = out_ptr[WIDTH * 4];
        uint64_t in5 = out_ptr[WIDTH * 5];
        uint64_t in6 = out_ptr[WIDTH * 6];
        uint64_t in7 = out_ptr[WIDTH * 7];

        out3 = in2 ^ in6;
        tmp0 = in1 ^ in4 ^ in5;
        out1 = out3 ^ in5;
        out5 = tmp0 ^ out3;
        out0 = tmp0 ^ in7;
        out4 = tmp0 ^ in0 ^ in3;
        out6 = out1 ^ in3 ^ in7;
        out2 = out4 ^ in6;
        out7 = out0 ^ out2 ^ in4;

        out_ptr[0] = out0 ^ in_ptr[0];
        out_ptr[WIDTH] = out1 ^ in_ptr[WIDTH];
        out_ptr[WIDTH * 2] = out2 ^ in_ptr[WIDTH * 2];
        out_ptr[WIDTH * 3] = out3 ^ in_ptr[WIDTH * 3];
        out_ptr[WIDTH * 4] = out4 ^ in_ptr[WIDTH * 4];
        out_ptr[WIDTH * 5] = out5 ^ in_ptr[WIDTH * 5];
        out_ptr[WIDTH * 6] = out6 ^ in_ptr[WIDTH * 6];
        out_ptr[WIDTH * 7] = out7 ^ in_ptr[WIDTH * 7];

        in_ptr++;
        out_ptr++;
    }
}

static void gf8_muladd_95(void *out, void *in)
{
    unsigned int i;
    uint64_t *in_ptr = (uint64_t *)in;
    uint64_t *out_ptr = (uint64_t *)out;

    for (i = 0; i < WIDTH; i++) {
        uint64_t out0, out1, out2, out3, out4, out5, out6, out7;
        uint64_t tmp0, tmp1, tmp2, tmp3, tmp4, tmp5;

        uint64_t in0 = out_ptr[0];
        uint64_t in1 = out_ptr[WIDTH];
        uint64_t in2 = out_ptr[WIDTH * 2];
        uint64_t in3 = out_ptr[WIDTH * 3];
        uint64_t in4 = out_ptr[WIDTH * 4];
        uint64_t in5 = out_ptr[WIDTH * 5];
        uint64_t in6 = out_ptr[WIDTH * 6];
        uint64_t in7 = out_ptr[WIDTH * 7];

        tmp0 = in2 ^ in3;
        out3 = tmp0 ^ in6;
        tmp1 = tmp0 ^ in7;
        tmp2 = out3 ^ in0;
        out6 = tmp1 ^ in5;
        tmp3 = tmp2 ^ in4;
        out7 = tmp3 ^ in2;
        tmp4 = tmp3 ^ in5;
        out2 = tmp4 ^ in1;
        tmp5 = out2 ^ in6;
        out0 = tmp1 ^ tmp5;
        out1 = tmp5 ^ out7;
        out4 = tmp2 ^ out1;
        out5 = tmp4 ^ out4;

        out_ptr[0] = out0 ^ in_ptr[0];
        out_ptr[WIDTH] = out1 ^ in_ptr[WIDTH];
        out_ptr[WIDTH * 2] = out2 ^ in_ptr[WIDTH * 2];
        out_ptr[WIDTH * 3] = out3 ^ in_ptr[WIDTH * 3];
        out_ptr[WIDTH * 4] = out4 ^ in_ptr[WIDTH * 4];
        out_ptr[WIDTH * 5] = out5 ^ in_ptr[WIDTH * 5];
        out_ptr[WIDTH * 6] = out6 ^ in_ptr[WIDTH * 6];
        out_ptr[WIDTH * 7] = out7 ^ in_ptr[WIDTH * 7];

        in_ptr++;
        out_ptr++;
    }
}

static void gf8_muladd_96(void *out, void *in)
{
    unsigned int i;
    uint64_t *in_ptr = (uint64_t *)in;
    uint64_t *out_ptr = (uint64_t *)out;

    for (i = 0; i < WIDTH; i++) {
        uint64_t out0, out1, out2, out3, out4, out5, out6, out7;
        uint64_t tmp0, tmp1, tmp2;

        uint64_t in0 = out_ptr[0];
        uint64_t in1 = out_ptr[WIDTH];
        uint64_t in2 = out_ptr[WIDTH * 2];
        uint64_t in3 = out_ptr[WIDTH * 3];
        uint64_t in4 = out_ptr[WIDTH * 4];
        uint64_t in5 = out_ptr[WIDTH * 5];
        uint64_t in6 = out_ptr[WIDTH * 6];
        uint64_t in7 = out_ptr[WIDTH * 7];

        out3 = in6 ^ in7;
        tmp0 = in1 ^ in5;
        tmp1 = in5 ^ in6;
        out6 = out3 ^ in2 ^ in3;
        out0 = tmp0 ^ in4;
        tmp2 = tmp1 ^ in2;
        out4 = out0 ^ in0 ^ in7;
        out1 = tmp2 ^ in0;
        out5 = tmp2 ^ in1;
        out7 = tmp0 ^ out4 ^ in3;
        out2 = tmp1 ^ out7;

        out_ptr[0] = out0 ^ in_ptr[0];
        out_ptr[WIDTH] = out1 ^ in_ptr[WIDTH];
        out_ptr[WIDTH * 2] = out2 ^ in_ptr[WIDTH * 2];
        out_ptr[WIDTH * 3] = out3 ^ in_ptr[WIDTH * 3];
        out_ptr[WIDTH * 4] = out4 ^ in_ptr[WIDTH * 4];
        out_ptr[WIDTH * 5] = out5 ^ in_ptr[WIDTH * 5];
        out_ptr[WIDTH * 6] = out6 ^ in_ptr[WIDTH * 6];
        out_ptr[WIDTH * 7] = out7 ^ in_ptr[WIDTH * 7];

        in_ptr++;
        out_ptr++;
    }
}

static void gf8_muladd_97(void *out, void *in)
{
    unsigned int i;
    uint64_t *in_ptr = (uint64_t *)in;
    uint64_t *out_ptr = (uint64_t *)out;

    for (i = 0; i < WIDTH; i++) {
        uint64_t out0, out1, out2, out3, out4, out5, out6, out7;
        uint64_t tmp0, tmp1, tmp2, tmp3;

        uint64_t in0 = out_ptr[0];
        uint64_t in1 = out_ptr[WIDTH];
        uint64_t in2 = out_ptr[WIDTH * 2];
        uint64_t in3 = out_ptr[WIDTH * 3];
        uint64_t in4 = out_ptr[WIDTH * 4];
        uint64_t in5 = out_ptr[WIDTH * 5];
        uint64_t in6 = out_ptr[WIDTH * 6];
        uint64_t in7 = out_ptr[WIDTH * 7];

        tmp0 = in0 ^ in4;
        tmp1 = in2 ^ in6;
        out3 = in3 ^ in6 ^ in7;
        out7 = tmp0 ^ in3;
        tmp2 = tmp0 ^ in5;
        out5 = tmp1 ^ in1;
        out6 = tmp1 ^ out3;
        out0 = tmp2 ^ in1;
        out2 = tmp2 ^ out3 ^ in2;
        tmp3 = out0 ^ in4;
        out4 = tmp3 ^ in7;
        out1 = tmp1 ^ tmp3;

        out_ptr[0] = out0 ^ in_ptr[0];
        out_ptr[WIDTH] = out1 ^ in_ptr[WIDTH];
        out_ptr[WIDTH * 2] = out2 ^ in_ptr[WIDTH * 2];
        out_ptr[WIDTH * 3] = out3 ^ in_ptr[WIDTH * 3];
        out_ptr[WIDTH * 4] = out4 ^ in_ptr[WIDTH * 4];
        out_ptr[WIDTH * 5] = out5 ^ in_ptr[WIDTH * 5];
        out_ptr[WIDTH * 6] = out6 ^ in_ptr[WIDTH * 6];
        out_ptr[WIDTH * 7] = out7 ^ in_ptr[WIDTH * 7];

        in_ptr++;
        out_ptr++;
    }
}

static void gf8_muladd_98(void *out, void *in)
{
    unsigned int i;
    uint64_t *in_ptr = (uint64_t *)in;
    uint64_t *out_ptr = (uint64_t *)out;

    for (i = 0; i < WIDTH; i++) {
        uint64_t out0, out1, out2, out3, out4, out5, out6, out7;
        uint64_t tmp0, tmp1;

        uint64_t in0 = out_ptr[0];
        uint64_t in1 = out_ptr[WIDTH];
        uint64_t in2 = out_ptr[WIDTH * 2];
        uint64_t in3 = out_ptr[WIDTH * 3];
        uint64_t in4 = out_ptr[WIDTH * 4];
        uint64_t in5 = out_ptr[WIDTH * 5];
        uint64_t in6 = out_ptr[WIDTH * 6];
        uint64_t in7 = out_ptr[WIDTH * 7];

        tmp0 = in5 ^ in7;
        tmp1 = in1 ^ in4 ^ in7;
        out1 = tmp0 ^ in2;
        out0 = tmp1 ^ in6;
        out2 = tmp1 ^ in3;
        out6 = out0 ^ out1 ^ in1;
        out5 = tmp0 ^ out2;
        out3 = tmp1 ^ out6 ^ in0;
        out7 = out0 ^ out5 ^ in0;
        out4 = out6 ^ out7 ^ in7;

        out_ptr[0] = out0 ^ in_ptr[0];
        out_ptr[WIDTH] = out1 ^ in_ptr[WIDTH];
        out_ptr[WIDTH * 2] = out2 ^ in_ptr[WIDTH * 2];
        out_ptr[WIDTH * 3] = out3 ^ in_ptr[WIDTH * 3];
        out_ptr[WIDTH * 4] = out4 ^ in_ptr[WIDTH * 4];
        out_ptr[WIDTH * 5] = out5 ^ in_ptr[WIDTH * 5];
        out_ptr[WIDTH * 6] = out6 ^ in_ptr[WIDTH * 6];
        out_ptr[WIDTH * 7] = out7 ^ in_ptr[WIDTH * 7];

        in_ptr++;
        out_ptr++;
    }
}

static void gf8_muladd_99(void *out, void *in)
{
    unsigned int i;
    uint64_t *in_ptr = (uint64_t *)in;
    uint64_t *out_ptr = (uint64_t *)out;

    for (i = 0; i < WIDTH; i++) {
        uint64_t out0, out1, out2, out3, out4, out5, out6, out7;
        uint64_t tmp0, tmp1, tmp2;

        uint64_t in0 = out_ptr[0];
        uint64_t in1 = out_ptr[WIDTH];
        uint64_t in2 = out_ptr[WIDTH * 2];
        uint64_t in3 = out_ptr[WIDTH * 3];
        uint64_t in4 = out_ptr[WIDTH * 4];
        uint64_t in5 = out_ptr[WIDTH * 5];
        uint64_t in6 = out_ptr[WIDTH * 6];
        uint64_t in7 = out_ptr[WIDTH * 7];

        tmp0 = in0 ^ in3;
        out5 = in1 ^ in3 ^ in4;
        out6 = in2 ^ in4 ^ in5;
        out4 = tmp0 ^ in2;
        tmp1 = tmp0 ^ in6;
        tmp2 = out5 ^ in7;
        out7 = tmp1 ^ in5;
        out0 = tmp1 ^ tmp2;
        out2 = tmp2 ^ in2;
        out3 = out0 ^ out6 ^ in3;
        out1 = tmp1 ^ out3;

        out_ptr[0] = out0 ^ in_ptr[0];
        out_ptr[WIDTH] = out1 ^ in_ptr[WIDTH];
        out_ptr[WIDTH * 2] = out2 ^ in_ptr[WIDTH * 2];
        out_ptr[WIDTH * 3] = out3 ^ in_ptr[WIDTH * 3];
        out_ptr[WIDTH * 4] = out4 ^ in_ptr[WIDTH * 4];
        out_ptr[WIDTH * 5] = out5 ^ in_ptr[WIDTH * 5];
        out_ptr[WIDTH * 6] = out6 ^ in_ptr[WIDTH * 6];
        out_ptr[WIDTH * 7] = out7 ^ in_ptr[WIDTH * 7];

        in_ptr++;
        out_ptr++;
    }
}

static void gf8_muladd_9A(void *out, void *in)
{
    unsigned int i;
    uint64_t *in_ptr = (uint64_t *)in;
    uint64_t *out_ptr = (uint64_t *)out;

    for (i = 0; i < WIDTH; i++) {
        uint64_t out0, out1, out2, out3, out4, out5, out6, out7;
        uint64_t tmp0, tmp1, tmp2;

        uint64_t in0 = out_ptr[0];
        uint64_t in1 = out_ptr[WIDTH];
        uint64_t in2 = out_ptr[WIDTH * 2];
        uint64_t in3 = out_ptr[WIDTH * 3];
        uint64_t in4 = out_ptr[WIDTH * 4];
        uint64_t in5 = out_ptr[WIDTH * 5];
        uint64_t in6 = out_ptr[WIDTH * 6];
        uint64_t in7 = out_ptr[WIDTH * 7];

        out2 = in3 ^ in4;
        tmp0 = in0 ^ in5;
        tmp1 = in1 ^ in6;
        out5 = in1 ^ in3 ^ in5;
        tmp2 = tmp0 ^ in7;
        out3 = tmp0 ^ tmp1;
        out0 = tmp1 ^ in4;
        out7 = tmp2 ^ in3;
        out1 = tmp2 ^ in2;
        out6 = out0 ^ in1 ^ in2;
        out4 = out1 ^ in4 ^ in5;

        out_ptr[0] = out0 ^ in_ptr[0];
        out_ptr[WIDTH] = out1 ^ in_ptr[WIDTH];
        out_ptr[WIDTH * 2] = out2 ^ in_ptr[WIDTH * 2];
        out_ptr[WIDTH * 3] = out3 ^ in_ptr[WIDTH * 3];
        out_ptr[WIDTH * 4] = out4 ^ in_ptr[WIDTH * 4];
        out_ptr[WIDTH * 5] = out5 ^ in_ptr[WIDTH * 5];
        out_ptr[WIDTH * 6] = out6 ^ in_ptr[WIDTH * 6];
        out_ptr[WIDTH * 7] = out7 ^ in_ptr[WIDTH * 7];

        in_ptr++;
        out_ptr++;
    }
}

static void gf8_muladd_9B(void *out, void *in)
{
    unsigned int i;
    uint64_t *in_ptr = (uint64_t *)in;
    uint64_t *out_ptr = (uint64_t *)out;

    for (i = 0; i < WIDTH; i++) {
        uint64_t out0, out1, out2, out3, out4, out5, out6, out7;
        uint64_t tmp0;

        uint64_t in0 = out_ptr[0];
        uint64_t in1 = out_ptr[WIDTH];
        uint64_t in2 = out_ptr[WIDTH * 2];
        uint64_t in3 = out_ptr[WIDTH * 3];
        uint64_t in4 = out_ptr[WIDTH * 4];
        uint64_t in5 = out_ptr[WIDTH * 5];
        uint64_t in6 = out_ptr[WIDTH * 6];
        uint64_t in7 = out_ptr[WIDTH * 7];

        out5 = in1 ^ in3;
        tmp0 = in3 ^ in5;
        out6 = in2 ^ in4;
        out4 = in0 ^ in2 ^ in7;
        out7 = tmp0 ^ in0;
        out2 = out6 ^ in3;
        out1 = out4 ^ in1 ^ in5;
        out3 = out7 ^ in1 ^ in6;
        out0 = tmp0 ^ out3 ^ in4;

        out_ptr[0] = out0 ^ in_ptr[0];
        out_ptr[WIDTH] = out1 ^ in_ptr[WIDTH];
        out_ptr[WIDTH * 2] = out2 ^ in_ptr[WIDTH * 2];
        out_ptr[WIDTH * 3] = out3 ^ in_ptr[WIDTH * 3];
        out_ptr[WIDTH * 4] = out4 ^ in_ptr[WIDTH * 4];
        out_ptr[WIDTH * 5] = out5 ^ in_ptr[WIDTH * 5];
        out_ptr[WIDTH * 6] = out6 ^ in_ptr[WIDTH * 6];
        out_ptr[WIDTH * 7] = out7 ^ in_ptr[WIDTH * 7];

        in_ptr++;
        out_ptr++;
    }
}

static void gf8_muladd_9C(void *out, void *in)
{
    unsigned int i;
    uint64_t *in_ptr = (uint64_t *)in;
    uint64_t *out_ptr = (uint64_t *)out;

    for (i = 0; i < WIDTH; i++) {
        uint64_t out0, out1, out2, out3, out4, out5, out6, out7;
        uint64_t tmp0;

        uint64_t in0 = out_ptr[0];
        uint64_t in1 = out_ptr[WIDTH];
        uint64_t in2 = out_ptr[WIDTH * 2];
        uint64_t in3 = out_ptr[WIDTH * 3];
        uint64_t in4 = out_ptr[WIDTH * 4];
        uint64_t in5 = out_ptr[WIDTH * 5];
        uint64_t in6 = out_ptr[WIDTH * 6];
        uint64_t in7 = out_ptr[WIDTH * 7];

        out1 = in2 ^ in5;
        tmp0 = in0 ^ in3 ^ in6;
        out3 = out1 ^ in0;
        out6 = out1 ^ in6;
        out7 = tmp0 ^ in7;
        out4 = out7 ^ in4;
        out2 = out4 ^ in1;
        out0 = tmp0 ^ out2;
        out5 = out0 ^ in5;

        out_ptr[0] = out0 ^ in_ptr[0];
        out_ptr[WIDTH] = out1 ^ in_ptr[WIDTH];
        out_ptr[WIDTH * 2] = out2 ^ in_ptr[WIDTH * 2];
        out_ptr[WIDTH * 3] = out3 ^ in_ptr[WIDTH * 3];
        out_ptr[WIDTH * 4] = out4 ^ in_ptr[WIDTH * 4];
        out_ptr[WIDTH * 5] = out5 ^ in_ptr[WIDTH * 5];
        out_ptr[WIDTH * 6] = out6 ^ in_ptr[WIDTH * 6];
        out_ptr[WIDTH * 7] = out7 ^ in_ptr[WIDTH * 7];

        in_ptr++;
        out_ptr++;
    }
}

static void gf8_muladd_9D(void *out, void *in)
{
    unsigned int i;
    uint64_t *in_ptr = (uint64_t *)in;
    uint64_t *out_ptr = (uint64_t *)out;

    for (i = 0; i < WIDTH; i++) {
        uint64_t out0, out1, out2, out3, out4, out5, out6, out7;
        uint64_t tmp0;

        uint64_t in0 = out_ptr[0];
        uint64_t in1 = out_ptr[WIDTH];
        uint64_t in2 = out_ptr[WIDTH * 2];
        uint64_t in3 = out_ptr[WIDTH * 3];
        uint64_t in4 = out_ptr[WIDTH * 4];
        uint64_t in5 = out_ptr[WIDTH * 5];
        uint64_t in6 = out_ptr[WIDTH * 6];
        uint64_t in7 = out_ptr[WIDTH * 7];

        out6 = in2 ^ in5;
        tmp0 = in0 ^ in3;
        out5 = in1 ^ in4 ^ in7;
        out1 = out6 ^ in1;
        out3 = tmp0 ^ out6;
        out7 = tmp0 ^ in6;
        out0 = out5 ^ in0;
        out4 = out7 ^ in7;
        out2 = out5 ^ out7 ^ in2;

        out_ptr[0] = out0 ^ in_ptr[0];
        out_ptr[WIDTH] = out1 ^ in_ptr[WIDTH];
        out_ptr[WIDTH * 2] = out2 ^ in_ptr[WIDTH * 2];
        out_ptr[WIDTH * 3] = out3 ^ in_ptr[WIDTH * 3];
        out_ptr[WIDTH * 4] = out4 ^ in_ptr[WIDTH * 4];
        out_ptr[WIDTH * 5] = out5 ^ in_ptr[WIDTH * 5];
        out_ptr[WIDTH * 6] = out6 ^ in_ptr[WIDTH * 6];
        out_ptr[WIDTH * 7] = out7 ^ in_ptr[WIDTH * 7];

        in_ptr++;
        out_ptr++;
    }
}

static void gf8_muladd_9E(void *out, void *in)
{
    unsigned int i;
    uint64_t *in_ptr = (uint64_t *)in;
    uint64_t *out_ptr = (uint64_t *)out;

    for (i = 0; i < WIDTH; i++) {
        uint64_t out0, out1, out2, out3, out4, out5, out6, out7;
        uint64_t tmp0;

        uint64_t in0 = out_ptr[0];
        uint64_t in1 = out_ptr[WIDTH];
        uint64_t in2 = out_ptr[WIDTH * 2];
        uint64_t in3 = out_ptr[WIDTH * 3];
        uint64_t in4 = out_ptr[WIDTH * 4];
        uint64_t in5 = out_ptr[WIDTH * 5];
        uint64_t in6 = out_ptr[WIDTH * 6];
        uint64_t in7 = out_ptr[WIDTH * 7];

        out0 = in1 ^ in4;
        tmp0 = in0 ^ in5;
        out6 = in2 ^ in6;
        out7 = in0 ^ in3 ^ in7;
        out4 = in0 ^ in4 ^ in6;
        out5 = in1 ^ in5 ^ in7;
        out1 = tmp0 ^ in2;
        out3 = tmp0 ^ in7;
        out2 = out4 ^ in3;

        out_ptr[0] = out0 ^ in_ptr[0];
        out_ptr[WIDTH] = out1 ^ in_ptr[WIDTH];
        out_ptr[WIDTH * 2] = out2 ^ in_ptr[WIDTH * 2];
        out_ptr[WIDTH * 3] = out3 ^ in_ptr[WIDTH * 3];
        out_ptr[WIDTH * 4] = out4 ^ in_ptr[WIDTH * 4];
        out_ptr[WIDTH * 5] = out5 ^ in_ptr[WIDTH * 5];
        out_ptr[WIDTH * 6] = out6 ^ in_ptr[WIDTH * 6];
        out_ptr[WIDTH * 7] = out7 ^ in_ptr[WIDTH * 7];

        in_ptr++;
        out_ptr++;
    }
}

static void gf8_muladd_9F(void *out, void *in)
{
    unsigned int i;
    uint64_t *in_ptr = (uint64_t *)in;
    uint64_t *out_ptr = (uint64_t *)out;

    for (i = 0; i < WIDTH; i++) {
        uint64_t out0, out1, out2, out3, out4, out5, out6, out7;
        uint64_t tmp0;

        uint64_t in0 = out_ptr[0];
        uint64_t in1 = out_ptr[WIDTH];
        uint64_t in2 = out_ptr[WIDTH * 2];
        uint64_t in3 = out_ptr[WIDTH * 3];
        uint64_t in4 = out_ptr[WIDTH * 4];
        uint64_t in5 = out_ptr[WIDTH * 5];
        uint64_t in6 = out_ptr[WIDTH * 6];
        uint64_t in7 = out_ptr[WIDTH * 7];

        out6 = in2;
        out7 = in0 ^ in3;
        tmp0 = in0 ^ in1;
        out4 = in0 ^ in6;
        out5 = in1 ^ in7;
        out1 = tmp0 ^ in2 ^ in5;
        out2 = out7 ^ in2 ^ in4 ^ in6;
        out3 = out7 ^ in5 ^ in7;
        out0 = tmp0 ^ in4;

        out_ptr[0] = out0 ^ in_ptr[0];
        out_ptr[WIDTH] = out1 ^ in_ptr[WIDTH];
        out_ptr[WIDTH * 2] = out2 ^ in_ptr[WIDTH * 2];
        out_ptr[WIDTH * 3] = out3 ^ in_ptr[WIDTH * 3];
        out_ptr[WIDTH * 4] = out4 ^ in_ptr[WIDTH * 4];
        out_ptr[WIDTH * 5] = out5 ^ in_ptr[WIDTH * 5];
        out_ptr[WIDTH * 6] = out6 ^ in_ptr[WIDTH * 6];
        out_ptr[WIDTH * 7] = out7 ^ in_ptr[WIDTH * 7];

        in_ptr++;
        out_ptr++;
    }
}

static void gf8_muladd_A0(void *out, void *in)
{
    unsigned int i;
    uint64_t *in_ptr = (uint64_t *)in;
    uint64_t *out_ptr = (uint64_t *)out;

    for (i = 0; i < WIDTH; i++) {
        uint64_t out0, out1, out2, out3, out4, out5, out6, out7;
        uint64_t tmp0, tmp1, tmp2, tmp3;

        uint64_t in0 = out_ptr[0];
        uint64_t in1 = out_ptr[WIDTH];
        uint64_t in2 = out_ptr[WIDTH * 2];
        uint64_t in3 = out_ptr[WIDTH * 3];
        uint64_t in4 = out_ptr[WIDTH * 4];
        uint64_t in5 = out_ptr[WIDTH * 5];
        uint64_t in6 = out_ptr[WIDTH * 6];
        uint64_t in7 = out_ptr[WIDTH * 7];

        tmp0 = in1 ^ in6;
        out2 = tmp0 ^ in7;
        tmp1 = tmp0 ^ in5;
        out6 = out2 ^ in3 ^ in4;
        out0 = tmp1 ^ in3;
        tmp2 = out0 ^ in2;
        out3 = tmp2 ^ in7;
        tmp3 = tmp2 ^ in1;
        out5 = tmp3 ^ in0;
        out4 = tmp3 ^ out6;
        out7 = out5 ^ out6 ^ in1;
        out1 = tmp1 ^ out4;

        out_ptr[0] = out0 ^ in_ptr[0];
        out_ptr[WIDTH] = out1 ^ in_ptr[WIDTH];
        out_ptr[WIDTH * 2] = out2 ^ in_ptr[WIDTH * 2];
        out_ptr[WIDTH * 3] = out3 ^ in_ptr[WIDTH * 3];
        out_ptr[WIDTH * 4] = out4 ^ in_ptr[WIDTH * 4];
        out_ptr[WIDTH * 5] = out5 ^ in_ptr[WIDTH * 5];
        out_ptr[WIDTH * 6] = out6 ^ in_ptr[WIDTH * 6];
        out_ptr[WIDTH * 7] = out7 ^ in_ptr[WIDTH * 7];

        in_ptr++;
        out_ptr++;
    }
}

static void gf8_muladd_A1(void *out, void *in)
{
    unsigned int i;
    uint64_t *in_ptr = (uint64_t *)in;
    uint64_t *out_ptr = (uint64_t *)out;

    for (i = 0; i < WIDTH; i++) {
        uint64_t out0, out1, out2, out3, out4, out5, out6, out7;
        uint64_t tmp0, tmp1, tmp2;

        uint64_t in0 = out_ptr[0];
        uint64_t in1 = out_ptr[WIDTH];
        uint64_t in2 = out_ptr[WIDTH * 2];
        uint64_t in3 = out_ptr[WIDTH * 3];
        uint64_t in4 = out_ptr[WIDTH * 4];
        uint64_t in5 = out_ptr[WIDTH * 5];
        uint64_t in6 = out_ptr[WIDTH * 6];
        uint64_t in7 = out_ptr[WIDTH * 7];

        tmp0 = in2 ^ in5;
        tmp1 = tmp0 ^ in1;
        tmp2 = tmp0 ^ in4;
        out4 = tmp1 ^ in7;
        out7 = tmp2 ^ in0;
        out6 = tmp2 ^ out4 ^ in3;
        out3 = out4 ^ in6;
        out2 = out3 ^ in5;
        out1 = out2 ^ in4;
        out5 = out1 ^ out6 ^ in0;
        out0 = tmp1 ^ out5;

        out_ptr[0] = out0 ^ in_ptr[0];
        out_ptr[WIDTH] = out1 ^ in_ptr[WIDTH];
        out_ptr[WIDTH * 2] = out2 ^ in_ptr[WIDTH * 2];
        out_ptr[WIDTH * 3] = out3 ^ in_ptr[WIDTH * 3];
        out_ptr[WIDTH * 4] = out4 ^ in_ptr[WIDTH * 4];
        out_ptr[WIDTH * 5] = out5 ^ in_ptr[WIDTH * 5];
        out_ptr[WIDTH * 6] = out6 ^ in_ptr[WIDTH * 6];
        out_ptr[WIDTH * 7] = out7 ^ in_ptr[WIDTH * 7];

        in_ptr++;
        out_ptr++;
    }
}

static void gf8_muladd_A2(void *out, void *in)
{
    unsigned int i;
    uint64_t *in_ptr = (uint64_t *)in;
    uint64_t *out_ptr = (uint64_t *)out;

    for (i = 0; i < WIDTH; i++) {
        uint64_t out0, out1, out2, out3, out4, out5, out6, out7;
        uint64_t tmp0;

        uint64_t in0 = out_ptr[0];
        uint64_t in1 = out_ptr[WIDTH];
        uint64_t in2 = out_ptr[WIDTH * 2];
        uint64_t in3 = out_ptr[WIDTH * 3];
        uint64_t in4 = out_ptr[WIDTH * 4];
        uint64_t in5 = out_ptr[WIDTH * 5];
        uint64_t in6 = out_ptr[WIDTH * 6];
        uint64_t in7 = out_ptr[WIDTH * 7];

        out2 = in6;
        tmp0 = in1 ^ in3 ^ in5;
        out3 = tmp0 ^ in6;
        out4 = tmp0 ^ in2 ^ in4;
        out0 = out3 ^ in7;
        out6 = out0 ^ in4;
        out1 = out0 ^ out4 ^ in0;
        out7 = out1 ^ in5;
        out5 = out7 ^ in3 ^ in7;

        out_ptr[0] = out0 ^ in_ptr[0];
        out_ptr[WIDTH] = out1 ^ in_ptr[WIDTH];
        out_ptr[WIDTH * 2] = out2 ^ in_ptr[WIDTH * 2];
        out_ptr[WIDTH * 3] = out3 ^ in_ptr[WIDTH * 3];
        out_ptr[WIDTH * 4] = out4 ^ in_ptr[WIDTH * 4];
        out_ptr[WIDTH * 5] = out5 ^ in_ptr[WIDTH * 5];
        out_ptr[WIDTH * 6] = out6 ^ in_ptr[WIDTH * 6];
        out_ptr[WIDTH * 7] = out7 ^ in_ptr[WIDTH * 7];

        in_ptr++;
        out_ptr++;
    }
}

static void gf8_muladd_A3(void *out, void *in)
{
    unsigned int i;
    uint64_t *in_ptr = (uint64_t *)in;
    uint64_t *out_ptr = (uint64_t *)out;

    for (i = 0; i < WIDTH; i++) {
        uint64_t out0, out1, out2, out3, out4, out5, out6, out7;
        uint64_t tmp0, tmp1;

        uint64_t in0 = out_ptr[0];
        uint64_t in1 = out_ptr[WIDTH];
        uint64_t in2 = out_ptr[WIDTH * 2];
        uint64_t in3 = out_ptr[WIDTH * 3];
        uint64_t in4 = out_ptr[WIDTH * 4];
        uint64_t in5 = out_ptr[WIDTH * 5];
        uint64_t in6 = out_ptr[WIDTH * 6];
        uint64_t in7 = out_ptr[WIDTH * 7];

        out2 = in2 ^ in6;
        out3 = in1 ^ in5 ^ in6;
        tmp0 = out2 ^ in0;
        out4 = out2 ^ out3 ^ in3;
        tmp1 = tmp0 ^ in4;
        out0 = tmp0 ^ out4 ^ in7;
        out5 = tmp1 ^ in3;
        out7 = tmp1 ^ in5;
        out1 = tmp1 ^ in1 ^ in7;
        out6 = tmp1 ^ out0 ^ in2;

        out_ptr[0] = out0 ^ in_ptr[0];
        out_ptr[WIDTH] = out1 ^ in_ptr[WIDTH];
        out_ptr[WIDTH * 2] = out2 ^ in_ptr[WIDTH * 2];
        out_ptr[WIDTH * 3] = out3 ^ in_ptr[WIDTH * 3];
        out_ptr[WIDTH * 4] = out4 ^ in_ptr[WIDTH * 4];
        out_ptr[WIDTH * 5] = out5 ^ in_ptr[WIDTH * 5];
        out_ptr[WIDTH * 6] = out6 ^ in_ptr[WIDTH * 6];
        out_ptr[WIDTH * 7] = out7 ^ in_ptr[WIDTH * 7];

        in_ptr++;
        out_ptr++;
    }
}

static void gf8_muladd_A4(void *out, void *in)
{
    unsigned int i;
    uint64_t *in_ptr = (uint64_t *)in;
    uint64_t *out_ptr = (uint64_t *)out;

    for (i = 0; i < WIDTH; i++) {
        uint64_t out0, out1, out2, out3, out4, out5, out6, out7;
        uint64_t tmp0, tmp1, tmp2, tmp3, tmp4;

        uint64_t in0 = out_ptr[0];
        uint64_t in1 = out_ptr[WIDTH];
        uint64_t in2 = out_ptr[WIDTH * 2];
        uint64_t in3 = out_ptr[WIDTH * 3];
        uint64_t in4 = out_ptr[WIDTH * 4];
        uint64_t in5 = out_ptr[WIDTH * 5];
        uint64_t in6 = out_ptr[WIDTH * 6];
        uint64_t in7 = out_ptr[WIDTH * 7];

        tmp0 = in1 ^ in3;
        tmp1 = in2 ^ in4;
        tmp2 = in2 ^ in5;
        tmp3 = in0 ^ in7;
        out0 = tmp0 ^ in5;
        out6 = tmp0 ^ in6 ^ in7;
        out1 = tmp1 ^ in6;
        out7 = tmp1 ^ tmp3;
        out3 = tmp2 ^ in3;
        tmp4 = tmp2 ^ out1;
        out2 = tmp3 ^ in1;
        out5 = tmp4 ^ out7;
        out4 = tmp4 ^ in1;

        out_ptr[0] = out0 ^ in_ptr[0];
        out_ptr[WIDTH] = out1 ^ in_ptr[WIDTH];
        out_ptr[WIDTH * 2] = out2 ^ in_ptr[WIDTH * 2];
        out_ptr[WIDTH * 3] = out3 ^ in_ptr[WIDTH * 3];
        out_ptr[WIDTH * 4] = out4 ^ in_ptr[WIDTH * 4];
        out_ptr[WIDTH * 5] = out5 ^ in_ptr[WIDTH * 5];
        out_ptr[WIDTH * 6] = out6 ^ in_ptr[WIDTH * 6];
        out_ptr[WIDTH * 7] = out7 ^ in_ptr[WIDTH * 7];

        in_ptr++;
        out_ptr++;
    }
}

static void gf8_muladd_A5(void *out, void *in)
{
    unsigned int i;
    uint64_t *in_ptr = (uint64_t *)in;
    uint64_t *out_ptr = (uint64_t *)out;

    for (i = 0; i < WIDTH; i++) {
        uint64_t out0, out1, out2, out3, out4, out5, out6, out7;
        uint64_t tmp0, tmp1, tmp2;

        uint64_t in0 = out_ptr[0];
        uint64_t in1 = out_ptr[WIDTH];
        uint64_t in2 = out_ptr[WIDTH * 2];
        uint64_t in3 = out_ptr[WIDTH * 3];
        uint64_t in4 = out_ptr[WIDTH * 4];
        uint64_t in5 = out_ptr[WIDTH * 5];
        uint64_t in6 = out_ptr[WIDTH * 6];
        uint64_t in7 = out_ptr[WIDTH * 7];

        out3 = in2 ^ in5;
        tmp0 = in1 ^ in6;
        tmp1 = in0 ^ in1;
        tmp2 = in2 ^ in4;
        out6 = in1 ^ in3 ^ in7;
        out4 = tmp0 ^ in5;
        out1 = tmp0 ^ tmp2;
        out0 = tmp1 ^ in3 ^ in5;
        out2 = tmp1 ^ in2 ^ in7;
        out7 = tmp2 ^ in0;
        out5 = tmp0 ^ out2;

        out_ptr[0] = out0 ^ in_ptr[0];
        out_ptr[WIDTH] = out1 ^ in_ptr[WIDTH];
        out_ptr[WIDTH * 2] = out2 ^ in_ptr[WIDTH * 2];
        out_ptr[WIDTH * 3] = out3 ^ in_ptr[WIDTH * 3];
        out_ptr[WIDTH * 4] = out4 ^ in_ptr[WIDTH * 4];
        out_ptr[WIDTH * 5] = out5 ^ in_ptr[WIDTH * 5];
        out_ptr[WIDTH * 6] = out6 ^ in_ptr[WIDTH * 6];
        out_ptr[WIDTH * 7] = out7 ^ in_ptr[WIDTH * 7];

        in_ptr++;
        out_ptr++;
    }
}

static void gf8_muladd_A6(void *out, void *in)
{
    unsigned int i;
    uint64_t *in_ptr = (uint64_t *)in;
    uint64_t *out_ptr = (uint64_t *)out;

    for (i = 0; i < WIDTH; i++) {
        uint64_t out0, out1, out2, out3, out4, out5, out6, out7;

        uint64_t in0 = out_ptr[0];
        uint64_t in1 = out_ptr[WIDTH];
        uint64_t in2 = out_ptr[WIDTH * 2];
        uint64_t in3 = out_ptr[WIDTH * 3];
        uint64_t in4 = out_ptr[WIDTH * 4];
        uint64_t in5 = out_ptr[WIDTH * 5];
        uint64_t in6 = out_ptr[WIDTH * 6];
        uint64_t in7 = out_ptr[WIDTH * 7];

        out2 = in0;
        out3 = in3 ^ in5 ^ in7;
        out1 = in0 ^ in2 ^ in4 ^ in6;
        out0 = out3 ^ in1;
        out7 = out1 ^ in7;
        out6 = out0 ^ in6;
        out5 = out7 ^ in5;
        out4 = out6 ^ in4;

        out_ptr[0] = out0 ^ in_ptr[0];
        out_ptr[WIDTH] = out1 ^ in_ptr[WIDTH];
        out_ptr[WIDTH * 2] = out2 ^ in_ptr[WIDTH * 2];
        out_ptr[WIDTH * 3] = out3 ^ in_ptr[WIDTH * 3];
        out_ptr[WIDTH * 4] = out4 ^ in_ptr[WIDTH * 4];
        out_ptr[WIDTH * 5] = out5 ^ in_ptr[WIDTH * 5];
        out_ptr[WIDTH * 6] = out6 ^ in_ptr[WIDTH * 6];
        out_ptr[WIDTH * 7] = out7 ^ in_ptr[WIDTH * 7];

        in_ptr++;
        out_ptr++;
    }
}

static void gf8_muladd_A7(void *out, void *in)
{
    unsigned int i;
    uint64_t *in_ptr = (uint64_t *)in;
    uint64_t *out_ptr = (uint64_t *)out;

    for (i = 0; i < WIDTH; i++) {
        uint64_t out0, out1, out2, out3, out4, out5, out6, out7;

        uint64_t in0 = out_ptr[0];
        uint64_t in1 = out_ptr[WIDTH];
        uint64_t in2 = out_ptr[WIDTH * 2];
        uint64_t in3 = out_ptr[WIDTH * 3];
        uint64_t in4 = out_ptr[WIDTH * 4];
        uint64_t in5 = out_ptr[WIDTH * 5];
        uint64_t in6 = out_ptr[WIDTH * 6];
        uint64_t in7 = out_ptr[WIDTH * 7];

        out2 = in0 ^ in2;
        out3 = in5 ^ in7;
        out7 = out2 ^ in4 ^ in6;
        out6 = out3 ^ in1 ^ in3;
        out1 = out7 ^ in1;
        out5 = out7 ^ in7;
        out0 = out6 ^ in0;
        out4 = out6 ^ in6;

        out_ptr[0] = out0 ^ in_ptr[0];
        out_ptr[WIDTH] = out1 ^ in_ptr[WIDTH];
        out_ptr[WIDTH * 2] = out2 ^ in_ptr[WIDTH * 2];
        out_ptr[WIDTH * 3] = out3 ^ in_ptr[WIDTH * 3];
        out_ptr[WIDTH * 4] = out4 ^ in_ptr[WIDTH * 4];
        out_ptr[WIDTH * 5] = out5 ^ in_ptr[WIDTH * 5];
        out_ptr[WIDTH * 6] = out6 ^ in_ptr[WIDTH * 6];
        out_ptr[WIDTH * 7] = out7 ^ in_ptr[WIDTH * 7];

        in_ptr++;
        out_ptr++;
    }
}

static void gf8_muladd_A8(void *out, void *in)
{
    unsigned int i;
    uint64_t *in_ptr = (uint64_t *)in;
    uint64_t *out_ptr = (uint64_t *)out;

    for (i = 0; i < WIDTH; i++) {
        uint64_t out0, out1, out2, out3, out4, out5, out6, out7;
        uint64_t tmp0, tmp1, tmp2;

        uint64_t in0 = out_ptr[0];
        uint64_t in1 = out_ptr[WIDTH];
        uint64_t in2 = out_ptr[WIDTH * 2];
        uint64_t in3 = out_ptr[WIDTH * 3];
        uint64_t in4 = out_ptr[WIDTH * 4];
        uint64_t in5 = out_ptr[WIDTH * 5];
        uint64_t in6 = out_ptr[WIDTH * 6];
        uint64_t in7 = out_ptr[WIDTH * 7];

        tmp0 = in2 ^ in4;
        tmp1 = in1 ^ in6;
        tmp2 = in0 ^ in2 ^ in7;
        out1 = tmp0 ^ in7;
        out4 = tmp0 ^ in6;
        out0 = tmp1 ^ in3;
        out2 = tmp1 ^ in5;
        out6 = tmp1 ^ in4;
        out7 = tmp2 ^ in5;
        out3 = tmp2 ^ out0 ^ in6;
        out5 = out7 ^ in2 ^ in3;

        out_ptr[0] = out0 ^ in_ptr[0];
        out_ptr[WIDTH] = out1 ^ in_ptr[WIDTH];
        out_ptr[WIDTH * 2] = out2 ^ in_ptr[WIDTH * 2];
        out_ptr[WIDTH * 3] = out3 ^ in_ptr[WIDTH * 3];
        out_ptr[WIDTH * 4] = out4 ^ in_ptr[WIDTH * 4];
        out_ptr[WIDTH * 5] = out5 ^ in_ptr[WIDTH * 5];
        out_ptr[WIDTH * 6] = out6 ^ in_ptr[WIDTH * 6];
        out_ptr[WIDTH * 7] = out7 ^ in_ptr[WIDTH * 7];

        in_ptr++;
        out_ptr++;
    }
}

static void gf8_muladd_A9(void *out, void *in)
{
    unsigned int i;
    uint64_t *in_ptr = (uint64_t *)in;
    uint64_t *out_ptr = (uint64_t *)out;

    for (i = 0; i < WIDTH; i++) {
        uint64_t out0, out1, out2, out3, out4, out5, out6, out7;

        uint64_t in0 = out_ptr[0];
        uint64_t in1 = out_ptr[WIDTH];
        uint64_t in2 = out_ptr[WIDTH * 2];
        uint64_t in3 = out_ptr[WIDTH * 3];
        uint64_t in4 = out_ptr[WIDTH * 4];
        uint64_t in5 = out_ptr[WIDTH * 5];
        uint64_t in6 = out_ptr[WIDTH * 6];
        uint64_t in7 = out_ptr[WIDTH * 7];

        out4 = in2 ^ in6;
        out6 = in1 ^ in4;
        out7 = in0 ^ in2 ^ in5;
        out5 = in0 ^ in3 ^ in7;
        out2 = out4 ^ in1 ^ in5;
        out1 = out6 ^ in2 ^ in7;
        out0 = out2 ^ out7 ^ in3;
        out3 = out1 ^ in0 ^ in4;

        out_ptr[0] = out0 ^ in_ptr[0];
        out_ptr[WIDTH] = out1 ^ in_ptr[WIDTH];
        out_ptr[WIDTH * 2] = out2 ^ in_ptr[WIDTH * 2];
        out_ptr[WIDTH * 3] = out3 ^ in_ptr[WIDTH * 3];
        out_ptr[WIDTH * 4] = out4 ^ in_ptr[WIDTH * 4];
        out_ptr[WIDTH * 5] = out5 ^ in_ptr[WIDTH * 5];
        out_ptr[WIDTH * 6] = out6 ^ in_ptr[WIDTH * 6];
        out_ptr[WIDTH * 7] = out7 ^ in_ptr[WIDTH * 7];

        in_ptr++;
        out_ptr++;
    }
}

static void gf8_muladd_AA(void *out, void *in)
{
    unsigned int i;
    uint64_t *in_ptr = (uint64_t *)in;
    uint64_t *out_ptr = (uint64_t *)out;

    for (i = 0; i < WIDTH; i++) {
        uint64_t out0, out1, out2, out3, out4, out5, out6, out7;
        uint64_t tmp0, tmp1, tmp2;

        uint64_t in0 = out_ptr[0];
        uint64_t in1 = out_ptr[WIDTH];
        uint64_t in2 = out_ptr[WIDTH * 2];
        uint64_t in3 = out_ptr[WIDTH * 3];
        uint64_t in4 = out_ptr[WIDTH * 4];
        uint64_t in5 = out_ptr[WIDTH * 5];
        uint64_t in6 = out_ptr[WIDTH * 6];
        uint64_t in7 = out_ptr[WIDTH * 7];

        tmp0 = in0 ^ in2;
        tmp1 = in1 ^ in3;
        tmp2 = in6 ^ in7;
        out1 = tmp0 ^ in4 ^ in7;
        out3 = tmp1 ^ in0;
        out0 = tmp1 ^ tmp2;
        out2 = tmp2 ^ in5;
        out7 = tmp0 ^ out2;
        out6 = out1 ^ out7 ^ in1;
        out5 = out0 ^ out6 ^ in0;
        out4 = out5 ^ out7 ^ in7;

        out_ptr[0] = out0 ^ in_ptr[0];
        out_ptr[WIDTH] = out1 ^ in_ptr[WIDTH];
        out_ptr[WIDTH * 2] = out2 ^ in_ptr[WIDTH * 2];
        out_ptr[WIDTH * 3] = out3 ^ in_ptr[WIDTH * 3];
        out_ptr[WIDTH * 4] = out4 ^ in_ptr[WIDTH * 4];
        out_ptr[WIDTH * 5] = out5 ^ in_ptr[WIDTH * 5];
        out_ptr[WIDTH * 6] = out6 ^ in_ptr[WIDTH * 6];
        out_ptr[WIDTH * 7] = out7 ^ in_ptr[WIDTH * 7];

        in_ptr++;
        out_ptr++;
    }
}

static void gf8_muladd_AB(void *out, void *in)
{
    unsigned int i;
    uint64_t *in_ptr = (uint64_t *)in;
    uint64_t *out_ptr = (uint64_t *)out;

    for (i = 0; i < WIDTH; i++) {
        uint64_t out0, out1, out2, out3, out4, out5, out6, out7;
        uint64_t tmp0, tmp1;

        uint64_t in0 = out_ptr[0];
        uint64_t in1 = out_ptr[WIDTH];
        uint64_t in2 = out_ptr[WIDTH * 2];
        uint64_t in3 = out_ptr[WIDTH * 3];
        uint64_t in4 = out_ptr[WIDTH * 4];
        uint64_t in5 = out_ptr[WIDTH * 5];
        uint64_t in6 = out_ptr[WIDTH * 6];
        uint64_t in7 = out_ptr[WIDTH * 7];

        out3 = in0 ^ in1;
        tmp0 = in1 ^ in4;
        tmp1 = in0 ^ in7;
        out6 = tmp0 ^ in5;
        out1 = tmp0 ^ tmp1 ^ in2;
        out5 = tmp1 ^ in3 ^ in4;
        out0 = tmp0 ^ out5 ^ in6;
        out4 = out0 ^ out3 ^ in2;
        out2 = out4 ^ in3 ^ in5;
        out7 = tmp1 ^ out2;

        out_ptr[0] = out0 ^ in_ptr[0];
        out_ptr[WIDTH] = out1 ^ in_ptr[WIDTH];
        out_ptr[WIDTH * 2] = out2 ^ in_ptr[WIDTH * 2];
        out_ptr[WIDTH * 3] = out3 ^ in_ptr[WIDTH * 3];
        out_ptr[WIDTH * 4] = out4 ^ in_ptr[WIDTH * 4];
        out_ptr[WIDTH * 5] = out5 ^ in_ptr[WIDTH * 5];
        out_ptr[WIDTH * 6] = out6 ^ in_ptr[WIDTH * 6];
        out_ptr[WIDTH * 7] = out7 ^ in_ptr[WIDTH * 7];

        in_ptr++;
        out_ptr++;
    }
}

static void gf8_muladd_AC(void *out, void *in)
{
    unsigned int i;
    uint64_t *in_ptr = (uint64_t *)in;
    uint64_t *out_ptr = (uint64_t *)out;

    for (i = 0; i < WIDTH; i++) {
        uint64_t out0, out1, out2, out3, out4, out5, out6, out7;
        uint64_t tmp0;

        uint64_t in0 = out_ptr[0];
        uint64_t in1 = out_ptr[WIDTH];
        uint64_t in2 = out_ptr[WIDTH * 2];
        uint64_t in3 = out_ptr[WIDTH * 3];
        uint64_t in4 = out_ptr[WIDTH * 4];
        uint64_t in5 = out_ptr[WIDTH * 5];
        uint64_t in6 = out_ptr[WIDTH * 6];
        uint64_t in7 = out_ptr[WIDTH * 7];

        out0 = in1 ^ in3;
        out1 = in2 ^ in4;
        tmp0 = in0 ^ in2;
        out4 = in4 ^ in7;
        out5 = in0 ^ in5;
        out6 = in1 ^ in6;
        out7 = tmp0 ^ in7;
        out3 = tmp0 ^ in3 ^ in6;
        out2 = out5 ^ in1;

        out_ptr[0] = out0 ^ in_ptr[0];
        out_ptr[WIDTH] = out1 ^ in_ptr[WIDTH];
        out_ptr[WIDTH * 2] = out2 ^ in_ptr[WIDTH * 2];
        out_ptr[WIDTH * 3] = out3 ^ in_ptr[WIDTH * 3];
        out_ptr[WIDTH * 4] = out4 ^ in_ptr[WIDTH * 4];
        out_ptr[WIDTH * 5] = out5 ^ in_ptr[WIDTH * 5];
        out_ptr[WIDTH * 6] = out6 ^ in_ptr[WIDTH * 6];
        out_ptr[WIDTH * 7] = out7 ^ in_ptr[WIDTH * 7];

        in_ptr++;
        out_ptr++;
    }
}

static void gf8_muladd_AD(void *out, void *in)
{
    unsigned int i;
    uint64_t *in_ptr = (uint64_t *)in;
    uint64_t *out_ptr = (uint64_t *)out;

    for (i = 0; i < WIDTH; i++) {
        uint64_t out0, out1, out2, out3, out4, out5, out6, out7;

        uint64_t in0 = out_ptr[0];
        uint64_t in1 = out_ptr[WIDTH];
        uint64_t in2 = out_ptr[WIDTH * 2];
        uint64_t in3 = out_ptr[WIDTH * 3];
        uint64_t in4 = out_ptr[WIDTH * 4];
        uint64_t in5 = out_ptr[WIDTH * 5];
        uint64_t in6 = out_ptr[WIDTH * 6];
        uint64_t in7 = out_ptr[WIDTH * 7];

        out4 = in7;
        out5 = in0;
        out6 = in1;
        out7 = in0 ^ in2;
        out0 = in0 ^ in1 ^ in3;
        out2 = out7 ^ in1 ^ in5;
        out1 = in1 ^ in2 ^ in4;
        out3 = out7 ^ in6;

        out_ptr[0] = out0 ^ in_ptr[0];
        out_ptr[WIDTH] = out1 ^ in_ptr[WIDTH];
        out_ptr[WIDTH * 2] = out2 ^ in_ptr[WIDTH * 2];
        out_ptr[WIDTH * 3] = out3 ^ in_ptr[WIDTH * 3];
        out_ptr[WIDTH * 4] = out4 ^ in_ptr[WIDTH * 4];
        out_ptr[WIDTH * 5] = out5 ^ in_ptr[WIDTH * 5];
        out_ptr[WIDTH * 6] = out6 ^ in_ptr[WIDTH * 6];
        out_ptr[WIDTH * 7] = out7 ^ in_ptr[WIDTH * 7];

        in_ptr++;
        out_ptr++;
    }
}

static void gf8_muladd_AE(void *out, void *in)
{
    unsigned int i;
    uint64_t *in_ptr = (uint64_t *)in;
    uint64_t *out_ptr = (uint64_t *)out;

    for (i = 0; i < WIDTH; i++) {
        uint64_t out0, out1, out2, out3, out4, out5, out6, out7;
        uint64_t tmp0, tmp1, tmp2;

        uint64_t in0 = out_ptr[0];
        uint64_t in1 = out_ptr[WIDTH];
        uint64_t in2 = out_ptr[WIDTH * 2];
        uint64_t in3 = out_ptr[WIDTH * 3];
        uint64_t in4 = out_ptr[WIDTH * 4];
        uint64_t in5 = out_ptr[WIDTH * 5];
        uint64_t in6 = out_ptr[WIDTH * 6];
        uint64_t in7 = out_ptr[WIDTH * 7];

        out4 = in3 ^ in4;
        tmp0 = in0 ^ in4;
        tmp1 = in0 ^ in7;
        out0 = in1 ^ in3 ^ in7;
        out1 = tmp0 ^ in2;
        out5 = tmp0 ^ in5;
        tmp2 = tmp1 ^ in6;
        out2 = tmp1 ^ in5;
        out3 = tmp2 ^ in3;
        out7 = tmp2 ^ in2;
        out6 = tmp2 ^ out2 ^ in1;

        out_ptr[0] = out0 ^ in_ptr[0];
        out_ptr[WIDTH] = out1 ^ in_ptr[WIDTH];
        out_ptr[WIDTH * 2] = out2 ^ in_ptr[WIDTH * 2];
        out_ptr[WIDTH * 3] = out3 ^ in_ptr[WIDTH * 3];
        out_ptr[WIDTH * 4] = out4 ^ in_ptr[WIDTH * 4];
        out_ptr[WIDTH * 5] = out5 ^ in_ptr[WIDTH * 5];
        out_ptr[WIDTH * 6] = out6 ^ in_ptr[WIDTH * 6];
        out_ptr[WIDTH * 7] = out7 ^ in_ptr[WIDTH * 7];

        in_ptr++;
        out_ptr++;
    }
}

static void gf8_muladd_AF(void *out, void *in)
{
    unsigned int i;
    uint64_t *in_ptr = (uint64_t *)in;
    uint64_t *out_ptr = (uint64_t *)out;

    for (i = 0; i < WIDTH; i++) {
        uint64_t out0, out1, out2, out3, out4, out5, out6, out7;
        uint64_t tmp0;

        uint64_t in0 = out_ptr[0];
        uint64_t in1 = out_ptr[WIDTH];
        uint64_t in2 = out_ptr[WIDTH * 2];
        uint64_t in3 = out_ptr[WIDTH * 3];
        uint64_t in4 = out_ptr[WIDTH * 4];
        uint64_t in5 = out_ptr[WIDTH * 5];
        uint64_t in6 = out_ptr[WIDTH * 6];
        uint64_t in7 = out_ptr[WIDTH * 7];

        out4 = in3;
        tmp0 = in0 ^ in7;
        out5 = in0 ^ in4;
        out6 = in1 ^ in5;
        out7 = in0 ^ in2 ^ in6;
        out0 = tmp0 ^ in1 ^ in3;
        out3 = tmp0 ^ in6;
        out2 = tmp0 ^ in2 ^ in5;
        out1 = out5 ^ in1 ^ in2;

        out_ptr[0] = out0 ^ in_ptr[0];
        out_ptr[WIDTH] = out1 ^ in_ptr[WIDTH];
        out_ptr[WIDTH * 2] = out2 ^ in_ptr[WIDTH * 2];
        out_ptr[WIDTH * 3] = out3 ^ in_ptr[WIDTH * 3];
        out_ptr[WIDTH * 4] = out4 ^ in_ptr[WIDTH * 4];
        out_ptr[WIDTH * 5] = out5 ^ in_ptr[WIDTH * 5];
        out_ptr[WIDTH * 6] = out6 ^ in_ptr[WIDTH * 6];
        out_ptr[WIDTH * 7] = out7 ^ in_ptr[WIDTH * 7];

        in_ptr++;
        out_ptr++;
    }
}

static void gf8_muladd_B0(void *out, void *in)
{
    unsigned int i;
    uint64_t *in_ptr = (uint64_t *)in;
    uint64_t *out_ptr = (uint64_t *)out;

    for (i = 0; i < WIDTH; i++) {
        uint64_t out0, out1, out2, out3, out4, out5, out6, out7;
        uint64_t tmp0, tmp1, tmp2, tmp3;

        uint64_t in0 = out_ptr[0];
        uint64_t in1 = out_ptr[WIDTH];
        uint64_t in2 = out_ptr[WIDTH * 2];
        uint64_t in3 = out_ptr[WIDTH * 3];
        uint64_t in4 = out_ptr[WIDTH * 4];
        uint64_t in5 = out_ptr[WIDTH * 5];
        uint64_t in6 = out_ptr[WIDTH * 6];
        uint64_t in7 = out_ptr[WIDTH * 7];

        tmp0 = in1 ^ in4;
        tmp1 = in3 ^ in6;
        out2 = tmp0 ^ in7;
        tmp2 = tmp0 ^ tmp1;
        out0 = tmp2 ^ in5;
        out3 = tmp2 ^ in2;
        out6 = out3 ^ in6;
        tmp3 = out6 ^ in0 ^ in1;
        out7 = tmp3 ^ in5;
        out5 = tmp3 ^ out2;
        out1 = out0 ^ out5 ^ in0;
        out4 = tmp1 ^ out5;

        out_ptr[0] = out0 ^ in_ptr[0];
        out_ptr[WIDTH] = out1 ^ in_ptr[WIDTH];
        out_ptr[WIDTH * 2] = out2 ^ in_ptr[WIDTH * 2];
        out_ptr[WIDTH * 3] = out3 ^ in_ptr[WIDTH * 3];
        out_ptr[WIDTH * 4] = out4 ^ in_ptr[WIDTH * 4];
        out_ptr[WIDTH * 5] = out5 ^ in_ptr[WIDTH * 5];
        out_ptr[WIDTH * 6] = out6 ^ in_ptr[WIDTH * 6];
        out_ptr[WIDTH * 7] = out7 ^ in_ptr[WIDTH * 7];

        in_ptr++;
        out_ptr++;
    }
}

static void gf8_muladd_B1(void *out, void *in)
{
    unsigned int i;
    uint64_t *in_ptr = (uint64_t *)in;
    uint64_t *out_ptr = (uint64_t *)out;

    for (i = 0; i < WIDTH; i++) {
        uint64_t out0, out1, out2, out3, out4, out5, out6, out7;
        uint64_t tmp0, tmp1;

        uint64_t in0 = out_ptr[0];
        uint64_t in1 = out_ptr[WIDTH];
        uint64_t in2 = out_ptr[WIDTH * 2];
        uint64_t in3 = out_ptr[WIDTH * 3];
        uint64_t in4 = out_ptr[WIDTH * 4];
        uint64_t in5 = out_ptr[WIDTH * 5];
        uint64_t in6 = out_ptr[WIDTH * 6];
        uint64_t in7 = out_ptr[WIDTH * 7];

        tmp0 = in1 ^ in4;
        out2 = tmp0 ^ in2 ^ in7;
        tmp1 = out2 ^ in6;
        out1 = tmp1 ^ in5;
        out3 = tmp1 ^ in7;
        out4 = tmp1 ^ in0;
        out6 = out3 ^ in3;
        out0 = out6 ^ in0 ^ in2 ^ in5;
        out5 = tmp1 ^ out0 ^ in1;
        out7 = tmp0 ^ out5;

        out_ptr[0] = out0 ^ in_ptr[0];
        out_ptr[WIDTH] = out1 ^ in_ptr[WIDTH];
        out_ptr[WIDTH * 2] = out2 ^ in_ptr[WIDTH * 2];
        out_ptr[WIDTH * 3] = out3 ^ in_ptr[WIDTH * 3];
        out_ptr[WIDTH * 4] = out4 ^ in_ptr[WIDTH * 4];
        out_ptr[WIDTH * 5] = out5 ^ in_ptr[WIDTH * 5];
        out_ptr[WIDTH * 6] = out6 ^ in_ptr[WIDTH * 6];
        out_ptr[WIDTH * 7] = out7 ^ in_ptr[WIDTH * 7];

        in_ptr++;
        out_ptr++;
    }
}

static void gf8_muladd_B2(void *out, void *in)
{
    unsigned int i;
    uint64_t *in_ptr = (uint64_t *)in;
    uint64_t *out_ptr = (uint64_t *)out;

    for (i = 0; i < WIDTH; i++) {
        uint64_t out0, out1, out2, out3, out4, out5, out6, out7;
        uint64_t tmp0, tmp1, tmp2, tmp3, tmp4;

        uint64_t in0 = out_ptr[0];
        uint64_t in1 = out_ptr[WIDTH];
        uint64_t in2 = out_ptr[WIDTH * 2];
        uint64_t in3 = out_ptr[WIDTH * 3];
        uint64_t in4 = out_ptr[WIDTH * 4];
        uint64_t in5 = out_ptr[WIDTH * 5];
        uint64_t in6 = out_ptr[WIDTH * 6];
        uint64_t in7 = out_ptr[WIDTH * 7];

        out2 = in4;
        tmp0 = in4 ^ in7;
        tmp1 = in1 ^ in3 ^ in6;
        out3 = tmp0 ^ tmp1;
        tmp2 = tmp1 ^ in0;
        out0 = out3 ^ in5;
        out4 = tmp2 ^ in2;
        tmp3 = out4 ^ in6;
        out5 = tmp0 ^ tmp3;
        out1 = tmp3 ^ out0;
        tmp4 = out1 ^ in7;
        out7 = tmp4 ^ in3;
        out6 = tmp2 ^ tmp4;

        out_ptr[0] = out0 ^ in_ptr[0];
        out_ptr[WIDTH] = out1 ^ in_ptr[WIDTH];
        out_ptr[WIDTH * 2] = out2 ^ in_ptr[WIDTH * 2];
        out_ptr[WIDTH * 3] = out3 ^ in_ptr[WIDTH * 3];
        out_ptr[WIDTH * 4] = out4 ^ in_ptr[WIDTH * 4];
        out_ptr[WIDTH * 5] = out5 ^ in_ptr[WIDTH * 5];
        out_ptr[WIDTH * 6] = out6 ^ in_ptr[WIDTH * 6];
        out_ptr[WIDTH * 7] = out7 ^ in_ptr[WIDTH * 7];

        in_ptr++;
        out_ptr++;
    }
}

static void gf8_muladd_B3(void *out, void *in)
{
    unsigned int i;
    uint64_t *in_ptr = (uint64_t *)in;
    uint64_t *out_ptr = (uint64_t *)out;

    for (i = 0; i < WIDTH; i++) {
        uint64_t out0, out1, out2, out3, out4, out5, out6, out7;
        uint64_t tmp0, tmp1, tmp2;

        uint64_t in0 = out_ptr[0];
        uint64_t in1 = out_ptr[WIDTH];
        uint64_t in2 = out_ptr[WIDTH * 2];
        uint64_t in3 = out_ptr[WIDTH * 3];
        uint64_t in4 = out_ptr[WIDTH * 4];
        uint64_t in5 = out_ptr[WIDTH * 5];
        uint64_t in6 = out_ptr[WIDTH * 6];
        uint64_t in7 = out_ptr[WIDTH * 7];

        out2 = in2 ^ in4;
        tmp0 = in0 ^ in5;
        tmp1 = in1 ^ in6;
        out3 = tmp1 ^ in4 ^ in7;
        tmp2 = tmp0 ^ out3;
        out0 = tmp2 ^ in3;
        out1 = tmp2 ^ in2;
        out5 = out0 ^ in2 ^ in6;
        out7 = tmp1 ^ out5;
        out4 = out7 ^ in1 ^ in5 ^ in7;
        out6 = tmp0 ^ out4;

        out_ptr[0] = out0 ^ in_ptr[0];
        out_ptr[WIDTH] = out1 ^ in_ptr[WIDTH];
        out_ptr[WIDTH * 2] = out2 ^ in_ptr[WIDTH * 2];
        out_ptr[WIDTH * 3] = out3 ^ in_ptr[WIDTH * 3];
        out_ptr[WIDTH * 4] = out4 ^ in_ptr[WIDTH * 4];
        out_ptr[WIDTH * 5] = out5 ^ in_ptr[WIDTH * 5];
        out_ptr[WIDTH * 6] = out6 ^ in_ptr[WIDTH * 6];
        out_ptr[WIDTH * 7] = out7 ^ in_ptr[WIDTH * 7];

        in_ptr++;
        out_ptr++;
    }
}

static void gf8_muladd_B4(void *out, void *in)
{
    unsigned int i;
    uint64_t *in_ptr = (uint64_t *)in;
    uint64_t *out_ptr = (uint64_t *)out;

    for (i = 0; i < WIDTH; i++) {
        uint64_t out0, out1, out2, out3, out4, out5, out6, out7;
        uint64_t tmp0;

        uint64_t in0 = out_ptr[0];
        uint64_t in1 = out_ptr[WIDTH];
        uint64_t in2 = out_ptr[WIDTH * 2];
        uint64_t in3 = out_ptr[WIDTH * 3];
        uint64_t in4 = out_ptr[WIDTH * 4];
        uint64_t in5 = out_ptr[WIDTH * 5];
        uint64_t in6 = out_ptr[WIDTH * 6];
        uint64_t in7 = out_ptr[WIDTH * 7];

        out4 = in0 ^ in1;
        out5 = out4 ^ in2;
        tmp0 = out4 ^ in4;
        out6 = out5 ^ in0 ^ in3;
        out7 = tmp0 ^ out6;
        out2 = tmp0 ^ in6 ^ in7;
        out3 = out7 ^ in0 ^ in7;
        out0 = out5 ^ out7 ^ in5;
        out1 = out0 ^ out6 ^ in6;

        out_ptr[0] = out0 ^ in_ptr[0];
        out_ptr[WIDTH] = out1 ^ in_ptr[WIDTH];
        out_ptr[WIDTH * 2] = out2 ^ in_ptr[WIDTH * 2];
        out_ptr[WIDTH * 3] = out3 ^ in_ptr[WIDTH * 3];
        out_ptr[WIDTH * 4] = out4 ^ in_ptr[WIDTH * 4];
        out_ptr[WIDTH * 5] = out5 ^ in_ptr[WIDTH * 5];
        out_ptr[WIDTH * 6] = out6 ^ in_ptr[WIDTH * 6];
        out_ptr[WIDTH * 7] = out7 ^ in_ptr[WIDTH * 7];

        in_ptr++;
        out_ptr++;
    }
}

static void gf8_muladd_B5(void *out, void *in)
{
    unsigned int i;
    uint64_t *in_ptr = (uint64_t *)in;
    uint64_t *out_ptr = (uint64_t *)out;

    for (i = 0; i < WIDTH; i++) {
        uint64_t out0, out1, out2, out3, out4, out5, out6, out7;
        uint64_t tmp0, tmp1, tmp2;

        uint64_t in0 = out_ptr[0];
        uint64_t in1 = out_ptr[WIDTH];
        uint64_t in2 = out_ptr[WIDTH * 2];
        uint64_t in3 = out_ptr[WIDTH * 3];
        uint64_t in4 = out_ptr[WIDTH * 4];
        uint64_t in5 = out_ptr[WIDTH * 5];
        uint64_t in6 = out_ptr[WIDTH * 6];
        uint64_t in7 = out_ptr[WIDTH * 7];

        tmp0 = in0 ^ in1;
        tmp1 = in2 ^ in4;
        out4 = tmp0 ^ in4;
        out3 = tmp1 ^ in7;
        tmp2 = out4 ^ in5;
        out7 = out3 ^ in0 ^ in3;
        out0 = tmp2 ^ in3;
        out2 = tmp0 ^ out3 ^ in6;
        out5 = tmp1 ^ tmp2;
        out6 = out2 ^ out7 ^ in2;
        out1 = tmp0 ^ out0 ^ out6;

        out_ptr[0] = out0 ^ in_ptr[0];
        out_ptr[WIDTH] = out1 ^ in_ptr[WIDTH];
        out_ptr[WIDTH * 2] = out2 ^ in_ptr[WIDTH * 2];
        out_ptr[WIDTH * 3] = out3 ^ in_ptr[WIDTH * 3];
        out_ptr[WIDTH * 4] = out4 ^ in_ptr[WIDTH * 4];
        out_ptr[WIDTH * 5] = out5 ^ in_ptr[WIDTH * 5];
        out_ptr[WIDTH * 6] = out6 ^ in_ptr[WIDTH * 6];
        out_ptr[WIDTH * 7] = out7 ^ in_ptr[WIDTH * 7];

        in_ptr++;
        out_ptr++;
    }
}

static void gf8_muladd_B6(void *out, void *in)
{
    unsigned int i;
    uint64_t *in_ptr = (uint64_t *)in;
    uint64_t *out_ptr = (uint64_t *)out;

    for (i = 0; i < WIDTH; i++) {
        uint64_t out0, out1, out2, out3, out4, out5, out6, out7;
        uint64_t tmp0, tmp1, tmp2, tmp3;

        uint64_t in0 = out_ptr[0];
        uint64_t in1 = out_ptr[WIDTH];
        uint64_t in2 = out_ptr[WIDTH * 2];
        uint64_t in3 = out_ptr[WIDTH * 3];
        uint64_t in4 = out_ptr[WIDTH * 4];
        uint64_t in5 = out_ptr[WIDTH * 5];
        uint64_t in6 = out_ptr[WIDTH * 6];
        uint64_t in7 = out_ptr[WIDTH * 7];

        out3 = in3 ^ in4;
        tmp0 = in1 ^ in2;
        tmp1 = in0 ^ in4;
        tmp2 = in3 ^ in5;
        tmp3 = out3 ^ in1 ^ in7;
        out5 = tmp0 ^ tmp1;
        out6 = tmp0 ^ tmp2;
        out2 = tmp1 ^ in6;
        out4 = tmp1 ^ tmp3;
        out0 = tmp3 ^ in5;
        out1 = out2 ^ in2 ^ in5;
        out7 = tmp2 ^ out1;

        out_ptr[0] = out0 ^ in_ptr[0];
        out_ptr[WIDTH] = out1 ^ in_ptr[WIDTH];
        out_ptr[WIDTH * 2] = out2 ^ in_ptr[WIDTH * 2];
        out_ptr[WIDTH * 3] = out3 ^ in_ptr[WIDTH * 3];
        out_ptr[WIDTH * 4] = out4 ^ in_ptr[WIDTH * 4];
        out_ptr[WIDTH * 5] = out5 ^ in_ptr[WIDTH * 5];
        out_ptr[WIDTH * 6] = out6 ^ in_ptr[WIDTH * 6];
        out_ptr[WIDTH * 7] = out7 ^ in_ptr[WIDTH * 7];

        in_ptr++;
        out_ptr++;
    }
}

static void gf8_muladd_B7(void *out, void *in)
{
    unsigned int i;
    uint64_t *in_ptr = (uint64_t *)in;
    uint64_t *out_ptr = (uint64_t *)out;

    for (i = 0; i < WIDTH; i++) {
        uint64_t out0, out1, out2, out3, out4, out5, out6, out7;
        uint64_t tmp0, tmp1;

        uint64_t in0 = out_ptr[0];
        uint64_t in1 = out_ptr[WIDTH];
        uint64_t in2 = out_ptr[WIDTH * 2];
        uint64_t in3 = out_ptr[WIDTH * 3];
        uint64_t in4 = out_ptr[WIDTH * 4];
        uint64_t in5 = out_ptr[WIDTH * 5];
        uint64_t in6 = out_ptr[WIDTH * 6];
        uint64_t in7 = out_ptr[WIDTH * 7];

        out3 = in4;
        tmp0 = in0 ^ in4;
        out2 = tmp0 ^ in2 ^ in6;
        tmp1 = out2 ^ in7;
        out1 = out2 ^ in1 ^ in5;
        out7 = tmp1 ^ in3;
        out5 = out1 ^ in6;
        out6 = tmp0 ^ out1 ^ in3;
        out0 = tmp1 ^ out6;
        out4 = out0 ^ in5;

        out_ptr[0] = out0 ^ in_ptr[0];
        out_ptr[WIDTH] = out1 ^ in_ptr[WIDTH];
        out_ptr[WIDTH * 2] = out2 ^ in_ptr[WIDTH * 2];
        out_ptr[WIDTH * 3] = out3 ^ in_ptr[WIDTH * 3];
        out_ptr[WIDTH * 4] = out4 ^ in_ptr[WIDTH * 4];
        out_ptr[WIDTH * 5] = out5 ^ in_ptr[WIDTH * 5];
        out_ptr[WIDTH * 6] = out6 ^ in_ptr[WIDTH * 6];
        out_ptr[WIDTH * 7] = out7 ^ in_ptr[WIDTH * 7];

        in_ptr++;
        out_ptr++;
    }
}

static void gf8_muladd_B8(void *out, void *in)
{
    unsigned int i;
    uint64_t *in_ptr = (uint64_t *)in;
    uint64_t *out_ptr = (uint64_t *)out;

    for (i = 0; i < WIDTH; i++) {
        uint64_t out0, out1, out2, out3, out4, out5, out6, out7;
        uint64_t tmp0, tmp1, tmp2;

        uint64_t in0 = out_ptr[0];
        uint64_t in1 = out_ptr[WIDTH];
        uint64_t in2 = out_ptr[WIDTH * 2];
        uint64_t in3 = out_ptr[WIDTH * 3];
        uint64_t in4 = out_ptr[WIDTH * 4];
        uint64_t in5 = out_ptr[WIDTH * 5];
        uint64_t in6 = out_ptr[WIDTH * 6];
        uint64_t in7 = out_ptr[WIDTH * 7];

        tmp0 = in1 ^ in4;
        tmp1 = in2 ^ in5;
        out2 = tmp0 ^ in5;
        out4 = tmp1 ^ in0;
        tmp2 = tmp1 ^ in7;
        out6 = tmp2 ^ out2;
        out7 = out4 ^ in3;
        out1 = tmp2 ^ in4;
        out3 = tmp0 ^ out7;
        out0 = out3 ^ out4 ^ in6;
        out5 = out0 ^ in0 ^ in4;

        out_ptr[0] = out0 ^ in_ptr[0];
        out_ptr[WIDTH] = out1 ^ in_ptr[WIDTH];
        out_ptr[WIDTH * 2] = out2 ^ in_ptr[WIDTH * 2];
        out_ptr[WIDTH * 3] = out3 ^ in_ptr[WIDTH * 3];
        out_ptr[WIDTH * 4] = out4 ^ in_ptr[WIDTH * 4];
        out_ptr[WIDTH * 5] = out5 ^ in_ptr[WIDTH * 5];
        out_ptr[WIDTH * 6] = out6 ^ in_ptr[WIDTH * 6];
        out_ptr[WIDTH * 7] = out7 ^ in_ptr[WIDTH * 7];

        in_ptr++;
        out_ptr++;
    }
}

static void gf8_muladd_B9(void *out, void *in)
{
    unsigned int i;
    uint64_t *in_ptr = (uint64_t *)in;
    uint64_t *out_ptr = (uint64_t *)out;

    for (i = 0; i < WIDTH; i++) {
        uint64_t out0, out1, out2, out3, out4, out5, out6, out7;
        uint64_t tmp0, tmp1, tmp2;

        uint64_t in0 = out_ptr[0];
        uint64_t in1 = out_ptr[WIDTH];
        uint64_t in2 = out_ptr[WIDTH * 2];
        uint64_t in3 = out_ptr[WIDTH * 3];
        uint64_t in4 = out_ptr[WIDTH * 4];
        uint64_t in5 = out_ptr[WIDTH * 5];
        uint64_t in6 = out_ptr[WIDTH * 6];
        uint64_t in7 = out_ptr[WIDTH * 7];

        tmp0 = in0 ^ in2;
        tmp1 = in4 ^ in5;
        out4 = tmp0 ^ tmp1;
        tmp2 = tmp0 ^ in3 ^ in7;
        out3 = out4 ^ in1;
        out7 = tmp2 ^ in5;
        out2 = out3 ^ in0;
        out1 = out2 ^ in7;
        out6 = out1 ^ in5 ^ in6;
        out0 = tmp2 ^ out6;
        out5 = tmp1 ^ out0;

        out_ptr[0] = out0 ^ in_ptr[0];
        out_ptr[WIDTH] = out1 ^ in_ptr[WIDTH];
        out_ptr[WIDTH * 2] = out2 ^ in_ptr[WIDTH * 2];
        out_ptr[WIDTH * 3] = out3 ^ in_ptr[WIDTH * 3];
        out_ptr[WIDTH * 4] = out4 ^ in_ptr[WIDTH * 4];
        out_ptr[WIDTH * 5] = out5 ^ in_ptr[WIDTH * 5];
        out_ptr[WIDTH * 6] = out6 ^ in_ptr[WIDTH * 6];
        out_ptr[WIDTH * 7] = out7 ^ in_ptr[WIDTH * 7];

        in_ptr++;
        out_ptr++;
    }
}

static void gf8_muladd_BA(void *out, void *in)
{
    unsigned int i;
    uint64_t *in_ptr = (uint64_t *)in;
    uint64_t *out_ptr = (uint64_t *)out;

    for (i = 0; i < WIDTH; i++) {
        uint64_t out0, out1, out2, out3, out4, out5, out6, out7;
        uint64_t tmp0, tmp1, tmp2;

        uint64_t in0 = out_ptr[0];
        uint64_t in1 = out_ptr[WIDTH];
        uint64_t in2 = out_ptr[WIDTH * 2];
        uint64_t in3 = out_ptr[WIDTH * 3];
        uint64_t in4 = out_ptr[WIDTH * 4];
        uint64_t in5 = out_ptr[WIDTH * 5];
        uint64_t in6 = out_ptr[WIDTH * 6];
        uint64_t in7 = out_ptr[WIDTH * 7];

        tmp0 = in5 ^ in7;
        out2 = tmp0 ^ in4;
        tmp1 = out2 ^ in2;
        out1 = tmp1 ^ in0;
        out6 = tmp1 ^ in1;
        out4 = out1 ^ in3 ^ in4;
        tmp2 = out4 ^ out6;
        out7 = out4 ^ in6 ^ in7;
        out5 = tmp2 ^ in6;
        out3 = tmp0 ^ tmp2;
        out0 = out6 ^ out7 ^ in0;

        out_ptr[0] = out0 ^ in_ptr[0];
        out_ptr[WIDTH] = out1 ^ in_ptr[WIDTH];
        out_ptr[WIDTH * 2] = out2 ^ in_ptr[WIDTH * 2];
        out_ptr[WIDTH * 3] = out3 ^ in_ptr[WIDTH * 3];
        out_ptr[WIDTH * 4] = out4 ^ in_ptr[WIDTH * 4];
        out_ptr[WIDTH * 5] = out5 ^ in_ptr[WIDTH * 5];
        out_ptr[WIDTH * 6] = out6 ^ in_ptr[WIDTH * 6];
        out_ptr[WIDTH * 7] = out7 ^ in_ptr[WIDTH * 7];

        in_ptr++;
        out_ptr++;
    }
}

static void gf8_muladd_BB(void *out, void *in)
{
    unsigned int i;
    uint64_t *in_ptr = (uint64_t *)in;
    uint64_t *out_ptr = (uint64_t *)out;

    for (i = 0; i < WIDTH; i++) {
        uint64_t out0, out1, out2, out3, out4, out5, out6, out7;
        uint64_t tmp0, tmp1;

        uint64_t in0 = out_ptr[0];
        uint64_t in1 = out_ptr[WIDTH];
        uint64_t in2 = out_ptr[WIDTH * 2];
        uint64_t in3 = out_ptr[WIDTH * 3];
        uint64_t in4 = out_ptr[WIDTH * 4];
        uint64_t in5 = out_ptr[WIDTH * 5];
        uint64_t in6 = out_ptr[WIDTH * 6];
        uint64_t in7 = out_ptr[WIDTH * 7];

        out2 = in2 ^ in4 ^ in5 ^ in7;
        tmp0 = out2 ^ in1;
        out4 = out2 ^ in0 ^ in3;
        out1 = tmp0 ^ in0;
        out6 = tmp0 ^ in6;
        out3 = out1 ^ in2;
        tmp1 = out4 ^ out6 ^ in4;
        out0 = tmp1 ^ in7;
        out5 = tmp1 ^ in5;
        out7 = tmp0 ^ tmp1;

        out_ptr[0] = out0 ^ in_ptr[0];
        out_ptr[WIDTH] = out1 ^ in_ptr[WIDTH];
        out_ptr[WIDTH * 2] = out2 ^ in_ptr[WIDTH * 2];
        out_ptr[WIDTH * 3] = out3 ^ in_ptr[WIDTH * 3];
        out_ptr[WIDTH * 4] = out4 ^ in_ptr[WIDTH * 4];
        out_ptr[WIDTH * 5] = out5 ^ in_ptr[WIDTH * 5];
        out_ptr[WIDTH * 6] = out6 ^ in_ptr[WIDTH * 6];
        out_ptr[WIDTH * 7] = out7 ^ in_ptr[WIDTH * 7];

        in_ptr++;
        out_ptr++;
    }
}

static void gf8_muladd_BC(void *out, void *in)
{
    unsigned int i;
    uint64_t *in_ptr = (uint64_t *)in;
    uint64_t *out_ptr = (uint64_t *)out;

    for (i = 0; i < WIDTH; i++) {
        uint64_t out0, out1, out2, out3, out4, out5, out6, out7;
        uint64_t tmp0, tmp1, tmp2;

        uint64_t in0 = out_ptr[0];
        uint64_t in1 = out_ptr[WIDTH];
        uint64_t in2 = out_ptr[WIDTH * 2];
        uint64_t in3 = out_ptr[WIDTH * 3];
        uint64_t in4 = out_ptr[WIDTH * 4];
        uint64_t in5 = out_ptr[WIDTH * 5];
        uint64_t in6 = out_ptr[WIDTH * 6];
        uint64_t in7 = out_ptr[WIDTH * 7];

        tmp0 = in0 ^ in2;
        tmp1 = in2 ^ in4;
        out0 = in1 ^ in3 ^ in4;
        out6 = in1 ^ in2 ^ in7;
        out7 = tmp0 ^ in3;
        out5 = tmp0 ^ out6 ^ in6;
        out1 = tmp1 ^ in5;
        tmp2 = out1 ^ out5 ^ in1;
        out3 = tmp2 ^ in3;
        out4 = tmp1 ^ tmp2;
        out2 = tmp2 ^ out6;

        out_ptr[0] = out0 ^ in_ptr[0];
        out_ptr[WIDTH] = out1 ^ in_ptr[WIDTH];
        out_ptr[WIDTH * 2] = out2 ^ in_ptr[WIDTH * 2];
        out_ptr[WIDTH * 3] = out3 ^ in_ptr[WIDTH * 3];
        out_ptr[WIDTH * 4] = out4 ^ in_ptr[WIDTH * 4];
        out_ptr[WIDTH * 5] = out5 ^ in_ptr[WIDTH * 5];
        out_ptr[WIDTH * 6] = out6 ^ in_ptr[WIDTH * 6];
        out_ptr[WIDTH * 7] = out7 ^ in_ptr[WIDTH * 7];

        in_ptr++;
        out_ptr++;
    }
}

static void gf8_muladd_BD(void *out, void *in)
{
    unsigned int i;
    uint64_t *in_ptr = (uint64_t *)in;
    uint64_t *out_ptr = (uint64_t *)out;

    for (i = 0; i < WIDTH; i++) {
        uint64_t out0, out1, out2, out3, out4, out5, out6, out7;
        uint64_t tmp0, tmp1, tmp2;

        uint64_t in0 = out_ptr[0];
        uint64_t in1 = out_ptr[WIDTH];
        uint64_t in2 = out_ptr[WIDTH * 2];
        uint64_t in3 = out_ptr[WIDTH * 3];
        uint64_t in4 = out_ptr[WIDTH * 4];
        uint64_t in5 = out_ptr[WIDTH * 5];
        uint64_t in6 = out_ptr[WIDTH * 6];
        uint64_t in7 = out_ptr[WIDTH * 7];

        tmp0 = in0 ^ in3;
        tmp1 = in1 ^ in4;
        out0 = tmp0 ^ tmp1;
        out7 = tmp0 ^ in2 ^ in7;
        out1 = tmp1 ^ in2 ^ in5;
        tmp2 = out1 ^ in0;
        out2 = tmp2 ^ in6;
        out3 = out2 ^ in1 ^ in7;
        out4 = out3 ^ in2;
        out5 = tmp1 ^ out4;
        out6 = tmp2 ^ out4;

        out_ptr[0] = out0 ^ in_ptr[0];
        out_ptr[WIDTH] = out1 ^ in_ptr[WIDTH];
        out_ptr[WIDTH * 2] = out2 ^ in_ptr[WIDTH * 2];
        out_ptr[WIDTH * 3] = out3 ^ in_ptr[WIDTH * 3];
        out_ptr[WIDTH * 4] = out4 ^ in_ptr[WIDTH * 4];
        out_ptr[WIDTH * 5] = out5 ^ in_ptr[WIDTH * 5];
        out_ptr[WIDTH * 6] = out6 ^ in_ptr[WIDTH * 6];
        out_ptr[WIDTH * 7] = out7 ^ in_ptr[WIDTH * 7];

        in_ptr++;
        out_ptr++;
    }
}

static void gf8_muladd_BE(void *out, void *in)
{
    unsigned int i;
    uint64_t *in_ptr = (uint64_t *)in;
    uint64_t *out_ptr = (uint64_t *)out;

    for (i = 0; i < WIDTH; i++) {
        uint64_t out0, out1, out2, out3, out4, out5, out6, out7;
        uint64_t tmp0;

        uint64_t in0 = out_ptr[0];
        uint64_t in1 = out_ptr[WIDTH];
        uint64_t in2 = out_ptr[WIDTH * 2];
        uint64_t in3 = out_ptr[WIDTH * 3];
        uint64_t in4 = out_ptr[WIDTH * 4];
        uint64_t in5 = out_ptr[WIDTH * 5];
        uint64_t in6 = out_ptr[WIDTH * 6];
        uint64_t in7 = out_ptr[WIDTH * 7];

        tmp0 = in0 ^ in3 ^ in6;
        out4 = tmp0 ^ in5;
        out7 = tmp0 ^ in2;
        out3 = out4 ^ in4;
        out1 = out3 ^ out7 ^ in0;
        out2 = out3 ^ in3 ^ in7;
        out0 = out2 ^ out4 ^ in1;
        out5 = tmp0 ^ out0;
        out6 = out1 ^ out5 ^ in6;

        out_ptr[0] = out0 ^ in_ptr[0];
        out_ptr[WIDTH] = out1 ^ in_ptr[WIDTH];
        out_ptr[WIDTH * 2] = out2 ^ in_ptr[WIDTH * 2];
        out_ptr[WIDTH * 3] = out3 ^ in_ptr[WIDTH * 3];
        out_ptr[WIDTH * 4] = out4 ^ in_ptr[WIDTH * 4];
        out_ptr[WIDTH * 5] = out5 ^ in_ptr[WIDTH * 5];
        out_ptr[WIDTH * 6] = out6 ^ in_ptr[WIDTH * 6];
        out_ptr[WIDTH * 7] = out7 ^ in_ptr[WIDTH * 7];

        in_ptr++;
        out_ptr++;
    }
}

static void gf8_muladd_BF(void *out, void *in)
{
    unsigned int i;
    uint64_t *in_ptr = (uint64_t *)in;
    uint64_t *out_ptr = (uint64_t *)out;

    for (i = 0; i < WIDTH; i++) {
        uint64_t out0, out1, out2, out3, out4, out5, out6, out7;
        uint64_t tmp0, tmp1, tmp2, tmp3;

        uint64_t in0 = out_ptr[0];
        uint64_t in1 = out_ptr[WIDTH];
        uint64_t in2 = out_ptr[WIDTH * 2];
        uint64_t in3 = out_ptr[WIDTH * 3];
        uint64_t in4 = out_ptr[WIDTH * 4];
        uint64_t in5 = out_ptr[WIDTH * 5];
        uint64_t in6 = out_ptr[WIDTH * 6];
        uint64_t in7 = out_ptr[WIDTH * 7];

        tmp0 = in0 ^ in4;
        out3 = tmp0 ^ in5 ^ in6;
        out4 = out3 ^ in3;
        tmp1 = out3 ^ in7;
        out2 = tmp1 ^ in2;
        out5 = tmp1 ^ in1;
        tmp2 = out2 ^ in5;
        out7 = tmp2 ^ in3 ^ in4;
        tmp3 = tmp0 ^ out5;
        out0 = tmp3 ^ out4;
        out1 = tmp2 ^ tmp3;
        out6 = tmp3 ^ in2;

        out_ptr[0] = out0 ^ in_ptr[0];
        out_ptr[WIDTH] = out1 ^ in_ptr[WIDTH];
        out_ptr[WIDTH * 2] = out2 ^ in_ptr[WIDTH * 2];
        out_ptr[WIDTH * 3] = out3 ^ in_ptr[WIDTH * 3];
        out_ptr[WIDTH * 4] = out4 ^ in_ptr[WIDTH * 4];
        out_ptr[WIDTH * 5] = out5 ^ in_ptr[WIDTH * 5];
        out_ptr[WIDTH * 6] = out6 ^ in_ptr[WIDTH * 6];
        out_ptr[WIDTH * 7] = out7 ^ in_ptr[WIDTH * 7];

        in_ptr++;
        out_ptr++;
    }
}

static void gf8_muladd_C0(void *out, void *in)
{
    unsigned int i;
    uint64_t *in_ptr = (uint64_t *)in;
    uint64_t *out_ptr = (uint64_t *)out;

    for (i = 0; i < WIDTH; i++) {
        uint64_t out0, out1, out2, out3, out4, out5, out6, out7;
        uint64_t tmp0, tmp1;

        uint64_t in0 = out_ptr[0];
        uint64_t in1 = out_ptr[WIDTH];
        uint64_t in2 = out_ptr[WIDTH * 2];
        uint64_t in3 = out_ptr[WIDTH * 3];
        uint64_t in4 = out_ptr[WIDTH * 4];
        uint64_t in5 = out_ptr[WIDTH * 5];
        uint64_t in6 = out_ptr[WIDTH * 6];
        uint64_t in7 = out_ptr[WIDTH * 7];

        out5 = in2 ^ in5;
        tmp0 = in1 ^ in4;
        tmp1 = in3 ^ in6;
        out0 = out5 ^ in1;
        out4 = tmp0 ^ in7;
        out3 = tmp0 ^ tmp1;
        out1 = tmp1 ^ in2;
        out6 = tmp1 ^ in0;
        out7 = out4 ^ in0;
        out2 = out4 ^ out5 ^ in3;

        out_ptr[0] = out0 ^ in_ptr[0];
        out_ptr[WIDTH] = out1 ^ in_ptr[WIDTH];
        out_ptr[WIDTH * 2] = out2 ^ in_ptr[WIDTH * 2];
        out_ptr[WIDTH * 3] = out3 ^ in_ptr[WIDTH * 3];
        out_ptr[WIDTH * 4] = out4 ^ in_ptr[WIDTH * 4];
        out_ptr[WIDTH * 5] = out5 ^ in_ptr[WIDTH * 5];
        out_ptr[WIDTH * 6] = out6 ^ in_ptr[WIDTH * 6];
        out_ptr[WIDTH * 7] = out7 ^ in_ptr[WIDTH * 7];

        in_ptr++;
        out_ptr++;
    }
}

static void gf8_muladd_C1(void *out, void *in)
{
    unsigned int i;
    uint64_t *in_ptr = (uint64_t *)in;
    uint64_t *out_ptr = (uint64_t *)out;

    for (i = 0; i < WIDTH; i++) {
        uint64_t out0, out1, out2, out3, out4, out5, out6, out7;
        uint64_t tmp0, tmp1;

        uint64_t in0 = out_ptr[0];
        uint64_t in1 = out_ptr[WIDTH];
        uint64_t in2 = out_ptr[WIDTH * 2];
        uint64_t in3 = out_ptr[WIDTH * 3];
        uint64_t in4 = out_ptr[WIDTH * 4];
        uint64_t in5 = out_ptr[WIDTH * 5];
        uint64_t in6 = out_ptr[WIDTH * 6];
        uint64_t in7 = out_ptr[WIDTH * 7];

        out5 = in2;
        tmp0 = in0 ^ in1;
        out4 = in1 ^ in7;
        out6 = in0 ^ in3;
        out3 = in1 ^ in4 ^ in6;
        tmp1 = tmp0 ^ in2;
        out7 = tmp0 ^ in4;
        out0 = tmp1 ^ in5;
        out1 = tmp1 ^ out6 ^ in6;
        out2 = out6 ^ out7 ^ in5 ^ in7;

        out_ptr[0] = out0 ^ in_ptr[0];
        out_ptr[WIDTH] = out1 ^ in_ptr[WIDTH];
        out_ptr[WIDTH * 2] = out2 ^ in_ptr[WIDTH * 2];
        out_ptr[WIDTH * 3] = out3 ^ in_ptr[WIDTH * 3];
        out_ptr[WIDTH * 4] = out4 ^ in_ptr[WIDTH * 4];
        out_ptr[WIDTH * 5] = out5 ^ in_ptr[WIDTH * 5];
        out_ptr[WIDTH * 6] = out6 ^ in_ptr[WIDTH * 6];
        out_ptr[WIDTH * 7] = out7 ^ in_ptr[WIDTH * 7];

        in_ptr++;
        out_ptr++;
    }
}

static void gf8_muladd_C2(void *out, void *in)
{
    unsigned int i;
    uint64_t *in_ptr = (uint64_t *)in;
    uint64_t *out_ptr = (uint64_t *)out;

    for (i = 0; i < WIDTH; i++) {
        uint64_t out0, out1, out2, out3, out4, out5, out6, out7;
        uint64_t tmp0, tmp1;

        uint64_t in0 = out_ptr[0];
        uint64_t in1 = out_ptr[WIDTH];
        uint64_t in2 = out_ptr[WIDTH * 2];
        uint64_t in3 = out_ptr[WIDTH * 3];
        uint64_t in4 = out_ptr[WIDTH * 4];
        uint64_t in5 = out_ptr[WIDTH * 5];
        uint64_t in6 = out_ptr[WIDTH * 6];
        uint64_t in7 = out_ptr[WIDTH * 7];

        out4 = in1 ^ in3 ^ in4;
        tmp0 = in0 ^ in3 ^ in6;
        out5 = in2 ^ in4 ^ in5;
        tmp1 = out4 ^ in7;
        out1 = tmp0 ^ in2;
        out6 = tmp0 ^ in5;
        out2 = out5 ^ in3;
        out7 = tmp0 ^ tmp1;
        out3 = tmp1 ^ in2 ^ in6;
        out0 = tmp1 ^ out2;

        out_ptr[0] = out0 ^ in_ptr[0];
        out_ptr[WIDTH] = out1 ^ in_ptr[WIDTH];
        out_ptr[WIDTH * 2] = out2 ^ in_ptr[WIDTH * 2];
        out_ptr[WIDTH * 3] = out3 ^ in_ptr[WIDTH * 3];
        out_ptr[WIDTH * 4] = out4 ^ in_ptr[WIDTH * 4];
        out_ptr[WIDTH * 5] = out5 ^ in_ptr[WIDTH * 5];
        out_ptr[WIDTH * 6] = out6 ^ in_ptr[WIDTH * 6];
        out_ptr[WIDTH * 7] = out7 ^ in_ptr[WIDTH * 7];

        in_ptr++;
        out_ptr++;
    }
}

static void gf8_muladd_C3(void *out, void *in)
{
    unsigned int i;
    uint64_t *in_ptr = (uint64_t *)in;
    uint64_t *out_ptr = (uint64_t *)out;

    for (i = 0; i < WIDTH; i++) {
        uint64_t out0, out1, out2, out3, out4, out5, out6, out7;
        uint64_t tmp0, tmp1, tmp2;

        uint64_t in0 = out_ptr[0];
        uint64_t in1 = out_ptr[WIDTH];
        uint64_t in2 = out_ptr[WIDTH * 2];
        uint64_t in3 = out_ptr[WIDTH * 3];
        uint64_t in4 = out_ptr[WIDTH * 4];
        uint64_t in5 = out_ptr[WIDTH * 5];
        uint64_t in6 = out_ptr[WIDTH * 6];
        uint64_t in7 = out_ptr[WIDTH * 7];

        out4 = in1 ^ in3;
        tmp0 = in0 ^ in2;
        tmp1 = in3 ^ in5;
        out5 = in2 ^ in4;
        tmp2 = tmp0 ^ out4;
        out2 = tmp1 ^ in4;
        out6 = tmp1 ^ in0;
        out0 = tmp1 ^ tmp2 ^ in7;
        out1 = tmp2 ^ in6;
        out7 = out1 ^ out5 ^ in3;
        out3 = tmp0 ^ out7 ^ in7;

        out_ptr[0] = out0 ^ in_ptr[0];
        out_ptr[WIDTH] = out1 ^ in_ptr[WIDTH];
        out_ptr[WIDTH * 2] = out2 ^ in_ptr[WIDTH * 2];
        out_ptr[WIDTH * 3] = out3 ^ in_ptr[WIDTH * 3];
        out_ptr[WIDTH * 4] = out4 ^ in_ptr[WIDTH * 4];
        out_ptr[WIDTH * 5] = out5 ^ in_ptr[WIDTH * 5];
        out_ptr[WIDTH * 6] = out6 ^ in_ptr[WIDTH * 6];
        out_ptr[WIDTH * 7] = out7 ^ in_ptr[WIDTH * 7];

        in_ptr++;
        out_ptr++;
    }
}

static void gf8_muladd_C4(void *out, void *in)
{
    unsigned int i;
    uint64_t *in_ptr = (uint64_t *)in;
    uint64_t *out_ptr = (uint64_t *)out;

    for (i = 0; i < WIDTH; i++) {
        uint64_t out0, out1, out2, out3, out4, out5, out6, out7;
        uint64_t tmp0, tmp1;

        uint64_t in0 = out_ptr[0];
        uint64_t in1 = out_ptr[WIDTH];
        uint64_t in2 = out_ptr[WIDTH * 2];
        uint64_t in3 = out_ptr[WIDTH * 3];
        uint64_t in4 = out_ptr[WIDTH * 4];
        uint64_t in5 = out_ptr[WIDTH * 5];
        uint64_t in6 = out_ptr[WIDTH * 6];
        uint64_t in7 = out_ptr[WIDTH * 7];

        tmp0 = in3 ^ in7;
        out3 = tmp0 ^ in4;
        tmp1 = tmp0 ^ in2;
        out1 = tmp1 ^ in6;
        out5 = tmp1 ^ in5;
        out4 = out1 ^ out3 ^ in1;
        out0 = out4 ^ in4 ^ in5;
        out2 = out0 ^ out3 ^ in0;
        out7 = out1 ^ out2 ^ in7;
        out6 = tmp1 ^ out0 ^ out7;

        out_ptr[0] = out0 ^ in_ptr[0];
        out_ptr[WIDTH] = out1 ^ in_ptr[WIDTH];
        out_ptr[WIDTH * 2] = out2 ^ in_ptr[WIDTH * 2];
        out_ptr[WIDTH * 3] = out3 ^ in_ptr[WIDTH * 3];
        out_ptr[WIDTH * 4] = out4 ^ in_ptr[WIDTH * 4];
        out_ptr[WIDTH * 5] = out5 ^ in_ptr[WIDTH * 5];
        out_ptr[WIDTH * 6] = out6 ^ in_ptr[WIDTH * 6];
        out_ptr[WIDTH * 7] = out7 ^ in_ptr[WIDTH * 7];

        in_ptr++;
        out_ptr++;
    }
}

static void gf8_muladd_C5(void *out, void *in)
{
    unsigned int i;
    uint64_t *in_ptr = (uint64_t *)in;
    uint64_t *out_ptr = (uint64_t *)out;

    for (i = 0; i < WIDTH; i++) {
        uint64_t out0, out1, out2, out3, out4, out5, out6, out7;
        uint64_t tmp0;

        uint64_t in0 = out_ptr[0];
        uint64_t in1 = out_ptr[WIDTH];
        uint64_t in2 = out_ptr[WIDTH * 2];
        uint64_t in3 = out_ptr[WIDTH * 3];
        uint64_t in4 = out_ptr[WIDTH * 4];
        uint64_t in5 = out_ptr[WIDTH * 5];
        uint64_t in6 = out_ptr[WIDTH * 6];
        uint64_t in7 = out_ptr[WIDTH * 7];

        out3 = in4 ^ in7;
        tmp0 = in3 ^ in7;
        out4 = in1 ^ in2 ^ in6;
        out6 = in0 ^ in3 ^ in4;
        out5 = tmp0 ^ in2;
        out1 = tmp0 ^ out4;
        out0 = out4 ^ in0 ^ in5;
        out2 = out0 ^ out5 ^ in4;
        out7 = tmp0 ^ out2 ^ in6;

        out_ptr[0] = out0 ^ in_ptr[0];
        out_ptr[WIDTH] = out1 ^ in_ptr[WIDTH];
        out_ptr[WIDTH * 2] = out2 ^ in_ptr[WIDTH * 2];
        out_ptr[WIDTH * 3] = out3 ^ in_ptr[WIDTH * 3];
        out_ptr[WIDTH * 4] = out4 ^ in_ptr[WIDTH * 4];
        out_ptr[WIDTH * 5] = out5 ^ in_ptr[WIDTH * 5];
        out_ptr[WIDTH * 6] = out6 ^ in_ptr[WIDTH * 6];
        out_ptr[WIDTH * 7] = out7 ^ in_ptr[WIDTH * 7];

        in_ptr++;
        out_ptr++;
    }
}

static void gf8_muladd_C6(void *out, void *in)
{
    unsigned int i;
    uint64_t *in_ptr = (uint64_t *)in;
    uint64_t *out_ptr = (uint64_t *)out;

    for (i = 0; i < WIDTH; i++) {
        uint64_t out0, out1, out2, out3, out4, out5, out6, out7;
        uint64_t tmp0, tmp1, tmp2, tmp3, tmp4, tmp5;

        uint64_t in0 = out_ptr[0];
        uint64_t in1 = out_ptr[WIDTH];
        uint64_t in2 = out_ptr[WIDTH * 2];
        uint64_t in3 = out_ptr[WIDTH * 3];
        uint64_t in4 = out_ptr[WIDTH * 4];
        uint64_t in5 = out_ptr[WIDTH * 5];
        uint64_t in6 = out_ptr[WIDTH * 6];
        uint64_t in7 = out_ptr[WIDTH * 7];

        tmp0 = in5 ^ in6;
        tmp1 = in1 ^ in7;
        tmp2 = tmp0 ^ in0;
        tmp3 = tmp0 ^ tmp1;
        tmp4 = tmp2 ^ in4;
        out0 = tmp3 ^ in2;
        out6 = tmp4 ^ in3;
        out2 = out6 ^ in2;
        out7 = tmp1 ^ tmp4;
        out3 = tmp2 ^ out2;
        tmp5 = out3 ^ in5;
        out5 = tmp5 ^ in7;
        out4 = tmp3 ^ tmp5;
        out1 = tmp4 ^ out5;

        out_ptr[0] = out0 ^ in_ptr[0];
        out_ptr[WIDTH] = out1 ^ in_ptr[WIDTH];
        out_ptr[WIDTH * 2] = out2 ^ in_ptr[WIDTH * 2];
        out_ptr[WIDTH * 3] = out3 ^ in_ptr[WIDTH * 3];
        out_ptr[WIDTH * 4] = out4 ^ in_ptr[WIDTH * 4];
        out_ptr[WIDTH * 5] = out5 ^ in_ptr[WIDTH * 5];
        out_ptr[WIDTH * 6] = out6 ^ in_ptr[WIDTH * 6];
        out_ptr[WIDTH * 7] = out7 ^ in_ptr[WIDTH * 7];

        in_ptr++;
        out_ptr++;
    }
}

static void gf8_muladd_C7(void *out, void *in)
{
    unsigned int i;
    uint64_t *in_ptr = (uint64_t *)in;
    uint64_t *out_ptr = (uint64_t *)out;

    for (i = 0; i < WIDTH; i++) {
        uint64_t out0, out1, out2, out3, out4, out5, out6, out7;
        uint64_t tmp0, tmp1;

        uint64_t in0 = out_ptr[0];
        uint64_t in1 = out_ptr[WIDTH];
        uint64_t in2 = out_ptr[WIDTH * 2];
        uint64_t in3 = out_ptr[WIDTH * 3];
        uint64_t in4 = out_ptr[WIDTH * 4];
        uint64_t in5 = out_ptr[WIDTH * 5];
        uint64_t in6 = out_ptr[WIDTH * 6];
        uint64_t in7 = out_ptr[WIDTH * 7];

        out3 = in2 ^ in4;
        tmp0 = in3 ^ in5;
        tmp1 = out3 ^ in7;
        out6 = tmp0 ^ in0 ^ in4;
        out5 = tmp1 ^ in3;
        out2 = out6 ^ in6;
        out7 = out2 ^ in1 ^ in3;
        out0 = tmp1 ^ out7;
        out1 = tmp0 ^ out0;
        out4 = out1 ^ in0;

        out_ptr[0] = out0 ^ in_ptr[0];
        out_ptr[WIDTH] = out1 ^ in_ptr[WIDTH];
        out_ptr[WIDTH * 2] = out2 ^ in_ptr[WIDTH * 2];
        out_ptr[WIDTH * 3] = out3 ^ in_ptr[WIDTH * 3];
        out_ptr[WIDTH * 4] = out4 ^ in_ptr[WIDTH * 4];
        out_ptr[WIDTH * 5] = out5 ^ in_ptr[WIDTH * 5];
        out_ptr[WIDTH * 6] = out6 ^ in_ptr[WIDTH * 6];
        out_ptr[WIDTH * 7] = out7 ^ in_ptr[WIDTH * 7];

        in_ptr++;
        out_ptr++;
    }
}

static void gf8_muladd_C8(void *out, void *in)
{
    unsigned int i;
    uint64_t *in_ptr = (uint64_t *)in;
    uint64_t *out_ptr = (uint64_t *)out;

    for (i = 0; i < WIDTH; i++) {
        uint64_t out0, out1, out2, out3, out4, out5, out6, out7;
        uint64_t tmp0, tmp1;

        uint64_t in0 = out_ptr[0];
        uint64_t in1 = out_ptr[WIDTH];
        uint64_t in2 = out_ptr[WIDTH * 2];
        uint64_t in3 = out_ptr[WIDTH * 3];
        uint64_t in4 = out_ptr[WIDTH * 4];
        uint64_t in5 = out_ptr[WIDTH * 5];
        uint64_t in6 = out_ptr[WIDTH * 6];
        uint64_t in7 = out_ptr[WIDTH * 7];

        out0 = in1 ^ in2;
        out1 = in2 ^ in3;
        tmp0 = in5 ^ in6;
        tmp1 = in0 ^ in7;
        out2 = out1 ^ in1 ^ in4;
        out4 = tmp0 ^ in4;
        out5 = tmp0 ^ in7;
        out6 = tmp1 ^ in6;
        out7 = tmp1 ^ in1;
        out3 = out2 ^ in0 ^ in2 ^ in5;

        out_ptr[0] = out0 ^ in_ptr[0];
        out_ptr[WIDTH] = out1 ^ in_ptr[WIDTH];
        out_ptr[WIDTH * 2] = out2 ^ in_ptr[WIDTH * 2];
        out_ptr[WIDTH * 3] = out3 ^ in_ptr[WIDTH * 3];
        out_ptr[WIDTH * 4] = out4 ^ in_ptr[WIDTH * 4];
        out_ptr[WIDTH * 5] = out5 ^ in_ptr[WIDTH * 5];
        out_ptr[WIDTH * 6] = out6 ^ in_ptr[WIDTH * 6];
        out_ptr[WIDTH * 7] = out7 ^ in_ptr[WIDTH * 7];

        in_ptr++;
        out_ptr++;
    }
}

static void gf8_muladd_C9(void *out, void *in)
{
    unsigned int i;
    uint64_t *in_ptr = (uint64_t *)in;
    uint64_t *out_ptr = (uint64_t *)out;

    for (i = 0; i < WIDTH; i++) {
        uint64_t out0, out1, out2, out3, out4, out5, out6, out7;
        uint64_t tmp0;

        uint64_t in0 = out_ptr[0];
        uint64_t in1 = out_ptr[WIDTH];
        uint64_t in2 = out_ptr[WIDTH * 2];
        uint64_t in3 = out_ptr[WIDTH * 3];
        uint64_t in4 = out_ptr[WIDTH * 4];
        uint64_t in5 = out_ptr[WIDTH * 5];
        uint64_t in6 = out_ptr[WIDTH * 6];
        uint64_t in7 = out_ptr[WIDTH * 7];

        out4 = in5 ^ in6;
        out7 = in0 ^ in1;
        tmp0 = in1 ^ in3;
        out5 = in6 ^ in7;
        out6 = in0 ^ in7;
        out0 = out7 ^ in2;
        out3 = out7 ^ in4 ^ in5;
        out1 = tmp0 ^ in2;
        out2 = tmp0 ^ in4;

        out_ptr[0] = out0 ^ in_ptr[0];
        out_ptr[WIDTH] = out1 ^ in_ptr[WIDTH];
        out_ptr[WIDTH * 2] = out2 ^ in_ptr[WIDTH * 2];
        out_ptr[WIDTH * 3] = out3 ^ in_ptr[WIDTH * 3];
        out_ptr[WIDTH * 4] = out4 ^ in_ptr[WIDTH * 4];
        out_ptr[WIDTH * 5] = out5 ^ in_ptr[WIDTH * 5];
        out_ptr[WIDTH * 6] = out6 ^ in_ptr[WIDTH * 6];
        out_ptr[WIDTH * 7] = out7 ^ in_ptr[WIDTH * 7];

        in_ptr++;
        out_ptr++;
    }
}

static void gf8_muladd_CA(void *out, void *in)
{
    unsigned int i;
    uint64_t *in_ptr = (uint64_t *)in;
    uint64_t *out_ptr = (uint64_t *)out;

    for (i = 0; i < WIDTH; i++) {
        uint64_t out0, out1, out2, out3, out4, out5, out6, out7;
        uint64_t tmp0, tmp1, tmp2, tmp3;

        uint64_t in0 = out_ptr[0];
        uint64_t in1 = out_ptr[WIDTH];
        uint64_t in2 = out_ptr[WIDTH * 2];
        uint64_t in3 = out_ptr[WIDTH * 3];
        uint64_t in4 = out_ptr[WIDTH * 4];
        uint64_t in5 = out_ptr[WIDTH * 5];
        uint64_t in6 = out_ptr[WIDTH * 6];
        uint64_t in7 = out_ptr[WIDTH * 7];

        tmp0 = in0 ^ in7;
        tmp1 = in2 ^ in7;
        tmp2 = tmp0 ^ in6;
        out0 = tmp1 ^ in1;
        tmp3 = tmp1 ^ in3;
        out6 = tmp2 ^ in5;
        out7 = tmp2 ^ in1;
        out2 = tmp3 ^ in4;
        out5 = out6 ^ in0 ^ in4;
        out4 = out5 ^ in3;
        out1 = tmp0 ^ tmp3;
        out3 = tmp3 ^ out5 ^ out7;

        out_ptr[0] = out0 ^ in_ptr[0];
        out_ptr[WIDTH] = out1 ^ in_ptr[WIDTH];
        out_ptr[WIDTH * 2] = out2 ^ in_ptr[WIDTH * 2];
        out_ptr[WIDTH * 3] = out3 ^ in_ptr[WIDTH * 3];
        out_ptr[WIDTH * 4] = out4 ^ in_ptr[WIDTH * 4];
        out_ptr[WIDTH * 5] = out5 ^ in_ptr[WIDTH * 5];
        out_ptr[WIDTH * 6] = out6 ^ in_ptr[WIDTH * 6];
        out_ptr[WIDTH * 7] = out7 ^ in_ptr[WIDTH * 7];

        in_ptr++;
        out_ptr++;
    }
}

static void gf8_muladd_CB(void *out, void *in)
{
    unsigned int i;
    uint64_t *in_ptr = (uint64_t *)in;
    uint64_t *out_ptr = (uint64_t *)out;

    for (i = 0; i < WIDTH; i++) {
        uint64_t out0, out1, out2, out3, out4, out5, out6, out7;
        uint64_t tmp0, tmp1, tmp2;

        uint64_t in0 = out_ptr[0];
        uint64_t in1 = out_ptr[WIDTH];
        uint64_t in2 = out_ptr[WIDTH * 2];
        uint64_t in3 = out_ptr[WIDTH * 3];
        uint64_t in4 = out_ptr[WIDTH * 4];
        uint64_t in5 = out_ptr[WIDTH * 5];
        uint64_t in6 = out_ptr[WIDTH * 6];
        uint64_t in7 = out_ptr[WIDTH * 7];

        tmp0 = in4 ^ in7;
        tmp1 = in5 ^ in7;
        out7 = in0 ^ in1 ^ in6;
        out5 = tmp0 ^ in6;
        out2 = tmp0 ^ in3;
        out6 = tmp1 ^ in0;
        out4 = tmp1 ^ in3 ^ in6;
        tmp2 = out5 ^ out7 ^ in2;
        out1 = tmp2 ^ out2;
        out0 = tmp2 ^ in4;
        out3 = tmp2 ^ in5;

        out_ptr[0] = out0 ^ in_ptr[0];
        out_ptr[WIDTH] = out1 ^ in_ptr[WIDTH];
        out_ptr[WIDTH * 2] = out2 ^ in_ptr[WIDTH * 2];
        out_ptr[WIDTH * 3] = out3 ^ in_ptr[WIDTH * 3];
        out_ptr[WIDTH * 4] = out4 ^ in_ptr[WIDTH * 4];
        out_ptr[WIDTH * 5] = out5 ^ in_ptr[WIDTH * 5];
        out_ptr[WIDTH * 6] = out6 ^ in_ptr[WIDTH * 6];
        out_ptr[WIDTH * 7] = out7 ^ in_ptr[WIDTH * 7];

        in_ptr++;
        out_ptr++;
    }
}

static void gf8_muladd_CC(void *out, void *in)
{
    unsigned int i;
    uint64_t *in_ptr = (uint64_t *)in;
    uint64_t *out_ptr = (uint64_t *)out;

    for (i = 0; i < WIDTH; i++) {
        uint64_t out0, out1, out2, out3, out4, out5, out6, out7;
        uint64_t tmp0, tmp1, tmp2, tmp3;

        uint64_t in0 = out_ptr[0];
        uint64_t in1 = out_ptr[WIDTH];
        uint64_t in2 = out_ptr[WIDTH * 2];
        uint64_t in3 = out_ptr[WIDTH * 3];
        uint64_t in4 = out_ptr[WIDTH * 4];
        uint64_t in5 = out_ptr[WIDTH * 5];
        uint64_t in6 = out_ptr[WIDTH * 6];
        uint64_t in7 = out_ptr[WIDTH * 7];

        tmp0 = in3 ^ in5;
        tmp1 = in1 ^ in6;
        out1 = in2 ^ in3 ^ in7;
        out5 = tmp0 ^ in6;
        out0 = tmp1 ^ in2;
        tmp2 = out5 ^ in0 ^ in7;
        out3 = tmp2 ^ in4;
        out6 = tmp0 ^ out3;
        out7 = tmp1 ^ tmp2 ^ in3;
        tmp3 = out1 ^ out6;
        out4 = tmp2 ^ tmp3;
        out2 = tmp3 ^ in1;

        out_ptr[0] = out0 ^ in_ptr[0];
        out_ptr[WIDTH] = out1 ^ in_ptr[WIDTH];
        out_ptr[WIDTH * 2] = out2 ^ in_ptr[WIDTH * 2];
        out_ptr[WIDTH * 3] = out3 ^ in_ptr[WIDTH * 3];
        out_ptr[WIDTH * 4] = out4 ^ in_ptr[WIDTH * 4];
        out_ptr[WIDTH * 5] = out5 ^ in_ptr[WIDTH * 5];
        out_ptr[WIDTH * 6] = out6 ^ in_ptr[WIDTH * 6];
        out_ptr[WIDTH * 7] = out7 ^ in_ptr[WIDTH * 7];

        in_ptr++;
        out_ptr++;
    }
}

static void gf8_muladd_CD(void *out, void *in)
{
    unsigned int i;
    uint64_t *in_ptr = (uint64_t *)in;
    uint64_t *out_ptr = (uint64_t *)out;

    for (i = 0; i < WIDTH; i++) {
        uint64_t out0, out1, out2, out3, out4, out5, out6, out7;
        uint64_t tmp0, tmp1;

        uint64_t in0 = out_ptr[0];
        uint64_t in1 = out_ptr[WIDTH];
        uint64_t in2 = out_ptr[WIDTH * 2];
        uint64_t in3 = out_ptr[WIDTH * 3];
        uint64_t in4 = out_ptr[WIDTH * 4];
        uint64_t in5 = out_ptr[WIDTH * 5];
        uint64_t in6 = out_ptr[WIDTH * 6];
        uint64_t in7 = out_ptr[WIDTH * 7];

        out5 = in3 ^ in6;
        tmp0 = in0 ^ in1;
        tmp1 = in2 ^ in7;
        out6 = in0 ^ in4 ^ in7;
        out2 = tmp0 ^ out5 ^ in4;
        out7 = tmp0 ^ in5;
        out0 = tmp0 ^ in2 ^ in6;
        out4 = tmp1 ^ in5;
        out1 = tmp1 ^ in1 ^ in3;
        out3 = out6 ^ in5 ^ in6;

        out_ptr[0] = out0 ^ in_ptr[0];
        out_ptr[WIDTH] = out1 ^ in_ptr[WIDTH];
        out_ptr[WIDTH * 2] = out2 ^ in_ptr[WIDTH * 2];
        out_ptr[WIDTH * 3] = out3 ^ in_ptr[WIDTH * 3];
        out_ptr[WIDTH * 4] = out4 ^ in_ptr[WIDTH * 4];
        out_ptr[WIDTH * 5] = out5 ^ in_ptr[WIDTH * 5];
        out_ptr[WIDTH * 6] = out6 ^ in_ptr[WIDTH * 6];
        out_ptr[WIDTH * 7] = out7 ^ in_ptr[WIDTH * 7];

        in_ptr++;
        out_ptr++;
    }
}

static void gf8_muladd_CE(void *out, void *in)
{
    unsigned int i;
    uint64_t *in_ptr = (uint64_t *)in;
    uint64_t *out_ptr = (uint64_t *)out;

    for (i = 0; i < WIDTH; i++) {
        uint64_t out0, out1, out2, out3, out4, out5, out6, out7;
        uint64_t tmp0, tmp1, tmp2;

        uint64_t in0 = out_ptr[0];
        uint64_t in1 = out_ptr[WIDTH];
        uint64_t in2 = out_ptr[WIDTH * 2];
        uint64_t in3 = out_ptr[WIDTH * 3];
        uint64_t in4 = out_ptr[WIDTH * 4];
        uint64_t in5 = out_ptr[WIDTH * 5];
        uint64_t in6 = out_ptr[WIDTH * 6];
        uint64_t in7 = out_ptr[WIDTH * 7];

        tmp0 = in2 ^ in5;
        tmp1 = tmp0 ^ in3;
        out4 = tmp1 ^ in4;
        tmp2 = out4 ^ in6;
        out3 = tmp2 ^ in0;
        out5 = tmp2 ^ in2;
        out2 = out3 ^ in5 ^ in7;
        out6 = tmp1 ^ out2;
        out7 = out2 ^ out4 ^ in1;
        out1 = tmp2 ^ out6;
        out0 = tmp0 ^ out7 ^ in0;

        out_ptr[0] = out0 ^ in_ptr[0];
        out_ptr[WIDTH] = out1 ^ in_ptr[WIDTH];
        out_ptr[WIDTH * 2] = out2 ^ in_ptr[WIDTH * 2];
        out_ptr[WIDTH * 3] = out3 ^ in_ptr[WIDTH * 3];
        out_ptr[WIDTH * 4] = out4 ^ in_ptr[WIDTH * 4];
        out_ptr[WIDTH * 5] = out5 ^ in_ptr[WIDTH * 5];
        out_ptr[WIDTH * 6] = out6 ^ in_ptr[WIDTH * 6];
        out_ptr[WIDTH * 7] = out7 ^ in_ptr[WIDTH * 7];

        in_ptr++;
        out_ptr++;
    }
}

static void gf8_muladd_CF(void *out, void *in)
{
    unsigned int i;
    uint64_t *in_ptr = (uint64_t *)in;
    uint64_t *out_ptr = (uint64_t *)out;

    for (i = 0; i < WIDTH; i++) {
        uint64_t out0, out1, out2, out3, out4, out5, out6, out7;
        uint64_t tmp0, tmp1, tmp2;

        uint64_t in0 = out_ptr[0];
        uint64_t in1 = out_ptr[WIDTH];
        uint64_t in2 = out_ptr[WIDTH * 2];
        uint64_t in3 = out_ptr[WIDTH * 3];
        uint64_t in4 = out_ptr[WIDTH * 4];
        uint64_t in5 = out_ptr[WIDTH * 5];
        uint64_t in6 = out_ptr[WIDTH * 6];
        uint64_t in7 = out_ptr[WIDTH * 7];

        tmp0 = in3 ^ in6;
        tmp1 = in0 ^ in1 ^ in5;
        out4 = in2 ^ in3 ^ in5;
        out5 = tmp0 ^ in4;
        out7 = tmp1 ^ in6;
        out1 = tmp1 ^ out4 ^ in7;
        tmp2 = out5 ^ in0;
        out2 = tmp2 ^ in7;
        out3 = tmp2 ^ out4;
        out6 = tmp0 ^ out2 ^ in5;
        out0 = tmp0 ^ out1;

        out_ptr[0] = out0 ^ in_ptr[0];
        out_ptr[WIDTH] = out1 ^ in_ptr[WIDTH];
        out_ptr[WIDTH * 2] = out2 ^ in_ptr[WIDTH * 2];
        out_ptr[WIDTH * 3] = out3 ^ in_ptr[WIDTH * 3];
        out_ptr[WIDTH * 4] = out4 ^ in_ptr[WIDTH * 4];
        out_ptr[WIDTH * 5] = out5 ^ in_ptr[WIDTH * 5];
        out_ptr[WIDTH * 6] = out6 ^ in_ptr[WIDTH * 6];
        out_ptr[WIDTH * 7] = out7 ^ in_ptr[WIDTH * 7];

        in_ptr++;
        out_ptr++;
    }
}

static void gf8_muladd_D0(void *out, void *in)
{
    unsigned int i;
    uint64_t *in_ptr = (uint64_t *)in;
    uint64_t *out_ptr = (uint64_t *)out;

    for (i = 0; i < WIDTH; i++) {
        uint64_t out0, out1, out2, out3, out4, out5, out6, out7;
        uint64_t tmp0, tmp1, tmp2, tmp3, tmp4;

        uint64_t in0 = out_ptr[0];
        uint64_t in1 = out_ptr[WIDTH];
        uint64_t in2 = out_ptr[WIDTH * 2];
        uint64_t in3 = out_ptr[WIDTH * 3];
        uint64_t in4 = out_ptr[WIDTH * 4];
        uint64_t in5 = out_ptr[WIDTH * 5];
        uint64_t in6 = out_ptr[WIDTH * 6];
        uint64_t in7 = out_ptr[WIDTH * 7];

        tmp0 = in0 ^ in3;
        tmp1 = in1 ^ in4;
        tmp2 = in2 ^ in5;
        out7 = tmp0 ^ tmp1;
        out0 = tmp1 ^ tmp2;
        tmp3 = tmp2 ^ in3;
        out1 = tmp3 ^ in6;
        tmp4 = out1 ^ in1;
        out2 = tmp4 ^ in7;
        out3 = out2 ^ in2;
        out4 = tmp0 ^ out3;
        out5 = tmp3 ^ out3;
        out6 = tmp4 ^ out4;

        out_ptr[0] = out0 ^ in_ptr[0];
        out_ptr[WIDTH] = out1 ^ in_ptr[WIDTH];
        out_ptr[WIDTH * 2] = out2 ^ in_ptr[WIDTH * 2];
        out_ptr[WIDTH * 3] = out3 ^ in_ptr[WIDTH * 3];
        out_ptr[WIDTH * 4] = out4 ^ in_ptr[WIDTH * 4];
        out_ptr[WIDTH * 5] = out5 ^ in_ptr[WIDTH * 5];
        out_ptr[WIDTH * 6] = out6 ^ in_ptr[WIDTH * 6];
        out_ptr[WIDTH * 7] = out7 ^ in_ptr[WIDTH * 7];

        in_ptr++;
        out_ptr++;
    }
}

static void gf8_muladd_D1(void *out, void *in)
{
    unsigned int i;
    uint64_t *in_ptr = (uint64_t *)in;
    uint64_t *out_ptr = (uint64_t *)out;

    for (i = 0; i < WIDTH; i++) {
        uint64_t out0, out1, out2, out3, out4, out5, out6, out7;
        uint64_t tmp0, tmp1, tmp2;

        uint64_t in0 = out_ptr[0];
        uint64_t in1 = out_ptr[WIDTH];
        uint64_t in2 = out_ptr[WIDTH * 2];
        uint64_t in3 = out_ptr[WIDTH * 3];
        uint64_t in4 = out_ptr[WIDTH * 4];
        uint64_t in5 = out_ptr[WIDTH * 5];
        uint64_t in6 = out_ptr[WIDTH * 6];
        uint64_t in7 = out_ptr[WIDTH * 7];

        tmp0 = in3 ^ in5 ^ in6;
        tmp1 = tmp0 ^ in1;
        out1 = tmp1 ^ in2;
        out2 = tmp1 ^ in7;
        out3 = out2 ^ in3;
        out5 = out3 ^ in2;
        tmp2 = out3 ^ in0;
        out4 = tmp2 ^ in4;
        out7 = tmp0 ^ out4;
        out6 = tmp2 ^ out1 ^ in6;
        out0 = out2 ^ out6 ^ in4;

        out_ptr[0] = out0 ^ in_ptr[0];
        out_ptr[WIDTH] = out1 ^ in_ptr[WIDTH];
        out_ptr[WIDTH * 2] = out2 ^ in_ptr[WIDTH * 2];
        out_ptr[WIDTH * 3] = out3 ^ in_ptr[WIDTH * 3];
        out_ptr[WIDTH * 4] = out4 ^ in_ptr[WIDTH * 4];
        out_ptr[WIDTH * 5] = out5 ^ in_ptr[WIDTH * 5];
        out_ptr[WIDTH * 6] = out6 ^ in_ptr[WIDTH * 6];
        out_ptr[WIDTH * 7] = out7 ^ in_ptr[WIDTH * 7];

        in_ptr++;
        out_ptr++;
    }
}

static void gf8_muladd_D2(void *out, void *in)
{
    unsigned int i;
    uint64_t *in_ptr = (uint64_t *)in;
    uint64_t *out_ptr = (uint64_t *)out;

    for (i = 0; i < WIDTH; i++) {
        uint64_t out0, out1, out2, out3, out4, out5, out6, out7;
        uint64_t tmp0;

        uint64_t in0 = out_ptr[0];
        uint64_t in1 = out_ptr[WIDTH];
        uint64_t in2 = out_ptr[WIDTH * 2];
        uint64_t in3 = out_ptr[WIDTH * 3];
        uint64_t in4 = out_ptr[WIDTH * 4];
        uint64_t in5 = out_ptr[WIDTH * 5];
        uint64_t in6 = out_ptr[WIDTH * 6];
        uint64_t in7 = out_ptr[WIDTH * 7];

        tmp0 = in5 ^ in6;
        out2 = tmp0 ^ in2 ^ in3;
        out1 = out2 ^ in0;
        out3 = out2 ^ in1;
        out4 = out1 ^ in1 ^ in2;
        out6 = out1 ^ in6 ^ in7;
        out7 = out4 ^ in4 ^ in5;
        out5 = out4 ^ out6 ^ in4;
        out0 = tmp0 ^ out5;

        out_ptr[0] = out0 ^ in_ptr[0];
        out_ptr[WIDTH] = out1 ^ in_ptr[WIDTH];
        out_ptr[WIDTH * 2] = out2 ^ in_ptr[WIDTH * 2];
        out_ptr[WIDTH * 3] = out3 ^ in_ptr[WIDTH * 3];
        out_ptr[WIDTH * 4] = out4 ^ in_ptr[WIDTH * 4];
        out_ptr[WIDTH * 5] = out5 ^ in_ptr[WIDTH * 5];
        out_ptr[WIDTH * 6] = out6 ^ in_ptr[WIDTH * 6];
        out_ptr[WIDTH * 7] = out7 ^ in_ptr[WIDTH * 7];

        in_ptr++;
        out_ptr++;
    }
}

static void gf8_muladd_D3(void *out, void *in)
{
    unsigned int i;
    uint64_t *in_ptr = (uint64_t *)in;
    uint64_t *out_ptr = (uint64_t *)out;

    for (i = 0; i < WIDTH; i++) {
        uint64_t out0, out1, out2, out3, out4, out5, out6, out7;
        uint64_t tmp0, tmp1, tmp2, tmp3;

        uint64_t in0 = out_ptr[0];
        uint64_t in1 = out_ptr[WIDTH];
        uint64_t in2 = out_ptr[WIDTH * 2];
        uint64_t in3 = out_ptr[WIDTH * 3];
        uint64_t in4 = out_ptr[WIDTH * 4];
        uint64_t in5 = out_ptr[WIDTH * 5];
        uint64_t in6 = out_ptr[WIDTH * 6];
        uint64_t in7 = out_ptr[WIDTH * 7];

        out2 = in3 ^ in5 ^ in6;
        tmp0 = out2 ^ in2;
        tmp1 = tmp0 ^ in1;
        out1 = tmp1 ^ in0;
        out3 = tmp1 ^ in3;
        out4 = out1 ^ in2 ^ in4;
        tmp2 = out4 ^ in5;
        out7 = tmp2 ^ in7;
        out0 = tmp0 ^ out7;
        tmp3 = out0 ^ in0;
        out5 = tmp3 ^ in6;
        out6 = tmp2 ^ tmp3;

        out_ptr[0] = out0 ^ in_ptr[0];
        out_ptr[WIDTH] = out1 ^ in_ptr[WIDTH];
        out_ptr[WIDTH * 2] = out2 ^ in_ptr[WIDTH * 2];
        out_ptr[WIDTH * 3] = out3 ^ in_ptr[WIDTH * 3];
        out_ptr[WIDTH * 4] = out4 ^ in_ptr[WIDTH * 4];
        out_ptr[WIDTH * 5] = out5 ^ in_ptr[WIDTH * 5];
        out_ptr[WIDTH * 6] = out6 ^ in_ptr[WIDTH * 6];
        out_ptr[WIDTH * 7] = out7 ^ in_ptr[WIDTH * 7];

        in_ptr++;
        out_ptr++;
    }
}

static void gf8_muladd_D4(void *out, void *in)
{
    unsigned int i;
    uint64_t *in_ptr = (uint64_t *)in;
    uint64_t *out_ptr = (uint64_t *)out;

    for (i = 0; i < WIDTH; i++) {
        uint64_t out0, out1, out2, out3, out4, out5, out6, out7;
        uint64_t tmp0, tmp1, tmp2;

        uint64_t in0 = out_ptr[0];
        uint64_t in1 = out_ptr[WIDTH];
        uint64_t in2 = out_ptr[WIDTH * 2];
        uint64_t in3 = out_ptr[WIDTH * 3];
        uint64_t in4 = out_ptr[WIDTH * 4];
        uint64_t in5 = out_ptr[WIDTH * 5];
        uint64_t in6 = out_ptr[WIDTH * 6];
        uint64_t in7 = out_ptr[WIDTH * 7];

        out3 = in3 ^ in5;
        tmp0 = in1 ^ in5;
        tmp1 = tmp0 ^ in2;
        out4 = tmp1 ^ in0;
        tmp2 = tmp1 ^ in6;
        out2 = out4 ^ in3 ^ in7;
        out0 = tmp2 ^ in4;
        out5 = tmp2 ^ out3;
        out1 = tmp0 ^ out5 ^ in7;
        out6 = tmp0 ^ out2 ^ in4;
        out7 = tmp1 ^ out6 ^ in7;

        out_ptr[0] = out0 ^ in_ptr[0];
        out_ptr[WIDTH] = out1 ^ in_ptr[WIDTH];
        out_ptr[WIDTH * 2] = out2 ^ in_ptr[WIDTH * 2];
        out_ptr[WIDTH * 3] = out3 ^ in_ptr[WIDTH * 3];
        out_ptr[WIDTH * 4] = out4 ^ in_ptr[WIDTH * 4];
        out_ptr[WIDTH * 5] = out5 ^ in_ptr[WIDTH * 5];
        out_ptr[WIDTH * 6] = out6 ^ in_ptr[WIDTH * 6];
        out_ptr[WIDTH * 7] = out7 ^ in_ptr[WIDTH * 7];

        in_ptr++;
        out_ptr++;
    }
}

static void gf8_muladd_D5(void *out, void *in)
{
    unsigned int i;
    uint64_t *in_ptr = (uint64_t *)in;
    uint64_t *out_ptr = (uint64_t *)out;

    for (i = 0; i < WIDTH; i++) {
        uint64_t out0, out1, out2, out3, out4, out5, out6, out7;
        uint64_t tmp0, tmp1, tmp2;

        uint64_t in0 = out_ptr[0];
        uint64_t in1 = out_ptr[WIDTH];
        uint64_t in2 = out_ptr[WIDTH * 2];
        uint64_t in3 = out_ptr[WIDTH * 3];
        uint64_t in4 = out_ptr[WIDTH * 4];
        uint64_t in5 = out_ptr[WIDTH * 5];
        uint64_t in6 = out_ptr[WIDTH * 6];
        uint64_t in7 = out_ptr[WIDTH * 7];

        out3 = in5;
        tmp0 = in0 ^ in4;
        tmp1 = tmp0 ^ in1 ^ in5;
        out4 = tmp1 ^ in2;
        out0 = out4 ^ in6;
        tmp2 = tmp0 ^ out0;
        out5 = tmp2 ^ in3;
        out1 = out5 ^ in7;
        out6 = tmp1 ^ out1;
        out7 = tmp2 ^ out6;
        out2 = out7 ^ in4;

        out_ptr[0] = out0 ^ in_ptr[0];
        out_ptr[WIDTH] = out1 ^ in_ptr[WIDTH];
        out_ptr[WIDTH * 2] = out2 ^ in_ptr[WIDTH * 2];
        out_ptr[WIDTH * 3] = out3 ^ in_ptr[WIDTH * 3];
        out_ptr[WIDTH * 4] = out4 ^ in_ptr[WIDTH * 4];
        out_ptr[WIDTH * 5] = out5 ^ in_ptr[WIDTH * 5];
        out_ptr[WIDTH * 6] = out6 ^ in_ptr[WIDTH * 6];
        out_ptr[WIDTH * 7] = out7 ^ in_ptr[WIDTH * 7];

        in_ptr++;
        out_ptr++;
    }
}

static void gf8_muladd_D6(void *out, void *in)
{
    unsigned int i;
    uint64_t *in_ptr = (uint64_t *)in;
    uint64_t *out_ptr = (uint64_t *)out;

    for (i = 0; i < WIDTH; i++) {
        uint64_t out0, out1, out2, out3, out4, out5, out6, out7;
        uint64_t tmp0, tmp1;

        uint64_t in0 = out_ptr[0];
        uint64_t in1 = out_ptr[WIDTH];
        uint64_t in2 = out_ptr[WIDTH * 2];
        uint64_t in3 = out_ptr[WIDTH * 3];
        uint64_t in4 = out_ptr[WIDTH * 4];
        uint64_t in5 = out_ptr[WIDTH * 5];
        uint64_t in6 = out_ptr[WIDTH * 6];
        uint64_t in7 = out_ptr[WIDTH * 7];

        tmp0 = in1 ^ in2 ^ in4 ^ in6;
        out5 = tmp0 ^ in3;
        out0 = tmp0 ^ in5 ^ in7;
        out3 = out0 ^ out5 ^ in2;
        tmp1 = out3 ^ in0;
        out1 = tmp1 ^ in6;
        out2 = tmp1 ^ in7;
        out4 = tmp1 ^ in1;
        out6 = tmp1 ^ in4;
        out7 = tmp0 ^ out2;

        out_ptr[0] = out0 ^ in_ptr[0];
        out_ptr[WIDTH] = out1 ^ in_ptr[WIDTH];
        out_ptr[WIDTH * 2] = out2 ^ in_ptr[WIDTH * 2];
        out_ptr[WIDTH * 3] = out3 ^ in_ptr[WIDTH * 3];
        out_ptr[WIDTH * 4] = out4 ^ in_ptr[WIDTH * 4];
        out_ptr[WIDTH * 5] = out5 ^ in_ptr[WIDTH * 5];
        out_ptr[WIDTH * 6] = out6 ^ in_ptr[WIDTH * 6];
        out_ptr[WIDTH * 7] = out7 ^ in_ptr[WIDTH * 7];

        in_ptr++;
        out_ptr++;
    }
}

static void gf8_muladd_D7(void *out, void *in)
{
    unsigned int i;
    uint64_t *in_ptr = (uint64_t *)in;
    uint64_t *out_ptr = (uint64_t *)out;

    for (i = 0; i < WIDTH; i++) {
        uint64_t out0, out1, out2, out3, out4, out5, out6, out7;
        uint64_t tmp0, tmp1, tmp2;

        uint64_t in0 = out_ptr[0];
        uint64_t in1 = out_ptr[WIDTH];
        uint64_t in2 = out_ptr[WIDTH * 2];
        uint64_t in3 = out_ptr[WIDTH * 3];
        uint64_t in4 = out_ptr[WIDTH * 4];
        uint64_t in5 = out_ptr[WIDTH * 5];
        uint64_t in6 = out_ptr[WIDTH * 6];
        uint64_t in7 = out_ptr[WIDTH * 7];

        tmp0 = in0 ^ in3;
        out3 = in2 ^ in5 ^ in7;
        out2 = tmp0 ^ in5;
        tmp1 = tmp0 ^ out3 ^ in1;
        out1 = tmp1 ^ in6;
        out4 = tmp1 ^ in4;
        tmp2 = out1 ^ in4;
        out6 = tmp2 ^ in1;
        out7 = tmp2 ^ in2;
        out0 = tmp2 ^ in3;
        out5 = tmp2 ^ in0 ^ in7;

        out_ptr[0] = out0 ^ in_ptr[0];
        out_ptr[WIDTH] = out1 ^ in_ptr[WIDTH];
        out_ptr[WIDTH * 2] = out2 ^ in_ptr[WIDTH * 2];
        out_ptr[WIDTH * 3] = out3 ^ in_ptr[WIDTH * 3];
        out_ptr[WIDTH * 4] = out4 ^ in_ptr[WIDTH * 4];
        out_ptr[WIDTH * 5] = out5 ^ in_ptr[WIDTH * 5];
        out_ptr[WIDTH * 6] = out6 ^ in_ptr[WIDTH * 6];
        out_ptr[WIDTH * 7] = out7 ^ in_ptr[WIDTH * 7];

        in_ptr++;
        out_ptr++;
    }
}

static void gf8_muladd_D8(void *out, void *in)
{
    unsigned int i;
    uint64_t *in_ptr = (uint64_t *)in;
    uint64_t *out_ptr = (uint64_t *)out;

    for (i = 0; i < WIDTH; i++) {
        uint64_t out0, out1, out2, out3, out4, out5, out6, out7;
        uint64_t tmp0, tmp1;

        uint64_t in0 = out_ptr[0];
        uint64_t in1 = out_ptr[WIDTH];
        uint64_t in2 = out_ptr[WIDTH * 2];
        uint64_t in3 = out_ptr[WIDTH * 3];
        uint64_t in4 = out_ptr[WIDTH * 4];
        uint64_t in5 = out_ptr[WIDTH * 5];
        uint64_t in6 = out_ptr[WIDTH * 6];
        uint64_t in7 = out_ptr[WIDTH * 7];

        out4 = in0;
        out5 = in1;
        tmp0 = in1 ^ in2;
        out6 = in0 ^ in2;
        out0 = tmp0 ^ in4;
        tmp1 = tmp0 ^ in3;
        out7 = tmp1 ^ out6;
        out2 = tmp1 ^ in6;
        out3 = out7 ^ in7;
        out1 = tmp1 ^ in1 ^ in5;

        out_ptr[0] = out0 ^ in_ptr[0];
        out_ptr[WIDTH] = out1 ^ in_ptr[WIDTH];
        out_ptr[WIDTH * 2] = out2 ^ in_ptr[WIDTH * 2];
        out_ptr[WIDTH * 3] = out3 ^ in_ptr[WIDTH * 3];
        out_ptr[WIDTH * 4] = out4 ^ in_ptr[WIDTH * 4];
        out_ptr[WIDTH * 5] = out5 ^ in_ptr[WIDTH * 5];
        out_ptr[WIDTH * 6] = out6 ^ in_ptr[WIDTH * 6];
        out_ptr[WIDTH * 7] = out7 ^ in_ptr[WIDTH * 7];

        in_ptr++;
        out_ptr++;
    }
}

static void gf8_muladd_D9(void *out, void *in)
{
    unsigned int i;
    uint64_t *in_ptr = (uint64_t *)in;
    uint64_t *out_ptr = (uint64_t *)out;

    for (i = 0; i < WIDTH; i++) {
        uint64_t out0, out1, out2, out3, out4, out5, out6, out7;

        uint64_t in0 = out_ptr[0];
        uint64_t in1 = out_ptr[WIDTH];
        uint64_t in2 = out_ptr[WIDTH * 2];
        uint64_t in3 = out_ptr[WIDTH * 3];
        uint64_t in4 = out_ptr[WIDTH * 4];
        uint64_t in5 = out_ptr[WIDTH * 5];
        uint64_t in6 = out_ptr[WIDTH * 6];
        uint64_t in7 = out_ptr[WIDTH * 7];

        out4 = in0 ^ in4;
        out5 = in1 ^ in5;
        out2 = in1 ^ in3 ^ in6;
        out3 = in0 ^ in1 ^ in7;
        out6 = in0 ^ in2 ^ in6;
        out0 = out4 ^ in1 ^ in2;
        out1 = out5 ^ in2 ^ in3;
        out7 = out3 ^ in3;

        out_ptr[0] = out0 ^ in_ptr[0];
        out_ptr[WIDTH] = out1 ^ in_ptr[WIDTH];
        out_ptr[WIDTH * 2] = out2 ^ in_ptr[WIDTH * 2];
        out_ptr[WIDTH * 3] = out3 ^ in_ptr[WIDTH * 3];
        out_ptr[WIDTH * 4] = out4 ^ in_ptr[WIDTH * 4];
        out_ptr[WIDTH * 5] = out5 ^ in_ptr[WIDTH * 5];
        out_ptr[WIDTH * 6] = out6 ^ in_ptr[WIDTH * 6];
        out_ptr[WIDTH * 7] = out7 ^ in_ptr[WIDTH * 7];

        in_ptr++;
        out_ptr++;
    }
}

static void gf8_muladd_DA(void *out, void *in)
{
    unsigned int i;
    uint64_t *in_ptr = (uint64_t *)in;
    uint64_t *out_ptr = (uint64_t *)out;

    for (i = 0; i < WIDTH; i++) {
        uint64_t out0, out1, out2, out3, out4, out5, out6, out7;
        uint64_t tmp0, tmp1;

        uint64_t in0 = out_ptr[0];
        uint64_t in1 = out_ptr[WIDTH];
        uint64_t in2 = out_ptr[WIDTH * 2];
        uint64_t in3 = out_ptr[WIDTH * 3];
        uint64_t in4 = out_ptr[WIDTH * 4];
        uint64_t in5 = out_ptr[WIDTH * 5];
        uint64_t in6 = out_ptr[WIDTH * 6];
        uint64_t in7 = out_ptr[WIDTH * 7];

        out5 = in1 ^ in4;
        tmp0 = in2 ^ in7;
        tmp1 = in0 ^ in2 ^ in3;
        out0 = tmp0 ^ out5;
        out4 = tmp0 ^ tmp1;
        out2 = tmp0 ^ in3 ^ in6;
        out1 = tmp1 ^ in5;
        out3 = tmp1 ^ in1;
        out6 = out1 ^ in3;
        out7 = out3 ^ in2 ^ in6;

        out_ptr[0] = out0 ^ in_ptr[0];
        out_ptr[WIDTH] = out1 ^ in_ptr[WIDTH];
        out_ptr[WIDTH * 2] = out2 ^ in_ptr[WIDTH * 2];
        out_ptr[WIDTH * 3] = out3 ^ in_ptr[WIDTH * 3];
        out_ptr[WIDTH * 4] = out4 ^ in_ptr[WIDTH * 4];
        out_ptr[WIDTH * 5] = out5 ^ in_ptr[WIDTH * 5];
        out_ptr[WIDTH * 6] = out6 ^ in_ptr[WIDTH * 6];
        out_ptr[WIDTH * 7] = out7 ^ in_ptr[WIDTH * 7];

        in_ptr++;
        out_ptr++;
    }
}

static void gf8_muladd_DB(void *out, void *in)
{
    unsigned int i;
    uint64_t *in_ptr = (uint64_t *)in;
    uint64_t *out_ptr = (uint64_t *)out;

    for (i = 0; i < WIDTH; i++) {
        uint64_t out0, out1, out2, out3, out4, out5, out6, out7;
        uint64_t tmp0, tmp1, tmp2, tmp3, tmp4;

        uint64_t in0 = out_ptr[0];
        uint64_t in1 = out_ptr[WIDTH];
        uint64_t in2 = out_ptr[WIDTH * 2];
        uint64_t in3 = out_ptr[WIDTH * 3];
        uint64_t in4 = out_ptr[WIDTH * 4];
        uint64_t in5 = out_ptr[WIDTH * 5];
        uint64_t in6 = out_ptr[WIDTH * 6];
        uint64_t in7 = out_ptr[WIDTH * 7];

        tmp0 = in0 ^ in1;
        tmp1 = in1 ^ in5;
        tmp2 = in3 ^ in7;
        out3 = tmp0 ^ in2;
        out5 = tmp1 ^ in4;
        out6 = tmp1 ^ out3 ^ in6;
        out2 = tmp2 ^ in6;
        tmp3 = tmp2 ^ in4;
        tmp4 = out3 ^ in3;
        out4 = tmp3 ^ in0;
        out1 = tmp4 ^ in5;
        out0 = tmp3 ^ tmp4;
        out7 = tmp0 ^ out2;

        out_ptr[0] = out0 ^ in_ptr[0];
        out_ptr[WIDTH] = out1 ^ in_ptr[WIDTH];
        out_ptr[WIDTH * 2] = out2 ^ in_ptr[WIDTH * 2];
        out_ptr[WIDTH * 3] = out3 ^ in_ptr[WIDTH * 3];
        out_ptr[WIDTH * 4] = out4 ^ in_ptr[WIDTH * 4];
        out_ptr[WIDTH * 5] = out5 ^ in_ptr[WIDTH * 5];
        out_ptr[WIDTH * 6] = out6 ^ in_ptr[WIDTH * 6];
        out_ptr[WIDTH * 7] = out7 ^ in_ptr[WIDTH * 7];

        in_ptr++;
        out_ptr++;
    }
}

static void gf8_muladd_DC(void *out, void *in)
{
    unsigned int i;
    uint64_t *in_ptr = (uint64_t *)in;
    uint64_t *out_ptr = (uint64_t *)out;

    for (i = 0; i < WIDTH; i++) {
        uint64_t out0, out1, out2, out3, out4, out5, out6, out7;
        uint64_t tmp0, tmp1, tmp2, tmp3;

        uint64_t in0 = out_ptr[0];
        uint64_t in1 = out_ptr[WIDTH];
        uint64_t in2 = out_ptr[WIDTH * 2];
        uint64_t in3 = out_ptr[WIDTH * 3];
        uint64_t in4 = out_ptr[WIDTH * 4];
        uint64_t in5 = out_ptr[WIDTH * 5];
        uint64_t in6 = out_ptr[WIDTH * 6];
        uint64_t in7 = out_ptr[WIDTH * 7];

        tmp0 = in0 ^ in2;
        tmp1 = in0 ^ in3;
        out6 = tmp0 ^ in4;
        tmp2 = tmp0 ^ in7;
        out3 = tmp1 ^ in6;
        tmp3 = tmp1 ^ in1;
        out1 = tmp1 ^ tmp2 ^ in5;
        out4 = tmp2 ^ in6;
        out2 = tmp3 ^ in2;
        out7 = tmp3 ^ in5;
        out5 = tmp2 ^ out2;
        out0 = out2 ^ out3 ^ in4;

        out_ptr[0] = out0 ^ in_ptr[0];
        out_ptr[WIDTH] = out1 ^ in_ptr[WIDTH];
        out_ptr[WIDTH * 2] = out2 ^ in_ptr[WIDTH * 2];
        out_ptr[WIDTH * 3] = out3 ^ in_ptr[WIDTH * 3];
        out_ptr[WIDTH * 4] = out4 ^ in_ptr[WIDTH * 4];
        out_ptr[WIDTH * 5] = out5 ^ in_ptr[WIDTH * 5];
        out_ptr[WIDTH * 6] = out6 ^ in_ptr[WIDTH * 6];
        out_ptr[WIDTH * 7] = out7 ^ in_ptr[WIDTH * 7];

        in_ptr++;
        out_ptr++;
    }
}

static void gf8_muladd_DD(void *out, void *in)
{
    unsigned int i;
    uint64_t *in_ptr = (uint64_t *)in;
    uint64_t *out_ptr = (uint64_t *)out;

    for (i = 0; i < WIDTH; i++) {
        uint64_t out0, out1, out2, out3, out4, out5, out6, out7;

        uint64_t in0 = out_ptr[0];
        uint64_t in1 = out_ptr[WIDTH];
        uint64_t in2 = out_ptr[WIDTH * 2];
        uint64_t in3 = out_ptr[WIDTH * 3];
        uint64_t in4 = out_ptr[WIDTH * 4];
        uint64_t in5 = out_ptr[WIDTH * 5];
        uint64_t in6 = out_ptr[WIDTH * 6];
        uint64_t in7 = out_ptr[WIDTH * 7];

        out3 = in0 ^ in6;
        out2 = in0 ^ in1 ^ in3;
        out6 = out3 ^ in2 ^ in4;
        out7 = out2 ^ in5 ^ in7;
        out0 = out6 ^ in1;
        out4 = out6 ^ in7;
        out5 = out7 ^ in0;
        out1 = out5 ^ in2;

        out_ptr[0] = out0 ^ in_ptr[0];
        out_ptr[WIDTH] = out1 ^ in_ptr[WIDTH];
        out_ptr[WIDTH * 2] = out2 ^ in_ptr[WIDTH * 2];
        out_ptr[WIDTH * 3] = out3 ^ in_ptr[WIDTH * 3];
        out_ptr[WIDTH * 4] = out4 ^ in_ptr[WIDTH * 4];
        out_ptr[WIDTH * 5] = out5 ^ in_ptr[WIDTH * 5];
        out_ptr[WIDTH * 6] = out6 ^ in_ptr[WIDTH * 6];
        out_ptr[WIDTH * 7] = out7 ^ in_ptr[WIDTH * 7];

        in_ptr++;
        out_ptr++;
    }
}

static void gf8_muladd_DE(void *out, void *in)
{
    unsigned int i;
    uint64_t *in_ptr = (uint64_t *)in;
    uint64_t *out_ptr = (uint64_t *)out;

    for (i = 0; i < WIDTH; i++) {
        uint64_t out0, out1, out2, out3, out4, out5, out6, out7;
        uint64_t tmp0, tmp1;

        uint64_t in0 = out_ptr[0];
        uint64_t in1 = out_ptr[WIDTH];
        uint64_t in2 = out_ptr[WIDTH * 2];
        uint64_t in3 = out_ptr[WIDTH * 3];
        uint64_t in4 = out_ptr[WIDTH * 4];
        uint64_t in5 = out_ptr[WIDTH * 5];
        uint64_t in6 = out_ptr[WIDTH * 6];
        uint64_t in7 = out_ptr[WIDTH * 7];

        tmp0 = in2 ^ in3 ^ in6;
        tmp1 = in3 ^ in4 ^ in7;
        out4 = tmp0 ^ in0;
        out5 = tmp1 ^ in1;
        out3 = out4 ^ in7;
        out2 = out3 ^ in6;
        out1 = out2 ^ in5;
        out6 = tmp1 ^ out1;
        out0 = tmp0 ^ out5;
        out7 = out0 ^ out1 ^ in4;

        out_ptr[0] = out0 ^ in_ptr[0];
        out_ptr[WIDTH] = out1 ^ in_ptr[WIDTH];
        out_ptr[WIDTH * 2] = out2 ^ in_ptr[WIDTH * 2];
        out_ptr[WIDTH * 3] = out3 ^ in_ptr[WIDTH * 3];
        out_ptr[WIDTH * 4] = out4 ^ in_ptr[WIDTH * 4];
        out_ptr[WIDTH * 5] = out5 ^ in_ptr[WIDTH * 5];
        out_ptr[WIDTH * 6] = out6 ^ in_ptr[WIDTH * 6];
        out_ptr[WIDTH * 7] = out7 ^ in_ptr[WIDTH * 7];

        in_ptr++;
        out_ptr++;
    }
}

static void gf8_muladd_DF(void *out, void *in)
{
    unsigned int i;
    uint64_t *in_ptr = (uint64_t *)in;
    uint64_t *out_ptr = (uint64_t *)out;

    for (i = 0; i < WIDTH; i++) {
        uint64_t out0, out1, out2, out3, out4, out5, out6, out7;
        uint64_t tmp0, tmp1, tmp2;

        uint64_t in0 = out_ptr[0];
        uint64_t in1 = out_ptr[WIDTH];
        uint64_t in2 = out_ptr[WIDTH * 2];
        uint64_t in3 = out_ptr[WIDTH * 3];
        uint64_t in4 = out_ptr[WIDTH * 4];
        uint64_t in5 = out_ptr[WIDTH * 5];
        uint64_t in6 = out_ptr[WIDTH * 6];
        uint64_t in7 = out_ptr[WIDTH * 7];

        out2 = in0 ^ in3 ^ in7;
        tmp0 = out2 ^ in1 ^ in5;
        out1 = tmp0 ^ in2;
        out7 = tmp0 ^ in6;
        out5 = tmp0 ^ in0 ^ in4;
        tmp1 = out1 ^ out5 ^ in6;
        out4 = tmp1 ^ in3;
        out6 = tmp1 ^ in5;
        tmp2 = tmp1 ^ in7;
        out0 = tmp2 ^ in1;
        out3 = tmp2 ^ in4;

        out_ptr[0] = out0 ^ in_ptr[0];
        out_ptr[WIDTH] = out1 ^ in_ptr[WIDTH];
        out_ptr[WIDTH * 2] = out2 ^ in_ptr[WIDTH * 2];
        out_ptr[WIDTH * 3] = out3 ^ in_ptr[WIDTH * 3];
        out_ptr[WIDTH * 4] = out4 ^ in_ptr[WIDTH * 4];
        out_ptr[WIDTH * 5] = out5 ^ in_ptr[WIDTH * 5];
        out_ptr[WIDTH * 6] = out6 ^ in_ptr[WIDTH * 6];
        out_ptr[WIDTH * 7] = out7 ^ in_ptr[WIDTH * 7];

        in_ptr++;
        out_ptr++;
    }
}

static void gf8_muladd_E0(void *out, void *in)
{
    unsigned int i;
    uint64_t *in_ptr = (uint64_t *)in;
    uint64_t *out_ptr = (uint64_t *)out;

    for (i = 0; i < WIDTH; i++) {
        uint64_t out0, out1, out2, out3, out4, out5, out6, out7;
        uint64_t tmp0, tmp1;

        uint64_t in0 = out_ptr[0];
        uint64_t in1 = out_ptr[WIDTH];
        uint64_t in2 = out_ptr[WIDTH * 2];
        uint64_t in3 = out_ptr[WIDTH * 3];
        uint64_t in4 = out_ptr[WIDTH * 4];
        uint64_t in5 = out_ptr[WIDTH * 5];
        uint64_t in6 = out_ptr[WIDTH * 6];
        uint64_t in7 = out_ptr[WIDTH * 7];

        out3 = in1 ^ in7;
        tmp0 = in2 ^ in4;
        out4 = out3 ^ in3 ^ in5;
        out2 = tmp0 ^ in1;
        tmp1 = tmp0 ^ in6;
        out0 = out4 ^ in2;
        out6 = out4 ^ in0;
        out1 = tmp1 ^ in3;
        out5 = tmp1 ^ in0;
        out7 = out5 ^ in1;

        out_ptr[0] = out0 ^ in_ptr[0];
        out_ptr[WIDTH] = out1 ^ in_ptr[WIDTH];
        out_ptr[WIDTH * 2] = out2 ^ in_ptr[WIDTH * 2];
        out_ptr[WIDTH * 3] = out3 ^ in_ptr[WIDTH * 3];
        out_ptr[WIDTH * 4] = out4 ^ in_ptr[WIDTH * 4];
        out_ptr[WIDTH * 5] = out5 ^ in_ptr[WIDTH * 5];
        out_ptr[WIDTH * 6] = out6 ^ in_ptr[WIDTH * 6];
        out_ptr[WIDTH * 7] = out7 ^ in_ptr[WIDTH * 7];

        in_ptr++;
        out_ptr++;
    }
}

static void gf8_muladd_E1(void *out, void *in)
{
    unsigned int i;
    uint64_t *in_ptr = (uint64_t *)in;
    uint64_t *out_ptr = (uint64_t *)out;

    for (i = 0; i < WIDTH; i++) {
        uint64_t out0, out1, out2, out3, out4, out5, out6, out7;
        uint64_t tmp0, tmp1, tmp2, tmp3;

        uint64_t in0 = out_ptr[0];
        uint64_t in1 = out_ptr[WIDTH];
        uint64_t in2 = out_ptr[WIDTH * 2];
        uint64_t in3 = out_ptr[WIDTH * 3];
        uint64_t in4 = out_ptr[WIDTH * 4];
        uint64_t in5 = out_ptr[WIDTH * 5];
        uint64_t in6 = out_ptr[WIDTH * 6];
        uint64_t in7 = out_ptr[WIDTH * 7];

        out2 = in1 ^ in4;
        tmp0 = in1 ^ in7;
        out3 = tmp0 ^ in3;
        tmp1 = out3 ^ in5;
        out4 = tmp1 ^ in4;
        tmp2 = tmp1 ^ in0;
        out0 = tmp2 ^ in2;
        out6 = tmp2 ^ in6;
        tmp3 = out0 ^ out4 ^ in6;
        out5 = tmp3 ^ in5;
        out7 = tmp0 ^ tmp3;
        out1 = tmp2 ^ out5 ^ in7;

        out_ptr[0] = out0 ^ in_ptr[0];
        out_ptr[WIDTH] = out1 ^ in_ptr[WIDTH];
        out_ptr[WIDTH * 2] = out2 ^ in_ptr[WIDTH * 2];
        out_ptr[WIDTH * 3] = out3 ^ in_ptr[WIDTH * 3];
        out_ptr[WIDTH * 4] = out4 ^ in_ptr[WIDTH * 4];
        out_ptr[WIDTH * 5] = out5 ^ in_ptr[WIDTH * 5];
        out_ptr[WIDTH * 6] = out6 ^ in_ptr[WIDTH * 6];
        out_ptr[WIDTH * 7] = out7 ^ in_ptr[WIDTH * 7];

        in_ptr++;
        out_ptr++;
    }
}

static void gf8_muladd_E2(void *out, void *in)
{
    unsigned int i;
    uint64_t *in_ptr = (uint64_t *)in;
    uint64_t *out_ptr = (uint64_t *)out;

    for (i = 0; i < WIDTH; i++) {
        uint64_t out0, out1, out2, out3, out4, out5, out6, out7;

        uint64_t in0 = out_ptr[0];
        uint64_t in1 = out_ptr[WIDTH];
        uint64_t in2 = out_ptr[WIDTH * 2];
        uint64_t in3 = out_ptr[WIDTH * 3];
        uint64_t in4 = out_ptr[WIDTH * 4];
        uint64_t in5 = out_ptr[WIDTH * 5];
        uint64_t in6 = out_ptr[WIDTH * 6];
        uint64_t in7 = out_ptr[WIDTH * 7];

        out3 = in1 ^ in2;
        out4 = in1 ^ in5;
        out2 = in2 ^ in4 ^ in7;
        out5 = in0 ^ in2 ^ in6;
        out0 = out3 ^ in3 ^ in5;
        out7 = out3 ^ in0 ^ in4;
        out6 = out2 ^ out7 ^ in3;
        out1 = out5 ^ in3 ^ in4;

        out_ptr[0] = out0 ^ in_ptr[0];
        out_ptr[WIDTH] = out1 ^ in_ptr[WIDTH];
        out_ptr[WIDTH * 2] = out2 ^ in_ptr[WIDTH * 2];
        out_ptr[WIDTH * 3] = out3 ^ in_ptr[WIDTH * 3];
        out_ptr[WIDTH * 4] = out4 ^ in_ptr[WIDTH * 4];
        out_ptr[WIDTH * 5] = out5 ^ in_ptr[WIDTH * 5];
        out_ptr[WIDTH * 6] = out6 ^ in_ptr[WIDTH * 6];
        out_ptr[WIDTH * 7] = out7 ^ in_ptr[WIDTH * 7];

        in_ptr++;
        out_ptr++;
    }
}

static void gf8_muladd_E3(void *out, void *in)
{
    unsigned int i;
    uint64_t *in_ptr = (uint64_t *)in;
    uint64_t *out_ptr = (uint64_t *)out;

    for (i = 0; i < WIDTH; i++) {
        uint64_t out0, out1, out2, out3, out4, out5, out6, out7;
        uint64_t tmp0, tmp1, tmp2, tmp3, tmp4;

        uint64_t in0 = out_ptr[0];
        uint64_t in1 = out_ptr[WIDTH];
        uint64_t in2 = out_ptr[WIDTH * 2];
        uint64_t in3 = out_ptr[WIDTH * 3];
        uint64_t in4 = out_ptr[WIDTH * 4];
        uint64_t in5 = out_ptr[WIDTH * 5];
        uint64_t in6 = out_ptr[WIDTH * 6];
        uint64_t in7 = out_ptr[WIDTH * 7];

        out2 = in4 ^ in7;
        tmp0 = in1 ^ in3;
        out3 = tmp0 ^ in2;
        tmp1 = out3 ^ in0;
        out0 = tmp1 ^ in5;
        tmp2 = tmp1 ^ in4;
        out1 = tmp2 ^ in6;
        tmp3 = tmp2 ^ in3;
        out7 = tmp3 ^ in7;
        out6 = out1 ^ out2 ^ in2;
        tmp4 = tmp0 ^ out0;
        out5 = tmp4 ^ in6;
        out4 = tmp3 ^ tmp4;

        out_ptr[0] = out0 ^ in_ptr[0];
        out_ptr[WIDTH] = out1 ^ in_ptr[WIDTH];
        out_ptr[WIDTH * 2] = out2 ^ in_ptr[WIDTH * 2];
        out_ptr[WIDTH * 3] = out3 ^ in_ptr[WIDTH * 3];
        out_ptr[WIDTH * 4] = out4 ^ in_ptr[WIDTH * 4];
        out_ptr[WIDTH * 5] = out5 ^ in_ptr[WIDTH * 5];
        out_ptr[WIDTH * 6] = out6 ^ in_ptr[WIDTH * 6];
        out_ptr[WIDTH * 7] = out7 ^ in_ptr[WIDTH * 7];

        in_ptr++;
        out_ptr++;
    }
}

static void gf8_muladd_E4(void *out, void *in)
{
    unsigned int i;
    uint64_t *in_ptr = (uint64_t *)in;
    uint64_t *out_ptr = (uint64_t *)out;

    for (i = 0; i < WIDTH; i++) {
        uint64_t out0, out1, out2, out3, out4, out5, out6, out7;
        uint64_t tmp0, tmp1, tmp2;

        uint64_t in0 = out_ptr[0];
        uint64_t in1 = out_ptr[WIDTH];
        uint64_t in2 = out_ptr[WIDTH * 2];
        uint64_t in3 = out_ptr[WIDTH * 3];
        uint64_t in4 = out_ptr[WIDTH * 4];
        uint64_t in5 = out_ptr[WIDTH * 5];
        uint64_t in6 = out_ptr[WIDTH * 6];
        uint64_t in7 = out_ptr[WIDTH * 7];

        out3 = in6;
        tmp0 = in0 ^ in4;
        tmp1 = tmp0 ^ in2 ^ in6;
        out2 = tmp1 ^ in1;
        out7 = out2 ^ in5;
        tmp2 = tmp0 ^ out7;
        out4 = tmp2 ^ in3;
        out0 = out4 ^ in7;
        out6 = tmp1 ^ out0;
        out5 = tmp2 ^ out6;
        out1 = out5 ^ in0;

        out_ptr[0] = out0 ^ in_ptr[0];
        out_ptr[WIDTH] = out1 ^ in_ptr[WIDTH];
        out_ptr[WIDTH * 2] = out2 ^ in_ptr[WIDTH * 2];
        out_ptr[WIDTH * 3] = out3 ^ in_ptr[WIDTH * 3];
        out_ptr[WIDTH * 4] = out4 ^ in_ptr[WIDTH * 4];
        out_ptr[WIDTH * 5] = out5 ^ in_ptr[WIDTH * 5];
        out_ptr[WIDTH * 6] = out6 ^ in_ptr[WIDTH * 6];
        out_ptr[WIDTH * 7] = out7 ^ in_ptr[WIDTH * 7];

        in_ptr++;
        out_ptr++;
    }
}

static void gf8_muladd_E5(void *out, void *in)
{
    unsigned int i;
    uint64_t *in_ptr = (uint64_t *)in;
    uint64_t *out_ptr = (uint64_t *)out;

    for (i = 0; i < WIDTH; i++) {
        uint64_t out0, out1, out2, out3, out4, out5, out6, out7;
        uint64_t tmp0, tmp1, tmp2;

        uint64_t in0 = out_ptr[0];
        uint64_t in1 = out_ptr[WIDTH];
        uint64_t in2 = out_ptr[WIDTH * 2];
        uint64_t in3 = out_ptr[WIDTH * 3];
        uint64_t in4 = out_ptr[WIDTH * 4];
        uint64_t in5 = out_ptr[WIDTH * 5];
        uint64_t in6 = out_ptr[WIDTH * 6];
        uint64_t in7 = out_ptr[WIDTH * 7];

        out3 = in3 ^ in6;
        tmp0 = in0 ^ in1;
        tmp1 = in5 ^ in7;
        out2 = tmp0 ^ in4 ^ in6;
        tmp2 = tmp1 ^ out2;
        out6 = tmp2 ^ in3;
        out7 = tmp2 ^ in2;
        out0 = out6 ^ in2 ^ in4;
        out5 = out6 ^ in1 ^ in2;
        out1 = tmp0 ^ out5 ^ in5;
        out4 = tmp1 ^ out1;

        out_ptr[0] = out0 ^ in_ptr[0];
        out_ptr[WIDTH] = out1 ^ in_ptr[WIDTH];
        out_ptr[WIDTH * 2] = out2 ^ in_ptr[WIDTH * 2];
        out_ptr[WIDTH * 3] = out3 ^ in_ptr[WIDTH * 3];
        out_ptr[WIDTH * 4] = out4 ^ in_ptr[WIDTH * 4];
        out_ptr[WIDTH * 5] = out5 ^ in_ptr[WIDTH * 5];
        out_ptr[WIDTH * 6] = out6 ^ in_ptr[WIDTH * 6];
        out_ptr[WIDTH * 7] = out7 ^ in_ptr[WIDTH * 7];

        in_ptr++;
        out_ptr++;
    }
}

static void gf8_muladd_E6(void *out, void *in)
{
    unsigned int i;
    uint64_t *in_ptr = (uint64_t *)in;
    uint64_t *out_ptr = (uint64_t *)out;

    for (i = 0; i < WIDTH; i++) {
        uint64_t out0, out1, out2, out3, out4, out5, out6, out7;

        uint64_t in0 = out_ptr[0];
        uint64_t in1 = out_ptr[WIDTH];
        uint64_t in2 = out_ptr[WIDTH * 2];
        uint64_t in3 = out_ptr[WIDTH * 3];
        uint64_t in4 = out_ptr[WIDTH * 4];
        uint64_t in5 = out_ptr[WIDTH * 5];
        uint64_t in6 = out_ptr[WIDTH * 6];
        uint64_t in7 = out_ptr[WIDTH * 7];

        out3 = in2 ^ in6 ^ in7;
        out2 = out3 ^ in0 ^ in4;
        out4 = out3 ^ in1 ^ in5;
        out1 = out2 ^ in3;
        out7 = out2 ^ out4 ^ in2;
        out0 = out4 ^ in3 ^ in7;
        out5 = out1 ^ in4;
        out6 = out0 ^ out2 ^ in5;

        out_ptr[0] = out0 ^ in_ptr[0];
        out_ptr[WIDTH] = out1 ^ in_ptr[WIDTH];
        out_ptr[WIDTH * 2] = out2 ^ in_ptr[WIDTH * 2];
        out_ptr[WIDTH * 3] = out3 ^ in_ptr[WIDTH * 3];
        out_ptr[WIDTH * 4] = out4 ^ in_ptr[WIDTH * 4];
        out_ptr[WIDTH * 5] = out5 ^ in_ptr[WIDTH * 5];
        out_ptr[WIDTH * 6] = out6 ^ in_ptr[WIDTH * 6];
        out_ptr[WIDTH * 7] = out7 ^ in_ptr[WIDTH * 7];

        in_ptr++;
        out_ptr++;
    }
}

static void gf8_muladd_E7(void *out, void *in)
{
    unsigned int i;
    uint64_t *in_ptr = (uint64_t *)in;
    uint64_t *out_ptr = (uint64_t *)out;

    for (i = 0; i < WIDTH; i++) {
        uint64_t out0, out1, out2, out3, out4, out5, out6, out7;
        uint64_t tmp0, tmp1, tmp2, tmp3, tmp4;

        uint64_t in0 = out_ptr[0];
        uint64_t in1 = out_ptr[WIDTH];
        uint64_t in2 = out_ptr[WIDTH * 2];
        uint64_t in3 = out_ptr[WIDTH * 3];
        uint64_t in4 = out_ptr[WIDTH * 4];
        uint64_t in5 = out_ptr[WIDTH * 5];
        uint64_t in6 = out_ptr[WIDTH * 6];
        uint64_t in7 = out_ptr[WIDTH * 7];

        tmp0 = in2 ^ in3;
        out3 = tmp0 ^ in6 ^ in7;
        tmp1 = out3 ^ in0;
        out5 = tmp1 ^ in5;
        tmp2 = tmp1 ^ in4;
        tmp3 = out5 ^ in7;
        out1 = tmp2 ^ in1;
        out0 = tmp3 ^ in1;
        out6 = out1 ^ in2;
        out2 = tmp0 ^ tmp2;
        tmp4 = tmp3 ^ out6;
        out4 = tmp4 ^ in6;
        out7 = tmp4 ^ in0;

        out_ptr[0] = out0 ^ in_ptr[0];
        out_ptr[WIDTH] = out1 ^ in_ptr[WIDTH];
        out_ptr[WIDTH * 2] = out2 ^ in_ptr[WIDTH * 2];
        out_ptr[WIDTH * 3] = out3 ^ in_ptr[WIDTH * 3];
        out_ptr[WIDTH * 4] = out4 ^ in_ptr[WIDTH * 4];
        out_ptr[WIDTH * 5] = out5 ^ in_ptr[WIDTH * 5];
        out_ptr[WIDTH * 6] = out6 ^ in_ptr[WIDTH * 6];
        out_ptr[WIDTH * 7] = out7 ^ in_ptr[WIDTH * 7];

        in_ptr++;
        out_ptr++;
    }
}

static void gf8_muladd_E8(void *out, void *in)
{
    unsigned int i;
    uint64_t *in_ptr = (uint64_t *)in;
    uint64_t *out_ptr = (uint64_t *)out;

    for (i = 0; i < WIDTH; i++) {
        uint64_t out0, out1, out2, out3, out4, out5, out6, out7;
        uint64_t tmp0, tmp1, tmp2, tmp3;

        uint64_t in0 = out_ptr[0];
        uint64_t in1 = out_ptr[WIDTH];
        uint64_t in2 = out_ptr[WIDTH * 2];
        uint64_t in3 = out_ptr[WIDTH * 3];
        uint64_t in4 = out_ptr[WIDTH * 4];
        uint64_t in5 = out_ptr[WIDTH * 5];
        uint64_t in6 = out_ptr[WIDTH * 6];
        uint64_t in7 = out_ptr[WIDTH * 7];

        out4 = in3 ^ in6;
        tmp0 = in4 ^ in7;
        out1 = in2 ^ in3 ^ in4;
        out5 = tmp0 ^ in0;
        tmp1 = tmp0 ^ in1;
        tmp2 = tmp1 ^ in5;
        out0 = tmp1 ^ out1;
        out2 = tmp2 ^ in2;
        out6 = tmp2 ^ out5;
        tmp3 = out6 ^ in6;
        out3 = tmp3 ^ in7;
        out7 = tmp3 ^ in2 ^ in5;

        out_ptr[0] = out0 ^ in_ptr[0];
        out_ptr[WIDTH] = out1 ^ in_ptr[WIDTH];
        out_ptr[WIDTH * 2] = out2 ^ in_ptr[WIDTH * 2];
        out_ptr[WIDTH * 3] = out3 ^ in_ptr[WIDTH * 3];
        out_ptr[WIDTH * 4] = out4 ^ in_ptr[WIDTH * 4];
        out_ptr[WIDTH * 5] = out5 ^ in_ptr[WIDTH * 5];
        out_ptr[WIDTH * 6] = out6 ^ in_ptr[WIDTH * 6];
        out_ptr[WIDTH * 7] = out7 ^ in_ptr[WIDTH * 7];

        in_ptr++;
        out_ptr++;
    }
}

static void gf8_muladd_E9(void *out, void *in)
{
    unsigned int i;
    uint64_t *in_ptr = (uint64_t *)in;
    uint64_t *out_ptr = (uint64_t *)out;

    for (i = 0; i < WIDTH; i++) {
        uint64_t out0, out1, out2, out3, out4, out5, out6, out7;
        uint64_t tmp0, tmp1, tmp2;

        uint64_t in0 = out_ptr[0];
        uint64_t in1 = out_ptr[WIDTH];
        uint64_t in2 = out_ptr[WIDTH * 2];
        uint64_t in3 = out_ptr[WIDTH * 3];
        uint64_t in4 = out_ptr[WIDTH * 4];
        uint64_t in5 = out_ptr[WIDTH * 5];
        uint64_t in6 = out_ptr[WIDTH * 6];
        uint64_t in7 = out_ptr[WIDTH * 7];

        tmp0 = in0 ^ in1;
        tmp1 = in3 ^ in6;
        tmp2 = tmp0 ^ in6;
        out4 = tmp1 ^ in4;
        out6 = tmp2 ^ in5;
        out7 = tmp2 ^ in2 ^ in7;
        out3 = out6 ^ in3 ^ in7;
        out0 = tmp1 ^ out7;
        out2 = out3 ^ out4 ^ in0;
        out5 = tmp0 ^ out2;
        out1 = out0 ^ out5 ^ in5;

        out_ptr[0] = out0 ^ in_ptr[0];
        out_ptr[WIDTH] = out1 ^ in_ptr[WIDTH];
        out_ptr[WIDTH * 2] = out2 ^ in_ptr[WIDTH * 2];
        out_ptr[WIDTH * 3] = out3 ^ in_ptr[WIDTH * 3];
        out_ptr[WIDTH * 4] = out4 ^ in_ptr[WIDTH * 4];
        out_ptr[WIDTH * 5] = out5 ^ in_ptr[WIDTH * 5];
        out_ptr[WIDTH * 6] = out6 ^ in_ptr[WIDTH * 6];
        out_ptr[WIDTH * 7] = out7 ^ in_ptr[WIDTH * 7];

        in_ptr++;
        out_ptr++;
    }
}

static void gf8_muladd_EA(void *out, void *in)
{
    unsigned int i;
    uint64_t *in_ptr = (uint64_t *)in;
    uint64_t *out_ptr = (uint64_t *)out;

    for (i = 0; i < WIDTH; i++) {
        uint64_t out0, out1, out2, out3, out4, out5, out6, out7;

        uint64_t in0 = out_ptr[0];
        uint64_t in1 = out_ptr[WIDTH];
        uint64_t in2 = out_ptr[WIDTH * 2];
        uint64_t in3 = out_ptr[WIDTH * 3];
        uint64_t in4 = out_ptr[WIDTH * 4];
        uint64_t in5 = out_ptr[WIDTH * 5];
        uint64_t in6 = out_ptr[WIDTH * 6];
        uint64_t in7 = out_ptr[WIDTH * 7];

        out4 = in6 ^ in7;
        out5 = in0 ^ in7;
        out6 = in0 ^ in1;
        out0 = in1 ^ in2 ^ in3;
        out2 = in2 ^ in4 ^ in5;
        out7 = out6 ^ in2;
        out1 = out0 ^ out6 ^ in4;
        out3 = out7 ^ in5 ^ in6;

        out_ptr[0] = out0 ^ in_ptr[0];
        out_ptr[WIDTH] = out1 ^ in_ptr[WIDTH];
        out_ptr[WIDTH * 2] = out2 ^ in_ptr[WIDTH * 2];
        out_ptr[WIDTH * 3] = out3 ^ in_ptr[WIDTH * 3];
        out_ptr[WIDTH * 4] = out4 ^ in_ptr[WIDTH * 4];
        out_ptr[WIDTH * 5] = out5 ^ in_ptr[WIDTH * 5];
        out_ptr[WIDTH * 6] = out6 ^ in_ptr[WIDTH * 6];
        out_ptr[WIDTH * 7] = out7 ^ in_ptr[WIDTH * 7];

        in_ptr++;
        out_ptr++;
    }
}

static void gf8_muladd_EB(void *out, void *in)
{
    unsigned int i;
    uint64_t *in_ptr = (uint64_t *)in;
    uint64_t *out_ptr = (uint64_t *)out;

    for (i = 0; i < WIDTH; i++) {
        uint64_t out0, out1, out2, out3, out4, out5, out6, out7;
        uint64_t tmp0, tmp1;

        uint64_t in0 = out_ptr[0];
        uint64_t in1 = out_ptr[WIDTH];
        uint64_t in2 = out_ptr[WIDTH * 2];
        uint64_t in3 = out_ptr[WIDTH * 3];
        uint64_t in4 = out_ptr[WIDTH * 4];
        uint64_t in5 = out_ptr[WIDTH * 5];
        uint64_t in6 = out_ptr[WIDTH * 6];
        uint64_t in7 = out_ptr[WIDTH * 7];

        out2 = in4 ^ in5;
        tmp0 = in0 ^ in1;
        out4 = in4 ^ in6 ^ in7;
        out5 = in0 ^ in5 ^ in7;
        out6 = tmp0 ^ in6;
        tmp1 = tmp0 ^ in2;
        out0 = tmp1 ^ in3;
        out7 = tmp1 ^ in7;
        out1 = out0 ^ in4;
        out3 = out0 ^ in5 ^ in6;

        out_ptr[0] = out0 ^ in_ptr[0];
        out_ptr[WIDTH] = out1 ^ in_ptr[WIDTH];
        out_ptr[WIDTH * 2] = out2 ^ in_ptr[WIDTH * 2];
        out_ptr[WIDTH * 3] = out3 ^ in_ptr[WIDTH * 3];
        out_ptr[WIDTH * 4] = out4 ^ in_ptr[WIDTH * 4];
        out_ptr[WIDTH * 5] = out5 ^ in_ptr[WIDTH * 5];
        out_ptr[WIDTH * 6] = out6 ^ in_ptr[WIDTH * 6];
        out_ptr[WIDTH * 7] = out7 ^ in_ptr[WIDTH * 7];

        in_ptr++;
        out_ptr++;
    }
}

static void gf8_muladd_EC(void *out, void *in)
{
    unsigned int i;
    uint64_t *in_ptr = (uint64_t *)in;
    uint64_t *out_ptr = (uint64_t *)out;

    for (i = 0; i < WIDTH; i++) {
        uint64_t out0, out1, out2, out3, out4, out5, out6, out7;

        uint64_t in0 = out_ptr[0];
        uint64_t in1 = out_ptr[WIDTH];
        uint64_t in2 = out_ptr[WIDTH * 2];
        uint64_t in3 = out_ptr[WIDTH * 3];
        uint64_t in4 = out_ptr[WIDTH * 4];
        uint64_t in5 = out_ptr[WIDTH * 5];
        uint64_t in6 = out_ptr[WIDTH * 6];
        uint64_t in7 = out_ptr[WIDTH * 7];

        out3 = in0 ^ in5;
        out4 = in2 ^ in3 ^ in7;
        out5 = in0 ^ in3 ^ in4;
        out6 = out3 ^ in1 ^ in4;
        out1 = out4 ^ in4;
        out0 = out4 ^ in1 ^ in6;
        out2 = out0 ^ out5 ^ in5;
        out7 = out2 ^ in4 ^ in7;

        out_ptr[0] = out0 ^ in_ptr[0];
        out_ptr[WIDTH] = out1 ^ in_ptr[WIDTH];
        out_ptr[WIDTH * 2] = out2 ^ in_ptr[WIDTH * 2];
        out_ptr[WIDTH * 3] = out3 ^ in_ptr[WIDTH * 3];
        out_ptr[WIDTH * 4] = out4 ^ in_ptr[WIDTH * 4];
        out_ptr[WIDTH * 5] = out5 ^ in_ptr[WIDTH * 5];
        out_ptr[WIDTH * 6] = out6 ^ in_ptr[WIDTH * 6];
        out_ptr[WIDTH * 7] = out7 ^ in_ptr[WIDTH * 7];

        in_ptr++;
        out_ptr++;
    }
}

static void gf8_muladd_ED(void *out, void *in)
{
    unsigned int i;
    uint64_t *in_ptr = (uint64_t *)in;
    uint64_t *out_ptr = (uint64_t *)out;

    for (i = 0; i < WIDTH; i++) {
        uint64_t out0, out1, out2, out3, out4, out5, out6, out7;
        uint64_t tmp0, tmp1;

        uint64_t in0 = out_ptr[0];
        uint64_t in1 = out_ptr[WIDTH];
        uint64_t in2 = out_ptr[WIDTH * 2];
        uint64_t in3 = out_ptr[WIDTH * 3];
        uint64_t in4 = out_ptr[WIDTH * 4];
        uint64_t in5 = out_ptr[WIDTH * 5];
        uint64_t in6 = out_ptr[WIDTH * 6];
        uint64_t in7 = out_ptr[WIDTH * 7];

        tmp0 = in2 ^ in4;
        tmp1 = in3 ^ in5;
        out4 = tmp0 ^ in3 ^ in7;
        out3 = tmp1 ^ in0;
        out1 = out4 ^ in1;
        out5 = out3 ^ in4;
        out7 = out1 ^ out5 ^ in6;
        out2 = tmp0 ^ out7;
        out0 = tmp1 ^ out7;
        out6 = out2 ^ in7;

        out_ptr[0] = out0 ^ in_ptr[0];
        out_ptr[WIDTH] = out1 ^ in_ptr[WIDTH];
        out_ptr[WIDTH * 2] = out2 ^ in_ptr[WIDTH * 2];
        out_ptr[WIDTH * 3] = out3 ^ in_ptr[WIDTH * 3];
        out_ptr[WIDTH * 4] = out4 ^ in_ptr[WIDTH * 4];
        out_ptr[WIDTH * 5] = out5 ^ in_ptr[WIDTH * 5];
        out_ptr[WIDTH * 6] = out6 ^ in_ptr[WIDTH * 6];
        out_ptr[WIDTH * 7] = out7 ^ in_ptr[WIDTH * 7];

        in_ptr++;
        out_ptr++;
    }
}

static void gf8_muladd_EE(void *out, void *in)
{
    unsigned int i;
    uint64_t *in_ptr = (uint64_t *)in;
    uint64_t *out_ptr = (uint64_t *)out;

    for (i = 0; i < WIDTH; i++) {
        uint64_t out0, out1, out2, out3, out4, out5, out6, out7;
        uint64_t tmp0, tmp1, tmp2, tmp3;

        uint64_t in0 = out_ptr[0];
        uint64_t in1 = out_ptr[WIDTH];
        uint64_t in2 = out_ptr[WIDTH * 2];
        uint64_t in3 = out_ptr[WIDTH * 3];
        uint64_t in4 = out_ptr[WIDTH * 4];
        uint64_t in5 = out_ptr[WIDTH * 5];
        uint64_t in6 = out_ptr[WIDTH * 6];
        uint64_t in7 = out_ptr[WIDTH * 7];

        out4 = in2;
        tmp0 = in0 ^ in1;
        out5 = in0 ^ in3;
        tmp1 = tmp0 ^ in2;
        out6 = tmp0 ^ in4;
        tmp2 = tmp1 ^ out5;
        out7 = tmp1 ^ in5;
        out1 = tmp2 ^ out6 ^ in7;
        out0 = tmp2 ^ in6;
        tmp3 = out7 ^ in1;
        out3 = tmp3 ^ in7;
        out2 = tmp3 ^ in4 ^ in6;

        out_ptr[0] = out0 ^ in_ptr[0];
        out_ptr[WIDTH] = out1 ^ in_ptr[WIDTH];
        out_ptr[WIDTH * 2] = out2 ^ in_ptr[WIDTH * 2];
        out_ptr[WIDTH * 3] = out3 ^ in_ptr[WIDTH * 3];
        out_ptr[WIDTH * 4] = out4 ^ in_ptr[WIDTH * 4];
        out_ptr[WIDTH * 5] = out5 ^ in_ptr[WIDTH * 5];
        out_ptr[WIDTH * 6] = out6 ^ in_ptr[WIDTH * 6];
        out_ptr[WIDTH * 7] = out7 ^ in_ptr[WIDTH * 7];

        in_ptr++;
        out_ptr++;
    }
}

static void gf8_muladd_EF(void *out, void *in)
{
    unsigned int i;
    uint64_t *in_ptr = (uint64_t *)in;
    uint64_t *out_ptr = (uint64_t *)out;

    for (i = 0; i < WIDTH; i++) {
        uint64_t out0, out1, out2, out3, out4, out5, out6, out7;
        uint64_t tmp0, tmp1;

        uint64_t in0 = out_ptr[0];
        uint64_t in1 = out_ptr[WIDTH];
        uint64_t in2 = out_ptr[WIDTH * 2];
        uint64_t in3 = out_ptr[WIDTH * 3];
        uint64_t in4 = out_ptr[WIDTH * 4];
        uint64_t in5 = out_ptr[WIDTH * 5];
        uint64_t in6 = out_ptr[WIDTH * 6];
        uint64_t in7 = out_ptr[WIDTH * 7];

        out4 = in2 ^ in4;
        tmp0 = in0 ^ in5;
        tmp1 = in4 ^ in6;
        out5 = tmp0 ^ in3;
        out2 = tmp0 ^ tmp1;
        out6 = tmp1 ^ in0 ^ in1;
        out3 = out5 ^ in2 ^ in7;
        out7 = out3 ^ in1 ^ in3;
        out0 = out4 ^ out6 ^ in3;
        out1 = tmp1 ^ out0 ^ in7;

        out_ptr[0] = out0 ^ in_ptr[0];
        out_ptr[WIDTH] = out1 ^ in_ptr[WIDTH];
        out_ptr[WIDTH * 2] = out2 ^ in_ptr[WIDTH * 2];
        out_ptr[WIDTH * 3] = out3 ^ in_ptr[WIDTH * 3];
        out_ptr[WIDTH * 4] = out4 ^ in_ptr[WIDTH * 4];
        out_ptr[WIDTH * 5] = out5 ^ in_ptr[WIDTH * 5];
        out_ptr[WIDTH * 6] = out6 ^ in_ptr[WIDTH * 6];
        out_ptr[WIDTH * 7] = out7 ^ in_ptr[WIDTH * 7];

        in_ptr++;
        out_ptr++;
    }
}

static void gf8_muladd_F0(void *out, void *in)
{
    unsigned int i;
    uint64_t *in_ptr = (uint64_t *)in;
    uint64_t *out_ptr = (uint64_t *)out;

    for (i = 0; i < WIDTH; i++) {
        uint64_t out0, out1, out2, out3, out4, out5, out6, out7;
        uint64_t tmp0, tmp1, tmp2, tmp3;

        uint64_t in0 = out_ptr[0];
        uint64_t in1 = out_ptr[WIDTH];
        uint64_t in2 = out_ptr[WIDTH * 2];
        uint64_t in3 = out_ptr[WIDTH * 3];
        uint64_t in4 = out_ptr[WIDTH * 4];
        uint64_t in5 = out_ptr[WIDTH * 5];
        uint64_t in6 = out_ptr[WIDTH * 6];
        uint64_t in7 = out_ptr[WIDTH * 7];

        tmp0 = in1 ^ in2;
        tmp1 = in4 ^ in5;
        out2 = tmp0 ^ in6;
        out3 = tmp1 ^ in1;
        tmp2 = tmp1 ^ in7;
        out1 = out2 ^ out3 ^ in3;
        tmp3 = tmp0 ^ tmp2;
        out0 = tmp3 ^ in3;
        out5 = tmp3 ^ in0;
        out4 = out1 ^ out5 ^ in4;
        out7 = out4 ^ in2;
        out6 = tmp2 ^ out7;

        out_ptr[0] = out0 ^ in_ptr[0];
        out_ptr[WIDTH] = out1 ^ in_ptr[WIDTH];
        out_ptr[WIDTH * 2] = out2 ^ in_ptr[WIDTH * 2];
        out_ptr[WIDTH * 3] = out3 ^ in_ptr[WIDTH * 3];
        out_ptr[WIDTH * 4] = out4 ^ in_ptr[WIDTH * 4];
        out_ptr[WIDTH * 5] = out5 ^ in_ptr[WIDTH * 5];
        out_ptr[WIDTH * 6] = out6 ^ in_ptr[WIDTH * 6];
        out_ptr[WIDTH * 7] = out7 ^ in_ptr[WIDTH * 7];

        in_ptr++;
        out_ptr++;
    }
}

static void gf8_muladd_F1(void *out, void *in)
{
    unsigned int i;
    uint64_t *in_ptr = (uint64_t *)in;
    uint64_t *out_ptr = (uint64_t *)out;

    for (i = 0; i < WIDTH; i++) {
        uint64_t out0, out1, out2, out3, out4, out5, out6, out7;
        uint64_t tmp0, tmp1, tmp2, tmp3;

        uint64_t in0 = out_ptr[0];
        uint64_t in1 = out_ptr[WIDTH];
        uint64_t in2 = out_ptr[WIDTH * 2];
        uint64_t in3 = out_ptr[WIDTH * 3];
        uint64_t in4 = out_ptr[WIDTH * 4];
        uint64_t in5 = out_ptr[WIDTH * 5];
        uint64_t in6 = out_ptr[WIDTH * 6];
        uint64_t in7 = out_ptr[WIDTH * 7];

        out2 = in1 ^ in6;
        tmp0 = in3 ^ in5;
        out3 = tmp0 ^ in1 ^ in4;
        tmp1 = out3 ^ in2;
        out1 = tmp1 ^ in6;
        tmp2 = tmp1 ^ in0;
        tmp3 = out1 ^ in5;
        out0 = tmp2 ^ in7;
        out6 = tmp2 ^ in4;
        out7 = tmp3 ^ in0;
        out5 = tmp0 ^ out0;
        out4 = tmp3 ^ out5 ^ in1;

        out_ptr[0] = out0 ^ in_ptr[0];
        out_ptr[WIDTH] = out1 ^ in_ptr[WIDTH];
        out_ptr[WIDTH * 2] = out2 ^ in_ptr[WIDTH * 2];
        out_ptr[WIDTH * 3] = out3 ^ in_ptr[WIDTH * 3];
        out_ptr[WIDTH * 4] = out4 ^ in_ptr[WIDTH * 4];
        out_ptr[WIDTH * 5] = out5 ^ in_ptr[WIDTH * 5];
        out_ptr[WIDTH * 6] = out6 ^ in_ptr[WIDTH * 6];
        out_ptr[WIDTH * 7] = out7 ^ in_ptr[WIDTH * 7];

        in_ptr++;
        out_ptr++;
    }
}

static void gf8_muladd_F2(void *out, void *in)
{
    unsigned int i;
    uint64_t *in_ptr = (uint64_t *)in;
    uint64_t *out_ptr = (uint64_t *)out;

    for (i = 0; i < WIDTH; i++) {
        uint64_t out0, out1, out2, out3, out4, out5, out6, out7;
        uint64_t tmp0, tmp1, tmp2, tmp3;

        uint64_t in0 = out_ptr[0];
        uint64_t in1 = out_ptr[WIDTH];
        uint64_t in2 = out_ptr[WIDTH * 2];
        uint64_t in3 = out_ptr[WIDTH * 3];
        uint64_t in4 = out_ptr[WIDTH * 4];
        uint64_t in5 = out_ptr[WIDTH * 5];
        uint64_t in6 = out_ptr[WIDTH * 6];
        uint64_t in7 = out_ptr[WIDTH * 7];

        tmp0 = in4 ^ in5;
        out2 = in2 ^ in6 ^ in7;
        tmp1 = tmp0 ^ in1;
        tmp2 = tmp1 ^ in2;
        out0 = tmp2 ^ in3;
        out3 = tmp2 ^ in7;
        out5 = out3 ^ in0 ^ in4;
        tmp3 = tmp0 ^ out5;
        out7 = tmp3 ^ in3;
        out4 = tmp3 ^ out2;
        out1 = out0 ^ out4 ^ in4;
        out6 = tmp1 ^ out1;

        out_ptr[0] = out0 ^ in_ptr[0];
        out_ptr[WIDTH] = out1 ^ in_ptr[WIDTH];
        out_ptr[WIDTH * 2] = out2 ^ in_ptr[WIDTH * 2];
        out_ptr[WIDTH * 3] = out3 ^ in_ptr[WIDTH * 3];
        out_ptr[WIDTH * 4] = out4 ^ in_ptr[WIDTH * 4];
        out_ptr[WIDTH * 5] = out5 ^ in_ptr[WIDTH * 5];
        out_ptr[WIDTH * 6] = out6 ^ in_ptr[WIDTH * 6];
        out_ptr[WIDTH * 7] = out7 ^ in_ptr[WIDTH * 7];

        in_ptr++;
        out_ptr++;
    }
}

static void gf8_muladd_F3(void *out, void *in)
{
    unsigned int i;
    uint64_t *in_ptr = (uint64_t *)in;
    uint64_t *out_ptr = (uint64_t *)out;

    for (i = 0; i < WIDTH; i++) {
        uint64_t out0, out1, out2, out3, out4, out5, out6, out7;
        uint64_t tmp0, tmp1;

        uint64_t in0 = out_ptr[0];
        uint64_t in1 = out_ptr[WIDTH];
        uint64_t in2 = out_ptr[WIDTH * 2];
        uint64_t in3 = out_ptr[WIDTH * 3];
        uint64_t in4 = out_ptr[WIDTH * 4];
        uint64_t in5 = out_ptr[WIDTH * 5];
        uint64_t in6 = out_ptr[WIDTH * 6];
        uint64_t in7 = out_ptr[WIDTH * 7];

        out2 = in6 ^ in7;
        tmp0 = in0 ^ in1;
        out4 = tmp0 ^ in6;
        tmp1 = tmp0 ^ in2;
        out5 = tmp1 ^ in7;
        out6 = tmp1 ^ in3;
        out7 = out6 ^ in4;
        out0 = out7 ^ in5;
        out1 = out0 ^ in6;
        out3 = out0 ^ in0 ^ in7;

        out_ptr[0] = out0 ^ in_ptr[0];
        out_ptr[WIDTH] = out1 ^ in_ptr[WIDTH];
        out_ptr[WIDTH * 2] = out2 ^ in_ptr[WIDTH * 2];
        out_ptr[WIDTH * 3] = out3 ^ in_ptr[WIDTH * 3];
        out_ptr[WIDTH * 4] = out4 ^ in_ptr[WIDTH * 4];
        out_ptr[WIDTH * 5] = out5 ^ in_ptr[WIDTH * 5];
        out_ptr[WIDTH * 6] = out6 ^ in_ptr[WIDTH * 6];
        out_ptr[WIDTH * 7] = out7 ^ in_ptr[WIDTH * 7];

        in_ptr++;
        out_ptr++;
    }
}

static void gf8_muladd_F4(void *out, void *in)
{
    unsigned int i;
    uint64_t *in_ptr = (uint64_t *)in;
    uint64_t *out_ptr = (uint64_t *)out;

    for (i = 0; i < WIDTH; i++) {
        uint64_t out0, out1, out2, out3, out4, out5, out6, out7;
        uint64_t tmp0;

        uint64_t in0 = out_ptr[0];
        uint64_t in1 = out_ptr[WIDTH];
        uint64_t in2 = out_ptr[WIDTH * 2];
        uint64_t in3 = out_ptr[WIDTH * 3];
        uint64_t in4 = out_ptr[WIDTH * 4];
        uint64_t in5 = out_ptr[WIDTH * 5];
        uint64_t in6 = out_ptr[WIDTH * 6];
        uint64_t in7 = out_ptr[WIDTH * 7];

        out2 = in0 ^ in1 ^ in2;
        tmp0 = out2 ^ in3;
        out4 = tmp0 ^ in4;
        out5 = out4 ^ in5;
        out6 = out5 ^ in6;
        out7 = out6 ^ in7;
        out0 = out7 ^ in0;
        out1 = out0 ^ in1;
        out3 = tmp0 ^ out7;

        out_ptr[0] = out0 ^ in_ptr[0];
        out_ptr[WIDTH] = out1 ^ in_ptr[WIDTH];
        out_ptr[WIDTH * 2] = out2 ^ in_ptr[WIDTH * 2];
        out_ptr[WIDTH * 3] = out3 ^ in_ptr[WIDTH * 3];
        out_ptr[WIDTH * 4] = out4 ^ in_ptr[WIDTH * 4];
        out_ptr[WIDTH * 5] = out5 ^ in_ptr[WIDTH * 5];
        out_ptr[WIDTH * 6] = out6 ^ in_ptr[WIDTH * 6];
        out_ptr[WIDTH * 7] = out7 ^ in_ptr[WIDTH * 7];

        in_ptr++;
        out_ptr++;
    }
}

static void gf8_muladd_F5(void *out, void *in)
{
    unsigned int i;
    uint64_t *in_ptr = (uint64_t *)in;
    uint64_t *out_ptr = (uint64_t *)out;

    for (i = 0; i < WIDTH; i++) {
        uint64_t out0, out1, out2, out3, out4, out5, out6, out7;
        uint64_t tmp0;

        uint64_t in0 = out_ptr[0];
        uint64_t in1 = out_ptr[WIDTH];
        uint64_t in2 = out_ptr[WIDTH * 2];
        uint64_t in3 = out_ptr[WIDTH * 3];
        uint64_t in4 = out_ptr[WIDTH * 4];
        uint64_t in5 = out_ptr[WIDTH * 5];
        uint64_t in6 = out_ptr[WIDTH * 6];
        uint64_t in7 = out_ptr[WIDTH * 7];

        out2 = in0 ^ in1;
        tmp0 = out2 ^ in2;
        out4 = tmp0 ^ in3;
        out5 = out4 ^ in4;
        out6 = out5 ^ in5;
        out7 = out6 ^ in6;
        out0 = out7 ^ in7;
        out1 = out0 ^ in0;
        out3 = tmp0 ^ out0;

        out_ptr[0] = out0 ^ in_ptr[0];
        out_ptr[WIDTH] = out1 ^ in_ptr[WIDTH];
        out_ptr[WIDTH * 2] = out2 ^ in_ptr[WIDTH * 2];
        out_ptr[WIDTH * 3] = out3 ^ in_ptr[WIDTH * 3];
        out_ptr[WIDTH * 4] = out4 ^ in_ptr[WIDTH * 4];
        out_ptr[WIDTH * 5] = out5 ^ in_ptr[WIDTH * 5];
        out_ptr[WIDTH * 6] = out6 ^ in_ptr[WIDTH * 6];
        out_ptr[WIDTH * 7] = out7 ^ in_ptr[WIDTH * 7];

        in_ptr++;
        out_ptr++;
    }
}

static void gf8_muladd_F6(void *out, void *in)
{
    unsigned int i;
    uint64_t *in_ptr = (uint64_t *)in;
    uint64_t *out_ptr = (uint64_t *)out;

    for (i = 0; i < WIDTH; i++) {
        uint64_t out0, out1, out2, out3, out4, out5, out6, out7;
        uint64_t tmp0, tmp1;

        uint64_t in0 = out_ptr[0];
        uint64_t in1 = out_ptr[WIDTH];
        uint64_t in2 = out_ptr[WIDTH * 2];
        uint64_t in3 = out_ptr[WIDTH * 3];
        uint64_t in4 = out_ptr[WIDTH * 4];
        uint64_t in5 = out_ptr[WIDTH * 5];
        uint64_t in6 = out_ptr[WIDTH * 6];
        uint64_t in7 = out_ptr[WIDTH * 7];

        tmp0 = in0 ^ in7;
        out2 = tmp0 ^ in2;
        out4 = out2 ^ in1 ^ in4;
        out7 = out4 ^ in3 ^ in5;
        out5 = out7 ^ in4 ^ in7;
        out0 = tmp0 ^ out7 ^ in6;
        tmp1 = out0 ^ in1;
        out6 = out0 ^ in0 ^ in5;
        out3 = tmp1 ^ in3;
        out1 = tmp0 ^ tmp1;

        out_ptr[0] = out0 ^ in_ptr[0];
        out_ptr[WIDTH] = out1 ^ in_ptr[WIDTH];
        out_ptr[WIDTH * 2] = out2 ^ in_ptr[WIDTH * 2];
        out_ptr[WIDTH * 3] = out3 ^ in_ptr[WIDTH * 3];
        out_ptr[WIDTH * 4] = out4 ^ in_ptr[WIDTH * 4];
        out_ptr[WIDTH * 5] = out5 ^ in_ptr[WIDTH * 5];
        out_ptr[WIDTH * 6] = out6 ^ in_ptr[WIDTH * 6];
        out_ptr[WIDTH * 7] = out7 ^ in_ptr[WIDTH * 7];

        in_ptr++;
        out_ptr++;
    }
}

static void gf8_muladd_F7(void *out, void *in)
{
    unsigned int i;
    uint64_t *in_ptr = (uint64_t *)in;
    uint64_t *out_ptr = (uint64_t *)out;

    for (i = 0; i < WIDTH; i++) {
        uint64_t out0, out1, out2, out3, out4, out5, out6, out7;
        uint64_t tmp0;

        uint64_t in0 = out_ptr[0];
        uint64_t in1 = out_ptr[WIDTH];
        uint64_t in2 = out_ptr[WIDTH * 2];
        uint64_t in3 = out_ptr[WIDTH * 3];
        uint64_t in4 = out_ptr[WIDTH * 4];
        uint64_t in5 = out_ptr[WIDTH * 5];
        uint64_t in6 = out_ptr[WIDTH * 6];
        uint64_t in7 = out_ptr[WIDTH * 7];

        out2 = in0 ^ in7;
        tmp0 = out2 ^ in1;
        out4 = tmp0 ^ in2;
        out5 = out4 ^ in3 ^ in7;
        out6 = out5 ^ in4;
        out7 = out6 ^ in5;
        out0 = out7 ^ in6;
        out1 = out0 ^ in7;
        out3 = tmp0 ^ out1;

        out_ptr[0] = out0 ^ in_ptr[0];
        out_ptr[WIDTH] = out1 ^ in_ptr[WIDTH];
        out_ptr[WIDTH * 2] = out2 ^ in_ptr[WIDTH * 2];
        out_ptr[WIDTH * 3] = out3 ^ in_ptr[WIDTH * 3];
        out_ptr[WIDTH * 4] = out4 ^ in_ptr[WIDTH * 4];
        out_ptr[WIDTH * 5] = out5 ^ in_ptr[WIDTH * 5];
        out_ptr[WIDTH * 6] = out6 ^ in_ptr[WIDTH * 6];
        out_ptr[WIDTH * 7] = out7 ^ in_ptr[WIDTH * 7];

        in_ptr++;
        out_ptr++;
    }
}

static void gf8_muladd_F8(void *out, void *in)
{
    unsigned int i;
    uint64_t *in_ptr = (uint64_t *)in;
    uint64_t *out_ptr = (uint64_t *)out;

    for (i = 0; i < WIDTH; i++) {
        uint64_t out0, out1, out2, out3, out4, out5, out6, out7;
        uint64_t tmp0, tmp1, tmp2;

        uint64_t in0 = out_ptr[0];
        uint64_t in1 = out_ptr[WIDTH];
        uint64_t in2 = out_ptr[WIDTH * 2];
        uint64_t in3 = out_ptr[WIDTH * 3];
        uint64_t in4 = out_ptr[WIDTH * 4];
        uint64_t in5 = out_ptr[WIDTH * 5];
        uint64_t in6 = out_ptr[WIDTH * 6];
        uint64_t in7 = out_ptr[WIDTH * 7];

        tmp0 = in0 ^ in4;
        tmp1 = in3 ^ in5;
        tmp2 = tmp0 ^ in6;
        out4 = tmp0 ^ tmp1;
        out1 = tmp1 ^ in2 ^ in4;
        out3 = tmp2 ^ in1;
        out5 = out3 ^ in5;
        out7 = out1 ^ out5 ^ in7;
        out6 = tmp1 ^ out7;
        out0 = tmp2 ^ out7;
        out2 = out6 ^ in0;

        out_ptr[0] = out0 ^ in_ptr[0];
        out_ptr[WIDTH] = out1 ^ in_ptr[WIDTH];
        out_ptr[WIDTH * 2] = out2 ^ in_ptr[WIDTH * 2];
        out_ptr[WIDTH * 3] = out3 ^ in_ptr[WIDTH * 3];
        out_ptr[WIDTH * 4] = out4 ^ in_ptr[WIDTH * 4];
        out_ptr[WIDTH * 5] = out5 ^ in_ptr[WIDTH * 5];
        out_ptr[WIDTH * 6] = out6 ^ in_ptr[WIDTH * 6];
        out_ptr[WIDTH * 7] = out7 ^ in_ptr[WIDTH * 7];

        in_ptr++;
        out_ptr++;
    }
}

static void gf8_muladd_F9(void *out, void *in)
{
    unsigned int i;
    uint64_t *in_ptr = (uint64_t *)in;
    uint64_t *out_ptr = (uint64_t *)out;

    for (i = 0; i < WIDTH; i++) {
        uint64_t out0, out1, out2, out3, out4, out5, out6, out7;
        uint64_t tmp0, tmp1, tmp2, tmp3, tmp4;

        uint64_t in0 = out_ptr[0];
        uint64_t in1 = out_ptr[WIDTH];
        uint64_t in2 = out_ptr[WIDTH * 2];
        uint64_t in3 = out_ptr[WIDTH * 3];
        uint64_t in4 = out_ptr[WIDTH * 4];
        uint64_t in5 = out_ptr[WIDTH * 5];
        uint64_t in6 = out_ptr[WIDTH * 6];
        uint64_t in7 = out_ptr[WIDTH * 7];

        tmp0 = in3 ^ in5;
        tmp1 = in0 ^ in6;
        out4 = tmp0 ^ in0;
        tmp2 = tmp1 ^ in4;
        tmp3 = tmp1 ^ in2;
        out5 = tmp2 ^ in1;
        out3 = out5 ^ in3;
        tmp4 = tmp3 ^ out3;
        out1 = tmp4 ^ in5;
        out0 = tmp4 ^ in0 ^ in7;
        out6 = tmp0 ^ out0 ^ in4;
        out7 = tmp2 ^ tmp4;
        out2 = tmp3 ^ out6;

        out_ptr[0] = out0 ^ in_ptr[0];
        out_ptr[WIDTH] = out1 ^ in_ptr[WIDTH];
        out_ptr[WIDTH * 2] = out2 ^ in_ptr[WIDTH * 2];
        out_ptr[WIDTH * 3] = out3 ^ in_ptr[WIDTH * 3];
        out_ptr[WIDTH * 4] = out4 ^ in_ptr[WIDTH * 4];
        out_ptr[WIDTH * 5] = out5 ^ in_ptr[WIDTH * 5];
        out_ptr[WIDTH * 6] = out6 ^ in_ptr[WIDTH * 6];
        out_ptr[WIDTH * 7] = out7 ^ in_ptr[WIDTH * 7];

        in_ptr++;
        out_ptr++;
    }
}

static void gf8_muladd_FA(void *out, void *in)
{
    unsigned int i;
    uint64_t *in_ptr = (uint64_t *)in;
    uint64_t *out_ptr = (uint64_t *)out;

    for (i = 0; i < WIDTH; i++) {
        uint64_t out0, out1, out2, out3, out4, out5, out6, out7;
        uint64_t tmp0, tmp1, tmp2, tmp3;

        uint64_t in0 = out_ptr[0];
        uint64_t in1 = out_ptr[WIDTH];
        uint64_t in2 = out_ptr[WIDTH * 2];
        uint64_t in3 = out_ptr[WIDTH * 3];
        uint64_t in4 = out_ptr[WIDTH * 4];
        uint64_t in5 = out_ptr[WIDTH * 5];
        uint64_t in6 = out_ptr[WIDTH * 6];
        uint64_t in7 = out_ptr[WIDTH * 7];

        tmp0 = in0 ^ in1;
        tmp1 = tmp0 ^ in2;
        tmp2 = tmp0 ^ in5;
        tmp3 = tmp1 ^ in7;
        out5 = tmp2 ^ in6;
        out6 = tmp3 ^ in6;
        out7 = tmp3 ^ in3;
        out3 = out6 ^ in4;
        out2 = tmp1 ^ out5;
        out4 = out2 ^ out3 ^ in1;
        out0 = out4 ^ out7 ^ in5;
        out1 = tmp2 ^ out0;

        out_ptr[0] = out0 ^ in_ptr[0];
        out_ptr[WIDTH] = out1 ^ in_ptr[WIDTH];
        out_ptr[WIDTH * 2] = out2 ^ in_ptr[WIDTH * 2];
        out_ptr[WIDTH * 3] = out3 ^ in_ptr[WIDTH * 3];
        out_ptr[WIDTH * 4] = out4 ^ in_ptr[WIDTH * 4];
        out_ptr[WIDTH * 5] = out5 ^ in_ptr[WIDTH * 5];
        out_ptr[WIDTH * 6] = out6 ^ in_ptr[WIDTH * 6];
        out_ptr[WIDTH * 7] = out7 ^ in_ptr[WIDTH * 7];

        in_ptr++;
        out_ptr++;
    }
}

static void gf8_muladd_FB(void *out, void *in)
{
    unsigned int i;
    uint64_t *in_ptr = (uint64_t *)in;
    uint64_t *out_ptr = (uint64_t *)out;

    for (i = 0; i < WIDTH; i++) {
        uint64_t out0, out1, out2, out3, out4, out5, out6, out7;
        uint64_t tmp0, tmp1;

        uint64_t in0 = out_ptr[0];
        uint64_t in1 = out_ptr[WIDTH];
        uint64_t in2 = out_ptr[WIDTH * 2];
        uint64_t in3 = out_ptr[WIDTH * 3];
        uint64_t in4 = out_ptr[WIDTH * 4];
        uint64_t in5 = out_ptr[WIDTH * 5];
        uint64_t in6 = out_ptr[WIDTH * 6];
        uint64_t in7 = out_ptr[WIDTH * 7];

        out2 = in5 ^ in6;
        tmp0 = in0 ^ in1;
        out4 = in0 ^ in5 ^ in7;
        out5 = tmp0 ^ in6;
        tmp1 = tmp0 ^ in2;
        out6 = tmp1 ^ in7;
        out7 = tmp1 ^ in3;
        out0 = out7 ^ in4;
        out1 = out0 ^ in5;
        out3 = out0 ^ in6 ^ in7;

        out_ptr[0] = out0 ^ in_ptr[0];
        out_ptr[WIDTH] = out1 ^ in_ptr[WIDTH];
        out_ptr[WIDTH * 2] = out2 ^ in_ptr[WIDTH * 2];
        out_ptr[WIDTH * 3] = out3 ^ in_ptr[WIDTH * 3];
        out_ptr[WIDTH * 4] = out4 ^ in_ptr[WIDTH * 4];
        out_ptr[WIDTH * 5] = out5 ^ in_ptr[WIDTH * 5];
        out_ptr[WIDTH * 6] = out6 ^ in_ptr[WIDTH * 6];
        out_ptr[WIDTH * 7] = out7 ^ in_ptr[WIDTH * 7];

        in_ptr++;
        out_ptr++;
    }
}

static void gf8_muladd_FC(void *out, void *in)
{
    unsigned int i;
    uint64_t *in_ptr = (uint64_t *)in;
    uint64_t *out_ptr = (uint64_t *)out;

    for (i = 0; i < WIDTH; i++) {
        uint64_t out0, out1, out2, out3, out4, out5, out6, out7;
        uint64_t tmp0, tmp1, tmp2, tmp3;

        uint64_t in0 = out_ptr[0];
        uint64_t in1 = out_ptr[WIDTH];
        uint64_t in2 = out_ptr[WIDTH * 2];
        uint64_t in3 = out_ptr[WIDTH * 3];
        uint64_t in4 = out_ptr[WIDTH * 4];
        uint64_t in5 = out_ptr[WIDTH * 5];
        uint64_t in6 = out_ptr[WIDTH * 6];
        uint64_t in7 = out_ptr[WIDTH * 7];

        tmp0 = in1 ^ in2;
        tmp1 = in0 ^ in7;
        out2 = tmp0 ^ tmp1 ^ in5;
        out3 = tmp1 ^ in4;
        tmp2 = out2 ^ in6;
        out6 = tmp2 ^ in4;
        out7 = tmp2 ^ in3;
        out4 = out6 ^ in1 ^ in3;
        tmp3 = out4 ^ in0;
        out1 = tmp3 ^ in6;
        out0 = tmp3 ^ in1 ^ in5;
        out5 = tmp0 ^ out4;

        out_ptr[0] = out0 ^ in_ptr[0];
        out_ptr[WIDTH] = out1 ^ in_ptr[WIDTH];
        out_ptr[WIDTH * 2] = out2 ^ in_ptr[WIDTH * 2];
        out_ptr[WIDTH * 3] = out3 ^ in_ptr[WIDTH * 3];
        out_ptr[WIDTH * 4] = out4 ^ in_ptr[WIDTH * 4];
        out_ptr[WIDTH * 5] = out5 ^ in_ptr[WIDTH * 5];
        out_ptr[WIDTH * 6] = out6 ^ in_ptr[WIDTH * 6];
        out_ptr[WIDTH * 7] = out7 ^ in_ptr[WIDTH * 7];

        in_ptr++;
        out_ptr++;
    }
}

static void gf8_muladd_FD(void *out, void *in)
{
    unsigned int i;
    uint64_t *in_ptr = (uint64_t *)in;
    uint64_t *out_ptr = (uint64_t *)out;

    for (i = 0; i < WIDTH; i++) {
        uint64_t out0, out1, out2, out3, out4, out5, out6, out7;
        uint64_t tmp0, tmp1, tmp2, tmp3;

        uint64_t in0 = out_ptr[0];
        uint64_t in1 = out_ptr[WIDTH];
        uint64_t in2 = out_ptr[WIDTH * 2];
        uint64_t in3 = out_ptr[WIDTH * 3];
        uint64_t in4 = out_ptr[WIDTH * 4];
        uint64_t in5 = out_ptr[WIDTH * 5];
        uint64_t in6 = out_ptr[WIDTH * 6];
        uint64_t in7 = out_ptr[WIDTH * 7];

        tmp0 = in0 ^ in5;
        tmp1 = in1 ^ in7;
        out2 = tmp0 ^ tmp1;
        out6 = out2 ^ in2 ^ in4;
        tmp2 = out6 ^ in0;
        out1 = tmp2 ^ in3;
        out0 = tmp0 ^ out1 ^ in6;
        out5 = out0 ^ in2;
        tmp3 = out5 ^ in1;
        out3 = tmp3 ^ in6;
        out7 = tmp2 ^ tmp3;
        out4 = tmp1 ^ out7;

        out_ptr[0] = out0 ^ in_ptr[0];
        out_ptr[WIDTH] = out1 ^ in_ptr[WIDTH];
        out_ptr[WIDTH * 2] = out2 ^ in_ptr[WIDTH * 2];
        out_ptr[WIDTH * 3] = out3 ^ in_ptr[WIDTH * 3];
        out_ptr[WIDTH * 4] = out4 ^ in_ptr[WIDTH * 4];
        out_ptr[WIDTH * 5] = out5 ^ in_ptr[WIDTH * 5];
        out_ptr[WIDTH * 6] = out6 ^ in_ptr[WIDTH * 6];
        out_ptr[WIDTH * 7] = out7 ^ in_ptr[WIDTH * 7];

        in_ptr++;
        out_ptr++;
    }
}

static void gf8_muladd_FE(void *out, void *in)
{
    unsigned int i;
    uint64_t *in_ptr = (uint64_t *)in;
    uint64_t *out_ptr = (uint64_t *)out;

    for (i = 0; i < WIDTH; i++) {
        uint64_t out0, out1, out2, out3, out4, out5, out6, out7;
        uint64_t tmp0, tmp1, tmp2, tmp3, tmp4;

        uint64_t in0 = out_ptr[0];
        uint64_t in1 = out_ptr[WIDTH];
        uint64_t in2 = out_ptr[WIDTH * 2];
        uint64_t in3 = out_ptr[WIDTH * 3];
        uint64_t in4 = out_ptr[WIDTH * 4];
        uint64_t in5 = out_ptr[WIDTH * 5];
        uint64_t in6 = out_ptr[WIDTH * 6];
        uint64_t in7 = out_ptr[WIDTH * 7];

        tmp0 = in0 ^ in2;
        out2 = tmp0 ^ in5;
        out3 = tmp0 ^ in4;
        tmp1 = out3 ^ in6;
        out4 = tmp1 ^ in5;
        tmp2 = tmp1 ^ in1;
        out6 = tmp2 ^ in7;
        tmp3 = tmp2 ^ in0;
        out0 = tmp3 ^ in3;
        tmp4 = out0 ^ out4 ^ in7;
        out5 = tmp4 ^ in6;
        out7 = tmp4 ^ in2;
        out1 = tmp3 ^ out5;

        out_ptr[0] = out0 ^ in_ptr[0];
        out_ptr[WIDTH] = out1 ^ in_ptr[WIDTH];
        out_ptr[WIDTH * 2] = out2 ^ in_ptr[WIDTH * 2];
        out_ptr[WIDTH * 3] = out3 ^ in_ptr[WIDTH * 3];
        out_ptr[WIDTH * 4] = out4 ^ in_ptr[WIDTH * 4];
        out_ptr[WIDTH * 5] = out5 ^ in_ptr[WIDTH * 5];
        out_ptr[WIDTH * 6] = out6 ^ in_ptr[WIDTH * 6];
        out_ptr[WIDTH * 7] = out7 ^ in_ptr[WIDTH * 7];

        in_ptr++;
        out_ptr++;
    }
}

static void gf8_muladd_FF(void *out, void *in)
{
    unsigned int i;
    uint64_t *in_ptr = (uint64_t *)in;
    uint64_t *out_ptr = (uint64_t *)out;

    for (i = 0; i < WIDTH; i++) {
        uint64_t out0, out1, out2, out3, out4, out5, out6, out7;
        uint64_t tmp0, tmp1, tmp2, tmp3;

        uint64_t in0 = out_ptr[0];
        uint64_t in1 = out_ptr[WIDTH];
        uint64_t in2 = out_ptr[WIDTH * 2];
        uint64_t in3 = out_ptr[WIDTH * 3];
        uint64_t in4 = out_ptr[WIDTH * 4];
        uint64_t in5 = out_ptr[WIDTH * 5];
        uint64_t in6 = out_ptr[WIDTH * 6];
        uint64_t in7 = out_ptr[WIDTH * 7];

        out2 = in0 ^ in5;
        tmp0 = in4 ^ in7;
        tmp1 = out2 ^ in2;
        out4 = tmp1 ^ in6;
        out7 = tmp1 ^ in1 ^ in3;
        out1 = tmp0 ^ out7;
        tmp2 = out1 ^ in5;
        out6 = tmp2 ^ in3;
        tmp3 = tmp2 ^ in7;
        out0 = tmp3 ^ in6;
        out3 = tmp3 ^ in1;
        out5 = tmp0 ^ out0 ^ in2;

        out_ptr[0] = out0 ^ in_ptr[0];
        out_ptr[WIDTH] = out1 ^ in_ptr[WIDTH];
        out_ptr[WIDTH * 2] = out2 ^ in_ptr[WIDTH * 2];
        out_ptr[WIDTH * 3] = out3 ^ in_ptr[WIDTH * 3];
        out_ptr[WIDTH * 4] = out4 ^ in_ptr[WIDTH * 4];
        out_ptr[WIDTH * 5] = out5 ^ in_ptr[WIDTH * 5];
        out_ptr[WIDTH * 6] = out6 ^ in_ptr[WIDTH * 6];
        out_ptr[WIDTH * 7] = out7 ^ in_ptr[WIDTH * 7];

        in_ptr++;
        out_ptr++;
    }
}

static void (*gf8_muladd[])(void *out, void *in) = {
    gf8_muladd_00, gf8_muladd_01, gf8_muladd_02, gf8_muladd_03,
    gf8_muladd_04, gf8_muladd_05, gf8_muladd_06, gf8_muladd_07,
    gf8_muladd_08, gf8_muladd_09, gf8_muladd_0A, gf8_muladd_0B,
    gf8_muladd_0C, gf8_muladd_0D, gf8_muladd_0E, gf8_muladd_0F,
    gf8_muladd_10, gf8_muladd_11, gf8_muladd_12, gf8_muladd_13,
    gf8_muladd_14, gf8_muladd_15, gf8_muladd_16, gf8_muladd_17,
    gf8_muladd_18, gf8_muladd_19, gf8_muladd_1A, gf8_muladd_1B,
    gf8_muladd_1C, gf8_muladd_1D, gf8_muladd_1E, gf8_muladd_1F,
    gf8_muladd_20, gf8_muladd_21, gf8_muladd_22, gf8_muladd_23,
    gf8_muladd_24, gf8_muladd_25, gf8_muladd_26, gf8_muladd_27,
    gf8_muladd_28, gf8_muladd_29, gf8_muladd_2A, gf8_muladd_2B,
    gf8_muladd_2C, gf8_muladd_2D, gf8_muladd_2E, gf8_muladd_2F,
    gf8_muladd_30, gf8_muladd_31, gf8_muladd_32, gf8_muladd_33,
    gf8_muladd_34, gf8_muladd_35, gf8_muladd_36, gf8_muladd_37,
    gf8_muladd_38, gf8_muladd_39, gf8_muladd_3A, gf8_muladd_3B,
    gf8_muladd_3C, gf8_muladd_3D, gf8_muladd_3E, gf8_muladd_3F,
    gf8_muladd_40, gf8_muladd_41, gf8_muladd_42, gf8_muladd_43,
    gf8_muladd_44, gf8_muladd_45, gf8_muladd_46, gf8_muladd_47,
    gf8_muladd_48, gf8_muladd_49, gf8_muladd_4A, gf8_muladd_4B,
    gf8_muladd_4C, gf8_muladd_4D, gf8_muladd_4E, gf8_muladd_4F,
    gf8_muladd_50, gf8_muladd_51, gf8_muladd_52, gf8_muladd_53,
    gf8_muladd_54, gf8_muladd_55, gf8_muladd_56, gf8_muladd_57,
    gf8_muladd_58, gf8_muladd_59, gf8_muladd_5A, gf8_muladd_5B,
    gf8_muladd_5C, gf8_muladd_5D, gf8_muladd_5E, gf8_muladd_5F,
    gf8_muladd_60, gf8_muladd_61, gf8_muladd_62, gf8_muladd_63,
    gf8_muladd_64, gf8_muladd_65, gf8_muladd_66, gf8_muladd_67,
    gf8_muladd_68, gf8_muladd_69, gf8_muladd_6A, gf8_muladd_6B,
    gf8_muladd_6C, gf8_muladd_6D, gf8_muladd_6E, gf8_muladd_6F,
    gf8_muladd_70, gf8_muladd_71, gf8_muladd_72, gf8_muladd_73,
    gf8_muladd_74, gf8_muladd_75, gf8_muladd_76, gf8_muladd_77,
    gf8_muladd_78, gf8_muladd_79, gf8_muladd_7A, gf8_muladd_7B,
    gf8_muladd_7C, gf8_muladd_7D, gf8_muladd_7E, gf8_muladd_7F,
    gf8_muladd_80, gf8_muladd_81, gf8_muladd_82, gf8_muladd_83,
    gf8_muladd_84, gf8_muladd_85, gf8_muladd_86, gf8_muladd_87,
    gf8_muladd_88, gf8_muladd_89, gf8_muladd_8A, gf8_muladd_8B,
    gf8_muladd_8C, gf8_muladd_8D, gf8_muladd_8E, gf8_muladd_8F,
    gf8_muladd_90, gf8_muladd_91, gf8_muladd_92, gf8_muladd_93,
    gf8_muladd_94, gf8_muladd_95, gf8_muladd_96, gf8_muladd_97,
    gf8_muladd_98, gf8_muladd_99, gf8_muladd_9A, gf8_muladd_9B,
    gf8_muladd_9C, gf8_muladd_9D, gf8_muladd_9E, gf8_muladd_9F,
    gf8_muladd_A0, gf8_muladd_A1, gf8_muladd_A2, gf8_muladd_A3,
    gf8_muladd_A4, gf8_muladd_A5, gf8_muladd_A6, gf8_muladd_A7,
    gf8_muladd_A8, gf8_muladd_A9, gf8_muladd_AA, gf8_muladd_AB,
    gf8_muladd_AC, gf8_muladd_AD, gf8_muladd_AE, gf8_muladd_AF,
    gf8_muladd_B0, gf8_muladd_B1, gf8_muladd_B2, gf8_muladd_B3,
    gf8_muladd_B4, gf8_muladd_B5, gf8_muladd_B6, gf8_muladd_B7,
    gf8_muladd_B8, gf8_muladd_B9, gf8_muladd_BA, gf8_muladd_BB,
    gf8_muladd_BC, gf8_muladd_BD, gf8_muladd_BE, gf8_muladd_BF,
    gf8_muladd_C0, gf8_muladd_C1, gf8_muladd_C2, gf8_muladd_C3,
    gf8_muladd_C4, gf8_muladd_C5, gf8_muladd_C6, gf8_muladd_C7,
    gf8_muladd_C8, gf8_muladd_C9, gf8_muladd_CA, gf8_muladd_CB,
    gf8_muladd_CC, gf8_muladd_CD, gf8_muladd_CE, gf8_muladd_CF,
    gf8_muladd_D0, gf8_muladd_D1, gf8_muladd_D2, gf8_muladd_D3,
    gf8_muladd_D4, gf8_muladd_D5, gf8_muladd_D6, gf8_muladd_D7,
    gf8_muladd_D8, gf8_muladd_D9, gf8_muladd_DA, gf8_muladd_DB,
    gf8_muladd_DC, gf8_muladd_DD, gf8_muladd_DE, gf8_muladd_DF,
    gf8_muladd_E0, gf8_muladd_E1, gf8_muladd_E2, gf8_muladd_E3,
    gf8_muladd_E4, gf8_muladd_E5, gf8_muladd_E6, gf8_muladd_E7,
    gf8_muladd_E8, gf8_muladd_E9, gf8_muladd_EA, gf8_muladd_EB,
    gf8_muladd_EC, gf8_muladd_ED, gf8_muladd_EE, gf8_muladd_EF,
    gf8_muladd_F0, gf8_muladd_F1, gf8_muladd_F2, gf8_muladd_F3,
    gf8_muladd_F4, gf8_muladd_F5, gf8_muladd_F6, gf8_muladd_F7,
    gf8_muladd_F8, gf8_muladd_F9, gf8_muladd_FA, gf8_muladd_FB,
    gf8_muladd_FC, gf8_muladd_FD, gf8_muladd_FE, gf8_muladd_FF
};

static uint64_t zero[EC_METHOD_WORD_SIZE * 8] = {0, };

void ec_code_c_prepare(ec_gf_t *gf, uint32_t *values, uint32_t count)
{
    uint32_t i, last, tmp;

    last = 1;
    for (i = count; i > 0; i--) {
        if (values[i - 1] != 0) {
            tmp = values[i - 1];
            values[i - 1] = ec_gf_div(gf, tmp, last);
            last = tmp;
        }
    }
}

void ec_code_c_linear(void *dst, void *src, uint64_t offset, uint32_t *values,
                      uint32_t count)
{
    src += offset;
    gf8_muladd_00(dst, src);
    while (--count > 0) {
        src += EC_METHOD_CHUNK_SIZE;
        gf8_muladd[*values](dst, src);
        values++;
    }
}

void ec_code_c_interleaved(void *dst, void **src, uint64_t offset,
                           uint32_t *values, uint32_t count)
{
    uint32_t i, last, tmp;

    i = 0;
    while ((last = *values++) == 0) {
        i++;
    }
    gf8_muladd_00(dst, src[i++] + offset);
    while (i < count) {
        tmp = *values++;
        if (tmp != 0) {
            gf8_muladd[last](dst, src[i] + offset);
            last = tmp;
        }
        i++;
    }
    gf8_muladd[last](dst, zero);
}
