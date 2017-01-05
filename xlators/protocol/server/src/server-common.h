#include "server.h"
#include "defaults.h"
#include "rpc-common-xdr.h"
#include "glusterfs3-xdr.h"
#include "glusterfs3.h"
#include "compat-errno.h"
#include "server-messages.h"
#include "defaults.h"

#include "xdr-nfs3.h"
void
server_post_stat (gfs3_stat_rsp *rsp, struct iatt *stbuf);

void
server_post_readlink (gfs3_readlink_rsp *rsp, struct iatt *stbuf,
                      const char *buf);

void
server_post_mknod (server_state_t *state, gfs3_mknod_rsp *rsp,
                   struct iatt *stbuf, struct iatt *preparent,
                   struct iatt *postparent, inode_t *inode);
void
server_post_mkdir (server_state_t *state, gfs3_mkdir_rsp *rsp,
                   inode_t *inode,
                   struct iatt *stbuf, struct iatt *preparent,
                   struct iatt *postparent, dict_t *xdata);

void
server_post_unlink (server_state_t *state, gfs3_unlink_rsp *rsp,
                    struct iatt *preparent, struct iatt *postparent);
void
server_post_rmdir (server_state_t *state, gfs3_rmdir_rsp *rsp,
                    struct iatt *preparent, struct iatt *postparent);

void
server_post_symlink (server_state_t *state, gfs3_symlink_rsp *rsp,
                   inode_t *inode,
                   struct iatt *stbuf, struct iatt *preparent,
                   struct iatt *postparent, dict_t *xdata);
void
server_post_link (server_state_t *state, gfs3_link_rsp *rsp,
                   inode_t *inode,
                   struct iatt *stbuf, struct iatt *preparent,
                   struct iatt *postparent, dict_t *xdata);
void
server_post_truncate (gfs3_truncate_rsp *rsp, struct iatt *prebuf,
                      struct iatt *postbuf);

void
server_post_writev (gfs3_write_rsp *rsp, struct iatt *prebuf,
                      struct iatt *postbuf);
void
server_post_statfs (gfs3_statfs_rsp *rsp, struct statvfs *stbuf);

void
server_post_fsync (gfs3_fsync_rsp *rsp, struct iatt *prebuf,
                   struct iatt *postbuf);

void
server_post_ftruncate (gfs3_ftruncate_rsp *rsp, struct iatt *prebuf,
                      struct iatt *postbuf);

void
server_post_fstat (gfs3_fstat_rsp *rsp, struct iatt *stbuf);

void
server_post_lk (xlator_t *this, gfs3_lk_rsp *rsp, struct gf_flock *lock);

int
server_post_readdir (gfs3_readdir_rsp *rsp, gf_dirent_t *entries);

void
server_post_zerofill (gfs3_zerofill_rsp *rsp, struct iatt *statpre,
                      struct iatt *statpost);

void
server_post_discard (gfs3_discard_rsp *rsp, struct iatt *statpre,
                      struct iatt *statpost);

void
server_post_fallocate (gfs3_fallocate_rsp *rsp, struct iatt *statpre,
                       struct iatt *statpost);

void
server_post_seek (gfs3_seek_rsp *rsp, off_t offset);

int
server_post_readdirp (gfs3_readdirp_rsp *rsp, gf_dirent_t *entries);

void
server_post_fsetattr (gfs3_fsetattr_rsp *rsp, struct iatt *statpre,
                       struct iatt *statpost);

void
server_post_setattr (gfs3_setattr_rsp *rsp, struct iatt *statpre,
                     struct iatt *statpost);

void
server_post_rchecksum (gfs3_rchecksum_rsp *rsp, uint32_t weak_checksum,
                       uint8_t *strong_checksum);

void
server_post_rename (call_frame_t *frame, server_state_t *state,
                    gfs3_rename_rsp *rsp,
                    struct iatt *stbuf,
                    struct iatt *preoldparent,
                    struct iatt *postoldparent,
                    struct iatt *prenewparent,
                    struct iatt *postnewparent);

int
server_post_open (call_frame_t *frame, xlator_t *this,
                  gfs3_open_rsp *rsp, fd_t *fd);
void
server_post_readv (gfs3_read_rsp *rsp, struct iatt *stbuf, int op_ret);

int
server_post_opendir (call_frame_t *frame, xlator_t *this,
                     gfs3_opendir_rsp *rsp, fd_t *fd);

int
server_post_create (call_frame_t *frame, gfs3_create_rsp *rsp,
                    server_state_t *state,
                    xlator_t *this, fd_t *fd, inode_t *inode,
                    struct iatt *stbuf, struct iatt *preparent,
                    struct iatt *postparent);

void
server_post_lookup (gfs3_lookup_rsp *rsp, call_frame_t *frame,
                    server_state_t *state,
                    inode_t *inode, struct iatt *stbuf,
                    struct iatt *postparent);

void
server_post_lease (gfs3_lease_rsp *rsp, struct gf_lease *lease);
