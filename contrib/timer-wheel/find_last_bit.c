/* bit search implementation
 *
 * Copyright (C) 2004 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 *
 * Copyright (C) 2008 IBM Corporation
 * 'find_last_bit' is written by Rusty Russell <rusty@rustcorp.com.au>
 * (Inspired by David Howell's find_next_bit implementation)
 *
 * Rewritten by Yury Norov <yury.norov@gmail.com> to decrease
 * size and improve performance, 2015.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

/**
 * @find_last_bit
 * optimized implementation of find last bit in
 */

#ifndef BITS_PER_LONG
#ifdef __LP64__
#define BITS_PER_LONG 64
#else
#define BITS_PER_LONG 32
#endif
#endif

unsigned long gw_tw_fls (unsigned long word)
{
        int num = BITS_PER_LONG;

#if BITS_PER_LONG == 64
        if (!(word & (~0ul << 32))) {
                num -= 32;
                word <<= 32;
        }
#endif
        if (!(word & (~0ul << (BITS_PER_LONG-16)))) {
                num -= 16;
                word <<= 16;
        }
        if (!(word & (~0ul << (BITS_PER_LONG-8)))) {
                num -= 8;
                word <<= 8;
        }
        if (!(word & (~0ul << (BITS_PER_LONG-4)))) {
                num -= 4;
                word <<= 4;
        }
        if (!(word & (~0ul << (BITS_PER_LONG-2)))) {
                num -= 2;
                word <<= 2;
        }
        if (!(word & (~0ul << (BITS_PER_LONG-1))))
                num -= 1;
        return num;
}
