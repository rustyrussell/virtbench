#include <sys/mman.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <err.h>
#include "../benchmarks.h"

static void do_cow(int fd, u32 runs,
		   struct sockaddr *from, socklen_t *fromlen,
		   struct benchmark *bench, const void *opts)
{
	unsigned int i;
	int *maps[runs];
	int pagefd;
	char page[getpagesize()];

	memset(page, 0, sizeof(page));
	if (write(pagefd = open("/tmp/cow_test", O_RDWR|O_CREAT, 0600),
		  page, getpagesize()) != getpagesize())
		err(1, "writing /tmp/cow_test");

	for (i = 0; i < runs; i++) {
		maps[i] = mmap(NULL, getpagesize(), PROT_READ|PROT_WRITE,
			       MAP_PRIVATE, pagefd, 0);
		if (maps[i] == MAP_FAILED)
			err(1, "mapping /tmp/cow_test");
	}

	send_ack(fd, from, fromlen);

	if (wait_for_start(fd)) {
		unsigned int i;

		for (i = 0; i < runs; i++)
			maps[i][0] = 1;
		send_ack(fd, from, fromlen);
	}

	for (i = 0; i < runs; i++)
		munmap(maps[i], getpagesize());

	close(pagefd);
}

struct benchmark cow_benchmark _benchmark_
= { "cow", "Time for one Copy-on-Write fault: %u nsec",
    do_single_bench, do_cow };

