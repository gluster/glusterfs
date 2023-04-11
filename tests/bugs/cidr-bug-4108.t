#!/bin/bash

. $(dirname $0)/../include.rc
. $(dirname $0)/../volume.rc

cleanup

IN_RANGE=$(dirname $0)/ip_in_cidr
build_tester $(dirname $0)/../utils/ip-in-cidr.c -o ${IN_RANGE}

EXPECT "YES" ${IN_RANGE} "10.57.49.248/29" "10.57.49.250"
EXPECT "NO" ${IN_RANGE} "10.57.49.248/29" "10.57.48.250"

rm -f ${IN_RANGE}
cleanup
