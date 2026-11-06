#!/usr/bin/env python3
# Helper for fd-reopen-error-path.t: hold an fd open across a DHT file
# migration, then run fd-based fops on it when told to.
#
# usage: fd-reopen-error-path.py FILE GO-MARKER OUT OP[,OP...]
#
# Opens FILE read-write and appends "READY" to OUT; waits until GO-MARKER
# exists; then runs each OP on the held fd and appends "OP=OK" or
# "OP=<errno name>" to OUT.  Supported OPs: fstat, fallocate, close
# ("close" must be last).

import errno
import os
import sys
import time

path, marker, out, ops = sys.argv[1], sys.argv[2], sys.argv[3], sys.argv[4].split(',')


def report(line):
    with open(out, 'a') as f:
        f.write(line + "\n")


fd = os.open(path, os.O_RDWR)
report("READY")
while not os.path.exists(marker):
    time.sleep(0.2)
for op in ops:
    try:
        if op == 'fstat':
            os.fstat(fd)
        elif op == 'fallocate':
            os.posix_fallocate(fd, 0, 8192)
        elif op == 'close':
            os.close(fd)
        else:
            raise ValueError("unknown op " + op)
        res = "OK"
    except OSError as e:
        res = errno.errorcode.get(e.errno, str(e.errno))
    report("%s=%s" % (op, res))
