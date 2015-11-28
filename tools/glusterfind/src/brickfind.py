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
import logging
from argparse import ArgumentParser, RawDescriptionHelpFormatter
import urllib
import time

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

        def output_callback(path, filter_result):
            path = path.strip()
            path = path[brick_path_len+1:]
            output_write(fout, path, args.output_prefix,
                         encode=(not args.no_encode), tag=args.tag)

        ignore_dirs = [os.path.join(brick, dirname)
                       for dirname in
                       conf.get_opt("brick_ignore_dirs").split(",")]

        find(brick, callback_func=output_callback,
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
    parser.add_argument("tag", help="Tag to prefix file name with")
    parser.add_argument("--only-query", help="Only query, No session update",
                        action="store_true")
    parser.add_argument("--debug", help="Debug", action="store_true")
    parser.add_argument("--no-encode",
                        help="Do not encode path in outfile",
                        action="store_true")
    parser.add_argument("--output-prefix", help="File prefix in output",
                        default=".")

    return parser.parse_args()


if __name__ == "__main__":
    args = _get_args()
    session_dir = os.path.join(conf.get_opt("session_dir"), args.session)
    status_file = os.path.join(session_dir, args.volume,
                               "%s.status" % urllib.quote_plus(args.brick))
    status_file_pre = status_file + ".pre"
    mkdirp(os.path.join(session_dir, args.volume), exit_on_err=True,
           logger=logger)
    mkdirp(os.path.join(conf.get_opt("log_dir"), args.session, args.volume),
           exit_on_err=True)
    log_file = os.path.join(conf.get_opt("log_dir"),
                            args.session,
                            args.volume,
                            "brickfind.log")
    setup_logger(logger, log_file, args.debug)

    time_to_update = int(time.time())
    brickfind_crawl(args.brick, args)
    if not args.only_query:
        with open(status_file_pre, "w", buffering=0) as f:
            f.write(str(time_to_update))
    sys.exit(0)
