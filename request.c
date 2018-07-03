#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <errno.h>
#include <netinet/in.h>
#include <arpa/nameser.h>
#include <resolv.h>
#include <curl/curl.h>
#include <curl/multi.h>
#include <unistd.h>

#include "request.h" 

static size_t write_nosave_callback(void *ptr, size_t size, size_t nmemb, void *data)
{
	/* we are not interested in the downloaded bytes itself,
	 so we only return the size we would have saved ... */ 
	(void)ptr;  /* unused */ 
	(void)data; /* unused */ 
	return (size_t)(size * nmemb);
}

static size_t 
write_file_callback(char *ptr, size_t size, size_t nmemb, void *userdata)
{
	FILE *fp = (FILE*)userdata;
	size_t retcode;
	
	retcode = fwrite(ptr, size, nmemb, fp);
	
	return retcode;
}

size_t
write_mem_callback(void *contents, size_t size, size_t nmemb, void *userp)
{
	size_t realsize = size * nmemb;
	membuf_t *mem = (membuf_t *)userp;

	mem->buffer= realloc(mem->buffer, mem->buflen+ realsize + 1);
	if (mem->buffer == NULL) {
		/* out of memory! */
		printf("NOT ENOUGH MEMORY (REALLOC RETURNED NULL)\n");
		return 0;
	}

	memcpy(&(mem->buffer[mem->buflen]), contents, realsize);
	mem->buflen += realsize;
	mem->buffer[mem->buflen] = 0;

	return realsize;
}

uint8_t download_unit(char *url)
{
	CURL *curl_handle;
	CURLcode res;

	if(!url){
		return 1;
	}

	/* init the curl session */ 
	curl_handle = curl_easy_init();

	/* specify URL to get */ 
	curl_easy_setopt(curl_handle, CURLOPT_URL, url);

	/* send all data to this function  */ 
	curl_easy_setopt(curl_handle, CURLOPT_WRITEFUNCTION, write_nosave_callback);

	/* some servers don't like requests that are made without a user-agent
	 field, so we provide one */ 
	curl_easy_setopt(curl_handle, CURLOPT_USERAGENT,
	               "Mozilla/5.0 (Windows NT 6.1; WOW64) AppleWebKit/537.31 (KHTML, like Gecko) Chrome/26.0.1410.64 Safari/537.31");

	curl_easy_setopt(curl_handle, CURLOPT_FOLLOWLOCATION, 1L);
	curl_easy_setopt(curl_handle, CURLOPT_CONNECTTIMEOUT, 15L);
	curl_easy_setopt(curl_handle, CURLOPT_LOW_SPEED_LIMIT, 10L);
	curl_easy_setopt(curl_handle, CURLOPT_LOW_SPEED_TIME, 15L);	
	curl_easy_setopt(curl_handle, CURLOPT_NOSIGNAL, 1);
	curl_easy_setopt(curl_handle, CURLOPT_TIMEOUT, 60L);  /*60s communication*/
	//curl_easy_setopt(curl_handle, CURLOPT_VERBOSE, 1L);
	/* get it! */ 
	res = curl_easy_perform(curl_handle);
 
	if(CURLE_OK == res) {
		printf("OK:%s\n", url);	
	}
	else {
		fprintf(stderr, "Error while fetching '%s' : %s\n",
	        url, curl_easy_strerror(res));		
		res_init();
		/* cleanup curl stuff */ 
		curl_easy_cleanup(curl_handle);
		return 1;		
	}
 
	/* cleanup curl stuff */ 
	curl_easy_cleanup(curl_handle);

	return 0;
}

uint8_t download_speed_test(char *url, int concurrent)
{
	CURLM *cm;
	CURLMsg *msg;
	CURL **ehlist;
	long L;
	unsigned int C = 0;
	int M, Q, U = -1;
	fd_set R, W, E;
	struct timeval T;
	int result_count = 0;

	
	ehlist = calloc(1, sizeof(CURL *)*concurrent);
	if(!ehlist){
		printf("[%d]Calloc Failed:%s\n", __LINE__, strerror(errno));
		return 1;
	}	 
	cm = curl_multi_init();
	/* we can optionally limit the total amount of connections this multi handle
	 uses */ 
	curl_multi_setopt(cm, CURLMOPT_MAXCONNECTS, (long)concurrent);
	for(C = 0; C < concurrent; ++C) {
		ehlist[C] = curl_easy_init();
		curl_easy_setopt(ehlist[C], CURLOPT_WRITEFUNCTION, write_nosave_callback);
		curl_easy_setopt(ehlist[C], CURLOPT_HEADER, 0L);
		curl_easy_setopt(ehlist[C], CURLOPT_URL, url);
		curl_easy_setopt(ehlist[C], CURLOPT_PRIVATE, url);
		curl_easy_setopt(ehlist[C], CURLOPT_LOW_SPEED_LIMIT, 10L);
		curl_easy_setopt(ehlist[C], CURLOPT_LOW_SPEED_TIME, 25L);
		curl_easy_setopt(ehlist[C], CURLOPT_TIMEOUT, 120L);  /*120s communication*/		
		curl_easy_setopt(ehlist[C], CURLOPT_VERBOSE, 0L);
		curl_multi_add_handle(cm, ehlist[C]);
	}

	while(U) {
		curl_multi_perform(cm, &U);

		if(U) {
			FD_ZERO(&R);
			FD_ZERO(&W);
			FD_ZERO(&E);

			if(curl_multi_fdset(cm, &R, &W, &E, &M)) {
				fprintf(stderr, "E: curl_multi_fdset\n");
				goto DW_ERR;
			}

			if(curl_multi_timeout(cm, &L)) {
				fprintf(stderr, "E: curl_multi_timeout\n");
				goto DW_ERR;
			}
			if(L == -1){
				L = 100;
			}

			if(M == -1){
				printf("[%d]Sleep\n", __LINE__);				
				usleep(100000);
			}else {
				T.tv_sec = L/1000;
				T.tv_usec = (L%1000)*1000;
				if(0 > select(M + 1, &R, &W, &E, &T)) {
					fprintf(stderr, "E: select(%i,,,,%li): %i: %s\n",
					  M + 1, L, errno, strerror(errno));
					goto DW_ERR;
				}
			}
		}

		while((msg = curl_multi_info_read(cm, &Q))) {
			if(msg->msg == CURLMSG_DONE){
				char *url;
				CURL *e = msg->easy_handle;
				curl_easy_getinfo(msg->easy_handle, CURLINFO_PRIVATE, &url);
				if(msg->data.result != CURLE_OK){
					result_count++;
				}
				fprintf(stderr, "R: %d - %s <%s>\n",
						msg->data.result, curl_easy_strerror(msg->data.result), url);
				curl_multi_remove_handle(cm, e);
				//curl_easy_cleanup(e);
			}else {
				fprintf(stderr, "E: CURLMsg (%d)\n", msg->msg);
			}
		}
	}
	
	for(C = 0; C < concurrent; ++C) {
		if(ehlist[C]){
			printf("[%d]Clearup:%d\n", __LINE__, C);
			curl_easy_cleanup(ehlist[C]);
		}
	}
	free(ehlist);
	curl_multi_cleanup(cm);
	if(result_count == concurrent){
		printf("[%d]All Failed\n", __LINE__);
		return 1;
	}
	return 0;

DW_ERR:
	for(C = 0; C < concurrent; ++C) {
		if(ehlist[C]){
			printf("[%d]Clearup:%d\n", __LINE__, C);
			curl_easy_cleanup(ehlist[C]);
		}
	}
	free(ehlist);	
	curl_multi_cleanup(cm);
	return 1;
}

uint8_t upload_speed_test(char *url, int concurrent, char *post_data, int post_len)
{
	CURLM *cm;
	CURLMsg *msg;
	CURL **ehlist;
	long L;
	unsigned int C = 0;
	int M, Q, U = -1;
	fd_set R, W, E;
	struct timeval T;
	int result_count = 0;

	if(!post_data || !url || !post_len){
		printf("[%d] Argument error\n", __LINE__);
		return 1;
	}
	ehlist = calloc(1, sizeof(CURL *)*concurrent);
	if(!ehlist){
		printf("[%d]Calloc Failed:%s\n", __LINE__, strerror(errno));
		return 1;
	}	 
	cm = curl_multi_init();
	/* we can optionally limit the total amount of connections this multi handle
	 uses */ 
	curl_multi_setopt(cm, CURLMOPT_MAXCONNECTS, (long)concurrent);
	for(C = 0; C < concurrent; ++C) {
		ehlist[C] = curl_easy_init();
		curl_easy_setopt(ehlist[C], CURLOPT_WRITEFUNCTION, write_nosave_callback);
		curl_easy_setopt(ehlist[C], CURLOPT_HEADER, 0L);
		curl_easy_setopt(ehlist[C], CURLOPT_URL, url);
		curl_easy_setopt(ehlist[C], CURLOPT_PRIVATE, url);
		curl_easy_setopt(ehlist[C], CURLOPT_VERBOSE, 0L);		
		curl_easy_setopt(ehlist[C], CURLOPT_POST, 1L);		
		curl_easy_setopt(ehlist[C], CURLOPT_LOW_SPEED_LIMIT, 10L);
		curl_easy_setopt(ehlist[C], CURLOPT_LOW_SPEED_TIME, 25L);
		curl_easy_setopt(ehlist[C], CURLOPT_TIMEOUT, 120L);  /*120s communication*/		
		curl_easy_setopt(ehlist[C], CURLOPT_POSTFIELDSIZE, post_len);
		/* pass in a pointer to the data - libcurl will not copy */
		curl_easy_setopt(ehlist[C], CURLOPT_POSTFIELDS, post_data);		
		curl_multi_add_handle(cm, ehlist[C]);
	}

	while(U) {
		curl_multi_perform(cm, &U);

		if(U) {
			FD_ZERO(&R);
			FD_ZERO(&W);
			FD_ZERO(&E);

			if(curl_multi_fdset(cm, &R, &W, &E, &M)) {
				fprintf(stderr, "E: curl_multi_fdset\n");
				goto DW_ERR;
			}

			if(curl_multi_timeout(cm, &L)) {
				fprintf(stderr, "E: curl_multi_timeout\n");
				goto DW_ERR;
			}
			if(L == -1){
				L = 100;
			}

			if(M == -1){
				printf("[%d]Sleep\n", __LINE__);				
				usleep(100000);
			}else {
				T.tv_sec = L/1000;
				T.tv_usec = (L%1000)*1000;
				if(0 > select(M + 1, &R, &W, &E, &T)) {
					fprintf(stderr, "E: select(%i,,,,%li): %i: %s\n",
					  M + 1, L, errno, strerror(errno));
					goto DW_ERR;
				}
			}
		}

		while((msg = curl_multi_info_read(cm, &Q))) {
			if(msg->msg == CURLMSG_DONE){
				char *url;
				CURL *e = msg->easy_handle;
				curl_easy_getinfo(msg->easy_handle, CURLINFO_PRIVATE, &url);
				if(msg->data.result != CURLE_OK){
					result_count++;
				}				
				fprintf(stderr, "W: %d - %s <%s>\n",
						msg->data.result, curl_easy_strerror(msg->data.result), url);
				curl_multi_remove_handle(cm, e);
				//curl_easy_cleanup(e);
			}else {
				fprintf(stderr, "E: CURLMsg (%d)\n", msg->msg);
			}
		}
	}
	for(C = 0; C < concurrent; ++C) {
		if(ehlist[C]){
			printf("[%d]Clearup:%d\n", __LINE__, C);
			curl_easy_cleanup(ehlist[C]);
		}
	}	
	free(ehlist);
	curl_multi_cleanup(cm);
	if(result_count == concurrent){
		printf("[%d]All Failed\n", __LINE__);
		return 1;
	}	
	return 0;

DW_ERR:
	for(C = 0; C < concurrent; ++C) {
		if(ehlist[C]){
			printf("[%d]Clearup:%d\n", __LINE__, C);
			curl_easy_cleanup(ehlist[C]);
		}
	}
	free(ehlist);	
	curl_multi_cleanup(cm);
	return 1;
}

void request_init(void)
{
	/* init libcurl */ 
	curl_global_init(CURL_GLOBAL_ALL);
}

uint8_t request_handle(req_method *request)
{
	CURL *curl_handle;
	CURLcode res;
	struct curl_slist *slist = NULL;
	int iter = 0;
	FILE *fp;
	
	if(!request){
		return 1;
	}

	/* init the curl session */ 
	curl_handle = curl_easy_init();

	/* specify URL to get */ 
	curl_easy_setopt(curl_handle, CURLOPT_URL, request->url);
	curl_easy_setopt(curl_handle, CURLOPT_FOLLOWLOCATION, 1L);
	curl_easy_setopt(curl_handle, CURLOPT_CONNECTTIMEOUT, 20L);
	curl_easy_setopt(curl_handle, CURLOPT_LOW_SPEED_LIMIT, 10L);
	curl_easy_setopt(curl_handle, CURLOPT_LOW_SPEED_TIME, 25L);
	curl_easy_setopt(curl_handle, CURLOPT_TIMEOUT, 120L);  /*120s communication*/
	
	if(!strncmp(request->url, "https", strlen("https"))){
		printf("[%d]Set Https Option\n", __LINE__);
		curl_easy_setopt(curl_handle, CURLOPT_SSL_VERIFYPEER, 0L);
	}
	curl_easy_setopt(curl_handle, CURLOPT_NOSIGNAL, 1);
	curl_easy_setopt(curl_handle, CURLOPT_VERBOSE, 1L);
	/* some servers don't like requests that are made without a user-agent
	 field, so we provide one */ 
	slist = curl_slist_append(slist, "User-Agent: Mozilla/5.0 (Windows NT 6.1; WOW64) AppleWebKit/537.31 (KHTML, like Gecko) Chrome/26.0.1410.64 Safari/537.31");
	for(iter = 0; iter < request->headers.count;  iter++){
		slist = curl_slist_append(slist, request->headers.header[iter]);
	}
	curl_easy_setopt(curl_handle, CURLOPT_HTTPHEADER, slist);
	
	/* send all data to this function  */ 
	if(request->response_save == R_SFILE){
		fp = fopen(request->file_resp, "w");
		if(fp == NULL){
			printf("fopen failed:%s\n", strerror(errno));
			/* cleanup curl stuff */ 
			curl_easy_cleanup(curl_handle);
			return 1;
		}
		curl_easy_setopt(curl_handle, CURLOPT_WRITEFUNCTION, write_file_callback);
		curl_easy_setopt(curl_handle, CURLOPT_WRITEDATA, (void*)fp);
		
	}else if(request->response_save == R_SMEM){
		curl_easy_setopt(curl_handle, CURLOPT_WRITEFUNCTION, write_mem_callback);
		curl_easy_setopt(curl_handle, CURLOPT_WRITEDATA, (void *)&(request->mem_resp));
	}
	if(request->method == R_POST){
		curl_easy_setopt(curl_handle, CURLOPT_POST, 1L);
		if(request->post_data.post_len){
			/* size of the POST data */
			curl_easy_setopt(curl_handle, CURLOPT_POSTFIELDSIZE, request->post_data.post_len);
			/* pass in a pointer to the data - libcurl will not copy */
			curl_easy_setopt(curl_handle, CURLOPT_POSTFIELDS, request->post_data.post_data);
		}
		if(request->post_data.urlencode){
			slist = curl_slist_append(slist, "Content-Type: application/x-www-form-urlencoded");
		}
	
	}else if(request->method == R_GET){
		/*Not support*/
		//if(request->get_data.accept_gzip){
		//	curl_easy_setopt(curl_handle, CURLOPT_ACCEPT_ENCODING, "gzip");
		//}
		if(request->get_data.urlencode){
			slist = curl_slist_append(slist, "Content-Type: application/x-www-form-urlencoded");
		}		
	}

	res = curl_easy_perform(curl_handle);
	if(request->response_save == R_SFILE){
		fclose(fp);
	}
	if(slist){
		curl_slist_free_all(slist);
	}
	if(res != CURLE_OK) {
		/* perform failed */
		if (res == CURLE_SSL_CONNECT_ERROR
				|| res == CURLE_OPERATION_TIMEDOUT
				|| res == CURLE_COULDNT_CONNECT
				|| res == CURLE_COULDNT_RESOLVE_HOST){
		}		
		res_init();
		printf("[%d]%s\n", __LINE__, curl_easy_strerror(res));		
		curl_easy_cleanup(curl_handle);
		return 2;		
	}
	/* perform success */
	curl_easy_getinfo(curl_handle, CURLINFO_RESPONSE_CODE, &request->code);
	fprintf(stderr, "+++++++++Response code: %ld\n", request->code);
	curl_easy_cleanup(curl_handle);
	
	return 0;
}

void request_nameserver_reinit(void)
{
	res_init();
}
