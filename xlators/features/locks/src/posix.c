/*
   Copyright (c) 2006-2012, 2016 Red Hat, Inc. <http://www.redhat.com>
   This file is part of GlusterFS.

   This file is licensed to you under your choice of the GNU Lesser
   General Public License, version 3 or any later version (LGPLv3 or
   later), or the GNU General Public License, version 2 (GPLv2), in all
   cases as published by the Free Software Foundation.
*/
#include <unistd.h>
#include <fcntl.h>
#include <limits.h>
#include <pthread.h>

#include <glusterfs/compat.h>
#include <glusterfs/logging.h>

#include "locks.h"
#include "common.h"
#include <glusterfs/statedump.h>
#include "clear.h"
#include <glusterfs/defaults.h>
#include <glusterfs/syncop.h>

#ifndef LLONG_MAX
#define LLONG_MAX LONG_LONG_MAX /* compat with old gcc */
#endif                          /* LLONG_MAX */

/* Forward declarations */

void
do_blocked_rw(pl_inode_t *);
static int
__rw_allowable(pl_inode_t *, posix_lock_t *, glusterfs_fop_t);
static int
format_brickname(char *);
int
pl_lockinfo_get_brickname(xlator_t *, inode_t *, int32_t *);
static int
fetch_pathinfo(xlator_t *, inode_t *, int32_t *, char **);

/*
 * The client is always requesting data, but older
 * servers were not returning it. Newer ones are, so
 * the client is receiving a mix of NULL and non-NULL
 * xdata in the answers when bricks are of different
 * versions. This triggers a bug in older clients.
 * To prevent that, we avoid returning extra xdata to
 * older clients (making the newer brick to behave as
 * an old brick).
 */
#define PL_STACK_UNWIND_FOR_CLIENT(fop, xdata, frame, op_ret, params...)       \
    do {                                                                       \
        pl_local_t *__local = NULL;                                            \
        if (frame->root->client &&                                             \
            (frame->root->client->opversion < GD_OP_VERSION_3_10_0)) {         \
            __local = frame->local;                                            \
            PL_STACK_UNWIND_AND_FREE(__local, fop, frame, op_ret, params);     \
        } else {                                                               \
            PL_STACK_UNWIND(fop, xdata, frame, op_ret, params);                \
        }                                                                      \
    } while (0)

#define PL_STACK_UNWIND(fop, xdata, frame, op_ret, params...)                  \
    do {                                                                       \
        pl_local_t *__local = NULL;                                            \
        inode_t *__parent = NULL;                                              \
        inode_t *__inode = NULL;                                               \
        char *__name = NULL;                                                   \
        dict_t *__unref = NULL;                                                \
        int __i = 0;                                                           \
        __local = frame->local;                                                \
        if (op_ret >= 0 && pl_needs_xdata_response(frame->local)) {            \
            if (xdata)                                                         \
                dict_ref(xdata);                                               \
            else                                                               \
                xdata = dict_new();                                            \
            if (xdata) {                                                       \
                __unref = xdata;                                               \
                while (__local->fd || __local->loc[__i].inode) {               \
                    pl_get_xdata_rsp_args(__local, #fop, &__parent, &__inode,  \
                                          &__name, __i);                       \
                    pl_set_xdata_response(frame->this, __local, __parent,      \
                                          __inode, __name, xdata, __i > 0);    \
                    if (__local->fd || __i == 1)                               \
                        break;                                                 \
                    __i++;                                                     \
                }                                                              \
            }                                                                  \
        }                                                                      \
        PL_STACK_UNWIND_AND_FREE(__local, fop, frame, op_ret, params);         \
        if (__unref)                                                           \
            dict_unref(__unref);                                               \
    } while (0)

#define PL_LOCAL_GET_REQUESTS(frame, this, xdata, __fd, __loc, __newloc)       \
    do {                                                                       \
        if (pl_has_xdata_requests(xdata)) {                                    \
            if (!frame->local)                                                 \
                frame->local = mem_get0(this->local_pool);                     \
            pl_local_t *__local = frame->local;                                \
            if (__local) {                                                     \
                if (__fd) {                                                    \
                    __local->fd = fd_ref(__fd);                                \
                    __local->inode = inode_ref(__fd->inode);                   \
                } else {                                                       \
                    if (__loc)                                                 \
                        loc_copy(&__local->loc[0], __loc);                     \
                    if (__newloc)                                              \
                        loc_copy(&__local->loc[1], __newloc);                  \
                    __local->inode = inode_ref(__local->loc[0].inode);         \
                }                                                              \
                pl_get_xdata_requests(__local, xdata);                         \
            }                                                                  \
        }                                                                      \
    } while (0)

#define PL_CHECK_LOCK_ENFORCE_KEY(frame, dict, name, this, loc, fd, priv)      \
    do {                                                                       \
        if ((dict && (dict_get(dict, GF_ENFORCE_MANDATORY_LOCK))) ||           \
            (name && (strcmp(name, GF_ENFORCE_MANDATORY_LOCK) == 0))) {        \
            inode_t *__inode = (loc ? loc->inode : fd->inode);                 \
            pl_inode_t *__pl_inode = pl_inode_get(this, __inode, NULL);        \
            if (__pl_inode == NULL) {                                          \
                op_ret = -1;                                                   \
                op_errno = ENOMEM;                                             \
                goto unwind;                                                   \
            }                                                                  \
            if (!pl_is_mandatory_locking_enabled(__pl_inode) ||                \
                !priv->mlock_enforced) {                                       \
                op_ret = -1;                                                   \
                gf_msg(this->name, GF_LOG_DEBUG, EINVAL, 0,                    \
                       "option %s would need mandatory lock to be enabled "    \
                       "and feature.enforce-mandatory-lock option to be set "  \
                       "to on",                                                \
                       GF_ENFORCE_MANDATORY_LOCK);                             \
                op_errno = EINVAL;                                             \
                goto unwind;                                                   \
            }                                                                  \
                                                                               \
            op_ret = pl_local_init(frame, this, loc, fd);                      \
            if (op_ret) {                                                      \
                op_errno = ENOMEM;                                             \
                goto unwind;                                                   \
            }                                                                  \
                                                                               \
            ((pl_local_t *)(frame->local))->update_mlock_enforced_flag = 1;    \
        }                                                                      \
    } while (0)

#define PL_INODE_REMOVE(_fop, _frame, _xl, _loc1, _loc2, _cont, _cbk,          \
                        _args...)                                              \
    ({                                                                         \
        struct list_head contend;                                              \
        pl_inode_t *__pl_inode;                                                \
        call_stub_t *__stub;                                                   \
        int32_t __error;                                                       \
        INIT_LIST_HEAD(&contend);                                              \
        __error = pl_inode_remove_prepare(_xl, _frame, _loc2 ? _loc2 : _loc1,  \
                                          &__pl_inode, &contend);              \
        if (__error < 0) {                                                     \
            __stub = fop_##_fop##_stub(_frame, _cont, ##_args);                \
            __error = pl_inode_remove_complete(_xl, __pl_inode, __stub,        \
                                               &contend);                      \
        } else if (__error == 0) {                                             \
            PL_LOCAL_GET_REQUESTS(_frame, _xl, xdata, ((fd_t *)NULL), _loc1,   \
                                  _loc2);                                      \
            STACK_WIND_COOKIE(_frame, _cbk, __pl_inode, FIRST_CHILD(_xl),      \
                              FIRST_CHILD(_xl)->fops->_fop, ##_args);          \
        }                                                                      \
        __error;                                                               \
    })

gf_boolean_t
pl_has_xdata_requests(dict_t *xdata)
{
    static char *reqs[] = {GLUSTERFS_ENTRYLK_COUNT,
                           GLUSTERFS_INODELK_COUNT,
                           GLUSTERFS_INODELK_DOM_COUNT,
                           GLUSTERFS_POSIXLK_COUNT,
                           GLUSTERFS_PARENT_ENTRYLK,
                           GLUSTERFS_MULTIPLE_DOM_LK_CNT_REQUESTS,
                           NULL};
    static int reqs_size[] = {SLEN(GLUSTERFS_ENTRYLK_COUNT),
                              SLEN(GLUSTERFS_INODELK_COUNT),
                              SLEN(GLUSTERFS_INODELK_DOM_COUNT),
                              SLEN(GLUSTERFS_POSIXLK_COUNT),
                              SLEN(GLUSTERFS_PARENT_ENTRYLK),
                              SLEN(GLUSTERFS_MULTIPLE_DOM_LK_CNT_REQUESTS),
                              0};
    int i = 0;

    if (!xdata)
        return _gf_false;

    for (i = 0; reqs[i]; i++)
        if (dict_getn(xdata, reqs[i], reqs_size[i]))
            return _gf_true;

    return _gf_false;
}

static int
dict_delete_domain_key(dict_t *dict, char *key, data_t *value, void *data)
{
    dict_del(dict, key);
    return 0;
}

void
pl_get_xdata_requests(pl_local_t *local, dict_t *xdata)
{
    if (!local || !xdata)
        return;

    GF_ASSERT(local->xdata == NULL);
    local->xdata = dict_copy_with_ref(xdata, NULL);

    if (dict_get_sizen(xdata, GLUSTERFS_ENTRYLK_COUNT)) {
        local->entrylk_count_req = 1;
        dict_del_sizen(xdata, GLUSTERFS_ENTRYLK_COUNT);
    }
    if (dict_get_sizen(xdata, GLUSTERFS_INODELK_COUNT)) {
        local->inodelk_count_req = 1;
        dict_del_sizen(xdata, GLUSTERFS_INODELK_COUNT);
    }
    if (dict_get_sizen(xdata, GLUSTERFS_MULTIPLE_DOM_LK_CNT_REQUESTS)) {
        local->multiple_dom_lk_requests = 1;
        dict_del_sizen(xdata, GLUSTERFS_MULTIPLE_DOM_LK_CNT_REQUESTS);
        dict_foreach_fnmatch(xdata, GLUSTERFS_INODELK_DOM_PREFIX "*",
                             dict_delete_domain_key, NULL);
    }

    local->inodelk_dom_count_req = dict_get_sizen(xdata,
                                                  GLUSTERFS_INODELK_DOM_COUNT);
    if (local->inodelk_dom_count_req) {
        data_ref(local->inodelk_dom_count_req);
        dict_del_sizen(xdata, GLUSTERFS_INODELK_DOM_COUNT);
    }

    if (dict_get_sizen(xdata, GLUSTERFS_POSIXLK_COUNT)) {
        local->posixlk_count_req = 1;
        dict_del_sizen(xdata, GLUSTERFS_POSIXLK_COUNT);
    }

    if (dict_get_sizen(xdata, GLUSTERFS_PARENT_ENTRYLK)) {
        local->parent_entrylk_req = 1;
        dict_del_sizen(xdata, GLUSTERFS_PARENT_ENTRYLK);
    }
}

gf_boolean_t
pl_needs_xdata_response(pl_local_t *local)
{
    if (!local)
        return _gf_false;

    if (local->parent_entrylk_req || local->entrylk_count_req ||
        local->inodelk_dom_count_req || local->inodelk_count_req ||
        local->posixlk_count_req || local->multiple_dom_lk_requests)
        return _gf_true;

    return _gf_false;
}

void
pl_get_xdata_rsp_args(pl_local_t *local, char *fop, inode_t **parent,
                      inode_t **inode, char **name, int i)
{
    if (strcmp(fop, "lookup") == 0) {
        *parent = local->loc[0].parent;
        *inode = local->loc[0].inode;
        *name = (char *)local->loc[0].name;
    } else {
        if (local->fd) {
            *inode = local->fd->inode;
        } else {
            *inode = local->loc[i].parent;
        }
    }
}

static inline int
pl_track_io_fop_count(pl_local_t *local, xlator_t *this, pl_count_op_t op)
{
    pl_inode_t *pl_inode = NULL;

    if (!local)
        return -1;

    pl_inode = pl_inode_get(this, local->inode, NULL);
    if (!pl_inode)
        return -1;

    if (pl_inode->mlock_enforced && pl_inode->track_fop_wind_count) {
        pthread_mutex_lock(&pl_inode->mutex);
        {
            if (op == DECREMENT) {
                pl_inode->fop_wind_count--;
                /* fop_wind_count can go negative when lock enforcement is
                 * enabled on unwind path of an IO. Hence the "<" comparision.
                 */
                if (pl_inode->fop_wind_count <= 0) {
                    pthread_cond_broadcast(&pl_inode->check_fop_wind_count);
                    pl_inode->track_fop_wind_count = _gf_false;
                    pl_inode->fop_wind_count = 0;
                }
            } else {
                pl_inode->fop_wind_count++;
            }
        }
        pthread_mutex_unlock(&pl_inode->mutex);
    }

    return 0;
}

static int32_t
__get_posixlk_count(pl_inode_t *pl_inode)
{
    posix_lock_t *lock = NULL;
    int32_t count = 0;

    list_for_each_entry(lock, &pl_inode->ext_list, list) { count++; }

    return count;
}

int32_t
get_posixlk_count(xlator_t *this, inode_t *inode)
{
    pl_inode_t *pl_inode = NULL;
    uint64_t tmp_pl_inode = 0;
    int32_t count = 0;

    int ret = inode_ctx_get(inode, this, &tmp_pl_inode);
    if (ret != 0) {
        goto out;
    }

    pl_inode = (pl_inode_t *)(long)tmp_pl_inode;

    pthread_mutex_lock(&pl_inode->mutex);
    {
        count = __get_posixlk_count(pl_inode);
    }
    pthread_mutex_unlock(&pl_inode->mutex);

out:
    return count;
}

void
pl_parent_entrylk_xattr_fill(xlator_t *this, inode_t *parent, char *basename,
                             dict_t *dict, gf_boolean_t keep_max)
{
    int32_t entrylk = 0;
    int32_t maxcount = -1;
    int ret = -1;

    if (!parent || !basename)
        goto out;
    if (keep_max) {
        ret = dict_get_int32_sizen(dict, GLUSTERFS_PARENT_ENTRYLK, &maxcount);
        if (ret < 0)
            gf_msg_debug(this->name, 0, " Failed to fetch the value for key %s",
                         GLUSTERFS_PARENT_ENTRYLK);
    }
    entrylk = check_entrylk_on_basename(this, parent, basename);
    if (maxcount >= entrylk)
        return;
out:
    ret = dict_set_int32_sizen(dict, GLUSTERFS_PARENT_ENTRYLK, entrylk);
    if (ret < 0) {
        gf_msg_debug(this->name, 0, " dict_set failed on key %s",
                     GLUSTERFS_PARENT_ENTRYLK);
    }
}

void
pl_entrylk_xattr_fill(xlator_t *this, inode_t *inode, dict_t *dict,
                      gf_boolean_t keep_max)
{
    int32_t count = 0;
    int32_t maxcount = -1;
    int ret = -1;

    if (keep_max) {
        ret = dict_get_int32_sizen(dict, GLUSTERFS_ENTRYLK_COUNT, &maxcount);
        if (ret < 0)
            gf_msg_debug(this->name, 0, " Failed to fetch the value for key %s",
                         GLUSTERFS_ENTRYLK_COUNT);
    }
    count = get_entrylk_count(this, inode);
    if (maxcount >= count)
        return;

    ret = dict_set_int32_sizen(dict, GLUSTERFS_ENTRYLK_COUNT, count);
    if (ret < 0) {
        gf_msg_debug(this->name, 0, " dict_set failed on key %s",
                     GLUSTERFS_ENTRYLK_COUNT);
    }
}

void
pl_inodelk_xattr_fill(xlator_t *this, inode_t *inode, dict_t *dict,
                      char *domname, gf_boolean_t keep_max)
{
    int32_t count = 0;
    int32_t maxcount = -1;
    int ret = -1;

    if (keep_max) {
        ret = dict_get_int32_sizen(dict, GLUSTERFS_INODELK_COUNT, &maxcount);
        if (ret < 0)
            gf_msg_debug(this->name, 0, " Failed to fetch the value for key %s",
                         GLUSTERFS_INODELK_COUNT);
    }
    count = get_inodelk_count(this, inode, domname);
    if (maxcount >= count)
        return;

    ret = dict_set_int32_sizen(dict, GLUSTERFS_INODELK_COUNT, count);
    if (ret < 0) {
        gf_msg_debug(this->name, 0,
                     "Failed to set count for "
                     "key %s",
                     GLUSTERFS_INODELK_COUNT);
    }

    return;
}

void
pl_posixlk_xattr_fill(xlator_t *this, inode_t *inode, dict_t *dict,
                      gf_boolean_t keep_max)
{
    int32_t count = 0;
    int32_t maxcount = -1;
    int ret = -1;

    if (keep_max) {
        ret = dict_get_int32_sizen(dict, GLUSTERFS_POSIXLK_COUNT, &maxcount);
        if (ret < 0)
            gf_msg_debug(this->name, 0, " Failed to fetch the value for key %s",
                         GLUSTERFS_POSIXLK_COUNT);
    }
    count = get_posixlk_count(this, inode);
    if (maxcount >= count)
        return;

    ret = dict_set_int32_sizen(dict, GLUSTERFS_POSIXLK_COUNT, count);
    if (ret < 0) {
        gf_msg_debug(this->name, 0, " dict_set failed on key %s",
                     GLUSTERFS_POSIXLK_COUNT);
    }
}

void
pl_inodelk_xattr_fill_each(xlator_t *this, inode_t *inode, dict_t *dict,
                           char *domname, gf_boolean_t keep_max, char *key)
{
    int32_t count = 0;
    int32_t maxcount = -1;
    int ret = -1;

    if (keep_max) {
        ret = dict_get_int32(dict, key, &maxcount);
        if (ret < 0)
            gf_msg_debug(this->name, 0, " Failed to fetch the value for key %s",
                         GLUSTERFS_INODELK_COUNT);
    }
    count = get_inodelk_count(this, inode, domname);
    if (maxcount >= count)
        return;

    ret = dict_set_int32(dict, key, count);
    if (ret < 0) {
        gf_msg_debug(this->name, 0,
                     "Failed to set count for "
                     "key %s",
                     key);
    }

    return;
}

static int
pl_inodelk_xattr_fill_multiple(dict_t *this, char *key, data_t *value,
                               void *data)
{
    multi_dom_lk_data *d = data;
    char *tmp_key = NULL;
    char *save_ptr = NULL;

    tmp_key = gf_strdup(key);
    if (!tmp_key)
        return -1;

    strtok_r(tmp_key, ":", &save_ptr);
    if (!*save_ptr) {
        if (tmp_key)
            GF_FREE(tmp_key);
        gf_msg(THIS->name, GF_LOG_ERROR, 0, EINVAL,
               "Could not tokenize domain string from key %s", key);
        return -1;
    }

    pl_inodelk_xattr_fill_each(d->this, d->inode, d->xdata_rsp, save_ptr,
                               d->keep_max, key);
    if (tmp_key)
        GF_FREE(tmp_key);

    return 0;
}

void
pl_fill_multiple_dom_lk_requests(xlator_t *this, pl_local_t *local,
                                 inode_t *inode, dict_t *dict,
                                 gf_boolean_t keep_max)
{
    multi_dom_lk_data data;

    data.this = this;
    data.inode = inode;
    data.xdata_rsp = dict;
    data.keep_max = keep_max;

    dict_foreach_fnmatch(local->xdata, GLUSTERFS_INODELK_DOM_PREFIX "*",
                         pl_inodelk_xattr_fill_multiple, &data);
}

void
pl_set_xdata_response(xlator_t *this, pl_local_t *local, inode_t *parent,
                      inode_t *inode, char *name, dict_t *xdata,
                      gf_boolean_t max_lock)
{
    if (!xdata || !local)
        return;

    if (local->parent_entrylk_req && parent && name && name[0] != '\0')
        pl_parent_entrylk_xattr_fill(this, parent, name, xdata, max_lock);

    if (!inode)
        return;

    if (local->entrylk_count_req)
        pl_entrylk_xattr_fill(this, inode, xdata, max_lock);

    if (local->inodelk_dom_count_req)
        pl_inodelk_xattr_fill(this, inode, xdata,
                              data_to_str(local->inodelk_dom_count_req),
                              max_lock);

    if (local->inodelk_count_req)
        pl_inodelk_xattr_fill(this, inode, xdata, NULL, max_lock);

    if (local->posixlk_count_req)
        pl_posixlk_xattr_fill(this, inode, xdata, max_lock);

    if (local->multiple_dom_lk_requests)
        pl_fill_multiple_dom_lk_requests(this, local, inode, xdata, max_lock);
}

/* Checks whether the region where fop is acting upon conflicts
 * with existing locks. If there is no conflict function returns
 * 1 else returns 0 with can_block boolean set accordingly to
 * indicate block/fail the fop.
 */
int
pl_is_fop_allowed(pl_inode_t *pl_inode, posix_lock_t *region, fd_t *fd,
                  glusterfs_fop_t op, gf_boolean_t *can_block)
{
    int ret = 0;

    if (!__rw_allowable(pl_inode, region, op)) {
        if (pl_inode->mlock_enforced) {
            *can_block = _gf_false;
        } else if ((!fd) || (fd && (fd->flags & O_NONBLOCK))) {
            gf_log("locks", GF_LOG_TRACE,
                   "returning EAGAIN"
                   " because fd is O_NONBLOCK");
            *can_block = _gf_false;
        } else {
            *can_block = _gf_true;
        }
    } else {
        ret = 1;
    }

    return ret;
}

static pl_fdctx_t *
pl_new_fdctx()
{
    pl_fdctx_t *fdctx = GF_MALLOC(sizeof(*fdctx), gf_locks_mt_pl_fdctx_t);
    GF_VALIDATE_OR_GOTO("posix-locks", fdctx, out);

    INIT_LIST_HEAD(&fdctx->locks_list);

out:
    return fdctx;
}

static pl_fdctx_t *
pl_check_n_create_fdctx(xlator_t *this, fd_t *fd)
{
    int ret = 0;
    uint64_t tmp = 0;
    pl_fdctx_t *fdctx = NULL;

    GF_VALIDATE_OR_GOTO("posix-locks", this, out);
    GF_VALIDATE_OR_GOTO(this->name, fd, out);

    LOCK(&fd->lock);
    {
        ret = __fd_ctx_get(fd, this, &tmp);
        if ((ret != 0) || (tmp == 0)) {
            fdctx = pl_new_fdctx();
            if (fdctx == NULL) {
                goto unlock;
            }
        }

        ret = __fd_ctx_set(fd, this, (uint64_t)(long)fdctx);
        if (ret != 0) {
            GF_FREE(fdctx);
            fdctx = NULL;
            UNLOCK(&fd->lock);
            gf_log(this->name, GF_LOG_DEBUG, "failed to set fd ctx");
            goto out;
        }
    }
unlock:
    UNLOCK(&fd->lock);

out:
    return fdctx;
}

int32_t
pl_discard_cbk(call_frame_t *frame, void *cookie, xlator_t *this,
               int32_t op_ret, int32_t op_errno, struct iatt *prebuf,
               struct iatt *postbuf, dict_t *xdata)
{
    pl_track_io_fop_count(frame->local, this, DECREMENT);

    PL_STACK_UNWIND(discard, xdata, frame, op_ret, op_errno, prebuf, postbuf,
                    xdata);
    return 0;
}

int
pl_discard_cont(call_frame_t *frame, xlator_t *this, fd_t *fd, off_t offset,
                size_t len, dict_t *xdata)
{
    pl_track_io_fop_count(frame->local, this, INCREMENT);

    STACK_WIND(frame, pl_discard_cbk, FIRST_CHILD(this),
               FIRST_CHILD(this)->fops->discard, fd, offset, len, xdata);
    return 0;
}

int32_t
pl_discard(call_frame_t *frame, xlator_t *this, fd_t *fd, off_t offset,
           size_t len, dict_t *xdata)
{
    pl_local_t *local = NULL;
    pl_inode_t *pl_inode = NULL;
    pl_rw_req_t *rw = NULL;
    posix_lock_t region = {
        .list =
            {
                0,
            },
    };
    gf_boolean_t enabled = _gf_false;
    gf_boolean_t can_block = _gf_true;
    int op_ret = 0;
    int op_errno = 0;
    int allowed = 1;

    GF_VALIDATE_OR_GOTO("locks", this, unwind);

    local = mem_get0(this->local_pool);
    if (!local) {
        op_ret = -1;
        op_errno = ENOMEM;
        goto unwind;
    }

    frame->local = local;
    local->inode = inode_ref(fd->inode);
    local->fd = fd_ref(fd);

    pl_inode = pl_inode_get(this, fd->inode, local);
    if (!pl_inode) {
        op_ret = -1;
        op_errno = ENOMEM;
        goto unwind;
    }

    if (frame->root->pid < 0)
        enabled = _gf_false;
    else
        enabled = pl_is_mandatory_locking_enabled(pl_inode);

    if (enabled) {
        region.fl_start = offset;
        region.fl_end = offset + len - 1;
        region.client = frame->root->client;
        region.fd_num = fd_to_fdnum(fd);
        region.client_pid = frame->root->pid;
        region.owner = frame->root->lk_owner;

        pthread_mutex_lock(&pl_inode->mutex);
        {
            allowed = pl_is_fop_allowed(pl_inode, &region, fd, GF_FOP_DISCARD,
                                        &can_block);
            if (allowed == 1) {
                if (pl_inode->mlock_enforced &&
                    pl_inode->track_fop_wind_count) {
                    pl_inode->fop_wind_count++;
                }
                goto unlock;
            } else if (!can_block) {
                op_errno = EAGAIN;
                op_ret = -1;
                goto unlock;
            }

            rw = GF_MALLOC(sizeof(*rw), gf_locks_mt_pl_rw_req_t);
            if (!rw) {
                op_errno = ENOMEM;
                op_ret = -1;
                goto unlock;
            }

            rw->stub = fop_discard_stub(frame, pl_discard_cont, fd, offset, len,
                                        xdata);
            if (!rw->stub) {
                op_errno = ENOMEM;
                op_ret = -1;
                GF_FREE(rw);
                goto unlock;
            }

            rw->region = region;

            list_add_tail(&rw->list, &pl_inode->rw_list);
        }
    unlock:
        pthread_mutex_unlock(&pl_inode->mutex);
    }

    if (allowed == 1)
        STACK_WIND(frame, pl_discard_cbk, FIRST_CHILD(this),
                   FIRST_CHILD(this)->fops->discard, fd, offset, len, xdata);
unwind:
    if (op_ret == -1)
        PL_STACK_UNWIND(discard, xdata, frame, op_ret, op_errno, NULL, NULL,
                        NULL);

    return 0;
}

int32_t
pl_zerofill_cbk(call_frame_t *frame, void *cookie, xlator_t *this,
                int32_t op_ret, int32_t op_errno, struct iatt *prebuf,
                struct iatt *postbuf, dict_t *xdata)
{
    pl_track_io_fop_count(frame->local, this, DECREMENT);

    PL_STACK_UNWIND(zerofill, xdata, frame, op_ret, op_errno, prebuf, postbuf,
                    xdata);
    return 0;
}

int
pl_zerofill_cont(call_frame_t *frame, xlator_t *this, fd_t *fd, off_t offset,
                 off_t len, dict_t *xdata)
{
    pl_track_io_fop_count(frame->local, this, INCREMENT);

    STACK_WIND(frame, pl_zerofill_cbk, FIRST_CHILD(this),
               FIRST_CHILD(this)->fops->zerofill, fd, offset, len, xdata);
    return 0;
}

int32_t
pl_zerofill(call_frame_t *frame, xlator_t *this, fd_t *fd, off_t offset,
            off_t len, dict_t *xdata)
{
    pl_local_t *local = NULL;
    pl_inode_t *pl_inode = NULL;
    pl_rw_req_t *rw = NULL;
    posix_lock_t region = {
        .list =
            {
                0,
            },
    };
    gf_boolean_t enabled = _gf_false;
    gf_boolean_t can_block = _gf_true;
    int op_ret = 0;
    int op_errno = 0;
    int allowed = 1;

    GF_VALIDATE_OR_GOTO("locks", this, unwind);

    local = mem_get0(this->local_pool);
    if (!local) {
        op_ret = -1;
        op_errno = ENOMEM;
        goto unwind;
    }

    frame->local = local;
    local->inode = inode_ref(fd->inode);
    local->fd = fd_ref(fd);

    pl_inode = pl_inode_get(this, fd->inode, local);
    if (!pl_inode) {
        op_ret = -1;
        op_errno = ENOMEM;
        goto unwind;
    }

    if (frame->root->pid < 0)
        enabled = _gf_false;
    else
        enabled = pl_is_mandatory_locking_enabled(pl_inode);

    if (enabled) {
        region.fl_start = offset;
        region.fl_end = offset + len - 1;
        region.client = frame->root->client;
        region.fd_num = fd_to_fdnum(fd);
        region.client_pid = frame->root->pid;
        region.owner = frame->root->lk_owner;

        pthread_mutex_lock(&pl_inode->mutex);
        {
            allowed = pl_is_fop_allowed(pl_inode, &region, fd, GF_FOP_ZEROFILL,
                                        &can_block);
            if (allowed == 1) {
                if (pl_inode->mlock_enforced &&
                    pl_inode->track_fop_wind_count) {
                    pl_inode->fop_wind_count++;
                }
                goto unlock;
            } else if (!can_block) {
                op_errno = EAGAIN;
                op_ret = -1;
                goto unlock;
            }

            rw = GF_MALLOC(sizeof(*rw), gf_locks_mt_pl_rw_req_t);
            if (!rw) {
                op_errno = ENOMEM;
                op_ret = -1;
                goto unlock;
            }

            rw->stub = fop_zerofill_stub(frame, pl_zerofill_cont, fd, offset,
                                         len, xdata);
            if (!rw->stub) {
                op_errno = ENOMEM;
                op_ret = -1;
                GF_FREE(rw);
                goto unlock;
            }

            rw->region = region;

            list_add_tail(&rw->list, &pl_inode->rw_list);
        }
    unlock:
        pthread_mutex_unlock(&pl_inode->mutex);
    }

    if (allowed == 1)
        STACK_WIND(frame, pl_zerofill_cbk, FIRST_CHILD(this),
                   FIRST_CHILD(this)->fops->zerofill, fd, offset, len, xdata);
unwind:
    if (op_ret == -1)
        PL_STACK_UNWIND(zerofill, xdata, frame, op_ret, op_errno, NULL, NULL,
                        NULL);

    return 0;
}

int
pl_truncate_cbk(call_frame_t *frame, void *cookie, xlator_t *this,
                int32_t op_ret, int32_t op_errno, struct iatt *prebuf,
                struct iatt *postbuf, dict_t *xdata)
{
    pl_local_t *local = frame->local;

    pl_track_io_fop_count(local, this, DECREMENT);

    if (local->op == GF_FOP_TRUNCATE)
        PL_STACK_UNWIND(truncate, xdata, frame, op_ret, op_errno, prebuf,
                        postbuf, xdata);
    else
        PL_STACK_UNWIND(ftruncate, xdata, frame, op_ret, op_errno, prebuf,
                        postbuf, xdata);
    return 0;
}

int
pl_ftruncate_cont(call_frame_t *frame, xlator_t *this, fd_t *fd, off_t offset,
                  dict_t *xdata)
{
    pl_track_io_fop_count(frame->local, this, INCREMENT);

    STACK_WIND(frame, pl_truncate_cbk, FIRST_CHILD(this),
               FIRST_CHILD(this)->fops->ftruncate, fd, offset, xdata);
    return 0;
}

int
pl_truncate_cont(call_frame_t *frame, xlator_t *this, loc_t *loc, off_t offset,
                 dict_t *xdata)
{
    pl_track_io_fop_count(frame->local, this, INCREMENT);

    STACK_WIND(frame, pl_truncate_cbk, FIRST_CHILD(this),
               FIRST_CHILD(this)->fops->truncate, loc, offset, xdata);
    return 0;
}

static int
truncate_stat_cbk(call_frame_t *frame, void *cookie, xlator_t *this,
                  int32_t op_ret, int32_t op_errno, struct iatt *buf,
                  dict_t *xdata)
{
    pl_local_t *local = frame->local;
    inode_t *inode = NULL;
    pl_inode_t *pl_inode = NULL;
    pl_rw_req_t *rw = NULL;
    posix_lock_t region = {
        .list =
            {
                0,
            },
    };
    gf_boolean_t enabled = _gf_false;
    gf_boolean_t can_block = _gf_true;
    int allowed = 1;

    GF_VALIDATE_OR_GOTO("locks", this, unwind);

    if (op_ret != 0) {
        gf_log(this->name, GF_LOG_ERROR,
               "got error (errno=%d, stderror=%s) from child", op_errno,
               strerror(op_errno));
        goto unwind;
    }

    if (local->op == GF_FOP_TRUNCATE)
        inode = local->loc[0].inode;
    else
        inode = local->fd->inode;

    local->inode = inode_ref(inode);

    pl_inode = pl_inode_get(this, inode, local);
    if (!pl_inode) {
        op_ret = -1;
        op_errno = ENOMEM;
        goto unwind;
    }

    if (frame->root->pid < 0)
        enabled = _gf_false;
    else
        enabled = pl_is_mandatory_locking_enabled(pl_inode);

    if (enabled) {
        region.fl_start = local->offset;
        region.fl_end = LLONG_MAX;
        region.client = frame->root->client;
        region.fd_num = fd_to_fdnum(local->fd);
        region.client_pid = frame->root->pid;
        region.owner = frame->root->lk_owner;
        pthread_mutex_lock(&pl_inode->mutex);
        {
            allowed = pl_is_fop_allowed(pl_inode, &region, local->fd, local->op,
                                        &can_block);

            if (allowed == 1) {
                if (pl_inode->mlock_enforced &&
                    pl_inode->track_fop_wind_count) {
                    pl_inode->fop_wind_count++;
                }
                goto unlock;
            } else if (!can_block) {
                op_errno = EAGAIN;
                op_ret = -1;
                goto unlock;
            }

            rw = GF_MALLOC(sizeof(*rw), gf_locks_mt_pl_rw_req_t);
            if (!rw) {
                op_errno = ENOMEM;
                op_ret = -1;
                goto unlock;
            }

            if (local->op == GF_FOP_TRUNCATE)
                rw->stub = fop_truncate_stub(frame, pl_truncate_cont,
                                             &local->loc[0], local->offset,
                                             local->xdata);
            else
                rw->stub = fop_ftruncate_stub(frame, pl_ftruncate_cont,
                                              local->fd, local->offset,
                                              local->xdata);
            if (!rw->stub) {
                op_errno = ENOMEM;
                op_ret = -1;
                GF_FREE(rw);
                goto unlock;
            }

            rw->region = region;

            list_add_tail(&rw->list, &pl_inode->rw_list);
        }
    unlock:
        pthread_mutex_unlock(&pl_inode->mutex);
    }

    if (allowed == 1) {
        switch (local->op) {
            case GF_FOP_TRUNCATE:
                STACK_WIND(frame, pl_truncate_cbk, FIRST_CHILD(this),
                           FIRST_CHILD(this)->fops->truncate, &local->loc[0],
                           local->offset, local->xdata);
                break;
            case GF_FOP_FTRUNCATE:
                STACK_WIND(frame, pl_truncate_cbk, FIRST_CHILD(this),
                           FIRST_CHILD(this)->fops->ftruncate, local->fd,
                           local->offset, local->xdata);
                break;
            default:
                break;
        }
    }
unwind:
    if (op_ret == -1) {
        gf_log(this ? this->name : "locks", GF_LOG_ERROR,
               "truncate failed with "
               "ret: %d, error: %s",
               op_ret, strerror(op_errno));

        switch (local->op) {
            case GF_FOP_TRUNCATE:
                PL_STACK_UNWIND(truncate, xdata, frame, op_ret, op_errno, buf,
                                NULL, xdata);
                break;
            case GF_FOP_FTRUNCATE:
                PL_STACK_UNWIND(ftruncate, xdata, frame, op_ret, op_errno, buf,
                                NULL, xdata);
                break;
            default:
                break;
        }
    }
    return 0;
}

int
pl_truncate(call_frame_t *frame, xlator_t *this, loc_t *loc, off_t offset,
            dict_t *xdata)
{
    pl_local_t *local = NULL;
    int ret = -1;

    GF_VALIDATE_OR_GOTO("locks", this, unwind);

    local = mem_get0(this->local_pool);
    GF_VALIDATE_OR_GOTO(this->name, local, unwind);

    local->op = GF_FOP_TRUNCATE;
    local->offset = offset;
    loc_copy(&local->loc[0], loc);
    if (xdata)
        local->xdata = dict_ref(xdata);

    frame->local = local;

    STACK_WIND(frame, truncate_stat_cbk, FIRST_CHILD(this),
               FIRST_CHILD(this)->fops->stat, loc, NULL);
    ret = 0;

unwind:
    if (ret == -1) {
        gf_log(this ? this->name : "locks", GF_LOG_ERROR,
               "truncate on %s failed with"
               " ret: %d, error: %s",
               loc->path, -1, strerror(ENOMEM));
        STACK_UNWIND_STRICT(truncate, frame, -1, ENOMEM, NULL, NULL, NULL);
    }
    return 0;
}

int
pl_ftruncate(call_frame_t *frame, xlator_t *this, fd_t *fd, off_t offset,
             dict_t *xdata)
{
    pl_local_t *local = NULL;
    int ret = -1;

    GF_VALIDATE_OR_GOTO("locks", this, unwind);
    local = mem_get0(this->local_pool);
    GF_VALIDATE_OR_GOTO(this->name, local, unwind);

    local->op = GF_FOP_FTRUNCATE;
    local->offset = offset;
    local->fd = fd_ref(fd);
    if (xdata)
        local->xdata = dict_ref(xdata);

    frame->local = local;

    STACK_WIND(frame, truncate_stat_cbk, FIRST_CHILD(this),
               FIRST_CHILD(this)->fops->fstat, fd, xdata);
    ret = 0;
unwind:
    if (ret == -1) {
        gf_log(this ? this->name : "locks", GF_LOG_ERROR,
               "ftruncate failed with"
               " ret: %d, error: %s",
               -1, strerror(ENOMEM));
        STACK_UNWIND_STRICT(ftruncate, frame, -1, ENOMEM, NULL, NULL, NULL);
    }
    return 0;
}

int
pl_locks_by_fd(pl_inode_t *pl_inode, fd_t *fd)
{
    posix_lock_t *l = NULL;
    int found = 0;

    pthread_mutex_lock(&pl_inode->mutex);
    {
        list_for_each_entry(l, &pl_inode->ext_list, list)
        {
            if (l->fd_num == fd_to_fdnum(fd)) {
                found = 1;
                break;
            }
        }
    }
    pthread_mutex_unlock(&pl_inode->mutex);
    return found;
}

static void
delete_locks_of_fd(xlator_t *this, pl_inode_t *pl_inode, fd_t *fd)
{
    posix_lock_t *tmp = NULL;
    posix_lock_t *l = NULL;

    struct list_head blocked_list;

    INIT_LIST_HEAD(&blocked_list);

    pthread_mutex_lock(&pl_inode->mutex);
    {
        list_for_each_entry_safe(l, tmp, &pl_inode->ext_list, list)
        {
            if (l->fd_num == fd_to_fdnum(fd)) {
                if (l->blocked) {
                    list_move_tail(&l->list, &blocked_list);
                    continue;
                }
                __delete_lock(l);
                __destroy_lock(l);
            }
        }
    }
    pthread_mutex_unlock(&pl_inode->mutex);

    list_for_each_entry_safe(l, tmp, &blocked_list, list)
    {
        list_del_init(&l->list);
        STACK_UNWIND_STRICT(lk, l->frame, -1, EAGAIN, &l->user_flock, NULL);
        __destroy_lock(l);
    }

    grant_blocked_locks(this, pl_inode);

    do_blocked_rw(pl_inode);
}

static void
__delete_locks_of_owner(pl_inode_t *pl_inode, client_t *client,
                        gf_lkowner_t *owner)
{
    posix_lock_t *tmp = NULL;
    posix_lock_t *l = NULL;

    /* TODO: what if it is a blocked lock with pending l->frame */

    list_for_each_entry_safe(l, tmp, &pl_inode->ext_list, list)
    {
        if (l->blocked)
            continue;
        if ((l->client == client) && is_same_lkowner(&l->owner, owner)) {
            gf_log("posix-locks", GF_LOG_TRACE,
                   " Flushing lock"
                   "%s (pid=%d) (lk-owner=%s) %" PRId64 " - %" PRId64
                   " state: %s",
                   l->fl_type == F_UNLCK ? "Unlock" : "Lock", l->client_pid,
                   lkowner_utoa(&l->owner), l->user_flock.l_start,
                   l->user_flock.l_len, l->blocked == 1 ? "Blocked" : "Active");

            __delete_lock(l);
            __destroy_lock(l);
        }
    }

    return;
}

int32_t
pl_getxattr_cbk(call_frame_t *frame, void *cookie, xlator_t *this,
                int32_t op_ret, int32_t op_errno, dict_t *dict, dict_t *xdata)
{
    STACK_UNWIND_STRICT(getxattr, frame, op_ret, op_errno, dict, xdata);
    return 0;
}

static int32_t
pl_getxattr_clrlk(xlator_t *this, const char *name, inode_t *inode,
                  dict_t **dict, int32_t *op_errno)
{
    int32_t bcount = 0;
    int32_t gcount = 0;
    char *key = NULL;
    char *lk_summary = NULL;
    pl_inode_t *pl_inode = NULL;
    clrlk_args args = {
        0,
    };
    char *brickname = NULL;
    int32_t op_ret = -1;

    *op_errno = EINVAL;

    if (clrlk_parse_args(name, &args)) {
        *op_errno = EINVAL;
        goto out;
    }

    *dict = dict_new();
    if (!*dict) {
        *op_errno = ENOMEM;
        goto out;
    }

    pl_inode = pl_inode_get(this, inode, NULL);
    if (!pl_inode) {
        *op_errno = ENOMEM;
        goto out;
    }

    switch (args.type) {
        case CLRLK_INODE:
        case CLRLK_ENTRY:
            op_ret = clrlk_clear_lks_in_all_domains(this, pl_inode, &args,
                                                    &bcount, &gcount, op_errno);
            break;
        case CLRLK_POSIX:
            op_ret = clrlk_clear_posixlk(this, pl_inode, &args, &bcount,
                                         &gcount, op_errno);
            break;
        default:
            op_ret = -1;
            *op_errno = EINVAL;
    }
    if (op_ret) {
        if (args.type >= CLRLK_TYPE_MAX) {
            gf_log(this->name, GF_LOG_ERROR,
                   "clear locks: invalid lock type %d", args.type);
        } else {
            gf_log(this->name, GF_LOG_ERROR,
                   "clear locks of type %s failed: %s",
                   clrlk_type_names[args.type], strerror(*op_errno));
        }

        goto out;
    }

    op_ret = fetch_pathinfo(this, inode, op_errno, &brickname);
    if (op_ret) {
        gf_log(this->name, GF_LOG_WARNING, "Couldn't get brickname");
    } else {
        op_ret = format_brickname(brickname);
        if (op_ret) {
            gf_log(this->name, GF_LOG_WARNING, "Couldn't format brickname");
            GF_FREE(brickname);
            brickname = NULL;
        }
    }

    if (!gcount && !bcount) {
        if (gf_asprintf(&lk_summary, "No locks cleared.") == -1) {
            op_ret = -1;
            *op_errno = ENOMEM;
            goto out;
        }
    } else if (gf_asprintf(&lk_summary,
                           "%s: %s blocked locks=%d "
                           "granted locks=%d",
                           (brickname == NULL) ? this->name : brickname,
                           clrlk_type_names[args.type], bcount, gcount) == -1) {
        op_ret = -1;
        *op_errno = ENOMEM;
        goto out;
    }
    gf_log(this->name, GF_LOG_DEBUG, "%s", lk_summary);

    key = gf_strdup(name);
    if (!key) {
        op_ret = -1;
        goto out;
    }
    if (dict_set_dynstr(*dict, key, lk_summary)) {
        op_ret = -1;
        *op_errno = ENOMEM;
        goto out;
    }

    op_ret = 0;

out:
    GF_FREE(brickname);
    GF_FREE(args.opts);
    GF_FREE(key);
    if (op_ret) {
        GF_FREE(lk_summary);
    }

    return op_ret;
}

int32_t
pl_getxattr(call_frame_t *frame, xlator_t *this, loc_t *loc, const char *name,
            dict_t *xdata)
{
    int32_t op_errno = EINVAL;
    int32_t op_ret = -1;
    dict_t *dict = NULL;

    if (!name)
        goto usual;

    if (strncmp(name, GF_XATTR_CLRLK_CMD, SLEN(GF_XATTR_CLRLK_CMD)))
        goto usual;

    op_ret = pl_getxattr_clrlk(this, name, loc->inode, &dict, &op_errno);

    STACK_UNWIND_STRICT(getxattr, frame, op_ret, op_errno, dict, xdata);

    if (dict)
        dict_unref(dict);
    return 0;

usual:
    STACK_WIND(frame, pl_getxattr_cbk, FIRST_CHILD(this),
               FIRST_CHILD(this)->fops->getxattr, loc, name, xdata);
    return 0;
}

static int
format_brickname(char *brickname)
{
    int ret = -1;
    char *hostname = NULL;
    char *volume = NULL;
    char *saveptr = NULL;

    if (!brickname)
        goto out;

    strtok_r(brickname, ":", &saveptr);
    hostname = gf_strdup(strtok_r(NULL, ":", &saveptr));
    if (hostname == NULL)
        goto out;
    volume = gf_strdup(strtok_r(NULL, ".", &saveptr));
    if (volume == NULL)
        goto out;

    sprintf(brickname, "%s:%s", hostname, volume);

    ret = 0;
out:
    GF_FREE(hostname);
    GF_FREE(volume);
    return ret;
}

static int
fetch_pathinfo(xlator_t *this, inode_t *inode, int32_t *op_errno,
               char **brickname)
{
    int ret = -1;
    loc_t loc = {
        0,
    };
    dict_t *dict = NULL;

    if (!brickname)
        goto out;

    if (!op_errno)
        goto out;

    gf_uuid_copy(loc.gfid, inode->gfid);
    loc.inode = inode_ref(inode);

    ret = syncop_getxattr(FIRST_CHILD(this), &loc, &dict, GF_XATTR_PATHINFO_KEY,
                          NULL, NULL);
    if (ret < 0) {
        *op_errno = -ret;
        ret = -1;
        goto out;
    }

    ret = dict_get_str_sizen(dict, GF_XATTR_PATHINFO_KEY, brickname);
    if (ret)
        goto out;

    *brickname = gf_strdup(*brickname);
    if (*brickname == NULL) {
        ret = -1;
        goto out;
    }

    ret = 0;
out:
    if (dict != NULL) {
        dict_unref(dict);
    }
    loc_wipe(&loc);

    return ret;
}

int
pl_lockinfo_get_brickname(xlator_t *this, inode_t *inode, int32_t *op_errno)
{
    posix_locks_private_t *priv = this->private;
    char *brickname = NULL;
    char *end = NULL;
    char *tmp = NULL;

    int ret = fetch_pathinfo(this, inode, op_errno, &brickname);
    if (ret)
        goto out;

    end = strrchr(brickname, ':');
    if (!end) {
        GF_FREE(brickname);
        ret = -1;
        goto out;
    }

    tmp = brickname;
    brickname = gf_strndup(brickname, (end - brickname));
    if (brickname == NULL) {
        ret = -1;
        goto out;
    }

    priv->brickname = brickname;
    ret = 0;
out:
    GF_FREE(tmp);
    return ret;
}

char *
pl_lockinfo_key(xlator_t *this, inode_t *inode, int32_t *op_errno)
{
    posix_locks_private_t *priv = this->private;
    char *key = NULL;
    int ret = 0;

    if (priv->brickname == NULL) {
        ret = pl_lockinfo_get_brickname(this, inode, op_errno);
        if (ret < 0) {
            gf_log(this->name, GF_LOG_WARNING, "cannot get brickname");
            goto out;
        }
    }

    key = priv->brickname;
out:
    return key;
}

int32_t
pl_fgetxattr_handle_lockinfo(xlator_t *this, fd_t *fd, dict_t *dict,
                             int32_t *op_errno)
{
    char *key = NULL, *buf = NULL;
    int32_t op_ret = 0;
    unsigned long fdnum = 0;
    int32_t len = 0;
    dict_t *tmp = NULL;

    pl_inode_t *pl_inode = pl_inode_get(this, fd->inode, NULL);

    if (!pl_inode) {
        gf_log(this->name, GF_LOG_DEBUG, "Could not get inode.");
        *op_errno = EBADFD;
        op_ret = -1;
        goto out;
    }

    if (!pl_locks_by_fd(pl_inode, fd)) {
        op_ret = 0;
        goto out;
    }

    fdnum = fd_to_fdnum(fd);

    key = pl_lockinfo_key(this, fd->inode, op_errno);
    if (key == NULL) {
        op_ret = -1;
        goto out;
    }

    tmp = dict_new();
    if (tmp == NULL) {
        op_ret = -1;
        *op_errno = ENOMEM;
        goto out;
    }

    op_ret = dict_set_uint64(tmp, key, fdnum);
    if (op_ret < 0) {
        *op_errno = -op_ret;
        op_ret = -1;
        gf_log(this->name, GF_LOG_WARNING,
               "setting lockinfo value "
               "(%lu) for fd (ptr:%p inode-gfid:%s) failed (%s)",
               fdnum, fd, uuid_utoa(fd->inode->gfid), strerror(*op_errno));
        goto out;
    }

    op_ret = dict_allocate_and_serialize(tmp, (char **)&buf,
                                         (unsigned int *)&len);
    if (op_ret != 0) {
        *op_errno = -op_ret;
        op_ret = -1;
        gf_log(this->name, GF_LOG_WARNING,
               "dict_serialized_length failed (%s) while handling "
               "lockinfo for fd (ptr:%p inode-gfid:%s)",
               strerror(*op_errno), fd, uuid_utoa(fd->inode->gfid));
        goto out;
    }

    op_ret = dict_set_dynptr(dict, GF_XATTR_LOCKINFO_KEY, buf, len);
    if (op_ret < 0) {
        *op_errno = -op_ret;
        op_ret = -1;
        gf_log(this->name, GF_LOG_WARNING,
               "setting lockinfo value "
               "(%lu) for fd (ptr:%p inode-gfid:%s) failed (%s)",
               fdnum, fd, uuid_utoa(fd->inode->gfid), strerror(*op_errno));
        goto out;
    }

    buf = NULL;
out:
    if (tmp != NULL) {
        dict_unref(tmp);
    }

    if (buf != NULL) {
        GF_FREE(buf);
    }

    return op_ret;
}

int32_t
pl_fgetxattr(call_frame_t *frame, xlator_t *this, fd_t *fd, const char *name,
             dict_t *xdata)
{
    int32_t op_ret = 0, op_errno = 0;
    dict_t *dict = NULL;

    if (!name) {
        goto usual;
    }

    if (strcmp(name, GF_XATTR_LOCKINFO_KEY) == 0) {
        dict = dict_new();
        if (dict == NULL) {
            op_ret = -1;
            op_errno = ENOMEM;
            goto unwind;
        }

        op_ret = pl_fgetxattr_handle_lockinfo(this, fd, dict, &op_errno);
        if (op_ret < 0) {
            gf_log(this->name, GF_LOG_WARNING,
                   "getting lockinfo on fd (ptr:%p inode-gfid:%s) "
                   "failed (%s)",
                   fd, uuid_utoa(fd->inode->gfid), strerror(op_errno));
        }

        goto unwind;
    } else if (strncmp(name, GF_XATTR_CLRLK_CMD, SLEN(GF_XATTR_CLRLK_CMD)) ==
               0) {
        op_ret = pl_getxattr_clrlk(this, name, fd->inode, &dict, &op_errno);

        goto unwind;
    } else {
        goto usual;
    }

unwind:
    STACK_UNWIND_STRICT(fgetxattr, frame, op_ret, op_errno, dict, NULL);
    if (dict != NULL) {
        dict_unref(dict);
    }

    return 0;

usual:
    STACK_WIND(frame, default_fgetxattr_cbk, FIRST_CHILD(this),
               FIRST_CHILD(this)->fops->fgetxattr, fd, name, xdata);
    return 0;
}

int32_t
pl_migrate_locks(call_frame_t *frame, fd_t *newfd, uint64_t oldfd_num,
                 int32_t *op_errno)
{
    posix_lock_t *l = NULL;
    int32_t op_ret = 0;
    uint64_t newfd_num = fd_to_fdnum(newfd);

    pl_inode_t *pl_inode = pl_inode_get(frame->this, newfd->inode, NULL);
    if (pl_inode == NULL) {
        op_ret = -1;
        *op_errno = EBADFD;
        goto out;
    }

    pthread_mutex_lock(&pl_inode->mutex);
    {
        list_for_each_entry(l, &pl_inode->ext_list, list)
        {
            if (l->fd_num == oldfd_num) {
                l->fd_num = newfd_num;
                l->client = frame->root->client;
            }
        }
    }
    pthread_mutex_unlock(&pl_inode->mutex);

    op_ret = 0;
out:
    return op_ret;
}

int32_t
pl_fsetxattr_handle_lockinfo(call_frame_t *frame, fd_t *fd, char *lockinfo_buf,
                             int len, int32_t *op_errno)
{
    int32_t op_ret = -1;
    uint64_t oldfd_num = 0;
    char *key = NULL;

    dict_t *lockinfo = dict_new();
    if (lockinfo == NULL) {
        op_ret = -1;
        *op_errno = ENOMEM;
        goto out;
    }

    op_ret = dict_unserialize(lockinfo_buf, len, &lockinfo);
    if (op_ret < 0) {
        *op_errno = -op_ret;
        op_ret = -1;
        goto out;
    }

    key = pl_lockinfo_key(frame->this, fd->inode, op_errno);
    if (key == NULL) {
        op_ret = -1;
        goto out;
    }

    op_ret = dict_get_uint64(lockinfo, key, &oldfd_num);

    if (oldfd_num == 0) {
        op_ret = 0;
        goto out;
    }

    op_ret = pl_migrate_locks(frame, fd, oldfd_num, op_errno);
    if (op_ret < 0) {
        gf_log(frame->this->name, GF_LOG_WARNING,
               "migration of locks from oldfd (ptr:%p) to newfd "
               "(ptr:%p) (inode-gfid:%s)",
               (void *)(uintptr_t)oldfd_num, fd, uuid_utoa(fd->inode->gfid));
        goto out;
    }

out:
    dict_unref(lockinfo);

    return op_ret;
}

int32_t
pl_fsetxattr_cbk(call_frame_t *frame, void *cookie, xlator_t *this,
                 int32_t op_ret, int32_t op_errno, dict_t *xdata)
{
    pl_local_t *local = NULL;
    pl_inode_t *pl_inode = NULL;

    local = frame->local;
    if (local && local->update_mlock_enforced_flag && op_ret != -1) {
        pl_inode = pl_inode_get(this, local->inode, NULL);
        if (!pl_inode) {
            op_ret = -1;
            op_errno = ENOMEM;
            goto unwind;
        }

        pthread_mutex_lock(&pl_inode->mutex);
        {
            pl_inode->mlock_enforced = _gf_true;
            pl_inode->check_mlock_info = _gf_false;
        }
        pthread_mutex_unlock(&pl_inode->mutex);
    }

unwind:
    PL_STACK_UNWIND_FOR_CLIENT(fsetxattr, xdata, frame, op_ret, op_errno,
                               xdata);
    return 0;
}

int32_t
pl_fsetxattr(call_frame_t *frame, xlator_t *this, fd_t *fd, dict_t *dict,
             int32_t flags, dict_t *xdata)
{
    int32_t op_errno = 0;
    void *lockinfo_buf = NULL;
    int len = 0;
    char *name = NULL;
    posix_locks_private_t *priv = this->private;

    int32_t op_ret = dict_get_ptr_and_len(dict, GF_XATTR_LOCKINFO_KEY,
                                          &lockinfo_buf, &len);
    if (lockinfo_buf == NULL) {
        goto usual;
    }

    op_ret = pl_fsetxattr_handle_lockinfo(frame, fd, lockinfo_buf, len,
                                          &op_errno);
    if (op_ret < 0) {
        goto unwind;
    }

usual:
    PL_LOCAL_GET_REQUESTS(frame, this, xdata, fd, NULL, NULL);

    PL_CHECK_LOCK_ENFORCE_KEY(frame, dict, name, this, ((loc_t *)NULL), fd,
                              priv);

    STACK_WIND(frame, pl_fsetxattr_cbk, FIRST_CHILD(this),
               FIRST_CHILD(this)->fops->fsetxattr, fd, dict, flags, xdata);
    return 0;

unwind:
    PL_STACK_UNWIND_FOR_CLIENT(fsetxattr, xdata, frame, op_ret, op_errno, NULL);

    return 0;
}

int32_t
pl_opendir_cbk(call_frame_t *frame, void *cookie, xlator_t *this,
               int32_t op_ret, int32_t op_errno, fd_t *fd, dict_t *xdata)
{
    pl_fdctx_t *fdctx = NULL;

    if (op_ret < 0)
        goto unwind;

    fdctx = pl_check_n_create_fdctx(this, fd);
    if (!fdctx) {
        op_errno = ENOMEM;
        op_ret = -1;
        goto unwind;
    }

unwind:
    PL_STACK_UNWIND(opendir, xdata, frame, op_ret, op_errno, fd, xdata);

    return 0;
}

int32_t
pl_opendir(call_frame_t *frame, xlator_t *this, loc_t *loc, fd_t *fd,
           dict_t *xdata)
{
    PL_LOCAL_GET_REQUESTS(frame, this, xdata, fd, NULL, NULL);
    STACK_WIND(frame, pl_opendir_cbk, FIRST_CHILD(this),
               FIRST_CHILD(this)->fops->opendir, loc, fd, xdata);
    return 0;
}

int
pl_flush_cbk(call_frame_t *frame, void *cookie, xlator_t *this, int32_t op_ret,
             int32_t op_errno, dict_t *xdata)
{
    PL_STACK_UNWIND_FOR_CLIENT(flush, xdata, frame, op_ret, op_errno, xdata);

    return 0;
}

int
pl_flush(call_frame_t *frame, xlator_t *this, fd_t *fd, dict_t *xdata)
{
    pl_inode_t *pl_inode = pl_inode_get(this, fd->inode, NULL);
    if (!pl_inode) {
        gf_log(this->name, GF_LOG_DEBUG, "Could not get inode.");
        STACK_UNWIND_STRICT(flush, frame, -1, EBADFD, NULL);
        return 0;
    }

    pthread_mutex_lock(&pl_inode->mutex);
    {
        if (pl_inode->migrated) {
            pthread_mutex_unlock(&pl_inode->mutex);
            STACK_UNWIND_STRICT(flush, frame, -1, EREMOTE, NULL);
            return 0;
        }
    }
    pthread_mutex_unlock(&pl_inode->mutex);

    pl_trace_flush(this, frame, fd);

    if (frame->root->lk_owner.len == 0) {
        /* Handle special case when protocol/server sets lk-owner to zero.
         * This usually happens due to a client disconnection. Hence, free
         * all locks opened with this fd.
         */
        gf_log(this->name, GF_LOG_TRACE, "Releasing all locks with fd %p", fd);
        delete_locks_of_fd(this, pl_inode, fd);
        goto wind;
    }
    pthread_mutex_lock(&pl_inode->mutex);
    {
        __delete_locks_of_owner(pl_inode, frame->root->client,
                                &frame->root->lk_owner);
    }
    pthread_mutex_unlock(&pl_inode->mutex);

    grant_blocked_locks(this, pl_inode);

    do_blocked_rw(pl_inode);

wind:
    PL_LOCAL_GET_REQUESTS(frame, this, xdata, fd, NULL, NULL);
    STACK_WIND(frame, pl_flush_cbk, FIRST_CHILD(this),
               FIRST_CHILD(this)->fops->flush, fd, xdata);
    return 0;
}

int
pl_open_cbk(call_frame_t *frame, void *cookie, xlator_t *this, int32_t op_ret,
            int32_t op_errno, fd_t *fd, dict_t *xdata)
{
    pl_fdctx_t *fdctx = NULL;

    if (op_ret < 0)
        goto unwind;

    fdctx = pl_check_n_create_fdctx(this, fd);
    if (!fdctx) {
        op_errno = ENOMEM;
        op_ret = -1;
        goto unwind;
    }

unwind:
    STACK_UNWIND_STRICT(open, frame, op_ret, op_errno, fd, xdata);

    return 0;
}

int
pl_open(call_frame_t *frame, xlator_t *this, loc_t *loc, int32_t flags,
        fd_t *fd, dict_t *xdata)
{
    int op_ret = -1;
    int op_errno = EINVAL;
    pl_inode_t *pl_inode = NULL;
    posix_lock_t *l = NULL;
    posix_locks_private_t *priv = this->private;

    GF_VALIDATE_OR_GOTO("locks", this, unwind);

    op_ret = 0, op_errno = 0;
    pl_inode = pl_inode_get(this, fd->inode, NULL);
    if (!pl_inode) {
        gf_msg(this->name, GF_LOG_ERROR, 0, ENOMEM, "Could not get inode");
        op_ret = -1;
        op_errno = ENOMEM;
        goto unwind;
    }

    /* As per design, under forced and file-based mandatory locking modes
     * it doesn't matter whether inodes's lock list contain advisory or
     * mandatory type locks. So we just check whether inode's lock list is
     * empty or not to make sure that no locks are being held for the file.
     * Whereas under optimal mandatory locking mode, we strictly fail open
     * if and only if lock list contain mandatory locks.
     */
    if (((priv->mandatory_mode == MLK_FILE_BASED) && pl_inode->mandatory) ||
        priv->mandatory_mode == MLK_FORCED) {
        if (fd->flags & O_TRUNC) {
            pthread_mutex_lock(&pl_inode->mutex);
            {
                if (!list_empty(&pl_inode->ext_list)) {
                    op_ret = -1;
                    op_errno = EAGAIN;
                }
            }
            pthread_mutex_unlock(&pl_inode->mutex);
        }
    } else if (priv->mandatory_mode == MLK_OPTIMAL) {
        if (fd->flags & O_TRUNC) {
            pthread_mutex_lock(&pl_inode->mutex);
            {
                list_for_each_entry(l, &pl_inode->ext_list, list)
                {
                    if ((l->lk_flags & GF_LK_MANDATORY)) {
                        op_ret = -1;
                        op_errno = EAGAIN;
                        break;
                    }
                }
            }
            pthread_mutex_unlock(&pl_inode->mutex);
        }
    }

unwind:
    if (op_ret == -1)
        STACK_UNWIND_STRICT(open, frame, op_ret, op_errno, NULL, NULL);
    else
        STACK_WIND(frame, pl_open_cbk, FIRST_CHILD(this),
                   FIRST_CHILD(this)->fops->open, loc, flags, fd, xdata);
    return 0;
}

int
pl_create_cbk(call_frame_t *frame, void *cookie, xlator_t *this, int32_t op_ret,
              int32_t op_errno, fd_t *fd, inode_t *inode, struct iatt *buf,
              struct iatt *preparent, struct iatt *postparent, dict_t *xdata)
{
    pl_fdctx_t *fdctx = NULL;

    if (op_ret < 0)
        goto unwind;

    fdctx = pl_check_n_create_fdctx(this, fd);
    if (!fdctx) {
        op_errno = ENOMEM;
        op_ret = -1;
        goto unwind;
    }

unwind:
    PL_STACK_UNWIND(create, xdata, frame, op_ret, op_errno, fd, inode, buf,
                    preparent, postparent, xdata);

    return 0;
}

int
pl_create(call_frame_t *frame, xlator_t *this, loc_t *loc, int32_t flags,
          mode_t mode, mode_t umask, fd_t *fd, dict_t *xdata)
{
    PL_LOCAL_GET_REQUESTS(frame, this, xdata, fd, NULL, NULL);

    STACK_WIND(frame, pl_create_cbk, FIRST_CHILD(this),
               FIRST_CHILD(this)->fops->create, loc, flags, mode, umask, fd,
               xdata);
    return 0;
}

int
pl_readv_cbk(call_frame_t *frame, void *cookie, xlator_t *this, int32_t op_ret,
             int32_t op_errno, struct iovec *vector, int32_t count,
             struct iatt *stbuf, struct iobref *iobref, dict_t *xdata)
{
    pl_track_io_fop_count(frame->local, this, DECREMENT);

    PL_STACK_UNWIND(readv, xdata, frame, op_ret, op_errno, vector, count, stbuf,
                    iobref, xdata);

    return 0;
}

int
pl_writev_cbk(call_frame_t *frame, void *cookie, xlator_t *this, int32_t op_ret,
              int32_t op_errno, struct iatt *prebuf, struct iatt *postbuf,
              dict_t *xdata)
{
    pl_track_io_fop_count(frame->local, this, DECREMENT);

    PL_STACK_UNWIND(writev, xdata, frame, op_ret, op_errno, prebuf, postbuf,
                    xdata);

    return 0;
}

void
do_blocked_rw(pl_inode_t *pl_inode)
{
    struct list_head wind_list;
    pl_rw_req_t *rw = NULL;
    pl_rw_req_t *tmp = NULL;

    INIT_LIST_HEAD(&wind_list);

    pthread_mutex_lock(&pl_inode->mutex);
    {
        list_for_each_entry_safe(rw, tmp, &pl_inode->rw_list, list)
        {
            if (__rw_allowable(pl_inode, &rw->region, rw->stub->fop)) {
                list_del_init(&rw->list);
                list_add_tail(&rw->list, &wind_list);
                if (pl_inode->mlock_enforced &&
                    pl_inode->track_fop_wind_count) {
                    pl_inode->fop_wind_count++;
                }
            }
        }
    }
    pthread_mutex_unlock(&pl_inode->mutex);

    list_for_each_entry_safe(rw, tmp, &wind_list, list)
    {
        list_del_init(&rw->list);
        call_resume(rw->stub);
        GF_FREE(rw);
    }

    return;
}

/* when mandatory lock is enforced:
    If an IO request comes on a region which is out of the boundary of the
    granted mandatory lock, it will be rejected.

    Note: There is no IO blocking with mandatory lock enforced as it may be
    a stale data from an old client.
 */
gf_boolean_t static within_range(posix_lock_t *existing, posix_lock_t *new)
{
    if (existing->fl_start <= new->fl_start && existing->fl_end >= new->fl_end)
        return _gf_true;

    return _gf_false;
}

static int
__rw_allowable(pl_inode_t *pl_inode, posix_lock_t *region, glusterfs_fop_t op)
{
    posix_lock_t *l = NULL;
    posix_locks_private_t *priv = THIS->private;
    int ret = 1;

    if (pl_inode->mlock_enforced) {
        list_for_each_entry(l, &pl_inode->ext_list, list)
        {
            /*
                with lock enforced (fencing) there should not be any blocking
                lock coexisting.
            */
            if (same_owner(l, region)) {
                /* Should range check be strict for same owner with fencing? */
                if (locks_overlap(l, region)) {
                    if (within_range(l, region)) {
                        return 1;
                    } else {
                        /*
                        Should we allow read fop if it does not fit it in the
                        range?
                        if (op == GF_FOP_READ && l->fl_type != F_WRLCK) {
                            return 1;
                        }
                        */
                        return 0;
                    }
                }
            } else {
                if (locks_overlap(l, region)) {
                    /*
                    with fencing should a read from a different owner be
                    allowed if the mandatory lock taken is F_RDLCK?
                    if (op == GF_FOP_READ && l->fl_type != F_WRLCK) {
                        return 1;
                    }
                    */
                    return 0;
                }
            }
        }

        /* No lock has been taken by this owner */
        return 0;
    }

    list_for_each_entry(l, &pl_inode->ext_list, list)
    {
        if (!l->blocked && locks_overlap(l, region) && !same_owner(l, region)) {
            if ((op == GF_FOP_READ) && (l->fl_type != F_WRLCK))
                continue;
            /* Check for mandatory lock under optimal
             * mandatory-locking mode */
            if (priv->mandatory_mode == MLK_OPTIMAL &&
                !(l->lk_flags & GF_LK_MANDATORY))
                continue;
            ret = 0;
            break;
        }
    }

    return ret;
}

int
pl_readv_cont(call_frame_t *frame, xlator_t *this, fd_t *fd, size_t size,
              off_t offset, uint32_t flags, dict_t *xdata)
{
    pl_track_io_fop_count(frame->local, this, INCREMENT);

    STACK_WIND(frame, pl_readv_cbk, FIRST_CHILD(this),
               FIRST_CHILD(this)->fops->readv, fd, size, offset, flags, xdata);

    return 0;
}

int
pl_readv(call_frame_t *frame, xlator_t *this, fd_t *fd, size_t size,
         off_t offset, uint32_t flags, dict_t *xdata)
{
    pl_local_t *local = NULL;
    pl_inode_t *pl_inode = NULL;
    pl_rw_req_t *rw = NULL;
    posix_lock_t region = {
        .list =
            {
                0,
            },
    };
    gf_boolean_t enabled = _gf_false;
    gf_boolean_t can_block = _gf_true;
    int op_ret = 0;
    int op_errno = 0;
    int allowed = 1;

    GF_VALIDATE_OR_GOTO("locks", this, unwind);

    PL_LOCAL_GET_REQUESTS(frame, this, xdata, fd, NULL, NULL);

    if (!frame->local) {
        frame->local = mem_get0(this->local_pool);
        local = frame->local;
        local->inode = inode_ref(fd->inode);
        local->fd = fd_ref(fd);
    }

    pl_inode = pl_inode_get(this, fd->inode, local);
    if (!pl_inode) {
        op_ret = -1;
        op_errno = ENOMEM;
        goto unwind;
    }

    if (frame->root->pid < 0)
        enabled = _gf_false;
    else
        enabled = pl_is_mandatory_locking_enabled(pl_inode);

    if (enabled) {
        region.fl_start = offset;
        region.fl_end = offset + size - 1;
        region.client = frame->root->client;
        region.fd_num = fd_to_fdnum(fd);
        region.client_pid = frame->root->pid;
        region.owner = frame->root->lk_owner;

        pthread_mutex_lock(&pl_inode->mutex);
        {
            allowed = pl_is_fop_allowed(pl_inode, &region, fd, GF_FOP_READ,
                                        &can_block);
            if (allowed == 1) {
                if (pl_inode->mlock_enforced &&
                    pl_inode->track_fop_wind_count) {
                    pl_inode->fop_wind_count++;
                }
                goto unlock;
            } else if (!can_block) {
                op_errno = EAGAIN;
                op_ret = -1;
                goto unlock;
            }

            rw = GF_MALLOC(sizeof(*rw), gf_locks_mt_pl_rw_req_t);
            if (!rw) {
                op_errno = ENOMEM;
                op_ret = -1;
                goto unlock;
            }

            rw->stub = fop_readv_stub(frame, pl_readv_cont, fd, size, offset,
                                      flags, xdata);
            if (!rw->stub) {
                op_errno = ENOMEM;
                op_ret = -1;
                GF_FREE(rw);
                goto unlock;
            }

            rw->region = region;

            list_add_tail(&rw->list, &pl_inode->rw_list);
        }
    unlock:
        pthread_mutex_unlock(&pl_inode->mutex);
    }

    if (allowed == 1) {
        STACK_WIND(frame, pl_readv_cbk, FIRST_CHILD(this),
                   FIRST_CHILD(this)->fops->readv, fd, size, offset, flags,
                   xdata);
    }
unwind:
    if (op_ret == -1)
        PL_STACK_UNWIND(readv, xdata, frame, op_ret, op_errno, NULL, 0, NULL,
                        NULL, NULL);

    return 0;
}

int
pl_writev_cont(call_frame_t *frame, xlator_t *this, fd_t *fd,
               struct iovec *vector, int count, off_t offset, uint32_t flags,
               struct iobref *iobref, dict_t *xdata)
{
    pl_track_io_fop_count(frame->local, this, INCREMENT);

    STACK_WIND(frame, pl_writev_cbk, FIRST_CHILD(this),
               FIRST_CHILD(this)->fops->writev, fd, vector, count, offset,
               flags, iobref, xdata);

    return 0;
}

int
pl_writev(call_frame_t *frame, xlator_t *this, fd_t *fd, struct iovec *vector,
          int32_t count, off_t offset, uint32_t flags, struct iobref *iobref,
          dict_t *xdata)
{
    pl_local_t *local = NULL;
    pl_inode_t *pl_inode = NULL;
    pl_rw_req_t *rw = NULL;
    posix_lock_t region = {
        .list =
            {
                0,
            },
    };
    gf_boolean_t enabled = _gf_false;
    gf_boolean_t can_block = _gf_true;
    int op_ret = 0;
    int op_errno = 0;
    int allowed = 1;

    GF_VALIDATE_OR_GOTO("locks", this, unwind);

    PL_LOCAL_GET_REQUESTS(frame, this, xdata, fd, NULL, NULL);

    if (!frame->local) {
        frame->local = mem_get0(this->local_pool);
        local = frame->local;
        local->inode = inode_ref(fd->inode);
        local->fd = fd_ref(fd);
    }

    pl_inode = pl_inode_get(this, fd->inode, local);
    if (!pl_inode) {
        op_ret = -1;
        op_errno = ENOMEM;
        goto unwind;
    }

    if (frame->root->pid < 0)
        enabled = _gf_false;
    else
        enabled = pl_is_mandatory_locking_enabled(pl_inode);

    if (enabled) {
        region.fl_start = offset;
        region.fl_end = offset + iov_length(vector, count) - 1;
        region.client = frame->root->client;
        region.fd_num = fd_to_fdnum(fd);
        region.client_pid = frame->root->pid;
        region.owner = frame->root->lk_owner;

        pthread_mutex_lock(&pl_inode->mutex);
        {
            allowed = pl_is_fop_allowed(pl_inode, &region, fd, GF_FOP_WRITE,
                                        &can_block);
            if (allowed == 1) {
                if (pl_inode->mlock_enforced &&
                    pl_inode->track_fop_wind_count) {
                    pl_inode->fop_wind_count++;
                }
                goto unlock;
            } else if (!can_block) {
                if (pl_inode->mlock_enforced) {
                    op_errno = EBUSY;
                } else {
                    op_errno = EAGAIN;
                }

                op_ret = -1;
                goto unlock;
            }

            rw = GF_MALLOC(sizeof(*rw), gf_locks_mt_pl_rw_req_t);
            if (!rw) {
                op_errno = ENOMEM;
                op_ret = -1;
                goto unlock;
            }

            rw->stub = fop_writev_stub(frame, pl_writev_cont, fd, vector, count,
                                       offset, flags, iobref, xdata);
            if (!rw->stub) {
                op_errno = ENOMEM;
                op_ret = -1;
                GF_FREE(rw);
                goto unlock;
            }

            rw->region = region;

            list_add_tail(&rw->list, &pl_inode->rw_list);
        }
    unlock:
        pthread_mutex_unlock(&pl_inode->mutex);
    }

    if (allowed == 1) {
        STACK_WIND(frame, pl_writev_cbk, FIRST_CHILD(this),
                   FIRST_CHILD(this)->fops->writev, fd, vector, count, offset,
                   flags, iobref, xdata);
    }
unwind:
    if (op_ret == -1)
        PL_STACK_UNWIND(writev, xdata, frame, op_ret, op_errno, NULL, NULL,
                        NULL);

    return 0;
}

static int
__fd_has_locks(pl_inode_t *pl_inode, fd_t *fd)
{
    posix_lock_t *l = NULL;

    list_for_each_entry(l, &pl_inode->ext_list, list)
    {
        if (l->fd_num == fd_to_fdnum(fd)) {
            return 1;
        }
    }

    return 0;
}

static posix_lock_t *
lock_dup(posix_lock_t *lock)
{
    int32_t op_errno = 0;
    return new_posix_lock(&lock->user_flock, lock->client, lock->client_pid,
                          &lock->owner, (fd_t *)lock->fd_num, lock->lk_flags,
                          lock->blocking, &op_errno);
}

static int
__dup_locks_to_fdctx(pl_inode_t *pl_inode, fd_t *fd, pl_fdctx_t *fdctx)
{
    posix_lock_t *l = NULL;
    posix_lock_t *duplock = NULL;
    int ret = 0;

    list_for_each_entry(l, &pl_inode->ext_list, list)
    {
        if (l->fd_num == fd_to_fdnum(fd)) {
            duplock = lock_dup(l);
            if (!duplock) {
                ret = -1;
                break;
            }

            list_add_tail(&duplock->list, &fdctx->locks_list);
        }
    }

    return ret;
}

static int
__copy_locks_to_fdctx(pl_inode_t *pl_inode, fd_t *fd, pl_fdctx_t *fdctx)
{
    return __dup_locks_to_fdctx(pl_inode, fd, fdctx);
}

static void
pl_mark_eol_lock(posix_lock_t *lock)
{
    lock->user_flock.l_type = GF_LK_EOL;
    return;
}

static posix_lock_t *
__get_next_fdctx_lock(pl_fdctx_t *fdctx)
{
    posix_lock_t *lock = NULL;

    GF_ASSERT(fdctx);

    if (list_empty(&fdctx->locks_list)) {
        gf_log(THIS->name, GF_LOG_DEBUG, "fdctx lock list empty");
        goto out;
    }

    lock = list_entry(fdctx->locks_list.next, typeof(*lock), list);

    GF_ASSERT(lock);

    list_del_init(&lock->list);

out:
    return lock;
}

static int
__set_next_lock_fd(pl_fdctx_t *fdctx, posix_lock_t *reqlock)
{
    posix_lock_t *lock = NULL;
    int ret = 0;

    GF_ASSERT(fdctx);

    lock = __get_next_fdctx_lock(fdctx);
    if (!lock) {
        gf_log(THIS->name, GF_LOG_DEBUG, "marking EOL in reqlock");
        pl_mark_eol_lock(reqlock);
        goto out;
    }

    reqlock->user_flock = lock->user_flock;
    reqlock->fl_start = lock->fl_start;
    reqlock->fl_type = lock->fl_type;
    reqlock->fl_end = lock->fl_end;
    reqlock->owner = lock->owner;

out:
    if (lock)
        __destroy_lock(lock);

    return ret;
}

static int
pl_getlk_fd(xlator_t *this, pl_inode_t *pl_inode, fd_t *fd,
            posix_lock_t *reqlock)
{
    uint64_t tmp = 0;
    pl_fdctx_t *fdctx = NULL;
    int ret = 0;

    pthread_mutex_lock(&pl_inode->mutex);
    {
        if (!__fd_has_locks(pl_inode, fd)) {
            pthread_mutex_unlock(&pl_inode->mutex);
            gf_log(this->name, GF_LOG_DEBUG, "fd=%p has no active locks", fd);
            ret = 0;
            goto out;
        }

        gf_log(this->name, GF_LOG_DEBUG, "There are active locks on fd");

        ret = fd_ctx_get(fd, this, &tmp);
        fdctx = (pl_fdctx_t *)(long)tmp;

        if (list_empty(&fdctx->locks_list)) {
            gf_log(this->name, GF_LOG_TRACE,
                   "no fdctx -> copying all locks on fd");

            ret = __copy_locks_to_fdctx(pl_inode, fd, fdctx);
            if (ret) {
                goto unlock;
            }

            ret = __set_next_lock_fd(fdctx, reqlock);

        } else {
            gf_log(this->name, GF_LOG_TRACE,
                   "fdctx present -> returning the next lock");
            ret = __set_next_lock_fd(fdctx, reqlock);
            if (ret) {
                pthread_mutex_unlock(&pl_inode->mutex);
                gf_log(this->name, GF_LOG_DEBUG,
                       "could not get next lock of fd");
                goto out;
            }
        }
    }

unlock:
    pthread_mutex_unlock(&pl_inode->mutex);
out:
    return ret;
}

int
pl_metalock_is_active(pl_inode_t *pl_inode)
{
    if (list_empty(&pl_inode->metalk_list))
        return 0;
    else
        return 1;
}

void
__pl_queue_lock(pl_inode_t *pl_inode, posix_lock_t *reqlock)
{
    list_add_tail(&reqlock->list, &pl_inode->queued_locks);
}

int
pl_lk(call_frame_t *frame, xlator_t *this, fd_t *fd, int32_t cmd,
      struct gf_flock *flock, dict_t *xdata)
{
    pl_inode_t *pl_inode = NULL;
    int op_ret = 0;
    int op_errno = 0;
    int can_block = 0;
    posix_lock_t *reqlock = NULL;
    posix_lock_t *conf = NULL;
    uint32_t lk_flags = 0;
    posix_locks_private_t *priv = this->private;
    pl_local_t *local = NULL;
    short lock_type = 0;

    int ret = dict_get_uint32(xdata, GF_LOCK_MODE, &lk_flags);
    if (ret == 0) {
        if (priv->mandatory_mode == MLK_NONE)
            gf_log(this->name, GF_LOG_DEBUG,
                   "Lock flags received "
                   "in a non-mandatory locking environment, "
                   "continuing");
        else
            gf_log(this->name, GF_LOG_DEBUG,
                   "Lock flags received, "
                   "continuing");
    }

    if ((flock->l_start < 0) || ((flock->l_start + flock->l_len) < 0)) {
        op_ret = -1;
        op_errno = EINVAL;
        goto unwind;
    }

    /* As per 'man 3 fcntl', the value of l_len may be
     * negative. In such cases, lock request should be
     * considered for the range starting at 'l_start+l_len'
     * and ending at 'l_start-1'. Update the fields accordingly.
     */
    if (flock->l_len < 0) {
        flock->l_start += flock->l_len;
        flock->l_len = labs(flock->l_len);
    }

    local = mem_get0(this->local_pool);
    if (!local) {
        op_ret = -1;
        op_errno = ENOMEM;
        goto unwind;
    } else {
        frame->local = local;
        local->fd = fd_ref(fd);
    }

    pl_inode = pl_inode_get(this, fd->inode, local);
    if (!pl_inode) {
        op_ret = -1;
        op_errno = ENOMEM;
        goto unwind;
    }

    reqlock = new_posix_lock(flock, frame->root->client, frame->root->pid,
                             &frame->root->lk_owner, fd, lk_flags, can_block,
                             &op_errno);

    if (!reqlock) {
        op_ret = -1;
        goto unwind;
    }

    pl_trace_in(this, frame, fd, NULL, cmd, flock, NULL);

    switch (cmd) {
        case F_RESLK_LCKW:
            can_block = 1;

            /* fall through */
        case F_RESLK_LCK:
            reqlock->frame = frame;
            reqlock->this = this;

            ret = pl_reserve_setlk(this, pl_inode, reqlock, can_block);
            if (ret < 0) {
                if (can_block)
                    goto out;

                op_ret = -1;
                op_errno = -ret;
                __destroy_lock(reqlock);
                goto unwind;
            }
            /* Finally a getlk and return the call */
            conf = pl_getlk(pl_inode, reqlock);
            if (conf)
                posix_lock_to_flock(conf, flock);
            break;

        case F_RESLK_UNLCK:
            reqlock->frame = frame;
            reqlock->this = this;
            ret = pl_reserve_unlock(this, pl_inode, reqlock);
            if (ret < 0) {
                op_ret = -1;
                op_errno = -ret;
            }
            __destroy_lock(reqlock);
            goto unwind;

            break;

        case F_GETLK_FD:
            reqlock->frame = frame;
            reqlock->this = this;
            ret = pl_verify_reservelk(this, pl_inode, reqlock, can_block);
            GF_ASSERT(ret >= 0);

            ret = pl_getlk_fd(this, pl_inode, fd, reqlock);
            if (ret < 0) {
                gf_log(this->name, GF_LOG_DEBUG, "getting locks on fd failed");
                op_ret = -1;
                op_errno = ENOLCK;
                goto unwind;
            }

            gf_log(this->name, GF_LOG_TRACE,
                   "Replying with a lock on fd for healing");

            posix_lock_to_flock(reqlock, flock);
            __destroy_lock(reqlock);

            break;

#if F_GETLK != F_GETLK64
        case F_GETLK64:
#endif
        case F_GETLK:
            conf = pl_getlk(pl_inode, reqlock);
            posix_lock_to_flock(conf, flock);
            __destroy_lock(reqlock);

            break;

#if F_SETLKW != F_SETLKW64
        case F_SETLKW64:
#endif
        case F_SETLKW:
            can_block = 1;
            reqlock->frame = frame;
            reqlock->this = this;
            reqlock->blocking = can_block;
            /* fall through */

#if F_SETLK != F_SETLK64
        case F_SETLK64:
#endif
        case F_SETLK:
            reqlock->frame = frame;
            reqlock->this = this;
            lock_type = flock->l_type;

            pthread_mutex_lock(&pl_inode->mutex);
            {
                if (pl_inode->migrated) {
                    op_errno = EREMOTE;
                    pthread_mutex_unlock(&pl_inode->mutex);
                    STACK_UNWIND_STRICT(lk, frame, -1, op_errno, flock, xdata);

                    __destroy_lock(reqlock);
                    goto out;
                }
            }
            pthread_mutex_unlock(&pl_inode->mutex);

            ret = pl_verify_reservelk(this, pl_inode, reqlock, can_block);
            if (ret < 0) {
                gf_log(this->name, GF_LOG_TRACE,
                       "Lock blocked due to conflicting reserve lock");
                goto out;
            }

            if (reqlock->fl_type != F_UNLCK && pl_inode->mlock_enforced) {
                ret = pl_lock_preempt(pl_inode, reqlock);
                if (ret == -1) {
                    gf_log(this->name, GF_LOG_ERROR, "lock preempt failed");
                    op_ret = -1;
                    op_errno = EAGAIN;
                    __destroy_lock(reqlock);
                    goto out;
                }

                pl_trace_block(this, frame, fd, NULL, cmd, flock, NULL);
                goto unwind;
            }

            ret = pl_setlk(this, pl_inode, reqlock, can_block);
            if (ret == -1) {
                if ((can_block) && (F_UNLCK != lock_type)) {
                    goto out;
                }
                gf_log(this->name, GF_LOG_DEBUG, "returning EAGAIN");
                op_ret = -1;
                op_errno = EAGAIN;
                __destroy_lock(reqlock);
            } else if (ret == -2) {
                goto out;
            } else if ((0 == ret) && (F_UNLCK == flock->l_type)) {
                /* For NLM's last "unlock on fd" detection */
                if (pl_locks_by_fd(pl_inode, fd))
                    flock->l_type = F_RDLCK;
                else
                    flock->l_type = F_UNLCK;
            }
    }

unwind:
    pl_trace_out(this, frame, fd, NULL, cmd, flock, op_ret, op_errno, NULL);
    pl_update_refkeeper(this, fd->inode);

    PL_STACK_UNWIND(lk, xdata, frame, op_ret, op_errno, flock, xdata);
out:
    return 0;
}

/* TODO: this function just logs, no action required?? */
int
pl_forget(xlator_t *this, inode_t *inode)
{
    pl_inode_t *pl_inode = NULL;

    posix_lock_t *ext_tmp = NULL;
    posix_lock_t *ext_l = NULL;
    struct list_head posixlks_released;

    pl_inode_lock_t *ino_tmp = NULL;
    pl_inode_lock_t *ino_l = NULL;
    struct list_head inodelks_released;

    pl_rw_req_t *rw_tmp = NULL;
    pl_rw_req_t *rw_req = NULL;

    pl_entry_lock_t *entry_tmp = NULL;
    pl_entry_lock_t *entry_l = NULL;
    struct list_head entrylks_released;

    pl_dom_list_t *dom = NULL;
    pl_dom_list_t *dom_tmp = NULL;

    INIT_LIST_HEAD(&posixlks_released);
    INIT_LIST_HEAD(&inodelks_released);
    INIT_LIST_HEAD(&entrylks_released);

    pl_inode = pl_inode_get(this, inode, NULL);
    if (!pl_inode)
        return 0;

    pthread_mutex_lock(&pl_inode->mutex);
    {
        if (!list_empty(&pl_inode->rw_list)) {
            gf_log(this->name, GF_LOG_WARNING,
                   "Pending R/W requests found, releasing.");

            list_for_each_entry_safe(rw_req, rw_tmp, &pl_inode->rw_list, list)
            {
                list_del(&rw_req->list);
                call_stub_destroy(rw_req->stub);
                GF_FREE(rw_req);
            }
        }

        if (!list_empty(&pl_inode->ext_list)) {
            gf_log(this->name, GF_LOG_WARNING,
                   "Pending fcntl locks found, releasing.");

            list_for_each_entry_safe(ext_l, ext_tmp, &pl_inode->ext_list, list)
            {
                __delete_lock(ext_l);
                if (ext_l->blocked) {
                    list_add_tail(&ext_l->list, &posixlks_released);
                    continue;
                }
                __destroy_lock(ext_l);
            }
        }

        list_for_each_entry_safe(dom, dom_tmp, &pl_inode->dom_list, inode_list)
        {
            if (!list_empty(&dom->inodelk_list)) {
                gf_log(this->name, GF_LOG_WARNING,
                       "Pending inode locks found, releasing.");

                list_for_each_entry_safe(ino_l, ino_tmp, &dom->inodelk_list,
                                         list)
                {
                    __delete_inode_lock(ino_l);
                    __pl_inodelk_unref(ino_l);
                }

                list_splice_init(&dom->blocked_inodelks, &inodelks_released);
            }
            if (!list_empty(&dom->entrylk_list)) {
                gf_log(this->name, GF_LOG_WARNING,
                       "Pending entry locks found, releasing.");

                list_for_each_entry_safe(entry_l, entry_tmp, &dom->entrylk_list,
                                         domain_list)
                {
                    list_del_init(&entry_l->domain_list);

                    GF_FREE((char *)entry_l->basename);
                    GF_FREE(entry_l->connection_id);
                    GF_FREE(entry_l);
                }

                list_splice_init(&dom->blocked_entrylks, &entrylks_released);
            }

            list_del(&dom->inode_list);
            gf_log("posix-locks", GF_LOG_TRACE, " Cleaning up domain: %s",
                   dom->domain);
            GF_FREE((char *)(dom->domain));
            GF_FREE(dom);
        }
    }
    pthread_mutex_unlock(&pl_inode->mutex);

    if (!list_empty(&posixlks_released)) {
        list_for_each_entry_safe(ext_l, ext_tmp, &posixlks_released, list)
        {
            STACK_UNWIND_STRICT(lk, ext_l->frame, -1, 0, &ext_l->user_flock,
                                NULL);
            __destroy_lock(ext_l);
        }
    }

    if (!list_empty(&inodelks_released)) {
        list_for_each_entry_safe(ino_l, ino_tmp, &inodelks_released,
                                 blocked_locks)
        {
            STACK_UNWIND_STRICT(inodelk, ino_l->frame, -1, 0, NULL);
            __pl_inodelk_unref(ino_l);
        }
    }

    if (!list_empty(&entrylks_released)) {
        list_for_each_entry_safe(entry_l, entry_tmp, &entrylks_released,
                                 blocked_locks)
        {
            STACK_UNWIND_STRICT(entrylk, entry_l->frame, -1, 0, NULL);
            GF_FREE((char *)entry_l->basename);
            GF_FREE(entry_l->connection_id);
            GF_FREE(entry_l);
        }
    }

    pthread_mutex_destroy(&pl_inode->mutex);

    GF_FREE(pl_inode);

    return 0;
}

int
pl_release(xlator_t *this, fd_t *fd)
{
    pl_inode_t *pl_inode = NULL;
    uint64_t tmp_pl_inode = 0;
    int ret = -1;
    uint64_t tmp = 0;
    pl_fdctx_t *fdctx = NULL;

    if (fd == NULL) {
        goto out;
    }

    ret = inode_ctx_get(fd->inode, this, &tmp_pl_inode);
    if (ret != 0)
        goto clean;

    pl_inode = (pl_inode_t *)(long)tmp_pl_inode;

    pl_trace_release(this, fd);

    gf_log(this->name, GF_LOG_TRACE, "Releasing all locks with fd %p", fd);

    delete_locks_of_fd(this, pl_inode, fd);
    pl_update_refkeeper(this, fd->inode);

clean:
    ret = fd_ctx_del(fd, this, &tmp);
    if (ret) {
        gf_log(this->name, GF_LOG_DEBUG, "Could not get fdctx");
        goto out;
    }

    fdctx = (pl_fdctx_t *)(long)tmp;

    GF_FREE(fdctx);
out:
    return ret;
}

int
pl_releasedir(xlator_t *this, fd_t *fd)
{
    int ret = -1;
    uint64_t tmp = 0;
    pl_fdctx_t *fdctx = NULL;

    if (fd == NULL) {
        goto out;
    }

    ret = fd_ctx_del(fd, this, &tmp);
    if (ret) {
        gf_log(this->name, GF_LOG_DEBUG, "Could not get fdctx");
        goto out;
    }

    fdctx = (pl_fdctx_t *)(long)tmp;

    GF_FREE(fdctx);
out:
    return ret;
}

static int32_t
pl_request_link_count(dict_t **pxdata)
{
    dict_t *xdata;

    xdata = *pxdata;
    if (xdata == NULL) {
        xdata = dict_new();
        if (xdata == NULL) {
            return ENOMEM;
        }
    } else {
        dict_ref(xdata);
    }

    if (dict_set_uint32(xdata, GET_LINK_COUNT, 0) != 0) {
        dict_unref(xdata);
        return ENOMEM;
    }

    *pxdata = xdata;

    return 0;
}

static int32_t
pl_check_link_count(dict_t *xdata)
{
    int32_t count;

    /* In case we are unable to read the link count from xdata, we take a
     * conservative approach and return -2, which will prevent the inode from
     * being considered deleted. In fact it will cause link tracking for this
     * inode to be disabled completely to avoid races. */

    if (xdata == NULL) {
        return -2;
    }

    if (dict_get_int32(xdata, GET_LINK_COUNT, &count) != 0) {
        return -2;
    }

    return count;
}

int32_t
pl_lookup_cbk(call_frame_t *frame, void *cookie, xlator_t *this, int32_t op_ret,
              int32_t op_errno, inode_t *inode, struct iatt *buf, dict_t *xdata,
              struct iatt *postparent)
{
    pl_inode_t *pl_inode;

    if (op_ret >= 0) {
        pl_inode = pl_inode_get(this, inode, NULL);
        if (pl_inode == NULL) {
            PL_STACK_UNWIND(lookup, xdata, frame, -1, ENOMEM, NULL, NULL, NULL,
                            NULL);
            return 0;
        }

        pthread_mutex_lock(&pl_inode->mutex);

        /* We only update the link count if we previously didn't know it.
         * Doing it always can lead to races since lookup is not executed
         * atomically most of the times. */
        if (pl_inode->links == -2) {
            pl_inode->links = pl_check_link_count(xdata);
            if (buf->ia_type == IA_IFDIR) {
                /* Directories have at least 2 links. To avoid special handling
                 * for directories, we simply decrement the value here to make
                 * them equivalent to regular files. */
                pl_inode->links--;
            }
        }

        pthread_mutex_unlock(&pl_inode->mutex);
    }

    PL_STACK_UNWIND(lookup, xdata, frame, op_ret, op_errno, inode, buf, xdata,
                    postparent);
    return 0;
}

int32_t
pl_lookup(call_frame_t *frame, xlator_t *this, loc_t *loc, dict_t *xdata)
{
    int32_t error;

    error = pl_request_link_count(&xdata);
    if (error == 0) {
        PL_LOCAL_GET_REQUESTS(frame, this, xdata, ((fd_t *)NULL), loc, NULL);
        STACK_WIND(frame, pl_lookup_cbk, FIRST_CHILD(this),
                   FIRST_CHILD(this)->fops->lookup, loc, xdata);
        dict_unref(xdata);
    } else {
        STACK_UNWIND_STRICT(lookup, frame, -1, error, NULL, NULL, NULL, NULL);
    }
    return 0;
}

int32_t
pl_fstat_cbk(call_frame_t *frame, void *cookie, xlator_t *this, int32_t op_ret,
             int32_t op_errno, struct iatt *buf, dict_t *xdata)
{
    PL_STACK_UNWIND(fstat, xdata, frame, op_ret, op_errno, buf, xdata);
    return 0;
}

int32_t
pl_fstat(call_frame_t *frame, xlator_t *this, fd_t *fd, dict_t *xdata)
{
    PL_LOCAL_GET_REQUESTS(frame, this, xdata, fd, NULL, NULL);
    STACK_WIND(frame, pl_fstat_cbk, FIRST_CHILD(this),
               FIRST_CHILD(this)->fops->fstat, fd, xdata);
    return 0;
}

int
pl_readdirp_cbk(call_frame_t *frame, void *cookie, xlator_t *this, int op_ret,
                int op_errno, gf_dirent_t *entries, dict_t *xdata)
{
    pl_local_t *local = NULL;
    gf_dirent_t *entry = NULL;

    if (op_ret <= 0)
        goto unwind;

    local = frame->local;
    if (!local)
        goto unwind;

    list_for_each_entry(entry, &entries->list, list)
    {
        pl_set_xdata_response(this, local, local->fd->inode, entry->inode,
                              entry->d_name, entry->dict, 0);
    }

unwind:
    PL_STACK_UNWIND(readdirp, xdata, frame, op_ret, op_errno, entries, xdata);

    return 0;
}

int
pl_readdirp(call_frame_t *frame, xlator_t *this, fd_t *fd, size_t size,
            off_t offset, dict_t *xdata)
{
    PL_LOCAL_GET_REQUESTS(frame, this, xdata, fd, NULL, NULL);
    STACK_WIND(frame, pl_readdirp_cbk, FIRST_CHILD(this),
               FIRST_CHILD(this)->fops->readdirp, fd, size, offset, xdata);

    return 0;
}

lock_migration_info_t *
gf_mig_info_for_lock(posix_lock_t *lock)
{
    lock_migration_info_t *new = GF_MALLOC(sizeof(lock_migration_info_t),
                                           gf_common_mt_lock_mig);
    if (new == NULL) {
        goto out;
    }

    INIT_LIST_HEAD(&new->list);

    posix_lock_to_flock(lock, &new->flock);

    new->lk_flags = lock->lk_flags;

    new->client_uid = gf_strdup(lock->client_uid);

out:
    return new;
}

int
pl_fill_active_locks(pl_inode_t *pl_inode, lock_migration_info_t *lmi)
{
    posix_lock_t *temp = NULL;
    lock_migration_info_t *newlock = NULL;
    int count = 0;

    pthread_mutex_lock(&pl_inode->mutex);
    {
        if (list_empty(&pl_inode->ext_list)) {
            count = 0;
            goto unlock;
        }

        list_for_each_entry(temp, &pl_inode->ext_list, list)
        {
            if (temp->blocked)
                continue;

            newlock = gf_mig_info_for_lock(temp);
            if (!newlock) {
                pthread_mutex_unlock(&pl_inode->mutex);
                gf_msg(THIS->name, GF_LOG_ERROR, 0, 0, "lock_dup failed");
                count = -1;
                goto out;
            }

            list_add_tail(&newlock->list, &lmi->list);
            count++;
        }
    }

unlock:
    pthread_mutex_unlock(&pl_inode->mutex);
out:
    return count;
}

/* This function reads only active locks */
static int
pl_getactivelk(call_frame_t *frame, xlator_t *this, loc_t *loc, dict_t *xdata)
{
    pl_inode_t *pl_inode = NULL;
    lock_migration_info_t locks;
    int op_ret = 0;
    int op_errno = 0;
    int count = 0;

    INIT_LIST_HEAD(&locks.list);

    pl_inode = pl_inode_get(this, loc->inode, NULL);
    if (!pl_inode) {
        gf_msg(this->name, GF_LOG_ERROR, 0, 0, "pl_inode_get failed");

        op_ret = -1;
        op_errno = ENOMEM;
        goto out;
    }

    count = pl_fill_active_locks(pl_inode, &locks);

    op_ret = count;

out:
    STACK_UNWIND_STRICT(getactivelk, frame, op_ret, op_errno, &locks, NULL);

    gf_free_mig_locks(&locks);

    return 0;
}

void
pl_metalk_unref(pl_meta_lock_t *lock)
{
    lock->ref--;
    if (!lock->ref) {
        GF_FREE(lock->client_uid);
        GF_FREE(lock);
    }
}

void
__pl_metalk_ref(pl_meta_lock_t *lock)
{
    lock->ref++;
}

pl_meta_lock_t *
new_meta_lock(call_frame_t *frame, xlator_t *this)
{
    pl_meta_lock_t *lock = GF_CALLOC(1, sizeof(*lock),
                                     gf_locks_mt_pl_meta_lock_t);

    if (!lock) {
        gf_msg(this->name, GF_LOG_ERROR, 0, ENOMEM,
               "mem allocation"
               " failed for meta lock");
        goto out;
    }

    INIT_LIST_HEAD(&lock->list);
    INIT_LIST_HEAD(&lock->client_list);

    lock->client_uid = gf_strdup(frame->root->client->client_uid);
    if (!lock->client_uid) {
        gf_msg(this->name, GF_LOG_ERROR, 0, ENOMEM,
               "mem allocation"
               " failed for client_uid");
        GF_FREE(lock);
        lock = NULL;
        goto out;
    }

    __pl_metalk_ref(lock);
out:
    return lock;
}

int
pl_insert_metalk(pl_inode_t *pl_inode, pl_ctx_t *ctx, pl_meta_lock_t *lock)
{
    int ret = 0;

    if (!pl_inode || !ctx || !lock) {
        gf_msg(THIS->name, GF_LOG_INFO, 0, 0, "NULL parameter");
        ret = -1;
        goto out;
    }

    lock->pl_inode = pl_inode;

    /* refer function pl_inode_setlk for more info for this ref.
     * This should be unrefed on meta-unlock triggered by rebalance or
     * in cleanup with client disconnect*/
    /*TODO: unref this in  cleanup code for disconnect and meta-unlock*/
    pl_inode->inode = inode_ref(pl_inode->inode);

    /* NOTE:In case of a client-server disconnect we need to cleanup metalk.
     * Hence, adding the metalk to pl_ctx_t as well. The mutex lock order
     * should always be on ctx and then on pl_inode*/

    pthread_mutex_lock(&ctx->lock);
    {
        pthread_mutex_lock(&pl_inode->mutex);
        {
            list_add_tail(&lock->list, &pl_inode->metalk_list);
        }
        pthread_mutex_unlock(&pl_inode->mutex);

        list_add_tail(&lock->client_list, &ctx->metalk_list);
    }
    pthread_mutex_unlock(&ctx->lock);

out:
    return ret;
}

int32_t
pl_metalk(call_frame_t *frame, xlator_t *this, inode_t *inode)
{
    pl_inode_t *pl_inode = NULL;
    int ret = 0;
    pl_meta_lock_t *reqlk = NULL;
    pl_ctx_t *ctx = NULL;

    pl_inode = pl_inode_get(this, inode, NULL);
    if (!pl_inode) {
        gf_msg(this->name, GF_LOG_ERROR, 0, ENOMEM,
               "pl_inode mem allocation failedd");

        ret = -1;
        goto out;
    }

    /* Non rebalance process trying to do metalock */
    if (frame->root->pid != GF_CLIENT_PID_DEFRAG) {
        ret = -1;
        goto out;
    }

    /* Note: In the current scheme of glusterfs where lock migration is
     * experimental, (ideally) the rebalance process which is migrating
     * the file should request for a metalock. Hence, the metalock count
     * should not be more than one for an inode. In future, if there is a
     * need for meta-lock from other clients, the following block can be
     * removed.
     *
     * Since pl_metalk is called as part of setxattr operation, any client
     * process(non-rebalance) residing outside trusted network can exhaust
     * memory of the server node by issuing setxattr repetitively on the
     * metalock key. The following code makes sure that more than
     * one metalock cannot be granted on an inode*/
    pthread_mutex_lock(&pl_inode->mutex);
    {
        if (pl_metalock_is_active(pl_inode)) {
            ret = -1;
        }
    }
    pthread_mutex_unlock(&pl_inode->mutex);

    if (ret == -1) {
        gf_msg(this->name, GF_LOG_WARNING, EINVAL, 0,
               "More than one meta-lock cannot be granted on"
               " the inode");
        goto out;
    }

    if (frame->root->client) {
        ctx = pl_ctx_get(frame->root->client, this);
        if (!ctx) {
            gf_msg(this->name, GF_LOG_ERROR, 0, 0, "pl_ctx_get failed");

            ret = -1;
            goto out;
        }
    } else {
        gf_msg(this->name, GF_LOG_INFO, 0, 0,
               "frame-root-client "
               "is NULL");

        ret = -1;
        goto out;
    }

    reqlk = new_meta_lock(frame, this);
    if (!reqlk) {
        ret = -1;
        goto out;
    }

    ret = pl_insert_metalk(pl_inode, ctx, reqlk);
    if (ret < 0) {
        pl_metalk_unref(reqlk);
    }

out:
    return ret;
}

static void
__unwind_queued_locks(pl_inode_t *pl_inode, struct list_head *tmp_list)
{
    if (list_empty(&pl_inode->queued_locks))
        return;

    list_splice_init(&pl_inode->queued_locks, tmp_list);
}

static void
__unwind_blocked_locks(pl_inode_t *pl_inode, struct list_head *tmp_list)
{
    posix_lock_t *lock = NULL;
    posix_lock_t *tmp = NULL;

    if (list_empty(&pl_inode->ext_list))
        return;

    list_for_each_entry_safe(lock, tmp, &pl_inode->ext_list, list)
    {
        if (!lock->blocking)
            continue;

        list_del_init(&lock->list);
        list_add_tail(&lock->list, tmp_list);
    }
}

int
pl_metaunlock(call_frame_t *frame, xlator_t *this, inode_t *inode, dict_t *dict)
{
    pl_inode_t *pl_inode = NULL;
    int ret = 0;
    pl_meta_lock_t *meta_lock = NULL;
    pl_meta_lock_t *tmp_metalk = NULL;
    pl_ctx_t *ctx = NULL;
    posix_lock_t *posix_lock = NULL;
    posix_lock_t *tmp_posixlk = NULL;
    struct list_head tmp_posixlk_list;

    INIT_LIST_HEAD(&tmp_posixlk_list);

    if (frame->root->client) {
        ctx = pl_ctx_get(frame->root->client, this);
        if (!ctx) {
            gf_msg(this->name, GF_LOG_ERROR, 0, 0, "pl_ctx_get failed");

            ret = -1;
            goto out;
        }
    } else {
        gf_msg(this->name, GF_LOG_ERROR, 0, 0,
               "frame-root-client is "
               "NULL");
        ret = -1;
        goto out;
    }

    pl_inode = pl_inode_get(this, inode, NULL);
    if (!pl_inode) {
        ret = -1;
        goto out;
    }

    pthread_mutex_lock(&ctx->lock);
    {
        pthread_mutex_lock(&pl_inode->mutex);
        {
            /* Unwind queued locks regardless of migration status */
            __unwind_queued_locks(pl_inode, &tmp_posixlk_list);

            /* Unwind blocked locks only for successful migration */
            if (dict_get_sizen(dict, "status")) {
                /* unwind all blocked locks */
                __unwind_blocked_locks(pl_inode, &tmp_posixlk_list);
            }

            /* unlock metalk */
            /* if this list is empty then pl_inode->metalk_list
             * should be empty too. meta lock should in all cases
             * be added/removed from both pl_ctx_t and pl_inode */

            if (list_empty(&ctx->metalk_list))
                goto unlock;

            list_for_each_entry_safe(meta_lock, tmp_metalk, &ctx->metalk_list,
                                     client_list)
            {
                list_del_init(&meta_lock->client_list);

                pl_inode = meta_lock->pl_inode;

                list_del_init(&meta_lock->list);

                pl_metalk_unref(meta_lock);

                /* The corresponding ref is taken in
                 * pl_insert_metalk*/
                inode_unref(pl_inode->inode);
            }

            if (dict_get_sizen(dict, "status"))
                pl_inode->migrated = _gf_true;
            else
                pl_inode->migrated = _gf_false;
        }
    unlock:

        pthread_mutex_unlock(&pl_inode->mutex);
    }
    pthread_mutex_unlock(&ctx->lock);

out:
    list_for_each_entry_safe(posix_lock, tmp_posixlk, &tmp_posixlk_list, list)
    {
        list_del_init(&posix_lock->list);

        STACK_UNWIND_STRICT(lk, posix_lock->frame, -1, EREMOTE,
                            &posix_lock->user_flock, NULL);

        __destroy_lock(posix_lock);
    }

    return ret;
}

int32_t
pl_setxattr_cbk(call_frame_t *frame, void *cookie, xlator_t *this,
                int32_t op_ret, int32_t op_errno, dict_t *xdata)
{
    pl_local_t *local = NULL;
    pl_inode_t *pl_inode = NULL;
    local = frame->local;
    if (local && local->update_mlock_enforced_flag && op_ret != -1) {
        pl_inode = pl_inode_get(this, local->inode, NULL);
        if (!pl_inode) {
            op_ret = -1;
            op_errno = ENOMEM;
            goto unwind;
        }

        pthread_mutex_lock(&pl_inode->mutex);
        {
            while (pl_inode->fop_wind_count > 0) {
                gf_msg(this->name, GF_LOG_INFO, 0, 0,
                       "waiting for existing fops (count %d) to drain for "
                       "gfid %s",
                       pl_inode->fop_wind_count, uuid_utoa(pl_inode->gfid));
                pthread_cond_wait(&pl_inode->check_fop_wind_count,
                                  &pl_inode->mutex);
            }
            pl_inode->mlock_enforced = _gf_true;
            pl_inode->check_mlock_info = _gf_false;
        }
        pthread_mutex_unlock(&pl_inode->mutex);
    }

unwind:
    PL_STACK_UNWIND_FOR_CLIENT(setxattr, xdata, frame, op_ret, op_errno, xdata);
    return 0;
}

int32_t
pl_setxattr(call_frame_t *frame, xlator_t *this, loc_t *loc, dict_t *dict,
            int flags, dict_t *xdata)
{
    int op_ret = 0;
    int op_errno = EINVAL;
    dict_t *xdata_rsp = NULL;
    char *name = NULL;
    posix_locks_private_t *priv = this->private;

    PL_LOCAL_GET_REQUESTS(frame, this, xdata, ((fd_t *)NULL), loc, NULL);

    if (dict_get_sizen(dict, GF_META_LOCK_KEY)) {
        op_ret = pl_metalk(frame, this, loc->inode);

    } else if (dict_get_sizen(dict, GF_META_UNLOCK_KEY)) {
        op_ret = pl_metaunlock(frame, this, loc->inode, dict);
    } else {
        goto usual;
    }

    PL_STACK_UNWIND_FOR_CLIENT(setxattr, xdata_rsp, frame, op_ret, op_errno,
                               xdata_rsp);
    return 0;

usual:
    PL_CHECK_LOCK_ENFORCE_KEY(frame, dict, name, this, loc, ((fd_t *)NULL),
                              priv);

    STACK_WIND(frame, pl_setxattr_cbk, FIRST_CHILD(this),
               FIRST_CHILD(this)->fops->setxattr, loc, dict, flags, xdata);
    return 0;

unwind:
    PL_STACK_UNWIND_FOR_CLIENT(setxattr, xdata, frame, op_ret, op_errno, xdata);

    return 0;
}

void
pl_dump_lock(char *str, int size, struct gf_flock *flock, gf_lkowner_t *owner,
             void *trans, char *conn_id, time_t *granted_time,
             time_t *blkd_time, gf_boolean_t active)
{
    char *type_str = NULL;
    char granted[GF_TIMESTR_SIZE] = {
        0,
    };
    char blocked[GF_TIMESTR_SIZE] = {
        0,
    };

    if (granted_time)
        gf_time_fmt(granted, sizeof(granted), *granted_time, gf_timefmt_FT);
    if (blkd_time)
        gf_time_fmt(blocked, sizeof(blocked), *blkd_time, gf_timefmt_FT);
    switch (flock->l_type) {
        case F_RDLCK:
            type_str = "READ";
            break;
        case F_WRLCK:
            type_str = "WRITE";
            break;
        case F_UNLCK:
            type_str = "UNLOCK";
            break;
        default:
            type_str = "UNKNOWN";
            break;
    }

    if (active) {
        if (blkd_time && *blkd_time == 0) {
            snprintf(str, size, RANGE_GRNTD_FMT, type_str, flock->l_whence,
                     (unsigned long long)flock->l_start,
                     (unsigned long long)flock->l_len,
                     (unsigned long long)flock->l_pid, lkowner_utoa(owner),
                     trans, conn_id, granted);
        } else {
            snprintf(str, size, RANGE_BLKD_GRNTD_FMT, type_str, flock->l_whence,
                     (unsigned long long)flock->l_start,
                     (unsigned long long)flock->l_len,
                     (unsigned long long)flock->l_pid, lkowner_utoa(owner),
                     trans, conn_id, blocked, granted);
        }
    } else {
        snprintf(str, size, RANGE_BLKD_FMT, type_str, flock->l_whence,
                 (unsigned long long)flock->l_start,
                 (unsigned long long)flock->l_len,
                 (unsigned long long)flock->l_pid, lkowner_utoa(owner), trans,
                 conn_id, blocked);
    }
}

void
__dump_entrylks(pl_inode_t *pl_inode)
{
    pl_dom_list_t *dom = NULL;
    pl_entry_lock_t *lock = NULL;
    char blocked[GF_TIMESTR_SIZE] = {
        0,
    };
    char granted[GF_TIMESTR_SIZE] = {
        0,
    };
    int count = 0;
    char key[GF_DUMP_MAX_BUF_LEN] = {
        0,
    };
    char *k = "xlator.feature.locks.lock-dump.domain.entrylk";

    char tmp[4098];

    list_for_each_entry(dom, &pl_inode->dom_list, inode_list)
    {
        count = 0;

        gf_proc_dump_build_key(key, "lock-dump.domain", "domain");
        gf_proc_dump_write(key, "%s", dom->domain);

        list_for_each_entry(lock, &dom->entrylk_list, domain_list)
        {
            gf_time_fmt(granted, sizeof(granted), lock->granted_time,
                        gf_timefmt_FT);
            gf_proc_dump_build_key(key, k, "entrylk[%d](ACTIVE)", count);
            if (lock->blkd_time == 0) {
                snprintf(tmp, sizeof(tmp), ENTRY_GRNTD_FMT,
                         lock->type == ENTRYLK_RDLCK ? "ENTRYLK_RDLCK"
                                                     : "ENTRYLK_WRLCK",
                         lock->basename, (unsigned long long)lock->client_pid,
                         lkowner_utoa(&lock->owner), lock->client,
                         lock->connection_id, granted);
            } else {
                gf_time_fmt(blocked, sizeof(blocked), lock->blkd_time,
                            gf_timefmt_FT);
                snprintf(tmp, sizeof(tmp), ENTRY_BLKD_GRNTD_FMT,
                         lock->type == ENTRYLK_RDLCK ? "ENTRYLK_RDLCK"
                                                     : "ENTRYLK_WRLCK",
                         lock->basename, (unsigned long long)lock->client_pid,
                         lkowner_utoa(&lock->owner), lock->client,
                         lock->connection_id, blocked, granted);
            }

            gf_proc_dump_write(key, "%s", tmp);

            count++;
        }

        list_for_each_entry(lock, &dom->blocked_entrylks, blocked_locks)
        {
            gf_time_fmt(blocked, sizeof(blocked), lock->blkd_time,
                        gf_timefmt_FT);

            gf_proc_dump_build_key(key, k, "entrylk[%d](BLOCKED)", count);
            snprintf(
                tmp, sizeof(tmp), ENTRY_BLKD_FMT,
                lock->type == ENTRYLK_RDLCK ? "ENTRYLK_RDLCK" : "ENTRYLK_WRLCK",
                lock->basename, (unsigned long long)lock->client_pid,
                lkowner_utoa(&lock->owner), lock->client, lock->connection_id,
                blocked);

            gf_proc_dump_write(key, "%s", tmp);

            count++;
        }
    }
}

void
dump_entrylks(pl_inode_t *pl_inode)
{
    pthread_mutex_lock(&pl_inode->mutex);
    {
        __dump_entrylks(pl_inode);
    }
    pthread_mutex_unlock(&pl_inode->mutex);
}

void
__dump_inodelks(pl_inode_t *pl_inode)
{
    pl_dom_list_t *dom = NULL;
    pl_inode_lock_t *lock = NULL;
    int count = 0;
    char key[GF_DUMP_MAX_BUF_LEN];

    char tmp[4098];

    list_for_each_entry(dom, &pl_inode->dom_list, inode_list)
    {
        count = 0;

        gf_proc_dump_build_key(key, "lock-dump.domain", "domain");
        gf_proc_dump_write(key, "%s", dom->domain);

        list_for_each_entry(lock, &dom->inodelk_list, list)
        {
            gf_proc_dump_build_key(key, "inodelk", "inodelk[%d](ACTIVE)",
                                   count);

            SET_FLOCK_PID(&lock->user_flock, lock);
            pl_dump_lock(tmp, sizeof(tmp), &lock->user_flock, &lock->owner,
                         lock->client, lock->connection_id, &lock->granted_time,
                         &lock->blkd_time, _gf_true);
            gf_proc_dump_write(key, "%s", tmp);

            count++;
        }

        list_for_each_entry(lock, &dom->blocked_inodelks, blocked_locks)
        {
            gf_proc_dump_build_key(key, "inodelk", "inodelk[%d](BLOCKED)",
                                   count);
            SET_FLOCK_PID(&lock->user_flock, lock);
            pl_dump_lock(tmp, sizeof(tmp), &lock->user_flock, &lock->owner,
                         lock->client, lock->connection_id, 0, &lock->blkd_time,
                         _gf_false);
            gf_proc_dump_write(key, "%s", tmp);

            count++;
        }
    }
}

void
dump_inodelks(pl_inode_t *pl_inode)
{
    pthread_mutex_lock(&pl_inode->mutex);
    {
        __dump_inodelks(pl_inode);
    }
    pthread_mutex_unlock(&pl_inode->mutex);
}

void
__dump_posixlks(pl_inode_t *pl_inode)
{
    posix_lock_t *lock = NULL;
    int count = 0;
    char key[GF_DUMP_MAX_BUF_LEN];

    char tmp[4098];

    list_for_each_entry(lock, &pl_inode->ext_list, list)
    {
        SET_FLOCK_PID(&lock->user_flock, lock);
        gf_proc_dump_build_key(key, "posixlk", "posixlk[%d](%s)", count,
                               lock->blocked ? "BLOCKED" : "ACTIVE");
        pl_dump_lock(tmp, sizeof(tmp), &lock->user_flock, &lock->owner,
                     lock->client, lock->client_uid, &lock->granted_time,
                     &lock->blkd_time, (lock->blocked) ? _gf_false : _gf_true);
        gf_proc_dump_write(key, "%s", tmp);

        count++;
    }
}

void
dump_posixlks(pl_inode_t *pl_inode)
{
    pthread_mutex_lock(&pl_inode->mutex);
    {
        __dump_posixlks(pl_inode);
    }
    pthread_mutex_unlock(&pl_inode->mutex);
}

int32_t
pl_dump_inode_priv(xlator_t *this, inode_t *inode)
{
    int ret = -1;
    uint64_t tmp_pl_inode = 0;
    pl_inode_t *pl_inode = NULL;
    char *pathname = NULL;
    gf_boolean_t section_added = _gf_false;

    int count = 0;

    if (!inode) {
        errno = EINVAL;
        goto out;
    }

    ret = TRY_LOCK(&inode->lock);
    if (ret)
        goto out;
    {
        ret = __inode_ctx_get(inode, this, &tmp_pl_inode);
        if (ret)
            goto unlock;
    }
unlock:
    UNLOCK(&inode->lock);
    if (ret)
        goto out;

    pl_inode = (pl_inode_t *)(long)tmp_pl_inode;
    if (!pl_inode) {
        ret = -1;
        goto out;
    }

    gf_proc_dump_add_section("xlator.features.locks.%s.inode", this->name);
    section_added = _gf_true;

    /*We are safe to call __inode_path since we have the
     * inode->table->lock */
    __inode_path(inode, NULL, &pathname);
    if (pathname)
        gf_proc_dump_write("path", "%s", pathname);

    gf_proc_dump_write("mandatory", "%d", pl_inode->mandatory);

    ret = pthread_mutex_trylock(&pl_inode->mutex);
    if (ret)
        goto out;
    {
        count = __get_entrylk_count(this, pl_inode);
        if (count) {
            gf_proc_dump_write("entrylk-count", "%d", count);
            __dump_entrylks(pl_inode);
        }

        count = __get_inodelk_count(this, pl_inode, NULL);
        if (count) {
            gf_proc_dump_write("inodelk-count", "%d", count);
            __dump_inodelks(pl_inode);
        }

        count = __get_posixlk_count(pl_inode);
        if (count) {
            gf_proc_dump_write("posixlk-count", "%d", count);
            __dump_posixlks(pl_inode);
        }

        gf_proc_dump_write("links", "%d", pl_inode->links);
        gf_proc_dump_write("removes_pending", "%u", pl_inode->remove_running);
        gf_proc_dump_write("removed", "%u", pl_inode->removed);
    }
    pthread_mutex_unlock(&pl_inode->mutex);

out:
    GF_FREE(pathname);

    if (ret && inode) {
        if (!section_added)
            gf_proc_dump_add_section(
                "xlator.features.locks.%s."
                "inode",
                this->name);
        gf_proc_dump_write("Unable to print lock state",
                           "(Lock "
                           "acquisition failure) %s",
                           uuid_utoa(inode->gfid));
    }
    return ret;
}

int32_t
mem_acct_init(xlator_t *this)
{
    int ret = -1;

    if (!this)
        return ret;

    ret = xlator_mem_acct_init(this, gf_locks_mt_end + 1);

    if (ret != 0) {
        gf_log(this->name, GF_LOG_ERROR,
               "Memory accounting init"
               "failed");
        return ret;
    }

    return ret;
}

pl_ctx_t *
pl_ctx_get(client_t *client, xlator_t *xlator)
{
    void *tmp = NULL;
    pl_ctx_t *ctx = NULL;
    pl_ctx_t *setted_ctx = NULL;

    client_ctx_get(client, xlator, &tmp);

    ctx = tmp;

    if (ctx != NULL)
        goto out;

    ctx = GF_CALLOC(1, sizeof(pl_ctx_t), gf_locks_mt_posix_lock_t);

    if (ctx == NULL)
        goto out;

    pthread_mutex_init(&ctx->lock, NULL);
    INIT_LIST_HEAD(&ctx->inodelk_lockers);
    INIT_LIST_HEAD(&ctx->entrylk_lockers);
    INIT_LIST_HEAD(&ctx->metalk_list);

    setted_ctx = client_ctx_set(client, xlator, ctx);
    if (ctx != setted_ctx) {
        pthread_mutex_destroy(&ctx->lock);
        GF_FREE(ctx);
        ctx = setted_ctx;
    }
out:
    return ctx;
}

int
pl_metalk_client_cleanup(xlator_t *this, pl_ctx_t *ctx)
{
    pl_meta_lock_t *meta_lock = NULL;
    pl_meta_lock_t *tmp_metalk = NULL;
    pl_inode_t *pl_inode = NULL;
    posix_lock_t *posix_lock = NULL;
    posix_lock_t *tmp_posixlk = NULL;
    struct list_head tmp_posixlk_list;

    INIT_LIST_HEAD(&tmp_posixlk_list);

    pthread_mutex_lock(&ctx->lock);
    {
        /* if this list is empty then pl_inode->metalk_list should be
         * empty too. meta lock should in all cases be added/removed
         * from both pl_ctx_t and pl_inode */
        if (list_empty(&ctx->metalk_list))
            goto unlock;

        list_for_each_entry_safe(meta_lock, tmp_metalk, &ctx->metalk_list,
                                 client_list)
        {
            list_del_init(&meta_lock->client_list);

            pl_inode = meta_lock->pl_inode;

            pthread_mutex_lock(&pl_inode->mutex);

            {
                /* Since the migration status is unknown here
                 * unwind all queued and blocked locks to check
                 * migration status and find the correct
                 * destination */
                __unwind_queued_locks(pl_inode, &tmp_posixlk_list);

                __unwind_blocked_locks(pl_inode, &tmp_posixlk_list);

                list_del_init(&meta_lock->list);

                pl_metalk_unref(meta_lock);
            }
            pthread_mutex_unlock(&pl_inode->mutex);

            /* The corresponding ref is taken in
             * pl_insert_metalk*/
            inode_unref(pl_inode->inode);
        }
    }

unlock:
    pthread_mutex_unlock(&ctx->lock);

    list_for_each_entry_safe(posix_lock, tmp_posixlk, &tmp_posixlk_list, list)
    {
        list_del_init(&posix_lock->list);

        STACK_UNWIND_STRICT(lk, posix_lock->frame, -1, EREMOTE,
                            &posix_lock->user_flock, NULL);

        __destroy_lock(posix_lock);
    }
    return 0;
}

static int
pl_client_disconnect_cbk(xlator_t *this, client_t *client)
{
    pl_ctx_t *pl_ctx = pl_ctx_get(client, this);
    if (pl_ctx) {
        pl_inodelk_client_cleanup(this, pl_ctx);
        pl_entrylk_client_cleanup(this, pl_ctx);
        pl_metalk_client_cleanup(this, pl_ctx);
    }

    return 0;
}

static int
pl_client_destroy_cbk(xlator_t *this, client_t *client)
{
    void *tmp = NULL;
    pl_ctx_t *pl_ctx = NULL;

    pl_client_disconnect_cbk(this, client);

    client_ctx_del(client, this, &tmp);

    if (tmp == NULL)
        return 0;

    pl_ctx = tmp;

    GF_ASSERT(list_empty(&pl_ctx->inodelk_lockers));
    GF_ASSERT(list_empty(&pl_ctx->entrylk_lockers));

    pthread_mutex_destroy(&pl_ctx->lock);
    GF_FREE(pl_ctx);

    return 0;
}

int
reconfigure(xlator_t *this, dict_t *options)
{
    posix_locks_private_t *priv = this->private;
    int ret = -1;
    char *tmp_str = NULL;

    GF_OPTION_RECONF("trace", priv->trace, options, bool, out);

    GF_OPTION_RECONF("monkey-unlocking", priv->monkey_unlocking, options, bool,
                     out);

    GF_OPTION_RECONF("revocation-secs", priv->revocation_secs, options, uint32,
                     out);

    GF_OPTION_RECONF("revocation-clear-all", priv->revocation_clear_all,
                     options, bool, out);

    GF_OPTION_RECONF("revocation-max-blocked", priv->revocation_max_blocked,
                     options, uint32, out);

    GF_OPTION_RECONF("notify-contention", priv->notify_contention, options,
                     bool, out);

    GF_OPTION_RECONF("notify-contention-delay", priv->notify_contention_delay,
                     options, uint32, out);

    GF_OPTION_RECONF("mandatory-locking", tmp_str, options, str, out);

    GF_OPTION_RECONF("enforce-mandatory-lock", priv->mlock_enforced, options,
                     bool, out);

    if (!strcmp(tmp_str, "forced"))
        priv->mandatory_mode = MLK_FORCED;
    else if (!strcmp(tmp_str, "file"))
        priv->mandatory_mode = MLK_FILE_BASED;
    else if (!strcmp(tmp_str, "optimal"))
        priv->mandatory_mode = MLK_OPTIMAL;
    else
        priv->mandatory_mode = MLK_NONE;

    ret = 0;

out:
    return ret;
}

int
init(xlator_t *this)
{
    posix_locks_private_t *priv = NULL;
    xlator_list_t *trav = NULL;
    char *tmp_str = NULL;
    int ret = -1;

    if (!this->children || this->children->next) {
        gf_log(this->name, GF_LOG_CRITICAL,
               "FATAL: posix-locks should have exactly one child");
        goto out;
    }

    if (!this->parents) {
        gf_log(this->name, GF_LOG_WARNING,
               "Volume is dangling. Please check the volume file.");
    }

    trav = this->children;
    while (trav->xlator->children)
        trav = trav->xlator->children;

    if (strncmp("storage/", trav->xlator->type, 8)) {
        gf_log(this->name, GF_LOG_CRITICAL,
               "'locks' translator is not loaded over a storage "
               "translator");
        goto out;
    }

    priv = GF_CALLOC(1, sizeof(*priv), gf_locks_mt_posix_locks_private_t);

    GF_OPTION_INIT("mandatory-locking", tmp_str, str, out);
    if (!strcmp(tmp_str, "forced"))
        priv->mandatory_mode = MLK_FORCED;
    else if (!strcmp(tmp_str, "file"))
        priv->mandatory_mode = MLK_FILE_BASED;
    else if (!strcmp(tmp_str, "optimal"))
        priv->mandatory_mode = MLK_OPTIMAL;
    else
        priv->mandatory_mode = MLK_NONE;

    tmp_str = NULL;

    GF_OPTION_INIT("trace", priv->trace, bool, out);

    GF_OPTION_INIT("monkey-unlocking", priv->monkey_unlocking, bool, out);

    GF_OPTION_INIT("revocation-secs", priv->revocation_secs, uint32, out);

    GF_OPTION_INIT("revocation-clear-all", priv->revocation_clear_all, bool,
                   out);

    GF_OPTION_INIT("revocation-max-blocked", priv->revocation_max_blocked,
                   uint32, out);

    GF_OPTION_INIT("notify-contention", priv->notify_contention, bool, out);

    GF_OPTION_INIT("notify-contention-delay", priv->notify_contention_delay,
                   uint32, out);

    GF_OPTION_INIT("enforce-mandatory-lock", priv->mlock_enforced, bool, out);

    this->local_pool = mem_pool_new(pl_local_t, 32);
    if (!this->local_pool) {
        ret = -1;
        gf_log(this->name, GF_LOG_ERROR,
               "failed to create local_t's memory pool");
        goto out;
    }

    this->private = priv;
    ret = 0;

out:
    if (ret) {
        GF_FREE(priv);
    }
    return ret;
}

void
fini(xlator_t *this)
{
    posix_locks_private_t *priv = this->private;
    if (!priv)
        return;
    this->private = NULL;
    if (this->local_pool) {
        mem_pool_destroy(this->local_pool);
        this->local_pool = NULL;
    }
    GF_FREE(priv->brickname);
    GF_FREE(priv);

    return;
}

int
pl_inodelk(call_frame_t *frame, xlator_t *this, const char *volume, loc_t *loc,
           int32_t cmd, struct gf_flock *flock, dict_t *xdata);

int
pl_finodelk(call_frame_t *frame, xlator_t *this, const char *volume, fd_t *fd,
            int32_t cmd, struct gf_flock *flock, dict_t *xdata);

int
pl_entrylk(call_frame_t *frame, xlator_t *this, const char *volume, loc_t *loc,
           const char *basename, entrylk_cmd cmd, entrylk_type type,
           dict_t *xdata);

int
pl_fentrylk(call_frame_t *frame, xlator_t *this, const char *volume, fd_t *fd,
            const char *basename, entrylk_cmd cmd, entrylk_type type,
            dict_t *xdata);

int32_t
pl_rename_cbk(call_frame_t *frame, void *cookie, xlator_t *this, int32_t op_ret,
              int32_t op_errno, struct iatt *buf, struct iatt *preoldparent,
              struct iatt *postoldparent, struct iatt *prenewparent,
              struct iatt *postnewparent, dict_t *xdata)
{
    pl_inode_remove_cbk(this, cookie, op_ret < 0 ? op_errno : 0);

    PL_STACK_UNWIND(rename, xdata, frame, op_ret, op_errno, buf, preoldparent,
                    postoldparent, prenewparent, postnewparent, xdata);

    return 0;
}

int32_t
pl_rename(call_frame_t *frame, xlator_t *this, loc_t *oldloc, loc_t *newloc,
          dict_t *xdata)
{
    int32_t error;

    error = PL_INODE_REMOVE(rename, frame, this, oldloc, newloc, pl_rename,
                            pl_rename_cbk, oldloc, newloc, xdata);
    if (error > 0) {
        STACK_UNWIND_STRICT(rename, frame, -1, error, NULL, NULL, NULL, NULL,
                            NULL, NULL);
    }

    return 0;
}

posix_lock_t *
gf_lkmig_info_to_posix_lock(call_frame_t *frame, lock_migration_info_t *lmi)
{
    posix_lock_t *lock = GF_CALLOC(1, sizeof(posix_lock_t),
                                   gf_locks_mt_posix_lock_t);
    if (!lock)
        goto out;

    lock->fl_start = lmi->flock.l_start;
    lock->fl_type = lmi->flock.l_type;

    if (lmi->flock.l_len == 0)
        lock->fl_end = LLONG_MAX;
    else
        lock->fl_end = lmi->flock.l_start + lmi->flock.l_len - 1;

    lock->client = frame->root->client;

    lock->lk_flags = lmi->lk_flags;

    lock->client_uid = gf_strdup(lmi->client_uid);
    if (lock->client_uid == NULL) {
        GF_FREE(lock);
        lock = NULL;
        goto out;
    }

    lock->client_pid = lmi->flock.l_pid;
    lock->owner = lmi->flock.l_owner;

    INIT_LIST_HEAD(&lock->list);

out:
    return lock;
}

/* This function is supposed to write the active locks from the source brick(in
 * rebalance context) and write here. Hence, will add the locks directly to the
 * pl_inode->ext_list*/
int
pl_write_active_locks(call_frame_t *frame, pl_inode_t *pl_inode,
                      lock_migration_info_t *locklist)
{
    posix_lock_t *newlock = NULL;
    lock_migration_info_t *temp = NULL;
    int ret = 0;

    pthread_mutex_lock(&pl_inode->mutex);
    {
        /* Just making sure the activelk list is empty. Should not
         * happen though*/
        if (!list_empty(&pl_inode->ext_list)) {
            pthread_mutex_unlock(&pl_inode->mutex);
            gf_msg(THIS->name, GF_LOG_ERROR, 0, 0, "invalid locks found");

            ret = -1;
            goto out;
        }

        /* This list also should not be empty */
        if (list_empty(&locklist->list)) {
            pthread_mutex_unlock(&pl_inode->mutex);
            gf_msg(THIS->name, GF_LOG_ERROR, 0, 0, "empty lock list");

            ret = -1;
            goto out;
        }

        list_for_each_entry(temp, &locklist->list, list)
        {
            newlock = gf_lkmig_info_to_posix_lock(frame, temp);
            if (!newlock) {
                pthread_mutex_unlock(&pl_inode->mutex);
                gf_msg(THIS->name, GF_LOG_ERROR, 0, 0,
                       "mem allocation failed for newlock");

                ret = -1;
                goto out;
            }
            list_add_tail(&newlock->list, &pl_inode->ext_list);
        }
    }
    /*TODO: What if few lock add failed with ENOMEM. Should the already
     *      added locks be clearted */
    pthread_mutex_unlock(&pl_inode->mutex);
out:
    return ret;
}

static int
pl_setactivelk(call_frame_t *frame, xlator_t *this, loc_t *loc,
               lock_migration_info_t *locklist, dict_t *xdata)
{
    int op_ret = 0;
    int op_errno = 0;
    int ret = 0;

    pl_inode_t *pl_inode = pl_inode_get(this, loc->inode, NULL);
    if (!pl_inode) {
        gf_msg(this->name, GF_LOG_ERROR, 0, 0, "pl_inode_get failed");

        op_ret = -1;
        op_errno = ENOMEM;
        goto out;
    }
    ret = pl_write_active_locks(frame, pl_inode, locklist);

    op_ret = ret;

out:
    STACK_UNWIND_STRICT(setactivelk, frame, op_ret, op_errno, NULL);

    return 0;
}

int32_t
pl_unlink_cbk(call_frame_t *frame, void *cookie, xlator_t *this, int32_t op_ret,
              int32_t op_errno, struct iatt *preparent, struct iatt *postparent,
              dict_t *xdata)
{
    pl_inode_remove_cbk(this, cookie, op_ret < 0 ? op_errno : 0);

    PL_STACK_UNWIND(unlink, xdata, frame, op_ret, op_errno, preparent,
                    postparent, xdata);

    return 0;
}

int32_t
pl_unlink(call_frame_t *frame, xlator_t *this, loc_t *loc, int xflag,
          dict_t *xdata)
{
    int32_t error;

    error = PL_INODE_REMOVE(unlink, frame, this, loc, NULL, pl_unlink,
                            pl_unlink_cbk, loc, xflag, xdata);
    if (error > 0) {
        STACK_UNWIND_STRICT(unlink, frame, -1, error, NULL, NULL, NULL);
    }

    return 0;
}

int32_t
pl_mkdir_cbk(call_frame_t *frame, void *cookie, xlator_t *this, int32_t op_ret,
             int32_t op_errno, inode_t *inode, struct iatt *buf,
             struct iatt *preparent, struct iatt *postparent, dict_t *xdata)
{
    PL_STACK_UNWIND_FOR_CLIENT(mkdir, xdata, frame, op_ret, op_errno, inode,
                               buf, preparent, postparent, xdata);
    return 0;
}

int
pl_mkdir(call_frame_t *frame, xlator_t *this, loc_t *loc, mode_t mode,
         mode_t umask, dict_t *xdata)
{
    PL_LOCAL_GET_REQUESTS(frame, this, xdata, ((fd_t *)NULL), loc, NULL);
    STACK_WIND(frame, pl_mkdir_cbk, FIRST_CHILD(this),
               FIRST_CHILD(this)->fops->mkdir, loc, mode, umask, xdata);
    return 0;
}

int32_t
pl_stat_cbk(call_frame_t *frame, void *cookie, xlator_t *this, int32_t op_ret,
            int32_t op_errno, struct iatt *buf, dict_t *xdata)
{
    PL_STACK_UNWIND_FOR_CLIENT(stat, xdata, frame, op_ret, op_errno, buf,
                               xdata);
    return 0;
}

int
pl_stat(call_frame_t *frame, xlator_t *this, loc_t *loc, dict_t *xdata)
{
    PL_LOCAL_GET_REQUESTS(frame, this, xdata, ((fd_t *)NULL), loc, NULL);
    STACK_WIND(frame, pl_stat_cbk, FIRST_CHILD(this),
               FIRST_CHILD(this)->fops->stat, loc, xdata);
    return 0;
}

int32_t
pl_mknod_cbk(call_frame_t *frame, void *cookie, xlator_t *this, int32_t op_ret,
             int32_t op_errno, inode_t *inode, struct iatt *buf,
             struct iatt *preparent, struct iatt *postparent, dict_t *xdata)
{
    PL_STACK_UNWIND_FOR_CLIENT(mknod, xdata, frame, op_ret, op_errno, inode,
                               buf, preparent, postparent, xdata);
    return 0;
}

int
pl_mknod(call_frame_t *frame, xlator_t *this, loc_t *loc, mode_t mode,
         dev_t rdev, mode_t umask, dict_t *xdata)
{
    PL_LOCAL_GET_REQUESTS(frame, this, xdata, ((fd_t *)NULL), loc, NULL);
    STACK_WIND(frame, pl_mknod_cbk, FIRST_CHILD(this),
               FIRST_CHILD(this)->fops->mknod, loc, mode, rdev, umask, xdata);
    return 0;
}

int32_t
pl_rmdir_cbk(call_frame_t *frame, void *cookie, xlator_t *this, int32_t op_ret,
             int32_t op_errno, struct iatt *preparent, struct iatt *postparent,
             dict_t *xdata)
{
    pl_inode_remove_cbk(this, cookie, op_ret < 0 ? op_errno : 0);

    PL_STACK_UNWIND_FOR_CLIENT(rmdir, xdata, frame, op_ret, op_errno, preparent,
                               postparent, xdata);

    return 0;
}

int
pl_rmdir(call_frame_t *frame, xlator_t *this, loc_t *loc, int xflags,
         dict_t *xdata)
{
    int32_t error;

    error = PL_INODE_REMOVE(rmdir, frame, this, loc, NULL, pl_rmdir,
                            pl_rmdir_cbk, loc, xflags, xdata);
    if (error > 0) {
        STACK_UNWIND_STRICT(rmdir, frame, -1, error, NULL, NULL, NULL);
    }

    return 0;
}

int32_t
pl_symlink_cbk(call_frame_t *frame, void *cookie, xlator_t *this,
               int32_t op_ret, int32_t op_errno, inode_t *inode,
               struct iatt *buf, struct iatt *preparent,
               struct iatt *postparent, dict_t *xdata)
{
    PL_STACK_UNWIND_FOR_CLIENT(symlink, xdata, frame, op_ret, op_errno, inode,
                               buf, preparent, postparent, xdata);
    return 0;
}

int
pl_symlink(call_frame_t *frame, xlator_t *this, const char *linkname,
           loc_t *loc, mode_t umask, dict_t *xdata)
{
    PL_LOCAL_GET_REQUESTS(frame, this, xdata, ((fd_t *)NULL), loc, NULL);
    STACK_WIND(frame, pl_symlink_cbk, FIRST_CHILD(this),
               FIRST_CHILD(this)->fops->symlink, linkname, loc, umask, xdata);
    return 0;
}

int32_t
pl_link_cbk(call_frame_t *frame, void *cookie, xlator_t *this, int32_t op_ret,
            int32_t op_errno, inode_t *inode, struct iatt *buf,
            struct iatt *preparent, struct iatt *postparent, dict_t *xdata)
{
    pl_inode_t *pl_inode = (pl_inode_t *)cookie;

    if (op_ret >= 0) {
        pthread_mutex_lock(&pl_inode->mutex);

        /* TODO: can happen pl_inode->links == 0 ? */
        if (pl_inode->links >= 0) {
            pl_inode->links++;
        }

        pthread_mutex_unlock(&pl_inode->mutex);
    }

    PL_STACK_UNWIND_FOR_CLIENT(link, xdata, frame, op_ret, op_errno, inode, buf,
                               preparent, postparent, xdata);
    return 0;
}

int
pl_link(call_frame_t *frame, xlator_t *this, loc_t *oldloc, loc_t *newloc,
        dict_t *xdata)
{
    pl_inode_t *pl_inode;

    pl_inode = pl_inode_get(this, oldloc->inode, NULL);
    if (pl_inode == NULL) {
        STACK_UNWIND_STRICT(link, frame, -1, ENOMEM, NULL, NULL, NULL, NULL,
                            NULL);
        return 0;
    }

    PL_LOCAL_GET_REQUESTS(frame, this, xdata, ((fd_t *)NULL), oldloc, newloc);
    STACK_WIND_COOKIE(frame, pl_link_cbk, pl_inode, FIRST_CHILD(this),
                      FIRST_CHILD(this)->fops->link, oldloc, newloc, xdata);
    return 0;
}

int32_t
pl_fsync_cbk(call_frame_t *frame, void *cookie, xlator_t *this, int32_t op_ret,
             int32_t op_errno, struct iatt *prebuf, struct iatt *postbuf,
             dict_t *xdata)
{
    PL_STACK_UNWIND_FOR_CLIENT(fsync, xdata, frame, op_ret, op_errno, prebuf,
                               postbuf, xdata);
    return 0;
}

int
pl_fsync(call_frame_t *frame, xlator_t *this, fd_t *fd, int32_t datasync,
         dict_t *xdata)
{
    PL_LOCAL_GET_REQUESTS(frame, this, xdata, fd, NULL, NULL);
    STACK_WIND(frame, pl_fsync_cbk, FIRST_CHILD(this),
               FIRST_CHILD(this)->fops->fsync, fd, datasync, xdata);
    return 0;
}

int32_t
pl_readdir_cbk(call_frame_t *frame, void *cookie, xlator_t *this,
               int32_t op_ret, int32_t op_errno, gf_dirent_t *entries,
               dict_t *xdata)
{
    PL_STACK_UNWIND_FOR_CLIENT(readdir, xdata, frame, op_ret, op_errno, entries,
                               xdata);
    return 0;
}

int
pl_readdir(call_frame_t *frame, xlator_t *this, fd_t *fd, size_t size,
           off_t offset, dict_t *xdata)
{
    PL_LOCAL_GET_REQUESTS(frame, this, xdata, fd, NULL, NULL);
    STACK_WIND(frame, pl_readdir_cbk, FIRST_CHILD(this),
               FIRST_CHILD(this)->fops->readdir, fd, size, offset, xdata);
    return 0;
}

int32_t
pl_fsyncdir_cbk(call_frame_t *frame, void *cookie, xlator_t *this,
                int32_t op_ret, int32_t op_errno, dict_t *xdata)
{
    PL_STACK_UNWIND_FOR_CLIENT(fsyncdir, xdata, frame, op_ret, op_errno, xdata);
    return 0;
}

int
pl_fsyncdir(call_frame_t *frame, xlator_t *this, fd_t *fd, int32_t datasync,
            dict_t *xdata)
{
    PL_LOCAL_GET_REQUESTS(frame, this, xdata, fd, NULL, NULL);
    STACK_WIND(frame, pl_fsyncdir_cbk, FIRST_CHILD(this),
               FIRST_CHILD(this)->fops->fsyncdir, fd, datasync, xdata);
    return 0;
}

int32_t
pl_statfs_cbk(call_frame_t *frame, void *cookie, xlator_t *this, int32_t op_ret,
              int32_t op_errno, struct statvfs *buf, dict_t *xdata)
{
    PL_STACK_UNWIND_FOR_CLIENT(statfs, xdata, frame, op_ret, op_errno, buf,
                               xdata);
    return 0;
}

int
pl_statfs(call_frame_t *frame, xlator_t *this, loc_t *loc, dict_t *xdata)
{
    PL_LOCAL_GET_REQUESTS(frame, this, xdata, ((fd_t *)NULL), loc, NULL);
    STACK_WIND(frame, pl_statfs_cbk, FIRST_CHILD(this),
               FIRST_CHILD(this)->fops->statfs, loc, xdata);
    return 0;
}

int32_t
pl_removexattr_cbk(call_frame_t *frame, void *cookie, xlator_t *this,
                   int32_t op_ret, int32_t op_errno, dict_t *xdata)
{
    pl_local_t *local = NULL;
    pl_inode_t *pl_inode = NULL;

    local = frame->local;
    if (local && local->update_mlock_enforced_flag && op_ret != -1) {
        pl_inode = pl_inode_get(this, local->inode, NULL);
        if (!pl_inode) {
            op_ret = -1;
            op_errno = ENOMEM;
            goto unwind;
        }

        pthread_mutex_lock(&pl_inode->mutex);
        {
            pl_inode->mlock_enforced = _gf_false;
            pl_inode->check_mlock_info = _gf_false;
            pl_inode->track_fop_wind_count = _gf_true;
        }
        pthread_mutex_unlock(&pl_inode->mutex);
    }

unwind:
    PL_STACK_UNWIND_FOR_CLIENT(removexattr, xdata, frame, op_ret, op_errno,
                               xdata);
    return 0;
}

int
pl_removexattr(call_frame_t *frame, xlator_t *this, loc_t *loc,
               const char *name, dict_t *xdata)
{
    int op_ret = 0;
    int op_errno = EINVAL;
    posix_locks_private_t *priv = this->private;

    PL_LOCAL_GET_REQUESTS(frame, this, xdata, ((fd_t *)NULL), loc, NULL);

    PL_CHECK_LOCK_ENFORCE_KEY(frame, ((dict_t *)NULL), name, this, loc,
                              ((fd_t *)NULL), priv);

    STACK_WIND(frame, pl_removexattr_cbk, FIRST_CHILD(this),
               FIRST_CHILD(this)->fops->removexattr, loc, name, xdata);
    return 0;

unwind:
    PL_STACK_UNWIND_FOR_CLIENT(removexattr, xdata, frame, op_ret, op_errno,
                               NULL);

    return 0;
}

int32_t
pl_fremovexattr_cbk(call_frame_t *frame, void *cookie, xlator_t *this,
                    int32_t op_ret, int32_t op_errno, dict_t *xdata)
{
    pl_local_t *local = NULL;
    pl_inode_t *pl_inode = NULL;

    local = frame->local;
    if (local && local->update_mlock_enforced_flag && op_ret != -1) {
        pl_inode = pl_inode_get(this, local->inode, NULL);
        if (!pl_inode) {
            op_ret = -1;
            op_errno = ENOMEM;
            goto unwind;
        }

        pthread_mutex_lock(&pl_inode->mutex);
        {
            pl_inode->mlock_enforced = _gf_false;
            pl_inode->check_mlock_info = _gf_false;
        }
        pthread_mutex_unlock(&pl_inode->mutex);
    }

unwind:
    PL_STACK_UNWIND_FOR_CLIENT(fremovexattr, xdata, frame, op_ret, op_errno,
                               xdata);
    return 0;
}

int
pl_fremovexattr(call_frame_t *frame, xlator_t *this, fd_t *fd, const char *name,
                dict_t *xdata)
{
    int op_ret = -1;
    int op_errno = EINVAL;
    posix_locks_private_t *priv = this->private;

    PL_LOCAL_GET_REQUESTS(frame, this, xdata, fd, NULL, NULL);

    PL_CHECK_LOCK_ENFORCE_KEY(frame, ((dict_t *)NULL), name, this,
                              ((loc_t *)NULL), fd, priv);

    STACK_WIND(frame, pl_fremovexattr_cbk, FIRST_CHILD(this),
               FIRST_CHILD(this)->fops->fremovexattr, fd, name, xdata);
    return 0;

unwind:
    PL_STACK_UNWIND_FOR_CLIENT(fremovexattr, xdata, frame, op_ret, op_errno,
                               NULL);
    return 0;
}

int32_t
pl_rchecksum_cbk(call_frame_t *frame, void *cookie, xlator_t *this,
                 int32_t op_ret, int32_t op_errno, uint32_t weak_cksum,
                 uint8_t *strong_cksum, dict_t *xdata)
{
    PL_STACK_UNWIND_FOR_CLIENT(rchecksum, xdata, frame, op_ret, op_errno,
                               weak_cksum, strong_cksum, xdata);
    return 0;
}

int
pl_rchecksum(call_frame_t *frame, xlator_t *this, fd_t *fd, off_t offset,
             int32_t len, dict_t *xdata)
{
    PL_LOCAL_GET_REQUESTS(frame, this, xdata, fd, NULL, NULL);
    STACK_WIND(frame, pl_rchecksum_cbk, FIRST_CHILD(this),
               FIRST_CHILD(this)->fops->rchecksum, fd, offset, len, xdata);
    return 0;
}

int32_t
pl_xattrop_cbk(call_frame_t *frame, void *cookie, xlator_t *this,
               int32_t op_ret, int32_t op_errno, dict_t *dict, dict_t *xdata)
{
    PL_STACK_UNWIND_FOR_CLIENT(xattrop, xdata, frame, op_ret, op_errno, dict,
                               xdata);
    return 0;
}

int
pl_xattrop(call_frame_t *frame, xlator_t *this, loc_t *loc,
           gf_xattrop_flags_t optype, dict_t *xattr, dict_t *xdata)
{
    PL_LOCAL_GET_REQUESTS(frame, this, xdata, ((fd_t *)NULL), loc, NULL);
    STACK_WIND(frame, pl_xattrop_cbk, FIRST_CHILD(this),
               FIRST_CHILD(this)->fops->xattrop, loc, optype, xattr, xdata);
    return 0;
}

int32_t
pl_fxattrop_cbk(call_frame_t *frame, void *cookie, xlator_t *this,
                int32_t op_ret, int32_t op_errno, dict_t *dict, dict_t *xdata)
{
    PL_STACK_UNWIND_FOR_CLIENT(fxattrop, xdata, frame, op_ret, op_errno, dict,
                               xdata);
    return 0;
}

int
pl_fxattrop(call_frame_t *frame, xlator_t *this, fd_t *fd,
            gf_xattrop_flags_t optype, dict_t *xattr, dict_t *xdata)
{
    PL_LOCAL_GET_REQUESTS(frame, this, xdata, fd, NULL, NULL);
    STACK_WIND(frame, pl_fxattrop_cbk, FIRST_CHILD(this),
               FIRST_CHILD(this)->fops->fxattrop, fd, optype, xattr, xdata);
    return 0;
}

int32_t
pl_setattr_cbk(call_frame_t *frame, void *cookie, xlator_t *this,
               int32_t op_ret, int32_t op_errno, struct iatt *statpre,
               struct iatt *statpost, dict_t *xdata)
{
    PL_STACK_UNWIND_FOR_CLIENT(setattr, xdata, frame, op_ret, op_errno, statpre,
                               statpost, xdata);
    return 0;
}

int
pl_setattr(call_frame_t *frame, xlator_t *this, loc_t *loc, struct iatt *stbuf,
           int32_t valid, dict_t *xdata)
{
    PL_LOCAL_GET_REQUESTS(frame, this, xdata, ((fd_t *)NULL), loc, NULL);
    STACK_WIND(frame, pl_setattr_cbk, FIRST_CHILD(this),
               FIRST_CHILD(this)->fops->setattr, loc, stbuf, valid, xdata);
    return 0;
}

int32_t
pl_fsetattr_cbk(call_frame_t *frame, void *cookie, xlator_t *this,
                int32_t op_ret, int32_t op_errno, struct iatt *statpre,
                struct iatt *statpost, dict_t *xdata)
{
    PL_STACK_UNWIND_FOR_CLIENT(fsetattr, xdata, frame, op_ret, op_errno,
                               statpre, statpost, xdata);
    return 0;
}

int
pl_fsetattr(call_frame_t *frame, xlator_t *this, fd_t *fd, struct iatt *stbuf,
            int32_t valid, dict_t *xdata)
{
    PL_LOCAL_GET_REQUESTS(frame, this, xdata, fd, NULL, NULL);
    STACK_WIND(frame, pl_fsetattr_cbk, FIRST_CHILD(this),
               FIRST_CHILD(this)->fops->fsetattr, fd, stbuf, valid, xdata);
    return 0;
}

int32_t
pl_fallocate_cbk(call_frame_t *frame, void *cookie, xlator_t *this,
                 int32_t op_ret, int32_t op_errno, struct iatt *pre,
                 struct iatt *post, dict_t *xdata)
{
    PL_STACK_UNWIND_FOR_CLIENT(fallocate, xdata, frame, op_ret, op_errno, pre,
                               post, xdata);
    return 0;
}

int
pl_fallocate(call_frame_t *frame, xlator_t *this, fd_t *fd, int32_t keep_size,
             off_t offset, size_t len, dict_t *xdata)
{
    PL_LOCAL_GET_REQUESTS(frame, this, xdata, fd, NULL, NULL);
    STACK_WIND(frame, pl_fallocate_cbk, FIRST_CHILD(this),
               FIRST_CHILD(this)->fops->fallocate, fd, keep_size, offset, len,
               xdata);
    return 0;
}

int32_t
pl_readlink_cbk(call_frame_t *frame, void *cookie, xlator_t *this,
                int32_t op_ret, int32_t op_errno, const char *path,
                struct iatt *buf, dict_t *xdata)
{
    PL_STACK_UNWIND_FOR_CLIENT(readlink, xdata, frame, op_ret, op_errno, path,
                               buf, xdata);
    return 0;
}

int
pl_readlink(call_frame_t *frame, xlator_t *this, loc_t *loc, size_t size,
            dict_t *xdata)
{
    PL_LOCAL_GET_REQUESTS(frame, this, xdata, ((fd_t *)NULL), loc, NULL);
    STACK_WIND(frame, pl_readlink_cbk, FIRST_CHILD(this),
               FIRST_CHILD(this)->fops->readlink, loc, size, xdata);
    return 0;
}

int32_t
pl_access_cbk(call_frame_t *frame, void *cookie, xlator_t *this, int32_t op_ret,
              int32_t op_errno, dict_t *xdata)
{
    PL_STACK_UNWIND_FOR_CLIENT(access, xdata, frame, op_ret, op_errno, xdata);
    return 0;
}

int
pl_access(call_frame_t *frame, xlator_t *this, loc_t *loc, int32_t mask,
          dict_t *xdata)
{
    PL_LOCAL_GET_REQUESTS(frame, this, xdata, ((fd_t *)NULL), loc, NULL);
    STACK_WIND(frame, pl_access_cbk, FIRST_CHILD(this),
               FIRST_CHILD(this)->fops->access, loc, mask, xdata);
    return 0;
}

int32_t
pl_seek_cbk(call_frame_t *frame, void *cookie, xlator_t *this, int32_t op_ret,
            int32_t op_errno, off_t offset, dict_t *xdata)
{
    PL_STACK_UNWIND_FOR_CLIENT(seek, xdata, frame, op_ret, op_errno, offset,
                               xdata);
    return 0;
}

int32_t
pl_seek(call_frame_t *frame, xlator_t *this, fd_t *fd, off_t offset,
        gf_seek_what_t what, dict_t *xdata)
{
    PL_LOCAL_GET_REQUESTS(frame, this, xdata, fd, NULL, NULL);
    STACK_WIND(frame, pl_seek_cbk, FIRST_CHILD(this),
               FIRST_CHILD(this)->fops->seek, fd, offset, what, xdata);
    return 0;
}

struct xlator_fops fops = {
    .lookup = pl_lookup,
    .create = pl_create,
    .fstat = pl_fstat,
    .truncate = pl_truncate,
    .ftruncate = pl_ftruncate,
    .discard = pl_discard,
    .zerofill = pl_zerofill,
    .open = pl_open,
    .readv = pl_readv,
    .writev = pl_writev,
    .lk = pl_lk,
    .inodelk = pl_inodelk,
    .finodelk = pl_finodelk,
    .entrylk = pl_entrylk,
    .fentrylk = pl_fentrylk,
    .flush = pl_flush,
    .opendir = pl_opendir,
    .readdirp = pl_readdirp,
    .setxattr = pl_setxattr,
    .fsetxattr = pl_fsetxattr,
    .getxattr = pl_getxattr,
    .fgetxattr = pl_fgetxattr,
    .removexattr = pl_removexattr,
    .fremovexattr = pl_fremovexattr,
    .rename = pl_rename,
    .getactivelk = pl_getactivelk,
    .setactivelk = pl_setactivelk,
    .unlink = pl_unlink,
    .access = pl_access,
    .readlink = pl_readlink,
    .fallocate = pl_fallocate,
    .fsetattr = pl_fsetattr,
    .setattr = pl_setattr,
    .fxattrop = pl_fxattrop,
    .xattrop = pl_xattrop,
    .rchecksum = pl_rchecksum,
    .statfs = pl_statfs,
    .fsyncdir = pl_fsyncdir,
    .readdir = pl_readdir,
    .symlink = pl_symlink,
    .link = pl_link,
    .rmdir = pl_rmdir,
    .mknod = pl_mknod,
    .stat = pl_stat,
    .seek = pl_seek,
};

struct xlator_dumpops dumpops = {
    .inodectx = pl_dump_inode_priv,
};

struct xlator_cbks cbks = {
    .forget = pl_forget,
    .release = pl_release,
    .releasedir = pl_releasedir,
    .client_destroy = pl_client_destroy_cbk,
    .client_disconnect = pl_client_disconnect_cbk,
};

struct volume_options options[] = {
    {.key = {"mandatory-locking"},
     .type = GF_OPTION_TYPE_STR,
     .default_value = "off",
     .op_version = {GD_OP_VERSION_3_8_0},
     .flags = OPT_FLAG_SETTABLE | OPT_FLAG_DOC,
     .tags = {"locks"},
     .description = "Specifies the mandatory-locking mode. Valid options "
                    "are 'file' to use linux style mandatory locks, "
                    "'forced' to use volume strictly under mandatory lock "
                    "semantics only and 'optimal' to treat advisory and "
                    "mandatory locks separately on their own."},
    {.key = {"trace"},
     .type = GF_OPTION_TYPE_BOOL,
     .default_value = "off",
     .op_version = {GD_OP_VERSION_3_7_0},
     .flags = OPT_FLAG_SETTABLE | OPT_FLAG_DOC,
     .tags = {"locks"},
     .description = "Trace the different lock requests "
                    "to logs."},
    {.key = {"monkey-unlocking"},
     .type = GF_OPTION_TYPE_BOOL,
     .default_value = "false",
     .op_version = {GD_OP_VERSION_3_9_0},
     .flags = OPT_FLAG_SETTABLE,
     .tags = {"locks"},
     .description = "Ignore a random number of unlock requests.  Useful "
                    "for testing/creating robust lock recovery mechanisms."},
    {
        .key = {"revocation-secs"},
        .type = GF_OPTION_TYPE_INT,
        .min = 0,
        .max = INT_MAX,
        .default_value = "0",
        .op_version = {GD_OP_VERSION_3_9_0},
        .flags = OPT_FLAG_SETTABLE | OPT_FLAG_DOC,
        .tags = {"locks"},
        .description = "Maximum time a lock can be taken out, before"
                       "being revoked.",
    },
    {
        .key = {"revocation-clear-all"},
        .type = GF_OPTION_TYPE_BOOL,
        .default_value = "false",
        .op_version = {GD_OP_VERSION_3_9_0},
        .flags = OPT_FLAG_SETTABLE | OPT_FLAG_DOC,
        .tags = {"locks"},
        .description = "If set to true, will revoke BOTH granted and blocked "
                       "(pending) lock requests if a revocation threshold is "
                       "hit.",
    },
    {.key = {"revocation-max-blocked"},
     .type = GF_OPTION_TYPE_INT,
     .min = 0,
     .max = INT_MAX,
     .default_value = "0",
     .op_version = {GD_OP_VERSION_3_9_0},
     .flags = OPT_FLAG_SETTABLE | OPT_FLAG_DOC,
     .tags = {"locks"},
     .description = "A number of blocked lock requests after which a lock "
                    "will be revoked to allow the others to proceed.  Can "
                    "be used in conjunction w/ revocation-clear-all."},
    {.key = {"notify-contention"},
     .type = GF_OPTION_TYPE_BOOL,
     .default_value = "yes",
     .flags = OPT_FLAG_SETTABLE | OPT_FLAG_DOC,
     .op_version = {GD_OP_VERSION_4_0_0},
     .tags = {"locks", "contention"},
     .description = "When this option is enabled and a lock request "
                    "conflicts with a currently granted lock, an upcall "
                    "notification will be sent to the current owner of "
                    "the lock to request it to be released as soon as "
                    "possible."},
    {.key = {"notify-contention-delay"},
     .type = GF_OPTION_TYPE_INT,
     .min = 0, /* An upcall notification is sent every time a conflict is
                * detected. */
     .max = 60,
     .default_value = "5",
     .flags = OPT_FLAG_SETTABLE | OPT_FLAG_DOC,
     .op_version = {GD_OP_VERSION_4_0_0},
     .tags = {"locks", "contention", "timeout"},
     .description = "This value determines the minimum amount of time "
                    "(in seconds) between upcall contention notifications "
                    "on the same inode. If multiple lock requests are "
                    "received during this period, only one upcall will "
                    "be sent."},
    {.key = {"enforce-mandatory-lock"},
     .type = GF_OPTION_TYPE_BOOL,
     .default_value = "off",
     .flags = OPT_FLAG_SETTABLE,
     .op_version = {GD_OP_VERSION_6_0},
     .description = "option to enable lock enforcement"},
    {.key = {NULL}},
};

xlator_api_t xlator_api = {
    .init = init,
    .fini = fini,
    .reconfigure = reconfigure,
    .mem_acct_init = mem_acct_init,
    .op_version = {1}, /* Present from the initial version */
    .dumpops = &dumpops,
    .fops = &fops,
    .cbks = &cbks,
    .options = options,
    .identifier = "locks",
    .category = GF_MAINTAINED,
};
