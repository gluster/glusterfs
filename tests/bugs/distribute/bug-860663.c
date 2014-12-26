#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <fcntl.h>
#include <string.h>
#include <err.h>
#include <sys/param.h>

int
main(argc, argv)
        int argc;
        char **argv;
{
        char *basepath;
        char path[MAXPATHLEN + 1];
        unsigned int count;
        int i, fd;

        if (argc != 3)
                errx(1, "usage: %s path count", argv[0]);

        basepath = argv[1];
        count = atoi(argv[2]);

        if (count > 999999)
                errx(1, "count too big");

        if (strlen(basepath) > MAXPATHLEN - 6)
                errx(1, "path too long");

        for (i = 0; i < count; i++) {
                (void)sprintf(path, "%s%06d", basepath, i);

                fd = open(path, O_CREAT|O_RDWR, 0644);
                if (fd == -1)
                        err(1, "create %s failed", path);

                if (close(fd) != 0)
                        warn("close %s failed", path);
        }

        return 0;
}
