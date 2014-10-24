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

#include <fnmatch.h>

#include "libxlator.h"

#include "ec-data.h"
#include "ec-helpers.h"
#include "ec-common.h"
#include "ec-combine.h"

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

int32_t ec_iatt_combine(struct iatt * dst, struct iatt * src, int32_t count)
{
    int32_t i;

    for (i = 0; i < count; i++)
    {
        if ((dst->ia_ino != src->ia_ino) ||
            (dst->ia_uid != src->ia_uid) ||
            (dst->ia_gid != src->ia_gid) ||
            (((dst->ia_type == IA_IFBLK) || (dst->ia_type == IA_IFCHR)) &&
             (dst->ia_rdev != src->ia_rdev)) ||
            ((dst->ia_type == IA_IFREG) && (dst->ia_size != src->ia_size)) ||
            (st_mode_from_ia(dst->ia_prot, dst->ia_type) !=
             st_mode_from_ia(src->ia_prot, src->ia_type)) ||
            (uuid_compare(dst->ia_gfid, src->ia_gfid) != 0))
        {
            gf_log(THIS->name, GF_LOG_WARNING,
                   "Failed to combine iatt (inode: %lu-%lu, links: %u-%u, "
                   "uid: %u-%u, gid: %u-%u, rdev: %lu-%lu, size: %lu-%lu, "
                   "mode: %o-%o)",
                   dst->ia_ino, src->ia_ino, dst->ia_nlink, src->ia_nlink,
                   dst->ia_uid, src->ia_uid, dst->ia_gid, src->ia_gid,
                   dst->ia_rdev, src->ia_rdev, dst->ia_size, src->ia_size,
                   st_mode_from_ia(dst->ia_prot, dst->ia_type),
                   st_mode_from_ia(src->ia_prot, dst->ia_type));

            return 0;
        }
    }

    while (count-- > 0)
    {
        dst->ia_blocks += src->ia_blocks;
        if (dst->ia_blksize < src->ia_blksize)
        {
            dst->ia_blksize = src->ia_blksize;
        }

        ec_iatt_time_merge(&dst->ia_atime, &dst->ia_atime_nsec, src->ia_atime,
                           src->ia_atime_nsec);
        ec_iatt_time_merge(&dst->ia_mtime, &dst->ia_mtime_nsec, src->ia_mtime,
                           src->ia_mtime_nsec);
        ec_iatt_time_merge(&dst->ia_ctime, &dst->ia_ctime_nsec, src->ia_ctime,
                           src->ia_ctime_nsec);
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

int32_t ec_dict_data_compare(dict_t * dict, char * key, data_t * value,
                             void * arg)
{
    ec_dict_info_t * info = arg;
    data_t * data;

    data = dict_get(info->dict, key);
    if (data == NULL)
    {
        gf_log("ec", GF_LOG_DEBUG, "key '%s' found only on one dict", key);

        return -1;
    }

    info->count--;

    if ((strcmp(key, GF_CONTENT_KEY) == 0) ||
        (strcmp(key, GF_XATTR_PATHINFO_KEY) == 0) ||
        (strcmp(key, GF_XATTR_USER_PATHINFO_KEY) == 0) ||
        (strcmp(key, GF_XATTR_LOCKINFO_KEY) == 0) ||
        (strcmp(key, GF_XATTR_CLRLK_CMD) == 0) ||
        (strcmp(key, GLUSTERFS_OPEN_FD_COUNT) == 0) ||
        (fnmatch(GF_XATTR_STIME_PATTERN, key, 0) == 0) ||
        (fnmatch(MARKER_XATTR_PREFIX ".*." XTIME, key, 0) == 0) ||
        (XATTR_IS_NODE_UUID(key)))
    {
        return 0;
    }

    if ((data->len != value->len) ||
        (memcmp(data->data, value->data, data->len) != 0))
    {
        gf_log("ec", GF_LOG_DEBUG, "key '%s' is different (size: %u, %u)",
               key, data->len, value->len);

        return -1;
    }

    return 0;
}

int32_t ec_dict_data_show(dict_t * dict, char * key, data_t * value,
                          void * arg)
{
    if (dict_get(arg, key) == NULL)
    {
        gf_log("ec", GF_LOG_DEBUG, "key '%s' found only on one dict", key);
    }

    return 0;
}

int32_t ec_dict_compare(dict_t * dict1, dict_t * dict2)
{
    ec_dict_info_t info;
    dict_t * dict;

    if (dict1 != NULL)
    {
        info.dict = dict1;
        info.count = dict1->count;
        dict = dict2;
    }
    else if (dict2 != NULL)
    {
        info.dict = dict2;
        info.count = dict2->count;
        dict = dict1;
    }
    else
    {
        return 1;
    }

    if (dict != NULL)
    {
        if (dict_foreach(dict, ec_dict_data_compare, &info) != 0)
        {
            return 0;
        }
    }

    if (info.count != 0)
    {
        dict_foreach(info.dict, ec_dict_data_show, dict);
    }

    return (info.count == 0);
}

int32_t ec_dict_list(data_t ** list, int32_t * count, ec_cbk_data_t * cbk,
                     int32_t which, char * key)
{
    ec_cbk_data_t * ans;
    dict_t * dict;
    int32_t i, max;

    max = *count;
    i = 0;
    for (ans = cbk; ans != NULL; ans = ans->next)
    {
        if (i >= max)
        {
            gf_log(cbk->fop->xl->name, GF_LOG_ERROR, "Unexpected number of "
                                                     "dictionaries");

            return 0;
        }

        dict = (which == EC_COMBINE_XDATA) ? ans->xdata : ans->dict;
        list[i] = dict_get(dict, key);
        if (list[i] == NULL)
        {
            gf_log(cbk->fop->xl->name, GF_LOG_ERROR, "Unexpected missing "
                                                     "dictionary entry");

            return 0;
        }

        i++;
    }

    *count = i;

    return 1;
}

char * ec_concat_prepare(xlator_t * xl, char ** sep, char ** post,
                         const char * fmt, va_list args)
{
    char * str, * tmp;
    int32_t len;

    len = gf_vasprintf(&str, fmt, args);
    if (len < 0)
    {
        return NULL;
    }

    tmp = strchr(str, '{');
    if (tmp == NULL)
    {
        goto out;
    }
    *tmp++ = 0;
    *sep = tmp;
    tmp = strchr(tmp, '}');
    if (tmp == NULL)
    {
        goto out;
    }
    *tmp++ = 0;
    *post = tmp;

    return str;

out:
    gf_log(xl->name, GF_LOG_ERROR, "Invalid concat format");

    GF_FREE(str);

    return NULL;
}

int32_t ec_dict_data_concat(const char * fmt, ec_cbk_data_t * cbk,
                            int32_t which, char * key, ...)
{
    data_t * data[cbk->count];
    char * str = NULL, * pre = NULL, * sep, * post;
    dict_t * dict;
    va_list args;
    int32_t i, num, len, prelen, postlen, seplen, tmp;
    int32_t ret = -1;

    num = cbk->count;
    if (!ec_dict_list(data, &num, cbk, which, key))
    {
        return -1;
    }

    va_start(args, key);
    pre = ec_concat_prepare(cbk->fop->xl, &sep, &post, fmt, args);
    va_end(args);

    if (pre == NULL)
    {
        return -1;
    }

    prelen = strlen(pre);
    seplen = strlen(sep);
    postlen = strlen(post);

    len = prelen + (num - 1) * seplen + postlen + 1;
    for (i = 0; i < num; i++)
    {
        len += data[i]->len - 1;
    }

    str = GF_MALLOC(len, gf_common_mt_char);
    if (str == NULL)
    {
        goto out;
    }

    memcpy(str, pre, prelen);
    len = prelen;
    for (i = 0; i < num; i++)
    {
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
    if (dict_set_dynstr(dict, key, str) != 0)
    {
        goto out;
    }

    str = NULL;

    ret = 0;

out:
    GF_FREE(str);
    GF_FREE(pre);

    return ret;
}

int32_t ec_dict_data_merge(ec_cbk_data_t * cbk, int32_t which, char * key)
{
    data_t * data[cbk->count];
    dict_t * dict, * lockinfo, * tmp;
    char * ptr = NULL;
    int32_t i, num, len;
    int32_t ret = -1;

    num = cbk->count;
    if (!ec_dict_list(data, &num, cbk, which, key))
    {
        return -1;
    }

    lockinfo = dict_new();
    if (lockinfo == NULL)
    {
        return -1;
    }

    if (dict_unserialize(data[0]->data, data[0]->len, &lockinfo) != 0)
    {
        goto out;
    }

    for (i = 1; i < num; i++)
    {
        tmp = dict_new();
        if (tmp == NULL)
        {
            goto out;
        }
        if ((dict_unserialize(data[i]->data, data[i]->len, &tmp) != 0) ||
            (dict_copy(tmp, lockinfo) == NULL))
        {
            dict_unref(tmp);

            goto out;
        }

        dict_unref(tmp);
    }

    len = dict_serialized_length(lockinfo);
    if (len < 0)
    {
        goto out;
    }
    ptr = GF_MALLOC(len, gf_common_mt_char);
    if (ptr == NULL)
    {
        goto out;
    }
    if (dict_serialize(lockinfo, ptr) != 0)
    {
        goto out;
    }
    dict = (which == EC_COMBINE_XDATA) ? cbk->xdata : cbk->dict;
    if (dict_set_dynptr(dict, key, ptr, len) != 0)
    {
        goto out;
    }

    ptr = NULL;

    ret = 0;

out:
    GF_FREE(ptr);
    dict_unref(lockinfo);

    return ret;
}

int32_t ec_dict_data_uuid(ec_cbk_data_t * cbk, int32_t which, char * key)
{
    ec_cbk_data_t * ans, * min;
    dict_t * src, * dst;
    data_t * data;

    min = cbk;
    for (ans = cbk->next; ans != NULL; ans = ans->next)
    {
        if (ans->idx < min->idx)
        {
            min = ans;
        }
    }

    if (min != cbk)
    {
        src = (which == EC_COMBINE_XDATA) ? min->xdata : min->dict;
        dst = (which == EC_COMBINE_XDATA) ? cbk->xdata : cbk->dict;

        data = dict_get(src, key);
        if (data == NULL)
        {
            return -1;
        }
        if (dict_set(dst, key, data) != 0)
        {
            return -1;
        }
    }

    return 0;
}

int32_t ec_dict_data_max32(ec_cbk_data_t *cbk, int32_t which, char *key)
{
    data_t * data[cbk->count];
    dict_t * dict;
    int32_t i, num;
    uint32_t max, tmp;

    num = cbk->count;
    if (!ec_dict_list(data, &num, cbk, which, key))
    {
        return -1;
    }

    if (num <= 1)
    {
        return 0;
    }

    max = data_to_uint32(data[0]);
    for (i = 1; i < num; i++)
    {
        tmp = data_to_uint32(data[i]);
        if (max < tmp)
        {
            max = tmp;
        }
    }

    dict = (which == EC_COMBINE_XDATA) ? cbk->xdata : cbk->dict;
    if (dict_set_uint32(dict, key, max) != 0)
    {
        return -1;
    }

    return 0;
}

int32_t ec_dict_data_max64(ec_cbk_data_t *cbk, int32_t which, char *key)
{
    data_t *data[cbk->count];
    dict_t *dict;
    int32_t i, num;
    uint64_t max, tmp;

    num = cbk->count;
    if (!ec_dict_list(data, &num, cbk, which, key)) {
        return -1;
    }

    if (num <= 1) {
        return 0;
    }

    max = data_to_uint64(data[0]);
    for (i = 1; i < num; i++) {
        tmp = data_to_uint64(data[i]);
        if (max < tmp) {
            max = tmp;
        }
    }

    dict = (which == EC_COMBINE_XDATA) ? cbk->xdata : cbk->dict;
    if (dict_set_uint64(dict, key, max) != 0) {
        return -1;
    }

    return 0;
}

int32_t ec_dict_data_stime(ec_cbk_data_t * cbk, int32_t which, char * key)
{
    data_t * data[cbk->count];
    dict_t * dict;
    int32_t i, num;

    num = cbk->count;
    if (!ec_dict_list(data, &num, cbk, which, key))
    {
        return -1;
    }

    dict = (which == EC_COMBINE_XDATA) ? cbk->xdata : cbk->dict;
    for (i = 1; i < num; i++)
    {
        if (gf_get_max_stime(cbk->fop->xl, dict, key, data[i]) != 0)
        {
            gf_log(cbk->fop->xl->name, GF_LOG_ERROR, "STIME combination "
                                                     "failed");

            return -1;
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

    if (strcmp(key, GLUSTERFS_OPEN_FD_COUNT) == 0)
    {
        return ec_dict_data_max32(data->cbk, data->which, key);
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
    dict_t * dict;
    ec_dict_combine_t data;

    data.cbk = cbk;
    data.which = which;

    dict = (which == EC_COMBINE_XDATA) ? cbk->xdata : cbk->dict;
    if ((dict != NULL) &&
        (dict_foreach(dict, ec_dict_data_combine, &data) != 0))
    {
        gf_log(cbk->fop->xl->name, GF_LOG_ERROR, "Dictionary combination "
                                                 "failed");

        return 0;
    }

    return 1;
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
        gf_log(THIS->name, GF_LOG_DEBUG, "Mismatching file system flags "
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
        gf_log(fop->xl->name, GF_LOG_DEBUG, "Mismatching return code in "
                                            "answers of '%s': %d <-> %d",
               ec_fop_name(fop->id), dst->op_ret, src->op_ret);

        return 0;
    }
    if (dst->op_ret < 0)
    {
        if (dst->op_errno != src->op_errno)
        {
            gf_log(fop->xl->name, GF_LOG_DEBUG, "Mismatching errno code in "
                                                "answers of '%s': %d <-> %d",
                   ec_fop_name(fop->id), dst->op_errno, src->op_errno);

            return 0;
        }
    }

    if (!ec_dict_compare(dst->xdata, src->xdata))
    {
        gf_log(fop->xl->name, GF_LOG_WARNING, "Mismatching xdata in answers "
                                              "of '%s'",
               ec_fop_name(fop->id));

        return 0;
    }

    if ((dst->op_ret >= 0) && (combine != NULL))
    {
        return combine(fop, dst, src);
    }

    return 1;
}

void ec_combine(ec_cbk_data_t * cbk, ec_combine_f combine)
{
    ec_fop_data_t * fop = cbk->fop;
    ec_cbk_data_t * ans = NULL, * tmp = NULL;
    struct list_head * item = NULL;
    int32_t needed = 0, resume = 0;
    char str[32];

    LOCK(&fop->lock);

    item = fop->cbk_list.prev;
    list_for_each_entry(ans, &fop->cbk_list, list)
    {
        if (ec_combine_check(cbk, ans, combine))
        {
            cbk->count += ans->count;
            cbk->mask |= ans->mask;

            item = ans->list.prev;
            while (item != &fop->cbk_list)
            {
                tmp = list_entry(item, ec_cbk_data_t, list);
                if (tmp->count >= cbk->count)
                {
                    break;
                }
                item = item->prev;
            }
            list_del(&ans->list);

            cbk->next = ans;

            break;
        }
    }
    list_add(&cbk->list, item);

    ec_trace("ANSWER", fop, "combine=%s[%d]",
             ec_bin(str, sizeof(str), cbk->mask, 0), cbk->count);

    if ((cbk->count == fop->expected) && (fop->answer == NULL))
    {
        fop->answer = cbk;

        ec_update_bad(fop, cbk->mask);

        resume = 1;
    }

    ans = list_entry(fop->cbk_list.next, ec_cbk_data_t, list);
    needed = fop->minimum - ans->count - fop->winds + 1;

    UNLOCK(&fop->lock);

    if (needed > 0)
    {
        ec_dispatch_next(fop, cbk->idx);
    }
    else if (resume)
    {
        ec_resume(fop, 0);
    }
}
