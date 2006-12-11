#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <err.h>
#include "../benchmarks.h"

#define HIPQUAD(ip)				\
	((u8)(ip >> 24)),			\
	((u8)(ip >> 16)),			\
	((u8)(ip >> 8)),			\
	((u8)(ip))

static void do_pingpong_bench(int fd, u32 runs,
			      struct sockaddr *from, socklen_t *fromlen,
			      struct benchmark *bench, const void *opts)
{
	/* We're going to send a UDP to that addr. */
	struct sockaddr_in saddr;
	int sock;
	const struct pair_opt *opt = opts;

	sock = socket(PF_INET, SOCK_DGRAM, 0);
	if (sock < 0)
		err(1, "creating socket");

	saddr.sin_family = AF_INET;
	saddr.sin_port = htons(6099);
	saddr.sin_addr.s_addr = htonl(opt->yourip);
	if (bind(sock, (struct sockaddr *)&saddr, sizeof(saddr)) != 0)
		err(1, "binding socket");

	saddr.sin_addr.s_addr = htonl(opt->otherip);
	if (connect(sock, (struct sockaddr *)&saddr, sizeof(saddr)) != 0)
		err(1, "connecting socket");

	send_ack(fd, from, fromlen);

	if (wait_for_start(fd)) {
		u32 i;
		char c = 1;
		for (i = 0; i < runs; i++) {
			if (opt->start) {
				send(sock, &c, 1, 0);
				recv(sock, &c, 1, 0);
			} else {
				recv(sock, &c, 1, 0);
				send(sock, &c, 1, 0);
			}
		}
		send_ack(fd, from, fromlen);
	}
	close(sock);
}

struct benchmark pingpong_benchmark _benchmark_
= { "pingpong", "Time for inter-guest pingpong: %u nsec",
    do_pair_bench, do_pingpong_bench };

