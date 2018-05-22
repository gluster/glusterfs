if BUILD_CLOUDSYNC
  CLOUDSYNC_DIR = cloudsync
endif


SUBDIRS = locks quota read-only quiesce marker index barrier \
          arbiter compress changelog changetimerecorder \
          gfid-access $(GLUPY_SUBDIR) upcall snapview-client snapview-server \
          trash shard bit-rot leases selinux sdfs namespace $(CLOUDSYNC_DIR) thin-arbiter \
          utime
CLEANFILES =
