#include <sys/syscall.h>
#include <unistd.h>
#include "../benchmarks.h"

static void do_syscall_bench(int fd, u32 runs,
			     struct benchmark *bench, const void *opts)
{
	send_ack(fd);

	if (wait_for_start(fd)) {
		u32 i;
		u32 dummy;
		for (i = 0; i < runs; i++)
			asm volatile("int $0x80"
				     : "=a"(dummy) : "a"(__NR_getppid));
		send_ack(fd);
	}
}

struct benchmark syscall_benchmark _benchmark_
= { "syscall", "Time for one syscall: %u nsec",
    do_single_bench, do_syscall_bench };

