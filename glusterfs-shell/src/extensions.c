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

#include "extensions.h"

SCM ex_gf_hi;
SCM ex_gf_command_hook;

void
register_primitives (void)
{
  
  gh_new_procedure1_0 ("gf-hello", gf_hello);
  gh_new_procedure1_0 ("gf-demo", gf_demo);

  /* init procedure */
  gh_new_procedure1_0 ("gf-init", ex_gf_init);
  
  /* file operations structure exposed by the xlator */
  gh_new_procedure3_0 ("gf-open", gf_open);
  gh_new_procedure2_0 ("gf-getattr", gf_getattr);
  gh_new_procedure2_0 ("gf-readlink", gf_readlink);
  gh_new_procedure5_0 ("gf-mknod", gf_mknod);
  gh_new_procedure2_0 ("gf-mkdir", gf_mkdir);
  gh_new_procedure2_0 ("gf-unlink", gf_unlink);
  gh_new_procedure2_0 ("gf-rmdir", gf_rmdir);
  gh_new_procedure3_0 ("gf-symlink", gf_symlink);
  gh_new_procedure3_0 ("gf-rename", gf_rename);
  gh_new_procedure3_0 ("gf-link", gf_link);
  gh_new_procedure3_0 ("gf-chmod", gf_chmod);
  gh_new_procedure4_0 ("gf-chown", gf_chown);
  gh_new_procedure3_0 ("gf-truncate", gf_truncate);
  gh_new_procedure2_0 ("gf-utime", gf_utime);
  gh_new_procedure3_0 ("gf-read", gf_read);
  gh_new_procedure4_0 ("gf-write", gf_write);
  gh_new_procedure2_0 ("gf-statfs", gf_statfs);
  gh_new_procedure1_0 ("gf-flush", gf_flush);
  gh_new_procedure1_0 ("gf-release", gf_release);
  gh_new_procedure2_0 ("gf-fsync", gf_fsync);
  gh_new_procedure0_0 ("gf-setxattr", gf_setxattr);
  gh_new_procedure0_0 ("gf-getxattr", gf_getxattr);
  gh_new_procedure0_0 ("gf-listxattr", gf_listxattr);
  gh_new_procedure0_0 ("gf-removexattr", gf_removexattr);
  gh_new_procedure2_0 ("gf-opendir", gf_opendir);
  gh_new_procedure2_0 ("gf-readdir", gf_readdir);
  gh_new_procedure2_0 ("gf-releasedir", gf_releasedir);
  gh_new_procedure0_0 ("gf-fsyncdir", gf_fsyncdir);
  gh_new_procedure0_0 ("gf-access", gf_access);
  gh_new_procedure0_0 ("gf-create", gf_create);
  gh_new_procedure0_0 ("gf-ftruncate", gf_ftruncate);
  gh_new_procedure2_0 ("gf-fgetattr", gf_getattr);
  gh_new_procedure1_0 ("gf-close", gf_close);
  gh_new_procedure1_0 ("gf-stats", gf_stats);
  /* end of file_operations */


  /* gh_new_procedure (scheme-name, c_func_name, required_args, opt_args, rest) */
}

void
register_hooks (void)
{
  ex_gf_hi = scm_create_hook ("gf-hi-hook", 1);
  ex_gf_command_hook = scm_create_hook ("gf-command-hook", 2);
} 

void 
gf_load (const char *file)
{
  char *path_prepend ="" ; /* FIXME: very much temporary, assumed that file provided will have absolute path or is in current directory */
  char *path = NULL;
  struct stat lstat;
  int pathlen = 0;

  pathlen = strlen (path_prepend) + strlen(file) + 1;
  path = calloc (pathlen, 1);
  sprintf (path, "%s%s", path_prepend, file);
  
  if (stat (path, &lstat) == 0){
    gh_load (path);
    free (path);
    return;
  }

  free (path);  
  printf ("%s not found\n", path);
}
