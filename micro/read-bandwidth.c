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

#define READ_SIZE (256 kB)
static const char pretty_name[] = "Time to read from disk " __stringify(READ_SIZE);
#define kB * 1024

static void do_read_bandwidth(int fd, u32 runs,
			      struct benchmark *bench, const void *opts)
{
	int testfd;
	char *p, *pa;

	p = malloc(READ_SIZE*runs+getpagesize()-1);
	/* O_DIRECT wants an aligned pointer. */
	pa = (void *)(((unsigned long)p+getpagesize()-1) & ~(getpagesize()-1));

	testfd = open(blockdev, O_RDONLY|O_DIRECT);
	if (testfd < 0)
		err(1, "opening %s", blockdev);
	if (read(testfd, pa, READ_SIZE*runs) != READ_SIZE*runs)
		errx(1, "%s too small for %u runs", blockdev, runs);

	send_ack(fd);
	if (wait_for_start(fd)) {
		u32 i;

		for (i = 0; i < runs; i++) {
			int r;
			lseek(testfd, 0, SEEK_SET);
			r = read(testfd, pa, READ_SIZE*runs);
			if (r != READ_SIZE*runs)
				err(1, "reading from %s gave %i", blockdev, r);
		}
		send_ack(fd);
	}
	close(testfd);
	free(p);
}

static struct benchmark read_bandwidth_benchmark _benchmark_
= { "read-bandwidth", pretty_name, do_single_bench, do_read_bandwidth };
