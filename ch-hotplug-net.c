#include <stdlib.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <net/if.h>
#include <arpa/inet.h>
#include <linux/sockios.h>
#include <errno.h>
#include <sys/un.h>
#include <linux/if_tun.h>
#include <linux/if.h>
#include <linux/virtio_net.h>
#include <sys/ioctl.h>
#include <unistd.h>

#define TUN_DEV "/dev/net/tun"

int main(int argc, char *argv[])
{
	int dev, ret, sock, uds, size;
	unsigned char config[256] = {0};
	unsigned char message[512] = {0};
	struct ifreq ifr;
	struct sockaddr_un sockaddr;
	struct msghdr msg = { 0 };
	struct cmsghdr *cmsg;
	union {         /* Ancillary data buffer, wrapped in a union
                           in order to ensure it is suitably aligned */
		char buf[CMSG_SPACE(sizeof(dev))];
		struct cmsghdr align;
	} u;
	struct iovec io = {
		.iov_base = message,
		.iov_len = sizeof(message)
	};

	if (argc != 3) {
		printf("Usage: <%s> <tap-device-name> <api-socket-path>\n", argv[0]);
		return -1;
	}

	// Open tun;
	dev = open(TUN_DEV,O_RDWR|O_CLOEXEC|O_NONBLOCK);
	if (dev < 0) {
		printf("open device failed: %d, errno %d\n", dev, errno);
		return -1;
	}

	// TUNSETIFF.
	memset(&ifr, 0, sizeof(ifr));
	strcpy(ifr.ifr_name, argv[1]);
	ifr.ifr_flags = IFF_TAP | IFF_NO_PI | IFF_VNET_HDR;
	ret = ioctl(dev, TUNSETIFF, &ifr);
	if (ret < 0) {
		printf("ioctl failed: %d, errno %d\n", ret, errno);
		return -1;
	}

	// TUNSETVNETHDRSZ.
	size = sizeof(struct virtio_net_hdr_v1);
	ret = ioctl(dev, TUNSETVNETHDRSZ, &size);
	if (ret < 0) {
		printf("ioctl failed: %d, errno %d\n", ret, errno);
		return -1;
	}

	// Netlink Socket.
	sock = socket(AF_UNIX, SOCK_DGRAM, 0);
	if (sock < 0) {
		printf("Netlink socket failed: %d, errno %d\n", sock, errno);
		return -1;
	}

	memset(&ifr, 0, sizeof(ifr));
	strcpy(ifr.ifr_name, argv[1]);
	ifr.ifr_flags = IFF_UP|IFF_BROADCAST|IFF_RUNNING|IFF_MULTICAST;
	ret = ioctl(sock, SIOCGIFFLAGS, &ifr);
	if (ret < 0) {
		printf("ioctl SIOCGIFFLAGS failed: %d, errno %d\n", ret, errno);
		return -1;
	}

	ifr.ifr_flags = IFF_UP;
	ret = ioctl(sock, SIOCSIFFLAGS, &ifr);
	if (ret < 0) {
		printf("ioctl SIOCSIFFLAGS failed: %d, errno %d\n", ret, errno);
		return -1;
	}

	uds = socket(AF_UNIX, SOCK_STREAM|SOCK_CLOEXEC, 0);
	if (sock < 0) {
		printf("Unix domain socket failed: %d, errno %d\n", uds, errno);
		return -1;
	}

	sockaddr.sun_family = AF_UNIX;
	strcpy(sockaddr.sun_path, argv[2]);
	ret = connect(uds, (struct sockaddr *) &sockaddr, sizeof(sockaddr));
	if (ret < 0) {
		printf("uds connect failed: %d, errno %d\n", ret, errno);
		return -1;
	}

	//ioctl(uds, FIONBIO, [1]);

	sprintf(config,"{\"ip\":\"192.168.249.1\",\"mask\":\"255.255.255.0\"}");
	sprintf(message, "PUT /api/v1/vm.add-net HTTP/1.1\r\n"
			 "Host: localhost\r\n"
			 "Accept: */*\r\n"
			 "Content-Length: %d\r\n"
			 "\r\n"
			 "%s",
			 strlen(config), config);


	msg.msg_iov = &io;
	msg.msg_iovlen = 1;
	msg.msg_control = u.buf;
	msg.msg_controllen = sizeof(u.buf);
	cmsg = CMSG_FIRSTHDR(&msg);
	cmsg->cmsg_level = SOL_SOCKET;
	cmsg->cmsg_type = SCM_RIGHTS;
	cmsg->cmsg_len = CMSG_LEN(sizeof(dev));
	memcpy(CMSG_DATA(cmsg), &dev, sizeof(dev));

	ret = sendmsg(uds, &msg, 0);
	if (ret <= 0 ) {
		printf("sendmsg failed: %d, errno %d\n", ret, errno);
		return -1;
	}

	memset(message, 0, sizeof(message));
	ret = recvmsg(uds, &msg, 0);
	if (ret <= 0 ) {
		printf("recvmsg failed: %d, errno %d\n", ret, errno);
		return -1;
	}

	printf("Got result: %s\n", message);

	return 0;
}
