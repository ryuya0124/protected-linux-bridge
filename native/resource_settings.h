/* MIT. Root-private, fixed-schema VM resources; never host global settings. */
#ifndef PLB_RESOURCE_SETTINGS_H
#define PLB_RESOURCE_SETTINGS_H
#define PLB_RESOURCE_FILE BASE "/resources"
struct resource_settings {int memory,cpus,minutes,disk_gib,keep_alive,systemd;};
static int resources_valid(const struct resource_settings *r){
 return (r->memory==512||r->memory==1024||r->memory==2048||r->memory==4096||r->memory==8192)&&(r->cpus==1||r->cpus==2||r->cpus==4||r->cpus==8)&&(r->minutes==0||r->minutes==15||r->minutes==30||r->minutes==60||r->minutes==120||r->minutes==360)&&(r->disk_gib==0||r->disk_gib==4||r->disk_gib==8||r->disk_gib==16||r->disk_gib==32)&&(r->keep_alive==0||r->keep_alive==1)&&(r->systemd==0||r->systemd==1);
}
static int resources_parse(const char *data,size_t size,struct resource_settings *r){
 if(!size||size>=127)return -1;int values[6];size_t at=0;
 for(int i=0;i<6;i++){int value=0;size_t start=at;while(at<size&&data[at]>='0'&&data[at]<='9'){if(value>819)return -1;value=value*10+(data[at++]-'0');}if(at==start||at>=size||data[at++]!=(i==5?'\n':' '))return -1;values[i]=value;}
 if(at!=size)return -1;*r=(struct resource_settings){values[0],values[1],values[2],values[3],values[4],values[5]};return resources_valid(r)?0:-1;
}
static int resources_read(struct resource_settings *r){
 *r=(struct resource_settings){512,1,30,0,0,0};int fd=open(PLB_RESOURCE_FILE,O_RDONLY|O_CLOEXEC|O_NOFOLLOW);if(fd<0)return errno==ENOENT?0:-1;
 struct stat st;char data[128]={0};ssize_t n=read(fd,data,sizeof(data)-1);int ok=!fstat(fd,&st)&&S_ISREG(st.st_mode)&&st.st_uid==0&&!(st.st_mode&077)&&n>0&&n<127&&!memchr(data,0,(size_t)n);close(fd);if(!ok)return -1;
 return resources_parse(data,(size_t)n,r);
}
static int resources_save(const struct resource_settings *r){
 if(!resources_valid(r))return 1;int fd=open(PLB_RESOURCE_FILE ".pending",O_WRONLY|O_CREAT|O_TRUNC|O_CLOEXEC|O_NOFOLLOW,0600);if(fd<0)return 1;
 struct stat st;char data[128];int size=snprintf(data,sizeof(data),"%d %d %d %d %d %d\n",r->memory,r->cpus,r->minutes,r->disk_gib,r->keep_alive,r->systemd);
 int error=fstat(fd,&st)||!S_ISREG(st.st_mode)||st.st_uid!=0||fchmod(fd,0600)||plb_write_all(fd,data,(size_t)size)||fsync(fd);close(fd);if(!error)error=rename(PLB_RESOURCE_FILE ".pending",PLB_RESOURCE_FILE);if(error)unlink(PLB_RESOURCE_FILE ".pending");return !!error;
}
#endif
