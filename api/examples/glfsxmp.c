#include <stdio.h>
#include <errno.h>
#include "api/glfs.h"
#include <string.h>
#include <time.h>


int
test_dirops (glfs_t *fs)
{
	glfs_fd_t *fd = NULL;
	char buf[512];
	struct dirent *entry = NULL;

	fd = glfs_opendir (fs, "/");
	if (!fd) {
		fprintf (stderr, "/: %s\n", strerror (errno));
		return -1;
	}

	fprintf (stderr, "Entries:\n");
	while (glfs_readdir_r (fd, (struct dirent *)buf, &entry), entry) {
		fprintf (stderr, "%s: %lu\n", entry->d_name, glfs_telldir (fd));
	}

	glfs_closedir (fd);
	return 0;
}


int
test_xattr (glfs_t *fs)
{
	char *filename = "/filename2";
	char buf[512];
	char *ptr;
	int ret;

	ret = glfs_setxattr (fs, filename, "user.testkey", "testval", 8, 0);
	fprintf (stderr, "setxattr(%s): %d (%s)\n", filename, ret,
		 strerror (errno));

	ret = glfs_setxattr (fs, filename, "user.testkey2", "testval", 8, 0);
	fprintf (stderr, "setxattr(%s): %d (%s)\n", filename, ret,
		 strerror (errno));

	ret = glfs_listxattr (fs, filename, buf, 512);
	fprintf (stderr, "listxattr(%s): %d (%s)\n", filename, ret,
		 strerror (errno));
	if (ret < 0)
		return -1;

	for (ptr = buf; ptr < buf + ret; ptr++) {
		printf ("key=%s\n", ptr);
		ptr += strlen (ptr);
	}

	return 0;
}


int
test_chdir (glfs_t *fs)
{
	int ret = -1;
	ino_t ino = 0;
	struct stat st;
	char *topdir = "/topdir";
	char *linkdir = "/linkdir";
	char *subdir = "./subdir";
	char *respath = NULL;
	char pathbuf[4096];

	ret = glfs_mkdir (fs, topdir, 0755);
	if (ret) {
		fprintf (stderr, "mkdir(%s): %s\n", topdir, strerror (errno));
		return -1;
	}

	respath = glfs_getcwd (fs, pathbuf, 4096);
	fprintf (stdout, "getcwd() = %s\n", respath);

	ret = glfs_symlink (fs, topdir, linkdir);
	if (ret) {
		fprintf (stderr, "symlink(%s, %s): %s\n", topdir, linkdir, strerror (errno));
		return -1;
	}

	ret = glfs_chdir (fs, linkdir);
	if (ret) {
		fprintf (stderr, "chdir(%s): %s\n", linkdir, strerror (errno));
		return -1;
	}

	respath = glfs_getcwd (fs, pathbuf, 4096);
	fprintf (stdout, "getcwd() = %s\n", respath);

	respath = glfs_realpath (fs, subdir, pathbuf);
	if (respath) {
		fprintf (stderr, "realpath(%s) worked unexpectedly: %s\n", subdir, respath);
		return -1;
	}

	ret = glfs_mkdir (fs, subdir, 0755);
	if (ret) {
		fprintf (stderr, "mkdir(%s): %s\n", subdir, strerror (errno));
		return -1;
	}

	respath = glfs_realpath (fs, subdir, pathbuf);
	if (!respath) {
		fprintf (stderr, "realpath(%s): %s\n", subdir, strerror (errno));
	} else {
		fprintf (stdout, "realpath(%s) = %s\n", subdir, respath);
	}

	ret = glfs_chdir (fs, subdir);
	if (ret) {
		fprintf (stderr, "chdir(%s): %s\n", subdir, strerror (errno));
		return -1;
	}

	respath = glfs_getcwd (fs, pathbuf, 4096);
	fprintf (stdout, "getcwd() = %s\n", respath);

	respath = glfs_realpath (fs, "/linkdir/subdir", pathbuf);
	if (!respath) {
		fprintf (stderr, "realpath(/linkdir/subdir): %s\n", strerror (errno));
	} else {
		fprintf (stdout, "realpath(/linkdir/subdir) = %s\n", respath);
	}

	return 0;
}


int
main (int argc, char *argv[])
{
	glfs_t    *fs = NULL;
	glfs_t    *fs2 = NULL;
	int        ret = 0;
	glfs_fd_t *fd = NULL;
	glfs_fd_t *fd2 = NULL;
	struct stat sb = {0, };
	char       readbuf[32];
	char       writebuf[32];

	char      *filename = "/filename2";

	fs = glfs_new ("fsync");
	if (!fs) {
		fprintf (stderr, "glfs_new: returned NULL\n");
		return 1;
	}

//	ret = glfs_set_volfile (fs, "/tmp/posix.vol");

	ret = glfs_set_volfile_server (fs, "tcp", "localhost", 24007);

//	ret = glfs_set_volfile_server (fs, "unix", "/tmp/gluster.sock", 0);

	ret = glfs_set_logging (fs, "/dev/stderr", 7);

	ret = glfs_init (fs);

	fprintf (stderr, "glfs_init: returned %d\n", ret);

	sleep (2);

	fs2 = glfs_new ("fsync");
	if (!fs2) {
		fprintf (stderr, "glfs_new: returned NULL\n");
		return 1;
	}


//	ret = glfs_set_volfile (fs2, "/tmp/posix.vol");

	ret = glfs_set_volfile_server (fs2, "tcp", "localhost", 24007);

	ret = glfs_set_logging (fs2, "/dev/stderr", 7);

	ret = glfs_init (fs2);

	fprintf (stderr, "glfs_init: returned %d\n", ret);

	ret = glfs_lstat (fs, filename, &sb);
	fprintf (stderr, "%s: (%d) %s\n", filename, ret, strerror (errno));

	fd = glfs_creat (fs, filename, O_RDWR, 0644);
	fprintf (stderr, "%s: (%p) %s\n", filename, fd, strerror (errno));

	fd2 = glfs_open (fs2, filename, O_RDWR);
	fprintf (stderr, "%s: (%p) %s\n", filename, fd, strerror (errno));

	sprintf (writebuf, "hi there\n");
	ret = glfs_write (fd, writebuf, 32, 0);

	glfs_lseek (fd2, 0, SEEK_SET);

	ret = glfs_read (fd2, readbuf, 32, 0);

	printf ("read %d, %s", ret, readbuf);

	glfs_close (fd);
	glfs_close (fd2);

	filename = "/filename3";
	ret = glfs_mknod (fs, filename, S_IFIFO, 0);
	fprintf (stderr, "%s: (%d) %s\n", filename, ret, strerror (errno));

	ret = glfs_lstat (fs, filename, &sb);
	fprintf (stderr, "%s: (%d) %s\n", filename, ret, strerror (errno));


	ret = glfs_rename (fs, filename, "/filename4");
	fprintf (stderr, "rename(%s): (%d) %s\n", filename, ret,
		 strerror (errno));

	ret = glfs_unlink (fs, "/filename4");
	fprintf (stderr, "unlink(%s): (%d) %s\n", "/filename4", ret,
		 strerror (errno));

	filename = "/dirname2";
	ret = glfs_mkdir (fs, filename, 0);
	fprintf (stderr, "%s: (%d) %s\n", filename, ret, strerror (errno));

	ret = glfs_lstat (fs, filename, &sb);
	fprintf (stderr, "lstat(%s): (%d) %s\n", filename, ret, strerror (errno));

	ret = glfs_rmdir (fs, filename);
	fprintf (stderr, "rmdir(%s): (%d) %s\n", filename, ret, strerror (errno));

	test_dirops (fs);

	test_xattr (fs);

	test_chdir (fs);

	// done

	glfs_fini (fs);
	glfs_fini (fs2);

	return ret;
}
