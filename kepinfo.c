#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#include "request.h"
#include "module.h"
#include "cJSON.h"


#define KEPINFO_GET_CONFIG	"https://www.simicloud.com/api/report/dev/control"
#define KEPINFO_POST_DATA	"https://www.simicloud.com/api/report/dev/info"
#define KEPFREQ			1800	/*30minite get config*/

typedef struct _kepinfo_t{
	int interval;	/*report data frequency*/
}kepinfo_t;

kepinfo_t kepinfo;

static uint8_t decode_json_shit(char *sjson, int slen, char *djson, int* dlen)
{
	int j, i;
	char *ptr = NULL;
	int tlen;
	if(!sjson || !djson || !slen || !dlen){
		return 1;
	}
	ptr = sjson;
	tlen = slen;
	if(*ptr != '"' || *(ptr+1) != '{' ||
			*(ptr+slen-1) != '"'){
		printf("[%d]No Need decode\n", __LINE__);
		strncpy(djson, sjson, *dlen-1);
		djson[*dlen-1] = '\0';
		return 0;
	}
	tlen -= 2;/*drop begin and end "*/
	i = 0;
	ptr++;
	for(j = 0; j< tlen; j++){
		if(*(ptr+j) == '\\'){
			continue;
		}
		djson[i++] = *(ptr+j);
	}
	djson[i] = '\0';
	*dlen = i;

	printf("Decode Finish:%s\n", djson);
	
	return 0;
}
static uint8_t kepinfo_get_config(kepinfo_t *kepcont)
{
	req_method request;
	FILE *fp;
	char line[256] = {0}, key[128] = {0}, value[128] = {0};
	char version[64] = {0};
	int cur, j;
	uint8_t ret = 0, onoff = 0;

	if(!kepcont){
		printf("[%d]argment error\n", __LINE__);
		return 1;
	}
	/*get firmware version*/
#ifdef OPENWRT
	fp = fopen("/etc/system.conf", "r");
#else
	fp = fopen("/etc/firmware", "r");
#endif
	if(fp == NULL){
		printf("[%d]fopen failed:%s\n", __LINE__, strerror(errno));
		return 2;
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
			break;
		}
	}
	fclose(fp);	
	if(!strlen(version)){
		printf("[%d] Version empty\n", __LINE__);
		return 2;
	}
	
	memset(&request, 0, sizeof(request));
	request.method = R_GET;
	snprintf(request.url, sizeof(request.url), "%s?pv=fw&version=%s", KEPINFO_GET_CONFIG, version);
	request.response_save = R_SMEM;
	request.get_data.urlencode = 1;

	printf("URL:%s\n", request.url);
	if(request_handle(&request) != 0){
		printf("[%d]Get keep info config failed\n", __LINE__);
		if(request.mem_resp.buflen){
			free(request.mem_resp.buffer);
		}
		return 2;
	}
	if(request.code != 200){
		printf("[%d]Http Code is not 200[%ld]\n", __LINE__, request.code);
		if(request.mem_resp.buflen){
			free(request.mem_resp.buffer);
		}		
		return 3;
	}

	/*decode json*/
	cJSON *root, *config, *subconf;
	printf("Response:%s\n", request.mem_resp.buffer);
	root = cJSON_Parse(request.mem_resp.buffer);
	if(!root){
		printf("[%d]json decode failed\n", __LINE__);
		if(request.mem_resp.buflen){
			free(request.mem_resp.buffer);
		}
		return 4;
	}
	config = cJSON_GetObjectItem(root, "config");
	if(!config){
		printf("[%d]No found config\n", __LINE__);
		ret = 4;
		goto KEP_OUT;
	}
	/*Server developer not change the resoponse, we need to decode string FUCK SHIT*/
	char jsonstr[4096] = {0}, djson[4096] = {0};
	int tlen = sizeof(djson);
	strncpy(jsonstr, cJSON_Print(config), sizeof(jsonstr)-1);
	if(decode_json_shit(jsonstr, strlen(jsonstr), djson, &tlen) != 0){
		printf("[%d]Failed->%s\n", __LINE__, jsonstr);		
	}else{
		/*release root*/
		cJSON_Delete(root);		
		root = cJSON_Parse(djson);
		if(!root){
			printf("[%d]json decode failed\n", __LINE__);
			if(request.mem_resp.buflen){
				free(request.mem_resp.buffer);
			}
			return 4;
		}		
		config = root;
	}
	subconf = cJSON_GetObjectItem(config, "interval");
	if(!subconf){
		//ret = 4;
		//goto KEP_OUT;
		kepcont->interval = 600;
		printf("[%d]Config=>Dinterval:%d\n", __LINE__, kepcont->interval);		
	}else{
		kepcont->interval = subconf->valueint;		
		printf("[%d]Config=>interval:%d\n", __LINE__, kepcont->interval);
	}
	for(j = 0; j < IDMAX; j++){
		subconf = cJSON_GetObjectItem(config, modules_list[j]);
		if(!subconf){
			printf("[%d]Config=>D%s:1\n", __LINE__, modules_list[j]);
			onoff = 1;
		}else{
			onoff = (uint8_t)(subconf->valueint);			
			printf("[%d]Config=>%s:%d\n", __LINE__, modules_list[j], onoff);
		}

		change_report_status(modules_list[j], onoff);
	}
	ret = 0;	

KEP_OUT:
	cJSON_Delete(root);	
	if(request.mem_resp.buflen){
		free(request.mem_resp.buffer);
	}

	return ret;
}


static uint8_t kepinfo_post_data(void)
{
	req_method request;

	memset(&request, 0, sizeof(request));
	if(collect_report_info(&(request.post_data.post_data), &(request.post_data.post_len)) !=0){
		printf("[%d]Collect info failed\n", __LINE__);
		return 1;
	}
	if(!request.post_data.post_len){
		printf("[%d]No Data\n", __LINE__);
		return 0;
	}
	request.method = R_POST;
	snprintf(request.url, sizeof(request.url), "%s", KEPINFO_POST_DATA);
	request.response_save = R_IGNORE;
	request.post_data.urlencode = 1;
	
	if(request_handle(&request) != 0){
		printf("[%d]Get keep info config failed\n", __LINE__);
		free(request.post_data.post_data);
		return 2;
	}
	if(request.code != 200){
		printf("[%d]Http Code is not 200[%ld]\n", __LINE__, request.code);		
		free(request.post_data.post_data);
		return 3;
	}
	printf("[%d]Post Data OK[%d]:%s\n", __LINE__, request.post_data.post_len, request.post_data.post_data);
	free(request.post_data.post_data);
	
	return 0;
}

int main(int argc, char **argv)
{
	int internal  = 0, keeptime = 0;
	/*init libcurl first*/
	request_init();
	/*init collect module*/
	collect_report_init();

	/*must wait kepinfo_get_config successful*/
	while(kepinfo_get_config(&kepinfo)){
		sleep(30);
	}

	while(1){
		if(keeptime >= KEPFREQ){
			printf("Get Server Config\n");
			if(kepinfo_get_config(&kepinfo) != 0){
				sleep(30);
				continue;
			}
			keeptime = 0;
		}
		internal = kepinfo.interval;
		kepinfo_post_data();
		sleep(internal);
		keeptime += internal;
	}

	return 0;
}
