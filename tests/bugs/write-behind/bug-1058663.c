#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <signal.h>

#define FILE_SIZE 1048576

/* number of tests to run */
#define RUN_LOOP 1000

/* number of SIGBUS before exiting */
#define MAX_SIGBUS 1
static int expect_sigbus;
static int sigbus_received;

/* test for truncate()/seek()/write()/mmap()
 * There should ne no SIGBUS triggered.
 */
void seek_write(char *filename)
{
	int fd;
	uint8_t *map;
	int i;

	fd = open(filename, O_RDWR|O_CREAT|O_TRUNC, 0600);
	lseek(fd, FILE_SIZE - 1, SEEK_SET);
	write(fd, "\xff", 1);

	map = mmap(NULL, FILE_SIZE, PROT_READ, MAP_PRIVATE, fd, 0);
	for (i = 0; i < (FILE_SIZE - 1); i++) {
		if (map[i] != 0) /* should never be true */
			abort();
	}
	munmap(map, FILE_SIZE);

	close(fd);
}

int read_after_eof(char *filename)
{
	int ret = 0;
	int fd;
	char *data;
	uint8_t *map;

	fd = open(filename, O_RDWR|O_CREAT|O_TRUNC, 0600);
	lseek(fd, FILE_SIZE - 1, SEEK_SET);
	write(fd, "\xff", 1);

	/* trigger verify that reading after EOF fails */
	ret = read(fd, data, FILE_SIZE / 2);
	if (ret != 0)
		return 1;

	/* map an area of 1 byte after FILE_SIZE */
	map = mmap(NULL, 1, PROT_READ, MAP_PRIVATE, fd, FILE_SIZE);
	/* map[0] is an access after EOF, it should trigger SIGBUS */
	if (map[0] != 0)
		/* it is expected that we exit before we get here */
		if (!sigbus_received)
			return 1;
	munmap(map, FILE_SIZE);

	close(fd);

	return ret;
}

/* signal handler for SIGBUS */
void catch_sigbus(int signum)
{
	switch (signum) {
#ifdef __NetBSD__
	/* Depending on architecture, we can get SIGSEGV */
	case SIGSEGV: /* FALLTHROUGH */
#endif
	case SIGBUS:
		sigbus_received++;
		if (!expect_sigbus)
			exit(EXIT_FAILURE);
		if (sigbus_received >= MAX_SIGBUS)
			exit(EXIT_SUCCESS);
		break;
	default:
		printf("Unexpected signal received: %d\n", signum);
	}
}

int main(int argc, char **argv)
{
	int i = 0;

	if (argc == 1) {
		printf("Usage: %s <filename>\n", argv[0]);
		return EXIT_FAILURE;
	}

#ifdef __NetBSD__
	/* Depending on architecture, we can get SIGSEGV */
	signal(SIGSEGV, catch_sigbus);
#endif
	signal(SIGBUS, catch_sigbus);

	/* the next test should not trigger SIGBUS */
	expect_sigbus = 0;
	for (i = 0; i < RUN_LOOP; i++) {
		seek_write(argv[1]);
	}

	/* the next test should trigger SIGBUS */
	expect_sigbus = 1;
	if (read_after_eof(argv[1]))
		return EXIT_FAILURE;

	return EXIT_SUCCESS;
}
