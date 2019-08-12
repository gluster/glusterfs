#!/bin/bash

. $(dirname $0)/../../include.rc
. $(dirname $0)/../../volume.rc

cleanup;

TEST glusterd
TEST $CLI volume create $V0 replica 2 $H0:$B0/brick1 $H0:$B0/brick2
TEST $CLI volume start $V0
EXPECT 'Started' volinfo_field $V0 'Status'
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "1" brick_up_status $V0 $H0 $B0/brick1
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "1" brick_up_status $V0 $H0 $B0/brick2
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "Y" glustershd_up_status
EXPECT_WITHIN $CHILD_UP_TIMEOUT "1" afr_child_up_status_in_shd $V0 0
EXPECT_WITHIN $CHILD_UP_TIMEOUT "1" afr_child_up_status_in_shd $V0 1
pkill glusterfs
uuid=""
for line in $(cat $GLUSTERD_WORKDIR/glusterd.info)
do
	if [[ $line == UUID* ]]
	then
		uuid=`echo $line | sed -r 's/^.{5}//'`
	fi
done

#Command execution should fail reporting that the bricks are not running.
TEST ! $CLI volume heal $V0 info

cleanup;
