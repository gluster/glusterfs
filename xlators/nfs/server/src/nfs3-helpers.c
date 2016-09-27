/*
  Copyright (c) 2010-2011 Gluster, Inc. <http://www.gluster.com>
  This file is part of GlusterFS.

  This file is licensed to you under your choice of the GNU Lesser
  General Public License, version 3 or any later version (LGPLv3 or
  later), or the GNU General Public License, version 2 (GPLv2), in all
  cases as published by the Free Software Foundation.
*/

#include <inttypes.h>

#include "xlator.h"
#include "nfs3.h"
#include "nfs3-fh.h"
#include "msg-nfs3.h"
#include "rbthash.h"
#include "nfs-fops.h"
#include "nfs-inodes.h"
#include "nfs-generics.h"
#include "nfs3-helpers.h"
#include "nfs-mem-types.h"
#include "iatt.h"
#include "common-utils.h"
#include "nfs-messages.h"
#include "mount3.h"
#include <string.h>

extern int
nfs3_set_root_looked_up (struct nfs3_state *nfs3, struct nfs3_fh *rootfh);

extern int
nfs3_is_root_looked_up (struct nfs3_state *nfs3, struct nfs3_fh *rootfh);


#define nfs3_call_resume(cst)                                   \
        do {                                                    \
                if (((cst)) && (cst)->resume_fn)                \
                        (cst)->resume_fn (cst);                 \
        } while (0)                                             \

#define nfs3_call_resume_estale(csta)                           \
        do {                                                    \
                (csta)->resolve_ret = -1;                       \
                (csta)->resolve_errno = ESTALE;                 \
                nfs3_call_resume (csta);                        \
        } while (0)                                             \

struct nfs3stat_strerror {
        nfsstat3        stat;
        char            strerror[100];
};


struct nfs3stat_strerror nfs3stat_strerror_table[] = {
        { NFS3_OK,              "Call completed successfully."          },
        { NFS3ERR_PERM,         "Not owner"                             },
        { NFS3ERR_NOENT,        "No such file or directory"             },
        { NFS3ERR_IO,           "I/O error"                             },
        { NFS3ERR_NXIO,         "I/O error"                             },
        { NFS3ERR_ACCES,        "Permission denied"                     },
        { NFS3ERR_EXIST,        "File exists"                           },
        { NFS3ERR_XDEV,         "Attempt to do a cross-device hard link"},
        { NFS3ERR_NODEV,        "No such device"                        },
        { NFS3ERR_NOTDIR,       "Not a directory"                       },
        { NFS3ERR_ISDIR,        "Is a directory"                        },
        { NFS3ERR_INVAL,        "Invalid argument for operation"        },
        { NFS3ERR_FBIG,         "File too large"                        },
        { NFS3ERR_NOSPC,        "No space left on device"               },
        { NFS3ERR_ROFS,         "Read-only file system"                 },
        { NFS3ERR_MLINK,        "Too many hard links"                   },
        { NFS3ERR_NAMETOOLONG,  "Filename in operation was too long"    },
        { NFS3ERR_NOTEMPTY,     "Directory not empty"                   },
        { NFS3ERR_DQUOT,        "Resource (quota) hard limit exceeded"  },
        { NFS3ERR_STALE,        "Invalid file handle"                   },
        { NFS3ERR_REMOTE,       "Too many levels of remote in path"     },
        { NFS3ERR_BADHANDLE,    "Illegal NFS file handle"               },
        { NFS3ERR_NOT_SYNC,     "Update synchronization mismatch detected" },
        { NFS3ERR_BAD_COOKIE,   "READDIR or READDIRPLUS cookie is stale"},
        { NFS3ERR_NOTSUPP,      "Operation is not supported"            },
        { NFS3ERR_TOOSMALL,     "Buffer or request is too small"        },
        { NFS3ERR_SERVERFAULT,  "Error occurred on the server or IO Error" },
        { NFS3ERR_BADTYPE,      "Type not supported by the server"      },
        { NFS3ERR_JUKEBOX,      "Cannot complete server initiated request" },
        { NFS3ERR_END_OF_LIST,  "IO Error"                              },

};


uint64_t
nfs3_iatt_gfid_to_ino (struct iatt *buf)
{
        uint64_t ino  = 0;

        if (!buf)
                return 0;

        if (gf_nfs_enable_ino32()) {
                ino = (uint32_t )nfs_hash_gfid (buf->ia_gfid);
                goto hashout;
        }

        /* from posix its guaranteed to send unique ino */
        ino = buf->ia_ino;

hashout:
        return ino;
}


void
nfs3_map_deviceid_to_statdev (struct iatt *ia, uint64_t deviceid)
{
        if (!ia)
                return;

        ia->ia_dev = deviceid;
}


struct nfs3_fh
nfs3_extract_nfs3_fh (nfs_fh3 fh)
{
        struct nfs3_fh          gfh;

        memcpy (&gfh, fh.data.data_val, fh.data.data_len);
        return gfh;
}


struct nfs3_fh
nfs3_extract_lookup_fh (lookup3args *args)
{
        return nfs3_extract_nfs3_fh(args->what.dir);
}


char *
nfs3_extract_lookup_name (lookup3args *args)
{
        return args->what.name;
}


nfsstat3
nfs3_errno_to_nfsstat3 (int errnum)
{
        nfsstat3        stat = NFS3_OK;

        switch (errnum) {

        case 0:
                stat = NFS3_OK;
                break;

        case EPERM:
                stat = NFS3ERR_PERM;
                break;

        case ENOENT:
                stat = NFS3ERR_NOENT;
                break;

        case EACCES:
                stat = NFS3ERR_ACCES;
                break;

        case EEXIST:
                stat = NFS3ERR_EXIST;
                break;

        case EXDEV:
                stat = NFS3ERR_XDEV;
                break;

        case ENODEV:
                stat = NFS3ERR_NODEV;
                break;

        case EIO:
                stat = NFS3ERR_IO;
                break;

        case ENXIO:
                stat = NFS3ERR_NXIO;
                break;

        case ENOTDIR:
                stat = NFS3ERR_NOTDIR;
                break;

        case EISDIR:
                stat = NFS3ERR_ISDIR;
                break;

        case EINVAL:
                stat = NFS3ERR_INVAL;
                break;

        case ENOSPC:
                stat = NFS3ERR_NOSPC;
                break;

        case EROFS:
                stat = NFS3ERR_ROFS;
                break;

        case EFBIG:
                stat = NFS3ERR_FBIG;
                break;

        case EMLINK:
                stat = NFS3ERR_MLINK;
                break;

        case ENAMETOOLONG:
                stat = NFS3ERR_NAMETOOLONG;
                break;

        case ENOTEMPTY:
                stat = NFS3ERR_NOTEMPTY;
                break;

        case EFAULT:
                stat = NFS3ERR_SERVERFAULT;
                break;

        case ENOTSUP:
        case ENOSYS:
                stat = NFS3ERR_NOTSUPP;
                break;

        case EBADF:
                stat = NFS3ERR_BADTYPE;
                break;

        case ESTALE:
                stat = NFS3ERR_STALE;
                break;

        case ENOTCONN:
                stat = NFS3ERR_IO;
                break;

        case EDQUOT:
                stat = NFS3ERR_DQUOT;
                break;

        default:
                stat = NFS3ERR_SERVERFAULT;
                break;
        }

        return stat;
}

/*
 * Special case: If op_ret is -1, it's very unusual op_errno being
 * 0 which means something came wrong from upper layer(s). If it
 * happens by any means, then set NFS3 status to NFS3ERR_SERVERFAULT.
 */
nfsstat3
nfs3_cbk_errno_status (int32_t op_ret, int32_t op_errno)
{
        if ((op_ret == -1) && (op_errno == 0)) {
                return NFS3ERR_SERVERFAULT;
        }

        return nfs3_errno_to_nfsstat3 (op_errno);
}

void
nfs3_fill_lookup3res_error (lookup3res *res, nfsstat3 stat,
                            struct iatt *dirstat)
{

        memset (res, 0, sizeof (*res));
        res->status = stat;
        if (!dirstat) {
                res->lookup3res_u.resfail.dir_attributes.attributes_follow = FALSE;
        }

}

void
nfs3_stat_to_fattr3 (struct iatt *buf, fattr3 *fa)
{
        if (buf == NULL || fa == NULL) {
                errno = EINVAL;
                return;
        }

        if (IA_ISDIR (buf->ia_type))
                fa->type = NF3DIR;
        else if (IA_ISREG (buf->ia_type))
                fa->type = NF3REG;
        else if (IA_ISCHR (buf->ia_type))
                fa->type = NF3CHR;
        else if (IA_ISBLK (buf->ia_type))
                fa->type = NF3BLK;
        else if (IA_ISFIFO (buf->ia_type))
                fa->type = NF3FIFO;
        else if (IA_ISLNK (buf->ia_type))
                fa->type = NF3LNK;
        else if (IA_ISSOCK (buf->ia_type))
                fa->type = NF3SOCK;

        if (IA_PROT_RUSR (buf->ia_prot))
                fa->mode |= NFS3MODE_ROWNER;
        if (IA_PROT_WUSR (buf->ia_prot))
                fa->mode |= NFS3MODE_WOWNER;
        if (IA_PROT_XUSR (buf->ia_prot))
                fa->mode |= NFS3MODE_XOWNER;

        if (IA_PROT_RGRP (buf->ia_prot))
                fa->mode |= NFS3MODE_RGROUP;
        if (IA_PROT_WGRP (buf->ia_prot))
                fa->mode |= NFS3MODE_WGROUP;
        if (IA_PROT_XGRP (buf->ia_prot))
                fa->mode |= NFS3MODE_XGROUP;

        if (IA_PROT_ROTH (buf->ia_prot))
                fa->mode |= NFS3MODE_ROTHER;
        if (IA_PROT_WOTH (buf->ia_prot))
                fa->mode |= NFS3MODE_WOTHER;
        if (IA_PROT_XOTH (buf->ia_prot))
                fa->mode |= NFS3MODE_XOTHER;

        if (IA_PROT_SUID (buf->ia_prot))
                fa->mode |= NFS3MODE_SETXUID;
        if (IA_PROT_SGID (buf->ia_prot))
                fa->mode |= NFS3MODE_SETXGID;
        if (IA_PROT_STCKY (buf->ia_prot))
                fa->mode |= NFS3MODE_SAVESWAPTXT;

        fa->nlink = buf->ia_nlink;
        fa->uid = buf->ia_uid;
        fa->gid = buf->ia_gid;
        fa->size = buf->ia_size;
        fa->used = (buf->ia_blocks * 512);

        if ((IA_ISCHR (buf->ia_type) || IA_ISBLK (buf->ia_type))) {
                fa->rdev.specdata1 = ia_major (buf->ia_rdev);
                fa->rdev.specdata2 = ia_minor (buf->ia_rdev);
        } else {
                fa->rdev.specdata1 = 0;
                fa->rdev.specdata2 = 0;
        }

        fa->fsid = buf->ia_dev;
        fa->fileid = nfs3_iatt_gfid_to_ino (buf);

        fa->atime.seconds = buf->ia_atime;
        fa->atime.nseconds = buf->ia_atime_nsec;

        fa->ctime.seconds = buf->ia_ctime;
        fa->ctime.nseconds = buf->ia_ctime_nsec;

        fa->mtime.seconds = buf->ia_mtime;
        fa->mtime.nseconds = buf->ia_mtime_nsec;
}


post_op_attr
nfs3_stat_to_post_op_attr (struct iatt *buf)
{
        post_op_attr    attr = {0, };
        if (!buf)
                return attr;

        /* Some performance translators return zero-filled stats when they
         * do not have up-to-date attributes. Need to handle this by not
         * returning these zeroed out attrs.
         */
        attr.attributes_follow = FALSE;
        if (gf_is_zero_filled_stat (buf))
                goto out;

        nfs3_stat_to_fattr3 (buf, &(attr.post_op_attr_u.attributes));
        attr.attributes_follow = TRUE;

out:
        return attr;
}


pre_op_attr
nfs3_stat_to_pre_op_attr (struct iatt *pre)
{
        pre_op_attr     poa = {0, };

        /* Some performance translators return zero-filled stats when they
         * do not have up-to-date attributes. Need to handle this by not
         * returning these zeroed out attrs.
         */
        poa.attributes_follow = FALSE;
        if (gf_is_zero_filled_stat (pre))
                goto out;

        poa.attributes_follow = TRUE;
        poa.pre_op_attr_u.attributes.size = pre->ia_size;
        poa.pre_op_attr_u.attributes.mtime.seconds = pre->ia_mtime;
        poa.pre_op_attr_u.attributes.mtime.nseconds = pre->ia_mtime_nsec;
        poa.pre_op_attr_u.attributes.ctime.seconds = pre->ia_ctime;
        poa.pre_op_attr_u.attributes.ctime.nseconds = pre->ia_ctime_nsec;

out:
        return poa;
}

void
nfs3_fill_lookup3res_success (lookup3res *res, nfsstat3 stat,
                              struct nfs3_fh *fh, struct iatt *buf,
                              struct iatt *postparent)
{
        post_op_attr    obj, dir;
        uint32_t        fhlen = 0;

        res->status = stat;
        if (fh) {
                res->lookup3res_u.resok.object.data.data_val = (void *)fh;
                fhlen = nfs3_fh_compute_size ();
                res->lookup3res_u.resok.object.data.data_len = fhlen;
        }

        obj.attributes_follow = FALSE;
        dir.attributes_follow = FALSE;
        obj = nfs3_stat_to_post_op_attr (buf);
        dir = nfs3_stat_to_post_op_attr (postparent);

        res->lookup3res_u.resok.obj_attributes = obj;
        res->lookup3res_u.resok.dir_attributes = dir;
}


void
nfs3_fill_lookup3res (lookup3res *res, nfsstat3 stat, struct nfs3_fh *newfh,
                      struct iatt *buf, struct iatt *postparent,
                      uint64_t deviceid)
{

        memset (res, 0, sizeof (*res));
        nfs3_map_deviceid_to_statdev (buf, deviceid);
        nfs3_map_deviceid_to_statdev (postparent, deviceid);
        if (stat != NFS3_OK)
                nfs3_fill_lookup3res_error (res, stat, postparent);
        else
                nfs3_fill_lookup3res_success (res, stat, newfh, buf,
                                              postparent);
}

struct nfs3_fh
nfs3_extract_getattr_fh (getattr3args *args)
{
        return nfs3_extract_nfs3_fh(args->object);
}


void
nfs3_fill_getattr3res (getattr3res *res, nfsstat3 stat, struct iatt *buf,
                       uint64_t deviceid)
{

        memset (res, 0, sizeof (*res));
        res->status = stat;
        if (stat != NFS3_OK)
                return;

        nfs3_map_deviceid_to_statdev (buf, deviceid);
        nfs3_stat_to_fattr3 (buf, &(res->getattr3res_u.resok.obj_attributes));
}


struct nfs3_fh
nfs3_extract_fsinfo_fh (fsinfo3args *args)
{
        return nfs3_extract_nfs3_fh (args->fsroot);
}


void
nfs3_fill_fsinfo3res (struct nfs3_state *nfs3, fsinfo3res *res,
                      nfsstat3 status, struct iatt *fsroot, uint64_t deviceid)
{
        fsinfo3resok    resok = {{0}, };
        nfstime3        tdelta = GF_NFS3_TIMEDELTA_SECS;

        memset (res, 0, sizeof (*res));
        res->status = status;
        if (status != NFS3_OK)
                return;

        nfs3_map_deviceid_to_statdev (fsroot, deviceid);
        resok.obj_attributes = nfs3_stat_to_post_op_attr (fsroot);
        resok.rtmax = nfs3->readsize;
        resok.rtpref = nfs3->readsize;
        resok.rtmult = GF_NFS3_RTMULT;
        resok.wtmax = nfs3->writesize;
        resok.wtpref = nfs3->writesize;
        resok.wtmult = GF_NFS3_WTMULT;
        resok.dtpref = nfs3->readdirsize;
        resok.maxfilesize = GF_NFS3_MAXFILESIZE;
        resok.time_delta = tdelta;
        resok.properties = GF_NFS3_FS_PROP;

        res->fsinfo3res_u.resok = resok;

}


void
nfs3_prep_lookup3args (lookup3args *args, struct nfs3_fh *fh, char *name)
{
        memset (args, 0, sizeof (*args));
        args->what.dir.data.data_val = (void *)fh;
        args->what.name = name;
}


void
nfs3_prep_getattr3args (getattr3args *args, struct nfs3_fh *fh)
{
        memset (args, 0, sizeof (*args));
        args->object.data.data_val = (void *)fh;
}


void
nfs3_prep_fsinfo3args (fsinfo3args *args, struct nfs3_fh *root)
{
        memset (args, 0, sizeof (*args));
        args->fsroot.data.data_val = (void *)root;
}


char *
nfsstat3_strerror(int stat)
{
        int i;
        for(i = 0; nfs3stat_strerror_table[i].stat != NFS3ERR_END_OF_LIST ; i++) {
                if (nfs3stat_strerror_table[i].stat == stat)
                        return nfs3stat_strerror_table[i].strerror;
        }

        return nfs3stat_strerror_table[i].strerror;
}



void
nfs3_prep_access3args (access3args *args, struct nfs3_fh *fh)
{
        memset (args, 0, sizeof (*args));
        args->object.data.data_val = (void *)fh;
}

#define POSIX_READ      4
#define POSIX_WRITE     2
#define POSIX_EXEC      1

uint32_t
nfs3_accessbits (int32_t accbits)
{
        uint32_t        accresult = 0;

        if (accbits & POSIX_READ)
                accresult |= ACCESS3_READ;

        if (accbits & POSIX_WRITE)
                accresult |= (ACCESS3_MODIFY | ACCESS3_EXTEND | ACCESS3_DELETE);

        /* lookup on directory allowed only in case of execute permission */
        if (accbits & POSIX_EXEC)
                accresult |= (ACCESS3_EXECUTE | ACCESS3_LOOKUP);

        return accresult;
}

uint32_t
nfs3_request_to_accessbits (int32_t accbits)
{
        uint32_t        acc_request = 0;

        if (accbits & ACCESS3_READ)
                acc_request |= POSIX_READ;

        if (accbits & (ACCESS3_MODIFY | ACCESS3_EXTEND | ACCESS3_DELETE))
                acc_request |= POSIX_WRITE;

        /* For lookup on directory check for execute permission */
        if (accbits & (ACCESS3_EXECUTE | ACCESS3_LOOKUP))
                acc_request |= POSIX_EXEC;

        return acc_request;
}
void
nfs3_fill_access3res (access3res *res, nfsstat3 status, int32_t accbits,
		      int32_t reqaccbits)
{
        uint32_t        accres = 0;

        memset (res, 0, sizeof (*res));
        res->status = status;
        if (status != NFS3_OK)
                return;

        accres = nfs3_accessbits (accbits);

	/* do not answer what was not asked */
        res->access3res_u.resok.access = accres & reqaccbits;
}

void
nfs3_prep_readdir3args (readdir3args *ra, struct nfs3_fh *fh)
{
        memset (ra, 0, sizeof (*ra));
        ra->dir.data.data_val = (void *)fh;
}


int
nfs3_is_dot_entry (char *entry)
{
        int     ret = 0;

        if (!entry)
                return 0;

        if (strcmp (entry, ".") == 0)
                ret = 1;

        return ret;
}


int
nfs3_is_parentdir_entry (char *entry)
{
        int     ret = 0;

        if (!entry)
                return 0;

        if (strcmp (entry, "..") == 0)
                ret = 1;

        return ret;
}


void
nfs3_funge_root_dotdot_dirent (gf_dirent_t *ent, struct nfs3_fh *dfh)
{
        if ((!ent) || (!dfh))
                return;

        if (nfs3_fh_is_root_fh (dfh) &&
            nfs3_is_parentdir_entry (ent->d_name)) {
                ent->d_ino = 1;
                ent->d_stat.ia_ino = 1;
        }

        if (nfs3_fh_is_root_fh (dfh) &&
            nfs3_is_dot_entry (ent->d_name)) {
                ent->d_ino = 1;
                ent->d_stat.ia_ino = 1;
        }

}


entry3 *
nfs3_fill_entry3 (gf_dirent_t *entry, struct nfs3_fh *dfh)
{
        entry3          *ent = NULL;
        if ((!entry) || (!dfh))
                return NULL;

        ent = GF_CALLOC (1, sizeof (*ent), gf_nfs_mt_entry3);
        if (!ent)
                return NULL;

        gf_msg_trace (GF_NFS3, 0, "Entry: %s", entry->d_name);

        /* If the entry is . or .., we need to replace the physical ino and gen
         * with 1 and 0 respectively if the directory is root. This funging is
         * needed because there is no parent directory of the root. In that
         * sense the behavior we provide is similar to the output of the
         * command: "stat /.."
         */
        entry->d_ino = nfs3_iatt_gfid_to_ino (&entry->d_stat);
        nfs3_funge_root_dotdot_dirent (entry, dfh);
        ent->fileid = entry->d_ino;
        ent->cookie = entry->d_off;
        ent->name = GF_CALLOC ((strlen (entry->d_name) + 1), sizeof (char),
                               gf_nfs_mt_char);
        if (!ent->name) {
                GF_FREE (ent);
                ent = NULL;
                goto err;
        }
        strcpy (ent->name, entry->d_name);

err:
        return ent;
}


void
nfs3_fill_post_op_fh3 (struct nfs3_fh *fh, post_op_fh3 *pfh)
{
        uint32_t        fhlen = 0;

        if ((!fh) || (!pfh))
                return;

        pfh->handle_follows = 1;
        fhlen = nfs3_fh_compute_size ();
        pfh->post_op_fh3_u.handle.data.data_val = (void *)fh;
        pfh->post_op_fh3_u.handle.data.data_len = fhlen;
}


post_op_fh3
nfs3_fh_to_post_op_fh3 (struct nfs3_fh *fh)
{
        post_op_fh3     pfh = {0, };
        char            *fhp = NULL;

        if (!fh)
                return pfh;

        pfh.handle_follows = 1;

        fhp = GF_CALLOC (1, sizeof (*fh), gf_nfs_mt_char);
        if (!fhp)
                return pfh;

        memcpy (fhp, fh, sizeof (*fh));
        nfs3_fill_post_op_fh3 ((struct nfs3_fh *)fhp, &pfh);
        return pfh;
}


entryp3 *
nfs3_fill_entryp3 (gf_dirent_t *entry, struct nfs3_fh *dirfh, uint64_t devid)
{
        entryp3         *ent = NULL;
        struct nfs3_fh  newfh = {{0}, };

        if ((!entry) || (!dirfh))
                return NULL;

        /* If the entry is . or .., we need to replace the physical ino and gen
         * with 1 and 0 respectively if the directory is root. This funging is
         * needed because there is no parent directory of the root. In that
         * sense the behavior we provide is similar to the output of the
         * command: "stat /.."
         */
        entry->d_ino = nfs3_iatt_gfid_to_ino (&entry->d_stat);
        nfs3_funge_root_dotdot_dirent (entry, dirfh);
        gf_msg_trace (GF_NFS3, 0, "Entry: %s, ino: %"PRIu64,
                      entry->d_name, entry->d_ino);
        ent = GF_CALLOC (1, sizeof (*ent), gf_nfs_mt_entryp3);
        if (!ent)
                return NULL;

        ent->fileid = entry->d_ino;
        ent->cookie = entry->d_off;
        ent->name = GF_CALLOC ((strlen (entry->d_name) + 1), sizeof (char),
                               gf_nfs_mt_char);
        if (!ent->name) {
                GF_FREE (ent);
                ent = NULL;
                goto err;
        }
        strcpy (ent->name, entry->d_name);

        nfs3_fh_build_child_fh (dirfh, &entry->d_stat, &newfh);
        nfs3_map_deviceid_to_statdev (&entry->d_stat, devid);
        /* *
         * In tier volume, the readdirp send only to cold subvol
         * which will populate in the 'T' file entries in the result.
         * For such files an explicit stat call is required, by setting
         * following argument client will perform the same.
         *
         * The inode value for 'T' files and directory is NULL, so just
         * skip the check if it is directory.
         */
        if (!(IA_ISDIR(entry->d_stat.ia_type)) && (entry->inode == NULL))
                ent->name_attributes.attributes_follow = FALSE;
        else
                ent->name_attributes =
                        nfs3_stat_to_post_op_attr (&entry->d_stat);

        ent->name_handle = nfs3_fh_to_post_op_fh3 (&newfh);
err:
        return ent;
}


void
nfs3_fill_readdir3res (readdir3res *res, nfsstat3 stat, struct nfs3_fh *dirfh,
                       uint64_t cverf, struct iatt *dirstat,
                       gf_dirent_t *entries, count3 count, int is_eof,
                       uint64_t deviceid)
{
        post_op_attr    dirattr;
        entry3          *ent = NULL;
        entry3          *headentry = NULL;
        entry3          *preventry = NULL;
        count3          filled = 0;
        gf_dirent_t     *listhead = NULL;

        memset (res, 0, sizeof (*res));
        res->status = stat;
        if (stat != NFS3_OK)
                return;

        nfs3_map_deviceid_to_statdev (dirstat, deviceid);
        dirattr = nfs3_stat_to_post_op_attr (dirstat);
        res->readdir3res_u.resok.dir_attributes = dirattr;
        res->readdir3res_u.resok.reply.eof = (bool_t)is_eof;
        memcpy (res->readdir3res_u.resok.cookieverf, &cverf, sizeof (cverf));

        filled = NFS3_READDIR_RESOK_SIZE;
        /* First entry is just the list head */
        listhead = entries;
        entries = entries->next;
        while (((entries) && (entries != listhead)) && (filled < count)) {
                /*
                if ((strcmp (entries->d_name, ".") == 0) ||
                    (strcmp (entries->d_name, "..") == 0))
                        goto nextentry;
                        */
                ent = nfs3_fill_entry3 (entries, dirfh);
                if (!ent)
                        break;

                if (!headentry)
                        headentry = ent;

                if (preventry) {
                        preventry->nextentry = ent;
                        preventry = ent;
                } else
                        preventry = ent;

                filled += NFS3_ENTRY3_FIXED_SIZE + strlen (ent->name);
//nextentry:
                entries = entries->next;
        }

        res->readdir3res_u.resok.reply.entries = headentry;

        return;
}


void
nfs3_fill_readdirp3res (readdirp3res *res, nfsstat3 stat,
                        struct nfs3_fh *dirfh, uint64_t cverf,
                        struct iatt *dirstat, gf_dirent_t *entries,
                        count3 dircount, count3 maxcount, int is_eof,
                        uint64_t deviceid)
{
        post_op_attr    dirattr;
        entryp3         *ent = NULL;
        entryp3         *headentry = NULL;
        entryp3         *preventry = NULL;
        count3          filled = 0;
        gf_dirent_t     *listhead = NULL;
        int             fhlen = 0;

        memset (res, 0, sizeof (*res));
        res->status = stat;
        if (stat != NFS3_OK)
                return;

        nfs3_map_deviceid_to_statdev (dirstat, deviceid);
        dirattr = nfs3_stat_to_post_op_attr (dirstat);
        res->readdirp3res_u.resok.dir_attributes = dirattr;
        res->readdirp3res_u.resok.reply.eof = (bool_t)is_eof;
        memcpy (res->readdirp3res_u.resok.cookieverf, &cverf, sizeof (cverf));

        filled = NFS3_READDIR_RESOK_SIZE;
        /* First entry is just the list head */
        listhead = entries;
        entries = entries->next;
        while (((entries) && (entries != listhead)) && (filled < maxcount)) {
                /* Linux does not display . and .. entries unless we provide
                 * these entries here.
                 */
/*                if ((strcmp (entries->d_name, ".") == 0) ||
                    (strcmp (entries->d_name, "..") == 0))
                        goto nextentry;
                        */
                ent = nfs3_fill_entryp3 (entries, dirfh, deviceid);
                if (!ent)
                        break;

                if (!headentry)
                        headentry = ent;

                if (preventry) {
                        preventry->nextentry = ent;
                        preventry = ent;
                } else
                        preventry = ent;

                fhlen = ent->name_handle.post_op_fh3_u.handle.data.data_len;
                filled += NFS3_ENTRYP3_FIXED_SIZE + fhlen + strlen (ent->name);
//nextentry:
                entries = entries->next;
        }

        res->readdirp3res_u.resok.reply.entries = headentry;

        return;
}


void
nfs3_prep_readdirp3args (readdirp3args *ra, struct nfs3_fh *fh)
{
        memset (ra, 0, sizeof (*ra));
        ra->dir.data.data_val = (void *)fh;
}

void
nfs3_free_readdirp3res (readdirp3res *res)
{
        entryp3 *ent = NULL;
        entryp3 *next = NULL;

        if (!res)
                return;

        ent = res->readdirp3res_u.resok.reply.entries;
        while (ent) {

                next = ent->nextentry;
                GF_FREE (ent->name);
                GF_FREE (ent->name_handle.post_op_fh3_u.handle.data.data_val);
                GF_FREE (ent);
                ent = next;
        }

        return;
}


void
nfs3_free_readdir3res (readdir3res *res)
{
        entry3 *ent = NULL;
        entry3 *next = NULL;

        if (!res)
                return;

        ent = res->readdir3res_u.resok.reply.entries;
        while (ent) {

                next = ent->nextentry;
                GF_FREE (ent->name);
                GF_FREE (ent);
                ent = next;
        }

        return;
}

void
nfs3_prep_fsstat3args (fsstat3args *args, struct nfs3_fh *fh)
{
        memset (args, 0, sizeof (*args));
        args->fsroot.data.data_val = (char *)fh;
}


void
nfs3_fill_fsstat3res (fsstat3res *res, nfsstat3 stat, struct statvfs *fsbuf,
                      struct iatt *postbuf, uint64_t deviceid)
{
        post_op_attr    poa;
        fsstat3resok    resok;

        memset (res, 0, sizeof (*res));
        res->status = stat;
        if (stat != NFS3_OK)
                return;

        nfs3_map_deviceid_to_statdev (postbuf, deviceid);
        poa = nfs3_stat_to_post_op_attr (postbuf);
        resok.tbytes = (size3)(fsbuf->f_frsize * fsbuf->f_blocks);
        resok.fbytes = (size3)(fsbuf->f_frsize * fsbuf->f_bfree);
        resok.abytes = (size3)(fsbuf->f_frsize * fsbuf->f_bavail);
        resok.tfiles = (size3)(fsbuf->f_files);
        resok.ffiles = (size3)(fsbuf->f_ffree);
        resok.afiles = (size3)(fsbuf->f_favail);
        resok.invarsec = 0;

        resok.obj_attributes = poa;
        res->fsstat3res_u.resok = resok;
}


int32_t
nfs3_sattr3_to_setattr_valid (sattr3 *sattr, struct iatt *buf, mode_t *omode)
{
        int32_t         valid = 0;
        ia_prot_t       prot = {0, };
        mode_t          mode = 0;

        if (!sattr)
                return 0;

        if (sattr->mode.set_it) {
                valid |= GF_SET_ATTR_MODE;

                if (sattr->mode.set_mode3_u.mode & NFS3MODE_ROWNER) {
                        mode |= S_IRUSR;
                        prot.owner.read = 1;
                }
                if (sattr->mode.set_mode3_u.mode & NFS3MODE_WOWNER) {
                        mode |= S_IWUSR;
                        prot.owner.write = 1;
                }
                if (sattr->mode.set_mode3_u.mode & NFS3MODE_XOWNER) {
                        mode |= S_IXUSR;
                        prot.owner.exec = 1;
                }

                if (sattr->mode.set_mode3_u.mode & NFS3MODE_RGROUP) {
                        mode |= S_IRGRP;
                        prot.group.read = 1;
                }
                if (sattr->mode.set_mode3_u.mode & NFS3MODE_WGROUP) {
                        mode |= S_IWGRP;
                        prot.group.write = 1;
                }
                if (sattr->mode.set_mode3_u.mode & NFS3MODE_XGROUP) {
                        mode |= S_IXGRP;
                        prot.group.exec = 1;
                }

                if (sattr->mode.set_mode3_u.mode & NFS3MODE_ROTHER) {
                        mode |= S_IROTH;
                        prot.other.read = 1;
                }
                if (sattr->mode.set_mode3_u.mode & NFS3MODE_WOTHER) {
                        mode |= S_IWOTH;
                        prot.other.write = 1;
                }
                if (sattr->mode.set_mode3_u.mode & NFS3MODE_XOTHER) {
                        mode |= S_IXOTH;
                        prot.other.exec = 1;
                }

                if (sattr->mode.set_mode3_u.mode & NFS3MODE_SETXUID) {
                        mode |= S_ISUID;
                        prot.suid = 1;
                }
                if (sattr->mode.set_mode3_u.mode & NFS3MODE_SETXGID) {
                        mode |= S_ISGID;
                        prot.sgid = 1;
                }
                if (sattr->mode.set_mode3_u.mode & NFS3MODE_SAVESWAPTXT) {
                        mode |= S_ISVTX;
                        prot.sticky = 1;
                }

                if (buf)
                        buf->ia_prot = prot;
                /* Create fop still requires the old mode_t style argument. */
                if (omode)
                        *omode = mode;
        }

        if (sattr->uid.set_it) {
                valid |= GF_SET_ATTR_UID;
                if (buf)
                        buf->ia_uid = sattr->uid.set_uid3_u.uid;
        }

        if (sattr->gid.set_it) {
                valid |= GF_SET_ATTR_GID;
                if (buf)
                        buf->ia_gid = sattr->gid.set_gid3_u.gid;
        }

        if (sattr->size.set_it) {
                valid |= GF_SET_ATTR_SIZE;
                if (buf)
                        buf->ia_size = sattr->size.set_size3_u.size;
        }

        if (sattr->atime.set_it == SET_TO_CLIENT_TIME) {
                valid |= GF_SET_ATTR_ATIME;
                if (buf)
                        buf->ia_atime = sattr->atime.set_atime_u.atime.seconds;
        }

        if (sattr->atime.set_it == SET_TO_SERVER_TIME) {
                valid |= GF_SET_ATTR_ATIME;
                if (buf)
                        buf->ia_atime = time (NULL);
        }

        if (sattr->mtime.set_it == SET_TO_CLIENT_TIME) {
                valid |= GF_SET_ATTR_MTIME;
                if (buf)
                        buf->ia_mtime = sattr->mtime.set_mtime_u.mtime.seconds;
        }

        if (sattr->mtime.set_it == SET_TO_SERVER_TIME) {
                valid |= GF_SET_ATTR_MTIME;
                if (buf)
                        buf->ia_mtime = time (NULL);
        }

        return valid;
}


wcc_data
nfs3_stat_to_wcc_data (struct iatt *pre, struct iatt *post)
{
        wcc_data        wd = {{0}, };

        if (post)
                wd.after = nfs3_stat_to_post_op_attr (post);
        if (pre)
                wd.before = nfs3_stat_to_pre_op_attr (pre);

        return wd;
}

void
nfs3_fill_create3res (create3res *res, nfsstat3 stat, struct nfs3_fh *newfh,
                      struct iatt *newbuf, struct iatt *preparent,
                      struct iatt *postparent, uint64_t deviceid)
{
        post_op_attr    poa = {0, };
        wcc_data        dirwcc = {{0}, };

        memset (res, 0, sizeof (*res));
        res->status = stat;
        if (stat != NFS3_OK)
                return;

        nfs3_fill_post_op_fh3 (newfh, &res->create3res_u.resok.obj);
        nfs3_map_deviceid_to_statdev (newbuf, deviceid);
        poa = nfs3_stat_to_post_op_attr (newbuf);
        res->create3res_u.resok.obj_attributes = poa;
        nfs3_map_deviceid_to_statdev (preparent, deviceid);
        nfs3_map_deviceid_to_statdev (postparent, deviceid);
        dirwcc = nfs3_stat_to_wcc_data (preparent, postparent);

        res->create3res_u.resok.dir_wcc = dirwcc;
}

void
nfs3_prep_create3args (create3args *args, struct nfs3_fh *fh, char *name)
{

        memset (args, 0, sizeof (*args));
        args->where.dir.data.data_val = (void *)fh;
        args->where.name = name;
}

void
nfs3_prep_setattr3args (setattr3args *args, struct nfs3_fh *fh)
{
        memset (args, 0, sizeof (*args));
        args->object.data.data_val = (void *)fh;
}


void
nfs3_fill_setattr3res (setattr3res *res, nfsstat3 stat, struct iatt *preop,
                       struct iatt *postop, uint64_t deviceid)
{
        wcc_data        wcc;
        memset (res, 0, sizeof (*res));
        res->status = stat;
        if (stat != NFS3_OK)
                return;

        nfs3_map_deviceid_to_statdev (preop, deviceid);
        nfs3_map_deviceid_to_statdev (postop, deviceid);
        wcc = nfs3_stat_to_wcc_data (preop, postop);
        res->setattr3res_u.resok.obj_wcc = wcc;
}


void
nfs3_prep_mkdir3args (mkdir3args *args, struct nfs3_fh *dirfh, char *name)
{

        memset (args, 0, sizeof (*args));
        args->where.dir.data.data_val = (void *)dirfh;
        args->where.name = name;
}


void
nfs3_fill_mkdir3res (mkdir3res *res, nfsstat3 stat, struct nfs3_fh *fh,
                     struct iatt *buf, struct iatt *preparent,
                     struct iatt *postparent, uint64_t deviceid)
{
        wcc_data        dirwcc;
        post_op_attr    poa;

        memset (res, 0, sizeof (*res));
        res->status = stat;
        if (stat != NFS3_OK)
                return;

        nfs3_fill_post_op_fh3 (fh, &res->mkdir3res_u.resok.obj);
        nfs3_map_deviceid_to_statdev (buf, deviceid);
        poa = nfs3_stat_to_post_op_attr (buf);
        nfs3_map_deviceid_to_statdev (preparent, deviceid);
        nfs3_map_deviceid_to_statdev (postparent, deviceid);
        dirwcc = nfs3_stat_to_wcc_data (preparent, postparent);
        res->mkdir3res_u.resok.obj_attributes = poa;
        res->mkdir3res_u.resok.dir_wcc = dirwcc;

}


void
nfs3_prep_symlink3args (symlink3args *args, struct nfs3_fh *dirfh, char *name,
                        char *target)
{
        memset (args, 0, sizeof (*args));
        args->where.dir.data.data_val = (void *)dirfh;
        args->where.name = name;
        args->symlink.symlink_data = target;
}


void
nfs3_fill_symlink3res (symlink3res *res, nfsstat3 stat, struct nfs3_fh *fh,
                       struct iatt *buf, struct iatt *preparent,
                       struct iatt *postparent, uint64_t deviceid)
{
        wcc_data        dirwcc;
        post_op_attr    poa;

        memset (res, 0, sizeof (*res));
        res->status = stat;
        if (stat != NFS3_OK)
                return;

        nfs3_fill_post_op_fh3 (fh, &res->symlink3res_u.resok.obj);
        nfs3_map_deviceid_to_statdev (buf, deviceid);
        poa = nfs3_stat_to_post_op_attr (buf);
        nfs3_map_deviceid_to_statdev (postparent, deviceid);
        nfs3_map_deviceid_to_statdev (preparent, deviceid);
        dirwcc = nfs3_stat_to_wcc_data (preparent, postparent);
        res->symlink3res_u.resok.obj_attributes = poa;
        res->symlink3res_u.resok.dir_wcc = dirwcc;

}


void
nfs3_prep_readlink3args (readlink3args *args, struct nfs3_fh *fh)
{

        memset (args, 0, sizeof (*args));
        args->symlink.data.data_val = (void *)fh;
}


void
nfs3_fill_readlink3res (readlink3res *res, nfsstat3 stat, char *path,
                        struct iatt *buf, uint64_t deviceid)
{
        post_op_attr    poa;

        memset (res, 0, sizeof (*res));
        res->status = stat;

        if (stat != NFS3_OK)
                return;

        nfs3_map_deviceid_to_statdev (buf, deviceid);
        poa = nfs3_stat_to_post_op_attr (buf);
        res->readlink3res_u.resok.data = (void *)path;
        res->readlink3res_u.resok.symlink_attributes = poa;
}


void
nfs3_prep_mknod3args (mknod3args *args, struct nfs3_fh *fh, char *name)
{
        memset (args, 0, sizeof (*args));
        args->where.dir.data.data_val = (void *)fh;
        args->where.name = name;

}

void
nfs3_fill_mknod3res (mknod3res *res, nfsstat3 stat, struct nfs3_fh *fh,
                     struct iatt *buf, struct iatt *preparent,
                     struct iatt *postparent, uint64_t deviceid)
{
        post_op_attr    poa;
        wcc_data        wccdir;

        memset (res, 0, sizeof (*res));
        res->status = stat;
        if (stat != NFS3_OK)
                return;

        nfs3_fill_post_op_fh3 (fh, &res->mknod3res_u.resok.obj);
        nfs3_map_deviceid_to_statdev (buf, deviceid);
        poa = nfs3_stat_to_post_op_attr (buf);
        nfs3_map_deviceid_to_statdev (preparent, deviceid);
        nfs3_map_deviceid_to_statdev (postparent, deviceid);
        wccdir = nfs3_stat_to_wcc_data (preparent, postparent);
        res->mknod3res_u.resok.obj_attributes = poa;
        res->mknod3res_u.resok.dir_wcc = wccdir;

}


void
nfs3_fill_remove3res (remove3res *res, nfsstat3 stat, struct iatt *preparent,
                      struct iatt *postparent, uint64_t deviceid)
{
        wcc_data        dirwcc;

        memset (res, 0, sizeof (*res));
        res->status = stat;
        if (stat != NFS3_OK)
                return;

        nfs3_map_deviceid_to_statdev (preparent, deviceid);
        nfs3_map_deviceid_to_statdev (postparent, deviceid);
        dirwcc = nfs3_stat_to_wcc_data (preparent, postparent);
        res->remove3res_u.resok.dir_wcc = dirwcc;
}


void
nfs3_prep_remove3args (remove3args *args, struct nfs3_fh *fh, char *name)
{
        memset (args, 0, sizeof (*args));
        args->object.dir.data.data_val = (void *)fh;
        args->object.name = name;
}


void
nfs3_prep_rmdir3args (rmdir3args *args, struct nfs3_fh *fh, char *name)
{
        memset (args, 0, sizeof (*args));
        args->object.dir.data.data_val = (void *)fh;
        args->object.name = name;
}


void
nfs3_fill_rmdir3res (rmdir3res *res, nfsstat3 stat, struct iatt *preparent,
                     struct iatt *postparent, uint64_t deviceid)
{
        wcc_data        dirwcc;
        memset (res, 0, sizeof (*res));
        res->status = stat;

        if (stat != NFS3_OK)
                return;

        nfs3_map_deviceid_to_statdev (postparent, deviceid);
        nfs3_map_deviceid_to_statdev (preparent, deviceid);
        dirwcc = nfs3_stat_to_wcc_data (preparent, postparent);
        res->rmdir3res_u.resok.dir_wcc = dirwcc;
}


void
nfs3_prep_link3args (link3args *args, struct nfs3_fh *target,
                     struct nfs3_fh * dirfh, char *name)
{
        memset (args, 0, sizeof (*args));
        args->file.data.data_val = (void *)target;
        args->link.dir.data.data_val = (void *)dirfh;
        args->link.name = name;
}


void
nfs3_fill_link3res (link3res *res, nfsstat3 stat, struct iatt *buf,
                    struct iatt *preparent, struct iatt *postparent,
                    uint64_t deviceid)
{
        post_op_attr    poa;
        wcc_data        dirwcc;

        memset (res, 0, sizeof (*res));
        res->status = stat;
        if (stat != NFS3_OK)
                return;

        nfs3_map_deviceid_to_statdev (preparent, deviceid);
        nfs3_map_deviceid_to_statdev (postparent, deviceid);
        nfs3_map_deviceid_to_statdev (buf,deviceid);
        poa = nfs3_stat_to_post_op_attr (buf);
        dirwcc = nfs3_stat_to_wcc_data (preparent, postparent);
        res->link3res_u.resok.file_attributes = poa;
        res->link3res_u.resok.linkdir_wcc = dirwcc;
}



void
nfs3_prep_rename3args (rename3args *args, struct nfs3_fh *olddirfh,
                       char *oldname, struct nfs3_fh *newdirfh, char *newname)
{
        memset (args, 0, sizeof (*args));

        args->from.name = oldname;
        args->from.dir.data.data_val = (void *)olddirfh;
        args->to.name = newname;
        args->to.dir.data.data_val = (void *)newdirfh;

}


void
nfs3_fill_rename3res (rename3res *res, nfsstat3 stat, struct iatt *buf,
                      struct iatt *preoldparent, struct iatt *postoldparent,
                      struct iatt *prenewparent, struct iatt *postnewparent,
                      uint64_t deviceid)

{
        wcc_data        dirwcc;

        memset (res, 0, sizeof (*res));
        res->status = stat;
        if (stat != NFS3_OK)
                return;

        nfs3_map_deviceid_to_statdev (preoldparent, deviceid);
        nfs3_map_deviceid_to_statdev (postoldparent, deviceid);
        nfs3_map_deviceid_to_statdev (prenewparent, deviceid);
        nfs3_map_deviceid_to_statdev (postnewparent, deviceid);
        nfs3_map_deviceid_to_statdev (buf, deviceid);
        dirwcc = nfs3_stat_to_wcc_data (preoldparent, postoldparent);
        res->rename3res_u.resok.fromdir_wcc = dirwcc;
        dirwcc = nfs3_stat_to_wcc_data (prenewparent, postnewparent);
        res->rename3res_u.resok.todir_wcc = dirwcc;
}


void
nfs3_prep_write3args (write3args *args, struct nfs3_fh *fh)
{
        memset (args, 0, sizeof (*args));
        args->file.data.data_val = (void *)fh;
}


void
nfs3_fill_write3res (write3res *res, nfsstat3 stat, count3 count,
                     stable_how stable, uint64_t wverf, struct iatt *prestat,
                     struct iatt *poststat, uint64_t deviceid)
{
        write3resok     resok;
        memset (res, 0, sizeof (*res));
        res->status = stat;
        if (stat != NFS3_OK)
                return;

        nfs3_map_deviceid_to_statdev (prestat, deviceid);
        nfs3_map_deviceid_to_statdev (poststat, deviceid);
        resok.file_wcc = nfs3_stat_to_wcc_data (prestat, poststat);
        resok.count = count;
        resok.committed = stable;
        memcpy (resok.verf, &wverf, sizeof (wverf));

        res->write3res_u.resok = resok;
}


void
nfs3_prep_commit3args (commit3args *args, struct nfs3_fh *fh)
{
        memset (args, 0, sizeof (*args));
        args->file.data.data_val = (void *)fh;
}


void
nfs3_fill_commit3res (commit3res *res, nfsstat3 stat, uint64_t wverf,
                      struct iatt *prestat, struct iatt *poststat,
                      uint64_t deviceid)
{
        memset (res, 0, sizeof (*res));
        res->status = stat;
        if (stat != NFS3_OK)
                return;

        nfs3_map_deviceid_to_statdev (poststat, deviceid);
        nfs3_map_deviceid_to_statdev (prestat, deviceid);
        res->commit3res_u.resok.file_wcc = nfs3_stat_to_wcc_data (prestat,
                                                                  poststat);
        memcpy (res->commit3res_u.resok.verf, &wverf, sizeof (wverf));
}

void
nfs3_fill_read3res (read3res *res, nfsstat3 stat, count3 count,
                    struct iatt *poststat, int is_eof, uint64_t deviceid)
{
        post_op_attr    poa;

        memset (res, 0, sizeof (*res));
        res->status = stat;
        if (stat != NFS3_OK)
                return;

        nfs3_map_deviceid_to_statdev (poststat, deviceid);
        poa = nfs3_stat_to_post_op_attr (poststat);
        res->read3res_u.resok.file_attributes = poa;
        res->read3res_u.resok.count = count;
        res->read3res_u.resok.eof = is_eof;
        res->read3res_u.resok.data.data_len = count;
}


void
nfs3_prep_read3args (read3args *args, struct nfs3_fh *fh)
{
        memset (args, 0, sizeof (*args));
        args->file.data.data_val = (void *)fh;
}


void
nfs3_fill_pathconf3res (pathconf3res *res, nfsstat3 stat, struct iatt *buf,
                        uint64_t deviceid)
{
        pathconf3resok  resok;

        memset (res, 0, sizeof (*res));
        res->status = stat;
        if (stat != NFS3_OK)
                return;

        nfs3_map_deviceid_to_statdev (buf, deviceid);
        resok.obj_attributes = nfs3_stat_to_post_op_attr (buf);
        resok.linkmax = 256;
        resok.name_max = NFS_NAME_MAX;
        resok.no_trunc = TRUE;
        resok.chown_restricted = FALSE;
        resok.case_insensitive = FALSE;
        resok.case_preserving = TRUE;

        res->pathconf3res_u.resok = resok;
}


void
nfs3_prep_pathconf3args (pathconf3args *args, struct nfs3_fh *fh)
{
        memset (args, 0, sizeof (*args));
        args->object.data.data_val = (void *)fh;
}


int
nfs3_verify_dircookie (struct nfs3_state *nfs3, fd_t *dirfd, cookie3 cookie,
                       uint64_t cverf, nfsstat3 *stat)
{
        int             ret = -1;

        if ((!nfs3) || (!dirfd))
                return -1;

        /* Can assume that this is first read on the dir, so cookie check
         * is successful by default.
         */
        if (cookie == 0)
                return 0;

        gf_msg_trace (GF_NFS3, 0, "Verifying cookie: cverf: %"PRIu64
                      ", cookie: %"PRIu64, cverf, cookie);
        /* The cookie bad, no way cverf will be zero with a non-zero cookie. */
        if ((cverf == 0) && (cookie != 0)) {
                gf_msg_trace (GF_NFS3, 0, "Bad cookie requested");
                if (stat)
                        *stat = NFS3ERR_BAD_COOKIE;
                goto err;
        }

        /* Yes, its true, our cookie is simply the fd_t address.
         * NOTE: We used have the check for cookieverf but VMWare client sends
         * a readdirp requests even after we've told it that EOF has been
         * reached on the directory. This causes a problem because we close a
         * dir fd_t after reaching EOF. The next readdirp sent by VMWare
         * contains the address of the closed fd_t as cookieverf. Since we
         * closed that fd_t, this readdirp results in a new opendir which will
         * give an fd_t that will fail this check below.
         */
/*        if ((cverf != (uint64_t)dirfd)) {
                gf_log (GF_NFS3, GF_LOG_TRACE, "Cookieverf does not match");
                if (stat)
                        *stat = NFS3ERR_BAD_COOKIE;
                goto err;
        }
*/
        gf_msg_trace (GF_NFS3, 0, "Cookie verified");
        if (stat)
                *stat = NFS3_OK;
        ret = 0;
err:
        return ret;
}


void
nfs3_stat_to_errstr (uint32_t xid, char *op, nfsstat3 stat, int pstat,
                     char *errstr, size_t len)
{
        if ((!op) || (!errstr))
                return;

        snprintf (errstr, len, "XID: %x, %s: NFS: %d(%s), POSIX: %d(%s)",
                  xid, op,stat, nfsstat3_strerror (stat), pstat,
                  strerror (pstat));
}

void
nfs3_log_common_call (uint32_t xid, char *op, struct nfs3_fh *fh)
{
        char    fhstr[1024];

        if (THIS->ctx->log.loglevel < GF_LOG_DEBUG)
                return;

        nfs3_fh_to_str (fh, fhstr, sizeof (fhstr));
        gf_msg_debug (GF_NFS3, 0, "XID: %x, %s: args: %s", xid, op, fhstr);
}


void
nfs3_log_fh_entry_call (uint32_t xid, char *op, struct nfs3_fh *fh,
                        char *name)
{
        char    fhstr[1024];

        if (THIS->ctx->log.loglevel < GF_LOG_DEBUG)
                return;
        nfs3_fh_to_str (fh, fhstr, sizeof (fhstr));
        gf_msg_debug (GF_NFS3, 0, "XID: %x, %s: args: %s, name: %s", xid,
                      op, fhstr, name);
}


void
nfs3_log_rename_call (uint32_t xid, struct nfs3_fh *src, char *sname,
                      struct nfs3_fh *dst, char *dname)
{
        char    sfhstr[1024];
        char    dfhstr[1024];

        if (THIS->ctx->log.loglevel < GF_LOG_DEBUG)
                return;
        nfs3_fh_to_str (src, sfhstr, sizeof (sfhstr));
        nfs3_fh_to_str (dst, dfhstr, sizeof (dfhstr));
        gf_msg_debug (GF_NFS3, 0, "XID: %x, RENAME: args: Src: %s, "
                      "name: %s, Dst: %s, name: %s", xid, sfhstr, sname,
                      dfhstr, dname);
}



void
nfs3_log_create_call (uint32_t xid, struct nfs3_fh *fh, char *name,
                      createmode3 mode)
{
        char    fhstr[1024];
        char    *modestr = NULL;
        char    exclmode[] = "EXCLUSIVE";
        char    unchkd[] = "UNCHECKED";
        char    guarded[] = "GUARDED";

        if (THIS->ctx->log.loglevel < GF_LOG_DEBUG)
                return;
        nfs3_fh_to_str (fh, fhstr, sizeof (fhstr));
        if (mode == EXCLUSIVE)
                modestr = exclmode;
        else if (mode == GUARDED)
                modestr = guarded;
        else
                modestr = unchkd;

        gf_msg_debug (GF_NFS3, 0, "XID: %x, CREATE: args: %s, name: %s,"
                      " mode: %s", xid, fhstr, name, modestr);
}


void
nfs3_log_mknod_call (uint32_t xid, struct nfs3_fh *fh, char *name, int type)
{
        char    fhstr[1024];
        char    *modestr = NULL;
        char    chr[] = "CHAR";
        char    blk[] = "BLK";
        char    sock[] = "SOCK";
        char    fifo[] = "FIFO";

        if (THIS->ctx->log.loglevel < GF_LOG_DEBUG)
                return;
        nfs3_fh_to_str (fh, fhstr, sizeof (fhstr));
        if (type == NF3CHR)
                modestr = chr;
        else if (type == NF3BLK)
                modestr = blk;
        else if (type == NF3SOCK)
                modestr = sock;
        else
                modestr = fifo;

        gf_msg_debug (GF_NFS3, 0, "XID: %x, MKNOD: args: %s, name: %s,"
                      " type: %s", xid, fhstr, name, modestr);
}



void
nfs3_log_symlink_call (uint32_t xid, struct nfs3_fh *fh, char *name, char *tgt)
{
        char    fhstr[1024];

        if (THIS->ctx->log.loglevel < GF_LOG_DEBUG)
                return;
        nfs3_fh_to_str (fh, fhstr, sizeof (fhstr));
        gf_msg_debug (GF_NFS3, 0, "XID: %x, SYMLINK: args: %s, name: %s,"
                      " target: %s", xid, fhstr, name, tgt);
}


void
nfs3_log_link_call (uint32_t xid, struct nfs3_fh *fh, char *name,
                    struct nfs3_fh *tgt)
{
        char    dfhstr[1024];
        char    tfhstr[1024];

        if (THIS->ctx->log.loglevel < GF_LOG_DEBUG)
                return;
        nfs3_fh_to_str (fh, dfhstr, sizeof (dfhstr));
        nfs3_fh_to_str (tgt, tfhstr, sizeof (tfhstr));
        gf_msg_debug (GF_NFS3, 0, "XID: %x, LINK: args: %s, name: %s,"
                      " target: %s", xid, dfhstr, name, tfhstr);
}


void
nfs3_log_rw_call (uint32_t xid, char *op, struct nfs3_fh *fh, offset3 offt,
                  count3 count, int stablewrite)
{
        char    fhstr[1024];

        if (THIS->ctx->log.loglevel < GF_LOG_DEBUG)
                return;
        nfs3_fh_to_str (fh, fhstr, sizeof (fhstr));
        if (stablewrite == -1)
                gf_msg_debug (GF_NFS3, 0, "XID: %x, %s: args: %s, offset:"
                              " %"PRIu64",  count: %"PRIu32, xid, op, fhstr,
                              offt, count);
        else
                gf_msg_debug (GF_NFS3, 0, "XID: %x, %s: args: %s, offset:"
                              " %"PRIu64",  count: %"PRIu32", %s", xid, op,
                              fhstr, offt, count,
                              (stablewrite == UNSTABLE)?"UNSTABLE":"STABLE");

}


int
nfs3_getattr_loglevel (nfsstat3 stat) {

	int ll = GF_LOG_DEBUG;

	switch (stat) {

        case NFS3ERR_PERM:
		ll = GF_LOG_WARNING;
                break;

        case NFS3ERR_NOENT:
		ll = GF_LOG_WARNING;
                break;

        case NFS3ERR_ACCES:
		ll = GF_LOG_WARNING;
                break;

        case NFS3ERR_EXIST:
		ll = GF_LOG_WARNING;
                break;

        case NFS3ERR_XDEV:
		ll = GF_LOG_WARNING;
                break;

        case NFS3ERR_NODEV:
		ll = GF_LOG_WARNING;
                break;

        case NFS3ERR_IO:
		ll = GF_LOG_WARNING;
                break;

        case NFS3ERR_NXIO:
		ll = GF_LOG_WARNING;
                break;

        case NFS3ERR_NOTDIR:
		ll = GF_LOG_WARNING;
                break;

        case NFS3ERR_ISDIR:
		ll = GF_LOG_WARNING;
                break;

        case NFS3ERR_INVAL:
		ll = GF_LOG_WARNING;
                break;

        case NFS3ERR_NOSPC:
		ll = GF_LOG_WARNING;
                break;

        case NFS3ERR_ROFS:
		ll = GF_LOG_WARNING;
                break;

        case NFS3ERR_FBIG:
		ll = GF_LOG_WARNING;
                break;

        case NFS3ERR_MLINK:
		ll = GF_LOG_WARNING;
                break;

        case NFS3ERR_NAMETOOLONG:
		ll = GF_LOG_WARNING;
                break;

        case NFS3ERR_NOTEMPTY:
		ll = GF_LOG_WARNING;
                break;

        case NFS3ERR_SERVERFAULT:
		ll = GF_LOG_WARNING;
                break;

        case NFS3ERR_NOTSUPP:
		ll = GF_LOG_WARNING;
                break;

        case NFS3ERR_BADHANDLE:
		ll = GF_LOG_WARNING;
                break;

        case NFS3ERR_STALE:
		ll = GF_LOG_WARNING;
                break;

        case NFS3ERR_DQUOT:
		ll = GF_LOG_WARNING;
                break;

        default:
		ll = GF_LOG_DEBUG;
                break;
        }

        return ll;
}


int
nfs3_setattr_loglevel (nfsstat3 stat) {

	int ll = GF_LOG_DEBUG;

	switch (stat) {

        case NFS3ERR_NOENT:
		ll = GF_LOG_WARNING;
                break;

        case NFS3ERR_EXIST:
		ll = GF_LOG_WARNING;
                break;

        case NFS3ERR_XDEV:
		ll = GF_LOG_WARNING;
                break;

        case NFS3ERR_NODEV:
		ll = GF_LOG_WARNING;
                break;

        case NFS3ERR_IO:
		ll = GF_LOG_WARNING;
                break;

        case NFS3ERR_NXIO:
		ll = GF_LOG_WARNING;
                break;

        case NFS3ERR_NOTDIR:
		ll = GF_LOG_WARNING;
                break;

        case NFS3ERR_ISDIR:
		ll = GF_LOG_WARNING;
                break;

        case NFS3ERR_INVAL:
		ll = GF_LOG_WARNING;
                break;

        case NFS3ERR_NOSPC:
		ll = GF_LOG_WARNING;
                break;

        case NFS3ERR_ROFS:
		ll = GF_LOG_WARNING;
                break;

        case NFS3ERR_FBIG:
		ll = GF_LOG_WARNING;
                break;

        case NFS3ERR_MLINK:
		ll = GF_LOG_WARNING;
                break;

        case NFS3ERR_NAMETOOLONG:
		ll = GF_LOG_WARNING;
                break;

        case NFS3ERR_NOTEMPTY:
		ll = GF_LOG_WARNING;
                break;

        case NFS3ERR_SERVERFAULT:
		ll = GF_LOG_WARNING;
                break;

        case NFS3ERR_NOTSUPP:
		ll = GF_LOG_WARNING;
                break;

        case NFS3ERR_BADHANDLE:
		ll = GF_LOG_WARNING;
                break;

        case NFS3ERR_STALE:
		ll = GF_LOG_WARNING;
                break;

        case NFS3ERR_DQUOT:
		ll = GF_LOG_WARNING;
                break;

        default:
		ll = GF_LOG_DEBUG;
                break;
        }

        return ll;
}


int
nfs3_lookup_loglevel (nfsstat3 stat) {

	int ll = GF_LOG_DEBUG;

	switch (stat) {

        case NFS3ERR_PERM:
		ll = GF_LOG_WARNING;
                break;

        case NFS3ERR_ACCES:
		ll = GF_LOG_WARNING;
                break;

        case NFS3ERR_EXIST:
		ll = GF_LOG_WARNING;
                break;

        case NFS3ERR_XDEV:
		ll = GF_LOG_WARNING;
                break;

        case NFS3ERR_NODEV:
		ll = GF_LOG_WARNING;
                break;

        case NFS3ERR_IO:
		ll = GF_LOG_WARNING;
                break;

        case NFS3ERR_NXIO:
		ll = GF_LOG_WARNING;
                break;

        case NFS3ERR_NOTDIR:
		ll = GF_LOG_WARNING;
                break;

        case NFS3ERR_ISDIR:
		ll = GF_LOG_WARNING;
                break;

        case NFS3ERR_INVAL:
		ll = GF_LOG_WARNING;
                break;

        case NFS3ERR_NOSPC:
		ll = GF_LOG_WARNING;
                break;

        case NFS3ERR_ROFS:
		ll = GF_LOG_WARNING;
                break;

        case NFS3ERR_FBIG:
		ll = GF_LOG_WARNING;
                break;

        case NFS3ERR_MLINK:
		ll = GF_LOG_WARNING;
                break;

        case NFS3ERR_NAMETOOLONG:
		ll = GF_LOG_WARNING;
                break;

        case NFS3ERR_NOTEMPTY:
		ll = GF_LOG_WARNING;
                break;

        case NFS3ERR_SERVERFAULT:
		ll = GF_LOG_WARNING;
                break;

        case NFS3ERR_NOTSUPP:
		ll = GF_LOG_WARNING;
                break;

        case NFS3ERR_BADHANDLE:
		ll = GF_LOG_WARNING;
                break;

        case NFS3ERR_STALE:
		ll = GF_LOG_WARNING;
                break;

        case NFS3ERR_DQUOT:
		ll = GF_LOG_WARNING;
                break;

        default:
		ll = GF_LOG_DEBUG;
                break;
        }

        return ll;
}


int
nfs3_access_loglevel (nfsstat3 stat) {

	int ll = GF_LOG_DEBUG;

	switch (stat) {

        case NFS3ERR_NOENT:
		ll = GF_LOG_WARNING;
                break;

        case NFS3ERR_EXIST:
		ll = GF_LOG_WARNING;
                break;

        case NFS3ERR_XDEV:
		ll = GF_LOG_WARNING;
                break;

        case NFS3ERR_NODEV:
		ll = GF_LOG_WARNING;
                break;

        case NFS3ERR_IO:
		ll = GF_LOG_WARNING;
                break;

        case NFS3ERR_NXIO:
		ll = GF_LOG_WARNING;
                break;

        case NFS3ERR_NOTDIR:
		ll = GF_LOG_WARNING;
                break;

        case NFS3ERR_ISDIR:
		ll = GF_LOG_WARNING;
                break;

        case NFS3ERR_INVAL:
		ll = GF_LOG_WARNING;
                break;

        case NFS3ERR_NOSPC:
		ll = GF_LOG_WARNING;
                break;

        case NFS3ERR_ROFS:
		ll = GF_LOG_WARNING;
                break;

        case NFS3ERR_FBIG:
		ll = GF_LOG_WARNING;
                break;

        case NFS3ERR_MLINK:
		ll = GF_LOG_WARNING;
                break;

        case NFS3ERR_NAMETOOLONG:
		ll = GF_LOG_WARNING;
                break;

        case NFS3ERR_NOTEMPTY:
		ll = GF_LOG_WARNING;
                break;

        case NFS3ERR_SERVERFAULT:
		ll = GF_LOG_WARNING;
                break;

        case NFS3ERR_NOTSUPP:
		ll = GF_LOG_WARNING;
                break;

        case NFS3ERR_BADHANDLE:
		ll = GF_LOG_WARNING;
                break;

        case NFS3ERR_STALE:
		ll = GF_LOG_WARNING;
                break;

        case NFS3ERR_DQUOT:
		ll = GF_LOG_WARNING;
                break;

        default:
		ll = GF_LOG_DEBUG;
                break;
        }

        return ll;
}


int
nfs3_readlink_loglevel (nfsstat3 stat) {

	int ll = GF_LOG_DEBUG;

	switch (stat) {

        case NFS3ERR_EXIST:
		ll = GF_LOG_WARNING;
                break;

        case NFS3ERR_XDEV:
		ll = GF_LOG_WARNING;
                break;

        case NFS3ERR_NODEV:
		ll = GF_LOG_WARNING;
                break;

        case NFS3ERR_IO:
		ll = GF_LOG_WARNING;
                break;

        case NFS3ERR_NXIO:
		ll = GF_LOG_WARNING;
                break;

        case NFS3ERR_NOTDIR:
		ll = GF_LOG_WARNING;
                break;

        case NFS3ERR_ISDIR:
		ll = GF_LOG_WARNING;
                break;

        case NFS3ERR_INVAL:
		ll = GF_LOG_WARNING;
                break;

        case NFS3ERR_NOSPC:
		ll = GF_LOG_WARNING;
                break;

        case NFS3ERR_ROFS:
		ll = GF_LOG_WARNING;
                break;

        case NFS3ERR_FBIG:
		ll = GF_LOG_WARNING;
                break;

        case NFS3ERR_MLINK:
		ll = GF_LOG_WARNING;
                break;

        case NFS3ERR_NOTEMPTY:
		ll = GF_LOG_WARNING;
                break;

        case NFS3ERR_SERVERFAULT:
		ll = GF_LOG_WARNING;
                break;

        case NFS3ERR_NOTSUPP:
		ll = GF_LOG_WARNING;
                break;

        case NFS3ERR_BADHANDLE:
		ll = GF_LOG_WARNING;
                break;

        case NFS3ERR_STALE:
		ll = GF_LOG_WARNING;
                break;

        case NFS3ERR_DQUOT:
		ll = GF_LOG_WARNING;
                break;

        default:
		ll = GF_LOG_DEBUG;
                break;
        }

        return ll;
}

int
nfs3_read_loglevel (nfsstat3 stat) {

	int ll = GF_LOG_DEBUG;

	switch (stat) {

        case NFS3ERR_NOENT:
		ll = GF_LOG_WARNING;
                break;

        case NFS3ERR_EXIST:
		ll = GF_LOG_WARNING;
                break;

        case NFS3ERR_XDEV:
		ll = GF_LOG_WARNING;
                break;

        case NFS3ERR_NODEV:
		ll = GF_LOG_WARNING;
                break;

        case NFS3ERR_IO:
		ll = GF_LOG_WARNING;
                break;

        case NFS3ERR_NXIO:
		ll = GF_LOG_WARNING;
                break;

        case NFS3ERR_NOTDIR:
		ll = GF_LOG_WARNING;
                break;

        case NFS3ERR_ISDIR:
		ll = GF_LOG_WARNING;
                break;

        case NFS3ERR_INVAL:
		ll = GF_LOG_WARNING;
                break;

        case NFS3ERR_NOSPC:
		ll = GF_LOG_WARNING;
                break;

        case NFS3ERR_ROFS:
		ll = GF_LOG_WARNING;
                break;

        case NFS3ERR_FBIG:
		ll = GF_LOG_WARNING;
                break;

        case NFS3ERR_MLINK:
		ll = GF_LOG_WARNING;
                break;

        case NFS3ERR_NAMETOOLONG:
		ll = GF_LOG_WARNING;
                break;

        case NFS3ERR_NOTEMPTY:
		ll = GF_LOG_WARNING;
                break;

        case NFS3ERR_SERVERFAULT:
		ll = GF_LOG_WARNING;
                break;

        case NFS3ERR_NOTSUPP:
		ll = GF_LOG_WARNING;
                break;

        case NFS3ERR_BADHANDLE:
		ll = GF_LOG_WARNING;
                break;

        case NFS3ERR_STALE:
		ll = GF_LOG_WARNING;
                break;

        case NFS3ERR_DQUOT:
		ll = GF_LOG_WARNING;
                break;

        default:
		ll = GF_LOG_DEBUG;
                break;
        }

        return ll;
}


int
nfs3_write_loglevel (nfsstat3 stat) {

	int ll = GF_LOG_DEBUG;

	switch (stat) {

        case NFS3ERR_NOENT:
		ll = GF_LOG_WARNING;
                break;

        case NFS3ERR_EXIST:
		ll = GF_LOG_WARNING;
                break;

        case NFS3ERR_XDEV:
		ll = GF_LOG_WARNING;
                break;

        case NFS3ERR_NODEV:
		ll = GF_LOG_WARNING;
                break;

        case NFS3ERR_IO:
		ll = GF_LOG_WARNING;
                break;

        case NFS3ERR_NXIO:
		ll = GF_LOG_WARNING;
                break;

        case NFS3ERR_NOTDIR:
		ll = GF_LOG_WARNING;
                break;

        case NFS3ERR_ISDIR:
		ll = GF_LOG_WARNING;
                break;

        case NFS3ERR_INVAL:
		ll = GF_LOG_WARNING;
                break;

        case NFS3ERR_NOSPC:
		ll = GF_LOG_WARNING;
                break;

        case NFS3ERR_ROFS:
		ll = GF_LOG_WARNING;
                break;

        case NFS3ERR_FBIG:
		ll = GF_LOG_WARNING;
                break;

        case NFS3ERR_MLINK:
		ll = GF_LOG_WARNING;
                break;

        case NFS3ERR_NAMETOOLONG:
		ll = GF_LOG_WARNING;
                break;

        case NFS3ERR_NOTEMPTY:
		ll = GF_LOG_WARNING;
                break;

        case NFS3ERR_SERVERFAULT:
		ll = GF_LOG_WARNING;
                break;

        case NFS3ERR_NOTSUPP:
		ll = GF_LOG_WARNING;
                break;

        case NFS3ERR_BADHANDLE:
		ll = GF_LOG_WARNING;
                break;

        case NFS3ERR_STALE:
		ll = GF_LOG_WARNING;
                break;

        case NFS3ERR_DQUOT:
		ll = GF_LOG_WARNING;
                break;

        default:
		ll = GF_LOG_DEBUG;
                break;
        }

        return ll;
}


int
nfs3_create_loglevel (nfsstat3 stat) {

	int ll = GF_LOG_DEBUG;

	switch (stat) {

        case NFS3ERR_NOENT:
		ll = GF_LOG_WARNING;
                break;

        case NFS3ERR_EXIST:
		ll = GF_LOG_WARNING;
                break;

        case NFS3ERR_XDEV:
		ll = GF_LOG_WARNING;
                break;

        case NFS3ERR_NODEV:
		ll = GF_LOG_WARNING;
                break;

        case NFS3ERR_IO:
		ll = GF_LOG_WARNING;
                break;

        case NFS3ERR_NXIO:
		ll = GF_LOG_WARNING;
                break;

        case NFS3ERR_NOTDIR:
		ll = GF_LOG_WARNING;
                break;

        case NFS3ERR_ISDIR:
		ll = GF_LOG_WARNING;
                break;

        case NFS3ERR_INVAL:
		ll = GF_LOG_WARNING;
                break;

        case NFS3ERR_FBIG:
		ll = GF_LOG_WARNING;
                break;

        case NFS3ERR_MLINK:
		ll = GF_LOG_WARNING;
                break;

        case NFS3ERR_NOTEMPTY:
		ll = GF_LOG_WARNING;
                break;

        case NFS3ERR_SERVERFAULT:
		ll = GF_LOG_WARNING;
                break;

        case NFS3ERR_NOTSUPP:
		ll = GF_LOG_WARNING;
                break;

        case NFS3ERR_BADHANDLE:
		ll = GF_LOG_WARNING;
                break;

        case NFS3ERR_STALE:
		ll = GF_LOG_WARNING;
                break;

        default:
		ll = GF_LOG_DEBUG;
                break;
        }

        return ll;
}


int
nfs3_mkdir_loglevel (nfsstat3 stat) {

	int ll = GF_LOG_DEBUG;

	switch (stat) {

        case NFS3ERR_NOENT:
		ll = GF_LOG_WARNING;
                break;

        case NFS3ERR_XDEV:
		ll = GF_LOG_WARNING;
                break;

        case NFS3ERR_NODEV:
		ll = GF_LOG_WARNING;
                break;

        case NFS3ERR_IO:
		ll = GF_LOG_WARNING;
                break;

        case NFS3ERR_NXIO:
		ll = GF_LOG_WARNING;
                break;

        case NFS3ERR_NOTDIR:
		ll = GF_LOG_WARNING;
                break;

        case NFS3ERR_ISDIR:
		ll = GF_LOG_WARNING;
                break;

        case NFS3ERR_INVAL:
		ll = GF_LOG_WARNING;
                break;

        case NFS3ERR_FBIG:
		ll = GF_LOG_WARNING;
                break;

        case NFS3ERR_MLINK:
		ll = GF_LOG_WARNING;
                break;

        case NFS3ERR_NOTEMPTY:
		ll = GF_LOG_WARNING;
                break;

        case NFS3ERR_SERVERFAULT:
		ll = GF_LOG_WARNING;
                break;

        case NFS3ERR_NOTSUPP:
		ll = GF_LOG_WARNING;
                break;

        case NFS3ERR_BADHANDLE:
		ll = GF_LOG_WARNING;
                break;

        case NFS3ERR_STALE:
		ll = GF_LOG_WARNING;
                break;

        case NFS3ERR_DQUOT:
		ll = GF_LOG_WARNING;
                break;

        default:
		ll = GF_LOG_DEBUG;
                break;
        }

        return ll;
}


int
nfs3_symlink_loglevel (nfsstat3 stat) {

	int ll = GF_LOG_DEBUG;

	switch (stat) {

        case NFS3ERR_XDEV:
		ll = GF_LOG_WARNING;
                break;

        case NFS3ERR_NODEV:
		ll = GF_LOG_WARNING;
                break;

        case NFS3ERR_IO:
		ll = GF_LOG_WARNING;
                break;

        case NFS3ERR_NXIO:
		ll = GF_LOG_WARNING;
                break;

        case NFS3ERR_NOTDIR:
		ll = GF_LOG_WARNING;
                break;

        case NFS3ERR_ISDIR:
		ll = GF_LOG_WARNING;
                break;

        case NFS3ERR_INVAL:
		ll = GF_LOG_WARNING;
                break;

        case NFS3ERR_FBIG:
		ll = GF_LOG_WARNING;
                break;

        case NFS3ERR_MLINK:
		ll = GF_LOG_WARNING;
                break;

        case NFS3ERR_NOTEMPTY:
		ll = GF_LOG_WARNING;
                break;

        case NFS3ERR_SERVERFAULT:
		ll = GF_LOG_WARNING;
                break;

        case NFS3ERR_NOTSUPP:
		ll = GF_LOG_WARNING;
                break;

        case NFS3ERR_BADHANDLE:
		ll = GF_LOG_WARNING;
                break;

        case NFS3ERR_STALE:
		ll = GF_LOG_WARNING;
                break;

        case NFS3ERR_DQUOT:
		ll = GF_LOG_WARNING;
                break;

        default:
		ll = GF_LOG_DEBUG;
                break;
        }

        return ll;
}


int
nfs3_mknod_loglevel (nfsstat3 stat) {

	int ll = GF_LOG_DEBUG;

	switch (stat) {

        case NFS3ERR_NOENT:
		ll = GF_LOG_WARNING;
                break;

        case NFS3ERR_XDEV:
		ll = GF_LOG_WARNING;
                break;

        case NFS3ERR_NODEV:
		ll = GF_LOG_WARNING;
                break;

        case NFS3ERR_IO:
		ll = GF_LOG_WARNING;
                break;

        case NFS3ERR_NXIO:
		ll = GF_LOG_WARNING;
                break;

        case NFS3ERR_NOTDIR:
		ll = GF_LOG_WARNING;
                break;

        case NFS3ERR_ISDIR:
		ll = GF_LOG_WARNING;
                break;

        case NFS3ERR_INVAL:
		ll = GF_LOG_WARNING;
                break;

        case NFS3ERR_FBIG:
		ll = GF_LOG_WARNING;
                break;

        case NFS3ERR_MLINK:
		ll = GF_LOG_WARNING;
                break;

        case NFS3ERR_NOTEMPTY:
		ll = GF_LOG_WARNING;
                break;

        case NFS3ERR_SERVERFAULT:
		ll = GF_LOG_WARNING;
                break;

        case NFS3ERR_NOTSUPP:
		ll = GF_LOG_WARNING;
                break;

        case NFS3ERR_BADHANDLE:
		ll = GF_LOG_WARNING;
                break;

        case NFS3ERR_STALE:
		ll = GF_LOG_WARNING;
                break;

        default:
		ll = GF_LOG_DEBUG;
                break;
        }

        return ll;
}

int
nfs3_remove_loglevel (nfsstat3 stat) {

	int ll = GF_LOG_DEBUG;

	switch (stat) {

        case NFS3ERR_EXIST:
		ll = GF_LOG_WARNING;
                break;

        case NFS3ERR_XDEV:
		ll = GF_LOG_WARNING;
                break;

        case NFS3ERR_NODEV:
		ll = GF_LOG_WARNING;
                break;

        case NFS3ERR_IO:
		ll = GF_LOG_WARNING;
                break;

        case NFS3ERR_NXIO:
		ll = GF_LOG_WARNING;
                break;

        case NFS3ERR_NOTDIR:
		ll = GF_LOG_WARNING;
                break;

        case NFS3ERR_INVAL:
		ll = GF_LOG_WARNING;
                break;

        case NFS3ERR_NOSPC:
		ll = GF_LOG_WARNING;
                break;

        case NFS3ERR_FBIG:
		ll = GF_LOG_WARNING;
                break;

        case NFS3ERR_MLINK:
		ll = GF_LOG_WARNING;
                break;

        case NFS3ERR_SERVERFAULT:
		ll = GF_LOG_WARNING;
                break;

        case NFS3ERR_NOTSUPP:
		ll = GF_LOG_WARNING;
                break;

        case NFS3ERR_BADHANDLE:
		ll = GF_LOG_WARNING;
                break;

        case NFS3ERR_STALE:
		ll = GF_LOG_WARNING;
                break;

        case NFS3ERR_DQUOT:
		ll = GF_LOG_WARNING;
                break;

        default:
		ll = GF_LOG_DEBUG;
                break;
        }

        return ll;
}


int
nfs3_rmdir_loglevel (nfsstat3 stat) {

	int ll = GF_LOG_DEBUG;

	switch (stat) {

        case NFS3ERR_EXIST:
		ll = GF_LOG_WARNING;
                break;

        case NFS3ERR_XDEV:
		ll = GF_LOG_WARNING;
                break;

        case NFS3ERR_NODEV:
		ll = GF_LOG_WARNING;
                break;

        case NFS3ERR_IO:
		ll = GF_LOG_WARNING;
                break;

        case NFS3ERR_NXIO:
		ll = GF_LOG_WARNING;
                break;

        case NFS3ERR_NOTDIR:
		ll = GF_LOG_WARNING;
                break;

        case NFS3ERR_INVAL:
		ll = GF_LOG_WARNING;
                break;

        case NFS3ERR_NOSPC:
		ll = GF_LOG_WARNING;
                break;

        case NFS3ERR_FBIG:
		ll = GF_LOG_WARNING;
                break;

        case NFS3ERR_MLINK:
		ll = GF_LOG_WARNING;
                break;

        case NFS3ERR_SERVERFAULT:
		ll = GF_LOG_WARNING;
                break;

        case NFS3ERR_NOTSUPP:
		ll = GF_LOG_WARNING;
                break;

        case NFS3ERR_BADHANDLE:
		ll = GF_LOG_WARNING;
                break;

        case NFS3ERR_STALE:
		ll = GF_LOG_WARNING;
                break;

        case NFS3ERR_DQUOT:
		ll = GF_LOG_WARNING;
                break;

        default:
		ll = GF_LOG_DEBUG;
                break;
        }

        return ll;
}


int
nfs3_rename_loglevel (nfsstat3 stat) {

	int ll = GF_LOG_DEBUG;

	switch (stat) {

        case NFS3ERR_XDEV:
		ll = GF_LOG_WARNING;
                break;

        case NFS3ERR_NODEV:
		ll = GF_LOG_WARNING;
                break;

        case NFS3ERR_IO:
		ll = GF_LOG_WARNING;
                break;

        case NFS3ERR_NXIO:
		ll = GF_LOG_WARNING;
                break;

        case NFS3ERR_NOTDIR:
		ll = GF_LOG_WARNING;
                break;

        case NFS3ERR_ISDIR:
		ll = GF_LOG_WARNING;
                break;

        case NFS3ERR_INVAL:
		ll = GF_LOG_WARNING;
                break;

        case NFS3ERR_NOSPC:
		ll = GF_LOG_WARNING;
                break;

        case NFS3ERR_FBIG:
		ll = GF_LOG_WARNING;
                break;

        case NFS3ERR_MLINK:
		ll = GF_LOG_WARNING;
                break;

        case NFS3ERR_NOTEMPTY:
		ll = GF_LOG_WARNING;
                break;

        case NFS3ERR_SERVERFAULT:
		ll = GF_LOG_WARNING;
                break;

        case NFS3ERR_NOTSUPP:
		ll = GF_LOG_WARNING;
                break;

        case NFS3ERR_BADHANDLE:
		ll = GF_LOG_WARNING;
                break;

        case NFS3ERR_STALE:
		ll = GF_LOG_WARNING;
                break;

        case NFS3ERR_DQUOT:
		ll = GF_LOG_WARNING;
                break;

        default:
		ll = GF_LOG_DEBUG;
                break;
        }

        return ll;
}


int
nfs3_link_loglevel (nfsstat3 stat) {

	int ll = GF_LOG_DEBUG;

	switch (stat) {

        case NFS3ERR_XDEV:
		ll = GF_LOG_WARNING;
                break;

        case NFS3ERR_NODEV:
		ll = GF_LOG_WARNING;
                break;

        case NFS3ERR_IO:
		ll = GF_LOG_WARNING;
                break;

        case NFS3ERR_NXIO:
		ll = GF_LOG_WARNING;
                break;

        case NFS3ERR_INVAL:
		ll = GF_LOG_WARNING;
                break;

        case NFS3ERR_FBIG:
		ll = GF_LOG_WARNING;
                break;

        case NFS3ERR_MLINK:
		ll = GF_LOG_WARNING;
                break;

        case NFS3ERR_NOTEMPTY:
		ll = GF_LOG_WARNING;
                break;

        case NFS3ERR_SERVERFAULT:
		ll = GF_LOG_WARNING;
                break;

        case NFS3ERR_NOTSUPP:
		ll = GF_LOG_WARNING;
                break;

        case NFS3ERR_BADHANDLE:
		ll = GF_LOG_WARNING;
                break;

        case NFS3ERR_STALE:
		ll = GF_LOG_WARNING;
                break;

        case NFS3ERR_DQUOT:
		ll = GF_LOG_WARNING;
                break;

        default:
		ll = GF_LOG_DEBUG;
                break;
        }

        return ll;
}


int
nfs3_readdir_loglevel (nfsstat3 stat) {

	int ll = GF_LOG_DEBUG;

	switch (stat) {

        case NFS3ERR_NOENT:
		ll = GF_LOG_WARNING;
                break;

        case NFS3ERR_EXIST:
		ll = GF_LOG_WARNING;
                break;

        case NFS3ERR_XDEV:
		ll = GF_LOG_WARNING;
                break;

        case NFS3ERR_NODEV:
		ll = GF_LOG_WARNING;
                break;

        case NFS3ERR_IO:
		ll = GF_LOG_WARNING;
                break;

        case NFS3ERR_NXIO:
		ll = GF_LOG_WARNING;
                break;

        case NFS3ERR_NOTDIR:
		ll = GF_LOG_WARNING;
                break;

        case NFS3ERR_ISDIR:
		ll = GF_LOG_WARNING;
                break;

        case NFS3ERR_INVAL:
		ll = GF_LOG_WARNING;
                break;

        case NFS3ERR_NOSPC:
		ll = GF_LOG_WARNING;
                break;

        case NFS3ERR_ROFS:
		ll = GF_LOG_WARNING;
                break;

        case NFS3ERR_FBIG:
		ll = GF_LOG_WARNING;
                break;

        case NFS3ERR_MLINK:
		ll = GF_LOG_WARNING;
                break;

        case NFS3ERR_NAMETOOLONG:
		ll = GF_LOG_WARNING;
                break;

        case NFS3ERR_NOTEMPTY:
		ll = GF_LOG_WARNING;
                break;

        case NFS3ERR_SERVERFAULT:
		ll = GF_LOG_WARNING;
                break;

        case NFS3ERR_NOTSUPP:
		ll = GF_LOG_WARNING;
                break;

        case NFS3ERR_BADHANDLE:
		ll = GF_LOG_WARNING;
                break;

        case NFS3ERR_STALE:
		ll = GF_LOG_WARNING;
                break;

        case NFS3ERR_DQUOT:
		ll = GF_LOG_WARNING;
                break;

        default:
		ll = GF_LOG_DEBUG;
                break;
        }

        return ll;
}


int
nfs3_fsstat_loglevel (nfsstat3 stat) {

	int ll = GF_LOG_DEBUG;

	switch (stat) {

        case NFS3ERR_PERM:
		ll = GF_LOG_WARNING;
                break;

        case NFS3ERR_NOENT:
		ll = GF_LOG_WARNING;
                break;

        case NFS3ERR_ACCES:
		ll = GF_LOG_WARNING;
                break;

        case NFS3ERR_EXIST:
		ll = GF_LOG_WARNING;
                break;

        case NFS3ERR_XDEV:
		ll = GF_LOG_WARNING;
                break;

        case NFS3ERR_NODEV:
		ll = GF_LOG_WARNING;
                break;

        case NFS3ERR_IO:
		ll = GF_LOG_WARNING;
                break;

        case NFS3ERR_NXIO:
		ll = GF_LOG_WARNING;
                break;

        case NFS3ERR_NOTDIR:
		ll = GF_LOG_WARNING;
                break;

        case NFS3ERR_ISDIR:
		ll = GF_LOG_WARNING;
                break;

        case NFS3ERR_INVAL:
		ll = GF_LOG_WARNING;
                break;

        case NFS3ERR_NOSPC:
		ll = GF_LOG_WARNING;
                break;

        case NFS3ERR_ROFS:
		ll = GF_LOG_WARNING;
                break;

        case NFS3ERR_FBIG:
		ll = GF_LOG_WARNING;
                break;

        case NFS3ERR_MLINK:
		ll = GF_LOG_WARNING;
                break;

        case NFS3ERR_NAMETOOLONG:
		ll = GF_LOG_WARNING;
                break;

        case NFS3ERR_NOTEMPTY:
		ll = GF_LOG_WARNING;
                break;

        case NFS3ERR_SERVERFAULT:
		ll = GF_LOG_WARNING;
                break;

        case NFS3ERR_NOTSUPP:
		ll = GF_LOG_WARNING;
                break;

        case NFS3ERR_BADHANDLE:
		ll = GF_LOG_WARNING;
                break;

        case NFS3ERR_STALE:
		ll = GF_LOG_WARNING;
                break;

        case NFS3ERR_DQUOT:
		ll = GF_LOG_WARNING;
                break;

        default:
		ll = GF_LOG_DEBUG;
                break;
        }

        return ll;
}

struct nfs3op_str {
	int	op;
	char	str[100];
};

struct nfs3op_str nfs3op_strings[] = {
	{ NFS3_NULL, "NULL"},
	{ NFS3_GETATTR, "GETATTR"},
	{ NFS3_SETATTR, "SETATTR"},
	{ NFS3_LOOKUP, "LOOKUP"},
	{ NFS3_ACCESS, "ACCESS"},
	{ NFS3_READLINK, "READLINK"},
	{ NFS3_READ, "READ"},
	{ NFS3_WRITE, "WRITE"},
	{ NFS3_CREATE, "CREATE"},
	{ NFS3_MKDIR, "MKDIR"},
	{ NFS3_SYMLINK, "SYMLINK"},
	{ NFS3_MKNOD, "MKNOD"},
	{ NFS3_REMOVE, "REMOVE"},
	{ NFS3_RMDIR, "RMDIR"},
	{ NFS3_RENAME, "RENAME"},
	{ NFS3_LINK, "LINK"},
	{ NFS3_READDIR, "READDIR"},
	{ NFS3_READDIRP, "READDIRP"},
	{ NFS3_FSSTAT, "FSSTAT"},
	{ NFS3_FSINFO, "FSINFO"},
	{ NFS3_PATHCONF, "PATHCONF"},
	{ NFS3_COMMIT, "COMMIT"},
};

int
nfs3_loglevel (int nfs_op, nfsstat3 stat) {

	int	ll = GF_LOG_DEBUG;

	switch (nfs_op) {
	case NFS3_GETATTR:
		ll = nfs3_getattr_loglevel (stat);
		break;

	case NFS3_SETATTR:
		ll = nfs3_setattr_loglevel (stat);
		break;

	case NFS3_LOOKUP:
		ll = nfs3_lookup_loglevel (stat);
		break;

	case NFS3_ACCESS:
		ll = nfs3_access_loglevel (stat);
		break;

	case NFS3_READLINK:
		ll = nfs3_readlink_loglevel (stat);
		break;

	case NFS3_READ:
		ll = nfs3_read_loglevel (stat);
		break;

	case NFS3_WRITE:
		ll = nfs3_write_loglevel (stat);
		break;

	case NFS3_CREATE:
		ll = nfs3_create_loglevel (stat);
		break;

	case NFS3_MKDIR:
		ll = nfs3_mkdir_loglevel (stat);
		break;

	case NFS3_SYMLINK:
		ll = nfs3_symlink_loglevel (stat);
		break;

	case NFS3_MKNOD:
		ll = nfs3_mknod_loglevel (stat);
		break;

	case NFS3_REMOVE:
		ll = nfs3_remove_loglevel (stat);
		break;

	case NFS3_RMDIR:
		ll = nfs3_rmdir_loglevel (stat);
		break;

	case NFS3_RENAME:
		ll = nfs3_rename_loglevel (stat);
		break;

	case NFS3_LINK:
		ll = nfs3_link_loglevel (stat);
		break;

	case NFS3_READDIR:
		ll = nfs3_readdir_loglevel (stat);
		break;

	case NFS3_READDIRP:
		ll = nfs3_readdir_loglevel (stat);
		break;

	case NFS3_FSSTAT:
		ll = nfs3_fsstat_loglevel (stat);
		break;

	case NFS3_FSINFO:
		ll = nfs3_fsstat_loglevel (stat);
		break;

	case NFS3_PATHCONF:
		ll = nfs3_fsstat_loglevel (stat);
		break;

	case NFS3_COMMIT:
		ll = nfs3_write_loglevel (stat);
		break;

	default:
		ll = GF_LOG_DEBUG;
		break;
	}

	return ll;
}

void
nfs3_log_common_res (uint32_t xid, int op, nfsstat3 stat, int pstat,
                     const char *path)
{
        char    errstr[1024];
        int     ll = nfs3_loglevel (op, stat);

        if (THIS->ctx->log.loglevel < ll)
                return;
        nfs3_stat_to_errstr (xid, nfs3op_strings[op].str, stat, pstat, errstr, sizeof (errstr));
                if (ll == GF_LOG_DEBUG)
                        gf_msg_debug (GF_NFS3, 0, "%s => (%s)", path,
                                      errstr);
                else
                        gf_msg (GF_NFS3, ll, errno, NFS_MSG_STAT_ERROR,
                                "%s => (%s)", path, errstr);

}

void
nfs3_log_readlink_res (uint32_t xid, nfsstat3 stat, int pstat, char *linkpath,
                       const char *path)
{
        char    errstr[1024];
        int     ll = nfs3_loglevel (NFS3_READLINK, stat);

        if (THIS->ctx->log.loglevel < ll)
                return;

        nfs3_stat_to_errstr (xid, "READLINK", stat, pstat, errstr, sizeof (errstr));
        if (ll == GF_LOG_DEBUG)
                gf_msg_debug (GF_NFS3, 0, "%s => (%s), target: %s", path,
                              errstr, linkpath);
        else
                gf_msg (GF_NFS3, ll, errno, NFS_MSG_STAT_ERROR,
                        "%s => (%s) target: %s" , path,
                        errstr, linkpath);
}

void
nfs3_log_read_res (uint32_t xid, nfsstat3 stat, int pstat, count3 count,
                   int is_eof, struct iovec *vec,
                   int32_t veccount, const char *path)
{
        char    errstr[1024];
        int     ll = GF_LOG_DEBUG;

        ll = nfs3_loglevel (NFS3_READ, stat);
        if (THIS->ctx->log.loglevel < ll)
                return;
        nfs3_stat_to_errstr (xid, "READ", stat, pstat, errstr, sizeof (errstr));
        if (vec)
                if (ll == GF_LOG_DEBUG)
                        gf_msg_debug (GF_NFS3, 0,
                                      "%s => (%s), count: %"PRIu32", is_eof:"
                                      " %d, vector: count: %d, len: %zd", path,
                                      errstr, count, is_eof, veccount,
                                      vec->iov_len);
                else
                        gf_msg (GF_NFS3, ll, errno, NFS_MSG_STAT_ERROR,
                                "%s => (%s), count: %"PRIu32", is_eof:"
                                " %d, vector: count: %d, len: %zd", path,
                                errstr, count, is_eof, veccount, vec->iov_len);
        else
                if (ll == GF_LOG_DEBUG)
                        gf_msg_debug (GF_NFS3, 0,
                                      "%s => (%s), count: %"PRIu32", is_eof:"
                                      " %d", path, errstr, count, is_eof);
                else
                        gf_msg (GF_NFS3, ll, errno, NFS_MSG_STAT_ERROR,
                                "%s => (%s), count: %"PRIu32", is_eof:"
                                " %d", path, errstr, count, is_eof);

}

void
nfs3_log_write_res (uint32_t xid, nfsstat3 stat, int pstat, count3 count,
                    int stable, uint64_t wverf, const char *path)
{
        char    errstr[1024];
        int     ll = nfs3_loglevel (NFS3_WRITE, stat);

        if (THIS->ctx->log.loglevel < ll)
                return;

        nfs3_stat_to_errstr (xid, "WRITE", stat, pstat, errstr, sizeof (errstr));
        if (ll == GF_LOG_DEBUG)
                gf_msg_debug (GF_NFS3, 0,
                              "%s => (%s), count: %"PRIu32", %s,wverf: "
                              "%"PRIu64, path, errstr, count,
                              (stable == UNSTABLE)?"UNSTABLE":"STABLE", wverf);
        else
                gf_msg (GF_NFS3, ll, errno, NFS_MSG_STAT_ERROR,
                        "%s => (%s), count: %"PRIu32", %s,wverf: %"PRIu64
                        , path, errstr, count,
                        (stable == UNSTABLE)?"UNSTABLE":"STABLE", wverf);
}

void
nfs3_log_newfh_res (uint32_t xid, int op, nfsstat3 stat, int pstat,
                    struct nfs3_fh *newfh, const char *path)
{
        char    errstr[1024];
        char    fhstr[1024];
        int     ll = nfs3_loglevel (op, stat);

        if (THIS->ctx->log.loglevel < ll)
                return;
        nfs3_stat_to_errstr (xid, nfs3op_strings[op].str, stat, pstat, errstr, sizeof (errstr));
        nfs3_fh_to_str (newfh, fhstr, sizeof (fhstr));

        if (ll == GF_LOG_DEBUG)
                gf_msg_debug (GF_NFS3, 0, "%s => (%s), %s", path, errstr,
                              fhstr);
        else
                gf_msg (GF_NFS3, nfs3_loglevel (op, stat), errno, NFS_MSG_STAT_ERROR,
                        "%s => (%s), %s", path, errstr, fhstr);
}

void
nfs3_log_readdir_res (uint32_t xid, nfsstat3 stat, int pstat, uint64_t cverf,
                      count3 count, int is_eof, const char *path)
{
        char    errstr[1024];
        int     ll = nfs3_loglevel (NFS3_READDIR, stat);

        if (THIS->ctx->log.loglevel < ll)
                return;
        nfs3_stat_to_errstr (xid, "READDIR", stat, pstat, errstr, sizeof (errstr));
        if (ll == GF_LOG_DEBUG)
                gf_msg_debug (GF_NFS3, 0,
                              "%s => (%s), count: %"PRIu32", cverf: %"PRIu64
                              ", is_eof: %d", path, errstr, count, cverf,
                              is_eof);
        else
                gf_msg (GF_NFS3, ll, errno, NFS_MSG_STAT_ERROR,
                        "%s => (%s), count: %"PRIu32", cverf: %"PRIu64
                        ", is_eof: %d", path, errstr, count, cverf, is_eof);
}

void
nfs3_log_readdirp_res (uint32_t xid, nfsstat3 stat, int pstat, uint64_t cverf,
                       count3 dircount, count3 maxcount, int is_eof,
                       const char *path)
{
        char    errstr[1024];
        int	ll = nfs3_loglevel (NFS3_READDIRP, stat);

        if (THIS->ctx->log.loglevel < ll)
                return;
        nfs3_stat_to_errstr (xid, "READDIRPLUS", stat, pstat, errstr, sizeof (errstr));
        if (ll == GF_LOG_DEBUG)
                gf_msg_debug (GF_NFS3, 0,
                              "%s => (%s), dircount: %"PRIu32", maxcount: %"
                              PRIu32", cverf: %"PRIu64", is_eof: %d", path,
                              errstr, dircount, maxcount, cverf, is_eof);
        else
                gf_msg (GF_NFS3, ll, errno, NFS_MSG_STAT_ERROR,
                        "%s => (%s), dircount: %"PRIu32", maxcount: %"
                        PRIu32", cverf: %"PRIu64", is_eof: %d", path, errstr,
                        dircount, maxcount, cverf, is_eof);

}

void
nfs3_log_commit_res (uint32_t xid, nfsstat3 stat, int pstat, uint64_t wverf,
                     const char *path)
{
        char    errstr[1024];
        int	ll = nfs3_loglevel (NFS3_COMMIT, stat);

        if (THIS->ctx->log.loglevel < ll)
                return;
        nfs3_stat_to_errstr (xid, "COMMIT", stat, pstat, errstr, sizeof (errstr));
        if (ll == GF_LOG_DEBUG)
                gf_msg_debug (GF_NFS3, 0, "%s => (%s), wverf: %"PRIu64,
                              path, errstr, wverf);
        else
                gf_msg (GF_NFS3, ll, errno, NFS_MSG_STAT_ERROR,
                        "%s => (%s), wverf: %"PRIu64, path, errstr, wverf);

}

void
nfs3_log_readdir_call (uint32_t xid, struct nfs3_fh *fh, count3 dircount,
                       count3 maxcount)
{
        char    fhstr[1024];

        if (THIS->ctx->log.loglevel < GF_LOG_DEBUG)
                return;

        nfs3_fh_to_str (fh, fhstr, sizeof (fhstr));

        if (maxcount == 0)
                gf_msg_debug (GF_NFS3, 0, "XID: %x, READDIR: args: %s,"
                                " count: %d", xid, fhstr, (uint32_t)dircount);
        else
                gf_msg_debug (GF_NFS3, 0, "XID: %x, READDIRPLUS: args: %s,"
                                " dircount: %d, maxcount: %d", xid, fhstr,
                                (uint32_t)dircount, (uint32_t)maxcount);
}

int
nfs3_fh_resolve_inode_done (nfs3_call_state_t *cs, inode_t *inode)
{
        int             ret = -EFAULT;

        if ((!cs) || (!inode))
                return ret;

        gf_msg_trace (GF_NFS3, 0, "FH inode resolved");
        ret = nfs_inode_loc_fill (inode, &cs->resolvedloc, NFS_RESOLVE_EXIST);
        if (ret < 0) {
                gf_msg (GF_NFS3, GF_LOG_ERROR, -ret,
                                NFS_MSG_INODE_LOC_FILL_ERROR,
                                "inode loc fill failed");
                goto err;
        }

        nfs3_call_resume (cs);

err:
        return ret;
}

int32_t
nfs3_fh_resolve_entry_lookup_cbk (call_frame_t *frame, void *cookie,
                                  xlator_t *this, int32_t op_ret,
                                  int32_t op_errno, inode_t *inode,
                                  struct iatt *buf, dict_t *xattr,
                                  struct iatt *postparent)
{
        nfs3_call_state_t       *cs = NULL;
        inode_t                 *linked_inode = NULL;

        cs = frame->local;
        cs->resolve_ret = op_ret;
        cs->resolve_errno = op_errno;

        if (op_ret == -1) {
                if (op_errno == ENOENT) {
                        gf_msg_trace (GF_NFS3, 0, "Lookup failed: %s: %s",
                                        cs->resolvedloc.path,
                                        strerror (op_errno));
                } else {
                        gf_msg (GF_NFS3, GF_LOG_ERROR, op_errno,
                                        NFS_MSG_LOOKUP_FAIL, "Lookup failed: %s: %s",
                                        cs->resolvedloc.path, strerror (op_errno));
                }
                goto err;
        } else
                gf_msg_trace (GF_NFS3, 0, "Entry looked up: %s",
                                cs->resolvedloc.path);

       memcpy (&cs->stbuf, buf, sizeof (*buf));
       memcpy (&cs->postparent, postparent, sizeof (*postparent));
        linked_inode = inode_link (inode, cs->resolvedloc.parent,
                        cs->resolvedloc.name, buf);
        if (linked_inode) {
                nfs_fix_generation (this, linked_inode);
                inode_lookup (linked_inode);
                inode_unref (cs->resolvedloc.inode);
                cs->resolvedloc.inode = linked_inode;
        }
err:
        nfs3_call_resume (cs);
        return 0;
}

int32_t
nfs3_fh_resolve_inode_lookup_cbk (call_frame_t *frame, void *cookie,
                                  xlator_t *this, int32_t op_ret,
                                  int32_t op_errno, inode_t *inode,
                                  struct iatt *buf, dict_t *xattr,
                                  struct iatt *postparent)
{
        nfs3_call_state_t       *cs = NULL;
        inode_t                 *linked_inode = NULL;

        cs = frame->local;
        cs->resolve_ret = op_ret;
        cs->resolve_errno = op_errno;

        if (op_ret == -1) {
                if (op_errno == ENOENT) {
                        gf_msg_trace (GF_NFS3, 0, "Lookup failed: %s: %s",
                                        cs->resolvedloc.path,
                                        strerror (op_errno));
                } else {
                        gf_msg (GF_NFS3, GF_LOG_ERROR, op_errno,
                                        NFS_MSG_LOOKUP_FAIL, "Lookup failed: %s: %s",
                                        cs->resolvedloc.path, strerror (op_errno));
                }
                nfs3_call_resume (cs);
                goto err;
        }

        memcpy (&cs->stbuf, buf, sizeof(*buf));
        memcpy (&cs->postparent, buf, sizeof(*postparent));
        linked_inode = inode_link (inode, cs->resolvedloc.parent,
                                   cs->resolvedloc.name, buf);
        if (linked_inode) {
                nfs_fix_generation (this, linked_inode);
                inode_lookup (linked_inode);
		inode_unref (cs->resolvedloc.inode);
		cs->resolvedloc.inode = linked_inode;
        }

        /* If it is an entry lookup and we landed in the callback for hard
         * inode resolution, it means the parent inode was not available and
         * had to be resolved first. Now that is done, lets head back into
         * entry resolution.
         */
        if (cs->resolventry)
                nfs3_fh_resolve_entry_hard (cs);
	else
		nfs3_call_resume (cs);
err:
        return 0;
}


/* Needs no extra argument since it knows that the fh to be resolved is in
 * resolvefh and that it needs to start looking from the root.
 */
int
nfs3_fh_resolve_inode_hard (nfs3_call_state_t *cs)
{
        int             ret = -EFAULT;
        nfs_user_t      nfu = {0, };

        if (!cs)
                return ret;

        gf_msg_trace (GF_NFS3, 0, "FH hard resolution for: gfid 0x%s",
                      uuid_utoa (cs->resolvefh.gfid));
	cs->hardresolved = 1;
        nfs_loc_wipe (&cs->resolvedloc);
        ret = nfs_gfid_loc_fill (cs->vol->itable, cs->resolvefh.gfid,
                                 &cs->resolvedloc, NFS_RESOLVE_CREATE);
        if (ret < 0) {
                gf_msg (GF_NFS3, GF_LOG_ERROR, -ret,
                        NFS_MSG_INODE_LOC_FILL_ERROR,
                        "Failed to fill loc using gfid: "
                        "%s", strerror (-ret));
                goto out;
        }

        nfs_user_root_create (&nfu);
        ret = nfs_lookup (cs->nfsx, cs->vol, &nfu, &cs->resolvedloc,
                          nfs3_fh_resolve_inode_lookup_cbk, cs);

out:
        return ret;
}


int
nfs3_fh_resolve_entry_hard (nfs3_call_state_t *cs)
{
        int                     ret             = -EFAULT;
        nfs_user_t              nfu             = {0, };
        gf_boolean_t            freshlookup     = _gf_false;

        if (!cs)
                return ret;

        nfs_loc_wipe (&cs->resolvedloc);
        nfs_user_root_create (&nfu);
        gf_msg_trace (GF_NFS3, 0, "FH hard resolution: gfid: %s "
                      ", entry: %s", uuid_utoa (cs->resolvefh.gfid),
                      cs->resolventry);

        ret = nfs_entry_loc_fill (cs->nfsx, cs->vol->itable, cs->resolvefh.gfid,
                                  cs->resolventry, &cs->resolvedloc,
                                  NFS_RESOLVE_CREATE, &freshlookup);

        if (ret == -2) {
                gf_msg_trace (GF_NFS3, 0, "Entry needs lookup: %s",
                              cs->resolvedloc.path);
		/* If the NFS op is lookup, let the resume callback
		 * handle the sending of the lookup fop. Similarly,
		 * if the NFS op is create, let the create call
		 * go ahead in the resume callback so that an EEXIST gets
		 * handled at posix without an extra fop at this point.
		 */
                if (freshlookup && (nfs3_lookup_op (cs) ||
		    (nfs3_create_op (cs) && !nfs3_create_exclusive_op (cs)))) {
                        cs->lookuptype = GF_NFS3_FRESH;
                        cs->resolve_ret = 0;
                        cs->hardresolved = 0;
                        nfs3_call_resume (cs);
                } else {
			cs->hardresolved = 1;
                        nfs_lookup (cs->nfsx, cs->vol, &nfu, &cs->resolvedloc,
                                    nfs3_fh_resolve_entry_lookup_cbk, cs);
		}
                ret = 0;
        } else if (ret == -1) {
                gf_msg_trace (GF_NFS3, 0, "Entry needs parent lookup: %s",
                              cs->resolvedloc.path);
                ret = nfs3_fh_resolve_inode_hard (cs);
        } else if (ret == 0) {
                cs->resolve_ret = 0;
                nfs3_call_resume (cs);
        }

        return ret;
}

int
nfs3_fh_resolve_inode (nfs3_call_state_t *cs)
{
        inode_t         *inode = NULL;
        int             ret = -EFAULT;
        xlator_t        *this = NULL;

        if (!cs)
                return ret;

        this = cs->nfsx;
        gf_msg_trace (GF_NFS3, 0, "FH needs inode resolution");
        gf_uuid_copy (cs->resolvedloc.gfid, cs->resolvefh.gfid);

        inode = inode_find (cs->vol->itable, cs->resolvefh.gfid);
        if (!inode || inode_ctx_get (inode, this, NULL))
                ret = nfs3_fh_resolve_inode_hard (cs);
        else
                ret = nfs3_fh_resolve_inode_done (cs, inode);

        if (inode)
                inode_unref (inode);

        return ret;
}

int
nfs3_fh_resolve_entry (nfs3_call_state_t *cs)
{
        int     ret = -EFAULT;

        if (!cs)
                return ret;

        return nfs3_fh_resolve_entry_hard (cs);
}


int
nfs3_fh_resolve_resume (nfs3_call_state_t *cs)
{
        int     ret = -EFAULT;

        if (!cs)
                return ret;

        if (cs->resolve_ret < 0)
                goto err_resume_call;

        if (!cs->resolventry)
                ret = nfs3_fh_resolve_inode (cs);
        else
                ret = nfs3_fh_resolve_entry (cs);

err_resume_call:
        if (ret < 0) {
                cs->resolve_ret = -1;
                cs->resolve_errno = EFAULT;
                nfs3_call_resume (cs);
                ret = 0;
        }

        return ret;
}


int32_t
nfs3_fh_resolve_root_lookup_cbk (call_frame_t *frame, void *cookie,
                                 xlator_t *this, int32_t op_ret,
                                 int32_t op_errno, inode_t *inode,
                                 struct iatt *buf, dict_t *xattr,
                                 struct iatt *postparent)
{
        nfs3_call_state_t       *cs = NULL;

        cs = frame->local;
        cs->resolve_ret = op_ret;
        cs->resolve_errno = op_errno;

        if (op_ret == -1) {
                gf_msg (GF_NFS3, GF_LOG_ERROR, op_errno,
                        NFS_MSG_LOOKUP_ROOT_FAIL, "Root lookup failed: %s",
                        strerror (op_errno));
                goto err;
        } else
                gf_msg_trace (GF_NFS3, 0, "Root looked up: %s",
                              cs->resolvedloc.path);

        nfs3_set_root_looked_up (cs->nfs3state, &cs->resolvefh);
err:
        nfs3_fh_resolve_resume (cs);
        return 0;
}


int
nfs3_fh_resolve_root (nfs3_call_state_t *cs)
{
        int             ret = -EFAULT;
        nfs_user_t      nfu = {0, };

        if (!cs)
                return ret;

        if (nfs3_is_root_looked_up (cs->nfs3state, &cs->resolvefh)) {
                ret = nfs3_fh_resolve_resume (cs);
                goto out;
        }

        nfs_user_root_create (&nfu);
        gf_msg_trace (GF_NFS3, 0, "Root needs lookup");
        ret = nfs_root_loc_fill (cs->vol->itable, &cs->resolvedloc);
	if (ret < 0) {
		gf_msg (GF_NFS3, GF_LOG_ERROR, -ret, NFS_MSG_LOOKUP_ROOT_FAIL,
                        "Failed to lookup root from itable: %s",
                        strerror (-ret));
		goto out;
	}

        ret = nfs_lookup (cs->nfsx, cs->vol, &nfu, &cs->resolvedloc,
                          nfs3_fh_resolve_root_lookup_cbk, cs);

out:
        return ret;
}

/**
 * __nfs3_fh_auth_get_peer -- Get a peer name from the rpc request object
 *
 * @peer: Char * to write to
 * @req : The request to get host/peer from
 */
int
__nfs3_fh_auth_get_peer (const rpcsvc_request_t *req, char *peer)
{
        struct sockaddr_storage sastorage       = {0, };
        rpc_transport_t         *trans          = NULL;
        int                     ret             = 0;

        /* Why do we pass in the peer here and then
         * store it rather than malloc() and return a char * ? We want to avoid
         * heap allocations in the IO path as much as possible for speed
         * so we try to keep all allocations on the stack.
         */
        trans = rpcsvc_request_transport (req);
        ret = rpcsvc_transport_peeraddr (trans, peer, RPCSVC_PEER_STRLEN,
                                         &sastorage, sizeof (sastorage));
        if (ret != 0) {
                gf_msg (GF_NFS3, GF_LOG_WARNING, 0, NFS_MSG_GET_PEER_ADDR_FAIL,
                        "Failed to get peer addr: %s", gai_strerror (ret));
        }
        return ret;
}

/*
 * nfs3_fh_auth_nfsop () -- Checks if an nfsop is authorized.
 *
 * @cs: The NFS call state containing all the relevant information
 *
 * @return: 0 if authorized
 *          -EACCES for completely unauthorized fop
 *          -EROFS  for unauthorized write operations (rm, mkdir, write)
 */
int
nfs3_fh_auth_nfsop (nfs3_call_state_t *cs, gf_boolean_t is_write_op)
{
        struct nfs_state    *nfs = NULL;
        struct mount3_state *ms  = NULL;

        nfs = (struct nfs_state *)cs->nfsx->private;
        ms  = (struct mount3_state *)nfs->mstate;
        return  mnt3_authenticate_request (ms, cs->req, &cs->resolvefh, NULL,
                                           NULL, NULL, NULL, is_write_op);
}

int
nfs3_fh_resolve_and_resume (nfs3_call_state_t *cs, struct nfs3_fh *fh,
                            char *entry, nfs3_resume_fn_t resum_fn)
{
        int     ret = -EFAULT;

        if ((!cs) || (!fh))
                return ret;

        cs->resume_fn = resum_fn;
        cs->resolvefh = *fh;
        cs->hashidx = 0;

        /* Check if the resolution is:
         * a. fh resolution
         *
         * or
         *
         * b. (fh, basename) resolution
         */
        if (entry) {    /* b */
                cs->resolventry = gf_strdup (entry);
                if (!cs->resolventry)
                        goto err;
        }

        ret = nfs3_fh_resolve_root (cs);
err:
        return ret;
}
