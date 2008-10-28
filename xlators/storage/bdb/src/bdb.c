/*
   Copyright (c) 2008 Z RESEARCH, Inc. <http://www.zresearch.com>
   This file is part of GlusterFS.

   GlusterFS is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published
   by the Free Software Foundation; either version 3 of the License,
   or (at your option) any later version.

   GlusterFS is distributed in the hope that it will be useful, but
   WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program.  If not, see
   <http://www.gnu.org/licenses/>.
*/

/* bdb based storage translator - named as 'bdb' translator
 * 
 * 
 * There can be only two modes for files existing on bdb translator:
 * 1. DIRECTORY - directories are stored by bdb as regular directories on background 
 * file-system. directories also have an entry in the ns_db.db of their parent directory.
 * 2. REGULAR FILE - regular files are stored as records in the storage_db.db present in
 * the directory. regular files also have an entry in ns_db.db
 *
 * Internally bdb has a maximum of three different types of logical files associated with
 * each directory:
 * 1. storage_db.db - storage database, used to store the data corresponding to regular
 *                   files in the form of key/value pair. file-name is the 'key' and data
 *                   is 'value'.
 * 2. directory (all subdirectories) - any subdirectory will have a regular directory entry.
 */
#ifndef _CONFIG_H
#define _CONFIG_H
#include "config.h"
#endif

#define __XOPEN_SOURCE 500

#include <stdint.h>
#include <sys/time.h>
#include <errno.h>
#include <ftw.h>
#include <libgen.h>

#include "glusterfs.h"
#include "dict.h"
#include "logging.h"
#include "bdb.h"
#include "xlator.h"
#include "lock.h"
#include "defaults.h"
#include "common-utils.h"

/* to be used only by fops, nobody else */
#define BDB_ENV(this) ((((struct bdb_private *)this->private)->b_table)->dbenv)
#define B_TABLE(this) (((struct bdb_private *)this->private)->b_table)


int32_t 
bdb_mknod (call_frame_t *frame,
           xlator_t *this,
           loc_t *loc,
           mode_t mode,
           dev_t dev)
{
	int32_t     op_ret     = -1;
	int32_t     op_errno   = EINVAL;
	char       *key_string = NULL; /* after translating loc->path to DB key */
	char       *db_path    = NULL;
	bctx_t     *bctx       = NULL;
	struct stat stbuf      = {0,};


	GF_VALIDATE_OR_GOTO ("bdb", frame, out);
	GF_VALIDATE_OR_GOTO ("bdb", this, out);
	GF_VALIDATE_OR_GOTO (this->name, loc, out);

	if (!S_ISREG(mode)) {
		gf_log (this->name,
			GF_LOG_DEBUG,
			"mknod for non-regular file");
		op_ret = -1;
		op_errno = EPERM;
		goto out;
	} /* if(!S_ISREG(mode)) */
  
	bctx = bctx_parent (B_TABLE(this), loc->path);
  
	if (bctx == NULL) {
		gf_log (this->name,
			GF_LOG_ERROR,
			"failed to get bctx for path: %s", loc->path);
		op_ret = -1;
		op_errno = ENOENT;
		goto out;
	} /* if(bctx == NULL) */

	MAKE_REAL_PATH_TO_STORAGE_DB (db_path, this, bctx->directory);
	
	op_ret = lstat (db_path, &stbuf);				
	op_errno = errno;				
	if (op_ret != 0) {				
		gf_log (this->name, GF_LOG_ERROR,	
			"failed to lstat on %s (%s)",	
			db_path, strerror (op_errno));		
		goto out;				
	}						

	MAKE_KEY_FROM_PATH (key_string, loc->path);
	op_ret = bdb_db_put (bctx, NULL, key_string, NULL, 0, 0, 0);
	if (op_ret > 0) {
		/* create successful */
		stbuf.st_ino = bdb_inode_transform (stbuf.st_ino, bctx);
		stbuf.st_mode  = mode;
		stbuf.st_size = 0;
		stbuf.st_blocks = BDB_COUNT_BLOCKS (stbuf.st_size, stbuf.st_blksize);
	} else {
		gf_log (this->name,
			GF_LOG_ERROR,
			"bdb_db_get() failed for path: %s", loc->path);
		op_ret = -1;
		op_errno = ENOENT;
	}/* if (!op_ret)...else */

out:
	if (bctx) {
		/* NOTE: bctx_unref always returns success, 
		 * see description of bctx_unref for more details */
		bctx_unref (bctx);
	}

	frame->root->rsp_refs = NULL;  
  
	STACK_UNWIND (frame, op_ret, op_errno, loc->inode, &stbuf);
	return 0;
}

static inline int32_t
is_dir_empty (xlator_t *this,
              loc_t *loc)
{
	int32_t        ret       = 1;
	bctx_t        *bctx      = NULL;
	DIR           *dir       = NULL;
	char          *real_path = NULL;
	void          *dbstat    = NULL;
	struct dirent *entry     = NULL;

	GF_VALIDATE_OR_GOTO ("bdb", this, out);
	GF_VALIDATE_OR_GOTO (this->name, loc, out);
    
	bctx = bctx_lookup (B_TABLE(this), loc->path);
	if (bctx == NULL) {
		gf_log (this->name,
			GF_LOG_DEBUG, 
			"failed to get bctx from inode for dir: %s,"
			"assuming empty directory",
			loc->path);
		ret = 1;
		goto out;
	}

	dbstat = bdb_db_stat (bctx, NULL, 0);
	if (dbstat) {
		switch (bctx->table->access_mode)
		{
		case DB_HASH:
			ret = (((DB_HASH_STAT *)dbstat)->hash_nkeys == 0);
			break;
		case DB_BTREE:
		case DB_RECNO:
			ret = (((DB_BTREE_STAT *)dbstat)->bt_nkeys == 0);
			break;
		case DB_QUEUE:
			ret = (((DB_QUEUE_STAT *)dbstat)->qs_nkeys == 0);
			break;
		case DB_UNKNOWN:
			gf_log (this->name,
				GF_LOG_CRITICAL,
				"unknown access-mode set for db");
			ret = 0;
		}
	} else {
		gf_log (this->name,
			GF_LOG_ERROR,
			"failed to get db stat for db at path: %s", loc->path);
		ret = 1;
		goto out;
	}
	
	MAKE_REAL_PATH (real_path, this, loc->path);
	dir = opendir (real_path);
	if (dir == NULL) {
		gf_log (this->name,
			GF_LOG_DEBUG,
			"failed to opendir(%s)", loc->path);
		ret = 0;
		goto out;
	}

	while ((entry = readdir (dir))) {
		if ((!IS_BDB_PRIVATE_FILE(entry->d_name)) && 
		    (!IS_DOT_DOTDOT(entry->d_name))) {
			gf_log (this->name,
				GF_LOG_DEBUG,
				"directory (%s) not empty, has a non-db entry", 
				loc->path);
			ret = 0;
			break;
		}/* if(!IS_BDB_PRIVATE_FILE()) */
	} /* while(true) */
	closedir (dir);
out:  
	if (bctx) {
		/* NOTE: bctx_unref always returns success, 
		 * see description of bctx_unref for more details */
		bctx_unref (bctx);
	}

	return ret;
}

int32_t 
bdb_rename (call_frame_t *frame,
            xlator_t *this,
            loc_t *oldloc,
            loc_t *newloc)
{
	struct bdb_private *private      = NULL;
	bctx_table_t       *table        = NULL;
	bctx_t             *oldbctx      = NULL;
	bctx_t             *newbctx      = NULL;
	bctx_t             *tmpbctx      = NULL;
	int32_t             op_ret       = -1;
	int32_t             op_errno     = ENOENT;
	int32_t             read_size    = 0;
	struct stat         stbuf        = {0,};
	struct stat         old_stbuf    = {0,};
	DB_TXN             *txnid        = NULL;
	char               *real_newpath = NULL;
	char               *real_oldpath = NULL;
	char               *oldkey       = NULL;
	char               *newkey       = NULL;
	char               *buf          = NULL; /* pointer to temporary buffer, where
						  * the contents of a file are read, if
						  * file being renamed is a regular file */
	char               *real_db_newpath = NULL;
	char               *tmp_db_newpath  = NULL;

	GF_VALIDATE_OR_GOTO ("bdb", frame, out);
	GF_VALIDATE_OR_GOTO ("bdb", this, out);
	GF_VALIDATE_OR_GOTO (this->name, newloc, out);
	GF_VALIDATE_OR_GOTO (this->name, oldloc, out);
    
	private = this->private;
	table = private->b_table;

	MAKE_REAL_PATH (real_oldpath, this, oldloc->path);

	if (S_ISREG (oldloc->inode->st_mode)) {
		oldbctx = bctx_parent (B_TABLE(this), oldloc->path);
		MAKE_REAL_PATH (real_newpath, this, newloc->path);

		op_ret = lstat (real_newpath, &stbuf);
		
		if ((op_ret == 0) && (S_ISDIR (stbuf.st_mode))) {
			op_ret = -1;
			op_errno = EISDIR;
			goto out;
		} 
		if (op_ret == 0) {
			/* destination is a symlink */
			MAKE_KEY_FROM_PATH (oldkey, oldloc->path);
			MAKE_KEY_FROM_PATH (newkey, newloc->path);

			op_ret = unlink (real_newpath);
			op_errno = errno;
			if (op_ret != 0) {
				gf_log (this->name,
					GF_LOG_ERROR,
					"failed to unlink %s (%s)", 
					newloc->path, strerror (op_errno));
				goto out;
			}
			newbctx = bctx_parent (B_TABLE (this), newloc->path);
			GF_VALIDATE_OR_GOTO (this->name, newbctx, out);

			op_ret = bdb_txn_begin (BDB_ENV(this), &txnid);

			if ((read_size = 
			     bdb_db_get (oldbctx, txnid, oldkey, &buf, 0, 0)) < 0) {
				bdb_txn_abort (txnid);
			} else if ((op_ret = 
				    bdb_db_del (oldbctx, txnid, oldkey)) != 0) {
				bdb_txn_abort (txnid);
			} else if ((op_ret = bdb_db_put (newbctx, txnid, 
							 newkey, buf, 
							 read_size, 0, 0)) != 0) {
				bdb_txn_abort (txnid);
			} else {
				bdb_txn_commit (txnid);
			}
			
			/* NOTE: bctx_unref always returns success, 
			 * see description of bctx_unref for more details */
			bctx_unref (newbctx);
		} else {
			/* destination doesn't exist or a regular file */
			MAKE_KEY_FROM_PATH (oldkey, oldloc->path);
			MAKE_KEY_FROM_PATH (newkey, newloc->path);

			newbctx = bctx_parent (B_TABLE (this), newloc->path);
			GF_VALIDATE_OR_GOTO (this->name, newbctx, out);

			op_ret = bdb_txn_begin (BDB_ENV(this), &txnid);

			if ((read_size = bdb_db_get (oldbctx, txnid, 
						     oldkey, &buf, 
						     0, 0)) < 0) {
				bdb_txn_abort (txnid);
			} else if ((op_ret = bdb_db_del (oldbctx, 
							 txnid, oldkey)) != 0) {
				bdb_txn_abort (txnid);
			} else if ((op_ret = bdb_db_put (newbctx, txnid, 
							 newkey, buf, 
							 read_size, 0, 0)) != 0) {
				bdb_txn_abort (txnid);
			} else {
				bdb_txn_commit (txnid);
			}
      
			/* NOTE: bctx_unref always returns success, 
			 * see description of bctx_unref for more details */
			bctx_unref (newbctx);
		}
		/* NOTE: bctx_unref always returns success, 
		 * see description of bctx_unref for more details */
		bctx_unref (oldbctx);
	} else if (S_ISLNK (oldloc->inode->st_mode)) {
		MAKE_REAL_PATH (real_newpath, this, newloc->path);
		op_ret = lstat (real_newpath, &stbuf);
		if ((op_ret == 0) && (S_ISDIR (stbuf.st_mode))) {
			op_ret = -1;
			op_errno = EISDIR;
			goto out;
		}

		if (op_ret == 0){
			/* destination exists and is also a symlink */
			MAKE_REAL_PATH (real_oldpath, this, oldloc->path);
			op_ret = rename (real_oldpath, real_newpath);
			op_errno = errno;
			
			if (op_ret != 0) {
				gf_log (this->name,
					GF_LOG_ERROR,
					"failed to rename symlink %s (%s)", 
					oldloc->path, strerror (op_errno));
			}
			goto out;
		} 
		
		/* destination doesn't exist */
		MAKE_REAL_PATH (real_oldpath, this, oldloc->path);
		MAKE_KEY_FROM_PATH (newkey, newloc->path);
		newbctx = bctx_parent (B_TABLE (this), newloc->path);
		GF_VALIDATE_OR_GOTO (this->name, newbctx, out);
		
		op_ret = bdb_db_del (newbctx, txnid, newkey);
		if (op_ret != 0) {
			/* no problem */
		} 
		op_ret = rename (real_oldpath, real_newpath);
		op_errno = errno;
		if (op_ret != 0) {
			gf_log (this->name, 
				GF_LOG_ERROR,
				"failed to rename %s to %s (%s)",
				oldloc->path, newloc->path, strerror (op_errno));
			goto out;
		}
		/* NOTE: bctx_unref always returns success, 
		 * see description of bctx_unref for more details */
		bctx_unref (newbctx);
	} else if (S_ISDIR (oldloc->inode->st_mode) && 
		   (old_stbuf.st_nlink == 2)) {

		tmp_db_newpath = tempnam (private->export_path, "rename_temp");
		GF_VALIDATE_OR_GOTO (this->name, tmp_db_newpath, out);

		MAKE_REAL_PATH (real_newpath, this, newloc->path);

		MAKE_REAL_PATH_TO_STORAGE_DB (real_db_newpath, this, newloc->path);

		oldbctx = bctx_lookup (B_TABLE(this), oldloc->path);
		op_ret = -1;
		op_errno = EINVAL;
		GF_VALIDATE_OR_GOTO (this->name, oldbctx, out);

		op_ret = lstat (real_newpath, &stbuf);
		if ((op_ret == 0) && 
		    S_ISDIR (stbuf.st_mode) && 
		    is_dir_empty (this, newloc)) {
			
			tmpbctx = bctx_rename (oldbctx, tmp_db_newpath);
			op_ret = -1;
			op_errno = ENOENT;
			GF_VALIDATE_OR_GOTO (this->name, tmpbctx, out);

			op_ret = rename (real_oldpath, real_newpath);
			op_errno = errno;
			if (op_ret != 0) {
				gf_log (this->name,
					GF_LOG_ERROR,
					"rename directory %s to %s failed: %s", 
					oldloc->path, newloc->path, 
					strerror (errno));
				op_ret = bdb_db_rename (table, 
							tmp_db_newpath, 
							oldbctx->db_path);
				if (op_ret != 0) {
					gf_log (this->name,
						GF_LOG_ERROR,
						"renaming temp database back to old db failed"
						" for directory %s", oldloc->path);
					goto out;
				} else {
					/* this is a error case, set op_errno & op_ret */
					op_ret = -1;
					op_errno = ENOENT; /* TODO: errno */
				}
			} 
			op_ret = bdb_db_rename (table, tmp_db_newpath, real_db_newpath);
			if (op_ret != 0) {
				gf_log (this->name,
					GF_LOG_ERROR,
					"renaming temp database to new db failed"
					" for directory %s", oldloc->path);
				goto out;
			}
		} else if ((op_ret != 0) && (errno == ENOENT)) {
			tmp_db_newpath = tempnam (private->export_path, "rename_temp");
			GF_VALIDATE_OR_GOTO (this->name, tmp_db_newpath, out);

			tmpbctx = bctx_rename (oldbctx, tmp_db_newpath);
			op_ret = -1;
			op_errno = ENOENT;
			GF_VALIDATE_OR_GOTO (this->name, tmpbctx, out);

			op_ret = rename (real_oldpath, real_newpath);
			op_errno = errno;
			if (op_ret != 0) {
				gf_log (this->name,
					GF_LOG_ERROR,
					"rename directory %s to %s failed: %s", 
					oldloc->path, newloc->path, 
					strerror (errno));
				op_ret = bdb_db_rename (table, 
							tmp_db_newpath, 
							oldbctx->db_path);
				if (op_ret != 0) {
					gf_log (this->name,
						GF_LOG_ERROR,
						"renaming temp database back to old db failed"
						" for directory %s", oldloc->path);
					goto out;
				} else {
					/* this is a error case, set op_errno & op_ret */
					op_ret = -1;
					op_errno = ENOENT; /* TODO: errno */
				}
			} else {
				op_ret = bdb_db_rename (table, 
							tmp_db_newpath, 
							real_db_newpath);
				if (op_ret != 0) {
					gf_log (this->name,
						GF_LOG_ERROR,
						"renaming temp database to new db failed"
						" for directory %s", oldloc->path);
					goto out;
				} else {
					/* this is a error case, set op_errno & op_ret */
					op_ret = -1;
					op_errno = ENOENT; /* TODO: errno */
				}
			}
		}
	} else {
		gf_log (this->name,
			GF_LOG_CRITICAL,
			"rename called on non-existent file type");
		op_ret = -1;
		op_errno = EPERM;
	}

out:
	frame->root->rsp_refs = NULL;
	STACK_UNWIND (frame, op_ret, op_errno, &stbuf);
	return 0;
}

int32_t 
bdb_link (call_frame_t *frame, 
          xlator_t *this,
          loc_t *oldloc,
          loc_t *newloc)
{
	frame->root->rsp_refs = NULL;
	STACK_UNWIND (frame, -1, EPERM, NULL, NULL);
	return 0;
}

int32_t
is_space_left (xlator_t *this,
	       size_t size)
{
	struct bdb_private *private = this->private;
	struct statvfs stbuf = {0,};
	int32_t ret = -1;
	fsblkcnt_t req_blocks = 0;
	fsblkcnt_t usable_blocks = 0;

	ret = statvfs (private->export_path, &stbuf);
	if (ret != 0) {
		gf_log (this->name,
			GF_LOG_ERROR,
			"failed to do statvfs on %s", private->export_path);
		return 0;
	} else {
		req_blocks = (size / stbuf.f_frsize) + 1;

		usable_blocks = (stbuf.f_bfree - BDB_ENOSPC_THRESHOLD); 
		
		gf_log (this->name,
			GF_LOG_DEBUG,
			"requested size: %d\nfree blocks: %d\nblock size: %d\nfrag size: %d",
			size, stbuf.f_bfree, stbuf.f_bsize, stbuf.f_frsize);
		
		if (req_blocks < usable_blocks)
			return 1;
		else 
			return 0;
	}
}

int32_t 
bdb_create (call_frame_t *frame,
            xlator_t *this,
            loc_t *loc,
            int32_t flags,
            mode_t mode,
            fd_t *fd)
{
	int32_t             op_ret     = -1;
	int32_t             op_errno   = EPERM;
	char               *db_path    = NULL;
	struct stat         stbuf      = {0,};
	bctx_t             *bctx       = NULL;
	struct bdb_private *private    = NULL; 
	char               *key_string = NULL;
	struct bdb_fd      *bfd        = NULL;
	
	GF_VALIDATE_OR_GOTO ("bdb", frame, out);
	GF_VALIDATE_OR_GOTO ("bdb", this, out);
	GF_VALIDATE_OR_GOTO (this->name, loc, out);
	GF_VALIDATE_OR_GOTO (this->name, fd, out);

	private = this->private;

	bctx = bctx_parent (B_TABLE(this), loc->path);
	op_errno = ENOENT;
	GF_VALIDATE_OR_GOTO (this->name, bctx, out);

	MAKE_REAL_PATH_TO_STORAGE_DB (db_path, this, bctx->directory);
	op_ret = lstat (db_path, &stbuf);				
	op_errno = errno;				
	if (op_ret != 0) {				
		gf_log (this->name, GF_LOG_ERROR,	
			"failed to lstat on %s (%s)",	
			db_path, strerror (op_errno));		
		goto out;				
	}						

	MAKE_KEY_FROM_PATH (key_string, loc->path);
	op_ret = bdb_db_put (bctx, NULL, key_string, NULL, 0, 0, 0);
	op_errno = EINVAL;
	GF_VALIDATE_OR_GOTO (this->name, (op_ret == 0), out);
	
        /* create successful */
	bfd = calloc (1, sizeof (*bfd));
	op_ret = -1;
	op_errno = ENOMEM;
	GF_VALIDATE_OR_GOTO (this->name, bfd, out);
		
	/* NOTE: bdb_get_bctx_from () returns bctx with a ref */
	bfd->ctx = bctx; 
	bfd->key = strdup (key_string);
	op_ret = -1;
	op_errno = ENOMEM;
	GF_VALIDATE_OR_GOTO (this->name, bfd->key, out);
	
	BDB_SET_BFD (this, fd, bfd);
		
	stbuf.st_ino = bdb_inode_transform (stbuf.st_ino, bctx);
	stbuf.st_mode = private->file_mode;
	stbuf.st_size = 0;
	stbuf.st_nlink = 1;
	stbuf.st_blocks = BDB_COUNT_BLOCKS (stbuf.st_size, stbuf.st_blksize);
	op_ret = 0;
	op_errno = 0;
out:
	frame->root->rsp_refs = NULL;
	STACK_UNWIND (frame, op_ret, op_errno, fd, loc->inode, &stbuf);

	return 0;
}


/* bdb_open
 *
 * as input parameters bdb_open gets the file name, i.e key. bdb_open should effectively 
 * do: store key, open storage db, store storage-db pointer.
 *
 */
int32_t 
bdb_open (call_frame_t *frame,
          xlator_t *this,
          loc_t *loc,
          int32_t flags,
          fd_t *fd)
{
	int32_t         op_ret     = -1;
	int32_t         op_errno   = EINVAL;
	bctx_t         *bctx       = NULL;
	char           *key_string = NULL;
	struct bdb_fd  *bfd        = NULL;

	GF_VALIDATE_OR_GOTO ("bdb", frame, out);
	GF_VALIDATE_OR_GOTO ("bdb", this, out);
	GF_VALIDATE_OR_GOTO (this->name, loc, out);
	GF_VALIDATE_OR_GOTO (this->name, fd, out);

	bctx = bctx_parent (B_TABLE(this), loc->path);
	op_errno = EBADFD;
	GF_VALIDATE_OR_GOTO (this->name, bctx, out);

	bfd = calloc (1, sizeof (*bfd));
	op_errno = ENOMEM;
	GF_VALIDATE_OR_GOTO (this->name, bfd, out);

	/* NOTE: bctx_parent () returns bctx with a ref */
	bfd->ctx = bctx;
      
	MAKE_KEY_FROM_PATH (key_string, loc->path);
	bfd->key = strdup (key_string);
	op_ret = -1;
	op_errno = ENOMEM;
	GF_VALIDATE_OR_GOTO (this->name, bfd->key, out);

	BDB_SET_BFD (this, fd, bfd);
	op_ret = 0;
out:
	frame->root->rsp_refs = NULL;
	STACK_UNWIND (frame, op_ret, op_errno, fd);

	return 0;
}

int32_t 
bdb_readv (call_frame_t *frame,
           xlator_t *this,
           fd_t *fd,
           size_t size,
           off_t offset)
{
	int32_t        op_ret     = -1;
	int32_t        op_errno   = EINVAL;
	struct iovec   vec        = {0,};
	struct stat    stbuf      = {0,};
	struct bdb_fd *bfd        = NULL;  
	dict_t        *reply_dict = NULL;
	char          *buf        = NULL;
	data_t        *buf_data   = NULL;
	char          *db_path    = NULL;
	int32_t        read_size  = 0;

	GF_VALIDATE_OR_GOTO ("bdb", frame, out);
	GF_VALIDATE_OR_GOTO ("bdb", this, out);
	GF_VALIDATE_OR_GOTO (this->name, fd, out);

	bfd = bdb_extract_bfd (fd, this->name);
	op_errno = EBADFD;
	GF_VALIDATE_OR_GOTO (this->name, bfd, out);

	MAKE_REAL_PATH_TO_STORAGE_DB (db_path, this, bfd->ctx->directory);
	op_ret = lstat (db_path, &stbuf);				
	op_errno = errno;				
	if (op_ret != 0) {				
		gf_log (this->name, GF_LOG_ERROR,	
			"failed to lstat on %s (%s)",	
			db_path, strerror (op_errno));		
		goto out;				
	}						

	/* we are ready to go */
	op_ret = bdb_db_get (bfd->ctx, NULL, 
			     bfd->key, &buf, 
			     size, offset);
	read_size = op_ret;
	if (op_ret == -1) {
		gf_log (this->name,
			GF_LOG_ERROR,
			"failed to do db_storage_get()");
		op_ret = -1;
		op_errno = ENOENT;
		goto out;
	} else if (op_ret == 0) {
		goto out;
	}

	buf_data = get_new_data ();
	op_ret = -1;
	op_errno = ENOMEM;
	GF_VALIDATE_OR_GOTO (this->name, buf_data, out);

	reply_dict = get_new_dict ();
	op_ret = -1;
	op_errno = ENOMEM;
	GF_VALIDATE_OR_GOTO (this->name, reply_dict, out);

	reply_dict->is_locked = 1;
	buf_data->is_locked = 1;
	buf_data->data      = buf;

	if (size < read_size) {
		op_ret = size;
		read_size = size;
	}

	buf_data->len       = op_ret;
      
	dict_set (reply_dict, NULL, buf_data);
      
	frame->root->rsp_refs = dict_ref (reply_dict);

	vec.iov_base = buf;
	vec.iov_len = read_size;
      
	stbuf.st_ino = fd->inode->ino;
	stbuf.st_size = op_ret ; 
	stbuf.st_blocks = BDB_COUNT_BLOCKS (stbuf.st_size, stbuf.st_blksize);
	op_ret = size;
out:  
	STACK_UNWIND (frame, op_ret, op_errno, &vec, 1, &stbuf);

	if (reply_dict)
		dict_unref (reply_dict);

	return 0;
}


int32_t 
bdb_writev (call_frame_t *frame,
            xlator_t *this,
            fd_t *fd,
            struct iovec *vector,
            int32_t count,
            off_t offset)
{
	int32_t        op_ret   = -1;
	int32_t        op_errno = EINVAL;
	struct stat    stbuf    = {0,};
	struct bdb_fd *bfd      = NULL;
	int32_t        idx      = 0;
	off_t          c_off    = offset;
	int32_t        c_ret    = -1;
	char          *db_path  = NULL;
	size_t         total_size = 0;

	GF_VALIDATE_OR_GOTO ("bdb", frame, out);
	GF_VALIDATE_OR_GOTO ("bdb", this, out);
	GF_VALIDATE_OR_GOTO (this->name, fd, out);
	GF_VALIDATE_OR_GOTO (this->name, vector, out);

	bfd = bdb_extract_bfd (fd, this->name);
	op_errno = EBADFD;
	GF_VALIDATE_OR_GOTO (this->name, bfd, out);

	MAKE_REAL_PATH_TO_STORAGE_DB (db_path, this, bfd->ctx->directory);
	op_ret = lstat (db_path, &stbuf);				
	op_errno = errno;				
	if (op_ret != 0) {				
		gf_log (this->name, GF_LOG_ERROR,	
			"failed to lstat on %s (%s)",	
			db_path, strerror (op_errno));		
		goto out;				
	}						

 
	for (idx = 0; idx < count; idx++)
		total_size += vector[idx].iov_len;
     
	if (!is_space_left (this, total_size)) {
		gf_log (this->name,
			GF_LOG_ERROR,
			"requested storage for %d, ENOSPC", total_size);
		op_ret = -1;
		op_errno = ENOSPC;
		goto out;
	}
 

	/* we are ready to go */
	for (idx = 0; idx < count; idx++) {
		c_ret = bdb_db_put (bfd->ctx, NULL, 
				    bfd->key, vector[idx].iov_base, 
				    vector[idx].iov_len, c_off, 0);
		if (c_ret != 0) {
			gf_log (this->name,
				GF_LOG_ERROR,
				"failed to do bdb_db_put at offset: %d for file: %s", 
				c_off, bfd->key);
			break;
		} else {
			c_off += vector[idx].iov_len;
		}
		op_ret += vector[idx].iov_len;
	} /* for(idx=0;...)... */
    
	if (c_ret) {
		/* write failed */
		gf_log (this->name,
			GF_LOG_ERROR,
			"failed to do bdb_db_put(): %s", 
			db_strerror (op_ret));
		op_ret = -1;
		op_errno = EBADFD; /* TODO: search for a more meaningful errno */
		goto out;
	} 
	/* NOTE: we want to increment stbuf->st_size, as stored in db */
	stbuf.st_size = op_ret;
	stbuf.st_blocks = BDB_COUNT_BLOCKS (stbuf.st_size, stbuf.st_blksize);
	op_errno = 0;

out:
	frame->root->rsp_refs = NULL;
	STACK_UNWIND (frame, op_ret, op_errno, &stbuf);
	return 0;
}

int32_t 
bdb_flush (call_frame_t *frame,
           xlator_t *this,
           fd_t *fd)
{
	int32_t        op_ret   = -1;
	int32_t        op_errno = EPERM;
	struct bdb_fd *bfd      = NULL;
	
	GF_VALIDATE_OR_GOTO ("bdb", frame, out);
	GF_VALIDATE_OR_GOTO ("bdb", this, out);
	GF_VALIDATE_OR_GOTO (this->name, fd, out);

	bfd = bdb_extract_bfd (fd, this->name);
	op_errno = EBADFD;
	GF_VALIDATE_OR_GOTO (this->name, bfd, out);
	
        /* do nothing */
	op_ret = 0;
	op_errno = 0;

out:
	frame->root->rsp_refs = NULL;
	STACK_UNWIND (frame, op_ret, op_errno);
	return 0;
}

int32_t 
bdb_release (xlator_t *this,
	     fd_t *fd)
{
  int32_t op_ret = -1;
  int32_t op_errno = EBADFD;
  struct bdb_fd *bfd = NULL;
  
  if ((bfd = bdb_extract_bfd (fd, this->name)) == NULL){
    gf_log (this->name,
	    GF_LOG_ERROR,
	    "failed to extract %s specific information from fd:%p", this->name, fd);
    op_ret = -1;
    op_errno = EBADFD;
  } else {
    bctx_unref (bfd->ctx);
    bfd->ctx = NULL; 
    
    if (bfd->key)
      free (bfd->key); /* we did strdup() in bdb_open() */
    free (bfd);
    op_ret = 0;
    op_errno = 0;
  } /* if((fd->ctx == NULL)...)...else */

  return 0;
}/* bdb_release */


int32_t 
bdb_fsync (call_frame_t *frame,
           xlator_t *this,
           fd_t *fd,
           int32_t datasync)
{
	frame->root->rsp_refs = NULL;
	STACK_UNWIND (frame, 0, 0);
	return 0;
}/* bdb_fsync */

static int gf_bdb_lk_log;

int32_t 
bdb_lk (call_frame_t *frame,
        xlator_t *this,
        fd_t *fd,
        int32_t cmd,
        struct flock *lock)
{
	struct flock nullock = {0, };

	gf_bdb_lk_log++;
	if (!(gf_bdb_lk_log % GF_UNIVERSAL_ANSWER)) {
		gf_log (this->name, GF_LOG_ERROR, 
			"\"features/posix-locks\" translator is not loaded, you need to use it");
	}

	frame->root->rsp_refs = NULL;
	STACK_UNWIND (frame, -1, ENOSYS, &nullock);
	return 0;
}/* bdb_lk */

/* bdb_lookup
 *
 * there are four possibilities for a file being looked up:
 *  1. file exists and is a directory.
 *  2. file exists and is a symlink.
 *  3. file exists and is a regular file.
 *  4. file does not exist.
 * case 1 and 2 are handled by doing lstat() on the @loc. if the file is a directory or symlink, 
 * lstat() succeeds. lookup continues to check if the @loc belongs to case-3 only if lstat() fails.
 * to check for case 3, bdb_lookup does a bdb_db_get() for the given @loc. (see description of 
 * bdb_db_get() for more details on how @loc is transformed into db handle and key). if check 
 * for case 1, 2 and 3 fail, we proceed to conclude that file doesn't exist (case 4).
 *
 * @frame:      call frame.
 * @this:       xlator_t of this instance of bdb xlator.
 * @loc:        loc_t specifying the file to operate upon.
 * @need_xattr: if need_xattr != 0, we are asked to return all the extended attributed of @loc, 
 *             if any exist, in a dictionary. if @loc is a regular file and need_xattr is set, then 
 *             we look for value of need_xattr. if need_xattr > sizo-of-the-file @loc, then the file
 *             content of @loc is returned in dictionary of xattr with 'glusterfs.content' as
 *             dictionary key.
 *
 * NOTE: bdb currently supports only directories, symlinks and regular files. 
 *
 * NOTE: bdb_lookup returns the 'struct stat' of underlying file itself, in case of directory and 
 *      symlink (st_ino is modified as bdb allocates its own set of inodes of all files). for 
 *      regular files, bdb uses 'struct stat' of the database file in which the @loc is stored 
 *      as templete and modifies st_ino (see bdb_inode_transform for more details), st_mode (can 
 *      be set in volume spec file 'option file-mode <mode>'), st_size (exact size of the @loc
 *      contents), st_blocks (block count on the underlying filesystem to accomodate st_size, 
 *      see BDB_COUNT_BLOCKS in bdb.h for more details).
 */
int32_t
bdb_lookup (call_frame_t *frame,
            xlator_t *this,
            loc_t *loc,
            int32_t need_xattr)
{
	struct stat stbuf           = {0, };
	int32_t op_ret              = -1;
	int32_t op_errno            = ENOENT;
	dict_t *xattr               = NULL;
	char *pathname              = NULL;
	char *directory             = NULL;
	char *real_path             = NULL;
	bctx_t *bctx                = NULL;
	char *db_path               = NULL;
	struct bdb_private *private = NULL;
	char *key_string            = NULL;
	int32_t entry_size          = 0;
	char *file_content          = NULL;
	data_t *file_content_data   = NULL;
	
	GF_VALIDATE_OR_GOTO ("bdb", frame, out);
	GF_VALIDATE_OR_GOTO ("bdb", this, out);
	GF_VALIDATE_OR_GOTO (this->name, loc, out);

	private = this->private;

	MAKE_REAL_PATH (real_path, this, loc->path);

	pathname = strdup (loc->path);
	GF_VALIDATE_OR_GOTO (this->name, pathname, out);

	directory = dirname (pathname);
	GF_VALIDATE_OR_GOTO (this->name, directory, out);

	if (!strcmp (directory, loc->path)) {
		/* SPECIAL CASE: looking up root */
		op_ret = lstat (real_path, &stbuf);				
		op_errno = errno;				
		if (op_ret != 0) {				
			gf_log (this->name, GF_LOG_ERROR,	
				"failed to lstat on %s (%s)",	
				real_path, strerror (op_errno));		
			goto out;				
		}						

		/* bctx_lookup() returns NULL only when its time to wind up, 
		 * we should shutdown functioning */
		bctx = bctx_lookup (B_TABLE(this), (char *)loc->path);
		op_ret = -1;
		op_errno = EINVAL;
		GF_VALIDATE_OR_GOTO (this->name, bctx, out);
		
		stbuf.st_ino = 1;
		stbuf.st_mode = private->dir_mode;
	} else {
		MAKE_KEY_FROM_PATH (key_string, loc->path);
		op_ret = lstat (real_path, &stbuf);
		if ((op_ret == 0) && (S_ISDIR (stbuf.st_mode))){
			bctx = bctx_lookup (B_TABLE(this), (char *)loc->path);
			op_ret = -1;
			op_errno = ENOMEM;
			GF_VALIDATE_OR_GOTO (this->name, bctx, out);

			if (loc->ino) {
				/* revalidating directory inode */
				gf_log (this->name,
					GF_LOG_DEBUG,
					"revalidating directory %s", (char *)loc->path);
				stbuf.st_ino = loc->ino;
			} else {
				stbuf.st_ino = bdb_inode_transform (stbuf.st_ino, bctx);
			}
			stbuf.st_mode = private->dir_mode;
			op_ret = 0;
			op_errno = 0;
			goto out;
		} else if (op_ret == 0) {
			/* a symlink */
			gf_log (this->name,
				GF_LOG_DEBUG,
				"lookup called for symlink: %s", loc->path);
			bctx = bctx_parent (B_TABLE(this), loc->path);
			op_ret = -1;
			op_errno = ENOMEM;
			GF_VALIDATE_OR_GOTO (this->name, bctx, out);

			if (loc->ino) {
				stbuf.st_ino = loc->ino;
			} else {
				stbuf.st_ino = bdb_inode_transform (stbuf.st_ino, bctx);
			}
			stbuf.st_mode = private->symlink_mode;
			op_ret = 0;
			op_errno = 0;
			goto out;
		} 
		
		/* for regular files */
		bctx = bctx_parent (B_TABLE(this), loc->path);
		op_ret = -1;
		op_errno = ENOENT;
		GF_VALIDATE_OR_GOTO (this->name, bctx, out);

		if (need_xattr) {
			entry_size = bdb_db_get (bctx, 
						 NULL, 
						 loc->path, 
						 &file_content, 
						 0, 0);
		} else {
			entry_size = bdb_db_get (bctx, 
						 NULL, 
						 loc->path, 
						 NULL, 
						 0, 0);
		}
		
		op_ret = entry_size;
		op_errno = ENOENT;
		if (op_ret == -1) {
			gf_log (this->name,
				GF_LOG_DEBUG,
				"returning ENOENT for %s", loc->path);
			goto out;
		}

		MAKE_REAL_PATH_TO_STORAGE_DB (db_path, this, bctx->directory);
		op_ret = lstat (db_path, &stbuf);				
		op_errno = errno;				
		if (op_ret != 0) {				
			gf_log (this->name, GF_LOG_ERROR,	
				"failed to lstat on %s (%s)",	
				db_path, strerror (op_errno));		
			goto out;				
		}						

		if ((need_xattr >= entry_size) && 
		    (entry_size) && (file_content)) {
			file_content_data = data_from_dynptr (file_content, 
							      entry_size);
			xattr = get_new_dict ();
			dict_set (xattr, "glusterfs.content", 
				  file_content_data);
		} else {
			if (file_content)
				free (file_content);
		}

		if (loc->ino) {
			/* revalidate */
			stbuf.st_ino = loc->ino;
			stbuf.st_size = entry_size;
			stbuf.st_blocks = BDB_COUNT_BLOCKS (stbuf.st_size, stbuf.st_blksize);
		} else {
			/* fresh lookup, create an inode number */
			stbuf.st_ino = bdb_inode_transform (stbuf.st_ino, bctx);
			stbuf.st_size = entry_size;
			stbuf.st_blocks = BDB_COUNT_BLOCKS (stbuf.st_size, stbuf.st_blksize);
		}/* if(inode->ino)...else */
		stbuf.st_nlink = 1;
		stbuf.st_mode = private->file_mode;
	}
	op_ret = 0;
out:  
	if (bctx) {
		/* NOTE: bctx_unref always returns success, 
		 * see description of bctx_unref for more details */
		bctx_unref (bctx);
	}

	if (pathname)
		free (pathname);
  
	if (xattr)
		dict_ref (xattr);

	frame->root->rsp_refs = NULL;
	STACK_UNWIND (frame, op_ret, op_errno, loc->inode, &stbuf, xattr);
  
	if (xattr)
		dict_unref (xattr);

	return 0;
  
}/* bdb_lookup */

int32_t
bdb_stat (call_frame_t *frame,
          xlator_t *this,
          loc_t *loc)
{
 
	struct stat stbuf           = {0,};
	char *real_path             = NULL;
	int32_t op_ret              = -1;
	int32_t op_errno            = EINVAL;
	struct bdb_private *private = NULL;
	char *db_path               = NULL;
	bctx_t *bctx                = NULL;
	
	GF_VALIDATE_OR_GOTO ("bdb", frame, out);
	GF_VALIDATE_OR_GOTO ("bdb", this, out);
	GF_VALIDATE_OR_GOTO (this->name, loc, out);

	private = this->private;
	GF_VALIDATE_OR_GOTO (this->name, private, out);

	MAKE_REAL_PATH (real_path, this, loc->path);

	op_ret = lstat (real_path, &stbuf);
	op_errno = errno;
  	if (op_ret == 0) {
		/* directory or symlink */
		stbuf.st_ino = loc->inode->ino;
		if (S_ISDIR(stbuf.st_mode))
			stbuf.st_mode = private->dir_mode;
		else
			stbuf.st_mode = private->symlink_mode;
		/* we are done, lets unwind the stack */
		goto out;
	} 

	bctx = bctx_parent (B_TABLE(this), loc->path);
	op_ret = -1;
	op_errno = ENOENT;
	GF_VALIDATE_OR_GOTO (this->name, bctx, out);
  
	MAKE_REAL_PATH_TO_STORAGE_DB (db_path, this, bctx->directory);
	op_ret = lstat (db_path, &stbuf);				
	op_errno = errno;				
	if (op_ret != 0) {				
		gf_log (this->name, GF_LOG_ERROR,	
			"failed to lstat on %s (%s)",	
			db_path, strerror (op_errno));		
		goto out;				
	}						

	stbuf.st_size = bdb_db_get (bctx, NULL, loc->path, NULL, 0, 0);
	stbuf.st_blocks = BDB_COUNT_BLOCKS (stbuf.st_size, stbuf.st_blksize);
	stbuf.st_ino = loc->inode->ino;
	
out:
	if (bctx) {
		/* NOTE: bctx_unref always returns success, 
		 * see description of bctx_unref for more details */
		bctx_unref (bctx);
	}

	frame->root->rsp_refs = NULL;
	STACK_UNWIND (frame, op_ret, op_errno, &stbuf);

	return 0;
}/* bdb_stat */



/* bdb_opendir - in the world of bdb, open/opendir is all about opening correspondind databases.
 *               opendir in particular, opens the database for the directory which is
 *               to be opened. after opening the database, a cursor to the database is also created.
 *               cursor helps us get the dentries one after the other, and cursor maintains the state
 *               about current positions in directory. pack 'pointer to db', 'pointer to the
 *               cursor' into struct bdb_dir and store it in fd->ctx, we get from our parent xlator.
 *
 * @frame: call frame
 * @this:  our information, as we filled during init()
 * @loc:   location information
 * @fd:    file descriptor structure (glusterfs internal)
 *
 * return value - immaterial, async call.
 *
 */
int32_t 
bdb_opendir (call_frame_t *frame,
             xlator_t *this,
             loc_t *loc, 
             fd_t *fd)
{
	char           *real_path = NULL;
	int32_t         op_ret    = -1;
	int32_t         op_errno  = EINVAL;
	bctx_t         *bctx      = NULL;
	struct bdb_dir *bfd       = NULL;
  
	GF_VALIDATE_OR_GOTO ("bdb", frame, out);
	GF_VALIDATE_OR_GOTO ("bdb", this, out);
	GF_VALIDATE_OR_GOTO (this->name, loc, out);
	GF_VALIDATE_OR_GOTO (this->name, fd, out);

	MAKE_REAL_PATH (real_path, this, loc->path);

	bctx = bctx_lookup (B_TABLE(this), (char *)loc->path);
	op_errno = EBADFD;
	GF_VALIDATE_OR_GOTO (this->name, bctx, out);

	bfd = calloc (1, sizeof (*bfd));
	op_errno = ENOMEM;
	GF_VALIDATE_OR_GOTO (this->name, bfd, out);

	bfd->dir = opendir (real_path);
	op_errno = errno;
	GF_VALIDATE_OR_GOTO (this->name, bfd->dir, out);

	/* NOTE: bctx_lookup() return bctx with ref */
	bfd->ctx = bctx; 

	bfd->path = strdup (real_path);
	op_errno = ENOMEM;
	GF_VALIDATE_OR_GOTO (this->name, bfd->path, out);

	BDB_SET_BFD (this, fd, bfd);
	op_ret = 0;
out:  
	frame->root->rsp_refs = NULL;
	STACK_UNWIND (frame, op_ret, op_errno, fd);

	return 0;
}/* bdb_opendir */


int32_t
bdb_getdents (call_frame_t *frame,
              xlator_t     *this,
              fd_t         *fd,
              size_t        size,
              off_t         off,
              int32_t       flag)
{
	int32_t         op_ret         = -1;
	int32_t         op_errno       = EINVAL;
	int32_t         ret            = -1;
	int32_t         real_path_len  = 0;
	int32_t         entry_path_len = 0;
	int32_t         count          = 0;
	char           *real_path      = NULL;
	char           *entry_path     = NULL;
	char           *db_path        = NULL;
	dir_entry_t     entries        = {0, };
	dir_entry_t    *tmp            = NULL;
	DIR            *dir            = NULL;
	struct dirent  *dirent         = NULL;
	struct bdb_dir *bfd            = NULL;
	struct stat     db_stbuf       = {0,};
	struct stat     buf            = {0,};
	DBC            *cursorp        = NULL;
	size_t          tmp_name_len   = 0;

	GF_VALIDATE_OR_GOTO ("bdb", frame, out);
	GF_VALIDATE_OR_GOTO ("bdb", this, out);
	GF_VALIDATE_OR_GOTO (this->name, fd, out);

	bfd = bdb_extract_bfd (fd, this->name);
	op_errno = EBADFD;
	GF_VALIDATE_OR_GOTO (this->name, bfd, out);

	MAKE_REAL_PATH (real_path, this, bfd->path);
	dir = bfd->dir;

	while ((dirent = readdir (dir))) {
		if (!dirent)
			break;
    
		if (IS_BDB_PRIVATE_FILE(dirent->d_name)) {
			continue;
		}

		tmp_name_len = strlen (dirent->d_name);
		if (entry_path_len < (real_path_len + 1 + (tmp_name_len) + 1)) {
			entry_path_len = real_path_len + tmp_name_len + 1024;
			entry_path = realloc (entry_path, entry_path_len);
			op_errno = ENOMEM;
			GF_VALIDATE_OR_GOTO (this->name, entry_path, out);
		}
		
		strncpy (&entry_path[real_path_len+1], dirent->d_name, tmp_name_len);
		op_ret = stat (entry_path, &buf);				
		op_errno = errno;				
		if (op_ret != 0) {				
			gf_log (this->name, GF_LOG_ERROR,	
				"failed to lstat on %s (%s)",	
				entry_path, strerror (op_errno));		
			goto out;				
		}						

		if ((flag == GF_GET_DIR_ONLY) && 
		    (ret != -1 && !S_ISDIR(buf.st_mode))) {
			continue;
		}

		tmp = calloc (1, sizeof (*tmp));
		op_errno = ENOMEM;
		GF_VALIDATE_OR_GOTO (this->name, tmp, out);

		tmp->name = strdup (dirent->d_name);		       
		op_errno = ENOMEM;
		GF_VALIDATE_OR_GOTO (this->name, dirent->d_name, out);
		
		memcpy (&tmp->buf, &buf, sizeof  (buf));

		tmp->buf.st_ino = -1;
		if (S_ISLNK(tmp->buf.st_mode)) {
			char linkpath[GF_PATH_MAX] = {0,};
			ret = readlink (entry_path, linkpath, GF_PATH_MAX);
			if (ret != -1) {
				linkpath[ret] = '\0';
				tmp->link = strdup (linkpath);
			}
		} else {
			tmp->link = "";
		}

		count++;
        
		tmp->next = entries.next;
		entries.next = tmp;
		/* if size is 0, count can never be = size, so entire dir is read */

		if (count == size)
			break;
	}
    
	if ((flag != GF_GET_DIR_ONLY) && (count < size)) {
		/* read from db */
		op_ret = bdb_cursor_open (bfd->ctx, &cursorp);
		op_errno = EINVAL;
		GF_VALIDATE_OR_GOTO (this->name, (op_ret == 0), out);
        
		MAKE_REAL_PATH_TO_STORAGE_DB (db_path, this, bfd->ctx->directory);
		op_ret = lstat (db_path, &db_stbuf);				
		op_errno = errno;				
		if (op_ret != 0) {				
			gf_log (this->name, GF_LOG_ERROR,	
				"failed to lstat on %s (%s)",	
				db_path, strerror (op_errno));		
			goto out;				
		}						

		/* read all the entries in database, one after the other and put into dictionary */
		while (1) {
			DBT key = {0,}, value = {0,};
          
			key.flags = DB_DBT_MALLOC;
			value.flags = DB_DBT_MALLOC;
			op_ret = bdb_cursor_get (cursorp, &key, &value, DB_NEXT);
          
			if (op_ret == DB_NOTFOUND) {
				gf_log (this->name,
					GF_LOG_DEBUG,
					"end of list of key/value pair in db for directory: %s", 
					bfd->ctx->directory);
				op_ret = 0;
				op_errno = 0;
				break;
			} else if (op_ret != 0){
				gf_log (this->name,
					GF_LOG_ERROR,
					"failed to do cursor get for directory %s: %s", 
					bfd->ctx->directory, db_strerror (op_ret));
				op_ret = -1;
				op_errno = ENOENT;
				break;
			}
			/* successfully read */
			tmp = calloc (1, sizeof (*tmp));
			op_errno = ENOMEM;
			GF_VALIDATE_OR_GOTO (this->name, tmp, out);

			tmp->name = calloc (1, key.size + 1);
			op_errno = ENOMEM;
			GF_VALIDATE_OR_GOTO (this->name, tmp->name, out);

			memcpy (tmp->name, key.data, key.size);
			tmp->buf = db_stbuf;
			tmp->buf.st_size = bdb_db_get (bfd->ctx, NULL, 
						       tmp->name, NULL, 
						       0, 0);
			tmp->buf.st_blocks = BDB_COUNT_BLOCKS (tmp->buf.st_size, \
							       tmp->buf.st_blksize);
			/* FIXME: wat will be the effect of this? */
			tmp->buf.st_ino = -1;
			count++;
        
			tmp->next = entries.next;
			tmp->link = "";
			entries.next = tmp;
			/* if size is 0, count can never be = size, so entire dir is read */
			if (count == size)
				break;

			free (key.data);
		} /* while(1){ } */
		bdb_cursor_close (bfd->ctx, cursorp);
	} else {
		/* do nothing */
	}
	FREE (entry_path);
	op_ret = 0;

out:
	frame->root->rsp_refs = NULL;
	STACK_UNWIND (frame, op_ret, op_errno, &entries, count);

	while (entries.next) {
		tmp = entries.next;
		entries.next = entries.next->next;
		FREE (tmp->name);
		FREE (tmp);
	}
	return 0;
}/* bdb_getdents */


int32_t 
bdb_releasedir (xlator_t *this,
		fd_t *fd)
{
  int32_t op_ret = 0;
  int32_t op_errno = 0;
  struct bdb_dir *bfd = NULL;

  if ((bfd = bdb_extract_bfd (fd, this->name)) == NULL) {
    gf_log (this->name, 
	    GF_LOG_ERROR, 
	    "failed to extract fd data from fd=%p", fd);
    op_ret = -1;
    op_errno = EBADF;
  } else {
    if (bfd->path) {
      free (bfd->path);
    } else {
      gf_log (this->name, GF_LOG_ERROR, "bfd->path was NULL. fd=%p bfd=%p",
	      fd, bfd);
    }
    
    if (bfd->dir) {
      closedir (bfd->dir);
    } else {
      gf_log (this->name,
	      GF_LOG_ERROR,
	      "bfd->dir is NULL.");
    }
    if (bfd->ctx) {
      bctx_unref (bfd->ctx);
    } else {
      gf_log (this->name,
	      GF_LOG_ERROR,
	      "bfd->ctx is NULL");
    }
    free (bfd);
  }

  return 0;
}/* bdb_releasedir */


int32_t 
bdb_readlink (call_frame_t *frame,
              xlator_t *this,
              loc_t *loc,
              size_t size)
{
	char   *dest      = NULL;
	int32_t op_ret    = -1;
	int32_t op_errno  = EPERM;
	char   *real_path = NULL;

	GF_VALIDATE_OR_GOTO ("bdb", frame, out);
	GF_VALIDATE_OR_GOTO ("bdb", this, out);
	GF_VALIDATE_OR_GOTO (this->name, loc, out);

	dest = alloca (size + 1);
	GF_VALIDATE_OR_GOTO (this->name, dest, out);

	MAKE_REAL_PATH (real_path, this, loc->path);
  
	op_ret = readlink (real_path, dest, size);
  
	if (op_ret > 0)
		dest[op_ret] = 0;

	op_errno = errno;
  
	if (op_ret == -1) {
		gf_log (this->name,
			GF_LOG_DEBUG,
			"readlink failed on %s: %s", 
			loc->path, strerror (op_errno));
	}
out:
	frame->root->rsp_refs = NULL;
	STACK_UNWIND (frame, op_ret, op_errno, dest);

	return 0;
}/* bdb_readlink */


int32_t 
bdb_mkdir (call_frame_t *frame,
           xlator_t *this,
           loc_t *loc,
           mode_t mode)
{
	int32_t op_ret = -1;
	int32_t ret = -1;
	int32_t op_errno = EINVAL;
	char *real_path = NULL;
	struct stat stbuf = {0, };
	bctx_t *bctx = NULL;
	
	GF_VALIDATE_OR_GOTO ("bdb", frame, out);
	GF_VALIDATE_OR_GOTO ("bdb", this, out);
	GF_VALIDATE_OR_GOTO (this->name, loc, out);

	MAKE_REAL_PATH (real_path, this, loc->path);
	
	op_ret = mkdir (real_path, mode);				
	op_errno = errno;				
	if (op_ret != 0) {				
		gf_log (this->name, GF_LOG_ERROR,	
			"failed to mkdir %s (%s)",	
			real_path, strerror (op_errno));		
		goto out;				
	}						
	
	op_ret = chown (real_path, frame->root->uid, frame->root->gid);				
	op_errno = errno;				
	if (op_ret != 0) {				
		gf_log (this->name, GF_LOG_ERROR,	
			"failed to chmod on %s (%s)",	
			real_path, strerror (op_errno));		
		goto err;				
	}						

	op_ret = lstat (real_path, &stbuf);				
	op_errno = errno;				
	if (op_ret != 0) {				
		gf_log (this->name, GF_LOG_ERROR,	
			"failed to lstat on %s (%s)",	
			real_path, strerror (op_errno));		
		goto err;				
	}						

	bctx = bctx_lookup (B_TABLE(this), (char *)loc->path);
	op_errno = ENOMEM;
	GF_VALIDATE_OR_GOTO (this->name, bctx, err);

	stbuf.st_ino = bdb_inode_transform (stbuf.st_ino, bctx);
	
	goto out;

err:
	ret = rmdir (real_path);
	if (ret != 0) {			       
		gf_log (this->name,
			GF_LOG_ERROR,
			"failed to rmdir the directory created (%s)",
			strerror (errno));
	}
	

out:  
	if (bctx) {
		/* NOTE: bctx_unref always returns success, 
		 * see description of bctx_unref for more details */
		bctx_unref (bctx);
	}

	frame->root->rsp_refs = NULL;
	STACK_UNWIND (frame, op_ret, op_errno, loc->inode, &stbuf);

	return 0;
}/* bdb_mkdir */


int32_t 
bdb_unlink (call_frame_t *frame,
            xlator_t *this,
            loc_t *loc)
{
	int32_t op_ret    = -1;
	int32_t op_errno  = EINVAL;
	bctx_t *bctx      = NULL;
	char   *real_path = NULL;

  	GF_VALIDATE_OR_GOTO ("bdb", frame, out);
	GF_VALIDATE_OR_GOTO ("bdb", this, out);
	GF_VALIDATE_OR_GOTO (this->name, loc, out);

	bctx = bctx_parent (B_TABLE(this), loc->path);
	op_errno = ENOENT;
	GF_VALIDATE_OR_GOTO (this->name, bctx, out);

	op_ret = bdb_db_del (bctx, NULL, loc->path);
	if (op_ret == DB_NOTFOUND) {
		MAKE_REAL_PATH (real_path, this, loc->path);
		op_ret = unlink (real_path);				
		op_errno = errno;				
		if (op_ret != 0) {				
			gf_log (this->name, GF_LOG_ERROR,	
				"failed to unlink on %s (%s)",	
				real_path, strerror (op_errno));		
			goto out;				
		}						

	} else if (op_ret == 0) {
		op_errno = 0;
	}
out:
	if (bctx) {
		/* NOTE: bctx_unref always returns success, 
		 * see description of bctx_unref for more details */
		bctx_unref (bctx);
	}

	frame->root->rsp_refs = NULL;
	STACK_UNWIND (frame, op_ret, op_errno);

	return 0;
}/* bdb_unlink */


int32_t
bdb_rmelem (call_frame_t *frame,
            xlator_t *this,
            const char *path)
{
	int32_t op_ret   = -1; 
	int32_t op_errno = EPERM;

	frame->root->rsp_refs = NULL;
	STACK_UNWIND (frame, op_ret, op_errno);

	return 0;
} /* bdb_rmelm */


int32_t
bdb_do_rmdir (xlator_t *this,
              loc_t *loc)
{
	char   *real_path = NULL;
	int32_t ret       = -1;
	bctx_t *bctx      = NULL;
	DB_ENV *dbenv     = NULL;
	
	GF_VALIDATE_OR_GOTO ("bdb", this, out);
	GF_VALIDATE_OR_GOTO (this->name, loc, out);

	dbenv = BDB_ENV(this);
	GF_VALIDATE_OR_GOTO (this->name, dbenv, out);

	MAKE_REAL_PATH (real_path, this, loc->path);

	bctx = bctx_lookup (B_TABLE(this), loc->path);
	GF_VALIDATE_OR_GOTO (this->name, bctx, out);
	
	LOCK(&bctx->lock);
	{
		if (bctx->dbp == NULL) {
			goto unlock;
		}
	
		ret = bctx->dbp->close (bctx->dbp, 0);
		GF_VALIDATE_OR_GOTO (this->name, (ret == 0), unlock);

		bctx->dbp = NULL;

		ret = dbenv->dbremove (dbenv, NULL, bctx->db_path, NULL, 0);
		if (ret != 0) {
			gf_log (this->name,
				GF_LOG_ERROR,
				"failed to DB_ENV->dbremove() on path %s: %s", 
				loc->path, db_strerror (ret));
		}
	}
unlock:
	UNLOCK(&bctx->lock);
    
	if (ret) {
		gf_log (this->name,
			GF_LOG_ERROR,
			"failed to remove db %s: %s", bctx->db_path, db_strerror (ret));
		ret = -1;
		goto out;
	} 
	gf_log (this->name,
		GF_LOG_DEBUG,
		"removed db %s", bctx->db_path);
	ret = rmdir (real_path);

out:
	if (bctx) {
		/* NOTE: bctx_unref always returns success, 
		 * see description of bctx_unref for more details */
		bctx_unref (bctx);
	}

	return ret;
}

int32_t 
bdb_rmdir (call_frame_t *frame,
           xlator_t *this,
           loc_t *loc)
{
	int32_t op_ret   = -1; 
	int32_t op_errno = ENOTEMPTY;

	if (!is_dir_empty (this, loc)) {
		gf_log (this->name,
			GF_LOG_DEBUG,
			"rmdir: directory %s not empty", loc->path);
		op_errno = ENOTEMPTY;
		op_ret = -1;
		goto out;
	}

	op_ret = bdb_do_rmdir (this, loc);				
	if (op_ret != 0) {				
		gf_log (this->name, GF_LOG_ERROR,	
			"failed to bdb_do_rmdir on %s",	
			loc->path);		
		goto out;				
	}						

out:
	frame->root->rsp_refs = NULL;
	STACK_UNWIND (frame, op_ret, op_errno);

	return 0;
} /* bdb_rmdir */

int32_t 
bdb_symlink (call_frame_t *frame,
             xlator_t *this,
             const char *linkname,
             loc_t *loc)
{
	int32_t             op_ret    = -1;
	int32_t             op_errno  = EINVAL;
	char               *real_path = NULL;
	struct stat         stbuf     = {0,};
	struct bdb_private *private   = NULL; 
	bctx_t             *bctx      = NULL;
	
	GF_VALIDATE_OR_GOTO ("bdb", frame, out);
	GF_VALIDATE_OR_GOTO ("bdb", this, out);
	GF_VALIDATE_OR_GOTO (this->name, loc, out);
	GF_VALIDATE_OR_GOTO (this->name, linkname, out);

	private = this->private;
	GF_VALIDATE_OR_GOTO (this->name, private, out);

	MAKE_REAL_PATH (real_path, this, loc->path);
	op_ret = symlink (linkname, real_path);
	op_errno = errno;
	if (op_ret == 0) {
		op_ret = lstat (real_path, &stbuf);				
		op_errno = errno;				
		if (op_ret != 0) {				
			gf_log (this->name, GF_LOG_ERROR,	
				"failed to lstat on %s (%s)",	
				real_path, strerror (op_errno));		
			goto err;				
		}						

		bctx = bctx_parent (B_TABLE(this), loc->path);
		GF_VALIDATE_OR_GOTO (this->name, bctx, err);

		stbuf.st_ino = bdb_inode_transform (stbuf.st_ino, bctx);
		stbuf.st_mode = private->symlink_mode;

		goto out;
	}
err:
	op_ret = unlink (real_path);
	op_errno = errno;
	if (op_ret != 0) {
		gf_log (this->name, 
			GF_LOG_ERROR,
			"failed to unlink the previously created symlink (%s)",
			strerror (op_errno));
	}
	op_ret = -1;
	op_errno = ENOENT;
out:
	if (bctx) {
		/* NOTE: bctx_unref always returns success, 
		 * see description of bctx_unref for more details */
		bctx_unref (bctx);
	}

	frame->root->rsp_refs = NULL;
	STACK_UNWIND (frame, op_ret, op_errno, loc->inode, &stbuf);

	return 0;
} /* bdb_symlink */

int32_t 
bdb_chmod (call_frame_t *frame,
           xlator_t *this,
           loc_t *loc,
           mode_t mode)
{
	int32_t     op_ret    = -1;
	int32_t     op_errno  = EINVAL;
	char       *real_path = NULL;
	struct stat stbuf     = {0,};
	
	GF_VALIDATE_OR_GOTO ("bdb", frame, out);
	GF_VALIDATE_OR_GOTO ("bdb", this, out);
	GF_VALIDATE_OR_GOTO (this->name, loc, out);

	MAKE_REAL_PATH (real_path, this, loc->path);
	op_ret = lstat (real_path, &stbuf);				
	op_errno = errno;				
	if (op_ret != 0) {				
		gf_log (this->name, GF_LOG_ERROR,	
			"failed to lstat on %s (%s)",	
			real_path, strerror (op_errno));		
		goto out;				
	}						

	/* directory or symlink */
	op_ret = chmod (real_path, mode);
	op_errno = errno;

out:    
	frame->root->rsp_refs = NULL;
	STACK_UNWIND (frame, op_ret, op_errno, &stbuf);

	return 0;
}/* bdb_chmod */


int32_t 
bdb_chown (call_frame_t *frame,
           xlator_t *this,
           loc_t *loc,
           uid_t uid,
           gid_t gid)
{
	int32_t     op_ret    = -1;
	int32_t     op_errno  = EINVAL;
	char       *real_path = NULL;
	struct stat stbuf     = {0,};

	GF_VALIDATE_OR_GOTO ("bdb", frame, out);
	GF_VALIDATE_OR_GOTO ("bdb", this, out);
	GF_VALIDATE_OR_GOTO (this->name, loc, out);

	MAKE_REAL_PATH (real_path, this, loc->path);
	op_ret = lstat (real_path, &stbuf);				
	op_errno = errno;				
	if (op_ret != 0) {				
		gf_log (this->name, GF_LOG_ERROR,	
			"failed to lstat on %s (%s)",	
			real_path, strerror (op_errno));		
		goto out;				
	}						

	/* directory or symlink */
	op_ret = lchown (real_path, uid, gid);
	op_errno = errno; 
out:    
	frame->root->rsp_refs = NULL;  
	STACK_UNWIND (frame, op_ret, op_errno, &stbuf);

	return 0;
}/* bdb_chown */


int32_t 
bdb_truncate (call_frame_t *frame,
              xlator_t *this,
              loc_t *loc,
              off_t offset)
{
	int32_t     op_ret     = -1;
	int32_t     op_errno   = EINVAL;
	char       *real_path  = NULL;
	struct stat stbuf      = {0,};
	char       *db_path    = NULL;
	bctx_t     *bctx       = NULL;
	char       *key_string = NULL;

	GF_VALIDATE_OR_GOTO ("bdb", frame, out);
	GF_VALIDATE_OR_GOTO ("bdb", this, out);
	GF_VALIDATE_OR_GOTO (this->name, loc, out);

	bctx = bctx_parent (B_TABLE(this), loc->path);
	op_errno = ENOENT;
	GF_VALIDATE_OR_GOTO (this->name, bctx, out);

	MAKE_REAL_PATH (real_path, this, loc->path);
	MAKE_KEY_FROM_PATH (key_string, loc->path);
    
	/* now truncate */
	MAKE_REAL_PATH_TO_STORAGE_DB (db_path, this, bctx->directory);
	op_ret = lstat (db_path, &stbuf);				
	op_errno = errno;				
	if (op_ret != 0) {				
		gf_log (this->name, GF_LOG_ERROR,	
			"failed to lstat on %s (%s)",	
			db_path, strerror (op_errno));		
		goto out;				
	}						

	if (loc->inode->ino) {
		stbuf.st_ino = loc->inode->ino;
	}else {
		stbuf.st_ino = bdb_inode_transform (stbuf.st_ino, bctx);
	}
    
	op_ret = bdb_db_put (bctx, NULL, key_string, NULL, 0, 1, 0);
	if (op_ret == -1) {
		gf_log (this->name,
			GF_LOG_DEBUG,
			"failed to do bdb_db_put: %s", 
			db_strerror (op_ret));
		op_ret = -1;
		op_errno = EINVAL; /* TODO: better errno */
	} 

out:
	if (bctx) {
		/* NOTE: bctx_unref always returns success, 
		 * see description of bctx_unref for more details */
		bctx_unref (bctx);
	}

	frame->root->rsp_refs = NULL;
	STACK_UNWIND (frame, op_ret, op_errno, &stbuf);
  
	return 0;
}/* bdb_truncate */


int32_t 
bdb_utimens (call_frame_t *frame,
             xlator_t *this,
             loc_t *loc,
             struct timespec ts[2])
{
	int32_t     op_ret    = -1;
	int32_t     op_errno  = EPERM;
	char       *real_path = NULL;
	struct stat stbuf     = {0,};
	struct timeval tv[2] = {{0,},};
  
	GF_VALIDATE_OR_GOTO ("bdb", frame, out);
	GF_VALIDATE_OR_GOTO ("bdb", this, out);
	GF_VALIDATE_OR_GOTO (this->name, loc, out);

	MAKE_REAL_PATH (real_path, this, loc->path);
	op_ret = lstat (real_path, &stbuf);				
	op_errno = errno;				
	if (op_ret != 0) {				
		op_errno = EPERM;
		gf_log (this->name, GF_LOG_ERROR,	
			"failed to lstat on %s (%s)",	
			real_path, strerror (op_errno));		
		goto out;				
	}						

	/* directory or symlink */
	tv[0].tv_sec = ts[0].tv_sec;
	tv[0].tv_usec = ts[0].tv_nsec / 1000;
	tv[1].tv_sec = ts[1].tv_sec;
	tv[1].tv_usec = ts[1].tv_nsec / 1000;
    
	op_ret = lutimes (real_path, tv);
	if (op_ret == -1 && errno == ENOSYS) {
		op_ret = utimes (real_path, tv);
	}
	op_errno = errno;
	if (op_ret == -1) {
		gf_log (this->name, 
			GF_LOG_WARNING, 
			"utimes on %s failed: %s", 
			loc->path, strerror (op_errno));
		goto out;
	}

	op_ret = lstat (real_path, &stbuf);				
	op_errno = errno;				
	if (op_ret != 0) {				
		gf_log (this->name, GF_LOG_ERROR,	
			"failed to lstat on %s (%s)",	
			real_path, strerror (op_errno));		
		goto out;				
	}						

	stbuf.st_ino = loc->inode->ino;
    
out:  
	frame->root->rsp_refs = NULL;
	STACK_UNWIND (frame, op_ret, op_errno, &stbuf);
  
	return 0;
}/* bdb_utimens */

int32_t 
bdb_statfs (call_frame_t *frame,
            xlator_t *this,
            loc_t *loc)

{
	int32_t        op_ret    = -1;
	int32_t        op_errno  = EINVAL;
	char          *real_path = NULL;
	struct statvfs buf       = {0, };

	GF_VALIDATE_OR_GOTO ("bdb", frame, out);
	GF_VALIDATE_OR_GOTO ("bdb", this, out);
	GF_VALIDATE_OR_GOTO (this->name, loc, out);

	MAKE_REAL_PATH (real_path, this, loc->path);

	op_ret = statvfs (real_path, &buf);
	op_errno = errno;
out:
	frame->root->rsp_refs = NULL;
	STACK_UNWIND (frame, op_ret, op_errno, &buf);
	return 0;
}/* bdb_statfs */

static int gf_bdb_xattr_log;

/* bdb_setxattr - set extended attributes.
 *
 * bdb allows setxattr operation only on directories. 
 *    bdb reservers 'glusterfs.file.<attribute-name>' to operate on the content of the files 
 * under the specified directory. 'glusterfs.file.<attribute-name>' transforms to contents of 
 * file of name '<attribute-name>' under specified directory.
 *
 * @frame: call frame.
 * @this:  xlator_t of this instance of bdb xlator.
 * @loc:   loc_t specifying the file to operate upon.
 * @dict:  list of extended attributes to set on @loc.
 * @flags: can be XATTR_REPLACE (replace an existing extended attribute only if it exists) or
 *         XATTR_CREATE (create an extended attribute only if it doesn't already exist).
 *
 *
 */
int32_t 
bdb_setxattr (call_frame_t *frame,
              xlator_t *this,
              loc_t *loc,
              dict_t *dict,
              int flags)
{
	int32_t      op_ret = -1;
	int32_t      op_errno = EINVAL;
	data_pair_t *trav = dict->members_list;
	bctx_t      *bctx = NULL;
	char        *real_path = NULL;
	char        *key = NULL;

	GF_VALIDATE_OR_GOTO ("bdb", frame, out);
	GF_VALIDATE_OR_GOTO ("bdb", this, out);
	GF_VALIDATE_OR_GOTO (this->name, loc, out);
	GF_VALIDATE_OR_GOTO (this->name, dict, out);

	MAKE_REAL_PATH (real_path, this, loc->path);
	if (!S_ISDIR (loc->inode->st_mode)) {
		op_ret   = -1;
		op_errno = EPERM;
		goto out;
	}

	while (trav) {
		if (GF_FILE_CONTENT_REQUEST(trav->key) ) {
			bctx = bctx_lookup (B_TABLE(this), loc->path);
			op_errno = EINVAL;
			GF_VALIDATE_OR_GOTO (this->name, bctx, out);

			key = &(trav->key[15]);

			if (flags & XATTR_REPLACE) {
				/* replace only if previously exists, otherwise error out */
				op_ret = bdb_db_get (bctx, NULL, key,
						     NULL, 0, 0);
				if (op_ret == -1) {
					/* key doesn't exist in database */
					gf_log (this->name,
						GF_LOG_DEBUG,
						"cannot XATTR_REPLACE, xattr %s doesn't exist "
						"on path %s", key, loc->path);
					op_ret = -1;
					op_errno = ENOENT;
					break;
				} 
				op_ret = bdb_db_put (bctx, NULL, 
						     key, trav->value->data, 
						     trav->value->len, 
						     op_ret, BDB_TRUNCATE_RECORD);
				if (op_ret != 0) {
					op_ret   = -1;
					op_errno = EINVAL;
					break;
				} 
			} else {
				/* fresh create */
				op_ret = bdb_db_put (bctx, NULL, key, 
						     trav->value->data, 
						     trav->value->len, 
						     0, 0);
				if (op_ret != 0) {
					op_ret   = -1;
					op_errno = EINVAL;
					break;
				} else {
					op_ret = 0;
					op_errno = 0;
				} /* if(op_ret!=0)...else */
			} /* if(flags&XATTR_REPLACE)...else */
			if (bctx) {
				/* NOTE: bctx_unref always returns success, 
				 * see description of bctx_unref for more details */
				bctx_unref (bctx);
			}
		} else {
			/* do plain setxattr */
			op_ret = lsetxattr (real_path, 
					    trav->key, 
					    trav->value->data, 
					    trav->value->len, 
					    flags);
			op_errno = errno;
			if ((op_ret == -1) && (op_errno != ENOENT)) {
				if (op_errno == ENOTSUP) {
					gf_bdb_xattr_log++;
					if (!(gf_bdb_xattr_log % GF_UNIVERSAL_ANSWER)) {
						gf_log (this->name, GF_LOG_WARNING, 
							"Extended Attributes support not present."\
							"Please check");
					}
				} else {
					gf_log (this->name, GF_LOG_DEBUG, 
						"setxattr failed on %s (%s)", 
						loc->path, strerror (op_errno));
				}
				break;
			}
		} /* if(GF_FILE_CONTENT_REQUEST())...else */
		trav = trav->next;
	}/* while(trav) */
out:
	frame->root->rsp_refs = NULL;
	
	STACK_UNWIND (frame, op_ret, op_errno);
	return 0;  
}/* bdb_setxattr */


/* bdb_gettxattr - get extended attributes.
 *
 * bdb allows getxattr operation only on directories. 
 * bdb_getxattr retrieves the whole content of the file, when glusterfs.file.<attribute-name> 
 * is specified. 
 *
 * @frame: call frame.
 * @this:  xlator_t of this instance of bdb xlator.
 * @loc:   loc_t specifying the file to operate upon.
 * @name:  name of extended attributes to get for @loc.
 *
 * NOTE: see description of bdb_setxattr for details on how
 *     'glusterfs.file.<attribute-name>' is handles by bdb.
 */
int32_t 
bdb_getxattr (call_frame_t *frame,
              xlator_t *this,
              loc_t *loc,
              const char *name)
{
	int32_t op_ret         = 0; 
	int32_t op_errno       = 0;
	dict_t *dict           = NULL;
	bctx_t *bctx           = NULL; 
	char   *buf            = NULL;
	char   *key_string     = NULL;
	int32_t list_offset    = 0;
	size_t  size           = 0;
	size_t  remaining_size = 0;
	char   *real_path      = NULL;
	char    key[1024]      = {0,};
	char   *value          = NULL;
	char   *list           = NULL;
	
	GF_VALIDATE_OR_GOTO ("bdb", frame, out);
	GF_VALIDATE_OR_GOTO ("bdb", this, out);
	GF_VALIDATE_OR_GOTO (this->name, loc, out);
	GF_VALIDATE_OR_GOTO (this->name, name, out);

	dict = get_new_dict ();
	GF_VALIDATE_OR_GOTO (this->name, dict, out);

	if (!S_ISDIR (loc->inode->st_mode)) {
		gf_log (this->name,
			GF_LOG_DEBUG,
			"operation not permitted on a non-directory file: %s", loc->path);
		op_ret   = -1;
		op_errno = ENODATA;
		goto out;
	}

	if (name && GF_FILE_CONTENT_REQUEST(name)) {
		bctx = bctx_lookup (B_TABLE(this), loc->path);
		op_errno = EINVAL;
		GF_VALIDATE_OR_GOTO (this->name, bctx, out);

		key_string = (char *)&(name[15]);

		op_ret = bdb_db_get (bctx, NULL, key_string, &buf, 0, 0);
		if (op_ret == -1) {
			gf_log (this->name,
				GF_LOG_DEBUG,
				"failed to db get on directory: %s for key: %s", 
				bctx->directory, name);
			op_ret   = -1;
			op_errno = ENODATA;
			goto out;
		} 
		
		dict_set (dict, (char *)name, data_from_dynptr (buf, op_ret));
	} else {
		MAKE_REAL_PATH (real_path, this, loc->path);
		size = llistxattr (real_path, NULL, 0);
		op_errno = errno;
		if (size <= 0) {
			/* There are no extended attributes, send an empty dictionary */
			if (size == -1 && op_errno != ENODATA) {
				if (op_errno == ENOTSUP) {
					gf_bdb_xattr_log++;
					if (!(gf_bdb_xattr_log % GF_UNIVERSAL_ANSWER)) 
						gf_log (this->name, 
							GF_LOG_WARNING, 
							"Extended Attributes support not present."\
							"Please check");
				} else {
					gf_log (this->name, 
						GF_LOG_WARNING, 
						"llistxattr failed on %s (%s)", 
						loc->path, strerror (op_errno));
				}
			}
			op_ret = -1;
			op_errno = ENODATA;
		} else {
			list = alloca (size + 1);
			op_errno = ENOMEM;
			GF_VALIDATE_OR_GOTO (this->name, list, out);

			size = llistxattr (real_path, list, size);
			op_ret = size;
			op_errno = errno;
			if (size == -1) {
				gf_log (this->name,
					GF_LOG_ERROR,
					"llistxattr failed on %s (%s)",
					loc->path, strerror (errno));
				goto out;
			}
			remaining_size = size;
			list_offset = 0;
			while (remaining_size > 0) {
				if(*(list+list_offset) == '\0')
					break;
				strcpy (key, list + list_offset);
				op_ret = lgetxattr (real_path, key, NULL, 0);
				if (op_ret == -1)
					break;
				value = calloc (op_ret + 1, sizeof(char));
				GF_VALIDATE_OR_GOTO (this->name, value, out);

				op_ret = lgetxattr (real_path, key, value, op_ret);
				if (op_ret == -1)
					break;
				value [op_ret] = '\0';
				dict_set (dict, key, data_from_dynptr (value, op_ret));
				remaining_size -= strlen (key) + 1;
				list_offset += strlen (key) + 1;
			} /* while(remaining_size>0) */
		} /* if(size <= 0)...else */
	} /* if(name...)...else */

out:
	if(bctx) {
		/* NOTE: bctx_unref always returns success, 
		 * see description of bctx_unref for more details */
		bctx_unref (bctx);
	}

	if (dict)
		dict_ref (dict);

	STACK_UNWIND (frame, op_ret, op_errno, dict);

	if (dict)
		dict_unref (dict);
  
	return 0;
}/* bdb_getxattr */


int32_t 
bdb_removexattr (call_frame_t *frame,
                 xlator_t *this,
                 loc_t *loc,
                 const char *name)
{
	int32_t op_ret    = -1; 
	int32_t op_errno  = EINVAL;
	bctx_t *bctx      = NULL;
	char   *real_path = NULL;
	
	GF_VALIDATE_OR_GOTO ("bdb", frame, out);
	GF_VALIDATE_OR_GOTO ("bdb", this, out);
	GF_VALIDATE_OR_GOTO (this->name, loc, out);
	GF_VALIDATE_OR_GOTO (this->name, name, out);

	if (!S_ISDIR(loc->inode->st_mode)) {	
		gf_log (this->name,
			GF_LOG_WARNING,
			"operation not permitted on non-directory files");
		op_ret = -1;
		op_errno = EPERM;
		goto out;
	} 

	if (GF_FILE_CONTENT_REQUEST(name)) {
		bctx = bctx_lookup (B_TABLE(this), loc->path);
		op_errno = EINVAL;
		GF_VALIDATE_OR_GOTO (this->name, bctx, out);

		op_ret = bdb_db_del (bctx, NULL, name);
      		if (op_ret == -1) {
			gf_log (this->name,
				GF_LOG_ERROR,
				"failed to delete %s from db of %s directory", 
				name, loc->path);
			op_errno = EINVAL; /* TODO: errno */
			goto out;
		} 
	} else {
		MAKE_REAL_PATH(real_path, this, loc->path);
		op_ret = lremovexattr (real_path, name);
		op_errno = errno;
		if (op_ret == -1) {
			if (op_errno == ENOTSUP) {
				gf_bdb_xattr_log++;
				if (!(gf_bdb_xattr_log % GF_UNIVERSAL_ANSWER)) 
					gf_log (this->name, GF_LOG_WARNING, 
						"Extended Attributes support not present."
						"Please check");
			} else {
				gf_log (this->name, 
					GF_LOG_WARNING, 
					"%s: %s", 
					loc->path, strerror (op_errno));
			}
		} /* if(op_ret == -1) */
	} /* if (GF_FILE_CONTENT_REQUEST(name))...else */

out:
	if (bctx) {
		/* NOTE: bctx_unref always returns success, 
		 * see description of bctx_unref for more details */
		bctx_unref (bctx);
	}

	frame->root->rsp_refs = NULL;  
	STACK_UNWIND (frame, op_ret, op_errno);
	return 0;
}/* bdb_removexattr */


int32_t 
bdb_fsyncdir (call_frame_t *frame,
	      xlator_t *this,
	      fd_t *fd,
	      int datasync)
{
	int32_t op_ret = -1;
	int32_t op_errno = EINVAL;
	struct bdb_fd *bfd = NULL;
	
	GF_VALIDATE_OR_GOTO ("bdb", frame, out);
	GF_VALIDATE_OR_GOTO ("bdb", this, out);
	GF_VALIDATE_OR_GOTO (this->name, fd, out);
	
	frame->root->rsp_refs = NULL;

	bfd = bdb_extract_bfd (fd, this->name);
	op_errno = EBADFD;
	GF_VALIDATE_OR_GOTO (this->name, bfd, out);

out:
	STACK_UNWIND (frame, op_ret, op_errno);

	return 0;
}/* bdb_fsycndir */


int32_t 
bdb_access (call_frame_t *frame,
	    xlator_t *this,
	    loc_t *loc,
	    int32_t mask)
{
	int32_t op_ret = -1;
	int32_t op_errno = EINVAL;
	char *real_path = NULL;

	GF_VALIDATE_OR_GOTO ("bdb", frame, out);
	GF_VALIDATE_OR_GOTO ("bdb", this, out);
	GF_VALIDATE_OR_GOTO (this->name, loc, out);
	
	MAKE_REAL_PATH (real_path, this, loc->path);

	op_ret = access (real_path, mask);
	op_errno = errno;
	/* TODO: implement for db entries */
out:
	frame->root->rsp_refs = NULL;  
	STACK_UNWIND (frame, op_ret, op_errno);
	return 0;
}/* bdb_access */


int32_t 
bdb_ftruncate (call_frame_t *frame,
	       xlator_t *this,
	       fd_t *fd,
	       off_t offset)
{
	int32_t op_ret = -1;
	int32_t op_errno = EPERM;
	struct stat buf = {0,};
	
	GF_VALIDATE_OR_GOTO ("bdb", frame, out);
	GF_VALIDATE_OR_GOTO ("bdb", this, out);
	GF_VALIDATE_OR_GOTO (this->name, fd, out);
	/* TODO: impelement */
out:	
	frame->root->rsp_refs = NULL;
	STACK_UNWIND (frame, op_ret, op_errno, &buf);

	return 0;
}

int32_t 
bdb_fchown (call_frame_t *frame,
            xlator_t *this,
            fd_t *fd,
            uid_t uid,
            gid_t gid)
{
	int32_t op_ret = -1;
	int32_t op_errno = EPERM;
	struct stat buf = {0,};

	GF_VALIDATE_OR_GOTO ("bdb", frame, out);
	GF_VALIDATE_OR_GOTO ("bdb", this, out);
	GF_VALIDATE_OR_GOTO (this->name, fd, out);
	
	/* TODO: implement */
out:	
	STACK_UNWIND (frame, op_ret, op_errno, &buf);

	return 0;
}


int32_t 
bdb_fchmod (call_frame_t *frame,
            xlator_t *this,
            fd_t *fd,
            mode_t mode)
{
	int32_t op_ret = -1;
	int32_t op_errno = EPERM;
	struct stat buf = {0,};

	GF_VALIDATE_OR_GOTO ("bdb", frame, out);
	GF_VALIDATE_OR_GOTO ("bdb", this, out);
	GF_VALIDATE_OR_GOTO (this->name, fd, out);
	
	/* TODO: impelement */
out:	
	frame->root->rsp_refs = NULL;  
	STACK_UNWIND (frame, op_ret, op_errno, &buf);

	return 0;
}

int32_t 
bdb_setdents (call_frame_t *frame,
              xlator_t *this,
              fd_t *fd,
              int32_t flags,
              dir_entry_t *entries,
              int32_t count)
{
	int32_t op_ret = -1, op_errno = EINVAL;
	char *entry_path = NULL;
	int32_t real_path_len = 0;
	int32_t entry_path_len = 0;
	int32_t ret = 0;
	struct bdb_dir *bfd = NULL;
	dir_entry_t *trav = NULL;

	GF_VALIDATE_OR_GOTO ("bdb", frame, out);
	GF_VALIDATE_OR_GOTO ("bdb", this, out);
	GF_VALIDATE_OR_GOTO (this->name, fd, out);
	GF_VALIDATE_OR_GOTO (this->name, entries, out);

	frame->root->rsp_refs = NULL;
	
	bfd = bdb_extract_bfd (fd, this->name);
	op_errno = EBADFD;
	GF_VALIDATE_OR_GOTO (this->name, bfd, out);

	real_path_len = strlen (bfd->path);
	entry_path_len = real_path_len + 256;
	entry_path = calloc (1, entry_path_len);
	GF_VALIDATE_OR_GOTO (this->name, entry_path, out);

	strcpy (entry_path, bfd->path);
	entry_path[real_path_len] = '/';
      
	trav = entries->next;
	while (trav) {
		char pathname[GF_PATH_MAX] = {0,};
		strcpy (pathname, entry_path);
		strcat (pathname, trav->name);
        
		if (S_ISDIR(trav->buf.st_mode)) {
			/* If the entry is directory, create it by calling 'mkdir'. If 
			 * directory is not present, it will be created, if its present, 
			 * no worries even if it fails.
			 */
			ret = mkdir (pathname, trav->buf.st_mode);
			if ((ret == -1) && (errno != EEXIST)) {
				gf_log (this->name,
					GF_LOG_ERROR,
					"failed to created directory %s: %s", 
					pathname, strerror(errno));
				goto loop;
			}

			gf_log (this->name, 
				GF_LOG_DEBUG, 
				"Creating directory %s with mode (0%o)", 
				pathname,
				trav->buf.st_mode);
			/* Change the mode 
			 * NOTE: setdents tries its best to restore the state
			 *       of storage. if chmod and chown fail, they can be
			 *       ignored now */
			ret = chmod (pathname, trav->buf.st_mode);
			if (ret != 0) {
				op_ret = -1;
				op_errno = errno;
				gf_log (this->name,
					GF_LOG_ERROR,
					"chmod failed on %s (%s)",
					pathname, strerror (errno));
				goto loop;
			}
			/* change the ownership */
			ret = chown (pathname, trav->buf.st_uid, trav->buf.st_gid);
			if (ret != 0) {
				op_ret = -1;
				op_errno = errno;
				gf_log (this->name,
					GF_LOG_ERROR,
					"chown failed on %s (%s)",
					pathname, strerror (errno));
				goto loop;
			}
		} else if ((flags == GF_SET_IF_NOT_PRESENT) || 
			   (flags != GF_SET_DIR_ONLY)) {
			/* Create a 0 byte file here */
			if (S_ISREG (trav->buf.st_mode)) {
				op_ret = bdb_db_put (bfd->ctx, NULL, 
						     trav->name, NULL, 0, 0, 0);
				if (op_ret != 0) {
					/* create successful */
					gf_log (this->name,
						GF_LOG_ERROR,
						"failed to create file %s",
						pathname);
				} /* if (!op_ret)...else */
			} else if (S_ISLNK (trav->buf.st_mode)) {
				/* TODO: impelement */;
			} else {
				gf_log (this->name,
					GF_LOG_ERROR,
					"storage/bdb allows to create regular files only"
					"file %s (mode = %d) cannot be created",
					pathname, trav->buf.st_mode);
			} /* if(S_ISREG())...else */
		} /* if(S_ISDIR())...else if */
	loop:
		/* consider the next entry */
		trav = trav->next;
	} /* while(trav) */

out:
	frame->root->rsp_refs = NULL;
	STACK_UNWIND (frame, op_ret, op_errno);
  
	FREE (entry_path);
	return 0;
}

int32_t 
bdb_fstat (call_frame_t *frame,
           xlator_t *this,
           fd_t *fd)
{
	int32_t        op_ret   = -1;
	int32_t        op_errno = EINVAL;
	struct stat    stbuf    = {0,};
	struct bdb_fd *bfd      = NULL;
	bctx_t        *bctx     = NULL;
	char          *db_path  = NULL;

	GF_VALIDATE_OR_GOTO ("bdb", frame, out);
	GF_VALIDATE_OR_GOTO ("bdb", this, out);
	GF_VALIDATE_OR_GOTO (this->name, fd, out);
	
	bfd      = bdb_extract_bfd (fd, this->name);
	op_errno = EBADFD;
	GF_VALIDATE_OR_GOTO (this->name, bfd, out);

	bctx = bfd->ctx;

	MAKE_REAL_PATH_TO_STORAGE_DB (db_path, this, bctx->directory);
	op_ret = lstat (db_path, &stbuf);				
	op_errno = errno;				
	if (op_ret != 0) {				
		gf_log (this->name, GF_LOG_ERROR,	
			"failed to lstat on %s (%s)",	
			db_path, strerror (op_errno));		
		goto out;				
	}						

	stbuf.st_ino = fd->inode->ino;
	stbuf.st_size = bdb_db_get (bctx, NULL, bfd->key, NULL, 0, 0);
	stbuf.st_blocks = BDB_COUNT_BLOCKS (stbuf.st_size, stbuf.st_blksize);

out:
	frame->root->rsp_refs = NULL;

	STACK_UNWIND (frame, op_ret, op_errno, &stbuf);
	return 0;
}


int32_t
bdb_readdir (call_frame_t *frame,
             xlator_t *this,
             fd_t *fd,
             size_t size,
             off_t off)
{
	struct bdb_dir *bfd        = NULL;
	int32_t         op_ret     = -1; 
	int32_t         op_errno   = EINVAL;
	size_t          filled     = 0;
	gf_dirent_t    *this_entry = NULL;
	gf_dirent_t     entries;
	struct dirent  *entry      = NULL;
	off_t           in_case    = 0;
	int32_t         this_size  = 0;
	DBC            *cursorp    = NULL;
	int32_t count = 0;

	GF_VALIDATE_OR_GOTO ("bdb", frame, out);
	GF_VALIDATE_OR_GOTO ("bdb", this, out);
	GF_VALIDATE_OR_GOTO (this->name, fd, out);

	INIT_LIST_HEAD (&entries.list);
	
	bfd = bdb_extract_bfd (fd, this->name);
	op_errno = EBADFD;
	GF_VALIDATE_OR_GOTO (this->name, bfd, out);

	op_errno = ENOMEM;

	while (filled <= size) {
		this_entry = NULL;
		entry      = NULL;
		in_case    = 0;
		this_size  = 0;
        
		in_case = telldir (bfd->dir);
		entry = readdir (bfd->dir);
		if (!entry)
			break;

		if (IS_BDB_PRIVATE_FILE(entry->d_name))
			continue;
		
		this_size = dirent_size (entry);
        
		if (this_size + filled > size) {
			seekdir (bfd->dir, in_case);
			break;
		}
		
		count++;

		this_entry = gf_dirent_for_name (entry->d_name);
		this_entry->d_ino = entry->d_ino;
          
		this_entry->d_off = -1;
          
		this_entry->d_type = entry->d_type;
		this_entry->d_len = entry->d_reclen;


		list_add (&this_entry->list, &entries.list);
          
		filled += this_size;
	}
	op_ret = filled;
	op_errno = 0;
	if (filled >= size) {
		goto out;
	}

	/* hungry kyaa? */
	op_ret = bdb_cursor_open (bfd->ctx, &cursorp);
	op_errno = EBADFD;
	GF_VALIDATE_OR_GOTO (this->name, (op_ret == 0), out);

	/* TODO: fix d_off, don't use bfd->offset. wrong method */
	if (strlen (bfd->offset)) {
		DBT key = {0,}, value = {0,};
		key.data = bfd->offset;
		key.size = strlen (bfd->offset);
		key.flags = DB_DBT_USERMEM;
		value.dlen = 0;
		value.doff = 0;
		value.flags = DB_DBT_PARTIAL;

		op_ret = bdb_cursor_get (cursorp, &key, &value, DB_SET);
		op_errno = EBADFD;
		GF_VALIDATE_OR_GOTO (this->name, (op_ret == 0), out);

	} else {
		/* first time or last time, do nothing */
	}

	while (filled <= size) {
		DBT key = {0,}, value = {0,};
            	this_entry = NULL;

		key.flags = DB_DBT_MALLOC;
		value.dlen = 0;
		value.doff = 0; 
		value.flags = DB_DBT_PARTIAL;
		op_ret = bdb_cursor_get (cursorp, &key, &value, DB_NEXT);
            
		if (op_ret == DB_NOTFOUND) {
			/* we reached end of the directory */
			op_ret = 0;
			op_errno = 0;
			break;
		} else if (op_ret != 0) {
			gf_log (this->name,
				GF_LOG_DEBUG,
				"database error during readdir");
			op_ret = -1;
			op_errno = ENOENT;
			break;
		} /* if (op_ret == DB_NOTFOUND)...else if...else */

		if (key.data == NULL) {
			/* NOTE: currently ignore when we get key.data == NULL.
			 * TODO: we should not get key.data = NULL */
			gf_log (this->name,
				GF_LOG_DEBUG,
				"null key read from db");
			continue;
		}/* if(key.data)...else */
		count++;
		this_size = bdb_dirent_size (&key);
		if (this_size + filled > size)
			break;
		/* TODO - consider endianness here */
		this_entry = gf_dirent_for_name ((const char *)key.data);
		/* FIXME: bug, if someone is going to use ->d_ino */
		this_entry->d_ino = -1;
		this_entry->d_off = 0;
		this_entry->d_type = 0;
		this_entry->d_len = key.size;
                
		if (key.data) {
			strncpy (bfd->offset, key.data, key.size);
			bfd->offset [key.size] = '\0';
			free (key.data);
		}

		list_add (&this_entry->list, &entries.list);

		filled += this_size;
	}/* while */
	bdb_cursor_close (bfd->ctx, cursorp);
	op_ret = filled;
	op_errno = 0;
out:
	frame->root->rsp_refs = NULL;
	gf_log (this->name,
		GF_LOG_DEBUG,
		"read %d bytes for %d entries", filled, count);
	STACK_UNWIND (frame, count, op_errno, &entries);

	gf_dirent_free (&entries);
    
	return 0;
}


int32_t 
bdb_stats (call_frame_t *frame,
           xlator_t *this,
           int32_t flags)

{
	int32_t op_ret = 0;
	int32_t op_errno = 0;

	struct xlator_stats xlstats = {0, }, *stats = NULL; 
	struct statvfs buf;
	struct timeval tv;
	struct bdb_private *private = NULL;
	int64_t avg_read = 0;
	int64_t avg_write = 0;
	int64_t _time_ms = 0; 
	
	GF_VALIDATE_OR_GOTO ("bdb", frame, out);
	GF_VALIDATE_OR_GOTO ("bdb", this, out);

	private = (struct bdb_private *)(this->private);
	stats = &xlstats;
	
  	op_ret = statvfs (private->export_path, &buf);				
	op_errno = errno;				
	if (op_ret != 0) {				
		gf_log (this->name, GF_LOG_ERROR,	
			"failed to statvfs on %s (%s)",	
			private->export_path, strerror (op_errno));		
		goto out;				
	}						

	stats->nr_files = private->stats.nr_files;
	stats->nr_clients = private->stats.nr_clients; /* client info is maintained at FSd */
	stats->free_disk = buf.f_bfree * buf.f_bsize; /* Number of Free block in the filesystem. */
	stats->total_disk_size = buf.f_blocks * buf.f_bsize; /* */
	stats->disk_usage = (buf.f_blocks - buf.f_bavail) * buf.f_bsize;

	/* Calculate read and write usage */
	gettimeofday (&tv, NULL);
  
	/* Read */
	_time_ms = (tv.tv_sec - private->init_time.tv_sec) * 1000 +
		((tv.tv_usec - private->init_time.tv_usec) / 1000);

	avg_read = (_time_ms) ? (private->read_value / _time_ms) : 0; /* KBps */
	avg_write = (_time_ms) ? (private->write_value / _time_ms) : 0; /* KBps */
  
	_time_ms = (tv.tv_sec - private->prev_fetch_time.tv_sec) * 1000 +
		((tv.tv_usec - private->prev_fetch_time.tv_usec) / 1000);
	if (_time_ms && ((private->interval_read / _time_ms) > private->max_read)) {
		private->max_read = (private->interval_read / _time_ms);
	}
	if (_time_ms && ((private->interval_write / _time_ms) > private->max_write)) {
		private->max_write = private->interval_write / _time_ms;
	}

	stats->read_usage = avg_read / private->max_read;
	stats->write_usage = avg_write / private->max_write;

	gettimeofday (&(private->prev_fetch_time), NULL);
	private->interval_read = 0;
	private->interval_write = 0;

out:
	frame->root->rsp_refs = NULL;
	STACK_UNWIND (frame, op_ret, op_errno, stats);
	return 0;
}


int32_t 
bdb_inodelk (call_frame_t *frame, xlator_t *this,
	     loc_t *loc, int32_t cmd, struct flock *lock)
{
        frame->root->rsp_refs = NULL;

	gf_log (this->name, GF_LOG_CRITICAL,
		"\"features/posix-locks\" translator is not loaded. You need to use it for proper functioning of GlusterFS");

        STACK_UNWIND (frame, -1, ENOSYS);
        return 0;
}


int32_t 
bdb_finodelk (call_frame_t *frame, xlator_t *this,
	      fd_t *fd, int32_t cmd, struct flock *lock)
{
        frame->root->rsp_refs = NULL;

	gf_log (this->name, GF_LOG_CRITICAL,
		"\"features/posix-locks\" translator is not loaded. You need to use it for proper functioning of GlusterFS");

        STACK_UNWIND (frame, -1, ENOSYS);
        return 0;
}


int32_t 
bdb_entrylk (call_frame_t *frame, xlator_t *this,
	     loc_t *loc, const char *basename, gf_dir_lk_cmd cmd, 
	     gf_dir_lk_type type)
{
        frame->root->rsp_refs = NULL;

	gf_log (this->name, GF_LOG_CRITICAL,
		"\"features/posix-locks\" translator is not loaded. You need to use it for proper functioning of GlusterFS");

        STACK_UNWIND (frame, -1, ENOSYS);
        return 0;
}


int32_t 
bdb_fentrylk (call_frame_t *frame, xlator_t *this,
	      fd_t *fd, const char *basename, gf_dir_lk_cmd cmd, 
	      gf_dir_lk_type type)
{
        frame->root->rsp_refs = NULL;

	gf_log (this->name, GF_LOG_CRITICAL,
		"\"features/posix-locks\" translator is not loaded. You need to use it for proper functioning of GlusterFS");

        STACK_UNWIND (frame, -1, ENOSYS);
        return 0;
}


int32_t 
bdb_checksum (call_frame_t *frame,
              xlator_t *this,
              loc_t *loc,
              int32_t flag)
{
	char          *real_path = NULL;
	DIR           *dir       = NULL;
	struct dirent *dirent    = NULL;
	uint8_t        file_checksum[GF_FILENAME_MAX] = {0,};
	uint8_t        dir_checksum[GF_FILENAME_MAX]  = {0,};
	int32_t        op_ret   = -1;
	int32_t        op_errno = EINVAL;
	int32_t        i = 0, length = 0;
	bctx_t        *bctx    = NULL;
	DBC           *cursorp = NULL;
	char          *data    = NULL;

	GF_VALIDATE_OR_GOTO ("bdb", frame, out);
	GF_VALIDATE_OR_GOTO ("bdb", this, out);
	GF_VALIDATE_OR_GOTO (this->name, loc, out);

	MAKE_REAL_PATH (real_path, this, loc->path);

	{
		dir = opendir (real_path);
		op_errno = errno;
		GF_VALIDATE_OR_GOTO (this->name, dir, out);
		while ((dirent = readdir (dir))) {
			if (!dirent)
				break;
        
			if (IS_BDB_PRIVATE_FILE(dirent->d_name))
				continue;

			length = strlen (dirent->d_name);
			for (i = 0; i < length; i++)
				dir_checksum[i] ^= dirent->d_name[i];
		} /* while((dirent...)) */
		closedir (dir);
	}

	{
		bctx = bctx_lookup (B_TABLE(this), (char *)loc->path);
		op_errno = EINVAL;
		GF_VALIDATE_OR_GOTO (this->name, bctx, out);

		op_ret = bdb_cursor_open (bctx, &cursorp);
		op_errno = EINVAL;
		GF_VALIDATE_OR_GOTO (this->name, (op_ret == 0), out);

		while (1) {
			DBT key = {0,}, value = {0,};
          
			key.flags = DB_DBT_MALLOC;
			value.doff = 0;
			value.dlen = 0;
			op_ret = bdb_cursor_get (cursorp, &key, &value, DB_NEXT);
          
			if (op_ret == DB_NOTFOUND) {
				gf_log (this->name,
					GF_LOG_DEBUG,
					"end of list of key/value pair in db for "
					"directory: %s", bctx->directory);
				op_ret = 0;
				op_errno = 0;
				break;
			} else if (op_ret == 0){
				/* successfully read */
				data = key.data;
				length = key.size;
				for (i = 0; i < length; i++)
					file_checksum[i] ^= data[i];
            
				free (key.data);
			} else {
				gf_log (this->name,
					GF_LOG_ERROR,
					"failed to do cursor get for directory %s: %s", 
					bctx->directory, db_strerror (op_ret));
				op_ret = -1;
				op_errno = ENOENT;
				break;
			}/* if(op_ret == DB_NOTFOUND)...else if...else */
		} /* while(1) */
		bdb_cursor_close (bctx, cursorp);
	}
out:
	if (bctx) {
		/* NOTE: bctx_unref always returns success, 
		 * see description of bctx_unref for more details */
		bctx_unref (bctx);
	}

	frame->root->rsp_refs = NULL;
	STACK_UNWIND (frame, op_ret, op_errno, file_checksum, dir_checksum);

	return 0;
}

/**
 * notify - when parent sends PARENT_UP, send CHILD_UP event from here
 */
int32_t
notify (xlator_t *this,
        int32_t event,
        void *data,
        ...)
{
	switch (event)
	{
	case GF_EVENT_PARENT_UP:
	{
		/* Tell the parent that bdb xlator is up */
		assert ((this->private != NULL) && 
			(BDB_ENV(this) != NULL));
		default_notify (this, GF_EVENT_CHILD_UP, data);
	}
	break;
	default:
		/* */
		break;
	}
	return 0;
}



/**
 * init - 
 */
int32_t 
init (xlator_t *this)
{
	int32_t             ret = -1;
	struct stat         buf = {0,};
	struct bdb_private *_private = NULL;
	data_t             *directory = NULL;
	bctx_t             *bctx = NULL;

	GF_VALIDATE_OR_GOTO ("bdb", this, out);

	_private = calloc (1, sizeof (*_private));
	GF_VALIDATE_OR_GOTO (this->name, _private, out);

	if (this->children) {
		gf_log (this->name,
			GF_LOG_ERROR,
			"FATAL: storage/bdb cannot have subvolumes");
		FREE (_private);
		goto out;;
	}

	directory = dict_get (this->options, "directory");
	if (!directory) {
		gf_log (this->name, GF_LOG_ERROR,
			"export directory not specified in spec file");
		FREE (_private);
		goto out;
	} 
	umask (000); // umask `masking' is done at the client side
	if (mkdir (directory->data, 0777) == 0) {
		gf_log (this->name, GF_LOG_WARNING,
			"directory specified not exists, created");
	}
  
	/* Check whether the specified directory exists, if not create it. */
	ret = stat (directory->data, &buf);
	if ((ret != 0) || !S_ISDIR (buf.st_mode)) {
		gf_log (this->name, GF_LOG_ERROR, 
			"Specified directory doesn't exists, Exiting");
		FREE (_private);
		goto out;
	} else {
		ret = 0;
	}


	_private->export_path = strdup (directory->data);
	_private->export_path_length = strlen (_private->export_path);

	{
		/* Stats related variables */
		gettimeofday (&_private->init_time, NULL);
		gettimeofday (&_private->prev_fetch_time, NULL);
		_private->max_read = 1;
		_private->max_write = 1;
	}

	this->private = (void *)_private;
	{
		ret = bdb_db_init (this, this->options);
    
		if (ret == -1){
			gf_log (this->name,
				GF_LOG_DEBUG,
				"failed to initialize database");
			goto out;
		} else {
			bctx = bctx_lookup (_private->b_table, "/");
			/* NOTE: we are not doing bctx_unref() for root bctx, 
			 *      let it remain in active list forever */
			if (!bctx) {
				gf_log (this->name,
					GF_LOG_ERROR,
					"failed to allocate memory for root (/) bctx: out of memory");
				goto out;
			} else {
				ret = 0;
			}
		}
	}
out:
	return ret;
}

void 
bctx_cleanup (struct list_head *head)
{
	bctx_t *trav    = NULL;
	bctx_t *tmp     = NULL;
	DB     *storage = NULL;

	list_for_each_entry_safe (trav, tmp, head, list) {
		LOCK (&trav->lock);
		storage = trav->dbp;
		trav->dbp = NULL;
		list_del_init (&trav->list);
		UNLOCK (&trav->lock);
    
		if (storage) {
			storage->close (storage, 0);
			storage = NULL;
		}
	}
  	return;
}

void
fini (xlator_t *this)
{
	struct bdb_private *private = NULL; 
	int32_t             idx     = 0;
	int32_t             ret     = 0;
	private = this->private;

	if (B_TABLE(this)) {
		/* close all the dbs from lru list */
		bctx_cleanup (&(B_TABLE(this)->b_lru));
		for (idx = 0; idx < B_TABLE(this)->hash_size; idx++)
			bctx_cleanup (&(B_TABLE(this)->b_hash[idx]));
    
		if (BDB_ENV(this)) {
			LOCK (&private->active_lock);
			private->active = 0;
			UNLOCK (&private->active_lock);
       
			ret = pthread_join (private->checkpoint_thread, NULL);
			if (ret != 0) {
				gf_log (this->name,
					GF_LOG_CRITICAL,
					"failed to join checkpoint thread");
			}

			/* TODO: pick each of the 'struct bctx' from private->b_hash
			 * and close all the databases that are open */
			BDB_ENV(this)->close (BDB_ENV(this), 0);
		} else {
			/* impossible to reach here */
		}

		FREE (B_TABLE(this));
	}
	FREE (private);
	return;
}

struct xlator_mops mops = {
	.stats    = bdb_stats,
	.lock     = mop_lock_impl,
	.unlock   = mop_unlock_impl,
};

struct xlator_fops fops = {
	.lookup      = bdb_lookup,
	.stat        = bdb_stat,
	.opendir     = bdb_opendir,
	.readdir     = bdb_readdir,
	.readlink    = bdb_readlink,
	.mknod       = bdb_mknod,
	.mkdir       = bdb_mkdir,
	.unlink      = bdb_unlink,
	.rmelem      = bdb_rmelem,
	.rmdir       = bdb_rmdir,
	.symlink     = bdb_symlink,
	.rename      = bdb_rename,
	.link        = bdb_link,
	.chmod       = bdb_chmod,
	.chown       = bdb_chown,
	.truncate    = bdb_truncate,
	.utimens     = bdb_utimens,
	.create      = bdb_create,
	.open        = bdb_open,
	.readv       = bdb_readv,
	.writev      = bdb_writev,
	.statfs      = bdb_statfs,
	.flush       = bdb_flush,
	.fsync       = bdb_fsync,
	.setxattr    = bdb_setxattr,
	.getxattr    = bdb_getxattr,
	.removexattr = bdb_removexattr,
	.fsyncdir    = bdb_fsyncdir,
	.access      = bdb_access,
	.ftruncate   = bdb_ftruncate,
	.fstat       = bdb_fstat,
	.lk          = bdb_lk,
	.inodelk     = bdb_inodelk,
	.finodelk    = bdb_finodelk,
	.entrylk     = bdb_entrylk,
	.fentrylk    = bdb_fentrylk,
	.fchown      = bdb_fchown,
	.fchmod      = bdb_fchmod,
	.setdents    = bdb_setdents,
	.getdents    = bdb_getdents,
	.checksum    = bdb_checksum,
};

struct xlator_cbks cbks = {
	.release    = bdb_release,
	.releasedir = bdb_releasedir
};

struct xlator_options options[] = {
	{ "directory", GF_OPTION_TYPE_PATH, 0, },
	{ "logdir", GF_OPTION_TYPE_PATH, 0, },
	{ "errfile", GF_OPTION_TYPE_PATH, 0, },
	{ "dir-mode", GF_OPTION_TYPE_ANY, 0, }, /* base 8 number */
	{ "file-mode", GF_OPTION_TYPE_ANY, 0, }, /* base 8 number */
	{ "page-size", GF_OPTION_TYPE_SIZET, -1, },
	{ "lru-limit", GF_OPTION_TYPE_INT, -1, },
	{ "lock-timeout", GF_OPTION_TYPE_TIME, 0, },
	{ "checkpoint-timeout", GF_OPTION_TYPE_TIME, 0, },
	{ "transaction-timeout", GF_OPTION_TYPE_TIME, 0, },
	{ "mode", GF_OPTION_TYPE_BOOL, 0, }, /* Should be 'cache' ?? */
	{ "access-mode", GF_OPTION_TYPE_STR, 0, 0, 0, "btree"},
	{ NULL, 0, }
};
