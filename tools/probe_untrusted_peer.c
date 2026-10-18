/* MIT. Run as a UID other than root, Terminal, or this module. Expects rejection. */
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/time.h>
#include <unistd.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
int main(void){
 if(getuid()==0){fputs("Refusing root: this tests an untrusted UID\n",stderr);return 2;}
 const char name[]="net.ryuya_dev.protectedlinux.console";
 struct sockaddr_un a={.sun_family=AF_UNIX};memcpy(a.sun_path+1,name,sizeof(name)-1);
 int fd=socket(AF_UNIX,SOCK_STREAM,0);if(fd<0)return 3;
 if(connect(fd,(struct sockaddr*)&a,offsetof(struct sockaddr_un,sun_path)+sizeof(name))){perror("connect");close(fd);return 4;}
 struct timeval timeout={2,0};setsockopt(fd,SOL_SOCKET,SO_RCVTIMEO,&timeout,sizeof(timeout));
 char buf[64];send(fd,"?",1,MSG_NOSIGNAL);ssize_t n=recv(fd,buf,sizeof(buf),0);int saved=errno;close(fd);
 if(n==0||(n<0&&saved==ECONNRESET)){printf("UNTRUSTED_UID_REJECTED uid=%d\n",getuid());return 0;}
 fprintf(stderr,"Unexpected response to untrusted UID: %ld\n",(long)n);return 5;
}
