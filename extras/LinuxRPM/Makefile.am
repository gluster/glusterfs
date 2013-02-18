
GFS_TAR = ../../glusterfs-3git.tar.gz
UFO_TAR = ../../gluster-swift-ufo-3git.tar.gz
GFS_SPEC = ../../glusterfs.spec

.PHONY: all

all: 
	@echo "To build RPMS run 'make glusterrpms'"

.PHONY: glusterrpms prep srcrpm testsrpm clean

glusterrpms: prep srcrpm rpms
	-rm -rf rpmbuild

prep:: 
	if [ ! -e $(GFS_TAR) -a ! -e $(UFO_TAR) -a ! -e $(GFS_SPEC) ]; then \
		$(MAKE) -c ../.. dist; \
	fi
	-mkdir -p rpmbuild/SPECS
	-mkdir -p rpmbuild/RPMS
	-mkdir -p rpmbuild/SRPMS
	-rm -rf rpmbuild/SOURCES
	cd rpmbuild && git clone http://pkgs.fedoraproject.org/cgit/glusterfs.git > /dev/null && mv glusterfs SOURCES
	cp ../../*.tar.gz rpmbuild/SOURCES
	cp ../../glusterfs.spec rpmbuild/SPECS

# openstack/launchpad 'hides' source tarballs behind javascript glorp,
# and curl will fail to fetch it using their download URL; we're forced
# to fetch it from somewhere else.
# maybe someone with better awk and sed skills than I have can suggest 
# something better here.
SWIFT_URL = http://pkgs.fedoraproject.org/repo/pkgs/glusterfs/$(shell grep -v gluster rpmbuild/SOURCES/sources | cut -d ' ' -f 3)/$(shell grep -v gluster rpmbuild/SOURCES/sources | cut -d ' ' -f 1)/$(shell grep -v gluster rpmbuild/SOURCES/sources | cut -d ' ' -f 3)

prep::
	@if [ -x /usr/bin/curl -a ! -e rpmbuild/SOURCES/swift-1.7.4.tar.gz ]; then \
		echo "fetching swift..." ; \
		cd rpmbuild/SOURCES && /usr/bin/curl -s -O $(SWIFT_URL) ; \
	elif [ ! -x /usr/bin/curl ]; then \
		echo "you don't have curl installed, swift source not fetched" ; \
		exit 1 ; \
	fi

srcrpm:
	rpmbuild --define '_topdir $(shell pwd)/rpmbuild' -bs rpmbuild/SPECS/glusterfs.spec
	mv rpmbuild/SRPMS/* .

rpms:
	rpmbuild --define '_topdir $(shell pwd)/rpmbuild' -bb rpmbuild/SPECS/glusterfs.spec
	mv rpmbuild/RPMS/*/* .

# EPEL-5 does not like new versions of rpmbuild and requires some
# _source_* defines

testsrpm: prep
	rpmbuild --define '_topdir $(shell pwd)/rpmbuild' \
		--define '_source_payload w9.gzdio' \
		--define '_source_filedigest_algorithm 1' \
		-bs rpmbuild/SPECS/glusterfs.spec
	mv rpmbuild/SRPMS/* ../..
	-rm -rf rpmbuild

clean:
	-rm -rf rpmbuild
	-rm -f *.rpm
