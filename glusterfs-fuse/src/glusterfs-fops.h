#ifndef __GLUSTERFS_FOPS_H__
#define __GLUSTERFS_FOPS_H__

#define GLUSTERFS_DEFAULT_NOPTS 1
#define GLUSTERFS_NAME "glusterfs"
#define GLUSTERFS_MINUSO "-o"
#define GLUSTERFS_MINUSF "-f"
struct mt_options {
  char *mt_options;
  int nopts;
  struct mt_options *next;
};

#endif /* __GLUSTERFS_FOPS_H__ */
