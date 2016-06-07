/*
  Copyright (c) 2007-2012 Red Hat, Inc. <http://www.redhat.com>
  This file is part of GlusterFS.

  This file is licensed to you under your choice of the GNU Lesser
  General Public License, version 3 or any later version (LGPLv3 or
  later), or the GNU General Public License, version 2 (GPLv2), in all
  cases as published by the Free Software Foundation.
*/

#if defined(__GNUC__)
#if __GNUC__ >= 4
#if !defined(__clang__)
#if !defined(__NetBSD__)
#pragma GCC diagnostic ignored "-Wunused-but-set-variable"
#pragma GCC diagnostic ignored "-Wunused-variable"
#endif
#else
#pragma clang diagnostic ignored "-Wunused-variable"
#pragma clang diagnostic ignored "-Wunused-value"
#endif
#endif
#endif

#include "xdr-nfs3.h"
#include "mem-pool.h"
#include "xdr-common.h"

bool_t
xdr_uint64 (XDR *xdrs, uint64 *objp)
{
	 if (!xdr_uint64_t (xdrs, objp))
		 return FALSE;
	return TRUE;
}

bool_t
xdr_int64 (XDR *xdrs, int64 *objp)
{
	 if (!xdr_int64_t (xdrs, objp))
		 return FALSE;
	return TRUE;
}

bool_t
xdr_uint32 (XDR *xdrs, uint32 *objp)
{
	 if (!xdr_uint32_t (xdrs, objp))
		 return FALSE;
	return TRUE;
}

bool_t
xdr_int32 (XDR *xdrs, int32 *objp)
{
	 if (!xdr_int32_t (xdrs, objp))
		 return FALSE;
	return TRUE;
}

bool_t
xdr_filename3 (XDR *xdrs, filename3 *objp)
{
	 if (!xdr_string (xdrs, objp, ~0))
		 return FALSE;
	return TRUE;
}

bool_t
xdr_nfspath3 (XDR *xdrs, nfspath3 *objp)
{
	 if (!xdr_string (xdrs, objp, ~0))
		 return FALSE;
	return TRUE;
}

bool_t
xdr_fileid3 (XDR *xdrs, fileid3 *objp)
{
	 if (!xdr_uint64 (xdrs, objp))
		 return FALSE;
	return TRUE;
}

bool_t
xdr_cookie3 (XDR *xdrs, cookie3 *objp)
{
	 if (!xdr_uint64 (xdrs, objp))
		 return FALSE;
	return TRUE;
}

bool_t
xdr_cookieverf3 (XDR *xdrs, cookieverf3 objp)
{
	 if (!xdr_opaque (xdrs, objp, NFS3_COOKIEVERFSIZE))
		 return FALSE;
	return TRUE;
}

bool_t
xdr_createverf3 (XDR *xdrs, createverf3 objp)
{
	 if (!xdr_opaque (xdrs, objp, NFS3_CREATEVERFSIZE))
		 return FALSE;
	return TRUE;
}

bool_t
xdr_writeverf3 (XDR *xdrs, writeverf3 objp)
{
	 if (!xdr_opaque (xdrs, objp, NFS3_WRITEVERFSIZE))
		 return FALSE;
	return TRUE;
}

bool_t
xdr_uid3 (XDR *xdrs, uid3 *objp)
{
	 if (!xdr_uint32 (xdrs, objp))
		 return FALSE;
	return TRUE;
}

bool_t
xdr_gid3 (XDR *xdrs, gid3 *objp)
{
	 if (!xdr_uint32 (xdrs, objp))
		 return FALSE;
	return TRUE;
}

bool_t
xdr_size3 (XDR *xdrs, size3 *objp)
{
	 if (!xdr_uint64 (xdrs, objp))
		 return FALSE;
	return TRUE;
}

bool_t
xdr_offset3 (XDR *xdrs, offset3 *objp)
{
	 if (!xdr_uint64 (xdrs, objp))
		 return FALSE;
	return TRUE;
}

bool_t
xdr_mode3 (XDR *xdrs, mode3 *objp)
{
	 if (!xdr_uint32 (xdrs, objp))
		 return FALSE;
	return TRUE;
}

bool_t
xdr_count3 (XDR *xdrs, count3 *objp)
{
	 if (!xdr_uint32 (xdrs, objp))
		 return FALSE;
	return TRUE;
}

bool_t
xdr_nfsstat3 (XDR *xdrs, nfsstat3 *objp)
{
	 if (!xdr_enum (xdrs, (enum_t *) objp))
		 return FALSE;
	return TRUE;
}

bool_t
xdr_ftype3 (XDR *xdrs, ftype3 *objp)
{
	 if (!xdr_enum (xdrs, (enum_t *) objp))
		 return FALSE;
	return TRUE;
}

bool_t
xdr_specdata3 (XDR *xdrs, specdata3 *objp)
{
	 if (!xdr_uint32 (xdrs, &objp->specdata1))
		 return FALSE;
	 if (!xdr_uint32 (xdrs, &objp->specdata2))
		 return FALSE;
	return TRUE;
}

bool_t
xdr_nfs_fh3 (XDR *xdrs, nfs_fh3 *objp)
{
	 if (!xdr_bytes (xdrs, (char **)&objp->data.data_val, (u_int *) &objp->data.data_len, NFS3_FHSIZE))
		 return FALSE;
	return TRUE;
}

bool_t
xdr_nfstime3 (XDR *xdrs, nfstime3 *objp)
{
	 if (!xdr_uint32 (xdrs, &objp->seconds))
		 return FALSE;
	 if (!xdr_uint32 (xdrs, &objp->nseconds))
		 return FALSE;
	return TRUE;
}

bool_t
xdr_fattr3 (XDR *xdrs, fattr3 *objp)
{
	 if (!xdr_ftype3 (xdrs, &objp->type))
		 return FALSE;
	 if (!xdr_mode3 (xdrs, &objp->mode))
		 return FALSE;
	 if (!xdr_uint32 (xdrs, &objp->nlink))
		 return FALSE;
	 if (!xdr_uid3 (xdrs, &objp->uid))
		 return FALSE;
	 if (!xdr_gid3 (xdrs, &objp->gid))
		 return FALSE;
	 if (!xdr_size3 (xdrs, &objp->size))
		 return FALSE;
	 if (!xdr_size3 (xdrs, &objp->used))
		 return FALSE;
	 if (!xdr_specdata3 (xdrs, &objp->rdev))
		 return FALSE;
	 if (!xdr_uint64 (xdrs, &objp->fsid))
		 return FALSE;
	 if (!xdr_fileid3 (xdrs, &objp->fileid))
		 return FALSE;
	 if (!xdr_nfstime3 (xdrs, &objp->atime))
		 return FALSE;
	 if (!xdr_nfstime3 (xdrs, &objp->mtime))
		 return FALSE;
	 if (!xdr_nfstime3 (xdrs, &objp->ctime))
		 return FALSE;
	return TRUE;
}

bool_t
xdr_post_op_attr (XDR *xdrs, post_op_attr *objp)
{
	 if (!xdr_bool (xdrs, &objp->attributes_follow))
		 return FALSE;
	switch (objp->attributes_follow) {
	case TRUE:
		 if (!xdr_fattr3 (xdrs, &objp->post_op_attr_u.attributes))
			 return FALSE;
		break;
	case FALSE:
		break;
	default:
		return FALSE;
	}
	return TRUE;
}

bool_t
xdr_wcc_attr (XDR *xdrs, wcc_attr *objp)
{
	 if (!xdr_size3 (xdrs, &objp->size))
		 return FALSE;
	 if (!xdr_nfstime3 (xdrs, &objp->mtime))
		 return FALSE;
	 if (!xdr_nfstime3 (xdrs, &objp->ctime))
		 return FALSE;
	return TRUE;
}

bool_t
xdr_pre_op_attr (XDR *xdrs, pre_op_attr *objp)
{
	 if (!xdr_bool (xdrs, &objp->attributes_follow))
		 return FALSE;
	switch (objp->attributes_follow) {
	case TRUE:
		 if (!xdr_wcc_attr (xdrs, &objp->pre_op_attr_u.attributes))
			 return FALSE;
		break;
	case FALSE:
		break;
	default:
		return FALSE;
	}
	return TRUE;
}

bool_t
xdr_wcc_data (XDR *xdrs, wcc_data *objp)
{
	 if (!xdr_pre_op_attr (xdrs, &objp->before))
		 return FALSE;
	 if (!xdr_post_op_attr (xdrs, &objp->after))
		 return FALSE;
	return TRUE;
}

bool_t
xdr_post_op_fh3 (XDR *xdrs, post_op_fh3 *objp)
{
	 if (!xdr_bool (xdrs, &objp->handle_follows))
		 return FALSE;
	switch (objp->handle_follows) {
	case TRUE:
		 if (!xdr_nfs_fh3 (xdrs, &objp->post_op_fh3_u.handle))
			 return FALSE;
		break;
	case FALSE:
		break;
	default:
		return FALSE;
	}
	return TRUE;
}

bool_t
xdr_time_how (XDR *xdrs, time_how *objp)
{
	 if (!xdr_enum (xdrs, (enum_t *) objp))
		 return FALSE;
	return TRUE;
}

bool_t
xdr_set_mode3 (XDR *xdrs, set_mode3 *objp)
{
	 if (!xdr_bool (xdrs, &objp->set_it))
		 return FALSE;
	switch (objp->set_it) {
	case TRUE:
		 if (!xdr_mode3 (xdrs, &objp->set_mode3_u.mode))
			 return FALSE;
		break;
	default:
		break;
	}
	return TRUE;
}

bool_t
xdr_set_uid3 (XDR *xdrs, set_uid3 *objp)
{
	 if (!xdr_bool (xdrs, &objp->set_it))
		 return FALSE;
	switch (objp->set_it) {
	case TRUE:
		 if (!xdr_uid3 (xdrs, &objp->set_uid3_u.uid))
			 return FALSE;
		break;
	default:
		break;
	}
	return TRUE;
}

bool_t
xdr_set_gid3 (XDR *xdrs, set_gid3 *objp)
{
	 if (!xdr_bool (xdrs, &objp->set_it))
		 return FALSE;
	switch (objp->set_it) {
	case TRUE:
		 if (!xdr_gid3 (xdrs, &objp->set_gid3_u.gid))
			 return FALSE;
		break;
	default:
		break;
	}
	return TRUE;
}

bool_t
xdr_set_size3 (XDR *xdrs, set_size3 *objp)
{
	 if (!xdr_bool (xdrs, &objp->set_it))
		 return FALSE;
	switch (objp->set_it) {
	case TRUE:
		 if (!xdr_size3 (xdrs, &objp->set_size3_u.size))
			 return FALSE;
		break;
	default:
		break;
	}
	return TRUE;
}

bool_t
xdr_set_atime (XDR *xdrs, set_atime *objp)
{
	 if (!xdr_time_how (xdrs, &objp->set_it))
		 return FALSE;
	switch (objp->set_it) {
	case SET_TO_CLIENT_TIME:
		 if (!xdr_nfstime3 (xdrs, &objp->set_atime_u.atime))
			 return FALSE;
		break;
	default:
		break;
	}
	return TRUE;
}

bool_t
xdr_set_mtime (XDR *xdrs, set_mtime *objp)
{
	 if (!xdr_time_how (xdrs, &objp->set_it))
		 return FALSE;
	switch (objp->set_it) {
	case SET_TO_CLIENT_TIME:
		 if (!xdr_nfstime3 (xdrs, &objp->set_mtime_u.mtime))
			 return FALSE;
		break;
	default:
		break;
	}
	return TRUE;
}

bool_t
xdr_sattr3 (XDR *xdrs, sattr3 *objp)
{
	 if (!xdr_set_mode3 (xdrs, &objp->mode))
		 return FALSE;
	 if (!xdr_set_uid3 (xdrs, &objp->uid))
		 return FALSE;
	 if (!xdr_set_gid3 (xdrs, &objp->gid))
		 return FALSE;
	 if (!xdr_set_size3 (xdrs, &objp->size))
		 return FALSE;
	 if (!xdr_set_atime (xdrs, &objp->atime))
		 return FALSE;
	 if (!xdr_set_mtime (xdrs, &objp->mtime))
		 return FALSE;
	return TRUE;
}

bool_t
xdr_diropargs3 (XDR *xdrs, diropargs3 *objp)
{
	 if (!xdr_nfs_fh3 (xdrs, &objp->dir))
		 return FALSE;
	 if (!xdr_filename3 (xdrs, &objp->name))
		 return FALSE;
	return TRUE;
}

bool_t
xdr_getattr3args (XDR *xdrs, getattr3args *objp)
{
	 if (!xdr_nfs_fh3 (xdrs, &objp->object))
		 return FALSE;
	return TRUE;
}

bool_t
xdr_getattr3resok (XDR *xdrs, getattr3resok *objp)
{
	 if (!xdr_fattr3 (xdrs, &objp->obj_attributes))
		 return FALSE;
	return TRUE;
}

bool_t
xdr_getattr3res (XDR *xdrs, getattr3res *objp)
{
	 if (!xdr_nfsstat3 (xdrs, &objp->status))
		 return FALSE;
	switch (objp->status) {
	case NFS3_OK:
		 if (!xdr_getattr3resok (xdrs, &objp->getattr3res_u.resok))
			 return FALSE;
		break;
	default:
		break;
	}
	return TRUE;
}

bool_t
xdr_sattrguard3 (XDR *xdrs, sattrguard3 *objp)
{
	 if (!xdr_bool (xdrs, &objp->check))
		 return FALSE;
	switch (objp->check) {
	case TRUE:
		 if (!xdr_nfstime3 (xdrs, &objp->sattrguard3_u.obj_ctime))
			 return FALSE;
		break;
	case FALSE:
		break;
	default:
		return FALSE;
	}
	return TRUE;
}

bool_t
xdr_setattr3args (XDR *xdrs, setattr3args *objp)
{
	 if (!xdr_nfs_fh3 (xdrs, &objp->object))
		 return FALSE;
	 if (!xdr_sattr3 (xdrs, &objp->new_attributes))
		 return FALSE;
	 if (!xdr_sattrguard3 (xdrs, &objp->guard))
		 return FALSE;
	return TRUE;
}

bool_t
xdr_setattr3resok (XDR *xdrs, setattr3resok *objp)
{
	 if (!xdr_wcc_data (xdrs, &objp->obj_wcc))
		 return FALSE;
	return TRUE;
}

bool_t
xdr_setattr3resfail (XDR *xdrs, setattr3resfail *objp)
{
	 if (!xdr_wcc_data (xdrs, &objp->obj_wcc))
		 return FALSE;
	return TRUE;
}

bool_t
xdr_setattr3res (XDR *xdrs, setattr3res *objp)
{
	 if (!xdr_nfsstat3 (xdrs, &objp->status))
		 return FALSE;
	switch (objp->status) {
	case NFS3_OK:
		 if (!xdr_setattr3resok (xdrs, &objp->setattr3res_u.resok))
			 return FALSE;
		break;
	default:
		 if (!xdr_setattr3resfail (xdrs, &objp->setattr3res_u.resfail))
			 return FALSE;
		break;
	}
	return TRUE;
}

bool_t
xdr_lookup3args (XDR *xdrs, lookup3args *objp)
{
	 if (!xdr_diropargs3 (xdrs, &objp->what))
		 return FALSE;
	return TRUE;
}

bool_t
xdr_lookup3resok (XDR *xdrs, lookup3resok *objp)
{
	 if (!xdr_nfs_fh3 (xdrs, &objp->object))
		 return FALSE;
	 if (!xdr_post_op_attr (xdrs, &objp->obj_attributes))
		 return FALSE;
	 if (!xdr_post_op_attr (xdrs, &objp->dir_attributes))
		 return FALSE;
	return TRUE;
}

bool_t
xdr_lookup3resfail (XDR *xdrs, lookup3resfail *objp)
{
	 if (!xdr_post_op_attr (xdrs, &objp->dir_attributes))
		 return FALSE;
	return TRUE;
}

bool_t
xdr_lookup3res (XDR *xdrs, lookup3res *objp)
{
	 if (!xdr_nfsstat3 (xdrs, &objp->status))
		 return FALSE;
	switch (objp->status) {
	case NFS3_OK:
		 if (!xdr_lookup3resok (xdrs, &objp->lookup3res_u.resok))
			 return FALSE;
		break;
	default:
		 if (!xdr_lookup3resfail (xdrs, &objp->lookup3res_u.resfail))
			 return FALSE;
		break;
	}
	return TRUE;
}

bool_t
xdr_access3args (XDR *xdrs, access3args *objp)
{
	 if (!xdr_nfs_fh3 (xdrs, &objp->object))
		 return FALSE;
	 if (!xdr_uint32 (xdrs, &objp->access))
		 return FALSE;
	return TRUE;
}

bool_t
xdr_access3resok (XDR *xdrs, access3resok *objp)
{
	 if (!xdr_post_op_attr (xdrs, &objp->obj_attributes))
		 return FALSE;
	 if (!xdr_uint32 (xdrs, &objp->access))
		 return FALSE;
	return TRUE;
}

bool_t
xdr_access3resfail (XDR *xdrs, access3resfail *objp)
{
	 if (!xdr_post_op_attr (xdrs, &objp->obj_attributes))
		 return FALSE;
	return TRUE;
}

bool_t
xdr_access3res (XDR *xdrs, access3res *objp)
{
	 if (!xdr_nfsstat3 (xdrs, &objp->status))
		 return FALSE;
	switch (objp->status) {
	case NFS3_OK:
		 if (!xdr_access3resok (xdrs, &objp->access3res_u.resok))
			 return FALSE;
		break;
	default:
		 if (!xdr_access3resfail (xdrs, &objp->access3res_u.resfail))
			 return FALSE;
		break;
	}
	return TRUE;
}

bool_t
xdr_readlink3args (XDR *xdrs, readlink3args *objp)
{
	 if (!xdr_nfs_fh3 (xdrs, &objp->symlink))
		 return FALSE;
	return TRUE;
}

bool_t
xdr_readlink3resok (XDR *xdrs, readlink3resok *objp)
{
	 if (!xdr_post_op_attr (xdrs, &objp->symlink_attributes))
		 return FALSE;
	 if (!xdr_nfspath3 (xdrs, &objp->data))
		 return FALSE;
	return TRUE;
}

bool_t
xdr_readlink3resfail (XDR *xdrs, readlink3resfail *objp)
{
	 if (!xdr_post_op_attr (xdrs, &objp->symlink_attributes))
		 return FALSE;
	return TRUE;
}

bool_t
xdr_readlink3res (XDR *xdrs, readlink3res *objp)
{
	 if (!xdr_nfsstat3 (xdrs, &objp->status))
		 return FALSE;
	switch (objp->status) {
	case NFS3_OK:
		 if (!xdr_readlink3resok (xdrs, &objp->readlink3res_u.resok))
			 return FALSE;
		break;
	default:
		 if (!xdr_readlink3resfail (xdrs, &objp->readlink3res_u.resfail))
			 return FALSE;
		break;
	}
	return TRUE;
}

bool_t
xdr_read3args (XDR *xdrs, read3args *objp)
{
	 if (!xdr_nfs_fh3 (xdrs, &objp->file))
		 return FALSE;
	 if (!xdr_offset3 (xdrs, &objp->offset))
		 return FALSE;
	 if (!xdr_count3 (xdrs, &objp->count))
		 return FALSE;
	return TRUE;
}

bool_t
xdr_read3resok_nocopy (XDR *xdrs, read3resok *objp)
{
	 if (!xdr_post_op_attr (xdrs, &objp->file_attributes))
		 return FALSE;
	 if (!xdr_count3 (xdrs, &objp->count))
		 return FALSE;
	 if (!xdr_bool (xdrs, &objp->eof))
		 return FALSE;
         if (!xdr_u_int (xdrs, (u_int *) &objp->data.data_len))
                 return FALSE;
	return TRUE;
}


bool_t
xdr_read3resok (XDR *xdrs, read3resok *objp)
{
	 if (!xdr_post_op_attr (xdrs, &objp->file_attributes))
		 return FALSE;
	 if (!xdr_count3 (xdrs, &objp->count))
		 return FALSE;
	 if (!xdr_bool (xdrs, &objp->eof))
		 return FALSE;
	 if (!xdr_bytes (xdrs, (char **)&objp->data.data_val, (u_int *) &objp->data.data_len, ~0))
		 return FALSE;
	return TRUE;
}

bool_t
xdr_read3resfail (XDR *xdrs, read3resfail *objp)
{
	 if (!xdr_post_op_attr (xdrs, &objp->file_attributes))
		 return FALSE;
	return TRUE;
}


bool_t
xdr_read3res_nocopy (XDR *xdrs, read3res *objp)
{
	 if (!xdr_nfsstat3 (xdrs, &objp->status))
		 return FALSE;
	switch (objp->status) {
	case NFS3_OK:
		 if (!xdr_read3resok_nocopy (xdrs, &objp->read3res_u.resok))
			 return FALSE;
		break;
	default:
		 if (!xdr_read3resfail (xdrs, &objp->read3res_u.resfail))
			 return FALSE;
		break;
	}
	return TRUE;
}


bool_t
xdr_read3res (XDR *xdrs, read3res *objp)
{
	 if (!xdr_nfsstat3 (xdrs, &objp->status))
		 return FALSE;
	switch (objp->status) {
	case NFS3_OK:
		 if (!xdr_read3resok (xdrs, &objp->read3res_u.resok))
			 return FALSE;
		break;
	default:
		 if (!xdr_read3resfail (xdrs, &objp->read3res_u.resfail))
			 return FALSE;
		break;
	}
	return TRUE;
}

bool_t
xdr_stable_how (XDR *xdrs, stable_how *objp)
{
	 if (!xdr_enum (xdrs, (enum_t *) objp))
		 return FALSE;
	return TRUE;
}

bool_t
xdr_write3args (XDR *xdrs, write3args *objp)
{
	 if (!xdr_nfs_fh3 (xdrs, &objp->file))
		 return FALSE;
	 if (!xdr_offset3 (xdrs, &objp->offset))
		 return FALSE;
	 if (!xdr_count3 (xdrs, &objp->count))
		 return FALSE;
	 if (!xdr_stable_how (xdrs, &objp->stable))
		 return FALSE;

         /* Added specifically to avoid copies from the xdr buffer into
          * the write3args structure, which will also require an already
          * allocated buffer. That is not optimal.
          */
         if (!xdr_u_int (xdrs, (u_int *) &objp->data.data_len))
                 return FALSE;

         /* The remaining bytes in the xdr buffer are the bytes that need to be
          * written. See how these bytes are extracted in the xdr_to_write3args
          * code path. Be careful, while using the write3args structure, since
          * only the data.data_len has been filled. The actual data is
          * extracted in xdr_to_write3args path.
          */

         /*	 if (!xdr_bytes (xdrs, (char **)&objp->data.data_val, (u_int *) &objp->data.data_len, ~0))
		 return FALSE;
                 */
	return TRUE;
}

bool_t
xdr_write3resok (XDR *xdrs, write3resok *objp)
{
	 if (!xdr_wcc_data (xdrs, &objp->file_wcc))
		 return FALSE;
	 if (!xdr_count3 (xdrs, &objp->count))
		 return FALSE;
	 if (!xdr_stable_how (xdrs, &objp->committed))
		 return FALSE;
	 if (!xdr_writeverf3 (xdrs, objp->verf))
		 return FALSE;
	return TRUE;
}

bool_t
xdr_write3resfail (XDR *xdrs, write3resfail *objp)
{
	 if (!xdr_wcc_data (xdrs, &objp->file_wcc))
		 return FALSE;
	return TRUE;
}

bool_t
xdr_write3res (XDR *xdrs, write3res *objp)
{
	 if (!xdr_nfsstat3 (xdrs, &objp->status))
		 return FALSE;
	switch (objp->status) {
	case NFS3_OK:
		 if (!xdr_write3resok (xdrs, &objp->write3res_u.resok))
			 return FALSE;
		break;
	default:
		 if (!xdr_write3resfail (xdrs, &objp->write3res_u.resfail))
			 return FALSE;
		break;
	}
	return TRUE;
}

bool_t
xdr_createmode3 (XDR *xdrs, createmode3 *objp)
{
	 if (!xdr_enum (xdrs, (enum_t *) objp))
		 return FALSE;
	return TRUE;
}

bool_t
xdr_createhow3 (XDR *xdrs, createhow3 *objp)
{
	 if (!xdr_createmode3 (xdrs, &objp->mode))
		 return FALSE;
	switch (objp->mode) {
	case UNCHECKED:
	case GUARDED:
		 if (!xdr_sattr3 (xdrs, &objp->createhow3_u.obj_attributes))
			 return FALSE;
		break;
	case EXCLUSIVE:
		 if (!xdr_createverf3 (xdrs, objp->createhow3_u.verf))
			 return FALSE;
		break;
	default:
		return FALSE;
	}
	return TRUE;
}

bool_t
xdr_create3args (XDR *xdrs, create3args *objp)
{
	 if (!xdr_diropargs3 (xdrs, &objp->where))
		 return FALSE;
	 if (!xdr_createhow3 (xdrs, &objp->how))
		 return FALSE;
	return TRUE;
}

bool_t
xdr_create3resok (XDR *xdrs, create3resok *objp)
{
	 if (!xdr_post_op_fh3 (xdrs, &objp->obj))
		 return FALSE;
	 if (!xdr_post_op_attr (xdrs, &objp->obj_attributes))
		 return FALSE;
	 if (!xdr_wcc_data (xdrs, &objp->dir_wcc))
		 return FALSE;
	return TRUE;
}

bool_t
xdr_create3resfail (XDR *xdrs, create3resfail *objp)
{
	 if (!xdr_wcc_data (xdrs, &objp->dir_wcc))
		 return FALSE;
	return TRUE;
}

bool_t
xdr_create3res (XDR *xdrs, create3res *objp)
{
	 if (!xdr_nfsstat3 (xdrs, &objp->status))
		 return FALSE;
	switch (objp->status) {
	case NFS3_OK:
		 if (!xdr_create3resok (xdrs, &objp->create3res_u.resok))
			 return FALSE;
		break;
	default:
		 if (!xdr_create3resfail (xdrs, &objp->create3res_u.resfail))
			 return FALSE;
		break;
	}
	return TRUE;
}

bool_t
xdr_mkdir3args (XDR *xdrs, mkdir3args *objp)
{
	 if (!xdr_diropargs3 (xdrs, &objp->where))
		 return FALSE;
	 if (!xdr_sattr3 (xdrs, &objp->attributes))
		 return FALSE;
	return TRUE;
}

bool_t
xdr_mkdir3resok (XDR *xdrs, mkdir3resok *objp)
{
	 if (!xdr_post_op_fh3 (xdrs, &objp->obj))
		 return FALSE;
	 if (!xdr_post_op_attr (xdrs, &objp->obj_attributes))
		 return FALSE;
	 if (!xdr_wcc_data (xdrs, &objp->dir_wcc))
		 return FALSE;
	return TRUE;
}

bool_t
xdr_mkdir3resfail (XDR *xdrs, mkdir3resfail *objp)
{
	 if (!xdr_wcc_data (xdrs, &objp->dir_wcc))
		 return FALSE;
	return TRUE;
}

bool_t
xdr_mkdir3res (XDR *xdrs, mkdir3res *objp)
{
	 if (!xdr_nfsstat3 (xdrs, &objp->status))
		 return FALSE;
	switch (objp->status) {
	case NFS3_OK:
		 if (!xdr_mkdir3resok (xdrs, &objp->mkdir3res_u.resok))
			 return FALSE;
		break;
	default:
		 if (!xdr_mkdir3resfail (xdrs, &objp->mkdir3res_u.resfail))
			 return FALSE;
		break;
	}
	return TRUE;
}

bool_t
xdr_symlinkdata3 (XDR *xdrs, symlinkdata3 *objp)
{
	 if (!xdr_sattr3 (xdrs, &objp->symlink_attributes))
		 return FALSE;
	 if (!xdr_nfspath3 (xdrs, &objp->symlink_data))
		 return FALSE;
	return TRUE;
}

bool_t
xdr_symlink3args (XDR *xdrs, symlink3args *objp)
{
	 if (!xdr_diropargs3 (xdrs, &objp->where))
		 return FALSE;
	 if (!xdr_symlinkdata3 (xdrs, &objp->symlink))
		 return FALSE;
	return TRUE;
}

bool_t
xdr_symlink3resok (XDR *xdrs, symlink3resok *objp)
{
	 if (!xdr_post_op_fh3 (xdrs, &objp->obj))
		 return FALSE;
	 if (!xdr_post_op_attr (xdrs, &objp->obj_attributes))
		 return FALSE;
	 if (!xdr_wcc_data (xdrs, &objp->dir_wcc))
		 return FALSE;
	return TRUE;
}

bool_t
xdr_symlink3resfail (XDR *xdrs, symlink3resfail *objp)
{
	 if (!xdr_wcc_data (xdrs, &objp->dir_wcc))
		 return FALSE;
	return TRUE;
}

bool_t
xdr_symlink3res (XDR *xdrs, symlink3res *objp)
{
	 if (!xdr_nfsstat3 (xdrs, &objp->status))
		 return FALSE;
	switch (objp->status) {
	case NFS3_OK:
		 if (!xdr_symlink3resok (xdrs, &objp->symlink3res_u.resok))
			 return FALSE;
		break;
	default:
		 if (!xdr_symlink3resfail (xdrs, &objp->symlink3res_u.resfail))
			 return FALSE;
		break;
	}
	return TRUE;
}

bool_t
xdr_devicedata3 (XDR *xdrs, devicedata3 *objp)
{
	 if (!xdr_sattr3 (xdrs, &objp->dev_attributes))
		 return FALSE;
	 if (!xdr_specdata3 (xdrs, &objp->spec))
		 return FALSE;
	return TRUE;
}

bool_t
xdr_mknoddata3 (XDR *xdrs, mknoddata3 *objp)
{
	 if (!xdr_ftype3 (xdrs, &objp->type))
		 return FALSE;
	switch (objp->type) {
	case NF3CHR:
	case NF3BLK:
		 if (!xdr_devicedata3 (xdrs, &objp->mknoddata3_u.device))
			 return FALSE;
		break;
	case NF3SOCK:
	case NF3FIFO:
		 if (!xdr_sattr3 (xdrs, &objp->mknoddata3_u.pipe_attributes))
			 return FALSE;
		break;
	default:
		break;
	}
	return TRUE;
}

bool_t
xdr_mknod3args (XDR *xdrs, mknod3args *objp)
{
	 if (!xdr_diropargs3 (xdrs, &objp->where))
		 return FALSE;
	 if (!xdr_mknoddata3 (xdrs, &objp->what))
		 return FALSE;
	return TRUE;
}

bool_t
xdr_mknod3resok (XDR *xdrs, mknod3resok *objp)
{
	 if (!xdr_post_op_fh3 (xdrs, &objp->obj))
		 return FALSE;
	 if (!xdr_post_op_attr (xdrs, &objp->obj_attributes))
		 return FALSE;
	 if (!xdr_wcc_data (xdrs, &objp->dir_wcc))
		 return FALSE;
	return TRUE;
}

bool_t
xdr_mknod3resfail (XDR *xdrs, mknod3resfail *objp)
{
	 if (!xdr_wcc_data (xdrs, &objp->dir_wcc))
		 return FALSE;
	return TRUE;
}

bool_t
xdr_mknod3res (XDR *xdrs, mknod3res *objp)
{
	 if (!xdr_nfsstat3 (xdrs, &objp->status))
		 return FALSE;
	switch (objp->status) {
	case NFS3_OK:
		 if (!xdr_mknod3resok (xdrs, &objp->mknod3res_u.resok))
			 return FALSE;
		break;
	default:
		 if (!xdr_mknod3resfail (xdrs, &objp->mknod3res_u.resfail))
			 return FALSE;
		break;
	}
	return TRUE;
}

bool_t
xdr_remove3args (XDR *xdrs, remove3args *objp)
{
	 if (!xdr_diropargs3 (xdrs, &objp->object))
		 return FALSE;
	return TRUE;
}

bool_t
xdr_remove3resok (XDR *xdrs, remove3resok *objp)
{
	 if (!xdr_wcc_data (xdrs, &objp->dir_wcc))
		 return FALSE;
	return TRUE;
}

bool_t
xdr_remove3resfail (XDR *xdrs, remove3resfail *objp)
{
	 if (!xdr_wcc_data (xdrs, &objp->dir_wcc))
		 return FALSE;
	return TRUE;
}

bool_t
xdr_remove3res (XDR *xdrs, remove3res *objp)
{
	 if (!xdr_nfsstat3 (xdrs, &objp->status))
		 return FALSE;
	switch (objp->status) {
	case NFS3_OK:
		 if (!xdr_remove3resok (xdrs, &objp->remove3res_u.resok))
			 return FALSE;
		break;
	default:
		 if (!xdr_remove3resfail (xdrs, &objp->remove3res_u.resfail))
			 return FALSE;
		break;
	}
	return TRUE;
}

bool_t
xdr_rmdir3args (XDR *xdrs, rmdir3args *objp)
{
	 if (!xdr_diropargs3 (xdrs, &objp->object))
		 return FALSE;
	return TRUE;
}

bool_t
xdr_rmdir3resok (XDR *xdrs, rmdir3resok *objp)
{
	 if (!xdr_wcc_data (xdrs, &objp->dir_wcc))
		 return FALSE;
	return TRUE;
}

bool_t
xdr_rmdir3resfail (XDR *xdrs, rmdir3resfail *objp)
{
	 if (!xdr_wcc_data (xdrs, &objp->dir_wcc))
		 return FALSE;
	return TRUE;
}

bool_t
xdr_rmdir3res (XDR *xdrs, rmdir3res *objp)
{
	 if (!xdr_nfsstat3 (xdrs, &objp->status))
		 return FALSE;
	switch (objp->status) {
	case NFS3_OK:
		 if (!xdr_rmdir3resok (xdrs, &objp->rmdir3res_u.resok))
			 return FALSE;
		break;
	default:
		 if (!xdr_rmdir3resfail (xdrs, &objp->rmdir3res_u.resfail))
			 return FALSE;
		break;
	}
	return TRUE;
}

bool_t
xdr_rename3args (XDR *xdrs, rename3args *objp)
{
	 if (!xdr_diropargs3 (xdrs, &objp->from))
		 return FALSE;
	 if (!xdr_diropargs3 (xdrs, &objp->to))
		 return FALSE;
	return TRUE;
}

bool_t
xdr_rename3resok (XDR *xdrs, rename3resok *objp)
{
	 if (!xdr_wcc_data (xdrs, &objp->fromdir_wcc))
		 return FALSE;
	 if (!xdr_wcc_data (xdrs, &objp->todir_wcc))
		 return FALSE;
	return TRUE;
}

bool_t
xdr_rename3resfail (XDR *xdrs, rename3resfail *objp)
{
	 if (!xdr_wcc_data (xdrs, &objp->fromdir_wcc))
		 return FALSE;
	 if (!xdr_wcc_data (xdrs, &objp->todir_wcc))
		 return FALSE;
	return TRUE;
}

bool_t
xdr_rename3res (XDR *xdrs, rename3res *objp)
{
	 if (!xdr_nfsstat3 (xdrs, &objp->status))
		 return FALSE;
	switch (objp->status) {
	case NFS3_OK:
		 if (!xdr_rename3resok (xdrs, &objp->rename3res_u.resok))
			 return FALSE;
		break;
	default:
		 if (!xdr_rename3resfail (xdrs, &objp->rename3res_u.resfail))
			 return FALSE;
		break;
	}
	return TRUE;
}

bool_t
xdr_link3args (XDR *xdrs, link3args *objp)
{
	 if (!xdr_nfs_fh3 (xdrs, &objp->file))
		 return FALSE;
	 if (!xdr_diropargs3 (xdrs, &objp->link))
		 return FALSE;
	return TRUE;
}

bool_t
xdr_link3resok (XDR *xdrs, link3resok *objp)
{
	 if (!xdr_post_op_attr (xdrs, &objp->file_attributes))
		 return FALSE;
	 if (!xdr_wcc_data (xdrs, &objp->linkdir_wcc))
		 return FALSE;
	return TRUE;
}

bool_t
xdr_link3resfail (XDR *xdrs, link3resfail *objp)
{
	 if (!xdr_post_op_attr (xdrs, &objp->file_attributes))
		 return FALSE;
	 if (!xdr_wcc_data (xdrs, &objp->linkdir_wcc))
		 return FALSE;
	return TRUE;
}

bool_t
xdr_link3res (XDR *xdrs, link3res *objp)
{
	 if (!xdr_nfsstat3 (xdrs, &objp->status))
		 return FALSE;
	switch (objp->status) {
	case NFS3_OK:
		 if (!xdr_link3resok (xdrs, &objp->link3res_u.resok))
			 return FALSE;
		break;
	default:
		 if (!xdr_link3resfail (xdrs, &objp->link3res_u.resfail))
			 return FALSE;
		break;
	}
	return TRUE;
}

bool_t
xdr_readdir3args (XDR *xdrs, readdir3args *objp)
{
	 if (!xdr_nfs_fh3 (xdrs, &objp->dir))
		 return FALSE;
	 if (!xdr_cookie3 (xdrs, &objp->cookie))
		 return FALSE;
	 if (!xdr_cookieverf3 (xdrs, objp->cookieverf))
		 return FALSE;
	 if (!xdr_count3 (xdrs, &objp->count))
		 return FALSE;
	return TRUE;
}

bool_t
xdr_entry3 (XDR *xdrs, entry3 *objp)
{
	 if (!xdr_fileid3 (xdrs, &objp->fileid))
		 return FALSE;
	 if (!xdr_filename3 (xdrs, &objp->name))
		 return FALSE;
	 if (!xdr_cookie3 (xdrs, &objp->cookie))
		 return FALSE;
	 if (!xdr_pointer (xdrs, (char **)&objp->nextentry, sizeof (entry3), (xdrproc_t) xdr_entry3))
		 return FALSE;
	return TRUE;
}

bool_t
xdr_dirlist3 (XDR *xdrs, dirlist3 *objp)
{
	 if (!xdr_pointer (xdrs, (char **)&objp->entries, sizeof (entry3), (xdrproc_t) xdr_entry3))
		 return FALSE;
	 if (!xdr_bool (xdrs, &objp->eof))
		 return FALSE;
	return TRUE;
}

bool_t
xdr_readdir3resok (XDR *xdrs, readdir3resok *objp)
{
	 if (!xdr_post_op_attr (xdrs, &objp->dir_attributes))
		 return FALSE;
	 if (!xdr_cookieverf3 (xdrs, objp->cookieverf))
		 return FALSE;
	 if (!xdr_dirlist3 (xdrs, &objp->reply))
		 return FALSE;
	return TRUE;
}

bool_t
xdr_readdir3resfail (XDR *xdrs, readdir3resfail *objp)
{
	 if (!xdr_post_op_attr (xdrs, &objp->dir_attributes))
		 return FALSE;
	return TRUE;
}

bool_t
xdr_readdir3res (XDR *xdrs, readdir3res *objp)
{
	 if (!xdr_nfsstat3 (xdrs, &objp->status))
		 return FALSE;
	switch (objp->status) {
	case NFS3_OK:
		 if (!xdr_readdir3resok (xdrs, &objp->readdir3res_u.resok))
			 return FALSE;
		break;
	default:
		 if (!xdr_readdir3resfail (xdrs, &objp->readdir3res_u.resfail))
			 return FALSE;
		break;
	}
	return TRUE;
}

bool_t
xdr_readdirp3args (XDR *xdrs, readdirp3args *objp)
{
	 if (!xdr_nfs_fh3 (xdrs, &objp->dir))
		 return FALSE;
	 if (!xdr_cookie3 (xdrs, &objp->cookie))
		 return FALSE;
	 if (!xdr_cookieverf3 (xdrs, objp->cookieverf))
		 return FALSE;
	 if (!xdr_count3 (xdrs, &objp->dircount))
		 return FALSE;
	 if (!xdr_count3 (xdrs, &objp->maxcount))
		 return FALSE;
	return TRUE;
}

bool_t
xdr_entryp3 (XDR *xdrs, entryp3 *objp)
{
	 if (!xdr_fileid3 (xdrs, &objp->fileid))
		 return FALSE;
	 if (!xdr_filename3 (xdrs, &objp->name))
		 return FALSE;
	 if (!xdr_cookie3 (xdrs, &objp->cookie))
		 return FALSE;
	 if (!xdr_post_op_attr (xdrs, &objp->name_attributes))
		 return FALSE;
	 if (!xdr_post_op_fh3 (xdrs, &objp->name_handle))
		 return FALSE;
	 if (!xdr_pointer (xdrs, (char **)&objp->nextentry, sizeof (entryp3), (xdrproc_t) xdr_entryp3))
		 return FALSE;
	return TRUE;
}

bool_t
xdr_dirlistp3 (XDR *xdrs, dirlistp3 *objp)
{
	 if (!xdr_pointer (xdrs, (char **)&objp->entries, sizeof (entryp3), (xdrproc_t) xdr_entryp3))
		 return FALSE;
	 if (!xdr_bool (xdrs, &objp->eof))
		 return FALSE;
	return TRUE;
}

bool_t
xdr_readdirp3resok (XDR *xdrs, readdirp3resok *objp)
{
	 if (!xdr_post_op_attr (xdrs, &objp->dir_attributes))
		 return FALSE;
	 if (!xdr_cookieverf3 (xdrs, objp->cookieverf))
		 return FALSE;
	 if (!xdr_dirlistp3 (xdrs, &objp->reply))
		 return FALSE;
	return TRUE;
}

bool_t
xdr_readdirp3resfail (XDR *xdrs, readdirp3resfail *objp)
{
	 if (!xdr_post_op_attr (xdrs, &objp->dir_attributes))
		 return FALSE;
	return TRUE;
}

bool_t
xdr_readdirp3res (XDR *xdrs, readdirp3res *objp)
{
	 if (!xdr_nfsstat3 (xdrs, &objp->status))
		 return FALSE;
	switch (objp->status) {
	case NFS3_OK:
		 if (!xdr_readdirp3resok (xdrs, &objp->readdirp3res_u.resok))
			 return FALSE;
		break;
	default:
		 if (!xdr_readdirp3resfail (xdrs, &objp->readdirp3res_u.resfail))
			 return FALSE;
		break;
	}
	return TRUE;
}

bool_t
xdr_fsstat3args (XDR *xdrs, fsstat3args *objp)
{
	 if (!xdr_nfs_fh3 (xdrs, &objp->fsroot))
		 return FALSE;
	return TRUE;
}

bool_t
xdr_fsstat3resok (XDR *xdrs, fsstat3resok *objp)
{
	 if (!xdr_post_op_attr (xdrs, &objp->obj_attributes))
		 return FALSE;
	 if (!xdr_size3 (xdrs, &objp->tbytes))
		 return FALSE;
	 if (!xdr_size3 (xdrs, &objp->fbytes))
		 return FALSE;
	 if (!xdr_size3 (xdrs, &objp->abytes))
		 return FALSE;
	 if (!xdr_size3 (xdrs, &objp->tfiles))
		 return FALSE;
	 if (!xdr_size3 (xdrs, &objp->ffiles))
		 return FALSE;
	 if (!xdr_size3 (xdrs, &objp->afiles))
		 return FALSE;
	 if (!xdr_uint32 (xdrs, &objp->invarsec))
		 return FALSE;
	return TRUE;
}

bool_t
xdr_fsstat3resfail (XDR *xdrs, fsstat3resfail *objp)
{
	 if (!xdr_post_op_attr (xdrs, &objp->obj_attributes))
		 return FALSE;
	return TRUE;
}

bool_t
xdr_fsstat3res (XDR *xdrs, fsstat3res *objp)
{
	 if (!xdr_nfsstat3 (xdrs, &objp->status))
		 return FALSE;
	switch (objp->status) {
	case NFS3_OK:
		 if (!xdr_fsstat3resok (xdrs, &objp->fsstat3res_u.resok))
			 return FALSE;
		break;
	default:
		 if (!xdr_fsstat3resfail (xdrs, &objp->fsstat3res_u.resfail))
			 return FALSE;
		break;
	}
	return TRUE;
}

bool_t
xdr_fsinfo3args (XDR *xdrs, fsinfo3args *objp)
{
	 if (!xdr_nfs_fh3 (xdrs, &objp->fsroot))
		 return FALSE;
	return TRUE;
}

bool_t
xdr_fsinfo3resok (XDR *xdrs, fsinfo3resok *objp)
{
	 if (!xdr_post_op_attr (xdrs, &objp->obj_attributes))
		 return FALSE;
	 if (!xdr_uint32 (xdrs, &objp->rtmax))
		 return FALSE;
	 if (!xdr_uint32 (xdrs, &objp->rtpref))
		 return FALSE;
	 if (!xdr_uint32 (xdrs, &objp->rtmult))
		 return FALSE;
	 if (!xdr_uint32 (xdrs, &objp->wtmax))
		 return FALSE;
	 if (!xdr_uint32 (xdrs, &objp->wtpref))
		 return FALSE;
	 if (!xdr_uint32 (xdrs, &objp->wtmult))
		 return FALSE;
	 if (!xdr_uint32 (xdrs, &objp->dtpref))
		 return FALSE;
	 if (!xdr_size3 (xdrs, &objp->maxfilesize))
		 return FALSE;
	 if (!xdr_nfstime3 (xdrs, &objp->time_delta))
		 return FALSE;
	 if (!xdr_uint32 (xdrs, &objp->properties))
		 return FALSE;
	return TRUE;
}

bool_t
xdr_fsinfo3resfail (XDR *xdrs, fsinfo3resfail *objp)
{
	 if (!xdr_post_op_attr (xdrs, &objp->obj_attributes))
		 return FALSE;
	return TRUE;
}

bool_t
xdr_fsinfo3res (XDR *xdrs, fsinfo3res *objp)
{
	 if (!xdr_nfsstat3 (xdrs, &objp->status))
		 return FALSE;
	switch (objp->status) {
	case NFS3_OK:
		 if (!xdr_fsinfo3resok (xdrs, &objp->fsinfo3res_u.resok))
			 return FALSE;
		break;
	default:
		 if (!xdr_fsinfo3resfail (xdrs, &objp->fsinfo3res_u.resfail))
			 return FALSE;
		break;
	}
	return TRUE;
}

bool_t
xdr_pathconf3args (XDR *xdrs, pathconf3args *objp)
{
	 if (!xdr_nfs_fh3 (xdrs, &objp->object))
		 return FALSE;
	return TRUE;
}

bool_t
xdr_pathconf3resok (XDR *xdrs, pathconf3resok *objp)
{
	register int32_t *buf;


	if (xdrs->x_op == XDR_ENCODE) {
		 if (!xdr_post_op_attr (xdrs, &objp->obj_attributes))
			 return FALSE;
		 if (!xdr_uint32 (xdrs, &objp->linkmax))
			 return FALSE;
		 if (!xdr_uint32 (xdrs, &objp->name_max))
			 return FALSE;
		buf = XDR_INLINE (xdrs, 4 * BYTES_PER_XDR_UNIT);
		if (buf == NULL) {
			 if (!xdr_bool (xdrs, &objp->no_trunc))
				 return FALSE;
			 if (!xdr_bool (xdrs, &objp->chown_restricted))
				 return FALSE;
			 if (!xdr_bool (xdrs, &objp->case_insensitive))
				 return FALSE;
			 if (!xdr_bool (xdrs, &objp->case_preserving))
				 return FALSE;
		} else {
			IXDR_PUT_BOOL(buf, objp->no_trunc);
			IXDR_PUT_BOOL(buf, objp->chown_restricted);
			IXDR_PUT_BOOL(buf, objp->case_insensitive);
			IXDR_PUT_BOOL(buf, objp->case_preserving);
		}
		return TRUE;
	} else if (xdrs->x_op == XDR_DECODE) {
		 if (!xdr_post_op_attr (xdrs, &objp->obj_attributes))
			 return FALSE;
		 if (!xdr_uint32 (xdrs, &objp->linkmax))
			 return FALSE;
		 if (!xdr_uint32 (xdrs, &objp->name_max))
			 return FALSE;
		buf = XDR_INLINE (xdrs, 4 * BYTES_PER_XDR_UNIT);
		if (buf == NULL) {
			 if (!xdr_bool (xdrs, &objp->no_trunc))
				 return FALSE;
			 if (!xdr_bool (xdrs, &objp->chown_restricted))
				 return FALSE;
			 if (!xdr_bool (xdrs, &objp->case_insensitive))
				 return FALSE;
			 if (!xdr_bool (xdrs, &objp->case_preserving))
				 return FALSE;
		} else {
			objp->no_trunc = IXDR_GET_BOOL(buf);
			objp->chown_restricted = IXDR_GET_BOOL(buf);
			objp->case_insensitive = IXDR_GET_BOOL(buf);
			objp->case_preserving = IXDR_GET_BOOL(buf);
		}
	 return TRUE;
	}

	 if (!xdr_post_op_attr (xdrs, &objp->obj_attributes))
		 return FALSE;
	 if (!xdr_uint32 (xdrs, &objp->linkmax))
		 return FALSE;
	 if (!xdr_uint32 (xdrs, &objp->name_max))
		 return FALSE;
	 if (!xdr_bool (xdrs, &objp->no_trunc))
		 return FALSE;
	 if (!xdr_bool (xdrs, &objp->chown_restricted))
		 return FALSE;
	 if (!xdr_bool (xdrs, &objp->case_insensitive))
		 return FALSE;
	 if (!xdr_bool (xdrs, &objp->case_preserving))
		 return FALSE;
	return TRUE;
}

bool_t
xdr_pathconf3resfail (XDR *xdrs, pathconf3resfail *objp)
{
	 if (!xdr_post_op_attr (xdrs, &objp->obj_attributes))
		 return FALSE;
	return TRUE;
}

bool_t
xdr_pathconf3res (XDR *xdrs, pathconf3res *objp)
{
	 if (!xdr_nfsstat3 (xdrs, &objp->status))
		 return FALSE;
	switch (objp->status) {
	case NFS3_OK:
		 if (!xdr_pathconf3resok (xdrs, &objp->pathconf3res_u.resok))
			 return FALSE;
		break;
	default:
		 if (!xdr_pathconf3resfail (xdrs, &objp->pathconf3res_u.resfail))
			 return FALSE;
		break;
	}
	return TRUE;
}

bool_t
xdr_commit3args (XDR *xdrs, commit3args *objp)
{
	 if (!xdr_nfs_fh3 (xdrs, &objp->file))
		 return FALSE;
	 if (!xdr_offset3 (xdrs, &objp->offset))
		 return FALSE;
	 if (!xdr_count3 (xdrs, &objp->count))
		 return FALSE;
	return TRUE;
}

bool_t
xdr_commit3resok (XDR *xdrs, commit3resok *objp)
{
	 if (!xdr_wcc_data (xdrs, &objp->file_wcc))
		 return FALSE;
	 if (!xdr_writeverf3 (xdrs, objp->verf))
		 return FALSE;
	return TRUE;
}

bool_t
xdr_commit3resfail (XDR *xdrs, commit3resfail *objp)
{
	 if (!xdr_wcc_data (xdrs, &objp->file_wcc))
		 return FALSE;
	return TRUE;
}

bool_t
xdr_commit3res (XDR *xdrs, commit3res *objp)
{
	 if (!xdr_nfsstat3 (xdrs, &objp->status))
		 return FALSE;
	switch (objp->status) {
	case NFS3_OK:
		 if (!xdr_commit3resok (xdrs, &objp->commit3res_u.resok))
			 return FALSE;
		break;
	default:
		 if (!xdr_commit3resfail (xdrs, &objp->commit3res_u.resfail))
			 return FALSE;
		break;
	}
	return TRUE;
}

bool_t
xdr_fhandle3 (XDR *xdrs, fhandle3 *objp)
{
	 if (!xdr_bytes (xdrs, (char **)&objp->fhandle3_val, (u_int *) &objp->fhandle3_len, FHSIZE3))
		 return FALSE;
	return TRUE;
}

bool_t
xdr_dirpath (XDR *xdrs, dirpath *objp)
{
	 if (!xdr_string (xdrs, objp, MNTPATHLEN))
		 return FALSE;
	return TRUE;
}

bool_t
xdr_name (XDR *xdrs, name *objp)
{
	 if (!xdr_string (xdrs, objp, MNTNAMLEN))
		 return FALSE;
	return TRUE;
}

bool_t
xdr_mountstat3 (XDR *xdrs, mountstat3 *objp)
{
	 if (!xdr_enum (xdrs, (enum_t *) objp))
		 return FALSE;
	return TRUE;
}

bool_t
xdr_mountres3_ok (XDR *xdrs, mountres3_ok *objp)
{
	 if (!xdr_fhandle3 (xdrs, &objp->fhandle))
		 return FALSE;
	 if (!xdr_array (xdrs, (char **)&objp->auth_flavors.auth_flavors_val, (u_int *) &objp->auth_flavors.auth_flavors_len, ~0,
		sizeof (int), (xdrproc_t) xdr_int))
		 return FALSE;
	return TRUE;
}

bool_t
xdr_mountres3 (XDR *xdrs, mountres3 *objp)
{
	 if (!xdr_mountstat3 (xdrs, &objp->fhs_status))
		 return FALSE;
	switch (objp->fhs_status) {
	case MNT3_OK:
		 if (!xdr_mountres3_ok (xdrs, &objp->mountres3_u.mountinfo))
			 return FALSE;
		break;
	default:
		break;
	}
	return TRUE;
}

bool_t
xdr_mountlist (XDR *xdrs, mountlist *objp)
{
	 if (!xdr_pointer (xdrs, (char **)objp, sizeof (struct mountbody), (xdrproc_t) xdr_mountbody))
		 return FALSE;
	return TRUE;
}

bool_t
xdr_mountbody (XDR *xdrs, mountbody *objp)
{
	 if (!xdr_name (xdrs, &objp->ml_hostname))
		 return FALSE;
	 if (!xdr_dirpath (xdrs, &objp->ml_directory))
		 return FALSE;
	 if (!xdr_mountlist (xdrs, &objp->ml_next))
		 return FALSE;
	return TRUE;
}

bool_t
xdr_groups (XDR *xdrs, groups *objp)
{
	 if (!xdr_pointer (xdrs, (char **)objp, sizeof (struct groupnode), (xdrproc_t) xdr_groupnode))
		 return FALSE;
	return TRUE;
}

bool_t
xdr_groupnode (XDR *xdrs, groupnode *objp)
{
	 if (!xdr_name (xdrs, &objp->gr_name))
		 return FALSE;
	 if (!xdr_groups (xdrs, &objp->gr_next))
		 return FALSE;
	return TRUE;
}

bool_t
xdr_exports (XDR *xdrs, exports *objp)
{
	 if (!xdr_pointer (xdrs, (char **)objp, sizeof (struct exportnode), (xdrproc_t) xdr_exportnode))
		 return FALSE;
	return TRUE;
}

bool_t
xdr_exportnode (XDR *xdrs, exportnode *objp)
{
	 if (!xdr_dirpath (xdrs, &objp->ex_dir))
		 return FALSE;
	 if (!xdr_groups (xdrs, &objp->ex_groups))
		 return FALSE;
	 if (!xdr_exports (xdrs, &objp->ex_next))
		 return FALSE;
	return TRUE;
}

static void
xdr_free_groupnode (struct groupnode *group)
{
        if (!group)
                return;

        if (group->gr_next)
                xdr_free_groupnode (group->gr_next);

        GF_FREE (group->gr_name);
        GF_FREE (group);
}

void
xdr_free_exports_list (struct exportnode *first)
{
        struct exportnode       *elist = NULL;

        if (!first)
                return;

        while (first) {
                elist = first->ex_next;
                GF_FREE (first->ex_dir);

                xdr_free_groupnode (first->ex_groups);

                GF_FREE (first);
                first = elist;
        }

}


void
xdr_free_mountlist (mountlist ml)
{
        struct mountbody        *next = NULL;

        if (!ml)
                return;

        while (ml) {
                GF_FREE (ml->ml_hostname);
                GF_FREE (ml->ml_directory);
                next = ml->ml_next;
                GF_FREE (ml);
                ml = next;
        }

        return;
}


/* Free statements are based on the way sunrpc xdr decoding
 * code performs memory allocations.
 */
void
xdr_free_write3args_nocopy (write3args *wa)
{
        if (!wa)
                return;

        FREE (wa->file.data.data_val);
}
