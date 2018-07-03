#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#include "module.h"
#include "network.h"

#define JOINER	"&"	/*Used this to join parts report info*/
typedef enum{
	EVERYTIME = 1,	/*report everytime*/
	ONCE,			/*report once*/
	CHANGED,		/*report status changed*/
}REPTYPE;

typedef enum{
	MFALSE = 0,
	MTRUE = 1,
}BOOL;

enum{
	EREP_OK = 0,
	EREP_ARG,
	EREP_NONEED,
	EREP_SYS,
};

typedef struct _module_t{
	uint8_t moudle_id;			/*module id*/
	char module_name[64];		/*module name*/
	char module_descript[256];		/*module description*/
	BOOL report_on;				/*report on/off*/
	REPTYPE report_freq;		/*report frequency*/
	uint8_t (*report_func)(uint8_t, char *, int*);/*report function->id/payload/len*/
	void (*on_off)(uint8_t, BOOL); /*on/off report->id/on*/
}module_t;

#define REP_BASE_CHECK(x)	((x) > IDMAX || modules[(x)].report_on== MFALSE)
static uint8_t report_sn_uptime(uint8_t id, char *payload, int* len);
static uint8_t report_version_product(uint8_t id, char *payload, int* len);
static uint8_t report_mode(uint8_t id, char *payload, int* len);
static uint8_t report_download_upload_speed(uint8_t id, char *payload, int* len);
static uint8_t report_ssid_password(uint8_t id, char *payload, int* len);
static uint8_t report_nat_type(uint8_t id, char *payload, int* len);
static void common_on_off_func(uint8_t id, BOOL on);
static void special_on_off_func(uint8_t id, BOOL on);

char* modules_list[IDMAX] = {"uptime", "fwinfo", "mode", "speed", "wireless", "nat"};
static module_t modules[IDMAX] = {
	{SNUPTIME, "", "system uptime and sn report", MTRUE, EVERYTIME, report_sn_uptime, common_on_off_func},
	{FWINFO, "", "firmware version and product report", MTRUE, ONCE, report_version_product, common_on_off_func},
	{MODE, "", "mode change report", MTRUE, CHANGED, report_mode, special_on_off_func},
	{SPEED, "", "download upload speed report", MTRUE, CHANGED, report_download_upload_speed, special_on_off_func},
	{WIRELESS, "", "ssid password report", MTRUE, CHANGED, report_ssid_password, special_on_off_func},
	{NAT, "", "nat type report", MTRUE, CHANGED, report_nat_type, special_on_off_func},	
};

static void common_on_off_func(uint8_t id, BOOL on)
{
	if(id > IDMAX){
		printf("ID Error:%d\n", id);
		return;
	}
	modules[id].report_on= on;
}

static void special_on_off_func(uint8_t id, BOOL on)
{
	uint8_t type = 0;
	if(id > IDMAX){
		printf("ID Error:%d\n", id);
		return;
	}
	modules[id].report_on= on;
	if(modules[id].moudle_id == MODE){
		type = NET_MODE;
	}else if(modules[id].moudle_id == SPEED){
		type = NET_SPED;
	}else if(modules[id].moudle_id == WIRELESS){
		type = NET_WIRELESS;
	}else if(modules[id].moudle_id == NAT){
		type = NET_NAT;
	}else{
		type = NET_MAX;
	}

	network_onoff(type, on);
}

int str_encode_xmlurl(const char *src, int src_len, char *dst,int dst_len)
{
	char *encoded;
	unsigned char c;
	int  i,j = 0;
	encoded = dst;

	if(src == NULL){
		printf("error1\n");
		return 1;
	}
	if(dst == NULL){
		printf("error2\n");
		return 1;
	}
	if(src_len >= dst_len -1){
		printf("error3\n");
		dst[0] = '\0';
		return 1;
	}
	for(i=0; i < src_len; i++){
		c = src[i];
		if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
			(c >= '0' && c <= '9') ||c == '.' || c == '-' || c == '_' || c == '/' ){
			// allowed characters in a url that have non special meaning
			encoded[j] = c;
			j++;
			continue;
		}
		dst_len = dst_len - 2;
		if(src_len >= dst_len -1){
			encoded[0] = '\0';
			printf("error4\n");
			return 1;
		}else{
			j += sprintf(&encoded[j], "%%%x", c);
		}
	}
	encoded[j] = '\0';
	return 0;
}

static int flash_read(const char *mtddev, int start, char *buf, int buflen)
{
	int fd = -1;
	if ((fd = open(mtddev, O_RDONLY)) == -1) {
		printf("Cann't open mtd device: %s", mtddev);
		return 1;
	}
	/* Write the mtdblock device  */
	if (lseek(fd, start, SEEK_SET) == -1) {
		printf("Cann't lseek(%d)", start);
		close(fd);
		return 1;
	}
	/* Read data from mtdblock    */
	if (read(fd, buf, buflen - 1) != (buflen - 1)) {
		printf("Cann't Read(%s)", strerror(errno));
		close(fd);
		return 1;
	}else{
		buf[buflen - 1] = '\0'; /* prevent over-read */
	}

	close(fd);
	return 0;
}

#ifdef OPENWRT
static int get_devsn(char *getsn, int len)
{
	char str_sn[33]= {0};

	/*get wifi interface last 4 bit mac from flash,*/
	if(flash_read("/dev/mtd2", 0x43c, str_sn, 32) != 0){
		return 1;
	}
	
	memcpy(getsn, str_sn, len);
	//printf("SN:%s\n", getsn);
	
	return 0;
}
#else
static int get_devsn(char *getsn, int len)
{
	char str_sn[33]= {0};

	/*get wifi interface last 4 bit mac from flash,*/
	if(flash_read("/dev/mtd5", 0x43c, str_sn, 32) != 0){
		return 1;
	}
	
	memcpy(getsn, str_sn, len);
	//printf("SN:%s\n", getsn);
	
	return 0;
}

#endif

static uint8_t report_sn_uptime(uint8_t id, char *payload, int* len)
{
	FILE *fp;
	char line[256] = {0};
	char uptime[128] ={0}, idtime[128] = {0};
	char sn[64] = {0}, *ptr = NULL;
	
	if(!payload || !len){
		return EREP_ARG;
	}
	if(REP_BASE_CHECK(id)){
		printf("No Need Report SNUPTIME:%d\n", id);
		*len = 0;
		return EREP_NONEED;
	}

	/*get uptime*/
	fp = fopen("/proc/uptime", "r");
	if(!fp){
		printf("Open uptime Failed:%s\n", strerror(errno));
		*len = 0;
		return EREP_SYS;
	}
	fgets(line, 255, fp);
	fclose(fp);
	if(sscanf(line, "%s %[^\n]", uptime, idtime) != 2){
		printf("[%d]Sscanf failed\n", __LINE__);
		*len = 0;
		return EREP_SYS;
	}
	/*SHIT:server need int type, not float*/	
	if((ptr = strchr(uptime, '.')) != NULL){
		*ptr = '\0';
	}
//	printf("uptime:%s idle time:%s\n", uptime, idtime);
	/*get sn*/
	if(get_devsn(sn, 63) != 0){
		*len = 0;		
		printf("[%d]Get SN Failed\n", __LINE__);
		return EREP_SYS;
	}
	int plen = *len;
	*len = snprintf(payload, plen, "sn=%s%suptime=%s", sn, JOINER, uptime);
	payload[*len] = '\0';	
	printf("UPTIME Content:%s\n", payload);

	return EREP_OK;
}

static uint8_t report_version_product(uint8_t id, char *payload, int* len)
{
	FILE *fp;
	char line[256];
	char key[128], value[128];
	char version[64]= {0}, product[64] = {0};
	int cur = 0, j = 0;
	
	static BOOL have_report = MFALSE;
	if(!payload || !len){
		return EREP_ARG;
	}
	if(have_report == MTRUE ||
			REP_BASE_CHECK(id)){
		printf("No Need Report FWINFO:%d->%d\n", id, have_report);
		*len = 0;
		return EREP_NONEED;
	}
#ifdef OPENWRT
	fp = fopen("/etc/system.conf", "r");
#else
	fp = fopen("/etc/firmware", "r");
#endif

	if(fp == NULL){
		printf("fopen failed:%s\n", strerror(errno));
		*len = 0;
		return EREP_SYS;
	}

	while(fgets(line, 255, fp)){
		if(sscanf(line, "%[^=]=%[^\n]", key, value) != 2){
			continue;
		}
		if(!strcasecmp(key, "CURVER")){
			/*drop dot*/
			for(cur = 0, j = 0; cur < strlen(value); cur++){
				if(value[cur] == '.'){
					continue;
				}
				version[j++] = value[cur];
			}
		}
	#ifdef OPENWRT		
		else if(!strcasecmp(key, "product")){
			strcpy(product, value);
		}
	#else
		else if(!strcasecmp(key, "CURFILE")){
			strcpy(product, value);
		}
	#endif
	}
	fclose(fp);
	if(!strlen(version) || !strlen(product)){
		printf("Get version or product failed:%s-%s!\n", version, product);
		return EREP_SYS;
	}
	
	int plen = *len;
	*len = snprintf(payload, plen, "fwversion=%s%stype=%s", version, JOINER, product);
	payload[*len] = '\0';
	printf("BaseInfo Content:%s\n", payload);
	//have_report = MTRUE;

	return EREP_OK;
}

static uint8_t report_mode(uint8_t id, char *payload, int* len)
{
	if(!payload || !len){
		return EREP_ARG;
	}
	if(REP_BASE_CHECK(id)){
		printf("No Need Report MODE:%d\n", id);
		*len = 0;
		return EREP_NONEED;
	}
	mode_info mode;
	int plen = *len;
	if(network_collect_mode(&mode)){
		/*Get Info failed or no need to report*/
		*len = 0;
		return EREP_NONEED;
	}
	*len = snprintf(payload, plen, "mode=%d", mode.mode);
	payload[*len] = '\0';	
	printf("Mode Content:%s\n", payload);
	
	return EREP_OK;
}

static uint8_t report_download_upload_speed(uint8_t id, char *payload, int* len)
{
	if(!payload || !len){
		return EREP_ARG;
	}
	if(REP_BASE_CHECK(id)){
		printf("No Need Report MODE:%d\n", id);
		*len = 0;
		return EREP_NONEED;
	}
	int plen = *len;
	speed_info speed;
	if(network_collect_speed(&speed)){
		/*Get Info failed or no need to report*/
		*len = 0;
		return EREP_NONEED;
	}
	*len = snprintf(payload, plen, "upspeed=%.2f%sdownspeed=%.2f", 
				speed.upspeed, JOINER, speed.dwspeed);
	payload[*len] = '\0';	
	printf("Speed Content:%s\n", payload);
	
	return EREP_OK;
}

static uint8_t report_ssid_password(uint8_t id, char *payload, int* len)
{
	if(!payload || !len){
		return EREP_ARG;
	}
	if(REP_BASE_CHECK(id)){
		printf("No Need Report MODE:%d\n", id);
		*len = 0;
		return EREP_NONEED;
	}
	int plen = *len;
	wireless_info wireless;
	if(network_collect_wireless(&wireless)){
		/*Get Info failed or no need to report*/
		*len = 0;
		return EREP_NONEED;
	}
	char encssid[512] = {0}, encpwd[512] = {0};
	if(str_encode_xmlurl(wireless.SSID, strlen(wireless.SSID),
			encssid, 511) != 0){
		strcpy(encssid, wireless.SSID);
	}
	if(str_encode_xmlurl(wireless.password, strlen(wireless.password),
			encpwd, 511) != 0){
		strcpy(encpwd, wireless.password);
	}	
	*len = snprintf(payload, plen, "Ssid=%s%sPwd=%s", 
				encssid, JOINER, encpwd);
	payload[*len] = '\0';	
	printf("Wireless Content:%s\n", payload);
	
	return EREP_OK;
}

static uint8_t report_nat_type(uint8_t id, char *payload, int* len)
{
	if(!payload || !len){
		return EREP_ARG;
	}
	if(REP_BASE_CHECK(id)){
		printf("No Need Report MODE:%d\n", id);
		*len = 0;
		return EREP_NONEED;
	}
	int plen = *len;
	nat_info nat;
	if(network_collect_nattype(&nat)){
		/*Get Info failed or no need to report*/
		*len = 0;
		return EREP_NONEED;
	}

	*len = snprintf(payload, plen, "nattype=%d", nat.nattype);
	payload[*len] = '\0';	
	printf("NAT Content:%s\n", payload);

	return EREP_OK;
}

/*******************************************************************/
void collect_report_init(void)
{
	int j;

	/*reinit module name*/
	for(j = 0; j < IDMAX; j++){
		strcpy(modules[j].module_name, modules_list[modules[j].moudle_id]);
	}
	printf("Module==>");
	for(j = 0; j < IDMAX; j++){
		printf("%s ", modules[j].module_name);
	}
	printf("\n");
	network_init();
}
/*Dose not forget free buffer*/
uint8_t collect_report_info(char **payload, int *paylen)
{
	if(!payload || !paylen){
		return 1;
	}
	uint8_t modid =0;
	int buflen = 0, tmplen, usedlen = 0;
	char *buffer = NULL;
	char modbuf[4096] = {0}; 
	
#define MALLOC_SIZE		1024
	for(modid = 0; modid < IDMAX; modid++){
		if(REP_BASE_CHECK(modid)){
			continue;
		}
		tmplen = sizeof(modbuf)-1;
		memset(modbuf, 0, sizeof(modbuf));
		if(modules[modid].report_func(modid, modbuf, &tmplen) != EREP_OK){
			continue;
		}
		printf("[%d]%s:paylaod %d\n", __LINE__, modules[modid].module_name, tmplen);
		if(buflen-usedlen < tmplen){
			buffer = realloc(buffer, buflen+tmplen+MALLOC_SIZE);
			if(buffer == NULL){
				printf("Recalloc Error:%s\n", strerror(errno));
				return 2;
			}
			buflen += (tmplen+MALLOC_SIZE);
		}
		if(usedlen){
			strncpy(buffer+usedlen, JOINER, strlen(JOINER));
			usedlen+=1;
		}
		memcpy(buffer+usedlen, modbuf, tmplen);
		usedlen+= tmplen;
	}
	if(buffer){
		buffer[usedlen] = '\0';
	}
	printf("Collect Report Finish\n->Paylen:%d\n->%s\n", usedlen, buffer);
	*paylen = usedlen;
	*payload = buffer;

	return 0;
}

uint8_t change_report_status(char *item, uint8_t onoff)
{
	if(!item){
		return 1;
	}

	if(onoff != MFALSE && onoff != MTRUE){
		printf("Unknow Status:%d\n", onoff);
		return 1;
	}
	uint8_t modid =0;
	for(modid = 0; modid < IDMAX; modid++){
		if(!strcmp(item, modules[modid].module_name)){
			printf("[%d]%s--->%s\n", __LINE__, modules[modid].module_name, onoff==1?"ON":"OFF");
			modules[modid].on_off(modid, (onoff?MTRUE:MFALSE));
			break;
		}
	}

	return 0;
}

