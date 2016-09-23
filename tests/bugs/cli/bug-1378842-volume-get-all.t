#!/bin/bash

. $(dirname $0)/../../include.rc
. $(dirname $0)/../../volume.rc

cleanup;
TEST glusterd
TEST pidof glusterd

TEST $CLI volume set all server-quorum-ratio 80

# Execute volume get without having an explicit option, this should fail
TEST ! $CLI volume get all

# Also volume get on an option not applicable for all volumes should fail
TEST ! $CLI volume get all cluster.tier-mode

# Execute volume get with an explicit global option
TEST $CLI volume get all server-quorum-ratio
EXPECT '80' volume_get_field all 'cluster.server-quorum-ratio'

# Execute volume get with 'all'
TEST $CLI volume get all all

cleanup;

