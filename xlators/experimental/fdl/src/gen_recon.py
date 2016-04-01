#!/usr/bin/python

import os
import re
import string
import sys

curdir = os.path.dirname (sys.argv[0])
gendir = os.path.join (curdir, '../../../../libglusterfs/src')
sys.path.append (gendir)
from generator import ops, fop_subs, cbk_subs, generate

# See the big header comment at the start of gen_fdl.py to see how the stages
# fit together.  The big difference here is that *all* of the C code is in the
# template file as labelled fragments, instead of as Python strings.  That
# makes it much easier to edit in one place, with proper syntax highlighting
# and indentation.
#
#   Stage 1 uses type-specific fragments to generate FUNCTION_BODY, instead of
#   LEN_*_TEMPLATE and SERLZ_*_TEMPLATE to generate LEN_CODE and SER_CODE.
#
#   Stage 2 uses the FOP and CASE fragments instead of RECON_TEMPLATE and
#   FOP_TEMPLATE.  The expanded FOP code (including FUNCTION_BODY substitution
#   in the middle of each function) is emitted immediately; the expanded CASE
#   code is saved for the next stage.
#
#   Stage 3 uses the PROLOG and EPILOG fragments, with the expanded CASE code
#   in the middle of EPILOG, to generate the whole output file.
#
# Another way of looking at it is to consider how the fragments appear in
# the final output:
#
#   PROLOG
#   FOP (expanded for CREATE)
#       FOP before FUNCTION_BODY
#       LOC, INTEGER, GFID, etc. (one per arg, by type)
#       FOP after FUNCTION_BODY
#   FOP (expanded for WRITEV)
#       FOP before FUNCTION_BODY
#       GFID, VECTOR, etc. (one per arg, by type)
#       FOP after FUNCTION_BODY
#   (more FOPs)
#   EPILOG
#       EPILOG before CASE
#       CASE statements (one per fop)
#       EPILOG after CASE

typemap = {
	'dict_t *':				"DICT",
	'fd_t *':				"FD",
	'dev_t':				"DOUBLE",
	'gf_xattrop_flags_t':	"INTEGER",
	'int32_t':				"INTEGER",
	'mode_t':				"INTEGER",
	'off_t':				"DOUBLE",
	'size_t':				"DOUBLE",
	'uint32_t':				"INTEGER",
	'loc_t *':				"LOC",
	'const char *':			"STRING",
	'struct iovec *':		"VECTOR",
	'struct iatt *':		"IATT",
	'struct iobref *':		"IOBREF",
}

def get_special_subs (name, args, fop_type):
	code = ""
	cleanups = ""
	links = ""
	s_args = []
	for arg in args:
		if arg[0] == 'extra':
			code += "\t%s %s;\n\n" % (arg[2], arg[1])
			s_args.append(arg[3])
			continue
		if arg[0] == 'link':
			links += fragments["LINK"].replace("@INODE_ARG@",arg[1])	\
									  .replace("@IATT_ARG@",arg[2])
			continue
		if arg[0] != 'fop-arg':
			continue
		if (name, arg[1]) == ('writev', 'count'):
			# Special case: just skip this.  We can't mark it as 'nosync'
			# because of the way the translator and dumper generators look for
			# that after 'stub-name' which we don't define.  Instead of adding a
			# bunch of generic infrastructure for this one case, just pound it
			# here.
			continue
		recon_type = typemap[arg[2]]
		# print "/* %s.%s => %s (%s)*/" % (name, arg[1], recon_type, fop_type)
		if (name == "create") and (arg[1] == "fd"):
			# Special case: fd for create is new, not looked up.
			# print "/* change to NEW_FD */"
			recon_type = "NEW_FD"
		elif (recon_type == "LOC") and (fop_type == "entry-op"):
			# Need to treat this differently for inode vs. entry ops.
			# Special case: link source is treated like inode-op.
			if (name != "link") or (arg[1] != "oldloc"):
				# print "/* change to PARENT_LOC */"
				recon_type = "PARENT_LOC"
		code += fragments[recon_type].replace("@ARGNAME@",arg[1])		\
									 .replace("@ARGTYPE@",arg[2])
		cleanup_key = recon_type + "_CLEANUP"
		if fragments.has_key(cleanup_key):
			new_frag = fragments[cleanup_key].replace("@ARGNAME@",arg[1])
			# Make sure these get added in *reverse* order.  Otherwise, a
			# failure for an earlier argument might goto a label that falls
			# through to the cleanup code for a variable associated with a
			# later argument, but that variable might not even have been
			# *declared* (let alone initialized) yet.  Consider the following
			# case.
			#
			#         process argument A (on failure goto cleanup_A)
			#         set error label to cleanup_A
			#
			#         declare pointer variable for argument B
			#         process argument B (on failure goto cleanup_B)
			#
			#     cleanup_A:
			#         /* whatever */
			#     cleanup_B:
			#         free pointer variable <= "USED BUT NOT SET" error here
			#
			# By adding these in reverse order, we ensure that cleanup_B is
			# actually *before* cleanup_A, and nothing will try to do the free
			# until we've actually attempted processing of B.
			cleanups = new_frag + cleanups
		if 'nosync' in arg[4:]:
			code += "\t(void)%s;\n" % arg[1];
			continue
		if arg[2] in ("loc_t *", "struct iatt *"):
			# These are passed as pointers to the syncop, but they're actual
			# structures in the generated code.
			s_args.append("&"+arg[1]);
		else:
			s_args.append(arg[1])
	# We have to handle a couple of special cases here, because some n00b
	# defined the syncops with a different argument order than the fops they're
	# based on.
	if name == 'writev':
		# Swap 'flags' and 'iobref'.  Also, we need to add the iov count, which
		# is not stored in or read from the journal.  There are other ways to
		# do that, but this is the only place we need anything similar and we
		# already have to treat it as a special case so this is simplest.
		s_args_str = 'fd, &vector, 1, off, iobref, flags, xdata'
	elif name == 'symlink':
		# Swap 'linkpath' and 'loc'.
		s_args_str = '&loc, linkpath, &iatt, xdata'
	else:
		s_args_str = string.join (s_args, ", ")
	return code, links, s_args_str, cleanups

# TBD: probably need to generate type-specific cleanup code as well - e.g.
# fd_unref for an fd_t, loc_wipe for a loc_t, and so on.  All of these
# generated CLEANUP fragments will go at the end of the function, with goto
# labels.  Meanwhile, the error-checking part of each type-specific fragment
# (e.g. LOC or FD) will need to update the indirect label that we jump to when
# an error is detected.  This will probably get messy.
def gen_functions ():
	code = ""
	for name, value in ops.iteritems():
		fop_type = [ x[1] for x in value if x[0] == "journal" ]
		if not fop_type:
			continue
		body, links, syncop_args, cleanups = get_special_subs (name, value,
															   fop_type[0])
		fop_subs[name]["@FUNCTION_BODY@"] = body
		fop_subs[name]["@LINKS@"] = links
		fop_subs[name]["@SYNCOP_ARGS@"] = syncop_args
		fop_subs[name]["@CLEANUPS@"] = cleanups
		if name == "writev":
			# Take advantage of the fact that, *during reconciliation*, the
			# vector is always a single element.  In normal I/O it's not.
			fop_subs[name]["@SUCCESS_VALUE@"] = "vector.iov_len"
		else:
			fop_subs[name]["@SUCCESS_VALUE@"] = "GFAPI_SUCCESS"
		# Print the FOP fragment with @FUNCTION_BODY@ in the middle.
		code += generate(fragments["FOP"],name,fop_subs)
	return code

def gen_cases ():
	code = ""
	for name, value in ops.iteritems():
		if "journal" not in [ x[0] for x in value ]:
			continue
		# Add the CASE fragment for this fop.
		code += generate(fragments["CASE"],name,fop_subs)
	return code

def load_fragments (path="recon-tmpl.c"):
	pragma_re = re.compile('pragma fragment (.*)')
	cur_symbol = None
	cur_value = ""
	result = {}
	for line in open(path,"r").readlines():
		m = pragma_re.search(line)
		if m:
			if cur_symbol:
				result[cur_symbol] = cur_value
			cur_symbol = m.group(1)
			cur_value = ""
		else:
			cur_value += line
	if cur_symbol:
		result[cur_symbol] = cur_value
	return result

if __name__ == "__main__":
	fragments = load_fragments(sys.argv[1])
	print "/* BEGIN GENERATED CODE - DO NOT MODIFY */"
	print fragments["PROLOG"]
	print gen_functions()
	print fragments["EPILOG"].replace("@SWITCH_BODY@",gen_cases())
	print "/* END GENERATED CODE */"
