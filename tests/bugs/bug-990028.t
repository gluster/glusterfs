#!/bin/bash

. $(dirname $0)/../include.rc
. $(dirname $0)/../fileio.rc

cleanup;

TESTS_EXPECTED_IN_LOOP=153

function __init()
{
    TEST glusterd
    TEST pidof glusterd
    TEST $CLI volume info;

    TEST $CLI volume create $V0 $H0:$B0/brick

    EXPECT 'Created' volinfo_field $V0 'Status';

    TEST $CLI volume start $V0

    TEST glusterfs --volfile-id=$V0 --volfile-server=$H0 $M0

    TEST $CLI volume quota $V0 enable
}

#CASE-1
#checking pgfid under same directory
function links_in_same_directory()
{
    # create a file file1
    TEST touch $M0/file1

    # create 50 hardlinks for file1
    for i in `seq 2 50`; do
        TEST_IN_LOOP ln $M0/file1 $M0/file$i
    done

    # store the pgfid of file1 in PGFID_FILE1 [should be 50 now (0x000000032)]
    PGFID_FILE1=`getfattr -m "trusted.pgfid.*" -de hex  $B0/brick/file1 2>&1 | grep "trusted.pgfid" | gawk -F '=' '{print $2}'`

    # compare the pgfid(link value ) of each  hard links are equal or not
    for i in `seq  2 50`; do
        TEMP=`getfattr -m "trusted.pgfid.*" -de hex $B0/brick/file$i 2>&1 | grep "trusted.pgfid" | gawk -F '=' '{print $2}'`
        TEST_IN_LOOP [ $PGFID_FILE1 = $TEMP ]
    done

    # check if no of links value is 50 or not
    TEST [ $PGFID_FILE1 = "0x00000032" ]

    # unlink file 2 to 50
    for i in `seq 2 50`; do
        TEST_IN_LOOP unlink $M0/file$i;
    done

    # now check if pgfid value is 1 or not
    PGFID_FILE1=`getfattr -m "trusted.pgfid.*" -de hex  $B0/brick/file1 2>&1 | grep "trusted.pgfid" | gawk -F '=' '{print $2}'`;

    TEST [ $PGFID_FILE1 = "0x00000001" ]

    TEST rm -f $M0/*
}

##checking pgfid under diff directories
function links_across_directories()
{
    TEST mkdir $M0/dir1 $M0/dir2;

    # create a file in dir1
    TEST touch $M0/dir1/file1;

    # create  hard link for file1 in dir2
    TEST ln $M0/dir1/file1 $M0/dir2/file2;

    #first check is to find whether there are two pgfids or not
    LINES=`getfattr -m "trusted.pgfid.*" -de hex $B0/brick/dir1/file1 2>&1 | grep "trusted.pgfid" | wc -l`
    TEST [ $LINES = 2 ]

    for i in $(seq 1  2); do
        HL=`getfattr -m "trusted.pgfid.*" -de hex $B0/brick/dir$i/file$i 2>&1 | grep "trusted.pgfid" | cut -d$'\n' -f$i | cut -d'=' -f2`        
        TEST_IN_LOOP [ $HL = "0x00000001" ]
    done

    #now unlink file2 and check the pgfid of file1
    #1. no. of pgfid should be one
    #2. no. of hard link should be one
    TEST unlink $M0/dir2/file2

    LINES=`getfattr -m "trusted.pgfid.*" -de hex $B0/brick/dir1/file1 2>&1 | grep "trusted.pgfid" | wc -l`
    TEST [ $LINES == 1 ]

    #next to check is to whether they contain hard link value of one or not
    HL=`getfattr -m "trusted.pgfid.*" -de hex $B0/brick/dir1/file1 2>&1 | grep "trusted.pgfid" | cut -d'=' -f2`
    TEST [ $HL = "0x00000001" ]

    #rename file under same directory

    TEST touch $M0/r_file1
    PGFID_rfile1=`getfattr -m "trusted.pgfid.*" -de hex $B0/brick/r_file1 2>&1 | grep "trusted.pgfid"`

    #cross check whether hard link count is one
    HL=`getfattr -m "trusted.pgfid.*" -de hex $B0/brick/r_file1 2>&1 | grep "trusted.pgfid" | cut -d'=' -f2`

    TEST [ $HL = "0x00000001" ]

    #now rename the file to r_file1
    TEST mv $M0/r_file1 $M0/r_file2

    #now check the pgfid hard link count is still one or not
    HL=`getfattr -m "trusted.pgfid.*" -de hex $B0/brick/r_file2 2>&1 | grep "trusted.pgfid" | cut -d'=' -f2` 

    TEST [ $HL = "0x00000001" ]

    #now move the file to a different directory where it has no hard link and check
    TEST mkdir $M0/dir3;
    TEST mv $M0/r_file2 $M0/dir3;

    #now check the pgfid has changed or not and hard limit is one or not
    PGFID_newDir=`getfattr -m "trusted.pgfid.*" -de hex $B0/brick/dir3/r_file2 2>&1 | grep "trusted.pgfid"`

    #now the older pgfid and new pgfid shouldn't match
    TEST [ $PGFID_rfile1 != $PGFID_newDir ]

    HL=`getfattr -m "trusted.pgfid" -de hex $B0/brick/dir3/r_file2 2>&1 | grep "trusted.pgfid" | cut -d'=' -f2`
    TEST [ $HL = "0x00000001" ]

    TEST touch $M0/dir1/rl_file_1
    ln $M0/dir1/rl_file_1 $M0/dir2/rl_file_2
    mv $M0/dir1/rl_file_1 $M0/dir2

    #now the there should be just one pgfid for both files
    for i in $(seq 1 2); do
            NL=`getfattr -m "trusted.pgfid" -de hex $B0/brick/dir2/rl_file_$i 2>&1 | grep "trusted.pgfid"|wc -l `
            TEST_IN_LOOP [ $HL = "0x00000001" ]
    done

    #now pgfid of both files should match
    P_rl_file_1=`getfattr -m "trusted.pgfid" -de hex $B0/brick/dir2/rl_file_1 2>&1 | grep "trusted.pgfid"`
    P_rl_file_2=`getfattr -m "trusted.pgfid" -de hex $B0/brick/dir2/rl_file_2 2>&1 | grep "trusted.pgfid"`
    TEST [ $P_rl_file_1 = $P_rl_file_2 ]

    #now the no of hard link should be two for both rl_file_1 and rl_file_2
    for i in  $(seq 1 2); do
        HL=`getfattr -m "trusted.pgfid" -de hex $B0/brick/dir2/rl_file_$i 2>&1 | grep "trusted.pgfid" | cut -d'=' -f2`
        TEST_IN_LOOP [ $HL = "0x00000002" ]
    done

    TEST rm -rf $M0/*
}

__init;
links_in_same_directory;
links_across_directories;

cleanup
