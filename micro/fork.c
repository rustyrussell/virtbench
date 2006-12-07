#include <unistd.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/wait.h>
#include "../benchmarks.h"

static void do_syscall_fork(int fd, u32 runs,
			     struct sockaddr *from, socklen_t *fromlen,
			     struct benchmark *bench)
{
	send_ack(fd, from, fromlen);

	if (wait_for_start(fd)) {
		unsigned int i;

		for (i = 0; i < runs; i++) {
			switch (fork()) {
			case 0:
				exit(0);
			case -1:
				exit(1);
			}
		}
		send_ack(fd, from, fromlen);
		for (i = 0; i < runs; i++)
			wait(NULL);
	}
}

struct benchmark fork_benchmark __attribute__((section("benchmarks")))
= { "fork", "Time for one fork: %u nsec",
    do_single_bench, do_syscall_fork };

