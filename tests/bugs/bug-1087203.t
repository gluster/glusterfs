#!/bin/bash

. $(dirname $0)/../include.rc
. $(dirname $0)/../volume.rc

cleanup;

TEST glusterd;
TEST $CLI volume info;

TEST $CLI volume create $V0 replica 2 $H0:$B0/${V0}-{0,1};
EXPECT "$V0" volinfo_field $V0 'Volume Name';
EXPECT 'Created' volinfo_field $V0 'Status';

TEST $CLI volume start $V0
EXPECT 'Started' volinfo_field $V0 'Status';


# Setting system limit
TEST $CLI snapshot config snap-max-hard-limit 100

# Volume limit cannot exceed system limit, as limit is set to 100,
# this should fail.
TEST ! $CLI snapshot config $V0 snap-max-hard-limit 101

# Following are the invalid cases
TEST ! $CLI snapshot config $V0 snap-max-hard-limit a10
TEST ! $CLI snapshot config snap-max-hard-limit 10a
TEST ! $CLI snapshot config snap-max-hard-limit 10%
TEST ! $CLI snapshot config snap-max-soft-limit 50%1
TEST ! $CLI snapshot config snap-max-soft-limit 0111
TEST ! $CLI snapshot config snap-max-hard-limit OXA
TEST ! $CLI snapshot config snap-max-hard-limit 11.11
TEST ! $CLI snapshot config snap-max-soft-limit 50%

# Soft limit cannot be assigned to volume
TEST ! $CLI snapshot config $V0 snap-max-soft-limit 10

# Valid case
TEST $CLI snapshot config snap-max-soft-limit 50
TEST $CLI snapshot config $V0 snap-max-hard-limit 10

cleanup;

