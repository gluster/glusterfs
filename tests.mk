ALL_TEST_FILES = $(shell find $(top_srcdir)/tests -type f -print)

regressiontestsdir = $(datarootdir)/glusterfs

nobase_dist_regressiontests_DATA = $(ALL_TEST_FILES) run-tests.sh

install-data-hook:
	chmod +x $(DESTDIR)$(datarootdir)/glusterfs/run-tests.sh
