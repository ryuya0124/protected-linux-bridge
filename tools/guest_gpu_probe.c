/* MIT. Diagnostic PID 1 for an isolated, read-only guest disk. Not an Android tool. */
#define _GNU_SOURCE
#include <sys/mount.h>
#include <sys/stat.h>
#include <sys/sysmacros.h>
#include <sys/wait.h>
#include <sys/reboot.h>
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>
#include <stdlib.h>
#include <stdio.h>
#include <dirent.h>
static void poweroff(void){puts("PLB_GPU_PROBE_COMPLETE");fflush(stdout);reboot(RB_POWER_OFF);for(;;)pause();}
static void diagnostic(const char *path,const char *a,const char *b,const char *c){
 pid_t child=fork();if(child==0){alarm(12);execl(path,path,a,b,c,(char*)NULL);perror(path);_exit(1);}if(child<0)return;int status;waitpid(child,&status,0);printf("PLB_GPU_COMMAND_STATUS=%d\n",status);fflush(stdout);
}
int main(void){
 if(getpid()!=1)return 99;
 int console=open("/dev/console",O_RDWR);if(console<0)return 98;for(int i=0;i<3;i++)dup2(console,i);if(console>2)close(console);setvbuf(stdout,NULL,_IONBF,0);
 puts("PLB_GPU_READONLY_PROBE");
 mount("proc","/proc","proc",0,NULL);mount("sysfs","/sys","sysfs",0,NULL);
 unsigned ma=0,mi=0;FILE *f=fopen("/sys/block/vda/dev","r");if(!f||fscanf(f,"%u:%u",&ma,&mi)!=2)poweroff();fclose(f);
 mknod("/dev/vda",S_IFBLK|0600,makedev(ma,mi));
 if(mount("/dev/vda","/newroot","ext4",MS_RDONLY,"noload")){perror("readonly guest mount");poweroff();}
 mount("devtmpfs","/newroot/dev","devtmpfs",MS_NOSUID,"mode=755");mount("proc","/newroot/proc","proc",0,NULL);mount("sysfs","/newroot/sys","sysfs",0,NULL);mount("tmpfs","/newroot/run","tmpfs",MS_NOSUID,"mode=755,size=16m");mount("tmpfs","/newroot/tmp","tmpfs",MS_NOSUID,"mode=1777,size=16m");
 if(chroot("/newroot")||chdir("/"))poweroff();mkdir("/run/user",0755);mkdir("/run/user/0",0700);
 clearenv();setenv("PATH","/usr/bin:/bin",1);setenv("HOME","/tmp",1);setenv("XDG_RUNTIME_DIR","/run/user/0",1);setenv("MESA_SHADER_CACHE_DISABLE","true",1);setenv("GALLIUM_DRIVER","virgl",1);setenv("MESA_LOADER_DRIVER_OVERRIDE","virtio_gpu",1);
 DIR *icds=opendir("/usr/share/vulkan/icd.d");struct dirent *entry;while(icds&&(entry=readdir(icds)))printf("ICD_FILE=%s\n",entry->d_name);if(icds)closedir(icds);
 const char *driver="/usr/share/vulkan/icd.d/virtio_icd.json";if(access(driver,R_OK))driver="/usr/share/vulkan/icd.d/virtio_icd.aarch64.json";setenv("VK_DRIVER_FILES",driver,1);
 diagnostic("/usr/bin/eglinfo","-B","-p","surfaceless");
 diagnostic("/usr/bin/vulkaninfo","--summary",NULL,NULL);
 poweroff();
}
