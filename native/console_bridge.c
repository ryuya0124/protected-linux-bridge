/* MIT - Copyright (c) 2026 ryuya0124. Temporary-root VM manager, not a host shell. */
#define _GNU_SOURCE
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/prctl.h>
#include <sys/resource.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <linux/vm_sockets.h>
#include <signal.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stddef.h>
#include "stream_relay.h"
#include <stdint.h>
#ifdef PLB_PROFILE_MAINLINE
#define BASE "/data/user/0/net.ryuya_dev.protectedlinux/files/vm-mainline"
#define KERNEL_FILE "/data/user/0/net.ryuya_dev.protectedlinux/files/kernels/linux-6.18.57-plb1/Image"
#define PROFILE_TAG " mainline-6.18.57"
#else
#define BASE "/data/user/0/net.ryuya_dev.protectedlinux/files/vm"
#define KERNEL_FILE "/apex/com.android.virt/etc/fs/microdroid_kernel"
#define PROFILE_TAG " stock"
#endif
#define ROOT "/data/user/0/com.android.virtualization.terminal/files/linux/root_part"
#define BACKING BASE "/base-root.ext4"
#define OVERLAY BASE "/debian-v2.qcow2"
#define ABSTRACT "net.ryuya_dev.protectedlinux.console"
#define CID 39820
#define AGENT_PORT 50442
#define MAX_SESSIONS 12
#define READY "PLB3 " PLB_BUILD_ID PROFILE_TAG " READY\n"
pid_t plb_start_proxy(uid_t terminal_uid,int manager_listener,int startup_client,int lifetime);
static volatile sig_atomic_t stopped;
static void stop(int sig){(void)sig;stopped=1;}
static int peer_uid(int fd){struct ucred peer;socklen_t size=sizeof(peer);return getsockopt(fd,SOL_SOCKET,SO_PEERCRED,&peer,&size)?-1:(int)peer.uid;}
static int local_socket(int listen_socket){
 int fd=socket(AF_UNIX,SOCK_STREAM|SOCK_CLOEXEC,0);if(fd<0)return -1;
 struct sockaddr_un a={.sun_family=AF_UNIX};memcpy(a.sun_path+1,ABSTRACT,sizeof(ABSTRACT)-1);socklen_t length=offsetof(struct sockaddr_un,sun_path)+sizeof(ABSTRACT);
 if(listen_socket?(bind(fd,(struct sockaddr*)&a,length)||listen(fd,16)):(connect(fd,(struct sockaddr*)&a,length)||peer_uid(fd)!=0)){int saved=errno;close(fd);errno=saved;return -1;}return fd;
}
static void send_status(int fd,unsigned char code){plb_write_all(fd,&code,1);}
static void wait_or_kill(pid_t child,int grace_ms){
 if(child<=0)return;int status;pid_t result=waitpid(child,&status,WNOHANG);
 for(int elapsed=0;result==0&&elapsed<grace_ms;elapsed+=100){usleep(100000);result=waitpid(child,&status,WNOHANG);}
 if(result==0){kill(child,SIGTERM);for(int n=0;n<20&&result==0;n++){usleep(100000);result=waitpid(child,&status,WNOHANG);}if(!result){kill(child,SIGKILL);waitpid(child,&status,0);}}
}
static void log_child(const char *name){
 int fd=open(name,O_WRONLY|O_CREAT|O_TRUNC|O_CLOEXEC|O_NOFOLLOW,0600);if(fd<0)_exit(80);dup2(fd,STDOUT_FILENO);dup2(fd,STDERR_FILENO);close(fd);
 fd=open("/dev/null",O_RDONLY|O_CLOEXEC);if(fd>=0){dup2(fd,STDIN_FILENO);close(fd);}
}

#include "gpu_host.h"
#include "graphics_settings.h"
#include "resource_settings.h"
#include "clipboard_host.h"
#include "profile_lock.h"
static struct resource_settings vm_resources;
static int vm_seconds=1800;
static int same_file(const struct stat *a,const struct stat *b){return a->st_dev==b->st_dev&&a->st_ino==b->st_ino&&a->st_size==b->st_size&&a->st_mtim.tv_sec==b->st_mtim.tv_sec&&a->st_mtim.tv_nsec==b->st_mtim.tv_nsec;}
static int cache_base(uid_t terminal_uid){
 struct stat cached;if(lstat(BACKING,&cached)==0)return S_ISREG(cached.st_mode)&&cached.st_uid==0&&cached.st_size>=128*1024*1024?0:6;
 int original=open(ROOT,O_RDONLY|O_CLOEXEC|O_NOFOLLOW);if(original<0)return 7;struct stat before,after;
 if(fstat(original,&before)||!S_ISREG(before.st_mode)||before.st_uid!=terminal_uid||before.st_size<128*1024*1024||before.st_size>8LL*1024*1024*1024){close(original);return 7;}
 struct statvfs space;if(statvfs(BASE,&space)||(unsigned long long)space.f_bavail*space.f_frsize<(unsigned long long)before.st_size+256ULL*1024*1024){close(original);return 10;}
 // All writes are in the module's own private data, never the stock image or partitions.
 int output=open(BACKING ".pending",O_WRONLY|O_CREAT|O_TRUNC|O_CLOEXEC|O_NOFOLLOW,0600);if(output<0){close(original);return 3;}
 unsigned char buffer[65536],zeros[65536]={0};ssize_t n;int error=0;off_t copied=0;
 while(!stopped&&(n=read(original,buffer,sizeof(buffer)))>0){if(copied+n>before.st_size){error=3;break;}
  if(!memcmp(buffer,zeros,(size_t)n)){if(lseek(output,n,SEEK_CUR)<0){error=3;break;}}
  else if(plb_write_all(output,buffer,(size_t)n)){error=3;break;}copied+=n;
 }
 if(stopped||copied!=before.st_size||fstat(original,&after)||!same_file(&before,&after)||ftruncate(output,before.st_size)||fsync(output)||fchmod(output,0400))error=3;
 close(output);close(original);if(error){unlink(BACKING ".pending");return error;}
 return rename(BACKING ".pending",BACKING)?3:0;
}
static int prepare_overlay(uid_t terminal_uid){
 int error=cache_base(terminal_uid);if(error)return error;
 struct stat backing;if(lstat(BACKING,&backing))return 6;
 char identity[256];int size=snprintf(identity,sizeof(identity),"PLB_BASE_2 %llu %llu %lld %lld %ld\n",(unsigned long long)backing.st_dev,(unsigned long long)backing.st_ino,(long long)backing.st_size,(long long)backing.st_mtim.tv_sec,backing.st_mtim.tv_nsec);
 struct stat disk;int have_disk=lstat(OVERLAY,&disk)==0;
 int meta=open(BASE "/base-v2.identity",O_RDONLY|O_CLOEXEC|O_NOFOLLOW);
 if(have_disk||meta>=0){
  if(!have_disk||meta<0||!S_ISREG(disk.st_mode)||disk.st_uid!=0){if(meta>=0)close(meta);return 6;}
  char stored[256];ssize_t n=read(meta,stored,sizeof(stored));close(meta);return n==size&&!memcmp(stored,identity,(size_t)size)?0:6;
 }
 pid_t parent=getpid(),child=fork();if(child==0){prctl(PR_SET_PDEATHSIG,SIGKILL);if(getppid()!=parent)_exit(81);log_child(BASE "/disk-creation.log");execl("/apex/com.android.virt/bin/crosvm","crosvm","create_qcow2",OVERLAY ".pending","--backing-file",BACKING,(char*)NULL);_exit(82);}
 if(child<0)return 3;int status=0;double until=plb_now()+10;pid_t done;
 do{done=waitpid(child,&status,WNOHANG);if(!done)usleep(100000);}while(!done&&!stopped&&plb_now()<until);
 if(!done){wait_or_kill(child,0);return 3;}if(done<0||!WIFEXITED(status)||WEXITSTATUS(status))return 3;
 meta=open(BASE "/base-v2.identity.pending",O_WRONLY|O_CREAT|O_TRUNC|O_CLOEXEC|O_NOFOLLOW,0600);if(meta<0)return 3;
 int result=plb_write_all(meta,identity,(size_t)size)||fsync(meta);close(meta);if(result)return 3;
 if(rename(OVERLAY ".pending",OVERLAY)||rename(BASE "/base-v2.identity.pending",BASE "/base-v2.identity"))return 3;
 return 0;
}

static int guest_connect(int retries){
 for(int n=0;n<retries&&!stopped;n++){
  int fd=socket(AF_VSOCK,SOCK_STREAM|SOCK_CLOEXEC,0);if(fd<0)return -1;struct sockaddr_vm a={.svm_family=AF_VSOCK,.svm_port=AGENT_PORT,.svm_cid=CID};
  int flags=fcntl(fd,F_GETFL);fcntl(fd,F_SETFL,flags|O_NONBLOCK);int result=connect(fd,(struct sockaddr*)&a,sizeof(a));
  if(result&&errno==EINPROGRESS){struct pollfd p={fd,POLLOUT,0};int error=1;socklen_t size=sizeof(error);if(poll(&p,1,150)>0&&!getsockopt(fd,SOL_SOCKET,SO_ERROR,&error,&size)&&!error)result=0;}
  if(!result){fcntl(fd,F_SETFL,flags);struct timeval t={12,0};setsockopt(fd,SOL_SOCKET,SO_RCVTIMEO,&t,sizeof(t));return fd;}close(fd);usleep(100000);
 }
 return -1;
}
static int memory_ready(void){FILE *file=fopen("/proc/meminfo","re");if(!file)return 0;char line[128];unsigned long long available=0;while(fgets(line,sizeof(line),file))if(sscanf(line,"MemAvailable: %llu kB",&available)==1)break;fclose(file);return available>=(unsigned long long)(vm_resources.memory+768)*1024&&vm_resources.cpus<=sysconf(_SC_NPROCESSORS_ONLN);}
static pid_t start_vm(void){
 struct graphics_settings settings;if(graphics_read(&settings))return -1;int graphics=settings.mode;
 pid_t parent=getpid(),child=fork();if(child!=0)return child;
 prctl(PR_SET_PDEATHSIG,SIGKILL);if(getppid()!=parent)_exit(83);log_child(BASE "/crosvm-bridge.log");
 char params[384],memory[40],cpus[24];snprintf(memory,sizeof(memory),"size=%d",vm_resources.memory);snprintf(cpus,sizeof(cpus),"num-cores=%d",vm_resources.cpus);snprintf(params,sizeof(params),"console=ttyS0 earlycon 8250.nr_uarts=1 rdinit=/init panic=0 quiet loglevel=3%s%d plb.display=%d:%d plb.lifetime=%d plb.systemd=%d plb.diskgib=%d plb.fullkernel=%d",graphics<0?" plb.preserve=":" plb.graphics=",graphics<0?1:graphics,settings.resolution,settings.fps,vm_seconds,vm_resources.systemd,vm_resources.disk_gib,strcmp(PROFILE_TAG," stock")!=0);
 execl("/apex/com.android.virt/bin/crosvm","crosvm","--log-level","info","run","--name","protected-linux","--disable-sandbox","--no-balloon","--protected-vm-without-firmware","--swiotlb","32","--mem",memory,"--cpus",cpus,"--socket",BASE "/control.sock",
 "--serial","type=file,path=" BASE "/boot-console.log,console=true,earlycon=true",
 "--params",params,
 "--vsock","cid=39820","--block","path=" OVERLAY ",ro=false","--initrd",BASE "/debian-initramfs.cpio",KERNEL_FILE,(char*)NULL);_exit(84);
}
static void shutdown_vm(pid_t vm,pid_t proxy){
 if(vm>0){int guest=guest_connect(2);if(guest>=0){unsigned char ack;struct timeval timeout={10,0};setsockopt(guest,SOL_SOCKET,SO_RCVTIMEO,&timeout,sizeof(timeout));if(!plb_write_all(guest,"Q",1))read(guest,&ack,1);close(guest);}wait_or_kill(vm,12000);}
 if(proxy>0){kill(proxy,SIGTERM);wait_or_kill(proxy,500);}
}
#include "disk_migration.h"
static int client_command(const char *command){
 int fd=local_socket(0);if(fd<0)return *command=='X'?0:1;struct timeval t={3,0};setsockopt(fd,SOL_SOCKET,SO_RCVTIMEO,&t,sizeof(t));char line[64]={0};int ok=plb_write_all(fd,command,1)==0;
 if(*command=='?')ok=ok&&read(fd,line,sizeof(line)-1)==(ssize_t)(sizeof(READY)-1)&&!memcmp(line,READY,sizeof(READY)-1);
 close(fd);if(ok&&*command=='?'){puts("BRIDGE_WAITING existing");fflush(stdout);}
 if(ok&&*command=='X'){for(int i=0;i<400;i++){fd=local_socket(0);if(fd<0)return 0;close(fd);usleep(100000);}return 1;}return ok?0:1;
}
#ifdef PLB_PROFILE_MAINLINE
#include "profile_clone.h"
#endif
int main(int argc,char **argv){
 if(getuid()!=0)return 20;
#ifdef PLB_PROFILE_MAINLINE
 if(argc==2&&!strcmp(argv[1],"--kernel-ready"))return kernel_ready();
 if(argc==3&&!strcmp(argv[1],"--install-kernel")){char *end;long uid=strtol(argv[2],&end,10);return *argv[2]&&!(*end)&&uid>=10000&&uid<=19999?kernel_install((uid_t)uid):1;}
 if(argc==2&&!strcmp(argv[1],"--init-profile"))return profile_clone();
 #endif
 if(argc==3&&!strcmp(argv[1],"--clipboard")){int mode=graphics_number(argv[2]);return mode>=0&&mode<=3?clipboard_client(mode):1;}
 if(argc==2&&!strcmp(argv[1],"--resources-state")){struct resource_settings r;int error=resources_read(&r);printf("RESOURCES %d %d %d %d %d %d\n",r.memory,r.cpus,r.minutes,r.disk_gib,r.keep_alive,r.systemd);return !!error;}
 if(argc==8&&!strcmp(argv[1],"--resources")){struct resource_settings r={graphics_number(argv[2]),graphics_number(argv[3]),graphics_number(argv[4]),graphics_number(argv[5]),graphics_number(argv[6]),graphics_number(argv[7])};return disk_target_allowed(r.disk_gib)?resources_save(&r):1;}
 if(argc==2&&!strcmp(argv[1],"--graphics-state")){struct graphics_settings value;int error=graphics_read(&value);printf("GRAPHICS %d %d %d\n",error?-2:value.mode,value.resolution,value.fps);return error?1:0;}
 if(argc==5&&!strcmp(argv[1],"--settings"))return graphics_save(graphics_number(argv[2]),graphics_number(argv[3]),graphics_number(argv[4]));
 if(argc==3&&!strcmp(argv[1],"--graphics")){struct graphics_settings value;if(graphics_read(&value))return 1;return graphics_save(graphics_number(argv[2]),value.resolution,value.fps);}
 if(argc==2&&!strcmp(argv[1],"--check"))return client_command("?");if(argc==2&&!strcmp(argv[1],"--stop"))return client_command("X");
#ifdef PLB_PROFILE_MAINLINE
 if(kernel_ready())return 27;
#endif
 char *end1=NULL,*end2=NULL;long terminal_uid=argc==3?strtol(argv[1],&end1,10):-1,module_uid=argc==3?strtol(argv[2],&end2,10):-1;
 if(argc!=3||*end1||*end2||terminal_uid<10000||terminal_uid>19999||module_uid<10000||module_uid>19999||terminal_uid==module_uid)return 21;
 struct rlimit limit={RLIM_INFINITY,RLIM_INFINITY};if(setrlimit(RLIMIT_MEMLOCK,&limit))return 22;
 umask(077);signal(SIGTERM,stop);signal(SIGINT,stop);signal(SIGHUP,stop);signal(SIGPIPE,SIG_IGN);
 struct stat device;if(stat("/dev/gunyah",&device)||!S_ISCHR(device.st_mode)||access(KERNEL_FILE,R_OK)||access("/apex/com.android.virt/bin/crosvm",X_OK))return 25;
 int owner_lock=profile_lock();if(owner_lock<0)return 28;
 if(chdir(BASE))return 23;int control=local_socket(1);if(control<0){perror("bind control");return 24;}
 if(disk_recover()){close(control);return 26;}
 int gpu_ready=gpu_prepare((uid_t)module_uid)==0;
 printf("BRIDGE_WAITING uid=%ld\n",terminal_uid);fflush(stdout);
 int gpu_socket=-1;pid_t gpu_renderer=-1,gpu_server=-1,gpu_workers[PLB_GPU_CLIENTS]={0};
 pid_t vm=-1,proxy=-1,sessions[MAX_SESSIONS]={0};int interactive[MAX_SESSIONS]={0};double idle=plb_now()+1800,vm_deadline=0;
 while(!stopped&&plb_now()<idle){
  int active=0;for(int i=0;i<MAX_SESSIONS;i++)if(sessions[i]>0){if(waitpid(sessions[i],NULL,WNOHANG)!=0)sessions[i]=0;else if(interactive[i])active++;}
  for(int i=0;i<PLB_GPU_CLIENTS;i++)if(gpu_workers[i]>0&&waitpid(gpu_workers[i],NULL,WNOHANG)!=0)gpu_workers[i]=0;
  int vm_ended=vm>0&&waitpid(vm,NULL,WNOHANG)!=0;
  if(vm>0&&((!active&&!vm_resources.keep_alive)||vm_ended||plb_now()>vm_deadline)){
   for(int i=0;i<MAX_SESSIONS;i++)if(sessions[i]>0){kill(sessions[i],SIGTERM);wait_or_kill(sessions[i],500);sessions[i]=0;}
   gpu_stop(&gpu_socket,&gpu_renderer,gpu_workers);shutdown_vm(vm_ended?-1:vm,proxy);vm=proxy=-1;idle=plb_now()+1800;
  }
  struct pollfd p[2]={{control,POLLIN,0},{gpu_socket,POLLIN,0}};if(poll(p,2,100)<=0)continue;
  if(p[1].revents&POLLIN){
   struct sockaddr_vm peer;socklen_t length=sizeof(peer);int fd=accept4(gpu_socket,(struct sockaddr*)&peer,&length,SOCK_CLOEXEC);
   if(fd>=0){
    int slot=-1;for(int i=0;i<PLB_GPU_CLIENTS;i++)if(!gpu_workers[i]){slot=i;break;}
    if(peer.svm_cid!=CID||slot<0){send_status(fd,4);close(fd);}else{
     if(gpu_renderer<0)gpu_renderer=gpu_start(&gpu_server,vm_seconds);
     pid_t parent=getpid(),worker=gpu_renderer>0?fork():-1;
     if(worker==0){signal(SIGTERM,SIG_DFL);prctl(PR_SET_PDEATHSIG,SIGKILL);if(getppid()!=parent)_exit(95);close(control);close(gpu_socket);int renderer=gpu_connect(gpu_server);send_status(fd,renderer<0?3:0);if(renderer>=0){plb_relay(fd,renderer,vm_seconds,0);close(renderer);}close(fd);_exit(0);}
     if(worker>0)gpu_workers[slot]=worker;else send_status(fd,3);close(fd);
    }
   }
  }
  if(!(p[0].revents&POLLIN))continue;
  int client=accept4(control,NULL,NULL,SOCK_CLOEXEC);if(client<0)continue;int uid=peer_uid(client);struct ucred peer;socklen_t peer_size=sizeof(peer);int clip_peer=!getsockopt(client,SOL_SOCKET,SO_PEERCRED,&peer,&peer_size)&&uid==2000&&peer.pid==clipboard_pid;
  if(uid!=terminal_uid&&uid!=module_uid&&uid!=0&&!clip_peer){close(client);continue;}
  struct timeval timeout={2,0};setsockopt(client,SOL_SOCKET,SO_RCVTIMEO,&timeout,sizeof(timeout));setsockopt(client,SOL_SOCKET,SO_SNDTIMEO,&timeout,sizeof(timeout));char request=0;ssize_t received=read(client,&request,1);
  if(received==1&&request=='?')plb_write_all(client,READY,sizeof(READY)-1);
  else if(received==1&&request=='L'&&(uid==module_uid||uid==0)){unsigned char mode=255;int error=read(client,&mode,1)!=1||mode>3||clipboard_start((uid_t)module_uid,mode);send_status(client,error?3:0);}
  else if(received==1&&request=='X'&&(uid==module_uid||uid==0))stopped=1;
  else if(received==1&&((request=='R'&&uid==terminal_uid)||((request=='V'||request=='C'||request=='W')&&(uid==terminal_uid||uid==module_uid))||((request=='C'||request=='W')&&clip_peer)||((request=='A'||request=='M')&&uid==module_uid))){
   int slot=-1;for(int i=0;i<MAX_SESSIONS;i++)if(!sessions[i]){slot=i;break;}
   if(slot<0){send_status(client,4);close(client);continue;}
   if(vm<0){if(request!='R'&&request!='V'){send_status(client,3);close(client);continue;}
    int error=resources_read(&vm_resources)?3:memory_ready()?prepare_overlay((uid_t)terminal_uid):8;vm_seconds=vm_resources.minutes?vm_resources.minutes*60:2147483647;if(error){send_status(client,(unsigned char)error);close(client);continue;}
    int migration=disk_prepare_growth(control,(uid_t)module_uid);if(migration<0||stopped){send_status(client,11);close(client);continue;}
    proxy=plb_start_proxy((uid_t)terminal_uid,control,client,vm_seconds);if(proxy<0){if(migration>0)disk_recover();send_status(client,9);close(client);continue;}
    unlink(BASE "/control.sock");vm=start_vm();if(vm<0){shutdown_vm(-1,proxy);if(migration>0)disk_recover();proxy=-1;send_status(client,3);close(client);continue;}if(migration>0&&disk_confirm_growth()){shutdown_vm(vm,proxy);vm=proxy=-1;disk_recover();vm_resources.disk_gib=0;resources_save(&vm_resources);send_status(client,11);close(client);continue;}gpu_socket=gpu_ready?gpu_listener():-1;vm_deadline=plb_now()+vm_seconds;idle=vm_deadline+5;
   }
   pid_t parent=getpid(),worker=fork();if(worker==0){
    signal(SIGTERM,SIG_DFL);signal(SIGINT,SIG_DFL);prctl(PR_SET_PDEATHSIG,SIGKILL);if(getppid()!=parent)_exit(85);close(control);if(gpu_socket>=0)close(gpu_socket);
    int guest=guest_connect(120);if(guest<0){send_status(client,5);close(client);_exit(86);}char mode=request=='V'?'G':request=='C'?'C':request=='W'?'W':request=='A'?'O':request=='M'?'I':'P';unsigned char status=3;
    if(plb_write_all(guest,&mode,1)||read(guest,&status,1)!=1)status=3;send_status(client,status);
    if(!status){timeout.tv_sec=0;setsockopt(client,SOL_SOCKET,SO_RCVTIMEO,&timeout,sizeof(timeout));setsockopt(guest,SOL_SOCKET,SO_RCVTIMEO,&timeout,sizeof(timeout));plb_relay(client,guest,vm_seconds,1);}
    close(guest);close(client);_exit(0);
   }
   if(worker>0){sessions[slot]=worker;interactive[slot]=request!='C'&&request!='W';}else send_status(client,3);
  }
  close(client);
 }
 for(int i=0;i<MAX_SESSIONS;i++)if(sessions[i]>0){kill(sessions[i],SIGTERM);wait_or_kill(sessions[i],500);}clipboard_start((uid_t)module_uid,0);gpu_stop(&gpu_socket,&gpu_renderer,gpu_workers);shutdown_vm(vm,proxy);close(control);return 0;
}
