#!/usr/bin/env python2
#
# Copyright (c) 2011-2014 Red Hat, Inc. <http://www.redhat.com>
# This file is part of GlusterFS.

# This file is licensed to you under your choice of the GNU Lesser
# General Public License, version 3 or any later version (LGPLv3 or
# later), or the GNU General Public License, version 2 (GPLv2), in all
# cases as published by the Free Software Foundation.
#

import os
import sys
import stat
import time
import struct
import random
import libcxattr

from errno import EEXIST

Xattr = libcxattr.Xattr()

def umask():
    return os.umask(0)

def _fmt_mknod(l):
    return "!II%dsI%dsIII" % (37, l+1)

def _fmt_mkdir(l):
    return "!II%dsI%dsII" % (37, l+1)

def _fmt_symlink(l1, l2):
    return "!II%dsI%ds%ds" % (37, l1+1, l2+1)

def entry_pack_reg(gf, bn, mo, uid, gid):
    blen = len(bn)
    return struct.pack(_fmt_mknod(blen),
                       uid, gid, gf, mo, bn,
                       stat.S_IMODE(mo), 0, umask())

def entry_pack_dir(gf, bn, mo, uid, gid):
    blen = len(bn)
    return struct.pack(_fmt_mkdir(blen),
                       uid, gid, gf, mo, bn,
                       stat.S_IMODE(mo), umask())

def entry_pack_symlink(gf, bn, lnk, mo, uid, gid):
    blen = len(bn)
    llen = len(lnk)
    return struct.pack(_fmt_symlink(blen, llen),
                       uid, gid, gf, mo, bn, lnk)

if __name__ == '__main__':
    if len(sys.argv) < 9:
        print("USAGE: %s <mount> <pargfid|ROOT> <filename> <GFID> <file type>"
              " <uid> <gid> <file permission(octal str)>" % (sys.argv[0]))
        sys.exit(-1) # nothing to do
    mtpt       = sys.argv[1]
    pargfid    = sys.argv[2]
    fname      = sys.argv[3]
    randomgfid = sys.argv[4]
    ftype      = sys.argv[5]
    uid        = int(sys.argv[6])
    gid        = int(sys.argv[7])
    perm       = int(sys.argv[8],8)

    os.chdir(mtpt)
    if pargfid == 'ROOT':
        pargfid = '.gfid/00000000-0000-0000-0000-000000000001'
    else:
        pargfid = '.gfid/' + pargfid

    blob = None

    # entry op: use non-zero uid/gid (to catch gfid-access xlator bugs)
    if ftype == 'file':
        mode = stat.S_IFREG | perm
        blob = entry_pack_reg(randomgfid, fname, mode, uid, gid)
    elif ftype =='dir':
        mode = stat.S_IFDIR | perm
        blob = entry_pack_dir(randomgfid, fname, mode, uid, gid)
    else: # not yet...
        sys.exit(-1)

    if blob == None:
        sys.exit(-1)
    try:
        Xattr.lsetxattr(pargfid, 'glusterfs.gfid.newfile', blob)
    except OSError:
        ex = sys.exc_info()[1]
        if not ex.errno in [EEXIST]:
            raise
            sys.exit(-1)
    print "File creation OK"
    sys.exit(0)
