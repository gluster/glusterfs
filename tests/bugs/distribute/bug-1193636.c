#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <attr/xattr.h>
#include <fcntl.h>
#include <string.h>


#define MY_XATTR_NAME   "user.ftest"
#define MY_XATTR_VAL    "ftestval"


void usage (void)
{
        printf ("Usage : bug-1193636 <filename> <xattr_name> <op>\n");
        printf ("   op : 0 - set, 1 - remove\n");
}


int main (int argc, char **argv)
{
        int fd;
        int err = 0;
        char *xattr_name = NULL;
        int op = 0;

        if (argc != 4) {
                usage ();
                exit (1);
        }

        op = atoi (argv[3]);

        if ((op != 0) && (op != 1)) {
                printf ("Invalid operation specified.\n");
                usage ();
                exit (1);
        }

        xattr_name = argv[2];

        fd = open(argv[1], O_RDWR);
        if (fd == -1) {
                printf ("Failed to open file %s\n", argv[1]);
                exit (1);
        }

        if (!op) {
                err = fsetxattr (fd, xattr_name, MY_XATTR_VAL,
                                 strlen (MY_XATTR_VAL) + 1, XATTR_CREATE);

                if (err) {
                        printf ("Failed to set xattr %s: %m\n", xattr_name);
                        exit (1);
                }

        } else {
                err = fremovexattr (fd, xattr_name);

                if (err) {
                        printf ("Failed to remove xattr %s: %m\n", xattr_name);
                        exit (1);
                }
        }

        close (fd);

        return 0;
}

