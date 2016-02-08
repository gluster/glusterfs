#pragma fragment PROLOG
#ifndef _CONFIG_H
#define _CONFIG_H
#include "config.h"
#endif

#include "glfs.h"
#include "iatt.h"
#include "xlator.h"
#include "jnl-types.h"

#pragma fragment DICT
        {
                int key_len, data_len;
                char *key_ptr;
                printf ("@ARGNAME@ = dict {\n");
                for (;;) {
                        key_len = *((int *)new_meta);
                        new_meta += sizeof(int);
                        if (!key_len) {
                                break;
                        }
                        key_ptr = new_meta;
                        new_meta += key_len;
                        data_len = *((int *)new_meta);
                        new_meta += sizeof(int) + data_len;
                        printf (" %s = <%d bytes>\n", key_ptr, data_len);
                }
                printf ("}\n");
        }

#pragma fragment DOUBLE
        printf ("@ARGNAME@ = @FORMAT@\n", *((uint64_t *)new_meta),
                *((uint64_t *)new_meta));
        new_meta += sizeof(uint64_t);

#pragma fragment GFID
        printf ("@ARGNAME@ = <gfid %s>\n", uuid_utoa(*((uuid_t *)new_meta)));
        new_meta += 16;

#pragma fragment INTEGER
        printf ("@ARGNAME@ = @FORMAT@\n", *((uint32_t *)new_meta),
                *((uint32_t *)new_meta));
        new_meta += sizeof(uint32_t);

#pragma fragment LOC
        printf ("@ARGNAME@ = loc {\n");
        printf ("  gfid = %s\n", uuid_utoa(*((uuid_t *)new_meta)));
        new_meta += 16;
        printf ("  pargfid = %s\n", uuid_utoa(*((uuid_t *)new_meta)));
        new_meta += 16;
        if (*(new_meta++)) {
                printf ("  name = %s\n", new_meta);
                new_meta += (strlen(new_meta) + 1);
        }
        printf ("}\n");

#pragma fragment STRING
        if (*(new_meta++)) {
                printf ("@ARGNAME@ = %s\n", new_meta);
                new_meta += (strlen(new_meta) + 1);
        }

#pragma fragment VECTOR
        {
                size_t len = *((size_t *)new_meta);
                new_meta += sizeof(len);
                printf ("@ARGNAME@ = <%zu bytes>\n", len);
                new_data += len;
        }

#pragma fragment IATT
        {
                ia_prot_t *myprot = ((ia_prot_t *)new_meta);
                printf ("@ARGNAME@ = iatt {\n");
                printf ("  ia_prot = %c%c%c",
                        myprot->suid ? 'S' : '-',
                        myprot->sgid ? 'S' : '-',
                        myprot->sticky ? 'T' : '-');
                printf ("%c%c%c",
                        myprot->owner.read ? 'r' : '-',
                        myprot->owner.write ? 'w' : '-',
                        myprot->owner.exec ? 'x' : '-');
                printf ("%c%c%c",
                        myprot->group.read ? 'r' : '-',
                        myprot->group.write ? 'w' : '-',
                        myprot->group.exec ? 'x' : '-');
                printf ("%c%c%c\n",
                        myprot->other.read ? 'r' : '-',
                        myprot->other.write ? 'w' : '-',
                        myprot->other.exec ? 'x' : '-');
                new_meta += sizeof(ia_prot_t);
                uint32_t *myints = (uint32_t *)new_meta;
                printf ("  ia_uid = %u\n", myints[0]);
                printf ("  ia_gid = %u\n", myints[1]);
                printf ("  ia_atime = %u.%09u\n", myints[2], myints[3]);
                printf ("  ia_mtime = %u.%09u\n", myints[4], myints[5]);
                new_meta += sizeof(*myints) * 6;
        }

#pragma fragment FOP
void
fdl_dump_@NAME@ (char **old_meta, char **old_data)
{
        char    *new_meta	= *old_meta;
        char	*new_data	= *old_data;

        /* TBD: word size/endianness */
@FUNCTION_BODY@

        *old_meta = new_meta;
        *old_data = new_data;
}

#pragma fragment CASE
        case GF_FOP_@UPNAME@:
                printf ("=== GF_FOP_@UPNAME@\n");
                fdl_dump_@NAME@ (&new_meta, &new_data);
                break;

#pragma fragment EPILOG
int
fdl_dump (char **old_meta, char **old_data)
{
        char            *new_meta       = *old_meta;
        char            *new_data       = *old_data;
        static glfs_t   *fs             = NULL;
        int             recognized      = 1;
        event_header_t  *eh;

        /*
         * We don't really call anything else in GFAPI, but this is the most
         * convenient way to satisfy all of the spurious dependencies on how it
         * or glusterfsd initialize (e.g. setting up THIS).
         */
        if (!fs) {
                fs = glfs_new ("dummy");
        }

        eh = (event_header_t *)new_meta;
        new_meta += sizeof (*eh);

        /* TBD: check event_type instead of assuming NEW_REQUEST */

        switch (eh->fop_type) {
@SWITCH_BODY@

        default:
                printf ("unknown fop %u\n", eh->fop_type);
                recognized = 0;
        }

        *old_meta = new_meta;
        *old_data = new_data;
        return recognized;
}
