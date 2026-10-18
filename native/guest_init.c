/* MIT License - Copyright (c) 2026 ryuya0124. Linux guest init, never an Android init. */
typedef unsigned long size_t;
static long sc(long n,long a,long b,long c,long d,long e,long f) {
 register long x0 __asm__("x0")=a,x1 __asm__("x1")=b,x2 __asm__("x2")=c,x3 __asm__("x3")=d,x4 __asm__("x4")=e,x5 __asm__("x5")=f,x8 __asm__("x8")=n;
 __asm__ volatile("svc 0" : "+r"(x0) : "r"(x1),"r"(x2),"r"(x3),"r"(x4),"r"(x5),"r"(x8) : "memory");return x0;
}
static size_t len(const char *s){size_t n=0;while(s[n])n++;return n;}
static void say(const char *s){sc(64,1,(long)s,len(s),0,0,0);}
static long open(const char *p,long f){return sc(56,-100,(long)p,f,0,0,0);}
static long mount(const char *s,const char *t,const char *type,long flags,const char *opts){return sc(40,(long)s,(long)t,(long)type,flags,(long)opts,0);}
static void mkdir(const char *p){sc(34,-100,(long)p,0755,0,0,0);}
static void node(const char *p,int major,int minor,int permissions){unsigned dev=(minor&255)|(major<<8)|((minor&~255)<<12);sc(33,-100,(long)p,0020000|permissions,dev,0,0);}
static void fail(const char *s){say("GUEST_INIT_ERROR: ");say(s);say("\n");sc(142,0xfee1dead,672274793,0x4321fedc,0,0,0);for(;;)sc(101,(long)(long[2]){60,0},0,0,0,0,0);}
void start(void){
 long guest_umask=sc(166,0,0,0,0,0,0);
 long fd=open("/dev/console",2);if(fd<0)for(;;){};
 for(int i=0;i<3;i++)if(fd!=i)sc(24,fd,i,0,0,0,0);
 if(mount("proc","/proc","proc",0,0)<0)fail("proc");
 if(mount("sysfs","/sys","sysfs",0,0)<0)fail("sysfs");
 char text[64]={0};fd=open("/sys/block/vda/dev",0);if(fd<0)fail("guest disk missing");
 long count=sc(63,fd,(long)text,sizeof(text)-1,0,0,0);sc(57,fd,0,0,0,0,0);if(count<=0)fail("disk identity");
 int major=0,minor=0;char *p=text;while(*p>='0'&&*p<='9')major=major*10+*p++-'0';if(*p++!=':')fail("disk identity format");while(*p>='0'&&*p<='9')minor=minor*10+*p++-'0';
 sc(33,-100,(long)"/dev/vda",0060600,(minor&255)|(major<<8)|((minor&~255)<<12),0,0);
 char cmdline[2048];cmdline[0]=0;long options=open("/proc/cmdline",0);if(options>=0){long count=sc(63,options,(long)cmdline,sizeof(cmdline)-1,0,0,0);sc(57,options,0,0,0,0,0);if(count>0)cmdline[count]=0;}
 int migration=0;for(size_t i=0;cmdline[i];i++){const char marker[]=" plb.migrate=1 ";size_t j=0;while(marker[j]&&cmdline[i+j]==marker[j])j++;if(!marker[j]){migration=1;break;}}
 if(migration){char *args[]={"/plb-agent","--copy-disk",0};char *env[]={"PATH=/usr/sbin:/usr/bin:/sbin:/bin",0};sc(221,(long)args[0],(long)args,(long)env,0,0,0);fail("migration agent exec");}
 if(mount("/dev/vda","/newroot","ext4",0,0)<0)fail("Debian root mount");
 if(mount("devtmpfs","/newroot/dev","devtmpfs",2,"mode=755,size=16m")<0&&mount("tmpfs","/newroot/dev","tmpfs",2,"mode=755,size=4m")<0)fail("dev");
 mkdir("/newroot/dev/pts");mount("devpts","/newroot/dev/pts","devpts",0,"mode=620,ptmxmode=666");
 sc(33,-100,(long)"/newroot/dev/vda",0060600,(minor&255)|(major<<8)|((minor&~255)<<12),0,0);
 node("/newroot/dev/console",5,1,0600);node("/newroot/dev/ttyS0",4,64,0600);node("/newroot/dev/tty",5,0,0666);
 node("/newroot/dev/null",1,3,0666);node("/newroot/dev/zero",1,5,0666);node("/newroot/dev/random",1,8,0666);node("/newroot/dev/urandom",1,9,0666);node("/newroot/dev/ptmx",5,2,0666);
 sc(166,guest_umask,0,0,0,0,0);
 mount("proc","/newroot/proc","proc",0,0);mount("sysfs","/newroot/sys","sysfs",0,0);mount("tmpfs","/newroot/tmp","tmpfs",2,"mode=1777,size=128m");mount("tmpfs","/newroot/run","tmpfs",2,"mode=755,size=64m");
 sc(161,(long)"ryuya-linux",11,0,0,0,0);
 mkdir("/newroot/run/plb");
 for(int si=0;si<4;si++){
 const char *sources[]={"/plb-install-gui","/plb-install-gpu","/plb-install-integration","/clipboard-watch.py"},*targets[]={"/newroot/run/plb/plb-install-gui","/newroot/run/plb/plb-install-gpu","/newroot/run/plb/plb-install-integration","/newroot/run/plb/clipboard-watch.py"};
 long script_in=open(sources[si],0);if(script_in<0)fail("guest script input");
 long script_out=sc(56,-100,(long)targets[si],577,0755,0,0);if(script_out<0)fail("guest script output");
 char script[2048];long script_n;while((script_n=sc(63,script_in,(long)script,sizeof(script),0,0,0))>0){long done=0;while(done<script_n){long part=sc(64,script_out,(long)script+done,script_n-done,0,0,0);if(part<=0)fail("guest script copy");done+=part;}}
 if(script_n<0)fail("guest script read");sc(57,script_in,0,0,0,0,0);sc(57,script_out,0,0,0,0,0);
 }
 long input=open("/plb-agent",0);if(input<0)fail("agent input");
 long output=sc(56,-100,(long)"/newroot/run/plb-agent",577,0755,0,0);if(output<0)fail("agent output");
 char data[4096];long n;while((n=sc(63,input,(long)data,sizeof(data),0,0,0))>0){long done=0;while(done<n){long w=sc(64,output,(long)data+done,n-done,0,0,0);if(w<=0)fail("agent copy");done+=w;}}
 sc(57,input,0,0,0,0,0);sc(57,output,0,0,0,0,0);
 for(int i=0;i<7;i++){const char *names[]={"/newroot/run/plb/ping","/newroot/run/plb/plb-ping","/newroot/run/plb/plb-udp-forward","/newroot/run/plb/plb-gpu-run","/newroot/run/plb/plb-gpu-info","/newroot/run/plb/plb-gpu-mode","/newroot/run/plb/plb-desktop-mode"};if(sc(36,(long)"../plb-agent",-100,(long)names[i],0,0,0)<0)fail("network command link");}
 if(sc(51,(long)"/newroot",0,0,0,0,0)<0||sc(49,(long)"/",0,0,0,0,0)<0)fail("agent chroot");
 char *args[]={"/run/plb-agent",0};char *env[]={"PATH=/usr/sbin:/usr/bin:/sbin:/bin",0};
 sc(221,(long)args[0],(long)args,(long)env,0,0,0);fail("agent exec");

}
__asm__(".global _start\n_start:\nmov x29,xzr\nmov x30,xzr\nbl start\n1: b 1b\n");
