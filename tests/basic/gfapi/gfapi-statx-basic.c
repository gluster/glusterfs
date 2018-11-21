#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <stdbool.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <glusterfs/api/glfs.h>

#define VALIDATE_AND_GOTO_LABEL_ON_ERROR(func, ret, label)                     \
    do {                                                                       \
        if (ret < 0) {                                                         \
            fprintf(stderr, "%s : returned error %d (%s)\n", func, ret,        \
                    strerror(errno));                                          \
            goto label;                                                        \
        }                                                                      \
    } while (0)

#define GOTO_LABEL_ON_FALSE(compstr, ret, label)                               \
    do {                                                                       \
        if (ret == false) {                                                    \
            fprintf(stderr, "%s : comparison failed!\n", compstr);             \
            goto label;                                                        \
        }                                                                      \
    } while (0)

#define WRITE_SIZE 513
#define TRUNC_SIZE 4096

/* Using private function and hence providing a forward declation in sync with
code in glfs-internal.h */
int
glfs_statx(struct glfs *fs, const char *path, unsigned int mask,
           struct glfs_stat *statxbuf);

int
main(int argc, char *argv[])
{
    int ret = -1;
    int flags = O_RDWR | O_SYNC;
    glfs_t *fs = NULL;
    glfs_fd_t *fd1 = NULL;
    char *volname = NULL;
    char *logfile = NULL;
    const char *filename = "file_tmp";
    const char buff[WRITE_SIZE];
    struct stat sb;
    unsigned int mask;
    struct glfs_stat statx;
    bool bret;

    if (argc != 3) {
        fprintf(stderr, "Invalid argument\n");
        fprintf(stderr, "Usage: %s <volname> <logfile>\n", argv[0]);
        return 1;
    }

    volname = argv[1];
    logfile = argv[2];

    fs = glfs_new(volname);
    if (!fs)
        VALIDATE_AND_GOTO_LABEL_ON_ERROR("glfs_new", ret, out);

    ret = glfs_set_volfile_server(fs, "tcp", "localhost", 24007);
    VALIDATE_AND_GOTO_LABEL_ON_ERROR("glfs_set_volfile_server", ret, out);

    ret = glfs_set_logging(fs, logfile, 7);
    VALIDATE_AND_GOTO_LABEL_ON_ERROR("glfs_set_logging", ret, out);

    ret = glfs_init(fs);
    VALIDATE_AND_GOTO_LABEL_ON_ERROR("glfs_init", ret, out);

    fd1 = glfs_creat(fs, filename, flags, 0644);
    if (fd1 == NULL) {
        ret = -1;
        VALIDATE_AND_GOTO_LABEL_ON_ERROR("glfs_creat", ret, out);
    }

    ret = glfs_truncate(fs, filename, TRUNC_SIZE);
    VALIDATE_AND_GOTO_LABEL_ON_ERROR("glfs_truncate", ret, out);

    ret = glfs_write(fd1, buff, WRITE_SIZE, flags);
    VALIDATE_AND_GOTO_LABEL_ON_ERROR("glfs_write", ret, out);

    ret = glfs_fstat(fd1, &sb);
    VALIDATE_AND_GOTO_LABEL_ON_ERROR("glfs_fstat", ret, out);

    if (sb.st_size != TRUNC_SIZE) {
        fprintf(stderr, "wrong size %jd should be %jd\n", (intmax_t)sb.st_size,
                (intmax_t)2048);
        ret = -1;
        goto out;
    }

    glfs_close(fd1);
    fd1 = NULL;

    /* TEST 1: Invalid mask to statx */
    mask = 0xfafadbdb;
    ret = glfs_statx(fs, filename, mask, NULL);
    if (ret == 0 || ((ret == -1) && (errno != EINVAL))) {
        fprintf(stderr,
                "Invalid args passed, but error returned is"
                " incorrect (ret - %d, errno - %d)\n",
                ret, errno);
        ret = -1;
        goto out;
    }
    ret = 0;

    /* TEST 2: Call statx and validate fields against prior fstat data */
    /* NOTE: This fails, as iatt->ia_flags are not carried through the stack,
     * for example if mdc_to_iatt is invoked to serve cached stat, we will loose
     * the flags. */
    mask = GLFS_STAT_ALL;
    ret = glfs_statx(fs, filename, mask, &statx);
    VALIDATE_AND_GOTO_LABEL_ON_ERROR("glfs_statx", ret, out);

    if ((statx.glfs_st_mask & GLFS_STAT_BASIC_STATS) != GLFS_STAT_BASIC_STATS) {
        fprintf(stderr, "Invalid glfs_st_mask, expecting 0x%x got 0x%x\n",
                GLFS_STAT_ALL, statx.glfs_st_mask);
        ret = -1;
        goto out;
    }

    bret = (sb.st_ino == statx.glfs_st_ino);
    GOTO_LABEL_ON_FALSE("(sb.st_ino == statx.glfs_st_ino)", bret, out);

    bret = (sb.st_mode == statx.glfs_st_mode);
    GOTO_LABEL_ON_FALSE("(sb.st_mode == statx.glfs_st_mode)", bret, out);

    bret = (sb.st_nlink == statx.glfs_st_nlink);
    GOTO_LABEL_ON_FALSE("(sb.st_nlink == statx.glfs_st_nlink)", bret, out);

    bret = (sb.st_uid == statx.glfs_st_uid);
    GOTO_LABEL_ON_FALSE("(sb.st_uid == statx.glfs_st_uid)", bret, out);

    bret = (sb.st_gid == statx.glfs_st_gid);
    GOTO_LABEL_ON_FALSE("(sb.st_gid == statx.glfs_st_gid)", bret, out);

    bret = (sb.st_size == statx.glfs_st_size);
    GOTO_LABEL_ON_FALSE("(sb.st_size == statx.glfs_st_size)", bret, out);

    bret = (sb.st_blksize == statx.glfs_st_blksize);
    GOTO_LABEL_ON_FALSE("(sb.st_blksize == statx.glfs_st_blksize)", bret, out);

    bret = (sb.st_blocks == statx.glfs_st_blocks);
    GOTO_LABEL_ON_FALSE("(sb.st_blocks == statx.glfs_st_blocks)", bret, out);

    bret = (!memcmp(&sb.st_atim, &statx.glfs_st_atime,
                    sizeof(struct timespec)));
    GOTO_LABEL_ON_FALSE("(sb.st_atim == statx.glfs_st_atime)", bret, out);

    bret = (!memcmp(&sb.st_mtim, &statx.glfs_st_mtime,
                    sizeof(struct timespec)));
    GOTO_LABEL_ON_FALSE("(sb.st_mtim == statx.glfs_st_mtime)", bret, out);

    bret = (!memcmp(&sb.st_ctim, &statx.glfs_st_ctime,
                    sizeof(struct timespec)));
    GOTO_LABEL_ON_FALSE("(sb.st_ctim == statx.glfs_st_ctime)", bret, out);

    /* TEST 3: Check if partial masks are accepted */
    mask = GLFS_STAT_TYPE | GLFS_STAT_UID | GLFS_STAT_GID;
    ret = glfs_statx(fs, filename, mask, &statx);
    VALIDATE_AND_GOTO_LABEL_ON_ERROR("glfs_statx", ret, out);

    /* We currently still return all stats, as is acceptable based on the API
     * definition in the header (and in statx as well) */
    if ((statx.glfs_st_mask & GLFS_STAT_BASIC_STATS) != GLFS_STAT_BASIC_STATS) {
        fprintf(stderr, "Invalid glfs_st_mask, expecting 0x%x got 0x%x\n",
                GLFS_STAT_ALL, statx.glfs_st_mask);
        ret = -1;
        goto out;
    }
out:
    if (fd1 != NULL)
        glfs_close(fd1);
    if (fs) {
        (void)glfs_fini(fs);
    }

    return ret;
}
