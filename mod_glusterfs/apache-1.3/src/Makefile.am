mod_glusterfs_PROGRAMS = mod_glusterfs.so
mod_glusterfsdir = $(libdir)/glusterfs/$(PACKAGE_VERSION)/apache-1.3

mod_glusterfs_so_SOURCES = mod_glusterfs.c

all: mod_glusterfs.so

mod_glusterfs.so: $(top_srcdir)/mod_glusterfs/apache-1.3/src/mod_glusterfs.c $(top_builddir)/libglusterfsclient/src/libglusterfsclient.la
	ln -sf $(top_srcdir)/mod_glusterfs/apache-1.3/src/mod_glusterfs.c $(top_builddir)/mod_glusterfs/apache-1.3/src/mod_glusterfs-build.c
	$(APXS) -c -Wc,-g3 -Wc,-O0 -D_FILE_OFFSET_BITS=64 -D__USE_FILE_OFFSET64 -D_GNU_SOURCE -I$(top_srcdir)/libglusterfsclient/src -L$(top_builddir)/libglusterfsclient/src/.libs/ -lglusterfsclient mod_glusterfs-build.c -o $(top_builddir)/mod_glusterfs/apache-1.3/src/mod_glusterfs.so

$(top_builddir)/libglusterfsclient/src/libglusterfsclient.la:
	make -C $(top_builddir)/libglusterfsclient/src/ all

install-data-local:
	@echo ""
	@echo ""
	@echo "**********************************************************************************"
	@echo "* TO INSTALL MODGLUSTERFS, PLEASE USE,                                            "
	@echo "* $(APXS) -n glusterfs -ia $(mod_glusterfsdir)/mod_glusterfs.so               "
	@echo "**********************************************************************************"
	@echo ""
	@echo ""

#install:
#	cp -fv mod_glusterfs.so $(HTTPD_LIBEXECDIR)
#	cp -fv httpd.conf $(HTTPD_CONF_DIR)

clean:
	rm -fv *.so *.o
