/*
   Copyright 2014-present Facebook. All Rights Reserved

   This file is part of GlusterFS.

   Author :
   Shreyas Siravara <shreyas.siravara@gmail.com>

   This file is licensed to you under your choice of the GNU Lesser
   General Public License, version 3 or any later version (LGPLv3 or
   later), or the GNU General Public License, version 2 (GPLv2), in all
   cases as published by the Free Software Foundation.
*/

#ifndef _MOUNT3_AUTH
#define _MOUNT3_AUTH

#include "nfs-mem-types.h"
#include "netgroups.h"
#include "exports.h"
#include "mount3.h"
#include "nfs.h"

#define GF_MNT_AUTH GF_NFS"-mount3-auth"

struct mnt3_auth_params {
        struct netgroups_file *ngfile;  /* The netgroup file to auth against */
        struct exports_file   *expfile; /* The export file to auth against */
        struct mount3_state   *ms;      /* The mount state that owns this */
};

/* Initialize auth params struct */
struct mnt3_auth_params *
mnt3_auth_params_init (struct mount3_state *ms);

/* Set the netgroups file to use in the auth */
int
mnt3_auth_set_netgroups_auth (struct mnt3_auth_params *aps,
                              const char *filename);

/* Set the exports file to use in the auth */
int
mnt3_auth_set_exports_auth (struct mnt3_auth_params *aps, const char *filename);

/* Check if a host is authorized to perform a mount / nfs-fop */
int
mnt3_auth_host (const struct mnt3_auth_params *aps, const char *host,
                struct nfs3_fh *fh, const char *dir, gf_boolean_t is_write_op,
                struct export_item **save_item);

/* Free resources used by the auth params struct */
void
mnt3_auth_params_deinit (struct mnt3_auth_params *aps);

int
mnt3_auth_fop_options_verify (const struct mnt3_auth_params *auth_params,
                              const char *host, const char *dir);

#endif /* _MOUNT3_AUTH */
