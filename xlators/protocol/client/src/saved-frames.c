/*
  Copyright (c) 2008-2009 Z RESEARCH, Inc. <http://www.zresearch.com>
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


#include "saved-frames.h"
#include "common-utils.h"
#include "protocol.h"
#include "xlator.h"



struct saved_frames *
saved_frames_new (void)
{
	struct saved_frames *saved_frames = NULL;

	saved_frames = CALLOC (sizeof (*saved_frames), 1);
	if (!saved_frames) {
		return NULL;
	}

	INIT_LIST_HEAD (&saved_frames->fops.list);
	INIT_LIST_HEAD (&saved_frames->mops.list);
	INIT_LIST_HEAD (&saved_frames->cbks.list);

	return saved_frames;
}


struct saved_frame *
get_head_frame_for_type (struct saved_frames *frames, int8_t type)
{
	struct saved_frame *head_frame = NULL;

	switch (type) {
	case GF_OP_TYPE_FOP_REQUEST:
	case GF_OP_TYPE_FOP_REPLY:
		head_frame = &frames->fops;
		break;
	case GF_OP_TYPE_MOP_REQUEST:
	case GF_OP_TYPE_MOP_REPLY:
		head_frame = &frames->mops;
		break;
	case GF_OP_TYPE_CBK_REQUEST:
	case GF_OP_TYPE_CBK_REPLY:
		head_frame = &frames->cbks;
		break;
	}

	return head_frame;
}


int
saved_frames_put (struct saved_frames *frames, call_frame_t *frame,
		  int32_t op, int8_t type, int64_t callid)
{
	struct saved_frame *saved_frame = NULL;
	struct saved_frame *head_frame = NULL;

	head_frame = get_head_frame_for_type (frames, type);

	saved_frame = CALLOC (sizeof (*saved_frame), 1);
	if (!saved_frame) {
		return -ENOMEM;
	}

	INIT_LIST_HEAD (&saved_frame->list);
	saved_frame->frame  = frame;
	saved_frame->op     = op;
	saved_frame->type   = type;
	saved_frame->callid = callid;

	gettimeofday (&saved_frame->saved_at, NULL);

	list_add_tail (&saved_frame->list, &head_frame->list);
	frames->count++;

	return 0;
}


call_frame_t *
saved_frames_get (struct saved_frames *frames, int32_t op,
		  int8_t type, int64_t callid)
{
	struct saved_frame *saved_frame = NULL;
	struct saved_frame *tmp = NULL;
	struct saved_frame *head_frame = NULL;
	call_frame_t       *frame = NULL;

	head_frame = get_head_frame_for_type (frames, type);

	list_for_each_entry (tmp, &head_frame->list, list) {
		if (tmp->callid == callid) {
			list_del_init (&tmp->list);
			frames->count--;
			saved_frame = tmp;
			break;
		}
	}

	if (saved_frame)
		frame = saved_frame->frame;

	FREE (saved_frame);

	return frame;
}

struct saved_frame *
saved_frames_get_timedout (struct saved_frames *frames, int8_t type, 
			   uint32_t timeout, struct timeval *current)
{
	struct saved_frame *bailout_frame = NULL, *tmp = NULL;
	struct saved_frame *head_frame = NULL;

	head_frame = get_head_frame_for_type (frames, type);

	if (!list_empty(&head_frame->list)) {
		tmp = list_entry (head_frame->list.next, typeof (*tmp), list);
		if ((tmp->saved_at.tv_sec + timeout) < current->tv_sec) {
			bailout_frame = tmp;
			list_del_init (&bailout_frame->list);
			frames->count--;
		}
	}

	return bailout_frame;
}

void
saved_frames_unwind (xlator_t *this, struct saved_frames *saved_frames,
		     struct saved_frame *head,
		     gf_op_t gf_ops[], char *gf_op_list[])
{
	struct saved_frame   *trav = NULL;
	struct saved_frame   *tmp = NULL;

	gf_hdr_common_t       hdr = {0, };
	call_frame_t         *frame = NULL;
	dict_t               *reply = NULL;

	reply = get_new_dict();
	dict_ref (reply);

	hdr.rsp.op_ret   = hton32 (-1);
	hdr.rsp.op_errno = hton32 (ENOTCONN);

	list_for_each_entry_safe (trav, tmp, &head->list, list) {
		gf_log (this->name, GF_LOG_ERROR,
			"forced unwinding frame type(%d) op(%s)",
			trav->type, gf_op_list[trav->op]);

		hdr.type = hton32 (trav->type);
		hdr.op   = hton32 (trav->op);

		frame = trav->frame;
		frame->root->rsp_refs = reply;

		saved_frames->count--;

		gf_ops[trav->op] (frame, &hdr, sizeof (hdr), NULL, 0);

		list_del_init (&trav->list);
		FREE (trav);
	}

	dict_unref (reply);
}


void
saved_frames_destroy (xlator_t *this, struct saved_frames *frames,
		      gf_op_t gf_fops[], gf_op_t gf_mops[], gf_op_t gf_cbks[])
{
	saved_frames_unwind (this, frames, &frames->fops, gf_fops, gf_fop_list);
	saved_frames_unwind (this, frames, &frames->mops, gf_mops, gf_mop_list);
	saved_frames_unwind (this, frames, &frames->cbks, gf_cbks, gf_cbk_list);

	FREE (frames);
}
