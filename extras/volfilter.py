# Copyright (c) 2010-2011 Red Hat, Inc.
#
# This file is part of HekaFS.
#
# HekaFS is free software: you can redistribute it and/or modify it under the
# terms of the GNU General Public License, version 3, as published by the Free
# Software Foundation.
#
# HekaFS is distributed in the hope that it will be useful, but WITHOUT ANY
# WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR
# A PARTICULAR PURPOSE.  See the GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License * along
# with HekaFS.  If not, see <http://www.gnu.org/licenses/>.

from __future__ import print_function
import copy
import string
import sys
import types

good_xlators = [
	"cluster/afr",
	"cluster/dht",
	"cluster/distribute",
	"cluster/replicate",
	"cluster/stripe",
	"debug/io-stats",
	"features/access-control",
	"features/locks",
	"features/marker",
	"features/uidmap",
	"performance/io-threads",
	"protocol/client",
	"protocol/server",
	"storage/posix",
]

def copy_stack (old_xl,suffix,recursive=False):
	if recursive:
		new_name = old_xl.name + "-" + suffix
	else:
		new_name = suffix
	new_xl = Translator(new_name)
	new_xl.type = old_xl.type
	# The results with normal assignment here are . . . amusing.
	new_xl.opts = copy.deepcopy(old_xl.opts)
	for sv in old_xl.subvols:
		new_xl.subvols.append(copy_stack(sv,suffix,True))
	# Patch up the path at the bottom.
	if new_xl.type == "storage/posix":
		new_xl.opts["directory"] += ("/" + suffix)
	return new_xl

def cleanup (parent, graph):
	if parent.type in good_xlators:
		# Temporary fix so that HekaFS volumes can use the
		# SSL-enabled multi-threaded socket transport.
		if parent.type == "protocol/server":
			parent.type = "protocol/server2"
			parent.opts["transport-type"] = "ssl"
		elif parent.type == "protocol/client":
			parent.type = "protocol/client2"
			parent.opts["transport-type"] = "ssl"
		sv = []
		for child in parent.subvols:
			sv.append(cleanup(child,graph))
		parent.subvols = sv
	else:
		parent = cleanup(parent.subvols[0],graph)
	return parent

class Translator:
	def __init__ (self, name):
		self.name = name
		self.type = ""
		self.opts = {}
		self.subvols = []
		self.dumped = False
	def __repr__ (self):
		return "<Translator %s>" % self.name

def load (path):
	# If it's a string, open it; otherwise, assume it's already a
	# file-like object (most notably from urllib*).
	if type(path) in types.StringTypes:
		fp = file(path,"r")
	else:
		fp = path
	all_xlators = {}
	xlator = None
	last_xlator = None
	while True:
		text = fp.readline()
		if text == "":
			break
		text = text.split()
		if not len(text):
			continue
		if text[0] == "volume":
			if xlator:
				raise RuntimeError, "nested volume definition"
			xlator = Translator(text[1])
			continue
		if not xlator:
			raise RuntimeError, "text outside volume definition"
		if text[0] == "type":
			xlator.type = text[1]
			continue
		if text[0] == "option":
			xlator.opts[text[1]] = string.join(text[2:])
			continue
		if text[0] == "subvolumes":
			for sv in text[1:]:
				xlator.subvols.append(all_xlators[sv])
			continue
		if text[0] == "end-volume":
			all_xlators[xlator.name] = xlator
			last_xlator = xlator
			xlator = None
			continue
		raise RuntimeError, "unrecognized keyword %s" % text[0]
	if xlator:
		raise RuntimeError, "unclosed volume definition"
	return all_xlators, last_xlator

def generate (graph, last, stream=sys.stdout):
	for sv in last.subvols:
		if not sv.dumped:
			generate(graph,sv,stream)
			print("", file=stream)
			sv.dumped = True
	print("volume %s" % last.name, file=stream)
	print("    type %s" % last.type, file=stream)
	for k, v in last.opts.iteritems():
		print("    option %s %s" % (k, v), file=stream)
	if last.subvols:
		print("    subvolumes %s" % string.join(
			[ sv.name for sv in last.subvols ]), file=stream)
	print("end-volume", file=stream)

def push_filter (graph, old_xl, filt_type, opts={}):
	suffix = "-" + old_xl.type.split("/")[1]
	if len(old_xl.name) > len(suffix):
		if old_xl.name[-len(suffix):] == suffix:
			old_xl.name = old_xl.name[:-len(suffix)]
	new_xl = Translator(old_xl.name+suffix)
	new_xl.type = old_xl.type
	new_xl.opts = old_xl.opts
	new_xl.subvols = old_xl.subvols
	graph[new_xl.name] = new_xl
	old_xl.name += ("-" + filt_type.split("/")[1])
	old_xl.type = filt_type
	old_xl.opts = opts
	old_xl.subvols = [new_xl]
	graph[old_xl.name] = old_xl

def delete (graph, victim):
	if len(victim.subvols) != 1:
		raise RuntimeError, "attempt to delete non-unary translator"
	for xl in graph.itervalues():
		while xl.subvols.count(victim):
			i = xl.subvols.index(victim)
			xl.subvols[i] = victim.subvols[0]

if __name__ == "__main__":
	graph, last = load(sys.argv[1])
	generate(graph,last)
