#!/bin/bash
#
# Gather statistics on "Who wrote GlusterFS". The idea comes from the excellent
# articles on http://lwn.net/ named "Who wrote <linux-version>?".
#
# gitdm comes from git://git.lwn.net/gitdm.git by Jonathan Corbet.
#
# Confguration files used:
#  - gitdm.config: main configuration file, pointing to the others
#  - gitdm.aliases: merge users with different emailaddresses into one
#  - gitdm.domain-map: map domain names from emailaddresses to companies
#

DIRNAME=$(dirname $0)

GITDM_REPO=git://git.lwn.net/gitdm.git
GITDM_DIR=${DIRNAME}/gitdm
GITDM_CMD="python ${GITDM_DIR}/gitdm"

error()
{
        local ret=${?}
        printf "${@}\n" > /dev/stderr
        return ${ret}
}

check_gitdm()
{
        if [ ! -e "${GITDM_DIR}/gitdm" ]
        then
                git clone --quiet ${GITDM_REPO} ${DIRNAME}/gitdm
        fi
}

# The first argument is the revision-range (see 'git rev-list --help').
# REV can be empty, and the statistics will be calculated over the whole
# current branch.
REV=${1}
shift
# all remaining options are passed to gitdm, see the gitdm script for an
# explanation of the accepted options.
GITDM_OPTS=${@}

if ! check_gitdm
then
        error "Could not find 'gitdm', exiting..."
        exit 1
fi

git log --numstat -M ${REV} | ${GITDM_CMD} -b ${DIRNAME} -n ${GITDM_OPTS}
