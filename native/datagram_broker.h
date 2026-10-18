/* MIT. UDP and IPv4 echo only. Runs after UID/capability drop; no raw socket. */
#ifndef PLB_DATAGRAM_BROKER_H
#define PLB_DATAGRAM_BROKER_H
#ifndef PLB_NETWORK_LIFETIME
#define PLB_NETWORK_LIFETIME 1800
#endif
#include <netinet/ip_icmp.h>
#include "datagram_protocol.h"
static int plb_echo_request_valid(const unsigned char *data,size_t size){return size>=8&&size<=1400&&data[0]==ICMP_ECHO&&data[1]==0&&!plb_icmp_checksum(data,size);}
static void plb_datagram_broker(int client){
 alarm(15);unsigned char head[6],ready[PLB_DGRAM_READY_SIZE]={1};char host[254];
 if(plb_read_exact(client,head,6)||head[0]||head[1]!=PLB_DGRAM_VERSION||(head[2]!='U'&&head[2]!='I')||!head[5]||head[5]>253)return;
 unsigned port=((unsigned)head[3]<<8)|head[4];int echo=head[2]=='I';
 if((echo&&port)||(!echo&&!port)||plb_read_exact(client,host,head[5])||!plb_valid_host((unsigned char*)host,head[5]))return;host[head[5]]=0;
 struct addrinfo hints={.ai_family=echo?AF_INET:AF_UNSPEC,.ai_socktype=SOCK_DGRAM},*addresses=NULL;char service[6];snprintf(service,sizeof(service),"%u",port);
 if(getaddrinfo(host,echo?NULL:service,&hints,&addresses)){ready[0]=3;plb_write_all(client,ready,sizeof(ready));return;}
 int remote=-1;ready[0]=2;
 for(struct addrinfo *a=addresses;a;a=a->ai_next){
  if(forbidden_address(a->ai_addr))continue;
  remote=socket(a->ai_family,SOCK_DGRAM|SOCK_CLOEXEC,echo?IPPROTO_ICMP:IPPROTO_UDP);
  if(remote<0){ready[0]=errno==EACCES||errno==EPERM?5:4;continue;}
  if(connect(remote,a->ai_addr,a->ai_addrlen)){ready[0]=4;close(remote);remote=-1;continue;}
  ready[0]=0;ready[1]=a->ai_family==AF_INET?4:6;
  memcpy(ready+2,a->ai_family==AF_INET?(void*)&((struct sockaddr_in*)a->ai_addr)->sin_addr:(void*)&((struct sockaddr_in6*)a->ai_addr)->sin6_addr,ready[1]==4?4:16);
  ready[18]=head[3];ready[19]=head[4];break;
 }
 freeaddrinfo(addresses);struct timeval timeout={2,0};setsockopt(client,SOL_SOCKET,SO_RCVTIMEO,&timeout,sizeof(timeout));setsockopt(client,SOL_SOCKET,SO_SNDTIMEO,&timeout,sizeof(timeout));
 if(plb_write_all(client,ready,sizeof(ready))||remote<0){if(remote>=0)close(remote);return;}
 fprintf(stderr,"DGRAM_ACCEPTED mode=%c uid=%d\n",head[2],getuid());alarm(0);
 unsigned char data[PLB_DGRAM_MAX];double deadline=plb_now()+PLB_NETWORK_LIFETIME,idle=plb_now()+60,last_echo=0;unsigned budget=64;
 while(plb_now()<deadline&&plb_now()<idle){
  struct pollfd p[2]={{client,POLLIN,0},{remote,POLLIN,0}};int result=poll(p,2,1000);if(result<0){if(errno==EINTR)continue;break;}
  if(p[0].revents&(POLLIN|POLLHUP|POLLERR)){
   size_t size;if(plb_dgram_read(client,data,sizeof(data),&size))break;
   if(echo){double now=plb_now();if(!budget||now-last_echo<0.15||!plb_echo_request_valid(data,size))break;last_echo=now;budget--;}
   ssize_t sent=send(remote,data,size,MSG_DONTWAIT);if(sent<0||(size_t)sent!=size)break;idle=plb_now()+60;
  }
  if(p[1].revents&(POLLIN|POLLERR|POLLHUP)){
   ssize_t size=recv(remote,data,sizeof(data),MSG_DONTWAIT|MSG_TRUNC);if(size<0){if(errno==EINTR||errno==EAGAIN)continue;break;}if(size>PLB_DGRAM_MAX)continue;
   if(echo&&(size<8||data[0]!=ICMP_ECHOREPLY||data[1]!=0))continue;
   if(plb_dgram_write(client,data,(size_t)size))break;idle=plb_now()+60;
  }
 }
 close(remote);
}
#endif
