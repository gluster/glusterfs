#!/usr/bin/python

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

# Stolen from gen_fdl.py
def gen_client (templates):
	for name, value in ops.iteritems():
		if name == 'getspec':
			# It's not real if it doesn't have a stub function.
			continue
		print generate(templates['cbk'],name,cbk_subs)
		print generate(templates['cont-func'],name,fop_subs)
		print generate(templates['fop'],name,fop_subs)

tmpl = load_templates(sys.argv[1])
for l in open(sys.argv[2],'r').readlines():
	if l.find('#pragma generate') != -1:
		print "/* BEGIN GENERATED CODE - DO NOT MODIFY */"
		gen_client(tmpl)
		print "/* END GENERATED CODE */"
	else:
		print l[:-1]
