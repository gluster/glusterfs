#!/bin/bash

. $(dirname $0)/../../include.rc
. $(dirname $0)/../../volume.rc
. $(dirname $0)/../../dht.rc

# Initialize
#------------------------------------------------------------
cleanup;

# Start glusterd
TEST glusterd;
TEST pidof glusterd;
TEST $CLI volume info;

# Create a volume
TEST $CLI volume create $V0 $H0:$B0/${V0}{1,2};

# Verify volume creation
EXPECT "$V0" volinfo_field $V0 'Volume Name';
EXPECT 'Created' volinfo_field $V0 'Status';

# Start volume and verify successful start
TEST $CLI volume start $V0;
EXPECT 'Started' volinfo_field $V0 'Status';
TEST glusterfs --volfile-id=$V0 --volfile-server=$H0 --entry-timeout=0 $M0;

#------------------------------------------------------------

# Test case - Create sparse files on MP and verify
# file info after rebalance
#------------------------------------------------------------

# Create some sparse files and get their size
TEST cd $M0;
dd if=/dev/urandom of=sparse_file bs=10k count=1 seek=2M
cp --sparse=always sparse_file sparse_file_3;

# Add a 3rd brick
TEST $CLI volume add-brick $V0 $H0:$B0/${V0}3;

# Trigger rebalance
TEST $CLI volume rebalance $V0 start force;
EXPECT_WITHIN $REBALANCE_TIMEOUT "0" rebalance_completed;

# Compare original and rebalanced files
TEST cd $B0/${V0}2
TEST cmp sparse_file $B0/${V0}3/sparse_file_3
EXPECT_WITHIN 30 "";

cleanup;
