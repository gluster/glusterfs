#!/bin/bash

. $(dirname $0)/../../include.rc
. $(dirname $0)/../../volume.rc

cleanup;
TEST glusterd
TEST pidof glusterd

TEST $CLI volume create $V0 $H0:$B0/$V0

# Set a volume option
TEST $CLI volume set $V0 open-behind on
TEST $CLI volume start $V0

TEST $CLI volume set all server-quorum-ratio 80

TEST $CLI volume set $V0 user.metadata 'dummy'

# Execute volume get without having an explicit option, this should fail
TEST ! $CLI volume get $V0

# Execute volume get with an explicit option
TEST $CLI volume get $V0 open-behind
EXPECT 'on' volume_get_field $V0 'open-behind'

# Execute volume get with 'all"
TEST $CLI volume get $V0 all

# Check if volume get can display correct global options values as well
EXPECT '80' volume_get_field $V0 'server-quorum-ratio'

# Check user.* options can also be retrived using volume get
EXPECT 'dummy' volume_get_field $V0 'user.metadata'
