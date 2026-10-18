/* MIT. Versioned datagram records over the VM-only stream, not an IP tunnel. */
#ifndef PLB_DATAGRAM_PROTOCOL_H
#define PLB_DATAGRAM_PROTOCOL_H
#include <stdint.h>
#include <unistd.h>
#include <errno.h>
#include <stddef.h>
#include <string.h>
#define PLB_DGRAM_MAX 65507
#define PLB_DGRAM_VERSION 1
#define PLB_DGRAM_READY_SIZE 20
static int plb_read_exact(int fd,void *data,size_t length){
 unsigned char *p=data;while(length){ssize_t n=read(fd,p,length);if(n<0&&errno==EINTR)continue;if(n<=0)return -1;p+=n;length-=(size_t)n;}return 0;
}
static int plb_dgram_read(int fd,void *data,size_t capacity,size_t *size){
 unsigned char head[2];if(plb_read_exact(fd,head,2))return -1;size_t n=((size_t)head[0]<<8)|head[1];if(n>PLB_DGRAM_MAX||n>capacity)return -1;
 if(plb_read_exact(fd,data,n))return -1;*size=n;return 0;
}
static int plb_dgram_write(int fd,const void *data,size_t size){
 if(size>PLB_DGRAM_MAX)return -1;unsigned char head[2]={(unsigned char)(size>>8),(unsigned char)size};return plb_write_all(fd,head,2)||plb_write_all(fd,data,size)?-1:0;
}
static int plb_valid_host(const unsigned char *host,size_t size){
 if(!size||size>253)return 0;for(size_t i=0;i<size;i++){unsigned char c=host[i];if(!((c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||c=='.'||c=='-'||c==':'||c=='_'))return 0;}return 1;
}
static uint16_t plb_icmp_checksum(const unsigned char *p,size_t n){
 uint32_t sum=0;while(n>1){sum+=((unsigned)p[0]<<8)|p[1];p+=2;n-=2;}if(n)sum+=(unsigned)*p<<8;while(sum>>16)sum=(sum&65535)+(sum>>16);return (uint16_t)~sum;
}
#endif
