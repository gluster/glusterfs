#!/bin/bash

. $(dirname $0)/../include.rc
. $(dirname $0)/../volume.rc

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

#Run lookup to create a layout
TEST ls $M0/

# Test case 1 - Subvolume down + Healing
#------------------------------------------------------------
# Kill 2nd brick process
TEST kill_brick $V0 $H0 $B0/${V0}2
EXPECT_WITHIN ${PROCESS_UP_TIMEOUT} "1" online_brick_count

# Change root permissions
TEST chmod 444 $M0

# Store permission for comparision
TEST permission_new=`stat -c "%A" $M0`

# Bring up the killed brick process
TEST $CLI volume start $V0 force

# Perform lookup
sleep 5
TEST ls $M0

# Check brick permissions
TEST brick_perm=`stat -c "%A" $B0/${V0}2`
TEST [ ${brick_perm} = ${permission_new} ]
#------------------------------------------------------------

# Test case 2 - Add-brick + Healing
#------------------------------------------------------------
# Change root permissions
TEST chmod 777 $M0

# Store permission for comparision
TEST permission_new_2=`stat -c "%A" $M0`

# Add a 3rd brick
TEST $CLI volume add-brick $V0 $H0:$B0/${V0}3

# Perform lookup
sleep 5
TEST ls $M0

# Check permissions on the new brick
TEST brick_perm2=`stat -c "%A" $B0/${V0}3`

TEST [ ${brick_perm2} = ${permission_new_2} ]

cleanup;
