/*
  Copyright (c) 2012-2014 DataLab, s.l. <http://www.datalab.es>
  This file is part of GlusterFS.

  This file is licensed to you under your choice of the GNU Lesser
  General Public License, version 3 or any later version (LGPLv3 or
  later), or the GNU General Public License, version 2 (GPLv2), in all
  cases as published by the Free Software Foundation.
*/

#ifndef __EC_HELPERS_H__
#define __EC_HELPERS_H__

#include "ec-data.h"

const char * ec_bin(char * str, size_t size, uint64_t value, int32_t digits);
const char * ec_fop_name(int32_t id);
void ec_trace(const char * event, ec_fop_data_t * fop, const char * fmt, ...);
int32_t ec_bits_count(uint64_t n);
int32_t ec_bits_index(uint64_t n);
int32_t ec_bits_consume(uint64_t * n);
size_t ec_iov_copy_to(void * dst, struct iovec * vector, int32_t count,
                      off_t offset, size_t size);

int32_t ec_dict_set_array(dict_t *dict, char *key,
                          uint64_t *value, int32_t size);
int32_t ec_dict_del_array(dict_t *dict, char *key,
                          uint64_t *value, int32_t size);
int32_t ec_dict_set_number(dict_t * dict, char * key, uint64_t value);
int32_t ec_dict_del_number(dict_t * dict, char * key, uint64_t * value);
int32_t ec_dict_set_config(dict_t * dict, char * key, ec_config_t * config);
int32_t ec_dict_del_config(dict_t * dict, char * key, ec_config_t * config);

int32_t ec_loc_parent(xlator_t *xl, loc_t *loc, loc_t *parent);
int32_t ec_loc_update(xlator_t *xl, loc_t *loc, inode_t *inode,
                      struct iatt *iatt);

int32_t ec_loc_from_fd(xlator_t * xl, loc_t * loc, fd_t * fd);
int32_t ec_loc_from_loc(xlator_t * xl, loc_t * dst, loc_t * src);

void ec_owner_set(call_frame_t * frame, void * owner);
void ec_owner_copy(call_frame_t * frame, gf_lkowner_t * owner);

ec_inode_t * __ec_inode_get(inode_t * inode, xlator_t * xl);
ec_inode_t * ec_inode_get(inode_t * inode, xlator_t * xl);
ec_fd_t * __ec_fd_get(fd_t * fd, xlator_t * xl);
ec_fd_t * ec_fd_get(fd_t * fd, xlator_t * xl);

uint32_t ec_adjust_offset(ec_t * ec, off_t * offset, int32_t scale);
uint64_t ec_adjust_size(ec_t * ec, uint64_t size, int32_t scale);

static inline int32_t ec_is_power_of_2(uint32_t value)
{
    return (value != 0) && ((value & (value - 1)) == 0);
}

gf_boolean_t
ec_is_internal_xattr (dict_t *dict, char *key, data_t *value, void *data);

void
ec_filter_internal_xattrs (dict_t *xattr);

gf_boolean_t
ec_is_data_fop (glusterfs_fop_t fop);
/*
gf_boolean_t
ec_is_metadata_fop (glusterfs_fop_t fop);
*/
#endif /* __EC_HELPERS_H__ */
