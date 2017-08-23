/*
  Copyright (c) 2016 Red Hat, Inc. <http://www.redhat.com>
  This file is part of GlusterFS.

  This file is licensed to you under your choice of the GNU Lesser
  General Public License, version 3 or any later version (LGPLv3 or
  later), or the GNU General Public License, version 2 (GPLv2), in all
  cases as published by the Free Software Foundation.
*/

/* File: rio-client-main.c
 * This file contains the xlator loading functions, FOP entry points
 * and options.
 * The entire functionality including comments is TODO.
 */

#include "xlator.h"
#include "locking.h"
#include "defaults.h"

#include "rio-common.h"

int
rio_client_notify (xlator_t *this, int32_t event, void *data, ...)
{
        int ret = 0;

        /* TODO: Pretty darn unsure about this!
        Need to check this better and see if the following actions are right */
        switch (event) {
        case GF_EVENT_CHILD_UP:
        {
                struct rio_conf *conf = this->private;
                int count;

                /* HACK: keep a count of child up events and return when all
                are connected */
                LOCK (&(conf->riocnf_lock));
                count = ++conf->riocnf_notify_count;
                UNLOCK (&(conf->riocnf_lock));

                if (count == (conf->riocnf_dc_count + conf->riocnf_mdc_count)) {
                        ret = default_notify (this, event, data);
                }
        }
        break;
        default:
        {
                ret = default_notify (this, event, data);
        }
        }

        return ret;
}

int32_t
rio_client_init (xlator_t *this)
{
        return rio_common_init (this);
}

void
rio_client_fini (xlator_t *this)
{
        return rio_common_fini (this);
}

class_methods_t class_methods = {
        .init           = rio_client_init,
        .fini           = rio_client_fini,
        .notify         = rio_client_notify,
};

struct xlator_fops fops = {
};

struct xlator_cbks cbks = {
};

/*
struct xlator_dumpops dumpops = {
};
*/
