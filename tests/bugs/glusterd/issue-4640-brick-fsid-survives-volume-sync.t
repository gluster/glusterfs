#!/bin/bash

# gluster/glusterfs#4640 - after a node that was down during a volume
# transaction rejoins, it imports the volume from a peer. The import used to
# rebuild every brickinfo without its statfs_fsid, so glusterd stored
# brick-fsid=0 for all bricks and regenerated the brick volfiles with every
# local brick "sharing" fsid 0: option shared-brick-count N (N = bricks on the
# node). posix divides statvfs by N, disperse takes the per-set minimum, and
# df ends up reporting exactly one data-set for the whole volume.
#
# Three properties are checked, one per fix:
#  A. the locally observed fsids survive a peer import (volume sync)
#  B. the restart-time fsid repair does not persist a value taken from a
#     directory that is not the brick (no trusted.glusterfs.volume-id)
#  C. an unknown fsid (0) is never counted as shared with another brick

. $(dirname $0)/../../include.rc
. $(dirname $0)/../../cluster.rc
. $(dirname $0)/../../volume.rc

function check_peers {
        eval \$CLI_$1 peer status | grep 'Peer in Cluster (Connected)' | wc -l
}

# volume version as persisted by glusterd N
function gd_vol_version {
        local wd=$(eval \$CLI_$1 system getwd)
        grep '^version=' $wd/vols/$V0/info | cut -d= -f2
}

# brick-fsid as persisted by glusterd N for brick <host>:<path>
function brick_fsid {
        local wd=$(eval \$CLI_$1 system getwd)
        grep '^brick-fsid=' $wd/vols/$V0/bricks/$2:$(echo $3 | tr / -) | cut -d= -f2
}

# shared-brick-count in the brick volfile glusterd N generated for <host>:<path>
function shared_brick_count {
        local wd=$(eval \$CLI_$1 system getwd)
        local p=${3#/}
        grep -o 'shared-brick-count [0-9]*' $wd/vols/$V0/$V0.$2.$(echo $p | tr / -).vol | awk '{print $2}'
}

cleanup

TEST launch_cluster 3
TEST $CLI_1 peer probe $H2
TEST $CLI_1 peer probe $H3
EXPECT_WITHIN $PROBE_TIMEOUT 2 check_peers 1
EXPECT_WITHIN $PROBE_TIMEOUT 2 check_peers 2
EXPECT_WITHIN $PROBE_TIMEOUT 2 check_peers 3

# Two bricks on node 3, so that "how many of my bricks share a filesystem" is a
# real question there. In this harness both live on the same filesystem, so the
# legitimate shared-brick-count for them is 2.
B3_1=$B3/${V0}_1
B3_2=$B3/${V0}_2
TEST $CLI_1 volume create $V0 $H1:$B1/${V0}_1 $H2:$B2/${V0}_1 $H3:$B3_1 $H3:$B3_2
TEST $CLI_1 volume start $V0
EXPECT_WITHIN $PROCESS_UP_TIMEOUT 'Started' volinfo_field_1 $V0 'Status'

fsid_1=$(brick_fsid 3 $H3 $B3_1)
fsid_2=$(brick_fsid 3 $H3 $B3_2)
TEST [ -n "$fsid_1" -a "$fsid_1" != 0 ]
TEST [ -n "$fsid_2" -a "$fsid_2" != 0 ]
EXPECT 2 shared_brick_count 3 $H3 $B3_1
EXPECT 2 shared_brick_count 3 $H3 $B3_2

# ---- A. node 3 is down while the cluster changes the volume -----------------
TEST kill_node 3
EXPECT_WITHIN $PROBE_TIMEOUT 1 check_peers 1
TEST $CLI_1 volume stop $V0
TEST $CLI_1 volume start $V0
EXPECT_WITHIN $PROCESS_UP_TIMEOUT 'Started' volinfo_field_1 $V0 'Status'

# node 3 comes back with an older volume version and imports it from a peer
TEST $glusterd_3
EXPECT_WITHIN $PROBE_TIMEOUT 2 check_peers 1
EXPECT_WITHIN $PROBE_TIMEOUT 2 check_peers 3
EXPECT_WITHIN $PROBE_TIMEOUT "$(gd_vol_version 1)" gd_vol_version 3

# the import must not lose the fsids glusterd 3 observed at volume create ...
EXPECT "$fsid_1" brick_fsid 3 $H3 $B3_1
EXPECT "$fsid_2" brick_fsid 3 $H3 $B3_2
# ... and the regenerated brick volfiles keep the true shared-brick-count
EXPECT 2 shared_brick_count 3 $H3 $B3_1
EXPECT 2 shared_brick_count 3 $H3 $B3_2

# ---- B. + C. a zero fsid stays unknown, and unknown is never "shared" --------
# The restart-time repair keeps its result in memory; the store is rewritten by
# the next transaction, so every store assertion below follows a 'volume set'.
TEST $CLI_1 volume stop $V0
EXPECT_WITHIN $PROCESS_DOWN_TIMEOUT 'Stopped' volinfo_field_1 $V0 'Status'
GD3_WD=$($CLI_3 system getwd)
TEST kill_glusterd 3
volid_hex=$(getfattr -n trusted.glusterfs.volume-id -e hex --absolute-names $B3_1 | sed -n 's/^trusted.glusterfs.volume-id=//p')
TEST [ -n "$volid_hex" ]
# simulate the shape left behind by an import with no old volinfo, or an
# operator restarting glusterd with the brick filesystems not mounted: the
# stored fsid is 0 and the brick directories are not the bricks
TEST sed -i 's/^brick-fsid=.*/brick-fsid=0/' $GD3_WD/vols/$V0/bricks/$H3:*
TEST setfattr -x trusted.glusterfs.volume-id $B3_1
TEST setfattr -x trusted.glusterfs.volume-id $B3_2
TEST start_glusterd 3
EXPECT_WITHIN $PROBE_TIMEOUT 2 check_peers 3
TEST $CLI_1 volume set $V0 cluster.min-free-inodes 6%
EXPECT_WITHIN $PROBE_TIMEOUT "$(gd_vol_version 1)" gd_vol_version 3

# B. the restart-time repair must not take an fsid from a non-brick directory
EXPECT 0 brick_fsid 3 $H3 $B3_1
EXPECT 0 brick_fsid 3 $H3 $B3_2
# C. regenerating the volfiles with two unknown fsids must not divide by 2
EXPECT 1 shared_brick_count 3 $H3 $B3_1
EXPECT 1 shared_brick_count 3 $H3 $B3_2

# ---- D. once the bricks are back, a restart repairs and the truth returns ----
TEST kill_glusterd 3
TEST setfattr -n trusted.glusterfs.volume-id -v $volid_hex $B3_1
TEST setfattr -n trusted.glusterfs.volume-id -v $volid_hex $B3_2
TEST start_glusterd 3
EXPECT_WITHIN $PROBE_TIMEOUT 2 check_peers 3
TEST $CLI_1 volume set $V0 cluster.min-free-inodes 7%
EXPECT_WITHIN $PROBE_TIMEOUT "$(gd_vol_version 1)" gd_vol_version 3
EXPECT "$fsid_1" brick_fsid 3 $H3 $B3_1
EXPECT "$fsid_2" brick_fsid 3 $H3 $B3_2
EXPECT 2 shared_brick_count 3 $H3 $B3_1
EXPECT 2 shared_brick_count 3 $H3 $B3_2

cleanup
