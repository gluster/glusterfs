#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>

int
main (int argc, char *argv[])
{
	int fds[argc-1];
	char buf[argc-1][4096];
	int i;
	int max_ret, ret;

	if (argc < 2) {
		printf ("Usage: %s file1 file2 ... >file\n", argv[0]);
		return 1;
	}

	for (i=0; i<argc-1; i++) {
		fds[i] = open (argv[i+1], O_RDONLY);
		if (fds[i] == -1) {
			perror (argv[i+1]);
			return 1;
		}
	}

	max_ret = 0;
	do {
		char newbuf[4096] = {0, };
		int j;

		max_ret = 0;
		for (i=0; i<argc-1; i++) {
			memset (buf[i], 0, 4096);
			ret = read (fds[i], buf[i], 4096); 
			if (ret > max_ret)
				max_ret = ret;
		}
		for (i=0; i<max_ret;i++)
			for (j=0; j<argc-1; j++)
				newbuf[i] |= buf[j][i];
		write (1, newbuf, max_ret);
	} while (max_ret);

	return 0;
}

