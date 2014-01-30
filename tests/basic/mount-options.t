#!/bin/bash

. $(dirname $0)/../include.rc
. $(dirname $0)/../volume.rc

cleanup;

TEST glusterd --xlator-option=*.rpc-auth-allow-insecure=on
TEST pidof glusterd
TEST $CLI volume create $V0 replica 2 $H0:$B0/${V0}{0,1,2,3,4,5}
TEST $CLI volume set $V0 server.allow-insecure on
TEST $CLI volume start $V0

#test all the options available to see if the mount succeeds with those options
#or not. This does not test functionality. This is added to prevent options
#being removed in future breaking backward-compatibility.

TEST glusterfs --volfile-id=/$V0 --volfile-server=$H0 $M0
TEST umount -l $M0

TEST glusterfs --volfile-id=/$V0 --volfile-server=$H0 $M0 --attribute-timeout=0
TEST umount -l $M0

TEST glusterfs --volfile-id=/$V0 --volfile-server=$H0 $M0 --entry-timeout=0
TEST umount -l $M0

TEST glusterfs --volfile=/var/lib/glusterd/vols/$V0/${V0}-fuse.vol $M0
TEST umount -l $M0

TEST glusterfs --volfile-id=/$V0 --volfile-server=$H0 --log-file=/tmp/a.txt --log-level=DEBUG $M0
EXPECT_NOT "0" wc -l /tmp/a.txt
TEST grep " D " /tmp/a.txt
TEST rm -f /tmp/a.txt
TEST umount -l $M0

TEST glusterfs --volfile-id=/$V0 --volfile-server=$H0 $M0 --acl
TEST umount -l $M0

TEST glusterfs --volfile-id=/$V0 --volfile-server=$H0 $M0 --aux-gfid-mount
TEST umount -l $M0

TEST glusterfs --volfile-id=/$V0 --volfile-server=$H0 $M0 --enable-ino32
TEST umount -l $M0

TEST glusterfs --volfile-id=/$V0 --volfile-server=$H0 $M0 --fopen-keep-cache=yes
TEST umount -l $M0
TEST glusterfs --volfile-id=/$V0 --volfile-server=$H0 $M0 --fopen-keep-cache=no
TEST umount -l $M0
TEST ! glusterfs --volfile-id=/$V0 --volfile-server=$H0 $M0 --fopen-keep-cache=fail

TEST glusterfs --volfile-id=/$V0 --volfile-server=$H0 $M0 --mac-compat=yes
TEST umount -l $M0
TEST glusterfs --volfile-id=/$V0 --volfile-server=$H0 $M0 --mac-compat=no
TEST umount -l $M0
TEST ! glusterfs --volfile-id=/$V0 --volfile-server=$H0 $M0 --mac-compat=fail

TEST glusterfs --volfile-id=/$V0 --volfile-server=$H0 $M0 --use-readdirp=yes
TEST umount -l $M0
TEST glusterfs --volfile-id=/$V0 --volfile-server=$H0 $M0 --use-readdirp=no
TEST umount -l $M0
TEST ! glusterfs --volfile-id=/$V0 --volfile-server=$H0 $M0 --use-readdirp=fail

TEST glusterfs --volfile-id=/$V0 --volfile-server=$H0 $M0 --direct-io-mode=yes
TEST umount -l $M0
TEST glusterfs --volfile-id=/$V0 --volfile-server=$H0 $M0 --direct-io-mode=no
TEST umount -l $M0
TEST ! glusterfs --volfile-id=/$V0 --volfile-server=$H0 $M0 --direct-io-mode=fail

TEST glusterfs --volfile-id=/$V0 --volfile-server=$H0 $M0 --read-only
TEST umount -l $M0

TEST glusterfs --volfile-id=/$V0 --volfile-server=$H0 $M0 --selinux
TEST umount -l $M0

TEST glusterfs --volfile-id=/$V0 --volfile-server=$H0 $M0 --worm
TEST umount -l $M0

TEST glusterfs --volfile-id=/$V0 --volfile-server=$H0 $M0 --volfile-check
TEST umount -l $M0

TEST glusterfs --volfile-id=/$V0 --volfile-server=$H0 $M0 --dump-fuse=/tmp/a.txt
EXPECT "0" stat /tmp/a.txt
TEST rm -f /tmp/a.txt
TEST umount -l $M0

TEST glusterfs --volfile-id=/$V0 --volfile-server=$H0 $M0 --gid-timeout=0
TEST umount -l $M0

TEST glusterfs --volfile-id=/$V0 --volfile-server=$H0 $M0 --gid-timeout=-1
TEST umount -l $M0

TEST ! glusterfs --volfile-id=/$V0 --volfile-server=$H0 $M0 --gid-timeout=abc

TEST glusterfs --volfile-id=/$V0 --volfile-server=$H0 $M0 --background-qlen=16
TEST umount -l $M0

TEST ! glusterfs --volfile-id=/$V0 --volfile-server=$H0 $M0 --background-qlen=abc

TEST ! glusterfs --volfile-id=/$V0 --volfile-server=$H0 $M0 --background-qlen=-1

TEST glusterfs --volfile-id=/$V0 --volfile-server=$H0 $M0 --congestion-threshold=12
TEST umount -l $M0

TEST ! glusterfs --volfile-id=/$V0 --volfile-server=$H0 $M0 --congestion-threshold=abc

TEST ! glusterfs --volfile-id=/$V0 --volfile-server=$H0 $M0 --congestion=threshold=-1

TEST glusterfs --volfile-id=/$V0 --volfile-server=$H0 $M0 --negative-timeout=10
TEST umount -l $M0

TEST ! glusterfs --volfile-id=/$V0 --volfile-server=$H0 $M0 --negative-timeout=abc
TEST ! glusterfs --volfile-id=/$V0 --volfile-server=$H0 $M0 --negative-timeout=-1

TEST glusterfs --volfile-id=/$V0 --volfile-server=$H0 $M0 --pid-file=/tmp/a.txt
EXPECT_NOT "0" wc -l /tmp/a.txt
TEST rm -f /tmp/a.txt
TEST umount -l $M0

TEST glusterfs --volfile-id=/$V0 --volfile-server=$H0 $M0 --volfile-server-port=24007
TEST umount -l $M0

TEST ! glusterfs --volfile-id=/$V0 --volfile-server=$H0 $M0 --volfile-server-port=2400

TEST glusterfs --volfile-id=/$V0 --volfile-server=$H0 $M0 --volfile-server-transport=tcp
TEST umount -l $M0

TEST glusterfs --volfile-id=/$V0 --volfile-server=$H0 $M0 --volfile-server-transport=ib-verbs
TEST umount -l $M0

TEST ! glusterfs --volfile-id=/$V0 --volfile-server=$H0 $M0 --volfile-server-port=socket

TEST glusterfs --volfile-id=/$V0 --volfile-server=$H0 $M0 --volume-name=$V0
TEST umount -l $M0

TEST ! glusterfs --volfile-id=/$V0 --volfile-server=$H0 $M0 --volume-name=abcd

TEST ! glusterfs --volfile-id=/$V0 --volfile-server=$H0 $M0 --invalid-option

TEST ! glusterfs --volfile-id=/$V0 --volfile-server=$H0 $M0 --invalid-option=abc
cleanup;
