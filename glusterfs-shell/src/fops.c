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

#include "fops.h"

/* gf_open:
 *   open a file and return the file handle
 * @scm_volume: volume handle
 * @scm_path: path of file
 * @scm_flags: mode of opening file
 */
SCM 
gf_open (SCM scm_volume, SCM scm_path, SCM scm_flags)
{
  struct xlator *volume = (void *)SCM_INUM (scm_volume);
  char *path = SCM_STRING_CHARS (scm_path);
  struct file_context *ctxt = (void *) calloc (1, sizeof (struct file_context));
  int ret = -1;
  int flags = SCM_INUM (scm_flags);

  /* store this file's complete context here */
  ctxt->volume = volume;
  ctxt->context = path;

  ret = volume->fops->open (volume, path, flags, 0666, ctxt);
  if (ret < 0){
    free (ctxt);
    return SCM_BOOL_F; /* this should be an indication of error */
  }
  
  return SCM_MAKINUM ((int)ctxt);
}

SCM 
gf_getattr (SCM scm_volume, SCM scm_pathname)
{
  struct stat *statbuf = (void *) calloc (1, sizeof (struct stat));
  struct xlator *volume = (void *) SCM_INUM (scm_volume);
  char *pathname = SCM_STRING_CHARS (scm_pathname);
  int ret = volume->fops->getattr (volume, pathname, statbuf);
  SCM scm_statbuf;

  if (ret < 0){
    free (statbuf);
    return SCM_BOOL_F;
  }
  
  /* make a list containing the members of the struct stat structure */
  scm_statbuf = scm_list_n (SCM_MAKINUM (statbuf->st_dev),
			    SCM_MAKINUM (statbuf->st_ino),
			    SCM_MAKINUM (statbuf->st_mode),
			    SCM_MAKINUM (statbuf->st_nlink),
			    SCM_MAKINUM (statbuf->st_uid),
			    SCM_MAKINUM (statbuf->st_gid),
			    SCM_MAKINUM (statbuf->st_rdev),
			    SCM_MAKINUM (statbuf->st_size),
			    SCM_MAKINUM (statbuf->st_blksize),
			    SCM_MAKINUM (statbuf->st_blocks),
			    SCM_MAKINUM (statbuf->st_atime),
			    SCM_MAKINUM (statbuf->st_mtime),
			    SCM_MAKINUM (statbuf->st_ctime),
			    SCM_UNDEFINED);
  free (statbuf);
  return scm_statbuf;
  
}

SCM 
gf_readlink (SCM scm_volume, SCM scm_pathname)
{
  struct xlator *volume = (void *) SCM_INUM (scm_volume);
  char *pathname = (char *) SCM_STRING_CHARS (scm_pathname);
  char *buffer = (void *) calloc (sizeof (char), 256);
  int buffsize = 256;
  SCM scm_buffer;
  
  printf ("Calling readlink....\n");
  int ret = volume->fops->readlink (volume, pathname, buffer, buffsize);
  printf ("done");
  if (ret < 0){
    free (buffer);
    return SCM_BOOL_F;
  }
  
  scm_buffer = scm_makfrom0str (buffer);
  free (buffer);

  return scm_buffer;  
}


static int
gf_get_filetype (char *type)
{
  int mode = 0;
  if (GF_ISREG(type))
    mode |= S_IFREG;
  else if (GF_ISBLK(type))
    mode |= S_IFBLK;
  else if (GF_ISCHR(type))
    mode |= S_IFCHR;
  else if (GF_ISFIFO(type))
    mode |= S_IFIFO;
  else if (GF_ISSOCK(type))
    mode |= S_IFSOCK;

  return mode;
}
/* scm_type can have values,
 * 'regular - regular file
 * 'symlink - symbolic link
 * 'directory - directory
 * 'block-special - block device node
 * 'char-special - char device node
 * 'fifo - fifo file
 * 'socket - unix domain socket
 */
SCM 
gf_mknod (SCM scm_volume, SCM scm_pathname, SCM scm_type, SCM scm_mode, SCM scm_dev)
{
  /* need to add mode as well as dev_t to the args */
  struct xlator *volume = (void *) SCM_INUM (scm_volume);
  char *pathname = SCM_STRING_CHARS (scm_pathname); 
  char *type = SCM_SYMBOL_CHARS (scm_type);
  int mode = SCM_INUM (scm_mode);
  int dev = SCM_INUM (scm_dev);
  int ret = -1;
  
  printf ("type: %s\nmode: %d\ndev: %d\n", type, mode, dev);
  mode |= gf_get_filetype (type);
  ret = volume->fops->mknod (volume, pathname, mode, dev, getuid (), getgid ());
  if (ret < 0)
    return SCM_BOOL_F;
  return SCM_BOOL_T;
}

SCM 
gf_mkdir (SCM scm_volume, SCM scm_pathname)
{
  /* need to add mode as an arg */
  struct xlator *volume = (void *) SCM_INUM (scm_volume);
  char *pathname = SCM_STRING_CHARS (scm_pathname);
  int ret = -1;
  
  ret = volume->fops->mkdir (volume, pathname, 0, 0, 0);
  if (ret < 0)
    return SCM_BOOL_F;
  return SCM_BOOL_T;
}


SCM 
gf_unlink (SCM scm_volume, SCM scm_pathname)
{
  struct xlator *volume = (void *) SCM_INUM (scm_volume);
  char *pathname = SCM_STRING_CHARS (scm_pathname);
  int ret = -1;
  ret = volume->fops->unlink (volume, pathname);
  if (ret < 0)
    return SCM_BOOL_F;

  return SCM_BOOL_T;
}


SCM 
gf_rmdir (SCM scm_volume, SCM scm_pathname)
{
  struct xlator *volume = (void *) SCM_INUM (scm_volume);
  char *pathname = SCM_STRING_CHARS (scm_pathname);
  int ret = -1;
  ret = volume->fops->rmdir (volume, pathname);
  if (ret < 0)
    return SCM_BOOL_F;
  
  return SCM_BOOL_T;
}


SCM 
gf_symlink (SCM scm_volume, SCM scm_oldpath, SCM scm_newpath)
{
  struct xlator *volume = (void *) SCM_INUM (scm_volume);
  char *oldpath = SCM_STRING_CHARS (scm_oldpath);
  char *newpath = SCM_STRING_CHARS (scm_newpath);
  int ret = -1;
  ret = volume->fops->symlink (volume, oldpath, newpath, getuid (), getgid ());
  if (ret < 0)
    return SCM_BOOL_F;
  
  return SCM_BOOL_T;
}

SCM 
gf_rename (SCM scm_volume, SCM scm_oldpath, SCM scm_newpath)
{
  struct xlator *volume = (void *) SCM_INUM (scm_volume);
  char *oldpath = SCM_STRING_CHARS (scm_oldpath);
  char *newpath = SCM_STRING_CHARS (scm_newpath);
  int ret = -1;
  ret = volume->fops->rename (volume, oldpath, newpath, getuid (), getgid ());
  if (ret < 0)
    return SCM_BOOL_F;

  return SCM_BOOL_T;
}

SCM 
gf_link (SCM scm_volume, SCM scm_oldpath, SCM scm_newpath)
{
  struct xlator *volume = (void *) SCM_INUM (scm_volume);
  char *oldpath = SCM_STRING_CHARS (scm_oldpath);
  char *newpath = SCM_STRING_CHARS (scm_newpath);
  int ret = -1;
  ret = volume->fops->link (volume, oldpath, newpath, getuid (), getgid ());
  if (ret < 0)
    return SCM_BOOL_F;

  return SCM_BOOL_T;
}

SCM 
gf_chmod (SCM scm_volume, SCM scm_pathname, SCM scm_mode)
{
  struct xlator *volume = (void *) SCM_INUM (scm_volume);
  char *pathname = SCM_STRING_CHARS (scm_pathname);
  int mode = SCM_INUM (scm_mode);
  int ret = -1;
  ret = volume->fops->chmod (volume, pathname, mode);
  if (ret < 0)
    return SCM_BOOL_F;
  
  return SCM_BOOL_T;
}


SCM 
gf_chown (SCM scm_volume, SCM scm_pathname, SCM scm_uid, SCM scm_gid)
{
  struct xlator *volume = (void *) SCM_INUM (scm_volume);
  char *pathname = SCM_STRING_CHARS (scm_pathname);
  int uid = SCM_INUM (scm_uid);
  int gid = SCM_INUM (scm_gid);
  int ret = -1;
  ret = volume->fops->chown (volume, pathname, uid, gid);
  if (ret < 0)
    return SCM_BOOL_F;

  return SCM_BOOL_T;
}

SCM 
gf_truncate (SCM scm_volume, SCM scm_pathname, SCM scm_offset)
{
  struct xlator *volume = (void *) SCM_INUM (scm_volume);
  char *pathname = SCM_STRING_CHARS (scm_pathname);
  int offset = SCM_INUM (scm_offset);
  int ret = -1;
  ret = volume->fops->truncate (volume, pathname, offset);
  if (ret < 0)
    return SCM_BOOL_F;
  
  return SCM_BOOL_T;
}

SCM 
gf_utime (SCM scm_volume, SCM scm_pathname)
{
  struct xlator *volume = (void *) SCM_INUM (scm_volume);
  char *pathname = SCM_STRING_CHARS (scm_pathname);
  struct utimbuf *buf = calloc (sizeof (struct utimbuf), 1);
  SCM scm_utimbuf;
  int ret = -1;
  ret = volume->fops->utime (volume, pathname, buf);
  if (ret < 0)
    return SCM_BOOL_F;
  
  scm_utimbuf = scm_list_n ( SCM_MAKINUM (buf->actime),
			      SCM_MAKINUM (buf->modtime),
			      SCM_UNDEFINED);
  
  free (buf);
  return scm_utimbuf;
}

SCM 
gf_read (SCM scm_ctxt, SCM scm_len, SCM scm_offset)
{
  size_t count = SCM_INUM (scm_len);
  int offset = SCM_INUM (scm_offset);
  SCM scm_buffer;
  char *buffer = (void *) calloc (sizeof (char), count + 1);
  struct file_context *ctxt = (void *) SCM_INUM (scm_ctxt);
  struct xlator *volume = ctxt->volume;
  int ret = -1;
  
  if (!buffer)
    return SCM_BOOL_F;
  ret = volume->fops->read (volume, ctxt->context /* this is pathname :O */, 
			    buffer, count, offset, ctxt);
  if (ret < 0)
    return SCM_BOOL_F; /* this should be error */

  scm_buffer = gh_str02scm (buffer);
  printf ("gf_read : read %d bytes from the server\n", ret);
  free (buffer);
  return scm_buffer;
}

SCM 
gf_write (SCM scm_ctxt, SCM scm_buffer, SCM scm_len, SCM scm_offset)
{
  size_t count = SCM_INUM (scm_len);
  struct file_context *ctxt = (void *) SCM_INUM (scm_ctxt);
  struct xlator *volume = ctxt->volume;
  int ret  = -1;
  char *buffer = SCM_STRING_CHARS (scm_buffer);
  int offset = SCM_INUM (scm_offset);

  ret = volume->fops->write (volume, ctxt->context /* this is pathname :O */, 
			     buffer, count, offset, ctxt);

  if (ret < 0)
    return SCM_BOOL_T;

  free (buffer);
  return SCM_MAKINUM (ret);
}

SCM 
gf_statfs (SCM scm_volume, SCM scm_pathname)
{
  struct xlator *volume = (void *) SCM_INUM (scm_volume);
  char *pathname = SCM_STRING_CHARS (scm_pathname);
  struct statvfs *buf = calloc (sizeof (struct statvfs), 1);
  int ret = -1;
  SCM scm_statfs;
  
  ret = volume->fops->statfs (volume, pathname, buf);
  if (ret < 0){
    free (buf);
    return SCM_BOOL_T;
  }
  
  scm_statfs = scm_list_n (/*SCM_MAKINUM (buf->f_type),*/
			   SCM_MAKINUM (buf->f_bsize),
			   SCM_MAKINUM (buf->f_blocks),
			   SCM_MAKINUM (buf->f_bfree),
			   SCM_MAKINUM (buf->f_bavail),
			   SCM_MAKINUM (buf->f_files),
			   SCM_MAKINUM (buf->f_ffree),
			   SCM_MAKINUM (buf->f_fsid),
			   /*SCM_MAKINUM (buf->f_namelen),*/
			   SCM_UNDEFINED);

  free (buf);
  return scm_statfs;  
}

SCM 
gf_flush (SCM scm_context)
{
  struct file_context *ctxt = (void *) SCM_INUM (scm_context);
  struct xlator *volume = ctxt->volume;
  char *pathname = ctxt->context;
  int ret = -1;
  
  ret = volume->fops->flush (volume, pathname, ctxt);
  if (ret < 0)
    return SCM_BOOL_F;
  
  return SCM_BOOL_T;
}

SCM 
gf_release (SCM scm_context)
{
  return gf_close (scm_context);
}

SCM 
gf_fsync (SCM scm_context, SCM scm_fdatasync)
{
  struct file_context *ctxt = (void *) SCM_INUM (scm_context);
  struct xlator *volume = ctxt->volume;
  char *pathname = ctxt->context;
  int fdatasync = SCM_INUM (scm_fdatasync);
  int ret = -1;
  
  ret = volume->fops->fsync (volume, pathname, fdatasync, ctxt);
  if (ret < 0)
    return SCM_BOOL_F;
  
  return SCM_BOOL_T;  
}

SCM 
gf_setxattr (void)
{
  printf ("gf_setxattr");
  return SCM_UNSPECIFIED;
}

SCM 
gf_getxattr (void)
{
  printf ("gf_getxattr");
  return SCM_UNSPECIFIED;
}

SCM 
gf_listxattr (void)
{
  printf ("gf_listxattr");
  return SCM_UNSPECIFIED;
}

SCM 
gf_removexattr (void)
{
  printf ("gf_removexattr");
  return SCM_UNSPECIFIED;
}

SCM 
gf_opendir (SCM scm_volume, SCM scm_pathname)
{
  struct xlator *volume = (struct xlator *) SCM_INUM (scm_volume);
  char *dirname = SCM_STRING_CHARS (scm_pathname);
  struct file_context *ctxt = (void *) calloc (1, sizeof (struct file_context));
  int ret = -1;

  ctxt->volume = volume;
  ctxt->context = dirname;
  if (volume->fops){
    printf ("opening directory %s from %s\n", dirname, volume->name);  
    ret = volume->fops->opendir (volume, dirname, ctxt);
    if (ret < 0){
      free (ctxt);
      return SCM_BOOL_F;
    }
    return SCM_MAKINUM (ctxt);;
  }
  
  return SCM_BOOL_F;
}

SCM 
gf_readdir (SCM scm_volume, SCM scm_pathname)
{
  struct xlator *volume = (struct xlator *) SCM_INUM (scm_volume);
  char *dirname = SCM_STRING_CHARS (scm_pathname);
  SCM scm_fulllisting;
  SCM scm_splitchar;
  SCM scm_listing;

  if (volume->fops){
    printf ("reading directory %s from %s\n", dirname, volume->name);  
    /* FIXME: always reads from the begining of the directory */
    char *dirlisting = volume->fops->readdir (volume, dirname, 0);
    
    scm_fulllisting = gh_str02scm (dirlisting);
    scm_splitchar = gh_char2scm ('/');
    scm_listing = scm_string_split (scm_fulllisting, scm_splitchar); 
    
    return scm_listing;
  }
  
  return SCM_UNSPECIFIED;
}

SCM 
gf_releasedir (SCM scm_volume, SCM scm_context)
{
  struct file_context *ctxt = (void *)SCM_INUM (scm_context);
  struct xlator *volume = ctxt->volume;

  volume->fops->releasedir (volume, ctxt->context, ctxt);
  free (ctxt->context);
  free (ctxt);  
  return SCM_UNSPECIFIED;
}

SCM 
gf_fsyncdir (void)
{
  printf ("gf_fsyncdir");
  return SCM_UNSPECIFIED;
}

SCM 
gf_access (void)
{
  printf ("gf_access");
  return SCM_UNSPECIFIED;
}

SCM 
gf_create (void)
{
  printf ("gf_create");
  return SCM_UNSPECIFIED;
}

SCM 
gf_ftruncate (void)
{
  printf ("gf_ftruncate");
  return SCM_UNSPECIFIED;
}

SCM 
gf_fgetattr (void)
{
  printf ("gf_fgetattr");
  return SCM_UNSPECIFIED;
}

SCM 
gf_close (SCM scm_ctxt)
{
  struct file_context *ctxt = (void *)SCM_INUM (scm_ctxt);
  struct xlator *volume = ctxt->volume;
  char *path = ctxt->context;

  volume->fops->release (volume, path, ctxt);

  free (path);
  free (ctxt);

  return SCM_UNSPECIFIED;
}

SCM
gf_stats (SCM scm_volume)
{
  struct xlator *volume = (void *)SCM_INUM (scm_volume);
  struct xlator_stats stats;
  SCM scm_stats;

  int ret = volume->mgmt_ops->stats (volume, &stats);

  if (ret < 0)
    return SCM_BOOL_F;

  scm_stats = scm_list_4 (SCM_MAKINUM (stats.nr_files),
			  SCM_MAKINUM (stats.disk_usage),
			  SCM_MAKINUM (stats.free_disk),
			  SCM_MAKINUM (stats.nr_clients));

  return scm_stats;
}

/* gf_fops_init:
 *    initialize the shell on the file operations side. get the translator
 */
static void *
gf_fops_init (const char *specfile)
{
  FILE *conf = fopen (specfile, "r");
  
  if (!conf){
    perror ("open ()");
    exit (1);
  }
  printf ("Loading spec file from %s\n", specfile);
  struct xlator *tree = file_to_xlator_tree (conf);

  printf ("opened xlator %s\n", (tree->name?tree->name:"null"));
  fclose (conf);

  return tree;
}

SCM
ex_gf_init (SCM scm_filename)
{
  char *specfile = SCM_STRING_CHARS (scm_filename);
  
  printf ("Supposed to load specfile %s\n", specfile);

  struct xlator *tree = gf_fops_init (specfile);
  struct xlator *trav = tree;
  
  if (!tree){
    printf ("error opening the specfile and generating the xlator\n");
    exit (0);
  }

  /* call init of all the translators */
  while (trav) {
    if (trav->init)
      trav->init (trav);
    trav = trav->next;
  }
  
  while (tree->parent)
    tree = tree->parent;

  return SCM_MAKINUM ((unsigned int)tree);
}
