/*
  Copyright (c) 2008-2012 Red Hat, Inc. <http://www.redhat.com>
  This file is part of GlusterFS.

  This file is licensed to you under your choice of the GNU Lesser
  General Public License, version 3 or any later version (LGPLv3 or
  later), or the GNU General Public License, version 2 (GPLv2), in all
  cases as published by the Free Software Foundation.
*/
#include <glusterfs/call-stub.h>
#include <glusterfs/defaults.h>
#include <glusterfs/glusterfs.h>
#include <glusterfs/logging.h>
#include <glusterfs/dict.h>
#include <glusterfs/xlator.h>
#include "fs-cache.h"
#include "fsc-mem-types.h"
#include <signal.h>
#include <stdlib.h>
#include <sys/time.h>
#include <time.h>
#include <glusterfs/locking.h>
#include <glusterfs/timespec.h>

#define ALIGN_SIZE 4096

gf_boolean_t
fsc_inode_is_idle(fsc_inode_t *fsc_inode)
{
    gf_boolean_t is_idle = _gf_false;
    int64_t sec_elapsed = 0;
    struct timeval now = {
        0,
    };
    fsc_conf_t *conf = NULL;

    conf = fsc_inode->conf;

    gettimeofday(&now, NULL);

    sec_elapsed = now.tv_sec - fsc_inode->last_op_time.tv_sec;
    if (sec_elapsed >= conf->time_idle_inode)
        is_idle = _gf_true;

    return is_idle;
}

fsc_inode_t *
fsc_inode_create(xlator_t *this, inode_t *inode, char* path)
{
    int32_t local_path_len = 0;
    fsc_conf_t* priv = this->private;
    fsc_inode_t *fsc_inode = NULL;
    char* base_str = NULL;
    int32_t base_len = 0;
    char* base_cur = NULL;
    char* tmp = NULL;
    char  tmm_val = 0;
    fsc_inode = GF_CALLOC(1, sizeof(fsc_inode_t), gf_fsc_mt_fsc_inode_t);
    if (fsc_inode == NULL) {
        goto out;
    }
    fsc_inode->conf = priv;
    fsc_inode->inode = inode_ref(inode);


    if ( strncmp(path, "<gfid:", 6) == 0 ) {
        /*<gfid:c8fca0b4-f94c-490a-9b9b-0e7f9cb7f443>/file*/
        base_str = alloca(strlen(path) + 1);
        base_cur = base_str;
        tmp = path + 6;
        while ( (tmm_val = *tmp++) != 0 ) {
            if (tmm_val != '<' && tmm_val != '>') {
                *base_cur = tmm_val;
                base_cur++;
                base_len++;
            }
        }
        *base_cur = 0;

        local_path_len = strlen(priv->cache_dir) + base_len + 1;
        fsc_inode->local_path = GF_CALLOC(1, local_path_len + 1, gf_fsc_mt_fsc_path_t);
        local_path_len = snprintf(fsc_inode->local_path, local_path_len + 1, "%s/%s",
                                  priv->cache_dir, base_str);

    } else {
        local_path_len = strlen(priv->cache_dir) + strlen(path);
        fsc_inode->local_path = GF_CALLOC(1, local_path_len + 1, gf_fsc_mt_fsc_path_t);
        local_path_len = snprintf(fsc_inode->local_path, local_path_len + 1, "%s%s",
                                  priv->cache_dir, path);
    }

    gettimeofday(&fsc_inode->last_op_time, NULL);

    INIT_LIST_HEAD(&fsc_inode->inode_list);
    pthread_mutex_init(&fsc_inode->inode_lock, NULL);

    fsc_inodes_list_lock(priv);
    {
        priv->inodes_count++;
        list_add(&fsc_inode->inode_list, &priv->inodes);
    }
    fsc_inodes_list_unlock(priv);
    gf_msg(this->name, GF_LOG_INFO, 0, FS_CACHE_MSG_INFO,
           "adding to fsc=%p inode_list path=(%s),real_path=(%s)", fsc_inode, path, fsc_inode->local_path);
out:
    return fsc_inode;
}

void fsc_inode_destroy(fsc_inode_t *fsc_inode, int32_t tag)
{
    fsc_conf_t* conf = fsc_inode->conf;
    gf_msg(conf->this->name, GF_LOG_INFO, 0, FS_CACHE_MSG_INFO,
           "xlator=%p, destroy fsc tag=%d,fd=%d,path=%s", conf->this, tag, fsc_inode->fsc_fd, fsc_inode->local_path);

    inode_ctx_put(fsc_inode->inode, conf->this, (uint64_t)0);
    inode_ref(fsc_inode->inode);

    fsc_inode_lock(fsc_inode);
    {
        if (fsc_inode->fsc_fd) {
            close(fsc_inode->fsc_fd);
            fsc_inode->fsc_fd = 0;
        }
    }
    fsc_inode_unlock(fsc_inode);

    GF_FREE(fsc_inode->local_path);
    GF_FREE(fsc_inode->write_block);
    pthread_mutex_destroy(&fsc_inode->inode_lock);
    GF_FREE(fsc_inode);
}


int32_t
fsc_inode_update(xlator_t *this, inode_t *inode, char *path, struct iatt *iabuf)
{
    off_t old_ia_size = 0;
    uint64_t tmp_fsc_inode = 0;
    fsc_inode_t *fsc_inode = NULL;
    fsc_conf_t *conf = NULL;

    if (!this || !inode || !iabuf)
        goto out;

    conf = this->private;
    if (iabuf->ia_size < conf->min_file_size) {
        gf_msg(this->name, GF_LOG_TRACE, 0, FS_CACHE_MSG_TRACE,
               "ignore small file path=%s, %zu < %zu", path, iabuf->ia_size, conf->min_file_size);
        goto out;
    }


    if (!path) {
        gf_msg(this->name, GF_LOG_ERROR, 0, FS_CACHE_MSG_ERROR,
               "invalid path");
        goto out;
    }

    if (!fsc_check_filter(this->private, path)) {
        goto out;
    }

    LOCK(&inode->lock);
    {
        (void)__inode_ctx_get(inode, this, &tmp_fsc_inode);
        fsc_inode = (fsc_inode_t *)(long)tmp_fsc_inode;

        if (!fsc_inode) {
            fsc_inode = fsc_inode_create(this, inode, path);
            (void)__inode_ctx_put(inode, this, (uint64_t)(long)fsc_inode);
        }
    }
    UNLOCK(&inode->lock);

    fsc_inode_lock(fsc_inode);
    {
        old_ia_size = fsc_inode->ia_size;
        if (fsc_inode->s_mtime == 0) {
            fsc_inode->s_mtime = iabuf->ia_mtime;
            fsc_inode->s_mtime_nsec = iabuf->ia_mtime_nsec;
        }
        gettimeofday(&fsc_inode->last_op_time, NULL);
        fsc_inode->ia_size = iabuf->ia_size;
    }
    fsc_inode_unlock(fsc_inode);

    gf_msg(this->name, GF_LOG_TRACE, 0, FS_CACHE_MSG_TRACE,
           "fsc_inode fsc=%p update ia_size from %zu to %zu, path=%s, local_path=(%s),gfid=(%s)",
           fsc_inode, old_ia_size, fsc_inode->ia_size, path, fsc_inode->local_path, uuid_utoa(inode->gfid));
out:
    return 0;
}


int32_t
fsc_inode_open_for_read(xlator_t *this, fsc_inode_t *fsc_inode) {
    int32_t op_ret = -1;
    int32_t op_errno = 0;
    int32_t flag = O_RDWR;
    fsc_conf_t *conf = this->private;
    struct stat fstatbuf = {
        0,
    };
    gettimeofday(&fsc_inode->last_op_time, NULL);
    if (fsc_inode->fsc_fd > 0) {
        op_ret = 0;
        goto out;
    }

    if (conf->direct_io_read == 1) {
        flag |= O_DIRECT;
    }

    op_ret = sys_stat(fsc_inode->local_path, &fstatbuf);
    if (op_ret == -1) {
        gf_msg(this->name, GF_LOG_ERROR, 0, FS_CACHE_MSG_ERROR, "fsc_inode open for read not find path=(%s),gfid=(%s)",
               fsc_inode->local_path, uuid_utoa(fsc_inode->inode->gfid));
        goto out;
    }

    fsc_inode->fsc_fd = sys_open(fsc_inode->local_path, flag, S_IRWXU | S_IRWXG | S_IRWXO);
    if (fsc_inode->fsc_fd == -1) {
        op_ret = -1;
        op_errno = errno;
        gf_msg(this->name, GF_LOG_ERROR, errno, FS_CACHE_MSG_ERROR,
               "open on %s, flags: %d", fsc_inode->local_path, O_DIRECT | O_RDWR);
        goto out;
    }
    fsc_block_init(this, fsc_inode);
    /*fsc_inode->fsc_size = fstatbuf.st_size;*/
    gf_msg(this->name, GF_LOG_TRACE, 0, FS_CACHE_MSG_INFO, "fsc_inode open for read fd=%d,localsize=%zu,serversize=%zu path=(%s),gfid=(%s)",
           fsc_inode->fsc_fd, fsc_inode->fsc_size, fsc_inode->ia_size, fsc_inode->local_path, uuid_utoa(fsc_inode->inode->gfid));
out:
    return op_ret;
}


int32_t
fsc_inode_resovle_dir(xlator_t *this, char* file_full_path) {
    char tmp[512];
    char *p = NULL;
    size_t len;
    size_t base_len;
    fsc_conf_t* priv = this->private;

    snprintf(tmp, sizeof(tmp), "%s", file_full_path);
    len = strlen(tmp);
    base_len = strlen(priv->cache_dir);
    if (base_len >= len) {
        return -1;
    }

    if (tmp[len - 1] == '/')
        tmp[len - 1] = 0;

    for (p = tmp + base_len + 1; *p; p++) {
        if (*p == '/') {
            *p = 0;
            mkdir(tmp, 0755);
            *p = '/';
        }
    }
    /*mkdir(tmp, 0755);*/
    return 0;
}

int32_t
fsc_inode_open_for_write(xlator_t *this, fsc_inode_t *fsc_inode) {
    int32_t op_ret = -1;
    int32_t op_errno = 0;
    int32_t flag = O_RDWR | O_CREAT;
    fsc_conf_t *conf = this->private;
    struct stat fstatbuf = {
        0,
    };

    if (fsc_inode->fsc_fd > 0) {
        op_ret = 0;
        goto out;
    }
    gettimeofday(&fsc_inode->last_op_time, NULL);
    if (conf->direct_io_write == 1) {
        flag |= O_DIRECT;
    }
    fsc_inode_resovle_dir(this, fsc_inode->local_path);

    fsc_inode->fsc_fd = sys_open(fsc_inode->local_path, flag, S_IRWXU | S_IRWXG | S_IRWXO);
    if (fsc_inode->fsc_fd == -1) {
        op_ret = -1;
        op_errno = errno;
        gf_msg(this->name, GF_LOG_ERROR, errno, FS_CACHE_MSG_ERROR,
               "open on %s failed", fsc_inode->local_path);
        goto out;
    }
    op_ret = sys_fstat(fsc_inode->fsc_fd, &fstatbuf);
    if (op_ret == -1) {
        gf_msg(this->name, GF_LOG_ERROR, errno, FS_CACHE_MSG_ERROR,
               "fsc_inode open for fstat error ath=(%s),gfid=(%s)",
               fsc_inode->local_path, uuid_utoa(fsc_inode->inode->gfid));
        goto out;
    }
    fsc_block_init(this, fsc_inode);
    /*fsc_inode->fsc_size = fstatbuf.st_size;*/
    gf_msg(this->name, GF_LOG_INFO, 0, FS_CACHE_MSG_INFO, "fsc_inode open for write fd=%d,localsize=%zu,serversize=%zu path=(%s),gfid=(%s)",
           fsc_inode->fsc_fd, fsc_inode->fsc_size, fsc_inode->ia_size, fsc_inode->local_path, uuid_utoa(fsc_inode->inode->gfid));
out:
    return op_ret;
}


int32_t
fsc_inode_read(fsc_inode_t *fsc_inode, call_frame_t *frame, xlator_t *this, fd_t *fd, size_t size,
               off_t offset, uint32_t flags, dict_t *xdata) {

    int32_t op_ret = -1;
    int32_t op_errno = 0;
    gf_boolean_t is_fault = _gf_true;
    struct iobuf *iobuf = NULL;
    struct iobref *iobref = NULL;
    struct iovec vec = {
        0,
    };
    struct iatt stbuf = {
        0,
    };
    dict_t *rsp_xdata = NULL;

    fsc_inode_lock(fsc_inode);
    {
        op_ret = fsc_inode_open_for_read(this, fsc_inode);
        if (op_ret >= 0) {
            op_ret = fsc_block_is_cache(this, fsc_inode, offset, size);
            if (op_ret == 0) {
                is_fault = _gf_false;
            }
        }
    }
    fsc_inode_unlock(fsc_inode);

    if (is_fault) {
        op_ret = -1;
        gf_msg(this->name, GF_LOG_TRACE, 0, FS_CACHE_MSG_TRACE,
               "fsc_inode fault=(%s),fd=%d, offset=%zu,req_size=%zu",
               fsc_inode->local_path, fsc_inode->fsc_fd, offset, size);
        goto out;
    }

    /* read */
    iobuf = iobuf_get_page_aligned(this->ctx->iobuf_pool, size, ALIGN_SIZE);
    if (!iobuf) {
        op_errno = ENOMEM;
        op_ret = -1;
        goto out;
    }
    stbuf.ia_size = fsc_inode->ia_size;
    op_ret = sys_pread(fsc_inode->fsc_fd, iobuf->ptr, size, offset);
    if (op_ret == -1) {
        op_errno = errno;
        gf_msg(this->name, GF_LOG_ERROR, errno, FS_CACHE_MSG_ERROR,
               "read failed on gfid=%s, "
               "fd=%d, offset=%" PRIu64 " size=%" GF_PRI_SIZET
               ", "
               "buf=%p",
               uuid_utoa(fsc_inode->inode->gfid), fsc_inode->fsc_fd, offset, size, iobuf->ptr);
        op_ret = -1;
        goto out;
    }

    if ((offset + op_ret) >= fsc_inode->ia_size) {
        /*op_errno = ENOENT;*/
        gf_msg(this->name, GF_LOG_INFO, 0, FS_CACHE_MSG_INFO,
               "fsc_inode read local finish=(%s),fd=%d, offset=%zu,req_size=%zu, rsp_size=%d, op_errno=%d",
               fsc_inode->local_path, fsc_inode->fsc_fd, offset, size, op_ret, op_errno);
    }

    vec.iov_base = iobuf->ptr;
    vec.iov_len = op_ret;
    iobref = iobref_new();
    iobref_add(iobref, iobuf);

    gf_msg(this->name, GF_LOG_TRACE, 0, FS_CACHE_MSG_TRACE,
           "fsc_inode read local=(%s),fd=%d, offset=%zu,req_size=%zu, rsp_size=%d, op_errno=%d",
           fsc_inode->local_path, fsc_inode->fsc_fd, offset, size, op_ret, op_errno);

    STACK_UNWIND_STRICT(readv, frame, op_ret, op_errno, &vec, 1, &stbuf, iobref,
                        rsp_xdata);
out:
    if (iobref)
        iobref_unref(iobref);
    if (iobuf)
        iobuf_unref(iobuf);
    return op_ret;
}

char *
fsc_page_aligned_alloc(size_t size, char **aligned_buf)
{
    char *alloc_buf = NULL;
    char *buf = NULL;

    alloc_buf = GF_CALLOC(1, (size + ALIGN_SIZE), gf_fsc_mt_fsc_posix_page_aligned_t);
    if (!alloc_buf)
        goto out;
    /* page aligned buffer */
    buf = GF_ALIGN_BUF(alloc_buf, ALIGN_SIZE);
    *aligned_buf = buf;
out:
    return alloc_buf;
}
