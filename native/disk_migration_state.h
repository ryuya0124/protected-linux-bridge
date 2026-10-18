/* MIT. Crash recovery of the owned disk promotion transaction. */
#ifndef PLB_DISK_STATE_H
#define PLB_DISK_STATE_H
#define NEW_DISK BASE "/debian-growth.pending.qcow2"
#define OLD_DISK BASE "/debian-before-growth.qcow2"
#define DISK_TRANSACTION BASE "/disk-growth"
static int disk_path_exists(const char *path){struct stat st;return !lstat(path,&st);}
static int disk_regular(const char *path){struct stat s;return !lstat(path,&s)&&S_ISREG(s.st_mode)&&s.st_uid==geteuid()&&s.st_nlink==1;}
static int disk_dir_sync(void){int fd=open(BASE,O_RDONLY|O_DIRECTORY|O_CLOEXEC|O_NOFOLLOW);if(fd<0)return -1;int e=fsync(fd);close(fd);return e;}
static int disk_remove_pending(void){if(!disk_path_exists(NEW_DISK)){return errno==ENOENT?0:-1;}return disk_regular(NEW_DISK)?unlink(NEW_DISK):-1;}
static int disk_recover(void){
 int old=disk_path_exists(OLD_DISK),transaction=disk_path_exists(DISK_TRANSACTION);
 if(!old&&!disk_path_exists(OVERLAY)&&(transaction||disk_path_exists(NEW_DISK)))return -1;
 if(old){if(!disk_regular(OLD_DISK)||!transaction||!disk_regular(DISK_TRANSACTION))return -1;
  if(disk_path_exists(OVERLAY)){if(!disk_regular(OVERLAY)||disk_remove_pending()||rename(OVERLAY,NEW_DISK))return -1;}
  if(rename(OLD_DISK,OVERLAY)||disk_dir_sync())return -1;
 }
 if(transaction){if(!disk_regular(DISK_TRANSACTION)||unlink(DISK_TRANSACTION))return -1;}
 return disk_remove_pending()||disk_dir_sync()?-1:0;
}
#endif
