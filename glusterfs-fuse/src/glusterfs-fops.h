#ifndef __GLUSTERFS_FOPS_H__
#define __GLUSTERFS_FOPS_H__

#define GLUSTERFS_DEFAULT_NOPTS 1
#define GLUSTERFS_NAME "glusterfs"
#define GLUSTERFS_MINUSO "-o"
#define GLUSTERFS_MINUSF "-f"

#define SPEC_LOCAL_FILE      1
#define SPEC_REMOTE_FILE     2

/* looks ugly, but is very neat */
struct spec_location {
  int where;
  union {
    char *file;
    struct {
      char *ip;
      char *port;
    }server;
  }spec;
};

int glusterfs_mount (struct spec_location *spec, char *mount_point, char *options);
#endif /* __GLUSTERFS_FOPS_H__ */
