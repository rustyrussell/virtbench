NUM_MACHINES:=4

SERVERCFILES := server.c results.c stdrusty.c talloc.c $(wildcard micro/*.c) $(wildcard inter/*.c)
CLIENTCFILES := client.c stdrusty.c talloc.c $(wildcard micro/*.c) $(wildcard inter/*.c)
#CFLAGS := -g -O3 -Wall -Wmissing-prototypes -DNUM_MACHINES=$(NUM_MACHINES)
CFLAGS := -g -Wall -Wmissing-prototypes -DNUM_MACHINES=$(NUM_MACHINES)
BASE_ROOTFS:=rootfs/virtbench-root
ROOTFS:=$(foreach N, $(shell seq 0 `expr $(NUM_MACHINES) - 1`), $(BASE_ROOTFS)-$N)

all: virtbench virtclient $(ROOTFS)

include testsuite/Makefile

clean:
	$(RM) virtbench virtclient $(ROOTFS)

distclean: clean
	$(RM) $(BASE_ROOTFS)

virtbench: $(SERVERCFILES) Makefile $(wildcard *.h)
	$(CC) $(CFLAGS) -o $@ $(SERVERCFILES)

# Can't build static, because then libc won't use sysenter 8(
virtclient: $(CLIENTCFILES) Makefile $(wildcard *.h)
	$(CC) $(CFLAGS) -o $@ $(CLIENTCFILES)

$(BASE_ROOTFS): Makefile $(BASE_ROOTFS).tmp
	@mv $(BASE_ROOTFS).tmp $(BASE_ROOTFS)

.INTERMEDIATE: $(BASE_ROOTFS).tmp

$(BASE_ROOTFS).tmp:
	# 50M
	dd if=/dev/zero of=$@ count=50 bs=1048576
	/sbin/mkfs.ext3 -q -F $@
	# /dev/xvda needed for xen
	set -e; sudo mount -text3 -o,loop,rw $@ /mnt;	\
	 trap 'sudo umount /mnt' 0;			\
	 sudo mkdir /mnt/lib /mnt/tmp /mnt/dev /mnt/proc; \
	 sudo mknod /mnt/dev/null    c   1 3;		\
	 sudo mknod /mnt/dev/zero    c   1 5;		\
	 sudo mknod /mnt/dev/console c   5 1;		\
	 sudo mknod /mnt/dev/xvda    b 202 0;		\
	 sudo mknod /mnt/dev/xvda1   b 202 1
	sync

$(ROOTFS): $(BASE_ROOTFS) virtclient
	[ $(BASE_ROOTFS) -ot $@ ] || cp $(BASE_ROOTFS) $@
	set -e; trap 'sudo umount /mnt' 0; \
	 sudo mount -text3 -o,loop,rw $@ /mnt;	\
	 sudo cp virtclient /mnt; \
	 cd /; sudo cp --parents `ldd /mnt/virtclient | sed -e 's/.*=>//' -e 's/(.*//'` /mnt
