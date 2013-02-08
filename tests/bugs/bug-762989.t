#!/bin/bash

. $(dirname $0)/../include.rc

cleanup;

## reserve port 1023
older_ports=$(cat /proc/sys/net/ipv4/ip_local_reserved_ports);
echo "1023" > /proc/sys/net/ipv4/ip_local_reserved_ports;

## Start and create a volume
TEST glusterd
TEST pidof glusterd
TEST $CLI volume info;

TEST $CLI volume create $V0 replica 2 stripe 2 $H0:$B0/${V0}{1,2,3,4,5,6,7,8};

TEST $CLI volume start $V0;

TEST glusterfs --entry-timeout=0 --attribute-timeout=0 -s $H0 --volfile-id $V0 \
$M0;

## Wait for volume to register with rpc.mountd
sleep 6;
## check if port 1023 (which has been reserved) is used by the gluster processes
op=$(netstat -ntp | grep gluster | grep -w 1023);
EXPECT "" echo $op;

#set the reserved ports to the older values
echo $older_ports > /proc/sys/net/ipv4/ip_local_reserved_ports

cleanup;
