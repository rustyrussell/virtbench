TMP_FILE:=$(shell mktemp)
ROOTFS=virtbench-root

all: $(ROOTFS) domains

$(ROOTFS): Makefile
	# 50M
	dd if=/dev/zero of=$(TMP_FILE) count=50 bs=1048576
	mke2fs -F $(TMP_FILE)
	sudo mount -text2 -o,loop,rw $(TMP_FILE) /mnt
	sudo mkdir /mnt/{tmp,dev}
	sudo mknod /mnt/dev/null    c   1 3
	sudo mknod /mnt/dev/zero    c   1 5
	sudo mknod /mnt/dev/console c   5 1
	# Needed for xen
	sudo mknod /mnt/dev/xvda    b 202 0
	sudo mknod /mnt/dev/xvda1   b 202 1
	sudo umount /mnt
	mv $(TMP_FILE) $@
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
