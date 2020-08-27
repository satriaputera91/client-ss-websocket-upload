/*
 * http-request.c
 *
 *  Created on: Apr 18, 2019
 *      Author: stria
 */

#include "http-request.h"


int http_get(char *url, char **result,int b_result) {
	CURL *curl;
	CURLcode res;
	struct string s;
	long response_code;

	init_str(&s);
	curl_global_init(CURL_GLOBAL_DEFAULT);

	curl = curl_easy_init();
	if (curl) {
		curl_easy_setopt(curl, CURLOPT_URL, url);

#ifdef SKIP_PEER_VERIFICATION
		/*
		 * If you want to connect to a site who isn't using a certificate that is
		 * signed by one of the certs in the CA bundle you have, you can skip the
		 * verification of the server's certificate. This makes the connection
		 * A LOT LESS SECURE.
		 *
		 * If you have a CA cert for the server stored someplace else than in the
		 * default bundle, then the CURLOPT_CAPATH option might come handy for
		 * you.
		 */
		curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
#endif

#ifdef SKIP_HOSTNAME_VERIFICATION
		/*
		 * If the site you're connecting to uses a different host name that what
		 * they have mentioned in their server certificate's commonName (or
		 * subjectAltName) fields, libcurl will refuse to connect. You can skip
		 * this check, but this will make the connection less secure.
		 */
		curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);
#endif
		/*Set Response output*/

		curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, *writefunc);
		curl_easy_setopt(curl, CURLOPT_WRITEDATA, &s);
		/* Perform the request, res will get the return code */
		res = curl_easy_perform(curl);

		/* Check for errors */
		if (res != CURLE_OK) {
			fprintf(stderr, "curl_easy_perform() failed: %s\n",
					curl_easy_strerror(res));
			return 1;
		} else {

			curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &response_code);

		}

		/*Response data*/
		if(b_result){
			*result = (char *) malloc((s.len + 1) * sizeof(char));
			strcpy(*result, s.ptr);
			//printf("output :%s\n", s.ptr);
			free(s.ptr);
		}
		/* always cleanup */
		curl_easy_cleanup(curl);
	}

	curl_global_cleanup();

	return response_code;;

}

int http_put(char *url, char *json_struct){
	CURL *curl;
	CURLcode res;
	FILE * hd_src;
	struct stat file_info;

	char *file;
	//char *url;

	//if(argc < 3)
	//	return 1;

	//file = argv[1];
	//url = argv[2];

	long response_code;

	struct string s;
	//struct WriteThis wt;

	init_str(&s);
	//wt.readptr = message;
	////wt.sizeleft = strlen(message);
	//
	/* get the file size of the local file */
	//stat(file, &file_info);

	/* get a FILE * of the same file, could also be made with
	 * fdopen() from the previous descriptor, but hey this is just
	 * an example! */
	//hd_src = fopen(file, "rb");

	/* In windows, this will init the winsock stuff */
	curl_global_init(CURL_GLOBAL_ALL);

	struct curl_slist *headers = NULL;

	/* get a curl handle */
	curl = curl_easy_init();

	if(curl) {
		/* we want to use our own read function */
		curl_easy_setopt(curl, CURLOPT_READFUNCTION, read_callback);

		/* enable uploading */
		//curl_easy_setopt(curl, CURLOPT_UPLOAD, 1L);

		/* HTTP PUT please */
		//curl_easy_setopt(curl, CURLOPT_PUT, 1L);

		/*Set cerficate*/
                //curl_easy_setopt(curl, CURLOPT_SSLCERTTYPE, "PEM");
                curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
                curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);

		/* Set Headers  */
		//headers = curl_slist_append(headers, client_id_header);
		headers = curl_slist_append(headers, "Content-Type: application/json");

		curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers); 

		/* specify target URL, and note that this URL should include a file
		 * name, not only a directory */
		curl_easy_setopt(curl, CURLOPT_URL, url);

		/* Put Custom Request */
		curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "PUT"); /* !!! */
		curl_easy_setopt(curl, CURLOPT_POSTFIELDS, json_struct); /* data goes here */

		/* now specify which file to upload */
		//curl_easy_setopt(curl, CURLOPT_READDATA, hd_src);

		/* provide the size of the upload, we specicially typecast the value
		 * to curl_off_t since we must be sure to use the correct data size */
		//curl_easy_setopt(curl, CURLOPT_INFILESIZE_LARGE,
		//	(curl_off_t)file_info.st_size);

		/* Now run off and do what you've been told! */
		res = curl_easy_perform(curl);
		/* Check for errors */
		if(res != CURLE_OK)
			fprintf(stderr, "curl_easy_perform() failed: %s\n",
				curl_easy_strerror(res));

		/*Response data*/
                //if(b_result){
		//*result = (char *) malloc((s.len + 1) * sizeof(char));
		//strcpy(*result, s.ptr);
		//printf("output :%s\n", s.ptr);
		//free(s.ptr);
		//}

		curl_slist_free_all(headers);

		/* always cleanup */
		curl_easy_cleanup(curl);
	}

	//fclose(hd_src); /* close the local file */

	curl_global_cleanup();

	return response_code;

}

int http_post(char *url, char * token, char *message, char **result) {

	CURLcode rv;
	CURL *curl;
	long response_code;

	struct string s;
	//struct WriteThis wt;

	init_str(&s);
	//wt.readptr = message;
	//wt.sizeleft = strlen(message);

	curl_global_init(CURL_GLOBAL_ALL);

	curl = curl_easy_init();
	struct curl_slist *headers = NULL;

	if (curl) {
		/*Set Header*/
		//headers = curl_slist_append(headers, char_url->authorization);
		//headers = curl_slist_append(headers, char_url->content_type);
		headers = curl_slist_append(headers, "Accept: application/json");
		headers = curl_slist_append(headers,
				"Content-Type: application/json;ty=4");
		//headers = curl_slist_append(headers, "charsets: utf-8");
		if (token != NULL) {
			headers = curl_slist_append(headers, token);
		}

		headers = curl_slist_append(headers, "Expect:");

		curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);

		/* Set Request mode*/
		//curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "POST");
		/*Set cerficate*/
		//curl_easy_setopt(curl, CURLOPT_SSLCERTTYPE, "PEM");
		curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
		curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);
		/* set URL*/
		curl_easy_setopt(curl, CURLOPT_URL, url);

		/*Set Read*/
		/* Now specify we want to POST data */

		//curl_easy_setopt(curl, CURLOPT_READFUNCTION, read_callback);
		//curl_easy_setopt(curl, CURLOPT_READDATA, &wt);
		curl_easy_setopt(curl, CURLOPT_POSTFIELDS, message);
		//curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE_LARGE, (long )wt.sizeleft);

		/*set Timeout*/
		curl_easy_setopt(curl, CURLOPT_TIMEOUT, 5);
		/*Set Response output*/
		curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, *writefunc);
		curl_easy_setopt(curl, CURLOPT_WRITEDATA, &s);

		/* Perform the request, res will get the return code*/
		rv = curl_easy_perform(curl);

		curl_slist_free_all(headers);

		/*Check for errors*/
		if (rv != CURLE_OK) {
			fprintf(stderr, "curl_easy_perform() failed: %s\n",
					curl_easy_strerror(rv));
			return 1;

		} else {
			curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &response_code);
			//printf("URL : %d \n", response_code);

		}

		/*Response data*/
		*result = (char *) malloc((s.len + 1) * sizeof(char));

		strcpy(*result, s.ptr);
		//printf("output :%s\n", s.ptr);
		free(s.ptr);

		/*always cleanup*/
		curl_easy_cleanup(curl);
	}

	curl_global_cleanup();

	return response_code;
}
