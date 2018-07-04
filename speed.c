#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <errno.h>
#include <unistd.h>

#include <math.h> /*need -lm*/
#include <stdlib.h>
#include <sys/time.h>

#include "request.h"
#include <mxml.h>

static int dl_sizes[] = {350, 500, 750, 1000, 1500, 2000, 2500, 3000, 3500, 4000};
static int ul_sizes[] = {100, 300, 500, 800, 1000, 1500, 2500, 3000, 3500, 4000}; //kB


#define SPEED_MYSELF		"http://speedtest.net/speedtest-config.php"
#define SPEED_SERVER1		"http://www.speedtest.net/speedtest-servers-static.php"
#define SPEED_SERVER2		"http://c.speedtest.net/speedtest-servers-static.php"


#define XML_WAN			"/tmp/speed.waninfo"
#define XML_SERVER		"/tmp/speed.server"
typedef struct _user_info{
	char ip[64];
	char lat[64];
	char lon[64];
	char isp[256];
}user_info;

typedef struct _server_info{
	char url[1024];
	char lat[64];
	char lon[64];
	char name[64];
	char country[128];
	char sponsor[256];
	double distance;
}server_info;

typedef struct _speed_contx{
	user_info user;
	server_info server;
}speed_contx;


static speed_contx cspeed;


static double distance(char *lat1, char *lon1, char *lat2, char *lon2)
{
	double radius = 6378.137;
	double a1, b1, a2, b2, x;

	a1 = atof(lat1)*M_PI/180.0;
	b1 = atof(lon1)*M_PI/180.0;
	a2 = atof(lat2)*M_PI/180.0;
	b2 = atof(lon2)*M_PI/180.0;

	x = sin(a1)*sin(a2)+cos(a1)*cos(a2)*cos(b2-b1);

	return radius*acos(x);
}

/*Get the nearest server*/
static uint8_t get_nearest_server(user_info *user, server_info *server)
{
	req_method request;

	if(!user || !server){
		return 1;
	}
#define DBL_MAX		0xFFFF
	memset(&request, 0, sizeof(request));
	request.method = R_GET;
	strcpy(request.url, SPEED_SERVER1);
	request.response_save = R_SFILE;
	strcpy(request.file_resp, XML_SERVER);
	/*accept gzip encode, if libcurl add zlib support*/
	request.get_data.accept_gzip = 1;

	if(request_handle(&request) != 0 
			|| request.code != 200){
		printf("Get ServerList failed, Try second\n");
		strcpy(request.url, SPEED_SERVER2);
		request.code = 0;
		if(request_handle(&request) != 0){
			printf("Get ServerList failed\n");
			return 2;
		}
	}
	if(request.code != 200){
		printf("[%d]Http Code is not 200[%ld]\n", __LINE__,request.code);
		return 3;
	}
	/*decode xml response*/
	FILE *fp;
	mxml_node_t *tree;
	mxml_node_t *xserver;
	server_info tsrv;	
	const char *paytr;
	double tdistance = DBL_MAX;
	
	fp = fopen(XML_SERVER, "r");
	if(fp == NULL){
		printf("[%d]fopen failed:%s\n", __LINE__, strerror(errno));
		return 4;
	}
	tree = mxmlLoadFile(NULL, fp, MXML_NO_CALLBACK);
	fclose(fp);

	for (xserver = mxmlFindElement(tree, tree, "server", NULL, NULL,
	                            MXML_DESCEND); xserver != NULL;
	     xserver = mxmlFindElement(xserver, tree, "server", NULL, NULL,
	                            MXML_DESCEND)){
	                            
		memset(&tsrv, 0, sizeof(tsrv));
		/*get url*/
		paytr = mxmlElementGetAttr(xserver,"url");
		if(!paytr){
			printf("Missing url\n");
			continue;
		}
		strcpy(tsrv.url, paytr);
		/*get lat*/
		paytr = mxmlElementGetAttr(xserver,"lat");
		if(!paytr){
			printf("Missing lat\n");
			continue;
		}
		strcpy(tsrv.lat, paytr);
		
		/*get lon*/ 
		paytr = mxmlElementGetAttr(xserver,"lon");
		if(!paytr){
			printf("Missing lon\n");
			continue;
		}
		strcpy(tsrv.lon, paytr);		
		/*get name*/ 
		paytr = mxmlElementGetAttr(xserver,"name");
		if(paytr){
			strcpy(tsrv.name, paytr);
		}
		/*get country*/ 
		paytr = mxmlElementGetAttr(xserver,"country");
		if(paytr){
			strcpy(tsrv.country, paytr);
		}
		/*get sponsor*/ 
		paytr = mxmlElementGetAttr(xserver,"sponsor");
		if(paytr){
			strcpy(tsrv.sponsor, paytr);
		}
		tsrv.distance = distance(user->lat, user->lon, tsrv.lat, tsrv.lon);
		if(tdistance > tsrv.distance){
			tdistance = tsrv.distance;
			memcpy(server, &tsrv, sizeof(tsrv));
			printf("Found Near Distance:%5.2f\n", server->distance);
		}
	}
	mxmlDelete(tree);
	/*remove XML_SERVER*/
	remove(XML_SERVER);
	if(tdistance == DBL_MAX){
		printf("No Found Server\n");
		return 5;
	}
	printf("Nearest Server:\n\tURL:%s\n\tLAT:%s\n\tLAT:%s\n\tName:%s\n\tCountry:%s\n\tSponsor:%s\n\tDistance:%.2fkm\n", 
			server->url, server->lat, server->lon,
				server->name, server->country, server->sponsor, server->distance);
	return 0;
}

static uint8_t get_latency(char *url, int *latency)
{
	char *ptr = NULL;
	int tstcnt = 0, tlatency  =0, tlatency2 = 0;
	char laturl[2048] = {0};
	struct timeval start;
	struct timeval end;
	
	if(!latency || !url){
		return 1;
	}

	ptr = strstr(url, "/upload");
	if(ptr == NULL){
		printf("[%d]Server Bad:%s\n", __LINE__, url);
		return 2;
	}
	strncpy(laturl, url, ptr-url);
	strcat(laturl, "/latency.txt");
	printf("Latency Server:%s\n", laturl);

	tlatency = 100000;/*default 100s*/
	
	for(tstcnt = 0; tstcnt < 4; tstcnt++){
		gettimeofday(&start,NULL);
		if(download_unit(laturl) != 0){
			printf("Test Latency[%d] Failed\n", tstcnt);
			continue;
		}
		gettimeofday(&end,NULL);
		tlatency2 =(end.tv_sec-start.tv_sec)*1000+(end.tv_usec-start.tv_usec)/1000;
		if(tlatency > tlatency2){
			tlatency = tlatency2;
		}
		printf("Test Latency[%d]:%dms\n", tstcnt, tlatency);
		/*sleep a while*/
		usleep(100000);
	}
	if(tlatency == 100000){
		printf("Test Latency Failed\n");
		return 1;
	}

	*latency = tlatency/2;
	printf("Latency:%dms\n", *latency);
	
	return 0;
}


static uint8_t get_download_speed(char *url, int latency, float *dwspeed)
{
	char *ptr = NULL;
	float ttime = 0, totalspeed = 0;
	char baseurl[2048] = {0}, dwurl[2048] = {0};
	int tstcnt, vaild = 0;
	struct timeval gstart;
	struct timeval gend;
#define DW_WARMCNT	3
	if(!dwspeed || !url){
		return 1;
	}

	ptr = strstr(url, "/upload");
	if(ptr == NULL){
		printf("[%d]Server Bad:%s\n", __LINE__, url);
		return 2;
	}
	strncpy(baseurl, url, ptr-url);
	baseurl[ptr-url] = '\0';
	printf("Download URL:%s\n", baseurl);
	snprintf(dwurl, sizeof(dwurl), "%s/random%dx%d.jpg", baseurl, dl_sizes[2], dl_sizes[2]);
	for(tstcnt = 0; tstcnt < DW_WARMCNT; tstcnt++){
		gettimeofday(&gstart,NULL);
		if(download_unit(dwurl) != 0){
			printf("Test Download[%d] Failed\n", tstcnt);
			continue;
		}		
		gettimeofday(&gend,NULL);
		ttime += ((gend.tv_sec-gstart.tv_sec)+(float)(gend.tv_usec-gstart.tv_usec)/1000.0/1000.0);
		vaild++;
		usleep(100000);
	}
	if(vaild == 0){
		printf("[%d] Warm Download Failed\n", __LINE__);
		return 1;
	}
	if(ttime > latency/1000.0){
		ttime -= (latency/1000.0);
	}
	printf("Cost %3.3fs\n", ttime);
	totalspeed = 1.125 * 8 * vaild /ttime;
	printf("WarmSpeed is %3.2fMbps\n", totalspeed);
	int concurrent = 0;
	int weight = 0;

	if(totalspeed > 10.0){
		/*10Mbps*/
		concurrent = 10;
		weight = 6;
	}else if(totalspeed > 4.0){
		/*4M*/
		concurrent = 6;
		weight = 5;
	}else if(totalspeed > 2.5){
		/*2.5M*/
		concurrent = 3;
		weight = 4;
	}else{
		*dwspeed = totalspeed;
		printf("No Need Test.\n");
		return 0;
	}

	snprintf(dwurl, sizeof(dwurl), "%s/random%dx%d.jpg", baseurl, dl_sizes[weight], dl_sizes[weight]);
	
	printf("Speed Download URL:%s\n", dwurl);
	gettimeofday(&gstart,NULL);
	if(download_speed_test(dwurl, concurrent) != 0){
		printf("[%d]Test Download Speed Failed\n", __LINE__);
		*dwspeed = totalspeed;
		return 0;
	}
	gettimeofday(&gend,NULL);
	ttime = ((gend.tv_sec-gstart.tv_sec)+(float)(gend.tv_usec-gstart.tv_usec)/1000.0/1000.0);
	ttime -= (latency/1000.0);
	printf("[%d]Download Cost %.3f\n", __LINE__, ttime);
	*dwspeed = dl_sizes[weight]*dl_sizes[weight]*2.0/1000.0/1000.0;
	*dwspeed = (*dwspeed)*8*concurrent/ttime;

	printf("Speed is %3.2fMbps\n", *dwspeed);

	return 0;
}

static uint8_t get_upload_speed(char *url, int latency, float *upspeed)
{
	char *payload = NULL;
	float ttime = 0, totalspeed = 0;
	int paylen = 0;
	struct timeval gstart;
	struct timeval gend;
#define UP_WARMCNT	3
	if(!upspeed || !url){
		return 1;
	}

	paylen = strlen("content=") + ul_sizes[4]*1000-510;
	payload = malloc(paylen);
	if(!payload){
		printf("[%d]malloc failed:%s\n", __LINE__, strerror(errno));
		return 1;
	}
	/*init to content=000000000000*/
	memset(payload, '0', paylen);
	strncpy(payload, "content=", strlen("content="));
	
	printf("Upload URL:%s\n", url);
	gettimeofday(&gstart,NULL);
	if(upload_speed_test(url, UP_WARMCNT, payload, paylen) != 0){
		printf("[%d]Warm Upload Speed Failed\n", __LINE__);
		free(payload);
		return 1;
	}
	gettimeofday(&gend,NULL);
	ttime += ((gend.tv_sec-gstart.tv_sec)+(float)(gend.tv_usec-gstart.tv_usec)/1000.0/1000.0);
	/*free memory*/
	free(payload);
	if(ttime > latency/1000.0){
		ttime -= (latency/1000.0);
	}
	printf("Cost %3.3fs\n", ttime);
	totalspeed = 1.0 * 8 * UP_WARMCNT /ttime;
	printf("WarmSpeed is %3.2fMbps\n", totalspeed);
	int concurrent = 0;
	int weight = 0;

	if(totalspeed > 10.0){
		/*10Mbps*/
		concurrent = 10;
		weight = 9;
	}else if(totalspeed > 4.0){
		/*4M*/
		concurrent = 8;
		weight = 9;
	}else if(totalspeed > 2.5){
		/*2.5M*/
		concurrent = 6;
		weight = 5;
	}else{	
		printf("No Need Test.\n");
		*upspeed = totalspeed;
		return 0;
	}
	paylen = strlen("content=") + ul_sizes[weight]*1000-510;
	payload = malloc(paylen);
	if(!payload){
		printf("[%d]malloc failed:%s\n", __LINE__, strerror(errno));
		return 1;
	}
	/*init to content=000000000000*/
	memset(payload, '0', paylen);
	strncpy(payload, "content=", strlen("content="));
	gettimeofday(&gstart,NULL);
	if(upload_speed_test(url, concurrent, payload, paylen) != 0){
		printf("[%d]Test Upload Speed Failed\n", __LINE__);
		*upspeed = totalspeed;		
		free(payload);
		return 0;
	}
	gettimeofday(&gend,NULL);
	free(payload);
	ttime = ((gend.tv_sec-gstart.tv_sec)+(float)(gend.tv_usec-gstart.tv_usec)/1000.0/1000.0);
	if(ttime > latency/1000.0){
		ttime -= (latency/1000.0);
	}	
	printf("[%d]Upload Cost %.3fs\n", __LINE__, ttime);
	*upspeed = (ul_sizes[weight]/1000.0)*8*concurrent/ttime;

	printf("Speed is %3.2fMbps\n", *upspeed);

	return 0;
}

/***************************************************************************************/

uint8_t speed_get_internet_info(char *ip, char *isp)
{
	req_method request;

	if(!ip || !isp){
		return 1;
	}
	memset(&request, 0, sizeof(request));
	request.method = R_GET;
	strcpy(request.url, SPEED_MYSELF);
	request.response_save = R_SFILE;
	strcpy(request.file_resp, XML_WAN);
	/*accept gzip encode, if libcurl add zlib support*/
	request.get_data.accept_gzip = 1;

	/*Try twice*/
	if(request_handle(&request) != 0 &&
			request_handle(&request) != 0){
		printf("Get wan info failed\n");
		return 2;
	}
	if(request.code != 200){
		printf("Http Code is not 200[%ld]\n", request.code);
		return 3;
	}
	/*decode xml response*/
	FILE *fp;
	mxml_node_t *tree;
	mxml_node_t *client;
	const char *paytr;
	user_info tuser;

	
	fp = fopen(XML_WAN, "r");
	if(fp == NULL){
		printf("[%d]fopen failed:%s\n", __LINE__, strerror(errno));
		return 4;
	}
	tree = mxmlLoadFile(NULL, fp, MXML_NO_CALLBACK);
	fclose(fp);
	client = mxmlFindElement(tree, tree, "client",NULL, NULL,MXML_DESCEND);
	/*get ip*/
	paytr = mxmlElementGetAttr(client,"ip");
	if(!paytr){
		printf("Missing ip\n");
		return 4;
	}
	strcpy(tuser.ip, paytr);

	/*get lat*/
	paytr = mxmlElementGetAttr(client,"lat");
	if(!paytr){
		printf("Missing lat\n");
		return 4;
	}
	strcpy(tuser.lat, paytr);
	
	/*get lon*/	
	paytr = mxmlElementGetAttr(client,"lon");
	if(!paytr){
		printf("Missing lon\n");
		return 4;
	}
	strcpy(tuser.lon, paytr);

	/*get isp*/	
	paytr = mxmlElementGetAttr(client,"isp");
	if(!paytr){
		printf("Missing isp\n");
		return 4;
	}
	strcpy(tuser.isp, paytr);
	mxmlDelete(tree);
	/*remove the XML_WAN*/	
	remove(XML_WAN);
	/*return*/
	memcpy(&(cspeed.user), &tuser, sizeof(tuser));
	strcpy(ip, cspeed.user.ip);
	strcpy(isp, cspeed.user.isp);

	printf("[%d]IP:%s  ISP:%s\n", __LINE__, ip, isp);

	return 0;
}

uint8_t speed_test_speed(int *latency, float *upspeed, float *dwspeed)
{
	if(!latency || !upspeed || !dwspeed){
		return 1;
	}
	if(get_nearest_server(&(cspeed.user), &(cspeed.server)) != 0){
		printf("[%d]Get Nearest Server Failed\n", __LINE__);
		return 2;
	}

	/*Get latency*/
	if(get_latency(cspeed.server.url, latency) != 0){
		printf("[%d]Get latency Failed\n", __LINE__);
		return 3;
	}
	/*Test download speed*/
	if(get_download_speed(cspeed.server.url, *latency, dwspeed) != 0){
		printf("[%d]Get download speed Failed\n", __LINE__);
		return 3;
	}
	
	/*Test upload speed*/
	if(get_upload_speed(cspeed.server.url, *latency, upspeed) != 0){
		printf("[%d]Get upload speed Failed\n", __LINE__);
		return 3;
	}

	printf("Latency:%dms Download:%dMbps Upload:%dMbps\n", *latency, (int)*dwspeed, (int)*upspeed);

	return 0;
}
