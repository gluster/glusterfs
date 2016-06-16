#!/bin/bash

. $(dirname $0)/../../include.rc
. $(dirname $0)/../../volume.rc
. $(dirname $0)/../../afr.rc
cleanup;

# Negative test cases for arbiter volume creation should not crash.

TEST glusterd;
TEST pidof glusterd

# No replica count.
TEST ! $CLI volume create $V0 arbiter 3 $H0:$B0/${V0}{0,1,2}

# replica count given after arbiter count.
TEST ! $CLI volume create $V0 arbiter 1 replica 3 $H0:$B0/${V0}{0,1,2}

#Incorrect values for replica and arbiter count.
TEST ! $CLI volume create $V0  replica 2 arbiter 1 $H0:$B0/${V0}{0,1,2}
TEST ! $CLI volume create $V0  replica 3 arbiter 2 $H0:$B0/${V0}{0,1,2}

# Only permissible value is replica=3 and arbiter=1.
TEST  $CLI volume create $V0  replica 3 arbiter 1 $H0:$B0/${V0}{0,1,2}
cleanup
