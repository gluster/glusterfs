#!/usr/bin/env python

# Copyright (c) 2015 Red Hat, Inc. <http://www.redhat.com/>
# This file is part of GlusterFS.
#
# This file is licensed to you under your choice of the GNU Lesser
# General Public License, version 3 or any later version (LGPLv3 or
# later), or the GNU General Public License, version 2 (GPLv2), in all
# cases as published by the Free Software Foundation.

import sys
import os
import xattr
import uuid
import re
import errno

CHANGELOG_SEARCH_MAX_TRY = 31
DEC_CTIME_START = 5
ROOT_GFID = "00000000-0000-0000-0000-000000000001"
MAX_NUM_CHANGELOGS_TRY = 2


def output_not_found(gfid):
    # Write GFID to stderr
    sys.stderr.write("%s\n" % gfid)


def output_success(path):
    # Write converted Path to Stdout
    sys.stdout.write("%s\n" % path)


def full_dir_path(gfid):
    out_path = ""
    while True:
        path = os.path.join(".glusterfs", gfid[0:2], gfid[2:4], gfid)
        path_readlink = os.readlink(path)
        pgfid = os.path.dirname(path_readlink)
        out_path = os.path.join(os.path.basename(path_readlink), out_path)
        if pgfid == "../../00/00/%s" % ROOT_GFID:
            out_path = os.path.join("./", out_path)
            break
        gfid = os.path.basename(pgfid)
    return out_path


def find_path_from_changelog(fd, gfid):
    """
    In given Changelog File, finds using following pattern
    <T><GFID>\x00<TYPE>\x00<MODE>\x00<UID>\x00<GID>\x00<PARGFID>/<BASENAME>
    Pattern search finds PARGFID and BASENAME, Convert PARGFID to Path
    Using readlink and add basename to form Full path.
    """
    content = fd.read()

    pattern = "E%s" % gfid
    pattern += "\x00(3|23)\x00\d+\x00\d+\x00\d+\x00([^\x00]+)/([^\x00]+)"
    pat = re.compile(pattern)
    match = pat.search(content)

    if match:
        pgfid = match.group(2)
        basename = match.group(3)
        if pgfid == ROOT_GFID:
            return os.path.join("./", basename)
        else:
            full_path_parent = full_dir_path(pgfid)
            if full_path_parent:
                return os.path.join(full_path_parent, basename)

    return None


def gfid_to_path(gfid):
    """
    Try readlink, if it is directory it succeeds.
    Get ctime of the GFID file, Decrement by 5 sec
    Search for Changelog filename, Since Changelog file generated
    every 15 sec, Search and get immediate next Changelog after the file
    Creation. Get the Path by searching in Changelog file.
    Get the resultant file's GFID and Compare with the input, If these
    GFIDs are different then Some thing is changed(May be Rename)
    """
    gfid = gfid.strip()
    gpath = os.path.join(".glusterfs", gfid[0:2], gfid[2:4], gfid)
    try:
        output_success(full_dir_path(gfid))
        return
    except OSError:
        # Not an SymLink
        pass

    try:
        ctime = int(os.stat(gpath).st_ctime)
        ctime -= DEC_CTIME_START
    except (OSError, IOError):
        output_not_found(gfid)
        return

    path = None
    found_changelog = False
    changelog_parse_try = 0
    for i in range(CHANGELOG_SEARCH_MAX_TRY):
        cl = os.path.join(".glusterfs/changelogs", "CHANGELOG.%s" % ctime)

        try:
            with open(cl, "rb") as f:
                changelog_parse_try += 1
                found_changelog = True
                path = find_path_from_changelog(f, gfid)
                if not path and changelog_parse_try < MAX_NUM_CHANGELOGS_TRY:
                    ctime += 1
                    continue
            break
        except (IOError, OSError) as e:
            if e.errno == errno.ENOENT:
                ctime += 1
            else:
                break

    if not found_changelog:
        output_not_found(gfid)
        return

    if not path:
        output_not_found(gfid)
        return
    gfid1 = str(uuid.UUID(bytes=xattr.get(path, "trusted.gfid")))
    if gfid != gfid1:
        output_not_found(gfid)
        return

    output_success(path)


def main():
    num_arguments = 3
    if not sys.stdin.isatty():
        num_arguments = 2

    if len(sys.argv) != num_arguments:
        sys.stderr.write("Invalid arguments\nUsage: "
                         "%s <BRICK_PATH> <GFID_FILE>\n" % sys.argv[0])
        sys.exit(1)

    path = sys.argv[1]

    if sys.stdin.isatty():
        gfid_list = os.path.abspath(sys.argv[2])
        os.chdir(path)
        with open(gfid_list) as f:
            for gfid in f:
                gfid_to_path(gfid)
    else:
        os.chdir(path)
        for gfid in sys.stdin:
            gfid_to_path(gfid)


if __name__ == "__main__":
    main()
