#include <sys/syscall.h>
#include <unistd.h>
#include <sys/socket.h>
#include "benchmarks.h"

/* They use this to see if we're alive yet. */
static void do_ping(int fd, u32 runs, struct benchmark *b, const void *opts)
{
	send_ack(fd);
}

struct benchmark ping __attribute__((section("benchmarks")))
= { "ping", "", NULL, do_ping };

