#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <glusterfs/api/glfs.h>
#include <glusterfs/api/glfs-handles.h>
#include <time.h>

#define VALIDATE_AND_GOTO_LABEL_ON_ERROR(func, ret, label) do { \
        if (ret < 0) {            \
                fprintf (stderr, "%s : returned error %d (%s)\n", \
                         func, ret, strerror (errno)); \
                goto label; \
        } \
        } while (0)

#define VALIDATE_BOOL_AND_GOTO_LABEL_ON_ERROR(func, bool_var, ret, label) do { \
        if (!bool_var) {            \
                fprintf (stderr, "%s : returned error (%s)\n", \
                         func, strerror (errno)); \
                ret = -1;                         \
                goto label; \
        } \
        } while (0)

#define MAX_FILES_CREATE 10
#define MAXPATHNAME      512

void
assimilatetime (struct timespec *ts, struct timespec ts_st,
                struct timespec ts_ed)
{
        if ((ts_ed.tv_nsec - ts_st.tv_nsec) < 0) {
                ts->tv_sec += ts_ed.tv_sec - ts_st.tv_sec - 1;
                ts->tv_nsec += 1000000000 + ts_ed.tv_nsec - ts_st.tv_nsec;
        } else {
                ts->tv_sec += ts_ed.tv_sec - ts_st.tv_sec;
                ts->tv_nsec += ts_ed.tv_nsec - ts_st.tv_nsec;
        }

        if (ts->tv_nsec > 1000000000) {
                ts->tv_nsec = ts->tv_nsec - 1000000000;
                ts->tv_sec += 1;
        }

        return;
}

/*
 * Returns '%' difference between ts1 & ts2
 */
int
comparetime (struct timespec ts1, struct timespec ts2)
{
        uint64_t ts1_n, ts2_n;
        int pct = 0;

        ts1_n = (ts1.tv_sec * 1000000000) + ts1.tv_nsec;
        ts2_n = (ts2.tv_sec * 1000000000) + ts2.tv_nsec;

        pct = ((ts1_n - ts2_n)*100)/ts1_n;

        return pct;
}

int
old_readdir (glfs_t *fs)
{
        struct glfs_object     *root = NULL;
        struct glfs_fd         *fd = NULL;
        struct stat            *sb = NULL;
        char buf[512];
        struct dirent          *entry = NULL;
        int                     ret = -1;
        struct glfs_object     *glhandle = NULL;

        if (!fs)
                return -1;

        root = glfs_h_lookupat (fs, NULL, "/", sb, 0);
        VALIDATE_BOOL_AND_GOTO_LABEL_ON_ERROR ("glfs_h_lookupat", !!root, ret, out);

        fd = glfs_opendir (fs, "/");
        VALIDATE_BOOL_AND_GOTO_LABEL_ON_ERROR ("glfs_opendir", !!fd, ret, out);

        while (glfs_readdir_r (fd, (struct dirent *)buf, &entry), entry) {
                if (strcmp(entry->d_name, ".") && strcmp(entry->d_name, "..")) {
                        glhandle = glfs_h_lookupat (fs, root, "/", sb, 0);
                        VALIDATE_BOOL_AND_GOTO_LABEL_ON_ERROR ("glfs_h_lookupat", !!glhandle, ret, out);
                }
        }

        glfs_closedir (fd);

        ret  = 0;
out:
        return ret;
}

int
new_xreaddirplus (glfs_t *fs)
{
        struct glfs_fd         *fd = NULL;
        struct stat            *sb = NULL;
        int                     ret = -1;
        uint32_t                rflags = (GFAPI_XREADDIRP_STAT |
                                          GFAPI_XREADDIRP_HANDLE);
        struct glfs_xreaddirp_stat   *xstat = NULL;
        struct dirent           de;
        struct dirent          *pde = NULL;
        struct glfs_object     *glhandle = NULL;

        if (!fs)
                return -1;

        fd = glfs_opendir (fs, "/");
        VALIDATE_BOOL_AND_GOTO_LABEL_ON_ERROR ("glfs_opendir", !!fd, ret, out);

        ret = glfs_xreaddirplus_r(fd, rflags, &xstat, &de, &pde);
        while (ret > 0 && pde != NULL) {
                if (xstat) {
                        sb = glfs_xreaddirplus_get_stat (xstat);
                        VALIDATE_BOOL_AND_GOTO_LABEL_ON_ERROR ("glfs_xreaddirplus_get_stat", !!sb, ret, out);

                        if (strcmp(de.d_name, ".") && strcmp(de.d_name, "..")) {
                                glhandle = glfs_xreaddirplus_get_object (xstat);
                                VALIDATE_BOOL_AND_GOTO_LABEL_ON_ERROR ("glfs_xreaddirplus_get_object", !!glhandle, ret, out);
                        }
                }

                if (xstat) {
                        glfs_free(xstat);
                        xstat = NULL;
                }

                ret = glfs_xreaddirplus_r(fd, rflags, &xstat, &de, &pde);

                VALIDATE_AND_GOTO_LABEL_ON_ERROR ("glfs_xreaddirp_r", ret, out);

        }

        if (xstat)
                glfs_free(xstat);

        ret  = 0;
out:
        return ret;
}

int
main (int argc, char *argv[])
{
        int                     ret = -1;
        glfs_t                 *fs = NULL;
        char                   *volname = NULL;
        char                   *logfile = NULL;
        char                   *hostname = NULL;
        char                   *my_file = "file_";
        char                    my_file_name[MAXPATHNAME];
        uint32_t                flags = O_RDWR|O_SYNC;
        struct glfs_fd         *fd = NULL;
        int                     i = 0;
        int                     pct = 0;
        struct timespec         timestamp = {0, 0}, st_timestamp, ed_timestamp;
        struct timespec         otimestamp = {0, 0}, ost_timestamp, oed_timestamp;

        if (argc != 4) {
                fprintf (stderr, "Invalid argument\n");
                return 1;
        }

        hostname = argv[1];
        volname = argv[2];
        logfile = argv[3];

        fs = glfs_new (volname);
        VALIDATE_BOOL_AND_GOTO_LABEL_ON_ERROR ("glfs_new", !!fs, ret, out);

        ret = glfs_set_volfile_server (fs, "tcp", hostname, 24007);
        VALIDATE_AND_GOTO_LABEL_ON_ERROR ("glfs_set_volfile_server", ret, out);

        ret = glfs_set_logging (fs, logfile, 7);
        VALIDATE_AND_GOTO_LABEL_ON_ERROR ("glfs_set_logging", ret, out);

        ret = glfs_init (fs);
        VALIDATE_AND_GOTO_LABEL_ON_ERROR ("glfs_init", ret, out);

        for (i = 0; i < MAX_FILES_CREATE; i++) {
                sprintf (my_file_name, "%s%d", my_file, i);

                fd = glfs_creat(fs, my_file_name, flags, 0644);
                VALIDATE_BOOL_AND_GOTO_LABEL_ON_ERROR ("glfs_creat", !!fd, ret, out);

                glfs_close (fd);
        }

        /* measure performance using old readdir call and new xreaddirplus call and compare */
        ret = clock_gettime (CLOCK_REALTIME, &ost_timestamp);
        VALIDATE_AND_GOTO_LABEL_ON_ERROR ("clock_gettime", ret, out);

        ret = old_readdir (fs);
        VALIDATE_AND_GOTO_LABEL_ON_ERROR ("old_readdir", ret, out);

        ret = clock_gettime (CLOCK_REALTIME, &oed_timestamp);
        VALIDATE_AND_GOTO_LABEL_ON_ERROR ("clock_gettime", ret, out);

        assimilatetime (&otimestamp, ost_timestamp, oed_timestamp);

        printf ("\tOverall time using readdir:\n\t\tSecs:%ld\n\t\tnSecs:%ld\n",
                otimestamp.tv_sec, otimestamp.tv_nsec);


        ret = clock_gettime (CLOCK_REALTIME, &st_timestamp);
        VALIDATE_AND_GOTO_LABEL_ON_ERROR ("clock_gettime", ret, out);

        ret = new_xreaddirplus (fs);
        VALIDATE_AND_GOTO_LABEL_ON_ERROR ("new_xreaddirplus", ret, out);

        ret = clock_gettime (CLOCK_REALTIME, &ed_timestamp);
        VALIDATE_AND_GOTO_LABEL_ON_ERROR ("clock_gettime", ret, out);

        assimilatetime (&timestamp, st_timestamp, ed_timestamp);

        printf ("\tOverall time using xreaddirplus:\n\t\tSecs:%ld\n\t\tnSecs:%ld\n",
                timestamp.tv_sec, timestamp.tv_nsec);


        pct = comparetime (otimestamp, timestamp);
        printf ("There is improvement by %d%%\n", pct);

        ret = 0;
out:
        if (fs) {
                ret = glfs_fini(fs);
                if (ret)
                        fprintf (stderr, "glfs_fini(fs) returned %d\n", ret);
        }

        return ret;
}


