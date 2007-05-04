NUM_MACHINES:=4

SERVERCFILES := server.c results.c stdrusty.c talloc.c $(wildcard micro/*.c) $(wildcard inter/*.c)
CLIENTCFILES := client.c stdrusty.c talloc.c $(wildcard micro/*.c) $(wildcard inter/*.c)
#CFLAGS := -g -O3 -Wall -Wmissing-prototypes -DNUM_MACHINES=$(NUM_MACHINES)
CFLAGS := -g -Wall -Wmissing-prototypes -DNUM_MACHINES=$(NUM_MACHINES)
INITRD:=initrd.gz

all: virtbench virtclient scratchfile $(INITRD)

include testsuite/Makefile

clean:
	$(RM) virtbench virtclient scratchfile $(INITRD)

distclean: clean
	$(RM) -rf rootfs/mnt

virtbench: $(SERVERCFILES) Makefile $(wildcard *.h)
	$(CC) $(CFLAGS) -o $@ $(SERVERCFILES)

# Can't build static, because then libc won't use sysenter 8(
virtclient: $(CLIENTCFILES) Makefile $(wildcard *.h)
	$(CC) $(CFLAGS) -o $@ $(CLIENTCFILES)

scratchfile:
	dd if=/dev/zero of=$@ bs=1M count=32

$(INITRD): rootfs/mnt
	cd rootfs/mnt && find . | cpio --quiet -L -H newc -o | gzip -9 -n > ../../$@

DEVICES:=/dev/null /dev/zero /dev/console

# This is a tree of symlinks, that way we don't need to be root to create it.
# Unfortunately, still need to create the special devices for xen, lguest, kvm.
rootfs/mnt: $(DEVICES) virtclient
	for f in $(DEVICES) `ldd ./virtclient | sed -e 's/.*=>//' -e 's/(.*//'`; do \
		mkdir -p $@/`dirname $$f` >/dev/null; \
		ln -sf $$f $@/$$f; \
	done
	ln -sf `pwd`/virtclient rootfs/mnt/virtclient
	[ -b $@/dev/lgba ] || sudo mknod $@/dev/lgba b 254 0
	[ -b $@/dev/xvda ] || sudo mknod $@/dev/xvda b 202 0
	[ -b $@/dev/xvda1 ] || sudo mknod $@/dev/xvda1 b 202 1
	[ -b $@/dev/hda ] || sudo mknod $@/dev/hda b 3 0
