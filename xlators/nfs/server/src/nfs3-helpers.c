/*
  Copyright (c) 2010 Gluster, Inc. <http://www.gluster.com>
  This file is part of GlusterFS.

  GlusterFS is free software; you can redistribute it and/or modify
  it under the terms of the GNU Affero General Public License as published
  by the Free Software Foundation; either version 3 of the License,
  or (at your option) any later version.

  GlusterFS is distributed in the hope that it will be useful, but
  WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
  Affero General Public License for more details.

  You should have received a copy of the GNU Affero General Public License
  along with this program.  If not, see
  <http://www.gnu.org/licenses/>.
*/

#ifndef _CONFIG_H
#define _CONFIG_H
#include "config.h"
#endif

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
        { -1,                   "IO Error"                              },

};


uint64_t
nfs3_iatt_gfid_to_ino (struct iatt *buf)
{
        uuid_t          gfid = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1};
        uint64_t        ino = 0;

        if (!buf)
                return 0;

        if ((buf->ia_ino != 1) && (uuid_compare (buf->ia_gfid, gfid) != 0)) {
                if (gf_nfs_enable_ino32()) {
                        ino = (uint32_t )nfs_hash_gfid (buf->ia_gfid);
                        goto hashout;
                }

                memcpy (&ino, &buf->ia_gfid[8], sizeof (uint64_t));
        } else
                ino = 1;

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

        default:
                stat = NFS3ERR_SERVERFAULT;
                break;
        }

        return stat;
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

fattr3
nfs3_stat_to_fattr3 (struct iatt *buf)
{
        fattr3          fa = {0, };

        if (IA_ISDIR (buf->ia_type))
                fa.type = NF3DIR;
        else if (IA_ISREG (buf->ia_type))
                fa.type = NF3REG;
        else if (IA_ISCHR (buf->ia_type))
                fa.type = NF3CHR;
        else if (IA_ISBLK (buf->ia_type))
                fa.type = NF3BLK;
        else if (IA_ISFIFO (buf->ia_type))
                fa.type = NF3FIFO;
        else if (IA_ISLNK (buf->ia_type))
                fa.type = NF3LNK;
        else if (IA_ISSOCK (buf->ia_type))
                fa.type = NF3SOCK;

        if (IA_PROT_RUSR (buf->ia_prot))
                fa.mode |= NFS3MODE_ROWNER;
        if (IA_PROT_WUSR (buf->ia_prot))
                fa.mode |= NFS3MODE_WOWNER;
        if (IA_PROT_XUSR (buf->ia_prot))
                fa.mode |= NFS3MODE_XOWNER;

        if (IA_PROT_RGRP (buf->ia_prot))
                fa.mode |= NFS3MODE_RGROUP;
        if (IA_PROT_WGRP (buf->ia_prot))
                fa.mode |= NFS3MODE_WGROUP;
        if (IA_PROT_XGRP (buf->ia_prot))
                fa.mode |= NFS3MODE_XGROUP;

        if (IA_PROT_ROTH (buf->ia_prot))
                fa.mode |= NFS3MODE_ROTHER;
        if (IA_PROT_WOTH (buf->ia_prot))
                fa.mode |= NFS3MODE_WOTHER;
        if (IA_PROT_XOTH (buf->ia_prot))
                fa.mode |= NFS3MODE_XOTHER;

        if (IA_PROT_SUID (buf->ia_prot))
                fa.mode |= NFS3MODE_SETXUID;
        if (IA_PROT_SGID (buf->ia_prot))
                fa.mode |= NFS3MODE_SETXGID;
        if (IA_PROT_STCKY (buf->ia_prot))
                fa.mode |= NFS3MODE_SAVESWAPTXT;

        fa.nlink = buf->ia_nlink;
        fa.uid = buf->ia_uid;
        fa.gid = buf->ia_gid;
        fa.size = buf->ia_size;
        fa.used = (buf->ia_blocks * 512);

        if ((IA_ISCHR (buf->ia_type) || IA_ISBLK (buf->ia_type))) {
                fa.rdev.specdata1 = ia_major (buf->ia_rdev);
                fa.rdev.specdata2 = ia_minor (buf->ia_rdev);
        } else {
                fa.rdev.specdata1 = 0;
                fa.rdev.specdata2 = 0;
        }

        fa.fsid = buf->ia_dev;
        fa.fileid = nfs3_iatt_gfid_to_ino (buf);
        /* FIXME: Handle time resolutions for sub-second granularity */
        if (buf->ia_atime == 9669) {
                fa.mtime.seconds = 0;
                fa.mtime.nseconds = 0;
                fa.atime.seconds = 0;
                fa.atime.nseconds = 0;
        } else {
                fa.mtime.seconds = buf->ia_mtime;
                fa.mtime.nseconds = 0;
                fa.atime.seconds = buf->ia_atime;
                fa.atime.seconds = 0;
                fa.atime.nseconds = 0;
        }

        fa.atime.seconds = buf->ia_atime;
        fa.atime.nseconds = 0;

        fa.ctime.seconds = buf->ia_ctime;
        fa.ctime.nseconds = 0;

        return fa;
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
        if (nfs_zero_filled_stat (buf))
                goto out;

        attr.post_op_attr_u.attributes = nfs3_stat_to_fattr3 (buf);
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
        if (nfs_zero_filled_stat (pre))
                goto out;

        poa.attributes_follow = TRUE;
        poa.pre_op_attr_u.attributes.size = pre->ia_size;
        if (pre->ia_atime == 9669)
                poa.pre_op_attr_u.attributes.mtime.seconds = 0;
        else
                poa.pre_op_attr_u.attributes.mtime.seconds = pre->ia_mtime;
        poa.pre_op_attr_u.attributes.ctime.seconds = pre->ia_ctime;

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
                fhlen = nfs3_fh_compute_size (fh);
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
        res->getattr3res_u.resok.obj_attributes = nfs3_stat_to_fattr3 (buf);

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
        resok.maxfilesize = GF_NFS3_MAXFILE;
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
        for(i = 0; nfs3stat_strerror_table[i].stat != -1; i++) {
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


uint32_t
nfs3_owner_accessbits (ia_prot_t prot, ia_type_t type, uint32_t request)
{
        uint32_t accresult = 0;

        if (IA_PROT_RUSR (prot) && (request & ACCESS3_READ))
                accresult |= ACCESS3_READ;

        if (request & ACCESS3_LOOKUP)
                if ((IA_ISDIR (type)) && (IA_PROT_XUSR (prot)))
                        accresult |= ACCESS3_LOOKUP;

        if ((IA_PROT_WUSR (prot) && (request & ACCESS3_MODIFY)))
                accresult |= ACCESS3_MODIFY;

        if ((IA_PROT_WUSR (prot) && (request & ACCESS3_EXTEND)))
                accresult |= ACCESS3_EXTEND;

        /* ACCESS3_DELETE is ignored for now since that requires
         * knowing the permissions on the parent directory.
         */

        if (request & ACCESS3_EXECUTE)
                if (IA_PROT_XUSR (prot) && (!IA_ISDIR (type)))
                        accresult |= ACCESS3_EXECUTE;

        return accresult;
}


uint32_t
nfs3_group_accessbits (ia_prot_t prot, ia_type_t type, uint32_t request)
{
        uint32_t accresult = 0;

        if (IA_PROT_RGRP (prot) && (request & ACCESS3_READ))
                accresult |= ACCESS3_READ;

        if (request & ACCESS3_LOOKUP)
                if ((IA_ISDIR (type)) && IA_PROT_RGRP (prot))
                        accresult |= ACCESS3_LOOKUP;

        if (IA_PROT_WGRP (prot) && (request & ACCESS3_MODIFY))
                accresult |= ACCESS3_MODIFY;

        if (IA_PROT_WGRP (prot) && (request & ACCESS3_EXTEND))
                accresult |= ACCESS3_EXTEND;

        /* ACCESS3_DELETE is ignored for now since that requires
         * knowing the permissions on the parent directory.
         */

        if (request & ACCESS3_EXECUTE)
                if (IA_PROT_XGRP (prot) && (!IA_ISDIR (type)))
                        accresult |= ACCESS3_EXECUTE;

        return accresult;
}


uint32_t
nfs3_other_accessbits (ia_prot_t prot, ia_type_t type, uint32_t request)
{
        uint32_t accresult = 0;

        if (IA_PROT_ROTH (prot) && (request & ACCESS3_READ))
                accresult |= ACCESS3_READ;

        if (request & ACCESS3_LOOKUP)
                if (IA_ISDIR (type) && IA_PROT_ROTH (prot))
                        accresult |= ACCESS3_LOOKUP;

        if (IA_PROT_WOTH (prot) && (request & ACCESS3_MODIFY))
                accresult |= ACCESS3_MODIFY;

        if (IA_PROT_WOTH (prot) && (request & ACCESS3_EXTEND))
                accresult |= ACCESS3_EXTEND;

        /* ACCESS3_DELETE is ignored for now since that requires
         * knowing the permissions on the parent directory.
         */

        if (request & ACCESS3_EXECUTE)
                if (IA_PROT_XOTH (prot) && (!IA_ISDIR (type)))
                        accresult |= ACCESS3_EXECUTE;

        return accresult;
}


uint32_t
nfs3_superuser_accessbits (ia_prot_t prot, ia_type_t type, uint32_t request)
{
        uint32_t accresult = 0;

        if (request & ACCESS3_READ)
                accresult |= ACCESS3_READ;

        if (request & ACCESS3_LOOKUP)
                if (IA_ISDIR (type))
                        accresult |= ACCESS3_LOOKUP;

        if (request & ACCESS3_MODIFY)
                accresult |= ACCESS3_MODIFY;

        if (request & ACCESS3_EXTEND)
                accresult |= ACCESS3_EXTEND;

        /* ACCESS3_DELETE is ignored for now since that requires
         * knowing the permissions on the parent directory.
         */

        if (request & ACCESS3_EXECUTE)
                if ((IA_PROT_XOTH (prot) || IA_PROT_XUSR (prot) ||
                     IA_PROT_XGRP (prot)) && (!IA_ISDIR (type)))
                        accresult |= ACCESS3_EXECUTE;

        return accresult;
}


uint32_t
nfs3_stat_to_accessbits (struct iatt *buf, uint32_t request, uid_t uid,
                         gid_t gid, gid_t *auxgids, int gids)
{
        uint32_t        accresult = 0;
        ia_prot_t       prot = {0, };
        ia_type_t       type = 0;
        int             testgid = -1;
        int             x = 0;

        prot = buf->ia_prot;
        type = buf->ia_type;

        if (buf->ia_gid == gid)
                testgid = gid;
        else {
                for (; x < gids; ++x) {
                        if (buf->ia_gid == auxgids[x]) {
                                testgid = buf->ia_gid;
                                break;
                        }
                }
        }

        if (uid == 0)
                accresult = nfs3_superuser_accessbits (prot, type, request);
        else if (buf->ia_uid == uid)
                accresult = nfs3_owner_accessbits (prot, type, request);
        else if ((testgid != -1) && (buf->ia_gid == testgid))
                accresult = nfs3_group_accessbits (prot, type, request);
        else
                accresult = nfs3_other_accessbits (prot, type, request);

        return accresult;
}


void
nfs3_fill_access3res (access3res *res, nfsstat3 status, struct iatt *buf,
                      uint32_t accbits, uid_t uid, gid_t gid,
                      uint64_t deviceid, gid_t *gidarr, int gids)
{
        post_op_attr    objattr;
        uint32_t        accres = 0;

        memset (res, 0, sizeof (*res));
        res->status = status;
        if (status != NFS3_OK)
                return;

        nfs3_map_deviceid_to_statdev (buf, deviceid);
        objattr = nfs3_stat_to_post_op_attr (buf);
        accres = nfs3_stat_to_accessbits (buf, accbits, uid, gid, gidarr, gids);

        res->access3res_u.resok.obj_attributes = objattr;
        res->access3res_u.resok.access = accres;
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

        gf_log (GF_NFS3, GF_LOG_TRACE, "Entry: %s", entry->d_name);

        /* If the entry is . or .., we need to replace the physical ino and gen
         * with 1 and 0 respectively if the directory is root. This funging is
         * needed because there is no parent directory of the root. In that
         * sense the behavious we provide is similar to the output of the
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
        fhlen = nfs3_fh_compute_size (fh);
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
         * sense the behavious we provide is similar to the output of the
         * command: "stat /.."
         */
        entry->d_ino = nfs3_iatt_gfid_to_ino (&entry->d_stat);
        nfs3_funge_root_dotdot_dirent (entry, dirfh);
        gf_log (GF_NFS3, GF_LOG_TRACE, "Entry: %s, ino: %"PRIu64,
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
        ent->name_attributes = nfs3_stat_to_post_op_attr (&entry->d_stat);
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

        gf_log (GF_NFS3, GF_LOG_TRACE, "Verifying cookie: cverf: %"PRIu64
                ", cookie: %"PRIu64, cverf, cookie);
        /* The cookie bad, no way cverf will be zero with a non-zero cookie. */
        if ((cverf == 0) && (cookie != 0)) {
                gf_log (GF_NFS3, GF_LOG_TRACE, "Bad cookie requested");
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
        gf_log (GF_NFS3, GF_LOG_TRACE, "Cookie verified");
        if (stat)
                *stat = NFS3_OK;
        ret = 0;
err:
        return ret;
}


/* When remove a file, we need to unref the cached fd for an inode but this
 * needs to happen only when the file was in fact opened. However, it is
 * possible that fd_lookup on a file returns an fd because the file was in
 * process of being created(which also returns an fd) but since this fd was not
 * opened through this path, in the NFS3 remove path, we'll end up removing the
 * reference that belongs to someone else. That means, nfs3 remove path should
 * not unref unless it is sure that the file was cached open also. If it was,
 * only then perform the fd_unref, else not.
 *
 * We determine that using a flag in the inode context.
 */
int
nfs3_set_inode_opened (xlator_t *nfsxl, inode_t *inode)
{
        if ((!nfsxl) || (!inode))
                return -1;

        inode_ctx_put (inode, nfsxl, GF_NFS3_FD_CACHED);

        return 0;
}


/* Returns 1 if inode was cached open, otherwise 0 */
int
nfs3_cached_inode_opened (xlator_t *nfsxl, inode_t *inode)
{
        int             ret = -1;
        uint64_t        cflag = 0;

        if ((!nfsxl) || (!inode))
                return -1;

        ret = inode_ctx_get (inode, nfsxl, &cflag);
        if (ret == -1)
                ret = 0;
        else if (cflag == GF_NFS3_FD_CACHED)
                ret = 1;

        return ret;
}


int32_t
nfs3_dir_open_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                   int32_t op_ret, int32_t op_errno, fd_t *fd)
{
        nfs3_call_state_t       *cs = NULL;

        cs = frame->local;
        if (op_ret == -1) {
                cs->resolve_ret = -1;
                cs->resolve_errno = op_errno;
                nfs3_call_resume (cs);
                goto err;
        }

        cs->fd = fd;     /* Gets unrefd when the call state is wiped. */
        nfs3_set_inode_opened (cs->nfsx, cs->resolvedloc.inode);
        gf_log (GF_NFS3, GF_LOG_TRACE, "FD_REF: %d", fd->refcount);
        nfs3_call_resume (cs);
err:
        return 0;
}


int
__nfs3_dir_open_and_resume (nfs3_call_state_t *cs)
{
        nfs_user_t      nfu = {0, };
        int             ret = -EFAULT;

        if (!cs)
                return ret;

        nfs_user_root_create (&nfu);
        ret = nfs_opendir (cs->nfsx, cs->vol, &nfu, &cs->resolvedloc,
                           nfs3_dir_open_cbk, cs);
        return ret;
}


int
nfs3_dir_open_and_resume (nfs3_call_state_t *cs, nfs3_resume_fn_t resume)
{
        fd_t    *fd = NULL;
        int     ret = -EFAULT;

        if ((!cs))
                return ret;

        cs->resume_fn = resume;
        gf_log (GF_NFS3, GF_LOG_TRACE, "Opening: %s", cs->resolvedloc.path);
        fd = fd_lookup (cs->resolvedloc.inode, 0);
        if (fd) {
                gf_log (GF_NFS3, GF_LOG_TRACE, "fd found in state: ref: %d", fd->refcount);
                cs->fd = fd;    /* Gets unrefd when the call state is wiped. */
                cs->resolve_ret = 0;
                nfs3_call_resume (cs);
                ret = 0;
                goto err;
        }

        ret = __nfs3_dir_open_and_resume (cs);

err:
        return ret;
}


int
nfs3_flush_call_state (nfs3_call_state_t *cs, fd_t *openedfd)
{
        if ((!cs) || (!openedfd))
                return -1;

        gf_log (GF_NFS3, GF_LOG_TRACE, "Calling resume");
        cs->resolve_ret = 0;
        gf_log (GF_NFS3, GF_LOG_TRACE, "Opening uncached fd done: %d",
                openedfd->refcount);
        cs->fd = fd_ref (openedfd);
        list_del (&cs->openwait_q);
        nfs3_call_resume (cs);

        return 0;
}


int
nfs3_flush_inode_queue (struct inode_op_queue *inode_q, fd_t *openedfd)
{
        nfs3_call_state_t       *cstmp = NULL;
        nfs3_call_state_t       *cs = NULL;

        if ((!openedfd) || (!inode_q))
                return -1;

        list_for_each_entry_safe (cs, cstmp, &inode_q->opq, openwait_q)
                nfs3_flush_call_state (cs, openedfd);

        return 0;
}


int
nfs3_flush_open_wait_call_states (nfs3_call_state_t *cs, fd_t *openedfd)
{
        struct inode_op_queue   *inode_q = NULL;
        uint64_t                ctxaddr = 0;
        int                     ret = 0;

        if (!cs)
                return -1;

        gf_log (GF_NFS3, GF_LOG_TRACE, "Flushing call state");
        ret = inode_ctx_get (cs->resolvedloc.inode, cs->nfsx, &ctxaddr);
        if (ret == -1) {
                gf_log (GF_NFS3, GF_LOG_TRACE, "No inode queue present");
                goto out;
        }

        inode_q = (struct inode_op_queue *)(long)ctxaddr;
        if (!inode_q)
                goto out;

        pthread_mutex_lock (&inode_q->qlock);
        {
                nfs3_flush_inode_queue (inode_q, openedfd);
        }
        pthread_mutex_unlock (&inode_q->qlock);

out:
        return 0;
}


int
__nfs3_fdcache_update_entry (struct nfs3_state *nfs3, fd_t *fd)
{
        uint64_t                ctxaddr = 0;
        struct nfs3_fd_entry    *fde = NULL;

        if ((!nfs3) || (!fd))
                return -1;

        gf_log (GF_NFS3, GF_LOG_TRACE, "Updating fd: 0x%lx", (long int)fd);
        fd_ctx_get (fd, nfs3->nfsx, &ctxaddr);
        fde = (struct nfs3_fd_entry *)(long)ctxaddr;
        if (fde) {
                list_del (&fde->list);
                list_add_tail (&fde->list, &nfs3->fdlru);
        }

        return 0;
}


int
nfs3_fdcache_update (struct nfs3_state *nfs3, fd_t *fd)
{
        if ((!nfs3) || (!fd))
                return -1;

        LOCK (&nfs3->fdlrulock);
        {
                __nfs3_fdcache_update_entry (nfs3, fd);
        }
        UNLOCK (&nfs3->fdlrulock);

        return 0;
}


int
__nfs3_fdcache_remove_entry (struct nfs3_state *nfs3, struct nfs3_fd_entry *fde)
{
        if ((!fde) || (!nfs3))
                return 0;

        gf_log (GF_NFS3, GF_LOG_TRACE, "Removing fd: 0x%lx: %d",
                (long int)fde->cachedfd, fde->cachedfd->refcount);
        list_del (&fde->list);
        fd_ctx_del (fde->cachedfd, nfs3->nfsx, NULL);
        fd_unref (fde->cachedfd);
        GF_FREE (fde);
        --nfs3->fdcount;

        return 0;
}


int
nfs3_fdcache_remove (struct nfs3_state *nfs3, fd_t *fd)
{
        struct nfs3_fd_entry    *fde = NULL;
        uint64_t                ctxaddr = 0;

        if ((!nfs3) || (!fd))
                return -1;

        LOCK (&nfs3->fdlrulock);
        {
                fd_ctx_get (fd, nfs3->nfsx, &ctxaddr);
                fde = (struct nfs3_fd_entry *)(long)ctxaddr;
                __nfs3_fdcache_remove_entry (nfs3, fde);
        }
        UNLOCK (&nfs3->fdlrulock);

        return 0;
}


int
__nfs3_fdcache_replace (struct nfs3_state *nfs3)
{
        struct nfs3_fd_entry    *fde = NULL;
        struct nfs3_fd_entry    *tmp = NULL;

        if (!nfs3)
                return -1;

        if (nfs3->fdcount <= GF_NFS3_FDCACHE_SIZE)
                return 0;

        list_for_each_entry_safe (fde, tmp, &nfs3->fdlru, list)
                break;

        __nfs3_fdcache_remove_entry (nfs3, fde);

        return 0;
}



int
nfs3_fdcache_add (struct nfs3_state *nfs3, fd_t *fd)
{
        struct nfs3_fd_entry    *fde = NULL;
        int                     ret = -1;

        if ((!nfs3) || (!fd))
                return -1;

        fde = GF_CALLOC (1, sizeof (*fd), gf_nfs_mt_nfs3_fd_entry);
        if (!fde) {
                gf_log (GF_NFS3, GF_LOG_ERROR, "fd entry allocation failed");
                goto out;
        }

        /* Already refd by caller. */
        fde->cachedfd = fd;
        INIT_LIST_HEAD (&fde->list);

        LOCK (&nfs3->fdlrulock);
        {
                gf_log (GF_NFS3, GF_LOG_TRACE, "Adding fd: 0x%lx",
                        (long int) fd);
                fd_ctx_set (fd, nfs3->nfsx, (uintptr_t)fde);
                fd_bind (fd);
                list_add_tail (&fde->list, &nfs3->fdlru);
                ++nfs3->fdcount;
                __nfs3_fdcache_replace (nfs3);
        }
        UNLOCK (&nfs3->fdlrulock);

out:
        return ret;
}


int32_t
nfs3_file_open_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                    int32_t op_ret, int32_t op_errno, fd_t *fd)
{
        nfs3_call_state_t       *cs = NULL;
        struct nfs3_state       *nfs3 = NULL;

        cs = frame->local;
        if (op_ret == -1) {
                gf_log (GF_NFS3, GF_LOG_TRACE, "Opening uncached fd failed");
                cs->resolve_ret = -1;
                cs->resolve_errno = op_errno;
                fd = NULL;
        } else {
                gf_log (GF_NFS3, GF_LOG_TRACE, "Opening uncached fd done: %d",
                        fd->refcount);
        }

        nfs3 = nfs_rpcsvc_request_program_private (cs->req);
        nfs3_flush_open_wait_call_states (cs, fd);
        nfs3_fdcache_add (nfs3, fd);
        return 0;
}


struct inode_op_queue *
__nfs3_get_inode_queue (nfs3_call_state_t *cs)
{
        struct inode_op_queue   *inode_q = NULL;
        int                     ret = -1;
        uint64_t                ctxaddr = 0;

        ret = __inode_ctx_get (cs->resolvedloc.inode, cs->nfsx, &ctxaddr);
        if (ret == 0) {
                inode_q = (struct inode_op_queue *)(long)ctxaddr;
                gf_log (GF_NFS3, GF_LOG_TRACE, "Inode queue already inited");
                goto err;
        }

        inode_q = GF_CALLOC (1, sizeof (*inode_q), gf_nfs_mt_inode_q);
        if (!inode_q) {
                gf_log (GF_NFS3, GF_LOG_ERROR, "Memory allocation failed");
                goto err;
        }

        gf_log (GF_NFS3, GF_LOG_TRACE, "Initing inode queue");
        INIT_LIST_HEAD (&inode_q->opq);
        pthread_mutex_init (&inode_q->qlock, NULL);
        __inode_ctx_put (cs->resolvedloc.inode, cs->nfsx, (uintptr_t)inode_q);

err:
        return inode_q;
}


struct inode_op_queue *
nfs3_get_inode_queue (nfs3_call_state_t *cs)
{
        struct inode_op_queue   *inode_q = NULL;

        LOCK (&cs->resolvedloc.inode->lock);
        {
                inode_q = __nfs3_get_inode_queue (cs);
        }
        UNLOCK (&cs->resolvedloc.inode->lock);

        return inode_q;
}


#define GF_NFS3_FD_OPEN_INPROGRESS      1
#define GF_NFS3_FD_NEEDS_OPEN           0


int
__nfs3_queue_call_state (struct inode_op_queue *inode_q, nfs3_call_state_t *cs)
{
        int     ret = -1;

        if (!inode_q)
                goto err;

        pthread_mutex_lock (&inode_q->qlock);
        {
                if (list_empty (&inode_q->opq)) {
                        gf_log (GF_NFS3, GF_LOG_TRACE, "First call in queue");
                        ret = GF_NFS3_FD_NEEDS_OPEN;
                } else
                        ret = GF_NFS3_FD_OPEN_INPROGRESS;

                gf_log (GF_NFS3, GF_LOG_TRACE, "Queueing call state");
                list_add_tail (&cs->openwait_q, &inode_q->opq);
        }
        pthread_mutex_unlock (&inode_q->qlock);

err:
        return ret;
}


/* Returns GF_NFS3_FD_NEEDS_OPEN if the current call is the first one to be
 * queued. If so, the caller will need to send the open fop. If this is a
 * non-first call to be queued, it means the fd opening is in progress and
 * GF_NFS3_FD_OPEN_INPROGRESS is returned.
 *
 * Returns -1 on error.
 */
int
nfs3_queue_call_state (nfs3_call_state_t *cs)
{
        struct inode_op_queue   *inode_q = NULL;
        int                     ret = -1;

        inode_q = nfs3_get_inode_queue (cs);
        if (!inode_q) {
                gf_log (GF_NFS3, GF_LOG_ERROR, "Failed to get inode op queue");
                goto err;
        }

        ret = __nfs3_queue_call_state (inode_q, cs);

err:
        return ret;
}


int
__nfs3_file_open_and_resume (nfs3_call_state_t *cs)
{
        nfs_user_t      nfu = {0, };
        int             ret = -EFAULT;

        if (!cs)
                return ret;

        ret = nfs3_queue_call_state (cs);
        if (ret == -1) {
                gf_log (GF_NFS3, GF_LOG_ERROR, "Error queueing call state");
                ret = -EFAULT;
                goto out;
        } else if (ret == GF_NFS3_FD_OPEN_INPROGRESS) {
                gf_log (GF_NFS3, GF_LOG_TRACE, "Open in progress. Will wait.");
                ret = 0;
                goto out;
        }

        nfs_user_root_create (&nfu);
        gf_log (GF_NFS3, GF_LOG_TRACE, "Opening uncached fd");
        ret = nfs_open (cs->nfsx, cs->vol, &nfu, &cs->resolvedloc, O_RDWR,
                        nfs3_file_open_cbk, cs);
out:
        return ret;
}


fd_t *
nfs3_fdcache_getfd (struct nfs3_state *nfs3, inode_t *inode)
{
        fd_t    *fd = NULL;

        if ((!nfs3) || (!inode))
                return NULL;

        fd = fd_lookup (inode, 0);
        if (fd) {
                /* Already refd by fd_lookup, so no need to ref again. */
                gf_log (GF_NFS3, GF_LOG_TRACE, "fd found in state: %d",
                        fd->refcount);
                nfs3_fdcache_update (nfs3, fd);
        } else
                gf_log (GF_NFS3, GF_LOG_TRACE, "fd not found in state");

        return fd;
}



int
nfs3_file_open_and_resume (nfs3_call_state_t *cs, nfs3_resume_fn_t resume)
{
        fd_t    *fd = NULL;
        int     ret = -EFAULT;

        if (!cs)
                return ret;

        cs->resume_fn = resume;
        gf_log (GF_NFS3, GF_LOG_TRACE, "Opening: %s", cs->resolvedloc.path);
        fd = nfs3_fdcache_getfd (cs->nfs3state, cs->resolvedloc.inode);
        if (fd) {
                cs->fd = fd;    /* Gets unrefd when the call state is wiped. */
                cs->resolve_ret = 0;
                nfs3_call_resume (cs);
                ret = 0;
                goto err;
        }

        ret = __nfs3_file_open_and_resume (cs);

err:
        return ret;
}


void
nfs3_stat_to_errstr (uint32_t xid, char *op, nfsstat3 stat, int pstat,
                     char *errstr)
{
        if ((!op) || (!errstr))
                return;

        sprintf (errstr, "XID: %x, %s: NFS: %d(%s), POSIX: %d(%s)", xid, op,
                 stat, nfsstat3_strerror (stat), pstat, strerror (pstat));
}

void
nfs3_log_common_call (uint32_t xid, char *op, struct nfs3_fh *fh)
{
        char    fhstr[1024];

        nfs3_fh_to_str (fh, fhstr);
        gf_log (GF_NFS3, GF_LOG_DEBUG, "XID: %x, %s: args: %s", xid, op,
                fhstr);
}


void
nfs3_log_fh_entry_call (uint32_t xid, char *op, struct nfs3_fh *fh,
                        char *name)
{
        char    fhstr[1024];

        nfs3_fh_to_str (fh, fhstr);
        gf_log (GF_NFS3, GF_LOG_DEBUG, "XID: %x, %s: args: %s, name: %s", xid,
                op, fhstr, name);
}


void
nfs3_log_rename_call (uint32_t xid, struct nfs3_fh *src, char *sname,
                      struct nfs3_fh *dst, char *dname)
{
        char    sfhstr[1024];
        char    dfhstr[1024];

        nfs3_fh_to_str (src, sfhstr);
        nfs3_fh_to_str (dst, dfhstr);
        gf_log (GF_NFS3, GF_LOG_DEBUG, "XID: %x, RENAME: args: Src: %s, "
                "name: %s, Dst: %s, name: %s", xid, sfhstr, sname, dfhstr,
                dname);
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

        nfs3_fh_to_str (fh, fhstr);
        if (mode == EXCLUSIVE)
                modestr = exclmode;
        else if (mode == GUARDED)
                modestr = guarded;
        else
                modestr = unchkd;

        gf_log (GF_NFS3, GF_LOG_DEBUG, "XID: %x, CREATE: args: %s, name: %s,"
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

        nfs3_fh_to_str (fh, fhstr);
        if (type == NF3CHR)
                modestr = chr;
        else if (type == NF3BLK)
                modestr = blk;
        else if (type == NF3SOCK)
                modestr = sock;
        else
                modestr = fifo;

        gf_log (GF_NFS3, GF_LOG_DEBUG, "XID: %x, MKNOD: args: %s, name: %s,"
                " type: %s", xid, fhstr, name, modestr);
}



void
nfs3_log_symlink_call (uint32_t xid, struct nfs3_fh *fh, char *name, char *tgt)
{
        char    fhstr[1024];

        nfs3_fh_to_str (fh, fhstr);
        gf_log (GF_NFS3, GF_LOG_DEBUG, "XID: %x, SYMLINK: args: %s, name: %s,"
                " target: %s", xid, fhstr, name, tgt);
}


void
nfs3_log_link_call (uint32_t xid, struct nfs3_fh *fh, char *name,
                    struct nfs3_fh *tgt)
{
        char    dfhstr[1024];
        char    tfhstr[1024];

        nfs3_fh_to_str (fh, dfhstr);
        nfs3_fh_to_str (tgt, tfhstr);
        gf_log (GF_NFS3, GF_LOG_DEBUG, "XID: %x, LINK: args: %s, name: %s,"
                " target: %s", xid, dfhstr, name, tfhstr);
}


void
nfs3_log_rw_call (uint32_t xid, char *op, struct nfs3_fh *fh, offset3 offt,
                  count3 count, int stablewrite)
{
        char    fhstr[1024];

        nfs3_fh_to_str (fh, fhstr);
        if (stablewrite == -1)
                gf_log (GF_NFS3, GF_LOG_DEBUG, "XID: %x, %s: args: %s, offset:"
                        " %"PRIu64",  count: %"PRIu32, xid, op, fhstr, offt,
                        count);
        else
                gf_log (GF_NFS3, GF_LOG_DEBUG, "XID: %x, %s: args: %s, offset:"
                        " %"PRIu64",  count: %"PRIu32", %s", xid, op, fhstr,
                        offt, count,
                        (stablewrite == UNSTABLE)?"UNSTABLE":"STABLE");

}


void
nfs3_log_common_res (uint32_t xid, char *op, nfsstat3 stat, int pstat)
{
        char    errstr[1024];

        nfs3_stat_to_errstr (xid, op, stat, pstat, errstr);
        gf_log (GF_NFS3, GF_LOG_DEBUG, "%s", errstr);
}

void
nfs3_log_readlink_res (uint32_t xid, nfsstat3 stat, int pstat, char *linkpath)
{
        char    errstr[1024];

        nfs3_stat_to_errstr (xid, "READLINK", stat, pstat, errstr);
        gf_log (GF_NFS3, GF_LOG_DEBUG, "%s, target: %s", errstr, linkpath);

}

void
nfs3_log_read_res (uint32_t xid, nfsstat3 stat, int pstat, count3 count,
                   int is_eof, struct iovec *vec, int32_t veccount)
{
        char    errstr[1024];

        nfs3_stat_to_errstr (xid, "READ", stat, pstat, errstr);
        if (vec)
                gf_log (GF_NFS3, GF_LOG_DEBUG, "%s, count: %"PRIu32", is_eof:"
                        " %d, vector: count: %d, len: %zd", errstr, count,
                        is_eof, veccount, vec->iov_len);
        else
                gf_log (GF_NFS3, GF_LOG_DEBUG, "%s, count: %"PRIu32", is_eof:"
                        " %d", errstr, count, is_eof);
}


void
nfs3_log_write_res (uint32_t xid, nfsstat3 stat, int pstat, count3 count,
                    int stable, uint64_t wverf)
{
        char    errstr[1024];

        nfs3_stat_to_errstr (xid, "WRITE", stat, pstat, errstr);
        gf_log (GF_NFS3, GF_LOG_DEBUG, "%s, count: %"PRIu32", %s,wverf: %"PRIu64
                , errstr, count, (stable == UNSTABLE)?"UNSTABLE":"STABLE",
                wverf);
}


void
nfs3_log_newfh_res (uint32_t xid, char *op, nfsstat3 stat, int pstat,
                    struct nfs3_fh *newfh)
{
        char    errstr[1024];
        char    fhstr[1024];

        nfs3_stat_to_errstr (xid, op, stat, pstat, errstr);
        nfs3_fh_to_str (newfh, fhstr);

        gf_log (GF_NFS3, GF_LOG_DEBUG, "%s, %s", errstr, fhstr);
}


void
nfs3_log_readdir_res (uint32_t xid, nfsstat3 stat, int pstat, uint64_t cverf,
                      count3 count, int is_eof)
{
        char    errstr[1024];

        nfs3_stat_to_errstr (xid, "READDIR", stat, pstat, errstr);
        gf_log (GF_NFS3, GF_LOG_DEBUG, "%s, count: %"PRIu32", cverf: %"PRIu64
                ", is_eof: %d", errstr, count, cverf, is_eof);
}


void
nfs3_log_readdirp_res (uint32_t xid, nfsstat3 stat, int pstat, uint64_t cverf,
                       count3 dircount, count3 maxcount, int is_eof)
{
        char    errstr[1024];

        nfs3_stat_to_errstr (xid, "READDIRPLUS", stat, pstat, errstr);
        gf_log (GF_NFS3, GF_LOG_DEBUG, "%s, dircount: %"PRIu32", maxcount: %"
                PRIu32", cverf: %"PRIu64", is_eof: %d", errstr, dircount,
                maxcount, cverf, is_eof);
}


void
nfs3_log_commit_res (uint32_t xid, nfsstat3 stat, int pstat, uint64_t wverf)
{
        char    errstr[1024];

        nfs3_stat_to_errstr (xid, "COMMIT", stat, pstat, errstr);
        gf_log (GF_NFS3, GF_LOG_DEBUG, "%s, wverf: %"PRIu64, errstr, wverf);
}


void
nfs3_log_readdir_call (uint32_t xid, struct nfs3_fh *fh, count3 dircount,
                       count3 maxcount)
{
        char    fhstr[1024];

        nfs3_fh_to_str (fh, fhstr);

        if (maxcount == 0)
                gf_log (GF_NFS3, GF_LOG_DEBUG, "XID: %x, READDIR: args: %s,"
                        " count: %d", xid, fhstr, (uint32_t)dircount);
        else
                gf_log (GF_NFS3, GF_LOG_DEBUG, "XID: %x, READDIRPLUS: args: %s,"
                        " dircount: %d, maxcount: %d", xid, fhstr,
                        (uint32_t)dircount, (uint32_t)maxcount);
}


int
nfs3_fh_resolve_inode_done (nfs3_call_state_t *cs, inode_t *inode)
{
        int             ret = -EFAULT;

        if ((!cs) || (!inode))
                return ret;

        gf_log (GF_NFS3, GF_LOG_TRACE, "FH inode resolved");
        ret = nfs_inode_loc_fill (inode, &cs->resolvedloc);
        if (ret < 0)
                goto err;

        nfs3_call_resume (cs);

err:
        return ret;
}

#define GF_NFS3_FHRESOLVE_FOUND         1
#define GF_NFS3_FHRESOLVE_NOTFOUND      2
#define GF_NFS3_FHRESOLVE_DIRFOUND      3

int
nfs3_fh_resolve_check_entry (struct nfs3_fh *fh, gf_dirent_t *candidate,
                             int hashidx)
{
        struct iatt             *ia = NULL;
        int                     ret = GF_NFS3_FHRESOLVE_NOTFOUND;
        nfs3_hash_entry_t       entryhash = 0;

        if ((!fh) || (!candidate))
                return ret;

        if ((strcmp (candidate->d_name, ".") == 0) ||
            (strcmp (candidate->d_name, "..") == 0))
                goto found_entry;

        ia = &candidate->d_stat;
        if ((uuid_compare (candidate->d_stat.ia_gfid, fh->gfid)) == 0) {
                gf_log (GF_NFS3, GF_LOG_TRACE, "Found entry: gfid: %s, "
                        "name: %s, hashcount %d",
                         uuid_utoa (candidate->d_stat.ia_gfid),
                         candidate->d_name, hashidx);
                ret = GF_NFS3_FHRESOLVE_FOUND;
                goto found_entry;
        }

        /* This condition ensures that we never have to be afraid of having
         * a directory hash conflict with a file hash. The consequence of
         * this condition is that we can now have unlimited files in a directory
         * and upto 65536 sub-directories in a directory.
         */
        if (!IA_ISDIR (candidate->d_stat.ia_type))
                goto found_entry;
        entryhash = fh->entryhash[hashidx];
        if (entryhash == nfs3_fh_hash_entry (ia->ia_gfid)) {
                gf_log (GF_NFS3, GF_LOG_TRACE, "Found hash match: %s: %d, "
                        "hashidx: %d", candidate->d_name, entryhash, hashidx);
                ret = GF_NFS3_FHRESOLVE_DIRFOUND;
                goto found_entry;
        }

found_entry:

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
                gf_log (GF_NFS3, GF_LOG_TRACE, "Lookup failed: %s: %s",
                        cs->resolvedloc.path, strerror (op_errno));
                goto err;
        } else
                gf_log (GF_NFS3, GF_LOG_TRACE, "Entry looked up: %s",
                        cs->resolvedloc.path);

        linked_inode = inode_link (inode, cs->resolvedloc.parent,
                                   cs->resolvedloc.name, buf);
        if (linked_inode) {
                inode_lookup (linked_inode);
                inode_unref (linked_inode);
        }
err:
        nfs3_call_resume (cs);
        return 0;
}



int32_t
nfs3_fh_resolve_readdir_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                             int32_t op_ret, int32_t op_errno,
                             gf_dirent_t *entries);

int
nfs3_fh_resolve_found_entry (nfs3_call_state_t *cs, gf_dirent_t *candidate)
{
        int             ret = 0;
        nfs_user_t      nfu = {0, };
        uuid_t          gfid = {0, };

        if ((!cs) || (!candidate))
                return -EFAULT;

        uuid_copy (gfid, cs->resolvedloc.inode->gfid);
        nfs_loc_wipe (&cs->resolvedloc);
        ret = nfs_entry_loc_fill (cs->vol->itable, gfid, candidate->d_name,
                                  &cs->resolvedloc, NFS_RESOLVE_CREATE);
        if (ret == -ENOENT) {
                gf_log (GF_NFS3, GF_LOG_TRACE, "Entry not in itable, needs"
                        " lookup");
                nfs_user_root_create (&nfu);
                ret = nfs_lookup (cs->nfsx, cs->vol, &nfu, &cs->resolvedloc,
                                  nfs3_fh_resolve_entry_lookup_cbk,
                                  cs);
        } else {
                gf_log (GF_NFS3, GF_LOG_TRACE, "Entry got from itable");
                nfs3_call_resume (cs);
        }

        return ret;
}


int32_t
nfs3_fh_resolve_parent_lookup_cbk (call_frame_t *frame, void *cookie,
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
                gf_log (GF_NFS3, GF_LOG_TRACE, "Lookup failed: %s: %s",
                        cs->resolvedloc.path, strerror (op_errno));
                nfs3_call_resume (cs);
                goto err;
        } else
                gf_log (GF_NFS3, GF_LOG_TRACE, "Entry looked up: %s",
                        cs->resolvedloc.path);

        linked_inode = inode_link (inode, cs->resolvedloc.parent,
                                   cs->resolvedloc.name, buf);
        if (linked_inode) {
                inode_lookup (linked_inode);
                inode_unref (linked_inode);
        }
        nfs3_fh_resolve_entry_hard (cs);

err:
        return 0;
}


int
nfs3_fh_resolve_found_parent (nfs3_call_state_t *cs, gf_dirent_t *candidate)
{
        int             ret = 0;
        nfs_user_t      nfu = {0, };
        uuid_t          gfid = {0, };

        if ((!cs) || (!candidate))
                return -EFAULT;

        uuid_copy (gfid, cs->resolvedloc.inode->gfid);
        nfs_loc_wipe (&cs->resolvedloc);
        ret = nfs_entry_loc_fill (cs->vol->itable, gfid, candidate->d_name,
                                  &cs->resolvedloc, NFS_RESOLVE_CREATE);
        if (ret == -ENOENT) {
                nfs_user_root_create (&nfu);
                ret = nfs_lookup (cs->nfsx, cs->vol, &nfu, &cs->resolvedloc,
                                  nfs3_fh_resolve_parent_lookup_cbk,
                                  cs);
        } else
                nfs3_fh_resolve_entry_hard (cs);

        return ret;
}


int
nfs3_fh_resolve_found (nfs3_call_state_t *cs, gf_dirent_t *candidate)
{
        int             ret = 0;

        if ((!cs) || (!candidate))
                return -EFAULT;

        if (!cs->resolventry) {
                gf_log (GF_NFS3, GF_LOG_TRACE, "Candidate entry was found");
                ret = nfs3_fh_resolve_found_entry (cs, candidate);
        } else {
                gf_log (GF_NFS3, GF_LOG_TRACE, "Entry's parent was found");
                ret = nfs3_fh_resolve_found_parent (cs, candidate);
        }

        return ret;
}


int32_t
nfs3_fh_resolve_opendir_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                             int32_t op_ret, int32_t op_errno, fd_t *fd)
{
        nfs3_call_state_t       *cs = NULL;
        int                     ret = -EFAULT;
        nfs_user_t              nfu = {0, };

        cs = frame->local;
        cs->resolve_ret = op_ret;
        cs->resolve_errno = op_errno;

        if (op_ret == -1) {
                gf_log (GF_NFS3, GF_LOG_TRACE, "Dir open failed: %s: %s",
                        cs->resolvedloc.path, strerror (op_errno));
                nfs3_call_resume (cs);
                goto err;
        } else
                gf_log (GF_NFS3, GF_LOG_TRACE, "Reading directory: %s",
                        cs->resolvedloc.path);

        nfs_user_root_create (&nfu);
        /* Keep this directory fd_t around till we have either:
         * a. found the entry,
         * b. exhausted all the entries,
         * c. decide to step into a child directory.
         *
         * This decision is made in nfs3_fh_resolve_check_response.
         */
        cs->resolve_dir_fd = fd;
        gf_log (GF_NFS3, GF_LOG_TRACE, "resolve new fd refed: 0x%lx, ref: %d",
                (long)cs->resolve_dir_fd, cs->resolve_dir_fd->refcount);
        ret = nfs_readdirp (cs->nfsx, cs->vol, &nfu, fd, GF_NFS3_DTPREF, 0,
                            nfs3_fh_resolve_readdir_cbk, cs);

err:
        return ret;
}

int32_t
nfs3_fh_resolve_dir_lookup_cbk (call_frame_t *frame, void *cookie,
                                xlator_t *this, int32_t op_ret,int32_t op_errno,
                                inode_t *inode, struct iatt *buf, dict_t *xattr,
                                struct iatt *postparent)
{
        nfs3_call_state_t       *cs = NULL;
        nfs_user_t              nfu = {0, };
        inode_t                 *linked_inode = NULL;

        cs = frame->local;
        cs->resolve_ret = op_ret;
        cs->resolve_errno = op_errno;

        if (op_ret == -1) {
                gf_log (GF_NFS3, GF_LOG_TRACE, "Lookup failed: %s: %s",
                        cs->resolvedloc.path, strerror (op_errno));
                nfs3_call_resume (cs);
                goto err;
        } else
                gf_log (GF_NFS3, GF_LOG_TRACE, "Dir will be opened: %s",
                        cs->resolvedloc.path);

        nfs_user_root_create (&nfu);
        linked_inode = inode_link (inode, cs->resolvedloc.parent,
                                   cs->resolvedloc.name, buf);
        if (linked_inode) {
                inode_lookup (linked_inode);
                inode_unref (linked_inode);
        }

        nfs_opendir (cs->nfsx, cs->vol, &nfu, &cs->resolvedloc,
                     nfs3_fh_resolve_opendir_cbk, cs);

err:
        return 0;
}


/* Validate the depth of the dir such that we do not end up opening and
 * reading directories beyond those that are needed for resolving the file
 * handle.
 * Returns 1 if fh resolution can continue, 0 otherwise.
 */
int
nfs3_fh_resolve_validate_dirdepth (nfs3_call_state_t *cs)
{
        int     ret = 1;

        if (!cs)
                return 0;

        /* This condition will generally never be hit because the
         * hash-matching scheme will prevent us from going into a
         * directory that is not part of the hash-array.
         */
        if (nfs3_fh_hash_index_is_beyond (&cs->resolvefh, cs->hashidx)) {
                gf_log (GF_NFS3, GF_LOG_TRACE, "Hash index is beyond: idx %d,"
                        " fh idx: %d", cs->hashidx, cs->resolvefh.hashcount);
                ret = 0;
                goto out;
        }

        if (cs->hashidx >= GF_NFSFH_MAXHASHES) {
                gf_log (GF_NFS3, GF_LOG_TRACE, "Hash index beyond max hashes:"
                        " hashidx %d, max: %d", cs->hashidx,
                        GF_NFSFH_MAXHASHES);
                ret = 0;
                goto out;
        }

out:
        return ret;
}


int
nfs3_fh_resolve_dir_hard (nfs3_call_state_t *cs, uuid_t dirgfid, char *entry)
{
        int             ret = -EFAULT;
        nfs_user_t      nfu = {0, };

        if (!cs)
                return ret;

        cs->hashidx++;
        nfs_loc_wipe (&cs->resolvedloc);
        if (!nfs3_fh_resolve_validate_dirdepth (cs)) {
                gf_log (GF_NFS3, GF_LOG_TRACE, "Dir depth validation failed");
                nfs3_call_resume_estale (cs);
                ret = 0;
                goto out;
        }

        nfs_user_root_create (&nfu);
        gf_log (GF_NFS3, GF_LOG_TRACE, "FH hard dir resolution: gfid: %s, "
                "entry: %s, next hashcount: %d", uuid_utoa (dirgfid), entry,
                cs->hashidx);
        ret = nfs_entry_loc_fill (cs->vol->itable, dirgfid, entry,
                                  &cs->resolvedloc, NFS_RESOLVE_CREATE);

        if (ret == 0) {
                gf_log (GF_NFS3, GF_LOG_TRACE, "Dir will be opened: %s",
                        cs->resolvedloc.path);
                ret = nfs_opendir (cs->nfsx, cs->vol, &nfu, &cs->resolvedloc,
                                   nfs3_fh_resolve_opendir_cbk, cs);
        } else if (ret == -ENOENT) {
                gf_log (GF_NFS3, GF_LOG_TRACE, "Dir needs lookup: %s",
                        cs->resolvedloc.path);
                ret = nfs_lookup (cs->nfsx, cs->vol, &nfu, &cs->resolvedloc,
                                  nfs3_fh_resolve_dir_lookup_cbk, cs);
        }
out:
        return ret;
}


/*
 * Called in a recursive code path, so if another
 * directory was opened in an earlier call during fh resolution, we must unref
 * through this reference before opening another fd_t.
 */
#define nfs3_fh_resolve_close_cwd(cst)                                  \
        do {                                                            \
                if ((cst)->resolve_dir_fd) {                            \
                        gf_log (GF_NFS3, GF_LOG_TRACE, "resolve fd "    \
                                "unrefing: 0x%lx, ref: %d",             \
                                (long)(cst)->resolve_dir_fd,            \
                                (cst)->resolve_dir_fd->refcount);       \
                                fd_unref ((cst)->resolve_dir_fd);       \
                }                                                       \
        } while (0)                                                     \


int
nfs3_fh_resolve_check_response (nfs3_call_state_t *cs, gf_dirent_t *candidate,
                                int response, off_t last_offt)
{
        int             ret = -EFAULT;
        nfs_user_t      nfu = {0, };

        if (!cs)
                return ret;

        switch (response) {

        case GF_NFS3_FHRESOLVE_DIRFOUND:
                nfs3_fh_resolve_close_cwd (cs);
                nfs3_fh_resolve_dir_hard (cs, cs->resolvedloc.inode->gfid,
                                          candidate->d_name);
                break;

        case GF_NFS3_FHRESOLVE_FOUND:
                nfs3_fh_resolve_close_cwd (cs);
                nfs3_fh_resolve_found (cs, candidate);
                break;

        case GF_NFS3_FHRESOLVE_NOTFOUND:
                nfs_user_root_create (&nfu);
                nfs_readdirp (cs->nfsx, cs->vol, &nfu, cs->resolve_dir_fd,
                              GF_NFS3_DTPREF, last_offt,
                              nfs3_fh_resolve_readdir_cbk, cs);
                break;
        }

        return 0;
}

int
nfs3_fh_resolve_search_dir (nfs3_call_state_t *cs, gf_dirent_t *entries)
{
        gf_dirent_t     *candidate = NULL;
        int             ret = GF_NFS3_FHRESOLVE_NOTFOUND;
        off_t           lastoff = 0;

        if ((!cs) || (!entries))
                return -EFAULT;

        if (list_empty (&entries->list))
                goto not_found;

        list_for_each_entry (candidate, &entries->list, list) {
                lastoff = candidate->d_off;
                gf_log (GF_NFS3, GF_LOG_TRACE, "Candidate: %s, gfid: %s",
                        candidate->d_name,
                        uuid_utoa (candidate->d_stat.ia_gfid));
                ret = nfs3_fh_resolve_check_entry (&cs->resolvefh, candidate,
                                                   cs->hashidx);
                if (ret != GF_NFS3_FHRESOLVE_NOTFOUND)
                        break;
        }

not_found:
        nfs3_fh_resolve_check_response (cs, candidate, ret, lastoff);
        return ret;
}


int32_t
nfs3_fh_resolve_readdir_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                             int32_t op_ret, int32_t op_errno,
                             gf_dirent_t *entries)
{
        nfs3_call_state_t       *cs = NULL;

        cs = frame->local;
        if (op_ret <= 0) {
                gf_log (GF_NFS3, GF_LOG_TRACE, "Directory read done: %s: %s",
                        cs->resolvedloc.path, strerror (op_ret));
                cs->resolve_ret = -1;
                cs->resolve_errno = ESTALE;
                nfs3_call_resume (cs);
                goto err;
        }

        nfs3_fh_resolve_search_dir (cs, entries);

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

        cs->hashidx++;
        nfs_loc_wipe (&cs->resolvedloc);
        if (!nfs3_fh_resolve_validate_dirdepth (cs)) {
                gf_log (GF_NFS3, GF_LOG_TRACE, "Dir depth validation failed");
                nfs3_call_resume_estale (cs);
                ret = 0;
                goto out;
        }

        nfs_user_root_create (&nfu);
        gf_log (GF_NFS3, GF_LOG_TRACE, "FH hard resolution for: gfid 0x%s"
                ", hashcount: %d, current hashidx %d",
                uuid_utoa (cs->resolvefh.gfid),
                cs->resolvefh.hashcount, cs->hashidx);
        ret = nfs_root_loc_fill (cs->vol->itable, &cs->resolvedloc);

        if (ret == 0) {
                gf_log (GF_NFS3, GF_LOG_TRACE, "Dir will be opened: %s",
                        cs->resolvedloc.path);
                ret = nfs_opendir (cs->nfsx, cs->vol, &nfu, &cs->resolvedloc,
                                   nfs3_fh_resolve_opendir_cbk, cs);
        } else if (ret == -ENOENT) {
                gf_log (GF_NFS3, GF_LOG_TRACE, "Dir needs lookup: %s",
                        cs->resolvedloc.path);
                ret = nfs_lookup (cs->nfsx, cs->vol, &nfu, &cs->resolvedloc,
                                  nfs3_fh_resolve_dir_lookup_cbk, cs);
        }

out:
        return ret;
}


int
nfs3_fh_resolve_entry_hard (nfs3_call_state_t *cs)
{
        int             ret = -EFAULT;
        nfs_user_t      nfu = {0, };

        if (!cs)
                return ret;

        nfs_loc_wipe (&cs->resolvedloc);
        nfs_user_root_create (&nfu);
        gf_log (GF_NFS3, GF_LOG_TRACE, "FH hard resolution: gfid: %s "
                ", entry: %s, hashidx: %d", uuid_utoa (cs->resolvefh.gfid),
                cs->resolventry, cs->hashidx);

        ret = nfs_entry_loc_fill (cs->vol->itable, cs->resolvefh.gfid,
                                  cs->resolventry, &cs->resolvedloc,
                                  NFS_RESOLVE_CREATE);

        if (ret == -2) {
                gf_log (GF_NFS3, GF_LOG_TRACE, "Entry needs lookup: %s",
                        cs->resolvedloc.path);
                if (nfs3_lookup_op (cs)) {
                        cs->lookuptype = GF_NFS3_FRESH;
                        cs->resolve_ret = 0;
                        nfs3_call_resume (cs);
                } else
                        nfs_lookup (cs->nfsx, cs->vol, &nfu, &cs->resolvedloc,
                                    nfs3_fh_resolve_entry_lookup_cbk, cs);
                ret = 0;
        } else if (ret == -1) {
                gf_log (GF_NFS3, GF_LOG_TRACE, "Entry needs parent lookup: %s",
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

        if (!cs)
                return ret;

        gf_log (GF_NFS3, GF_LOG_TRACE, "FH needs inode resolution");
        inode = inode_find (cs->vol->itable, cs->resolvefh.gfid);
        if (!inode)
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
                gf_log (GF_NFS3, GF_LOG_TRACE, "Root lookup failed: %s",
                        strerror (op_errno));
                goto err;
        } else
                gf_log (GF_NFS3, GF_LOG_TRACE, "Root looked up: %s",
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
        gf_log (GF_NFS3, GF_LOG_TRACE, "Root needs lookup");
        ret = nfs_root_loc_fill (cs->vol->itable, &cs->resolvedloc);

        ret = nfs_lookup (cs->nfsx, cs->vol, &nfu, &cs->resolvedloc,
                          nfs3_fh_resolve_root_lookup_cbk, cs);

out:
        return ret;
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
