/*
  Copyright (c) 2008-2012 Red Hat, Inc. <http://www.redhat.com>
  This file is part of GlusterFS.

  This file is licensed to you under your choice of the GNU Lesser
  General Public License, version 3 or any later version (LGPLv3 or
  later), or the GNU General Public License, version 2 (GPLv2), in all
  cases as published by the Free Software Foundation.
*/

#include <libgen.h>
#include <unistd.h>
#include <fnmatch.h>
#include <sys/time.h>
#include <stdlib.h>
#include <signal.h>

#include <urcu/uatomic.h>

#include <glusterfs/glusterfs.h>
#include "afr.h"
#include <glusterfs/dict.h>
#include <glusterfs/logging.h>
#include <glusterfs/list.h>
#include <glusterfs/byte-order.h>
#include <glusterfs/defaults.h>
#include <glusterfs/common-utils.h>
#include <glusterfs/compat-errno.h>
#include <glusterfs/compat.h>
#include <glusterfs/quota-common-utils.h>

#include "afr-transaction.h"
#include "afr-messages.h"

/*
 * Quota size xattrs are not maintained by afr. There is a
 * possibility that they differ even when both the directory changelog xattrs
 * suggest everything is fine. So if there is at least one 'source' check among
 * the sources which has the maximum quota size. Otherwise check among all the
 * available ones for maximum quota size. This way if there is a source and
 * stale copies it always votes for the 'source'.
 * */

int
afr_handle_quota_size(call_frame_t *frame, xlator_t *this)
{
    unsigned char *readable = NULL;
    afr_local_t *local = NULL;
    afr_private_t *priv = NULL;
    struct afr_reply *replies = NULL;
    int i = 0;
    int ret = 0;
    quota_meta_t size = {
        0,
    };
    quota_meta_t max_size = {
        0,
    };
    int readable_cnt = 0;
    int read_subvol = -1;

    local = frame->local;
    priv = this->private;
    replies = local->replies;

    readable = alloca0(priv->child_count);

    afr_inode_read_subvol_get(local->inode, this, readable, 0, 0);

    readable_cnt = AFR_COUNT(readable, priv->child_count);

    for (i = 0; i < priv->child_count; i++) {
        if (!replies[i].valid || replies[i].op_ret == -1)
            continue;
        if (readable_cnt && !readable[i])
            continue;
        if (!replies[i].xdata)
            continue;
        ret = quota_dict_get_meta(replies[i].xdata, QUOTA_SIZE_KEY,
                                  SLEN(QUOTA_SIZE_KEY), &size);
        if (ret == -1)
            continue;
        if (read_subvol == -1)
            read_subvol = i;
        if (size.size > max_size.size ||
            (size.file_count + size.dir_count) >
                (max_size.file_count + max_size.dir_count))
            read_subvol = i;

        if (size.size > max_size.size)
            max_size.size = size.size;
        if (size.file_count > max_size.file_count)
            max_size.file_count = size.file_count;
        if (size.dir_count > max_size.dir_count)
            max_size.dir_count = size.dir_count;
    }

    if (max_size.size == 0 && max_size.file_count == 0 &&
        max_size.dir_count == 0)
        return read_subvol;

    for (i = 0; i < priv->child_count; i++) {
        if (!replies[i].valid || replies[i].op_ret == -1)
            continue;
        if (readable_cnt && !readable[i])
            continue;
        if (!replies[i].xdata)
            continue;
        quota_dict_set_meta(replies[i].xdata, QUOTA_SIZE_KEY, &max_size,
                            IA_IFDIR);
    }

    return read_subvol;
}

/* {{{ access */

int
afr_access_cbk(call_frame_t *frame, void *cookie, xlator_t *this, int op_ret,
               int op_errno, dict_t *xdata)
{
    afr_local_t *local = NULL;

    local = frame->local;

    if (op_ret < 0) {
        local->op_ret = op_ret;
        local->op_errno = op_errno;

        afr_read_txn_continue(frame, this, (long)cookie);
        return 0;
    }

    AFR_STACK_UNWIND(access, frame, op_ret, op_errno, xdata);

    return 0;
}

int
afr_access_wind(call_frame_t *frame, xlator_t *this, int subvol)
{
    afr_private_t *priv = NULL;
    afr_local_t *local = NULL;

    priv = this->private;
    local = frame->local;

    if (subvol == -1) {
        AFR_STACK_UNWIND(access, frame, local->op_ret, local->op_errno, 0);
        return 0;
    }

    STACK_WIND_COOKIE(frame, afr_access_cbk, (void *)(long)subvol,
                      priv->children[subvol],
                      priv->children[subvol]->fops->access, &local->loc,
                      local->cont.access.mask, local->xdata_req);
    return 0;
}

int
afr_access(call_frame_t *frame, xlator_t *this, loc_t *loc, int mask,
           dict_t *xdata)
{
    afr_local_t *local = NULL;
    int op_errno = 0;

    local = AFR_FRAME_INIT(frame, op_errno);
    if (!local)
        goto out;

    local->op = GF_FOP_ACCESS;
    loc_copy(&local->loc, loc);
    local->cont.access.mask = mask;
    if (xdata)
        local->xdata_req = dict_ref(xdata);

    afr_read_txn(frame, this, loc->inode, afr_access_wind,
                 AFR_METADATA_TRANSACTION);

    return 0;
out:
    AFR_STACK_UNWIND(access, frame, -1, op_errno, NULL);

    return 0;
}

/* }}} */

/* {{{ stat */

int
afr_stat_cbk(call_frame_t *frame, void *cookie, xlator_t *this, int32_t op_ret,
             int32_t op_errno, struct iatt *buf, dict_t *xdata)
{
    afr_local_t *local = NULL;

    local = frame->local;

    if (op_ret < 0) {
        local->op_ret = op_ret;
        local->op_errno = op_errno;

        afr_read_txn_continue(frame, this, (long)cookie);
        return 0;
    }

    AFR_STACK_UNWIND(stat, frame, op_ret, op_errno, buf, xdata);

    return 0;
}

int
afr_stat_wind(call_frame_t *frame, xlator_t *this, int subvol)
{
    afr_private_t *priv = NULL;
    afr_local_t *local = NULL;

    priv = this->private;
    local = frame->local;

    if (subvol == -1) {
        AFR_STACK_UNWIND(stat, frame, local->op_ret, local->op_errno, 0, 0);
        return 0;
    }

    STACK_WIND_COOKIE(
        frame, afr_stat_cbk, (void *)(long)subvol, priv->children[subvol],
        priv->children[subvol]->fops->stat, &local->loc, local->xdata_req);
    return 0;
}

int
afr_stat(call_frame_t *frame, xlator_t *this, loc_t *loc, dict_t *xdata)
{
    afr_local_t *local = NULL;
    int op_errno = 0;

    local = AFR_FRAME_INIT(frame, op_errno);
    if (!local)
        goto out;

    local->op = GF_FOP_STAT;
    loc_copy(&local->loc, loc);
    if (xdata)
        local->xdata_req = dict_ref(xdata);

    afr_read_txn(frame, this, loc->inode, afr_stat_wind, AFR_DATA_TRANSACTION);

    return 0;
out:
    AFR_STACK_UNWIND(stat, frame, -1, op_errno, NULL, NULL);

    return 0;
}

/* }}} */

/* {{{ fstat */

int
afr_fstat_cbk(call_frame_t *frame, void *cookie, xlator_t *this, int32_t op_ret,
              int32_t op_errno, struct iatt *buf, dict_t *xdata)
{
    afr_local_t *local = NULL;

    local = frame->local;

    if (op_ret < 0) {
        local->op_ret = op_ret;
        local->op_errno = op_errno;

        afr_read_txn_continue(frame, this, (long)cookie);
        return 0;
    }

    AFR_STACK_UNWIND(fstat, frame, op_ret, op_errno, buf, xdata);

    return 0;
}

int
afr_fstat_wind(call_frame_t *frame, xlator_t *this, int subvol)
{
    afr_private_t *priv = NULL;
    afr_local_t *local = NULL;

    priv = this->private;
    local = frame->local;

    if (subvol == -1) {
        AFR_STACK_UNWIND(fstat, frame, local->op_ret, local->op_errno, 0, 0);
        return 0;
    }

    STACK_WIND_COOKIE(
        frame, afr_fstat_cbk, (void *)(long)subvol, priv->children[subvol],
        priv->children[subvol]->fops->fstat, local->fd, local->xdata_req);
    return 0;
}

int32_t
afr_fstat(call_frame_t *frame, xlator_t *this, fd_t *fd, dict_t *xdata)
{
    afr_local_t *local = NULL;
    int op_errno = 0;

    AFR_ERROR_OUT_IF_FDCTX_INVALID(fd, this, op_errno, out);
    local = AFR_FRAME_INIT(frame, op_errno);
    if (!local)
        goto out;

    local->op = GF_FOP_FSTAT;
    local->fd = fd_ref(fd);
    if (xdata)
        local->xdata_req = dict_ref(xdata);

    afr_fix_open(fd, this);

    afr_read_txn(frame, this, fd->inode, afr_fstat_wind, AFR_DATA_TRANSACTION);

    return 0;
out:
    AFR_STACK_UNWIND(fstat, frame, -1, op_errno, NULL, NULL);

    return 0;
}

/* }}} */

/* {{{ readlink */

int
afr_readlink_cbk(call_frame_t *frame, void *cookie, xlator_t *this,
                 int32_t op_ret, int32_t op_errno, const char *buf,
                 struct iatt *sbuf, dict_t *xdata)
{
    afr_local_t *local = NULL;

    local = frame->local;

    if (op_ret < 0) {
        local->op_ret = -1;
        local->op_errno = op_errno;

        afr_read_txn_continue(frame, this, (long)cookie);
        return 0;
    }

    AFR_STACK_UNWIND(readlink, frame, op_ret, op_errno, buf, sbuf, xdata);
    return 0;
}

int
afr_readlink_wind(call_frame_t *frame, xlator_t *this, int subvol)
{
    afr_local_t *local = NULL;
    afr_private_t *priv = NULL;

    local = frame->local;
    priv = this->private;

    if (subvol == -1) {
        AFR_STACK_UNWIND(readlink, frame, local->op_ret, local->op_errno, 0, 0,
                         0);
        return 0;
    }

    STACK_WIND_COOKIE(frame, afr_readlink_cbk, (void *)(long)subvol,
                      priv->children[subvol],
                      priv->children[subvol]->fops->readlink, &local->loc,
                      local->cont.readlink.size, local->xdata_req);
    return 0;
}

int
afr_readlink(call_frame_t *frame, xlator_t *this, loc_t *loc, size_t size,
             dict_t *xdata)
{
    afr_local_t *local = NULL;
    int32_t op_errno = 0;

    local = AFR_FRAME_INIT(frame, op_errno);
    if (!local)
        goto out;

    local->op = GF_FOP_READLINK;
    loc_copy(&local->loc, loc);
    local->cont.readlink.size = size;
    if (xdata)
        local->xdata_req = dict_ref(xdata);

    afr_read_txn(frame, this, loc->inode, afr_readlink_wind,
                 AFR_DATA_TRANSACTION);

    return 0;
out:
    AFR_STACK_UNWIND(readlink, frame, -1, op_errno, 0, 0, 0);

    return 0;
}

/* }}} */

/* {{{ getxattr */

struct _xattr_key {
    char *key;
    struct list_head list;
};

int
__gather_xattr_keys(dict_t *dict, char *key, data_t *value, void *data)
{
    struct list_head *list = data;
    struct _xattr_key *xkey = NULL;

    if (!strncmp(key, AFR_XATTR_PREFIX, SLEN(AFR_XATTR_PREFIX))) {
        xkey = GF_MALLOC(sizeof(*xkey), gf_afr_mt_xattr_key);
        if (!xkey)
            return -1;

        xkey->key = key;
        INIT_LIST_HEAD(&xkey->list);

        list_add_tail(&xkey->list, list);
    }
    return 0;
}

void
afr_filter_xattrs(dict_t *dict)
{
    struct list_head keys = {
        0,
    };
    struct _xattr_key *key = NULL;
    struct _xattr_key *tmp = NULL;

    INIT_LIST_HEAD(&keys);

    dict_foreach(dict, __gather_xattr_keys, (void *)&keys);

    list_for_each_entry_safe(key, tmp, &keys, list)
    {
        dict_del(dict, key->key);

        list_del_init(&key->list);

        GF_FREE(key);
    }
}

static gf_boolean_t
afr_getxattr_ignorable_errnos(int32_t op_errno)
{
    if (op_errno == ENODATA || op_errno == ENOTSUP || op_errno == ERANGE ||
        op_errno == ENAMETOOLONG)
        return _gf_true;

    return _gf_false;
}
int
afr_getxattr_cbk(call_frame_t *frame, void *cookie, xlator_t *this,
                 int32_t op_ret, int32_t op_errno, dict_t *dict, dict_t *xdata)
{
    afr_local_t *local = NULL;

    local = frame->local;

    if (op_ret < 0 && !afr_getxattr_ignorable_errnos(op_errno)) {
        local->op_ret = op_ret;
        local->op_errno = op_errno;

        afr_read_txn_continue(frame, this, (long)cookie);
        return 0;
    }

    if (dict)
        afr_filter_xattrs(dict);

    AFR_STACK_UNWIND(getxattr, frame, op_ret, op_errno, dict, xdata);

    return 0;
}

int
afr_getxattr_wind(call_frame_t *frame, xlator_t *this, int subvol)
{
    afr_local_t *local = NULL;
    afr_private_t *priv = NULL;

    local = frame->local;
    priv = this->private;

    if (subvol == -1) {
        AFR_STACK_UNWIND(getxattr, frame, local->op_ret, local->op_errno, NULL,
                         NULL);
        return 0;
    }

    STACK_WIND_COOKIE(frame, afr_getxattr_cbk, (void *)(long)subvol,
                      priv->children[subvol],
                      priv->children[subvol]->fops->getxattr, &local->loc,
                      local->cont.getxattr.name, local->xdata_req);
    return 0;
}

int32_t
afr_getxattr_unwind(call_frame_t *frame, int op_ret, int op_errno, dict_t *dict,
                    dict_t *xdata)

{
    AFR_STACK_UNWIND(getxattr, frame, op_ret, op_errno, dict, xdata);
    return 0;
}

int32_t
afr_fgetxattr_clrlk_cbk(call_frame_t *frame, void *cookie, xlator_t *this,
                        int32_t op_ret, int32_t op_errno, dict_t *dict,
                        dict_t *xdata)
{
    afr_local_t *local = NULL;
    afr_private_t *priv = NULL;
    xlator_t **children = NULL;
    dict_t *xattr = NULL;
    char *tmp_report = NULL;
    char lk_summary[1024] = {
        0,
    };
    int serz_len = 0;
    int32_t callcnt = 0;
    long int cky = 0;
    int ret = 0;
    int keylen = 0;
    int children_keylen = 0;

    priv = this->private;
    children = priv->children;

    local = frame->local;
    cky = (long)cookie;
    keylen = strlen(local->cont.getxattr.name);
    children_keylen = strlen(children[cky]->name);

    LOCK(&frame->lock);
    {
        callcnt = --local->call_count;
        if (op_ret == -1)
            local->replies[cky].op_errno = op_errno;

        if (!local->dict)
            local->dict = dict_new();
        if (local->dict) {
            ret = dict_get_strn(dict, local->cont.getxattr.name, keylen,
                                &tmp_report);
            if (ret)
                goto unlock;
            ret = dict_set_dynstrn(local->dict, children[cky]->name,
                                   children_keylen, gf_strdup(tmp_report));
            if (ret)
                goto unlock;
        }
    }
unlock:
    UNLOCK(&frame->lock);

    if (!callcnt) {
        xattr = dict_new();
        if (!xattr) {
            op_ret = -1;
            op_errno = ENOMEM;
            goto unwind;
        }
        ret = dict_serialize_value_with_delim(local->dict, lk_summary,
                                              &serz_len, '\n');
        if (ret) {
            op_ret = -1;
            op_errno = ENOMEM;
            goto unwind;
        }
        if (serz_len == -1)
            snprintf(lk_summary, sizeof(lk_summary), "No locks cleared.");
        ret = dict_set_dynstrn(xattr, local->cont.getxattr.name, keylen,
                               gf_strdup(lk_summary));
        if (ret) {
            op_ret = -1;
            op_errno = ENOMEM;
            gf_msg(this->name, GF_LOG_ERROR, ENOMEM, AFR_MSG_DICT_SET_FAILED,
                   "Error setting dictionary");
            goto unwind;
        }

        op_errno = afr_final_errno(local, priv);

    unwind:
        AFR_STACK_UNWIND(fgetxattr, frame, op_ret, op_errno, xattr, xdata);
        if (xattr)
            dict_unref(xattr);
    }

    return ret;
}

int32_t
afr_getxattr_clrlk_cbk(call_frame_t *frame, void *cookie, xlator_t *this,
                       int32_t op_ret, int32_t op_errno, dict_t *dict,
                       dict_t *xdata)
{
    afr_local_t *local = NULL;
    afr_private_t *priv = NULL;
    xlator_t **children = NULL;
    dict_t *xattr = NULL;
    char *tmp_report = NULL;
    char lk_summary[1024] = {
        0,
    };
    int serz_len = 0;
    int32_t callcnt = 0;
    long int cky = 0;
    int ret = 0;
    int keylen = 0;
    int children_keylen = 0;

    priv = this->private;
    children = priv->children;

    local = frame->local;
    cky = (long)cookie;

    keylen = strlen(local->cont.getxattr.name);
    children_keylen = strlen(children[cky]->name);

    LOCK(&frame->lock);
    {
        callcnt = --local->call_count;
        if (op_ret == -1)
            local->replies[cky].op_errno = op_errno;

        if (!local->dict)
            local->dict = dict_new();
        if (local->dict) {
            ret = dict_get_strn(dict, local->cont.getxattr.name, keylen,
                                &tmp_report);
            if (ret)
                goto unlock;
            ret = dict_set_dynstrn(local->dict, children[cky]->name,
                                   children_keylen, gf_strdup(tmp_report));
            if (ret)
                goto unlock;
        }
    }
unlock:
    UNLOCK(&frame->lock);

    if (!callcnt) {
        xattr = dict_new();
        if (!xattr) {
            op_ret = -1;
            op_errno = ENOMEM;
            goto unwind;
        }
        ret = dict_serialize_value_with_delim(local->dict, lk_summary,
                                              &serz_len, '\n');
        if (ret) {
            op_ret = -1;
            op_errno = ENOMEM;
            goto unwind;
        }
        if (serz_len == -1)
            snprintf(lk_summary, sizeof(lk_summary), "No locks cleared.");
        ret = dict_set_dynstrn(xattr, local->cont.getxattr.name, keylen,
                               gf_strdup(lk_summary));
        if (ret) {
            op_ret = -1;
            op_errno = ENOMEM;
            gf_msg(this->name, GF_LOG_ERROR, ENOMEM, AFR_MSG_DICT_SET_FAILED,
                   "Error setting dictionary");
            goto unwind;
        }

        op_errno = afr_final_errno(local, priv);

    unwind:
        AFR_STACK_UNWIND(getxattr, frame, op_ret, op_errno, xattr, xdata);

        if (xattr)
            dict_unref(xattr);
    }

    return ret;
}

/**
 * node-uuid cbk uses next child querying mechanism
 */
int32_t
afr_getxattr_node_uuid_cbk(call_frame_t *frame, void *cookie, xlator_t *this,
                           int32_t op_ret, int32_t op_errno, dict_t *dict,
                           dict_t *xdata)
{
    afr_private_t *priv = NULL;
    afr_local_t *local = NULL;
    xlator_t **children = NULL;
    int unwind = 1;
    int curr_call_child = 0;

    priv = this->private;
    children = priv->children;

    local = frame->local;

    if (op_ret == -1) { /** query the _next_ child */

        /**
         * _current_ becomes _next_
         * If done with all children and yet no success; give up !
         */
        curr_call_child = (int)((long)cookie);
        if (++curr_call_child == priv->child_count)
            goto unwind;

        gf_msg_debug(this->name, op_errno,
                     "op_ret (-1): Re-querying afr-child (%d/%d)",
                     curr_call_child, priv->child_count);

        unwind = 0;
        STACK_WIND_COOKIE(
            frame, afr_getxattr_node_uuid_cbk, (void *)(long)curr_call_child,
            children[curr_call_child],
            children[curr_call_child]->fops->getxattr, &local->loc,
            local->cont.getxattr.name, local->xdata_req);
    }

unwind:
    if (unwind)
        AFR_STACK_UNWIND(getxattr, frame, op_ret, op_errno, dict, xdata);

    return 0;
}

/**
 * list-node-uuids cbk returns the list of node_uuids for the subvolume.
 */
int32_t
afr_getxattr_list_node_uuids_cbk(call_frame_t *frame, void *cookie,
                                 xlator_t *this, int32_t op_ret,
                                 int32_t op_errno, dict_t *dict, dict_t *xdata)
{
    afr_local_t *local = NULL;
    afr_private_t *priv = NULL;
    int32_t callcnt = 0;
    int ret = 0;
    char *xattr_serz = NULL;
    long cky = 0;
    int32_t tlen = 0;

    local = frame->local;
    priv = this->private;
    cky = (long)cookie;

    LOCK(&frame->lock);
    {
        callcnt = --local->call_count;
        local->replies[cky].valid = 1;
        local->replies[cky].op_ret = op_ret;
        local->replies[cky].op_errno = op_errno;

        if (op_ret < 0)
            goto unlock;

        local->op_ret = 0;

        if (!local->xdata_rsp && xdata)
            local->xdata_rsp = dict_ref(xdata);
        local->replies[cky].xattr = dict_ref(dict);
    }

unlock:
    UNLOCK(&frame->lock);

    if (!callcnt) {
        if (local->op_ret != 0) {
            /* All bricks gave an error. */
            local->op_errno = afr_final_errno(local, priv);
            goto unwind;
        }

        /*Since we store the UUID0_STR as node uuid for down bricks and
         *for non zero op_ret, assigning length to  priv->child_count
         *number of uuids*/
        local->cont.getxattr.xattr_len = (SLEN(UUID0_STR) + 2) *
                                         priv->child_count;

        if (!local->dict)
            local->dict = dict_new();
        if (!local->dict) {
            local->op_ret = -1;
            local->op_errno = ENOMEM;
            goto unwind;
        }

        xattr_serz = GF_CALLOC(local->cont.getxattr.xattr_len, sizeof(char),
                               gf_common_mt_char);

        if (!xattr_serz) {
            local->op_ret = -1;
            local->op_errno = ENOMEM;
            goto unwind;
        }

        ret = afr_serialize_xattrs_with_delimiter(frame, this, xattr_serz,
                                                  UUID0_STR, &tlen, ' ');
        if (ret) {
            local->op_ret = -1;
            local->op_errno = ENOMEM;
            GF_FREE(xattr_serz);
            goto unwind;
        }
        ret = dict_set_dynstr_sizen(local->dict, GF_XATTR_LIST_NODE_UUIDS_KEY,
                                    xattr_serz);
        if (ret) {
            gf_msg(this->name, GF_LOG_ERROR, -ret, AFR_MSG_DICT_SET_FAILED,
                   "Cannot set node_uuid key in dict");
            local->op_ret = -1;
            local->op_errno = ENOMEM;
            if (ret == -EINVAL)
                GF_FREE(xattr_serz);
        } else {
            local->op_ret = local->cont.getxattr.xattr_len - 1;
            local->op_errno = 0;
        }

    unwind:
        AFR_STACK_UNWIND(getxattr, frame, local->op_ret, local->op_errno,
                         local->dict, local->xdata_rsp);
    }

    return ret;
}

int32_t
afr_getxattr_quota_size_cbk(call_frame_t *frame, void *cookie, xlator_t *this,
                            int32_t op_ret, int32_t op_errno, dict_t *dict,
                            dict_t *xdata)
{
    int idx = (long)cookie;
    int call_count = 0;
    afr_local_t *local = frame->local;
    int read_subvol = -1;

    local->replies[idx].valid = 1;
    local->replies[idx].op_ret = op_ret;
    local->replies[idx].op_errno = op_errno;
    if (dict)
        local->replies[idx].xdata = dict_ref(dict);
    call_count = afr_frame_return(frame);
    if (call_count == 0) {
        local->inode = inode_ref(local->loc.inode);
        read_subvol = afr_handle_quota_size(frame, this);
        if (read_subvol != -1) {
            op_ret = local->replies[read_subvol].op_ret;
            op_errno = local->replies[read_subvol].op_errno;
            dict = local->replies[read_subvol].xdata;
        }
        AFR_STACK_UNWIND(getxattr, frame, op_ret, op_errno, dict, xdata);
    }

    return 0;
}

static int32_t
afr_update_local_dicts(call_frame_t *frame, dict_t *dict, dict_t *xdata)
{
    afr_local_t *local;
    dict_t *local_dict;
    dict_t *local_xdata;
    int32_t ret;

    local = frame->local;
    local_dict = NULL;
    local_xdata = NULL;

    ret = -ENOMEM;

    if ((dict != NULL) && (local->dict == NULL)) {
        local_dict = dict_new();
        if (local_dict == NULL) {
            goto done;
        }
    }

    if ((xdata != NULL) && (local->xdata_rsp == NULL)) {
        local_xdata = dict_new();
        if (local_xdata == NULL) {
            goto done;
        }
    }

    if ((local_dict != NULL) || (local_xdata != NULL)) {
        /* TODO: Maybe it would be better to preallocate both dicts before
         *       sending the requests. This way we don't need to use a LOCK()
         *       here. */
        LOCK(&frame->lock);

        if ((local_dict != NULL) && (local->dict == NULL)) {
            local->dict = local_dict;
            local_dict = NULL;
        }

        if ((local_xdata != NULL) && (local->xdata_rsp == NULL)) {
            local->xdata_rsp = local_xdata;
            local_xdata = NULL;
        }

        UNLOCK(&frame->lock);
    }

    if (dict != NULL) {
        if (dict_copy(dict, local->dict) == NULL) {
            goto done;
        }
    }

    if (xdata != NULL) {
        if (dict_copy(xdata, local->xdata_rsp) == NULL) {
            goto done;
        }
    }

    ret = 0;

done:
    if (local_dict != NULL) {
        dict_unref(local_dict);
    }

    if (local_xdata != NULL) {
        dict_unref(local_xdata);
    }

    return ret;
}

static void
afr_getxattr_lockinfo_cbk_common(call_frame_t *frame, int32_t op_ret,
                                 int32_t op_errno, dict_t *dict, dict_t *xdata,
                                 bool is_fgetxattr)
{
    int len = 0;
    char *lockinfo_buf = NULL;
    dict_t *lockinfo = NULL, *newdict = NULL;
    afr_local_t *local = NULL;

    local = frame->local;

    if ((op_ret >= 0) && (dict != NULL)) {
        op_ret = dict_get_ptr_and_len(dict, GF_XATTR_LOCKINFO_KEY,
                                      (void **)&lockinfo_buf, &len);
        if (lockinfo_buf != NULL) {
            lockinfo = dict_new();
            if (lockinfo == NULL) {
                op_ret = -1;
            } else {
                op_ret = dict_unserialize(lockinfo_buf, len, &lockinfo);
            }
        }
    }

    if ((op_ret >= 0) && ((lockinfo != NULL) || (xdata != NULL))) {
        op_ret = afr_update_local_dicts(frame, lockinfo, xdata);
        if (lockinfo != NULL) {
            dict_unref(lockinfo);
        }
    }

    if (op_ret < 0) {
        local->op_ret = -1;
        local->op_errno = ENOMEM;
    }

    if (uatomic_sub_return(&local->call_count, 1) == 0) {
        newdict = dict_new();
        if (!newdict) {
            local->op_ret = -1;
            local->op_errno = op_errno = ENOMEM;
            goto unwind;
        }

        op_ret = dict_allocate_and_serialize(
            local->dict, (char **)&lockinfo_buf, (unsigned int *)&len);
        if (op_ret != 0) {
            local->op_ret = -1;
            local->op_errno = op_errno = ENOMEM;
            goto unwind;
        }

        op_ret = dict_set_dynptr(newdict, GF_XATTR_LOCKINFO_KEY,
                                 (void *)lockinfo_buf, len);
        if (op_ret < 0) {
            GF_FREE(lockinfo_buf);
            local->op_ret = op_ret = -1;
            local->op_errno = op_errno = -op_ret;
            goto unwind;
        }

    unwind:
        /* TODO: These unwinds use op_ret and op_errno instead of local->op_ret
         *       and local->op_errno. This doesn't seem right because any
         *       failure during processing of each answer could be silently
         *       ignored. This is kept this was the old behavior and because
         *       local->op_ret is initialized as -1 and local->op_errno is
         *       initialized as EUCLEAN, which makes these values useless. */
        if (is_fgetxattr) {
            AFR_STACK_UNWIND(fgetxattr, frame, op_ret, op_errno, newdict,
                             local->xdata_rsp);
        } else {
            AFR_STACK_UNWIND(getxattr, frame, op_ret, op_errno, newdict,
                             local->xdata_rsp);
        }

        if (newdict != NULL) {
            dict_unref(newdict);
        }
    }
}

static int32_t
afr_getxattr_lockinfo_cbk(call_frame_t *frame, void *cookie, xlator_t *this,
                          int32_t op_ret, int32_t op_errno, dict_t *dict,
                          dict_t *xdata)
{
    afr_getxattr_lockinfo_cbk_common(frame, op_ret, op_errno, dict, xdata,
                                     false);

    return 0;
}

static int32_t
afr_fgetxattr_lockinfo_cbk(call_frame_t *frame, void *cookie, xlator_t *this,
                           int32_t op_ret, int32_t op_errno, dict_t *dict,
                           dict_t *xdata)
{
    afr_getxattr_lockinfo_cbk_common(frame, op_ret, op_errno, dict, xdata,
                                     true);

    return 0;
}

int32_t
afr_fgetxattr_pathinfo_cbk(call_frame_t *frame, void *cookie, xlator_t *this,
                           int32_t op_ret, int32_t op_errno, dict_t *dict,
                           dict_t *xdata)
{
    afr_local_t *local = NULL;
    int32_t callcnt = 0;
    int ret = 0;
    char *xattr = NULL;
    char *xattr_serz = NULL;
    int keylen = 0;
    char xattr_cky[1024] = {
        0,
    };
    int xattr_cky_len = 0;
    dict_t *nxattr = NULL;
    long cky = 0;
    int32_t padding = 0;
    int32_t tlen = 0;

    if (!frame || !frame->local || !this) {
        gf_msg("", GF_LOG_ERROR, 0, AFR_MSG_INVALID_ARG, "possible NULL deref");
        goto out;
    }

    local = frame->local;
    cky = (long)cookie;
    keylen = strlen(local->cont.getxattr.name);
    xattr_cky_len = snprintf(xattr_cky, sizeof(xattr_cky), "%s-%ld",
                             local->cont.getxattr.name, cky);
    LOCK(&frame->lock);
    {
        callcnt = --local->call_count;

        if (op_ret < 0) {
            local->op_errno = op_errno;
        } else {
            local->op_ret = op_ret;
            if (!local->xdata_rsp && xdata)
                local->xdata_rsp = dict_ref(xdata);
        }

        if (!dict || (op_ret < 0))
            goto unlock;

        if (!local->dict) {
            local->dict = dict_new();
            if (!local->dict)
                goto unlock;
        }
        ret = dict_get_strn(dict, local->cont.getxattr.name, keylen, &xattr);
        if (ret)
            goto unlock;

        xattr = gf_strdup(xattr);

        ret = dict_set_dynstrn(local->dict, xattr_cky, xattr_cky_len, xattr);
        if (ret) {
            UNLOCK(&frame->lock);
            gf_msg(this->name, GF_LOG_ERROR, -ret, AFR_MSG_DICT_SET_FAILED,
                   "Cannot set xattr cookie key");
            goto post_unlock;
        }

        local->cont.getxattr.xattr_len += strlen(xattr) + 1;
    }
unlock:
    UNLOCK(&frame->lock);
post_unlock:
    if (!callcnt) {
        if (!local->cont.getxattr.xattr_len)
            goto unwind;

        nxattr = dict_new();
        if (!nxattr)
            goto unwind;

        /* extra bytes for decorations (brackets and <>'s) */
        padding += strlen(this->name) + SLEN(AFR_PATHINFO_HEADER) + 4;
        local->cont.getxattr.xattr_len += (padding + 2);

        xattr_serz = GF_MALLOC(local->cont.getxattr.xattr_len,
                               gf_common_mt_char);

        if (!xattr_serz)
            goto unwind;

        /* the xlator info */
        int xattr_serz_len = sprintf(
            xattr_serz, "(<" AFR_PATHINFO_HEADER "%s> ", this->name);

        /* actual series of pathinfo */
        ret = dict_serialize_value_with_delim(
            local->dict, xattr_serz + xattr_serz_len, &tlen, ' ');
        if (ret) {
            GF_FREE(xattr_serz);
            goto unwind;
        }

        /* closing part */
        *(xattr_serz + padding + tlen) = ')';
        *(xattr_serz + padding + tlen + 1) = '\0';

        ret = dict_set_dynstrn(nxattr, local->cont.getxattr.name, keylen,
                               xattr_serz);
        if (ret) {
            gf_msg(this->name, GF_LOG_ERROR, -ret, AFR_MSG_DICT_SET_FAILED,
                   "Cannot set pathinfo key in dict");
            if (ret == -EINVAL)
                GF_FREE(xattr_serz);
        }

    unwind:
        AFR_STACK_UNWIND(fgetxattr, frame, local->op_ret, local->op_errno,
                         nxattr, local->xdata_rsp);

        if (nxattr)
            dict_unref(nxattr);
    }

out:
    return ret;
}

int32_t
afr_getxattr_pathinfo_cbk(call_frame_t *frame, void *cookie, xlator_t *this,
                          int32_t op_ret, int32_t op_errno, dict_t *dict,
                          dict_t *xdata)
{
    afr_local_t *local = NULL;
    int32_t callcnt = 0;
    int ret = 0;
    char *xattr = NULL;
    char *xattr_serz = NULL;
    char xattr_cky[1024] = {
        0,
    };
    int keylen = 0;
    int xattr_cky_len = 0;
    dict_t *nxattr = NULL;
    long cky = 0;
    int32_t padding = 0;
    int32_t tlen = 0;

    if (!frame || !frame->local || !this) {
        gf_msg("", GF_LOG_ERROR, 0, AFR_MSG_INVALID_ARG, "possible NULL deref");
        goto out;
    }

    local = frame->local;
    cky = (long)cookie;
    keylen = strlen(local->cont.getxattr.name);
    xattr_cky_len = snprintf(xattr_cky, sizeof(xattr_cky), "%s-%ld",
                             local->cont.getxattr.name, cky);
    LOCK(&frame->lock);
    {
        callcnt = --local->call_count;

        if (op_ret < 0) {
            local->op_errno = op_errno;
        } else {
            local->op_ret = op_ret;
            if (!local->xdata_rsp && xdata)
                local->xdata_rsp = dict_ref(xdata);
        }

        if (!dict || (op_ret < 0))
            goto unlock;

        if (!local->dict) {
            local->dict = dict_new();
            if (!local->dict)
                goto unlock;
        }
        ret = dict_get_strn(dict, local->cont.getxattr.name, keylen, &xattr);
        if (ret)
            goto unlock;

        xattr = gf_strdup(xattr);

        ret = dict_set_dynstrn(local->dict, xattr_cky, xattr_cky_len, xattr);
        if (ret) {
            UNLOCK(&frame->lock);
            gf_msg(this->name, GF_LOG_ERROR, -ret, AFR_MSG_DICT_SET_FAILED,
                   "Cannot set xattr cookie key");
            goto post_unlock;
        }

        local->cont.getxattr.xattr_len += strlen(xattr) + 1;
    }
unlock:
    UNLOCK(&frame->lock);
post_unlock:
    if (!callcnt) {
        if (!local->cont.getxattr.xattr_len)
            goto unwind;

        nxattr = dict_new();
        if (!nxattr)
            goto unwind;

        /* extra bytes for decorations (brackets and <>'s) */
        padding += strlen(this->name) + SLEN(AFR_PATHINFO_HEADER) + 4;
        local->cont.getxattr.xattr_len += (padding + 2);

        xattr_serz = GF_MALLOC(local->cont.getxattr.xattr_len,
                               gf_common_mt_char);

        if (!xattr_serz)
            goto unwind;

        /* the xlator info */
        int xattr_serz_len = sprintf(
            xattr_serz, "(<" AFR_PATHINFO_HEADER "%s> ", this->name);

        /* actual series of pathinfo */
        ret = dict_serialize_value_with_delim(
            local->dict, xattr_serz + xattr_serz_len, &tlen, ' ');
        if (ret) {
            GF_FREE(xattr_serz);
            goto unwind;
        }

        /* closing part */
        *(xattr_serz + padding + tlen) = ')';
        *(xattr_serz + padding + tlen + 1) = '\0';

        ret = dict_set_dynstrn(nxattr, local->cont.getxattr.name, keylen,
                               xattr_serz);
        if (ret) {
            gf_msg(this->name, GF_LOG_ERROR, -ret, AFR_MSG_DICT_SET_FAILED,
                   "Cannot set pathinfo key in dict");
            if (ret == -EINVAL)
                GF_FREE(xattr_serz);
        }

    unwind:
        AFR_STACK_UNWIND(getxattr, frame, local->op_ret, local->op_errno,
                         nxattr, local->xdata_rsp);

        if (nxattr)
            dict_unref(nxattr);
    }

out:
    return ret;
}

static int
afr_aggregate_stime_xattr(dict_t *this, char *key, data_t *value, void *data)
{
    int ret = 0;

    if (fnmatch(GF_XATTR_STIME_PATTERN, key, FNM_NOESCAPE) == 0)
        ret = gf_get_max_stime(THIS, data, key, value);

    return ret;
}

int32_t
afr_common_getxattr_stime_cbk(call_frame_t *frame, void *cookie, xlator_t *this,
                              int32_t op_ret, int32_t op_errno, dict_t *dict,
                              dict_t *xdata)
{
    afr_local_t *local = NULL;
    int32_t callcnt = 0;

    if (!frame || !frame->local || !this) {
        gf_msg("", GF_LOG_ERROR, 0, AFR_MSG_INVALID_ARG, "possible NULL deref");
        goto out;
    }

    local = frame->local;

    LOCK(&frame->lock);
    {
        callcnt = --local->call_count;

        if (!dict || (op_ret < 0)) {
            local->op_errno = op_errno;
            goto cleanup;
        }

        if (!local->dict)
            local->dict = dict_copy_with_ref(dict, NULL);
        else
            dict_foreach(dict, afr_aggregate_stime_xattr, local->dict);
        local->op_ret = 0;
    }

cleanup:
    UNLOCK(&frame->lock);

    if (!callcnt) {
        AFR_STACK_UNWIND(getxattr, frame, local->op_ret, local->op_errno,
                         local->dict, xdata);
    }

out:
    return 0;
}

static gf_boolean_t
afr_is_special_xattr(const char *name, fop_getxattr_cbk_t *cbk,
                     gf_boolean_t is_fgetxattr)
{
    gf_boolean_t is_spl = _gf_true;

    GF_ASSERT(cbk);
    if (!cbk || !name) {
        is_spl = _gf_false;
        goto out;
    }

    if (!strcmp(name, GF_XATTR_PATHINFO_KEY) ||
        !strcmp(name, GF_XATTR_USER_PATHINFO_KEY)) {
        if (is_fgetxattr) {
            *cbk = afr_fgetxattr_pathinfo_cbk;
        } else {
            *cbk = afr_getxattr_pathinfo_cbk;
        }
    } else if (!strncmp(name, GF_XATTR_CLRLK_CMD, SLEN(GF_XATTR_CLRLK_CMD))) {
        if (is_fgetxattr) {
            *cbk = afr_fgetxattr_clrlk_cbk;
        } else {
            *cbk = afr_getxattr_clrlk_cbk;
        }
    } else if (!strncmp(name, GF_XATTR_LOCKINFO_KEY,
                        SLEN(GF_XATTR_LOCKINFO_KEY))) {
        if (is_fgetxattr) {
            *cbk = afr_fgetxattr_lockinfo_cbk;
        } else {
            *cbk = afr_getxattr_lockinfo_cbk;
        }
    } else if (fnmatch(GF_XATTR_STIME_PATTERN, name, FNM_NOESCAPE) == 0) {
        *cbk = afr_common_getxattr_stime_cbk;
    } else if (strcmp(name, QUOTA_SIZE_KEY) == 0) {
        *cbk = afr_getxattr_quota_size_cbk;
    } else if (!strcmp(name, GF_XATTR_LIST_NODE_UUIDS_KEY)) {
        *cbk = afr_getxattr_list_node_uuids_cbk;
    } else {
        is_spl = _gf_false;
    }

out:
    return is_spl;
}

static void
afr_getxattr_all_subvols(xlator_t *this, call_frame_t *frame, const char *name,
                         loc_t *loc, fop_getxattr_cbk_t cbk)
{
    afr_private_t *priv = NULL;
    afr_local_t *local = NULL;
    int i = 0;
    int call_count = 0;

    priv = this->private;

    local = frame->local;
    // local->call_count set in afr_local_init
    call_count = local->call_count;

    if (!strcmp(name, GF_XATTR_LIST_NODE_UUIDS_KEY)) {
        GF_FREE(local->cont.getxattr.name);
        local->cont.getxattr.name = gf_strdup(GF_XATTR_NODE_UUID_KEY);
    }

    // If up-children count is 0, afr_local_init would have failed already
    // and the call would have unwound so not handling it here.
    for (i = 0; i < priv->child_count; i++) {
        if (local->child_up[i]) {
            STACK_WIND_COOKIE(frame, cbk, (void *)(long)i, priv->children[i],
                              priv->children[i]->fops->getxattr, loc,
                              local->cont.getxattr.name, NULL);
            if (!--call_count)
                break;
        }
    }
    return;
}

int
afr_marker_populate_args(call_frame_t *frame, int type, int *gauge,
                         xlator_t **subvols)
{
    xlator_t *this = frame->this;
    afr_private_t *priv = this->private;

    memcpy(subvols, priv->children, sizeof(*subvols) * priv->child_count);

    if (type == MARKER_XTIME_TYPE) {
        /*Don't error out on ENOENT/ENOTCONN */
        gauge[MCNT_NOTFOUND] = 0;
        gauge[MCNT_ENOTCONN] = 0;
    }
    return priv->child_count;
}

static int
afr_handle_heal_xattrs(call_frame_t *frame, xlator_t *this, loc_t *loc,
                       const char *heal_op)
{
    int ret = -1;
    afr_spb_status_t *data = NULL;

    if (!strcmp(heal_op, GF_HEAL_INFO)) {
        afr_get_heal_info(frame, this, loc);
        ret = 0;
        goto out;
    }

    if (!strcmp(heal_op, GF_AFR_HEAL_SBRAIN)) {
        afr_heal_splitbrain_file(frame, this, loc);
        ret = 0;
        goto out;
    }

    if (!strcmp(heal_op, GF_AFR_SBRAIN_STATUS)) {
        data = GF_CALLOC(1, sizeof(*data), gf_afr_mt_spb_status_t);
        if (!data) {
            ret = 1;
            goto out;
        }
        data->frame = frame;
        data->loc = loc;
        ret = synctask_new(this->ctx->env, afr_get_split_brain_status,
                           afr_get_split_brain_status_cbk, NULL, data);
        if (ret) {
            gf_msg(this->name, GF_LOG_ERROR, 0, AFR_MSG_SPLIT_BRAIN_STATUS,
                   "Failed to create"
                   " synctask. Unable to fetch split-brain status"
                   " for %s.",
                   loc->name);
            ret = 1;
            goto out;
        }
        goto out;
    }

out:
    if (ret == 1) {
        AFR_STACK_UNWIND(getxattr, frame, -1, ENOMEM, NULL, NULL);
        if (data)
            GF_FREE(data);
        ret = 0;
    }
    return ret;
}

int32_t
afr_getxattr(call_frame_t *frame, xlator_t *this, loc_t *loc, const char *name,
             dict_t *xdata)
{
    afr_private_t *priv = NULL;
    afr_local_t *local = NULL;
    xlator_t **children = NULL;
    int i = 0;
    int32_t op_errno = 0;
    int ret = -1;
    fop_getxattr_cbk_t cbk = NULL;

    local = AFR_FRAME_INIT(frame, op_errno);
    if (!local)
        goto out;

    priv = this->private;

    children = priv->children;

    loc_copy(&local->loc, loc);

    local->op = GF_FOP_GETXATTR;

    if (xdata)
        local->xdata_req = dict_ref(xdata);

    if (!name)
        goto no_name;

    local->cont.getxattr.name = gf_strdup(name);

    if (!local->cont.getxattr.name) {
        op_errno = ENOMEM;
        goto out;
    }

    if (!strncmp(name, AFR_XATTR_PREFIX, SLEN(AFR_XATTR_PREFIX))) {
        op_errno = ENODATA;
        goto out;
    }

    if (cluster_handle_marker_getxattr(frame, loc, name, priv->vol_uuid,
                                       afr_getxattr_unwind,
                                       afr_marker_populate_args) == 0)
        return 0;

    ret = afr_handle_heal_xattrs(frame, this, &local->loc, name);
    if (ret == 0)
        return 0;

    /*
     * Heal daemons don't have IO threads ... and as a result they
     * send this getxattr down and eventually crash :(
     */
    GF_CHECK_XATTR_KEY_AND_GOTO(name, IO_THREADS_QUEUE_SIZE_KEY, op_errno, out);

    /*
     * Special xattrs which need responses from all subvols
     */
    if (afr_is_special_xattr(name, &cbk, 0)) {
        afr_getxattr_all_subvols(this, frame, name, loc, cbk);
        return 0;
    }

    if (XATTR_IS_NODE_UUID(name)) {
        i = 0;
        STACK_WIND_COOKIE(frame, afr_getxattr_node_uuid_cbk, (void *)(long)i,
                          children[i], children[i]->fops->getxattr, loc, name,
                          xdata);
        return 0;
    }

no_name:

    afr_read_txn(frame, this, local->loc.inode, afr_getxattr_wind,
                 AFR_METADATA_TRANSACTION);

    ret = 0;
out:
    if (ret < 0)
        AFR_STACK_UNWIND(getxattr, frame, -1, op_errno, NULL, NULL);

    return 0;
}

/* {{{ fgetxattr */

int32_t
afr_fgetxattr_cbk(call_frame_t *frame, void *cookie, xlator_t *this,
                  int32_t op_ret, int32_t op_errno, dict_t *dict, dict_t *xdata)
{
    afr_local_t *local = NULL;

    local = frame->local;

    if (op_ret < 0) {
        local->op_ret = -1;
        local->op_errno = op_errno;

        afr_read_txn_continue(frame, this, (long)cookie);
        return 0;
    }

    if (dict)
        afr_filter_xattrs(dict);

    AFR_STACK_UNWIND(fgetxattr, frame, op_ret, op_errno, dict, xdata);

    return 0;
}

int
afr_fgetxattr_wind(call_frame_t *frame, xlator_t *this, int subvol)
{
    afr_local_t *local = NULL;
    afr_private_t *priv = NULL;

    local = frame->local;
    priv = this->private;

    if (subvol == -1) {
        AFR_STACK_UNWIND(fgetxattr, frame, local->op_ret, local->op_errno, NULL,
                         NULL);
        return 0;
    }

    STACK_WIND_COOKIE(frame, afr_fgetxattr_cbk, (void *)(long)subvol,
                      priv->children[subvol],
                      priv->children[subvol]->fops->fgetxattr, local->fd,
                      local->cont.getxattr.name, local->xdata_req);
    return 0;
}

static void
afr_fgetxattr_all_subvols(xlator_t *this, call_frame_t *frame,
                          fop_fgetxattr_cbk_t cbk)
{
    afr_private_t *priv = NULL;
    afr_local_t *local = NULL;
    int i = 0;
    int call_count = 0;

    priv = this->private;

    local = frame->local;
    // local->call_count set in afr_local_init
    call_count = local->call_count;

    // If up-children count is 0, afr_local_init would have failed already
    // and the call would have unwound so not handling it here.

    for (i = 0; i < priv->child_count; i++) {
        if (local->child_up[i]) {
            STACK_WIND_COOKIE(frame, cbk, (void *)(long)i, priv->children[i],
                              priv->children[i]->fops->fgetxattr, local->fd,
                              local->cont.getxattr.name, NULL);
            if (!--call_count)
                break;
        }
    }

    return;
}

int
afr_fgetxattr(call_frame_t *frame, xlator_t *this, fd_t *fd, const char *name,
              dict_t *xdata)
{
    afr_local_t *local = NULL;
    int32_t op_errno = 0;
    fop_fgetxattr_cbk_t cbk = NULL;

    AFR_ERROR_OUT_IF_FDCTX_INVALID(fd, this, op_errno, out);
    local = AFR_FRAME_INIT(frame, op_errno);
    if (!local)
        goto out;

    local->op = GF_FOP_FGETXATTR;
    local->fd = fd_ref(fd);
    if (name) {
        local->cont.getxattr.name = gf_strdup(name);
        if (!local->cont.getxattr.name) {
            op_errno = ENOMEM;
            goto out;
        }
    }
    if (xdata)
        local->xdata_req = dict_ref(xdata);

    /* pathinfo gets handled only in getxattr(), but we need to handle
     * lockinfo.
     * If we are doing fgetxattr with lockinfo as the key then we
     * collect information from all children.
     */
    if (afr_is_special_xattr(name, &cbk, 1)) {
        afr_fgetxattr_all_subvols(this, frame, cbk);
        return 0;
    }

    afr_fix_open(fd, this);

    afr_read_txn(frame, this, fd->inode, afr_fgetxattr_wind,
                 AFR_METADATA_TRANSACTION);

    return 0;
out:
    AFR_STACK_UNWIND(fgetxattr, frame, -1, op_errno, NULL, NULL);

    return 0;
}

/* }}} */

/* {{{ readv */

int
afr_readv_cbk(call_frame_t *frame, void *cookie, xlator_t *this, int32_t op_ret,
              int32_t op_errno, struct iovec *vector, int32_t count,
              struct iatt *buf, struct iobref *iobref, dict_t *xdata)
{
    afr_local_t *local = NULL;

    local = frame->local;

    if (op_ret < 0) {
        local->op_ret = -1;
        local->op_errno = op_errno;

        afr_read_txn_continue(frame, this, (long)cookie);
        return 0;
    }

    AFR_STACK_UNWIND(readv, frame, op_ret, op_errno, vector, count, buf, iobref,
                     xdata);
    return 0;
}

int
afr_readv_wind(call_frame_t *frame, xlator_t *this, int subvol)
{
    afr_local_t *local = NULL;
    afr_private_t *priv = NULL;

    local = frame->local;
    priv = this->private;

    if (subvol == -1) {
        AFR_STACK_UNWIND(readv, frame, local->op_ret, local->op_errno, 0, 0, 0,
                         0, 0);
        return 0;
    }

    STACK_WIND_COOKIE(
        frame, afr_readv_cbk, (void *)(long)subvol, priv->children[subvol],
        priv->children[subvol]->fops->readv, local->fd, local->cont.readv.size,
        local->cont.readv.offset, local->cont.readv.flags, local->xdata_req);
    return 0;
}

int
afr_readv(call_frame_t *frame, xlator_t *this, fd_t *fd, size_t size,
          off_t offset, uint32_t flags, dict_t *xdata)
{
    afr_local_t *local = NULL;
    int32_t op_errno = 0;

    AFR_ERROR_OUT_IF_FDCTX_INVALID(fd, this, op_errno, out);
    local = AFR_FRAME_INIT(frame, op_errno);
    if (!local)
        goto out;

    local->op = GF_FOP_READ;
    local->fd = fd_ref(fd);
    local->cont.readv.size = size;
    local->cont.readv.offset = offset;
    local->cont.readv.flags = flags;
    if (xdata)
        local->xdata_req = dict_ref(xdata);

    afr_fix_open(fd, this);

    afr_read_txn(frame, this, fd->inode, afr_readv_wind, AFR_DATA_TRANSACTION);

    return 0;
out:
    AFR_STACK_UNWIND(readv, frame, -1, op_errno, 0, 0, 0, 0, 0);

    return 0;
}

/* }}} */

/* {{{ seek */

int
afr_seek_cbk(call_frame_t *frame, void *cookie, xlator_t *this, int32_t op_ret,
             int32_t op_errno, off_t offset, dict_t *xdata)
{
    afr_local_t *local = NULL;

    local = frame->local;

    if (op_ret < 0) {
        local->op_ret = -1;
        local->op_errno = op_errno;

        afr_read_txn_continue(frame, this, (long)cookie);
        return 0;
    }

    AFR_STACK_UNWIND(seek, frame, op_ret, op_errno, offset, xdata);
    return 0;
}

int
afr_seek_wind(call_frame_t *frame, xlator_t *this, int subvol)
{
    afr_local_t *local = NULL;
    afr_private_t *priv = NULL;

    local = frame->local;
    priv = this->private;

    if (subvol == -1) {
        AFR_STACK_UNWIND(seek, frame, local->op_ret, local->op_errno, 0, NULL);
        return 0;
    }

    STACK_WIND_COOKIE(
        frame, afr_seek_cbk, (void *)(long)subvol, priv->children[subvol],
        priv->children[subvol]->fops->seek, local->fd, local->cont.seek.offset,
        local->cont.seek.what, local->xdata_req);
    return 0;
}

int
afr_seek(call_frame_t *frame, xlator_t *this, fd_t *fd, off_t offset,
         gf_seek_what_t what, dict_t *xdata)
{
    afr_local_t *local = NULL;
    int32_t op_errno = 0;

    AFR_ERROR_OUT_IF_FDCTX_INVALID(fd, this, op_errno, out);
    local = AFR_FRAME_INIT(frame, op_errno);
    if (!local)
        goto out;

    local->op = GF_FOP_SEEK;
    local->fd = fd_ref(fd);
    local->cont.seek.offset = offset;
    local->cont.seek.what = what;
    if (xdata)
        local->xdata_req = dict_ref(xdata);

    afr_fix_open(fd, this);

    afr_read_txn(frame, this, fd->inode, afr_seek_wind, AFR_DATA_TRANSACTION);

    return 0;
out:
    AFR_STACK_UNWIND(seek, frame, -1, op_errno, 0, NULL);

    return 0;
}
/* }}} */
