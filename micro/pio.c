#include <sys/io.h>
#include <errno.h>
#include <err.h>
#include "../benchmarks.h"

static void do_pio(int fd, u32 runs,
		   struct benchmark *bench, const void *opts)
{
	if (iopl(3) == -1)
		err(errno, "iopl");

	send_ack(fd);

	if (wait_for_start(fd)) {
		u32 i;

		for (i = 0; i < runs; i++)
			outb(0xFF, 0x512);
		send_ack(fd);
	}
}

struct benchmark pio_wait_benchmark _benchmark_
= { "pio", "Time for one outb PIO operation: %u nsec",
    do_single_bench, do_pio };
