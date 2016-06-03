lib_LTLIBRARIES = libposix2common.la

posix2_common_sources = posix2-common.c

libposix2common_la_SOURCES = $(posix2_common_sources)
libposix2common_la_LIBADD = $(top_builddir)/libglusterfs/src/libglusterfs.la

libposix2common_la_CFLAGS = -Wall $(GF_CFLAGS)

libposix2common_la_CPPFLAGS = $(GF_CPPFLAGS)
libposix2common_la_CPPFLAGS += -I$(top_srcdir)/libglusterfs/src

CLEANFILES =