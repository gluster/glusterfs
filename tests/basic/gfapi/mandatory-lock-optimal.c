/* Pre-requisites:-
 *
 * 1. Make sure that peformance translators are switched off while running this test.
 * 2. Perform the following volume set operation:
 *    # gluster volume set <VOLNAME> locks.mandatory-locking optimal
 * 3. For installation under non-standard paths, export LD_LIBRARY_PATH to
 *    automatically load exact libgfapi.so and compile this C file as follows:
 *    $ gcc mandatory-lock-optimal.c -lgfapi -I <include path for api/glfs.h> -L <include path for libgfapi shared library>
 */

#include <errno.h>
#include <stdio.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <glusterfs/api/glfs.h>

#define TOTAL_TEST_COUNT 8

/* C1 = Client 1 : C2 = Client 2 : C3 = Client 3 :
 * fs1, fd1 are associated with C1. Similarly fs2, fd2 for C2
 * and fs3, fd3 for C3 */

FILE *fp;
glfs_t *fs1, *fs2, *fs3;
glfs_fd_t *fd, *fd1, *fd2, *fd3;
struct flock lock;
char buf1[10], *buf2 = "ten bytes!", *fname = "/mand.lock";
int ret, test_count;
off_t offset;

/* run_test_1 ()   : C1 takes byte range mandatory read lock.
                     C2 attempts to read from a conflicting range.
   Expected result : Read from C2 should pass.

 * run_test_2 ()   : C1 takes byte range mandatory read lock.
                     C2 attempts write to a conflicting range.
   Expected result : Write from C2 should fail with EAGAIN.

 * run_test_3 ()   : C1 takes byte range advisory write lock.
                     C2 attempts to read from a conflicting range.
   Expected result : Read from C2 should pass.

 * run_test_4 ()   : C1 takes byte range advisory write lock.
                     C2 attempts write to a conflicting range.
   Expected result : Write from C2 should pass.

 * run_test_5 ()   : C1 takes byte range advisory read lock.
                     C2 attempts to open the same file with O_TRUNC.
   Expected result : Open from C2 should pass.

 * run_test_6 ()   : C1 takes byte range mandatory read lock.
                     C2 attempts to open the same file with O_TRUNC.
   Expected result : Open from C2 should fail with EAGAIN.

 * run_test_7 ()   : C1 takes byte range mandatory read lock.
                     C2 attempts ftruncate on a conflicting range.
   Expected result : Write from C2 should fail with EAGAIN.

 * run_test_8 ()   : C1 takes byte range advisory read lock.
                     C2 takes byte range mandatory read lock
                     within the byte range for which C1 already
                     holds an advisory lock so as to perform a
                     basic split/merge. C3 repositions fd3 to
                     start of C2's byte range mandatory lock
                     offset and attempts a write. Then it again
                     repositions fd3 to one byte past C2's byte
                     range mandatoy lock and again attempts a write.
   Expected result : First write should fail with EAGAIN.
                     Second write should pass. */

#define LOG_ERR(func, err) do {                                                         \
        if (!fp)                                                                        \
                fprintf (stderr, "\n%s : returned error (%s)\n", func, strerror (err)); \
        else                                                                            \
                fprintf (fp, "\n%s : returned error (%s)\n", func, strerror (err));     \
        cleanup_and_exit (err);                                                         \
} while (0)

void cleanup_and_exit (int exit_status) {
        if (exit_status || test_count != TOTAL_TEST_COUNT) {
                fprintf (fp, "\nAborting due to some test failures.\n");
                exit_status = 1;
        } else
                fprintf (fp, "\nAll tests ran successfully.\n");
        if (fp)
                fclose (fp);
        if (fd)
                glfs_close (fd);
        if (fd1)
                glfs_close (fd1);
        if (fd2)
                glfs_close (fd2);

        glfs_unlink (fs1, fname);

        if (fs1)
                glfs_fini (fs1);
        if (fs2)
                glfs_fini (fs2);

        exit (exit_status);
}

glfs_t *new_client_create (char *hostname, char *volname, char *logfile_name) {
        glfs_t *fs = NULL;

        fs = glfs_new (volname);
        if (!fs)
                LOG_ERR ("glfs_new", errno);

        ret = glfs_set_volfile_server (fs, "tcp", hostname, 24007);
        if (ret)
                LOG_ERR ("glfs_set_volfile_server", errno);

        ret = glfs_set_logging (fs, logfile_name, 7);
        if (ret)
                LOG_ERR ("glfs_set_logging", errno);

        ret = glfs_init (fs);
        if (ret)
                LOG_ERR ("glfs_init", errno);

        return fs;
}

void run_test_1 (int i) {
        fprintf (fp, "\nRunning Test-%d . . . ", i);

        fd1 = glfs_open (fs1, fname, O_RDONLY | O_NONBLOCK);
        if (!fd1)
                LOG_ERR ("glfs_open", errno);

        lock.l_type = F_RDLCK;
        lock.l_whence = SEEK_SET;
        lock.l_start = 0L;
        lock.l_len = 5L;

        ret = glfs_file_lock (fd1, F_SETLK, &lock, GLFS_LK_MANDATORY);
        if (ret)
                LOG_ERR ("glfs_file_lock", errno);

        fd2 = glfs_open (fs2, fname, O_RDONLY | O_NONBLOCK);
        if (!fd2)
                LOG_ERR ("glfs_open", errno);

        /* On successful read, 0 is returned as there is no content inside the
         * file
         */
        ret = glfs_read (fd2, buf1, 10, 0);
        if (ret)
                LOG_ERR ("glfs_read", errno);

        ret = glfs_close (fd1);
        if (ret)
                LOG_ERR ("glfs_close", errno);
        fd1 = NULL;

        ret = glfs_close (fd2);
        if (ret)
                LOG_ERR ("glfs_close", errno);
        fd2 = NULL;

        test_count++;
        fprintf (fp, "OK\n", i);
}

void run_test_2 (int i) {
        fprintf (fp, "\nRunning Test-%d . . . ", i);

        fd1 = glfs_open (fs1, fname, O_RDONLY | O_NONBLOCK);
        if (!fd1)
                LOG_ERR ("glfs_open", errno);

        lock.l_type = F_RDLCK;
        lock.l_whence = SEEK_SET;
        lock.l_start = 0L;
        lock.l_len = 5L;

        ret = glfs_file_lock (fd1, F_SETLK, &lock, GLFS_LK_MANDATORY);
        if (ret)
                LOG_ERR ("glfs_file_lock", errno);

        fd2 = glfs_open (fs2, fname, O_WRONLY | O_NONBLOCK);
        if (!fd2)
                LOG_ERR ("glfs_open", errno);

        ret = glfs_write (fd2, buf2, 10, 0);
        if (ret == 10 || errno != EAGAIN)
                LOG_ERR ("glfs_write", errno);

        ret = glfs_close (fd1);
        if (ret)
                LOG_ERR ("glfs_close", errno);
        fd1 = NULL;

        ret = glfs_close (fd2);
        if (ret)
                LOG_ERR ("glfs_close", errno);
        fd2 = NULL;

        test_count++;
        fprintf (fp, "OK\n", i);
}

void run_test_3 (int i) {
        fprintf (fp, "\nRunning Test-%d . . . ", i);

        fd1 = glfs_open (fs1, fname, O_WRONLY | O_NONBLOCK);
        if (!fd1)
                LOG_ERR ("glfs_open", errno);

        lock.l_type = F_WRLCK;
        lock.l_whence = SEEK_SET;
        lock.l_start = 0L;
        lock.l_len = 5L;

        ret = glfs_file_lock (fd1, F_SETLK, &lock, GLFS_LK_ADVISORY);
        if (ret)
                LOG_ERR ("glfs_file_lock", errno);

        fd2 = glfs_open (fs2, fname, O_RDONLY | O_NONBLOCK);
        if (!fd2)
                LOG_ERR ("glfs_open", errno);

        /* Still there is no content inside file. So following read should
         * return 0
         */
        ret = glfs_read (fd2, buf1, 10, 0);
        if (ret)
                LOG_ERR ("glfs_read", errno);

        ret = glfs_close (fd1);
        if (ret)
                LOG_ERR ("glfs_close", errno);
        fd1 = NULL;

        ret = glfs_close (fd2);
        if (ret)
                LOG_ERR ("glfs_close", errno);
        fd2 = NULL;

        test_count++;
        fprintf (fp, "OK\n", i);
}

void run_test_4 (int i) {
        fprintf (fp, "\nRunning Test-%d . . . ", i);

        fd1 = glfs_open (fs1, fname, O_WRONLY | O_NONBLOCK);
        if (!fd1)
                LOG_ERR ("glfs_open", errno);

        lock.l_type = F_WRLCK;
        lock.l_whence = SEEK_SET;
        lock.l_start = 0L;
        lock.l_len = 5L;

        ret = glfs_file_lock (fd1, F_SETLK, &lock, GLFS_LK_ADVISORY);
        if (ret)
                LOG_ERR ("glfs_file_lock", errno);

        fd2 = glfs_open (fs2, fname, O_WRONLY | O_NONBLOCK);
        if (!fd2)
                LOG_ERR ("glfs_open", errno);

        ret = glfs_write (fd2, buf2, 10, 0);
        if (ret != 10)
                LOG_ERR ("glfs_write", errno);

        ret = glfs_close (fd1);
        if (ret)
                LOG_ERR ("glfs_close", errno);
        fd1 = NULL;

        ret = glfs_close (fd2);
        if (ret)
                LOG_ERR ("glfs_close", errno);
        fd2 = NULL;

        test_count++;
        fprintf (fp, "OK\n", i);
}

void run_test_5 (int i) {
        fprintf (fp, "\nRunning Test-%d . . . ", i);

        fd1 = glfs_open (fs1, fname, O_RDONLY | O_NONBLOCK);
        if (!fd1)
                LOG_ERR ("glfs_open", errno);

        lock.l_type = F_RDLCK;
        lock.l_whence = SEEK_SET;
        lock.l_start = 0L;
        lock.l_len = 5L;

        ret = glfs_file_lock (fd1, F_SETLK, &lock, GLFS_LK_ADVISORY);
        if (ret)
                LOG_ERR ("glfs_file_lock", errno);

        fd2 = glfs_open (fs2, fname, O_RDONLY | O_NONBLOCK | O_TRUNC);
        if (!fd2)
                LOG_ERR ("glfs_open", errno);

        ret = glfs_close (fd1);
        if (ret)
                LOG_ERR ("glfs_close", errno);
        fd1 = NULL;

        ret = glfs_close (fd2);
        if (ret)
                LOG_ERR ("glfs_close", errno);
        fd2 = NULL;

        test_count++;
        fprintf (fp, "OK\n", i);
}

void run_test_6 (int i) {
        fprintf (fp, "\nRunning Test-%d . . . ", i);

        fd1 = glfs_open (fs1, fname, O_RDONLY | O_NONBLOCK);
        if (!fd1)
                LOG_ERR ("glfs_open", errno);

        lock.l_type = F_RDLCK;
        lock.l_whence = SEEK_SET;
        lock.l_start = 0L;
        lock.l_len = 5L;

        ret = glfs_file_lock (fd1, F_SETLK, &lock, GLFS_LK_MANDATORY);
        if (ret)
                LOG_ERR ("glfs_file_lock", errno);

        fd2 = glfs_open (fs2, fname, O_RDONLY | O_NONBLOCK | O_TRUNC);
        if (fd2)
                LOG_ERR ("glfs_open", errno);

        ret = glfs_close (fd1);
        if (ret)
                LOG_ERR ("glfs_close", errno);
        fd1 = NULL;

        test_count++;
        fprintf (fp, "OK\n", i);
}

void run_test_7 (int i) {
        fprintf (fp, "\nRunning Test-%d . . . ", i);

        fd1 = glfs_open (fs1, fname, O_RDONLY | O_NONBLOCK);
        if (!fd1)
                LOG_ERR ("glfs_open", errno);

        lock.l_type = F_RDLCK;
        lock.l_whence = SEEK_SET;
        lock.l_start = 0L;
        lock.l_len = 5L;

        ret = glfs_file_lock (fd1, F_SETLK, &lock, GLFS_LK_MANDATORY);
        if (ret)
                LOG_ERR ("glfs_file_lock", errno);

        fd2 = glfs_open (fs2, fname, O_RDWR | O_NONBLOCK);
        if (!fd2)
                LOG_ERR ("glfs_open", errno);

        ret = glfs_ftruncate (fd2, 4);
        if (ret == 0 || errno != EAGAIN)
                LOG_ERR ("glfs_ftruncate", errno);

        ret = glfs_close (fd1);
        if (ret)
                LOG_ERR ("glfs_close", errno);
        fd1 = NULL;

        ret = glfs_close (fd2);
        if (ret)
                LOG_ERR ("glfs_close", errno);
        fd2 = NULL;

        test_count++;
        fprintf (fp, "OK\n", i);
}

void run_test_8 (int i) {
        fprintf (fp, "\nRunning Test-%d . . . ", i);

        fd1 = glfs_open (fs1, fname, O_RDONLY | O_NONBLOCK);
        if (!fd1)
                LOG_ERR ("glfs_open", errno);

        lock.l_type = F_RDLCK;
        lock.l_whence = SEEK_SET;
        lock.l_start = 0L;
        lock.l_len = 10L;

        ret = glfs_file_lock (fd1, F_SETLK, &lock, GLFS_LK_ADVISORY);
        if (ret)
                LOG_ERR ("glfs_file_lock", errno);

        fd2 = glfs_open (fs2, fname, O_RDONLY | O_NONBLOCK);
        if (!fd2)
                LOG_ERR ("glfs_open", errno);

        lock.l_type = F_RDLCK;
        lock.l_whence = SEEK_SET;
        lock.l_start = 5L;
        lock.l_len = 2L;

        ret = glfs_file_lock (fd2, F_SETLK, &lock, GLFS_LK_MANDATORY);
        if (ret)
                LOG_ERR ("glfs_file_lock", errno);

        fd3 = glfs_open (fs3, fname, O_RDWR | O_NONBLOCK);
        if (!fd3)
                LOG_ERR ("glfs_open", errno);

        offset = glfs_lseek (fd3, 5L, SEEK_SET);
        if (offset != 5)
                LOG_ERR ("glfs_lseek", errno);

        ret = glfs_write (fd3, buf2, 10, 0);
        if (ret == 10 || errno != EAGAIN)
                LOG_ERR ("glfs_write", errno);

        offset = glfs_lseek (fd3, 8L, SEEK_SET);
        if (offset != 8)
                LOG_ERR ("glfs_lseek", errno);

        ret = glfs_write (fd3, buf2, 10, 0);
        if (ret != 10)
                LOG_ERR ("glfs_write", errno);

        ret = glfs_close (fd1);
        if (ret)
                LOG_ERR ("glfs_close", errno);
        fd1 = NULL;

        ret = glfs_close (fd2);
        if (ret)
                LOG_ERR ("glfs_close", errno);
        fd2 = NULL;

        ret = glfs_close (fd3);
        if (ret)
                LOG_ERR ("glfs_close", errno);
        fd3 = NULL;

        test_count++;
        fprintf (fp, "OK\n", i);
}

int main (int argc, char *argv[]) {
        char logfile[50];

        if (argc != 4) {
                fprintf (stderr, "Usage: %s <server ip/hostname> <volume name> <test log directory>\n", argv[0]);
                return 0;
        }

        sprintf (logfile, "%s/%s", argv[3], "mandatory-lock-optimal-test.log");
        fp = fopen (logfile, "w");
        if (!fp) {
                fprintf (stderr, "\n%s\n", logfile);
                LOG_ERR ("Log file creation", errno);
        }

        sprintf (logfile, "%s/%s", argv[3], "glfs-client-1.log");
        fs1 = new_client_create (argv[1], argv[2], logfile);
        if (!fs1)
                LOG_ERR ("client-1 creation", EINVAL);

        sprintf (logfile, "%s/%s", argv[3], "glfs-client-2.log");
        fs2 = new_client_create (argv[1], argv[2], logfile);
        if (!fs2)
                LOG_ERR ("client-2 creation", EINVAL);

        sprintf (logfile, "%s/%s", argv[3], "glfs-client-3.log");
        fs3 = new_client_create (argv[1], argv[2], logfile);
        if (!fs3)
                LOG_ERR ("client-3 creation", EINVAL);

        fd = glfs_creat (fs1, fname, O_RDWR, 0644);
        if (!fd)
                LOG_ERR ("glfs_creat", errno);

        test_count = 0;

        run_test_1 (1);
        run_test_2 (2);
        run_test_3 (3);
        run_test_4 (4);
        run_test_5 (5);
        run_test_6 (6);
        run_test_7 (7);
        run_test_8 (8);

        cleanup_and_exit (0);

        return 0;
}
