#lib_LIBRARIES = mod_glusterfs.so
#mod_glusterfs_so_SOURCES = mod_glusterfs.c

EXTRA_DIST = mod_glusterfs.c

all: mod_glusterfs.so

mod_glusterfs.so: $(top_srcdir)/mod-glusterfs/apache-1.3.9/src/mod_glusterfs.c
	-ln -s $(top_srcdir)/mod-glusterfs/apache-1.3.9/src/mod_glusterfs.c $(top_builddir)/mod-glusterfs/apache-1.3.9/src/mod_glusterfs.c
	$(APXS) -c -Wc,-g3 -Wc,-O0 -D_FILE_OFFSET_BITS=64 -D__USE_FILE_OFFSET64 -D_GNU_SOURCE -I$(top_srcdir)/libglusterfsclient/src -L$(top_builddir)/libglusterfsclient/src/.libs -lglusterfsclient mod_glusterfs.c

install:mod_glusterfs.so
	$(APXS) -i -a -n "glusterfs" mod_glusterfs.so

clean:
	rm -fv *.c *.so *.o