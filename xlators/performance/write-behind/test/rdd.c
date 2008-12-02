#include <stdio.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <fcntl.h>
#include <unistd.h>
#include <pthread.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <argp.h>

#define TWO_POWER(power) (2UL << (power))

#define RDD_INTEGER_VALUE ((TWO_POWER ((sizeof (int) * 8))) - 1)

#ifndef UNIX_PATH_MAX
#define UNIX_PATH_MAX 108
#endif

struct rdd_file {
	char path[UNIX_PATH_MAX];
	struct stat st;
	int fd;
};
 
struct rdd_config {
	long iters;
	long max_ops_per_seq;
	size_t max_bs;
	size_t min_bs;
	int thread_count;
	pthread_t *threads;
	pthread_barrier_t barrier;
	pthread_mutex_t lock;
	struct rdd_file in_file;
	struct rdd_file out_file;
};
static struct rdd_config rdd_config;

enum rdd_keys {
	RDD_MIN_BS_KEY = 1,
	RDD_MAX_BS_KEY,
};
 
static error_t
rdd_parse_opts (int key, char *arg,
		struct argp_state *_state)
{
	switch (key) {
	case 'o':
	{
		int len = 0;
		len = strlen (arg);
		if (len > UNIX_PATH_MAX) {
			fprintf (stderr, "output file name too long (%s)\n", arg);
			return -1;
		}

		strncpy (rdd_config.out_file.path, arg, len);
	}
	break;

	case 'i':
	{
		int len = 0;
		len = strlen (arg);
		if (len > UNIX_PATH_MAX) {
			fprintf (stderr, "input file name too long (%s)\n", arg);
			return -1;
		}

		strncpy (rdd_config.in_file.path, arg, len);
	}
	break;

	case RDD_MIN_BS_KEY:
	{
		char *tmp = NULL;
		long bs = 0;
		bs = strtol (arg, &tmp, 10);
		if ((bs == LONG_MAX) || (bs == LONG_MIN) || (tmp && *tmp)) {
			fprintf (stderr, "invalid argument for minimum block size (%s)\n", arg);
			return -1;
		}

		rdd_config.min_bs = bs;
	}
	break;

	case RDD_MAX_BS_KEY:
	{
		char *tmp = NULL;
		long bs = 0;
		bs = strtol (arg, &tmp, 10);
		if ((bs == LONG_MAX) || (bs == LONG_MIN) || (tmp && *tmp)) {
			fprintf (stderr, "invalid argument for maximum block size (%s)\n", arg);
			return -1;
		}

		rdd_config.max_bs = bs;
	}
	break;

	case 'r':
	{
		char *tmp = NULL;
		long iters = 0;
		iters = strtol (arg, &tmp, 10);
		if ((iters == LONG_MAX) || (iters == LONG_MIN) || (tmp && *tmp)) {
			fprintf (stderr, "invalid argument for iterations (%s)\n", arg);
			return -1;
		}

		rdd_config.iters = iters;
	}
	break;

	case 'm':
	{
		char *tmp = NULL;
		long max_ops = 0;
		max_ops = strtol (arg, &tmp, 10);
		if ((max_ops == LONG_MAX) || (max_ops == LONG_MIN) || (tmp && *tmp)) {
			fprintf (stderr, "invalid argument for max-ops (%s)\n", arg);
			return -1;
		}

		rdd_config.max_ops_per_seq = max_ops;
	}
	break;

	case 't':
	{
		char *tmp = NULL;
		long threads = 0;
		threads = strtol (arg, &tmp, 10);
		if ((threads == LONG_MAX) || (threads == LONG_MIN) || (tmp && *tmp)) {
			fprintf (stderr, "invalid argument for thread count (%s)\n", arg);
			return -1;
		}

		rdd_config.thread_count = threads;
	}
	break;

	case ARGP_KEY_NO_ARGS:
		break;
	case ARGP_KEY_ARG:
		break;
	case ARGP_KEY_END:
		if (_state->argc == 1) {
			argp_usage (_state); 
		}

	}

	return 0;
}

static struct argp_option rdd_options[] = {
	{"if", 'i', "INPUT_FILE", 0, "input-file"},
	{"of", 'o', "OUTPUT_FILE", 0, "output-file"},
	{"threads", 't', "COUNT", 0, "number of threads to spawn (defaults to 2)"},
	{"min-bs", RDD_MIN_BS_KEY, "MIN_BLOCK_SIZE", 0, 
	 "Minimum block size in bytes (defaults to 1024)"},
	{"max-bs", RDD_MAX_BS_KEY, "MAX_BLOCK_SIZE", 0,
	 "Maximum block size in bytes (defaults to 4096)"},
	{"iters", 'r', "ITERS", 0,
	 "Number of read-write sequences (defaults to 1000000)"},
	{"max-ops", 'm', "MAXOPS", 0,
	 "maximum number of read-writes to be performed in a sequence (defaults to 1)"},
	{0, 0, 0, 0, 0}
};

static struct argp argp = {
  rdd_options,
  rdd_parse_opts,
  "",
  "random dd - tool to do a sequence of random block-sized continuous read writes starting at a random offset"
};


static void
rdd_default_config (void)
{
	rdd_config.thread_count = 2;
	rdd_config.iters = 1000000;
	rdd_config.max_bs = 4096;
	rdd_config.min_bs = 1024;
	rdd_config.in_file.fd = rdd_config.out_file.fd = -1;
	rdd_config.max_ops_per_seq = 1;

	return;
}


static char
rdd_valid_config (void)
{
	char ret = 1;
	int fd = -1;

	fd = open (rdd_config.in_file.path, O_RDONLY);
	if (fd == -1) {
		ret = 0;
		goto out;
	}
	close (fd);

	if (rdd_config.min_bs > rdd_config.max_bs) {
		ret = 0;
		goto out;
	}

	if (strlen (rdd_config.out_file.path) == 0) {
		sprintf (rdd_config.out_file.path, "%s.rddout", rdd_config.in_file.path);
	}

out:
	return ret;
}


static void *
rdd_read_write (void *arg)
{
	int i = 0, ret = 0;
	size_t bs = 0;
	off_t offset = 0;
	long rand = 0;
	long max_ops = 0;
	char *buf = NULL;

	buf = calloc (1, rdd_config.max_bs);
	if (!buf) {
		fprintf (stderr, "calloc failed (%s)\n", strerror (errno));
		ret = -1;
		goto out;
	}

	for (i = 0; i < rdd_config.iters; i++) 
	{
		pthread_mutex_lock (&rdd_config.lock);
		{
			int bytes = 0;
			rand = random ();
			
			if (rdd_config.min_bs == rdd_config.max_bs) {
				bs = rdd_config.max_bs;
			} else {
				bs = rdd_config.min_bs + (rand % (rdd_config.max_bs - rdd_config.min_bs));
			}
			
			offset = rand % rdd_config.in_file.st.st_size;
			max_ops = rand % rdd_config.max_ops_per_seq;
			if (!max_ops) {
				max_ops ++;
			}

			ret = lseek (rdd_config.in_file.fd, offset, SEEK_SET);
			if (ret != offset) {
				fprintf (stderr, "lseek failed (%s)\n", strerror (errno));
				ret = -1;
				goto unlock;
			}

			ret = lseek (rdd_config.out_file.fd, offset, SEEK_SET);
			if (ret != offset) {
				fprintf (stderr, "lseek failed (%s)\n", strerror (errno));
				ret = -1;
				goto unlock;
			}

			while (max_ops--) 
			{ 
				bytes = read (rdd_config.in_file.fd, buf, bs);
				if (!bytes) {
					break;
				}

				if (bytes == -1) {
					fprintf (stderr, "read failed (%s)\n", strerror (errno));
					ret = -1;
					goto unlock;
				}

				if (write (rdd_config.out_file.fd, buf, bytes) != bytes) {
					fprintf (stderr, "write failed (%s)\n", strerror (errno));
					ret = -1;
					goto unlock;
				}
			}
		}
	unlock:
		pthread_mutex_unlock (&rdd_config.lock);
		if (ret == -1) {
			goto out;
		}
		ret = 0;
	}
out:
	free (buf);
	pthread_barrier_wait (&rdd_config.barrier);

	return NULL;
}


static int
rdd_spawn_threads (void)
{
	int i = 0, ret = -1, fd = -1;
	char buf[4096]; 

	fd = open (rdd_config.in_file.path, O_RDONLY);
	if (fd < 0) {
		fprintf (stderr, "cannot open %s (%s)\n", rdd_config.in_file.path, strerror (errno));
		ret = -1;
		goto out;
	}
	ret = fstat (fd, &rdd_config.in_file.st);
	if (ret != 0) {
		close (fd);
		fprintf (stderr, "cannot stat %s (%s)\n", rdd_config.in_file.path, strerror (errno));
		ret = -1;
		goto out;
	}
	rdd_config.in_file.fd = fd;

	fd = open (rdd_config.out_file.path, O_WRONLY | O_CREAT, S_IRWXU | S_IROTH);
	if (fd < 0) {
		close (rdd_config.in_file.fd);
		rdd_config.in_file.fd = -1;
		fprintf (stderr, "cannot open %s (%s)\n", rdd_config.out_file.path, strerror (errno));
		ret = -1;
		goto out;
	}
	rdd_config.out_file.fd = fd;

	while ((ret = read (rdd_config.in_file.fd, buf, 4096)) > 0) {
		if (write (rdd_config.out_file.fd, buf, ret) != ret) {
			fprintf (stderr, "write failed (%s)\n", strerror (errno));
			close (rdd_config.in_file.fd);
			close (rdd_config.out_file.fd);
			rdd_config.in_file.fd = rdd_config.out_file.fd = -1;
			ret = -1;
			goto out;
		}
	}

	rdd_config.threads = calloc (rdd_config.thread_count, sizeof (pthread_t));
	if (rdd_config.threads == NULL) {
		fprintf (stderr, "calloc() failed (%s)\n", strerror (errno));

		ret = -1;
		close (rdd_config.in_file.fd);
		close (rdd_config.out_file.fd);
		rdd_config.in_file.fd = rdd_config.out_file.fd = -1;
		goto out;
	}

	ret = pthread_barrier_init (&rdd_config.barrier, NULL, rdd_config.thread_count + 1);
	if (ret != 0) {
		fprintf (stderr, "pthread_barrier_init() failed (%s)\n", strerror (ret));

		free (rdd_config.threads);
		close (rdd_config.in_file.fd);
		close (rdd_config.out_file.fd);
		rdd_config.in_file.fd = rdd_config.out_file.fd = -1;
		ret = -1;
		goto out;
	}

	ret = pthread_mutex_init (&rdd_config.lock, NULL);
	if (ret != 0) {
		fprintf (stderr, "pthread_mutex_init() failed (%s)\n", strerror (ret));

		free (rdd_config.threads);
		pthread_barrier_destroy (&rdd_config.barrier);
		close (rdd_config.in_file.fd);
		close (rdd_config.out_file.fd);
		rdd_config.in_file.fd = rdd_config.out_file.fd = -1;
		ret = -1;
		goto out;
	}

	for (i = 0; i < rdd_config.thread_count; i++)
	{
		ret = pthread_create (&rdd_config.threads[i], NULL, rdd_read_write, NULL);
		if (ret != 0) {
			fprintf (stderr, "pthread_create failed (%s)\n", strerror (errno));
			exit (1);
		}
	}

out:
	return ret;
}


static void
rdd_wait_for_completion (void)
{
	pthread_barrier_wait (&rdd_config.barrier);
}


int 
main (int argc, char *argv[])
{
	int ret = -1;

	rdd_default_config ();

	ret = argp_parse (&argp, argc, argv, 0, 0, NULL);
	if (ret != 0) {
		ret = -1;
		fprintf (stderr, "%s: argp_parse() failed\n", argv[0]);
		goto err;
	}

	if (!rdd_valid_config ()) {
		ret = -1;
		fprintf (stderr, "%s: configuration validation failed\n", argv[0]);
		goto err;
	}

	ret = rdd_spawn_threads ();
	if (ret != 0) {
		fprintf (stderr, "%s: spawning threads failed\n", argv[0]);
		goto err;
	}

	rdd_wait_for_completion ();

err:
	return ret;
} 
