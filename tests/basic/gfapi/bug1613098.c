#include <inttypes.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>

#include <glusterfs/api/glfs.h>
#include <glusterfs/api/glfs-handles.h>

#define ACL_TYPE_ACCESS (0x8000)

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
    glfs_fd_t *fd = NULL;
    char *volname = NULL;
    char *logfile = NULL;
    const char *filename = "file_tmp";
    struct glfs_object *object = NULL;
    acl_t acl = NULL;
    struct stat sb;

    if (argc != 3) {
        fprintf(stderr, "Invalid argument\n");
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

    fd = glfs_creat(fs, filename, flags, 0044);
    if (fd == NULL) {
        ret = -1;
        VALIDATE_AND_GOTO_LABEL_ON_ERROR("glfs_creat", ret, out);
    }
    glfs_close(fd);

    object = glfs_h_lookupat(fs, NULL, filename, NULL, 0);
    if (object == NULL) {
        ret = -1;
        VALIDATE_AND_GOTO_LABEL_ON_ERROR("glfs_h_lookupat", ret, out);
    }

    ret = glfs_chown(fs, filename, 99, 99);
    VALIDATE_AND_GOTO_LABEL_ON_ERROR("glfs_chown", ret, out);

    ret = glfs_setfsuid(99);
    VALIDATE_AND_GOTO_LABEL_ON_ERROR("glfs_setfsuid", ret, out);

    ret = glfs_setfsgid(99);
    VALIDATE_AND_GOTO_LABEL_ON_ERROR("glfs_setfsgid", ret, out);

    acl = glfs_h_acl_get(fs, object, ACL_TYPE_ACCESS);
    if (acl == NULL) {
        ret = -1;
        VALIDATE_AND_GOTO_LABEL_ON_ERROR("glfs_h_acl_get", ret, out);
    }

    ret = glfs_h_acl_set(fs, object, ACL_TYPE_ACCESS, acl);
    VALIDATE_AND_GOTO_LABEL_ON_ERROR("glfs_h_acl_get", ret, out);
out:
    glfs_setfsuid(0);
    glfs_setfsgid(0);

    if (object)
        glfs_h_close(object);

    if (fs)
        glfs_fini(fs);

    return ret;
}
