#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdlib.h>
#include <err.h>
#include "../benchmarks.h"

#define TESTFILE "/tmp/virtbench-read-latency.test"

#ifndef O_DIRECT
#define O_DIRECT	00040000	/* direct disk access hint */
#endif

static void do_read_latency(int fd, u32 runs,
			    struct sockaddr *from, socklen_t *fromlen,
			    struct benchmark *bench, const void *opts)
{
	int testfd;
	struct stat st;
	char *p, *pa;

	testfd = open(TESTFILE, O_RDONLY|O_DIRECT, 0);
	if (testfd < 0) {
		testfd = open(TESTFILE, O_CREAT|O_EXCL|O_RDWR, 0600);
		if (testfd < 0)
			err(1, "creating " TESTFILE);

		fstat(testfd, &st);
		{
			char crap[st.st_blksize];

			if (write(testfd, crap, sizeof(crap)) != sizeof(crap))
				err(1, "writing to " TESTFILE);
		}
		close(testfd);

		testfd = open(TESTFILE, O_RDONLY|O_DIRECT, 0);
		if (testfd < 0)
			err(1, "opening " TESTFILE " after creation");
	}

	fstat(testfd, &st);
	p = malloc(st.st_blksize*2);
	/* O_DIRECT wants an aligned pointer. */
	pa = (void *)(((unsigned long)p+st.st_blksize-1) & ~(st.st_blksize-1));

	send_ack(fd, from, fromlen);
	if (wait_for_start(fd)) {
		u32 i;

		for (i = 0; i < runs; i++) {
			lseek(testfd, 0, SEEK_SET);
			if (read(testfd, pa, st.st_blksize) != st.st_blksize)
				err(1, "reading from " TESTFILE);
		}
		send_ack(fd, from, fromlen);
	}
	close(testfd);
	free(p);
}

struct benchmark read_latency_benchmark _benchmark_
= { "read-latency", "Time for one disk read: %u nsec",
    do_single_bench, do_read_latency };
