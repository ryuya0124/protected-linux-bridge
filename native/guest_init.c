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
static void node(const char *p,int major,int minor){unsigned dev=(minor&255)|(major<<8)|((minor&~255)<<12);sc(33,-100,(long)p,0020600,dev,0,0);}
static void fail(const char *s){say("GUEST_INIT_ERROR: ");say(s);say("\n");sc(142,0xfee1dead,672274793,0x4321fedc,0,0,0);for(;;)sc(101,(long)(long[2]){60,0},0,0,0,0,0);}
void start(void){
 long fd=open("/dev/console",2);if(fd<0)for(;;){};
 for(int i=0;i<3;i++)if(fd!=i)sc(24,fd,i,0,0,0,0);
 if(mount("proc","/proc","proc",0,0)<0)fail("proc");
 if(mount("sysfs","/sys","sysfs",0,0)<0)fail("sysfs");
 char text[64]={0};fd=open("/sys/block/vda/dev",0);if(fd<0)fail("guest disk missing");
 long count=sc(63,fd,(long)text,sizeof(text)-1,0,0,0);sc(57,fd,0,0,0,0,0);if(count<=0)fail("disk identity");
 int major=0,minor=0;char *p=text;while(*p>='0'&&*p<='9')major=major*10+*p++-'0';if(*p++!=':')fail("disk identity format");while(*p>='0'&&*p<='9')minor=minor*10+*p++-'0';
 sc(33,-100,(long)"/dev/vda",0060600,(minor&255)|(major<<8)|((minor&~255)<<12),0,0);
 if(mount("/dev/vda","/newroot","ext4",1,0)<0)fail("Debian root mount");
 if(mount("tmpfs","/newroot/dev","tmpfs",2,"mode=755,size=4m")<0)fail("dev");
 mkdir("/newroot/dev/pts");mount("devpts","/newroot/dev/pts","devpts",0,"mode=620,ptmxmode=666");
 node("/newroot/dev/console",5,1);node("/newroot/dev/ttyS0",4,64);node("/newroot/dev/tty",5,0);
 node("/newroot/dev/null",1,3);node("/newroot/dev/zero",1,5);node("/newroot/dev/random",1,8);node("/newroot/dev/urandom",1,9);node("/newroot/dev/ptmx",5,2);
 mount("proc","/newroot/proc","proc",0,0);mount("sysfs","/newroot/sys","sysfs",0,0);mount("tmpfs","/newroot/tmp","tmpfs",2,"mode=1777,size=128m");mount("tmpfs","/newroot/run","tmpfs",2,"mode=755,size=16m");
 sc(161,(long)"ryuya-linux",11,0,0,0,0);
 say("DEBIAN_GUEST_READY\n");
 long child=sc(220,17,0,0,0,0,0);if(child<0)fail("fork");
 if(child==0){
  sc(157,0,0,0,0,0,0);fd=open("/newroot/dev/ttyS0",2);if(fd<0)fail("serial");
  for(int i=0;i<3;i++)if(fd!=i)sc(24,fd,i,0,0,0,0);
  if(sc(51,(long)"/newroot",0,0,0,0,0)<0||sc(49,(long)"/",0,0,0,0,0)<0)fail("guest chroot");
  char *args[]={"/bin/bash","--noprofile","--norc","-i",0};
  char *env[]={"PATH=/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin","TERM=xterm-256color","HOME=/tmp","LANG=C.UTF-8","PS1=\\u@\\h:\\w\\$ ",0};
  sc(221,(long)args[0],(long)args,(long)env,0,0,0);fail("Debian bash exec");
 }
 long waited;do{waited=sc(260,child,0,0,0,0,0);}while(waited==-4);
 say("DEBIAN_GUEST_CLOSED\n");sc(81,0,0,0,0,0,0);sc(142,0xfee1dead,672274793,0x4321fedc,0,0,0);for(;;){}
}
__asm__(".global _start\n_start:\nmov x29,xzr\nmov x30,xzr\nbl start\n1: b 1b\n");
