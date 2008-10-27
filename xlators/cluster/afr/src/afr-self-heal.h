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

#ifndef __AFR_SELF_HEAL_H__
#define __AFR_SELF_HEAL_H__

#include <sys/stat.h>

#define FILETYPE_DIFFERS(buf1,buf2) ((S_IFMT & ((struct stat *)buf1)->st_mode) != (S_IFMT & ((struct stat *)buf2)->st_mode))
#define PERMISSION_DIFFERS(buf1,buf2) ((((struct stat *)buf1)->st_mode) != (((struct stat *)buf2)->st_mode))
#define OWNERSHIP_DIFFERS(buf1,buf2) ((((struct stat *)buf1)->st_uid) != (((struct stat *)buf2)->st_uid) || (((struct stat *)buf1)->st_gid != (((struct stat *)buf2)->st_gid)))
#define SIZE_DIFFERS(buf1,buf2) ((((struct stat *)buf1)->st_size) != (((struct stat *)buf2)->st_size))



int
afr_sh_has_metadata_pending (dict_t *xattr, xlator_t *this);
int
afr_sh_has_entry_pending (dict_t *xattr, xlator_t *this);
int
afr_sh_has_data_pending (dict_t *xattr, xlator_t *this);

int
afr_self_heal_entry (call_frame_t *frame, xlator_t *this);

int
afr_self_heal_data (call_frame_t *frame, xlator_t *this);

int
afr_self_heal_metadata (call_frame_t *frame, xlator_t *this);

int
afr_self_heal (call_frame_t *frame, xlator_t *this,
	       int (*completion_cbk) (call_frame_t *, xlator_t *));

#endif /* __AFR_SELF_HEAL_H__ */
