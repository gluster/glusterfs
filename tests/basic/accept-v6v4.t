#!/bin/bash

. $(dirname $0)/../nfs.rc

#
# This test ensures that GlusterFS provides NFS, Mount and its Management daemon
# over both IPv4 and IPv6. It uses netcat to check the services running on both
# IPv4 & IPv6 addresses as well as a mount to test that mount & nfs work.
#

IPV4_SUPPORT=false
IPV6_SUPPORT=false

host $HOSTNAME | grep -q "has address" && IPV4_SUPPORT=true
host $HOSTNAME | grep -q "has IPv6 address" && IPV6_SUPPORT=true

. $(dirname $0)/../include.rc

cleanup;

mkdir -p $B0/b{0,1,2}

# make sure no registered rpcbind services are running
service rpcbind restart

TEST glusterd
TEST pidof glusterd

TEST $CLI vol create $V0 replica 3 $H0:$B0/b0 $H0:$B0/b1 $H0:$B0/b2

TEST $CLI vol set $V0 cluster.self-heal-daemon off
TEST $CLI vol set $V0 nfs.disable off
TEST $CLI vol set $V0 cluster.choose-local off
TEST $CLI vol start $V0

MOUNTD_PORT=38465
MGMTD_PORT=24007
NFSD_PORT=2049

function check_ip_port {
        ip=$1
        port=$2
        type=$3

        nc_flags=""
        if [ "$type" == "v6" ] && [ "$ip" == "NONE" ]; then
          echo "Y"
          return
        else
          nc_flags="-6"
        fi

        if [ "$type" == "v4" ] && [ "$ip" == "NONE" ]; then
          echo "Y"
          return
        fi

        if exec 3<>/dev/tcp/$ip/$port; then
          echo "Y"
        else
          echo "N"
        fi
}

function check_nfs {
        ip=$1
        type=$2

        if [ "$ip" == "NONE" ]; then
          echo "Y"
          return
        fi

        if [ "$type" == "v6" ]; then
          addr="[$ip]"
        else
          addr="$ip"
        fi

        if mount_nfs $addr:/$V0 $N0; then
          umount_nfs $N0
          echo "Y"
        else
          echo "N"
        fi
}

if [ ! $IPV4_SUPPORT ] && [ ! $IPV6_SUPPORT ]; then
  exit 1
fi

# Get the V4 & V6 addresses of this host
if $IPV4_SUPPORT; then
  V4=$(host $HOSTNAME | head -n1 | awk -F ' ' '{print $4}')
else
  V4="NONE"
fi

if $IPV6_SUPPORT; then
  V6=$(host $HOSTNAME | tail -n1 | awk -F ' ' '{print $5}')
else
  V6="NONE"
fi

# First check the management daemon
EXPECT "Y" check_ip_port $V6 $MGMTD_PORT "v6"
EXPECT "Y" check_ip_port $V4 $MGMTD_PORT "v4"

# Give the MOUNT/NFS Daemon some time to start up
sleep 4

EXPECT "Y" check_ip_port $V4 $MOUNTD_PORT "v6"
EXPECT "Y" check_ip_port $V6 $MOUNTD_PORT "v4"

EXPECT "Y" check_ip_port $V4 $NFSD_PORT "v6"
EXPECT "Y" check_ip_port $V6 $NFSD_PORT "v4"

# Mount the file system
EXPECT "Y" check_nfs $V6 "v6"
EXPECT "Y" check_nfs $V4 "v4"

cleanup;
