## compiles against the *system* version of libgfapi,
## but not the libgfapi for the testcases

CFLAGS   = -Wall -g $(shell pkg-config --cflags glusterfs-api)
LDFLAGS  = $(shell pkg-config --libs glusterfs-api)

BINARIES = upcall-cache-invalidate libgfapi-fini-hang

%: %.c

all: $(BINARIES)

clean:
        -$(RM) $(BINARIES)

