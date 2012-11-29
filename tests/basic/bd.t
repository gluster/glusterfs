#!/bin/bash

. $(dirname $0)/../include.rc

cleanup;


function execute()
{
        cmd=$1
        shift
        ${cmd} $@ >/dev/null 2>&1
}

function bd_cleanup()
{
        execute vgremove -f ${VG}
        execute pvremove ${ld}
        execute losetup -d ${ld}
        execute rm ${BD_DISK}
        execute $CLI volume delete ${V0}
        cleanup
}

function check()
{
        if [ $? -ne 0 ]; then
                echo prerequsite $@ failed
                bd_cleanup
                exit
        fi
}

VG=__bd_vg
SIZE=256 #in MB

## Configure environment needed for BD backend volumes
## Create a file with configured size and
## set it as a temporary loop device to create
## physical volume & VG. These are basic things needed
## for testing BD xlator if anyone of these steps fail,
## test script exits
function configure()
{
    GLDIR=`$CLI system:: getwd`
    BD_DISK=${GLDIR}/bd_disk

    execute truncate -s${SIZE}M ${BD_DISK}
    check ${BD_DISK} creation

    execute losetup -f
    check losetup
    ld=`losetup -f`

    execute losetup ${ld} ${BD_DISK}
    check losetup ${BD_DISK}
    execute pvcreate -f ${ld}
    check pvcreate ${ld}
    execute vgcreate ${VG} ${ld}
    check vgcreate ${VG}
}

function volinfo_field()
{
    local vol=$1;
    local field=$2;

    $CLI volume info $vol | grep "^$field: " | sed 's/.*: //';
}

TEST glusterd
TEST pidof glusterd
configure

TEST $CLI volume create $V0 device vg ${H0}:/${VG}
EXPECT "$V0" volinfo_field $V0 'Volume Name';
EXPECT 'Created' volinfo_field $V0 'Status';

## Start volume and verify
TEST $CLI volume start $V0;
EXPECT 'Started' volinfo_field $V0 'Status'

TEST glusterfs --volfile-server=$H0 --volfile-id=$V0 $M0

## Create file (LV)
TEST touch $M0/$VG/lv1
TEST stat /dev/$VG/lv1

TEST rm $M0/$VG/lv1;
TEST ! stat $M0/$VG/lv1;

TEST touch $M0/$VG/lv1
TEST truncate -s64M $M0/$VG/lv1

TEST ln $M0/$VG/lv1 $M0/$VG/lv2
TEST stat /dev/$VG/lv2

rm $M0/$VG/lv1
rm $M0/$VG/lv2

TEST $CLI bd create $V0:/$VG/lv1 4MB
TEST stat /dev/$VG/lv1

TEST $CLI bd clone $V0:/$VG/lv1 lv2
TEST stat /dev/$VG/lv2
TEST $CLI bd delete  $V0:/$VG/lv2

TEST $CLI bd snapshot $V0:/$VG/lv1 lv2 1
TEST stat /dev/$VG/lv2
rm $M0/$VG/lv2
rm $M0/$VG/lv1

TEST umount $M0
TEST $CLI volume stop ${V0}
TEST $CLI volume delete ${V0}

bd_cleanup
