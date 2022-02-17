#include "server.h"
#include "glusterfs3.h"
#include <glusterfs/compat-errno.h>
#include "server-messages.h"

#ifdef BUILD_GNFS
#include "xdr-nfs3.h"
#endif

void
server4_post_readlink(gfx_readlink_rsp *rsp, struct iatt *stbuf,
                      const char *buf);

void
server4_post_statfs(gfx_statfs_rsp *rsp, struct statvfs *stbuf);

void
server4_post_lk(xlator_t *this, gfx_lk_rsp *rsp, struct gf_flock *lock);

int
server4_post_readdir(gfx_readdir_rsp *rsp, gf_dirent_t *entries);

void
server4_post_seek(gfx_seek_rsp *rsp, off_t offset);

int
server4_post_readdirp(gfx_readdirp_rsp *rsp, gf_dirent_t *entries);

void
server4_post_rchecksum(gfx_rchecksum_rsp *rsp, uint32_t weak_checksum,
                       uint8_t *strong_checksum);

void
server4_post_rename(call_frame_t *frame, server_state_t *state,
                    gfx_rename_rsp *rsp, struct iatt *stbuf,
                    struct iatt *preoldparent, struct iatt *postoldparent,
                    struct iatt *prenewparent, struct iatt *postnewparent);

int
server4_post_open(call_frame_t *frame, xlator_t *this, gfx_open_rsp *rsp,
                  fd_t *fd);
void
server4_post_readv(gfx_read_rsp *rsp, struct iatt *stbuf, int op_ret);

int
server4_post_create(call_frame_t *frame, gfx_create_rsp *rsp,
                    server_state_t *state, xlator_t *this, fd_t *fd,
                    inode_t *inode, struct iatt *stbuf, struct iatt *preparent,
                    struct iatt *postparent);

void
server4_post_common_2iatt(gfx_common_2iatt_rsp *rsp, struct iatt *stbuf1,
                          struct iatt *stbuf2);

void
server4_post_entry_remove(server_state_t *state, gfx_common_2iatt_rsp *rsp,
                          struct iatt *stbuf1, struct iatt *stbuf2);

void
server4_post_common_3iatt(server_state_t *state, gfx_common_3iatt_rsp *rsp,
                          inode_t *inode, struct iatt *stbuf, struct iatt *pre,
                          struct iatt *post);
void
server4_post_common_iatt(server_state_t *state, gfx_common_iatt_rsp *rsp,
                         struct iatt *stbuf);
void
server4_post_lease(gfx_lease_rsp *rsp, struct gf_lease *lease);
void
server4_post_lookup(gfx_common_2iatt_rsp *rsp, call_frame_t *frame,
                    server_state_t *state, inode_t *inode, struct iatt *stbuf,
                    dict_t *xdata);
void
server4_post_link(server_state_t *state, gfx_common_3iatt_rsp *rsp,
                  inode_t *inode, struct iatt *stbuf, struct iatt *pre,
                  struct iatt *post);

void
server4_post_common_3iatt_noinode(gfx_common_3iatt_rsp *rsp, struct iatt *stbuf,
                                  struct iatt *prebuf_dst,
                                  struct iatt *postbuf_dst);
