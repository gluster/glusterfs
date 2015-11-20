/*
   Copyright (c) 2006-2012 Red Hat, Inc. <http://www.redhat.com>
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

#include "glusterfs.h"
#include "compat.h"
#include "xlator.h"
#include "inode.h"
#include "logging.h"
#include "common-utils.h"

#include "locks.h"
#include "common.h"
#include "statedump.h"
#include "clear.h"

int
clrlk_get_kind (char *kind)
{
        char  *clrlk_kinds[CLRLK_KIND_MAX] = {"dummy", "blocked", "granted",
                                              "all"};
        int   ret_kind                     = CLRLK_KIND_MAX;
        int   i                            = 0;

        for (i = CLRLK_BLOCKED; i < CLRLK_KIND_MAX; i++) {
                if (!strcmp (clrlk_kinds[i], kind)) {
                        ret_kind = i;
                        break;
                }
        }

        return ret_kind;
}

int
clrlk_get_type (char *type)
{
        char    *clrlk_types[CLRLK_TYPE_MAX]    = {"inode", "entry", "posix"};
        int     ret_type                        = CLRLK_TYPE_MAX;
        int     i                               = 0;

        for (i = CLRLK_INODE; i < CLRLK_TYPE_MAX; i++) {
                if (!strcmp (clrlk_types[i], type)) {
                        ret_type = i;
                        break;
                }
        }

        return ret_type;
}

int
clrlk_get_lock_range (char *range_str, struct gf_flock *ulock,
                      gf_boolean_t *chk_range)
{
        int     ret     =       -1;

        if (!chk_range)
                goto out;

        if (!range_str) {
                ret = 0;
                *chk_range = _gf_false;
                goto out;
        }

        if (sscanf (range_str, "%hd,%"PRId64"-""%"PRId64, &ulock->l_whence,
                    &ulock->l_start, &ulock->l_len) != 3) {
                goto out;
        }

        ret = 0;
        *chk_range = _gf_true;
out:
        return ret;
}

int
clrlk_parse_args (const char* cmd, clrlk_args *args)
{
        char            *opts           = NULL;
        char            *cur            = NULL;
        char            *tok            = NULL;
        char            *sptr           = NULL;
        char            *free_ptr       = NULL;
        char            kw[KW_MAX]     = {[KW_TYPE]     = 't',
                                          [KW_KIND]     = 'k',
                                          };
        int             ret             = -1;
        int             i               = 0;

        GF_ASSERT (cmd);
        free_ptr = opts = GF_CALLOC (1, strlen (cmd), gf_common_mt_char);
        if (!opts)
                goto out;

        if (sscanf (cmd, GF_XATTR_CLRLK_CMD".%s", opts) < 1) {
                ret = -1;
                goto out;
        }

        /*clr_lk_prefix.ttype.kkind.args, args - type specific*/
        cur = opts;
        for (i = 0; i < KW_MAX && (tok = strtok_r (cur, ".", &sptr));
             cur = NULL, i++) {
                if (tok[0] != kw[i]) {
                        ret = -1;
                        goto out;
                }
                if (i == KW_TYPE)
                        args->type = clrlk_get_type (tok+1);
                if (i == KW_KIND)
                        args->kind = clrlk_get_kind (tok+1);
        }

        if ((args->type == CLRLK_TYPE_MAX) || (args->kind == CLRLK_KIND_MAX))
                goto out;

        /*optional args, neither range nor basename can 'legally' contain
         * "/" in them*/
        tok = strtok_r (NULL, "/", &sptr);
        if (tok)
                args->opts = gf_strdup (tok);

        ret = 0;
out:
        GF_FREE (free_ptr);
        return ret;
}

int
clrlk_clear_posixlk (xlator_t *this, pl_inode_t *pl_inode, clrlk_args *args,
                     int *blkd, int *granted, int *op_errno)
{
        posix_lock_t            *plock          = NULL;
        posix_lock_t            *tmp            = NULL;
        struct gf_flock         ulock           = {0, };
        int                     ret             = -1;
        int                     bcount          = 0;
        int                     gcount          = 0;
        gf_boolean_t            chk_range       = _gf_false;

        if (clrlk_get_lock_range (args->opts, &ulock, &chk_range)) {
                *op_errno = EINVAL;
                goto out;
        }

        pthread_mutex_lock (&pl_inode->mutex);
        {
                list_for_each_entry_safe (plock, tmp, &pl_inode->ext_list,
                                          list) {
                        if ((plock->blocked &&
                             !(args->kind & CLRLK_BLOCKED)) ||
                            (!plock->blocked &&
                             !(args->kind & CLRLK_GRANTED)))
                                continue;

                        if (chk_range &&
                            (plock->user_flock.l_whence != ulock.l_whence
                            || plock->user_flock.l_start != ulock.l_start
                            || plock->user_flock.l_len != ulock.l_len))
                                continue;

                        list_del_init (&plock->list);
                        if (plock->blocked) {
                                bcount++;
                                pl_trace_out (this, plock->frame, NULL, NULL,
                                              F_SETLKW, &plock->user_flock,
                                              -1, EAGAIN, NULL);

                                STACK_UNWIND_STRICT (lk, plock->frame, -1, EAGAIN,
                                                     &plock->user_flock, NULL);

                        } else {
                                gcount++;
                        }
                        GF_FREE (plock);
                }
        }
        pthread_mutex_unlock (&pl_inode->mutex);
        grant_blocked_locks (this, pl_inode);
        ret = 0;
out:
        *blkd    = bcount;
        *granted = gcount;
        return ret;
}

/* Returns 0 on success and -1 on failure */
int
clrlk_clear_inodelk (xlator_t *this, pl_inode_t *pl_inode, pl_dom_list_t *dom,
                     clrlk_args *args, int *blkd, int *granted, int *op_errno)
{
        pl_inode_lock_t         *ilock          = NULL;
        pl_inode_lock_t         *tmp            = NULL;
        struct gf_flock         ulock           = {0, };
        int                     ret             = -1;
        int                     bcount          = 0;
        int                     gcount          = 0;
        gf_boolean_t            chk_range       = _gf_false;
        struct list_head        released;

        INIT_LIST_HEAD (&released);
        if (clrlk_get_lock_range (args->opts, &ulock, &chk_range)) {
                *op_errno = EINVAL;
                goto out;
        }

        if (args->kind & CLRLK_BLOCKED)
                goto blkd;

        if (args->kind & CLRLK_GRANTED)
                goto granted;

blkd:
        pthread_mutex_lock (&pl_inode->mutex);
        {
                list_for_each_entry_safe (ilock, tmp, &dom->blocked_inodelks,
                                          blocked_locks) {
                        if (chk_range &&
                            (ilock->user_flock.l_whence != ulock.l_whence
                            || ilock->user_flock.l_start != ulock.l_start
                            || ilock->user_flock.l_len != ulock.l_len))
                                continue;

                        bcount++;
                        list_del_init (&ilock->client_list);
                        list_del_init (&ilock->blocked_locks);
                        list_add (&ilock->blocked_locks, &released);
                }
        }
        pthread_mutex_unlock (&pl_inode->mutex);

        list_for_each_entry_safe (ilock, tmp, &released, blocked_locks) {
                list_del_init (&ilock->blocked_locks);
                pl_trace_out (this, ilock->frame, NULL, NULL, F_SETLKW,
                              &ilock->user_flock, -1, EAGAIN,
                              ilock->volume);
                STACK_UNWIND_STRICT (inodelk, ilock->frame, -1,
                                     EAGAIN, NULL);
                //No need to take lock as the locks are only in one list
                __pl_inodelk_unref (ilock);
        }

        if (!(args->kind & CLRLK_GRANTED)) {
                ret = 0;
                goto out;
        }

granted:
        pthread_mutex_lock (&pl_inode->mutex);
        {
                list_for_each_entry_safe (ilock, tmp, &dom->inodelk_list,
                                          list) {
                        if (chk_range &&
                            (ilock->user_flock.l_whence != ulock.l_whence
                            || ilock->user_flock.l_start != ulock.l_start
                            || ilock->user_flock.l_len != ulock.l_len))
                                continue;

                        gcount++;
                        list_del_init (&ilock->client_list);
                        list_del_init (&ilock->list);
                        list_add (&ilock->list, &released);
                }
        }
        pthread_mutex_unlock (&pl_inode->mutex);

        list_for_each_entry_safe (ilock, tmp, &released, list) {
                list_del_init (&ilock->list);
                //No need to take lock as the locks are only in one list
                __pl_inodelk_unref (ilock);
        }

        ret = 0;
out:
        grant_blocked_inode_locks (this, pl_inode, dom);
        *blkd    = bcount;
        *granted = gcount;
        return ret;
}

/* Returns 0 on success and -1 on failure */
int
clrlk_clear_entrylk (xlator_t *this, pl_inode_t *pl_inode, pl_dom_list_t *dom,
                     clrlk_args *args, int *blkd, int *granted, int *op_errno)
{
        pl_entry_lock_t         *elock          = NULL;
        pl_entry_lock_t         *tmp            = NULL;
        int                     bcount          = 0;
        int                     gcount          = 0;
        int                     ret             = -1;
        struct list_head        removed;
        struct list_head        released;

        INIT_LIST_HEAD (&released);
        if (args->kind & CLRLK_BLOCKED)
                goto blkd;

        if (args->kind & CLRLK_GRANTED)
                goto granted;

blkd:
        pthread_mutex_lock (&pl_inode->mutex);
        {
                list_for_each_entry_safe (elock, tmp, &dom->blocked_entrylks,
                                          blocked_locks) {
                        if (args->opts) {
                                if (!elock->basename ||
                                    strcmp (elock->basename, args->opts))
                                        continue;
                        }

                        bcount++;

                        list_del_init (&elock->client_list);
                        list_del_init (&elock->blocked_locks);
                        list_add_tail (&elock->blocked_locks, &released);
                }
        }
        pthread_mutex_unlock (&pl_inode->mutex);

        list_for_each_entry_safe (elock, tmp, &released, blocked_locks) {
                list_del_init (&elock->blocked_locks);
                entrylk_trace_out (this, elock->frame, elock->volume, NULL, NULL,
                                   elock->basename, ENTRYLK_LOCK, elock->type,
                                   -1, EAGAIN);
                STACK_UNWIND_STRICT (entrylk, elock->frame, -1, EAGAIN, NULL);

		__pl_entrylk_unref (elock);
        }

        if (!(args->kind & CLRLK_GRANTED)) {
                ret = 0;
                goto out;
        }

granted:
        INIT_LIST_HEAD (&removed);
        pthread_mutex_lock (&pl_inode->mutex);
        {
                list_for_each_entry_safe (elock, tmp, &dom->entrylk_list,
                                          domain_list) {
                        if (args->opts) {
                                if (!elock->basename ||
                                    strcmp (elock->basename, args->opts))
                                        continue;
                        }

                        gcount++;
                        list_del_init (&elock->client_list);
                        list_del_init (&elock->domain_list);
                        list_add_tail (&elock->domain_list, &removed);

			__pl_entrylk_unref (elock);
                }
        }
        pthread_mutex_unlock (&pl_inode->mutex);

	grant_blocked_entry_locks (this, pl_inode, dom);

        ret = 0;
out:
        *blkd    = bcount;
        *granted = gcount;
        return ret;
}

int
clrlk_clear_lks_in_all_domains (xlator_t *this, pl_inode_t *pl_inode,
                                clrlk_args *args, int *blkd, int *granted,
                                int *op_errno)
{
        pl_dom_list_t   *dom            = NULL;
        int             ret             = -1;
        int             tmp_bcount      = 0;
        int             tmp_gcount      = 0;

        if (list_empty (&pl_inode->dom_list)) {
                ret = 0;
                goto out;
        }

        list_for_each_entry (dom, &pl_inode->dom_list, inode_list) {
                tmp_bcount = tmp_gcount = 0;

                switch (args->type)
                {
                case CLRLK_INODE:
                        ret = clrlk_clear_inodelk (this, pl_inode, dom, args,
                                             &tmp_bcount, &tmp_gcount,
                                             op_errno);
                        if (ret)
                                goto out;
                        break;
                case CLRLK_ENTRY:
                        ret = clrlk_clear_entrylk (this, pl_inode, dom, args,
                                             &tmp_bcount, &tmp_gcount,
                                             op_errno);
                        if (ret)
                                goto out;
                        break;
                }

                *blkd    += tmp_bcount;
                *granted += tmp_gcount;
        }

        ret = 0;
out:
        return ret;
}
