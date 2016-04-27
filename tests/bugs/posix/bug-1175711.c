#include <stdio.h>
#include <dirent.h>
#include <string.h>
#include <assert.h>

int
main(int argc, char **argv)
{
        DIR *dir = NULL;
        struct dirent *entry = NULL;
        int ret = 0;
        char *path = NULL;

        assert (argc == 2);
        path = argv[1];

        dir = opendir(path);
        if (!dir) {
                printf("opendir(%s) failed.\n", path);
                return -1;
        }

#ifdef _DIRENT_HAVE_D_TYPE
        while ((entry = readdir(dir)) != NULL) {
                if (entry->d_type == DT_UNKNOWN) {
                        printf("d_type found to be DT_UNKNOWN\n");
                        ret = -1;
                        break;
                }
        }
#endif

        if (dir)
                closedir(dir);

        return ret;
}
