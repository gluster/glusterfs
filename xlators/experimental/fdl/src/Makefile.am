xlatordir = $(libdir)/glusterfs/$(PACKAGE_VERSION)/xlator/experimental
xlator_LTLIBRARIES = fdl.la

noinst_HEADERS = jnl-types.h

nodist_fdl_la_SOURCES = fdl.c
fdl_la_LDFLAGS = -module -avoid-version
fdl_la_LIBADD = $(top_builddir)/libglusterfs/src/libglusterfs.la

sbin_PROGRAMS = gf_logdump gf_recon
gf_logdump_SOURCES = logdump.c
nodist_gf_logdump_SOURCES = libfdl.c
gf_logdump_LDADD = $(top_builddir)/libglusterfs/src/libglusterfs.la\
                   $(top_builddir)/api/src/libgfapi.la

# Eventually recon(ciliation) code will move elsewhere, but for now it's
# easier to have it next to the similar logdump code.
gf_recon_SOURCES = recon.c
nodist_gf_recon_SOURCES = librecon.c
gf_recon_LDADD = $(top_builddir)/libglusterfs/src/libglusterfs.la\
                   $(top_builddir)/api/src/libgfapi.la

AM_CPPFLAGS = $(GF_CPPFLAGS) -I$(top_srcdir)/libglusterfs/src \
	      -I$(top_srcdir)/api/src -fPIC \
	      -D_FILE_OFFSET_BITS=64 -D_GNU_SOURCE -D$(GF_HOST_OS) \
	      -DDATADIR=\"$(localstatedir)\"

AM_CFLAGS = -Wall $(GF_CFLAGS)

noinst_PYTHON = gen_fdl.py gen_dumper.py gen_recon.py
EXTRA_DIST = fdl-tmpl.c dump-tmpl.c recon-tmpl.c

CLEANFILES = $(nodist_fdl_la_SOURCES) $(nodist_gf_logdump_SOURCES) \
	$(nodist_gf_recon_SOURCES)

fdl.c: fdl-tmpl.c gen_fdl.py
	$(PYTHON) $(srcdir)/gen_fdl.py $(srcdir)/fdl-tmpl.c > $@

libfdl.c: dump-tmpl.c gen_dumper.py
	$(PYTHON) $(srcdir)/gen_dumper.py $(srcdir)/dump-tmpl.c > $@

librecon.c: recon-tmpl.c gen_recon.py
	$(PYTHON) $(srcdir)/gen_recon.py $(srcdir)/recon-tmpl.c > $@
