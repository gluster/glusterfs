#pragma fragment PROLOG
#ifndef _CONFIG_H
#define _CONFIG_H
#include "config.h"
#endif

#include "glusterfs.h"
#include "fd.h"
#include "iatt.h"
#include "syncop.h"
#include "xlator.h"
#include "glfs-internal.h"

#include "jnl-types.h"

#define GFAPI_SUCCESS 0

inode_t *
recon_get_inode (glfs_t *fs, uuid_t gfid)
{
        inode_t         *inode;
        loc_t           loc     = {NULL,};
        struct iatt     iatt;
        int             ret;
        inode_t         *newinode;

        inode = inode_find (fs->active_subvol->itable, gfid);
        if (inode) {
                printf ("=== FOUND %s IN TABLE\n", uuid_utoa(gfid));
                return inode;
        }

        loc.inode = inode_new (fs->active_subvol->itable);
        if (!loc.inode) {
                return NULL;
        }
        gf_uuid_copy (loc.inode->gfid, gfid);
        gf_uuid_copy (loc.gfid, gfid);

        printf ("=== DOING LOOKUP FOR %s\n", uuid_utoa(gfid));

        ret = syncop_lookup (fs->active_subvol, &loc, &iatt,
                             NULL, NULL, NULL);
        if (ret != GFAPI_SUCCESS) {
                fprintf (stderr, "syncop_lookup failed (%d)\n", ret);
                return NULL;
        }

        newinode = inode_link (loc.inode, NULL, NULL, &iatt);
        if (newinode) {
                inode_lookup (newinode);
        }

        return newinode;
}

#pragma fragment DICT
        dict_t  *@ARGNAME@;

        @ARGNAME@ = dict_new();
        if (!@ARGNAME@) {
                goto *err_label;
        }
        err_label = &&cleanup_@ARGNAME@;

        {
                int     key_len, data_len;
                char    *key_ptr;
                int     garbage;
                for (;;) {
                        key_len = *((int *)new_meta);
                        new_meta += sizeof(int);
                        if (!key_len) {
                                break;
                        }
                        key_ptr = new_meta;
                        new_meta += key_len;
                        data_len = *((int *)new_meta);
                        new_meta += sizeof(int);
                        garbage = dict_set_static_bin (@ARGNAME@, key_ptr,
                                                       new_meta, data_len);
                        /* TBD: check error from dict_set_static_bin */
                        (void)garbage;
                        new_meta += data_len;
                }
        }

#pragma fragment DICT_CLEANUP
cleanup_@ARGNAME@:
        dict_unref (@ARGNAME@);

#pragma fragment DOUBLE
        @ARGTYPE@       @ARGNAME@       = *((@ARGTYPE@ *)new_meta);
        new_meta += sizeof(uint64_t);

#pragma fragment FD
        inode_t *@ARGNAME@_ino;
        fd_t    *@ARGNAME@;

        @ARGNAME@_ino = recon_get_inode (fs, *((uuid_t *)new_meta));
        new_meta += 16;
        if (!@ARGNAME@_ino) {
                goto *err_label;
        }
        err_label = &&cleanup_@ARGNAME@_ino;

        @ARGNAME@ = fd_anonymous (@ARGNAME@_ino);
        if (!@ARGNAME@) {
                goto *err_label;
        }
        err_label = &&cleanup_@ARGNAME@;

#pragma fragment FD_CLEANUP
cleanup_@ARGNAME@:
        fd_unref (@ARGNAME@);
cleanup_@ARGNAME@_ino:
        inode_unref (@ARGNAME@_ino);

#pragma fragment NEW_FD
        /*
         * This pseudo-type is only used for create, and in that case we know
         * we'll be using loc.inode, so it's not worth generalizing to take an
         * extra argument.
         */
        fd_t    *@ARGNAME@      = fd_anonymous (loc.inode);

        if (!fd) {
                goto *err_label;
        }
        err_label = &&cleanup_@ARGNAME@;
        new_meta += 16;

#pragma fragment NEW_FD_CLEANUP
cleanup_@ARGNAME@:
        fd_unref (@ARGNAME@);

#pragma fragment INTEGER
        @ARGTYPE@       @ARGNAME@       = *((@ARGTYPE@ *)new_meta);

        new_meta += sizeof(@ARGTYPE@);

#pragma fragment LOC
        loc_t           @ARGNAME@       = { NULL, };

        @ARGNAME@.inode = recon_get_inode (fs, *((uuid_t *)new_meta));
        if (!@ARGNAME@.inode) {
                goto *err_label;
        }
        err_label = &&cleanup_@ARGNAME@;
        gf_uuid_copy (@ARGNAME@.gfid, @ARGNAME@.inode->gfid);
        new_meta += 16;
        new_meta += 16; /* skip over pargfid */
        if (*(new_meta++)) {
                @ARGNAME@.name = new_meta;
                new_meta += strlen(new_meta) + 1;
        }

#pragma fragment LOC_CLEANUP
cleanup_@ARGNAME@:
        loc_wipe (&@ARGNAME@);

#pragma fragment PARENT_LOC
        loc_t           @ARGNAME@       = { NULL, };

        new_meta += 16; /* skip over gfid */
        @ARGNAME@.parent = recon_get_inode (fs, *((uuid_t *)new_meta));
        if (!@ARGNAME@.parent) {
                goto *err_label;
        }
        err_label = &&cleanup_@ARGNAME@;
        gf_uuid_copy (@ARGNAME@.pargfid, @ARGNAME@.parent->gfid);
        new_meta += 16;
        if (!*(new_meta++)) {
                goto *err_label;
        }
        @ARGNAME@.name = new_meta;
        new_meta += strlen(new_meta) + 1;

        @ARGNAME@.inode = inode_new (fs->active_subvol->itable);
        if (!@ARGNAME@.inode) {
                goto *err_label;
        }

#pragma fragment PARENT_LOC_CLEANUP
cleanup_@ARGNAME@:
        loc_wipe (&@ARGNAME@);

#pragma fragment STRING
        char    *@ARGNAME@;
        if (*(new_meta++)) {
                @ARGNAME@ = new_meta;
                new_meta += (strlen(new_meta) + 1);
        }
        else {
                goto *err_label;
        }

#pragma fragment VECTOR
        struct iovec    @ARGNAME@;

        @ARGNAME@.iov_len = *((size_t *)new_meta);
        new_meta += sizeof(@ARGNAME@.iov_len);
        @ARGNAME@.iov_base = new_data;
        new_data += @ARGNAME@.iov_len;

#pragma fragment IATT
        struct iatt     @ARGNAME@;
        {
                @ARGNAME@.ia_prot = *((ia_prot_t *)new_meta);
                new_meta += sizeof(ia_prot_t);
                uint32_t *myints = (uint32_t *)new_meta;
                @ARGNAME@.ia_uid = myints[0];
                @ARGNAME@.ia_gid = myints[1];
                @ARGNAME@.ia_atime = myints[2];
                @ARGNAME@.ia_atime_nsec = myints[3];
                @ARGNAME@.ia_mtime = myints[4];
                @ARGNAME@.ia_mtime_nsec = myints[5];
                new_meta += sizeof(*myints) * 6;
        }

#pragma fragment IOBREF
        struct iobref   *@ARGNAME@;

        @ARGNAME@ = iobref_new();
        if (!@ARGNAME@) {
                goto *err_label;
        }
        err_label = &&cleanup_@ARGNAME@;

#pragma fragment IOBREF_CLEANUP
cleanup_@ARGNAME@:
        iobref_unref (@ARGNAME@);

#pragma fragment LINK
        /* TBD: check error */
        inode_t *new_inode = inode_link (@INODE_ARG@, NULL, NULL, @IATT_ARG@);
        if (new_inode) {
                inode_lookup (new_inode);
        }

#pragma fragment FOP
int
fdl_replay_@NAME@ (glfs_t *fs, char **old_meta, char **old_data)
{
        char    *new_meta	= *old_meta;
        char	*new_data	= *old_data;
        int     ret;
        int     status          = 0xbad;
        void    *err_label      = &&done;

@FUNCTION_BODY@

        ret = syncop_@NAME@ (fs->active_subvol, @SYNCOP_ARGS@, NULL);
        if (ret != @SUCCESS_VALUE@) {
                fprintf (stderr, "syncop_@NAME@ returned %d", ret);
                goto *err_label;
        }

@LINKS@

        status = 0;

@CLEANUPS@

done:
        *old_meta = new_meta;
        *old_data = new_data;
        return status;
}

#pragma fragment CASE
        case GF_FOP_@UPNAME@:
                printf ("=== GF_FOP_@UPNAME@\n");
                if (fdl_replay_@NAME@ (fs, &new_meta, &new_data) != 0) {
                        goto done;
                }
                recognized = 1;
                break;

#pragma fragment EPILOG
int
recon_execute (glfs_t *fs, char **old_meta, char **old_data)
{
        char            *new_meta       = *old_meta;
        char            *new_data       = *old_data;
        int             recognized      = 0;
        event_header_t  *eh;

        eh = (event_header_t *)new_meta;
        new_meta += sizeof (*eh);

        /* TBD: check event_type instead of assuming NEW_REQUEST */

        switch (eh->fop_type) {
@SWITCH_BODY@

        default:
                printf ("unknown fop %u\n", eh->fop_type);
        }

done:
        *old_meta = new_meta;
        *old_data = new_data;
        return recognized;
}
