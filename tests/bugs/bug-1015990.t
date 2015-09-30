#!/bin/bash

. $(dirname $0)/../include.rc
. $(dirname $0)/../volume.rc
. $(dirname $0)/../afr.rc
cleanup;

## Start and create a volume
TEST glusterd;
TEST pidof glusterd;
TEST $CLI volume info;

TEST $CLI volume create $V0 replica 2 $H0:$B0/${V0}{1,2,3,4};

## Verify volume is is created
EXPECT "$V0" volinfo_field $V0 'Volume Name';
EXPECT 'Created' volinfo_field $V0 'Status';

## Start volume and verify
TEST $CLI volume start $V0;
EXPECT 'Started' volinfo_field $V0 'Status';


TEST glusterfs --volfile-id=/$V0 --volfile-server=$H0 $M0 --attribute-timeout=0 --entry-timeout=0



TEST kill_brick $V0 $H0 $B0/$V0"1"
sleep 5
TEST kill_brick $V0 $H0 $B0/$V0"3"
sleep 5

for  i in  {1..100}; do echo "STRING" > $M0/File$i; done

brick_2_sh_entries=$(count_sh_entries $B0/$V0"2")
brick_4_sh_entries=$(count_sh_entries $B0/$V0"4")


command_output=$(gluster volume heal $V0 statistics heal-count)


substring="Number of entries:"
count=0
while read -r line;
do
        if [[ "$line" == *$substring* ]]
                then
                        value=$(echo $line | cut -f 2 -d :)
                        count=$(($count + $value))
        fi

done <<< "$command_output"

brick_2_entries_count=$(($count-$value))
brick_4_entries_count=$value


xattrop_count_brick_2=$(count_sh_entries $B0/$V0"2")
##Remove the count of the xattrop-gfid entry count as it does not contribute
##to the number of files to be healed

sub_val=1
xattrop_count_brick_2=$(($xattrop_count_brick_2-$sub_val))

xattrop_count_brick_4=$(count_sh_entries $B0/$V0"4")
##Remove xattrop-gfid entry count

xattrop_count_brick_4=$(($xattrop_count_brick_4-$sub_val))


ret=0
if [ "$xattrop_count_brick_2" -eq "$brick_2_entries_count" ]
        then
                ret=$(($ret + $sub_val))
fi

EXPECT "1" echo $ret


ret=0
if [ "$xattrop_count_brick_4" -eq "$brick_4_entries_count" ]
        then
                ret=$(($ret + $sub_val))
fi

EXPECT "1" echo $ret

## Finish up
TEST $CLI volume stop $V0;
EXPECT 'Stopped' volinfo_field $V0 'Status';
TEST $CLI volume delete $V0;
TEST ! $CLI volume info $V0

cleanup;

