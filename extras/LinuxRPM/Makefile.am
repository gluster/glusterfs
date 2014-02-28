
GFS_TAR = ../../glusterfs-$(VERSION).tar.gz

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
	-mkdir -p rpmbuild/BUILD
	-mkdir -p rpmbuild/SPECS
	-mkdir -p rpmbuild/RPMS
	-mkdir -p rpmbuild/SRPMS
	-mkdir -p rpmbuild/SOURCES
	-rm -rf rpmbuild/SOURCES/*
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
