#!/usr/bin/env python

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
from errno import ENOENT

from utils import setup_logger, mkdirp
import conf

logger = logging.getLogger()


if __name__ == "__main__":
    # Args: <SESSION> <VOLUME>
    session = sys.argv[1]
    volume = sys.argv[2]

    working_dir = os.path.join(conf.get_opt("working_dir"),
                               session,
                               volume)

    mkdirp(os.path.join(conf.get_opt("log_dir"), session, volume),
           exit_on_err=True)
    log_file = os.path.join(conf.get_opt("log_dir"),
                            session,
                            volume,
                            "changelog.log")

    setup_logger(logger, log_file)

    try:
        def handle_rm_error(func, path, exc_info):
            if exc_info[1].errno == ENOENT:
                return

            raise exc_info[1]

        shutil.rmtree(working_dir, onerror=handle_rm_error)
    except (OSError, IOError) as e:
        logger.error("Failed to delete working directory: %s" % e)
        sys.exit(1)
