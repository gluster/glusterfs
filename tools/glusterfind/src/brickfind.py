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
import logging
from argparse import ArgumentParser, RawDescriptionHelpFormatter
from errno import ENOENT

from utils import mkdirp, setup_logger, create_file, output_write, find
import conf


PROG_DESCRIPTION = """
Changelog Crawler
"""

logger = logging.getLogger()


def brickfind_crawl(brick, args):
    if brick.endswith("/"):
        brick = brick[0:len(brick)-1]

    working_dir = os.path.dirname(args.outfile)
    mkdirp(working_dir, exit_on_err=True, logger=logger)
    create_file(args.outfile, exit_on_err=True, logger=logger)

    with open(args.outfile, "a+") as fout:
        brick_path_len = len(brick)

        def mtime_filter(path):
            try:
                st = os.lstat(path)
            except (OSError, IOError) as e:
                if e.errno == ENOENT:
                    st = None
                else:
                    raise

            if st and (st.st_mtime > args.start or st.st_ctime > args.start):
                return True

            return False

        def output_callback(path):
            path = path.strip()
            path = path[brick_path_len+1:]
            output_write(fout, path, args.output_prefix)

        ignore_dirs = [os.path.join(brick, dirname)
                       for dirname in
                       conf.get_opt("brick_ignore_dirs").split(",")]

        if args.full:
            find(brick, callback_func=output_callback,
                 ignore_dirs=ignore_dirs)
        else:
            find(brick, callback_func=output_callback,
                 filter_func=mtime_filter,
                 ignore_dirs=ignore_dirs)

        fout.flush()
        os.fsync(fout.fileno())


def _get_args():
    parser = ArgumentParser(formatter_class=RawDescriptionHelpFormatter,
                            description=PROG_DESCRIPTION)

    parser.add_argument("session", help="Session Name")
    parser.add_argument("volume", help="Volume Name")
    parser.add_argument("brick", help="Brick Name")
    parser.add_argument("outfile", help="Output File")
    parser.add_argument("start", help="Start Time", type=float)
    parser.add_argument("--debug", help="Debug", action="store_true")
    parser.add_argument("--full", help="Full Find", action="store_true")
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
                            "brickfind.log")
    setup_logger(logger, log_file, args.debug)
    brickfind_crawl(args.brick, args)
    sys.exit(0)
