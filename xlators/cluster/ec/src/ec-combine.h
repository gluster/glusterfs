/*
  Copyright (c) 2012 DataLab, s.l. <http://www.datalab.es>

  This file is part of the cluster/ec translator for GlusterFS.

  The cluster/ec translator for GlusterFS is free software: you can
  redistribute it and/or modify it under the terms of the GNU General
  Public License as published by the Free Software Foundation, either
  version 3 of the License, or (at your option) any later version.

  The cluster/ec translator for GlusterFS is distributed in the hope
  that it will be useful, but WITHOUT ANY WARRANTY; without even the
  implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
  PURPOSE. See the GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with the cluster/ec translator for GlusterFS. If not, see
  <http://www.gnu.org/licenses/>.
*/

#ifndef __EC_COMBINE_H__
#define __EC_COMBINE_H__

#define EC_COMBINE_DICT  0
#define EC_COMBINE_XDATA 1

typedef int32_t (* ec_combine_f)(ec_fop_data_t * fop, ec_cbk_data_t * dst,
                                 ec_cbk_data_t * src);

void ec_iatt_rebuild(ec_t * ec, struct iatt * iatt, int32_t count,
                     int32_t answers);

int32_t ec_iatt_combine(struct iatt * dst, struct iatt * src, int32_t count);
int32_t ec_dict_compare(dict_t * dict1, dict_t * dict2);
int32_t ec_vector_compare(struct iovec * dst_vector, int32_t dst_count,
                          struct iovec * src_vector, int32_t src_count);
int32_t ec_flock_compare(struct gf_flock * dst, struct gf_flock * src);
void ec_statvfs_combine(struct statvfs * dst, struct statvfs * src);

int32_t ec_dict_combine(ec_cbk_data_t * cbk, int32_t which);

void ec_combine(ec_cbk_data_t * cbk, ec_combine_f combine);

#endif /* __EC_COMBINE_H__ */
