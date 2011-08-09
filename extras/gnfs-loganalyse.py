#!/bin/python
"""
  Copyright (c) 2010-2011 Gluster, Inc. <http://www.gluster.com>
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
"""

import os
import string
import sys


class NFSRequest:
        def __init__ (self, logline, linecount):
                self.calllinecount = 0
                self.xid = ""
                self.op = ""
                self.opdata = ""
                self.replydata = ""
                self.replylinecount = 0

                tokens = logline.strip ().split (" ")
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


        def getXID (self):
                return self.xid

        def setReply (self, logline, linecount):
                tokens = logline.strip ().split (" ")
                statidx = tokens.index ("NFS:")
                self.replydata = " ".join (tokens [statidx+1:])
                self.replylinecount = linecount

        def dump (self):
                print "ReqLine: " + str(self.calllinecount) + " XID: " + self.xid + " " + self.op + " ARGS: " + self.opdata + " RepLine: " + str(self.replylinecount) + " " + self.replydata

class NFSLogAnalyzer:

        def __init__ (self):
                self.xid_request_map = {}
                self.orphan_replies = {}
                self.CALL = 1
                self.REPLY = 2

        def handle_call_line (self, logline, linecount):
                newreq = NFSRequest (logline, linecount)
                xid = newreq.getXID ()
                self.xid_request_map [xid] = newreq

        def handle_reply_line (self, logline, linecount):
                tokens = logline.strip ().split (" ")

                xididx = tokens.index ("XID:")
                xid = tokens [xididx + 1].strip(",")
                if xid not in self.xid_request_map.keys ():
                        self.orphan_replies [xid] = logline
                else:
                        self.xid_request_map [xid].setReply (logline, linecount)

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
                rcount = len (self.xid_request_map.keys ())
                orphancount = len (self.orphan_replies.keys ())
                print "Requests: " + str(rcount) + ", Orphans: " + str(orphancount)

        def dump (self):
                self.getStats ()
                for rq in self.xid_request_map.values ():
                        rq.dump ()




linecount = 0
la = NFSLogAnalyzer ()

progmsgcount = 1000
dumpinterval = 2000000

if "--progress" in sys.argv:
        idx = sys.argv.index ("--progress")
        progmsgcount = int(sys.argv[idx+1])


if "--dump" in sys.argv:
        idx = sys.argv.index ("--dump")
        dumpinterval = int(sys.argv[idx+1])

for line in sys.stdin:
        linecount = linecount + 1
        if linecount % dumpinterval == 0:
                sys.stderr.write ("Dumping data..\n")
                la.dump ()
                del la
                la = NFSLogAnalyzer ()

        if linecount % progmsgcount == 0:
                sys.stderr.write ("Integrating line: "+ str(linecount) + "\n")
        la.analyzeLine (line, linecount)

la.dump ()
