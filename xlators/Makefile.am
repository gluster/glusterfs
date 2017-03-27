if ENABLE_EXPERIMENTAL
  EXPERIMENTAL = experimental
endif

if BUILD_GNFS
  GNFS_DIR = nfs
endif

DIST_SUBDIRS = cluster storage protocol performance debug features encryption \
          mount nfs mgmt system playground meta experimental

SUBDIRS = cluster storage protocol performance debug features encryption \
          mount ${GNFS_DIR} mgmt system playground meta $(EXPERIMENTAL)

EXTRA_DIST = xlator.sym

CLEANFILES =
