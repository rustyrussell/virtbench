#ifndef VIRTBENCH_BENCHMARKS_H
#define VIRTBENCH_BENCHMARKS_H
#include "stdrusty.h"

struct benchmark
{
	const char *name;
	const char *format;
	u64 (*local)(struct benchmark *bench);
	void (*remote)(int fd, u32 runs,
		       struct sockaddr *from, socklen_t *fromlen,
		       struct benchmark *bench);
};

/* Linker magic defines these */
extern struct benchmark __start_benchmarks[], __stop_benchmarks[];

/* Local (server) side helpers. */
u64 do_single_bench(struct benchmark *bench);

/* Remote (client) side helpers. */
struct sockaddr;
bool wait_for_start(int sock);
void send_ack(int sock, struct sockaddr *from, socklen_t *fromlen);

#endif	/* VIRTBENCH_BENCHMARKS_H */
