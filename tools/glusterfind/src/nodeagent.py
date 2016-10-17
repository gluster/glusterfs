#!/usr/bin/env python
# -*- coding: utf-8 -*-

# Copyright (c) 2015 Red Hat, Inc. <http://www.redhat.com/>
# This file is part of GlusterFS.
#
# This file is licensed to you under your choice of the GNU Lesser
# General Public License, version 3 or any later version (LGPLv3 or
# later), or the GNU General Public License, version 2 (GPLv2), in all
# cases as published by the Free Software Foundation.

import shutil
import sys
import os
import logging
from argparse import ArgumentParser, RawDescriptionHelpFormatter
import urllib
from errno import ENOTEMPTY

from utils import setup_logger, mkdirp, handle_rm_error
import conf

logger = logging.getLogger()


def mode_cleanup(args):
    working_dir = os.path.join(conf.get_opt("working_dir"),
                               args.session,
                               args.volume,
                               args.tmpfilename)

    mkdirp(os.path.join(conf.get_opt("log_dir"), args.session, args.volume),
           exit_on_err=True)
    log_file = os.path.join(conf.get_opt("log_dir"),
                            args.session,
                            args.volume,
                            "changelog.log")

    setup_logger(logger, log_file)

    try:
        shutil.rmtree(working_dir, onerror=handle_rm_error)
    except (OSError, IOError) as e:
        logger.error("Failed to delete working directory: %s" % e)
        sys.exit(1)


def mode_create(args):
    session_dir = os.path.join(conf.get_opt("session_dir"),
                               args.session)
    status_file = os.path.join(session_dir, args.volume,
                               "%s.status" % urllib.quote_plus(args.brick))

    mkdirp(os.path.join(session_dir, args.volume), exit_on_err=True,
           logger=logger)

    if not os.path.exists(status_file) or args.reset_session_time:
        with open(status_file, "w", buffering=0) as f:
            f.write(args.time_to_update)

    sys.exit(0)


def mode_post(args):
    session_dir = os.path.join(conf.get_opt("session_dir"), args.session)
    status_file = os.path.join(session_dir, args.volume,
                               "%s.status" % urllib.quote_plus(args.brick))

    mkdirp(os.path.join(session_dir, args.volume), exit_on_err=True,
           logger=logger)
    status_file_pre = status_file + ".pre"

    if os.path.exists(status_file_pre):
        os.rename(status_file_pre, status_file)
        sys.exit(0)


def mode_delete(args):
    session_dir = os.path.join(conf.get_opt("session_dir"),
                               args.session)
    shutil.rmtree(os.path.join(session_dir, args.volume),
                  onerror=handle_rm_error)

    # If the session contains only this volume, then cleanup the
    # session directory. If a session contains multiple volumes
    # then os.rmdir will fail with ENOTEMPTY
    try:
        os.rmdir(session_dir)
    except OSError as e:
        if not e.errno == ENOTEMPTY:
            logger.warn("Failed to delete session directory: %s" % e)


def _get_args():
    parser = ArgumentParser(formatter_class=RawDescriptionHelpFormatter,
                            description="Node Agent")
    subparsers = parser.add_subparsers(dest="mode")

    parser_cleanup = subparsers.add_parser('cleanup')
    parser_cleanup.add_argument("session", help="Session Name")
    parser_cleanup.add_argument("volume", help="Volume Name")
    parser_cleanup.add_argument("tmpfilename", help="Temporary File Name")
    parser_cleanup.add_argument("--debug", help="Debug", action="store_true")

    parser_session_create = subparsers.add_parser('create')
    parser_session_create.add_argument("session", help="Session Name")
    parser_session_create.add_argument("volume", help="Volume Name")
    parser_session_create.add_argument("brick", help="Brick Path")
    parser_session_create.add_argument("time_to_update", help="Time to Update")
    parser_session_create.add_argument("--reset-session-time",
                                       help="Reset Session Time",
                                       action="store_true")
    parser_session_create.add_argument("--debug", help="Debug",
                                       action="store_true")

    parser_post = subparsers.add_parser('post')
    parser_post.add_argument("session", help="Session Name")
    parser_post.add_argument("volume", help="Volume Name")
    parser_post.add_argument("brick", help="Brick Path")
    parser_post.add_argument("--debug", help="Debug",
                             action="store_true")

    parser_delete = subparsers.add_parser('delete')
    parser_delete.add_argument("session", help="Session Name")
    parser_delete.add_argument("volume", help="Volume Name")
    parser_delete.add_argument("--debug", help="Debug",
                               action="store_true")
    return parser.parse_args()


if __name__ == "__main__":
    args = _get_args()

    # globals() will have all the functions already defined.
    # mode_<args.mode> will be the function name to be called
    globals()["mode_" + args.mode](args)
