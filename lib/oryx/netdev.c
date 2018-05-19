#include "oryx.h"
#include "netdev.h"


static int is_there_an_eth_iface_named(const char *iface) {
	int skfd = 0;
	struct ifreq ifr;

	skfd = socket(AF_INET, SOCK_DGRAM, 0);
	if(skfd < 0) {
		oryx_loge(-1,
				"%s", strerror(errno));
		return -1;
	}

	strcpy(ifr.ifr_name, iface);

	if(ioctl(skfd, SIOCGIFFLAGS, &ifr) < 0) {
		oryx_loge(-1,
				"%s-%d(\"%s\")", strerror(errno), errno, ifr.ifr_name);
		if (errno == ENODEV) {
			close(skfd);
			skfd = -ENODEV;
	    	return skfd;
		}
		return -1;
	}

	return skfd;
}

int netdev_exist(const char *iface) {
	int skfd = is_there_an_eth_iface_named(iface);
	if(skfd > 0) {
		close (skfd);
		return 1;
	} else {
		if (skfd == 0/** no such device */)
			return 0;
		else
			return -1;
	}
}

int netdev_is_running(const char *iface) {
	int skfd = is_there_an_eth_iface_named(iface);
	struct ifreq ifr;

	if(skfd <= 0)
		return skfd;
	
	strcpy(ifr.ifr_name, iface);

	if(ioctl(skfd, SIOCGIFFLAGS, &ifr) < 0) {
		oryx_loge(-1,
				"%s(\"%s\")", strerror(errno), ifr.ifr_name);
	    close(skfd);
	    return -1;
	}

	close (skfd);

	if(ifr.ifr_flags & IFF_RUNNING) {
	    return 1;
	} else {
	    return 0;
	}
}


int netdev_is_up(const char *iface) {
	int skfd = is_there_an_eth_iface_named(iface);
	struct ifreq ifr;

	if(skfd <= 0)
		return skfd;
	
	strcpy(ifr.ifr_name, iface);

	if(ioctl(skfd, SIOCGIFFLAGS, &ifr) < 0) {
		oryx_loge(-1,
				"%s(\"%s\")", strerror(errno), ifr.ifr_name);
	    close(skfd);
	    return -1;
	}

	close (skfd);
	
	if(ifr.ifr_flags & IFF_UP) {
	    return 1;
	} else {
	    return 0;
	}
}

int netdev_up(const char *iface) {
	int skfd = is_there_an_eth_iface_named(iface);
	struct ifreq ifr;

	if(skfd < 0)
		return skfd;

	strcpy(ifr.ifr_name, iface);
	
	if(ioctl(skfd, SIOCGIFFLAGS, &ifr) < 0) {
		oryx_loge(-1,
				"%s(\"%s\")", strerror(errno), ifr.ifr_name);
	    close(skfd);
	    return -1;
	}

	ifr.ifr_flags |= IFF_UP;

	if(ioctl(skfd, SIOCSIFFLAGS, &ifr) < 0) {
		oryx_loge(-1,
				"%s(\"%s\")", strerror(errno), ifr.ifr_name);
	    close(skfd);
	    return -1;
	}

	close (skfd);
	return 0;
}

int netdev_down(const char *iface) {
	int skfd = is_there_an_eth_iface_named(iface);
	struct ifreq ifr;

	if(skfd < 0)
		return skfd;

	strcpy(ifr.ifr_name, iface);
	
	if(ioctl(skfd, SIOCGIFFLAGS, &ifr) < 0) {
		oryx_loge(-1,
				"%s(\"%s\")", strerror(errno), ifr.ifr_name);
	    close(skfd);
	    return -1;
	}

	ifr.ifr_flags &= ~IFF_UP;

	if(ioctl(skfd, SIOCSIFFLAGS, &ifr) < 0) {
		oryx_loge(-1,
				"%s(\"%s\")", strerror(errno), ifr.ifr_name);
	    close(skfd);
	    return -1;
	}

	close (skfd);
	return 0;
}

static void
netdev_dispatcher(u_char *argument,
		const struct pcap_pkthdr *pkthdr, const u_char *packet)
{
	argument = argument;
	pkthdr = pkthdr;
	packet = packet;
	printf ("defualt dispatch\n");
}

int netdev_open(struct netdev_t *netdev)
{
	netdev->ul_flags = NETDEV_OPEN_LIVE;
	return netdev_pcap_open ((dev_handler_t **)&netdev->handler, 
				netdev->devname, netdev->ul_flags);
}

void *netdev_cap(void *argv)
{
	int64_t rank_acc;
	struct netdev_t *netdev = (struct netdev_t *)argv;
	
	atomic64_set(&netdev->rank, 0);

	netdev_open(netdev);
	
	FOREVER {
		if (!netdev->handler)
			continue;
		
		rank_acc = pcap_dispatch(netdev->handler,
			1024, 
			(netdev->dispatch != NULL) ? netdev->dispatch : netdev_dispatcher, 
			(u_char *)netdev);

		if (rank_acc >= 0) {
			atomic64_add (&netdev->rank, rank_acc);
		} else {
			pcap_close (netdev->handler);
			netdev->handler = NULL;
			switch (rank_acc) {
				case -1:
				case -2:
				case -3:
				default:
					printf("pcap_dispatch=%ld\n", rank_acc);
					break;
			}
		}
	}

	oryx_task_deregistry_id(pthread_self());
}
