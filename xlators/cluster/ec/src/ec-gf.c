/*
  Copyright (c) 2012-2014 DataLab, s.l. <http://www.datalab.es>
  This file is part of GlusterFS.

  This file is licensed to you under your choice of the GNU Lesser
  General Public License, version 3 or any later version (LGPLv3 or
  later), or the GNU General Public License, version 2 (GPLv2), in all
  cases as published by the Free Software Foundation.
*/

#include <inttypes.h>
#include <string.h>

#include "ec-gf.h"

static void gf8_muladd_00(uint8_t * out, uint8_t * in, unsigned int width)
{
    memcpy(out, in, sizeof(uint64_t) * 8 * width);
}

static void gf8_muladd_01(uint8_t * out, uint8_t * in, unsigned int width)
{
    unsigned int i;
    uint64_t * in_ptr = (uint64_t *)in;
    uint64_t * out_ptr = (uint64_t *)out;

    for (i = 0; i < width; i++)
    {
        out_ptr[0] ^= in_ptr[0];
        out_ptr[width] ^= in_ptr[width];
        out_ptr[width * 2] ^= in_ptr[width * 2];
        out_ptr[width * 3] ^= in_ptr[width * 3];
        out_ptr[width * 4] ^= in_ptr[width * 4];
        out_ptr[width * 5] ^= in_ptr[width * 5];
        out_ptr[width * 6] ^= in_ptr[width * 6];
        out_ptr[width * 7] ^= in_ptr[width * 7];

        in_ptr++;
        out_ptr++;
    }
}

static void gf8_muladd_02(uint8_t * out, uint8_t * in, unsigned int width)
{
    unsigned int i;
    uint64_t * in_ptr = (uint64_t *)in;
    uint64_t * out_ptr = (uint64_t *)out;

    for (i = 0; i < width; i++)
    {
        uint64_t out0, out1, out2, out3, out4, out5, out6, out7;

        uint64_t in0 = out_ptr[0];
        uint64_t in1 = out_ptr[width];
        uint64_t in2 = out_ptr[width * 2];
        uint64_t in3 = out_ptr[width * 3];
        uint64_t in4 = out_ptr[width * 4];
        uint64_t in5 = out_ptr[width * 5];
        uint64_t in6 = out_ptr[width * 6];
        uint64_t in7 = out_ptr[width * 7];

        out0 = in7;
        out1 = in0;
        out7 = in6;
        out5 = in4;
        out6 = in5;
        out3 = in2 ^ in7;
        out4 = in3 ^ in7;
        out2 = in1 ^ in7;

        out_ptr[0] = out0 ^ in_ptr[0];
        out_ptr[width] = out1 ^ in_ptr[width];
        out_ptr[width * 2] = out2 ^ in_ptr[width * 2];
        out_ptr[width * 3] = out3 ^ in_ptr[width * 3];
        out_ptr[width * 4] = out4 ^ in_ptr[width * 4];
        out_ptr[width * 5] = out5 ^ in_ptr[width * 5];
        out_ptr[width * 6] = out6 ^ in_ptr[width * 6];
        out_ptr[width * 7] = out7 ^ in_ptr[width * 7];

        in_ptr++;
        out_ptr++;
    }
}

static void gf8_muladd_03(uint8_t * out, uint8_t * in, unsigned int width)
{
    unsigned int i;
    uint64_t * in_ptr = (uint64_t *)in;
    uint64_t * out_ptr = (uint64_t *)out;

    for (i = 0; i < width; i++)
    {
        uint64_t out0, out1, out2, out3, out4, out5, out6, out7;
        uint64_t tmp0;

        uint64_t in0 = out_ptr[0];
        uint64_t in1 = out_ptr[width];
        uint64_t in2 = out_ptr[width * 2];
        uint64_t in3 = out_ptr[width * 3];
        uint64_t in4 = out_ptr[width * 4];
        uint64_t in5 = out_ptr[width * 5];
        uint64_t in6 = out_ptr[width * 6];
        uint64_t in7 = out_ptr[width * 7];

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
        out_ptr[width] = out1 ^ in_ptr[width];
        out_ptr[width * 2] = out2 ^ in_ptr[width * 2];
        out_ptr[width * 3] = out3 ^ in_ptr[width * 3];
        out_ptr[width * 4] = out4 ^ in_ptr[width * 4];
        out_ptr[width * 5] = out5 ^ in_ptr[width * 5];
        out_ptr[width * 6] = out6 ^ in_ptr[width * 6];
        out_ptr[width * 7] = out7 ^ in_ptr[width * 7];

        in_ptr++;
        out_ptr++;
    }
}

static void gf8_muladd_04(uint8_t * out, uint8_t * in, unsigned int width)
{
    unsigned int i;
    uint64_t * in_ptr = (uint64_t *)in;
    uint64_t * out_ptr = (uint64_t *)out;

    for (i = 0; i < width; i++)
    {
        uint64_t out0, out1, out2, out3, out4, out5, out6, out7;
        uint64_t tmp0;

        uint64_t in0 = out_ptr[0];
        uint64_t in1 = out_ptr[width];
        uint64_t in2 = out_ptr[width * 2];
        uint64_t in3 = out_ptr[width * 3];
        uint64_t in4 = out_ptr[width * 4];
        uint64_t in5 = out_ptr[width * 5];
        uint64_t in6 = out_ptr[width * 6];
        uint64_t in7 = out_ptr[width * 7];

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
        out_ptr[width] = out1 ^ in_ptr[width];
        out_ptr[width * 2] = out2 ^ in_ptr[width * 2];
        out_ptr[width * 3] = out3 ^ in_ptr[width * 3];
        out_ptr[width * 4] = out4 ^ in_ptr[width * 4];
        out_ptr[width * 5] = out5 ^ in_ptr[width * 5];
        out_ptr[width * 6] = out6 ^ in_ptr[width * 6];
        out_ptr[width * 7] = out7 ^ in_ptr[width * 7];

        in_ptr++;
        out_ptr++;
    }
}

static void gf8_muladd_05(uint8_t * out, uint8_t * in, unsigned int width)
{
    unsigned int i;
    uint64_t * in_ptr = (uint64_t *)in;
    uint64_t * out_ptr = (uint64_t *)out;

    for (i = 0; i < width; i++)
    {
        uint64_t out0, out1, out2, out3, out4, out5, out6, out7;

        uint64_t in0 = out_ptr[0];
        uint64_t in1 = out_ptr[width];
        uint64_t in2 = out_ptr[width * 2];
        uint64_t in3 = out_ptr[width * 3];
        uint64_t in4 = out_ptr[width * 4];
        uint64_t in5 = out_ptr[width * 5];
        uint64_t in6 = out_ptr[width * 6];
        uint64_t in7 = out_ptr[width * 7];

        out0 = in0 ^ in6;
        out1 = in1 ^ in7;
        out7 = in5 ^ in7;
        out6 = in4 ^ in6;
        out2 = out0 ^ in2;
        out3 = out1 ^ in3 ^ in6;
        out5 = out7 ^ in3;
        out4 = out6 ^ in2 ^ in7;

        out_ptr[0] = out0 ^ in_ptr[0];
        out_ptr[width] = out1 ^ in_ptr[width];
        out_ptr[width * 2] = out2 ^ in_ptr[width * 2];
        out_ptr[width * 3] = out3 ^ in_ptr[width * 3];
        out_ptr[width * 4] = out4 ^ in_ptr[width * 4];
        out_ptr[width * 5] = out5 ^ in_ptr[width * 5];
        out_ptr[width * 6] = out6 ^ in_ptr[width * 6];
        out_ptr[width * 7] = out7 ^ in_ptr[width * 7];

        in_ptr++;
        out_ptr++;
    }
}

static void gf8_muladd_06(uint8_t * out, uint8_t * in, unsigned int width)
{
    unsigned int i;
    uint64_t * in_ptr = (uint64_t *)in;
    uint64_t * out_ptr = (uint64_t *)out;

    for (i = 0; i < width; i++)
    {
        uint64_t out0, out1, out2, out3, out4, out5, out6, out7;
        uint64_t tmp0;

        uint64_t in0 = out_ptr[0];
        uint64_t in1 = out_ptr[width];
        uint64_t in2 = out_ptr[width * 2];
        uint64_t in3 = out_ptr[width * 3];
        uint64_t in4 = out_ptr[width * 4];
        uint64_t in5 = out_ptr[width * 5];
        uint64_t in6 = out_ptr[width * 6];
        uint64_t in7 = out_ptr[width * 7];

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
        out_ptr[width] = out1 ^ in_ptr[width];
        out_ptr[width * 2] = out2 ^ in_ptr[width * 2];
        out_ptr[width * 3] = out3 ^ in_ptr[width * 3];
        out_ptr[width * 4] = out4 ^ in_ptr[width * 4];
        out_ptr[width * 5] = out5 ^ in_ptr[width * 5];
        out_ptr[width * 6] = out6 ^ in_ptr[width * 6];
        out_ptr[width * 7] = out7 ^ in_ptr[width * 7];

        in_ptr++;
        out_ptr++;
    }
}

static void gf8_muladd_07(uint8_t * out, uint8_t * in, unsigned int width)
{
    unsigned int i;
    uint64_t * in_ptr = (uint64_t *)in;
    uint64_t * out_ptr = (uint64_t *)out;

    for (i = 0; i < width; i++)
    {
        uint64_t out0, out1, out2, out3, out4, out5, out6, out7;
        uint64_t tmp0, tmp1, tmp2, tmp3;

        uint64_t in0 = out_ptr[0];
        uint64_t in1 = out_ptr[width];
        uint64_t in2 = out_ptr[width * 2];
        uint64_t in3 = out_ptr[width * 3];
        uint64_t in4 = out_ptr[width * 4];
        uint64_t in5 = out_ptr[width * 5];
        uint64_t in6 = out_ptr[width * 6];
        uint64_t in7 = out_ptr[width * 7];

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
        out_ptr[width] = out1 ^ in_ptr[width];
        out_ptr[width * 2] = out2 ^ in_ptr[width * 2];
        out_ptr[width * 3] = out3 ^ in_ptr[width * 3];
        out_ptr[width * 4] = out4 ^ in_ptr[width * 4];
        out_ptr[width * 5] = out5 ^ in_ptr[width * 5];
        out_ptr[width * 6] = out6 ^ in_ptr[width * 6];
        out_ptr[width * 7] = out7 ^ in_ptr[width * 7];

        in_ptr++;
        out_ptr++;
    }
}

static void gf8_muladd_08(uint8_t * out, uint8_t * in, unsigned int width)
{
    unsigned int i;
    uint64_t * in_ptr = (uint64_t *)in;
    uint64_t * out_ptr = (uint64_t *)out;

    for (i = 0; i < width; i++)
    {
        uint64_t out0, out1, out2, out3, out4, out5, out6, out7;

        uint64_t in0 = out_ptr[0];
        uint64_t in1 = out_ptr[width];
        uint64_t in2 = out_ptr[width * 2];
        uint64_t in3 = out_ptr[width * 3];
        uint64_t in4 = out_ptr[width * 4];
        uint64_t in5 = out_ptr[width * 5];
        uint64_t in6 = out_ptr[width * 6];
        uint64_t in7 = out_ptr[width * 7];

        out0 = in5;
        out1 = in6;
        out7 = in4;
        out6 = in3 ^ in7;
        out3 = in0 ^ in5 ^ in6;
        out5 = in2 ^ in6 ^ in7;
        out2 = in5 ^ in7;
        out4 = out2 ^ in1 ^ in6;

        out_ptr[0] = out0 ^ in_ptr[0];
        out_ptr[width] = out1 ^ in_ptr[width];
        out_ptr[width * 2] = out2 ^ in_ptr[width * 2];
        out_ptr[width * 3] = out3 ^ in_ptr[width * 3];
        out_ptr[width * 4] = out4 ^ in_ptr[width * 4];
        out_ptr[width * 5] = out5 ^ in_ptr[width * 5];
        out_ptr[width * 6] = out6 ^ in_ptr[width * 6];
        out_ptr[width * 7] = out7 ^ in_ptr[width * 7];

        in_ptr++;
        out_ptr++;
    }
}

static void gf8_muladd_09(uint8_t * out, uint8_t * in, unsigned int width)
{
    unsigned int i;
    uint64_t * in_ptr = (uint64_t *)in;
    uint64_t * out_ptr = (uint64_t *)out;

    for (i = 0; i < width; i++)
    {
        uint64_t out0, out1, out2, out3, out4, out5, out6, out7;
        uint64_t tmp0;

        uint64_t in0 = out_ptr[0];
        uint64_t in1 = out_ptr[width];
        uint64_t in2 = out_ptr[width * 2];
        uint64_t in3 = out_ptr[width * 3];
        uint64_t in4 = out_ptr[width * 4];
        uint64_t in5 = out_ptr[width * 5];
        uint64_t in6 = out_ptr[width * 6];
        uint64_t in7 = out_ptr[width * 7];

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
        out_ptr[width] = out1 ^ in_ptr[width];
        out_ptr[width * 2] = out2 ^ in_ptr[width * 2];
        out_ptr[width * 3] = out3 ^ in_ptr[width * 3];
        out_ptr[width * 4] = out4 ^ in_ptr[width * 4];
        out_ptr[width * 5] = out5 ^ in_ptr[width * 5];
        out_ptr[width * 6] = out6 ^ in_ptr[width * 6];
        out_ptr[width * 7] = out7 ^ in_ptr[width * 7];

        in_ptr++;
        out_ptr++;
    }
}

static void gf8_muladd_0A(uint8_t * out, uint8_t * in, unsigned int width)
{
    unsigned int i;
    uint64_t * in_ptr = (uint64_t *)in;
    uint64_t * out_ptr = (uint64_t *)out;

    for (i = 0; i < width; i++)
    {
        uint64_t out0, out1, out2, out3, out4, out5, out6, out7;

        uint64_t in0 = out_ptr[0];
        uint64_t in1 = out_ptr[width];
        uint64_t in2 = out_ptr[width * 2];
        uint64_t in3 = out_ptr[width * 3];
        uint64_t in4 = out_ptr[width * 4];
        uint64_t in5 = out_ptr[width * 5];
        uint64_t in6 = out_ptr[width * 6];
        uint64_t in7 = out_ptr[width * 7];

        out0 = in5 ^ in7;
        out1 = in0 ^ in6;
        out7 = in4 ^ in6;
        out2 = in1 ^ in5;
        out6 = out0 ^ in3;
        out3 = out0 ^ out1 ^ in2;
        out5 = out7 ^ in2 ^ in7;
        out4 = out2 ^ in3 ^ in6;

        out_ptr[0] = out0 ^ in_ptr[0];
        out_ptr[width] = out1 ^ in_ptr[width];
        out_ptr[width * 2] = out2 ^ in_ptr[width * 2];
        out_ptr[width * 3] = out3 ^ in_ptr[width * 3];
        out_ptr[width * 4] = out4 ^ in_ptr[width * 4];
        out_ptr[width * 5] = out5 ^ in_ptr[width * 5];
        out_ptr[width * 6] = out6 ^ in_ptr[width * 6];
        out_ptr[width * 7] = out7 ^ in_ptr[width * 7];

        in_ptr++;
        out_ptr++;
    }
}

static void gf8_muladd_0B(uint8_t * out, uint8_t * in, unsigned int width)
{
    unsigned int i;
    uint64_t * in_ptr = (uint64_t *)in;
    uint64_t * out_ptr = (uint64_t *)out;

    for (i = 0; i < width; i++)
    {
        uint64_t out0, out1, out2, out3, out4, out5, out6, out7;
        uint64_t tmp0, tmp1, tmp2;

        uint64_t in0 = out_ptr[0];
        uint64_t in1 = out_ptr[width];
        uint64_t in2 = out_ptr[width * 2];
        uint64_t in3 = out_ptr[width * 3];
        uint64_t in4 = out_ptr[width * 4];
        uint64_t in5 = out_ptr[width * 5];
        uint64_t in6 = out_ptr[width * 6];
        uint64_t in7 = out_ptr[width * 7];

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
        out_ptr[width] = out1 ^ in_ptr[width];
        out_ptr[width * 2] = out2 ^ in_ptr[width * 2];
        out_ptr[width * 3] = out3 ^ in_ptr[width * 3];
        out_ptr[width * 4] = out4 ^ in_ptr[width * 4];
        out_ptr[width * 5] = out5 ^ in_ptr[width * 5];
        out_ptr[width * 6] = out6 ^ in_ptr[width * 6];
        out_ptr[width * 7] = out7 ^ in_ptr[width * 7];

        in_ptr++;
        out_ptr++;
    }
}

static void gf8_muladd_0C(uint8_t * out, uint8_t * in, unsigned int width)
{
    unsigned int i;
    uint64_t * in_ptr = (uint64_t *)in;
    uint64_t * out_ptr = (uint64_t *)out;

    for (i = 0; i < width; i++)
    {
        uint64_t out0, out1, out2, out3, out4, out5, out6, out7;
        uint64_t tmp0, tmp1;

        uint64_t in0 = out_ptr[0];
        uint64_t in1 = out_ptr[width];
        uint64_t in2 = out_ptr[width * 2];
        uint64_t in3 = out_ptr[width * 3];
        uint64_t in4 = out_ptr[width * 4];
        uint64_t in5 = out_ptr[width * 5];
        uint64_t in6 = out_ptr[width * 6];
        uint64_t in7 = out_ptr[width * 7];

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
        out_ptr[width] = out1 ^ in_ptr[width];
        out_ptr[width * 2] = out2 ^ in_ptr[width * 2];
        out_ptr[width * 3] = out3 ^ in_ptr[width * 3];
        out_ptr[width * 4] = out4 ^ in_ptr[width * 4];
        out_ptr[width * 5] = out5 ^ in_ptr[width * 5];
        out_ptr[width * 6] = out6 ^ in_ptr[width * 6];
        out_ptr[width * 7] = out7 ^ in_ptr[width * 7];

        in_ptr++;
        out_ptr++;
    }
}

static void gf8_muladd_0D(uint8_t * out, uint8_t * in, unsigned int width)
{
    unsigned int i;
    uint64_t * in_ptr = (uint64_t *)in;
    uint64_t * out_ptr = (uint64_t *)out;

    for (i = 0; i < width; i++)
    {
        uint64_t out0, out1, out2, out3, out4, out5, out6, out7;
        uint64_t tmp0, tmp1, tmp2;

        uint64_t in0 = out_ptr[0];
        uint64_t in1 = out_ptr[width];
        uint64_t in2 = out_ptr[width * 2];
        uint64_t in3 = out_ptr[width * 3];
        uint64_t in4 = out_ptr[width * 4];
        uint64_t in5 = out_ptr[width * 5];
        uint64_t in6 = out_ptr[width * 6];
        uint64_t in7 = out_ptr[width * 7];

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
        out_ptr[width] = out1 ^ in_ptr[width];
        out_ptr[width * 2] = out2 ^ in_ptr[width * 2];
        out_ptr[width * 3] = out3 ^ in_ptr[width * 3];
        out_ptr[width * 4] = out4 ^ in_ptr[width * 4];
        out_ptr[width * 5] = out5 ^ in_ptr[width * 5];
        out_ptr[width * 6] = out6 ^ in_ptr[width * 6];
        out_ptr[width * 7] = out7 ^ in_ptr[width * 7];

        in_ptr++;
        out_ptr++;
    }
}

static void gf8_muladd_0E(uint8_t * out, uint8_t * in, unsigned int width)
{
    unsigned int i;
    uint64_t * in_ptr = (uint64_t *)in;
    uint64_t * out_ptr = (uint64_t *)out;

    for (i = 0; i < width; i++)
    {
        uint64_t out0, out1, out2, out3, out4, out5, out6, out7;
        uint64_t tmp0, tmp1, tmp2, tmp3;

        uint64_t in0 = out_ptr[0];
        uint64_t in1 = out_ptr[width];
        uint64_t in2 = out_ptr[width * 2];
        uint64_t in3 = out_ptr[width * 3];
        uint64_t in4 = out_ptr[width * 4];
        uint64_t in5 = out_ptr[width * 5];
        uint64_t in6 = out_ptr[width * 6];
        uint64_t in7 = out_ptr[width * 7];

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
        out_ptr[width] = out1 ^ in_ptr[width];
        out_ptr[width * 2] = out2 ^ in_ptr[width * 2];
        out_ptr[width * 3] = out3 ^ in_ptr[width * 3];
        out_ptr[width * 4] = out4 ^ in_ptr[width * 4];
        out_ptr[width * 5] = out5 ^ in_ptr[width * 5];
        out_ptr[width * 6] = out6 ^ in_ptr[width * 6];
        out_ptr[width * 7] = out7 ^ in_ptr[width * 7];

        in_ptr++;
        out_ptr++;
    }
}

static void gf8_muladd_0F(uint8_t * out, uint8_t * in, unsigned int width)
{
    unsigned int i;
    uint64_t * in_ptr = (uint64_t *)in;
    uint64_t * out_ptr = (uint64_t *)out;

    for (i = 0; i < width; i++)
    {
        uint64_t out0, out1, out2, out3, out4, out5, out6, out7;
        uint64_t tmp0, tmp1, tmp2, tmp3;

        uint64_t in0 = out_ptr[0];
        uint64_t in1 = out_ptr[width];
        uint64_t in2 = out_ptr[width * 2];
        uint64_t in3 = out_ptr[width * 3];
        uint64_t in4 = out_ptr[width * 4];
        uint64_t in5 = out_ptr[width * 5];
        uint64_t in6 = out_ptr[width * 6];
        uint64_t in7 = out_ptr[width * 7];

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
        out_ptr[width] = out1 ^ in_ptr[width];
        out_ptr[width * 2] = out2 ^ in_ptr[width * 2];
        out_ptr[width * 3] = out3 ^ in_ptr[width * 3];
        out_ptr[width * 4] = out4 ^ in_ptr[width * 4];
        out_ptr[width * 5] = out5 ^ in_ptr[width * 5];
        out_ptr[width * 6] = out6 ^ in_ptr[width * 6];
        out_ptr[width * 7] = out7 ^ in_ptr[width * 7];

        in_ptr++;
        out_ptr++;
    }
}

static void gf8_muladd_10(uint8_t * out, uint8_t * in, unsigned int width)
{
    unsigned int i;
    uint64_t * in_ptr = (uint64_t *)in;
    uint64_t * out_ptr = (uint64_t *)out;

    for (i = 0; i < width; i++)
    {
        uint64_t out0, out1, out2, out3, out4, out5, out6, out7;
        uint64_t tmp0, tmp1;

        uint64_t in0 = out_ptr[0];
        uint64_t in1 = out_ptr[width];
        uint64_t in2 = out_ptr[width * 2];
        uint64_t in3 = out_ptr[width * 3];
        uint64_t in4 = out_ptr[width * 4];
        uint64_t in5 = out_ptr[width * 5];
        uint64_t in6 = out_ptr[width * 6];
        uint64_t in7 = out_ptr[width * 7];

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
        out_ptr[width] = out1 ^ in_ptr[width];
        out_ptr[width * 2] = out2 ^ in_ptr[width * 2];
        out_ptr[width * 3] = out3 ^ in_ptr[width * 3];
        out_ptr[width * 4] = out4 ^ in_ptr[width * 4];
        out_ptr[width * 5] = out5 ^ in_ptr[width * 5];
        out_ptr[width * 6] = out6 ^ in_ptr[width * 6];
        out_ptr[width * 7] = out7 ^ in_ptr[width * 7];

        in_ptr++;
        out_ptr++;
    }
}

static void gf8_muladd_11(uint8_t * out, uint8_t * in, unsigned int width)
{
    unsigned int i;
    uint64_t * in_ptr = (uint64_t *)in;
    uint64_t * out_ptr = (uint64_t *)out;

    for (i = 0; i < width; i++)
    {
        uint64_t out0, out1, out2, out3, out4, out5, out6, out7;

        uint64_t in0 = out_ptr[0];
        uint64_t in1 = out_ptr[width];
        uint64_t in2 = out_ptr[width * 2];
        uint64_t in3 = out_ptr[width * 3];
        uint64_t in4 = out_ptr[width * 4];
        uint64_t in5 = out_ptr[width * 5];
        uint64_t in6 = out_ptr[width * 6];
        uint64_t in7 = out_ptr[width * 7];

        out7 = in3;
        out0 = in0 ^ in4;
        out1 = in1 ^ in5;
        out6 = in2 ^ in7;
        out4 = in0 ^ in5 ^ in6;
        out5 = in1 ^ in6 ^ in7;
        out2 = in2 ^ in4 ^ in6;
        out3 = in3 ^ in4 ^ in5 ^ in7;

        out_ptr[0] = out0 ^ in_ptr[0];
        out_ptr[width] = out1 ^ in_ptr[width];
        out_ptr[width * 2] = out2 ^ in_ptr[width * 2];
        out_ptr[width * 3] = out3 ^ in_ptr[width * 3];
        out_ptr[width * 4] = out4 ^ in_ptr[width * 4];
        out_ptr[width * 5] = out5 ^ in_ptr[width * 5];
        out_ptr[width * 6] = out6 ^ in_ptr[width * 6];
        out_ptr[width * 7] = out7 ^ in_ptr[width * 7];

        in_ptr++;
        out_ptr++;
    }
}

static void gf8_muladd_12(uint8_t * out, uint8_t * in, unsigned int width)
{
    unsigned int i;
    uint64_t * in_ptr = (uint64_t *)in;
    uint64_t * out_ptr = (uint64_t *)out;

    for (i = 0; i < width; i++)
    {
        uint64_t out0, out1, out2, out3, out4, out5, out6, out7;
        uint64_t tmp0, tmp1;

        uint64_t in0 = out_ptr[0];
        uint64_t in1 = out_ptr[width];
        uint64_t in2 = out_ptr[width * 2];
        uint64_t in3 = out_ptr[width * 3];
        uint64_t in4 = out_ptr[width * 4];
        uint64_t in5 = out_ptr[width * 5];
        uint64_t in6 = out_ptr[width * 6];
        uint64_t in7 = out_ptr[width * 7];

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
        out_ptr[width] = out1 ^ in_ptr[width];
        out_ptr[width * 2] = out2 ^ in_ptr[width * 2];
        out_ptr[width * 3] = out3 ^ in_ptr[width * 3];
        out_ptr[width * 4] = out4 ^ in_ptr[width * 4];
        out_ptr[width * 5] = out5 ^ in_ptr[width * 5];
        out_ptr[width * 6] = out6 ^ in_ptr[width * 6];
        out_ptr[width * 7] = out7 ^ in_ptr[width * 7];

        in_ptr++;
        out_ptr++;
    }
}

static void gf8_muladd_13(uint8_t * out, uint8_t * in, unsigned int width)
{
    unsigned int i;
    uint64_t * in_ptr = (uint64_t *)in;
    uint64_t * out_ptr = (uint64_t *)out;

    for (i = 0; i < width; i++)
    {
        uint64_t out0, out1, out2, out3, out4, out5, out6, out7;
        uint64_t tmp0, tmp1;

        uint64_t in0 = out_ptr[0];
        uint64_t in1 = out_ptr[width];
        uint64_t in2 = out_ptr[width * 2];
        uint64_t in3 = out_ptr[width * 3];
        uint64_t in4 = out_ptr[width * 4];
        uint64_t in5 = out_ptr[width * 5];
        uint64_t in6 = out_ptr[width * 6];
        uint64_t in7 = out_ptr[width * 7];

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
        out_ptr[width] = out1 ^ in_ptr[width];
        out_ptr[width * 2] = out2 ^ in_ptr[width * 2];
        out_ptr[width * 3] = out3 ^ in_ptr[width * 3];
        out_ptr[width * 4] = out4 ^ in_ptr[width * 4];
        out_ptr[width * 5] = out5 ^ in_ptr[width * 5];
        out_ptr[width * 6] = out6 ^ in_ptr[width * 6];
        out_ptr[width * 7] = out7 ^ in_ptr[width * 7];

        in_ptr++;
        out_ptr++;
    }
}

static void gf8_muladd_14(uint8_t * out, uint8_t * in, unsigned int width)
{
    unsigned int i;
    uint64_t * in_ptr = (uint64_t *)in;
    uint64_t * out_ptr = (uint64_t *)out;

    for (i = 0; i < width; i++)
    {
        uint64_t out0, out1, out2, out3, out4, out5, out6, out7;
        uint64_t tmp0, tmp1;

        uint64_t in0 = out_ptr[0];
        uint64_t in1 = out_ptr[width];
        uint64_t in2 = out_ptr[width * 2];
        uint64_t in3 = out_ptr[width * 3];
        uint64_t in4 = out_ptr[width * 4];
        uint64_t in5 = out_ptr[width * 5];
        uint64_t in6 = out_ptr[width * 6];
        uint64_t in7 = out_ptr[width * 7];

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
        out_ptr[width] = out1 ^ in_ptr[width];
        out_ptr[width * 2] = out2 ^ in_ptr[width * 2];
        out_ptr[width * 3] = out3 ^ in_ptr[width * 3];
        out_ptr[width * 4] = out4 ^ in_ptr[width * 4];
        out_ptr[width * 5] = out5 ^ in_ptr[width * 5];
        out_ptr[width * 6] = out6 ^ in_ptr[width * 6];
        out_ptr[width * 7] = out7 ^ in_ptr[width * 7];

        in_ptr++;
        out_ptr++;
    }
}

static void gf8_muladd_15(uint8_t * out, uint8_t * in, unsigned int width)
{
    unsigned int i;
    uint64_t * in_ptr = (uint64_t *)in;
    uint64_t * out_ptr = (uint64_t *)out;

    for (i = 0; i < width; i++)
    {
        uint64_t out0, out1, out2, out3, out4, out5, out6, out7;
        uint64_t tmp0;

        uint64_t in0 = out_ptr[0];
        uint64_t in1 = out_ptr[width];
        uint64_t in2 = out_ptr[width * 2];
        uint64_t in3 = out_ptr[width * 3];
        uint64_t in4 = out_ptr[width * 4];
        uint64_t in5 = out_ptr[width * 5];
        uint64_t in6 = out_ptr[width * 6];
        uint64_t in7 = out_ptr[width * 7];

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
        out_ptr[width] = out1 ^ in_ptr[width];
        out_ptr[width * 2] = out2 ^ in_ptr[width * 2];
        out_ptr[width * 3] = out3 ^ in_ptr[width * 3];
        out_ptr[width * 4] = out4 ^ in_ptr[width * 4];
        out_ptr[width * 5] = out5 ^ in_ptr[width * 5];
        out_ptr[width * 6] = out6 ^ in_ptr[width * 6];
        out_ptr[width * 7] = out7 ^ in_ptr[width * 7];

        in_ptr++;
        out_ptr++;
    }
}

static void gf8_muladd_16(uint8_t * out, uint8_t * in, unsigned int width)
{
    unsigned int i;
    uint64_t * in_ptr = (uint64_t *)in;
    uint64_t * out_ptr = (uint64_t *)out;

    for (i = 0; i < width; i++)
    {
        uint64_t out0, out1, out2, out3, out4, out5, out6, out7;
        uint64_t tmp0, tmp1, tmp2, tmp3;

        uint64_t in0 = out_ptr[0];
        uint64_t in1 = out_ptr[width];
        uint64_t in2 = out_ptr[width * 2];
        uint64_t in3 = out_ptr[width * 3];
        uint64_t in4 = out_ptr[width * 4];
        uint64_t in5 = out_ptr[width * 5];
        uint64_t in6 = out_ptr[width * 6];
        uint64_t in7 = out_ptr[width * 7];

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
        out_ptr[width] = out1 ^ in_ptr[width];
        out_ptr[width * 2] = out2 ^ in_ptr[width * 2];
        out_ptr[width * 3] = out3 ^ in_ptr[width * 3];
        out_ptr[width * 4] = out4 ^ in_ptr[width * 4];
        out_ptr[width * 5] = out5 ^ in_ptr[width * 5];
        out_ptr[width * 6] = out6 ^ in_ptr[width * 6];
        out_ptr[width * 7] = out7 ^ in_ptr[width * 7];

        in_ptr++;
        out_ptr++;
    }
}

static void gf8_muladd_17(uint8_t * out, uint8_t * in, unsigned int width)
{
    unsigned int i;
    uint64_t * in_ptr = (uint64_t *)in;
    uint64_t * out_ptr = (uint64_t *)out;

    for (i = 0; i < width; i++)
    {
        uint64_t out0, out1, out2, out3, out4, out5, out6, out7;
        uint64_t tmp0, tmp1, tmp2, tmp3;

        uint64_t in0 = out_ptr[0];
        uint64_t in1 = out_ptr[width];
        uint64_t in2 = out_ptr[width * 2];
        uint64_t in3 = out_ptr[width * 3];
        uint64_t in4 = out_ptr[width * 4];
        uint64_t in5 = out_ptr[width * 5];
        uint64_t in6 = out_ptr[width * 6];
        uint64_t in7 = out_ptr[width * 7];

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
        out_ptr[width] = out1 ^ in_ptr[width];
        out_ptr[width * 2] = out2 ^ in_ptr[width * 2];
        out_ptr[width * 3] = out3 ^ in_ptr[width * 3];
        out_ptr[width * 4] = out4 ^ in_ptr[width * 4];
        out_ptr[width * 5] = out5 ^ in_ptr[width * 5];
        out_ptr[width * 6] = out6 ^ in_ptr[width * 6];
        out_ptr[width * 7] = out7 ^ in_ptr[width * 7];

        in_ptr++;
        out_ptr++;
    }
}

static void gf8_muladd_18(uint8_t * out, uint8_t * in, unsigned int width)
{
    unsigned int i;
    uint64_t * in_ptr = (uint64_t *)in;
    uint64_t * out_ptr = (uint64_t *)out;

    for (i = 0; i < width; i++)
    {
        uint64_t out0, out1, out2, out3, out4, out5, out6, out7;
        uint64_t tmp0, tmp1;

        uint64_t in0 = out_ptr[0];
        uint64_t in1 = out_ptr[width];
        uint64_t in2 = out_ptr[width * 2];
        uint64_t in3 = out_ptr[width * 3];
        uint64_t in4 = out_ptr[width * 4];
        uint64_t in5 = out_ptr[width * 5];
        uint64_t in6 = out_ptr[width * 6];
        uint64_t in7 = out_ptr[width * 7];

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
        out_ptr[width] = out1 ^ in_ptr[width];
        out_ptr[width * 2] = out2 ^ in_ptr[width * 2];
        out_ptr[width * 3] = out3 ^ in_ptr[width * 3];
        out_ptr[width * 4] = out4 ^ in_ptr[width * 4];
        out_ptr[width * 5] = out5 ^ in_ptr[width * 5];
        out_ptr[width * 6] = out6 ^ in_ptr[width * 6];
        out_ptr[width * 7] = out7 ^ in_ptr[width * 7];

        in_ptr++;
        out_ptr++;
    }
}

static void gf8_muladd_19(uint8_t * out, uint8_t * in, unsigned int width)
{
    unsigned int i;
    uint64_t * in_ptr = (uint64_t *)in;
    uint64_t * out_ptr = (uint64_t *)out;

    for (i = 0; i < width; i++)
    {
        uint64_t out0, out1, out2, out3, out4, out5, out6, out7;
        uint64_t tmp0, tmp1;

        uint64_t in0 = out_ptr[0];
        uint64_t in1 = out_ptr[width];
        uint64_t in2 = out_ptr[width * 2];
        uint64_t in3 = out_ptr[width * 3];
        uint64_t in4 = out_ptr[width * 4];
        uint64_t in5 = out_ptr[width * 5];
        uint64_t in6 = out_ptr[width * 6];
        uint64_t in7 = out_ptr[width * 7];

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
        out_ptr[width] = out1 ^ in_ptr[width];
        out_ptr[width * 2] = out2 ^ in_ptr[width * 2];
        out_ptr[width * 3] = out3 ^ in_ptr[width * 3];
        out_ptr[width * 4] = out4 ^ in_ptr[width * 4];
        out_ptr[width * 5] = out5 ^ in_ptr[width * 5];
        out_ptr[width * 6] = out6 ^ in_ptr[width * 6];
        out_ptr[width * 7] = out7 ^ in_ptr[width * 7];

        in_ptr++;
        out_ptr++;
    }
}

static void gf8_muladd_1A(uint8_t * out, uint8_t * in, unsigned int width)
{
    unsigned int i;
    uint64_t * in_ptr = (uint64_t *)in;
    uint64_t * out_ptr = (uint64_t *)out;

    for (i = 0; i < width; i++)
    {
        uint64_t out0, out1, out2, out3, out4, out5, out6, out7;
        uint64_t tmp0, tmp1, tmp2, tmp3;

        uint64_t in0 = out_ptr[0];
        uint64_t in1 = out_ptr[width];
        uint64_t in2 = out_ptr[width * 2];
        uint64_t in3 = out_ptr[width * 3];
        uint64_t in4 = out_ptr[width * 4];
        uint64_t in5 = out_ptr[width * 5];
        uint64_t in6 = out_ptr[width * 6];
        uint64_t in7 = out_ptr[width * 7];

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
        out_ptr[width] = out1 ^ in_ptr[width];
        out_ptr[width * 2] = out2 ^ in_ptr[width * 2];
        out_ptr[width * 3] = out3 ^ in_ptr[width * 3];
        out_ptr[width * 4] = out4 ^ in_ptr[width * 4];
        out_ptr[width * 5] = out5 ^ in_ptr[width * 5];
        out_ptr[width * 6] = out6 ^ in_ptr[width * 6];
        out_ptr[width * 7] = out7 ^ in_ptr[width * 7];

        in_ptr++;
        out_ptr++;
    }
}

static void gf8_muladd_1B(uint8_t * out, uint8_t * in, unsigned int width)
{
    unsigned int i;
    uint64_t * in_ptr = (uint64_t *)in;
    uint64_t * out_ptr = (uint64_t *)out;

    for (i = 0; i < width; i++)
    {
        uint64_t out0, out1, out2, out3, out4, out5, out6, out7;
        uint64_t tmp0, tmp1, tmp2, tmp3, tmp4;

        uint64_t in0 = out_ptr[0];
        uint64_t in1 = out_ptr[width];
        uint64_t in2 = out_ptr[width * 2];
        uint64_t in3 = out_ptr[width * 3];
        uint64_t in4 = out_ptr[width * 4];
        uint64_t in5 = out_ptr[width * 5];
        uint64_t in6 = out_ptr[width * 6];
        uint64_t in7 = out_ptr[width * 7];

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
        out_ptr[width] = out1 ^ in_ptr[width];
        out_ptr[width * 2] = out2 ^ in_ptr[width * 2];
        out_ptr[width * 3] = out3 ^ in_ptr[width * 3];
        out_ptr[width * 4] = out4 ^ in_ptr[width * 4];
        out_ptr[width * 5] = out5 ^ in_ptr[width * 5];
        out_ptr[width * 6] = out6 ^ in_ptr[width * 6];
        out_ptr[width * 7] = out7 ^ in_ptr[width * 7];

        in_ptr++;
        out_ptr++;
    }
}

static void gf8_muladd_1C(uint8_t * out, uint8_t * in, unsigned int width)
{
    unsigned int i;
    uint64_t * in_ptr = (uint64_t *)in;
    uint64_t * out_ptr = (uint64_t *)out;

    for (i = 0; i < width; i++)
    {
        uint64_t out0, out1, out2, out3, out4, out5, out6, out7;
        uint64_t tmp0, tmp1, tmp2, tmp3, tmp4;

        uint64_t in0 = out_ptr[0];
        uint64_t in1 = out_ptr[width];
        uint64_t in2 = out_ptr[width * 2];
        uint64_t in3 = out_ptr[width * 3];
        uint64_t in4 = out_ptr[width * 4];
        uint64_t in5 = out_ptr[width * 5];
        uint64_t in6 = out_ptr[width * 6];
        uint64_t in7 = out_ptr[width * 7];

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
        out_ptr[width] = out1 ^ in_ptr[width];
        out_ptr[width * 2] = out2 ^ in_ptr[width * 2];
        out_ptr[width * 3] = out3 ^ in_ptr[width * 3];
        out_ptr[width * 4] = out4 ^ in_ptr[width * 4];
        out_ptr[width * 5] = out5 ^ in_ptr[width * 5];
        out_ptr[width * 6] = out6 ^ in_ptr[width * 6];
        out_ptr[width * 7] = out7 ^ in_ptr[width * 7];

        in_ptr++;
        out_ptr++;
    }
}

static void gf8_muladd_1D(uint8_t * out, uint8_t * in, unsigned int width)
{
    unsigned int i;
    uint64_t * in_ptr = (uint64_t *)in;
    uint64_t * out_ptr = (uint64_t *)out;

    for (i = 0; i < width; i++)
    {
        uint64_t out0, out1, out2, out3, out4, out5, out6, out7;
        uint64_t tmp0, tmp1, tmp2, tmp3, tmp4;

        uint64_t in0 = out_ptr[0];
        uint64_t in1 = out_ptr[width];
        uint64_t in2 = out_ptr[width * 2];
        uint64_t in3 = out_ptr[width * 3];
        uint64_t in4 = out_ptr[width * 4];
        uint64_t in5 = out_ptr[width * 5];
        uint64_t in6 = out_ptr[width * 6];
        uint64_t in7 = out_ptr[width * 7];

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
        out_ptr[width] = out1 ^ in_ptr[width];
        out_ptr[width * 2] = out2 ^ in_ptr[width * 2];
        out_ptr[width * 3] = out3 ^ in_ptr[width * 3];
        out_ptr[width * 4] = out4 ^ in_ptr[width * 4];
        out_ptr[width * 5] = out5 ^ in_ptr[width * 5];
        out_ptr[width * 6] = out6 ^ in_ptr[width * 6];
        out_ptr[width * 7] = out7 ^ in_ptr[width * 7];

        in_ptr++;
        out_ptr++;
    }
}

static void gf8_muladd_1E(uint8_t * out, uint8_t * in, unsigned int width)
{
    unsigned int i;
    uint64_t * in_ptr = (uint64_t *)in;
    uint64_t * out_ptr = (uint64_t *)out;

    for (i = 0; i < width; i++)
    {
        uint64_t out0, out1, out2, out3, out4, out5, out6, out7;
        uint64_t tmp0, tmp1, tmp2, tmp3;

        uint64_t in0 = out_ptr[0];
        uint64_t in1 = out_ptr[width];
        uint64_t in2 = out_ptr[width * 2];
        uint64_t in3 = out_ptr[width * 3];
        uint64_t in4 = out_ptr[width * 4];
        uint64_t in5 = out_ptr[width * 5];
        uint64_t in6 = out_ptr[width * 6];
        uint64_t in7 = out_ptr[width * 7];

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
        out_ptr[width] = out1 ^ in_ptr[width];
        out_ptr[width * 2] = out2 ^ in_ptr[width * 2];
        out_ptr[width * 3] = out3 ^ in_ptr[width * 3];
        out_ptr[width * 4] = out4 ^ in_ptr[width * 4];
        out_ptr[width * 5] = out5 ^ in_ptr[width * 5];
        out_ptr[width * 6] = out6 ^ in_ptr[width * 6];
        out_ptr[width * 7] = out7 ^ in_ptr[width * 7];

        in_ptr++;
        out_ptr++;
    }
}

static void gf8_muladd_1F(uint8_t * out, uint8_t * in, unsigned int width)
{
    unsigned int i;
    uint64_t * in_ptr = (uint64_t *)in;
    uint64_t * out_ptr = (uint64_t *)out;

    for (i = 0; i < width; i++)
    {
        uint64_t out0, out1, out2, out3, out4, out5, out6, out7;
        uint64_t tmp0, tmp1;

        uint64_t in0 = out_ptr[0];
        uint64_t in1 = out_ptr[width];
        uint64_t in2 = out_ptr[width * 2];
        uint64_t in3 = out_ptr[width * 3];
        uint64_t in4 = out_ptr[width * 4];
        uint64_t in5 = out_ptr[width * 5];
        uint64_t in6 = out_ptr[width * 6];
        uint64_t in7 = out_ptr[width * 7];

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
        out_ptr[width] = out1 ^ in_ptr[width];
        out_ptr[width * 2] = out2 ^ in_ptr[width * 2];
        out_ptr[width * 3] = out3 ^ in_ptr[width * 3];
        out_ptr[width * 4] = out4 ^ in_ptr[width * 4];
        out_ptr[width * 5] = out5 ^ in_ptr[width * 5];
        out_ptr[width * 6] = out6 ^ in_ptr[width * 6];
        out_ptr[width * 7] = out7 ^ in_ptr[width * 7];

        in_ptr++;
        out_ptr++;
    }
}

static void gf8_muladd_20(uint8_t * out, uint8_t * in, unsigned int width)
{
    unsigned int i;
    uint64_t * in_ptr = (uint64_t *)in;
    uint64_t * out_ptr = (uint64_t *)out;

    for (i = 0; i < width; i++)
    {
        uint64_t out0, out1, out2, out3, out4, out5, out6, out7;
        uint64_t tmp0, tmp1;

        uint64_t in0 = out_ptr[0];
        uint64_t in1 = out_ptr[width];
        uint64_t in2 = out_ptr[width * 2];
        uint64_t in3 = out_ptr[width * 3];
        uint64_t in4 = out_ptr[width * 4];
        uint64_t in5 = out_ptr[width * 5];
        uint64_t in6 = out_ptr[width * 6];
        uint64_t in7 = out_ptr[width * 7];

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
        out_ptr[width] = out1 ^ in_ptr[width];
        out_ptr[width * 2] = out2 ^ in_ptr[width * 2];
        out_ptr[width * 3] = out3 ^ in_ptr[width * 3];
        out_ptr[width * 4] = out4 ^ in_ptr[width * 4];
        out_ptr[width * 5] = out5 ^ in_ptr[width * 5];
        out_ptr[width * 6] = out6 ^ in_ptr[width * 6];
        out_ptr[width * 7] = out7 ^ in_ptr[width * 7];

        in_ptr++;
        out_ptr++;
    }
}

static void gf8_muladd_21(uint8_t * out, uint8_t * in, unsigned int width)
{
    unsigned int i;
    uint64_t * in_ptr = (uint64_t *)in;
    uint64_t * out_ptr = (uint64_t *)out;

    for (i = 0; i < width; i++)
    {
        uint64_t out0, out1, out2, out3, out4, out5, out6, out7;
        uint64_t tmp0;

        uint64_t in0 = out_ptr[0];
        uint64_t in1 = out_ptr[width];
        uint64_t in2 = out_ptr[width * 2];
        uint64_t in3 = out_ptr[width * 3];
        uint64_t in4 = out_ptr[width * 4];
        uint64_t in5 = out_ptr[width * 5];
        uint64_t in6 = out_ptr[width * 6];
        uint64_t in7 = out_ptr[width * 7];

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
        out_ptr[width] = out1 ^ in_ptr[width];
        out_ptr[width * 2] = out2 ^ in_ptr[width * 2];
        out_ptr[width * 3] = out3 ^ in_ptr[width * 3];
        out_ptr[width * 4] = out4 ^ in_ptr[width * 4];
        out_ptr[width * 5] = out5 ^ in_ptr[width * 5];
        out_ptr[width * 6] = out6 ^ in_ptr[width * 6];
        out_ptr[width * 7] = out7 ^ in_ptr[width * 7];

        in_ptr++;
        out_ptr++;
    }
}

static void gf8_muladd_22(uint8_t * out, uint8_t * in, unsigned int width)
{
    unsigned int i;
    uint64_t * in_ptr = (uint64_t *)in;
    uint64_t * out_ptr = (uint64_t *)out;

    for (i = 0; i < width; i++)
    {
        uint64_t out0, out1, out2, out3, out4, out5, out6, out7;

        uint64_t in0 = out_ptr[0];
        uint64_t in1 = out_ptr[width];
        uint64_t in2 = out_ptr[width * 2];
        uint64_t in3 = out_ptr[width * 3];
        uint64_t in4 = out_ptr[width * 4];
        uint64_t in5 = out_ptr[width * 5];
        uint64_t in6 = out_ptr[width * 6];
        uint64_t in7 = out_ptr[width * 7];

        out0 = in3;
        out1 = in0 ^ in4;
        out7 = in2 ^ in7;
        out4 = in4 ^ in5 ^ in7;
        out5 = in0 ^ in5 ^ in6;
        out6 = in1 ^ in6 ^ in7;
        out3 = in2 ^ in3 ^ in4 ^ in6;
        out2 = in1 ^ in3 ^ in5;

        out_ptr[0] = out0 ^ in_ptr[0];
        out_ptr[width] = out1 ^ in_ptr[width];
        out_ptr[width * 2] = out2 ^ in_ptr[width * 2];
        out_ptr[width * 3] = out3 ^ in_ptr[width * 3];
        out_ptr[width * 4] = out4 ^ in_ptr[width * 4];
        out_ptr[width * 5] = out5 ^ in_ptr[width * 5];
        out_ptr[width * 6] = out6 ^ in_ptr[width * 6];
        out_ptr[width * 7] = out7 ^ in_ptr[width * 7];

        in_ptr++;
        out_ptr++;
    }
}

static void gf8_muladd_23(uint8_t * out, uint8_t * in, unsigned int width)
{
    unsigned int i;
    uint64_t * in_ptr = (uint64_t *)in;
    uint64_t * out_ptr = (uint64_t *)out;

    for (i = 0; i < width; i++)
    {
        uint64_t out0, out1, out2, out3, out4, out5, out6, out7;

        uint64_t in0 = out_ptr[0];
        uint64_t in1 = out_ptr[width];
        uint64_t in2 = out_ptr[width * 2];
        uint64_t in3 = out_ptr[width * 3];
        uint64_t in4 = out_ptr[width * 4];
        uint64_t in5 = out_ptr[width * 5];
        uint64_t in6 = out_ptr[width * 6];
        uint64_t in7 = out_ptr[width * 7];

        out7 = in2;
        out0 = in0 ^ in3;
        out4 = in5 ^ in7;
        out5 = in0 ^ in6;
        out6 = in1 ^ in7;
        out3 = in2 ^ in4 ^ in6;
        out1 = in0 ^ in1 ^ in4;
        out2 = out4 ^ out6 ^ in2 ^ in3;

        out_ptr[0] = out0 ^ in_ptr[0];
        out_ptr[width] = out1 ^ in_ptr[width];
        out_ptr[width * 2] = out2 ^ in_ptr[width * 2];
        out_ptr[width * 3] = out3 ^ in_ptr[width * 3];
        out_ptr[width * 4] = out4 ^ in_ptr[width * 4];
        out_ptr[width * 5] = out5 ^ in_ptr[width * 5];
        out_ptr[width * 6] = out6 ^ in_ptr[width * 6];
        out_ptr[width * 7] = out7 ^ in_ptr[width * 7];

        in_ptr++;
        out_ptr++;
    }
}

static void gf8_muladd_24(uint8_t * out, uint8_t * in, unsigned int width)
{
    unsigned int i;
    uint64_t * in_ptr = (uint64_t *)in;
    uint64_t * out_ptr = (uint64_t *)out;

    for (i = 0; i < width; i++)
    {
        uint64_t out0, out1, out2, out3, out4, out5, out6, out7;
        uint64_t tmp0, tmp1;

        uint64_t in0 = out_ptr[0];
        uint64_t in1 = out_ptr[width];
        uint64_t in2 = out_ptr[width * 2];
        uint64_t in3 = out_ptr[width * 3];
        uint64_t in4 = out_ptr[width * 4];
        uint64_t in5 = out_ptr[width * 5];
        uint64_t in6 = out_ptr[width * 6];
        uint64_t in7 = out_ptr[width * 7];

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
        out_ptr[width] = out1 ^ in_ptr[width];
        out_ptr[width * 2] = out2 ^ in_ptr[width * 2];
        out_ptr[width * 3] = out3 ^ in_ptr[width * 3];
        out_ptr[width * 4] = out4 ^ in_ptr[width * 4];
        out_ptr[width * 5] = out5 ^ in_ptr[width * 5];
        out_ptr[width * 6] = out6 ^ in_ptr[width * 6];
        out_ptr[width * 7] = out7 ^ in_ptr[width * 7];

        in_ptr++;
        out_ptr++;
    }
}

static void gf8_muladd_25(uint8_t * out, uint8_t * in, unsigned int width)
{
    unsigned int i;
    uint64_t * in_ptr = (uint64_t *)in;
    uint64_t * out_ptr = (uint64_t *)out;

    for (i = 0; i < width; i++)
    {
        uint64_t out0, out1, out2, out3, out4, out5, out6, out7;
        uint64_t tmp0;

        uint64_t in0 = out_ptr[0];
        uint64_t in1 = out_ptr[width];
        uint64_t in2 = out_ptr[width * 2];
        uint64_t in3 = out_ptr[width * 3];
        uint64_t in4 = out_ptr[width * 4];
        uint64_t in5 = out_ptr[width * 5];
        uint64_t in6 = out_ptr[width * 6];
        uint64_t in7 = out_ptr[width * 7];

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
        out_ptr[width] = out1 ^ in_ptr[width];
        out_ptr[width * 2] = out2 ^ in_ptr[width * 2];
        out_ptr[width * 3] = out3 ^ in_ptr[width * 3];
        out_ptr[width * 4] = out4 ^ in_ptr[width * 4];
        out_ptr[width * 5] = out5 ^ in_ptr[width * 5];
        out_ptr[width * 6] = out6 ^ in_ptr[width * 6];
        out_ptr[width * 7] = out7 ^ in_ptr[width * 7];

        in_ptr++;
        out_ptr++;
    }
}

static void gf8_muladd_26(uint8_t * out, uint8_t * in, unsigned int width)
{
    unsigned int i;
    uint64_t * in_ptr = (uint64_t *)in;
    uint64_t * out_ptr = (uint64_t *)out;

    for (i = 0; i < width; i++)
    {
        uint64_t out0, out1, out2, out3, out4, out5, out6, out7;
        uint64_t tmp0, tmp1, tmp2;

        uint64_t in0 = out_ptr[0];
        uint64_t in1 = out_ptr[width];
        uint64_t in2 = out_ptr[width * 2];
        uint64_t in3 = out_ptr[width * 3];
        uint64_t in4 = out_ptr[width * 4];
        uint64_t in5 = out_ptr[width * 5];
        uint64_t in6 = out_ptr[width * 6];
        uint64_t in7 = out_ptr[width * 7];

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
        out_ptr[width] = out1 ^ in_ptr[width];
        out_ptr[width * 2] = out2 ^ in_ptr[width * 2];
        out_ptr[width * 3] = out3 ^ in_ptr[width * 3];
        out_ptr[width * 4] = out4 ^ in_ptr[width * 4];
        out_ptr[width * 5] = out5 ^ in_ptr[width * 5];
        out_ptr[width * 6] = out6 ^ in_ptr[width * 6];
        out_ptr[width * 7] = out7 ^ in_ptr[width * 7];

        in_ptr++;
        out_ptr++;
    }
}

static void gf8_muladd_27(uint8_t * out, uint8_t * in, unsigned int width)
{
    unsigned int i;
    uint64_t * in_ptr = (uint64_t *)in;
    uint64_t * out_ptr = (uint64_t *)out;

    for (i = 0; i < width; i++)
    {
        uint64_t out0, out1, out2, out3, out4, out5, out6, out7;

        uint64_t in0 = out_ptr[0];
        uint64_t in1 = out_ptr[width];
        uint64_t in2 = out_ptr[width * 2];
        uint64_t in3 = out_ptr[width * 3];
        uint64_t in4 = out_ptr[width * 4];
        uint64_t in5 = out_ptr[width * 5];
        uint64_t in6 = out_ptr[width * 6];
        uint64_t in7 = out_ptr[width * 7];

        out7 = in2 ^ in5;
        out0 = in0 ^ in3 ^ in6;
        out6 = in1 ^ in4 ^ in7;
        out4 = out7 ^ in6;
        out2 = out0 ^ out7 ^ in1;
        out5 = out0 ^ in7;
        out1 = out6 ^ in0;
        out3 = out6 ^ in2;

        out_ptr[0] = out0 ^ in_ptr[0];
        out_ptr[width] = out1 ^ in_ptr[width];
        out_ptr[width * 2] = out2 ^ in_ptr[width * 2];
        out_ptr[width * 3] = out3 ^ in_ptr[width * 3];
        out_ptr[width * 4] = out4 ^ in_ptr[width * 4];
        out_ptr[width * 5] = out5 ^ in_ptr[width * 5];
        out_ptr[width * 6] = out6 ^ in_ptr[width * 6];
        out_ptr[width * 7] = out7 ^ in_ptr[width * 7];

        in_ptr++;
        out_ptr++;
    }
}

static void gf8_muladd_28(uint8_t * out, uint8_t * in, unsigned int width)
{
    unsigned int i;
    uint64_t * in_ptr = (uint64_t *)in;
    uint64_t * out_ptr = (uint64_t *)out;

    for (i = 0; i < width; i++)
    {
        uint64_t out0, out1, out2, out3, out4, out5, out6, out7;
        uint64_t tmp0, tmp1, tmp2;

        uint64_t in0 = out_ptr[0];
        uint64_t in1 = out_ptr[width];
        uint64_t in2 = out_ptr[width * 2];
        uint64_t in3 = out_ptr[width * 3];
        uint64_t in4 = out_ptr[width * 4];
        uint64_t in5 = out_ptr[width * 5];
        uint64_t in6 = out_ptr[width * 6];
        uint64_t in7 = out_ptr[width * 7];

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
        out_ptr[width] = out1 ^ in_ptr[width];
        out_ptr[width * 2] = out2 ^ in_ptr[width * 2];
        out_ptr[width * 3] = out3 ^ in_ptr[width * 3];
        out_ptr[width * 4] = out4 ^ in_ptr[width * 4];
        out_ptr[width * 5] = out5 ^ in_ptr[width * 5];
        out_ptr[width * 6] = out6 ^ in_ptr[width * 6];
        out_ptr[width * 7] = out7 ^ in_ptr[width * 7];

        in_ptr++;
        out_ptr++;
    }
}

static void gf8_muladd_29(uint8_t * out, uint8_t * in, unsigned int width)
{
    unsigned int i;
    uint64_t * in_ptr = (uint64_t *)in;
    uint64_t * out_ptr = (uint64_t *)out;

    for (i = 0; i < width; i++)
    {
        uint64_t out0, out1, out2, out3, out4, out5, out6, out7;
        uint64_t tmp0, tmp1, tmp2;

        uint64_t in0 = out_ptr[0];
        uint64_t in1 = out_ptr[width];
        uint64_t in2 = out_ptr[width * 2];
        uint64_t in3 = out_ptr[width * 3];
        uint64_t in4 = out_ptr[width * 4];
        uint64_t in5 = out_ptr[width * 5];
        uint64_t in6 = out_ptr[width * 6];
        uint64_t in7 = out_ptr[width * 7];

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
        out_ptr[width] = out1 ^ in_ptr[width];
        out_ptr[width * 2] = out2 ^ in_ptr[width * 2];
        out_ptr[width * 3] = out3 ^ in_ptr[width * 3];
        out_ptr[width * 4] = out4 ^ in_ptr[width * 4];
        out_ptr[width * 5] = out5 ^ in_ptr[width * 5];
        out_ptr[width * 6] = out6 ^ in_ptr[width * 6];
        out_ptr[width * 7] = out7 ^ in_ptr[width * 7];

        in_ptr++;
        out_ptr++;
    }
}

static void gf8_muladd_2A(uint8_t * out, uint8_t * in, unsigned int width)
{
    unsigned int i;
    uint64_t * in_ptr = (uint64_t *)in;
    uint64_t * out_ptr = (uint64_t *)out;

    for (i = 0; i < width; i++)
    {
        uint64_t out0, out1, out2, out3, out4, out5, out6, out7;
        uint64_t tmp0, tmp1;

        uint64_t in0 = out_ptr[0];
        uint64_t in1 = out_ptr[width];
        uint64_t in2 = out_ptr[width * 2];
        uint64_t in3 = out_ptr[width * 3];
        uint64_t in4 = out_ptr[width * 4];
        uint64_t in5 = out_ptr[width * 5];
        uint64_t in6 = out_ptr[width * 6];
        uint64_t in7 = out_ptr[width * 7];

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
        out_ptr[width] = out1 ^ in_ptr[width];
        out_ptr[width * 2] = out2 ^ in_ptr[width * 2];
        out_ptr[width * 3] = out3 ^ in_ptr[width * 3];
        out_ptr[width * 4] = out4 ^ in_ptr[width * 4];
        out_ptr[width * 5] = out5 ^ in_ptr[width * 5];
        out_ptr[width * 6] = out6 ^ in_ptr[width * 6];
        out_ptr[width * 7] = out7 ^ in_ptr[width * 7];

        in_ptr++;
        out_ptr++;
    }
}

static void gf8_muladd_2B(uint8_t * out, uint8_t * in, unsigned int width)
{
    unsigned int i;
    uint64_t * in_ptr = (uint64_t *)in;
    uint64_t * out_ptr = (uint64_t *)out;

    for (i = 0; i < width; i++)
    {
        uint64_t out0, out1, out2, out3, out4, out5, out6, out7;
        uint64_t tmp0, tmp1;

        uint64_t in0 = out_ptr[0];
        uint64_t in1 = out_ptr[width];
        uint64_t in2 = out_ptr[width * 2];
        uint64_t in3 = out_ptr[width * 3];
        uint64_t in4 = out_ptr[width * 4];
        uint64_t in5 = out_ptr[width * 5];
        uint64_t in6 = out_ptr[width * 6];
        uint64_t in7 = out_ptr[width * 7];

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
        out_ptr[width] = out1 ^ in_ptr[width];
        out_ptr[width * 2] = out2 ^ in_ptr[width * 2];
        out_ptr[width * 3] = out3 ^ in_ptr[width * 3];
        out_ptr[width * 4] = out4 ^ in_ptr[width * 4];
        out_ptr[width * 5] = out5 ^ in_ptr[width * 5];
        out_ptr[width * 6] = out6 ^ in_ptr[width * 6];
        out_ptr[width * 7] = out7 ^ in_ptr[width * 7];

        in_ptr++;
        out_ptr++;
    }
}

static void gf8_muladd_2C(uint8_t * out, uint8_t * in, unsigned int width)
{
    unsigned int i;
    uint64_t * in_ptr = (uint64_t *)in;
    uint64_t * out_ptr = (uint64_t *)out;

    for (i = 0; i < width; i++)
    {
        uint64_t out0, out1, out2, out3, out4, out5, out6, out7;
        uint64_t tmp0, tmp1, tmp2, tmp3;

        uint64_t in0 = out_ptr[0];
        uint64_t in1 = out_ptr[width];
        uint64_t in2 = out_ptr[width * 2];
        uint64_t in3 = out_ptr[width * 3];
        uint64_t in4 = out_ptr[width * 4];
        uint64_t in5 = out_ptr[width * 5];
        uint64_t in6 = out_ptr[width * 6];
        uint64_t in7 = out_ptr[width * 7];

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
        out_ptr[width] = out1 ^ in_ptr[width];
        out_ptr[width * 2] = out2 ^ in_ptr[width * 2];
        out_ptr[width * 3] = out3 ^ in_ptr[width * 3];
        out_ptr[width * 4] = out4 ^ in_ptr[width * 4];
        out_ptr[width * 5] = out5 ^ in_ptr[width * 5];
        out_ptr[width * 6] = out6 ^ in_ptr[width * 6];
        out_ptr[width * 7] = out7 ^ in_ptr[width * 7];

        in_ptr++;
        out_ptr++;
    }
}

static void gf8_muladd_2D(uint8_t * out, uint8_t * in, unsigned int width)
{
    unsigned int i;
    uint64_t * in_ptr = (uint64_t *)in;
    uint64_t * out_ptr = (uint64_t *)out;

    for (i = 0; i < width; i++)
    {
        uint64_t out0, out1, out2, out3, out4, out5, out6, out7;
        uint64_t tmp0, tmp1, tmp2, tmp3;

        uint64_t in0 = out_ptr[0];
        uint64_t in1 = out_ptr[width];
        uint64_t in2 = out_ptr[width * 2];
        uint64_t in3 = out_ptr[width * 3];
        uint64_t in4 = out_ptr[width * 4];
        uint64_t in5 = out_ptr[width * 5];
        uint64_t in6 = out_ptr[width * 6];
        uint64_t in7 = out_ptr[width * 7];

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
        out_ptr[width] = out1 ^ in_ptr[width];
        out_ptr[width * 2] = out2 ^ in_ptr[width * 2];
        out_ptr[width * 3] = out3 ^ in_ptr[width * 3];
        out_ptr[width * 4] = out4 ^ in_ptr[width * 4];
        out_ptr[width * 5] = out5 ^ in_ptr[width * 5];
        out_ptr[width * 6] = out6 ^ in_ptr[width * 6];
        out_ptr[width * 7] = out7 ^ in_ptr[width * 7];

        in_ptr++;
        out_ptr++;
    }
}

static void gf8_muladd_2E(uint8_t * out, uint8_t * in, unsigned int width)
{
    unsigned int i;
    uint64_t * in_ptr = (uint64_t *)in;
    uint64_t * out_ptr = (uint64_t *)out;

    for (i = 0; i < width; i++)
    {
        uint64_t out0, out1, out2, out3, out4, out5, out6, out7;
        uint64_t tmp0, tmp1, tmp2;

        uint64_t in0 = out_ptr[0];
        uint64_t in1 = out_ptr[width];
        uint64_t in2 = out_ptr[width * 2];
        uint64_t in3 = out_ptr[width * 3];
        uint64_t in4 = out_ptr[width * 4];
        uint64_t in5 = out_ptr[width * 5];
        uint64_t in6 = out_ptr[width * 6];
        uint64_t in7 = out_ptr[width * 7];

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
        out_ptr[width] = out1 ^ in_ptr[width];
        out_ptr[width * 2] = out2 ^ in_ptr[width * 2];
        out_ptr[width * 3] = out3 ^ in_ptr[width * 3];
        out_ptr[width * 4] = out4 ^ in_ptr[width * 4];
        out_ptr[width * 5] = out5 ^ in_ptr[width * 5];
        out_ptr[width * 6] = out6 ^ in_ptr[width * 6];
        out_ptr[width * 7] = out7 ^ in_ptr[width * 7];

        in_ptr++;
        out_ptr++;
    }
}

static void gf8_muladd_2F(uint8_t * out, uint8_t * in, unsigned int width)
{
    unsigned int i;
    uint64_t * in_ptr = (uint64_t *)in;
    uint64_t * out_ptr = (uint64_t *)out;

    for (i = 0; i < width; i++)
    {
        uint64_t out0, out1, out2, out3, out4, out5, out6, out7;
        uint64_t tmp0, tmp1, tmp2;

        uint64_t in0 = out_ptr[0];
        uint64_t in1 = out_ptr[width];
        uint64_t in2 = out_ptr[width * 2];
        uint64_t in3 = out_ptr[width * 3];
        uint64_t in4 = out_ptr[width * 4];
        uint64_t in5 = out_ptr[width * 5];
        uint64_t in6 = out_ptr[width * 6];
        uint64_t in7 = out_ptr[width * 7];

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
        out_ptr[width] = out1 ^ in_ptr[width];
        out_ptr[width * 2] = out2 ^ in_ptr[width * 2];
        out_ptr[width * 3] = out3 ^ in_ptr[width * 3];
        out_ptr[width * 4] = out4 ^ in_ptr[width * 4];
        out_ptr[width * 5] = out5 ^ in_ptr[width * 5];
        out_ptr[width * 6] = out6 ^ in_ptr[width * 6];
        out_ptr[width * 7] = out7 ^ in_ptr[width * 7];

        in_ptr++;
        out_ptr++;
    }
}

static void gf8_muladd_30(uint8_t * out, uint8_t * in, unsigned int width)
{
    unsigned int i;
    uint64_t * in_ptr = (uint64_t *)in;
    uint64_t * out_ptr = (uint64_t *)out;

    for (i = 0; i < width; i++)
    {
        uint64_t out0, out1, out2, out3, out4, out5, out6, out7;
        uint64_t tmp0, tmp1;

        uint64_t in0 = out_ptr[0];
        uint64_t in1 = out_ptr[width];
        uint64_t in2 = out_ptr[width * 2];
        uint64_t in3 = out_ptr[width * 3];
        uint64_t in4 = out_ptr[width * 4];
        uint64_t in5 = out_ptr[width * 5];
        uint64_t in6 = out_ptr[width * 6];
        uint64_t in7 = out_ptr[width * 7];

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
        out_ptr[width] = out1 ^ in_ptr[width];
        out_ptr[width * 2] = out2 ^ in_ptr[width * 2];
        out_ptr[width * 3] = out3 ^ in_ptr[width * 3];
        out_ptr[width * 4] = out4 ^ in_ptr[width * 4];
        out_ptr[width * 5] = out5 ^ in_ptr[width * 5];
        out_ptr[width * 6] = out6 ^ in_ptr[width * 6];
        out_ptr[width * 7] = out7 ^ in_ptr[width * 7];

        in_ptr++;
        out_ptr++;
    }
}

static void gf8_muladd_31(uint8_t * out, uint8_t * in, unsigned int width)
{
    unsigned int i;
    uint64_t * in_ptr = (uint64_t *)in;
    uint64_t * out_ptr = (uint64_t *)out;

    for (i = 0; i < width; i++)
    {
        uint64_t out0, out1, out2, out3, out4, out5, out6, out7;
        uint64_t tmp0, tmp1, tmp2;

        uint64_t in0 = out_ptr[0];
        uint64_t in1 = out_ptr[width];
        uint64_t in2 = out_ptr[width * 2];
        uint64_t in3 = out_ptr[width * 3];
        uint64_t in4 = out_ptr[width * 4];
        uint64_t in5 = out_ptr[width * 5];
        uint64_t in6 = out_ptr[width * 6];
        uint64_t in7 = out_ptr[width * 7];

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
        out_ptr[width] = out1 ^ in_ptr[width];
        out_ptr[width * 2] = out2 ^ in_ptr[width * 2];
        out_ptr[width * 3] = out3 ^ in_ptr[width * 3];
        out_ptr[width * 4] = out4 ^ in_ptr[width * 4];
        out_ptr[width * 5] = out5 ^ in_ptr[width * 5];
        out_ptr[width * 6] = out6 ^ in_ptr[width * 6];
        out_ptr[width * 7] = out7 ^ in_ptr[width * 7];

        in_ptr++;
        out_ptr++;
    }
}

static void gf8_muladd_32(uint8_t * out, uint8_t * in, unsigned int width)
{
    unsigned int i;
    uint64_t * in_ptr = (uint64_t *)in;
    uint64_t * out_ptr = (uint64_t *)out;

    for (i = 0; i < width; i++)
    {
        uint64_t out0, out1, out2, out3, out4, out5, out6, out7;
        uint64_t tmp0, tmp1;

        uint64_t in0 = out_ptr[0];
        uint64_t in1 = out_ptr[width];
        uint64_t in2 = out_ptr[width * 2];
        uint64_t in3 = out_ptr[width * 3];
        uint64_t in4 = out_ptr[width * 4];
        uint64_t in5 = out_ptr[width * 5];
        uint64_t in6 = out_ptr[width * 6];
        uint64_t in7 = out_ptr[width * 7];

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
        out_ptr[width] = out1 ^ in_ptr[width];
        out_ptr[width * 2] = out2 ^ in_ptr[width * 2];
        out_ptr[width * 3] = out3 ^ in_ptr[width * 3];
        out_ptr[width * 4] = out4 ^ in_ptr[width * 4];
        out_ptr[width * 5] = out5 ^ in_ptr[width * 5];
        out_ptr[width * 6] = out6 ^ in_ptr[width * 6];
        out_ptr[width * 7] = out7 ^ in_ptr[width * 7];

        in_ptr++;
        out_ptr++;
    }
}

static void gf8_muladd_33(uint8_t * out, uint8_t * in, unsigned int width)
{
    unsigned int i;
    uint64_t * in_ptr = (uint64_t *)in;
    uint64_t * out_ptr = (uint64_t *)out;

    for (i = 0; i < width; i++)
    {
        uint64_t out0, out1, out2, out3, out4, out5, out6, out7;
        uint64_t tmp0, tmp1, tmp2, tmp3, tmp4;

        uint64_t in0 = out_ptr[0];
        uint64_t in1 = out_ptr[width];
        uint64_t in2 = out_ptr[width * 2];
        uint64_t in3 = out_ptr[width * 3];
        uint64_t in4 = out_ptr[width * 4];
        uint64_t in5 = out_ptr[width * 5];
        uint64_t in6 = out_ptr[width * 6];
        uint64_t in7 = out_ptr[width * 7];

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
        out_ptr[width] = out1 ^ in_ptr[width];
        out_ptr[width * 2] = out2 ^ in_ptr[width * 2];
        out_ptr[width * 3] = out3 ^ in_ptr[width * 3];
        out_ptr[width * 4] = out4 ^ in_ptr[width * 4];
        out_ptr[width * 5] = out5 ^ in_ptr[width * 5];
        out_ptr[width * 6] = out6 ^ in_ptr[width * 6];
        out_ptr[width * 7] = out7 ^ in_ptr[width * 7];

        in_ptr++;
        out_ptr++;
    }
}

static void gf8_muladd_34(uint8_t * out, uint8_t * in, unsigned int width)
{
    unsigned int i;
    uint64_t * in_ptr = (uint64_t *)in;
    uint64_t * out_ptr = (uint64_t *)out;

    for (i = 0; i < width; i++)
    {
        uint64_t out0, out1, out2, out3, out4, out5, out6, out7;
        uint64_t tmp0, tmp1, tmp2, tmp3, tmp4;

        uint64_t in0 = out_ptr[0];
        uint64_t in1 = out_ptr[width];
        uint64_t in2 = out_ptr[width * 2];
        uint64_t in3 = out_ptr[width * 3];
        uint64_t in4 = out_ptr[width * 4];
        uint64_t in5 = out_ptr[width * 5];
        uint64_t in6 = out_ptr[width * 6];
        uint64_t in7 = out_ptr[width * 7];

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
        out_ptr[width] = out1 ^ in_ptr[width];
        out_ptr[width * 2] = out2 ^ in_ptr[width * 2];
        out_ptr[width * 3] = out3 ^ in_ptr[width * 3];
        out_ptr[width * 4] = out4 ^ in_ptr[width * 4];
        out_ptr[width * 5] = out5 ^ in_ptr[width * 5];
        out_ptr[width * 6] = out6 ^ in_ptr[width * 6];
        out_ptr[width * 7] = out7 ^ in_ptr[width * 7];

        in_ptr++;
        out_ptr++;
    }
}

static void gf8_muladd_35(uint8_t * out, uint8_t * in, unsigned int width)
{
    unsigned int i;
    uint64_t * in_ptr = (uint64_t *)in;
    uint64_t * out_ptr = (uint64_t *)out;

    for (i = 0; i < width; i++)
    {
        uint64_t out0, out1, out2, out3, out4, out5, out6, out7;
        uint64_t tmp0, tmp1, tmp2;

        uint64_t in0 = out_ptr[0];
        uint64_t in1 = out_ptr[width];
        uint64_t in2 = out_ptr[width * 2];
        uint64_t in3 = out_ptr[width * 3];
        uint64_t in4 = out_ptr[width * 4];
        uint64_t in5 = out_ptr[width * 5];
        uint64_t in6 = out_ptr[width * 6];
        uint64_t in7 = out_ptr[width * 7];

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
        out_ptr[width] = out1 ^ in_ptr[width];
        out_ptr[width * 2] = out2 ^ in_ptr[width * 2];
        out_ptr[width * 3] = out3 ^ in_ptr[width * 3];
        out_ptr[width * 4] = out4 ^ in_ptr[width * 4];
        out_ptr[width * 5] = out5 ^ in_ptr[width * 5];
        out_ptr[width * 6] = out6 ^ in_ptr[width * 6];
        out_ptr[width * 7] = out7 ^ in_ptr[width * 7];

        in_ptr++;
        out_ptr++;
    }
}

static void gf8_muladd_36(uint8_t * out, uint8_t * in, unsigned int width)
{
    unsigned int i;
    uint64_t * in_ptr = (uint64_t *)in;
    uint64_t * out_ptr = (uint64_t *)out;

    for (i = 0; i < width; i++)
    {
        uint64_t out0, out1, out2, out3, out4, out5, out6, out7;
        uint64_t tmp0, tmp1;

        uint64_t in0 = out_ptr[0];
        uint64_t in1 = out_ptr[width];
        uint64_t in2 = out_ptr[width * 2];
        uint64_t in3 = out_ptr[width * 3];
        uint64_t in4 = out_ptr[width * 4];
        uint64_t in5 = out_ptr[width * 5];
        uint64_t in6 = out_ptr[width * 6];
        uint64_t in7 = out_ptr[width * 7];

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
        out_ptr[width] = out1 ^ in_ptr[width];
        out_ptr[width * 2] = out2 ^ in_ptr[width * 2];
        out_ptr[width * 3] = out3 ^ in_ptr[width * 3];
        out_ptr[width * 4] = out4 ^ in_ptr[width * 4];
        out_ptr[width * 5] = out5 ^ in_ptr[width * 5];
        out_ptr[width * 6] = out6 ^ in_ptr[width * 6];
        out_ptr[width * 7] = out7 ^ in_ptr[width * 7];

        in_ptr++;
        out_ptr++;
    }
}

static void gf8_muladd_37(uint8_t * out, uint8_t * in, unsigned int width)
{
    unsigned int i;
    uint64_t * in_ptr = (uint64_t *)in;
    uint64_t * out_ptr = (uint64_t *)out;

    for (i = 0; i < width; i++)
    {
        uint64_t out0, out1, out2, out3, out4, out5, out6, out7;
        uint64_t tmp0, tmp1, tmp2, tmp3;

        uint64_t in0 = out_ptr[0];
        uint64_t in1 = out_ptr[width];
        uint64_t in2 = out_ptr[width * 2];
        uint64_t in3 = out_ptr[width * 3];
        uint64_t in4 = out_ptr[width * 4];
        uint64_t in5 = out_ptr[width * 5];
        uint64_t in6 = out_ptr[width * 6];
        uint64_t in7 = out_ptr[width * 7];

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
        out_ptr[width] = out1 ^ in_ptr[width];
        out_ptr[width * 2] = out2 ^ in_ptr[width * 2];
        out_ptr[width * 3] = out3 ^ in_ptr[width * 3];
        out_ptr[width * 4] = out4 ^ in_ptr[width * 4];
        out_ptr[width * 5] = out5 ^ in_ptr[width * 5];
        out_ptr[width * 6] = out6 ^ in_ptr[width * 6];
        out_ptr[width * 7] = out7 ^ in_ptr[width * 7];

        in_ptr++;
        out_ptr++;
    }
}

static void gf8_muladd_38(uint8_t * out, uint8_t * in, unsigned int width)
{
    unsigned int i;
    uint64_t * in_ptr = (uint64_t *)in;
    uint64_t * out_ptr = (uint64_t *)out;

    for (i = 0; i < width; i++)
    {
        uint64_t out0, out1, out2, out3, out4, out5, out6, out7;
        uint64_t tmp0, tmp1, tmp2;

        uint64_t in0 = out_ptr[0];
        uint64_t in1 = out_ptr[width];
        uint64_t in2 = out_ptr[width * 2];
        uint64_t in3 = out_ptr[width * 3];
        uint64_t in4 = out_ptr[width * 4];
        uint64_t in5 = out_ptr[width * 5];
        uint64_t in6 = out_ptr[width * 6];
        uint64_t in7 = out_ptr[width * 7];

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
        out_ptr[width] = out1 ^ in_ptr[width];
        out_ptr[width * 2] = out2 ^ in_ptr[width * 2];
        out_ptr[width * 3] = out3 ^ in_ptr[width * 3];
        out_ptr[width * 4] = out4 ^ in_ptr[width * 4];
        out_ptr[width * 5] = out5 ^ in_ptr[width * 5];
        out_ptr[width * 6] = out6 ^ in_ptr[width * 6];
        out_ptr[width * 7] = out7 ^ in_ptr[width * 7];

        in_ptr++;
        out_ptr++;
    }
}

static void gf8_muladd_39(uint8_t * out, uint8_t * in, unsigned int width)
{
    unsigned int i;
    uint64_t * in_ptr = (uint64_t *)in;
    uint64_t * out_ptr = (uint64_t *)out;

    for (i = 0; i < width; i++)
    {
        uint64_t out0, out1, out2, out3, out4, out5, out6, out7;
        uint64_t tmp0, tmp1, tmp2;

        uint64_t in0 = out_ptr[0];
        uint64_t in1 = out_ptr[width];
        uint64_t in2 = out_ptr[width * 2];
        uint64_t in3 = out_ptr[width * 3];
        uint64_t in4 = out_ptr[width * 4];
        uint64_t in5 = out_ptr[width * 5];
        uint64_t in6 = out_ptr[width * 6];
        uint64_t in7 = out_ptr[width * 7];

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
        out_ptr[width] = out1 ^ in_ptr[width];
        out_ptr[width * 2] = out2 ^ in_ptr[width * 2];
        out_ptr[width * 3] = out3 ^ in_ptr[width * 3];
        out_ptr[width * 4] = out4 ^ in_ptr[width * 4];
        out_ptr[width * 5] = out5 ^ in_ptr[width * 5];
        out_ptr[width * 6] = out6 ^ in_ptr[width * 6];
        out_ptr[width * 7] = out7 ^ in_ptr[width * 7];

        in_ptr++;
        out_ptr++;
    }
}

static void gf8_muladd_3A(uint8_t * out, uint8_t * in, unsigned int width)
{
    unsigned int i;
    uint64_t * in_ptr = (uint64_t *)in;
    uint64_t * out_ptr = (uint64_t *)out;

    for (i = 0; i < width; i++)
    {
        uint64_t out0, out1, out2, out3, out4, out5, out6, out7;
        uint64_t tmp0, tmp1, tmp2, tmp3, tmp4, tmp5;

        uint64_t in0 = out_ptr[0];
        uint64_t in1 = out_ptr[width];
        uint64_t in2 = out_ptr[width * 2];
        uint64_t in3 = out_ptr[width * 3];
        uint64_t in4 = out_ptr[width * 4];
        uint64_t in5 = out_ptr[width * 5];
        uint64_t in6 = out_ptr[width * 6];
        uint64_t in7 = out_ptr[width * 7];

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
        out_ptr[width] = out1 ^ in_ptr[width];
        out_ptr[width * 2] = out2 ^ in_ptr[width * 2];
        out_ptr[width * 3] = out3 ^ in_ptr[width * 3];
        out_ptr[width * 4] = out4 ^ in_ptr[width * 4];
        out_ptr[width * 5] = out5 ^ in_ptr[width * 5];
        out_ptr[width * 6] = out6 ^ in_ptr[width * 6];
        out_ptr[width * 7] = out7 ^ in_ptr[width * 7];

        in_ptr++;
        out_ptr++;
    }
}

static void gf8_muladd_3B(uint8_t * out, uint8_t * in, unsigned int width)
{
    unsigned int i;
    uint64_t * in_ptr = (uint64_t *)in;
    uint64_t * out_ptr = (uint64_t *)out;

    for (i = 0; i < width; i++)
    {
        uint64_t out0, out1, out2, out3, out4, out5, out6, out7;
        uint64_t tmp0, tmp1, tmp2;

        uint64_t in0 = out_ptr[0];
        uint64_t in1 = out_ptr[width];
        uint64_t in2 = out_ptr[width * 2];
        uint64_t in3 = out_ptr[width * 3];
        uint64_t in4 = out_ptr[width * 4];
        uint64_t in5 = out_ptr[width * 5];
        uint64_t in6 = out_ptr[width * 6];
        uint64_t in7 = out_ptr[width * 7];

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
        out_ptr[width] = out1 ^ in_ptr[width];
        out_ptr[width * 2] = out2 ^ in_ptr[width * 2];
        out_ptr[width * 3] = out3 ^ in_ptr[width * 3];
        out_ptr[width * 4] = out4 ^ in_ptr[width * 4];
        out_ptr[width * 5] = out5 ^ in_ptr[width * 5];
        out_ptr[width * 6] = out6 ^ in_ptr[width * 6];
        out_ptr[width * 7] = out7 ^ in_ptr[width * 7];

        in_ptr++;
        out_ptr++;
    }
}

static void gf8_muladd_3C(uint8_t * out, uint8_t * in, unsigned int width)
{
    unsigned int i;
    uint64_t * in_ptr = (uint64_t *)in;
    uint64_t * out_ptr = (uint64_t *)out;

    for (i = 0; i < width; i++)
    {
        uint64_t out0, out1, out2, out3, out4, out5, out6, out7;
        uint64_t tmp0, tmp1, tmp2;

        uint64_t in0 = out_ptr[0];
        uint64_t in1 = out_ptr[width];
        uint64_t in2 = out_ptr[width * 2];
        uint64_t in3 = out_ptr[width * 3];
        uint64_t in4 = out_ptr[width * 4];
        uint64_t in5 = out_ptr[width * 5];
        uint64_t in6 = out_ptr[width * 6];
        uint64_t in7 = out_ptr[width * 7];

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
        out_ptr[width] = out1 ^ in_ptr[width];
        out_ptr[width * 2] = out2 ^ in_ptr[width * 2];
        out_ptr[width * 3] = out3 ^ in_ptr[width * 3];
        out_ptr[width * 4] = out4 ^ in_ptr[width * 4];
        out_ptr[width * 5] = out5 ^ in_ptr[width * 5];
        out_ptr[width * 6] = out6 ^ in_ptr[width * 6];
        out_ptr[width * 7] = out7 ^ in_ptr[width * 7];

        in_ptr++;
        out_ptr++;
    }
}

static void gf8_muladd_3D(uint8_t * out, uint8_t * in, unsigned int width)
{
    unsigned int i;
    uint64_t * in_ptr = (uint64_t *)in;
    uint64_t * out_ptr = (uint64_t *)out;

    for (i = 0; i < width; i++)
    {
        uint64_t out0, out1, out2, out3, out4, out5, out6, out7;
        uint64_t tmp0, tmp1, tmp2;

        uint64_t in0 = out_ptr[0];
        uint64_t in1 = out_ptr[width];
        uint64_t in2 = out_ptr[width * 2];
        uint64_t in3 = out_ptr[width * 3];
        uint64_t in4 = out_ptr[width * 4];
        uint64_t in5 = out_ptr[width * 5];
        uint64_t in6 = out_ptr[width * 6];
        uint64_t in7 = out_ptr[width * 7];

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
        out_ptr[width] = out1 ^ in_ptr[width];
        out_ptr[width * 2] = out2 ^ in_ptr[width * 2];
        out_ptr[width * 3] = out3 ^ in_ptr[width * 3];
        out_ptr[width * 4] = out4 ^ in_ptr[width * 4];
        out_ptr[width * 5] = out5 ^ in_ptr[width * 5];
        out_ptr[width * 6] = out6 ^ in_ptr[width * 6];
        out_ptr[width * 7] = out7 ^ in_ptr[width * 7];

        in_ptr++;
        out_ptr++;
    }
}

static void gf8_muladd_3E(uint8_t * out, uint8_t * in, unsigned int width)
{
    unsigned int i;
    uint64_t * in_ptr = (uint64_t *)in;
    uint64_t * out_ptr = (uint64_t *)out;

    for (i = 0; i < width; i++)
    {
        uint64_t out0, out1, out2, out3, out4, out5, out6, out7;
        uint64_t tmp0, tmp1;

        uint64_t in0 = out_ptr[0];
        uint64_t in1 = out_ptr[width];
        uint64_t in2 = out_ptr[width * 2];
        uint64_t in3 = out_ptr[width * 3];
        uint64_t in4 = out_ptr[width * 4];
        uint64_t in5 = out_ptr[width * 5];
        uint64_t in6 = out_ptr[width * 6];
        uint64_t in7 = out_ptr[width * 7];

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
        out_ptr[width] = out1 ^ in_ptr[width];
        out_ptr[width * 2] = out2 ^ in_ptr[width * 2];
        out_ptr[width * 3] = out3 ^ in_ptr[width * 3];
        out_ptr[width * 4] = out4 ^ in_ptr[width * 4];
        out_ptr[width * 5] = out5 ^ in_ptr[width * 5];
        out_ptr[width * 6] = out6 ^ in_ptr[width * 6];
        out_ptr[width * 7] = out7 ^ in_ptr[width * 7];

        in_ptr++;
        out_ptr++;
    }
}

static void gf8_muladd_3F(uint8_t * out, uint8_t * in, unsigned int width)
{
    unsigned int i;
    uint64_t * in_ptr = (uint64_t *)in;
    uint64_t * out_ptr = (uint64_t *)out;

    for (i = 0; i < width; i++)
    {
        uint64_t out0, out1, out2, out3, out4, out5, out6, out7;
        uint64_t tmp0, tmp1, tmp2;

        uint64_t in0 = out_ptr[0];
        uint64_t in1 = out_ptr[width];
        uint64_t in2 = out_ptr[width * 2];
        uint64_t in3 = out_ptr[width * 3];
        uint64_t in4 = out_ptr[width * 4];
        uint64_t in5 = out_ptr[width * 5];
        uint64_t in6 = out_ptr[width * 6];
        uint64_t in7 = out_ptr[width * 7];

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
        out_ptr[width] = out1 ^ in_ptr[width];
        out_ptr[width * 2] = out2 ^ in_ptr[width * 2];
        out_ptr[width * 3] = out3 ^ in_ptr[width * 3];
        out_ptr[width * 4] = out4 ^ in_ptr[width * 4];
        out_ptr[width * 5] = out5 ^ in_ptr[width * 5];
        out_ptr[width * 6] = out6 ^ in_ptr[width * 6];
        out_ptr[width * 7] = out7 ^ in_ptr[width * 7];

        in_ptr++;
        out_ptr++;
    }
}

static void gf8_muladd_40(uint8_t * out, uint8_t * in, unsigned int width)
{
    unsigned int i;
    uint64_t * in_ptr = (uint64_t *)in;
    uint64_t * out_ptr = (uint64_t *)out;

    for (i = 0; i < width; i++)
    {
        uint64_t out0, out1, out2, out3, out4, out5, out6, out7;
        uint64_t tmp0, tmp1;

        uint64_t in0 = out_ptr[0];
        uint64_t in1 = out_ptr[width];
        uint64_t in2 = out_ptr[width * 2];
        uint64_t in3 = out_ptr[width * 3];
        uint64_t in4 = out_ptr[width * 4];
        uint64_t in5 = out_ptr[width * 5];
        uint64_t in6 = out_ptr[width * 6];
        uint64_t in7 = out_ptr[width * 7];

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
        out_ptr[width] = out1 ^ in_ptr[width];
        out_ptr[width * 2] = out2 ^ in_ptr[width * 2];
        out_ptr[width * 3] = out3 ^ in_ptr[width * 3];
        out_ptr[width * 4] = out4 ^ in_ptr[width * 4];
        out_ptr[width * 5] = out5 ^ in_ptr[width * 5];
        out_ptr[width * 6] = out6 ^ in_ptr[width * 6];
        out_ptr[width * 7] = out7 ^ in_ptr[width * 7];

        in_ptr++;
        out_ptr++;
    }
}

static void gf8_muladd_41(uint8_t * out, uint8_t * in, unsigned int width)
{
    unsigned int i;
    uint64_t * in_ptr = (uint64_t *)in;
    uint64_t * out_ptr = (uint64_t *)out;

    for (i = 0; i < width; i++)
    {
        uint64_t out0, out1, out2, out3, out4, out5, out6, out7;
        uint64_t tmp0, tmp1;

        uint64_t in0 = out_ptr[0];
        uint64_t in1 = out_ptr[width];
        uint64_t in2 = out_ptr[width * 2];
        uint64_t in3 = out_ptr[width * 3];
        uint64_t in4 = out_ptr[width * 4];
        uint64_t in5 = out_ptr[width * 5];
        uint64_t in6 = out_ptr[width * 6];
        uint64_t in7 = out_ptr[width * 7];

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
        out_ptr[width] = out1 ^ in_ptr[width];
        out_ptr[width * 2] = out2 ^ in_ptr[width * 2];
        out_ptr[width * 3] = out3 ^ in_ptr[width * 3];
        out_ptr[width * 4] = out4 ^ in_ptr[width * 4];
        out_ptr[width * 5] = out5 ^ in_ptr[width * 5];
        out_ptr[width * 6] = out6 ^ in_ptr[width * 6];
        out_ptr[width * 7] = out7 ^ in_ptr[width * 7];

        in_ptr++;
        out_ptr++;
    }
}

static void gf8_muladd_42(uint8_t * out, uint8_t * in, unsigned int width)
{
    unsigned int i;
    uint64_t * in_ptr = (uint64_t *)in;
    uint64_t * out_ptr = (uint64_t *)out;

    for (i = 0; i < width; i++)
    {
        uint64_t out0, out1, out2, out3, out4, out5, out6, out7;

        uint64_t in0 = out_ptr[0];
        uint64_t in1 = out_ptr[width];
        uint64_t in2 = out_ptr[width * 2];
        uint64_t in3 = out_ptr[width * 3];
        uint64_t in4 = out_ptr[width * 4];
        uint64_t in5 = out_ptr[width * 5];
        uint64_t in6 = out_ptr[width * 6];
        uint64_t in7 = out_ptr[width * 7];

        out0 = in2 ^ in6;
        out5 = in3 ^ in5;
        out1 = in0 ^ in3 ^ in7;
        out7 = in1 ^ in5 ^ in7;
        out4 = in2 ^ in4 ^ in7;
        out6 = in0 ^ in4 ^ in6;
        out2 = out0 ^ in1 ^ in4;
        out3 = out5 ^ in6 ^ in7;

        out_ptr[0] = out0 ^ in_ptr[0];
        out_ptr[width] = out1 ^ in_ptr[width];
        out_ptr[width * 2] = out2 ^ in_ptr[width * 2];
        out_ptr[width * 3] = out3 ^ in_ptr[width * 3];
        out_ptr[width * 4] = out4 ^ in_ptr[width * 4];
        out_ptr[width * 5] = out5 ^ in_ptr[width * 5];
        out_ptr[width * 6] = out6 ^ in_ptr[width * 6];
        out_ptr[width * 7] = out7 ^ in_ptr[width * 7];

        in_ptr++;
        out_ptr++;
    }
}

static void gf8_muladd_43(uint8_t * out, uint8_t * in, unsigned int width)
{
    unsigned int i;
    uint64_t * in_ptr = (uint64_t *)in;
    uint64_t * out_ptr = (uint64_t *)out;

    for (i = 0; i < width; i++)
    {
        uint64_t out0, out1, out2, out3, out4, out5, out6, out7;

        uint64_t in0 = out_ptr[0];
        uint64_t in1 = out_ptr[width];
        uint64_t in2 = out_ptr[width * 2];
        uint64_t in3 = out_ptr[width * 3];
        uint64_t in4 = out_ptr[width * 4];
        uint64_t in5 = out_ptr[width * 5];
        uint64_t in6 = out_ptr[width * 6];
        uint64_t in7 = out_ptr[width * 7];

        out5 = in3;
        out7 = in1 ^ in5;
        out4 = in2 ^ in7;
        out6 = in0 ^ in4;
        out0 = in0 ^ in2 ^ in6;
        out3 = in5 ^ in6 ^ in7;
        out2 = in1 ^ in4 ^ in6;
        out1 = in0 ^ in1 ^ in3 ^ in7;

        out_ptr[0] = out0 ^ in_ptr[0];
        out_ptr[width] = out1 ^ in_ptr[width];
        out_ptr[width * 2] = out2 ^ in_ptr[width * 2];
        out_ptr[width * 3] = out3 ^ in_ptr[width * 3];
        out_ptr[width * 4] = out4 ^ in_ptr[width * 4];
        out_ptr[width * 5] = out5 ^ in_ptr[width * 5];
        out_ptr[width * 6] = out6 ^ in_ptr[width * 6];
        out_ptr[width * 7] = out7 ^ in_ptr[width * 7];

        in_ptr++;
        out_ptr++;
    }
}

static void gf8_muladd_44(uint8_t * out, uint8_t * in, unsigned int width)
{
    unsigned int i;
    uint64_t * in_ptr = (uint64_t *)in;
    uint64_t * out_ptr = (uint64_t *)out;

    for (i = 0; i < width; i++)
    {
        uint64_t out0, out1, out2, out3, out4, out5, out6, out7;
        uint64_t tmp0;

        uint64_t in0 = out_ptr[0];
        uint64_t in1 = out_ptr[width];
        uint64_t in2 = out_ptr[width * 2];
        uint64_t in3 = out_ptr[width * 3];
        uint64_t in4 = out_ptr[width * 4];
        uint64_t in5 = out_ptr[width * 5];
        uint64_t in6 = out_ptr[width * 6];
        uint64_t in7 = out_ptr[width * 7];

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
        out_ptr[width] = out1 ^ in_ptr[width];
        out_ptr[width * 2] = out2 ^ in_ptr[width * 2];
        out_ptr[width * 3] = out3 ^ in_ptr[width * 3];
        out_ptr[width * 4] = out4 ^ in_ptr[width * 4];
        out_ptr[width * 5] = out5 ^ in_ptr[width * 5];
        out_ptr[width * 6] = out6 ^ in_ptr[width * 6];
        out_ptr[width * 7] = out7 ^ in_ptr[width * 7];

        in_ptr++;
        out_ptr++;
    }
}

static void gf8_muladd_45(uint8_t * out, uint8_t * in, unsigned int width)
{
    unsigned int i;
    uint64_t * in_ptr = (uint64_t *)in;
    uint64_t * out_ptr = (uint64_t *)out;

    for (i = 0; i < width; i++)
    {
        uint64_t out0, out1, out2, out3, out4, out5, out6, out7;

        uint64_t in0 = out_ptr[0];
        uint64_t in1 = out_ptr[width];
        uint64_t in2 = out_ptr[width * 2];
        uint64_t in3 = out_ptr[width * 3];
        uint64_t in4 = out_ptr[width * 4];
        uint64_t in5 = out_ptr[width * 5];
        uint64_t in6 = out_ptr[width * 6];
        uint64_t in7 = out_ptr[width * 7];

        out1 = in1 ^ in3;
        out7 = in1 ^ in6;
        out5 = in4 ^ in7;
        out6 = in0 ^ in5;
        out0 = in0 ^ in2 ^ in7;
        out4 = in3 ^ in6 ^ in7;
        out2 = out5 ^ in0;
        out3 = out0 ^ out6 ^ in1;

        out_ptr[0] = out0 ^ in_ptr[0];
        out_ptr[width] = out1 ^ in_ptr[width];
        out_ptr[width * 2] = out2 ^ in_ptr[width * 2];
        out_ptr[width * 3] = out3 ^ in_ptr[width * 3];
        out_ptr[width * 4] = out4 ^ in_ptr[width * 4];
        out_ptr[width * 5] = out5 ^ in_ptr[width * 5];
        out_ptr[width * 6] = out6 ^ in_ptr[width * 6];
        out_ptr[width * 7] = out7 ^ in_ptr[width * 7];

        in_ptr++;
        out_ptr++;
    }
}

static void gf8_muladd_46(uint8_t * out, uint8_t * in, unsigned int width)
{
    unsigned int i;
    uint64_t * in_ptr = (uint64_t *)in;
    uint64_t * out_ptr = (uint64_t *)out;

    for (i = 0; i < width; i++)
    {
        uint64_t out0, out1, out2, out3, out4, out5, out6, out7;

        uint64_t in0 = out_ptr[0];
        uint64_t in1 = out_ptr[width];
        uint64_t in2 = out_ptr[width * 2];
        uint64_t in3 = out_ptr[width * 3];
        uint64_t in4 = out_ptr[width * 4];
        uint64_t in5 = out_ptr[width * 5];
        uint64_t in6 = out_ptr[width * 6];
        uint64_t in7 = out_ptr[width * 7];

        out0 = in2;
        out1 = in0 ^ in3;
        out7 = in1 ^ in7;
        out4 = in4 ^ in6;
        out5 = in5 ^ in7;
        out6 = in0 ^ in6;
        out3 = in1 ^ in3 ^ in5;
        out2 = out4 ^ out6 ^ in1 ^ in2;

        out_ptr[0] = out0 ^ in_ptr[0];
        out_ptr[width] = out1 ^ in_ptr[width];
        out_ptr[width * 2] = out2 ^ in_ptr[width * 2];
        out_ptr[width * 3] = out3 ^ in_ptr[width * 3];
        out_ptr[width * 4] = out4 ^ in_ptr[width * 4];
        out_ptr[width * 5] = out5 ^ in_ptr[width * 5];
        out_ptr[width * 6] = out6 ^ in_ptr[width * 6];
        out_ptr[width * 7] = out7 ^ in_ptr[width * 7];

        in_ptr++;
        out_ptr++;
    }
}

static void gf8_muladd_47(uint8_t * out, uint8_t * in, unsigned int width)
{
    unsigned int i;
    uint64_t * in_ptr = (uint64_t *)in;
    uint64_t * out_ptr = (uint64_t *)out;

    for (i = 0; i < width; i++)
    {
        uint64_t out0, out1, out2, out3, out4, out5, out6, out7;
        uint64_t tmp0;

        uint64_t in0 = out_ptr[0];
        uint64_t in1 = out_ptr[width];
        uint64_t in2 = out_ptr[width * 2];
        uint64_t in3 = out_ptr[width * 3];
        uint64_t in4 = out_ptr[width * 4];
        uint64_t in5 = out_ptr[width * 5];
        uint64_t in6 = out_ptr[width * 6];
        uint64_t in7 = out_ptr[width * 7];

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
        out_ptr[width] = out1 ^ in_ptr[width];
        out_ptr[width * 2] = out2 ^ in_ptr[width * 2];
        out_ptr[width * 3] = out3 ^ in_ptr[width * 3];
        out_ptr[width * 4] = out4 ^ in_ptr[width * 4];
        out_ptr[width * 5] = out5 ^ in_ptr[width * 5];
        out_ptr[width * 6] = out6 ^ in_ptr[width * 6];
        out_ptr[width * 7] = out7 ^ in_ptr[width * 7];

        in_ptr++;
        out_ptr++;
    }
}

static void gf8_muladd_48(uint8_t * out, uint8_t * in, unsigned int width)
{
    unsigned int i;
    uint64_t * in_ptr = (uint64_t *)in;
    uint64_t * out_ptr = (uint64_t *)out;

    for (i = 0; i < width; i++)
    {
        uint64_t out0, out1, out2, out3, out4, out5, out6, out7;
        uint64_t tmp0, tmp1;

        uint64_t in0 = out_ptr[0];
        uint64_t in1 = out_ptr[width];
        uint64_t in2 = out_ptr[width * 2];
        uint64_t in3 = out_ptr[width * 3];
        uint64_t in4 = out_ptr[width * 4];
        uint64_t in5 = out_ptr[width * 5];
        uint64_t in6 = out_ptr[width * 6];
        uint64_t in7 = out_ptr[width * 7];

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
        out_ptr[width] = out1 ^ in_ptr[width];
        out_ptr[width * 2] = out2 ^ in_ptr[width * 2];
        out_ptr[width * 3] = out3 ^ in_ptr[width * 3];
        out_ptr[width * 4] = out4 ^ in_ptr[width * 4];
        out_ptr[width * 5] = out5 ^ in_ptr[width * 5];
        out_ptr[width * 6] = out6 ^ in_ptr[width * 6];
        out_ptr[width * 7] = out7 ^ in_ptr[width * 7];

        in_ptr++;
        out_ptr++;
    }
}

static void gf8_muladd_49(uint8_t * out, uint8_t * in, unsigned int width)
{
    unsigned int i;
    uint64_t * in_ptr = (uint64_t *)in;
    uint64_t * out_ptr = (uint64_t *)out;

    for (i = 0; i < width; i++)
    {
        uint64_t out0, out1, out2, out3, out4, out5, out6, out7;
        uint64_t tmp0, tmp1;

        uint64_t in0 = out_ptr[0];
        uint64_t in1 = out_ptr[width];
        uint64_t in2 = out_ptr[width * 2];
        uint64_t in3 = out_ptr[width * 3];
        uint64_t in4 = out_ptr[width * 4];
        uint64_t in5 = out_ptr[width * 5];
        uint64_t in6 = out_ptr[width * 6];
        uint64_t in7 = out_ptr[width * 7];

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
        out_ptr[width] = out1 ^ in_ptr[width];
        out_ptr[width * 2] = out2 ^ in_ptr[width * 2];
        out_ptr[width * 3] = out3 ^ in_ptr[width * 3];
        out_ptr[width * 4] = out4 ^ in_ptr[width * 4];
        out_ptr[width * 5] = out5 ^ in_ptr[width * 5];
        out_ptr[width * 6] = out6 ^ in_ptr[width * 6];
        out_ptr[width * 7] = out7 ^ in_ptr[width * 7];

        in_ptr++;
        out_ptr++;
    }
}

static void gf8_muladd_4A(uint8_t * out, uint8_t * in, unsigned int width)
{
    unsigned int i;
    uint64_t * in_ptr = (uint64_t *)in;
    uint64_t * out_ptr = (uint64_t *)out;

    for (i = 0; i < width; i++)
    {
        uint64_t out0, out1, out2, out3, out4, out5, out6, out7;
        uint64_t tmp0, tmp1;

        uint64_t in0 = out_ptr[0];
        uint64_t in1 = out_ptr[width];
        uint64_t in2 = out_ptr[width * 2];
        uint64_t in3 = out_ptr[width * 3];
        uint64_t in4 = out_ptr[width * 4];
        uint64_t in5 = out_ptr[width * 5];
        uint64_t in6 = out_ptr[width * 6];
        uint64_t in7 = out_ptr[width * 7];

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
        out_ptr[width] = out1 ^ in_ptr[width];
        out_ptr[width * 2] = out2 ^ in_ptr[width * 2];
        out_ptr[width * 3] = out3 ^ in_ptr[width * 3];
        out_ptr[width * 4] = out4 ^ in_ptr[width * 4];
        out_ptr[width * 5] = out5 ^ in_ptr[width * 5];
        out_ptr[width * 6] = out6 ^ in_ptr[width * 6];
        out_ptr[width * 7] = out7 ^ in_ptr[width * 7];

        in_ptr++;
        out_ptr++;
    }
}

static void gf8_muladd_4B(uint8_t * out, uint8_t * in, unsigned int width)
{
    unsigned int i;
    uint64_t * in_ptr = (uint64_t *)in;
    uint64_t * out_ptr = (uint64_t *)out;

    for (i = 0; i < width; i++)
    {
        uint64_t out0, out1, out2, out3, out4, out5, out6, out7;
        uint64_t tmp0, tmp1, tmp2, tmp3;

        uint64_t in0 = out_ptr[0];
        uint64_t in1 = out_ptr[width];
        uint64_t in2 = out_ptr[width * 2];
        uint64_t in3 = out_ptr[width * 3];
        uint64_t in4 = out_ptr[width * 4];
        uint64_t in5 = out_ptr[width * 5];
        uint64_t in6 = out_ptr[width * 6];
        uint64_t in7 = out_ptr[width * 7];

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
        out_ptr[width] = out1 ^ in_ptr[width];
        out_ptr[width * 2] = out2 ^ in_ptr[width * 2];
        out_ptr[width * 3] = out3 ^ in_ptr[width * 3];
        out_ptr[width * 4] = out4 ^ in_ptr[width * 4];
        out_ptr[width * 5] = out5 ^ in_ptr[width * 5];
        out_ptr[width * 6] = out6 ^ in_ptr[width * 6];
        out_ptr[width * 7] = out7 ^ in_ptr[width * 7];

        in_ptr++;
        out_ptr++;
    }
}

static void gf8_muladd_4C(uint8_t * out, uint8_t * in, unsigned int width)
{
    unsigned int i;
    uint64_t * in_ptr = (uint64_t *)in;
    uint64_t * out_ptr = (uint64_t *)out;

    for (i = 0; i < width; i++)
    {
        uint64_t out0, out1, out2, out3, out4, out5, out6, out7;
        uint64_t tmp0, tmp1, tmp2;

        uint64_t in0 = out_ptr[0];
        uint64_t in1 = out_ptr[width];
        uint64_t in2 = out_ptr[width * 2];
        uint64_t in3 = out_ptr[width * 3];
        uint64_t in4 = out_ptr[width * 4];
        uint64_t in5 = out_ptr[width * 5];
        uint64_t in6 = out_ptr[width * 6];
        uint64_t in7 = out_ptr[width * 7];

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
        out_ptr[width] = out1 ^ in_ptr[width];
        out_ptr[width * 2] = out2 ^ in_ptr[width * 2];
        out_ptr[width * 3] = out3 ^ in_ptr[width * 3];
        out_ptr[width * 4] = out4 ^ in_ptr[width * 4];
        out_ptr[width * 5] = out5 ^ in_ptr[width * 5];
        out_ptr[width * 6] = out6 ^ in_ptr[width * 6];
        out_ptr[width * 7] = out7 ^ in_ptr[width * 7];

        in_ptr++;
        out_ptr++;
    }
}

static void gf8_muladd_4D(uint8_t * out, uint8_t * in, unsigned int width)
{
    unsigned int i;
    uint64_t * in_ptr = (uint64_t *)in;
    uint64_t * out_ptr = (uint64_t *)out;

    for (i = 0; i < width; i++)
    {
        uint64_t out0, out1, out2, out3, out4, out5, out6, out7;
        uint64_t tmp0, tmp1, tmp2;

        uint64_t in0 = out_ptr[0];
        uint64_t in1 = out_ptr[width];
        uint64_t in2 = out_ptr[width * 2];
        uint64_t in3 = out_ptr[width * 3];
        uint64_t in4 = out_ptr[width * 4];
        uint64_t in5 = out_ptr[width * 5];
        uint64_t in6 = out_ptr[width * 6];
        uint64_t in7 = out_ptr[width * 7];

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
        out_ptr[width] = out1 ^ in_ptr[width];
        out_ptr[width * 2] = out2 ^ in_ptr[width * 2];
        out_ptr[width * 3] = out3 ^ in_ptr[width * 3];
        out_ptr[width * 4] = out4 ^ in_ptr[width * 4];
        out_ptr[width * 5] = out5 ^ in_ptr[width * 5];
        out_ptr[width * 6] = out6 ^ in_ptr[width * 6];
        out_ptr[width * 7] = out7 ^ in_ptr[width * 7];

        in_ptr++;
        out_ptr++;
    }
}

static void gf8_muladd_4E(uint8_t * out, uint8_t * in, unsigned int width)
{
    unsigned int i;
    uint64_t * in_ptr = (uint64_t *)in;
    uint64_t * out_ptr = (uint64_t *)out;

    for (i = 0; i < width; i++)
    {
        uint64_t out0, out1, out2, out3, out4, out5, out6, out7;

        uint64_t in0 = out_ptr[0];
        uint64_t in1 = out_ptr[width];
        uint64_t in2 = out_ptr[width * 2];
        uint64_t in3 = out_ptr[width * 3];
        uint64_t in4 = out_ptr[width * 4];
        uint64_t in5 = out_ptr[width * 5];
        uint64_t in6 = out_ptr[width * 6];
        uint64_t in7 = out_ptr[width * 7];

        out0 = in2 ^ in5;
        out7 = in1 ^ in4 ^ in7;
        out1 = in0 ^ in3 ^ in6;
        out5 = out0 ^ in6;
        out4 = out7 ^ in5;
        out3 = out1 ^ in1;
        out6 = out1 ^ in7;
        out2 = out4 ^ in0 ^ in2;

        out_ptr[0] = out0 ^ in_ptr[0];
        out_ptr[width] = out1 ^ in_ptr[width];
        out_ptr[width * 2] = out2 ^ in_ptr[width * 2];
        out_ptr[width * 3] = out3 ^ in_ptr[width * 3];
        out_ptr[width * 4] = out4 ^ in_ptr[width * 4];
        out_ptr[width * 5] = out5 ^ in_ptr[width * 5];
        out_ptr[width * 6] = out6 ^ in_ptr[width * 6];
        out_ptr[width * 7] = out7 ^ in_ptr[width * 7];

        in_ptr++;
        out_ptr++;
    }
}

static void gf8_muladd_4F(uint8_t * out, uint8_t * in, unsigned int width)
{
    unsigned int i;
    uint64_t * in_ptr = (uint64_t *)in;
    uint64_t * out_ptr = (uint64_t *)out;

    for (i = 0; i < width; i++)
    {
        uint64_t out0, out1, out2, out3, out4, out5, out6, out7;

        uint64_t in0 = out_ptr[0];
        uint64_t in1 = out_ptr[width];
        uint64_t in2 = out_ptr[width * 2];
        uint64_t in3 = out_ptr[width * 3];
        uint64_t in4 = out_ptr[width * 4];
        uint64_t in5 = out_ptr[width * 5];
        uint64_t in6 = out_ptr[width * 6];
        uint64_t in7 = out_ptr[width * 7];

        out5 = in2 ^ in6;
        out7 = in1 ^ in4;
        out3 = in0 ^ in1 ^ in6;
        out4 = in1 ^ in5 ^ in7;
        out0 = in0 ^ in2 ^ in5;
        out6 = in0 ^ in3 ^ in7;
        out1 = out3 ^ in3;
        out2 = out4 ^ in0 ^ in4;

        out_ptr[0] = out0 ^ in_ptr[0];
        out_ptr[width] = out1 ^ in_ptr[width];
        out_ptr[width * 2] = out2 ^ in_ptr[width * 2];
        out_ptr[width * 3] = out3 ^ in_ptr[width * 3];
        out_ptr[width * 4] = out4 ^ in_ptr[width * 4];
        out_ptr[width * 5] = out5 ^ in_ptr[width * 5];
        out_ptr[width * 6] = out6 ^ in_ptr[width * 6];
        out_ptr[width * 7] = out7 ^ in_ptr[width * 7];

        in_ptr++;
        out_ptr++;
    }
}

static void gf8_muladd_50(uint8_t * out, uint8_t * in, unsigned int width)
{
    unsigned int i;
    uint64_t * in_ptr = (uint64_t *)in;
    uint64_t * out_ptr = (uint64_t *)out;

    for (i = 0; i < width; i++)
    {
        uint64_t out0, out1, out2, out3, out4, out5, out6, out7;
        uint64_t tmp0, tmp1, tmp2;

        uint64_t in0 = out_ptr[0];
        uint64_t in1 = out_ptr[width];
        uint64_t in2 = out_ptr[width * 2];
        uint64_t in3 = out_ptr[width * 3];
        uint64_t in4 = out_ptr[width * 4];
        uint64_t in5 = out_ptr[width * 5];
        uint64_t in6 = out_ptr[width * 6];
        uint64_t in7 = out_ptr[width * 7];

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
        out_ptr[width] = out1 ^ in_ptr[width];
        out_ptr[width * 2] = out2 ^ in_ptr[width * 2];
        out_ptr[width * 3] = out3 ^ in_ptr[width * 3];
        out_ptr[width * 4] = out4 ^ in_ptr[width * 4];
        out_ptr[width * 5] = out5 ^ in_ptr[width * 5];
        out_ptr[width * 6] = out6 ^ in_ptr[width * 6];
        out_ptr[width * 7] = out7 ^ in_ptr[width * 7];

        in_ptr++;
        out_ptr++;
    }
}

static void gf8_muladd_51(uint8_t * out, uint8_t * in, unsigned int width)
{
    unsigned int i;
    uint64_t * in_ptr = (uint64_t *)in;
    uint64_t * out_ptr = (uint64_t *)out;

    for (i = 0; i < width; i++)
    {
        uint64_t out0, out1, out2, out3, out4, out5, out6, out7;

        uint64_t in0 = out_ptr[0];
        uint64_t in1 = out_ptr[width];
        uint64_t in2 = out_ptr[width * 2];
        uint64_t in3 = out_ptr[width * 3];
        uint64_t in4 = out_ptr[width * 4];
        uint64_t in5 = out_ptr[width * 5];
        uint64_t in6 = out_ptr[width * 6];
        uint64_t in7 = out_ptr[width * 7];

        out2 = in7;
        out3 = in2 ^ in4 ^ in6 ^ in7;
        out0 = out3 ^ in0;
        out6 = out0 ^ in5;
        out4 = out6 ^ in3 ^ in7;
        out1 = out0 ^ out4 ^ in1;
        out7 = out1 ^ in6;
        out5 = out7 ^ in4;

        out_ptr[0] = out0 ^ in_ptr[0];
        out_ptr[width] = out1 ^ in_ptr[width];
        out_ptr[width * 2] = out2 ^ in_ptr[width * 2];
        out_ptr[width * 3] = out3 ^ in_ptr[width * 3];
        out_ptr[width * 4] = out4 ^ in_ptr[width * 4];
        out_ptr[width * 5] = out5 ^ in_ptr[width * 5];
        out_ptr[width * 6] = out6 ^ in_ptr[width * 6];
        out_ptr[width * 7] = out7 ^ in_ptr[width * 7];

        in_ptr++;
        out_ptr++;
    }
}

static void gf8_muladd_52(uint8_t * out, uint8_t * in, unsigned int width)
{
    unsigned int i;
    uint64_t * in_ptr = (uint64_t *)in;
    uint64_t * out_ptr = (uint64_t *)out;

    for (i = 0; i < width; i++)
    {
        uint64_t out0, out1, out2, out3, out4, out5, out6, out7;
        uint64_t tmp0, tmp1, tmp2, tmp3;

        uint64_t in0 = out_ptr[0];
        uint64_t in1 = out_ptr[width];
        uint64_t in2 = out_ptr[width * 2];
        uint64_t in3 = out_ptr[width * 3];
        uint64_t in4 = out_ptr[width * 4];
        uint64_t in5 = out_ptr[width * 5];
        uint64_t in6 = out_ptr[width * 6];
        uint64_t in7 = out_ptr[width * 7];

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
        out_ptr[width] = out1 ^ in_ptr[width];
        out_ptr[width * 2] = out2 ^ in_ptr[width * 2];
        out_ptr[width * 3] = out3 ^ in_ptr[width * 3];
        out_ptr[width * 4] = out4 ^ in_ptr[width * 4];
        out_ptr[width * 5] = out5 ^ in_ptr[width * 5];
        out_ptr[width * 6] = out6 ^ in_ptr[width * 6];
        out_ptr[width * 7] = out7 ^ in_ptr[width * 7];

        in_ptr++;
        out_ptr++;
    }
}

static void gf8_muladd_53(uint8_t * out, uint8_t * in, unsigned int width)
{
    unsigned int i;
    uint64_t * in_ptr = (uint64_t *)in;
    uint64_t * out_ptr = (uint64_t *)out;

    for (i = 0; i < width; i++)
    {
        uint64_t out0, out1, out2, out3, out4, out5, out6, out7;

        uint64_t in0 = out_ptr[0];
        uint64_t in1 = out_ptr[width];
        uint64_t in2 = out_ptr[width * 2];
        uint64_t in3 = out_ptr[width * 3];
        uint64_t in4 = out_ptr[width * 4];
        uint64_t in5 = out_ptr[width * 5];
        uint64_t in6 = out_ptr[width * 6];
        uint64_t in7 = out_ptr[width * 7];

        out2 = in1;
        out3 = in4 ^ in6;
        out0 = out3 ^ in0 ^ in2;
        out6 = out0 ^ in7;
        out4 = out6 ^ in5;
        out7 = out0 ^ out4 ^ in1 ^ in3;
        out1 = out7 ^ in0;
        out5 = out7 ^ in6;

        out_ptr[0] = out0 ^ in_ptr[0];
        out_ptr[width] = out1 ^ in_ptr[width];
        out_ptr[width * 2] = out2 ^ in_ptr[width * 2];
        out_ptr[width * 3] = out3 ^ in_ptr[width * 3];
        out_ptr[width * 4] = out4 ^ in_ptr[width * 4];
        out_ptr[width * 5] = out5 ^ in_ptr[width * 5];
        out_ptr[width * 6] = out6 ^ in_ptr[width * 6];
        out_ptr[width * 7] = out7 ^ in_ptr[width * 7];

        in_ptr++;
        out_ptr++;
    }
}

static void gf8_muladd_54(uint8_t * out, uint8_t * in, unsigned int width)
{
    unsigned int i;
    uint64_t * in_ptr = (uint64_t *)in;
    uint64_t * out_ptr = (uint64_t *)out;

    for (i = 0; i < width; i++)
    {
        uint64_t out0, out1, out2, out3, out4, out5, out6, out7;
        uint64_t tmp0, tmp1, tmp2, tmp3;

        uint64_t in0 = out_ptr[0];
        uint64_t in1 = out_ptr[width];
        uint64_t in2 = out_ptr[width * 2];
        uint64_t in3 = out_ptr[width * 3];
        uint64_t in4 = out_ptr[width * 4];
        uint64_t in5 = out_ptr[width * 5];
        uint64_t in6 = out_ptr[width * 6];
        uint64_t in7 = out_ptr[width * 7];

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
        out_ptr[width] = out1 ^ in_ptr[width];
        out_ptr[width * 2] = out2 ^ in_ptr[width * 2];
        out_ptr[width * 3] = out3 ^ in_ptr[width * 3];
        out_ptr[width * 4] = out4 ^ in_ptr[width * 4];
        out_ptr[width * 5] = out5 ^ in_ptr[width * 5];
        out_ptr[width * 6] = out6 ^ in_ptr[width * 6];
        out_ptr[width * 7] = out7 ^ in_ptr[width * 7];

        in_ptr++;
        out_ptr++;
    }
}

static void gf8_muladd_55(uint8_t * out, uint8_t * in, unsigned int width)
{
    unsigned int i;
    uint64_t * in_ptr = (uint64_t *)in;
    uint64_t * out_ptr = (uint64_t *)out;

    for (i = 0; i < width; i++)
    {
        uint64_t out0, out1, out2, out3, out4, out5, out6, out7;
        uint64_t tmp0, tmp1, tmp2;

        uint64_t in0 = out_ptr[0];
        uint64_t in1 = out_ptr[width];
        uint64_t in2 = out_ptr[width * 2];
        uint64_t in3 = out_ptr[width * 3];
        uint64_t in4 = out_ptr[width * 4];
        uint64_t in5 = out_ptr[width * 5];
        uint64_t in6 = out_ptr[width * 6];
        uint64_t in7 = out_ptr[width * 7];

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
        out_ptr[width] = out1 ^ in_ptr[width];
        out_ptr[width * 2] = out2 ^ in_ptr[width * 2];
        out_ptr[width * 3] = out3 ^ in_ptr[width * 3];
        out_ptr[width * 4] = out4 ^ in_ptr[width * 4];
        out_ptr[width * 5] = out5 ^ in_ptr[width * 5];
        out_ptr[width * 6] = out6 ^ in_ptr[width * 6];
        out_ptr[width * 7] = out7 ^ in_ptr[width * 7];

        in_ptr++;
        out_ptr++;
    }
}

static void gf8_muladd_56(uint8_t * out, uint8_t * in, unsigned int width)
{
    unsigned int i;
    uint64_t * in_ptr = (uint64_t *)in;
    uint64_t * out_ptr = (uint64_t *)out;

    for (i = 0; i < width; i++)
    {
        uint64_t out0, out1, out2, out3, out4, out5, out6, out7;
        uint64_t tmp0;

        uint64_t in0 = out_ptr[0];
        uint64_t in1 = out_ptr[width];
        uint64_t in2 = out_ptr[width * 2];
        uint64_t in3 = out_ptr[width * 3];
        uint64_t in4 = out_ptr[width * 4];
        uint64_t in5 = out_ptr[width * 5];
        uint64_t in6 = out_ptr[width * 6];
        uint64_t in7 = out_ptr[width * 7];

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
        out_ptr[width] = out1 ^ in_ptr[width];
        out_ptr[width * 2] = out2 ^ in_ptr[width * 2];
        out_ptr[width * 3] = out3 ^ in_ptr[width * 3];
        out_ptr[width * 4] = out4 ^ in_ptr[width * 4];
        out_ptr[width * 5] = out5 ^ in_ptr[width * 5];
        out_ptr[width * 6] = out6 ^ in_ptr[width * 6];
        out_ptr[width * 7] = out7 ^ in_ptr[width * 7];

        in_ptr++;
        out_ptr++;
    }
}

static void gf8_muladd_57(uint8_t * out, uint8_t * in, unsigned int width)
{
    unsigned int i;
    uint64_t * in_ptr = (uint64_t *)in;
    uint64_t * out_ptr = (uint64_t *)out;

    for (i = 0; i < width; i++)
    {
        uint64_t out0, out1, out2, out3, out4, out5, out6, out7;
        uint64_t tmp0, tmp1;

        uint64_t in0 = out_ptr[0];
        uint64_t in1 = out_ptr[width];
        uint64_t in2 = out_ptr[width * 2];
        uint64_t in3 = out_ptr[width * 3];
        uint64_t in4 = out_ptr[width * 4];
        uint64_t in5 = out_ptr[width * 5];
        uint64_t in6 = out_ptr[width * 6];
        uint64_t in7 = out_ptr[width * 7];

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
        out_ptr[width] = out1 ^ in_ptr[width];
        out_ptr[width * 2] = out2 ^ in_ptr[width * 2];
        out_ptr[width * 3] = out3 ^ in_ptr[width * 3];
        out_ptr[width * 4] = out4 ^ in_ptr[width * 4];
        out_ptr[width * 5] = out5 ^ in_ptr[width * 5];
        out_ptr[width * 6] = out6 ^ in_ptr[width * 6];
        out_ptr[width * 7] = out7 ^ in_ptr[width * 7];

        in_ptr++;
        out_ptr++;
    }
}

static void gf8_muladd_58(uint8_t * out, uint8_t * in, unsigned int width)
{
    unsigned int i;
    uint64_t * in_ptr = (uint64_t *)in;
    uint64_t * out_ptr = (uint64_t *)out;

    for (i = 0; i < width; i++)
    {
        uint64_t out0, out1, out2, out3, out4, out5, out6, out7;
        uint64_t tmp0, tmp1;

        uint64_t in0 = out_ptr[0];
        uint64_t in1 = out_ptr[width];
        uint64_t in2 = out_ptr[width * 2];
        uint64_t in3 = out_ptr[width * 3];
        uint64_t in4 = out_ptr[width * 4];
        uint64_t in5 = out_ptr[width * 5];
        uint64_t in6 = out_ptr[width * 6];
        uint64_t in7 = out_ptr[width * 7];

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
        out_ptr[width] = out1 ^ in_ptr[width];
        out_ptr[width * 2] = out2 ^ in_ptr[width * 2];
        out_ptr[width * 3] = out3 ^ in_ptr[width * 3];
        out_ptr[width * 4] = out4 ^ in_ptr[width * 4];
        out_ptr[width * 5] = out5 ^ in_ptr[width * 5];
        out_ptr[width * 6] = out6 ^ in_ptr[width * 6];
        out_ptr[width * 7] = out7 ^ in_ptr[width * 7];

        in_ptr++;
        out_ptr++;
    }
}

static void gf8_muladd_59(uint8_t * out, uint8_t * in, unsigned int width)
{
    unsigned int i;
    uint64_t * in_ptr = (uint64_t *)in;
    uint64_t * out_ptr = (uint64_t *)out;

    for (i = 0; i < width; i++)
    {
        uint64_t out0, out1, out2, out3, out4, out5, out6, out7;
        uint64_t tmp0, tmp1;

        uint64_t in0 = out_ptr[0];
        uint64_t in1 = out_ptr[width];
        uint64_t in2 = out_ptr[width * 2];
        uint64_t in3 = out_ptr[width * 3];
        uint64_t in4 = out_ptr[width * 4];
        uint64_t in5 = out_ptr[width * 5];
        uint64_t in6 = out_ptr[width * 6];
        uint64_t in7 = out_ptr[width * 7];

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
        out_ptr[width] = out1 ^ in_ptr[width];
        out_ptr[width * 2] = out2 ^ in_ptr[width * 2];
        out_ptr[width * 3] = out3 ^ in_ptr[width * 3];
        out_ptr[width * 4] = out4 ^ in_ptr[width * 4];
        out_ptr[width * 5] = out5 ^ in_ptr[width * 5];
        out_ptr[width * 6] = out6 ^ in_ptr[width * 6];
        out_ptr[width * 7] = out7 ^ in_ptr[width * 7];

        in_ptr++;
        out_ptr++;
    }
}

static void gf8_muladd_5A(uint8_t * out, uint8_t * in, unsigned int width)
{
    unsigned int i;
    uint64_t * in_ptr = (uint64_t *)in;
    uint64_t * out_ptr = (uint64_t *)out;

    for (i = 0; i < width; i++)
    {
        uint64_t out0, out1, out2, out3, out4, out5, out6, out7;
        uint64_t tmp0, tmp1, tmp2;

        uint64_t in0 = out_ptr[0];
        uint64_t in1 = out_ptr[width];
        uint64_t in2 = out_ptr[width * 2];
        uint64_t in3 = out_ptr[width * 3];
        uint64_t in4 = out_ptr[width * 4];
        uint64_t in5 = out_ptr[width * 5];
        uint64_t in6 = out_ptr[width * 6];
        uint64_t in7 = out_ptr[width * 7];

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
        out_ptr[width] = out1 ^ in_ptr[width];
        out_ptr[width * 2] = out2 ^ in_ptr[width * 2];
        out_ptr[width * 3] = out3 ^ in_ptr[width * 3];
        out_ptr[width * 4] = out4 ^ in_ptr[width * 4];
        out_ptr[width * 5] = out5 ^ in_ptr[width * 5];
        out_ptr[width * 6] = out6 ^ in_ptr[width * 6];
        out_ptr[width * 7] = out7 ^ in_ptr[width * 7];

        in_ptr++;
        out_ptr++;
    }
}

static void gf8_muladd_5B(uint8_t * out, uint8_t * in, unsigned int width)
{
    unsigned int i;
    uint64_t * in_ptr = (uint64_t *)in;
    uint64_t * out_ptr = (uint64_t *)out;

    for (i = 0; i < width; i++)
    {
        uint64_t out0, out1, out2, out3, out4, out5, out6, out7;
        uint64_t tmp0, tmp1, tmp2, tmp3, tmp4;

        uint64_t in0 = out_ptr[0];
        uint64_t in1 = out_ptr[width];
        uint64_t in2 = out_ptr[width * 2];
        uint64_t in3 = out_ptr[width * 3];
        uint64_t in4 = out_ptr[width * 4];
        uint64_t in5 = out_ptr[width * 5];
        uint64_t in6 = out_ptr[width * 6];
        uint64_t in7 = out_ptr[width * 7];

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
        out_ptr[width] = out1 ^ in_ptr[width];
        out_ptr[width * 2] = out2 ^ in_ptr[width * 2];
        out_ptr[width * 3] = out3 ^ in_ptr[width * 3];
        out_ptr[width * 4] = out4 ^ in_ptr[width * 4];
        out_ptr[width * 5] = out5 ^ in_ptr[width * 5];
        out_ptr[width * 6] = out6 ^ in_ptr[width * 6];
        out_ptr[width * 7] = out7 ^ in_ptr[width * 7];

        in_ptr++;
        out_ptr++;
    }
}

static void gf8_muladd_5C(uint8_t * out, uint8_t * in, unsigned int width)
{
    unsigned int i;
    uint64_t * in_ptr = (uint64_t *)in;
    uint64_t * out_ptr = (uint64_t *)out;

    for (i = 0; i < width; i++)
    {
        uint64_t out0, out1, out2, out3, out4, out5, out6, out7;
        uint64_t tmp0, tmp1, tmp2;

        uint64_t in0 = out_ptr[0];
        uint64_t in1 = out_ptr[width];
        uint64_t in2 = out_ptr[width * 2];
        uint64_t in3 = out_ptr[width * 3];
        uint64_t in4 = out_ptr[width * 4];
        uint64_t in5 = out_ptr[width * 5];
        uint64_t in6 = out_ptr[width * 6];
        uint64_t in7 = out_ptr[width * 7];

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
        out_ptr[width] = out1 ^ in_ptr[width];
        out_ptr[width * 2] = out2 ^ in_ptr[width * 2];
        out_ptr[width * 3] = out3 ^ in_ptr[width * 3];
        out_ptr[width * 4] = out4 ^ in_ptr[width * 4];
        out_ptr[width * 5] = out5 ^ in_ptr[width * 5];
        out_ptr[width * 6] = out6 ^ in_ptr[width * 6];
        out_ptr[width * 7] = out7 ^ in_ptr[width * 7];

        in_ptr++;
        out_ptr++;
    }
}

static void gf8_muladd_5D(uint8_t * out, uint8_t * in, unsigned int width)
{
    unsigned int i;
    uint64_t * in_ptr = (uint64_t *)in;
    uint64_t * out_ptr = (uint64_t *)out;

    for (i = 0; i < width; i++)
    {
        uint64_t out0, out1, out2, out3, out4, out5, out6, out7;
        uint64_t tmp0, tmp1, tmp2, tmp3, tmp4;

        uint64_t in0 = out_ptr[0];
        uint64_t in1 = out_ptr[width];
        uint64_t in2 = out_ptr[width * 2];
        uint64_t in3 = out_ptr[width * 3];
        uint64_t in4 = out_ptr[width * 4];
        uint64_t in5 = out_ptr[width * 5];
        uint64_t in6 = out_ptr[width * 6];
        uint64_t in7 = out_ptr[width * 7];

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
        out_ptr[width] = out1 ^ in_ptr[width];
        out_ptr[width * 2] = out2 ^ in_ptr[width * 2];
        out_ptr[width * 3] = out3 ^ in_ptr[width * 3];
        out_ptr[width * 4] = out4 ^ in_ptr[width * 4];
        out_ptr[width * 5] = out5 ^ in_ptr[width * 5];
        out_ptr[width * 6] = out6 ^ in_ptr[width * 6];
        out_ptr[width * 7] = out7 ^ in_ptr[width * 7];

        in_ptr++;
        out_ptr++;
    }
}

static void gf8_muladd_5E(uint8_t * out, uint8_t * in, unsigned int width)
{
    unsigned int i;
    uint64_t * in_ptr = (uint64_t *)in;
    uint64_t * out_ptr = (uint64_t *)out;

    for (i = 0; i < width; i++)
    {
        uint64_t out0, out1, out2, out3, out4, out5, out6, out7;
        uint64_t tmp0, tmp1, tmp2, tmp3, tmp4;

        uint64_t in0 = out_ptr[0];
        uint64_t in1 = out_ptr[width];
        uint64_t in2 = out_ptr[width * 2];
        uint64_t in3 = out_ptr[width * 3];
        uint64_t in4 = out_ptr[width * 4];
        uint64_t in5 = out_ptr[width * 5];
        uint64_t in6 = out_ptr[width * 6];
        uint64_t in7 = out_ptr[width * 7];

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
        out_ptr[width] = out1 ^ in_ptr[width];
        out_ptr[width * 2] = out2 ^ in_ptr[width * 2];
        out_ptr[width * 3] = out3 ^ in_ptr[width * 3];
        out_ptr[width * 4] = out4 ^ in_ptr[width * 4];
        out_ptr[width * 5] = out5 ^ in_ptr[width * 5];
        out_ptr[width * 6] = out6 ^ in_ptr[width * 6];
        out_ptr[width * 7] = out7 ^ in_ptr[width * 7];

        in_ptr++;
        out_ptr++;
    }
}

static void gf8_muladd_5F(uint8_t * out, uint8_t * in, unsigned int width)
{
    unsigned int i;
    uint64_t * in_ptr = (uint64_t *)in;
    uint64_t * out_ptr = (uint64_t *)out;

    for (i = 0; i < width; i++)
    {
        uint64_t out0, out1, out2, out3, out4, out5, out6, out7;
        uint64_t tmp0, tmp1, tmp2, tmp3;

        uint64_t in0 = out_ptr[0];
        uint64_t in1 = out_ptr[width];
        uint64_t in2 = out_ptr[width * 2];
        uint64_t in3 = out_ptr[width * 3];
        uint64_t in4 = out_ptr[width * 4];
        uint64_t in5 = out_ptr[width * 5];
        uint64_t in6 = out_ptr[width * 6];
        uint64_t in7 = out_ptr[width * 7];

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
        out_ptr[width] = out1 ^ in_ptr[width];
        out_ptr[width * 2] = out2 ^ in_ptr[width * 2];
        out_ptr[width * 3] = out3 ^ in_ptr[width * 3];
        out_ptr[width * 4] = out4 ^ in_ptr[width * 4];
        out_ptr[width * 5] = out5 ^ in_ptr[width * 5];
        out_ptr[width * 6] = out6 ^ in_ptr[width * 6];
        out_ptr[width * 7] = out7 ^ in_ptr[width * 7];

        in_ptr++;
        out_ptr++;
    }
}

static void gf8_muladd_60(uint8_t * out, uint8_t * in, unsigned int width)
{
    unsigned int i;
    uint64_t * in_ptr = (uint64_t *)in;
    uint64_t * out_ptr = (uint64_t *)out;

    for (i = 0; i < width; i++)
    {
        uint64_t out0, out1, out2, out3, out4, out5, out6, out7;
        uint64_t tmp0, tmp1;

        uint64_t in0 = out_ptr[0];
        uint64_t in1 = out_ptr[width];
        uint64_t in2 = out_ptr[width * 2];
        uint64_t in3 = out_ptr[width * 3];
        uint64_t in4 = out_ptr[width * 4];
        uint64_t in5 = out_ptr[width * 5];
        uint64_t in6 = out_ptr[width * 6];
        uint64_t in7 = out_ptr[width * 7];

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
        out_ptr[width] = out1 ^ in_ptr[width];
        out_ptr[width * 2] = out2 ^ in_ptr[width * 2];
        out_ptr[width * 3] = out3 ^ in_ptr[width * 3];
        out_ptr[width * 4] = out4 ^ in_ptr[width * 4];
        out_ptr[width * 5] = out5 ^ in_ptr[width * 5];
        out_ptr[width * 6] = out6 ^ in_ptr[width * 6];
        out_ptr[width * 7] = out7 ^ in_ptr[width * 7];

        in_ptr++;
        out_ptr++;
    }
}

static void gf8_muladd_61(uint8_t * out, uint8_t * in, unsigned int width)
{
    unsigned int i;
    uint64_t * in_ptr = (uint64_t *)in;
    uint64_t * out_ptr = (uint64_t *)out;

    for (i = 0; i < width; i++)
    {
        uint64_t out0, out1, out2, out3, out4, out5, out6, out7;
        uint64_t tmp0, tmp1;

        uint64_t in0 = out_ptr[0];
        uint64_t in1 = out_ptr[width];
        uint64_t in2 = out_ptr[width * 2];
        uint64_t in3 = out_ptr[width * 3];
        uint64_t in4 = out_ptr[width * 4];
        uint64_t in5 = out_ptr[width * 5];
        uint64_t in6 = out_ptr[width * 6];
        uint64_t in7 = out_ptr[width * 7];

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
        out_ptr[width] = out1 ^ in_ptr[width];
        out_ptr[width * 2] = out2 ^ in_ptr[width * 2];
        out_ptr[width * 3] = out3 ^ in_ptr[width * 3];
        out_ptr[width * 4] = out4 ^ in_ptr[width * 4];
        out_ptr[width * 5] = out5 ^ in_ptr[width * 5];
        out_ptr[width * 6] = out6 ^ in_ptr[width * 6];
        out_ptr[width * 7] = out7 ^ in_ptr[width * 7];

        in_ptr++;
        out_ptr++;
    }
}

static void gf8_muladd_62(uint8_t * out, uint8_t * in, unsigned int width)
{
    unsigned int i;
    uint64_t * in_ptr = (uint64_t *)in;
    uint64_t * out_ptr = (uint64_t *)out;

    for (i = 0; i < width; i++)
    {
        uint64_t out0, out1, out2, out3, out4, out5, out6, out7;
        uint64_t tmp0, tmp1, tmp2, tmp3;

        uint64_t in0 = out_ptr[0];
        uint64_t in1 = out_ptr[width];
        uint64_t in2 = out_ptr[width * 2];
        uint64_t in3 = out_ptr[width * 3];
        uint64_t in4 = out_ptr[width * 4];
        uint64_t in5 = out_ptr[width * 5];
        uint64_t in6 = out_ptr[width * 6];
        uint64_t in7 = out_ptr[width * 7];

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
        out_ptr[width] = out1 ^ in_ptr[width];
        out_ptr[width * 2] = out2 ^ in_ptr[width * 2];
        out_ptr[width * 3] = out3 ^ in_ptr[width * 3];
        out_ptr[width * 4] = out4 ^ in_ptr[width * 4];
        out_ptr[width * 5] = out5 ^ in_ptr[width * 5];
        out_ptr[width * 6] = out6 ^ in_ptr[width * 6];
        out_ptr[width * 7] = out7 ^ in_ptr[width * 7];

        in_ptr++;
        out_ptr++;
    }
}

static void gf8_muladd_63(uint8_t * out, uint8_t * in, unsigned int width)
{
    unsigned int i;
    uint64_t * in_ptr = (uint64_t *)in;
    uint64_t * out_ptr = (uint64_t *)out;

    for (i = 0; i < width; i++)
    {
        uint64_t out0, out1, out2, out3, out4, out5, out6, out7;
        uint64_t tmp0, tmp1, tmp2, tmp3, tmp4;

        uint64_t in0 = out_ptr[0];
        uint64_t in1 = out_ptr[width];
        uint64_t in2 = out_ptr[width * 2];
        uint64_t in3 = out_ptr[width * 3];
        uint64_t in4 = out_ptr[width * 4];
        uint64_t in5 = out_ptr[width * 5];
        uint64_t in6 = out_ptr[width * 6];
        uint64_t in7 = out_ptr[width * 7];

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
        out_ptr[width] = out1 ^ in_ptr[width];
        out_ptr[width * 2] = out2 ^ in_ptr[width * 2];
        out_ptr[width * 3] = out3 ^ in_ptr[width * 3];
        out_ptr[width * 4] = out4 ^ in_ptr[width * 4];
        out_ptr[width * 5] = out5 ^ in_ptr[width * 5];
        out_ptr[width * 6] = out6 ^ in_ptr[width * 6];
        out_ptr[width * 7] = out7 ^ in_ptr[width * 7];

        in_ptr++;
        out_ptr++;
    }
}

static void gf8_muladd_64(uint8_t * out, uint8_t * in, unsigned int width)
{
    unsigned int i;
    uint64_t * in_ptr = (uint64_t *)in;
    uint64_t * out_ptr = (uint64_t *)out;

    for (i = 0; i < width; i++)
    {
        uint64_t out0, out1, out2, out3, out4, out5, out6, out7;
        uint64_t tmp0, tmp1;

        uint64_t in0 = out_ptr[0];
        uint64_t in1 = out_ptr[width];
        uint64_t in2 = out_ptr[width * 2];
        uint64_t in3 = out_ptr[width * 3];
        uint64_t in4 = out_ptr[width * 4];
        uint64_t in5 = out_ptr[width * 5];
        uint64_t in6 = out_ptr[width * 6];
        uint64_t in7 = out_ptr[width * 7];

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
        out_ptr[width] = out1 ^ in_ptr[width];
        out_ptr[width * 2] = out2 ^ in_ptr[width * 2];
        out_ptr[width * 3] = out3 ^ in_ptr[width * 3];
        out_ptr[width * 4] = out4 ^ in_ptr[width * 4];
        out_ptr[width * 5] = out5 ^ in_ptr[width * 5];
        out_ptr[width * 6] = out6 ^ in_ptr[width * 6];
        out_ptr[width * 7] = out7 ^ in_ptr[width * 7];

        in_ptr++;
        out_ptr++;
    }
}

static void gf8_muladd_65(uint8_t * out, uint8_t * in, unsigned int width)
{
    unsigned int i;
    uint64_t * in_ptr = (uint64_t *)in;
    uint64_t * out_ptr = (uint64_t *)out;

    for (i = 0; i < width; i++)
    {
        uint64_t out0, out1, out2, out3, out4, out5, out6, out7;
        uint64_t tmp0, tmp1, tmp2, tmp3;

        uint64_t in0 = out_ptr[0];
        uint64_t in1 = out_ptr[width];
        uint64_t in2 = out_ptr[width * 2];
        uint64_t in3 = out_ptr[width * 3];
        uint64_t in4 = out_ptr[width * 4];
        uint64_t in5 = out_ptr[width * 5];
        uint64_t in6 = out_ptr[width * 6];
        uint64_t in7 = out_ptr[width * 7];

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
        out_ptr[width] = out1 ^ in_ptr[width];
        out_ptr[width * 2] = out2 ^ in_ptr[width * 2];
        out_ptr[width * 3] = out3 ^ in_ptr[width * 3];
        out_ptr[width * 4] = out4 ^ in_ptr[width * 4];
        out_ptr[width * 5] = out5 ^ in_ptr[width * 5];
        out_ptr[width * 6] = out6 ^ in_ptr[width * 6];
        out_ptr[width * 7] = out7 ^ in_ptr[width * 7];

        in_ptr++;
        out_ptr++;
    }
}

static void gf8_muladd_66(uint8_t * out, uint8_t * in, unsigned int width)
{
    unsigned int i;
    uint64_t * in_ptr = (uint64_t *)in;
    uint64_t * out_ptr = (uint64_t *)out;

    for (i = 0; i < width; i++)
    {
        uint64_t out0, out1, out2, out3, out4, out5, out6, out7;
        uint64_t tmp0, tmp1, tmp2, tmp3, tmp4;

        uint64_t in0 = out_ptr[0];
        uint64_t in1 = out_ptr[width];
        uint64_t in2 = out_ptr[width * 2];
        uint64_t in3 = out_ptr[width * 3];
        uint64_t in4 = out_ptr[width * 4];
        uint64_t in5 = out_ptr[width * 5];
        uint64_t in6 = out_ptr[width * 6];
        uint64_t in7 = out_ptr[width * 7];

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
        out_ptr[width] = out1 ^ in_ptr[width];
        out_ptr[width * 2] = out2 ^ in_ptr[width * 2];
        out_ptr[width * 3] = out3 ^ in_ptr[width * 3];
        out_ptr[width * 4] = out4 ^ in_ptr[width * 4];
        out_ptr[width * 5] = out5 ^ in_ptr[width * 5];
        out_ptr[width * 6] = out6 ^ in_ptr[width * 6];
        out_ptr[width * 7] = out7 ^ in_ptr[width * 7];

        in_ptr++;
        out_ptr++;
    }
}

static void gf8_muladd_67(uint8_t * out, uint8_t * in, unsigned int width)
{
    unsigned int i;
    uint64_t * in_ptr = (uint64_t *)in;
    uint64_t * out_ptr = (uint64_t *)out;

    for (i = 0; i < width; i++)
    {
        uint64_t out0, out1, out2, out3, out4, out5, out6, out7;
        uint64_t tmp0, tmp1, tmp2, tmp3;

        uint64_t in0 = out_ptr[0];
        uint64_t in1 = out_ptr[width];
        uint64_t in2 = out_ptr[width * 2];
        uint64_t in3 = out_ptr[width * 3];
        uint64_t in4 = out_ptr[width * 4];
        uint64_t in5 = out_ptr[width * 5];
        uint64_t in6 = out_ptr[width * 6];
        uint64_t in7 = out_ptr[width * 7];

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
        out_ptr[width] = out1 ^ in_ptr[width];
        out_ptr[width * 2] = out2 ^ in_ptr[width * 2];
        out_ptr[width * 3] = out3 ^ in_ptr[width * 3];
        out_ptr[width * 4] = out4 ^ in_ptr[width * 4];
        out_ptr[width * 5] = out5 ^ in_ptr[width * 5];
        out_ptr[width * 6] = out6 ^ in_ptr[width * 6];
        out_ptr[width * 7] = out7 ^ in_ptr[width * 7];

        in_ptr++;
        out_ptr++;
    }
}

static void gf8_muladd_68(uint8_t * out, uint8_t * in, unsigned int width)
{
    unsigned int i;
    uint64_t * in_ptr = (uint64_t *)in;
    uint64_t * out_ptr = (uint64_t *)out;

    for (i = 0; i < width; i++)
    {
        uint64_t out0, out1, out2, out3, out4, out5, out6, out7;
        uint64_t tmp0, tmp1, tmp2, tmp3;

        uint64_t in0 = out_ptr[0];
        uint64_t in1 = out_ptr[width];
        uint64_t in2 = out_ptr[width * 2];
        uint64_t in3 = out_ptr[width * 3];
        uint64_t in4 = out_ptr[width * 4];
        uint64_t in5 = out_ptr[width * 5];
        uint64_t in6 = out_ptr[width * 6];
        uint64_t in7 = out_ptr[width * 7];

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
        out_ptr[width] = out1 ^ in_ptr[width];
        out_ptr[width * 2] = out2 ^ in_ptr[width * 2];
        out_ptr[width * 3] = out3 ^ in_ptr[width * 3];
        out_ptr[width * 4] = out4 ^ in_ptr[width * 4];
        out_ptr[width * 5] = out5 ^ in_ptr[width * 5];
        out_ptr[width * 6] = out6 ^ in_ptr[width * 6];
        out_ptr[width * 7] = out7 ^ in_ptr[width * 7];

        in_ptr++;
        out_ptr++;
    }
}

static void gf8_muladd_69(uint8_t * out, uint8_t * in, unsigned int width)
{
    unsigned int i;
    uint64_t * in_ptr = (uint64_t *)in;
    uint64_t * out_ptr = (uint64_t *)out;

    for (i = 0; i < width; i++)
    {
        uint64_t out0, out1, out2, out3, out4, out5, out6, out7;
        uint64_t tmp0;

        uint64_t in0 = out_ptr[0];
        uint64_t in1 = out_ptr[width];
        uint64_t in2 = out_ptr[width * 2];
        uint64_t in3 = out_ptr[width * 3];
        uint64_t in4 = out_ptr[width * 4];
        uint64_t in5 = out_ptr[width * 5];
        uint64_t in6 = out_ptr[width * 6];
        uint64_t in7 = out_ptr[width * 7];

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
        out_ptr[width] = out1 ^ in_ptr[width];
        out_ptr[width * 2] = out2 ^ in_ptr[width * 2];
        out_ptr[width * 3] = out3 ^ in_ptr[width * 3];
        out_ptr[width * 4] = out4 ^ in_ptr[width * 4];
        out_ptr[width * 5] = out5 ^ in_ptr[width * 5];
        out_ptr[width * 6] = out6 ^ in_ptr[width * 6];
        out_ptr[width * 7] = out7 ^ in_ptr[width * 7];

        in_ptr++;
        out_ptr++;
    }
}

static void gf8_muladd_6A(uint8_t * out, uint8_t * in, unsigned int width)
{
    unsigned int i;
    uint64_t * in_ptr = (uint64_t *)in;
    uint64_t * out_ptr = (uint64_t *)out;

    for (i = 0; i < width; i++)
    {
        uint64_t out0, out1, out2, out3, out4, out5, out6, out7;
        uint64_t tmp0, tmp1, tmp2;

        uint64_t in0 = out_ptr[0];
        uint64_t in1 = out_ptr[width];
        uint64_t in2 = out_ptr[width * 2];
        uint64_t in3 = out_ptr[width * 3];
        uint64_t in4 = out_ptr[width * 4];
        uint64_t in5 = out_ptr[width * 5];
        uint64_t in6 = out_ptr[width * 6];
        uint64_t in7 = out_ptr[width * 7];

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
        out_ptr[width] = out1 ^ in_ptr[width];
        out_ptr[width * 2] = out2 ^ in_ptr[width * 2];
        out_ptr[width * 3] = out3 ^ in_ptr[width * 3];
        out_ptr[width * 4] = out4 ^ in_ptr[width * 4];
        out_ptr[width * 5] = out5 ^ in_ptr[width * 5];
        out_ptr[width * 6] = out6 ^ in_ptr[width * 6];
        out_ptr[width * 7] = out7 ^ in_ptr[width * 7];

        in_ptr++;
        out_ptr++;
    }
}

static void gf8_muladd_6B(uint8_t * out, uint8_t * in, unsigned int width)
{
    unsigned int i;
    uint64_t * in_ptr = (uint64_t *)in;
    uint64_t * out_ptr = (uint64_t *)out;

    for (i = 0; i < width; i++)
    {
        uint64_t out0, out1, out2, out3, out4, out5, out6, out7;
        uint64_t tmp0, tmp1;

        uint64_t in0 = out_ptr[0];
        uint64_t in1 = out_ptr[width];
        uint64_t in2 = out_ptr[width * 2];
        uint64_t in3 = out_ptr[width * 3];
        uint64_t in4 = out_ptr[width * 4];
        uint64_t in5 = out_ptr[width * 5];
        uint64_t in6 = out_ptr[width * 6];
        uint64_t in7 = out_ptr[width * 7];

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
        out_ptr[width] = out1 ^ in_ptr[width];
        out_ptr[width * 2] = out2 ^ in_ptr[width * 2];
        out_ptr[width * 3] = out3 ^ in_ptr[width * 3];
        out_ptr[width * 4] = out4 ^ in_ptr[width * 4];
        out_ptr[width * 5] = out5 ^ in_ptr[width * 5];
        out_ptr[width * 6] = out6 ^ in_ptr[width * 6];
        out_ptr[width * 7] = out7 ^ in_ptr[width * 7];

        in_ptr++;
        out_ptr++;
    }
}

static void gf8_muladd_6C(uint8_t * out, uint8_t * in, unsigned int width)
{
    unsigned int i;
    uint64_t * in_ptr = (uint64_t *)in;
    uint64_t * out_ptr = (uint64_t *)out;

    for (i = 0; i < width; i++)
    {
        uint64_t out0, out1, out2, out3, out4, out5, out6, out7;
        uint64_t tmp0, tmp1;

        uint64_t in0 = out_ptr[0];
        uint64_t in1 = out_ptr[width];
        uint64_t in2 = out_ptr[width * 2];
        uint64_t in3 = out_ptr[width * 3];
        uint64_t in4 = out_ptr[width * 4];
        uint64_t in5 = out_ptr[width * 5];
        uint64_t in6 = out_ptr[width * 6];
        uint64_t in7 = out_ptr[width * 7];

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
        out_ptr[width] = out1 ^ in_ptr[width];
        out_ptr[width * 2] = out2 ^ in_ptr[width * 2];
        out_ptr[width * 3] = out3 ^ in_ptr[width * 3];
        out_ptr[width * 4] = out4 ^ in_ptr[width * 4];
        out_ptr[width * 5] = out5 ^ in_ptr[width * 5];
        out_ptr[width * 6] = out6 ^ in_ptr[width * 6];
        out_ptr[width * 7] = out7 ^ in_ptr[width * 7];

        in_ptr++;
        out_ptr++;
    }
}

static void gf8_muladd_6D(uint8_t * out, uint8_t * in, unsigned int width)
{
    unsigned int i;
    uint64_t * in_ptr = (uint64_t *)in;
    uint64_t * out_ptr = (uint64_t *)out;

    for (i = 0; i < width; i++)
    {
        uint64_t out0, out1, out2, out3, out4, out5, out6, out7;
        uint64_t tmp0, tmp1;

        uint64_t in0 = out_ptr[0];
        uint64_t in1 = out_ptr[width];
        uint64_t in2 = out_ptr[width * 2];
        uint64_t in3 = out_ptr[width * 3];
        uint64_t in4 = out_ptr[width * 4];
        uint64_t in5 = out_ptr[width * 5];
        uint64_t in6 = out_ptr[width * 6];
        uint64_t in7 = out_ptr[width * 7];

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
        out_ptr[width] = out1 ^ in_ptr[width];
        out_ptr[width * 2] = out2 ^ in_ptr[width * 2];
        out_ptr[width * 3] = out3 ^ in_ptr[width * 3];
        out_ptr[width * 4] = out4 ^ in_ptr[width * 4];
        out_ptr[width * 5] = out5 ^ in_ptr[width * 5];
        out_ptr[width * 6] = out6 ^ in_ptr[width * 6];
        out_ptr[width * 7] = out7 ^ in_ptr[width * 7];

        in_ptr++;
        out_ptr++;
    }
}

static void gf8_muladd_6E(uint8_t * out, uint8_t * in, unsigned int width)
{
    unsigned int i;
    uint64_t * in_ptr = (uint64_t *)in;
    uint64_t * out_ptr = (uint64_t *)out;

    for (i = 0; i < width; i++)
    {
        uint64_t out0, out1, out2, out3, out4, out5, out6, out7;
        uint64_t tmp0, tmp1, tmp2;

        uint64_t in0 = out_ptr[0];
        uint64_t in1 = out_ptr[width];
        uint64_t in2 = out_ptr[width * 2];
        uint64_t in3 = out_ptr[width * 3];
        uint64_t in4 = out_ptr[width * 4];
        uint64_t in5 = out_ptr[width * 5];
        uint64_t in6 = out_ptr[width * 6];
        uint64_t in7 = out_ptr[width * 7];

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
        out_ptr[width] = out1 ^ in_ptr[width];
        out_ptr[width * 2] = out2 ^ in_ptr[width * 2];
        out_ptr[width * 3] = out3 ^ in_ptr[width * 3];
        out_ptr[width * 4] = out4 ^ in_ptr[width * 4];
        out_ptr[width * 5] = out5 ^ in_ptr[width * 5];
        out_ptr[width * 6] = out6 ^ in_ptr[width * 6];
        out_ptr[width * 7] = out7 ^ in_ptr[width * 7];

        in_ptr++;
        out_ptr++;
    }
}

static void gf8_muladd_6F(uint8_t * out, uint8_t * in, unsigned int width)
{
    unsigned int i;
    uint64_t * in_ptr = (uint64_t *)in;
    uint64_t * out_ptr = (uint64_t *)out;

    for (i = 0; i < width; i++)
    {
        uint64_t out0, out1, out2, out3, out4, out5, out6, out7;
        uint64_t tmp0, tmp1, tmp2;

        uint64_t in0 = out_ptr[0];
        uint64_t in1 = out_ptr[width];
        uint64_t in2 = out_ptr[width * 2];
        uint64_t in3 = out_ptr[width * 3];
        uint64_t in4 = out_ptr[width * 4];
        uint64_t in5 = out_ptr[width * 5];
        uint64_t in6 = out_ptr[width * 6];
        uint64_t in7 = out_ptr[width * 7];

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
        out_ptr[width] = out1 ^ in_ptr[width];
        out_ptr[width * 2] = out2 ^ in_ptr[width * 2];
        out_ptr[width * 3] = out3 ^ in_ptr[width * 3];
        out_ptr[width * 4] = out4 ^ in_ptr[width * 4];
        out_ptr[width * 5] = out5 ^ in_ptr[width * 5];
        out_ptr[width * 6] = out6 ^ in_ptr[width * 6];
        out_ptr[width * 7] = out7 ^ in_ptr[width * 7];

        in_ptr++;
        out_ptr++;
    }
}

static void gf8_muladd_70(uint8_t * out, uint8_t * in, unsigned int width)
{
    unsigned int i;
    uint64_t * in_ptr = (uint64_t *)in;
    uint64_t * out_ptr = (uint64_t *)out;

    for (i = 0; i < width; i++)
    {
        uint64_t out0, out1, out2, out3, out4, out5, out6, out7;
        uint64_t tmp0, tmp1, tmp2;

        uint64_t in0 = out_ptr[0];
        uint64_t in1 = out_ptr[width];
        uint64_t in2 = out_ptr[width * 2];
        uint64_t in3 = out_ptr[width * 3];
        uint64_t in4 = out_ptr[width * 4];
        uint64_t in5 = out_ptr[width * 5];
        uint64_t in6 = out_ptr[width * 6];
        uint64_t in7 = out_ptr[width * 7];

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
        out_ptr[width] = out1 ^ in_ptr[width];
        out_ptr[width * 2] = out2 ^ in_ptr[width * 2];
        out_ptr[width * 3] = out3 ^ in_ptr[width * 3];
        out_ptr[width * 4] = out4 ^ in_ptr[width * 4];
        out_ptr[width * 5] = out5 ^ in_ptr[width * 5];
        out_ptr[width * 6] = out6 ^ in_ptr[width * 6];
        out_ptr[width * 7] = out7 ^ in_ptr[width * 7];

        in_ptr++;
        out_ptr++;
    }
}

static void gf8_muladd_71(uint8_t * out, uint8_t * in, unsigned int width)
{
    unsigned int i;
    uint64_t * in_ptr = (uint64_t *)in;
    uint64_t * out_ptr = (uint64_t *)out;

    for (i = 0; i < width; i++)
    {
        uint64_t out0, out1, out2, out3, out4, out5, out6, out7;
        uint64_t tmp0, tmp1, tmp2;

        uint64_t in0 = out_ptr[0];
        uint64_t in1 = out_ptr[width];
        uint64_t in2 = out_ptr[width * 2];
        uint64_t in3 = out_ptr[width * 3];
        uint64_t in4 = out_ptr[width * 4];
        uint64_t in5 = out_ptr[width * 5];
        uint64_t in6 = out_ptr[width * 6];
        uint64_t in7 = out_ptr[width * 7];

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
        out_ptr[width] = out1 ^ in_ptr[width];
        out_ptr[width * 2] = out2 ^ in_ptr[width * 2];
        out_ptr[width * 3] = out3 ^ in_ptr[width * 3];
        out_ptr[width * 4] = out4 ^ in_ptr[width * 4];
        out_ptr[width * 5] = out5 ^ in_ptr[width * 5];
        out_ptr[width * 6] = out6 ^ in_ptr[width * 6];
        out_ptr[width * 7] = out7 ^ in_ptr[width * 7];

        in_ptr++;
        out_ptr++;
    }
}

static void gf8_muladd_72(uint8_t * out, uint8_t * in, unsigned int width)
{
    unsigned int i;
    uint64_t * in_ptr = (uint64_t *)in;
    uint64_t * out_ptr = (uint64_t *)out;

    for (i = 0; i < width; i++)
    {
        uint64_t out0, out1, out2, out3, out4, out5, out6, out7;
        uint64_t tmp0, tmp1, tmp2;

        uint64_t in0 = out_ptr[0];
        uint64_t in1 = out_ptr[width];
        uint64_t in2 = out_ptr[width * 2];
        uint64_t in3 = out_ptr[width * 3];
        uint64_t in4 = out_ptr[width * 4];
        uint64_t in5 = out_ptr[width * 5];
        uint64_t in6 = out_ptr[width * 6];
        uint64_t in7 = out_ptr[width * 7];

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
        out_ptr[width] = out1 ^ in_ptr[width];
        out_ptr[width * 2] = out2 ^ in_ptr[width * 2];
        out_ptr[width * 3] = out3 ^ in_ptr[width * 3];
        out_ptr[width * 4] = out4 ^ in_ptr[width * 4];
        out_ptr[width * 5] = out5 ^ in_ptr[width * 5];
        out_ptr[width * 6] = out6 ^ in_ptr[width * 6];
        out_ptr[width * 7] = out7 ^ in_ptr[width * 7];

        in_ptr++;
        out_ptr++;
    }
}

static void gf8_muladd_73(uint8_t * out, uint8_t * in, unsigned int width)
{
    unsigned int i;
    uint64_t * in_ptr = (uint64_t *)in;
    uint64_t * out_ptr = (uint64_t *)out;

    for (i = 0; i < width; i++)
    {
        uint64_t out0, out1, out2, out3, out4, out5, out6, out7;

        uint64_t in0 = out_ptr[0];
        uint64_t in1 = out_ptr[width];
        uint64_t in2 = out_ptr[width * 2];
        uint64_t in3 = out_ptr[width * 3];
        uint64_t in4 = out_ptr[width * 4];
        uint64_t in5 = out_ptr[width * 5];
        uint64_t in6 = out_ptr[width * 6];
        uint64_t in7 = out_ptr[width * 7];

        out3 = in3 ^ in7;
        out2 = out3 ^ in1 ^ in5;
        out1 = out2 ^ in0 ^ in4;
        out5 = out1 ^ in5;
        out6 = out1 ^ out3 ^ in2;
        out0 = out2 ^ out6 ^ in6;
        out7 = out0 ^ out1 ^ in3;
        out4 = out0 ^ in4;

        out_ptr[0] = out0 ^ in_ptr[0];
        out_ptr[width] = out1 ^ in_ptr[width];
        out_ptr[width * 2] = out2 ^ in_ptr[width * 2];
        out_ptr[width * 3] = out3 ^ in_ptr[width * 3];
        out_ptr[width * 4] = out4 ^ in_ptr[width * 4];
        out_ptr[width * 5] = out5 ^ in_ptr[width * 5];
        out_ptr[width * 6] = out6 ^ in_ptr[width * 6];
        out_ptr[width * 7] = out7 ^ in_ptr[width * 7];

        in_ptr++;
        out_ptr++;
    }
}

static void gf8_muladd_74(uint8_t * out, uint8_t * in, unsigned int width)
{
    unsigned int i;
    uint64_t * in_ptr = (uint64_t *)in;
    uint64_t * out_ptr = (uint64_t *)out;

    for (i = 0; i < width; i++)
    {
        uint64_t out0, out1, out2, out3, out4, out5, out6, out7;
        uint64_t tmp0, tmp1;

        uint64_t in0 = out_ptr[0];
        uint64_t in1 = out_ptr[width];
        uint64_t in2 = out_ptr[width * 2];
        uint64_t in3 = out_ptr[width * 3];
        uint64_t in4 = out_ptr[width * 4];
        uint64_t in5 = out_ptr[width * 5];
        uint64_t in6 = out_ptr[width * 6];
        uint64_t in7 = out_ptr[width * 7];

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
        out_ptr[width] = out1 ^ in_ptr[width];
        out_ptr[width * 2] = out2 ^ in_ptr[width * 2];
        out_ptr[width * 3] = out3 ^ in_ptr[width * 3];
        out_ptr[width * 4] = out4 ^ in_ptr[width * 4];
        out_ptr[width * 5] = out5 ^ in_ptr[width * 5];
        out_ptr[width * 6] = out6 ^ in_ptr[width * 6];
        out_ptr[width * 7] = out7 ^ in_ptr[width * 7];

        in_ptr++;
        out_ptr++;
    }
}

static void gf8_muladd_75(uint8_t * out, uint8_t * in, unsigned int width)
{
    unsigned int i;
    uint64_t * in_ptr = (uint64_t *)in;
    uint64_t * out_ptr = (uint64_t *)out;

    for (i = 0; i < width; i++)
    {
        uint64_t out0, out1, out2, out3, out4, out5, out6, out7;
        uint64_t tmp0, tmp1, tmp2;

        uint64_t in0 = out_ptr[0];
        uint64_t in1 = out_ptr[width];
        uint64_t in2 = out_ptr[width * 2];
        uint64_t in3 = out_ptr[width * 3];
        uint64_t in4 = out_ptr[width * 4];
        uint64_t in5 = out_ptr[width * 5];
        uint64_t in6 = out_ptr[width * 6];
        uint64_t in7 = out_ptr[width * 7];

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
        out_ptr[width] = out1 ^ in_ptr[width];
        out_ptr[width * 2] = out2 ^ in_ptr[width * 2];
        out_ptr[width * 3] = out3 ^ in_ptr[width * 3];
        out_ptr[width * 4] = out4 ^ in_ptr[width * 4];
        out_ptr[width * 5] = out5 ^ in_ptr[width * 5];
        out_ptr[width * 6] = out6 ^ in_ptr[width * 6];
        out_ptr[width * 7] = out7 ^ in_ptr[width * 7];

        in_ptr++;
        out_ptr++;
    }
}

static void gf8_muladd_76(uint8_t * out, uint8_t * in, unsigned int width)
{
    unsigned int i;
    uint64_t * in_ptr = (uint64_t *)in;
    uint64_t * out_ptr = (uint64_t *)out;

    for (i = 0; i < width; i++)
    {
        uint64_t out0, out1, out2, out3, out4, out5, out6, out7;
        uint64_t tmp0, tmp1, tmp2, tmp3;

        uint64_t in0 = out_ptr[0];
        uint64_t in1 = out_ptr[width];
        uint64_t in2 = out_ptr[width * 2];
        uint64_t in3 = out_ptr[width * 3];
        uint64_t in4 = out_ptr[width * 4];
        uint64_t in5 = out_ptr[width * 5];
        uint64_t in6 = out_ptr[width * 6];
        uint64_t in7 = out_ptr[width * 7];

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
        out_ptr[width] = out1 ^ in_ptr[width];
        out_ptr[width * 2] = out2 ^ in_ptr[width * 2];
        out_ptr[width * 3] = out3 ^ in_ptr[width * 3];
        out_ptr[width * 4] = out4 ^ in_ptr[width * 4];
        out_ptr[width * 5] = out5 ^ in_ptr[width * 5];
        out_ptr[width * 6] = out6 ^ in_ptr[width * 6];
        out_ptr[width * 7] = out7 ^ in_ptr[width * 7];

        in_ptr++;
        out_ptr++;
    }
}

static void gf8_muladd_77(uint8_t * out, uint8_t * in, unsigned int width)
{
    unsigned int i;
    uint64_t * in_ptr = (uint64_t *)in;
    uint64_t * out_ptr = (uint64_t *)out;

    for (i = 0; i < width; i++)
    {
        uint64_t out0, out1, out2, out3, out4, out5, out6, out7;
        uint64_t tmp0, tmp1, tmp2, tmp3;

        uint64_t in0 = out_ptr[0];
        uint64_t in1 = out_ptr[width];
        uint64_t in2 = out_ptr[width * 2];
        uint64_t in3 = out_ptr[width * 3];
        uint64_t in4 = out_ptr[width * 4];
        uint64_t in5 = out_ptr[width * 5];
        uint64_t in6 = out_ptr[width * 6];
        uint64_t in7 = out_ptr[width * 7];

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
        out_ptr[width] = out1 ^ in_ptr[width];
        out_ptr[width * 2] = out2 ^ in_ptr[width * 2];
        out_ptr[width * 3] = out3 ^ in_ptr[width * 3];
        out_ptr[width * 4] = out4 ^ in_ptr[width * 4];
        out_ptr[width * 5] = out5 ^ in_ptr[width * 5];
        out_ptr[width * 6] = out6 ^ in_ptr[width * 6];
        out_ptr[width * 7] = out7 ^ in_ptr[width * 7];

        in_ptr++;
        out_ptr++;
    }
}

static void gf8_muladd_78(uint8_t * out, uint8_t * in, unsigned int width)
{
    unsigned int i;
    uint64_t * in_ptr = (uint64_t *)in;
    uint64_t * out_ptr = (uint64_t *)out;

    for (i = 0; i < width; i++)
    {
        uint64_t out0, out1, out2, out3, out4, out5, out6, out7;
        uint64_t tmp0, tmp1, tmp2;

        uint64_t in0 = out_ptr[0];
        uint64_t in1 = out_ptr[width];
        uint64_t in2 = out_ptr[width * 2];
        uint64_t in3 = out_ptr[width * 3];
        uint64_t in4 = out_ptr[width * 4];
        uint64_t in5 = out_ptr[width * 5];
        uint64_t in6 = out_ptr[width * 6];
        uint64_t in7 = out_ptr[width * 7];

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
        out_ptr[width] = out1 ^ in_ptr[width];
        out_ptr[width * 2] = out2 ^ in_ptr[width * 2];
        out_ptr[width * 3] = out3 ^ in_ptr[width * 3];
        out_ptr[width * 4] = out4 ^ in_ptr[width * 4];
        out_ptr[width * 5] = out5 ^ in_ptr[width * 5];
        out_ptr[width * 6] = out6 ^ in_ptr[width * 6];
        out_ptr[width * 7] = out7 ^ in_ptr[width * 7];

        in_ptr++;
        out_ptr++;
    }
}

static void gf8_muladd_79(uint8_t * out, uint8_t * in, unsigned int width)
{
    unsigned int i;
    uint64_t * in_ptr = (uint64_t *)in;
    uint64_t * out_ptr = (uint64_t *)out;

    for (i = 0; i < width; i++)
    {
        uint64_t out0, out1, out2, out3, out4, out5, out6, out7;
        uint64_t tmp0, tmp1, tmp2, tmp3;

        uint64_t in0 = out_ptr[0];
        uint64_t in1 = out_ptr[width];
        uint64_t in2 = out_ptr[width * 2];
        uint64_t in3 = out_ptr[width * 3];
        uint64_t in4 = out_ptr[width * 4];
        uint64_t in5 = out_ptr[width * 5];
        uint64_t in6 = out_ptr[width * 6];
        uint64_t in7 = out_ptr[width * 7];

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
        out_ptr[width] = out1 ^ in_ptr[width];
        out_ptr[width * 2] = out2 ^ in_ptr[width * 2];
        out_ptr[width * 3] = out3 ^ in_ptr[width * 3];
        out_ptr[width * 4] = out4 ^ in_ptr[width * 4];
        out_ptr[width * 5] = out5 ^ in_ptr[width * 5];
        out_ptr[width * 6] = out6 ^ in_ptr[width * 6];
        out_ptr[width * 7] = out7 ^ in_ptr[width * 7];

        in_ptr++;
        out_ptr++;
    }
}

static void gf8_muladd_7A(uint8_t * out, uint8_t * in, unsigned int width)
{
    unsigned int i;
    uint64_t * in_ptr = (uint64_t *)in;
    uint64_t * out_ptr = (uint64_t *)out;

    for (i = 0; i < width; i++)
    {
        uint64_t out0, out1, out2, out3, out4, out5, out6, out7;
        uint64_t tmp0, tmp1;

        uint64_t in0 = out_ptr[0];
        uint64_t in1 = out_ptr[width];
        uint64_t in2 = out_ptr[width * 2];
        uint64_t in3 = out_ptr[width * 3];
        uint64_t in4 = out_ptr[width * 4];
        uint64_t in5 = out_ptr[width * 5];
        uint64_t in6 = out_ptr[width * 6];
        uint64_t in7 = out_ptr[width * 7];

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
        out_ptr[width] = out1 ^ in_ptr[width];
        out_ptr[width * 2] = out2 ^ in_ptr[width * 2];
        out_ptr[width * 3] = out3 ^ in_ptr[width * 3];
        out_ptr[width * 4] = out4 ^ in_ptr[width * 4];
        out_ptr[width * 5] = out5 ^ in_ptr[width * 5];
        out_ptr[width * 6] = out6 ^ in_ptr[width * 6];
        out_ptr[width * 7] = out7 ^ in_ptr[width * 7];

        in_ptr++;
        out_ptr++;
    }
}

static void gf8_muladd_7B(uint8_t * out, uint8_t * in, unsigned int width)
{
    unsigned int i;
    uint64_t * in_ptr = (uint64_t *)in;
    uint64_t * out_ptr = (uint64_t *)out;

    for (i = 0; i < width; i++)
    {
        uint64_t out0, out1, out2, out3, out4, out5, out6, out7;
        uint64_t tmp0, tmp1, tmp2;

        uint64_t in0 = out_ptr[0];
        uint64_t in1 = out_ptr[width];
        uint64_t in2 = out_ptr[width * 2];
        uint64_t in3 = out_ptr[width * 3];
        uint64_t in4 = out_ptr[width * 4];
        uint64_t in5 = out_ptr[width * 5];
        uint64_t in6 = out_ptr[width * 6];
        uint64_t in7 = out_ptr[width * 7];

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
        out_ptr[width] = out1 ^ in_ptr[width];
        out_ptr[width * 2] = out2 ^ in_ptr[width * 2];
        out_ptr[width * 3] = out3 ^ in_ptr[width * 3];
        out_ptr[width * 4] = out4 ^ in_ptr[width * 4];
        out_ptr[width * 5] = out5 ^ in_ptr[width * 5];
        out_ptr[width * 6] = out6 ^ in_ptr[width * 6];
        out_ptr[width * 7] = out7 ^ in_ptr[width * 7];

        in_ptr++;
        out_ptr++;
    }
}

static void gf8_muladd_7C(uint8_t * out, uint8_t * in, unsigned int width)
{
    unsigned int i;
    uint64_t * in_ptr = (uint64_t *)in;
    uint64_t * out_ptr = (uint64_t *)out;

    for (i = 0; i < width; i++)
    {
        uint64_t out0, out1, out2, out3, out4, out5, out6, out7;
        uint64_t tmp0, tmp1;

        uint64_t in0 = out_ptr[0];
        uint64_t in1 = out_ptr[width];
        uint64_t in2 = out_ptr[width * 2];
        uint64_t in3 = out_ptr[width * 3];
        uint64_t in4 = out_ptr[width * 4];
        uint64_t in5 = out_ptr[width * 5];
        uint64_t in6 = out_ptr[width * 6];
        uint64_t in7 = out_ptr[width * 7];

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
        out_ptr[width] = out1 ^ in_ptr[width];
        out_ptr[width * 2] = out2 ^ in_ptr[width * 2];
        out_ptr[width * 3] = out3 ^ in_ptr[width * 3];
        out_ptr[width * 4] = out4 ^ in_ptr[width * 4];
        out_ptr[width * 5] = out5 ^ in_ptr[width * 5];
        out_ptr[width * 6] = out6 ^ in_ptr[width * 6];
        out_ptr[width * 7] = out7 ^ in_ptr[width * 7];

        in_ptr++;
        out_ptr++;
    }
}

static void gf8_muladd_7D(uint8_t * out, uint8_t * in, unsigned int width)
{
    unsigned int i;
    uint64_t * in_ptr = (uint64_t *)in;
    uint64_t * out_ptr = (uint64_t *)out;

    for (i = 0; i < width; i++)
    {
        uint64_t out0, out1, out2, out3, out4, out5, out6, out7;
        uint64_t tmp0, tmp1, tmp2, tmp3;

        uint64_t in0 = out_ptr[0];
        uint64_t in1 = out_ptr[width];
        uint64_t in2 = out_ptr[width * 2];
        uint64_t in3 = out_ptr[width * 3];
        uint64_t in4 = out_ptr[width * 4];
        uint64_t in5 = out_ptr[width * 5];
        uint64_t in6 = out_ptr[width * 6];
        uint64_t in7 = out_ptr[width * 7];

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
        out_ptr[width] = out1 ^ in_ptr[width];
        out_ptr[width * 2] = out2 ^ in_ptr[width * 2];
        out_ptr[width * 3] = out3 ^ in_ptr[width * 3];
        out_ptr[width * 4] = out4 ^ in_ptr[width * 4];
        out_ptr[width * 5] = out5 ^ in_ptr[width * 5];
        out_ptr[width * 6] = out6 ^ in_ptr[width * 6];
        out_ptr[width * 7] = out7 ^ in_ptr[width * 7];

        in_ptr++;
        out_ptr++;
    }
}

static void gf8_muladd_7E(uint8_t * out, uint8_t * in, unsigned int width)
{
    unsigned int i;
    uint64_t * in_ptr = (uint64_t *)in;
    uint64_t * out_ptr = (uint64_t *)out;

    for (i = 0; i < width; i++)
    {
        uint64_t out0, out1, out2, out3, out4, out5, out6, out7;
        uint64_t tmp0, tmp1, tmp2;

        uint64_t in0 = out_ptr[0];
        uint64_t in1 = out_ptr[width];
        uint64_t in2 = out_ptr[width * 2];
        uint64_t in3 = out_ptr[width * 3];
        uint64_t in4 = out_ptr[width * 4];
        uint64_t in5 = out_ptr[width * 5];
        uint64_t in6 = out_ptr[width * 6];
        uint64_t in7 = out_ptr[width * 7];

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
        out_ptr[width] = out1 ^ in_ptr[width];
        out_ptr[width * 2] = out2 ^ in_ptr[width * 2];
        out_ptr[width * 3] = out3 ^ in_ptr[width * 3];
        out_ptr[width * 4] = out4 ^ in_ptr[width * 4];
        out_ptr[width * 5] = out5 ^ in_ptr[width * 5];
        out_ptr[width * 6] = out6 ^ in_ptr[width * 6];
        out_ptr[width * 7] = out7 ^ in_ptr[width * 7];

        in_ptr++;
        out_ptr++;
    }
}

static void gf8_muladd_7F(uint8_t * out, uint8_t * in, unsigned int width)
{
    unsigned int i;
    uint64_t * in_ptr = (uint64_t *)in;
    uint64_t * out_ptr = (uint64_t *)out;

    for (i = 0; i < width; i++)
    {
        uint64_t out0, out1, out2, out3, out4, out5, out6, out7;
        uint64_t tmp0, tmp1, tmp2, tmp3;

        uint64_t in0 = out_ptr[0];
        uint64_t in1 = out_ptr[width];
        uint64_t in2 = out_ptr[width * 2];
        uint64_t in3 = out_ptr[width * 3];
        uint64_t in4 = out_ptr[width * 4];
        uint64_t in5 = out_ptr[width * 5];
        uint64_t in6 = out_ptr[width * 6];
        uint64_t in7 = out_ptr[width * 7];

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
        out_ptr[width] = out1 ^ in_ptr[width];
        out_ptr[width * 2] = out2 ^ in_ptr[width * 2];
        out_ptr[width * 3] = out3 ^ in_ptr[width * 3];
        out_ptr[width * 4] = out4 ^ in_ptr[width * 4];
        out_ptr[width * 5] = out5 ^ in_ptr[width * 5];
        out_ptr[width * 6] = out6 ^ in_ptr[width * 6];
        out_ptr[width * 7] = out7 ^ in_ptr[width * 7];

        in_ptr++;
        out_ptr++;
    }
}

static void gf8_muladd_80(uint8_t * out, uint8_t * in, unsigned int width)
{
    unsigned int i;
    uint64_t * in_ptr = (uint64_t *)in;
    uint64_t * out_ptr = (uint64_t *)out;

    for (i = 0; i < width; i++)
    {
        uint64_t out0, out1, out2, out3, out4, out5, out6, out7;
        uint64_t tmp0, tmp1, tmp2;

        uint64_t in0 = out_ptr[0];
        uint64_t in1 = out_ptr[width];
        uint64_t in2 = out_ptr[width * 2];
        uint64_t in3 = out_ptr[width * 3];
        uint64_t in4 = out_ptr[width * 4];
        uint64_t in5 = out_ptr[width * 5];
        uint64_t in6 = out_ptr[width * 6];
        uint64_t in7 = out_ptr[width * 7];

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
        out_ptr[width] = out1 ^ in_ptr[width];
        out_ptr[width * 2] = out2 ^ in_ptr[width * 2];
        out_ptr[width * 3] = out3 ^ in_ptr[width * 3];
        out_ptr[width * 4] = out4 ^ in_ptr[width * 4];
        out_ptr[width * 5] = out5 ^ in_ptr[width * 5];
        out_ptr[width * 6] = out6 ^ in_ptr[width * 6];
        out_ptr[width * 7] = out7 ^ in_ptr[width * 7];

        in_ptr++;
        out_ptr++;
    }
}

static void gf8_muladd_81(uint8_t * out, uint8_t * in, unsigned int width)
{
    unsigned int i;
    uint64_t * in_ptr = (uint64_t *)in;
    uint64_t * out_ptr = (uint64_t *)out;

    for (i = 0; i < width; i++)
    {
        uint64_t out0, out1, out2, out3, out4, out5, out6, out7;
        uint64_t tmp0, tmp1;

        uint64_t in0 = out_ptr[0];
        uint64_t in1 = out_ptr[width];
        uint64_t in2 = out_ptr[width * 2];
        uint64_t in3 = out_ptr[width * 3];
        uint64_t in4 = out_ptr[width * 4];
        uint64_t in5 = out_ptr[width * 5];
        uint64_t in6 = out_ptr[width * 6];
        uint64_t in7 = out_ptr[width * 7];

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
        out_ptr[width] = out1 ^ in_ptr[width];
        out_ptr[width * 2] = out2 ^ in_ptr[width * 2];
        out_ptr[width * 3] = out3 ^ in_ptr[width * 3];
        out_ptr[width * 4] = out4 ^ in_ptr[width * 4];
        out_ptr[width * 5] = out5 ^ in_ptr[width * 5];
        out_ptr[width * 6] = out6 ^ in_ptr[width * 6];
        out_ptr[width * 7] = out7 ^ in_ptr[width * 7];

        in_ptr++;
        out_ptr++;
    }
}

static void gf8_muladd_82(uint8_t * out, uint8_t * in, unsigned int width)
{
    unsigned int i;
    uint64_t * in_ptr = (uint64_t *)in;
    uint64_t * out_ptr = (uint64_t *)out;

    for (i = 0; i < width; i++)
    {
        uint64_t out0, out1, out2, out3, out4, out5, out6, out7;
        uint64_t tmp0;

        uint64_t in0 = out_ptr[0];
        uint64_t in1 = out_ptr[width];
        uint64_t in2 = out_ptr[width * 2];
        uint64_t in3 = out_ptr[width * 3];
        uint64_t in4 = out_ptr[width * 4];
        uint64_t in5 = out_ptr[width * 5];
        uint64_t in6 = out_ptr[width * 6];
        uint64_t in7 = out_ptr[width * 7];

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
        out_ptr[width] = out1 ^ in_ptr[width];
        out_ptr[width * 2] = out2 ^ in_ptr[width * 2];
        out_ptr[width * 3] = out3 ^ in_ptr[width * 3];
        out_ptr[width * 4] = out4 ^ in_ptr[width * 4];
        out_ptr[width * 5] = out5 ^ in_ptr[width * 5];
        out_ptr[width * 6] = out6 ^ in_ptr[width * 6];
        out_ptr[width * 7] = out7 ^ in_ptr[width * 7];

        in_ptr++;
        out_ptr++;
    }
}

static void gf8_muladd_83(uint8_t * out, uint8_t * in, unsigned int width)
{
    unsigned int i;
    uint64_t * in_ptr = (uint64_t *)in;
    uint64_t * out_ptr = (uint64_t *)out;

    for (i = 0; i < width; i++)
    {
        uint64_t out0, out1, out2, out3, out4, out5, out6, out7;
        uint64_t tmp0, tmp1, tmp2, tmp3, tmp4;

        uint64_t in0 = out_ptr[0];
        uint64_t in1 = out_ptr[width];
        uint64_t in2 = out_ptr[width * 2];
        uint64_t in3 = out_ptr[width * 3];
        uint64_t in4 = out_ptr[width * 4];
        uint64_t in5 = out_ptr[width * 5];
        uint64_t in6 = out_ptr[width * 6];
        uint64_t in7 = out_ptr[width * 7];

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
        out_ptr[width] = out1 ^ in_ptr[width];
        out_ptr[width * 2] = out2 ^ in_ptr[width * 2];
        out_ptr[width * 3] = out3 ^ in_ptr[width * 3];
        out_ptr[width * 4] = out4 ^ in_ptr[width * 4];
        out_ptr[width * 5] = out5 ^ in_ptr[width * 5];
        out_ptr[width * 6] = out6 ^ in_ptr[width * 6];
        out_ptr[width * 7] = out7 ^ in_ptr[width * 7];

        in_ptr++;
        out_ptr++;
    }
}

static void gf8_muladd_84(uint8_t * out, uint8_t * in, unsigned int width)
{
    unsigned int i;
    uint64_t * in_ptr = (uint64_t *)in;
    uint64_t * out_ptr = (uint64_t *)out;

    for (i = 0; i < width; i++)
    {
        uint64_t out0, out1, out2, out3, out4, out5, out6, out7;

        uint64_t in0 = out_ptr[0];
        uint64_t in1 = out_ptr[width];
        uint64_t in2 = out_ptr[width * 2];
        uint64_t in3 = out_ptr[width * 3];
        uint64_t in4 = out_ptr[width * 4];
        uint64_t in5 = out_ptr[width * 5];
        uint64_t in6 = out_ptr[width * 6];
        uint64_t in7 = out_ptr[width * 7];

        out1 = in2 ^ in6;
        out6 = in3 ^ in5;
        out0 = in1 ^ in5 ^ in7;
        out7 = in0 ^ in4 ^ in6;
        out4 = in1 ^ in3 ^ in6;
        out5 = in2 ^ in4 ^ in7;
        out2 = out6 ^ in0 ^ in1;
        out3 = out5 ^ in5 ^ in6;

        out_ptr[0] = out0 ^ in_ptr[0];
        out_ptr[width] = out1 ^ in_ptr[width];
        out_ptr[width * 2] = out2 ^ in_ptr[width * 2];
        out_ptr[width * 3] = out3 ^ in_ptr[width * 3];
        out_ptr[width * 4] = out4 ^ in_ptr[width * 4];
        out_ptr[width * 5] = out5 ^ in_ptr[width * 5];
        out_ptr[width * 6] = out6 ^ in_ptr[width * 6];
        out_ptr[width * 7] = out7 ^ in_ptr[width * 7];

        in_ptr++;
        out_ptr++;
    }
}

static void gf8_muladd_85(uint8_t * out, uint8_t * in, unsigned int width)
{
    unsigned int i;
    uint64_t * in_ptr = (uint64_t *)in;
    uint64_t * out_ptr = (uint64_t *)out;

    for (i = 0; i < width; i++)
    {
        uint64_t out0, out1, out2, out3, out4, out5, out6, out7;
        uint64_t tmp0, tmp1, tmp2, tmp3;

        uint64_t in0 = out_ptr[0];
        uint64_t in1 = out_ptr[width];
        uint64_t in2 = out_ptr[width * 2];
        uint64_t in3 = out_ptr[width * 3];
        uint64_t in4 = out_ptr[width * 4];
        uint64_t in5 = out_ptr[width * 5];
        uint64_t in6 = out_ptr[width * 6];
        uint64_t in7 = out_ptr[width * 7];

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
        out_ptr[width] = out1 ^ in_ptr[width];
        out_ptr[width * 2] = out2 ^ in_ptr[width * 2];
        out_ptr[width * 3] = out3 ^ in_ptr[width * 3];
        out_ptr[width * 4] = out4 ^ in_ptr[width * 4];
        out_ptr[width * 5] = out5 ^ in_ptr[width * 5];
        out_ptr[width * 6] = out6 ^ in_ptr[width * 6];
        out_ptr[width * 7] = out7 ^ in_ptr[width * 7];

        in_ptr++;
        out_ptr++;
    }
}

static void gf8_muladd_86(uint8_t * out, uint8_t * in, unsigned int width)
{
    unsigned int i;
    uint64_t * in_ptr = (uint64_t *)in;
    uint64_t * out_ptr = (uint64_t *)out;

    for (i = 0; i < width; i++)
    {
        uint64_t out0, out1, out2, out3, out4, out5, out6, out7;

        uint64_t in0 = out_ptr[0];
        uint64_t in1 = out_ptr[width];
        uint64_t in2 = out_ptr[width * 2];
        uint64_t in3 = out_ptr[width * 3];
        uint64_t in4 = out_ptr[width * 4];
        uint64_t in5 = out_ptr[width * 5];
        uint64_t in6 = out_ptr[width * 6];
        uint64_t in7 = out_ptr[width * 7];

        out6 = in3;
        out7 = in0 ^ in4;
        out0 = in1 ^ in5;
        out5 = in2 ^ in7;
        out3 = in4 ^ in5 ^ in6;
        out1 = in0 ^ in2 ^ in6;
        out4 = in1 ^ in6 ^ in7;
        out2 = in0 ^ in3 ^ in5 ^ in7;

        out_ptr[0] = out0 ^ in_ptr[0];
        out_ptr[width] = out1 ^ in_ptr[width];
        out_ptr[width * 2] = out2 ^ in_ptr[width * 2];
        out_ptr[width * 3] = out3 ^ in_ptr[width * 3];
        out_ptr[width * 4] = out4 ^ in_ptr[width * 4];
        out_ptr[width * 5] = out5 ^ in_ptr[width * 5];
        out_ptr[width * 6] = out6 ^ in_ptr[width * 6];
        out_ptr[width * 7] = out7 ^ in_ptr[width * 7];

        in_ptr++;
        out_ptr++;
    }
}

static void gf8_muladd_87(uint8_t * out, uint8_t * in, unsigned int width)
{
    unsigned int i;
    uint64_t * in_ptr = (uint64_t *)in;
    uint64_t * out_ptr = (uint64_t *)out;

    for (i = 0; i < width; i++)
    {
        uint64_t out0, out1, out2, out3, out4, out5, out6, out7;
        uint64_t tmp0, tmp1;

        uint64_t in0 = out_ptr[0];
        uint64_t in1 = out_ptr[width];
        uint64_t in2 = out_ptr[width * 2];
        uint64_t in3 = out_ptr[width * 3];
        uint64_t in4 = out_ptr[width * 4];
        uint64_t in5 = out_ptr[width * 5];
        uint64_t in6 = out_ptr[width * 6];
        uint64_t in7 = out_ptr[width * 7];

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
        out_ptr[width] = out1 ^ in_ptr[width];
        out_ptr[width * 2] = out2 ^ in_ptr[width * 2];
        out_ptr[width * 3] = out3 ^ in_ptr[width * 3];
        out_ptr[width * 4] = out4 ^ in_ptr[width * 4];
        out_ptr[width * 5] = out5 ^ in_ptr[width * 5];
        out_ptr[width * 6] = out6 ^ in_ptr[width * 6];
        out_ptr[width * 7] = out7 ^ in_ptr[width * 7];

        in_ptr++;
        out_ptr++;
    }
}

static void gf8_muladd_88(uint8_t * out, uint8_t * in, unsigned int width)
{
    unsigned int i;
    uint64_t * in_ptr = (uint64_t *)in;
    uint64_t * out_ptr = (uint64_t *)out;

    for (i = 0; i < width; i++)
    {
        uint64_t out0, out1, out2, out3, out4, out5, out6, out7;
        uint64_t tmp0, tmp1;

        uint64_t in0 = out_ptr[0];
        uint64_t in1 = out_ptr[width];
        uint64_t in2 = out_ptr[width * 2];
        uint64_t in3 = out_ptr[width * 3];
        uint64_t in4 = out_ptr[width * 4];
        uint64_t in5 = out_ptr[width * 5];
        uint64_t in6 = out_ptr[width * 6];
        uint64_t in7 = out_ptr[width * 7];

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
        out_ptr[width] = out1 ^ in_ptr[width];
        out_ptr[width * 2] = out2 ^ in_ptr[width * 2];
        out_ptr[width * 3] = out3 ^ in_ptr[width * 3];
        out_ptr[width * 4] = out4 ^ in_ptr[width * 4];
        out_ptr[width * 5] = out5 ^ in_ptr[width * 5];
        out_ptr[width * 6] = out6 ^ in_ptr[width * 6];
        out_ptr[width * 7] = out7 ^ in_ptr[width * 7];

        in_ptr++;
        out_ptr++;
    }
}

static void gf8_muladd_89(uint8_t * out, uint8_t * in, unsigned int width)
{
    unsigned int i;
    uint64_t * in_ptr = (uint64_t *)in;
    uint64_t * out_ptr = (uint64_t *)out;

    for (i = 0; i < width; i++)
    {
        uint64_t out0, out1, out2, out3, out4, out5, out6, out7;
        uint64_t tmp0, tmp1, tmp2;

        uint64_t in0 = out_ptr[0];
        uint64_t in1 = out_ptr[width];
        uint64_t in2 = out_ptr[width * 2];
        uint64_t in3 = out_ptr[width * 3];
        uint64_t in4 = out_ptr[width * 4];
        uint64_t in5 = out_ptr[width * 5];
        uint64_t in6 = out_ptr[width * 6];
        uint64_t in7 = out_ptr[width * 7];

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
        out_ptr[width] = out1 ^ in_ptr[width];
        out_ptr[width * 2] = out2 ^ in_ptr[width * 2];
        out_ptr[width * 3] = out3 ^ in_ptr[width * 3];
        out_ptr[width * 4] = out4 ^ in_ptr[width * 4];
        out_ptr[width * 5] = out5 ^ in_ptr[width * 5];
        out_ptr[width * 6] = out6 ^ in_ptr[width * 6];
        out_ptr[width * 7] = out7 ^ in_ptr[width * 7];

        in_ptr++;
        out_ptr++;
    }
}

static void gf8_muladd_8A(uint8_t * out, uint8_t * in, unsigned int width)
{
    unsigned int i;
    uint64_t * in_ptr = (uint64_t *)in;
    uint64_t * out_ptr = (uint64_t *)out;

    for (i = 0; i < width; i++)
    {
        uint64_t out0, out1, out2, out3, out4, out5, out6, out7;

        uint64_t in0 = out_ptr[0];
        uint64_t in1 = out_ptr[width];
        uint64_t in2 = out_ptr[width * 2];
        uint64_t in3 = out_ptr[width * 3];
        uint64_t in4 = out_ptr[width * 4];
        uint64_t in5 = out_ptr[width * 5];
        uint64_t in6 = out_ptr[width * 6];
        uint64_t in7 = out_ptr[width * 7];

        out0 = in1 ^ in6;
        out7 = in0 ^ in5;
        out2 = in3 ^ in6;
        out6 = in4 ^ in7;
        out1 = in0 ^ in2 ^ in7;
        out3 = out0 ^ out6 ^ in0;
        out4 = out1 ^ out7 ^ in6;
        out5 = out2 ^ in7;

        out_ptr[0] = out0 ^ in_ptr[0];
        out_ptr[width] = out1 ^ in_ptr[width];
        out_ptr[width * 2] = out2 ^ in_ptr[width * 2];
        out_ptr[width * 3] = out3 ^ in_ptr[width * 3];
        out_ptr[width * 4] = out4 ^ in_ptr[width * 4];
        out_ptr[width * 5] = out5 ^ in_ptr[width * 5];
        out_ptr[width * 6] = out6 ^ in_ptr[width * 6];
        out_ptr[width * 7] = out7 ^ in_ptr[width * 7];

        in_ptr++;
        out_ptr++;
    }
}

static void gf8_muladd_8B(uint8_t * out, uint8_t * in, unsigned int width)
{
    unsigned int i;
    uint64_t * in_ptr = (uint64_t *)in;
    uint64_t * out_ptr = (uint64_t *)out;

    for (i = 0; i < width; i++)
    {
        uint64_t out0, out1, out2, out3, out4, out5, out6, out7;
        uint64_t tmp0, tmp1, tmp2, tmp3, tmp4;

        uint64_t in0 = out_ptr[0];
        uint64_t in1 = out_ptr[width];
        uint64_t in2 = out_ptr[width * 2];
        uint64_t in3 = out_ptr[width * 3];
        uint64_t in4 = out_ptr[width * 4];
        uint64_t in5 = out_ptr[width * 5];
        uint64_t in6 = out_ptr[width * 6];
        uint64_t in7 = out_ptr[width * 7];

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
        out_ptr[width] = out1 ^ in_ptr[width];
        out_ptr[width * 2] = out2 ^ in_ptr[width * 2];
        out_ptr[width * 3] = out3 ^ in_ptr[width * 3];
        out_ptr[width * 4] = out4 ^ in_ptr[width * 4];
        out_ptr[width * 5] = out5 ^ in_ptr[width * 5];
        out_ptr[width * 6] = out6 ^ in_ptr[width * 6];
        out_ptr[width * 7] = out7 ^ in_ptr[width * 7];

        in_ptr++;
        out_ptr++;
    }
}

static void gf8_muladd_8C(uint8_t * out, uint8_t * in, unsigned int width)
{
    unsigned int i;
    uint64_t * in_ptr = (uint64_t *)in;
    uint64_t * out_ptr = (uint64_t *)out;

    for (i = 0; i < width; i++)
    {
        uint64_t out0, out1, out2, out3, out4, out5, out6, out7;

        uint64_t in0 = out_ptr[0];
        uint64_t in1 = out_ptr[width];
        uint64_t in2 = out_ptr[width * 2];
        uint64_t in3 = out_ptr[width * 3];
        uint64_t in4 = out_ptr[width * 4];
        uint64_t in5 = out_ptr[width * 5];
        uint64_t in6 = out_ptr[width * 6];
        uint64_t in7 = out_ptr[width * 7];

        out1 = in2;
        out0 = in1 ^ in7;
        out7 = in0 ^ in6;
        out5 = in4 ^ in6;
        out6 = in5 ^ in7;
        out2 = out0 ^ in0 ^ in3;
        out3 = out5 ^ out7 ^ in2 ^ in7;
        out4 = out6 ^ in3;

        out_ptr[0] = out0 ^ in_ptr[0];
        out_ptr[width] = out1 ^ in_ptr[width];
        out_ptr[width * 2] = out2 ^ in_ptr[width * 2];
        out_ptr[width * 3] = out3 ^ in_ptr[width * 3];
        out_ptr[width * 4] = out4 ^ in_ptr[width * 4];
        out_ptr[width * 5] = out5 ^ in_ptr[width * 5];
        out_ptr[width * 6] = out6 ^ in_ptr[width * 6];
        out_ptr[width * 7] = out7 ^ in_ptr[width * 7];

        in_ptr++;
        out_ptr++;
    }
}

static void gf8_muladd_8D(uint8_t * out, uint8_t * in, unsigned int width)
{
    unsigned int i;
    uint64_t * in_ptr = (uint64_t *)in;
    uint64_t * out_ptr = (uint64_t *)out;

    for (i = 0; i < width; i++)
    {
        uint64_t out0, out1, out2, out3, out4, out5, out6, out7;
        uint64_t tmp0;

        uint64_t in0 = out_ptr[0];
        uint64_t in1 = out_ptr[width];
        uint64_t in2 = out_ptr[width * 2];
        uint64_t in3 = out_ptr[width * 3];
        uint64_t in4 = out_ptr[width * 4];
        uint64_t in5 = out_ptr[width * 5];
        uint64_t in6 = out_ptr[width * 6];
        uint64_t in7 = out_ptr[width * 7];

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
        out_ptr[width] = out1 ^ in_ptr[width];
        out_ptr[width * 2] = out2 ^ in_ptr[width * 2];
        out_ptr[width * 3] = out3 ^ in_ptr[width * 3];
        out_ptr[width * 4] = out4 ^ in_ptr[width * 4];
        out_ptr[width * 5] = out5 ^ in_ptr[width * 5];
        out_ptr[width * 6] = out6 ^ in_ptr[width * 6];
        out_ptr[width * 7] = out7 ^ in_ptr[width * 7];

        in_ptr++;
        out_ptr++;
    }
}

static void gf8_muladd_8E(uint8_t * out, uint8_t * in, unsigned int width)
{
    unsigned int i;
    uint64_t * in_ptr = (uint64_t *)in;
    uint64_t * out_ptr = (uint64_t *)out;

    for (i = 0; i < width; i++)
    {
        uint64_t out0, out1, out2, out3, out4, out5, out6, out7;

        uint64_t in0 = out_ptr[0];
        uint64_t in1 = out_ptr[width];
        uint64_t in2 = out_ptr[width * 2];
        uint64_t in3 = out_ptr[width * 3];
        uint64_t in4 = out_ptr[width * 4];
        uint64_t in5 = out_ptr[width * 5];
        uint64_t in6 = out_ptr[width * 6];
        uint64_t in7 = out_ptr[width * 7];

        out0 = in1;
        out4 = in5;
        out7 = in0;
        out5 = in6;
        out6 = in7;
        out3 = in0 ^ in4;
        out1 = in0 ^ in2;
        out2 = in0 ^ in3;

        out_ptr[0] = out0 ^ in_ptr[0];
        out_ptr[width] = out1 ^ in_ptr[width];
        out_ptr[width * 2] = out2 ^ in_ptr[width * 2];
        out_ptr[width * 3] = out3 ^ in_ptr[width * 3];
        out_ptr[width * 4] = out4 ^ in_ptr[width * 4];
        out_ptr[width * 5] = out5 ^ in_ptr[width * 5];
        out_ptr[width * 6] = out6 ^ in_ptr[width * 6];
        out_ptr[width * 7] = out7 ^ in_ptr[width * 7];

        in_ptr++;
        out_ptr++;
    }
}

static void gf8_muladd_8F(uint8_t * out, uint8_t * in, unsigned int width)
{
    unsigned int i;
    uint64_t * in_ptr = (uint64_t *)in;
    uint64_t * out_ptr = (uint64_t *)out;

    for (i = 0; i < width; i++)
    {
        uint64_t out0, out1, out2, out3, out4, out5, out6, out7;
        uint64_t tmp0;

        uint64_t in0 = out_ptr[0];
        uint64_t in1 = out_ptr[width];
        uint64_t in2 = out_ptr[width * 2];
        uint64_t in3 = out_ptr[width * 3];
        uint64_t in4 = out_ptr[width * 4];
        uint64_t in5 = out_ptr[width * 5];
        uint64_t in6 = out_ptr[width * 6];
        uint64_t in7 = out_ptr[width * 7];

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
        out_ptr[width] = out1 ^ in_ptr[width];
        out_ptr[width * 2] = out2 ^ in_ptr[width * 2];
        out_ptr[width * 3] = out3 ^ in_ptr[width * 3];
        out_ptr[width * 4] = out4 ^ in_ptr[width * 4];
        out_ptr[width * 5] = out5 ^ in_ptr[width * 5];
        out_ptr[width * 6] = out6 ^ in_ptr[width * 6];
        out_ptr[width * 7] = out7 ^ in_ptr[width * 7];

        in_ptr++;
        out_ptr++;
    }
}

static void gf8_muladd_90(uint8_t * out, uint8_t * in, unsigned int width)
{
    unsigned int i;
    uint64_t * in_ptr = (uint64_t *)in;
    uint64_t * out_ptr = (uint64_t *)out;

    for (i = 0; i < width; i++)
    {
        uint64_t out0, out1, out2, out3, out4, out5, out6, out7;
        uint64_t tmp0, tmp1, tmp2;

        uint64_t in0 = out_ptr[0];
        uint64_t in1 = out_ptr[width];
        uint64_t in2 = out_ptr[width * 2];
        uint64_t in3 = out_ptr[width * 3];
        uint64_t in4 = out_ptr[width * 4];
        uint64_t in5 = out_ptr[width * 5];
        uint64_t in6 = out_ptr[width * 6];
        uint64_t in7 = out_ptr[width * 7];

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
        out_ptr[width] = out1 ^ in_ptr[width];
        out_ptr[width * 2] = out2 ^ in_ptr[width * 2];
        out_ptr[width * 3] = out3 ^ in_ptr[width * 3];
        out_ptr[width * 4] = out4 ^ in_ptr[width * 4];
        out_ptr[width * 5] = out5 ^ in_ptr[width * 5];
        out_ptr[width * 6] = out6 ^ in_ptr[width * 6];
        out_ptr[width * 7] = out7 ^ in_ptr[width * 7];

        in_ptr++;
        out_ptr++;
    }
}

static void gf8_muladd_91(uint8_t * out, uint8_t * in, unsigned int width)
{
    unsigned int i;
    uint64_t * in_ptr = (uint64_t *)in;
    uint64_t * out_ptr = (uint64_t *)out;

    for (i = 0; i < width; i++)
    {
        uint64_t out0, out1, out2, out3, out4, out5, out6, out7;
        uint64_t tmp0, tmp1, tmp2, tmp3;

        uint64_t in0 = out_ptr[0];
        uint64_t in1 = out_ptr[width];
        uint64_t in2 = out_ptr[width * 2];
        uint64_t in3 = out_ptr[width * 3];
        uint64_t in4 = out_ptr[width * 4];
        uint64_t in5 = out_ptr[width * 5];
        uint64_t in6 = out_ptr[width * 6];
        uint64_t in7 = out_ptr[width * 7];

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
        out_ptr[width] = out1 ^ in_ptr[width];
        out_ptr[width * 2] = out2 ^ in_ptr[width * 2];
        out_ptr[width * 3] = out3 ^ in_ptr[width * 3];
        out_ptr[width * 4] = out4 ^ in_ptr[width * 4];
        out_ptr[width * 5] = out5 ^ in_ptr[width * 5];
        out_ptr[width * 6] = out6 ^ in_ptr[width * 6];
        out_ptr[width * 7] = out7 ^ in_ptr[width * 7];

        in_ptr++;
        out_ptr++;
    }
}

static void gf8_muladd_92(uint8_t * out, uint8_t * in, unsigned int width)
{
    unsigned int i;
    uint64_t * in_ptr = (uint64_t *)in;
    uint64_t * out_ptr = (uint64_t *)out;

    for (i = 0; i < width; i++)
    {
        uint64_t out0, out1, out2, out3, out4, out5, out6, out7;
        uint64_t tmp0, tmp1;

        uint64_t in0 = out_ptr[0];
        uint64_t in1 = out_ptr[width];
        uint64_t in2 = out_ptr[width * 2];
        uint64_t in3 = out_ptr[width * 3];
        uint64_t in4 = out_ptr[width * 4];
        uint64_t in5 = out_ptr[width * 5];
        uint64_t in6 = out_ptr[width * 6];
        uint64_t in7 = out_ptr[width * 7];

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
        out_ptr[width] = out1 ^ in_ptr[width];
        out_ptr[width * 2] = out2 ^ in_ptr[width * 2];
        out_ptr[width * 3] = out3 ^ in_ptr[width * 3];
        out_ptr[width * 4] = out4 ^ in_ptr[width * 4];
        out_ptr[width * 5] = out5 ^ in_ptr[width * 5];
        out_ptr[width * 6] = out6 ^ in_ptr[width * 6];
        out_ptr[width * 7] = out7 ^ in_ptr[width * 7];

        in_ptr++;
        out_ptr++;
    }
}

static void gf8_muladd_93(uint8_t * out, uint8_t * in, unsigned int width)
{
    unsigned int i;
    uint64_t * in_ptr = (uint64_t *)in;
    uint64_t * out_ptr = (uint64_t *)out;

    for (i = 0; i < width; i++)
    {
        uint64_t out0, out1, out2, out3, out4, out5, out6, out7;
        uint64_t tmp0, tmp1, tmp2;

        uint64_t in0 = out_ptr[0];
        uint64_t in1 = out_ptr[width];
        uint64_t in2 = out_ptr[width * 2];
        uint64_t in3 = out_ptr[width * 3];
        uint64_t in4 = out_ptr[width * 4];
        uint64_t in5 = out_ptr[width * 5];
        uint64_t in6 = out_ptr[width * 6];
        uint64_t in7 = out_ptr[width * 7];

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
        out_ptr[width] = out1 ^ in_ptr[width];
        out_ptr[width * 2] = out2 ^ in_ptr[width * 2];
        out_ptr[width * 3] = out3 ^ in_ptr[width * 3];
        out_ptr[width * 4] = out4 ^ in_ptr[width * 4];
        out_ptr[width * 5] = out5 ^ in_ptr[width * 5];
        out_ptr[width * 6] = out6 ^ in_ptr[width * 6];
        out_ptr[width * 7] = out7 ^ in_ptr[width * 7];

        in_ptr++;
        out_ptr++;
    }
}

static void gf8_muladd_94(uint8_t * out, uint8_t * in, unsigned int width)
{
    unsigned int i;
    uint64_t * in_ptr = (uint64_t *)in;
    uint64_t * out_ptr = (uint64_t *)out;

    for (i = 0; i < width; i++)
    {
        uint64_t out0, out1, out2, out3, out4, out5, out6, out7;
        uint64_t tmp0;

        uint64_t in0 = out_ptr[0];
        uint64_t in1 = out_ptr[width];
        uint64_t in2 = out_ptr[width * 2];
        uint64_t in3 = out_ptr[width * 3];
        uint64_t in4 = out_ptr[width * 4];
        uint64_t in5 = out_ptr[width * 5];
        uint64_t in6 = out_ptr[width * 6];
        uint64_t in7 = out_ptr[width * 7];

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
        out_ptr[width] = out1 ^ in_ptr[width];
        out_ptr[width * 2] = out2 ^ in_ptr[width * 2];
        out_ptr[width * 3] = out3 ^ in_ptr[width * 3];
        out_ptr[width * 4] = out4 ^ in_ptr[width * 4];
        out_ptr[width * 5] = out5 ^ in_ptr[width * 5];
        out_ptr[width * 6] = out6 ^ in_ptr[width * 6];
        out_ptr[width * 7] = out7 ^ in_ptr[width * 7];

        in_ptr++;
        out_ptr++;
    }
}

static void gf8_muladd_95(uint8_t * out, uint8_t * in, unsigned int width)
{
    unsigned int i;
    uint64_t * in_ptr = (uint64_t *)in;
    uint64_t * out_ptr = (uint64_t *)out;

    for (i = 0; i < width; i++)
    {
        uint64_t out0, out1, out2, out3, out4, out5, out6, out7;
        uint64_t tmp0, tmp1, tmp2, tmp3, tmp4, tmp5;

        uint64_t in0 = out_ptr[0];
        uint64_t in1 = out_ptr[width];
        uint64_t in2 = out_ptr[width * 2];
        uint64_t in3 = out_ptr[width * 3];
        uint64_t in4 = out_ptr[width * 4];
        uint64_t in5 = out_ptr[width * 5];
        uint64_t in6 = out_ptr[width * 6];
        uint64_t in7 = out_ptr[width * 7];

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
        out_ptr[width] = out1 ^ in_ptr[width];
        out_ptr[width * 2] = out2 ^ in_ptr[width * 2];
        out_ptr[width * 3] = out3 ^ in_ptr[width * 3];
        out_ptr[width * 4] = out4 ^ in_ptr[width * 4];
        out_ptr[width * 5] = out5 ^ in_ptr[width * 5];
        out_ptr[width * 6] = out6 ^ in_ptr[width * 6];
        out_ptr[width * 7] = out7 ^ in_ptr[width * 7];

        in_ptr++;
        out_ptr++;
    }
}

static void gf8_muladd_96(uint8_t * out, uint8_t * in, unsigned int width)
{
    unsigned int i;
    uint64_t * in_ptr = (uint64_t *)in;
    uint64_t * out_ptr = (uint64_t *)out;

    for (i = 0; i < width; i++)
    {
        uint64_t out0, out1, out2, out3, out4, out5, out6, out7;
        uint64_t tmp0, tmp1, tmp2;

        uint64_t in0 = out_ptr[0];
        uint64_t in1 = out_ptr[width];
        uint64_t in2 = out_ptr[width * 2];
        uint64_t in3 = out_ptr[width * 3];
        uint64_t in4 = out_ptr[width * 4];
        uint64_t in5 = out_ptr[width * 5];
        uint64_t in6 = out_ptr[width * 6];
        uint64_t in7 = out_ptr[width * 7];

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
        out_ptr[width] = out1 ^ in_ptr[width];
        out_ptr[width * 2] = out2 ^ in_ptr[width * 2];
        out_ptr[width * 3] = out3 ^ in_ptr[width * 3];
        out_ptr[width * 4] = out4 ^ in_ptr[width * 4];
        out_ptr[width * 5] = out5 ^ in_ptr[width * 5];
        out_ptr[width * 6] = out6 ^ in_ptr[width * 6];
        out_ptr[width * 7] = out7 ^ in_ptr[width * 7];

        in_ptr++;
        out_ptr++;
    }
}

static void gf8_muladd_97(uint8_t * out, uint8_t * in, unsigned int width)
{
    unsigned int i;
    uint64_t * in_ptr = (uint64_t *)in;
    uint64_t * out_ptr = (uint64_t *)out;

    for (i = 0; i < width; i++)
    {
        uint64_t out0, out1, out2, out3, out4, out5, out6, out7;
        uint64_t tmp0, tmp1, tmp2, tmp3;

        uint64_t in0 = out_ptr[0];
        uint64_t in1 = out_ptr[width];
        uint64_t in2 = out_ptr[width * 2];
        uint64_t in3 = out_ptr[width * 3];
        uint64_t in4 = out_ptr[width * 4];
        uint64_t in5 = out_ptr[width * 5];
        uint64_t in6 = out_ptr[width * 6];
        uint64_t in7 = out_ptr[width * 7];

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
        out_ptr[width] = out1 ^ in_ptr[width];
        out_ptr[width * 2] = out2 ^ in_ptr[width * 2];
        out_ptr[width * 3] = out3 ^ in_ptr[width * 3];
        out_ptr[width * 4] = out4 ^ in_ptr[width * 4];
        out_ptr[width * 5] = out5 ^ in_ptr[width * 5];
        out_ptr[width * 6] = out6 ^ in_ptr[width * 6];
        out_ptr[width * 7] = out7 ^ in_ptr[width * 7];

        in_ptr++;
        out_ptr++;
    }
}

static void gf8_muladd_98(uint8_t * out, uint8_t * in, unsigned int width)
{
    unsigned int i;
    uint64_t * in_ptr = (uint64_t *)in;
    uint64_t * out_ptr = (uint64_t *)out;

    for (i = 0; i < width; i++)
    {
        uint64_t out0, out1, out2, out3, out4, out5, out6, out7;
        uint64_t tmp0, tmp1;

        uint64_t in0 = out_ptr[0];
        uint64_t in1 = out_ptr[width];
        uint64_t in2 = out_ptr[width * 2];
        uint64_t in3 = out_ptr[width * 3];
        uint64_t in4 = out_ptr[width * 4];
        uint64_t in5 = out_ptr[width * 5];
        uint64_t in6 = out_ptr[width * 6];
        uint64_t in7 = out_ptr[width * 7];

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
        out_ptr[width] = out1 ^ in_ptr[width];
        out_ptr[width * 2] = out2 ^ in_ptr[width * 2];
        out_ptr[width * 3] = out3 ^ in_ptr[width * 3];
        out_ptr[width * 4] = out4 ^ in_ptr[width * 4];
        out_ptr[width * 5] = out5 ^ in_ptr[width * 5];
        out_ptr[width * 6] = out6 ^ in_ptr[width * 6];
        out_ptr[width * 7] = out7 ^ in_ptr[width * 7];

        in_ptr++;
        out_ptr++;
    }
}

static void gf8_muladd_99(uint8_t * out, uint8_t * in, unsigned int width)
{
    unsigned int i;
    uint64_t * in_ptr = (uint64_t *)in;
    uint64_t * out_ptr = (uint64_t *)out;

    for (i = 0; i < width; i++)
    {
        uint64_t out0, out1, out2, out3, out4, out5, out6, out7;
        uint64_t tmp0, tmp1, tmp2;

        uint64_t in0 = out_ptr[0];
        uint64_t in1 = out_ptr[width];
        uint64_t in2 = out_ptr[width * 2];
        uint64_t in3 = out_ptr[width * 3];
        uint64_t in4 = out_ptr[width * 4];
        uint64_t in5 = out_ptr[width * 5];
        uint64_t in6 = out_ptr[width * 6];
        uint64_t in7 = out_ptr[width * 7];

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
        out_ptr[width] = out1 ^ in_ptr[width];
        out_ptr[width * 2] = out2 ^ in_ptr[width * 2];
        out_ptr[width * 3] = out3 ^ in_ptr[width * 3];
        out_ptr[width * 4] = out4 ^ in_ptr[width * 4];
        out_ptr[width * 5] = out5 ^ in_ptr[width * 5];
        out_ptr[width * 6] = out6 ^ in_ptr[width * 6];
        out_ptr[width * 7] = out7 ^ in_ptr[width * 7];

        in_ptr++;
        out_ptr++;
    }
}

static void gf8_muladd_9A(uint8_t * out, uint8_t * in, unsigned int width)
{
    unsigned int i;
    uint64_t * in_ptr = (uint64_t *)in;
    uint64_t * out_ptr = (uint64_t *)out;

    for (i = 0; i < width; i++)
    {
        uint64_t out0, out1, out2, out3, out4, out5, out6, out7;
        uint64_t tmp0, tmp1, tmp2;

        uint64_t in0 = out_ptr[0];
        uint64_t in1 = out_ptr[width];
        uint64_t in2 = out_ptr[width * 2];
        uint64_t in3 = out_ptr[width * 3];
        uint64_t in4 = out_ptr[width * 4];
        uint64_t in5 = out_ptr[width * 5];
        uint64_t in6 = out_ptr[width * 6];
        uint64_t in7 = out_ptr[width * 7];

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
        out_ptr[width] = out1 ^ in_ptr[width];
        out_ptr[width * 2] = out2 ^ in_ptr[width * 2];
        out_ptr[width * 3] = out3 ^ in_ptr[width * 3];
        out_ptr[width * 4] = out4 ^ in_ptr[width * 4];
        out_ptr[width * 5] = out5 ^ in_ptr[width * 5];
        out_ptr[width * 6] = out6 ^ in_ptr[width * 6];
        out_ptr[width * 7] = out7 ^ in_ptr[width * 7];

        in_ptr++;
        out_ptr++;
    }
}

static void gf8_muladd_9B(uint8_t * out, uint8_t * in, unsigned int width)
{
    unsigned int i;
    uint64_t * in_ptr = (uint64_t *)in;
    uint64_t * out_ptr = (uint64_t *)out;

    for (i = 0; i < width; i++)
    {
        uint64_t out0, out1, out2, out3, out4, out5, out6, out7;
        uint64_t tmp0;

        uint64_t in0 = out_ptr[0];
        uint64_t in1 = out_ptr[width];
        uint64_t in2 = out_ptr[width * 2];
        uint64_t in3 = out_ptr[width * 3];
        uint64_t in4 = out_ptr[width * 4];
        uint64_t in5 = out_ptr[width * 5];
        uint64_t in6 = out_ptr[width * 6];
        uint64_t in7 = out_ptr[width * 7];

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
        out_ptr[width] = out1 ^ in_ptr[width];
        out_ptr[width * 2] = out2 ^ in_ptr[width * 2];
        out_ptr[width * 3] = out3 ^ in_ptr[width * 3];
        out_ptr[width * 4] = out4 ^ in_ptr[width * 4];
        out_ptr[width * 5] = out5 ^ in_ptr[width * 5];
        out_ptr[width * 6] = out6 ^ in_ptr[width * 6];
        out_ptr[width * 7] = out7 ^ in_ptr[width * 7];

        in_ptr++;
        out_ptr++;
    }
}

static void gf8_muladd_9C(uint8_t * out, uint8_t * in, unsigned int width)
{
    unsigned int i;
    uint64_t * in_ptr = (uint64_t *)in;
    uint64_t * out_ptr = (uint64_t *)out;

    for (i = 0; i < width; i++)
    {
        uint64_t out0, out1, out2, out3, out4, out5, out6, out7;
        uint64_t tmp0;

        uint64_t in0 = out_ptr[0];
        uint64_t in1 = out_ptr[width];
        uint64_t in2 = out_ptr[width * 2];
        uint64_t in3 = out_ptr[width * 3];
        uint64_t in4 = out_ptr[width * 4];
        uint64_t in5 = out_ptr[width * 5];
        uint64_t in6 = out_ptr[width * 6];
        uint64_t in7 = out_ptr[width * 7];

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
        out_ptr[width] = out1 ^ in_ptr[width];
        out_ptr[width * 2] = out2 ^ in_ptr[width * 2];
        out_ptr[width * 3] = out3 ^ in_ptr[width * 3];
        out_ptr[width * 4] = out4 ^ in_ptr[width * 4];
        out_ptr[width * 5] = out5 ^ in_ptr[width * 5];
        out_ptr[width * 6] = out6 ^ in_ptr[width * 6];
        out_ptr[width * 7] = out7 ^ in_ptr[width * 7];

        in_ptr++;
        out_ptr++;
    }
}

static void gf8_muladd_9D(uint8_t * out, uint8_t * in, unsigned int width)
{
    unsigned int i;
    uint64_t * in_ptr = (uint64_t *)in;
    uint64_t * out_ptr = (uint64_t *)out;

    for (i = 0; i < width; i++)
    {
        uint64_t out0, out1, out2, out3, out4, out5, out6, out7;
        uint64_t tmp0;

        uint64_t in0 = out_ptr[0];
        uint64_t in1 = out_ptr[width];
        uint64_t in2 = out_ptr[width * 2];
        uint64_t in3 = out_ptr[width * 3];
        uint64_t in4 = out_ptr[width * 4];
        uint64_t in5 = out_ptr[width * 5];
        uint64_t in6 = out_ptr[width * 6];
        uint64_t in7 = out_ptr[width * 7];

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
        out_ptr[width] = out1 ^ in_ptr[width];
        out_ptr[width * 2] = out2 ^ in_ptr[width * 2];
        out_ptr[width * 3] = out3 ^ in_ptr[width * 3];
        out_ptr[width * 4] = out4 ^ in_ptr[width * 4];
        out_ptr[width * 5] = out5 ^ in_ptr[width * 5];
        out_ptr[width * 6] = out6 ^ in_ptr[width * 6];
        out_ptr[width * 7] = out7 ^ in_ptr[width * 7];

        in_ptr++;
        out_ptr++;
    }
}

static void gf8_muladd_9E(uint8_t * out, uint8_t * in, unsigned int width)
{
    unsigned int i;
    uint64_t * in_ptr = (uint64_t *)in;
    uint64_t * out_ptr = (uint64_t *)out;

    for (i = 0; i < width; i++)
    {
        uint64_t out0, out1, out2, out3, out4, out5, out6, out7;
        uint64_t tmp0;

        uint64_t in0 = out_ptr[0];
        uint64_t in1 = out_ptr[width];
        uint64_t in2 = out_ptr[width * 2];
        uint64_t in3 = out_ptr[width * 3];
        uint64_t in4 = out_ptr[width * 4];
        uint64_t in5 = out_ptr[width * 5];
        uint64_t in6 = out_ptr[width * 6];
        uint64_t in7 = out_ptr[width * 7];

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
        out_ptr[width] = out1 ^ in_ptr[width];
        out_ptr[width * 2] = out2 ^ in_ptr[width * 2];
        out_ptr[width * 3] = out3 ^ in_ptr[width * 3];
        out_ptr[width * 4] = out4 ^ in_ptr[width * 4];
        out_ptr[width * 5] = out5 ^ in_ptr[width * 5];
        out_ptr[width * 6] = out6 ^ in_ptr[width * 6];
        out_ptr[width * 7] = out7 ^ in_ptr[width * 7];

        in_ptr++;
        out_ptr++;
    }
}

static void gf8_muladd_9F(uint8_t * out, uint8_t * in, unsigned int width)
{
    unsigned int i;
    uint64_t * in_ptr = (uint64_t *)in;
    uint64_t * out_ptr = (uint64_t *)out;

    for (i = 0; i < width; i++)
    {
        uint64_t out0, out1, out2, out3, out4, out5, out6, out7;
        uint64_t tmp0;

        uint64_t in0 = out_ptr[0];
        uint64_t in1 = out_ptr[width];
        uint64_t in2 = out_ptr[width * 2];
        uint64_t in3 = out_ptr[width * 3];
        uint64_t in4 = out_ptr[width * 4];
        uint64_t in5 = out_ptr[width * 5];
        uint64_t in6 = out_ptr[width * 6];
        uint64_t in7 = out_ptr[width * 7];

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
        out_ptr[width] = out1 ^ in_ptr[width];
        out_ptr[width * 2] = out2 ^ in_ptr[width * 2];
        out_ptr[width * 3] = out3 ^ in_ptr[width * 3];
        out_ptr[width * 4] = out4 ^ in_ptr[width * 4];
        out_ptr[width * 5] = out5 ^ in_ptr[width * 5];
        out_ptr[width * 6] = out6 ^ in_ptr[width * 6];
        out_ptr[width * 7] = out7 ^ in_ptr[width * 7];

        in_ptr++;
        out_ptr++;
    }
}

static void gf8_muladd_A0(uint8_t * out, uint8_t * in, unsigned int width)
{
    unsigned int i;
    uint64_t * in_ptr = (uint64_t *)in;
    uint64_t * out_ptr = (uint64_t *)out;

    for (i = 0; i < width; i++)
    {
        uint64_t out0, out1, out2, out3, out4, out5, out6, out7;
        uint64_t tmp0, tmp1, tmp2, tmp3;

        uint64_t in0 = out_ptr[0];
        uint64_t in1 = out_ptr[width];
        uint64_t in2 = out_ptr[width * 2];
        uint64_t in3 = out_ptr[width * 3];
        uint64_t in4 = out_ptr[width * 4];
        uint64_t in5 = out_ptr[width * 5];
        uint64_t in6 = out_ptr[width * 6];
        uint64_t in7 = out_ptr[width * 7];

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
        out_ptr[width] = out1 ^ in_ptr[width];
        out_ptr[width * 2] = out2 ^ in_ptr[width * 2];
        out_ptr[width * 3] = out3 ^ in_ptr[width * 3];
        out_ptr[width * 4] = out4 ^ in_ptr[width * 4];
        out_ptr[width * 5] = out5 ^ in_ptr[width * 5];
        out_ptr[width * 6] = out6 ^ in_ptr[width * 6];
        out_ptr[width * 7] = out7 ^ in_ptr[width * 7];

        in_ptr++;
        out_ptr++;
    }
}

static void gf8_muladd_A1(uint8_t * out, uint8_t * in, unsigned int width)
{
    unsigned int i;
    uint64_t * in_ptr = (uint64_t *)in;
    uint64_t * out_ptr = (uint64_t *)out;

    for (i = 0; i < width; i++)
    {
        uint64_t out0, out1, out2, out3, out4, out5, out6, out7;
        uint64_t tmp0, tmp1, tmp2;

        uint64_t in0 = out_ptr[0];
        uint64_t in1 = out_ptr[width];
        uint64_t in2 = out_ptr[width * 2];
        uint64_t in3 = out_ptr[width * 3];
        uint64_t in4 = out_ptr[width * 4];
        uint64_t in5 = out_ptr[width * 5];
        uint64_t in6 = out_ptr[width * 6];
        uint64_t in7 = out_ptr[width * 7];

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
        out_ptr[width] = out1 ^ in_ptr[width];
        out_ptr[width * 2] = out2 ^ in_ptr[width * 2];
        out_ptr[width * 3] = out3 ^ in_ptr[width * 3];
        out_ptr[width * 4] = out4 ^ in_ptr[width * 4];
        out_ptr[width * 5] = out5 ^ in_ptr[width * 5];
        out_ptr[width * 6] = out6 ^ in_ptr[width * 6];
        out_ptr[width * 7] = out7 ^ in_ptr[width * 7];

        in_ptr++;
        out_ptr++;
    }
}

static void gf8_muladd_A2(uint8_t * out, uint8_t * in, unsigned int width)
{
    unsigned int i;
    uint64_t * in_ptr = (uint64_t *)in;
    uint64_t * out_ptr = (uint64_t *)out;

    for (i = 0; i < width; i++)
    {
        uint64_t out0, out1, out2, out3, out4, out5, out6, out7;
        uint64_t tmp0;

        uint64_t in0 = out_ptr[0];
        uint64_t in1 = out_ptr[width];
        uint64_t in2 = out_ptr[width * 2];
        uint64_t in3 = out_ptr[width * 3];
        uint64_t in4 = out_ptr[width * 4];
        uint64_t in5 = out_ptr[width * 5];
        uint64_t in6 = out_ptr[width * 6];
        uint64_t in7 = out_ptr[width * 7];

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
        out_ptr[width] = out1 ^ in_ptr[width];
        out_ptr[width * 2] = out2 ^ in_ptr[width * 2];
        out_ptr[width * 3] = out3 ^ in_ptr[width * 3];
        out_ptr[width * 4] = out4 ^ in_ptr[width * 4];
        out_ptr[width * 5] = out5 ^ in_ptr[width * 5];
        out_ptr[width * 6] = out6 ^ in_ptr[width * 6];
        out_ptr[width * 7] = out7 ^ in_ptr[width * 7];

        in_ptr++;
        out_ptr++;
    }
}

static void gf8_muladd_A3(uint8_t * out, uint8_t * in, unsigned int width)
{
    unsigned int i;
    uint64_t * in_ptr = (uint64_t *)in;
    uint64_t * out_ptr = (uint64_t *)out;

    for (i = 0; i < width; i++)
    {
        uint64_t out0, out1, out2, out3, out4, out5, out6, out7;
        uint64_t tmp0, tmp1;

        uint64_t in0 = out_ptr[0];
        uint64_t in1 = out_ptr[width];
        uint64_t in2 = out_ptr[width * 2];
        uint64_t in3 = out_ptr[width * 3];
        uint64_t in4 = out_ptr[width * 4];
        uint64_t in5 = out_ptr[width * 5];
        uint64_t in6 = out_ptr[width * 6];
        uint64_t in7 = out_ptr[width * 7];

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
        out_ptr[width] = out1 ^ in_ptr[width];
        out_ptr[width * 2] = out2 ^ in_ptr[width * 2];
        out_ptr[width * 3] = out3 ^ in_ptr[width * 3];
        out_ptr[width * 4] = out4 ^ in_ptr[width * 4];
        out_ptr[width * 5] = out5 ^ in_ptr[width * 5];
        out_ptr[width * 6] = out6 ^ in_ptr[width * 6];
        out_ptr[width * 7] = out7 ^ in_ptr[width * 7];

        in_ptr++;
        out_ptr++;
    }
}

static void gf8_muladd_A4(uint8_t * out, uint8_t * in, unsigned int width)
{
    unsigned int i;
    uint64_t * in_ptr = (uint64_t *)in;
    uint64_t * out_ptr = (uint64_t *)out;

    for (i = 0; i < width; i++)
    {
        uint64_t out0, out1, out2, out3, out4, out5, out6, out7;
        uint64_t tmp0, tmp1, tmp2, tmp3, tmp4;

        uint64_t in0 = out_ptr[0];
        uint64_t in1 = out_ptr[width];
        uint64_t in2 = out_ptr[width * 2];
        uint64_t in3 = out_ptr[width * 3];
        uint64_t in4 = out_ptr[width * 4];
        uint64_t in5 = out_ptr[width * 5];
        uint64_t in6 = out_ptr[width * 6];
        uint64_t in7 = out_ptr[width * 7];

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
        out_ptr[width] = out1 ^ in_ptr[width];
        out_ptr[width * 2] = out2 ^ in_ptr[width * 2];
        out_ptr[width * 3] = out3 ^ in_ptr[width * 3];
        out_ptr[width * 4] = out4 ^ in_ptr[width * 4];
        out_ptr[width * 5] = out5 ^ in_ptr[width * 5];
        out_ptr[width * 6] = out6 ^ in_ptr[width * 6];
        out_ptr[width * 7] = out7 ^ in_ptr[width * 7];

        in_ptr++;
        out_ptr++;
    }
}

static void gf8_muladd_A5(uint8_t * out, uint8_t * in, unsigned int width)
{
    unsigned int i;
    uint64_t * in_ptr = (uint64_t *)in;
    uint64_t * out_ptr = (uint64_t *)out;

    for (i = 0; i < width; i++)
    {
        uint64_t out0, out1, out2, out3, out4, out5, out6, out7;
        uint64_t tmp0, tmp1, tmp2;

        uint64_t in0 = out_ptr[0];
        uint64_t in1 = out_ptr[width];
        uint64_t in2 = out_ptr[width * 2];
        uint64_t in3 = out_ptr[width * 3];
        uint64_t in4 = out_ptr[width * 4];
        uint64_t in5 = out_ptr[width * 5];
        uint64_t in6 = out_ptr[width * 6];
        uint64_t in7 = out_ptr[width * 7];

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
        out_ptr[width] = out1 ^ in_ptr[width];
        out_ptr[width * 2] = out2 ^ in_ptr[width * 2];
        out_ptr[width * 3] = out3 ^ in_ptr[width * 3];
        out_ptr[width * 4] = out4 ^ in_ptr[width * 4];
        out_ptr[width * 5] = out5 ^ in_ptr[width * 5];
        out_ptr[width * 6] = out6 ^ in_ptr[width * 6];
        out_ptr[width * 7] = out7 ^ in_ptr[width * 7];

        in_ptr++;
        out_ptr++;
    }
}

static void gf8_muladd_A6(uint8_t * out, uint8_t * in, unsigned int width)
{
    unsigned int i;
    uint64_t * in_ptr = (uint64_t *)in;
    uint64_t * out_ptr = (uint64_t *)out;

    for (i = 0; i < width; i++)
    {
        uint64_t out0, out1, out2, out3, out4, out5, out6, out7;

        uint64_t in0 = out_ptr[0];
        uint64_t in1 = out_ptr[width];
        uint64_t in2 = out_ptr[width * 2];
        uint64_t in3 = out_ptr[width * 3];
        uint64_t in4 = out_ptr[width * 4];
        uint64_t in5 = out_ptr[width * 5];
        uint64_t in6 = out_ptr[width * 6];
        uint64_t in7 = out_ptr[width * 7];

        out2 = in0;
        out3 = in3 ^ in5 ^ in7;
        out1 = in0 ^ in2 ^ in4 ^ in6;
        out0 = out3 ^ in1;
        out7 = out1 ^ in7;
        out6 = out0 ^ in6;
        out5 = out7 ^ in5;
        out4 = out6 ^ in4;

        out_ptr[0] = out0 ^ in_ptr[0];
        out_ptr[width] = out1 ^ in_ptr[width];
        out_ptr[width * 2] = out2 ^ in_ptr[width * 2];
        out_ptr[width * 3] = out3 ^ in_ptr[width * 3];
        out_ptr[width * 4] = out4 ^ in_ptr[width * 4];
        out_ptr[width * 5] = out5 ^ in_ptr[width * 5];
        out_ptr[width * 6] = out6 ^ in_ptr[width * 6];
        out_ptr[width * 7] = out7 ^ in_ptr[width * 7];

        in_ptr++;
        out_ptr++;
    }
}

static void gf8_muladd_A7(uint8_t * out, uint8_t * in, unsigned int width)
{
    unsigned int i;
    uint64_t * in_ptr = (uint64_t *)in;
    uint64_t * out_ptr = (uint64_t *)out;

    for (i = 0; i < width; i++)
    {
        uint64_t out0, out1, out2, out3, out4, out5, out6, out7;

        uint64_t in0 = out_ptr[0];
        uint64_t in1 = out_ptr[width];
        uint64_t in2 = out_ptr[width * 2];
        uint64_t in3 = out_ptr[width * 3];
        uint64_t in4 = out_ptr[width * 4];
        uint64_t in5 = out_ptr[width * 5];
        uint64_t in6 = out_ptr[width * 6];
        uint64_t in7 = out_ptr[width * 7];

        out2 = in0 ^ in2;
        out3 = in5 ^ in7;
        out7 = out2 ^ in4 ^ in6;
        out6 = out3 ^ in1 ^ in3;
        out1 = out7 ^ in1;
        out5 = out7 ^ in7;
        out0 = out6 ^ in0;
        out4 = out6 ^ in6;

        out_ptr[0] = out0 ^ in_ptr[0];
        out_ptr[width] = out1 ^ in_ptr[width];
        out_ptr[width * 2] = out2 ^ in_ptr[width * 2];
        out_ptr[width * 3] = out3 ^ in_ptr[width * 3];
        out_ptr[width * 4] = out4 ^ in_ptr[width * 4];
        out_ptr[width * 5] = out5 ^ in_ptr[width * 5];
        out_ptr[width * 6] = out6 ^ in_ptr[width * 6];
        out_ptr[width * 7] = out7 ^ in_ptr[width * 7];

        in_ptr++;
        out_ptr++;
    }
}

static void gf8_muladd_A8(uint8_t * out, uint8_t * in, unsigned int width)
{
    unsigned int i;
    uint64_t * in_ptr = (uint64_t *)in;
    uint64_t * out_ptr = (uint64_t *)out;

    for (i = 0; i < width; i++)
    {
        uint64_t out0, out1, out2, out3, out4, out5, out6, out7;
        uint64_t tmp0, tmp1, tmp2;

        uint64_t in0 = out_ptr[0];
        uint64_t in1 = out_ptr[width];
        uint64_t in2 = out_ptr[width * 2];
        uint64_t in3 = out_ptr[width * 3];
        uint64_t in4 = out_ptr[width * 4];
        uint64_t in5 = out_ptr[width * 5];
        uint64_t in6 = out_ptr[width * 6];
        uint64_t in7 = out_ptr[width * 7];

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
        out_ptr[width] = out1 ^ in_ptr[width];
        out_ptr[width * 2] = out2 ^ in_ptr[width * 2];
        out_ptr[width * 3] = out3 ^ in_ptr[width * 3];
        out_ptr[width * 4] = out4 ^ in_ptr[width * 4];
        out_ptr[width * 5] = out5 ^ in_ptr[width * 5];
        out_ptr[width * 6] = out6 ^ in_ptr[width * 6];
        out_ptr[width * 7] = out7 ^ in_ptr[width * 7];

        in_ptr++;
        out_ptr++;
    }
}

static void gf8_muladd_A9(uint8_t * out, uint8_t * in, unsigned int width)
{
    unsigned int i;
    uint64_t * in_ptr = (uint64_t *)in;
    uint64_t * out_ptr = (uint64_t *)out;

    for (i = 0; i < width; i++)
    {
        uint64_t out0, out1, out2, out3, out4, out5, out6, out7;

        uint64_t in0 = out_ptr[0];
        uint64_t in1 = out_ptr[width];
        uint64_t in2 = out_ptr[width * 2];
        uint64_t in3 = out_ptr[width * 3];
        uint64_t in4 = out_ptr[width * 4];
        uint64_t in5 = out_ptr[width * 5];
        uint64_t in6 = out_ptr[width * 6];
        uint64_t in7 = out_ptr[width * 7];

        out4 = in2 ^ in6;
        out6 = in1 ^ in4;
        out7 = in0 ^ in2 ^ in5;
        out5 = in0 ^ in3 ^ in7;
        out2 = out4 ^ in1 ^ in5;
        out1 = out6 ^ in2 ^ in7;
        out0 = out2 ^ out7 ^ in3;
        out3 = out1 ^ in0 ^ in4;

        out_ptr[0] = out0 ^ in_ptr[0];
        out_ptr[width] = out1 ^ in_ptr[width];
        out_ptr[width * 2] = out2 ^ in_ptr[width * 2];
        out_ptr[width * 3] = out3 ^ in_ptr[width * 3];
        out_ptr[width * 4] = out4 ^ in_ptr[width * 4];
        out_ptr[width * 5] = out5 ^ in_ptr[width * 5];
        out_ptr[width * 6] = out6 ^ in_ptr[width * 6];
        out_ptr[width * 7] = out7 ^ in_ptr[width * 7];

        in_ptr++;
        out_ptr++;
    }
}

static void gf8_muladd_AA(uint8_t * out, uint8_t * in, unsigned int width)
{
    unsigned int i;
    uint64_t * in_ptr = (uint64_t *)in;
    uint64_t * out_ptr = (uint64_t *)out;

    for (i = 0; i < width; i++)
    {
        uint64_t out0, out1, out2, out3, out4, out5, out6, out7;
        uint64_t tmp0, tmp1, tmp2;

        uint64_t in0 = out_ptr[0];
        uint64_t in1 = out_ptr[width];
        uint64_t in2 = out_ptr[width * 2];
        uint64_t in3 = out_ptr[width * 3];
        uint64_t in4 = out_ptr[width * 4];
        uint64_t in5 = out_ptr[width * 5];
        uint64_t in6 = out_ptr[width * 6];
        uint64_t in7 = out_ptr[width * 7];

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
        out_ptr[width] = out1 ^ in_ptr[width];
        out_ptr[width * 2] = out2 ^ in_ptr[width * 2];
        out_ptr[width * 3] = out3 ^ in_ptr[width * 3];
        out_ptr[width * 4] = out4 ^ in_ptr[width * 4];
        out_ptr[width * 5] = out5 ^ in_ptr[width * 5];
        out_ptr[width * 6] = out6 ^ in_ptr[width * 6];
        out_ptr[width * 7] = out7 ^ in_ptr[width * 7];

        in_ptr++;
        out_ptr++;
    }
}

static void gf8_muladd_AB(uint8_t * out, uint8_t * in, unsigned int width)
{
    unsigned int i;
    uint64_t * in_ptr = (uint64_t *)in;
    uint64_t * out_ptr = (uint64_t *)out;

    for (i = 0; i < width; i++)
    {
        uint64_t out0, out1, out2, out3, out4, out5, out6, out7;
        uint64_t tmp0, tmp1;

        uint64_t in0 = out_ptr[0];
        uint64_t in1 = out_ptr[width];
        uint64_t in2 = out_ptr[width * 2];
        uint64_t in3 = out_ptr[width * 3];
        uint64_t in4 = out_ptr[width * 4];
        uint64_t in5 = out_ptr[width * 5];
        uint64_t in6 = out_ptr[width * 6];
        uint64_t in7 = out_ptr[width * 7];

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
        out_ptr[width] = out1 ^ in_ptr[width];
        out_ptr[width * 2] = out2 ^ in_ptr[width * 2];
        out_ptr[width * 3] = out3 ^ in_ptr[width * 3];
        out_ptr[width * 4] = out4 ^ in_ptr[width * 4];
        out_ptr[width * 5] = out5 ^ in_ptr[width * 5];
        out_ptr[width * 6] = out6 ^ in_ptr[width * 6];
        out_ptr[width * 7] = out7 ^ in_ptr[width * 7];

        in_ptr++;
        out_ptr++;
    }
}

static void gf8_muladd_AC(uint8_t * out, uint8_t * in, unsigned int width)
{
    unsigned int i;
    uint64_t * in_ptr = (uint64_t *)in;
    uint64_t * out_ptr = (uint64_t *)out;

    for (i = 0; i < width; i++)
    {
        uint64_t out0, out1, out2, out3, out4, out5, out6, out7;
        uint64_t tmp0;

        uint64_t in0 = out_ptr[0];
        uint64_t in1 = out_ptr[width];
        uint64_t in2 = out_ptr[width * 2];
        uint64_t in3 = out_ptr[width * 3];
        uint64_t in4 = out_ptr[width * 4];
        uint64_t in5 = out_ptr[width * 5];
        uint64_t in6 = out_ptr[width * 6];
        uint64_t in7 = out_ptr[width * 7];

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
        out_ptr[width] = out1 ^ in_ptr[width];
        out_ptr[width * 2] = out2 ^ in_ptr[width * 2];
        out_ptr[width * 3] = out3 ^ in_ptr[width * 3];
        out_ptr[width * 4] = out4 ^ in_ptr[width * 4];
        out_ptr[width * 5] = out5 ^ in_ptr[width * 5];
        out_ptr[width * 6] = out6 ^ in_ptr[width * 6];
        out_ptr[width * 7] = out7 ^ in_ptr[width * 7];

        in_ptr++;
        out_ptr++;
    }
}

static void gf8_muladd_AD(uint8_t * out, uint8_t * in, unsigned int width)
{
    unsigned int i;
    uint64_t * in_ptr = (uint64_t *)in;
    uint64_t * out_ptr = (uint64_t *)out;

    for (i = 0; i < width; i++)
    {
        uint64_t out0, out1, out2, out3, out4, out5, out6, out7;

        uint64_t in0 = out_ptr[0];
        uint64_t in1 = out_ptr[width];
        uint64_t in2 = out_ptr[width * 2];
        uint64_t in3 = out_ptr[width * 3];
        uint64_t in4 = out_ptr[width * 4];
        uint64_t in5 = out_ptr[width * 5];
        uint64_t in6 = out_ptr[width * 6];
        uint64_t in7 = out_ptr[width * 7];

        out4 = in7;
        out5 = in0;
        out6 = in1;
        out7 = in0 ^ in2;
        out0 = in0 ^ in1 ^ in3;
        out2 = out7 ^ in1 ^ in5;
        out1 = in1 ^ in2 ^ in4;
        out3 = out7 ^ in6;

        out_ptr[0] = out0 ^ in_ptr[0];
        out_ptr[width] = out1 ^ in_ptr[width];
        out_ptr[width * 2] = out2 ^ in_ptr[width * 2];
        out_ptr[width * 3] = out3 ^ in_ptr[width * 3];
        out_ptr[width * 4] = out4 ^ in_ptr[width * 4];
        out_ptr[width * 5] = out5 ^ in_ptr[width * 5];
        out_ptr[width * 6] = out6 ^ in_ptr[width * 6];
        out_ptr[width * 7] = out7 ^ in_ptr[width * 7];

        in_ptr++;
        out_ptr++;
    }
}

static void gf8_muladd_AE(uint8_t * out, uint8_t * in, unsigned int width)
{
    unsigned int i;
    uint64_t * in_ptr = (uint64_t *)in;
    uint64_t * out_ptr = (uint64_t *)out;

    for (i = 0; i < width; i++)
    {
        uint64_t out0, out1, out2, out3, out4, out5, out6, out7;
        uint64_t tmp0, tmp1, tmp2;

        uint64_t in0 = out_ptr[0];
        uint64_t in1 = out_ptr[width];
        uint64_t in2 = out_ptr[width * 2];
        uint64_t in3 = out_ptr[width * 3];
        uint64_t in4 = out_ptr[width * 4];
        uint64_t in5 = out_ptr[width * 5];
        uint64_t in6 = out_ptr[width * 6];
        uint64_t in7 = out_ptr[width * 7];

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
        out_ptr[width] = out1 ^ in_ptr[width];
        out_ptr[width * 2] = out2 ^ in_ptr[width * 2];
        out_ptr[width * 3] = out3 ^ in_ptr[width * 3];
        out_ptr[width * 4] = out4 ^ in_ptr[width * 4];
        out_ptr[width * 5] = out5 ^ in_ptr[width * 5];
        out_ptr[width * 6] = out6 ^ in_ptr[width * 6];
        out_ptr[width * 7] = out7 ^ in_ptr[width * 7];

        in_ptr++;
        out_ptr++;
    }
}

static void gf8_muladd_AF(uint8_t * out, uint8_t * in, unsigned int width)
{
    unsigned int i;
    uint64_t * in_ptr = (uint64_t *)in;
    uint64_t * out_ptr = (uint64_t *)out;

    for (i = 0; i < width; i++)
    {
        uint64_t out0, out1, out2, out3, out4, out5, out6, out7;
        uint64_t tmp0;

        uint64_t in0 = out_ptr[0];
        uint64_t in1 = out_ptr[width];
        uint64_t in2 = out_ptr[width * 2];
        uint64_t in3 = out_ptr[width * 3];
        uint64_t in4 = out_ptr[width * 4];
        uint64_t in5 = out_ptr[width * 5];
        uint64_t in6 = out_ptr[width * 6];
        uint64_t in7 = out_ptr[width * 7];

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
        out_ptr[width] = out1 ^ in_ptr[width];
        out_ptr[width * 2] = out2 ^ in_ptr[width * 2];
        out_ptr[width * 3] = out3 ^ in_ptr[width * 3];
        out_ptr[width * 4] = out4 ^ in_ptr[width * 4];
        out_ptr[width * 5] = out5 ^ in_ptr[width * 5];
        out_ptr[width * 6] = out6 ^ in_ptr[width * 6];
        out_ptr[width * 7] = out7 ^ in_ptr[width * 7];

        in_ptr++;
        out_ptr++;
    }
}

static void gf8_muladd_B0(uint8_t * out, uint8_t * in, unsigned int width)
{
    unsigned int i;
    uint64_t * in_ptr = (uint64_t *)in;
    uint64_t * out_ptr = (uint64_t *)out;

    for (i = 0; i < width; i++)
    {
        uint64_t out0, out1, out2, out3, out4, out5, out6, out7;
        uint64_t tmp0, tmp1, tmp2, tmp3;

        uint64_t in0 = out_ptr[0];
        uint64_t in1 = out_ptr[width];
        uint64_t in2 = out_ptr[width * 2];
        uint64_t in3 = out_ptr[width * 3];
        uint64_t in4 = out_ptr[width * 4];
        uint64_t in5 = out_ptr[width * 5];
        uint64_t in6 = out_ptr[width * 6];
        uint64_t in7 = out_ptr[width * 7];

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
        out_ptr[width] = out1 ^ in_ptr[width];
        out_ptr[width * 2] = out2 ^ in_ptr[width * 2];
        out_ptr[width * 3] = out3 ^ in_ptr[width * 3];
        out_ptr[width * 4] = out4 ^ in_ptr[width * 4];
        out_ptr[width * 5] = out5 ^ in_ptr[width * 5];
        out_ptr[width * 6] = out6 ^ in_ptr[width * 6];
        out_ptr[width * 7] = out7 ^ in_ptr[width * 7];

        in_ptr++;
        out_ptr++;
    }
}

static void gf8_muladd_B1(uint8_t * out, uint8_t * in, unsigned int width)
{
    unsigned int i;
    uint64_t * in_ptr = (uint64_t *)in;
    uint64_t * out_ptr = (uint64_t *)out;

    for (i = 0; i < width; i++)
    {
        uint64_t out0, out1, out2, out3, out4, out5, out6, out7;
        uint64_t tmp0, tmp1;

        uint64_t in0 = out_ptr[0];
        uint64_t in1 = out_ptr[width];
        uint64_t in2 = out_ptr[width * 2];
        uint64_t in3 = out_ptr[width * 3];
        uint64_t in4 = out_ptr[width * 4];
        uint64_t in5 = out_ptr[width * 5];
        uint64_t in6 = out_ptr[width * 6];
        uint64_t in7 = out_ptr[width * 7];

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
        out_ptr[width] = out1 ^ in_ptr[width];
        out_ptr[width * 2] = out2 ^ in_ptr[width * 2];
        out_ptr[width * 3] = out3 ^ in_ptr[width * 3];
        out_ptr[width * 4] = out4 ^ in_ptr[width * 4];
        out_ptr[width * 5] = out5 ^ in_ptr[width * 5];
        out_ptr[width * 6] = out6 ^ in_ptr[width * 6];
        out_ptr[width * 7] = out7 ^ in_ptr[width * 7];

        in_ptr++;
        out_ptr++;
    }
}

static void gf8_muladd_B2(uint8_t * out, uint8_t * in, unsigned int width)
{
    unsigned int i;
    uint64_t * in_ptr = (uint64_t *)in;
    uint64_t * out_ptr = (uint64_t *)out;

    for (i = 0; i < width; i++)
    {
        uint64_t out0, out1, out2, out3, out4, out5, out6, out7;
        uint64_t tmp0, tmp1, tmp2, tmp3, tmp4;

        uint64_t in0 = out_ptr[0];
        uint64_t in1 = out_ptr[width];
        uint64_t in2 = out_ptr[width * 2];
        uint64_t in3 = out_ptr[width * 3];
        uint64_t in4 = out_ptr[width * 4];
        uint64_t in5 = out_ptr[width * 5];
        uint64_t in6 = out_ptr[width * 6];
        uint64_t in7 = out_ptr[width * 7];

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
        out_ptr[width] = out1 ^ in_ptr[width];
        out_ptr[width * 2] = out2 ^ in_ptr[width * 2];
        out_ptr[width * 3] = out3 ^ in_ptr[width * 3];
        out_ptr[width * 4] = out4 ^ in_ptr[width * 4];
        out_ptr[width * 5] = out5 ^ in_ptr[width * 5];
        out_ptr[width * 6] = out6 ^ in_ptr[width * 6];
        out_ptr[width * 7] = out7 ^ in_ptr[width * 7];

        in_ptr++;
        out_ptr++;
    }
}

static void gf8_muladd_B3(uint8_t * out, uint8_t * in, unsigned int width)
{
    unsigned int i;
    uint64_t * in_ptr = (uint64_t *)in;
    uint64_t * out_ptr = (uint64_t *)out;

    for (i = 0; i < width; i++)
    {
        uint64_t out0, out1, out2, out3, out4, out5, out6, out7;
        uint64_t tmp0, tmp1, tmp2;

        uint64_t in0 = out_ptr[0];
        uint64_t in1 = out_ptr[width];
        uint64_t in2 = out_ptr[width * 2];
        uint64_t in3 = out_ptr[width * 3];
        uint64_t in4 = out_ptr[width * 4];
        uint64_t in5 = out_ptr[width * 5];
        uint64_t in6 = out_ptr[width * 6];
        uint64_t in7 = out_ptr[width * 7];

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
        out_ptr[width] = out1 ^ in_ptr[width];
        out_ptr[width * 2] = out2 ^ in_ptr[width * 2];
        out_ptr[width * 3] = out3 ^ in_ptr[width * 3];
        out_ptr[width * 4] = out4 ^ in_ptr[width * 4];
        out_ptr[width * 5] = out5 ^ in_ptr[width * 5];
        out_ptr[width * 6] = out6 ^ in_ptr[width * 6];
        out_ptr[width * 7] = out7 ^ in_ptr[width * 7];

        in_ptr++;
        out_ptr++;
    }
}

static void gf8_muladd_B4(uint8_t * out, uint8_t * in, unsigned int width)
{
    unsigned int i;
    uint64_t * in_ptr = (uint64_t *)in;
    uint64_t * out_ptr = (uint64_t *)out;

    for (i = 0; i < width; i++)
    {
        uint64_t out0, out1, out2, out3, out4, out5, out6, out7;
        uint64_t tmp0;

        uint64_t in0 = out_ptr[0];
        uint64_t in1 = out_ptr[width];
        uint64_t in2 = out_ptr[width * 2];
        uint64_t in3 = out_ptr[width * 3];
        uint64_t in4 = out_ptr[width * 4];
        uint64_t in5 = out_ptr[width * 5];
        uint64_t in6 = out_ptr[width * 6];
        uint64_t in7 = out_ptr[width * 7];

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
        out_ptr[width] = out1 ^ in_ptr[width];
        out_ptr[width * 2] = out2 ^ in_ptr[width * 2];
        out_ptr[width * 3] = out3 ^ in_ptr[width * 3];
        out_ptr[width * 4] = out4 ^ in_ptr[width * 4];
        out_ptr[width * 5] = out5 ^ in_ptr[width * 5];
        out_ptr[width * 6] = out6 ^ in_ptr[width * 6];
        out_ptr[width * 7] = out7 ^ in_ptr[width * 7];

        in_ptr++;
        out_ptr++;
    }
}

static void gf8_muladd_B5(uint8_t * out, uint8_t * in, unsigned int width)
{
    unsigned int i;
    uint64_t * in_ptr = (uint64_t *)in;
    uint64_t * out_ptr = (uint64_t *)out;

    for (i = 0; i < width; i++)
    {
        uint64_t out0, out1, out2, out3, out4, out5, out6, out7;
        uint64_t tmp0, tmp1, tmp2;

        uint64_t in0 = out_ptr[0];
        uint64_t in1 = out_ptr[width];
        uint64_t in2 = out_ptr[width * 2];
        uint64_t in3 = out_ptr[width * 3];
        uint64_t in4 = out_ptr[width * 4];
        uint64_t in5 = out_ptr[width * 5];
        uint64_t in6 = out_ptr[width * 6];
        uint64_t in7 = out_ptr[width * 7];

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
        out_ptr[width] = out1 ^ in_ptr[width];
        out_ptr[width * 2] = out2 ^ in_ptr[width * 2];
        out_ptr[width * 3] = out3 ^ in_ptr[width * 3];
        out_ptr[width * 4] = out4 ^ in_ptr[width * 4];
        out_ptr[width * 5] = out5 ^ in_ptr[width * 5];
        out_ptr[width * 6] = out6 ^ in_ptr[width * 6];
        out_ptr[width * 7] = out7 ^ in_ptr[width * 7];

        in_ptr++;
        out_ptr++;
    }
}

static void gf8_muladd_B6(uint8_t * out, uint8_t * in, unsigned int width)
{
    unsigned int i;
    uint64_t * in_ptr = (uint64_t *)in;
    uint64_t * out_ptr = (uint64_t *)out;

    for (i = 0; i < width; i++)
    {
        uint64_t out0, out1, out2, out3, out4, out5, out6, out7;
        uint64_t tmp0, tmp1, tmp2, tmp3;

        uint64_t in0 = out_ptr[0];
        uint64_t in1 = out_ptr[width];
        uint64_t in2 = out_ptr[width * 2];
        uint64_t in3 = out_ptr[width * 3];
        uint64_t in4 = out_ptr[width * 4];
        uint64_t in5 = out_ptr[width * 5];
        uint64_t in6 = out_ptr[width * 6];
        uint64_t in7 = out_ptr[width * 7];

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
        out_ptr[width] = out1 ^ in_ptr[width];
        out_ptr[width * 2] = out2 ^ in_ptr[width * 2];
        out_ptr[width * 3] = out3 ^ in_ptr[width * 3];
        out_ptr[width * 4] = out4 ^ in_ptr[width * 4];
        out_ptr[width * 5] = out5 ^ in_ptr[width * 5];
        out_ptr[width * 6] = out6 ^ in_ptr[width * 6];
        out_ptr[width * 7] = out7 ^ in_ptr[width * 7];

        in_ptr++;
        out_ptr++;
    }
}

static void gf8_muladd_B7(uint8_t * out, uint8_t * in, unsigned int width)
{
    unsigned int i;
    uint64_t * in_ptr = (uint64_t *)in;
    uint64_t * out_ptr = (uint64_t *)out;

    for (i = 0; i < width; i++)
    {
        uint64_t out0, out1, out2, out3, out4, out5, out6, out7;
        uint64_t tmp0, tmp1;

        uint64_t in0 = out_ptr[0];
        uint64_t in1 = out_ptr[width];
        uint64_t in2 = out_ptr[width * 2];
        uint64_t in3 = out_ptr[width * 3];
        uint64_t in4 = out_ptr[width * 4];
        uint64_t in5 = out_ptr[width * 5];
        uint64_t in6 = out_ptr[width * 6];
        uint64_t in7 = out_ptr[width * 7];

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
        out_ptr[width] = out1 ^ in_ptr[width];
        out_ptr[width * 2] = out2 ^ in_ptr[width * 2];
        out_ptr[width * 3] = out3 ^ in_ptr[width * 3];
        out_ptr[width * 4] = out4 ^ in_ptr[width * 4];
        out_ptr[width * 5] = out5 ^ in_ptr[width * 5];
        out_ptr[width * 6] = out6 ^ in_ptr[width * 6];
        out_ptr[width * 7] = out7 ^ in_ptr[width * 7];

        in_ptr++;
        out_ptr++;
    }
}

static void gf8_muladd_B8(uint8_t * out, uint8_t * in, unsigned int width)
{
    unsigned int i;
    uint64_t * in_ptr = (uint64_t *)in;
    uint64_t * out_ptr = (uint64_t *)out;

    for (i = 0; i < width; i++)
    {
        uint64_t out0, out1, out2, out3, out4, out5, out6, out7;
        uint64_t tmp0, tmp1, tmp2;

        uint64_t in0 = out_ptr[0];
        uint64_t in1 = out_ptr[width];
        uint64_t in2 = out_ptr[width * 2];
        uint64_t in3 = out_ptr[width * 3];
        uint64_t in4 = out_ptr[width * 4];
        uint64_t in5 = out_ptr[width * 5];
        uint64_t in6 = out_ptr[width * 6];
        uint64_t in7 = out_ptr[width * 7];

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
        out_ptr[width] = out1 ^ in_ptr[width];
        out_ptr[width * 2] = out2 ^ in_ptr[width * 2];
        out_ptr[width * 3] = out3 ^ in_ptr[width * 3];
        out_ptr[width * 4] = out4 ^ in_ptr[width * 4];
        out_ptr[width * 5] = out5 ^ in_ptr[width * 5];
        out_ptr[width * 6] = out6 ^ in_ptr[width * 6];
        out_ptr[width * 7] = out7 ^ in_ptr[width * 7];

        in_ptr++;
        out_ptr++;
    }
}

static void gf8_muladd_B9(uint8_t * out, uint8_t * in, unsigned int width)
{
    unsigned int i;
    uint64_t * in_ptr = (uint64_t *)in;
    uint64_t * out_ptr = (uint64_t *)out;

    for (i = 0; i < width; i++)
    {
        uint64_t out0, out1, out2, out3, out4, out5, out6, out7;
        uint64_t tmp0, tmp1, tmp2;

        uint64_t in0 = out_ptr[0];
        uint64_t in1 = out_ptr[width];
        uint64_t in2 = out_ptr[width * 2];
        uint64_t in3 = out_ptr[width * 3];
        uint64_t in4 = out_ptr[width * 4];
        uint64_t in5 = out_ptr[width * 5];
        uint64_t in6 = out_ptr[width * 6];
        uint64_t in7 = out_ptr[width * 7];

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
        out_ptr[width] = out1 ^ in_ptr[width];
        out_ptr[width * 2] = out2 ^ in_ptr[width * 2];
        out_ptr[width * 3] = out3 ^ in_ptr[width * 3];
        out_ptr[width * 4] = out4 ^ in_ptr[width * 4];
        out_ptr[width * 5] = out5 ^ in_ptr[width * 5];
        out_ptr[width * 6] = out6 ^ in_ptr[width * 6];
        out_ptr[width * 7] = out7 ^ in_ptr[width * 7];

        in_ptr++;
        out_ptr++;
    }
}

static void gf8_muladd_BA(uint8_t * out, uint8_t * in, unsigned int width)
{
    unsigned int i;
    uint64_t * in_ptr = (uint64_t *)in;
    uint64_t * out_ptr = (uint64_t *)out;

    for (i = 0; i < width; i++)
    {
        uint64_t out0, out1, out2, out3, out4, out5, out6, out7;
        uint64_t tmp0, tmp1, tmp2;

        uint64_t in0 = out_ptr[0];
        uint64_t in1 = out_ptr[width];
        uint64_t in2 = out_ptr[width * 2];
        uint64_t in3 = out_ptr[width * 3];
        uint64_t in4 = out_ptr[width * 4];
        uint64_t in5 = out_ptr[width * 5];
        uint64_t in6 = out_ptr[width * 6];
        uint64_t in7 = out_ptr[width * 7];

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
        out_ptr[width] = out1 ^ in_ptr[width];
        out_ptr[width * 2] = out2 ^ in_ptr[width * 2];
        out_ptr[width * 3] = out3 ^ in_ptr[width * 3];
        out_ptr[width * 4] = out4 ^ in_ptr[width * 4];
        out_ptr[width * 5] = out5 ^ in_ptr[width * 5];
        out_ptr[width * 6] = out6 ^ in_ptr[width * 6];
        out_ptr[width * 7] = out7 ^ in_ptr[width * 7];

        in_ptr++;
        out_ptr++;
    }
}

static void gf8_muladd_BB(uint8_t * out, uint8_t * in, unsigned int width)
{
    unsigned int i;
    uint64_t * in_ptr = (uint64_t *)in;
    uint64_t * out_ptr = (uint64_t *)out;

    for (i = 0; i < width; i++)
    {
        uint64_t out0, out1, out2, out3, out4, out5, out6, out7;
        uint64_t tmp0, tmp1;

        uint64_t in0 = out_ptr[0];
        uint64_t in1 = out_ptr[width];
        uint64_t in2 = out_ptr[width * 2];
        uint64_t in3 = out_ptr[width * 3];
        uint64_t in4 = out_ptr[width * 4];
        uint64_t in5 = out_ptr[width * 5];
        uint64_t in6 = out_ptr[width * 6];
        uint64_t in7 = out_ptr[width * 7];

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
        out_ptr[width] = out1 ^ in_ptr[width];
        out_ptr[width * 2] = out2 ^ in_ptr[width * 2];
        out_ptr[width * 3] = out3 ^ in_ptr[width * 3];
        out_ptr[width * 4] = out4 ^ in_ptr[width * 4];
        out_ptr[width * 5] = out5 ^ in_ptr[width * 5];
        out_ptr[width * 6] = out6 ^ in_ptr[width * 6];
        out_ptr[width * 7] = out7 ^ in_ptr[width * 7];

        in_ptr++;
        out_ptr++;
    }
}

static void gf8_muladd_BC(uint8_t * out, uint8_t * in, unsigned int width)
{
    unsigned int i;
    uint64_t * in_ptr = (uint64_t *)in;
    uint64_t * out_ptr = (uint64_t *)out;

    for (i = 0; i < width; i++)
    {
        uint64_t out0, out1, out2, out3, out4, out5, out6, out7;
        uint64_t tmp0, tmp1, tmp2;

        uint64_t in0 = out_ptr[0];
        uint64_t in1 = out_ptr[width];
        uint64_t in2 = out_ptr[width * 2];
        uint64_t in3 = out_ptr[width * 3];
        uint64_t in4 = out_ptr[width * 4];
        uint64_t in5 = out_ptr[width * 5];
        uint64_t in6 = out_ptr[width * 6];
        uint64_t in7 = out_ptr[width * 7];

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
        out_ptr[width] = out1 ^ in_ptr[width];
        out_ptr[width * 2] = out2 ^ in_ptr[width * 2];
        out_ptr[width * 3] = out3 ^ in_ptr[width * 3];
        out_ptr[width * 4] = out4 ^ in_ptr[width * 4];
        out_ptr[width * 5] = out5 ^ in_ptr[width * 5];
        out_ptr[width * 6] = out6 ^ in_ptr[width * 6];
        out_ptr[width * 7] = out7 ^ in_ptr[width * 7];

        in_ptr++;
        out_ptr++;
    }
}

static void gf8_muladd_BD(uint8_t * out, uint8_t * in, unsigned int width)
{
    unsigned int i;
    uint64_t * in_ptr = (uint64_t *)in;
    uint64_t * out_ptr = (uint64_t *)out;

    for (i = 0; i < width; i++)
    {
        uint64_t out0, out1, out2, out3, out4, out5, out6, out7;
        uint64_t tmp0, tmp1, tmp2;

        uint64_t in0 = out_ptr[0];
        uint64_t in1 = out_ptr[width];
        uint64_t in2 = out_ptr[width * 2];
        uint64_t in3 = out_ptr[width * 3];
        uint64_t in4 = out_ptr[width * 4];
        uint64_t in5 = out_ptr[width * 5];
        uint64_t in6 = out_ptr[width * 6];
        uint64_t in7 = out_ptr[width * 7];

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
        out_ptr[width] = out1 ^ in_ptr[width];
        out_ptr[width * 2] = out2 ^ in_ptr[width * 2];
        out_ptr[width * 3] = out3 ^ in_ptr[width * 3];
        out_ptr[width * 4] = out4 ^ in_ptr[width * 4];
        out_ptr[width * 5] = out5 ^ in_ptr[width * 5];
        out_ptr[width * 6] = out6 ^ in_ptr[width * 6];
        out_ptr[width * 7] = out7 ^ in_ptr[width * 7];

        in_ptr++;
        out_ptr++;
    }
}

static void gf8_muladd_BE(uint8_t * out, uint8_t * in, unsigned int width)
{
    unsigned int i;
    uint64_t * in_ptr = (uint64_t *)in;
    uint64_t * out_ptr = (uint64_t *)out;

    for (i = 0; i < width; i++)
    {
        uint64_t out0, out1, out2, out3, out4, out5, out6, out7;
        uint64_t tmp0;

        uint64_t in0 = out_ptr[0];
        uint64_t in1 = out_ptr[width];
        uint64_t in2 = out_ptr[width * 2];
        uint64_t in3 = out_ptr[width * 3];
        uint64_t in4 = out_ptr[width * 4];
        uint64_t in5 = out_ptr[width * 5];
        uint64_t in6 = out_ptr[width * 6];
        uint64_t in7 = out_ptr[width * 7];

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
        out_ptr[width] = out1 ^ in_ptr[width];
        out_ptr[width * 2] = out2 ^ in_ptr[width * 2];
        out_ptr[width * 3] = out3 ^ in_ptr[width * 3];
        out_ptr[width * 4] = out4 ^ in_ptr[width * 4];
        out_ptr[width * 5] = out5 ^ in_ptr[width * 5];
        out_ptr[width * 6] = out6 ^ in_ptr[width * 6];
        out_ptr[width * 7] = out7 ^ in_ptr[width * 7];

        in_ptr++;
        out_ptr++;
    }
}

static void gf8_muladd_BF(uint8_t * out, uint8_t * in, unsigned int width)
{
    unsigned int i;
    uint64_t * in_ptr = (uint64_t *)in;
    uint64_t * out_ptr = (uint64_t *)out;

    for (i = 0; i < width; i++)
    {
        uint64_t out0, out1, out2, out3, out4, out5, out6, out7;
        uint64_t tmp0, tmp1, tmp2, tmp3;

        uint64_t in0 = out_ptr[0];
        uint64_t in1 = out_ptr[width];
        uint64_t in2 = out_ptr[width * 2];
        uint64_t in3 = out_ptr[width * 3];
        uint64_t in4 = out_ptr[width * 4];
        uint64_t in5 = out_ptr[width * 5];
        uint64_t in6 = out_ptr[width * 6];
        uint64_t in7 = out_ptr[width * 7];

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
        out_ptr[width] = out1 ^ in_ptr[width];
        out_ptr[width * 2] = out2 ^ in_ptr[width * 2];
        out_ptr[width * 3] = out3 ^ in_ptr[width * 3];
        out_ptr[width * 4] = out4 ^ in_ptr[width * 4];
        out_ptr[width * 5] = out5 ^ in_ptr[width * 5];
        out_ptr[width * 6] = out6 ^ in_ptr[width * 6];
        out_ptr[width * 7] = out7 ^ in_ptr[width * 7];

        in_ptr++;
        out_ptr++;
    }
}

static void gf8_muladd_C0(uint8_t * out, uint8_t * in, unsigned int width)
{
    unsigned int i;
    uint64_t * in_ptr = (uint64_t *)in;
    uint64_t * out_ptr = (uint64_t *)out;

    for (i = 0; i < width; i++)
    {
        uint64_t out0, out1, out2, out3, out4, out5, out6, out7;
        uint64_t tmp0, tmp1;

        uint64_t in0 = out_ptr[0];
        uint64_t in1 = out_ptr[width];
        uint64_t in2 = out_ptr[width * 2];
        uint64_t in3 = out_ptr[width * 3];
        uint64_t in4 = out_ptr[width * 4];
        uint64_t in5 = out_ptr[width * 5];
        uint64_t in6 = out_ptr[width * 6];
        uint64_t in7 = out_ptr[width * 7];

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
        out_ptr[width] = out1 ^ in_ptr[width];
        out_ptr[width * 2] = out2 ^ in_ptr[width * 2];
        out_ptr[width * 3] = out3 ^ in_ptr[width * 3];
        out_ptr[width * 4] = out4 ^ in_ptr[width * 4];
        out_ptr[width * 5] = out5 ^ in_ptr[width * 5];
        out_ptr[width * 6] = out6 ^ in_ptr[width * 6];
        out_ptr[width * 7] = out7 ^ in_ptr[width * 7];

        in_ptr++;
        out_ptr++;
    }
}

static void gf8_muladd_C1(uint8_t * out, uint8_t * in, unsigned int width)
{
    unsigned int i;
    uint64_t * in_ptr = (uint64_t *)in;
    uint64_t * out_ptr = (uint64_t *)out;

    for (i = 0; i < width; i++)
    {
        uint64_t out0, out1, out2, out3, out4, out5, out6, out7;
        uint64_t tmp0, tmp1;

        uint64_t in0 = out_ptr[0];
        uint64_t in1 = out_ptr[width];
        uint64_t in2 = out_ptr[width * 2];
        uint64_t in3 = out_ptr[width * 3];
        uint64_t in4 = out_ptr[width * 4];
        uint64_t in5 = out_ptr[width * 5];
        uint64_t in6 = out_ptr[width * 6];
        uint64_t in7 = out_ptr[width * 7];

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
        out_ptr[width] = out1 ^ in_ptr[width];
        out_ptr[width * 2] = out2 ^ in_ptr[width * 2];
        out_ptr[width * 3] = out3 ^ in_ptr[width * 3];
        out_ptr[width * 4] = out4 ^ in_ptr[width * 4];
        out_ptr[width * 5] = out5 ^ in_ptr[width * 5];
        out_ptr[width * 6] = out6 ^ in_ptr[width * 6];
        out_ptr[width * 7] = out7 ^ in_ptr[width * 7];

        in_ptr++;
        out_ptr++;
    }
}

static void gf8_muladd_C2(uint8_t * out, uint8_t * in, unsigned int width)
{
    unsigned int i;
    uint64_t * in_ptr = (uint64_t *)in;
    uint64_t * out_ptr = (uint64_t *)out;

    for (i = 0; i < width; i++)
    {
        uint64_t out0, out1, out2, out3, out4, out5, out6, out7;
        uint64_t tmp0, tmp1;

        uint64_t in0 = out_ptr[0];
        uint64_t in1 = out_ptr[width];
        uint64_t in2 = out_ptr[width * 2];
        uint64_t in3 = out_ptr[width * 3];
        uint64_t in4 = out_ptr[width * 4];
        uint64_t in5 = out_ptr[width * 5];
        uint64_t in6 = out_ptr[width * 6];
        uint64_t in7 = out_ptr[width * 7];

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
        out_ptr[width] = out1 ^ in_ptr[width];
        out_ptr[width * 2] = out2 ^ in_ptr[width * 2];
        out_ptr[width * 3] = out3 ^ in_ptr[width * 3];
        out_ptr[width * 4] = out4 ^ in_ptr[width * 4];
        out_ptr[width * 5] = out5 ^ in_ptr[width * 5];
        out_ptr[width * 6] = out6 ^ in_ptr[width * 6];
        out_ptr[width * 7] = out7 ^ in_ptr[width * 7];

        in_ptr++;
        out_ptr++;
    }
}

static void gf8_muladd_C3(uint8_t * out, uint8_t * in, unsigned int width)
{
    unsigned int i;
    uint64_t * in_ptr = (uint64_t *)in;
    uint64_t * out_ptr = (uint64_t *)out;

    for (i = 0; i < width; i++)
    {
        uint64_t out0, out1, out2, out3, out4, out5, out6, out7;
        uint64_t tmp0, tmp1, tmp2;

        uint64_t in0 = out_ptr[0];
        uint64_t in1 = out_ptr[width];
        uint64_t in2 = out_ptr[width * 2];
        uint64_t in3 = out_ptr[width * 3];
        uint64_t in4 = out_ptr[width * 4];
        uint64_t in5 = out_ptr[width * 5];
        uint64_t in6 = out_ptr[width * 6];
        uint64_t in7 = out_ptr[width * 7];

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
        out_ptr[width] = out1 ^ in_ptr[width];
        out_ptr[width * 2] = out2 ^ in_ptr[width * 2];
        out_ptr[width * 3] = out3 ^ in_ptr[width * 3];
        out_ptr[width * 4] = out4 ^ in_ptr[width * 4];
        out_ptr[width * 5] = out5 ^ in_ptr[width * 5];
        out_ptr[width * 6] = out6 ^ in_ptr[width * 6];
        out_ptr[width * 7] = out7 ^ in_ptr[width * 7];

        in_ptr++;
        out_ptr++;
    }
}

static void gf8_muladd_C4(uint8_t * out, uint8_t * in, unsigned int width)
{
    unsigned int i;
    uint64_t * in_ptr = (uint64_t *)in;
    uint64_t * out_ptr = (uint64_t *)out;

    for (i = 0; i < width; i++)
    {
        uint64_t out0, out1, out2, out3, out4, out5, out6, out7;
        uint64_t tmp0, tmp1;

        uint64_t in0 = out_ptr[0];
        uint64_t in1 = out_ptr[width];
        uint64_t in2 = out_ptr[width * 2];
        uint64_t in3 = out_ptr[width * 3];
        uint64_t in4 = out_ptr[width * 4];
        uint64_t in5 = out_ptr[width * 5];
        uint64_t in6 = out_ptr[width * 6];
        uint64_t in7 = out_ptr[width * 7];

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
        out_ptr[width] = out1 ^ in_ptr[width];
        out_ptr[width * 2] = out2 ^ in_ptr[width * 2];
        out_ptr[width * 3] = out3 ^ in_ptr[width * 3];
        out_ptr[width * 4] = out4 ^ in_ptr[width * 4];
        out_ptr[width * 5] = out5 ^ in_ptr[width * 5];
        out_ptr[width * 6] = out6 ^ in_ptr[width * 6];
        out_ptr[width * 7] = out7 ^ in_ptr[width * 7];

        in_ptr++;
        out_ptr++;
    }
}

static void gf8_muladd_C5(uint8_t * out, uint8_t * in, unsigned int width)
{
    unsigned int i;
    uint64_t * in_ptr = (uint64_t *)in;
    uint64_t * out_ptr = (uint64_t *)out;

    for (i = 0; i < width; i++)
    {
        uint64_t out0, out1, out2, out3, out4, out5, out6, out7;
        uint64_t tmp0;

        uint64_t in0 = out_ptr[0];
        uint64_t in1 = out_ptr[width];
        uint64_t in2 = out_ptr[width * 2];
        uint64_t in3 = out_ptr[width * 3];
        uint64_t in4 = out_ptr[width * 4];
        uint64_t in5 = out_ptr[width * 5];
        uint64_t in6 = out_ptr[width * 6];
        uint64_t in7 = out_ptr[width * 7];

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
        out_ptr[width] = out1 ^ in_ptr[width];
        out_ptr[width * 2] = out2 ^ in_ptr[width * 2];
        out_ptr[width * 3] = out3 ^ in_ptr[width * 3];
        out_ptr[width * 4] = out4 ^ in_ptr[width * 4];
        out_ptr[width * 5] = out5 ^ in_ptr[width * 5];
        out_ptr[width * 6] = out6 ^ in_ptr[width * 6];
        out_ptr[width * 7] = out7 ^ in_ptr[width * 7];

        in_ptr++;
        out_ptr++;
    }
}

static void gf8_muladd_C6(uint8_t * out, uint8_t * in, unsigned int width)
{
    unsigned int i;
    uint64_t * in_ptr = (uint64_t *)in;
    uint64_t * out_ptr = (uint64_t *)out;

    for (i = 0; i < width; i++)
    {
        uint64_t out0, out1, out2, out3, out4, out5, out6, out7;
        uint64_t tmp0, tmp1, tmp2, tmp3, tmp4, tmp5;

        uint64_t in0 = out_ptr[0];
        uint64_t in1 = out_ptr[width];
        uint64_t in2 = out_ptr[width * 2];
        uint64_t in3 = out_ptr[width * 3];
        uint64_t in4 = out_ptr[width * 4];
        uint64_t in5 = out_ptr[width * 5];
        uint64_t in6 = out_ptr[width * 6];
        uint64_t in7 = out_ptr[width * 7];

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
        out_ptr[width] = out1 ^ in_ptr[width];
        out_ptr[width * 2] = out2 ^ in_ptr[width * 2];
        out_ptr[width * 3] = out3 ^ in_ptr[width * 3];
        out_ptr[width * 4] = out4 ^ in_ptr[width * 4];
        out_ptr[width * 5] = out5 ^ in_ptr[width * 5];
        out_ptr[width * 6] = out6 ^ in_ptr[width * 6];
        out_ptr[width * 7] = out7 ^ in_ptr[width * 7];

        in_ptr++;
        out_ptr++;
    }
}

static void gf8_muladd_C7(uint8_t * out, uint8_t * in, unsigned int width)
{
    unsigned int i;
    uint64_t * in_ptr = (uint64_t *)in;
    uint64_t * out_ptr = (uint64_t *)out;

    for (i = 0; i < width; i++)
    {
        uint64_t out0, out1, out2, out3, out4, out5, out6, out7;
        uint64_t tmp0, tmp1;

        uint64_t in0 = out_ptr[0];
        uint64_t in1 = out_ptr[width];
        uint64_t in2 = out_ptr[width * 2];
        uint64_t in3 = out_ptr[width * 3];
        uint64_t in4 = out_ptr[width * 4];
        uint64_t in5 = out_ptr[width * 5];
        uint64_t in6 = out_ptr[width * 6];
        uint64_t in7 = out_ptr[width * 7];

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
        out_ptr[width] = out1 ^ in_ptr[width];
        out_ptr[width * 2] = out2 ^ in_ptr[width * 2];
        out_ptr[width * 3] = out3 ^ in_ptr[width * 3];
        out_ptr[width * 4] = out4 ^ in_ptr[width * 4];
        out_ptr[width * 5] = out5 ^ in_ptr[width * 5];
        out_ptr[width * 6] = out6 ^ in_ptr[width * 6];
        out_ptr[width * 7] = out7 ^ in_ptr[width * 7];

        in_ptr++;
        out_ptr++;
    }
}

static void gf8_muladd_C8(uint8_t * out, uint8_t * in, unsigned int width)
{
    unsigned int i;
    uint64_t * in_ptr = (uint64_t *)in;
    uint64_t * out_ptr = (uint64_t *)out;

    for (i = 0; i < width; i++)
    {
        uint64_t out0, out1, out2, out3, out4, out5, out6, out7;
        uint64_t tmp0, tmp1;

        uint64_t in0 = out_ptr[0];
        uint64_t in1 = out_ptr[width];
        uint64_t in2 = out_ptr[width * 2];
        uint64_t in3 = out_ptr[width * 3];
        uint64_t in4 = out_ptr[width * 4];
        uint64_t in5 = out_ptr[width * 5];
        uint64_t in6 = out_ptr[width * 6];
        uint64_t in7 = out_ptr[width * 7];

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
        out_ptr[width] = out1 ^ in_ptr[width];
        out_ptr[width * 2] = out2 ^ in_ptr[width * 2];
        out_ptr[width * 3] = out3 ^ in_ptr[width * 3];
        out_ptr[width * 4] = out4 ^ in_ptr[width * 4];
        out_ptr[width * 5] = out5 ^ in_ptr[width * 5];
        out_ptr[width * 6] = out6 ^ in_ptr[width * 6];
        out_ptr[width * 7] = out7 ^ in_ptr[width * 7];

        in_ptr++;
        out_ptr++;
    }
}

static void gf8_muladd_C9(uint8_t * out, uint8_t * in, unsigned int width)
{
    unsigned int i;
    uint64_t * in_ptr = (uint64_t *)in;
    uint64_t * out_ptr = (uint64_t *)out;

    for (i = 0; i < width; i++)
    {
        uint64_t out0, out1, out2, out3, out4, out5, out6, out7;
        uint64_t tmp0;

        uint64_t in0 = out_ptr[0];
        uint64_t in1 = out_ptr[width];
        uint64_t in2 = out_ptr[width * 2];
        uint64_t in3 = out_ptr[width * 3];
        uint64_t in4 = out_ptr[width * 4];
        uint64_t in5 = out_ptr[width * 5];
        uint64_t in6 = out_ptr[width * 6];
        uint64_t in7 = out_ptr[width * 7];

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
        out_ptr[width] = out1 ^ in_ptr[width];
        out_ptr[width * 2] = out2 ^ in_ptr[width * 2];
        out_ptr[width * 3] = out3 ^ in_ptr[width * 3];
        out_ptr[width * 4] = out4 ^ in_ptr[width * 4];
        out_ptr[width * 5] = out5 ^ in_ptr[width * 5];
        out_ptr[width * 6] = out6 ^ in_ptr[width * 6];
        out_ptr[width * 7] = out7 ^ in_ptr[width * 7];

        in_ptr++;
        out_ptr++;
    }
}

static void gf8_muladd_CA(uint8_t * out, uint8_t * in, unsigned int width)
{
    unsigned int i;
    uint64_t * in_ptr = (uint64_t *)in;
    uint64_t * out_ptr = (uint64_t *)out;

    for (i = 0; i < width; i++)
    {
        uint64_t out0, out1, out2, out3, out4, out5, out6, out7;
        uint64_t tmp0, tmp1, tmp2, tmp3;

        uint64_t in0 = out_ptr[0];
        uint64_t in1 = out_ptr[width];
        uint64_t in2 = out_ptr[width * 2];
        uint64_t in3 = out_ptr[width * 3];
        uint64_t in4 = out_ptr[width * 4];
        uint64_t in5 = out_ptr[width * 5];
        uint64_t in6 = out_ptr[width * 6];
        uint64_t in7 = out_ptr[width * 7];

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
        out_ptr[width] = out1 ^ in_ptr[width];
        out_ptr[width * 2] = out2 ^ in_ptr[width * 2];
        out_ptr[width * 3] = out3 ^ in_ptr[width * 3];
        out_ptr[width * 4] = out4 ^ in_ptr[width * 4];
        out_ptr[width * 5] = out5 ^ in_ptr[width * 5];
        out_ptr[width * 6] = out6 ^ in_ptr[width * 6];
        out_ptr[width * 7] = out7 ^ in_ptr[width * 7];

        in_ptr++;
        out_ptr++;
    }
}

static void gf8_muladd_CB(uint8_t * out, uint8_t * in, unsigned int width)
{
    unsigned int i;
    uint64_t * in_ptr = (uint64_t *)in;
    uint64_t * out_ptr = (uint64_t *)out;

    for (i = 0; i < width; i++)
    {
        uint64_t out0, out1, out2, out3, out4, out5, out6, out7;
        uint64_t tmp0, tmp1, tmp2;

        uint64_t in0 = out_ptr[0];
        uint64_t in1 = out_ptr[width];
        uint64_t in2 = out_ptr[width * 2];
        uint64_t in3 = out_ptr[width * 3];
        uint64_t in4 = out_ptr[width * 4];
        uint64_t in5 = out_ptr[width * 5];
        uint64_t in6 = out_ptr[width * 6];
        uint64_t in7 = out_ptr[width * 7];

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
        out_ptr[width] = out1 ^ in_ptr[width];
        out_ptr[width * 2] = out2 ^ in_ptr[width * 2];
        out_ptr[width * 3] = out3 ^ in_ptr[width * 3];
        out_ptr[width * 4] = out4 ^ in_ptr[width * 4];
        out_ptr[width * 5] = out5 ^ in_ptr[width * 5];
        out_ptr[width * 6] = out6 ^ in_ptr[width * 6];
        out_ptr[width * 7] = out7 ^ in_ptr[width * 7];

        in_ptr++;
        out_ptr++;
    }
}

static void gf8_muladd_CC(uint8_t * out, uint8_t * in, unsigned int width)
{
    unsigned int i;
    uint64_t * in_ptr = (uint64_t *)in;
    uint64_t * out_ptr = (uint64_t *)out;

    for (i = 0; i < width; i++)
    {
        uint64_t out0, out1, out2, out3, out4, out5, out6, out7;
        uint64_t tmp0, tmp1, tmp2, tmp3;

        uint64_t in0 = out_ptr[0];
        uint64_t in1 = out_ptr[width];
        uint64_t in2 = out_ptr[width * 2];
        uint64_t in3 = out_ptr[width * 3];
        uint64_t in4 = out_ptr[width * 4];
        uint64_t in5 = out_ptr[width * 5];
        uint64_t in6 = out_ptr[width * 6];
        uint64_t in7 = out_ptr[width * 7];

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
        out_ptr[width] = out1 ^ in_ptr[width];
        out_ptr[width * 2] = out2 ^ in_ptr[width * 2];
        out_ptr[width * 3] = out3 ^ in_ptr[width * 3];
        out_ptr[width * 4] = out4 ^ in_ptr[width * 4];
        out_ptr[width * 5] = out5 ^ in_ptr[width * 5];
        out_ptr[width * 6] = out6 ^ in_ptr[width * 6];
        out_ptr[width * 7] = out7 ^ in_ptr[width * 7];

        in_ptr++;
        out_ptr++;
    }
}

static void gf8_muladd_CD(uint8_t * out, uint8_t * in, unsigned int width)
{
    unsigned int i;
    uint64_t * in_ptr = (uint64_t *)in;
    uint64_t * out_ptr = (uint64_t *)out;

    for (i = 0; i < width; i++)
    {
        uint64_t out0, out1, out2, out3, out4, out5, out6, out7;
        uint64_t tmp0, tmp1;

        uint64_t in0 = out_ptr[0];
        uint64_t in1 = out_ptr[width];
        uint64_t in2 = out_ptr[width * 2];
        uint64_t in3 = out_ptr[width * 3];
        uint64_t in4 = out_ptr[width * 4];
        uint64_t in5 = out_ptr[width * 5];
        uint64_t in6 = out_ptr[width * 6];
        uint64_t in7 = out_ptr[width * 7];

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
        out_ptr[width] = out1 ^ in_ptr[width];
        out_ptr[width * 2] = out2 ^ in_ptr[width * 2];
        out_ptr[width * 3] = out3 ^ in_ptr[width * 3];
        out_ptr[width * 4] = out4 ^ in_ptr[width * 4];
        out_ptr[width * 5] = out5 ^ in_ptr[width * 5];
        out_ptr[width * 6] = out6 ^ in_ptr[width * 6];
        out_ptr[width * 7] = out7 ^ in_ptr[width * 7];

        in_ptr++;
        out_ptr++;
    }
}

static void gf8_muladd_CE(uint8_t * out, uint8_t * in, unsigned int width)
{
    unsigned int i;
    uint64_t * in_ptr = (uint64_t *)in;
    uint64_t * out_ptr = (uint64_t *)out;

    for (i = 0; i < width; i++)
    {
        uint64_t out0, out1, out2, out3, out4, out5, out6, out7;
        uint64_t tmp0, tmp1, tmp2;

        uint64_t in0 = out_ptr[0];
        uint64_t in1 = out_ptr[width];
        uint64_t in2 = out_ptr[width * 2];
        uint64_t in3 = out_ptr[width * 3];
        uint64_t in4 = out_ptr[width * 4];
        uint64_t in5 = out_ptr[width * 5];
        uint64_t in6 = out_ptr[width * 6];
        uint64_t in7 = out_ptr[width * 7];

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
        out_ptr[width] = out1 ^ in_ptr[width];
        out_ptr[width * 2] = out2 ^ in_ptr[width * 2];
        out_ptr[width * 3] = out3 ^ in_ptr[width * 3];
        out_ptr[width * 4] = out4 ^ in_ptr[width * 4];
        out_ptr[width * 5] = out5 ^ in_ptr[width * 5];
        out_ptr[width * 6] = out6 ^ in_ptr[width * 6];
        out_ptr[width * 7] = out7 ^ in_ptr[width * 7];

        in_ptr++;
        out_ptr++;
    }
}

static void gf8_muladd_CF(uint8_t * out, uint8_t * in, unsigned int width)
{
    unsigned int i;
    uint64_t * in_ptr = (uint64_t *)in;
    uint64_t * out_ptr = (uint64_t *)out;

    for (i = 0; i < width; i++)
    {
        uint64_t out0, out1, out2, out3, out4, out5, out6, out7;
        uint64_t tmp0, tmp1, tmp2;

        uint64_t in0 = out_ptr[0];
        uint64_t in1 = out_ptr[width];
        uint64_t in2 = out_ptr[width * 2];
        uint64_t in3 = out_ptr[width * 3];
        uint64_t in4 = out_ptr[width * 4];
        uint64_t in5 = out_ptr[width * 5];
        uint64_t in6 = out_ptr[width * 6];
        uint64_t in7 = out_ptr[width * 7];

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
        out_ptr[width] = out1 ^ in_ptr[width];
        out_ptr[width * 2] = out2 ^ in_ptr[width * 2];
        out_ptr[width * 3] = out3 ^ in_ptr[width * 3];
        out_ptr[width * 4] = out4 ^ in_ptr[width * 4];
        out_ptr[width * 5] = out5 ^ in_ptr[width * 5];
        out_ptr[width * 6] = out6 ^ in_ptr[width * 6];
        out_ptr[width * 7] = out7 ^ in_ptr[width * 7];

        in_ptr++;
        out_ptr++;
    }
}

static void gf8_muladd_D0(uint8_t * out, uint8_t * in, unsigned int width)
{
    unsigned int i;
    uint64_t * in_ptr = (uint64_t *)in;
    uint64_t * out_ptr = (uint64_t *)out;

    for (i = 0; i < width; i++)
    {
        uint64_t out0, out1, out2, out3, out4, out5, out6, out7;
        uint64_t tmp0, tmp1, tmp2, tmp3, tmp4;

        uint64_t in0 = out_ptr[0];
        uint64_t in1 = out_ptr[width];
        uint64_t in2 = out_ptr[width * 2];
        uint64_t in3 = out_ptr[width * 3];
        uint64_t in4 = out_ptr[width * 4];
        uint64_t in5 = out_ptr[width * 5];
        uint64_t in6 = out_ptr[width * 6];
        uint64_t in7 = out_ptr[width * 7];

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
        out_ptr[width] = out1 ^ in_ptr[width];
        out_ptr[width * 2] = out2 ^ in_ptr[width * 2];
        out_ptr[width * 3] = out3 ^ in_ptr[width * 3];
        out_ptr[width * 4] = out4 ^ in_ptr[width * 4];
        out_ptr[width * 5] = out5 ^ in_ptr[width * 5];
        out_ptr[width * 6] = out6 ^ in_ptr[width * 6];
        out_ptr[width * 7] = out7 ^ in_ptr[width * 7];

        in_ptr++;
        out_ptr++;
    }
}

static void gf8_muladd_D1(uint8_t * out, uint8_t * in, unsigned int width)
{
    unsigned int i;
    uint64_t * in_ptr = (uint64_t *)in;
    uint64_t * out_ptr = (uint64_t *)out;

    for (i = 0; i < width; i++)
    {
        uint64_t out0, out1, out2, out3, out4, out5, out6, out7;
        uint64_t tmp0, tmp1, tmp2;

        uint64_t in0 = out_ptr[0];
        uint64_t in1 = out_ptr[width];
        uint64_t in2 = out_ptr[width * 2];
        uint64_t in3 = out_ptr[width * 3];
        uint64_t in4 = out_ptr[width * 4];
        uint64_t in5 = out_ptr[width * 5];
        uint64_t in6 = out_ptr[width * 6];
        uint64_t in7 = out_ptr[width * 7];

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
        out_ptr[width] = out1 ^ in_ptr[width];
        out_ptr[width * 2] = out2 ^ in_ptr[width * 2];
        out_ptr[width * 3] = out3 ^ in_ptr[width * 3];
        out_ptr[width * 4] = out4 ^ in_ptr[width * 4];
        out_ptr[width * 5] = out5 ^ in_ptr[width * 5];
        out_ptr[width * 6] = out6 ^ in_ptr[width * 6];
        out_ptr[width * 7] = out7 ^ in_ptr[width * 7];

        in_ptr++;
        out_ptr++;
    }
}

static void gf8_muladd_D2(uint8_t * out, uint8_t * in, unsigned int width)
{
    unsigned int i;
    uint64_t * in_ptr = (uint64_t *)in;
    uint64_t * out_ptr = (uint64_t *)out;

    for (i = 0; i < width; i++)
    {
        uint64_t out0, out1, out2, out3, out4, out5, out6, out7;
        uint64_t tmp0;

        uint64_t in0 = out_ptr[0];
        uint64_t in1 = out_ptr[width];
        uint64_t in2 = out_ptr[width * 2];
        uint64_t in3 = out_ptr[width * 3];
        uint64_t in4 = out_ptr[width * 4];
        uint64_t in5 = out_ptr[width * 5];
        uint64_t in6 = out_ptr[width * 6];
        uint64_t in7 = out_ptr[width * 7];

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
        out_ptr[width] = out1 ^ in_ptr[width];
        out_ptr[width * 2] = out2 ^ in_ptr[width * 2];
        out_ptr[width * 3] = out3 ^ in_ptr[width * 3];
        out_ptr[width * 4] = out4 ^ in_ptr[width * 4];
        out_ptr[width * 5] = out5 ^ in_ptr[width * 5];
        out_ptr[width * 6] = out6 ^ in_ptr[width * 6];
        out_ptr[width * 7] = out7 ^ in_ptr[width * 7];

        in_ptr++;
        out_ptr++;
    }
}

static void gf8_muladd_D3(uint8_t * out, uint8_t * in, unsigned int width)
{
    unsigned int i;
    uint64_t * in_ptr = (uint64_t *)in;
    uint64_t * out_ptr = (uint64_t *)out;

    for (i = 0; i < width; i++)
    {
        uint64_t out0, out1, out2, out3, out4, out5, out6, out7;
        uint64_t tmp0, tmp1, tmp2, tmp3;

        uint64_t in0 = out_ptr[0];
        uint64_t in1 = out_ptr[width];
        uint64_t in2 = out_ptr[width * 2];
        uint64_t in3 = out_ptr[width * 3];
        uint64_t in4 = out_ptr[width * 4];
        uint64_t in5 = out_ptr[width * 5];
        uint64_t in6 = out_ptr[width * 6];
        uint64_t in7 = out_ptr[width * 7];

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
        out_ptr[width] = out1 ^ in_ptr[width];
        out_ptr[width * 2] = out2 ^ in_ptr[width * 2];
        out_ptr[width * 3] = out3 ^ in_ptr[width * 3];
        out_ptr[width * 4] = out4 ^ in_ptr[width * 4];
        out_ptr[width * 5] = out5 ^ in_ptr[width * 5];
        out_ptr[width * 6] = out6 ^ in_ptr[width * 6];
        out_ptr[width * 7] = out7 ^ in_ptr[width * 7];

        in_ptr++;
        out_ptr++;
    }
}

static void gf8_muladd_D4(uint8_t * out, uint8_t * in, unsigned int width)
{
    unsigned int i;
    uint64_t * in_ptr = (uint64_t *)in;
    uint64_t * out_ptr = (uint64_t *)out;

    for (i = 0; i < width; i++)
    {
        uint64_t out0, out1, out2, out3, out4, out5, out6, out7;
        uint64_t tmp0, tmp1, tmp2;

        uint64_t in0 = out_ptr[0];
        uint64_t in1 = out_ptr[width];
        uint64_t in2 = out_ptr[width * 2];
        uint64_t in3 = out_ptr[width * 3];
        uint64_t in4 = out_ptr[width * 4];
        uint64_t in5 = out_ptr[width * 5];
        uint64_t in6 = out_ptr[width * 6];
        uint64_t in7 = out_ptr[width * 7];

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
        out_ptr[width] = out1 ^ in_ptr[width];
        out_ptr[width * 2] = out2 ^ in_ptr[width * 2];
        out_ptr[width * 3] = out3 ^ in_ptr[width * 3];
        out_ptr[width * 4] = out4 ^ in_ptr[width * 4];
        out_ptr[width * 5] = out5 ^ in_ptr[width * 5];
        out_ptr[width * 6] = out6 ^ in_ptr[width * 6];
        out_ptr[width * 7] = out7 ^ in_ptr[width * 7];

        in_ptr++;
        out_ptr++;
    }
}

static void gf8_muladd_D5(uint8_t * out, uint8_t * in, unsigned int width)
{
    unsigned int i;
    uint64_t * in_ptr = (uint64_t *)in;
    uint64_t * out_ptr = (uint64_t *)out;

    for (i = 0; i < width; i++)
    {
        uint64_t out0, out1, out2, out3, out4, out5, out6, out7;
        uint64_t tmp0, tmp1, tmp2;

        uint64_t in0 = out_ptr[0];
        uint64_t in1 = out_ptr[width];
        uint64_t in2 = out_ptr[width * 2];
        uint64_t in3 = out_ptr[width * 3];
        uint64_t in4 = out_ptr[width * 4];
        uint64_t in5 = out_ptr[width * 5];
        uint64_t in6 = out_ptr[width * 6];
        uint64_t in7 = out_ptr[width * 7];

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
        out_ptr[width] = out1 ^ in_ptr[width];
        out_ptr[width * 2] = out2 ^ in_ptr[width * 2];
        out_ptr[width * 3] = out3 ^ in_ptr[width * 3];
        out_ptr[width * 4] = out4 ^ in_ptr[width * 4];
        out_ptr[width * 5] = out5 ^ in_ptr[width * 5];
        out_ptr[width * 6] = out6 ^ in_ptr[width * 6];
        out_ptr[width * 7] = out7 ^ in_ptr[width * 7];

        in_ptr++;
        out_ptr++;
    }
}

static void gf8_muladd_D6(uint8_t * out, uint8_t * in, unsigned int width)
{
    unsigned int i;
    uint64_t * in_ptr = (uint64_t *)in;
    uint64_t * out_ptr = (uint64_t *)out;

    for (i = 0; i < width; i++)
    {
        uint64_t out0, out1, out2, out3, out4, out5, out6, out7;
        uint64_t tmp0, tmp1;

        uint64_t in0 = out_ptr[0];
        uint64_t in1 = out_ptr[width];
        uint64_t in2 = out_ptr[width * 2];
        uint64_t in3 = out_ptr[width * 3];
        uint64_t in4 = out_ptr[width * 4];
        uint64_t in5 = out_ptr[width * 5];
        uint64_t in6 = out_ptr[width * 6];
        uint64_t in7 = out_ptr[width * 7];

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
        out_ptr[width] = out1 ^ in_ptr[width];
        out_ptr[width * 2] = out2 ^ in_ptr[width * 2];
        out_ptr[width * 3] = out3 ^ in_ptr[width * 3];
        out_ptr[width * 4] = out4 ^ in_ptr[width * 4];
        out_ptr[width * 5] = out5 ^ in_ptr[width * 5];
        out_ptr[width * 6] = out6 ^ in_ptr[width * 6];
        out_ptr[width * 7] = out7 ^ in_ptr[width * 7];

        in_ptr++;
        out_ptr++;
    }
}

static void gf8_muladd_D7(uint8_t * out, uint8_t * in, unsigned int width)
{
    unsigned int i;
    uint64_t * in_ptr = (uint64_t *)in;
    uint64_t * out_ptr = (uint64_t *)out;

    for (i = 0; i < width; i++)
    {
        uint64_t out0, out1, out2, out3, out4, out5, out6, out7;
        uint64_t tmp0, tmp1, tmp2;

        uint64_t in0 = out_ptr[0];
        uint64_t in1 = out_ptr[width];
        uint64_t in2 = out_ptr[width * 2];
        uint64_t in3 = out_ptr[width * 3];
        uint64_t in4 = out_ptr[width * 4];
        uint64_t in5 = out_ptr[width * 5];
        uint64_t in6 = out_ptr[width * 6];
        uint64_t in7 = out_ptr[width * 7];

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
        out_ptr[width] = out1 ^ in_ptr[width];
        out_ptr[width * 2] = out2 ^ in_ptr[width * 2];
        out_ptr[width * 3] = out3 ^ in_ptr[width * 3];
        out_ptr[width * 4] = out4 ^ in_ptr[width * 4];
        out_ptr[width * 5] = out5 ^ in_ptr[width * 5];
        out_ptr[width * 6] = out6 ^ in_ptr[width * 6];
        out_ptr[width * 7] = out7 ^ in_ptr[width * 7];

        in_ptr++;
        out_ptr++;
    }
}

static void gf8_muladd_D8(uint8_t * out, uint8_t * in, unsigned int width)
{
    unsigned int i;
    uint64_t * in_ptr = (uint64_t *)in;
    uint64_t * out_ptr = (uint64_t *)out;

    for (i = 0; i < width; i++)
    {
        uint64_t out0, out1, out2, out3, out4, out5, out6, out7;
        uint64_t tmp0, tmp1;

        uint64_t in0 = out_ptr[0];
        uint64_t in1 = out_ptr[width];
        uint64_t in2 = out_ptr[width * 2];
        uint64_t in3 = out_ptr[width * 3];
        uint64_t in4 = out_ptr[width * 4];
        uint64_t in5 = out_ptr[width * 5];
        uint64_t in6 = out_ptr[width * 6];
        uint64_t in7 = out_ptr[width * 7];

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
        out_ptr[width] = out1 ^ in_ptr[width];
        out_ptr[width * 2] = out2 ^ in_ptr[width * 2];
        out_ptr[width * 3] = out3 ^ in_ptr[width * 3];
        out_ptr[width * 4] = out4 ^ in_ptr[width * 4];
        out_ptr[width * 5] = out5 ^ in_ptr[width * 5];
        out_ptr[width * 6] = out6 ^ in_ptr[width * 6];
        out_ptr[width * 7] = out7 ^ in_ptr[width * 7];

        in_ptr++;
        out_ptr++;
    }
}

static void gf8_muladd_D9(uint8_t * out, uint8_t * in, unsigned int width)
{
    unsigned int i;
    uint64_t * in_ptr = (uint64_t *)in;
    uint64_t * out_ptr = (uint64_t *)out;

    for (i = 0; i < width; i++)
    {
        uint64_t out0, out1, out2, out3, out4, out5, out6, out7;

        uint64_t in0 = out_ptr[0];
        uint64_t in1 = out_ptr[width];
        uint64_t in2 = out_ptr[width * 2];
        uint64_t in3 = out_ptr[width * 3];
        uint64_t in4 = out_ptr[width * 4];
        uint64_t in5 = out_ptr[width * 5];
        uint64_t in6 = out_ptr[width * 6];
        uint64_t in7 = out_ptr[width * 7];

        out4 = in0 ^ in4;
        out5 = in1 ^ in5;
        out2 = in1 ^ in3 ^ in6;
        out3 = in0 ^ in1 ^ in7;
        out6 = in0 ^ in2 ^ in6;
        out0 = out4 ^ in1 ^ in2;
        out1 = out5 ^ in2 ^ in3;
        out7 = out3 ^ in3;

        out_ptr[0] = out0 ^ in_ptr[0];
        out_ptr[width] = out1 ^ in_ptr[width];
        out_ptr[width * 2] = out2 ^ in_ptr[width * 2];
        out_ptr[width * 3] = out3 ^ in_ptr[width * 3];
        out_ptr[width * 4] = out4 ^ in_ptr[width * 4];
        out_ptr[width * 5] = out5 ^ in_ptr[width * 5];
        out_ptr[width * 6] = out6 ^ in_ptr[width * 6];
        out_ptr[width * 7] = out7 ^ in_ptr[width * 7];

        in_ptr++;
        out_ptr++;
    }
}

static void gf8_muladd_DA(uint8_t * out, uint8_t * in, unsigned int width)
{
    unsigned int i;
    uint64_t * in_ptr = (uint64_t *)in;
    uint64_t * out_ptr = (uint64_t *)out;

    for (i = 0; i < width; i++)
    {
        uint64_t out0, out1, out2, out3, out4, out5, out6, out7;
        uint64_t tmp0, tmp1;

        uint64_t in0 = out_ptr[0];
        uint64_t in1 = out_ptr[width];
        uint64_t in2 = out_ptr[width * 2];
        uint64_t in3 = out_ptr[width * 3];
        uint64_t in4 = out_ptr[width * 4];
        uint64_t in5 = out_ptr[width * 5];
        uint64_t in6 = out_ptr[width * 6];
        uint64_t in7 = out_ptr[width * 7];

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
        out_ptr[width] = out1 ^ in_ptr[width];
        out_ptr[width * 2] = out2 ^ in_ptr[width * 2];
        out_ptr[width * 3] = out3 ^ in_ptr[width * 3];
        out_ptr[width * 4] = out4 ^ in_ptr[width * 4];
        out_ptr[width * 5] = out5 ^ in_ptr[width * 5];
        out_ptr[width * 6] = out6 ^ in_ptr[width * 6];
        out_ptr[width * 7] = out7 ^ in_ptr[width * 7];

        in_ptr++;
        out_ptr++;
    }
}

static void gf8_muladd_DB(uint8_t * out, uint8_t * in, unsigned int width)
{
    unsigned int i;
    uint64_t * in_ptr = (uint64_t *)in;
    uint64_t * out_ptr = (uint64_t *)out;

    for (i = 0; i < width; i++)
    {
        uint64_t out0, out1, out2, out3, out4, out5, out6, out7;
        uint64_t tmp0, tmp1, tmp2, tmp3, tmp4;

        uint64_t in0 = out_ptr[0];
        uint64_t in1 = out_ptr[width];
        uint64_t in2 = out_ptr[width * 2];
        uint64_t in3 = out_ptr[width * 3];
        uint64_t in4 = out_ptr[width * 4];
        uint64_t in5 = out_ptr[width * 5];
        uint64_t in6 = out_ptr[width * 6];
        uint64_t in7 = out_ptr[width * 7];

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
        out_ptr[width] = out1 ^ in_ptr[width];
        out_ptr[width * 2] = out2 ^ in_ptr[width * 2];
        out_ptr[width * 3] = out3 ^ in_ptr[width * 3];
        out_ptr[width * 4] = out4 ^ in_ptr[width * 4];
        out_ptr[width * 5] = out5 ^ in_ptr[width * 5];
        out_ptr[width * 6] = out6 ^ in_ptr[width * 6];
        out_ptr[width * 7] = out7 ^ in_ptr[width * 7];

        in_ptr++;
        out_ptr++;
    }
}

static void gf8_muladd_DC(uint8_t * out, uint8_t * in, unsigned int width)
{
    unsigned int i;
    uint64_t * in_ptr = (uint64_t *)in;
    uint64_t * out_ptr = (uint64_t *)out;

    for (i = 0; i < width; i++)
    {
        uint64_t out0, out1, out2, out3, out4, out5, out6, out7;
        uint64_t tmp0, tmp1, tmp2, tmp3;

        uint64_t in0 = out_ptr[0];
        uint64_t in1 = out_ptr[width];
        uint64_t in2 = out_ptr[width * 2];
        uint64_t in3 = out_ptr[width * 3];
        uint64_t in4 = out_ptr[width * 4];
        uint64_t in5 = out_ptr[width * 5];
        uint64_t in6 = out_ptr[width * 6];
        uint64_t in7 = out_ptr[width * 7];

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
        out_ptr[width] = out1 ^ in_ptr[width];
        out_ptr[width * 2] = out2 ^ in_ptr[width * 2];
        out_ptr[width * 3] = out3 ^ in_ptr[width * 3];
        out_ptr[width * 4] = out4 ^ in_ptr[width * 4];
        out_ptr[width * 5] = out5 ^ in_ptr[width * 5];
        out_ptr[width * 6] = out6 ^ in_ptr[width * 6];
        out_ptr[width * 7] = out7 ^ in_ptr[width * 7];

        in_ptr++;
        out_ptr++;
    }
}

static void gf8_muladd_DD(uint8_t * out, uint8_t * in, unsigned int width)
{
    unsigned int i;
    uint64_t * in_ptr = (uint64_t *)in;
    uint64_t * out_ptr = (uint64_t *)out;

    for (i = 0; i < width; i++)
    {
        uint64_t out0, out1, out2, out3, out4, out5, out6, out7;

        uint64_t in0 = out_ptr[0];
        uint64_t in1 = out_ptr[width];
        uint64_t in2 = out_ptr[width * 2];
        uint64_t in3 = out_ptr[width * 3];
        uint64_t in4 = out_ptr[width * 4];
        uint64_t in5 = out_ptr[width * 5];
        uint64_t in6 = out_ptr[width * 6];
        uint64_t in7 = out_ptr[width * 7];

        out3 = in0 ^ in6;
        out2 = in0 ^ in1 ^ in3;
        out6 = out3 ^ in2 ^ in4;
        out7 = out2 ^ in5 ^ in7;
        out0 = out6 ^ in1;
        out4 = out6 ^ in7;
        out5 = out7 ^ in0;
        out1 = out5 ^ in2;

        out_ptr[0] = out0 ^ in_ptr[0];
        out_ptr[width] = out1 ^ in_ptr[width];
        out_ptr[width * 2] = out2 ^ in_ptr[width * 2];
        out_ptr[width * 3] = out3 ^ in_ptr[width * 3];
        out_ptr[width * 4] = out4 ^ in_ptr[width * 4];
        out_ptr[width * 5] = out5 ^ in_ptr[width * 5];
        out_ptr[width * 6] = out6 ^ in_ptr[width * 6];
        out_ptr[width * 7] = out7 ^ in_ptr[width * 7];

        in_ptr++;
        out_ptr++;
    }
}

static void gf8_muladd_DE(uint8_t * out, uint8_t * in, unsigned int width)
{
    unsigned int i;
    uint64_t * in_ptr = (uint64_t *)in;
    uint64_t * out_ptr = (uint64_t *)out;

    for (i = 0; i < width; i++)
    {
        uint64_t out0, out1, out2, out3, out4, out5, out6, out7;
        uint64_t tmp0, tmp1;

        uint64_t in0 = out_ptr[0];
        uint64_t in1 = out_ptr[width];
        uint64_t in2 = out_ptr[width * 2];
        uint64_t in3 = out_ptr[width * 3];
        uint64_t in4 = out_ptr[width * 4];
        uint64_t in5 = out_ptr[width * 5];
        uint64_t in6 = out_ptr[width * 6];
        uint64_t in7 = out_ptr[width * 7];

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
        out_ptr[width] = out1 ^ in_ptr[width];
        out_ptr[width * 2] = out2 ^ in_ptr[width * 2];
        out_ptr[width * 3] = out3 ^ in_ptr[width * 3];
        out_ptr[width * 4] = out4 ^ in_ptr[width * 4];
        out_ptr[width * 5] = out5 ^ in_ptr[width * 5];
        out_ptr[width * 6] = out6 ^ in_ptr[width * 6];
        out_ptr[width * 7] = out7 ^ in_ptr[width * 7];

        in_ptr++;
        out_ptr++;
    }
}

static void gf8_muladd_DF(uint8_t * out, uint8_t * in, unsigned int width)
{
    unsigned int i;
    uint64_t * in_ptr = (uint64_t *)in;
    uint64_t * out_ptr = (uint64_t *)out;

    for (i = 0; i < width; i++)
    {
        uint64_t out0, out1, out2, out3, out4, out5, out6, out7;
        uint64_t tmp0, tmp1, tmp2;

        uint64_t in0 = out_ptr[0];
        uint64_t in1 = out_ptr[width];
        uint64_t in2 = out_ptr[width * 2];
        uint64_t in3 = out_ptr[width * 3];
        uint64_t in4 = out_ptr[width * 4];
        uint64_t in5 = out_ptr[width * 5];
        uint64_t in6 = out_ptr[width * 6];
        uint64_t in7 = out_ptr[width * 7];

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
        out_ptr[width] = out1 ^ in_ptr[width];
        out_ptr[width * 2] = out2 ^ in_ptr[width * 2];
        out_ptr[width * 3] = out3 ^ in_ptr[width * 3];
        out_ptr[width * 4] = out4 ^ in_ptr[width * 4];
        out_ptr[width * 5] = out5 ^ in_ptr[width * 5];
        out_ptr[width * 6] = out6 ^ in_ptr[width * 6];
        out_ptr[width * 7] = out7 ^ in_ptr[width * 7];

        in_ptr++;
        out_ptr++;
    }
}

static void gf8_muladd_E0(uint8_t * out, uint8_t * in, unsigned int width)
{
    unsigned int i;
    uint64_t * in_ptr = (uint64_t *)in;
    uint64_t * out_ptr = (uint64_t *)out;

    for (i = 0; i < width; i++)
    {
        uint64_t out0, out1, out2, out3, out4, out5, out6, out7;
        uint64_t tmp0, tmp1;

        uint64_t in0 = out_ptr[0];
        uint64_t in1 = out_ptr[width];
        uint64_t in2 = out_ptr[width * 2];
        uint64_t in3 = out_ptr[width * 3];
        uint64_t in4 = out_ptr[width * 4];
        uint64_t in5 = out_ptr[width * 5];
        uint64_t in6 = out_ptr[width * 6];
        uint64_t in7 = out_ptr[width * 7];

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
        out_ptr[width] = out1 ^ in_ptr[width];
        out_ptr[width * 2] = out2 ^ in_ptr[width * 2];
        out_ptr[width * 3] = out3 ^ in_ptr[width * 3];
        out_ptr[width * 4] = out4 ^ in_ptr[width * 4];
        out_ptr[width * 5] = out5 ^ in_ptr[width * 5];
        out_ptr[width * 6] = out6 ^ in_ptr[width * 6];
        out_ptr[width * 7] = out7 ^ in_ptr[width * 7];

        in_ptr++;
        out_ptr++;
    }
}

static void gf8_muladd_E1(uint8_t * out, uint8_t * in, unsigned int width)
{
    unsigned int i;
    uint64_t * in_ptr = (uint64_t *)in;
    uint64_t * out_ptr = (uint64_t *)out;

    for (i = 0; i < width; i++)
    {
        uint64_t out0, out1, out2, out3, out4, out5, out6, out7;
        uint64_t tmp0, tmp1, tmp2, tmp3;

        uint64_t in0 = out_ptr[0];
        uint64_t in1 = out_ptr[width];
        uint64_t in2 = out_ptr[width * 2];
        uint64_t in3 = out_ptr[width * 3];
        uint64_t in4 = out_ptr[width * 4];
        uint64_t in5 = out_ptr[width * 5];
        uint64_t in6 = out_ptr[width * 6];
        uint64_t in7 = out_ptr[width * 7];

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
        out_ptr[width] = out1 ^ in_ptr[width];
        out_ptr[width * 2] = out2 ^ in_ptr[width * 2];
        out_ptr[width * 3] = out3 ^ in_ptr[width * 3];
        out_ptr[width * 4] = out4 ^ in_ptr[width * 4];
        out_ptr[width * 5] = out5 ^ in_ptr[width * 5];
        out_ptr[width * 6] = out6 ^ in_ptr[width * 6];
        out_ptr[width * 7] = out7 ^ in_ptr[width * 7];

        in_ptr++;
        out_ptr++;
    }
}

static void gf8_muladd_E2(uint8_t * out, uint8_t * in, unsigned int width)
{
    unsigned int i;
    uint64_t * in_ptr = (uint64_t *)in;
    uint64_t * out_ptr = (uint64_t *)out;

    for (i = 0; i < width; i++)
    {
        uint64_t out0, out1, out2, out3, out4, out5, out6, out7;

        uint64_t in0 = out_ptr[0];
        uint64_t in1 = out_ptr[width];
        uint64_t in2 = out_ptr[width * 2];
        uint64_t in3 = out_ptr[width * 3];
        uint64_t in4 = out_ptr[width * 4];
        uint64_t in5 = out_ptr[width * 5];
        uint64_t in6 = out_ptr[width * 6];
        uint64_t in7 = out_ptr[width * 7];

        out3 = in1 ^ in2;
        out4 = in1 ^ in5;
        out2 = in2 ^ in4 ^ in7;
        out5 = in0 ^ in2 ^ in6;
        out0 = out3 ^ in3 ^ in5;
        out7 = out3 ^ in0 ^ in4;
        out6 = out2 ^ out7 ^ in3;
        out1 = out5 ^ in3 ^ in4;

        out_ptr[0] = out0 ^ in_ptr[0];
        out_ptr[width] = out1 ^ in_ptr[width];
        out_ptr[width * 2] = out2 ^ in_ptr[width * 2];
        out_ptr[width * 3] = out3 ^ in_ptr[width * 3];
        out_ptr[width * 4] = out4 ^ in_ptr[width * 4];
        out_ptr[width * 5] = out5 ^ in_ptr[width * 5];
        out_ptr[width * 6] = out6 ^ in_ptr[width * 6];
        out_ptr[width * 7] = out7 ^ in_ptr[width * 7];

        in_ptr++;
        out_ptr++;
    }
}

static void gf8_muladd_E3(uint8_t * out, uint8_t * in, unsigned int width)
{
    unsigned int i;
    uint64_t * in_ptr = (uint64_t *)in;
    uint64_t * out_ptr = (uint64_t *)out;

    for (i = 0; i < width; i++)
    {
        uint64_t out0, out1, out2, out3, out4, out5, out6, out7;
        uint64_t tmp0, tmp1, tmp2, tmp3, tmp4;

        uint64_t in0 = out_ptr[0];
        uint64_t in1 = out_ptr[width];
        uint64_t in2 = out_ptr[width * 2];
        uint64_t in3 = out_ptr[width * 3];
        uint64_t in4 = out_ptr[width * 4];
        uint64_t in5 = out_ptr[width * 5];
        uint64_t in6 = out_ptr[width * 6];
        uint64_t in7 = out_ptr[width * 7];

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
        out_ptr[width] = out1 ^ in_ptr[width];
        out_ptr[width * 2] = out2 ^ in_ptr[width * 2];
        out_ptr[width * 3] = out3 ^ in_ptr[width * 3];
        out_ptr[width * 4] = out4 ^ in_ptr[width * 4];
        out_ptr[width * 5] = out5 ^ in_ptr[width * 5];
        out_ptr[width * 6] = out6 ^ in_ptr[width * 6];
        out_ptr[width * 7] = out7 ^ in_ptr[width * 7];

        in_ptr++;
        out_ptr++;
    }
}

static void gf8_muladd_E4(uint8_t * out, uint8_t * in, unsigned int width)
{
    unsigned int i;
    uint64_t * in_ptr = (uint64_t *)in;
    uint64_t * out_ptr = (uint64_t *)out;

    for (i = 0; i < width; i++)
    {
        uint64_t out0, out1, out2, out3, out4, out5, out6, out7;
        uint64_t tmp0, tmp1, tmp2;

        uint64_t in0 = out_ptr[0];
        uint64_t in1 = out_ptr[width];
        uint64_t in2 = out_ptr[width * 2];
        uint64_t in3 = out_ptr[width * 3];
        uint64_t in4 = out_ptr[width * 4];
        uint64_t in5 = out_ptr[width * 5];
        uint64_t in6 = out_ptr[width * 6];
        uint64_t in7 = out_ptr[width * 7];

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
        out_ptr[width] = out1 ^ in_ptr[width];
        out_ptr[width * 2] = out2 ^ in_ptr[width * 2];
        out_ptr[width * 3] = out3 ^ in_ptr[width * 3];
        out_ptr[width * 4] = out4 ^ in_ptr[width * 4];
        out_ptr[width * 5] = out5 ^ in_ptr[width * 5];
        out_ptr[width * 6] = out6 ^ in_ptr[width * 6];
        out_ptr[width * 7] = out7 ^ in_ptr[width * 7];

        in_ptr++;
        out_ptr++;
    }
}

static void gf8_muladd_E5(uint8_t * out, uint8_t * in, unsigned int width)
{
    unsigned int i;
    uint64_t * in_ptr = (uint64_t *)in;
    uint64_t * out_ptr = (uint64_t *)out;

    for (i = 0; i < width; i++)
    {
        uint64_t out0, out1, out2, out3, out4, out5, out6, out7;
        uint64_t tmp0, tmp1, tmp2;

        uint64_t in0 = out_ptr[0];
        uint64_t in1 = out_ptr[width];
        uint64_t in2 = out_ptr[width * 2];
        uint64_t in3 = out_ptr[width * 3];
        uint64_t in4 = out_ptr[width * 4];
        uint64_t in5 = out_ptr[width * 5];
        uint64_t in6 = out_ptr[width * 6];
        uint64_t in7 = out_ptr[width * 7];

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
        out_ptr[width] = out1 ^ in_ptr[width];
        out_ptr[width * 2] = out2 ^ in_ptr[width * 2];
        out_ptr[width * 3] = out3 ^ in_ptr[width * 3];
        out_ptr[width * 4] = out4 ^ in_ptr[width * 4];
        out_ptr[width * 5] = out5 ^ in_ptr[width * 5];
        out_ptr[width * 6] = out6 ^ in_ptr[width * 6];
        out_ptr[width * 7] = out7 ^ in_ptr[width * 7];

        in_ptr++;
        out_ptr++;
    }
}

static void gf8_muladd_E6(uint8_t * out, uint8_t * in, unsigned int width)
{
    unsigned int i;
    uint64_t * in_ptr = (uint64_t *)in;
    uint64_t * out_ptr = (uint64_t *)out;

    for (i = 0; i < width; i++)
    {
        uint64_t out0, out1, out2, out3, out4, out5, out6, out7;

        uint64_t in0 = out_ptr[0];
        uint64_t in1 = out_ptr[width];
        uint64_t in2 = out_ptr[width * 2];
        uint64_t in3 = out_ptr[width * 3];
        uint64_t in4 = out_ptr[width * 4];
        uint64_t in5 = out_ptr[width * 5];
        uint64_t in6 = out_ptr[width * 6];
        uint64_t in7 = out_ptr[width * 7];

        out3 = in2 ^ in6 ^ in7;
        out2 = out3 ^ in0 ^ in4;
        out4 = out3 ^ in1 ^ in5;
        out1 = out2 ^ in3;
        out7 = out2 ^ out4 ^ in2;
        out0 = out4 ^ in3 ^ in7;
        out5 = out1 ^ in4;
        out6 = out0 ^ out2 ^ in5;

        out_ptr[0] = out0 ^ in_ptr[0];
        out_ptr[width] = out1 ^ in_ptr[width];
        out_ptr[width * 2] = out2 ^ in_ptr[width * 2];
        out_ptr[width * 3] = out3 ^ in_ptr[width * 3];
        out_ptr[width * 4] = out4 ^ in_ptr[width * 4];
        out_ptr[width * 5] = out5 ^ in_ptr[width * 5];
        out_ptr[width * 6] = out6 ^ in_ptr[width * 6];
        out_ptr[width * 7] = out7 ^ in_ptr[width * 7];

        in_ptr++;
        out_ptr++;
    }
}

static void gf8_muladd_E7(uint8_t * out, uint8_t * in, unsigned int width)
{
    unsigned int i;
    uint64_t * in_ptr = (uint64_t *)in;
    uint64_t * out_ptr = (uint64_t *)out;

    for (i = 0; i < width; i++)
    {
        uint64_t out0, out1, out2, out3, out4, out5, out6, out7;
        uint64_t tmp0, tmp1, tmp2, tmp3, tmp4;

        uint64_t in0 = out_ptr[0];
        uint64_t in1 = out_ptr[width];
        uint64_t in2 = out_ptr[width * 2];
        uint64_t in3 = out_ptr[width * 3];
        uint64_t in4 = out_ptr[width * 4];
        uint64_t in5 = out_ptr[width * 5];
        uint64_t in6 = out_ptr[width * 6];
        uint64_t in7 = out_ptr[width * 7];

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
        out_ptr[width] = out1 ^ in_ptr[width];
        out_ptr[width * 2] = out2 ^ in_ptr[width * 2];
        out_ptr[width * 3] = out3 ^ in_ptr[width * 3];
        out_ptr[width * 4] = out4 ^ in_ptr[width * 4];
        out_ptr[width * 5] = out5 ^ in_ptr[width * 5];
        out_ptr[width * 6] = out6 ^ in_ptr[width * 6];
        out_ptr[width * 7] = out7 ^ in_ptr[width * 7];

        in_ptr++;
        out_ptr++;
    }
}

static void gf8_muladd_E8(uint8_t * out, uint8_t * in, unsigned int width)
{
    unsigned int i;
    uint64_t * in_ptr = (uint64_t *)in;
    uint64_t * out_ptr = (uint64_t *)out;

    for (i = 0; i < width; i++)
    {
        uint64_t out0, out1, out2, out3, out4, out5, out6, out7;
        uint64_t tmp0, tmp1, tmp2, tmp3;

        uint64_t in0 = out_ptr[0];
        uint64_t in1 = out_ptr[width];
        uint64_t in2 = out_ptr[width * 2];
        uint64_t in3 = out_ptr[width * 3];
        uint64_t in4 = out_ptr[width * 4];
        uint64_t in5 = out_ptr[width * 5];
        uint64_t in6 = out_ptr[width * 6];
        uint64_t in7 = out_ptr[width * 7];

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
        out_ptr[width] = out1 ^ in_ptr[width];
        out_ptr[width * 2] = out2 ^ in_ptr[width * 2];
        out_ptr[width * 3] = out3 ^ in_ptr[width * 3];
        out_ptr[width * 4] = out4 ^ in_ptr[width * 4];
        out_ptr[width * 5] = out5 ^ in_ptr[width * 5];
        out_ptr[width * 6] = out6 ^ in_ptr[width * 6];
        out_ptr[width * 7] = out7 ^ in_ptr[width * 7];

        in_ptr++;
        out_ptr++;
    }
}

static void gf8_muladd_E9(uint8_t * out, uint8_t * in, unsigned int width)
{
    unsigned int i;
    uint64_t * in_ptr = (uint64_t *)in;
    uint64_t * out_ptr = (uint64_t *)out;

    for (i = 0; i < width; i++)
    {
        uint64_t out0, out1, out2, out3, out4, out5, out6, out7;
        uint64_t tmp0, tmp1, tmp2;

        uint64_t in0 = out_ptr[0];
        uint64_t in1 = out_ptr[width];
        uint64_t in2 = out_ptr[width * 2];
        uint64_t in3 = out_ptr[width * 3];
        uint64_t in4 = out_ptr[width * 4];
        uint64_t in5 = out_ptr[width * 5];
        uint64_t in6 = out_ptr[width * 6];
        uint64_t in7 = out_ptr[width * 7];

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
        out_ptr[width] = out1 ^ in_ptr[width];
        out_ptr[width * 2] = out2 ^ in_ptr[width * 2];
        out_ptr[width * 3] = out3 ^ in_ptr[width * 3];
        out_ptr[width * 4] = out4 ^ in_ptr[width * 4];
        out_ptr[width * 5] = out5 ^ in_ptr[width * 5];
        out_ptr[width * 6] = out6 ^ in_ptr[width * 6];
        out_ptr[width * 7] = out7 ^ in_ptr[width * 7];

        in_ptr++;
        out_ptr++;
    }
}

static void gf8_muladd_EA(uint8_t * out, uint8_t * in, unsigned int width)
{
    unsigned int i;
    uint64_t * in_ptr = (uint64_t *)in;
    uint64_t * out_ptr = (uint64_t *)out;

    for (i = 0; i < width; i++)
    {
        uint64_t out0, out1, out2, out3, out4, out5, out6, out7;

        uint64_t in0 = out_ptr[0];
        uint64_t in1 = out_ptr[width];
        uint64_t in2 = out_ptr[width * 2];
        uint64_t in3 = out_ptr[width * 3];
        uint64_t in4 = out_ptr[width * 4];
        uint64_t in5 = out_ptr[width * 5];
        uint64_t in6 = out_ptr[width * 6];
        uint64_t in7 = out_ptr[width * 7];

        out4 = in6 ^ in7;
        out5 = in0 ^ in7;
        out6 = in0 ^ in1;
        out0 = in1 ^ in2 ^ in3;
        out2 = in2 ^ in4 ^ in5;
        out7 = out6 ^ in2;
        out1 = out0 ^ out6 ^ in4;
        out3 = out7 ^ in5 ^ in6;

        out_ptr[0] = out0 ^ in_ptr[0];
        out_ptr[width] = out1 ^ in_ptr[width];
        out_ptr[width * 2] = out2 ^ in_ptr[width * 2];
        out_ptr[width * 3] = out3 ^ in_ptr[width * 3];
        out_ptr[width * 4] = out4 ^ in_ptr[width * 4];
        out_ptr[width * 5] = out5 ^ in_ptr[width * 5];
        out_ptr[width * 6] = out6 ^ in_ptr[width * 6];
        out_ptr[width * 7] = out7 ^ in_ptr[width * 7];

        in_ptr++;
        out_ptr++;
    }
}

static void gf8_muladd_EB(uint8_t * out, uint8_t * in, unsigned int width)
{
    unsigned int i;
    uint64_t * in_ptr = (uint64_t *)in;
    uint64_t * out_ptr = (uint64_t *)out;

    for (i = 0; i < width; i++)
    {
        uint64_t out0, out1, out2, out3, out4, out5, out6, out7;
        uint64_t tmp0, tmp1;

        uint64_t in0 = out_ptr[0];
        uint64_t in1 = out_ptr[width];
        uint64_t in2 = out_ptr[width * 2];
        uint64_t in3 = out_ptr[width * 3];
        uint64_t in4 = out_ptr[width * 4];
        uint64_t in5 = out_ptr[width * 5];
        uint64_t in6 = out_ptr[width * 6];
        uint64_t in7 = out_ptr[width * 7];

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
        out_ptr[width] = out1 ^ in_ptr[width];
        out_ptr[width * 2] = out2 ^ in_ptr[width * 2];
        out_ptr[width * 3] = out3 ^ in_ptr[width * 3];
        out_ptr[width * 4] = out4 ^ in_ptr[width * 4];
        out_ptr[width * 5] = out5 ^ in_ptr[width * 5];
        out_ptr[width * 6] = out6 ^ in_ptr[width * 6];
        out_ptr[width * 7] = out7 ^ in_ptr[width * 7];

        in_ptr++;
        out_ptr++;
    }
}

static void gf8_muladd_EC(uint8_t * out, uint8_t * in, unsigned int width)
{
    unsigned int i;
    uint64_t * in_ptr = (uint64_t *)in;
    uint64_t * out_ptr = (uint64_t *)out;

    for (i = 0; i < width; i++)
    {
        uint64_t out0, out1, out2, out3, out4, out5, out6, out7;

        uint64_t in0 = out_ptr[0];
        uint64_t in1 = out_ptr[width];
        uint64_t in2 = out_ptr[width * 2];
        uint64_t in3 = out_ptr[width * 3];
        uint64_t in4 = out_ptr[width * 4];
        uint64_t in5 = out_ptr[width * 5];
        uint64_t in6 = out_ptr[width * 6];
        uint64_t in7 = out_ptr[width * 7];

        out3 = in0 ^ in5;
        out4 = in2 ^ in3 ^ in7;
        out5 = in0 ^ in3 ^ in4;
        out6 = out3 ^ in1 ^ in4;
        out1 = out4 ^ in4;
        out0 = out4 ^ in1 ^ in6;
        out2 = out0 ^ out5 ^ in5;
        out7 = out2 ^ in4 ^ in7;

        out_ptr[0] = out0 ^ in_ptr[0];
        out_ptr[width] = out1 ^ in_ptr[width];
        out_ptr[width * 2] = out2 ^ in_ptr[width * 2];
        out_ptr[width * 3] = out3 ^ in_ptr[width * 3];
        out_ptr[width * 4] = out4 ^ in_ptr[width * 4];
        out_ptr[width * 5] = out5 ^ in_ptr[width * 5];
        out_ptr[width * 6] = out6 ^ in_ptr[width * 6];
        out_ptr[width * 7] = out7 ^ in_ptr[width * 7];

        in_ptr++;
        out_ptr++;
    }
}

static void gf8_muladd_ED(uint8_t * out, uint8_t * in, unsigned int width)
{
    unsigned int i;
    uint64_t * in_ptr = (uint64_t *)in;
    uint64_t * out_ptr = (uint64_t *)out;

    for (i = 0; i < width; i++)
    {
        uint64_t out0, out1, out2, out3, out4, out5, out6, out7;
        uint64_t tmp0, tmp1;

        uint64_t in0 = out_ptr[0];
        uint64_t in1 = out_ptr[width];
        uint64_t in2 = out_ptr[width * 2];
        uint64_t in3 = out_ptr[width * 3];
        uint64_t in4 = out_ptr[width * 4];
        uint64_t in5 = out_ptr[width * 5];
        uint64_t in6 = out_ptr[width * 6];
        uint64_t in7 = out_ptr[width * 7];

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
        out_ptr[width] = out1 ^ in_ptr[width];
        out_ptr[width * 2] = out2 ^ in_ptr[width * 2];
        out_ptr[width * 3] = out3 ^ in_ptr[width * 3];
        out_ptr[width * 4] = out4 ^ in_ptr[width * 4];
        out_ptr[width * 5] = out5 ^ in_ptr[width * 5];
        out_ptr[width * 6] = out6 ^ in_ptr[width * 6];
        out_ptr[width * 7] = out7 ^ in_ptr[width * 7];

        in_ptr++;
        out_ptr++;
    }
}

static void gf8_muladd_EE(uint8_t * out, uint8_t * in, unsigned int width)
{
    unsigned int i;
    uint64_t * in_ptr = (uint64_t *)in;
    uint64_t * out_ptr = (uint64_t *)out;

    for (i = 0; i < width; i++)
    {
        uint64_t out0, out1, out2, out3, out4, out5, out6, out7;
        uint64_t tmp0, tmp1, tmp2, tmp3;

        uint64_t in0 = out_ptr[0];
        uint64_t in1 = out_ptr[width];
        uint64_t in2 = out_ptr[width * 2];
        uint64_t in3 = out_ptr[width * 3];
        uint64_t in4 = out_ptr[width * 4];
        uint64_t in5 = out_ptr[width * 5];
        uint64_t in6 = out_ptr[width * 6];
        uint64_t in7 = out_ptr[width * 7];

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
        out_ptr[width] = out1 ^ in_ptr[width];
        out_ptr[width * 2] = out2 ^ in_ptr[width * 2];
        out_ptr[width * 3] = out3 ^ in_ptr[width * 3];
        out_ptr[width * 4] = out4 ^ in_ptr[width * 4];
        out_ptr[width * 5] = out5 ^ in_ptr[width * 5];
        out_ptr[width * 6] = out6 ^ in_ptr[width * 6];
        out_ptr[width * 7] = out7 ^ in_ptr[width * 7];

        in_ptr++;
        out_ptr++;
    }
}

static void gf8_muladd_EF(uint8_t * out, uint8_t * in, unsigned int width)
{
    unsigned int i;
    uint64_t * in_ptr = (uint64_t *)in;
    uint64_t * out_ptr = (uint64_t *)out;

    for (i = 0; i < width; i++)
    {
        uint64_t out0, out1, out2, out3, out4, out5, out6, out7;
        uint64_t tmp0, tmp1;

        uint64_t in0 = out_ptr[0];
        uint64_t in1 = out_ptr[width];
        uint64_t in2 = out_ptr[width * 2];
        uint64_t in3 = out_ptr[width * 3];
        uint64_t in4 = out_ptr[width * 4];
        uint64_t in5 = out_ptr[width * 5];
        uint64_t in6 = out_ptr[width * 6];
        uint64_t in7 = out_ptr[width * 7];

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
        out_ptr[width] = out1 ^ in_ptr[width];
        out_ptr[width * 2] = out2 ^ in_ptr[width * 2];
        out_ptr[width * 3] = out3 ^ in_ptr[width * 3];
        out_ptr[width * 4] = out4 ^ in_ptr[width * 4];
        out_ptr[width * 5] = out5 ^ in_ptr[width * 5];
        out_ptr[width * 6] = out6 ^ in_ptr[width * 6];
        out_ptr[width * 7] = out7 ^ in_ptr[width * 7];

        in_ptr++;
        out_ptr++;
    }
}

static void gf8_muladd_F0(uint8_t * out, uint8_t * in, unsigned int width)
{
    unsigned int i;
    uint64_t * in_ptr = (uint64_t *)in;
    uint64_t * out_ptr = (uint64_t *)out;

    for (i = 0; i < width; i++)
    {
        uint64_t out0, out1, out2, out3, out4, out5, out6, out7;
        uint64_t tmp0, tmp1, tmp2, tmp3;

        uint64_t in0 = out_ptr[0];
        uint64_t in1 = out_ptr[width];
        uint64_t in2 = out_ptr[width * 2];
        uint64_t in3 = out_ptr[width * 3];
        uint64_t in4 = out_ptr[width * 4];
        uint64_t in5 = out_ptr[width * 5];
        uint64_t in6 = out_ptr[width * 6];
        uint64_t in7 = out_ptr[width * 7];

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
        out_ptr[width] = out1 ^ in_ptr[width];
        out_ptr[width * 2] = out2 ^ in_ptr[width * 2];
        out_ptr[width * 3] = out3 ^ in_ptr[width * 3];
        out_ptr[width * 4] = out4 ^ in_ptr[width * 4];
        out_ptr[width * 5] = out5 ^ in_ptr[width * 5];
        out_ptr[width * 6] = out6 ^ in_ptr[width * 6];
        out_ptr[width * 7] = out7 ^ in_ptr[width * 7];

        in_ptr++;
        out_ptr++;
    }
}

static void gf8_muladd_F1(uint8_t * out, uint8_t * in, unsigned int width)
{
    unsigned int i;
    uint64_t * in_ptr = (uint64_t *)in;
    uint64_t * out_ptr = (uint64_t *)out;

    for (i = 0; i < width; i++)
    {
        uint64_t out0, out1, out2, out3, out4, out5, out6, out7;
        uint64_t tmp0, tmp1, tmp2, tmp3;

        uint64_t in0 = out_ptr[0];
        uint64_t in1 = out_ptr[width];
        uint64_t in2 = out_ptr[width * 2];
        uint64_t in3 = out_ptr[width * 3];
        uint64_t in4 = out_ptr[width * 4];
        uint64_t in5 = out_ptr[width * 5];
        uint64_t in6 = out_ptr[width * 6];
        uint64_t in7 = out_ptr[width * 7];

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
        out_ptr[width] = out1 ^ in_ptr[width];
        out_ptr[width * 2] = out2 ^ in_ptr[width * 2];
        out_ptr[width * 3] = out3 ^ in_ptr[width * 3];
        out_ptr[width * 4] = out4 ^ in_ptr[width * 4];
        out_ptr[width * 5] = out5 ^ in_ptr[width * 5];
        out_ptr[width * 6] = out6 ^ in_ptr[width * 6];
        out_ptr[width * 7] = out7 ^ in_ptr[width * 7];

        in_ptr++;
        out_ptr++;
    }
}

static void gf8_muladd_F2(uint8_t * out, uint8_t * in, unsigned int width)
{
    unsigned int i;
    uint64_t * in_ptr = (uint64_t *)in;
    uint64_t * out_ptr = (uint64_t *)out;

    for (i = 0; i < width; i++)
    {
        uint64_t out0, out1, out2, out3, out4, out5, out6, out7;
        uint64_t tmp0, tmp1, tmp2, tmp3;

        uint64_t in0 = out_ptr[0];
        uint64_t in1 = out_ptr[width];
        uint64_t in2 = out_ptr[width * 2];
        uint64_t in3 = out_ptr[width * 3];
        uint64_t in4 = out_ptr[width * 4];
        uint64_t in5 = out_ptr[width * 5];
        uint64_t in6 = out_ptr[width * 6];
        uint64_t in7 = out_ptr[width * 7];

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
        out_ptr[width] = out1 ^ in_ptr[width];
        out_ptr[width * 2] = out2 ^ in_ptr[width * 2];
        out_ptr[width * 3] = out3 ^ in_ptr[width * 3];
        out_ptr[width * 4] = out4 ^ in_ptr[width * 4];
        out_ptr[width * 5] = out5 ^ in_ptr[width * 5];
        out_ptr[width * 6] = out6 ^ in_ptr[width * 6];
        out_ptr[width * 7] = out7 ^ in_ptr[width * 7];

        in_ptr++;
        out_ptr++;
    }
}

static void gf8_muladd_F3(uint8_t * out, uint8_t * in, unsigned int width)
{
    unsigned int i;
    uint64_t * in_ptr = (uint64_t *)in;
    uint64_t * out_ptr = (uint64_t *)out;

    for (i = 0; i < width; i++)
    {
        uint64_t out0, out1, out2, out3, out4, out5, out6, out7;
        uint64_t tmp0, tmp1;

        uint64_t in0 = out_ptr[0];
        uint64_t in1 = out_ptr[width];
        uint64_t in2 = out_ptr[width * 2];
        uint64_t in3 = out_ptr[width * 3];
        uint64_t in4 = out_ptr[width * 4];
        uint64_t in5 = out_ptr[width * 5];
        uint64_t in6 = out_ptr[width * 6];
        uint64_t in7 = out_ptr[width * 7];

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
        out_ptr[width] = out1 ^ in_ptr[width];
        out_ptr[width * 2] = out2 ^ in_ptr[width * 2];
        out_ptr[width * 3] = out3 ^ in_ptr[width * 3];
        out_ptr[width * 4] = out4 ^ in_ptr[width * 4];
        out_ptr[width * 5] = out5 ^ in_ptr[width * 5];
        out_ptr[width * 6] = out6 ^ in_ptr[width * 6];
        out_ptr[width * 7] = out7 ^ in_ptr[width * 7];

        in_ptr++;
        out_ptr++;
    }
}

static void gf8_muladd_F4(uint8_t * out, uint8_t * in, unsigned int width)
{
    unsigned int i;
    uint64_t * in_ptr = (uint64_t *)in;
    uint64_t * out_ptr = (uint64_t *)out;

    for (i = 0; i < width; i++)
    {
        uint64_t out0, out1, out2, out3, out4, out5, out6, out7;
        uint64_t tmp0;

        uint64_t in0 = out_ptr[0];
        uint64_t in1 = out_ptr[width];
        uint64_t in2 = out_ptr[width * 2];
        uint64_t in3 = out_ptr[width * 3];
        uint64_t in4 = out_ptr[width * 4];
        uint64_t in5 = out_ptr[width * 5];
        uint64_t in6 = out_ptr[width * 6];
        uint64_t in7 = out_ptr[width * 7];

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
        out_ptr[width] = out1 ^ in_ptr[width];
        out_ptr[width * 2] = out2 ^ in_ptr[width * 2];
        out_ptr[width * 3] = out3 ^ in_ptr[width * 3];
        out_ptr[width * 4] = out4 ^ in_ptr[width * 4];
        out_ptr[width * 5] = out5 ^ in_ptr[width * 5];
        out_ptr[width * 6] = out6 ^ in_ptr[width * 6];
        out_ptr[width * 7] = out7 ^ in_ptr[width * 7];

        in_ptr++;
        out_ptr++;
    }
}

static void gf8_muladd_F5(uint8_t * out, uint8_t * in, unsigned int width)
{
    unsigned int i;
    uint64_t * in_ptr = (uint64_t *)in;
    uint64_t * out_ptr = (uint64_t *)out;

    for (i = 0; i < width; i++)
    {
        uint64_t out0, out1, out2, out3, out4, out5, out6, out7;
        uint64_t tmp0;

        uint64_t in0 = out_ptr[0];
        uint64_t in1 = out_ptr[width];
        uint64_t in2 = out_ptr[width * 2];
        uint64_t in3 = out_ptr[width * 3];
        uint64_t in4 = out_ptr[width * 4];
        uint64_t in5 = out_ptr[width * 5];
        uint64_t in6 = out_ptr[width * 6];
        uint64_t in7 = out_ptr[width * 7];

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
        out_ptr[width] = out1 ^ in_ptr[width];
        out_ptr[width * 2] = out2 ^ in_ptr[width * 2];
        out_ptr[width * 3] = out3 ^ in_ptr[width * 3];
        out_ptr[width * 4] = out4 ^ in_ptr[width * 4];
        out_ptr[width * 5] = out5 ^ in_ptr[width * 5];
        out_ptr[width * 6] = out6 ^ in_ptr[width * 6];
        out_ptr[width * 7] = out7 ^ in_ptr[width * 7];

        in_ptr++;
        out_ptr++;
    }
}

static void gf8_muladd_F6(uint8_t * out, uint8_t * in, unsigned int width)
{
    unsigned int i;
    uint64_t * in_ptr = (uint64_t *)in;
    uint64_t * out_ptr = (uint64_t *)out;

    for (i = 0; i < width; i++)
    {
        uint64_t out0, out1, out2, out3, out4, out5, out6, out7;
        uint64_t tmp0, tmp1;

        uint64_t in0 = out_ptr[0];
        uint64_t in1 = out_ptr[width];
        uint64_t in2 = out_ptr[width * 2];
        uint64_t in3 = out_ptr[width * 3];
        uint64_t in4 = out_ptr[width * 4];
        uint64_t in5 = out_ptr[width * 5];
        uint64_t in6 = out_ptr[width * 6];
        uint64_t in7 = out_ptr[width * 7];

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
        out_ptr[width] = out1 ^ in_ptr[width];
        out_ptr[width * 2] = out2 ^ in_ptr[width * 2];
        out_ptr[width * 3] = out3 ^ in_ptr[width * 3];
        out_ptr[width * 4] = out4 ^ in_ptr[width * 4];
        out_ptr[width * 5] = out5 ^ in_ptr[width * 5];
        out_ptr[width * 6] = out6 ^ in_ptr[width * 6];
        out_ptr[width * 7] = out7 ^ in_ptr[width * 7];

        in_ptr++;
        out_ptr++;
    }
}

static void gf8_muladd_F7(uint8_t * out, uint8_t * in, unsigned int width)
{
    unsigned int i;
    uint64_t * in_ptr = (uint64_t *)in;
    uint64_t * out_ptr = (uint64_t *)out;

    for (i = 0; i < width; i++)
    {
        uint64_t out0, out1, out2, out3, out4, out5, out6, out7;
        uint64_t tmp0;

        uint64_t in0 = out_ptr[0];
        uint64_t in1 = out_ptr[width];
        uint64_t in2 = out_ptr[width * 2];
        uint64_t in3 = out_ptr[width * 3];
        uint64_t in4 = out_ptr[width * 4];
        uint64_t in5 = out_ptr[width * 5];
        uint64_t in6 = out_ptr[width * 6];
        uint64_t in7 = out_ptr[width * 7];

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
        out_ptr[width] = out1 ^ in_ptr[width];
        out_ptr[width * 2] = out2 ^ in_ptr[width * 2];
        out_ptr[width * 3] = out3 ^ in_ptr[width * 3];
        out_ptr[width * 4] = out4 ^ in_ptr[width * 4];
        out_ptr[width * 5] = out5 ^ in_ptr[width * 5];
        out_ptr[width * 6] = out6 ^ in_ptr[width * 6];
        out_ptr[width * 7] = out7 ^ in_ptr[width * 7];

        in_ptr++;
        out_ptr++;
    }
}

static void gf8_muladd_F8(uint8_t * out, uint8_t * in, unsigned int width)
{
    unsigned int i;
    uint64_t * in_ptr = (uint64_t *)in;
    uint64_t * out_ptr = (uint64_t *)out;

    for (i = 0; i < width; i++)
    {
        uint64_t out0, out1, out2, out3, out4, out5, out6, out7;
        uint64_t tmp0, tmp1, tmp2;

        uint64_t in0 = out_ptr[0];
        uint64_t in1 = out_ptr[width];
        uint64_t in2 = out_ptr[width * 2];
        uint64_t in3 = out_ptr[width * 3];
        uint64_t in4 = out_ptr[width * 4];
        uint64_t in5 = out_ptr[width * 5];
        uint64_t in6 = out_ptr[width * 6];
        uint64_t in7 = out_ptr[width * 7];

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
        out_ptr[width] = out1 ^ in_ptr[width];
        out_ptr[width * 2] = out2 ^ in_ptr[width * 2];
        out_ptr[width * 3] = out3 ^ in_ptr[width * 3];
        out_ptr[width * 4] = out4 ^ in_ptr[width * 4];
        out_ptr[width * 5] = out5 ^ in_ptr[width * 5];
        out_ptr[width * 6] = out6 ^ in_ptr[width * 6];
        out_ptr[width * 7] = out7 ^ in_ptr[width * 7];

        in_ptr++;
        out_ptr++;
    }
}

static void gf8_muladd_F9(uint8_t * out, uint8_t * in, unsigned int width)
{
    unsigned int i;
    uint64_t * in_ptr = (uint64_t *)in;
    uint64_t * out_ptr = (uint64_t *)out;

    for (i = 0; i < width; i++)
    {
        uint64_t out0, out1, out2, out3, out4, out5, out6, out7;
        uint64_t tmp0, tmp1, tmp2, tmp3, tmp4;

        uint64_t in0 = out_ptr[0];
        uint64_t in1 = out_ptr[width];
        uint64_t in2 = out_ptr[width * 2];
        uint64_t in3 = out_ptr[width * 3];
        uint64_t in4 = out_ptr[width * 4];
        uint64_t in5 = out_ptr[width * 5];
        uint64_t in6 = out_ptr[width * 6];
        uint64_t in7 = out_ptr[width * 7];

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
        out_ptr[width] = out1 ^ in_ptr[width];
        out_ptr[width * 2] = out2 ^ in_ptr[width * 2];
        out_ptr[width * 3] = out3 ^ in_ptr[width * 3];
        out_ptr[width * 4] = out4 ^ in_ptr[width * 4];
        out_ptr[width * 5] = out5 ^ in_ptr[width * 5];
        out_ptr[width * 6] = out6 ^ in_ptr[width * 6];
        out_ptr[width * 7] = out7 ^ in_ptr[width * 7];

        in_ptr++;
        out_ptr++;
    }
}

static void gf8_muladd_FA(uint8_t * out, uint8_t * in, unsigned int width)
{
    unsigned int i;
    uint64_t * in_ptr = (uint64_t *)in;
    uint64_t * out_ptr = (uint64_t *)out;

    for (i = 0; i < width; i++)
    {
        uint64_t out0, out1, out2, out3, out4, out5, out6, out7;
        uint64_t tmp0, tmp1, tmp2, tmp3;

        uint64_t in0 = out_ptr[0];
        uint64_t in1 = out_ptr[width];
        uint64_t in2 = out_ptr[width * 2];
        uint64_t in3 = out_ptr[width * 3];
        uint64_t in4 = out_ptr[width * 4];
        uint64_t in5 = out_ptr[width * 5];
        uint64_t in6 = out_ptr[width * 6];
        uint64_t in7 = out_ptr[width * 7];

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
        out_ptr[width] = out1 ^ in_ptr[width];
        out_ptr[width * 2] = out2 ^ in_ptr[width * 2];
        out_ptr[width * 3] = out3 ^ in_ptr[width * 3];
        out_ptr[width * 4] = out4 ^ in_ptr[width * 4];
        out_ptr[width * 5] = out5 ^ in_ptr[width * 5];
        out_ptr[width * 6] = out6 ^ in_ptr[width * 6];
        out_ptr[width * 7] = out7 ^ in_ptr[width * 7];

        in_ptr++;
        out_ptr++;
    }
}

static void gf8_muladd_FB(uint8_t * out, uint8_t * in, unsigned int width)
{
    unsigned int i;
    uint64_t * in_ptr = (uint64_t *)in;
    uint64_t * out_ptr = (uint64_t *)out;

    for (i = 0; i < width; i++)
    {
        uint64_t out0, out1, out2, out3, out4, out5, out6, out7;
        uint64_t tmp0, tmp1;

        uint64_t in0 = out_ptr[0];
        uint64_t in1 = out_ptr[width];
        uint64_t in2 = out_ptr[width * 2];
        uint64_t in3 = out_ptr[width * 3];
        uint64_t in4 = out_ptr[width * 4];
        uint64_t in5 = out_ptr[width * 5];
        uint64_t in6 = out_ptr[width * 6];
        uint64_t in7 = out_ptr[width * 7];

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
        out_ptr[width] = out1 ^ in_ptr[width];
        out_ptr[width * 2] = out2 ^ in_ptr[width * 2];
        out_ptr[width * 3] = out3 ^ in_ptr[width * 3];
        out_ptr[width * 4] = out4 ^ in_ptr[width * 4];
        out_ptr[width * 5] = out5 ^ in_ptr[width * 5];
        out_ptr[width * 6] = out6 ^ in_ptr[width * 6];
        out_ptr[width * 7] = out7 ^ in_ptr[width * 7];

        in_ptr++;
        out_ptr++;
    }
}

static void gf8_muladd_FC(uint8_t * out, uint8_t * in, unsigned int width)
{
    unsigned int i;
    uint64_t * in_ptr = (uint64_t *)in;
    uint64_t * out_ptr = (uint64_t *)out;

    for (i = 0; i < width; i++)
    {
        uint64_t out0, out1, out2, out3, out4, out5, out6, out7;
        uint64_t tmp0, tmp1, tmp2, tmp3;

        uint64_t in0 = out_ptr[0];
        uint64_t in1 = out_ptr[width];
        uint64_t in2 = out_ptr[width * 2];
        uint64_t in3 = out_ptr[width * 3];
        uint64_t in4 = out_ptr[width * 4];
        uint64_t in5 = out_ptr[width * 5];
        uint64_t in6 = out_ptr[width * 6];
        uint64_t in7 = out_ptr[width * 7];

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
        out_ptr[width] = out1 ^ in_ptr[width];
        out_ptr[width * 2] = out2 ^ in_ptr[width * 2];
        out_ptr[width * 3] = out3 ^ in_ptr[width * 3];
        out_ptr[width * 4] = out4 ^ in_ptr[width * 4];
        out_ptr[width * 5] = out5 ^ in_ptr[width * 5];
        out_ptr[width * 6] = out6 ^ in_ptr[width * 6];
        out_ptr[width * 7] = out7 ^ in_ptr[width * 7];

        in_ptr++;
        out_ptr++;
    }
}

static void gf8_muladd_FD(uint8_t * out, uint8_t * in, unsigned int width)
{
    unsigned int i;
    uint64_t * in_ptr = (uint64_t *)in;
    uint64_t * out_ptr = (uint64_t *)out;

    for (i = 0; i < width; i++)
    {
        uint64_t out0, out1, out2, out3, out4, out5, out6, out7;
        uint64_t tmp0, tmp1, tmp2, tmp3;

        uint64_t in0 = out_ptr[0];
        uint64_t in1 = out_ptr[width];
        uint64_t in2 = out_ptr[width * 2];
        uint64_t in3 = out_ptr[width * 3];
        uint64_t in4 = out_ptr[width * 4];
        uint64_t in5 = out_ptr[width * 5];
        uint64_t in6 = out_ptr[width * 6];
        uint64_t in7 = out_ptr[width * 7];

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
        out_ptr[width] = out1 ^ in_ptr[width];
        out_ptr[width * 2] = out2 ^ in_ptr[width * 2];
        out_ptr[width * 3] = out3 ^ in_ptr[width * 3];
        out_ptr[width * 4] = out4 ^ in_ptr[width * 4];
        out_ptr[width * 5] = out5 ^ in_ptr[width * 5];
        out_ptr[width * 6] = out6 ^ in_ptr[width * 6];
        out_ptr[width * 7] = out7 ^ in_ptr[width * 7];

        in_ptr++;
        out_ptr++;
    }
}

static void gf8_muladd_FE(uint8_t * out, uint8_t * in, unsigned int width)
{
    unsigned int i;
    uint64_t * in_ptr = (uint64_t *)in;
    uint64_t * out_ptr = (uint64_t *)out;

    for (i = 0; i < width; i++)
    {
        uint64_t out0, out1, out2, out3, out4, out5, out6, out7;
        uint64_t tmp0, tmp1, tmp2, tmp3, tmp4;

        uint64_t in0 = out_ptr[0];
        uint64_t in1 = out_ptr[width];
        uint64_t in2 = out_ptr[width * 2];
        uint64_t in3 = out_ptr[width * 3];
        uint64_t in4 = out_ptr[width * 4];
        uint64_t in5 = out_ptr[width * 5];
        uint64_t in6 = out_ptr[width * 6];
        uint64_t in7 = out_ptr[width * 7];

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
        out_ptr[width] = out1 ^ in_ptr[width];
        out_ptr[width * 2] = out2 ^ in_ptr[width * 2];
        out_ptr[width * 3] = out3 ^ in_ptr[width * 3];
        out_ptr[width * 4] = out4 ^ in_ptr[width * 4];
        out_ptr[width * 5] = out5 ^ in_ptr[width * 5];
        out_ptr[width * 6] = out6 ^ in_ptr[width * 6];
        out_ptr[width * 7] = out7 ^ in_ptr[width * 7];

        in_ptr++;
        out_ptr++;
    }
}

static void gf8_muladd_FF(uint8_t * out, uint8_t * in, unsigned int width)
{
    unsigned int i;
    uint64_t * in_ptr = (uint64_t *)in;
    uint64_t * out_ptr = (uint64_t *)out;

    for (i = 0; i < width; i++)
    {
        uint64_t out0, out1, out2, out3, out4, out5, out6, out7;
        uint64_t tmp0, tmp1, tmp2, tmp3;

        uint64_t in0 = out_ptr[0];
        uint64_t in1 = out_ptr[width];
        uint64_t in2 = out_ptr[width * 2];
        uint64_t in3 = out_ptr[width * 3];
        uint64_t in4 = out_ptr[width * 4];
        uint64_t in5 = out_ptr[width * 5];
        uint64_t in6 = out_ptr[width * 6];
        uint64_t in7 = out_ptr[width * 7];

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
        out_ptr[width] = out1 ^ in_ptr[width];
        out_ptr[width * 2] = out2 ^ in_ptr[width * 2];
        out_ptr[width * 3] = out3 ^ in_ptr[width * 3];
        out_ptr[width * 4] = out4 ^ in_ptr[width * 4];
        out_ptr[width * 5] = out5 ^ in_ptr[width * 5];
        out_ptr[width * 6] = out6 ^ in_ptr[width * 6];
        out_ptr[width * 7] = out7 ^ in_ptr[width * 7];

        in_ptr++;
        out_ptr++;
    }
}

void (* ec_gf_muladd[])(uint8_t * out, uint8_t * in, unsigned int width) =
{
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
