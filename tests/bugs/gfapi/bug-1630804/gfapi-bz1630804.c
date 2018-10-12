#include <inttypes.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <stdlib.h>
#include <glusterfs/api/glfs.h>
#include <glusterfs/api/glfs-handles.h>

#define VALIDATE_AND_GOTO_LABEL_ON_ERROR(func, ret, label)                     \
    do {                                                                       \
        if (ret < 0) {                                                         \
            fprintf(stderr, "%s : returned error %d (%s)\n", func, ret,        \
                    strerror(errno));                                          \
            goto label;                                                        \
        }                                                                      \
    } while (0)

int
main(int argc, char *argv[])
{
    int ret = -1;
    int flags = O_WRONLY | O_CREAT | O_TRUNC;
    int do_write = 0;
    glfs_t *fs = NULL;
    glfs_fd_t *fd1 = NULL;
    glfs_fd_t *fd2 = NULL;
    char *volname = NULL;
    char *logfile = NULL;
    const char *dirname = "/some_dir1";
    const char *filename = "/some_dir1/testfile";
    const char *short_filename = "testfile";
    struct stat sb;
    char buf[512];
    struct dirent *entry = NULL;

    if (argc != 4) {
        fprintf(stderr, "Invalid argument\n");
        fprintf(stderr, "Usage: %s <volname> <logfile> <do-write [0/1]\n",
                argv[0]);
        return 1;
    }

    volname = argv[1];
    logfile = argv[2];
    do_write = atoi(argv[3]);

    fs = glfs_new(volname);
    if (!fs)
        VALIDATE_AND_GOTO_LABEL_ON_ERROR("glfs_new", ret, out);

    ret = glfs_set_volfile_server(fs, "tcp", "localhost", 24007);
    VALIDATE_AND_GOTO_LABEL_ON_ERROR("glfs_set_volfile_server", ret, out);

    ret = glfs_set_logging(fs, logfile, 7);
    VALIDATE_AND_GOTO_LABEL_ON_ERROR("glfs_set_logging", ret, out);

    ret = glfs_init(fs);
    VALIDATE_AND_GOTO_LABEL_ON_ERROR("glfs_init", ret, out);

    ret = glfs_mkdir(fs, dirname, 0755);
    if (ret && errno != EEXIST)
        VALIDATE_AND_GOTO_LABEL_ON_ERROR("glfs_mkdir", ret, out);

    fd1 = glfs_creat(fs, filename, flags, 0644);
    if (fd1 == NULL) {
        ret = -1;
        VALIDATE_AND_GOTO_LABEL_ON_ERROR("glfs_creat", ret, out);
    }

    if (do_write) {
        ret = glfs_write(fd1, "hello world", 11, flags);
        VALIDATE_AND_GOTO_LABEL_ON_ERROR("glfs_write", ret, out);
    }

    fd2 = glfs_opendir(fs, dirname);
    if (fd2 == NULL) {
        ret = -1;
        VALIDATE_AND_GOTO_LABEL_ON_ERROR("glfs_opendir", ret, out);
    }

    do {
        ret = glfs_readdirplus_r(fd2, &sb, (struct dirent *)buf, &entry);
        if (entry != NULL) {
            if (!strcmp(entry->d_name, short_filename)) {
                if (sb.st_mode == 0) {
                    fprintf(
                        stderr,
                        "Mode bits are incorrect: d_name - %s, st_mode - %jd\n",
                        entry->d_name, (intmax_t)sb.st_mode);
                    ret = -1;
                    goto out;
                }
            }
        }
    } while (entry != NULL);

out:
    if (fd1 != NULL)
        glfs_close(fd1);
    if (fd2 != NULL)
        glfs_closedir(fd2);

    if (fs) {
        /*
         * If this fails (as it does on Special Snowflake NetBSD for no
         * good reason), it shouldn't affect the result of the test.
         */
        (void)glfs_fini(fs);
    }

    return ret;
}
