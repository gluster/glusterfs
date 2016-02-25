/*
   Copyright (c) 2008-2012 Red Hat, Inc. <http://www.redhat.com>
   This file is part of GlusterFS.

   This file is licensed to you under your choice of the GNU Lesser
   General Public License, version 3 or any later version (LGPLv3 or
   later), or the GNU General Public License, version 2 (GPLv2), in all
   cases as published by the Free Software Foundation.
*/
#include "index.h"
#include "options.h"
#include "glusterfs3-xdr.h"
#include "syscall.h"
#include "syncop.h"

#define XATTROP_SUBDIR "xattrop"
#define DIRTY_SUBDIR "dirty"

struct index_syncop_args {
        inode_t *parent;
        gf_dirent_t *entries;
};

call_stub_t *
__index_dequeue (struct list_head *callstubs)
{
        call_stub_t *stub = NULL;

        if (!list_empty (callstubs)) {
                stub = list_entry (callstubs->next, call_stub_t, list);
                list_del_init (&stub->list);
        }

        return stub;
}

static void
__index_enqueue (struct list_head *callstubs, call_stub_t *stub)
{
        list_add_tail (&stub->list, callstubs);
}

static void
worker_enqueue (xlator_t *this, call_stub_t *stub)
{
        index_priv_t    *priv = NULL;

        priv = this->private;
        pthread_mutex_lock (&priv->mutex);
        {
                __index_enqueue (&priv->callstubs, stub);
                pthread_cond_signal (&priv->cond);
        }
        pthread_mutex_unlock (&priv->mutex);
}

void *
index_worker (void *data)
{
        index_priv_t     *priv = NULL;
        xlator_t         *this = NULL;
        call_stub_t      *stub = NULL;
        int               ret = 0;

        THIS = data;
        this = data;
        priv = this->private;

        for (;;) {
                pthread_mutex_lock (&priv->mutex);
                {
                        while (list_empty (&priv->callstubs)) {
                                ret = pthread_cond_wait (&priv->cond,
                                                         &priv->mutex);
                        }

                        stub = __index_dequeue (&priv->callstubs);
                }
                pthread_mutex_unlock (&priv->mutex);

                if (stub) /* guard against spurious wakeups */
                        call_resume (stub);
        }

        return NULL;
}

int
__index_inode_ctx_get (inode_t *inode, xlator_t *this, index_inode_ctx_t **ctx)
{
        int               ret = 0;
        index_inode_ctx_t *ictx = NULL;
        uint64_t          tmpctx = 0;

        ret = __inode_ctx_get (inode, this, &tmpctx);
        if (!ret) {
                ictx = (index_inode_ctx_t*) (long) tmpctx;
                goto out;
        }
        ictx = GF_CALLOC (1, sizeof (*ictx), gf_index_inode_ctx_t);
        if (!ictx) {
                ret = -1;
                goto out;
        }

        INIT_LIST_HEAD (&ictx->callstubs);
        ret = __inode_ctx_put (inode, this, (uint64_t)ictx);
        if (ret) {
                GF_FREE (ictx);
                ictx = NULL;
                goto out;
        }
out:
        if (ictx)
                *ctx = ictx;
        return ret;
}

int
index_inode_ctx_get (inode_t *inode, xlator_t *this, index_inode_ctx_t **ctx)
{
        int               ret = 0;

        LOCK (&inode->lock);
        {
                ret = __index_inode_ctx_get (inode, this, ctx);
        }
        UNLOCK (&inode->lock);

        return ret;
}

static void
make_index_dir_path (char *base, const char *subdir,
                     char *index_dir, size_t len)
{
        snprintf (index_dir, len, "%s/%s", base, subdir);
}

int
index_dir_create (xlator_t *this, const char *subdir)
{
        int          ret = 0;
        struct       stat st = {0};
        char         fullpath[PATH_MAX] = {0};
        char         path[PATH_MAX] = {0};
        char         *dir = NULL;
        index_priv_t *priv = NULL;
        size_t       len = 0;
        size_t       pathlen = 0;

        priv = this->private;
        make_index_dir_path (priv->index_basepath, subdir, fullpath,
                             sizeof (fullpath));
        ret = sys_stat (fullpath, &st);
        if (!ret) {
                if (!S_ISDIR (st.st_mode))
                        ret = -2;
                goto out;
        }

        pathlen = strlen (fullpath);
        if ((pathlen > 1) && fullpath[pathlen - 1] == '/')
                fullpath[pathlen - 1] = '\0';
        dir = strchr (fullpath, '/');
        while (dir) {
                dir = strchr (dir + 1, '/');
                if (dir)
                        len = pathlen - strlen (dir);
                else
                        len = pathlen;
                strncpy (path, fullpath, len);
                path[len] = '\0';
                ret = sys_mkdir (path, 0600);
                if (ret && (errno != EEXIST))
                        goto out;
        }
        ret = 0;
out:
        if (ret == -1) {
                gf_log (this->name, GF_LOG_ERROR, "%s/%s: Failed to "
                        "create (%s)", priv->index_basepath, subdir,
                        strerror (errno));
        } else if (ret == -2) {
                gf_log (this->name, GF_LOG_ERROR, "%s/%s: Failed to create, "
                        "path exists, not a directory ", priv->index_basepath,
                        subdir);
        }
        return ret;
}

void
index_get_index (index_priv_t *priv, uuid_t index)
{
        LOCK (&priv->lock);
        {
                gf_uuid_copy (index, priv->index);
        }
        UNLOCK (&priv->lock);
}

void
index_generate_index (index_priv_t *priv, uuid_t index)
{
        LOCK (&priv->lock);
        {
                //To prevent duplicate generates.
                //This method fails if number of contending threads is greater
                //than MAX_LINK count of the fs
                if (!gf_uuid_compare (priv->index, index))
                        gf_uuid_generate (priv->index);
                gf_uuid_copy (index, priv->index);
        }
        UNLOCK (&priv->lock);
}

static void
make_index_path (char *base, const char *subdir, uuid_t index,
                 char *index_path, size_t len)
{
        make_index_dir_path (base, subdir, index_path, len);
        snprintf (index_path + strlen (index_path), len - strlen (index_path),
                  "/%s-%s", subdir, uuid_utoa (index));
}

static void
make_gfid_path (char *base, const char *subdir, uuid_t gfid,
                char *gfid_path, size_t len)
{
        make_index_dir_path (base, subdir, gfid_path, len);
        snprintf (gfid_path + strlen (gfid_path), len - strlen (gfid_path),
                  "/%s", uuid_utoa (gfid));
}

static void
make_file_path (char *base, const char *subdir, const char *filename,
                char *file_path, size_t len)
{
        make_index_dir_path (base, subdir, file_path, len);
        snprintf (file_path + strlen (file_path), len - strlen (file_path),
                  "/%s", filename);
}

static int
is_index_file_current (char *filename, uuid_t priv_index, char *subdir)
{
        char current_index[GF_UUID_BUF_SIZE + 16] = {0, };

        snprintf (current_index, sizeof current_index,
                  "%s-%s", subdir, uuid_utoa(priv_index));
        return (!strcmp(filename, current_index));
}

static void
check_delete_stale_index_file (xlator_t *this, char *filename, char *subdir)
{
        int             ret = 0;
        struct stat     st = {0};
        char            filepath[PATH_MAX] = {0};
        index_priv_t    *priv = NULL;

        priv = this->private;

        if (is_index_file_current (filename, priv->index, subdir))
                return;

        make_file_path (priv->index_basepath, subdir,
                        filename, filepath, sizeof (filepath));
        ret = sys_stat (filepath, &st);
        if (!ret && st.st_nlink == 1)
                sys_unlink (filepath);
}

static void
index_set_link_count (index_priv_t *priv, int64_t count,
                      index_xattrop_type_t type)
{
        switch (type) {
        case PENDING:
                LOCK (&priv->lock);
                {
                        priv->pending_count = count;
                }
                UNLOCK (&priv->lock);
                break;
        default:
                break;
        }
}

static void
index_get_link_count (index_priv_t *priv, int64_t *count,
                      index_xattrop_type_t type)
{
        switch (type) {
        case PENDING:
                LOCK (&priv->lock);
                {
                        *count = priv->pending_count;
                }
                UNLOCK (&priv->lock);
                break;
        default:
                break;
        }
}

static void
index_dec_link_count (index_priv_t *priv, index_xattrop_type_t type)
{
        switch (type) {
        case PENDING:
                LOCK (&priv->lock);
                {
                        priv->pending_count--;
                        if (priv->pending_count == 0)
                                priv->pending_count--;
                }
                UNLOCK (&priv->lock);
                break;
        default:
                break;
        }
}

int
index_get_type_from_vgfid (index_priv_t *priv, uuid_t vgfid)
{
        if (!gf_uuid_compare (priv->xattrop_vgfid, vgfid))
                return PENDING;
        if (!gf_uuid_compare (priv->dirty_vgfid, vgfid)) {
                return DIRTY;
        }
        return -1;
}

char*
index_get_subdir_from_vgfid (index_priv_t *priv, uuid_t vgfid)
{

        if (!gf_uuid_compare (priv->xattrop_vgfid, vgfid))
                return XATTROP_SUBDIR;
        if (!gf_uuid_compare (priv->dirty_vgfid, vgfid)) {
                return DIRTY_SUBDIR;
        }
        return NULL;
}

char*
index_get_subdir_from_type (index_xattrop_type_t type)
{

        switch (type) {
        case PENDING:
                return XATTROP_SUBDIR;
        case DIRTY:
                return DIRTY_SUBDIR;
        default:
                return NULL;
        }
}

static int
index_fill_readdir (fd_t *fd, index_fd_ctx_t *fctx, DIR *dir, off_t off,
                    size_t size, gf_dirent_t *entries)
{
        off_t     in_case = -1;
        off_t     last_off = 0;
        size_t    filled = 0;
        int       count = 0;
        char      entrybuf[sizeof(struct dirent) + 256 + 8];
        struct dirent  *entry          = NULL;
        int32_t              this_size      = -1;
        gf_dirent_t          *this_entry     = NULL;
        xlator_t             *this = NULL;

        this = THIS;
        if (!off) {
                rewinddir (dir);
        } else {
                seekdir (dir, off);
#ifndef GF_LINUX_HOST_OS
                if ((u_long)telldir(dir) != off && off != fctx->dir_eof) {
                        gf_log (THIS->name, GF_LOG_ERROR,
                                "seekdir(0x%llx) failed on dir=%p: "
				"Invalid argument (offset reused from "
				"another DIR * structure?)", off, dir);
                        errno = EINVAL;
                        count = -1;
                        goto out;
                }
#endif /* GF_LINUX_HOST_OS */
        }

        while (filled <= size) {
                in_case = (u_long)telldir (dir);

                if (in_case == -1) {
                        gf_log (THIS->name, GF_LOG_ERROR,
                                "telldir failed on dir=%p: %s",
                                dir, strerror (errno));
                        goto out;
                }

                errno = 0;
                entry = NULL;
                readdir_r (dir, (struct dirent *)entrybuf, &entry);

                if (!entry) {
                        if (errno == EBADF) {
                                gf_log (THIS->name, GF_LOG_WARNING,
                                        "readdir failed on dir=%p: %s",
                                        dir, strerror (errno));
                                goto out;
                        }
                        break;
                }

                if (!strncmp (entry->d_name, XATTROP_SUBDIR"-",
                              strlen (XATTROP_SUBDIR"-"))) {
                        check_delete_stale_index_file (this, entry->d_name,
                                                       XATTROP_SUBDIR);
                        continue;
                } else if (!strncmp (entry->d_name, DIRTY_SUBDIR"-",
                           strlen (DIRTY_SUBDIR"-"))) {
                        check_delete_stale_index_file (this, entry->d_name,
                                                       DIRTY_SUBDIR);
                        continue;
                }

                this_size = max (sizeof (gf_dirent_t),
                                 sizeof (gfs3_dirplist))
                        + strlen (entry->d_name) + 1;

                if (this_size + filled > size) {
                        seekdir (dir, in_case);
#ifndef GF_LINUX_HOST_OS
                        if ((u_long)telldir(dir) != in_case &&
                            in_case != fctx->dir_eof) {
				gf_log (THIS->name, GF_LOG_ERROR,
					"seekdir(0x%llx) failed on dir=%p: "
					"Invalid argument (offset reused from "
					"another DIR * structure?)",
					in_case, dir);
				errno = EINVAL;
				count = -1;
				goto out;
                        }
#endif /* GF_LINUX_HOST_OS */
                        break;
                }

                this_entry = gf_dirent_for_name (entry->d_name);

                if (!this_entry) {
                        gf_log (THIS->name, GF_LOG_ERROR,
                                "could not create gf_dirent for entry %s: (%s)",
                                entry->d_name, strerror (errno));
                        goto out;
                }
                /*
                 * we store the offset of next entry here, which is
                 * probably not intended, but code using syncop_readdir()
                 * (glfs-heal.c, afr-self-heald.c, pump.c) rely on it
                 * for directory read resumption.
                 */
                last_off = (u_long)telldir(dir);
                this_entry->d_off = last_off;
                this_entry->d_ino = entry->d_ino;

                list_add_tail (&this_entry->list, &entries->list);

                filled += this_size;
                count ++;
        }

        if ((!sys_readdir (dir) && (errno == 0))) {
                /* Indicate EOF */
                errno = ENOENT;
                /* Remember EOF offset for later detection */
                fctx->dir_eof = last_off;
        }
out:
        return count;
}

int
index_add (xlator_t *this, uuid_t gfid, const char *subdir,
           index_xattrop_type_t type)
{
        int32_t           op_errno = 0;
        char              gfid_path[PATH_MAX] = {0};
        char              index_path[PATH_MAX] = {0};
        int               ret = 0;
        uuid_t            index = {0};
        index_priv_t      *priv = NULL;
        struct stat       st = {0};
        int               fd = 0;

        priv = this->private;
        GF_ASSERT_AND_GOTO_WITH_ERROR (this->name, !gf_uuid_is_null (gfid),
                                       out, op_errno, EINVAL);

        make_gfid_path (priv->index_basepath, subdir, gfid,
                        gfid_path, sizeof (gfid_path));

        ret = sys_stat (gfid_path, &st);
        if (!ret)
                goto out;
        index_get_index (priv, index);
        make_index_path (priv->index_basepath, subdir,
                         index, index_path, sizeof (index_path));
        ret = sys_link (index_path, gfid_path);
        if (!ret || (errno == EEXIST))  {
                ret = 0;
                goto out;
        }

        op_errno = errno;
        if (op_errno == ENOENT) {
                ret = index_dir_create (this, subdir);
                if (ret)
                        goto out;
        } else if (op_errno == EMLINK) {
                index_generate_index (priv, index);
                make_index_path (priv->index_basepath, subdir,
                                 index, index_path, sizeof (index_path));
        } else {
                goto out;
        }

        fd = sys_creat (index_path, 0);
        if ((fd < 0) && (errno != EEXIST)) {
                ret = -1;
                gf_log (this->name, GF_LOG_ERROR, "%s: Not able to "
                        "create index (%s)", uuid_utoa (gfid),
                        strerror (errno));
                goto out;
        }

        if (fd >= 0)
                sys_close (fd);

        ret = sys_link (index_path, gfid_path);
        if (ret && (errno != EEXIST)) {
                gf_log (this->name, GF_LOG_ERROR, "%s: Not able to "
                        "add to index (%s)", uuid_utoa (gfid),
                        strerror (errno));
                goto out;
        }

        ret = 0;
out:
        return ret;
}

int
index_del (xlator_t *this, uuid_t gfid, const char *subdir, int type)
{
        int32_t      op_errno __attribute__((unused)) = 0;
        index_priv_t *priv = NULL;
        int          ret = 0;
        char         gfid_path[PATH_MAX] = {0};

        priv = this->private;
        GF_ASSERT_AND_GOTO_WITH_ERROR (this->name, !gf_uuid_is_null (gfid),
                                       out, op_errno, EINVAL);
        make_gfid_path (priv->index_basepath, subdir, gfid,
                        gfid_path, sizeof (gfid_path));
        ret = sys_unlink (gfid_path);
        if (ret && (errno != ENOENT)) {
                gf_log (this->name, GF_LOG_ERROR,
                        "%s: failed to delete from index (%s)",
                        gfid_path, strerror (errno));
                ret = -errno;
                goto out;
        }
        index_dec_link_count (priv, type);
        ret = 0;
out:
        return ret;
}

static gf_boolean_t
_is_xattr_in_watchlist (dict_t *d, char *k, data_t *v, void *tmp)
{
        const char *data = tmp;

        if (!strncmp (k, tmp, strlen (k)))
                return _gf_true;

        return _gf_false;
}

static gf_boolean_t
is_xattr_in_watchlist (dict_t *this, char *key, data_t *value, void *matchdata)
{
        int    ret = -1;

        //matchdata is a list of xattrs
        //key is strncmp'ed with each xattr in matchdata.
        //ret will be 0 if key pattern is not present in the matchdata
        //else ret will be count number of xattrs the key pattern-matches with.
        ret = dict_foreach_match (matchdata, _is_xattr_in_watchlist, key,
                                  dict_null_foreach_fn, NULL);

        if (ret > 0)
                return _gf_true;
        return _gf_false;
}

static int
index_find_xattr_type (dict_t *d, char *k, data_t *v)
{
        int             idx  = -1;
        index_priv_t   *priv = THIS->private;

        if (priv->dirty_watchlist && is_xattr_in_watchlist (d, k, v,
                                            priv->dirty_watchlist))
                idx = DIRTY;
        else if (priv->pending_watchlist && is_xattr_in_watchlist (d, k, v,
                                                  priv->pending_watchlist))
                idx = PENDING;

        return idx;
}

int
index_fill_zero_array (dict_t *d, char *k, data_t *v, void *adata)
{
        int     idx = -1;
        int     *zfilled = adata;
        //zfilled array contains `state` for all types xattrs.
        //state : whether the gfid file of this file exists in
        //corresponding xattr directory or not.

        idx = index_find_xattr_type (d, k, v);
        if (idx == -1)
                return 0;
        zfilled[idx] = 0;
        return 0;
}

static int
_check_key_is_zero_filled (dict_t *d, char *k, data_t *v,
                           void *tmp)
{
        int            *zfilled = tmp;
        int             idx = -1;

        idx = index_find_xattr_type (d, k, v);
        if (idx == -1)
                return 0;

        /* Along with checking that the value of a key is zero filled
         * the key's corresponding index should be assigned
         * appropriate value.
         * zfilled[idx] will be 0(false) if value not zero.
         *              will be 1(true) if value is zero.
         */
        if (mem_0filled ((const char*)v->data, v->len)) {
                zfilled[idx] = 0;
                return 0;
        }

        /* If zfilled[idx] was previously 0, it means at least
         * one xattr of its "kind" is non-zero. Keep its value
         * the same.
         */
        if (zfilled[idx])
                zfilled[idx] = 1;
        return 0;
}

void
_index_action (xlator_t *this, inode_t *inode, int *zfilled)
{
        int               ret  = 0;
        int               i    = 0;
        index_inode_ctx_t *ctx = NULL;
        char           *subdir = NULL;

        ret = index_inode_ctx_get (inode, this, &ctx);
        if (ret) {
                gf_log (this->name, GF_LOG_ERROR, "Not able to get"
                        " inode context for %s.", uuid_utoa (inode->gfid));
                goto out;
        }
        for (i = 0; i < XATTROP_TYPE_END; i++) {
                subdir = index_get_subdir_from_type (i);
                if (zfilled[i] == 1) {
                        if (ctx->state[i] == NOTIN)
                                continue;
                        ret = index_del (this, inode->gfid, subdir, i);
                        if (!ret)
                                ctx->state[i] = NOTIN;
                } else if (zfilled[i] == 0){
                        if (ctx->state[i] == IN)
                                continue;
                        ret = index_add (this, inode->gfid, subdir, i);
                        if (!ret)
                                ctx->state[i] = IN;
                }
        }
out:
        return;
}

void
xattrop_index_action (xlator_t *this, inode_t *inode, dict_t *xattr,
                      dict_match_t match, void *match_data)
{
        gf_boolean_t      zero_xattr = _gf_true;
        int               ret = 0, i = 0;
        int               zfilled[XATTROP_TYPE_END] = {0,};

        memset (zfilled, -1, sizeof (zfilled));
        ret = dict_foreach_match (xattr, match, match_data,
                                  _check_key_is_zero_filled, zfilled);
        _index_action (this, inode, zfilled);
        return;
}

static gf_boolean_t
index_xattrop_track (xlator_t *this, gf_xattrop_flags_t flags, dict_t *dict)
{
        index_priv_t *priv = this->private;

        if (flags == GF_XATTROP_ADD_ARRAY)
                return _gf_true;

        if (flags != GF_XATTROP_ADD_ARRAY64)
                return _gf_false;

        if (!priv->pending_watchlist)
                return _gf_false;

        if (dict_foreach_match (dict, is_xattr_in_watchlist,
                                priv->pending_watchlist, dict_null_foreach_fn,
                                NULL) > 0)
                return _gf_true;

        return _gf_false;
}

int
__index_fd_ctx_get (fd_t *fd, xlator_t *this, index_fd_ctx_t **ctx)
{
        int               ret = 0;
        index_fd_ctx_t    *fctx = NULL;
        uint64_t          tmpctx = 0;
        char              index_dir[PATH_MAX] = {0};
        char              *subdir = NULL;
        index_priv_t      *priv = NULL;

        priv = this->private;
        if (gf_uuid_compare (fd->inode->gfid, priv->xattrop_vgfid) &&
           (gf_uuid_compare (fd->inode->gfid, priv->dirty_vgfid))) {
                ret = -EINVAL;
                goto out;
        }

        ret = __fd_ctx_get (fd, this, &tmpctx);
        if (!ret) {
                fctx = (index_fd_ctx_t*) (long) tmpctx;
                goto out;
        }

        fctx = GF_CALLOC (1, sizeof (*fctx), gf_index_fd_ctx_t);
        if (!fctx) {
                ret = -ENOMEM;
                goto out;
        }

        subdir = index_get_subdir_from_vgfid (priv, fd->inode->gfid);
        make_index_dir_path (priv->index_basepath, subdir,
                             index_dir, sizeof (index_dir));
        fctx->dir = sys_opendir (index_dir);
        if (!fctx->dir) {
                ret = -errno;
                GF_FREE (fctx);
                fctx = NULL;
                goto out;
        }
        fctx->dir_eof = -1;

        ret = __fd_ctx_set (fd, this, (uint64_t)(long)fctx);
        if (ret) {
                sys_closedir (fctx->dir);
                GF_FREE (fctx);
                fctx = NULL;
                ret = -EINVAL;
                goto out;
        }
out:
        if (fctx)
                *ctx = fctx;
        return ret;
}

int
index_fd_ctx_get (fd_t *fd, xlator_t *this, index_fd_ctx_t **ctx)
{
        int     ret = 0;
        LOCK (&fd->lock);
        {
                ret = __index_fd_ctx_get (fd, this, ctx);
        }
        UNLOCK (&fd->lock);
        return ret;
}

//new - Not NULL means start a fop
//new - NULL means done processing the fop
void
index_queue_process (xlator_t *this, inode_t *inode, call_stub_t *new)
{
        call_stub_t       *stub = NULL;
        index_inode_ctx_t *ctx = NULL;
        int               ret = 0;
        call_frame_t      *frame = NULL;

        LOCK (&inode->lock);
        {
                ret = __index_inode_ctx_get (inode, this, &ctx);
                if (ret)
                        goto unlock;

                if (new) {
                        __index_enqueue (&ctx->callstubs, new);
                        new = NULL;
                } else {
                        ctx->processing = _gf_false;
                }

                if (!ctx->processing) {
                        stub = __index_dequeue (&ctx->callstubs);
                        if (stub)
                                ctx->processing = _gf_true;
                        else
                                ctx->processing = _gf_false;
                }
        }
unlock:
        UNLOCK (&inode->lock);

        if (ret && new) {
                frame = new->frame;
                if (new->fop == GF_FOP_XATTROP) {
                        INDEX_STACK_UNWIND (xattrop, frame, -1, ENOMEM,
                                            NULL, NULL);
                } else if (new->fop == GF_FOP_FXATTROP) {
                        INDEX_STACK_UNWIND (fxattrop, frame, -1, ENOMEM,
                                            NULL, NULL);
                }
                call_stub_destroy (new);
        } else if (stub) {
                call_resume (stub);
        }
        return;
}

static int
xattrop_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
             int32_t op_ret, int32_t op_errno, dict_t *xattr,
             dict_t *xdata, dict_match_t match, dict_t *matchdata)
{
        inode_t *inode = NULL;

        inode = inode_ref (frame->local);
        if (op_ret < 0)
                goto out;

        xattrop_index_action (this, frame->local, xattr, match, matchdata);
out:
        INDEX_STACK_UNWIND (xattrop, frame, op_ret, op_errno, xattr, xdata);
        index_queue_process (this, inode, NULL);
        inode_unref (inode);

        return 0;
}

int32_t
index_xattrop_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                   int32_t op_ret, int32_t op_errno, dict_t *xattr,
                   dict_t *xdata)
{
        index_priv_t *priv = this->private;

        xattrop_cbk (frame, cookie, this, op_ret, op_errno,
                     xattr, xdata, is_xattr_in_watchlist,
                     priv->complete_watchlist);
        return 0;
}

int32_t
index_xattrop64_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                     int32_t op_ret, int32_t op_errno, dict_t *xattr,
                     dict_t *xdata)
{
        index_priv_t *priv = this->private;

        return xattrop_cbk (frame, cookie, this, op_ret, op_errno, xattr, xdata,
                            is_xattr_in_watchlist, priv->pending_watchlist);
}

void
index_xattrop_do (call_frame_t *frame, xlator_t *this, loc_t *loc,
                  fd_t *fd, gf_xattrop_flags_t optype, dict_t *xattr,
                  dict_t *xdata)
{
        fop_xattrop_cbk_t cbk = NULL;
        int     zfilled[XATTROP_TYPE_END] = {0,};
        int     ret = -1, i = 0;
        index_priv_t *priv = this->private;

        //In wind phase bring the gfid into index. This way if the brick crashes
        //just after posix performs xattrop before _cbk reaches index xlator
        //we will still have the gfid in index.
        memset (zfilled, -1, sizeof (zfilled));

        /* Foreach xattr, set corresponding index of zfilled to 1
         * zfilled[index] = 1 implies the xattr's value is zero filled
         * and should be added in its corresponding subdir.
         *
         * zfilled should be set to 1 only for those index that
         * exist in xattr variable. This is to distinguish
         * between different types of volumes.
         * For e.g., if the check is not made,
         * zfilled[DIRTY] is set to 1 for EC volumes,
         * index file will be tried to create in indices/dirty dir
         * which doesn't exist for an EC volume.
         */
        ret = dict_foreach (xattr, index_fill_zero_array, zfilled);

        _index_action (this, frame->local, zfilled);

        if (optype == GF_XATTROP_ADD_ARRAY)
                cbk = index_xattrop_cbk;
        else
                cbk = index_xattrop64_cbk;

        if (loc)
                STACK_WIND (frame, cbk, FIRST_CHILD (this),
                            FIRST_CHILD (this)->fops->xattrop,
                            loc, optype, xattr, xdata);
        else
                STACK_WIND (frame, cbk, FIRST_CHILD (this),
                            FIRST_CHILD (this)->fops->fxattrop, fd,
                            optype, xattr, xdata);
}

int
index_xattrop_wrapper (call_frame_t *frame, xlator_t *this, loc_t *loc,
                       gf_xattrop_flags_t optype, dict_t *xattr, dict_t *xdata)
{
        index_xattrop_do (frame, this, loc, NULL, optype, xattr, xdata);
        return 0;
}

int
index_fxattrop_wrapper (call_frame_t *frame, xlator_t *this, fd_t *fd,
                        gf_xattrop_flags_t optype, dict_t *xattr, dict_t *xdata)
{
        index_xattrop_do (frame, this, NULL, fd, optype, xattr, xdata);
        return 0;
}

int32_t
index_xattrop (call_frame_t *frame, xlator_t *this, loc_t *loc,
	       gf_xattrop_flags_t flags, dict_t *dict, dict_t *xdata)
{
        call_stub_t     *stub = NULL;

        if (!index_xattrop_track (this, flags, dict))
                goto out;

        frame->local = inode_ref (loc->inode);
        stub = fop_xattrop_stub (frame, index_xattrop_wrapper,
                                 loc, flags, dict, xdata);
        if (!stub) {
                INDEX_STACK_UNWIND (xattrop, frame, -1, ENOMEM, NULL, NULL);
                return 0;
        }

        index_queue_process (this, loc->inode, stub);
        return 0;
out:
        STACK_WIND (frame, default_xattrop_cbk, FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->xattrop, loc, flags, dict, xdata);
        return 0;
}

int32_t
index_fxattrop (call_frame_t *frame, xlator_t *this, fd_t *fd,
		gf_xattrop_flags_t flags, dict_t *dict, dict_t *xdata)
{
        call_stub_t    *stub = NULL;

        if (!index_xattrop_track (this, flags, dict))
                goto out;

        frame->local = inode_ref (fd->inode);
        stub = fop_fxattrop_stub (frame, index_fxattrop_wrapper,
                                  fd, flags, dict, xdata);
        if (!stub) {
                INDEX_STACK_UNWIND (fxattrop, frame, -1, ENOMEM, NULL, xdata);
                return 0;
        }

        index_queue_process (this, fd->inode, stub);
        return 0;
out:
        STACK_WIND (frame, default_fxattrop_cbk, FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->fxattrop, fd, flags, dict, xdata);
        return 0;
}

uint64_t
index_entry_count (xlator_t *this, char *subdir)
{
	index_priv_t *priv = NULL;
	char index_dir[PATH_MAX];
	DIR *dirp = NULL;
	uint64_t count = 0;
	struct dirent buf;
	struct dirent *entry = NULL;

	priv = this->private;

	make_index_dir_path (priv->index_basepath, subdir,
			     index_dir, sizeof (index_dir));

	dirp = sys_opendir (index_dir);
	if (!dirp)
		return 0;

	while (readdir_r (dirp, &buf, &entry) == 0) {
		if (!entry)
			break;
		if (!strcmp (entry->d_name, ".") ||
		    !strcmp (entry->d_name, ".."))
			continue;
                if (!strncmp (entry->d_name, subdir, strlen (subdir)))
			continue;
		count++;
	}
	sys_closedir (dirp);

	return count;
}


int32_t
index_getxattr_wrapper (call_frame_t *frame, xlator_t *this,
                        loc_t *loc, const char *name, dict_t *xdata)
{
        index_priv_t    *priv = NULL;
        dict_t          *xattr = NULL;
        int             ret = 0;
	uint64_t        count = 0;

        priv = this->private;

        xattr = dict_new ();
        if (!xattr) {
                ret = -ENOMEM;
                goto done;
        }

	if (strcmp (name, GF_XATTROP_INDEX_GFID) == 0) {
		ret = dict_set_static_bin (xattr, (char*)name, priv->xattrop_vgfid,
					   sizeof (priv->xattrop_vgfid));
		if (ret) {
			ret = -ENOMEM;
			gf_log (this->name, GF_LOG_ERROR, "xattrop index "
				"gfid set failed");
			goto done;
		}
	} else if (strcmp (name, GF_XATTROP_DIRTY_GFID) == 0) {
		ret = dict_set_static_bin (xattr, (char*)name, priv->dirty_vgfid,
					   sizeof (priv->dirty_vgfid));
		if (ret) {
			ret = -ENOMEM;
			gf_log (this->name, GF_LOG_ERROR, "dirty index "
				"gfid set failed");
			goto done;
		}
        } else if (strcmp (name, GF_XATTROP_INDEX_COUNT) == 0) {
		count = index_entry_count (this, XATTROP_SUBDIR);

		ret = dict_set_uint64 (xattr, (char *)name, count);
		if (ret) {
			ret = -ENOMEM;
			gf_log (this->name, GF_LOG_ERROR, "xattrop index "
				"count set failed");
			goto done;
		}
        } else if (strcmp (name, GF_XATTROP_DIRTY_COUNT) == 0) {
		count = index_entry_count (this, DIRTY_SUBDIR);

		ret = dict_set_uint64 (xattr, (char *)name, count);
		if (ret) {
			ret = -ENOMEM;
			gf_log (this->name, GF_LOG_ERROR, "dirty index "
				"count set failed");
			goto done;
		}
	}
done:
        if (ret)
                STACK_UNWIND_STRICT (getxattr, frame, -1, -ret, xattr, xdata);
        else
                STACK_UNWIND_STRICT (getxattr, frame, 0, 0, xattr, xdata);

        if (xattr)
                dict_unref (xattr);

        return 0;
}

int32_t
index_lookup_wrapper (call_frame_t *frame, xlator_t *this,
                      loc_t *loc, dict_t *xattr_req)
{
        index_priv_t    *priv = NULL;
        struct stat     lstatbuf = {0};
        int             ret = 0;
        int32_t         op_errno = EINVAL;
        int32_t         op_ret = -1;
        char            path[PATH_MAX] = {0};
        struct iatt     stbuf        = {0, };
        struct iatt     postparent = {0,};
        dict_t          *xattr = NULL;
        gf_boolean_t    is_dir = _gf_false;
        char            *subdir = NULL;

        priv = this->private;

        VALIDATE_OR_GOTO (loc, done);
        subdir = index_get_subdir_from_vgfid (priv, loc->gfid);
        if (subdir) {
                make_index_dir_path (priv->index_basepath, subdir,
                                     path, sizeof (path));
                is_dir = _gf_true;
        } else {
                subdir = index_get_subdir_from_vgfid (priv, loc->pargfid);
                make_file_path (priv->index_basepath, subdir,
                                loc->name, path, sizeof (path));
        }
        ret = sys_lstat (path, &lstatbuf);
        if (ret) {
                gf_log (this->name, GF_LOG_DEBUG, "Stat failed on index dir "
                        "(%s)", strerror (errno));
                op_errno = errno;
                goto done;
        } else if (!S_ISDIR (lstatbuf.st_mode) && is_dir) {
                gf_log (this->name, GF_LOG_DEBUG, "Stat failed on index dir, "
                        "not a directory");
                op_errno = ENOENT;
                goto done;
        }
        xattr = dict_new ();
        if (!xattr) {
                op_errno = ENOMEM;
                goto done;
        }

        iatt_from_stat (&stbuf, &lstatbuf);
        if (is_dir)
                gf_uuid_copy (stbuf.ia_gfid, loc->gfid);
        else
                gf_uuid_generate (stbuf.ia_gfid);
        stbuf.ia_ino = -1;
        op_ret = 0;
done:
        STACK_UNWIND_STRICT (lookup, frame, op_ret, op_errno,
                             loc->inode, &stbuf, xattr, &postparent);
        if (xattr)
                dict_unref (xattr);
        return 0;
}

int
index_get_gfid_type (void *opaque)
{
        gf_dirent_t              *entry = NULL;
        inode_t                  *inode = NULL;
        xlator_t                 *this  = THIS;
        struct index_syncop_args *args  = opaque;
        loc_t                    loc    = {0};
        struct iatt              iatt   = {0};
        int                      ret    = 0;

        list_for_each_entry (entry, &args->entries->list, list) {
                loc_wipe (&loc);
                gf_uuid_parse (entry->d_name, loc.gfid);
                entry->d_type = IA_INVAL;
                loc.inode = inode_find (args->parent->table, loc.gfid);
                if (loc.inode) {
                        entry->d_type = loc.inode->ia_type;
                        continue;
                }
                loc.inode = inode_new (args->parent->table);
                if (!loc.inode)
                        continue;
                ret = syncop_lookup (FIRST_CHILD (this), &loc, &iatt, 0, 0, 0);
                if (ret == 0)
                        entry->d_type = iatt.ia_type;
        }
        loc_wipe (&loc);

        return 0;
}

int32_t
index_readdir_wrapper (call_frame_t *frame, xlator_t *this,
                       fd_t *fd, size_t size, off_t off, dict_t *xdata)
{
        index_fd_ctx_t       *fctx           = NULL;
        DIR                  *dir            = NULL;
        int                   ret            = -1;
        int32_t               op_ret         = -1;
        int32_t               op_errno       = 0;
        int                   count          = 0;
        gf_dirent_t           entries;
        struct index_syncop_args args = {0};

        INIT_LIST_HEAD (&entries.list);

        ret = index_fd_ctx_get (fd, this, &fctx);
        if (ret < 0) {
                gf_log (this->name, GF_LOG_WARNING,
                        "pfd is NULL, fd=%p", fd);
                op_errno = -ret;
                goto done;
        }

        dir = fctx->dir;

        if (!dir) {
                gf_log (this->name, GF_LOG_WARNING,
                        "dir is NULL for fd=%p", fd);
                op_errno = EINVAL;
                goto done;
        }

        count = index_fill_readdir (fd, fctx, dir, off, size, &entries);

        /* pick ENOENT to indicate EOF */
        op_errno = errno;
        op_ret = count;
        if (xdata && dict_get (xdata, "get-gfid-type")) {
                args.parent = fd->inode;
                args.entries = &entries;
                ret = synctask_new (this->ctx->env, index_get_gfid_type,
                                    NULL, NULL, &args);
        }
done:
        STACK_UNWIND_STRICT (readdir, frame, op_ret, op_errno, &entries, NULL);
        gf_dirent_free (&entries);
        return 0;
}

int
index_unlink_wrapper (call_frame_t *frame, xlator_t *this, loc_t *loc, int flag,
                      dict_t *xdata)
{
        index_priv_t    *priv = NULL;
        int32_t         op_ret = 0;
        int32_t         op_errno = 0;
        int             ret = 0;
        int             type = -1;
        struct  iatt    preparent = {0};
        struct  iatt    postparent = {0};
        char            index_dir[PATH_MAX] = {0};
        struct  stat    lstatbuf = {0};
        uuid_t          gfid = {0};
        char            *subdir = NULL;

        priv = this->private;
        type = index_get_type_from_vgfid (priv, loc->pargfid);
        subdir = index_get_subdir_from_type (type);
        make_index_dir_path (priv->index_basepath, subdir,
                             index_dir, sizeof (index_dir));
        ret = sys_lstat (index_dir, &lstatbuf);
        if (ret < 0) {
                op_ret = -1;
                op_errno = errno;
                goto done;
        }

        iatt_from_stat (&preparent, &lstatbuf);
        gf_uuid_copy (preparent.ia_gfid, loc->pargfid);
        preparent.ia_ino = -1;
        gf_uuid_parse (loc->name, gfid);
        ret = index_del (this, gfid, subdir, type);
        if (ret < 0) {
                op_ret = -1;
                op_errno = -ret;
                goto done;
        }
        memset (&lstatbuf, 0, sizeof (lstatbuf));
        ret = sys_lstat (index_dir, &lstatbuf);
        if (ret < 0) {
                op_ret = -1;
                op_errno = errno;
                goto done;
        }
        iatt_from_stat (&postparent, &lstatbuf);
        gf_uuid_copy (postparent.ia_gfid, loc->pargfid);
        postparent.ia_ino = -1;
done:
        INDEX_STACK_UNWIND (unlink, frame, op_ret, op_errno, &preparent,
                            &postparent, xdata);
        return 0;
}

int32_t
index_getxattr (call_frame_t *frame, xlator_t *this,
                loc_t *loc, const char *name, dict_t *xdata)
{
        call_stub_t     *stub = NULL;
        index_priv_t    *priv = NULL;

        priv = this->private;

        if (!name || (strcmp (GF_XATTROP_INDEX_GFID, name) &&
		      strcmp (GF_XATTROP_INDEX_COUNT, name) &&
                      strcmp (GF_XATTROP_DIRTY_GFID, name) &&
                      strcmp (GF_XATTROP_DIRTY_COUNT, name)))
                goto out;

        stub = fop_getxattr_stub (frame, index_getxattr_wrapper, loc, name,
                                  xdata);
        if (!stub) {
                STACK_UNWIND_STRICT (getxattr, frame, -1, ENOMEM, NULL, NULL);
                return 0;
        }
        worker_enqueue (this, stub);
        return 0;
out:
        STACK_WIND (frame, default_getxattr_cbk, FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->getxattr, loc, name, xdata);
        return 0;
}

int64_t
index_fetch_link_count (xlator_t *this, index_xattrop_type_t type)
{
	char            index_dir[PATH_MAX] = {0};
	char            index_path[PATH_MAX] = {0};
        index_priv_t    *priv    = this->private;
        char            *subdir  = NULL;
	DIR             *dirp    = NULL;
	struct dirent   *entry   = NULL;
        struct stat     lstatbuf = {0};
        int             ret      = -1;
        int64_t         count    = -1;
        struct dirent   buf;

        subdir = index_get_subdir_from_type (type);
	make_index_dir_path (priv->index_basepath, subdir,
			     index_dir, sizeof (index_dir));

	dirp = sys_opendir (index_dir);
	if (!dirp)
                goto out;

	while (readdir_r (dirp, &buf, &entry) == 0) {
		if (!entry) {
                        if (count == -1)
                                count = 0;
                        goto out;
                } else if (!strcmp (entry->d_name, ".") ||
		           !strcmp (entry->d_name, "..")) {
			continue;
                } else {
                        make_file_path (priv->index_basepath, subdir,
                                        entry->d_name, index_path,
                                        sizeof (index_path));
                        ret = sys_lstat (index_path, &lstatbuf);
                        if (ret < 0) {
                                count = -2;
                                continue;
                        } else {
                                count = lstatbuf.st_nlink - 1;
                                if (count == 0)
                                        continue;
                                else
                                        break;
                        }
                }
	}
out:
        if (dirp)
                sys_closedir (dirp);
        return count;
}

dict_t*
index_fill_link_count (xlator_t *this, dict_t *xdata)
{
        int             ret       = -1;
        index_priv_t    *priv     = NULL;
        int64_t         count     = -1;

        priv = this->private;
        xdata = (xdata) ? dict_ref (xdata) : dict_new ();
        if (!xdata)
                goto out;

        index_get_link_count (priv, &count, PENDING);
        if (count < 0) {
                count = index_fetch_link_count (this, PENDING);
                index_set_link_count (priv, count, PENDING);
        }

        if (count == 0) {
                ret = dict_set_int8 (xdata, "link-count", 0);
                if (ret < 0)
                        gf_log (this->name, GF_LOG_ERROR,
                                "Unable to set link-count");
        } else {
                ret = dict_set_int8 (xdata, "link-count", 1);
                if (ret < 0)
                        gf_log (this->name, GF_LOG_ERROR,
                                "Unable to set link-count");
        }

out:
        return xdata;
}

int32_t
index_lookup_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                  int32_t op_ret, int32_t op_errno, inode_t *inode,
                  struct iatt *buf, dict_t *xdata, struct iatt *postparent)
{

        xdata = index_fill_link_count (this, xdata);
        STACK_UNWIND_STRICT (lookup, frame, op_ret, op_errno, inode, buf,
                             xdata, postparent);
        if (xdata)
                dict_unref (xdata);
        return 0;
}

int32_t
index_lookup (call_frame_t *frame, xlator_t *this,
              loc_t *loc, dict_t *xattr_req)
{
        call_stub_t     *stub = NULL;
        index_priv_t    *priv = NULL;
        char            *flag = NULL;
        int              ret  = -1;

        priv = this->private;

        if (gf_uuid_compare (loc->gfid, priv->xattrop_vgfid) &&
            gf_uuid_compare (loc->pargfid, priv->xattrop_vgfid) &&
            gf_uuid_compare (loc->gfid, priv->dirty_vgfid) &&
            gf_uuid_compare (loc->pargfid, priv->dirty_vgfid))
                goto normal;

        stub = fop_lookup_stub (frame, index_lookup_wrapper, loc, xattr_req);
        if (!stub) {
                STACK_UNWIND_STRICT (lookup, frame, -1, ENOMEM, loc->inode,
                                     NULL, NULL, NULL);
                return 0;
        }
        worker_enqueue (this, stub);
        return 0;
normal:
        ret = dict_get_str (xattr_req, "link-count", &flag);
        if ((ret == 0) && (strcmp (flag, GF_XATTROP_INDEX_COUNT) == 0)) {
                STACK_WIND (frame, index_lookup_cbk, FIRST_CHILD(this),
                            FIRST_CHILD(this)->fops->lookup, loc, xattr_req);
        } else {
                STACK_WIND (frame, default_lookup_cbk, FIRST_CHILD(this),
                            FIRST_CHILD(this)->fops->lookup, loc, xattr_req);
        }

        return 0;
}

int32_t
index_fstat_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                 int32_t op_ret, int32_t op_errno, struct iatt *buf,
                 dict_t *xdata)
{
        xdata = index_fill_link_count (this, xdata);
        STACK_UNWIND_STRICT (fstat, frame, op_ret, op_errno, buf, xdata);
        if (xdata)
                dict_unref (xdata);
        return 0;
}

int32_t
index_fstat (call_frame_t *frame, xlator_t *this, fd_t *fd, dict_t *xdata)
{
        int  ret   = -1;
        char *flag = NULL;

        ret = dict_get_str (xdata, "link-count", &flag);
        if ((ret == 0) && (strcmp (flag, GF_XATTROP_INDEX_COUNT) == 0)) {
                STACK_WIND (frame, index_fstat_cbk, FIRST_CHILD(this),
                            FIRST_CHILD(this)->fops->fstat, fd, xdata);
        } else {
                STACK_WIND (frame, default_fstat_cbk, FIRST_CHILD(this),
                            FIRST_CHILD(this)->fops->fstat, fd, xdata);
        }

        return 0;
}

int32_t
index_opendir (call_frame_t *frame, xlator_t *this,
               loc_t *loc, fd_t *fd, dict_t *xdata)
{
        index_priv_t    *priv = NULL;

        priv = this->private;
        if (gf_uuid_compare (fd->inode->gfid, priv->xattrop_vgfid) &&
            gf_uuid_compare (fd->inode->gfid, priv->dirty_vgfid))
                goto normal;

        frame->local = NULL;
        STACK_UNWIND_STRICT (opendir, frame, 0, 0, fd, NULL);
        return 0;

normal:
        STACK_WIND (frame, default_opendir_cbk, FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->opendir, loc, fd, xdata);
        return 0;
}

int32_t
index_readdir (call_frame_t *frame, xlator_t *this,
               fd_t *fd, size_t size, off_t off, dict_t *xdata)
{
        call_stub_t     *stub = NULL;
        index_priv_t    *priv = NULL;

        priv = this->private;
        if (gf_uuid_compare (fd->inode->gfid, priv->xattrop_vgfid) &&
            gf_uuid_compare (fd->inode->gfid, priv->dirty_vgfid))
                goto out;
        stub = fop_readdir_stub (frame, index_readdir_wrapper, fd, size, off,
                                 xdata);
        if (!stub) {
                STACK_UNWIND_STRICT (readdir, frame, -1, ENOMEM, NULL, NULL);
                return 0;
        }
        worker_enqueue (this, stub);
        return 0;
out:
        STACK_WIND (frame, default_readdir_cbk, FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->readdir, fd, size, off, xdata);
        return 0;
}

int
index_unlink (call_frame_t *frame, xlator_t *this, loc_t *loc, int xflag,
              dict_t *xdata)
{
        call_stub_t     *stub = NULL;
        index_priv_t    *priv = NULL;

        priv = this->private;
        if (gf_uuid_compare (loc->pargfid, priv->xattrop_vgfid) &&
            gf_uuid_compare (loc->pargfid, priv->dirty_vgfid))
                goto out;

        stub = fop_unlink_stub (frame, index_unlink_wrapper, loc, xflag, xdata);
        if (!stub) {
                STACK_UNWIND_STRICT (unlink, frame, -1, ENOMEM, NULL, NULL,
                                     NULL);
                return 0;
        }
        worker_enqueue (this, stub);
        return 0;
out:
        STACK_WIND (frame, default_unlink_cbk, FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->unlink, loc, xflag, xdata);
        return 0;
}

int
index_make_xattrop_watchlist (xlator_t *this, index_priv_t *priv,
                              char *watchlist, index_xattrop_type_t type)
{
        char   *delim         = NULL;
        char   *dup_watchlist = NULL;
        char   *key           = NULL;
        char   *saveptr       = NULL;
        dict_t *xattrs        = NULL;
        data_t *dummy         = NULL;
        int    ret            = 0;

        if (!watchlist)
                return 0;

        dup_watchlist = gf_strdup (watchlist);
        if (!dup_watchlist)
                return -1;

        xattrs = dict_new ();
        if (!xattrs) {
                ret = -1;
                goto out;
        }

        dummy = int_to_data (1);
        if (!dummy) {
                ret = -1;
                goto out;
        }

        data_ref (dummy);

        delim = ",";
        key = strtok_r (dup_watchlist, delim, &saveptr);
        while (key) {
                if (strlen (key) == 0) {
                        ret = -1;
                        goto out;
                }

                ret = dict_set (xattrs, key, dummy);
                if (ret)
                        goto out;

                key = strtok_r (NULL, delim, &saveptr);
        }

        switch (type) {
        case DIRTY:
                priv->dirty_watchlist = xattrs;
                break;
        case PENDING:
                priv->pending_watchlist = xattrs;
                break;
        default:
                break;
        }
        xattrs = NULL;

        ret = 0;
out:
        if (xattrs)
                dict_unref (xattrs);

        GF_FREE (dup_watchlist);

        if (dummy)
                data_unref (dummy);

        return ret;
}

int32_t
mem_acct_init (xlator_t *this)
{
        int     ret = -1;

        ret = xlator_mem_acct_init (this, gf_index_mt_end + 1);

        return ret;
}

int
init (xlator_t *this)
{
        int ret = -1;
        int64_t count = -1;
        index_priv_t *priv = NULL;
        pthread_t thread;
        pthread_attr_t  w_attr;
        gf_boolean_t    mutex_inited = _gf_false;
        gf_boolean_t    cond_inited  = _gf_false;
        gf_boolean_t    attr_inited  = _gf_false;
        char            *watchlist = NULL;
        char            *dirtylist = NULL;
        char            *pendinglist = NULL;

	if (!this->children || this->children->next) {
		gf_log (this->name, GF_LOG_ERROR,
			"'index' not configured with exactly one child");
                goto out;
	}

	if (!this->parents) {
		gf_log (this->name, GF_LOG_WARNING,
			"dangling volume. check volfile ");
	}

        priv = GF_CALLOC (1, sizeof (*priv), gf_index_mt_priv_t);
        if (!priv)
                goto out;

        LOCK_INIT (&priv->lock);
        if ((ret = pthread_cond_init(&priv->cond, NULL)) != 0) {
                gf_log (this->name, GF_LOG_ERROR,
                        "pthread_cond_init failed (%d)", ret);
                goto out;
        }
        cond_inited = _gf_true;

        if ((ret = pthread_mutex_init(&priv->mutex, NULL)) != 0) {
                gf_log (this->name, GF_LOG_ERROR,
                        "pthread_mutex_init failed (%d)", ret);
                goto out;
        }
        mutex_inited = _gf_true;

        if ((ret = pthread_attr_init (&w_attr)) != 0) {
                gf_log (this->name, GF_LOG_ERROR,
                        "pthread_attr_init failed (%d)", ret);
                goto out;
        }
        attr_inited = _gf_true;

        ret = pthread_attr_setstacksize (&w_attr, INDEX_THREAD_STACK_SIZE);
        if (ret == EINVAL) {
                gf_log (this->name, GF_LOG_WARNING,
                        "Using default thread stack size");
        }

        GF_OPTION_INIT ("index-base", priv->index_basepath, path, out);

        GF_OPTION_INIT ("xattrop64-watchlist", watchlist, str, out);
        ret = index_make_xattrop_watchlist (this, priv, watchlist,
                                            PENDING);
        if (ret)
                goto out;

        GF_OPTION_INIT ("xattrop-dirty-watchlist", dirtylist, str, out);
        ret = index_make_xattrop_watchlist (this, priv, dirtylist,
                                            DIRTY);
        if (ret)
                goto out;

        GF_OPTION_INIT ("xattrop-pending-watchlist", pendinglist, str, out);
        ret = index_make_xattrop_watchlist (this, priv, pendinglist,
                                            PENDING);
        if (ret)
                goto out;

        if (priv->dirty_watchlist)
                priv->complete_watchlist = dict_copy_with_ref (priv->dirty_watchlist,
                                                      priv->complete_watchlist);
        if (priv->pending_watchlist)
                priv->complete_watchlist = dict_copy_with_ref (priv->pending_watchlist,
                                                      priv->complete_watchlist);
        gf_uuid_generate (priv->index);
        gf_uuid_generate (priv->xattrop_vgfid);
        gf_uuid_generate (priv->dirty_vgfid);
        INIT_LIST_HEAD (&priv->callstubs);

        this->private = priv;

        ret = index_dir_create (this, XATTROP_SUBDIR);
        if (ret < 0)
                goto out;

        if (priv->dirty_watchlist) {
                ret = index_dir_create (this, DIRTY_SUBDIR);
                if (ret < 0)
                        goto out;
        }

        /*init indices files counts*/
        count = index_fetch_link_count (this, PENDING);
        index_set_link_count (priv, count, PENDING);

        ret = gf_thread_create (&thread, &w_attr, index_worker, this);
        if (ret) {
                gf_log (this->name, GF_LOG_WARNING, "Failed to create "
                        "worker thread, aborting");
                goto out;
        }

        ret = 0;
out:
        if (ret) {
                if (cond_inited)
                        pthread_cond_destroy (&priv->cond);
                if (mutex_inited)
                        pthread_mutex_destroy (&priv->mutex);
                if (priv && priv->dirty_watchlist)
                        dict_unref (priv->dirty_watchlist);
                if (priv && priv->pending_watchlist)
                        dict_unref (priv->pending_watchlist);
                if (priv && priv->complete_watchlist)
                        dict_unref (priv->complete_watchlist);
                if (priv)
                        GF_FREE (priv);
                this->private = NULL;
        }

        if (attr_inited)
                pthread_attr_destroy (&w_attr);
        return ret;
}

void
fini (xlator_t *this)
{
        index_priv_t *priv = NULL;

        priv = this->private;
        if (!priv)
                goto out;
        this->private = NULL;
        LOCK_DESTROY (&priv->lock);
        pthread_cond_destroy (&priv->cond);
        pthread_mutex_destroy (&priv->mutex);
        if (priv->dirty_watchlist)
                dict_unref (priv->dirty_watchlist);
        if (priv->pending_watchlist)
                dict_unref (priv->pending_watchlist);
        if (priv->complete_watchlist)
                dict_unref (priv->complete_watchlist);
        GF_FREE (priv);
out:
        return;
}

int
index_forget (xlator_t *this, inode_t *inode)
{
        uint64_t tmp_cache = 0;
        if (!inode_ctx_del (inode, this, &tmp_cache))
                GF_FREE ((index_inode_ctx_t*) (long)tmp_cache);

        return 0;
}

int32_t
index_releasedir (xlator_t *this, fd_t *fd)
{
        index_fd_ctx_t *fctx = NULL;
        uint64_t        ctx = 0;
        int             ret = 0;

        ret = fd_ctx_del (fd, this, &ctx);
        if (ret < 0)
                goto out;

        fctx = (index_fd_ctx_t*) (long) ctx;
        if (fctx->dir) {
                ret = sys_closedir (fctx->dir);
                if (ret)
                        gf_log (this->name, GF_LOG_ERROR, "closedir error: %s", strerror (errno));
        }

        GF_FREE (fctx);
out:
        return 0;
}

int32_t
index_release (xlator_t *this, fd_t *fd)
{
        index_fd_ctx_t *fctx = NULL;
        uint64_t        ctx = 0;
        int             ret = 0;

        ret = fd_ctx_del (fd, this, &ctx);
        if (ret < 0)
                goto out;

        fctx = (index_fd_ctx_t*) (long) ctx;
        GF_FREE (fctx);
out:
        return 0;
}

int
notify (xlator_t *this, int event, void *data, ...)
{
        int     ret = 0;
        ret = default_notify (this, event, data);
        return ret;
}

struct xlator_fops fops = {
	.xattrop     = index_xattrop,
	.fxattrop    = index_fxattrop,

        //interface functions follow
        .getxattr    = index_getxattr,
        .lookup      = index_lookup,
        .opendir     = index_opendir,
        .readdir     = index_readdir,
        .unlink      = index_unlink,
        .fstat       = index_fstat,
};

struct xlator_dumpops dumpops;

struct xlator_cbks cbks = {
        .forget         = index_forget,
        .release        = index_release,
        .releasedir     = index_releasedir
};

struct volume_options options[] = {
        { .key  = {"index-base" },
          .type = GF_OPTION_TYPE_PATH,
          .description = "path where the index files need to be stored",
        },
        { .key  = {"xattrop64-watchlist" },
          .type = GF_OPTION_TYPE_STR,
          .description = "Comma separated list of xattrs that are watched",
        },
        { .key  = {"xattrop-dirty-watchlist" },
          .type = GF_OPTION_TYPE_STR,
          .description = "Comma separated list of xattrs that are watched",
        },
        { .key  = {"xattrop-pending-watchlist" },
          .type = GF_OPTION_TYPE_STR,
          .description = "Comma separated list of xattrs that are watched",
        },
        { .key  = {NULL} },
};
