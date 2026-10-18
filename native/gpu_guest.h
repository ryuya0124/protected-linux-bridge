/* MIT. Guest-only VTest negotiation. Root host transports bytes without parsing GPU commands. */
#ifndef PLB_GPU_GUEST_H
#define PLB_GPU_GUEST_H
#include <sys/un.h>
#define PLB_GUEST_GPU_SOCKET "/run/plb-vtest.sock"
#include "gpu_protocol.h"
static void gpu_forwarder(void){
 int listener=socket(AF_UNIX,SOCK_STREAM|SOCK_CLOEXEC,0);if(listener<0)_exit(50);struct sockaddr_un a={.sun_family=AF_UNIX};strcpy(a.sun_path,PLB_GUEST_GPU_SOCKET);
 if(bind(listener,(struct sockaddr*)&a,sizeof(a))||chmod(PLB_GUEST_GPU_SOCKET,0666)||listen(listener,4))_exit(51);
 int active=0;signal(SIGCHLD,SIG_DFL);
 for(;;){while(waitpid(-1,NULL,WNOHANG)>0)if(active>0)active--;struct pollfd p={listener,POLLIN,0};if(poll(&p,1,200)<=0)continue;
  int client=accept4(listener,NULL,NULL,SOCK_CLOEXEC);if(client<0)continue;if(active>=4){close(client);continue;}
  pid_t child=fork();if(child==0){
   close(listener);int host=vsock_connect(50444);if(host<0){fputs("GPU relay unavailable\n",stderr);close(client);_exit(52);}
   struct timeval timeout={10,0};setsockopt(host,SOL_SOCKET,SO_RCVTIMEO,&timeout,sizeof(timeout));setsockopt(host,SOL_SOCKET,SO_SNDTIMEO,&timeout,sizeof(timeout));setsockopt(client,SOL_SOCKET,SO_RCVTIMEO,&timeout,sizeof(timeout));setsockopt(client,SOL_SOCKET,SO_SNDTIMEO,&timeout,sizeof(timeout));unsigned char status=3;
   if(plb_read_exact(host,&status,1)||status||gpu_negotiate(client,host)){fputs("GPU negotiation failed or renderer is busy\n",stderr);close(host);close(client);_exit(53);}
   timeout.tv_sec=0;setsockopt(host,SOL_SOCKET,SO_RCVTIMEO,&timeout,sizeof(timeout));setsockopt(host,SOL_SOCKET,SO_SNDTIMEO,&timeout,sizeof(timeout));setsockopt(client,SOL_SOCKET,SO_RCVTIMEO,&timeout,sizeof(timeout));setsockopt(client,SOL_SOCKET,SO_SNDTIMEO,&timeout,sizeof(timeout));plb_relay(client,host,1800,0);close(host);close(client);_exit(0);
  }if(child>0)active++;close(client);
 }
}
static int gpu_command(int argc,char **argv){
 const char *name=strrchr(argv[0],'/');name=name?name+1:argv[0];
 if(!strcmp(name,"plb-gpu-mode")||!strcmp(name,"plb-desktop-mode")){
  int desktop=!strcmp(name,"plb-desktop-mode");const char *yes=desktop?"gpu":"on",*no=desktop?"classic":"off";
  if(argc!=2||(strcmp(argv[1],yes)&&strcmp(argv[1],no))){fprintf(stderr,"Usage: %s %s|%s\nRecreate the VM to apply.\n",name,yes,no);return 2;}
  mkdir("/root/.config",0755);mkdir("/root/.config/protected-linux",0700);
  const char *path=desktop?"/root/.config/protected-linux/desktop-gpu":"/root/.config/protected-linux/gpu-enabled";
  if(!strcmp(argv[1],yes)){int file=open(path,O_WRONLY|O_CREAT|O_CLOEXEC|O_NOFOLLOW,0600);if(file<0){perror("GPU mode");return 2;}int error=fsync(file);close(file);if(error){perror("Save mode");return 2;}}
  else if(unlink(path)&&errno!=ENOENT){perror("GPU mode");return 2;}
  sync();printf("%s: %s. Stop and reopen the VM to apply; no Android reboot.\n",name,argv[1]);return 0;
 }
 if(strcmp(name,"plb-gpu-run")&&strcmp(name,"plb-gpu-info"))return -1;
 if(!strcmp(name,"plb-gpu-run")&&argc<2){fputs("Usage: plb-gpu-run PROGRAM [ARGS...]\nOptional guest Mesa packages: plb-install-gpu\n",stderr);return 2;}
 struct stat socket_info;if(lstat(PLB_GUEST_GPU_SOCKET,&socket_info)||!S_ISSOCK(socket_info.st_mode)){fputs("Guest GPU bridge unavailable.\n",stderr);return 2;}
 setenv("GALLIUM_DRIVER","virpipe",1);setenv("VTEST_SOCKET_NAME",PLB_GUEST_GPU_SOCKET,1);setenv("LIBGL_ALWAYS_SOFTWARE","1",1);
 if(!strcmp(name,"plb-gpu-info")){execlp("eglinfo","eglinfo","-B","-p","surfaceless",(char*)NULL);fputs("Run plb-install-gpu to install Mesa diagnostics.\n",stderr);return 2;}
 execvp(argv[1],argv+1);perror("GPU program");return 2;
}
#endif
