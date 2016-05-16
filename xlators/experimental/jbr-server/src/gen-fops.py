#!/usr/bin/python

# This script generates the boilerplate versions of most fops and cbks in the
# server.  This allows the details of leadership-status checking, sequencing
# between leader and followers (including fan-out), and basic error checking
# to be centralized one place, with per-operation code kept to a minimum.

import os
import re
import string
import sys

curdir = os.path.dirname(sys.argv[0])
gendir = os.path.join(curdir,'../../../../libglusterfs/src')
sys.path.append(gendir)
from generator import ops, fop_subs, cbk_subs, generate

# We really want the callback argument list, even when we're generating fop
# code, so we propagate here.
# TBD: this should probably be right in generate.py
for k, v in cbk_subs.iteritems():
	fop_subs[k]['@ERROR_ARGS@'] = v['@ERROR_ARGS@']

# Stolen from old codegen.py
def load_templates (path):
	templates = {}
	tmpl_re = re.compile("/\* template-name (.*) \*/")
	templates = {}
	t_name = None
	for line in open(path,"r").readlines():
		if not line:
			break
		m = tmpl_re.match(line)
		if m:
			if t_name:
				templates[t_name] = string.join(t_contents,'')
			t_name = m.group(1).strip()
			t_contents = []
		elif t_name:
			t_contents.append(line)
	if t_name:
		templates[t_name] = string.join(t_contents,'')
	return templates

# We need two types of templates.  The first, for pure read operations, just
# needs to do a simple am-i-leader check (augmented to allow dirty reads).
# The second, for pure writes, needs to do fan-out to followers between those
# initial checks and local execution.  There are other operations that don't
# fit neatly into either category - e.g. lock ops or fsync - so we'll just have
# to handle those manually.  The table thus includes entries only for those we
# can categorize.  The special cases, plus any new operations we've never even
# heard of, aren't in there.
#
# Various keywords can be used to define/undefine preprocessor symbols used
# in the templates, on a per-function basis.  For example, if the keyword here
# is "fsync" (lowercase word or abbreviation) that will cause JBR_CG_FSYNC
# (prefix plus uppercase version) to be defined above all of the generated code
# for that fop.

fop_table = {
	"access":		"read",
	"create":		"write",
	"discard":		"write",
#	"entrylk":		"read",
	"fallocate":	"write",
#	"fentrylk":		"read",
	"fgetxattr":	"read",
#	"finodelk":		"read",
#	"flush":		"read",
	"fremovexattr":	"write",
	"fsetattr":		"write",
	"fsetxattr":	"write",
	"fstat":		"read",
#	"fsync":		"read",
#	"fsyncdir":		"read",
	"ftruncate":	"write",
	"fxattrop":		"write",
	"getxattr":		"read",
#	"inodelk":		"read",
	"link":			"write",
	"lk":			"write,queue",
#	"lookup":		"read",
	"mkdir":		"write",
	"mknod":		"write",
	"open":			"write",
	"opendir":		"read",
	"rchecksum":	"read",
	"readdir":		"read",
	"readdirp":		"read",
	"readlink":		"read",
	"readv":		"read",
	"removexattr":	"write",
	"rename":		"write",
	"rmdir":		"write",
	"setattr":		"write",
	"setxattr":		"write",
	"stat":			"read",
	"statfs":		"read",
	"symlink":		"write",
	"truncate":		"write",
	"unlink":		"write",
	"writev":		"write,fsync,queue",
	"xattrop":		"write",
}

# Mention those fops in the selective_generate table, for which
# only a few common functions will be generated, and mention those
# functions. Rest of the functions can be customized
selective_generate = {
	"lk":			"fop,dispatch,call_dispatch",
}

# Stolen from gen_fdl.py
def gen_server (templates):
	fops_done = []
	for name in fop_table.keys():
		info = fop_table[name].split(",")
		kind = info[0]
		flags = info[1:]

		# generate all functions for the fops in fop_table
		# except for the ones in selective_generate for which
		# generate only the functions mentioned in the
		# selective_generate table
		gen_funcs = "fop,complete,continue,fan-in,dispatch, \
			call_dispatch,perform_local_op"
		if name in selective_generate:
			gen_funcs = selective_generate[name].split(",")

		if ("fsync" in flags) or ("queue" in flags):
			flags.append("need_fd")
		for fname in flags:
			print "#define JBR_CG_%s" % fname.upper()

		if 'complete' in gen_funcs:
			print generate(templates[kind+"-complete"],
					name,cbk_subs)

		if 'continue' in gen_funcs:
			print generate(templates[kind+"-continue"],
					name,fop_subs)

		if 'fan-in' in gen_funcs:
			print generate(templates[kind+"-fan-in"],
					name,cbk_subs)

		if 'dispatch' in gen_funcs:
			print generate(templates[kind+"-dispatch"],
					name,fop_subs)

		if 'call_dispatch' in gen_funcs:
			print generate(templates[kind+"-call_dispatch"],
					name,fop_subs)

		if 'perform_local_op' in gen_funcs:
			print generate(templates[kind+"-perform_local_op"],
					name, fop_subs)

		if 'fop' in gen_funcs:
			print generate(templates[kind+"-fop"],name,fop_subs)

		for fname in flags:
			print "#undef JBR_CG_%s" % fname.upper()
		fops_done.append(name)
	# Just for fun, emit the fops table too.
	print("struct xlator_fops fops = {")
	for x in fops_done:
		print("	.%s = jbr_%s,"%(x,x))
	print("};")

tmpl = load_templates(sys.argv[1])
for l in open(sys.argv[2],'r').readlines():
	if l.find('#pragma generate') != -1:
		print "/* BEGIN GENERATED CODE - DO NOT MODIFY */"
		gen_server(tmpl)
		print "/* END GENERATED CODE */"
	else:
		print l[:-1]
