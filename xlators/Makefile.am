if BUILD_GNFS
  GNFS_DIR = nfs
endif

DIST_SUBDIRS = cluster storage protocol performance debug features encryption \
          mount nfs mgmt system playground meta

SUBDIRS = cluster storage protocol performance debug features encryption \
          mount ${GNFS_DIR} mgmt system playground meta

EXTRA_DIST = xlator.sym

CLEANFILES =
