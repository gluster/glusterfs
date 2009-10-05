/*
   Copyright (c) 2007-2009 Gluster, Inc. <http://www.gluster.com>
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

#include <sys/types.h>

extern int
booster_stat (const char *path, void *buf);

extern int
booster_stat64 (const char *path, void *buf);

extern int
booster_xstat (int ver, const char *path, void *buf);

extern int
booster_xstat64 (int ver, const char *path, void *buf);

extern int
booster_fxstat (int ver, int fd, void *buf);
extern int
booster_fxstat64 (int ver, int fd, void *buf);
extern int
booster_fstat (int fd, void *buf);
extern int
booster_fstat64 (int fd, void *buf);

extern int
booster_lstat (const char *path, void *buf);
extern int
booster_lstat64 (const char *path, void *buf);
extern int
booster_lxstat (int ver, const char *path, void *buf);
extern int
booster_lxstat64 (int ver, const char *path, void *buf);


extern int
booster_statfs (const char *path, void *buf);
extern int
booster_statfs64 (const char *path, void *buf);

extern int
booster_statvfs (const char *path, void *buf);

extern int
booster_statvfs64 (const char *path, void *buf);

extern void *
booster_readdir (void *dir);

extern void
booster_rewinddir (void *dir);

extern void
booster_seekdir (void *dir, off_t offset);

extern off_t
booster_telldir (void *dir);

int
stat (const char *path, void *buf)
{
        return booster_stat (path, buf);
}

int
stat64 (const char *path, void *buf)
{
        return booster_stat64 (path, buf);
}

int
__xstat (int ver, const char *path, void *buf)
{
        return booster_xstat (ver, path, buf);
}

int
__xstat64 (int ver, const char *path, void *buf)
{
        return booster_xstat64 (ver, path, buf);
}

int
__fxstat (int ver, int fd, void *buf)
{
        return booster_fxstat (ver, fd, buf);
}

int
__fxstat64 (int ver, int fd, void *buf)
{
        return booster_fxstat64 (ver, fd, buf);
}

int
fstat (int fd, void *buf)
{
        return booster_fstat (fd, buf);
}

int
fstat64 (int fd, void *buf)
{
        return booster_fstat64 (fd, buf);
}

int
lstat (const char *path, void *buf)
{
        return booster_lstat (path, buf);
}

int
lstat64 (const char *path, void *buf)
{
        return booster_lstat64 (path, buf);
}

int
__lxstat (int ver, const char *path, void *buf)
{
        return booster_lxstat (ver, path, buf);
}

int
__lxstat64 (int ver, const char *path, void *buf)
{
        return booster_lxstat64 (ver, path, buf);
}

int
statfs (const char *pathname, void *buf)
{
        return booster_statfs (pathname, buf);
}

int
statfs64 (const char *pathname, void *buf)
{
        return booster_statfs64 (pathname, buf);
}

int
statvfs (const char *pathname, void *buf)
{
        return booster_statvfs (pathname, buf);
}

int
statvfs64 (const char *pathname, void *buf)
{
        return booster_statvfs64 (pathname, buf);
}

void *
readdir (void *dir)
{
        return booster_readdir (dir);
}

void *
readdir64 (void *dir)
{
        return booster_readdir (dir);
}

void
rewinddir (void *dir)
{
	return booster_rewinddir (dir);
}

void
seekdir (void *dir, off_t offset)
{
	return booster_seekdir (dir, offset);
}

off_t
telldir (void *dir)
{
	return booster_telldir (dir);
}
