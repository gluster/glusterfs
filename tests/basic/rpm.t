#!/bin/bash
#
# This test will run mock and rebuild the srpm for the latest two EPEL version.
# By default, the results and the chroots are deleted.
#
# When debugging is needed, make sure to set DEBUG=1 in the environment or this
# script. When debugging is enabled, the resulting log files and chroots are
# kept. With debugging enabled, this test will fail the regression test, and
# all output is saved to rpmbuild-mock.log. Tests are run in parallel, so the
# logfile may be difficult to read.
#
# chroots are configured in /etc/mock/*.cfg, with site-defaults.cfg as main
# configuration file. The default for chroots is /var/lib/mock, but this
# depends on the 'basedir' configuration option set in the mentioned files.
#

. $(dirname $0)/../include.rc

# enable some extra debugging
if [ -n "${DEBUG}" -a "${DEBUG}" != "0" ]
then
	exec &> rpmbuild-mock.log
	set -x
        MOCK_CLEANUP='--no-cleanup-after'
else
        MOCK_CLEANUP='--cleanup-after'
fi

# detect the branch we're based off
if [ -n "${BRANCH}" ] ; then
        # $BRANCH is set in the environment (by Jenkins or other)
        GIT_PARENT="origin/${BRANCH}"
else
        # get a reference to the latest clean tree
        GIT_PARENT=$(git describe --abbrev=0)
fi

# Filter out everything and what remains needs to be built
BUILD_FILES=$(git diff --name-status ${GIT_PARENT} | grep -Ev '^M.*\.(c|h|py)' | awk {'print $2'})
SELFTEST=$(grep -e 'tests/basic/rpm.t' <<< "${BUILD_FILES}")
BUILD_FILES=$(grep -Ev '^tests/' <<< "${BUILD_FILES}")
if [ -z "${BUILD_FILES}" -a -z "${SELFTEST}" ]
then
        # nothing affecting packaging changed, no need to retest rpmbuild
        SKIP_TESTS
        cleanup
        exit 0
fi

# checkout the sources to a new directory to execute ./configure and all
REPO=${PWD}
COMMIT=$(git describe)
mkdir rpmbuild-mock.d
pushd rpmbuild-mock.d 2>/dev/null

function git_quiet() {
        git ${@} 2>&1 > /dev/null
}

TEST git_quiet clone -s file://${REPO} .
TEST git_quiet checkout -b rpm-test ${COMMIT}

# build the glusterfs-*.tar.gz
function build_srpm_from_tgz() {
        rpmbuild -ts $1 \
                --define "_srcrpmdir ${PWD}" \
                --define '_source_payload w9.gzdio' \
                --define '_source_filedigest_algorithm 1'
}

TEST ./autogen.sh
TEST ./configure
TEST make dist

# build the glusterfs src.rpm
TEST build_srpm_from_tgz ${PWD}/*.tar.gz

# build for the last two Fedora EPEL releases (x86_64 only)
for MOCK_CONF in $(ls -x1 /etc/mock/*.cfg | egrep -e 'epel-[0-9]+-x86_64.cfg$' | tail -n2)
do
	EPEL_RELEASE=$(basename ${MOCK_CONF} .cfg)
	mkdir -p "${PWD}/mock.d/${EPEL_RELEASE}"
	chgrp mock "${PWD}/mock.d/${EPEL_RELEASE}"
	chmod 0775 "${PWD}/mock.d/${EPEL_RELEASE}"
	MOCK_RESULTDIR="--resultdir ${PWD}/mock.d/${EPEL_RELEASE}"
	# expand the mock command line
	MOCK_CMD="/usr/bin/mock ${MOCK_CLEANUP} \
		${MOCK_RESULTDIR} \
		-r ${EPEL_RELEASE} --rebuild ${PWD}/*.src.rpm"

	# write the mock command to a file, so that its easier to execute
	cat << EOF > mock-${EPEL_RELEASE}.sh
#!/bin/sh
${MOCK_CMD}
EOF
	chmod +x mock-${EPEL_RELEASE}.sh

	# root can not run 'mock', it needs to drop priviledges
	if (groups | grep -q mock)
	then
		# the current user is in group 'mock'
		${PWD}/mock-${EPEL_RELEASE}.sh &
	else
		# "su" might not work, using sudo instead
		sudo -u mock -E ${PWD}/mock-${EPEL_RELEASE}.sh &
	fi
	sleep 5
done

# TAP and Prove aren't smart about loops
TESTS_EXPECTED_IN_LOOP=2
for mockjob in $(jobs -p)
do
	TEST_IN_LOOP wait ${mockjob}
done

# we could build for the last two Fedora releases too, but that is not
# possible on EPEL-5/6 installations, Fedora 17 and newer have unmet
# dependencies on the build-server :-/

# logs are archived by Jenkins
if [ -d '/build/install/var' ]
then
        LOGS=$(find mock.d -type f -name '*.log')
        [ -n "${LOGS}" ] && xargs cp --parents ${LOGS} /build/install/var/
fi

popd 2>/dev/null
# only remove rpmbuild-mock.d if we're not debugging
[ "${DEBUG}" = "0" ] && rm -rf rpmbuild-mock.d

cleanup
