#!/bin/bash

. $(dirname $0)/../../include.rc
. $(dirname $0)/../../cluster.rc
. $(dirname $0)/../../volume.rc

# Initialize
#------------------------------------------------------------
cleanup;

volname=bug-1286171

# Start glusterd
TEST glusterd;
TEST pidof glusterd;
TEST $CLI volume info;

# Create a volume
TEST $CLI volume create $volname $H0:$B0/${volname}{1,2}

# Verify volume creation
EXPECT "$volname" volinfo_field $volname 'Volume Name';
EXPECT 'Created' volinfo_field $volname 'Status';

# Start volume and verify successful start
TEST $CLI volume start $volname;
EXPECT 'Started' volinfo_field $volname 'Status';
TEST glusterfs --volfile-id=$volname --volfile-server=$H0 --entry-timeout=0 $M0;
#------------------------------------------------------------

# Create a nested dir structure and some file under MP
cd $M0;
for i in {1..5}
do
	mkdir dir$i
	cd dir$i
	for j in {1..5}
	do
		mkdir dir$i$j
		cd dir$i$j
		for k in {1..5}
		do
			mkdir dir$i$j$k
			cd dir$i$j$k
			touch {1..300}
			cd ..
		done
		touch {1..300}
		cd ..
	done
	touch {1..300}
	cd ..
done
touch {1..300}

# Add-brick and start rebalance
TEST $CLI volume add-brick $volname $H0:$B0/${volname}4;
TEST $CLI volume rebalance $volname start;

# Let rebalance run for a while
sleep 5

# Stop rebalance
TEST $CLI volume rebalance $volname stop;

# Allow rebalance to stop
sleep 5

# Examine the logfile for errors
cd /var/log/glusterfs;
failures=`grep "failures:" ${volname}-rebalance.log | tail -1 | sed 's/.*failures: //; s/,.*//'`;

TEST [ $failures == 0 ];

cleanup;
