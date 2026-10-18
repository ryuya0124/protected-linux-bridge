/* MIT. Stable IDs shared by the host configuration and guest agent. */
#ifndef PLB_DISPLAY_MODES_H
#define PLB_DISPLAY_MODES_H
struct plb_display_mode {int width,height;};
static const struct plb_display_mode plb_display_modes[]={{960,540},{1024,768},{1280,720},{1280,800},{1600,900},{1920,1080},{2560,1440},{720,1280},{1080,1920}};
#define PLB_DISPLAY_COUNT (sizeof(plb_display_modes)/sizeof(*plb_display_modes))
static int plb_display_valid(int id,int fps){return id>=0&&(unsigned)id<PLB_DISPLAY_COUNT&&(fps==15||fps==24||fps==30||fps==60||fps==90||fps==120);}
#endif
