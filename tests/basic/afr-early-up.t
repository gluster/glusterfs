#!/bin/bash

. $(dirname $0)/../include.rc
. $(dirname $0)/../volume.rc
. $(dirname $0)/../dht.rc

function mount_gluster ()
{
  local host=$1
  local volume=$2
  local mount=$3
  local timeout=$4

  if ! glusterfs -s $host --volfile-id $volume $mount; then
    echo "N"
    return
  fi

  if ! timeout -s 9 $timeout stat $mount; then
    echo "N"
    return
  fi

  echo "Y"
}

cleanup;

TEST glusterd
TEST pidof glusterd
TEST $CLI volume info;

TEST $CLI volume create $V0 replica 3  $H0:$B0/${V0}{1,2,3};

EXPECT "$V0" volinfo_field $V0 'Volume Name';
EXPECT 'Created' volinfo_field $V0 'Status';
EXPECT '3' brick_count $V0

TEST $CLI volume start $V0;
EXPECT 'Started' volinfo_field $V0 'Status';

BRICKPORT=$(pgrep -fl glusterfsd | head -n1 | sed 's/^.*listen-port=//')

TEST $CLI volume set $V0 cluster.quorum-type auto
EXPECT auto volume_option $V0 cluster.quorum-type

# Use iptables to block access to one of the brick ports.
BRICKPORT=$(pgrep -fla glusterfsd | head -n1 | sed 's/^.*listen-port=//')
iptables -A INPUT -p tcp --dport $BRICKPORT -j DROP
ip6tables -A INPUT -p tcp --dport $BRICKPORT -j DROP

# Should still be able to mount within 10 seconds even though brick is
# unreachable.
EXPECT "Y" mount_gluster $H0 $V0 $M0 10

# Mount should be writable (we should have quorum)
TEST dd if=/dev/zero of=$M0/test.out bs=128K count=1 conv=fsync

iptables -D INPUT -p tcp --dport $BRICKPORT -j DROP
ip6tables -D INPUT -p tcp --dport $BRICKPORT -j DROP

cleanup;
