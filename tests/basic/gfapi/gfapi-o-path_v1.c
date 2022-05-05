#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
//#include <asm-generic/fcntl.h> O_PATH here
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <glusterfs/api/glfs.h>
#include <glusterfs/api/glfs-handles.h>

#ifndef O_PATH
#define O_PATH 010000000
#endif

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
    int flags = O_RDWR | O_SYNC;
    glfs_t *fs = NULL;
    glfs_fd_t *fd1 = NULL;
    glfs_fd_t *fd2 = NULL;
    char *volname = NULL;
    char *logfile = NULL;
    char *hostname = NULL;
    const char *dirname = "dir_tmp";
    const char *filename = "file_tmp";
    const char *buff =
        "An opinion should be the result of thought, "
        "not a substitute for it.";

    if (argc != 4) {
        fprintf(stderr, "Invalid argument\n");
        return 1;
    }

    hostname = argv[1];
    volname = argv[2];
    logfile = argv[3];

    fs = glfs_new(volname);
    if (!fs)
        VALIDATE_AND_GOTO_LABEL_ON_ERROR("glfs_new", ret, out);

    ret = glfs_set_volfile_server(fs, "tcp", hostname, 24007);
    VALIDATE_AND_GOTO_LABEL_ON_ERROR("glfs_set_volfile_server", ret, out);

    ret = glfs_set_logging(fs, logfile, 8);
    VALIDATE_AND_GOTO_LABEL_ON_ERROR("glfs_set_logging", ret, out);

    ret = glfs_init(fs);
    VALIDATE_AND_GOTO_LABEL_ON_ERROR("glfs_init", ret, out);

    fd1 = glfs_creat(fs, filename, O_CREAT, 0644);
    if (fd1 == NULL) {
        ret = -1;
        VALIDATE_AND_GOTO_LABEL_ON_ERROR("glfs_creat", ret, out);
    }
    glfs_close(fd1);
    
    fd1 = glfs_open(fs, filename, O_PATH);
    if (fd1 == NULL) {
        ret = -1;
        VALIDATE_AND_GOTO_LABEL_ON_ERROR("glfs_open(O_PATH)", ret, out);
    }

    ret = glfs_unlink(fs, filename);
    if (ret == -1) {
        VALIDATE_AND_GOTO_LABEL_ON_ERROR("glfs_unlink()", ret, out);
    }

    struct stat stat;
    ret = glfs_fstat(fd1, &stat);
    if (ret == -1) {
        VALIDATE_AND_GOTO_LABEL_ON_ERROR("glfs_fstat()", ret, out);
    }

    ret = 0;
out:
    if (fd1 != NULL)
        glfs_close(fd1);
    if (fs) {
        ret = glfs_fini(fs);
        if (ret)
            fprintf(stderr, "glfs_fini(fs) returned %d\n", ret);
    }

    return ret;
}
