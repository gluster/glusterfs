/*
   Copyright (c) 2008 Z RESEARCH, Inc. <http://www.zresearch.com>
   This file is part of GlusterFS.

   GlusterFS is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published
   by the Free Software Foundation; either version 3 of the License,
   or (at your option) any later version.

   GlusterFS is distributed in the hope that it will be useful, but
   WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program.  If not, see
   <http://www.gnu.org/licenses/>.
*/

#ifndef _BYTE_ORDER_H
#define _BYTE_ORDER_H

#ifndef _CONFIG_H
#define _CONFIG_H
//#include "config.h"
#endif

#include "compat.h"

#include <inttypes.h>

#define LS1 0x00ffU
#define MS1 0xff00U
#define LS2 0x0000ffffU
#define MS2 0xffff0000U
#define LS4 0x00000000ffffffffULL
#define MS4 0xffffffff00000000ULL


#define do_swap2(x) (((x&LS1) << 8)|(((x&MS1) >> 8)))
#define do_swap4(x) ((do_swap2(x&LS2) << 16)|(do_swap2((x&MS2) >> 16)))
#define do_swap8(x) ((do_swap4(x&LS4) << 32)|(do_swap4((x&MS4) >> 32)))


static inline uint16_t
__swap16 (uint16_t x)
{
  return do_swap2(x);
}


static inline int32_t
__swap32 (int32_t x)
{
  return do_swap4(x);
}


static inline int64_t
__swap64 (int64_t x)
{
  return do_swap8(x);
}


#if __BYTE_ORDER == __LITTLE_ENDIAN
#define hton16(x) __swap16(x)
#define hton32(x) __swap32(x)
#define hton64(x) __swap64(x)
#else
#define hton16(x) (x)
#define hton32(x) (x)
#define hton64(x) (x)
#endif


#define ntoh16(x) hton16(x)
#define ntoh32(x) hton32(x)
#define ntoh64(x) hton64(x)

#endif /* _BYTE_ORDER_H */
