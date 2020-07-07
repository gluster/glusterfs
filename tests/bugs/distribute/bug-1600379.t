#!/bin/bash

. $(dirname $0)/../../include.rc
. $(dirname $0)/../../volume.rc

# Initialize
#------------------------------------------------------------
cleanup;

# Start glusterd
TEST glusterd;
TEST pidof glusterd;
TEST $CLI volume info;

# Create a volume
TEST $CLI volume create $V0 $H0:$B0/${V0}{1,2}

# Verify volume creation
EXPECT "$V0" volinfo_field $V0 'Volume Name';
EXPECT 'Created' volinfo_field $V0 'Status';

# Start volume and verify successful start
TEST $CLI volume start $V0;
EXPECT 'Started' volinfo_field $V0 'Status';
TEST glusterfs --volfile-id=$V0 --volfile-server=$H0 --entry-timeout=0 $M0;
#------------------------------------------------------------

# Test case - Remove xattr from killed brick on lookup
#------------------------------------------------------------
# Create a dir and set custom xattr
TEST mkdir $M0/testdir
TEST setfattr -n user.attr -v val $M0/testdir
xattr_val=`getfattr -d $B0/${V0}2/testdir | awk '{print $1}'`;
TEST ${xattr_val}='user.attr="val"';

# Kill 2nd brick process
TEST kill_brick $V0 $H0 $B0/${V0}2
EXPECT_WITHIN ${PROCESS_UP_TIMEOUT} "1" online_brick_count

# Remove custom xattr
TEST setfattr -x user.attr $M0/testdir

# Bring up the killed brick process
TEST $CLI volume start $V0 force

# Perform lookup
sleep 5
TEST ls $M0/testdir

# Check brick xattrs
xattr_val_2=`getfattr -d $B0/${V0}2/testdir`;
TEST [ ${xattr_val_2} = ''] ;

cleanup;
