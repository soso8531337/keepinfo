#ifndef _REQUEST_H
#define _REQUEST_H

#include <stddef.h>
#include <stdint.h>
enum{
	R_GET = 1,
	R_POST = 2,
};
enum{
	R_SMEM = 1,
	R_SFILE = 2,
	R_IGNORE = 3,
};

typedef struct _membuf_t{
	char *buffer;
	int buflen;
}membuf_t;

typedef struct _header_list{
	char **header;
	int count;
}header_list;

typedef struct _post_field{
	char *post_data;
	int post_len;
	int urlencode;
}post_field;

typedef struct _get_field{
	int accept_gzip;	
	int urlencode;
}get_field;
typedef struct _rmethod_t{
	uint8_t method;
	char url[1024];
	uint8_t response_save;/*File or memeory*/
	union{
		char file_resp[512];
		membuf_t mem_resp;
	};
	union{
		post_field post_data;
		get_field get_data;
	};
	header_list headers;
	long code;
}req_method;

uint8_t download_unit(char *url);
uint8_t download_speed_test(char *url, int concurrent);
uint8_t upload_speed_test(char *url, int concurrent, char *post_data, int post_len);
void request_init(void);
uint8_t request_handle(req_method *request);
void request_nameserver_reinit(void);

#endif
