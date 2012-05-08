/*
  Copyright (c) 2008-2012 Red Hat, Inc. <http://www.redhat.com>
  This file is part of GlusterFS.

  This file is licensed to you under your choice of the GNU Lesser
  General Public License, version 3 or any later version (LGPLv3 or
  later), or the GNU General Public License, version 2 (GPLv2), in all
  cases as published by the Free Software Foundation.
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

static uint16_t (*htole16) (uint16_t);
static uint32_t (*htole32) (uint32_t);
static uint64_t (*htole64) (uint64_t);

#define letoh16 htole16
#define letoh32 htole32
#define letoh64 htole64

static uint16_t (*htobe16) (uint16_t);
static uint32_t (*htobe32) (uint32_t);
static uint64_t (*htobe64) (uint64_t);

#define betoh16 htobe16
#define betoh32 htobe32
#define betoh64 htobe64


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
__byte_order_n16 (uint16_t i)
{
	uint32_t num = 1;

	if (((char *)(&num))[0] == 1) {
                /* cpu is le */
		hton16 = __swap16;
		hton32 = __swap32;
		hton64 = __swap64;
	} else {
                /* cpu is be */
		hton16 = __noswap16;
		hton32 = __noswap32;
		hton64 = __noswap64;
	}

	return hton16 (i);
}


static inline uint32_t
__byte_order_n32 (uint32_t i)
{
	uint32_t num = 1;

	if (((char *)(&num))[0] == 1) {
                /* cpu is le */
		hton16 = __swap16;
		hton32 = __swap32;
		hton64 = __swap64;
	} else {
                /* cpu is be */
		hton16 = __noswap16;
		hton32 = __noswap32;
		hton64 = __noswap64;
	}

	return hton32 (i);
}


static inline uint64_t
__byte_order_n64 (uint64_t i)
{
	uint32_t num = 1;

	if (((char *)(&num))[0] == 1) {
                /* cpu is le */
		hton16 = __swap16;
		hton32 = __swap32;
		hton64 = __swap64;
	} else {
                /* cpu is be */
		hton16 = __noswap16;
		hton32 = __noswap32;
		hton64 = __noswap64;
	}

	return hton64 (i);
}


static uint16_t (*hton16) (uint16_t) = __byte_order_n16;
static uint32_t (*hton32) (uint32_t) = __byte_order_n32;
static uint64_t (*hton64) (uint64_t) = __byte_order_n64;


static inline uint16_t
__byte_order_le16 (uint16_t i)
{
	uint32_t num = 1;

	if (((char *)(&num))[0] == 1) {
                /* cpu is le */
		htole16 = __noswap16;
		htole32 = __noswap32;
		htole64 = __noswap64;
	} else {
                /* cpu is be */
		htole16 = __swap16;
		htole32 = __swap32;
		htole64 = __swap64;
	}

	return htole16 (i);
}


static inline uint32_t
__byte_order_le32 (uint32_t i)
{
	uint32_t num = 1;

	if (((char *)(&num))[0] == 1) {
                /* cpu is le */
		htole16 = __noswap16;
		htole32 = __noswap32;
		htole64 = __noswap64;
	} else {
                /* cpu is be */
		htole16 = __swap16;
		htole32 = __swap32;
		htole64 = __swap64;
	}

	return htole32 (i);
}


static inline uint64_t
__byte_order_le64 (uint64_t i)
{
	uint32_t num = 1;

	if (((char *)(&num))[0] == 1) {
                /* cpu is le */
		htole16 = __noswap16;
		htole32 = __noswap32;
		htole64 = __noswap64;
	} else {
                /* cpu is be */
		htole16 = __swap16;
		htole32 = __swap32;
		htole64 = __swap64;
	}

	return htole64 (i);
}


static uint16_t (*htole16) (uint16_t) = __byte_order_le16;
static uint32_t (*htole32) (uint32_t) = __byte_order_le32;
static uint64_t (*htole64) (uint64_t) = __byte_order_le64;


static inline uint16_t
__byte_order_be16 (uint16_t i)
{
	uint32_t num = 1;

	if (((char *)(&num))[0] == 1) {
                /* cpu is le */
		htobe16 = __swap16;
		htobe32 = __swap32;
		htobe64 = __swap64;
	} else {
                /* cpu is be */
		htobe16 = __noswap16;
		htobe32 = __noswap32;
		htobe64 = __noswap64;
	}

	return htobe16 (i);
}


static inline uint32_t
__byte_order_be32 (uint32_t i)
{
	uint32_t num = 1;

	if (((char *)(&num))[0] == 1) {
                /* cpu is le */
		htobe16 = __swap16;
		htobe32 = __swap32;
		htobe64 = __swap64;
	} else {
                /* cpu is be */
		htobe16 = __noswap16;
		htobe32 = __noswap32;
		htobe64 = __noswap64;
	}

	return htobe32 (i);
}


static inline uint64_t
__byte_order_be64 (uint64_t i)
{
	uint32_t num = 1;

	if (((char *)(&num))[0] == 1) {
                /* cpu is le */
		htobe16 = __swap16;
		htobe32 = __swap32;
		htobe64 = __swap64;
	} else {
                /* cpu is be */
		htobe16 = __noswap16;
		htobe32 = __noswap32;
		htobe64 = __noswap64;
	}

	return htobe64 (i);
}


static uint16_t (*htobe16) (uint16_t) = __byte_order_be16;
static uint32_t (*htobe32) (uint32_t) = __byte_order_be32;
static uint64_t (*htobe64) (uint64_t) = __byte_order_be64;



#endif /* _BYTE_ORDER_H */
