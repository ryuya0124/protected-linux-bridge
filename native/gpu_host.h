/* MIT. Opaque graphics transport. The GPU decoder runs as Android nobody, never root. */
#ifndef PLB_GPU_HOST_H
#define PLB_GPU_HOST_H
#include <grp.h>
#define PLB_GPU_DIR "/data/local/tmp/net.ryuya_dev.protectedlinux.gpu"
#define PLB_GPU_SOCKET PLB_GPU_DIR "/socket/vtest"
#define PLB_GPU_UID 9999
#define PLB_GPU_PORT 50444
#define PLB_GPU_CLIENTS 4
static int gpu_prepare(uid_t module_uid){
 if(mkdir(PLB_GPU_DIR,0755)&&errno!=EEXIST)return -1;
 int directory=open(PLB_GPU_DIR,O_RDONLY|O_DIRECTORY|O_CLOEXEC|O_NOFOLLOW);struct stat d;
 if(directory<0)return -1;if(fstat(directory,&d)||d.st_uid||!S_ISDIR(d.st_mode)||(d.st_mode&022)){close(directory);return -1;}
 if(fchmod(directory,0755)){close(directory);return -1;}
 const char *source[]={"gpu-server","gpu-libepoxy.so","gpu-libvirglrenderer.so"};const char *dest[]={"gpu-server","libepoxy.so","libvirglrenderer.so"};int result=0;
 for(int i=0;i<3&&!result;i++){
  char path[256];snprintf(path,sizeof(path),"/data/user/0/net.ryuya_dev.protectedlinux/files/runtime/%s",source[i]);int input=open(path,O_RDONLY|O_CLOEXEC|O_NOFOLLOW);struct stat before,after;
  if(input<0){result=-1;break;}if(fstat(input,&before)||!S_ISREG(before.st_mode)||before.st_uid!=module_uid||before.st_size<4096||before.st_size>16*1024*1024){close(input);result=-1;break;}
  char pending[64];snprintf(pending,sizeof(pending),"%s.pending",dest[i]);int output=openat(directory,pending,O_WRONLY|O_CREAT|O_TRUNC|O_CLOEXEC|O_NOFOLLOW,0600);if(output<0){close(input);result=-1;break;}
  unsigned char data[16384];ssize_t n;off_t copied=0;while((n=read(input,data,sizeof(data)))>0){if(plb_write_all(output,data,(size_t)n)){result=-1;break;}copied+=n;}
  if(n<0||copied!=before.st_size||fstat(input,&after)||before.st_ino!=after.st_ino||before.st_size!=after.st_size||before.st_mtim.tv_sec!=after.st_mtim.tv_sec||before.st_mtim.tv_nsec!=after.st_mtim.tv_nsec||fsync(output)||fchmod(output,i?0644:0755))result=-1;
  close(input);close(output);if(!result&&renameat(directory,pending,directory,dest[i]))result=-1;
 }
 if(!result){
  if(mkdirat(directory,"socket",0700)&&errno!=EEXIST)result=-1;
  int socket_dir=openat(directory,"socket",O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_CLOEXEC);struct stat st;
  if(socket_dir<0)result=-1;else{if(fstat(socket_dir,&st)||!S_ISDIR(st.st_mode)||(st.st_uid!=0&&st.st_uid!=PLB_GPU_UID)||fchmod(socket_dir,0700)||fchown(socket_dir,PLB_GPU_UID,PLB_GPU_UID))result=-1;else unlinkat(socket_dir,"vtest",0);close(socket_dir);}
 }
 close(directory);return result;
}
static int gpu_listener(void){
 int fd=socket(AF_VSOCK,SOCK_STREAM|SOCK_CLOEXEC,0);if(fd<0)return -1;struct sockaddr_vm a={.svm_family=AF_VSOCK,.svm_port=PLB_GPU_PORT,.svm_cid=VMADDR_CID_ANY};if(bind(fd,(struct sockaddr*)&a,sizeof(a))||listen(fd,PLB_GPU_CLIENTS)){close(fd);return -1;}return fd;
}
static volatile sig_atomic_t gpu_parent_gone;
static void gpu_parent_exit(int signal_number){(void)signal_number;gpu_parent_gone=1;}
static pid_t gpu_start(pid_t *expected_server,int lifetime){
 int report[2];if(pipe2(report,O_CLOEXEC))return -1;
 pid_t parent=getpid(),child=fork();
 if(child<0){close(report[0]);close(report[1]);return -1;}
 if(child>0){
  close(report[1]);struct pollfd ready={report[0],POLLIN,0};pid_t server=-1;
  int ok=poll(&ready,1,3000)>0&&read(report[0],&server,sizeof(server))==sizeof(server)&&server>0;close(report[0]);
  if(!ok){kill(child,SIGTERM);wait_or_kill(child,500);return -1;}*expected_server=server;return child;
 }
 close(report[0]);if(setsid()<0)_exit(90);log_child(BASE "/gpu-renderer.log");
 // A dedicated unprivileged watcher owns every renderer descendant. Even an unexpected
 // manager exit closes the complete GPU process group, including upstream forked clients.
 signal(SIGTERM,gpu_parent_exit);signal(SIGINT,gpu_parent_exit);signal(SIGHUP,gpu_parent_exit);
 struct rlimit files={256,256},processes={64,64},log_size={1024*1024,1024*1024};
 if(setrlimit(RLIMIT_NOFILE,&files)||setrlimit(RLIMIT_NPROC,&processes)||setrlimit(RLIMIT_FSIZE,&log_size))_exit(91);
 if(setgroups(0,NULL)||setgid(PLB_GPU_UID)||setuid(PLB_GPU_UID)||prctl(PR_SET_NO_NEW_PRIVS,1,0,0,0)||prctl(PR_SET_PDEATHSIG,SIGTERM)||getppid()!=parent||prctl(PR_SET_CHILD_SUBREAPER,1,0,0,0))_exit(92);
 for(int fd=3;fd<256;fd++)if(fd!=report[1])close(fd);
 if(chdir(PLB_GPU_DIR "/socket"))_exit(93);clearenv();setenv("LD_LIBRARY_PATH",PLB_GPU_DIR,1);setenv("PATH","/system/bin",1);setenv("ANDROID_ROOT","/system",1);setenv("ANDROID_DATA","/data",1);setenv("HOME",PLB_GPU_DIR "/socket",1);setenv("MESA_SHADER_CACHE_DISABLE","true",1);
 pid_t watcher=getpid(),server=fork();
 if(server==0){
  close(report[1]);signal(SIGTERM,SIG_DFL);signal(SIGINT,SIG_DFL);signal(SIGHUP,SIG_DFL);
  if(prctl(PR_SET_PDEATHSIG,SIGKILL)||getppid()!=watcher)_exit(94);
  execl(PLB_GPU_DIR "/gpu-server","plb-gpu-server","--socket-path",PLB_GPU_SOCKET,(char*)NULL);_exit(94);
 }
 if(server<0||plb_write_all(report[1],&server,sizeof(server)))gpu_parent_gone=1;close(report[1]);
 double deadline=plb_now()+lifetime;
 while(!gpu_parent_gone&&plb_now()<deadline){if(waitpid(server,NULL,WNOHANG)!=0)break;usleep(100000);}
 kill(-getpid(),SIGKILL);_exit(95);
}
static int gpu_connect(pid_t renderer){
 for(int i=0;i<30&&!stopped;i++){
  int fd=socket(AF_UNIX,SOCK_STREAM|SOCK_CLOEXEC,0);if(fd<0)return -1;struct sockaddr_un a={.sun_family=AF_UNIX};snprintf(a.sun_path,sizeof(a.sun_path),"%s",PLB_GPU_SOCKET);
  if(!connect(fd,(struct sockaddr*)&a,sizeof(a))){struct ucred peer;socklen_t size=sizeof(peer);if(!getsockopt(fd,SOL_SOCKET,SO_PEERCRED,&peer,&size)&&peer.uid==PLB_GPU_UID&&peer.pid==renderer)return fd;close(fd);return -1;}close(fd);usleep(100000);
 }return -1;
}
static void gpu_stop(int *listener,pid_t *renderer,pid_t workers[PLB_GPU_CLIENTS]){
 if(*listener>=0){close(*listener);*listener=-1;}
 for(int i=0;i<PLB_GPU_CLIENTS;i++)if(workers[i]>0){kill(workers[i],SIGTERM);wait_or_kill(workers[i],500);workers[i]=0;}
 // Keep the original child unreaped until its entire dedicated process group is killed.
 if(*renderer>0){kill(-*renderer,SIGKILL);wait_or_kill(*renderer,500);*renderer=-1;}
}
#endif
