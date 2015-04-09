#!/usr/bin/env python
#
# Copyright (c) 2015 Red Hat, Inc. <http://www.redhat.com>
# This file is part of GlusterFS.
#
# This file is licensed to you under your choice of the GNU Lesser
# General Public License, version 3 or any later version (LGPLv3 or
# later), or the GNU General Public License, version 2 (GPLv2), in all
# cases as published by the Free Software Foundation.

from __future__ import print_function
import subprocess
import os
import os.path
import logging
import argparse
import fcntl
import logging.handlers
import sys
from errno import EEXIST


SCRIPT_NAME = "snap_scheduler"
scheduler_enabled = False
log = logging.getLogger(SCRIPT_NAME)
SHARED_STORAGE_DIR="/var/run/gluster/shared_storage"
GCRON_DISABLED = SHARED_STORAGE_DIR+"/snaps/gcron_disabled"
GCRON_ENABLED = SHARED_STORAGE_DIR+"/snaps/gcron_enabled"
GCRON_TASKS = SHARED_STORAGE_DIR+"/snaps/glusterfs_snap_cron_tasks"
GCRON_CROND_TASK = "/etc/cron.d/glusterfs_snap_cron_tasks"
LOCK_FILE_DIR = SHARED_STORAGE_DIR+"/snaps/lock_files/"
LOCK_FILE = LOCK_FILE_DIR+"lock_file"
TMP_FILE = SHARED_STORAGE_DIR+"/snaps/tmp_file"
GCRON_UPDATE_TASK = "/etc/cron.d/gcron_update_task"
tasks = {}
longest_field = 12


def output(msg):
    print("%s: %s" % (SCRIPT_NAME, msg))


def initLogger():
    log.setLevel(logging.DEBUG)
    logFormat = "[%(asctime)s %(filename)s:%(lineno)s %(funcName)s] "\
        "%(levelname)s %(message)s"
    formatter = logging.Formatter(logFormat)

    sh = logging.handlers.SysLogHandler()
    sh.setLevel(logging.ERROR)
    sh.setFormatter(formatter)

    process = subprocess.Popen(["gluster", "--print-logdir"],
                               stdout=subprocess.PIPE)
    logfile = os.path.join(process.stdout.read()[:-1], SCRIPT_NAME + ".log")

    fh = logging.FileHandler(logfile)
    fh.setLevel(logging.DEBUG)
    fh.setFormatter(formatter)

    log.addHandler(sh)
    log.addHandler(fh)


def scheduler_status():
    success = False
    global scheduler_enabled
    try:
        f = os.path.realpath(GCRON_TASKS)
        if f != GCRON_ENABLED or not os.path.exists(GCRON_ENABLED):
            log.info("Snapshot scheduler is currently disabled.")
            scheduler_enabled = False
        else:
            log.info("Snapshot scheduler is currently enabled.")
            scheduler_enabled = True
        success = True
    except:
        log.error("Failed to enable snapshot scheduling. Error: "
                  "Failed to check the status of %s.", GCRON_DISABLED)

    return success


def enable_scheduler():
    ret = scheduler_status()
    if ret:
        if not scheduler_enabled:
            log.info("Enabling snapshot scheduler.")
            try:
                if os.path.exists(GCRON_DISABLED):
                    os.remove(GCRON_DISABLED)
                if os.path.lexists(GCRON_TASKS):
                    os.remove(GCRON_TASKS)
                try:
                    f = os.open(GCRON_ENABLED, os.O_CREAT | os.O_NONBLOCK,
                                0644)
                    os.close(f)
                except IOError as (errno, strerror):
                    log.error("Failed to open %s. Error: %s.",
                              GCRON_ENABLED, strerror)
                    ret = False
                    return ret
                os.symlink(GCRON_ENABLED, GCRON_TASKS)
                log.info("Snapshot scheduling is enabled")
                output("Snapshot scheduling is enabled")
            except IOError as (errno, strerror):
                print_str = "Failed to enable snapshot scheduling. Error: "+strerror
                log.error(print_str)
                output(print_str)
                ret = False
        else:
            print_str = "Failed to enable snapshot scheduling. " \
                        "Error: Snapshot scheduling is already enabled."
            log.error(print_str)
            output(print_str)
            ret = False
    else:
        print_str = "Failed to enable snapshot scheduling. " \
                    "Error: Failed to check scheduler status."
        log.error(print_str)
        output(print_str)

    return ret


def disable_scheduler():
    ret = scheduler_status()
    if ret:
        if scheduler_enabled:
            log.info("Disabling snapshot scheduler.")
            try:
                if os.path.exists(GCRON_DISABLED):
                    os.remove(GCRON_DISABLED)
                if os.path.lexists(GCRON_TASKS):
                    os.remove(GCRON_TASKS)
                f = os.open(GCRON_DISABLED, os.O_CREAT, 0644)
                os.close(f)
                os.symlink(GCRON_DISABLED, GCRON_TASKS)
                log.info("Snapshot scheduling is disabled")
                output("Snapshot scheduling is disabled")
            except IOError as (errno, strerror):
                print_str = "Failed to disable snapshot scheduling. Error: "+strerror
                log.error(print_str)
                output(print_str)
                ret = False
        else:
            print_str = "Failed to disable scheduling. " \
                        "Error: Snapshot scheduling is already disabled."
            log.error(print_str)
            output(print_str)
            ret = False
    else:
        print_str = "Failed to disable snapshot scheduling. " \
                    "Error: Failed to check scheduler status."
        log.error(print_str)
        output(print_str)

    return ret


def load_tasks_from_file():
    global tasks
    global longest_field
    try:
        with open(GCRON_ENABLED, 'r') as f:
            for line in f:
                line = line.rstrip('\n')
                if not line:
                    break
                line = line.split("gcron.py")
                schedule = line[0].split("root")[0].rstrip(' ')
                line = line[1].split(" ")
                volname = line[1]
                jobname = line[2]
                longest_field = max(longest_field, len(jobname), len(volname),
                                    len(schedule))
                tasks[jobname] = schedule+":"+volname
        ret = True
    except IOError as (errno, strerror):
        log.error("Failed to open %s. Error: %s.", GCRON_ENABLED, strerror)
        ret = False

    return ret


def list_schedules():
    log.info("Listing snapshot schedules.")
    ret = load_tasks_from_file()
    if ret:
        if len(tasks) == 0:
            output("No snapshots scheduled")
        else:
            jobname = "JOB_NAME".ljust(longest_field+5)
            schedule = "SCHEDULE".ljust(longest_field+5)
            operation = "OPERATION".ljust(longest_field+5)
            volname = "VOLUME NAME".ljust(longest_field+5)
            hyphens = "".ljust((longest_field+5) * 4, '-')
            print(jobname+schedule+operation+volname)
            print(hyphens)
            for key in sorted(tasks):
                jobname = key.ljust(longest_field+5)
                schedule = tasks[key].split(":")[0].ljust(
                           longest_field + 5)
                volname = tasks[key].split(":")[1].ljust(
                          longest_field + 5)
                operation = "Snapshot Create".ljust(longest_field+5)
                print(jobname+schedule+operation+volname)
    else:
        print_str = "Failed to list snapshot schedules. " \
                    "Error: Failed to load tasks from "+GCRON_ENABLED
        log.error(print_str)
        output(print_str)

    return ret


def write_tasks_to_file():
    ret = False
    try:
        with open(TMP_FILE, "w", 0644) as f:
            # If tasks is empty, just create an empty tmp file
            if len(tasks) != 0:
                for key in sorted(tasks):
                    jobname = key
                    schedule = tasks[key].split(":")[0]
                    volname = tasks[key].split(":")[1]
                    f.write("%s root PATH=$PATH:/usr/local/sbin:/usr/sbin "
                            "gcron.py %s %s\n" % (schedule, volname, jobname))
                f.write("\n")
                f.flush()
                os.fsync(f.fileno())
    except IOError as (errno, strerror):
        log.error("Failed to open %s. Error: %s.", TMP_FILE, strerror)
        ret = False
        return ret

    os.rename(TMP_FILE, GCRON_ENABLED)
    ret = True

    return ret


def add_schedules(jobname, schedule, volname):
    log.info("Adding snapshot schedules.")
    ret = load_tasks_from_file()
    if ret:
        if jobname in tasks:
            print_str = ("%s already exists in schedule. Use "
                         "'edit' to modify %s" % (jobname, jobname))
            log.error(print_str)
            output(print_str)
            ret = False
        else:
            tasks[jobname] = schedule + ":" + volname
            ret = write_tasks_to_file()
            if ret:
                # Create a LOCK_FILE for the job
                job_lockfile = LOCK_FILE_DIR + jobname
                try:
                    f = os.open(job_lockfile, os.O_CREAT | os.O_NONBLOCK, 0644)
                    os.close(f)
                except IOError as (errno, strerror):
                    log.error("Failed to open %s. Error: %s.",
                              job_lockfile, strerror)
                    ret = False
                    return ret
                log.info("Successfully added snapshot schedule %s" % jobname)
                output("Successfully added snapshot schedule")
    else:
        print_str = "Failed to add snapshot schedule. " \
                    "Error: Failed to load tasks from "+GCRON_ENABLED
        log.error(print_str)
        output(print_str)

    return ret


def delete_schedules(jobname):
    log.info("Delete snapshot schedules.")
    ret = load_tasks_from_file()
    if ret:
        if jobname in tasks:
            del tasks[jobname]
            ret = write_tasks_to_file()
            if ret:
                # Delete the LOCK_FILE for the job
                job_lockfile = LOCK_FILE_DIR+jobname
                try:
                    os.remove(job_lockfile)
                except IOError as (errno, strerror):
                    log.error("Failed to open %s. Error: %s.",
                              job_lockfile, strerror)
                log.info("Successfully deleted snapshot schedule %s"
                         % jobname)
                output("Successfully deleted snapshot schedule")
        else:
            print_str = ("Failed to delete %s. Error: No such "
                         "job scheduled" % jobname)
            log.error(print_str)
            output(print_str)
            ret = False
    else:
        print_str = "Failed to delete snapshot schedule. " \
                    "Error: Failed to load tasks from "+GCRON_ENABLED
        log.error(print_str)
        output(print_str)

    return ret


def edit_schedules(jobname, schedule, volname):
    log.info("Editing snapshot schedules.")
    ret = load_tasks_from_file()
    if ret:
        if jobname in tasks:
            tasks[jobname] = schedule+":"+volname
            ret = write_tasks_to_file()
            if ret:
                log.info("Successfully edited snapshot schedule %s" % jobname)
                output("Successfully edited snapshot schedule")
        else:
            print_str = ("Failed to edit %s. Error: No such "
                         "job scheduled" % jobname)
            log.error(print_str)
            output(print_str)
            ret = False
    else:
        print_str = "Failed to edit snapshot schedule. " \
                    "Error: Failed to load tasks from "+GCRON_ENABLED
        log.error(print_str)
        output(print_str)

    return ret


def initialise_scheduler():
    try:
        with open("/tmp/crontab", "w+", 0644) as f:
            updater = ("* * * * * root PATH=$PATH:/usr/local/sbin:"
                       "/usr/sbin gcron.py --update\n")
            f.write("%s\n" % updater)
            f.flush()
            os.fsync(f.fileno())
    except IOError as (errno, strerror):
        log.error("Failed to open /tmp/crontab. Error: %s.", strerror)
        ret = False
        return ret

    os.rename("/tmp/crontab", GCRON_UPDATE_TASK)

    if not os.path.lexists(GCRON_TASKS):
        try:
            f = open(GCRON_TASKS, "w", 0644)
            f.close()
        except IOError as (errno, strerror):
            log.error("Failed to open %s. Error: %s.", GCRON_TASKS, strerror)
            ret = False
            return ret

    if os.path.lexists(GCRON_CROND_TASK):
        os.remove(GCRON_CROND_TASK)

    os.symlink(GCRON_TASKS, GCRON_CROND_TASK)

    log.info("Successfully inited snapshot scheduler for this node")
    output("Successfully inited snapshot scheduler for this node")

    ret = True
    return ret


def syntax_checker(args):
    ret = False

    if hasattr(args, 'jobname'):
        if (len(args.jobname.split()) != 1):
            output("Invalid Jobname. Jobname should not be empty and should not contain \" \" character.")
            return ret
        args.jobname=args.jobname.strip()

    if hasattr(args, 'volname'):
        if (len(args.volname.split()) != 1):
            output("Invalid Volname. Volname should not be empty and should not contain \" \" character.")
            return ret
        args.volname=args.volname.strip()

    if hasattr(args, 'schedule'):
        if (len(args.schedule.split()) != 5):
            output("Invalid Schedule. Please refer to the following for adding a valid cron schedule")
            print ("* * * * *")
            print ("| | | | |")
            print ("| | | | +---- Day of the Week   (range: 1-7, 1 standing for Monday)")
            print ("| | | +------ Month of the Year (range: 1-12)")
            print ("| | +-------- Day of the Month  (range: 1-31)")
            print ("| +---------- Hour              (range: 0-23)")
            print ("+------------ Minute            (range: 0-59)")
            return ret

    ret = True
    return ret


def perform_operation(args):
    ret = False

    # Initialise snapshot scheduler on local node
    if args.action == "init":
        ret = initialise_scheduler()
        if not ret:
            output("Failed to initialise snapshot scheduling")
        return ret

    # Check if the symlink to GCRON_TASKS is properly set in the shared storage
    if (not os.path.lexists(GCRON_UPDATE_TASK) or
        not os.path.lexists(GCRON_CROND_TASK) or
        os.readlink(GCRON_CROND_TASK) != GCRON_TASKS):
        print_str = ("Please run 'snap_scheduler.py' init to initialise "
                     "the snap scheduler for the local node.")
        log.error(print_str)
        output(print_str)
        return ret

    # Check status of snapshot scheduler.
    if args.action == "status":
        ret = scheduler_status()
        if ret:
            if scheduler_enabled:
                output("Snapshot scheduling status: Enabled")
            else:
                output("Snapshot scheduling status: Disabled")
        else:
            output("Failed to check status of snapshot scheduler")
        return ret

    # Enable snapshot scheduler
    if args.action == "enable":
        ret = enable_scheduler()
        if ret:
            subprocess.Popen(["touch", "-h", GCRON_TASKS])
        return ret

    # Disable snapshot scheduler
    if args.action == "disable":
        ret = disable_scheduler()
        if ret:
            subprocess.Popen(["touch", "-h", GCRON_TASKS])
        return ret

    # List snapshot schedules
    if args.action == "list":
        ret = list_schedules()
        return ret

    # Add snapshot schedules
    if args.action == "add":
        ret = syntax_checker(args)
        if not ret:
            return ret
        ret = add_schedules(args.jobname, args.schedule, args.volname)
        if ret:
            subprocess.Popen(["touch", "-h", GCRON_TASKS])
        return ret

    # Delete snapshot schedules
    if args.action == "delete":
        ret = syntax_checker(args)
        if not ret:
            return ret
        ret = delete_schedules(args.jobname)
        if ret:
            subprocess.Popen(["touch", "-h", GCRON_TASKS])
        return ret

    # Edit snapshot schedules
    if args.action == "edit":
        ret = syntax_checker(args)
        if not ret:
            return ret
        ret = edit_schedules(args.jobname, args.schedule, args.volname)
        if ret:
            subprocess.Popen(["touch", "-h", GCRON_TASKS])
        return ret

    return ret


def main():
    initLogger()
    ret = -1
    parser = argparse.ArgumentParser()
    subparsers = parser.add_subparsers(dest="action")
    subparsers.add_parser('init',
                          help="Initialise the node for snapshot scheduling")

    subparsers.add_parser("status",
                          help="Check if snapshot scheduling is "
                          "enabled or disabled")
    subparsers.add_parser("enable",
                          help="Enable snapshot scheduling")
    subparsers.add_parser("disable",
                          help="Disable snapshot scheduling")
    subparsers.add_parser("list",
                          help="List snapshot schedules")
    parser_add = subparsers.add_parser("add",
                                       help="Add snapshot schedules")
    parser_add.add_argument("jobname", help="Job Name")
    parser_add.add_argument("schedule", help="Schedule")
    parser_add.add_argument("volname", help="Volume Name")

    parser_delete = subparsers.add_parser("delete",
                                          help="Delete snapshot schedules")
    parser_delete.add_argument("jobname", help="Job Name")
    parser_edit = subparsers.add_parser("edit",
                                        help="Edit snapshot schedules")
    parser_edit.add_argument("jobname", help="Job Name")
    parser_edit.add_argument("schedule", help="Schedule")
    parser_edit.add_argument("volname", help="Volume Name")

    args = parser.parse_args()

    if not os.path.exists(SHARED_STORAGE_DIR):
        output("Failed: "+SHARED_STORAGE_DIR+" does not exist.")
        return ret

    if not os.path.ismount(SHARED_STORAGE_DIR):
        output("Failed: Shared storage is not mounted at "+SHARED_STORAGE_DIR)
        return ret

    if not os.path.exists(SHARED_STORAGE_DIR+"/snaps/"):
        try:
            os.makedirs(SHARED_STORAGE_DIR+"/snaps/")
        except IOError as (errno, strerror):
            if errno != EEXIST:
                log.error("Failed to create %s : %s", SHARED_STORAGE_DIR+"/snaps/", strerror)
                output("Failed to create %s. Error: %s"
                       % (SHARED_STORAGE_DIR+"/snaps/", strerror))

    if not os.path.exists(GCRON_ENABLED):
        f = os.open(GCRON_ENABLED, os.O_CREAT | os.O_NONBLOCK, 0644)
        os.close(f)

    if not os.path.exists(LOCK_FILE_DIR):
        try:
            os.makedirs(LOCK_FILE_DIR)
        except IOError as (errno, strerror):
            if errno != EEXIST:
                log.error("Failed to create %s : %s", LOCK_FILE_DIR, strerror)
                output("Failed to create %s. Error: %s"
                       % (LOCK_FILE_DIR, strerror))

    try:
        f = os.open(LOCK_FILE, os.O_CREAT | os.O_RDWR | os.O_NONBLOCK, 0644)
        try:
            fcntl.flock(f, fcntl.LOCK_EX | fcntl.LOCK_NB)
            ret = perform_operation(args)
            if not ret:
                ret = 1
            else:
                ret = 0
            fcntl.flock(f, fcntl.LOCK_UN)
        except IOError as (errno, strerror):
            log.info("%s is being processed by another agent.", LOCK_FILE)
            output("Another snap_scheduler command is running. "
                   "Please try again after some time.")
        os.close(f)
    except IOError as (errno, strerror):
        log.error("Failed to open %s : %s", LOCK_FILE, strerror)
        output("Failed to open %s. Error: %s" % (LOCK_FILE, strerror))

    return ret


if __name__ == "__main__":
    sys.exit(main())
