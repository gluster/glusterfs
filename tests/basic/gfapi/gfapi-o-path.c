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

#ifndef AT_SYMLINK_NOFOLLOW
#define AT_SYMLINK_NOFOLLOW 0x100
#endif

#ifndef AT_SYMLINK_FOLLOW
#define AT_SYMLINK_FOLLOW 0x400
#endif

#ifndef AT_EMPTY_PATH
#define AT_EMPTY_PATH 0x1000
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
    glfs_fd_t *fd4a = NULL;
    glfs_fd_t *fd4b = NULL;
    glfs_fd_t *fd5 = NULL;
    char *volname = NULL;
    char *logfile = NULL;
    char *hostname = NULL;
    const char *topdir = "/dir_tmp";
    const char *mkdirat = "/dir_tmp_mkdirat";
    const char *emptypath_dir = "/dir_tmp/emptypath_dir";
    const char *filename = "file_tmp";
    const char *filename2 = "file_tmp_2";
    const char *filename_linkat = "file_tmp_linkat";
    const char *filename_symlinkat = "file_tmp_symlinkat";
    const char *filename_BLK = "file_tmp_BLK";
    const char *filename_renameat = "file_tmp_renameat";
    const char *filename_renameat2 = "file_tmp_renameat2";
    const char *filename_unlinkat = "file_tmp_unlinkat";
    const char *dirname_mkdir2_unlinkat = "mkdir2_unlinkat";
    const char *filename_emptypath = "file_tmp_emptypath";
    const char *filepath = "/dir_tmp/file_tmp";
    const char *filepath_linkat = "/dir_tmp/file_tmp_linkat";
    const char *filepath_symlinkat = "/dir_tmp/file_tmp_symlinkat";
    const char *filepath_BLK = "/dir_tmp/file_tmp_BLK";
    const char *filepath_renameat = "/dir_tmp/file_tmp_renameat";
    const char *filepath_renameat2 = "/dir_tmp/file_tmp_renameat2";
    const char *filepath_unlinkat = "/dir_tmp/file_tmp_unlinkat";
    const char *dirpath_mkdir2_unlinkat = "/dir_tmp/mkdir2_unlinkat";
    const char
        *filepath_emptypath = "/dir_tmp/emptypath_dir/file_tmp_emptypath";

    const char *buff =
        "An opinion should be the result of thought, "
        "not a substitute for it.";
    struct stat stbuf = {
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

    ret = glfs_mkdir(fs, emptypath_dir, 0777);
    fprintf(stderr, "mkdir(%s): %s\n", emptypath_dir, strerror(errno));
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

    fd1 = glfs_creat(fs, filepath_unlinkat, O_CREAT | O_RDWR, 0644);
    if (fd1 == NULL) {
        ret = -1;
        VALIDATE_AND_GOTO_LABEL_ON_ERROR("glfs_creat", ret, out);
    }
    glfs_close(fd1);

    fd1 = glfs_creat(fs, filepath_emptypath, O_CREAT | O_RDWR, 0777);
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

    // For AT_EMPTYPATH flag test on Dir file.
    fd4a = glfs_open(fs, emptypath_dir, O_PATH);
    if (fd4a == NULL) {
        ret = -1;
        VALIDATE_AND_GOTO_LABEL_ON_ERROR("glfs_open(O_PATH)", ret, out);
    }

    // For AT_EMPTYPATH flag test on normal file.
    fd4b = glfs_open(fs, filepath_emptypath, O_PATH);
    if (fd4b == NULL) {
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

    fd5 = glfs_openat(fd4a, filename_emptypath, O_RDWR, 0777);
    if (fd5 == NULL) {
        ret = -1;
        VALIDATE_AND_GOTO_LABEL_ON_ERROR("glfs_openat(O_CREAT | O_RDWR)", ret,
                                         out);
    }

    ret = glfs_write(fd5, buff, strlen(buff), flags);
    VALIDATE_AND_GOTO_LABEL_ON_ERROR("glfs_write(filename)", ret, out);

    ret = glfs_write(fd2, buff, strlen(buff), flags);
    VALIDATE_AND_GOTO_LABEL_ON_ERROR("glfs_write(filename)", ret, out);

    ret = glfs_write(fd3, buff, strlen(buff), flags);
    VALIDATE_AND_GOTO_LABEL_ON_ERROR("glfs_write(filename_2)", ret, out);

    ret = glfs_fstatat(fd1, filename, &stbuf, 0);
    VALIDATE_AND_GOTO_LABEL_ON_ERROR("glfs_fstatat", ret, out);

    if (strlen(buff) != stbuf.st_size) {
        ret = -1;
        VALIDATE_AND_GOTO_LABEL_ON_ERROR("glfs_fstatat(Size mismatch)", ret,
                                         out);
    }

    // fstatat on directory file with `AT_EMPTY_PATH'
    ret = glfs_fstatat(fd4a, "", &stbuf, AT_EMPTY_PATH);
    VALIDATE_AND_GOTO_LABEL_ON_ERROR("glfs_fstatat", ret, out);

    if (stbuf.st_mode & 0777 != 0777) {
        ret = -1;
        VALIDATE_AND_GOTO_LABEL_ON_ERROR(
            "glfs_fstatat(AT_EMPTY_PATH) operation failed", ret, out);
    }

    // fstatat on normal file with `AT_EMPTY_PATH'
    ret = glfs_fstatat(fd4b, "", &stbuf, AT_EMPTY_PATH);
    VALIDATE_AND_GOTO_LABEL_ON_ERROR("glfs_fstatat", ret, out);

    if (strlen(buff) != stbuf.st_size) {
        ret = -1;
        VALIDATE_AND_GOTO_LABEL_ON_ERROR(
            "glfs_fstatat(AT_EMPTY_PATH) operation failed", ret, out);
    }

    ret = glfs_mkdirat(fd1, mkdirat, 0755);
    if (ret) {
        ret = -1;
        VALIDATE_AND_GOTO_LABEL_ON_ERROR("glfs_mkdirat", ret, out);
    }

    ret = glfs_faccessat(fd1, filename, F_OK, flags);
    VALIDATE_AND_GOTO_LABEL_ON_ERROR("glfs_faccessat", ret, out);

    ret = glfs_fchmodat(fd1, filename, 0777, flags);
    VALIDATE_AND_GOTO_LABEL_ON_ERROR("glfs_fchmodat", ret, out);

    ret = glfs_stat(fs, filepath, &stbuf);
    VALIDATE_AND_GOTO_LABEL_ON_ERROR("glfs_fstat", ret, out);
    if (stbuf.st_mode & 0777 != 0777) {
        ret = -1;
        VALIDATE_AND_GOTO_LABEL_ON_ERROR("glfs_fchmodat operation failed", ret,
                                         out);
    }

    ret = glfs_fchownat(fd4a, "", 1001, 1001, AT_EMPTY_PATH);
    VALIDATE_AND_GOTO_LABEL_ON_ERROR("glfs_fchownat", ret, out);

    ret = glfs_stat(fs, emptypath_dir, &stbuf);
    VALIDATE_AND_GOTO_LABEL_ON_ERROR("glfs_stat", ret, out);

    if (stbuf.st_uid != 1001 || stbuf.st_gid != 1001) {
        ret = -1;
        VALIDATE_AND_GOTO_LABEL_ON_ERROR("glfs_fchownat operation failed1", ret,
                                         out);
    }

    ret = glfs_fchownat(fd4b, "", 1001, 1001, AT_EMPTY_PATH);
    VALIDATE_AND_GOTO_LABEL_ON_ERROR("glfs_fchownat", ret, out);

    ret = glfs_stat(fs, filepath_emptypath, &stbuf);
    VALIDATE_AND_GOTO_LABEL_ON_ERROR("glfs_stat", ret, out);

    if (stbuf.st_uid != 1001 || stbuf.st_gid != 1001) {
        ret = -1;
        VALIDATE_AND_GOTO_LABEL_ON_ERROR("glfs_fchownat operation failed2", ret,
                                         out);
    }

    ret = glfs_fchownat(fd1, filename, 1001, 1001, 0);
    VALIDATE_AND_GOTO_LABEL_ON_ERROR("glfs_fchownat", ret, out);

    ret = glfs_stat(fs, filepath, &stbuf);
    VALIDATE_AND_GOTO_LABEL_ON_ERROR("glfs_stat", ret, out);

    if (stbuf.st_uid != 1001 || stbuf.st_gid != 1001) {
        ret = -1;
        VALIDATE_AND_GOTO_LABEL_ON_ERROR("glfs_fchownat operation failed3", ret,
                                         out);
    }

    // TESTS for 'AT_SYMLINK_FOLLOW' flag.
    // Test for symlink file with 'AT_SYMLINK_FOLLOW' not set

    int filename_symlinkat_inode = 0;
    int filename_linkat_inode = 0;
    int filename_inode = 0;
    int filename_emptypath_inode = 0;

    ret = glfs_symlink(fs, filepath, filepath_symlinkat);
    VALIDATE_AND_GOTO_LABEL_ON_ERROR("glfs_symlink", ret, out);

    ret = glfs_lstat(fs, filepath_symlinkat, &stbuf);
    VALIDATE_AND_GOTO_LABEL_ON_ERROR("glfs_lstat", ret, out);
    filename_symlinkat_inode = stbuf.st_ino;

    ret = glfs_linkat(fd1, filename_symlinkat, fd1, filename_linkat, 0);
    VALIDATE_AND_GOTO_LABEL_ON_ERROR("glfs_linkat", ret, out);

    ret = glfs_lstat(fs, filepath_linkat, &stbuf);
    VALIDATE_AND_GOTO_LABEL_ON_ERROR("glfs_lstat", ret, out);
    filename_linkat_inode = stbuf.st_ino;

    if (filename_symlinkat_inode != filename_linkat_inode) {
        ret = -1;
        VALIDATE_AND_GOTO_LABEL_ON_ERROR("glfs_linkat operation failed", ret,
                                         out);
    }

    ret = glfs_unlink(fs, filepath_symlinkat);
    VALIDATE_AND_GOTO_LABEL_ON_ERROR("glfs_unlink", ret, out);
    ret = glfs_unlink(fs, filepath_linkat);
    VALIDATE_AND_GOTO_LABEL_ON_ERROR("glfs_unlink", ret, out);

    // Test for symlink file with 'AT_SYMLINK_FOLLOW' set
    ret = glfs_symlink(fs, filepath, filepath_symlinkat);
    VALIDATE_AND_GOTO_LABEL_ON_ERROR("glfs_symlink", ret, out);

    ret = glfs_linkat(fd1, filename_symlinkat, fd1, filename_linkat,
                      AT_SYMLINK_FOLLOW);

    ret = glfs_stat(fs, filepath_linkat, &stbuf);
    VALIDATE_AND_GOTO_LABEL_ON_ERROR("glfs_stat", ret, out);
    filename_linkat_inode = stbuf.st_ino;

    ret = glfs_stat(fs, filepath, &stbuf);
    VALIDATE_AND_GOTO_LABEL_ON_ERROR("glfs_stat", ret, out);
    filename_inode = stbuf.st_ino;

    if (filename_linkat_inode != filename_inode) {
        ret = -1;
        VALIDATE_AND_GOTO_LABEL_ON_ERROR("glfs_linkat operation failed", ret,
                                         out);
    }

    ret = glfs_unlink(fs, filepath_symlinkat);
    VALIDATE_AND_GOTO_LABEL_ON_ERROR("glfs_unlink", ret, out);
    ret = glfs_unlink(fs, filepath_linkat);
    VALIDATE_AND_GOTO_LABEL_ON_ERROR("glfs_unlink", ret, out);

    // TEST without 'AT_SYMLINK_FOLLOW' flag & symlink file.
    ret = glfs_linkat(fd1, filename, fd1, filename_linkat, 0);
    VALIDATE_AND_GOTO_LABEL_ON_ERROR("glfs_linkat", ret, out);

    ret = glfs_stat(fs, filepath, &stbuf);
    VALIDATE_AND_GOTO_LABEL_ON_ERROR("glfs_stat", ret, out);
    filename_inode = stbuf.st_ino;

    ret = glfs_stat(fs, filepath_linkat, &stbuf);
    VALIDATE_AND_GOTO_LABEL_ON_ERROR("glfs_stat", ret, out);
    filename_linkat_inode = stbuf.st_ino;

    if (filename_inode != filename_linkat_inode) {
        ret = -1;
        VALIDATE_AND_GOTO_LABEL_ON_ERROR("glfs_linkat operation failed", ret,
                                         out);
    }

    ret = glfs_unlink(fs, filepath_linkat);
    VALIDATE_AND_GOTO_LABEL_ON_ERROR("glfs_unlink", ret, out);

    // TEST with `AT_EMPTY_PATH`
    ret = glfs_linkat(fd4b, "", fd1, filename_linkat, AT_EMPTY_PATH);
    VALIDATE_AND_GOTO_LABEL_ON_ERROR("glfs_linkat", ret, out);

    ret = glfs_stat(fs, filepath_emptypath, &stbuf);
    VALIDATE_AND_GOTO_LABEL_ON_ERROR("glfs_stat", ret, out);
    filename_emptypath_inode = stbuf.st_ino;

    ret = glfs_stat(fs, filepath_linkat, &stbuf);
    VALIDATE_AND_GOTO_LABEL_ON_ERROR("glfs_stat", ret, out);
    filename_linkat_inode = stbuf.st_ino;

    if (filename_emptypath_inode != filename_linkat_inode) {
        ret = -1;
        VALIDATE_AND_GOTO_LABEL_ON_ERROR("glfs_linkat operation failed", ret,
                                         out);
    }

    ret = glfs_mknodat(fd1, filename_BLK, __S_IFBLK | 0777, 0);
    VALIDATE_AND_GOTO_LABEL_ON_ERROR("glfs_mknodat", ret, out);

    ret = glfs_stat(fs, filepath_BLK, &stbuf);
    VALIDATE_AND_GOTO_LABEL_ON_ERROR("glfs_stat", ret, out);

    if (!S_ISBLK(stbuf.st_mode) || (stbuf.st_mode & 0777) != 0777) {
        ret = -1;
        VALIDATE_AND_GOTO_LABEL_ON_ERROR("glfs_mknodat operation failed", ret,
                                         out);
    }

    ret = glfs_symlink(fs, filepath, filepath_symlinkat);
    VALIDATE_AND_GOTO_LABEL_ON_ERROR("glfs_symlink", ret, out);

    ret = glfs_lstat(fs, filepath_symlinkat, &stbuf);
    VALIDATE_AND_GOTO_LABEL_ON_ERROR("glfs_lstat", ret, out);

    char buffer[56];
    ret = glfs_readlinkat(fd1, filename_symlinkat, buffer, sizeof(buffer));
    VALIDATE_AND_GOTO_LABEL_ON_ERROR("glfs_readlinkat", ret, out);

    if (ret != stbuf.st_size) {
        ret = -1;
        VALIDATE_AND_GOTO_LABEL_ON_ERROR("glfs_readlinkat buf size mismatch",
                                         ret, out);
    }

    ret = glfs_renameat(fd1, filename, fd1, filename_renameat);
    VALIDATE_AND_GOTO_LABEL_ON_ERROR("glfs_renameat", ret, out);

    if (glfs_access(fs, filepath, F_OK) == 0 ||
        glfs_access(fs, filepath_renameat, F_OK) == -1) {
        ret = -1;
        VALIDATE_AND_GOTO_LABEL_ON_ERROR("glfs_renameat operation has failed",
                                         ret, out);
    }

    ret = glfs_renameat2(fd1, filename_renameat, fd1, filename_renameat2,
                         flags);
    VALIDATE_AND_GOTO_LABEL_ON_ERROR("glfs_renameat2", ret, out);

    if (glfs_access(fs, filepath_renameat, F_OK) == 0 ||
        glfs_access(fs, filepath_renameat2, F_OK) == -1) {
        ret = -1;
        VALIDATE_AND_GOTO_LABEL_ON_ERROR("glfs_renameat2 operation failed", ret,
                                         out);
    }

    ret = glfs_unlink(fs, filepath_symlinkat);
    VALIDATE_AND_GOTO_LABEL_ON_ERROR("glfs_unlink", ret, out);

    ret = glfs_symlinkat(filename_renameat2, fd1, filename_symlinkat);
    VALIDATE_AND_GOTO_LABEL_ON_ERROR("glfs_symlinkat", ret, out);

    ret = glfs_lstat(fs, filepath_symlinkat, &stbuf);
    VALIDATE_AND_GOTO_LABEL_ON_ERROR("glfs_lstat", ret, out);

    if (!S_ISLNK(stbuf.st_mode)) {
        ret = -1;
        VALIDATE_AND_GOTO_LABEL_ON_ERROR("glfs_symlinkat operation failed", ret,
                                         out);
    }

    ret = glfs_mkdir(fs, dirpath_mkdir2_unlinkat, 0755);
    fprintf(stderr, "mkdir(%s): %s\n", dirpath_mkdir2_unlinkat,
            strerror(errno));
    if (ret) {
        ret = -1;
        VALIDATE_AND_GOTO_LABEL_ON_ERROR("glfs_mkdir", ret, out);
    }

    ret = glfs_unlinkat(fd1, dirname_mkdir2_unlinkat, AT_REMOVEDIR);
    VALIDATE_AND_GOTO_LABEL_ON_ERROR("glfs_unlinkat", ret, out);

    ret = glfs_unlinkat(fd1, filename_unlinkat, flags);
    VALIDATE_AND_GOTO_LABEL_ON_ERROR("glfs_unlinkat", ret, out);

    if (glfs_access(fs, filepath_unlinkat, F_OK) == 0) {
        VALIDATE_AND_GOTO_LABEL_ON_ERROR("glfs_unlinkat operation failed", ret,
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
    if (fd4a != NULL)
        glfs_close(fd4a);
    if (fd4b != NULL)
        glfs_close(fd4b);
    if (fd5 != NULL)
        glfs_close(fd5);
    if (fs) {
        (void)glfs_fini(fs);
    }

    return ret;
}
