#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <getopt.h>
#include <memory.h>
#include <net/if.h>
#include <sys/socket.h>
#include <linux/rtnetlink.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/un.h>

#define LOG	"/tmp/ifmon.log"
#define PIDFILE	"/var/run/ifmon.pid"
#define SOCKFILE "/var/run/ifmon.socket"

#define TRUE		1
#define FALSE		0

static int background = FALSE;
static int DEBUG = FALSE;
static int wait = 1;

static char *optlog = NULL;
static char *optpid = NULL;
static char *optsock = NULL;
static char sockfile[255];
static char logfile[255];
static char pidfile[255];

static void version(void)
{
	printf ("ifmon version 0.1, 2019\n");
	return;
}

static void help(int err, const char *name)
{
	version();
	printf("Interfaces state monitor via netlink\n");
	printf("\nusage:\n");
	printf("  ifmon [-hvb] [-l <log>] [-n <num>] [-D] [-P <pidfile>] [-S <sockfile>]\n");
	printf("\noptions:\n");
	printf("  -b = Run in background.\n");
	printf("  -l = Path to log file (def: /tmp/ifmon.log).\n");
	printf("  -n = Check every <num> seconds (def: 1).\n");
	printf("  -h = Show this help.\n");
	printf("  -v = Show version.\n");
	printf("  -D = Verbose mode.\n");
	printf("  -P = Pidfile.\n");
	printf("  -S = Sockfile.\n");
	printf("\nexample:\n");
	printf("  ifmon -b -n 2 -l /tmp/ifmon.log\n");
	exit(err);
}


static int parse_cmdline(int argc, char *argv[])
{
	static const char *shortopts = "-hvbDl:n:P:S:";
	static const struct option longopts[] = {
		{"help",	0, NULL, 'h'},
		{"version",	0, NULL, 'v'},
		{"background",	0, NULL, 'b'},
		{"log",		1, NULL, 'l'},
		{"wait",	1, NULL, 'n'},
		{"debug",	0, NULL, 'D'},
		{"pid",		1, NULL, 'P'},
		{"socket",	1, NULL, 'S'},
	};

	while(1) {
		int opt;
		int index;

		opt = getopt_long(argc, argv, shortopts, longopts, &index);
		if (opt == -1 )
			break;
		switch (opt) {
			case 'h': /* help */
				help(0, argv[0]);
				break;
			case 'v': /* version */
				version();
				exit (0);
				break;
			case 'b': /*run to  backgroung */
				background = TRUE;
				break;
			case 'n': /* sleep */
				wait = atoi(optarg);
				break;
			case 'l': /* log */
				optlog = optarg;
				break;
			case 'P': /* pid */
				optpid = optarg;
				break;
			case 'D': /* debug */
				DEBUG = TRUE;
				break;
			default:
				help(1, argv[0]);
				break;
		}
	}
	return 0;
}

static void im_log(char * message, ...)
{
	FILE *f = fopen(logfile,"a+");

	if(f){
		fprintf(f,"[ifmon]: %s\n", message);
		fflush(f);
		fclose(f);
	}
	if (!background)
		printf("[ifmon]: %s\n", message);
}

static int ifmon_init_pid(void) {
	char buf[256];
	int len, fd;
	if (DEBUG)
		im_log ("ifmon_init_pid()...");
	pid_t pid = getpid ();

	if (!access(pidfile, F_OK)) {
		im_log ("Daemon already started");
		return 1;
	}
	if ((fd = creat(pidfile, O_RDWR)) == -1) {
		im_log ("Can not create pidfile");
		return 1;
	}
	bzero (buf, sizeof (buf));
	sprintf (buf, "%d", pid);
	len = strlen (buf);
	if (write (fd, buf, len) != len) {
		im_log ("Can not write pid file");
		close (fd);
		unlink (pidfile);
		return 1;
	}
	close (fd);

	return 0;
}

static int ifmon_cleanup(void) {
	if (DEBUG)
		im_log ("ifmon_cleanup()");
	im_log ("Delete pid file");
	unlink (pidfile);
	unlink(sockfile);
	exit (0);
}

static void ifmon_sig(int sig) {
	switch (sig) {
	case SIGTERM:
		im_log("Get SIGTERM");
		ifmon_cleanup();
		break;
	default:
		break;
	}
}

static int ifmon_init_sig(void) {
	if (signal (SIGTERM, ifmon_sig))
		return 1;
	return 0;
}

static int ifmon_init(void) {
	if (ifmon_init_sig()) {
		im_log ("Signal init failed");
		return 1;
	}

	if (ifmon_init_pid()) {
		im_log ("Pidfile init failed");
		return 1;
	}
	if (DEBUG)
		im_log("Pidfile init ok");
	return 0;
}

void parse_attr(struct rtattr *tb[], int max, struct rtattr *rta, int len)
{
	memset(tb, 0, sizeof(struct rtattr *) * (max + 1));

	while (RTA_OK(rta, len)) {
		if (rta->rta_type <= max) {
			tb[rta->rta_type] = rta;
		}
		rta = RTA_NEXT(rta,len);
	}
}

static int create_unix_socket(char * path)
{
	int sock = -1;

	struct sockaddr_un local;

	local.sun_family = AF_UNIX;
	strncpy(local.sun_path, path, sizeof(local.sun_path));
	local.sun_path[sizeof(local.sun_path)-1] = '\0';
	unlink(path);

	sock = socket(AF_UNIX, SOCK_STREAM, 0);
	if (sock < 0)
		return -1;

	if (bind(sock, (struct sockaddr *)&local, sizeof(local)) < 0) {
		close(sock);
		return -1;
	}

	listen(sock, 5);

	return sock;
}

static int create_netlink_socket(void)
{
	int sock = -1;

	struct sockaddr_nl local;

	memset(&local, 0, sizeof(local));

	local.nl_family = AF_NETLINK;
	local.nl_groups = RTMGRP_IPV4_IFADDR | RTMGRP_LINK;
	local.nl_pid = getpid();

	sock = socket(AF_NETLINK, SOCK_DGRAM, NETLINK_ROUTE);
	if (sock < 0)
		return -1;

	if (bind(sock, (struct sockaddr*)&local, sizeof(local)) < 0) {
		close(sock);
		return -1;
	}

	return sock;
}


static int get_nldata(int fd, char *buf2)
{
	int status;
	struct nlmsghdr *h;
	struct sockaddr_nl nladdr;
	struct iovec iov;
	struct msghdr msg = {
		.msg_name = &nladdr,
		.msg_namelen = sizeof(nladdr),
		.msg_iov = &iov,
		.msg_iovlen = 1,
	};
	char   buf[16384];
	char message[1024];

	memset(&nladdr, 0, sizeof(nladdr));
	nladdr.nl_family = AF_NETLINK;
	nladdr.nl_pid = 0;
	nladdr.nl_groups = 0;

	iov.iov_base = buf;
	while (1) {
		iov.iov_len = sizeof(buf);
		status = recvmsg(fd, &msg, MSG_DONTWAIT);

		if (status < 0) {
			if (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK)
				break;
			fprintf(stderr, "netlink receive error %s (%d)\n",
				strerror(errno), errno);
			return -1;
		}

		if (status == 0) {
			break;
		}

		if (msg.msg_namelen != sizeof(nladdr)) {
			fprintf(stderr, "Sender address length == %d\n", msg.msg_namelen);
			exit(1);
		}
		for (h = (struct nlmsghdr*)buf; status >= sizeof(*h); ) {
			int len = h->nlmsg_len;
			int l = len - sizeof(*h);
			char *ifname;
			char ifflags[1024] = "";
			struct ifinfomsg *ifi;
			struct rtattr *tb[IFLA_MAX + 1];

			if ((l < 0) || (len > status)) {
				break;
			}

			ifi = (struct ifinfomsg*) NLMSG_DATA(h);

			parse_attr(tb, IFLA_MAX, IFLA_RTA(ifi), h->nlmsg_len);

			if (tb[IFLA_IFNAME])
				ifname = (char*)RTA_DATA(tb[IFLA_IFNAME]);

			if (ifi->ifi_flags & IFF_UP)
				snprintf(&ifflags[strlen(ifflags)], sizeof(ifflags), "%s;", "IFF_UP");
			if (ifi->ifi_flags & IFF_RUNNING)
				snprintf(&ifflags[strlen(ifflags)], sizeof(ifflags), "%s;", "IFF_RUNNING");
			if (ifi->ifi_flags & IFF_BROADCAST)
				snprintf(&ifflags[strlen(ifflags)], sizeof(ifflags), "%s;", "IFF_BROADCAST");
			if (ifi->ifi_flags & IFF_POINTOPOINT)
				snprintf(&ifflags[strlen(ifflags)], sizeof(ifflags), "%s;", "IFF_POINTOPOINT");

			ifflags[sizeof(ifflags) - 1] = 0;

			char ifaddr[256];
			struct ifaddrmsg *ifa;
			struct rtattr *tba[IFA_MAX+1];
			ifa = (struct ifaddrmsg*)NLMSG_DATA(h);

			parse_attr(tba, IFA_MAX, IFA_RTA(ifa), h->nlmsg_len);
			if (tba[IFA_LOCAL])
				inet_ntop(AF_INET, RTA_DATA(tba[IFA_LOCAL]), ifaddr, sizeof(ifaddr));

			switch (h->nlmsg_type) {
				case RTM_NEWADDR:
					snprintf(message, sizeof(message), "%s=RTM_NEWADDR:%s\n", ifname, ifaddr);
					message[sizeof(message) - 1] = 0;
					im_log(message);
					break;
				case RTM_DELADDR:
					snprintf(message, sizeof(message), "%s=RTM_DELADDR:\n", ifname);
					message[sizeof(message) - 1] = 0;
					im_log(message);
					break;
				case RTM_NEWLINK:
					snprintf(message, sizeof(message), "%s=RTM_NEWLINK:%s\n", ifname,ifflags);
					message[sizeof(message) - 1] = 0;
					im_log(message);
					break;
				case RTM_DELLINK:
					snprintf(message, sizeof(message), "%s=RTM_DELLINK:\n", ifname);
					message[sizeof(message) - 1] = 0;
					im_log(message);
					break;
				default:
					break;
			}
			status = status - NLMSG_ALIGN(len);
			h = (struct nlmsghdr*)((char*)h + NLMSG_ALIGN(len));
		}
	}
	strcat(buf2, message);
	printf("buf %s\n", buf2);
	return 0;
}
	

static void run_ifmon(char * tolog, char *topid, int sec)
{

	int netlink_sockfd;
	int server_sockfd;

	char buf[8192];
	char message[1024];

	struct sockaddr_un client_address;

	fd_set readfds, testfds;

	snprintf(message,
		sizeof(message),
		"Run 'ifmon -n %d -l %s -P %s -S %s'%s with pid %u",
		sec, tolog, pidfile, sockfile,
		(background ? " as daemon" : ""),
		getpid());

	message[sizeof(message) - 1] = 0;

	netlink_sockfd = create_netlink_socket();
	if (netlink_sockfd < 0) {
		im_log("Error create server netlink socket");
		return;
	}

	server_sockfd = create_unix_socket(sockfile);
	if (server_sockfd < 0) {
		im_log("Error create server unix socket");
		return;
	}

	FD_ZERO(&readfds);
	FD_SET(server_sockfd, &readfds);
	FD_SET(netlink_sockfd, &readfds);

	im_log(message);

	while (TRUE) {

		sleep(wait);

		int fd, fd1;

	     	testfds = readfds;

		int result = select(FD_SETSIZE, &testfds, NULL, NULL, NULL);
	
		if (result < 1) {
			im_log("Error select");
	       		break;
	      	}

		for (fd = 0; fd < FD_SETSIZE; fd++)
			if (FD_ISSET(fd, &testfds)) {
				if (fd == netlink_sockfd){
					char msg[1024] = "\0";
					get_nldata(fd, msg);
					for (fd1 = 0; fd1 < FD_SETSIZE; fd1++)
						if (FD_ISSET(fd1, &readfds)) {
							if (fd1 != netlink_sockfd && fd1 != server_sockfd) {
								snprintf(message, sizeof(message),"send notify to %d", fd1);
								message[sizeof(message) - 1] = 0;
								im_log(message);
								write(fd1, msg, strlen(msg));
							}
						}
				}
				else if (fd == server_sockfd) {
					socklen_t client_len = sizeof(client_address);
					int client_sockfd = accept(server_sockfd,(struct sockaddr*)&client_address, &client_len);
					FD_SET(client_sockfd, &readfds);
					im_log("new client connected");
				} else {
					int  bytes_read = recv(fd, buf, 1024, 0);
         				if (bytes_read <= 0) {
						close(fd);
						FD_CLR(fd, &readfds);
						im_log("client disconnected");
					}
				}
			}


	} //end while
	close(netlink_sockfd);
}



int main(int argc, char **argv)
{
	int pid;
	if ((parse_cmdline(argc, argv)) || (argc == 1)) {
		help(1, argv[0]);
	}

	if (!wait)
		wait = 1; /* default: 1 second */

	if (optlog == NULL)
		/* default: /tmp/ifmon.log */
		strncpy(logfile, LOG, sizeof(logfile));
	else
		strncpy(logfile, optlog, sizeof(logfile));

	if (optpid == NULL)
		/* default: /var/run/ifmon.pid */
		strncpy(pidfile, PIDFILE, sizeof(pidfile));
	else
		strncpy(pidfile, optpid, sizeof(pidfile));

	if (optsock == NULL)
		/* default: /var/run/ifmon.pid */
		strncpy(sockfile, SOCKFILE, sizeof(sockfile));
	else
		strncpy(sockfile, optsock, sizeof(sockfile));


	if (background) {
		if((pid = fork()) < 0) {
			printf("Error: Start ifmon failed (%s)\n", strerror(errno));
			exit(1);
		}
		else if (pid != 0)
			exit(0);
		setsid();
		if (ifmon_init()) {
			ifmon_cleanup();
			exit(1);
		}
	        run_ifmon(logfile, NULL, wait);
	}
	else
		run_ifmon(logfile, NULL, wait);

	return 0;
}
