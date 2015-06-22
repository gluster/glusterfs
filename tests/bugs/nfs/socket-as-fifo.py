#!/usr/bin/env python
#
# Create a unix domain socket and test if it is a socket (and not a fifo/pipe).
#
# Author: Niels de Vos <ndevos@redhat.com>
#

import os
import stat
import sys
import socket

ret = 1

if len(sys.argv) != 2:
        print 'Usage: %s <socket>' % (sys.argv[0])
        sys.exit(ret)

path = sys.argv[1]

sock = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
sock.bind(path)

stbuf = os.stat(path)
mode = stbuf.st_mode

if stat.S_ISSOCK(mode):
        ret = 0

sock.close()
os.unlink(path)

sys.exit(ret)
