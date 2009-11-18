/* Utilities for reading/writing fstab, mtab, etc.
   Copyright (C) 1995-2000, 2001, 2002, 2003, 2006
   Free Software Foundation, Inc.
   This file is part of the GNU C Library.

   The GNU C Library is free software; you can redistribute it and/or
   modify it under the terms of the GNU Lesser General Public
   License as published by the Free Software Foundation; either
   version 2.1 of the License, or (at your option) any later version.

   The GNU C Library is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   Lesser General Public License for more details.

   You should have received a copy of the GNU Lesser General Public
   License along with the GNU C Library; if not, write to the Free
   Software Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA
   02111-1307 USA.  */

#include <alloca.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include "booster_fstab.h"
#include <stdlib.h>
#include <libglusterfsclient.h>
#include <errno.h>

/* The default timeout for inode and stat cache. */
#define BOOSTER_DEFAULT_ATTR_TIMEO      5 /* In Secs */

/* Prepare to begin reading and/or writing mount table entries from the
   beginning of FILE.  MODE is as for `fopen'.  */
glusterfs_fstab_t *
glusterfs_fstab_init (const char *file, const char *mode)
{
        glusterfs_fstab_t *handle = NULL;
        handle = calloc (1, sizeof (glusterfs_fstab_t));
        if (!handle) {
                gf_log ("booster-fstab", GF_LOG_ERROR, "Memory allocation"
                        " failed");
                goto out;
        }

        gf_log ("booster-fstab", GF_LOG_DEBUG, "FSTAB file: %s", file);
        FILE *result = fopen (file,mode);
        if (result != NULL) {
                handle->fp = result;
        } else {
                gf_log ("booster-fstab", GF_LOG_ERROR, "FSTAB file open failed:"
                        " %s", strerror (errno));
                free (handle);
                handle = NULL;
        }

out:

        return handle;
}

int
glusterfs_fstab_close (glusterfs_fstab_t *h)
{
        if (!h)
                return -1;

        if (h->fp)
                fclose (h->fp);

        return 0;
}

/* Since the values in a line are separated by spaces, a name cannot
   contain a space.  Therefore some programs encode spaces in names
   by the strings "\040".  We undo the encoding when reading an entry.
   The decoding happens in place.  */
static char *
decode_name (char *buf)
{
        char *rp = buf;
        char *wp = buf;

        do
                if (rp[0] == '\\' && rp[1] == '0' && rp[2] == '4'
                                && rp[3] == '0')
                {
                        /* \040 is a SPACE.  */
                        *wp++ = ' ';
                        rp += 3;
                }
                else if (rp[0] == '\\' && rp[1] == '0' && rp[2] == '1'
                                && rp[3] == '1')
                {
                        /* \011 is a TAB.  */
                        *wp++ = '\t';
                        rp += 3;
                }
                else if (rp[0] == '\\' && rp[1] == '0' && rp[2] == '1'
                                && rp[3] == '2')
                {
                        /* \012 is a NEWLINE.  */
                        *wp++ = '\n';
                        rp += 3;
                }
                else if (rp[0] == '\\' && rp[1] == '\\')
                {
                        /* We have to escape \\ to be able to represent all
                         * characters.  */
                        *wp++ = '\\';
                        rp += 1;
                }
                else if (rp[0] == '\\' && rp[1] == '1' && rp[2] == '3'
                                && rp[3] == '4')
                {
                        /* \134 is also \\.  */
                        *wp++ = '\\';
                        rp += 3;
                }
                else
                        *wp++ = *rp;
        while (*rp++ != '\0');

        return buf;
}


/* Read one mount table entry from STREAM.  Returns a pointer to storage
   reused on the next call, or null for EOF or error (use feof/ferror to
   check).  */
struct glusterfs_mntent *
__glusterfs_fstab_getent (FILE *stream, struct glusterfs_mntent *mp,
                          char *buffer, int bufsiz)
{
        char *cp;
        char *head;

        do
        {
                char *end_ptr;

                if (fgets (buffer, bufsiz, stream) == NULL)
                {
                        return NULL;
                }

                end_ptr = strchr (buffer, '\n');
                if (end_ptr != NULL)	/* chop newline */
                        *end_ptr = '\0';
                else
                {
                        /* Not the whole line was read.  Do it now but forget
                         * it.  */
                        char tmp[1024];
                        while (fgets (tmp, sizeof tmp, stream) != NULL)
                                if (strchr (tmp, '\n') != NULL)
                                        break;
                }

                head = buffer + strspn (buffer, " \t");
                /* skip empty lines and comment lines:  */
        }
        while (head[0] == '\0' || head[0] == '#');

        cp = strsep (&head, " \t");
        mp->mnt_fsname = cp != NULL ? decode_name (cp) : (char *) "";
        if (head)
                head += strspn (head, " \t");
        cp = strsep (&head, " \t");
        mp->mnt_dir = cp != NULL ? decode_name (cp) : (char *) "";
        if (head)
                head += strspn (head, " \t");
        cp = strsep (&head, " \t");
        mp->mnt_type = cp != NULL ? decode_name (cp) : (char *) "";
        if (head)
                head += strspn (head, " \t");
        cp = strsep (&head, " \t");
        mp->mnt_opts = cp != NULL ? decode_name (cp) : (char *) "";
        switch (head ? sscanf (head, " %d %d ", &mp->mnt_freq,
                               &mp->mnt_passno) : 0)
        {
                case 0:
                        mp->mnt_freq = 0;
                case 1:
                        mp->mnt_passno = 0;
                case 2:
                        break;
        }

        return mp;
}

struct glusterfs_mntent *
glusterfs_fstab_getent (glusterfs_fstab_t *h)
{
        if (!h)
                return NULL;

        if (!h->fp)
                return NULL;

        return __glusterfs_fstab_getent (h->fp, &h->tmpent, h->buf,
                                         GF_MNTENT_BUFSIZE);
}

/* We have to use an encoding for names if they contain spaces or tabs.
   To be able to represent all characters we also have to escape the
   backslash itself.  This "function" must be a macro since we use
   `alloca'.  */
#define encode_name(name)                                               \
        do {                                                            \
                const char *rp = name;		                        \
                                                                        \
                while (*rp != '\0')     	                        \
                        if (*rp == ' ' || *rp == '\t' || *rp == '\\')   \
                                break;                                  \
                        else	                                        \
                                ++rp;                                   \
                                                                        \
                if (*rp != '\0')                                        \
                {                                               \
                /* In the worst case the length of the string   \
                 * can increase to four times the current       \
                 * length.  */				        \
                        char *wp;				\
                                                                \
                        rp = name;				\
                        name = wp = (char *) alloca (strlen (name) * 4 + 1);                                                                 \
                                                                \
                        do {                            \
                                if (*rp == ' ')		\
                                {       		\
                                        *wp++ = '\\';   \
                                        *wp++ = '0';	\
                                        *wp++ = '4';	\
                                        *wp++ = '0';    \
                                }			\
                                else if (*rp == '\t')	\
                                {			\
                                        *wp++ = '\\';	\
                                        *wp++ = '0';	\
                                        *wp++ = '1';	\
                                        *wp++ = '1';	\
                                }			\
                                else if (*rp == '\n')	\
                                {	                \
                                        *wp++ = '\\';	\
                                        *wp++ = '0';	\
                                        *wp++ = '1';	\
                                        *wp++ = '2';	\
                                }	                \
                                else if (*rp == '\\')	\
                                {                       \
                                        *wp++ = '\\';	\
                                        *wp++ = '\\';	\
                                }                       \
                                else	                \
                                        *wp++ = *rp;	\
                        } while (*rp++ != '\0');	\
                }                                       \
        } while (0)                                     \


int
glusterfs_fstab_addent (glusterfs_fstab_t *h,
                const struct glusterfs_mntent *mnt)
{
        struct glusterfs_mntent mntcopy = *mnt;
        if (!h)
                return -1;

        if (!h->fp)
                return -1;

        if (fseek (h->fp, 0, SEEK_END))
                return -1;

        /* Encode spaces and tabs in the names.  */
        encode_name (mntcopy.mnt_fsname);
        encode_name (mntcopy.mnt_dir);
        encode_name (mntcopy.mnt_type);
        encode_name (mntcopy.mnt_opts);

        return (fprintf (h->fp, "%s %s %s %s %d %d\n",
                                mntcopy.mnt_fsname,
                                mntcopy.mnt_dir,
                                mntcopy.mnt_type,
                                mntcopy.mnt_opts,
                                mntcopy.mnt_freq,
                                mntcopy.mnt_passno)
                        < 0 ? 1 : 0);
}


/* Search MNT->mnt_opts for an option matching OPT.
   Returns the address of the substring, or null if none found.  */
char *
glusterfs_fstab_hasoption (const struct glusterfs_mntent *mnt, const char *opt)
{
        const size_t optlen = strlen (opt);
        char *rest = mnt->mnt_opts, *p;

        while ((p = strstr (rest, opt)) != NULL)
        {
                if ((p == rest || p[-1] == ',')
                                && (p[optlen] == '\0' || p[optlen] == '=' || p[optlen] == ','))
                        return p;

                rest = strchr (p, ',');
                if (rest == NULL)
                        break;
                ++rest;
        }

        return NULL;
}

void
clean_init_params (glusterfs_init_params_t *ipars)
{
        if (!ipars)
                return;

        if (ipars->volume_name)
                free (ipars->volume_name);

        if (ipars->specfile)
                free (ipars->specfile);

        if (ipars->logfile)
                free (ipars->logfile);

        if (ipars->loglevel)
                free (ipars->loglevel);

        return;
}

char *
get_option_value (char *opt)
{
        char *val = NULL;
        char *saveptr = NULL;
        char *copy_opt = NULL;
        char *retval = NULL;

        copy_opt = strdup (opt);

        /* Get the = before the value of the option. */
        val = index (copy_opt, '=');
        if (val) {
                /* Move to start of option */
                ++val;

                /* Now, to create a '\0' delimited string out of the
                 * options string, first get the position where the
                 * next option starts, that would be the next ','.
                 */
                saveptr = index (val, ',');
                if (saveptr)
                        *saveptr = '\0';
                retval = strdup (val);
        }

        free (copy_opt);

        return retval;
}

void
booster_mount (struct glusterfs_mntent *ent)
{
        char                    *opt = NULL;
        glusterfs_init_params_t ipars;
        time_t                  timeout = BOOSTER_DEFAULT_ATTR_TIMEO;
        char                    *timeostr = NULL;
        char                    *endptr = NULL;

        if (!ent)
                return;

        gf_log ("booster-fstab", GF_LOG_DEBUG, "Mount entry: volfile: %s,"
                " VMP: %s, Type: %s, Options: %s", ent->mnt_fsname,
                ent->mnt_dir, ent->mnt_type, ent->mnt_opts);
        if ((strcmp (ent->mnt_type, "glusterfs") != 0)) {
                gf_log ("booster-fstab", GF_LOG_ERROR, "Type is not glusterfs");
                return;
        }

        memset (&ipars, 0, sizeof (glusterfs_init_params_t));
        if (ent->mnt_fsname)
                ipars.specfile = strdup (ent->mnt_fsname);

        opt = glusterfs_fstab_hasoption (ent, "subvolume");
        if (opt)
                ipars.volume_name = get_option_value (opt);

        opt = glusterfs_fstab_hasoption (ent, "log-file");
        if (!opt)
                opt = glusterfs_fstab_hasoption (ent, "logfile");

        if (opt)
                ipars.logfile = get_option_value (opt);

        opt = glusterfs_fstab_hasoption (ent, "log-level");
        if (!opt)
                opt = glusterfs_fstab_hasoption (ent, "loglevel");

        if (opt)
                ipars.loglevel = get_option_value (opt);

        /* Attribute cache timeout */
        opt = glusterfs_fstab_hasoption (ent, "attr_timeout");
        if (opt) {
                 timeostr = get_option_value (opt);
                 if (timeostr)
                         timeout = strtol (timeostr, &endptr, 10);
        }

        ipars.lookup_timeout = timeout;
        ipars.stat_timeout = timeout;

        if ((glusterfs_mount (ent->mnt_dir, &ipars)) == -1)
                gf_log ("booster-fstab", GF_LOG_ERROR, "VMP mounting failed");

        clean_init_params (&ipars);
}

int
booster_configure (char *confpath)
{
        int                     ret = -1;
        glusterfs_fstab_t       *handle = NULL;
        struct glusterfs_mntent *ent = NULL;

        if (!confpath)
                goto out;

        handle = glusterfs_fstab_init (confpath, "r");
        if (!handle)
                goto out;

        while ((ent = glusterfs_fstab_getent (handle)) != NULL)
                booster_mount (ent);

        glusterfs_fstab_close (handle);
        ret = 0;
out:
        return ret;
}


