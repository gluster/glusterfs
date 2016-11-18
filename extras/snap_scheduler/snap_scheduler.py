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
import shutil
from errno import EEXIST
from conf import GLUSTERFS_LIBEXECDIR
sys.path.insert(1, GLUSTERFS_LIBEXECDIR)

EVENTS_ENABLED = True
try:
    from events.eventtypes import SNAPSHOT_SCHEDULER_INITIALISED \
                         as EVENT_SNAPSHOT_SCHEDULER_INITIALISED
    from events.eventtypes import SNAPSHOT_SCHEDULER_INIT_FAILED \
                         as EVENT_SNAPSHOT_SCHEDULER_INIT_FAILED
    from events.eventtypes import SNAPSHOT_SCHEDULER_DISABLED \
                         as EVENT_SNAPSHOT_SCHEDULER_DISABLED
    from events.eventtypes import SNAPSHOT_SCHEDULER_DISABLE_FAILED \
                         as EVENT_SNAPSHOT_SCHEDULER_DISABLE_FAILED
    from events.eventtypes import SNAPSHOT_SCHEDULER_ENABLED \
                         as EVENT_SNAPSHOT_SCHEDULER_ENABLED
    from events.eventtypes import SNAPSHOT_SCHEDULER_ENABLE_FAILED \
                         as EVENT_SNAPSHOT_SCHEDULER_ENABLE_FAILED
    from events.eventtypes import SNAPSHOT_SCHEDULER_SCHEDULE_ADDED \
                         as EVENT_SNAPSHOT_SCHEDULER_SCHEDULE_ADDED
    from events.eventtypes import SNAPSHOT_SCHEDULER_SCHEDULE_ADD_FAILED \
                         as EVENT_SNAPSHOT_SCHEDULER_SCHEDULE_ADD_FAILED
    from events.eventtypes import SNAPSHOT_SCHEDULER_SCHEDULE_DELETED \
                         as EVENT_SNAPSHOT_SCHEDULER_SCHEDULE_DELETED
    from events.eventtypes import SNAPSHOT_SCHEDULER_SCHEDULE_DELETE_FAILED \
                         as EVENT_SNAPSHOT_SCHEDULER_SCHEDULE_DELETE_FAILED
    from events.eventtypes import SNAPSHOT_SCHEDULER_SCHEDULE_EDITED \
                         as EVENT_SNAPSHOT_SCHEDULER_SCHEDULE_EDITED
    from events.eventtypes import SNAPSHOT_SCHEDULER_SCHEDULE_EDIT_FAILED \
                         as EVENT_SNAPSHOT_SCHEDULER_SCHEDULE_EDIT_FAILED
except ImportError:
    # Events APIs not installed, dummy eventtypes with None
    EVENTS_ENABLED = False
    EVENT_SNAPSHOT_SCHEDULER_INITIALISED = None
    EVENT_SNAPSHOT_SCHEDULER_INIT_FAILED = None
    EVENT_SNAPSHOT_SCHEDULER_DISABLED = None
    EVENT_SNAPSHOT_SCHEDULER_DISABLE_FAILED = None
    EVENT_SNAPSHOT_SCHEDULER_ENABLED = None
    EVENT_SNAPSHOT_SCHEDULER_ENABLE_FAILED = None
    EVENT_SNAPSHOT_SCHEDULER_SCHEDULE_ADDED = None
    EVENT_SNAPSHOT_SCHEDULER_SCHEDULE_ADD_FAILED = None
    EVENT_SNAPSHOT_SCHEDULER_SCHEDULE_DELETED = None
    EVENT_SNAPSHOT_SCHEDULER_SCHEDULE_DELETE_FAILED = None
    EVENT_SNAPSHOT_SCHEDULER_SCHEDULE_EDITED = None
    EVENT_SNAPSHOT_SCHEDULER_SCHEDULE_EDIT_FAILED = None

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
CURRENT_SCHEDULER = SHARED_STORAGE_DIR+"/snaps/current_scheduler"
tasks = {}
longest_field = 12
current_scheduler = ""

INTERNAL_ERROR = 2
SHARED_STORAGE_DIR_DOESNT_EXIST = 3
SHARED_STORAGE_NOT_MOUNTED = 4
ANOTHER_TRANSACTION_IN_PROGRESS = 5
INIT_FAILED = 6
SCHEDULING_ALREADY_DISABLED = 7
SCHEDULING_ALREADY_ENABLED = 8
NODE_NOT_INITIALISED = 9
ANOTHER_SCHEDULER_ACTIVE = 10
JOB_ALREADY_EXISTS = 11
JOB_NOT_FOUND = 12
INVALID_JOBNAME = 13
INVALID_VOLNAME = 14
INVALID_SCHEDULE = 15
INVALID_ARG = 16
VOLUME_DOES_NOT_EXIST = 17

def print_error (error_num):
    if error_num == INTERNAL_ERROR:
        return "Internal Error"
    elif error_num == SHARED_STORAGE_DIR_DOESNT_EXIST:
        return "The shared storage directory ("+SHARED_STORAGE_DIR+")" \
               " does not exist."
    elif error_num == SHARED_STORAGE_NOT_MOUNTED:
        return "The shared storage directory ("+SHARED_STORAGE_DIR+")" \
               " is not mounted."
    elif error_num == ANOTHER_TRANSACTION_IN_PROGRESS:
        return "Another transaction is in progress."
    elif error_num == INIT_FAILED:
        return "Initialisation failed."
    elif error_num == SCHEDULING_ALREADY_DISABLED:
        return "Snapshot scheduler is already disabled."
    elif error_num == SCHEDULING_ALREADY_ENABLED:
        return "Snapshot scheduler is already enabled."
    elif error_num == NODE_NOT_INITIALISED:
        return "The node is not initialised."
    elif error_num == ANOTHER_SCHEDULER_ACTIVE:
        return "Another scheduler is active."
    elif error_num == JOB_ALREADY_EXISTS:
        return "The job already exists."
    elif error_num == JOB_NOT_FOUND:
        return "The job cannot be found."
    elif error_num == INVALID_JOBNAME:
        return "The job name is invalid."
    elif error_num == INVALID_VOLNAME:
        return "The volume name is invalid."
    elif error_num == INVALID_SCHEDULE:
        return "The schedule is invalid."
    elif error_num == INVALID_ARG:
        return "The argument is invalid."
    elif error_num == VOLUME_DOES_NOT_EXIST:
        return "The volume does not exist."

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
    ret = INTERNAL_ERROR
    global scheduler_enabled
    try:
        f = os.path.realpath(GCRON_TASKS)
        if f != os.path.realpath(GCRON_ENABLED) or not os.path.exists(GCRON_ENABLED):
            log.info("Snapshot scheduler is currently disabled.")
            scheduler_enabled = False
        else:
            log.info("Snapshot scheduler is currently enabled.")
            scheduler_enabled = True
        ret = 0
    except:
        log.error("Failed to enable snapshot scheduling. Error: "
                  "Failed to check the status of %s.", GCRON_DISABLED)

    return ret

def enable_scheduler():
    ret = scheduler_status()
    if ret == 0:
        if not scheduler_enabled:

            # Check if another scheduler is active.
            ret = get_current_scheduler()
            if ret == 0:
                if (current_scheduler != "none"):
                    print_str = "Failed to enable snapshot scheduling. " \
                                "Error: Another scheduler is active."
                    log.error(print_str)
                    output(print_str)
                    ret = ANOTHER_SCHEDULER_ACTIVE
                    return ret
            else:
                print_str = "Failed to get current scheduler info."
                log.error(print_str)
                output(print_str)
                return ret

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
                except OSError as (errno, strerror):
                    log.error("Failed to open %s. Error: %s.",
                              GCRON_ENABLED, strerror)
                    ret = INTERNAL_ERROR
                    return ret
                os.symlink(GCRON_ENABLED, GCRON_TASKS)
                update_current_scheduler("cli")
                log.info("Snapshot scheduling is enabled")
                output("Snapshot scheduling is enabled")
                ret = 0
            except OSError as (errno, strerror):
                print_str = "Failed to enable snapshot scheduling. Error: "+strerror
                log.error(print_str)
                output(print_str)
                ret = INTERNAL_ERROR
        else:
            print_str = "Failed to enable snapshot scheduling. " \
                        "Error: Snapshot scheduling is already enabled."
            log.error(print_str)
            output(print_str)
            ret = SCHEDULING_ALREADY_ENABLED
    else:
        print_str = "Failed to enable snapshot scheduling. " \
                    "Error: Failed to check scheduler status."
        log.error(print_str)
        output(print_str)

    return ret


def disable_scheduler():
    ret = scheduler_status()
    if ret == 0:
        if scheduler_enabled:
            log.info("Disabling snapshot scheduler.")
            try:
                # Check if another scheduler is active. If not, then
                # update current scheduler to "none". Else do nothing.
                ret = get_current_scheduler()
                if ret == 0:
                    if (current_scheduler == "cli"):
                        update_current_scheduler("none")
                else:
                    print_str = "Failed to disable snapshot scheduling. " \
                                "Error: Failed to get current scheduler info."
                    log.error(print_str)
                    output(print_str)
                    return ret

                if os.path.exists(GCRON_DISABLED):
                    os.remove(GCRON_DISABLED)
                if os.path.lexists(GCRON_TASKS):
                    os.remove(GCRON_TASKS)
                f = os.open(GCRON_DISABLED, os.O_CREAT, 0644)
                os.close(f)
                os.symlink(GCRON_DISABLED, GCRON_TASKS)
                log.info("Snapshot scheduling is disabled")
                output("Snapshot scheduling is disabled")
                ret = 0
            except OSError as (errno, strerror):
                print_str = "Failed to disable snapshot scheduling. Error: "+strerror
                log.error(print_str)
                output(print_str)
                ret = INTERNAL_ERROR
        else:
            print_str = "Failed to disable scheduling. " \
                        "Error: Snapshot scheduling is already disabled."
            log.error(print_str)
            output(print_str)
            ret = SCHEDULING_ALREADY_DISABLED
    else:
        print_str = "Failed to disable snapshot scheduling. " \
                    "Error: Failed to check scheduler status."
        log.error(print_str)
        output(print_str)
        ret = INTERNAL_ERROR

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
            f.close()
        ret = 0
    except IOError as (errno, strerror):
        log.error("Failed to open %s. Error: %s.", GCRON_ENABLED, strerror)
        ret = INTERNAL_ERROR

    return ret


def get_current_scheduler():
    global current_scheduler
    try:
        with open(CURRENT_SCHEDULER, 'r') as f:
            current_scheduler = f.readline().rstrip('\n')
            f.close()
        ret = 0
    except IOError as (errno, strerror):
        log.error("Failed to open %s. Error: %s.", CURRENT_SCHEDULER, strerror)
        ret = INTERNAL_ERROR

    return ret


def list_schedules():
    log.info("Listing snapshot schedules.")
    ret = load_tasks_from_file()
    if ret == 0:
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
            ret = 0
    else:
        print_str = "Failed to list snapshot schedules. " \
                    "Error: Failed to load tasks from "+GCRON_ENABLED
        log.error(print_str)
        output(print_str)

    return ret


def write_tasks_to_file():
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
            f.close()
    except IOError as (errno, strerror):
        log.error("Failed to open %s. Error: %s.", TMP_FILE, strerror)
        ret = INTERNAL_ERROR
        return ret

    shutil.move(TMP_FILE, GCRON_ENABLED)
    ret = 0

    return ret

def update_current_scheduler(data):
    try:
        with open(TMP_FILE, "w", 0644) as f:
            f.write("%s" % data)
            f.flush()
            os.fsync(f.fileno())
            f.close()
    except IOError as (errno, strerror):
        log.error("Failed to open %s. Error: %s.", TMP_FILE, strerror)
        ret = INTERNAL_ERROR
        return ret

    shutil.move(TMP_FILE, CURRENT_SCHEDULER)
    ret = 0

    return ret


def isVolumePresent(volname):
    success = False
    if volname == "":
        log.debug("No volname given")
        return success

    cli = ["gluster",
           "volume",
           "info",
           volname]
    log.debug("Running command '%s'", " ".join(cli))

    p = subprocess.Popen(cli, stdout=subprocess.PIPE,
                         stderr=subprocess.PIPE)
    out, err = p.communicate()
    rv = p.returncode

    log.debug("Command '%s' returned '%d'", " ".join(cli), rv)

    if rv:
        log.error("Command output:")
        log.error(err)
    else:
        success = True;

    return success


def add_schedules(jobname, schedule, volname):
    log.info("Adding snapshot schedules.")
    ret = load_tasks_from_file()
    if ret == 0:
        if jobname in tasks:
            print_str = ("%s already exists in schedule. Use "
                         "'edit' to modify %s" % (jobname, jobname))
            log.error(print_str)
            output(print_str)
            ret = JOB_ALREADY_EXISTS
        else:
            if not isVolumePresent(volname):
                print_str = ("Volume %s does not exist. Create %s and retry." %
                             (volname, volname))
                log.error(print_str)
                output(print_str)
                ret = VOLUME_DOES_NOT_EXIST
            else:
                tasks[jobname] = schedule + ":" + volname
                ret = write_tasks_to_file()
                if ret == 0:
                    # Create a LOCK_FILE for the job
                    job_lockfile = LOCK_FILE_DIR + jobname
                    try:
                        f = os.open(job_lockfile, os.O_CREAT | os.O_NONBLOCK,
                                    0644)
                        os.close(f)
                    except OSError as (errno, strerror):
                        log.error("Failed to open %s. Error: %s.",
                                  job_lockfile, strerror)
                        ret = INTERNAL_ERROR
                        return ret
                    log.info("Successfully added snapshot schedule %s" %
                             jobname)
                    output("Successfully added snapshot schedule")
                    ret = 0
    else:
        print_str = "Failed to add snapshot schedule. " \
                    "Error: Failed to load tasks from "+GCRON_ENABLED
        log.error(print_str)
        output(print_str)

    return ret


def delete_schedules(jobname):
    log.info("Delete snapshot schedules.")
    ret = load_tasks_from_file()
    if ret == 0:
        if jobname in tasks:
            del tasks[jobname]
            ret = write_tasks_to_file()
            if ret == 0:
                # Delete the LOCK_FILE for the job
                job_lockfile = LOCK_FILE_DIR+jobname
                try:
                    os.remove(job_lockfile)
                except OSError as (errno, strerror):
                    log.error("Failed to open %s. Error: %s.",
                              job_lockfile, strerror)
                    ret = INTERNAL_ERROR
                    return ret
                log.info("Successfully deleted snapshot schedule %s"
                         % jobname)
                output("Successfully deleted snapshot schedule")
                ret = 0
        else:
            print_str = ("Failed to delete %s. Error: No such "
                         "job scheduled" % jobname)
            log.error(print_str)
            output(print_str)
            ret = JOB_NOT_FOUND
    else:
        print_str = "Failed to delete snapshot schedule. " \
                    "Error: Failed to load tasks from "+GCRON_ENABLED
        log.error(print_str)
        output(print_str)

    return ret


def edit_schedules(jobname, schedule, volname):
    log.info("Editing snapshot schedules.")
    ret = load_tasks_from_file()
    if ret == 0:
        if jobname in tasks:
            if not isVolumePresent(volname):
                print_str = ("Volume %s does not exist. Create %s and retry." %
                             (volname, volname))
                log.error(print_str)
                output(print_str)
                ret = VOLUME_DOES_NOT_EXIST
            else:
                tasks[jobname] = schedule+":"+volname
                ret = write_tasks_to_file()
                if ret == 0:
                    log.info("Successfully edited snapshot schedule %s" %
                             jobname)
                    output("Successfully edited snapshot schedule")
        else:
            print_str = ("Failed to edit %s. Error: No such "
                         "job scheduled" % jobname)
            log.error(print_str)
            output(print_str)
            ret = JOB_NOT_FOUND
    else:
        print_str = "Failed to edit snapshot schedule. " \
                    "Error: Failed to load tasks from "+GCRON_ENABLED
        log.error(print_str)
        output(print_str)

    return ret


def initialise_scheduler():
    try:
        with open(TMP_FILE, "w+", 0644) as f:
            updater = ("* * * * * root PATH=$PATH:/usr/local/sbin:"
                       "/usr/sbin gcron.py --update\n")
            f.write("%s\n" % updater)
            f.flush()
            os.fsync(f.fileno())
            f.close()
    except IOError as (errno, strerror):
        log.error("Failed to open %s. Error: %s.", TMP_FILE, strerror)
        ret = INIT_FAILED
        return ret

    shutil.move(TMP_FILE, GCRON_UPDATE_TASK)

    if not os.path.lexists(GCRON_TASKS):
        try:
            f = open(GCRON_TASKS, "w", 0644)
            f.close()
        except IOError as (errno, strerror):
            log.error("Failed to open %s. Error: %s.", GCRON_TASKS, strerror)
            ret = INIT_FAILED
            return ret

    if os.path.lexists(GCRON_CROND_TASK):
        os.remove(GCRON_CROND_TASK)

    os.symlink(GCRON_TASKS, GCRON_CROND_TASK)

    log.info("Successfully initialised snapshot scheduler for this node")
    output("Successfully initialised snapshot scheduler for this node")
    gf_event (EVENT_SNAPSHOT_SCHEDULER_INITIALISED, status="Success")

    ret = 0
    return ret


def syntax_checker(args):
    if hasattr(args, 'jobname'):
        if (len(args.jobname.split()) != 1):
            output("Invalid Jobname. Jobname should not be empty and should not contain \" \" character.")
            ret = INVALID_JOBNAME
            return ret
        args.jobname=args.jobname.strip()

    if hasattr(args, 'volname'):
        if (len(args.volname.split()) != 1):
            output("Invalid Volname. Volname should not be empty and should not contain \" \" character.")
            ret = INVALID_VOLNAME
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
            ret = INVALID_SCHEDULE
            return ret

    ret = 0
    return ret


def perform_operation(args):
    if not os.path.exists(CURRENT_SCHEDULER):
        update_current_scheduler("none")

    # Initialise snapshot scheduler on local node
    if args.action == "init":
        ret = initialise_scheduler()
        if ret != 0:
            output("Failed to initialise snapshot scheduling")
            gf_event (EVENT_SNAPSHOT_SCHEDULER_INIT_FAILED,
                      error=print_error(ret))
        return ret

    # Disable snapshot scheduler
    if args.action == "disable_force":
        ret = disable_scheduler()
        if ret == 0:
            subprocess.Popen(["touch", "-h", GCRON_TASKS])
            gf_event (EVENT_SNAPSHOT_SCHEDULER_DISABLED,
                      status="Successfuly Disabled")
        else:
            gf_event (EVENT_SNAPSHOT_SCHEDULER_DISABLE_FAILED,
                      error=print_error(ret))
        return ret

    # Check if the symlink to GCRON_TASKS is properly set in the shared storage
    if (not os.path.lexists(GCRON_UPDATE_TASK) or
        not os.path.lexists(GCRON_CROND_TASK) or
        os.readlink(GCRON_CROND_TASK) != GCRON_TASKS):
        print_str = ("Please run 'snap_scheduler.py' init to initialise "
                     "the snap scheduler for the local node.")
        log.error(print_str)
        output(print_str)
        ret = NODE_NOT_INITIALISED
        return ret

    # Check status of snapshot scheduler.
    if args.action == "status":
        ret = scheduler_status()
        if ret == 0:
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
        if ret == 0:
            subprocess.Popen(["touch", "-h", GCRON_TASKS])
            gf_event (EVENT_SNAPSHOT_SCHEDULER_ENABLED,
                      status="Successfuly Enabled")
        else:
            gf_event (EVENT_SNAPSHOT_SCHEDULER_ENABLE_FAILED,
                      error=print_error(ret))
        return ret

    # Disable snapshot scheduler
    if args.action == "disable":
        ret = disable_scheduler()
        if ret == 0:
            subprocess.Popen(["touch", "-h", GCRON_TASKS])
            gf_event (EVENT_SNAPSHOT_SCHEDULER_DISABLED,
                      status="Successfuly Disabled")
        else:
            gf_event (EVENT_SNAPSHOT_SCHEDULER_DISABLE_FAILED,
                      error=print_error(ret))
        return ret

    # List snapshot schedules
    if args.action == "list":
        ret = list_schedules()
        return ret

    # Add snapshot schedules
    if args.action == "add":
        ret = syntax_checker(args)
        if ret != 0:
            return ret
        ret = add_schedules(args.jobname, args.schedule, args.volname)
        if ret == 0:
            subprocess.Popen(["touch", "-h", GCRON_TASKS])
            gf_event (EVENT_SNAPSHOT_SCHEDULER_SCHEDULE_ADDED,
                      status="Successfuly added job "+args.jobname)
        else:
            gf_event (EVENT_SNAPSHOT_SCHEDULER_SCHEDULE_ADD_FAILED,
                      status="Failed to add job "+args.jobname,
                      error=print_error(ret))
        return ret

    # Delete snapshot schedules
    if args.action == "delete":
        ret = syntax_checker(args)
        if ret != 0:
            return ret
        ret = delete_schedules(args.jobname)
        if ret == 0:
            subprocess.Popen(["touch", "-h", GCRON_TASKS])
            gf_event (EVENT_SNAPSHOT_SCHEDULER_SCHEDULE_DELETED,
                      status="Successfuly deleted job "+args.jobname)
        else:
            gf_event (EVENT_SNAPSHOT_SCHEDULER_SCHEDULE_DELETE_FAILED,
                      status="Failed to delete job "+args.jobname,
                      error=print_error(ret))
        return ret

    # Edit snapshot schedules
    if args.action == "edit":
        ret = syntax_checker(args)
        if ret != 0:
            return ret
        ret = edit_schedules(args.jobname, args.schedule, args.volname)
        if ret == 0:
            subprocess.Popen(["touch", "-h", GCRON_TASKS])
            gf_event (EVENT_SNAPSHOT_SCHEDULER_SCHEDULE_EDITED,
                      status="Successfuly edited job "+args.jobname)
        else:
            gf_event (EVENT_SNAPSHOT_SCHEDULER_SCHEDULE_EDIT_FAILED,
                      status="Failed to edit job "+args.jobname,
                      error=print_error(ret))
        return ret

    ret = INVALID_ARG
    return ret

def gf_event(event_type, **kwargs):
    if EVENTS_ENABLED:
        from events.gf_event import gf_event as gfevent
        gfevent(event_type, **kwargs)


def main(argv):
    initLogger()
    ret = -1
    parser = argparse.ArgumentParser()
    subparsers = parser.add_subparsers(dest="action",
                                       metavar=('{init, status, enable,'
                                               ' disable, list, add,'
                                               ' delete, edit}'))
    subparsers.add_parser('init',
                          help="Initialise the node for snapshot scheduling")

    subparsers.add_parser("status",
                          help="Check if snapshot scheduling is "
                          "enabled or disabled")
    subparsers.add_parser("enable",
                          help="Enable snapshot scheduling")
    subparsers.add_parser("disable",
                          help="Disable snapshot scheduling")
    subparsers.add_parser("disable_force")
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

    args = parser.parse_args(argv)

    if not os.path.exists(SHARED_STORAGE_DIR):
        output("Failed: "+SHARED_STORAGE_DIR+" does not exist.")
        return SHARED_STORAGE_DIR_DOESNT_EXIST

    if not os.path.ismount(SHARED_STORAGE_DIR):
        output("Failed: Shared storage is not mounted at "+SHARED_STORAGE_DIR)
        return SHARED_STORAGE_NOT_MOUNTED

    if not os.path.exists(SHARED_STORAGE_DIR+"/snaps/"):
        try:
            os.makedirs(SHARED_STORAGE_DIR+"/snaps/")
        except OSError as (errno, strerror):
            if errno != EEXIST:
                log.error("Failed to create %s : %s", SHARED_STORAGE_DIR+"/snaps/", strerror)
                output("Failed to create %s. Error: %s"
                       % (SHARED_STORAGE_DIR+"/snaps/", strerror))
                return INTERNAL_ERROR

    if not os.path.exists(GCRON_ENABLED):
        f = os.open(GCRON_ENABLED, os.O_CREAT | os.O_NONBLOCK, 0644)
        os.close(f)

    if not os.path.exists(LOCK_FILE_DIR):
        try:
            os.makedirs(LOCK_FILE_DIR)
        except OSError as (errno, strerror):
            if errno != EEXIST:
                log.error("Failed to create %s : %s", LOCK_FILE_DIR, strerror)
                output("Failed to create %s. Error: %s"
                       % (LOCK_FILE_DIR, strerror))
                return INTERNAL_ERROR

    try:
        f = os.open(LOCK_FILE, os.O_CREAT | os.O_RDWR | os.O_NONBLOCK, 0644)
        try:
            fcntl.flock(f, fcntl.LOCK_EX | fcntl.LOCK_NB)
            ret = perform_operation(args)
            fcntl.flock(f, fcntl.LOCK_UN)
        except IOError as (errno, strerror):
            log.info("%s is being processed by another agent.", LOCK_FILE)
            output("Another snap_scheduler command is running. "
                   "Please try again after some time.")
            return ANOTHER_TRANSACTION_IN_PROGRESS
        os.close(f)
    except OSError as (errno, strerror):
        log.error("Failed to open %s : %s", LOCK_FILE, strerror)
        output("Failed to open %s. Error: %s" % (LOCK_FILE, strerror))
        return INTERNAL_ERROR

    return ret


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
