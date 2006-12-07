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

static unsigned int setup(int fd, struct sockaddr *from, socklen_t *fromlen)
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

	send_ack(fd, from, fromlen);
	return wait_for_start(fd);
}

static void do_memburn_linear(int fd, u32 runs,
			      struct sockaddr *from, socklen_t *fromlen,
			      struct benchmark *bench)
{
	unsigned int r, i;

	if (setup(fd, from, fromlen)) {
		for (r = 0; r < runs; r++)
			for (i = 0; i < MEMBURN_SIZE; i+= pagesize)
				mem[i] = i;
		send_ack(fd, from, fromlen);
	}
}

static void do_memburn_random(int fd, u32 runs,
			      struct sockaddr *from, socklen_t *fromlen,
			      struct benchmark *bench)
{
	unsigned int r, i;

	if (setup(fd, from, fromlen)) {
		for (r = 0; r < runs; r++)
			for (i = 0; i < MEMBURN_SIZE; i+= pagesize)
				mem[random() % MEMBURN_SIZE] = i;
		send_ack(fd, from, fromlen);
	}
}

struct benchmark memburn_linear __attribute__((section("benchmarks")))
= { "memburn-linear", fmtstr, do_single_bench, do_memburn_linear };

struct benchmark memburn_random __attribute__((section("benchmarks")))
= { "memburn-random", fmtstr, do_single_bench, do_memburn_random };

