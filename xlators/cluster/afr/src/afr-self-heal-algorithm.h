/*
   Copyright (c) 2009 Z RESEARCH, Inc. <http://www.zresearch.com>
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

#ifndef __AFR_SELF_HEAL_ALGORITHM_H__
#define __AFR_SELF_HEAL_ALGORITHM_H__


typedef int (*afr_sh_algo_fn) (call_frame_t *frame,
                               xlator_t *this);

struct afr_sh_algorithm {
        const char *name;
        afr_sh_algo_fn fn;
};

struct afr_sh_algorithm afr_self_heal_algorithms[2];

typedef struct {
        uint8_t *checksum;     /* array of MD5 checksums for each child
                                  Each checksum is MD5_DIGEST_LEN bytes long */

        unsigned char *write_needed;
        size_t block_size;
} afr_sh_algo_diff_private_t;

#endif /* __AFR_SELF_HEAL_ALGORITHM_H__ */
