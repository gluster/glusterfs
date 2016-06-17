#define _GNU_SOURCE
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <glusterfs/api/glfs.h>
#include <glusterfs/api/glfs-handles.h>

enum fallocate_flag {
        TEST_FALLOCATE_NONE,
        TEST_FALLOCATE_KEEP_SIZE,
        TEST_FALLOCATE_ZERO_RANGE,
        TEST_FALLOCATE_PUNCH_HOLE,
        TEST_FALLOCATE_MAX,
};

int
get_fallocate_flag (int opcode)
{
        int ret = 0;

        switch (opcode) {
        case TEST_FALLOCATE_NONE:
                ret = 0;
                break;
        case TEST_FALLOCATE_KEEP_SIZE:
                ret = FALLOC_FL_KEEP_SIZE;
                break;
        case TEST_FALLOCATE_ZERO_RANGE:
                ret = FALLOC_FL_ZERO_RANGE;
                break;
        case TEST_FALLOCATE_PUNCH_HOLE:
                ret = FALLOC_FL_PUNCH_HOLE;
                break;
        default:
                ret = -1;
                break;
        }
        return ret;
}

int
main (int argc, char *argv[])
{
        int        ret = 1;
        int        opcode = -1;
        off_t      offset = 0;
        size_t     len = 0;
        glfs_t    *fs = NULL;
        glfs_fd_t *fd = NULL;

        if (argc != 8) {
                fprintf (stderr, "Syntax: %s <host> <volname> <opcode> <offset> <len> <file-path> <log-file>\n", argv[0]);
                return 1;
        }

        fs = glfs_new (argv[2]);
        if (!fs) {
                fprintf (stderr, "glfs_new: returned NULL\n");
                return 1;
        }

        ret = glfs_set_volfile_server (fs, "tcp", argv[1], 24007);
        if (ret != 0) {
                fprintf (stderr, "glfs_set_volfile_server: retuned %d\n", ret);
                goto out;
        }

        ret = glfs_set_logging (fs, argv[7], 7);
        if (ret != 0) {
                fprintf (stderr, "glfs_set_logging: returned %d\n", ret);
                goto out;
        }

        ret = glfs_init (fs);
        if (ret != 0) {
                fprintf (stderr, "glfs_init: returned %d\n", ret);
                goto out;
        }

        opcode = atoi (argv[3]);
        opcode = get_fallocate_flag (opcode);
        if (opcode < 0) {
                fprintf (stderr, "get_fallocate_flag: invalid flag \n");
                goto out;
        }

        offset = atoi (argv[4]);
        len    = atoi (argv[5]);

        fd = glfs_open (fs, argv[6], O_RDWR);
        if (fd == NULL) {
                fprintf (stderr, "glfs_open: returned NULL\n");
                goto out;
        }

        ret = glfs_fallocate (fd, opcode, offset, len);
        if (ret <= 0) {
                fprintf (stderr, "glfs_fallocate: returned %d\n", ret);
                goto out;
        }

        ret = 0;

out:
        if (fd)
                glfs_close(fd);
        glfs_fini (fs);
        return ret;
}
