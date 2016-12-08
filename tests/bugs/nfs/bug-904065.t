#!/bin/bash
#
# This test does not use 'showmount' from the nfs-utils package, it would
# require setting up a portmapper (either rpcbind or portmap, depending on the
# Linux distribution used for testing). The persistancy of the rmtab should not
# affect the current showmount outputs, so existing regression tests should be
# sufficient.
#

# count the lines of a file, return 0 if the file does not exist
function count_lines()
{
        if [ -e "$1" ]
        then
                wc -l < $1
        else
                echo 0
        fi
}


. $(dirname $0)/../../include.rc
. $(dirname $0)/../../nfs.rc
. $(dirname $0)/../../volume.rc

cleanup

TEST glusterd
TEST pidof glusterd

TEST $CLI volume create $V0 $H0:$B0/brick1
EXPECT 'Created' volinfo_field $V0 'Status'
TEST $CLI volume set $V0 nfs.disable false

TEST $CLI volume start $V0;
EXPECT 'Started' volinfo_field $V0 'Status'

# glusterfs/nfs needs some time to start up in the background
EXPECT_WITHIN $NFS_EXPORT_TIMEOUT 1 is_nfs_export_available

# before mounting the rmtab should be empty
EXPECT '0' count_lines $GLUSTERD_WORKDIR/nfs/rmtab

TEST mount_nfs $H0:/$V0 $N0 nolock
# the output would looks similar to:
#
#   hostname-0=172.31.122.104
#   mountpoint-0=/ufo
#
EXPECT '2' count_lines $GLUSTERD_WORKDIR/nfs/rmtab

# duplicate mounts should not be recorded (client could have crashed)
TEST mount_nfs $H0:/$V0 $N1 nolock
EXPECT '2' count_lines $GLUSTERD_WORKDIR/nfs/rmtab

# removing a mount should (even if there are two) should remove the entry
EXPECT_WITHIN $UMOUNT_TIMEOUT "Y" force_umount $N1
EXPECT '0' count_lines $GLUSTERD_WORKDIR/nfs/rmtab

# unmounting the other mount should work flawlessly
EXPECT_WITHIN $UMOUNT_TIMEOUT "Y" force_umount $N0
EXPECT '0' count_lines $GLUSTERD_WORKDIR/nfs/rmtab

TEST glusterfs --entry-timeout=0 --attribute-timeout=0 --volfile-server=$H0 --volfile-id=$V0 $M0

# we'll create a fake rmtab here, similar to how an other storage server would do
# using an invalid IP address to prevent (unlikely) collisions on the test-machine
cat << EOF > $M0/rmtab
hostname-0=127.0.0.256
mountpoint-0=/ufo
EOF
EXPECT '2' count_lines $M0/rmtab

# reconfigure merges the rmtab with the one on the volume
TEST gluster volume set $V0 nfs.mount-rmtab $M0/rmtab

# glusterfs/nfs needs some time to restart
EXPECT_WITHIN $NFS_EXPORT_TIMEOUT 1 is_nfs_export_available

# Apparently "is_nfs_export_available" might return even if the export is
# not, in fact, available.  (eyeroll)  Give it a bit of extra time.
#
# TBD: fix the broken shell function instead of working around it here
sleep 5

# a new mount should be added to the rmtab, not overwrite exiting ones
TEST mount_nfs $H0:/$V0 $N0 nolock
EXPECT_WITHIN $PROCESS_UP_TIMEOUT '4' count_lines $M0/rmtab

EXPECT_WITHIN $UMOUNT_TIMEOUT "Y" force_umount $N0
EXPECT '2' count_lines $M0/rmtab

# TODO: nfs/reconfigure() is never called and is therefor disabled. When the
# NFS-server supports reloading and does not get restarted anymore, we should
# add a test that includes the merging of entries in the old rmtab with the new
# rmtab.

cleanup
