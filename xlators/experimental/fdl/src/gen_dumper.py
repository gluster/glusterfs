#!/usr/bin/python

import os
import re
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
#       GFID, VECTOR, etc. (on per arg, by type)
#       FOP after FUNCTION_BODY
#   (more FOPs)
#   EPILOG
#       EPILOG before CASE
#       CASE statements (one per fop)
#       EPILOG after CASE

typemap = {
	'dict_t *':				( "DICT",		""),
	'fd_t *':				( "GFID",		""),
	'dev_t':				( "DOUBLE",		"%ld (0x%lx)"),
	'gf_xattrop_flags_t':	( "INTEGER",	"%d (0x%x)"),
	'int32_t':				( "INTEGER",	"%d (0x%x)"),
	'mode_t':				( "INTEGER",	"%d (0x%x)"),
	'off_t':				( "DOUBLE",		"%ld (0x%lx)"),
	'size_t':				( "DOUBLE",		"%ld (0x%lx)"),
	'uint32_t':				( "INTEGER",	"%d (0x%x)"),
	'loc_t *':				( "LOC",		""),
	'const char *':			( "STRING",		""),
	'struct iovec *':		( "VECTOR",		""),
	'struct iatt *':		( "IATT",		""),
}

def get_special_subs (args):
	code = ""
	for arg in args:
		if (arg[0] != 'fop-arg') or (len(arg) < 4):
			continue
		recon_type, recon_fmt = typemap[arg[2]]
		code += fragments[recon_type].replace("@ARGNAME@",arg[3])		\
									 .replace("@FORMAT@",recon_fmt)
	return code

def gen_functions ():
	code = ""
	for name, value in ops.iteritems():
		if "journal" not in [ x[0] for x in value ]:
			continue
		fop_subs[name]["@FUNCTION_BODY@"] = get_special_subs(value)
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
