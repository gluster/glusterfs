/*
  Copyright (c) 2012 Red Hat, Inc. <http://www.redhat.com>
  This file is part of GlusterFS.

  This file is licensed to you under your choice of the GNU Lesser
  General Public License, version 3 or any later version (LGPLv3 or
  later), or the GNU General Public License, version 2 (GPLv2), in all
  cases as published by the Free Software Foundation.
*/

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#define _XOPEN_SOURCE 500

#include <ftw.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <dirent.h>
#include <stdlib.h>
#include <libgen.h>
#include <stdint.h>
#include <dirent.h>
#include <argp.h>

/*
 * FTW_ACTIONRETVAL is a GNU libc extension. It is used here to skip
 * hiearchies. On other systems we will still walk the tree, ignoring
 * entries.
 */
#ifndef FTW_ACTIONRETVAL
#define FTW_ACTIONRETVAL 0
#endif

int debug = 0;

typedef struct {
        char test_directory[4096];
        char **ignored_directory;
        unsigned int directories_ignored;
} arequal_config_t;

static arequal_config_t arequal_config;

static error_t
arequal_parse_opts (int key, char *arg, struct argp_state *_state);

static struct argp_option arequal_options[] = {
        { "ignore", 'i', "IGNORED", 0,
          "entry in the given path to be ignored"},
        { "path", 'p', "PATH", 0, "path where arequal has to be run"},
        {0, 0, 0, 0, 0}
};

#define DBG(fmt ...) do {                       \
                if (debug) {                    \
                        fprintf (stderr, "D "); \
                        fprintf (stderr, fmt);  \
                }                               \
        } while (0)

void
add_to_list (char *arg);
void
get_absolute_path (char directory[], char *arg);

static int roof(int a, int b)
{
        return ((((a)+(b)-1)/((b)?(b):1))*(b));
}

void
add_to_list (char *arg)
{
        char *string = NULL;
        int index = 0;

        index = arequal_config.directories_ignored - 1;
        string = strdup (arg);

        if (!arequal_config.ignored_directory) {
                arequal_config.ignored_directory = calloc (1, sizeof (char *));
        } else
                arequal_config.ignored_directory =
                        realloc (arequal_config.ignored_directory,
                                 sizeof (char *) * (index+1));

        arequal_config.ignored_directory[index] = string;
}

static error_t
arequal_parse_opts (int key, char *arg, struct argp_state *_state)
{
        switch (key) {
        case 'i':
        {
                arequal_config.directories_ignored++;
                add_to_list (arg);
        }
        break;
        case 'p':
        {
                if (arg[0] == '/')
                        strcpy (arequal_config.test_directory, arg);
                else
                        get_absolute_path (arequal_config.test_directory, arg);

                if (arequal_config.test_directory
                    [strlen(arequal_config.test_directory) - 1] == '/')
                        arequal_config.test_directory
                                [strlen(arequal_config.test_directory) - 1] = '\0';
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

void
get_absolute_path (char directory[], char *arg)
{
        char   cwd[4096] = {0,};

        if (getcwd (cwd, sizeof (cwd)) == NULL)
                printf ("some error in getting cwd\n");

        if (strcmp (arg, ".") != 0) {
                if (cwd[strlen(cwd)] != '/')
                        cwd[strlen (cwd)] = '/';
                strcat (cwd, arg);
        }
        strcpy (directory, cwd);
}

static struct argp argp = {
        arequal_options,
        arequal_parse_opts,
        "",
        "arequal - Tool which calculates the checksum of all the entries"
        "present in a given directory"
};

/* All this runs in single thread, hence using 'global' variables */

unsigned long long     avg_uid_file    = 0;
unsigned long long     avg_uid_dir     = 0;
unsigned long long     avg_uid_symlink = 0;
unsigned long long     avg_uid_other   = 0;

unsigned long long     avg_gid_file    = 0;
unsigned long long     avg_gid_dir     = 0;
unsigned long long     avg_gid_symlink = 0;
unsigned long long     avg_gid_other   = 0;

unsigned long long     avg_mode_file    = 0;
unsigned long long     avg_mode_dir     = 0;
unsigned long long     avg_mode_symlink = 0;
unsigned long long     avg_mode_other   = 0;

unsigned long long global_ctime_checksum = 0;


unsigned long long      count_dir        = 0;
unsigned long long      count_file       = 0;
unsigned long long      count_symlink    = 0;
unsigned long long      count_other      = 0;


unsigned long long      checksum_file1   = 0;
unsigned long long      checksum_file2   = 0;
unsigned long long      checksum_dir     = 0;
unsigned long long      checksum_symlink = 0;
unsigned long long      checksum_other   = 0;


unsigned long long
checksum_path (const char *path)
{
        unsigned long long   csum = 0;
        unsigned long long  *nums = 0;
        int                  len = 0;
        int                  cnt = 0;

        len = roof (strlen (path), sizeof (csum));
        cnt = len / sizeof (csum);

        nums = __builtin_alloca (len);
        memset (nums, 0, len);
        strcpy ((char *)nums, path);

        while (cnt) {
                csum ^= *nums;
                nums++;
                cnt--;
        }

        return csum;
}

int
checksum_md5 (const char *path, const struct stat *sb)
{
        uint64_t    this_data_checksum = 0;
        FILE       *filep              = NULL;
        char        *cmd               = NULL;
        char        strvalue[17]       = {0,};
        int         ret                = -1;
        int         len                = 0;
        const char  *pos               = NULL;
        char        *cpos              = NULL;

        /* Have to escape single-quotes in filename.
         * First, calculate the size of the buffer I'll need.
         */
        for (pos = path; *pos; pos++) {
                if ( *pos == '\'' )
                        len += 4;
                else
                        len += 1;
        }

        cmd = malloc(sizeof(char) * (len + 20));
        cmd[0] = '\0';

        /* Now, build the command with single quotes escaped. */

        cpos = cmd;
#if defined(linux)
        strcpy(cpos, "md5sum '");
        cpos += 8;
#elif defined(__NetBSD__)
        strcpy(cpos, "md5 -n '");
        cpos += 8;
#elif defined(__FreeBSD__) || defined(__APPLE__)
        strcpy(cpos, "md5 -q '");
        cpos += 8;
#else
#error "Please add system-specific md5 command"
#endif

        /* Add the file path, with every single quotes replaced with this sequence:
         * '\''
         */

        for (pos = path; *pos; pos++) {
                if ( *pos == '\'' ) {
                        strcpy(cpos, "'\\''");
                        cpos += 4;
                } else {
                        *cpos = *pos;
                        cpos++;
                }
        }

        /* Add on the trailing single-quote and null-terminate. */
        strcpy(cpos, "'");

        filep = popen (cmd, "r");
        if (!filep) {
                perror (path);
                goto out;
        }

        if (fread (strvalue, sizeof (char), 16, filep) != 16) {
                fprintf (stderr, "%s: short read\n", path);
                goto out;
        }

        this_data_checksum = strtoull (strvalue, NULL, 16);
        if (-1 == this_data_checksum) {
                fprintf (stderr, "%s: %s\n", strvalue, strerror (errno));
                goto out;
        }
        checksum_file1 ^= this_data_checksum;

        if (fread (strvalue, sizeof (char), 16, filep) != 16) {
                fprintf (stderr, "%s: short read\n", path);
                goto out;
        }

        this_data_checksum = strtoull (strvalue, NULL, 16);
        if (-1 == this_data_checksum) {
                fprintf (stderr, "%s: %s\n", strvalue, strerror (errno));
                goto out;
        }
        checksum_file2 ^= this_data_checksum;

        ret = 0;
out:
        if (filep)
                pclose (filep);

        if (cmd)
                free(cmd);

        return ret;
}

int
checksum_filenames (const char *path, const struct stat *sb)
{
        DIR                *dirp = NULL;
        struct dirent      *entry = NULL;
        unsigned long long  csum = 0;
        int                 i = 0;
        int                 found = 0;

        dirp = opendir (path);
        if (!dirp) {
                perror (path);
                goto out;
        }

        errno = 0;
        while ((entry = readdir (dirp))) {
                /* do not calculate the checksum of the entries which user has
                   told to ignore and proceed to other siblings.*/
                if (arequal_config.ignored_directory) {
                        for (i = 0;i < arequal_config.directories_ignored;i++) {
                                if ((strcmp (entry->d_name,
                                             arequal_config.ignored_directory[i])
                                     == 0)) {
                                        found = 1;
                                        DBG ("ignoring the entry %s\n",
                                             entry->d_name);
                                        break;
                                }
                        }
                        if (found == 1) {
                                found = 0;
                                continue;
                        }
                }
                csum = checksum_path (entry->d_name);
                checksum_dir ^= csum;
        }

        if (errno) {
                perror (path);
                goto out;
        }

out:
        if (dirp)
                closedir (dirp);

        return 0;
}


int
process_file (const char *path, const struct stat *sb)
{
        int    ret = 0;

        count_file++;

        avg_uid_file ^= sb->st_uid;
        avg_gid_file ^= sb->st_gid;
        avg_mode_file ^= sb->st_mode;

        ret = checksum_md5 (path, sb);

        return ret;
}


int
process_dir (const char *path, const struct stat *sb)
{
        unsigned long long csum = 0;

        count_dir++;

        avg_uid_dir ^= sb->st_uid;
        avg_gid_dir ^= sb->st_gid;
        avg_mode_dir ^= sb->st_mode;

        csum = checksum_filenames (path, sb);

        checksum_dir ^= csum;

        return 0;
}


int
process_symlink (const char *path, const struct stat *sb)
{
        int                ret = 0;
        char               buf[4096] = {0, };
        unsigned long long csum = 0;

        count_symlink++;

        avg_uid_symlink ^= sb->st_uid;
        avg_gid_symlink ^= sb->st_gid;
        avg_mode_symlink ^= sb->st_mode;

        ret = readlink (path, buf, 4096);
        if (ret < 0) {
                perror (path);
                goto out;
        }

        DBG ("readlink (%s) => %s\n", path, buf);

        csum = checksum_path (buf);

        DBG ("checksum_path (%s) => %llx\n", buf, csum);

        checksum_symlink ^= csum;

        ret = 0;
out:
        return ret;
}


int
process_other (const char *path, const struct stat *sb)
{
        count_other++;

        avg_uid_other ^= sb->st_uid;
        avg_gid_other ^= sb->st_gid;
        avg_mode_other ^= sb->st_mode;

        checksum_other ^= sb->st_rdev;

        return 0;
}

static int
ignore_entry(const char *bname, const char *dname)
{
        int i;

	for (i  = 0; i < arequal_config.directories_ignored; i++) {
		if (strcmp(bname, arequal_config.ignored_directory[i]) == 0 &&
                    strncmp(arequal_config.test_directory, dname,
                            strlen(arequal_config.test_directory)) == 0)
                        return 1;
        }

        return 0;
}

int
process_entry (const char *path, const struct stat *sb,
               int typeflag, struct FTW *ftwbuf)
{
        int ret = 0;
        char *name = NULL;
        char *bname = NULL;
        char *dname = NULL;
        int  i = 0;

        /* The if condition below helps in ignoring some directories in
           the given path. If the name of the entry is one of the directory
           names that the user told to ignore, then that directory will not
           be processed and will return FTW_SKIP_SUBTREE to nftw which will
           not crawl this directory and move on to other siblings.
           Note that for nftw to recognize FTW_SKIP_TREE, FTW_ACTIONRETVAL
           should be passed as an argument to nftw.

           This mainly helps in calculating the checksum of network filesystems
           (client-server), where the server might have some hidden directories
           for managing the filesystem. So to calculate the sanity of filesytem
           one has to get the checksum of the client and then the export directory
           of server by telling arequal to ignore some of the directories which
           are not part of the namespace.
        */

        if (arequal_config.ignored_directory) {
#ifndef FTW_SKIP_SUBTREE
                char *cp;

                name = strdup (path);
                dname = dirname (name);

                for (cp = strtok(name, "/"); cp; cp = strtok(NULL, "/")) {
                        if (ignore_entry(cp, dname)) {
                                DBG ("ignoring %s\n", path);
                                if (name)
                                        free (name);
                                return 0;
                        }
                }
#else /* FTW_SKIP_SUBTREE */
                name = strdup (path);

                name[strlen(name)] = '\0';

                bname = strrchr (name, '/');
                if (bname)
                        bname++;

                dname = dirname (name);
                if (ignore_entry(bname, dname)) {
                                DBG ("ignoring %s\n", bname);
                                ret = FTW_SKIP_SUBTREE;
                                if (name)
                                        free (name);
                                return ret;
                }
#endif /* FTW_SKIP_SUBTREE */
        }

        DBG ("processing entry %s\n", path);

        switch ((S_IFMT & sb->st_mode)) {
        case S_IFDIR:
                ret = process_dir (path, sb);
                break;
        case S_IFREG:
                ret = process_file (path, sb);
                break;
        case S_IFLNK:
                ret = process_symlink (path, sb);
                break;
        default:
                ret = process_other (path, sb);
                break;
        }

        if (name)
                free (name);
        return ret;
}


int
display_counts (FILE *fp)
{
        fprintf (fp, "\n");
        fprintf (fp, "Entry counts\n");
        fprintf (fp, "Regular files   : %lld\n", count_file);
        fprintf (fp, "Directories     : %lld\n", count_dir);
        fprintf (fp, "Symbolic links  : %lld\n", count_symlink);
        fprintf (fp, "Other           : %lld\n", count_other);
        fprintf (fp, "Total           : %lld\n",
                 (count_file + count_dir + count_symlink + count_other));

        return 0;
}


int
display_checksums (FILE *fp)
{
        fprintf (fp, "\n");
        fprintf (fp, "Checksums\n");
        fprintf (fp, "Regular files   : %llx%llx\n", checksum_file1, checksum_file2);
        fprintf (fp, "Directories     : %llx\n", checksum_dir);
        fprintf (fp, "Symbolic links  : %llx\n", checksum_symlink);
        fprintf (fp, "Other           : %llx\n", checksum_other);
        fprintf (fp, "Total           : %llx\n",
                 (checksum_file1 ^ checksum_file2 ^ checksum_dir ^ checksum_symlink ^ checksum_other));

        return 0;
}


int
display_metadata (FILE *fp)
{
        fprintf (fp, "\n");
        fprintf (fp, "Metadata checksums\n");
        fprintf (fp, "Regular files   : %llx\n",
                 (avg_uid_file + 13) * (avg_gid_file + 11) * (avg_mode_file + 7));
        fprintf (fp, "Directories     : %llx\n",
                 (avg_uid_dir + 13) * (avg_gid_dir + 11) * (avg_mode_dir + 7));
        fprintf (fp, "Symbolic links  : %llx\n",
                 (avg_uid_symlink + 13) * (avg_gid_symlink + 11) * (avg_mode_symlink + 7));
        fprintf (fp, "Other           : %llx\n",
                 (avg_uid_other + 13) * (avg_gid_other + 11) * (avg_mode_other + 7));

        return 0;
}

int
display_stats (FILE *fp)
{
        display_counts (fp);

        display_metadata (fp);

        display_checksums (fp);

        return 0;
}


int
main(int argc, char *argv[])
{
        int  ret = 0;
        int  i = 0;

        ret = argp_parse (&argp, argc, argv, 0, 0, NULL);
        if (ret != 0) {
                fprintf (stderr, "parsing arguments failed\n");
                return -2;
        }

         /* Use FTW_ACTIONRETVAL to take decision on what to do depending upon */
         /* the return value of the callback function */
         /* (process_entry in this case) */
        ret = nftw (arequal_config.test_directory, process_entry, 30,
                    FTW_ACTIONRETVAL|FTW_PHYS|FTW_MOUNT);
        if (ret != 0) {
                fprintf (stderr, "ftw (%s) returned %d (%s), terminating\n",
                         argv[1], ret, strerror (errno));
                return 1;
        }

        display_stats (stdout);

        if (arequal_config.ignored_directory) {
                for (i = 0; i < arequal_config.directories_ignored; i++) {
                        if (arequal_config.ignored_directory[i])
                            free (arequal_config.ignored_directory[i]);
                }
                free (arequal_config.ignored_directory);
        }

        return 0;
}
