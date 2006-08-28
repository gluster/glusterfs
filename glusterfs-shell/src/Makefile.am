bin_PROGRAMS=glusterfs-shell

glusterfs_shell_SOURCES=shell.c primitives.c extensions.c init.c interpreter.c fops.c
glusterfs_shell_LDADD = -lreadline -lncurses ../../libglusterfs/src/libglusterfs.a 

AM_CFLAGS = -fPIC -D_FILE_OFFSET_BITS=64 -DFUSE_USE_VERSION=25 -D_GNU_SOURCE \
	-Wall -I$(top_srcdir)/libglusterfs/src $(GUILE_CLFAGS)
AM_LDFLAGS = $(GUILE_LDFLAGS)

CLEANFILES = *~

../../libglusterfs/src/libglusterfs.a:
	$(MAKE) -C $(dir $@) $(notdir $@)