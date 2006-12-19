/* The client which runs inside the virtual machine. */
#include <unistd.h>
#include <stdio.h>
#include <net/if.h>
#include <err.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <netinet/in.h>
#include <net/route.h>
#include <sys/types.h>
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

/* Boot parameters can't have . in them, so we accept / too. */
static u32 dotted_to_addr(const char *ipaddr)
{
	unsigned int b[4];

	if (sscanf(ipaddr, "%u.%u.%u.%u", &b[0], &b[1], &b[2], &b[3]) != 4
	    && sscanf(ipaddr, "%u/%u/%u/%u", &b[0], &b[1], &b[2], &b[3]) != 4)
		errx(1, "invalid ip address '%s'", ipaddr);
	return htonl((b[0]<<24) | (b[1]<<16) | (b[2]<<8) | b[3]);
}

static struct in_addr setup_network(const char *devname, const char *addrstr)
{
	struct ifreq ifr;
	int fd;
	struct in_addr addr;
	struct sockaddr_in *sin = (struct sockaddr_in *)&ifr.ifr_addr;

	strcpy(ifr.ifr_name, devname);
	addr.s_addr = dotted_to_addr(addrstr);
	sin->sin_family = AF_INET;
	sin->sin_addr = addr;
	fd = socket(PF_INET, SOCK_DGRAM, IPPROTO_IP);
	if (fd < 0)
		err(1, "opening IP socket");
	if (ioctl(fd, SIOCSIFADDR, &ifr) != 0)
		err(1, "Setting interface address for %s", devname);
	ifr.ifr_flags = IFF_UP;
	if (ioctl(fd, SIOCSIFFLAGS, &ifr) != 0)
		err(1, "Bringing interface %s up", devname);
	close(fd);

	return addr;
}

static void add_default_route(const char *devname)
{
	struct rtentry rt;
	struct sockaddr_in *sin;
	int fd;

	fd = socket(PF_INET, SOCK_DGRAM, IPPROTO_IP);
	if (fd < 0)
		err(1, "opening IP socket");

	memset(&rt, 0, sizeof(rt));
	sin = (struct sockaddr_in *)&rt.rt_dst;
	sin->sin_family = AF_INET;
	sin->sin_addr.s_addr = INADDR_ANY;
	rt.rt_gateway = rt.rt_genmask = rt.rt_dst;
	rt.rt_dev = (char *)devname;
	rt.rt_flags = 0;
	if (ioctl(fd, SIOCADDRT, &rt) != 0)
		err(1, "adding route");
	close(fd);
}

static void remove_base_route(const char *devname, u32 devaddr)
{
	struct rtentry rt;
	struct sockaddr_in *sin;
	int fd;

	fd = socket(PF_INET, SOCK_DGRAM, IPPROTO_IP);
	if (fd < 0)
		err(1, "opening IP socket");

	memset(&rt, 0, sizeof(rt));
	sin = (struct sockaddr_in *)&rt.rt_dst;
	sin->sin_family = AF_INET;
	sin->sin_addr.s_addr = (devaddr & htonl(0xFFFFFF00));

	sin = (struct sockaddr_in *)&rt.rt_genmask;
	sin->sin_family = AF_INET;
	sin->sin_addr.s_addr = htonl(0xFFFFFF00);

	sin = (struct sockaddr_in *)&rt.rt_gateway;
	sin->sin_family = AF_INET;
	sin->sin_addr.s_addr = INADDR_ANY;

	rt.rt_dev = (char *)devname;
	rt.rt_flags = 0;
	if (ioctl(fd, SIOCDELRT, &rt) != 0)
		err(1, "deleting route");
	close(fd);
}

static void __attribute__((noreturn)) usage(void)
{
	errx(1, "Usage: init extifname ifaddr [intifname]\n");
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

	if (argc != 3 && argc != 4)
		usage();

	if (argc >= 3) {
		addr = setup_network(argv[1], argv[2]);
		add_default_route(argv[1]);

		if (argc == 4) {
			setup_network(argv[3], argv[2]);
			/* We don't want local traffic out external iface */
			remove_base_route(argv[1], addr.s_addr);
		}
	}

	sock = socket(PF_INET, SOCK_DGRAM, 0);
	if (sock < 0)
		err(1, "creating socket");

	saddr.sin_family = AF_INET;
	saddr.sin_port = 6099;
	saddr.sin_addr.s_addr = addr.s_addr;
	if (bind(sock, (struct sockaddr *)&saddr, sizeof(saddr)) != 0)
		err(1, "binding socket");

	printf("virtclient ready\n");
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
