/*
  Copyright (c) 2015 Red Hat, Inc. <http://www.redhat.com>
  This file is part of GlusterFS.

  This file is licensed to you under your choice of the GNU Lesser
  General Public License, version 3 or any later version (LGPLv3 or
  later), or the GNU General Public License, version 2 (GPLv2), in all
  cases as published by the Free Software Foundation.
*/

#include "syncop.h"
#include "syncop-utils.h"
#include "common-utils.h"
#include "libglusterfs-messages.h"

struct syncop_dir_scan_data {
        xlator_t *subvol;
        loc_t *parent;
        void *data;
        gf_dirent_t *q;
        gf_dirent_t *entry;
        pthread_cond_t *cond;
        pthread_mutex_t *mut;
        syncop_dir_scan_fn_t fn;
        uint32_t *jobs_running;
        uint32_t *qlen;
        int32_t  *retval;
};

int
syncop_dirfd (xlator_t *subvol, loc_t *loc, fd_t **fd, int pid)
{
        int  ret    = 0;
        fd_t *dirfd = NULL;

        if (!fd)
                return -EINVAL;

        dirfd = fd_create (loc->inode, pid);
        if (!dirfd) {
                gf_msg (subvol->name, GF_LOG_ERROR, errno,
                        LG_MSG_FD_CREATE_FAILED, "fd_create of %s",
                        uuid_utoa (loc->gfid));
                ret = -errno;
                goto out;
        }

        ret = syncop_opendir (subvol, loc, dirfd, NULL, NULL);
        if (ret) {
        /*
         * On Linux, if the brick was not updated, opendir will
         * fail. We therefore use backward compatible code
         * that violate the standards by reusing offsets
         * in seekdir() from different DIR *, but it works on Linux.
         *
         * On other systems it never worked, hence we do not need
         * to provide backward-compatibility.
         */
#ifdef GF_LINUX_HOST_OS
                fd_unref (dirfd);
                dirfd = fd_anonymous (loc->inode);
                if (!dirfd) {
                        gf_msg (subvol->name, GF_LOG_ERROR, errno,
                                LG_MSG_FD_ANONYMOUS_FAILED, "fd_anonymous of "
                                "%s", uuid_utoa (loc->gfid));
                        ret = -errno;
                        goto out;
                }
                ret = 0;
#else /* GF_LINUX_HOST_OS */
                fd_unref (dirfd);
                gf_msg (subvol->name, GF_LOG_ERROR, errno,
                        LG_MSG_DIR_OP_FAILED, "opendir of %s",
                        uuid_utoa (loc->gfid));
                goto out;
#endif /* GF_LINUX_HOST_OS */
        } else {
                fd_bind (dirfd);
        }
out:
        if (ret == 0)
                *fd = dirfd;
        return ret;
}

int
syncop_ftw (xlator_t *subvol, loc_t *loc, int pid, void *data,
            int (*fn) (xlator_t *subvol, gf_dirent_t *entry, loc_t *parent,
                       void *data))
{
        loc_t       child_loc = {0, };
        fd_t        *fd       = NULL;
        uint64_t    offset    = 0;
        gf_dirent_t *entry    = NULL;
        int         ret       = 0;
        gf_dirent_t entries;

        ret = syncop_dirfd (subvol, loc, &fd, pid);
        if (ret)
                goto out;

        INIT_LIST_HEAD (&entries.list);

        while ((ret = syncop_readdirp (subvol, fd, 131072, offset, &entries,
                                       NULL, NULL))) {
                if (ret < 0)
                        break;

                if (ret > 0) {
                        /* If the entries are only '.', and '..' then ret
                         * value will be non-zero. so set it to zero here. */
                        ret = 0;
                }
                list_for_each_entry (entry, &entries.list, list) {
                        offset = entry->d_off;

                        if (!strcmp (entry->d_name, ".") ||
                            !strcmp (entry->d_name, ".."))
                                continue;

                        gf_link_inode_from_dirent (NULL, fd->inode, entry);

                        ret = fn (subvol, entry, loc, data);
                        if (ret)
                                break;

                        if (entry->d_stat.ia_type == IA_IFDIR) {
                                child_loc.inode = inode_ref (entry->inode);
                                gf_uuid_copy (child_loc.gfid, entry->inode->gfid);
                                ret = syncop_ftw (subvol, &child_loc,
                                                  pid, data, fn);
                                loc_wipe (&child_loc);
                                if (ret)
                                        break;
                        }
                }

                gf_dirent_free (&entries);
                if (ret)
                        break;
        }

out:
        if (fd)
                fd_unref (fd);
        return ret;
}

/**
 * Syncop_ftw_throttle can be used in a configurable way to control
 * the speed at which crawling is done. It takes 2 more arguments
 * compared to syncop_ftw.
 * After @count entries are finished in a directory (to be
 * precise, @count files) sleep for @sleep_time seconds.
 * If either @count or @sleep_time is <=0, then it behaves similar to
 * syncop_ftw.
 */
int
syncop_ftw_throttle (xlator_t *subvol, loc_t *loc, int pid, void *data,
                     int (*fn) (xlator_t *subvol, gf_dirent_t *entry,
                                loc_t *parent, void *data),
                     int count, int sleep_time)
{
        loc_t       child_loc = {0, };
        fd_t        *fd       = NULL;
        uint64_t    offset    = 0;
        gf_dirent_t *entry    = NULL;
        int         ret       = 0;
        gf_dirent_t entries;
        int         tmp       = 0;

        if (sleep_time <= 0) {
                ret = syncop_ftw (subvol, loc, pid, data, fn);
                goto out;
        }

        ret = syncop_dirfd (subvol, loc, &fd, pid);
        if (ret)
                goto out;

        INIT_LIST_HEAD (&entries.list);

        while ((ret = syncop_readdirp (subvol, fd, 131072, offset, &entries,
                                       NULL, NULL))) {
                if (ret < 0)
                        break;

                if (ret > 0) {
                        /* If the entries are only '.', and '..' then ret
                         * value will be non-zero. so set it to zero here. */
                        ret = 0;
                }

                tmp = 0;

                list_for_each_entry (entry, &entries.list, list) {
                        offset = entry->d_off;

                        if (!strcmp (entry->d_name, ".") ||
                            !strcmp (entry->d_name, ".."))
                                continue;

                        if (++tmp >= count) {
                                tmp = 0;
                                sleep (sleep_time);
                        }

                        gf_link_inode_from_dirent (NULL, fd->inode, entry);

                        ret = fn (subvol, entry, loc, data);
                        if (ret)
                                continue;

                        if (entry->d_stat.ia_type == IA_IFDIR) {
                                child_loc.inode = inode_ref (entry->inode);
                                gf_uuid_copy (child_loc.gfid, entry->inode->gfid);
                                ret = syncop_ftw_throttle (subvol, &child_loc,
                                                           pid, data, fn, count,
                                                           sleep_time);
                                loc_wipe (&child_loc);
                                if (ret)
                                        continue;
                        }
                }

                gf_dirent_free (&entries);
                if (ret)
                        break;
        }

out:
        if (fd)
                fd_unref (fd);
        return ret;
}

static void
_scan_data_destroy (struct syncop_dir_scan_data *data)
{
        GF_FREE (data);
}

static int
_dir_scan_job_fn_cbk (int ret, call_frame_t *frame, void *opaque)
{
        struct syncop_dir_scan_data *scan_data = opaque;

        _scan_data_destroy (scan_data);
        return 0;
}

static int
_dir_scan_job_fn (void *data)
{
        struct syncop_dir_scan_data *scan_data = data;
        gf_dirent_t                 *entry     = NULL;
        int                         ret        = 0;

        entry = scan_data->entry;
        scan_data->entry = NULL;
        do {
                ret = scan_data->fn (scan_data->subvol, entry,
                                     scan_data->parent,
                                     scan_data->data);
                gf_dirent_entry_free (entry);
                entry = NULL;
                pthread_mutex_lock (scan_data->mut);
                {
                        if (ret)
                                *scan_data->retval |= ret;
                        if (list_empty (&scan_data->q->list)) {
                                (*scan_data->jobs_running)--;
                                pthread_cond_broadcast (scan_data->cond);
                        } else {
                                entry = list_first_entry (&scan_data->q->list,
                                                  typeof (*scan_data->q), list);
                                list_del_init (&entry->list);
                                (*scan_data->qlen)--;
                        }
                }
                pthread_mutex_unlock (scan_data->mut);
        } while (entry);

        return ret;
}

static int
_run_dir_scan_task (call_frame_t *frame, xlator_t *subvol, loc_t *parent,
                    gf_dirent_t *q, gf_dirent_t *entry, int *retval,
                    pthread_mutex_t *mut, pthread_cond_t *cond,
                    uint32_t *jobs_running, uint32_t *qlen,
                    syncop_dir_scan_fn_t fn, void *data)
{
        int     ret = 0;
        struct syncop_dir_scan_data *scan_data = NULL;


        scan_data = GF_CALLOC (1, sizeof (struct syncop_dir_scan_data),
                               gf_common_mt_scan_data);
        if (!scan_data) {
                ret = -ENOMEM;
                goto out;
        }

        scan_data->subvol       = subvol;
        scan_data->parent       = parent;
        scan_data->data         = data;
        scan_data->mut          = mut;
        scan_data->cond         = cond;
        scan_data->fn           = fn;
        scan_data->jobs_running = jobs_running;
        scan_data->entry        = entry;
        scan_data->q            = q;
        scan_data->qlen         = qlen;
        scan_data->retval       = retval;

        ret = synctask_new (subvol->ctx->env, _dir_scan_job_fn,
                            _dir_scan_job_fn_cbk, frame, scan_data);
out:
        if (ret < 0) {
                gf_dirent_entry_free (entry);
                _scan_data_destroy (scan_data);
                pthread_mutex_lock (mut);
                {
                        *jobs_running = *jobs_running - 1;
                }
                pthread_mutex_unlock (mut);
                /*No need to cond-broadcast*/
        }
        return ret;
}

int
syncop_mt_dir_scan (call_frame_t *frame, xlator_t *subvol, loc_t *loc, int pid,
                    void *data, syncop_dir_scan_fn_t fn, dict_t *xdata,
                    uint32_t max_jobs, uint32_t max_qlen)
{
        fd_t        *fd    = NULL;
        uint64_t    offset = 0;
        gf_dirent_t *last = NULL;
        int         ret    = 0;
        int         retval = 0;
        gf_dirent_t q;
        gf_dirent_t *entry = NULL;
        gf_dirent_t *tmp = NULL;
        uint32_t    jobs_running = 0;
        uint32_t    qlen = 0;
        pthread_cond_t cond;
        pthread_mutex_t mut;
        gf_boolean_t cond_init = _gf_false;
        gf_boolean_t mut_init = _gf_false;
        gf_dirent_t entries;

        /*For this functionality to be implemented in general, we need
         * synccond_t infra which doesn't block the executing thread. Until then
         * return failures inside synctask if they use this.*/
        if (synctask_get())
                return -ENOTSUP;

        if (max_jobs == 0)
                return -EINVAL;

        /*Code becomes simpler this way. cond_wait just on qlength.
         * Little bit of cheating*/
        if (max_qlen == 0)
                max_qlen = 1;

        ret = syncop_dirfd (subvol, loc, &fd, pid);
        if (ret)
                goto out;

        INIT_LIST_HEAD (&entries.list);
        INIT_LIST_HEAD (&q.list);
        ret = pthread_mutex_init (&mut, NULL);
        if (ret)
                goto out;
        mut_init = _gf_true;

        ret = pthread_cond_init (&cond, NULL);
        if (ret)
                goto out;
        cond_init = _gf_true;

        while ((ret = syncop_readdir (subvol, fd, 131072, offset, &entries,
                                      xdata, NULL))) {
                if (ret < 0)
                        break;

                if (ret > 0) {
                        /* If the entries are only '.', and '..' then ret
                         * value will be non-zero. so set it to zero here. */
                        ret = 0;
                }

                last = list_last_entry (&entries.list, typeof (*last), list);
                offset = last->d_off;

                list_for_each_entry_safe (entry, tmp, &entries.list, list) {
                        list_del_init (&entry->list);
                        if (!strcmp (entry->d_name, ".") ||
                            !strcmp (entry->d_name, "..")) {
                                gf_dirent_entry_free (entry);
                                continue;
                        }

                        if (entry->d_type == IA_IFDIR) {
                                ret = fn (subvol, entry, loc, data);
                                gf_dirent_entry_free (entry);
                                if (ret)
                                        goto out;
                                continue;
                        }

                        if (retval) /*Any jobs failed?*/
                                goto out;

                        pthread_mutex_lock (&mut);
                        {
                                while (qlen == max_qlen)
                                        pthread_cond_wait (&cond, &mut);
                                if (max_jobs == jobs_running) {
                                        list_add_tail (&entry->list, &q.list);
                                        qlen++;
                                        entry = NULL;
                                } else {
                                        jobs_running++;
                                }
                        }
                        pthread_mutex_unlock (&mut);


                        if (!entry)
                                continue;

                        ret = _run_dir_scan_task (frame, subvol, loc, &q, entry,
                                                  &retval, &mut, &cond,
                                                &jobs_running, &qlen, fn, data);
                        if (ret)
                                goto out;
                }
        }

out:
        if (fd)
                fd_unref (fd);
        if (mut_init && cond_init) {
                pthread_mutex_lock (&mut);
                {
                        while (jobs_running)
                                pthread_cond_wait (&cond, &mut);
                }
                pthread_mutex_unlock (&mut);
                gf_dirent_free (&q);
                gf_dirent_free (&entries);
        }

        if (mut_init)
                pthread_mutex_destroy (&mut);
        if (cond_init)
                pthread_cond_destroy (&cond);
        return ret|retval;
}

int
syncop_dir_scan (xlator_t *subvol, loc_t *loc, int pid, void *data,
                 int (*fn) (xlator_t *subvol, gf_dirent_t *entry, loc_t *parent,
                            void *data))
{
        fd_t        *fd    = NULL;
        uint64_t    offset = 0;
        gf_dirent_t *entry = NULL;
        int         ret    = 0;
        gf_dirent_t entries;

        ret = syncop_dirfd (subvol, loc, &fd, pid);
        if (ret)
                goto out;

        INIT_LIST_HEAD (&entries.list);

        while ((ret = syncop_readdir (subvol, fd, 131072, offset, &entries,
                                      NULL, NULL))) {
                if (ret < 0)
                        break;

                if (ret > 0) {
                        /* If the entries are only '.', and '..' then ret
                         * value will be non-zero. so set it to zero here. */
                        ret = 0;
                }

                list_for_each_entry (entry, &entries.list, list) {
                        offset = entry->d_off;

                        if (!strcmp (entry->d_name, ".") ||
                            !strcmp (entry->d_name, ".."))
                                continue;

                        ret = fn (subvol, entry, loc, data);
                        if (ret)
                                break;
                }
                gf_dirent_free (&entries);
                if (ret)
                        break;
        }

out:
        if (fd)
                fd_unref (fd);
        return ret;
}

int
syncop_is_subvol_local (xlator_t *this, loc_t *loc, gf_boolean_t *is_local)
{
        char *pathinfo = NULL;
        dict_t *xattr = NULL;
        int ret = 0;

        if (!this || !this->type || !is_local)
                return -EINVAL;

        if (strcmp (this->type, "protocol/client") != 0)
                return -EINVAL;

        *is_local = _gf_false;

        ret = syncop_getxattr (this, loc, &xattr, GF_XATTR_PATHINFO_KEY, NULL,
                               NULL);
        if (ret < 0) {
                ret = -1;
                goto out;
        }

        if (!xattr) {
                ret = -EINVAL;
                goto out;
        }

        ret = dict_get_str (xattr, GF_XATTR_PATHINFO_KEY, &pathinfo);
        if (ret)
                goto out;

        ret = glusterfs_is_local_pathinfo (pathinfo, is_local);

        gf_msg_debug (this->name, 0, "subvol %s is %slocal",
                this->name, *is_local ? "" : "not ");

out:
        if (xattr)
                dict_unref (xattr);

        return ret;
}

int
syncop_gfid_to_path (inode_table_t *itable, xlator_t *subvol, uuid_t gfid,
                     char **path_p)
{
        int      ret   = 0;
        char    *path  = NULL;
        loc_t    loc   = {0,};
        dict_t  *xattr = NULL;

        gf_uuid_copy (loc.gfid, gfid);
        loc.inode = inode_new (itable);

        ret = syncop_getxattr (subvol, &loc, &xattr, GFID_TO_PATH_KEY, NULL,
                               NULL);
        if (ret < 0)
                goto out;

        ret = dict_get_str (xattr, GFID_TO_PATH_KEY, &path);
        if (ret || !path) {
                ret = -EINVAL;
                goto out;
        }

        if (path_p) {
                *path_p = gf_strdup (path);
                if (!*path_p) {
                        ret = -ENOMEM;
                        goto out;
                }
        }

        ret = 0;

out:
        if (xattr)
                dict_unref (xattr);
        loc_wipe (&loc);

        return ret;
}

int
syncop_inode_find (xlator_t *this, xlator_t *subvol,
                   uuid_t gfid, inode_t **inode,
                   dict_t *xdata, dict_t **rsp_dict)
{
        int         ret    = 0;
        loc_t       loc    = {0, };
        struct iatt iatt   = {0, };
	*inode =  NULL;

        *inode = inode_find (this->itable, gfid);
        if (*inode)
                goto out;

        loc.inode = inode_new (this->itable);
        if (!loc.inode) {
                ret = -ENOMEM;
                goto out;
        }
        gf_uuid_copy (loc.gfid, gfid);

	ret = syncop_lookup (subvol, &loc, &iatt, NULL, xdata, rsp_dict);
        if (ret < 0)
                goto out;

        *inode = inode_link (loc.inode, NULL, NULL, &iatt);
        if (!*inode) {
                ret = -ENOMEM;
                goto out;
        }
out:
        loc_wipe (&loc);
        return ret;
}
