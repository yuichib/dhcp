/*
 * udp_cli.c
 *
 *  Created on: 2008/12/22
 *      Author: yuichi
 */

#include <stdio.h>
#include <string.h>
#include <netinet/in.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <stdlib.h>
#include <netdb.h>




int
main(int argc,char *argv[])
{
	int s;
	int real_rcvsize,real_sndsize;
	in_port_t myport = 0; //rely OS
	in_port_t srvport;
	struct sockaddr_in srvskt;//server
	struct hostent *srv_ent;
	struct sockaddr_in myskt;//client
	char srv_hostname[64];
	char rbuf[BUFSIZ];//receive buffer
	char sbuf[BUFSIZ];//send buffer

	socklen_t srv_sktlen;

	if((s = socket(AF_INET,SOCK_DGRAM,0)) < 0){
		perror("socket");
		exit(-1);
	}

	/* client addr */
	memset(&myskt,0,sizeof(myskt));
	myskt.sin_family = AF_INET;
	myskt.sin_addr.s_addr = htonl(INADDR_ANY);//cli IP rely OS
	myskt.sin_port = htons(myport);//cli port rely OS
	/* server addr */
	memset(&srvskt,0,sizeof(srvskt));
	srvskt.sin_family = AF_INET;
	printf("please input server host name = ");//srv hostname
	scanf("%s",srv_hostname);
	if((srv_ent = gethostbyname(srv_hostname)) == NULL){
		herror("gethostbyname");
		exit(-1);
	}
	memcpy((char *)&srvskt.sin_addr,srv_ent->h_addr,srv_ent->h_length);
	printf("please input server port number = ");//srv port
	scanf("%d",&srvport);
	srvskt.sin_port = htons(srvport);
	srv_sktlen = sizeof(srvskt);

	while(1){
		printf("message = ");
		scanf("%s",sbuf);
		if((real_sndsize = sendto(s,sbuf,sizeof(sbuf),0,
				(struct sockaddr *)&srvskt,sizeof(srvskt))) < 0){
			perror("sendto");
			exit(-1);
		}
		if(strcmp(sbuf,"exit") == 0){
			break;
		}
		if((real_rcvsize = recvfrom(s,rbuf,sizeof(rbuf),0,
				(struct sockaddr *)&srvskt,&srv_sktlen)) < 0){
			perror("recvfrom");
			exit(-1);
		}
		printf("recv = %s\n",rbuf);
	}

	close(s);

	exit(0);
}




