/*
  Copyright (c) 2008-2012 Red Hat, Inc. <http://www.redhat.com>
  This file is part of GlusterFS.

  This file is licensed to you under your choice of the GNU Lesser
  General Public License, version 3 or any later version (LGPLv3 or
  later), or the GNU General Public License, version 2 (GPLv2), in all
  cases as published by the Free Software Foundation.
*/

#include <math.h>
#include "quick-read.h"
#include <glusterfs/statedump.h>
#include "quick-read-messages.h"
#include <glusterfs/upcall-utils.h>
#include <glusterfs/atomic.h>

typedef struct qr_local {
    inode_t *inode;
    uint64_t incident_gen;
    fd_t *fd;
} qr_local_t;

qr_inode_t *
qr_inode_ctx_get(xlator_t *this, inode_t *inode);

void
__qr_inode_prune_data(xlator_t *this, qr_inode_table_t *table,
                      qr_inode_t *qr_inode);

void
qr_local_wipe(qr_local_t *local)
{
    if (!local)
        goto out;

    if (local->inode)
        inode_unref(local->inode);

    if (local->fd)
        fd_unref(local->fd);

    GF_FREE(local);
out:
    return;
}

uint64_t
__qr_get_generation(xlator_t *this, qr_inode_t *qr_inode)
{
    uint64_t gen = 0, rollover;
    qr_private_t *priv = NULL;
    qr_inode_table_t *table = NULL;

    priv = this->private;
    table = &priv->table;

    gen = GF_ATOMIC_INC(priv->generation);
    if (gen == 0) {
        qr_inode->gen_rollover = !qr_inode->gen_rollover;
        gen = GF_ATOMIC_INC(priv->generation);
        __qr_inode_prune_data(this, table, qr_inode);
        qr_inode->gen = qr_inode->invalidation_time = gen - 1;
    }

    rollover = qr_inode->gen_rollover;
    gen |= (rollover << 32);
    return gen;
}

uint64_t
qr_get_generation(xlator_t *this, inode_t *inode)
{
    qr_inode_t *qr_inode = NULL;
    uint64_t gen = 0;
    qr_inode_table_t *table = NULL;
    qr_private_t *priv = NULL;

    priv = this->private;
    table = &priv->table;

    qr_inode = qr_inode_ctx_get(this, inode);

    if (qr_inode) {
        LOCK(&table->lock);
        {
            gen = __qr_get_generation(this, qr_inode);
        }
        UNLOCK(&table->lock);
    } else {
        gen = GF_ATOMIC_INC(priv->generation);
        if (gen == 0) {
            gen = GF_ATOMIC_INC(priv->generation);
        }
    }

    return gen;
}

qr_local_t *
qr_local_get(xlator_t *this, inode_t *inode)
{
    qr_local_t *local = NULL;

    local = GF_CALLOC(1, sizeof(*local), gf_common_mt_char);
    if (!local)
        goto out;

    local->incident_gen = qr_get_generation(this, inode);
out:
    return local;
}

#define QR_STACK_UNWIND(fop, frame, params...)                                 \
    do {                                                                       \
        qr_local_t *__local = NULL;                                            \
        if (frame) {                                                           \
            __local = frame->local;                                            \
            frame->local = NULL;                                               \
        }                                                                      \
        STACK_UNWIND_STRICT(fop, frame, params);                               \
        qr_local_wipe(__local);                                                \
    } while (0)

void
__qr_inode_prune(xlator_t *this, qr_inode_table_t *table, qr_inode_t *qr_inode,
                 uint64_t gen);

int
__qr_inode_ctx_set(xlator_t *this, inode_t *inode, qr_inode_t *qr_inode)
{
    uint64_t value = 0;
    int ret = -1;

    value = (long)qr_inode;

    ret = __inode_ctx_set(inode, this, &value);

    return ret;
}

qr_inode_t *
__qr_inode_ctx_get(xlator_t *this, inode_t *inode)
{
    qr_inode_t *qr_inode = NULL;
    uint64_t value = 0;
    int ret = -1;

    ret = __inode_ctx_get(inode, this, &value);
    if (ret)
        return NULL;

    qr_inode = (void *)((long)value);

    return qr_inode;
}

qr_inode_t *
qr_inode_ctx_get(xlator_t *this, inode_t *inode)
{
    qr_inode_t *qr_inode = NULL;

    if (inode == NULL)
        goto out;

    LOCK(&inode->lock);
    {
        qr_inode = __qr_inode_ctx_get(this, inode);
    }
    UNLOCK(&inode->lock);

out:
    return qr_inode;
}

qr_inode_t *
qr_inode_new(xlator_t *this, inode_t *inode)
{
    qr_inode_t *qr_inode = NULL;

    qr_inode = GF_CALLOC(1, sizeof(*qr_inode), gf_qr_mt_qr_inode_t);
    if (!qr_inode)
        return NULL;

    INIT_LIST_HEAD(&qr_inode->lru);

    qr_inode->priority = 0; /* initial priority */

    return qr_inode;
}

qr_inode_t *
qr_inode_ctx_get_or_new(xlator_t *this, inode_t *inode)
{
    qr_inode_t *qr_inode = NULL;
    int ret = -1;
    qr_private_t *priv = NULL;

    priv = this->private;

    LOCK(&inode->lock);
    {
        qr_inode = __qr_inode_ctx_get(this, inode);
        if (qr_inode)
            goto unlock;

        qr_inode = qr_inode_new(this, inode);
        if (!qr_inode)
            goto unlock;

        ret = __qr_inode_ctx_set(this, inode, qr_inode);
        if (ret) {
            __qr_inode_prune(this, &priv->table, qr_inode, 0);
            GF_FREE(qr_inode);
            qr_inode = NULL;
        }
    }
unlock:
    UNLOCK(&inode->lock);

    return qr_inode;
}

uint32_t
qr_get_priority(qr_conf_t *conf, const char *path)
{
    uint32_t priority = 0;
    struct qr_priority *curr = NULL;

    list_for_each_entry(curr, &conf->priority_list, list)
    {
        if (fnmatch(curr->pattern, path, FNM_NOESCAPE) == 0)
            priority = curr->priority;
    }

    return priority;
}

void
__qr_inode_register(xlator_t *this, qr_inode_table_t *table,
                    qr_inode_t *qr_inode)
{
    qr_private_t *priv = NULL;

    if (!qr_inode->data)
        return;

    priv = this->private;
    if (!priv)
        return;

    if (list_empty(&qr_inode->lru))
        /* first time addition of this qr_inode into table */
        table->cache_used += qr_inode->size;
    else
        list_del_init(&qr_inode->lru);

    list_add_tail(&qr_inode->lru, &table->lru[qr_inode->priority]);

    GF_ATOMIC_INC(priv->qr_counter.files_cached);

    return;
}

void
qr_inode_set_priority(xlator_t *this, inode_t *inode, const char *path)
{
    uint32_t priority = 0;
    qr_inode_table_t *table = NULL;
    qr_inode_t *qr_inode = NULL;
    qr_private_t *priv = NULL;
    qr_conf_t *conf = NULL;

    qr_inode = qr_inode_ctx_get(this, inode);
    if (!qr_inode)
        return;

    priv = this->private;
    table = &priv->table;
    conf = &priv->conf;

    if (path)
        priority = qr_get_priority(conf, path);
    else
        /* retain existing priority, just bump LRU */
        priority = qr_inode->priority;

    LOCK(&table->lock);
    {
        qr_inode->priority = priority;

        __qr_inode_register(this, table, qr_inode);
    }
    UNLOCK(&table->lock);
}

void
__qr_inode_prune_data(xlator_t *this, qr_inode_table_t *table,
                      qr_inode_t *qr_inode)
{
    qr_private_t *priv = NULL;

    priv = this->private;

    GF_FREE(qr_inode->data);
    qr_inode->data = NULL;

    if (!list_empty(&qr_inode->lru)) {
        table->cache_used -= qr_inode->size;
        qr_inode->size = 0;

        list_del_init(&qr_inode->lru);

        GF_ATOMIC_DEC(priv->qr_counter.files_cached);
    }

    memset(&qr_inode->buf, 0, sizeof(qr_inode->buf));
}

/* To be called with priv->table.lock held */
void
__qr_inode_prune(xlator_t *this, qr_inode_table_t *table, qr_inode_t *qr_inode,
                 uint64_t gen)
{
    __qr_inode_prune_data(this, table, qr_inode);
    if (gen)
        qr_inode->gen = gen;
    qr_inode->invalidation_time = __qr_get_generation(this, qr_inode);
}

void
qr_inode_prune(xlator_t *this, inode_t *inode, uint64_t gen)
{
    qr_private_t *priv = NULL;
    qr_inode_table_t *table = NULL;
    qr_inode_t *qr_inode = NULL;

    qr_inode = qr_inode_ctx_get(this, inode);
    if (!qr_inode)
        return;

    priv = this->private;
    table = &priv->table;

    LOCK(&table->lock);
    {
        __qr_inode_prune(this, table, qr_inode, gen);
    }
    UNLOCK(&table->lock);
}

/* To be called with priv->table.lock held */
void
__qr_cache_prune(xlator_t *this, qr_inode_table_t *table, qr_conf_t *conf)
{
    qr_inode_t *curr = NULL;
    qr_inode_t *next = NULL;
    int index = 0;
    size_t size_pruned = 0;

    for (index = 0; index < conf->max_pri; index++) {
        list_for_each_entry_safe(curr, next, &table->lru[index], lru)
        {
            size_pruned += curr->size;

            __qr_inode_prune(this, table, curr, 0);

            if (table->cache_used < conf->cache_size)
                return;
        }
    }

    return;
}

void
qr_cache_prune(xlator_t *this)
{
    qr_private_t *priv = NULL;
    qr_conf_t *conf = NULL;
    qr_inode_table_t *table = NULL;

    priv = this->private;
    table = &priv->table;
    conf = &priv->conf;

    LOCK(&table->lock);
    {
        if (table->cache_used > conf->cache_size)
            __qr_cache_prune(this, table, conf);
    }
    UNLOCK(&table->lock);
}

void *
qr_content_extract(dict_t *xdata)
{
    data_t *data = NULL;
    void *content = NULL;
    int ret = 0;

    ret = dict_get_with_ref(xdata, GF_CONTENT_KEY, &data);
    if (ret < 0 || !data)
        return NULL;

    content = GF_MALLOC(data->len, gf_qr_mt_content_t);
    if (!content)
        goto out;

    memcpy(content, data->data, data->len);

out:
    data_unref(data);
    return content;
}

void
qr_content_update(xlator_t *this, qr_inode_t *qr_inode, void *data,
                  struct iatt *buf, uint64_t gen)
{
    qr_private_t *priv = NULL;
    qr_inode_table_t *table = NULL;
    uint32_t rollover = 0;

    rollover = gen >> 32;
    gen = gen & 0xffffffff;

    priv = this->private;
    table = &priv->table;

    LOCK(&table->lock);
    {
        if ((rollover != qr_inode->gen_rollover) ||
            (gen && qr_inode->gen && (qr_inode->gen >= gen)))
            goto unlock;

        if ((qr_inode->data == NULL) && (qr_inode->invalidation_time >= gen))
            goto unlock;

        __qr_inode_prune(this, table, qr_inode, gen);

        qr_inode->data = data;
        data = NULL;
        qr_inode->size = buf->ia_size;

        qr_inode->ia_mtime = buf->ia_mtime;
        qr_inode->ia_mtime_nsec = buf->ia_mtime_nsec;
        qr_inode->ia_ctime = buf->ia_ctime;
        qr_inode->ia_ctime_nsec = buf->ia_ctime_nsec;

        qr_inode->buf = *buf;
        qr_inode->last_refresh = gf_time();

        __qr_inode_register(this, table, qr_inode);
    }
unlock:
    UNLOCK(&table->lock);

    if (data)
        GF_FREE(data);

    qr_cache_prune(this);
}

gf_boolean_t
qr_size_fits(qr_conf_t *conf, struct iatt *buf)
{
    return (buf->ia_size <= conf->max_file_size);
}

gf_boolean_t
qr_mtime_equal(qr_inode_t *qr_inode, struct iatt *buf)
{
    return (qr_inode->ia_mtime == buf->ia_mtime &&
            qr_inode->ia_mtime_nsec == buf->ia_mtime_nsec);
}

gf_boolean_t
qr_ctime_equal(qr_inode_t *qr_inode, struct iatt *buf)
{
    return (qr_inode->ia_ctime == buf->ia_ctime &&
            qr_inode->ia_ctime_nsec == buf->ia_ctime_nsec);
}

gf_boolean_t
qr_time_equal(qr_conf_t *conf, qr_inode_t *qr_inode, struct iatt *buf)
{
    if (conf->ctime_invalidation)
        return qr_ctime_equal(qr_inode, buf);
    else
        return qr_mtime_equal(qr_inode, buf);
}

void
__qr_content_refresh(xlator_t *this, qr_inode_t *qr_inode, struct iatt *buf,
                     uint64_t gen)
{
    qr_private_t *priv = NULL;
    qr_inode_table_t *table = NULL;
    qr_conf_t *conf = NULL;
    uint32_t rollover = 0;

    rollover = gen >> 32;
    gen = gen & 0xffffffff;

    priv = this->private;
    table = &priv->table;
    conf = &priv->conf;

    /* allow for rollover of frame->root->unique */
    if ((rollover != qr_inode->gen_rollover) ||
        (gen && qr_inode->gen && (qr_inode->gen >= gen)))
        goto done;

    if ((qr_inode->data == NULL) && (qr_inode->invalidation_time >= gen))
        goto done;

    qr_inode->gen = gen;

    if (qr_size_fits(conf, buf) && qr_time_equal(conf, qr_inode, buf)) {
        qr_inode->buf = *buf;
        qr_inode->last_refresh = gf_time();
        __qr_inode_register(this, table, qr_inode);
    } else {
        __qr_inode_prune(this, table, qr_inode, gen);
    }

done:
    return;
}

void
qr_content_refresh(xlator_t *this, qr_inode_t *qr_inode, struct iatt *buf,
                   uint64_t gen)
{
    qr_private_t *priv = NULL;
    qr_inode_table_t *table = NULL;

    priv = this->private;
    table = &priv->table;

    LOCK(&table->lock);
    {
        __qr_content_refresh(this, qr_inode, buf, gen);
    }
    UNLOCK(&table->lock);
}

gf_boolean_t
__qr_cache_is_fresh(xlator_t *this, qr_inode_t *qr_inode)
{
    qr_conf_t *conf = NULL;
    qr_private_t *priv = NULL;

    priv = this->private;
    conf = &priv->conf;

    if (qr_inode->last_refresh < priv->last_child_down)
        return _gf_false;

    if (gf_time() - qr_inode->last_refresh >= conf->cache_timeout)
        return _gf_false;

    return _gf_true;
}

int
qr_lookup_cbk(call_frame_t *frame, void *cookie, xlator_t *this, int32_t op_ret,
              int32_t op_errno, inode_t *inode_ret, struct iatt *buf,
              dict_t *xdata, struct iatt *postparent)
{
    void *content = NULL;
    qr_inode_t *qr_inode = NULL;
    inode_t *inode = NULL;
    qr_local_t *local = NULL;

    local = frame->local;
    inode = local->inode;

    if (op_ret == -1) {
        qr_inode_prune(this, inode, local->incident_gen);
        goto out;
    }

    if (dict_get(xdata, GLUSTERFS_BAD_INODE)) {
        qr_inode_prune(this, inode, local->incident_gen);
        goto out;
    }

    if (dict_get(xdata, "sh-failed")) {
        qr_inode_prune(this, inode, local->incident_gen);
        goto out;
    }

    content = qr_content_extract(xdata);

    if (content) {
        /* new content came along, always replace old content */
        qr_inode = qr_inode_ctx_get_or_new(this, inode);
        if (!qr_inode) {
            /* no harm done */
            GF_FREE(content);
            goto out;
        }

        qr_content_update(this, qr_inode, content, buf, local->incident_gen);
    } else {
        /* purge old content if necessary */
        qr_inode = qr_inode_ctx_get(this, inode);
        if (!qr_inode)
            /* usual path for large files */
            goto out;

        qr_content_refresh(this, qr_inode, buf, local->incident_gen);
    }
out:
    QR_STACK_UNWIND(lookup, frame, op_ret, op_errno, inode_ret, buf, xdata,
                    postparent);
    return 0;
}

int
qr_lookup(call_frame_t *frame, xlator_t *this, loc_t *loc, dict_t *xdata)
{
    qr_private_t *priv = NULL;
    qr_conf_t *conf = NULL;
    qr_inode_t *qr_inode = NULL;
    int ret = -1;
    dict_t *new_xdata = NULL;
    qr_local_t *local = NULL;

    priv = this->private;
    conf = &priv->conf;
    local = qr_local_get(this, loc->inode);
    local->inode = inode_ref(loc->inode);
    frame->local = local;

    qr_inode = qr_inode_ctx_get(this, loc->inode);
    if (qr_inode && qr_inode->data)
        /* cached. only validate in qr_lookup_cbk */
        goto wind;

    if (!xdata)
        xdata = new_xdata = dict_new();

    if (!xdata)
        goto wind;

    ret = 0;
    if (conf->max_file_size)
        ret = dict_set(xdata, GF_CONTENT_KEY,
                       data_from_uint64(conf->max_file_size));
    if (ret)
        gf_msg(this->name, GF_LOG_WARNING, 0, QUICK_READ_MSG_DICT_SET_FAILED,
               "cannot set key in request dict (%s)", loc->path);
wind:
    STACK_WIND(frame, qr_lookup_cbk, FIRST_CHILD(this),
               FIRST_CHILD(this)->fops->lookup, loc, xdata);

    if (new_xdata)
        dict_unref(new_xdata);

    return 0;
}

int
qr_readdirp_cbk(call_frame_t *frame, void *cookie, xlator_t *this, int op_ret,
                int op_errno, gf_dirent_t *entries, dict_t *xdata)
{
    gf_dirent_t *entry = NULL;
    qr_inode_t *qr_inode = NULL;
    qr_local_t *local = NULL;

    local = frame->local;

    if (op_ret <= 0)
        goto unwind;

    list_for_each_entry(entry, &entries->list, list)
    {
        if (!entry->inode)
            continue;

        qr_inode = qr_inode_ctx_get(this, entry->inode);
        if (!qr_inode)
            /* no harm */
            continue;

        qr_content_refresh(this, qr_inode, &entry->d_stat, local->incident_gen);
    }

unwind:
    QR_STACK_UNWIND(readdirp, frame, op_ret, op_errno, entries, xdata);
    return 0;
}

int
qr_readdirp(call_frame_t *frame, xlator_t *this, fd_t *fd, size_t size,
            off_t offset, dict_t *xdata)
{
    qr_local_t *local = NULL;

    local = qr_local_get(this, NULL);
    frame->local = local;

    STACK_WIND(frame, qr_readdirp_cbk, FIRST_CHILD(this),
               FIRST_CHILD(this)->fops->readdirp, fd, size, offset, xdata);
    return 0;
}

int
qr_readv_cached(call_frame_t *frame, qr_inode_t *qr_inode, size_t size,
                off_t offset, uint32_t flags, dict_t *xdata)
{
    xlator_t *this = NULL;
    qr_private_t *priv = NULL;
    qr_inode_table_t *table = NULL;
    int op_ret = -1;
    struct iobuf *iobuf = NULL;
    struct iobref *iobref = NULL;
    struct iovec iov = {
        0,
    };
    struct iatt buf = {
        0,
    };

    this = frame->this;
    priv = this->private;
    table = &priv->table;

    LOCK(&table->lock);
    {
        if (!qr_inode->data)
            goto unlock;

        if (offset >= qr_inode->size)
            goto unlock;

        if (!__qr_cache_is_fresh(this, qr_inode))
            goto unlock;

        op_ret = min(size, (qr_inode->size - offset));

        iobuf = iobuf_get2(this->ctx->iobuf_pool, op_ret);
        if (!iobuf) {
            op_ret = -1;
            goto unlock;
        }

        iobref = iobref_new();
        if (!iobref) {
            op_ret = -1;
            goto unlock;
        }

        iobref_add(iobref, iobuf);

        memcpy(iobuf->ptr, qr_inode->data + offset, op_ret);

        buf = qr_inode->buf;

        /* bump LRU */
        __qr_inode_register(frame->this, table, qr_inode);
    }
unlock:
    UNLOCK(&table->lock);

    if (op_ret >= 0) {
        iov.iov_base = iobuf->ptr;
        iov.iov_len = op_ret;

        GF_ATOMIC_INC(priv->qr_counter.cache_hit);
        STACK_UNWIND_STRICT(readv, frame, op_ret, 0, &iov, 1, &buf, iobref,
                            xdata);
    } else {
        GF_ATOMIC_INC(priv->qr_counter.cache_miss);
    }

    if (iobuf)
        iobuf_unref(iobuf);

    if (iobref)
        iobref_unref(iobref);

    return op_ret;
}

int
qr_readv(call_frame_t *frame, xlator_t *this, fd_t *fd, size_t size,
         off_t offset, uint32_t flags, dict_t *xdata)
{
    qr_inode_t *qr_inode = NULL;

    qr_inode = qr_inode_ctx_get(this, fd->inode);
    if (!qr_inode)
        goto wind;

    if (qr_readv_cached(frame, qr_inode, size, offset, flags, xdata) < 0)
        goto wind;

    return 0;
wind:
    STACK_WIND(frame, default_readv_cbk, FIRST_CHILD(this),
               FIRST_CHILD(this)->fops->readv, fd, size, offset, flags, xdata);
    return 0;
}

int32_t
qr_writev_cbk(call_frame_t *frame, void *cookie, xlator_t *this, int32_t op_ret,
              int32_t op_errno, struct iatt *prebuf, struct iatt *postbuf,
              dict_t *xdata)
{
    qr_local_t *local = NULL;

    local = frame->local;

    qr_inode_prune(this, local->fd->inode, local->incident_gen);

    QR_STACK_UNWIND(writev, frame, op_ret, op_errno, prebuf, postbuf, xdata);
    return 0;
}

int
qr_writev(call_frame_t *frame, xlator_t *this, fd_t *fd, struct iovec *iov,
          int count, off_t offset, uint32_t flags, struct iobref *iobref,
          dict_t *xdata)
{
    qr_local_t *local = NULL;

    local = qr_local_get(this, fd->inode);
    local->fd = fd_ref(fd);

    frame->local = local;

    STACK_WIND(frame, qr_writev_cbk, FIRST_CHILD(this),
               FIRST_CHILD(this)->fops->writev, fd, iov, count, offset, flags,
               iobref, xdata);
    return 0;
}

int32_t
qr_truncate_cbk(call_frame_t *frame, void *cookie, xlator_t *this,
                int32_t op_ret, int32_t op_errno, struct iatt *prebuf,
                struct iatt *postbuf, dict_t *xdata)
{
    qr_local_t *local = NULL;

    local = frame->local;
    qr_inode_prune(this, local->inode, local->incident_gen);

    QR_STACK_UNWIND(truncate, frame, op_ret, op_errno, prebuf, postbuf, xdata);
    return 0;
}

int
qr_truncate(call_frame_t *frame, xlator_t *this, loc_t *loc, off_t offset,
            dict_t *xdata)
{
    qr_local_t *local = NULL;

    local = qr_local_get(this, loc->inode);
    local->inode = inode_ref(loc->inode);
    frame->local = local;

    STACK_WIND(frame, qr_truncate_cbk, FIRST_CHILD(this),
               FIRST_CHILD(this)->fops->truncate, loc, offset, xdata);
    return 0;
}

int32_t
qr_ftruncate_cbk(call_frame_t *frame, void *cookie, xlator_t *this,
                 int32_t op_ret, int32_t op_errno, struct iatt *prebuf,
                 struct iatt *postbuf, dict_t *xdata)
{
    qr_local_t *local = NULL;

    local = frame->local;
    qr_inode_prune(this, local->fd->inode, local->incident_gen);

    QR_STACK_UNWIND(ftruncate, frame, op_ret, op_errno, prebuf, postbuf, xdata);
    return 0;
}

int
qr_ftruncate(call_frame_t *frame, xlator_t *this, fd_t *fd, off_t offset,
             dict_t *xdata)
{
    qr_local_t *local = NULL;

    local = qr_local_get(this, fd->inode);
    local->fd = fd_ref(fd);
    frame->local = local;

    STACK_WIND(frame, qr_ftruncate_cbk, FIRST_CHILD(this),
               FIRST_CHILD(this)->fops->ftruncate, fd, offset, xdata);
    return 0;
}

int32_t
qr_fallocate_cbk(call_frame_t *frame, void *cookie, xlator_t *this,
                 int32_t op_ret, int32_t op_errno, struct iatt *pre,
                 struct iatt *post, dict_t *xdata)
{
    qr_local_t *local = NULL;

    local = frame->local;
    qr_inode_prune(this, local->fd->inode, local->incident_gen);

    QR_STACK_UNWIND(fallocate, frame, op_ret, op_errno, pre, post, xdata);
    return 0;
}

static int
qr_fallocate(call_frame_t *frame, xlator_t *this, fd_t *fd, int keep_size,
             off_t offset, size_t len, dict_t *xdata)
{
    qr_local_t *local = NULL;

    local = qr_local_get(this, fd->inode);
    local->fd = fd_ref(fd);
    frame->local = local;

    STACK_WIND(frame, qr_fallocate_cbk, FIRST_CHILD(this),
               FIRST_CHILD(this)->fops->fallocate, fd, keep_size, offset, len,
               xdata);
    return 0;
}

int32_t
qr_discard_cbk(call_frame_t *frame, void *cookie, xlator_t *this,
               int32_t op_ret, int32_t op_errno, struct iatt *pre,
               struct iatt *post, dict_t *xdata)
{
    qr_local_t *local = NULL;

    local = frame->local;
    qr_inode_prune(this, local->fd->inode, local->incident_gen);

    QR_STACK_UNWIND(discard, frame, op_ret, op_errno, pre, post, xdata);
    return 0;
}

static int
qr_discard(call_frame_t *frame, xlator_t *this, fd_t *fd, off_t offset,
           size_t len, dict_t *xdata)
{
    qr_local_t *local = NULL;

    local = qr_local_get(this, fd->inode);
    local->fd = fd_ref(fd);
    frame->local = local;

    STACK_WIND(frame, qr_discard_cbk, FIRST_CHILD(this),
               FIRST_CHILD(this)->fops->discard, fd, offset, len, xdata);
    return 0;
}

int32_t
qr_zerofill_cbk(call_frame_t *frame, void *cookie, xlator_t *this,
                int32_t op_ret, int32_t op_errno, struct iatt *pre,
                struct iatt *post, dict_t *xdata)
{
    qr_local_t *local = NULL;

    local = frame->local;
    qr_inode_prune(this, local->fd->inode, local->incident_gen);

    QR_STACK_UNWIND(zerofill, frame, op_ret, op_errno, pre, post, xdata);
    return 0;
}

static int
qr_zerofill(call_frame_t *frame, xlator_t *this, fd_t *fd, off_t offset,
            off_t len, dict_t *xdata)
{
    qr_local_t *local = NULL;

    local = qr_local_get(this, fd->inode);
    local->fd = fd_ref(fd);
    frame->local = local;

    STACK_WIND(frame, qr_zerofill_cbk, FIRST_CHILD(this),
               FIRST_CHILD(this)->fops->zerofill, fd, offset, len, xdata);
    return 0;
}

int
qr_open(call_frame_t *frame, xlator_t *this, loc_t *loc, int flags, fd_t *fd,
        dict_t *xdata)
{
    qr_inode_set_priority(this, fd->inode, loc->path);

    STACK_WIND(frame, default_open_cbk, FIRST_CHILD(this),
               FIRST_CHILD(this)->fops->open, loc, flags, fd, xdata);
    return 0;
}

int
qr_forget(xlator_t *this, inode_t *inode)
{
    qr_inode_t *qr_inode = NULL;

    qr_inode = qr_inode_ctx_get(this, inode);

    if (!qr_inode)
        return 0;

    qr_inode_prune(this, inode, qr_get_generation(this, inode));

    GF_FREE(qr_inode);

    return 0;
}

int32_t
qr_inodectx_dump(xlator_t *this, inode_t *inode)
{
    qr_inode_t *qr_inode = NULL;
    int32_t ret = -1;
    char key_prefix[GF_DUMP_MAX_BUF_LEN] = {
        0,
    };
    char buf[GF_TIMESTR_SIZE] = {
        0,
    };

    qr_inode = qr_inode_ctx_get(this, inode);
    if (!qr_inode)
        goto out;

    gf_proc_dump_build_key(key_prefix, "xlator.performance.quick-read",
                           "inodectx");
    gf_proc_dump_add_section("%s", key_prefix);

    gf_proc_dump_write("entire-file-cached", "%s",
                       qr_inode->data ? "yes" : "no");

    if (qr_inode->last_refresh) {
        gf_time_fmt(buf, sizeof buf, qr_inode->last_refresh, gf_timefmt_FT);
        gf_proc_dump_write("last-cache-validation-time", "%s", buf);
    }

    ret = 0;
out:
    return ret;
}

int
qr_priv_dump(xlator_t *this)
{
    qr_conf_t *conf = NULL;
    qr_private_t *priv = NULL;
    qr_inode_table_t *table = NULL;
    uint32_t file_count = 0;
    uint32_t i = 0;
    qr_inode_t *curr = NULL;
    uint64_t total_size = 0;
    char key_prefix[GF_DUMP_MAX_BUF_LEN];

    if (!this) {
        return -1;
    }

    priv = this->private;
    conf = &priv->conf;
    if (!conf)
        return -1;

    table = &priv->table;

    gf_proc_dump_build_key(key_prefix, "xlator.performance.quick-read", "priv");

    gf_proc_dump_add_section("%s", key_prefix);

    gf_proc_dump_write("max_file_size", "%" PRIu64, conf->max_file_size);
    gf_proc_dump_write("cache_timeout", "%d", conf->cache_timeout);

    if (!table) {
        goto out;
    } else {
        for (i = 0; i < conf->max_pri; i++) {
            list_for_each_entry(curr, &table->lru[i], lru)
            {
                file_count++;
                total_size += curr->size;
            }
        }
    }

    gf_proc_dump_write("total_files_cached", "%d", file_count);
    gf_proc_dump_write("total_cache_used", "%" PRIu64, total_size);
    gf_proc_dump_write("cache-hit", "%" GF_PRI_ATOMIC,
                       GF_ATOMIC_GET(priv->qr_counter.cache_hit));
    gf_proc_dump_write("cache-miss", "%" GF_PRI_ATOMIC,
                       GF_ATOMIC_GET(priv->qr_counter.cache_miss));
    gf_proc_dump_write("cache-invalidations", "%" GF_PRI_ATOMIC,
                       GF_ATOMIC_GET(priv->qr_counter.file_data_invals));

out:
    return 0;
}

static int32_t
qr_dump_metrics(xlator_t *this, int fd)
{
    qr_private_t *priv = NULL;
    qr_inode_table_t *table = NULL;

    priv = this->private;
    table = &priv->table;

    dprintf(fd, "%s.total_files_cached %" PRId64 "\n", this->name,
            GF_ATOMIC_GET(priv->qr_counter.files_cached));
    dprintf(fd, "%s.total_cache_used %" PRId64 "\n", this->name,
            table->cache_used);
    dprintf(fd, "%s.cache-hit %" PRId64 "\n", this->name,
            GF_ATOMIC_GET(priv->qr_counter.cache_hit));
    dprintf(fd, "%s.cache-miss %" PRId64 "\n", this->name,
            GF_ATOMIC_GET(priv->qr_counter.cache_miss));
    dprintf(fd, "%s.cache-invalidations %" PRId64 "\n", this->name,
            GF_ATOMIC_GET(priv->qr_counter.file_data_invals));

    return 0;
}

int32_t
qr_mem_acct_init(xlator_t *this)
{
    int ret = -1;

    if (!this)
        return ret;

    ret = xlator_mem_acct_init(this, gf_qr_mt_end);

    if (ret != 0) {
        gf_msg(this->name, GF_LOG_ERROR, ENOMEM, QUICK_READ_MSG_NO_MEMORY,
               "Memory accounting init failed");
        return ret;
    }

    return ret;
}

static gf_boolean_t
check_cache_size_ok(xlator_t *this, int64_t cache_size)
{
    int ret = _gf_true;
    uint64_t total_mem = 0;
    uint64_t max_cache_size = 0;
    volume_option_t *opt = NULL;

    GF_ASSERT(this);
    opt = xlator_volume_option_get(this, "cache-size");
    if (!opt) {
        ret = _gf_false;
        gf_msg(this->name, GF_LOG_ERROR, EINVAL,
               QUICK_READ_MSG_INVALID_ARGUMENT,
               "could not get cache-size option");
        goto out;
    }

    total_mem = get_mem_size();
    if (-1 == total_mem)
        max_cache_size = opt->max;
    else
        max_cache_size = total_mem;

    gf_msg_debug(this->name, 0, "Max cache size is %" PRIu64, max_cache_size);
    if (cache_size > max_cache_size) {
        ret = _gf_false;
        gf_msg(this->name, GF_LOG_ERROR, 0, QUICK_READ_MSG_INVALID_ARGUMENT,
               "Cache size %" PRIu64
               " is greater than the max size of %" PRIu64,
               cache_size, max_cache_size);
        goto out;
    }
out:
    return ret;
}

int
qr_reconfigure(xlator_t *this, dict_t *options)
{
    int32_t ret = -1;
    qr_private_t *priv = NULL;
    qr_conf_t *conf = NULL;
    uint64_t cache_size_new = 0;

    GF_VALIDATE_OR_GOTO("quick-read", this, out);
    GF_VALIDATE_OR_GOTO(this->name, this->private, out);
    GF_VALIDATE_OR_GOTO(this->name, options, out);

    priv = this->private;

    conf = &priv->conf;
    if (!conf) {
        goto out;
    }

    GF_OPTION_RECONF("cache-timeout", conf->cache_timeout, options, int32, out);

    GF_OPTION_RECONF("quick-read-cache-invalidation", conf->qr_invalidation,
                     options, bool, out);

    GF_OPTION_RECONF("ctime-invalidation", conf->ctime_invalidation, options,
                     bool, out);

    GF_OPTION_RECONF("cache-size", cache_size_new, options, size_uint64, out);
    if (!check_cache_size_ok(this, cache_size_new)) {
        ret = -1;
        gf_msg(this->name, GF_LOG_ERROR, EINVAL, QUICK_READ_MSG_INVALID_CONFIG,
               "Not reconfiguring cache-size");
        goto out;
    }
    conf->cache_size = cache_size_new;

    ret = 0;
out:
    return ret;
}

int32_t
qr_get_priority_list(const char *opt_str, struct list_head *first)
{
    int32_t max_pri = 1;
    char *tmp_str = NULL;
    char *tmp_str1 = NULL;
    char *tmp_str2 = NULL;
    char *dup_str = NULL;
    char *priority_str = NULL;
    char *pattern = NULL;
    char *priority = NULL;
    char *string = NULL;
    struct qr_priority *curr = NULL, *tmp = NULL;

    GF_VALIDATE_OR_GOTO("quick-read", opt_str, out);
    GF_VALIDATE_OR_GOTO("quick-read", first, out);

    string = gf_strdup(opt_str);
    if (string == NULL) {
        max_pri = -1;
        goto out;
    }

    /* Get the pattern for cache priority.
     * "option priority *.jpg:1,abc*:2" etc
     */
    /* TODO: inode_lru in table is statically hard-coded to 5,
     * should be changed to run-time configuration
     */
    priority_str = strtok_r(string, ",", &tmp_str);
    while (priority_str) {
        curr = GF_CALLOC(1, sizeof(*curr), gf_qr_mt_qr_priority_t);
        if (curr == NULL) {
            max_pri = -1;
            goto out;
        }

        list_add_tail(&curr->list, first);

        dup_str = gf_strdup(priority_str);
        if (dup_str == NULL) {
            max_pri = -1;
            goto out;
        }

        pattern = strtok_r(dup_str, ":", &tmp_str1);
        if (!pattern) {
            max_pri = -1;
            goto out;
        }

        priority = strtok_r(NULL, ":", &tmp_str1);
        if (!priority) {
            max_pri = -1;
            goto out;
        }

        gf_msg_trace("quick-read", 0,
                     "quick-read priority : pattern %s : priority %s", pattern,
                     priority);

        curr->pattern = gf_strdup(pattern);
        if (curr->pattern == NULL) {
            max_pri = -1;
            goto out;
        }

        curr->priority = strtol(priority, &tmp_str2, 0);
        if (tmp_str2 && (*tmp_str2)) {
            max_pri = -1;
            goto out;
        } else {
            max_pri = max(max_pri, curr->priority);
        }

        GF_FREE(dup_str);
        dup_str = NULL;

        priority_str = strtok_r(NULL, ",", &tmp_str);
    }
out:
    GF_FREE(string);

    GF_FREE(dup_str);

    if (max_pri == -1) {
        list_for_each_entry_safe(curr, tmp, first, list)
        {
            list_del_init(&curr->list);
            GF_FREE(curr->pattern);
            GF_FREE(curr);
        }
    }

    return max_pri;
}

int32_t
qr_init(xlator_t *this)
{
    int32_t ret = -1, i = 0;
    qr_private_t *priv = NULL;
    qr_conf_t *conf = NULL;

    if (!this->children || this->children->next) {
        gf_msg(this->name, GF_LOG_ERROR, 0,
               QUICK_READ_MSG_XLATOR_CHILD_MISCONFIGURED,
               "FATAL: volume (%s) not configured with exactly one "
               "child",
               this->name);
        return -1;
    }

    if (!this->parents) {
        gf_msg(this->name, GF_LOG_WARNING, 0, QUICK_READ_MSG_VOL_MISCONFIGURED,
               "dangling volume. check volfile ");
    }

    priv = GF_CALLOC(1, sizeof(*priv), gf_qr_mt_qr_private_t);
    if (priv == NULL) {
        ret = -1;
        goto out;
    }

    LOCK_INIT(&priv->table.lock);
    conf = &priv->conf;

    GF_OPTION_INIT("max-file-size", conf->max_file_size, size_uint64, out);

    GF_OPTION_INIT("cache-timeout", conf->cache_timeout, int32, out);

    GF_OPTION_INIT("quick-read-cache-invalidation", conf->qr_invalidation, bool,
                   out);

    GF_OPTION_INIT("cache-size", conf->cache_size, size_uint64, out);
    if (!check_cache_size_ok(this, conf->cache_size)) {
        ret = -1;
        goto out;
    }

    GF_OPTION_INIT("ctime-invalidation", conf->ctime_invalidation, bool, out);

    INIT_LIST_HEAD(&conf->priority_list);
    conf->max_pri = 1;
    if (dict_get(this->options, "priority")) {
        char *option_list = data_to_str(dict_get(this->options, "priority"));
        gf_msg_trace(this->name, 0, "option path %s", option_list);
        /* parse the list of pattern:priority */
        conf->max_pri = qr_get_priority_list(option_list, &conf->priority_list);

        if (conf->max_pri == -1) {
            goto out;
        }
        conf->max_pri++;
    }

    priv->table.lru = GF_CALLOC(conf->max_pri, sizeof(*priv->table.lru),
                                gf_common_mt_list_head);
    if (priv->table.lru == NULL) {
        ret = -1;
        goto out;
    }

    for (i = 0; i < conf->max_pri; i++) {
        INIT_LIST_HEAD(&priv->table.lru[i]);
    }

    ret = 0;

    priv->last_child_down = gf_time();
    GF_ATOMIC_INIT(priv->generation, 0);
    this->private = priv;
out:
    if ((ret == -1) && priv) {
        GF_FREE(priv);
    }

    return ret;
}

void
qr_inode_table_destroy(qr_private_t *priv)
{
    int i = 0;
    qr_conf_t *conf = NULL;

    conf = &priv->conf;

    for (i = 0; i < conf->max_pri; i++) {
        /* There is a known leak of inodes, hence until
         * that is fixed, log the assert as warning.
        GF_ASSERT (list_empty (&priv->table.lru[i]));*/
        if (!list_empty(&priv->table.lru[i])) {
            gf_msg("quick-read", GF_LOG_INFO, 0, QUICK_READ_MSG_LRU_NOT_EMPTY,
                   "quick read inode table lru not empty");
        }
    }

    LOCK_DESTROY(&priv->table.lock);

    return;
}

void
qr_conf_destroy(qr_conf_t *conf)
{
    struct qr_priority *curr = NULL, *tmp = NULL;

    list_for_each_entry_safe(curr, tmp, &conf->priority_list, list)
    {
        list_del(&curr->list);
        GF_FREE(curr->pattern);
        GF_FREE(curr);
    }

    return;
}

void
qr_update_child_down_time(xlator_t *this, time_t now)
{
    qr_private_t *priv = NULL;

    priv = this->private;

    LOCK(&priv->lock);
    {
        priv->last_child_down = now;
    }
    UNLOCK(&priv->lock);
}

static int
qr_invalidate(xlator_t *this, void *data)
{
    struct gf_upcall *up_data = NULL;
    struct gf_upcall_cache_invalidation *up_ci = NULL;
    inode_t *inode = NULL;
    int ret = 0;
    inode_table_t *itable = NULL;
    qr_private_t *priv = NULL;

    up_data = (struct gf_upcall *)data;

    if (up_data->event_type != GF_UPCALL_CACHE_INVALIDATION)
        goto out;

    priv = this->private;
    up_ci = (struct gf_upcall_cache_invalidation *)up_data->data;

    if (up_ci && (up_ci->flags & UP_WRITE_FLAGS)) {
        GF_ATOMIC_INC(priv->qr_counter.file_data_invals);
        itable = ((xlator_t *)this->graph->top)->itable;
        inode = inode_find(itable, up_data->gfid);
        if (!inode) {
            ret = -1;
            goto out;
        }
        qr_inode_prune(this, inode, qr_get_generation(this, inode));
    }

out:
    if (inode)
        inode_unref(inode);

    return ret;
}

int
qr_notify(xlator_t *this, int event, void *data, ...)
{
    int ret = 0;
    qr_private_t *priv = NULL;
    qr_conf_t *conf = NULL;

    priv = this->private;
    conf = &priv->conf;

    switch (event) {
        case GF_EVENT_CHILD_DOWN:
        case GF_EVENT_SOME_DESCENDENT_DOWN:
            qr_update_child_down_time(this, gf_time());
            break;
        case GF_EVENT_UPCALL:
            if (conf->qr_invalidation)
                ret = qr_invalidate(this, data);
            break;
        default:
            break;
    }

    if (default_notify(this, event, data) != 0)
        ret = -1;

    return ret;
}

void
qr_fini(xlator_t *this)
{
    qr_private_t *priv = NULL;

    if (this == NULL) {
        goto out;
    }

    priv = this->private;
    if (priv == NULL) {
        goto out;
    }

    qr_inode_table_destroy(priv);
    qr_conf_destroy(&priv->conf);

    this->private = NULL;

    GF_FREE(priv);
out:
    return;
}

struct xlator_fops qr_fops = {.lookup = qr_lookup,
                              .readdirp = qr_readdirp,
                              .open = qr_open,
                              .readv = qr_readv,
                              .writev = qr_writev,
                              .truncate = qr_truncate,
                              .ftruncate = qr_ftruncate,
                              .fallocate = qr_fallocate,
                              .discard = qr_discard,
                              .zerofill = qr_zerofill};

struct xlator_cbks qr_cbks = {
    .forget = qr_forget,
};

struct xlator_dumpops qr_dumpops = {
    .priv = qr_priv_dump,
    .inodectx = qr_inodectx_dump,
};

struct volume_options qr_options[] = {
    {
        .key = {"quick-read"},
        .type = GF_OPTION_TYPE_BOOL,
        .default_value = "off",
        .description = "enable/disable quick-read",
        .op_version = {GD_OP_VERSION_6_0},
        .flags = OPT_FLAG_SETTABLE,
    },
    {.key = {"priority"}, .type = GF_OPTION_TYPE_ANY},
    {.key = {"cache-size"},
     .type = GF_OPTION_TYPE_SIZET,
     .min = 0,
     .max = INFINITY,
     .default_value = "128MB",
     .op_version = {1},
     .flags = OPT_FLAG_CLIENT_OPT | OPT_FLAG_SETTABLE | OPT_FLAG_DOC,
     .description = "Size of small file read cache."},
    {
        .key = {"cache-timeout"},
        .type = GF_OPTION_TYPE_INT,
        .default_value = "1",
        .op_version = {1},
        .flags = OPT_FLAG_CLIENT_OPT | OPT_FLAG_SETTABLE | OPT_FLAG_DOC,
    },
    {
        .key = {"max-file-size"},
        .type = GF_OPTION_TYPE_SIZET,
        .min = 0,
        .max = 1 * GF_UNIT_KB * 1000,
        .default_value = "64KB",
        .op_version = {1},
        .flags = OPT_FLAG_CLIENT_OPT | OPT_FLAG_SETTABLE | OPT_FLAG_DOC,
    },
    {
        .key = {"quick-read-cache-invalidation"},
        .type = GF_OPTION_TYPE_BOOL,
        .default_value = "false",
        .op_version = {GD_OP_VERSION_4_0_0},
        .flags = OPT_FLAG_CLIENT_OPT | OPT_FLAG_SETTABLE | OPT_FLAG_DOC,
        .description = "When \"on\", invalidates/updates the metadata cache,"
                       " on receiving the cache-invalidation notifications",
    },
    {
        .key = {"ctime-invalidation"},
        .type = GF_OPTION_TYPE_BOOL,
        .default_value = "false",
        .op_version = {GD_OP_VERSION_5_0},
        .flags = OPT_FLAG_CLIENT_OPT | OPT_FLAG_SETTABLE | OPT_FLAG_DOC,
        .description = "Quick-read by default uses mtime to identify changes "
                       "to file data. However there are applications like "
                       "rsync which explicitly set mtime making it unreliable "
                       "for the purpose of identifying change in file content "
                       ". Since ctime also changes when content of a file "
                       " changes and it cannot be set explicitly, it becomes "
                       " suitable for identifying staleness of cached data. "
                       "This option makes quick-read to prefer ctime over "
                       "mtime to validate its cache. However, using ctime "
                       "can result in false positives as ctime changes with "
                       "just attribute changes like permission without "
                       "changes to file data. So, use this only when mtime "
                       "is not reliable",
    },
    {.key = {NULL}}};

xlator_api_t xlator_api = {
    .init = qr_init,
    .fini = qr_fini,
    .notify = qr_notify,
    .reconfigure = qr_reconfigure,
    .mem_acct_init = qr_mem_acct_init,
    .dump_metrics = qr_dump_metrics,
    .op_version = {1}, /* Present from the initial version */
    .dumpops = &qr_dumpops,
    .fops = &qr_fops,
    .cbks = &qr_cbks,
    .options = qr_options,
    .identifier = "quick-read",
    .category = GF_MAINTAINED,
};
