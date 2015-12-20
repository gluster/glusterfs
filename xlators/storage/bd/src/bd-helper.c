#include <lvm2app.h>
#ifdef HAVE_LIBAIO
#include <libaio.h>
#endif
#include <linux/fs.h>
#include <sys/ioctl.h>
#include "bd.h"
#include "bd-mem-types.h"
#include "run.h"
#include "lvm-defaults.h"
#include "syscall.h"

int
bd_inode_ctx_set (inode_t *inode, xlator_t *this, bd_attr_t *ctx)
{
        int       ret  = -1;
        uint64_t  ctx_int = 0;

        GF_VALIDATE_OR_GOTO (this->name, inode, out);
        GF_VALIDATE_OR_GOTO (this->name, ctx, out);

        ctx_int = (long)ctx;
        ret = inode_ctx_set (inode, this, &ctx_int);
out:
        return ret;
}

int
bd_inode_ctx_get (inode_t *inode, xlator_t *this, bd_attr_t **ctx)
{
        int       ret     = -1;
        uint64_t  ctx_int = 0;

        GF_VALIDATE_OR_GOTO (this->name, inode, out);
        ret = inode_ctx_get (inode, this, &ctx_int);
        if (ret)
                return ret;
        if (ctx)
                *ctx = (bd_attr_t *) ctx_int;
out:
        return ret;
}

void
bd_local_free (xlator_t *this, bd_local_t *local)
{
        if (!local)
                return;
        if (local->fd)
                fd_unref (local->fd);
        else if (local->loc.path)
                loc_wipe (&local->loc);
        if (local->dict)
                dict_unref (local->dict);
        if (local->inode)
                inode_unref (local->inode);
        if (local->bdatt) {
                GF_FREE (local->bdatt->type);
                GF_FREE (local->bdatt);
        }
        mem_put (local);
        local = NULL;
}

bd_local_t *
bd_local_init (call_frame_t *frame, xlator_t *this)
{
        frame->local = mem_get0 (this->local_pool);
        if (!frame->local)
                return NULL;

        return frame->local;
}

/*
 * VG are set with the tag in GF_XATTR_VOL_ID_KEY:<uuid> format.
 * This function validates this tag agains volume-uuid. Also goes
 * through LV list to find out if a thin-pool is configured or not.
 */
int bd_scan_vg (xlator_t *this, bd_priv_t *priv)
{
        vg_t                   brick      = NULL;
        data_t                *tmp_data   = NULL;
        struct dm_list        *tags       = NULL;
        int                    op_ret     = -1;
        uuid_t                 dict_uuid  = {0, };
        uuid_t                 vg_uuid    = {0, };
        gf_boolean_t           uuid       = _gf_false;
        lvm_str_list_t        *strl       = NULL;
        struct dm_list        *lv_dm_list = NULL;
        lv_list_t             *lv_list    = NULL;
        struct dm_list        *dm_seglist = NULL;
        lvseg_list_t          *seglist    = NULL;
        lvm_property_value_t   prop       = {0, };
        gf_boolean_t           thin       = _gf_false;
        const char            *lv_name    = NULL;

        brick = lvm_vg_open (priv->handle, priv->vg, "w", 0);
        if (!brick) {
                gf_log (this->name, GF_LOG_CRITICAL, "VG %s is not found",
                        priv->vg);
                return ENOENT;
        }

        lv_dm_list = lvm_vg_list_lvs (brick);
        if (!lv_dm_list)
                goto check;

        dm_list_iterate_items (lv_list, lv_dm_list) {
                dm_seglist = lvm_lv_list_lvsegs (lv_list->lv);
                if (!dm_seglist)
                        continue;
                dm_list_iterate_items (seglist, dm_seglist) {
                        prop = lvm_lvseg_get_property (seglist->lvseg,
                                                       "segtype");
                        if (!prop.is_valid || !prop.value.string)
                                continue;
                        if (!strcmp (prop.value.string, "thin-pool")) {
                                thin = _gf_true;
                                lv_name = lvm_lv_get_name (lv_list->lv);
                                priv->pool = gf_strdup (lv_name);
                                gf_log (THIS->name, GF_LOG_INFO, "Thin Pool "
                                        "\"%s\" will be used for thin LVs",
                                        lv_name);
                                break;
                        }
                }
        }

check:
        /* If there is no volume-id set in dict, we cant validate */
        tmp_data = dict_get (this->options, "volume-id");
        if (!tmp_data) {
                op_ret = 0;
                goto out;
        }

        op_ret = gf_uuid_parse (tmp_data->data, dict_uuid);
        if (op_ret < 0) {
                gf_log (this->name, GF_LOG_ERROR,
                        "wrong volume-id (%s) set in volume file",
                        tmp_data->data);
                op_ret = -1;
                goto out;
        }

        tags = lvm_vg_get_tags (brick);
        if (!tags) { /* no tags in the VG */
                gf_log (this->name, GF_LOG_ERROR,
                        "Extended attribute trusted.glusterfs."
                        "volume-id is absent");
                op_ret = -1;
                goto out;
        }
        dm_list_iterate_items (strl, tags) {
                if (!strncmp (strl->str, GF_XATTR_VOL_ID_KEY,
                              strlen (GF_XATTR_VOL_ID_KEY))) {
                        uuid = _gf_true;
                        break;
                }
        }
        /* UUID tag is not set in VG */
        if (!uuid) {
                gf_log (this->name, GF_LOG_ERROR,
                        "Extended attribute trusted.glusterfs."
                        "volume-id is absent");
                op_ret = -1;
                goto out;
        }

        op_ret = gf_uuid_parse (strl->str + strlen (GF_XATTR_VOL_ID_KEY) + 1,
                             vg_uuid);
        if (op_ret < 0) {
                        gf_log (this->name, GF_LOG_ERROR,
                                "wrong volume-id (%s) set in VG", strl->str);
                        op_ret = -1;
                        goto out;
        }
        if (gf_uuid_compare (dict_uuid, vg_uuid)) {
                gf_log (this->name, GF_LOG_ERROR,
                        "mismatching volume-id (%s) received. "
                        "already is a part of volume %s ",
                        tmp_data->data, vg_uuid);
                op_ret = -1;
                goto out;
        }

        op_ret = 0;

out:
        lvm_vg_close (brick);

        if (!thin)
                gf_log (THIS->name, GF_LOG_WARNING, "No thin pool found in "
                        "VG %s\n", priv->vg);
        else
                priv->caps |= BD_CAPS_THIN;

        return op_ret;
}

/* FIXME: Move this code to common place, so posix and bd xlator can use */
char *
page_aligned_alloc (size_t size, char **aligned_buf)
{
        char    *alloc_buf = NULL;
        char    *buf       = NULL;

        alloc_buf = GF_CALLOC (1, (size + ALIGN_SIZE), gf_common_mt_char);
        if (!alloc_buf)
                return NULL;
        /* page aligned buffer */
        buf = GF_ALIGN_BUF (alloc_buf, ALIGN_SIZE);
        *aligned_buf = buf;

        return alloc_buf;
}

static int
__bd_fd_ctx_get (xlator_t *this, fd_t *fd, bd_fd_t **bdfd_p)
{
        int         ret      = -1;
        int         _fd      = -1;
        char       *devpath  = NULL;
        bd_fd_t    *bdfd     = NULL;
        uint64_t    tmp_bdfd = 0;
        bd_priv_t  *priv     = this->private;
        bd_gfid_t   gfid     = {0, };
        bd_attr_t  *bdatt    = NULL;

        /* not bd file */
        if (fd->inode->ia_type != IA_IFREG ||
            bd_inode_ctx_get (fd->inode, this, &bdatt))
                return 0;

        ret = __fd_ctx_get (fd, this, &tmp_bdfd);
        if (ret == 0) {
                bdfd = (void *)(long) tmp_bdfd;
                *bdfd_p = bdfd;
                return 0;
        }

        uuid_utoa_r (fd->inode->gfid, gfid);
        gf_asprintf (&devpath, "/dev/%s/%s", priv->vg, gfid);
        if (!devpath)
                goto out;

        _fd = open (devpath, O_RDWR | O_LARGEFILE, 0);
        if (_fd < 0) {
                ret = errno;
                gf_log (this->name, GF_LOG_ERROR, "open on %s: %s", devpath,
                        strerror (ret));
                goto out;
        }
        bdfd = GF_CALLOC (1, sizeof(bd_fd_t), gf_bd_fd);
        BD_VALIDATE_MEM_ALLOC (bdfd, ret, out);

        bdfd->fd = _fd;
        bdfd->flag = O_RDWR | O_LARGEFILE;
        if (__fd_ctx_set (fd, this, (uint64_t)(long)bdfd) < 0) {
                gf_log (this->name, GF_LOG_WARNING,
                        "failed to set the fd context fd=%p", fd);
                goto out;
        }

        *bdfd_p = bdfd;

        ret = 0;
out:
        GF_FREE (devpath);
        if (ret) {
                if (_fd >= 0)
                        sys_close (_fd);
                GF_FREE (bdfd);
        }
        return ret;
}

int
bd_fd_ctx_get (xlator_t *this, fd_t *fd, bd_fd_t **bdfd)
{
        int   ret;

        /* FIXME: Is it ok to fd->lock here ? */
        LOCK (&fd->lock);
        {
                ret = __bd_fd_ctx_get (this, fd, bdfd);
        }
        UNLOCK (&fd->lock);

        return ret;
}

/*
 * Validates if LV exists for given inode or not.
 * Returns 0 if LV exists and size also matches.
 * If LV does not exist -1 returned
 * If LV size mismatches, returnes 1 also lv_size is updated with actual
 * size
 */
int
bd_validate_bd_xattr (xlator_t *this, char *bd, char **type,
                      uint64_t *lv_size, uuid_t uuid)
{
        char       *path  = NULL;
        int         ret   = -1;
        bd_gfid_t   gfid  = {0, };
        bd_priv_t  *priv  = this->private;
        struct stat stbuf = {0, };
        uint64_t    size  = 0;
        vg_t        vg    = NULL;
        lv_t        lv    = NULL;
        char     *bytes = NULL;

        bytes = strrchr (bd, ':');
        if (bytes) {
                *bytes = '\0';
                bytes++;
                gf_string2bytesize (bytes, &size);
        }

        if (strcmp (bd, BD_LV) && strcmp (bd, BD_THIN)) {
                gf_log (this->name, GF_LOG_WARNING,
                        "invalid xattr %s", bd);
                return -1;
        }
        *type = gf_strdup (bd);

        /*
         * Check if LV really exist, there could be a failure
         * after setxattr and successful LV creation
         */
        uuid_utoa_r (uuid, gfid);
        gf_asprintf (&path, "/dev/%s/%s", priv->vg, gfid);
        if (!path) {
                gf_log (this->name, GF_LOG_WARNING,
                        "insufficient memory");
                return 0;
        }

        /* Destination file does not exist */
        if (sys_stat (path, &stbuf)) {
                gf_log (this->name, GF_LOG_WARNING,
                        "lstat failed for path %s", path);
                return -1;
        }

        vg = lvm_vg_open (priv->handle, priv->vg, "r", 0);
        if (!vg) {
                gf_log (this->name, GF_LOG_WARNING,
                        "VG %s does not exist?", priv->vg);
                ret = -1;
                goto out;
        }

        lv = lvm_lv_from_name (vg, gfid);
        if (!lv) {
                gf_log (this->name, GF_LOG_WARNING,
                        "LV %s does not exist", gfid);
                ret = -1;
                goto out;
        }

        *lv_size = lvm_lv_get_size (lv);
        if (size == *lv_size) {
                ret = 0;
                goto out;
        }

        ret = 1;

out:
        if (vg)
                lvm_vg_close (vg);

        GF_FREE (path);
        return ret;
}

static int
create_thin_lv (char *vg, char *pool, char *lv, uint64_t extent)
{
        int         ret    = -1;
        runner_t    runner = {0, };
        char       *path   = NULL;
        struct stat stat   = {0, };

        runinit (&runner);
        runner_add_args  (&runner, LVM_CREATE, NULL);
        runner_add_args  (&runner, "--thin", NULL);
        runner_argprintf (&runner, "%s/%s", vg, pool);
        runner_add_args  (&runner, "--name", NULL);
        runner_argprintf (&runner, "%s", lv);
        runner_add_args  (&runner, "--virtualsize", NULL);
        runner_argprintf (&runner, "%ldB", extent);
        runner_start (&runner);
        runner_end (&runner);

        gf_asprintf (&path, "/dev/%s/%s", vg, lv);
        if (!path) {
                ret = ENOMEM;
                goto out;
        }
        if (sys_lstat (path, &stat) < 0)
                ret = EAGAIN;
        else
                ret = 0;
out:
        GF_FREE (path);
        return ret;
}

int
bd_create (uuid_t uuid, uint64_t size, char *type, bd_priv_t *priv)
{
        int       ret  = 0;
        vg_t      vg   = NULL;
        bd_gfid_t gfid = {0, };

        uuid_utoa_r (uuid, gfid);

        if (!strcmp (type, BD_THIN))
                return create_thin_lv (priv->vg, priv->pool, gfid,
                                       size);

        vg = lvm_vg_open (priv->handle, priv->vg,  "w", 0);
        if (!vg) {
                gf_log (THIS->name, GF_LOG_WARNING, "opening VG %s failed",
                        priv->vg);
                return ENOENT;
        }

        if (!lvm_vg_create_lv_linear (vg, gfid, size)) {
                gf_log (THIS->name, GF_LOG_WARNING, "lvm_vg_create_lv_linear "
                        "failed");
                ret = errno;
        }

        lvm_vg_close (vg);

        return ret;
}

int32_t
bd_resize (bd_priv_t *priv, uuid_t uuid, size_t size)
{
        uint64_t        new_size  = 0;
        runner_t        runner    = {0, };
        bd_gfid_t       gfid      = {0, };
        int             ret       = 0;
        vg_t            vg        = NULL;
        lv_t            lv        = NULL;

        uuid_utoa_r (uuid, gfid);

        runinit (&runner);

        runner_add_args  (&runner, LVM_RESIZE, NULL);
        runner_argprintf (&runner, "%s/%s", priv->vg, gfid);
        runner_argprintf (&runner, "-L%ldb", size);
        runner_add_args  (&runner, "-f", NULL);

        runner_start (&runner);
        runner_end (&runner);

        vg = lvm_vg_open (priv->handle, priv->vg, "w", 0);
        if (!vg) {
                gf_log (THIS->name, GF_LOG_WARNING, "opening VG %s failed",
                        priv->vg);
                return EAGAIN;
        }

        lv = lvm_lv_from_name (vg, gfid);
        if (!lv) {
                gf_log (THIS->name, GF_LOG_WARNING, "LV %s not found", gfid);
                ret = EIO;
                goto out;
        }
        new_size = lvm_lv_get_size (lv);

        if (new_size != size) {
                gf_log (THIS->name, GF_LOG_WARNING,
                        "resized LV size %" PRIu64 " does "
                        "not match requested size %zd", new_size, size);
                ret = EIO;
        }

out:
        lvm_vg_close (vg);
        return ret;
}

uint64_t
bd_get_default_extent (bd_priv_t *priv)
{
        vg_t   vg = NULL;
        uint64_t size = 0;

        vg = lvm_vg_open (priv->handle, priv->vg,  "w", 0);
        if (!vg) {
                gf_log (THIS->name, GF_LOG_WARNING, "opening VG %s failed",
                        priv->vg);
                return 0;
        }

        size = lvm_vg_get_extent_size (vg);

        lvm_vg_close (vg);

        return size;
}

/*
 * Adjusts the user specified size to VG specific extent size
 */
uint64_t
bd_adjust_size (bd_priv_t *priv, size_t size)
{
        uint64_t extent = 0;
        uint64_t nr_ex  = 0;

        extent = bd_get_default_extent (priv);
        if (!extent)
                return 0;

        nr_ex = size / extent;
        if (size % extent)
                nr_ex++;

        size = extent * nr_ex;

        return size;
}

int
bd_delete_lv (bd_priv_t *priv, const char *lv_name, int *op_errno)
{
        vg_t    vg  = NULL;
        lv_t    lv  = NULL;
        int     ret = -1;

        *op_errno = 0;
        vg = lvm_vg_open (priv->handle, priv->vg, "w", 0);
        if (!vg) {
                gf_log (THIS->name, GF_LOG_WARNING, "opening VG %s failed",
                        priv->vg);
                *op_errno = ENOENT;
                return -1;
        }
        lv = lvm_lv_from_name (vg, lv_name);
        if (!lv) {
                gf_log (THIS->name, GF_LOG_WARNING, "No such LV %s", lv_name);
                *op_errno = ENOENT;
                goto out;
        }
        ret = lvm_vg_remove_lv (lv);
        if (ret < 0) {
                gf_log (THIS->name, GF_LOG_WARNING, "removing LV %s failed",
                        lv_name);
                *op_errno = errno;
                goto out;
        }
out:
        lvm_vg_close (vg);

        return ret;
}

void
bd_update_amtime(struct iatt *iatt, int flag)
{
        struct timespec ts = {0, };

        clock_gettime (CLOCK_REALTIME, &ts);
        if (flag & GF_SET_ATTR_ATIME) {
                iatt->ia_atime = ts.tv_sec;
                iatt->ia_atime_nsec = ts.tv_nsec;
        }
        if (flag & GF_SET_ATTR_MTIME) {
                iatt->ia_mtime = ts.tv_sec;
                iatt->ia_mtime_nsec = ts.tv_nsec;
        }
}

int
bd_snapshot_create (bd_local_t *local, bd_priv_t *priv)
{
        char       *path   = NULL;
        bd_gfid_t   dest   = {0, };
        bd_gfid_t   origin = {0, };
        int         ret    = 0;
        runner_t    runner = {0, };
        struct stat stat   = {0, };

        uuid_utoa_r (local->dloc->gfid, dest);
        uuid_utoa_r (local->loc.gfid, origin);

        gf_asprintf (&path, "/dev/%s/%s", priv->vg, dest);
        if (!path) {
                gf_log (THIS->name, GF_LOG_WARNING,
                        "Insufficient memory");
                return ENOMEM;
        }

        runinit (&runner);
        runner_add_args  (&runner, LVM_CREATE, NULL);
        runner_add_args  (&runner, "--snapshot", NULL);
        runner_argprintf (&runner, "/dev/%s/%s", priv->vg, origin);
        runner_add_args  (&runner, "--name", NULL);
        runner_argprintf (&runner, "%s", dest);
        if (strcmp (local->bdatt->type, BD_THIN))
                runner_argprintf (&runner, "-L%ldB", local->size);
        runner_start (&runner);
        runner_end (&runner);

        if (sys_lstat (path, &stat) < 0)
                ret = EIO;

        GF_FREE (path);
        return ret;
}

int
bd_clone (bd_local_t *local, bd_priv_t *priv)
{
        int           ret          = ENOMEM;
        int           fd1          = -1;
        int           fd2          = -1;
        int           i            = 0;
        char         *buff         = NULL;
        ssize_t       bytes        = 0;
        char         *spath        = NULL;
        char         *dpath        = NULL;
        struct iovec *vec          = NULL;
        bd_gfid_t     source       = {0, };
        bd_gfid_t     dest         = {0, };
        void         *bufp[IOV_NR] = {0, };

        vec = GF_CALLOC (IOV_NR, sizeof (struct iovec), gf_common_mt_iovec);
        if (!vec)
                return ENOMEM;

        for (i = 0; i < IOV_NR; i++) {
                bufp[i] = page_aligned_alloc (IOV_SIZE, &buff);
                if (!buff)
                        goto out;
                vec[i].iov_base = buff;
                vec[i].iov_len = IOV_SIZE;
        }

        uuid_utoa_r (local->loc.gfid, source);
        uuid_utoa_r (local->dloc->gfid, dest);

        gf_asprintf (&spath, "/dev/%s/%s", priv->vg, source);
        gf_asprintf (&dpath, "/dev/%s/%s", priv->vg, dest);
        if (!spath || !dpath)
                goto out;

        ret = bd_create (local->dloc->gfid, local->size,
                         local->bdatt->type,  priv);
        if (ret)
                goto out;

        fd1 = open (spath, O_RDONLY | O_DIRECT);
        if (fd1 < 0) {
                ret = errno;
                goto out;
        }
        fd2 = open (dpath, O_WRONLY | O_DIRECT);
        if (fd2 < 0) {
                ret = errno;
                goto out;
        }

        while (1) {
                bytes = sys_readv (fd1, vec, IOV_NR);
                if (bytes < 0) {
                        ret = errno;
                        gf_log (THIS->name, GF_LOG_WARNING, "read failed: %s",
                                strerror (ret));
                        goto out;
                }
                if (!bytes)
                        break;
                bytes = sys_writev (fd2, vec, IOV_NR);
                if (bytes < 0) {
                        ret = errno;
                        gf_log (THIS->name, GF_LOG_WARNING,
                                "write failed: %s", strerror (ret));
                        goto out;
                }
        }
        ret = 0;

out:
        for (i = 0; i < IOV_NR; i++)
                GF_FREE (bufp[i]);
        GF_FREE (vec);

        if (fd1 != -1)
                sys_close (fd1);
        if (fd2 != -1)
                sys_close (fd2);

        GF_FREE (spath);
        GF_FREE (dpath);

        return ret;
}

/*
 * Merges snapshot LV to origin LV and returns status
 */
int
bd_merge (bd_priv_t *priv, uuid_t gfid)
{
        bd_gfid_t   dest   = {0, };
        char       *path   = NULL;
        struct stat stat   = {0, };
        runner_t    runner = {0, };
        int         ret    = 0;

        uuid_utoa_r (gfid, dest);
        gf_asprintf (&path, "/dev/%s/%s", priv->vg, dest);

        runinit (&runner);
        runner_add_args (&runner, LVM_CONVERT, NULL);
        runner_add_args (&runner, "--merge", NULL);
        runner_argprintf (&runner, "%s", path);
        runner_start (&runner);
        runner_end (&runner);

        if (!sys_lstat (path, &stat))
                ret = EIO;

        GF_FREE (path);

        return ret;
}

int
bd_get_origin (bd_priv_t *priv, loc_t *loc, fd_t *fd, dict_t *dict)
{
        vg_t                      brick      = NULL;
        lvm_property_value_t      prop       = {0, };
        lv_t                      lv         = NULL;
        int                       ret        = -1;
        bd_gfid_t                 gfid       = {0, };
        inode_t                  *inode      = NULL;
        char                     *origin     = NULL;

        brick = lvm_vg_open (priv->handle, priv->vg, "w", 0);
        if (!brick) {
                gf_log (THIS->name, GF_LOG_CRITICAL, "VG %s is not found",
                        priv->vg);
                return ENOENT;
        }

        if (fd)
                inode = fd->inode;
        else
                inode = loc->inode;

        uuid_utoa_r (inode->gfid, gfid);
        lv = lvm_lv_from_name (brick, gfid);
        if (!lv) {
                gf_log (THIS->name, GF_LOG_CRITICAL, "LV %s not found", gfid);
                ret = ENOENT;
                goto out;
        }

        prop = lvm_lv_get_property (lv, "origin");
        if (!prop.is_valid || !prop.value.string) {
                ret = ENODATA;
                goto out;
        }

        origin = gf_strdup (prop.value.string);
        ret = dict_set_dynstr (dict, BD_ORIGIN, origin);

out:
        lvm_vg_close (brick);
        return ret;
}

#ifndef BLKZEROOUT

int
bd_do_manual_zerofill (int fd, off_t offset, off_t len, int o_direct)
{
        off_t           num_vect            = 0;
        off_t           num_loop            = 1;
        int             idx                 = 0;
        int             op_ret              = -1;
        int             vect_size           = IOV_SIZE;
        off_t           remain              = 0;
        off_t           extra               = 0;
        struct iovec   *vector              = NULL;
        char           *iov_base            = NULL;
        char           *alloc_buf           = NULL;

        if (len == 0)
                return 0;

        if (len < IOV_SIZE)
                vect_size = len;

        num_vect = len / (vect_size);
        remain = len % vect_size ;

        if (num_vect > MAX_NO_VECT) {
                extra = num_vect % MAX_NO_VECT;
                num_loop = num_vect / MAX_NO_VECT;
                num_vect = MAX_NO_VECT;
        }

        vector = GF_CALLOC (num_vect, sizeof(struct iovec),
                             gf_common_mt_iovec);
        if (!vector)
                  return -1;

        if (o_direct) {
                alloc_buf = page_aligned_alloc (vect_size, &iov_base);
                if (!alloc_buf) {
                        gf_log ("bd_do_manual_zerofill", GF_LOG_DEBUG,
                                 "memory alloc failed, vect_size %d: %s",
                                  vect_size, strerror (errno));
                        GF_FREE (vector);
                        return -1;
                }
        } else {
                iov_base = GF_CALLOC (vect_size, sizeof(char),
                                        gf_common_mt_char);
                if (!iov_base) {
                        GF_FREE (vector);
                        return -1;
                 }
        }

        for (idx = 0; idx < num_vect; idx++) {
                vector[idx].iov_base = iov_base;
                vector[idx].iov_len  = vect_size;
        }

        if (sys_lseek (fd, offset, SEEK_SET) < 0) {
                op_ret = -1;
                goto err;
        }

        for (idx = 0; idx < num_loop; idx++) {
                op_ret = sys_writev (fd, vector, num_vect);
                if (op_ret < 0)
                        goto err;
        }
        if (extra) {
                op_ret = sys_writev (fd, vector, extra);
                if (op_ret < 0)
                        goto err;
        }
        if (remain) {
                vector[0].iov_len = remain;
                op_ret = sys_writev (fd, vector , 1);
                if (op_ret < 0)
                        goto err;
        }
        op_ret = 0;
err:
        if (o_direct)
                GF_FREE (alloc_buf);
        else
                GF_FREE (iov_base);
        GF_FREE (vector);
        return op_ret;
}

#else

/*
 * Issue Linux ZEROOUT ioctl to write '0' to a scsi device at given offset
 * and number of bytes. Each SCSI device's maximum write same bytes are exported
 * in sysfs file. Sending ioctl request greater than this bytes results in slow
 * performance. Read this file to get the maximum bytes and break down single
 * ZEROOUT request into multiple ZEROOUT request not exceeding maximum bytes.
 * From VG & LV name of device mapper identified and sysfs file read.
 * /sys/block/<block-device>/queue/write_same_max_bytes
 */
int
bd_do_ioctl_zerofill (bd_priv_t *priv, bd_attr_t *bdatt, int fd, char *vg,
                      off_t offset, off_t len)
{
        char      *dm           = NULL;
        char       dmname[4096] = {0, };
        char       lvname[4096] = {0, };
        char       sysfs[4096]  = {0, };
        bd_gfid_t  uuid         = {0, };
        char      *p            = NULL;
        off_t      max_bytes    = 0;
        int        sysfd        = -1;
        uint64_t   param[2]     = {0, 0};
        off_t      nr_loop      = 0;
        char       buff[16]     = {0, };

        uuid_utoa_r (bdatt->iatt.ia_gfid, uuid);
        sprintf (lvname, "/dev/%s/%s", vg, uuid);

        sys_readlink (lvname, dmname, sizeof (dmname) - 1);

        p = strrchr (dmname, '/');
        if (p)
                dm = p + 1;
        else
                dm = dmname;

        sprintf(sysfs, "/sys/block/%s/queue/write_same_max_bytes", dm);
        sysfd = open (sysfs, O_RDONLY);
        if (sysfd < 0) {
                gf_log ("bd_do_ioctl_zerofill", GF_LOG_DEBUG,
                        "sysfs file %s does not exist", lvname);
                goto skip;
        }

        sys_read (sysfd, buff, sizeof (buff));
        sys_close (sysfd);

        max_bytes = atoll (buff);

skip:
        /*
         * If requested len is less than write_same_max_bytes,
         * issue single ioctl to zeroout. Otherwise split the ioctls
         */
        if (!max_bytes || len <= max_bytes) {
                param[0] = offset;
                param[1] = len;

                if (ioctl (fd, BLKZEROOUT, param) < 0)
                        return errno;
                return 0;
        }

        /* Split ioctls to max write_same_max_bytes */
        nr_loop = len / max_bytes;
        for (; nr_loop; nr_loop--) {
                param[0] = offset;
                param[1] = max_bytes;

                if (ioctl (fd, BLKZEROOUT, param) < 0)
                        return errno;

                offset += max_bytes;
        }

        if (!(len % max_bytes))
                return 0;

        param[0] = offset;
        param[1] = len % max_bytes;

        if (ioctl (fd, BLKZEROOUT, param) < 0)
                return errno;

        return 0;
}
#endif

int
bd_do_zerofill(call_frame_t *frame, xlator_t *this, fd_t *fd,
               off_t offset, size_t len, struct iatt *prebuf,
               struct iatt *postbuf)
{
        int          ret   = -1;
        bd_fd_t     *bd_fd = NULL;
        bd_priv_t   *priv  = this->private;
        bd_attr_t   *bdatt = NULL;

        VALIDATE_OR_GOTO (frame, out);
        VALIDATE_OR_GOTO (this, out);
        VALIDATE_OR_GOTO (fd, out);
        VALIDATE_OR_GOTO (priv, out);

        ret = bd_fd_ctx_get (this, fd, &bd_fd);
        if (ret < 0) {
                gf_log (this->name, GF_LOG_DEBUG,
                        "bd_fd is NULL from fd=%p", fd);
                goto out;
        }

        bd_inode_ctx_get (fd->inode, this, &bdatt);
#ifndef BLKZEROOUT
        ret = bd_do_manual_zerofill(bd_fd->fd, offset, len,
                                    bd_fd->flag & O_DIRECT);
#else
        ret = bd_do_ioctl_zerofill(priv, bdatt, bd_fd->fd, priv->vg, offset,
                                   len);
#endif
        if (ret) {
                gf_log(this->name, GF_LOG_ERROR,
                       "zerofill failed on fd %d length %zu %s",
                       bd_fd->fd, len, strerror (ret));
                goto out;
        }

        if (bd_fd->flag & (O_SYNC|O_DSYNC)) {
                ret = sys_fsync (bd_fd->fd);
                if (ret) {
                        gf_log (this->name, GF_LOG_ERROR,
                                "fsync() in writev on fd %d failed: %s",
                                bd_fd->fd, strerror (errno));
                        return errno;
                }
        }

        memcpy (prebuf, &bdatt->iatt, sizeof (struct iatt));
        bd_update_amtime (&bdatt->iatt, GF_SET_ATTR_MTIME);
        memcpy (postbuf, &bdatt->iatt, sizeof (struct iatt));

out:

        return ret;
}
