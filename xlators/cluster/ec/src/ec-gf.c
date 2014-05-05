/*
  Copyright (c) 2012 DataLab, s.l. <http://www.datalab.es>

  This file is part of the cluster/ec translator for GlusterFS.

  The cluster/ec translator for GlusterFS is free software: you can
  redistribute it and/or modify it under the terms of the GNU General
  Public License as published by the Free Software Foundation, either
  version 3 of the License, or (at your option) any later version.

  The cluster/ec translator for GlusterFS is distributed in the hope
  that it will be useful, but WITHOUT ANY WARRANTY; without even the
  implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
  PURPOSE. See the GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with the cluster/ec translator for GlusterFS. If not, see
  <http://www.gnu.org/licenses/>.
*/

/*
 * File automatically generated on Thu Jan 26 12:08:19 2012
 *
 * DO NOT MODIFY
 *
 * Multiplications in a GF(2^8) with modulus 0x11D using XOR's
 *
 * 7994 total xor's
 * 31.3 average xor's per number
 * 0 xor's for the best case (01)
 * 43 xor's for the worst case (F4)
 *
 *  0 xor's: 01
 * 10 xor's: 03
 * 12 xor's: F5
 * 16 xor's: 04 05
 * 17 xor's: 9C A6
 * 18 xor's: 02 73
 * 19 xor's: 10 39
 * 20 xor's: 0B
 * 21 xor's: 0D 59 D2 E9 EC
 * 22 xor's: 12 28 61
 * 23 xor's: 08 09 44
 * 24 xor's: 0A 1D 25 55 B4
 * 25 xor's: 07 11 21 51 63 C4
 * 26 xor's: 0C 0F 13 45 54 5E 64 BD F2
 * 27 xor's: 06 1F 22 41 6B B9 C7 D1 F7
 * 28 xor's: 19 31 8C 95 B5 C1 F3
 * 29 xor's: 26 30 42 4A 4B 50 6A 88 90 A3 D8 E0 E8 F0 FD
 * 30 xor's: 14 15 20 2E 34 5D 89 99 A2 A9 B0 E5 F9
 * 31 xor's: 16 17 18 1A 1B 24 29 2B 2D 3B 57 84 85 87 8F 97 A5 EB F1 FB
 * 32 xor's: 33 36 43 47 65 67 72 75 78 79 81 83 8D 9B A8 AF B8 BB C5 CB CC CE E6 ED
 * 33 xor's: 0E 35 3D 49 4C 4D 6E 70 94 98 A0 AB B1 B2 B6 C8 C9 CD D0 D6 DC DD E3 EA F8
 * 34 xor's: 1C 1E 23 27 2C 32 40 46 5C 60 68 6F 71 7F 8A 9A AA AC B3 C2 D3 FC FF
 * 35 xor's: 3A 53 58 6D 74 7C 7D 8B 91 93 96 A1 AE C0 CA D5 DB E4 F6
 * 36 xor's: 2A 2F 38 48 4F 5B 66 6C 82 86 92 9F AD BC CF D4 DA DE E2 FA FE
 * 37 xor's: 37 3E 52 69 7B 9D B7 BE C3 C6 EE
 * 38 xor's: 3C 5A 7E 80 9E A7 BA BF D7 E7 EF
 * 39 xor's: 3F 4E 77 8E A4 D9 E1
 * 40 xor's: 76 7A
 * 41 xor's: 62
 * 42 xor's: 56 5F DF
 * 43 xor's: F4
 *
 */

#include <xmmintrin.h>

#include "ec-gf.h"

static void gf8mul_00000000(void)
{
    __asm__ __volatile__
    (
        "\tpxor    %xmm0, %xmm0\n"
        "\tpxor    %xmm1, %xmm1\n"
        "\tpxor    %xmm2, %xmm2\n"
        "\tpxor    %xmm3, %xmm3\n"
        "\tpxor    %xmm4, %xmm4\n"
        "\tpxor    %xmm5, %xmm5\n"
        "\tpxor    %xmm6, %xmm6\n"
        "\tpxor    %xmm7, %xmm7\n"
    );
}

static void gf8mul_00000001(void)
{
}

static void gf8mul_00000010(void)
{
    __asm__ __volatile__
    (
        "\tpxor    %xmm7, %xmm6\n"
        "\tpxor    %xmm7, %xmm5\n"
        "\tpxor    %xmm7, %xmm4\n"
        "\tpxor    %xmm7, %xmm0\n"
        "\tpxor    %xmm6, %xmm7\n"
        "\tpxor    %xmm7, %xmm6\n"
        "\tpxor    %xmm5, %xmm6\n"
        "\tpxor    %xmm6, %xmm5\n"
        "\tpxor    %xmm4, %xmm5\n"
        "\tpxor    %xmm5, %xmm4\n"
        "\tpxor    %xmm3, %xmm4\n"
        "\tpxor    %xmm4, %xmm3\n"
        "\tpxor    %xmm2, %xmm3\n"
        "\tpxor    %xmm3, %xmm2\n"
        "\tpxor    %xmm1, %xmm2\n"
        "\tpxor    %xmm2, %xmm1\n"
        "\tpxor    %xmm0, %xmm1\n"
        "\tpxor    %xmm1, %xmm0\n"
    );
}

static void gf8mul_00000011(void)
{
    __asm__ __volatile__
    (
        "\tpxor    %xmm7, %xmm3\n"
        "\tpxor    %xmm7, %xmm1\n"
        "\tpxor    %xmm7, %xmm0\n"
        "\tpxor    %xmm6, %xmm7\n"
        "\tpxor    %xmm5, %xmm6\n"
        "\tpxor    %xmm4, %xmm5\n"
        "\tpxor    %xmm3, %xmm4\n"
        "\tpxor    %xmm2, %xmm3\n"
        "\tpxor    %xmm1, %xmm2\n"
        "\tpxor    %xmm0, %xmm1\n"
    );
}

static void gf8mul_00000100(void)
{
    __asm__ __volatile__
    (
        "\tpxor    %xmm7, %xmm5\n"
        "\tpxor    %xmm7, %xmm2\n"
        "\tpxor    %xmm6, %xmm4\n"
        "\tpxor    %xmm6, %xmm1\n"
        "\tpxor    %xmm5, %xmm7\n"
        "\tpxor    %xmm7, %xmm5\n"
        "\tpxor    %xmm4, %xmm6\n"
        "\tpxor    %xmm6, %xmm4\n"
        "\tpxor    %xmm3, %xmm5\n"
        "\tpxor    %xmm5, %xmm3\n"
        "\tpxor    %xmm2, %xmm4\n"
        "\tpxor    %xmm4, %xmm2\n"
        "\tpxor    %xmm1, %xmm3\n"
        "\tpxor    %xmm3, %xmm1\n"
        "\tpxor    %xmm0, %xmm2\n"
        "\tpxor    %xmm2, %xmm0\n"
    );
}

static void gf8mul_00000101(void)
{
    __asm__ __volatile__
    (
        "\tpxor    %xmm7, %xmm6\n"
        "\tpxor    %xmm7, %xmm3\n"
        "\tpxor    %xmm7, %xmm1\n"
        "\tpxor    %xmm7, %xmm0\n"
        "\tpxor    %xmm6, %xmm7\n"
        "\tpxor    %xmm6, %xmm5\n"
        "\tpxor    %xmm6, %xmm4\n"
        "\tpxor    %xmm6, %xmm3\n"
        "\tpxor    %xmm6, %xmm0\n"
        "\tpxor    %xmm7, %xmm6\n"
        "\tpxor    %xmm5, %xmm7\n"
        "\tpxor    %xmm4, %xmm6\n"
        "\tpxor    %xmm3, %xmm5\n"
        "\tpxor    %xmm2, %xmm4\n"
        "\tpxor    %xmm1, %xmm3\n"
        "\tpxor    %xmm0, %xmm2\n"
    );
}

static void gf8mul_00000110(void)
{
    __asm__ __volatile__
    (
        "\tpxor    %xmm7, %xmm6\n"
        "\tpxor    %xmm7, %xmm3\n"
        "\tpxor    %xmm7, %xmm1\n"
        "\tpxor    %xmm7, %xmm0\n"
        "\tpxor    %xmm6, %xmm5\n"
        "\tpxor    %xmm6, %xmm3\n"
        "\tpxor    %xmm6, %xmm2\n"
        "\tpxor    %xmm6, %xmm1\n"
        "\tpxor    %xmm6, %xmm0\n"
        "\tpxor    %xmm5, %xmm7\n"
        "\tpxor    %xmm5, %xmm6\n"
        "\tpxor    %xmm6, %xmm5\n"
        "\tpxor    %xmm4, %xmm6\n"
        "\tpxor    %xmm4, %xmm5\n"
        "\tpxor    %xmm5, %xmm4\n"
        "\tpxor    %xmm3, %xmm5\n"
        "\tpxor    %xmm3, %xmm4\n"
        "\tpxor    %xmm4, %xmm3\n"
        "\tpxor    %xmm2, %xmm4\n"
        "\tpxor    %xmm2, %xmm3\n"
        "\tpxor    %xmm3, %xmm2\n"
        "\tpxor    %xmm1, %xmm3\n"
        "\tpxor    %xmm1, %xmm2\n"
        "\tpxor    %xmm2, %xmm1\n"
        "\tpxor    %xmm0, %xmm2\n"
        "\tpxor    %xmm0, %xmm1\n"
        "\tpxor    %xmm1, %xmm0\n"
    );
}

static void gf8mul_00000111(void)
{
    __asm__ __volatile__
    (
        "\tpxor    %xmm7, %xmm6\n"
        "\tpxor    %xmm7, %xmm5\n"
        "\tpxor    %xmm7, %xmm4\n"
        "\tpxor    %xmm7, %xmm3\n"
        "\tpxor    %xmm7, %xmm2\n"
        "\tpxor    %xmm7, %xmm1\n"
        "\tpxor    %xmm6, %xmm7\n"
        "\tpxor    %xmm6, %xmm4\n"
        "\tpxor    %xmm6, %xmm3\n"
        "\tpxor    %xmm6, %xmm2\n"
        "\tpxor    %xmm6, %xmm1\n"
        "\tpxor    %xmm6, %xmm0\n"
        "\tpxor    %xmm7, %xmm6\n"
        "\tpxor    %xmm5, %xmm7\n"
        "\tpxor    %xmm5, %xmm6\n"
        "\tpxor    %xmm4, %xmm6\n"
        "\tpxor    %xmm4, %xmm5\n"
        "\tpxor    %xmm3, %xmm5\n"
        "\tpxor    %xmm3, %xmm4\n"
        "\tpxor    %xmm2, %xmm4\n"
        "\tpxor    %xmm2, %xmm3\n"
        "\tpxor    %xmm1, %xmm3\n"
        "\tpxor    %xmm1, %xmm2\n"
        "\tpxor    %xmm0, %xmm2\n"
        "\tpxor    %xmm0, %xmm1\n"
    );
}

static void gf8mul_00001000(void)
{
    __asm__ __volatile__
    (
        "\tpxor    %xmm7, %xmm6\n"
        "\tpxor    %xmm7, %xmm5\n"
        "\tpxor    %xmm7, %xmm4\n"
        "\tpxor    %xmm7, %xmm0\n"
        "\tpxor    %xmm6, %xmm7\n"
        "\tpxor    %xmm6, %xmm4\n"
        "\tpxor    %xmm6, %xmm2\n"
        "\tpxor    %xmm6, %xmm0\n"
        "\tpxor    %xmm7, %xmm6\n"
        "\tpxor    %xmm5, %xmm6\n"
        "\tpxor    %xmm5, %xmm3\n"
        "\tpxor    %xmm5, %xmm2\n"
        "\tpxor    %xmm5, %xmm1\n"
        "\tpxor    %xmm4, %xmm7\n"
        "\tpxor    %xmm7, %xmm4\n"
        "\tpxor    %xmm3, %xmm6\n"
        "\tpxor    %xmm6, %xmm3\n"
        "\tpxor    %xmm2, %xmm5\n"
        "\tpxor    %xmm5, %xmm2\n"
        "\tpxor    %xmm1, %xmm4\n"
        "\tpxor    %xmm4, %xmm1\n"
        "\tpxor    %xmm0, %xmm3\n"
        "\tpxor    %xmm3, %xmm0\n"
    );
}

static void gf8mul_00001001(void)
{
    __asm__ __volatile__
    (
        "\tpxor    %xmm7, %xmm6\n"
        "\tpxor    %xmm7, %xmm5\n"
        "\tpxor    %xmm7, %xmm3\n"
        "\tpxor    %xmm7, %xmm1\n"
        "\tpxor    %xmm7, %xmm0\n"
        "\tpxor    %xmm6, %xmm7\n"
        "\tpxor    %xmm6, %xmm5\n"
        "\tpxor    %xmm6, %xmm4\n"
        "\tpxor    %xmm6, %xmm3\n"
        "\tpxor    %xmm6, %xmm2\n"
        "\tpxor    %xmm6, %xmm1\n"
        "\tpxor    %xmm6, %xmm0\n"
        "\tpxor    %xmm7, %xmm6\n"
        "\tpxor    %xmm5, %xmm7\n"
        "\tpxor    %xmm5, %xmm4\n"
        "\tpxor    %xmm5, %xmm2\n"
        "\tpxor    %xmm5, %xmm0\n"
        "\tpxor    %xmm7, %xmm5\n"
        "\tpxor    %xmm4, %xmm7\n"
        "\tpxor    %xmm3, %xmm6\n"
        "\tpxor    %xmm2, %xmm5\n"
        "\tpxor    %xmm1, %xmm4\n"
        "\tpxor    %xmm0, %xmm3\n"
    );
}

static void gf8mul_00001010(void)
{
    __asm__ __volatile__
    (
        "\tpxor    %xmm7, %xmm5\n"
        "\tpxor    %xmm7, %xmm4\n"
        "\tpxor    %xmm7, %xmm1\n"
        "\tpxor    %xmm6, %xmm4\n"
        "\tpxor    %xmm6, %xmm3\n"
        "\tpxor    %xmm6, %xmm0\n"
        "\tpxor    %xmm5, %xmm6\n"
        "\tpxor    %xmm5, %xmm2\n"
        "\tpxor    %xmm5, %xmm0\n"
        "\tpxor    %xmm4, %xmm7\n"
        "\tpxor    %xmm4, %xmm5\n"
        "\tpxor    %xmm5, %xmm4\n"
        "\tpxor    %xmm3, %xmm6\n"
        "\tpxor    %xmm3, %xmm4\n"
        "\tpxor    %xmm4, %xmm3\n"
        "\tpxor    %xmm2, %xmm5\n"
        "\tpxor    %xmm2, %xmm3\n"
        "\tpxor    %xmm3, %xmm2\n"
        "\tpxor    %xmm1, %xmm4\n"
        "\tpxor    %xmm1, %xmm2\n"
        "\tpxor    %xmm2, %xmm1\n"
        "\tpxor    %xmm0, %xmm3\n"
        "\tpxor    %xmm0, %xmm1\n"
        "\tpxor    %xmm1, %xmm0\n"
    );
}

static void gf8mul_00001011(void)
{
    __asm__ __volatile__
    (
        "\tpxor    %xmm7, %xmm3\n"
        "\tpxor    %xmm7, %xmm2\n"
        "\tpxor    %xmm7, %xmm1\n"
        "\tpxor    %xmm7, %xmm0\n"
        "\tpxor    %xmm6, %xmm7\n"
        "\tpxor    %xmm6, %xmm2\n"
        "\tpxor    %xmm6, %xmm1\n"
        "\tpxor    %xmm5, %xmm6\n"
        "\tpxor    %xmm5, %xmm1\n"
        "\tpxor    %xmm5, %xmm0\n"
        "\tpxor    %xmm4, %xmm7\n"
        "\tpxor    %xmm4, %xmm5\n"
        "\tpxor    %xmm3, %xmm6\n"
        "\tpxor    %xmm3, %xmm4\n"
        "\tpxor    %xmm2, %xmm5\n"
        "\tpxor    %xmm2, %xmm3\n"
        "\tpxor    %xmm1, %xmm4\n"
        "\tpxor    %xmm1, %xmm2\n"
        "\tpxor    %xmm0, %xmm3\n"
        "\tpxor    %xmm0, %xmm1\n"
    );
}

static void gf8mul_00001100(void)
{
    __asm__ __volatile__
    (
        "\tpxor    %xmm7, %xmm5\n"
        "\tpxor    %xmm7, %xmm2\n"
        "\tpxor    %xmm6, %xmm4\n"
        "\tpxor    %xmm6, %xmm1\n"
        "\tpxor    %xmm5, %xmm7\n"
        "\tpxor    %xmm7, %xmm5\n"
        "\tpxor    %xmm4, %xmm6\n"
        "\tpxor    %xmm6, %xmm4\n"
        "\tpxor    %xmm3, %xmm5\n"
        "\tpxor    %xmm5, %xmm3\n"
        "\tpxor    %xmm2, %xmm4\n"
        "\tpxor    %xmm4, %xmm2\n"
        "\tpxor    %xmm1, %xmm3\n"
        "\tpxor    %xmm3, %xmm1\n"
        "\tpxor    %xmm0, %xmm2\n"
        "\tpxor    %xmm2, %xmm0\n"
        "\tpxor    %xmm7, %xmm3\n"
        "\tpxor    %xmm7, %xmm1\n"
        "\tpxor    %xmm7, %xmm0\n"
        "\tpxor    %xmm6, %xmm7\n"
        "\tpxor    %xmm5, %xmm6\n"
        "\tpxor    %xmm4, %xmm5\n"
        "\tpxor    %xmm3, %xmm4\n"
        "\tpxor    %xmm2, %xmm3\n"
        "\tpxor    %xmm1, %xmm2\n"
        "\tpxor    %xmm0, %xmm1\n"
    );
}

static void gf8mul_00001101(void)
{
    __asm__ __volatile__
    (
        "\tpxor    %xmm7, %xmm5\n"
        "\tpxor    %xmm7, %xmm4\n"
        "\tpxor    %xmm7, %xmm2\n"
        "\tpxor    %xmm7, %xmm1\n"
        "\tpxor    %xmm7, %xmm0\n"
        "\tpxor    %xmm6, %xmm7\n"
        "\tpxor    %xmm6, %xmm5\n"
        "\tpxor    %xmm6, %xmm1\n"
        "\tpxor    %xmm5, %xmm6\n"
        "\tpxor    %xmm5, %xmm4\n"
        "\tpxor    %xmm5, %xmm0\n"
        "\tpxor    %xmm4, %xmm7\n"
        "\tpxor    %xmm4, %xmm6\n"
        "\tpxor    %xmm3, %xmm6\n"
        "\tpxor    %xmm3, %xmm5\n"
        "\tpxor    %xmm2, %xmm5\n"
        "\tpxor    %xmm2, %xmm4\n"
        "\tpxor    %xmm1, %xmm4\n"
        "\tpxor    %xmm1, %xmm3\n"
        "\tpxor    %xmm0, %xmm3\n"
        "\tpxor    %xmm0, %xmm2\n"
    );
}

static void gf8mul_00001110(void)
{
    __asm__ __volatile__
    (
        "\tpxor    %xmm7, %xmm5\n"
        "\tpxor    %xmm7, %xmm0\n"
        "\tpxor    %xmm6, %xmm7\n"
        "\tpxor    %xmm6, %xmm5\n"
        "\tpxor    %xmm6, %xmm4\n"
        "\tpxor    %xmm6, %xmm3\n"
        "\tpxor    %xmm6, %xmm2\n"
        "\tpxor    %xmm6, %xmm1\n"
        "\tpxor    %xmm6, %xmm0\n"
        "\tpxor    %xmm5, %xmm7\n"
        "\tpxor    %xmm5, %xmm3\n"
        "\tpxor    %xmm5, %xmm1\n"
        "\tpxor    %xmm5, %xmm0\n"
        "\tpxor    %xmm4, %xmm7\n"
        "\tpxor    %xmm4, %xmm6\n"
        "\tpxor    %xmm4, %xmm5\n"
        "\tpxor    %xmm5, %xmm4\n"
        "\tpxor    %xmm3, %xmm6\n"
        "\tpxor    %xmm3, %xmm5\n"
        "\tpxor    %xmm3, %xmm4\n"
        "\tpxor    %xmm4, %xmm3\n"
        "\tpxor    %xmm2, %xmm5\n"
        "\tpxor    %xmm2, %xmm4\n"
        "\tpxor    %xmm2, %xmm3\n"
        "\tpxor    %xmm3, %xmm2\n"
        "\tpxor    %xmm1, %xmm4\n"
        "\tpxor    %xmm1, %xmm3\n"
        "\tpxor    %xmm1, %xmm2\n"
        "\tpxor    %xmm2, %xmm1\n"
        "\tpxor    %xmm0, %xmm3\n"
        "\tpxor    %xmm0, %xmm2\n"
        "\tpxor    %xmm0, %xmm1\n"
        "\tpxor    %xmm1, %xmm0\n"
    );
}

static void gf8mul_00001111(void)
{
    __asm__ __volatile__
    (
        "\tpxor    %xmm7, %xmm6\n"
        "\tpxor    %xmm7, %xmm3\n"
        "\tpxor    %xmm7, %xmm1\n"
        "\tpxor    %xmm7, %xmm0\n"
        "\tpxor    %xmm6, %xmm7\n"
        "\tpxor    %xmm6, %xmm5\n"
        "\tpxor    %xmm6, %xmm4\n"
        "\tpxor    %xmm6, %xmm3\n"
        "\tpxor    %xmm6, %xmm0\n"
        "\tpxor    %xmm7, %xmm6\n"
        "\tpxor    %xmm5, %xmm7\n"
        "\tpxor    %xmm4, %xmm6\n"
        "\tpxor    %xmm3, %xmm5\n"
        "\tpxor    %xmm2, %xmm4\n"
        "\tpxor    %xmm1, %xmm3\n"
        "\tpxor    %xmm0, %xmm2\n"
        "\tpxor    %xmm7, %xmm3\n"
        "\tpxor    %xmm7, %xmm1\n"
        "\tpxor    %xmm7, %xmm0\n"
        "\tpxor    %xmm6, %xmm7\n"
        "\tpxor    %xmm5, %xmm6\n"
        "\tpxor    %xmm4, %xmm5\n"
        "\tpxor    %xmm3, %xmm4\n"
        "\tpxor    %xmm2, %xmm3\n"
        "\tpxor    %xmm1, %xmm2\n"
        "\tpxor    %xmm0, %xmm1\n"
    );
}

static void gf8mul_00010000(void)
{
    __asm__ __volatile__
    (
        "\tpxor    %xmm7, %xmm2\n"
        "\tpxor    %xmm7, %xmm1\n"
        "\tpxor    %xmm6, %xmm1\n"
        "\tpxor    %xmm6, %xmm0\n"
        "\tpxor    %xmm5, %xmm7\n"
        "\tpxor    %xmm5, %xmm3\n"
        "\tpxor    %xmm5, %xmm0\n"
        "\tpxor    %xmm4, %xmm7\n"
        "\tpxor    %xmm4, %xmm6\n"
        "\tpxor    %xmm4, %xmm3\n"
        "\tpxor    %xmm4, %xmm2\n"
        "\tpxor    %xmm3, %xmm7\n"
        "\tpxor    %xmm7, %xmm3\n"
        "\tpxor    %xmm2, %xmm6\n"
        "\tpxor    %xmm6, %xmm2\n"
        "\tpxor    %xmm1, %xmm5\n"
        "\tpxor    %xmm5, %xmm1\n"
        "\tpxor    %xmm0, %xmm4\n"
        "\tpxor    %xmm4, %xmm0\n"
    );
}

static void gf8mul_00010001(void)
{
    __asm__ __volatile__
    (
        "\tpxor    %xmm7, %xmm5\n"
        "\tpxor    %xmm7, %xmm4\n"
        "\tpxor    %xmm7, %xmm3\n"
        "\tpxor    %xmm7, %xmm2\n"
        "\tpxor    %xmm7, %xmm1\n"
        "\tpxor    %xmm7, %xmm0\n"
        "\tpxor    %xmm6, %xmm7\n"
        "\tpxor    %xmm6, %xmm5\n"
        "\tpxor    %xmm6, %xmm3\n"
        "\tpxor    %xmm6, %xmm2\n"
        "\tpxor    %xmm6, %xmm1\n"
        "\tpxor    %xmm5, %xmm6\n"
        "\tpxor    %xmm5, %xmm4\n"
        "\tpxor    %xmm5, %xmm2\n"
        "\tpxor    %xmm5, %xmm1\n"
        "\tpxor    %xmm5, %xmm0\n"
        "\tpxor    %xmm4, %xmm7\n"
        "\tpxor    %xmm4, %xmm6\n"
        "\tpxor    %xmm4, %xmm3\n"
        "\tpxor    %xmm4, %xmm2\n"
        "\tpxor    %xmm4, %xmm0\n"
        "\tpxor    %xmm3, %xmm7\n"
        "\tpxor    %xmm2, %xmm6\n"
        "\tpxor    %xmm1, %xmm5\n"
        "\tpxor    %xmm0, %xmm4\n"
    );
}

static void gf8mul_00010010(void)
{
    __asm__ __volatile__
    (
        "\tpxor    %xmm7, %xmm4\n"
        "\tpxor    %xmm7, %xmm2\n"
        "\tpxor    %xmm6, %xmm3\n"
        "\tpxor    %xmm6, %xmm1\n"
        "\tpxor    %xmm5, %xmm2\n"
        "\tpxor    %xmm5, %xmm0\n"
        "\tpxor    %xmm4, %xmm7\n"
        "\tpxor    %xmm4, %xmm5\n"
        "\tpxor    %xmm4, %xmm3\n"
        "\tpxor    %xmm4, %xmm0\n"
        "\tpxor    %xmm3, %xmm7\n"
        "\tpxor    %xmm3, %xmm4\n"
        "\tpxor    %xmm4, %xmm3\n"
        "\tpxor    %xmm2, %xmm6\n"
        "\tpxor    %xmm2, %xmm3\n"
        "\tpxor    %xmm3, %xmm2\n"
        "\tpxor    %xmm1, %xmm5\n"
        "\tpxor    %xmm1, %xmm2\n"
        "\tpxor    %xmm2, %xmm1\n"
        "\tpxor    %xmm0, %xmm4\n"
        "\tpxor    %xmm0, %xmm1\n"
        "\tpxor    %xmm1, %xmm0\n"
    );
}

static void gf8mul_00010011(void)
{
    __asm__ __volatile__
    (
        "\tpxor    %xmm7, %xmm6\n"
        "\tpxor    %xmm7, %xmm5\n"
        "\tpxor    %xmm7, %xmm4\n"
        "\tpxor    %xmm7, %xmm1\n"
        "\tpxor    %xmm6, %xmm7\n"
        "\tpxor    %xmm6, %xmm5\n"
        "\tpxor    %xmm6, %xmm4\n"
        "\tpxor    %xmm6, %xmm0\n"
        "\tpxor    %xmm5, %xmm7\n"
        "\tpxor    %xmm5, %xmm4\n"
        "\tpxor    %xmm5, %xmm3\n"
        "\tpxor    %xmm5, %xmm2\n"
        "\tpxor    %xmm5, %xmm0\n"
        "\tpxor    %xmm4, %xmm7\n"
        "\tpxor    %xmm4, %xmm3\n"
        "\tpxor    %xmm4, %xmm1\n"
        "\tpxor    %xmm4, %xmm0\n"
        "\tpxor    %xmm7, %xmm4\n"
        "\tpxor    %xmm3, %xmm7\n"
        "\tpxor    %xmm3, %xmm4\n"
        "\tpxor    %xmm2, %xmm6\n"
        "\tpxor    %xmm2, %xmm3\n"
        "\tpxor    %xmm1, %xmm5\n"
        "\tpxor    %xmm1, %xmm2\n"
        "\tpxor    %xmm0, %xmm4\n"
        "\tpxor    %xmm0, %xmm1\n"
    );
}

static void gf8mul_00010100(void)
{
    __asm__ __volatile__
    (
        "\tpxor    %xmm7, %xmm6\n"
        "\tpxor    %xmm7, %xmm5\n"
        "\tpxor    %xmm7, %xmm4\n"
        "\tpxor    %xmm7, %xmm3\n"
        "\tpxor    %xmm7, %xmm1\n"
        "\tpxor    %xmm7, %xmm0\n"
        "\tpxor    %xmm6, %xmm7\n"
        "\tpxor    %xmm6, %xmm4\n"
        "\tpxor    %xmm6, %xmm3\n"
        "\tpxor    %xmm6, %xmm0\n"
        "\tpxor    %xmm7, %xmm6\n"
        "\tpxor    %xmm5, %xmm7\n"
        "\tpxor    %xmm5, %xmm6\n"
        "\tpxor    %xmm5, %xmm2\n"
        "\tpxor    %xmm4, %xmm7\n"
        "\tpxor    %xmm4, %xmm6\n"
        "\tpxor    %xmm4, %xmm3\n"
        "\tpxor    %xmm4, %xmm1\n"
        "\tpxor    %xmm3, %xmm7\n"
        "\tpxor    %xmm3, %xmm5\n"
        "\tpxor    %xmm5, %xmm3\n"
        "\tpxor    %xmm2, %xmm6\n"
        "\tpxor    %xmm2, %xmm4\n"
        "\tpxor    %xmm4, %xmm2\n"
        "\tpxor    %xmm1, %xmm5\n"
        "\tpxor    %xmm1, %xmm3\n"
        "\tpxor    %xmm3, %xmm1\n"
        "\tpxor    %xmm0, %xmm4\n"
        "\tpxor    %xmm0, %xmm2\n"
        "\tpxor    %xmm2, %xmm0\n"
    );
}

static void gf8mul_00010101(void)
{
    __asm__ __volatile__
    (
        "\tpxor    %xmm7, %xmm6\n"
        "\tpxor    %xmm7, %xmm4\n"
        "\tpxor    %xmm7, %xmm3\n"
        "\tpxor    %xmm7, %xmm2\n"
        "\tpxor    %xmm7, %xmm1\n"
        "\tpxor    %xmm6, %xmm7\n"
        "\tpxor    %xmm6, %xmm5\n"
        "\tpxor    %xmm6, %xmm4\n"
        "\tpxor    %xmm6, %xmm2\n"
        "\tpxor    %xmm6, %xmm1\n"
        "\tpxor    %xmm7, %xmm6\n"
        "\tpxor    %xmm5, %xmm6\n"
        "\tpxor    %xmm5, %xmm4\n"
        "\tpxor    %xmm5, %xmm3\n"
        "\tpxor    %xmm5, %xmm1\n"
        "\tpxor    %xmm5, %xmm0\n"
        "\tpxor    %xmm6, %xmm5\n"
        "\tpxor    %xmm4, %xmm7\n"
        "\tpxor    %xmm4, %xmm6\n"
        "\tpxor    %xmm4, %xmm5\n"
        "\tpxor    %xmm4, %xmm3\n"
        "\tpxor    %xmm4, %xmm0\n"
        "\tpxor    %xmm3, %xmm7\n"
        "\tpxor    %xmm3, %xmm5\n"
        "\tpxor    %xmm2, %xmm6\n"
        "\tpxor    %xmm2, %xmm4\n"
        "\tpxor    %xmm1, %xmm5\n"
        "\tpxor    %xmm1, %xmm3\n"
        "\tpxor    %xmm0, %xmm4\n"
        "\tpxor    %xmm0, %xmm2\n"
    );
}

static void gf8mul_00010110(void)
{
    __asm__ __volatile__
    (
        "\tpxor    %xmm7, %xmm6\n"
        "\tpxor    %xmm7, %xmm3\n"
        "\tpxor    %xmm7, %xmm0\n"
        "\tpxor    %xmm6, %xmm7\n"
        "\tpxor    %xmm6, %xmm4\n"
        "\tpxor    %xmm6, %xmm2\n"
        "\tpxor    %xmm6, %xmm1\n"
        "\tpxor    %xmm5, %xmm7\n"
        "\tpxor    %xmm5, %xmm6\n"
        "\tpxor    %xmm5, %xmm1\n"
        "\tpxor    %xmm5, %xmm0\n"
        "\tpxor    %xmm6, %xmm5\n"
        "\tpxor    %xmm4, %xmm2\n"
        "\tpxor    %xmm4, %xmm1\n"
        "\tpxor    %xmm4, %xmm0\n"
        "\tpxor    %xmm3, %xmm7\n"
        "\tpxor    %xmm3, %xmm5\n"
        "\tpxor    %xmm3, %xmm4\n"
        "\tpxor    %xmm4, %xmm3\n"
        "\tpxor    %xmm2, %xmm6\n"
        "\tpxor    %xmm2, %xmm4\n"
        "\tpxor    %xmm2, %xmm3\n"
        "\tpxor    %xmm3, %xmm2\n"
        "\tpxor    %xmm1, %xmm5\n"
        "\tpxor    %xmm1, %xmm3\n"
        "\tpxor    %xmm1, %xmm2\n"
        "\tpxor    %xmm2, %xmm1\n"
        "\tpxor    %xmm0, %xmm4\n"
        "\tpxor    %xmm0, %xmm2\n"
        "\tpxor    %xmm0, %xmm1\n"
        "\tpxor    %xmm1, %xmm0\n"
    );
}

static void gf8mul_00010111(void)
{
    __asm__ __volatile__
    (
        "\tpxor    %xmm7, %xmm6\n"
        "\tpxor    %xmm7, %xmm4\n"
        "\tpxor    %xmm7, %xmm2\n"
        "\tpxor    %xmm7, %xmm0\n"
        "\tpxor    %xmm6, %xmm7\n"
        "\tpxor    %xmm6, %xmm5\n"
        "\tpxor    %xmm6, %xmm3\n"
        "\tpxor    %xmm6, %xmm2\n"
        "\tpxor    %xmm6, %xmm0\n"
        "\tpxor    %xmm7, %xmm6\n"
        "\tpxor    %xmm5, %xmm7\n"
        "\tpxor    %xmm5, %xmm4\n"
        "\tpxor    %xmm5, %xmm0\n"
        "\tpxor    %xmm4, %xmm7\n"
        "\tpxor    %xmm4, %xmm5\n"
        "\tpxor    %xmm4, %xmm3\n"
        "\tpxor    %xmm4, %xmm2\n"
        "\tpxor    %xmm4, %xmm1\n"
        "\tpxor    %xmm4, %xmm0\n"
        "\tpxor    %xmm3, %xmm7\n"
        "\tpxor    %xmm3, %xmm5\n"
        "\tpxor    %xmm3, %xmm4\n"
        "\tpxor    %xmm2, %xmm6\n"
        "\tpxor    %xmm2, %xmm4\n"
        "\tpxor    %xmm2, %xmm3\n"
        "\tpxor    %xmm1, %xmm5\n"
        "\tpxor    %xmm1, %xmm3\n"
        "\tpxor    %xmm1, %xmm2\n"
        "\tpxor    %xmm0, %xmm4\n"
        "\tpxor    %xmm0, %xmm2\n"
        "\tpxor    %xmm0, %xmm1\n"
    );
}

static void gf8mul_00011000(void)
{
    __asm__ __volatile__
    (
        "\tpxor    %xmm7, %xmm6\n"
        "\tpxor    %xmm7, %xmm5\n"
        "\tpxor    %xmm7, %xmm4\n"
        "\tpxor    %xmm7, %xmm3\n"
        "\tpxor    %xmm7, %xmm1\n"
        "\tpxor    %xmm7, %xmm0\n"
        "\tpxor    %xmm6, %xmm7\n"
        "\tpxor    %xmm6, %xmm5\n"
        "\tpxor    %xmm6, %xmm3\n"
        "\tpxor    %xmm6, %xmm2\n"
        "\tpxor    %xmm5, %xmm7\n"
        "\tpxor    %xmm5, %xmm6\n"
        "\tpxor    %xmm5, %xmm3\n"
        "\tpxor    %xmm5, %xmm0\n"
        "\tpxor    %xmm4, %xmm6\n"
        "\tpxor    %xmm4, %xmm5\n"
        "\tpxor    %xmm4, %xmm3\n"
        "\tpxor    %xmm4, %xmm1\n"
        "\tpxor    %xmm5, %xmm4\n"
        "\tpxor    %xmm3, %xmm7\n"
        "\tpxor    %xmm3, %xmm6\n"
        "\tpxor    %xmm6, %xmm3\n"
        "\tpxor    %xmm2, %xmm6\n"
        "\tpxor    %xmm2, %xmm5\n"
        "\tpxor    %xmm5, %xmm2\n"
        "\tpxor    %xmm1, %xmm5\n"
        "\tpxor    %xmm1, %xmm4\n"
        "\tpxor    %xmm4, %xmm1\n"
        "\tpxor    %xmm0, %xmm4\n"
        "\tpxor    %xmm0, %xmm3\n"
        "\tpxor    %xmm3, %xmm0\n"
    );
}

static void gf8mul_00011001(void)
{
    __asm__ __volatile__
    (
        "\tpxor    %xmm7, %xmm6\n"
        "\tpxor    %xmm7, %xmm5\n"
        "\tpxor    %xmm7, %xmm3\n"
        "\tpxor    %xmm7, %xmm2\n"
        "\tpxor    %xmm7, %xmm0\n"
        "\tpxor    %xmm6, %xmm7\n"
        "\tpxor    %xmm6, %xmm5\n"
        "\tpxor    %xmm6, %xmm4\n"
        "\tpxor    %xmm6, %xmm2\n"
        "\tpxor    %xmm5, %xmm7\n"
        "\tpxor    %xmm5, %xmm3\n"
        "\tpxor    %xmm5, %xmm2\n"
        "\tpxor    %xmm5, %xmm1\n"
        "\tpxor    %xmm5, %xmm0\n"
        "\tpxor    %xmm7, %xmm5\n"
        "\tpxor    %xmm4, %xmm7\n"
        "\tpxor    %xmm4, %xmm6\n"
        "\tpxor    %xmm4, %xmm5\n"
        "\tpxor    %xmm4, %xmm2\n"
        "\tpxor    %xmm4, %xmm0\n"
        "\tpxor    %xmm3, %xmm7\n"
        "\tpxor    %xmm3, %xmm6\n"
        "\tpxor    %xmm2, %xmm6\n"
        "\tpxor    %xmm2, %xmm5\n"
        "\tpxor    %xmm1, %xmm5\n"
        "\tpxor    %xmm1, %xmm4\n"
        "\tpxor    %xmm0, %xmm4\n"
        "\tpxor    %xmm0, %xmm3\n"
    );
}

static void gf8mul_00011010(void)
{
    __asm__ __volatile__
    (
        "\tpxor    %xmm7, %xmm6\n"
        "\tpxor    %xmm7, %xmm4\n"
        "\tpxor    %xmm7, %xmm2\n"
        "\tpxor    %xmm7, %xmm0\n"
        "\tpxor    %xmm6, %xmm7\n"
        "\tpxor    %xmm6, %xmm1\n"
        "\tpxor    %xmm6, %xmm0\n"
        "\tpxor    %xmm5, %xmm7\n"
        "\tpxor    %xmm5, %xmm6\n"
        "\tpxor    %xmm5, %xmm4\n"
        "\tpxor    %xmm5, %xmm0\n"
        "\tpxor    %xmm6, %xmm5\n"
        "\tpxor    %xmm4, %xmm3\n"
        "\tpxor    %xmm4, %xmm2\n"
        "\tpxor    %xmm4, %xmm0\n"
        "\tpxor    %xmm3, %xmm7\n"
        "\tpxor    %xmm3, %xmm6\n"
        "\tpxor    %xmm3, %xmm4\n"
        "\tpxor    %xmm4, %xmm3\n"
        "\tpxor    %xmm2, %xmm6\n"
        "\tpxor    %xmm2, %xmm5\n"
        "\tpxor    %xmm2, %xmm3\n"
        "\tpxor    %xmm3, %xmm2\n"
        "\tpxor    %xmm1, %xmm5\n"
        "\tpxor    %xmm1, %xmm4\n"
        "\tpxor    %xmm1, %xmm2\n"
        "\tpxor    %xmm2, %xmm1\n"
        "\tpxor    %xmm0, %xmm4\n"
        "\tpxor    %xmm0, %xmm3\n"
        "\tpxor    %xmm0, %xmm1\n"
        "\tpxor    %xmm1, %xmm0\n"
    );
}

static void gf8mul_00011011(void)
{
    __asm__ __volatile__
    (
        "\tpxor    %xmm7, %xmm6\n"
        "\tpxor    %xmm7, %xmm5\n"
        "\tpxor    %xmm7, %xmm4\n"
        "\tpxor    %xmm7, %xmm3\n"
        "\tpxor    %xmm7, %xmm1\n"
        "\tpxor    %xmm7, %xmm0\n"
        "\tpxor    %xmm6, %xmm7\n"
        "\tpxor    %xmm6, %xmm5\n"
        "\tpxor    %xmm6, %xmm4\n"
        "\tpxor    %xmm6, %xmm3\n"
        "\tpxor    %xmm6, %xmm2\n"
        "\tpxor    %xmm5, %xmm7\n"
        "\tpxor    %xmm5, %xmm4\n"
        "\tpxor    %xmm5, %xmm2\n"
        "\tpxor    %xmm5, %xmm1\n"
        "\tpxor    %xmm4, %xmm7\n"
        "\tpxor    %xmm4, %xmm1\n"
        "\tpxor    %xmm4, %xmm0\n"
        "\tpxor    %xmm7, %xmm4\n"
        "\tpxor    %xmm3, %xmm7\n"
        "\tpxor    %xmm3, %xmm6\n"
        "\tpxor    %xmm3, %xmm4\n"
        "\tpxor    %xmm2, %xmm6\n"
        "\tpxor    %xmm2, %xmm5\n"
        "\tpxor    %xmm2, %xmm3\n"
        "\tpxor    %xmm1, %xmm5\n"
        "\tpxor    %xmm1, %xmm4\n"
        "\tpxor    %xmm1, %xmm2\n"
        "\tpxor    %xmm0, %xmm4\n"
        "\tpxor    %xmm0, %xmm3\n"
        "\tpxor    %xmm0, %xmm1\n"
    );
}

static void gf8mul_00011100(void)
{
    __asm__ __volatile__
    (
        "\tpxor    %xmm7, %xmm6\n"
        "\tpxor    %xmm7, %xmm4\n"
        "\tpxor    %xmm7, %xmm3\n"
        "\tpxor    %xmm7, %xmm2\n"
        "\tpxor    %xmm7, %xmm1\n"
        "\tpxor    %xmm6, %xmm5\n"
        "\tpxor    %xmm6, %xmm3\n"
        "\tpxor    %xmm6, %xmm2\n"
        "\tpxor    %xmm6, %xmm1\n"
        "\tpxor    %xmm6, %xmm0\n"
        "\tpxor    %xmm5, %xmm7\n"
        "\tpxor    %xmm5, %xmm6\n"
        "\tpxor    %xmm5, %xmm4\n"
        "\tpxor    %xmm5, %xmm3\n"
        "\tpxor    %xmm5, %xmm2\n"
        "\tpxor    %xmm4, %xmm7\n"
        "\tpxor    %xmm4, %xmm2\n"
        "\tpxor    %xmm4, %xmm1\n"
        "\tpxor    %xmm3, %xmm7\n"
        "\tpxor    %xmm3, %xmm6\n"
        "\tpxor    %xmm3, %xmm5\n"
        "\tpxor    %xmm5, %xmm3\n"
        "\tpxor    %xmm2, %xmm6\n"
        "\tpxor    %xmm2, %xmm5\n"
        "\tpxor    %xmm2, %xmm4\n"
        "\tpxor    %xmm4, %xmm2\n"
        "\tpxor    %xmm1, %xmm5\n"
        "\tpxor    %xmm1, %xmm4\n"
        "\tpxor    %xmm1, %xmm3\n"
        "\tpxor    %xmm3, %xmm1\n"
        "\tpxor    %xmm0, %xmm4\n"
        "\tpxor    %xmm0, %xmm3\n"
        "\tpxor    %xmm0, %xmm2\n"
        "\tpxor    %xmm2, %xmm0\n"
    );
}

static void gf8mul_00011101(void)
{
    __asm__ __volatile__
    (
        "\tpxor    %xmm7, %xmm3\n"
        "\tpxor    %xmm7, %xmm2\n"
        "\tpxor    %xmm7, %xmm1\n"
        "\tpxor    %xmm6, %xmm2\n"
        "\tpxor    %xmm6, %xmm1\n"
        "\tpxor    %xmm6, %xmm0\n"
        "\tpxor    %xmm5, %xmm7\n"
        "\tpxor    %xmm5, %xmm1\n"
        "\tpxor    %xmm5, %xmm0\n"
        "\tpxor    %xmm4, %xmm7\n"
        "\tpxor    %xmm4, %xmm6\n"
        "\tpxor    %xmm4, %xmm0\n"
        "\tpxor    %xmm3, %xmm7\n"
        "\tpxor    %xmm3, %xmm6\n"
        "\tpxor    %xmm3, %xmm5\n"
        "\tpxor    %xmm2, %xmm6\n"
        "\tpxor    %xmm2, %xmm5\n"
        "\tpxor    %xmm2, %xmm4\n"
        "\tpxor    %xmm1, %xmm5\n"
        "\tpxor    %xmm1, %xmm4\n"
        "\tpxor    %xmm1, %xmm3\n"
        "\tpxor    %xmm0, %xmm4\n"
        "\tpxor    %xmm0, %xmm3\n"
        "\tpxor    %xmm0, %xmm2\n"
    );
}

static void gf8mul_00011110(void)
{
    __asm__ __volatile__
    (
        "\tpxor    %xmm7, %xmm5\n"
        "\tpxor    %xmm7, %xmm4\n"
        "\tpxor    %xmm7, %xmm1\n"
        "\tpxor    %xmm6, %xmm4\n"
        "\tpxor    %xmm6, %xmm3\n"
        "\tpxor    %xmm6, %xmm0\n"
        "\tpxor    %xmm5, %xmm6\n"
        "\tpxor    %xmm5, %xmm2\n"
        "\tpxor    %xmm5, %xmm0\n"
        "\tpxor    %xmm4, %xmm7\n"
        "\tpxor    %xmm4, %xmm5\n"
        "\tpxor    %xmm5, %xmm4\n"
        "\tpxor    %xmm3, %xmm6\n"
        "\tpxor    %xmm3, %xmm4\n"
        "\tpxor    %xmm4, %xmm3\n"
        "\tpxor    %xmm2, %xmm5\n"
        "\tpxor    %xmm2, %xmm3\n"
        "\tpxor    %xmm3, %xmm2\n"
        "\tpxor    %xmm1, %xmm4\n"
        "\tpxor    %xmm1, %xmm2\n"
        "\tpxor    %xmm2, %xmm1\n"
        "\tpxor    %xmm0, %xmm3\n"
        "\tpxor    %xmm0, %xmm1\n"
        "\tpxor    %xmm1, %xmm0\n"
        "\tpxor    %xmm7, %xmm3\n"
        "\tpxor    %xmm7, %xmm1\n"
        "\tpxor    %xmm7, %xmm0\n"
        "\tpxor    %xmm6, %xmm7\n"
        "\tpxor    %xmm5, %xmm6\n"
        "\tpxor    %xmm4, %xmm5\n"
        "\tpxor    %xmm3, %xmm4\n"
        "\tpxor    %xmm2, %xmm3\n"
        "\tpxor    %xmm1, %xmm2\n"
        "\tpxor    %xmm0, %xmm1\n"
    );
}

static void gf8mul_00011111(void)
{
    __asm__ __volatile__
    (
        "\tpxor    %xmm7, %xmm5\n"
        "\tpxor    %xmm7, %xmm2\n"
        "\tpxor    %xmm6, %xmm4\n"
        "\tpxor    %xmm6, %xmm1\n"
        "\tpxor    %xmm5, %xmm3\n"
        "\tpxor    %xmm5, %xmm0\n"
        "\tpxor    %xmm4, %xmm7\n"
        "\tpxor    %xmm4, %xmm5\n"
        "\tpxor    %xmm4, %xmm2\n"
        "\tpxor    %xmm4, %xmm1\n"
        "\tpxor    %xmm4, %xmm0\n"
        "\tpxor    %xmm3, %xmm7\n"
        "\tpxor    %xmm3, %xmm6\n"
        "\tpxor    %xmm3, %xmm5\n"
        "\tpxor    %xmm3, %xmm4\n"
        "\tpxor    %xmm2, %xmm6\n"
        "\tpxor    %xmm2, %xmm5\n"
        "\tpxor    %xmm2, %xmm4\n"
        "\tpxor    %xmm2, %xmm3\n"
        "\tpxor    %xmm1, %xmm5\n"
        "\tpxor    %xmm1, %xmm4\n"
        "\tpxor    %xmm1, %xmm3\n"
        "\tpxor    %xmm1, %xmm2\n"
        "\tpxor    %xmm0, %xmm4\n"
        "\tpxor    %xmm0, %xmm3\n"
        "\tpxor    %xmm0, %xmm2\n"
        "\tpxor    %xmm0, %xmm1\n"
    );
}

static void gf8mul_00100000(void)
{
    __asm__ __volatile__
    (
        "\tpxor    %xmm7, %xmm5\n"
        "\tpxor    %xmm7, %xmm3\n"
        "\tpxor    %xmm7, %xmm0\n"
        "\tpxor    %xmm6, %xmm7\n"
        "\tpxor    %xmm6, %xmm5\n"
        "\tpxor    %xmm6, %xmm4\n"
        "\tpxor    %xmm6, %xmm1\n"
        "\tpxor    %xmm6, %xmm0\n"
        "\tpxor    %xmm5, %xmm6\n"
        "\tpxor    %xmm5, %xmm4\n"
        "\tpxor    %xmm5, %xmm3\n"
        "\tpxor    %xmm5, %xmm0\n"
        "\tpxor    %xmm4, %xmm7\n"
        "\tpxor    %xmm4, %xmm6\n"
        "\tpxor    %xmm4, %xmm5\n"
        "\tpxor    %xmm4, %xmm3\n"
        "\tpxor    %xmm4, %xmm2\n"
        "\tpxor    %xmm4, %xmm1\n"
        "\tpxor    %xmm5, %xmm4\n"
        "\tpxor    %xmm3, %xmm7\n"
        "\tpxor    %xmm3, %xmm5\n"
        "\tpxor    %xmm3, %xmm4\n"
        "\tpxor    %xmm3, %xmm2\n"
        "\tpxor    %xmm3, %xmm0\n"
        "\tpxor    %xmm2, %xmm7\n"
        "\tpxor    %xmm7, %xmm2\n"
        "\tpxor    %xmm1, %xmm6\n"
        "\tpxor    %xmm6, %xmm1\n"
        "\tpxor    %xmm0, %xmm5\n"
        "\tpxor    %xmm5, %xmm0\n"
    );
}

static void gf8mul_00100001(void)
{
    __asm__ __volatile__
    (
        "\tpxor    %xmm7, %xmm4\n"
        "\tpxor    %xmm7, %xmm2\n"
        "\tpxor    %xmm7, %xmm1\n"
        "\tpxor    %xmm7, %xmm0\n"
        "\tpxor    %xmm6, %xmm7\n"
        "\tpxor    %xmm6, %xmm4\n"
        "\tpxor    %xmm6, %xmm1\n"
        "\tpxor    %xmm5, %xmm6\n"
        "\tpxor    %xmm5, %xmm3\n"
        "\tpxor    %xmm5, %xmm0\n"
        "\tpxor    %xmm4, %xmm7\n"
        "\tpxor    %xmm4, %xmm6\n"
        "\tpxor    %xmm4, %xmm3\n"
        "\tpxor    %xmm4, %xmm2\n"
        "\tpxor    %xmm4, %xmm1\n"
        "\tpxor    %xmm4, %xmm0\n"
        "\tpxor    %xmm3, %xmm7\n"
        "\tpxor    %xmm3, %xmm5\n"
        "\tpxor    %xmm3, %xmm4\n"
        "\tpxor    %xmm3, %xmm2\n"
        "\tpxor    %xmm3, %xmm0\n"
        "\tpxor    %xmm4, %xmm3\n"
        "\tpxor    %xmm2, %xmm7\n"
        "\tpxor    %xmm1, %xmm6\n"
        "\tpxor    %xmm0, %xmm5\n"
    );
}

static void gf8mul_00100010(void)
{
    __asm__ __volatile__
    (
        "\tpxor    %xmm7, %xmm6\n"
        "\tpxor    %xmm7, %xmm4\n"
        "\tpxor    %xmm7, %xmm0\n"
        "\tpxor    %xmm6, %xmm7\n"
        "\tpxor    %xmm6, %xmm2\n"
        "\tpxor    %xmm5, %xmm7\n"
        "\tpxor    %xmm5, %xmm6\n"
        "\tpxor    %xmm5, %xmm4\n"
        "\tpxor    %xmm5, %xmm2\n"
        "\tpxor    %xmm5, %xmm1\n"
        "\tpxor    %xmm5, %xmm0\n"
        "\tpxor    %xmm6, %xmm5\n"
        "\tpxor    %xmm4, %xmm7\n"
        "\tpxor    %xmm4, %xmm5\n"
        "\tpxor    %xmm4, %xmm2\n"
        "\tpxor    %xmm4, %xmm0\n"
        "\tpxor    %xmm3, %xmm5\n"
        "\tpxor    %xmm3, %xmm0\n"
        "\tpxor    %xmm2, %xmm7\n"
        "\tpxor    %xmm2, %xmm3\n"
        "\tpxor    %xmm3, %xmm2\n"
        "\tpxor    %xmm1, %xmm6\n"
        "\tpxor    %xmm1, %xmm2\n"
        "\tpxor    %xmm2, %xmm1\n"
        "\tpxor    %xmm0, %xmm5\n"
        "\tpxor    %xmm0, %xmm1\n"
        "\tpxor    %xmm1, %xmm0\n"
    );
}

static void gf8mul_00100011(void)
{
    __asm__ __volatile__
    (
        "\tpxor    %xmm7, %xmm6\n"
        "\tpxor    %xmm7, %xmm5\n"
        "\tpxor    %xmm7, %xmm3\n"
        "\tpxor    %xmm7, %xmm2\n"
        "\tpxor    %xmm7, %xmm1\n"
        "\tpxor    %xmm7, %xmm0\n"
        "\tpxor    %xmm6, %xmm7\n"
        "\tpxor    %xmm6, %xmm5\n"
        "\tpxor    %xmm6, %xmm3\n"
        "\tpxor    %xmm6, %xmm2\n"
        "\tpxor    %xmm6, %xmm1\n"
        "\tpxor    %xmm6, %xmm0\n"
        "\tpxor    %xmm5, %xmm7\n"
        "\tpxor    %xmm5, %xmm6\n"
        "\tpxor    %xmm5, %xmm3\n"
        "\tpxor    %xmm5, %xmm2\n"
        "\tpxor    %xmm5, %xmm1\n"
        "\tpxor    %xmm5, %xmm0\n"
        "\tpxor    %xmm4, %xmm7\n"
        "\tpxor    %xmm4, %xmm6\n"
        "\tpxor    %xmm4, %xmm2\n"
        "\tpxor    %xmm4, %xmm1\n"
        "\tpxor    %xmm6, %xmm4\n"
        "\tpxor    %xmm3, %xmm6\n"
        "\tpxor    %xmm3, %xmm5\n"
        "\tpxor    %xmm3, %xmm1\n"
        "\tpxor    %xmm3, %xmm0\n"
        "\tpxor    %xmm5, %xmm3\n"
        "\tpxor    %xmm2, %xmm7\n"
        "\tpxor    %xmm2, %xmm3\n"
        "\tpxor    %xmm1, %xmm6\n"
        "\tpxor    %xmm1, %xmm2\n"
        "\tpxor    %xmm0, %xmm5\n"
        "\tpxor    %xmm0, %xmm1\n"
    );
}

static void gf8mul_00100100(void)
{
    __asm__ __volatile__
    (
        "\tpxor    %xmm7, %xmm6\n"
        "\tpxor    %xmm7, %xmm5\n"
        "\tpxor    %xmm7, %xmm4\n"
        "\tpxor    %xmm7, %xmm1\n"
        "\tpxor    %xmm7, %xmm0\n"
        "\tpxor    %xmm6, %xmm7\n"
        "\tpxor    %xmm6, %xmm3\n"
        "\tpxor    %xmm6, %xmm1\n"
        "\tpxor    %xmm7, %xmm6\n"
        "\tpxor    %xmm5, %xmm6\n"
        "\tpxor    %xmm5, %xmm2\n"
        "\tpxor    %xmm5, %xmm0\n"
        "\tpxor    %xmm6, %xmm5\n"
        "\tpxor    %xmm4, %xmm7\n"
        "\tpxor    %xmm4, %xmm3\n"
        "\tpxor    %xmm4, %xmm2\n"
        "\tpxor    %xmm4, %xmm1\n"
        "\tpxor    %xmm3, %xmm6\n"
        "\tpxor    %xmm3, %xmm5\n"
        "\tpxor    %xmm3, %xmm4\n"
        "\tpxor    %xmm3, %xmm1\n"
        "\tpxor    %xmm4, %xmm3\n"
        "\tpxor    %xmm2, %xmm7\n"
        "\tpxor    %xmm2, %xmm4\n"
        "\tpxor    %xmm4, %xmm2\n"
        "\tpxor    %xmm1, %xmm6\n"
        "\tpxor    %xmm1, %xmm3\n"
        "\tpxor    %xmm3, %xmm1\n"
        "\tpxor    %xmm0, %xmm5\n"
        "\tpxor    %xmm0, %xmm2\n"
        "\tpxor    %xmm2, %xmm0\n"
    );
}

static void gf8mul_00100101(void)
{
    __asm__ __volatile__
    (
        "\tpxor    %xmm7, %xmm6\n"
        "\tpxor    %xmm7, %xmm5\n"
        "\tpxor    %xmm7, %xmm4\n"
        "\tpxor    %xmm7, %xmm3\n"
        "\tpxor    %xmm7, %xmm2\n"
        "\tpxor    %xmm7, %xmm0\n"
        "\tpxor    %xmm6, %xmm7\n"
        "\tpxor    %xmm6, %xmm3\n"
        "\tpxor    %xmm7, %xmm6\n"
        "\tpxor    %xmm5, %xmm6\n"
        "\tpxor    %xmm5, %xmm2\n"
        "\tpxor    %xmm6, %xmm5\n"
        "\tpxor    %xmm4, %xmm5\n"
        "\tpxor    %xmm4, %xmm1\n"
        "\tpxor    %xmm5, %xmm4\n"
        "\tpxor    %xmm3, %xmm4\n"
        "\tpxor    %xmm3, %xmm0\n"
        "\tpxor    %xmm4, %xmm3\n"
        "\tpxor    %xmm2, %xmm7\n"
        "\tpxor    %xmm2, %xmm4\n"
        "\tpxor    %xmm1, %xmm6\n"
        "\tpxor    %xmm1, %xmm3\n"
        "\tpxor    %xmm0, %xmm5\n"
        "\tpxor    %xmm0, %xmm2\n"
    );
}

static void gf8mul_00100110(void)
{
    __asm__ __volatile__
    (
        "\tpxor    %xmm7, %xmm1\n"
        "\tpxor    %xmm7, %xmm0\n"
        "\tpxor    %xmm6, %xmm7\n"
        "\tpxor    %xmm6, %xmm3\n"
        "\tpxor    %xmm6, %xmm2\n"
        "\tpxor    %xmm5, %xmm6\n"
        "\tpxor    %xmm5, %xmm2\n"
        "\tpxor    %xmm5, %xmm1\n"
        "\tpxor    %xmm4, %xmm5\n"
        "\tpxor    %xmm4, %xmm1\n"
        "\tpxor    %xmm4, %xmm0\n"
        "\tpxor    %xmm3, %xmm7\n"
        "\tpxor    %xmm3, %xmm6\n"
        "\tpxor    %xmm3, %xmm4\n"
        "\tpxor    %xmm3, %xmm2\n"
        "\tpxor    %xmm3, %xmm1\n"
        "\tpxor    %xmm3, %xmm0\n"
        "\tpxor    %xmm2, %xmm7\n"
        "\tpxor    %xmm2, %xmm4\n"
        "\tpxor    %xmm2, %xmm3\n"
        "\tpxor    %xmm3, %xmm2\n"
        "\tpxor    %xmm1, %xmm6\n"
        "\tpxor    %xmm1, %xmm3\n"
        "\tpxor    %xmm1, %xmm2\n"
        "\tpxor    %xmm2, %xmm1\n"
        "\tpxor    %xmm0, %xmm5\n"
        "\tpxor    %xmm0, %xmm2\n"
        "\tpxor    %xmm0, %xmm1\n"
        "\tpxor    %xmm1, %xmm0\n"
    );
}

static void gf8mul_00100111(void)
{
    __asm__ __volatile__
    (
        "\tpxor    %xmm7, %xmm3\n"
        "\tpxor    %xmm7, %xmm2\n"
        "\tpxor    %xmm7, %xmm1\n"
        "\tpxor    %xmm6, %xmm2\n"
        "\tpxor    %xmm6, %xmm1\n"
        "\tpxor    %xmm6, %xmm0\n"
        "\tpxor    %xmm5, %xmm7\n"
        "\tpxor    %xmm5, %xmm1\n"
        "\tpxor    %xmm5, %xmm0\n"
        "\tpxor    %xmm4, %xmm7\n"
        "\tpxor    %xmm4, %xmm6\n"
        "\tpxor    %xmm4, %xmm0\n"
        "\tpxor    %xmm3, %xmm7\n"
        "\tpxor    %xmm3, %xmm6\n"
        "\tpxor    %xmm3, %xmm5\n"
        "\tpxor    %xmm2, %xmm6\n"
        "\tpxor    %xmm2, %xmm5\n"
        "\tpxor    %xmm2, %xmm4\n"
        "\tpxor    %xmm1, %xmm5\n"
        "\tpxor    %xmm1, %xmm4\n"
        "\tpxor    %xmm1, %xmm3\n"
        "\tpxor    %xmm0, %xmm4\n"
        "\tpxor    %xmm0, %xmm3\n"
        "\tpxor    %xmm0, %xmm2\n"
        "\tpxor    %xmm7, %xmm3\n"
        "\tpxor    %xmm7, %xmm1\n"
        "\tpxor    %xmm7, %xmm0\n"
        "\tpxor    %xmm6, %xmm7\n"
        "\tpxor    %xmm5, %xmm6\n"
        "\tpxor    %xmm4, %xmm5\n"
        "\tpxor    %xmm3, %xmm4\n"
        "\tpxor    %xmm2, %xmm3\n"
        "\tpxor    %xmm1, %xmm2\n"
        "\tpxor    %xmm0, %xmm1\n"
    );
}

static void gf8mul_00101000(void)
{
    __asm__ __volatile__
    (
        "\tpxor    %xmm7, %xmm5\n"
        "\tpxor    %xmm7, %xmm1\n"
        "\tpxor    %xmm6, %xmm4\n"
        "\tpxor    %xmm6, %xmm0\n"
        "\tpxor    %xmm5, %xmm6\n"
        "\tpxor    %xmm5, %xmm3\n"
        "\tpxor    %xmm5, %xmm1\n"
        "\tpxor    %xmm4, %xmm7\n"
        "\tpxor    %xmm4, %xmm0\n"
        "\tpxor    %xmm3, %xmm7\n"
        "\tpxor    %xmm3, %xmm5\n"
        "\tpxor    %xmm3, %xmm2\n"
        "\tpxor    %xmm3, %xmm1\n"
        "\tpxor    %xmm2, %xmm7\n"
        "\tpxor    %xmm2, %xmm5\n"
        "\tpxor    %xmm5, %xmm2\n"
        "\tpxor    %xmm1, %xmm6\n"
        "\tpxor    %xmm1, %xmm4\n"
        "\tpxor    %xmm4, %xmm1\n"
        "\tpxor    %xmm0, %xmm5\n"
        "\tpxor    %xmm0, %xmm3\n"
        "\tpxor    %xmm3, %xmm0\n"
    );
}

static void gf8mul_00101001(void)
{
    __asm__ __volatile__
    (
        "\tpxor    %xmm7, %xmm2\n"
        "\tpxor    %xmm6, %xmm1\n"
        "\tpxor    %xmm5, %xmm0\n"
        "\tpxor    %xmm4, %xmm7\n"
        "\tpxor    %xmm4, %xmm3\n"
        "\tpxor    %xmm4, %xmm1\n"
        "\tpxor    %xmm3, %xmm6\n"
        "\tpxor    %xmm3, %xmm2\n"
        "\tpxor    %xmm3, %xmm0\n"
        "\tpxor    %xmm2, %xmm7\n"
        "\tpxor    %xmm2, %xmm4\n"
        "\tpxor    %xmm1, %xmm7\n"
        "\tpxor    %xmm1, %xmm6\n"
        "\tpxor    %xmm1, %xmm2\n"
        "\tpxor    %xmm2, %xmm1\n"
        "\tpxor    %xmm0, %xmm7\n"
        "\tpxor    %xmm0, %xmm6\n"
        "\tpxor    %xmm0, %xmm5\n"
        "\tpxor    %xmm0, %xmm3\n"
        "\tpxor    %xmm0, %xmm2\n"
        "\tpxor    %xmm2, %xmm0\n"
        "\tpxor    %xmm7, %xmm3\n"
        "\tpxor    %xmm7, %xmm1\n"
        "\tpxor    %xmm7, %xmm0\n"
        "\tpxor    %xmm6, %xmm7\n"
        "\tpxor    %xmm5, %xmm6\n"
        "\tpxor    %xmm4, %xmm5\n"
        "\tpxor    %xmm3, %xmm4\n"
        "\tpxor    %xmm2, %xmm3\n"
        "\tpxor    %xmm1, %xmm2\n"
        "\tpxor    %xmm0, %xmm1\n"
    );
}

static void gf8mul_00101010(void)
{
    __asm__ __volatile__
    (
        "\tpxor    %xmm7, %xmm6\n"
        "\tpxor    %xmm7, %xmm5\n"
        "\tpxor    %xmm7, %xmm4\n"
        "\tpxor    %xmm7, %xmm3\n"
        "\tpxor    %xmm7, %xmm2\n"
        "\tpxor    %xmm6, %xmm7\n"
        "\tpxor    %xmm6, %xmm5\n"
        "\tpxor    %xmm6, %xmm3\n"
        "\tpxor    %xmm6, %xmm2\n"
        "\tpxor    %xmm6, %xmm1\n"
        "\tpxor    %xmm6, %xmm0\n"
        "\tpxor    %xmm5, %xmm7\n"
        "\tpxor    %xmm5, %xmm4\n"
        "\tpxor    %xmm5, %xmm3\n"
        "\tpxor    %xmm5, %xmm1\n"
        "\tpxor    %xmm5, %xmm0\n"
        "\tpxor    %xmm7, %xmm5\n"
        "\tpxor    %xmm4, %xmm7\n"
        "\tpxor    %xmm4, %xmm5\n"
        "\tpxor    %xmm4, %xmm0\n"
        "\tpxor    %xmm3, %xmm7\n"
        "\tpxor    %xmm3, %xmm6\n"
        "\tpxor    %xmm3, %xmm2\n"
        "\tpxor    %xmm3, %xmm0\n"
        "\tpxor    %xmm2, %xmm7\n"
        "\tpxor    %xmm2, %xmm5\n"
        "\tpxor    %xmm2, %xmm3\n"
        "\tpxor    %xmm3, %xmm2\n"
        "\tpxor    %xmm1, %xmm6\n"
        "\tpxor    %xmm1, %xmm4\n"
        "\tpxor    %xmm1, %xmm2\n"
        "\tpxor    %xmm2, %xmm1\n"
        "\tpxor    %xmm0, %xmm5\n"
        "\tpxor    %xmm0, %xmm3\n"
        "\tpxor    %xmm0, %xmm1\n"
        "\tpxor    %xmm1, %xmm0\n"
    );
}

static void gf8mul_00101011(void)
{
    __asm__ __volatile__
    (
        "\tpxor    %xmm7, %xmm6\n"
        "\tpxor    %xmm7, %xmm5\n"
        "\tpxor    %xmm7, %xmm3\n"
        "\tpxor    %xmm7, %xmm2\n"
        "\tpxor    %xmm7, %xmm1\n"
        "\tpxor    %xmm6, %xmm5\n"
        "\tpxor    %xmm6, %xmm4\n"
        "\tpxor    %xmm6, %xmm2\n"
        "\tpxor    %xmm6, %xmm1\n"
        "\tpxor    %xmm6, %xmm0\n"
        "\tpxor    %xmm5, %xmm7\n"
        "\tpxor    %xmm5, %xmm6\n"
        "\tpxor    %xmm5, %xmm3\n"
        "\tpxor    %xmm5, %xmm2\n"
        "\tpxor    %xmm4, %xmm6\n"
        "\tpxor    %xmm4, %xmm5\n"
        "\tpxor    %xmm4, %xmm2\n"
        "\tpxor    %xmm4, %xmm1\n"
        "\tpxor    %xmm3, %xmm5\n"
        "\tpxor    %xmm3, %xmm4\n"
        "\tpxor    %xmm3, %xmm1\n"
        "\tpxor    %xmm3, %xmm0\n"
        "\tpxor    %xmm2, %xmm7\n"
        "\tpxor    %xmm2, %xmm5\n"
        "\tpxor    %xmm2, %xmm3\n"
        "\tpxor    %xmm1, %xmm6\n"
        "\tpxor    %xmm1, %xmm4\n"
        "\tpxor    %xmm1, %xmm2\n"
        "\tpxor    %xmm0, %xmm5\n"
        "\tpxor    %xmm0, %xmm3\n"
        "\tpxor    %xmm0, %xmm1\n"
    );
}

static void gf8mul_00101100(void)
{
    __asm__ __volatile__
    (
        "\tpxor    %xmm7, %xmm6\n"
        "\tpxor    %xmm7, %xmm5\n"
        "\tpxor    %xmm7, %xmm3\n"
        "\tpxor    %xmm7, %xmm1\n"
        "\tpxor    %xmm6, %xmm5\n"
        "\tpxor    %xmm6, %xmm4\n"
        "\tpxor    %xmm6, %xmm2\n"
        "\tpxor    %xmm6, %xmm0\n"
        "\tpxor    %xmm5, %xmm7\n"
        "\tpxor    %xmm5, %xmm6\n"
        "\tpxor    %xmm5, %xmm3\n"
        "\tpxor    %xmm5, %xmm1\n"
        "\tpxor    %xmm5, %xmm0\n"
        "\tpxor    %xmm4, %xmm7\n"
        "\tpxor    %xmm4, %xmm6\n"
        "\tpxor    %xmm4, %xmm3\n"
        "\tpxor    %xmm4, %xmm1\n"
        "\tpxor    %xmm3, %xmm7\n"
        "\tpxor    %xmm3, %xmm4\n"
        "\tpxor    %xmm3, %xmm2\n"
        "\tpxor    %xmm3, %xmm1\n"
        "\tpxor    %xmm4, %xmm3\n"
        "\tpxor    %xmm2, %xmm7\n"
        "\tpxor    %xmm2, %xmm5\n"
        "\tpxor    %xmm2, %xmm4\n"
        "\tpxor    %xmm4, %xmm2\n"
        "\tpxor    %xmm1, %xmm6\n"
        "\tpxor    %xmm1, %xmm4\n"
        "\tpxor    %xmm1, %xmm3\n"
        "\tpxor    %xmm3, %xmm1\n"
        "\tpxor    %xmm0, %xmm5\n"
        "\tpxor    %xmm0, %xmm3\n"
        "\tpxor    %xmm0, %xmm2\n"
        "\tpxor    %xmm2, %xmm0\n"
    );
}

static void gf8mul_00101101(void)
{
    __asm__ __volatile__
    (
        "\tpxor    %xmm7, %xmm6\n"
        "\tpxor    %xmm7, %xmm5\n"
        "\tpxor    %xmm7, %xmm4\n"
        "\tpxor    %xmm7, %xmm3\n"
        "\tpxor    %xmm7, %xmm1\n"
        "\tpxor    %xmm6, %xmm5\n"
        "\tpxor    %xmm6, %xmm4\n"
        "\tpxor    %xmm6, %xmm3\n"
        "\tpxor    %xmm6, %xmm2\n"
        "\tpxor    %xmm6, %xmm0\n"
        "\tpxor    %xmm5, %xmm7\n"
        "\tpxor    %xmm5, %xmm6\n"
        "\tpxor    %xmm5, %xmm4\n"
        "\tpxor    %xmm5, %xmm2\n"
        "\tpxor    %xmm5, %xmm1\n"
        "\tpxor    %xmm5, %xmm0\n"
        "\tpxor    %xmm4, %xmm7\n"
        "\tpxor    %xmm4, %xmm5\n"
        "\tpxor    %xmm4, %xmm1\n"
        "\tpxor    %xmm3, %xmm6\n"
        "\tpxor    %xmm3, %xmm4\n"
        "\tpxor    %xmm3, %xmm0\n"
        "\tpxor    %xmm2, %xmm7\n"
        "\tpxor    %xmm2, %xmm5\n"
        "\tpxor    %xmm2, %xmm4\n"
        "\tpxor    %xmm1, %xmm6\n"
        "\tpxor    %xmm1, %xmm4\n"
        "\tpxor    %xmm1, %xmm3\n"
        "\tpxor    %xmm0, %xmm5\n"
        "\tpxor    %xmm0, %xmm3\n"
        "\tpxor    %xmm0, %xmm2\n"
    );
}

static void gf8mul_00101110(void)
{
    __asm__ __volatile__
    (
        "\tpxor    %xmm7, %xmm5\n"
        "\tpxor    %xmm7, %xmm4\n"
        "\tpxor    %xmm7, %xmm3\n"
        "\tpxor    %xmm6, %xmm4\n"
        "\tpxor    %xmm6, %xmm3\n"
        "\tpxor    %xmm6, %xmm2\n"
        "\tpxor    %xmm5, %xmm3\n"
        "\tpxor    %xmm5, %xmm2\n"
        "\tpxor    %xmm5, %xmm1\n"
        "\tpxor    %xmm4, %xmm2\n"
        "\tpxor    %xmm4, %xmm1\n"
        "\tpxor    %xmm4, %xmm0\n"
        "\tpxor    %xmm3, %xmm4\n"
        "\tpxor    %xmm3, %xmm1\n"
        "\tpxor    %xmm3, %xmm0\n"
        "\tpxor    %xmm2, %xmm7\n"
        "\tpxor    %xmm2, %xmm5\n"
        "\tpxor    %xmm2, %xmm4\n"
        "\tpxor    %xmm2, %xmm3\n"
        "\tpxor    %xmm3, %xmm2\n"
        "\tpxor    %xmm1, %xmm6\n"
        "\tpxor    %xmm1, %xmm4\n"
        "\tpxor    %xmm1, %xmm3\n"
        "\tpxor    %xmm1, %xmm2\n"
        "\tpxor    %xmm2, %xmm1\n"
        "\tpxor    %xmm0, %xmm5\n"
        "\tpxor    %xmm0, %xmm3\n"
        "\tpxor    %xmm0, %xmm2\n"
        "\tpxor    %xmm0, %xmm1\n"
        "\tpxor    %xmm1, %xmm0\n"
    );
}

static void gf8mul_00101111(void)
{
    __asm__ __volatile__
    (
        "\tpxor    %xmm7, %xmm5\n"
        "\tpxor    %xmm7, %xmm4\n"
        "\tpxor    %xmm7, %xmm2\n"
        "\tpxor    %xmm7, %xmm1\n"
        "\tpxor    %xmm7, %xmm0\n"
        "\tpxor    %xmm6, %xmm7\n"
        "\tpxor    %xmm6, %xmm5\n"
        "\tpxor    %xmm6, %xmm4\n"
        "\tpxor    %xmm6, %xmm2\n"
        "\tpxor    %xmm5, %xmm7\n"
        "\tpxor    %xmm5, %xmm6\n"
        "\tpxor    %xmm5, %xmm4\n"
        "\tpxor    %xmm5, %xmm2\n"
        "\tpxor    %xmm5, %xmm0\n"
        "\tpxor    %xmm6, %xmm5\n"
        "\tpxor    %xmm4, %xmm7\n"
        "\tpxor    %xmm4, %xmm3\n"
        "\tpxor    %xmm4, %xmm0\n"
        "\tpxor    %xmm7, %xmm4\n"
        "\tpxor    %xmm3, %xmm7\n"
        "\tpxor    %xmm3, %xmm5\n"
        "\tpxor    %xmm3, %xmm2\n"
        "\tpxor    %xmm3, %xmm1\n"
        "\tpxor    %xmm3, %xmm0\n"
        "\tpxor    %xmm2, %xmm7\n"
        "\tpxor    %xmm2, %xmm5\n"
        "\tpxor    %xmm2, %xmm4\n"
        "\tpxor    %xmm2, %xmm3\n"
        "\tpxor    %xmm1, %xmm6\n"
        "\tpxor    %xmm1, %xmm4\n"
        "\tpxor    %xmm1, %xmm3\n"
        "\tpxor    %xmm1, %xmm2\n"
        "\tpxor    %xmm0, %xmm5\n"
        "\tpxor    %xmm0, %xmm3\n"
        "\tpxor    %xmm0, %xmm2\n"
        "\tpxor    %xmm0, %xmm1\n"
    );
}

static void gf8mul_00110000(void)
{
    __asm__ __volatile__
    (
        "\tpxor    %xmm7, %xmm2\n"
        "\tpxor    %xmm7, %xmm1\n"
        "\tpxor    %xmm6, %xmm1\n"
        "\tpxor    %xmm6, %xmm0\n"
        "\tpxor    %xmm5, %xmm7\n"
        "\tpxor    %xmm5, %xmm3\n"
        "\tpxor    %xmm5, %xmm0\n"
        "\tpxor    %xmm4, %xmm7\n"
        "\tpxor    %xmm4, %xmm6\n"
        "\tpxor    %xmm4, %xmm3\n"
        "\tpxor    %xmm4, %xmm2\n"
        "\tpxor    %xmm3, %xmm7\n"
        "\tpxor    %xmm7, %xmm3\n"
        "\tpxor    %xmm2, %xmm6\n"
        "\tpxor    %xmm6, %xmm2\n"
        "\tpxor    %xmm1, %xmm5\n"
        "\tpxor    %xmm5, %xmm1\n"
        "\tpxor    %xmm0, %xmm4\n"
        "\tpxor    %xmm4, %xmm0\n"
        "\tpxor    %xmm7, %xmm3\n"
        "\tpxor    %xmm7, %xmm1\n"
        "\tpxor    %xmm7, %xmm0\n"
        "\tpxor    %xmm6, %xmm7\n"
        "\tpxor    %xmm5, %xmm6\n"
        "\tpxor    %xmm4, %xmm5\n"
        "\tpxor    %xmm3, %xmm4\n"
        "\tpxor    %xmm2, %xmm3\n"
        "\tpxor    %xmm1, %xmm2\n"
        "\tpxor    %xmm0, %xmm1\n"
    );
}

static void gf8mul_00110001(void)
{
    __asm__ __volatile__
    (
        "\tpxor    %xmm7, %xmm6\n"
        "\tpxor    %xmm7, %xmm5\n"
        "\tpxor    %xmm7, %xmm2\n"
        "\tpxor    %xmm7, %xmm1\n"
        "\tpxor    %xmm7, %xmm0\n"
        "\tpxor    %xmm6, %xmm7\n"
        "\tpxor    %xmm6, %xmm5\n"
        "\tpxor    %xmm6, %xmm1\n"
        "\tpxor    %xmm5, %xmm7\n"
        "\tpxor    %xmm5, %xmm3\n"
        "\tpxor    %xmm5, %xmm1\n"
        "\tpxor    %xmm5, %xmm0\n"
        "\tpxor    %xmm4, %xmm7\n"
        "\tpxor    %xmm4, %xmm6\n"
        "\tpxor    %xmm4, %xmm3\n"
        "\tpxor    %xmm4, %xmm1\n"
        "\tpxor    %xmm6, %xmm4\n"
        "\tpxor    %xmm3, %xmm6\n"
        "\tpxor    %xmm3, %xmm5\n"
        "\tpxor    %xmm3, %xmm2\n"
        "\tpxor    %xmm3, %xmm0\n"
        "\tpxor    %xmm5, %xmm3\n"
        "\tpxor    %xmm2, %xmm7\n"
        "\tpxor    %xmm2, %xmm6\n"
        "\tpxor    %xmm1, %xmm6\n"
        "\tpxor    %xmm1, %xmm5\n"
        "\tpxor    %xmm0, %xmm5\n"
        "\tpxor    %xmm0, %xmm4\n"
    );
}

static void gf8mul_00110010(void)
{
    __asm__ __volatile__
    (
        "\tpxor    %xmm7, %xmm4\n"
        "\tpxor    %xmm7, %xmm3\n"
        "\tpxor    %xmm7, %xmm0\n"
        "\tpxor    %xmm6, %xmm7\n"
        "\tpxor    %xmm6, %xmm4\n"
        "\tpxor    %xmm6, %xmm3\n"
        "\tpxor    %xmm6, %xmm1\n"
        "\tpxor    %xmm6, %xmm0\n"
        "\tpxor    %xmm5, %xmm7\n"
        "\tpxor    %xmm5, %xmm6\n"
        "\tpxor    %xmm5, %xmm4\n"
        "\tpxor    %xmm5, %xmm3\n"
        "\tpxor    %xmm5, %xmm1\n"
        "\tpxor    %xmm4, %xmm6\n"
        "\tpxor    %xmm4, %xmm5\n"
        "\tpxor    %xmm4, %xmm3\n"
        "\tpxor    %xmm4, %xmm2\n"
        "\tpxor    %xmm4, %xmm0\n"
        "\tpxor    %xmm3, %xmm7\n"
        "\tpxor    %xmm3, %xmm5\n"
        "\tpxor    %xmm3, %xmm4\n"
        "\tpxor    %xmm3, %xmm0\n"
        "\tpxor    %xmm2, %xmm7\n"
        "\tpxor    %xmm2, %xmm6\n"
        "\tpxor    %xmm2, %xmm3\n"
        "\tpxor    %xmm3, %xmm2\n"
        "\tpxor    %xmm1, %xmm6\n"
        "\tpxor    %xmm1, %xmm5\n"
        "\tpxor    %xmm1, %xmm2\n"
        "\tpxor    %xmm2, %xmm1\n"
        "\tpxor    %xmm0, %xmm5\n"
        "\tpxor    %xmm0, %xmm4\n"
        "\tpxor    %xmm0, %xmm1\n"
        "\tpxor    %xmm1, %xmm0\n"
    );
}

static void gf8mul_00110011(void)
{
    __asm__ __volatile__
    (
        "\tpxor    %xmm7, %xmm4\n"
        "\tpxor    %xmm7, %xmm3\n"
        "\tpxor    %xmm7, %xmm2\n"
        "\tpxor    %xmm7, %xmm1\n"
        "\tpxor    %xmm6, %xmm3\n"
        "\tpxor    %xmm6, %xmm2\n"
        "\tpxor    %xmm6, %xmm1\n"
        "\tpxor    %xmm6, %xmm0\n"
        "\tpxor    %xmm5, %xmm7\n"
        "\tpxor    %xmm5, %xmm4\n"
        "\tpxor    %xmm5, %xmm2\n"
        "\tpxor    %xmm5, %xmm1\n"
        "\tpxor    %xmm5, %xmm0\n"
        "\tpxor    %xmm4, %xmm5\n"
        "\tpxor    %xmm4, %xmm3\n"
        "\tpxor    %xmm4, %xmm2\n"
        "\tpxor    %xmm4, %xmm1\n"
        "\tpxor    %xmm3, %xmm7\n"
        "\tpxor    %xmm3, %xmm6\n"
        "\tpxor    %xmm3, %xmm4\n"
        "\tpxor    %xmm3, %xmm1\n"
        "\tpxor    %xmm3, %xmm0\n"
        "\tpxor    %xmm4, %xmm3\n"
        "\tpxor    %xmm2, %xmm7\n"
        "\tpxor    %xmm2, %xmm6\n"
        "\tpxor    %xmm2, %xmm3\n"
        "\tpxor    %xmm1, %xmm6\n"
        "\tpxor    %xmm1, %xmm5\n"
        "\tpxor    %xmm1, %xmm2\n"
        "\tpxor    %xmm0, %xmm5\n"
        "\tpxor    %xmm0, %xmm4\n"
        "\tpxor    %xmm0, %xmm1\n"
    );
}

static void gf8mul_00110100(void)
{
    __asm__ __volatile__
    (
        "\tpxor    %xmm7, %xmm6\n"
        "\tpxor    %xmm7, %xmm5\n"
        "\tpxor    %xmm7, %xmm2\n"
        "\tpxor    %xmm6, %xmm5\n"
        "\tpxor    %xmm6, %xmm4\n"
        "\tpxor    %xmm6, %xmm1\n"
        "\tpxor    %xmm5, %xmm4\n"
        "\tpxor    %xmm5, %xmm3\n"
        "\tpxor    %xmm5, %xmm0\n"
        "\tpxor    %xmm4, %xmm7\n"
        "\tpxor    %xmm4, %xmm6\n"
        "\tpxor    %xmm4, %xmm2\n"
        "\tpxor    %xmm4, %xmm1\n"
        "\tpxor    %xmm3, %xmm7\n"
        "\tpxor    %xmm3, %xmm6\n"
        "\tpxor    %xmm3, %xmm4\n"
        "\tpxor    %xmm3, %xmm1\n"
        "\tpxor    %xmm4, %xmm3\n"
        "\tpxor    %xmm2, %xmm7\n"
        "\tpxor    %xmm2, %xmm6\n"
        "\tpxor    %xmm2, %xmm4\n"
        "\tpxor    %xmm4, %xmm2\n"
        "\tpxor    %xmm1, %xmm6\n"
        "\tpxor    %xmm1, %xmm5\n"
        "\tpxor    %xmm1, %xmm3\n"
        "\tpxor    %xmm3, %xmm1\n"
        "\tpxor    %xmm0, %xmm5\n"
        "\tpxor    %xmm0, %xmm4\n"
        "\tpxor    %xmm0, %xmm2\n"
        "\tpxor    %xmm2, %xmm0\n"
    );
}

static void gf8mul_00110101(void)
{
    __asm__ __volatile__
    (
        "\tpxor    %xmm7, %xmm6\n"
        "\tpxor    %xmm7, %xmm4\n"
        "\tpxor    %xmm7, %xmm2\n"
        "\tpxor    %xmm7, %xmm0\n"
        "\tpxor    %xmm6, %xmm7\n"
        "\tpxor    %xmm6, %xmm4\n"
        "\tpxor    %xmm6, %xmm3\n"
        "\tpxor    %xmm6, %xmm2\n"
        "\tpxor    %xmm6, %xmm1\n"
        "\tpxor    %xmm6, %xmm0\n"
        "\tpxor    %xmm7, %xmm6\n"
        "\tpxor    %xmm5, %xmm7\n"
        "\tpxor    %xmm5, %xmm6\n"
        "\tpxor    %xmm5, %xmm4\n"
        "\tpxor    %xmm5, %xmm3\n"
        "\tpxor    %xmm5, %xmm2\n"
        "\tpxor    %xmm4, %xmm7\n"
        "\tpxor    %xmm4, %xmm5\n"
        "\tpxor    %xmm4, %xmm3\n"
        "\tpxor    %xmm4, %xmm1\n"
        "\tpxor    %xmm5, %xmm4\n"
        "\tpxor    %xmm3, %xmm7\n"
        "\tpxor    %xmm3, %xmm0\n"
        "\tpxor    %xmm7, %xmm3\n"
        "\tpxor    %xmm2, %xmm7\n"
        "\tpxor    %xmm2, %xmm6\n"
        "\tpxor    %xmm2, %xmm4\n"
        "\tpxor    %xmm1, %xmm6\n"
        "\tpxor    %xmm1, %xmm5\n"
        "\tpxor    %xmm1, %xmm3\n"
        "\tpxor    %xmm0, %xmm5\n"
        "\tpxor    %xmm0, %xmm4\n"
        "\tpxor    %xmm0, %xmm2\n"
    );
}

static void gf8mul_00110110(void)
{
    __asm__ __volatile__
    (
        "\tpxor    %xmm7, %xmm4\n"
        "\tpxor    %xmm7, %xmm2\n"
        "\tpxor    %xmm6, %xmm3\n"
        "\tpxor    %xmm6, %xmm1\n"
        "\tpxor    %xmm5, %xmm2\n"
        "\tpxor    %xmm5, %xmm0\n"
        "\tpxor    %xmm4, %xmm7\n"
        "\tpxor    %xmm4, %xmm5\n"
        "\tpxor    %xmm4, %xmm3\n"
        "\tpxor    %xmm4, %xmm0\n"
        "\tpxor    %xmm3, %xmm7\n"
        "\tpxor    %xmm3, %xmm4\n"
        "\tpxor    %xmm4, %xmm3\n"
        "\tpxor    %xmm2, %xmm6\n"
        "\tpxor    %xmm2, %xmm3\n"
        "\tpxor    %xmm3, %xmm2\n"
        "\tpxor    %xmm1, %xmm5\n"
        "\tpxor    %xmm1, %xmm2\n"
        "\tpxor    %xmm2, %xmm1\n"
        "\tpxor    %xmm0, %xmm4\n"
        "\tpxor    %xmm0, %xmm1\n"
        "\tpxor    %xmm1, %xmm0\n"
        "\tpxor    %xmm7, %xmm3\n"
        "\tpxor    %xmm7, %xmm1\n"
        "\tpxor    %xmm7, %xmm0\n"
        "\tpxor    %xmm6, %xmm7\n"
        "\tpxor    %xmm5, %xmm6\n"
        "\tpxor    %xmm4, %xmm5\n"
        "\tpxor    %xmm3, %xmm4\n"
        "\tpxor    %xmm2, %xmm3\n"
        "\tpxor    %xmm1, %xmm2\n"
        "\tpxor    %xmm0, %xmm1\n"
    );
}

static void gf8mul_00110111(void)
{
    __asm__ __volatile__
    (
        "\tpxor    %xmm7, %xmm6\n"
        "\tpxor    %xmm7, %xmm5\n"
        "\tpxor    %xmm7, %xmm4\n"
        "\tpxor    %xmm7, %xmm3\n"
        "\tpxor    %xmm7, %xmm0\n"
        "\tpxor    %xmm6, %xmm7\n"
        "\tpxor    %xmm6, %xmm5\n"
        "\tpxor    %xmm6, %xmm3\n"
        "\tpxor    %xmm6, %xmm2\n"
        "\tpxor    %xmm6, %xmm1\n"
        "\tpxor    %xmm5, %xmm7\n"
        "\tpxor    %xmm5, %xmm4\n"
        "\tpxor    %xmm5, %xmm3\n"
        "\tpxor    %xmm5, %xmm2\n"
        "\tpxor    %xmm7, %xmm5\n"
        "\tpxor    %xmm4, %xmm6\n"
        "\tpxor    %xmm4, %xmm3\n"
        "\tpxor    %xmm4, %xmm2\n"
        "\tpxor    %xmm4, %xmm1\n"
        "\tpxor    %xmm6, %xmm4\n"
        "\tpxor    %xmm3, %xmm5\n"
        "\tpxor    %xmm3, %xmm2\n"
        "\tpxor    %xmm3, %xmm1\n"
        "\tpxor    %xmm3, %xmm0\n"
        "\tpxor    %xmm5, %xmm3\n"
        "\tpxor    %xmm2, %xmm7\n"
        "\tpxor    %xmm2, %xmm6\n"
        "\tpxor    %xmm2, %xmm4\n"
        "\tpxor    %xmm2, %xmm3\n"
        "\tpxor    %xmm1, %xmm6\n"
        "\tpxor    %xmm1, %xmm5\n"
        "\tpxor    %xmm1, %xmm3\n"
        "\tpxor    %xmm1, %xmm2\n"
        "\tpxor    %xmm0, %xmm5\n"
        "\tpxor    %xmm0, %xmm4\n"
        "\tpxor    %xmm0, %xmm2\n"
        "\tpxor    %xmm0, %xmm1\n"
    );
}

static void gf8mul_00111000(void)
{
    __asm__ __volatile__
    (
        "\tpxor    %xmm7, %xmm6\n"
        "\tpxor    %xmm7, %xmm4\n"
        "\tpxor    %xmm7, %xmm2\n"
        "\tpxor    %xmm7, %xmm1\n"
        "\tpxor    %xmm6, %xmm7\n"
        "\tpxor    %xmm6, %xmm5\n"
        "\tpxor    %xmm6, %xmm3\n"
        "\tpxor    %xmm6, %xmm2\n"
        "\tpxor    %xmm6, %xmm1\n"
        "\tpxor    %xmm6, %xmm0\n"
        "\tpxor    %xmm7, %xmm6\n"
        "\tpxor    %xmm5, %xmm6\n"
        "\tpxor    %xmm5, %xmm4\n"
        "\tpxor    %xmm5, %xmm2\n"
        "\tpxor    %xmm5, %xmm1\n"
        "\tpxor    %xmm4, %xmm7\n"
        "\tpxor    %xmm4, %xmm6\n"
        "\tpxor    %xmm4, %xmm3\n"
        "\tpxor    %xmm4, %xmm2\n"
        "\tpxor    %xmm4, %xmm1\n"
        "\tpxor    %xmm4, %xmm0\n"
        "\tpxor    %xmm3, %xmm7\n"
        "\tpxor    %xmm3, %xmm5\n"
        "\tpxor    %xmm3, %xmm1\n"
        "\tpxor    %xmm2, %xmm7\n"
        "\tpxor    %xmm2, %xmm6\n"
        "\tpxor    %xmm2, %xmm5\n"
        "\tpxor    %xmm5, %xmm2\n"
        "\tpxor    %xmm1, %xmm6\n"
        "\tpxor    %xmm1, %xmm5\n"
        "\tpxor    %xmm1, %xmm4\n"
        "\tpxor    %xmm4, %xmm1\n"
        "\tpxor    %xmm0, %xmm5\n"
        "\tpxor    %xmm0, %xmm4\n"
        "\tpxor    %xmm0, %xmm3\n"
        "\tpxor    %xmm3, %xmm0\n"
    );
}

static void gf8mul_00111001(void)
{
    __asm__ __volatile__
    (
        "\tpxor    %xmm7, %xmm6\n"
        "\tpxor    %xmm7, %xmm4\n"
        "\tpxor    %xmm6, %xmm5\n"
        "\tpxor    %xmm6, %xmm3\n"
        "\tpxor    %xmm5, %xmm4\n"
        "\tpxor    %xmm5, %xmm2\n"
        "\tpxor    %xmm4, %xmm3\n"
        "\tpxor    %xmm4, %xmm1\n"
        "\tpxor    %xmm3, %xmm2\n"
        "\tpxor    %xmm3, %xmm0\n"
        "\tpxor    %xmm2, %xmm7\n"
        "\tpxor    %xmm2, %xmm6\n"
        "\tpxor    %xmm2, %xmm5\n"
        "\tpxor    %xmm1, %xmm6\n"
        "\tpxor    %xmm1, %xmm5\n"
        "\tpxor    %xmm1, %xmm4\n"
        "\tpxor    %xmm0, %xmm5\n"
        "\tpxor    %xmm0, %xmm4\n"
        "\tpxor    %xmm0, %xmm3\n"
    );
}

static void gf8mul_00111010(void)
{
    __asm__ __volatile__
    (
        "\tpxor    %xmm7, %xmm6\n"
        "\tpxor    %xmm7, %xmm5\n"
        "\tpxor    %xmm7, %xmm3\n"
        "\tpxor    %xmm7, %xmm1\n"
        "\tpxor    %xmm6, %xmm7\n"
        "\tpxor    %xmm6, %xmm2\n"
        "\tpxor    %xmm6, %xmm1\n"
        "\tpxor    %xmm6, %xmm0\n"
        "\tpxor    %xmm7, %xmm6\n"
        "\tpxor    %xmm5, %xmm7\n"
        "\tpxor    %xmm5, %xmm6\n"
        "\tpxor    %xmm5, %xmm4\n"
        "\tpxor    %xmm5, %xmm1\n"
        "\tpxor    %xmm4, %xmm6\n"
        "\tpxor    %xmm4, %xmm5\n"
        "\tpxor    %xmm4, %xmm3\n"
        "\tpxor    %xmm4, %xmm0\n"
        "\tpxor    %xmm3, %xmm4\n"
        "\tpxor    %xmm3, %xmm2\n"
        "\tpxor    %xmm3, %xmm0\n"
        "\tpxor    %xmm2, %xmm7\n"
        "\tpxor    %xmm2, %xmm6\n"
        "\tpxor    %xmm2, %xmm5\n"
        "\tpxor    %xmm2, %xmm3\n"
        "\tpxor    %xmm3, %xmm2\n"
        "\tpxor    %xmm1, %xmm6\n"
        "\tpxor    %xmm1, %xmm5\n"
        "\tpxor    %xmm1, %xmm4\n"
        "\tpxor    %xmm1, %xmm2\n"
        "\tpxor    %xmm2, %xmm1\n"
        "\tpxor    %xmm0, %xmm5\n"
        "\tpxor    %xmm0, %xmm4\n"
        "\tpxor    %xmm0, %xmm3\n"
        "\tpxor    %xmm0, %xmm1\n"
        "\tpxor    %xmm1, %xmm0\n"
    );
}

static void gf8mul_00111011(void)
{
    __asm__ __volatile__
    (
        "\tpxor    %xmm7, %xmm5\n"
        "\tpxor    %xmm7, %xmm3\n"
        "\tpxor    %xmm7, %xmm2\n"
        "\tpxor    %xmm7, %xmm1\n"
        "\tpxor    %xmm6, %xmm4\n"
        "\tpxor    %xmm6, %xmm2\n"
        "\tpxor    %xmm6, %xmm1\n"
        "\tpxor    %xmm6, %xmm0\n"
        "\tpxor    %xmm5, %xmm7\n"
        "\tpxor    %xmm5, %xmm3\n"
        "\tpxor    %xmm5, %xmm1\n"
        "\tpxor    %xmm7, %xmm5\n"
        "\tpxor    %xmm4, %xmm6\n"
        "\tpxor    %xmm4, %xmm2\n"
        "\tpxor    %xmm4, %xmm0\n"
        "\tpxor    %xmm6, %xmm4\n"
        "\tpxor    %xmm3, %xmm7\n"
        "\tpxor    %xmm3, %xmm1\n"
        "\tpxor    %xmm3, %xmm0\n"
        "\tpxor    %xmm2, %xmm7\n"
        "\tpxor    %xmm2, %xmm6\n"
        "\tpxor    %xmm2, %xmm5\n"
        "\tpxor    %xmm2, %xmm3\n"
        "\tpxor    %xmm1, %xmm6\n"
        "\tpxor    %xmm1, %xmm5\n"
        "\tpxor    %xmm1, %xmm4\n"
        "\tpxor    %xmm1, %xmm2\n"
        "\tpxor    %xmm0, %xmm5\n"
        "\tpxor    %xmm0, %xmm4\n"
        "\tpxor    %xmm0, %xmm3\n"
        "\tpxor    %xmm0, %xmm1\n"
    );
}

static void gf8mul_00111100(void)
{
    __asm__ __volatile__
    (
        "\tpxor    %xmm7, %xmm6\n"
        "\tpxor    %xmm7, %xmm5\n"
        "\tpxor    %xmm7, %xmm4\n"
        "\tpxor    %xmm7, %xmm1\n"
        "\tpxor    %xmm7, %xmm0\n"
        "\tpxor    %xmm6, %xmm7\n"
        "\tpxor    %xmm6, %xmm5\n"
        "\tpxor    %xmm6, %xmm2\n"
        "\tpxor    %xmm6, %xmm1\n"
        "\tpxor    %xmm5, %xmm7\n"
        "\tpxor    %xmm5, %xmm4\n"
        "\tpxor    %xmm5, %xmm2\n"
        "\tpxor    %xmm5, %xmm1\n"
        "\tpxor    %xmm5, %xmm0\n"
        "\tpxor    %xmm7, %xmm5\n"
        "\tpxor    %xmm4, %xmm7\n"
        "\tpxor    %xmm4, %xmm1\n"
        "\tpxor    %xmm3, %xmm6\n"
        "\tpxor    %xmm3, %xmm5\n"
        "\tpxor    %xmm3, %xmm4\n"
        "\tpxor    %xmm3, %xmm2\n"
        "\tpxor    %xmm3, %xmm1\n"
        "\tpxor    %xmm4, %xmm3\n"
        "\tpxor    %xmm2, %xmm7\n"
        "\tpxor    %xmm2, %xmm6\n"
        "\tpxor    %xmm2, %xmm5\n"
        "\tpxor    %xmm2, %xmm4\n"
        "\tpxor    %xmm4, %xmm2\n"
        "\tpxor    %xmm1, %xmm6\n"
        "\tpxor    %xmm1, %xmm5\n"
        "\tpxor    %xmm1, %xmm4\n"
        "\tpxor    %xmm1, %xmm3\n"
        "\tpxor    %xmm3, %xmm1\n"
        "\tpxor    %xmm0, %xmm5\n"
        "\tpxor    %xmm0, %xmm4\n"
        "\tpxor    %xmm0, %xmm3\n"
        "\tpxor    %xmm0, %xmm2\n"
        "\tpxor    %xmm2, %xmm0\n"
    );
}

static void gf8mul_00111101(void)
{
    __asm__ __volatile__
    (
        "\tpxor    %xmm7, %xmm6\n"
        "\tpxor    %xmm7, %xmm5\n"
        "\tpxor    %xmm7, %xmm4\n"
        "\tpxor    %xmm7, %xmm2\n"
        "\tpxor    %xmm6, %xmm7\n"
        "\tpxor    %xmm6, %xmm3\n"
        "\tpxor    %xmm6, %xmm2\n"
        "\tpxor    %xmm6, %xmm1\n"
        "\tpxor    %xmm7, %xmm6\n"
        "\tpxor    %xmm5, %xmm6\n"
        "\tpxor    %xmm5, %xmm2\n"
        "\tpxor    %xmm5, %xmm1\n"
        "\tpxor    %xmm5, %xmm0\n"
        "\tpxor    %xmm6, %xmm5\n"
        "\tpxor    %xmm4, %xmm7\n"
        "\tpxor    %xmm4, %xmm5\n"
        "\tpxor    %xmm4, %xmm1\n"
        "\tpxor    %xmm4, %xmm0\n"
        "\tpxor    %xmm3, %xmm7\n"
        "\tpxor    %xmm3, %xmm6\n"
        "\tpxor    %xmm3, %xmm0\n"
        "\tpxor    %xmm2, %xmm7\n"
        "\tpxor    %xmm2, %xmm6\n"
        "\tpxor    %xmm2, %xmm5\n"
        "\tpxor    %xmm2, %xmm4\n"
        "\tpxor    %xmm1, %xmm6\n"
        "\tpxor    %xmm1, %xmm5\n"
        "\tpxor    %xmm1, %xmm4\n"
        "\tpxor    %xmm1, %xmm3\n"
        "\tpxor    %xmm0, %xmm5\n"
        "\tpxor    %xmm0, %xmm4\n"
        "\tpxor    %xmm0, %xmm3\n"
        "\tpxor    %xmm0, %xmm2\n"
    );
}

static void gf8mul_00111110(void)
{
    __asm__ __volatile__
    (
        "\tpxor    %xmm7, %xmm6\n"
        "\tpxor    %xmm6, %xmm5\n"
        "\tpxor    %xmm5, %xmm4\n"
        "\tpxor    %xmm4, %xmm3\n"
        "\tpxor    %xmm3, %xmm2\n"
        "\tpxor    %xmm2, %xmm1\n"
        "\tpxor    %xmm1, %xmm0\n"
        "\tpxor    %xmm0, %xmm7\n"
        "\tpxor    %xmm0, %xmm6\n"
        "\tpxor    %xmm0, %xmm5\n"
        "\tpxor    %xmm0, %xmm4\n"
        "\tpxor    %xmm0, %xmm2\n"
        "\tpxor    %xmm7, %xmm4\n"
        "\tpxor    %xmm7, %xmm2\n"
        "\tpxor    %xmm7, %xmm1\n"
        "\tpxor    %xmm7, %xmm0\n"
        "\tpxor    %xmm6, %xmm7\n"
        "\tpxor    %xmm6, %xmm4\n"
        "\tpxor    %xmm6, %xmm1\n"
        "\tpxor    %xmm5, %xmm6\n"
        "\tpxor    %xmm5, %xmm3\n"
        "\tpxor    %xmm5, %xmm0\n"
        "\tpxor    %xmm4, %xmm7\n"
        "\tpxor    %xmm4, %xmm6\n"
        "\tpxor    %xmm4, %xmm3\n"
        "\tpxor    %xmm4, %xmm2\n"
        "\tpxor    %xmm4, %xmm1\n"
        "\tpxor    %xmm4, %xmm0\n"
        "\tpxor    %xmm3, %xmm7\n"
        "\tpxor    %xmm3, %xmm5\n"
        "\tpxor    %xmm3, %xmm4\n"
        "\tpxor    %xmm3, %xmm2\n"
        "\tpxor    %xmm3, %xmm0\n"
        "\tpxor    %xmm4, %xmm3\n"
        "\tpxor    %xmm2, %xmm7\n"
        "\tpxor    %xmm1, %xmm6\n"
        "\tpxor    %xmm0, %xmm5\n"
    );
}

static void gf8mul_00111111(void)
{
    __asm__ __volatile__
    (
        "\tpxor    %xmm7, %xmm6\n"
        "\tpxor    %xmm7, %xmm4\n"
        "\tpxor    %xmm7, %xmm3\n"
        "\tpxor    %xmm7, %xmm0\n"
        "\tpxor    %xmm6, %xmm7\n"
        "\tpxor    %xmm6, %xmm3\n"
        "\tpxor    %xmm6, %xmm1\n"
        "\tpxor    %xmm5, %xmm7\n"
        "\tpxor    %xmm5, %xmm6\n"
        "\tpxor    %xmm5, %xmm4\n"
        "\tpxor    %xmm5, %xmm3\n"
        "\tpxor    %xmm5, %xmm1\n"
        "\tpxor    %xmm5, %xmm0\n"
        "\tpxor    %xmm6, %xmm5\n"
        "\tpxor    %xmm4, %xmm7\n"
        "\tpxor    %xmm4, %xmm5\n"
        "\tpxor    %xmm4, %xmm3\n"
        "\tpxor    %xmm4, %xmm2\n"
        "\tpxor    %xmm4, %xmm1\n"
        "\tpxor    %xmm3, %xmm6\n"
        "\tpxor    %xmm3, %xmm4\n"
        "\tpxor    %xmm3, %xmm2\n"
        "\tpxor    %xmm3, %xmm1\n"
        "\tpxor    %xmm3, %xmm0\n"
        "\tpxor    %xmm2, %xmm7\n"
        "\tpxor    %xmm2, %xmm6\n"
        "\tpxor    %xmm2, %xmm5\n"
        "\tpxor    %xmm2, %xmm4\n"
        "\tpxor    %xmm2, %xmm3\n"
        "\tpxor    %xmm1, %xmm6\n"
        "\tpxor    %xmm1, %xmm5\n"
        "\tpxor    %xmm1, %xmm4\n"
        "\tpxor    %xmm1, %xmm3\n"
        "\tpxor    %xmm1, %xmm2\n"
        "\tpxor    %xmm0, %xmm5\n"
        "\tpxor    %xmm0, %xmm4\n"
        "\tpxor    %xmm0, %xmm3\n"
        "\tpxor    %xmm0, %xmm2\n"
        "\tpxor    %xmm0, %xmm1\n"
    );
}

static void gf8mul_01000000(void)
{
    __asm__ __volatile__
    (
        "\tpxor    %xmm7, %xmm6\n"
        "\tpxor    %xmm7, %xmm4\n"
        "\tpxor    %xmm7, %xmm2\n"
        "\tpxor    %xmm7, %xmm1\n"
        "\tpxor    %xmm7, %xmm0\n"
        "\tpxor    %xmm6, %xmm7\n"
        "\tpxor    %xmm6, %xmm5\n"
        "\tpxor    %xmm6, %xmm3\n"
        "\tpxor    %xmm6, %xmm2\n"
        "\tpxor    %xmm6, %xmm1\n"
        "\tpxor    %xmm7, %xmm6\n"
        "\tpxor    %xmm5, %xmm6\n"
        "\tpxor    %xmm5, %xmm4\n"
        "\tpxor    %xmm5, %xmm2\n"
        "\tpxor    %xmm5, %xmm1\n"
        "\tpxor    %xmm5, %xmm0\n"
        "\tpxor    %xmm6, %xmm5\n"
        "\tpxor    %xmm4, %xmm7\n"
        "\tpxor    %xmm4, %xmm6\n"
        "\tpxor    %xmm4, %xmm3\n"
        "\tpxor    %xmm4, %xmm2\n"
        "\tpxor    %xmm4, %xmm1\n"
        "\tpxor    %xmm3, %xmm7\n"
        "\tpxor    %xmm3, %xmm5\n"
        "\tpxor    %xmm3, %xmm4\n"
        "\tpxor    %xmm3, %xmm1\n"
        "\tpxor    %xmm2, %xmm6\n"
        "\tpxor    %xmm2, %xmm4\n"
        "\tpxor    %xmm2, %xmm3\n"
        "\tpxor    %xmm2, %xmm0\n"
        "\tpxor    %xmm1, %xmm7\n"
        "\tpxor    %xmm7, %xmm1\n"
        "\tpxor    %xmm0, %xmm6\n"
        "\tpxor    %xmm6, %xmm0\n"
    );
}

static void gf8mul_01000001(void)
{
    __asm__ __volatile__
    (
        "\tpxor    %xmm7, %xmm5\n"
        "\tpxor    %xmm7, %xmm4\n"
        "\tpxor    %xmm7, %xmm3\n"
        "\tpxor    %xmm7, %xmm2\n"
        "\tpxor    %xmm6, %xmm4\n"
        "\tpxor    %xmm6, %xmm3\n"
        "\tpxor    %xmm6, %xmm2\n"
        "\tpxor    %xmm6, %xmm1\n"
        "\tpxor    %xmm5, %xmm3\n"
        "\tpxor    %xmm5, %xmm2\n"
        "\tpxor    %xmm5, %xmm1\n"
        "\tpxor    %xmm5, %xmm0\n"
        "\tpxor    %xmm4, %xmm5\n"
        "\tpxor    %xmm4, %xmm2\n"
        "\tpxor    %xmm4, %xmm0\n"
        "\tpxor    %xmm3, %xmm7\n"
        "\tpxor    %xmm3, %xmm5\n"
        "\tpxor    %xmm3, %xmm4\n"
        "\tpxor    %xmm3, %xmm1\n"
        "\tpxor    %xmm4, %xmm3\n"
        "\tpxor    %xmm2, %xmm6\n"
        "\tpxor    %xmm2, %xmm4\n"
        "\tpxor    %xmm2, %xmm3\n"
        "\tpxor    %xmm2, %xmm0\n"
        "\tpxor    %xmm3, %xmm2\n"
        "\tpxor    %xmm1, %xmm7\n"
        "\tpxor    %xmm0, %xmm6\n"
    );
}

static void gf8mul_01000010(void)
{
    __asm__ __volatile__
    (
        "\tpxor    %xmm7, %xmm6\n"
        "\tpxor    %xmm7, %xmm2\n"
        "\tpxor    %xmm7, %xmm0\n"
        "\tpxor    %xmm6, %xmm7\n"
        "\tpxor    %xmm6, %xmm4\n"
        "\tpxor    %xmm6, %xmm3\n"
        "\tpxor    %xmm6, %xmm2\n"
        "\tpxor    %xmm6, %xmm1\n"
        "\tpxor    %xmm6, %xmm0\n"
        "\tpxor    %xmm5, %xmm7\n"
        "\tpxor    %xmm5, %xmm6\n"
        "\tpxor    %xmm5, %xmm3\n"
        "\tpxor    %xmm5, %xmm0\n"
        "\tpxor    %xmm6, %xmm5\n"
        "\tpxor    %xmm4, %xmm7\n"
        "\tpxor    %xmm4, %xmm6\n"
        "\tpxor    %xmm4, %xmm1\n"
        "\tpxor    %xmm3, %xmm6\n"
        "\tpxor    %xmm3, %xmm5\n"
        "\tpxor    %xmm3, %xmm0\n"
        "\tpxor    %xmm2, %xmm6\n"
        "\tpxor    %xmm2, %xmm4\n"
        "\tpxor    %xmm2, %xmm0\n"
        "\tpxor    %xmm1, %xmm7\n"
        "\tpxor    %xmm1, %xmm2\n"
        "\tpxor    %xmm2, %xmm1\n"
        "\tpxor    %xmm0, %xmm6\n"
        "\tpxor    %xmm0, %xmm1\n"
        "\tpxor    %xmm1, %xmm0\n"
    );
}

static void gf8mul_01000011(void)
{
    __asm__ __volatile__
    (
        "\tpxor    %xmm7, %xmm6\n"
        "\tpxor    %xmm7, %xmm2\n"
        "\tpxor    %xmm7, %xmm1\n"
        "\tpxor    %xmm6, %xmm5\n"
        "\tpxor    %xmm6, %xmm1\n"
        "\tpxor    %xmm6, %xmm0\n"
        "\tpxor    %xmm5, %xmm7\n"
        "\tpxor    %xmm5, %xmm6\n"
        "\tpxor    %xmm5, %xmm3\n"
        "\tpxor    %xmm5, %xmm2\n"
        "\tpxor    %xmm5, %xmm0\n"
        "\tpxor    %xmm4, %xmm7\n"
        "\tpxor    %xmm4, %xmm6\n"
        "\tpxor    %xmm4, %xmm5\n"
        "\tpxor    %xmm4, %xmm3\n"
        "\tpxor    %xmm4, %xmm1\n"
        "\tpxor    %xmm5, %xmm4\n"
        "\tpxor    %xmm3, %xmm6\n"
        "\tpxor    %xmm3, %xmm5\n"
        "\tpxor    %xmm3, %xmm4\n"
        "\tpxor    %xmm3, %xmm2\n"
        "\tpxor    %xmm3, %xmm0\n"
        "\tpxor    %xmm4, %xmm3\n"
        "\tpxor    %xmm2, %xmm7\n"
        "\tpxor    %xmm2, %xmm6\n"
        "\tpxor    %xmm2, %xmm4\n"
        "\tpxor    %xmm2, %xmm1\n"
        "\tpxor    %xmm2, %xmm0\n"
        "\tpxor    %xmm1, %xmm7\n"
        "\tpxor    %xmm1, %xmm2\n"
        "\tpxor    %xmm0, %xmm6\n"
        "\tpxor    %xmm0, %xmm1\n"
    );
}

static void gf8mul_01000100(void)
{
    __asm__ __volatile__
    (
        "\tpxor    %xmm7, %xmm6\n"
        "\tpxor    %xmm7, %xmm5\n"
        "\tpxor    %xmm7, %xmm2\n"
        "\tpxor    %xmm7, %xmm1\n"
        "\tpxor    %xmm6, %xmm5\n"
        "\tpxor    %xmm6, %xmm4\n"
        "\tpxor    %xmm6, %xmm1\n"
        "\tpxor    %xmm6, %xmm0\n"
        "\tpxor    %xmm5, %xmm7\n"
        "\tpxor    %xmm5, %xmm6\n"
        "\tpxor    %xmm5, %xmm1\n"
        "\tpxor    %xmm4, %xmm6\n"
        "\tpxor    %xmm4, %xmm5\n"
        "\tpxor    %xmm4, %xmm0\n"
        "\tpxor    %xmm3, %xmm4\n"
        "\tpxor    %xmm2, %xmm7\n"
        "\tpxor    %xmm2, %xmm1\n"
        "\tpxor    %xmm1, %xmm7\n"
        "\tpxor    %xmm1, %xmm3\n"
        "\tpxor    %xmm3, %xmm1\n"
        "\tpxor    %xmm0, %xmm6\n"
        "\tpxor    %xmm0, %xmm2\n"
        "\tpxor    %xmm2, %xmm0\n"
    );
}

static void gf8mul_01000101(void)
{
    __asm__ __volatile__
    (
        "\tpxor    %xmm7, %xmm6\n"
        "\tpxor    %xmm7, %xmm4\n"
        "\tpxor    %xmm7, %xmm2\n"
        "\tpxor    %xmm6, %xmm5\n"
        "\tpxor    %xmm6, %xmm3\n"
        "\tpxor    %xmm6, %xmm1\n"
        "\tpxor    %xmm5, %xmm4\n"
        "\tpxor    %xmm5, %xmm2\n"
        "\tpxor    %xmm5, %xmm0\n"
        "\tpxor    %xmm4, %xmm7\n"
        "\tpxor    %xmm4, %xmm6\n"
        "\tpxor    %xmm4, %xmm5\n"
        "\tpxor    %xmm4, %xmm3\n"
        "\tpxor    %xmm4, %xmm2\n"
        "\tpxor    %xmm4, %xmm1\n"
        "\tpxor    %xmm4, %xmm0\n"
        "\tpxor    %xmm3, %xmm7\n"
        "\tpxor    %xmm3, %xmm4\n"
        "\tpxor    %xmm3, %xmm1\n"
        "\tpxor    %xmm2, %xmm6\n"
        "\tpxor    %xmm2, %xmm3\n"
        "\tpxor    %xmm2, %xmm0\n"
        "\tpxor    %xmm1, %xmm7\n"
        "\tpxor    %xmm1, %xmm3\n"
        "\tpxor    %xmm0, %xmm6\n"
        "\tpxor    %xmm0, %xmm2\n"
    );
}

static void gf8mul_01000110(void)
{
    __asm__ __volatile__
    (
        "\tpxor    %xmm7, %xmm6\n"
        "\tpxor    %xmm7, %xmm5\n"
        "\tpxor    %xmm7, %xmm4\n"
        "\tpxor    %xmm7, %xmm3\n"
        "\tpxor    %xmm7, %xmm0\n"
        "\tpxor    %xmm6, %xmm7\n"
        "\tpxor    %xmm6, %xmm4\n"
        "\tpxor    %xmm6, %xmm1\n"
        "\tpxor    %xmm5, %xmm7\n"
        "\tpxor    %xmm5, %xmm6\n"
        "\tpxor    %xmm5, %xmm3\n"
        "\tpxor    %xmm5, %xmm1\n"
        "\tpxor    %xmm5, %xmm0\n"
        "\tpxor    %xmm4, %xmm6\n"
        "\tpxor    %xmm4, %xmm3\n"
        "\tpxor    %xmm4, %xmm0\n"
        "\tpxor    %xmm3, %xmm7\n"
        "\tpxor    %xmm3, %xmm6\n"
        "\tpxor    %xmm3, %xmm1\n"
        "\tpxor    %xmm3, %xmm0\n"
        "\tpxor    %xmm6, %xmm3\n"
        "\tpxor    %xmm2, %xmm7\n"
        "\tpxor    %xmm2, %xmm6\n"
        "\tpxor    %xmm2, %xmm3\n"
        "\tpxor    %xmm2, %xmm1\n"
        "\tpxor    %xmm2, %xmm0\n"
        "\tpxor    %xmm1, %xmm7\n"
        "\tpxor    %xmm1, %xmm3\n"
        "\tpxor    %xmm1, %xmm2\n"
        "\tpxor    %xmm2, %xmm1\n"
        "\tpxor    %xmm0, %xmm6\n"
        "\tpxor    %xmm0, %xmm2\n"
        "\tpxor    %xmm0, %xmm1\n"
        "\tpxor    %xmm1, %xmm0\n"
    );
}

static void gf8mul_01000111(void)
{
    __asm__ __volatile__
    (
        "\tpxor    %xmm7, %xmm5\n"
        "\tpxor    %xmm7, %xmm4\n"
        "\tpxor    %xmm7, %xmm3\n"
        "\tpxor    %xmm7, %xmm1\n"
        "\tpxor    %xmm6, %xmm4\n"
        "\tpxor    %xmm6, %xmm3\n"
        "\tpxor    %xmm6, %xmm2\n"
        "\tpxor    %xmm6, %xmm0\n"
        "\tpxor    %xmm5, %xmm7\n"
        "\tpxor    %xmm5, %xmm4\n"
        "\tpxor    %xmm5, %xmm3\n"
        "\tpxor    %xmm5, %xmm1\n"
        "\tpxor    %xmm7, %xmm5\n"
        "\tpxor    %xmm4, %xmm6\n"
        "\tpxor    %xmm4, %xmm3\n"
        "\tpxor    %xmm4, %xmm2\n"
        "\tpxor    %xmm4, %xmm0\n"
        "\tpxor    %xmm6, %xmm4\n"
        "\tpxor    %xmm3, %xmm7\n"
        "\tpxor    %xmm3, %xmm1\n"
        "\tpxor    %xmm2, %xmm7\n"
        "\tpxor    %xmm2, %xmm6\n"
        "\tpxor    %xmm2, %xmm3\n"
        "\tpxor    %xmm2, %xmm1\n"
        "\tpxor    %xmm2, %xmm0\n"
        "\tpxor    %xmm3, %xmm2\n"
        "\tpxor    %xmm1, %xmm7\n"
        "\tpxor    %xmm1, %xmm3\n"
        "\tpxor    %xmm1, %xmm2\n"
        "\tpxor    %xmm0, %xmm6\n"
        "\tpxor    %xmm0, %xmm2\n"
        "\tpxor    %xmm0, %xmm1\n"
    );
}

static void gf8mul_01001000(void)
{
    __asm__ __volatile__
    (
        "\tpxor    %xmm7, %xmm6\n"
        "\tpxor    %xmm7, %xmm5\n"
        "\tpxor    %xmm7, %xmm4\n"
        "\tpxor    %xmm7, %xmm2\n"
        "\tpxor    %xmm7, %xmm1\n"
        "\tpxor    %xmm7, %xmm0\n"
        "\tpxor    %xmm6, %xmm7\n"
        "\tpxor    %xmm6, %xmm5\n"
        "\tpxor    %xmm6, %xmm3\n"
        "\tpxor    %xmm6, %xmm1\n"
        "\tpxor    %xmm6, %xmm0\n"
        "\tpxor    %xmm7, %xmm6\n"
        "\tpxor    %xmm5, %xmm7\n"
        "\tpxor    %xmm5, %xmm2\n"
        "\tpxor    %xmm5, %xmm0\n"
        "\tpxor    %xmm7, %xmm5\n"
        "\tpxor    %xmm4, %xmm7\n"
        "\tpxor    %xmm4, %xmm6\n"
        "\tpxor    %xmm4, %xmm5\n"
        "\tpxor    %xmm4, %xmm3\n"
        "\tpxor    %xmm4, %xmm2\n"
        "\tpxor    %xmm4, %xmm0\n"
        "\tpxor    %xmm3, %xmm5\n"
        "\tpxor    %xmm3, %xmm4\n"
        "\tpxor    %xmm3, %xmm0\n"
        "\tpxor    %xmm4, %xmm3\n"
        "\tpxor    %xmm2, %xmm7\n"
        "\tpxor    %xmm2, %xmm5\n"
        "\tpxor    %xmm2, %xmm3\n"
        "\tpxor    %xmm2, %xmm1\n"
        "\tpxor    %xmm1, %xmm7\n"
        "\tpxor    %xmm1, %xmm4\n"
        "\tpxor    %xmm4, %xmm1\n"
        "\tpxor    %xmm0, %xmm6\n"
        "\tpxor    %xmm0, %xmm3\n"
        "\tpxor    %xmm3, %xmm0\n"
    );
}

static void gf8mul_01001001(void)
{
    __asm__ __volatile__
    (
        "\tpxor    %xmm7, %xmm6\n"
        "\tpxor    %xmm7, %xmm4\n"
        "\tpxor    %xmm7, %xmm3\n"
        "\tpxor    %xmm7, %xmm1\n"
        "\tpxor    %xmm6, %xmm7\n"
        "\tpxor    %xmm6, %xmm5\n"
        "\tpxor    %xmm6, %xmm2\n"
        "\tpxor    %xmm6, %xmm1\n"
        "\tpxor    %xmm6, %xmm0\n"
        "\tpxor    %xmm7, %xmm6\n"
        "\tpxor    %xmm5, %xmm7\n"
        "\tpxor    %xmm5, %xmm6\n"
        "\tpxor    %xmm5, %xmm4\n"
        "\tpxor    %xmm5, %xmm3\n"
        "\tpxor    %xmm5, %xmm2\n"
        "\tpxor    %xmm5, %xmm1\n"
        "\tpxor    %xmm4, %xmm7\n"
        "\tpxor    %xmm4, %xmm2\n"
        "\tpxor    %xmm4, %xmm0\n"
        "\tpxor    %xmm3, %xmm7\n"
        "\tpxor    %xmm3, %xmm6\n"
        "\tpxor    %xmm3, %xmm5\n"
        "\tpxor    %xmm3, %xmm1\n"
        "\tpxor    %xmm5, %xmm3\n"
        "\tpxor    %xmm2, %xmm6\n"
        "\tpxor    %xmm2, %xmm5\n"
        "\tpxor    %xmm2, %xmm4\n"
        "\tpxor    %xmm2, %xmm0\n"
        "\tpxor    %xmm4, %xmm2\n"
        "\tpxor    %xmm1, %xmm7\n"
        "\tpxor    %xmm1, %xmm4\n"
        "\tpxor    %xmm0, %xmm6\n"
        "\tpxor    %xmm0, %xmm3\n"
    );
}

static void gf8mul_01001010(void)
{
    __asm__ __volatile__
    (
        "\tpxor    %xmm7, %xmm6\n"
        "\tpxor    %xmm7, %xmm5\n"
        "\tpxor    %xmm7, %xmm4\n"
        "\tpxor    %xmm6, %xmm7\n"
        "\tpxor    %xmm6, %xmm5\n"
        "\tpxor    %xmm6, %xmm3\n"
        "\tpxor    %xmm7, %xmm6\n"
        "\tpxor    %xmm5, %xmm7\n"
        "\tpxor    %xmm5, %xmm2\n"
        "\tpxor    %xmm7, %xmm5\n"
        "\tpxor    %xmm4, %xmm6\n"
        "\tpxor    %xmm4, %xmm1\n"
        "\tpxor    %xmm6, %xmm4\n"
        "\tpxor    %xmm3, %xmm5\n"
        "\tpxor    %xmm3, %xmm0\n"
        "\tpxor    %xmm5, %xmm3\n"
        "\tpxor    %xmm2, %xmm6\n"
        "\tpxor    %xmm2, %xmm5\n"
        "\tpxor    %xmm2, %xmm4\n"
        "\tpxor    %xmm2, %xmm3\n"
        "\tpxor    %xmm2, %xmm0\n"
        "\tpxor    %xmm1, %xmm7\n"
        "\tpxor    %xmm1, %xmm4\n"
        "\tpxor    %xmm1, %xmm2\n"
        "\tpxor    %xmm2, %xmm1\n"
        "\tpxor    %xmm0, %xmm6\n"
        "\tpxor    %xmm0, %xmm3\n"
        "\tpxor    %xmm0, %xmm1\n"
        "\tpxor    %xmm1, %xmm0\n"
    );
}

static void gf8mul_01001011(void)
{
    __asm__ __volatile__
    (
        "\tpxor    %xmm7, %xmm5\n"
        "\tpxor    %xmm7, %xmm4\n"
        "\tpxor    %xmm7, %xmm3\n"
        "\tpxor    %xmm7, %xmm1\n"
        "\tpxor    %xmm7, %xmm0\n"
        "\tpxor    %xmm6, %xmm7\n"
        "\tpxor    %xmm6, %xmm5\n"
        "\tpxor    %xmm6, %xmm3\n"
        "\tpxor    %xmm5, %xmm6\n"
        "\tpxor    %xmm5, %xmm4\n"
        "\tpxor    %xmm5, %xmm2\n"
        "\tpxor    %xmm4, %xmm5\n"
        "\tpxor    %xmm4, %xmm3\n"
        "\tpxor    %xmm4, %xmm1\n"
        "\tpxor    %xmm3, %xmm4\n"
        "\tpxor    %xmm3, %xmm2\n"
        "\tpxor    %xmm3, %xmm0\n"
        "\tpxor    %xmm2, %xmm7\n"
        "\tpxor    %xmm2, %xmm6\n"
        "\tpxor    %xmm2, %xmm5\n"
        "\tpxor    %xmm2, %xmm3\n"
        "\tpxor    %xmm2, %xmm1\n"
        "\tpxor    %xmm2, %xmm0\n"
        "\tpxor    %xmm1, %xmm7\n"
        "\tpxor    %xmm1, %xmm4\n"
        "\tpxor    %xmm1, %xmm2\n"
        "\tpxor    %xmm0, %xmm6\n"
        "\tpxor    %xmm0, %xmm3\n"
        "\tpxor    %xmm0, %xmm1\n"
    );
}

static void gf8mul_01001100(void)
{
    __asm__ __volatile__
    (
        "\tpxor    %xmm7, %xmm6\n"
        "\tpxor    %xmm7, %xmm3\n"
        "\tpxor    %xmm7, %xmm2\n"
        "\tpxor    %xmm7, %xmm1\n"
        "\tpxor    %xmm7, %xmm0\n"
        "\tpxor    %xmm6, %xmm5\n"
        "\tpxor    %xmm6, %xmm3\n"
        "\tpxor    %xmm6, %xmm2\n"
        "\tpxor    %xmm6, %xmm1\n"
        "\tpxor    %xmm5, %xmm7\n"
        "\tpxor    %xmm5, %xmm6\n"
        "\tpxor    %xmm5, %xmm2\n"
        "\tpxor    %xmm5, %xmm1\n"
        "\tpxor    %xmm6, %xmm5\n"
        "\tpxor    %xmm4, %xmm6\n"
        "\tpxor    %xmm4, %xmm5\n"
        "\tpxor    %xmm4, %xmm1\n"
        "\tpxor    %xmm4, %xmm0\n"
        "\tpxor    %xmm5, %xmm4\n"
        "\tpxor    %xmm3, %xmm6\n"
        "\tpxor    %xmm3, %xmm4\n"
        "\tpxor    %xmm2, %xmm7\n"
        "\tpxor    %xmm2, %xmm5\n"
        "\tpxor    %xmm2, %xmm4\n"
        "\tpxor    %xmm2, %xmm1\n"
        "\tpxor    %xmm1, %xmm7\n"
        "\tpxor    %xmm1, %xmm4\n"
        "\tpxor    %xmm1, %xmm3\n"
        "\tpxor    %xmm3, %xmm1\n"
        "\tpxor    %xmm0, %xmm6\n"
        "\tpxor    %xmm0, %xmm3\n"
        "\tpxor    %xmm0, %xmm2\n"
        "\tpxor    %xmm2, %xmm0\n"
    );
}

static void gf8mul_01001101(void)
{
    __asm__ __volatile__
    (
        "\tpxor    %xmm7, %xmm6\n"
        "\tpxor    %xmm7, %xmm5\n"
        "\tpxor    %xmm7, %xmm3\n"
        "\tpxor    %xmm7, %xmm2\n"
        "\tpxor    %xmm7, %xmm0\n"
        "\tpxor    %xmm6, %xmm7\n"
        "\tpxor    %xmm6, %xmm4\n"
        "\tpxor    %xmm6, %xmm2\n"
        "\tpxor    %xmm6, %xmm1\n"
        "\tpxor    %xmm6, %xmm0\n"
        "\tpxor    %xmm5, %xmm7\n"
        "\tpxor    %xmm5, %xmm6\n"
        "\tpxor    %xmm5, %xmm3\n"
        "\tpxor    %xmm5, %xmm1\n"
        "\tpxor    %xmm5, %xmm0\n"
        "\tpxor    %xmm6, %xmm5\n"
        "\tpxor    %xmm4, %xmm7\n"
        "\tpxor    %xmm4, %xmm6\n"
        "\tpxor    %xmm4, %xmm2\n"
        "\tpxor    %xmm4, %xmm0\n"
        "\tpxor    %xmm6, %xmm4\n"
        "\tpxor    %xmm3, %xmm7\n"
        "\tpxor    %xmm3, %xmm6\n"
        "\tpxor    %xmm3, %xmm1\n"
        "\tpxor    %xmm2, %xmm6\n"
        "\tpxor    %xmm2, %xmm5\n"
        "\tpxor    %xmm2, %xmm0\n"
        "\tpxor    %xmm1, %xmm7\n"
        "\tpxor    %xmm1, %xmm4\n"
        "\tpxor    %xmm1, %xmm3\n"
        "\tpxor    %xmm0, %xmm6\n"
        "\tpxor    %xmm0, %xmm3\n"
        "\tpxor    %xmm0, %xmm2\n"
    );
}

static void gf8mul_01001110(void)
{
    __asm__ __volatile__
    (
        "\tpxor    %xmm7, %xmm6\n"
        "\tpxor    %xmm7, %xmm5\n"
        "\tpxor    %xmm7, %xmm2\n"
        "\tpxor    %xmm7, %xmm0\n"
        "\tpxor    %xmm6, %xmm7\n"
        "\tpxor    %xmm6, %xmm5\n"
        "\tpxor    %xmm6, %xmm4\n"
        "\tpxor    %xmm6, %xmm2\n"
        "\tpxor    %xmm6, %xmm0\n"
        "\tpxor    %xmm7, %xmm6\n"
        "\tpxor    %xmm5, %xmm7\n"
        "\tpxor    %xmm5, %xmm4\n"
        "\tpxor    %xmm5, %xmm3\n"
        "\tpxor    %xmm5, %xmm2\n"
        "\tpxor    %xmm5, %xmm0\n"
        "\tpxor    %xmm7, %xmm5\n"
        "\tpxor    %xmm4, %xmm7\n"
        "\tpxor    %xmm4, %xmm3\n"
        "\tpxor    %xmm4, %xmm0\n"
        "\tpxor    %xmm3, %xmm7\n"
        "\tpxor    %xmm3, %xmm4\n"
        "\tpxor    %xmm3, %xmm1\n"
        "\tpxor    %xmm3, %xmm0\n"
        "\tpxor    %xmm2, %xmm7\n"
        "\tpxor    %xmm2, %xmm6\n"
        "\tpxor    %xmm2, %xmm5\n"
        "\tpxor    %xmm2, %xmm4\n"
        "\tpxor    %xmm2, %xmm1\n"
        "\tpxor    %xmm2, %xmm0\n"
        "\tpxor    %xmm1, %xmm7\n"
        "\tpxor    %xmm1, %xmm4\n"
        "\tpxor    %xmm1, %xmm3\n"
        "\tpxor    %xmm1, %xmm2\n"
        "\tpxor    %xmm2, %xmm1\n"
        "\tpxor    %xmm0, %xmm6\n"
        "\tpxor    %xmm0, %xmm3\n"
        "\tpxor    %xmm0, %xmm2\n"
        "\tpxor    %xmm0, %xmm1\n"
        "\tpxor    %xmm1, %xmm0\n"
    );
}

static void gf8mul_01001111(void)
{
    __asm__ __volatile__
    (
        "\tpxor    %xmm7, %xmm6\n"
        "\tpxor    %xmm7, %xmm1\n"
        "\tpxor    %xmm6, %xmm7\n"
        "\tpxor    %xmm6, %xmm5\n"
        "\tpxor    %xmm6, %xmm2\n"
        "\tpxor    %xmm6, %xmm1\n"
        "\tpxor    %xmm7, %xmm6\n"
        "\tpxor    %xmm5, %xmm6\n"
        "\tpxor    %xmm5, %xmm4\n"
        "\tpxor    %xmm5, %xmm1\n"
        "\tpxor    %xmm5, %xmm0\n"
        "\tpxor    %xmm6, %xmm5\n"
        "\tpxor    %xmm4, %xmm6\n"
        "\tpxor    %xmm4, %xmm5\n"
        "\tpxor    %xmm4, %xmm2\n"
        "\tpxor    %xmm4, %xmm1\n"
        "\tpxor    %xmm4, %xmm0\n"
        "\tpxor    %xmm3, %xmm7\n"
        "\tpxor    %xmm3, %xmm6\n"
        "\tpxor    %xmm3, %xmm4\n"
        "\tpxor    %xmm3, %xmm1\n"
        "\tpxor    %xmm2, %xmm7\n"
        "\tpxor    %xmm2, %xmm6\n"
        "\tpxor    %xmm2, %xmm5\n"
        "\tpxor    %xmm2, %xmm4\n"
        "\tpxor    %xmm2, %xmm1\n"
        "\tpxor    %xmm2, %xmm0\n"
        "\tpxor    %xmm4, %xmm2\n"
        "\tpxor    %xmm1, %xmm7\n"
        "\tpxor    %xmm1, %xmm4\n"
        "\tpxor    %xmm1, %xmm3\n"
        "\tpxor    %xmm1, %xmm2\n"
        "\tpxor    %xmm0, %xmm6\n"
        "\tpxor    %xmm0, %xmm3\n"
        "\tpxor    %xmm0, %xmm2\n"
        "\tpxor    %xmm0, %xmm1\n"
    );
}

static void gf8mul_01010000(void)
{
    __asm__ __volatile__
    (
        "\tpxor    %xmm7, %xmm6\n"
        "\tpxor    %xmm7, %xmm5\n"
        "\tpxor    %xmm7, %xmm4\n"
        "\tpxor    %xmm7, %xmm2\n"
        "\tpxor    %xmm7, %xmm1\n"
        "\tpxor    %xmm7, %xmm0\n"
        "\tpxor    %xmm6, %xmm7\n"
        "\tpxor    %xmm6, %xmm4\n"
        "\tpxor    %xmm6, %xmm0\n"
        "\tpxor    %xmm7, %xmm6\n"
        "\tpxor    %xmm5, %xmm1\n"
        "\tpxor    %xmm5, %xmm0\n"
        "\tpxor    %xmm4, %xmm6\n"
        "\tpxor    %xmm4, %xmm5\n"
        "\tpxor    %xmm4, %xmm3\n"
        "\tpxor    %xmm4, %xmm1\n"
        "\tpxor    %xmm3, %xmm7\n"
        "\tpxor    %xmm3, %xmm6\n"
        "\tpxor    %xmm3, %xmm5\n"
        "\tpxor    %xmm3, %xmm0\n"
        "\tpxor    %xmm2, %xmm6\n"
        "\tpxor    %xmm2, %xmm4\n"
        "\tpxor    %xmm2, %xmm3\n"
        "\tpxor    %xmm1, %xmm7\n"
        "\tpxor    %xmm1, %xmm5\n"
        "\tpxor    %xmm5, %xmm1\n"
        "\tpxor    %xmm0, %xmm6\n"
        "\tpxor    %xmm0, %xmm4\n"
        "\tpxor    %xmm4, %xmm0\n"
    );
}

static void gf8mul_01010001(void)
{
    __asm__ __volatile__
    (
        "\tpxor    %xmm7, %xmm6\n"
        "\tpxor    %xmm7, %xmm5\n"
        "\tpxor    %xmm7, %xmm3\n"
        "\tpxor    %xmm7, %xmm2\n"
        "\tpxor    %xmm7, %xmm1\n"
        "\tpxor    %xmm7, %xmm0\n"
        "\tpxor    %xmm6, %xmm7\n"
        "\tpxor    %xmm6, %xmm4\n"
        "\tpxor    %xmm7, %xmm6\n"
        "\tpxor    %xmm5, %xmm6\n"
        "\tpxor    %xmm5, %xmm3\n"
        "\tpxor    %xmm6, %xmm5\n"
        "\tpxor    %xmm4, %xmm5\n"
        "\tpxor    %xmm4, %xmm2\n"
        "\tpxor    %xmm5, %xmm4\n"
        "\tpxor    %xmm3, %xmm4\n"
        "\tpxor    %xmm3, %xmm1\n"
        "\tpxor    %xmm4, %xmm3\n"
        "\tpxor    %xmm2, %xmm3\n"
        "\tpxor    %xmm2, %xmm0\n"
        "\tpxor    %xmm3, %xmm2\n"
        "\tpxor    %xmm1, %xmm7\n"
        "\tpxor    %xmm1, %xmm5\n"
        "\tpxor    %xmm0, %xmm6\n"
        "\tpxor    %xmm0, %xmm4\n"
    );
}

static void gf8mul_01010010(void)
{
    __asm__ __volatile__
    (
        "\tpxor    %xmm7, %xmm5\n"
        "\tpxor    %xmm7, %xmm4\n"
        "\tpxor    %xmm7, %xmm3\n"
        "\tpxor    %xmm7, %xmm2\n"
        "\tpxor    %xmm7, %xmm1\n"
        "\tpxor    %xmm7, %xmm0\n"
        "\tpxor    %xmm6, %xmm7\n"
        "\tpxor    %xmm6, %xmm5\n"
        "\tpxor    %xmm6, %xmm3\n"
        "\tpxor    %xmm6, %xmm2\n"
        "\tpxor    %xmm6, %xmm1\n"
        "\tpxor    %xmm5, %xmm7\n"
        "\tpxor    %xmm5, %xmm4\n"
        "\tpxor    %xmm5, %xmm3\n"
        "\tpxor    %xmm5, %xmm2\n"
        "\tpxor    %xmm5, %xmm1\n"
        "\tpxor    %xmm7, %xmm5\n"
        "\tpxor    %xmm4, %xmm6\n"
        "\tpxor    %xmm4, %xmm3\n"
        "\tpxor    %xmm4, %xmm2\n"
        "\tpxor    %xmm4, %xmm1\n"
        "\tpxor    %xmm4, %xmm0\n"
        "\tpxor    %xmm6, %xmm4\n"
        "\tpxor    %xmm3, %xmm7\n"
        "\tpxor    %xmm3, %xmm6\n"
        "\tpxor    %xmm3, %xmm5\n"
        "\tpxor    %xmm3, %xmm4\n"
        "\tpxor    %xmm3, %xmm0\n"
        "\tpxor    %xmm2, %xmm0\n"
        "\tpxor    %xmm1, %xmm7\n"
        "\tpxor    %xmm1, %xmm5\n"
        "\tpxor    %xmm1, %xmm2\n"
        "\tpxor    %xmm2, %xmm1\n"
        "\tpxor    %xmm0, %xmm6\n"
        "\tpxor    %xmm0, %xmm4\n"
        "\tpxor    %xmm0, %xmm1\n"
        "\tpxor    %xmm1, %xmm0\n"
    );
}

static void gf8mul_01010011(void)
{
    __asm__ __volatile__
    (
        "\tpxor    %xmm7, %xmm6\n"
        "\tpxor    %xmm7, %xmm5\n"
        "\tpxor    %xmm7, %xmm4\n"
        "\tpxor    %xmm7, %xmm3\n"
        "\tpxor    %xmm7, %xmm2\n"
        "\tpxor    %xmm7, %xmm0\n"
        "\tpxor    %xmm6, %xmm7\n"
        "\tpxor    %xmm6, %xmm4\n"
        "\tpxor    %xmm6, %xmm3\n"
        "\tpxor    %xmm6, %xmm2\n"
        "\tpxor    %xmm6, %xmm0\n"
        "\tpxor    %xmm7, %xmm6\n"
        "\tpxor    %xmm5, %xmm7\n"
        "\tpxor    %xmm5, %xmm4\n"
        "\tpxor    %xmm5, %xmm0\n"
        "\tpxor    %xmm4, %xmm7\n"
        "\tpxor    %xmm4, %xmm5\n"
        "\tpxor    %xmm4, %xmm3\n"
        "\tpxor    %xmm4, %xmm0\n"
        "\tpxor    %xmm5, %xmm4\n"
        "\tpxor    %xmm3, %xmm7\n"
        "\tpxor    %xmm3, %xmm5\n"
        "\tpxor    %xmm3, %xmm2\n"
        "\tpxor    %xmm3, %xmm0\n"
        "\tpxor    %xmm5, %xmm3\n"
        "\tpxor    %xmm2, %xmm7\n"
        "\tpxor    %xmm2, %xmm5\n"
        "\tpxor    %xmm2, %xmm1\n"
        "\tpxor    %xmm2, %xmm0\n"
        "\tpxor    %xmm1, %xmm7\n"
        "\tpxor    %xmm1, %xmm5\n"
        "\tpxor    %xmm1, %xmm2\n"
        "\tpxor    %xmm0, %xmm6\n"
        "\tpxor    %xmm0, %xmm4\n"
        "\tpxor    %xmm0, %xmm1\n"
    );
}

static void gf8mul_01010100(void)
{
    __asm__ __volatile__
    (
        "\tpxor    %xmm7, %xmm4\n"
        "\tpxor    %xmm7, %xmm1\n"
        "\tpxor    %xmm7, %xmm0\n"
        "\tpxor    %xmm6, %xmm7\n"
        "\tpxor    %xmm6, %xmm4\n"
        "\tpxor    %xmm6, %xmm2\n"
        "\tpxor    %xmm5, %xmm6\n"
        "\tpxor    %xmm5, %xmm3\n"
        "\tpxor    %xmm5, %xmm1\n"
        "\tpxor    %xmm4, %xmm5\n"
        "\tpxor    %xmm4, %xmm2\n"
        "\tpxor    %xmm4, %xmm0\n"
        "\tpxor    %xmm3, %xmm7\n"
        "\tpxor    %xmm3, %xmm4\n"
        "\tpxor    %xmm2, %xmm7\n"
        "\tpxor    %xmm2, %xmm6\n"
        "\tpxor    %xmm2, %xmm5\n"
        "\tpxor    %xmm2, %xmm1\n"
        "\tpxor    %xmm1, %xmm7\n"
        "\tpxor    %xmm1, %xmm5\n"
        "\tpxor    %xmm1, %xmm3\n"
        "\tpxor    %xmm3, %xmm1\n"
        "\tpxor    %xmm0, %xmm6\n"
        "\tpxor    %xmm0, %xmm4\n"
        "\tpxor    %xmm0, %xmm2\n"
        "\tpxor    %xmm2, %xmm0\n"
    );
}

static void gf8mul_01010101(void)
{
    __asm__ __volatile__
    (
        "\tpxor    %xmm7, %xmm0\n"
        "\tpxor    %xmm6, %xmm7\n"
        "\tpxor    %xmm6, %xmm3\n"
        "\tpxor    %xmm6, %xmm2\n"
        "\tpxor    %xmm6, %xmm1\n"
        "\tpxor    %xmm6, %xmm0\n"
        "\tpxor    %xmm5, %xmm7\n"
        "\tpxor    %xmm5, %xmm6\n"
        "\tpxor    %xmm5, %xmm3\n"
        "\tpxor    %xmm4, %xmm6\n"
        "\tpxor    %xmm4, %xmm5\n"
        "\tpxor    %xmm4, %xmm2\n"
        "\tpxor    %xmm3, %xmm5\n"
        "\tpxor    %xmm3, %xmm4\n"
        "\tpxor    %xmm3, %xmm1\n"
        "\tpxor    %xmm2, %xmm4\n"
        "\tpxor    %xmm2, %xmm3\n"
        "\tpxor    %xmm2, %xmm0\n"
        "\tpxor    %xmm1, %xmm7\n"
        "\tpxor    %xmm1, %xmm5\n"
        "\tpxor    %xmm1, %xmm3\n"
        "\tpxor    %xmm0, %xmm6\n"
        "\tpxor    %xmm0, %xmm4\n"
        "\tpxor    %xmm0, %xmm2\n"
    );
}

static void gf8mul_01010110(void)
{
    __asm__ __volatile__
    (
        "\tpxor    %xmm7, %xmm6\n"
        "\tpxor    %xmm7, %xmm1\n"
        "\tpxor    %xmm6, %xmm7\n"
        "\tpxor    %xmm6, %xmm5\n"
        "\tpxor    %xmm6, %xmm4\n"
        "\tpxor    %xmm6, %xmm2\n"
        "\tpxor    %xmm6, %xmm1\n"
        "\tpxor    %xmm6, %xmm0\n"
        "\tpxor    %xmm7, %xmm6\n"
        "\tpxor    %xmm5, %xmm6\n"
        "\tpxor    %xmm5, %xmm4\n"
        "\tpxor    %xmm5, %xmm3\n"
        "\tpxor    %xmm5, %xmm2\n"
        "\tpxor    %xmm5, %xmm1\n"
        "\tpxor    %xmm4, %xmm7\n"
        "\tpxor    %xmm4, %xmm6\n"
        "\tpxor    %xmm4, %xmm5\n"
        "\tpxor    %xmm4, %xmm2\n"
        "\tpxor    %xmm4, %xmm1\n"
        "\tpxor    %xmm5, %xmm4\n"
        "\tpxor    %xmm3, %xmm6\n"
        "\tpxor    %xmm3, %xmm5\n"
        "\tpxor    %xmm3, %xmm4\n"
        "\tpxor    %xmm3, %xmm1\n"
        "\tpxor    %xmm3, %xmm0\n"
        "\tpxor    %xmm4, %xmm3\n"
        "\tpxor    %xmm2, %xmm7\n"
        "\tpxor    %xmm2, %xmm5\n"
        "\tpxor    %xmm2, %xmm4\n"
        "\tpxor    %xmm2, %xmm3\n"
        "\tpxor    %xmm2, %xmm1\n"
        "\tpxor    %xmm2, %xmm0\n"
        "\tpxor    %xmm1, %xmm7\n"
        "\tpxor    %xmm1, %xmm5\n"
        "\tpxor    %xmm1, %xmm3\n"
        "\tpxor    %xmm1, %xmm2\n"
        "\tpxor    %xmm2, %xmm1\n"
        "\tpxor    %xmm0, %xmm6\n"
        "\tpxor    %xmm0, %xmm4\n"
        "\tpxor    %xmm0, %xmm2\n"
        "\tpxor    %xmm0, %xmm1\n"
        "\tpxor    %xmm1, %xmm0\n"
    );
}

static void gf8mul_01010111(void)
{
    __asm__ __volatile__
    (
        "\tpxor    %xmm7, %xmm6\n"
        "\tpxor    %xmm7, %xmm3\n"
        "\tpxor    %xmm7, %xmm1\n"
        "\tpxor    %xmm6, %xmm5\n"
        "\tpxor    %xmm6, %xmm2\n"
        "\tpxor    %xmm6, %xmm0\n"
        "\tpxor    %xmm5, %xmm7\n"
        "\tpxor    %xmm5, %xmm3\n"
        "\tpxor    %xmm4, %xmm7\n"
        "\tpxor    %xmm4, %xmm6\n"
        "\tpxor    %xmm4, %xmm5\n"
        "\tpxor    %xmm4, %xmm1\n"
        "\tpxor    %xmm4, %xmm0\n"
        "\tpxor    %xmm5, %xmm4\n"
        "\tpxor    %xmm3, %xmm5\n"
        "\tpxor    %xmm3, %xmm1\n"
        "\tpxor    %xmm2, %xmm7\n"
        "\tpxor    %xmm2, %xmm5\n"
        "\tpxor    %xmm2, %xmm4\n"
        "\tpxor    %xmm2, %xmm3\n"
        "\tpxor    %xmm2, %xmm1\n"
        "\tpxor    %xmm2, %xmm0\n"
        "\tpxor    %xmm3, %xmm2\n"
        "\tpxor    %xmm1, %xmm7\n"
        "\tpxor    %xmm1, %xmm5\n"
        "\tpxor    %xmm1, %xmm3\n"
        "\tpxor    %xmm1, %xmm2\n"
        "\tpxor    %xmm0, %xmm6\n"
        "\tpxor    %xmm0, %xmm4\n"
        "\tpxor    %xmm0, %xmm2\n"
        "\tpxor    %xmm0, %xmm1\n"
    );
}

static void gf8mul_01011000(void)
{
    __asm__ __volatile__
    (
        "\tpxor    %xmm7, %xmm6\n"
        "\tpxor    %xmm7, %xmm0\n"
        "\tpxor    %xmm6, %xmm7\n"
        "\tpxor    %xmm6, %xmm5\n"
        "\tpxor    %xmm6, %xmm4\n"
        "\tpxor    %xmm6, %xmm2\n"
        "\tpxor    %xmm6, %xmm0\n"
        "\tpxor    %xmm7, %xmm6\n"
        "\tpxor    %xmm5, %xmm7\n"
        "\tpxor    %xmm5, %xmm3\n"
        "\tpxor    %xmm5, %xmm2\n"
        "\tpxor    %xmm5, %xmm1\n"
        "\tpxor    %xmm5, %xmm0\n"
        "\tpxor    %xmm4, %xmm7\n"
        "\tpxor    %xmm4, %xmm6\n"
        "\tpxor    %xmm4, %xmm5\n"
        "\tpxor    %xmm4, %xmm3\n"
        "\tpxor    %xmm4, %xmm1\n"
        "\tpxor    %xmm4, %xmm0\n"
        "\tpxor    %xmm3, %xmm4\n"
        "\tpxor    %xmm3, %xmm1\n"
        "\tpxor    %xmm3, %xmm0\n"
        "\tpxor    %xmm4, %xmm3\n"
        "\tpxor    %xmm2, %xmm7\n"
        "\tpxor    %xmm2, %xmm6\n"
        "\tpxor    %xmm2, %xmm3\n"
        "\tpxor    %xmm2, %xmm1\n"
        "\tpxor    %xmm1, %xmm7\n"
        "\tpxor    %xmm1, %xmm5\n"
        "\tpxor    %xmm1, %xmm4\n"
        "\tpxor    %xmm4, %xmm1\n"
        "\tpxor    %xmm0, %xmm6\n"
        "\tpxor    %xmm0, %xmm4\n"
        "\tpxor    %xmm0, %xmm3\n"
        "\tpxor    %xmm3, %xmm0\n"
    );
}

static void gf8mul_01011001(void)
{
    __asm__ __volatile__
    (
        "\tpxor    %xmm7, %xmm4\n"
        "\tpxor    %xmm7, %xmm3\n"
        "\tpxor    %xmm6, %xmm3\n"
        "\tpxor    %xmm6, %xmm2\n"
        "\tpxor    %xmm5, %xmm2\n"
        "\tpxor    %xmm5, %xmm1\n"
        "\tpxor    %xmm4, %xmm7\n"
        "\tpxor    %xmm4, %xmm2\n"
        "\tpxor    %xmm7, %xmm4\n"
        "\tpxor    %xmm3, %xmm6\n"
        "\tpxor    %xmm3, %xmm1\n"
        "\tpxor    %xmm6, %xmm3\n"
        "\tpxor    %xmm2, %xmm5\n"
        "\tpxor    %xmm2, %xmm0\n"
        "\tpxor    %xmm5, %xmm2\n"
        "\tpxor    %xmm1, %xmm7\n"
        "\tpxor    %xmm1, %xmm5\n"
        "\tpxor    %xmm1, %xmm4\n"
        "\tpxor    %xmm0, %xmm6\n"
        "\tpxor    %xmm0, %xmm4\n"
        "\tpxor    %xmm0, %xmm3\n"
    );
}

static void gf8mul_01011010(void)
{
    __asm__ __volatile__
    (
        "\tpxor    %xmm7, %xmm6\n"
        "\tpxor    %xmm7, %xmm3\n"
        "\tpxor    %xmm7, %xmm2\n"
        "\tpxor    %xmm7, %xmm0\n"
        "\tpxor    %xmm6, %xmm7\n"
        "\tpxor    %xmm6, %xmm5\n"
        "\tpxor    %xmm6, %xmm4\n"
        "\tpxor    %xmm6, %xmm3\n"
        "\tpxor    %xmm6, %xmm2\n"
        "\tpxor    %xmm6, %xmm0\n"
        "\tpxor    %xmm7, %xmm6\n"
        "\tpxor    %xmm5, %xmm7\n"
        "\tpxor    %xmm5, %xmm2\n"
        "\tpxor    %xmm5, %xmm0\n"
        "\tpxor    %xmm4, %xmm7\n"
        "\tpxor    %xmm4, %xmm5\n"
        "\tpxor    %xmm4, %xmm3\n"
        "\tpxor    %xmm4, %xmm2\n"
        "\tpxor    %xmm4, %xmm1\n"
        "\tpxor    %xmm4, %xmm0\n"
        "\tpxor    %xmm3, %xmm7\n"
        "\tpxor    %xmm3, %xmm5\n"
        "\tpxor    %xmm3, %xmm4\n"
        "\tpxor    %xmm3, %xmm0\n"
        "\tpxor    %xmm4, %xmm3\n"
        "\tpxor    %xmm2, %xmm5\n"
        "\tpxor    %xmm2, %xmm3\n"
        "\tpxor    %xmm2, %xmm0\n"
        "\tpxor    %xmm1, %xmm7\n"
        "\tpxor    %xmm1, %xmm5\n"
        "\tpxor    %xmm1, %xmm4\n"
        "\tpxor    %xmm1, %xmm2\n"
        "\tpxor    %xmm2, %xmm1\n"
        "\tpxor    %xmm0, %xmm6\n"
        "\tpxor    %xmm0, %xmm4\n"
        "\tpxor    %xmm0, %xmm3\n"
        "\tpxor    %xmm0, %xmm1\n"
        "\tpxor    %xmm1, %xmm0\n"
    );
}

static void gf8mul_01011011(void)
{
    __asm__ __volatile__
    (
        "\tpxor    %xmm7, %xmm6\n"
        "\tpxor    %xmm7, %xmm4\n"
        "\tpxor    %xmm7, %xmm2\n"
        "\tpxor    %xmm7, %xmm1\n"
        "\tpxor    %xmm7, %xmm0\n"
        "\tpxor    %xmm6, %xmm5\n"
        "\tpxor    %xmm6, %xmm2\n"
        "\tpxor    %xmm6, %xmm1\n"
        "\tpxor    %xmm6, %xmm0\n"
        "\tpxor    %xmm5, %xmm7\n"
        "\tpxor    %xmm5, %xmm6\n"
        "\tpxor    %xmm5, %xmm4\n"
        "\tpxor    %xmm5, %xmm1\n"
        "\tpxor    %xmm6, %xmm5\n"
        "\tpxor    %xmm4, %xmm6\n"
        "\tpxor    %xmm4, %xmm5\n"
        "\tpxor    %xmm4, %xmm3\n"
        "\tpxor    %xmm4, %xmm0\n"
        "\tpxor    %xmm5, %xmm4\n"
        "\tpxor    %xmm3, %xmm7\n"
        "\tpxor    %xmm3, %xmm6\n"
        "\tpxor    %xmm3, %xmm2\n"
        "\tpxor    %xmm3, %xmm0\n"
        "\tpxor    %xmm2, %xmm7\n"
        "\tpxor    %xmm2, %xmm4\n"
        "\tpxor    %xmm2, %xmm3\n"
        "\tpxor    %xmm2, %xmm1\n"
        "\tpxor    %xmm2, %xmm0\n"
        "\tpxor    %xmm1, %xmm7\n"
        "\tpxor    %xmm1, %xmm5\n"
        "\tpxor    %xmm1, %xmm4\n"
        "\tpxor    %xmm1, %xmm2\n"
        "\tpxor    %xmm0, %xmm6\n"
        "\tpxor    %xmm0, %xmm4\n"
        "\tpxor    %xmm0, %xmm3\n"
        "\tpxor    %xmm0, %xmm1\n"
    );
}

static void gf8mul_01011100(void)
{
    __asm__ __volatile__
    (
        "\tpxor    %xmm7, %xmm6\n"
        "\tpxor    %xmm7, %xmm5\n"
        "\tpxor    %xmm6, %xmm7\n"
        "\tpxor    %xmm6, %xmm4\n"
        "\tpxor    %xmm6, %xmm3\n"
        "\tpxor    %xmm6, %xmm2\n"
        "\tpxor    %xmm7, %xmm6\n"
        "\tpxor    %xmm5, %xmm6\n"
        "\tpxor    %xmm5, %xmm3\n"
        "\tpxor    %xmm5, %xmm2\n"
        "\tpxor    %xmm5, %xmm1\n"
        "\tpxor    %xmm6, %xmm5\n"
        "\tpxor    %xmm4, %xmm5\n"
        "\tpxor    %xmm4, %xmm2\n"
        "\tpxor    %xmm4, %xmm1\n"
        "\tpxor    %xmm4, %xmm0\n"
        "\tpxor    %xmm5, %xmm4\n"
        "\tpxor    %xmm3, %xmm7\n"
        "\tpxor    %xmm3, %xmm6\n"
        "\tpxor    %xmm3, %xmm4\n"
        "\tpxor    %xmm2, %xmm7\n"
        "\tpxor    %xmm2, %xmm6\n"
        "\tpxor    %xmm2, %xmm4\n"
        "\tpxor    %xmm2, %xmm1\n"
        "\tpxor    %xmm1, %xmm7\n"
        "\tpxor    %xmm1, %xmm5\n"
        "\tpxor    %xmm1, %xmm4\n"
        "\tpxor    %xmm1, %xmm3\n"
        "\tpxor    %xmm3, %xmm1\n"
        "\tpxor    %xmm0, %xmm6\n"
        "\tpxor    %xmm0, %xmm4\n"
        "\tpxor    %xmm0, %xmm3\n"
        "\tpxor    %xmm0, %xmm2\n"
        "\tpxor    %xmm2, %xmm0\n"
    );
}

static void gf8mul_01011101(void)
{
    __asm__ __volatile__
    (
        "\tpxor    %xmm7, %xmm6\n"
        "\tpxor    %xmm7, %xmm5\n"
        "\tpxor    %xmm7, %xmm4\n"
        "\tpxor    %xmm7, %xmm2\n"
        "\tpxor    %xmm6, %xmm7\n"
        "\tpxor    %xmm6, %xmm4\n"
        "\tpxor    %xmm6, %xmm1\n"
        "\tpxor    %xmm6, %xmm0\n"
        "\tpxor    %xmm7, %xmm6\n"
        "\tpxor    %xmm5, %xmm7\n"
        "\tpxor    %xmm5, %xmm6\n"
        "\tpxor    %xmm5, %xmm3\n"
        "\tpxor    %xmm5, %xmm0\n"
        "\tpxor    %xmm4, %xmm7\n"
        "\tpxor    %xmm4, %xmm6\n"
        "\tpxor    %xmm4, %xmm2\n"
        "\tpxor    %xmm3, %xmm6\n"
        "\tpxor    %xmm3, %xmm5\n"
        "\tpxor    %xmm3, %xmm1\n"
        "\tpxor    %xmm2, %xmm5\n"
        "\tpxor    %xmm2, %xmm4\n"
        "\tpxor    %xmm2, %xmm0\n"
        "\tpxor    %xmm1, %xmm7\n"
        "\tpxor    %xmm1, %xmm5\n"
        "\tpxor    %xmm1, %xmm4\n"
        "\tpxor    %xmm1, %xmm3\n"
        "\tpxor    %xmm0, %xmm6\n"
        "\tpxor    %xmm0, %xmm4\n"
        "\tpxor    %xmm0, %xmm3\n"
        "\tpxor    %xmm0, %xmm2\n"
    );
}

static void gf8mul_01011110(void)
{
    __asm__ __volatile__
    (
        "\tpxor    %xmm7, %xmm1\n"
        "\tpxor    %xmm6, %xmm0\n"
        "\tpxor    %xmm5, %xmm7\n"
        "\tpxor    %xmm5, %xmm3\n"
        "\tpxor    %xmm5, %xmm2\n"
        "\tpxor    %xmm4, %xmm6\n"
        "\tpxor    %xmm4, %xmm2\n"
        "\tpxor    %xmm4, %xmm1\n"
        "\tpxor    %xmm3, %xmm5\n"
        "\tpxor    %xmm3, %xmm1\n"
        "\tpxor    %xmm3, %xmm0\n"
        "\tpxor    %xmm2, %xmm7\n"
        "\tpxor    %xmm2, %xmm1\n"
        "\tpxor    %xmm2, %xmm0\n"
        "\tpxor    %xmm1, %xmm7\n"
        "\tpxor    %xmm1, %xmm5\n"
        "\tpxor    %xmm1, %xmm4\n"
        "\tpxor    %xmm1, %xmm3\n"
        "\tpxor    %xmm1, %xmm2\n"
        "\tpxor    %xmm2, %xmm1\n"
        "\tpxor    %xmm0, %xmm6\n"
        "\tpxor    %xmm0, %xmm4\n"
        "\tpxor    %xmm0, %xmm3\n"
        "\tpxor    %xmm0, %xmm2\n"
        "\tpxor    %xmm0, %xmm1\n"
        "\tpxor    %xmm1, %xmm0\n"
    );
}

static void gf8mul_01011111(void)
{
    __asm__ __volatile__
    (
        "\tpxor    %xmm7, %xmm6\n"
        "\tpxor    %xmm7, %xmm5\n"
        "\tpxor    %xmm7, %xmm4\n"
        "\tpxor    %xmm7, %xmm1\n"
        "\tpxor    %xmm6, %xmm7\n"
        "\tpxor    %xmm6, %xmm5\n"
        "\tpxor    %xmm6, %xmm4\n"
        "\tpxor    %xmm6, %xmm0\n"
        "\tpxor    %xmm5, %xmm7\n"
        "\tpxor    %xmm5, %xmm4\n"
        "\tpxor    %xmm5, %xmm3\n"
        "\tpxor    %xmm5, %xmm2\n"
        "\tpxor    %xmm5, %xmm0\n"
        "\tpxor    %xmm4, %xmm7\n"
        "\tpxor    %xmm4, %xmm3\n"
        "\tpxor    %xmm4, %xmm1\n"
        "\tpxor    %xmm4, %xmm0\n"
        "\tpxor    %xmm7, %xmm4\n"
        "\tpxor    %xmm3, %xmm7\n"
        "\tpxor    %xmm3, %xmm4\n"
        "\tpxor    %xmm2, %xmm6\n"
        "\tpxor    %xmm2, %xmm3\n"
        "\tpxor    %xmm1, %xmm5\n"
        "\tpxor    %xmm1, %xmm2\n"
        "\tpxor    %xmm0, %xmm4\n"
        "\tpxor    %xmm0, %xmm1\n"
        "\tpxor    %xmm7, %xmm6\n"
        "\tpxor    %xmm7, %xmm3\n"
        "\tpxor    %xmm7, %xmm1\n"
        "\tpxor    %xmm7, %xmm0\n"
        "\tpxor    %xmm6, %xmm7\n"
        "\tpxor    %xmm6, %xmm5\n"
        "\tpxor    %xmm6, %xmm4\n"
        "\tpxor    %xmm6, %xmm3\n"
        "\tpxor    %xmm6, %xmm0\n"
        "\tpxor    %xmm7, %xmm6\n"
        "\tpxor    %xmm5, %xmm7\n"
        "\tpxor    %xmm4, %xmm6\n"
        "\tpxor    %xmm3, %xmm5\n"
        "\tpxor    %xmm2, %xmm4\n"
        "\tpxor    %xmm1, %xmm3\n"
        "\tpxor    %xmm0, %xmm2\n"
    );
}

static void gf8mul_01100000(void)
{
    __asm__ __volatile__
    (
        "\tpxor    %xmm7, %xmm6\n"
        "\tpxor    %xmm7, %xmm5\n"
        "\tpxor    %xmm7, %xmm4\n"
        "\tpxor    %xmm7, %xmm2\n"
        "\tpxor    %xmm7, %xmm1\n"
        "\tpxor    %xmm7, %xmm0\n"
        "\tpxor    %xmm6, %xmm7\n"
        "\tpxor    %xmm6, %xmm3\n"
        "\tpxor    %xmm6, %xmm1\n"
        "\tpxor    %xmm5, %xmm7\n"
        "\tpxor    %xmm5, %xmm2\n"
        "\tpxor    %xmm5, %xmm1\n"
        "\tpxor    %xmm5, %xmm0\n"
        "\tpxor    %xmm4, %xmm6\n"
        "\tpxor    %xmm4, %xmm5\n"
        "\tpxor    %xmm4, %xmm2\n"
        "\tpxor    %xmm4, %xmm1\n"
        "\tpxor    %xmm4, %xmm0\n"
        "\tpxor    %xmm3, %xmm7\n"
        "\tpxor    %xmm3, %xmm6\n"
        "\tpxor    %xmm3, %xmm4\n"
        "\tpxor    %xmm3, %xmm2\n"
        "\tpxor    %xmm3, %xmm0\n"
        "\tpxor    %xmm2, %xmm5\n"
        "\tpxor    %xmm2, %xmm4\n"
        "\tpxor    %xmm2, %xmm3\n"
        "\tpxor    %xmm2, %xmm1\n"
        "\tpxor    %xmm2, %xmm0\n"
        "\tpxor    %xmm1, %xmm7\n"
        "\tpxor    %xmm1, %xmm6\n"
        "\tpxor    %xmm6, %xmm1\n"
        "\tpxor    %xmm0, %xmm6\n"
        "\tpxor    %xmm0, %xmm5\n"
        "\tpxor    %xmm5, %xmm0\n"
    );
}

static void gf8mul_01100001(void)
{
    __asm__ __volatile__
    (
        "\tpxor    %xmm7, %xmm6\n"
        "\tpxor    %xmm7, %xmm3\n"
        "\tpxor    %xmm6, %xmm5\n"
        "\tpxor    %xmm6, %xmm2\n"
        "\tpxor    %xmm5, %xmm4\n"
        "\tpxor    %xmm5, %xmm1\n"
        "\tpxor    %xmm4, %xmm3\n"
        "\tpxor    %xmm4, %xmm0\n"
        "\tpxor    %xmm3, %xmm7\n"
        "\tpxor    %xmm3, %xmm1\n"
        "\tpxor    %xmm3, %xmm0\n"
        "\tpxor    %xmm2, %xmm7\n"
        "\tpxor    %xmm2, %xmm6\n"
        "\tpxor    %xmm2, %xmm5\n"
        "\tpxor    %xmm2, %xmm4\n"
        "\tpxor    %xmm2, %xmm3\n"
        "\tpxor    %xmm2, %xmm0\n"
        "\tpxor    %xmm3, %xmm2\n"
        "\tpxor    %xmm1, %xmm7\n"
        "\tpxor    %xmm1, %xmm6\n"
        "\tpxor    %xmm0, %xmm6\n"
        "\tpxor    %xmm0, %xmm5\n"
    );
}

static void gf8mul_01100010(void)
{
    __asm__ __volatile__
    (
        "\tpxor    %xmm7, %xmm6\n"
        "\tpxor    %xmm7, %xmm5\n"
        "\tpxor    %xmm7, %xmm4\n"
        "\tpxor    %xmm7, %xmm3\n"
        "\tpxor    %xmm7, %xmm2\n"
        "\tpxor    %xmm7, %xmm0\n"
        "\tpxor    %xmm6, %xmm7\n"
        "\tpxor    %xmm6, %xmm4\n"
        "\tpxor    %xmm6, %xmm2\n"
        "\tpxor    %xmm6, %xmm1\n"
        "\tpxor    %xmm6, %xmm0\n"
        "\tpxor    %xmm5, %xmm7\n"
        "\tpxor    %xmm5, %xmm6\n"
        "\tpxor    %xmm5, %xmm3\n"
        "\tpxor    %xmm5, %xmm2\n"
        "\tpxor    %xmm5, %xmm1\n"
        "\tpxor    %xmm5, %xmm0\n"
        "\tpxor    %xmm6, %xmm5\n"
        "\tpxor    %xmm4, %xmm7\n"
        "\tpxor    %xmm4, %xmm6\n"
        "\tpxor    %xmm4, %xmm1\n"
        "\tpxor    %xmm4, %xmm0\n"
        "\tpxor    %xmm6, %xmm4\n"
        "\tpxor    %xmm3, %xmm7\n"
        "\tpxor    %xmm3, %xmm6\n"
        "\tpxor    %xmm3, %xmm2\n"
        "\tpxor    %xmm3, %xmm0\n"
        "\tpxor    %xmm6, %xmm3\n"
        "\tpxor    %xmm2, %xmm7\n"
        "\tpxor    %xmm2, %xmm6\n"
        "\tpxor    %xmm2, %xmm5\n"
        "\tpxor    %xmm2, %xmm4\n"
        "\tpxor    %xmm2, %xmm0\n"
        "\tpxor    %xmm1, %xmm7\n"
        "\tpxor    %xmm1, %xmm6\n"
        "\tpxor    %xmm1, %xmm2\n"
        "\tpxor    %xmm2, %xmm1\n"
        "\tpxor    %xmm0, %xmm6\n"
        "\tpxor    %xmm0, %xmm5\n"
        "\tpxor    %xmm0, %xmm1\n"
        "\tpxor    %xmm1, %xmm0\n"
    );
}

static void gf8mul_01100011(void)
{
    __asm__ __volatile__
    (
        "\tpxor    %xmm7, %xmm2\n"
        "\tpxor    %xmm7, %xmm1\n"
        "\tpxor    %xmm6, %xmm1\n"
        "\tpxor    %xmm6, %xmm0\n"
        "\tpxor    %xmm5, %xmm7\n"
        "\tpxor    %xmm5, %xmm3\n"
        "\tpxor    %xmm5, %xmm0\n"
        "\tpxor    %xmm4, %xmm7\n"
        "\tpxor    %xmm4, %xmm6\n"
        "\tpxor    %xmm4, %xmm3\n"
        "\tpxor    %xmm4, %xmm2\n"
        "\tpxor    %xmm3, %xmm6\n"
        "\tpxor    %xmm3, %xmm5\n"
        "\tpxor    %xmm3, %xmm2\n"
        "\tpxor    %xmm3, %xmm1\n"
        "\tpxor    %xmm2, %xmm5\n"
        "\tpxor    %xmm2, %xmm4\n"
        "\tpxor    %xmm2, %xmm1\n"
        "\tpxor    %xmm2, %xmm0\n"
        "\tpxor    %xmm1, %xmm7\n"
        "\tpxor    %xmm1, %xmm6\n"
        "\tpxor    %xmm1, %xmm2\n"
        "\tpxor    %xmm0, %xmm6\n"
        "\tpxor    %xmm0, %xmm5\n"
        "\tpxor    %xmm0, %xmm1\n"
    );
}

static void gf8mul_01100100(void)
{
    __asm__ __volatile__
    (
        "\tpxor    %xmm7, %xmm6\n"
        "\tpxor    %xmm7, %xmm1\n"
        "\tpxor    %xmm6, %xmm5\n"
        "\tpxor    %xmm6, %xmm0\n"
        "\tpxor    %xmm5, %xmm7\n"
        "\tpxor    %xmm5, %xmm1\n"
        "\tpxor    %xmm5, %xmm0\n"
        "\tpxor    %xmm4, %xmm7\n"
        "\tpxor    %xmm4, %xmm6\n"
        "\tpxor    %xmm4, %xmm5\n"
        "\tpxor    %xmm4, %xmm3\n"
        "\tpxor    %xmm4, %xmm2\n"
        "\tpxor    %xmm4, %xmm0\n"
        "\tpxor    %xmm5, %xmm4\n"
        "\tpxor    %xmm3, %xmm7\n"
        "\tpxor    %xmm3, %xmm2\n"
        "\tpxor    %xmm2, %xmm6\n"
        "\tpxor    %xmm2, %xmm1\n"
        "\tpxor    %xmm1, %xmm7\n"
        "\tpxor    %xmm1, %xmm6\n"
        "\tpxor    %xmm1, %xmm3\n"
        "\tpxor    %xmm3, %xmm1\n"
        "\tpxor    %xmm0, %xmm6\n"
        "\tpxor    %xmm0, %xmm5\n"
        "\tpxor    %xmm0, %xmm2\n"
        "\tpxor    %xmm2, %xmm0\n"
    );
}

static void gf8mul_01100101(void)
{
    __asm__ __volatile__
    (
        "\tpxor    %xmm7, %xmm5\n"
        "\tpxor    %xmm7, %xmm3\n"
        "\tpxor    %xmm7, %xmm2\n"
        "\tpxor    %xmm7, %xmm1\n"
        "\tpxor    %xmm6, %xmm4\n"
        "\tpxor    %xmm6, %xmm2\n"
        "\tpxor    %xmm6, %xmm1\n"
        "\tpxor    %xmm6, %xmm0\n"
        "\tpxor    %xmm5, %xmm7\n"
        "\tpxor    %xmm5, %xmm6\n"
        "\tpxor    %xmm5, %xmm4\n"
        "\tpxor    %xmm5, %xmm1\n"
        "\tpxor    %xmm4, %xmm5\n"
        "\tpxor    %xmm4, %xmm2\n"
        "\tpxor    %xmm4, %xmm1\n"
        "\tpxor    %xmm4, %xmm0\n"
        "\tpxor    %xmm3, %xmm7\n"
        "\tpxor    %xmm3, %xmm5\n"
        "\tpxor    %xmm3, %xmm1\n"
        "\tpxor    %xmm3, %xmm0\n"
        "\tpxor    %xmm5, %xmm3\n"
        "\tpxor    %xmm2, %xmm7\n"
        "\tpxor    %xmm2, %xmm6\n"
        "\tpxor    %xmm2, %xmm5\n"
        "\tpxor    %xmm2, %xmm3\n"
        "\tpxor    %xmm2, %xmm0\n"
        "\tpxor    %xmm1, %xmm7\n"
        "\tpxor    %xmm1, %xmm6\n"
        "\tpxor    %xmm1, %xmm3\n"
        "\tpxor    %xmm0, %xmm6\n"
        "\tpxor    %xmm0, %xmm5\n"
        "\tpxor    %xmm0, %xmm2\n"
    );
}

static void gf8mul_01100110(void)
{
    __asm__ __volatile__
    (
        "\tpxor    %xmm7, %xmm6\n"
        "\tpxor    %xmm6, %xmm5\n"
        "\tpxor    %xmm5, %xmm4\n"
        "\tpxor    %xmm4, %xmm3\n"
        "\tpxor    %xmm3, %xmm2\n"
        "\tpxor    %xmm2, %xmm1\n"
        "\tpxor    %xmm1, %xmm0\n"
        "\tpxor    %xmm0, %xmm7\n"
        "\tpxor    %xmm0, %xmm6\n"
        "\tpxor    %xmm0, %xmm5\n"
        "\tpxor    %xmm0, %xmm4\n"
        "\tpxor    %xmm0, %xmm2\n"
        "\tpxor    %xmm7, %xmm0\n"
        "\tpxor    %xmm6, %xmm7\n"
        "\tpxor    %xmm6, %xmm3\n"
        "\tpxor    %xmm6, %xmm2\n"
        "\tpxor    %xmm6, %xmm1\n"
        "\tpxor    %xmm6, %xmm0\n"
        "\tpxor    %xmm5, %xmm7\n"
        "\tpxor    %xmm5, %xmm6\n"
        "\tpxor    %xmm5, %xmm3\n"
        "\tpxor    %xmm4, %xmm6\n"
        "\tpxor    %xmm4, %xmm5\n"
        "\tpxor    %xmm4, %xmm2\n"
        "\tpxor    %xmm3, %xmm5\n"
        "\tpxor    %xmm3, %xmm4\n"
        "\tpxor    %xmm3, %xmm1\n"
        "\tpxor    %xmm2, %xmm4\n"
        "\tpxor    %xmm2, %xmm3\n"
        "\tpxor    %xmm2, %xmm0\n"
        "\tpxor    %xmm1, %xmm7\n"
        "\tpxor    %xmm1, %xmm5\n"
        "\tpxor    %xmm1, %xmm3\n"
        "\tpxor    %xmm0, %xmm6\n"
        "\tpxor    %xmm0, %xmm4\n"
        "\tpxor    %xmm0, %xmm2\n"
    );
}

static void gf8mul_01100111(void)
{
    __asm__ __volatile__
    (
        "\tpxor    %xmm7, %xmm6\n"
        "\tpxor    %xmm7, %xmm5\n"
        "\tpxor    %xmm7, %xmm1\n"
        "\tpxor    %xmm7, %xmm0\n"
        "\tpxor    %xmm6, %xmm7\n"
        "\tpxor    %xmm6, %xmm3\n"
        "\tpxor    %xmm6, %xmm0\n"
        "\tpxor    %xmm7, %xmm6\n"
        "\tpxor    %xmm5, %xmm6\n"
        "\tpxor    %xmm4, %xmm7\n"
        "\tpxor    %xmm4, %xmm5\n"
        "\tpxor    %xmm4, %xmm3\n"
        "\tpxor    %xmm4, %xmm2\n"
        "\tpxor    %xmm5, %xmm4\n"
        "\tpxor    %xmm3, %xmm6\n"
        "\tpxor    %xmm3, %xmm4\n"
        "\tpxor    %xmm3, %xmm2\n"
        "\tpxor    %xmm3, %xmm1\n"
        "\tpxor    %xmm4, %xmm3\n"
        "\tpxor    %xmm2, %xmm5\n"
        "\tpxor    %xmm2, %xmm3\n"
        "\tpxor    %xmm2, %xmm1\n"
        "\tpxor    %xmm2, %xmm0\n"
        "\tpxor    %xmm3, %xmm2\n"
        "\tpxor    %xmm1, %xmm7\n"
        "\tpxor    %xmm1, %xmm6\n"
        "\tpxor    %xmm1, %xmm3\n"
        "\tpxor    %xmm1, %xmm2\n"
        "\tpxor    %xmm0, %xmm6\n"
        "\tpxor    %xmm0, %xmm5\n"
        "\tpxor    %xmm0, %xmm2\n"
        "\tpxor    %xmm0, %xmm1\n"
    );
}

static void gf8mul_01101000(void)
{
    __asm__ __volatile__
    (
        "\tpxor    %xmm7, %xmm6\n"
        "\tpxor    %xmm7, %xmm4\n"
        "\tpxor    %xmm7, %xmm3\n"
        "\tpxor    %xmm7, %xmm0\n"
        "\tpxor    %xmm6, %xmm7\n"
        "\tpxor    %xmm6, %xmm5\n"
        "\tpxor    %xmm6, %xmm4\n"
        "\tpxor    %xmm6, %xmm1\n"
        "\tpxor    %xmm7, %xmm6\n"
        "\tpxor    %xmm5, %xmm6\n"
        "\tpxor    %xmm5, %xmm4\n"
        "\tpxor    %xmm5, %xmm3\n"
        "\tpxor    %xmm5, %xmm0\n"
        "\tpxor    %xmm6, %xmm5\n"
        "\tpxor    %xmm4, %xmm2\n"
        "\tpxor    %xmm4, %xmm0\n"
        "\tpxor    %xmm3, %xmm7\n"
        "\tpxor    %xmm3, %xmm5\n"
        "\tpxor    %xmm3, %xmm4\n"
        "\tpxor    %xmm3, %xmm2\n"
        "\tpxor    %xmm3, %xmm0\n"
        "\tpxor    %xmm4, %xmm3\n"
        "\tpxor    %xmm2, %xmm6\n"
        "\tpxor    %xmm2, %xmm5\n"
        "\tpxor    %xmm2, %xmm3\n"
        "\tpxor    %xmm2, %xmm1\n"
        "\tpxor    %xmm1, %xmm7\n"
        "\tpxor    %xmm1, %xmm6\n"
        "\tpxor    %xmm1, %xmm4\n"
        "\tpxor    %xmm4, %xmm1\n"
        "\tpxor    %xmm0, %xmm6\n"
        "\tpxor    %xmm0, %xmm5\n"
        "\tpxor    %xmm0, %xmm3\n"
        "\tpxor    %xmm3, %xmm0\n"
    );
}

static void gf8mul_01101001(void)
{
    __asm__ __volatile__
    (
        "\tpxor    %xmm7, %xmm3\n"
        "\tpxor    %xmm6, %xmm2\n"
        "\tpxor    %xmm5, %xmm1\n"
        "\tpxor    %xmm4, %xmm0\n"
        "\tpxor    %xmm3, %xmm7\n"
        "\tpxor    %xmm3, %xmm2\n"
        "\tpxor    %xmm3, %xmm1\n"
        "\tpxor    %xmm2, %xmm6\n"
        "\tpxor    %xmm2, %xmm1\n"
        "\tpxor    %xmm2, %xmm0\n"
        "\tpxor    %xmm1, %xmm7\n"
        "\tpxor    %xmm1, %xmm6\n"
        "\tpxor    %xmm1, %xmm5\n"
        "\tpxor    %xmm1, %xmm2\n"
        "\tpxor    %xmm0, %xmm6\n"
        "\tpxor    %xmm0, %xmm5\n"
        "\tpxor    %xmm0, %xmm4\n"
        "\tpxor    %xmm0, %xmm1\n"
        "\tpxor    %xmm7, %xmm6\n"
        "\tpxor    %xmm7, %xmm4\n"
        "\tpxor    %xmm6, %xmm5\n"
        "\tpxor    %xmm6, %xmm3\n"
        "\tpxor    %xmm5, %xmm4\n"
        "\tpxor    %xmm5, %xmm2\n"
        "\tpxor    %xmm4, %xmm3\n"
        "\tpxor    %xmm4, %xmm1\n"
        "\tpxor    %xmm3, %xmm2\n"
        "\tpxor    %xmm3, %xmm0\n"
        "\tpxor    %xmm2, %xmm7\n"
        "\tpxor    %xmm2, %xmm6\n"
        "\tpxor    %xmm2, %xmm5\n"
        "\tpxor    %xmm1, %xmm6\n"
        "\tpxor    %xmm1, %xmm5\n"
        "\tpxor    %xmm1, %xmm4\n"
        "\tpxor    %xmm0, %xmm5\n"
        "\tpxor    %xmm0, %xmm4\n"
        "\tpxor    %xmm0, %xmm3\n"
    );
}

static void gf8mul_01101010(void)
{
    __asm__ __volatile__
    (
        "\tpxor    %xmm7, %xmm3\n"
        "\tpxor    %xmm7, %xmm1\n"
        "\tpxor    %xmm6, %xmm2\n"
        "\tpxor    %xmm6, %xmm0\n"
        "\tpxor    %xmm5, %xmm7\n"
        "\tpxor    %xmm5, %xmm2\n"
        "\tpxor    %xmm5, %xmm1\n"
        "\tpxor    %xmm4, %xmm6\n"
        "\tpxor    %xmm4, %xmm1\n"
        "\tpxor    %xmm4, %xmm0\n"
        "\tpxor    %xmm3, %xmm7\n"
        "\tpxor    %xmm3, %xmm5\n"
        "\tpxor    %xmm3, %xmm2\n"
        "\tpxor    %xmm3, %xmm0\n"
        "\tpxor    %xmm2, %xmm7\n"
        "\tpxor    %xmm2, %xmm6\n"
        "\tpxor    %xmm2, %xmm4\n"
        "\tpxor    %xmm2, %xmm3\n"
        "\tpxor    %xmm2, %xmm0\n"
        "\tpxor    %xmm1, %xmm7\n"
        "\tpxor    %xmm1, %xmm6\n"
        "\tpxor    %xmm1, %xmm4\n"
        "\tpxor    %xmm1, %xmm2\n"
        "\tpxor    %xmm2, %xmm1\n"
        "\tpxor    %xmm0, %xmm6\n"
        "\tpxor    %xmm0, %xmm5\n"
        "\tpxor    %xmm0, %xmm3\n"
        "\tpxor    %xmm0, %xmm1\n"
        "\tpxor    %xmm1, %xmm0\n"
    );
}

static void gf8mul_01101011(void)
{
    __asm__ __volatile__
    (
        "\tpxor    %xmm7, %xmm5\n"
        "\tpxor    %xmm7, %xmm3\n"
        "\tpxor    %xmm7, %xmm1\n"
        "\tpxor    %xmm7, %xmm0\n"
        "\tpxor    %xmm6, %xmm7\n"
        "\tpxor    %xmm6, %xmm5\n"
        "\tpxor    %xmm6, %xmm4\n"
        "\tpxor    %xmm5, %xmm6\n"
        "\tpxor    %xmm5, %xmm4\n"
        "\tpxor    %xmm5, %xmm3\n"
        "\tpxor    %xmm4, %xmm5\n"
        "\tpxor    %xmm4, %xmm3\n"
        "\tpxor    %xmm4, %xmm2\n"
        "\tpxor    %xmm3, %xmm4\n"
        "\tpxor    %xmm3, %xmm2\n"
        "\tpxor    %xmm3, %xmm1\n"
        "\tpxor    %xmm2, %xmm3\n"
        "\tpxor    %xmm2, %xmm1\n"
        "\tpxor    %xmm2, %xmm0\n"
        "\tpxor    %xmm1, %xmm7\n"
        "\tpxor    %xmm1, %xmm6\n"
        "\tpxor    %xmm1, %xmm4\n"
        "\tpxor    %xmm1, %xmm2\n"
        "\tpxor    %xmm0, %xmm6\n"
        "\tpxor    %xmm0, %xmm5\n"
        "\tpxor    %xmm0, %xmm3\n"
        "\tpxor    %xmm0, %xmm1\n"
    );
}

static void gf8mul_01101100(void)
{
    __asm__ __volatile__
    (
        "\tpxor    %xmm7, %xmm6\n"
        "\tpxor    %xmm7, %xmm3\n"
        "\tpxor    %xmm7, %xmm2\n"
        "\tpxor    %xmm7, %xmm0\n"
        "\tpxor    %xmm6, %xmm7\n"
        "\tpxor    %xmm6, %xmm4\n"
        "\tpxor    %xmm6, %xmm0\n"
        "\tpxor    %xmm5, %xmm7\n"
        "\tpxor    %xmm5, %xmm6\n"
        "\tpxor    %xmm5, %xmm2\n"
        "\tpxor    %xmm5, %xmm0\n"
        "\tpxor    %xmm6, %xmm5\n"
        "\tpxor    %xmm4, %xmm7\n"
        "\tpxor    %xmm4, %xmm6\n"
        "\tpxor    %xmm4, %xmm3\n"
        "\tpxor    %xmm4, %xmm2\n"
        "\tpxor    %xmm4, %xmm1\n"
        "\tpxor    %xmm4, %xmm0\n"
        "\tpxor    %xmm3, %xmm7\n"
        "\tpxor    %xmm3, %xmm6\n"
        "\tpxor    %xmm3, %xmm5\n"
        "\tpxor    %xmm3, %xmm2\n"
        "\tpxor    %xmm2, %xmm6\n"
        "\tpxor    %xmm2, %xmm5\n"
        "\tpxor    %xmm2, %xmm4\n"
        "\tpxor    %xmm2, %xmm1\n"
        "\tpxor    %xmm1, %xmm7\n"
        "\tpxor    %xmm1, %xmm6\n"
        "\tpxor    %xmm1, %xmm4\n"
        "\tpxor    %xmm1, %xmm3\n"
        "\tpxor    %xmm3, %xmm1\n"
        "\tpxor    %xmm0, %xmm6\n"
        "\tpxor    %xmm0, %xmm5\n"
        "\tpxor    %xmm0, %xmm3\n"
        "\tpxor    %xmm0, %xmm2\n"
        "\tpxor    %xmm2, %xmm0\n"
    );
}

static void gf8mul_01101101(void)
{
    __asm__ __volatile__
    (
        "\tpxor    %xmm7, %xmm6\n"
        "\tpxor    %xmm7, %xmm5\n"
        "\tpxor    %xmm7, %xmm4\n"
        "\tpxor    %xmm7, %xmm2\n"
        "\tpxor    %xmm6, %xmm7\n"
        "\tpxor    %xmm6, %xmm5\n"
        "\tpxor    %xmm6, %xmm4\n"
        "\tpxor    %xmm6, %xmm0\n"
        "\tpxor    %xmm7, %xmm6\n"
        "\tpxor    %xmm5, %xmm7\n"
        "\tpxor    %xmm5, %xmm6\n"
        "\tpxor    %xmm5, %xmm4\n"
        "\tpxor    %xmm5, %xmm3\n"
        "\tpxor    %xmm4, %xmm7\n"
        "\tpxor    %xmm4, %xmm2\n"
        "\tpxor    %xmm4, %xmm1\n"
        "\tpxor    %xmm4, %xmm0\n"
        "\tpxor    %xmm7, %xmm4\n"
        "\tpxor    %xmm3, %xmm7\n"
        "\tpxor    %xmm3, %xmm6\n"
        "\tpxor    %xmm3, %xmm5\n"
        "\tpxor    %xmm3, %xmm4\n"
        "\tpxor    %xmm3, %xmm1\n"
        "\tpxor    %xmm3, %xmm0\n"
        "\tpxor    %xmm2, %xmm7\n"
        "\tpxor    %xmm2, %xmm6\n"
        "\tpxor    %xmm2, %xmm0\n"
        "\tpxor    %xmm1, %xmm7\n"
        "\tpxor    %xmm1, %xmm6\n"
        "\tpxor    %xmm1, %xmm4\n"
        "\tpxor    %xmm1, %xmm3\n"
        "\tpxor    %xmm0, %xmm6\n"
        "\tpxor    %xmm0, %xmm5\n"
        "\tpxor    %xmm0, %xmm3\n"
        "\tpxor    %xmm0, %xmm2\n"
    );
}

static void gf8mul_01101110(void)
{
    __asm__ __volatile__
    (
        "\tpxor    %xmm7, %xmm6\n"
        "\tpxor    %xmm6, %xmm5\n"
        "\tpxor    %xmm5, %xmm4\n"
        "\tpxor    %xmm4, %xmm3\n"
        "\tpxor    %xmm3, %xmm2\n"
        "\tpxor    %xmm2, %xmm1\n"
        "\tpxor    %xmm1, %xmm0\n"
        "\tpxor    %xmm0, %xmm7\n"
        "\tpxor    %xmm0, %xmm6\n"
        "\tpxor    %xmm0, %xmm5\n"
        "\tpxor    %xmm0, %xmm4\n"
        "\tpxor    %xmm0, %xmm2\n"
        "\tpxor    %xmm7, %xmm4\n"
        "\tpxor    %xmm7, %xmm3\n"
        "\tpxor    %xmm6, %xmm3\n"
        "\tpxor    %xmm6, %xmm2\n"
        "\tpxor    %xmm5, %xmm2\n"
        "\tpxor    %xmm5, %xmm1\n"
        "\tpxor    %xmm4, %xmm7\n"
        "\tpxor    %xmm4, %xmm2\n"
        "\tpxor    %xmm7, %xmm4\n"
        "\tpxor    %xmm3, %xmm6\n"
        "\tpxor    %xmm3, %xmm1\n"
        "\tpxor    %xmm6, %xmm3\n"
        "\tpxor    %xmm2, %xmm5\n"
        "\tpxor    %xmm2, %xmm0\n"
        "\tpxor    %xmm5, %xmm2\n"
        "\tpxor    %xmm1, %xmm7\n"
        "\tpxor    %xmm1, %xmm5\n"
        "\tpxor    %xmm1, %xmm4\n"
        "\tpxor    %xmm0, %xmm6\n"
        "\tpxor    %xmm0, %xmm4\n"
        "\tpxor    %xmm0, %xmm3\n"
    );
}

static void gf8mul_01101111(void)
{
    __asm__ __volatile__
    (
        "\tpxor    %xmm7, %xmm5\n"
        "\tpxor    %xmm7, %xmm4\n"
        "\tpxor    %xmm7, %xmm3\n"
        "\tpxor    %xmm7, %xmm1\n"
        "\tpxor    %xmm7, %xmm0\n"
        "\tpxor    %xmm6, %xmm7\n"
        "\tpxor    %xmm6, %xmm5\n"
        "\tpxor    %xmm6, %xmm3\n"
        "\tpxor    %xmm5, %xmm7\n"
        "\tpxor    %xmm5, %xmm4\n"
        "\tpxor    %xmm5, %xmm3\n"
        "\tpxor    %xmm7, %xmm5\n"
        "\tpxor    %xmm4, %xmm6\n"
        "\tpxor    %xmm4, %xmm3\n"
        "\tpxor    %xmm4, %xmm2\n"
        "\tpxor    %xmm6, %xmm4\n"
        "\tpxor    %xmm3, %xmm5\n"
        "\tpxor    %xmm3, %xmm2\n"
        "\tpxor    %xmm3, %xmm1\n"
        "\tpxor    %xmm5, %xmm3\n"
        "\tpxor    %xmm2, %xmm4\n"
        "\tpxor    %xmm2, %xmm1\n"
        "\tpxor    %xmm2, %xmm0\n"
        "\tpxor    %xmm4, %xmm2\n"
        "\tpxor    %xmm1, %xmm7\n"
        "\tpxor    %xmm1, %xmm6\n"
        "\tpxor    %xmm1, %xmm4\n"
        "\tpxor    %xmm1, %xmm3\n"
        "\tpxor    %xmm1, %xmm2\n"
        "\tpxor    %xmm0, %xmm6\n"
        "\tpxor    %xmm0, %xmm5\n"
        "\tpxor    %xmm0, %xmm3\n"
        "\tpxor    %xmm0, %xmm2\n"
        "\tpxor    %xmm0, %xmm1\n"
    );
}

static void gf8mul_01110000(void)
{
    __asm__ __volatile__
    (
        "\tpxor    %xmm7, %xmm5\n"
        "\tpxor    %xmm7, %xmm4\n"
        "\tpxor    %xmm7, %xmm3\n"
        "\tpxor    %xmm7, %xmm0\n"
        "\tpxor    %xmm6, %xmm7\n"
        "\tpxor    %xmm6, %xmm5\n"
        "\tpxor    %xmm6, %xmm3\n"
        "\tpxor    %xmm6, %xmm1\n"
        "\tpxor    %xmm6, %xmm0\n"
        "\tpxor    %xmm5, %xmm4\n"
        "\tpxor    %xmm5, %xmm3\n"
        "\tpxor    %xmm5, %xmm0\n"
        "\tpxor    %xmm4, %xmm7\n"
        "\tpxor    %xmm4, %xmm5\n"
        "\tpxor    %xmm4, %xmm1\n"
        "\tpxor    %xmm3, %xmm7\n"
        "\tpxor    %xmm3, %xmm6\n"
        "\tpxor    %xmm3, %xmm5\n"
        "\tpxor    %xmm3, %xmm2\n"
        "\tpxor    %xmm3, %xmm1\n"
        "\tpxor    %xmm3, %xmm0\n"
        "\tpxor    %xmm2, %xmm7\n"
        "\tpxor    %xmm2, %xmm6\n"
        "\tpxor    %xmm2, %xmm4\n"
        "\tpxor    %xmm2, %xmm3\n"
        "\tpxor    %xmm1, %xmm7\n"
        "\tpxor    %xmm1, %xmm6\n"
        "\tpxor    %xmm1, %xmm5\n"
        "\tpxor    %xmm5, %xmm1\n"
        "\tpxor    %xmm0, %xmm6\n"
        "\tpxor    %xmm0, %xmm5\n"
        "\tpxor    %xmm0, %xmm4\n"
        "\tpxor    %xmm4, %xmm0\n"
    );
}

static void gf8mul_01110001(void)
{
    __asm__ __volatile__
    (
        "\tpxor    %xmm7, %xmm6\n"
        "\tpxor    %xmm7, %xmm5\n"
        "\tpxor    %xmm7, %xmm4\n"
        "\tpxor    %xmm7, %xmm3\n"
        "\tpxor    %xmm7, %xmm2\n"
        "\tpxor    %xmm6, %xmm5\n"
        "\tpxor    %xmm6, %xmm4\n"
        "\tpxor    %xmm6, %xmm3\n"
        "\tpxor    %xmm6, %xmm2\n"
        "\tpxor    %xmm6, %xmm1\n"
        "\tpxor    %xmm5, %xmm4\n"
        "\tpxor    %xmm5, %xmm3\n"
        "\tpxor    %xmm5, %xmm2\n"
        "\tpxor    %xmm5, %xmm1\n"
        "\tpxor    %xmm5, %xmm0\n"
        "\tpxor    %xmm4, %xmm7\n"
        "\tpxor    %xmm4, %xmm6\n"
        "\tpxor    %xmm4, %xmm1\n"
        "\tpxor    %xmm4, %xmm0\n"
        "\tpxor    %xmm3, %xmm5\n"
        "\tpxor    %xmm3, %xmm4\n"
        "\tpxor    %xmm3, %xmm1\n"
        "\tpxor    %xmm3, %xmm0\n"
        "\tpxor    %xmm2, %xmm7\n"
        "\tpxor    %xmm2, %xmm5\n"
        "\tpxor    %xmm2, %xmm3\n"
        "\tpxor    %xmm2, %xmm0\n"
        "\tpxor    %xmm3, %xmm2\n"
        "\tpxor    %xmm1, %xmm7\n"
        "\tpxor    %xmm1, %xmm6\n"
        "\tpxor    %xmm1, %xmm5\n"
        "\tpxor    %xmm0, %xmm6\n"
        "\tpxor    %xmm0, %xmm5\n"
        "\tpxor    %xmm0, %xmm4\n"
    );
}

static void gf8mul_01110010(void)
{
    __asm__ __volatile__
    (
        "\tpxor    %xmm7, %xmm6\n"
        "\tpxor    %xmm7, %xmm1\n"
        "\tpxor    %xmm7, %xmm0\n"
        "\tpxor    %xmm6, %xmm7\n"
        "\tpxor    %xmm6, %xmm5\n"
        "\tpxor    %xmm6, %xmm3\n"
        "\tpxor    %xmm7, %xmm6\n"
        "\tpxor    %xmm5, %xmm6\n"
        "\tpxor    %xmm5, %xmm4\n"
        "\tpxor    %xmm5, %xmm2\n"
        "\tpxor    %xmm6, %xmm5\n"
        "\tpxor    %xmm4, %xmm5\n"
        "\tpxor    %xmm4, %xmm3\n"
        "\tpxor    %xmm4, %xmm1\n"
        "\tpxor    %xmm5, %xmm4\n"
        "\tpxor    %xmm3, %xmm4\n"
        "\tpxor    %xmm3, %xmm2\n"
        "\tpxor    %xmm3, %xmm0\n"
        "\tpxor    %xmm4, %xmm3\n"
        "\tpxor    %xmm2, %xmm7\n"
        "\tpxor    %xmm2, %xmm5\n"
        "\tpxor    %xmm2, %xmm0\n"
        "\tpxor    %xmm1, %xmm7\n"
        "\tpxor    %xmm1, %xmm6\n"
        "\tpxor    %xmm1, %xmm5\n"
        "\tpxor    %xmm1, %xmm2\n"
        "\tpxor    %xmm2, %xmm1\n"
        "\tpxor    %xmm0, %xmm6\n"
        "\tpxor    %xmm0, %xmm5\n"
        "\tpxor    %xmm0, %xmm4\n"
        "\tpxor    %xmm0, %xmm1\n"
        "\tpxor    %xmm1, %xmm0\n"
    );
}

static void gf8mul_01110011(void)
{
    __asm__ __volatile__
    (
        "\tpxor    %xmm7, %xmm3\n"
        "\tpxor    %xmm6, %xmm2\n"
        "\tpxor    %xmm5, %xmm1\n"
        "\tpxor    %xmm4, %xmm0\n"
        "\tpxor    %xmm3, %xmm7\n"
        "\tpxor    %xmm3, %xmm2\n"
        "\tpxor    %xmm3, %xmm1\n"
        "\tpxor    %xmm2, %xmm6\n"
        "\tpxor    %xmm2, %xmm1\n"
        "\tpxor    %xmm2, %xmm0\n"
        "\tpxor    %xmm1, %xmm7\n"
        "\tpxor    %xmm1, %xmm6\n"
        "\tpxor    %xmm1, %xmm5\n"
        "\tpxor    %xmm1, %xmm2\n"
        "\tpxor    %xmm0, %xmm6\n"
        "\tpxor    %xmm0, %xmm5\n"
        "\tpxor    %xmm0, %xmm4\n"
        "\tpxor    %xmm0, %xmm1\n"
    );
}

static void gf8mul_01110100(void)
{
    __asm__ __volatile__
    (
        "\tpxor    %xmm7, %xmm5\n"
        "\tpxor    %xmm7, %xmm3\n"
        "\tpxor    %xmm7, %xmm2\n"
        "\tpxor    %xmm7, %xmm0\n"
        "\tpxor    %xmm6, %xmm7\n"
        "\tpxor    %xmm6, %xmm5\n"
        "\tpxor    %xmm6, %xmm4\n"
        "\tpxor    %xmm6, %xmm2\n"
        "\tpxor    %xmm6, %xmm0\n"
        "\tpxor    %xmm5, %xmm7\n"
        "\tpxor    %xmm5, %xmm6\n"
        "\tpxor    %xmm5, %xmm3\n"
        "\tpxor    %xmm5, %xmm2\n"
        "\tpxor    %xmm5, %xmm1\n"
        "\tpxor    %xmm5, %xmm0\n"
        "\tpxor    %xmm4, %xmm7\n"
        "\tpxor    %xmm4, %xmm6\n"
        "\tpxor    %xmm4, %xmm5\n"
        "\tpxor    %xmm4, %xmm3\n"
        "\tpxor    %xmm4, %xmm0\n"
        "\tpxor    %xmm5, %xmm4\n"
        "\tpxor    %xmm3, %xmm6\n"
        "\tpxor    %xmm3, %xmm2\n"
        "\tpxor    %xmm2, %xmm5\n"
        "\tpxor    %xmm2, %xmm1\n"
        "\tpxor    %xmm1, %xmm7\n"
        "\tpxor    %xmm1, %xmm6\n"
        "\tpxor    %xmm1, %xmm5\n"
        "\tpxor    %xmm1, %xmm3\n"
        "\tpxor    %xmm3, %xmm1\n"
        "\tpxor    %xmm0, %xmm6\n"
        "\tpxor    %xmm0, %xmm5\n"
        "\tpxor    %xmm0, %xmm4\n"
        "\tpxor    %xmm0, %xmm2\n"
        "\tpxor    %xmm2, %xmm0\n"
    );
}

static void gf8mul_01110101(void)
{
    __asm__ __volatile__
    (
        "\tpxor    %xmm7, %xmm6\n"
        "\tpxor    %xmm7, %xmm5\n"
        "\tpxor    %xmm7, %xmm1\n"
        "\tpxor    %xmm6, %xmm7\n"
        "\tpxor    %xmm6, %xmm4\n"
        "\tpxor    %xmm6, %xmm3\n"
        "\tpxor    %xmm7, %xmm6\n"
        "\tpxor    %xmm5, %xmm6\n"
        "\tpxor    %xmm5, %xmm3\n"
        "\tpxor    %xmm5, %xmm2\n"
        "\tpxor    %xmm6, %xmm5\n"
        "\tpxor    %xmm4, %xmm5\n"
        "\tpxor    %xmm4, %xmm2\n"
        "\tpxor    %xmm4, %xmm1\n"
        "\tpxor    %xmm5, %xmm4\n"
        "\tpxor    %xmm3, %xmm4\n"
        "\tpxor    %xmm3, %xmm1\n"
        "\tpxor    %xmm3, %xmm0\n"
        "\tpxor    %xmm4, %xmm3\n"
        "\tpxor    %xmm2, %xmm7\n"
        "\tpxor    %xmm2, %xmm5\n"
        "\tpxor    %xmm2, %xmm4\n"
        "\tpxor    %xmm2, %xmm3\n"
        "\tpxor    %xmm2, %xmm0\n"
        "\tpxor    %xmm1, %xmm7\n"
        "\tpxor    %xmm1, %xmm6\n"
        "\tpxor    %xmm1, %xmm5\n"
        "\tpxor    %xmm1, %xmm3\n"
        "\tpxor    %xmm0, %xmm6\n"
        "\tpxor    %xmm0, %xmm5\n"
        "\tpxor    %xmm0, %xmm4\n"
        "\tpxor    %xmm0, %xmm2\n"
    );
}

static void gf8mul_01110110(void)
{
    __asm__ __volatile__
    (
        "\tpxor    %xmm7, %xmm6\n"
        "\tpxor    %xmm7, %xmm5\n"
        "\tpxor    %xmm7, %xmm4\n"
        "\tpxor    %xmm7, %xmm1\n"
        "\tpxor    %xmm6, %xmm7\n"
        "\tpxor    %xmm6, %xmm5\n"
        "\tpxor    %xmm6, %xmm4\n"
        "\tpxor    %xmm6, %xmm2\n"
        "\tpxor    %xmm6, %xmm1\n"
        "\tpxor    %xmm7, %xmm6\n"
        "\tpxor    %xmm5, %xmm7\n"
        "\tpxor    %xmm5, %xmm4\n"
        "\tpxor    %xmm5, %xmm2\n"
        "\tpxor    %xmm7, %xmm5\n"
        "\tpxor    %xmm4, %xmm7\n"
        "\tpxor    %xmm4, %xmm2\n"
        "\tpxor    %xmm4, %xmm0\n"
        "\tpxor    %xmm7, %xmm4\n"
        "\tpxor    %xmm3, %xmm7\n"
        "\tpxor    %xmm3, %xmm6\n"
        "\tpxor    %xmm3, %xmm2\n"
        "\tpxor    %xmm3, %xmm1\n"
        "\tpxor    %xmm3, %xmm0\n"
        "\tpxor    %xmm2, %xmm6\n"
        "\tpxor    %xmm2, %xmm4\n"
        "\tpxor    %xmm2, %xmm3\n"
        "\tpxor    %xmm2, %xmm1\n"
        "\tpxor    %xmm2, %xmm0\n"
        "\tpxor    %xmm1, %xmm7\n"
        "\tpxor    %xmm1, %xmm6\n"
        "\tpxor    %xmm1, %xmm5\n"
        "\tpxor    %xmm1, %xmm3\n"
        "\tpxor    %xmm1, %xmm2\n"
        "\tpxor    %xmm2, %xmm1\n"
        "\tpxor    %xmm0, %xmm6\n"
        "\tpxor    %xmm0, %xmm5\n"
        "\tpxor    %xmm0, %xmm4\n"
        "\tpxor    %xmm0, %xmm2\n"
        "\tpxor    %xmm0, %xmm1\n"
        "\tpxor    %xmm1, %xmm0\n"
    );
}

static void gf8mul_01110111(void)
{
    __asm__ __volatile__
    (
        "\tpxor    %xmm7, %xmm6\n"
        "\tpxor    %xmm7, %xmm5\n"
        "\tpxor    %xmm7, %xmm2\n"
        "\tpxor    %xmm7, %xmm1\n"
        "\tpxor    %xmm6, %xmm7\n"
        "\tpxor    %xmm6, %xmm4\n"
        "\tpxor    %xmm6, %xmm2\n"
        "\tpxor    %xmm6, %xmm1\n"
        "\tpxor    %xmm7, %xmm6\n"
        "\tpxor    %xmm5, %xmm6\n"
        "\tpxor    %xmm5, %xmm3\n"
        "\tpxor    %xmm5, %xmm1\n"
        "\tpxor    %xmm5, %xmm0\n"
        "\tpxor    %xmm6, %xmm5\n"
        "\tpxor    %xmm4, %xmm7\n"
        "\tpxor    %xmm4, %xmm5\n"
        "\tpxor    %xmm4, %xmm3\n"
        "\tpxor    %xmm3, %xmm7\n"
        "\tpxor    %xmm3, %xmm5\n"
        "\tpxor    %xmm3, %xmm4\n"
        "\tpxor    %xmm3, %xmm2\n"
        "\tpxor    %xmm3, %xmm1\n"
        "\tpxor    %xmm4, %xmm3\n"
        "\tpxor    %xmm2, %xmm6\n"
        "\tpxor    %xmm2, %xmm4\n"
        "\tpxor    %xmm2, %xmm3\n"
        "\tpxor    %xmm2, %xmm1\n"
        "\tpxor    %xmm2, %xmm0\n"
        "\tpxor    %xmm3, %xmm2\n"
        "\tpxor    %xmm1, %xmm7\n"
        "\tpxor    %xmm1, %xmm6\n"
        "\tpxor    %xmm1, %xmm5\n"
        "\tpxor    %xmm1, %xmm3\n"
        "\tpxor    %xmm1, %xmm2\n"
        "\tpxor    %xmm0, %xmm6\n"
        "\tpxor    %xmm0, %xmm5\n"
        "\tpxor    %xmm0, %xmm4\n"
        "\tpxor    %xmm0, %xmm2\n"
        "\tpxor    %xmm0, %xmm1\n"
    );
}

static void gf8mul_01111000(void)
{
    __asm__ __volatile__
    (
        "\tpxor    %xmm7, %xmm4\n"
        "\tpxor    %xmm7, %xmm2\n"
        "\tpxor    %xmm7, %xmm0\n"
        "\tpxor    %xmm6, %xmm7\n"
        "\tpxor    %xmm6, %xmm4\n"
        "\tpxor    %xmm6, %xmm0\n"
        "\tpxor    %xmm5, %xmm7\n"
        "\tpxor    %xmm5, %xmm6\n"
        "\tpxor    %xmm5, %xmm4\n"
        "\tpxor    %xmm5, %xmm1\n"
        "\tpxor    %xmm5, %xmm0\n"
        "\tpxor    %xmm4, %xmm6\n"
        "\tpxor    %xmm4, %xmm1\n"
        "\tpxor    %xmm4, %xmm0\n"
        "\tpxor    %xmm3, %xmm7\n"
        "\tpxor    %xmm3, %xmm4\n"
        "\tpxor    %xmm3, %xmm2\n"
        "\tpxor    %xmm3, %xmm1\n"
        "\tpxor    %xmm3, %xmm0\n"
        "\tpxor    %xmm4, %xmm3\n"
        "\tpxor    %xmm2, %xmm3\n"
        "\tpxor    %xmm2, %xmm1\n"
        "\tpxor    %xmm1, %xmm7\n"
        "\tpxor    %xmm1, %xmm6\n"
        "\tpxor    %xmm1, %xmm5\n"
        "\tpxor    %xmm1, %xmm4\n"
        "\tpxor    %xmm4, %xmm1\n"
        "\tpxor    %xmm0, %xmm6\n"
        "\tpxor    %xmm0, %xmm5\n"
        "\tpxor    %xmm0, %xmm4\n"
        "\tpxor    %xmm0, %xmm3\n"
        "\tpxor    %xmm3, %xmm0\n"
    );
}

static void gf8mul_01111001(void)
{
    __asm__ __volatile__
    (
        "\tpxor    %xmm7, %xmm6\n"
        "\tpxor    %xmm7, %xmm2\n"
        "\tpxor    %xmm6, %xmm7\n"
        "\tpxor    %xmm6, %xmm3\n"
        "\tpxor    %xmm5, %xmm7\n"
        "\tpxor    %xmm5, %xmm4\n"
        "\tpxor    %xmm5, %xmm3\n"
        "\tpxor    %xmm5, %xmm2\n"
        "\tpxor    %xmm5, %xmm1\n"
        "\tpxor    %xmm4, %xmm7\n"
        "\tpxor    %xmm4, %xmm5\n"
        "\tpxor    %xmm4, %xmm3\n"
        "\tpxor    %xmm4, %xmm2\n"
        "\tpxor    %xmm4, %xmm0\n"
        "\tpxor    %xmm5, %xmm4\n"
        "\tpxor    %xmm3, %xmm7\n"
        "\tpxor    %xmm3, %xmm6\n"
        "\tpxor    %xmm3, %xmm5\n"
        "\tpxor    %xmm3, %xmm2\n"
        "\tpxor    %xmm3, %xmm1\n"
        "\tpxor    %xmm5, %xmm3\n"
        "\tpxor    %xmm2, %xmm7\n"
        "\tpxor    %xmm2, %xmm0\n"
        "\tpxor    %xmm7, %xmm2\n"
        "\tpxor    %xmm1, %xmm7\n"
        "\tpxor    %xmm1, %xmm6\n"
        "\tpxor    %xmm1, %xmm5\n"
        "\tpxor    %xmm1, %xmm4\n"
        "\tpxor    %xmm0, %xmm6\n"
        "\tpxor    %xmm0, %xmm5\n"
        "\tpxor    %xmm0, %xmm4\n"
        "\tpxor    %xmm0, %xmm3\n"
    );
}

static void gf8mul_01111010(void)
{
    __asm__ __volatile__
    (
        "\tpxor    %xmm7, %xmm6\n"
        "\tpxor    %xmm7, %xmm5\n"
        "\tpxor    %xmm7, %xmm2\n"
        "\tpxor    %xmm7, %xmm1\n"
        "\tpxor    %xmm7, %xmm0\n"
        "\tpxor    %xmm6, %xmm7\n"
        "\tpxor    %xmm6, %xmm5\n"
        "\tpxor    %xmm6, %xmm4\n"
        "\tpxor    %xmm6, %xmm3\n"
        "\tpxor    %xmm6, %xmm1\n"
        "\tpxor    %xmm5, %xmm7\n"
        "\tpxor    %xmm5, %xmm2\n"
        "\tpxor    %xmm5, %xmm1\n"
        "\tpxor    %xmm5, %xmm0\n"
        "\tpxor    %xmm7, %xmm5\n"
        "\tpxor    %xmm4, %xmm7\n"
        "\tpxor    %xmm4, %xmm6\n"
        "\tpxor    %xmm4, %xmm5\n"
        "\tpxor    %xmm4, %xmm3\n"
        "\tpxor    %xmm4, %xmm1\n"
        "\tpxor    %xmm3, %xmm6\n"
        "\tpxor    %xmm3, %xmm5\n"
        "\tpxor    %xmm3, %xmm4\n"
        "\tpxor    %xmm3, %xmm2\n"
        "\tpxor    %xmm3, %xmm0\n"
        "\tpxor    %xmm2, %xmm7\n"
        "\tpxor    %xmm2, %xmm3\n"
        "\tpxor    %xmm2, %xmm0\n"
        "\tpxor    %xmm1, %xmm7\n"
        "\tpxor    %xmm1, %xmm6\n"
        "\tpxor    %xmm1, %xmm5\n"
        "\tpxor    %xmm1, %xmm4\n"
        "\tpxor    %xmm1, %xmm2\n"
        "\tpxor    %xmm2, %xmm1\n"
        "\tpxor    %xmm0, %xmm6\n"
        "\tpxor    %xmm0, %xmm5\n"
        "\tpxor    %xmm0, %xmm4\n"
        "\tpxor    %xmm0, %xmm3\n"
        "\tpxor    %xmm0, %xmm1\n"
        "\tpxor    %xmm1, %xmm0\n"
    );
}

static void gf8mul_01111011(void)
{
    __asm__ __volatile__
    (
        "\tpxor    %xmm7, %xmm2\n"
        "\tpxor    %xmm6, %xmm1\n"
        "\tpxor    %xmm5, %xmm0\n"
        "\tpxor    %xmm4, %xmm7\n"
        "\tpxor    %xmm4, %xmm3\n"
        "\tpxor    %xmm4, %xmm1\n"
        "\tpxor    %xmm3, %xmm6\n"
        "\tpxor    %xmm3, %xmm2\n"
        "\tpxor    %xmm3, %xmm0\n"
        "\tpxor    %xmm2, %xmm7\n"
        "\tpxor    %xmm2, %xmm4\n"
        "\tpxor    %xmm1, %xmm7\n"
        "\tpxor    %xmm1, %xmm6\n"
        "\tpxor    %xmm1, %xmm2\n"
        "\tpxor    %xmm2, %xmm1\n"
        "\tpxor    %xmm0, %xmm7\n"
        "\tpxor    %xmm0, %xmm6\n"
        "\tpxor    %xmm0, %xmm5\n"
        "\tpxor    %xmm0, %xmm3\n"
        "\tpxor    %xmm0, %xmm2\n"
        "\tpxor    %xmm2, %xmm0\n"
        "\tpxor    %xmm7, %xmm6\n"
        "\tpxor    %xmm7, %xmm3\n"
        "\tpxor    %xmm7, %xmm1\n"
        "\tpxor    %xmm7, %xmm0\n"
        "\tpxor    %xmm6, %xmm7\n"
        "\tpxor    %xmm6, %xmm5\n"
        "\tpxor    %xmm6, %xmm4\n"
        "\tpxor    %xmm6, %xmm3\n"
        "\tpxor    %xmm6, %xmm0\n"
        "\tpxor    %xmm7, %xmm6\n"
        "\tpxor    %xmm5, %xmm7\n"
        "\tpxor    %xmm4, %xmm6\n"
        "\tpxor    %xmm3, %xmm5\n"
        "\tpxor    %xmm2, %xmm4\n"
        "\tpxor    %xmm1, %xmm3\n"
        "\tpxor    %xmm0, %xmm2\n"
    );
}

static void gf8mul_01111100(void)
{
    __asm__ __volatile__
    (
        "\tpxor    %xmm7, %xmm6\n"
        "\tpxor    %xmm7, %xmm5\n"
        "\tpxor    %xmm7, %xmm4\n"
        "\tpxor    %xmm7, %xmm3\n"
        "\tpxor    %xmm7, %xmm2\n"
        "\tpxor    %xmm7, %xmm1\n"
        "\tpxor    %xmm6, %xmm7\n"
        "\tpxor    %xmm6, %xmm5\n"
        "\tpxor    %xmm6, %xmm2\n"
        "\tpxor    %xmm7, %xmm6\n"
        "\tpxor    %xmm5, %xmm7\n"
        "\tpxor    %xmm5, %xmm3\n"
        "\tpxor    %xmm5, %xmm0\n"
        "\tpxor    %xmm7, %xmm5\n"
        "\tpxor    %xmm4, %xmm7\n"
        "\tpxor    %xmm4, %xmm6\n"
        "\tpxor    %xmm4, %xmm3\n"
        "\tpxor    %xmm4, %xmm1\n"
        "\tpxor    %xmm4, %xmm0\n"
        "\tpxor    %xmm3, %xmm5\n"
        "\tpxor    %xmm3, %xmm2\n"
        "\tpxor    %xmm2, %xmm4\n"
        "\tpxor    %xmm2, %xmm1\n"
        "\tpxor    %xmm1, %xmm7\n"
        "\tpxor    %xmm1, %xmm6\n"
        "\tpxor    %xmm1, %xmm5\n"
        "\tpxor    %xmm1, %xmm4\n"
        "\tpxor    %xmm1, %xmm3\n"
        "\tpxor    %xmm3, %xmm1\n"
        "\tpxor    %xmm0, %xmm6\n"
        "\tpxor    %xmm0, %xmm5\n"
        "\tpxor    %xmm0, %xmm4\n"
        "\tpxor    %xmm0, %xmm3\n"
        "\tpxor    %xmm0, %xmm2\n"
        "\tpxor    %xmm2, %xmm0\n"
    );
}

static void gf8mul_01111101(void)
{
    __asm__ __volatile__
    (
        "\tpxor    %xmm7, %xmm6\n"
        "\tpxor    %xmm7, %xmm5\n"
        "\tpxor    %xmm7, %xmm3\n"
        "\tpxor    %xmm7, %xmm2\n"
        "\tpxor    %xmm7, %xmm1\n"
        "\tpxor    %xmm7, %xmm0\n"
        "\tpxor    %xmm6, %xmm7\n"
        "\tpxor    %xmm6, %xmm4\n"
        "\tpxor    %xmm6, %xmm3\n"
        "\tpxor    %xmm6, %xmm1\n"
        "\tpxor    %xmm7, %xmm6\n"
        "\tpxor    %xmm5, %xmm6\n"
        "\tpxor    %xmm5, %xmm3\n"
        "\tpxor    %xmm5, %xmm2\n"
        "\tpxor    %xmm5, %xmm0\n"
        "\tpxor    %xmm6, %xmm5\n"
        "\tpxor    %xmm4, %xmm7\n"
        "\tpxor    %xmm4, %xmm2\n"
        "\tpxor    %xmm4, %xmm1\n"
        "\tpxor    %xmm3, %xmm6\n"
        "\tpxor    %xmm3, %xmm1\n"
        "\tpxor    %xmm3, %xmm0\n"
        "\tpxor    %xmm2, %xmm7\n"
        "\tpxor    %xmm2, %xmm4\n"
        "\tpxor    %xmm2, %xmm0\n"
        "\tpxor    %xmm1, %xmm7\n"
        "\tpxor    %xmm1, %xmm6\n"
        "\tpxor    %xmm1, %xmm5\n"
        "\tpxor    %xmm1, %xmm4\n"
        "\tpxor    %xmm1, %xmm3\n"
        "\tpxor    %xmm0, %xmm6\n"
        "\tpxor    %xmm0, %xmm5\n"
        "\tpxor    %xmm0, %xmm4\n"
        "\tpxor    %xmm0, %xmm3\n"
        "\tpxor    %xmm0, %xmm2\n"
    );
}

static void gf8mul_01111110(void)
{
    __asm__ __volatile__
    (
        "\tpxor    %xmm7, %xmm6\n"
        "\tpxor    %xmm7, %xmm5\n"
        "\tpxor    %xmm7, %xmm1\n"
        "\tpxor    %xmm6, %xmm5\n"
        "\tpxor    %xmm6, %xmm4\n"
        "\tpxor    %xmm6, %xmm0\n"
        "\tpxor    %xmm5, %xmm7\n"
        "\tpxor    %xmm5, %xmm6\n"
        "\tpxor    %xmm5, %xmm2\n"
        "\tpxor    %xmm5, %xmm0\n"
        "\tpxor    %xmm4, %xmm7\n"
        "\tpxor    %xmm4, %xmm3\n"
        "\tpxor    %xmm4, %xmm1\n"
        "\tpxor    %xmm3, %xmm7\n"
        "\tpxor    %xmm3, %xmm5\n"
        "\tpxor    %xmm3, %xmm4\n"
        "\tpxor    %xmm3, %xmm2\n"
        "\tpxor    %xmm3, %xmm1\n"
        "\tpxor    %xmm3, %xmm0\n"
        "\tpxor    %xmm4, %xmm3\n"
        "\tpxor    %xmm2, %xmm6\n"
        "\tpxor    %xmm2, %xmm5\n"
        "\tpxor    %xmm2, %xmm1\n"
        "\tpxor    %xmm2, %xmm0\n"
        "\tpxor    %xmm1, %xmm7\n"
        "\tpxor    %xmm1, %xmm6\n"
        "\tpxor    %xmm1, %xmm5\n"
        "\tpxor    %xmm1, %xmm4\n"
        "\tpxor    %xmm1, %xmm3\n"
        "\tpxor    %xmm1, %xmm2\n"
        "\tpxor    %xmm2, %xmm1\n"
        "\tpxor    %xmm0, %xmm6\n"
        "\tpxor    %xmm0, %xmm5\n"
        "\tpxor    %xmm0, %xmm4\n"
        "\tpxor    %xmm0, %xmm3\n"
        "\tpxor    %xmm0, %xmm2\n"
        "\tpxor    %xmm0, %xmm1\n"
        "\tpxor    %xmm1, %xmm0\n"
    );
}

static void gf8mul_01111111(void)
{
    __asm__ __volatile__
    (
        "\tpxor    %xmm7, %xmm6\n"
        "\tpxor    %xmm7, %xmm5\n"
        "\tpxor    %xmm7, %xmm4\n"
        "\tpxor    %xmm7, %xmm3\n"
        "\tpxor    %xmm7, %xmm2\n"
        "\tpxor    %xmm7, %xmm1\n"
        "\tpxor    %xmm7, %xmm0\n"
        "\tpxor    %xmm6, %xmm1\n"
        "\tpxor    %xmm5, %xmm0\n"
        "\tpxor    %xmm4, %xmm6\n"
        "\tpxor    %xmm4, %xmm5\n"
        "\tpxor    %xmm4, %xmm3\n"
        "\tpxor    %xmm3, %xmm7\n"
        "\tpxor    %xmm3, %xmm6\n"
        "\tpxor    %xmm3, %xmm2\n"
        "\tpxor    %xmm3, %xmm1\n"
        "\tpxor    %xmm6, %xmm3\n"
        "\tpxor    %xmm2, %xmm6\n"
        "\tpxor    %xmm2, %xmm5\n"
        "\tpxor    %xmm2, %xmm1\n"
        "\tpxor    %xmm2, %xmm0\n"
        "\tpxor    %xmm5, %xmm2\n"
        "\tpxor    %xmm1, %xmm7\n"
        "\tpxor    %xmm1, %xmm6\n"
        "\tpxor    %xmm1, %xmm5\n"
        "\tpxor    %xmm1, %xmm4\n"
        "\tpxor    %xmm1, %xmm3\n"
        "\tpxor    %xmm1, %xmm2\n"
        "\tpxor    %xmm0, %xmm6\n"
        "\tpxor    %xmm0, %xmm5\n"
        "\tpxor    %xmm0, %xmm4\n"
        "\tpxor    %xmm0, %xmm3\n"
        "\tpxor    %xmm0, %xmm2\n"
        "\tpxor    %xmm0, %xmm1\n"
    );
}

static void gf8mul_10000000(void)
{
    __asm__ __volatile__
    (
        "\tpxor    %xmm7, %xmm5\n"
        "\tpxor    %xmm7, %xmm4\n"
        "\tpxor    %xmm7, %xmm2\n"
        "\tpxor    %xmm7, %xmm1\n"
        "\tpxor    %xmm7, %xmm0\n"
        "\tpxor    %xmm6, %xmm7\n"
        "\tpxor    %xmm6, %xmm5\n"
        "\tpxor    %xmm6, %xmm4\n"
        "\tpxor    %xmm6, %xmm3\n"
        "\tpxor    %xmm6, %xmm2\n"
        "\tpxor    %xmm6, %xmm1\n"
        "\tpxor    %xmm5, %xmm7\n"
        "\tpxor    %xmm5, %xmm6\n"
        "\tpxor    %xmm5, %xmm4\n"
        "\tpxor    %xmm5, %xmm3\n"
        "\tpxor    %xmm5, %xmm0\n"
        "\tpxor    %xmm6, %xmm5\n"
        "\tpxor    %xmm4, %xmm7\n"
        "\tpxor    %xmm4, %xmm3\n"
        "\tpxor    %xmm4, %xmm1\n"
        "\tpxor    %xmm7, %xmm4\n"
        "\tpxor    %xmm3, %xmm7\n"
        "\tpxor    %xmm3, %xmm6\n"
        "\tpxor    %xmm3, %xmm5\n"
        "\tpxor    %xmm3, %xmm1\n"
        "\tpxor    %xmm3, %xmm0\n"
        "\tpxor    %xmm2, %xmm7\n"
        "\tpxor    %xmm2, %xmm5\n"
        "\tpxor    %xmm2, %xmm1\n"
        "\tpxor    %xmm2, %xmm0\n"
        "\tpxor    %xmm1, %xmm7\n"
        "\tpxor    %xmm1, %xmm4\n"
        "\tpxor    %xmm1, %xmm3\n"
        "\tpxor    %xmm1, %xmm2\n"
        "\tpxor    %xmm1, %xmm0\n"
        "\tpxor    %xmm2, %xmm1\n"
        "\tpxor    %xmm0, %xmm7\n"
        "\tpxor    %xmm7, %xmm0\n"
    );
}

static void gf8mul_10000001(void)
{
    __asm__ __volatile__
    (
        "\tpxor    %xmm7, %xmm6\n"
        "\tpxor    %xmm7, %xmm5\n"
        "\tpxor    %xmm7, %xmm4\n"
        "\tpxor    %xmm7, %xmm0\n"
        "\tpxor    %xmm6, %xmm7\n"
        "\tpxor    %xmm6, %xmm4\n"
        "\tpxor    %xmm6, %xmm2\n"
        "\tpxor    %xmm6, %xmm0\n"
        "\tpxor    %xmm7, %xmm6\n"
        "\tpxor    %xmm5, %xmm4\n"
        "\tpxor    %xmm5, %xmm2\n"
        "\tpxor    %xmm5, %xmm1\n"
        "\tpxor    %xmm4, %xmm5\n"
        "\tpxor    %xmm4, %xmm3\n"
        "\tpxor    %xmm4, %xmm2\n"
        "\tpxor    %xmm4, %xmm1\n"
        "\tpxor    %xmm4, %xmm0\n"
        "\tpxor    %xmm5, %xmm4\n"
        "\tpxor    %xmm3, %xmm6\n"
        "\tpxor    %xmm3, %xmm5\n"
        "\tpxor    %xmm3, %xmm4\n"
        "\tpxor    %xmm2, %xmm7\n"
        "\tpxor    %xmm2, %xmm5\n"
        "\tpxor    %xmm2, %xmm1\n"
        "\tpxor    %xmm2, %xmm0\n"
        "\tpxor    %xmm5, %xmm2\n"
        "\tpxor    %xmm1, %xmm7\n"
        "\tpxor    %xmm1, %xmm4\n"
        "\tpxor    %xmm1, %xmm3\n"
        "\tpxor    %xmm1, %xmm2\n"
        "\tpxor    %xmm1, %xmm0\n"
        "\tpxor    %xmm0, %xmm7\n"
    );
}

static void gf8mul_10000010(void)
{
    __asm__ __volatile__
    (
        "\tpxor    %xmm7, %xmm6\n"
        "\tpxor    %xmm7, %xmm5\n"
        "\tpxor    %xmm7, %xmm3\n"
        "\tpxor    %xmm6, %xmm7\n"
        "\tpxor    %xmm6, %xmm4\n"
        "\tpxor    %xmm6, %xmm3\n"
        "\tpxor    %xmm6, %xmm2\n"
        "\tpxor    %xmm6, %xmm1\n"
        "\tpxor    %xmm7, %xmm6\n"
        "\tpxor    %xmm5, %xmm6\n"
        "\tpxor    %xmm5, %xmm3\n"
        "\tpxor    %xmm5, %xmm2\n"
        "\tpxor    %xmm5, %xmm1\n"
        "\tpxor    %xmm5, %xmm0\n"
        "\tpxor    %xmm6, %xmm5\n"
        "\tpxor    %xmm4, %xmm3\n"
        "\tpxor    %xmm4, %xmm2\n"
        "\tpxor    %xmm4, %xmm0\n"
        "\tpxor    %xmm3, %xmm7\n"
        "\tpxor    %xmm3, %xmm6\n"
        "\tpxor    %xmm3, %xmm4\n"
        "\tpxor    %xmm3, %xmm2\n"
        "\tpxor    %xmm3, %xmm0\n"
        "\tpxor    %xmm4, %xmm3\n"
        "\tpxor    %xmm2, %xmm7\n"
        "\tpxor    %xmm2, %xmm5\n"
        "\tpxor    %xmm2, %xmm4\n"
        "\tpxor    %xmm2, %xmm0\n"
        "\tpxor    %xmm4, %xmm2\n"
        "\tpxor    %xmm1, %xmm7\n"
        "\tpxor    %xmm1, %xmm4\n"
        "\tpxor    %xmm1, %xmm3\n"
        "\tpxor    %xmm1, %xmm0\n"
        "\tpxor    %xmm0, %xmm7\n"
        "\tpxor    %xmm0, %xmm1\n"
        "\tpxor    %xmm1, %xmm0\n"
    );
}

static void gf8mul_10000011(void)
{
    __asm__ __volatile__
    (
        "\tpxor    %xmm7, %xmm5\n"
        "\tpxor    %xmm7, %xmm0\n"
        "\tpxor    %xmm6, %xmm7\n"
        "\tpxor    %xmm6, %xmm5\n"
        "\tpxor    %xmm6, %xmm4\n"
        "\tpxor    %xmm6, %xmm3\n"
        "\tpxor    %xmm6, %xmm2\n"
        "\tpxor    %xmm6, %xmm1\n"
        "\tpxor    %xmm6, %xmm0\n"
        "\tpxor    %xmm5, %xmm7\n"
        "\tpxor    %xmm5, %xmm2\n"
        "\tpxor    %xmm5, %xmm1\n"
        "\tpxor    %xmm7, %xmm5\n"
        "\tpxor    %xmm4, %xmm6\n"
        "\tpxor    %xmm4, %xmm1\n"
        "\tpxor    %xmm4, %xmm0\n"
        "\tpxor    %xmm6, %xmm4\n"
        "\tpxor    %xmm3, %xmm6\n"
        "\tpxor    %xmm3, %xmm4\n"
        "\tpxor    %xmm3, %xmm2\n"
        "\tpxor    %xmm2, %xmm7\n"
        "\tpxor    %xmm2, %xmm5\n"
        "\tpxor    %xmm2, %xmm3\n"
        "\tpxor    %xmm2, %xmm1\n"
        "\tpxor    %xmm2, %xmm0\n"
        "\tpxor    %xmm1, %xmm7\n"
        "\tpxor    %xmm1, %xmm4\n"
        "\tpxor    %xmm1, %xmm3\n"
        "\tpxor    %xmm1, %xmm0\n"
        "\tpxor    %xmm3, %xmm1\n"
        "\tpxor    %xmm0, %xmm7\n"
        "\tpxor    %xmm0, %xmm1\n"
    );
}

static void gf8mul_10000100(void)
{
    __asm__ __volatile__
    (
        "\tpxor    %xmm7, %xmm6\n"
        "\tpxor    %xmm7, %xmm5\n"
        "\tpxor    %xmm7, %xmm3\n"
        "\tpxor    %xmm7, %xmm2\n"
        "\tpxor    %xmm6, %xmm5\n"
        "\tpxor    %xmm6, %xmm4\n"
        "\tpxor    %xmm6, %xmm2\n"
        "\tpxor    %xmm6, %xmm1\n"
        "\tpxor    %xmm5, %xmm4\n"
        "\tpxor    %xmm5, %xmm3\n"
        "\tpxor    %xmm5, %xmm1\n"
        "\tpxor    %xmm5, %xmm0\n"
        "\tpxor    %xmm4, %xmm6\n"
        "\tpxor    %xmm4, %xmm5\n"
        "\tpxor    %xmm4, %xmm3\n"
        "\tpxor    %xmm4, %xmm0\n"
        "\tpxor    %xmm3, %xmm7\n"
        "\tpxor    %xmm3, %xmm6\n"
        "\tpxor    %xmm3, %xmm4\n"
        "\tpxor    %xmm3, %xmm0\n"
        "\tpxor    %xmm4, %xmm3\n"
        "\tpxor    %xmm2, %xmm5\n"
        "\tpxor    %xmm2, %xmm4\n"
        "\tpxor    %xmm2, %xmm3\n"
        "\tpxor    %xmm2, %xmm1\n"
        "\tpxor    %xmm1, %xmm4\n"
        "\tpxor    %xmm1, %xmm2\n"
        "\tpxor    %xmm2, %xmm1\n"
        "\tpxor    %xmm0, %xmm7\n"
        "\tpxor    %xmm0, %xmm2\n"
        "\tpxor    %xmm2, %xmm0\n"
    );
}

static void gf8mul_10000101(void)
{
    __asm__ __volatile__
    (
        "\tpxor    %xmm7, %xmm6\n"
        "\tpxor    %xmm7, %xmm4\n"
        "\tpxor    %xmm7, %xmm3\n"
        "\tpxor    %xmm7, %xmm1\n"
        "\tpxor    %xmm6, %xmm5\n"
        "\tpxor    %xmm6, %xmm3\n"
        "\tpxor    %xmm6, %xmm2\n"
        "\tpxor    %xmm6, %xmm0\n"
        "\tpxor    %xmm5, %xmm7\n"
        "\tpxor    %xmm5, %xmm6\n"
        "\tpxor    %xmm5, %xmm0\n"
        "\tpxor    %xmm4, %xmm7\n"
        "\tpxor    %xmm4, %xmm5\n"
        "\tpxor    %xmm3, %xmm7\n"
        "\tpxor    %xmm3, %xmm6\n"
        "\tpxor    %xmm3, %xmm5\n"
        "\tpxor    %xmm3, %xmm2\n"
        "\tpxor    %xmm3, %xmm1\n"
        "\tpxor    %xmm3, %xmm0\n"
        "\tpxor    %xmm5, %xmm3\n"
        "\tpxor    %xmm2, %xmm7\n"
        "\tpxor    %xmm2, %xmm5\n"
        "\tpxor    %xmm2, %xmm4\n"
        "\tpxor    %xmm2, %xmm3\n"
        "\tpxor    %xmm2, %xmm1\n"
        "\tpxor    %xmm2, %xmm0\n"
        "\tpxor    %xmm1, %xmm7\n"
        "\tpxor    %xmm1, %xmm4\n"
        "\tpxor    %xmm1, %xmm0\n"
        "\tpxor    %xmm0, %xmm7\n"
        "\tpxor    %xmm0, %xmm2\n"
    );
}

static void gf8mul_10000110(void)
{
    __asm__ __volatile__
    (
        "\tpxor    %xmm7, %xmm6\n"
        "\tpxor    %xmm7, %xmm4\n"
        "\tpxor    %xmm7, %xmm3\n"
        "\tpxor    %xmm7, %xmm2\n"
        "\tpxor    %xmm6, %xmm7\n"
        "\tpxor    %xmm6, %xmm5\n"
        "\tpxor    %xmm6, %xmm1\n"
        "\tpxor    %xmm6, %xmm0\n"
        "\tpxor    %xmm7, %xmm6\n"
        "\tpxor    %xmm5, %xmm7\n"
        "\tpxor    %xmm5, %xmm2\n"
        "\tpxor    %xmm5, %xmm1\n"
        "\tpxor    %xmm5, %xmm0\n"
        "\tpxor    %xmm4, %xmm6\n"
        "\tpxor    %xmm4, %xmm5\n"
        "\tpxor    %xmm4, %xmm3\n"
        "\tpxor    %xmm4, %xmm2\n"
        "\tpxor    %xmm4, %xmm0\n"
        "\tpxor    %xmm5, %xmm4\n"
        "\tpxor    %xmm3, %xmm7\n"
        "\tpxor    %xmm3, %xmm6\n"
        "\tpxor    %xmm3, %xmm5\n"
        "\tpxor    %xmm3, %xmm2\n"
        "\tpxor    %xmm3, %xmm0\n"
        "\tpxor    %xmm5, %xmm3\n"
        "\tpxor    %xmm2, %xmm7\n"
        "\tpxor    %xmm2, %xmm5\n"
        "\tpxor    %xmm2, %xmm0\n"
        "\tpxor    %xmm1, %xmm7\n"
        "\tpxor    %xmm1, %xmm4\n"
        "\tpxor    %xmm1, %xmm2\n"
        "\tpxor    %xmm1, %xmm0\n"
        "\tpxor    %xmm0, %xmm7\n"
        "\tpxor    %xmm0, %xmm2\n"
        "\tpxor    %xmm0, %xmm1\n"
        "\tpxor    %xmm1, %xmm0\n"
    );
}

static void gf8mul_10000111(void)
{
    __asm__ __volatile__
    (
        "\tpxor    %xmm7, %xmm5\n"
        "\tpxor    %xmm7, %xmm4\n"
        "\tpxor    %xmm7, %xmm2\n"
        "\tpxor    %xmm7, %xmm0\n"
        "\tpxor    %xmm6, %xmm7\n"
        "\tpxor    %xmm6, %xmm5\n"
        "\tpxor    %xmm6, %xmm0\n"
        "\tpxor    %xmm5, %xmm7\n"
        "\tpxor    %xmm5, %xmm4\n"
        "\tpxor    %xmm5, %xmm2\n"
        "\tpxor    %xmm5, %xmm1\n"
        "\tpxor    %xmm7, %xmm5\n"
        "\tpxor    %xmm4, %xmm6\n"
        "\tpxor    %xmm4, %xmm3\n"
        "\tpxor    %xmm4, %xmm1\n"
        "\tpxor    %xmm4, %xmm0\n"
        "\tpxor    %xmm6, %xmm4\n"
        "\tpxor    %xmm3, %xmm7\n"
        "\tpxor    %xmm3, %xmm6\n"
        "\tpxor    %xmm3, %xmm4\n"
        "\tpxor    %xmm3, %xmm1\n"
        "\tpxor    %xmm3, %xmm0\n"
        "\tpxor    %xmm2, %xmm5\n"
        "\tpxor    %xmm1, %xmm7\n"
        "\tpxor    %xmm1, %xmm4\n"
        "\tpxor    %xmm1, %xmm2\n"
        "\tpxor    %xmm1, %xmm0\n"
        "\tpxor    %xmm2, %xmm1\n"
        "\tpxor    %xmm0, %xmm7\n"
        "\tpxor    %xmm0, %xmm2\n"
        "\tpxor    %xmm0, %xmm1\n"
    );
}

static void gf8mul_10001000(void)
{
    __asm__ __volatile__
    (
        "\tpxor    %xmm7, %xmm6\n"
        "\tpxor    %xmm7, %xmm2\n"
        "\tpxor    %xmm6, %xmm7\n"
        "\tpxor    %xmm6, %xmm5\n"
        "\tpxor    %xmm6, %xmm4\n"
        "\tpxor    %xmm6, %xmm1\n"
        "\tpxor    %xmm6, %xmm0\n"
        "\tpxor    %xmm7, %xmm6\n"
        "\tpxor    %xmm5, %xmm4\n"
        "\tpxor    %xmm5, %xmm0\n"
        "\tpxor    %xmm4, %xmm7\n"
        "\tpxor    %xmm4, %xmm6\n"
        "\tpxor    %xmm4, %xmm5\n"
        "\tpxor    %xmm4, %xmm0\n"
        "\tpxor    %xmm5, %xmm4\n"
        "\tpxor    %xmm3, %xmm5\n"
        "\tpxor    %xmm3, %xmm4\n"
        "\tpxor    %xmm3, %xmm1\n"
        "\tpxor    %xmm2, %xmm7\n"
        "\tpxor    %xmm2, %xmm4\n"
        "\tpxor    %xmm2, %xmm3\n"
        "\tpxor    %xmm2, %xmm1\n"
        "\tpxor    %xmm2, %xmm0\n"
        "\tpxor    %xmm1, %xmm3\n"
        "\tpxor    %xmm1, %xmm2\n"
        "\tpxor    %xmm2, %xmm1\n"
        "\tpxor    %xmm0, %xmm7\n"
        "\tpxor    %xmm0, %xmm3\n"
        "\tpxor    %xmm3, %xmm0\n"
    );
}

static void gf8mul_10001001(void)
{
    __asm__ __volatile__
    (
        "\tpxor    %xmm7, %xmm5\n"
        "\tpxor    %xmm7, %xmm2\n"
        "\tpxor    %xmm7, %xmm0\n"
        "\tpxor    %xmm6, %xmm7\n"
        "\tpxor    %xmm6, %xmm5\n"
        "\tpxor    %xmm6, %xmm4\n"
        "\tpxor    %xmm6, %xmm3\n"
        "\tpxor    %xmm6, %xmm0\n"
        "\tpxor    %xmm5, %xmm7\n"
        "\tpxor    %xmm5, %xmm6\n"
        "\tpxor    %xmm4, %xmm7\n"
        "\tpxor    %xmm4, %xmm6\n"
        "\tpxor    %xmm4, %xmm5\n"
        "\tpxor    %xmm4, %xmm2\n"
        "\tpxor    %xmm4, %xmm1\n"
        "\tpxor    %xmm4, %xmm0\n"
        "\tpxor    %xmm5, %xmm4\n"
        "\tpxor    %xmm3, %xmm5\n"
        "\tpxor    %xmm3, %xmm4\n"
        "\tpxor    %xmm2, %xmm7\n"
        "\tpxor    %xmm2, %xmm4\n"
        "\tpxor    %xmm2, %xmm3\n"
        "\tpxor    %xmm2, %xmm1\n"
        "\tpxor    %xmm2, %xmm0\n"
        "\tpxor    %xmm3, %xmm2\n"
        "\tpxor    %xmm1, %xmm7\n"
        "\tpxor    %xmm1, %xmm2\n"
        "\tpxor    %xmm1, %xmm0\n"
        "\tpxor    %xmm0, %xmm7\n"
        "\tpxor    %xmm0, %xmm3\n"
    );
}

static void gf8mul_10001010(void)
{
    __asm__ __volatile__
    (
        "\tpxor    %xmm7, %xmm6\n"
        "\tpxor    %xmm7, %xmm3\n"
        "\tpxor    %xmm7, %xmm1\n"
        "\tpxor    %xmm7, %xmm0\n"
        "\tpxor    %xmm6, %xmm7\n"
        "\tpxor    %xmm6, %xmm5\n"
        "\tpxor    %xmm6, %xmm3\n"
        "\tpxor    %xmm6, %xmm1\n"
        "\tpxor    %xmm7, %xmm6\n"
        "\tpxor    %xmm5, %xmm6\n"
        "\tpxor    %xmm5, %xmm4\n"
        "\tpxor    %xmm5, %xmm2\n"
        "\tpxor    %xmm5, %xmm0\n"
        "\tpxor    %xmm6, %xmm5\n"
        "\tpxor    %xmm4, %xmm7\n"
        "\tpxor    %xmm4, %xmm6\n"
        "\tpxor    %xmm4, %xmm2\n"
        "\tpxor    %xmm4, %xmm0\n"
        "\tpxor    %xmm3, %xmm7\n"
        "\tpxor    %xmm3, %xmm5\n"
        "\tpxor    %xmm3, %xmm4\n"
        "\tpxor    %xmm3, %xmm2\n"
        "\tpxor    %xmm3, %xmm0\n"
        "\tpxor    %xmm2, %xmm7\n"
        "\tpxor    %xmm2, %xmm4\n"
        "\tpxor    %xmm2, %xmm3\n"
        "\tpxor    %xmm2, %xmm0\n"
        "\tpxor    %xmm3, %xmm2\n"
        "\tpxor    %xmm1, %xmm7\n"
        "\tpxor    %xmm1, %xmm0\n"
        "\tpxor    %xmm0, %xmm7\n"
        "\tpxor    %xmm0, %xmm3\n"
        "\tpxor    %xmm0, %xmm1\n"
        "\tpxor    %xmm1, %xmm0\n"
    );
}

static void gf8mul_10001011(void)
{
    __asm__ __volatile__
    (
        "\tpxor    %xmm7, %xmm6\n"
        "\tpxor    %xmm7, %xmm4\n"
        "\tpxor    %xmm7, %xmm2\n"
        "\tpxor    %xmm7, %xmm1\n"
        "\tpxor    %xmm6, %xmm7\n"
        "\tpxor    %xmm6, %xmm4\n"
        "\tpxor    %xmm6, %xmm3\n"
        "\tpxor    %xmm6, %xmm0\n"
        "\tpxor    %xmm7, %xmm6\n"
        "\tpxor    %xmm5, %xmm1\n"
        "\tpxor    %xmm5, %xmm0\n"
        "\tpxor    %xmm4, %xmm7\n"
        "\tpxor    %xmm4, %xmm6\n"
        "\tpxor    %xmm4, %xmm5\n"
        "\tpxor    %xmm4, %xmm3\n"
        "\tpxor    %xmm4, %xmm2\n"
        "\tpxor    %xmm4, %xmm1\n"
        "\tpxor    %xmm4, %xmm0\n"
        "\tpxor    %xmm5, %xmm4\n"
        "\tpxor    %xmm3, %xmm7\n"
        "\tpxor    %xmm3, %xmm5\n"
        "\tpxor    %xmm3, %xmm4\n"
        "\tpxor    %xmm3, %xmm2\n"
        "\tpxor    %xmm3, %xmm1\n"
        "\tpxor    %xmm3, %xmm0\n"
        "\tpxor    %xmm2, %xmm7\n"
        "\tpxor    %xmm2, %xmm4\n"
        "\tpxor    %xmm2, %xmm1\n"
        "\tpxor    %xmm2, %xmm0\n"
        "\tpxor    %xmm1, %xmm7\n"
        "\tpxor    %xmm1, %xmm0\n"
        "\tpxor    %xmm7, %xmm1\n"
        "\tpxor    %xmm0, %xmm7\n"
        "\tpxor    %xmm0, %xmm3\n"
        "\tpxor    %xmm0, %xmm1\n"
    );
}

static void gf8mul_10001100(void)
{
    __asm__ __volatile__
    (
        "\tpxor    %xmm7, %xmm6\n"
        "\tpxor    %xmm7, %xmm5\n"
        "\tpxor    %xmm7, %xmm4\n"
        "\tpxor    %xmm7, %xmm1\n"
        "\tpxor    %xmm6, %xmm5\n"
        "\tpxor    %xmm6, %xmm4\n"
        "\tpxor    %xmm6, %xmm3\n"
        "\tpxor    %xmm6, %xmm0\n"
        "\tpxor    %xmm5, %xmm7\n"
        "\tpxor    %xmm5, %xmm6\n"
        "\tpxor    %xmm5, %xmm4\n"
        "\tpxor    %xmm5, %xmm3\n"
        "\tpxor    %xmm5, %xmm0\n"
        "\tpxor    %xmm4, %xmm7\n"
        "\tpxor    %xmm4, %xmm5\n"
        "\tpxor    %xmm4, %xmm3\n"
        "\tpxor    %xmm4, %xmm0\n"
        "\tpxor    %xmm3, %xmm7\n"
        "\tpxor    %xmm3, %xmm4\n"
        "\tpxor    %xmm3, %xmm0\n"
        "\tpxor    %xmm2, %xmm3\n"
        "\tpxor    %xmm2, %xmm1\n"
        "\tpxor    %xmm1, %xmm2\n"
        "\tpxor    %xmm2, %xmm1\n"
        "\tpxor    %xmm0, %xmm7\n"
        "\tpxor    %xmm0, %xmm3\n"
        "\tpxor    %xmm0, %xmm2\n"
        "\tpxor    %xmm2, %xmm0\n"
    );
}

static void gf8mul_10001101(void)
{
    __asm__ __volatile__
    (
        "\tpxor    %xmm7, %xmm5\n"
        "\tpxor    %xmm7, %xmm4\n"
        "\tpxor    %xmm7, %xmm3\n"
        "\tpxor    %xmm7, %xmm2\n"
        "\tpxor    %xmm7, %xmm1\n"
        "\tpxor    %xmm6, %xmm4\n"
        "\tpxor    %xmm6, %xmm3\n"
        "\tpxor    %xmm6, %xmm2\n"
        "\tpxor    %xmm6, %xmm1\n"
        "\tpxor    %xmm6, %xmm0\n"
        "\tpxor    %xmm5, %xmm6\n"
        "\tpxor    %xmm4, %xmm7\n"
        "\tpxor    %xmm4, %xmm5\n"
        "\tpxor    %xmm4, %xmm3\n"
        "\tpxor    %xmm4, %xmm2\n"
        "\tpxor    %xmm4, %xmm1\n"
        "\tpxor    %xmm4, %xmm0\n"
        "\tpxor    %xmm5, %xmm4\n"
        "\tpxor    %xmm3, %xmm7\n"
        "\tpxor    %xmm3, %xmm4\n"
        "\tpxor    %xmm3, %xmm2\n"
        "\tpxor    %xmm3, %xmm1\n"
        "\tpxor    %xmm3, %xmm0\n"
        "\tpxor    %xmm2, %xmm7\n"
        "\tpxor    %xmm2, %xmm1\n"
        "\tpxor    %xmm2, %xmm0\n"
        "\tpxor    %xmm1, %xmm7\n"
        "\tpxor    %xmm1, %xmm3\n"
        "\tpxor    %xmm1, %xmm0\n"
        "\tpxor    %xmm0, %xmm7\n"
        "\tpxor    %xmm0, %xmm3\n"
        "\tpxor    %xmm0, %xmm2\n"
    );
}

static void gf8mul_10001110(void)
{
    __asm__ __volatile__
    (
        "\tpxor    %xmm7, %xmm6\n"
        "\tpxor    %xmm7, %xmm5\n"
        "\tpxor    %xmm7, %xmm4\n"
        "\tpxor    %xmm7, %xmm3\n"
        "\tpxor    %xmm7, %xmm2\n"
        "\tpxor    %xmm7, %xmm0\n"
        "\tpxor    %xmm6, %xmm7\n"
        "\tpxor    %xmm6, %xmm5\n"
        "\tpxor    %xmm6, %xmm4\n"
        "\tpxor    %xmm6, %xmm3\n"
        "\tpxor    %xmm6, %xmm2\n"
        "\tpxor    %xmm6, %xmm0\n"
        "\tpxor    %xmm7, %xmm6\n"
        "\tpxor    %xmm5, %xmm7\n"
        "\tpxor    %xmm5, %xmm4\n"
        "\tpxor    %xmm5, %xmm3\n"
        "\tpxor    %xmm5, %xmm2\n"
        "\tpxor    %xmm5, %xmm0\n"
        "\tpxor    %xmm7, %xmm5\n"
        "\tpxor    %xmm4, %xmm7\n"
        "\tpxor    %xmm4, %xmm3\n"
        "\tpxor    %xmm4, %xmm2\n"
        "\tpxor    %xmm4, %xmm0\n"
        "\tpxor    %xmm7, %xmm4\n"
        "\tpxor    %xmm3, %xmm7\n"
        "\tpxor    %xmm3, %xmm2\n"
        "\tpxor    %xmm3, %xmm0\n"
        "\tpxor    %xmm2, %xmm7\n"
        "\tpxor    %xmm2, %xmm3\n"
        "\tpxor    %xmm2, %xmm0\n"
        "\tpxor    %xmm1, %xmm7\n"
        "\tpxor    %xmm1, %xmm3\n"
        "\tpxor    %xmm1, %xmm2\n"
        "\tpxor    %xmm1, %xmm0\n"
        "\tpxor    %xmm0, %xmm7\n"
        "\tpxor    %xmm0, %xmm3\n"
        "\tpxor    %xmm0, %xmm2\n"
        "\tpxor    %xmm0, %xmm1\n"
        "\tpxor    %xmm1, %xmm0\n"
    );
}

static void gf8mul_10001111(void)
{
    __asm__ __volatile__
    (
        "\tpxor    %xmm7, %xmm6\n"
        "\tpxor    %xmm7, %xmm5\n"
        "\tpxor    %xmm7, %xmm4\n"
        "\tpxor    %xmm6, %xmm7\n"
        "\tpxor    %xmm6, %xmm5\n"
        "\tpxor    %xmm6, %xmm4\n"
        "\tpxor    %xmm6, %xmm3\n"
        "\tpxor    %xmm6, %xmm1\n"
        "\tpxor    %xmm6, %xmm0\n"
        "\tpxor    %xmm5, %xmm7\n"
        "\tpxor    %xmm5, %xmm4\n"
        "\tpxor    %xmm5, %xmm3\n"
        "\tpxor    %xmm5, %xmm1\n"
        "\tpxor    %xmm5, %xmm0\n"
        "\tpxor    %xmm4, %xmm7\n"
        "\tpxor    %xmm4, %xmm3\n"
        "\tpxor    %xmm4, %xmm1\n"
        "\tpxor    %xmm4, %xmm0\n"
        "\tpxor    %xmm3, %xmm7\n"
        "\tpxor    %xmm3, %xmm1\n"
        "\tpxor    %xmm3, %xmm0\n"
        "\tpxor    %xmm7, %xmm3\n"
        "\tpxor    %xmm1, %xmm7\n"
        "\tpxor    %xmm1, %xmm3\n"
        "\tpxor    %xmm1, %xmm2\n"
        "\tpxor    %xmm1, %xmm0\n"
        "\tpxor    %xmm2, %xmm1\n"
        "\tpxor    %xmm0, %xmm7\n"
        "\tpxor    %xmm0, %xmm3\n"
        "\tpxor    %xmm0, %xmm2\n"
        "\tpxor    %xmm0, %xmm1\n"
    );
}

static void gf8mul_10010000(void)
{
    __asm__ __volatile__
    (
        "\tpxor    %xmm7, %xmm4\n"
        "\tpxor    %xmm7, %xmm3\n"
        "\tpxor    %xmm7, %xmm2\n"
        "\tpxor    %xmm6, %xmm3\n"
        "\tpxor    %xmm6, %xmm2\n"
        "\tpxor    %xmm6, %xmm1\n"
        "\tpxor    %xmm5, %xmm2\n"
        "\tpxor    %xmm5, %xmm1\n"
        "\tpxor    %xmm5, %xmm0\n"
        "\tpxor    %xmm4, %xmm7\n"
        "\tpxor    %xmm4, %xmm3\n"
        "\tpxor    %xmm4, %xmm0\n"
        "\tpxor    %xmm3, %xmm6\n"
        "\tpxor    %xmm3, %xmm4\n"
        "\tpxor    %xmm3, %xmm1\n"
        "\tpxor    %xmm3, %xmm0\n"
        "\tpxor    %xmm2, %xmm7\n"
        "\tpxor    %xmm2, %xmm6\n"
        "\tpxor    %xmm2, %xmm4\n"
        "\tpxor    %xmm2, %xmm1\n"
        "\tpxor    %xmm2, %xmm0\n"
        "\tpxor    %xmm1, %xmm5\n"
        "\tpxor    %xmm1, %xmm4\n"
        "\tpxor    %xmm1, %xmm3\n"
        "\tpxor    %xmm1, %xmm2\n"
        "\tpxor    %xmm2, %xmm1\n"
        "\tpxor    %xmm0, %xmm7\n"
        "\tpxor    %xmm0, %xmm4\n"
        "\tpxor    %xmm4, %xmm0\n"
    );
}

static void gf8mul_10010001(void)
{
    __asm__ __volatile__
    (
        "\tpxor    %xmm7, %xmm5\n"
        "\tpxor    %xmm7, %xmm3\n"
        "\tpxor    %xmm7, %xmm2\n"
        "\tpxor    %xmm7, %xmm1\n"
        "\tpxor    %xmm7, %xmm0\n"
        "\tpxor    %xmm6, %xmm7\n"
        "\tpxor    %xmm6, %xmm5\n"
        "\tpxor    %xmm6, %xmm4\n"
        "\tpxor    %xmm6, %xmm2\n"
        "\tpxor    %xmm6, %xmm1\n"
        "\tpxor    %xmm5, %xmm6\n"
        "\tpxor    %xmm5, %xmm4\n"
        "\tpxor    %xmm5, %xmm3\n"
        "\tpxor    %xmm5, %xmm1\n"
        "\tpxor    %xmm5, %xmm0\n"
        "\tpxor    %xmm4, %xmm6\n"
        "\tpxor    %xmm4, %xmm5\n"
        "\tpxor    %xmm4, %xmm0\n"
        "\tpxor    %xmm3, %xmm7\n"
        "\tpxor    %xmm3, %xmm6\n"
        "\tpxor    %xmm3, %xmm5\n"
        "\tpxor    %xmm3, %xmm4\n"
        "\tpxor    %xmm2, %xmm7\n"
        "\tpxor    %xmm2, %xmm6\n"
        "\tpxor    %xmm2, %xmm4\n"
        "\tpxor    %xmm2, %xmm1\n"
        "\tpxor    %xmm2, %xmm0\n"
        "\tpxor    %xmm4, %xmm2\n"
        "\tpxor    %xmm1, %xmm7\n"
        "\tpxor    %xmm1, %xmm5\n"
        "\tpxor    %xmm1, %xmm3\n"
        "\tpxor    %xmm1, %xmm2\n"
        "\tpxor    %xmm1, %xmm0\n"
        "\tpxor    %xmm0, %xmm7\n"
        "\tpxor    %xmm0, %xmm4\n"
    );
}

static void gf8mul_10010010(void)
{
    __asm__ __volatile__
    (
        "\tpxor    %xmm7, %xmm5\n"
        "\tpxor    %xmm7, %xmm4\n"
        "\tpxor    %xmm7, %xmm3\n"
        "\tpxor    %xmm7, %xmm2\n"
        "\tpxor    %xmm7, %xmm0\n"
        "\tpxor    %xmm6, %xmm7\n"
        "\tpxor    %xmm6, %xmm5\n"
        "\tpxor    %xmm6, %xmm3\n"
        "\tpxor    %xmm6, %xmm2\n"
        "\tpxor    %xmm6, %xmm0\n"
        "\tpxor    %xmm5, %xmm7\n"
        "\tpxor    %xmm5, %xmm6\n"
        "\tpxor    %xmm5, %xmm2\n"
        "\tpxor    %xmm5, %xmm1\n"
        "\tpxor    %xmm4, %xmm7\n"
        "\tpxor    %xmm4, %xmm6\n"
        "\tpxor    %xmm4, %xmm5\n"
        "\tpxor    %xmm4, %xmm1\n"
        "\tpxor    %xmm5, %xmm4\n"
        "\tpxor    %xmm3, %xmm7\n"
        "\tpxor    %xmm3, %xmm6\n"
        "\tpxor    %xmm3, %xmm5\n"
        "\tpxor    %xmm5, %xmm3\n"
        "\tpxor    %xmm2, %xmm7\n"
        "\tpxor    %xmm2, %xmm6\n"
        "\tpxor    %xmm2, %xmm5\n"
        "\tpxor    %xmm2, %xmm0\n"
        "\tpxor    %xmm5, %xmm2\n"
        "\tpxor    %xmm1, %xmm7\n"
        "\tpxor    %xmm1, %xmm5\n"
        "\tpxor    %xmm1, %xmm3\n"
        "\tpxor    %xmm1, %xmm0\n"
        "\tpxor    %xmm0, %xmm7\n"
        "\tpxor    %xmm0, %xmm4\n"
        "\tpxor    %xmm0, %xmm1\n"
        "\tpxor    %xmm1, %xmm0\n"
    );
}

static void gf8mul_10010011(void)
{
    __asm__ __volatile__
    (
        "\tpxor    %xmm7, %xmm6\n"
        "\tpxor    %xmm7, %xmm5\n"
        "\tpxor    %xmm7, %xmm3\n"
        "\tpxor    %xmm7, %xmm2\n"
        "\tpxor    %xmm7, %xmm1\n"
        "\tpxor    %xmm7, %xmm0\n"
        "\tpxor    %xmm6, %xmm7\n"
        "\tpxor    %xmm6, %xmm4\n"
        "\tpxor    %xmm6, %xmm2\n"
        "\tpxor    %xmm7, %xmm6\n"
        "\tpxor    %xmm5, %xmm6\n"
        "\tpxor    %xmm5, %xmm3\n"
        "\tpxor    %xmm5, %xmm1\n"
        "\tpxor    %xmm6, %xmm5\n"
        "\tpxor    %xmm4, %xmm5\n"
        "\tpxor    %xmm4, %xmm2\n"
        "\tpxor    %xmm4, %xmm0\n"
        "\tpxor    %xmm5, %xmm4\n"
        "\tpxor    %xmm3, %xmm7\n"
        "\tpxor    %xmm3, %xmm4\n"
        "\tpxor    %xmm3, %xmm2\n"
        "\tpxor    %xmm2, %xmm7\n"
        "\tpxor    %xmm2, %xmm6\n"
        "\tpxor    %xmm2, %xmm4\n"
        "\tpxor    %xmm2, %xmm3\n"
        "\tpxor    %xmm2, %xmm1\n"
        "\tpxor    %xmm2, %xmm0\n"
        "\tpxor    %xmm1, %xmm7\n"
        "\tpxor    %xmm1, %xmm5\n"
        "\tpxor    %xmm1, %xmm3\n"
        "\tpxor    %xmm1, %xmm0\n"
        "\tpxor    %xmm3, %xmm1\n"
        "\tpxor    %xmm0, %xmm7\n"
        "\tpxor    %xmm0, %xmm4\n"
        "\tpxor    %xmm0, %xmm1\n"
    );
}

static void gf8mul_10010100(void)
{
    __asm__ __volatile__
    (
        "\tpxor    %xmm7, %xmm6\n"
        "\tpxor    %xmm7, %xmm5\n"
        "\tpxor    %xmm7, %xmm4\n"
        "\tpxor    %xmm7, %xmm1\n"
        "\tpxor    %xmm6, %xmm7\n"
        "\tpxor    %xmm6, %xmm4\n"
        "\tpxor    %xmm6, %xmm2\n"
        "\tpxor    %xmm6, %xmm1\n"
        "\tpxor    %xmm6, %xmm0\n"
        "\tpxor    %xmm7, %xmm6\n"
        "\tpxor    %xmm5, %xmm6\n"
        "\tpxor    %xmm5, %xmm3\n"
        "\tpxor    %xmm5, %xmm2\n"
        "\tpxor    %xmm5, %xmm1\n"
        "\tpxor    %xmm5, %xmm0\n"
        "\tpxor    %xmm4, %xmm7\n"
        "\tpxor    %xmm4, %xmm1\n"
        "\tpxor    %xmm7, %xmm4\n"
        "\tpxor    %xmm3, %xmm6\n"
        "\tpxor    %xmm3, %xmm0\n"
        "\tpxor    %xmm6, %xmm3\n"
        "\tpxor    %xmm2, %xmm6\n"
        "\tpxor    %xmm2, %xmm4\n"
        "\tpxor    %xmm2, %xmm3\n"
        "\tpxor    %xmm2, %xmm1\n"
        "\tpxor    %xmm1, %xmm5\n"
        "\tpxor    %xmm1, %xmm4\n"
        "\tpxor    %xmm1, %xmm2\n"
        "\tpxor    %xmm2, %xmm1\n"
        "\tpxor    %xmm0, %xmm7\n"
        "\tpxor    %xmm0, %xmm4\n"
        "\tpxor    %xmm0, %xmm2\n"
        "\tpxor    %xmm2, %xmm0\n"
    );
}

static void gf8mul_10010101(void)
{
    __asm__ __volatile__
    (
        "\tpxor    %xmm7, %xmm3\n"
        "\tpxor    %xmm6, %xmm2\n"
        "\tpxor    %xmm5, %xmm1\n"
        "\tpxor    %xmm4, %xmm0\n"
        "\tpxor    %xmm3, %xmm7\n"
        "\tpxor    %xmm3, %xmm2\n"
        "\tpxor    %xmm3, %xmm1\n"
        "\tpxor    %xmm2, %xmm6\n"
        "\tpxor    %xmm2, %xmm1\n"
        "\tpxor    %xmm2, %xmm0\n"
        "\tpxor    %xmm1, %xmm7\n"
        "\tpxor    %xmm1, %xmm6\n"
        "\tpxor    %xmm1, %xmm5\n"
        "\tpxor    %xmm1, %xmm2\n"
        "\tpxor    %xmm0, %xmm6\n"
        "\tpxor    %xmm0, %xmm5\n"
        "\tpxor    %xmm0, %xmm4\n"
        "\tpxor    %xmm0, %xmm1\n"
        "\tpxor    %xmm7, %xmm3\n"
        "\tpxor    %xmm7, %xmm1\n"
        "\tpxor    %xmm7, %xmm0\n"
        "\tpxor    %xmm6, %xmm7\n"
        "\tpxor    %xmm5, %xmm6\n"
        "\tpxor    %xmm4, %xmm5\n"
        "\tpxor    %xmm3, %xmm4\n"
        "\tpxor    %xmm2, %xmm3\n"
        "\tpxor    %xmm1, %xmm2\n"
        "\tpxor    %xmm0, %xmm1\n"
    );
}

static void gf8mul_10010110(void)
{
    __asm__ __volatile__
    (
        "\tpxor    %xmm7, %xmm6\n"
        "\tpxor    %xmm7, %xmm4\n"
        "\tpxor    %xmm7, %xmm1\n"
        "\tpxor    %xmm7, %xmm0\n"
        "\tpxor    %xmm6, %xmm7\n"
        "\tpxor    %xmm6, %xmm0\n"
        "\tpxor    %xmm5, %xmm7\n"
        "\tpxor    %xmm5, %xmm6\n"
        "\tpxor    %xmm5, %xmm4\n"
        "\tpxor    %xmm5, %xmm2\n"
        "\tpxor    %xmm6, %xmm5\n"
        "\tpxor    %xmm4, %xmm6\n"
        "\tpxor    %xmm4, %xmm5\n"
        "\tpxor    %xmm4, %xmm3\n"
        "\tpxor    %xmm4, %xmm1\n"
        "\tpxor    %xmm5, %xmm4\n"
        "\tpxor    %xmm3, %xmm5\n"
        "\tpxor    %xmm3, %xmm4\n"
        "\tpxor    %xmm3, %xmm2\n"
        "\tpxor    %xmm3, %xmm0\n"
        "\tpxor    %xmm4, %xmm3\n"
        "\tpxor    %xmm2, %xmm7\n"
        "\tpxor    %xmm2, %xmm6\n"
        "\tpxor    %xmm2, %xmm5\n"
        "\tpxor    %xmm2, %xmm4\n"
        "\tpxor    %xmm2, %xmm0\n"
        "\tpxor    %xmm1, %xmm7\n"
        "\tpxor    %xmm1, %xmm5\n"
        "\tpxor    %xmm1, %xmm2\n"
        "\tpxor    %xmm1, %xmm0\n"
        "\tpxor    %xmm0, %xmm7\n"
        "\tpxor    %xmm0, %xmm4\n"
        "\tpxor    %xmm0, %xmm2\n"
        "\tpxor    %xmm0, %xmm1\n"
        "\tpxor    %xmm1, %xmm0\n"
    );
}

static void gf8mul_10010111(void)
{
    __asm__ __volatile__
    (
        "\tpxor    %xmm7, %xmm6\n"
        "\tpxor    %xmm7, %xmm5\n"
        "\tpxor    %xmm7, %xmm4\n"
        "\tpxor    %xmm6, %xmm7\n"
        "\tpxor    %xmm6, %xmm5\n"
        "\tpxor    %xmm6, %xmm3\n"
        "\tpxor    %xmm6, %xmm2\n"
        "\tpxor    %xmm6, %xmm1\n"
        "\tpxor    %xmm7, %xmm6\n"
        "\tpxor    %xmm5, %xmm7\n"
        "\tpxor    %xmm5, %xmm0\n"
        "\tpxor    %xmm7, %xmm5\n"
        "\tpxor    %xmm4, %xmm7\n"
        "\tpxor    %xmm4, %xmm6\n"
        "\tpxor    %xmm4, %xmm2\n"
        "\tpxor    %xmm4, %xmm1\n"
        "\tpxor    %xmm3, %xmm6\n"
        "\tpxor    %xmm3, %xmm5\n"
        "\tpxor    %xmm3, %xmm1\n"
        "\tpxor    %xmm3, %xmm0\n"
        "\tpxor    %xmm2, %xmm6\n"
        "\tpxor    %xmm2, %xmm5\n"
        "\tpxor    %xmm1, %xmm7\n"
        "\tpxor    %xmm1, %xmm5\n"
        "\tpxor    %xmm1, %xmm2\n"
        "\tpxor    %xmm1, %xmm0\n"
        "\tpxor    %xmm2, %xmm1\n"
        "\tpxor    %xmm0, %xmm7\n"
        "\tpxor    %xmm0, %xmm4\n"
        "\tpxor    %xmm0, %xmm2\n"
        "\tpxor    %xmm0, %xmm1\n"
    );
}

static void gf8mul_10011000(void)
{
    __asm__ __volatile__
    (
        "\tpxor    %xmm7, %xmm6\n"
        "\tpxor    %xmm7, %xmm5\n"
        "\tpxor    %xmm7, %xmm3\n"
        "\tpxor    %xmm7, %xmm0\n"
        "\tpxor    %xmm6, %xmm3\n"
        "\tpxor    %xmm6, %xmm1\n"
        "\tpxor    %xmm5, %xmm6\n"
        "\tpxor    %xmm5, %xmm4\n"
        "\tpxor    %xmm5, %xmm2\n"
        "\tpxor    %xmm5, %xmm1\n"
        "\tpxor    %xmm5, %xmm0\n"
        "\tpxor    %xmm4, %xmm7\n"
        "\tpxor    %xmm4, %xmm6\n"
        "\tpxor    %xmm4, %xmm1\n"
        "\tpxor    %xmm4, %xmm0\n"
        "\tpxor    %xmm6, %xmm4\n"
        "\tpxor    %xmm3, %xmm7\n"
        "\tpxor    %xmm3, %xmm4\n"
        "\tpxor    %xmm3, %xmm1\n"
        "\tpxor    %xmm2, %xmm7\n"
        "\tpxor    %xmm2, %xmm6\n"
        "\tpxor    %xmm2, %xmm5\n"
        "\tpxor    %xmm2, %xmm3\n"
        "\tpxor    %xmm2, %xmm1\n"
        "\tpxor    %xmm2, %xmm0\n"
        "\tpxor    %xmm1, %xmm5\n"
        "\tpxor    %xmm1, %xmm3\n"
        "\tpxor    %xmm1, %xmm2\n"
        "\tpxor    %xmm2, %xmm1\n"
        "\tpxor    %xmm0, %xmm7\n"
        "\tpxor    %xmm0, %xmm4\n"
        "\tpxor    %xmm0, %xmm3\n"
        "\tpxor    %xmm3, %xmm0\n"
    );
}

static void gf8mul_10011001(void)
{
    __asm__ __volatile__
    (
        "\tpxor    %xmm7, %xmm5\n"
        "\tpxor    %xmm7, %xmm4\n"
        "\tpxor    %xmm6, %xmm4\n"
        "\tpxor    %xmm6, %xmm3\n"
        "\tpxor    %xmm5, %xmm7\n"
        "\tpxor    %xmm5, %xmm4\n"
        "\tpxor    %xmm5, %xmm1\n"
        "\tpxor    %xmm7, %xmm5\n"
        "\tpxor    %xmm4, %xmm6\n"
        "\tpxor    %xmm4, %xmm3\n"
        "\tpxor    %xmm4, %xmm0\n"
        "\tpxor    %xmm6, %xmm4\n"
        "\tpxor    %xmm3, %xmm7\n"
        "\tpxor    %xmm3, %xmm5\n"
        "\tpxor    %xmm3, %xmm4\n"
        "\tpxor    %xmm2, %xmm7\n"
        "\tpxor    %xmm2, %xmm6\n"
        "\tpxor    %xmm2, %xmm5\n"
        "\tpxor    %xmm2, %xmm3\n"
        "\tpxor    %xmm2, %xmm1\n"
        "\tpxor    %xmm2, %xmm0\n"
        "\tpxor    %xmm3, %xmm2\n"
        "\tpxor    %xmm1, %xmm7\n"
        "\tpxor    %xmm1, %xmm5\n"
        "\tpxor    %xmm1, %xmm4\n"
        "\tpxor    %xmm1, %xmm2\n"
        "\tpxor    %xmm1, %xmm0\n"
        "\tpxor    %xmm0, %xmm7\n"
        "\tpxor    %xmm0, %xmm4\n"
        "\tpxor    %xmm0, %xmm3\n"
    );
}

static void gf8mul_10011010(void)
{
    __asm__ __volatile__
    (
        "\tpxor    %xmm7, %xmm6\n"
        "\tpxor    %xmm7, %xmm5\n"
        "\tpxor    %xmm7, %xmm4\n"
        "\tpxor    %xmm7, %xmm3\n"
        "\tpxor    %xmm6, %xmm5\n"
        "\tpxor    %xmm6, %xmm4\n"
        "\tpxor    %xmm6, %xmm3\n"
        "\tpxor    %xmm6, %xmm2\n"
        "\tpxor    %xmm5, %xmm4\n"
        "\tpxor    %xmm5, %xmm3\n"
        "\tpxor    %xmm5, %xmm2\n"
        "\tpxor    %xmm5, %xmm1\n"
        "\tpxor    %xmm4, %xmm3\n"
        "\tpxor    %xmm4, %xmm2\n"
        "\tpxor    %xmm4, %xmm1\n"
        "\tpxor    %xmm4, %xmm0\n"
        "\tpxor    %xmm3, %xmm6\n"
        "\tpxor    %xmm3, %xmm5\n"
        "\tpxor    %xmm3, %xmm2\n"
        "\tpxor    %xmm3, %xmm0\n"
        "\tpxor    %xmm2, %xmm7\n"
        "\tpxor    %xmm2, %xmm6\n"
        "\tpxor    %xmm2, %xmm3\n"
        "\tpxor    %xmm2, %xmm0\n"
        "\tpxor    %xmm3, %xmm2\n"
        "\tpxor    %xmm1, %xmm7\n"
        "\tpxor    %xmm1, %xmm5\n"
        "\tpxor    %xmm1, %xmm4\n"
        "\tpxor    %xmm1, %xmm0\n"
        "\tpxor    %xmm0, %xmm7\n"
        "\tpxor    %xmm0, %xmm4\n"
        "\tpxor    %xmm0, %xmm3\n"
        "\tpxor    %xmm0, %xmm1\n"
        "\tpxor    %xmm1, %xmm0\n"
    );
}

static void gf8mul_10011011(void)
{
    __asm__ __volatile__
    (
        "\tpxor    %xmm7, %xmm6\n"
        "\tpxor    %xmm7, %xmm0\n"
        "\tpxor    %xmm6, %xmm7\n"
        "\tpxor    %xmm6, %xmm4\n"
        "\tpxor    %xmm6, %xmm3\n"
        "\tpxor    %xmm5, %xmm7\n"
        "\tpxor    %xmm5, %xmm6\n"
        "\tpxor    %xmm5, %xmm3\n"
        "\tpxor    %xmm5, %xmm2\n"
        "\tpxor    %xmm5, %xmm1\n"
        "\tpxor    %xmm5, %xmm0\n"
        "\tpxor    %xmm6, %xmm5\n"
        "\tpxor    %xmm4, %xmm6\n"
        "\tpxor    %xmm4, %xmm5\n"
        "\tpxor    %xmm4, %xmm3\n"
        "\tpxor    %xmm4, %xmm0\n"
        "\tpxor    %xmm3, %xmm7\n"
        "\tpxor    %xmm3, %xmm6\n"
        "\tpxor    %xmm3, %xmm5\n"
        "\tpxor    %xmm3, %xmm4\n"
        "\tpxor    %xmm3, %xmm2\n"
        "\tpxor    %xmm2, %xmm6\n"
        "\tpxor    %xmm2, %xmm4\n"
        "\tpxor    %xmm1, %xmm7\n"
        "\tpxor    %xmm1, %xmm5\n"
        "\tpxor    %xmm1, %xmm4\n"
        "\tpxor    %xmm1, %xmm0\n"
        "\tpxor    %xmm4, %xmm1\n"
        "\tpxor    %xmm0, %xmm7\n"
        "\tpxor    %xmm0, %xmm4\n"
        "\tpxor    %xmm0, %xmm3\n"
        "\tpxor    %xmm0, %xmm1\n"
    );
}

static void gf8mul_10011100(void)
{
    __asm__ __volatile__
    (
        "\tpxor    %xmm7, %xmm4\n"
        "\tpxor    %xmm6, %xmm3\n"
        "\tpxor    %xmm5, %xmm2\n"
        "\tpxor    %xmm4, %xmm1\n"
        "\tpxor    %xmm3, %xmm0\n"
        "\tpxor    %xmm2, %xmm6\n"
        "\tpxor    %xmm2, %xmm5\n"
        "\tpxor    %xmm2, %xmm3\n"
        "\tpxor    %xmm2, %xmm1\n"
        "\tpxor    %xmm1, %xmm5\n"
        "\tpxor    %xmm1, %xmm2\n"
        "\tpxor    %xmm2, %xmm1\n"
        "\tpxor    %xmm0, %xmm7\n"
        "\tpxor    %xmm0, %xmm4\n"
        "\tpxor    %xmm0, %xmm3\n"
        "\tpxor    %xmm0, %xmm2\n"
        "\tpxor    %xmm2, %xmm0\n"
    );
}

static void gf8mul_10011101(void)
{
    __asm__ __volatile__
    (
        "\tpxor    %xmm7, %xmm6\n"
        "\tpxor    %xmm7, %xmm5\n"
        "\tpxor    %xmm7, %xmm4\n"
        "\tpxor    %xmm7, %xmm2\n"
        "\tpxor    %xmm6, %xmm5\n"
        "\tpxor    %xmm6, %xmm4\n"
        "\tpxor    %xmm6, %xmm3\n"
        "\tpxor    %xmm6, %xmm1\n"
        "\tpxor    %xmm5, %xmm4\n"
        "\tpxor    %xmm5, %xmm3\n"
        "\tpxor    %xmm5, %xmm2\n"
        "\tpxor    %xmm5, %xmm0\n"
        "\tpxor    %xmm4, %xmm7\n"
        "\tpxor    %xmm4, %xmm6\n"
        "\tpxor    %xmm4, %xmm3\n"
        "\tpxor    %xmm4, %xmm2\n"
        "\tpxor    %xmm4, %xmm1\n"
        "\tpxor    %xmm3, %xmm6\n"
        "\tpxor    %xmm3, %xmm5\n"
        "\tpxor    %xmm3, %xmm2\n"
        "\tpxor    %xmm3, %xmm1\n"
        "\tpxor    %xmm3, %xmm0\n"
        "\tpxor    %xmm2, %xmm7\n"
        "\tpxor    %xmm2, %xmm6\n"
        "\tpxor    %xmm2, %xmm5\n"
        "\tpxor    %xmm2, %xmm4\n"
        "\tpxor    %xmm2, %xmm1\n"
        "\tpxor    %xmm2, %xmm0\n"
        "\tpxor    %xmm1, %xmm7\n"
        "\tpxor    %xmm1, %xmm5\n"
        "\tpxor    %xmm1, %xmm4\n"
        "\tpxor    %xmm1, %xmm3\n"
        "\tpxor    %xmm1, %xmm0\n"
        "\tpxor    %xmm0, %xmm7\n"
        "\tpxor    %xmm0, %xmm4\n"
        "\tpxor    %xmm0, %xmm3\n"
        "\tpxor    %xmm0, %xmm2\n"
    );
}

static void gf8mul_10011110(void)
{
    __asm__ __volatile__
    (
        "\tpxor    %xmm7, %xmm6\n"
        "\tpxor    %xmm7, %xmm5\n"
        "\tpxor    %xmm7, %xmm4\n"
        "\tpxor    %xmm7, %xmm2\n"
        "\tpxor    %xmm7, %xmm1\n"
        "\tpxor    %xmm6, %xmm7\n"
        "\tpxor    %xmm6, %xmm5\n"
        "\tpxor    %xmm6, %xmm0\n"
        "\tpxor    %xmm5, %xmm7\n"
        "\tpxor    %xmm5, %xmm4\n"
        "\tpxor    %xmm5, %xmm1\n"
        "\tpxor    %xmm5, %xmm0\n"
        "\tpxor    %xmm7, %xmm5\n"
        "\tpxor    %xmm4, %xmm7\n"
        "\tpxor    %xmm4, %xmm5\n"
        "\tpxor    %xmm4, %xmm3\n"
        "\tpxor    %xmm4, %xmm1\n"
        "\tpxor    %xmm3, %xmm6\n"
        "\tpxor    %xmm3, %xmm4\n"
        "\tpxor    %xmm3, %xmm2\n"
        "\tpxor    %xmm3, %xmm0\n"
        "\tpxor    %xmm2, %xmm7\n"
        "\tpxor    %xmm2, %xmm6\n"
        "\tpxor    %xmm2, %xmm4\n"
        "\tpxor    %xmm2, %xmm3\n"
        "\tpxor    %xmm2, %xmm0\n"
        "\tpxor    %xmm1, %xmm7\n"
        "\tpxor    %xmm1, %xmm5\n"
        "\tpxor    %xmm1, %xmm4\n"
        "\tpxor    %xmm1, %xmm3\n"
        "\tpxor    %xmm1, %xmm2\n"
        "\tpxor    %xmm1, %xmm0\n"
        "\tpxor    %xmm0, %xmm7\n"
        "\tpxor    %xmm0, %xmm4\n"
        "\tpxor    %xmm0, %xmm3\n"
        "\tpxor    %xmm0, %xmm2\n"
        "\tpxor    %xmm0, %xmm1\n"
        "\tpxor    %xmm1, %xmm0\n"
    );
}

static void gf8mul_10011111(void)
{
    __asm__ __volatile__
    (
        "\tpxor    %xmm7, %xmm6\n"
        "\tpxor    %xmm7, %xmm1\n"
        "\tpxor    %xmm7, %xmm0\n"
        "\tpxor    %xmm6, %xmm7\n"
        "\tpxor    %xmm6, %xmm4\n"
        "\tpxor    %xmm6, %xmm3\n"
        "\tpxor    %xmm6, %xmm2\n"
        "\tpxor    %xmm6, %xmm1\n"
        "\tpxor    %xmm5, %xmm7\n"
        "\tpxor    %xmm5, %xmm6\n"
        "\tpxor    %xmm5, %xmm3\n"
        "\tpxor    %xmm5, %xmm2\n"
        "\tpxor    %xmm6, %xmm5\n"
        "\tpxor    %xmm4, %xmm6\n"
        "\tpxor    %xmm4, %xmm5\n"
        "\tpxor    %xmm4, %xmm2\n"
        "\tpxor    %xmm4, %xmm1\n"
        "\tpxor    %xmm5, %xmm4\n"
        "\tpxor    %xmm3, %xmm5\n"
        "\tpxor    %xmm3, %xmm4\n"
        "\tpxor    %xmm3, %xmm1\n"
        "\tpxor    %xmm3, %xmm0\n"
        "\tpxor    %xmm4, %xmm3\n"
        "\tpxor    %xmm2, %xmm6\n"
        "\tpxor    %xmm1, %xmm7\n"
        "\tpxor    %xmm1, %xmm5\n"
        "\tpxor    %xmm1, %xmm4\n"
        "\tpxor    %xmm1, %xmm3\n"
        "\tpxor    %xmm1, %xmm2\n"
        "\tpxor    %xmm1, %xmm0\n"
        "\tpxor    %xmm2, %xmm1\n"
        "\tpxor    %xmm0, %xmm7\n"
        "\tpxor    %xmm0, %xmm4\n"
        "\tpxor    %xmm0, %xmm3\n"
        "\tpxor    %xmm0, %xmm2\n"
        "\tpxor    %xmm0, %xmm1\n"
    );
}

static void gf8mul_10100000(void)
{
    __asm__ __volatile__
    (
        "\tpxor    %xmm7, %xmm5\n"
        "\tpxor    %xmm7, %xmm2\n"
        "\tpxor    %xmm7, %xmm1\n"
        "\tpxor    %xmm6, %xmm4\n"
        "\tpxor    %xmm6, %xmm1\n"
        "\tpxor    %xmm6, %xmm0\n"
        "\tpxor    %xmm5, %xmm6\n"
        "\tpxor    %xmm5, %xmm4\n"
        "\tpxor    %xmm5, %xmm2\n"
        "\tpxor    %xmm5, %xmm0\n"
        "\tpxor    %xmm4, %xmm7\n"
        "\tpxor    %xmm4, %xmm5\n"
        "\tpxor    %xmm4, %xmm3\n"
        "\tpxor    %xmm4, %xmm2\n"
        "\tpxor    %xmm4, %xmm0\n"
        "\tpxor    %xmm5, %xmm4\n"
        "\tpxor    %xmm3, %xmm6\n"
        "\tpxor    %xmm3, %xmm5\n"
        "\tpxor    %xmm2, %xmm7\n"
        "\tpxor    %xmm2, %xmm6\n"
        "\tpxor    %xmm2, %xmm5\n"
        "\tpxor    %xmm2, %xmm1\n"
        "\tpxor    %xmm1, %xmm7\n"
        "\tpxor    %xmm1, %xmm6\n"
        "\tpxor    %xmm1, %xmm5\n"
        "\tpxor    %xmm1, %xmm4\n"
        "\tpxor    %xmm1, %xmm3\n"
        "\tpxor    %xmm1, %xmm2\n"
        "\tpxor    %xmm1, %xmm0\n"
        "\tpxor    %xmm2, %xmm1\n"
        "\tpxor    %xmm0, %xmm7\n"
        "\tpxor    %xmm0, %xmm5\n"
        "\tpxor    %xmm5, %xmm0\n"
    );
}

static void gf8mul_10100001(void)
{
    __asm__ __volatile__
    (
        "\tpxor    %xmm7, %xmm6\n"
        "\tpxor    %xmm7, %xmm4\n"
        "\tpxor    %xmm7, %xmm1\n"
        "\tpxor    %xmm6, %xmm7\n"
        "\tpxor    %xmm6, %xmm5\n"
        "\tpxor    %xmm6, %xmm3\n"
        "\tpxor    %xmm6, %xmm1\n"
        "\tpxor    %xmm7, %xmm6\n"
        "\tpxor    %xmm5, %xmm6\n"
        "\tpxor    %xmm5, %xmm4\n"
        "\tpxor    %xmm5, %xmm2\n"
        "\tpxor    %xmm5, %xmm0\n"
        "\tpxor    %xmm6, %xmm5\n"
        "\tpxor    %xmm4, %xmm7\n"
        "\tpxor    %xmm4, %xmm6\n"
        "\tpxor    %xmm4, %xmm5\n"
        "\tpxor    %xmm4, %xmm3\n"
        "\tpxor    %xmm4, %xmm1\n"
        "\tpxor    %xmm3, %xmm7\n"
        "\tpxor    %xmm3, %xmm2\n"
        "\tpxor    %xmm3, %xmm1\n"
        "\tpxor    %xmm7, %xmm3\n"
        "\tpxor    %xmm2, %xmm6\n"
        "\tpxor    %xmm2, %xmm1\n"
        "\tpxor    %xmm2, %xmm0\n"
        "\tpxor    %xmm6, %xmm2\n"
        "\tpxor    %xmm1, %xmm7\n"
        "\tpxor    %xmm1, %xmm6\n"
        "\tpxor    %xmm1, %xmm5\n"
        "\tpxor    %xmm1, %xmm4\n"
        "\tpxor    %xmm1, %xmm3\n"
        "\tpxor    %xmm1, %xmm2\n"
        "\tpxor    %xmm1, %xmm0\n"
        "\tpxor    %xmm0, %xmm7\n"
        "\tpxor    %xmm0, %xmm5\n"
    );
}

static void gf8mul_10100010(void)
{
    __asm__ __volatile__
    (
        "\tpxor    %xmm7, %xmm5\n"
        "\tpxor    %xmm7, %xmm3\n"
        "\tpxor    %xmm7, %xmm2\n"
        "\tpxor    %xmm7, %xmm1\n"
        "\tpxor    %xmm6, %xmm4\n"
        "\tpxor    %xmm6, %xmm2\n"
        "\tpxor    %xmm6, %xmm1\n"
        "\tpxor    %xmm6, %xmm0\n"
        "\tpxor    %xmm5, %xmm7\n"
        "\tpxor    %xmm5, %xmm3\n"
        "\tpxor    %xmm7, %xmm5\n"
        "\tpxor    %xmm4, %xmm6\n"
        "\tpxor    %xmm4, %xmm2\n"
        "\tpxor    %xmm6, %xmm4\n"
        "\tpxor    %xmm3, %xmm5\n"
        "\tpxor    %xmm3, %xmm1\n"
        "\tpxor    %xmm5, %xmm3\n"
        "\tpxor    %xmm2, %xmm4\n"
        "\tpxor    %xmm2, %xmm0\n"
        "\tpxor    %xmm4, %xmm2\n"
        "\tpxor    %xmm1, %xmm7\n"
        "\tpxor    %xmm1, %xmm6\n"
        "\tpxor    %xmm1, %xmm5\n"
        "\tpxor    %xmm1, %xmm4\n"
        "\tpxor    %xmm1, %xmm3\n"
        "\tpxor    %xmm1, %xmm0\n"
        "\tpxor    %xmm0, %xmm7\n"
        "\tpxor    %xmm0, %xmm5\n"
        "\tpxor    %xmm0, %xmm1\n"
        "\tpxor    %xmm1, %xmm0\n"
    );
}

static void gf8mul_10100011(void)
{
    __asm__ __volatile__
    (
        "\tpxor    %xmm7, %xmm5\n"
        "\tpxor    %xmm7, %xmm4\n"
        "\tpxor    %xmm7, %xmm1\n"
        "\tpxor    %xmm7, %xmm0\n"
        "\tpxor    %xmm6, %xmm7\n"
        "\tpxor    %xmm6, %xmm5\n"
        "\tpxor    %xmm6, %xmm2\n"
        "\tpxor    %xmm5, %xmm6\n"
        "\tpxor    %xmm5, %xmm4\n"
        "\tpxor    %xmm5, %xmm1\n"
        "\tpxor    %xmm4, %xmm5\n"
        "\tpxor    %xmm4, %xmm3\n"
        "\tpxor    %xmm4, %xmm0\n"
        "\tpxor    %xmm3, %xmm5\n"
        "\tpxor    %xmm3, %xmm1\n"
        "\tpxor    %xmm2, %xmm6\n"
        "\tpxor    %xmm2, %xmm3\n"
        "\tpxor    %xmm2, %xmm1\n"
        "\tpxor    %xmm2, %xmm0\n"
        "\tpxor    %xmm1, %xmm7\n"
        "\tpxor    %xmm1, %xmm6\n"
        "\tpxor    %xmm1, %xmm5\n"
        "\tpxor    %xmm1, %xmm4\n"
        "\tpxor    %xmm1, %xmm3\n"
        "\tpxor    %xmm1, %xmm0\n"
        "\tpxor    %xmm3, %xmm1\n"
        "\tpxor    %xmm0, %xmm7\n"
        "\tpxor    %xmm0, %xmm5\n"
        "\tpxor    %xmm0, %xmm1\n"
    );
}

static void gf8mul_10100100(void)
{
    __asm__ __volatile__
    (
        "\tpxor    %xmm7, %xmm4\n"
        "\tpxor    %xmm7, %xmm3\n"
        "\tpxor    %xmm6, %xmm3\n"
        "\tpxor    %xmm6, %xmm2\n"
        "\tpxor    %xmm5, %xmm2\n"
        "\tpxor    %xmm5, %xmm1\n"
        "\tpxor    %xmm4, %xmm1\n"
        "\tpxor    %xmm4, %xmm0\n"
        "\tpxor    %xmm3, %xmm7\n"
        "\tpxor    %xmm3, %xmm4\n"
        "\tpxor    %xmm3, %xmm2\n"
        "\tpxor    %xmm2, %xmm6\n"
        "\tpxor    %xmm2, %xmm3\n"
        "\tpxor    %xmm2, %xmm1\n"
        "\tpxor    %xmm1, %xmm5\n"
        "\tpxor    %xmm1, %xmm2\n"
        "\tpxor    %xmm1, %xmm0\n"
        "\tpxor    %xmm0, %xmm7\n"
        "\tpxor    %xmm0, %xmm6\n"
        "\tpxor    %xmm0, %xmm5\n"
        "\tpxor    %xmm0, %xmm3\n"
        "\tpxor    %xmm7, %xmm3\n"
        "\tpxor    %xmm6, %xmm2\n"
        "\tpxor    %xmm5, %xmm1\n"
        "\tpxor    %xmm4, %xmm0\n"
        "\tpxor    %xmm3, %xmm7\n"
        "\tpxor    %xmm3, %xmm2\n"
        "\tpxor    %xmm3, %xmm1\n"
        "\tpxor    %xmm2, %xmm6\n"
        "\tpxor    %xmm2, %xmm1\n"
        "\tpxor    %xmm2, %xmm0\n"
        "\tpxor    %xmm1, %xmm7\n"
        "\tpxor    %xmm1, %xmm6\n"
        "\tpxor    %xmm1, %xmm5\n"
        "\tpxor    %xmm1, %xmm2\n"
        "\tpxor    %xmm0, %xmm6\n"
        "\tpxor    %xmm0, %xmm5\n"
        "\tpxor    %xmm0, %xmm4\n"
        "\tpxor    %xmm0, %xmm1\n"
    );
}

static void gf8mul_10100101(void)
{
    __asm__ __volatile__
    (
        "\tpxor    %xmm7, %xmm3\n"
        "\tpxor    %xmm7, %xmm0\n"
        "\tpxor    %xmm6, %xmm7\n"
        "\tpxor    %xmm6, %xmm1\n"
        "\tpxor    %xmm6, %xmm0\n"
        "\tpxor    %xmm5, %xmm7\n"
        "\tpxor    %xmm5, %xmm6\n"
        "\tpxor    %xmm5, %xmm2\n"
        "\tpxor    %xmm5, %xmm1\n"
        "\tpxor    %xmm4, %xmm6\n"
        "\tpxor    %xmm4, %xmm5\n"
        "\tpxor    %xmm4, %xmm1\n"
        "\tpxor    %xmm4, %xmm0\n"
        "\tpxor    %xmm3, %xmm7\n"
        "\tpxor    %xmm3, %xmm5\n"
        "\tpxor    %xmm3, %xmm4\n"
        "\tpxor    %xmm3, %xmm2\n"
        "\tpxor    %xmm3, %xmm1\n"
        "\tpxor    %xmm2, %xmm6\n"
        "\tpxor    %xmm2, %xmm4\n"
        "\tpxor    %xmm2, %xmm3\n"
        "\tpxor    %xmm2, %xmm1\n"
        "\tpxor    %xmm2, %xmm0\n"
        "\tpxor    %xmm1, %xmm7\n"
        "\tpxor    %xmm1, %xmm6\n"
        "\tpxor    %xmm1, %xmm5\n"
        "\tpxor    %xmm1, %xmm4\n"
        "\tpxor    %xmm1, %xmm0\n"
        "\tpxor    %xmm0, %xmm7\n"
        "\tpxor    %xmm0, %xmm5\n"
        "\tpxor    %xmm0, %xmm2\n"
    );
}

static void gf8mul_10100110(void)
{
    __asm__ __volatile__
    (
        "\tpxor    %xmm7, %xmm5\n"
        "\tpxor    %xmm6, %xmm4\n"
        "\tpxor    %xmm5, %xmm3\n"
        "\tpxor    %xmm4, %xmm2\n"
        "\tpxor    %xmm3, %xmm1\n"
        "\tpxor    %xmm2, %xmm0\n"
        "\tpxor    %xmm1, %xmm7\n"
        "\tpxor    %xmm1, %xmm6\n"
        "\tpxor    %xmm1, %xmm5\n"
        "\tpxor    %xmm1, %xmm4\n"
        "\tpxor    %xmm1, %xmm2\n"
        "\tpxor    %xmm1, %xmm0\n"
        "\tpxor    %xmm0, %xmm7\n"
        "\tpxor    %xmm0, %xmm5\n"
        "\tpxor    %xmm0, %xmm2\n"
        "\tpxor    %xmm0, %xmm1\n"
        "\tpxor    %xmm1, %xmm0\n"
    );
}

static void gf8mul_10100111(void)
{
    __asm__ __volatile__
    (
        "\tpxor    %xmm7, %xmm5\n"
        "\tpxor    %xmm7, %xmm4\n"
        "\tpxor    %xmm7, %xmm3\n"
        "\tpxor    %xmm7, %xmm1\n"
        "\tpxor    %xmm6, %xmm4\n"
        "\tpxor    %xmm6, %xmm3\n"
        "\tpxor    %xmm6, %xmm2\n"
        "\tpxor    %xmm6, %xmm0\n"
        "\tpxor    %xmm5, %xmm7\n"
        "\tpxor    %xmm5, %xmm6\n"
        "\tpxor    %xmm5, %xmm4\n"
        "\tpxor    %xmm5, %xmm3\n"
        "\tpxor    %xmm5, %xmm2\n"
        "\tpxor    %xmm5, %xmm1\n"
        "\tpxor    %xmm5, %xmm0\n"
        "\tpxor    %xmm4, %xmm7\n"
        "\tpxor    %xmm4, %xmm5\n"
        "\tpxor    %xmm4, %xmm3\n"
        "\tpxor    %xmm4, %xmm1\n"
        "\tpxor    %xmm5, %xmm4\n"
        "\tpxor    %xmm3, %xmm6\n"
        "\tpxor    %xmm3, %xmm4\n"
        "\tpxor    %xmm3, %xmm2\n"
        "\tpxor    %xmm3, %xmm0\n"
        "\tpxor    %xmm4, %xmm3\n"
        "\tpxor    %xmm2, %xmm7\n"
        "\tpxor    %xmm2, %xmm5\n"
        "\tpxor    %xmm1, %xmm7\n"
        "\tpxor    %xmm1, %xmm6\n"
        "\tpxor    %xmm1, %xmm5\n"
        "\tpxor    %xmm1, %xmm4\n"
        "\tpxor    %xmm1, %xmm2\n"
        "\tpxor    %xmm1, %xmm0\n"
        "\tpxor    %xmm2, %xmm1\n"
        "\tpxor    %xmm0, %xmm7\n"
        "\tpxor    %xmm0, %xmm5\n"
        "\tpxor    %xmm0, %xmm2\n"
        "\tpxor    %xmm0, %xmm1\n"
    );
}

static void gf8mul_10101000(void)
{
    __asm__ __volatile__
    (
        "\tpxor    %xmm7, %xmm6\n"
        "\tpxor    %xmm7, %xmm2\n"
        "\tpxor    %xmm7, %xmm1\n"
        "\tpxor    %xmm7, %xmm0\n"
        "\tpxor    %xmm6, %xmm5\n"
        "\tpxor    %xmm6, %xmm3\n"
        "\tpxor    %xmm6, %xmm0\n"
        "\tpxor    %xmm5, %xmm7\n"
        "\tpxor    %xmm5, %xmm6\n"
        "\tpxor    %xmm5, %xmm3\n"
        "\tpxor    %xmm5, %xmm1\n"
        "\tpxor    %xmm6, %xmm5\n"
        "\tpxor    %xmm4, %xmm6\n"
        "\tpxor    %xmm4, %xmm5\n"
        "\tpxor    %xmm4, %xmm2\n"
        "\tpxor    %xmm4, %xmm0\n"
        "\tpxor    %xmm5, %xmm4\n"
        "\tpxor    %xmm3, %xmm5\n"
        "\tpxor    %xmm2, %xmm6\n"
        "\tpxor    %xmm2, %xmm5\n"
        "\tpxor    %xmm2, %xmm4\n"
        "\tpxor    %xmm2, %xmm3\n"
        "\tpxor    %xmm2, %xmm1\n"
        "\tpxor    %xmm2, %xmm0\n"
        "\tpxor    %xmm1, %xmm6\n"
        "\tpxor    %xmm1, %xmm3\n"
        "\tpxor    %xmm1, %xmm2\n"
        "\tpxor    %xmm2, %xmm1\n"
        "\tpxor    %xmm0, %xmm7\n"
        "\tpxor    %xmm0, %xmm5\n"
        "\tpxor    %xmm0, %xmm3\n"
        "\tpxor    %xmm3, %xmm0\n"
    );
}

static void gf8mul_10101001(void)
{
    __asm__ __volatile__
    (
        "\tpxor    %xmm7, %xmm6\n"
        "\tpxor    %xmm6, %xmm5\n"
        "\tpxor    %xmm5, %xmm4\n"
        "\tpxor    %xmm4, %xmm3\n"
        "\tpxor    %xmm3, %xmm2\n"
        "\tpxor    %xmm2, %xmm1\n"
        "\tpxor    %xmm1, %xmm0\n"
        "\tpxor    %xmm0, %xmm7\n"
        "\tpxor    %xmm0, %xmm6\n"
        "\tpxor    %xmm0, %xmm5\n"
        "\tpxor    %xmm0, %xmm4\n"
        "\tpxor    %xmm0, %xmm2\n"
        "\tpxor    %xmm7, %xmm3\n"
        "\tpxor    %xmm6, %xmm2\n"
        "\tpxor    %xmm5, %xmm1\n"
        "\tpxor    %xmm4, %xmm0\n"
        "\tpxor    %xmm3, %xmm7\n"
        "\tpxor    %xmm3, %xmm2\n"
        "\tpxor    %xmm3, %xmm1\n"
        "\tpxor    %xmm2, %xmm6\n"
        "\tpxor    %xmm2, %xmm1\n"
        "\tpxor    %xmm2, %xmm0\n"
        "\tpxor    %xmm1, %xmm7\n"
        "\tpxor    %xmm1, %xmm6\n"
        "\tpxor    %xmm1, %xmm5\n"
        "\tpxor    %xmm1, %xmm2\n"
        "\tpxor    %xmm0, %xmm6\n"
        "\tpxor    %xmm0, %xmm5\n"
        "\tpxor    %xmm0, %xmm4\n"
        "\tpxor    %xmm0, %xmm1\n"
    );
}

static void gf8mul_10101010(void)
{
    __asm__ __volatile__
    (
        "\tpxor    %xmm7, %xmm6\n"
        "\tpxor    %xmm7, %xmm3\n"
        "\tpxor    %xmm7, %xmm2\n"
        "\tpxor    %xmm7, %xmm1\n"
        "\tpxor    %xmm6, %xmm5\n"
        "\tpxor    %xmm6, %xmm2\n"
        "\tpxor    %xmm6, %xmm1\n"
        "\tpxor    %xmm6, %xmm0\n"
        "\tpxor    %xmm5, %xmm4\n"
        "\tpxor    %xmm5, %xmm0\n"
        "\tpxor    %xmm4, %xmm7\n"
        "\tpxor    %xmm4, %xmm6\n"
        "\tpxor    %xmm4, %xmm5\n"
        "\tpxor    %xmm4, %xmm2\n"
        "\tpxor    %xmm5, %xmm4\n"
        "\tpxor    %xmm3, %xmm6\n"
        "\tpxor    %xmm3, %xmm5\n"
        "\tpxor    %xmm3, %xmm4\n"
        "\tpxor    %xmm3, %xmm1\n"
        "\tpxor    %xmm4, %xmm3\n"
        "\tpxor    %xmm2, %xmm5\n"
        "\tpxor    %xmm2, %xmm4\n"
        "\tpxor    %xmm2, %xmm3\n"
        "\tpxor    %xmm2, %xmm0\n"
        "\tpxor    %xmm3, %xmm2\n"
        "\tpxor    %xmm1, %xmm7\n"
        "\tpxor    %xmm1, %xmm6\n"
        "\tpxor    %xmm1, %xmm5\n"
        "\tpxor    %xmm1, %xmm0\n"
        "\tpxor    %xmm0, %xmm7\n"
        "\tpxor    %xmm0, %xmm5\n"
        "\tpxor    %xmm0, %xmm3\n"
        "\tpxor    %xmm0, %xmm1\n"
        "\tpxor    %xmm1, %xmm0\n"
    );
}

static void gf8mul_10101011(void)
{
    __asm__ __volatile__
    (
        "\tpxor    %xmm7, %xmm6\n"
        "\tpxor    %xmm7, %xmm4\n"
        "\tpxor    %xmm6, %xmm7\n"
        "\tpxor    %xmm6, %xmm5\n"
        "\tpxor    %xmm6, %xmm3\n"
        "\tpxor    %xmm6, %xmm1\n"
        "\tpxor    %xmm6, %xmm0\n"
        "\tpxor    %xmm7, %xmm6\n"
        "\tpxor    %xmm5, %xmm7\n"
        "\tpxor    %xmm5, %xmm2\n"
        "\tpxor    %xmm5, %xmm1\n"
        "\tpxor    %xmm5, %xmm0\n"
        "\tpxor    %xmm4, %xmm6\n"
        "\tpxor    %xmm4, %xmm5\n"
        "\tpxor    %xmm5, %xmm4\n"
        "\tpxor    %xmm3, %xmm6\n"
        "\tpxor    %xmm3, %xmm5\n"
        "\tpxor    %xmm3, %xmm4\n"
        "\tpxor    %xmm3, %xmm1\n"
        "\tpxor    %xmm2, %xmm6\n"
        "\tpxor    %xmm2, %xmm5\n"
        "\tpxor    %xmm2, %xmm4\n"
        "\tpxor    %xmm2, %xmm1\n"
        "\tpxor    %xmm2, %xmm0\n"
        "\tpxor    %xmm1, %xmm7\n"
        "\tpxor    %xmm1, %xmm6\n"
        "\tpxor    %xmm1, %xmm5\n"
        "\tpxor    %xmm1, %xmm0\n"
        "\tpxor    %xmm5, %xmm1\n"
        "\tpxor    %xmm0, %xmm7\n"
        "\tpxor    %xmm0, %xmm5\n"
        "\tpxor    %xmm0, %xmm3\n"
        "\tpxor    %xmm0, %xmm1\n"
    );
}

static void gf8mul_10101100(void)
{
    __asm__ __volatile__
    (
        "\tpxor    %xmm7, %xmm5\n"
        "\tpxor    %xmm7, %xmm4\n"
        "\tpxor    %xmm7, %xmm2\n"
        "\tpxor    %xmm7, %xmm0\n"
        "\tpxor    %xmm6, %xmm7\n"
        "\tpxor    %xmm6, %xmm5\n"
        "\tpxor    %xmm6, %xmm0\n"
        "\tpxor    %xmm5, %xmm6\n"
        "\tpxor    %xmm5, %xmm3\n"
        "\tpxor    %xmm5, %xmm1\n"
        "\tpxor    %xmm4, %xmm6\n"
        "\tpxor    %xmm4, %xmm5\n"
        "\tpxor    %xmm4, %xmm3\n"
        "\tpxor    %xmm4, %xmm2\n"
        "\tpxor    %xmm4, %xmm1\n"
        "\tpxor    %xmm4, %xmm0\n"
        "\tpxor    %xmm3, %xmm7\n"
        "\tpxor    %xmm3, %xmm6\n"
        "\tpxor    %xmm3, %xmm5\n"
        "\tpxor    %xmm3, %xmm1\n"
        "\tpxor    %xmm3, %xmm0\n"
        "\tpxor    %xmm5, %xmm3\n"
        "\tpxor    %xmm2, %xmm7\n"
        "\tpxor    %xmm2, %xmm6\n"
        "\tpxor    %xmm2, %xmm3\n"
        "\tpxor    %xmm2, %xmm1\n"
        "\tpxor    %xmm1, %xmm6\n"
        "\tpxor    %xmm1, %xmm2\n"
        "\tpxor    %xmm2, %xmm1\n"
        "\tpxor    %xmm0, %xmm7\n"
        "\tpxor    %xmm0, %xmm5\n"
        "\tpxor    %xmm0, %xmm3\n"
        "\tpxor    %xmm0, %xmm2\n"
        "\tpxor    %xmm2, %xmm0\n"
    );
}

static void gf8mul_10101101(void)
{
    __asm__ __volatile__
    (
        "\tpxor    %xmm7, %xmm6\n"
        "\tpxor    %xmm7, %xmm4\n"
        "\tpxor    %xmm7, %xmm1\n"
        "\tpxor    %xmm7, %xmm0\n"
        "\tpxor    %xmm6, %xmm7\n"
        "\tpxor    %xmm6, %xmm5\n"
        "\tpxor    %xmm6, %xmm4\n"
        "\tpxor    %xmm6, %xmm2\n"
        "\tpxor    %xmm7, %xmm6\n"
        "\tpxor    %xmm5, %xmm6\n"
        "\tpxor    %xmm5, %xmm4\n"
        "\tpxor    %xmm5, %xmm3\n"
        "\tpxor    %xmm5, %xmm1\n"
        "\tpxor    %xmm6, %xmm5\n"
        "\tpxor    %xmm4, %xmm5\n"
        "\tpxor    %xmm4, %xmm3\n"
        "\tpxor    %xmm4, %xmm2\n"
        "\tpxor    %xmm4, %xmm0\n"
        "\tpxor    %xmm5, %xmm4\n"
        "\tpxor    %xmm3, %xmm7\n"
        "\tpxor    %xmm3, %xmm6\n"
        "\tpxor    %xmm3, %xmm2\n"
        "\tpxor    %xmm3, %xmm1\n"
        "\tpxor    %xmm2, %xmm6\n"
        "\tpxor    %xmm2, %xmm5\n"
        "\tpxor    %xmm2, %xmm1\n"
        "\tpxor    %xmm2, %xmm0\n"
        "\tpxor    %xmm1, %xmm7\n"
        "\tpxor    %xmm1, %xmm6\n"
        "\tpxor    %xmm1, %xmm5\n"
        "\tpxor    %xmm1, %xmm3\n"
        "\tpxor    %xmm1, %xmm0\n"
        "\tpxor    %xmm0, %xmm7\n"
        "\tpxor    %xmm0, %xmm5\n"
        "\tpxor    %xmm0, %xmm3\n"
        "\tpxor    %xmm0, %xmm2\n"
    );
}

static void gf8mul_10101110(void)
{
    __asm__ __volatile__
    (
        "\tpxor    %xmm7, %xmm6\n"
        "\tpxor    %xmm7, %xmm4\n"
        "\tpxor    %xmm7, %xmm3\n"
        "\tpxor    %xmm7, %xmm0\n"
        "\tpxor    %xmm6, %xmm7\n"
        "\tpxor    %xmm6, %xmm5\n"
        "\tpxor    %xmm6, %xmm2\n"
        "\tpxor    %xmm6, %xmm0\n"
        "\tpxor    %xmm7, %xmm6\n"
        "\tpxor    %xmm5, %xmm7\n"
        "\tpxor    %xmm5, %xmm4\n"
        "\tpxor    %xmm5, %xmm3\n"
        "\tpxor    %xmm5, %xmm1\n"
        "\tpxor    %xmm5, %xmm0\n"
        "\tpxor    %xmm4, %xmm7\n"
        "\tpxor    %xmm4, %xmm5\n"
        "\tpxor    %xmm4, %xmm2\n"
        "\tpxor    %xmm3, %xmm6\n"
        "\tpxor    %xmm3, %xmm4\n"
        "\tpxor    %xmm3, %xmm1\n"
        "\tpxor    %xmm2, %xmm5\n"
        "\tpxor    %xmm2, %xmm3\n"
        "\tpxor    %xmm2, %xmm0\n"
        "\tpxor    %xmm1, %xmm7\n"
        "\tpxor    %xmm1, %xmm6\n"
        "\tpxor    %xmm1, %xmm5\n"
        "\tpxor    %xmm1, %xmm3\n"
        "\tpxor    %xmm1, %xmm2\n"
        "\tpxor    %xmm1, %xmm0\n"
        "\tpxor    %xmm0, %xmm7\n"
        "\tpxor    %xmm0, %xmm5\n"
        "\tpxor    %xmm0, %xmm3\n"
        "\tpxor    %xmm0, %xmm2\n"
        "\tpxor    %xmm0, %xmm1\n"
        "\tpxor    %xmm1, %xmm0\n"
    );
}

static void gf8mul_10101111(void)
{
    __asm__ __volatile__
    (
        "\tpxor    %xmm7, %xmm4\n"
        "\tpxor    %xmm7, %xmm0\n"
        "\tpxor    %xmm6, %xmm7\n"
        "\tpxor    %xmm6, %xmm4\n"
        "\tpxor    %xmm6, %xmm2\n"
        "\tpxor    %xmm6, %xmm1\n"
        "\tpxor    %xmm6, %xmm0\n"
        "\tpxor    %xmm5, %xmm7\n"
        "\tpxor    %xmm5, %xmm6\n"
        "\tpxor    %xmm5, %xmm4\n"
        "\tpxor    %xmm5, %xmm2\n"
        "\tpxor    %xmm4, %xmm6\n"
        "\tpxor    %xmm4, %xmm5\n"
        "\tpxor    %xmm4, %xmm3\n"
        "\tpxor    %xmm4, %xmm1\n"
        "\tpxor    %xmm3, %xmm5\n"
        "\tpxor    %xmm3, %xmm4\n"
        "\tpxor    %xmm3, %xmm2\n"
        "\tpxor    %xmm3, %xmm0\n"
        "\tpxor    %xmm2, %xmm7\n"
        "\tpxor    %xmm1, %xmm7\n"
        "\tpxor    %xmm1, %xmm6\n"
        "\tpxor    %xmm1, %xmm5\n"
        "\tpxor    %xmm1, %xmm3\n"
        "\tpxor    %xmm1, %xmm2\n"
        "\tpxor    %xmm1, %xmm0\n"
        "\tpxor    %xmm2, %xmm1\n"
        "\tpxor    %xmm0, %xmm7\n"
        "\tpxor    %xmm0, %xmm5\n"
        "\tpxor    %xmm0, %xmm3\n"
        "\tpxor    %xmm0, %xmm2\n"
        "\tpxor    %xmm0, %xmm1\n"
    );
}

static void gf8mul_10110000(void)
{
    __asm__ __volatile__
    (
        "\tpxor    %xmm7, %xmm5\n"
        "\tpxor    %xmm7, %xmm4\n"
        "\tpxor    %xmm7, %xmm2\n"
        "\tpxor    %xmm6, %xmm4\n"
        "\tpxor    %xmm6, %xmm3\n"
        "\tpxor    %xmm6, %xmm1\n"
        "\tpxor    %xmm5, %xmm3\n"
        "\tpxor    %xmm5, %xmm2\n"
        "\tpxor    %xmm5, %xmm0\n"
        "\tpxor    %xmm4, %xmm3\n"
        "\tpxor    %xmm4, %xmm2\n"
        "\tpxor    %xmm4, %xmm1\n"
        "\tpxor    %xmm4, %xmm0\n"
        "\tpxor    %xmm3, %xmm6\n"
        "\tpxor    %xmm3, %xmm4\n"
        "\tpxor    %xmm3, %xmm0\n"
        "\tpxor    %xmm2, %xmm5\n"
        "\tpxor    %xmm2, %xmm4\n"
        "\tpxor    %xmm2, %xmm1\n"
        "\tpxor    %xmm2, %xmm0\n"
        "\tpxor    %xmm1, %xmm6\n"
        "\tpxor    %xmm1, %xmm5\n"
        "\tpxor    %xmm1, %xmm4\n"
        "\tpxor    %xmm1, %xmm3\n"
        "\tpxor    %xmm1, %xmm2\n"
        "\tpxor    %xmm2, %xmm1\n"
        "\tpxor    %xmm0, %xmm7\n"
        "\tpxor    %xmm0, %xmm5\n"
        "\tpxor    %xmm0, %xmm4\n"
        "\tpxor    %xmm4, %xmm0\n"
    );
}

static void gf8mul_10110001(void)
{
    __asm__ __volatile__
    (
        "\tpxor    %xmm7, %xmm6\n"
        "\tpxor    %xmm7, %xmm5\n"
        "\tpxor    %xmm7, %xmm1\n"
        "\tpxor    %xmm7, %xmm0\n"
        "\tpxor    %xmm6, %xmm7\n"
        "\tpxor    %xmm6, %xmm4\n"
        "\tpxor    %xmm6, %xmm3\n"
        "\tpxor    %xmm6, %xmm0\n"
        "\tpxor    %xmm5, %xmm6\n"
        "\tpxor    %xmm5, %xmm4\n"
        "\tpxor    %xmm4, %xmm7\n"
        "\tpxor    %xmm4, %xmm6\n"
        "\tpxor    %xmm4, %xmm3\n"
        "\tpxor    %xmm4, %xmm2\n"
        "\tpxor    %xmm6, %xmm4\n"
        "\tpxor    %xmm3, %xmm6\n"
        "\tpxor    %xmm3, %xmm5\n"
        "\tpxor    %xmm3, %xmm2\n"
        "\tpxor    %xmm3, %xmm1\n"
        "\tpxor    %xmm5, %xmm3\n"
        "\tpxor    %xmm2, %xmm5\n"
        "\tpxor    %xmm2, %xmm4\n"
        "\tpxor    %xmm2, %xmm1\n"
        "\tpxor    %xmm2, %xmm0\n"
        "\tpxor    %xmm4, %xmm2\n"
        "\tpxor    %xmm1, %xmm7\n"
        "\tpxor    %xmm1, %xmm6\n"
        "\tpxor    %xmm1, %xmm3\n"
        "\tpxor    %xmm1, %xmm2\n"
        "\tpxor    %xmm1, %xmm0\n"
        "\tpxor    %xmm0, %xmm7\n"
        "\tpxor    %xmm0, %xmm5\n"
        "\tpxor    %xmm0, %xmm4\n"
    );
}

static void gf8mul_10110010(void)
{
    __asm__ __volatile__
    (
        "\tpxor    %xmm7, %xmm6\n"
        "\tpxor    %xmm7, %xmm5\n"
        "\tpxor    %xmm7, %xmm1\n"
        "\tpxor    %xmm7, %xmm0\n"
        "\tpxor    %xmm6, %xmm7\n"
        "\tpxor    %xmm6, %xmm5\n"
        "\tpxor    %xmm6, %xmm4\n"
        "\tpxor    %xmm6, %xmm3\n"
        "\tpxor    %xmm6, %xmm0\n"
        "\tpxor    %xmm5, %xmm7\n"
        "\tpxor    %xmm5, %xmm3\n"
        "\tpxor    %xmm5, %xmm0\n"
        "\tpxor    %xmm7, %xmm5\n"
        "\tpxor    %xmm4, %xmm6\n"
        "\tpxor    %xmm4, %xmm5\n"
        "\tpxor    %xmm4, %xmm2\n"
        "\tpxor    %xmm4, %xmm1\n"
        "\tpxor    %xmm5, %xmm4\n"
        "\tpxor    %xmm3, %xmm7\n"
        "\tpxor    %xmm3, %xmm1\n"
        "\tpxor    %xmm7, %xmm3\n"
        "\tpxor    %xmm2, %xmm6\n"
        "\tpxor    %xmm2, %xmm0\n"
        "\tpxor    %xmm6, %xmm2\n"
        "\tpxor    %xmm1, %xmm7\n"
        "\tpxor    %xmm1, %xmm6\n"
        "\tpxor    %xmm1, %xmm3\n"
        "\tpxor    %xmm1, %xmm0\n"
        "\tpxor    %xmm0, %xmm7\n"
        "\tpxor    %xmm0, %xmm5\n"
        "\tpxor    %xmm0, %xmm4\n"
        "\tpxor    %xmm0, %xmm1\n"
        "\tpxor    %xmm1, %xmm0\n"
    );
}

static void gf8mul_10110011(void)
{
    __asm__ __volatile__
    (
        "\tpxor    %xmm7, %xmm5\n"
        "\tpxor    %xmm7, %xmm1\n"
        "\tpxor    %xmm7, %xmm0\n"
        "\tpxor    %xmm6, %xmm7\n"
        "\tpxor    %xmm6, %xmm5\n"
        "\tpxor    %xmm6, %xmm4\n"
        "\tpxor    %xmm6, %xmm3\n"
        "\tpxor    %xmm6, %xmm2\n"
        "\tpxor    %xmm5, %xmm6\n"
        "\tpxor    %xmm5, %xmm4\n"
        "\tpxor    %xmm5, %xmm3\n"
        "\tpxor    %xmm5, %xmm2\n"
        "\tpxor    %xmm5, %xmm1\n"
        "\tpxor    %xmm4, %xmm5\n"
        "\tpxor    %xmm4, %xmm3\n"
        "\tpxor    %xmm4, %xmm2\n"
        "\tpxor    %xmm4, %xmm1\n"
        "\tpxor    %xmm4, %xmm0\n"
        "\tpxor    %xmm3, %xmm7\n"
        "\tpxor    %xmm3, %xmm1\n"
        "\tpxor    %xmm2, %xmm5\n"
        "\tpxor    %xmm2, %xmm4\n"
        "\tpxor    %xmm2, %xmm3\n"
        "\tpxor    %xmm2, %xmm1\n"
        "\tpxor    %xmm2, %xmm0\n"
        "\tpxor    %xmm1, %xmm7\n"
        "\tpxor    %xmm1, %xmm6\n"
        "\tpxor    %xmm1, %xmm3\n"
        "\tpxor    %xmm1, %xmm0\n"
        "\tpxor    %xmm3, %xmm1\n"
        "\tpxor    %xmm0, %xmm7\n"
        "\tpxor    %xmm0, %xmm5\n"
        "\tpxor    %xmm0, %xmm4\n"
        "\tpxor    %xmm0, %xmm1\n"
    );
}

static void gf8mul_10110100(void)
{
    __asm__ __volatile__
    (
        "\tpxor    %xmm7, %xmm5\n"
        "\tpxor    %xmm7, %xmm4\n"
        "\tpxor    %xmm6, %xmm4\n"
        "\tpxor    %xmm6, %xmm3\n"
        "\tpxor    %xmm5, %xmm3\n"
        "\tpxor    %xmm5, %xmm2\n"
        "\tpxor    %xmm4, %xmm2\n"
        "\tpxor    %xmm4, %xmm1\n"
        "\tpxor    %xmm3, %xmm1\n"
        "\tpxor    %xmm3, %xmm0\n"
        "\tpxor    %xmm2, %xmm7\n"
        "\tpxor    %xmm2, %xmm4\n"
        "\tpxor    %xmm2, %xmm3\n"
        "\tpxor    %xmm2, %xmm1\n"
        "\tpxor    %xmm1, %xmm6\n"
        "\tpxor    %xmm1, %xmm5\n"
        "\tpxor    %xmm1, %xmm4\n"
        "\tpxor    %xmm1, %xmm2\n"
        "\tpxor    %xmm2, %xmm1\n"
        "\tpxor    %xmm0, %xmm7\n"
        "\tpxor    %xmm0, %xmm5\n"
        "\tpxor    %xmm0, %xmm4\n"
        "\tpxor    %xmm0, %xmm2\n"
        "\tpxor    %xmm2, %xmm0\n"
    );
}

static void gf8mul_10110101(void)
{
    __asm__ __volatile__
    (
        "\tpxor    %xmm7, %xmm5\n"
        "\tpxor    %xmm7, %xmm3\n"
        "\tpxor    %xmm7, %xmm2\n"
        "\tpxor    %xmm6, %xmm4\n"
        "\tpxor    %xmm6, %xmm2\n"
        "\tpxor    %xmm6, %xmm1\n"
        "\tpxor    %xmm5, %xmm3\n"
        "\tpxor    %xmm5, %xmm1\n"
        "\tpxor    %xmm5, %xmm0\n"
        "\tpxor    %xmm4, %xmm7\n"
        "\tpxor    %xmm4, %xmm5\n"
        "\tpxor    %xmm4, %xmm3\n"
        "\tpxor    %xmm4, %xmm2\n"
        "\tpxor    %xmm3, %xmm6\n"
        "\tpxor    %xmm3, %xmm4\n"
        "\tpxor    %xmm3, %xmm2\n"
        "\tpxor    %xmm3, %xmm1\n"
        "\tpxor    %xmm2, %xmm5\n"
        "\tpxor    %xmm2, %xmm3\n"
        "\tpxor    %xmm2, %xmm1\n"
        "\tpxor    %xmm2, %xmm0\n"
        "\tpxor    %xmm1, %xmm7\n"
        "\tpxor    %xmm1, %xmm6\n"
        "\tpxor    %xmm1, %xmm0\n"
        "\tpxor    %xmm0, %xmm7\n"
        "\tpxor    %xmm0, %xmm5\n"
        "\tpxor    %xmm0, %xmm4\n"
        "\tpxor    %xmm0, %xmm2\n"
    );
}

static void gf8mul_10110110(void)
{
    __asm__ __volatile__
    (
        "\tpxor    %xmm7, %xmm6\n"
        "\tpxor    %xmm7, %xmm5\n"
        "\tpxor    %xmm7, %xmm2\n"
        "\tpxor    %xmm7, %xmm0\n"
        "\tpxor    %xmm6, %xmm7\n"
        "\tpxor    %xmm6, %xmm4\n"
        "\tpxor    %xmm6, %xmm3\n"
        "\tpxor    %xmm5, %xmm7\n"
        "\tpxor    %xmm5, %xmm6\n"
        "\tpxor    %xmm5, %xmm2\n"
        "\tpxor    %xmm5, %xmm1\n"
        "\tpxor    %xmm6, %xmm5\n"
        "\tpxor    %xmm4, %xmm7\n"
        "\tpxor    %xmm4, %xmm6\n"
        "\tpxor    %xmm4, %xmm3\n"
        "\tpxor    %xmm4, %xmm0\n"
        "\tpxor    %xmm6, %xmm4\n"
        "\tpxor    %xmm3, %xmm7\n"
        "\tpxor    %xmm3, %xmm5\n"
        "\tpxor    %xmm3, %xmm1\n"
        "\tpxor    %xmm2, %xmm6\n"
        "\tpxor    %xmm2, %xmm4\n"
        "\tpxor    %xmm2, %xmm0\n"
        "\tpxor    %xmm1, %xmm7\n"
        "\tpxor    %xmm1, %xmm6\n"
        "\tpxor    %xmm1, %xmm2\n"
        "\tpxor    %xmm1, %xmm0\n"
        "\tpxor    %xmm0, %xmm7\n"
        "\tpxor    %xmm0, %xmm5\n"
        "\tpxor    %xmm0, %xmm4\n"
        "\tpxor    %xmm0, %xmm2\n"
        "\tpxor    %xmm0, %xmm1\n"
        "\tpxor    %xmm1, %xmm0\n"
    );
}

static void gf8mul_10110111(void)
{
    __asm__ __volatile__
    (
        "\tpxor    %xmm7, %xmm6\n"
        "\tpxor    %xmm7, %xmm5\n"
        "\tpxor    %xmm7, %xmm4\n"
        "\tpxor    %xmm7, %xmm1\n"
        "\tpxor    %xmm6, %xmm7\n"
        "\tpxor    %xmm6, %xmm5\n"
        "\tpxor    %xmm6, %xmm4\n"
        "\tpxor    %xmm6, %xmm3\n"
        "\tpxor    %xmm6, %xmm1\n"
        "\tpxor    %xmm7, %xmm6\n"
        "\tpxor    %xmm5, %xmm7\n"
        "\tpxor    %xmm5, %xmm4\n"
        "\tpxor    %xmm5, %xmm3\n"
        "\tpxor    %xmm5, %xmm2\n"
        "\tpxor    %xmm5, %xmm1\n"
        "\tpxor    %xmm7, %xmm5\n"
        "\tpxor    %xmm4, %xmm7\n"
        "\tpxor    %xmm4, %xmm3\n"
        "\tpxor    %xmm4, %xmm2\n"
        "\tpxor    %xmm7, %xmm4\n"
        "\tpxor    %xmm3, %xmm7\n"
        "\tpxor    %xmm3, %xmm2\n"
        "\tpxor    %xmm3, %xmm0\n"
        "\tpxor    %xmm7, %xmm3\n"
        "\tpxor    %xmm2, %xmm7\n"
        "\tpxor    %xmm2, %xmm6\n"
        "\tpxor    %xmm2, %xmm5\n"
        "\tpxor    %xmm1, %xmm7\n"
        "\tpxor    %xmm1, %xmm6\n"
        "\tpxor    %xmm1, %xmm2\n"
        "\tpxor    %xmm1, %xmm0\n"
        "\tpxor    %xmm2, %xmm1\n"
        "\tpxor    %xmm0, %xmm7\n"
        "\tpxor    %xmm0, %xmm5\n"
        "\tpxor    %xmm0, %xmm4\n"
        "\tpxor    %xmm0, %xmm2\n"
        "\tpxor    %xmm0, %xmm1\n"
    );
}

static void gf8mul_10111000(void)
{
    __asm__ __volatile__
    (
        "\tpxor    %xmm7, %xmm5\n"
        "\tpxor    %xmm7, %xmm3\n"
        "\tpxor    %xmm7, %xmm1\n"
        "\tpxor    %xmm7, %xmm0\n"
        "\tpxor    %xmm6, %xmm7\n"
        "\tpxor    %xmm6, %xmm5\n"
        "\tpxor    %xmm6, %xmm4\n"
        "\tpxor    %xmm5, %xmm7\n"
        "\tpxor    %xmm5, %xmm3\n"
        "\tpxor    %xmm5, %xmm2\n"
        "\tpxor    %xmm5, %xmm1\n"
        "\tpxor    %xmm7, %xmm5\n"
        "\tpxor    %xmm4, %xmm6\n"
        "\tpxor    %xmm4, %xmm2\n"
        "\tpxor    %xmm4, %xmm1\n"
        "\tpxor    %xmm4, %xmm0\n"
        "\tpxor    %xmm6, %xmm4\n"
        "\tpxor    %xmm3, %xmm7\n"
        "\tpxor    %xmm3, %xmm5\n"
        "\tpxor    %xmm2, %xmm3\n"
        "\tpxor    %xmm2, %xmm1\n"
        "\tpxor    %xmm2, %xmm0\n"
        "\tpxor    %xmm1, %xmm6\n"
        "\tpxor    %xmm1, %xmm5\n"
        "\tpxor    %xmm1, %xmm3\n"
        "\tpxor    %xmm1, %xmm2\n"
        "\tpxor    %xmm2, %xmm1\n"
        "\tpxor    %xmm0, %xmm7\n"
        "\tpxor    %xmm0, %xmm5\n"
        "\tpxor    %xmm0, %xmm4\n"
        "\tpxor    %xmm0, %xmm3\n"
        "\tpxor    %xmm3, %xmm0\n"
    );
}

static void gf8mul_10111001(void)
{
    __asm__ __volatile__
    (
        "\tpxor    %xmm7, %xmm4\n"
        "\tpxor    %xmm6, %xmm3\n"
        "\tpxor    %xmm5, %xmm2\n"
        "\tpxor    %xmm4, %xmm1\n"
        "\tpxor    %xmm3, %xmm0\n"
        "\tpxor    %xmm2, %xmm6\n"
        "\tpxor    %xmm2, %xmm5\n"
        "\tpxor    %xmm2, %xmm3\n"
        "\tpxor    %xmm2, %xmm1\n"
        "\tpxor    %xmm1, %xmm5\n"
        "\tpxor    %xmm1, %xmm2\n"
        "\tpxor    %xmm2, %xmm1\n"
        "\tpxor    %xmm0, %xmm7\n"
        "\tpxor    %xmm0, %xmm4\n"
        "\tpxor    %xmm0, %xmm3\n"
        "\tpxor    %xmm0, %xmm2\n"
        "\tpxor    %xmm2, %xmm0\n"
        "\tpxor    %xmm7, %xmm3\n"
        "\tpxor    %xmm7, %xmm1\n"
        "\tpxor    %xmm7, %xmm0\n"
        "\tpxor    %xmm6, %xmm7\n"
        "\tpxor    %xmm5, %xmm6\n"
        "\tpxor    %xmm4, %xmm5\n"
        "\tpxor    %xmm3, %xmm4\n"
        "\tpxor    %xmm2, %xmm3\n"
        "\tpxor    %xmm1, %xmm2\n"
        "\tpxor    %xmm0, %xmm1\n"
    );
}

static void gf8mul_10111010(void)
{
    __asm__ __volatile__
    (
        "\tpxor    %xmm7, %xmm5\n"
        "\tpxor    %xmm7, %xmm2\n"
        "\tpxor    %xmm7, %xmm1\n"
        "\tpxor    %xmm7, %xmm0\n"
        "\tpxor    %xmm6, %xmm7\n"
        "\tpxor    %xmm6, %xmm5\n"
        "\tpxor    %xmm6, %xmm4\n"
        "\tpxor    %xmm6, %xmm3\n"
        "\tpxor    %xmm6, %xmm1\n"
        "\tpxor    %xmm5, %xmm6\n"
        "\tpxor    %xmm5, %xmm4\n"
        "\tpxor    %xmm5, %xmm3\n"
        "\tpxor    %xmm5, %xmm2\n"
        "\tpxor    %xmm5, %xmm0\n"
        "\tpxor    %xmm4, %xmm7\n"
        "\tpxor    %xmm4, %xmm5\n"
        "\tpxor    %xmm4, %xmm1\n"
        "\tpxor    %xmm4, %xmm0\n"
        "\tpxor    %xmm3, %xmm7\n"
        "\tpxor    %xmm3, %xmm6\n"
        "\tpxor    %xmm3, %xmm4\n"
        "\tpxor    %xmm3, %xmm1\n"
        "\tpxor    %xmm4, %xmm3\n"
        "\tpxor    %xmm2, %xmm6\n"
        "\tpxor    %xmm2, %xmm5\n"
        "\tpxor    %xmm2, %xmm3\n"
        "\tpxor    %xmm2, %xmm0\n"
        "\tpxor    %xmm3, %xmm2\n"
        "\tpxor    %xmm1, %xmm7\n"
        "\tpxor    %xmm1, %xmm6\n"
        "\tpxor    %xmm1, %xmm4\n"
        "\tpxor    %xmm1, %xmm0\n"
        "\tpxor    %xmm0, %xmm7\n"
        "\tpxor    %xmm0, %xmm5\n"
        "\tpxor    %xmm0, %xmm4\n"
        "\tpxor    %xmm0, %xmm3\n"
        "\tpxor    %xmm0, %xmm1\n"
        "\tpxor    %xmm1, %xmm0\n"
    );
}

static void gf8mul_10111011(void)
{
    __asm__ __volatile__
    (
        "\tpxor    %xmm7, %xmm6\n"
        "\tpxor    %xmm7, %xmm5\n"
        "\tpxor    %xmm6, %xmm7\n"
        "\tpxor    %xmm6, %xmm3\n"
        "\tpxor    %xmm6, %xmm1\n"
        "\tpxor    %xmm6, %xmm0\n"
        "\tpxor    %xmm7, %xmm6\n"
        "\tpxor    %xmm5, %xmm7\n"
        "\tpxor    %xmm5, %xmm6\n"
        "\tpxor    %xmm5, %xmm4\n"
        "\tpxor    %xmm5, %xmm3\n"
        "\tpxor    %xmm5, %xmm1\n"
        "\tpxor    %xmm5, %xmm0\n"
        "\tpxor    %xmm4, %xmm2\n"
        "\tpxor    %xmm4, %xmm0\n"
        "\tpxor    %xmm3, %xmm7\n"
        "\tpxor    %xmm3, %xmm6\n"
        "\tpxor    %xmm3, %xmm4\n"
        "\tpxor    %xmm3, %xmm1\n"
        "\tpxor    %xmm2, %xmm7\n"
        "\tpxor    %xmm2, %xmm6\n"
        "\tpxor    %xmm2, %xmm4\n"
        "\tpxor    %xmm1, %xmm7\n"
        "\tpxor    %xmm1, %xmm6\n"
        "\tpxor    %xmm1, %xmm4\n"
        "\tpxor    %xmm1, %xmm0\n"
        "\tpxor    %xmm4, %xmm1\n"
        "\tpxor    %xmm0, %xmm7\n"
        "\tpxor    %xmm0, %xmm5\n"
        "\tpxor    %xmm0, %xmm4\n"
        "\tpxor    %xmm0, %xmm3\n"
        "\tpxor    %xmm0, %xmm1\n"
    );
}

static void gf8mul_10111100(void)
{
    __asm__ __volatile__
    (
        "\tpxor    %xmm7, %xmm6\n"
        "\tpxor    %xmm7, %xmm5\n"
        "\tpxor    %xmm7, %xmm3\n"
        "\tpxor    %xmm7, %xmm2\n"
        "\tpxor    %xmm7, %xmm1\n"
        "\tpxor    %xmm7, %xmm0\n"
        "\tpxor    %xmm6, %xmm7\n"
        "\tpxor    %xmm6, %xmm0\n"
        "\tpxor    %xmm7, %xmm6\n"
        "\tpxor    %xmm5, %xmm3\n"
        "\tpxor    %xmm5, %xmm2\n"
        "\tpxor    %xmm5, %xmm1\n"
        "\tpxor    %xmm4, %xmm7\n"
        "\tpxor    %xmm4, %xmm5\n"
        "\tpxor    %xmm4, %xmm2\n"
        "\tpxor    %xmm4, %xmm1\n"
        "\tpxor    %xmm5, %xmm4\n"
        "\tpxor    %xmm3, %xmm6\n"
        "\tpxor    %xmm3, %xmm4\n"
        "\tpxor    %xmm3, %xmm1\n"
        "\tpxor    %xmm3, %xmm0\n"
        "\tpxor    %xmm4, %xmm3\n"
        "\tpxor    %xmm2, %xmm7\n"
        "\tpxor    %xmm2, %xmm5\n"
        "\tpxor    %xmm2, %xmm3\n"
        "\tpxor    %xmm2, %xmm1\n"
        "\tpxor    %xmm1, %xmm6\n"
        "\tpxor    %xmm1, %xmm5\n"
        "\tpxor    %xmm1, %xmm2\n"
        "\tpxor    %xmm2, %xmm1\n"
        "\tpxor    %xmm0, %xmm7\n"
        "\tpxor    %xmm0, %xmm5\n"
        "\tpxor    %xmm0, %xmm4\n"
        "\tpxor    %xmm0, %xmm3\n"
        "\tpxor    %xmm0, %xmm2\n"
        "\tpxor    %xmm2, %xmm0\n"
    );
}

static void gf8mul_10111101(void)
{
    __asm__ __volatile__
    (
        "\tpxor    %xmm7, %xmm4\n"
        "\tpxor    %xmm7, %xmm1\n"
        "\tpxor    %xmm6, %xmm3\n"
        "\tpxor    %xmm6, %xmm0\n"
        "\tpxor    %xmm5, %xmm7\n"
        "\tpxor    %xmm5, %xmm4\n"
        "\tpxor    %xmm5, %xmm3\n"
        "\tpxor    %xmm4, %xmm6\n"
        "\tpxor    %xmm4, %xmm3\n"
        "\tpxor    %xmm4, %xmm2\n"
        "\tpxor    %xmm3, %xmm5\n"
        "\tpxor    %xmm3, %xmm2\n"
        "\tpxor    %xmm3, %xmm1\n"
        "\tpxor    %xmm2, %xmm4\n"
        "\tpxor    %xmm2, %xmm1\n"
        "\tpxor    %xmm2, %xmm0\n"
        "\tpxor    %xmm1, %xmm7\n"
        "\tpxor    %xmm1, %xmm6\n"
        "\tpxor    %xmm1, %xmm4\n"
        "\tpxor    %xmm1, %xmm3\n"
        "\tpxor    %xmm1, %xmm0\n"
        "\tpxor    %xmm0, %xmm7\n"
        "\tpxor    %xmm0, %xmm5\n"
        "\tpxor    %xmm0, %xmm4\n"
        "\tpxor    %xmm0, %xmm3\n"
        "\tpxor    %xmm0, %xmm2\n"
    );
}

static void gf8mul_10111110(void)
{
    __asm__ __volatile__
    (
        "\tpxor    %xmm7, %xmm3\n"
        "\tpxor    %xmm7, %xmm2\n"
        "\tpxor    %xmm7, %xmm0\n"
        "\tpxor    %xmm6, %xmm7\n"
        "\tpxor    %xmm6, %xmm2\n"
        "\tpxor    %xmm6, %xmm0\n"
        "\tpxor    %xmm5, %xmm7\n"
        "\tpxor    %xmm5, %xmm6\n"
        "\tpxor    %xmm5, %xmm0\n"
        "\tpxor    %xmm4, %xmm7\n"
        "\tpxor    %xmm4, %xmm6\n"
        "\tpxor    %xmm4, %xmm5\n"
        "\tpxor    %xmm4, %xmm1\n"
        "\tpxor    %xmm4, %xmm0\n"
        "\tpxor    %xmm3, %xmm7\n"
        "\tpxor    %xmm3, %xmm6\n"
        "\tpxor    %xmm3, %xmm5\n"
        "\tpxor    %xmm3, %xmm4\n"
        "\tpxor    %xmm3, %xmm1\n"
        "\tpxor    %xmm2, %xmm6\n"
        "\tpxor    %xmm2, %xmm5\n"
        "\tpxor    %xmm2, %xmm4\n"
        "\tpxor    %xmm2, %xmm3\n"
        "\tpxor    %xmm2, %xmm0\n"
        "\tpxor    %xmm1, %xmm7\n"
        "\tpxor    %xmm1, %xmm6\n"
        "\tpxor    %xmm1, %xmm4\n"
        "\tpxor    %xmm1, %xmm3\n"
        "\tpxor    %xmm1, %xmm2\n"
        "\tpxor    %xmm1, %xmm0\n"
        "\tpxor    %xmm0, %xmm7\n"
        "\tpxor    %xmm0, %xmm5\n"
        "\tpxor    %xmm0, %xmm4\n"
        "\tpxor    %xmm0, %xmm3\n"
        "\tpxor    %xmm0, %xmm2\n"
        "\tpxor    %xmm0, %xmm1\n"
        "\tpxor    %xmm1, %xmm0\n"
    );
}

static void gf8mul_10111111(void)
{
    __asm__ __volatile__
    (
        "\tpxor    %xmm7, %xmm5\n"
        "\tpxor    %xmm6, %xmm4\n"
        "\tpxor    %xmm5, %xmm3\n"
        "\tpxor    %xmm4, %xmm2\n"
        "\tpxor    %xmm3, %xmm1\n"
        "\tpxor    %xmm2, %xmm0\n"
        "\tpxor    %xmm1, %xmm7\n"
        "\tpxor    %xmm1, %xmm6\n"
        "\tpxor    %xmm1, %xmm5\n"
        "\tpxor    %xmm1, %xmm4\n"
        "\tpxor    %xmm1, %xmm2\n"
        "\tpxor    %xmm1, %xmm0\n"
        "\tpxor    %xmm0, %xmm7\n"
        "\tpxor    %xmm0, %xmm5\n"
        "\tpxor    %xmm0, %xmm2\n"
        "\tpxor    %xmm0, %xmm1\n"
        "\tpxor    %xmm1, %xmm0\n"
        "\tpxor    %xmm7, %xmm4\n"
        "\tpxor    %xmm7, %xmm3\n"
        "\tpxor    %xmm6, %xmm3\n"
        "\tpxor    %xmm6, %xmm2\n"
        "\tpxor    %xmm5, %xmm2\n"
        "\tpxor    %xmm5, %xmm1\n"
        "\tpxor    %xmm4, %xmm7\n"
        "\tpxor    %xmm4, %xmm2\n"
        "\tpxor    %xmm7, %xmm4\n"
        "\tpxor    %xmm3, %xmm6\n"
        "\tpxor    %xmm3, %xmm1\n"
        "\tpxor    %xmm6, %xmm3\n"
        "\tpxor    %xmm2, %xmm5\n"
        "\tpxor    %xmm2, %xmm0\n"
        "\tpxor    %xmm5, %xmm2\n"
        "\tpxor    %xmm1, %xmm7\n"
        "\tpxor    %xmm1, %xmm5\n"
        "\tpxor    %xmm1, %xmm4\n"
        "\tpxor    %xmm0, %xmm6\n"
        "\tpxor    %xmm0, %xmm4\n"
        "\tpxor    %xmm0, %xmm3\n"
    );
}

static void gf8mul_11000000(void)
{
    __asm__ __volatile__
    (
        "\tpxor    %xmm7, %xmm6\n"
        "\tpxor    %xmm7, %xmm5\n"
        "\tpxor    %xmm7, %xmm4\n"
        "\tpxor    %xmm7, %xmm2\n"
        "\tpxor    %xmm7, %xmm0\n"
        "\tpxor    %xmm6, %xmm7\n"
        "\tpxor    %xmm6, %xmm4\n"
        "\tpxor    %xmm6, %xmm2\n"
        "\tpxor    %xmm5, %xmm7\n"
        "\tpxor    %xmm5, %xmm6\n"
        "\tpxor    %xmm5, %xmm3\n"
        "\tpxor    %xmm5, %xmm2\n"
        "\tpxor    %xmm5, %xmm1\n"
        "\tpxor    %xmm6, %xmm5\n"
        "\tpxor    %xmm4, %xmm7\n"
        "\tpxor    %xmm4, %xmm6\n"
        "\tpxor    %xmm4, %xmm1\n"
        "\tpxor    %xmm4, %xmm0\n"
        "\tpxor    %xmm6, %xmm4\n"
        "\tpxor    %xmm3, %xmm6\n"
        "\tpxor    %xmm3, %xmm5\n"
        "\tpxor    %xmm3, %xmm2\n"
        "\tpxor    %xmm2, %xmm7\n"
        "\tpxor    %xmm2, %xmm6\n"
        "\tpxor    %xmm2, %xmm5\n"
        "\tpxor    %xmm2, %xmm0\n"
        "\tpxor    %xmm1, %xmm6\n"
        "\tpxor    %xmm1, %xmm4\n"
        "\tpxor    %xmm1, %xmm3\n"
        "\tpxor    %xmm1, %xmm2\n"
        "\tpxor    %xmm1, %xmm0\n"
        "\tpxor    %xmm2, %xmm1\n"
        "\tpxor    %xmm0, %xmm7\n"
        "\tpxor    %xmm0, %xmm6\n"
        "\tpxor    %xmm6, %xmm0\n"
    );
}

static void gf8mul_11000001(void)
{
    __asm__ __volatile__
    (
        "\tpxor    %xmm7, %xmm4\n"
        "\tpxor    %xmm7, %xmm3\n"
        "\tpxor    %xmm7, %xmm1\n"
        "\tpxor    %xmm7, %xmm0\n"
        "\tpxor    %xmm6, %xmm7\n"
        "\tpxor    %xmm6, %xmm4\n"
        "\tpxor    %xmm6, %xmm3\n"
        "\tpxor    %xmm5, %xmm6\n"
        "\tpxor    %xmm5, %xmm3\n"
        "\tpxor    %xmm5, %xmm2\n"
        "\tpxor    %xmm4, %xmm5\n"
        "\tpxor    %xmm4, %xmm2\n"
        "\tpxor    %xmm4, %xmm1\n"
        "\tpxor    %xmm3, %xmm4\n"
        "\tpxor    %xmm3, %xmm1\n"
        "\tpxor    %xmm3, %xmm0\n"
        "\tpxor    %xmm2, %xmm7\n"
        "\tpxor    %xmm2, %xmm5\n"
        "\tpxor    %xmm2, %xmm4\n"
        "\tpxor    %xmm2, %xmm3\n"
        "\tpxor    %xmm2, %xmm1\n"
        "\tpxor    %xmm1, %xmm6\n"
        "\tpxor    %xmm1, %xmm4\n"
        "\tpxor    %xmm1, %xmm3\n"
        "\tpxor    %xmm1, %xmm2\n"
        "\tpxor    %xmm1, %xmm0\n"
        "\tpxor    %xmm0, %xmm7\n"
        "\tpxor    %xmm0, %xmm6\n"
    );
}

static void gf8mul_11000010(void)
{
    __asm__ __volatile__
    (
        "\tpxor    %xmm7, %xmm6\n"
        "\tpxor    %xmm7, %xmm3\n"
        "\tpxor    %xmm7, %xmm2\n"
        "\tpxor    %xmm7, %xmm0\n"
        "\tpxor    %xmm6, %xmm7\n"
        "\tpxor    %xmm6, %xmm5\n"
        "\tpxor    %xmm6, %xmm2\n"
        "\tpxor    %xmm7, %xmm6\n"
        "\tpxor    %xmm5, %xmm6\n"
        "\tpxor    %xmm5, %xmm4\n"
        "\tpxor    %xmm5, %xmm1\n"
        "\tpxor    %xmm6, %xmm5\n"
        "\tpxor    %xmm4, %xmm5\n"
        "\tpxor    %xmm4, %xmm3\n"
        "\tpxor    %xmm4, %xmm0\n"
        "\tpxor    %xmm5, %xmm4\n"
        "\tpxor    %xmm3, %xmm7\n"
        "\tpxor    %xmm3, %xmm6\n"
        "\tpxor    %xmm3, %xmm5\n"
        "\tpxor    %xmm3, %xmm2\n"
        "\tpxor    %xmm3, %xmm1\n"
        "\tpxor    %xmm2, %xmm6\n"
        "\tpxor    %xmm2, %xmm5\n"
        "\tpxor    %xmm2, %xmm4\n"
        "\tpxor    %xmm2, %xmm1\n"
        "\tpxor    %xmm2, %xmm0\n"
        "\tpxor    %xmm1, %xmm6\n"
        "\tpxor    %xmm1, %xmm4\n"
        "\tpxor    %xmm1, %xmm3\n"
        "\tpxor    %xmm1, %xmm0\n"
        "\tpxor    %xmm0, %xmm7\n"
        "\tpxor    %xmm0, %xmm6\n"
        "\tpxor    %xmm0, %xmm1\n"
        "\tpxor    %xmm1, %xmm0\n"
    );
}

static void gf8mul_11000011(void)
{
    __asm__ __volatile__
    (
        "\tpxor    %xmm7, %xmm5\n"
        "\tpxor    %xmm7, %xmm4\n"
        "\tpxor    %xmm7, %xmm3\n"
        "\tpxor    %xmm7, %xmm2\n"
        "\tpxor    %xmm6, %xmm4\n"
        "\tpxor    %xmm6, %xmm3\n"
        "\tpxor    %xmm6, %xmm2\n"
        "\tpxor    %xmm6, %xmm1\n"
        "\tpxor    %xmm5, %xmm3\n"
        "\tpxor    %xmm5, %xmm2\n"
        "\tpxor    %xmm5, %xmm1\n"
        "\tpxor    %xmm5, %xmm0\n"
        "\tpxor    %xmm4, %xmm5\n"
        "\tpxor    %xmm4, %xmm2\n"
        "\tpxor    %xmm4, %xmm0\n"
        "\tpxor    %xmm3, %xmm7\n"
        "\tpxor    %xmm3, %xmm5\n"
        "\tpxor    %xmm3, %xmm4\n"
        "\tpxor    %xmm3, %xmm1\n"
        "\tpxor    %xmm4, %xmm3\n"
        "\tpxor    %xmm2, %xmm6\n"
        "\tpxor    %xmm2, %xmm4\n"
        "\tpxor    %xmm2, %xmm3\n"
        "\tpxor    %xmm2, %xmm0\n"
        "\tpxor    %xmm3, %xmm2\n"
        "\tpxor    %xmm1, %xmm7\n"
        "\tpxor    %xmm0, %xmm6\n"
        "\tpxor    %xmm7, %xmm3\n"
        "\tpxor    %xmm7, %xmm1\n"
        "\tpxor    %xmm7, %xmm0\n"
        "\tpxor    %xmm6, %xmm7\n"
        "\tpxor    %xmm5, %xmm6\n"
        "\tpxor    %xmm4, %xmm5\n"
        "\tpxor    %xmm3, %xmm4\n"
        "\tpxor    %xmm2, %xmm3\n"
        "\tpxor    %xmm1, %xmm2\n"
        "\tpxor    %xmm0, %xmm1\n"
    );
}

static void gf8mul_11000100(void)
{
    __asm__ __volatile__
    (
        "\tpxor    %xmm7, %xmm6\n"
        "\tpxor    %xmm7, %xmm5\n"
        "\tpxor    %xmm7, %xmm4\n"
        "\tpxor    %xmm6, %xmm5\n"
        "\tpxor    %xmm6, %xmm4\n"
        "\tpxor    %xmm6, %xmm3\n"
        "\tpxor    %xmm5, %xmm4\n"
        "\tpxor    %xmm5, %xmm3\n"
        "\tpxor    %xmm5, %xmm2\n"
        "\tpxor    %xmm4, %xmm3\n"
        "\tpxor    %xmm4, %xmm2\n"
        "\tpxor    %xmm4, %xmm1\n"
        "\tpxor    %xmm3, %xmm2\n"
        "\tpxor    %xmm3, %xmm1\n"
        "\tpxor    %xmm3, %xmm0\n"
        "\tpxor    %xmm2, %xmm5\n"
        "\tpxor    %xmm2, %xmm4\n"
        "\tpxor    %xmm1, %xmm7\n"
        "\tpxor    %xmm1, %xmm4\n"
        "\tpxor    %xmm1, %xmm2\n"
        "\tpxor    %xmm2, %xmm1\n"
        "\tpxor    %xmm0, %xmm7\n"
        "\tpxor    %xmm0, %xmm6\n"
        "\tpxor    %xmm0, %xmm2\n"
        "\tpxor    %xmm2, %xmm0\n"
    );
}

static void gf8mul_11000101(void)
{
    __asm__ __volatile__
    (
        "\tpxor    %xmm7, %xmm6\n"
        "\tpxor    %xmm7, %xmm5\n"
        "\tpxor    %xmm7, %xmm4\n"
        "\tpxor    %xmm7, %xmm3\n"
        "\tpxor    %xmm7, %xmm2\n"
        "\tpxor    %xmm7, %xmm0\n"
        "\tpxor    %xmm6, %xmm7\n"
        "\tpxor    %xmm6, %xmm1\n"
        "\tpxor    %xmm7, %xmm6\n"
        "\tpxor    %xmm5, %xmm6\n"
        "\tpxor    %xmm5, %xmm0\n"
        "\tpxor    %xmm6, %xmm5\n"
        "\tpxor    %xmm4, %xmm7\n"
        "\tpxor    %xmm4, %xmm6\n"
        "\tpxor    %xmm4, %xmm5\n"
        "\tpxor    %xmm4, %xmm3\n"
        "\tpxor    %xmm4, %xmm1\n"
        "\tpxor    %xmm4, %xmm0\n"
        "\tpxor    %xmm3, %xmm7\n"
        "\tpxor    %xmm3, %xmm4\n"
        "\tpxor    %xmm3, %xmm2\n"
        "\tpxor    %xmm3, %xmm0\n"
        "\tpxor    %xmm4, %xmm3\n"
        "\tpxor    %xmm2, %xmm7\n"
        "\tpxor    %xmm2, %xmm5\n"
        "\tpxor    %xmm2, %xmm1\n"
        "\tpxor    %xmm1, %xmm6\n"
        "\tpxor    %xmm1, %xmm4\n"
        "\tpxor    %xmm1, %xmm0\n"
        "\tpxor    %xmm0, %xmm7\n"
        "\tpxor    %xmm0, %xmm6\n"
        "\tpxor    %xmm0, %xmm2\n"
    );
}

static void gf8mul_11000110(void)
{
    __asm__ __volatile__
    (
        "\tpxor    %xmm7, %xmm6\n"
        "\tpxor    %xmm7, %xmm5\n"
        "\tpxor    %xmm7, %xmm3\n"
        "\tpxor    %xmm7, %xmm2\n"
        "\tpxor    %xmm6, %xmm7\n"
        "\tpxor    %xmm6, %xmm1\n"
        "\tpxor    %xmm6, %xmm0\n"
        "\tpxor    %xmm7, %xmm6\n"
        "\tpxor    %xmm5, %xmm7\n"
        "\tpxor    %xmm5, %xmm6\n"
        "\tpxor    %xmm5, %xmm4\n"
        "\tpxor    %xmm5, %xmm2\n"
        "\tpxor    %xmm5, %xmm0\n"
        "\tpxor    %xmm4, %xmm7\n"
        "\tpxor    %xmm4, %xmm6\n"
        "\tpxor    %xmm4, %xmm5\n"
        "\tpxor    %xmm3, %xmm7\n"
        "\tpxor    %xmm3, %xmm6\n"
        "\tpxor    %xmm3, %xmm4\n"
        "\tpxor    %xmm3, %xmm2\n"
        "\tpxor    %xmm3, %xmm1\n"
        "\tpxor    %xmm4, %xmm3\n"
        "\tpxor    %xmm2, %xmm6\n"
        "\tpxor    %xmm2, %xmm5\n"
        "\tpxor    %xmm2, %xmm3\n"
        "\tpxor    %xmm2, %xmm1\n"
        "\tpxor    %xmm2, %xmm0\n"
        "\tpxor    %xmm3, %xmm2\n"
        "\tpxor    %xmm1, %xmm6\n"
        "\tpxor    %xmm1, %xmm4\n"
        "\tpxor    %xmm1, %xmm2\n"
        "\tpxor    %xmm1, %xmm0\n"
        "\tpxor    %xmm0, %xmm7\n"
        "\tpxor    %xmm0, %xmm6\n"
        "\tpxor    %xmm0, %xmm2\n"
        "\tpxor    %xmm0, %xmm1\n"
        "\tpxor    %xmm1, %xmm0\n"
    );
}

static void gf8mul_11000111(void)
{
    __asm__ __volatile__
    (
        "\tpxor    %xmm7, %xmm5\n"
        "\tpxor    %xmm7, %xmm3\n"
        "\tpxor    %xmm6, %xmm4\n"
        "\tpxor    %xmm6, %xmm2\n"
        "\tpxor    %xmm5, %xmm3\n"
        "\tpxor    %xmm5, %xmm1\n"
        "\tpxor    %xmm4, %xmm2\n"
        "\tpxor    %xmm4, %xmm0\n"
        "\tpxor    %xmm3, %xmm7\n"
        "\tpxor    %xmm3, %xmm6\n"
        "\tpxor    %xmm3, %xmm4\n"
        "\tpxor    %xmm3, %xmm2\n"
        "\tpxor    %xmm3, %xmm1\n"
        "\tpxor    %xmm2, %xmm7\n"
        "\tpxor    %xmm2, %xmm5\n"
        "\tpxor    %xmm2, %xmm3\n"
        "\tpxor    %xmm2, %xmm1\n"
        "\tpxor    %xmm3, %xmm2\n"
        "\tpxor    %xmm1, %xmm6\n"
        "\tpxor    %xmm1, %xmm4\n"
        "\tpxor    %xmm1, %xmm2\n"
        "\tpxor    %xmm1, %xmm0\n"
        "\tpxor    %xmm2, %xmm1\n"
        "\tpxor    %xmm0, %xmm7\n"
        "\tpxor    %xmm0, %xmm6\n"
        "\tpxor    %xmm0, %xmm2\n"
        "\tpxor    %xmm0, %xmm1\n"
    );
}

static void gf8mul_11001000(void)
{
    __asm__ __volatile__
    (
        "\tpxor    %xmm7, %xmm6\n"
        "\tpxor    %xmm7, %xmm4\n"
        "\tpxor    %xmm7, %xmm3\n"
        "\tpxor    %xmm7, %xmm2\n"
        "\tpxor    %xmm7, %xmm1\n"
        "\tpxor    %xmm7, %xmm0\n"
        "\tpxor    %xmm6, %xmm7\n"
        "\tpxor    %xmm6, %xmm5\n"
        "\tpxor    %xmm6, %xmm0\n"
        "\tpxor    %xmm7, %xmm6\n"
        "\tpxor    %xmm5, %xmm7\n"
        "\tpxor    %xmm5, %xmm4\n"
        "\tpxor    %xmm5, %xmm3\n"
        "\tpxor    %xmm5, %xmm2\n"
        "\tpxor    %xmm5, %xmm1\n"
        "\tpxor    %xmm4, %xmm6\n"
        "\tpxor    %xmm4, %xmm3\n"
        "\tpxor    %xmm4, %xmm2\n"
        "\tpxor    %xmm4, %xmm1\n"
        "\tpxor    %xmm4, %xmm0\n"
        "\tpxor    %xmm3, %xmm2\n"
        "\tpxor    %xmm2, %xmm7\n"
        "\tpxor    %xmm2, %xmm6\n"
        "\tpxor    %xmm2, %xmm3\n"
        "\tpxor    %xmm2, %xmm0\n"
        "\tpxor    %xmm1, %xmm7\n"
        "\tpxor    %xmm1, %xmm3\n"
        "\tpxor    %xmm1, %xmm2\n"
        "\tpxor    %xmm2, %xmm1\n"
        "\tpxor    %xmm0, %xmm7\n"
        "\tpxor    %xmm0, %xmm6\n"
        "\tpxor    %xmm0, %xmm3\n"
        "\tpxor    %xmm3, %xmm0\n"
    );
}

static void gf8mul_11001001(void)
{
    __asm__ __volatile__
    (
        "\tpxor    %xmm7, %xmm6\n"
        "\tpxor    %xmm7, %xmm4\n"
        "\tpxor    %xmm7, %xmm3\n"
        "\tpxor    %xmm7, %xmm2\n"
        "\tpxor    %xmm7, %xmm0\n"
        "\tpxor    %xmm6, %xmm5\n"
        "\tpxor    %xmm6, %xmm3\n"
        "\tpxor    %xmm6, %xmm1\n"
        "\tpxor    %xmm6, %xmm0\n"
        "\tpxor    %xmm5, %xmm7\n"
        "\tpxor    %xmm5, %xmm6\n"
        "\tpxor    %xmm5, %xmm4\n"
        "\tpxor    %xmm5, %xmm3\n"
        "\tpxor    %xmm5, %xmm2\n"
        "\tpxor    %xmm5, %xmm0\n"
        "\tpxor    %xmm6, %xmm5\n"
        "\tpxor    %xmm4, %xmm7\n"
        "\tpxor    %xmm4, %xmm2\n"
        "\tpxor    %xmm4, %xmm1\n"
        "\tpxor    %xmm7, %xmm4\n"
        "\tpxor    %xmm3, %xmm6\n"
        "\tpxor    %xmm3, %xmm1\n"
        "\tpxor    %xmm3, %xmm0\n"
        "\tpxor    %xmm6, %xmm3\n"
        "\tpxor    %xmm2, %xmm7\n"
        "\tpxor    %xmm2, %xmm3\n"
        "\tpxor    %xmm2, %xmm1\n"
        "\tpxor    %xmm1, %xmm6\n"
        "\tpxor    %xmm1, %xmm2\n"
        "\tpxor    %xmm1, %xmm0\n"
        "\tpxor    %xmm0, %xmm7\n"
        "\tpxor    %xmm0, %xmm6\n"
        "\tpxor    %xmm0, %xmm3\n"
    );
}

static void gf8mul_11001010(void)
{
    __asm__ __volatile__
    (
        "\tpxor    %xmm7, %xmm6\n"
        "\tpxor    %xmm7, %xmm5\n"
        "\tpxor    %xmm7, %xmm4\n"
        "\tpxor    %xmm7, %xmm1\n"
        "\tpxor    %xmm6, %xmm7\n"
        "\tpxor    %xmm6, %xmm4\n"
        "\tpxor    %xmm6, %xmm2\n"
        "\tpxor    %xmm6, %xmm1\n"
        "\tpxor    %xmm6, %xmm0\n"
        "\tpxor    %xmm7, %xmm6\n"
        "\tpxor    %xmm5, %xmm7\n"
        "\tpxor    %xmm5, %xmm6\n"
        "\tpxor    %xmm5, %xmm3\n"
        "\tpxor    %xmm5, %xmm2\n"
        "\tpxor    %xmm5, %xmm1\n"
        "\tpxor    %xmm4, %xmm6\n"
        "\tpxor    %xmm4, %xmm5\n"
        "\tpxor    %xmm4, %xmm2\n"
        "\tpxor    %xmm4, %xmm1\n"
        "\tpxor    %xmm4, %xmm0\n"
        "\tpxor    %xmm3, %xmm7\n"
        "\tpxor    %xmm3, %xmm4\n"
        "\tpxor    %xmm3, %xmm2\n"
        "\tpxor    %xmm3, %xmm1\n"
        "\tpxor    %xmm2, %xmm6\n"
        "\tpxor    %xmm2, %xmm3\n"
        "\tpxor    %xmm2, %xmm1\n"
        "\tpxor    %xmm2, %xmm0\n"
        "\tpxor    %xmm1, %xmm6\n"
        "\tpxor    %xmm1, %xmm0\n"
        "\tpxor    %xmm0, %xmm7\n"
        "\tpxor    %xmm0, %xmm6\n"
        "\tpxor    %xmm0, %xmm3\n"
        "\tpxor    %xmm0, %xmm1\n"
        "\tpxor    %xmm1, %xmm0\n"
    );
}

static void gf8mul_11001011(void)
{
    __asm__ __volatile__
    (
        "\tpxor    %xmm7, %xmm6\n"
        "\tpxor    %xmm7, %xmm2\n"
        "\tpxor    %xmm6, %xmm7\n"
        "\tpxor    %xmm6, %xmm5\n"
        "\tpxor    %xmm6, %xmm4\n"
        "\tpxor    %xmm6, %xmm1\n"
        "\tpxor    %xmm6, %xmm0\n"
        "\tpxor    %xmm7, %xmm6\n"
        "\tpxor    %xmm5, %xmm6\n"
        "\tpxor    %xmm5, %xmm4\n"
        "\tpxor    %xmm5, %xmm3\n"
        "\tpxor    %xmm4, %xmm6\n"
        "\tpxor    %xmm4, %xmm5\n"
        "\tpxor    %xmm4, %xmm3\n"
        "\tpxor    %xmm4, %xmm1\n"
        "\tpxor    %xmm4, %xmm0\n"
        "\tpxor    %xmm3, %xmm7\n"
        "\tpxor    %xmm3, %xmm6\n"
        "\tpxor    %xmm3, %xmm4\n"
        "\tpxor    %xmm3, %xmm2\n"
        "\tpxor    %xmm3, %xmm0\n"
        "\tpxor    %xmm4, %xmm3\n"
        "\tpxor    %xmm2, %xmm7\n"
        "\tpxor    %xmm2, %xmm1\n"
        "\tpxor    %xmm7, %xmm2\n"
        "\tpxor    %xmm1, %xmm6\n"
        "\tpxor    %xmm1, %xmm0\n"
        "\tpxor    %xmm6, %xmm1\n"
        "\tpxor    %xmm0, %xmm7\n"
        "\tpxor    %xmm0, %xmm6\n"
        "\tpxor    %xmm0, %xmm3\n"
        "\tpxor    %xmm0, %xmm1\n"
    );
}

static void gf8mul_11001100(void)
{
    __asm__ __volatile__
    (
        "\tpxor    %xmm7, %xmm5\n"
        "\tpxor    %xmm7, %xmm3\n"
        "\tpxor    %xmm7, %xmm0\n"
        "\tpxor    %xmm6, %xmm7\n"
        "\tpxor    %xmm6, %xmm5\n"
        "\tpxor    %xmm6, %xmm4\n"
        "\tpxor    %xmm6, %xmm1\n"
        "\tpxor    %xmm6, %xmm0\n"
        "\tpxor    %xmm5, %xmm7\n"
        "\tpxor    %xmm4, %xmm7\n"
        "\tpxor    %xmm4, %xmm6\n"
        "\tpxor    %xmm4, %xmm5\n"
        "\tpxor    %xmm4, %xmm3\n"
        "\tpxor    %xmm4, %xmm2\n"
        "\tpxor    %xmm4, %xmm1\n"
        "\tpxor    %xmm5, %xmm4\n"
        "\tpxor    %xmm3, %xmm6\n"
        "\tpxor    %xmm3, %xmm5\n"
        "\tpxor    %xmm3, %xmm4\n"
        "\tpxor    %xmm3, %xmm2\n"
        "\tpxor    %xmm3, %xmm1\n"
        "\tpxor    %xmm3, %xmm0\n"
        "\tpxor    %xmm4, %xmm3\n"
        "\tpxor    %xmm2, %xmm4\n"
        "\tpxor    %xmm1, %xmm7\n"
        "\tpxor    %xmm1, %xmm2\n"
        "\tpxor    %xmm2, %xmm1\n"
        "\tpxor    %xmm0, %xmm7\n"
        "\tpxor    %xmm0, %xmm6\n"
        "\tpxor    %xmm0, %xmm3\n"
        "\tpxor    %xmm0, %xmm2\n"
        "\tpxor    %xmm2, %xmm0\n"
    );
}

static void gf8mul_11001101(void)
{
    __asm__ __volatile__
    (
        "\tpxor    %xmm7, %xmm6\n"
        "\tpxor    %xmm7, %xmm1\n"
        "\tpxor    %xmm6, %xmm7\n"
        "\tpxor    %xmm6, %xmm5\n"
        "\tpxor    %xmm6, %xmm3\n"
        "\tpxor    %xmm6, %xmm2\n"
        "\tpxor    %xmm7, %xmm6\n"
        "\tpxor    %xmm5, %xmm6\n"
        "\tpxor    %xmm5, %xmm4\n"
        "\tpxor    %xmm5, %xmm2\n"
        "\tpxor    %xmm5, %xmm1\n"
        "\tpxor    %xmm6, %xmm5\n"
        "\tpxor    %xmm4, %xmm5\n"
        "\tpxor    %xmm4, %xmm3\n"
        "\tpxor    %xmm4, %xmm1\n"
        "\tpxor    %xmm4, %xmm0\n"
        "\tpxor    %xmm5, %xmm4\n"
        "\tpxor    %xmm3, %xmm7\n"
        "\tpxor    %xmm3, %xmm6\n"
        "\tpxor    %xmm3, %xmm5\n"
        "\tpxor    %xmm3, %xmm4\n"
        "\tpxor    %xmm3, %xmm2\n"
        "\tpxor    %xmm3, %xmm0\n"
        "\tpxor    %xmm2, %xmm7\n"
        "\tpxor    %xmm2, %xmm4\n"
        "\tpxor    %xmm2, %xmm1\n"
        "\tpxor    %xmm1, %xmm6\n"
        "\tpxor    %xmm1, %xmm3\n"
        "\tpxor    %xmm1, %xmm0\n"
        "\tpxor    %xmm0, %xmm7\n"
        "\tpxor    %xmm0, %xmm6\n"
        "\tpxor    %xmm0, %xmm3\n"
        "\tpxor    %xmm0, %xmm2\n"
    );
}

static void gf8mul_11001110(void)
{
    __asm__ __volatile__
    (
        "\tpxor    %xmm7, %xmm5\n"
        "\tpxor    %xmm7, %xmm3\n"
        "\tpxor    %xmm7, %xmm1\n"
        "\tpxor    %xmm6, %xmm4\n"
        "\tpxor    %xmm6, %xmm2\n"
        "\tpxor    %xmm6, %xmm0\n"
        "\tpxor    %xmm5, %xmm7\n"
        "\tpxor    %xmm5, %xmm2\n"
        "\tpxor    %xmm5, %xmm1\n"
        "\tpxor    %xmm5, %xmm0\n"
        "\tpxor    %xmm4, %xmm6\n"
        "\tpxor    %xmm4, %xmm5\n"
        "\tpxor    %xmm3, %xmm7\n"
        "\tpxor    %xmm3, %xmm5\n"
        "\tpxor    %xmm3, %xmm2\n"
        "\tpxor    %xmm3, %xmm1\n"
        "\tpxor    %xmm5, %xmm3\n"
        "\tpxor    %xmm2, %xmm6\n"
        "\tpxor    %xmm2, %xmm4\n"
        "\tpxor    %xmm2, %xmm1\n"
        "\tpxor    %xmm2, %xmm0\n"
        "\tpxor    %xmm4, %xmm2\n"
        "\tpxor    %xmm1, %xmm6\n"
        "\tpxor    %xmm1, %xmm3\n"
        "\tpxor    %xmm1, %xmm2\n"
        "\tpxor    %xmm1, %xmm0\n"
        "\tpxor    %xmm0, %xmm7\n"
        "\tpxor    %xmm0, %xmm6\n"
        "\tpxor    %xmm0, %xmm3\n"
        "\tpxor    %xmm0, %xmm2\n"
        "\tpxor    %xmm0, %xmm1\n"
        "\tpxor    %xmm1, %xmm0\n"
    );
}

static void gf8mul_11001111(void)
{
    __asm__ __volatile__
    (
        "\tpxor    %xmm7, %xmm6\n"
        "\tpxor    %xmm7, %xmm4\n"
        "\tpxor    %xmm7, %xmm2\n"
        "\tpxor    %xmm6, %xmm5\n"
        "\tpxor    %xmm6, %xmm3\n"
        "\tpxor    %xmm6, %xmm1\n"
        "\tpxor    %xmm5, %xmm4\n"
        "\tpxor    %xmm5, %xmm2\n"
        "\tpxor    %xmm5, %xmm0\n"
        "\tpxor    %xmm4, %xmm7\n"
        "\tpxor    %xmm4, %xmm6\n"
        "\tpxor    %xmm4, %xmm5\n"
        "\tpxor    %xmm4, %xmm3\n"
        "\tpxor    %xmm4, %xmm2\n"
        "\tpxor    %xmm4, %xmm1\n"
        "\tpxor    %xmm4, %xmm0\n"
        "\tpxor    %xmm3, %xmm7\n"
        "\tpxor    %xmm3, %xmm4\n"
        "\tpxor    %xmm3, %xmm1\n"
        "\tpxor    %xmm2, %xmm6\n"
        "\tpxor    %xmm2, %xmm3\n"
        "\tpxor    %xmm2, %xmm0\n"
        "\tpxor    %xmm1, %xmm7\n"
        "\tpxor    %xmm1, %xmm3\n"
        "\tpxor    %xmm0, %xmm6\n"
        "\tpxor    %xmm0, %xmm2\n"
        "\tpxor    %xmm7, %xmm3\n"
        "\tpxor    %xmm7, %xmm1\n"
        "\tpxor    %xmm7, %xmm0\n"
        "\tpxor    %xmm6, %xmm7\n"
        "\tpxor    %xmm5, %xmm6\n"
        "\tpxor    %xmm4, %xmm5\n"
        "\tpxor    %xmm3, %xmm4\n"
        "\tpxor    %xmm2, %xmm3\n"
        "\tpxor    %xmm1, %xmm2\n"
        "\tpxor    %xmm0, %xmm1\n"
    );
}

static void gf8mul_11010000(void)
{
    __asm__ __volatile__
    (
        "\tpxor    %xmm7, %xmm6\n"
        "\tpxor    %xmm7, %xmm5\n"
        "\tpxor    %xmm7, %xmm1\n"
        "\tpxor    %xmm6, %xmm7\n"
        "\tpxor    %xmm6, %xmm5\n"
        "\tpxor    %xmm6, %xmm4\n"
        "\tpxor    %xmm7, %xmm6\n"
        "\tpxor    %xmm5, %xmm7\n"
        "\tpxor    %xmm5, %xmm4\n"
        "\tpxor    %xmm5, %xmm3\n"
        "\tpxor    %xmm5, %xmm0\n"
        "\tpxor    %xmm7, %xmm5\n"
        "\tpxor    %xmm4, %xmm6\n"
        "\tpxor    %xmm4, %xmm0\n"
        "\tpxor    %xmm3, %xmm6\n"
        "\tpxor    %xmm3, %xmm5\n"
        "\tpxor    %xmm3, %xmm4\n"
        "\tpxor    %xmm3, %xmm2\n"
        "\tpxor    %xmm3, %xmm0\n"
        "\tpxor    %xmm2, %xmm7\n"
        "\tpxor    %xmm2, %xmm5\n"
        "\tpxor    %xmm2, %xmm4\n"
        "\tpxor    %xmm2, %xmm0\n"
        "\tpxor    %xmm1, %xmm7\n"
        "\tpxor    %xmm1, %xmm5\n"
        "\tpxor    %xmm1, %xmm4\n"
        "\tpxor    %xmm1, %xmm3\n"
        "\tpxor    %xmm1, %xmm2\n"
        "\tpxor    %xmm2, %xmm1\n"
        "\tpxor    %xmm0, %xmm7\n"
        "\tpxor    %xmm0, %xmm6\n"
        "\tpxor    %xmm0, %xmm4\n"
        "\tpxor    %xmm4, %xmm0\n"
    );
}

static void gf8mul_11010001(void)
{
    __asm__ __volatile__
    (
        "\tpxor    %xmm7, %xmm5\n"
        "\tpxor    %xmm7, %xmm2\n"
        "\tpxor    %xmm6, %xmm4\n"
        "\tpxor    %xmm6, %xmm1\n"
        "\tpxor    %xmm5, %xmm7\n"
        "\tpxor    %xmm5, %xmm1\n"
        "\tpxor    %xmm7, %xmm5\n"
        "\tpxor    %xmm4, %xmm6\n"
        "\tpxor    %xmm4, %xmm0\n"
        "\tpxor    %xmm6, %xmm4\n"
        "\tpxor    %xmm3, %xmm7\n"
        "\tpxor    %xmm3, %xmm5\n"
        "\tpxor    %xmm3, %xmm1\n"
        "\tpxor    %xmm3, %xmm0\n"
        "\tpxor    %xmm2, %xmm7\n"
        "\tpxor    %xmm2, %xmm6\n"
        "\tpxor    %xmm2, %xmm4\n"
        "\tpxor    %xmm2, %xmm3\n"
        "\tpxor    %xmm2, %xmm1\n"
        "\tpxor    %xmm1, %xmm6\n"
        "\tpxor    %xmm1, %xmm5\n"
        "\tpxor    %xmm1, %xmm3\n"
        "\tpxor    %xmm1, %xmm2\n"
        "\tpxor    %xmm1, %xmm0\n"
        "\tpxor    %xmm0, %xmm7\n"
        "\tpxor    %xmm0, %xmm6\n"
        "\tpxor    %xmm0, %xmm4\n"
    );
}

static void gf8mul_11010010(void)
{
    __asm__ __volatile__
    (
        "\tpxor    %xmm7, %xmm6\n"
        "\tpxor    %xmm7, %xmm5\n"
        "\tpxor    %xmm6, %xmm5\n"
        "\tpxor    %xmm6, %xmm4\n"
        "\tpxor    %xmm5, %xmm4\n"
        "\tpxor    %xmm5, %xmm3\n"
        "\tpxor    %xmm4, %xmm3\n"
        "\tpxor    %xmm4, %xmm2\n"
        "\tpxor    %xmm3, %xmm2\n"
        "\tpxor    %xmm3, %xmm1\n"
        "\tpxor    %xmm2, %xmm1\n"
        "\tpxor    %xmm2, %xmm0\n"
        "\tpxor    %xmm1, %xmm6\n"
        "\tpxor    %xmm1, %xmm5\n"
        "\tpxor    %xmm1, %xmm3\n"
        "\tpxor    %xmm1, %xmm0\n"
        "\tpxor    %xmm0, %xmm7\n"
        "\tpxor    %xmm0, %xmm6\n"
        "\tpxor    %xmm0, %xmm4\n"
        "\tpxor    %xmm0, %xmm1\n"
        "\tpxor    %xmm1, %xmm0\n"
    );
}

static void gf8mul_11010011(void)
{
    __asm__ __volatile__
    (
        "\tpxor    %xmm7, %xmm4\n"
        "\tpxor    %xmm7, %xmm2\n"
        "\tpxor    %xmm7, %xmm1\n"
        "\tpxor    %xmm6, %xmm3\n"
        "\tpxor    %xmm6, %xmm1\n"
        "\tpxor    %xmm6, %xmm0\n"
        "\tpxor    %xmm5, %xmm7\n"
        "\tpxor    %xmm5, %xmm4\n"
        "\tpxor    %xmm5, %xmm3\n"
        "\tpxor    %xmm5, %xmm2\n"
        "\tpxor    %xmm5, %xmm0\n"
        "\tpxor    %xmm4, %xmm7\n"
        "\tpxor    %xmm4, %xmm5\n"
        "\tpxor    %xmm4, %xmm3\n"
        "\tpxor    %xmm4, %xmm0\n"
        "\tpxor    %xmm3, %xmm7\n"
        "\tpxor    %xmm3, %xmm5\n"
        "\tpxor    %xmm3, %xmm4\n"
        "\tpxor    %xmm3, %xmm1\n"
        "\tpxor    %xmm3, %xmm0\n"
        "\tpxor    %xmm2, %xmm7\n"
        "\tpxor    %xmm2, %xmm6\n"
        "\tpxor    %xmm2, %xmm4\n"
        "\tpxor    %xmm2, %xmm1\n"
        "\tpxor    %xmm4, %xmm2\n"
        "\tpxor    %xmm1, %xmm6\n"
        "\tpxor    %xmm1, %xmm5\n"
        "\tpxor    %xmm1, %xmm3\n"
        "\tpxor    %xmm1, %xmm0\n"
        "\tpxor    %xmm3, %xmm1\n"
        "\tpxor    %xmm0, %xmm7\n"
        "\tpxor    %xmm0, %xmm6\n"
        "\tpxor    %xmm0, %xmm4\n"
        "\tpxor    %xmm0, %xmm1\n"
    );
}

static void gf8mul_11010100(void)
{
    __asm__ __volatile__
    (
        "\tpxor    %xmm7, %xmm6\n"
        "\tpxor    %xmm7, %xmm5\n"
        "\tpxor    %xmm7, %xmm3\n"
        "\tpxor    %xmm7, %xmm0\n"
        "\tpxor    %xmm6, %xmm7\n"
        "\tpxor    %xmm6, %xmm2\n"
        "\tpxor    %xmm6, %xmm0\n"
        "\tpxor    %xmm7, %xmm6\n"
        "\tpxor    %xmm5, %xmm7\n"
        "\tpxor    %xmm5, %xmm4\n"
        "\tpxor    %xmm5, %xmm3\n"
        "\tpxor    %xmm4, %xmm7\n"
        "\tpxor    %xmm4, %xmm5\n"
        "\tpxor    %xmm4, %xmm1\n"
        "\tpxor    %xmm4, %xmm0\n"
        "\tpxor    %xmm5, %xmm4\n"
        "\tpxor    %xmm3, %xmm7\n"
        "\tpxor    %xmm3, %xmm6\n"
        "\tpxor    %xmm3, %xmm5\n"
        "\tpxor    %xmm3, %xmm4\n"
        "\tpxor    %xmm3, %xmm2\n"
        "\tpxor    %xmm3, %xmm1\n"
        "\tpxor    %xmm3, %xmm0\n"
        "\tpxor    %xmm2, %xmm6\n"
        "\tpxor    %xmm2, %xmm5\n"
        "\tpxor    %xmm2, %xmm4\n"
        "\tpxor    %xmm1, %xmm7\n"
        "\tpxor    %xmm1, %xmm5\n"
        "\tpxor    %xmm1, %xmm4\n"
        "\tpxor    %xmm1, %xmm2\n"
        "\tpxor    %xmm2, %xmm1\n"
        "\tpxor    %xmm0, %xmm7\n"
        "\tpxor    %xmm0, %xmm6\n"
        "\tpxor    %xmm0, %xmm4\n"
        "\tpxor    %xmm0, %xmm2\n"
        "\tpxor    %xmm2, %xmm0\n"
    );
}

static void gf8mul_11010101(void)
{
    __asm__ __volatile__
    (
        "\tpxor    %xmm7, %xmm6\n"
        "\tpxor    %xmm7, %xmm5\n"
        "\tpxor    %xmm7, %xmm4\n"
        "\tpxor    %xmm7, %xmm3\n"
        "\tpxor    %xmm7, %xmm0\n"
        "\tpxor    %xmm6, %xmm7\n"
        "\tpxor    %xmm6, %xmm4\n"
        "\tpxor    %xmm6, %xmm3\n"
        "\tpxor    %xmm6, %xmm2\n"
        "\tpxor    %xmm6, %xmm1\n"
        "\tpxor    %xmm7, %xmm6\n"
        "\tpxor    %xmm5, %xmm6\n"
        "\tpxor    %xmm5, %xmm3\n"
        "\tpxor    %xmm5, %xmm2\n"
        "\tpxor    %xmm5, %xmm1\n"
        "\tpxor    %xmm5, %xmm0\n"
        "\tpxor    %xmm6, %xmm5\n"
        "\tpxor    %xmm4, %xmm7\n"
        "\tpxor    %xmm4, %xmm3\n"
        "\tpxor    %xmm4, %xmm1\n"
        "\tpxor    %xmm7, %xmm4\n"
        "\tpxor    %xmm3, %xmm6\n"
        "\tpxor    %xmm3, %xmm2\n"
        "\tpxor    %xmm3, %xmm0\n"
        "\tpxor    %xmm6, %xmm3\n"
        "\tpxor    %xmm2, %xmm7\n"
        "\tpxor    %xmm2, %xmm6\n"
        "\tpxor    %xmm2, %xmm1\n"
        "\tpxor    %xmm1, %xmm6\n"
        "\tpxor    %xmm1, %xmm5\n"
        "\tpxor    %xmm1, %xmm0\n"
        "\tpxor    %xmm0, %xmm7\n"
        "\tpxor    %xmm0, %xmm6\n"
        "\tpxor    %xmm0, %xmm4\n"
        "\tpxor    %xmm0, %xmm2\n"
    );
}

static void gf8mul_11010110(void)
{
    __asm__ __volatile__
    (
        "\tpxor    %xmm7, %xmm4\n"
        "\tpxor    %xmm6, %xmm3\n"
        "\tpxor    %xmm5, %xmm2\n"
        "\tpxor    %xmm4, %xmm1\n"
        "\tpxor    %xmm3, %xmm0\n"
        "\tpxor    %xmm2, %xmm6\n"
        "\tpxor    %xmm2, %xmm5\n"
        "\tpxor    %xmm2, %xmm3\n"
        "\tpxor    %xmm2, %xmm1\n"
        "\tpxor    %xmm1, %xmm5\n"
        "\tpxor    %xmm1, %xmm2\n"
        "\tpxor    %xmm2, %xmm1\n"
        "\tpxor    %xmm0, %xmm7\n"
        "\tpxor    %xmm0, %xmm4\n"
        "\tpxor    %xmm0, %xmm3\n"
        "\tpxor    %xmm0, %xmm2\n"
        "\tpxor    %xmm2, %xmm0\n"
        "\tpxor    %xmm7, %xmm6\n"
        "\tpxor    %xmm7, %xmm3\n"
        "\tpxor    %xmm7, %xmm1\n"
        "\tpxor    %xmm7, %xmm0\n"
        "\tpxor    %xmm6, %xmm7\n"
        "\tpxor    %xmm6, %xmm5\n"
        "\tpxor    %xmm6, %xmm4\n"
        "\tpxor    %xmm6, %xmm3\n"
        "\tpxor    %xmm6, %xmm0\n"
        "\tpxor    %xmm7, %xmm6\n"
        "\tpxor    %xmm5, %xmm7\n"
        "\tpxor    %xmm4, %xmm6\n"
        "\tpxor    %xmm3, %xmm5\n"
        "\tpxor    %xmm2, %xmm4\n"
        "\tpxor    %xmm1, %xmm3\n"
        "\tpxor    %xmm0, %xmm2\n"
    );
}

static void gf8mul_11010111(void)
{
    __asm__ __volatile__
    (
        "\tpxor    %xmm7, %xmm6\n"
        "\tpxor    %xmm7, %xmm5\n"
        "\tpxor    %xmm7, %xmm4\n"
        "\tpxor    %xmm7, %xmm3\n"
        "\tpxor    %xmm6, %xmm7\n"
        "\tpxor    %xmm6, %xmm4\n"
        "\tpxor    %xmm6, %xmm3\n"
        "\tpxor    %xmm6, %xmm1\n"
        "\tpxor    %xmm6, %xmm0\n"
        "\tpxor    %xmm7, %xmm6\n"
        "\tpxor    %xmm5, %xmm7\n"
        "\tpxor    %xmm5, %xmm6\n"
        "\tpxor    %xmm5, %xmm2\n"
        "\tpxor    %xmm5, %xmm1\n"
        "\tpxor    %xmm5, %xmm0\n"
        "\tpxor    %xmm4, %xmm7\n"
        "\tpxor    %xmm4, %xmm6\n"
        "\tpxor    %xmm4, %xmm3\n"
        "\tpxor    %xmm4, %xmm0\n"
        "\tpxor    %xmm3, %xmm5\n"
        "\tpxor    %xmm3, %xmm4\n"
        "\tpxor    %xmm3, %xmm2\n"
        "\tpxor    %xmm3, %xmm1\n"
        "\tpxor    %xmm2, %xmm7\n"
        "\tpxor    %xmm2, %xmm6\n"
        "\tpxor    %xmm2, %xmm3\n"
        "\tpxor    %xmm2, %xmm1\n"
        "\tpxor    %xmm3, %xmm2\n"
        "\tpxor    %xmm1, %xmm6\n"
        "\tpxor    %xmm1, %xmm5\n"
        "\tpxor    %xmm1, %xmm2\n"
        "\tpxor    %xmm1, %xmm0\n"
        "\tpxor    %xmm2, %xmm1\n"
        "\tpxor    %xmm0, %xmm7\n"
        "\tpxor    %xmm0, %xmm6\n"
        "\tpxor    %xmm0, %xmm4\n"
        "\tpxor    %xmm0, %xmm2\n"
        "\tpxor    %xmm0, %xmm1\n"
    );
}

static void gf8mul_11011000(void)
{
    __asm__ __volatile__
    (
        "\tpxor    %xmm7, %xmm6\n"
        "\tpxor    %xmm7, %xmm5\n"
        "\tpxor    %xmm7, %xmm3\n"
        "\tpxor    %xmm6, %xmm5\n"
        "\tpxor    %xmm6, %xmm4\n"
        "\tpxor    %xmm6, %xmm2\n"
        "\tpxor    %xmm5, %xmm4\n"
        "\tpxor    %xmm5, %xmm3\n"
        "\tpxor    %xmm5, %xmm1\n"
        "\tpxor    %xmm4, %xmm3\n"
        "\tpxor    %xmm4, %xmm2\n"
        "\tpxor    %xmm4, %xmm0\n"
        "\tpxor    %xmm3, %xmm7\n"
        "\tpxor    %xmm3, %xmm6\n"
        "\tpxor    %xmm3, %xmm2\n"
        "\tpxor    %xmm2, %xmm7\n"
        "\tpxor    %xmm2, %xmm4\n"
        "\tpxor    %xmm2, %xmm3\n"
        "\tpxor    %xmm2, %xmm0\n"
        "\tpxor    %xmm1, %xmm7\n"
        "\tpxor    %xmm1, %xmm5\n"
        "\tpxor    %xmm1, %xmm3\n"
        "\tpxor    %xmm1, %xmm2\n"
        "\tpxor    %xmm2, %xmm1\n"
        "\tpxor    %xmm0, %xmm7\n"
        "\tpxor    %xmm0, %xmm6\n"
        "\tpxor    %xmm0, %xmm4\n"
        "\tpxor    %xmm0, %xmm3\n"
        "\tpxor    %xmm3, %xmm0\n"
    );
}

static void gf8mul_11011001(void)
{
    __asm__ __volatile__
    (
        "\tpxor    %xmm7, %xmm6\n"
        "\tpxor    %xmm7, %xmm5\n"
        "\tpxor    %xmm7, %xmm4\n"
        "\tpxor    %xmm7, %xmm3\n"
        "\tpxor    %xmm7, %xmm0\n"
        "\tpxor    %xmm6, %xmm7\n"
        "\tpxor    %xmm6, %xmm5\n"
        "\tpxor    %xmm6, %xmm3\n"
        "\tpxor    %xmm5, %xmm7\n"
        "\tpxor    %xmm5, %xmm4\n"
        "\tpxor    %xmm5, %xmm3\n"
        "\tpxor    %xmm5, %xmm2\n"
        "\tpxor    %xmm5, %xmm1\n"
        "\tpxor    %xmm5, %xmm0\n"
        "\tpxor    %xmm7, %xmm5\n"
        "\tpxor    %xmm4, %xmm5\n"
        "\tpxor    %xmm4, %xmm3\n"
        "\tpxor    %xmm4, %xmm1\n"
        "\tpxor    %xmm3, %xmm7\n"
        "\tpxor    %xmm3, %xmm6\n"
        "\tpxor    %xmm3, %xmm5\n"
        "\tpxor    %xmm3, %xmm4\n"
        "\tpxor    %xmm3, %xmm1\n"
        "\tpxor    %xmm3, %xmm0\n"
        "\tpxor    %xmm4, %xmm3\n"
        "\tpxor    %xmm2, %xmm7\n"
        "\tpxor    %xmm2, %xmm6\n"
        "\tpxor    %xmm2, %xmm5\n"
        "\tpxor    %xmm2, %xmm3\n"
        "\tpxor    %xmm2, %xmm1\n"
        "\tpxor    %xmm1, %xmm6\n"
        "\tpxor    %xmm1, %xmm5\n"
        "\tpxor    %xmm1, %xmm4\n"
        "\tpxor    %xmm1, %xmm2\n"
        "\tpxor    %xmm1, %xmm0\n"
        "\tpxor    %xmm0, %xmm7\n"
        "\tpxor    %xmm0, %xmm6\n"
        "\tpxor    %xmm0, %xmm4\n"
        "\tpxor    %xmm0, %xmm3\n"
    );
}

static void gf8mul_11011010(void)
{
    __asm__ __volatile__
    (
        "\tpxor    %xmm7, %xmm2\n"
        "\tpxor    %xmm7, %xmm0\n"
        "\tpxor    %xmm6, %xmm7\n"
        "\tpxor    %xmm6, %xmm3\n"
        "\tpxor    %xmm6, %xmm0\n"
        "\tpxor    %xmm5, %xmm7\n"
        "\tpxor    %xmm5, %xmm6\n"
        "\tpxor    %xmm5, %xmm3\n"
        "\tpxor    %xmm5, %xmm2\n"
        "\tpxor    %xmm5, %xmm1\n"
        "\tpxor    %xmm5, %xmm0\n"
        "\tpxor    %xmm4, %xmm7\n"
        "\tpxor    %xmm4, %xmm6\n"
        "\tpxor    %xmm4, %xmm5\n"
        "\tpxor    %xmm4, %xmm3\n"
        "\tpxor    %xmm4, %xmm2\n"
        "\tpxor    %xmm3, %xmm6\n"
        "\tpxor    %xmm3, %xmm5\n"
        "\tpxor    %xmm3, %xmm4\n"
        "\tpxor    %xmm3, %xmm2\n"
        "\tpxor    %xmm3, %xmm1\n"
        "\tpxor    %xmm2, %xmm5\n"
        "\tpxor    %xmm2, %xmm4\n"
        "\tpxor    %xmm2, %xmm3\n"
        "\tpxor    %xmm2, %xmm1\n"
        "\tpxor    %xmm2, %xmm0\n"
        "\tpxor    %xmm1, %xmm6\n"
        "\tpxor    %xmm1, %xmm5\n"
        "\tpxor    %xmm1, %xmm4\n"
        "\tpxor    %xmm1, %xmm0\n"
        "\tpxor    %xmm0, %xmm7\n"
        "\tpxor    %xmm0, %xmm6\n"
        "\tpxor    %xmm0, %xmm4\n"
        "\tpxor    %xmm0, %xmm3\n"
        "\tpxor    %xmm0, %xmm1\n"
        "\tpxor    %xmm1, %xmm0\n"
    );
}

static void gf8mul_11011011(void)
{
    __asm__ __volatile__
    (
        "\tpxor    %xmm7, %xmm6\n"
        "\tpxor    %xmm7, %xmm4\n"
        "\tpxor    %xmm7, %xmm2\n"
        "\tpxor    %xmm7, %xmm1\n"
        "\tpxor    %xmm6, %xmm5\n"
        "\tpxor    %xmm6, %xmm3\n"
        "\tpxor    %xmm6, %xmm1\n"
        "\tpxor    %xmm6, %xmm0\n"
        "\tpxor    %xmm5, %xmm6\n"
        "\tpxor    %xmm5, %xmm3\n"
        "\tpxor    %xmm5, %xmm2\n"
        "\tpxor    %xmm5, %xmm0\n"
        "\tpxor    %xmm4, %xmm7\n"
        "\tpxor    %xmm4, %xmm1\n"
        "\tpxor    %xmm3, %xmm6\n"
        "\tpxor    %xmm3, %xmm5\n"
        "\tpxor    %xmm3, %xmm4\n"
        "\tpxor    %xmm3, %xmm2\n"
        "\tpxor    %xmm3, %xmm1\n"
        "\tpxor    %xmm4, %xmm3\n"
        "\tpxor    %xmm2, %xmm7\n"
        "\tpxor    %xmm2, %xmm6\n"
        "\tpxor    %xmm2, %xmm5\n"
        "\tpxor    %xmm2, %xmm1\n"
        "\tpxor    %xmm5, %xmm2\n"
        "\tpxor    %xmm1, %xmm6\n"
        "\tpxor    %xmm1, %xmm5\n"
        "\tpxor    %xmm1, %xmm4\n"
        "\tpxor    %xmm1, %xmm0\n"
        "\tpxor    %xmm4, %xmm1\n"
        "\tpxor    %xmm0, %xmm7\n"
        "\tpxor    %xmm0, %xmm6\n"
        "\tpxor    %xmm0, %xmm4\n"
        "\tpxor    %xmm0, %xmm3\n"
        "\tpxor    %xmm0, %xmm1\n"
    );
}

static void gf8mul_11011100(void)
{
    __asm__ __volatile__
    (
        "\tpxor    %xmm7, %xmm5\n"
        "\tpxor    %xmm7, %xmm4\n"
        "\tpxor    %xmm7, %xmm1\n"
        "\tpxor    %xmm7, %xmm0\n"
        "\tpxor    %xmm6, %xmm1\n"
        "\tpxor    %xmm6, %xmm0\n"
        "\tpxor    %xmm5, %xmm7\n"
        "\tpxor    %xmm5, %xmm6\n"
        "\tpxor    %xmm5, %xmm4\n"
        "\tpxor    %xmm5, %xmm3\n"
        "\tpxor    %xmm5, %xmm1\n"
        "\tpxor    %xmm6, %xmm5\n"
        "\tpxor    %xmm4, %xmm7\n"
        "\tpxor    %xmm4, %xmm5\n"
        "\tpxor    %xmm4, %xmm3\n"
        "\tpxor    %xmm4, %xmm2\n"
        "\tpxor    %xmm3, %xmm7\n"
        "\tpxor    %xmm3, %xmm2\n"
        "\tpxor    %xmm3, %xmm1\n"
        "\tpxor    %xmm3, %xmm0\n"
        "\tpxor    %xmm7, %xmm3\n"
        "\tpxor    %xmm2, %xmm6\n"
        "\tpxor    %xmm2, %xmm4\n"
        "\tpxor    %xmm1, %xmm7\n"
        "\tpxor    %xmm1, %xmm5\n"
        "\tpxor    %xmm1, %xmm2\n"
        "\tpxor    %xmm2, %xmm1\n"
        "\tpxor    %xmm0, %xmm7\n"
        "\tpxor    %xmm0, %xmm6\n"
        "\tpxor    %xmm0, %xmm4\n"
        "\tpxor    %xmm0, %xmm3\n"
        "\tpxor    %xmm0, %xmm2\n"
        "\tpxor    %xmm2, %xmm0\n"
    );
}

static void gf8mul_11011101(void)
{
    __asm__ __volatile__
    (
        "\tpxor    %xmm7, %xmm6\n"
        "\tpxor    %xmm7, %xmm4\n"
        "\tpxor    %xmm7, %xmm1\n"
        "\tpxor    %xmm7, %xmm0\n"
        "\tpxor    %xmm6, %xmm7\n"
        "\tpxor    %xmm6, %xmm5\n"
        "\tpxor    %xmm6, %xmm3\n"
        "\tpxor    %xmm6, %xmm0\n"
        "\tpxor    %xmm7, %xmm6\n"
        "\tpxor    %xmm5, %xmm7\n"
        "\tpxor    %xmm5, %xmm4\n"
        "\tpxor    %xmm5, %xmm2\n"
        "\tpxor    %xmm4, %xmm6\n"
        "\tpxor    %xmm4, %xmm3\n"
        "\tpxor    %xmm4, %xmm1\n"
        "\tpxor    %xmm3, %xmm5\n"
        "\tpxor    %xmm3, %xmm2\n"
        "\tpxor    %xmm3, %xmm0\n"
        "\tpxor    %xmm2, %xmm7\n"
        "\tpxor    %xmm2, %xmm6\n"
        "\tpxor    %xmm2, %xmm5\n"
        "\tpxor    %xmm2, %xmm4\n"
        "\tpxor    %xmm2, %xmm1\n"
        "\tpxor    %xmm1, %xmm6\n"
        "\tpxor    %xmm1, %xmm5\n"
        "\tpxor    %xmm1, %xmm4\n"
        "\tpxor    %xmm1, %xmm3\n"
        "\tpxor    %xmm1, %xmm0\n"
        "\tpxor    %xmm0, %xmm7\n"
        "\tpxor    %xmm0, %xmm6\n"
        "\tpxor    %xmm0, %xmm4\n"
        "\tpxor    %xmm0, %xmm3\n"
        "\tpxor    %xmm0, %xmm2\n"
    );
}

static void gf8mul_11011110(void)
{
    __asm__ __volatile__
    (
        "\tpxor    %xmm7, %xmm4\n"
        "\tpxor    %xmm7, %xmm0\n"
        "\tpxor    %xmm6, %xmm7\n"
        "\tpxor    %xmm6, %xmm4\n"
        "\tpxor    %xmm6, %xmm2\n"
        "\tpxor    %xmm6, %xmm1\n"
        "\tpxor    %xmm6, %xmm0\n"
        "\tpxor    %xmm5, %xmm7\n"
        "\tpxor    %xmm5, %xmm6\n"
        "\tpxor    %xmm5, %xmm4\n"
        "\tpxor    %xmm5, %xmm2\n"
        "\tpxor    %xmm4, %xmm7\n"
        "\tpxor    %xmm4, %xmm3\n"
        "\tpxor    %xmm4, %xmm2\n"
        "\tpxor    %xmm7, %xmm4\n"
        "\tpxor    %xmm3, %xmm6\n"
        "\tpxor    %xmm3, %xmm2\n"
        "\tpxor    %xmm3, %xmm1\n"
        "\tpxor    %xmm6, %xmm3\n"
        "\tpxor    %xmm2, %xmm5\n"
        "\tpxor    %xmm2, %xmm1\n"
        "\tpxor    %xmm2, %xmm0\n"
        "\tpxor    %xmm5, %xmm2\n"
        "\tpxor    %xmm1, %xmm6\n"
        "\tpxor    %xmm1, %xmm5\n"
        "\tpxor    %xmm1, %xmm4\n"
        "\tpxor    %xmm1, %xmm3\n"
        "\tpxor    %xmm1, %xmm2\n"
        "\tpxor    %xmm1, %xmm0\n"
        "\tpxor    %xmm0, %xmm7\n"
        "\tpxor    %xmm0, %xmm6\n"
        "\tpxor    %xmm0, %xmm4\n"
        "\tpxor    %xmm0, %xmm3\n"
        "\tpxor    %xmm0, %xmm2\n"
        "\tpxor    %xmm0, %xmm1\n"
        "\tpxor    %xmm1, %xmm0\n"
    );
}

static void gf8mul_11011111(void)
{
    __asm__ __volatile__
    (
        "\tpxor    %xmm7, %xmm6\n"
        "\tpxor    %xmm7, %xmm3\n"
        "\tpxor    %xmm6, %xmm7\n"
        "\tpxor    %xmm6, %xmm5\n"
        "\tpxor    %xmm6, %xmm4\n"
        "\tpxor    %xmm6, %xmm3\n"
        "\tpxor    %xmm6, %xmm1\n"
        "\tpxor    %xmm6, %xmm0\n"
        "\tpxor    %xmm7, %xmm6\n"
        "\tpxor    %xmm5, %xmm4\n"
        "\tpxor    %xmm5, %xmm1\n"
        "\tpxor    %xmm4, %xmm7\n"
        "\tpxor    %xmm4, %xmm6\n"
        "\tpxor    %xmm4, %xmm5\n"
        "\tpxor    %xmm4, %xmm3\n"
        "\tpxor    %xmm4, %xmm2\n"
        "\tpxor    %xmm4, %xmm1\n"
        "\tpxor    %xmm4, %xmm0\n"
        "\tpxor    %xmm5, %xmm4\n"
        "\tpxor    %xmm3, %xmm6\n"
        "\tpxor    %xmm3, %xmm2\n"
        "\tpxor    %xmm3, %xmm1\n"
        "\tpxor    %xmm2, %xmm7\n"
        "\tpxor    %xmm2, %xmm6\n"
        "\tpxor    %xmm2, %xmm5\n"
        "\tpxor    %xmm2, %xmm4\n"
        "\tpxor    %xmm2, %xmm3\n"
        "\tpxor    %xmm2, %xmm1\n"
        "\tpxor    %xmm3, %xmm2\n"
        "\tpxor    %xmm1, %xmm6\n"
        "\tpxor    %xmm1, %xmm5\n"
        "\tpxor    %xmm1, %xmm4\n"
        "\tpxor    %xmm1, %xmm3\n"
        "\tpxor    %xmm1, %xmm2\n"
        "\tpxor    %xmm1, %xmm0\n"
        "\tpxor    %xmm2, %xmm1\n"
        "\tpxor    %xmm0, %xmm7\n"
        "\tpxor    %xmm0, %xmm6\n"
        "\tpxor    %xmm0, %xmm4\n"
        "\tpxor    %xmm0, %xmm3\n"
        "\tpxor    %xmm0, %xmm2\n"
        "\tpxor    %xmm0, %xmm1\n"
    );
}

static void gf8mul_11100000(void)
{
    __asm__ __volatile__
    (
        "\tpxor    %xmm7, %xmm6\n"
        "\tpxor    %xmm7, %xmm4\n"
        "\tpxor    %xmm7, %xmm1\n"
        "\tpxor    %xmm6, %xmm5\n"
        "\tpxor    %xmm6, %xmm3\n"
        "\tpxor    %xmm6, %xmm0\n"
        "\tpxor    %xmm5, %xmm7\n"
        "\tpxor    %xmm5, %xmm0\n"
        "\tpxor    %xmm4, %xmm7\n"
        "\tpxor    %xmm4, %xmm6\n"
        "\tpxor    %xmm4, %xmm5\n"
        "\tpxor    %xmm4, %xmm2\n"
        "\tpxor    %xmm4, %xmm0\n"
        "\tpxor    %xmm5, %xmm4\n"
        "\tpxor    %xmm3, %xmm5\n"
        "\tpxor    %xmm3, %xmm2\n"
        "\tpxor    %xmm3, %xmm1\n"
        "\tpxor    %xmm2, %xmm7\n"
        "\tpxor    %xmm2, %xmm5\n"
        "\tpxor    %xmm1, %xmm5\n"
        "\tpxor    %xmm1, %xmm4\n"
        "\tpxor    %xmm1, %xmm3\n"
        "\tpxor    %xmm1, %xmm2\n"
        "\tpxor    %xmm1, %xmm0\n"
        "\tpxor    %xmm2, %xmm1\n"
        "\tpxor    %xmm0, %xmm7\n"
        "\tpxor    %xmm0, %xmm6\n"
        "\tpxor    %xmm0, %xmm5\n"
        "\tpxor    %xmm5, %xmm0\n"
    );
}

static void gf8mul_11100001(void)
{
    __asm__ __volatile__
    (
        "\tpxor    %xmm7, %xmm6\n"
        "\tpxor    %xmm6, %xmm5\n"
        "\tpxor    %xmm5, %xmm4\n"
        "\tpxor    %xmm4, %xmm3\n"
        "\tpxor    %xmm3, %xmm2\n"
        "\tpxor    %xmm2, %xmm1\n"
        "\tpxor    %xmm1, %xmm0\n"
        "\tpxor    %xmm0, %xmm7\n"
        "\tpxor    %xmm0, %xmm6\n"
        "\tpxor    %xmm0, %xmm5\n"
        "\tpxor    %xmm0, %xmm4\n"
        "\tpxor    %xmm0, %xmm2\n"
        "\tpxor    %xmm7, %xmm5\n"
        "\tpxor    %xmm7, %xmm2\n"
        "\tpxor    %xmm6, %xmm4\n"
        "\tpxor    %xmm6, %xmm1\n"
        "\tpxor    %xmm5, %xmm3\n"
        "\tpxor    %xmm5, %xmm0\n"
        "\tpxor    %xmm4, %xmm7\n"
        "\tpxor    %xmm4, %xmm5\n"
        "\tpxor    %xmm4, %xmm2\n"
        "\tpxor    %xmm4, %xmm1\n"
        "\tpxor    %xmm4, %xmm0\n"
        "\tpxor    %xmm3, %xmm7\n"
        "\tpxor    %xmm3, %xmm6\n"
        "\tpxor    %xmm3, %xmm5\n"
        "\tpxor    %xmm3, %xmm4\n"
        "\tpxor    %xmm2, %xmm6\n"
        "\tpxor    %xmm2, %xmm5\n"
        "\tpxor    %xmm2, %xmm4\n"
        "\tpxor    %xmm2, %xmm3\n"
        "\tpxor    %xmm1, %xmm5\n"
        "\tpxor    %xmm1, %xmm4\n"
        "\tpxor    %xmm1, %xmm3\n"
        "\tpxor    %xmm1, %xmm2\n"
        "\tpxor    %xmm0, %xmm4\n"
        "\tpxor    %xmm0, %xmm3\n"
        "\tpxor    %xmm0, %xmm2\n"
        "\tpxor    %xmm0, %xmm1\n"
    );
}

static void gf8mul_11100010(void)
{
    __asm__ __volatile__
    (
        "\tpxor    %xmm7, %xmm1\n"
        "\tpxor    %xmm6, %xmm0\n"
        "\tpxor    %xmm5, %xmm7\n"
        "\tpxor    %xmm5, %xmm3\n"
        "\tpxor    %xmm5, %xmm2\n"
        "\tpxor    %xmm4, %xmm6\n"
        "\tpxor    %xmm4, %xmm2\n"
        "\tpxor    %xmm4, %xmm1\n"
        "\tpxor    %xmm3, %xmm5\n"
        "\tpxor    %xmm3, %xmm1\n"
        "\tpxor    %xmm3, %xmm0\n"
        "\tpxor    %xmm2, %xmm7\n"
        "\tpxor    %xmm2, %xmm1\n"
        "\tpxor    %xmm2, %xmm0\n"
        "\tpxor    %xmm1, %xmm7\n"
        "\tpxor    %xmm1, %xmm5\n"
        "\tpxor    %xmm1, %xmm4\n"
        "\tpxor    %xmm1, %xmm3\n"
        "\tpxor    %xmm1, %xmm2\n"
        "\tpxor    %xmm2, %xmm1\n"
        "\tpxor    %xmm0, %xmm6\n"
        "\tpxor    %xmm0, %xmm4\n"
        "\tpxor    %xmm0, %xmm3\n"
        "\tpxor    %xmm0, %xmm2\n"
        "\tpxor    %xmm0, %xmm1\n"
        "\tpxor    %xmm1, %xmm0\n"
        "\tpxor    %xmm7, %xmm3\n"
        "\tpxor    %xmm7, %xmm1\n"
        "\tpxor    %xmm7, %xmm0\n"
        "\tpxor    %xmm6, %xmm7\n"
        "\tpxor    %xmm5, %xmm6\n"
        "\tpxor    %xmm4, %xmm5\n"
        "\tpxor    %xmm3, %xmm4\n"
        "\tpxor    %xmm2, %xmm3\n"
        "\tpxor    %xmm1, %xmm2\n"
        "\tpxor    %xmm0, %xmm1\n"
    );
}

static void gf8mul_11100011(void)
{
    __asm__ __volatile__
    (
        "\tpxor    %xmm7, %xmm5\n"
        "\tpxor    %xmm7, %xmm4\n"
        "\tpxor    %xmm7, %xmm0\n"
        "\tpxor    %xmm6, %xmm7\n"
        "\tpxor    %xmm6, %xmm5\n"
        "\tpxor    %xmm6, %xmm2\n"
        "\tpxor    %xmm6, %xmm1\n"
        "\tpxor    %xmm6, %xmm0\n"
        "\tpxor    %xmm5, %xmm7\n"
        "\tpxor    %xmm5, %xmm6\n"
        "\tpxor    %xmm5, %xmm1\n"
        "\tpxor    %xmm4, %xmm5\n"
        "\tpxor    %xmm4, %xmm3\n"
        "\tpxor    %xmm4, %xmm0\n"
        "\tpxor    %xmm3, %xmm7\n"
        "\tpxor    %xmm3, %xmm6\n"
        "\tpxor    %xmm3, %xmm5\n"
        "\tpxor    %xmm3, %xmm2\n"
        "\tpxor    %xmm5, %xmm3\n"
        "\tpxor    %xmm2, %xmm6\n"
        "\tpxor    %xmm2, %xmm5\n"
        "\tpxor    %xmm2, %xmm4\n"
        "\tpxor    %xmm2, %xmm1\n"
        "\tpxor    %xmm4, %xmm2\n"
        "\tpxor    %xmm1, %xmm5\n"
        "\tpxor    %xmm1, %xmm4\n"
        "\tpxor    %xmm1, %xmm3\n"
        "\tpxor    %xmm1, %xmm0\n"
        "\tpxor    %xmm3, %xmm1\n"
        "\tpxor    %xmm0, %xmm7\n"
        "\tpxor    %xmm0, %xmm6\n"
        "\tpxor    %xmm0, %xmm5\n"
        "\tpxor    %xmm0, %xmm1\n"
    );
}

static void gf8mul_11100100(void)
{
    __asm__ __volatile__
    (
        "\tpxor    %xmm7, %xmm6\n"
        "\tpxor    %xmm7, %xmm4\n"
        "\tpxor    %xmm6, %xmm5\n"
        "\tpxor    %xmm6, %xmm3\n"
        "\tpxor    %xmm5, %xmm4\n"
        "\tpxor    %xmm5, %xmm2\n"
        "\tpxor    %xmm4, %xmm3\n"
        "\tpxor    %xmm4, %xmm1\n"
        "\tpxor    %xmm3, %xmm2\n"
        "\tpxor    %xmm3, %xmm0\n"
        "\tpxor    %xmm2, %xmm7\n"
        "\tpxor    %xmm2, %xmm6\n"
        "\tpxor    %xmm2, %xmm5\n"
        "\tpxor    %xmm1, %xmm6\n"
        "\tpxor    %xmm1, %xmm5\n"
        "\tpxor    %xmm1, %xmm4\n"
        "\tpxor    %xmm0, %xmm5\n"
        "\tpxor    %xmm0, %xmm4\n"
        "\tpxor    %xmm0, %xmm3\n"
        "\tpxor    %xmm7, %xmm5\n"
        "\tpxor    %xmm7, %xmm2\n"
        "\tpxor    %xmm6, %xmm4\n"
        "\tpxor    %xmm6, %xmm1\n"
        "\tpxor    %xmm5, %xmm7\n"
        "\tpxor    %xmm7, %xmm5\n"
        "\tpxor    %xmm4, %xmm6\n"
        "\tpxor    %xmm6, %xmm4\n"
        "\tpxor    %xmm3, %xmm5\n"
        "\tpxor    %xmm5, %xmm3\n"
        "\tpxor    %xmm2, %xmm4\n"
        "\tpxor    %xmm4, %xmm2\n"
        "\tpxor    %xmm1, %xmm3\n"
        "\tpxor    %xmm3, %xmm1\n"
        "\tpxor    %xmm0, %xmm2\n"
        "\tpxor    %xmm2, %xmm0\n"
    );
}

static void gf8mul_11100101(void)
{
    __asm__ __volatile__
    (
        "\tpxor    %xmm7, %xmm5\n"
        "\tpxor    %xmm7, %xmm4\n"
        "\tpxor    %xmm7, %xmm2\n"
        "\tpxor    %xmm7, %xmm1\n"
        "\tpxor    %xmm6, %xmm4\n"
        "\tpxor    %xmm6, %xmm3\n"
        "\tpxor    %xmm6, %xmm1\n"
        "\tpxor    %xmm6, %xmm0\n"
        "\tpxor    %xmm5, %xmm6\n"
        "\tpxor    %xmm5, %xmm2\n"
        "\tpxor    %xmm5, %xmm1\n"
        "\tpxor    %xmm5, %xmm0\n"
        "\tpxor    %xmm4, %xmm7\n"
        "\tpxor    %xmm4, %xmm5\n"
        "\tpxor    %xmm4, %xmm2\n"
        "\tpxor    %xmm4, %xmm0\n"
        "\tpxor    %xmm5, %xmm4\n"
        "\tpxor    %xmm3, %xmm7\n"
        "\tpxor    %xmm3, %xmm6\n"
        "\tpxor    %xmm3, %xmm2\n"
        "\tpxor    %xmm2, %xmm6\n"
        "\tpxor    %xmm2, %xmm5\n"
        "\tpxor    %xmm2, %xmm1\n"
        "\tpxor    %xmm1, %xmm5\n"
        "\tpxor    %xmm1, %xmm4\n"
        "\tpxor    %xmm1, %xmm0\n"
        "\tpxor    %xmm0, %xmm7\n"
        "\tpxor    %xmm0, %xmm6\n"
        "\tpxor    %xmm0, %xmm5\n"
        "\tpxor    %xmm0, %xmm2\n"
    );
}

static void gf8mul_11100110(void)
{
    __asm__ __volatile__
    (
        "\tpxor    %xmm7, %xmm6\n"
        "\tpxor    %xmm7, %xmm5\n"
        "\tpxor    %xmm7, %xmm4\n"
        "\tpxor    %xmm7, %xmm0\n"
        "\tpxor    %xmm6, %xmm7\n"
        "\tpxor    %xmm6, %xmm2\n"
        "\tpxor    %xmm7, %xmm6\n"
        "\tpxor    %xmm5, %xmm6\n"
        "\tpxor    %xmm5, %xmm1\n"
        "\tpxor    %xmm6, %xmm5\n"
        "\tpxor    %xmm4, %xmm5\n"
        "\tpxor    %xmm4, %xmm0\n"
        "\tpxor    %xmm5, %xmm4\n"
        "\tpxor    %xmm3, %xmm7\n"
        "\tpxor    %xmm3, %xmm6\n"
        "\tpxor    %xmm3, %xmm4\n"
        "\tpxor    %xmm3, %xmm2\n"
        "\tpxor    %xmm2, %xmm7\n"
        "\tpxor    %xmm2, %xmm3\n"
        "\tpxor    %xmm2, %xmm1\n"
        "\tpxor    %xmm2, %xmm0\n"
        "\tpxor    %xmm3, %xmm2\n"
        "\tpxor    %xmm1, %xmm5\n"
        "\tpxor    %xmm1, %xmm4\n"
        "\tpxor    %xmm1, %xmm2\n"
        "\tpxor    %xmm1, %xmm0\n"
        "\tpxor    %xmm0, %xmm7\n"
        "\tpxor    %xmm0, %xmm6\n"
        "\tpxor    %xmm0, %xmm5\n"
        "\tpxor    %xmm0, %xmm2\n"
        "\tpxor    %xmm0, %xmm1\n"
        "\tpxor    %xmm1, %xmm0\n"
    );
}

static void gf8mul_11100111(void)
{
    __asm__ __volatile__
    (
        "\tpxor    %xmm7, %xmm6\n"
        "\tpxor    %xmm7, %xmm5\n"
        "\tpxor    %xmm7, %xmm3\n"
        "\tpxor    %xmm7, %xmm2\n"
        "\tpxor    %xmm6, %xmm7\n"
        "\tpxor    %xmm6, %xmm5\n"
        "\tpxor    %xmm6, %xmm4\n"
        "\tpxor    %xmm6, %xmm3\n"
        "\tpxor    %xmm6, %xmm1\n"
        "\tpxor    %xmm7, %xmm6\n"
        "\tpxor    %xmm5, %xmm7\n"
        "\tpxor    %xmm5, %xmm4\n"
        "\tpxor    %xmm5, %xmm0\n"
        "\tpxor    %xmm7, %xmm5\n"
        "\tpxor    %xmm4, %xmm7\n"
        "\tpxor    %xmm4, %xmm5\n"
        "\tpxor    %xmm4, %xmm2\n"
        "\tpxor    %xmm4, %xmm1\n"
        "\tpxor    %xmm3, %xmm7\n"
        "\tpxor    %xmm3, %xmm6\n"
        "\tpxor    %xmm3, %xmm4\n"
        "\tpxor    %xmm3, %xmm2\n"
        "\tpxor    %xmm4, %xmm3\n"
        "\tpxor    %xmm2, %xmm6\n"
        "\tpxor    %xmm2, %xmm5\n"
        "\tpxor    %xmm2, %xmm3\n"
        "\tpxor    %xmm2, %xmm1\n"
        "\tpxor    %xmm3, %xmm2\n"
        "\tpxor    %xmm1, %xmm5\n"
        "\tpxor    %xmm1, %xmm4\n"
        "\tpxor    %xmm1, %xmm2\n"
        "\tpxor    %xmm1, %xmm0\n"
        "\tpxor    %xmm2, %xmm1\n"
        "\tpxor    %xmm0, %xmm7\n"
        "\tpxor    %xmm0, %xmm6\n"
        "\tpxor    %xmm0, %xmm5\n"
        "\tpxor    %xmm0, %xmm2\n"
        "\tpxor    %xmm0, %xmm1\n"
    );
}

static void gf8mul_11101000(void)
{
    __asm__ __volatile__
    (
        "\tpxor    %xmm7, %xmm6\n"
        "\tpxor    %xmm6, %xmm5\n"
        "\tpxor    %xmm5, %xmm4\n"
        "\tpxor    %xmm4, %xmm3\n"
        "\tpxor    %xmm3, %xmm2\n"
        "\tpxor    %xmm2, %xmm1\n"
        "\tpxor    %xmm1, %xmm0\n"
        "\tpxor    %xmm0, %xmm7\n"
        "\tpxor    %xmm0, %xmm6\n"
        "\tpxor    %xmm0, %xmm5\n"
        "\tpxor    %xmm0, %xmm4\n"
        "\tpxor    %xmm0, %xmm2\n"
        "\tpxor    %xmm7, %xmm4\n"
        "\tpxor    %xmm6, %xmm3\n"
        "\tpxor    %xmm5, %xmm2\n"
        "\tpxor    %xmm4, %xmm1\n"
        "\tpxor    %xmm3, %xmm0\n"
        "\tpxor    %xmm2, %xmm6\n"
        "\tpxor    %xmm2, %xmm5\n"
        "\tpxor    %xmm2, %xmm3\n"
        "\tpxor    %xmm2, %xmm1\n"
        "\tpxor    %xmm1, %xmm5\n"
        "\tpxor    %xmm1, %xmm2\n"
        "\tpxor    %xmm2, %xmm1\n"
        "\tpxor    %xmm0, %xmm7\n"
        "\tpxor    %xmm0, %xmm4\n"
        "\tpxor    %xmm0, %xmm3\n"
        "\tpxor    %xmm0, %xmm2\n"
        "\tpxor    %xmm2, %xmm0\n"
    );
}

static void gf8mul_11101001(void)
{
    __asm__ __volatile__
    (
        "\tpxor    %xmm7, %xmm4\n"
        "\tpxor    %xmm7, %xmm3\n"
        "\tpxor    %xmm6, %xmm3\n"
        "\tpxor    %xmm6, %xmm2\n"
        "\tpxor    %xmm5, %xmm2\n"
        "\tpxor    %xmm5, %xmm1\n"
        "\tpxor    %xmm4, %xmm1\n"
        "\tpxor    %xmm4, %xmm0\n"
        "\tpxor    %xmm3, %xmm7\n"
        "\tpxor    %xmm3, %xmm4\n"
        "\tpxor    %xmm3, %xmm2\n"
        "\tpxor    %xmm2, %xmm6\n"
        "\tpxor    %xmm2, %xmm3\n"
        "\tpxor    %xmm2, %xmm1\n"
        "\tpxor    %xmm1, %xmm5\n"
        "\tpxor    %xmm1, %xmm2\n"
        "\tpxor    %xmm1, %xmm0\n"
        "\tpxor    %xmm0, %xmm7\n"
        "\tpxor    %xmm0, %xmm6\n"
        "\tpxor    %xmm0, %xmm5\n"
        "\tpxor    %xmm0, %xmm3\n"
    );
}

static void gf8mul_11101010(void)
{
    __asm__ __volatile__
    (
        "\tpxor    %xmm7, %xmm6\n"
        "\tpxor    %xmm7, %xmm5\n"
        "\tpxor    %xmm7, %xmm3\n"
        "\tpxor    %xmm7, %xmm2\n"
        "\tpxor    %xmm6, %xmm7\n"
        "\tpxor    %xmm6, %xmm5\n"
        "\tpxor    %xmm6, %xmm4\n"
        "\tpxor    %xmm6, %xmm0\n"
        "\tpxor    %xmm5, %xmm7\n"
        "\tpxor    %xmm5, %xmm3\n"
        "\tpxor    %xmm5, %xmm2\n"
        "\tpxor    %xmm7, %xmm5\n"
        "\tpxor    %xmm4, %xmm6\n"
        "\tpxor    %xmm4, %xmm2\n"
        "\tpxor    %xmm4, %xmm1\n"
        "\tpxor    %xmm6, %xmm4\n"
        "\tpxor    %xmm3, %xmm5\n"
        "\tpxor    %xmm3, %xmm1\n"
        "\tpxor    %xmm3, %xmm0\n"
        "\tpxor    %xmm5, %xmm3\n"
        "\tpxor    %xmm2, %xmm7\n"
        "\tpxor    %xmm2, %xmm5\n"
        "\tpxor    %xmm2, %xmm3\n"
        "\tpxor    %xmm2, %xmm1\n"
        "\tpxor    %xmm2, %xmm0\n"
        "\tpxor    %xmm1, %xmm5\n"
        "\tpxor    %xmm1, %xmm0\n"
        "\tpxor    %xmm0, %xmm7\n"
        "\tpxor    %xmm0, %xmm6\n"
        "\tpxor    %xmm0, %xmm5\n"
        "\tpxor    %xmm0, %xmm3\n"
        "\tpxor    %xmm0, %xmm1\n"
        "\tpxor    %xmm1, %xmm0\n"
    );
}

static void gf8mul_11101011(void)
{
    __asm__ __volatile__
    (
        "\tpxor    %xmm7, %xmm4\n"
        "\tpxor    %xmm7, %xmm3\n"
        "\tpxor    %xmm6, %xmm3\n"
        "\tpxor    %xmm6, %xmm2\n"
        "\tpxor    %xmm5, %xmm2\n"
        "\tpxor    %xmm5, %xmm1\n"
        "\tpxor    %xmm4, %xmm7\n"
        "\tpxor    %xmm4, %xmm2\n"
        "\tpxor    %xmm7, %xmm4\n"
        "\tpxor    %xmm3, %xmm6\n"
        "\tpxor    %xmm3, %xmm1\n"
        "\tpxor    %xmm6, %xmm3\n"
        "\tpxor    %xmm2, %xmm5\n"
        "\tpxor    %xmm2, %xmm0\n"
        "\tpxor    %xmm5, %xmm2\n"
        "\tpxor    %xmm1, %xmm7\n"
        "\tpxor    %xmm1, %xmm5\n"
        "\tpxor    %xmm1, %xmm4\n"
        "\tpxor    %xmm0, %xmm6\n"
        "\tpxor    %xmm0, %xmm4\n"
        "\tpxor    %xmm0, %xmm3\n"
        "\tpxor    %xmm7, %xmm3\n"
        "\tpxor    %xmm7, %xmm1\n"
        "\tpxor    %xmm7, %xmm0\n"
        "\tpxor    %xmm6, %xmm7\n"
        "\tpxor    %xmm5, %xmm6\n"
        "\tpxor    %xmm4, %xmm5\n"
        "\tpxor    %xmm3, %xmm4\n"
        "\tpxor    %xmm2, %xmm3\n"
        "\tpxor    %xmm1, %xmm2\n"
        "\tpxor    %xmm0, %xmm1\n"
    );
}

static void gf8mul_11101100(void)
{
    __asm__ __volatile__
    (
        "\tpxor    %xmm7, %xmm2\n"
        "\tpxor    %xmm6, %xmm1\n"
        "\tpxor    %xmm5, %xmm0\n"
        "\tpxor    %xmm4, %xmm7\n"
        "\tpxor    %xmm4, %xmm3\n"
        "\tpxor    %xmm4, %xmm1\n"
        "\tpxor    %xmm3, %xmm6\n"
        "\tpxor    %xmm3, %xmm2\n"
        "\tpxor    %xmm3, %xmm0\n"
        "\tpxor    %xmm2, %xmm7\n"
        "\tpxor    %xmm2, %xmm4\n"
        "\tpxor    %xmm1, %xmm7\n"
        "\tpxor    %xmm1, %xmm6\n"
        "\tpxor    %xmm1, %xmm2\n"
        "\tpxor    %xmm2, %xmm1\n"
        "\tpxor    %xmm0, %xmm7\n"
        "\tpxor    %xmm0, %xmm6\n"
        "\tpxor    %xmm0, %xmm5\n"
        "\tpxor    %xmm0, %xmm3\n"
        "\tpxor    %xmm0, %xmm2\n"
        "\tpxor    %xmm2, %xmm0\n"
    );
}

static void gf8mul_11101101(void)
{
    __asm__ __volatile__
    (
        "\tpxor    %xmm7, %xmm6\n"
        "\tpxor    %xmm7, %xmm5\n"
        "\tpxor    %xmm7, %xmm2\n"
        "\tpxor    %xmm7, %xmm0\n"
        "\tpxor    %xmm6, %xmm7\n"
        "\tpxor    %xmm6, %xmm3\n"
        "\tpxor    %xmm6, %xmm2\n"
        "\tpxor    %xmm6, %xmm0\n"
        "\tpxor    %xmm7, %xmm6\n"
        "\tpxor    %xmm5, %xmm7\n"
        "\tpxor    %xmm5, %xmm4\n"
        "\tpxor    %xmm5, %xmm1\n"
        "\tpxor    %xmm5, %xmm0\n"
        "\tpxor    %xmm4, %xmm7\n"
        "\tpxor    %xmm4, %xmm5\n"
        "\tpxor    %xmm4, %xmm3\n"
        "\tpxor    %xmm4, %xmm0\n"
        "\tpxor    %xmm5, %xmm4\n"
        "\tpxor    %xmm3, %xmm7\n"
        "\tpxor    %xmm3, %xmm5\n"
        "\tpxor    %xmm3, %xmm2\n"
        "\tpxor    %xmm2, %xmm6\n"
        "\tpxor    %xmm2, %xmm4\n"
        "\tpxor    %xmm2, %xmm1\n"
        "\tpxor    %xmm1, %xmm5\n"
        "\tpxor    %xmm1, %xmm3\n"
        "\tpxor    %xmm1, %xmm0\n"
        "\tpxor    %xmm0, %xmm7\n"
        "\tpxor    %xmm0, %xmm6\n"
        "\tpxor    %xmm0, %xmm5\n"
        "\tpxor    %xmm0, %xmm3\n"
        "\tpxor    %xmm0, %xmm2\n"
    );
}

static void gf8mul_11101110(void)
{
    __asm__ __volatile__
    (
        "\tpxor    %xmm7, %xmm6\n"
        "\tpxor    %xmm7, %xmm5\n"
        "\tpxor    %xmm7, %xmm3\n"
        "\tpxor    %xmm6, %xmm7\n"
        "\tpxor    %xmm6, %xmm5\n"
        "\tpxor    %xmm6, %xmm4\n"
        "\tpxor    %xmm6, %xmm1\n"
        "\tpxor    %xmm6, %xmm0\n"
        "\tpxor    %xmm5, %xmm7\n"
        "\tpxor    %xmm5, %xmm3\n"
        "\tpxor    %xmm5, %xmm1\n"
        "\tpxor    %xmm5, %xmm0\n"
        "\tpxor    %xmm7, %xmm5\n"
        "\tpxor    %xmm4, %xmm3\n"
        "\tpxor    %xmm4, %xmm2\n"
        "\tpxor    %xmm4, %xmm0\n"
        "\tpxor    %xmm3, %xmm7\n"
        "\tpxor    %xmm3, %xmm5\n"
        "\tpxor    %xmm3, %xmm4\n"
        "\tpxor    %xmm3, %xmm2\n"
        "\tpxor    %xmm2, %xmm7\n"
        "\tpxor    %xmm2, %xmm5\n"
        "\tpxor    %xmm2, %xmm4\n"
        "\tpxor    %xmm2, %xmm1\n"
        "\tpxor    %xmm2, %xmm0\n"
        "\tpxor    %xmm4, %xmm2\n"
        "\tpxor    %xmm1, %xmm5\n"
        "\tpxor    %xmm1, %xmm3\n"
        "\tpxor    %xmm1, %xmm2\n"
        "\tpxor    %xmm1, %xmm0\n"
        "\tpxor    %xmm0, %xmm7\n"
        "\tpxor    %xmm0, %xmm6\n"
        "\tpxor    %xmm0, %xmm5\n"
        "\tpxor    %xmm0, %xmm3\n"
        "\tpxor    %xmm0, %xmm2\n"
        "\tpxor    %xmm0, %xmm1\n"
        "\tpxor    %xmm1, %xmm0\n"
    );
}

static void gf8mul_11101111(void)
{
    __asm__ __volatile__
    (
        "\tpxor    %xmm7, %xmm6\n"
        "\tpxor    %xmm7, %xmm5\n"
        "\tpxor    %xmm7, %xmm1\n"
        "\tpxor    %xmm6, %xmm7\n"
        "\tpxor    %xmm6, %xmm5\n"
        "\tpxor    %xmm6, %xmm4\n"
        "\tpxor    %xmm6, %xmm3\n"
        "\tpxor    %xmm6, %xmm2\n"
        "\tpxor    %xmm6, %xmm0\n"
        "\tpxor    %xmm5, %xmm7\n"
        "\tpxor    %xmm5, %xmm1\n"
        "\tpxor    %xmm5, %xmm0\n"
        "\tpxor    %xmm7, %xmm5\n"
        "\tpxor    %xmm4, %xmm7\n"
        "\tpxor    %xmm4, %xmm5\n"
        "\tpxor    %xmm4, %xmm3\n"
        "\tpxor    %xmm4, %xmm1\n"
        "\tpxor    %xmm3, %xmm7\n"
        "\tpxor    %xmm3, %xmm5\n"
        "\tpxor    %xmm3, %xmm4\n"
        "\tpxor    %xmm3, %xmm2\n"
        "\tpxor    %xmm4, %xmm3\n"
        "\tpxor    %xmm2, %xmm6\n"
        "\tpxor    %xmm2, %xmm4\n"
        "\tpxor    %xmm2, %xmm3\n"
        "\tpxor    %xmm2, %xmm1\n"
        "\tpxor    %xmm3, %xmm2\n"
        "\tpxor    %xmm1, %xmm5\n"
        "\tpxor    %xmm1, %xmm3\n"
        "\tpxor    %xmm1, %xmm2\n"
        "\tpxor    %xmm1, %xmm0\n"
        "\tpxor    %xmm2, %xmm1\n"
        "\tpxor    %xmm0, %xmm7\n"
        "\tpxor    %xmm0, %xmm6\n"
        "\tpxor    %xmm0, %xmm5\n"
        "\tpxor    %xmm0, %xmm3\n"
        "\tpxor    %xmm0, %xmm2\n"
        "\tpxor    %xmm0, %xmm1\n"
    );
}

static void gf8mul_11110000(void)
{
    __asm__ __volatile__
    (
        "\tpxor    %xmm7, %xmm6\n"
        "\tpxor    %xmm7, %xmm2\n"
        "\tpxor    %xmm6, %xmm5\n"
        "\tpxor    %xmm6, %xmm1\n"
        "\tpxor    %xmm5, %xmm4\n"
        "\tpxor    %xmm5, %xmm0\n"
        "\tpxor    %xmm4, %xmm6\n"
        "\tpxor    %xmm4, %xmm2\n"
        "\tpxor    %xmm4, %xmm1\n"
        "\tpxor    %xmm4, %xmm0\n"
        "\tpxor    %xmm3, %xmm5\n"
        "\tpxor    %xmm3, %xmm4\n"
        "\tpxor    %xmm3, %xmm2\n"
        "\tpxor    %xmm3, %xmm1\n"
        "\tpxor    %xmm3, %xmm0\n"
        "\tpxor    %xmm2, %xmm4\n"
        "\tpxor    %xmm2, %xmm0\n"
        "\tpxor    %xmm1, %xmm7\n"
        "\tpxor    %xmm1, %xmm6\n"
        "\tpxor    %xmm1, %xmm5\n"
        "\tpxor    %xmm1, %xmm4\n"
        "\tpxor    %xmm1, %xmm3\n"
        "\tpxor    %xmm1, %xmm2\n"
        "\tpxor    %xmm2, %xmm1\n"
        "\tpxor    %xmm0, %xmm7\n"
        "\tpxor    %xmm0, %xmm6\n"
        "\tpxor    %xmm0, %xmm5\n"
        "\tpxor    %xmm0, %xmm4\n"
        "\tpxor    %xmm4, %xmm0\n"
    );
}

static void gf8mul_11110001(void)
{
    __asm__ __volatile__
    (
        "\tpxor    %xmm7, %xmm6\n"
        "\tpxor    %xmm7, %xmm5\n"
        "\tpxor    %xmm7, %xmm4\n"
        "\tpxor    %xmm7, %xmm3\n"
        "\tpxor    %xmm7, %xmm2\n"
        "\tpxor    %xmm7, %xmm1\n"
        "\tpxor    %xmm6, %xmm5\n"
        "\tpxor    %xmm6, %xmm4\n"
        "\tpxor    %xmm6, %xmm3\n"
        "\tpxor    %xmm6, %xmm2\n"
        "\tpxor    %xmm6, %xmm1\n"
        "\tpxor    %xmm6, %xmm0\n"
        "\tpxor    %xmm5, %xmm7\n"
        "\tpxor    %xmm5, %xmm6\n"
        "\tpxor    %xmm5, %xmm4\n"
        "\tpxor    %xmm4, %xmm6\n"
        "\tpxor    %xmm4, %xmm5\n"
        "\tpxor    %xmm4, %xmm3\n"
        "\tpxor    %xmm3, %xmm5\n"
        "\tpxor    %xmm3, %xmm4\n"
        "\tpxor    %xmm3, %xmm2\n"
        "\tpxor    %xmm2, %xmm4\n"
        "\tpxor    %xmm2, %xmm3\n"
        "\tpxor    %xmm2, %xmm1\n"
        "\tpxor    %xmm1, %xmm3\n"
        "\tpxor    %xmm1, %xmm2\n"
        "\tpxor    %xmm1, %xmm0\n"
        "\tpxor    %xmm0, %xmm7\n"
        "\tpxor    %xmm0, %xmm6\n"
        "\tpxor    %xmm0, %xmm5\n"
        "\tpxor    %xmm0, %xmm4\n"
    );
}

static void gf8mul_11110010(void)
{
    __asm__ __volatile__
    (
        "\tpxor    %xmm7, %xmm3\n"
        "\tpxor    %xmm7, %xmm2\n"
        "\tpxor    %xmm6, %xmm2\n"
        "\tpxor    %xmm6, %xmm1\n"
        "\tpxor    %xmm5, %xmm1\n"
        "\tpxor    %xmm5, %xmm0\n"
        "\tpxor    %xmm4, %xmm7\n"
        "\tpxor    %xmm4, %xmm1\n"
        "\tpxor    %xmm4, %xmm0\n"
        "\tpxor    %xmm3, %xmm7\n"
        "\tpxor    %xmm3, %xmm6\n"
        "\tpxor    %xmm3, %xmm1\n"
        "\tpxor    %xmm3, %xmm0\n"
        "\tpxor    %xmm2, %xmm7\n"
        "\tpxor    %xmm2, %xmm6\n"
        "\tpxor    %xmm2, %xmm5\n"
        "\tpxor    %xmm2, %xmm1\n"
        "\tpxor    %xmm2, %xmm0\n"
        "\tpxor    %xmm1, %xmm3\n"
        "\tpxor    %xmm1, %xmm0\n"
        "\tpxor    %xmm0, %xmm7\n"
        "\tpxor    %xmm0, %xmm6\n"
        "\tpxor    %xmm0, %xmm5\n"
        "\tpxor    %xmm0, %xmm4\n"
        "\tpxor    %xmm0, %xmm1\n"
        "\tpxor    %xmm1, %xmm0\n"
    );
}

static void gf8mul_11110011(void)
{
    __asm__ __volatile__
    (
        "\tpxor    %xmm7, %xmm6\n"
        "\tpxor    %xmm6, %xmm5\n"
        "\tpxor    %xmm5, %xmm4\n"
        "\tpxor    %xmm4, %xmm3\n"
        "\tpxor    %xmm3, %xmm2\n"
        "\tpxor    %xmm2, %xmm1\n"
        "\tpxor    %xmm1, %xmm0\n"
        "\tpxor    %xmm0, %xmm7\n"
        "\tpxor    %xmm0, %xmm6\n"
        "\tpxor    %xmm0, %xmm5\n"
        "\tpxor    %xmm0, %xmm4\n"
        "\tpxor    %xmm0, %xmm2\n"
        "\tpxor    %xmm7, %xmm5\n"
        "\tpxor    %xmm7, %xmm2\n"
        "\tpxor    %xmm6, %xmm4\n"
        "\tpxor    %xmm6, %xmm1\n"
        "\tpxor    %xmm5, %xmm7\n"
        "\tpxor    %xmm7, %xmm5\n"
        "\tpxor    %xmm4, %xmm6\n"
        "\tpxor    %xmm6, %xmm4\n"
        "\tpxor    %xmm3, %xmm5\n"
        "\tpxor    %xmm5, %xmm3\n"
        "\tpxor    %xmm2, %xmm4\n"
        "\tpxor    %xmm4, %xmm2\n"
        "\tpxor    %xmm1, %xmm3\n"
        "\tpxor    %xmm3, %xmm1\n"
        "\tpxor    %xmm0, %xmm2\n"
        "\tpxor    %xmm2, %xmm0\n"
    );
}

static void gf8mul_11110100(void)
{
    __asm__ __volatile__
    (
        "\tpxor    %xmm7, %xmm4\n"
        "\tpxor    %xmm7, %xmm3\n"
        "\tpxor    %xmm7, %xmm2\n"
        "\tpxor    %xmm7, %xmm0\n"
        "\tpxor    %xmm6, %xmm7\n"
        "\tpxor    %xmm6, %xmm4\n"
        "\tpxor    %xmm6, %xmm3\n"
        "\tpxor    %xmm6, %xmm2\n"
        "\tpxor    %xmm6, %xmm0\n"
        "\tpxor    %xmm5, %xmm7\n"
        "\tpxor    %xmm5, %xmm6\n"
        "\tpxor    %xmm5, %xmm4\n"
        "\tpxor    %xmm5, %xmm3\n"
        "\tpxor    %xmm5, %xmm2\n"
        "\tpxor    %xmm5, %xmm0\n"
        "\tpxor    %xmm4, %xmm7\n"
        "\tpxor    %xmm4, %xmm6\n"
        "\tpxor    %xmm4, %xmm5\n"
        "\tpxor    %xmm4, %xmm2\n"
        "\tpxor    %xmm4, %xmm0\n"
        "\tpxor    %xmm3, %xmm7\n"
        "\tpxor    %xmm3, %xmm6\n"
        "\tpxor    %xmm3, %xmm5\n"
        "\tpxor    %xmm3, %xmm4\n"
        "\tpxor    %xmm3, %xmm2\n"
        "\tpxor    %xmm3, %xmm0\n"
        "\tpxor    %xmm4, %xmm3\n"
        "\tpxor    %xmm2, %xmm7\n"
        "\tpxor    %xmm2, %xmm6\n"
        "\tpxor    %xmm2, %xmm5\n"
        "\tpxor    %xmm2, %xmm4\n"
        "\tpxor    %xmm1, %xmm7\n"
        "\tpxor    %xmm1, %xmm6\n"
        "\tpxor    %xmm1, %xmm5\n"
        "\tpxor    %xmm1, %xmm4\n"
        "\tpxor    %xmm1, %xmm2\n"
        "\tpxor    %xmm2, %xmm1\n"
        "\tpxor    %xmm0, %xmm7\n"
        "\tpxor    %xmm0, %xmm6\n"
        "\tpxor    %xmm0, %xmm5\n"
        "\tpxor    %xmm0, %xmm4\n"
        "\tpxor    %xmm0, %xmm2\n"
        "\tpxor    %xmm2, %xmm0\n"
    );
}

static void gf8mul_11110101(void)
{
    __asm__ __volatile__
    (
        "\tpxor    %xmm7, %xmm6\n"
        "\tpxor    %xmm6, %xmm5\n"
        "\tpxor    %xmm5, %xmm4\n"
        "\tpxor    %xmm4, %xmm3\n"
        "\tpxor    %xmm3, %xmm2\n"
        "\tpxor    %xmm2, %xmm1\n"
        "\tpxor    %xmm1, %xmm0\n"
        "\tpxor    %xmm0, %xmm7\n"
        "\tpxor    %xmm0, %xmm6\n"
        "\tpxor    %xmm0, %xmm5\n"
        "\tpxor    %xmm0, %xmm4\n"
        "\tpxor    %xmm0, %xmm2\n"
    );
}

static void gf8mul_11110110(void)
{
    __asm__ __volatile__
    (
        "\tpxor    %xmm7, %xmm6\n"
        "\tpxor    %xmm7, %xmm5\n"
        "\tpxor    %xmm7, %xmm0\n"
        "\tpxor    %xmm6, %xmm3\n"
        "\tpxor    %xmm6, %xmm2\n"
        "\tpxor    %xmm6, %xmm1\n"
        "\tpxor    %xmm6, %xmm0\n"
        "\tpxor    %xmm5, %xmm6\n"
        "\tpxor    %xmm5, %xmm4\n"
        "\tpxor    %xmm4, %xmm7\n"
        "\tpxor    %xmm4, %xmm6\n"
        "\tpxor    %xmm4, %xmm3\n"
        "\tpxor    %xmm4, %xmm2\n"
        "\tpxor    %xmm4, %xmm1\n"
        "\tpxor    %xmm4, %xmm0\n"
        "\tpxor    %xmm6, %xmm4\n"
        "\tpxor    %xmm3, %xmm4\n"
        "\tpxor    %xmm3, %xmm2\n"
        "\tpxor    %xmm2, %xmm7\n"
        "\tpxor    %xmm2, %xmm6\n"
        "\tpxor    %xmm2, %xmm5\n"
        "\tpxor    %xmm2, %xmm4\n"
        "\tpxor    %xmm2, %xmm3\n"
        "\tpxor    %xmm2, %xmm1\n"
        "\tpxor    %xmm2, %xmm0\n"
        "\tpxor    %xmm3, %xmm2\n"
        "\tpxor    %xmm1, %xmm2\n"
        "\tpxor    %xmm1, %xmm0\n"
        "\tpxor    %xmm0, %xmm7\n"
        "\tpxor    %xmm0, %xmm6\n"
        "\tpxor    %xmm0, %xmm5\n"
        "\tpxor    %xmm0, %xmm4\n"
        "\tpxor    %xmm0, %xmm2\n"
        "\tpxor    %xmm0, %xmm1\n"
        "\tpxor    %xmm1, %xmm0\n"
    );
}

static void gf8mul_11110111(void)
{
    __asm__ __volatile__
    (
        "\tpxor    %xmm7, %xmm5\n"
        "\tpxor    %xmm6, %xmm4\n"
        "\tpxor    %xmm5, %xmm3\n"
        "\tpxor    %xmm4, %xmm2\n"
        "\tpxor    %xmm3, %xmm1\n"
        "\tpxor    %xmm2, %xmm0\n"
        "\tpxor    %xmm1, %xmm7\n"
        "\tpxor    %xmm1, %xmm6\n"
        "\tpxor    %xmm1, %xmm5\n"
        "\tpxor    %xmm1, %xmm4\n"
        "\tpxor    %xmm1, %xmm2\n"
        "\tpxor    %xmm1, %xmm0\n"
        "\tpxor    %xmm0, %xmm7\n"
        "\tpxor    %xmm0, %xmm5\n"
        "\tpxor    %xmm0, %xmm2\n"
        "\tpxor    %xmm0, %xmm1\n"
        "\tpxor    %xmm1, %xmm0\n"
        "\tpxor    %xmm7, %xmm3\n"
        "\tpxor    %xmm7, %xmm1\n"
        "\tpxor    %xmm7, %xmm0\n"
        "\tpxor    %xmm6, %xmm7\n"
        "\tpxor    %xmm5, %xmm6\n"
        "\tpxor    %xmm4, %xmm5\n"
        "\tpxor    %xmm3, %xmm4\n"
        "\tpxor    %xmm2, %xmm3\n"
        "\tpxor    %xmm1, %xmm2\n"
        "\tpxor    %xmm0, %xmm1\n"
    );
}

static void gf8mul_11111000(void)
{
    __asm__ __volatile__
    (
        "\tpxor    %xmm7, %xmm4\n"
        "\tpxor    %xmm7, %xmm3\n"
        "\tpxor    %xmm7, %xmm1\n"
        "\tpxor    %xmm6, %xmm3\n"
        "\tpxor    %xmm6, %xmm2\n"
        "\tpxor    %xmm6, %xmm0\n"
        "\tpxor    %xmm5, %xmm7\n"
        "\tpxor    %xmm5, %xmm4\n"
        "\tpxor    %xmm5, %xmm1\n"
        "\tpxor    %xmm4, %xmm6\n"
        "\tpxor    %xmm4, %xmm3\n"
        "\tpxor    %xmm4, %xmm0\n"
        "\tpxor    %xmm3, %xmm7\n"
        "\tpxor    %xmm3, %xmm5\n"
        "\tpxor    %xmm3, %xmm4\n"
        "\tpxor    %xmm3, %xmm2\n"
        "\tpxor    %xmm3, %xmm1\n"
        "\tpxor    %xmm2, %xmm5\n"
        "\tpxor    %xmm2, %xmm4\n"
        "\tpxor    %xmm2, %xmm3\n"
        "\tpxor    %xmm2, %xmm0\n"
        "\tpxor    %xmm1, %xmm7\n"
        "\tpxor    %xmm1, %xmm6\n"
        "\tpxor    %xmm1, %xmm5\n"
        "\tpxor    %xmm1, %xmm3\n"
        "\tpxor    %xmm1, %xmm2\n"
        "\tpxor    %xmm2, %xmm1\n"
        "\tpxor    %xmm0, %xmm7\n"
        "\tpxor    %xmm0, %xmm6\n"
        "\tpxor    %xmm0, %xmm5\n"
        "\tpxor    %xmm0, %xmm4\n"
        "\tpxor    %xmm0, %xmm3\n"
        "\tpxor    %xmm3, %xmm0\n"
    );
}

static void gf8mul_11111001(void)
{
    __asm__ __volatile__
    (
        "\tpxor    %xmm7, %xmm6\n"
        "\tpxor    %xmm7, %xmm5\n"
        "\tpxor    %xmm7, %xmm4\n"
        "\tpxor    %xmm7, %xmm3\n"
        "\tpxor    %xmm7, %xmm1\n"
        "\tpxor    %xmm6, %xmm7\n"
        "\tpxor    %xmm6, %xmm2\n"
        "\tpxor    %xmm6, %xmm1\n"
        "\tpxor    %xmm7, %xmm6\n"
        "\tpxor    %xmm5, %xmm6\n"
        "\tpxor    %xmm5, %xmm1\n"
        "\tpxor    %xmm5, %xmm0\n"
        "\tpxor    %xmm6, %xmm5\n"
        "\tpxor    %xmm4, %xmm7\n"
        "\tpxor    %xmm4, %xmm5\n"
        "\tpxor    %xmm4, %xmm3\n"
        "\tpxor    %xmm3, %xmm6\n"
        "\tpxor    %xmm3, %xmm4\n"
        "\tpxor    %xmm3, %xmm2\n"
        "\tpxor    %xmm2, %xmm5\n"
        "\tpxor    %xmm2, %xmm3\n"
        "\tpxor    %xmm2, %xmm1\n"
        "\tpxor    %xmm1, %xmm4\n"
        "\tpxor    %xmm1, %xmm2\n"
        "\tpxor    %xmm1, %xmm0\n"
        "\tpxor    %xmm0, %xmm7\n"
        "\tpxor    %xmm0, %xmm6\n"
        "\tpxor    %xmm0, %xmm5\n"
        "\tpxor    %xmm0, %xmm4\n"
        "\tpxor    %xmm0, %xmm3\n"
    );
}

static void gf8mul_11111010(void)
{
    __asm__ __volatile__
    (
        "\tpxor    %xmm7, %xmm6\n"
        "\tpxor    %xmm7, %xmm5\n"
        "\tpxor    %xmm7, %xmm4\n"
        "\tpxor    %xmm7, %xmm3\n"
        "\tpxor    %xmm7, %xmm0\n"
        "\tpxor    %xmm6, %xmm7\n"
        "\tpxor    %xmm6, %xmm5\n"
        "\tpxor    %xmm6, %xmm0\n"
        "\tpxor    %xmm7, %xmm6\n"
        "\tpxor    %xmm5, %xmm7\n"
        "\tpxor    %xmm5, %xmm3\n"
        "\tpxor    %xmm5, %xmm2\n"
        "\tpxor    %xmm5, %xmm0\n"
        "\tpxor    %xmm7, %xmm5\n"
        "\tpxor    %xmm4, %xmm7\n"
        "\tpxor    %xmm4, %xmm3\n"
        "\tpxor    %xmm3, %xmm7\n"
        "\tpxor    %xmm3, %xmm4\n"
        "\tpxor    %xmm3, %xmm1\n"
        "\tpxor    %xmm3, %xmm0\n"
        "\tpxor    %xmm4, %xmm3\n"
        "\tpxor    %xmm2, %xmm7\n"
        "\tpxor    %xmm2, %xmm6\n"
        "\tpxor    %xmm2, %xmm4\n"
        "\tpxor    %xmm2, %xmm3\n"
        "\tpxor    %xmm2, %xmm1\n"
        "\tpxor    %xmm2, %xmm0\n"
        "\tpxor    %xmm1, %xmm4\n"
        "\tpxor    %xmm1, %xmm0\n"
        "\tpxor    %xmm0, %xmm7\n"
        "\tpxor    %xmm0, %xmm6\n"
        "\tpxor    %xmm0, %xmm5\n"
        "\tpxor    %xmm0, %xmm4\n"
        "\tpxor    %xmm0, %xmm3\n"
        "\tpxor    %xmm0, %xmm1\n"
        "\tpxor    %xmm1, %xmm0\n"
    );
}

static void gf8mul_11111011(void)
{
    __asm__ __volatile__
    (
        "\tpxor    %xmm7, %xmm6\n"
        "\tpxor    %xmm7, %xmm5\n"
        "\tpxor    %xmm7, %xmm4\n"
        "\tpxor    %xmm7, %xmm0\n"
        "\tpxor    %xmm6, %xmm7\n"
        "\tpxor    %xmm6, %xmm5\n"
        "\tpxor    %xmm6, %xmm4\n"
        "\tpxor    %xmm6, %xmm1\n"
        "\tpxor    %xmm5, %xmm7\n"
        "\tpxor    %xmm5, %xmm4\n"
        "\tpxor    %xmm5, %xmm3\n"
        "\tpxor    %xmm5, %xmm2\n"
        "\tpxor    %xmm5, %xmm1\n"
        "\tpxor    %xmm4, %xmm7\n"
        "\tpxor    %xmm4, %xmm3\n"
        "\tpxor    %xmm7, %xmm4\n"
        "\tpxor    %xmm3, %xmm6\n"
        "\tpxor    %xmm3, %xmm2\n"
        "\tpxor    %xmm6, %xmm3\n"
        "\tpxor    %xmm2, %xmm5\n"
        "\tpxor    %xmm2, %xmm1\n"
        "\tpxor    %xmm5, %xmm2\n"
        "\tpxor    %xmm1, %xmm4\n"
        "\tpxor    %xmm1, %xmm0\n"
        "\tpxor    %xmm4, %xmm1\n"
        "\tpxor    %xmm0, %xmm7\n"
        "\tpxor    %xmm0, %xmm6\n"
        "\tpxor    %xmm0, %xmm5\n"
        "\tpxor    %xmm0, %xmm4\n"
        "\tpxor    %xmm0, %xmm3\n"
        "\tpxor    %xmm0, %xmm1\n"
    );
}

static void gf8mul_11111100(void)
{
    __asm__ __volatile__
    (
        "\tpxor    %xmm7, %xmm6\n"
        "\tpxor    %xmm7, %xmm2\n"
        "\tpxor    %xmm7, %xmm1\n"
        "\tpxor    %xmm7, %xmm0\n"
        "\tpxor    %xmm6, %xmm7\n"
        "\tpxor    %xmm6, %xmm5\n"
        "\tpxor    %xmm6, %xmm4\n"
        "\tpxor    %xmm6, %xmm0\n"
        "\tpxor    %xmm7, %xmm6\n"
        "\tpxor    %xmm5, %xmm7\n"
        "\tpxor    %xmm5, %xmm3\n"
        "\tpxor    %xmm5, %xmm1\n"
        "\tpxor    %xmm4, %xmm6\n"
        "\tpxor    %xmm4, %xmm2\n"
        "\tpxor    %xmm4, %xmm0\n"
        "\tpxor    %xmm3, %xmm7\n"
        "\tpxor    %xmm3, %xmm4\n"
        "\tpxor    %xmm3, %xmm2\n"
        "\tpxor    %xmm3, %xmm0\n"
        "\tpxor    %xmm2, %xmm7\n"
        "\tpxor    %xmm2, %xmm6\n"
        "\tpxor    %xmm2, %xmm4\n"
        "\tpxor    %xmm1, %xmm7\n"
        "\tpxor    %xmm1, %xmm6\n"
        "\tpxor    %xmm1, %xmm5\n"
        "\tpxor    %xmm1, %xmm2\n"
        "\tpxor    %xmm2, %xmm1\n"
        "\tpxor    %xmm0, %xmm7\n"
        "\tpxor    %xmm0, %xmm6\n"
        "\tpxor    %xmm0, %xmm5\n"
        "\tpxor    %xmm0, %xmm4\n"
        "\tpxor    %xmm0, %xmm3\n"
        "\tpxor    %xmm0, %xmm2\n"
        "\tpxor    %xmm2, %xmm0\n"
    );
}

static void gf8mul_11111101(void)
{
    __asm__ __volatile__
    (
        "\tpxor    %xmm7, %xmm6\n"
        "\tpxor    %xmm7, %xmm2\n"
        "\tpxor    %xmm7, %xmm0\n"
        "\tpxor    %xmm6, %xmm7\n"
        "\tpxor    %xmm6, %xmm5\n"
        "\tpxor    %xmm6, %xmm1\n"
        "\tpxor    %xmm7, %xmm6\n"
        "\tpxor    %xmm5, %xmm6\n"
        "\tpxor    %xmm5, %xmm4\n"
        "\tpxor    %xmm5, %xmm0\n"
        "\tpxor    %xmm6, %xmm5\n"
        "\tpxor    %xmm4, %xmm7\n"
        "\tpxor    %xmm4, %xmm6\n"
        "\tpxor    %xmm4, %xmm3\n"
        "\tpxor    %xmm3, %xmm6\n"
        "\tpxor    %xmm3, %xmm5\n"
        "\tpxor    %xmm3, %xmm2\n"
        "\tpxor    %xmm2, %xmm5\n"
        "\tpxor    %xmm2, %xmm4\n"
        "\tpxor    %xmm2, %xmm1\n"
        "\tpxor    %xmm1, %xmm4\n"
        "\tpxor    %xmm1, %xmm3\n"
        "\tpxor    %xmm1, %xmm0\n"
        "\tpxor    %xmm0, %xmm7\n"
        "\tpxor    %xmm0, %xmm6\n"
        "\tpxor    %xmm0, %xmm5\n"
        "\tpxor    %xmm0, %xmm4\n"
        "\tpxor    %xmm0, %xmm3\n"
        "\tpxor    %xmm0, %xmm2\n"
    );
}

static void gf8mul_11111110(void)
{
    __asm__ __volatile__
    (
        "\tpxor    %xmm7, %xmm6\n"
        "\tpxor    %xmm7, %xmm5\n"
        "\tpxor    %xmm7, %xmm4\n"
        "\tpxor    %xmm7, %xmm0\n"
        "\tpxor    %xmm6, %xmm7\n"
        "\tpxor    %xmm6, %xmm4\n"
        "\tpxor    %xmm6, %xmm0\n"
        "\tpxor    %xmm5, %xmm7\n"
        "\tpxor    %xmm5, %xmm6\n"
        "\tpxor    %xmm5, %xmm3\n"
        "\tpxor    %xmm5, %xmm1\n"
        "\tpxor    %xmm4, %xmm6\n"
        "\tpxor    %xmm4, %xmm1\n"
        "\tpxor    %xmm4, %xmm0\n"
        "\tpxor    %xmm6, %xmm4\n"
        "\tpxor    %xmm3, %xmm6\n"
        "\tpxor    %xmm3, %xmm5\n"
        "\tpxor    %xmm3, %xmm4\n"
        "\tpxor    %xmm3, %xmm2\n"
        "\tpxor    %xmm2, %xmm7\n"
        "\tpxor    %xmm2, %xmm6\n"
        "\tpxor    %xmm2, %xmm1\n"
        "\tpxor    %xmm2, %xmm0\n"
        "\tpxor    %xmm6, %xmm2\n"
        "\tpxor    %xmm1, %xmm4\n"
        "\tpxor    %xmm1, %xmm3\n"
        "\tpxor    %xmm1, %xmm2\n"
        "\tpxor    %xmm1, %xmm0\n"
        "\tpxor    %xmm0, %xmm7\n"
        "\tpxor    %xmm0, %xmm6\n"
        "\tpxor    %xmm0, %xmm5\n"
        "\tpxor    %xmm0, %xmm4\n"
        "\tpxor    %xmm0, %xmm3\n"
        "\tpxor    %xmm0, %xmm2\n"
        "\tpxor    %xmm0, %xmm1\n"
        "\tpxor    %xmm1, %xmm0\n"
    );
}

static void gf8mul_11111111(void)
{
    __asm__ __volatile__
    (
        "\tpxor    %xmm7, %xmm0\n"
        "\tpxor    %xmm6, %xmm7\n"
        "\tpxor    %xmm6, %xmm3\n"
        "\tpxor    %xmm6, %xmm2\n"
        "\tpxor    %xmm6, %xmm1\n"
        "\tpxor    %xmm6, %xmm0\n"
        "\tpxor    %xmm5, %xmm7\n"
        "\tpxor    %xmm5, %xmm6\n"
        "\tpxor    %xmm5, %xmm3\n"
        "\tpxor    %xmm4, %xmm6\n"
        "\tpxor    %xmm4, %xmm5\n"
        "\tpxor    %xmm4, %xmm2\n"
        "\tpxor    %xmm3, %xmm5\n"
        "\tpxor    %xmm3, %xmm4\n"
        "\tpxor    %xmm3, %xmm1\n"
        "\tpxor    %xmm2, %xmm4\n"
        "\tpxor    %xmm2, %xmm3\n"
        "\tpxor    %xmm2, %xmm0\n"
        "\tpxor    %xmm1, %xmm7\n"
        "\tpxor    %xmm1, %xmm5\n"
        "\tpxor    %xmm1, %xmm3\n"
        "\tpxor    %xmm0, %xmm6\n"
        "\tpxor    %xmm0, %xmm4\n"
        "\tpxor    %xmm0, %xmm2\n"
        "\tpxor    %xmm7, %xmm3\n"
        "\tpxor    %xmm7, %xmm1\n"
        "\tpxor    %xmm7, %xmm0\n"
        "\tpxor    %xmm6, %xmm7\n"
        "\tpxor    %xmm5, %xmm6\n"
        "\tpxor    %xmm4, %xmm5\n"
        "\tpxor    %xmm3, %xmm4\n"
        "\tpxor    %xmm2, %xmm3\n"
        "\tpxor    %xmm1, %xmm2\n"
        "\tpxor    %xmm0, %xmm1\n"
    );
}

void (* ec_gf_mul_table[256])(void) =
{
    gf8mul_00000000,
    gf8mul_00000001,
    gf8mul_00000010,
    gf8mul_00000011,
    gf8mul_00000100,
    gf8mul_00000101,
    gf8mul_00000110,
    gf8mul_00000111,
    gf8mul_00001000,
    gf8mul_00001001,
    gf8mul_00001010,
    gf8mul_00001011,
    gf8mul_00001100,
    gf8mul_00001101,
    gf8mul_00001110,
    gf8mul_00001111,
    gf8mul_00010000,
    gf8mul_00010001,
    gf8mul_00010010,
    gf8mul_00010011,
    gf8mul_00010100,
    gf8mul_00010101,
    gf8mul_00010110,
    gf8mul_00010111,
    gf8mul_00011000,
    gf8mul_00011001,
    gf8mul_00011010,
    gf8mul_00011011,
    gf8mul_00011100,
    gf8mul_00011101,
    gf8mul_00011110,
    gf8mul_00011111,
    gf8mul_00100000,
    gf8mul_00100001,
    gf8mul_00100010,
    gf8mul_00100011,
    gf8mul_00100100,
    gf8mul_00100101,
    gf8mul_00100110,
    gf8mul_00100111,
    gf8mul_00101000,
    gf8mul_00101001,
    gf8mul_00101010,
    gf8mul_00101011,
    gf8mul_00101100,
    gf8mul_00101101,
    gf8mul_00101110,
    gf8mul_00101111,
    gf8mul_00110000,
    gf8mul_00110001,
    gf8mul_00110010,
    gf8mul_00110011,
    gf8mul_00110100,
    gf8mul_00110101,
    gf8mul_00110110,
    gf8mul_00110111,
    gf8mul_00111000,
    gf8mul_00111001,
    gf8mul_00111010,
    gf8mul_00111011,
    gf8mul_00111100,
    gf8mul_00111101,
    gf8mul_00111110,
    gf8mul_00111111,
    gf8mul_01000000,
    gf8mul_01000001,
    gf8mul_01000010,
    gf8mul_01000011,
    gf8mul_01000100,
    gf8mul_01000101,
    gf8mul_01000110,
    gf8mul_01000111,
    gf8mul_01001000,
    gf8mul_01001001,
    gf8mul_01001010,
    gf8mul_01001011,
    gf8mul_01001100,
    gf8mul_01001101,
    gf8mul_01001110,
    gf8mul_01001111,
    gf8mul_01010000,
    gf8mul_01010001,
    gf8mul_01010010,
    gf8mul_01010011,
    gf8mul_01010100,
    gf8mul_01010101,
    gf8mul_01010110,
    gf8mul_01010111,
    gf8mul_01011000,
    gf8mul_01011001,
    gf8mul_01011010,
    gf8mul_01011011,
    gf8mul_01011100,
    gf8mul_01011101,
    gf8mul_01011110,
    gf8mul_01011111,
    gf8mul_01100000,
    gf8mul_01100001,
    gf8mul_01100010,
    gf8mul_01100011,
    gf8mul_01100100,
    gf8mul_01100101,
    gf8mul_01100110,
    gf8mul_01100111,
    gf8mul_01101000,
    gf8mul_01101001,
    gf8mul_01101010,
    gf8mul_01101011,
    gf8mul_01101100,
    gf8mul_01101101,
    gf8mul_01101110,
    gf8mul_01101111,
    gf8mul_01110000,
    gf8mul_01110001,
    gf8mul_01110010,
    gf8mul_01110011,
    gf8mul_01110100,
    gf8mul_01110101,
    gf8mul_01110110,
    gf8mul_01110111,
    gf8mul_01111000,
    gf8mul_01111001,
    gf8mul_01111010,
    gf8mul_01111011,
    gf8mul_01111100,
    gf8mul_01111101,
    gf8mul_01111110,
    gf8mul_01111111,
    gf8mul_10000000,
    gf8mul_10000001,
    gf8mul_10000010,
    gf8mul_10000011,
    gf8mul_10000100,
    gf8mul_10000101,
    gf8mul_10000110,
    gf8mul_10000111,
    gf8mul_10001000,
    gf8mul_10001001,
    gf8mul_10001010,
    gf8mul_10001011,
    gf8mul_10001100,
    gf8mul_10001101,
    gf8mul_10001110,
    gf8mul_10001111,
    gf8mul_10010000,
    gf8mul_10010001,
    gf8mul_10010010,
    gf8mul_10010011,
    gf8mul_10010100,
    gf8mul_10010101,
    gf8mul_10010110,
    gf8mul_10010111,
    gf8mul_10011000,
    gf8mul_10011001,
    gf8mul_10011010,
    gf8mul_10011011,
    gf8mul_10011100,
    gf8mul_10011101,
    gf8mul_10011110,
    gf8mul_10011111,
    gf8mul_10100000,
    gf8mul_10100001,
    gf8mul_10100010,
    gf8mul_10100011,
    gf8mul_10100100,
    gf8mul_10100101,
    gf8mul_10100110,
    gf8mul_10100111,
    gf8mul_10101000,
    gf8mul_10101001,
    gf8mul_10101010,
    gf8mul_10101011,
    gf8mul_10101100,
    gf8mul_10101101,
    gf8mul_10101110,
    gf8mul_10101111,
    gf8mul_10110000,
    gf8mul_10110001,
    gf8mul_10110010,
    gf8mul_10110011,
    gf8mul_10110100,
    gf8mul_10110101,
    gf8mul_10110110,
    gf8mul_10110111,
    gf8mul_10111000,
    gf8mul_10111001,
    gf8mul_10111010,
    gf8mul_10111011,
    gf8mul_10111100,
    gf8mul_10111101,
    gf8mul_10111110,
    gf8mul_10111111,
    gf8mul_11000000,
    gf8mul_11000001,
    gf8mul_11000010,
    gf8mul_11000011,
    gf8mul_11000100,
    gf8mul_11000101,
    gf8mul_11000110,
    gf8mul_11000111,
    gf8mul_11001000,
    gf8mul_11001001,
    gf8mul_11001010,
    gf8mul_11001011,
    gf8mul_11001100,
    gf8mul_11001101,
    gf8mul_11001110,
    gf8mul_11001111,
    gf8mul_11010000,
    gf8mul_11010001,
    gf8mul_11010010,
    gf8mul_11010011,
    gf8mul_11010100,
    gf8mul_11010101,
    gf8mul_11010110,
    gf8mul_11010111,
    gf8mul_11011000,
    gf8mul_11011001,
    gf8mul_11011010,
    gf8mul_11011011,
    gf8mul_11011100,
    gf8mul_11011101,
    gf8mul_11011110,
    gf8mul_11011111,
    gf8mul_11100000,
    gf8mul_11100001,
    gf8mul_11100010,
    gf8mul_11100011,
    gf8mul_11100100,
    gf8mul_11100101,
    gf8mul_11100110,
    gf8mul_11100111,
    gf8mul_11101000,
    gf8mul_11101001,
    gf8mul_11101010,
    gf8mul_11101011,
    gf8mul_11101100,
    gf8mul_11101101,
    gf8mul_11101110,
    gf8mul_11101111,
    gf8mul_11110000,
    gf8mul_11110001,
    gf8mul_11110010,
    gf8mul_11110011,
    gf8mul_11110100,
    gf8mul_11110101,
    gf8mul_11110110,
    gf8mul_11110111,
    gf8mul_11111000,
    gf8mul_11111001,
    gf8mul_11111010,
    gf8mul_11111011,
    gf8mul_11111100,
    gf8mul_11111101,
    gf8mul_11111110,
    gf8mul_11111111
};
