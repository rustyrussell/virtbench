#include <sys/syscall.h>
#include <unistd.h>
#include "../benchmarks.h"

static void do_syscall_bench(int fd, u32 runs,
			     struct benchmark *bench, const void *opts)
{
	send_ack(fd);

	if (wait_for_start(fd)) {
		u32 i;
		u32 dummy = 1;

		for (i = 0; i < runs; i++)
			dummy += getppid();
		/* Avoids GCC optimizing it away, but it won't be true. */
		if (dummy)
			send_ack(fd);
	}
}

struct benchmark libc_syscall_benchmark _benchmark_
= { "libc-syscall", "Time for one syscall via libc: %u nsec",
    do_single_bench, do_syscall_bench };

