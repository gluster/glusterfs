#!/bin/bash

. $(dirname $0)/../../include.rc
. $(dirname $0)/../../volume.rc

## This tests failover achieved by providing multiple
## servers from the trusted pool for fetching volume
## specification

cleanup;

TEST glusterd
TEST pidof glusterd

TEST $CLI volume create $V0 $H0:$B0/$V0
TEST $CLI volume start $V0
TEST glusterfs --entry-timeout=0 --attribute-timeout=0 -s non-existent -s $H0 --volfile-id=/$V0 $M0

cleanup;
