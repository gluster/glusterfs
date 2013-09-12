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

# check for changed files
CHANGED_FILES=$(git diff --name-only ${GIT_PARENT})
# if a commit changes this test, we should not skip it
SELFTEST=$(grep -e 'tests/basic/rpm.t' <<< "${CHANGED_FILES}")
# filter out any files not affecting the build itself
CHANGED_FILES=$(grep -E -v \
        -e '\.c$' \
        -e '\.h$' \
        -e '\.py$' \
        -e '^tests/' \
        <<< "${CHANGED_FILES}")
if [ -z "${CHANGED_FILES}" -a -z "${SELFTEST}" ]
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
git clone -q -s file://${REPO} .
git checkout -q -b rpm-test ${COMMIT}

# build the glusterfs-*.tar.gz
[ -e configure ] || ./autogen.sh 2>&1 > /dev/null
TEST ./configure --enable-fusermount
TEST make dist

# build the glusterfs src.rpm
ls extras
TEST make -C extras/LinuxRPM testsrpm

# build for the last two Fedora EPEL releases (x86_64 only)
for MOCK_CONF in $(ls -x1 /etc/mock/*.cfg | egrep -e 'epel-[0-9]+-x86_64.cfg$' | tail -n2)
do
	EPEL_RELEASE=$(basename ${MOCK_CONF} .cfg)
	# expand the mock command line
	MOCK_CMD="/usr/bin/mock ${MOCK_CLEANUP} \
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

popd 2>/dev/null
# only remove rpmbuild-mock.d if we're not debugging
[ "${DEBUG}" = "0" ] && rm -rf rpmbuild-mock.d

cleanup
