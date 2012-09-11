/*
   Copyright (c) 2008-2012 Red Hat, Inc. <http://www.redhat.com>
   This file is part of GlusterFS.

   This file is licensed to you under your choice of the GNU Lesser
   General Public License, version 3 or any later version (LGPLv3 or
   later), or the GNU General Public License, version 2 (GPLv2), in all
   cases as published by the Free Software Foundation.
*/
#define _GNU_SOURCE
#define __USE_FILE_OFFSET64
#define _FILE_OFFSET_BITS 64

#include <stdio.h>
#include <argp.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/xattr.h>
#include <string.h>
#include <libgen.h>
#include <errno.h>
#include <sys/time.h>

struct state {
        char need_op_write:1;
        char need_op_read:1;

        char need_iface_fileio:1;
        char need_iface_xattr:1;

        char need_mode_posix:1;

        char prefix[512];
        long int count;

        size_t block_size;

        char *specfile;

        long int io_size;
};


#define MEASURE(func, arg) measure (func, #func, arg)


void
tv_difference (struct timeval *tv_stop,
               struct timeval *tv_start,
               struct timeval *tv_diff)
{
        if (tv_stop->tv_usec < tv_start->tv_usec) {
                tv_diff->tv_usec = (tv_stop->tv_usec + 1000000) - tv_start->tv_usec;
                tv_diff->tv_sec = (tv_stop->tv_sec - 1 - tv_start->tv_sec);
        } else {
                tv_diff->tv_usec = tv_stop->tv_usec - tv_start->tv_usec;
                tv_diff->tv_sec = tv_stop->tv_sec - tv_start->tv_sec;
        }
}


void
measure (int (*func)(struct state *state),
         char *func_name, struct state *state)
{
        struct timeval tv_start, tv_stop, tv_diff;
        state->io_size = 0;
        long int count;

        gettimeofday (&tv_start, NULL);
        count = func (state);
        gettimeofday (&tv_stop, NULL);

        tv_difference (&tv_stop, &tv_start, &tv_diff);

        fprintf (stdout, "%s: count=%ld, size=%ld, time=%ld:%ld\n",
                 func_name, count, state->io_size,
                 tv_diff.tv_sec, tv_diff.tv_usec);
}


static error_t
parse_opts (int key, char *arg,
            struct argp_state *_state)
{
        struct state *state = _state->input;

        switch (key)
        {
        case 'o':
                if (strcasecmp (arg, "read") == 0) {
                        state->need_op_write = 0;
                        state->need_op_read = 1;
                } else if (strcasecmp (arg, "write") == 0) {
                        state->need_op_write = 1;
                        state->need_op_read = 0;
                } else if (strcasecmp (arg, "both") == 0) {
                        state->need_op_write = 1;
                        state->need_op_read = 1;
                } else {
                        fprintf (stderr, "unknown op: %s\n", arg);
                        return -1;
                }
                break;
        case 'i':
                if (strcasecmp (arg, "fileio") == 0) {
                        state->need_iface_fileio = 1;
                        state->need_iface_xattr = 0;
                } else if (strcasecmp (arg, "xattr") == 0) {
                        state->need_iface_fileio = 0;
                        state->need_iface_xattr = 1;
                } else if (strcasecmp (arg, "both") == 0) {
                        state->need_iface_fileio = 1;
                        state->need_iface_xattr = 1;
                } else {
                        fprintf (stderr, "unknown interface: %s\n", arg);
                        return -1;
                }
                break;
        case 'b':
        {
                size_t block_size = atoi (arg);
                if (!block_size) {
                        fprintf (stderr, "incorrect size: %s\n", arg);
                        return -1;
                }
                state->block_size = block_size;
        }
        break;
        case 's':
                state->specfile = strdup (arg);
                break;
        case 'p':
                fprintf (stderr, "using prefix: %s\n", arg);
                strncpy (state->prefix, arg, 512);
                break;
        case 'c':
        {
                long count = atol (arg);
                if (!count) {
                        fprintf (stderr, "incorrect count: %s\n", arg);
                        return -1;
                }
                state->count = count;
        }
        break;
        case ARGP_KEY_NO_ARGS:
                break;
        case ARGP_KEY_ARG:
                break;
        }

        return 0;
}

int
do_mode_posix_iface_fileio_write (struct state *state)
{
        long int i;
        int ret = -1;
        char block[state->block_size];

        for (i=0; i<state->count; i++) {
                int fd = -1;
                char filename[512];

                sprintf (filename, "%s.%06ld", state->prefix, i);

                fd = open (filename, O_CREAT|O_WRONLY, 00600);
                if (fd == -1) {
                        fprintf (stderr, "open(%s) => %s\n", filename, strerror (errno));
                        break;
                }
                ret = write (fd, block, state->block_size);
                if (ret != state->block_size) {
                        fprintf (stderr, "write (%s) => %d/%s\n", filename, ret,
                                 strerror (errno));
                        close (fd);
                        break;
                }
                close (fd);
                state->io_size += ret;
        }

        return i;
}


int
do_mode_posix_iface_fileio_read (struct state *state)
{
        long int i;
        int ret = -1;
        char block[state->block_size];

        for (i=0; i<state->count; i++) {
                int fd = -1;
                char filename[512];

                sprintf (filename, "%s.%06ld", state->prefix, i);

                fd = open (filename, O_RDONLY);
                if (fd == -1) {
                        fprintf (stderr, "open(%s) => %s\n", filename, strerror (errno));
                        break;
                }
                ret = read (fd, block, state->block_size);
                if (ret == -1) {
                        fprintf (stderr, "read(%s) => %d/%s\n", filename, ret, strerror (errno));
                        close (fd);
                        break;
                }
                close (fd);
                state->io_size += ret;
        }

        return i;
}


int
do_mode_posix_iface_fileio (struct state *state)
{
        if (state->need_op_write)
                MEASURE (do_mode_posix_iface_fileio_write, state);

        if (state->need_op_read)
                MEASURE (do_mode_posix_iface_fileio_read, state);

        return 0;
}


int
do_mode_posix_iface_xattr_write (struct state *state)
{
        long int i;
        int ret = -1;
        char block[state->block_size];
        char *dname = NULL, *dirc = NULL;
        char *bname = NULL, *basec = NULL;

        dirc = strdup (state->prefix);
        basec = strdup (state->prefix);
        dname = dirname (dirc);
        bname = basename (basec);

        for (i=0; i<state->count; i++) {
                char key[512];

                sprintf (key, "glusterfs.file.%s.%06ld", bname, i);

                ret = lsetxattr (dname, key, block, state->block_size, 0);

                if (ret != 0) {
                        fprintf (stderr, "lsetxattr (%s, %s, %p) => %s\n",
                                 dname, key, block, strerror (errno));
                        break;
                }
                state->io_size += state->block_size;
        }

        free (dirc);
        free (basec);

        return i;
}


int
do_mode_posix_iface_xattr_read (struct state *state)
{
        long int i;
        int ret = -1;
        char block[state->block_size];
        char *dname = NULL, *dirc = NULL;
        char *bname = NULL, *basec = NULL;

        dirc = strdup (state->prefix);
        basec = strdup (state->prefix);
        dname = dirname (dirc);
        bname = basename (basec);

        for (i=0; i<state->count; i++) {
                char key[512];

                sprintf (key, "glusterfs.file.%s.%06ld", bname, i);

                ret = lgetxattr (dname, key, block, state->block_size);

                if (ret < 0) {
                        fprintf (stderr, "lgetxattr (%s, %s, %p) => %s\n",
                                 dname, key, block, strerror (errno));
                        break;
                }
                state->io_size += ret;
        }

        return i;
}


int
do_mode_posix_iface_xattr (struct state *state)
{
        if (state->need_op_write)
                MEASURE (do_mode_posix_iface_xattr_write, state);

        if (state->need_op_read)
                MEASURE (do_mode_posix_iface_xattr_read, state);

        return 0;
}

int
do_mode_posix (struct state *state)
{
        if (state->need_iface_fileio)
                do_mode_posix_iface_fileio (state);

        if (state->need_iface_xattr)
                do_mode_posix_iface_xattr (state);

        return 0;
}


int
do_actions (struct state *state)
{
        if (state->need_mode_posix)
                do_mode_posix (state);

        return 0;
}

static struct argp_option options[] = {
        {"op", 'o', "OPERATIONS", 0,
         "WRITE|READ|BOTH - defaults to BOTH"},
        {"iface", 'i', "INTERFACE", 0,
         "FILEIO|XATTR|BOTH - defaults to FILEIO"},
        {"block", 'b', "BLOCKSIZE", 0,
         "<NUM> - defaults to 4096"},
        {"specfile", 's', "SPECFILE", 0,
         "absolute path to specfile"},
        {"prefix", 'p', "PREFIX", 0,
         "filename prefix"},
        {"count", 'c', "COUNT", 0,
         "number of files"},
        {0, 0, 0, 0, 0}
};

static struct argp argp = {
        options,
        parse_opts,
        "tool",
        "tool to benchmark small file performance"
};

int
main (int argc, char *argv[])
{
        struct state state = {0, };

        state.need_op_write = 1;
        state.need_op_read = 1;

        state.need_iface_fileio = 1;
        state.need_iface_xattr = 0;

        state.need_mode_posix = 1;

        state.block_size = 4096;

        strcpy (state.prefix, "tmpfile");
        state.count = 1048576;

        if (argp_parse (&argp, argc, argv, 0, 0, &state) != 0) {
                fprintf (stderr, "argp_parse() failed\n");
                return 1;
        }

        do_actions (&state);

        return 0;
}
