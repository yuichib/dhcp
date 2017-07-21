/*
 * mydhcpc.c
 *
 *  Created on: 2008/12/26
 *      Author: yuichi
 */


#include <stdio.h>
#include <string.h>
#include <netinet/in.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <stdlib.h>
#include <netdb.h>
#include <time.h>
#include <sys/time.h>
#include <unistd.h>
#include "common.h"



//client stat
#define STAT_INIT       0
#define STAT_WAIT_OFFER 1
#define STAT_WAIT_REPLY 2
#define STAT_GET_IP     3

#define TTL_HOPE 15 //(sec)

//func
void print_stat(int status);
void Init(int *status,int *msgtype,struct itimerval *itimer);
void change_status(int *status,int stat);
void alrm_func(int sig);
void sigint_func(int sig);

//global var
int g_sigint = FALSE;//Ctrl_C flag

int
main(int argc,char *argv[])
{

	if( argc != 2){
		fprintf(stderr,"format: ./a.out server-IP-address\n");
		exit(-1);
	}

	int status;
	int msgtype;
	struct in_addr myip;//now
	struct in_addr mymask;//now
	struct in_addr zero_clear;
	zero_clear.s_addr = 0;
	uint16_t myttl;
	struct itimerval itimer;
	Init(&status,&msgtype,&itimer);

	//signal
	signal(SIGALRM,alrm_func);
	signal(SIGINT,sigint_func);

	int s;
	int real_rcvsize,real_sndsize;
	in_port_t srvport = SRVPORT;
	struct sockaddr_in myskt;//client
	struct sockaddr_in srvskt;//server
	struct hostent *srv_ent;
	struct Dhcp_msg rbuf;//receive buffer
	struct Dhcp_msg sbuf;//send buffer
	socklen_t srv_sktlen = sizeof(srvskt);

	/* client addr */
	memset(&myskt,0,sizeof(myskt));
	myskt.sin_family = AF_INET;
	myskt.sin_addr.s_addr = htonl(INADDR_ANY);//cli IP rely OS
	myskt.sin_port = htons(0);//cli port rely OS
	/* server addr */
	memset(&srvskt,0,sizeof(srvskt));
	srvskt.sin_family = AF_INET;
	if((srv_ent = gethostbyname(argv[1])) == NULL){
		herror("gethostbyname");
		exit(-1);
	}
	memcpy((char *)&srvskt.sin_addr,srv_ent->h_addr,srv_ent->h_length);
	srvskt.sin_port = htons(srvport);


	//create socket
	if((s = socket(AF_INET,SOCK_DGRAM,0)) < 0){
		perror("socket");
		exit(-1);
	}



	for(;;){
		//0 clear
		memset(&rbuf,0,sizeof(rbuf));
		memset(&sbuf,0,sizeof(sbuf));

		switch(status){
		case STAT_INIT:
			set_msg(&sbuf,DISCOVER,0,0,zero_clear,zero_clear);
			//send DISCOVER msg
			if((real_sndsize = sendto(s,&sbuf,sizeof(sbuf),0,
					(struct sockaddr *)&srvskt,sizeof(srvskt))) < 0){
				perror("sendto");
				exit(-1);
			}
			printf("sent message.\n");
			print_msg(sbuf);
			change_status(&status,STAT_WAIT_OFFER);//state change
			break;


		case STAT_WAIT_OFFER:
			//recv DHCP msg
			if((real_rcvsize = recvfrom(s,&rbuf,sizeof(rbuf),0,
					(struct sockaddr *)&srvskt,&srv_sktlen)) < 0){
				perror("recvfrom");
				exit(-1);
			}
			printf("message received.\n");
			print_msg(rbuf);
			//set msgtype
			msgtype = rbuf.type;
			if(msgtype == OFFER){
				if(rbuf.code == CODE_ASSIGN_OK){
					set_msg(&sbuf,REQUEST,CODE_REQUEST_ASSIGN,
							TTL_HOPE,rbuf.ip_addr,rbuf.netmask);
					//send REQUEST msg
					if((real_sndsize = sendto(s,&sbuf,sizeof(sbuf),0,
							(struct sockaddr *)&srvskt,sizeof(srvskt))) < 0){
						perror("sendto");
						exit(-1);
					}
					printf("sent message.\n");
					print_msg(sbuf);
					change_status(&status,STAT_WAIT_REPLY);//state change
				}
				else if(rbuf.code == CODE_ASSIGN_NOT){//error

				}
				else{//unknown code

				}
			}
			else{//unexpected msg

			}

			break;

		case STAT_WAIT_REPLY:
			//recv REPLY
			if((real_rcvsize = recvfrom(s,&rbuf,sizeof(rbuf),0,
					(struct sockaddr *)&srvskt,&srv_sktlen)) < 0){
				perror("recvfrom");
				exit(-1);
			}
			printf("message received.\n");
			print_msg(rbuf);
			//set msgtype
			msgtype = rbuf.type;

			if(msgtype == REPLY){
				if(rbuf.code == CODE_ASSIGN_OK){
					myip = rbuf.ip_addr;
					mymask = rbuf.netmask;
					myttl = rbuf.ttl;
					//set timer
					itimer.it_value.tv_sec = rbuf.ttl / 2;
					setitimer(ITIMER_REAL,&itimer,NULL);
					printf("GET IP and MASK !! After %d sec ,send req one more\n",
							itimer.it_value.tv_sec);
					print_ipAndmask(myip,mymask,myttl);
					change_status(&status,STAT_GET_IP);//state change
				}
				else if(rbuf.code == CODE_ASSIGNED_YET){//error

				}
				else{//unknown code

				}
			}
			else{//unexpected message

			}
			break;

		case STAT_GET_IP:
			printf("now I have IP address.\n");
			pause();//return any signal send
			//RELEASE SEND
			if(g_sigint == TRUE){
				set_msg(&sbuf,RELEASE,0,0,myip,zero_clear);
				myip = zero_clear;
				mymask = zero_clear;
				//send RELEASE msg
				if((real_sndsize = sendto(s,&sbuf,sizeof(sbuf),0,
						(struct sockaddr *)&srvskt,sizeof(srvskt))) < 0){
					perror("sendto");
					exit(-1);
				}
				printf("sent message.\n");
				print_msg(sbuf);
				change_status(&status,STAT_INIT);//state change
				exit(0);//client process end
			}
			//request extends IP when 1/2 TTL passed
			set_msg(&sbuf,REQUEST,CODE_REQUEST_EXTENSION,
					myttl,myip,mymask);
			//send REQUEST msg
			if((real_sndsize = sendto(s,&sbuf,sizeof(sbuf),0,
					(struct sockaddr *)&srvskt,sizeof(srvskt))) < 0){
				perror("sendto");
				exit(-1);
			}
			printf("sent message.\n");
			print_msg(sbuf);
			change_status(&status,STAT_WAIT_REPLY);

			break;
		default:
			//error
			fprintf(stderr,"UuDefined Status!!\n");
			break;
		}
		//switch end
	}
	//for loop end





	return 0;
}





void
print_stat(int status)
{
	switch(status){

	case STAT_INIT:
		printf("status is STAT_INIT\n");
		break;
	case STAT_WAIT_OFFER:
		printf("status is STAT_WAIT_OFFER\n");
		break;
	case STAT_WAIT_REPLY:
		printf("status is STAT_WAIT_REPLY\n");
		break;
	case STAT_GET_IP:
		printf("status is STAT_GET_IP\n");
		break;
	default:
		fprintf(stderr,"error print_stat() unknown stat");
		break;

	}

}

void
change_status(int *status,int stat)
{
	//before
	printf("before ");
	print_stat(*status);
	//change stat
	*status = stat;
	//after
	printf("after ");
	print_stat(*status);

}







void
Init(int *status,int *msgtype,struct itimerval *itimer)
{
	*status = STAT_INIT;
	*msgtype = UNKNOWN;
	itimer->it_value.tv_usec = 0;
	itimer->it_interval.tv_sec = 0;
	itimer->it_interval.tv_usec = 0;
}


void
alrm_func(int sig)
{
	if(sig == SIGALRM){
		printf("called alrm_func 1/2 passed\n");
	}
}

void
sigint_func(int sig)
{
	printf("SIGINT SEND\n");
	if(sig == SIGINT){
		g_sigint = TRUE;
	}

}
