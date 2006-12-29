ROOTFS=virtbench-root

all: $(ROOTFS) domains

$(ROOTFS): Makefile $(ROOTFS).tmp
	@mv $(ROOTFS).tmp $(ROOTFS)

.INTERMEDIATE: $(ROOTFS).tmp

$(ROOTFS).tmp:
	# 50M
	dd if=/dev/zero of=$@ count=50 bs=1048576
	mke2fs -F $@
	# /dev/xvda needed for xen
	set -e; sudo mount -text2 -o,loop,rw $@ /mnt;	\
	 trap 'sudo umount /mnt' 0;			\
	 sudo mkdir /mnt/tmp /mnt/dev;			\
	 sudo mknod /mnt/dev/null    c   1 3;		\
	 sudo mknod /mnt/dev/zero    c   1 5;		\
	 sudo mknod /mnt/dev/console c   5 1;		\
	 sudo mknod /mnt/dev/xvda    b 202 0;		\
	 sudo mknod /mnt/dev/xvda1   b 202 1
	sync

$(ROOTFS)-%: $(ROOTFS)
	cp $< $@

.PHONY: domains
domains: $(ROOTFS)
	@(for i in `seq 0 7` ; do                          \
	      $(MAKE) --no-print-directory $(ROOTFS)-$$i ; \
	  done)
	@sync

clean:
	-@$(RM) $(ROOTFS) $(ROOTFS)-*
