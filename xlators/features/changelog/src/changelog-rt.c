/*
   Copyright (c) 2013 Red Hat, Inc. <http://www.redhat.com>
   This file is part of GlusterFS.

   This file is licensed to you under your choice of the GNU Lesser
   General Public License, version 3 or any later version (LGPLv3 or
   later), or the GNU General Public License, version 2 (GPLv2), in all
   cases as published by the Free Software Foundation.
*/

#include "xlator.h"
#include "defaults.h"
#include "logging.h"

#include "changelog-rt.h"
#include "changelog-mem-types.h"

int
changelog_rt_init (xlator_t *this, changelog_dispatcher_t *cd)
{
        changelog_rt_t *crt = NULL;

        crt = GF_CALLOC (1, sizeof (*crt),
                         gf_changelog_mt_rt_t);
        if (!crt)
                return -1;

        LOCK_INIT (&crt->lock);

        cd->cd_data = crt;
        cd->dispatchfn = &changelog_rt_enqueue;

        return 0;
}

int
changelog_rt_fini (xlator_t *this, changelog_dispatcher_t *cd)
{
        changelog_rt_t *crt = NULL;

        crt = cd->cd_data;

        LOCK_DESTROY (&crt->lock);
        GF_FREE (crt);

        return 0;
}

int
changelog_rt_enqueue (xlator_t *this, changelog_priv_t *priv, void *cbatch,
                      changelog_log_data_t *cld_0, changelog_log_data_t *cld_1)
{
        int             ret = 0;
        changelog_rt_t *crt = NULL;

        crt = (changelog_rt_t *) cbatch;

        LOCK (&crt->lock);
        {
                ret = changelog_handle_change (this, priv, cld_0);
                if (!ret && cld_1)
                        ret = changelog_handle_change (this, priv, cld_1);
        }
        UNLOCK (&crt->lock);

        return ret;
}
