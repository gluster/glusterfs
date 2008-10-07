/*
   Copyright (c) 2007, 2008 Z RESEARCH, Inc. <http://www.zresearch.com>
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

#ifndef __TRANSACTION_H__
#define __TRANSACTION_H__

#define AFR_METADATA_PENDING "trusted.glusterfs-afr.metadata-pending"

#define AFR_DATA_PENDING "trusted.glusterfs-afr.data-pending"

#define AFR_ENTRY_PENDING "trusted.glusterfs-afr.entry-pending"

int32_t
afr_inode_transaction (call_frame_t *frame, afr_private_t *priv);

int32_t
afr_dir_transaction (call_frame_t *frame, afr_private_t *priv);

int32_t
afr_dir_link_transaction (call_frame_t *frame, afr_private_t *priv);

void
build_loc_from_fd (loc_t *loc, fd_t *fd);

#endif /* __TRANSACTION_H__ */
