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

#ifndef __GLUSTERFS_FOPS_H__
#define __GLUSTERFS_FOPS_H__

#define GLUSTERFS_DEFAULT_NOPTS 1
#define GLUSTERFS_NAME      "glusterfs"
#define GLUSTERFS_MINUSO    "-o"
#define GLUSTERFS_MINUSF    "-f"

#define GLUSTERFS_DEBUG     "debug"
/* hard-coded mount options */
#define DEFAULT_PERMISSIONS "default_permissions"
#define ALLOW_OTHER         "allow_other"
#define NONEMPTY            "nonempty"
#define HARD_REMOVE         "hard_remove"

#define DEFAULT_LOG_FILE   DATADIR"/log/glusterfs/glusterfs.log"

#define SPEC_LOCAL_FILE      1
#define SPEC_REMOTE_FILE     2

#define GF_YES 1
#define GF_NO  0
#ifdef GF_LOG_FUSE_ARGS
#undef GF_LOG_FUSE_ARGS
#endif

#define GF_LOG_FUSE_ARGS(args, index) do{\
                                          int32_t local_index = 0;\
                                          int8_t local_buffer[1024*2] = {0,};\
                                          while (local_index < index) {\
					    strncat (local_buffer, args[local_index], strlen (args[local_index]));\
                                            strncat (local_buffer, " ", strlen (" "));\
					    local_index++;\
					  }\
                                          gf_log ("glusterfsd", GF_LOG_DEBUG, "glusterfs-fops.c->glusterfs_mount: arguments to fuse - \"%s\"", local_buffer);\
                                      }while (0);



/* looks ugly, but is very neat */
struct spec_location {
  int32_t where;
  union {
    int8_t *file;
    struct {
      int8_t *ip;
      int8_t *port;
    }server;
  }spec;
};

int glusterfs_mount (struct spec_location *spec, char *mount_point, char *options);

extern int gf_cmd_def_daemon_mode;
#endif /* __GLUSTERFS_FOPS_H__ */
