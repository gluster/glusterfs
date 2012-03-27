/*
  Copyright (c) 2010-2011 Gluster, Inc. <http://www.gluster.com>
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

#include <openssl/md5.h>
#include <stdint.h>

#include "glusterfs.h"

/*
 * The "weak" checksum required for the rsync algorithm,
 * adapted from the rsync source code. The following comment
 * appears there:
 *
 * "a simple 32 bit checksum that can be upadted from either end
 *  (inspired by Mark Adler's Adler-32 checksum)"
 *
 * Note: these functions are only called to compute checksums on
 * pathnames; they don't need to handle arbitrarily long strings of
 * data. Thus int32_t and uint32_t are sufficient
 */

uint32_t
gf_rsync_weak_checksum (unsigned char *buf, size_t len)
{
        int32_t i = 0;
        uint32_t s1, s2;

        uint32_t csum;

        s1 = s2 = 0;
        if (len >= 4) {
                for (; i < (len-4); i+=4) {
                        s2 += 4*(s1 + buf[i]) + 3*buf[i+1] + 2*buf[i+2] + buf[i+3];
                        s1 += buf[i+0] + buf[i+1] + buf[i+2] + buf[i+3];
                }
        }

        for (; i < len; i++) {
                s1 += buf[i];
                s2 += s1;
        }

        csum = (s1 & 0xffff) + (s2 << 16);

        return csum;
}


/*
 * The "strong" checksum required for the rsync algorithm,
 * adapted from the rsync source code.
 */

void
gf_rsync_strong_checksum (unsigned char *data, size_t len, unsigned char *md5)
{
        MD5(data, len, md5);
}
