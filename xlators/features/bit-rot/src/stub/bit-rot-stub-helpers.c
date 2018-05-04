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
 * Adds an entry to the bad objects directory.
 * @gfid: gfid of the bad object being added to the bad objects directory
 */
int
br_stub_add (xlator_t *this, uuid_t gfid)
{
        char              gfid_path[BR_PATH_MAX_PLUS] = {0};
        char              bad_gfid_path[BR_PATH_MAX_PLUS] = {0};
        int               ret = 0;
        br_stub_private_t *priv = NULL;
        struct stat       st = {0};

        priv = this->private;
        GF_ASSERT_AND_GOTO_WITH_ERROR (this->name, !gf_uuid_is_null (gfid),
                                       out, errno, EINVAL);

        snprintf (gfid_path, sizeof (gfid_path), "%s/%s",
                  priv->stub_basepath, uuid_utoa (gfid));

        ret = sys_stat (gfid_path, &st);
        if (!ret)
                goto out;
        snprintf (bad_gfid_path, sizeof (bad_gfid_path), "%s/stub-%s",
                  priv->stub_basepath, uuid_utoa (priv->bad_object_dir_gfid));

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
        char         gfid_path[BR_PATH_MAX_PLUS] = {0};

        priv = this->private;
        GF_ASSERT_AND_GOTO_WITH_ERROR (this->name, !gf_uuid_is_null (gfid),
                                       out, op_errno, EINVAL);
        snprintf (gfid_path, sizeof (gfid_path), "%s/%s",
                  priv->stub_basepath, uuid_utoa (gfid));
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
        int         ret         = 0;
        struct stat st          = {0,};
        char  oldpath[BR_PATH_MAX_PLUS] = {0};
        br_stub_private_t    *priv = NULL;

        priv = this->private;

        snprintf (oldpath, sizeof (oldpath), "%s/%s",
                  priv->export, OLD_BR_STUB_QUARANTINE_DIR);

        ret = sys_stat (fullpath, &st);
        if (!ret && !S_ISDIR (st.st_mode))
                goto error_return;
        if (ret) {
                if (errno != ENOENT)
                        goto error_return;
                ret =  sys_stat (oldpath, &st);
                if (ret)
                        ret = mkdir_p (fullpath, 0600, _gf_true);
                else
                        ret = sys_rename (oldpath, fullpath);
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
        char         fullpath[BR_PATH_MAX_PLUS] = {0,};
        char         stub_gfid_path[BR_PATH_MAX_PLUS] = {0,};

        gf_uuid_copy (priv->bad_object_dir_gfid, BR_BAD_OBJ_CONTAINER);

        strncpy (fullpath, priv->stub_basepath, sizeof (fullpath));

        snprintf (stub_gfid_path, sizeof (stub_gfid_path), "%s/stub-%s",
                  priv->stub_basepath, uuid_utoa (priv->bad_object_dir_gfid));

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
        gf_boolean_t       ver_enabled = _gf_false;

        BR_STUB_VER_ENABLED_IN_CALLPATH(frame, ver_enabled);
        priv = this->private;
        BR_STUB_VER_COND_GOTO (priv, (!ver_enabled), done);

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
        char            filepath[BR_PATH_MAX_PLUS] = {0};
        br_stub_private_t    *priv = NULL;

        priv = this->private;

        if (is_bad_gfid_file_current (filename, priv->bad_object_dir_gfid))
                return;

        snprintf (filepath, sizeof (filepath), "%s/%s",
                  priv->stub_basepath, filename);

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
        gf_boolean_t          xdata_unref    = _gf_false;
        dict_t               *dict           = NULL;

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

        dict = xdata;
        (void) br_stub_bad_objects_path (this, fd, &entries, &dict);
        if (!xdata && dict) {
                xdata = dict;
                xdata_unref = _gf_true;
        }

done:
        STACK_UNWIND_STRICT (readdir, frame, op_ret, op_errno, &entries, xdata);
        gf_dirent_free (&entries);
        if (xdata_unref)
                dict_unref (xdata);
        return 0;
}

/**
 * This function is called to mainly obtain the paths of the corrupt
 * objects (files as of now). Currently scrub status prints only the
 * gfid of the corrupted files. Reason is, bitrot-stub maintains the
 * list of the corrupted objects as entries inside the quarantine
 * directory (<brick export>/.glusterfs/quarantine)
 *
 * And the name of each entry in the qurantine directory is the gfid
 * of the corrupted object. So scrub status will just show that info.
 * But it helps the users a lot if the actual path to the object is
 * also reported. Hence the below function to get that information.
 * The function allocates a new dict to be returned (if it does not
 * get one from the caller of readdir i.e. scrubber as of now), and
 * stores the paths of each corrupted gfid there. The gfid is used as
 * the key and path is used as the value.
 *
 * NOTE: The path will be there in following situations
 * 1) gfid2path option has been enabled (posix xlator option)
 *    and the corrupted file contains the path as an extended
 *    attribute.
 * 2) If the gfid2path option is not enabled, OR if the xattr
 *    is absent, then the inode table should have it.
 *    The path will be there if a name based lookup has happened
 *    on the file which has been corrupted. With lookup a inode and
 *    dentry would be created in the inode table. And the path is
 *    constructed using the in memory inode and dentry. If a lookup
 *    has not happened OR the inode corresponding to the corrupted
 *    file does not exist in the inode table (because it got purged
 *    as lru limit of the inodes exceeded) OR a nameless lookup had
 *    happened to populate the inode in the inode table, then the
 *    path will not be printed in scrub and only the gfid will be there.
 **/
int
br_stub_bad_objects_path (xlator_t *this, fd_t *fd, gf_dirent_t *entries,
                          dict_t **dict)
{
        gf_dirent_t     *entry    = NULL;
        inode_t         *inode    = NULL;
        char            *hpath    = NULL;
        uuid_t           gfid     = {0};
        int              ret      = -1;
        dict_t          *tmp_dict = NULL;
        char             str_gfid[64] = {0};

        if (list_empty(&entries->list))
                return 0;

        tmp_dict = *dict;

        if (!tmp_dict) {
                tmp_dict = dict_new ();
                /*
                 * If the allocation of dict fails then no need treat it
                 * it as a error. This path (or function) is executed when
                 * "gluster volume bitrot <volume name> scrub status" is
                 * executed, to get the list of the corrupted objects.
                 * And the motive of this function is to get the paths of
                 * the corrupted objects. If the dict allocation fails, then
                 * the scrub status will only show the gfids of those corrupted
                 * objects (which is the behavior as of the time of this patch
                 * being worked upon). So just return and only the gfids will
                 * be shown.
                 */
                if (!tmp_dict) {
                        gf_msg (this->name, GF_LOG_ERROR, 0, BRS_MSG_NO_MEMORY,
                                "failed to allocate new dict for saving the paths "
                                "of the corrupted objects. Scrub status will only "
                                "display the gfid");
                        goto out;
                }
        }

        list_for_each_entry (entry, &entries->list, list) {
                gf_uuid_clear (gfid);
                gf_uuid_parse (entry->d_name, gfid);

                inode = inode_find (fd->inode->table, gfid);

                /* No need to check the return value here.
                 * Because @hpath is examined.
                 */
                (void) br_stub_get_path_of_gfid (this, fd->inode, inode,
                                                 gfid, &hpath);

                if (hpath) {
                        gf_msg_debug (this->name, 0, "path of the corrupted "
                                      "object (gfid: %s) is %s",
                                      uuid_utoa (gfid), hpath);
                        br_stub_entry_xattr_fill (this, hpath, entry, tmp_dict);
                } else
                        gf_msg (this->name, GF_LOG_WARNING, 0,
                                BRS_MSG_PATH_GET_FAILED,
                                "failed to get the path for the inode %s",
                                uuid_utoa_r (gfid, str_gfid));

                inode = NULL;
                hpath = NULL;
        }

         ret = 0;
         *dict = tmp_dict;

out:
         return ret;
 }

int
br_stub_get_path_of_gfid (xlator_t *this, inode_t *parent, inode_t *inode,
                          uuid_t gfid, char **path)
{
        int32_t    ret = -1;
        char       gfid_str[64] = {0};

        GF_VALIDATE_OR_GOTO ("bitrot-stub", this, out);
        GF_VALIDATE_OR_GOTO (this->name, parent, out);
        GF_VALIDATE_OR_GOTO (this->name, path, out);

        /* Above, No need to validate the @inode for hard resolution. Because
         * inode can be NULL and if it is NULL, then syncop_gfid_to_path_hard
         * will allocate a new inode and proceed. So no need to bother about
         * @inode. Because we need it only to send a syncop_getxattr call
         * from inside syncop_gfid_to_path_hard. And getxattr fetches the
         * path from the backend.
         */

        ret = syncop_gfid_to_path_hard (parent->table, FIRST_CHILD (this), gfid,
                                        inode, path, _gf_true);
        if (ret < 0)
                gf_msg (this->name, GF_LOG_WARNING, 0, BRS_MSG_PATH_GET_FAILED,
                        "failed to get the path xattr from disk for the "
                        " gfid %s. Trying to get path from the memory",
                        uuid_utoa_r (gfid, gfid_str));

        /*
         * Try with soft resolution of path if hard resolve fails. Because
         * checking the xattr on disk to get the path of a inode (or gfid)
         * is dependent on whether that option is enabled in the posix
         * xlator or not. If it is not enabled, then hard resolution by
         * checking the on disk xattr fails.
         *
         * Thus in such situations fall back to the soft resolution which
         * mainly depends on the inode_path() function. And for using
         * inode_path, @inode has to be linked i.e. a successful lookup should
         * have happened on the gfid (or the path) to link the inode to the
         * inode table. And if @inode is NULL, means, the inode has not been
         * found in the inode table and better not to do inode_path() on the
         * inode which has not been linked.
         */
        if (ret < 0 && inode) {
                ret = syncop_gfid_to_path_hard (parent->table,
                                                FIRST_CHILD (this), gfid, inode,
                                                path, _gf_false);
                if (ret < 0)
                        gf_msg (this->name, GF_LOG_WARNING, 0,
                                BRS_MSG_PATH_GET_FAILED,
                                "failed to get the path from the memory for gfid %s",
                                uuid_utoa_r (gfid, gfid_str));
        }

out:
        return ret;
}


/**
* NOTE: If the file has multiple hardlinks (in gluster volume
* namespace), the path would be one of the hardlinks. Its upto
* the user to find the remaining hardlinks (using find -samefile)
* and remove them.
**/
void
br_stub_entry_xattr_fill (xlator_t *this, char *hpath, gf_dirent_t *entry,
                          dict_t *dict)
{
                int32_t    ret = -1;

        GF_VALIDATE_OR_GOTO ("bit-rot-stub", this, out);
        GF_VALIDATE_OR_GOTO (this->name, hpath, out);

        /*
         * Use the entry->d_name (which is nothing but the gfid of the
         * corrupted object) as the key. And the value will be the actual
         * path of that object (or file).
         *
         * ALso ignore the dict_set errors. scrubber will get the gfid of
         * the corrupted object for sure. So, for now lets just log the
         * dict_set_dynstr failure and move on.
         */

        ret = dict_set_dynstr (dict, entry->d_name, hpath);
        if (ret)
                gf_msg (this->name, GF_LOG_WARNING, 0, BRS_MSG_DICT_SET_FAILED,
                        "failed to set the actual path %s as the value in the "
                        "dict for the corrupted object %s", hpath,
                        entry->d_name);
out:
        return;
}
