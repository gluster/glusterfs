#!/usr/bin/env python

# Copyright (c) 2015 Red Hat, Inc. <http://www.redhat.com/>
# This file is part of GlusterFS.
#
# This file is licensed to you under your choice of the GNU Lesser
# General Public License, version 3 or any later version (LGPLv3 or
# later), or the GNU General Public License, version 2 (GPLv2), in all
# cases as published by the Free Software Foundation.

import sys
from errno import ENOENT
import time
from multiprocessing import Process
import os
import xml.etree.cElementTree as etree
from argparse import ArgumentParser, RawDescriptionHelpFormatter, Action
import logging
import shutil

from utils import execute, is_host_local, mkdirp, fail
from utils import setup_logger, human_time
import conf


PROG_DESCRIPTION = """
GlusterFS Incremental API
"""
ParseError = etree.ParseError if hasattr(etree, 'ParseError') else SyntaxError

logger = logging.getLogger()


class StoreAbsPath(Action):
    def __init__(self, option_strings, dest, nargs=None, **kwargs):
        super(StoreAbsPath, self).__init__(option_strings, dest, **kwargs)

    def __call__(self, parser, namespace, values, option_string=None):
        setattr(namespace, self.dest, os.path.abspath(values))


def node_run(volume, host, path, start, outfile, args, fallback=False):
    """
    If host is local node, execute the command locally. If not local
    execute the CHANGE_DETECTOR command via ssh and copy the output file from
    remote node using scp.
    """
    localdir = is_host_local(host)

    # If Full backup is requested or start time is zero, use brickfind
    change_detector = conf.get_change_detector(args.change_detector)
    if ((start == 0 or args.full) and args.change_detector == "changelog") or \
       fallback:
        change_detector = conf.get_change_detector("brickfind")

    # CHANGE_DETECTOR <SESSION> <VOLUME> <BRICK> <OUTFILE> <START> --debug
    # --gfidpath <TYPE>
    cmd = [change_detector,
           args.session,
           volume,
           path,
           outfile,
           str(start),
           "--output-prefix",
           args.output_prefix] + \
        (["--debug"] if args.debug else []) + \
        (["--full"] if args.full else [])

    if not localdir:
        # prefix with ssh command if not local node
        cmd = ["ssh",
               "-i", conf.get_opt("secret_pem"),
               "root@%s" % host] + cmd

    rc, out, err = execute(cmd, logger=logger)
    if rc == 2:
        # Partial History Fallback
        logger.info("%s %s Fallback to brickfind" % (host, err.strip()))
        # Exit only from process, handled in main.
        sys.exit(rc)
    elif rc != 0:
        fail("%s - Change detection failed" % host, logger=logger)

    if not localdir:
        cmd_copy = ["scp",
                    "-i", conf.get_opt("secret_pem"),
                    "root@%s:/%s" % (host, outfile),
                    os.path.dirname(outfile)]
        execute(cmd_copy, exit_msg="%s - Copy command failed" % host,
                logger=logger)


def node_cleanup(host, args):
    localdir = is_host_local(host)

    # CHANGE_DETECTOR <SESSION> <VOLUME> <BRICK> <OUTFILE> <START> --debug
    # --gfidpath <TYPE>
    cmd = [conf.get_opt("nodecleanup"),
           args.session,
           args.volume] + (["--debug"] if args.debug else [])

    if not localdir:
        # prefix with ssh command if not local node
        cmd = ["ssh",
               "-i", conf.get_opt("secret_pem"),
               "root@%s" % host] + cmd

    execute(cmd, exit_msg="%s - Cleanup failed" % host, logger=logger)


def cleanup(nodes, args):
    pool = []
    for num, node in enumerate(nodes):
        host, brick = node[1].split(":")
        # temp output file
        node_outfile = os.path.join(conf.get_opt("working_dir"),
                                    args.session,
                                    args.volume,
                                    "tmp_output_%s.txt" % num)

        try:
            os.remove(node_outfile)
        except (OSError, IOError):
            # TODO: Cleanup Failure, Handle
            pass

        p = Process(target=node_cleanup,
                    args=(host, args))
        p.start()
        pool.append(p)

    exit_codes = 0
    for p in pool:
        p.join()
        exit_codes += (0 if p.exitcode == 0 else 1)

    if exit_codes != 0:
        sys.exit(1)


def failback_node_run(brick_details, idx, volume, start, outfile, args):
    host, brick = brick_details.split(":")
    p = Process(target=node_run,
                args=(volume, host, brick, start, outfile, args, True))
    p.start()
    p.join()
    return p.exitcode


def run_in_nodes(volume, start, args):
    """
    Get nodes of volume using gluster volume info, spawn a process
    each for a Node. Merge the output files once all the process
    complete their tasks.
    """
    nodes = get_nodes(volume)
    pool = []
    node_outfiles = []
    for num, node in enumerate(nodes):
        host, brick = node[1].split(":")
        # temp output file
        node_outfile = os.path.join(conf.get_opt("working_dir"),
                                    args.session,
                                    volume,
                                    "tmp_output_%s.txt" % num)
        node_outfiles.append(node_outfile)
        p = Process(target=node_run, args=(volume, host, brick, start,
                                           node_outfile, args))
        p.start()
        pool.append(p)

    exit_codes = 0
    for idx, p in enumerate(pool):
        p.join()
        # Handle the Changelog failure, fallback to Brickfind
        if p.exitcode == 2:
            rc = failback_node_run(nodes[idx][1], idx, volume, start,
                                   node_outfiles[idx], args)
            exit_codes += (0 if rc == 0 else 1)
        elif p.exitcode != 0:
            exit_codes += (0 if p.exitcode == 0 else 1)

    if exit_codes != 0:
        sys.exit(1)

    # Merge all output files
    cmd = ["sort", "-u"] + node_outfiles + ["-o", args.outfile]
    execute(cmd,
            exit_msg="Failed to merge output files "
            "collected from nodes", logger=logger)

    cleanup(nodes, args)


def get_nodes(volume):
    """
    Get the gluster volume info xml output and parse to get
    the brick details.
    """
    cmd = ["gluster", 'volume', 'info', volume, "--xml"]
    _, data, _ = execute(cmd,
                         exit_msg="Failed to Run Gluster Volume Info",
                         logger=logger)
    tree = etree.fromstring(data)

    nodes = []
    volume_el = tree.find('volInfo/volumes/volume')
    try:
        for b in volume_el.findall('bricks/brick'):
            nodes.append((b.find('hostUuid').text,
                          b.find('name').text))
    except (ParseError, AttributeError, ValueError) as e:
        fail("Failed to parse Volume Info: %s" % e, logger=logger)

    return nodes


def _get_args():
    parser = ArgumentParser(formatter_class=RawDescriptionHelpFormatter,
                            description=PROG_DESCRIPTION)
    subparsers = parser.add_subparsers(dest="mode")

    # create <SESSION> <VOLUME> [--debug] [--force]
    parser_create = subparsers.add_parser('create')
    parser_create.add_argument("session", help="Session Name")
    parser_create.add_argument("volume", help="Volume Name")
    parser_create.add_argument("--debug", help="Debug", action="store_true")
    parser_create.add_argument("--force", help="Force option to recreate "
                               "the session", action="store_true")

    # delete <SESSION> <VOLUME> [--debug]
    parser_delete = subparsers.add_parser('delete')
    parser_delete.add_argument("session", help="Session Name")
    parser_delete.add_argument("volume", help="Volume Name")
    parser_delete.add_argument("--debug", help="Debug", action="store_true")

    # list [--session <SESSION>] [--volume <VOLUME>]
    parser_list = subparsers.add_parser('list')
    parser_list.add_argument("--session", help="Session Name", default="")
    parser_list.add_argument("--volume", help="Volume Name", default="")
    parser_list.add_argument("--debug", help="Debug", action="store_true")

    # pre <SESSION> <VOLUME> <OUTFILE> [--change-detector <CHANGE_DETECTOR>]
    #     [--output-prefix <OUTPUT_PREFIX>] [--full]
    parser_pre = subparsers.add_parser('pre')
    parser_pre.add_argument("session", help="Session Name")
    parser_pre.add_argument("volume", help="Volume Name")
    parser_pre.add_argument("outfile", help="Output File", action=StoreAbsPath)
    parser_pre.add_argument("--debug", help="Debug", action="store_true")
    parser_pre.add_argument("--full", help="Full find", action="store_true")
    parser_pre.add_argument("--change-detector", dest="change_detector",
                            help="Change detection",
                            choices=conf.list_change_detectors(),
                            type=str, default='changelog')
    parser_pre.add_argument("--output-prefix", help="File prefix in output",
                            default=".")

    # post <SESSION> <VOLUME>
    parser_post = subparsers.add_parser('post')
    parser_post.add_argument("session", help="Session Name")
    parser_post.add_argument("volume", help="Volume Name")
    parser_post.add_argument("--debug", help="Debug", action="store_true")

    return parser.parse_args()


def ssh_setup():
    if not os.path.exists(conf.get_opt("secret_pem")):
        # Generate ssh-key
        cmd = ["ssh-keygen",
               "-N",
               "",
               "-f",
               conf.get_opt("secret_pem")]
        execute(cmd,
                exit_msg="Unable to generate ssh key %s"
                % conf.get_opt("secret_pem"),
                logger=logger)

        logger.info("Ssh key generated %s" % conf.get_opt("secret_pem"))

    # Copy pub file to all nodes
    cmd = ["gluster",
           "system::",
           "copy",
           "file",
           "/" + os.path.basename(conf.get_opt("secret_pem")) + ".pub"]
    execute(cmd, exit_msg="Failed to distribute ssh keys", logger=logger)

    logger.info("Distributed ssh key to all nodes of Volume")

    # Add to authorized_keys file in each node
    cmd = ["gluster",
           "system::",
           "execute",
           "add_secret_pub",
           "root",
           os.path.basename(conf.get_opt("secret_pem")) + ".pub"]
    execute(cmd,
            exit_msg="Failed to add ssh keys to authorized_keys file",
            logger=logger)

    logger.info("Ssh key added to authorized_keys of Volume nodes")


def mode_create(session_dir, args):
    logger.debug("Init is called - Session: %s, Volume: %s"
                 % (args.session, args.volume))

    execute(["gluster", "volume", "info", args.volume],
            exit_msg="Unable to get volume details",
            logger=logger)

    mkdirp(session_dir, exit_on_err=True, logger=logger)
    mkdirp(os.path.join(session_dir, args.volume), exit_on_err=True,
           logger=logger)
    status_file = os.path.join(session_dir, args.volume, "status")

    if os.path.exists(status_file) and not args.force:
        fail("Session %s already created" % args.session, logger=logger)

    if not os.path.exists(status_file) or args.force:
        ssh_setup()

        execute(["gluster", "volume", "set",
                 args.volume, "build-pgfid", "on"],
                exit_msg="Failed to set volume option build-pgfid on",
                logger=logger)
        logger.info("Volume option set %s, build-pgfid on" % args.volume)

        execute(["gluster", "volume", "set",
                 args.volume, "changelog.changelog", "on"],
                exit_msg="Failed to set volume option "
                "changelog.changelog on", logger=logger)
        logger.info("Volume option set %s, changelog.changelog on"
                    % args.volume)

    if not os.path.exists(status_file):
        with open(status_file, "w", buffering=0) as f:
            # Add Rollover time to current time to make sure changelogs
            # will be available if we use this time as start time
            time_to_update = int(time.time()) + int(
                conf.get_opt("changelog_rollover_time"))
            f.write(str(time_to_update))

    sys.exit(0)


def mode_pre(session_dir, args):
    """
    Read from Session file and write to session.pre file
    """
    endtime_to_update = int(time.time()) - int(
        conf.get_opt("changelog_rollover_time"))
    status_file = os.path.join(session_dir, args.volume, "status")
    status_file_pre = status_file + ".pre"

    mkdirp(os.path.dirname(args.outfile), exit_on_err=True, logger=logger)

    start = 0
    try:
        with open(status_file) as f:
            start = int(f.read().strip())
    except ValueError:
        pass
    except (OSError, IOError) as e:
        fail("Error Opening Session file %s: %s"
             % (status_file, e), logger=logger)

    logger.debug("Pre is called - Session: %s, Volume: %s, "
                 "Start time: %s, End time: %s"
                 % (args.session, args.volume, start, endtime_to_update))

    run_in_nodes(args.volume, start, args)

    with open(status_file_pre, "w", buffering=0) as f:
        f.write(str(endtime_to_update))

    sys.stdout.write("Generated output file %s\n" % args.outfile)


def mode_post(session_dir, args):
    """
    If pre session file exists, overwrite session file
    If pre session file does not exists, return ERROR
    """
    status_file = os.path.join(session_dir, args.volume, "status")
    logger.debug("Post is called - Session: %s, Volume: %s"
                 % (args.session, args.volume))
    status_file_pre = status_file + ".pre"

    if os.path.exists(status_file_pre):
        os.rename(status_file_pre, status_file)
        sys.exit(0)
    else:
        fail("Pre script is not run", logger=logger)


def mode_delete(session_dir, args):
    def handle_rm_error(func, path, exc_info):
        if exc_info[1].errno == ENOENT:
            return

        raise exc_info[1]

    shutil.rmtree(os.path.join(session_dir, args.volume),
                  onerror=handle_rm_error)


def mode_list(session_dir, args):
    """
    List available sessions to stdout, if session name is set
    only list that session.
    """
    if args.session:
        if not os.path.exists(os.path.join(session_dir, args.session)):
            fail("Invalid Session", logger=logger)
        sessions = [args.session]
    else:
        sessions = []
        for d in os.listdir(session_dir):
            sessions.append(d)

    output = []
    for session in sessions:
        # Session Volume Last Processed
        volnames = os.listdir(os.path.join(session_dir, session))

        for volname in volnames:
            if args.volume and args.volume != volname:
                continue

            status_file = os.path.join(session_dir, session, volname, "status")
            last_processed = None
            try:
                with open(status_file) as f:
                    last_processed = f.read().strip()
            except (OSError, IOError) as e:
                if e.errno == ENOENT:
                    pass
                else:
                    raise
            output.append((session, volname, last_processed))

    if output:
        sys.stdout.write("%s %s %s\n" % ("SESSION".ljust(25),
                                         "VOLUME".ljust(25),
                                         "SESSION TIME".ljust(25)))
        sys.stdout.write("-"*75)
        sys.stdout.write("\n")
    for session, volname, last_processed in output:
        sys.stdout.write("%s %s %s\n" % (session.ljust(25),
                                         volname.ljust(25),
                                         human_time(last_processed).ljust(25)))


def main():
    args = _get_args()
    mkdirp(conf.get_opt("session_dir"), exit_on_err=True)

    if args.mode == "list":
        session_dir = conf.get_opt("session_dir")
    else:
        session_dir = os.path.join(conf.get_opt("session_dir"),
                                   args.session)

    if not os.path.exists(session_dir) and args.mode not in ["create", "list"]:
        fail("Invalid session %s" % args.session)

    mkdirp(os.path.join(conf.get_opt("log_dir"), args.session, args.volume),
           exit_on_err=True)
    log_file = os.path.join(conf.get_opt("log_dir"),
                            args.session,
                            args.volume,
                            "cli.log")
    setup_logger(logger, log_file, args.debug)

    # globals() will have all the functions already defined.
    # mode_<args.mode> will be the function name to be called
    globals()["mode_" + args.mode](session_dir, args)
