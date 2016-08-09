#include <inttypes.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <glusterfs/api/glfs.h>
#include <glusterfs/api/glfs-handles.h>

#define VALIDATE_AND_GOTO_LABEL_ON_ERROR(func, ret, label) do { \
        if (ret < 0) {            \
                fprintf (stderr, "%s : returned error %d (%s)\n", \
                         func, ret, strerror (errno)); \
                goto label; \
        } \
        } while (0)

#define WRITE_SIZE      4096
#define TRUNC_SIZE      1234
/* Make sure TRUNC_SIZE is smaller than WRITE_SIZE at compile time. */
typedef char _size_check[WRITE_SIZE-TRUNC_SIZE];

int
main (int argc, char *argv[])
{
        int             ret = -1;
        int             flags = O_RDWR|O_SYNC;
        glfs_t         *fs = NULL;
        glfs_fd_t      *fd1 = NULL;
        char           *volname = NULL;
        char           *logfile = NULL;
        const char     *filename = "file_tmp";
        const char     buff[WRITE_SIZE];
        struct stat    sb;

        if (argc != 3) {
                fprintf (stderr, "Invalid argument\n");
                return 1;
        }

        volname = argv[1];
        logfile = argv[2];

        fs = glfs_new (volname);
        if (!fs)
                VALIDATE_AND_GOTO_LABEL_ON_ERROR ("glfs_new", ret, out);

        ret = glfs_set_volfile_server (fs, "tcp", "localhost", 24007);
        VALIDATE_AND_GOTO_LABEL_ON_ERROR ("glfs_set_volfile_server", ret, out);

        ret = glfs_set_logging (fs, logfile, 7);
        VALIDATE_AND_GOTO_LABEL_ON_ERROR ("glfs_set_logging", ret, out);

        ret = glfs_init (fs);
        VALIDATE_AND_GOTO_LABEL_ON_ERROR ("glfs_init", ret, out);

        fd1 = glfs_creat(fs, filename, flags, 0644);
        if (fd1 == NULL) {
                ret = -1;
                VALIDATE_AND_GOTO_LABEL_ON_ERROR ("glfs_creat", ret, out);
        }

        ret = glfs_write (fd1, buff, WRITE_SIZE, flags);
        VALIDATE_AND_GOTO_LABEL_ON_ERROR ("glfs_write", ret, out);

        ret = glfs_truncate (fs, filename, TRUNC_SIZE);
        VALIDATE_AND_GOTO_LABEL_ON_ERROR ("glfs_truncate", ret, out);

        ret = glfs_fstat (fd1, &sb);
        VALIDATE_AND_GOTO_LABEL_ON_ERROR ("glfs_fstat", ret, out);

        if (sb.st_size != TRUNC_SIZE) {
                fprintf (stderr, "wrong size %jd should be %jd\n",
                         (intmax_t)sb.st_size, (intmax_t)2048);
                ret = -1;
        }

out:
        if (fd1 != NULL)
                glfs_close(fd1);
        if (fs) {
                /*
                 * If this fails (as it does on Special Snowflake NetBSD for no
                 * good reason), it shouldn't affect the result of the test.
                 */
                (void) glfs_fini(fs);
        }

        return ret;
}


