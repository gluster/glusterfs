/*
  Copyright (c) 2013 Red Hat, Inc. <http://www.redhat.com>
  This file is part of GlusterFS.

  This file is licensed to you under your choice of the GNU Lesser
  General Public License, version 3 or any later version (LGPLv3 or
  later), or the GNU General Public License, version 2 (GPLv2), in all
  cases as published by the Free Software Foundation.
*/

#ifndef _GLFS_HANDLES_H
#define _GLFS_HANDLES_H

#include "glfs.h"

/* GLFS OBJECT BASED OPERATIONS
 *
 * The following APIs are introduced to provide an API framework that can work
 * with gluster objects (files and directories), instead of absolute paths.
 *
 * The following API set can be related to the POSIX *at interfaces (like
 * openat (2)). The intention of these APIs is to be able to operate based
 * on parent object and looking up or creating child objects within, OR to be
 * used on the actual object thus looked up or created, and retrieve information
 * regarding the same.
 *
 * The APIs also provide for generating an opaque invariant handle to the
 * object, that can later be used to lookup the object, instead of the regular
 * glfs_h_* variants. The APIs that provide this behaviour are,
 * glfs_h_extract_handle and glfs_h_create_from_handle.
 *
 * The object handles can be transitioned to fd based operations as supported
 * by glfs.h calls, using the glfs_h_open call. This provides a way to move
 * from objects to fd's akin to moving from path to fd for required operations.
 *
 * NOTE: The opaque invariant handle is the GFID of the object in reality, but
 * maintained as an opaque data value, for potential internal changes to the
 * same without impacting the caller.
 *
 * NOTE: Currently looking up an object can create multiple object handles to
 * the same, i.e distinct glfs_object *. Hence each such looked up or received
 * handle from other calls, would need to be closed. In the future, for a given
 * object these pointers would be the same, and an ease of use API to forget all
 * instances of this bject would be provided (instead of a per lookup close).
 * This should not change the APIs in their current form.
 *
 */

/* Values for valid falgs to be used when using XXXsetattr, to set multiple
 attribute values passed via the related stat structure.
 */
#define GFAPI_SET_ATTR_MODE  0x1
#define GFAPI_SET_ATTR_UID   0x2
#define GFAPI_SET_ATTR_GID   0x4
#define GFAPI_SET_ATTR_SIZE  0x8
#define GFAPI_SET_ATTR_ATIME 0x10
#define GFAPI_SET_ATTR_MTIME 0x20

/* Handle length for object handles returned from glfs_h_extract_handle or
 * glfs_h_create_from_handle */
#define GFAPI_HANDLE_LENGTH 16

__BEGIN_DECLS

/*
 * Notes:
 *
 * The file object handle. One per looked up, created file/directory
 *
 * This had been introduced to facilitate gfid/inode based gfapi
 * - a requirement introduced by nfs-ganesha
 */
struct glfs_object;
typedef struct glfs_object glfs_object_t;

/* Handle based operations */
/* Operations that generate handles */
struct glfs_object *glfs_h_lookupat (struct glfs *fs,
				     struct glfs_object *parent,
				     const char *path, struct stat *stat);

struct glfs_object *glfs_h_creat (struct glfs *fs, struct glfs_object *parent,
				  const char *path, int flags, mode_t mode,
				  struct stat *sb);

struct glfs_object *glfs_h_mkdir (struct glfs *fs, struct glfs_object *parent,
				  const char *path, mode_t flags,
				  struct stat *sb);

struct glfs_object *glfs_h_mknod (struct glfs *fs, struct glfs_object *parent,
				  const char *path, mode_t mode, dev_t dev,
				  struct stat *sb);

struct glfs_object *glfs_h_symlink (struct glfs *fs, struct glfs_object *parent,
				    const char *name, const char *data,
				    struct stat *stat);

/* Operations on the actual objects */
int glfs_h_unlink (struct glfs *fs, struct glfs_object *parent,
		   const char *path);

int glfs_h_close (struct glfs_object *object);

int glfs_caller_specific_init (void *uid_caller_key, void *gid_caller_key,
			       void *future);

int glfs_h_truncate (struct glfs *fs, struct glfs_object *object, off_t offset);

int glfs_h_stat(struct glfs *fs, struct glfs_object *object, struct stat *stat);

int glfs_h_getattrs (struct glfs *fs, struct glfs_object *object,
		     struct stat *stat);

int glfs_h_getxattrs (struct glfs *fs, struct glfs_object *object,
                      const char *name, void *value,
                      size_t size) __THROW;

int glfs_h_setattrs (struct glfs *fs, struct glfs_object *object,
		     struct stat *sb, int valid);

int glfs_h_setxattrs (struct glfs *fs, struct glfs_object *object,
                      const char *name, const void *value,
                      size_t size, int flags) __THROW;

int glfs_h_readlink (struct glfs *fs, struct glfs_object *object, char *buf,
		     size_t bufsiz);

int glfs_h_link (struct glfs *fs, struct glfs_object *linktgt,
		 struct glfs_object *parent, const char *name);

int glfs_h_rename (struct glfs *fs, struct glfs_object *olddir,
		   const char *oldname, struct glfs_object *newdir,
		   const char *newname);

int glfs_h_removexattrs (struct glfs *fs, struct glfs_object *object,
                         const char *name) __THROW;

/* Operations enabling opaque invariant handle to object transitions */
ssize_t glfs_h_extract_handle (struct glfs_object *object,
			       unsigned char *handle, int len);

struct glfs_object *glfs_h_create_from_handle (struct glfs *fs,
					       unsigned char *handle, int len,
					       struct stat *stat);

/* Operations enabling object handles to fd transitions */
struct glfs_fd *glfs_h_opendir (struct glfs *fs, struct glfs_object *object);

struct glfs_fd *glfs_h_open (struct glfs *fs, struct glfs_object *object,
			     int flags);

__END_DECLS

#endif /* !_GLFS_HANDLES_H */
