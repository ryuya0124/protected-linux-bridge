/* MIT. Diskless guest PID 1; never execute as an Android program. */
typedef unsigned long size_t;
static long call(long n,long a,long b,long c,long d,long e,long f){register long x0 __asm__("x0")=a,x1 __asm__("x1")=b,x2 __asm__("x2")=c,x3 __asm__("x3")=d,x4 __asm__("x4")=e,x5 __asm__("x5")=f,x8 __asm__("x8")=n;__asm__ volatile("svc 0":"+r"(x0):"r"(x1),"r"(x2),"r"(x3),"r"(x4),"r"(x5),"r"(x8):"memory");return x0;}
static void out(const char *s){size_t n=0;while(s[n])n++;call(64,1,(long)s,n,0,0,0);}
static void exists(const char *path){long fd=call(56,-100,(long)path,0,0,0,0);out(path);out(fd>=0?" PRESENT\n":" ABSENT\n");if(fd>=0)call(57,fd,0,0,0,0,0);}
void begin(void){if(call(172,0,0,0,0,0,0)!=1){call(93,99,0,0,0,0,0);for(;;){}}
 long fd=call(56,-100,(long)"/dev/console",2,0,0,0);if(fd<0)for(;;){};for(int i=0;i<3;i++)call(24,fd,i,0,0,0,0);
 call(40,(long)"proc",(long)"/proc",(long)"proc",0,0,0);call(40,(long)"sysfs",(long)"/sys",(long)"sysfs",0,0,0);
 char u[6][65];out("PLB_DISKLESS_KERNEL_PROBE\n");if(!call(160,(long)u,0,0,0,0,0)){out("GUEST_RELEASE=");out(u[2]);out("\nGUEST_MACHINE=");out(u[4]);out("\n");}
 exists("/sys/module/virtio_net");exists("/sys/module/virtio_gpu");exists("/sys/bus/virtio/drivers/virtio_snd");exists("/sys/module/usbcore");exists("/sys/module/xhci_hcd");exists("/sys/class/misc/binder-control");exists("/proc/self/ns/net");
 out(call(167,21,0,0,0,0,0)>=0?"GUEST_SECCOMP_AVAILABLE\n":"GUEST_SECCOMP_UNAVAILABLE\n");
 out("PLB_DISKLESS_PROBE_COMPLETE\n");call(81,0,0,0,0,0,0);call(142,0xfee1dead,672274793,0x4321fedc,0,0,0);for(;;){}
}
__asm__(".global _start\n_start:\nmov x29,xzr\nmov x30,xzr\nbl begin\n1: b 1b\n");
