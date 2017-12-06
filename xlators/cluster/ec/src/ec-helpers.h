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

#include "ec-types.h"

#define EC_ERR(_x) ((void *)-(intptr_t)(_x))
#define EC_IS_ERR(_x) (((uintptr_t)(_x) & ~0xfffULL) == ~0xfffULL)
#define EC_GET_ERR(_x) ((int32_t)(intptr_t)(_x))

#define EC_ALIGN_CHECK(_ptr, _align) \
    ((((uintptr_t)(_ptr)) & ((_align) - 1)) == 0)

const char * ec_bin(char * str, size_t size, uint64_t value, int32_t digits);
const char * ec_fop_name(int32_t id);
void ec_trace(const char * event, ec_fop_data_t * fop, const char * fmt, ...);
int32_t ec_bits_consume(uint64_t * n);
size_t ec_iov_copy_to(void * dst, struct iovec * vector, int32_t count,
                      off_t offset, size_t size);
int32_t ec_buffer_alloc(xlator_t *xl, size_t size, struct iobref **piobref,
                        void **ptr);
int32_t ec_dict_set_array(dict_t *dict, char *key,
                          uint64_t *value, int32_t size);
int32_t ec_dict_get_array (dict_t *dict, char *key, uint64_t value[],
                           int32_t size);

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

static inline uint32_t
ec_adjust_size_down(ec_t *ec, uint64_t *value, gf_boolean_t scale)
{
        uint64_t head, tmp;

        tmp = *value;
        head = tmp % ec->stripe_size;
        tmp -= head;

        if (scale) {
                tmp /= ec->fragments;
        }

        *value = tmp;

        return (uint32_t)head;
}

/* This function can cause an overflow if the passed value is too near to the
 * uint64_t limit. If this happens, it returns the tail in negative form and
 * the value is set to UINT64_MAX. */
static inline int32_t
ec_adjust_size_up(ec_t *ec, uint64_t *value, gf_boolean_t scale)
{
        uint64_t tmp;
        int32_t tail;

        tmp = *value;
        /* We first adjust the value down. This never causes overflow. */
        tail = ec_adjust_size_down(ec, &tmp, scale);

        /* If the value was already aligned, tail will be 0 and nothing else
         * needs to be done. */
        if (tail != 0) {
                /* Otherwise, we need to compute the real tail and adjust the
                 * returned value to the next stripe. */
                tail = ec->stripe_size - tail;
                if (scale) {
                        tmp += ec->fragment_size;
                } else {
                        tmp += ec->stripe_size;
                        /* If no scaling is requested there's a posibility of
                         * overflow. */
                        if (tmp < ec->stripe_size) {
                                tmp = UINT64_MAX;
                                tail = -tail;
                        }
                }
        }

        *value = tmp;

        return tail;
}

/* This function is equivalent to ec_adjust_size_down() but with a potentially
 * different parameter size (off_t vs uint64_t). */
static inline uint32_t
ec_adjust_offset_down(ec_t *ec, off_t *value, gf_boolean_t scale)
{
        off_t head, tmp;

        tmp = *value;
        head = tmp % ec->stripe_size;
        tmp -= head;

        if (scale) {
                tmp /= ec->fragments;
        }

        *value = tmp;

        return (uint32_t)head;
}

/* This function is equivalent to ec_adjust_size_up() but with a potentially
 * different parameter size (off_t vs uint64_t). */
static inline int32_t
ec_adjust_offset_up(ec_t *ec, off_t *value, gf_boolean_t scale)
{
        uint64_t tail, tmp;

        /* An offset is a signed type that can only have positive values, so
         * we take advantage of this to avoid overflows. We simply convert it
         * to an unsigned integer and operate normally. This won't cause an
         * overflow. Overflow is only checked when converting back to an
         * off_t. */
        tmp = *value;
        tail = ec->stripe_size;
        tail -= (tmp + tail - 1) % tail + 1;
        tmp += tail;
        if (scale) {
                /* If we are scaling, we'll never get an overflow. */
                tmp /= ec->fragments;
        } else {
                /* Check if there has been an overflow. */
                if ((off_t)tmp < 0) {
                        tmp = GF_OFF_MAX;
                        tail = -tail;
                }
        }

        *value = (off_t)tmp;

        return (int32_t)tail;
}

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

int32_t
ec_launch_replace_heal (ec_t *ec);

#endif /* __EC_HELPERS_H__ */
