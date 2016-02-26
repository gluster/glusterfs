/*
  Copyright (c) 2010-2011 Gluster, Inc. <http://www.gluster.com>
  This file is part of GlusterFS.

  This file is licensed to you under your choice of the GNU Lesser
  General Public License, version 3 or any later version (LGPLv3 or
  later), or the GNU General Public License, version 2 (GPLv2), in all
  cases as published by the Free Software Foundation.
*/

#ifndef _MOUNT3_H_
#define _MOUNT3_H_

#include "rpcsvc.h"
#include "dict.h"
#include "xlator.h"
#include "iobuf.h"
#include "nfs.h"
#include "list.h"
#include "xdr-nfs3.h"
#include "locking.h"
#include "nfs3-fh.h"
#include "compat-uuid.h"
#include "exports.h"
#include "mount3-auth.h"
#include "auth-cache.h"

/* Registered with portmap */
#define GF_MOUNTV3_PORT         38465
#define GF_MOUNTV3_IOB          (2 * GF_UNIT_KB)
#define GF_MOUNTV3_IOBPOOL      (GF_MOUNTV3_IOB * 50)

#define GF_MOUNTV1_PORT         38466
#define GF_MNT                  GF_NFS"-mount"

extern rpcsvc_program_t *
mnt3svc_init (xlator_t *nfsx);

extern rpcsvc_program_t *
mnt1svc_init (xlator_t *nfsx);

extern void
mnt3svc_deinit (xlator_t *nfsx);

extern int
mount_init_state (xlator_t *nfsx);

extern int
mount_reconfigure_state (xlator_t *nfsx, dict_t *options);

void
mount_rewrite_rmtab (struct mount3_state *ms, char *new_rmtab);

struct mnt3_export *
mnt3_mntpath_to_export (struct mount3_state *ms, const char *dirpath,
                        gf_boolean_t export_parsing_match);

extern int
mnt3svc_update_mountlist (struct mount3_state *ms, rpcsvc_request_t *req,
                          const char *expname, const char *fullpath);

int
mnt3_authenticate_request (struct mount3_state *ms, rpcsvc_request_t *req,
                           struct nfs3_fh *fh, const char *volname,
                           const char *path, char **authorized_path,
                           char **authorized_host, gf_boolean_t is_write_op);

/* Data structure used to store the list of mounts points currently
 * in use by NFS clients.
 */
struct mountentry {
        /* Links to mount3_state->mountlist.  */
        struct list_head        mlist;

        /* The export name */
        char                    exname[MNTPATHLEN];
        char                    hostname[MNTPATHLEN];
        char                    fullpath[MNTPATHLEN];

        gf_boolean_t            has_full_path;

        /* Since this is stored in a dict, we want to be able
         * to find easily get the key we used to store
         * the struct in our dict
         */
        char                    hashkey[MNTPATHLEN*2+2];
};

#define MNT3_EXPTYPE_VOLUME     1
#define MNT3_EXPTYPE_DIR        2

/* Structure to hold export-dir AUTH parameter */
struct host_auth_spec {
        char                    *host_addr;    /* Allowed IP or host name */
        uint32_t                netmask;       /* Network mask (Big-Endian) */
        struct host_auth_spec   *next;         /* Pointer to next AUTH struct */
};

struct mnt3_export {
        struct list_head        explist;

        /* The string that may contain either the volume name if the full volume
         * is exported or the subdirectory in the volume.
         */
        char                    *expname;
        /*
         * IP address, hostname or subnets who are allowed to connect to expname
         * subvolume or subdirectory
         */
        struct host_auth_spec*  hostspec;
        xlator_t                *vol;
        int                     exptype;

        /* This holds the full path that the client requested including
         * the volume name AND the subdirectory in the volume.
         */
        char                    *fullpath;

        /* Extracted from nfs volume options if nfs.dynamicvolumes is on.
         */
        uuid_t                  volumeid;
        uuid_t                  mountid;
};

struct mount3_state {
        xlator_t                *nfsx;

        /* The NFS state that this belongs to */
        struct nfs_state        *nfs;

        /* The buffers for all network IO are got from this pool. */
        struct iobuf_pool       *iobpool;

        /* List of exports, can be volumes or directories in those volumes. */
        struct list_head        exportlist;

        /* List of current mount points over all the exports from this
         * server.
         */
        struct list_head        mountlist;

        /* Dict of current mount points over all the exports from this
         * server. Mirrors the mountlist above, but can be used for
         * faster lookup in the event that there are several mounts.
         * Currently, each NFSOP is validated against this dict: each
         * op is checked to see if the host that operates on the path
         * does in fact have an entry in the mount dict.
         */
        dict_t                  *mountdict;

        /* Used to protect the mountlist & the mount dict */
        gf_lock_t               mountlock;

        /* Used to insert additional authentication parameters */
        struct mnt3_auth_params      *auth_params;

        /* Set to 0 if exporting full volumes is disabled. On by default. */
        gf_boolean_t            export_volumes;
        gf_boolean_t            export_dirs;

        pthread_t               auth_refresh_thread;
        gf_boolean_t            stop_refresh;

        struct auth_cache       *authcache;
};

#define gf_mnt3_export_dirs(mst)        ((mst)->export_dirs)

struct mount3_resolve_state {
        struct mnt3_export      *exp;
        struct mount3_state     *mstate;
        rpcsvc_request_t        *req;

        char                    remainingdir[MNTPATHLEN];
        loc_t                   resolveloc;
        struct nfs3_fh          parentfh;
};

typedef struct mount3_resolve_state mnt3_resolve_t;

#endif
