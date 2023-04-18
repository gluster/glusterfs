#!/bin/bash

. $(dirname $0)/../include.rc
. $(dirname $0)/../volume.rc

cleanup

IN_RANGE=$(dirname $0)/ip_in_cidr
build_tester $(dirname $0)/../utils/ip-in-cidr.c -o ${IN_RANGE}

EXPECT "YES" ${IN_RANGE} "10.57.49.248/28" "10.57.49.240"
EXPECT "YES" ${IN_RANGE} "10.57.49.248/27" "10.57.49.224"
EXPECT "YES" ${IN_RANGE} "10.57.49.248/26" "10.57.49.192"
EXPECT "YES" ${IN_RANGE} "10.57.49.248/29" "10.57.49.248"

EXPECT "NO" ${IN_RANGE} "10.57.49.248/28" "10.57.48.239"
EXPECT "NO" ${IN_RANGE} "10.57.49.248/27" "10.57.48.223"
EXPECT "NO" ${IN_RANGE} "10.57.49.248/29" "10.57.48.247"
EXPECT "NO" ${IN_RANGE} "10.57.49.248/26" "10.57.48.190"

rm -f ${IN_RANGE}
cleanup
