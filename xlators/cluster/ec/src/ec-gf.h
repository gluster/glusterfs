/*
  Copyright (c) 2012-2014 DataLab, s.l. <http://www.datalab.es>
  This file is part of GlusterFS.

  This file is licensed to you under your choice of the GNU Lesser
  General Public License, version 3 or any later version (LGPLv3 or
  later), or the GNU General Public License, version 2 (GPLv2), in all
  cases as published by the Free Software Foundation.
*/

#ifndef __EC_GF8_H__
#define __EC_GF8_H__

#define EC_GF_BITS 8
#define EC_GF_MOD 0x11D

#define EC_GF_SIZE (1 << EC_GF_BITS)
#define EC_GF_WORD_SIZE sizeof(uint64_t)

extern void (* ec_gf_muladd[])(uint8_t * out, uint8_t * in,
                               unsigned int width);

#endif /* __EC_GF8_H__ */
