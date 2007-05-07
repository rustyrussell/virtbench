#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <stdlib.h>
#include <err.h>
#include "../benchmarks.h"

static const char pretty_name[] 
= "Time to send " __stringify(NET_BANDWIDTH_SIZE) " from host";
#define MB * 1024 * 1024

#define HIPQUAD(ip)				\
	((u8)(ip >> 24)),			\
	((u8)(ip >> 16)),			\
	((u8)(ip >> 8)),			\
	((u8)(ip))

static void send_data(int fd, const void *mem, unsigned long size)
{
	long ret, done = 0;
	while ((ret = write(fd, mem+done, size-done)) > 0) {
		done += ret;
		if (done == size)
			return;
	}
	if (ret < 0 || size != 0)
		err(1, "writing to other end");
}

static void do_bandwidth_bench(int fd, u32 runs,
			       struct benchmark *bench, const void *opts)
{
	char *mem = malloc(NET_BANDWIDTH_SIZE);

	if (!mem)
		err(1, "allocating %i bytes", NET_BANDWIDTH_SIZE);

	send_ack(fd);
	if (wait_for_start(fd)) {
		u32 i;

		/* We always send 1MB of warmup, to open TCP window. */
		send_data(fd, mem, NET_WARMUP_BYTES);

		for (i = 0; i < runs; i++)
			send_data(fd, mem, NET_BANDWIDTH_SIZE);
		send_ack(fd);
	}
	free(mem);
}

static struct benchmark bandwidth_benchmark _benchmark_
= { "tcp-bandwidth", pretty_name, do_receive_bench, do_bandwidth_bench };

