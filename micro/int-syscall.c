#include <sys/syscall.h>
#include <unistd.h>
#include "../benchmarks.h"

static inline int int_getppid()
{
	int ppid;
	asm volatile("int $0x80" : "=a"(ppid) : "a"(__NR_getppid));
	return ppid;
}

static void do_syscall_bench(int fd, u32 runs,
			     struct benchmark *bench, const void *opts)
{
	send_ack(fd);

	if (wait_for_start(fd)) {
		u32 i;
		u32 dummy = 1;

		for (i = 0; i < runs; i++)
			dummy += int_getppid();
		/* Avoids GCC optimizing it away, but it won't be true. */
		if (dummy)
			send_ack(fd);
	}
}

struct benchmark int_syscall_benchmark _benchmark_
= { "int-syscall", "Time for one int-0x80 syscall",
    do_single_bench, do_syscall_bench };

