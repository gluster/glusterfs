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


#include <stdint.h>
#include <stdio.h>
#include <string.h>


#define DELTA 0x9E3779B9
#define FULLROUNDS 10		/* 32 is overkill, 16 is strong crypto */
#define PARTROUNDS 6		/* 6 gets complete mixing */


static int
tearound (int rounds, uint32_t *array, uint32_t *h0, uint32_t *h1)
{
	uint32_t sum = 0;
	int      n = 0;
	uint32_t b0  = 0;
	uint32_t b1  = 0;

	b0 = *h0;
	b1 = *h1;

	n = rounds;

	do {
		sum += DELTA;
		b0  += ((b1 << 4) + array[0])
			^ (b1 + sum)
			^ ((b1 >> 5) + array[1]);
		b1  += ((b0 << 4) + array[2])
			^ (b0 + sum)
			^ ((b0 >> 5) + array[3]);
	} while (--n);

	*h0 += b0;
	*h1 += b1;

	return 0;
}


uint32_t
__pad (int len)
{
	uint32_t pad = 0;

	pad = (uint32_t) len | ((uint32_t) len << 8);
	pad |= pad << 16;

	return pad;
}


uint32_t
dht_hashfn_tea (const char *msg, int len)
{
	uint32_t  h0 = 0x9464a485;
	uint32_t  h1 = 0x542e1a94;
	uint32_t  array[4];
	uint32_t  pad = 0;
	int       i = 0;
	int       j = 0;
	int       full_quads = 0;
	int       full_words = 0;
	int       full_bytes = 0;
	uint32_t *intmsg = NULL;
	int       word = 0;


	intmsg = (uint32_t *) msg;
	pad = __pad (len);

	full_bytes   = len;
	full_words   = len / 4;
	full_quads   = len / 16;

	for (i = 0; i < full_quads; i++) {
		for (j = 0; j < 4; j++) {
			word     = *intmsg;
			array[j] = word;
			intmsg++;
			full_words--;
			full_bytes -= 4;
		}
		tearound (PARTROUNDS, &array[0], &h0, &h1);
	}

	if ((len % 16) == 0) {
		goto done;
	}

	for (j = 0; j < 4; j++) {
		if (full_words) {
			word     = *intmsg;
			array[j] = word;
			intmsg++;
			full_words--;
			full_bytes -= 4;
		} else {
			array[j] = pad;
			while (full_bytes) {
				array[j] <<= 8;
				array[j] |= msg[len - full_bytes];
				full_bytes--;
			}
		}
	}
	tearound (FULLROUNDS, &array[0], &h0, &h1);

done:
	return h0 ^ h1;
}


#if 0
int
main (int argc, char *argv[])
{
	int i = 0;
	int hashval = 0;

	for (i = 1; i < argc; i++) {
		hashval = tea (argv[i], strlen (argv[i]));
		printf ("%s: %x\n", argv[i], hashval);
	}
}
#endif
