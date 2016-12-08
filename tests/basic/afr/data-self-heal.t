#!/bin/bash
#Self-heal tests

. $(dirname $0)/../../include.rc
. $(dirname $0)/../../volume.rc
. $(dirname $0)/../../afr.rc
cleanup;

function create_xattrop_entry {
        local xattrop_dir0=$(afr_get_index_path $B0/brick0)
        local xattrop_dir1=$(afr_get_index_path $B0/brick1)
        local base_entry_b0=`ls $xattrop_dir0`
        local base_entry_b1=`ls $xattrop_dir1`
        local gfid_str

        for file in "$@"
        do
                gfid_str=$(gf_gfid_xattr_to_str $(gf_get_gfid_xattr $B0/brick0/$file))
                ln $xattrop_dir0/$base_entry_b0 $xattrop_dir0/$gfid_str
                ln $xattrop_dir1/$base_entry_b1 $xattrop_dir1/$gfid_str
        done
}

function is_heal_done {
        local f1_path="${1}/${3}"
        local f2_path="${2}/${3}"
        local zero_xattr="000000000000000000000000"
        local size1=$(stat -c "%s" $f1_path)
        local size2=$(stat -c "%s" $f2_path)
        local diff=$((size1-size2))
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
        if [ "${diff}${xattr11}${xattr12}${xattr21}${xattr22}${dirty1}${dirty2}" == "0${zero_xattr}${zero_xattr}${zero_xattr}${zero_xattr}${zero_xattr}${zero_xattr}" ];
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
                if [ "N" == $(is_heal_done $B0/brick0 $B0/brick1 $i) ];
                then
                        result="$result:$i"
                fi
        done
#To prevent any match for EXPECT_WITHIN, print a char non-existent in file-names
        if [ $result == ":" ]; then result="~"; fi
        echo $result
}

TEST glusterd
TEST pidof glusterd
TEST $CLI volume create $V0 replica 2 $H0:$B0/brick{0,1}
TEST $CLI volume set $V0 cluster.background-self-heal-count 0
TEST $CLI volume set $V0 performance.write-behind off
TEST $CLI volume set $V0 performance.read-ahead off
TEST $CLI volume set $V0 performance.io-cache off
TEST $CLI volume set $V0 performance.stat-prefetch off
TEST $CLI volume set $V0 performance.quick-read off
TEST $CLI volume set $V0 performance.open-behind off

TEST $CLI volume start $V0
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "Y" glustershd_up_status
EXPECT_WITHIN $CHILD_UP_TIMEOUT "1" afr_child_up_status_in_shd $V0 0
EXPECT_WITHIN $CHILD_UP_TIMEOUT "1" afr_child_up_status_in_shd $V0 1
TEST $GFS --volfile-id=$V0 --volfile-server=$H0 $M0;
cd $M0
TEST touch pending-changelog biggest-file-source.txt biggest-file-more-prio-than-changelog.txt same-size-more-prio-to-changelog.txt size-and-witness-same.txt self-accusing-vs-source.txt self-accusing-both.txt self-accusing-vs-innocent.txt self-accusing-bigger-exists.txt size-more-prio-than-self-accused.txt v1-dirty.txt split-brain.txt split-brain-all-dirty.txt split-brain-with-dirty.txt

TEST setfattr -n trusted.afr.$V0-client-1 -v 0x000000010000000000000000 $B0/brick0/pending-changelog
TEST "echo abc > $B0/brick1/pending-changelog"

TEST "echo abc > $B0/brick0/biggest-file-source.txt"
TEST "echo abcd > $B0/brick1/biggest-file-source.txt"

TEST "echo abc > $B0/brick0/biggest-file-more-prio-than-changelog.txt"
TEST "echo abcd > $B0/brick1/biggest-file-more-prio-than-changelog.txt"
TEST setfattr -n trusted.afr.dirty -v 0x000000200000000000000000 $B0/brick0/biggest-file-more-prio-than-changelog.txt

TEST "echo abc > $B0/brick0/same-size-more-prio-to-changelog.txt"
TEST "echo def > $B0/brick1/same-size-more-prio-to-changelog.txt"
TEST setfattr -n trusted.afr.dirty -v 0x000000200000000000000000 $B0/brick0/same-size-more-prio-to-changelog.txt

TEST "echo abc > $B0/brick0/size-and-witness-same.txt"
TEST "echo def > $B0/brick1/size-and-witness-same.txt"
TEST setfattr -n trusted.afr.dirty -v 0x000000200000000000000000 $B0/brick0/size-and-witness-same.txt
TEST setfattr -n trusted.afr.dirty -v 0x000000200000000000000000 $B0/brick1/size-and-witness-same.txt

TEST "echo abc > $B0/brick0/split-brain.txt"
TEST "echo def > $B0/brick1/split-brain.txt"
TEST setfattr -n trusted.afr.$V0-client-1 -v 0x000000200000000000000000 $B0/brick0/split-brain.txt
TEST setfattr -n trusted.afr.$V0-client-0 -v 0x000000200000000000000000 $B0/brick1/split-brain.txt

TEST "echo abc > $B0/brick0/split-brain-all-dirty.txt"
TEST "echo def > $B0/brick1/split-brain-all-dirty.txt"
TEST setfattr -n trusted.afr.$V0-client-1 -v 0x000000200000000000000000 $B0/brick0/split-brain-all-dirty.txt
TEST setfattr -n trusted.afr.$V0-client-0 -v 0x000000200000000000000000 $B0/brick1/split-brain-all-dirty.txt
TEST setfattr -n trusted.afr.dirty -v 0x000000200000000000000000 $B0/brick0/split-brain-all-dirty.txt
TEST setfattr -n trusted.afr.dirty -v 0x000000200000000000000000 $B0/brick1/split-brain-all-dirty.txt

TEST "echo abc > $B0/brick0/split-brain-with-dirty.txt"
TEST "echo def > $B0/brick1/split-brain-with-dirty.txt"
TEST setfattr -n trusted.afr.$V0-client-1 -v 0x000000200000000000000000 $B0/brick0/split-brain-with-dirty.txt
TEST setfattr -n trusted.afr.$V0-client-0 -v 0x000000200000000000000000 $B0/brick1/split-brain-with-dirty.txt
TEST setfattr -n trusted.afr.dirty -v 0x000000200000000000000000 $B0/brick1/split-brain-with-dirty.txt

TEST "echo def > $B0/brick1/self-accusing-vs-source.txt"
TEST "echo abc > $B0/brick0/self-accusing-vs-source.txt"
TEST setfattr -n trusted.afr.$V0-client-0 -v 0x000000200000000000000000 $B0/brick1/self-accusing-vs-source.txt
TEST setfattr -n trusted.afr.$V0-client-1 -v 0x000000200000000000000000 $B0/brick1/self-accusing-vs-source.txt
TEST setfattr -n trusted.afr.$V0-client-1 -v 0x000000200000000000000000 $B0/brick0/self-accusing-vs-source.txt

TEST "echo abc > $B0/brick0/self-accusing-both.txt"
TEST "echo def > $B0/brick1/self-accusing-both.txt"
TEST setfattr -n trusted.afr.$V0-client-0 -v 0x000000200000000000000000 $B0/brick0/self-accusing-both.txt
TEST setfattr -n trusted.afr.$V0-client-1 -v 0x000000200000000000000000 $B0/brick0/self-accusing-both.txt
TEST setfattr -n trusted.afr.$V0-client-0 -v 0x000000200000000000000000 $B0/brick1/self-accusing-both.txt
TEST setfattr -n trusted.afr.$V0-client-1 -v 0x000000200000000000000000 $B0/brick1/self-accusing-both.txt

TEST "echo abc > $B0/brick0/self-accusing-vs-innocent.txt"
TEST "echo def > $B0/brick1/self-accusing-vs-innocent.txt"
TEST setfattr -n trusted.afr.$V0-client-1 -v 0x000000200000000000000000 $B0/brick1/self-accusing-vs-innocent.txt

TEST "echo abc > $B0/brick0/self-accusing-bigger-exists.txt"
TEST "echo def > $B0/brick1/self-accusing-bigger-exists.txt"
TEST setfattr -n trusted.afr.$V0-client-0 -v 0x000000200000000000000000 $B0/brick0/self-accusing-bigger-exists.txt
TEST setfattr -n trusted.afr.$V0-client-1 -v 0x000000300000000000000000 $B0/brick0/self-accusing-bigger-exists.txt
TEST setfattr -n trusted.afr.$V0-client-0 -v 0x000000200000000000000000 $B0/brick1/self-accusing-bigger-exists.txt
TEST setfattr -n trusted.afr.$V0-client-1 -v 0x000000200000000000000000 $B0/brick1/self-accusing-bigger-exists.txt

TEST "echo abc > $B0/brick0/size-more-prio-than-self-accused.txt"
TEST "echo defg > $B0/brick1/size-more-prio-than-self-accused.txt"
TEST setfattr -n trusted.afr.$V0-client-0 -v 0x000000200000000000000000 $B0/brick0/size-more-prio-than-self-accused.txt
TEST setfattr -n trusted.afr.$V0-client-1 -v 0x000000300000000000000000 $B0/brick0/size-more-prio-than-self-accused.txt
TEST setfattr -n trusted.afr.$V0-client-0 -v 0x000000200000000000000000 $B0/brick1/size-more-prio-than-self-accused.txt
TEST setfattr -n trusted.afr.$V0-client-1 -v 0x000000200000000000000000 $B0/brick1/size-more-prio-than-self-accused.txt

TEST "echo abc > $B0/brick0/v1-dirty.txt"
TEST "echo def > $B0/brick1/v1-dirty.txt"
TEST setfattr -n trusted.afr.$V0-client-0 -v 0x000000200000000000000000 $B0/brick0/v1-dirty.txt
TEST setfattr -n trusted.afr.$V0-client-1 -v 0x000000100000000000000000 $B0/brick1/v1-dirty.txt

#Create base entry in indices/xattrop
echo "Data" > $M0/FILE
rm -f $M0/FILE
EXPECT "1" count_index_entries $B0/brick0
EXPECT "1" count_index_entries $B0/brick1
cd -

#Create gfid hard links for all files before triggering index heals.
create_xattrop_entry pending-changelog biggest-file-source.txt biggest-file-more-prio-than-changelog.txt same-size-more-prio-to-changelog.txt size-and-witness-same.txt self-accusing-vs-source.txt self-accusing-both.txt self-accusing-vs-innocent.txt self-accusing-bigger-exists.txt size-more-prio-than-self-accused.txt v1-dirty.txt

TEST $CLI volume heal $V0
EXPECT_WITHIN $HEAL_TIMEOUT "~" print_pending_heals pending-changelog biggest-file-source.txt biggest-file-more-prio-than-changelog.txt same-size-more-prio-to-changelog.txt size-and-witness-same.txt self-accusing-vs-source.txt self-accusing-both.txt self-accusing-vs-innocent.txt self-accusing-bigger-exists.txt size-more-prio-than-self-accused.txt v1-dirty.txt
EXPECT "N" is_heal_done $B0/brick0 $B0/brick1 split-brain.txt
EXPECT "N" is_heal_done $B0/brick0 $B0/brick1 split-brain-all-dirty.txt
EXPECT "N" is_heal_done $B0/brick0 $B0/brick1 split-brain-with-dirty.txt

EXPECT "0" stat -c "%s" $M0/pending-changelog
TEST cmp $B0/brick0/pending-changelog $B0/brick1/pending-changelog

EXPECT "abcd" cat $M0/biggest-file-source.txt
TEST cmp $B0/brick0/biggest-file-source.txt $B0/brick1/biggest-file-source.txt

EXPECT "abcd" cat $M0/biggest-file-more-prio-than-changelog.txt
TEST cmp $B0/brick0/biggest-file-more-prio-than-changelog.txt $B0/brick1/biggest-file-more-prio-than-changelog.txt

EXPECT "abc" cat $M0/same-size-more-prio-to-changelog.txt
TEST cmp $B0/brick0/same-size-more-prio-to-changelog.txt $B0/brick1/same-size-more-prio-to-changelog.txt

EXPECT "(abc|def)" cat $M0/size-and-witness-same.txt
TEST cmp $B0/brick0/size-and-witness-same.txt $B0/brick1/size-and-witness-same.txt

TEST ! cat $M0/split-brain.txt
TEST ! cat $M0/split-brain-all-dirty.txt
TEST ! cat $M0/split-brain-with-dirty.txt

EXPECT "abc" cat $M0/self-accusing-vs-source.txt
TEST cmp $B0/brick0/self-accusing-vs-source.txt $B0/brick1/self-accusing-vs-source.txt

EXPECT "(abc|def)" cat $M0/self-accusing-both.txt
TEST cmp $B0/brick0/self-accusing-both.txt $B0/brick1/self-accusing-both.txt

EXPECT "def" cat $M0/self-accusing-vs-innocent.txt
TEST cmp $B0/brick0/self-accusing-vs-innocent.txt $B0/brick1/self-accusing-vs-innocent.txt

EXPECT "abc" cat $M0/self-accusing-bigger-exists.txt
TEST cmp $B0/brick0/self-accusing-bigger-exists.txt $B0/brick1/self-accusing-bigger-exists.txt

EXPECT "defg" cat $M0/size-more-prio-than-self-accused.txt
TEST cmp $B0/brick0/size-more-prio-than-self-accused.txt $B0/brick1/size-more-prio-than-self-accused.txt

EXPECT "abc" cat $M0/v1-dirty.txt
TEST cmp $B0/brick0/v1-dirty.txt $B0/brick1/v1-dirty.txt
cleanup;
