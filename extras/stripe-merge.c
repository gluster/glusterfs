/*
  Copyright (c) 2008-2012 Red Hat, Inc. <http://www.redhat.com>
  This file is part of GlusterFS.

  This file is licensed to you under your choice of the GNU Lesser
  General Public License, version 3 or any later version (LGPLv3 or
  later), or the GNU General Public License, version 2 (GPLv2), in all
  cases as published by the Free Software Foundation.
*/

/*
 * stripe-merge.c
 *
 * This program recovers an original file based on the striped files stored on
 * the individual bricks of a striped volume. The file format and stripe
 * geometry is validated through the extended attributes stored in the file.
 *
 * TODO: Support optional xattr recovery (i.e., user xattrs). Perhaps provide a
 * 	 command-line flag to toggle this behavior.
 */

#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdint.h>
#include <errno.h>
#include <string.h>
#include <attr/xattr.h>
#include <fnmatch.h>

#define ATTRNAME_STRIPE_INDEX "trusted.*.stripe-index"
#define ATTRNAME_STRIPE_COUNT "trusted.*.stripe-count"
#define ATTRNAME_STRIPE_SIZE "trusted.*.stripe-size"
#define ATTRNAME_STRIPE_COALESCE "trusted.*.stripe-coalesce"

#define INVALID_FD -1
#define INVALID_MODE UINT32_MAX

struct file_stripe_info {
	int stripe_count;
	int stripe_size;
	int coalesce;
	mode_t mode;
	int fd[0];
};

static int close_files(struct file_stripe_info *);

static struct
file_stripe_info *alloc_file_stripe_info(int count)
{
	int i;
	struct file_stripe_info *finfo;

	finfo = calloc(1, sizeof(struct file_stripe_info) +
		(sizeof(int) * count));
	if (!finfo)
		return NULL;

	for (i = 0; i < count; i++)
		finfo->fd[i] = INVALID_FD;

	finfo->mode = INVALID_MODE;
	finfo->coalesce = INVALID_FD;

	return finfo;
}

/*
 * Search for an attribute matching the provided pattern. Return a count for
 * the total number of matching entries (including 0). Allocate a buffer for
 * the first matching entry found.
 */
static int
get_stripe_attr_name(const char *path, const char *pattern, char **attrname)
{
	char attrbuf[4096];
	char *ptr, *match = NULL;
	int len, r, match_count = 0;

	if (!path || !pattern || !attrname)
		return -1;

	len = listxattr(path, attrbuf, sizeof(attrbuf));
	if (len < 0)
		return len;

	ptr = attrbuf;
	while (ptr) {
		r = fnmatch(pattern, ptr, 0);
		if (!r) {
			if (!match)
				match = ptr;
			match_count++;
		} else if (r != FNM_NOMATCH) {
			return -1;
		}

		len -= strlen(ptr) + 1;
		if (len > 0)
			ptr += strlen(ptr) + 1;
		else
			ptr = NULL;
	}

	if (match)
		*attrname = strdup(match);

	return match_count;
}

/*
 * Get the integer representation of a named attribute.
 */
static int
get_stripe_attr_val(const char *path, const char *attr, int *val)
{
	char attrbuf[4096];
	int len;

	if (!path || !attr || !val)
		return -1;

	len = getxattr(path, attr, attrbuf, sizeof(attrbuf));
	if (len < 0)
		return len;

	*val = atoi(attrbuf);

	return 0;
}

/*
 * Get an attribute name/value (assumed to be an integer) pair based on a
 * specified search pattern. A buffer is allocated for the exact attr name
 * returned. Optionally, skip the pattern search if a buffer is provided
 * (which should contain an attribute name).
 *
 * Returns the attribute count or -1 on error. The value parameter is set only
 * when a single attribute is found.
 */
static int
get_attr(const char *path, const char *pattern, char **buf, int *val)
{
	int count = 1;

	if (!buf)
		return -1;

	if (!*buf) {
		count = get_stripe_attr_name(path, pattern, buf);
		if (count > 1) {
			/* pattern isn't good enough */
			fprintf(stderr, "ERROR: duplicate attributes found "
				"matching pattern: %s\n", pattern);
			free(*buf);
			*buf = NULL;
			return count;
		} else if (count < 1) {
			return count;
		}
	}

	if (get_stripe_attr_val(path, *buf, val) < 0)
		return -1;

	return count;
}

/*
 * validate_and_open_files()
 *
 * Open the provided source files and validate the extended attributes. Verify
 * that the geometric attributes are consistent across all of the files and
 * print a warning if any files are missing. We proceed without error in the
 * latter case to support partial recovery.
 */
static struct
file_stripe_info *validate_and_open_files(char *paths[], int count)
{
	int i, val, tmp;
	struct stat sbuf;
	char *stripe_count_attr = NULL;
	char *stripe_size_attr = NULL;
	char *stripe_index_attr = NULL;
	char *stripe_coalesce_attr = NULL;
	struct file_stripe_info *finfo = NULL;

	for (i = 0; i < count; i++) {
		if (!paths[i])
			goto err;

		/*
		 * Check the stripe count first so we can allocate the info
		 * struct with the appropriate number of fds.
		 */
		if (get_attr(paths[i], ATTRNAME_STRIPE_COUNT,
				&stripe_count_attr, &val) != 1) {
			fprintf(stderr, "ERROR: %s: attribute: '%s'\n",
				paths[i], ATTRNAME_STRIPE_COUNT);
			goto err;
		}
		if (!finfo) {
			finfo = alloc_file_stripe_info(val);
			if (!finfo)
				goto err;

			if (val != count)
				fprintf(stderr, "WARNING: %s: stripe-count "
					"(%d) != file count (%d). Result may "
					"be incomplete.\n", paths[i], val,
					count);

			finfo->stripe_count = val;
		} else if (val != finfo->stripe_count) {
			fprintf(stderr, "ERROR %s: invalid stripe count: %d "
				"(expected %d)\n", paths[i], val,
				finfo->stripe_count);
			goto err;
		}

		/*
		 * Get and validate the chunk size.
		 */
		if (get_attr(paths[i], ATTRNAME_STRIPE_SIZE, &stripe_size_attr,
				&val) != 1) {
			fprintf(stderr, "ERROR: %s: attribute: '%s'\n",
				paths[i], ATTRNAME_STRIPE_SIZE);
			goto err;
		}

		if (!finfo->stripe_size) {
			finfo->stripe_size = val;
		} else if (val != finfo->stripe_size) {
			fprintf(stderr, "ERROR: %s: invalid stripe size: %d "
				"(expected %d)\n", paths[i], val,
				finfo->stripe_size);
			goto err;
		}

		/*
		 * stripe-coalesce is a backward compatible attribute. If the
		 * attribute does not exist, assume a value of zero for the
		 * traditional stripe format.
		 */
		tmp = get_attr(paths[i], ATTRNAME_STRIPE_COALESCE,
				&stripe_coalesce_attr, &val);
		if (!tmp) {
			val = 0;
		} else if (tmp != 1) {
			fprintf(stderr, "ERROR: %s: attribute: '%s'\n",
				paths[i], ATTRNAME_STRIPE_COALESCE);
			goto err;
		}

		if (finfo->coalesce == INVALID_FD) {
			finfo->coalesce = val;
		} else if (val != finfo->coalesce) {
			fprintf(stderr, "ERROR: %s: invalid coalesce flag\n",
				paths[i]);
			goto err;
		}

		/*
		 * Get/validate the stripe index and open the file in the
		 * appropriate fd slot.
		 */
		if (get_attr(paths[i], ATTRNAME_STRIPE_INDEX,
				&stripe_index_attr, &val) != 1) {
			fprintf(stderr, "ERROR: %s: attribute: '%s'\n",
				paths[i], ATTRNAME_STRIPE_INDEX);
			goto err;
		}
		if (finfo->fd[val] != INVALID_FD) {
			fprintf(stderr, "ERROR: %s: duplicate stripe index: "
				"%d\n", paths[i], val);
			goto err;
		}

		finfo->fd[val] = open(paths[i], O_RDONLY);
		if (finfo->fd[val] < 0)
			goto err;

		/*
		 * Get the creation mode for the file.
		 */
		if (fstat(finfo->fd[val], &sbuf) < 0)
			goto err;
		if (finfo->mode == INVALID_MODE) {
			finfo->mode = sbuf.st_mode;
		} else if (sbuf.st_mode != finfo->mode) {
			fprintf(stderr, "ERROR: %s: invalid mode\n", paths[i]);
			goto err;
		}
	}

	free(stripe_count_attr);
	free(stripe_size_attr);
	free(stripe_index_attr);
	free(stripe_coalesce_attr);

	return finfo;
err:

	free(stripe_count_attr);
	free(stripe_size_attr);
	free(stripe_index_attr);
	free(stripe_coalesce_attr);

	if (finfo) {
		close_files(finfo);
		free(finfo);
	}

	return NULL;
}

static int
close_files(struct file_stripe_info *finfo)
{
	int i, ret;

	if (!finfo)
		return -1;

	for (i = 0; i < finfo->stripe_count; i++) {
		if (finfo->fd[i] == INVALID_FD)
			continue;

		ret = close(finfo->fd[i]);
		if (ret < 0)
			return ret;
	}

	return ret;
}

/*
 * Generate the original file using files striped in the coalesced format.
 * Data in the striped files is stored at a coalesced offset based on the
 * stripe number.
 *
 * Walk through the finfo fds (which are already ordered) and and iteratively
 * copy stripe_size bytes from the source files to the target file. If a source
 * file is missing, seek past the associated stripe_size bytes in the target
 * file.
 */
static int
generate_file_coalesce(int target, struct file_stripe_info *finfo)
{
	char *buf;
	int ret = 0;
	int r, w, i;

	buf = malloc(finfo->stripe_size);
	if (!buf)
		return -1;

	i = 0;
	while (1) {
		if (finfo->fd[i] == INVALID_FD) {
			if (lseek(target, finfo->stripe_size, SEEK_CUR) < 0)
				break;

			i = (i + 1) % finfo->stripe_count;
			continue;
		}

		r = read(finfo->fd[i], buf, finfo->stripe_size);
		if (r < 0) {
			ret = r;
			break;
		}
		if (!r)
			break;

		w = write(target, buf, r);
		if (w < 0) {
			ret = w;
			break;
		}

		i = (i + 1) % finfo->stripe_count;
	}

	free(buf);
	return ret;
}

/*
 * Generate the original file using files striped with the traditional stripe
 * format. Data in the striped files is stored at the equivalent offset from
 * the source file.
 */
static int
generate_file_traditional(int target, struct file_stripe_info *finfo)
{
	int i, j, max_ret, ret;
	char buf[finfo->stripe_count][4096];

	do {
		char newbuf[4096] = {0, };

		max_ret = 0;
		for (i = 0; i < finfo->stripe_count; i++) {
			memset(buf[i], 0, 4096);
			ret = read(finfo->fd[i], buf[i], 4096);
			if (ret > max_ret)
				max_ret = ret;
		}
		for (i = 0; i < max_ret; i++)
			for (j = 0; j < finfo->stripe_count; j++)
				newbuf[i] |= buf[j][i];
			write(target, newbuf, max_ret);
	} while (max_ret);

	return 0;
}

static int
generate_file(int target, struct file_stripe_info *finfo)
{
	if (finfo->coalesce)
		return generate_file_coalesce(target, finfo);

	return generate_file_traditional(target, finfo);
}

static void
usage(char *name)
{
	fprintf(stderr, "Usage: %s [-o <outputfile>] <inputfile1> "
		"<inputfile2> ...\n", name);
}

int
main(int argc, char *argv[])
{
	int file_count, opt;
	char *opath = NULL;
	int targetfd;
	struct file_stripe_info *finfo;

	while ((opt = getopt(argc, argv, "o:")) != -1) {
		switch (opt) {
		case 'o':
			opath = optarg;
			break;
		default:
			usage(argv[0]);
			return -1;
		}
	}

	file_count = argc - optind;

	if (!opath || !file_count) {
		usage(argv[0]);
		return -1;
	}

	finfo = validate_and_open_files(&argv[optind], file_count);
	if (!finfo)
		goto err;

	targetfd = open(opath, O_RDWR|O_CREAT, finfo->mode);
	if (targetfd < 0)
		goto err;

	if (generate_file(targetfd, finfo) < 0)
		goto err;

	if (fsync(targetfd) < 0)
		fprintf(stderr, "ERROR: %s\n", strerror(errno));
	if (close(targetfd) < 0)
		fprintf(stderr, "ERROR: %s\n", strerror(errno));

	close_files(finfo);
	free(finfo);

	return 0;

err:
	if (finfo) {
		close_files(finfo);
		free(finfo);
	}

	return -1;
}

