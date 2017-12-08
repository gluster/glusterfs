/*
 * ./test_bz1523599 0 vm140-111 gv0 test211 log
 * ./test_bz1523599 1 vm140-111 gv0 test211 log
 * Open - Discard - Read - Then check read information to see if the initial TEST_STR_LEN/2 bytes read zero
 */

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <glusterfs/api/glfs.h>
#include <glusterfs/api/glfs-handles.h>
#include <errno.h>
#include <sys/uio.h>

#define TEST_STR_LEN 2048

enum fallocate_flag {
	TEST_WRITE,
	TEST_DISCARD,
	TEST_ZEROFILL,
};

void print_str(char *str, int len)
{
	int i, addr;

	printf("%07x\t", 0);
	for (i = 0; i < len; i++) {
		printf("%02x", str[i]);
		if (i) {
			if ((i + 1) % 16 == 0)
				printf("\n%07x\t", i+1);
			else if ((i + 1) % 4 == 0)
				printf(" ");
		}
	}
	printf("\n");
}

int
test_read(char *str, int total_length, int len_zero)
{
	int i;
	int ret = 0;

	for (i = 0; i < len_zero; i++) {
		if (str[i]) {
			fprintf(stderr, "char at position %d not zeroed out\n",
				i);
			ret = -EIO;
			goto out;
		}
	}

	for (i = len_zero; i < total_length; i++) {
		if (str[i] != 0x11) {
			fprintf(stderr,
				"char at position %d does not contain pattern\n",
				i);
			ret = -EIO;
			goto out;
		}
	}
out:
	return ret;
}

int main(int argc, char *argv[])
{
	int opcode;
	char *host_name, *volume_name, *file_path, *glfs_log_path;
	glfs_t *fs = NULL;
	glfs_fd_t *fd = NULL;
	off_t offset = 0;
	size_t len_zero = TEST_STR_LEN / 2;
	char writestr[TEST_STR_LEN];
	char readstr[TEST_STR_LEN];
	struct iovec iov = {&readstr, TEST_STR_LEN};
	int i;
	int ret = 1;

	for (i = 0; i < TEST_STR_LEN; i++)
		writestr[i] = 0x11;
	for (i = 0; i < TEST_STR_LEN; i++)
		readstr[i] = 0x22;

	if (argc != 6) {
		fprintf(stderr,
			"Syntax: %s <test type> <host> <volname> <file-path> <log-file>\n",
			argv[0]);
		return 1;
	}

	opcode = atoi(argv[1]);
	host_name = argv[2];
	volume_name = argv[3];
	file_path = argv[4];
	glfs_log_path = argv[5];

	fs = glfs_new(volume_name);
	if (!fs) {
		perror("glfs_new");
		return 1;
	}

	ret = glfs_set_volfile_server(fs, "tcp", host_name, 24007);
	if (ret != 0) {
		perror("glfs_set_volfile_server");
		goto out;
	}

	ret = glfs_set_logging(fs, glfs_log_path, 7);
	if (ret != 0) {
		perror("glfs_set_logging");
		goto out;
	}

	ret = glfs_init(fs);
	if (ret != 0) {
		perror("glfs_init");
		goto out;
	}

	fd = glfs_creat(fs, file_path, O_RDWR, 0777);
	if (fd == NULL) {
		perror("glfs_creat");
		ret = -1;
		goto out;
	}

	switch (opcode) {
		case TEST_WRITE:
			fprintf(stderr, "Test Write\n");
			ret = glfs_write(fd, writestr, TEST_STR_LEN, 0);
			if (ret < 0) {
				perror("glfs_write");
				goto out;
			} else if (ret != TEST_STR_LEN) {
				fprintf(stderr, "insufficient data written %d \n", ret);
				ret = -EIO;
				goto out;
			}
			ret = 0;
			goto out;
		case TEST_DISCARD:
			fprintf(stderr, "Test Discard\n");
			ret = glfs_discard(fd, offset, len_zero);
			if (ret < 0) {
				if (errno == EOPNOTSUPP) {
					fprintf(stderr, "Operation not supported\n");
					ret = 0;
					goto out;
				}
				perror("glfs_discard");
				goto out;
			}
			goto test_read;
		case TEST_ZEROFILL:
			fprintf(stderr, "Test Zerofill\n");
			ret = glfs_zerofill(fd, offset, len_zero);
			if (ret < 0) {
				if (errno == EOPNOTSUPP) {
					fprintf(stderr, "Operation not supported\n");
					ret = 0;
					goto out;
				}
				perror("glfs_zerofill");
				goto out;
			}
			goto test_read;
		default:
			ret = -1;
			fprintf(stderr, "Incorrect test code %d\n", opcode);
			goto out;
	}

test_read:
	ret = glfs_readv(fd, &iov, 1, 0);
	if (ret < 0) {
		perror("glfs_readv");
		goto out;
	}

	/* printf("Read str\n"); print_str(readstr, TEST_STR_LEN); printf("\n"); */
	ret = test_read(readstr, TEST_STR_LEN, len_zero);

out:
	if (fd)
		glfs_close(fd);
	glfs_fini(fs);

	if (ret)
		return -1;

	return 0;
}
