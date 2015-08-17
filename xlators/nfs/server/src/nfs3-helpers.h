/*
  Copyright (c) 2010-2011 Gluster, Inc. <http://www.gluster.com>
  This file is part of GlusterFS.

  This file is licensed to you under your choice of the GNU Lesser
  General Public License, version 3 or any later version (LGPLv3 or
  later), or the GNU General Public License, version 2 (GPLv2), in all
  cases as published by the Free Software Foundation.
*/

#ifndef _NFS3_HELPER_H_
#define _NFS3_HELPER_H_

#include "xlator.h"
#include "nfs3.h"
#include "nfs3-fh.h"
#include "msg-nfs3.h"
#include "xdr-nfs3.h"

#include <sys/statvfs.h>

#define GF_NFS3_FD_CACHED       0xcaced

extern struct nfs3_fh
nfs3_extract_lookup_fh (lookup3args *args);

extern char *
nfs3_extract_lookup_name (lookup3args *args);

extern nfsstat3
nfs3_errno_to_nfsstat3 (int errnum);

extern nfsstat3
nfs3_cbk_errno_status (int32_t, int32_t);

extern void
nfs3_fill_lookup3res (lookup3res *res, nfsstat3 stat, struct nfs3_fh *newfh,
                      struct iatt *stbuf, struct iatt *postparent,
                      uint64_t deviceid);

extern post_op_attr
nfs3_stat_to_post_op_attr (struct iatt *buf);

extern struct nfs3_fh
nfs3_extract_getattr_fh (getattr3args *args);

extern void
nfs3_fill_getattr3res (getattr3res *res, nfsstat3 stat, struct iatt *buf,
                       uint64_t deviceid);

extern struct nfs3_fh
nfs3_extract_fsinfo_fh (fsinfo3args *args);

extern void
nfs3_fill_fsinfo3res (struct nfs3_state *nfs3, fsinfo3res *res,
                      nfsstat3 status, struct iatt *fsroot,uint64_t deviceid);

/* Functions containing _prep_ are used specifically to work around
 * the memory allocations that happen inside Sun RPC library.
 * In that library, there are numerous places where every NFS request
 * can result in really tiny malloc calls. I fear the memory fragmentation
 * that will follow. After studying the points at and the way in which malloc
 * is called in Sun RPC, I've come up with this work-around. It is based on
 * the idea that if the user/caller of the xdr_to_XXXXargs functions can provide
 * already allocated memory or provide references to memory areas on its stack
 * just for the short-term purpose of decoding the message from XDR format, we
 * can avoid the memory allocations in Sun RPC. This is based on the fact
 * that Sun RPC first checks whether structure members which require memory
 * are NULL or not and only then calls malloc. In this case, if the caller
 * provided references are non-NULL, then the if-branches containing malloc
 * in Sun RPC will be avoided.
 * PS: You're not expected to understand this unless you've spent some time
 * looking through the glibc/sunrpc sources.
 */
extern void
nfs3_prep_lookup3args (lookup3args *args, struct nfs3_fh *fh, char *name);

extern void
nfs3_prep_getattr3args (getattr3args *args, struct nfs3_fh *fh);

extern void
nfs3_prep_fsinfo3args (fsinfo3args *args, struct nfs3_fh *root);

extern char *
nfsstat3_strerror(int stat);

extern void
nfs3_prep_access3args (access3args *args, struct nfs3_fh *fh);

extern void
nfs3_fill_access3res (access3res *res, nfsstat3 status, int32_t accbits,
		      int32_t reqaccbits);

extern char *
nfs3_fhcache_getpath (struct nfs3_state *nfs3, struct nfs3_fh *fh);

extern int
nfs3_fhcache_putpath (struct nfs3_state *nfs3, struct nfs3_fh *fh, char *path);

extern void
nfs3_prep_readdir3args (readdir3args *ra, struct nfs3_fh *fh);

extern void
nfs3_fill_readdir3res (readdir3res *res, nfsstat3 stat, struct nfs3_fh *dfh,
                       uint64_t cverf, struct iatt *dirstat,
                       gf_dirent_t *entries, count3 count, int is_eof,
                       uint64_t deviceid);

extern void
nfs3_prep_readdirp3args (readdirp3args *ra, struct nfs3_fh *fh);

extern void
nfs3_fill_readdirp3res (readdirp3res *res, nfsstat3 stat,
                        struct nfs3_fh *dirfh, uint64_t cverf,
                        struct iatt *dirstat, gf_dirent_t *entries,
                        count3 dircount, count3 maxcount, int is_eof,
                        uint64_t deviceid);

extern void
nfs3_free_readdirp3res (readdirp3res *res);

extern void
nfs3_free_readdir3res (readdir3res *res);

extern void
nfs3_prep_fsstat3args (fsstat3args *args, struct nfs3_fh *fh);

extern void
nfs3_fill_fsstat3res (fsstat3res *res, nfsstat3 stat, struct statvfs *fsbuf,
                      struct iatt *postbuf, uint64_t deviceid);

extern int32_t
nfs3_sattr3_to_setattr_valid (sattr3 *sattr, struct iatt *buf, mode_t *omode);
extern void
nfs3_fill_create3res (create3res *res, nfsstat3 stat, struct nfs3_fh *newfh,
                      struct iatt *newbuf, struct iatt *preparent,
                      struct iatt *postparent, uint64_t deviceid);

extern void
nfs3_prep_create3args (create3args *args, struct nfs3_fh *fh, char *name);

extern void
nfs3_prep_setattr3args (setattr3args *args, struct nfs3_fh *fh);

extern void
nfs3_fill_setattr3res (setattr3res *res, nfsstat3 stat, struct iatt *preop,
                       struct iatt *postop, uint64_t deviceid);

extern void
nfs3_prep_mkdir3args (mkdir3args *args, struct nfs3_fh *dirfh, char *name);

extern void
nfs3_fill_mkdir3res (mkdir3res *res, nfsstat3 stat, struct nfs3_fh *fh,
                     struct iatt *buf, struct iatt *preparent,
                     struct iatt *postparent, uint64_t deviceid);

extern void
nfs3_prep_symlink3args (symlink3args *args, struct nfs3_fh *dirfh, char *name,
                        char *target);

extern void
nfs3_fill_symlink3res (symlink3res *res, nfsstat3 stat, struct nfs3_fh *fh,
                       struct iatt *buf, struct iatt *preparent,
                       struct iatt *postparent, uint64_t deviceid);

extern void
nfs3_prep_readlink3args (readlink3args *args, struct nfs3_fh *fh);

extern void
nfs3_fill_readlink3res (readlink3res *res, nfsstat3 stat, char *path,
                        struct iatt *buf, uint64_t deviceid);

extern void
nfs3_prep_mknod3args (mknod3args *args, struct nfs3_fh *fh, char *name);

extern void
nfs3_fill_mknod3res (mknod3res *res, nfsstat3 stat, struct nfs3_fh *fh,
                     struct iatt *buf, struct iatt *preparent,
                     struct iatt *postparent, uint64_t deviceid);

extern void
nfs3_fill_remove3res (remove3res *res, nfsstat3 stat, struct iatt *preparent,
                      struct iatt *postparent, uint64_t deviceid);
extern void
nfs3_prep_remove3args (remove3args *args, struct nfs3_fh *fh, char *name);

extern void
nfs3_fill_rmdir3res (rmdir3res *res, nfsstat3 stat, struct iatt *preparent,
                     struct iatt *postparent, uint64_t deviceid);

extern void
nfs3_prep_rmdir3args (rmdir3args *args, struct nfs3_fh *fh, char *name);

extern void
nfs3_fill_link3res (link3res *res, nfsstat3 stat, struct iatt *buf,
                    struct iatt *preparent, struct iatt *postparent,
                    uint64_t deviceid);

extern void
nfs3_prep_link3args (link3args *args, struct nfs3_fh *target,
                     struct nfs3_fh * dirfh, char *name);

extern void
nfs3_prep_rename3args (rename3args *args, struct nfs3_fh *olddirfh,
                       char *oldname, struct nfs3_fh *newdirfh,
                       char *newname);

extern void
nfs3_fill_rename3res (rename3res *res, nfsstat3 stat, struct iatt *buf,
                      struct iatt *preoldparent, struct iatt *postoldparent,
                      struct iatt *prenewparent, struct iatt *postnewparent,
                      uint64_t deviceid);

extern void
nfs3_prep_write3args (write3args *args, struct nfs3_fh *fh);

extern void
nfs3_fill_write3res (write3res *res, nfsstat3 stat, count3 count,
                     stable_how stable, uint64_t wverf, struct iatt *prestat,
                     struct iatt *poststat, uint64_t deviceid);

extern void
nfs3_prep_commit3args (commit3args *args, struct nfs3_fh *fh);

extern void
nfs3_fill_commit3res (commit3res *res, nfsstat3 stat, uint64_t wverf,
                      struct iatt *prestat, struct iatt *poststat,
                      uint64_t deviceid);

extern void
nfs3_fill_read3res (read3res *res, nfsstat3 stat, count3 count,
                    struct iatt *poststat, int is_eof, uint64_t deviceid);

extern void
nfs3_prep_read3args (read3args *args, struct nfs3_fh *fh);

extern void
nfs3_prep_pathconf3args (pathconf3args *args, struct nfs3_fh *fh);

extern void
nfs3_fill_pathconf3res (pathconf3res *res, nfsstat3 stat, struct iatt *buf,
                        uint64_t deviceid);

extern int
nfs3_cached_inode_opened (xlator_t *nfsxl, inode_t *inode);

extern void
nfs3_log_common_res (uint32_t xid, int op, nfsstat3 stat, int pstat,
                     const char *path);

extern void
nfs3_log_readlink_res (uint32_t xid, nfsstat3 stat, int pstat,
                       char *linkpath, const char *path);

extern void
nfs3_log_read_res (uint32_t xid, nfsstat3 stat, int pstat,
                   count3 count, int is_eof, struct iovec *vec,
                   int32_t vcount, const char *path);

extern void
nfs3_log_write_res (uint32_t xid, nfsstat3 stat, int pstat, count3 count,
                    int stable, uint64_t wverf, const char *path);

extern void
nfs3_log_newfh_res (uint32_t xid, int op, nfsstat3 stat, int pstat,
                    struct nfs3_fh *newfh, const char *path);

extern void
nfs3_log_readdir_res (uint32_t xid, nfsstat3 stat, int pstat, uint64_t cverf,
                      count3 count, int is_eof, const char *path);

extern void
nfs3_log_readdirp_res (uint32_t xid, nfsstat3 stat, int pstat, uint64_t cverf,
                       count3 dircount, count3 maxcount, int is_eof,
                       const char *path);

extern void
nfs3_log_commit_res (uint32_t xid, nfsstat3 stat, int pstat, uint64_t wverf,
                     const char *path);

extern void
nfs3_log_common_call (uint32_t xid, char *op, struct nfs3_fh *fh);

extern void
nfs3_log_fh_entry_call (uint32_t xid, char *op, struct nfs3_fh *fh,
                        char *name);

extern void
nfs3_log_rw_call (uint32_t xid, char *op, struct nfs3_fh *fh, offset3 offt,
                  count3 count, int stablewrite);

extern void
nfs3_log_create_call (uint32_t xid, struct nfs3_fh *fh, char *name,
                      createmode3 mode);

extern void
nfs3_log_symlink_call (uint32_t xid, struct nfs3_fh *fh, char *name, char *tgt);

extern void
nfs3_log_mknod_call (uint32_t xid, struct nfs3_fh *fh, char *name, int type);

extern void
nfs3_log_rename_call (uint32_t xid, struct nfs3_fh *src, char *sname,
                      struct nfs3_fh *dst, char *dname);

extern void
nfs3_log_link_call (uint32_t xid, struct nfs3_fh *fh, char *name,
                    struct nfs3_fh *tgt);

extern void
nfs3_log_readdir_call (uint32_t xid, struct nfs3_fh *fh, count3 dircount,
                       count3 maxcount);

extern int
nfs3_fh_resolve_entry_hard (nfs3_call_state_t *cs);

extern int
nfs3_fh_resolve_inode (nfs3_call_state_t *cs);

extern int
nfs3_fh_resolve_entry (nfs3_call_state_t *cs);

extern int
nfs3_fh_resolve_and_resume (nfs3_call_state_t *cs, struct nfs3_fh *fh,
                            char *entry, nfs3_resume_fn_t resum_fn);

extern int
nfs3_verify_dircookie (struct nfs3_state *nfs3, fd_t *dirfd, cookie3 cookie,
                       uint64_t cverf, nfsstat3 *stat);

extern int
nfs3_is_parentdir_entry (char *entry);

uint32_t
nfs3_request_to_accessbits (int32_t accbits);

extern int
nfs3_fh_auth_nfsop (nfs3_call_state_t *cs, gf_boolean_t is_write_op);

void
nfs3_map_deviceid_to_statdev (struct iatt *ia, uint64_t deviceid);

#endif
