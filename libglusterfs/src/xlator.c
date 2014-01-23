/*
  Copyright (c) 2008-2012 Red Hat, Inc. <http://www.redhat.com>
  This file is part of GlusterFS.

  This file is licensed to you under your choice of the GNU Lesser
  General Public License, version 3 or any later version (LGPLv3 or
  later), or the GNU General Public License, version 2 (GPLv2), in all
  cases as published by the Free Software Foundation.
*/

#ifndef _CONFIG_H
#define _CONFIG_H
#include "config.h"
#endif

#include "xlator.h"
#include <dlfcn.h>
#include <netdb.h>
#include <fnmatch.h>
#include "defaults.h"

#define SET_DEFAULT_FOP(fn) do {			\
                if (!xl->fops->fn)			\
                        xl->fops->fn = default_##fn;	\
        } while (0)

#define SET_DEFAULT_CBK(fn) do {			\
                if (!xl->cbks->fn)			\
                        xl->cbks->fn = default_##fn;	\
        } while (0)


static void
fill_defaults (xlator_t *xl)
{
        if (xl == NULL)	{
                gf_log_callingfn ("xlator", GF_LOG_WARNING, "invalid argument");
                return;
        }

        SET_DEFAULT_FOP (create);
        SET_DEFAULT_FOP (open);
        SET_DEFAULT_FOP (stat);
        SET_DEFAULT_FOP (readlink);
        SET_DEFAULT_FOP (mknod);
        SET_DEFAULT_FOP (mkdir);
        SET_DEFAULT_FOP (unlink);
        SET_DEFAULT_FOP (rmdir);
        SET_DEFAULT_FOP (symlink);
        SET_DEFAULT_FOP (rename);
        SET_DEFAULT_FOP (link);
        SET_DEFAULT_FOP (truncate);
        SET_DEFAULT_FOP (readv);
        SET_DEFAULT_FOP (writev);
        SET_DEFAULT_FOP (statfs);
        SET_DEFAULT_FOP (flush);
        SET_DEFAULT_FOP (fsync);
        SET_DEFAULT_FOP (setxattr);
        SET_DEFAULT_FOP (getxattr);
        SET_DEFAULT_FOP (fsetxattr);
        SET_DEFAULT_FOP (fgetxattr);
        SET_DEFAULT_FOP (removexattr);
        SET_DEFAULT_FOP (fremovexattr);
        SET_DEFAULT_FOP (opendir);
        SET_DEFAULT_FOP (readdir);
        SET_DEFAULT_FOP (readdirp);
        SET_DEFAULT_FOP (fsyncdir);
        SET_DEFAULT_FOP (access);
        SET_DEFAULT_FOP (ftruncate);
        SET_DEFAULT_FOP (fstat);
        SET_DEFAULT_FOP (lk);
        SET_DEFAULT_FOP (inodelk);
        SET_DEFAULT_FOP (finodelk);
        SET_DEFAULT_FOP (entrylk);
        SET_DEFAULT_FOP (fentrylk);
        SET_DEFAULT_FOP (lookup);
        SET_DEFAULT_FOP (rchecksum);
        SET_DEFAULT_FOP (xattrop);
        SET_DEFAULT_FOP (fxattrop);
        SET_DEFAULT_FOP (setattr);
        SET_DEFAULT_FOP (fsetattr);
	SET_DEFAULT_FOP (fallocate);
	SET_DEFAULT_FOP (discard);
        SET_DEFAULT_FOP (zerofill);

        SET_DEFAULT_FOP (getspec);

        SET_DEFAULT_CBK (release);
        SET_DEFAULT_CBK (releasedir);
        SET_DEFAULT_CBK (forget);

        if (!xl->notify)
                xl->notify = default_notify;

        if (!xl->mem_acct_init)
                xl->mem_acct_init = default_mem_acct_init;

        return;
}


int
xlator_set_type_virtual (xlator_t *xl, const char *type)
{
        GF_VALIDATE_OR_GOTO ("xlator", xl, out);
        GF_VALIDATE_OR_GOTO ("xlator", type, out);

        xl->type = gf_strdup (type);

        if (xl->type)
                return 0;

out:
        return -1;
}


int
xlator_volopt_dynload (char *xlator_type, void **dl_handle,
                       volume_opt_list_t *opt_list)
{
        int                     ret = -1;
        char                    *name = NULL;
        void                    *handle = NULL;

        GF_VALIDATE_OR_GOTO ("xlator", xlator_type, out);

        ret = gf_asprintf (&name, "%s/%s.so", XLATORDIR, xlator_type);
        if (-1 == ret) {
                gf_log ("xlator", GF_LOG_ERROR, "asprintf failed");
                goto out;
        }

        ret = -1;

        gf_log ("xlator", GF_LOG_TRACE, "attempt to load file %s", name);

        handle = dlopen (name, RTLD_NOW|RTLD_GLOBAL);
        if (!handle) {
                gf_log ("xlator", GF_LOG_WARNING, "%s", dlerror ());
                goto out;
        }

        if (!(opt_list->given_opt = dlsym (handle, "options"))) {
                dlerror ();
                gf_log ("xlator", GF_LOG_ERROR,
                        "Failed to load xlator opt table");
                goto out;
        }

        *dl_handle = handle;

        ret = 0;
 out:
        GF_FREE (name);

        gf_log ("xlator", GF_LOG_DEBUG, "Returning %d", ret);
        return ret;

}


int
xlator_dynload (xlator_t *xl)
{
        int                ret = -1;
        char              *name = NULL;
        void              *handle = NULL;
        volume_opt_list_t *vol_opt = NULL;
        class_methods_t   *vtbl = NULL;

        GF_VALIDATE_OR_GOTO ("xlator", xl, out);

        INIT_LIST_HEAD (&xl->volume_options);

        ret = gf_asprintf (&name, "%s/%s.so", XLATORDIR, xl->type);
        if (-1 == ret) {
                gf_log ("xlator", GF_LOG_ERROR, "asprintf failed");
                goto out;
        }

        ret = -1;

        gf_log ("xlator", GF_LOG_TRACE, "attempt to load file %s", name);

        handle = dlopen (name, RTLD_NOW|RTLD_GLOBAL);
        if (!handle) {
                gf_log ("xlator", GF_LOG_WARNING, "%s", dlerror ());
                goto out;
        }
        xl->dlhandle = handle;

        if (!(xl->fops = dlsym (handle, "fops"))) {
                gf_log ("xlator", GF_LOG_WARNING, "dlsym(fops) on %s",
                        dlerror ());
                goto out;
        }

        if (!(xl->cbks = dlsym (handle, "cbks"))) {
                gf_log ("xlator", GF_LOG_WARNING, "dlsym(cbks) on %s",
                        dlerror ());
                goto out;
        }

        /*
         * If class_methods exists, its contents override any definitions of
         * init or fini for that translator.  Otherwise, we fall back to the
         * older method of looking for init and fini directly.
         */
        vtbl = dlsym(handle,"class_methods");
        if (vtbl) {
                xl->init        = vtbl->init;
                xl->fini        = vtbl->fini;
                xl->reconfigure = vtbl->reconfigure;
                xl->notify      = vtbl->notify;
        }
        else {
                if (!(*VOID(&xl->init) = dlsym (handle, "init"))) {
                        gf_log ("xlator", GF_LOG_WARNING, "dlsym(init) on %s",
                                dlerror ());
                        goto out;
                }

                if (!(*VOID(&(xl->fini)) = dlsym (handle, "fini"))) {
                        gf_log ("xlator", GF_LOG_WARNING, "dlsym(fini) on %s",
                                dlerror ());
                        goto out;
                }
                if (!(*VOID(&(xl->reconfigure)) = dlsym (handle,
                                                         "reconfigure"))) {
                        gf_log ("xlator", GF_LOG_TRACE,
                                "dlsym(reconfigure) on %s -- neglecting",
                                dlerror());
                }
                if (!(*VOID(&(xl->notify)) = dlsym (handle, "notify"))) {
                        gf_log ("xlator", GF_LOG_TRACE,
                                "dlsym(notify) on %s -- neglecting",
                                dlerror ());
                }

        }

        if (!(xl->dumpops = dlsym (handle, "dumpops"))) {
                gf_log ("xlator", GF_LOG_TRACE,
                        "dlsym(dumpops) on %s -- neglecting", dlerror ());
        }

        if (!(*VOID(&(xl->mem_acct_init)) = dlsym (handle, "mem_acct_init"))) {
                gf_log (xl->name, GF_LOG_TRACE,
                        "dlsym(mem_acct_init) on %s -- neglecting",
                        dlerror ());
        }

        vol_opt = GF_CALLOC (1, sizeof (volume_opt_list_t),
                         gf_common_mt_volume_opt_list_t);

        if (!vol_opt) {
                goto out;
        }

        if (!(vol_opt->given_opt = dlsym (handle, "options"))) {
                dlerror ();
                gf_log (xl->name, GF_LOG_TRACE,
                        "Strict option validation not enforced -- neglecting");
        }
        INIT_LIST_HEAD (&vol_opt->list);
        list_add_tail (&vol_opt->list, &xl->volume_options);

        fill_defaults (xl);

        ret = 0;

out:
        GF_FREE (name);
        return ret;
}


int
xlator_set_type (xlator_t *xl, const char *type)
{
        int ret = 0;

        ret = xlator_set_type_virtual (xl, type);
        if (!ret)
                ret = xlator_dynload (xl);

        return ret;
}


void
xlator_foreach (xlator_t *this,
                void (*fn)(xlator_t *each,
                           void *data),
                void *data)
{
        xlator_t *first    = NULL;
        xlator_t *old_THIS = NULL;

        GF_VALIDATE_OR_GOTO ("xlator", this, out);
        GF_VALIDATE_OR_GOTO ("xlator", fn, out);

        first = this;

        while (first->prev)
                first = first->prev;

        while (first) {
                old_THIS = THIS;
                THIS = first;

                fn (first, data);

                THIS = old_THIS;
                first = first->next;
        }

out:
        return;
}


void
xlator_foreach_depth_first (xlator_t *this,
			    void (*fn)(xlator_t *each, void *data),
			    void *data)
{
	xlator_list_t *subv = NULL;

	subv = this->children;

	while (subv) {
		xlator_foreach_depth_first (subv->xlator, fn, data);
		subv = subv->next;
	}

	fn (this, data);
}


xlator_t *
xlator_search_by_name (xlator_t *any, const char *name)
{
        xlator_t *search = NULL;

        GF_VALIDATE_OR_GOTO ("xlator", any, out);
        GF_VALIDATE_OR_GOTO ("xlator", name, out);

        search = any;

        while (search->prev)
                search = search->prev;

        while (search) {
                if (!strcmp (search->name, name))
                        break;
                search = search->next;
        }

out:
        return search;
}

static int
__xlator_init(xlator_t *xl)
{
        xlator_t *old_THIS = NULL;
        int       ret = 0;

        old_THIS = THIS;
        THIS = xl;

        ret = xl->init (xl);

        THIS = old_THIS;

        return ret;
}


int
xlator_init (xlator_t *xl)
{
        int32_t ret = -1;

        GF_VALIDATE_OR_GOTO ("xlator", xl, out);

        if (xl->mem_acct_init)
                xl->mem_acct_init (xl);

        if (!xl->init) {
                gf_log (xl->name, GF_LOG_WARNING, "No init() found");
                goto out;
        }

        ret = __xlator_init (xl);

        if (ret) {
                gf_log (xl->name, GF_LOG_ERROR,
                        "Initialization of volume '%s' failed,"
                        " review your volfile again",
                        xl->name);
                goto out;
        }

        xl->init_succeeded = 1;

        ret = 0;
out:
        return ret;
}


static void
xlator_fini_rec (xlator_t *xl)
{
        xlator_list_t *trav     = NULL;
        xlator_t      *old_THIS = NULL;

        GF_VALIDATE_OR_GOTO ("xlator", xl, out);

        trav = xl->children;

        while (trav) {
                if (!trav->xlator->init_succeeded) {
                        break;
                }

                xlator_fini_rec (trav->xlator);
                gf_log (trav->xlator->name, GF_LOG_DEBUG, "fini done");
                trav = trav->next;
        }

        if (xl->init_succeeded) {
                if (xl->fini) {
                        old_THIS = THIS;
                        THIS = xl;

                        xl->fini (xl);

                        if (xl->local_pool)
                                mem_pool_destroy (xl->local_pool);

                        THIS = old_THIS;
                } else {
                        gf_log (xl->name, GF_LOG_DEBUG, "No fini() found");
                }
                xl->init_succeeded = 0;
        }

out:
        return;
}


int
xlator_notify (xlator_t *xl, int event, void *data, ...)
{
        xlator_t *old_THIS = NULL;
        int       ret = 0;

        old_THIS = THIS;
        THIS = xl;

        ret = xl->notify (xl, event, data);

        THIS = old_THIS;

        return ret;
}


int
xlator_mem_acct_init (xlator_t *xl, int num_types)
{
        int             i = 0;
        int             ret = 0;

        if (!xl)
                return -1;

        if (!xl->ctx->mem_acct_enable)
                return 0;

        xl->mem_acct.num_types = num_types;

        xl->mem_acct.rec = CALLOC(num_types, sizeof(struct mem_acct_rec));

        if (!xl->mem_acct.rec) {
                return -1;
        }

        for (i = 0; i < num_types; i++) {
                ret = LOCK_INIT(&(xl->mem_acct.rec[i].lock));
                if (ret) {
                        fprintf(stderr, "Unable to lock..errno : %d",errno);
                }
        }

        return 0;
}


void
xlator_tree_fini (xlator_t *xl)
{
        xlator_t *top = NULL;

        GF_VALIDATE_OR_GOTO ("xlator", xl, out);

        top = xl;
        xlator_fini_rec (top);

out:
        return;
}

int
xlator_list_destroy (xlator_list_t *list)
{
        xlator_list_t *next = NULL;

        while (list) {
                next = list->next;
                GF_FREE (list);
                list = next;
        }

        return 0;
}


int
xlator_tree_free (xlator_t *tree)
{
        volume_opt_list_t *vol_opt = NULL;
        volume_opt_list_t *tmp     = NULL;
        xlator_t *trav = tree;
        xlator_t *prev = tree;

        if (!tree) {
                gf_log ("parser", GF_LOG_ERROR, "Translator tree not found");
                return -1;
        }

        while (prev) {
                trav = prev->next;
                if (prev->dlhandle)
                        dlclose (prev->dlhandle);
                dict_unref (prev->options);
                GF_FREE (prev->name);
                GF_FREE (prev->type);
                xlator_list_destroy (prev->children);
                xlator_list_destroy (prev->parents);

                list_for_each_entry_safe (vol_opt, tmp, &prev->volume_options,
                                          list) {
                        list_del_init (&vol_opt->list);
                        GF_FREE (vol_opt);
                }
                GF_FREE (prev);
                prev = trav;
        }

        return 0;
}


void
loc_wipe (loc_t *loc)
{
        if (loc->inode) {
                inode_unref (loc->inode);
                loc->inode = NULL;
        }
        if (loc->path) {
                GF_FREE ((char *)loc->path);
                loc->path = NULL;
        }

        if (loc->parent) {
                inode_unref (loc->parent);
                loc->parent = NULL;
        }

        memset (loc, 0, sizeof (*loc));
}

int
loc_path (loc_t *loc, const char *bname)
{
        int     ret = 0;

        if (loc->path)
                goto out;

        ret = -1;

        if (bname && !strlen (bname))
                bname = NULL;

        if (!bname)
                goto inode_path;

        if (loc->parent && !uuid_is_null (loc->parent->gfid)) {
                ret = inode_path (loc->parent, bname, (char**)&loc->path);
        } else if (!uuid_is_null (loc->pargfid)) {
                ret = gf_asprintf ((char**)&loc->path, INODE_PATH_FMT"/%s",
                                   uuid_utoa (loc->pargfid), bname);
        }

        if (loc->path)
                goto out;

inode_path:
        if (loc->inode && !uuid_is_null (loc->inode->gfid)) {
                ret = inode_path (loc->inode, NULL, (char **)&loc->path);
        } else if (!uuid_is_null (loc->gfid)) {
                ret = gf_asprintf ((char**)&loc->path, INODE_PATH_FMT,
                                   uuid_utoa (loc->gfid));
        }
out:
        return ret;
}

void
loc_gfid (loc_t *loc, uuid_t gfid)
{
        if (!gfid)
                goto out;
        uuid_clear (gfid);

        if (!loc)
                goto out;
        else if (!uuid_is_null (loc->gfid))
                uuid_copy (gfid, loc->gfid);
        else if (loc->inode && (!uuid_is_null (loc->inode->gfid)))
                uuid_copy (gfid, loc->inode->gfid);
out:
        return;
}

char*
loc_gfid_utoa (loc_t *loc)
{
        uuid_t gfid;
        loc_gfid (loc, gfid);
        return uuid_utoa (gfid);
}

int
loc_copy_overload_parent (loc_t *dst, loc_t *src, inode_t *parent)
{
        int ret = -1;

        GF_VALIDATE_OR_GOTO ("xlator", dst, err);
        GF_VALIDATE_OR_GOTO ("xlator", src, err);
        GF_VALIDATE_OR_GOTO ("xlator", parent, err);

        uuid_copy (dst->gfid, src->gfid);
        uuid_copy (dst->pargfid, parent->gfid);

        if (src->inode)
                dst->inode = inode_ref (src->inode);

        if (parent)
                dst->parent = inode_ref (parent);

        if (src->path) {
                dst->path = gf_strdup (src->path);

                if (!dst->path)
                        goto out;

                if (src->name)
                        dst->name = strrchr (dst->path, '/');
                if (dst->name)
                        dst->name++;
        }

        ret = 0;
out:
        if (ret == -1)
                loc_wipe (dst);

err:
        return ret;
}

int
loc_copy (loc_t *dst, loc_t *src)
{
        int ret = -1;

        GF_VALIDATE_OR_GOTO ("xlator", dst, err);
        GF_VALIDATE_OR_GOTO ("xlator", src, err);

        uuid_copy (dst->gfid, src->gfid);
        uuid_copy (dst->pargfid, src->pargfid);

        if (src->inode)
                dst->inode = inode_ref (src->inode);

        if (src->parent)
                dst->parent = inode_ref (src->parent);

        if (src->path) {
                dst->path = gf_strdup (src->path);

                if (!dst->path)
                        goto out;

                if (src->name)
                        dst->name = strrchr (dst->path, '/');
                if (dst->name)
                        dst->name++;
        }

        ret = 0;
out:
        if (ret == -1)
                loc_wipe (dst);

err:
        return ret;
}

gf_boolean_t
loc_is_root (loc_t *loc)
{
        if (loc && __is_root_gfid (loc->gfid)) {
                return _gf_true;
        } else if (loc && loc->inode && __is_root_gfid (loc->inode->gfid)) {
                return _gf_true;
        }
        return _gf_false;
}

int
xlator_destroy (xlator_t *xl)
{
        volume_opt_list_t *vol_opt = NULL;
        volume_opt_list_t *tmp     = NULL;

        if (!xl)
                return 0;

        GF_FREE (xl->name);
        GF_FREE (xl->type);
        if (xl->dlhandle)
                dlclose (xl->dlhandle);
        if (xl->options)
                dict_destroy (xl->options);

        xlator_list_destroy (xl->children);

        xlator_list_destroy (xl->parents);

        list_for_each_entry_safe (vol_opt, tmp, &xl->volume_options, list) {
                list_del_init (&vol_opt->list);
                GF_FREE (vol_opt);
        }

        GF_FREE (xl);

        return 0;
}


int
is_gf_log_command (xlator_t *this, const char *name, char *value)
{
        xlator_t       *trav        = NULL;
        char            key[1024]   = {0,};
        int             ret         = -1;
        int             log_level   = -1;
        gf_boolean_t    syslog_flag = 0;
        glusterfs_ctx_t *ctx        = NULL;

        if (!strcmp ("trusted.glusterfs.syslog", name)) {
                ret = gf_string2boolean (value, &syslog_flag);
                if (ret) {
                        ret = EOPNOTSUPP;
                        goto out;
                }
                if (syslog_flag)
                        gf_log_enable_syslog ();
                else
                        gf_log_disable_syslog ();

                goto out;
        }

        if (fnmatch ("trusted.glusterfs*set-log-level", name, FNM_NOESCAPE))
                goto out;

        log_level = glusterd_check_log_level (value);
        if (log_level == -1) {
                ret = EOPNOTSUPP;
                goto out;
        }

        /* Some crude way to change the log-level of process */
        if (!strcmp (name, "trusted.glusterfs.set-log-level")) {
                /* */
                gf_log ("glusterfs", gf_log_get_loglevel(),
                        "setting log level to %d (old-value=%d)",
                        log_level, gf_log_get_loglevel());
                gf_log_set_loglevel (log_level);
                ret = 0;
                goto out;
        }

        if (!strcmp (name, "trusted.glusterfs.fuse.set-log-level")) {
                /* */
                gf_log (this->name, gf_log_get_xl_loglevel (this),
                        "setting log level to %d (old-value=%d)",
                        log_level, gf_log_get_xl_loglevel (this));
                gf_log_set_xl_loglevel (this, log_level);
                ret = 0;
                goto out;
        }

        ctx = this->ctx;
        if (!ctx)
                goto out;
        if (!ctx->active)
                goto out;
        trav = ctx->active->top;

        while (trav) {
                snprintf (key, 1024, "trusted.glusterfs.%s.set-log-level",
                          trav->name);
                if (fnmatch (name, key, FNM_NOESCAPE) == 0) {
                        gf_log (trav->name, gf_log_get_xl_loglevel (trav),
                                "setting log level to %d (old-value=%d)",
                                log_level, gf_log_get_xl_loglevel (trav));
                        gf_log_set_xl_loglevel (trav, log_level);
                        ret = 0;
                }
                trav = trav->next;
        }
out:
        return ret;
}


int
glusterd_check_log_level (const char *value)
{
        int log_level = -1;

        if (!strcasecmp (value, "CRITICAL")) {
                log_level = GF_LOG_CRITICAL;
        } else if (!strcasecmp (value, "ERROR")) {
                log_level = GF_LOG_ERROR;
        } else if (!strcasecmp (value, "WARNING")) {
                log_level = GF_LOG_WARNING;
        } else if (!strcasecmp (value, "INFO")) {
                log_level = GF_LOG_INFO;
        } else if (!strcasecmp (value, "DEBUG")) {
                log_level = GF_LOG_DEBUG;
        } else if (!strcasecmp (value, "TRACE")) {
                log_level = GF_LOG_TRACE;
        } else if (!strcasecmp (value, "NONE")) {
                log_level = GF_LOG_NONE;
        }

        if (log_level == -1)
                gf_log (THIS->name, GF_LOG_ERROR, "Invalid log-level. possible values "
                        "are DEBUG|WARNING|ERROR|CRITICAL|NONE|TRACE");

        return log_level;
}

