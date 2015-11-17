#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <assert.h>

int
main (int argc, char *argv[])
{
        int          fd                = -1, ret = -1, len = 0;
        char        *path              = NULL, buf[128] = {0, }, *cmd = NULL;
        struct stat  stbuf             = {0, };
        int          write_to_child[2] = {0, }, write_to_parent[2] = {0, };

        path = argv[1];
        cmd = argv[2];

        assert (argc == 3);

        ret = pipe (write_to_child);
        if (ret < 0) {
                fprintf (stderr, "creation of write-to-child pipe failed "
                         "(%s)\n", strerror (errno));
                goto out;
        }

        ret = pipe (write_to_parent);
        if (ret < 0) {
                fprintf (stderr, "creation of write-to-parent pipe failed "
                         "(%s)\n", strerror (errno));
                goto out;
        }

        ret = fork ();
        switch (ret) {
        case 0:
                close (write_to_child[1]);
                close (write_to_parent[0]);

                /* child, wait for instructions to execute command */
                ret = read (write_to_child[0], buf, 128);
                if (ret < 0) {
                        fprintf (stderr, "child: read on pipe failed (%s)\n",
                                 strerror (errno));
                        goto out;
                }

                system (cmd);

                ret = write (write_to_parent[1], "1", 2);
                if (ret < 0) {
                        fprintf (stderr, "child: write to pipe failed (%s)\n",
                                 strerror (errno));
                        goto out;
                }
                break;

        case -1:
                fprintf (stderr, "fork failed (%s)\n", strerror (errno));
                goto out;

        default:
                close (write_to_parent[1]);
                close (write_to_child[0]);

                fd = open (path, O_CREAT | O_RDWR | O_APPEND, S_IRWXU);
                if (fd < 0) {
                        fprintf (stderr, "open failed (%s)\n",
                                 strerror (errno));
                        goto out;
                }

                len = strlen ("test-content") + 1;
                ret = write (fd, "test-content", len);

                if (ret < len) {
                        fprintf (stderr, "write failed %d (%s)\n", ret,
                                 strerror (errno));
                }

                ret = pread (fd, buf, 128, 0);
                if ((ret == len) && (strcmp (buf, "test-content") == 0)) {
                        fprintf (stderr, "read should've failed as previous "
                                 "write would've failed with EDQUOT, but its "
                                 "successful");
                        ret = -1;
                        goto out;
                }

                ret = write (write_to_child[1], "1", 2);
                if (ret < 0) {
                        fprintf (stderr, "parent: write to pipe failed (%s)\n",
                                 strerror (errno));
                        goto out;
                }

                ret = read (write_to_parent[0], buf, 128);
                if (ret < 0) {
                        fprintf (stderr, "parent: read from pipe failed (%s)\n",
                                 strerror (errno));
                        goto out;
                }

                /* this will force a sync on cached-write and now that quota
                   limit is increased, sync will be successful. ignore return
                   value as fstat would fail with EDQUOT (picked up from
                   cached-write because of previous sync failure.
                */
                fstat (fd, &stbuf);

                ret = pread (fd, buf, 128, 0);
                if (ret != len) {
                        fprintf (stderr, "post cmd read failed %d (data:%s) "
                                 "(error:%s)\n", ret, buf, strerror (errno));
                        goto out;
                }

                if (strcmp (buf, "test-content")) {
                        fprintf (stderr, "wrong data (%s)\n", buf);
                        goto out;
                }
        }

        ret = 0;

out:
        return ret;
}
