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

#ifndef __DIR_READ_H__
#define __DIR_READ_H__


int32_t
afr_opendir (call_frame_t *frame, xlator_t *this,
	     loc_t *loc, fd_t *fd);

int32_t
afr_closedir (call_frame_t *frame, xlator_t *this,
	      fd_t *fd);

int32_t
afr_readdir (call_frame_t *frame, xlator_t *this,
	     fd_t *fd, size_t size, off_t offset);


int32_t
afr_getdents (call_frame_t *frame, xlator_t *this,
	      fd_t *fd, size_t size, off_t offset, int32_t flag);


int32_t
afr_checksum (call_frame_t *frame, xlator_t *this,
	      loc_t *loc, int32_t flags);


#endif /* __DIR_READ_H__ */
