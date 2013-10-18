/*
  Copyright (c) 2013 Red Hat, Inc. <http://www.redhat.com>
  This file is part of GlusterFS.

  This file is licensed to you under your choice of the GNU Lesser
  General Public License, version 3 or any later version (LGPLv3 or
  later), or the GNU General Public License, version 2 (GPLv2), in all
  cases as published by the Free Software Foundation.
*/

#ifndef __QEMU_BLOCK_H
#define __QEMU_BLOCK_H

#include "syncop.h"
#include "call-stub.h"
#include "block/block_int.h"
#include "monitor/monitor.h"

/* QB_XATTR_KEY_FMT is the on-disk xattr stored in the inode which
   indicates that the file must be "interpreted" by the block format
   logic. The value of the key is of the pattern:

   "format:virtual_size"

   e.g

   "qcow2:20GB" or "qed:100GB"

   The format and virtual size are colon separated. The format is
   a case sensitive string which qemu recognizes. virtual_size is
   specified as a size which glusterfs recognizes as size (i.e.,
   value accepted by gf_string2bytesize())
*/
#define QB_XATTR_KEY_FMT "trusted.glusterfs.%s.format"

#define QB_XATTR_KEY_MAX 64

#define QB_XATTR_VAL_MAX 64


typedef struct qb_inode {
	char fmt[QB_XATTR_VAL_MAX]; /* this is only the format, not "format:size" */
	size_t size; /* virtual size in bytes */
	BlockDriverState *bs;
	int refcnt;
	uuid_t backing_gfid;
	char *backing_fname;
} qb_inode_t;


typedef struct qb_conf {
	Monitor *mon;
	struct syncenv *env;
	char qb_xattr_key[QB_XATTR_KEY_MAX];
	char *default_password;
	inode_t *root_inode;
} qb_conf_t;


typedef struct qb_local {
	call_frame_t      *frame; /* backpointer */
	call_stub_t       *stub;
	inode_t           *inode;
	fd_t              *fd;
	char               fmt[QB_XATTR_VAL_MAX+1];
	char               name[256];
	synctask_fn_t      synctask_fn;
	struct list_head   list;
} qb_local_t;

void qb_local_free (xlator_t *this, qb_local_t *local);
int qb_coroutine (call_frame_t *frame, synctask_fn_t fn);
inode_t *qb_inode_from_filename (const char *filename);
int qb_inode_to_filename (inode_t *inode, char *filename, int size);
int qb_format_extract (xlator_t *this, char *format, inode_t *inode);

qb_inode_t *qb_inode_ctx_get (xlator_t *this, inode_t *inode);

#define QB_STACK_UNWIND(typ, frame, args ...) do {	\
	qb_local_t *__local = frame->local;		\
	xlator_t *__this = frame->this;			\
							\
	frame->local = NULL;				\
	STACK_UNWIND_STRICT (typ, frame, args);		\
	if (__local)					\
		qb_local_free (__this, __local);	\
	} while (0)

#define QB_STUB_UNWIND(stub, op_ret, op_errno) do {	\
	qb_local_t *__local = stub->frame->local;	\
	xlator_t *__this = stub->frame->this;		\
							\
	stub->frame->local = NULL;			\
	call_unwind_error (stub, op_ret, op_errno);	\
	if (__local)					\
		qb_local_free (__this, __local);	\
	} while (0)

#define QB_STUB_RESUME(stub_errno) do {			\
	qb_local_t *__local = stub->frame->local;	\
	xlator_t *__this = stub->frame->this;		\
							\
	stub->frame->local = NULL;			\
	call_resume (stub);				\
	if (__local)					\
		qb_local_free (__this, __local);	\
	} while (0)

#endif /* !__QEMU_BLOCK_H */
