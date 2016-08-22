/*
   Copyright (c) 2015 Red Hat, Inc. <http://www.redhat.com>
   This file is part of GlusterFS.

   This file is licensed to you under your choice of the GNU Lesser
   General Public License, version 3 or any later version (LGPLv3 or
   later), or the GNU General Public License, version 2 (GPLv2), in all
   cases as published by the Free Software Foundation.
*/

#include "bit-rot-stub.h"

br_stub_fd_t *
br_stub_fd_new (void)
{
        br_stub_fd_t    *br_stub_fd = NULL;

        br_stub_fd = GF_CALLOC (1, sizeof (*br_stub_fd),
                                gf_br_stub_mt_br_stub_fd_t);

        return br_stub_fd;
}

int
__br_stub_fd_ctx_set (xlator_t *this, fd_t *fd, br_stub_fd_t *br_stub_fd)
{
        uint64_t    value = 0;
        int         ret   = -1;

        GF_VALIDATE_OR_GOTO ("bit-rot-stub", this, out);
        GF_VALIDATE_OR_GOTO (this->name, fd, out);
        GF_VALIDATE_OR_GOTO (this->name, br_stub_fd, out);

        value = (uint64_t)(long) br_stub_fd;

        ret = __fd_ctx_set (fd, this, value);

out:
        return ret;
}

br_stub_fd_t *
__br_stub_fd_ctx_get (xlator_t *this, fd_t *fd)
{
        br_stub_fd_t *br_stub_fd = NULL;
        uint64_t  value  = 0;
        int       ret    = -1;

        GF_VALIDATE_OR_GOTO ("bit-rot-stub", this, out);
        GF_VALIDATE_OR_GOTO (this->name, fd, out);

        ret = __fd_ctx_get (fd, this, &value);
        if (ret)
                return NULL;

        br_stub_fd = (br_stub_fd_t *) ((long) value);

out:
        return br_stub_fd;
}

br_stub_fd_t *
br_stub_fd_ctx_get (xlator_t *this, fd_t *fd)
{
        br_stub_fd_t *br_stub_fd = NULL;

        GF_VALIDATE_OR_GOTO ("bit-rot-stub", this, out);
        GF_VALIDATE_OR_GOTO (this->name, fd, out);

        LOCK (&fd->lock);
        {
                br_stub_fd = __br_stub_fd_ctx_get (this, fd);
        }
        UNLOCK (&fd->lock);

out:
        return br_stub_fd;
}

int32_t
br_stub_fd_ctx_set (xlator_t *this, fd_t *fd, br_stub_fd_t *br_stub_fd)
{
        int32_t    ret = -1;

        GF_VALIDATE_OR_GOTO ("bit-rot-stub", this, out);
        GF_VALIDATE_OR_GOTO (this->name, fd, out);
        GF_VALIDATE_OR_GOTO (this->name, br_stub_fd, out);

        LOCK (&fd->lock);
        {
                ret = __br_stub_fd_ctx_set (this, fd, br_stub_fd);
        }
        UNLOCK (&fd->lock);

out:
        return ret;
}


/**
 * prints the path to the bad object's entry into the buffer provided.
 * @priv: xlator private
 * @filename: gfid of the bad object.
 * @file_path: buffer provided into which path of the bad object is printed
 *            using above 2 arguments.
 */
static void
br_stub_link_path (br_stub_private_t *priv, const char *filename,
                        char *file_path, size_t len)
{
        snprintf (file_path, len, "%s/%s", priv->stub_basepath, filename);
}

/**
 * Prints the path of the object which acts as a container for all the bad
 * objects. Each new entry corresponding to a bad object is a hard link to
 * the object with name "stub-0000000000000008".
 * @priv: xlator's private
 * @stub_gfid_path: buffer into which the path to the container of bad objects
 *                  is printed.
 */
static void
br_stub_container_entry (br_stub_private_t *priv, char *stub_gfid_path,
                         size_t len)
{

        snprintf (stub_gfid_path, len, "%s/stub-%s", priv->stub_basepath,
                  uuid_utoa (priv->bad_object_dir_gfid));
}

/**
 * Prints the path to the bad object's entry into the buffer provided.
 * @priv: xlator private
 * @gfid: gfid of the bad object.
 * @gfid_path: buffer provided into which path of the bad object is printed
 *            using above 2 arguments.
 * This function is same as br_stub_link_path. But in this function the
 * gfid of the bad object is obtained as an argument (i.e. uuid_t gfid),
 * where as in br_stub_link_path, the gfid is received as filename
 * (i.e. char *filename)
 */
static void
br_stub_linked_entry (br_stub_private_t *priv, char *gfid_path, uuid_t gfid,
                        size_t len)
{
        snprintf (gfid_path, len, "%s/%s", priv->stub_basepath,
                  uuid_utoa (gfid));
}

/**
 * Adds an entry to the bad objects directory.
 * @gfid: gfid of the bad object being added to the bad objects directory
 */
int
br_stub_add (xlator_t *this, uuid_t gfid)
{
        char              gfid_path[PATH_MAX] = {0};
        char              bad_gfid_path[PATH_MAX] = {0};
        int               ret = 0;
        br_stub_private_t *priv = NULL;
        struct stat       st = {0};

        priv = this->private;
        GF_ASSERT_AND_GOTO_WITH_ERROR (this->name, !gf_uuid_is_null (gfid),
                                       out, errno, EINVAL);

        br_stub_linked_entry (priv, gfid_path, gfid, sizeof (gfid_path));

        ret = sys_stat (gfid_path, &st);
        if (!ret)
                goto out;
        br_stub_container_entry (priv, bad_gfid_path, sizeof (bad_gfid_path));

        ret = sys_link (bad_gfid_path, gfid_path);
        if (ret) {
                if ((errno != ENOENT) && (errno != EMLINK) && (errno != EEXIST))
                        goto out;

                /*
                 * Continue with success. At least we'll have half of the
                 * functionality, in the sense, object is marked bad and
                 * would be inaccessible. It's only scrub status that would
                 * show up less number of objects. That's fine as we'll have
                 * the log files that will have the missing information.
                 */
                gf_msg (this->name, GF_LOG_WARNING, errno, BRS_MSG_LINK_FAIL,
                        "failed to record  gfid [%s]", uuid_utoa (gfid));
        }

        return 0;
out:
        return -1;
}

int
br_stub_del (xlator_t *this, uuid_t gfid)
{
        int32_t      op_errno __attribute__((unused)) = 0;
        br_stub_private_t *priv = NULL;
        int          ret = 0;
        char         gfid_path[PATH_MAX] = {0};

        priv = this->private;
        GF_ASSERT_AND_GOTO_WITH_ERROR (this->name, !gf_uuid_is_null (gfid),
                                       out, op_errno, EINVAL);
        br_stub_linked_entry (priv, gfid_path, gfid,
                              sizeof (gfid_path));
        ret = sys_unlink (gfid_path);
        if (ret && (errno != ENOENT)) {
                gf_msg (this->name, GF_LOG_ERROR, errno,
                        BRS_MSG_BAD_OBJ_UNLINK_FAIL,
                        "%s: failed to delete bad object link from quarantine "
                        "directory", gfid_path);
                ret = -errno;
                goto out;
        }

        ret = 0;

out:
        return ret;
}

static int
br_stub_check_stub_directory (xlator_t *this, char *fullpath)
{
        int ret = 0;
        struct stat st = {0,};

        ret = sys_stat (fullpath, &st);
        if (!ret && !S_ISDIR (st.st_mode))
                goto error_return;
        if (ret) {
                if (errno != ENOENT)
                        goto error_return;
                ret = mkdir_p (fullpath, 0600, _gf_true);
        }

        if (ret)
                gf_msg (this->name, GF_LOG_ERROR, errno,
                        BRS_MSG_BAD_OBJECT_DIR_FAIL,
                        "failed to create stub directory [%s]", fullpath);
        return ret;

error_return:
        gf_msg (this->name, GF_LOG_ERROR, errno,
                BRS_MSG_BAD_OBJECT_DIR_FAIL,
                "Failed to verify stub directory [%s]", fullpath);
        return -1;
}

/**
 * Function to create the container for the bad objects within the bad objects
 * directory.
 */
static int
br_stub_check_stub_file (xlator_t *this, char *path)
{
        int ret = 0;
        int fd = -1;
        struct stat st = {0,};

        ret = sys_stat (path, &st);
        if (!ret && !S_ISREG (st.st_mode))
                goto error_return;
        if (ret) {
                if (errno != ENOENT)
                        goto error_return;
                fd = sys_creat (path, 0);
                if (fd < 0)
                        gf_msg (this->name, GF_LOG_ERROR, errno,
                                BRS_MSG_BAD_OBJECT_DIR_FAIL,
                                "Failed ot create stub file [%s]", path);
        }

        if (fd >= 0) {
                sys_close (fd);
                ret = 0;
        }

        return ret;

error_return:
        gf_msg (this->name, GF_LOG_ERROR, errno,
                BRS_MSG_BAD_OBJECT_DIR_FAIL, "Failed ot verify stub file [%s]", path);
        return -1;
}

int
br_stub_dir_create (xlator_t *this, br_stub_private_t *priv)
{
        int          ret = -1;
        char         fullpath[PATH_MAX] = {0};
        char         stub_gfid_path[PATH_MAX] = {0, };

        gf_uuid_copy (priv->bad_object_dir_gfid, BR_BAD_OBJ_CONTAINER);

        snprintf (fullpath, sizeof (fullpath), "%s", priv->stub_basepath);

        br_stub_container_entry (priv, stub_gfid_path, sizeof (stub_gfid_path));

        ret = br_stub_check_stub_directory (this, fullpath);
        if (ret)
                goto out;
        ret = br_stub_check_stub_file (this, stub_gfid_path);
        if (ret)
                goto out;

        return 0;

out:
        return -1;
}

call_stub_t *
__br_stub_dequeue (struct list_head *callstubs)
{
        call_stub_t *stub = NULL;

        if (!list_empty (callstubs)) {
                stub = list_entry (callstubs->next, call_stub_t, list);
                list_del_init (&stub->list);
        }

        return stub;
}

void
__br_stub_enqueue (struct list_head *callstubs, call_stub_t *stub)
{
        list_add_tail (&stub->list, callstubs);
}

void
br_stub_worker_enqueue (xlator_t *this, call_stub_t *stub)
{
        br_stub_private_t    *priv = NULL;

        priv = this->private;
        pthread_mutex_lock (&priv->container.bad_lock);
        {
                __br_stub_enqueue (&priv->container.bad_queue, stub);
                pthread_cond_signal (&priv->container.bad_cond);
        }
        pthread_mutex_unlock (&priv->container.bad_lock);
}

void *
br_stub_worker (void *data)
{
        br_stub_private_t     *priv = NULL;
        xlator_t         *this = NULL;
        call_stub_t      *stub = NULL;


        THIS = data;
        this = data;
        priv = this->private;

        for (;;) {
                pthread_mutex_lock (&priv->container.bad_lock);
                {
                        while (list_empty (&priv->container.bad_queue)) {
                              (void) pthread_cond_wait (&priv->container.bad_cond,
                                                        &priv->container.bad_lock);
                        }

                        stub = __br_stub_dequeue (&priv->container.bad_queue);
                }
                pthread_mutex_unlock (&priv->container.bad_lock);

                if (stub) /* guard against spurious wakeups */
                        call_resume (stub);
        }

        return NULL;
}

int32_t
br_stub_lookup_wrapper (call_frame_t *frame, xlator_t *this,
                      loc_t *loc, dict_t *xattr_req)
{
        br_stub_private_t *priv        = NULL;
        struct stat        lstatbuf    = {0};
        int                ret         = 0;
        int32_t            op_errno    = EINVAL;
        int32_t            op_ret      = -1;
        struct iatt        stbuf       = {0, };
        struct iatt        postparent  = {0,};
        dict_t            *xattr       = NULL;

        priv = this->private;

        VALIDATE_OR_GOTO (loc, done);
        if (gf_uuid_compare (loc->gfid, priv->bad_object_dir_gfid))
                goto done;

        ret = sys_lstat (priv->stub_basepath, &lstatbuf);
        if (ret) {
                gf_msg_debug (this->name, errno, "Stat failed on stub bad "
                              "object dir");
                op_errno = errno;
                goto done;
        } else if (!S_ISDIR (lstatbuf.st_mode)) {
                gf_msg_debug (this->name, errno, "bad object container is not "
                              "a directory");
                op_errno = ENOTDIR;
                goto done;
        }

        iatt_from_stat (&stbuf, &lstatbuf);
        gf_uuid_copy (stbuf.ia_gfid, priv->bad_object_dir_gfid);

        op_ret = op_errno = 0;
        xattr = dict_new ();
        if (!xattr) {
                op_ret = -1;
                op_errno = ENOMEM;
        }

done:
        STACK_UNWIND_STRICT (lookup, frame, op_ret, op_errno,
                             loc->inode, &stbuf, xattr, &postparent);
        if (xattr)
                dict_unref (xattr);
        return 0;
}

static int
is_bad_gfid_file_current (char *filename, uuid_t gfid)
{
        char current_stub_gfid[GF_UUID_BUF_SIZE + 16] = {0, };

        snprintf (current_stub_gfid, sizeof current_stub_gfid,
                  "stub-%s", uuid_utoa(gfid));
        return (!strcmp(filename, current_stub_gfid));
}

static void
check_delete_stale_bad_file (xlator_t *this, char *filename)
{
        int             ret = 0;
        struct stat     st = {0};
        char            filepath[PATH_MAX] = {0};
        br_stub_private_t    *priv = NULL;

        priv = this->private;

        if (is_bad_gfid_file_current (filename, priv->bad_object_dir_gfid))
                return;

        br_stub_link_path (priv, filename, filepath, sizeof (filepath));

        ret = sys_stat (filepath, &st);
        if (!ret && st.st_nlink == 1)
                sys_unlink (filepath);
}

static int
br_stub_fill_readdir (fd_t *fd, br_stub_fd_t *fctx, DIR *dir, off_t off,
                      size_t size, gf_dirent_t *entries)
{
        off_t          in_case = -1;
        off_t          last_off = 0;
        size_t         filled = 0;
        int            count = 0;
        int32_t        this_size      = -1;
        gf_dirent_t   *this_entry     = NULL;
        xlator_t      *this           = NULL;
        struct dirent *entry          = NULL;
        struct dirent  scratch[2]     = {{0,},};

        this = THIS;
        if (!off) {
                rewinddir (dir);
        } else {
                seekdir (dir, off);
#ifndef GF_LINUX_HOST_OS
                if ((u_long)telldir(dir) != off &&
                    off != fctx->bad_object.dir_eof) {
                        gf_msg (THIS->name, GF_LOG_ERROR, 0,
                                BRS_MSG_BAD_OBJECT_DIR_SEEK_FAIL,
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
                        gf_msg (THIS->name, GF_LOG_ERROR, 0,
                                BRS_MSG_BAD_OBJECT_DIR_TELL_FAIL,
                                "telldir failed on dir=%p: %s",
                                dir, strerror (errno));
                        goto out;
                }

                errno = 0;
                entry = sys_readdir (dir, scratch);
                if (!entry || errno != 0) {
                        if (errno == EBADF) {
                                gf_msg (THIS->name, GF_LOG_WARNING, 0,
                                        BRS_MSG_BAD_OBJECT_DIR_READ_FAIL,
                                        "readdir failed on dir=%p: %s",
                                        dir, strerror (errno));
                                goto out;
                        }
                        break;
                }

                if (!strcmp (entry->d_name, ".") ||
                    !strcmp (entry->d_name, ".."))
                        continue;

                if (!strncmp (entry->d_name, "stub-",
                              strlen ("stub-"))) {
                        check_delete_stale_bad_file (this, entry->d_name);
                        continue;
                }

                this_size = max (sizeof (gf_dirent_t),
                                 sizeof (gfs3_dirplist))
                        + strlen (entry->d_name) + 1;

                if (this_size + filled > size) {
                        seekdir (dir, in_case);
#ifndef GF_LINUX_HOST_OS
                        if ((u_long)telldir(dir) != in_case &&
                            in_case != fctx->bad_object.dir_eof) {
				gf_msg (THIS->name, GF_LOG_ERROR, 0,
                                        BRS_MSG_BAD_OBJECT_DIR_SEEK_FAIL,
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
                        gf_msg (THIS->name, GF_LOG_ERROR, 0,
                                BRS_MSG_NO_MEMORY,
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
                count++;
        }

        if ((!sys_readdir (dir, scratch) && (errno == 0))) {
                /* Indicate EOF */
                errno = ENOENT;
                /* Remember EOF offset for later detection */
                fctx->bad_object.dir_eof = last_off;
        }
out:
        return count;
}

int32_t
br_stub_readdir_wrapper (call_frame_t *frame, xlator_t *this,
                         fd_t *fd, size_t size, off_t off, dict_t *xdata)
{
        br_stub_fd_t         *fctx           = NULL;
        DIR                  *dir            = NULL;
        int                   ret            = -1;
        int32_t               op_ret         = -1;
        int32_t               op_errno       = 0;
        int                   count          = 0;
        gf_dirent_t           entries;

        INIT_LIST_HEAD (&entries.list);

        fctx = br_stub_fd_ctx_get (this, fd);
        if (!fctx) {
                gf_msg (this->name, GF_LOG_WARNING, 0,
                        BRS_MSG_GET_FD_CONTEXT_FAILED,
                        "pfd is NULL, fd=%p", fd);
                op_errno = -ret;
                goto done;
        }

        dir = fctx->bad_object.dir;

        if (!dir) {
                gf_msg (this->name, GF_LOG_WARNING, 0,
                        BRS_MSG_BAD_HANDLE_DIR_NULL,
                        "dir is NULL for fd=%p", fd);
                op_errno = EINVAL;
                goto done;
        }

        count = br_stub_fill_readdir (fd, fctx, dir, off, size, &entries);

        /* pick ENOENT to indicate EOF */
        op_errno = errno;
        op_ret = count;
done:
        STACK_UNWIND_STRICT (readdir, frame, op_ret, op_errno, &entries, xdata);
        gf_dirent_free (&entries);
        return 0;
}

