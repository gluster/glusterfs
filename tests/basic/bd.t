#!/bin/bash

. $(dirname $0)/../include.rc
. $(dirname $0)/../volume.rc

function execute()
{
        cmd=$1
        shift
        ${cmd} $@ >/dev/null 2>&1
}

function bd_cleanup()
{
        execute vgremove -f ${V0}
        execute pvremove ${ld}
        execute losetup -d ${ld}
        execute rm ${BD_DISK}
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

SIZE=256 #in MB

bd_cleanup;

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
    execute vgcreate ${V0} ${ld}
    check vgcreate ${V0}
    execute lvcreate --thin ${V0}/pool --size 128M
}

function volinfo_field()
{
    local vol=$1;
    local field=$2;
    $CLI volume info $vol | grep "^$field: " | sed 's/.*: //';
}

function volume_type()
{
        getfattr -n volume.type $M0/. --only-values --absolute-names -e text
}

case $OSTYPE in
NetBSD)
        echo "Skip test on LVM which is not available on NetBSD" >&2
        SKIP_TESTS
        exit 0
        ;;
*)      
        ;;
esac 

TEST glusterd
TEST pidof glusterd
configure

TEST $CLI volume create $V0 ${H0}:/$B0/$V0?${V0}
EXPECT "$V0" volinfo_field $V0 'Volume Name';
EXPECT 'Created' volinfo_field $V0 'Status';

## Start volume and verify
TEST $CLI volume start $V0;
EXPECT 'Started' volinfo_field $V0 'Status'

TEST $GFS --volfile-id=/$V0 --volfile-server=$H0 $M0;
EXPECT '1' volume_type

## Create posix file
TEST touch $M0/posix

TEST touch $M0/lv
gfid=`getfattr -n glusterfs.gfid.string $M0/lv --only-values --absolute-names`
TEST setfattr -n user.glusterfs.bd -v "lv:4MB" $M0/lv
# Check if LV is created
TEST stat /dev/$V0/${gfid}

## Create filesystem
sleep 1
TEST mkfs.ext4 -qF $M0/lv
# Cloning
TEST touch $M0/lv_clone
gfid=`getfattr -n glusterfs.gfid.string $M0/lv_clone --only-values --absolute-names`
TEST setfattr -n clone -v ${gfid} $M0/lv
TEST stat /dev/$V0/${gfid}

sleep 1
## Check mounting
TEST mount -o loop $M0/lv $M1
EXPECT_WITHIN $UMOUNT_TIMEOUT "Y" force_umount $M1

# Snapshot
TEST touch $M0/lv_sn
gfid=`getfattr -n glusterfs.gfid.string $M0/lv_sn --only-values --absolute-names`
TEST setfattr -n snapshot -v ${gfid} $M0/lv
TEST stat /dev/$V0/${gfid}

# Merge
sleep 1
TEST setfattr -n merge -v "$M0/lv_sn" $M0/lv_sn
TEST ! stat $M0/lv_sn
TEST ! stat /dev/$V0/${gfid}


rm $M0/* -f

EXPECT_WITHIN $UMOUNT_TIMEOUT "Y" force_umount $M0
TEST $CLI volume stop ${V0}
EXPECT 'Stopped' volinfo_field $V0 'Status';
TEST $CLI volume delete ${V0}

bd_cleanup
