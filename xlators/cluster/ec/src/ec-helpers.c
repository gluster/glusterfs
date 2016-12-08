/*
  Copyright (c) 2012-2014 DataLab, s.l. <http://www.datalab.es>
  This file is part of GlusterFS.

  This file is licensed to you under your choice of the GNU Lesser
  General Public License, version 3 or any later version (LGPLv3 or
  later), or the GNU General Public License, version 2 (GPLv2), in all
  cases as published by the Free Software Foundation.
*/

#include <libgen.h>

#include "byte-order.h"

#include "ec.h"
#include "ec-mem-types.h"
#include "ec-messages.h"
#include "ec-fops.h"
#include "ec-method.h"
#include "ec-helpers.h"

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

    gf_msg_trace ("ec", 0, "%s(%s) %p(%p) [refs=%d, winds=%d, jobs=%d] "
                               "frame=%p/%p, min/exp=%d/%d, err=%d state=%d "
                               "{%s:%s:%s} %s",
           event, ec_fop_name(fop->id), fop, fop->parent, fop->refs,
           fop->winds, fop->jobs, fop->req_frame, fop->frame, fop->minimum,
           fop->expected, fop->error, fop->state,
           ec_bin(str1, sizeof(str1), fop->mask, ec->nodes),
           ec_bin(str2, sizeof(str2), fop->remaining, ec->nodes),
           ec_bin(str3, sizeof(str3), fop->good, ec->nodes), msg);

    if (ret >= 0)
    {
        free(msg);
    }
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

int32_t ec_buffer_alloc(xlator_t *xl, size_t size, struct iobref **piobref,
                        void **ptr)
{
    struct iobref *iobref = NULL;
    struct iobuf *iobuf = NULL;
    int32_t ret = -ENOMEM;

    iobuf = iobuf_get_page_aligned (xl->ctx->iobuf_pool, size,
                                    EC_METHOD_WORD_SIZE);
    if (iobuf == NULL) {
        goto out;
    }

    iobref = *piobref;
    if (iobref == NULL) {
        iobref = iobref_new();
        if (iobref == NULL) {
            goto out;
        }
    }

    ret = iobref_add(iobref, iobuf);
    if (ret != 0) {
        if (iobref != *piobref) {
            iobref_unref(iobref);
        }
        iobref = NULL;

        goto out;
    }

    GF_ASSERT(EC_ALIGN_CHECK(iobuf->ptr, EC_METHOD_WORD_SIZE));

    *ptr = iobuf->ptr;

out:
    if (iobuf != NULL) {
        iobuf_unref(iobuf);
    }

    if (iobref != NULL) {
        *piobref = iobref;
    }

    return ret;
}

int32_t ec_dict_set_array(dict_t *dict, char *key, uint64_t value[],
                          int32_t size)
{
    int         ret = -1;
    uint64_t   *ptr = NULL;
    int32_t     vindex;

    if (value == NULL) {
        return -EINVAL;
    }

    ptr = GF_MALLOC(sizeof(uint64_t) * size, gf_common_mt_char);
    if (ptr == NULL) {
        return -ENOMEM;
    }
    for (vindex = 0; vindex < size; vindex++) {
         ptr[vindex] = hton64(value[vindex]);
    }
    ret = dict_set_bin(dict, key, ptr, sizeof(uint64_t) * size);
    if (ret)
         GF_FREE (ptr);
    return ret;
}


int32_t ec_dict_del_array(dict_t *dict, char *key, uint64_t value[],
                          int32_t size)
{
    void    *ptr;
    int32_t len;
    int32_t vindex;
    int32_t old_size = 0;
    int32_t err;

    if (dict == NULL) {
        return -EINVAL;
    }
    err = dict_get_ptr_and_len(dict, key, &ptr, &len);
    if (err != 0) {
        return err;
    }

    if (len > (size * sizeof(uint64_t)) || (len % sizeof (uint64_t))) {
        return -EINVAL;
    }

    memset (value, 0, size * sizeof(uint64_t));
    /* 3.6 version ec would have stored version in 64 bit. In that case treat
     * metadata versions same as data*/
    old_size = min (size, len/sizeof(uint64_t));
    for (vindex = 0; vindex < old_size; vindex++) {
         value[vindex] = ntoh64(*((uint64_t *)ptr + vindex));
    }

    if (old_size < size) {
            for (vindex = old_size; vindex < size; vindex++) {
                 value[vindex] = value[old_size-1];
            }
    }

    dict_del(dict, key);

    return 0;
}


int32_t ec_dict_set_number(dict_t * dict, char * key, uint64_t value)
{
    int        ret = -1;
    uint64_t * ptr;

    ptr = GF_MALLOC(sizeof(value), gf_common_mt_char);
    if (ptr == NULL) {
        return -ENOMEM;
    }

    *ptr = hton64(value);

    ret = dict_set_bin(dict, key, ptr, sizeof(value));
    if (ret)
        GF_FREE (ptr);

    return ret;
}

int32_t ec_dict_del_number(dict_t * dict, char * key, uint64_t * value)
{
    void * ptr;
    int32_t len, err;

    if (dict == NULL) {
        return -EINVAL;
    }
    err = dict_get_ptr_and_len(dict, key, &ptr, &len);
    if (err != 0) {
        return err;
    }
    if (len != sizeof(uint64_t)) {
        return -EINVAL;
    }

    *value = ntoh64(*(uint64_t *)ptr);

    dict_del(dict, key);

    return 0;
}

int32_t ec_dict_set_config(dict_t * dict, char * key, ec_config_t * config)
{
    int ret = -1;
    uint64_t * ptr, data;

    if (config->version > EC_CONFIG_VERSION)
    {
        gf_msg ("ec", GF_LOG_ERROR, EINVAL,
                EC_MSG_UNSUPPORTED_VERSION,
                "Trying to store an unsupported config "
                "version (%u)", config->version);

        return -EINVAL;
    }

    ptr = GF_MALLOC(sizeof(uint64_t), gf_common_mt_char);
    if (ptr == NULL)
    {
        return -ENOMEM;
    }

    data = ((uint64_t)config->version) << 56;
    data |= ((uint64_t)config->algorithm) << 48;
    data |= ((uint64_t)config->gf_word_size) << 40;
    data |= ((uint64_t)config->bricks) << 32;
    data |= ((uint64_t)config->redundancy) << 24;
    data |= config->chunk_size;

    *ptr = hton64(data);

    ret = dict_set_bin(dict, key, ptr, sizeof(uint64_t));
    if (ret)
        GF_FREE (ptr);

    return ret;
}

int32_t ec_dict_del_config(dict_t * dict, char * key, ec_config_t * config)
{
    void * ptr;
    uint64_t data;
    int32_t len, err;

    if (dict == NULL) {
        return -EINVAL;
    }
    err = dict_get_ptr_and_len(dict, key, &ptr, &len);
    if (err != 0) {
        return err;
    }
    if (len != sizeof(uint64_t)) {
        return -EINVAL;
    }

    data = ntoh64(*(uint64_t *)ptr);
    /* Currently we need to get the config xattr for entries of type IA_INVAL.
     * These entries can later become IA_DIR entries (after inode_link()),
     * which don't have a config xattr. However, since the xattr is requested
     * using an xattrop() fop, it will always return a config full of 0's
     * instead of saying that it doesn't exist.
     *
     * We need to filter out this case and consider that a config xattr == 0 is
     * the same than a non-existant xattr. Otherwise ec_config_check() will
     * fail.
     */
    if (data == 0) {
        return -ENODATA;
    }

    config->version = (data >> 56) & 0xff;
    if (config->version > EC_CONFIG_VERSION)
    {
        gf_msg ("ec", GF_LOG_ERROR, EINVAL,
                EC_MSG_UNSUPPORTED_VERSION,
                "Found an unsupported config version (%u)",
                config->version);

        return -EINVAL;
    }

    config->algorithm = (data >> 48) & 0xff;
    config->gf_word_size = (data >> 40) & 0xff;
    config->bricks = (data >> 32) & 0xff;
    config->redundancy = (data >> 24) & 0xff;
    config->chunk_size = data & 0xffffff;

    dict_del(dict, key);

    return 0;
}

gf_boolean_t ec_loc_gfid_check(xlator_t *xl, uuid_t dst, uuid_t src)
{
    if (gf_uuid_is_null(src)) {
        return _gf_true;
    }

    if (gf_uuid_is_null(dst)) {
        gf_uuid_copy(dst, src);

        return _gf_true;
    }

    if (gf_uuid_compare(dst, src) != 0) {
        gf_msg (xl->name, GF_LOG_WARNING, 0,
                EC_MSG_GFID_MISMATCH,
                "Mismatching GFID's in loc");

        return _gf_false;
    }

    return _gf_true;
}

int32_t ec_loc_setup_inode(xlator_t *xl, inode_table_t *table, loc_t *loc)
{
    int32_t ret = -EINVAL;

    if (loc->inode != NULL) {
        if (!ec_loc_gfid_check(xl, loc->gfid, loc->inode->gfid)) {
            goto out;
        }
    } else if (table != NULL) {
        if (!gf_uuid_is_null(loc->gfid)) {
            loc->inode = inode_find(table, loc->gfid);
        } else if (loc->path && strchr (loc->path, '/')) {
            loc->inode = inode_resolve(table, (char *)loc->path);
        }
    }

    ret = 0;

out:
    return ret;
}

int32_t ec_loc_setup_parent(xlator_t *xl, inode_table_t *table, loc_t *loc)
{
    char *path, *parent;
    int32_t ret = -EINVAL;

    if (loc->parent != NULL) {
        if (!ec_loc_gfid_check(xl, loc->pargfid, loc->parent->gfid)) {
            goto out;
        }
    } else if (table != NULL) {
        if (!gf_uuid_is_null(loc->pargfid)) {
            loc->parent = inode_find(table, loc->pargfid);
        } else if (loc->path && strchr (loc->path, '/')) {
            path = gf_strdup(loc->path);
            if (path == NULL) {
                gf_msg (xl->name, GF_LOG_ERROR, ENOMEM,
                        EC_MSG_NO_MEMORY,
                        "Unable to duplicate path '%s'",
                        loc->path);

                ret = -ENOMEM;

                goto out;
            }
            parent = dirname(path);
            loc->parent = inode_resolve(table, parent);
            if (loc->parent != NULL) {
                gf_uuid_copy(loc->pargfid, loc->parent->gfid);
            }
            GF_FREE(path);
        }
    }

    /* If 'pargfid' has not been determined, clear 'name' to avoid resolutions
       based on <gfid:pargfid>/name. */
    if (gf_uuid_is_null(loc->pargfid)) {
        loc->name = NULL;
    }

    ret = 0;

out:
    return ret;
}

int32_t ec_loc_setup_path(xlator_t *xl, loc_t *loc)
{
    uuid_t root = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1};
    char *name;
    int32_t ret = -EINVAL;

    if (loc->path != NULL) {
        name = strrchr(loc->path, '/');
        if (name == NULL) {
            /* Allow gfid paths: <gfid:...> */
            if (strncmp(loc->path, "<gfid:", 6) == 0) {
                ret = 0;
            }
            goto out;
        }
        if (name == loc->path) {
            if (name[1] == 0) {
                if (!ec_loc_gfid_check(xl, loc->gfid, root)) {
                    goto out;
                }
            } else {
                if (!ec_loc_gfid_check(xl, loc->pargfid, root)) {
                    goto out;
                }
            }
        }
        name++;

        if (loc->name != NULL) {
            if (strcmp(loc->name, name) != 0) {
                gf_msg (xl->name, GF_LOG_ERROR, EINVAL,
                        EC_MSG_INVALID_LOC_NAME,
                        "Invalid name '%s' in loc",
                        loc->name);

                goto out;
            }
        } else {
            loc->name = name;
        }
    }

    ret = 0;

out:
    return ret;
}

int32_t ec_loc_parent(xlator_t *xl, loc_t *loc, loc_t *parent)
{
    inode_table_t *table = NULL;
    char *str = NULL;
    int32_t ret = -ENOMEM;

    memset(parent, 0, sizeof(loc_t));

    if (loc->parent != NULL) {
        table = loc->parent->table;
        parent->inode = inode_ref(loc->parent);
    } else if (loc->inode != NULL) {
        table = loc->inode->table;
    }
    if (!gf_uuid_is_null(loc->pargfid)) {
        gf_uuid_copy(parent->gfid, loc->pargfid);
    }
    if (loc->path && strchr (loc->path, '/')) {
        str = gf_strdup(loc->path);
        if (str == NULL) {
                gf_msg (xl->name, GF_LOG_ERROR, ENOMEM,
                        EC_MSG_NO_MEMORY,
                        "Unable to duplicate path '%s'",
                        loc->path);

                goto out;
        }
        parent->path = gf_strdup(dirname(str));
        if (parent->path == NULL) {
                gf_msg (xl->name, GF_LOG_ERROR, ENOMEM,
                        EC_MSG_NO_MEMORY,
                        "Unable to duplicate path '%s'",
                        dirname(str));

                goto out;
        }
    }

    ret = ec_loc_setup_path(xl, parent);
    if (ret == 0) {
        ret = ec_loc_setup_inode(xl, table, parent);
    }
    if (ret == 0) {
        ret = ec_loc_setup_parent(xl, table, parent);
    }
    if (ret != 0) {
        goto out;
    }

    if ((parent->inode == NULL) && (parent->path == NULL) &&
        gf_uuid_is_null(parent->gfid)) {
        gf_msg (xl->name, GF_LOG_ERROR, EINVAL,
                EC_MSG_LOC_PARENT_INODE_MISSING,
                "Parent inode missing for loc_t");

        ret = -EINVAL;

        goto out;
    }

    ret = 0;

out:
    GF_FREE(str);

    if (ret != 0) {
        loc_wipe(parent);
    }

    return ret;
}

int32_t ec_loc_update(xlator_t *xl, loc_t *loc, inode_t *inode,
                      struct iatt *iatt)
{
    inode_table_t *table = NULL;
    int32_t ret = -EINVAL;

    if (inode != NULL) {
        table = inode->table;
        if (loc->inode != inode) {
            if (loc->inode != NULL) {
                inode_unref(loc->inode);
            }
            loc->inode = inode_ref(inode);
            gf_uuid_copy(loc->gfid, inode->gfid);
        }
    } else if (loc->inode != NULL) {
        table = loc->inode->table;
    } else if (loc->parent != NULL) {
        table = loc->parent->table;
    }

    if (iatt != NULL) {
        if (!ec_loc_gfid_check(xl, loc->gfid, iatt->ia_gfid)) {
            goto out;
        }
    }

    ret = ec_loc_setup_path(xl, loc);
    if (ret == 0) {
        ret = ec_loc_setup_inode(xl, table, loc);
    }
    if (ret == 0) {
        ret = ec_loc_setup_parent(xl, table, loc);
    }
    if (ret != 0) {
        goto out;
    }

out:
    return ret;
}

int32_t ec_loc_from_fd(xlator_t * xl, loc_t * loc, fd_t * fd)
{
    ec_fd_t * ctx;
    int32_t ret = -ENOMEM;

    memset(loc, 0, sizeof(*loc));

    ctx = ec_fd_get(fd, xl);
    if (ctx != NULL) {
        if (loc_copy(loc, &ctx->loc) != 0) {
            goto out;
        }
    }

    ret = ec_loc_update(xl, loc, fd->inode, NULL);
    if (ret != 0) {
        goto out;
    }

out:
    if (ret != 0) {
        loc_wipe(loc);
    }

    return ret;
}

int32_t ec_loc_from_loc(xlator_t * xl, loc_t * dst, loc_t * src)
{
    int32_t ret = -ENOMEM;

    memset(dst, 0, sizeof(*dst));

    if (loc_copy(dst, src) != 0) {
        goto out;
    }

    ret = ec_loc_update(xl, dst, NULL, NULL);
    if (ret != 0) {
        goto out;
    }

out:
    if (ret != 0) {
        loc_wipe(dst);
    }

    return ret;
}

void ec_owner_set(call_frame_t * frame, void * owner)
{
    set_lk_owner_from_ptr(&frame->root->lk_owner, owner);
}

void ec_owner_copy(call_frame_t *frame, gf_lkowner_t *owner)
{
    lk_owner_copy (&frame->root->lk_owner, owner);
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
            INIT_LIST_HEAD(&ctx->heal);

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

    /* Treat anonymous fd specially */
    if (fd->anonymous) {
        /* Mark the fd open for all subvolumes. */
        ctx->open = -1;
        /* Try to populate ctx->loc with fd->inode information. */
        ec_loc_update(xl, &ctx->loc, fd->inode, NULL);
    }

    return ctx;
}

ec_fd_t * ec_fd_get(fd_t * fd, xlator_t * xl)
{
    ec_fd_t * ctx = NULL;

    LOCK(&fd->lock);

    ctx = __ec_fd_get(fd, xl);

    UNLOCK(&fd->lock);

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

gf_boolean_t
ec_is_internal_xattr (dict_t *dict, char *key, data_t *value, void *data)
{
        if (key &&
            (strncmp (key, EC_XATTR_PREFIX, strlen (EC_XATTR_PREFIX)) == 0))
                return _gf_true;

        return _gf_false;
}

void
ec_filter_internal_xattrs (dict_t *xattr)
{
        dict_foreach_match (xattr, ec_is_internal_xattr, NULL,
                            dict_remove_foreach_fn, NULL);
}

gf_boolean_t
ec_is_data_fop (glusterfs_fop_t fop)
{
        switch (fop) {
        case GF_FOP_WRITE:
        case GF_FOP_TRUNCATE:
        case GF_FOP_FTRUNCATE:
        case GF_FOP_FALLOCATE:
        case GF_FOP_DISCARD:
        case GF_FOP_ZEROFILL:
                return _gf_true;
        default:
                return _gf_false;
        }
        return _gf_false;
}
/*
gf_boolean_t
ec_is_metadata_fop (int32_t lock_kind, glusterfs_fop_t fop)
{
        if (lock_kind == EC_LOCK_ENTRY) {
                return _gf_false;
        }

        switch (fop) {
        case GF_FOP_SETATTR:
        case GF_FOP_FSETATTR:
        case GF_FOP_SETXATTR:
        case GF_FOP_FSETXATTR:
        case GF_FOP_REMOVEXATTR:
        case GF_FOP_FREMOVEXATTR:
                return _gf_true;
        default:
                return _gf_false;
        }
        return _gf_false;
}*/
