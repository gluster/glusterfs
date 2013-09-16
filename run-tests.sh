#!/bin/bash
# Copyright (c) 2013 Red Hat, Inc. <http://www.redhat.com>
#

function _init()
{
    regression_testsdir=$(dirname $0);

    if [ ! -f ${regression_testsdir}/tests/include.rc ]; then
        echo "Seems like GlusterFS quality tests are corrupted..aborting!!"
        exit 1
    fi
}

function main()
{
    if [ $# -lt 1 ]; then
        echo "Running all the regression test cases"
        prove -rf --timer ${regression_testsdir}/tests;
    else
        ## TODO
        echo "Running single regression test.."
        echo "WARNING: yet to be implemented.. exiting safely"
        exit 0
        #export DEBUG=1;
        #echo "Automatically setting up DEBUG=1 for this test $1";
    fi
}

_init "$@" && main "$@"
