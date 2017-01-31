#!/bin/bash

#This file checks if missing entry self-heal and entry self-heal are working
#as expected.
. $(dirname $0)/../../include.rc
. $(dirname $0)/../../volume.rc
. $(dirname $0)/../../afr.rc

cleanup;

function get_file_type {
        stat -c "%a:%F:%g:%t:%T:%u" $1
}

function diff_dirs {
        diff <(ls $1 | sort) <(ls $2 | sort)
}

function heal_status {
        local f1_path="${1}/${3}"
        local f2_path="${2}/${3}"
        local zero_xattr="000000000000000000000000"
        local insync=""
        diff_dirs $f1_path $f2_path
        if [ $? -eq 0 ];
        then
                insync="Y"
        else
                insync="N"
        fi
        local xattr11=$(get_hex_xattr trusted.afr.$V0-client-0 $f1_path)
        local xattr12=$(get_hex_xattr trusted.afr.$V0-client-1 $f1_path)
        local xattr21=$(get_hex_xattr trusted.afr.$V0-client-0 $f2_path)
        local xattr22=$(get_hex_xattr trusted.afr.$V0-client-1 $f2_path)
        local dirty1=$(get_hex_xattr trusted.afr.dirty $f1_path)
        local dirty2=$(get_hex_xattr trusted.afr.dirty $f2_path)
        if [ -z $xattr11 ]; then xattr11="000000000000000000000000"; fi
        if [ -z $xattr12 ]; then xattr12="000000000000000000000000"; fi
        if [ -z $xattr21 ]; then xattr21="000000000000000000000000"; fi
        if [ -z $xattr22 ]; then xattr22="000000000000000000000000"; fi
        if [ -z $dirty1 ]; then dirty1="000000000000000000000000"; fi
        if [ -z $dirty2 ]; then dirty2="000000000000000000000000"; fi
        echo ${insync}${xattr11}${xattr12}${xattr21}${xattr22}${dirty1}${dirty2}
}

function is_heal_done {
        local zero_xattr="000000000000000000000000"
        if [ "$(heal_status $@)" == "Y${zero_xattr}${zero_xattr}${zero_xattr}${zero_xattr}${zero_xattr}${zero_xattr}" ];
        then
                echo "Y"
        else
                echo "N"
        fi
}

function print_pending_heals {
        local result=":"
        for i in "$@";
        do
                if [ "N" == $(is_heal_done $B0/${V0}0 $B0/${V0}1 $i) ];
                then
                        result="$result:$i"
                fi
        done
#To prevent any match for EXPECT_WITHIN, print a char non-existent in file-names
        if [ $result == ":" ]; then result="~"; fi
        echo $result
}

zero_xattr="000000000000000000000000"
TEST glusterd
TEST pidof glusterd
TEST $CLI volume create $V0 replica 2 $H0:$B0/${V0}{0,1}
TEST $CLI volume set $V0 self-heal-daemon off
TEST $CLI volume set $V0 performance.write-behind off
TEST $CLI volume set $V0 performance.read-ahead off
TEST $CLI volume set $V0 performance.readdir-ahead off
TEST $CLI volume set $V0 performance.open-behind off
TEST $CLI volume set $V0 performance.stat-prefetch off
TEST $CLI volume set $V0 performance.io-cache off
TEST $CLI volume set $V0 performance.quick-read off
TEST $CLI volume start $V0

TEST $GFS --volfile-id=/$V0 --volfile-server=$H0 --use-readdirp=no $M0
cd $M0
#_me_ is dir on which missing entry self-heal happens, _heal is where dir self-heal happens
#spb is split-brain, fool is all fool

#source_self_accusing means there exists source and a sink which self-accuses.
#This simulates failures where fops failed on the bricks without it going down.
#Something like EACCESS/EDQUOT etc

TEST mkdir spb_heal spb spb_me_heal spb_me fool_heal fool_me v1_fool_heal v1_fool_me source_creations_heal source_deletions_heal source_creations_me source_deletions_me v1_dirty_me v1_dirty_heal source_self_accusing
TEST mkfifo source_deletions_heal/fifo
TEST mknod  source_deletions_heal/block b 4 5
TEST mknod  source_deletions_heal/char c 1 5
TEST touch  source_deletions_heal/file
TEST ln -s  source_deletions_heal/file source_deletions_heal/slink
TEST mkdir  source_deletions_heal/dir1
TEST mkdir  source_deletions_heal/dir1/dir2

TEST mkfifo source_deletions_me/fifo
TEST mknod  source_deletions_me/block b 4 5
TEST mknod  source_deletions_me/char c 1 5
TEST touch  source_deletions_me/file
TEST ln -s  source_deletions_me/file source_deletions_me/slink
TEST mkdir  source_deletions_me/dir1
TEST mkdir  source_deletions_me/dir1/dir2

TEST mkfifo source_self_accusing/fifo
TEST mknod  source_self_accusing/block b 4 5
TEST mknod  source_self_accusing/char c 1 5
TEST touch  source_self_accusing/file
TEST ln -s  source_self_accusing/file source_self_accusing/slink
TEST mkdir  source_self_accusing/dir1
TEST mkdir  source_self_accusing/dir1/dir2

TEST kill_brick $V0 $H0 $B0/${V0}0

TEST touch spb_heal/0 spb/0 spb_me_heal/0 spb_me/0 fool_heal/0 fool_me/0 v1_fool_heal/0 v1_fool_me/0 v1_dirty_heal/0 v1_dirty_me/0
TEST rm -rf source_deletions_heal/fifo source_deletions_heal/block source_deletions_heal/char source_deletions_heal/file source_deletions_heal/slink source_deletions_heal/dir1
TEST rm -rf source_deletions_me/fifo source_deletions_me/block source_deletions_me/char source_deletions_me/file source_deletions_me/slink source_deletions_me/dir1
TEST rm -rf source_self_accusing/fifo source_self_accusing/block source_self_accusing/char source_self_accusing/file source_self_accusing/slink source_self_accusing/dir1

#Test that the files are deleted
TEST ! stat $B0/${V0}1/source_deletions_heal/fifo
TEST ! stat $B0/${V0}1/source_deletions_heal/block
TEST ! stat $B0/${V0}1/source_deletions_heal/char
TEST ! stat $B0/${V0}1/source_deletions_heal/file
TEST ! stat $B0/${V0}1/source_deletions_heal/slink
TEST ! stat $B0/${V0}1/source_deletions_heal/dir1
TEST ! stat $B0/${V0}1/source_deletions_me/fifo
TEST ! stat $B0/${V0}1/source_deletions_me/block
TEST ! stat $B0/${V0}1/source_deletions_me/char
TEST ! stat $B0/${V0}1/source_deletions_me/file
TEST ! stat $B0/${V0}1/source_deletions_me/slink
TEST ! stat $B0/${V0}1/source_deletions_me/dir1
TEST ! stat $B0/${V0}1/source_self_accusing/fifo
TEST ! stat $B0/${V0}1/source_self_accusing/block
TEST ! stat $B0/${V0}1/source_self_accusing/char
TEST ! stat $B0/${V0}1/source_self_accusing/file
TEST ! stat $B0/${V0}1/source_self_accusing/slink
TEST ! stat $B0/${V0}1/source_self_accusing/dir1


TEST mkfifo source_creations_heal/fifo
TEST mknod  source_creations_heal/block b 4 5
TEST mknod  source_creations_heal/char c 1 5
TEST touch  source_creations_heal/file
TEST ln -s  source_creations_heal/file source_creations_heal/slink
TEST mkdir  source_creations_heal/dir1
TEST mkdir  source_creations_heal/dir1/dir2

TEST mkfifo source_creations_me/fifo
TEST mknod  source_creations_me/block b 4 5
TEST mknod  source_creations_me/char c 1 5
TEST touch  source_creations_me/file
TEST ln -s  source_creations_me/file source_creations_me/slink
TEST mkdir  source_creations_me/dir1
TEST mkdir  source_creations_me/dir1/dir2

$CLI volume stop $V0

#simulate fool fool scenario for fool_* dirs
setfattr -x trusted.afr.$V0-client-0 $B0/${V0}1/{fool_heal,fool_me}
setfattr -n trusted.afr.dirty -v 0x000000000000000000000001 $B0/${V0}1/{fool_heal,fool_me}
setfattr -n trusted.afr.$V0-client-1 -v 0x000000000000000000000001 $B0/${V0}1/{v1_fool_heal,v1_fool_me}

#Simulate v1-dirty(self-accusing but no pending ops on others) scenario for v1-dirty
setfattr -x trusted.afr.$V0-client-0 $B0/${V0}1/v1_dirty_{heal,me}
setfattr -n trusted.afr.$V0-client-1 -v 0x000000000000000000000001 $B0/${V0}1/v1_dirty_{heal,me}

$CLI volume start $V0 force
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "1" afr_child_up_status $V0 0
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "1" afr_child_up_status $V0 1
TEST kill_brick $V0 $H0 $B0/${V0}1

TEST touch spb_heal/1 spb/0 spb_me_heal/1 spb_me/0 fool_heal/1 fool_me/1 v1_fool_heal/1 v1_fool_me/1

$CLI volume stop $V0

#simulate fool fool scenario for fool_* dirs
setfattr -x trusted.afr.$V0-client-1 $B0/${V0}0/{fool_heal,fool_me}
setfattr -n trusted.afr.dirty -v 0x000000000000000000000001 $B0/${V0}1/{fool_heal,fool_me}
setfattr -n trusted.afr.$V0-client-0 -v 0x000000000000000000000001 $B0/${V0}1/{v1_fool_heal,v1_fool_me}

#simulate self-accusing for source_self_accusing
TEST setfattr -n trusted.afr.$V0-client-0 -v 0x000000000000000000000006 $B0/${V0}0/source_self_accusing

$CLI volume start $V0 force
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "1" afr_child_up_status $V0 1
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "1" afr_child_up_status $V0 0

# Check if conservative merges happened correctly on _me_ dirs
TEST stat spb_me_heal/1
TEST stat $B0/${V0}0/spb_me_heal/1
TEST stat $B0/${V0}1/spb_me_heal/1

TEST stat spb_me_heal/0
TEST stat $B0/${V0}0/spb_me_heal/0
TEST stat $B0/${V0}1/spb_me_heal/0

TEST stat fool_me/1
TEST stat $B0/${V0}0/fool_me/1
TEST stat $B0/${V0}1/fool_me/1

TEST stat fool_me/0
TEST stat $B0/${V0}0/fool_me/0
TEST stat $B0/${V0}1/fool_me/0

TEST stat v1_fool_me/0
TEST stat $B0/${V0}0/v1_fool_me/0
TEST stat $B0/${V0}1/v1_fool_me/0

TEST stat v1_fool_me/1
TEST stat $B0/${V0}0/v1_fool_me/1
TEST stat $B0/${V0}1/v1_fool_me/1

TEST stat v1_dirty_me/0
TEST stat $B0/${V0}0/v1_dirty_me/0
TEST stat $B0/${V0}1/v1_dirty_me/0

#Check if files that have gfid-mismatches in _me_ are giving EIO
TEST ! stat spb_me/0

#Check if stale files are deleted on access
TEST ! stat source_deletions_me/fifo
TEST ! stat $B0/${V0}0/source_deletions_me/fifo
TEST ! stat $B0/${V0}1/source_deletions_me/fifo
TEST ! stat source_deletions_me/block
TEST ! stat $B0/${V0}0/source_deletions_me/block
TEST ! stat $B0/${V0}1/source_deletions_me/block
TEST ! stat source_deletions_me/char
TEST ! stat $B0/${V0}0/source_deletions_me/char
TEST ! stat $B0/${V0}1/source_deletions_me/char
TEST ! stat source_deletions_me/file
TEST ! stat $B0/${V0}0/source_deletions_me/file
TEST ! stat $B0/${V0}1/source_deletions_me/file
TEST ! stat source_deletions_me/file
TEST ! stat $B0/${V0}0/source_deletions_me/file
TEST ! stat $B0/${V0}1/source_deletions_me/file
TEST ! stat source_deletions_me/dir1/dir2
TEST ! stat $B0/${V0}0/source_deletions_me/dir1/dir2
TEST ! stat $B0/${V0}1/source_deletions_me/dir1/dir2
TEST ! stat source_deletions_me/dir1
TEST ! stat $B0/${V0}0/source_deletions_me/dir1
TEST ! stat $B0/${V0}1/source_deletions_me/dir1

#Test if the files created as part of access are healed correctly
r=$(get_file_type source_creations_me/fifo)
EXPECT "$r" get_file_type $B0/${V0}0/source_creations_me/fifo
EXPECT "$r" get_file_type $B0/${V0}1/source_creations_me/fifo
TEST [ -p source_creations_me/fifo ]

r=$(get_file_type source_creations_me/block)
EXPECT "$r" get_file_type $B0/${V0}0/source_creations_me/block
EXPECT "$r" get_file_type $B0/${V0}1/source_creations_me/block
EXPECT "^4 5$" stat -c "%t %T" $B0/${V0}1/source_creations_me/block
EXPECT "^4 5$" stat -c "%t %T" $B0/${V0}0/source_creations_me/block
TEST [ -b source_creations_me/block ]

r=$(get_file_type source_creations_me/char)
EXPECT "$r" get_file_type $B0/${V0}0/source_creations_me/char
EXPECT "$r" get_file_type $B0/${V0}1/source_creations_me/char
EXPECT "^1 5$" stat -c "%t %T" $B0/${V0}1/source_creations_me/char
EXPECT "^1 5$" stat -c "%t %T" $B0/${V0}0/source_creations_me/char
TEST [ -c source_creations_me/char ]

r=$(get_file_type source_creations_me/file)
EXPECT "$r" get_file_type $B0/${V0}0/source_creations_me/file
EXPECT "$r" get_file_type $B0/${V0}1/source_creations_me/file
TEST [ -f source_creations_me/file ]

r=$(get_file_type source_creations_me/slink)
EXPECT "$r" get_file_type $B0/${V0}0/source_creations_me/slink
EXPECT "$r" get_file_type $B0/${V0}1/source_creations_me/slink
TEST [ -h source_creations_me/slink ]

r=$(get_file_type source_creations_me/dir1/dir2)
EXPECT "$r" get_file_type $B0/${V0}0/source_creations_me/dir1/dir2
EXPECT "$r" get_file_type $B0/${V0}1/source_creations_me/dir1/dir2
TEST [ -d source_creations_me/dir1/dir2 ]

r=$(get_file_type source_creations_me/dir1)
EXPECT "$r" get_file_type $B0/${V0}0/source_creations_me/dir1
EXPECT "$r" get_file_type $B0/${V0}1/source_creations_me/dir1
TEST [ -d source_creations_me/dir1 ]

#Trigger heal and check _heal dirs are healed properly
#Trigger change in event generation number. That way inodes would get refreshed during lookup
TEST kill_brick $V0 $H0 $B0/${V0}1
$CLI volume start $V0 force
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "1" afr_child_up_status $V0 1
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "1" afr_child_up_status $V0 0

TEST stat spb_heal
TEST stat spb_me_heal
TEST stat fool_heal
TEST stat fool_me
TEST stat v1_fool_heal
TEST stat v1_fool_me
TEST stat source_deletions_heal
TEST stat source_deletions_me
TEST stat source_self_accusing
TEST stat source_creations_heal
TEST stat source_creations_me
TEST stat v1_dirty_heal
TEST stat v1_dirty_me
TEST $CLI volume stop $V0
TEST rm -rf $B0/${V0}{0,1}/.glusterfs/indices/xattrop/*

$CLI volume start $V0
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "1" afr_child_up_status $V0 1
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "1" afr_child_up_status $V0 0

#Create base entry in indices/xattrop
echo "Data" > $M0/FILE
rm -f $M0/FILE
EXPECT "1" count_index_entries $B0/${V0}0
EXPECT "1" count_index_entries $B0/${V0}1

TEST $CLI volume stop $V0;

#Create entries for fool_heal and fool_me to ensure they are fully healed and dirty xattrs erased, before triggering index heal
create_brick_xattrop_entry $B0/${V0}0 fool_heal fool_me source_creations_heal/dir1

$CLI volume start $V0
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "1" afr_child_up_status $V0 1
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "1" afr_child_up_status $V0 0

$CLI volume set $V0 self-heal-daemon on
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "Y" glustershd_up_status
EXPECT_WITHIN $CHILD_UP_TIMEOUT "1" afr_child_up_status_in_shd $V0 0
EXPECT_WITHIN $CHILD_UP_TIMEOUT "1" afr_child_up_status_in_shd $V0 1

TEST $CLI volume heal $V0;
EXPECT_WITHIN $HEAL_TIMEOUT "~" print_pending_heals spb_heal spb_me_heal fool_heal fool_me v1_fool_heal v1_fool_me source_deletions_heal source_deletions_me source_creations_heal source_creations_me v1_dirty_heal v1_dirty_me source_self_accusing

EXPECT "Y${zero_xattr}${zero_xattr}${zero_xattr}${zero_xattr}${zero_xattr}${zero_xattr}" heal_status $B0/${V0}0 $B0/${V0}1 spb_heal
EXPECT "Y${zero_xattr}${zero_xattr}${zero_xattr}${zero_xattr}${zero_xattr}${zero_xattr}" heal_status $B0/${V0}0 $B0/${V0}1 spb_me_heal
EXPECT "Y${zero_xattr}${zero_xattr}${zero_xattr}${zero_xattr}${zero_xattr}${zero_xattr}" heal_status $B0/${V0}0 $B0/${V0}1 fool_heal
EXPECT "Y${zero_xattr}${zero_xattr}${zero_xattr}${zero_xattr}${zero_xattr}${zero_xattr}" heal_status $B0/${V0}0 $B0/${V0}1 fool_me
EXPECT "Y${zero_xattr}${zero_xattr}${zero_xattr}${zero_xattr}${zero_xattr}${zero_xattr}" heal_status $B0/${V0}0 $B0/${V0}1 v1_fool_heal
EXPECT "Y${zero_xattr}${zero_xattr}${zero_xattr}${zero_xattr}${zero_xattr}${zero_xattr}" heal_status $B0/${V0}0 $B0/${V0}1 v1_fool_me
EXPECT "Y${zero_xattr}${zero_xattr}${zero_xattr}${zero_xattr}${zero_xattr}${zero_xattr}" heal_status $B0/${V0}0 $B0/${V0}1 source_deletions_heal
EXPECT "Y${zero_xattr}${zero_xattr}${zero_xattr}${zero_xattr}${zero_xattr}${zero_xattr}" heal_status $B0/${V0}0 $B0/${V0}1 source_deletions_me
EXPECT "Y${zero_xattr}${zero_xattr}${zero_xattr}${zero_xattr}${zero_xattr}${zero_xattr}" heal_status $B0/${V0}0 $B0/${V0}1 source_self_accusing
EXPECT "Y${zero_xattr}${zero_xattr}${zero_xattr}${zero_xattr}${zero_xattr}${zero_xattr}" heal_status $B0/${V0}0 $B0/${V0}1 source_creations_heal
EXPECT "Y${zero_xattr}${zero_xattr}${zero_xattr}${zero_xattr}${zero_xattr}${zero_xattr}" heal_status $B0/${V0}0 $B0/${V0}1 source_creations_me
EXPECT "Y${zero_xattr}${zero_xattr}${zero_xattr}${zero_xattr}${zero_xattr}${zero_xattr}" heal_status $B0/${V0}0 $B0/${V0}1 v1_dirty_heal
EXPECT "Y${zero_xattr}${zero_xattr}${zero_xattr}${zero_xattr}${zero_xattr}${zero_xattr}" heal_status $B0/${V0}0 $B0/${V0}1 v1_dirty_me

#Don't access the files/dirs from mount point as that may cause self-heals
# Check if conservative merges happened correctly on heal dirs
TEST stat $B0/${V0}0/spb_heal/1
TEST stat $B0/${V0}1/spb_heal/1

TEST stat $B0/${V0}0/spb_heal/0
TEST stat $B0/${V0}1/spb_heal/0

TEST stat $B0/${V0}0/fool_heal/1
TEST stat $B0/${V0}1/fool_heal/1

TEST stat $B0/${V0}0/fool_heal/0
TEST stat $B0/${V0}1/fool_heal/0

TEST stat $B0/${V0}0/v1_fool_heal/0
TEST stat $B0/${V0}1/v1_fool_heal/0

TEST stat $B0/${V0}0/v1_fool_heal/1
TEST stat $B0/${V0}1/v1_fool_heal/1

TEST stat $B0/${V0}0/v1_dirty_heal/0
TEST stat $B0/${V0}1/v1_dirty_heal/0

#Check if files that have gfid-mismatches in spb are giving EIO
TEST ! stat spb/0

#Check if stale files are deleted on access
TEST ! stat $B0/${V0}0/source_deletions_heal/fifo
TEST ! stat $B0/${V0}1/source_deletions_heal/fifo
TEST ! stat $B0/${V0}0/source_deletions_heal/block
TEST ! stat $B0/${V0}1/source_deletions_heal/block
TEST ! stat $B0/${V0}0/source_deletions_heal/char
TEST ! stat $B0/${V0}1/source_deletions_heal/char
TEST ! stat $B0/${V0}0/source_deletions_heal/file
TEST ! stat $B0/${V0}1/source_deletions_heal/file
TEST ! stat $B0/${V0}0/source_deletions_heal/file
TEST ! stat $B0/${V0}1/source_deletions_heal/file
TEST ! stat $B0/${V0}0/source_deletions_heal/dir1/dir2
TEST ! stat $B0/${V0}1/source_deletions_heal/dir1/dir2
TEST ! stat $B0/${V0}0/source_deletions_heal/dir1
TEST ! stat $B0/${V0}1/source_deletions_heal/dir1

#Check if stale files are deleted on access
TEST ! stat $B0/${V0}0/source_self_accusing/fifo
TEST ! stat $B0/${V0}1/source_self_accusing/fifo
TEST ! stat $B0/${V0}0/source_self_accusing/block
TEST ! stat $B0/${V0}1/source_self_accusing/block
TEST ! stat $B0/${V0}0/source_self_accusing/char
TEST ! stat $B0/${V0}1/source_self_accusing/char
TEST ! stat $B0/${V0}0/source_self_accusing/file
TEST ! stat $B0/${V0}1/source_self_accusing/file
TEST ! stat $B0/${V0}0/source_self_accusing/file
TEST ! stat $B0/${V0}1/source_self_accusing/file
TEST ! stat $B0/${V0}0/source_self_accusing/dir1/dir2
TEST ! stat $B0/${V0}1/source_self_accusing/dir1/dir2
TEST ! stat $B0/${V0}0/source_self_accusing/dir1
TEST ! stat $B0/${V0}1/source_self_accusing/dir1

#Test if the files created as part of full self-heal correctly
r=$(get_file_type $B0/${V0}0/source_creations_heal/fifo)
EXPECT "$r" get_file_type $B0/${V0}1/source_creations_heal/fifo
TEST [ -p $B0/${V0}0/source_creations_heal/fifo ]
EXPECT "^4 5$" stat -c "%t %T" $B0/${V0}1/source_creations_heal/block
EXPECT "^4 5$" stat -c "%t %T" $B0/${V0}0/source_creations_heal/block

r=$(get_file_type $B0/${V0}0/source_creations_heal/block)
EXPECT "$r" get_file_type $B0/${V0}1/source_creations_heal/block

r=$(get_file_type $B0/${V0}0/source_creations_heal/char)
EXPECT "$r" get_file_type $B0/${V0}1/source_creations_heal/char
EXPECT "^1 5$" stat -c "%t %T" $B0/${V0}1/source_creations_heal/char
EXPECT "^1 5$" stat -c "%t %T" $B0/${V0}0/source_creations_heal/char

r=$(get_file_type $B0/${V0}0/source_creations_heal/file)
EXPECT "$r" get_file_type $B0/${V0}1/source_creations_heal/file
TEST [ -f $B0/${V0}0/source_creations_heal/file ]

r=$(get_file_type source_creations_heal/file $B0/${V0}0/slink)
EXPECT "$r" get_file_type $B0/${V0}1/source_creations_heal/file slink
TEST [ -h $B0/${V0}0/source_creations_heal/slink ]

r=$(get_file_type $B0/${V0}0/source_creations_heal/dir1/dir2)
EXPECT "$r" get_file_type $B0/${V0}1/source_creations_heal/dir1/dir2
TEST [ -d $B0/${V0}0/source_creations_heal/dir1/dir2 ]

r=$(get_file_type $B0/${V0}0/source_creations_heal/dir1)
EXPECT "$r" get_file_type $B0/${V0}1/source_creations_heal/dir1
TEST [ -d $B0/${V0}0/source_creations_heal/dir1 ]

cd -

cleanup
