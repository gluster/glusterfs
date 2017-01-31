#!/bin/bash
# Copyright (c) 2013-2014 Red Hat, Inc. <http://www.redhat.com>
#

export TZ=UTC
force="no"
head="yes"
retry="yes"
tests=""
exit_on_failure="yes"
skip_bad_tests="yes"
skip_known_bugs="yes"
section_separator="========================================"

OSTYPE=$(uname -s)

function check_dependencies()
{
    ## Check all dependencies are present
    MISSING=""

    # Check for dbench
    env dbench --usage > /dev/null 2>&1
    if [ $? -ne 0 ]; then
        MISSING="$MISSING dbench"
    fi

    # Check for git
    env git --version > /dev/null 2>&1
    if [ $? -ne 0 ]; then
        MISSING="$MISSING git"
    fi

    # Check for nfs-utils (Linux-only: built-in NetBSD with different name)
    if [ "x`uname -s`" = "xLinux" ] ; then
      env mount.nfs -V > /dev/null 2>&1
      if [ $? -ne 0 ]; then
          MISSING="$MISSING nfs-utils"
      fi
    fi

    # Check for the Perl Test Harness
    env prove --version > /dev/null 2>&1
    if [ $? -ne 0 ]; then
        MISSING="$MISSING perl-Test-Harness"
    fi

    which json_verify > /dev/null
    if [ $? -ne 0 ]; then
        MISSING="$MISSING json_verify"
    fi

    # Check for XFS programs (Linux Only: NetBSD does without)
    if [ "x`uname -s`" = "xLinux" ] ; then
      env mkfs.xfs -V > /dev/null 2>&1
      if [ $? -ne 0 ]; then
          MISSING="$MISSING xfsprogs"
      fi
    fi

    # Check for attr
    env getfattr --version > /dev/null 2>&1
    if [ $? -ne 0 ]; then
        MISSING="$MISSING attr"
    fi

    # Check for pidof
    pidof pidof > /dev/null 2>&1
    if [ $? -ne 0 ]; then
        MISSING="$MISSING pidof"
    fi

    # check for psutil python package
    test `uname -s` == "Darwin" || test `uname -s` == "FreeBSD" && {
        pip show psutil | grep -q psutil >/dev/null 2>&1
        if [ $? -ne 0 ]; then
            MISSING="$MISSING psutil"
        fi
    }

    ## If dependencies are missing, warn the user and abort
    if [ "x$MISSING" != "x" ]; then
        test "x${force}" != "xyes" && echo "Aborting."
        echo
        echo "The following required tools are missing:"
        echo
        for pkg in $MISSING; do
            echo "  * $pkg"
        done
        echo
        test "x${force}" = "xyes" && return
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

function match()
{
        # Patterns considered valid:
        # 0. Empty means everything
        #    ""           matches ** i.e all
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
        shift
        local a
        local match=1
        if [ -z "$@" ]; then
            match=0
            return $match
        fi
        for a in $@ ; do
            case "$t" in
                *$a*)
                    match=0
                    ;;
            esac
        done
        return $match
}

# Tests can have comment lines with some comma separated values within them.
# Key names used to determine test status are
# G_TESTDEF_TEST_STATUS_CENTOS6
# G_TESTDEF_TEST_STATUS_NETBSD7
# Some examples:
# G_TESTDEF_TEST_STATUS_CENTOS6=BAD_TEST,BUG=123456
# G_TESTDEF_TEST_STATUS_NETBSD7=KNOWN_ISSUE,BUG=4444444
# G_TESTDEF_TEST_STATUS_CENTOS6=BAD_TEST,BUG=123456;555555
# You can change status of test to enabled or delete the line only if all the
# bugs are closed or modified or if the patch fixes it.
function get_test_status ()
{
    local test_name=$1
    local host_os=""
    local result=""

    host_os=$(uname -s)

    case "$host_os" in
    # Leaving out the logic to determine the particular distro and version
    # for later. Why does the key have the distro and version then?
    # Because changing the key in all test files would be very big process
    # updating just this function with a better logic much simpler.
    Linux)
        result=$(grep -e "^#G_TESTDEF_TEST_STATUS_CENTOS6" $test_name | \
                 awk -F"," {'print $1'} | awk -F"=" {'print $2'}) ;;
    NetBSD)
        result=$(grep -e "^#G_TESTDEF_TEST_STATUS_NETBSD7" $test_name | \
                 awk -F"," {'print $1'} | awk -F"=" {'print $2'}) ;;
    *)
        result="ENABLED" ;;
    esac

    echo "$result"

}

function get_bug_list_for_disabled_test ()
{
    local test_name=$1
    local host_os=""
    local result=""

    host_os=$(uname -s)

    case "$host_os" in
    # Leaving out the logic to determine the particular distro and version
    # for later. Why does the key have the distro and version then?
    # Because changing the key in all test files would be very big process
    # updating just this function with a better logic much simpler.
    Linux)
        result=$(grep -e "^#G_TESTDEF_TEST_STATUS_CENTOS6" $test_name | \
                 awk -F"," {'print $2'} | awk -F"=" {'print $2'}) ;;
    NetBSD)
        result=$(grep -e "^#G_TESTDEF_TEST_STATUS_NETBSD7" $test_name | \
                 awk -F"," {'print $2'} | awk -F"=" {'print $2'}) ;;
    *)
        result="0000000" ;;
    esac

    echo "$result"

}

function run_tests()
{
    RES=0
    FAILED=''
    GENERATED_CORE=''
    total_tests=0
    selected_tests=0
    skipped_bad_tests=0
    skipped_known_issue_tests=0
    total_run_tests=0

    # key = path of .t file; value = time taken to run the .t file
    declare -A ELAPSEDTIMEMAP

    for t in $(find ${regression_testsdir}/tests -name '*.t' \
               | LC_COLLATE=C sort) ; do
        old_cores=$(ls /*-*.core 2> /dev/null | wc -l)
        total_tests=$((total_tests+1))
        if match $t "$@" ; then
            selected_tests=$((selected_tests+1))
            echo
            echo $section_separator$section_separator
            if [[ $(get_test_status $t) == "BAD_TEST" ]] && \
               [[ $skip_bad_tests == "yes" ]]
            then
                skipped_bad_tests=$((skipped_bad_tests+1))
                echo "Skipping bad test file $t"
                echo "Reason: bug(s):" $(get_bug_list_for_disabled_test $t)
                echo $section_separator$section_separator
                echo
                continue
            fi
            if [[ $(get_test_status $t) == "KNOWN_ISSUE" ]] && \
               [[ $skip_known_bugs == "yes" ]]
            then
                skipped_known_issue_tests=$((skipped_known_issue_tests+1))
                echo "Skipping test file $t due to known issue"
                echo "Reason: bug(s):" $(get_bug_list_for_disabled_test $t)
                echo $section_separator$section_separator
                echo
                continue
            fi
            total_run_tests=$((total_run_tests+1))
            echo "[$(date +%H:%M:%S)] Running tests in file $t"
            starttime="$(date +%s)"
            prove -vf $t
            TMP_RES=$?
            ELAPSEDTIMEMAP[$t]=`expr $(date +%s) - $starttime`
            if [ ${TMP_RES} -ne 0 ]  && [ "x${retry}" = "xyes" ] ; then
                echo "$t: bad status $TMP_RES"
                echo ""
                echo "       *********************************"
                echo "       *       REGRESSION FAILED       *"
                echo "       * Retrying failed tests in case *"
                echo "       * we got some spurous failures  *"
                echo "       *********************************"
                echo ""
                prove -vf $t
                TMP_RES=$?
            fi
            if [ ${TMP_RES} -ne 0 ] ; then
                RES=${TMP_RES}
                FAILED="${FAILED}${t} "
            fi
            new_cores=$(ls /*-*.core 2> /dev/null | wc -l)
            if [ x"$new_cores" != x"$old_cores" ]; then
                core_diff=$((new_cores-old_cores))
                echo "$t: $core_diff new core files"
                RES=1
                GENERATED_CORE="${GENERATED_CORE}${t} "
            fi
            echo "End of test $t"
            echo $section_separator$section_separator
            echo
            if [ $RES -ne 0 ] && [ x"$exit_on_failure" = "xyes" ] ; then
                break;
            fi
        fi
    done
    echo
    echo "Run complete"
    echo $section_separator$section_separator
    echo "Number of tests found:                             $total_tests"
    echo "Number of tests selected for run based on pattern: $selected_tests"
    echo "Number of tests skipped as they were marked bad:   $skipped_bad_tests"
    echo "Number of tests skipped because of known_issues:   $skipped_known_issue_tests"
    echo "Number of tests that were run:                     $total_run_tests"
    if [ ${RES} -ne 0 ] ; then
        FAILED=$( echo ${FAILED} | tr ' ' '\n' | sort -u )
        FAILED_COUNT=$( echo -n "${FAILED}" | grep -c '^' )
        echo -e "\n$FAILED_COUNT test(s) failed \n${FAILED}"
        GENERATED_CORE=$( echo  ${GENERATED_CORE} | tr ' ' '\n' | sort -u )
        GENERATED_CORE_COUNT=$( echo -n "${GENERATED_CORE}" | grep -c '^' )
        echo -e "\n$GENERATED_CORE_COUNT test(s) generated core \n${GENERATED_CORE}"
    fi

    echo
    echo "Tests ordered by time taken, slowest to fastest: "
    echo $section_separator$section_separator
    for key in "${!ELAPSEDTIMEMAP[@]}"
    do
        echo "$key  -  ${ELAPSEDTIMEMAP["$key"]} second"
    done | sort -rn -k3

    echo
    echo "Result is $RES"
    echo
    return ${RES}
}

function run_head_tests()
{
    [ -d ${regression_testsdir}/.git ] || return 0

    # The git command needs $cwd to be within the repository, but run_tests
    # needs it to be back where we started.
    pushd $regression_testsdir
    git_cmd="git diff-tree --no-commit-id --name-only --diff-filter=ACMRTUXB"
    htests=$($git_cmd -r HEAD tests | grep '.t$')
    popd
    [ -n "$htests" ] || return 0

    # Perhaps it's not ideal that we'll end up re-running these tests, but the
    # gains from letting them fail fast in the first round should outweigh the
    # losses from running them again in the second.  OTOH, with so many of our
    # tests being non-deterministic, maybe it doesn't hurt to give the newest
    # tests an extra workout.
    run_tests "$htests"
}

function parse_args () {
    args=`getopt frcbkhH "$@"`
    set -- $args
    while [ $# -gt 0 ]; do
        case "$1" in
        -f)    force="yes" ;;
        -h)    head="no" ;;
        -H)    head="only" ;;
        -r)    retry="yes" ;;
        -c)    exit_on_failure="no" ;;
        -b)    skip_bad_tests="no" ;;
        -k)    skip_known_bugs="no" ;;
        --)    shift; break;;
        esac
        shift
    done
    tests="$@"
}


echo
echo ... GlusterFS Test Framework ...
echo

# Get user options
parse_args "$@"

# Make sure we're running as the root user
check_user

# Make sure the needed programs are available
check_dependencies

# Check we're running from the right location
check_location

# Run the tests
if [ x"$head" != x"no" ]; then
        run_head_tests || exit 1
fi
if [ x"$head" != x"only" ]; then
        run_tests "$tests" || exit 1
fi
