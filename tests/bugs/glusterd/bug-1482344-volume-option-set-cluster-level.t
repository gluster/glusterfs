#!/bin/bash

#Test case: glusterd should disallow a volume level option to be set cluster
wide and glusterd should not crash

. $(dirname $0)/../../include.rc
. $(dirname $0)/../../volume.rc

cleanup;

#Basic checks
TEST glusterd
TEST pidof glusterd

#Create a 2x1 distributed volume
TEST $CLI volume create $V0 $H0:$B0/${V0}{1,2};
TEST $CLI volume start $V0

TEST ! $CLI volume set all transport.listen-backlog 128

# Check the volume info output, if glusterd would have crashed then this command
# will fail
TEST $CLI volume info $V0;

cleanup;
