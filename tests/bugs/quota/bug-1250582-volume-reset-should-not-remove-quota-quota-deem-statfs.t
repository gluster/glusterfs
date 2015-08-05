#!/bin/bash

# This test ensures that 'gluster volume reset' command do not remove
# features.quota-deem-statfs, features.quota.
# Also, tests that 'gluster volume set features.quota-deem-statfs' can be
# turned on/off when quota is enabled.

.  $(dirname $0)/../../include.rc
.  $(dirname $0)/../../volume.rc

cleanup;

TEST glusterd;
TEST pidof glusterd;
TEST $CLI volume info;

TEST $CLI volume create $V0 replica 2 $H0:$B0/${v0}{1,2};
EXPECT 'Created' volinfo_field $V0 'Status';

TEST $CLI volume start $V0;
EXPECT 'Started' volinfo_field $V0 'Status';

TEST $CLI volume quota $V0 enable
EXPECT 'on' volinfo_field $V0 'features.quota'
EXPECT 'on' volinfo_field $V0 'features.inode-quota'
EXPECT 'on' volinfo_field $V0 'features.quota-deem-statfs'

TEST $CLI volume reset $V0
EXPECT 'on' volinfo_field $V0 'features.quota'
EXPECT 'on' volinfo_field $V0 'features.inode-quota'
EXPECT 'on' volinfo_field $V0 'features.quota-deem-statfs'

TEST $CLI volume reset $V0 force
EXPECT 'on' volinfo_field $V0 'features.quota'
EXPECT 'on' volinfo_field $V0 'features.inode-quota'
EXPECT 'on' volinfo_field $V0 'features.quota-deem-statfs'

TEST $CLI volume reset $V0 features.quota-deem-statfs
EXPECT 'on' volinfo_field $V0 'features.quota-deem-statfs'

TEST $CLI volume set $V0 features.quota-deem-statfs off
EXPECT 'off' volinfo_field $V0 'features.quota-deem-statfs'

TEST $CLI volume set $V0 features.quota-deem-statfs on
EXPECT 'on' volinfo_field $V0 'features.quota-deem-statfs'

TEST $CLI volume quota $V0 disable
EXPECT 'off' volinfo_field $V0 'features.quota'
EXPECT 'off' volinfo_field $V0 'features.inode-quota'
EXPECT '' volinfo_field $V0 'features.quota-deem-statfs'

cleanup;

