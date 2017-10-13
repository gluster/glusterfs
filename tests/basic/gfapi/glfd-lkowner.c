#include <fcntl.h>
#include <unistd.h>
#include <time.h>
#include <limits.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <glusterfs/api/glfs.h>
#include <glusterfs/api/glfs-handles.h>
#include <sys/wait.h>
#include <stdbool.h>

int gfapi = 1;

#define LOG_ERR(func, ret) do { \
        if (ret != 0) {            \
                fprintf (stderr, "%s : returned error %d (%s)\n", \
                         func, ret, strerror (errno)); \
                goto out; \
        } else { \
               fprintf (stderr, "%s : returned %d\n", func, ret); \
        } \
        } while (0)

char lownera[8] = "lownera", lownerb[8] = "lownerb";
char lownerc[8] = "lownerc";

int lock_test (glfs_fd_t *glfd1, glfs_fd_t *glfd2, bool should_fail,
               int l1_start, int l1_len, char *l1_owner, int lo1_len,
               int l2_start, int l2_len, char *l2_owner, int lo2_len)
{
        int ret = -1, f_ret = -1;
        struct flock lock1 = {0, }, lock2 = {0, };

lock1:
        if (!glfd1)
                goto lock2;

        /* lock on glfd1 */
        lock1.l_type = F_WRLCK;
        lock1.l_whence = SEEK_SET;
        lock1.l_start = l1_start;
        lock1.l_len = l1_len;

        ret = glfs_fd_set_lkowner (glfd1, l1_owner, lo1_len);
        LOG_ERR ("glfs_fd_set_lkowner on glfd1", ret);

        ret = glfs_posix_lock (glfd1, F_SETLK, &lock1);
        LOG_ERR ("glfs_posix_lock on glfd1", ret);

lock2:
        if (!glfd2)
                goto out;

        /* lock on glfd2 */
        lock2.l_type = F_WRLCK;
        lock2.l_whence = SEEK_SET;
        lock2.l_start = l2_start;
        lock2.l_len = l2_len;

        ret = glfs_fd_set_lkowner (glfd2, l2_owner, lo2_len);
        LOG_ERR ("glfs_fd_set_lkowner on glfd2", ret);

        ret = glfs_posix_lock (glfd2, F_SETLK, &lock2);

        if (should_fail && ret) {
                f_ret = 0;
        } else if (!ret && !should_fail) {
                f_ret = 0;
        } else {
                f_ret = -1;
        }
out:
        fprintf (stderr, "Lock test on glfd1 (start(%d), len(%d),"
                         " lk_owner(%s)) and glfd2 (start(%d), len(%d), "
                         "lk_owner(%s)) - expected(%s) - result(%s)\n",
                         l1_start, l1_len, l1_owner, l2_start, l2_len, l2_owner,
                         (should_fail ? "FAIL" : "SUCCESS"),
                         (ret ? "FAIL" : "SUCCESS"));
        return f_ret;
}

int
main (int argc, char *argv[])
{
        glfs_t    *fs = NULL;
        int        ret = 0, i, status = 0;
        glfs_fd_t *fd1 = NULL;
        glfs_fd_t *fd2 = NULL;
        glfs_fd_t *fd3 = NULL;
        char      *filename = "file_tmp";
        char      *volname = NULL;
        char      *logfile = NULL;
        char      *hostname = NULL;

        if (argc != 4) {
                fprintf (stderr, "Invalid argument\n");
                exit(1);
        }

        hostname = argv[1];
        volname = argv[2];
        logfile = argv[3];

        fs = glfs_new (volname);
        if (!fs) {
                fprintf (stderr, "glfs_new: returned NULL\n");
                return -1;
        }

        ret = glfs_set_volfile_server (fs, "tcp", hostname, 24007);
        LOG_ERR("glfs_set_volfile_server", ret);

        ret = glfs_set_logging (fs, logfile, 7);
        LOG_ERR("glfs_set_logging", ret);

        ret = glfs_init (fs);
        LOG_ERR("glfs_init", ret);

        fd1 = glfs_creat(fs, filename, O_RDWR|O_SYNC, 0644);
        if (fd1 <= 0) {
                ret = -1;
                LOG_ERR ("glfs_creat", ret);
        }
        fprintf (stderr, "glfs-create fd1 - %d\n", fd1);

        fd2 = glfs_dup(fd1);
        fprintf (stderr, "glfs-dup fd2 - %d\n", fd2);

        fd3 = glfs_open(fs, filename, O_RDWR|O_SYNC);
        if (fd2 <= 0) {
                ret = -1;
                LOG_ERR ("glfs_open", ret);
        }
        fprintf (stderr, "glfs-open fd3 - %d\n", fd3);

        /* TEST 1: Conflicting ranges, same lk_owner
         * lock1 (0, 10, lownera)
         * lock2 (5, 10, lownera)
         * Expected: should not fail but get merged
         */
        ret = lock_test (fd1, fd2, false, 0, 10, lownera, 8,
                         5, 10, lownera, 8);
        LOG_ERR ("==== glfs_lock_test_1", ret);

        /* TEST 2: Conflicting ranges, different lk_owner
         * lock1 (0, 10, lownera) - already taken
         * lock2 (5, 10, lownerb)
         * Expected: should fail and not get merged
         */
        ret = lock_test (NULL, fd2, true, 0, 10, lownera, 8,
                         5, 10, lownerb, 8);
        LOG_ERR ("==== glfs_lock_test_2", ret);

        /* TEST 3: Different ranges, same lk_owner
         * lock1 (0, 10, lownera) - already taken
         * lock2 (30, 10, lownera)
         * Expected: should not fail
         */
        ret = lock_test (NULL, fd2, false, 0, 10, lownera, 8,
                         30, 10, lownera, 8);
        LOG_ERR ("==== glfs_lock_test_3", ret);

        /* TEST 4: Conflicting ranges, different lk_owner
         * lock1 (0, 10, lownera) - already taken
         * lock2 (50, 10, lownerb)
         * Expected: should not fail
         */
        ret = lock_test (NULL, fd2, false, 0, 10, lownera, 8,
                         50, 10, lownerb, 8);
        LOG_ERR ("==== glfs_lock_test_4", ret);

        /* TEST 5: Close fd1 & retry TEST2
         * lock1 (not applicable)
         * lock2 (5, 10, lownerb)
         * Expected: should succeed now
         */
        ret = glfs_close(fd1);
        LOG_ERR ("glfs_close", ret);

        ret = lock_test (NULL, fd2, false, 0, 10, lownera, 8,
                         5, 10, lownerb, 8);
        LOG_ERR ("==== glfs_lock_test_5", ret);

        /* TEST 6: Check closing fd1 doesn't flush fd2 locks
         * retry TEST 4 but with fd2 and fd3.
         * lock1 (50, 10, lownerb) - already taken
         * lock2 (55, 10, lownerc)
         * Expected: should fail
         */
        ret = lock_test (NULL, fd3, true, 50, 10, lownerb, 8,
                         55, 10, lownerc, 8);
        LOG_ERR ("==== glfs_lock_test_6", ret);

err:
        ret = glfs_close(fd2);
        LOG_ERR ("glfs_close", ret);

        ret = glfs_close(fd3);
        LOG_ERR ("glfs_close", ret);

out:
        if (fs) {
                ret = glfs_fini(fs);
                fprintf (stderr, "glfs_fini(fs) returned %d\n", ret);
        }

        if (ret)
                exit(1);
        exit(0);
}
