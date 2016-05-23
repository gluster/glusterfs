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

#include "rio-common.h"

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
};

struct xlator_fops fops = {
};

struct xlator_cbks cbks = {
};

/*
struct xlator_dumpops dumpops = {
};
*/
