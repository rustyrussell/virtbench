#include <sys/mman.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <err.h>
#include "../benchmarks.h"

static void do_cow(int fd, u32 runs, struct benchmark *bench, const void *opts)
{
	unsigned int i;
	int *maps[runs];
	int pagefd;

	pagefd = open(blockdev, O_RDWR);
	if (pagefd < 0)
		err(1, "opening %s", blockdev);

	for (i = 0; i < runs; i++) {
		maps[i] = mmap(NULL, getpagesize(), PROT_READ|PROT_WRITE,
			       MAP_PRIVATE, pagefd, 0);
		if (maps[i] == MAP_FAILED)
			err(1, "mapping %s", blockdev);
	}

	send_ack(fd);

	if (wait_for_start(fd)) {
		unsigned int i;

		for (i = 0; i < runs; i++)
			maps[i][0] = 1;
		send_ack(fd);
	}

	for (i = 0; i < runs; i++)
		munmap(maps[i], getpagesize());

	close(pagefd);
}

struct benchmark cow_benchmark _benchmark_
= { "cow", "Time for one Copy-on-Write fault", do_single_bench, do_cow };
