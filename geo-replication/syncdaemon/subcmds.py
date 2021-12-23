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

from __future__ import print_function
from syncdutils import lf
import logging
import gsyncdconfig as gconf


ERROR_CONFIG_INVALID = 2
ERROR_CONFIG_INVALID_VALUE = 3
ERROR_CONFIG_NOT_CONFIGURABLE = 4


def subcmd_monitor_status(args):
    from gsyncdstatus import set_monitor_status
    from rconf import rconf

    set_monitor_status(gconf.get("state-file"), args.status)
    rconf.log_exit = False
    logging.info(lf("Monitor Status Change", status=args.status))


def subcmd_status(args):
    from gsyncdstatus import GeorepStatus

    primary_name = args.primary.replace(":", "")
    secondary_data = args.secondary.replace("ssh://", "")

    brick_status = GeorepStatus(gconf.get("state-file"),
                                "",
                                args.local_path,
                                "",
                                primary_name,
                                secondary_data,
                                gconf.get("pid-file"))
    checkpoint_time = gconf.get("checkpoint", 0)
    brick_status.print_status(checkpoint_time=checkpoint_time,
                              json_output=args.json)


def subcmd_monitor(args):
    import monitor
    from resource import GLUSTER, SSH, Popen
    go_daemon = False if args.debug else True

    monitor.startup(go_daemon)
    Popen.init_errhandler()
    local = GLUSTER("localhost", args.primary)
    secondaryhost, secondaryvol = args.secondary.split("::")
    remote = SSH(secondaryhost, secondaryvol)
    return monitor.monitor(local, remote)


def subcmd_verify_spawning(args):
    logging.info("Able to spawn gsyncd.py")


def subcmd_worker(args):
    import os
    import fcntl

    from resource import GLUSTER, SSH, Popen

    Popen.init_errhandler()
    fcntl.fcntl(args.feedback_fd, fcntl.F_SETFD, fcntl.FD_CLOEXEC)
    local = GLUSTER("localhost", args.primary)
    secondary_url, secondaryvol = args.secondary.split("::")
    if "@" not in secondary_url:
        secondaryhost = args.resource_remote
    else:
        secondaryhost = "%s@%s" % (secondary_url.split("@")[0], args.resource_remote)
    remote = SSH(secondaryhost, secondaryvol)
    remote.connect_remote()
    local.connect()
    logging.info("Worker spawn successful. Acknowledging back to monitor")
    os.close(args.feedback_fd)
    local.service_loop(remote)


def subcmd_secondary(args):
    from resource import GLUSTER, Popen

    Popen.init_errhandler()
    secondaryvol = args.secondary.split("::")[-1]
    local = GLUSTER("localhost", secondaryvol)

    local.connect()
    local.service_loop()


def subcmd_voluuidget(args):
    from subprocess import Popen, PIPE
    import xml.etree.ElementTree as XET

    ParseError = XET.ParseError if hasattr(XET, 'ParseError') else SyntaxError

    cmd = ['gluster', '--xml', '--remote-host=' + args.host,
           'volume', 'info', args.volname]

    if args.inet6:
        cmd.append("--inet6")

    po = Popen(cmd, bufsize=0,
               stdin=None, stdout=PIPE, stderr=PIPE,
               universal_newlines=True)

    vix, err = po.communicate()
    if po.returncode != 0:
        logging.info(lf("Volume info failed, unable to get "
                        "volume uuid of secondaryvol, "
                        "returning empty string",
                        secondaryvol=args.volname,
                        secondaryhost=args.host,
                        error=po.returncode))
        return ""
    vi = XET.fromstring(vix)
    if vi.find('opRet').text != '0':
        logging.info(lf("Unable to get volume uuid of secondaryvol, "
                        "returning empty string",
                        secondaryvol=args.volname,
                        secondaryhost=args.host,
                        error=vi.find('opErrstr').text))
        return ""

    try:
        voluuid = vi.find("volInfo/volumes/volume/id").text
    except (ParseError, AttributeError, ValueError) as e:
        logging.info(lf("Parsing failed to volume uuid of secondaryvol, "
                        "returning empty string",
                        secondaryvol=args.volname,
                        secondaryhost=args.host,
                        error=e))
        voluuid = ""

    print(voluuid)


def _unlink(path):
    import os
    from errno import ENOENT
    from syncdutils import GsyncdError
    import sys

    try:
        os.unlink(path)
    except (OSError, IOError):
        if sys.exc_info()[1].errno == ENOENT:
            pass
        else:
            raise GsyncdError('Unlink error: %s' % path)


def subcmd_delete(args):
    import logging
    import shutil
    import glob
    import sys
    from errno import ENOENT, ENODATA
    import struct

    from syncdutils import GsyncdError, Xattr, errno_wrap
    import gsyncdconfig as gconf

    logging.info('geo-replication delete')
    # remove the stime xattr from all the brick paths so that
    # a re-create of a session will start sync all over again
    stime_xattr_prefix = gconf.get('stime-xattr-prefix', None)

    # Delete pid file, status file, socket file
    cleanup_paths = []
    cleanup_paths.append(gconf.get("pid-file"))

    # Cleanup Session dir
    try:
        shutil.rmtree(gconf.get("georep-session-working-dir"))
    except (IOError, OSError):
        if sys.exc_info()[1].errno == ENOENT:
            pass
        else:
            raise GsyncdError(
                'Error while removing working dir: %s' %
                gconf.get("georep-session-working-dir"))

    # Cleanup changelog working dirs
    try:
        shutil.rmtree(gconf.get("working-dir"))
    except (IOError, OSError):
        if sys.exc_info()[1].errno == ENOENT:
            pass
        else:
            raise GsyncdError(
                'Error while removing working dir: %s' %
                gconf.get("working-dir"))

    for path in cleanup_paths:
        # To delete temp files
        for f in glob.glob(path + "*"):
            _unlink(f)

    if args.reset_sync_time and stime_xattr_prefix:
        for p in args.paths:
            if p != "":
                # set stime to (0,0) to trigger full volume content resync
                # to secondary on session recreation
                # look at primary.py::Xcrawl   hint: zero_zero
                errno_wrap(Xattr.lsetxattr,
                           (p, stime_xattr_prefix + ".stime",
                            struct.pack("!II", 0, 0)),
                           [ENOENT, ENODATA])
                errno_wrap(Xattr.lremovexattr,
                           (p, stime_xattr_prefix + ".entry_stime"),
                           [ENOENT, ENODATA])

    return


def print_config(name, value, only_value=False, use_underscore=False):
    val = value
    if isinstance(value, bool):
        val = str(value).lower()

    if only_value:
        print(val)
    else:
        if use_underscore:
            name = name.replace("-", "_")

        print(("%s:%s" % (name, val)))


def config_name_format(val):
    return val.replace("_", "-")


def subcmd_config_get(args):
    import sys
    import json

    all_config = gconf.getall(show_defaults=args.show_defaults,
                              show_non_configurable=True)
    if args.name is not None:
        val = all_config.get(config_name_format(args.name), None)
        if val is None:
            sys.stderr.write("Invalid config name \"%s\"\n" % args.name)
            sys.exit(ERROR_CONFIG_INVALID)

        print_config(args.name, val["value"], only_value=args.only_value,
                     use_underscore=args.use_underscore)
        return

    if args.json:
        out = []
        # Convert all values as string
        for k in sorted(all_config):
            v = all_config[k]
            out.append({
                "name": k,
                "value": str(v["value"]),
                "default": str(v["default"]),
                "configurable": v["configurable"],
                "modified": v["modified"]
            })

        print((json.dumps(out)))
        return

    for k in sorted(all_config):
        print_config(k, all_config[k]["value"],
                     use_underscore=args.use_underscore)


def subcmd_config_check(args):
    import sys

    try:
        gconf.check(config_name_format(args.name), value=args.value,
                    with_conffile=False)
    except gconf.GconfNotConfigurable:
        cnf_val = gconf.get(config_name_format(args.name), None)
        if cnf_val is None:
            sys.stderr.write("Invalid config name \"%s\"\n" % args.name)
            sys.exit(ERROR_CONFIG_INVALID)

        # Not configurable
        sys.stderr.write("Not configurable \"%s\"\n" % args.name)
        sys.exit(ERROR_CONFIG_NOT_CONFIGURABLE)
    except gconf.GconfInvalidValue:
        sys.stderr.write("Invalid config value \"%s=%s\"\n" % (args.name,
                                                               args.value))
        sys.exit(ERROR_CONFIG_INVALID_VALUE)


def subcmd_config_set(args):
    import sys

    try:
        gconf.setconfig(config_name_format(args.name), args.value)
    except gconf.GconfNotConfigurable:
        cnf_val = gconf.get(config_name_format(args.name), None)
        if cnf_val is None:
            sys.stderr.write("Invalid config name \"%s\"\n" % args.name)
            sys.exit(ERROR_CONFIG_INVALID)

        # Not configurable
        sys.stderr.write("Not configurable \"%s\"\n" % args.name)
        sys.exit(ERROR_CONFIG_NOT_CONFIGURABLE)
    except gconf.GconfInvalidValue:
        sys.stderr.write("Invalid config value \"%s=%s\"\n" % (args.name,
                                                               args.value))
        sys.exit(ERROR_CONFIG_INVALID_VALUE)


def subcmd_config_reset(args):
    import sys

    try:
        gconf.resetconfig(config_name_format(args.name))
    except gconf.GconfNotConfigurable:
        cnf_val = gconf.get(config_name_format(args.name), None)
        if cnf_val is None:
            sys.stderr.write("Invalid config name \"%s\"\n" % args.name)
            sys.exit(ERROR_CONFIG_INVALID)

        # Not configurable
        sys.stderr.write("Not configurable \"%s\"\n" % args.name)
        sys.exit(ERROR_CONFIG_NOT_CONFIGURABLE)
