#!/bin/bash

. $(dirname $0)/../../include.rc
. $(dirname $0)/../../cluster.rc

## Check that opRet field has correct value assigned for non existent volumes
## --------------------------------------------------------------------------

function get_opret_value () {
  local VOL=$1
  $CLI volume info $VOL --xml | sed -ne 's/.*<opRet>\([-0-9]*\)<\/opRet>/\1/p'
}

cleanup;

TEST glusterd;
TEST pidof glusterd

TEST $CLI volume create $V0 $H0:$B0/$V0;

EXPECT 0 get_opret_value $V0
EXPECT -1 get_opret_value "novol"

cleanup;
