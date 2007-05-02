#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdlib.h>
#include <err.h>
#include "../benchmarks.h"

#ifndef O_DIRECT
#define O_DIRECT	00040000	/* direct disk access hint */
#endif

static void do_read_latency(int fd, u32 runs,
			    struct benchmark *bench, const void *opts)
{
	int testfd;
	struct stat st;
	char *p, *pa;

	testfd = open(blockdev, O_RDONLY|O_DIRECT, 0);
	if (testfd < 0)
		err(1, "opening %s", blockdev);

	fstat(testfd, &st);
	p = malloc(st.st_blksize*2);
	/* O_DIRECT wants an aligned pointer. */
	pa = (void *)(((unsigned long)p+st.st_blksize-1) & ~(st.st_blksize-1));

	send_ack(fd);
	if (wait_for_start(fd)) {
		u32 i;

		for (i = 0; i < runs; i++) {
			lseek(testfd, 0, SEEK_SET);
			if (read(testfd, pa, st.st_blksize) != st.st_blksize)
				err(1, "reading from %s", blockdev);
		}
		send_ack(fd);
	}
	close(testfd);
	free(p);
}

struct benchmark read_latency_benchmark _benchmark_
= { "read-latency", "Time for one disk read",
    do_single_bench, do_read_latency };
