#!/bin/bash

. $(dirname $0)/../include.rc
. $(dirname $0)/../volume.rc
. $(dirname $0)/../env.rc

SCRIPT_TIMEOUT=300

##Cleanup and start glusterd
cleanup;
TEST glusterd;
TEST pidof glusterd

##create .keys
mkdir -p /var/lib/glusterd/glusterfind/.keys

#create_and_start test_volume
TEST $CLI volume create test-vol $H0:$B0/b1 $H0:$B0/b2 $H0:$B0/b3
TEST gluster volume start test-vol

##Mount test-vol
TEST glusterfs -s $H0 --volfile-id test-vol $M0

TEST timestamp1=$(date +'%s')

##Create files and dirs inside the mount point
TEST mkdir -p $M0/dir1
TEST touch $M0/file1

##Glusterfind Create
TEST glusterfind create sess_vol1 test-vol --force

##################################################################################
#Incremental crawl
##################################################################################
##Glusterfind Pre
TEST glusterfind pre sess_vol1 test-vol output_file.txt

#Glusterfind Post
TEST glusterfind post sess_vol1 test-vol

##Glusterfind List
EXPECT '1' echo $(glusterfind list | grep sess_vol1 | wc -l)

TEST timestamp2=$(date +'%s')

##Glusterfind Query
TEST glusterfind query test-vol --since-time $timestamp1 --end-time $timestamp2 output_file.txt

#################################################################################
#Full Crawl
#################################################################################
##Glusterfind Pre
TEST glusterfind pre sess_vol1 test-vol output_file.txt --full --regenerate-outfile
EXPECT '1' echo $(grep 'NEW dir1' output_file.txt | wc -l)
EXPECT '1' echo $(grep 'NEW file1' output_file.txt | wc -l)

##Glusterfind Query commands
TEST glusterfind query test-vol --full output_file.txt
EXPECT '1' echo $(grep 'NEW dir1' output_file.txt | wc -l)
EXPECT '1' echo $(grep 'NEW file1' output_file.txt | wc -l)

##using tag, full crawl
TEST glusterfind query test-vol --full --tag-for-full-find NEW output_file.txt
EXPECT '1' echo $(grep 'NEW dir1' output_file.txt | wc -l)
EXPECT '1' echo $(grep 'NEW file1' output_file.txt | wc -l)

##using -field-separator option, full crawl
glusterfind query test-vol --full output_file.txt --field-separator "=="
EXPECT '1' echo $(grep 'NEW==dir1' output_file.txt | wc -l)
EXPECT '1' echo $(grep 'NEW==file1' output_file.txt | wc -l)

##Adding or Replacing a Brick from an Existing Glusterfind Session
TEST gluster volume add-brick test-vol $H0:$B0/b4 force

##To make existing session work after brick add
TEST glusterfind create sess_vol test-vol --force
EXPECT '1' echo $(glusterfind list | grep sess_vol1 | wc -l)

##glusterfind delete
TEST glusterfind delete sess_vol test-vol

rm -rf output_file.txt
cleanup;
