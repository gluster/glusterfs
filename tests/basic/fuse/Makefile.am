CFLAGS   = -Wall -g
LDFLAGS  =

BINARIES = seek

%: %.c
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $^

all: $(BINARIES)

clean:
	-$(RM) $(BINARIES)
