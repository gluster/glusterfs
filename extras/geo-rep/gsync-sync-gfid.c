
#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <limits.h>
#include <sys/types.h>
#include <libgen.h>
#include <ctype.h>
#include <stdlib.h>
#include "glusterfs.h"
#include "syscall.h"

#ifndef UUID_CANONICAL_FORM_LEN
#define UUID_CANONICAL_FORM_LEN 36
#endif

#ifndef GF_FUSE_AUX_GFID_HEAL
#define GF_FUSE_AUX_GFID_HEAL    "glusterfs.gfid.heal"
#endif

#define GLFS_LINE_MAX           (PATH_MAX + (2 * UUID_CANONICAL_FORM_LEN))

int
main (int argc, char *argv[])
{
        char *file                = NULL;
        char *tmp = NULL;
        char *tmp1 = NULL;
        char *parent_dir = NULL;
        char *gfid                = NULL;
        char *bname = NULL;
        int   ret                 = -1;
        int len = 0;
        FILE *fp                  = NULL;
        char  line[GLFS_LINE_MAX] = {0,};
        char *path = NULL;
        void  *blob               = NULL;
        void  *tmp_blob               = NULL;

        if (argc != 2) {
                /* each line in the file has the following format
                 * uuid-in-canonical-form path-relative-to-gluster-mount.
                 * Both uuid and relative path are from master mount.
                 */
                fprintf (stderr, "usage: %s <file-of-paths-to-be-synced>\n",
                         argv[0]);
                goto out;
        }

        file = argv[1];

        fp = fopen (file, "r");
        if (fp == NULL) {
                fprintf (stderr, "cannot open %s for reading (%s)\n",
                         file, strerror (errno));
                goto out;
        }

        while (fgets (line, GLFS_LINE_MAX, fp) != NULL) {
                tmp = line;
                path = gfid = line;

                path += UUID_CANONICAL_FORM_LEN + 1;

                while(isspace (*path))
                        path++;

                if ((strlen (line) < GLFS_LINE_MAX) &&
                    (line[strlen (line) - 1] == '\n'))
                        line[strlen (line) - 1] = '\0';

                line[UUID_CANONICAL_FORM_LEN] = '\0';

                tmp = strdup (path);
                tmp1 = strdup (path);
                parent_dir = dirname (tmp);
                bname = basename (tmp1);

                /* gfid + '\0' + bname + '\0' */
                len = UUID_CANONICAL_FORM_LEN + 1 + strlen (bname) + 1;

                blob = calloc (1, len);

                memcpy (blob, gfid, UUID_CANONICAL_FORM_LEN);

                tmp_blob = blob + UUID_CANONICAL_FORM_LEN + 1;

                memcpy (tmp_blob, bname, strlen (bname));

                ret = sys_lsetxattr (parent_dir, GF_FUSE_AUX_GFID_HEAL, 
                                blob, len, 0);
                if (ret < 0) {
                        fprintf (stderr, "setxattr on %s/%s failed (%s)\n",
                                 parent_dir, bname, strerror (errno));
                }
                memset (line, 0, GLFS_LINE_MAX);

                free (blob);
                free (tmp); free (tmp1);
                blob = NULL;
        }

        ret = 0;
out:
        if (fp)
                fclose(fp);
        return ret;
}

