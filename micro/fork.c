#include <unistd.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <err.h>
#include "../benchmarks.h"

static void do_fork(int fd, u32 runs,
		    struct benchmark *bench, const void *opts)
{
	send_ack(fd);

	if (wait_for_start(fd)) {
		unsigned int i;

		for (i = 0; i < runs; i++) {
			switch (fork()) {
			case 0:
				exit(0);
			case -1:
				err(1, "forking");
			default:
				wait(NULL);
			}
		}
		send_ack(fd);
	}
}

struct benchmark fork_wait_benchmark _benchmark_
= { "fork", "Time for one fork/exit/wait: %u nsec",
    do_single_bench, do_fork };
