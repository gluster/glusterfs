/*
  Copyright (c) 2012-2014 DataLab, s.l. <http://www.datalab.es>
  This file is part of GlusterFS.

  This file is licensed to you under your choice of the GNU Lesser
  General Public License, version 3 or any later version (LGPLv3 or
  later), or the GNU General Public License, version 2 (GPLv2), in all
  cases as published by the Free Software Foundation.
*/

#ifndef __EC_COMBINE_H__
#define __EC_COMBINE_H__

#define EC_COMBINE_DICT  0
#define EC_COMBINE_XDATA 1

typedef int32_t (* ec_combine_f)(ec_fop_data_t * fop, ec_cbk_data_t * dst,
                                 ec_cbk_data_t * src);

void ec_iatt_rebuild(ec_t * ec, struct iatt * iatt, int32_t count,
                     int32_t answers);

int32_t ec_iatt_combine(ec_fop_data_t *fop, struct iatt *dst, struct iatt *src,
                        int32_t count);
int32_t ec_dict_compare(dict_t * dict1, dict_t * dict2);
int32_t ec_vector_compare(struct iovec * dst_vector, int32_t dst_count,
                          struct iovec * src_vector, int32_t src_count);
int32_t ec_flock_compare(struct gf_flock * dst, struct gf_flock * src);
void ec_statvfs_combine(struct statvfs * dst, struct statvfs * src);

int32_t ec_dict_combine(ec_cbk_data_t * cbk, int32_t which);

void ec_combine(ec_cbk_data_t * cbk, ec_combine_f combine);

int32_t
ec_combine_write (ec_fop_data_t *fop, ec_cbk_data_t *dst,
                  ec_cbk_data_t *src);
#endif /* __EC_COMBINE_H__ */
