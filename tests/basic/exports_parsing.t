#!/bin/bash

. $(dirname $0)/../include.rc
. $(dirname $0)/../volume.rc

EXP_FILES=$(dirname $0)/../configfiles

cleanup

function test_good_file ()
{
        glusterfsd --print-exports $1
}

function test_long_netgroup()
{
        glusterfsd --print-exports $1 2>&1 | sed -n 1p
}

function test_bad_line ()
{
        glusterfsd --print-exports $1 2>&1 | sed -n 1p
}

function test_big_file ()
{
        glusterfsd --print-exports $1 | sed -n 3p
}

function test_bad_opt ()
{
        glusterfsd --print-exports $1 2>&1 | sed -n 1p
}

EXPECT_KEYWORD "/test @test(rw,anonuid=0,sec=sys,) 10.35.11.31(rw,anonuid=0,sec=sys,)" test_good_file $EXP_FILES/exports

EXPECT_KEYWORD "Error parsing netgroups for:" test_bad_line $EXP_FILES/bad_exports
EXPECT_KEYWORD "Error parsing netgroups for:" test_long_netgroup $EXP_FILES/bad_exports

EXPECT_KEYWORD "HDCDTY43SXOAH1TNUKB23MO9DE574W(rw,anonuid=0,sec=sys,)" test_big_file $EXP_FILES/big_exports

EXPECT_KEYWORD "Could not find any valid options" test_bad_opt $EXP_FILES/exports_bad_opt

cleanup
