#!/bin/bash

. $(dirname $0)/../include.rc
. $(dirname $0)/../volume.rc

function write_file()
{
	path="$1"; shift
	echo "$*" > "$path"
}

cleanup;
TEST glusterd
TEST pidof glusterd
TEST $CLI volume info;

## Start and create a volume
mkdir -p ${B0}/${V0}-0
mkdir -p ${B0}/${V0}-1
mkdir -p ${B0}/${V0}-2
TEST $CLI volume create $V0 replica 3 $H0:$B0/${V0}-{0,1,2}

## Verify volume is created
EXPECT "$V0" volinfo_field $V0 'Volume Name';
EXPECT 'Created' volinfo_field $V0 'Status';

## Make sure io-cache and write-behind don't interfere.
TEST $CLI volume set $V0 data-self-heal off;

## Make sure automatic self-heal doesn't perturb our results.
TEST $CLI volume set $V0 cluster.self-heal-daemon off

TEST $CLI volume set $V0 background-self-heal-count 0

## Start volume and verify
TEST $CLI volume start $V0;
EXPECT 'Started' volinfo_field $V0 'Status';

## Mount native
TEST glusterfs --volfile-server=$H0 --volfile-id=$V0 $M0

TEST `echo "TEST-FILE" > $M0/File`
TEST `mkdir $M0/Dir`
TEST `ln  $M0/File $M0/Link`
TEST `mknod $M0/FIFO p`

TEST $CLI volume add-brick $V0 replica 4 $H0:$B0/$V0-3 force
TEST $CLI volume add-brick $V0 replica 5 $H0:$B0/$V0-4 force
TEST $CLI volume add-brick $V0 replica 6 $H0:$B0/$V0-5 force

sleep 10

TEST ls $M0/


function compare()
{
	var=-1;
	if [ $1 == $2 ]; then
		var=0;
	else
		var=-1;
	fi

	echo $var
}

var2="000000000000000000000000"

var1=`getfattr -d -m . $B0/$V0-0/File -e hex 2>&1 | grep "client-3"`
EXPECT "0" echo $?
var3=`echo $var1 | cut -d x -f 2`
EXPECT_NOT $var2 echo $var3

var1=`getfattr -d -m . $B0/$V0-0/File -e hex 2>&1 | grep "client-4"`
EXPECT "0" echo $?
var3=`echo $var1 | cut -d x -f 2`
EXPECT_NOT $var2 echo $var3

var1=`getfattr -d -m . $B0/$V0-0/File -e hex 2>&1 | grep "client-5"`
EXPECT "0" echo $?
var3=`echo $var1 | cut -d x -f 2`
EXPECT_NOT $var2 echo $var3

var1=`getfattr -d -m . $B0/$V0-1/File -e hex 2>&1 | grep "client-3"`
EXPECT "0" echo $?
var3=`echo $var1| cut -d x -f 2`
EXPECT_NOT $var2 echo $var3

var1=`getfattr -d -m . $B0/$V0-1/File -e hex 2>&1 | grep "client-4"`
EXPECT "0" echo $?
var3=`echo $var1 | cut -d x -f 2`
EXPECT_NOT $var2 echo $var3

var1=`getfattr -d -m . $B0/$V0-1/File -e hex 2>&1 | grep "client-5"`
EXPECT "0" echo $?
var3=`echo $var1 | cut -d x -f 2`
EXPECT_NOT $var2 echo $var3

var1=`getfattr -d -m . $B0/$V0-2/File -e hex 2>&1 | grep "client-3"`
EXPECT "0" echo $?
var3=`echo $var1 | cut -d x -f 2`
EXPECT_NOT $var2 echo $var3

var1=`getfattr -d -m . $B0/$V0-2/File -e hex 2>&1 | grep "client-4"`
EXPECT "0" echo $?
var3=`echo $var1 | cut -d x -f 2`
EXPECT_NOT $var2 echo $var3

var1=`getfattr -d -m . $B0/$V0-2/File -e hex 2>&1 | grep "client-5"`
EXPECT "0" echo $?
var3=`echo $var1 | cut -d x -f 2`
EXPECT_NOT $var2 echo $var3

var1=`getfattr -d -m . $B0/$V0-0/Dir -e hex 2>&1 | grep "client-3"`
EXPECT "0" echo $?
var3=`echo $var1 | cut -d x -f 2`
EXPECT_NOT $var2 echo $var3

var1=`getfattr -d -m . $B0/$V0-0/Dir -e hex 2>&1 | grep "client-4"`
EXPECT "0" echo $?
var3=`echo $var1 | cut -d x -f 2`
EXPECT_NOT $var2 echo $var3

var1=`getfattr -d -m . $B0/$V0-0/Dir -e hex 2>&1 | grep "client-5"`
EXPECT "0" echo $?
var3=`echo $var1 | cut -d x -f 2`
EXPECT_NOT $var2 echo $var3

var1=`getfattr -d -m . $B0/$V0-1/Dir -e hex 2>&1 | grep "client-3"`
EXPECT "0" echo $?
var3=`echo $var1 | cut -d x -f 2`
EXPECT_NOT $var2 echo $var3

var1=`getfattr -d -m . $B0/$V0-1/Dir -e hex 2>&1 | grep "client-4"`
EXPECT "0" echo $?
var3=`echo $var1 | cut -d x -f 2`
EXPECT_NOT $var2 echo $var3

var1=`getfattr -d -m . $B0/$V0-1/Dir -e hex 2>&1 | grep "client-5"`
EXPECT "0" echo $?
var3=`echo $var1 | cut -d x -f 2`
EXPECT_NOT $var2 echo $var3

var1=`getfattr -d -m . $B0/$V0-2/Dir -e hex 2>&1 | grep "client-3"`
EXPECT "0" echo $?
var3=`echo $var1 | cut -d x -f 2`
EXPECT_NOT $var2 echo $var3

var1=`getfattr -d -m . $B0/$V0-2/Dir -e hex 2>&1 | grep "client-4"`
EXPECT "0" echo $?
var3=`echo $var1 | cut -d x -f 2`
EXPECT_NOT $var2 echo $var3

var1=`getfattr -d -m . $B0/$V0-2/Dir -e hex 2>&1 | grep "client-5"`
EXPECT "0" echo $?
var3=`echo $var1 | cut -d x -f 2`
EXPECT_NOT $var2 echo $var3


var1=`getfattr -d -m . $B0/$V0-0/Link -e hex 2>&1 | grep "client-3"`
EXPECT "0" echo $?
var3=`echo $var1 | cut -d x -f 2`
EXPECT_NOT $var2 echo $var3

var1=`getfattr -d -m . $B0/$V0-0/Link -e hex 2>&1 | grep "client-4"`
EXPECT "0" echo $?
var3=`echo $var1 | cut -d x -f 2`
EXPECT_NOT $var2 echo $var3

var1=`getfattr -d -m . $B0/$V0-0/Link -e hex 2>&1 | grep "client-5"`
EXPECT "0" echo $?
var3=`echo $var1 | cut -d x -f 2`
EXPECT_NOT $var2 echo $var3

var1=`getfattr -d -m . $B0/$V0-1/Link -e hex 2>&1 | grep "client-3"`
EXPECT "0" echo $?
var3=`echo $var1 | cut -d x -f 2`
EXPECT_NOT $var2 echo $var3

var1=`getfattr -d -m . $B0/$V0-1/Link -e hex 2>&1 | grep "client-4"`
EXPECT "0" echo $?
var3=`echo $var1 | cut -d x -f 2`
EXPECT_NOT $var2 echo $var3

var1=`getfattr -d -m . $B0/$V0-1/Link -e hex 2>&1 | grep "client-5"`
EXPECT "0" echo $?
var3=`echo $var1 | cut -d x -f 2`
EXPECT_NOT $var2 echo $var3

var1=`getfattr -d -m . $B0/$V0-2/Link -e hex 2>&1 | grep "client-3"`
EXPECT "0" echo $?
var3=`echo $var1 | cut -d x -f 2`
EXPECT_NOT $var2 echo $var3

var1=`getfattr -d -m . $B0/$V0-2/Link -e hex 2>&1 | grep "client-4"`
EXPECT "0" echo $?
var3=`echo $var1 | cut -d x -f 2`
EXPECT_NOT $var2 echo $var3

var1=`getfattr -d -m . $B0/$V0-2/Link -e hex 2>&1 | grep "client-5"`
EXPECT "0" echo $?
var3=`echo $var1 | cut -d x -f 2`
EXPECT_NOT $var2 echo $var3



var1=`getfattr -d -m . $B0/$V0-0/FIFO -e hex 2>&1 | grep "client-3"`
EXPECT "0" echo $?
var3=`echo $var1 | cut -d x -f 2`
EXPECT_NOT $var2 echo $var3

var1=`getfattr -d -m . $B0/$V0-0/FIFO -e hex 2>&1 | grep "client-4"`
EXPECT "0" echo $?
var3=`echo $var1 | cut -d x -f 2`
EXPECT_NOT $var2 echo $var3

var1=`getfattr -d -m . $B0/$V0-0/FIFO -e hex 2>&1 | grep "client-5"`
EXPECT "0" echo $?
var3=`echo $var1 | cut -d x -f 2`
EXPECT_NOT $var2 echo $var3

var1=`getfattr -d -m . $B0/$V0-1/FIFO -e hex 2>&1 | grep "client-3"`
EXPECT "0" echo $?
var3=`echo $var1 | cut -d x -f 2`
EXPECT_NOT $var2 echo $var3

var1=`getfattr -d -m . $B0/$V0-1/FIFO -e hex 2>&1 | grep "client-4"`
EXPECT "0" echo $?
var3=`echo $var1 | cut -d x -f 2`
EXPECT_NOT $var2 echo $var3

var1=`getfattr -d -m . $B0/$V0-1/FIFO -e hex 2>&1 | grep "client-5"`
EXPECT "0" echo $?
var3=`echo $var1 | cut -d x -f 2`
EXPECT_NOT $var2 echo $var3

var1=`getfattr -d -m . $B0/$V0-2/FIFO -e hex 2>&1 | grep "client-3"`
EXPECT "0" echo $?
var3=`echo $var1 | cut -d x -f 2`
EXPECT_NOT $var2 echo $var3

var1=`getfattr -d -m . $B0/$V0-2/FIFO -e hex 2>&1 | grep "client-4"`
EXPECT "0" echo $?
var3=`echo $var1 | cut -d x -f 2`
EXPECT_NOT $var2 echo $var3

var1=`getfattr -d -m . $B0/$V0-2/FIFO -e hex 2>&1 | grep "client-5"`
EXPECT "0" echo $?
var3=`echo $var1 | cut -d x -f 2`
EXPECT_NOT $var2 echo $var3

cleanup;
