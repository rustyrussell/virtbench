#include <sys/types.h>
#include <signal.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdlib.h>
#include <err.h>
#include "../benchmarks.h"

static void do_context_switch(int fd, u32 runs,
			      struct sockaddr *from, socklen_t *fromlen,
			      struct benchmark *bench, const void *opts)
{
	char c = 1;
	int fds1[2], fds2[2], child;

	if (pipe(fds1) != 0 || pipe(fds2) != 0)
		err(1, "Creating pipe");

	child = fork();
	if (child == -1)
		err(1, "forking");

	if (child > 0) {
		close(fds1[0]);
		close(fds2[1]);
		send_ack(fd, from, fromlen);

		if (wait_for_start(fd)) {
			while ((int)runs > 0) {
				write(fds1[1], &c, 1);
				read(fds2[0], &c, 1);
				runs -= 2;
			}
			if (runs == 0)
				send_ack(fd, from, fromlen);
		} else
			kill(child, SIGTERM);
		waitpid(child, NULL, 0);
		close(fds1[1]);
		close(fds2[0]);
	} else {
		close(fds2[0]);
		close(fds1[1]);

		while ((int)runs > 0) {
			read(fds1[0], &c, 1);
			write(fds2[1], &c, 1);
			runs -= 2;
		}
		if ((int)runs == -1)
			send_ack(fd, from, fromlen);
		exit(0);
	}
}

struct benchmark context_swtch_benchmark _benchmark_
= { "context-switch", "Time for one context switch via pipe: %u nsec",
    do_single_bench, do_context_switch };

