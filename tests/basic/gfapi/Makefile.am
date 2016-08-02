## compiles against the *system* version of libgfapi,
## but not the libgfapi for the testcases

CFLAGS   = -Wall -g $(shell pkg-config --cflags glusterfs-api)
LDFLAGS  = $(shell pkg-config --libs glusterfs-api)

BINARIES = upcall-cache-invalidate libgfapi-fini-hang anonymous_fd seek \
	bug1283983 bug1291259 gfapi-ssl-test

%: %.c
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $^

all: check-pkgconfig $(BINARIES)

clean:
	-$(RM) $(BINARIES)

.phony: check-pkgconfig

check-pkgconfig:
	pkg-config --exists glusterfs-api
