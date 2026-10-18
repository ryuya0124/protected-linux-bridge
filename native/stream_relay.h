/* MIT - Copyright (c) 2026 ryuya0124. Bounded raw-byte relay, no command parsing. */
#ifndef PLB_STREAM_RELAY_H
#define PLB_STREAM_RELAY_H
#include <poll.h>
#include <fcntl.h>
#include <errno.h>
#include <unistd.h>
#include <sys/socket.h>
#include <time.h>
#include <string.h>
static double plb_now(void){struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return t.tv_sec+t.tv_nsec/1e9;}
static int plb_write_all(int fd,const void *data,size_t size){const char *p=data;while(size){ssize_t n=write(fd,p,size);if(n<0&&errno==EINTR)continue;if(n<=0)return -1;p+=n;size-=(size_t)n;}return 0;}
static void plb_relay(int first,int second,int seconds,int terminal){
 int fd[2]={first,second},end[2]={0},half[2]={0};unsigned char data[2][16384];size_t used[2]={0};double deadline=plb_now()+seconds;
 for(int i=0;i<2;i++){int flags=fcntl(fd[i],F_GETFL);if(flags<0||fcntl(fd[i],F_SETFL,flags|O_NONBLOCK)<0)return;}
 while(plb_now()<deadline){
  for(int i=0;i<2;i++)if(end[i]&&!used[i]&&!half[i]){shutdown(fd[1-i],SHUT_WR);half[i]=1;}
  if(end[0]&&end[1]&&!used[0]&&!used[1])break;
  struct pollfd p[2];for(int i=0;i<2;i++)p[i]=(struct pollfd){fd[i],(short)((!end[i]&&used[i]<sizeof(data[i])?POLLIN:0)|(used[1-i]?POLLOUT:0)),0};
  int n=poll(p,2,500);if(n<0){if(errno==EINTR)continue;break;}
  for(int i=0;i<2;i++){
   if((p[i].revents&(POLLIN|POLLHUP|POLLERR))&&!end[i]&&used[i]<sizeof(data[i])){
    ssize_t r=read(fd[i],data[i]+used[i],sizeof(data[i])-used[i]);
    if(r>0)used[i]+=(size_t)r;else if(!r||(r<0&&errno!=EAGAIN&&errno!=EWOULDBLOCK&&errno!=EINTR)){end[i]=1;if(terminal){end[0]=end[1]=1;double flush=plb_now()+2;if(deadline>flush)deadline=flush;}}
   }
   if(p[i].revents&POLLOUT){int from=1-i;ssize_t w=write(fd[i],data[from],used[from]);if(w>0){used[from]-=(size_t)w;memmove(data[from],data[from]+w,used[from]);}else if(w<0&&errno!=EAGAIN&&errno!=EWOULDBLOCK&&errno!=EINTR)return;}
   if(p[i].revents&POLLNVAL)return;
  }
 }
}
#endif
