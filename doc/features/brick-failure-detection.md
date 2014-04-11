# Brick Failure Detection

This feature attempts to identify storage/file system failures and disable the failed brick without disrupting the remainder of the node's operation.

## Description

Detecting failures on the filesystem that a brick uses makes it possible to handle errors that are caused from outside of the Gluster environment.

There have been hanging brick processes when the underlying storage of a brick went unavailable. A hanging brick process can still use the network and repond to clients, but actual I/O to the storage is impossible and can cause noticible delays on the client side.

Provide better detection of storage subsytem failures and prevent bricks from hanging. It should prevent hanging brick processes when storage-hardware or the filesystem fails.

A health-checker (thread) has been added to the posix xlator. This thread periodically checks the status of the filesystem (implies checking of functional storage-hardware).

`glusterd` can detect that the brick process has exited, `gluster volume status` will show that the brick process is not running anymore. System administrators checking the logs should be able to triage the cause.

## Usage and Configuration

The health-checker is enabled by default and runs a check every 30 seconds. This interval can be changed per volume with:

    # gluster volume set <VOLNAME> storage.health-check-interval <SECONDS>

If `SECONDS` is set to 0, the health-checker will be disabled.

## Failure Detection

Error are logged to the standard syslog (mostly `/var/log/messages`):

    Jun 24 11:31:49 vm130-32 kernel: XFS (dm-2): metadata I/O error: block 0x0 ("xfs_buf_iodone_callbacks") error 5 buf count 512
    Jun 24 11:31:49 vm130-32 kernel: XFS (dm-2): I/O Error Detected. Shutting down filesystem
    Jun 24 11:31:49 vm130-32 kernel: XFS (dm-2): Please umount the filesystem and rectify the problem(s)
    Jun 24 11:31:49 vm130-32 kernel: VFS:Filesystem freeze failed
    Jun 24 11:31:50 vm130-32 GlusterFS[1969]: [2013-06-24 10:31:50.500674] M [posix-helpers.c:1114:posix_health_check_thread_proc] 0-failing_xfs-posix: health-check failed, going down
    Jun 24 11:32:09 vm130-32 kernel: XFS (dm-2): xfs_log_force: error 5 returned.
    Jun 24 11:32:20 vm130-32 GlusterFS[1969]: [2013-06-24 10:32:20.508690] M [posix-helpers.c:1119:posix_health_check_thread_proc] 0-failing_xfs-posix: still alive! -> SIGTERM

The messages labelled with `GlusterFS` in the above output are also written to the logs of the brick process.

## Recovery after a failure

When a brick process detects that the underlaying storage is not responding anymore, the process will exit. There is no automated way that the brick process gets restarted, the sysadmin will need to fix the problem with the storage first.

After correcting the storage (hardware or filesystem) issue, the following command will start the brick process again:

    # gluster volume start <VOLNAME> force

## How To Test

The health-checker thread that is part of each brick process will get started automatically when a volume has been started. Verifying its functionality can be done in different ways.

On virtual hardware:

* disconnect the disk from the VM that holds the brick

On real hardware:

* simulate a RAID-card failure by unplugging the card or cables

On a system that uses LVM for the bricks:

* use device-mapper to load an error-table for the disk, see [this description](http://review.gluster.org/5176).

On any system (writing to random offsets of the block device, more difficult to trigger):

1. cause corruption on the filesystem that holds the brick
2. read contents from the brick, hoping to hit the corrupted area
3. the filsystem should abort after hitting a bad spot, the health-checker should notice that shortly afterwards
