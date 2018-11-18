#include <glusterfs/api/glfs.h>
#include <glusterfs/api/glfs-handles.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <sys/stat.h>

/* Few rules:
 * 1. A client may have multiple lease keys, but a lease key cannot be shared by
 * multiple clients.
 * 2. Lease key can be set before open, or in glfs_lease request. A lease key
 * set like this is valid for the lifetime of the fd, i.e. a fd cannot have
 * multiple lease key. But a lease key can be shared across multiple fds.
 */
glfs_t *client1 = NULL, *client2 = NULL;
glfs_fd_t *fd1 = NULL;
FILE *log_file = NULL;
char lid1[GLFS_LEASE_ID_SIZE] = "lid1-clnt1",
     lid2[GLFS_LEASE_ID_SIZE] = "lid2-clnt2";
char lid3[GLFS_LEASE_ID_SIZE] = "lid3-clnt2", lid4[GLFS_LEASE_ID_SIZE] = {
                                                  0,
};
char *volname = NULL, *glfs_log_file = NULL;
int upcall_recv = 0;

#define MAX_CLIENTS 4
#define MAX_FDS 4
#define TEST_FILE "/test/lease"
#define SHUD_PASS 0
#define SHUD_FAIL -1
#define NONE 0

static void
recall_cbk(struct glfs_lease lease, void *data);

static int
set_read_lease(glfs_fd_t *fd, char ld[])
{
    struct glfs_lease lease = {
        0,
    };
    int ret = 0;

    memset(&lease, 0, sizeof(lease));
    lease.cmd = GLFS_SET_LEASE;
    lease.lease_type = GLFS_RD_LEASE;
    memcpy(&lease.lease_id, ld, GLFS_LEASE_ID_SIZE);
    ret = glfs_lease(fd, &lease, &recall_cbk, fd);
    if (ret < 0) {
        fprintf(log_file, "\n    RD_LEASE failed with ret: %d (%s)", ret,
                strerror(errno));
        return -1;
    }
    fprintf(log_file, "\n    Took RD_LEASE");
    return ret;
}

static int
set_write_lease(glfs_fd_t *fd, char ld[])
{
    struct glfs_lease lease = {
        0,
    };
    int ret = 0;

    memset(&lease, 0, sizeof(lease));
    lease.cmd = GLFS_SET_LEASE;
    lease.lease_type = GLFS_RW_LEASE;
    memcpy(&lease.lease_id, ld, GLFS_LEASE_ID_SIZE);
    ret = glfs_lease(fd, &lease, &recall_cbk, NULL);
    if (ret < 0) {
        fprintf(log_file, "\n    RW_LEASE failed with ret: %d (%s)", ret,
                strerror(errno));
        return -1;
    }
    fprintf(log_file, "\n    Took RW_LEASE");
    return ret;
}

static int
get_lease(glfs_fd_t *fd, char ld[])
{
    struct glfs_lease lease = {
        0,
    };
    int ret = 0;

    memset(&lease, 0, sizeof(lease));
    lease.cmd = GLFS_GET_LEASE;
    lease.lease_type = -1;
    memcpy(&lease.lease_id, ld, GLFS_LEASE_ID_SIZE);
    ret = glfs_lease(fd, &lease, &recall_cbk, NULL);
    if (ret < 0) {
        fprintf(log_file, "\n    GET_LEASE failed with ret: %d (%s)", ret,
                strerror(errno));
        return -1;
    }
    if (lease.lease_type == GLFS_RD_LEASE)
        fprintf(log_file, "\n    Esisting Lease: RD_LEASE");
    else if (lease.lease_type == GLFS_RW_LEASE)
        fprintf(log_file, "\n    Esisting Lease: RW_LEASE");
    else if (lease.lease_type == 3)
        fprintf(log_file, "\n    Esisting Lease: RD_LEASE|RW_LEASE");
    else if (lease.lease_type == 0)
        fprintf(log_file, "\n    Esisting Lease: NONE");
    else
        fprintf(log_file, "\n    Existing lease type:%d", lease.lease_type);
    return lease.lease_type;
}

static int
unlk_write_lease(glfs_fd_t *fd, char ld[])
{
    struct glfs_lease lease = {
        0,
    };
    int ret = 0;

    memset(&lease, 0, sizeof(lease));
    lease.cmd = GLFS_UNLK_LEASE;
    lease.lease_type = GLFS_RW_LEASE;
    memcpy(&lease.lease_id, ld, GLFS_LEASE_ID_SIZE);
    ret = glfs_lease(fd, &lease, &recall_cbk, NULL);
    if (ret < 0) {
        fprintf(log_file, "\n    Unlock RW_LESAE failed with ret: %d (%s)", ret,
                strerror(errno));
        return -1;
    }
    fprintf(log_file, "\n    Unlocked RW_LEASE");
    return ret;
}

static int
unlk_read_lease(glfs_fd_t *fd, char ld[])
{
    struct glfs_lease lease = {
        0,
    };
    int ret = 0;

    memset(&lease, 0, sizeof(lease));
    lease.cmd = GLFS_UNLK_LEASE;
    lease.lease_type = GLFS_RD_LEASE;
    memcpy(&lease.lease_id, ld, GLFS_LEASE_ID_SIZE);

    ret = glfs_lease(fd, &lease, &recall_cbk, NULL);
    if (ret < 0) {
        fprintf(log_file, "\n    Unlock RD_LEASE failed with ret: %d (%s)", ret,
                strerror(errno));
        return -1;
    }
    fprintf(log_file, "\n    Unlocked RD_LEASE");
    return ret;
}

void
up_async_lease_recall(struct glfs_upcall *up_arg, void *data)
{
    struct glfs_upcall_lease *in_arg = NULL;
    enum glfs_upcall_reason reason = 0;
    struct glfs_object *object = NULL;
    uint64_t flags = 0;
    uint64_t expire = 0;

    if (!up_arg)
        return;

    reason = glfs_upcall_get_reason(up_arg);

    /* Expect 'GLFS_UPCALL_RECALL_LEASE' upcall event. */

    if (reason == GLFS_UPCALL_RECALL_LEASE) {
        in_arg = glfs_upcall_get_event(up_arg);

        object = glfs_upcall_lease_get_object(in_arg);

        fprintf(log_file,
                " upcall event type - %d,"
                " object(%p)\n",
                reason, object);
        upcall_recv = 1;
    }

    glfs_free(up_arg);
    return;
}

glfs_t *
setup_new_client(char *volname, char *log_fileile)
{
    int ret = 0;
    glfs_t *fs = NULL;
    int up_events = GLFS_EVENT_ANY;

    fs = glfs_new(volname);
    if (!fs) {
        fprintf(log_file, "\nglfs_new: returned NULL (%s)\n", strerror(errno));
        goto error;
    }

    ret = glfs_set_volfile_server(fs, "tcp", "localhost", 24007);
    if (ret < 0) {
        fprintf(log_file, "\nglfs_set_volfile_server failed ret:%d (%s)\n", ret,
                strerror(errno));
        goto error;
    }

    ret = glfs_set_logging(fs, log_fileile, 7);
    if (ret < 0) {
        fprintf(log_file, "\nglfs_set_logging failed with ret: %d (%s)\n", ret,
                strerror(errno));
        goto error;
    }

    ret = glfs_init(fs);
    if (ret < 0) {
        fprintf(log_file, "\nglfs_init failed with ret: %d (%s)\n", ret,
                strerror(errno));
        goto error;
    }

    /* Register Upcalls */
    ret = glfs_upcall_register(fs, up_events, up_async_lease_recall, NULL);

    /* Check if the return mask contains the event */
    if ((ret < 0) || !(ret & GLFS_EVENT_RECALL_LEASE)) {
        fprintf(stderr,
                "glfs_upcall_register return doesn't contain"
                " upcall event - GLFS_EVENT_RECALL_LEASE\n");
        goto error;
    }

    return fs;
error:
    if (fs)
        glfs_fini(fs);
    return NULL;
}

#define OPEN(client, flags, fd, lease_id)                                      \
    do {                                                                       \
        int ret_val = 0;                                                       \
        ret_val = glfs_setfsleaseid(lease_id);                                 \
        if (ret_val) {                                                         \
            fprintf(log_file,                                                  \
                    "\nglfs_setfsleaseid failed with ret: %d (%s)\n", ret,     \
                    strerror(errno));                                          \
            return -1;                                                         \
        }                                                                      \
        fd = glfs_open(client, TEST_FILE, flags);                              \
        if (fd == NULL) {                                                      \
            fprintf(log_file, "\nglfs_open failed with ret: %d (%s)\n", ret,   \
                    strerror(errno));                                          \
            return -1;                                                         \
        }                                                                      \
    } while (0)

#define VERIFY_RESULT(test_case, ret, value)                                   \
    do {                                                                       \
        if (ret != value) {                                                    \
            fprintf(log_file,                                                  \
                    "\n    Testcase %d failed, ret = %d, value=%d\n",          \
                    test_case, ret, value);                                    \
            goto error; /*test unsuccessful*/                                  \
        }                                                                      \
        fprintf(log_file, "\n    Testcase %d Succeeded\n", test_case);         \
    } while (0)

static void
recall_cbk(struct glfs_lease lease, void *data)
{
    int ret = -1;
    char ld[GLFS_LEASE_ID_SIZE] = "";

    fprintf(log_file, "\nRECALL received on lease_id:(%s)", lease.lease_id);
    memcpy(ld, lease.lease_id, GLFS_LEASE_ID_SIZE);
    ret = unlk_write_lease((glfs_fd_t *)data, ld);
    VERIFY_RESULT(500, ret, SHUD_PASS);
error:
    return;
}

static int
testcase_recall_conflict_lease()
{
    struct glfs_object *obj = NULL;
    glfs_fd_t *fd1 = NULL;
    int ret = 0;
    struct glfs_lease lease = {
        0,
    };

    fprintf(log_file,
            "\n Basic test case for conflicting lease causing recall");

    memset(&lease, 0, sizeof(lease));
    lease.cmd = GLFS_SET_LEASE;
    lease.lease_type = GLFS_RD_LEASE;
    memcpy(&lease.lease_id, lid2, GLFS_LEASE_ID_SIZE);
    /* Open fd on client 1 in RD mode */
    OPEN(client1, O_RDWR, fd1, lid1);
    ret = set_write_lease(fd1, lid1);
    VERIFY_RESULT(1, ret, SHUD_PASS);

    /* reset counter */
    upcall_recv = 0;

    obj = glfs_h_lookupat(client2, NULL, TEST_FILE, NULL, 0);
    ret = glfs_h_lease(client2, obj, &lease);
    VERIFY_RESULT(2, ret, SHUD_FAIL);

    sleep(3);
    /* should recv upcall */
    VERIFY_RESULT(6, !upcall_recv, SHUD_PASS);

    ret = unlk_write_lease(fd1, lid1);
    VERIFY_RESULT(5, ret, SHUD_PASS);

    ret = glfs_h_close(obj);
    VERIFY_RESULT(3, ret, SHUD_PASS);
    ret = glfs_close(fd1);
    VERIFY_RESULT(4, ret, SHUD_PASS);

    return 0;
error:
    return -1;
}

int
main(int argc, char *argv[])
{
    int ret = 0;
    int i = 0;
    glfs_fd_t *fd = NULL;
    glfs_fd_t *fd1 = NULL;
    char *topdir = "topdir", *filename = "file1";
    char *buf = NULL;
    int x = 0;
    ssize_t xattr_size = -1;

    if (argc != 4) {
        fprintf(stderr,
                "Expect following args %s <Vol> <glfs client log file> "
                "<testcase log file>\n",
                argv[0]);
        return -1;
    }

    log_file = fopen(argv[3], "w");
    if (!log_file)
        goto error;

    volname = argv[1];
    glfs_log_file = argv[2];

    /* Setup 2 clients */
    client1 = setup_new_client(volname, glfs_log_file);
    client2 = setup_new_client(volname, glfs_log_file);

    ret = testcase_recall_conflict_lease();
    VERIFY_RESULT(101, ret, SHUD_PASS);

    glfs_fini(client1);
    glfs_fini(client2);

    fclose(log_file);
    return 0;
error:
    return -1;
}
