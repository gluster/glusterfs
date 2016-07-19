#!/bin/bash

. $(dirname $0)/../include.rc
. $(dirname $0)/../volume.rc

NG_FILES=$(dirname $0)/../configfiles
cleanup;

function test_ng_1 ()
{
        glusterfsd --print-netgroups $1 | sed -n 1p
}

function test_ng_2 ()
{
        glusterfsd --print-netgroups $1 | sed -n 2p
}

function test_ng_3 ()
{
        glusterfsd --print-netgroups $1 | sed -n 3p
}

function test_ng_4 ()
{
        glusterfsd --print-netgroups $1 | sed -n 4p
}

function test_bad_ng ()
{
        glusterfsd --print-netgroups $1 2>&1 | sed -n 1p
}

function test_large_file ()
{
        # The build system needs this path for the test to pass.
        # This is an important test because this file is ~1800 lines
        # longs and is a "real-world" netgroups file.
        glusterfsd --print-netgroups ~/opsfiles/storage/netgroup/netgroup | sed -n 1p
}

function test_empty_ng ()
{
        glusterfsd --print-netgroups $1 2>&1 | sed -n 2p
}

EXPECT_KEYWORD "ng3 (dev-1763.prn-2.example.com,,)" test_ng_1 $NG_FILES/netgroups
EXPECT_KEYWORD "ng2 (dev1763.prn2.example.com,,)" test_ng_2 $NG_FILES/netgroups
EXPECT_KEYWORD "ng1 ng2 (dev1763.prn2.example.com,,)" test_ng_3 $NG_FILES/netgroups
EXPECT_KEYWORD "asdf ng1 ng2 (dev1763.prn2.example.com,,)" test_ng_4  $NG_FILES/netgroups
# TODO: get a real-world large netgroup file
#EXPECT_KEYWORD "wikipedia001.07.prn1 (wikipedia003.prn1.example.com,,)(wikipedia002.prn1.example.com,,)(wikipedia001.prn1.example.com,,)"  test_large_file
EXPECT_KEYWORD "Parse error" test_bad_ng $NG_FILES/bad_netgroups
EXPECT_KEYWORD "No netgroups were specified except for the parent" test_empty_ng $NG_FILES/bad_netgroups

cleanup;
