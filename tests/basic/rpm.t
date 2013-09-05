#!/bin/bash

. $(dirname $0)/../include.rc

RESULT_DIR=$(mktemp -d -p /var/tmp rpm-tests.XXXXXXXX)

# enable some extra debugging
if [ -n "${DEBUG}" -a "${DEBUG}" != "0" ]
then
	exec &> ${RESULT_DIR}/log
	set -x
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
# filter out any files not affecting the build itself
CHANGED_FILES=$(grep -E -v \
        -e '\.c$' \
        -e '\.h$' \
        -e '\.py$' \
        -e '^tests/' \
        <<< "${CHANGED_FILES}")
if [ -z "${CHANGED_FILES}" ]
then
        # only contents of files were changed, no need to retest rpmbuild
        SKIP_TESTS
	rm -rf ${RESULT_DIR}
        cleanup
        exit 0
fi

# checkout the sources to a new directory to execute ./configure and all
REPO=${PWD}
COMMIT=$(git describe)
mkdir ${RESULT_DIR}/sources
cd ${RESULT_DIR}/sources
git clone -q -s file://${REPO} .
git checkout -q -b rpm-test ${COMMIT}

# build the glusterfs-*.tar.gz
[ -e configure ] || ./autogen.sh 2>&1 > /dev/null
TEST ./configure --enable-fusermount
TEST make dist

# build the glusterfs src.rpm
ls extras
TEST make -C extras/LinuxRPM testsrpm

chmod g=rwx ${RESULT_DIR}
chown :mock ${RESULT_DIR}

# build for the last two Fedora EPEL releases (x86_64 only)
for MOCK_CONF in $(ls -x1 /etc/mock/*.cfg | egrep -e 'epel-[0-9]+-x86_64.cfg$' | tail -n2)
do
	EPEL_RELEASE=$(basename ${MOCK_CONF} .cfg)
	mkdir ${RESULT_DIR}/${EPEL_RELEASE}
	chmod g=rwx ${RESULT_DIR}/${EPEL_RELEASE}
	chown :mock ${RESULT_DIR}/${EPEL_RELEASE}
	# expand the mock command line
	MOCK_CMD=$(echo /usr/bin/mock --cleanup-after \
		--resultdir=${RESULT_DIR}/${EPEL_RELEASE} \
		-r ${EPEL_RELEASE} --rebuild ${PWD}/*.src.rpm)

	# write the mock command to a file, so that its easier to execute
	cat << EOF > ${RESULT_DIR}/${EPEL_RELEASE}/mock.sh
#!/bin/sh
${MOCK_CMD}
EOF
	chmod +x ${RESULT_DIR}/${EPEL_RELEASE}/mock.sh

	# root can not run 'mock', it needs to drop priviledges
	if (groups | grep -q mock)
	then
		# the current user is in group 'mock'
		${RESULT_DIR}/${EPEL_RELEASE}/mock.sh 2>&1 > ${RESULT_DIR}/${EPEL_RELEASE}.out &
	else
		# switch to the user called 'mock'
		chown mock:mock ${RESULT_DIR}/${EPEL_RELEASE}
		# "su" might not work, using sudo instead
		sudo -u mock -E ${RESULT_DIR}/${EPEL_RELEASE}/mock.sh 2>&1 > ${RESULT_DIR}/${EPEL_RELEASE}.out &
	fi
	sleep 5
done

# TAP/Prove aren't smart about loops
TESTS_EXPECTED_IN_LOOP=2
for mockjob in `jobs -p`; do
	TEST_IN_LOOP wait $mockjob
done


# we could build for the last two Fedora releases too, but that is not
# possible on EPEL-5/6 installations, Fedora 17 and newer have unmet
# dependencies on the build-server :-/

# only remove ${RESULT_DIR} if we're not debugging
[ "${DEBUG}" = "0" ] && rm -rf ${RESULT_DIR}

cleanup
