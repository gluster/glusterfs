#!/bin/bash

. $(dirname $0)/../../include.rc
. $(dirname $0)/../../volume.rc
. $(dirname $0)/../../snapshot.rc

function volinfo_validate ()
{
        local var=$1
        $CLI volume info $V0 | grep "^$var" | sed 's/.*: //'
}

cleanup;

TEST verify_lvm_version
TEST glusterd
TEST pidof glusterd
TEST setup_lvm 1

TEST $CLI volume create $V0 $H0:$L1
TEST $CLI volume start $V0

EXPECT '' volinfo_validate 'snap-max-hard-limit'
EXPECT '' volinfo_validate 'snap-max-soft-limit'
EXPECT '' volinfo_validate 'auto-delete'

TEST $CLI snapshot config snap-max-hard-limit 100
TEST $CLI snapshot config $V0 snap-max-hard-limit 50
EXPECT '' volinfo_validate 'snap-max-hard-limit'
EXPECT '' volinfo_validate 'snap-max-soft-limit'
EXPECT '' volinfo_validate 'auto-delete'

TEST $CLI snapshot config snap-max-soft-limit 50
EXPECT '' volinfo_validate 'snap-max-hard-limit'
EXPECT '' volinfo_validate 'snap-max-soft-limit'
EXPECT '' volinfo_validate 'auto-delete'

TEST $CLI snapshot config auto-delete enable
EXPECT '' volinfo_validate 'snap-max-hard-limit'
EXPECT '' volinfo_validate 'snap-max-soft-limit'
EXPECT 'enable' volinfo_validate 'auto-delete'

cleanup;


