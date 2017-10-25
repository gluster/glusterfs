/*
 * Create a glfs instance based on a .vol file
 *
 * This is used to measure memory leaks by initializing a graph through a .vol
 * file and destroying it again.
 */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include <glusterfs/api/glfs.h>

#define PROGNAME "gfapi-load-volfile"

void
usage(FILE *output)
{
	fprintf(output, "Usage: " PROGNAME " <volfile>\n");
}

void
main(int argc, char **argv)
{
	int ret = 0;
	glfs_t *fs = NULL;

	if (argc != 2) {
		usage(stderr);
		exit(EXIT_FAILURE);
	}

	if (!strcmp(argv[1], "-h") || !strcmp(argv[1], "-h")) {
		usage(stdout);
		exit(EXIT_SUCCESS);
	}

	fs = glfs_new(PROGNAME);
	if (!fs) {
		perror("glfs_new failed");
		exit(EXIT_FAILURE);
	}

	glfs_set_logging(fs, PROGNAME ".log", 9);

	ret = glfs_set_volfile(fs, argv[1]);
	if (ret) {
		perror("glfs_set_volfile failed");
		ret = EXIT_FAILURE;
		goto out;
	}

	ret = glfs_init(fs);
	if (ret) {
		perror("glfs_init failed");
		ret = EXIT_FAILURE;
		goto out;
	}

	ret = EXIT_SUCCESS;
out:
	glfs_fini(fs);

	exit(ret);
}
