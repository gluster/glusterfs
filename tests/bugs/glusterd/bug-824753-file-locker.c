#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <sys/types.h>
#include <unistd.h>
#include <stdlib.h>


int main (int argc, char *argv[])
{
    int              fd            = -1;
    int              ret           = -1;
    char             command[2048] = "";
    char             filepath[255]     = "";
    struct flock fl;

    fl.l_type = F_WRLCK;
    fl.l_whence = SEEK_SET;
    fl.l_start = 7;
    fl.l_len = 1;
    fl.l_pid = getpid();

    snprintf(filepath, 255, "%s/%s", argv[4], argv[5]);

    fd = open(filepath, O_RDWR);

    if (fd == -1)
        return -1;

    if (fcntl(fd, F_SETLKW, &fl) == -1) {
        return -1;
    }

    snprintf(command, sizeof(command),
             "gluster volume clear-locks %s /%s kind all posix 0,7-1 |"
             " grep %s | awk -F'..: ' '{print $1}' | grep %s:%s/%s",
             argv[1], argv[5], argv[2], argv[2], argv[3], argv[1]);

    ret = system (command);
    close(fd);

    if (ret)
        return -1;
    else
        return 0;
}
