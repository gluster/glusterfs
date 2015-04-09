#define _GNU_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdint.h>
#include <sys/types.h>
#include <sys/xattr.h>

#include "bit-rot-object-version.h"

/* NOTE: no size discovery */
int
brstub_validate_version (char *bpath, unsigned long version)
{
        int ret = 0;
        int match = 0;
        size_t xsize = 0;
        br_version_t *xv = NULL;

        xsize = sizeof (br_version_t);

        xv = calloc (1, xsize);
        if (!xv)
                goto err;

        ret = getxattr (bpath, "trusted.bit-rot.version", xv, xsize);
        if (ret < 0)
                goto err;

        if (xv->ongoingversion != version)
                match = -1;
        free (xv);

        return match;

 err:
        return -1;
}

int
brstub_open_validation (char *filp, char *bpath, unsigned long startversion)
{
        int fd1 = 0;
        int fd2 = 0;
        int ret = 0;

        /* read only check */
        fd1 = open (filp, O_RDONLY);
        if (fd1 < 0)
                goto err;
        close (fd1);

        ret = brstub_validate_version (bpath, startversion);
        if (ret < 0)
                goto err;

        /* single open (write/) check */
        fd1 = open (filp, O_RDWR);
        if (fd1 < 0)
                goto err;
        close (fd1);

        startversion++;
        ret = brstub_validate_version (bpath, startversion);

        /* multi open (write/) check */
        fd1 = open (filp, O_RDWR);
        if (fd1 < 0)
                goto err;
        fd2 = open (filp, O_WRONLY);
        if (fd1 < 0)
                goto err;
        close (fd1);
        close (fd2);

        /**
         * incremented once per open()/open().../close()/close() sequence
         */
        startversion++;
        ret = brstub_validate_version (bpath, startversion);
        if (ret < 0)
                goto err;

        return 0;

 err:
        return -1;
}

int
brstub_new_object_validate (char *filp, char *brick)
{
        int ret = 0;
        char *fname = NULL;
        char bpath[PATH_MAX] = {0,};

        fname = basename (filp);
        if (!fname)
                goto err;

        (void) snprintf (bpath, PATH_MAX, "%s/%s", brick, fname);

        printf ("Validating initial version..\n");
        ret = brstub_validate_version (bpath, 1);
        if (ret < 0)
                goto err;

        printf ("Validating version on modifications..\n");
        ret = brstub_open_validation (filp, bpath, 1);
        if (ret < 0)
                goto err;

        return 0;

 err:
        return -1;
}

int
main (int argc, char **argv)
{
        int ret = 0;
        char *filp = NULL;
        char *brick = NULL;

        if (argc != 3) {
                printf ("Usage: %s <path> <brick>\n", argv[0]);
                goto err;
        }

        filp = argv[1];
        brick = argv[2];

        printf ("Validating object version [%s]\n", filp);
        ret = brstub_new_object_validate (filp, brick);
        if (ret < 0)
                goto err;

        return 0;

 err:
        return -1;
}
