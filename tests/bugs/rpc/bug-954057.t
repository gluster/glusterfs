#!/bin/bash

. $(dirname $0)/../../include.rc
. $(dirname $0)/../../volume.rc

# This script checks if use-readdirp option works as accepted in mount options

# Note on re-reading $M0/new after enabling root-squash:
# Since we have readen it once, the file is present in various caches.
# In order to actually fail on second attempt we must:
# 1) drop kernel cache
# 2) make sure FUSE does not cache the entry. This is also
#    in the kernel, but not flushed by a failed umount.
#    Using $GFS enforces this because it sets --entry-timeout=0
# 3) make sure reading new permissins does not produce stale
#    information from glusterfs metadata cache. Setting volume
#    option performance.stat-prefetch off enforces that.

TEST glusterd
TEST pidof glusterd
TEST $CLI volume create $V0 $H0:$B0/${V0}
TEST $CLI volume start $V0

TEST $GFS --volfile-id=/$V0 --volfile-server=$H0 $M0

TEST mkdir $M0/dir
TEST mkdir $M0/nobody
TEST chown nfsnobody:nfsnobody $M0/nobody
TEST `echo "file" >> $M0/file`
TEST cp $M0/file $M0/new
TEST chmod 700 $M0/new
TEST cat $M0/new

TEST $CLI volume set $V0 performance.stat-prefetch off
TEST $CLI volume set $V0 server.root-squash enable
drop_cache $M0
TEST ! mkdir $M0/other
TEST mkdir $M0/nobody/other
TEST cat $M0/file
TEST ! cat $M0/new
TEST `echo "nobody" >> $M0/nobody/file`

#mount the client without root-squashing
TEST $GFS --volfile-id=/$V0 --volfile-server=$H0 --no-root-squash=yes $M1
TEST mkdir $M1/m1_dir
TEST `echo "file" >> $M1/m1_file`
TEST cp $M0/file $M1/new
TEST chmod 700 $M1/new
TEST cat $M1/new

TEST $CLI volume set $V0 server.root-squash disable
TEST mkdir $M0/other
TEST cat $M0/new

cleanup
