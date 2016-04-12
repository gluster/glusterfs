#!/bin/bash
#

. $(dirname $0)/../include.rc

buildscratch=""

case $OSTYPE in
Linux)
        ;;
*)
        echo "Skip Linux specific test" >&2
        SKIP_TESTS
        exit 0
        ;;
esac

# look in the usual places for the build tree
if [ -d /build/scratch ]; then
        buildscratch="/build/scratch"
else
        # might be in developer's tree
        if [ -d ./libglusterfs/src/.libs ]; then
                buildscratch="."
        elif [ -d ../libglusterfs/src/.libs ]; then
                buildscratch=".."
        fi
fi

if [ -z ${buildscratch} ]; then
        echo "could find build tree in /build/scratch, . or .." >&2
        SKIP_TESTS
        exit 0
fi

# check symbols

rm -f ./.symbol-check-errors

TEST find ${buildscratch} -name \*.o -exec ./tests/basic/symbol-check.sh {} \\\;

TEST [ ! -e ./.symbol-check-errors ]

rm -f ./.symbol-check-errors

cleanup
