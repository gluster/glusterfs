#!/usr/bin/python2

"""
  Copyright (c) 2013 Red Hat, Inc. <http://www.redhat.com>
  This file is part of GlusterFS.

  This file is licensed to you under your choice of the GNU Lesser
  General Public License, version 3 or any later version (LGPLv3 or
  later), or the GNU General Public License, version 2 (GPLv2), in all
  cases as published by the Free Software Foundation.
"""

"""
  INSTRUCTIONS
  Put this in /usr/lib64/glusterfs/$version/filter to have it run automatically,
  or else you'll have to run it by hand every time you change the volume
  configuration.  Give it a list of volume names on which to enable the
  protection functionality; it will deliberately ignore client volfiles for
  other volumes, and all server volfiles.  It *will* include internal client
  volfiles such as those used for NFS or rebalance/self-heal; this is a
  deliberate choice so that it will catch deletions from those sources as well.
"""

from __future__ import print_function
import copy
import string
import sys
import types

volume_list = [ "jdtest" ]

class Translator:
    def __init__ (self, name):
        self.name = name
        self.xl_type = ""
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
            xlator.xl_type = text[1]
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
    print("    type %s" % last.xl_type, file=stream)
    for k, v in last.opts.iteritems():
        print("    option %s %s" % (k, v), file=stream)
    if last.subvols:
        print("    subvolumes %s" % string.join(
            [ sv.name for sv in last.subvols ]), file=stream)
    print("end-volume", file=stream)

def push_filter (graph, old_xl, filt_type, opts={}):
    new_type = "-" + filt_type.split("/")[1]
    old_type = "-" + old_xl.xl_type.split("/")[1]
    pos = old_xl.name.find(old_type)
    if pos >= 0:
        new_name = old_xl.name
        old_name = new_name[:pos] + new_type + new_name[len(old_type)+pos:]
    else:
        new_name = old_xl.name + old_type
        old_name = old_xl.name + new_type
    new_xl = Translator(new_name)
    new_xl.xl_type = old_xl.xl_type
    new_xl.opts = old_xl.opts
    new_xl.subvols = old_xl.subvols
    graph[new_xl.name] = new_xl
    old_xl.name = old_name
    old_xl.xl_type = filt_type
    old_xl.opts = opts
    old_xl.subvols = [new_xl]
    graph[old_xl.name] = old_xl

if __name__ == "__main__":
    path = sys.argv[1]
    # Alow an override for debugging.
    for extra in sys.argv[2:]:
        volume_list.append(extra)
    graph, last = load(path)
    for v in volume_list:
        if graph.has_key(v):
            break
    else:
        print("No configured volumes found - aborting.")
        sys.exit(0)
    for v in graph.values():
        if v.xl_type == "cluster/distribute":
            push_filter(graph,v,"features/prot_dht")
        elif v.xl_type == "protocol/client":
            push_filter(graph,v,"features/prot_client")
    # We push debug/trace so that every fop gets a real frame, because DHT
    # gets confused if STACK_WIND_TAIL causes certain fops to be invoked
    # from anything other than a direct child.
    for v in graph.values():
        if v.xl_type == "features/prot_client":
            push_filter(graph,v,"debug/trace")
    generate(graph,last,stream=open(path,"w"))
