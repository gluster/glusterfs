/*
  Copyright (c) 2008-2010 Gluster, Inc. <http://www.gluster.com>
  This file is part of GlusterFS.

  GlusterFS is free software; you can redistribute it and/or modify
  it under the terms of the GNU Affero General Public License as published
  by the Free Software Foundation; either version 3 of the License,
  or (at your option) any later version.

  GlusterFS is distributed in the hope that it will be useful, but
  WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
  Affero General Public License for more details.

  You should have received a copy of the GNU Affero General Public License
  along with this program.  If not, see
  <http://www.gnu.org/licenses/>.
*/

#ifndef __QR_MEM_TYPES_H__
#define __QR_MEM_TYPES_H__

#include "mem-types.h"

enum gf_qr_mem_types_ {
        gf_qr_mt_qr_inode_t   = gf_common_mt_end + 1,
        gf_qr_mt_qr_fd_ctx_t,
        gf_qr_mt_qr_local_t,
        gf_qr_mt_iovec,
        gf_qr_mt_qr_conf_t,
        gf_qr_mt_qr_priority_t,
        gf_qr_mt_qr_private_t,
        gf_qr_mt_end
};
#endif
