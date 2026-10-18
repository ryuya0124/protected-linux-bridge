/* MIT. Guest-only bootstrap: real systemd PID1 and a managed bridge service. */
#ifndef PLB_GUEST_SYSTEMD_H
#define PLB_GUEST_SYSTEMD_H
#include <sys/mount.h>
static int guest_file(const char *path,const char *text){int fd=open(path,O_WRONLY|O_CREAT|O_TRUNC|O_CLOEXEC|O_NOFOLLOW,0644);if(fd<0)return -1;int e=plb_write_all(fd,text,strlen(text));close(fd);return e;}
static int guest_boot_option(const char *key){char cmd[2048]={0};int fd=open("/proc/cmdline",O_RDONLY|O_CLOEXEC);if(fd<0)return 0;ssize_t n=read(fd,cmd,sizeof(cmd)-1);close(fd);if(n<1)return 0;char *p=strstr(cmd,key);return p?atoi(p+strlen(key)):0;}
static void guest_protect_kernel_packages(void){
 mkdir("/etc/apt/preferences.d",0755);guest_file("/etc/apt/preferences.d/protected-linux-no-kernel","Package: linux-image* linux-modules* linux-firmware* firmware-* flash-kernel grub-* initramfs-tools* systemd-boot*\nPin: version *\nPin-Priority: -1\n");
}
static void guest_start_systemd(void){
 if(!guest_boot_option(" plb.systemd="))return;
 mkdir("/dev/shm",01777);mount("tmpfs","/dev/shm","tmpfs",MS_NOSUID|MS_NODEV,"mode=1777,size=64m");symlink("/proc/self/fd","/dev/fd");symlink("/proc/self/fd/0","/dev/stdin");symlink("/proc/self/fd/1","/dev/stdout");symlink("/proc/self/fd/2","/dev/stderr");
 mkdir("/sys/fs/cgroup",0755);if(mount("cgroup2","/sys/fs/cgroup","cgroup2",MS_NOSUID|MS_NODEV|MS_NOEXEC,NULL)&&errno!=EBUSY){perror("SYSTEMD_CGROUP_UNAVAILABLE");return;}
 mkdir("/run/systemd",0755);mkdir("/run/systemd/system",0755);mkdir("/run/systemd/system/multi-user.target.wants",0755);mkdir("/run/systemd/system/service.d",0755);
 // Disable only cloud-image boot assumptions and unattended upgrades in this
 // guest runtime. Kernel package updates are forbidden by the device owner.
 const char *masks[]={"apt-daily.timer","apt-daily-upgrade.timer","apt-daily.service","apt-daily-upgrade.service","unattended-upgrades.service","systemd-remount-fs.service","systemd-sysctl.service","systemd-resolved.service","systemd-networkd.service","systemd-networkd.socket","systemd-networkd-wait-online.service","systemd-timesyncd.service","cloud-init-local.service","cloud-init-main.service","cloud-init-network.service","cloud-config.service","cloud-final.service","cloud-init-hotplugd.socket"};
 for(unsigned i=0;i<sizeof(masks)/sizeof(*masks);i++){char path[256];snprintf(path,sizeof(path),"/run/systemd/system/%s",masks[i]);symlink("/dev/null",path);}
 mkdir("/run/systemd/system/ssh.service.d",0755);guest_file("/run/systemd/system/ssh.service.d/10-plb.conf","[Unit]\nConditionPathExists=/etc/ssh/ssh_host_ed25519_key\n");
 const char service[]="[Unit]\nDescription=Protected Linux Bridge guest services\nAfter=basic.target\n[Service]\nType=simple\nExecStart=/run/plb-agent --serve\nRestart=on-failure\nRestartSec=1\nTimeoutStopSec=8\nKillMode=control-group\n[Install]\nWantedBy=multi-user.target\n";
 const char target[]="[Unit]\nDescription=Protected Linux VM\nRequires=basic.target\nAfter=basic.target\nWants=plb-bridge.service dbus.socket\nAllowIsolate=yes\n";
 // This preinstalled kernel has no namespaces/seccomp. Never pretend those
 // unit sandbox directives are enforced; remove only unsupported guest flags.
 const char compat[]="[Service]\nPrivateTmp=no\nPrivateDevices=no\nPrivateMounts=no\nPrivateNetwork=no\nPrivateUsers=no\nProtectSystem=no\nProtectHome=no\nProtectKernelTunables=no\nProtectKernelModules=no\nProtectKernelLogs=no\nProtectControlGroups=no\nProtectClock=no\nProtectHostname=no\nProtectProc=default\nProcSubset=all\nRestrictNamespaces=no\nRestrictAddressFamilies=\nSystemCallFilter=\nSystemCallArchitectures=\nMemoryDenyWriteExecute=no\nLockPersonality=no\n";
 if(guest_file("/run/systemd/system/plb-bridge.service",service)||guest_file("/run/systemd/system/plb.target",target)||(!guest_boot_option(" plb.fullkernel=")&&guest_file("/run/systemd/system/service.d/10-plb-kernel.conf",compat))||guest_file("/run/systemd/container","plb\n")){fputs("SYSTEMD_UNIT_SETUP_FAILED\n",stderr);return;}
 symlink("../plb-bridge.service","/run/systemd/system/multi-user.target.wants/plb-bridge.service");setenv("container","plb",1);
 execl("/lib/systemd/systemd","systemd","--unit=multi-user.target","--log-target=console","--show-status=no",(char*)NULL);
 execl("/usr/lib/systemd/systemd","systemd","--unit=multi-user.target","--log-target=console","--show-status=no",(char*)NULL);perror("SYSTEMD_EXEC_FAILED");
}
#endif
