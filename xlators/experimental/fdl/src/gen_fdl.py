#!/usr/bin/python

import os
import sys

curdir = os.path.dirname (sys.argv[0])
gendir = os.path.join (curdir, '../../../../libglusterfs/src')
sys.path.append (gendir)
from generator import ops, fop_subs, cbk_subs, generate

# Generation occurs in three stages.  In this case, it actually makes more
# sense to discuss them in the *opposite* order of that in which they
# actually happen.
#
#   Stage 3 is to insert all of the generated code into a file, replacing the
#   "#pragma generate" that's already there.  The file can thus contain all
#   sorts of stuff that's not specific to one fop, either before or after the
#   generated code as appropriate.
#
#   Stage 2 is to generate all of the code *for a particular fop*, using a
#   string-valued template plus a table of substitution values.  Most of these
#   are built in to the generator itself.  However, we also add a couple that
#   are specific to this particular translator - LEN_CODE and SER_CODE.  These
#   are per-fop functions to get the length or the contents (respectively) of
#   what we'll put in the log.  As with stage 3 allowing per-file boilerplate
#   before and after generated code, this allows per-fop boilerplate before and
#   after generated code.
#
#   Stage 1, therefore, is to create the LEN_CODE and SER_CODE substitutions for
#   each fop, and put them in the same table where e.g. NAME and SHORT_ARGS
#   already are.  We do this by looking at the fop-description table in the
#   generator module, then doing out own template substitution to plug each
#   specific argument name into another string-valued template.
#
# So, what does this leave us with in terms of variables and files?
#
#   For stage 1, we have a series of LEN_*_TEMPLATE and SERLZ_*_TEMPLATE
#   strings, which are used to generate the length and serialization code for
#   each argument type.
#
#   For stage 2, we have a bunch of *_TEMPLATE strings (no LEN_ or SERLZ_
#   prefix), which are used (along with the output from stage 1) to generate
#   whole functions.
#
#   For stage 3, we have a whole separate file (fdl_tmpl.c) into which we insert
#   the collection of all functions defined in stage 2.


LEN_TEMPLATE = """
void
fdl_len_@NAME@ (call_stub_t *stub)
{
        uint32_t    meta_len    = sizeof (event_header_t);
		uint32_t	data_len	= 0;

        /* TBD: global stuff, e.g. uid/gid */
@LEN_CODE@

		/* TBD: pad extension length */
		stub->jnl_meta_len = meta_len;
		stub->jnl_data_len = data_len;
}
"""

SER_TEMPLATE = """
void
fdl_serialize_@NAME@ (call_stub_t *stub, char *meta_buf, char *data_buf)
{
		event_header_t	*eh;
		unsigned long	offset = 0;

        /* TBD: word size/endianness */
		eh = (event_header_t *)meta_buf;
		eh->event_type = NEW_REQUEST;
		eh->fop_type = GF_FOP_@UPNAME@;
		eh->request_id = 0;	// TBD
		meta_buf += sizeof (*eh);
@SER_CODE@
		/* TBD: pad extension length */
		eh->ext_length = offset;
}
"""

CBK_TEMPLATE = """
int32_t
fdl_@NAME@_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                int32_t op_ret, int32_t op_errno,
                @LONG_ARGS@)
{
        STACK_UNWIND_STRICT (@NAME@, frame, op_ret, op_errno,
                             @SHORT_ARGS@);
        return 0;
}
"""

CONTINUE_TEMPLATE = """
int32_t
fdl_@NAME@_continue (call_frame_t *frame, xlator_t *this,
                     @LONG_ARGS@)
{
        STACK_WIND (frame, fdl_@NAME@_cbk,
                    FIRST_CHILD(this), FIRST_CHILD(this)->fops->@NAME@,
                    @SHORT_ARGS@);
        return 0;
}

"""

FOP_TEMPLATE = """
int32_t
fdl_@NAME@ (call_frame_t *frame, xlator_t *this,
            @LONG_ARGS@)
{
        call_stub_t     *stub;

        stub = fop_@NAME@_stub (frame, default_@NAME@,
                                @SHORT_ARGS@);
		fdl_len_@NAME@ (stub);
        stub->serialize = fdl_serialize_@NAME@;
        fdl_enqueue (this, stub);

        return 0;
}
"""

LEN_DICT_TEMPLATE = """
		if (@SRC@) {
			data_pair_t *memb;
			for (memb = @SRC@->members_list; memb; memb = memb->next) {
				meta_len += sizeof(int);
				meta_len += strlen(memb->key) + 1;
				meta_len += sizeof(int);
				meta_len += memb->value->len;
			}
		}
		meta_len += sizeof(int);
"""

LEN_GFID_TEMPLATE = """
        meta_len += 16;
"""

LEN_INTEGER_TEMPLATE = """
        meta_len += sizeof (@SRC@);
"""

# 16 for gfid, 16 for pargfid, 1 for flag, 0/1 for terminating NUL
LEN_LOC_TEMPLATE = """
        if (@SRC@.name) {
                meta_len += (strlen (@SRC@.name) + 34);
        } else {
                meta_len += 33;
        }
"""

LEN_STRING_TEMPLATE = """
        if (@SRC@) {
                meta_len += (strlen (@SRC@) + 1);
        } else {
                meta_len += 1;
        }
"""

LEN_VECTOR_TEMPLATE = """
        meta_len += sizeof(size_t);
        data_len += iov_length (@VEC@, @CNT@);
"""

LEN_IATT_TEMPLATE = """
		meta_len += sizeof(@SRC@.ia_prot);
		meta_len += sizeof(@SRC@.ia_uid);
		meta_len += sizeof(@SRC@.ia_gid);
		meta_len += sizeof(@SRC@.ia_atime);
		meta_len += sizeof(@SRC@.ia_atime_nsec);
		meta_len += sizeof(@SRC@.ia_mtime);
		meta_len += sizeof(@SRC@.ia_mtime_nsec);
"""

SERLZ_DICT_TEMPLATE = """
        if (@SRC@) {
			data_pair_t *memb;
			for (memb = @SRC@->members_list; memb; memb = memb->next) {
				*((int *)(meta_buf+offset)) = strlen(memb->key) + 1;
				offset += sizeof(int);
				strcpy (meta_buf+offset, memb->key);
				offset += strlen(memb->key) + 1;
				*((int *)(meta_buf+offset)) = memb->value->len;
				offset += sizeof(int);
				memcpy (meta_buf+offset, memb->value->data, memb->value->len);
				offset += memb->value->len;
			}
        }
		*((int *)(meta_buf+offset)) = 0;
		offset += sizeof(int);
"""

SERLZ_GFID_TEMPLATE = """
        memcpy (meta_buf+offset, @SRC@->inode->gfid, 16);
        offset += 16;
"""

SERLZ_INTEGER_TEMPLATE = """
        memcpy (meta_buf+offset, &@SRC@, sizeof(@SRC@));
        offset += sizeof(@SRC@);
"""

SERLZ_LOC_TEMPLATE = """
        memcpy (meta_buf+offset, @SRC@.gfid, 16);
        offset += 16;
        memcpy (meta_buf+offset, @SRC@.pargfid, 16);
        offset += 16;
        if (@SRC@.name) {
                *(meta_buf+offset) = 1;
				++offset;
                strcpy (meta_buf+offset, @SRC@.name);
                offset += (strlen (@SRC@.name) + 1);
        } else {
                *(meta_buf+offset) = 0;
				++offset;
        }
"""

SERLZ_STRING_TEMPLATE = """
        if (@SRC@) {
                *(meta_buf+offset) = 1;
				++offset;
                strcpy (meta_buf+offset, @SRC@);
                offset += strlen(@SRC@);
        } else {
                *(meta_buf+offset) = 0;
				++offset;
        }
"""

SERLZ_VECTOR_TEMPLATE = """
        *((size_t *)(meta_buf+offset)) = iov_length (@VEC@, @CNT@);
        offset += sizeof(size_t);
        int32_t i;
        for (i = 0; i < @CNT@; ++i) {
                memcpy (data_buf, @VEC@[i].iov_base, @VEC@[i].iov_len);
                data_buf += @VEC@[i].iov_len;
        }
"""

# We don't need to save all of the fields - only those affected by chown,
# chgrp, chmod, and utime.
SERLZ_IATT_TEMPLATE = """
		*((ia_prot_t *)(meta_buf+offset)) = @SRC@.ia_prot;
		offset += sizeof(@SRC@.ia_prot);
		*((uint32_t *)(meta_buf+offset)) = @SRC@.ia_uid;
		offset += sizeof(@SRC@.ia_uid);
		*((uint32_t *)(meta_buf+offset)) = @SRC@.ia_gid;
		offset += sizeof(@SRC@.ia_gid);
		*((uint32_t *)(meta_buf+offset)) = @SRC@.ia_atime;
		offset += sizeof(@SRC@.ia_atime);
		*((uint32_t *)(meta_buf+offset)) = @SRC@.ia_atime_nsec;
		offset += sizeof(@SRC@.ia_atime_nsec);
		*((uint32_t *)(meta_buf+offset)) = @SRC@.ia_mtime;
		offset += sizeof(@SRC@.ia_mtime);
		*((uint32_t *)(meta_buf+offset)) = @SRC@.ia_mtime_nsec;
		offset += sizeof(@SRC@.ia_mtime_nsec);
"""

typemap = {
	'dict_t *':				( LEN_DICT_TEMPLATE,	SERLZ_DICT_TEMPLATE),
	'fd_t *':				( LEN_GFID_TEMPLATE,	SERLZ_GFID_TEMPLATE),
	'dev_t':				( LEN_INTEGER_TEMPLATE,	SERLZ_INTEGER_TEMPLATE),
	'gf_xattrop_flags_t':	( LEN_INTEGER_TEMPLATE,	SERLZ_INTEGER_TEMPLATE),
	'int32_t':				( LEN_INTEGER_TEMPLATE,	SERLZ_INTEGER_TEMPLATE),
	'mode_t':				( LEN_INTEGER_TEMPLATE, SERLZ_INTEGER_TEMPLATE),
	'off_t':				( LEN_INTEGER_TEMPLATE,	SERLZ_INTEGER_TEMPLATE),
	'size_t':				( LEN_INTEGER_TEMPLATE,	SERLZ_INTEGER_TEMPLATE),
	'uint32_t':				( LEN_INTEGER_TEMPLATE,	SERLZ_INTEGER_TEMPLATE),
	'loc_t *':				( LEN_LOC_TEMPLATE,		SERLZ_LOC_TEMPLATE),
	'const char *':			( LEN_STRING_TEMPLATE,	SERLZ_STRING_TEMPLATE),
	'struct iatt *':		( LEN_IATT_TEMPLATE,	SERLZ_IATT_TEMPLATE),
}

def get_special_subs (args):
	len_code = ""
	ser_code = ""
	for arg in args:
		if (arg[0] != 'fop-arg') or (len(arg) < 4):
			continue
		# Let this throw an exception if we get an unknown field name.  The
		# broken build will remind whoever messed with the stub code that a
		# corresponding update is needed here.
		if arg[3] == "vector":
			# Make it as obvious as possible that this is a special case.
			len_code += LEN_VECTOR_TEMPLATE \
				.replace("@VEC@","stub->args.vector") \
				.replace("@CNT@","stub->args.count")
			ser_code += SERLZ_VECTOR_TEMPLATE \
				.replace("@VEC@","stub->args.vector") \
				.replace("@CNT@","stub->args.count")
		else:
			len_tmpl, ser_tmpl = typemap[arg[2]]
			src = "stub->args.%s" % arg[3]
			len_code += len_tmpl.replace("@SRC@",src)
			ser_code += ser_tmpl.replace("@SRC@",src)
	return len_code, ser_code

def gen_fdl ():
	entrypoints = []
	for name, value in ops.iteritems():
		if "journal" not in [ x[0] for x in value ]:
			continue
		len_code, ser_code = get_special_subs(value)
		fop_subs[name]["@LEN_CODE@"] = len_code[:-1]
		fop_subs[name]["@SER_CODE@"] = ser_code[:-1]
		print generate(LEN_TEMPLATE,name,fop_subs)
		print generate(SER_TEMPLATE,name,fop_subs)
		print generate(CBK_TEMPLATE,name,cbk_subs)
		print generate(CONTINUE_TEMPLATE,name,fop_subs)
		print generate(FOP_TEMPLATE,name,fop_subs)
		entrypoints.append(name)
	print "struct xlator_fops fops = {"
	for ep in entrypoints:
		print "\t.%s = fdl_%s," % (ep, ep)
	print "};"

for l in open(sys.argv[1],'r').readlines():
	if l.find('#pragma generate') != -1:
		print "/* BEGIN GENERATED CODE - DO NOT MODIFY */"
		gen_fdl()
		print "/* END GENERATED CODE */"
	else:
		print l[:-1]
