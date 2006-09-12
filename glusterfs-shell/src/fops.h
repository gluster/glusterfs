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

#include "shell.h"
#include "xlator.h"

/* helper macros */
#define GF_ISREG(str) (!strcmp ("regular", str))
#define GF_ISBLK(str) (!strcmp ("block-special", str))
#define GF_ISCHR(str) (!strcmp ("char-special", str))
#define GF_ISFIFO(str) (!strcmp ("fifo", str))
#define GF_ISSOCK(str) (!strcmp ("socket", str))

/* init */
SCM ex_gf_init (SCM filename);
/* file operations */
SCM gf_open (SCM scm_volume, SCM scm_mystring, SCM scm_flags);
SCM gf_getattr (SCM scm_volume, SCM scm_pathname);
SCM gf_readlink (SCM scm_volume, SCM scm_pathname);
SCM gf_mknod (SCM scm_volume, SCM scm_pathname, SCM scm_type, SCM scm_mode, SCM scm_dev);
SCM gf_mkdir (SCM scm_volume, SCM scm_pathname);
SCM gf_unlink (SCM scm_volume, SCM scm_pathname);
SCM gf_rmdir (SCM scm_volume, SCM scm_pathname);
SCM gf_symlink (SCM scm_volume, SCM scm_oldpath, SCM scm_newpath);
SCM gf_rename (SCM scm_volume, SCM scm_oldpath, SCM scm_newpath);
SCM gf_link (SCM scm_volume, SCM scm_oldpath, SCM scm_newpath);
SCM gf_chmod (SCM scm_volume, SCM scm_pathname, SCM scm_mode);
SCM gf_chown (SCM scm_volume, SCM scm_pathname, SCM scm_uid, SCM scm_gid);
SCM gf_truncate (SCM scm_volume, SCM scm_pathname, SCM scm_offset);
SCM gf_utime (SCM scm_volume, SCM scm_pathname);
SCM gf_read (SCM scm_ctxt, SCM scm_len, SCM scm_offset);
SCM gf_write (SCM scm_ctxt, SCM scm_buffer, SCM scm_len, SCM scm_offset);
SCM gf_statfs (SCM scm_volume, SCM scm_pathname);
SCM gf_flush (SCM scm_context);
SCM gf_release (SCM scm_context);
SCM gf_fsync (SCM scm_context, SCM scm_fdatasync);
SCM gf_setxattr (void);
SCM gf_getxattr (void);
SCM gf_listxattr (void);
SCM gf_removexattr (void);
SCM gf_opendir (SCM scm_volume, SCM scm_pathname);
SCM gf_readdir (SCM scm_volume, SCM scm_pathname);
SCM gf_releasedir (SCM scm_volume, SCM scm_context);
SCM gf_fsyncdir (void);
SCM gf_access (void);
SCM gf_create (void);
SCM gf_ftruncate (void);
SCM gf_fgetattr (void);
SCM gf_close (SCM scm_ctxt);
SCM gf_stats (SCM scm_volume);
