#include <sys/syscall.h>
#include <unistd.h>
#include <sys/socket.h>
#include "benchmarks.h"

/* They use this to see if we're alive yet. */
static void do_ping(int fd, u32 runs,
		    struct sockaddr *from, socklen_t *fromlen,
		    struct benchmark *bench)
{
	send_ack(fd, from, fromlen);
}

struct benchmark ping __attribute__((section("benchmarks")))
= { "ping", "", NULL, do_ping };

