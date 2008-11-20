mod_glusterfs_PROGRAMS = mod_glusterfs.so
mod_glusterfsdir = $(libdir)/glusterfs/$(PACKAGE_VERSION)/apache/2.2

mod_glusterfs_so_SOURCES = mod_glusterfs.c

all: mod_glusterfs.so

mod_glusterfs.so: $(top_srcdir)/mod_glusterfs/apache/2.2/src/mod_glusterfs.c $(top_builddir)/libglusterfsclient/src/libglusterfsclient.la
	ln -sf $(top_srcdir)/mod_glusterfs/apache/2.2/src/mod_glusterfs.c $(top_builddir)/mod_glusterfs/apache/2.2/src/mod_glusterfs-build.c
	$(APXS) -c -o mod_glusterfs.la -Wc,-g3 -Wc,-O0 -DLINUX=2 -D_REENTRANT -D_GNU_SOURCE -D_LARGEFILE64_SOURCE -D_FILE_OFFSET_BITS=64 -D__USE_FILE_OFFSET64 -I$(top_srcdir)/libglusterfsclient/src -L$(top_builddir)/libglusterfsclient/src/.libs/ -lglusterfsclient mod_glusterfs-build.c 
	-ln -sf .libs/mod_glusterfs.so mod_glusterfs.so

$(top_builddir)/libglusterfsclient/src/libglusterfsclient.la:
	make -C $(top_builddir)/libglusterfsclient/src/ all

install-data-local:
	@echo ""
	@echo ""
	@echo "**********************************************************************************"
	@echo "* TO INSTALL MODGLUSTERFS, PLEASE USE,                                            "
	@echo "* $(APXS) -n glusterfs -ia $(mod_glusterfsdir)/mod_glusterfs.so                "
	@echo "**********************************************************************************"
	@echo ""
	@echo ""

#install:
#	cp -fv mod_glusterfs.so $(HTTPD_LIBEXECDIR)
#	cp -fv httpd.conf $(HTTPD_CONF_DIR)

clean:
	rm -fv *.so *.o
