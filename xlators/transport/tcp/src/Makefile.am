
xlator_PROGRAMS = tcp.so
xlatordir = $(libdir)/glusterfs/xlator/transport

tcp_so_SOURCES = tcp.c

AM_CFLAGS = -fPIC -D_FILE_OFFSET_BITS=64 -D_GNU_SOURCE -Wall \
	-I$(top_srcdir)/libglusterfs/src -shared -nostartfiles

CLEANFILES = *~

	$(MAKE) -C $(dir $@) $(notdir $@)
