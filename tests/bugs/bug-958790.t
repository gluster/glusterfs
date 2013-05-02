#!/bin/bash

. $(dirname $0)/../include.rc
. $(dirname $0)/../volume.rc

cleanup;

TEST glusterd;
TEST pidof glusterd;
TEST $CLI volume info;

touch /var/lib/glusterd/groups/test
echo "read-ahead=off" > /var/lib/glusterd/groups/test
echo "open-behind=off" >> /var/lib/glusterd/groups/test

TEST $CLI volume create $V0 $H0:$B0/${V0}{1,2};
TEST $CLI volume set $V0 group test
EXPECT "off" volume_option $V0 performance.read-ahead
EXPECT "off" volume_option $V0 performance.open-behind

cleanup;
