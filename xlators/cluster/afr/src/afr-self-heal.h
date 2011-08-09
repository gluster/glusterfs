/*
   Copyright (c) 2008-2011 Gluster, Inc. <http://www.gluster.com>
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

#define FILETYPE_DIFFERS(buf1,buf2) ((buf1)->ia_type != (buf2)->ia_type)
#define PERMISSION_DIFFERS(buf1,buf2) (st_mode_from_ia ((buf1)->ia_prot, (buf1)->ia_type) != st_mode_from_ia ((buf2)->ia_prot, (buf2)->ia_type))
#define OWNERSHIP_DIFFERS(buf1,buf2) (((buf1)->ia_uid != (buf2)->ia_uid) || ((buf1)->ia_gid != (buf2)->ia_gid))
#define SIZE_DIFFERS(buf1,buf2) ((buf1)->ia_size != (buf2)->ia_size)

#define SIZE_GREATER(buf1,buf2) ((buf1)->ia_size > (buf2)->ia_size)

int
afr_sh_has_metadata_pending (dict_t *xattr, int child_count, xlator_t *this);
int
afr_sh_has_entry_pending (dict_t *xattr, int child_count, xlator_t *this);
int
afr_sh_has_data_pending (dict_t *xattr, int child_count, xlator_t *this);

int
afr_self_heal_entry (call_frame_t *frame, xlator_t *this);

int
afr_self_heal_data (call_frame_t *frame, xlator_t *this);

int
afr_self_heal_metadata (call_frame_t *frame, xlator_t *this);

void
afr_self_heal_find_sources (xlator_t *this, afr_local_t *local, dict_t **xattr,
                            afr_transaction_type transaction_type);

int
afr_self_heal (call_frame_t *frame, xlator_t *this);

#endif /* __AFR_SELF_HEAL_H__ */
