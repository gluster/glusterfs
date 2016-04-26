scriptsdir = $(datadir)/glusterfs/scripts
scripts_SCRIPTS =  gsync-upgrade.sh generate-gfid-file.sh get-gfid.sh \
	slave-upgrade.sh schedule_georep.py

scripts_PROGRAMS = gsync-sync-gfid
gsync_sync_gfid_CFLAGS = $(GF_CFLAGS) -Wall -I$(top_srcdir)/libglusterfs/src
gsync_sync_gfid_LDFLAGS = $(GF_LDFLAGS)
gsync_sync_gfid_LDADD = $(GF_LDADD) $(top_builddir)/libglusterfs/src/libglusterfs.la
gsync_sync_gfid_SOURCES = gsync-sync-gfid.c
gsync_sync_gfid_CPPFLAGS = $(GF_CPPFLAGS) -I$(top_srcdir)/libglusterfs/src \
	-I$(top_srcdir)/rpc/xdr/src -I$(top_builddir)/rpc/xdr/src

EXTRA_DIST = gsync-sync-gfid.c gsync-upgrade.sh generate-gfid-file.sh \
	get-gfid.sh slave-upgrade.sh schedule_georep.py.in

CLEANFILES = schedule_georep.py
