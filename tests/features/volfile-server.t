#!/bin/bash

#G_TESTDEF_TEST_STATUS_CENTOS6=BAD_TEST,BUG=000

# This test is marked bad, as the mount with volfile server fails
# when glusterd has written the volfile. This is a known problem
# as the default volfiles generated in glusterd doesn't contain
# brick port information in volfile, and expect all clients to
# connect to glusterd's 24007 and then get the brickport from
# internal portmapper. If the volfiles served by 'volfile-server'
# process contain 'remote-port' option in client-protocol
# definition, then the test would pass. Adding this test case in
# the code even though it fails to make sure this helps in
# understanding volfile-server use properly!

. $(dirname $0)/../include.rc
. $(dirname $0)/../volume.rc
. $(dirname $0)/../common-utils.rc


cleanup

TEST glusterd
TEST pidof glusterd

# Create a plain distribute volume with 2 subvols.
TEST   $CLI volume create $V0 $H0:$B0/${V0}{1,2};
TEST   $CLI volume start $V0;
EXPECT "Started" volinfo_field $V0 'Status';

# Now stop glusterd
TEST pkill glusterd

# Start volfile-xlator (using the sample file in the source)
TEST glusterfs -f $(dirname $0)/../../xlators/mgmt/volfile-server/sample-volfile.vol

netstat -ntlp | grep 24007

# Mount using FUSE
TEST glusterfs -s $H0 --volfile-id $V0 --log-level TRACE $M0

TEST mkdir $M0/dir1

force_umount $M0
TEST pkill glusterfsd

# Cleanup
cleanup
