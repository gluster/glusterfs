
sbin_PROGRAMS = glusterfs glusterfsd

glusterfs_SOURCES = glusterfs.c glusterfs-fops.c glusterfs-utils.c
glusterfs_LDADD = /usr/lib/libfuse.a -lpthread

glusterfsd_SOURCES = glusterfsd.c glusterfs-utils.c


AM_CFLAGS = -D_FILE_OFFSET_BITS=64 -DFUSE_USE_VERSION=25 -D_GNU_SOURCE -Wall
