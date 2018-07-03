#ifndef _SPEED_H
#define _SPEED_H

#include <stddef.h>
#include <stdint.h>

uint8_t speed_get_internet_info(char *ip, char *isp);
uint8_t speed_test_speed(int *latency, float *upspeed, float *dwspeed);

#endif
