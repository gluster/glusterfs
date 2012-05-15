/*
  FUSE: Filesystem in Userspace
  Copyright (C) 2001-2007  Miklos Szeredi <miklos@szeredi.hu>

  This program can be distributed under the terms of the GNU LGPLv2.
  See the file COPYING.LIB.
*/

#include <dirent.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>

int fuse_mnt_check_empty(const char *progname, const char *mnt,
			 mode_t rootmode, off_t rootsize)
{
	int isempty = 1;

	if (S_ISDIR(rootmode)) {
		struct dirent *ent;
		DIR *dp = opendir(mnt);
		if (dp == NULL) {
			fprintf(stderr,
				"%s: failed to open mountpoint for reading: %s\n",
				progname, strerror(errno));
			return -1;
		}
		while ((ent = readdir(dp)) != NULL) {
			if (strcmp(ent->d_name, ".") != 0 &&
			    strcmp(ent->d_name, "..") != 0) {
				isempty = 0;
				break;
			}
		}
		closedir(dp);
	} else if (rootsize)
		isempty = 0;

	if (!isempty) {
		fprintf(stderr, "%s: mountpoint is not empty\n", progname);
		fprintf(stderr, "%s: if you are sure this is safe, use the 'nonempty' mount option\n", progname);
		return -1;
	}
	return 0;
}

int fuse_mnt_check_fuseblk(void)
{
	char buf[256];
	FILE *f = fopen("/proc/filesystems", "r");
	if (!f)
		return 1;

	while (fgets(buf, sizeof(buf), f))
		if (strstr(buf, "fuseblk\n")) {
			fclose(f);
			return 1;
		}

	fclose(f);
	return 0;
}
