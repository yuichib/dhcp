/*
 * mydhcpd.c
 *
 *  Created on: 2008/12/24
 *      Author: yuichi
 */

#include <stdio.h>
#include <string.h>
#include <netinet/in.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <stdlib.h>
#include <time.h>
#include <sys/time.h>
#include <unistd.h>
#include "common.h"

//define
#define MAX_OFFER 20
#define CANUSE_IP_TIME 30 //sec


//srvstat
#define STAT_INIT 		   0
#define STAT_WAIT_DISCOVER 1
#define STAT_WAIT_REQUEST  2


//struct
struct Offer{
	struct in_addr ip_addr;
	struct in_addr netmask;
	struct Offer *next;
	struct Offer *back;
};


//func
int setOffer(char str[],struct Offer *offer);
struct Offer *search_offers(struct in_addr ip_addr);
struct Offer *search_list(struct Offer *head,struct in_addr ip_addr);
void insert_tail(struct Offer *h,struct Offer *p);
void remove_head(struct Offer *h);
void Init(struct Offer *head,int *status,int *msgtype,struct itimerval *itimer);
int has_assign_ip(struct Offer *head);
void set_assign_ip(struct Offer *head,struct Dhcp_msg *sbuf);
void change_status(int *status,int stat);
void print_stat(int status);
void print_list(struct Offer *head);
int collect_ip(struct Offer *h,struct in_addr ip_addr);


//global var
struct Offer g_offers[MAX_OFFER];//have all IP&mask
int g_offer_num = 0;
//for SIGALRM
struct Offer *g_phead;
struct in_addr g_dead_ip;

void alrm_func(int sig);


int
main(int argc,char *argv[])
{

	struct Offer head;
	int status;
	int msgtype;
	struct itimerval itimer;
	Init(&head,&status,&msgtype,&itimer);
	//SIGNAL
	signal(SIGALRM,alrm_func);

	/* include config-file */
	FILE *fp;
	char tmp[64];

	if(argc != 2){
		fprintf(stderr,"format: ./a.out config-file\n");
		exit(-1);
	}
	if((fp = fopen(argv[1],"r")) == NULL){
		fprintf(stderr,"Can't Open File %s\n",argv[1]);
		exit(-1);
	}
	while(1){
		if(fgets(tmp,sizeof(tmp),fp) == NULL){
			if(feof(fp)){
				break;
			}
			else{
				fprintf(stderr,"fgets error!!\n");
				exit(-1);
			}
		}
		if(setOffer(tmp,&g_offers[g_offer_num]) < 0){
			fprintf(stderr,"set Offer error!!\n");
			exit(-1);
		}
		/*printf("%s\n",tmp);
		printf("ip:%d mask:%d\n",offers[g_num].ip_addr,offers[g_num].netmask);
		printf("ip:%s\n",inet_ntoa(offers[g_num].ip_addr));
		printf("mask:%s\n",inet_ntoa(offers[g_num].netmask));*/

		insert_tail(&head,&g_offers[g_offer_num]);

		g_offer_num++;
	}
	fclose(fp);
	/* include config-file end */


	struct Offer *p,*q;
	p = &head;
	q = p->back->back;
	print_list(&head);
	printf("\n");
	remove_head(&head);
	print_list(&head);
	printf("\n");
	remove_head(&head);
	print_list(&head);
	printf("\n");

	struct in_addr t;
	t.s_addr = q->ip_addr.s_addr;
	insert_tail(&head,search_offers(t));
	print_list(&head);


	int s;
	int real_rcvsize,real_sndsize;
	in_port_t myport = SRVPORT;
	struct sockaddr_in myskt;//server
	struct sockaddr_in cliskt;//client
	struct Dhcp_msg rbuf;//receive buffer
	struct Dhcp_msg sbuf;//send buffer
	socklen_t cli_sktlen = sizeof(cliskt);

	//create socket
	if((s = socket(AF_INET,SOCK_DGRAM,0)) < 0){
		perror("socket");
		exit(-1);
	}
	//set srv addr
	memset(&myskt,0,sizeof(myskt));
	myskt.sin_family = AF_INET;
	myskt.sin_port = htons(myport);
	myskt.sin_addr.s_addr = htonl(INADDR_ANY);//rely OS
	//bind
	if( bind(s,(struct sockaddr *)&myskt,sizeof(myskt)) < 0 ){
		perror("bind");
		exit(-1);
	}

	change_status(&status,STAT_WAIT_DISCOVER);//change stat
	for(;;){
		print_list(&head);
		//0 clear
		memset(&rbuf,0,sizeof(rbuf));
		memset(&sbuf,0,sizeof(sbuf));
		//recv DHCP msg
		if((real_rcvsize = recvfrom(s,&rbuf,sizeof(rbuf),0,
				(struct sockaddr *)&cliskt,&cli_sktlen)) < 0){
			perror("recvfrom");
			continue;
		}
		printf("message received.\n");
		print_msg(rbuf);

		//set msgtype
		msgtype = rbuf.type;

		switch(status){
		case STAT_WAIT_DISCOVER:
			if(msgtype == DISCOVER){
				sbuf.type = OFFER;
				//select offer IP addr
				if(has_assign_ip(&head) == TRUE){
					sbuf.code = CODE_ASSIGN_OK;
					sbuf.ttl = CANUSE_IP_TIME;
					set_assign_ip(&head,&sbuf);//not remove
				}
				else{
					sbuf.code = CODE_ASSIGN_NOT;
				}
				//send OFFER msg
				if((real_sndsize = sendto(s,&sbuf,sizeof(sbuf),0,
						(struct sockaddr *)&cliskt,sizeof(cliskt))) < 0){
					perror("sendto");
					exit(-1);
				}
				printf("sent message.\n");
				print_msg(sbuf);
				printf("Offered IP and MASK.\n");
				print_ipAndmask(sbuf.ip_addr,sbuf.netmask,sbuf.ttl);
				change_status(&status,STAT_WAIT_REQUEST);//state change
			}
			else if(msgtype == REQUEST){
				if(rbuf.code == CODE_REQUEST_EXTENSION){
					//send REPLY msg
					set_msg(&sbuf,REPLY,CODE_ASSIGN_OK,CANUSE_IP_TIME,
							rbuf.ip_addr,rbuf.netmask);
					if((real_sndsize = sendto(s,&sbuf,sizeof(sbuf),0,
							(struct sockaddr *)&cliskt,sizeof(cliskt))) < 0){
						perror("sendto");
						exit(-1);
					}
					printf("sent message.\n");
					printf("IP TTL extends\n");
					print_msg(sbuf);
					//set timer
					g_dead_ip = rbuf.ip_addr;
					itimer.it_value.tv_sec = sbuf.ttl;
					setitimer(ITIMER_REAL,&itimer,NULL);

				}
				else{
					fprintf(stderr,"unexpected code %d\n",rbuf.code);
				}

			}
			else if(msgtype == RELEASE){
				//release timer
				itimer.it_value.tv_sec = 0;
				setitimer(ITIMER_REAL,&itimer,NULL);
				//collect IP
				if(search_list(&head,rbuf.ip_addr) == NULL){
					if(collect_ip(&head,rbuf.ip_addr) < 0){
						fprintf(stderr,"error collect_ip() Such IP Unknown\n");
					}
				}

			}
			else{//unexpected msg


			}
			break;

		case STAT_WAIT_REQUEST:
			if(msgtype == REQUEST){
				sbuf.type = REPLY;
				if(search_list(&head,rbuf.ip_addr) != NULL){
					sbuf.code = CODE_ASSIGN_OK;
					sbuf.ttl = CANUSE_IP_TIME;
					set_assign_ip(&head,&sbuf);//not remove
					remove_head(&head);//remove
					//set timer
					g_dead_ip = rbuf.ip_addr;
					itimer.it_value.tv_sec = sbuf.ttl;
					setitimer(ITIMER_REAL,&itimer,NULL);

				}
				else{//error
					sbuf.code = CODE_ASSIGNED_YET;
				}
				//send REPLY msg
				if((real_sndsize = sendto(s,&sbuf,sizeof(sbuf),0,
						(struct sockaddr *)&cliskt,sizeof(cliskt))) < 0){
					perror("sendto");
					exit(-1);
				}
				printf("sent message.\n");
				print_msg(sbuf);
				printf("Assigned IP and MASK.\n");
				print_ipAndmask(sbuf.ip_addr,sbuf.netmask,sbuf.ttl);
				change_status(&status,STAT_WAIT_DISCOVER);//state change
			}
			else{//unexpected msg

			}

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
insert_tail(struct Offer *h,struct Offer *p)
{
	p->next = h->next;
	p->back = h;
	h->next->back = p;
	h->next = p;

}

void
remove_head(struct Offer *h)
{
	struct Offer *rm_head = h->back;

	h->back = rm_head->back;
	h->back->next = h;
	rm_head->next = NULL;
	rm_head->back = NULL;


}



int
setOffer(char *str,struct Offer *offer)
{
	int i=0;
	char ip[64],mask[64];
	while(*str == ' ' || *str == '\t')//blank
		str++;
	while(*str != ' ' && *str != '\t' && *str != '\n'){
		ip[i++] = *str++;
	}
	ip[i] = '\0';
	i = 0;
	while(*str == ' ' || *str == '\t')//blank
		str++;
	while(*str != ' ' && *str != '\t' && *str != '\n'){
		mask[i++] = *str++;
	}
	mask[i] = '\0';

	//convert string_ip -> binary_ip
	if(inet_aton(ip,&offer->ip_addr) != 1){
		perror("inet_aton");
		return -1;
	}
	if(inet_aton(mask,&offer->netmask) != 1){
		perror("inet_aton");
		return -1;
	}

	return 1;

}


int
collect_ip(struct Offer *h,struct in_addr ip_addr)
{
	struct Offer *p;
	if((p = search_offers(ip_addr)) == NULL){
		return -1;//error
	}
	insert_tail(h,p);
	return 1;

}


struct Offer *
search_offers(struct in_addr ip_addr)
{
	int i;
	for(i=0; i<g_offer_num; i++){
		if(ip_addr.s_addr == g_offers[i].ip_addr.s_addr)
			return &g_offers[i];
	}
	return NULL;
}


struct Offer *
search_list(struct Offer *head,struct in_addr ip_addr)
{
	struct Offer *p;
	for(p=head->back; p!=head; p=p->back){
		if(p->ip_addr.s_addr == ip_addr.s_addr)
			return p;
	}

	return NULL;

}

void
Init(struct Offer *head,int *status,int *msgtype,struct itimerval *itimer)
{
	head->next = head;
	head->back = head;
	head->ip_addr.s_addr = 0;
	head->netmask.s_addr = 0;
	*status = STAT_INIT;
	*msgtype = UNKNOWN;
	itimer->it_value.tv_usec = 0;
	itimer->it_interval.tv_sec = 0;
	itimer->it_interval.tv_usec = 0;
	g_phead = head;
}

int
has_assign_ip(struct Offer *head)
{
	if(head->next == head && head->back == head){
		return FALSE;
	}
	return TRUE;
}


void
set_assign_ip(struct Offer *head,struct Dhcp_msg *sbuf)
{
	sbuf->ip_addr = head->back->ip_addr;
	sbuf->netmask = head->back->netmask;


}



void
print_stat(int status)
{
	switch(status){
	case STAT_INIT:
		printf("status is STAT_INIT\n");
		break;
	case STAT_WAIT_DISCOVER:
		printf("status is STAT_WAIT_DISCOVER\n");
		break;
	case STAT_WAIT_REQUEST:
		printf("status is STAT_WAIT_REQUEST\n");
		break;
	default:
		fprintf(stderr,"error print_stat() unknown stat\n");
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
print_list(struct Offer *head)
{
	struct Offer *p;
	printf("head.\n");
	for(p=head->back; p!=head; p=p->back){
		printf("%s\n",inet_ntoa(p->ip_addr));
	}
	printf("tail.\n");

}

void
alrm_func(int sig)
{
	printf("alrm_func() called.\n");

	if(sig == SIGALRM){
		printf("IP ADDRESS %s is no more used. So I Will Collect IP.\n",
				inet_ntoa(g_dead_ip));
		if(search_list(g_phead,g_dead_ip) == NULL){
			if(collect_ip(g_phead,g_dead_ip) < 0){
				fprintf(stderr,"error collect_ip(). Such IP Unknown.\n");
			}
		}
		g_dead_ip.s_addr = 0;
		print_list(g_phead);
	}

}


