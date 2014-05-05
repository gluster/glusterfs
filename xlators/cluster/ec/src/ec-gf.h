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
 */

#ifndef __EC_GF_H__
#define __EC_GF_H__

#define EC_GF_BITS 8
#define EC_GF_MOD 0x11D

#define ec_gf_load(addr) \
    do \
    { \
        __asm__ __volatile__ \
        ( \
            "\tmovdqa  0*16(%0), %%xmm0\n" \
            "\tmovdqa  1*16(%0), %%xmm1\n" \
            "\tmovdqa  2*16(%0), %%xmm2\n" \
            "\tmovdqa  3*16(%0), %%xmm3\n" \
            "\tmovdqa  4*16(%0), %%xmm4\n" \
            "\tmovdqa  5*16(%0), %%xmm5\n" \
            "\tmovdqa  6*16(%0), %%xmm6\n" \
            "\tmovdqa  7*16(%0), %%xmm7\n" \
            : \
            : "r" (addr) \
            : "xmm0", "xmm1", "xmm2", "xmm3", "xmm4", "xmm5", "xmm6", "xmm7" \
        ); \
    } while (0)

#define ec_gf_store(addr) \
    do \
    { \
        __asm__ __volatile__ \
        ( \
            "\tmovdqa  %%xmm0, 0*16(%0)\n" \
            "\tmovdqa  %%xmm1, 1*16(%0)\n" \
            "\tmovdqa  %%xmm2, 2*16(%0)\n" \
            "\tmovdqa  %%xmm3, 3*16(%0)\n" \
            "\tmovdqa  %%xmm4, 4*16(%0)\n" \
            "\tmovdqa  %%xmm5, 5*16(%0)\n" \
            "\tmovdqa  %%xmm6, 6*16(%0)\n" \
            "\tmovdqa  %%xmm7, 7*16(%0)\n" \
            : \
            : "r" (addr) \
            : "memory" \
        ); \
    } while (0)

#define ec_gf_clear() \
    do \
    { \
        __asm__ __volatile__ \
        ( \
            "\tpxor    %xmm0, %xmm0\n" \
            "\tpxor    %xmm1, %xmm1\n" \
            "\tpxor    %xmm2, %xmm2\n" \
            "\tpxor    %xmm3, %xmm3\n" \
            "\tpxor    %xmm4, %xmm4\n" \
            "\tpxor    %xmm5, %xmm5\n" \
            "\tpxor    %xmm6, %xmm6\n" \
            "\tpxor    %xmm7, %xmm7\n" \
            : \
            : \
            : "xmm0", "xmm1", "xmm2", "xmm3", "xmm4", "xmm5", "xmm6", "xmm7" \
        ); \
    } while (0)

#define ec_gf_xor(addr) \
    do \
    { \
        __asm__ __volatile__ \
        ( \
            "\tpxor    0*16(%0), %%xmm0\n" \
            "\tpxor    1*16(%0), %%xmm1\n" \
            "\tpxor    2*16(%0), %%xmm2\n" \
            "\tpxor    3*16(%0), %%xmm3\n" \
            "\tpxor    4*16(%0), %%xmm4\n" \
            "\tpxor    5*16(%0), %%xmm5\n" \
            "\tpxor    6*16(%0), %%xmm6\n" \
            "\tpxor    7*16(%0), %%xmm7\n" \
            : \
            : "r" (addr) \
            : "xmm0", "xmm1", "xmm2", "xmm3", "xmm4", "xmm5", "xmm6", "xmm7" \
        ); \
    } while (0)

extern void (* ec_gf_mul_table[])(void);

#endif /* __EC_GF_H__ */
