SERVERCFILES := server.c stdrusty.c talloc.c $(wildcard micro/*.c) $(wildcard inter/*.c)
CLIENTCFILES := client.c stdrusty.c talloc.c ping.c $(wildcard micro/*.c) $(wildcard inter/*.c)
CFLAGS := -g -O3 -Wall -Wmissing-prototypes

all: virtbench virtclient

clean:
	rm -f virtbench virtclient

virtbench: $(SERVERCFILES) Makefile
	$(CC) $(CFLAGS) -o $@ $(SERVERCFILES)

virtclient: $(CLIENTCFILES) Makefile
	$(CC) $(CFLAGS) -static -o $@ $(CLIENTCFILES)
