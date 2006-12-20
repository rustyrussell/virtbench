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
			      struct benchmark *bench, const void *opts)
{
	/* We're going to send TCP packets to that addr. */
	struct sockaddr_in saddr;
	int sock;
	const struct pair_opt *opt = opts;

	sock = socket(PF_INET, SOCK_STREAM, 0);
	if (sock < 0)
		err(1, "creating socket");

	if (opt->start) {
		/* We accept connection from other client. */
		int listen_sock = sock;
		int set = 1;

		saddr.sin_family = AF_INET;
		saddr.sin_port = htons(6100);
		saddr.sin_addr.s_addr = htonl(opt->yourip);
		if (setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &set, sizeof(set)) != 0)
			warn("setting SO_REUSEADDR");
		if (bind(sock, (struct sockaddr *)&saddr, sizeof(saddr)) != 0)
			err(1, "binding socket");

		if (listen(sock, 0) != 0)
			err(1, "listening on socket");

		send_ack(fd);

		sock = accept(listen_sock, NULL, 0);
		if (sock < 0)
			err(1, "accepting peer connection on socket");
		close(listen_sock);
	} else {
		/* We connect to other client. */
		saddr.sin_family = AF_INET;
		saddr.sin_port = htons(6100);
		saddr.sin_addr.s_addr = htonl(opt->otherip);
		if (connect(sock, (struct sockaddr *)&saddr, sizeof(saddr)))
			err(1, "connecting socket");

		send_ack(fd);
	}

	if (wait_for_start(fd)) {
		u32 i;
		char c = 1;
		for (i = 0; i < runs; i++) {
			if (opt->start) {
				write(sock, &c, 1);
				read(sock, &c, 1);
			} else {
				read(sock, &c, 1);
				write(sock, &c, 1);
			}
		}
		send_ack(fd);
	}
	close(sock);
}

struct benchmark pingpong_benchmark _benchmark_
= { "pingpong", "Time for inter-guest pingpong: %u nsec",
    do_pair_bench, do_pingpong_bench };

