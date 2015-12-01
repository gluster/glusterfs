/*
  Copyright (c) 2012-2014 DataLab, s.l. <http://www.datalab.es>
  This file is part of GlusterFS.

  This file is licensed to you under your choice of the GNU Lesser
  General Public License, version 3 or any later version (LGPLv3 or
  later), or the GNU General Public License, version 2 (GPLv2), in all
  cases as published by the Free Software Foundation.
*/

#ifndef __EC_H__
#define __EC_H__

#define EC_XATTR_PREFIX  "trusted.ec."
#define EC_XATTR_CONFIG  EC_XATTR_PREFIX"config"
#define EC_XATTR_SIZE    EC_XATTR_PREFIX"size"
#define EC_XATTR_VERSION EC_XATTR_PREFIX"version"
#define EC_XATTR_HEAL    EC_XATTR_PREFIX"heal"
#define EC_XATTR_DIRTY   EC_XATTR_PREFIX"dirty"

#define EC_VERSION_SIZE 2
#define EC_SHD_INODE_LRU_LIMIT          10

#endif /* __EC_H__ */
