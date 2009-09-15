TOOL_CFLAGS32=-g -O0  -Wall
TOOL_CFLAGS64=-g -O0 -D_GNU_SOURCE -Wall -D_FILE_OFFSET_BITS=64
LIB_CFLAGS=-g -O0 -fPIC -Wall -shared -ldl
%LIB_CFLAGS=-g -O0 -D_GNU_SOURCE -fPIC -Wall -shared -D_FILE_OFFSET_BITS=64 -ldl

all: ld-preload-test32 ld-preload-test64 ld-preload-lib


copy32:
	cp ld-preload-test.c test32.c

ld-preload-test32: copy32
	gcc ${TOOL_CFLAGS32} test32.c -o ld-preload-test32
	rm -f test32.c

copy64:
	cp ld-preload-test.c test64.c

ld-preload-test64: copy64
	gcc ${TOOL_CFLAGS64} test64.c -o ld-preload-test64
	rm -f test64.c

ld-preload-lib: ld-preload-lib.c
	gcc ${LIB_CFLAGS} ld-preload-lib.c -o ld-preload-lib.so

clean:
	rm -f ld-preload-test32 ld-preload-test64 ld-preload-lib.so

