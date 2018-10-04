#!/bin/bash

. $(dirname $0)/../../include.rc
. $(dirname $0)/../../volume.rc
. $(dirname $0)/../../afr.rc

function create_fake_links() {
        local dst="$1"
        local dir="$2"
        local end=0
        local start=0
        local src

        src="$(ls ${dst}/.glusterfs/indices/${dir}/${dir}-* | head -1)"
        mkdir -p ${dst}/.glusterfs/dummy/${dir}
        while ln ${src} ${dst}/.glusterfs/dummy/${dir}/link-${end}; do
                end="$((${end} + 1))"
        done

        if [[ ${end} -gt 50 ]]; then
                start="$((${end} - 50))"
        fi
        if [[ ${end} -gt 0 ]]; then
                end="$((${end} - 1))"
        fi

        for i in $(seq ${start} ${end}); do
                rm -f ${dst}/.glusterfs/dummy/${dir}/link-${i}
        done
}

function count_fake_links() {
        local dst="$1"
        local dir="$2"

        echo "$(find ${dst}/.glusterfs/dummy/${dir}/ -name "link-*" | wc -l)"
}

cleanup

TEST glusterd
TEST pidof glusterd
TEST $CLI volume info

TEST touch $B0/ext4-1
TEST touch $B0/ext4-2
TEST touch $B0/ext4-3
TEST truncate -s 2GB $B0/ext4-1
TEST truncate -s 2GB $B0/ext4-2
TEST truncate -s 2GB $B0/ext4-3

TEST mkfs.ext4 -F $B0/ext4-1
TEST mkfs.ext4 -F $B0/ext4-2
TEST mkfs.ext4 -F $B0/ext4-3
TEST mkdir $B0/ext41
TEST mkdir $B0/ext42
TEST mkdir $B0/ext43
TEST mount -t ext4 -o loop $B0/ext4-1 $B0/ext41
TEST mount -t ext4 -o loop $B0/ext4-2 $B0/ext42
TEST mount -t ext4 -o loop $B0/ext4-3 $B0/ext43

TEST $CLI volume create $V0 replica 3 $H0:$B0/ext4{1,2,3}
TEST $CLI volume start $V0
TEST $CLI volume heal $V0 granular-entry-heal enable
TEST $GFS --volfile-id=$V0 --volfile-server=$H0 $M0
TEST kill_brick $V0 $H0 $B0/ext41

# Make sure indices exist and are initialized
TEST touch $M0/dummy

# Create enough hard links on bricks to make it fail faster. This is much
# faster than creating ~70000 files on a volume.
create_fake_links $B0/ext42 xattrop &
create_fake_links $B0/ext42 entry-changes &
wait
count_xattrop="$(count_fake_links $B0/ext42 xattrop)"
count_entry="$(count_fake_links $B0/ext42 entry-changes)"

TEST mkdir $M0/d{1..10}
TEST touch $M0/d{1..10}/{1..10}

#On ext4 max number of hardlinks is ~65k, so there should be 2 base index files
EXPECT "^2$" echo $(ls $B0/ext42/.glusterfs/indices/xattrop | grep xattrop | wc -l)
EXPECT "^2$" echo $(ls $B0/ext42/.glusterfs/indices/entry-changes | grep entry-changes | wc -l)

#Number of hardlinks: count_xattrop/count_entry for fake links, 101 for files,
# 10 for dirs and 2 for base-indices and root-dir for xattrop
EXPECT "$((${count_xattrop} + 114))" echo $(ls -l $B0/ext42/.glusterfs/indices/xattrop | grep xattrop | awk '{sum+=$2} END{print sum}')
EXPECT "$((${count_entry} + 113))" echo $(ls -l $B0/ext42/.glusterfs/indices/entry-changes | grep entry-changes | awk '{sum+=$2} END{print sum}')

cleanup
