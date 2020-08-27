/*
 * client-socket.h
 *
 *  Created on: Jun 18, 2019
 *      Author: stria
 */
#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <stdlib.h>
#include <uuid/uuid.h>

#ifndef CLIENT_SOCKET_H_
#define CLIENT_SOCKET_H_

int connect_udp(int *sockfd, const char *ip, int port,int flag);
void stream_udp(int *sockfd, char *message,char *buffer);
void close_udp(int *sockfd);


#endif /* CLIENT_SOCKET_H_ */
