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

#include <libgen.h>

#include "byte-order.h"

#include "ec-mem-types.h"
#include "ec-fops.h"
#include "ec-helpers.h"

#define BACKEND_D_OFF_BITS 63
#define PRESENT_D_OFF_BITS 63

#define ONE 1ULL
#define MASK (~0ULL)
#define PRESENT_MASK (MASK >> (64 - PRESENT_D_OFF_BITS))
#define BACKEND_MASK (MASK >> (64 - BACKEND_D_OFF_BITS))

#define TOP_BIT (ONE << (PRESENT_D_OFF_BITS - 1))
#define SHIFT_BITS (max(0, (BACKEND_D_OFF_BITS - PRESENT_D_OFF_BITS + 1)))

#ifndef ffsll
#define ffsll(x) __builtin_ffsll(x)
#endif

static const char * ec_fop_list[] =
{
    [-EC_FOP_HEAL] = "HEAL"
};

const char * ec_bin(char * str, size_t size, uint64_t value, int32_t digits)
{
    str += size;

    if (size-- < 1)
    {
        goto failed;
    }
    *--str = 0;

    while ((value != 0) || (digits > 0))
    {
        if (size-- < 1)
        {
            goto failed;
        }
        *--str = '0' + (value & 1);
        digits--;
        value >>= 1;
    }

    return str;

failed:
    return "<buffer too small>";
}

const char * ec_fop_name(int32_t id)
{
    if (id >= 0)
    {
        return gf_fop_list[id];
    }

    return ec_fop_list[-id];
}

void ec_trace(const char * event, ec_fop_data_t * fop, const char * fmt, ...)
{
    char str1[32], str2[32], str3[32];
    char * msg;
    ec_t * ec = fop->xl->private;
    va_list args;
    int32_t ret;

    va_start(args, fmt);
    ret = vasprintf(&msg, fmt, args);
    va_end(args);

    if (ret < 0)
    {
        msg = "<memory allocation error>";
    }

    gf_log("ec", GF_LOG_TRACE, "%s(%s) %p(%p) [refs=%d, winds=%d, jobs=%d] "
                               "frame=%p/%p, min/exp=%d/%d, err=%d state=%d "
                               "{%s:%s:%s} %s",
           event, ec_fop_name(fop->id), fop, fop->parent, fop->refs,
           fop->winds, fop->jobs, fop->req_frame, fop->frame, fop->minimum,
           fop->expected, fop->error, fop->state,
           ec_bin(str1, sizeof(str1), fop->mask, ec->nodes),
           ec_bin(str2, sizeof(str2), fop->remaining, ec->nodes),
           ec_bin(str3, sizeof(str3), fop->bad, ec->nodes), msg);

    if (ret >= 0)
    {
        free(msg);
    }
}

uint64_t ec_itransform(ec_t * ec, int32_t idx, uint64_t offset)
{
    int32_t bits;

    if (offset == -1ULL)
    {
        return -1ULL;
    }

    bits = ec->bits_for_nodes;
    if ((offset & ~(PRESENT_MASK >> (bits + 1))) != 0)
    {
        return TOP_BIT | ((offset >> SHIFT_BITS) & (MASK << bits)) | idx;
    }

    return (offset * ec->nodes) + idx;
}

uint64_t ec_deitransform(ec_t * ec, int32_t * idx, uint64_t offset)
{
    uint64_t mask = 0;

    if ((offset & TOP_BIT) != 0)
    {
        mask = MASK << ec->bits_for_nodes;

        *idx = offset & ~mask;
        return ((offset & ~TOP_BIT) & mask) << SHIFT_BITS;
    }

    *idx = offset % ec->nodes;

    return offset / ec->nodes;
}

int32_t ec_bits_count(uint64_t n)
{
    n -= (n >> 1) & 0x5555555555555555ULL;
    n = ((n >> 2) & 0x3333333333333333ULL) + (n & 0x3333333333333333ULL);
    n = (n + (n >> 4)) & 0x0F0F0F0F0F0F0F0FULL;
    n += n >> 8;
    n += n >> 16;
    n += n >> 32;

    return n & 0xFF;
}

int32_t ec_bits_index(uint64_t n)
{
    return ffsll(n) - 1;
}

int32_t ec_bits_consume(uint64_t * n)
{
    uint64_t tmp;

    tmp = *n;
    tmp &= -tmp;
    *n ^= tmp;

    return ffsll(tmp) - 1;
}

size_t ec_iov_copy_to(void * dst, struct iovec * vector, int32_t count,
                      off_t offset, size_t size)
{
    int32_t i = 0;
    size_t total = 0, len = 0;

    while (i < count)
    {
        if (offset < vector[i].iov_len)
        {
            while ((i < count) && (size > 0))
            {
                len = size;
                if (len > vector[i].iov_len - offset)
                {
                    len = vector[i].iov_len - offset;
                }
                memcpy(dst, vector[i++].iov_base + offset, len);
                offset = 0;
                dst += len;
                total += len;
                size -= len;
            }

            break;
        }

        offset -= vector[i].iov_len;
        i++;
    }

    return total;
}

int32_t ec_dict_set_number(dict_t * dict, char * key, uint64_t value)
{
    uint64_t * ptr;

    ptr = GF_MALLOC(sizeof(value), gf_common_mt_char);
    if (ptr == NULL)
    {
        return -1;
    }

    *ptr = hton64(value);

    return dict_set_bin(dict, key, ptr, sizeof(value));
}

int32_t ec_dict_del_number(dict_t * dict, char * key, uint64_t * value)
{
    void * ptr;
    int32_t len;

    if ((dict == NULL) || (dict_get_ptr_and_len(dict, key, &ptr, &len) != 0) ||
        (len != sizeof(uint64_t)))
    {
        return -1;
    }

    *value = ntoh64(*(uint64_t *)ptr);

    dict_del(dict, key);

    return 0;
}

int32_t ec_dict_set_config(dict_t * dict, char * key, ec_config_t * config)
{
    uint64_t * ptr, data;

    if (config->version > EC_CONFIG_VERSION)
    {
        gf_log("ec", GF_LOG_ERROR, "Trying to store an unsupported config "
                                   "version (%u)", config->version);

        return -1;
    }

    ptr = GF_MALLOC(sizeof(uint64_t), gf_common_mt_char);
    if (ptr == NULL)
    {
        return -1;
    }

    data = ((uint64_t)config->version) << 56;
    data |= ((uint64_t)config->algorithm) << 48;
    data |= ((uint64_t)config->gf_word_size) << 40;
    data |= ((uint64_t)config->bricks) << 32;
    data |= ((uint64_t)config->redundancy) << 24;
    data |= config->chunk_size;

    *ptr = hton64(data);

    return dict_set_bin(dict, key, ptr, sizeof(uint64_t));
}

int32_t ec_dict_del_config(dict_t * dict, char * key, ec_config_t * config)
{
    void * ptr;
    uint64_t data;
    int32_t len;

    if ((dict == NULL) || (dict_get_ptr_and_len(dict, key, &ptr, &len) != 0) ||
        (len != sizeof(uint64_t)))
    {
        return -1;
    }

    data = ntoh64(*(uint64_t *)ptr);

    config->version = (data >> 56) & 0xff;
    if (config->version > EC_CONFIG_VERSION)
    {
        gf_log("ec", GF_LOG_ERROR, "Found an unsupported config version (%u)",
               config->version);

        return -1;
    }

    config->algorithm = (data >> 48) & 0xff;
    config->gf_word_size = (data >> 40) & 0xff;
    config->bricks = (data >> 32) & 0xff;
    config->redundancy = (data >> 24) & 0xff;
    config->chunk_size = data & 0xffffff;

    dict_del(dict, key);

    return 0;
}

int32_t ec_loc_gfid_check(xlator_t * xl, uuid_t dst, uuid_t src)
{
    if (uuid_is_null(src))
    {
        return 1;
    }

    if (uuid_is_null(dst))
    {
        uuid_copy(dst, src);

        return 1;
    }

    if (uuid_compare(dst, src) != 0)
    {
        gf_log(xl->name, GF_LOG_WARNING, "Mismatching GFID's in loc");

        return 0;
    }

    return 1;
}

int32_t ec_loc_parent(xlator_t *xl, loc_t *loc, loc_t *parent)
{
    char * str = NULL;
    int32_t error = 0;

    memset(parent, 0, sizeof(loc_t));

    if (loc->inode == NULL)
    {
        gf_log(xl->name, GF_LOG_ERROR, "Invalid loc");

        error = EINVAL;

        goto out;
    }

    if (__is_root_gfid(loc->inode->gfid) || __is_root_gfid(loc->gfid) ||
        ((loc->path != NULL) && (strcmp(loc->path, "/") == 0)))
    {
        parent->path = gf_strdup("/");
        if (parent->path == NULL) {
            gf_log(xl->name, GF_LOG_ERROR, "Unable to duplicate path '/'");

            error = ENOMEM;

            goto out;
        }

        parent->gfid[15] = 1;
        parent->inode = inode_find(loc->inode->table, parent->gfid);

        return 0;
    }

    if (loc->path != NULL) {
        str = gf_strdup(loc->path);
        if (str == NULL)
        {
            gf_log(xl->name, GF_LOG_ERROR, "Unable to duplicate path "
                                           "'%s'", loc->path);

            error = ENOMEM;

            goto out;
        }
        parent->path = gf_strdup(dirname(str));
        if (parent->path == NULL)
        {
            gf_log(xl->name, GF_LOG_ERROR, "Unable to get dirname of "
                                           "'%s'", loc->path);

            error = ENOMEM;

            goto out;
        }
        parent->name = strrchr(parent->path, '/');
        if (parent->name == NULL)
        {
            gf_log(xl->name, GF_LOG_ERROR, "Invalid path name (%s)",
                   parent->path);

            error = EINVAL;

            goto out;
        }
        parent->name++;
    }
    if (loc->parent != NULL) {
        parent->inode = inode_ref(loc->parent);
        uuid_copy(parent->gfid, loc->parent->gfid);
    }
    if (!uuid_is_null(loc->pargfid) && uuid_is_null(parent->gfid)) {
        uuid_copy(parent->gfid, loc->pargfid);
    }

    if ((parent->inode == NULL) && (parent->path != NULL))
    {
        if (strcmp(parent->path, "/") == 0) {
            parent->inode = inode_ref(loc->inode->table->root);

            goto out;
        }
        parent->inode = inode_resolve(loc->inode->table, (char *)parent->path);
        if (parent->inode != NULL) {
            goto out;
        }

        gf_log(xl->name, GF_LOG_WARNING, "Unable to resolve parent inode");
    }

    if ((parent->inode == NULL) && !uuid_is_null(parent->gfid)) {
        if (__is_root_gfid(parent->gfid)) {
            parent->inode = inode_ref(loc->inode->table->root);

            goto out;
        }
        parent->inode = inode_find(loc->inode->table, parent->gfid);
        if (parent->inode != NULL) {
            goto out;
        }

        gf_log(xl->name, GF_LOG_WARNING, "Unable to find parent inode");
    }

    if ((parent->inode == NULL) && (parent->path == NULL) &&
        uuid_is_null(parent->gfid)) {
        gf_log(xl->name, GF_LOG_ERROR, "Parent inode missing for loc_t");

        error = EINVAL;

        goto out;
    }

out:
    GF_FREE(str);

    if (error != 0)
    {
        loc_wipe(parent);
    }

    return error;
}

int32_t ec_loc_prepare(xlator_t * xl, loc_t * loc, inode_t * inode,
                       struct iatt * iatt)
{
    if ((inode != NULL) && (loc->inode != inode))
    {
        if (loc->inode != NULL)
        {
            inode_unref(loc->inode);
        }
        loc->inode = inode_ref(inode);

        uuid_copy(loc->gfid, inode->gfid);
    }
    else if (loc->inode != NULL)
    {
        if (!ec_loc_gfid_check(xl, loc->gfid, loc->inode->gfid))
        {
            return 0;
        }
    }

    if (iatt != NULL)
    {
        if (!ec_loc_gfid_check(xl, loc->gfid, iatt->ia_gfid))
        {
            return 0;
        }
    }

    if (loc->parent != NULL)
    {
        if (!ec_loc_gfid_check(xl, loc->pargfid, loc->parent->gfid))
        {
            return 0;
        }

    }

    if (uuid_is_null(loc->gfid))
    {
        gf_log(xl->name, GF_LOG_WARNING, "GFID not available for inode");
    }

    return 1;
}

int32_t ec_loc_from_fd(xlator_t * xl, loc_t * loc, fd_t * fd)
{
    ec_fd_t * ctx;

    memset(loc, 0, sizeof(*loc));

    ctx = ec_fd_get(fd, xl);
    if (ctx != NULL)
    {
        if (loc_copy(loc, &ctx->loc) != 0)
        {
            return 0;
        }
    }

    if (ec_loc_prepare(xl, loc, fd->inode, NULL))
    {
        return 1;
    }

    loc_wipe(loc);

    return 0;
}

int32_t ec_loc_from_loc(xlator_t * xl, loc_t * dst, loc_t * src)
{
    memset(dst, 0, sizeof(*dst));

    if (loc_copy(dst, src) != 0)
    {
        return 0;
    }

    if (ec_loc_prepare(xl, dst, NULL, NULL))
    {
        return 1;
    }

    loc_wipe(dst);

    return 0;
}

void ec_owner_set(call_frame_t * frame, void * owner)
{
    set_lk_owner_from_ptr(&frame->root->lk_owner, owner);
}

void ec_owner_copy(call_frame_t * frame, gf_lkowner_t * owner)
{
    frame->root->lk_owner.len = owner->len;
    memcpy(frame->root->lk_owner.data, owner->data, owner->len);
}

ec_inode_t * __ec_inode_get(inode_t * inode, xlator_t * xl)
{
    ec_inode_t * ctx = NULL;
    uint64_t value = 0;

    if ((__inode_ctx_get(inode, xl, &value) != 0) || (value == 0))
    {
        ctx = GF_MALLOC(sizeof(*ctx), ec_mt_ec_inode_t);
        if (ctx != NULL)
        {
            memset(ctx, 0, sizeof(*ctx));

            value = (uint64_t)(uintptr_t)ctx;
            if (__inode_ctx_set(inode, xl, &value) != 0)
            {
                GF_FREE(ctx);

                return NULL;
            }
        }
    }
    else
    {
        ctx = (ec_inode_t *)(uintptr_t)value;
    }

    return ctx;
}

ec_inode_t * ec_inode_get(inode_t * inode, xlator_t * xl)
{
    ec_inode_t * ctx = NULL;

    LOCK(&inode->lock);

    ctx = __ec_inode_get(inode, xl);

    UNLOCK(&inode->lock);

    return ctx;
}

ec_fd_t * __ec_fd_get(fd_t * fd, xlator_t * xl)
{
    ec_fd_t * ctx = NULL;
    uint64_t value = 0;

    if (fd->anonymous)
    {
        return NULL;
    }

    if ((__fd_ctx_get(fd, xl, &value) != 0) || (value == 0))
    {
        ctx = GF_MALLOC(sizeof(*ctx), ec_mt_ec_fd_t);
        if (ctx != NULL)
        {
            memset(ctx, 0, sizeof(*ctx));

            value = (uint64_t)(uintptr_t)ctx;
            if (__fd_ctx_set(fd, xl, value) != 0)
            {
                GF_FREE(ctx);

                return NULL;
            }
        }
    }
    else
    {
        ctx = (ec_fd_t *)(uintptr_t)value;
    }

    return ctx;
}

ec_fd_t * ec_fd_get(fd_t * fd, xlator_t * xl)
{
    ec_fd_t * ctx = NULL;

    if (!fd->anonymous)
    {
        LOCK(&fd->lock);

        ctx = __ec_fd_get(fd, xl);

        UNLOCK(&fd->lock);
    }

    return ctx;
}

uint32_t ec_adjust_offset(ec_t * ec, off_t * offset, int32_t scale)
{
    off_t head, tmp;

    tmp = *offset;
    head = tmp % ec->stripe_size;
    tmp -= head;
    if (scale)
    {
        tmp /= ec->fragments;
    }

    *offset = tmp;

    return head;
}

uint64_t ec_adjust_size(ec_t * ec, uint64_t size, int32_t scale)
{
    size += ec->stripe_size - 1;
    size -= size % ec->stripe_size;
    if (scale)
    {
        size /= ec->fragments;
    }

    return size;
}
