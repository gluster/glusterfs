/*
  Copyright (c) 2008-2013 Red Hat, Inc. <http://www.redhat.com>
  This file is part of GlusterFS.

  This file is licensed to you under your choice of the GNU Lesser
  General Public License, version 3 or any later version (LGPLv3 or
  later), or the GNU General Public License, version 2 (GPLv2), in all
  cases as published by the Free Software Foundation.
*/

#include <stdlib.h>
#include <stdio.h>
#include <sys/types.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <unistd.h>
#include <fcntl.h>

#define MAX_NUM_OPS (1 << 20)
#define MAX_FILE_SIZE (1 << 30)

typedef enum {
	READ_OP,
	WRITE_OP,
	TRUNC_OP,
	LAST_OP
} frag_op;

struct frag_ctx {
	int test_fd;
	int good_fd;
	char *test_buf;
	char *good_buf;
	char *content;
	int max_file_size;
};

typedef int (*frag_op_t)(struct frag_ctx *ctx, off_t offset, size_t count);

static int doread(int fd, off_t offset, size_t count,
		  char *buf, int max_file_size)
{
	int ret = 0;
	int was_read = 0;

	if (lseek(fd, offset, SEEK_SET) == -1) {
		perror("lseek failed");
                return -1;
	}
	while (count) {
		ret = read(fd, buf + offset + was_read, count);
		if (ret < 0)
			return -1;
		if (ret == 0)
			break;
		if (ret > count) {
			fprintf(stderr, "READ: read more than asked\n");
			return -1;
		}
		count -= ret;
		was_read += ret;
	}
	return ret;
}

static int dowrite(int fd, off_t offset, size_t count, char *buf)
{
	int ret;

	ret = lseek(fd, offset, SEEK_SET);
	if (ret == -1)
                return ret;
	return write(fd, buf, count);
}

static int dotrunc(int fd, off_t offset)
{
	int ret;

	ret = ftruncate(fd, offset);
	if (ret == -1)
		perror("truncate failed");
	return ret;
}

static int prepare_file(char *filename, int *fd, char **buf, int max_file_size)
{
	int ret;

	*buf = malloc(max_file_size);
	if (*buf == NULL) {
		perror("malloc failed");
	        return -1;
	}
	*fd = open(filename, O_CREAT | O_RDWR, S_IRWXU);
	if (*fd == -1) {
		perror("open failed");
		free(*buf);
		*buf = NULL;
		return -1;
	}
	return 0;
}

/*
 * @offset, @count: random values from [0, max_file_size - 1]
 */
static int frag_write(struct frag_ctx *ctx, off_t offset, size_t count)
{
	int ret;
	struct stat test_stbuf;
	struct stat good_stbuf;

	if (offset + count > ctx->max_file_size)
		offset = offset / 2;
	if (offset + count > ctx->max_file_size)
		count = count / 2;

	if (fstat(ctx->test_fd, &test_stbuf)) {
		fprintf(stderr, "WRITE: fstat of test file failed\n");
		return -1;
	}
	if (offset > test_stbuf.st_size)
		printf("writing hole\n");

	ret = dowrite(ctx->test_fd, offset, count, ctx->content);
	if (ret < 0 || ret != count){
		fprintf(stderr, "WRITE: failed to write test file\n");
		return -1;
	}
	ret = dowrite(ctx->good_fd, offset, count, ctx->content);
	if (ret < 0 || ret != count) {
		fprintf(stderr, "WRITE: failed to write test file\n");
		return -1;
	}	
	if (fstat(ctx->test_fd, &test_stbuf)) {
                fprintf(stderr, "WRITE: fstat of test file failed\n");
		return -1;
	}
	if (fstat(ctx->good_fd, &good_stbuf)) {
                fprintf(stderr, "WRITE: fstat of good file failed\n");
		return -1;
	}
	if (test_stbuf.st_size != good_stbuf.st_size) {
		fprintf(stderr,
			"READ: Bad file size %d (expected %d)\n",
			(int)test_stbuf.st_size,
			(int)good_stbuf.st_size);
		return -1;
	}
	return 0;
}

/*
 * @offset, @count: random values from [0, max_file_size - 1]
 */
static int frag_read(struct frag_ctx *ctx, off_t offset, size_t count)
{
	ssize_t test_ret;
	ssize_t good_ret;

	test_ret = doread(ctx->test_fd,
			  offset, count, ctx->test_buf, ctx->max_file_size);
	if (test_ret < 0) {
		fprintf(stderr, "READ: failed to read test file\n");
		return -1;
	}
	good_ret = doread(ctx->good_fd,
			  offset, count, ctx->good_buf, ctx->max_file_size);
	if (good_ret < 0) {
		fprintf(stderr, "READ: failed to read good file\n");
		return -1;
	}
	if (test_ret != good_ret) {
		fprintf(stderr,
			"READ: Bad return value %d (expected %d\n)",
			test_ret, good_ret);
		return -1;
	}
	if (memcmp(ctx->test_buf + offset, ctx->good_buf + offset, good_ret)) {
		fprintf(stderr, "READ: bad data\n");
		return -1;
	}
	return 0;
}

/*
 * @offset: random value from [0, max_file_size - 1]
 */
static int frag_truncate(struct frag_ctx *ctx,
			 off_t offset, __attribute__((unused))size_t count)
{
	int ret;
	struct stat test_stbuf;
	struct stat good_stbuf;

	if (fstat(ctx->test_fd, &test_stbuf)) {
		fprintf(stderr, "TRUNCATE: fstat of test file failed\n");
		return -1;
	}
	if (offset > test_stbuf.st_size)
		printf("expanding truncate to %d\n", offset);
	else if (offset < test_stbuf.st_size)
		printf("shrinking truncate to %d\n", offset);
	else
		printf("trivial truncate\n");
	
	ret = dotrunc(ctx->test_fd, offset);
	if (ret == -1) {
		fprintf(stderr, "TRUNCATE: failed for test file\n");
		return -1;
	}
	ret = dotrunc(ctx->good_fd, offset);
	if (ret == -1) {
		fprintf(stderr, "TRUNCATE: failed for good file\n");
		return -1;
	}
	if (fstat(ctx->test_fd, &test_stbuf)) {
		fprintf(stderr, "TRUNCATE: fstat of test file failed\n");
		return -1;
	}
	if (fstat(ctx->good_fd, &good_stbuf)) {
		fprintf(stderr, "TRUNCATE: fstat of good file failed\n");
		return -1;
	}
	if (test_stbuf.st_size != good_stbuf.st_size) {
		fprintf(stderr,
			"TRUNCATE: bad test file size %d (expected %d)\n",
			test_stbuf.st_size,
			good_stbuf.st_size);
		return -1;
	}
	return 0;
}

frag_op_t frag_ops[LAST_OP] = {
	[READ_OP]  = frag_read,
	[WRITE_OP] = frag_write,
	[TRUNC_OP] = frag_truncate
};

static void put_ctx(struct frag_ctx *ctx)
{
	if (ctx->test_buf)
		free(ctx->test_buf);
	if (ctx->good_buf)
		free(ctx->good_buf);
	if (ctx->content)
		free(ctx->content);
}

main (int argc, char *argv[])
{
	int i;
	int ret = 0;
	struct frag_ctx ctx;
	char *test_filename = NULL;
	char *good_filename = NULL;
	int num_ops;
	int max_file_size;

	memset(&ctx, 0, sizeof(ctx));
	if (argc != 5) {
                fprintf(stderr,
			"usage: %s <test-file-name> <good-file-name> <max-file-size> <number-of-operations>\n",
			argv[0]);
		ret = -1;
                goto exit;
        }
	test_filename = argv[1];
	good_filename = argv[2];
	max_file_size = atoi(argv[3]);
	if (max_file_size > MAX_FILE_SIZE)
		max_file_size = MAX_FILE_SIZE;
	num_ops = atoi(argv[4]);
	if (num_ops > MAX_NUM_OPS)
		num_ops = MAX_NUM_OPS;

	ret = prepare_file(test_filename,
			   &ctx.test_fd, &ctx.test_buf, max_file_size);
	if (ret)
		goto exit;
	ret = prepare_file(good_filename,
			   &ctx.good_fd, &ctx.good_buf, max_file_size);
	if (ret) {
		if (close(ctx.test_fd) == -1)
			perror("close test_buf failed");
		goto exit;
	}
	ctx.content = malloc(max_file_size);
	if (!ctx.content) {
		perror("malloc failed");
		goto close;
	}
	ctx.max_file_size = max_file_size;
	for (i = 0; i < max_file_size; i++)
                ctx.content[i] = random() % 256;

	for (i = 0; i < num_ops; i++) {
		ret = frag_ops[random() % LAST_OP](&ctx,
					random() % max_file_size, /* offset */
					random() % max_file_size  /* count */);
		if (ret)
			break;
	}
 close:
	if (close(ctx.test_fd) == -1)
		perror("close test_fd failed");
	if (close(ctx.good_fd) == -1)
		perror("close good_fd failed");	
 exit:
	put_ctx(&ctx);
	if (ret)
		exit(1);
	exit(0);
}

/*
  Local variables:
  c-indentation-style: "K&R"
  mode-name: "LC"
  c-basic-offset: 8
  tab-width: 8
  fill-column: 80
  scroll-step: 1
  End:
*/
