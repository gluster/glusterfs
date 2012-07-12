#include <stdio.h>
#include <errno.h>
#include "api/glfs.h"
#include <string.h>
#include <time.h>

int
main (int argc, char *argv[])
{
	glfs_t    *fs = NULL;
	int        ret = 0;
	glfs_fd_t *fd = NULL;
	struct stat sb = {0, };
	char       readbuf[32];
	char       writebuf[32];

	char      *volname = "iops";
	char      *filename = "/filename2";

	fs = glfs_new (volname);
	if (!fs) {
		fprintf (stderr, "glfs_new: returned NULL\n");
		return 1;
	}

//	ret = glfs_set_volfile (fs, "/tmp/filename.vol");

	ret = glfs_set_volfile_server (fs, "socket", "localhost", 24007);

//	ret = glfs_set_volfile_server (fs, "unix", "/tmp/gluster.sock", 0);

	ret = glfs_set_logging (fs, "/dev/stderr", 7);

	ret = glfs_init (fs);

	fprintf (stderr, "glfs_init: returned %d\n", ret);


	ret = glfs_lstat (fs, filename, &sb);
	fprintf (stderr, "%s: (%d) %s\n", filename, ret, strerror (errno));

	fd = glfs_creat (fs, filename, O_RDWR, 0644);
	fprintf (stderr, "%s: (%p) %s\n", filename, fd, strerror (errno));

	sprintf (writebuf, "hi there\n");
	ret = glfs_write (fd, writebuf, 32, 0);

	glfs_lseek (fd, 0, SEEK_SET);

	ret = glfs_read (fd, readbuf, 32, 0);

	printf ("read %d, %s", ret, readbuf);

	glfs_close (fd);

	return ret;
}
