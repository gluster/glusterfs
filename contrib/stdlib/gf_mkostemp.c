/* Borrowed from glibc-2.16/sysdeps/posix/tempname.c */

/* Copyright (C) 1991-2001, 2006, 2007, 2009 Free Software Foundation, Inc.
   This file is part of the GNU C Library.

   The GNU C Library is free software; you can redistribute it and/or
   modify it under the terms of the GNU Lesser General Public
   License as published by the Free Software Foundation; either
   version 2.1 of the License, or (at your option) any later version.

   The GNU C Library is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   Lesser General Public License for more details.

   You should have received a copy of the GNU Lesser General Public
   License along with the GNU C Library; if not, see
   <http://www.gnu.org/licenses/>.  */

#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <errno.h>
#include <sys/time.h>
#include <fcntl.h>
#include <sys/types.h>
#include <time.h>
#include <inttypes.h>

static const char letters[] =
"abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";

/* Generate a temporary file name based on TMPL.  TMPL must match the
   rules for mk[s]temp (i.e. end in "XXXXXX", possibly with a suffix).
*/

#if !defined(TMP_MAX)
#define TMP_MAX 238328
#endif

int
gf_mkostemp (char *tmpl, int suffixlen, int flags)
{
        int len;
        char *XXXXXX;
        static uint64_t value;
        uint64_t random_time_bits;
        unsigned int count;
        int fd = -1;

  /* A lower bound on the number of temporary files to attempt to
     generate.  The maximum total number of temporary file names that
     can exist for a given template is 62**6.  It should never be
     necessary to try all these combinations.  Instead if a reasonable
     number of names is tried (we define reasonable as 62**3) fail to
     give the system administrator the chance to remove the problems.  */

        unsigned int attempts = TMP_MAX; /* TMP_MAX == 62³ */

        len = strlen (tmpl);
        if (len < 6 + suffixlen || memcmp (&tmpl[len - 6 - suffixlen],
                                           "XXXXXX", 6))
                return -1;

  /* This is where the Xs start.  */
        XXXXXX = &tmpl[len - 6 - suffixlen];

  /* Get some more or less random data.  */
# if HAVE_GETTIMEOFDAY
        struct timeval tv;
        gettimeofday (&tv, NULL);
        random_time_bits = ((uint64_t) tv.tv_usec << 16) ^ tv.tv_sec;
# else
        random_time_bits = time (NULL);
# endif

        value += random_time_bits ^ getpid ();

        for (count = 0; count < attempts; value += 7777, ++count) {
                uint64_t v = value;

                /* Fill in the random bits.  */
                XXXXXX[0] = letters[v % 62];
                v /= 62;
                XXXXXX[1] = letters[v % 62];
                v /= 62;
                XXXXXX[2] = letters[v % 62];
                v /= 62;
                XXXXXX[3] = letters[v % 62];
                v /= 62;
                XXXXXX[4] = letters[v % 62];
                v /= 62;
                XXXXXX[5] = letters[v % 62];

                fd = open (tmpl, (flags & ~O_ACCMODE)
                           | O_RDWR | O_CREAT | O_EXCL, S_IRUSR | S_IWUSR);

                if (fd >= 0)
                        return fd;
                else if (errno != EEXIST)
                        return -1;
        }

        /* We got out of the loop because we ran out of combinations to try.  */
        return -1;
}
