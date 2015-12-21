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
#include <errno.h>

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
        if (!xv) {
                match = -1;
                goto err;
        }

        ret = getxattr (bpath, "trusted.bit-rot.version", xv, xsize);
        if (ret < 0) {
                if (errno == ENODATA)
                        match = -2;
                goto err;
        }

        if (xv->ongoingversion != version) {
                match = -3;
                fprintf (stderr, "ongoingversion: %lu\n", xv->ongoingversion);
        }
        free (xv);

 err:
        return match;
}

int
brstub_write_validation (char *filp, char *bpath, unsigned long startversion)
{
        int fd1 = 0;
        int fd2 = 0;
        int ret = 0;
        char *string = "string\n";

        /* read only check */
        fd1 = open (filp, O_RDONLY);
        if (fd1 < 0)
                goto err;
        close (fd1);

        ret = brstub_validate_version (bpath, startversion);
        if (ret != -2)
                goto err;

        /* single open (write/) check */
        fd1 = open (filp, O_RDWR);
        if (fd1 < 0)
                goto err;

        ret = write (fd1, string, strlen (string));
        if (ret <= 0)
                goto err;
        /**
         * Fsync is done so that the write call has properly reached the
         * disk. For fuse mounts write-behind xlator would have held the
         * writes with itself and for nfs, client would have held the
         * write in its cache. So write fop would not have triggered the
         * versioning as it would have not reached the bit-rot-stub.
         */
        fsync (fd1);
        ret = brstub_validate_version (bpath, startversion);
        if (ret != 0)
                goto err;
        ret = write (fd1, string, strlen (string));
        if (ret <= 0)
                goto err;
        fsync (fd1); /* let it reach the disk */

        ret = brstub_validate_version (bpath, startversion);
        if (ret != 0)
                goto err;

        close (fd1);

        /**
         * Well, this is not a _real_ test per se . For this test to pass
         * the inode should not get a forget() in the interim. Therefore,
         * perform this test asap.
         */

        /* multi open (write/) check */
        fd1 = open (filp, O_RDWR);
        if (fd1 < 0)
                goto err;
        fd2 = open (filp, O_WRONLY);
        if (fd1 < 0)
                goto err;

        ret = write (fd1, string, strlen (string));
        if (ret <= 0)
                goto err;

        ret = write (fd2, string, strlen (string));
        if (ret <= 0)
                goto err;

        /* probably do a syncfs() */
        fsync (fd1);
        fsync (fd2);

        close (fd1);
        close (fd2);

        /**
         * incremented once per write()/write().../close()/close() sequence
         */
        ret = brstub_validate_version (bpath, startversion);
        if (ret != 0)
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
        ret = brstub_validate_version (bpath, 2);
        if (ret != -2) /* version _should_ be missing */
                goto err;

        printf ("Validating version on modifications..\n");
        ret = brstub_write_validation (filp, bpath, 2);
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
