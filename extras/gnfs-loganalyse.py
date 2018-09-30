#!/bin/python
"""
  Copyright (c) 2006-2012 Red Hat, Inc. <http://www.redhat.com>
  This file is part of GlusterFS.

  This file is licensed to you under your choice of the GNU Lesser
  General Public License, version 3 or any later version (LGPLv3 or
  later), or the GNU General Public License, version 2 (GPLv2), in all
  cases as published by the Free Software Foundation.

"""

from __future__ import print_function
import os
import string
import sys


class NFSRequest:
        def requestIsEntryOp (self):
                op = self.op
                if op == "CREATE" or op == "LOOKUP" or op == "REMOVE" or op == "LINK" or op == "RENAME" or op == "MKDIR" or op == "RMDIR" or op == "SYMLINK" or op == "MKNOD":
                        return 1
                else:
                        return 0

        def __init__ (self, logline, linecount):
                self.calllinecount = 0
                self.xid = ""
                self.op = ""
                self.opdata = ""
                self.replydata = ""
                self.replylinecount = 0
                self.timestamp = ""
                self.entryname = ""
                self.gfid = ""
                self.replygfid = ""

                tokens = logline.strip ().split (" ")
                self.timestamp = tokens[0] + " " + tokens[1]
                if "XID:" not in tokens:
                        return None

                if  "args:" not in tokens:
                        return None

                self.calllinecount = linecount

                xididx = tokens.index ("XID:")
                self.xid = tokens [xididx+1].strip(",") 

                opidx = tokens.index ("args:")
                self.op = tokens [opidx-1].strip (":")
                self.opdata = " ".join(tokens [opidx+1:])
                if self.requestIsEntryOp ():
                        nameidx = tokens.index ("name:")
                        self.entryname = tokens[nameidx + 1].strip (",")
                gfididx = tokens.index ("gfid")
                self.gfid = tokens[gfididx +1].strip(",")


        def getXID (self):
                return self.xid

        def setReply (self, logline, linecount):
                tokens = logline.strip ().split (" ")
                timestamp = tokens[0] + " " + tokens[1]
                statidx = tokens.index ("NFS:")
                self.replydata = " TimeStamp: " + timestamp + " " + " ".join (tokens [statidx+1:])
                self.replylinecount = linecount
                if "gfid" in tokens:
                        gfididx = tokens.index ("gfid")
                        self.replygfid = tokens [gfididx + 1].strip(",")

        def dump (self):
                print("ReqLine: " + str(self.calllinecount) + " TimeStamp: " + self.timestamp + ", XID: " + self.xid + " " + self.op + " ARGS: " + self.opdata + " RepLine: " + str(self.replylinecount) + " " + self.replydata)

class NFSLogAnalyzer:

        def __init__ (self, optn, trackfilename, tracknamefh, stats):
                self.stats = stats
                self.xid_request_map = {}
                self.orphan_replies = {}
                self.rqlist = []
                self.CALL = 1
                self.REPLY = 2
                self.optn = optn
                self.trackfilename = trackfilename
                self.tracknamefh = tracknamefh
                self.trackedfilehandles = []

        def handle_call_line (self, logline, linecount):
                newreq = NFSRequest (logline, linecount)
                xid = newreq.getXID ()
                if (self.optn == SYNTHESIZE):
                        self.xid_request_map [xid] = newreq
                        self.rqlist.append(newreq)
                elif self.optn == TRACKFILENAME:
                        if newreq.requestIsEntryOp():
                                if newreq.entryname == self.trackfilename:
                                        self.xid_request_map [xid] = newreq
                                        self.rqlist.append(newreq)
                                else:
                                        del newreq
                        elif self.tracknamefh == ENABLE_TRACKNAME_FH:
                                if len (self.trackedfilehandles) > 0:
                                        if newreq.gfid in self.trackedfilehandles:
                                                self.xid_request_map [xid] = newreq
                                                self.rqlist.append(newreq)
                                        else:
                                                del newreq
                                else:
                                        del newreq
                        else:
                                del newreq


        def handle_reply_line (self, logline, linecount):
                tokens = logline.strip ().split (" ")

                xididx = tokens.index ("XID:")
                xid = tokens [xididx + 1].strip(",")
                if xid not in self.xid_request_map.keys ():
                        self.orphan_replies [xid] = logline
                else:
                        rq = self.xid_request_map [xid]
                        rq.setReply (logline, linecount)
                        if rq.requestIsEntryOp() and rq.entryname == self.trackfilename:
                                self.trackedfilehandles.append (rq.replygfid)

        def analyzeLine (self, logline, linecount):
                tokens = logline.strip ().split (" ")
                msgtype = 0

                if "XID:" not in tokens:
                        return

                if  "args:" in tokens:
                        msgtype = self.CALL
                elif "NFS:" in tokens:
                        msgtype = self.REPLY

                if msgtype == self.CALL:
                        self.handle_call_line (logline, linecount)
                elif msgtype == self.REPLY:
                        self.handle_reply_line (logline, linecount)

        def getStats (self):
                if self.stats == 0:
                        return
                rcount = len (self.xid_request_map.keys ())
                orphancount = len (self.orphan_replies.keys ())
                print("Requests: " + str(rcount) + ", Orphans: " + str(orphancount))

        def dump (self):
                self.getStats ()
                for rq in self.rqlist:
                        rq.dump ()
                        del rq

                self.rqlist = []
                self.orphan_replies = {}
                self.xid_request_map = {}


linecount = 0

SYNTHESIZE = 1
TRACKFILENAME = 2

ENABLESTATS = 1
DISABLESTATS = 0

ENABLE_TRACKNAME_FH = 1
DISABLE_TRACKNAME_FH = 0

progmsgcount = 1000
dumpinterval = 200000
operation = SYNTHESIZE
stats = ENABLESTATS
tracknamefh = DISABLE_TRACKNAME_FH
trackfilename = ""

"""
Print the progress of the analysing operations every X number of lines read from
the logs, where X is the argument provided to this option.

Use this to print a status message every say 10000 lines processed or 100000
lines processed to know how much longer the processing will go on for.


USAGE: --progress <NUMLINES>
"""
if "--progress" in sys.argv:
        idx = sys.argv.index ("--progress")
        progmsgcount = int(sys.argv[idx+1])

"""
The replies for a NFS request can be separated by hundreds and even thousands
of other NFS requests and replies. These can be spread over many hundreds and
thousands of log lines. This script maintains a memory dict to map each request
to its reply using the XID. Because this is in-core, there is a limit to the
number of entries in the dict. At regular intervals, it dumps the mapped
requests and the replies into the stdout. The requests whose replies were not
found at the point of dumping are left as orphans, i.e. without info about the
replies. Use this option to tune the number of lines to maximize the number of
requests whose replies are found while balancing the dict size with memory
on the machine. The default works fine for most cases.

USAGE: --dump <NUMLINES>
"""
if "--dump" in sys.argv:
        idx = sys.argv.index ("--dump")
        dumpinterval = int(sys.argv[idx+1])

"""
The default operation of the script is to output all the requests mapped to
their replies in a single line. This operation mode can be changed by this
argument. It is used to print only those operations that were performed on the
filename given as the argument to this option. Only those entry operations are
printed which contain this filename.

USAGE: --trackfilename <filename>
"""
if "--trackfilename" in sys.argv:
        idx = sys.argv.index ("--trackfilename")
        trackfilename = sys.argv[idx + 1]
        operation = TRACKFILENAME

"""
At every dump interval, some stats are printed about the dumped lines.
Use this option to disable printing that to avoid cluttering the
output.
"""
if "--nostats" in sys.argv:
        stats = DISABLESTATS

"""
While tracking a file using --trackfilename, we're only given those
operations which contain the filename. This excludes a large number
of operations which operate on that file using its filehandle instead of
the filename. This option enables outputting those operations also. It
tracks every single file handle that was ever seen in the log for a given
filename.

USAGE: --trackfilename
"""
if "--tracknamefh" in sys.argv:
        tracknamefh = ENABLE_TRACKNAME_FH

la = NFSLogAnalyzer (operation, trackfilename, tracknamefh, stats)

for line in sys.stdin:
        linecount = linecount + 1
        if linecount % dumpinterval == 0:
                sys.stderr.write ("Dumping data..\n")
                la.dump ()

        if linecount % progmsgcount == 0:
                sys.stderr.write ("Integrating line: "+ str(linecount) + "\n")
        la.analyzeLine (line, linecount)
