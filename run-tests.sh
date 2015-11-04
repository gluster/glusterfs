#!/bin/bash
# Copyright (c) 2013-2014 Red Hat, Inc. <http://www.redhat.com>
#

export TZ=UTC
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
        FAILED=$( echo ${FAILED} | tr ' ' '\n' | sort -u )
        echo "Failed tests ${FAILED}"
    fi
    return ${RES}
}

# If you're submitting a fix related to one of these tests and want its result
# to be considered, you'll need to remove it from the list as part of your
# patch.
function is_bad_test ()
{
    local name=$1
    for bt in ./tests/basic/quota-anon-fd-nfs.t \
              ./tests/bugs/quota/bug-1235182.t \
              ./tests/basic/quota-nfs.t \
              ./tests/basic/tier/tier_lookup_heal.t \
              ./tests/basic/tier/bug-1214222-directories_missing_after_attach_tier.t \
              ./tests/basic/tier/fops-during-migration.t \
	      ./tests/basic/tier/record-metadata-heat.t \
              ./tests/bugs/snapshot/bug-1109889.t \
              ./tests/bugs/distribute/bug-1066798.t \
              ./tests/bugs/glusterd/bug-1238706-daemons-stop-on-peer-cleanup.t \
              ./tests/geo-rep/georep-basic-dr-rsync.t \
              ./tests/geo-rep/georep-basic-dr-tarssh.t \
              ./tests/bugs/replicate/bug-1221481-allow-fops-on-dir-split-brain.t \
              ; do
        [ x"$name" = x"$bt" ] && return 0 # bash: zero means true/success
    done
    return 1				  # bash: non-zero means false/failure
}

function run_all ()
{
    find ${regression_testsdir}/tests -name '*.t' \
    | LC_COLLATE=C sort \
    | while read t; do
	old_cores=$(ls /core.* 2> /dev/null | wc -l)
        retval=0
        prove -mf --timer $t
        TMP_RES=$?
        if [ ${TMP_RES} -ne 0 ] ; then
            echo "$t: bad status $TMP_RES"
            retval=$((retval+1))
        fi
        new_cores=$(ls /core.* 2> /dev/null | wc -l)
        if [ x"$new_cores" != x"$old_cores" ]; then
            core_diff=$((new_cores-old_cores))
            echo "$t: $core_diff new core files"
            retval=$((retval+2))
        fi
        if [ $retval -ne 0 ]; then
	    if is_bad_test $t; then
		echo  "Ignoring failure from known-bad test $t"
	        retval=0
            else
                echo
                echo "Running failed test $t in debug mode"
                echo "Just for debug data, does not change test result"
                echo
                bash -x $t
                echo
		return $retval
	    fi
        fi
    done
}

function main()
{
    if [ $# -lt 1 ]; then
        echo "Running all the regression test cases (new way)"
        #prove -rf --timer ${regression_testsdir}/tests;
        run_all
    else
        run_tests "$@"
    fi
}

function main_and_retry()
{
    RESFILE=`mktemp /tmp/${0##*/}.XXXXXX` || exit 1
    main "$@" | tee ${RESFILE}
    RET=$?

    FAILED=$( awk '/Failed: /{print $1}' ${RESFILE} )
    if [ "x${FAILED}" != "x" ] ; then
       echo ""
       echo "       *********************************"
       echo "       *       REGRESSION FAILED       *"
       echo "       * Retrying failed tests in case *"
       echo "       * we got some spurous failures  *"
       echo "       *********************************"
       echo ""
       main ${FAILED}
       RET=$?
    fi

    rm -f ${RESFILE}
    return ${RET}
}

echo
echo ... GlusterFS Test Framework ...
echo

force="no"
retry="no"
args=`getopt fr $*`
set -- $args
while [ $# -gt 0 ]; do
    case "$1" in
    -f)    force="yes" ;;
    -r)    retry="yes" ;;
    --)    shift; break;;
    esac
    shift
done

# Make sure we're running as the root user
check_user

# Make sure the needed programs are available
check_dependencies

# Check we're running from the right location
check_location

# Run the tests
if [ "x${retry}" = "xyes" ] ; then
    main_and_retry $@
else
    main "$@"
fi
