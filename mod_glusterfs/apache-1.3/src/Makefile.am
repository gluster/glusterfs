mod_glusterfs_PROGRAMS = mod_glusterfs.so
mod_glusterfsdir = $(libdir)/glusterfs/$(PACKAGE_VERSION)/apache-1.3

mod_glusterfs_so_SOURCES = mod_glusterfs.c
mod_glusterfs_so_DATA = httpd.conf
mod_glusterfs_sodir = $(sysconfdir)/apache

EXTRA_DIST = mod_glusterfs.c

all: mod_glusterfs.so

mod_glusterfs.so: $(top_srcdir)/mod_glusterfs/apache-1.3/src/mod_glusterfs.c $(top_builddir)/libglusterfsclient/src/libglusterfsclient.la
	[ "$(top_srcdir)" = "$(top_builddir)" ] || ln -sf $(top_srcdir)/mod_glusterfs/apache-1.3/src/mod_glusterfs.c $(top_builddir)/mod_glusterfs/apache-1.3/src/mod_glusterfs.c
	$(APXS) -c -Wc,-g3 -Wc,-O0 -D_FILE_OFFSET_BITS=64 -D__USE_FILE_OFFSET64 -D_GNU_SOURCE -I$(top_srcdir)/libglusterfsclient/src -L$(top_builddir)/libglusterfsclient/src/.libs -lglusterfsclient mod_glusterfs.c

$(top_builddir)/libglusterfsclient/src/libglusterfsclient.la:
	make -C $(top_builddir)/libglusterfsclient/src/ all

httpd.conf:
	`cat /etc/apache/httpd.conf | sed 's,\(.*glusterfs_module[ ]*\).*,\1 $(mod_glusterfsdir)/mod_glusterfs.so,' > httpd.conf` ||  `cat /etc/apache/httpd.conf | sed "`cat /etc/apache/httpd.conf | sed -n "/^LoadModule/ =" | tail -1` a LoadModule glusterfs_module $(mod_glusterfsdir)/mod_glusterfs.so" > httpd.conf` 

clean:
	rm -fv *.c *.so *.o httpd.conf