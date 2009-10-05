/*
   Copyright (c) 2008-2009 Gluster, Inc. <http://www.gluster.com>
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

#include <inttypes.h>

#define LS1 0x00ffU
#define MS1 0xff00U
#define LS2 0x0000ffffU
#define MS2 0xffff0000U
#define LS4 0x00000000ffffffffULL
#define MS4 0xffffffff00000000ULL


static uint16_t (*hton16) (uint16_t);
static uint32_t (*hton32) (uint32_t);
static uint64_t (*hton64) (uint64_t);

#define ntoh16 hton16
#define ntoh32 hton32
#define ntoh64 hton64

#define do_swap2(x) (((x&LS1) << 8)|(((x&MS1) >> 8)))
#define do_swap4(x) ((do_swap2(x&LS2) << 16)|(do_swap2((x&MS2) >> 16)))
#define do_swap8(x) ((do_swap4(x&LS4) << 32)|(do_swap4((x&MS4) >> 32)))


static inline uint16_t
__swap16 (uint16_t x)
{
	return do_swap2(x);
}


static inline uint32_t
__swap32 (uint32_t x)
{
	return do_swap4(x);
}


static inline uint64_t
__swap64 (uint64_t x)
{
	return do_swap8(x);
}


static inline uint16_t
__noswap16 (uint16_t x)
{
	return x;
}


static inline uint32_t
__noswap32 (uint32_t x)
{
	return x;
}


static inline uint64_t
__noswap64 (uint64_t x)
{
	return x;
}


static inline uint16_t
__byte_order_init16 (uint16_t i)
{
	uint32_t num = 1;

	if (((char *)(&num))[0] == 1) {
		hton16 = __swap16;
		hton32 = __swap32;
		hton64 = __swap64;
	} else {
		hton16 = __noswap16;
		hton32 = __noswap32;
		hton64 = __noswap64;
	}

	return hton16 (i);
}


static inline uint32_t
__byte_order_init32 (uint32_t i)
{
	uint32_t num = 1;

	if (((char *)(&num))[0] == 1) {
		hton16 = __swap16;
		hton32 = __swap32;
		hton64 = __swap64;
	} else {
		hton16 = __noswap16;
		hton32 = __noswap32;
		hton64 = __noswap64;
	}

	return hton32 (i);
}


static inline uint64_t
__byte_order_init64 (uint64_t i)
{
	uint32_t num = 1;

	if (((char *)(&num))[0] == 1) {
		hton16 = __swap16;
		hton32 = __swap32;
		hton64 = __swap64;
	} else {
		hton16 = __noswap16;
		hton32 = __noswap32;
		hton64 = __noswap64;
	}

	return hton64 (i);
}


static uint16_t (*hton16) (uint16_t) = __byte_order_init16;
static uint32_t (*hton32) (uint32_t) = __byte_order_init32;
static uint64_t (*hton64) (uint64_t) = __byte_order_init64;


#endif /* _BYTE_ORDER_H */
