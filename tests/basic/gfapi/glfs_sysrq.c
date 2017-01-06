/** glfs_sysrq.c
 *
 * Simple test application to run all glfs_syqrq() debugging calls.
 *
 * Usage: ./glfs_sysrq <host> <volume> <logfile>
 */
#include <errno.h>
#include <stdio.h>

#include <glusterfs/api/glfs.h>

int
main (int argc, char *argv[])
{
        /* cmdline arguments */
        char   *host    = NULL;
        char   *volume  = NULL;
        char   *logfile = NULL;

        /* other variables */
        glfs_t *fs      = NULL;
        int     ret     = 0;

        if (argc != 4) {
                fprintf (stderr, "Usage: %s <host> <volume> <logfile>\n",
                         argv[0]);
                return -1;
        }

        host = argv[1];
        volume = argv[2];
        logfile = argv[3];

        fs = glfs_new (volume);
        if (!fs) {
                return -1;
        }

        ret = glfs_set_logging (fs, logfile, 7);
        if (ret < 0) {
                return -1;
        }

        ret = glfs_set_volfile_server (fs, "tcp", host, 24007);
        if (ret < 0) {
                return -1;
        }

        ret = glfs_init (fs);
        if (ret < 0) {
                return -1;
        }

        /* checking of the results is easier in the script running this test */
        glfs_sysrq (fs, GLFS_SYSRQ_HELP);
        glfs_sysrq (fs, GLFS_SYSRQ_STATEDUMP);

        glfs_fini (fs);

        return 0;
}
