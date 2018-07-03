#ifndef _MODULE_H
#define _MODULE_H

#include <stddef.h>
#include <stdint.h>

enum{
	SNUPTIME = 0,
	FWINFO,
	MODE,
	SPEED,
	WIRELESS,
	NAT,
	IDMAX,
};

extern char* modules_list[IDMAX];
void collect_report_init(void);
uint8_t collect_report_info(char **payload, int *paylen);
uint8_t change_report_status(char *item, uint8_t onoff);


#endif
