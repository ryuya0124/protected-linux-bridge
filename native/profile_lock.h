/* MIT. One owned manager or offline profile copy at a time, across all profiles. */
#ifndef PLB_PROFILE_LOCK_H
#define PLB_PROFILE_LOCK_H
#include <sys/file.h>
#ifndef PLB_OWNER_LOCK_FILE
#define PLB_OWNER_LOCK_FILE "/data/user/0/net.ryuya_dev.protectedlinux/files/vm-owner.lock"
#endif
static int profile_lock(void){
 int fd=open(PLB_OWNER_LOCK_FILE,O_RDWR|O_CREAT|O_CLOEXEC|O_NOFOLLOW,0600);if(fd<0)return -1;struct stat st;
 if(fstat(fd,&st)||!S_ISREG(st.st_mode)||st.st_uid!=geteuid()||st.st_nlink!=1||(st.st_mode&077)||flock(fd,LOCK_EX|LOCK_NB)){close(fd);return -1;}return fd;
}
#endif
