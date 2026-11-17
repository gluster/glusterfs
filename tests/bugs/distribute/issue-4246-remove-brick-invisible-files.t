#!/bin/bash

. $(dirname $0)/../../include.rc
. $(dirname $0)/../../volume.rc

# This test reproduces github issue #4246: files that are still on a brick
# being removed (decommissioned) become invisible to fresh lookups when the
# rebalance daemon dies after fixing the directory layout but before
# migrating the files. With lookup-optimize enabled and matching commit
# hashes, DHT trusts the ENOENT from the hashed subvolume and never checks
# the brick that holds the data. The files become visible again only after
# a readdir (ls/find) in the parent directory.

cleanup;

TEST glusterd
TEST pidof glusterd

TEST $CLI volume create $V0 $H0:$B0/${V0}1 $H0:$B0/${V0}2 $H0:$B0/${V0}3 $H0:$B0/${V0}4
TEST $CLI volume set $V0 cluster.lookup-optimize on
TEST $CLI volume set $V0 performance.nl-cache off
TEST $CLI volume set $V0 features.cache-invalidation off
TEST $CLI volume start $V0
TEST glusterfs --volfile-id=/$V0 --volfile-server=$H0 $M0 --attribute-timeout=0 --entry-timeout=0

TEST mkdir $M0/dir
for i in $(seq 1 4000); do
    echo data > $M0/dir/f$i
done
TEST [ "$(ls $M0/dir | wc -l)" = "4000" ]

# remove brick 4: the rebalance daemon rewrites the layout of every
# directory (decommissioned brick gets a zero hash range) BEFORE migrating
# that directory's files
TEST $CLI volume remove-brick $V0 $H0:$B0/${V0}4 start

# wait until the layout of dir is settled on disk (brick4's own layout
# xattr gets a zero stop offset), then kill the rebalance daemon to freeze
# the mid-migration state, the way a crashed or failing rebalance would
settled=no
for i in $(seq 1 300); do
    v=$(getfattr -e hex -n trusted.glusterfs.dht $B0/${V0}4/dir 2>/dev/null | grep -oE '0x[0-9a-fA-F]+')
    if [ "${v: -8}" = "00000000" ]; then
        settled=yes
        pkill -9 -f "process-name rebalance"
        break
    fi
    sleep 0.1
done
TEST [ "$settled" = "yes" ]

# a fresh client (no cached dcache entries) must still be able to find
# every file that exists on the bricks
TEST glusterfs --volfile-id=/$V0 --volfile-server=$H0 $M1 --attribute-timeout=0 --entry-timeout=0

missing=0
checked=0
for f in $(ls $B0/${V0}4/dir 2>/dev/null | head -100); do
    checked=$((checked + 1))
    stat $M1/dir/$f 2>/dev/null || missing=$((missing + 1))
done
TEST [ "$checked" -gt 0 ]
TEST [ "$missing" -eq 0 ]

cleanup
