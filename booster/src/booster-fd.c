/*
  Copyright (c) 2009 Z RESEARCH, Inc. <http://www.zresearch.com>
  This file is part of GlusterFS.

  GlusterFS is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published
  by the Free Software Foundation; either version 3 of the License,
  or (at your option) any later version.

  GlusterFS is distributed in the hope that it will be useful, but
  WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
  General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program.  If not, see
  <http://www.gnu.org/licenses/>.
*/



#include "booster-fd.h"
#include <logging.h>
#include <mem-pool.h>
#include <stdlib.h>
#include <errno.h>
#include <common-utils.h>
#include <string.h>

#include <assert.h>

extern fd_t *
fd_ref (fd_t *fd);

extern void
fd_unref (fd_t *fd);
/*
   Allocate in memory chunks of power of 2 starting from 1024B
   Assumes fdtable->lock is held
   */
static inline uint
gf_roundup_power_of_two (uint nr)
{
        uint result = 1;

        if (nr < 0) {
                gf_log ("server-protocol/fd",
                                GF_LOG_ERROR,
                                "Negative number passed");
                return -1;
        }
        
        while (result <= nr)
                result *= 2;

        return result;
}

int
booster_fdtable_expand (booster_fdtable_t *fdtable, uint nr)
{
        fd_t    **oldfds = NULL;
        uint    oldmax_fds = -1;
        uint    cpy = 0;
        int32_t ret = -1;

        if (fdtable == NULL || nr < 0) {
                gf_log ("fd", GF_LOG_ERROR, "invalid argument");
                errno = EINVAL;
                ret = -1;
                goto out;
        }

        nr /= (1024 / sizeof (fd_t *));
        nr = gf_roundup_power_of_two (nr + 1);
        nr *= (1024 / sizeof (fd_t *));

        oldfds = fdtable->fds;
        oldmax_fds = fdtable->max_fds;

        fdtable->fds = CALLOC (nr, sizeof (fd_t *));
        if (fdtable->fds == NULL) {
                fdtable->fds = oldfds;
                oldfds = NULL;
                ret = -1;
                goto out;
        }

        fdtable->max_fds = nr;

        if (oldfds) {
                cpy = oldmax_fds * sizeof (fd_t *);
                memcpy (fdtable->fds, oldfds, cpy);
        }

        ret = 0;
out:
        FREE (oldfds);

        return ret;
}

booster_fdtable_t *
booster_fdtable_alloc (void)
{
        booster_fdtable_t *fdtable = NULL;
        int32_t            ret = -1;

        fdtable = CALLOC (1, sizeof (*fdtable));
        GF_VALIDATE_OR_GOTO ("booster-fd", fdtable, out);

        LOCK_INIT (&fdtable->lock);

        LOCK (&fdtable->lock);
        {
                ret = booster_fdtable_expand (fdtable, 0);
        }
        UNLOCK (&fdtable->lock);

        if (ret == -1) {
                FREE (fdtable);
                fdtable = NULL;
        }

out:
        return fdtable;
}

fd_t **
__booster_fdtable_get_all_fds (booster_fdtable_t *fdtable, uint *count)
{
        fd_t **fds = NULL;

        if (count == NULL)
                goto out;

        fds = fdtable->fds;
        fdtable->fds = calloc (fdtable->max_fds, sizeof (fd_t *));
        *count = fdtable->max_fds;

out:
        return fds;
}

fd_t **
booster_fdtable_get_all_fds (booster_fdtable_t *fdtable, uint *count)
{
        fd_t **fds = NULL;
        if (!fdtable)
                return NULL;

        LOCK (&fdtable->lock);
        {
                fds = __booster_fdtable_get_all_fds (fdtable, count);
        }
        UNLOCK (&fdtable->lock);

        return fds;
}

void
booster_fdtable_destroy (booster_fdtable_t *fdtable)
{
        fd_t                    *fd = NULL;
        fd_t                    **fds = NULL;
        uint                    fd_count = 0;
        int                     i = 0;

        if (!fdtable)
                return;

        LOCK (&fdtable->lock);
        {
                fds = __booster_fdtable_get_all_fds (fdtable, &fd_count);
                FREE (fdtable->fds);
        }
        UNLOCK (&fdtable->lock);

        if (!fds)
                goto free_table;
        
        for (i = 0; i < fd_count; i++) {
                fd = fds[i];
                if (fd != NULL)
                        fd_unref (fd);
        }
        FREE (fds);
free_table:
        LOCK_DESTROY (&fdtable->lock);
        FREE (fdtable);
}

int
booster_fd_unused_get (booster_fdtable_t *fdtable, fd_t *fdptr, int fd)
{
        int ret = -1;
        int error = 0;

        if (fdtable == NULL || fdptr == NULL || fd < 0) {
                gf_log ("fd", GF_LOG_ERROR, "invalid argument");
                errno = EINVAL;
                return -1;
        }

        LOCK (&fdtable->lock);
        {
                while (fdtable->max_fds < fd) {
                        error = 0;
                        error = booster_fdtable_expand (fdtable,
                                                        fdtable->max_fds + 1);
                        if (error) {
                                gf_log ("booster-fd", GF_LOG_ERROR,
                                        "Cannot expand fdtable:%s",
                                        strerror (error));
                                goto err;
                        }
                }

                if (!fdtable->fds[fd]) {
                        fdtable->fds[fd] = fdptr;
                        fd_ref (fdptr);
                        ret = fd;
                } else
                        gf_log ("booster-fd", GF_LOG_ERROR, "Cannot allocate fd"
                                " %d (slot not empty in fdtable)", fd);
        }
err:
        UNLOCK (&fdtable->lock);

        return ret;
}

void
booster_fd_put (booster_fdtable_t *fdtable, int fd)
{
        fd_t *fdptr = NULL;
        if (fdtable == NULL || fd < 0) {
                gf_log ("booster-fd", GF_LOG_ERROR, "invalid argument");
                return;
        }

        if (!(fd < fdtable->max_fds)) {
                gf_log ("fd", GF_LOG_ERROR, "invalid argument");
                return;
        }

        LOCK (&fdtable->lock);
        {
                fdptr = fdtable->fds[fd];
                fdtable->fds[fd] = NULL;
        }
        UNLOCK (&fdtable->lock);

        if (fdptr)
                fd_unref (fdptr);
}

fd_t *
booster_fdptr_get (booster_fdtable_t *fdtable, int fd)
{
        fd_t *fdptr = NULL;

        if (fdtable == NULL || fd < 0) {
                gf_log ("booster-fd", GF_LOG_ERROR, "invalid argument");
                errno = EINVAL;
                return NULL;
        }

        if (!(fd < fdtable->max_fds)) {
                gf_log ("booster-fd", GF_LOG_ERROR, "invalid argument");
                errno = EINVAL;
                return NULL;
        }

        LOCK (&fdtable->lock);
        {
                fdptr = fdtable->fds[fd];
                if (fdptr)
                        fd_ref (fdptr);
        }
        UNLOCK (&fdtable->lock);

        return fdptr;
}

void
booster_fdptr_put (fd_t *booster_fd)
{
        if (booster_fd)
                fd_unref (booster_fd);
}
