#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <sys/types.h>
#include <fcntl.h>
#include <sys/stat.h>


int
main (int argc, char *argv[])
{
        int   ret  = -1;
        int   fd   = -1;

        fd = open (argv[1], O_CREAT|O_EXCL, 0644);

        if (fd == -1) {
                fprintf (stderr, "creation of the file %s failed (%s)\n", argv[1],
                         strerror (errno));
                goto out;
        }

        ret = 0;

out:
        if (fd > 0)
                close (fd);

        return ret;
}
