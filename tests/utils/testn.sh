#!/bin/bash
#
# Use this script to identify the command and line-number of test-cases.
#

if [ -z "${1}" -a -z "${2}" ]
then
        echo "Usage: ${0} path/to/test/case.t testnumber"
        exit 1
elif [ -z "${2}" ]
then
        echo "ERROR: The second parameter to ${0} should be a number."
        exit 2
fi

awk '{print FNR " " $0}' ${1} | egrep '^[[:digit:]]+[[:space:]]*(EXPECT|TEST|EXPECT_WITHIN|EXPECT_KEYWORD)' | sed -n ${2}p
