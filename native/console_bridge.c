/* MIT License - Copyright (c) 2026 ryuya0124. */
#define _GNU_SOURCE
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/prctl.h>
#include <sys/resource.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/sysinfo.h>
#include <poll.h>
#include <signal.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stddef.h>
#include <time.h>

#define BASE "/data/user/0/com.android.virtualization.terminal/files/protected-linux"
#define SERIAL BASE "/serial.sock"
#define ABSTRACT "net.ryuya_dev.protectedlinux.console"
static volatile sig_atomic_t stopped;
static void stop(int sig) { (void)sig; stopped=1; }
static double now(void) { struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return t.tv_sec+t.tv_nsec/1e9; }
static int listener(const char *name,int abstract) {
 int fd=socket(AF_UNIX,SOCK_STREAM|SOCK_CLOEXEC,0); if(fd<0)return -1;
 struct sockaddr_un a={.sun_family=AF_UNIX};size_t n=strlen(name);
 if(n+1>=sizeof(a.sun_path)){close(fd);errno=ENAMETOOLONG;return -1;}
 if(abstract)memcpy(a.sun_path+1,name,n);else memcpy(a.sun_path,name,n+1);
 socklen_t len=offsetof(struct sockaddr_un,sun_path)+n+1;
 if(bind(fd,(struct sockaddr*)&a,len)||listen(fd,1)){int e=errno;close(fd);errno=e;return -1;}return fd;
}
static int send_all(int fd,const char *data,size_t size) {
 while(size&&!stopped){ssize_t n=send(fd,data,size,MSG_NOSIGNAL);if(n<0){if(errno==EINTR)continue;return -1;}if(!n)return -1;data+=n;size-=n;}return size?-1:0;
}
static int peer_uid(int fd);
static int pump(int client,int serial,pid_t child,double deadline,int control,int uid,int module_uid) {
 char buf[16384];
 while(!stopped&&now()<deadline){
  struct pollfd p[3]={{client,POLLIN,0},{serial,POLLIN,0},{control,POLLIN,0}};
  int r=poll(p,3,200);if(r<0){if(errno==EINTR)continue;return -1;}
  if(p[2].revents&POLLIN){
   int probe=accept4(control,NULL,NULL,SOCK_CLOEXEC);if(probe>=0){
    int peer=peer_uid(probe);if(peer==uid||peer==module_uid||peer==0){
     struct timeval t={1,0};setsockopt(probe,SOL_SOCKET,SO_RCVTIMEO,&t,sizeof(t));setsockopt(probe,SOL_SOCKET,SO_SNDTIMEO,&t,sizeof(t));
     char request;if(recv(probe,&request,1,0)==1)send_all(probe,"PLB1 BUSY\n",10);
    }close(probe);
   }
  }
  for(int i=0;i<2;i++)if(p[i].revents&POLLIN){ssize_t n=recv(p[i].fd,buf,sizeof(buf),0);if(n<=0)return 0;if(send_all(p[1-i].fd,buf,n))return -1;}
  if(p[0].revents&(POLLHUP|POLLERR|POLLNVAL)||p[1].revents&(POLLHUP|POLLERR|POLLNVAL))return 0;
  if(waitid(P_PID,child,&(siginfo_t){0},WEXITED|WNOHANG|WNOWAIT)<0&&errno==ECHILD)return 0;
 }
 return 0;
}

static int peer_uid(int fd) {
 struct ucred peer;socklen_t size=sizeof(peer);
 return getsockopt(fd,SOL_SOCKET,SO_PEERCRED,&peer,&size)?-1:(int)peer.uid;
}
static int connect_control(void) {
 int fd=socket(AF_UNIX,SOCK_STREAM|SOCK_CLOEXEC,0);if(fd<0)return -1;
 struct sockaddr_un a={.sun_family=AF_UNIX};memcpy(a.sun_path+1,ABSTRACT,sizeof(ABSTRACT)-1);
 if(connect(fd,(struct sockaddr*)&a,offsetof(struct sockaddr_un,sun_path)+sizeof(ABSTRACT))||peer_uid(fd)!=0){close(fd);return -1;}
 return fd;
}
static void reap_vm(pid_t child) {
 if(child<=0)return;
 int status;pid_t r=waitpid(child,&status,WNOHANG);
 if(r==0){for(int n=0;n<10&&r==0;n++){usleep(100000);r=waitpid(child,&status,WNOHANG);}}
 if(r==0){kill(child,SIGTERM);for(int n=0;n<30&&r==0;n++){usleep(100000);r=waitpid(child,&status,WNOHANG);}if(r==0){kill(child,SIGKILL);r=waitpid(child,&status,0);}}
 if(r>0){printf("CROSVM_EXIT=%d\n",WIFEXITED(status)?WEXITSTATUS(status):128+WTERMSIG(status));fflush(stdout);}
}
static int run_vm(int client,int control,int uid,int module_uid) {
 FILE *mem=fopen("/proc/meminfo","re");unsigned long long available=0;char line[128];
 if(mem){while(fgets(line,sizeof(line),mem))if(sscanf(line,"MemAvailable: %llu kB",&available)==1)break;fclose(mem);}
 if(available<1536ULL*1024){const char *error="\r\n[At least 1.5 GiB available memory is required]\r\n";send_all(client,error,strlen(error));return 29;}
 unlink(SERIAL);int serial_listener=listener(SERIAL,0);if(serial_listener<0)return 25;
 pid_t parent=getpid();pid_t child=fork();int serial=-1,result=0;
 if(child==0){
  prctl(PR_SET_PDEATHSIG,SIGKILL);if(getppid()!=parent)_exit(125);
  int log=open(BASE "/crosvm-bridge.log",O_WRONLY|O_CREAT|O_TRUNC,0600);if(log<0)_exit(126);
  dup2(log,STDOUT_FILENO);dup2(log,STDERR_FILENO);close(log);
  int null=open("/dev/null",O_RDONLY);if(null>=0){dup2(null,STDIN_FILENO);close(null);}
  execl("/apex/com.android.virt/bin/crosvm","crosvm","--log-level","info","run",
   "--name","protected-linux","--disable-sandbox","--no-balloon","--protected-vm-without-firmware","--swiotlb","32",
   "--mem","size=512","--cpus","num-cores=1",
   "--serial","type=unix-stream,path=" SERIAL ",input-unix-stream=true,console=true,earlycon=true",
   "--params","console=ttyS0 earlycon 8250.nr_uarts=1 rdinit=/init panic=0 quiet loglevel=3",
   "--block","path=/data/user/0/com.android.virtualization.terminal/files/linux/root_part,ro=true",
   "--initrd",BASE "/debian-initramfs.cpio","/apex/com.android.virt/etc/fs/microdroid_kernel",(char*)NULL);
  _exit(127);
 }
 if(child<0){result=26;goto cleanup;}
 double boot_deadline=now()+20;
 while(!stopped&&now()<boot_deadline){
  struct pollfd p[2]={{serial_listener,POLLIN,0},{client,0,0}};int r=poll(p,2,200);
  if(r>0&&(p[1].revents&(POLLHUP|POLLERR)))break;
  if(r>0&&(p[0].revents&POLLIN)){serial=accept4(serial_listener,NULL,NULL,SOCK_CLOEXEC);break;}
  siginfo_t info={0};if(waitid(P_PID,child,&info,WEXITED|WNOHANG|WNOWAIT)==0&&info.si_pid){result=27;goto cleanup;}
 }
 if(serial>=0){struct timeval t={2,0};setsockopt(client,SOL_SOCKET,SO_SNDTIMEO,&t,sizeof(t));setsockopt(serial,SOL_SOCKET,SO_SNDTIMEO,&t,sizeof(t));pump(client,serial,child,now()+1800,control,uid,module_uid);}else result=28;
cleanup:
 if(serial>=0)close(serial);close(serial_listener);unlink(SERIAL);reap_vm(child);
 if(result){const char *error="\r\n[VM start failed. Check Protected Linux Bridge and its private crosvm log.]\r\n";send_all(client,error,strlen(error));}
 return result;
}
int main(int argc,char **argv) {
 if(getuid()!=0){fprintf(stderr,"Root required\n");return 20;}
 if(argc==2&&!strcmp(argv[1],"--check")){
  int fd=connect_control();if(fd<0)return 1;
  struct timeval t={2,0};setsockopt(fd,SOL_SOCKET,SO_RCVTIMEO,&t,sizeof(t));char line[80]={0};
  int ok=send_all(fd,"?",1)==0&&recv(fd,line,sizeof(line)-1,0)>0&&(!strncmp(line,"PLB1 READY",10)||!strncmp(line,"PLB1 BUSY",9));close(fd);
  if(ok){puts("BRIDGE_WAITING existing");fflush(stdout);}return ok?0:1;
 }
 char *end1=NULL,*end2=NULL;long uid=argc==3?strtol(argv[1],&end1,10):-1,module_uid=argc==3?strtol(argv[2],&end2,10):-1;
 if(argc!=3||*end1||*end2||uid<10000||uid>19999||module_uid<10000||module_uid>19999||uid==module_uid){fprintf(stderr,"Expected primary-user Terminal and module UIDs\n");return 21;}
 struct rlimit limit={RLIM_INFINITY,RLIM_INFINITY};if(setrlimit(RLIMIT_MEMLOCK,&limit)){perror("memlock");return 22;}
 umask(077);signal(SIGTERM,stop);signal(SIGINT,stop);signal(SIGHUP,stop);signal(SIGPIPE,SIG_IGN);
 if(chdir(BASE)){perror("chdir");return 23;}
 int control=listener(ABSTRACT,1);if(control<0){perror("bind control");return 24;}
 printf("BRIDGE_WAITING uid=%ld\n",uid);fflush(stdout);double deadline=now()+1800;
 while(!stopped&&now()<deadline){
  struct pollfd p={control,POLLIN,0};if(poll(&p,1,200)<=0)continue;
  int client=accept4(control,NULL,NULL,SOCK_CLOEXEC);if(client<0)continue;
  int peer=peer_uid(client);if(peer!=uid&&peer!=module_uid&&peer!=0){close(client);continue;}
  struct timeval t={2,0};setsockopt(client,SOL_SOCKET,SO_RCVTIMEO,&t,sizeof(t));setsockopt(client,SOL_SOCKET,SO_SNDTIMEO,&t,sizeof(t));
  char request=0;ssize_t n=recv(client,&request,1,0);
  if(n==1&&request=='?')send_all(client,"PLB1 READY\n",11);
  else if(n==1&&request=='R'&&peer==uid){t.tv_sec=0;setsockopt(client,SOL_SOCKET,SO_RCVTIMEO,&t,sizeof(t));run_vm(client,control,(int)uid,(int)module_uid);deadline=now()+1800;}
  close(client);
 }
 close(control);return 0;
}
