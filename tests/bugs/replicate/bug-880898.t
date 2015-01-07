#!/bin/bash

. $(dirname $0)/../../include.rc

cleanup;

TEST glusterd
TEST $CLI volume create $V0 replica 2 $H0:$B0/brick1 $H0:$B0/brick2
TEST $CLI volume start $V0
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
