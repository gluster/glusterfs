#!/usr/bin/env python

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
from errno import ENOENT
import logging
from argparse import ArgumentParser, RawDescriptionHelpFormatter
import hashlib
import urllib

import libgfchangelog
from utils import create_file, mkdirp, execute, symlink_gfid_to_path
from utils import fail, setup_logger, output_write, find
from utils import get_changelog_rollover_time
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


def gfid_to_path_using_batchfind(brick, gfids_file, output_file):
    """
    find -samefile gets the inode number and crawls entire namespace
    to get the list of files/dirs having same inode number.
    Do find without any option, except the ignore directory option,
    print the output in <INODE_NUM> <PATH> format, use this output
    to look into in-memory dictionary of inode numbers got from the
    list of GFIDs
    """
    with open(output_file, "a+") as fout:
        inode_dict = {}
        with open(gfids_file) as f:
            for gfid in f:
                gfid = gfid.strip()
                backend_path = os.path.join(brick, ".glusterfs",
                                            gfid[0:2], gfid[2:4], gfid)

                try:
                    inode_dict[str(os.stat(backend_path).st_ino)] = 1
                except (IOError, OSError) as e:
                    if e.errno == ENOENT:
                        continue
                    else:
                        fail("%s Failed to convert to path from "
                             "GFID %s: %s" % (brick, gfid, e), logger=logger)

        if not inode_dict:
            return

        def inode_filter(path):
            try:
                st = os.lstat(path)
            except (OSError, IOError) as e:
                if e.errno == ENOENT:
                    st = None
                else:
                    raise

            if st and inode_dict.get(str(st.st_ino), None):
                return True

            return False

        brick_path_len = len(brick)

        def output_callback(path):
            path = path.strip()
            path = path[brick_path_len+1:]
            output_write(fout, path, args.output_prefix)

        ignore_dirs = [os.path.join(brick, dirname)
                       for dirname in
                       conf.get_opt("brick_ignore_dirs").split(",")]
        # Length of brick path, to remove from output path
        find(brick, callback_func=output_callback,
             filter_func=inode_filter,
             ignore_dirs=ignore_dirs)

        fout.flush()
        os.fsync(fout.fileno())


def gfid_to_path_using_pgfid(brick, gfids_file, output_file, outfile_failures):
    """
    Parent GFID is saved as xattr, collect Parent GFIDs from all
    the files from gfids_file. Convert parent GFID to path and Crawl
    each directories to get the list of files/dirs having same inode number.
    Do find with maxdepth as 1 and print the output in <INODE_NUM> <PATH>
    format, use this output to look into in memory dictionary of inode
    numbers got from the list of GFIDs
    """
    with open(output_file, "a+") as fout:
        pgfids = set()
        inode_dict = {}
        with open(gfids_file) as f:
            for gfid in f:
                gfid = gfid.strip()
                p = os.path.join(brick,
                                 ".glusterfs",
                                 gfid[0:2],
                                 gfid[2:4],
                                 gfid)
                if os.path.islink(p):
                    path = symlink_gfid_to_path(brick, gfid)
                    output_write(fout, path, args.output_prefix)
                else:
                    try:
                        inode_dict[str(os.stat(p).st_ino)] = 1
                        file_xattrs = xattr.list(p)
                        num_parent_gfid = 0
                        for x in file_xattrs:
                            if x.startswith("trusted.pgfid."):
                                num_parent_gfid += 1
                                pgfids.add(x.split(".")[-1])

                        if num_parent_gfid == 0:
                            with open(outfile_failures, "a") as f:
                                f.write("%s\n" % gfid)
                                f.flush()
                                os.fsync(f.fileno())

                    except (IOError, OSError) as e:
                        if e.errno == ENOENT:
                            continue
                        else:
                            fail("%s Failed to convert to path from "
                                 "GFID %s: %s" % (brick, gfid, e),
                                 logger=logger)

        if not inode_dict:
            return

        def inode_filter(path):
            try:
                st = os.lstat(path)
            except (OSError, IOError) as e:
                if e.errno == ENOENT:
                    st = None
                else:
                    raise

            if st and inode_dict.get(str(st.st_ino), None):
                return True

            return False

        # Length of brick path, to remove from output path
        brick_path_len = len(brick)

        def output_callback(path):
            path = path.strip()
            path = path[brick_path_len+1:]
            output_write(fout, path, args.output_prefix)

        ignore_dirs = [os.path.join(brick, dirname)
                       for dirname in
                       conf.get_opt("brick_ignore_dirs").split(",")]

        for pgfid in pgfids:
            path = symlink_gfid_to_path(brick, pgfid)
            find(os.path.join(brick, path),
                 callback_func=output_callback,
                 filter_func=inode_filter,
                 ignore_dirs=ignore_dirs,
                 subdirs_crawl=False)

        fout.flush()
        os.fsync(fout.fileno())


def sort_unique(filename):
    execute(["sort", "-u", "-o", filename, filename],
            exit_msg="Sort failed", logger=logger)


def get_changes(brick, hash_dir, log_file, start, end, args):
    """
    Makes use of libgfchangelog's history API to get changelogs
    containing changes from start and end time. Further collects
    the modified gfids from the changelogs and writes the list
    of gfid to 'gfid_list' file.
    """
    try:
        libgfchangelog.cl_init()
        libgfchangelog.cl_register(brick, hash_dir, log_file,
                                   CHANGELOG_LOG_LEVEL, CHANGELOG_CONN_RETRIES)
    except libgfchangelog.ChangelogException as e:
        fail("%s Changelog register failed: %s" % (brick, e), logger=logger)

    # Output files to record GFIDs and GFID to Path failure GFIDs
    gfid_list_path = args.outfile + ".gfids"
    gfid_list_failures_file = gfid_list_path + ".failures"
    create_file(gfid_list_path, exit_on_err=True, logger=logger)
    create_file(gfid_list_failures_file, exit_on_err=True, logger=logger)

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
            changes += libgfchangelog.cl_history_getchanges()

            if changes:
                with open(gfid_list_path, 'a+') as fgfid:
                    for change in changes:
                        # Ignore if last processed changelog comes
                        # again in list
                        if change.endswith(".%s" % start):
                            continue

                        with open(change) as f:
                            for line in f:
                                # Space delimited list, collect GFID
                                details = line.split()
                                fgfid.write("%s\n" % details[1])

                        libgfchangelog.cl_history_done(change)
                    fgfid.flush()
                    os.fsync(fgfid.fileno())
    except libgfchangelog.ChangelogException as e:
        fail("%s Error during Changelog Crawl: %s" % (brick, e),
             logger=logger)

    # If TS returned from history_changelog is < end time
    # then FS crawl may be required, since history is only available
    # till TS returned from history_changelog
    if actual_end < end:
        fail("Partial History available with Changelog", 2, logger=logger)

    sort_unique(gfid_list_path)
    gfid_to_path_using_pgfid(brick, gfid_list_path,
                             args.outfile, gfid_list_failures_file)
    gfid_to_path_using_batchfind(brick, gfid_list_failures_file, args.outfile)

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
    create_file(args.outfile, exit_on_err=True, logger=logger)
    create_file(args.outfile + ".gfids", exit_on_err=True, logger=logger)

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
    parser.add_argument("--debug", help="Debug", action="store_true")
    parser.add_argument("--output-prefix", help="File prefix in output",
                        default=".")

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

    try:
        with open(status_file) as f:
            start = int(f.read().strip())
    except (ValueError, OSError, IOError):
        start = args.start

    end = int(time.time()) - get_changelog_rollover_time(args.volume)
    actual_end = changelog_crawl(args.brick, start, end, args)
    with open(status_file_pre, "w", buffering=0) as f:
        f.write(str(actual_end))

    sys.exit(0)
