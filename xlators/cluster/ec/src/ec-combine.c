/*
  Copyright (c) 2012-2014 DataLab, s.l. <http://www.datalab.es>
  This file is part of GlusterFS.

  This file is licensed to you under your choice of the GNU Lesser
  General Public License, version 3 or any later version (LGPLv3 or
  later), or the GNU General Public License, version 2 (GPLv2), in all
  cases as published by the Free Software Foundation.
*/

#include <fnmatch.h>

#include "libxlator.h"
#include "byte-order.h"

#include "ec-types.h"
#include "ec-helpers.h"
#include "ec-common.h"
#include "ec-combine.h"
#include "ec-messages.h"
#include "quota-common-utils.h"

#define EC_QUOTA_PREFIX "trusted.glusterfs.quota."

struct _ec_dict_info;
typedef struct _ec_dict_info ec_dict_info_t;

struct _ec_dict_combine;
typedef struct _ec_dict_combine ec_dict_combine_t;

struct _ec_dict_info
{
    dict_t * dict;
    int32_t  count;
};

struct _ec_dict_combine
{
    ec_cbk_data_t * cbk;
    int32_t         which;
};

int32_t
ec_combine_write (ec_fop_data_t *fop, ec_cbk_data_t *dst,
                  ec_cbk_data_t *src)
{
        int     valid = 0;

        if (!fop || !dst || !src)
                return 0;

        switch (fop->id) {
        case GF_FOP_REMOVEXATTR:
        case GF_FOP_FREMOVEXATTR:
        case GF_FOP_SETXATTR:
        case GF_FOP_FSETXATTR:
                return 1;

        case GF_FOP_SYMLINK:
        case GF_FOP_LINK:
        case GF_FOP_CREATE:
        case GF_FOP_MKNOD:
        case GF_FOP_MKDIR:
                valid = 3;
                break;
        case GF_FOP_UNLINK:
        case GF_FOP_RMDIR:
        case GF_FOP_SETATTR:
        case GF_FOP_FSETATTR:
        case GF_FOP_TRUNCATE:
        case GF_FOP_FTRUNCATE:
        case GF_FOP_WRITE:
        case GF_FOP_FALLOCATE:
        case GF_FOP_DISCARD:
        case GF_FOP_ZEROFILL:
                valid = 2;
                break;
        case GF_FOP_RENAME:
                valid = 5;
                break;
        default:
                gf_msg_callingfn (fop->xl->name, GF_LOG_WARNING, EINVAL,
                                  EC_MSG_INVALID_FOP,
                                  "Invalid fop %d", fop->id);
                return 0;
                break;
        }

        if (!ec_iatt_combine(fop, dst->iatt, src->iatt, valid)) {
                gf_msg (fop->xl->name, GF_LOG_NOTICE, 0,
                        EC_MSG_IATT_MISMATCH,
                        "Mismatching iatt in "
                        "answers of '%s'", gf_fop_list[fop->id]);
                return 0;
        }
        return 1;
}

void ec_iatt_time_merge(uint32_t * dst_sec, uint32_t * dst_nsec,
                        uint32_t src_sec, uint32_t src_nsec)
{
    if ((*dst_sec < src_sec) ||
        ((*dst_sec == src_sec) && (*dst_nsec < src_nsec)))
    {
        *dst_sec = src_sec;
        *dst_nsec = src_nsec;
    }
}

static
gf_boolean_t
ec_iatt_is_trusted(ec_fop_data_t *fop, struct iatt *iatt)
{
    uint64_t ino;
    int32_t i;

    /* Only the top level fop will have fop->locks filled. */
    while (fop->parent != NULL) {
        fop = fop->parent;
    }

    /* Lookups are special requests always done without locks taken but they
     * require to be able to identify differences between bricks. Special
     * handling of these differences is already done in lookup specific code
     * so we shouldn't ignore any difference here and consider all iatt
     * structures as trusted. */
    if (fop->id == GF_FOP_LOOKUP) {
        return _gf_true;
    }

    /* Check if the iatt references an inode locked by the current fop */
    for (i = 0; i < fop->lock_count; i++) {
        ino = gfid_to_ino(fop->locks[i].lock->loc.inode->gfid);
        if (iatt->ia_ino == ino) {
            return _gf_true;
        }
    }

    return _gf_false;
}

int32_t ec_iatt_combine(ec_fop_data_t *fop, struct iatt *dst, struct iatt *src,
                        int32_t count)
{
    int32_t i;
    gf_boolean_t failed = _gf_false;

    for (i = 0; i < count; i++)
    {
        /* Check for basic fields. These fields must be equal always, even if
         * the inode is not locked because in these cases the parent inode
         * will be locked and differences in these fields require changes in
         * the parent directory. */
        if ((dst[i].ia_ino != src[i].ia_ino) ||
            (((dst[i].ia_type == IA_IFBLK) || (dst[i].ia_type == IA_IFCHR)) &&
             (dst[i].ia_rdev != src[i].ia_rdev)) ||
            (gf_uuid_compare(dst[i].ia_gfid, src[i].ia_gfid) != 0)) {
            failed = _gf_true;
        }
        /* Check for not so stable fields. These fields can change if the
         * inode is not locked. */
        if (!failed && ((dst[i].ia_uid != src[i].ia_uid) ||
                        (dst[i].ia_gid != src[i].ia_gid) ||
                        ((dst[i].ia_type == IA_IFREG) &&
                         (dst[i].ia_size != src[i].ia_size)) ||
                        (st_mode_from_ia(dst[i].ia_prot, dst[i].ia_type) !=
                         st_mode_from_ia(src[i].ia_prot, src[i].ia_type)))) {
            if (ec_iatt_is_trusted(fop, dst)) {
                /* If the iatt contains information from an inode that is
                 * locked, these differences are real problems, so we need to
                 * report them. Otherwise we ignore them and don't care which
                 * data is returned. */
                failed = _gf_true;
            } else {
                gf_msg_debug (fop->xl->name, 0,
                       "Ignoring iatt differences because inode is not "
                       "locked");
            }
        }
        if (failed) {
            gf_msg (fop->xl->name, GF_LOG_WARNING, 0,
                    EC_MSG_IATT_COMBINE_FAIL,
                    "Failed to combine iatt (inode: %lu-%lu, links: %u-%u, "
                    "uid: %u-%u, gid: %u-%u, rdev: %lu-%lu, size: %lu-%lu, "
                    "mode: %o-%o)",
                    dst[i].ia_ino, src[i].ia_ino, dst[i].ia_nlink,
                    src[i].ia_nlink, dst[i].ia_uid, src[i].ia_uid,
                    dst[i].ia_gid, src[i].ia_gid, dst[i].ia_rdev,
                    src[i].ia_rdev, dst[i].ia_size, src[i].ia_size,
                    st_mode_from_ia(dst[i].ia_prot, dst[i].ia_type),
                    st_mode_from_ia(src[i].ia_prot, dst[i].ia_type));

            return 0;
        }
    }

    while (count-- > 0)
    {
        dst[count].ia_blocks += src[count].ia_blocks;
        if (dst[count].ia_blksize < src[count].ia_blksize)
        {
            dst[count].ia_blksize = src[count].ia_blksize;
        }

        ec_iatt_time_merge(&dst[count].ia_atime, &dst[count].ia_atime_nsec,
                           src[count].ia_atime, src[count].ia_atime_nsec);
        ec_iatt_time_merge(&dst[count].ia_mtime, &dst[count].ia_mtime_nsec,
                           src[count].ia_mtime, src[count].ia_mtime_nsec);
        ec_iatt_time_merge(&dst[count].ia_ctime, &dst[count].ia_ctime_nsec,
                           src[count].ia_ctime, src[count].ia_ctime_nsec);
    }

    return 1;
}

void ec_iatt_rebuild(ec_t * ec, struct iatt * iatt, int32_t count,
                     int32_t answers)
{
    uint64_t blocks;

    while (count-- > 0)
    {
        blocks = iatt[count].ia_blocks * ec->fragments + answers - 1;
        blocks /= answers;
        iatt[count].ia_blocks = blocks;
    }
}

gf_boolean_t
ec_xattr_match (dict_t *dict, char *key, data_t *value, void *arg)
{
        if ((fnmatch(GF_XATTR_STIME_PATTERN, key, 0) == 0) ||
            (strcmp(key, GET_LINK_COUNT) == 0) ||
            (strcmp(key, GLUSTERFS_INODELK_COUNT) == 0) ||
            (strcmp(key, GLUSTERFS_ENTRYLK_COUNT) == 0) ||
            (strcmp(key, GLUSTERFS_OPEN_FD_COUNT) == 0)) {
                return _gf_false;
        }

        return _gf_true;
}

gf_boolean_t
ec_value_ignore (char *key)
{
        if ((strcmp(key, GF_CONTENT_KEY) == 0) ||
            (strcmp(key, GF_XATTR_PATHINFO_KEY) == 0) ||
            (strcmp(key, GF_XATTR_USER_PATHINFO_KEY) == 0) ||
            (strcmp(key, GF_XATTR_LOCKINFO_KEY) == 0) ||
            (strcmp(key, GLUSTERFS_OPEN_FD_COUNT) == 0) ||
            (strcmp(key, GLUSTERFS_INODELK_COUNT) == 0) ||
            (strcmp(key, GLUSTERFS_ENTRYLK_COUNT) == 0) ||
            (strncmp(key, GF_XATTR_CLRLK_CMD,
                     strlen (GF_XATTR_CLRLK_CMD)) == 0) ||
            (strcmp(key, DHT_IATT_IN_XDATA_KEY) == 0) ||
            (strncmp(key, EC_QUOTA_PREFIX, strlen(EC_QUOTA_PREFIX)) == 0) ||
            (fnmatch(MARKER_XATTR_PREFIX ".*." XTIME, key, 0) == 0) ||
            (fnmatch(GF_XATTR_MARKER_KEY ".*", key, 0) == 0) ||
            (XATTR_IS_NODE_UUID(key))) {
                return _gf_true;
        }
        return _gf_false;
}

int32_t
ec_dict_compare (dict_t *dict1, dict_t *dict2)
{
        if (are_dicts_equal (dict1, dict2, ec_xattr_match, ec_value_ignore))
                return 1;
        return 0;
}

int32_t ec_dict_list(data_t ** list, int32_t * count, ec_cbk_data_t * cbk,
                     int32_t which, char * key)
{
    ec_cbk_data_t *ans = NULL;
    dict_t *dict = NULL;
    int32_t i, max;

    max = *count;
    i = 0;
    for (ans = cbk; ans != NULL; ans = ans->next) {
        if (i >= max) {
            gf_msg (cbk->fop->xl->name, GF_LOG_ERROR, EINVAL,
                    EC_MSG_INVALID_DICT_NUMS,
                    "Unexpected number of "
                    "dictionaries");

            return -EINVAL;
        }

        dict = (which == EC_COMBINE_XDATA) ? ans->xdata : ans->dict;
        list[i] = dict_get(dict, key);
        if (list[i] != NULL) {
            i++;
        }
    }

    *count = i;

    return 0;
}

int32_t ec_concat_prepare(xlator_t *xl, char **str, char **sep, char **post,
                          const char *fmt, va_list args)
{
    char *tmp;
    int32_t len;

    len = gf_vasprintf(str, fmt, args);
    if (len < 0) {
        return -ENOMEM;
    }

    tmp = strchr(*str, '{');
    if (tmp == NULL) {
        goto out;
    }
    *tmp++ = 0;
    *sep = tmp;
    tmp = strchr(tmp, '}');
    if (tmp == NULL) {
        goto out;
    }
    *tmp++ = 0;
    *post = tmp;

    return 0;

out:
    gf_msg (xl->name, GF_LOG_ERROR, EINVAL,
            EC_MSG_INVALID_FORMAT,
            "Invalid concat format");

    GF_FREE(*str);

    return -EINVAL;
}

int32_t ec_dict_data_concat(const char * fmt, ec_cbk_data_t * cbk,
                            int32_t which, char * key, ...)
{
    data_t * data[cbk->count];
    char * str = NULL, * pre = NULL, * sep, * post;
    dict_t * dict;
    va_list args;
    int32_t i, num, len, prelen, postlen, seplen, tmp;
    int32_t err;

    num = cbk->count;
    err = ec_dict_list(data, &num, cbk, which, key);
    if (err != 0) {
        return err;
    }

    va_start(args, key);
    err = ec_concat_prepare(cbk->fop->xl, &pre, &sep, &post, fmt, args);
    va_end(args);

    if (err != 0) {
        return err;
    }

    prelen = strlen(pre);
    seplen = strlen(sep);
    postlen = strlen(post);

    len = prelen + (num - 1) * seplen + postlen + 1;
    for (i = 0; i < num; i++) {
        len += data[i]->len - 1;
    }

    err = -ENOMEM;

    str = GF_MALLOC(len, gf_common_mt_char);
    if (str == NULL) {
        goto out;
    }

    memcpy(str, pre, prelen);
    len = prelen;
    for (i = 0; i < num; i++) {
        if (i > 0) {
            memcpy(str + len, sep, seplen);
            len += seplen;
        }
        tmp = data[i]->len - 1;
        memcpy(str + len, data[i]->data, tmp);
        len += tmp;
    }
    memcpy(str + len, post, postlen + 1);

    dict = (which == EC_COMBINE_XDATA) ? cbk->xdata : cbk->dict;
    err = dict_set_dynstr(dict, key, str);
    if (err != 0) {
        goto out;
    }

    str = NULL;

out:
    GF_FREE(str);
    GF_FREE(pre);

    return err;
}

int32_t ec_dict_data_merge(ec_cbk_data_t *cbk, int32_t which, char *key)
{
    data_t *data[cbk->count];
    dict_t *dict, *lockinfo, *tmp = NULL;
    char *ptr = NULL;
    int32_t i, num, len;
    int32_t err;

    num = cbk->count;
    err = ec_dict_list(data, &num, cbk, which, key);
    if (err != 0) {
        return err;
    }

    lockinfo = dict_new();
    if (lockinfo == NULL) {
        return -ENOMEM;
    }

    err = dict_unserialize(data[0]->data, data[0]->len, &lockinfo);
    if (err != 0) {
        goto out;
    }

    for (i = 1; i < num; i++)
    {
        tmp = dict_new();
        if (tmp == NULL) {
            err = -ENOMEM;

            goto out;
        }
        err = dict_unserialize(data[i]->data, data[i]->len, &tmp);
        if (err != 0) {
            goto out;
        }
        if (dict_copy(tmp, lockinfo) == NULL) {
            err = -ENOMEM;

            goto out;
        }

        dict_unref(tmp);
    }

    tmp = NULL;

    len = dict_serialized_length(lockinfo);
    if (len < 0) {
        err = len;

        goto out;
    }
    ptr = GF_MALLOC(len, gf_common_mt_char);
    if (ptr == NULL) {
        err = -ENOMEM;

        goto out;
    }
    err = dict_serialize(lockinfo, ptr);
    if (err != 0) {
        goto out;
    }
    dict = (which == EC_COMBINE_XDATA) ? cbk->xdata : cbk->dict;
    err = dict_set_dynptr(dict, key, ptr, len);
    if (err != 0) {
        goto out;
    }

    ptr = NULL;

out:
    GF_FREE(ptr);
    dict_unref(lockinfo);
    if (tmp != NULL) {
        dict_unref(tmp);
    }

    return err;
}

int32_t ec_dict_data_uuid(ec_cbk_data_t * cbk, int32_t which, char * key)
{
    ec_cbk_data_t * ans, * min;
    dict_t * src, * dst;
    data_t * data;

    min = cbk;
    for (ans = cbk->next; ans != NULL; ans = ans->next) {
        if (ans->idx < min->idx) {
            min = ans;
        }
    }

    if (min != cbk) {
        src = (which == EC_COMBINE_XDATA) ? min->xdata : min->dict;
        dst = (which == EC_COMBINE_XDATA) ? cbk->xdata : cbk->dict;

        data = dict_get(src, key);
        if (data == NULL) {
            return -ENOENT;
        }
        if (dict_set(dst, key, data) != 0) {
            return -ENOMEM;
        }
    }

    return 0;
}

int32_t ec_dict_data_max32(ec_cbk_data_t *cbk, int32_t which, char *key)
{
    data_t * data[cbk->count];
    dict_t * dict;
    int32_t i, num, err;
    uint32_t max, tmp;

    num = cbk->count;
    err = ec_dict_list(data, &num, cbk, which, key);
    if (err != 0) {
        return err;
    }

    max = data_to_uint32(data[0]);
    for (i = 1; i < num; i++) {
        tmp = data_to_uint32(data[i]);
        if (max < tmp) {
            max = tmp;
        }
    }

    dict = (which == EC_COMBINE_XDATA) ? cbk->xdata : cbk->dict;
    return dict_set_uint32(dict, key, max);
}

int32_t ec_dict_data_max64(ec_cbk_data_t *cbk, int32_t which, char *key)
{
    data_t *data[cbk->count];
    dict_t *dict;
    int32_t i, num, err;
    uint64_t max, tmp;

    num = cbk->count;
    err = ec_dict_list(data, &num, cbk, which, key);
    if (err != 0) {
        return err;
    }

    max = data_to_uint64(data[0]);
    for (i = 1; i < num; i++) {
        tmp = data_to_uint64(data[i]);
        if (max < tmp) {
            max = tmp;
        }
    }

    dict = (which == EC_COMBINE_XDATA) ? cbk->xdata : cbk->dict;
    return dict_set_uint64(dict, key, max);
}

int32_t ec_dict_data_quota(ec_cbk_data_t *cbk, int32_t which, char *key)
{
    data_t      *data[cbk->count];
    dict_t      *dict             = NULL;
    ec_t        *ec               = NULL;
    int32_t      i                = 0;
    int32_t      num              = 0;
    int32_t      err              = 0;
    quota_meta_t size             = {0, };
    quota_meta_t max_size         = {0, };

    num = cbk->count;
    err = ec_dict_list(data, &num, cbk, which, key);
    if (err != 0) {
        return err;
    }

    if (num == 0) {
        return 0;
    }

    /* Quota size xattr is managed outside of the control of the ec xlator.
     * This means that it might not be updated at the same time on all
     * bricks and we can receive slightly different values. If that's the
     * case, we take the maximum of all received values.
     */
    for (i = 0; i < num; i++) {
        if (quota_data_to_meta (data[i], QUOTA_SIZE_KEY, &size) < 0) {
                continue;
        }

        if (size.size > max_size.size)
                max_size.size = size.size;
        if (size.file_count > max_size.file_count)
                max_size.file_count = size.file_count;
        if (size.dir_count > max_size.dir_count)
                max_size.dir_count = size.dir_count;
    }

    ec = cbk->fop->xl->private;
    max_size.size *= ec->fragments;

    dict = (which == EC_COMBINE_XDATA) ? cbk->xdata : cbk->dict;
    return quota_dict_set_meta (dict, key, &max_size, IA_IFDIR);
}

int32_t ec_dict_data_stime(ec_cbk_data_t * cbk, int32_t which, char * key)
{
    data_t * data[cbk->count];
    dict_t * dict;
    int32_t i, num, err;

    num = cbk->count;
    err = ec_dict_list(data, &num, cbk, which, key);
    if (err != 0) {
        return err;
    }

    dict = (which == EC_COMBINE_XDATA) ? cbk->xdata : cbk->dict;
    for (i = 1; i < num; i++) {
        err = gf_get_max_stime(cbk->fop->xl, dict, key, data[i]);
        if (err != 0) {
            gf_msg (cbk->fop->xl->name, GF_LOG_ERROR, -err,
                    EC_MSG_STIME_COMBINE_FAIL, "STIME combination failed");

            return err;
        }
    }

    return 0;
}

int32_t ec_dict_data_combine(dict_t * dict, char * key, data_t * value,
                             void * arg)
{
    ec_dict_combine_t * data = arg;

    if ((strcmp(key, GF_XATTR_PATHINFO_KEY) == 0) ||
        (strcmp(key, GF_XATTR_USER_PATHINFO_KEY) == 0))
    {
        return ec_dict_data_concat("(<EC:%s> { })", data->cbk, data->which,
                                   key, data->cbk->fop->xl->name);
    }

    if (strncmp(key, GF_XATTR_CLRLK_CMD, strlen(GF_XATTR_CLRLK_CMD)) == 0)
    {
        return ec_dict_data_concat("{\n}", data->cbk, data->which, key);
    }

    if (strncmp(key, GF_XATTR_LOCKINFO_KEY,
                strlen(GF_XATTR_LOCKINFO_KEY)) == 0)
    {
        return ec_dict_data_merge(data->cbk, data->which, key);
    }

    if (strcmp(key, GET_LINK_COUNT) == 0) {
        return ec_dict_data_max32(data->cbk, data->which, key);
    }

    if (strcmp(key, GLUSTERFS_OPEN_FD_COUNT) == 0)
    {
        return ec_dict_data_max32(data->cbk, data->which, key);
    }
    if ((strcmp(key, GLUSTERFS_INODELK_COUNT) == 0) ||
        (strcmp(key, GLUSTERFS_ENTRYLK_COUNT) == 0)) {
        return ec_dict_data_max32(data->cbk, data->which, key);
    }

    if (strcmp(key, QUOTA_SIZE_KEY) == 0) {
        return ec_dict_data_quota(data->cbk, data->which, key);
    }
    /* Ignore all other quota attributes */
    if (strncmp(key, EC_QUOTA_PREFIX, strlen(EC_QUOTA_PREFIX)) == 0) {
        return 0;
    }

    if (XATTR_IS_NODE_UUID(key))
    {
        return ec_dict_data_uuid(data->cbk, data->which, key);
    }

    if (fnmatch(GF_XATTR_STIME_PATTERN, key, FNM_NOESCAPE) == 0)
    {
        return ec_dict_data_stime(data->cbk, data->which, key);
    }

    if (fnmatch(MARKER_XATTR_PREFIX ".*." XTIME, key, FNM_NOESCAPE) == 0) {
        return ec_dict_data_max64(data->cbk, data->which, key);
    }

    return 0;
}

int32_t ec_dict_combine(ec_cbk_data_t * cbk, int32_t which)
{
    dict_t *dict = NULL;
    ec_dict_combine_t data;
    int32_t err = 0;

    data.cbk = cbk;
    data.which = which;

    dict = (which == EC_COMBINE_XDATA) ? cbk->xdata : cbk->dict;
    if (dict != NULL) {
        err = dict_foreach(dict, ec_dict_data_combine, &data);
        if (err != 0) {
            gf_msg (cbk->fop->xl->name, GF_LOG_ERROR, -err,
                    EC_MSG_DICT_COMBINE_FAIL,
                    "Dictionary combination failed");

            return err;
        }
    }

    return 0;
}

int32_t ec_vector_compare(struct iovec * dst_vector, int32_t dst_count,
                          struct iovec * src_vector, int32_t src_count)
{
    int32_t dst_size = 0, src_size = 0;

    if (dst_count > 0)
    {
        dst_size = iov_length(dst_vector, dst_count);
    }
    if (src_count > 0)
    {
        src_size = iov_length(src_vector, src_count);
    }

    return (dst_size == src_size);
}

int32_t ec_flock_compare(struct gf_flock * dst, struct gf_flock * src)
{
    if ((dst->l_type != src->l_type) ||
        (dst->l_whence != src->l_whence) ||
        (dst->l_start != src->l_start) ||
        (dst->l_len != src->l_len) ||
        (dst->l_pid != src->l_pid) ||
        !is_same_lkowner(&dst->l_owner, &src->l_owner))
    {
        return 0;
    }

    return 1;
}

void ec_statvfs_combine(struct statvfs * dst, struct statvfs * src)
{
    if (dst->f_bsize < src->f_bsize)
    {
        dst->f_bsize = src->f_bsize;
    }

    if (dst->f_frsize < src->f_frsize)
    {
        dst->f_blocks *= dst->f_frsize;
        dst->f_blocks /= src->f_frsize;

        dst->f_bfree *= dst->f_frsize;
        dst->f_bfree /= src->f_frsize;

        dst->f_bavail *= dst->f_frsize;
        dst->f_bavail /= src->f_frsize;

        dst->f_frsize = src->f_frsize;
    }
    else if (dst->f_frsize > src->f_frsize)
    {
        src->f_blocks *= src->f_frsize;
        src->f_blocks /= dst->f_frsize;

        src->f_bfree *= src->f_frsize;
        src->f_bfree /= dst->f_frsize;

        src->f_bavail *= src->f_frsize;
        src->f_bavail /= dst->f_frsize;
    }
    if (dst->f_blocks > src->f_blocks)
    {
        dst->f_blocks = src->f_blocks;
    }
    if (dst->f_bfree > src->f_bfree)
    {
        dst->f_bfree = src->f_bfree;
    }
    if (dst->f_bavail > src->f_bavail)
    {
        dst->f_bavail = src->f_bavail;
    }

    if (dst->f_files < src->f_files)
    {
        dst->f_files = src->f_files;
    }
    if (dst->f_ffree > src->f_ffree)
    {
        dst->f_ffree = src->f_ffree;
    }
    if (dst->f_favail > src->f_favail)
    {
        dst->f_favail = src->f_favail;
    }
    if (dst->f_namemax > src->f_namemax)
    {
        dst->f_namemax = src->f_namemax;
    }

    if (dst->f_flag != src->f_flag)
    {
        gf_msg_debug (THIS->name, 0,
                "Mismatching file system flags "
                "(%lX, %lX)",
                dst->f_flag, src->f_flag);
    }
    dst->f_flag &= src->f_flag;
}

int32_t ec_combine_check(ec_cbk_data_t * dst, ec_cbk_data_t * src,
                         ec_combine_f combine)
{
    ec_fop_data_t * fop = dst->fop;

    if (dst->op_ret != src->op_ret)
    {
        gf_msg_debug (fop->xl->name, 0, "Mismatching return code in "
                                            "answers of '%s': %d <-> %d",
               ec_fop_name(fop->id), dst->op_ret, src->op_ret);

        return 0;
    }
    if (dst->op_ret < 0)
    {
        if (dst->op_errno != src->op_errno)
        {
            gf_msg_debug (fop->xl->name, 0, "Mismatching errno code in "
                                                "answers of '%s': %d <-> %d",
                   ec_fop_name(fop->id), dst->op_errno, src->op_errno);

            return 0;
        }
    }

    if (!ec_dict_compare(dst->xdata, src->xdata))
    {
        gf_msg (fop->xl->name, GF_LOG_DEBUG, 0,
                EC_MSG_XDATA_MISMATCH,
                "Mismatching xdata in answers "
                "of '%s'", ec_fop_name(fop->id));

        return 0;
    }

    if ((dst->op_ret >= 0) && (combine != NULL))
    {
        return combine(fop, dst, src);
    }

    return 1;
}

void ec_combine (ec_cbk_data_t *newcbk, ec_combine_f combine)
{
    ec_fop_data_t *fop = newcbk->fop;
    ec_cbk_data_t *cbk = NULL, *tmp = NULL;
    struct list_head *item = NULL;
    int32_t needed = 0;
    char str[32];

    LOCK(&fop->lock);

    fop->received |= newcbk->mask;

    item = fop->cbk_list.prev;
    list_for_each_entry(cbk, &fop->cbk_list, list)
    {
        if (ec_combine_check(newcbk, cbk, combine))
        {
            newcbk->count += cbk->count;
            newcbk->mask |= cbk->mask;

            item = cbk->list.prev;
            while (item != &fop->cbk_list)
            {
                tmp = list_entry(item, ec_cbk_data_t, list);
                if (tmp->count >= newcbk->count)
                {
                    break;
                }
                item = item->prev;
            }
            list_del(&cbk->list);

            newcbk->next = cbk;

            break;
        }
    }
    list_add(&newcbk->list, item);

    ec_trace("ANSWER", fop, "combine=%s[%d]",
             ec_bin(str, sizeof(str), newcbk->mask, 0), newcbk->count);

    cbk = list_entry(fop->cbk_list.next, ec_cbk_data_t, list);
    if ((fop->mask ^ fop->remaining) == fop->received) {
        needed = fop->minimum - cbk->count;
    }

    UNLOCK(&fop->lock);

    if (needed > 0) {
        ec_dispatch_next(fop, newcbk->idx);
    }
}
