#include "glusterfs.h"
#include "xlator.h"

int glusterfsd_getattr (FILE *fp);
int glusterfsd_readlink (FILE *fp);
int glusterfsd_mknod (FILE *fp);
int glusterfsd_mkdir (FILE *fp);
int glusterfsd_unlink (FILE *fp);
int glusterfsd_rmdir (FILE *fp);
int glusterfsd_symlink (FILE *fp);
int glusterfsd_rename (FILE *fp);
int glusterfsd_link (FILE *fp);
int glusterfsd_chmod (FILE *fp);
int glusterfsd_chown (FILE *fp);
int glusterfsd_truncate (FILE *fp);
int glusterfsd_utime (FILE *fp);
int glusterfsd_open (FILE *fp);
int glusterfsd_read (FILE *fp);
int glusterfsd_write (FILE *fp);
int glusterfsd_statfs (FILE *fp);
int glusterfsd_flush (FILE *fp);
int glusterfsd_release (FILE *fp);
int glusterfsd_fsync (FILE *fp);
int glusterfsd_setxattr (FILE *fp);
int glusterfsd_getxattr (FILE *fp);
int glusterfsd_listxattr (FILE *fp);
int glusterfsd_removexattr (FILE *fp);
int glusterfsd_opendir (FILE *fp);
int glusterfsd_readdir (FILE *fp);
int glusterfsd_releasedir (FILE *fp);
int glusterfsd_fsyncdir (FILE *fp);
int glusterfsd_init (FILE *fp);
int glusterfsd_destroy (FILE *fp);
int glusterfsd_access (FILE *fp);
int glusterfsd_create (FILE *fp);
int glusterfsd_ftruncate (FILE *fp);
int glusterfsd_fgetattr (FILE *fp);

struct gfsd_fops {
  int (*function) (FILE *fp);
};

typedef struct gfsd_fops glusterfsd_fops_t;

int server_fs_loop (glusterfsd_fops_t *gfsd, FILE *fp);
struct xlator *get_xlator_tree_node (void);

