#!/usr/bin/python3
# -*- coding: utf-8 -*-
#
#  Copyright (c) 2016 Red Hat, Inc. <http://www.redhat.com>
#  This file is part of GlusterFS.
#
#  This file is licensed to you under your choice of the GNU Lesser
#  General Public License, version 3 or any later version (LGPLv3 or
#  later), or the GNU General Public License, version 2 (GPLv2), in all
#  cases as published by the Free Software Foundation.
#

from argparse import ArgumentParser
import time
import os
from errno import EEXIST
import sys
import logging

from logutils import setup_logging
import gsyncdconfig as gconf
from rconf import rconf
import subcmds
from conf import GLUSTERD_WORKDIR, GLUSTERFS_CONFDIR, GCONF_VERSION
from syncdutils import (set_term_handler, finalize, lf,
                        log_raise_exception, FreeObject, escape)
import argsupgrade


GSYNCD_VERSION = "gsyncd.py %s.0" % GCONF_VERSION


def main():
    rconf.starttime = time.time()

    # If old Glusterd sends commands in old format, below function
    # converts the sys.argv to new format. This conversion is added
    # temporarily for backward compatibility. This can be removed
    # once integrated with Glusterd2
    # This modifies sys.argv globally, so rest of the code works as usual
    argsupgrade.upgrade()

    # Default argparse version handler prints to stderr, which is fixed in
    # 3.x series but not in 2.x, using custom parser to fix this issue
    if "--version" in sys.argv:
        print(GSYNCD_VERSION)
        sys.exit(0)

    parser = ArgumentParser()
    parser.add_argument("--inet6", action="store_true")
    sp = parser.add_subparsers(dest="subcmd")

    # Monitor Status File update
    p = sp.add_parser("monitor-status")
    p.add_argument("primary", help="Primary Volume Name")
    p.add_argument("secondary", help="Secondary details user@host::vol format")
    p.add_argument("status", help="Update Monitor Status")
    p.add_argument("-c", "--config-file", help="Config File")
    p.add_argument("--debug", action="store_true")

    # Monitor
    p = sp.add_parser("monitor")
    p.add_argument("primary", help="Primary Volume Name")
    p.add_argument("secondary", help="Secondary details user@host::vol format")
    p.add_argument("-c", "--config-file", help="Config File")
    p.add_argument("--pause-on-start",
                   action="store_true",
                   help="Start with Paused state")
    p.add_argument("--local-node-id", help="Local Node ID")
    p.add_argument("--debug", action="store_true")
    p.add_argument("--use-gconf-volinfo", action="store_true")

    # Worker
    p = sp.add_parser("worker")
    p.add_argument("primary", help="Primary Volume Name")
    p.add_argument("secondary", help="Secondary details user@host::vol format")
    p.add_argument("--local-path", help="Local Brick Path")
    p.add_argument("--feedback-fd", type=int,
                   help="feedback fd between monitor and worker")
    p.add_argument("--local-node", help="Local primary node")
    p.add_argument("--local-node-id", help="Local Node ID")
    p.add_argument("--subvol-num", type=int, help="Subvolume number")
    p.add_argument("--is-hottier", action="store_true",
                   help="Is this brick part of hot tier")
    p.add_argument("--resource-remote",
                   help="Remote node to connect to Secondary Volume")
    p.add_argument("--resource-remote-id",
                   help="Remote node ID to connect to Secondary Volume")
    p.add_argument("--secondary-id", help="Secondary Volume ID")
    p.add_argument("-c", "--config-file", help="Config File")
    p.add_argument("--debug", action="store_true")

    # Secondary
    p = sp.add_parser("secondary")
    p.add_argument("primary", help="Primary Volume Name")
    p.add_argument("secondary", help="Secondary details user@host::vol format")
    p.add_argument("--session-owner")
    p.add_argument("--primary-brick",
                   help="Primary brick which is connected to the Secondary")
    p.add_argument("--primary-node",
                   help="Primary node which is connected to the Secondary")
    p.add_argument("--primary-node-id",
                   help="Primary node ID which is connected to the Secondary")
    p.add_argument("--local-node", help="Local Secondary node")
    p.add_argument("--local-node-id", help="Local Secondary ID")
    p.add_argument("-c", "--config-file", help="Config File")
    p.add_argument("--debug", action="store_true")

    # All configurations which are configured via "secondary-" options
    # DO NOT add default values for these configurations, default values
    # will be picked from template config file
    p.add_argument("--secondary-timeout", type=int,
                   help="Timeout to end gsyncd at Secondary side")
    p.add_argument("--use-rsync-xattrs", action="store_true")
    p.add_argument("--secondary-log-level", help="Secondary Gsyncd Log level")
    p.add_argument("--secondary-gluster-log-level",
                   help="Secondary Gluster mount Log level")
    p.add_argument("--secondary-gluster-command-dir",
                   help="Directory where Gluster binaries exist on secondary")
    p.add_argument("--secondary-access-mount", action="store_true",
                   help="Do not lazy umount the secondary volume")
    p.add_argument("--primary-dist-count", type=int,
                   help="Primary Distribution count")

    # Status
    p = sp.add_parser("status")
    p.add_argument("primary", help="Primary Volume Name")
    p.add_argument("secondary", help="Secondary")
    p.add_argument("-c", "--config-file", help="Config File")
    p.add_argument("--local-path", help="Local Brick Path")
    p.add_argument("--debug", action="store_true")
    p.add_argument("--json", action="store_true")

    # Config-check
    p = sp.add_parser("config-check")
    p.add_argument("name", help="Config Name")
    p.add_argument("--value", help="Config Value")
    p.add_argument("--debug", action="store_true")

    # Config-get
    p = sp.add_parser("config-get")
    p.add_argument("primary", help="Primary Volume Name")
    p.add_argument("secondary", help="Secondary")
    p.add_argument("--name", help="Config Name")
    p.add_argument("-c", "--config-file", help="Config File")
    p.add_argument("--debug", action="store_true")
    p.add_argument("--show-defaults", action="store_true")
    p.add_argument("--only-value", action="store_true")
    p.add_argument("--use-underscore", action="store_true")
    p.add_argument("--json", action="store_true")

    # Config-set
    p = sp.add_parser("config-set")
    p.add_argument("primary", help="Primary Volume Name")
    p.add_argument("secondary", help="Secondary")
    p.add_argument("-n", "--name", help="Config Name")
    p.add_argument("-v", "--value", help="Config Value")
    p.add_argument("-c", "--config-file", help="Config File")
    p.add_argument("--debug", action="store_true")

    # Config-reset
    p = sp.add_parser("config-reset")
    p.add_argument("primary", help="Primary Volume Name")
    p.add_argument("secondary", help="Secondary")
    p.add_argument("name", help="Config Name")
    p.add_argument("-c", "--config-file", help="Config File")
    p.add_argument("--debug", action="store_true")

    # voluuidget
    p = sp.add_parser("voluuidget")
    p.add_argument("host", help="Hostname")
    p.add_argument("volname", help="Volume Name")
    p.add_argument("--debug", action="store_true")

    # Delete
    p = sp.add_parser("delete")
    p.add_argument("primary", help="Primary Volume Name")
    p.add_argument("secondary", help="Secondary")
    p.add_argument("-c", "--config-file", help="Config File")
    p.add_argument('--path', dest='paths', action="append")
    p.add_argument("--reset-sync-time", action="store_true",
                   help="Reset Sync Time")
    p.add_argument("--debug", action="store_true")

    # Parse arguments
    args = parser.parse_args()

    # Extra template values, All arguments are already part of template
    # variables, use this for adding extra variables
    extra_tmpl_args = {}

    # Add First/Primary Secondary host, user and volume
    if getattr(args, "secondary", None) is not None:
        hostdata, secondaryvol = args.secondary.split("::")
        hostdata = hostdata.split("@")
        secondaryhost = hostdata[-1]
        secondaryuser = "root"
        if len(hostdata) == 2:
            secondaryuser = hostdata[0]
        extra_tmpl_args["primary_secondary_host"] = secondaryhost
        extra_tmpl_args["secondaryuser"] = secondaryuser
        extra_tmpl_args["secondaryvol"] = secondaryvol

    # Add Bricks encoded path
    if getattr(args, "local_path", None) is not None:
        extra_tmpl_args["local_id"] = escape(args.local_path)

    # Add Primary Bricks encoded path(For Secondary)
    if getattr(args, "primary_brick", None) is not None:
        extra_tmpl_args["primary_brick_id"] = escape(args.primary_brick)

    # Load configurations
    config_file = getattr(args, "config_file", None)

    # Subcmd accepts config file argument but not passed
    # Set default path for config file in that case
    # If an subcmd accepts config file then it also accepts
    # primary and Secondary arguments.
    if config_file is None and hasattr(args, "config_file") \
        and args.subcmd != "secondary":
        config_file = "%s/geo-replication/%s_%s_%s/gsyncd.conf" % (
            GLUSTERD_WORKDIR,
            args.primary,
            extra_tmpl_args["primary_secondary_host"],
            extra_tmpl_args["secondaryvol"])

    # If Config file path not exists, log error and continue using default conf
    config_file_error_msg = None
    if config_file is not None and not os.path.exists(config_file):
        # Logging not yet initialized, create the error message to
        # log later and reset the config_file to None
        config_file_error_msg = lf(
            "Session config file not exists, using the default config",
            path=config_file)
        config_file = None

    rconf.config_file = config_file

    # Override gconf values from argument values only if it is secondary gsyncd
    override_from_args = False
    if args.subcmd == "secondary":
        override_from_args = True

    if config_file is not None and \
       args.subcmd in ["monitor", "config-get", "config-set", "config-reset"]:
        ret = gconf.is_config_file_old(config_file, args.primary, extra_tmpl_args["secondaryvol"])
        if ret is not None:
           gconf.config_upgrade(config_file, ret)

    # Load Config file
    gconf.load(GLUSTERFS_CONFDIR + "/gsyncd.conf",
               config_file,
               vars(args),
               extra_tmpl_args,
               override_from_args)

    # Default label to print in log file
    label = args.subcmd
    if args.subcmd in ("worker"):
        # If Worker, then add brick path also to label
        label = "%s %s" % (args.subcmd, args.local_path)
    elif args.subcmd == "secondary":
        # If Secondary add Primary node and Brick details
        label = "%s %s%s" % (args.subcmd, args.primary_node, args.primary_brick)

    # Setup Logger
    # Default log file
    log_file = gconf.get("cli-log-file")
    log_level = gconf.get("cli-log-level")
    if getattr(args, "primary", None) is not None and \
       getattr(args, "secondary", None) is not None:
        log_file = gconf.get("log-file")
        log_level = gconf.get("log-level")

    # Use different log file location for Secondary log file
    if args.subcmd == "secondary":
        log_file = gconf.get("secondary-log-file")
        log_level = gconf.get("secondary-log-level")

    if args.debug:
        log_file = "-"
        log_level = "DEBUG"

    # Create Logdir if not exists
    try:
        if log_file != "-":
            os.mkdir(os.path.dirname(log_file))
    except OSError as e:
        if e.errno != EEXIST:
            raise

    setup_logging(
        log_file=log_file,
        level=log_level,
        label=label
    )

    if config_file_error_msg is not None:
        logging.warning(config_file_error_msg)

    # Log message for loaded config file
    if config_file is not None:
        logging.debug(lf("Using session config file", path=config_file))

    set_term_handler()
    excont = FreeObject(exval=0)

    # Gets the function name based on the input argument. For example
    # if subcommand passed as argument is monitor then it looks for
    # function with name "subcmd_monitor" in subcmds file
    func = getattr(subcmds, "subcmd_" + args.subcmd.replace("-", "_"), None)

    try:
        try:
            if func is not None:
                rconf.args = args
                func(args)
        except:
            log_raise_exception(excont)
    finally:
        finalize(exval=excont.exval)


if __name__ == "__main__":
    main()
