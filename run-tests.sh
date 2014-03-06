#!/bin/bash
# Copyright (c) 2013-2014 Red Hat, Inc. <http://www.redhat.com>
#

function check_dependencies()
{
    ## Check all dependencies are present
    MISSING=""

    # Check for dbench
    if [ ! -x /usr/bin/dbench ]; then
        MISSING="$MISSING dbench"
    fi

    # Check for git
    env git --version > /dev/null 2>&1
    if [ $? -ne 0 ]; then
        MISSING="$MISSING git"
    fi

    # Check for mock
    if [ ! -e /usr/bin/mock ]; then
        MISSING="$MISSING mock"
    fi

    # Check for nfs-utils
    env mount.nfs -V > /dev/null 2>&1
    if [ $? -ne 0 ]; then
        MISSING="$MISSING nfs-utils"
    fi

    # Check for the Perl Test Harness
    env prove --version > /dev/null 2>&1
    if [ $? -ne 0 ]; then
        MISSING="$MISSING perl-Test-Harness"
    fi

    # Check for XFS programs
    env mkfs.xfs -V > /dev/null 2>&1
    if [ $? -ne 0 ]; then
        MISSING="$MISSING xfsprogs"
    fi

    # Check for attr
    env getfattr --version > /dev/null 2>&1
    if [ $? -ne 0 ]; then
        MISSING="$MISSING attr"
    fi

    ## If dependencies are missing, warn the user and abort
    if [ "x$MISSING" != "x" ]; then
        echo "Aborting."
        echo
        echo "The following required tools are missing:"
        echo
        for pkg in $MISSING; do
            echo "  * $pkg"
        done
        echo
        echo "Please install them and try again."
        echo
        exit 2
    fi
}

function check_location()
{
    regression_testsdir=$(dirname $0);

    if [ ! -f ${regression_testsdir}/tests/include.rc ]; then
        echo "Aborting."
        echo
        echo "The tests/ subdirectory seems to be missing."
        echo
        echo "Please correct the problem and try again."
        echo
        exit 1
    fi
}

function check_user()
{
    # If we're not running as root, warn the user and abort
    MYUID=`/usr/bin/id -u`
    if [ 0${MYUID} -ne 0 ]; then
        echo "Aborting."
        echo
        echo "The GlusterFS Test Framework must be run as root."
        echo
        echo "Please change to the root user and try again."
        echo
        exit 3
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

echo
echo ... GlusterFS Test Framework ...
echo

# Make sure we're running as the root user
check_user

# Make sure the needed programs are available
check_dependencies

# Check we're running from the right location
check_location

# Run the tests
main "$@"
