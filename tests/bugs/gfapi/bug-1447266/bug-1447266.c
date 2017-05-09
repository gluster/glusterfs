#include <glusterfs/api/glfs.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <stdlib.h>
#define TOTAL_ARGS 4
int main(int argc, char *argv[])
{
        char *cwd = (char *)malloc(PATH_MAX*sizeof(char *));
        char *resolved = NULL;
        char *result = NULL;
        char *buf = NULL;
        struct stat st;
        char *path = NULL;
        int ret;

        if (argc != TOTAL_ARGS) {
                printf ("Please give all required command line args.\n"
                         "Format : <volname> <server_ip> <path_name>\n");
                goto out;
        }

        glfs_t *fs = glfs_new (argv[1]);

        if (fs == NULL) {
                printf ("glfs_new: %s\n", strerror(errno));
                /* No need to fail the test for this error */
                ret = 0;
                goto out;
        }

        ret = glfs_set_volfile_server(fs, "tcp", argv[2], 24007);
        if (ret) {
                printf ("glfs_set_volfile_server: %s\n", strerror(errno));
                /* No need to fail the test for this error */
                ret = 0;
                goto out;
        }

        path = argv[3];

        ret = glfs_set_logging(fs, "/tmp/gfapi.log", 7);
        if (ret) {
                printf ("glfs_set_logging: %s\n", strerror(errno));
                /* No need to fail the test for this error */
                ret = 0;
                goto out;
        }

        ret = glfs_init(fs);
        if (ret) {
                printf ("glfs_init: %s\n", strerror(errno));
                /* No need to fail the test for this error */
                ret = 0;
                goto out;
        }

        sleep(1);

        ret = glfs_chdir(fs, path);
        if (ret) {
                printf ("glfs_chdir: %s\n", strerror(errno));
                goto out;
        }

        buf = glfs_getcwd(fs, cwd, PATH_MAX);
        if (cwd == NULL) {
                printf ("glfs_getcwd: %s\n", strerror(errno));
                goto out;
        }

        printf ("\ncwd = %s\n\n", cwd);

        result = glfs_realpath(fs, path, resolved);
        if (result == NULL) {
                printf ("glfs_realpath: %s\n", strerror(errno));
                goto out;
        }

        ret = glfs_stat(fs, path, &st);
        if (ret) {
                printf ("glfs_stat: %s\n", strerror(errno));
                goto out;
        }
        if (cwd)
                free(cwd);

        result = glfs_realpath(fs, path, resolved);
        if (result == NULL) {
                printf ("glfs_realpath: %s\n", strerror(errno));
                goto out;
        }

        ret = glfs_fini(fs);
        if (ret) {
                printf ("glfs_fini: %s\n", strerror(errno));
                /* No need to fail the test for this error */
                ret = 0;
                goto out;
        }

        printf ("\n");
out:
        return ret;
}
