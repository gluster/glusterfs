#!/bin/bash
.  $(dirname $0)/../../include.rc
.  $(dirname $0)/../../volume.rc

cleanup;

TEST glusterd
TEST pidof glusterd

TEST $CLI volume create $V0 replica 2  $H0:$B0/${V0}{1,2};
EXPECT 'Created' volinfo_field $V0 'Status';

TEST $CLI volume start $V0;
EXPECT 'Started' volinfo_field $V0 'Status';

TEST $CLI volume quota $V0 enable
EXPECT 'on' volinfo_field $V0 'features.quota'
EXPECT 'on' volinfo_field $V0 'features.quota-deem-statfs'

TEST $CLI volume quota $V0 disable
EXPECT 'off' volinfo_field $V0 'features.quota'
EXPECT '' volinfo_field $V0 'features.quota-deem-statfs'

cleanup;
