#!/bin/bash
# Copyright (c) 2013-2014 Red Hat, Inc. <http://www.redhat.com>
#

export TZ=UTC
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

    # Check for YAJL
    if [ ! -x /usr/bin/json_verify ]; then
        MISSING="$MISSING yajl"
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

function run_tests()
{
    declare -A DONE
    match()
    {
        # Patterns considered valid:
        # 1. full or partial file/directory names
        #   basic         matches tests/basic
        #   basic/afr     matches tests/basic/afr
        # 2. globs
        #   basic/*       matches all files and directories in basic
        #   basic/*/      matches subdirectories in basic (afr|ec)
        # 3. numbered bug matching
        #   884455        matches bugs/bug-884455.t
        #   859927        matches bugs/859927, bugs/bug-859927.t
        #   1015990       matches /bugs/bug-1015990-rep.t, bug-1015990.t
        # ...lots of other cases accepted as well, since globbing is tricky.
        local t=$1
        local mt=$1
        shift
        local a
        local match=1
        if [ -d $t ] ; then
            # Allow matching on globs like 'basic/*/'
            mt=$t/
        fi
        for a in "$@" ; do
            case "$mt" in
                *$a|*/bugs/$a/|*/bugs/$a.t|*/bugs/bug-$a.t|*/bugs/bug-$a-*.t)
                    match=0
                    ;;
            esac
        done
        if [ "${DONE[$(dirname $t)]}" != "" ] ; then
            # Parentdir is already matched
            match=1
            if [ -d $t ] ; then
                # Ignore subdirectory as well
                DONE[$t]=$t
            fi
       elif [ $match -eq 0 -a -d $t ] ; then
            # Make sure children of this matched directory will be ignored
            DONE[$t]=$t
        elif [[ -f $t && ! $t =~ .*\.t ]] ; then
            # Ignore files not ending in .t
            match=1
        fi
        return $match
    }
    RES=0
    for t in $(find ${regression_testsdir}/tests | LC_COLLATE=C sort) ; do
        if match $t "$@" ; then
            if [ -d $t ] ; then
                echo "Running tests in directory $t"
                prove -rf --timer $t
            elif  [ -f $t ] ; then
                echo "Running tests in file $t"
                prove -f --timer $t
            fi
            TMP_RES=$?
            if [ ${TMP_RES} -ne 0 ] ; then
                RES=${TMP_RES}
                FAILED="$FAILED $t"
            fi
        fi
    done
    if [ ${RES} -ne 0 ] ; then
        echo "Failed tests ${FAILED}"
    fi
    return ${RES}
}

function main()
{
    if [ $# -lt 1 ]; then
        echo "Running all the regression test cases"
        prove -rf --timer ${regression_testsdir}/tests;
    else
        run_tests "$@"
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
