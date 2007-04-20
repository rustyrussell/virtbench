#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdlib.h>
#include <err.h>
#include <stdio.h>
#include "../benchmarks.h"

#define PACKETS 1000

#define HIPQUAD(ip)				\
	((u8)(ip >> 24)),			\
	((u8)(ip >> 16)),			\
	((u8)(ip >> 8)),			\
	((u8)(ip))

static void do_udp_bandwidth_bench(int fd, u32 runs,
				   struct benchmark *bench, const void *opts)
{
	/* We're going to send UDP packets to that addr. */
	struct sockaddr_in saddr;
	int sock, udpsock;
	const struct pair_opt *opt = opts;

	sock = socket(PF_INET, SOCK_STREAM, 0);
	if (sock < 0)
		err(1, "creating socket");
	udpsock = socket(PF_INET, SOCK_DGRAM, 0);
	if (udpsock < 0)
		err(1, "creating UDP socket");

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
		if (setsockopt(udpsock, SOL_SOCKET, SO_REUSEADDR, &set, sizeof(set)) != 0)
			warn("setting SO_REUSEADDR");
		if (bind(udpsock, (struct sockaddr *)&saddr, sizeof(saddr))!=0)
			err(1, "binding UDP socket");

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
		fcntl(sock, F_SETFL, O_NONBLOCK|fcntl(sock, F_GETFL));

		if (connect(udpsock, (struct sockaddr *)&saddr, sizeof(saddr)))
			err(1, "connecting UDP socket");
		send_ack(fd);
	}

	if (wait_for_start(fd)) {
		char packet[1000] = { 0 };
		u32 i = 0;
		if (opt->start) {
			char c;
			for (i = 0; i < runs * PACKETS; i++) {
				if (recv(udpsock, packet, sizeof(packet), 0)
				    != sizeof(packet))
					err(1, "bad read UDP socket");
			}
			send_ack(fd);
			/* Tell other end to stop sending now. */
			write(sock, "1", 1);
			read(sock, &c, 1);
		} else {
			for (i = 0; ; i++) {
				if (send(udpsock, packet, sizeof(packet), 0)
				    != sizeof(packet))
					err(1, "bad write UDP socket");

				/* Occasionally check if we should stop */
				if (i > runs * PACKETS && (i % 32) == 0) {
					char c;
					if (read(sock, &c, 1) == 1)
						break;
				}
			}
			write(sock, "1", 1);
		}
	}
	close(sock);
	close(udpsock);
}

static struct benchmark bandwidth_benchmark _benchmark_
= { "udp-bandwidth",
    "Time to receive " __stringify(PACKETS) " 1k UDPs between guests",
    do_pair_bench_onestop, do_udp_bandwidth_bench };

