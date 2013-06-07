
GFS_TAR = ../../glusterfs-${VERSION}.tar.gz

.PHONY: all

all:
	@echo "To build RPMS run 'make glusterrpms'"

.PHONY: glusterrpms prep srcrpm testsrpm clean

glusterrpms: prep srcrpm rpms
	-rm -rf rpmbuild

prep:
	if [ ! -e $(GFS_TAR) ]; then \
		$(MAKE) -C ../.. dist; \
	fi
	-mkdir -p rpmbuild/SPECS
	-mkdir -p rpmbuild/RPMS
	-mkdir -p rpmbuild/SRPMS
	-rm -rf rpmbuild/SOURCES
	@if [ -d /d/cache/glusterfs -a -e /d/cache/glusterfs/sources ]; then \
		echo "copying glusterfs rpm files from local cache..."  ; \
		mkdir -p ./rpmbuild/SOURCES; \
		cp /d/cache/glusterfs/* ./rpmbuild/SOURCES/ ; \
	elif [ -x /usr/bin/git ]; then \
		echo "fetching glusterfs rpm files from fedora git repo..."; \
		cd ./rpmbuild && git clone git://pkgs.fedoraproject.org/glusterfs.git > /dev/null && mv glusterfs SOURCES; \
	else \
		echo "glusterfs rpm files not fetched, you don't have git installed!" ; \
		exit 1 ; \
	fi
	cp ../../*.tar.gz ./rpmbuild/SOURCES
	cp ../../glusterfs.spec ./rpmbuild/SPECS

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
