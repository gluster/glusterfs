/*
  Copyright (c) 2008-2012 Red Hat, Inc. <http://www.redhat.com>
  This file is part of GlusterFS.

  This file is licensed to you under your choice of the GNU Lesser
  General Public License, version 3 or any later version (LGPLv3 or
  later), or the GNU General Public License, version 2 (GPLv2), in all
  cases as published by the Free Software Foundation.
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
afr_self_heal_entry (call_frame_t *frame, xlator_t *this);

int
afr_self_heal_data (call_frame_t *frame, xlator_t *this);

int
afr_self_heal_metadata (call_frame_t *frame, xlator_t *this);

int
afr_self_heal_get_source (xlator_t *this, afr_local_t *local, dict_t **xattr);

int
afr_self_heal (call_frame_t *frame, xlator_t *this, inode_t *inode);

int
afr_lookup_select_read_child_by_txn_type (xlator_t *this, afr_local_t *local,
                                          dict_t **xattr,
                                          afr_transaction_type txn_type,
                                          uuid_t gfid);
#endif /* __AFR_SELF_HEAL_H__ */
