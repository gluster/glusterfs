/*
   Copyright (c) 2007-2009 Z RESEARCH, Inc. <http://www.zresearch.com>
   This file is part of GlusterFS.

   GlusterFS is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published
   by the Free Software Foundation; either version 3 of the License,
   or (at your option) any later version.

   GlusterFS is distributed in the hope that it will be useful, but
   WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program.  If not, see
   <http://www.gnu.org/licenses/>.
*/

#ifndef _CONFIG_H
#define _CONFIG_H
#include "config.h"
#endif

#include <dlfcn.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/uio.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <inttypes.h>
#include <libglusterfsclient.h>
#include <list.h>
#include <pthread.h>
#include <sys/xattr.h>
#include <string.h>
#include <assert.h>
#include <errno.h>
#include <ctype.h>
#include <logging.h>

#ifndef GF_UNIT_KB
#define GF_UNIT_KB 1024
#endif


extern int pipe (int filedes[2]);
/* We define these flags so that we can remove fcntl.h from the include path.
 * fcntl.h has certain defines and other lines of code that redirect the
 * application's open and open64 calls to the syscalls defined by
 * libc, for us, thats not a Good Thing (TM).
 */
#ifndef GF_O_CREAT
#define GF_O_CREAT      0x40
#endif

#ifndef GF_O_TRUNC
#define GF_O_TRUNC      0x200
#endif

#ifndef GF_O_RDWR
#define GF_O_RDWR       0x2
#endif

#ifndef GF_O_WRONLY
#define GF_O_WRONLY     0x1
#endif

#ifndef UNIX_PATH_MAX
#define UNIX_PATH_MAX 108
#endif

struct _inode;
struct _dict;
struct _fd {
        pid_t pid;
        struct list_head inode_list;
        struct _inode *inode;
        struct _dict *ctx;
        int32_t refcount;
};

typedef struct _fdtable fdtable_t;
typedef struct _fd fd_t;


inline void 
gf_fd_put (struct _fdtable *fdtable, int64_t fd);

struct _fd *
gf_fd_fdptr_get (struct _fdtable *fdtable, int64_t fd);

struct _fdtable *
gf_fd_fdtable_alloc (void);

void
gf_fd_fdtable_destroy (struct _fdtable *);

int32_t 
gf_fd_unused_get (struct _fdtable *fdtable, struct _fd *fdptr);

int32_t 
gf_fd_unused_get2 (struct _fdtable *fdtable, struct _fd *fdptr, int64_t fd);

void
fd_unref (struct _fd *fd);

fd_t *
fd_ref (struct _fd *fd);

pid_t
getpid (void);

ssize_t
write (int fd, const void *buf, size_t count);

/* open, open64, creat */
static int (*real_open) (const char *pathname, int flags, ...);
static int (*real_open64) (const char *pathname, int flags, ...);
static int (*real_creat) (const char *pathname, mode_t mode);

/* read, readv, pread, pread64 */
static ssize_t (*real_read) (int fd, void *buf, size_t count);
static ssize_t (*real_readv) (int fd, const struct iovec *vector, int count);
static ssize_t (*real_pread) (int fd, void *buf, size_t count,
                              unsigned long offset);
static ssize_t (*real_pread64) (int fd, void *buf, size_t count,
                                uint64_t offset);

/* write, writev, pwrite, pwrite64 */
static ssize_t (*real_write) (int fd, const void *buf, size_t count);
static ssize_t (*real_writev) (int fd, const struct iovec *vector, int count);
static ssize_t (*real_pwrite) (int fd, const void *buf, size_t count,
                               unsigned long offset);
static ssize_t (*real_pwrite64) (int fd, const void *buf, size_t count,
                                 uint64_t offset);

/* lseek, llseek, lseek64 */
static off_t (*real_lseek) (int fildes, unsigned long offset, int whence);
static off_t (*real_lseek64) (int fildes, uint64_t offset, int whence);

/* close */
static int (*real_close) (int fd);

/* dup dup2 */
static int (*real_dup) (int fd);
static int (*real_dup2) (int oldfd, int newfd);

static pid_t (*real_fork) (void);

#define RESOLVE(sym) do {                                       \
                if (!real_##sym)                                \
                        real_##sym = dlsym (RTLD_NEXT, #sym);   \
        } while (0)

/*TODO: set proper value */
#define MOUNT_HASH_SIZE 256

struct booster_mount {
        dev_t st_dev;
        glusterfs_handle_t handle;
        struct list_head device_list;
};
typedef struct booster_mount booster_mount_t;

struct booster_mount_table {
        pthread_mutex_t lock;
        struct list_head *mounts;
        int32_t hash_size;
};
typedef struct booster_mount_table booster_mount_table_t;

static fdtable_t *booster_glfs_fdtable = NULL;
static booster_mount_table_t *booster_mount_table = NULL;

/* This is dup'ed every time VMP open/creat wants a new fd.
 * This is needed so we occupy an entry in the process' file
 * table.
 */
int process_piped_fd = -1;

static int
booster_get_process_fd ()
{
        return real_dup (process_piped_fd);
}

#define DEFAULT_BOOSTER_CONF    "/etc/booster.conf"

int
booster_parse_line (char *buf, char **mount_point,
                    glusterfs_init_params_t *params)
{
        /* Structure of each line in booster config:
         * /virtual/mount/point /volume/specification/file volume-name /log/file loglevel cache-timeout
         * 0 can be entered for each of volume-name, /log/file, loglevel
         * to force default values.
         */
        char    *inbufptr = NULL;

        if (!buf || !mount_point || !params) {
                goto out;
        }
        memset (params, 0, sizeof (*params));
        inbufptr = strtok (buf, " ");
        if (inbufptr == NULL)
                goto out;
        *mount_point = strdup (inbufptr);

        inbufptr = strtok (NULL, " ");
        if (inbufptr == NULL)
                goto out;
        params->specfile = strdup (inbufptr);

        inbufptr = strtok (NULL, " ");
        if (inbufptr == NULL)
                goto out;
        params->volume_name = strdup (inbufptr);

        inbufptr = strtok (NULL, " ");
        if (inbufptr == NULL)
                goto out;
        params->logfile = strdup (inbufptr);

        inbufptr = strtok (NULL, " ");
        if (inbufptr == NULL)
                goto out;
        params->loglevel = strdup (inbufptr);

        return 0;
out:
        return -1;
}

int
booster_configure (char *confpath)
{
        FILE                    *fp = NULL;
        int                      ret = -1;
        char                     buf[4096];
        char                    *mount_point = NULL;
        glusterfs_init_params_t  params = {0, };

        fp = fopen (confpath, "r");
        if (fp == NULL) {
                goto out;
        }

        while (1) {
                fgets (buf, 4096, fp);
                if (feof (fp)) {
                        break;
                }

                mount_point = NULL;
                ret = booster_parse_line (buf, &mount_point, &params);
                if (ret == -1) {
                        goto out;
                }

                /* even if this mount fails, lets continue with others */
                glusterfs_mount (mount_point, &params);
        }

        ret = 0;
out:
        return ret;
}

static int32_t 
booster_put_handle (booster_mount_table_t *table,
                    dev_t st_dev,
                    glusterfs_handle_t handle)
{
        int32_t hash = 0;
        booster_mount_t *mount = NULL, *tmp = NULL;
	int32_t ret = 0;

        mount = calloc (1, sizeof (*mount));
	if (!mount) {
		return -1;
	}

        // ERR_ABORT (mount);
        INIT_LIST_HEAD (&mount->device_list);
        mount->st_dev = st_dev;
        mount->handle = handle;

        hash = st_dev % table->hash_size;

        pthread_mutex_lock (&table->lock);
        {
                list_for_each_entry (tmp, &table->mounts[hash], device_list) {
                        if (tmp->st_dev == st_dev) {
				ret = -1;
				errno = EEXIST;
				goto unlock;
                        }
                }

                list_add (&mount->device_list, &table->mounts[hash]);
        }
unlock:
        pthread_mutex_unlock (&table->lock);
  
        return ret;
}


static inline glusterfs_file_t
booster_get_glfs_fd (fdtable_t *fdtable, int fd)
{
        fd_t *glfs_fd = NULL;

        glfs_fd = gf_fd_fdptr_get (fdtable, fd);
        return glfs_fd;
}


static inline void
booster_put_glfs_fd (glusterfs_file_t glfs_fd)
{
        fd_unref ((fd_t *)glfs_fd);
}


static inline int32_t
booster_get_unused_fd (fdtable_t *fdtable, glusterfs_file_t glfs_fd, int fd)
{
        int32_t ret = -1;
        ret = gf_fd_unused_get2 (fdtable, (fd_t *)glfs_fd, fd);
        return ret;
}


static inline void
booster_put_fd (fdtable_t *fdtable, int fd)
{
        gf_fd_put (fdtable, fd);
}


static glusterfs_handle_t 
booster_get_handle (booster_mount_table_t *table, dev_t st_dev)
{
        int32_t hash = 0;
        booster_mount_t *mount = NULL;
        glusterfs_handle_t handle = NULL;

        hash = st_dev % table->hash_size; 

        pthread_mutex_lock (&table->lock);
        {
                list_for_each_entry (mount, &table->mounts[hash], device_list) {
                        if (mount->st_dev == st_dev) {
                                handle = mount->handle;
                                break;
                        }
                }
        }
        pthread_mutex_unlock (&table->lock);
  
        return handle;
}


void
do_open (int fd, int flags, mode_t mode)
{
        char *specfile = NULL;
        glusterfs_handle_t handle;
        int32_t file_size;
        struct stat st = {0,};
        int32_t ret = -1;

        ret = fstat (fd, &st);
        if (ret == -1) {
                return;
        }

        if (!booster_mount_table) {
                return;
        }

	handle = booster_get_handle (booster_mount_table, st.st_dev);
	if (!handle) {
		FILE *specfp = NULL;
		
		glusterfs_init_params_t ctx = {
			.loglevel = "critical",
			.lookup_timeout = 600,
			.stat_timeout = 600,
		};
      
		file_size = fgetxattr (fd, "user.glusterfs-booster-volfile",
                                       NULL, 0);
		if (file_size == -1) {
			return;
		}
		
		specfile = calloc (1, file_size);
		if (!specfile) {
			fprintf (stderr, "cannot allocate memory: %s\n",
                                 strerror (errno));
			return;
		}

		ret = fgetxattr (fd, "user.glusterfs-booster-volfile", specfile,
                                 file_size);
		if (ret == -1) {
			free (specfile);
			return ;
		}
    
		specfp = tmpfile ();
		if (!specfp) {
			free (specfile);
			return;
		}

		ret = fwrite (specfile, file_size, 1, specfp);
		if (ret != 1) {
			fclose (specfp);
			free (specfile);
		}
		
		fseek (specfp, 0L, SEEK_SET);
		
		ctx.logfile = getenv ("GLFS_BOOSTER_LOGFILE");
		ctx.specfp = specfp;

		handle = glusterfs_init (&ctx);
		
		free (specfile);
		fclose (specfp);
		
		if (!handle) {
			return;
		}

		ret = booster_put_handle (booster_mount_table, st.st_dev,
                                          handle);
		if (ret == -1) {
			glusterfs_fini (handle);
			if (errno != EEXIST) {
				return;
			}
		}
	}
  
        if (handle) {
                glusterfs_file_t glfs_fd;
                char path [UNIX_PATH_MAX];
                ret = fgetxattr (fd, "user.glusterfs-booster-path", path,
                                 UNIX_PATH_MAX);
                if (ret == -1) {
                        return;
                }

                glfs_fd = glusterfs_glh_open (handle, path, flags, mode);
                if (glfs_fd) {
                        ret = booster_get_unused_fd (booster_glfs_fdtable,
                                                     glfs_fd, fd);
                        if (ret == -1) {
                                glusterfs_close (glfs_fd);
                                return;
                        } 
                }
        } 

        return;
}

int
vmp_open (const char *pathname, int flags, ...)
{
        mode_t                  mode = 0;
        int                     fd = -1;
        glusterfs_file_t        fh = NULL;
        va_list                 ap;

        if (flags & GF_O_CREAT) {
                va_start (ap, flags);
                mode = va_arg (ap, mode_t);
                va_end (ap);

                fh = glusterfs_open (pathname, flags, mode);
        }
        else
                fh = glusterfs_open (pathname, flags);

        if (!fh)
                goto out;

        fd = booster_get_process_fd ();
        if (fd == -1)
                goto fh_close_out;

        if (booster_get_unused_fd (booster_glfs_fdtable, fh, fd) == -1)
                goto realfd_close_out;

        return fd;

realfd_close_out:
        real_close (fd);
        fd = -1;

fh_close_out:
        glusterfs_close (fh);

out:
        return fd;
}

#define BOOSTER_USE_OPEN64          1
#define BOOSTER_DONT_USE_OPEN64     0

int
booster_open (const char *pathname, int use64, int flags, ...)
{
        int     ret = -1;
        mode_t  mode = 0;
        va_list ap;
        int     (*my_open) (const char *pathname, int flags, ...);

        if (!pathname) {
                errno = EINVAL;
                goto out;
        }

        /* First try opening through the virtual mount point.
         * The difference lies in the fact that:
         * 1. We depend on libglusterfsclient library to perform
         * the translation from the path to handle.
         * 2. We do not go to the file system for the fd, instead
         * we use booster_get_process_fd (), which returns a dup'ed
         * fd of a pipe created in booster_init.
         */
        if (flags & GF_O_CREAT) {
                va_start (ap, flags);
                mode = va_arg (ap, mode_t);
                va_end (ap);
                ret = vmp_open (pathname, flags, mode);
        }
        else
                ret = vmp_open (pathname, flags);

        /* We receive an ENODEV if the VMP does not exist. If we
         * receive an error other than ENODEV, it means, there
         * actually was an error performing vmp_open. This must
         * be returned to the user.
         */
        if (((ret < 0) && (errno != ENODEV)) || (ret > 0))
                goto out;

        if (use64)
		my_open = real_open64;
        else
		my_open = real_open;

        /* It is possible the RESOLVE macro is not able
         * to resolve the symbol of a function, in that case
         * we dont want to seg-fault on calling a NULL functor.
         */
        if (my_open == NULL) {
                ret = -1;
                errno = ENOSYS;
                goto out;
        }

	if (flags & GF_O_CREAT) {
		va_start (ap, flags);
		mode = va_arg (ap, mode_t);
		va_end (ap);

                ret = my_open (pathname, flags, mode);
	} else
                ret = my_open (pathname, flags);

        if (ret != -1) {
                flags &= ~ GF_O_CREAT;
                do_open (ret, flags, mode);
        }

out:
        return ret;
}

int
open (const char *pathname, int flags, ...)
{
        int ret = -1;
	mode_t mode = 0;
	va_list ap;

        if (flags & GF_O_CREAT) {
                va_start (ap, flags);
                mode = va_arg (ap, mode_t);
                va_end (ap);

                ret = booster_open (pathname, BOOSTER_DONT_USE_OPEN64,
                                        flags, mode);
        }
        else
                ret = booster_open (pathname, BOOSTER_DONT_USE_OPEN64, flags);

        return ret;
}

#if defined (__USE_LARGEFILE64) || !defined (__USE_FILE_OFFSET64)
int
open64 (const char *pathname, int flags, ...)
{
        int ret;
	mode_t mode = 0;
	va_list ap;

        if (flags & GF_O_CREAT) {
                va_start (ap, flags);
                mode = va_arg (ap, mode_t);
                va_end (ap);

                ret = booster_open (pathname, BOOSTER_USE_OPEN64, flags, mode);
        }
        else
                ret = booster_open (pathname, BOOSTER_USE_OPEN64, flags);

        return ret;
}
#endif

int
vmp_creat (const char *pathname, mode_t mode)
{
        int                     fd = -1;
        glusterfs_file_t        fh = NULL;

        fh = glusterfs_creat (pathname, mode);
        if (!fh)
                goto out;

        fd = booster_get_process_fd ();
        if (fd == -1)
                goto close_out;

        if ((booster_get_unused_fd (booster_glfs_fdtable, fh, fd)) == -1)
                goto real_close_out;

        return fd;

real_close_out:
        real_close (fd);
        fd = -1;

close_out:
        glusterfs_close (fh);

out:
        return -1;
}

int
creat (const char *pathname, mode_t mode)
{
        int     ret = -1;
        if (!pathname) {
                errno = EINVAL;
                goto out;
        }

        ret = vmp_creat (pathname, mode);

        if (((ret == -1) && (errno != ENODEV)) || (ret > 0))
                goto out;

        if (real_creat == NULL) {
                errno = ENOSYS;
                ret = -1;
                goto out;
        }

        ret = real_creat (pathname, mode);

        if (ret != -1) {
                do_open (ret, GF_O_WRONLY | GF_O_TRUNC, mode);
        }

out:
        return ret;
}


/* pread */

ssize_t
pread (int fd, void *buf, size_t count, unsigned long offset)
{
        ssize_t ret;
        glusterfs_file_t glfs_fd = 0;

        glfs_fd = booster_get_glfs_fd (booster_glfs_fdtable, fd);
        if (!glfs_fd) { 
                if (real_pread == NULL) {
                        errno = ENOSYS;
                        ret = -1;
                } else
                        ret = real_pread (fd, buf, count, offset);
        } else {
                ret = glusterfs_pread (glfs_fd, buf, count, offset);
                booster_put_glfs_fd (glfs_fd);
        }

        return ret;
}


ssize_t
pread64 (int fd, void *buf, size_t count, uint64_t offset)
{
        ssize_t ret;
        glusterfs_file_t glfs_fd = 0;

        glfs_fd = booster_get_glfs_fd (booster_glfs_fdtable, fd);
        if (!glfs_fd) { 
                ret = real_pread (fd, buf, count, offset);
        } else {
                ret = glusterfs_pread (glfs_fd, buf, count, offset);
                if (ret == -1) {
                        ret = real_pread (fd, buf, count, offset);
                }
        }

        return ret;
}


ssize_t
read (int fd, void *buf, size_t count)
{
        int ret;
        glusterfs_file_t glfs_fd;

        glfs_fd = booster_get_glfs_fd (booster_glfs_fdtable, fd);
        if (!glfs_fd) {
                if (real_read == NULL) {
                        errno = ENOSYS;
                        ret = -1;
                } else
                        ret = real_read (fd, buf, count);
        } else {
                ret = glusterfs_read (glfs_fd, buf, count);
                booster_put_glfs_fd (glfs_fd);
        }

        return ret;
}


ssize_t
readv (int fd, const struct iovec *vector, int count)
{
        int ret;
        glusterfs_file_t glfs_fd = 0;

        glfs_fd = booster_get_glfs_fd (booster_glfs_fdtable, fd);
        if (!glfs_fd) {
                if (real_readv == NULL) {
                        errno = ENOSYS;
                        ret = -1;
                } else
                        ret = real_readv (fd, vector, count);
        } else {
		ret = glusterfs_readv (glfs_fd, vector, count);
                booster_put_glfs_fd (glfs_fd);
        }

        return ret;
}


ssize_t
write (int fd, const void *buf, size_t count)
{
        int ret;
        glusterfs_file_t glfs_fd = 0;

        glfs_fd = booster_get_glfs_fd (booster_glfs_fdtable, fd);

        if (!glfs_fd) {
                ret = real_write (fd, buf, count);
        } else {
		uint64_t offset = 0;
                offset = real_lseek64 (fd, 0L, SEEK_CUR);
                if (((int64_t) offset) != -1) {
                        ret = glusterfs_lseek (glfs_fd, offset, SEEK_SET);
                        if (ret != -1) {  
                                ret = glusterfs_write (glfs_fd, buf, count);
                        }
                } else {
                        ret = -1;
		}

                if (ret == -1) {
                        ret = real_write (fd, buf, count);
                }

                if (ret > 0 && ((int64_t) offset) >= 0) {
                        real_lseek64 (fd, offset + ret, SEEK_SET);
		}
                booster_put_glfs_fd (glfs_fd);
        }
 
        return ret;
}

ssize_t
writev (int fd, const struct iovec *vector, int count)
{
        int ret = 0;
        glusterfs_file_t glfs_fd = 0; 

        glfs_fd = booster_get_glfs_fd (booster_glfs_fdtable, fd);

        if (!glfs_fd) {
                ret = real_writev (fd, vector, count);
        } else {
                uint64_t offset = 0;
                offset = real_lseek64 (fd, 0L, SEEK_CUR);

                if (((int64_t) offset) != -1) {
                        ret = glusterfs_lseek (glfs_fd, offset, SEEK_SET);
                        if (ret != -1) {
                                ret = glusterfs_writev (glfs_fd, vector, count);
                        }
                } else {
                        ret = -1;
			} 

/*		ret = glusterfs_writev (glfs_fd, vector, count); */
                if (ret == -1) {
                        ret = real_writev (fd, vector, count);
                }

                if (ret > 0 && ((int64_t)offset) >= 0) {
                        real_lseek64 (fd, offset + ret, SEEK_SET);
                }

                booster_put_glfs_fd (glfs_fd);
        }

        return ret;
}


ssize_t
pwrite (int fd, const void *buf, size_t count, unsigned long offset)
{
        int ret;
        glusterfs_file_t glfs_fd = 0;

        assert (real_pwrite != NULL);

        glfs_fd = booster_get_glfs_fd (booster_glfs_fdtable, fd);

        if (!glfs_fd) {
                ret = real_pwrite (fd, buf, count, offset);
        } else {
                ret = glusterfs_pwrite (glfs_fd, buf, count, offset);
                if (ret == -1) {
                        ret = real_pwrite (fd, buf, count, offset);
                }
                booster_put_glfs_fd (glfs_fd);
        }

        return ret;
}


ssize_t
pwrite64 (int fd, const void *buf, size_t count, uint64_t offset)
{
        int ret;
        glusterfs_file_t glfs_fd = 0;

        glfs_fd = booster_get_glfs_fd (booster_glfs_fdtable, fd);
  
        if (!glfs_fd) {
                ret = real_pwrite64 (fd, buf, count, offset);
        } else {
                ret = glusterfs_pwrite (glfs_fd, buf, count, offset);
                if (ret == -1) {
                        ret = real_pwrite64 (fd, buf, count, offset);
                }
        }

        return ret;
}


int
close (int fd)
{
        int ret = -1;
        glusterfs_file_t glfs_fd = 0;

	glfs_fd = booster_get_glfs_fd (booster_glfs_fdtable, fd);
    
	if (glfs_fd) {
		booster_put_fd (booster_glfs_fdtable, fd);
		ret = glusterfs_close (glfs_fd);
		booster_put_glfs_fd (glfs_fd);
	}

        ret = real_close (fd);

        return ret;
}

#ifndef _LSEEK_DECLARED
#define _LSEEK_DECLARED
off_t
lseek (int filedes, unsigned long offset, int whence)
{
        int ret;
        glusterfs_file_t glfs_fd = 0;

        ret = real_lseek (filedes, offset, whence);

        glfs_fd = booster_get_glfs_fd (booster_glfs_fdtable, filedes);
        if (glfs_fd) {
                ret = glusterfs_lseek (glfs_fd, offset, whence);
                booster_put_glfs_fd (glfs_fd);
        }

        return ret;
}
#endif

off_t
lseek64 (int filedes, uint64_t offset, int whence)
{
        int ret;
        glusterfs_file_t glfs_fd = 0;

        ret = real_lseek64 (filedes, offset, whence);

        glfs_fd = booster_get_glfs_fd (booster_glfs_fdtable, filedes);
        if (glfs_fd) {
                ret = glusterfs_lseek (glfs_fd, offset, whence);
                booster_put_glfs_fd (glfs_fd);
        }

        return ret;
}

int 
dup (int oldfd)
{
        int ret = -1, new_fd = -1;
        glusterfs_file_t glfs_fd = 0;

        glfs_fd = booster_get_glfs_fd (booster_glfs_fdtable, oldfd);
        new_fd = real_dup (oldfd);

        if (new_fd >=0 && glfs_fd) {
                ret = booster_get_unused_fd (booster_glfs_fdtable, glfs_fd,
                                             new_fd);
                fd_ref ((fd_t *)glfs_fd);
                if (ret == -1) {
                        real_close (new_fd);
                } 
        }

        if (glfs_fd) {
                booster_put_glfs_fd (glfs_fd);
        }

        return new_fd;
}


int 
dup2 (int oldfd, int newfd)
{
        int ret = -1;
        glusterfs_file_t old_glfs_fd = NULL, new_glfs_fd = NULL;

        if (oldfd == newfd) {
                return newfd;
        }

        old_glfs_fd = booster_get_glfs_fd (booster_glfs_fdtable, oldfd);
        new_glfs_fd = booster_get_glfs_fd (booster_glfs_fdtable, newfd);
 
        ret = real_dup2 (oldfd, newfd); 
        if (ret >= 0) {
                if (new_glfs_fd) {
                        glusterfs_close (new_glfs_fd);
                        booster_put_glfs_fd (new_glfs_fd);
                        booster_put_fd (booster_glfs_fdtable, newfd);
                        new_glfs_fd = 0;
                }

                if (old_glfs_fd) {
                        ret = booster_get_unused_fd (booster_glfs_fdtable,
                                                     old_glfs_fd, newfd);
                        fd_ref ((fd_t *)old_glfs_fd);
                        if (ret == -1) {
                                real_close (newfd);
                        }
                }
        } 

        if (old_glfs_fd) {
                booster_put_glfs_fd (old_glfs_fd);
        }

        if (new_glfs_fd) {
                booster_put_glfs_fd (new_glfs_fd);
        }

        return ret;
}


#define MOUNT_TABLE_HASH_SIZE 256


static int 
booster_init (void)
{
        int     i = 0;
        char    *booster_conf_path = NULL;
        int     ret = -1;
        int     pipefd[2];

        booster_glfs_fdtable = gf_fd_fdtable_alloc ();
        if (!booster_glfs_fdtable) {
                fprintf (stderr, "cannot allocate fdtable: %s\n",
                         strerror (errno));
		goto err;
        }
 
        booster_mount_table = calloc (1, sizeof (*booster_mount_table));
        if (!booster_mount_table) {
                fprintf (stderr, "cannot allocate memory: %s\n",
                         strerror (errno));
		goto err;
        }

        pthread_mutex_init (&booster_mount_table->lock, NULL);
        booster_mount_table->hash_size = MOUNT_TABLE_HASH_SIZE;
        booster_mount_table->mounts = calloc (booster_mount_table->hash_size,
                                              sizeof (*booster_mount_table->mounts));
        if (!booster_mount_table->mounts) {
                fprintf (stderr, "cannot allocate memory: %s\n",
                         strerror (errno));
		goto err;
        }
 
        for (i = 0; i < booster_mount_table->hash_size; i++) 
        {
                INIT_LIST_HEAD (&booster_mount_table->mounts[i]);
        }

        if (pipe (pipefd) == -1)
                goto err;

        process_piped_fd = pipefd[0];
        real_close (pipefd[1]);
        /* libglusterfsclient based VMPs should be inited only
         * after the file tables are inited so that if the socket
         * calls use the fd based syscalls, the fd tables are
         * correctly initialized to return a NULL handle, on which the
         * socket calls will fall-back to the real API.
         */
        booster_conf_path = getenv ("GLFS_BOOSTER_CONF");
        if (booster_conf_path == NULL)
                ret = booster_configure (DEFAULT_BOOSTER_CONF);
        else
                ret = booster_configure (booster_conf_path);

        if (ret == -1)
                goto err;

	return 0;

err:
	if (booster_glfs_fdtable) {
		gf_fd_fdtable_destroy (booster_glfs_fdtable);
		booster_glfs_fdtable = NULL;
	}

	if (booster_mount_table) {
		if (booster_mount_table->mounts) {
			free (booster_mount_table->mounts);
		}

		free (booster_mount_table);
		booster_mount_table = NULL;
	}
	return -1; 
}


static void
booster_cleanup (void)
{
	int i;
	booster_mount_t *mount = NULL, *tmp = NULL;
	
	/* gf_fd_fdtable_destroy (booster_glfs_fdtable);*/
	/*for (i=0; i < booster_glfs_fdtable->max_fds; i++) {
		if (booster_glfs_fdtable->fds[i]) {
			fd_t *fd = booster_glfs_fdtable->fds[i];
			free (fd);			  
		}
		}*/

	free (booster_glfs_fdtable);
	booster_glfs_fdtable = NULL;

        pthread_mutex_lock (&booster_mount_table->lock);
        {
		for (i = 0; i < booster_mount_table->hash_size; i++) 
		{
			list_for_each_entry_safe (mount, tmp, 
						  &booster_mount_table->mounts[i], device_list) {
				list_del (&mount->device_list);
				glusterfs_fini (mount->handle);
				free (mount);
			}
                }
		free (booster_mount_table->mounts);
        }
        pthread_mutex_unlock (&booster_mount_table->lock);

	glusterfs_reset ();
	free (booster_mount_table);
	booster_mount_table = NULL;
}



pid_t 
fork (void)
{
	pid_t pid = 0;
	char child = 0;

	glusterfs_log_lock ();
	{
		pid = real_fork ();
	}
	glusterfs_log_unlock ();

	child = (pid == 0);
	if (child) {
		booster_cleanup ();
		booster_init ();
	}

	return pid;
}


void
_init (void)
{

        RESOLVE (open);
        RESOLVE (open64);
        RESOLVE (creat);

        RESOLVE (read);
        RESOLVE (readv);
        RESOLVE (pread);
        RESOLVE (pread64);

        RESOLVE (write);
        RESOLVE (writev);
        RESOLVE (pwrite);
        RESOLVE (pwrite64);

        RESOLVE (lseek);
        RESOLVE (lseek64);

        RESOLVE (close);

        RESOLVE (dup);
        RESOLVE (dup2);

	RESOLVE (fork); 

        /* This must be called after resolving real functions
         * above so that the socket based IO calls in libglusterfsclient
         * can fall back to a non-NULL real_XXX function pointer.
         * Calling booster_init before resolving the names above
         * results in seg-faults because the function symbols above are NULL.
         */
	booster_init ();
}

