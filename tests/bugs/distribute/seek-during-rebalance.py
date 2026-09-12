#!/usr/bin/env python3
# Helper for seek-during-rebalance.t: hold an fd open across an (external) DHT
# file migration, then run fops on the held fd when told to.
#
# usage: seek-during-rebalance.py FILE GO-MARKER OUT OP[,OP...]
#
# Opens FILE read-only and appends "READY" to OUT; waits until GO-MARKER exists;
# then runs each OP on the held fd and appends "OP=<result>" to OUT.
#   seek_data / seek_hole -> the returned offset, or the errno name (e.g. ENXIO)
#   fstat / close         -> "OK", or the errno name
# ("close" must be last.)

import errno
import os
import sys
import time

path, marker, out, ops = sys.argv[1], sys.argv[2], sys.argv[3], sys.argv[4].split(',')


def report(line):
    with open(out, 'a') as f:
        f.write(line + "\n")


fd = os.open(path, os.O_RDONLY)
report("READY")
while not os.path.exists(marker):
    time.sleep(0.2)
for op in ops:
    try:
        if op == 'seek_data':
            res = str(os.lseek(fd, 0, os.SEEK_DATA))
        elif op == 'seek_hole':
            res = str(os.lseek(fd, 0, os.SEEK_HOLE))
        elif op == 'fstat':
            os.fstat(fd)
            res = "OK"
        elif op == 'close':
            os.close(fd)
            res = "OK"
        else:
            raise ValueError("unknown op " + op)
    except OSError as e:
        res = errno.errorcode.get(e.errno, str(e.errno))
    report("%s=%s" % (op, res))
