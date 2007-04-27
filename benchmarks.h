#ifndef VIRTBENCH_BENCHMARKS_H
#define VIRTBENCH_BENCHMARKS_H
#include <sys/socket.h>
#include "stdrusty.h"

struct results;
struct benchmark
{
	const char *name;
	const char *pretty_name;
	struct results *(*server)(struct benchmark *bench, bool rough,
				  unsigned int forced_runs);
	void (*client)(int fd, u32 runs,
		       struct benchmark *bench, const void *opts);
	/* If we shouldn't run, return reason. */
	const char *(*should_not_run)(const char *platform,
				      struct benchmark *bench);
} __attribute__((aligned(32))); /* x86-64 mega-aligns this section. Grr... */

struct pair_opt
{
	u32 yourip;
	u32 otherip;
	u32 start;
};

struct results *new_results(void);
void add_result(struct results *, u64 res);
bool results_done(struct results *, unsigned int *runs, bool rough,
		  unsigned int forced_runs);
/* Answers are attached to the "struct results", so needn't be freed */
char *results_to_csv(struct results *);
char *results_to_dist_summary(struct results *);
char *results_to_quick_summary(struct results *);

/* Linker magic defines these */
extern struct benchmark __start_benchmarks[], __stop_benchmarks[];

/* Local (server) side helpers. */
struct results *do_single_bench(struct benchmark *bench, bool rough,
				unsigned int forced_runs);
struct results *do_pair_bench(struct benchmark *bench, bool rough,
				unsigned int forced_runs);
struct results *do_pair_bench_onestop(struct benchmark *bench, bool rough,
				      unsigned int forced_runs);

/* Remote (client) side helpers. */
struct sockaddr;
bool wait_for_start(int sock);
void send_ack(int sock);
void exec_test(char *runstr);

#define _benchmark_ __attribute__((section("benchmarks"), used))

#endif	/* VIRTBENCH_BENCHMARKS_H */
