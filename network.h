#ifndef _NETWORK_H
#define _NETWORK_H

#include <stddef.h>
#include <stdint.h>

enum{
	NET_MODE = 0,
	NET_SPED ,
	NET_WIRELESS,
	NET_NAT,
	NET_MAX,
};

enum{
	ADD=1,
	REMOVE,
};
enum{
	MODE_WIRED = 1,
	MODE_WIRELESS = 2,	
};
typedef struct _mode_info{
	uint8_t mode;	/*wireless or wired*/
	char ip[32];	/*local ip address*/
}mode_info;

typedef struct _speed_info{
	uint8_t mode;	/*wireless or wired*/
	char ip[32];	/*internet ip address*/
	char isp[1024];	/*internet service provider*/
	int latency;	/*ping latency*/
	float upspeed;	/*upload speed*/
	float dwspeed;	/*download speed*/
}speed_info;

typedef struct _wireless_info{/*We just record the current ssid/password connect*/
	char SSID[128];
	char password[128];
}wireless_info;
typedef struct _nat_info{
	int nattype;	/*nat type*/
}nat_info;


uint8_t network_init(void);
uint8_t network_onoff(uint8_t type, uint8_t value);
uint8_t network_collect_mode(mode_info *mode);
uint8_t network_collect_speed(speed_info *speed);
uint8_t network_collect_wireless(wireless_info *wireless);
uint8_t network_collect_nattype(nat_info *nat);
	
#endif
