#include <glusterfs/api/glfs.h>
#include <glusterfs/api/glfs-handles.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <sys/stat.h>

/* Few rules:
 * 1. A client may have multiple lease keys, but a lease key cannot be shared by multiple clients.
 * 2. Lease key can be set before open, or in glfs_lease request. A lease key set like this is
 *    valid for the lifetime of the fd, i.e. a fd cannot have multiple lease key. But a lease key
 *    can be shared across multiple fds.
 */
glfs_t *client1 = NULL, *client2 = NULL, *client3 = NULL, *client4 = NULL;
glfs_fd_t *fd1 = NULL, *fd2 = NULL, *fd3 = NULL, *fd4 = NULL;
FILE *log_file = NULL;
char lid1[GLFS_LEASE_ID_SIZE] = "lid1-clnt1", lid2[GLFS_LEASE_ID_SIZE] = "lid2-clnt2";
char lid3[GLFS_LEASE_ID_SIZE] = "lid3-clnt2", lid4[GLFS_LEASE_ID_SIZE] = {0,};
char *volname = NULL, *glfs_log_file = NULL;

#define MAX_CLIENTS 4
#define MAX_FDS 4
#define TEST_FILE "/test/lease"
#define SHUD_PASS 0
#define SHUD_FAIL -1
#define NONE 0

static void
recall_cbk (struct glfs_lease lease, void *data);

static int
set_read_lease (glfs_fd_t *fd, char ld[])
{
        struct glfs_lease lease = {0, };
        int ret = 0;

        memset (&lease, 0, sizeof (lease));
        lease.cmd = GLFS_SET_LEASE;
        lease.lease_type = GLFS_RD_LEASE;
        memcpy(&lease.lease_id, ld, GLFS_LEASE_ID_SIZE);
        ret = glfs_lease (fd, &lease, &recall_cbk, fd);
        if (ret < 0) {
                fprintf (log_file, "\n    RD_LEASE failed with ret: %d (%s)",
                                ret, strerror (errno));
                return -1;
        }
        fprintf (log_file, "\n    Took RD_LEASE");
        return ret;
}

static int
set_write_lease (glfs_fd_t *fd, char ld[])
{
        struct glfs_lease lease = {0, };
        int ret = 0;

        memset (&lease, 0, sizeof (lease));
        lease.cmd = GLFS_SET_LEASE;
        lease.lease_type = GLFS_RW_LEASE;
        memcpy(&lease.lease_id, ld, GLFS_LEASE_ID_SIZE);
        ret = glfs_lease (fd, &lease, &recall_cbk, NULL);
        if (ret < 0) {
                fprintf (log_file, "\n    RW_LEASE failed with ret: %d (%s)",
                                ret, strerror (errno));
                return -1;
        }
        fprintf (log_file, "\n    Took RW_LEASE");
        return ret;
}

static int
get_lease (glfs_fd_t *fd, char ld[])
{
        struct glfs_lease lease = {0, };
        int ret = 0;

        memset (&lease, 0, sizeof (lease));
        lease.cmd = GLFS_GET_LEASE;
        lease.lease_type = -1;
        memcpy(&lease.lease_id, ld, GLFS_LEASE_ID_SIZE);
        ret = glfs_lease (fd, &lease, &recall_cbk, NULL);
        if (ret < 0) {
                fprintf (log_file, "\n    GET_LEASE failed with ret: %d (%s)",
                                ret, strerror (errno));
                return -1;
        }
        if (lease.lease_type == GLFS_RD_LEASE)
                fprintf (log_file, "\n    Esisting Lease: RD_LEASE");
        else if (lease.lease_type == GLFS_RW_LEASE)
                fprintf (log_file, "\n    Esisting Lease: RW_LEASE");
        else if (lease.lease_type == 3)
                fprintf (log_file, "\n    Esisting Lease: RD_LEASE|RW_LEASE");
        else if (lease.lease_type == 0)
                fprintf (log_file, "\n    Esisting Lease: NONE");
        else
                fprintf (log_file, "\n    Existing lease type:%d", lease.lease_type);
        return lease.lease_type;
}

static int
unlk_write_lease (glfs_fd_t *fd, char ld[])
{
        struct glfs_lease lease = {0, };
        int ret = 0;

        memset (&lease, 0, sizeof (lease));
        lease.cmd = GLFS_UNLK_LEASE;
        lease.lease_type = GLFS_RW_LEASE;
        memcpy(&lease.lease_id, ld, GLFS_LEASE_ID_SIZE);
        ret = glfs_lease (fd, &lease, &recall_cbk, NULL);
        if (ret < 0) {
                fprintf (log_file, "\n    Unlock RW_LESAE failed with ret: %d (%s)",
                                ret, strerror (errno));
                return -1;
        }
        fprintf (log_file, "\n    Unlocked RW_LEASE");
        return ret;
}

static int
unlk_read_lease (glfs_fd_t *fd, char ld[])
{
        struct glfs_lease lease = {0, };
        int ret = 0;

        memset (&lease, 0, sizeof (lease));
        lease.cmd = GLFS_UNLK_LEASE;
        lease.lease_type = GLFS_RD_LEASE;
        memcpy(&lease.lease_id, ld, GLFS_LEASE_ID_SIZE);

        ret = glfs_lease (fd, &lease, &recall_cbk, NULL);
        if (ret < 0) {
                fprintf (log_file, "\n    Unlock RD_LEASE failed with ret: %d (%s)",
                                ret, strerror (errno));
                return -1;
        }
        fprintf (log_file, "\n    Unlocked RD_LEASE");
        return ret;
}

glfs_t *
setup_new_client(char *volname, char *log_fileile)
{
        int ret = 0;
        glfs_t *fs = NULL;

        fs = glfs_new (volname);
        if (!fs) {
                fprintf (log_file, "\nglfs_new: returned NULL (%s)\n",
                                 strerror (errno));
                goto error;
        }

        ret = glfs_set_volfile_server (fs, "tcp", "localhost", 24007);
        if (ret < 0) {
                fprintf (log_file, "\nglfs_set_volfile_server failed ret:%d (%s)\n",
                                 ret, strerror (errno));
                goto error;
        }

        ret = glfs_set_logging (fs, log_fileile, 7);
        if (ret < 0) {
                fprintf (log_file, "\nglfs_set_logging failed with ret: %d (%s)\n",
                                 ret, strerror (errno));
                goto error;
        }

        ret = glfs_init (fs);
        if (ret < 0) {
                fprintf (log_file, "\nglfs_init failed with ret: %d (%s)\n",
                                  ret, strerror (errno));
                goto error;
        }
        return fs;
error:
        return NULL;
}

#define OPEN(client, flags, fd, lease_id)                                                   \
do {                                                                                        \
        int ret_val = 0;                                                                    \
        ret_val = glfs_setfsleaseid (lease_id);                                          \
        if (ret_val) {                                                                      \
                fprintf (log_file, "\nglfs_setfsleaseid failed with ret: %d (%s)\n",        \
                                ret, strerror (errno));                                     \
                return -1;                                                                  \
        }                                                                                   \
        fd = glfs_open (client, TEST_FILE, flags);                                          \
        if (fd == NULL) {                                                                   \
                fprintf (log_file, "\nglfs_open failed with ret: %d (%s)\n",                \
                                ret, strerror (errno));                                     \
                return -1;                                                                  \
        }                                                                                   \
} while (0)                                                                                 \

#define VERIFY_RESULT(test_case, ret, value)                                                \
do {                                                                                        \
        if (ret != value) {                                                                 \
                fprintf (log_file, "\n    Testcase %d failed, ret = %d, value=%d\n", test_case, ret, value); \
                goto error; /*test unsuccesfull*/                                           \
        }                                                                                   \
        fprintf (log_file, "\n    Testcase %d Succeeded\n", test_case);                       \
} while (0)                                                                                 \

static void
recall_cbk (struct glfs_lease lease, void *data)
{
        int ret = -1;
        char ld[GLFS_LEASE_ID_SIZE] = "";

        fprintf (log_file, "\nRECALL recieved on lease_id:(%s)", lease.lease_id);
        memcpy (ld, lease.lease_id, GLFS_LEASE_ID_SIZE);
        ret = unlk_write_lease ((glfs_fd_t *)data, ld);
        VERIFY_RESULT (500, ret, SHUD_PASS);
error:
        return;
}


static int
testcase1_rd_lease ()
{
        glfs_fd_t *fd1      = NULL;
        int        ret      = 0;

	fprintf (log_file, "\n Basic test case for Read lease:");
        /* Open fd on client 1 in RD mode */
        OPEN (client1, O_RDONLY, fd1, lid1);
        ret = set_write_lease (fd1, lid1);
        VERIFY_RESULT (1, ret, SHUD_FAIL);

        ret = set_read_lease (fd1, lid1);
        VERIFY_RESULT (2, ret, SHUD_PASS);

        ret = get_lease (fd1, lid1);
        VERIFY_RESULT (3, ret, GLFS_RD_LEASE);

        ret = unlk_write_lease (fd1, lid1);
        VERIFY_RESULT (4, ret, SHUD_FAIL);

        ret = unlk_read_lease (fd1, lid1);
        VERIFY_RESULT (5, ret, SHUD_PASS);

        ret = get_lease (fd1, lid1);
        VERIFY_RESULT (6, ret, NONE);

        ret = unlk_read_lease (fd1, lid1);
        VERIFY_RESULT (7, ret, SHUD_PASS);

        ret = glfs_close (fd1);
        VERIFY_RESULT (8, ret, SHUD_PASS);

        return 0;
error:
        return -1;
}

static int
testcase2_wr_lease ()
{
        glfs_fd_t *fd1      = NULL;
        int        ret      = 0;

        fprintf (log_file, "\n Basic test case for Write lease:");
        /* Open fd on client 1 in WRonly mode */
        OPEN (client1, O_WRONLY, fd1, lid1);
        ret = set_read_lease (fd1, lid1);
        VERIFY_RESULT (1, ret, SHUD_FAIL);

        ret = unlk_write_lease (fd1, lid1);
        VERIFY_RESULT (2, ret, SHUD_PASS);

        ret = set_write_lease (fd1, lid1);
        VERIFY_RESULT (3, ret, SHUD_PASS);

        ret = get_lease (fd1, lid1);
        VERIFY_RESULT (4, ret, GLFS_RW_LEASE);

        ret = unlk_write_lease (fd1, lid1);
        VERIFY_RESULT (5, ret, SHUD_PASS);

        ret = get_lease (fd1, lid1);
        VERIFY_RESULT (6, ret, NONE);

        ret = unlk_read_lease (fd1, lid1);
        VERIFY_RESULT (7, ret, SHUD_FAIL);

        ret = glfs_close (fd1);
        VERIFY_RESULT (8, ret, SHUD_PASS);

        return 0;
error:
        return -1;
}

static int
testcase3_rd_wr_lease ()
{
        glfs_fd_t *fd1      = NULL;
        int        ret      = 0;

        fprintf (log_file, "\n Basic test case for Read Write lease:");
        /* Open fd on client 1 in WRonly mode */
        OPEN (client1, O_RDWR, fd1, lid1);
        ret = set_read_lease (fd1, lid1);
        VERIFY_RESULT (1, ret, SHUD_PASS);

        ret = set_write_lease (fd1, lid1);
        VERIFY_RESULT (2, ret, SHUD_PASS);

        ret = get_lease (fd1, lid1);
        VERIFY_RESULT (3, ret, (GLFS_RW_LEASE | GLFS_RD_LEASE));

        ret = unlk_write_lease (fd1, lid1);
        VERIFY_RESULT (4, ret, SHUD_PASS);

        ret = get_lease (fd1, lid1);
        VERIFY_RESULT (5, ret, GLFS_RD_LEASE);

        ret = unlk_read_lease (fd1, lid1);
        VERIFY_RESULT (6, ret, SHUD_PASS);

        ret = get_lease (fd1, lid1);
        VERIFY_RESULT (7, ret, NONE);

        ret = glfs_close (fd1);
        VERIFY_RESULT (8, ret, SHUD_PASS);

        return 0;
error:
        return -1;
}

static int
testcase4_rd_lease_multi_clnt ()
{
        glfs_fd_t *fd1      = NULL;
        glfs_fd_t *fd2      = NULL;
        int        ret      = 0;

        fprintf (log_file, "\n Basic test case for multi client Read lease:");

        /* Open fd on client 1 in RD mode */
        OPEN (client1, O_RDONLY, fd1, lid1);

        /* Open fd on client 2 in RW mode */
        OPEN (client2, O_RDONLY, fd2, lid2);

        ret = set_read_lease (fd1, lid1);
        VERIFY_RESULT (1, ret, SHUD_PASS);

        ret = set_read_lease (fd2, lid2);
        VERIFY_RESULT (2, ret, SHUD_PASS);

        ret = get_lease (fd1, lid1);
        VERIFY_RESULT (3, ret, GLFS_RD_LEASE);

        ret = unlk_read_lease (fd1, lid1);
        VERIFY_RESULT (4, ret, SHUD_PASS);

        ret = unlk_read_lease (fd2, lid2);
        VERIFY_RESULT (5, ret, SHUD_PASS);

        ret = get_lease (fd1, lid1);
        VERIFY_RESULT (6, ret, NONE);

        ret = get_lease (fd2, lid2);
        VERIFY_RESULT (7, ret, NONE);

        ret = glfs_close (fd1);
        VERIFY_RESULT (8, ret, SHUD_PASS);

        ret = glfs_close (fd2);
        VERIFY_RESULT (9, ret, SHUD_PASS);

        return 0;
error:
        return -1;
}

static int
testcase5_openfd_multi_lid ()
{
        glfs_fd_t *fd1      = NULL;
        glfs_fd_t *fd2      = NULL;
        glfs_fd_t *fd3      = NULL;
        int        ret      = 0;

        fprintf (log_file, "\n Basic test case for multi lid openfd check:");

        /* Open fd on client 1 in RD mode */
        OPEN (client1, O_RDONLY, fd1, lid1);

        /* Open fd on client 2 in RW mode */
        OPEN (client2, O_RDWR, fd2, lid2);
        OPEN (client2, O_RDWR, fd3, lid2);

        ret = set_read_lease (fd1, lid1);
        VERIFY_RESULT (1, ret, SHUD_FAIL); /*As there are other openfds in WR mode from diff lid*/

        ret = set_write_lease (fd2, lid2);
        VERIFY_RESULT (2, ret, SHUD_FAIL); /*As thers is another fd in RD mode from diff lid */

        ret = glfs_close (fd1);
        VERIFY_RESULT (3, ret, SHUD_PASS);

        ret = set_write_lease (fd2, lid2);
        VERIFY_RESULT (4, ret, SHUD_PASS);

        ret = unlk_write_lease (fd2, lid2);
        VERIFY_RESULT (5, ret, SHUD_PASS);

        ret = glfs_close (fd2);
        VERIFY_RESULT (6, ret, SHUD_PASS);

        ret = glfs_close (fd3);
        VERIFY_RESULT (7, ret, SHUD_PASS);

        return 0;
error:
        return -1;
}

static int
testcase6_openfd_same_lid ()
{
        glfs_fd_t *fd1      = NULL;
        glfs_fd_t *fd2      = NULL;
        glfs_fd_t *fd3      = NULL;
        int        ret      = 0;

        fprintf (log_file, "\n Basic test case for same lid openfd check:");

        /* Open fd on client 2 in RW mode */
        OPEN (client1, O_RDWR, fd1, lid2);
        OPEN (client1, O_RDWR, fd2, lid2);

        ret = set_write_lease (fd1, lid2);
        VERIFY_RESULT (4, ret, SHUD_PASS);

        ret = set_write_lease (fd2, lid2);
        VERIFY_RESULT (4, ret, SHUD_PASS);

        ret = set_read_lease (fd2, lid2);
        VERIFY_RESULT (4, ret, SHUD_PASS);

        ret = unlk_write_lease (fd1, lid2);
        VERIFY_RESULT (5, ret, SHUD_PASS);

        ret = unlk_read_lease (fd2, lid2);
        VERIFY_RESULT (5, ret, SHUD_PASS);

        ret = unlk_write_lease (fd2, lid2);
        VERIFY_RESULT (5, ret, SHUD_PASS);

        ret = glfs_close (fd1);
        VERIFY_RESULT (6, ret, SHUD_PASS);

        ret = glfs_close (fd2);
        VERIFY_RESULT (7, ret, SHUD_PASS);

        return 0;
error:
        return -1;
}

static int
testcase7_rd_multi_lid ()
{
        glfs_fd_t *fd1      = NULL;
        glfs_fd_t *fd2      = NULL;
        int        ret      = 0;

        fprintf (log_file, "\n Basic test case for multi lease id Read lease:");

        /* Open fd on client 1 in RD mode */
        OPEN (client2, O_RDONLY, fd1, lid2);

        /* Open fd on client 2 in RD mode */
        OPEN (client2, O_RDONLY, fd2, lid3);

        ret = set_read_lease (fd1, lid2);
        VERIFY_RESULT (1, ret, SHUD_PASS);

        ret = set_read_lease (fd2, lid3);
        VERIFY_RESULT (2, ret, SHUD_PASS);

        ret = get_lease (fd1, lid2);
        VERIFY_RESULT (3, ret, GLFS_RD_LEASE);

        ret = unlk_read_lease (fd1, lid2);
        VERIFY_RESULT (4, ret, SHUD_PASS);

        ret = unlk_read_lease (fd2, lid3);
        VERIFY_RESULT (5, ret, SHUD_PASS);

        ret = get_lease (fd1, lid2);
        VERIFY_RESULT (6, ret, NONE);

        ret = get_lease (fd2, lid3);
        VERIFY_RESULT (7, ret, NONE);

        ret = glfs_close (fd1);
        VERIFY_RESULT (8, ret, SHUD_PASS);

        ret = glfs_close (fd2);
        VERIFY_RESULT (9, ret, SHUD_PASS);

        return 0;
error:
        return -1;
}

static int
testcase8_client_disconnect ()
{
        glfs_fd_t *fd1      = NULL;
        glfs_fd_t *fd2      = NULL;
        int        ret      = 0;

        fprintf (log_file, "\n Basic test case for client disconnect cleanup");

        /* Open fd on client 1 in RD mode */
        OPEN (client1, O_RDWR, fd1, lid1);

        ret = set_read_lease (fd1, lid1);
        VERIFY_RESULT (1, ret, SHUD_PASS);

        ret = get_lease (fd1, lid1);
        VERIFY_RESULT (2, ret, GLFS_RD_LEASE);

        ret = set_write_lease (fd1, lid1);
        VERIFY_RESULT (3, ret, SHUD_PASS);

        ret = get_lease (fd1, lid1);
        VERIFY_RESULT (4, ret, (GLFS_RD_LEASE | GLFS_RW_LEASE));

        ret = glfs_fini (client1);
        VERIFY_RESULT (5, ret, SHUD_PASS);

        /* Open fd on client 2 in RD mode */
        OPEN (client2, O_RDONLY, fd2, lid3);

        ret = get_lease (fd2, lid3);
        VERIFY_RESULT (6, ret, NONE);

        ret = glfs_close (fd2);
        VERIFY_RESULT (7, ret, SHUD_PASS);

        client1 = setup_new_client (volname, glfs_log_file);

        return 0;
error:
        return -1;
}

static int
testcase9_recall_conflict_lease ()
{
        struct glfs_object *obj = NULL;
        glfs_fd_t *fd1      = NULL;
        int        ret      = 0;
        struct glfs_lease lease = {0, };

        fprintf (log_file, "\n Basic test case for conflicting lease causing recall");

        memset (&lease, 0, sizeof (lease));
        lease.cmd = GLFS_SET_LEASE;
        lease.lease_type = GLFS_RD_LEASE;
        memcpy(&lease.lease_id, lid2, GLFS_LEASE_ID_SIZE);
        /* Open fd on client 1 in RD mode */
        OPEN (client1, O_RDWR, fd1, lid1);
        ret = set_write_lease (fd1, lid1);
        VERIFY_RESULT (1, ret, SHUD_PASS);

        obj = glfs_h_lookupat (client2, NULL, TEST_FILE, NULL, 0);
        ret = glfs_h_lease (client2, obj, &lease);
        VERIFY_RESULT (2, ret, SHUD_FAIL);

        ret = unlk_write_lease (fd1, lid1);
        VERIFY_RESULT (5, ret, SHUD_PASS);

        sleep (3);
        ret = glfs_h_close (obj);
        VERIFY_RESULT (3, ret, SHUD_PASS);
        ret = glfs_close (fd1);
        VERIFY_RESULT (4, ret, SHUD_PASS);

        return 0;
error:
        return -1;
}

static int
testcase10_recall_open_conflict ()
{
        glfs_fd_t *fd1      = NULL;
        glfs_fd_t *fd2      = NULL;
        int        ret      = 0;

        fprintf (log_file, "\n Basic test case for conflicting open causing recall");

        /* Open fd on client 1 in RW mode */
        OPEN (client1, O_RDWR, fd1, lid1);

        ret = set_write_lease (fd1, lid1);
        VERIFY_RESULT (1, ret, SHUD_PASS);

        /* Open fd on client 1 in RW mode */
        OPEN (client2, O_RDWR, fd2, lid2);

        /* TODO: Check for recall cbk functionality */
        ret = glfs_close (fd1);
        VERIFY_RESULT (2, ret, SHUD_PASS);

        ret = glfs_close (fd2);
        VERIFY_RESULT (3, ret, SHUD_PASS);

        return 0;
error:
        return -1;
}

int
main (int argc, char *argv[])
{
        int        ret      = 0;
        int        i        = 0;
        glfs_fd_t *fd       = NULL;
        glfs_fd_t *fd1      = NULL;
        char      *topdir   = "topdir", *filename = "file1";
        char      *buf      = NULL;
        int        x        = 0;
        ssize_t    xattr_size = -1;

        if (argc != 4) {
                fprintf (stderr,
                                "Expect following args %s <Vol> <glfs client log file> <testcase log file>\n"
                                , argv[0]);
                return -1;
        }

        log_file = fopen (argv[3], "w");
	if (!log_file)
		goto error;

        volname = argv[1];
        glfs_log_file = argv[2];

        /* Setup 3 clients */
        client1 = setup_new_client (volname, glfs_log_file);
        client2 = setup_new_client (volname, glfs_log_file);
        client3 = setup_new_client (volname, glfs_log_file);

        ret = testcase1_rd_lease ();
        VERIFY_RESULT (101, ret, SHUD_PASS);

        ret = testcase2_wr_lease ();
        VERIFY_RESULT (102, ret, SHUD_PASS);

        ret = testcase3_rd_wr_lease ();
        VERIFY_RESULT (103, ret, SHUD_PASS);

        ret = testcase4_rd_lease_multi_clnt ();
        VERIFY_RESULT (104, ret, SHUD_PASS);

        ret = testcase5_openfd_multi_lid ();
        VERIFY_RESULT (105, ret, SHUD_PASS);

        ret = testcase6_openfd_same_lid ();
        VERIFY_RESULT (106, ret, SHUD_PASS);

        ret = testcase7_rd_multi_lid ();
        VERIFY_RESULT (107, ret, SHUD_PASS);

	ret = testcase8_client_disconnect ();
        VERIFY_RESULT (108, ret, SHUD_PASS);

        ret = testcase9_recall_conflict_lease ();
        VERIFY_RESULT (109, ret, SHUD_PASS);

        ret = testcase10_recall_open_conflict ();
        VERIFY_RESULT (110, ret, SHUD_PASS);

        glfs_fini (client1);
        glfs_fini (client2);
        glfs_fini (client3);

	fclose (log_file);
        return 0;
error:
        return -1;
}
