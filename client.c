/* The client which runs inside the virtual machine. */
#include <net/if.h>
#include <err.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <netinet/in.h>
#include <assert.h>
#include <stdlib.h>
#include "benchmarks.h"
#include "stdrusty.h"

struct message
{
	u32 runs;
	char bench[1024]; 	/* And options... */
};

static struct benchmark *find_bench(const char *name)
{
	struct benchmark *b;
	for (b = __start_benchmarks; b < __stop_benchmarks; b++) {
		if (streq(b->name, name))
		    return b;
	}
	errx(1, "Unknown benchmark '%s'", name);
}

bool wait_for_start(int sock)
{
	struct message msg;

	return recv(sock, &msg, sizeof(msg), 0) == 6;
}

void send_ack(int sock, struct sockaddr *from, socklen_t *fromlen)
{
	u32 result = 0;

	sendto(sock, &result, sizeof(result), 0, from, *fromlen);
}

static void dotted_to_addr(struct in_addr *addr, const char *dotted)
{
	unsigned char *addrp;
	char *p, *q;
	int i;
	char buf[20];

	/* copy dotted string, because we need to modify it */
	strncpy(buf, dotted, sizeof(buf) - 1);
	addrp = (unsigned char *) &(addr->s_addr);

	p = buf;
	for (i = 0; i < 3; i++) {
		if ((q = strchr(p, '.')) == NULL)
			errx(1, "badly formed ip address '%s'", dotted);
		addrp[i] = (unsigned char)atoi(p);
		p = q + 1;
	}
	addrp[3] = (unsigned char)atoi(p);
}

static struct in_addr setup_network(const char *devname, const char *addrstr)
{
	struct ifreq ifr;
	int fd;
	struct in_addr addr;
	struct sockaddr_in *sin = (struct sockaddr_in *)&ifr.ifr_addr;

	strcpy(ifr.ifr_name, devname);
	dotted_to_addr(&addr, addrstr);
	sin->sin_family = AF_INET;
	sin->sin_addr = addr;
	fd = socket(PF_INET, SOCK_DGRAM, IPPROTO_IP);
	if (fd < 0)
		err(1, "opening IP socket");
	if (ioctl(fd, SIOCSIFADDR, &ifr) != 0)
		err(1, "Setting interface address");
	ifr.ifr_flags = IFF_UP;
	if (ioctl(fd, SIOCSIFFLAGS, &ifr) != 0)
		err(1, "Bringing interface up");

	return addr;
}

static void __attribute__((noreturn)) usage(void)
{
	errx(1, "Usage: init [ifname ifaddr]\n");
}

int main(int argc, char *argv[])
{
	int sock, len;
	struct sockaddr_in saddr;
	struct message msg;
	struct sockaddr from;
	socklen_t fl;
	struct in_addr addr = { .s_addr = INADDR_ANY };

	if (argc == 2)
		exec_test(argv[1]);

	if (argc != 1 && argc != 3)
		usage();

	if (argc == 3)
		addr = setup_network(argv[1], argv[2]);

	sock = socket(PF_INET, SOCK_DGRAM, 0);
	if (sock < 0)
		err(1, "creating socket");

	saddr.sin_family = AF_INET;
	saddr.sin_port = 6099;
	saddr.sin_addr.s_addr = addr.s_addr;
	if (bind(sock, (struct sockaddr *)&saddr, sizeof(saddr)) != 0)
		err(1, "binding socket");

	while ((len = recvfrom(sock, &msg, sizeof(msg), 0, &from, &fl)) >= 4) {
		struct benchmark *b;
		b = find_bench(msg.bench);
		b->client(sock, msg.runs, &from, &fl, b,
			  msg.bench + strlen(msg.bench) + 1);
	}

	err(1, "reading from socket");
}

/* Dummy for compiling benchmarks. */
u64 do_single_bench(struct benchmark *bench)
{
	assert(0);
	return 0;
}

u64 do_pair_bench(struct benchmark *bench)
{
	assert(0);
	return 0;
}
