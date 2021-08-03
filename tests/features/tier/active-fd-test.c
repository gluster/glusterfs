#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/xattr.h>
#include <errno.h>
#include <string.h>
#include <dirent.h>
#include <sys/sysmacros.h>

#ifndef linux
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/un.h>
#endif

#define OPEN_MODE 0666
/* Execute all possible fops on a fd which is unlinked */
int
fd_based_fops_1(char *filename)
{
    int fd = 0;
    int ret = -1;
    int result = 0;
    struct stat stbuf = {
        0,
    };
    char wstr[50] = {
        0,
    };
    char rstr[50] = {
        0,
    };

    fd = open(filename, O_RDWR, OPEN_MODE);
    if (fd < 0) {
        fprintf(stderr, "open failed : %s\n", strerror(errno));
        return ret;
    }

    ret = unlink(filename);
    if (ret < 0) {
        fprintf(stderr, "unlink failed : %s\n", strerror(errno));
        result |= ret;
    }

    ret = lseek(fd, 0, SEEK_SET);
    if (ret < 0) {
        fprintf(stderr, "lseek failed: %s\n", strerror(errno));
        result |= ret;
    }

    ret = read(fd, rstr, 50);
    if (ret <= 0) {
        fprintf(stderr, "read failed: %s\n", strerror(errno));
        result |= ret;
    }

    fprintf(stderr, "read: %s\n", rstr);

    ret = ftruncate(fd, 0);
    if (ret < 0) {
        fprintf(stderr, "ftruncate failed : %s\n", strerror(errno));
        result |= ret;
    }

    ret = fstat(fd, &stbuf);
    if (ret < 0) {
        fprintf(stderr, "fstat failed : %s\n", strerror(errno));
        result |= ret;
    }

    ret = fsync(fd);
    if (ret < 0) {
        fprintf(stderr, "fsync failed : %s\n", strerror(errno));
        result |= ret;
    }

    ret = fdatasync(fd);
    if (ret < 0) {
        fprintf(stderr, "fdatasync failed : %s\n", strerror(errno));
        result |= ret;
    }

    /*
     *      These metadata operations fail at the moment because kernel doesn't
     *      pass the client fd in the operation.
     *      The following bug tracks this change.
     *      https://bugzilla.redhat.com/show_bug.cgi?id=1084422
     *      ret = fchmod (fd, 0640);
     *      if (ret < 0) {
     *              fprintf (stderr, "fchmod failed : %s\n", strerror (errno));
     *              result |= ret;
     *      }

     *      ret = fchown (fd, 10001, 10001);
     *      if (ret < 0) {
     *              fprintf (stderr, "fchown failed : %s\n", strerror (errno));
     *              result |= ret;
     *      }

     *      ret = fsetxattr (fd, "trusted.xattr-test", "working", 8, 0);
     *      if (ret < 0) {
     *              fprintf (stderr, "fsetxattr failed : %s\n", strerror
     (errno));
     *              result |= ret;
     *      }

     *      ret = flistxattr (fd, NULL, 0);
     *      if (ret <= 0) {
     *              fprintf (stderr, "flistxattr failed : %s\n", strerror
     (errno));
     *              result |= ret;
     *      }

     *      ret = fgetxattr (fd, "trusted.xattr-test", NULL, 0);
     *      if (ret <= 0) {
     *              fprintf (stderr, "fgetxattr failed : %s\n", strerror
     (errno));
     *              result |= ret;
     *      }

     *      ret = fremovexattr (fd, "trusted.xattr-test");
     *      if (ret < 0) {
     *              fprintf (stderr, "fremovexattr failed : %s\n", strerror
     (errno));
     *              result |= ret;
     *      }
     */

    if (fd)
        close(fd);
    return result;
}

int
main(int argc, char *argv[])
{
    int ret = -1;
    int result = 0;
    char filename[255] = {
        0,
    };

    if (argc > 1)
        strcpy(filename, argv[1]);
    else
        strcpy(filename, "temp-xattr-test-file");

    ret = fd_based_fops_1(filename);
    if (ret < 0) {
        fprintf(stderr, "fd based file operation 1 failed\n");
        result |= ret;
    } else {
        fprintf(stdout, "fd based file operation 1 passed\n");
    }

    return result;
}
