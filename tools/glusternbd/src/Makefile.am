glusternbddir = $(sbindir)

if WITH_SERVER
glusternbd_PROGRAMS = glusternbd
endif

glusternbd_SOURCES = glusternbd.c

glusternbd_LDADD = -lgfapi $(GLIB_LIBS)

glusternbd_LDFLAGS = $(GF_LDFLAGS) $(GLIB_LDFLAGS)

AM_CFLAGS = -Wall $(GF_CFLAGS) $(GLIB_CFLAGS)
