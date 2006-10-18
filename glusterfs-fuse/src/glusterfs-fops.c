/*
  (C) 2006 Gluster core team <http://www.gluster.org/>
  
  This program is free software; you can redistribute it and/or
  modify it under the terms of the GNU General Public License as
  published by the Free Software Foundation; either version 2 of
  the License, or (at your option) any later version.
    
  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
  GNU General Public License for more details.
    
  You should have received a copy of the GNU General Public
  License along with this program; if not, write to the Free
  Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
  Boston, MA 02110-1301 USA
*/ 

#include <stdint.h>

#include "glusterfs.h"
#include "logging.h"
#include "xlator.h"
#include "glusterfs-fops.h"

#include <fuse.h>
#include <fuse/fuse_lowlevel.h>

const char *specfile;
struct xlator *specfile_tree;
const char *mount_options;

#define ERR_EINVAL(cond)                         \
do                                               \
  {						 \
    if ((cond))					 \
      {						 \
	errno = EINVAL;				 \
	ret = -errno;				 \
	gf_log ("ERROR", 			 \
		GF_LOG_ERROR, 			 \
		"%s: %s: (%s) is true", 	 \
		__FILE__, __FUNCTION__, #cond);	 \
	return ret;				 \
      }                                          \
  } while (0)

int 
glusterfs_getattr (const char *path,
		   struct stat *stbuf)
{
  struct xlator *xlator = NULL;
  int32_t ret = 0;
  
  ERR_EINVAL (path == NULL || stbuf == NULL);
  
  xlator = fuse_get_context ()->private_data;
  ret = xlator->fops->getattr (xlator, path, stbuf);
  
  if (ret < 0)
    ret = -errno;
  else
    errno = 0;
  
  return ret;
}

int32_t
glusterfs_readlink (const char *path,
		    char *dest,
		    size_t size)
{
  struct xlator *xlator = NULL;
  int32_t ret = 0;
  
  ERR_EINVAL (path == NULL || dest == NULL || size == 0);
  
  xlator = fuse_get_context ()->private_data;
  ret = xlator->fops->readlink (xlator, path, dest, size);
  
  if (ret < 0)
    ret = -errno;
  else {
    dest[ret] = '\0';
    ret = 0;
    errno = 0;
  }
  return ret;
}

int32_t 
glusterfs_mknod (const char *path,
		 mode_t mode,
		 dev_t dev)
{
  struct xlator *xlator = NULL;
  int32_t ret = 0;
  
  ERR_EINVAL (path == NULL);
  
  xlator = fuse_get_context ()->private_data;
  ret = xlator->fops->mknod (xlator, path, mode, dev, 
			     fuse_get_context ()->uid, 
			     fuse_get_context ()->gid);
  
  if (ret < 0)
    ret = -errno;
  else
    errno = 0;
  return ret;
}

int32_t 
glusterfs_mkdir (const char *path,
		 mode_t mode)
{
  struct xlator *xlator = NULL;
  int32_t ret = 0;
  
  ERR_EINVAL (path == NULL);
  
  xlator = fuse_get_context ()->private_data;
  ret = xlator->fops->mkdir (xlator, path, mode,
			     fuse_get_context ()->uid,
			     fuse_get_context ()->gid);
  
  if (ret < 0)
    ret = -errno;
  else
    errno = 0;
  return ret;
}

int32_t 
glusterfs_unlink (const char *path)
{
  struct xlator *xlator = NULL;
  int32_t ret = 0;
  
  ERR_EINVAL (path == NULL);
  
  xlator = fuse_get_context ()->private_data;
  ret = xlator->fops->unlink (xlator, path);
  
  if (ret < 0)
    ret = -errno;
  else
    errno = 0;
  return ret;
}

int32_t 
glusterfs_rmdir (const char *path)
{
  struct xlator *xlator = NULL;
  int32_t ret = 0;
  
  ERR_EINVAL (path == NULL);
  
  xlator = fuse_get_context ()->private_data;
  ret = xlator->fops->rmdir (xlator, path);
  
  if (ret < 0)
    ret = -errno;
  else
    errno = 0;
  return ret;
}

int32_t 
glusterfs_symlink (const char *oldpath,
		   const char *newpath)
{
  struct xlator *xlator = NULL;
  int32_t ret = 0;
  
  ERR_EINVAL (oldpath == NULL || newpath == NULL);
  
  xlator = fuse_get_context ()->private_data;
  ret = xlator->fops->symlink (xlator, oldpath, newpath,
			       fuse_get_context ()->uid,
			       fuse_get_context ()->gid);
  
  if (ret < 0)
    ret = -errno;
  else
    errno = 0;
  return ret;
}

int32_t 
glusterfs_rename (const char *oldpath,
		  const char *newpath)
{
  struct xlator *xlator = NULL;
  int32_t ret = 0;
  
  ERR_EINVAL (oldpath == NULL || newpath == NULL);
  
  xlator = fuse_get_context ()->private_data;
  ret = xlator->fops->rename (xlator, oldpath, newpath,
			      fuse_get_context ()->uid,
			      fuse_get_context ()->gid);
  
  if (ret < 0)
    ret = -errno;
  else
    errno = 0;
  return ret;
}

int32_t 
glusterfs_link (const char *oldpath,
		const char *newpath)
{
  struct xlator *xlator = NULL;
  int32_t ret = 0;
  
  ERR_EINVAL (oldpath == NULL || newpath == NULL);
  
  xlator = fuse_get_context ()->private_data;
  ret = xlator->fops->link (xlator, oldpath, newpath,
			    fuse_get_context ()->uid,
			    fuse_get_context ()->gid);
  
  if (ret < 0)
    ret = -errno;
  else
    errno = 0;
  return ret;
}

int32_t 
glusterfs_chmod (const char *path,
		 mode_t mode)
{
  struct xlator *xlator = NULL;
  int32_t ret = 0;
  
  ERR_EINVAL (path == NULL);
  
  xlator = fuse_get_context ()->private_data;
  ret = xlator->fops->chmod (xlator, path, mode);
  
  if (ret < 0)
    ret = -errno;
  else
    errno = 0;
  return ret;
}

int32_t 
glusterfs_chown (const char *path,
		 uid_t uid,
		 gid_t gid)
{
  struct xlator *xlator = NULL;
  int32_t ret = 0;
  
  ERR_EINVAL (path == NULL);
  
  xlator = fuse_get_context ()->private_data;
  ret = xlator->fops->chown (xlator, path, uid, gid);
  
  if (ret < 0)
    ret = -errno;
  else
    errno = 0;
  return ret;
}

int32_t 
glusterfs_truncate (const char *path,
		    off_t offset)
{
  struct xlator *xlator = NULL;
  int32_t ret = 0;
  
  ERR_EINVAL (path == NULL);
  
  xlator = fuse_get_context ()->private_data;
  ret = xlator->fops->truncate (xlator, path, offset);
  
  if (ret < 0)
    ret = -errno;
  else
    errno = 0;
  return ret;
}

int32_t 
glusterfs_utime (const char *path,
		 struct utimbuf *buf)
{
  struct xlator *xlator = fuse_get_context ()->private_data;
  int32_t ret = xlator->fops->utime (xlator, path, buf);
  
  ERR_EINVAL (path == NULL || buf == NULL);
  
  xlator = fuse_get_context ()->private_data;
  ret = xlator->fops->utime (xlator, path, buf);
  
  if (ret < 0)
    ret = -errno;
  else
    errno = 0;
  return ret;
}

int32_t 
glusterfs_open (const char *path,
		struct fuse_file_info *info)
{
  struct xlator *xlator = NULL;
  struct file_context *ctx = NULL;
  int32_t ret = 0;
  
  ERR_EINVAL (path == NULL || info == NULL);
  
  xlator = fuse_get_context ()->private_data;
  ctx = (void *) calloc (1, sizeof (*ctx));
  ret = xlator->fops->open (xlator, path, info->flags, 0, ctx);
  
  if (ret < 0)
    {
      free (ctx);
      ret = -errno;
    }
  else
    {
      info->fh = (long)ctx;
      errno= 0;
      ret = 0;
      //    glusterfs_chown (path, fuse_get_context ()->uid, fuse_get_context ()->gid);
    }
  
  return ret;
}

int32_t 
glusterfs_read (const char *path,
		char *buf,
		size_t size,
		off_t offset,
		struct fuse_file_info *info)
{
  struct xlator *xlator = NULL;
  int32_t ret = 0;
  
  ERR_EINVAL (path == NULL || buf == NULL || info == NULL);
  
  printf ("read size = %d\n", size);
  xlator = fuse_get_context ()->private_data;
  ret = xlator->fops->read (xlator, path, buf, size, offset, (void *)(long)info->fh);
  
  if (ret < 0)
    ret = -errno;
  else
    errno = 0;
  return ret;
}

int32_t 
glusterfs_write (const char *path,
		 const char *buf,
		 size_t size,
		 off_t offset,
		 struct fuse_file_info *info)
{
  struct xlator *xlator = NULL;
  int32_t ret = 0;
  
  ERR_EINVAL (path == NULL || buf == NULL || info == NULL);
  
  xlator = fuse_get_context ()->private_data;
  ret = xlator->fops->write (xlator, path, buf, size, offset, (void *)(long)info->fh);
  
  if (ret < 0)
    ret = -errno;
  else
    errno = 0;
  return ret;
}

int32_t 
glusterfs_statfs (const char *path,
		  struct statvfs *buf)
{
  struct xlator *xlator = NULL;
  int32_t ret = 0;
  
  ERR_EINVAL (path == NULL || buf == NULL);
  
  xlator = fuse_get_context ()->private_data;
  ret = xlator->fops->statfs (xlator, path, buf);
  
  if (ret < 0)
    ret = -errno;
  else
    errno = 0;
  return ret;
}

int32_t 
glusterfs_flush (const char *path,
		 struct fuse_file_info *info)
{
  struct xlator *xlator = NULL;
  int32_t ret = 0;
  
  ERR_EINVAL (path == NULL || info == NULL);
  
  xlator = fuse_get_context ()->private_data;
  ret = xlator->fops->flush (xlator, path, (void *)(long)info->fh);
  
  if (ret < 0)
    ret = -errno;
  else
    errno = 0;
  return ret;
}

int32_t 
glusterfs_release (const char *path,
		   struct fuse_file_info *info)
{
  struct xlator *xlator = NULL;
  int32_t ret = 0;
  
  ERR_EINVAL (path == NULL || info == NULL);
  
  xlator = fuse_get_context ()->private_data;
  ret = xlator->fops->release (xlator, path, (void *)(long)info->fh);
  
  free ((void *)(long)info->fh);
  info->fh = 0;
  if (ret < 0)
    ret = -errno;
  else
    errno = 0;
  return ret;
}

int32_t 
glusterfs_fsync (const char *path,
		 int32_t datasync,
		 struct fuse_file_info *info)
{
  struct xlator *xlator = NULL;
  int32_t ret = 0;
  
  ERR_EINVAL (path == NULL || info == NULL);
  
  xlator = fuse_get_context ()->private_data;
  ret = xlator->fops->fsync (xlator, path, datasync, (void *)(long)info->fh);
  
  if (ret < 0)
    ret = -errno;
  else
    errno = 0;
  return ret;
}

int32_t 
glusterfs_setxattr (const char *path,
		    const char *name,
		    const char *value,
		    size_t size,
		    int32_t flags)
{
  struct xlator *xlator = NULL;
  int32_t ret = 0;
  
  ERR_EINVAL (path == NULL || name == NULL || value == NULL);
  
  xlator = fuse_get_context ()->private_data;
  ret = xlator->fops->setxattr (xlator, path, name, value, size, flags);
  
  if (ret < 0)
    ret = -errno;
  else
    errno = 0;
  return ret;
}

int32_t 
glusterfs_getxattr (const char *path,
		    const char *name,
		    char *value,
		    size_t size)
{
  struct xlator *xlator = NULL;
  int32_t ret = 0;
  
  ERR_EINVAL (path == NULL || name == NULL);
  
  xlator = fuse_get_context ()->private_data;
  ret = xlator->fops->getxattr (xlator, path, name, value, size);
  
  if (ret < 0)
    ret = -errno;
  else
    errno = 0;
  return ret;
}

int32_t 
glusterfs_listxattr (const char *path,
		     char *list,
		     size_t size)
{
  struct xlator *xlator = NULL;
  int32_t ret = 0;
  
  ERR_EINVAL (path == NULL || list == NULL);
  
  xlator = fuse_get_context ()->private_data;
  ret = xlator->fops->listxattr (xlator, path, list, size);
  
  if (ret < 0)
    ret = -errno;
  else
    errno = 0;
  return ret;
}

		     
int32_t 
glusterfs_removexattr (const char *path,
		       const char *name)
{
  struct xlator *xlator = NULL;
  int32_t ret = 0;
  
  ERR_EINVAL (path == NULL || name == NULL);
  
  xlator = fuse_get_context ()->private_data;
  ret = xlator->fops->removexattr (xlator, path, name);
  
  if (ret < 0)
    ret = -errno;
  else
    errno = 0;
  return ret;
}

int32_t 
glusterfs_opendir (const char *path,
		   struct fuse_file_info *info)
{
  struct xlator *xlator = NULL;
  int32_t ret = 0;
  
  ERR_EINVAL (path == NULL || info == NULL);
  
  xlator = fuse_get_context ()->private_data;
  ret = xlator->fops->opendir (xlator, path, (void *)(long)info->fh);
  
  if (ret < 0)
    ret = -errno;
  else
    errno = 0;
  return ret;
}

int32_t 
glusterfs_releasedir (const char *path,
		      struct fuse_file_info *info)
{
  struct xlator *xlator = NULL;
  int32_t ret = 0;
  
  ERR_EINVAL (path == NULL || info == NULL);
  
  xlator = fuse_get_context ()->private_data;
  ret = xlator->fops->releasedir (xlator, path, (void *)(long)info->fh);
  
  if (ret < 0)
    ret = -errno;
  else
    errno = 0;
  return ret;
}

int32_t 
glusterfs_fsyncdir (const char *path,
		    int32_t datasync,
		    struct fuse_file_info *info)
{
  struct xlator *xlator = NULL;
  int32_t ret = 0;
  
  ERR_EINVAL (path == NULL || info == NULL);
  
  xlator = fuse_get_context ()->private_data;
  ret = xlator->fops->fsyncdir (xlator, path, datasync, (void *)(long)info->fh);
  
  if (ret < 0)
    ret = -errno;
  else
    errno = 0;
  return ret;
}

int32_t 
glusterfs_access (const char *path,
		  int32_t mode)
{
  struct xlator *xlator = NULL;
  int32_t ret = 0;
  
  ERR_EINVAL (path);
  
  xlator = fuse_get_context ()->private_data;
  ret = xlator->fops->access (xlator, path, mode);
  
  if (ret < 0)
    ret = -errno;
  else
    errno = 0;
  return ret;
}

#if 0 /* hechchuvari */

int32_t 
glusterfs_create (const char *path,
		  mode_t mode,
		  struct fuse_file_info *info)
{
  struct xlator *xlator = NULL;
  struct file_context *cxt = NULL;
  int32_t ret = 0;
  
  ERR_EINVAL (path == NULL || info == NULL);
  
  xlator = fuse_get_context ()->private_data;
  cxt = (void *) calloc (1, sizeof (*cxt));
  ret = xlator->fops->open (xlator, path, info->flags | O_CREAT, mode, cxt);
  
  if (ret < 0)
    {
      free (cxt);
      ret = -errno;
    } 
  else
    {
      info->fh = (long)cxt;
      errno = 0;
    }
  
  return ret;
}
#endif /* hechchuvari */

int32_t 
glusterfs_ftruncate (const char *path,
		     off_t offset,
		     struct fuse_file_info *info)
{
  struct xlator *xlator = NULL;
  int32_t ret = 0;
  
  ERR_EINVAL (path == NULL || info == NULL);
  
  xlator = fuse_get_context ()->private_data;
  ret = xlator->fops->ftruncate (xlator, path, offset, (void *)(long)info->fh);
  
  if (ret < 0)
    ret = -errno;
  else
    errno = 0;
  return ret;
}

int32_t 
glusterfs_fgetattr (const char *path,
		    struct stat *buf,
		    struct fuse_file_info *info)
{
  struct xlator *xlator = NULL;
  int32_t ret = 0;
  
  ERR_EINVAL (path == NULL || buf == NULL || info == NULL);
  
  xlator = fuse_get_context ()->private_data;
  ret = xlator->fops->fgetattr (xlator, path, buf, (void *)(long)info->fh);
  
  if (ret < 0)
    ret = -errno;
  else
    errno = 0;
  return ret;
}

int32_t 
glusterfs_readdir (const char *path,
		   void *buf,
		   fuse_fill_dir_t fill,
		   off_t offset,
		   struct fuse_file_info *info)
{
  struct xlator *xlator = NULL;
  int32_t ret = 0;
  char *dirname = NULL;
  
  ERR_EINVAL (path == NULL || buf == NULL || info == NULL);
  
  xlator = fuse_get_context ()->private_data;
  dirname = xlator->fops->readdir (xlator, path, offset);
  
  char *ret_orig = dirname;
  {
    char *tmp;
    char *tmp_lock;

    tmp = strtok_r (dirname, "/", &tmp_lock);

    while (tmp) {
      fill (buf, tmp, NULL, 0);
      tmp = strtok_r (NULL, "/", &tmp_lock);
    }
  }
  
  free (ret_orig);
  errno = 0;
  return 0;
}

static void *
glusterfs_init (void)
{
  return specfile_tree;
}

static void
glusterfs_destroy (void *data)
{
  struct xlator *xlator = NULL;
  
  if (data == NULL)
    return;
  
  xlator = data;
  
  free (xlator);
  return;
}

static struct fuse_operations glusterfs_fops = {
  .getattr     = glusterfs_getattr,
  .readlink    = glusterfs_readlink,
  .getdir      = NULL /*glusterfs_getdir */,
  .mknod       = glusterfs_mknod,
  .mkdir       = glusterfs_mkdir,
  .unlink      = glusterfs_unlink,
  .rmdir       = glusterfs_rmdir,
  .symlink     = glusterfs_symlink,
  .rename      = glusterfs_rename,
  .link        = glusterfs_link,
  .chmod       = glusterfs_chmod,
  .chown       = glusterfs_chown,
  .truncate    = glusterfs_truncate,
  .utime       = glusterfs_utime,
  .open        = glusterfs_open,
  .read        = glusterfs_read,
  .write       = glusterfs_write,
  .statfs      = glusterfs_statfs,
  .flush       = glusterfs_flush,
  .release     = glusterfs_release,
  .fsync       = glusterfs_fsync,
  .setxattr    = glusterfs_setxattr,
  .getxattr    = glusterfs_getxattr,
  .listxattr   = glusterfs_listxattr,
  .removexattr = glusterfs_removexattr,
  .opendir     = glusterfs_opendir,
  .readdir     = glusterfs_readdir,
  .releasedir  = glusterfs_releasedir,
  .fsyncdir    = glusterfs_fsyncdir,
  .init        = glusterfs_init,
  .destroy     = glusterfs_destroy,
  .access      = glusterfs_access,
  /* Do not implement create. Let fuse call open with O_CREAT flag. */
  /*  .create      = glusterfs_create, */
  .ftruncate   = glusterfs_ftruncate,
  .fgetattr    = glusterfs_fgetattr
};


static int
defuse_wrapper (const char *mount_point)
{
  
  struct fuse *fuse;
  int res;
  int fd;
  char *mountpoint = strdup (mount_point);
  size_t op_size = sizeof (glusterfs_fops);
  int argc = 7;
  char *argv[] = { "glusterfs",
                   "-o",
                   "nonempty",
                   "-o",
                   "allow_other",
                   "-o",
                   "default_permissions",
                   NULL };

  extern struct fuse *my_fuse_setup (int, char *[], char *, struct fuse_operations *, size_t, int *);

  fuse = my_fuse_setup(argc, argv, mountpoint, &glusterfs_fops, op_size, &fd);

  if (fuse == NULL)
    return 1;

  int fuse_loop_wrapper (struct fuse *);
  res = fuse_loop_wrapper (fuse);

  fuse_teardown(fuse, fd, mountpoint);
  if (res == -1)
    return 1;

  return 0;
}

int32_t 
glusterfs_mount (struct spec_location *spec, 
		 char *mount_point, 
		 char *mount_fs_options)
{
  int32_t index = 0;
  struct xlator *trav = NULL;
  FILE *conf = NULL;
  char **full_arg = NULL;
  
  //  ERR_EINVAL (spec == NULL || mount_point == NULL || mount_fs_options == NULL);
  
  /* put the options to fuse in place */
  {
    int32_t count = 0;
    char *big_str = NULL;
    char *arg = NULL;
    
    /* format of how the options array to fuse_mount should look 
       char *argv[] = {
                       "glusterfs",
		       "-o", "default_permissions",
		       "-o", "allow_other",
		       "-o", "nonempty",
		       "-o", "hard_remove",
		       "-f",
		       mount_point,
		       NULL };
    */
    
    /* count the number of options */
    if (mount_fs_options){
      char *index_ptr = mount_fs_options;
      while (*index_ptr){
	if (*index_ptr == ',')
	  count++;
	++index_ptr;
      }
      count++;
    }
    
    full_arg = calloc (sizeof (char *), 
		       ((count * 2) /* fs mount options */ 
			+ (5 * 2) /* hard-coded mount options */
			+ 2 /* name of fs + NULL */
			+ 1 /* to specify mount point */));

    full_arg[index] = calloc (sizeof (char), strlen (GLUSTERFS_NAME) + 1);
    strcpy (full_arg[index], GLUSTERFS_NAME);
    index++;
    
    /* fill in the hard-coded options to fuse */
    {
      /*
      "-o", "default_permissions",
	"-o", "allow_other",
	"-o", "nonempty",
	"-o", "hard_remove" */
      
      /* "-o", "default_permissions" */
      full_arg[index] = calloc (sizeof (char), strlen (GLUSTERFS_MINUSO) + 1);
      strcpy (full_arg[index], "-o");
      index++;
      
      full_arg[index] = calloc (sizeof (char), strlen (DEFAULT_PERMISSIONS) + 1);
      strcpy (full_arg[index], DEFAULT_PERMISSIONS);
      index++;

      /* "-o", "allow_other" */
      full_arg[index] = calloc (sizeof (char), strlen (GLUSTERFS_MINUSO) + 1);
      strcpy (full_arg[index], "-o");
      index++;
      
      full_arg[index] = calloc (sizeof (char), strlen (ALLOW_OTHER) + 1);
      strcpy (full_arg[index], ALLOW_OTHER);
      index++;

      /* "-o", "nonempty" */
      full_arg[index] = calloc (sizeof (char), strlen (GLUSTERFS_MINUSO) + 1);
      strcpy (full_arg[index], "-o");
      index++;
      
      full_arg[index] = calloc (sizeof (char), strlen (NONEMPTY) + 1);
      strcpy (full_arg[index], NONEMPTY);
      index++;

      /* "-o", "hard_remove" */
      full_arg[index] = calloc (sizeof (char), strlen (GLUSTERFS_MINUSO) + 1);
      strcpy (full_arg[index], "-o");
      index++;
      
      full_arg[index] = calloc (sizeof (char), strlen (HARD_REMOVE) + 1);
      strcpy (full_arg[index], HARD_REMOVE);
      index++;

    }
    
    /* translate "-o debug" to "-f" for fuse */
    if (gf_cmd_def_daemon_mode == GF_NO) {
      full_arg[index] = calloc (sizeof (char), strlen (GLUSTERFS_MINUSF) + 1);
      strcpy (full_arg[index], GLUSTERFS_MINUSF);
      index++;
    }
    /* fill in user requested options */
    big_str = mount_fs_options;
    arg = strtok (big_str, ",");
    if (count){
      while (arg){
	{
	  /* copy the arguments sincerely for fuse */
	  full_arg[index] = calloc (sizeof (char), strlen (GLUSTERFS_MINUSO) + 1);
	  strcpy (full_arg[index], "-o");
	  index++;
	  
	  full_arg[index] = calloc (sizeof (char), strlen (arg) + 1);
	  strcpy (full_arg[index], arg);
	  index++;
	}
	arg = strtok (NULL, ",");
      }
    }
    
    /* put the mount point into the array */
    full_arg[index] = calloc (sizeof (char), strlen (mount_point) + 1);
    strcpy (full_arg[index], mount_point);
    index++;
    
    /* NULL terminate the array */
    full_arg[index] = NULL;
    
    /* debug: log the arguments we are sending to fuse */
    GF_LOG_FUSE_ARGS (full_arg, index);
  }

  /* spec - local spec file */
  if (spec->where == SPEC_LOCAL_FILE){
    specfile = spec->spec.file;
    
    conf = fopen (specfile, "r");
    
    if (!conf) {
      perror ("open()");
      exit (1);
    }
    gf_log ("glusterfs-fuse", GF_LOG_NORMAL, "loading spec from %s", specfile);
    specfile_tree = file_to_xlator_tree (conf);
    trav = specfile_tree;
  }else{
    /* add code here to get spec file from spec server */
     ; 
  }
  
  if (specfile_tree == NULL) {
    gf_log ("glusterfs-fuse", GF_LOG_ERROR, "specification file parsing failed, exiting");
    exit (-1);
  }
  
  while (trav) {
    if (trav->init)
      if (trav->init (trav) != 0) {
	struct xlator *node = specfile_tree;
	while (node != trav) {
	  node->fini (node);
	  node = node->next;
	}
	gf_log ("glusterfs-fuse", GF_LOG_ERROR, "%s xlator initialization failed\n", trav->name);
	exit (1);
      }
    trav = trav->next;
  }

  while (specfile_tree->parent)
    specfile_tree = specfile_tree->parent;

  fclose (conf);

  defuse_wrapper (mount_point);

  return 0;
}


