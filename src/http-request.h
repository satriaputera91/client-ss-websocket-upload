/*
 * http-request.h
 *
 *  Created on: Apr 18, 2019
 *      Author: stria
 */

#ifndef HTTP_REQUEST_H_
#define HTTP_REQUEST_H_

#include <curl/curl.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <fcntl.h>
#include <sys/stat.h>

struct string {
	char *ptr;
	size_t len;
};

static void init_str(struct string *s) {
	s->len = 0;
	s->ptr = malloc(s->len + 1);
	if (s->ptr == NULL) {
		fprintf(stderr, "malloc() failed\n");
	}
	s->ptr[0] = '\0';
}

static size_t writefunc(void *ptr, size_t size, size_t nmemb, struct string *s) {
	size_t new_len = s->len + size * nmemb;
	s->ptr = realloc(s->ptr, new_len + 1);
	if (s->ptr == NULL) {
		fprintf(stderr, "realloc() failed\n");
	}
	memcpy(s->ptr + s->len, ptr, size * nmemb);
	s->ptr[new_len] = '\0';
	s->len = new_len;

	return size * nmemb;
}

static size_t read_callback(void *ptr, size_t size, size_t nmemb, void *stream){
	size_t retcode;
	curl_off_t nread;

	/* in real-world cases, this would probably get this data differently
	as this fread() stuff is exactly what the library already would do
	by default internally */
	retcode = fread(ptr, size, nmemb, stream);

	nread = (curl_off_t)retcode;

	fprintf(stderr, "*** We read %" CURL_FORMAT_CURL_OFF_T
		" bytes from file\n", nread);

	return retcode;
}


int http_get(char *url_opt, char **result,int b_result);
int http_post(char *url, char * token, char *message, char **result);
int http_put(char *url, char *json_struct);

#endif /* HTTP_REQUEST_H_ */
