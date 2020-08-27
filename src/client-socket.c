/*
 * client-socket.c
 *
 *  Created on: Jun 18, 2019
 *      Author: stria
 */

#include <libwebsockets.h>
#include "client-socket.h"

#define MAXLINE 1024

// Driver code

int connect_udp(int *sockfd, const char *ip, int port, int flag) {

	struct sockaddr_in servaddr;

	char buffer[MAXLINE];
	int n;
	// clear servaddr
	bzero(&servaddr, sizeof(servaddr));
	if(flag == SOCK_DGRAM){
	 	servaddr.sin_addr.s_addr = htonl(INADDR_ANY);
	} else{
	 	servaddr.sin_addr.s_addr = inet_addr(ip);
	}
	servaddr.sin_port = htons(port);
	servaddr.sin_family = AF_INET;
	// create datagram socket
	*sockfd = socket(AF_INET, flag, 0);

	struct timeval timeout;
	timeout.tv_sec = 3;
	timeout.tv_usec = 0;

	if (setsockopt(*sockfd, SOL_SOCKET, SO_RCVTIMEO, (char *) &timeout,
			sizeof(timeout)) < 0) {
		error("setsockopt failed\n");
		return 0;
	}

	if (setsockopt(*sockfd, SOL_SOCKET, SO_SNDTIMEO, (char *) &timeout,
			sizeof(timeout)) < 0) {
		error("setsockopt failed\n");
		return 0;
	}
	// connect to server
	if(flag == SOCK_DGRAM){
		char *message = "stb_wifi_remote";
		int len;
		const int opt = 1;
		memset(buffer,0,MAXLINE);
		setsockopt(*sockfd,SOL_SOCKET,SO_BROADCAST,&opt,sizeof(opt));
		sendto(*sockfd,message,strlen(message),MSG_CONFIRM,(struct sockaddr *) &servaddr,sizeof(servaddr));
		//for(;;){
			n = recvfrom(*sockfd,buffer,MAXLINE,MSG_WAITALL,(struct sockaddr *) &servaddr,sizeof(servaddr));
			buffer[n]='\n';
			lwsl_user("result :%s \n",buffer);
		//}
		//char *ipbuffer = inet_ntoa(servaddr.sin_addr);
		//lwsl_user("ipbuffer :%s \n",ipbuffer);


	} else{
		if (connect(*sockfd, (struct sockaddr *) &servaddr, sizeof(servaddr)) < 0) {
			return 0;
		}
		n = read(*sockfd, buffer, MAXLINE);
	}
	return 1;
}

void discovery(int *sockfd, char *message) {
	char buffer[1024];
	int n;

	send(*sockfd, message, strlen(message), 0);
	n = read(*sockfd, buffer, MAXLINE);
	buffer[n] = '\0';
	lwsl_user("result :%s \n",buffer);
}

void stream_udp(int *sockfd, char *message,char * buffer) {
	int n;

	send(*sockfd, message, strlen(message), 0);

	n = read(*sockfd, buffer, MAXLINE);
	buffer[n] = '\0';
	lwsl_user("result :%s \n",buffer);
}

void setchannel_udp(int *sockfd, char *message) {

	char buffer[1024];
	int n;
	send(*sockfd, message, strlen(message), 0);
}

void close_udp(int *sockfd) {

	close(*sockfd);
}

