#!/bin/bash

. $(dirname $0)/../../include.rc
. $(dirname $0)/../../cluster.rc


cleanup;

## Setting Port number in specific range
sysctl net.ipv4.ip_local_reserved_ports="24007-24008,32765-32768,49152-49156"

## Start a 2 node virtual cluster
TEST launch_cluster 2;


## Peer probe server 2 from server 1 cli
TEST $CLI_1 peer probe $H2;

EXPECT_WITHIN $PROBE_TIMEOUT 1 peer_count

sysctl net.ipv4.ip_local_reserved_ports="
"

cleanup;

