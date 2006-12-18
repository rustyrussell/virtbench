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
		       struct sockaddr *from, socklen_t *fromlen,
		       struct benchmark *bench, const void *opts);
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
void send_ack(int sock, struct sockaddr *from, socklen_t *fromlen);
void exec_test(char *runstr);

#define _benchmark_ __attribute__((section("benchmarks"), used))

static inline u32 clientip(int dst)
{
	/* 192.168.13.x: you still need to htonl this... */
	return 0xC0A81300 + dst;
}
#endif	/* VIRTBENCH_BENCHMARKS_H */
