#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <pthread.h>
#include <netdb.h>
//#include <net/if.h>
#include <arpa/inet.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <asm/types.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <linux/netlink.h>
#include <linux/rtnetlink.h>
#include <linux/route.h>
#include "speed.h"
#include "network.h"
#include "request.h"

#define BUFLEN 20480
#ifdef OPENWRT
#define ETH_NAME		"eth0.2"
#else
#define ETH_NAME		"eth2.2"
#endif
#define APCLI_NAME		"apcli0"

typedef struct _thread_info{
	pthread_t pnet;	/*net monitor thread*/
	pthread_t pspeed;	/*speed test thread*/
}thread_info;

typedef struct _net_context{
	uint8_t quit;
	uint8_t onoff;	/*bit0:mode, bit1:speed bit2:wireless others reserved*/
	uint8_t report_status;	/*bit0:mode report status, 1 need reporte, 0 no need reporte; bit1:speed; bit2:wireless*/
	thread_info thread;
	mode_info mode;	
	speed_info speed;
	wireless_info wireless;
	nat_info nat;
}net_context;

/*mutext lock and condition*/
pthread_mutex_t mlock = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t	cond = PTHREAD_COND_INITIALIZER;
static volatile int wakenum;
static net_context netcont;

#ifdef OPENWRT
/*read /etc/config/network*/
char *str_rtrim_lf(char *str)
{
	int len;

	if(str == NULL){
		return NULL;
	}

	if (strlen(str) == 0)
		return str;
	
	len = strlen(str) - 1;
	/* Kill ' ','\t','\n' */
	while ((str[len] == ' ') || (str[len] == '\t') || (str[len] == '\n')) {
		if (len > 0)
			len--;
		else {
			str[0] = '\0';
			return str;
		}
	}
	str[len+1] = '\0';

	return str;
}

static int spit_openwrt_config_line(char *content, char *option, char *key, char *value)
{
	char *ptr = content, *ptr2;
	char *iter = NULL;
	
	if(!content || !option || 
			!key || !value){
		return 1;
	}

	/*option*/
	iter = strchr(ptr, ' ');
	if(iter == NULL){
		printf("Option Missing.\n");
		return 2;
	}
	strncpy(option, ptr, iter-ptr);
	option[iter-ptr] = '\0';
	/*key*/
	ptr = iter+1;
	iter = strchr(ptr, ' ');
	if(iter == NULL){
		printf("Option Key.\n");
		return 2;
	}
	strncpy(key, ptr, iter-ptr);
	option[iter-ptr] = '\0';	
	/*value*/
	iter++;
	if(*iter == '\''){
		iter++;
		ptr2 = value;
		while(*iter != '\''){
			*ptr2 = *iter;
			ptr2++;
			iter++;
		}
		*ptr2 = '\0';
	}else{
		strcpy(value, iter+1);
	}

	return 0;
}

static int get_wireless_ssid_password(char *ssid, char *password)
{
	FILE *fp;
	char line[512];
	char tssid[64] = {0}, tpwd[64] = {0}, *ptr = NULL;
	int cflag = 0;
	
	if(!ssid || !password){
		return 1;
	}
	
	fp = fopen("/etc/config/network", "r");
	if(fp == NULL){
		printf("Open Wireless Failed:%s\n", strerror(errno));
		return 1;
	}
	while(fgets(line, 511, fp)){
		char option[64] = {0}, key[128] = {0}, value[128] = {0};
		ptr = line;
		while(*ptr == ' ' || *ptr == '\t'){
			ptr++;
		}
		if(*ptr == '\n'){
			continue;
		}
		ptr = str_rtrim_lf(ptr);		
		printf("Strip->%s\n", ptr);

		if(spit_openwrt_config_line(ptr, option, key, value)){
			continue;
		}
		if(!strcmp(option, "config") && !strcmp(key, "interface")){
			if(!strcmp(value, "wan")){
				cflag = 1;
			}else{
				cflag = 0;
			}
			continue;
		}
		if(cflag == 1){
			if(!strcmp(key, "ssid")){
				strcpy(tssid, value);
			}else if(!strcmp(key, "key")){
				strcpy(tpwd, value);
			}
		}
	}
	fclose(fp);

	if(!strlen(tssid)){
		printf("No Found SSID\n");
		return 2;
	}
	strcpy(ssid, tssid);
	strcpy(password, tpwd);
	printf("[%d] SSID:%s! Password:%s!\n", __LINE__, ssid, password);

	return 0;
}

#else
/*just read /etc/Wireless/RT2860/RT2860.dat*/
static int get_wireless_ssid_password(char *ssid, char *password)
{
	FILE *fp;
	char line[512], key[256], value[256];
	char enctype[256] = {0}, tssid[64] = {0}, tpwd[64] = {0};

	if(!ssid || !password){
		return 1;
	}

	fp = fopen("/etc/Wireless/RT2860/RT2860.dat", "r");
	if(fp == NULL){
		printf("Open Wireless Failed:%s\n", strerror(errno));
		return 1;
	}

	while(fgets(line, 511, fp)){
		if(sscanf(line, "%s=%[^\n]", key, value) != 2){
			continue;
		}
		if(!strcmp(key, "ApCliSsid1")){
			printf("Found SSID:%s\n", value);
			strcpy(tssid, value);
		}else if(!strcmp(key, "ApCliEncrypType")){
			printf("Found EncryTpye:%s\n", value);
			strcpy(enctype, value);
		}else if(!strcmp(key, "ApCliWPAPSK")){
			printf("Found Password:%s\n", value);
			strcpy(tpwd, value);
		}
	}
	fclose(fp);

	if(!strlen(tssid) || !strlen(enctype)){
		printf("Wrong:%s-%s-%s!\n", tssid, tpwd, enctype);
		return 1;
	}
	if(!strcasecmp(tpwd, "NONE")){
		/*password is empty*/
		password[0] = '\0';
	}else{
		strcpy(password, tpwd);
	}
	strcpy(ssid, tssid);
	printf("[%d] SSID:%s! Password:%s!\n", __LINE__, ssid, password);

	return 0;
}

#endif
static void handle_network_nattype(int event, int *type)
{
	FILE *fp;
	char line[128] = {0};
#define NAT_FILE	"/tmp/nattype"
	if(event == REMOVE){
		if(!access(NAT_FILE, F_OK)){
			remove(NAT_FILE);
		}
	}else if(event == ADD){
		fp = fopen(NAT_FILE, "r");
		if(fp == NULL){
			printf("Fopen %s failed:%s\n", NAT_FILE, strerror(errno));
			return;
		}
		fgets(line, 127, fp);
		fclose(fp);
		if(type){
			*type = atoi(line);
			printf("Nat Type:%d\n", *type);
		}
	}
}

static int update_network_mode(int event, char *devname, char *ip)
{
	mode_info tmode;
	
	if(!devname ||!ip){
		printf("argument error\n");
		return 1;
	}
	memset(&tmode, 0, sizeof(tmode));
	if(!strcasecmp(devname, APCLI_NAME)){
		/*apclient mode*/
		tmode.mode = MODE_WIRELESS;
		strncpy(tmode.ip, ip, sizeof(tmode.ip)-1);
	}else if(!strcasecmp(devname, ETH_NAME)){
		/*wired mode*/
		tmode.mode = MODE_WIRED;
		strncpy(tmode.ip, ip, sizeof(tmode.ip)-1);		
	}else{
		printf("Ignore DEV:%s\n", devname);
		return 2;
	}
	pthread_mutex_lock(&mlock);
	if(event == ADD){
		if(netcont.mode.mode != tmode.mode||
				strcmp(netcont.mode.ip, tmode.ip)){
			printf("Mode Change:%d-%s\n", tmode.mode, tmode.ip);
			memcpy(&(netcont.mode), &tmode, sizeof(tmode));
			if(tmode.mode == MODE_WIRELESS){
				/*Get wireless ssid password*/
				if(get_wireless_ssid_password(netcont.wireless.SSID, netcont.wireless.password) != 0){
					netcont.report_status &= ~(1 << NET_WIRELESS);
				}else{
					netcont.report_status |= (1 << NET_WIRELESS);
				}
			}
			netcont.report_status |= (1 << NET_MODE);
			/*Need to notify speed thread*/
			if(netcont.onoff &(1<<NET_SPED)){
				wakenum++;
				printf("wakename is %d\n", wakenum);
				pthread_cond_signal(&cond);
			}else{
				printf("Speed test is OFF\n");
			}
			if(netcont.onoff &(1<<NET_NAT)){
				netcont.nat.nattype = -1;
				handle_network_nattype(ADD, &(netcont.nat.nattype));
				if(netcont.nat.nattype != -1){
					netcont.report_status |= (1 << NET_NAT);
				}
			}
		}
	}else if(event == REMOVE){
		/*clear bits*/
		netcont.report_status = 0;
		netcont.mode.mode = 0; /*reset mode*/
		handle_network_nattype(REMOVE, NULL);
	}
	pthread_mutex_unlock(&mlock);
	/*reinit resolve.conf*/
	request_nameserver_reinit();
	
	return 0;
}


#define t_assert(x) { \
	if(!(x))  {printf("%d: %s\n", __LINE__, strerror(errno)); return NULL;} \
}

static void parse_rtattr(struct rtattr **tb, int max, struct rtattr *attr, int len)
{
	for (; RTA_OK(attr, len); attr = RTA_NEXT(attr, len)) {
		if (attr->rta_type <= max) {
			tb[attr->rta_type] = attr;
		}
	}
}

static void print_ifinfomsg(struct nlmsghdr *nh)
{
	int len;
	struct rtattr *tb[IFLA_MAX + 1];
	struct ifinfomsg *ifinfo;
	bzero(tb, sizeof(tb));
	ifinfo = NLMSG_DATA(nh);
	len = nh->nlmsg_len - NLMSG_SPACE(sizeof(*ifinfo));
	parse_rtattr(tb, IFLA_MAX, IFLA_RTA (ifinfo), len);
	printf("%s: %s ", (nh->nlmsg_type==RTM_NEWLINK)?"NEWLINK":"DELLINK", (ifinfo->ifi_flags & IFF_UP) ? "up" : "down");
	if(tb[IFLA_IFNAME]) {
		printf("%s", (char *)(RTA_DATA(tb[IFLA_IFNAME])));
	}
	printf("\n");
}

static void print_ifaddrmsg(struct nlmsghdr *nh)
{
	int len;
	struct rtattr *tb[IFA_MAX + 1];
	struct ifaddrmsg *ifaddr;
	char tmp[256];
	bzero(tb, sizeof(tb));
	ifaddr = NLMSG_DATA(nh);
	len = nh->nlmsg_len - NLMSG_SPACE(sizeof(*ifaddr));
	parse_rtattr(tb, IFA_MAX, IFA_RTA (ifaddr), len);

	printf("%s ", (nh->nlmsg_type==RTM_NEWADDR)?"NEWADDR":"DELADDR");
	if (tb[IFA_LABEL] != NULL) {
		printf("%s ", (char *)(RTA_DATA(tb[IFA_LABEL])));
	}
	if (tb[IFA_ADDRESS] != NULL) {
		inet_ntop(ifaddr->ifa_family, RTA_DATA(tb[IFA_ADDRESS]), tmp, sizeof(tmp));
		printf("%s ", tmp);
	}
	printf("\n");
	/*set status, we just need to handle newaddr event*/
	if(tb[IFA_LABEL] != NULL && tb[IFA_ADDRESS] != NULL){
		update_network_mode((nh->nlmsg_type==RTM_NEWADDR?ADD:REMOVE), RTA_DATA(tb[IFA_LABEL]), tmp);
	}
}

static void print_rtmsg(struct nlmsghdr *nh)
{
	int len;
	struct rtattr *tb[RTA_MAX + 1];
	struct rtmsg *rt;
	char tmp[256];
	bzero(tb, sizeof(tb));
	rt = NLMSG_DATA(nh);
	len = nh->nlmsg_len - NLMSG_SPACE(sizeof(*rt));
	parse_rtattr(tb, RTA_MAX, RTM_RTA(rt), len);
	printf("%s: ", (nh->nlmsg_type==RTM_NEWROUTE)?"NEWROUT":"DELROUT");
	if (tb[RTA_DST] != NULL) {
		inet_ntop(rt->rtm_family, RTA_DATA(tb[RTA_DST]), tmp, sizeof(tmp));
		printf("RTA_DST %s ", tmp);
	}
	if (tb[RTA_SRC] != NULL) {
		inet_ntop(rt->rtm_family, RTA_DATA(tb[RTA_SRC]), tmp, sizeof(tmp));
		printf("RTA_SRC %s ", tmp);
	}
	if (tb[RTA_GATEWAY] != NULL) {
		inet_ntop(rt->rtm_family, RTA_DATA(tb[RTA_GATEWAY]), tmp, sizeof(tmp));
		printf("RTA_GATEWAY %s ", tmp);
	}

	printf("\n");
}

static void *network_monitor(void *arg)
{
	int socket_fd;
	fd_set rd_set;
	struct timeval timeout;
	int select_r;
	int read_r;
	struct sockaddr_nl sa;
	struct nlmsghdr *nh;
	uint8_t *quit = (uint8_t*)arg;

	int len = BUFLEN;
	char buff[2048];

	if(!quit){
		printf("argument null\n");
		return NULL;
	}
	socket_fd = socket(AF_NETLINK, SOCK_RAW, NETLINK_ROUTE);
	t_assert(socket_fd > 0);
	t_assert(!setsockopt(socket_fd, SOL_SOCKET, SO_RCVBUF, &len, sizeof(len)));

	bzero(&sa, sizeof(sa));
	sa.nl_family = AF_NETLINK;
	sa.nl_groups = RTMGRP_LINK | RTMGRP_IPV4_IFADDR | RTMGRP_IPV4_ROUTE | RTMGRP_IPV6_IFADDR | RTMGRP_IPV6_ROUTE;
	t_assert(!bind(socket_fd, (struct sockaddr *) &sa, sizeof(sa)));

	while (!(*quit)) {
		FD_ZERO(&rd_set);
		FD_SET(socket_fd, &rd_set);
		timeout.tv_sec = 5;
		timeout.tv_usec = 0;
		select_r = select(socket_fd + 1, &rd_set, NULL, NULL, &timeout);
		if (select_r < 0) {
			perror("select");
		} else if (select_r > 0) {
			if (FD_ISSET(socket_fd, &rd_set)) {
				read_r = read(socket_fd, buff, sizeof(buff));
				for (nh = (struct nlmsghdr *) buff; NLMSG_OK(nh, read_r); nh = NLMSG_NEXT(nh, read_r)) {
					switch (nh->nlmsg_type) {
					default:
						printf("nh->nlmsg_type = %d\n", nh->nlmsg_type);
						break;
					case NLMSG_DONE:
					case NLMSG_ERROR:
						break;
					case RTM_NEWLINK:
					case RTM_DELLINK:
						print_ifinfomsg(nh);
						break;
					case RTM_NEWADDR:
					case RTM_DELADDR:
						print_ifaddrmsg(nh);
						break;
					case RTM_NEWROUTE:
					case RTM_DELROUTE:
						print_rtmsg(nh);
						break;
					}

				}
			}
		}
	}

	close(socket_fd);
	printf("Net Monitor Thread quit\n");

	return NULL;
}

static void *network_speed(void *arg)
{
	uint8_t *quit = (uint8_t*)arg;
	speed_info vspeed;
	int slpcont = 0, lastmode = 0;
	char lastip[32];

	if(!quit){
		printf("argument null\n");
		return NULL;
	}

	while (!(*quit)) {
		pthread_mutex_lock(&mlock);
		if(!wakenum){
			pthread_cond_wait(&cond, &mlock);
		}
		/*reset wakenum to 0*/
		wakenum = 0;		
		lastmode = netcont.speed.mode;
		strcpy(lastip, netcont.speed.ip);
		pthread_mutex_unlock(&mlock);

		if((*quit)){
			printf("Quit..\n");
			return NULL;
		}
		/*Sleep more than 5 minite, if the network is stable, we begin test*/
		/*if tmpwake-wakenum>1, the prove mode have changed, so the test 
		  result is bad, we need to test again*/		
		for(slpcont = 0; slpcont < 60; slpcont++){
			if((*quit)){
				printf("Quit2..\n");
				return NULL;
			}
			if(wakenum){
				printf("Mode Have Change:%d\n", wakenum);
				goto CLAR_BIT;
			}
			usleep(500000);
		}
		memset(&vspeed, 0, sizeof(vspeed));		
		/*get global ipaddress*/
		if(speed_get_internet_info(vspeed.ip, vspeed.isp) != 0){
			printf("Get global address failed\n");
			goto CLAR_BIT;
		}
		if(lastmode == MODE_WIRED &&
				!strcmp(lastip, vspeed.ip)){
			printf("We just Test wired Mode:%s, so no need to test again\n", lastip);
			/*we dose not clear bit, because the speed may not reported*/
			//goto CLAR_BIT;			
			pthread_mutex_lock(&mlock);
			netcont.report_status |= (1 << NET_SPED);
			printf("Speed Result:\n\tMode:%d\n\tIP:%s\n\tLatency:%dms\n\tDownload:%5.2fMbps\n\tUpload:%5.2fMbps\n",
					netcont.speed.mode, netcont.speed.ip, netcont.speed.latency, netcont.speed.dwspeed, netcont.speed.upspeed);			
			pthread_mutex_unlock(&mlock);
			continue;
		}
		/*test spped*/
		if(speed_test_speed(&(vspeed.latency), &(vspeed.upspeed), &(vspeed.dwspeed)) != 0){
			printf("Test Speed Failed\n");
			goto CLAR_BIT;
		}
		/*test speed successful*/
		pthread_mutex_lock(&mlock);
		memcpy(&(netcont.speed), &vspeed, sizeof(vspeed));
		netcont.speed.mode = netcont.mode.mode;
		netcont.report_status |= (1 << NET_SPED);
		printf("Speed Test Finish:\n\tMode:%d\n\tIP:%s\n\tLatency:%dms\n\tDownload:%5.2fMbps\n\tUpload:%5.2fMbps\n",
				netcont.speed.mode, netcont.speed.ip, netcont.speed.latency, netcont.speed.dwspeed, netcont.speed.upspeed);
		pthread_mutex_unlock(&mlock);
		continue;
		
	CLAR_BIT:
		pthread_mutex_lock(&mlock);
		netcont.report_status &= ~(1 << NET_SPED);
		pthread_mutex_unlock(&mlock);
	}

	printf("Speed Thread quit\n");

	return NULL;
}

static int get_local_ip(const char *eth_if, char *ip)
{
	int fd;
    struct sockaddr_in sin;
    struct ifreq ifr;
	
	if(!eth_if || !ip){
		return 1;
	}
	fd = socket(AF_INET, SOCK_DGRAM, 0);
	if (-1 == fd){
		printf("Socket error: %s\n", strerror(errno));
		return 2;
	}
    strncpy(ifr.ifr_name, eth_if, IFNAMSIZ);
    ifr.ifr_name[IFNAMSIZ - 1] = 0;
    if (ioctl(fd, SIOCGIFADDR, &ifr) < 0){
		printf("[%s]ioctl:%s\n", eth_if, strerror(errno));
		close(fd);
		return 2;
    }

    memcpy(&sin, &ifr.ifr_addr, sizeof(sin));
    snprintf(ip, 16, "%s", inet_ntoa(sin.sin_addr));
    close(fd);
	
    return 0;
}

static void mode_init(void)
{
	char ip[32]= {0};

	if(!get_local_ip(ETH_NAME, ip)){
		printf("[%d]Init Mode:%s\n", __LINE__, ETH_NAME);
		update_network_mode(ADD, ETH_NAME, ip);
	}else if(!get_local_ip(APCLI_NAME, ip)){
		printf("[%d]Init Mode:%s\n", __LINE__, APCLI_NAME);
		update_network_mode(ADD, APCLI_NAME, ip);
	}
}

uint8_t network_init(void)
{
	memset(&netcont, 0, sizeof(netcont));
	/*mode,speed,wireless on default is off*/
	netcont.onoff = 0;
	/*create monitor thread*/
	if(pthread_create(&(netcont.thread.pnet), NULL, network_monitor, &(netcont.quit)) != 0){
		printf("create pthread faild:%s\n", strerror(errno));
		return 1;
	}
	/*create speed thread*/
	if(pthread_create(&(netcont.thread.pspeed), NULL, network_speed, &(netcont.quit)) != 0){
		printf("create pthread faild:%s\n", strerror(errno));
		return 1;
	}
	/*init mode*/
	mode_init();
	
	return 0;
}

uint8_t network_onoff(uint8_t type, uint8_t value)
{
	if(type >= NET_MAX){
		printf("Type Wrong:%d\n", type);
		return 1;
	}
	pthread_mutex_lock(&mlock);
	if(value == 1){
		if(type == NET_SPED && !(netcont.onoff&(1<<NET_SPED))){
			wakenum++;
			printf("[%d]wakename is %d\n", __LINE__, wakenum);
			pthread_cond_signal(&cond);
		}		
		netcont.onoff |= (1 << type);
	}else{
		netcont.onoff &= ~(1 << type);
	}
	pthread_mutex_unlock(&mlock);

	return 0;	
}

uint8_t network_collect_mode(mode_info *mode)
{
	uint8_t onoff = 0, needrep = 0;
	if(!mode){
		return 1;
	}
	pthread_mutex_lock(&mlock);
	onoff = (netcont.onoff & (1 << NET_MODE));
	needrep = (netcont.report_status& (1 << NET_MODE));	
	memcpy(mode, &(netcont.mode), sizeof(netcont.mode));
	/*Reset status to 0*/
	netcont.report_status &= ~(1 << NET_MODE);	
	pthread_mutex_unlock(&mlock);

	if(!onoff || !needrep){
		printf("Mode Report Off[%d:%d]\n", onoff, needrep);
		return 2;
	}

	return 0;
}

uint8_t network_collect_speed(speed_info *speed)
{
	uint8_t onoff = 0, needrep = 0;
	if(!speed){
		return 1;
	}
	pthread_mutex_lock(&mlock);
	onoff = (netcont.onoff & (1 << NET_SPED));
	needrep = (netcont.report_status& (1 << NET_SPED));	
	memcpy(speed, &(netcont.speed), sizeof(netcont.speed));
	/*Reset status to 0*/
	netcont.report_status &= ~(1 << NET_SPED);
	pthread_mutex_unlock(&mlock);

	if(!onoff || !needrep){
		printf("Speed Report Off[%d:%d]\n", onoff, needrep);
		return 2;
	}

	return 0;
}

uint8_t network_collect_wireless(wireless_info *wireless)
{
	uint8_t onoff = 0, needrep = 0;
	if(!wireless){
		return 1;
	}
	pthread_mutex_lock(&mlock);
	onoff = (netcont.onoff & (1 << NET_WIRELESS));
	needrep = (netcont.report_status& (1 << NET_WIRELESS));
	memcpy(wireless, &(netcont.wireless), sizeof(netcont.wireless));
	/*Reset status to 0*/
	netcont.report_status &= ~(1 << NET_WIRELESS);	
	pthread_mutex_unlock(&mlock);

	if(!onoff || !needrep){
		printf("Wireless Report Off[%d:%d]\n", onoff, needrep);
		return 2;
	}

	return 0;
}

uint8_t network_collect_nattype(nat_info *nat)
{
	uint8_t onoff = 0, needrep = 0;
	if(!nat){
		return 1;
	}
	pthread_mutex_lock(&mlock);
	onoff = (netcont.onoff & (1 << NET_NAT));
	needrep = (netcont.report_status& (1 << NET_NAT));
	memcpy(nat, &(netcont.nat), sizeof(netcont.nat));
	/*Reset status to 0*/
	netcont.report_status &= ~(1 << NET_NAT);	
	pthread_mutex_unlock(&mlock);

	if(!onoff || !needrep){
		printf("NAT Report Off[%d:%d]\n", onoff, needrep);
		return 2;
	}

	return 0;
}

