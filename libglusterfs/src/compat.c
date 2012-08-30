/*
  Copyright (c) 2008-2012 Red Hat, Inc. <http://www.redhat.com>
  This file is part of GlusterFS.

  This file is licensed to you under your choice of the GNU Lesser
  General Public License, version 3 or any later version (LGPLv3 or
  later), or the GNU General Public License, version 2 (GPLv2), in all
  cases as published by the Free Software Foundation.
*/

#ifndef _CONFIG_H
#define _CONFIG_H
#include "config.h"
#endif

#include <string.h>
#include <stdlib.h>
#include <stdarg.h>
#include <getopt.h>
#include <sys/types.h>
#include <dirent.h>

#ifdef GF_SOLARIS_HOST_OS
#include "logging.h"
#endif /* GF_SOLARIS_HOST_OS */

#include "compat.h"
#include "common-utils.h"
#include "iatt.h"
#include "inode.h"

#ifdef GF_SOLARIS_HOST_OS
int
solaris_fsetxattr(int fd, const char* key, const char *value, size_t size,
                  int flags)
{
        int attrfd = -1;
        int ret = 0;

        attrfd = openat (fd, key, flags|O_CREAT|O_WRONLY|O_XATTR, 0777);
        if (attrfd >= 0) {
                ftruncate (attrfd, 0);
                ret = write (attrfd, value, size);
                close (attrfd);
        } else {
                if (errno != ENOENT)
                        gf_log ("libglusterfs", GF_LOG_ERROR,
                                "Couldn't set extended attribute for %d (%d)",
                                fd, errno);
                return -1;
        }

        return 0;
}


int
solaris_fgetxattr(int fd, const char* key, char *value, size_t size)
{
        int attrfd = -1;
        int ret = 0;

        attrfd = openat (fd, key, O_RDONLY|O_XATTR);
        if (attrfd >= 0) {
                if (size == 0) {
                        struct stat buf;
                        fstat (attrfd, &buf);
                        ret = buf.st_size;
                } else {
                        ret = read (attrfd, value, size);
                }
                close (attrfd);
        } else {
                if (errno != ENOENT)
                        gf_log ("libglusterfs", GF_LOG_INFO,
                                "Couldn't read extended attribute for the file %d (%d)",
                                fd, errno);
                if (errno == ENOENT)
                        errno = ENODATA;
                return -1;
        }

        return ret;
}

/* Solaris does not support xattr for symlinks and dev files. Since gfid and
   other trusted attributes are stored as xattrs, we need to provide support for
   them. A mapped regular file is stored in the /.glusterfs_xattr_inode of the export dir.
   All xattr ops related to the special files are redirected to this map file.
*/

int
make_export_path (const char *real_path, char **path)
{
        int     ret = -1;
        char   *tmp = NULL;
        char   *export_path = NULL;
        char   *dup = NULL;
        char   *ptr = NULL;
        char   *freeptr = NULL;
        uuid_t  gfid = {0, };

        export_path = GF_CALLOC (1, sizeof (char) * PATH_MAX, 0);
        if (!export_path)
                goto out;

        dup = gf_strdup (real_path);
        if (!dup)
                goto out;

        freeptr = dup;
        ret = solaris_getxattr ("/", GFID_XATTR_KEY, gfid, 16);
        /* Return value of getxattr */
        if (ret == 16) {
                if (__is_root_gfid (gfid)){
                        strcat (export_path, "/");
                        ret = 0;
                        goto done;
                }
        }

        do {
                ptr = strtok_r (dup, "/", &tmp);
                if (!ptr)
                        break;
                strcat (export_path, dup);
                ret = solaris_getxattr (export_path, GFID_XATTR_KEY, gfid, 16);
                if (ret == 16) {
                        if (__is_root_gfid (gfid)) {
                                ret = 0;
                                goto done;
                        }
                }
                strcat (export_path, "/");
                dup = tmp;
        } while (ptr);

        goto out;

done:
        if (!ret) {
                *path = export_path;
        }
out:
        GF_FREE (freeptr);
        if (ret && export_path)
                GF_FREE (export_path);

        return ret;
}
int
solaris_xattr_resolve_path (const char *real_path, char **path)
{
        int                    ret  = -1;
        char                   *export_path = NULL;
        char                   xattr_path[PATH_MAX] = {0, };
        struct stat            lstatbuf = {0, };
        struct iatt            stbuf = {0, };
        struct stat            statbuf = {0, };

        ret = lstat (real_path, &lstatbuf);
        if (ret != 0 )
                return ret;
        iatt_from_stat (&stbuf, &lstatbuf);
        if (IA_ISREG(stbuf.ia_type) || IA_ISDIR(stbuf.ia_type))
                return -1;

        ret = make_export_path (real_path, &export_path);
        if (!ret && export_path) {
                strcat (export_path, "/"GF_SOLARIS_XATTR_DIR);
                if (lstat (export_path, &statbuf)) {
                        ret = mkdir (export_path, 0777);
                        if (ret && (errno != EEXIST)) {
                                gf_log (THIS->name, GF_LOG_DEBUG, "mkdir failed,"
                                        " errno: %d", errno);
                                goto out;
                        }
                }

                snprintf(xattr_path, PATH_MAX, "%s%s%lu", export_path,
                         "/", stbuf.ia_ino);

                ret = lstat (xattr_path, &statbuf);

                if (ret) {
                        ret = mknod (xattr_path, S_IFREG|O_WRONLY, 0);
                        if (ret && (errno != EEXIST)) {
                                gf_log (THIS->name, GF_LOG_WARNING,"Failed to create "
                                        "mapped file %s, error %d", xattr_path,
                                        errno);
                                goto out;
                        }
                }
                *path = gf_strdup (xattr_path);
        }
out:
        GF_FREE (export_path);
        if (*path)
                return 0;
        else
                return -1;
}

int
solaris_setxattr(const char *path, const char* key, const char *value,
                 size_t size, int flags)
{
        int attrfd = -1;
        int ret = 0;
        char *mapped_path = NULL;

        ret = solaris_xattr_resolve_path (path, &mapped_path);
        if (!ret) {
                attrfd = attropen (mapped_path,  key, flags|O_CREAT|O_WRONLY,
                                   0777);
        } else {
                attrfd = attropen (path, key, flags|O_CREAT|O_WRONLY, 0777);
        }
        if (attrfd >= 0) {
                ftruncate (attrfd, 0);
                ret = write (attrfd, value, size);
                close (attrfd);
                ret = 0;
        } else {
                if (errno != ENOENT)
                        gf_log ("libglusterfs", GF_LOG_ERROR,
                                "Couldn't set extended attribute for %s (%d)",
                                path, errno);
                ret = -1;
        }
        GF_FREE (mapped_path);
        return ret;
}


int
solaris_listxattr(const char *path, char *list, size_t size)
{
        int attrdirfd = -1;
        ssize_t len = 0;
        DIR *dirptr = NULL;
        struct dirent *dent = NULL;
        int newfd = -1;
        char *mapped_path = NULL;
        int ret = -1;

        ret = solaris_xattr_resolve_path (path, &mapped_path);
        if (!ret) {
                attrdirfd = attropen (mapped_path, ".", O_RDONLY, 0);
        } else {
                attrdirfd = attropen (path, ".", O_RDONLY, 0);
        }
        if (attrdirfd >= 0) {
                newfd = dup(attrdirfd);
                dirptr = fdopendir(newfd);
                if (dirptr) {
                        while ((dent = readdir(dirptr))) {
                                size_t listlen = strlen(dent->d_name);
                                if (!strcmp(dent->d_name, ".") ||
                                    !strcmp(dent->d_name, "..")) {
                                        /* we don't want "." and ".." here */
                                        continue;
                                }
                                if (size == 0) {
                                        /* return the current size of the list
                                           of extended attribute names*/
                                        len += listlen + 1;
                                } else {
                                        /* check size and copy entry + null
                                           into list. */
                                        if ((len + listlen + 1) > size) {
                                                errno = ERANGE;
                                                len = -1;
                                                break;
                                        } else {
                                                strncpy(list + len, dent->d_name, listlen);
                                                len += listlen;
                                                list[len] = '\0';
                                                ++len;
                                        }
                                }
                        }

                        if (closedir(dirptr) == -1) {
                                close (attrdirfd);
                                len = -1;
                                goto out;
                        }
                } else {
                        close (attrdirfd);
                        len = -1;
                        goto out;
                }
                close (attrdirfd);
        }
out:
        GF_FREE (mapped_path);
        return len;
}


int
solaris_flistxattr(int fd, char *list, size_t size)
{
        int attrdirfd = -1;
        ssize_t len = 0;
        DIR *dirptr = NULL;
        struct dirent *dent = NULL;
        int newfd = -1;

        attrdirfd = openat (fd, ".", O_RDONLY, 0);
        if (attrdirfd >= 0) {
                newfd = dup(attrdirfd);
                dirptr = fdopendir(newfd);
                if (dirptr) {
                        while ((dent = readdir(dirptr))) {
                                size_t listlen = strlen(dent->d_name);
                                if (!strcmp(dent->d_name, ".") ||
                                    !strcmp(dent->d_name, "..")) {
                                        /* we don't want "." and ".." here */
                                        continue;
                                }
                                if (size == 0) {
                                        /* return the current size of the list
                                           of extended attribute names*/
                                        len += listlen + 1;
                                } else {
                                        /* check size and copy entry + null
                                           into list. */
                                        if ((len + listlen + 1) > size) {
                                                errno = ERANGE;
                                                len = -1;
                                                break;
                                        } else {
                                                strncpy(list + len, dent->d_name, listlen);
                                                len += listlen;
                                                list[len] = '\0';
                                                ++len;
                                        }
                                }
                        }

                        if (closedir(dirptr) == -1) {
                                close (attrdirfd);
                                return -1;
                        }
                } else {
                        close (attrdirfd);
                        return -1;
                }
                close (attrdirfd);
        }
        return len;
}


int
solaris_removexattr(const char *path, const char* key)
{
        int ret = -1;
        int attrfd = -1;
        char *mapped_path = NULL;

        ret = solaris_xattr_resolve_path (path, &mapped_path);
        if (!ret) {
                attrfd = attropen (mapped_path, ".", O_RDONLY, 0);
        } else {
                attrfd = attropen (path, ".", O_RDONLY, 0);
        }
        if (attrfd >= 0) {
                ret = unlinkat (attrfd, key, 0);
                close (attrfd);
        } else {
                if (errno == ENOENT)
                        errno = ENODATA;
                ret = -1;
        }

        GF_FREE (mapped_path);

        return ret;
}

int
solaris_getxattr(const char *path,
                 const char* key,
                 char *value,
                 size_t size)
{
        int attrfd = -1;
        int ret = 0;
        char *mapped_path = NULL;

        ret = solaris_xattr_resolve_path (path, &mapped_path);
        if (!ret) {
                attrfd = attropen (mapped_path, key, O_RDONLY, 0);
        } else {
                attrfd = attropen (path, key, O_RDONLY, 0);
        }

        if (attrfd >= 0) {
                if (size == 0) {
                        struct stat buf;
                        fstat (attrfd, &buf);
                        ret = buf.st_size;
                } else {
                        ret = read (attrfd, value, size);
                }
                close (attrfd);
        } else {
                if (errno != ENOENT)
                        gf_log ("libglusterfs", GF_LOG_INFO,
                                "Couldn't read extended attribute for the file %s (%s)",
                                path, strerror (errno));
                if (errno == ENOENT)
                        errno = ENODATA;
                ret = -1;
        }
        GF_FREE (mapped_path);
        return ret;
}


char* strsep(char** str, const char* delims)
{
        char* token;

        if (*str==NULL) {
                /* No more tokens */
                return NULL;
        }

        token=*str;
        while (**str!='\0') {
                if (strchr(delims,**str)!=NULL) {
                        **str='\0';
                        (*str)++;
                        return token;
                }
                (*str)++;
        }
        /* There is no other token */
        *str=NULL;
        return token;
}

/* Code comes from libiberty */

int
vasprintf (char **result, const char *format, va_list args)
{
        return gf_vasprintf(result, format, args);
}

int
asprintf (char **buf, const char *fmt, ...)
{
        int status;
        va_list ap;

        va_start (ap, fmt);
        status = vasprintf (buf, fmt, ap);
        va_end (ap);
        return status;
}

int solaris_unlink (const char *path)
{
        char *mapped_path = NULL;
        struct stat     stbuf = {0, };
        int ret = -1;

        ret = solaris_xattr_resolve_path (path, &mapped_path);


        if (!ret && mapped_path) {
                if (lstat(path, &stbuf)) {
                        gf_log (THIS->name, GF_LOG_WARNING, "Stat failed on mapped"
                                " file %s with error %d", mapped_path, errno);
                        goto out;
                }
                if (stbuf.st_nlink == 1) {
                        if(remove (mapped_path))
                                gf_log (THIS->name, GF_LOG_WARNING, "Failed to remove mapped "
                                        "file %s. Errno %d", mapped_path, errno);
                }

        }

out:
        GF_FREE (mapped_path);

        return  unlink (path);
}

int
solaris_rename (const char *old_path, const char *new_path)
{
        char *mapped_path = NULL;
        int ret = -1;

        ret = solaris_xattr_resolve_path (new_path, &mapped_path);


        if (!ret && mapped_path) {
                if (!remove (mapped_path))
                        gf_log (THIS->name, GF_LOG_WARNING, "Failed to remove mapped "
                                "file %s. Errno %d", mapped_path, errno);
                GF_FREE (mapped_path);
        }

        return rename(old_path, new_path);

}

char *
mkdtemp (char *tempstring)
{
        char *new_string = NULL;
        int   ret        = 0;

        new_string = mkstemp (tempstring);
        if (!new_string)
                goto out;

        ret = mkdir (new_string, 0700);
        if (ret < 0)
                new_string = NULL;

out:
        return new_string;
}

#endif /* GF_SOLARIS_HOST_OS */

#ifndef HAVE_STRNLEN
size_t
strnlen(const char *string, size_t maxlen)
{
        int len = 0;
        while ((len < maxlen) && string[len])
                len++;
        return len;
}
#endif /* STRNLEN */
