Snapshot Scheduler
==============================

SUMMARY
-------

GlusterFS volume snapshot provides point-in-time copy of a GlusterFS volume. Currently, GlusterFS volume snapshots can be easily scheduled by setting up cron jobs on one of the nodes in the GlusterFS trusted storage pool. This has a single point failure (SPOF), as scheduled jobs can be missed if the node running the cron jobs dies.

We can avoid the SPOF by distributing the cron jobs to all nodes of the trusted storage pool.

DETAILED DESCRIPTION
--------------------

The solution to the above problems involves the usage of:

* A shared storage - This can be any shared storage (another gluster volume, a NFS mount, etc.) that will be used to share the schedule configuration and will help in the coordination of the jobs.
* An agent - This agent will perform the actual snapshot commands, instead of cron. It will contain the logic to perform coordinated snapshots.
* A helper script - This script will allow the user to initialise the scheduler on the local node, enable/disable scheduling, add/edit/list/delete snapshot schedules.
* cronie - It is the default cron daemon shipped with RHEL. It invokes the agent at the appropriate intervals as mentioned by the user to perform the snapshot operation on the volume as mentioned by the user in the schedule.

INITIAL SETUP
-------------

The administrator needs to create a shared storage that can be available to nodes across the cluster. A GlusterFS volume can also be used for the same. It is preferable that the *shared volume* be a replicate volume to avoid SPOF.

Once the shared storage is created, it should be mounted on all nodes in the trusted storage pool which will be participating in the scheduling. The location where the shared_storage should be mounted (/var/run/gluster/snaps/shared_storage) in these nodes is fixed and is not configurable. Each node participating in the scheduling then needs to perform an initialisation of the snapshot scheduler by invoking the following:

snap_scheduler.py init

NOTE: This command needs to be run on all the nodes participating in the scheduling

HELPER SCRIPT
-------------

The helper script(snap_scheduler.py) will initialise the scheduler on the local node, enable/disable scheduling, add/edit/list/delete snapshot schedules.

a) snap_scheduler.py init

This command initialises the snap_scheduler and interfaces it with the crond running on the local node. This is the first step, before executing any scheduling related commands from a node.

NOTE: The helper script needs to be run with this option on all the nodes participating in the scheduling. Other options of the helper script can be run independently from any node, where initialisation has been successfully completed.

b) snap_scheduler.py enable

The snap scheduler is disabled by default after initialisation. This command enables the snap scheduler.

c) snap_scheduler.py disable

This command disables the snap scheduler.

d) snap_scheduler.py status

This command displays the current status(Enabled/Disabled) of the snap scheduler.

e) snap_scheduler.py add "Job Name" "Schedule" "Volume Name"

This command adds a new snapshot schedule. All the arguments must be provided within double-quotes(""). It takes three arguments:

-> Job Name: This name uniquely identifies this particular schedule, and can be used to reference this schedule for future events like edit/delete. If a schedule already exists for the specified Job Name, the add command will fail.

-> Schedule: The schedules are accepted in the format crond understands:-

Example of job definition:
.---------------- minute (0 - 59)
| .------------- hour (0 - 23)
| | .---------- day of month (1 - 31)
| | | .------- month (1 - 12) OR jan,feb,mar,apr ...
| | | | .---- day of week (0 - 6) (Sunday=0 or 7) OR sun,mon,tue,wed,thu,fri,sat
| | | | |
* * * * * user-name command to be executed
Although we accept all valid cron schedules, currently we support granularity of snapshot schedules to a maximum of half-hourly snapshots.

-> Volume Name: The name of the volume on which the scheduled snapshot operation will be performed.

f) snap_scheduler.py edit "Job Name" "Schedule" "Volume Name"

This command edits an existing snapshot schedule. It takes the same three arguments that the add option takes. All the arguments must be provided within double-quotes(""). If a schedule does not exists for the specified Job Name, the edit command will fail.

g) snap_scheduler.py delete "Job Name"

This command deletes an existing snapshot schedule. It takes the job name of the schedule as argument. The argument must be provided within double-quotes(""). If a schedule does not exists for the specified Job Name, the delete command will fail.

h) snap_scheduler.py list

This command lists the existing snapshot schedules in the following manner: Pseudocode:

# snap_scheduler.py list
JOB_NAME         SCHEDULE         OPERATION        VOLUME NAME
--------------------------------------------------------------------
Job0             * * * * *        Snapshot Create  test_vol

THE AGENT
---------

The snapshots scheduled with the help of the helper script, are read by crond which then invokes the agent(gcron.py) at the scheduled intervals to perform the snapshot operations on the specified volumes. It then performs the scheduled snapshots using the following algorithm to coordinate.

start_time = get current time
lock_file = job_name passed as an argument
vol_name = volume name psased as an argument
try POSIX locking the $lock_file
    if lock is obtained, then
        mod_time = Get modification time of $entry
        if $mod_time < $start_time, then
            Take snapshot of $entry.name (Volume name)
            if snapshot failed, then
                log the failure
            Update modification time of $entry to current time
        unlock the $entry

The coordination with other scripts running on other nodes, is handled by the use of POSIX locks. All the instances of the script will attempt to lock the lock_file which is essentialy an empty file with the job name, and one which gets the lock will take the snapshot.

To prevent redoing a done task, the script will make use of the mtime attribute of the entry. At the beginning execution, the script would have saved its start time. Once the script obtains the lock on the lock_file, before taking the snapshot, it compares the mtime of the entry with the start time. The snapshot will only be taken if the mtime is smaller than start time. Once the snapshot command completes, the script will update the mtime of the lock_file to the current time before unlocking.

If a snapshot command fails, the script will log the failure (in syslog) and continue with its operation. It will not attempt to retry the failed snapshot in the current schedule, but will attempt it again in the next schedules. It is left to the administrator to monitor the logs and decide what to do after a failure.

ASSUMPTIONS AND LIMITATIONS
---------------------------

It is assumed that all nodes in the have their times synced using NTP or any other mechanism. This is a hard requirement for this feature to work.

The administrator needs to have python2.7 or higher installed, as well as the argparse module installed, to be able to use the helper script(snap_scheduler.py).

There is a latency of one minute, between providing a command by the helper script and that command taking effect. Hence, currently we do not support snapshot schedules with per minute granularity.

The administrator can however leverage the scheduler to schedule snapshots with granularity of half-hourly/hourly/daily/weekly/monthly/yearly periodic intervals. They can also schedule snapshots, which are customised mentioning which minute of the hour, which day of the week, which week of the month, and which month of the year, they want to schedule the snapshot operation.
