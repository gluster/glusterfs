#!/usr/bin/env python
# -*- coding: utf-8 -*-

# Copyright (c) 2015 Red Hat, Inc. <http://www.redhat.com/>
# This file is part of GlusterFS.
#
# This file is licensed to you under your choice of the GNU Lesser
# General Public License, version 3 or any later version (LGPLv3 or
# later), or the GNU General Public License, version 2 (GPLv2), in all
# cases as published by the Free Software Foundation.

import os
import sys
import time
import xattr
import logging
from argparse import ArgumentParser, RawDescriptionHelpFormatter
import hashlib
import urllib
import codecs

import libgfchangelog
from utils import mkdirp, symlink_gfid_to_path
from utils import fail, setup_logger, find
from utils import get_changelog_rollover_time
from utils import output_path_prepare
from changelogdata import ChangelogData
import conf


CHANGELOG_LOG_LEVEL = 9
CHANGELOG_CONN_RETRIES = 5
CHANGELOGAPI_NUM_WORKERS = 3
PROG_DESCRIPTION = """
Changelog Crawler
"""
history_turns = 0
history_turn_time = 0

logger = logging.getLogger()




def pgfid_to_path(brick, changelog_data):
    """
    For all the pgfids in table, converts into path using recursive
    readlink.
    """
    # pgfid1 to path1 in case of CREATE/MKNOD/MKDIR/LINK/SYMLINK
    for row in changelog_data.gfidpath_get_distinct("pgfid1", {"path1": ""}):
        # In case of Data/Metadata only, pgfid1 will not be their
        if row[0] == "":
            continue

        try:
            path = symlink_gfid_to_path(brick, row[0])
            path = output_path_prepare(path, args)
            changelog_data.gfidpath_set_path1(path, row[0])
        except (IOError, OSError) as e:
            logger.warn("Error converting to path: %s" % e)
            continue

    # pgfid2 to path2 in case of RENAME
    for row in changelog_data.gfidpath_get_distinct("pgfid2",
                                                    {"type": "RENAME",
                                                     "path2": ""}):
        # Only in case of Rename pgfid2 exists
        if row[0] == "":
            continue

        try:
            path = symlink_gfid_to_path(brick, row[0])
            path = output_path_prepare(path, args)
            changelog_data.gfidpath_set_path2(path, row[0])
        except (IOError, OSError) as e:
            logger.warn("Error converting to path: %s" % e)
            continue


def populate_pgfid_and_inodegfid(brick, changelog_data):
    """
    For all the DATA/METADATA modifications GFID,
    If symlink, directly convert to Path using Readlink.
    If not symlink, try to get PGFIDs via xattr query and populate it
    to pgfid table, collect inodes in inodegfid table
    """
    for row in changelog_data.gfidpath_get({"path1": "", "type": "MODIFY"}):
        gfid = row[3].strip()
        p = os.path.join(brick, ".glusterfs", gfid[0:2], gfid[2:4], gfid)
        if os.path.islink(p):
            # It is a Directory if GFID backend path is symlink
            try:
                path = symlink_gfid_to_path(brick, gfid)
                path = output_path_prepare(path, args)
                changelog_data.gfidpath_update({"path1": path},
                                                {"gfid": gfid})
            except (IOError, OSError) as e:
                logger.warn("Error converting to path: %s" % e)
                continue
        else:
            try:
                # INODE and GFID to inodegfid table
                changelog_data.inodegfid_add(os.stat(p).st_ino, gfid)
                file_xattrs = xattr.list(p)
                for x in file_xattrs:
                    if x.startswith("trusted.pgfid."):
                        # PGFID in pgfid table
                        changelog_data.pgfid_add(x.split(".")[-1])
            except (IOError, OSError):
                # All OS Errors ignored, since failures will be logged
                # in End. All GFIDs present in gfidpath table
                continue


def gfid_to_path_using_pgfid(brick, changelog_data, args):
    """
    For all the pgfids collected, Converts to Path and
    does readdir on those directories and looks up inodegfid
    table for matching inode number.
    """
    populate_pgfid_and_inodegfid(brick, changelog_data)

    # If no GFIDs needs conversion to Path
    if not changelog_data.inodegfid_exists({"converted": 0}):
        return

    def inode_filter(path):
        # Looks in inodegfid table, if exists returns
        # inode number else None
        try:
            st = os.lstat(path)
        except (OSError, IOError):
            st = None

        if st and changelog_data.inodegfid_exists({"inode": st.st_ino}):
            return st.st_ino

        return None

    # Length of brick path, to remove from output path
    brick_path_len = len(brick)

    def output_callback(path, inode):
        # For each path found, encodes it and updates path1
        # Also updates converted flag in inodegfid table as 1
        path = path.strip()
        path = path[brick_path_len+1:]

        path = output_path_prepare(path, args)

        changelog_data.append_path1(path, inode)
        changelog_data.inodegfid_update({"converted": 1}, {"inode": inode})

    ignore_dirs = [os.path.join(brick, dirname)
                   for dirname in
                   conf.get_opt("brick_ignore_dirs").split(",")]

    for row in changelog_data.pgfid_get():
        try:
            path = symlink_gfid_to_path(brick, row[0])
            find(os.path.join(brick, path),
                callback_func=output_callback,
                filter_func=inode_filter,
                ignore_dirs=ignore_dirs,
                subdirs_crawl=False)
        except (IOError, OSError) as e:
            logger.warn("Error converting to path: %s" % e)
            continue


def gfid_to_path_using_batchfind(brick, changelog_data):
    # If all the GFIDs converted using gfid_to_path_using_pgfid
    if not changelog_data.inodegfid_exists({"converted": 0}):
        return

    def inode_filter(path):
        # Looks in inodegfid table, if exists returns
        # inode number else None
        try:
            st = os.lstat(path)
        except (OSError, IOError):
            st = None

        if st and changelog_data.inodegfid_exists({"inode": st.st_ino}):
            return st.st_ino

        return None

    # Length of brick path, to remove from output path
    brick_path_len = len(brick)

    def output_callback(path, inode):
        # For each path found, encodes it and updates path1
        # Also updates converted flag in inodegfid table as 1
        path = path.strip()
        path = path[brick_path_len+1:]
        path = output_path_prepare(path, args)

        changelog_data.append_path1(path, inode)

    ignore_dirs = [os.path.join(brick, dirname)
                   for dirname in
                   conf.get_opt("brick_ignore_dirs").split(",")]

    # Full Namespace Crawl
    find(brick, callback_func=output_callback,
         filter_func=inode_filter,
         ignore_dirs=ignore_dirs)


def parse_changelog_to_db(changelog_data, filename, args):
    """
    Parses a Changelog file and populates data in gfidpath table
    """
    with codecs.open(filename, encoding="utf-8") as f:
        changelogfile = os.path.basename(filename)
        for line in f:
            data = line.strip().split(" ")
            if data[0] == "E" and data[2] in ["CREATE", "MKNOD", "MKDIR"]:
                # CREATE/MKDIR/MKNOD
                changelog_data.when_create_mknod_mkdir(changelogfile, data)
            elif data[0] in ["D", "M"]:
                # DATA/META
                if not args.only_namespace_changes:
                    changelog_data.when_data_meta(changelogfile, data)
            elif data[0] == "E" and data[2] in ["LINK", "SYMLINK"]:
                # LINK/SYMLINK
                changelog_data.when_link_symlink(changelogfile, data)
            elif data[0] == "E" and data[2] == "RENAME":
                # RENAME
                changelog_data.when_rename(changelogfile, data)
            elif data[0] == "E" and data[2] in ["UNLINK", "RMDIR"]:
                # UNLINK/RMDIR
                changelog_data.when_unlink_rmdir(changelogfile, data)


def get_changes(brick, hash_dir, log_file, start, end, args):
    """
    Makes use of libgfchangelog's history API to get changelogs
    containing changes from start and end time. Further collects
    the modified gfids from the changelogs and writes the list
    of gfid to 'gfid_list' file.
    """
    session_dir = os.path.join(conf.get_opt("session_dir"),
                               args.session)
    status_file = os.path.join(session_dir, args.volume,
                               "%s.status" % urllib.quote_plus(args.brick))

    # Get previous session
    try:
        with open(status_file) as f:
            start = int(f.read().strip())
    except (ValueError, OSError, IOError):
        start = args.start

    try:
        libgfchangelog.cl_init()
        libgfchangelog.cl_register(brick, hash_dir, log_file,
                                   CHANGELOG_LOG_LEVEL, CHANGELOG_CONN_RETRIES)
    except libgfchangelog.ChangelogException as e:
        fail("%s Changelog register failed: %s" % (brick, e), logger=logger)

    # Output files to record GFIDs and GFID to Path failure GFIDs
    changelog_data = ChangelogData(args.outfile, args)

    # Changelogs path(Hard coded to BRICK/.glusterfs/changelogs
    cl_path = os.path.join(brick, ".glusterfs/changelogs")

    # Fail if History fails for requested Start and End
    try:
        actual_end = libgfchangelog.cl_history_changelog(
            cl_path, start, end, CHANGELOGAPI_NUM_WORKERS)
    except libgfchangelog.ChangelogException as e:
        fail("%s Historical Changelogs not available: %s" % (brick, e),
             logger=logger)

    try:
        # scan followed by getchanges till scan returns zero.
        # history_scan() is blocking call, till it gets the number
        # of changelogs to process. Returns zero when no changelogs
        # to be processed. returns positive value as number of changelogs
        # to be processed, which will be fetched using
        # history_getchanges()
        changes = []
        while libgfchangelog.cl_history_scan() > 0:
            changes = libgfchangelog.cl_history_getchanges()

            for change in changes:
                # Ignore if last processed changelog comes
                # again in list
                if change.endswith(".%s" % start):
                    continue
                try:
                    parse_changelog_to_db(changelog_data, change, args)
                    libgfchangelog.cl_history_done(change)
                except IOError as e:
                    logger.warn("Error parsing changelog file %s: %s" %
                        (change, e))

            changelog_data.commit()
    except libgfchangelog.ChangelogException as e:
        fail("%s Error during Changelog Crawl: %s" % (brick, e),
             logger=logger)

    # Convert all pgfid available from Changelogs
    pgfid_to_path(brick, changelog_data)
    changelog_data.commit()

    # Convert all GFIDs for which no other additional details available
    gfid_to_path_using_pgfid(brick, changelog_data, args)
    changelog_data.commit()

    # If some GFIDs fail to get converted from previous step,
    # convert using find
    gfid_to_path_using_batchfind(brick, changelog_data)
    changelog_data.commit()

    return actual_end


def changelog_crawl(brick, start, end, args):
    """
    Init function, prepares working dir and calls Changelog query
    """
    if brick.endswith("/"):
        brick = brick[0:len(brick)-1]

    # WORKING_DIR/BRICKHASH/OUTFILE
    working_dir = os.path.dirname(args.outfile)
    brickhash = hashlib.sha1(brick)
    brickhash = str(brickhash.hexdigest())
    working_dir = os.path.join(working_dir, brickhash)

    mkdirp(working_dir, exit_on_err=True, logger=logger)

    log_file = os.path.join(conf.get_opt("log_dir"),
                            args.session,
                            args.volume,
                            "changelog.%s.log" % brickhash)

    logger.info("%s Started Changelog Crawl. Start: %s, End: %s"
                % (brick, start, end))
    return get_changes(brick, working_dir, log_file, start, end, args)


def _get_args():
    parser = ArgumentParser(formatter_class=RawDescriptionHelpFormatter,
                            description=PROG_DESCRIPTION)

    parser.add_argument("session", help="Session Name")
    parser.add_argument("volume", help="Volume Name")
    parser.add_argument("brick", help="Brick Name")
    parser.add_argument("outfile", help="Output File")
    parser.add_argument("start", help="Start Time", type=int)
    parser.add_argument("--only-query", help="Query mode only (no session)",
                        action="store_true")
    parser.add_argument("--debug", help="Debug", action="store_true")
    parser.add_argument("--no-encode",
                        help="Do not encode path in outfile",
                        action="store_true")
    parser.add_argument("--output-prefix", help="File prefix in output",
                        default=".")
    parser.add_argument("-N", "--only-namespace-changes",
                        help="List only namespace changes",
                        action="store_true")

    return parser.parse_args()


if __name__ == "__main__":
    args = _get_args()
    mkdirp(os.path.join(conf.get_opt("log_dir"), args.session, args.volume),
           exit_on_err=True)
    log_file = os.path.join(conf.get_opt("log_dir"),
                            args.session,
                            args.volume,
                            "changelog.log")
    setup_logger(logger, log_file, args.debug)

    session_dir = os.path.join(conf.get_opt("session_dir"), args.session)
    status_file = os.path.join(session_dir, args.volume,
                               "%s.status" % urllib.quote_plus(args.brick))
    status_file_pre = status_file + ".pre"
    mkdirp(os.path.join(session_dir, args.volume), exit_on_err=True,
           logger=logger)

    if args.only_query:
        start = args.start
    else:
        try:
            with open(status_file) as f:
                start = int(f.read().strip())
        except (ValueError, OSError, IOError):
            start = args.start

    end = int(time.time()) - get_changelog_rollover_time(args.volume)
    logger.info("%s Started Changelog Crawl - Start: %s End: %s" % (args.brick,
                                                                    start,
                                                                    end))
    actual_end = changelog_crawl(args.brick, start, end, args)
    if not args.only_query:
        with open(status_file_pre, "w", buffering=0) as f:
            f.write(str(actual_end))

    logger.info("%s Finished Changelog Crawl - End: %s" % (args.brick,
                                                           actual_end))
    sys.exit(0)
