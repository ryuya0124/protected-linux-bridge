/* MIT. Copy an unmounted block image into a zero-filled larger destination. */
#ifndef PLB_DISK_COPY_H
#define PLB_DISK_COPY_H
#define COPY_CHUNK 65536
static int disk_copy_verify(int source,int target,uint64_t bytes){
 unsigned char a[COPY_CHUNK],b[COPY_CHUNK];
 for(int pass=0;pass<2;pass++){
  if(lseek(source,0,SEEK_SET)<0||lseek(target,0,SEEK_SET)<0)return -1;
  for(uint64_t offset=0;offset<bytes;){size_t count=bytes-offset<COPY_CHUNK?(size_t)(bytes-offset):COPY_CHUNK;
   if(plb_read_exact(source,a,count))return -1;
   if(!pass){int nonzero=0;for(size_t i=0;i<count;i++)if(a[i]){nonzero=1;break;}
    if(nonzero){if(plb_write_all(target,a,count))return -1;}else if(lseek(target,(off_t)count,SEEK_CUR)<0)return -1;
   }else if(plb_read_exact(target,b,count)||memcmp(a,b,count))return -1;
   offset+=count;
  }
  if(!pass&&fsync(target))return -1;
 }
 return 0;
}
#endif
