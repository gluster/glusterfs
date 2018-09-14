#!/usr/bin/python2
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
from syncdutils import set_term_handler, finalize, lf
from syncdutils import log_raise_exception, FreeObject, escape
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
    sp = parser.add_subparsers(dest="subcmd")

    # Monitor Status File update
    p = sp.add_parser("monitor-status")
    p.add_argument("master", help="Master Volume Name")
    p.add_argument("slave", help="Slave details user@host::vol format")
    p.add_argument("status", help="Update Monitor Status")
    p.add_argument("-c", "--config-file", help="Config File")
    p.add_argument("--debug", action="store_true")

    # Monitor
    p = sp.add_parser("monitor")
    p.add_argument("master", help="Master Volume Name")
    p.add_argument("slave", help="Slave details user@host::vol format")
    p.add_argument("-c", "--config-file", help="Config File")
    p.add_argument("--pause-on-start",
                   action="store_true",
                   help="Start with Paused state")
    p.add_argument("--local-node-id", help="Local Node ID")
    p.add_argument("--debug", action="store_true")
    p.add_argument("--use-gconf-volinfo", action="store_true")

    # Worker
    p = sp.add_parser("worker")
    p.add_argument("master", help="Master Volume Name")
    p.add_argument("slave", help="Slave details user@host::vol format")
    p.add_argument("--local-path", help="Local Brick Path")
    p.add_argument("--feedback-fd", type=int,
                   help="feedback fd between monitor and worker")
    p.add_argument("--local-node", help="Local master node")
    p.add_argument("--local-node-id", help="Local Node ID")
    p.add_argument("--rpc-fd",
                   help="Read and Write fds for worker-agent communication")
    p.add_argument("--subvol-num", type=int, help="Subvolume number")
    p.add_argument("--is-hottier", action="store_true",
                   help="Is this brick part of hot tier")
    p.add_argument("--resource-remote",
                   help="Remote node to connect to Slave Volume")
    p.add_argument("--resource-remote-id",
                   help="Remote node ID to connect to Slave Volume")
    p.add_argument("--slave-id", help="Slave Volume ID")
    p.add_argument("-c", "--config-file", help="Config File")
    p.add_argument("--debug", action="store_true")

    # Agent
    p = sp.add_parser("agent")
    p.add_argument("master", help="Master Volume Name")
    p.add_argument("slave", help="Slave details user@host::vol format")
    p.add_argument("--local-path", help="Local brick path")
    p.add_argument("--local-node", help="Local master node")
    p.add_argument("--local-node-id", help="Local Node ID")
    p.add_argument("--slave-id", help="Slave Volume ID")
    p.add_argument("--rpc-fd",
                   help="Read and Write fds for worker-agent communication")
    p.add_argument("-c", "--config-file", help="Config File")
    p.add_argument("--debug", action="store_true")

    # Slave
    p = sp.add_parser("slave")
    p.add_argument("master", help="Master Volume Name")
    p.add_argument("slave", help="Slave details user@host::vol format")
    p.add_argument("--session-owner")
    p.add_argument("--master-brick",
                   help="Master brick which is connected to the Slave")
    p.add_argument("--master-node",
                   help="Master node which is connected to the Slave")
    p.add_argument("--master-node-id",
                   help="Master node ID which is connected to the Slave")
    p.add_argument("--local-node", help="Local Slave node")
    p.add_argument("--local-node-id", help="Local Slave ID")
    p.add_argument("-c", "--config-file", help="Config File")
    p.add_argument("--debug", action="store_true")

    # All configurations which are configured via "slave-" options
    # DO NOT add default values for these configurations, default values
    # will be picked from template config file
    p.add_argument("--slave-timeout", type=int,
                   help="Timeout to end gsyncd at Slave side")
    p.add_argument("--use-rsync-xattrs", action="store_true")
    p.add_argument("--slave-log-level", help="Slave Gsyncd Log level")
    p.add_argument("--slave-gluster-log-level",
                   help="Slave Gluster mount Log level")
    p.add_argument("--slave-gluster-command-dir",
                   help="Directory where Gluster binaries exist on slave")
    p.add_argument("--slave-access-mount", action="store_true",
                   help="Do not lazy umount the slave volume")

    # Status
    p = sp.add_parser("status")
    p.add_argument("master", help="Master Volume Name")
    p.add_argument("slave", help="Slave")
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
    p.add_argument("master", help="Master Volume Name")
    p.add_argument("slave", help="Slave")
    p.add_argument("--name", help="Config Name")
    p.add_argument("-c", "--config-file", help="Config File")
    p.add_argument("--debug", action="store_true")
    p.add_argument("--show-defaults", action="store_true")
    p.add_argument("--only-value", action="store_true")
    p.add_argument("--use-underscore", action="store_true")
    p.add_argument("--json", action="store_true")

    # Config-set
    p = sp.add_parser("config-set")
    p.add_argument("master", help="Master Volume Name")
    p.add_argument("slave", help="Slave")
    p.add_argument("-n", "--name", help="Config Name")
    p.add_argument("-v", "--value", help="Config Value")
    p.add_argument("-c", "--config-file", help="Config File")
    p.add_argument("--debug", action="store_true")

    # Config-reset
    p = sp.add_parser("config-reset")
    p.add_argument("master", help="Master Volume Name")
    p.add_argument("slave", help="Slave")
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
    p.add_argument("master", help="Master Volume Name")
    p.add_argument("slave", help="Slave")
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

    # Add First/Primary Slave host, user and volume
    if getattr(args, "slave", None) is not None:
        hostdata, slavevol = args.slave.split("::")
        hostdata = hostdata.split("@")
        slavehost = hostdata[-1]
        slaveuser = "root"
        if len(hostdata) == 2:
            slaveuser = hostdata[0]
        extra_tmpl_args["primary_slave_host"] = slavehost
        extra_tmpl_args["slaveuser"] = slaveuser
        extra_tmpl_args["slavevol"] = slavevol

    # Add Bricks encoded path
    if getattr(args, "local_path", None) is not None:
        extra_tmpl_args["local_id"] = escape(args.local_path)

    # Add Master Bricks encoded path(For Slave)
    if getattr(args, "master_brick", None) is not None:
        extra_tmpl_args["master_brick_id"] = escape(args.master_brick)

    # Load configurations
    config_file = getattr(args, "config_file", None)

    # Subcmd accepts config file argument but not passed
    # Set default path for config file in that case
    # If an subcmd accepts config file then it also accepts
    # master and Slave arguments.
    if config_file is None and hasattr(args, "config_file"):
        config_file = "%s/geo-replication/%s_%s_%s/gsyncd.conf" % (
            GLUSTERD_WORKDIR,
            args.master,
            extra_tmpl_args["primary_slave_host"],
            extra_tmpl_args["slavevol"])

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

    # Override gconf values from argument values only if it is slave gsyncd
    override_from_args = False
    if args.subcmd == "slave":
        override_from_args = True

    # Load Config file
    gconf.load(GLUSTERFS_CONFDIR + "/gsyncd.conf",
               config_file,
               vars(args),
               extra_tmpl_args,
               override_from_args)

    # Default label to print in log file
    label = args.subcmd
    if args.subcmd in ("worker", "agent"):
        # If Worker or agent, then add brick path also to label
        label = "%s %s" % (args.subcmd, args.local_path)
    elif args.subcmd == "slave":
        # If Slave add Master node and Brick details
        label = "%s %s%s" % (args.subcmd, args.master_node, args.master_brick)

    # Setup Logger
    # Default log file
    log_file = gconf.get("cli-log-file")
    log_level = gconf.get("cli-log-level")
    if getattr(args, "master", None) is not None and \
       getattr(args, "slave", None) is not None:
        log_file = gconf.get("log-file")
        log_level = gconf.get("log-level")

    # Use different log file location for Slave log file
    if args.subcmd == "slave":
        log_file = gconf.get("slave-log-file")
        log_level = gconf.get("slave-log-level")

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
        logging.warn(config_file_error_msg)

    # Log message for loaded config file
    if config_file is not None:
        logging.info(lf("Using session config file", path=config_file))

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
