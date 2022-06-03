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

#ifndef AT_SYMLINK_FOLLOW
#define AT_SYMLINK_FOLLOW 0x400
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
    glfs_fd_t *fd3 = NULL;
    char *volname = NULL;
    char *logfile = NULL;
    char *hostname = NULL;
    const char *topdir = "/dir_tmp";
    const char *filename = "file_tmp";
    const char *filename2 = "file_tmp_2";
    const char *filename_linkat = "file_tmp_linkat";
    const char *filename_symlinkat = "file_tmp_symlinkat";
    const char *filepath = "/dir_tmp/file_tmp";
    const char *filepath_linkat = "/dir_tmp/file_tmp_linkat";
    const char *filepath_symlinkat = "/dir_tmp/file_tmp_symlinkat";
    const char *buff =
        "An opinion should be the result of thought, "
        "not a substitute for it.";

    struct stat buf = {
        0,
    };

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

    ret = glfs_set_logging(fs, logfile, 9);
    VALIDATE_AND_GOTO_LABEL_ON_ERROR("glfs_set_logging", ret, out);

    ret = glfs_init(fs);
    VALIDATE_AND_GOTO_LABEL_ON_ERROR("glfs_init", ret, out);

    ret = glfs_mkdir(fs, topdir, 0755);
    fprintf(stderr, "mkdir(%s): %s\n", topdir, strerror(errno));
    if (ret) {
        ret = -1;
        VALIDATE_AND_GOTO_LABEL_ON_ERROR("glfs_mkdir", ret, out);
    }

    fd1 = glfs_creat(fs, filepath, O_CREAT | O_RDWR, 0644);
    if (fd1 == NULL) {
        ret = -1;
        VALIDATE_AND_GOTO_LABEL_ON_ERROR("glfs_creat", ret, out);
    }
    glfs_close(fd1);

    fd1 = glfs_open(fs, topdir, O_PATH);
    if (fd1 == NULL) {
        ret = -1;
        VALIDATE_AND_GOTO_LABEL_ON_ERROR("glfs_open(O_PATH)", ret, out);
    }

    fd2 = glfs_openat(fd1, filename, O_RDWR, 0);
    if (fd2 == NULL) {
        ret = -1;
        VALIDATE_AND_GOTO_LABEL_ON_ERROR("glfs_openat(O_RDWR)", ret, out);
    }

    fd3 = glfs_openat(fd1, filename2, O_CREAT | O_RDWR, 0644);
    if (fd3 == NULL) {
        ret = -1;
        VALIDATE_AND_GOTO_LABEL_ON_ERROR("glfs_openat(O_CREAT | O_RDWR)", ret,
                                         out);
    }

    ret = glfs_write(fd2, buff, strlen(buff), flags);
    VALIDATE_AND_GOTO_LABEL_ON_ERROR("glfs_write(filename)", ret, out);

    ret = glfs_write(fd3, buff, strlen(buff), flags);
    VALIDATE_AND_GOTO_LABEL_ON_ERROR("glfs_write(filename_2)", ret, out);

    // TEST for 'AT_SYMLINK_FOLLOW' flag.
    // ret = glfs_symlink(fs, filepath, filepath_symlinkat);
    // VALIDATE_AND_GOTO_LABEL_ON_ERROR("glfs_symlink", ret, out);

    // ret = glfs_lstat(fs, filepath_symlinkat, &buf);
    // if (S_ISLNK(buf.st_mode)) {
    //     fprintf(stderr, "It is a symlink");
    // }
    // ret = glfs_linkat(fd1, filename_symlinkat, filename_linkat,
    //                   AT_SYMLINK_FOLLOW);

    ret = glfs_linkat(fd1, filename, filename_linkat, flags);
    VALIDATE_AND_GOTO_LABEL_ON_ERROR("glfs_linkat", ret, out);

    ret = glfs_stat(fs, filepath_linkat, &buf);
    VALIDATE_AND_GOTO_LABEL_ON_ERROR("glfs_stat", ret, out);

    /* Number of links will be >2 when oldpath is a REG_FILE, or a symlink
       with 'AT_SYMLINK_FOLLOW' is set.
       If oldpath is symlink and 'AT_SYMLINK_FOLLOW' is not set then
       number of links will be minimum 3.
    */
    if (buf.st_nlink < 2) {
        ret = -1;
        VALIDATE_AND_GOTO_LABEL_ON_ERROR("glfs_linkat operation failed", ret,
                                         out);
    }

    ret = 0;
out:
    if (fd2 != NULL)
        glfs_close(fd2);
    if (fd3 != NULL)
        glfs_close(fd3);
    if (fd1 != NULL)
        glfs_close(fd1);
    if (fs) {
        (void)glfs_fini(fs);
    }

    return ret;
}