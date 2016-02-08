/*
  Copyright (c) 2016 Red Hat, Inc. <http://www.redhat.com>
  This file is part of GlusterFS.

  This file is licensed to you under your choice of the GNU Lesser
  General Public License, version 3 or any later version (LGPLv3 or
  later), or the GNU General Public License, version 2 (GPLv2), in all
  cases as published by the Free Software Foundation.
*/

/* File: dht2-server-main.c
 * This file contains the xlator loading functions, FOP entry points
 * and options.
 * The entire functionality including comments is TODO.
 */

#include "glusterfs.h"
#include "xlator.h"
#include "logging.h"
#include "statedump.h"

int32_t
dht2_server_init (xlator_t *this)
{
        if (!this->children) {
                gf_log (this->name, GF_LOG_ERROR,
                        "Missing children in volume graph, this (%s) is"
                        " not a leaf translator", this->name);
                return -1;
        }

        return 0;
}

void
dht2_server_fini (xlator_t *this)
{
        return;
}

class_methods_t class_methods = {
        .init           = dht2_server_init,
        .fini           = dht2_server_fini,
};

struct xlator_fops fops = {
};

struct xlator_cbks cbks = {
};

/*
struct xlator_dumpops dumpops = {
};
*/

struct volume_options options[] = {
        { .key  = {NULL} },
};
