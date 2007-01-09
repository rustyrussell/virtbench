#ifndef VIRTBENCH_BENCHMARKS_H
#define VIRTBENCH_BENCHMARKS_H
#include <sys/socket.h>
#include "stdrusty.h"

struct benchmark
{
	const char *name;
	const char *format;
	u64 (*server)(struct benchmark *bench);
	void (*client)(int fd, u32 runs,
		       struct benchmark *bench, const void *opts);
	/* If we shouldn't run, return reason. */
	const char *(*should_not_run)(const char *platform,
				      struct benchmark *bench);
};

struct pair_opt
{
	u32 yourip;
	u32 otherip;
	u32 start;
};

/* Linker magic defines these */
extern struct benchmark __start_benchmarks[], __stop_benchmarks[];

/* Local (server) side helpers. */
u64 do_single_bench(struct benchmark *bench);
u64 do_pair_bench(struct benchmark *bench);

/* Remote (client) side helpers. */
struct sockaddr;
bool wait_for_start(int sock);
void send_ack(int sock);
void exec_test(char *runstr);

#define _benchmark_ __attribute__((section("benchmarks"), used))

#endif	/* VIRTBENCH_BENCHMARKS_H */
