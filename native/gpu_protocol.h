/* MIT. Guest VTest handshake. Reject unknown FD-based protocol versions. */
#ifndef PLB_GPU_PROTOCOL_H
#define PLB_GPU_PROTOCOL_H
static uint32_t gpu_u32(const unsigned char *p){return (uint32_t)p[0]|((uint32_t)p[1]<<8)|((uint32_t)p[2]<<16)|((uint32_t)p[3]<<24);}
static int gpu_header(const unsigned char *h,uint32_t length,uint32_t command){return gpu_u32(h)==length&&gpu_u32(h+4)==command;}
static int gpu_negotiate(int client,int host){
 unsigned char head[8],name[256],data[20];
 if(plb_read_exact(client,head,8)||gpu_u32(head+4)!=8||!gpu_u32(head)||gpu_u32(head)>sizeof(name))return -1;
 size_t length=gpu_u32(head);if(plb_read_exact(client,name,length)||plb_write_all(host,head,8)||plb_write_all(host,name,length))return -1;
 if(plb_read_exact(client,head,8)||!gpu_header(head,0,10)||plb_write_all(host,head,8))return -1;
 if(plb_read_exact(client,data,16)||!gpu_header(data,2,7)||gpu_u32(data+8)||gpu_u32(data+12)||plb_write_all(host,data,16))return -1;
 if(plb_read_exact(host,data,20)||!gpu_header(data,0,10)||!gpu_header(data+8,1,7)||plb_write_all(client,data,20))return -1;
 if(plb_read_exact(client,data,12)||!gpu_header(data,1,11)||gpu_u32(data+8)<1||gpu_u32(data+8)>3)return -1;
 memset(data+8,0,4);if(plb_write_all(host,data,12))return -1;
 if(plb_read_exact(host,data,12)||!gpu_header(data,1,11)||gpu_u32(data+8)||plb_write_all(client,data,12))return -1;return 0;
}
#endif
