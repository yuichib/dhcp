/*
 * test.c
 *
 *  Created on: 2008/12/22
 *      Author: yuichi
 */

#include <stdio.h>
#include <string.h>
#include <netinet/in.h>
#include <sys/types.h>
#include <sys/socket.h>



int
main(int argc,char *argv[])
{
	int s;
	int real_rcvsize,real_sndsize;
	in_port_t myport = 10010;
	struct sockaddr_in myskt;//server
	struct sockaddr_in cliskt;//client
	char rbuf[BUFSIZ];
	char sbuf[BUFSIZ];
	socklen_t cli_sktlen;

	if((s = socket(AF_INET,SOCK_DGRAM,0)) < 0){
		perror("socket");
		exit(-1);
	}
	//srv addr
	memset(&myskt,0,sizeof(myskt));
	myskt.sin_family = AF_INET;
	myskt.sin_port = htons(myport);
	myskt.sin_addr.s_addr = htonl(INADDR_ANY);


	if( bind(s,(struct sockaddr *)&myskt,sizeof(myskt)) < 0 ){
		perror("bind");
		exit(-1);
	}

	cli_sktlen = sizeof(cliskt);
	while(1){

		if((real_rcvsize = recvfrom(s,rbuf,sizeof(rbuf),0,
				(struct sockaddr *)&cliskt,&cli_sktlen)) < 0){
			perror("recvfrom");
			exit(-1);
		}
		if(strcmp(rbuf,"exit") == 0){
			break;
		}
		printf("recv_msg:%s\n",rbuf);
		strcat(rbuf," + from server!");
		strcpy(sbuf,rbuf);

		if((real_sndsize = sendto(s,sbuf,sizeof(sbuf),0,
				(struct sockaddr *)&cliskt,sizeof(cliskt))) < 0){
			perror("sendto");
			exit(-1);
		}
	}

	return 0;
}




