#!/bin/bash

. $(dirname $0)/../include.rc

cleanup
RESULT_DIR=$(mktemp -d -p /var/tmp rpm-tests.XXXXXXXX)

# enable some extra debugging
if [ -n "${DEBUG}" -a "${DEBUG}" != "0" ]
then
	exec &> ${RESULT_DIR}/log
	set -x
fi

# checkout the sources to a new directory to execute ./configure and all
REPO=${PWD}
COMMIT=$(git describe)
mkdir ${RESULT_DIR}/sources
cd ${RESULT_DIR}/sources
git clone -q -s file://${REPO} .
git checkout -q -b rpm-test ${COMMIT}

# build the .tar.gz
[ -e configure ] || ./autogen.sh 2>&1 > /dev/null
TEST ./configure --enable-fusermount
TEST make dist

# need to use double quoting because the command is passed to TEST
# EPEL-5 does not like new versions of rpmbuild and requires some _source_* defines
TEST rpmbuild --define "'_srcrpmdir $PWD'" \
	--define "'_source_payload w9.gzdio'" \
	--define "'_source_filedigest_algorithm 1'" \
	-ts *.tar.gz

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
		TEST ${RESULT_DIR}/${EPEL_RELEASE}/mock.sh
	else
		# switch to the user called 'mock'
		chown mock:mock ${RESULT_DIR}/${EPEL_RELEASE}
		# "su" might not work, using sudo instead
		TEST sudo -u mock -E ${RESULT_DIR}/${EPEL_RELEASE}/mock.sh
	fi
done

# we could build for the last two Fedora releases too, but that is not
# possible on EPEL-5/6 installations, Fedora 17 and newer have unmet
# dependencies on the build-server :-/

# only remove ${RESULT_DIR} if we're not debugging
[ "${DEBUG}" = "0" ] && rm -rf ${RESULT_DIR}

cleanup

