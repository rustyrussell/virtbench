#include <unistd.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <stdio.h>
#include <err.h>
#include "../benchmarks.h"

#define MEMBURN_SIZE 64 MB
static const char fmtstr_linear[] 
= "Time to walk linear " __stringify(MEMBURN_SIZE) ": %u nsec";
static const char fmtstr_random[] 
= "Time to walk random " __stringify(MEMBURN_SIZE) ": %u nsec";
#define MB * 1024 * 1024

static char *mem = NULL;
static unsigned int pagesize;

static void setup(void)
{
	unsigned int i;

	/* Alloc once, so we don't thrash. */
	if (!mem) {
		mem = malloc(MEMBURN_SIZE);
		if (!mem)
			err(1, "allocating %i bytes", MEMBURN_SIZE);
		pagesize = getpagesize();
	}

	for (i = 0; i < MEMBURN_SIZE; i+= pagesize)
		mem[i] = i;
}

static void do_memburn_linear(int fd, u32 runs,
			      struct benchmark *bench, const void *opts)
{
	unsigned int r, i;

	setup();

	send_ack(fd);
	if (wait_for_start(fd)) {
		for (r = 0; r < runs; r++)
			for (i = 0; i < MEMBURN_SIZE; i+= pagesize)
				mem[i] = i;
		send_ack(fd);
	}
}

static void do_memburn_random(int fd, u32 runs,
			      struct benchmark *bench, const void *opts)
{
	unsigned int r, i;
	unsigned int random_table[MEMBURN_SIZE / getpagesize()];

	setup();

	for (i = 0; i < ARRAY_SIZE(random_table); i++)
		random_table[i] = i * pagesize;

	for (i = 0; i < ARRAY_SIZE(random_table); i++) {
		unsigned int tmp, r = random() % ARRAY_SIZE(random_table);
		tmp = random_table[i];
		random_table[i] = random_table[r];
		random_table[r] = tmp;
	}

	send_ack(fd);
	if (wait_for_start(fd)) {
		for (r = 0; r < runs; r++)
			for (i = 0; i < ARRAY_SIZE(random_table); i++)
				mem[random_table[i]] = i;
		send_ack(fd);
	}
}

struct benchmark memburn_linear _benchmark_
= { "memburn-linear", fmtstr_linear, do_single_bench, do_memburn_linear };

struct benchmark memburn_random _benchmark_
= { "memburn-random", fmtstr_random, do_single_bench, do_memburn_random };

