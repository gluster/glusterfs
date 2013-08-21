#!/bin/bash

. $(dirname $0)/../include.rc
. $(dirname $0)/../volume.rc

cleanup;

TEST glusterd
TEST pidof glusterd

mkdir -p $H0:$B0/${V0}0
mkdir -p $H0:$B0/${V0}1
mkdir -p $H0:$B0/${V0}2
mkdir -p $H0:$B0/${V0}3

# Create and start a volume.
TEST $CLI volume create $V0 $H0:$B0/${V0}0 $H0:$B0/${V0}1 $H0:$B0/${V0}2
TEST $CLI volume start $V0
EXPECT_WITHIN 15 'Started' volinfo_field $V0 'Status';

# Force assignment of initial ranges.
TEST $CLI volume rebalance $V0 fix-layout start
EXPECT_WITHIN 15 "fix-layout completed" rebalance_status_field $V0

# Get the original values.
xattrs=""
for i in $(seq 0 2); do
	xattrs="$xattrs $(dht_get_layout $B0/${V0}$i)"
done

# Expand the volume and force assignment of new ranges.
TEST $CLI volume add-brick $V0 $H0:$B0/${V0}3
# Force assignment of initial ranges.
TEST $CLI volume rebalance $V0 fix-layout start
EXPECT_WITHIN 15 "fix-layout completed" rebalance_status_field $V0

for i in $(seq 0 3); do
	xattrs="$xattrs $(dht_get_layout $B0/${V0}$i)"
done

overlap=$(python2 $(dirname $0)/overlap.py $xattrs)
# 2863311531 = 0xaaaaaaab = 2/3 overlap
TEST [ "$overlap" -ge 2863311531 ]

cleanup
