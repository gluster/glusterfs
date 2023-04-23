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

EXPECT "NO" ${IN_RANGE} "10.57.49.248/28" "10.57.49.239"
EXPECT "NO" ${IN_RANGE} "10.57.49.248/27" "10.57.49.223"
EXPECT "NO" ${IN_RANGE} "10.57.49.248/29" "10.57.49.247"
EXPECT "NO" ${IN_RANGE} "10.57.49.248/26" "10.57.49.190"

EXPECT "NO" ${IN_RANGE} "10.57.49.224/28" "10.57.49.223"
EXPECT "YES" ${IN_RANGE} "10.57.49.224/28" "10.57.49.224"
EXPECT "YES" ${IN_RANGE} "10.57.49.224/28" "10.57.49.239"
EXPECT "NO" ${IN_RANGE} "10.57.49.224/28" "10.57.49.240"

EXPECT "NO" ${IN_RANGE} "10.57.49.0/24" "10.57.48.255"
EXPECT "YES" ${IN_RANGE} "10.57.49.0/24" "10.57.49.0"
EXPECT "YES" ${IN_RANGE} "10.57.49.0/24" "10.57.49.255"
EXPECT "NO" ${IN_RANGE} "10.57.49.0/24" "10.57.50.0"

rm -f ${IN_RANGE}
cleanup
