#!/usr/bin/python

import sys
from generator import ops, fop_subs, cbk_subs, generate

FAILURE_CBK_TEMPLATE = """
int32_t
default_@NAME@_failure_cbk (call_frame_t *frame, int32_t op_errno)
{
	STACK_UNWIND_STRICT (@NAME@, frame, -1, op_errno, @ERROR_ARGS@);
	return 0;
}
"""

CBK_RESUME_TEMPLATE = """
int32_t
default_@NAME@_cbk_resume (call_frame_t *frame, void *cookie, xlator_t *this,
			   int32_t op_ret, int32_t op_errno, @LONG_ARGS@)
{
	STACK_UNWIND_STRICT (@NAME@, frame, op_ret, op_errno,
			     @SHORT_ARGS@);
	return 0;
}
"""

CBK_TEMPLATE = """
int32_t
default_@NAME@_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
		    int32_t op_ret, int32_t op_errno, @LONG_ARGS@)
{
	STACK_UNWIND_STRICT (@NAME@, frame, op_ret, op_errno,
			     @SHORT_ARGS@);
	return 0;
}
"""

RESUME_TEMPLATE = """
int32_t
default_@NAME@_resume (call_frame_t *frame, xlator_t *this, @LONG_ARGS@)
{
	STACK_WIND (frame, default_@NAME@_cbk,
		    FIRST_CHILD(this), FIRST_CHILD(this)->fops->@NAME@,
		    @SHORT_ARGS@);
	return 0;
}
"""

FOP_TEMPLATE = """
int32_t
default_@NAME@ (
	call_frame_t *frame,
	xlator_t *this,
	@LONG_ARGS@)
{
	STACK_WIND_TAIL (frame,
			 FIRST_CHILD(this), FIRST_CHILD(this)->fops->@NAME@,
			 @SHORT_ARGS@);
	return 0;
}
"""

def gen_defaults ():
	for name in ops.iterkeys():
		print generate(FAILURE_CBK_TEMPLATE,name,cbk_subs)
	for name in ops.iterkeys():
		print generate(CBK_RESUME_TEMPLATE,name,cbk_subs)
	for name in ops.iterkeys():
		print generate(CBK_TEMPLATE,name,cbk_subs)
	for name in ops.iterkeys():
		print generate(RESUME_TEMPLATE,name,fop_subs)
	for name in ops.iterkeys():
		print generate(FOP_TEMPLATE,name,fop_subs)

for l in open(sys.argv[1],'r').readlines():
	if l.find('#pragma generate') != -1:
		print "/* BEGIN GENERATED CODE - DO NOT MODIFY */"
		gen_defaults()
		print "/* END GENERATED CODE */"
	else:
		print l[:-1]
