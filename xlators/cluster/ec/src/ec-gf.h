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

#ifndef __EC_GF8_H__
#define __EC_GF8_H__

#define EC_GF_BITS 8
#define EC_GF_MOD 0x11D

#define EC_GF_SIZE (1 << EC_GF_BITS)
#define EC_GF_WORD_SIZE sizeof(uint64_t)

extern void (* ec_gf_muladd[])(uint8_t * out, uint8_t * in,
                               unsigned int width);

#endif /* __EC_GF8_H__ */
