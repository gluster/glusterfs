/*
  Copyright (c) 2008-2013 Red Hat, Inc. <http://www.redhat.com>
  This file is part of GlusterFS.

  This file is licensed to you under your choice of the GNU Lesser
  General Public License, version 3 or any later version (LGPLv3 or
  later), or the GNU General Public License, version 2 (GPLv2), in all
  cases as published by the Free Software Foundation.
*/


#ifndef __CRYPT_MEM_TYPES_H__
#define __CRYPT_MEM_TYPES_H__

#include "mem-types.h"

enum gf_crypt_mem_types_ {
	gf_crypt_mt_priv = gf_common_mt_end + 1,
	gf_crypt_mt_inode,
	gf_crypt_mt_data,
	gf_crypt_mt_mtd,
	gf_crypt_mt_loc,
	gf_crypt_mt_iatt,
	gf_crypt_mt_key,
	gf_crypt_mt_iovec,
	gf_crypt_mt_char,
	gf_crypt_mt_local,
	gf_crypt_mt_end,
};

#endif /* __CRYPT_MEM_TYPES_H__ */

/*
  Local variables:
  c-indentation-style: "K&R"
  mode-name: "LC"
  c-basic-offset: 8
  tab-width: 8
  fill-column: 80
  scroll-step: 1
  End:
*/



