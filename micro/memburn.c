#include <unistd.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <stdio.h>
#include <err.h>
#include "../benchmarks.h"

#define MEMBURN_SIZE (64 MB)
static const char fmtstr[] = "Time to walk memory " __stringify(MEMBURN_SIZE) ": %u nsec";
#define MB * 1024 * 1024

static char *mem = NULL;
static unsigned int pagesize;

static unsigned int setup(int fd)
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

	send_ack(fd);
	return wait_for_start(fd);
}

static void do_memburn_linear(int fd, u32 runs,
			      struct benchmark *bench, const void *opts)
{
	unsigned int r, i;

	if (setup(fd)) {
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

	if (setup(fd)) {
		for (r = 0; r < runs; r++)
			for (i = 0; i < MEMBURN_SIZE; i+= pagesize)
				mem[random() % MEMBURN_SIZE] = i;
		send_ack(fd);
	}
}

struct benchmark memburn_linear _benchmark_
= { "memburn-linear", fmtstr, do_single_bench, do_memburn_linear };

struct benchmark memburn_random _benchmark_
= { "memburn-random", fmtstr, do_single_bench, do_memburn_random };

