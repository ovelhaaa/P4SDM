#pragma once
#include "hal/display_hal.h"
#include "hal/touch_hal.h"
#include <cstdio>
namespace ui_pattern {
constexpr uint16_t BG=0x0841,WHITE=0xffff;
inline void base() {
    display::fill(BG);
    static const uint16_t bars[]={0xf800,0x07e0,0x001f,0xffe0,0xf81f,0x07ff,0xffff};
    for(int i=0;i<7;++i) display::rect(50+i*100,85,100,65,bars[i]);
    display::line(0,0,799,0,WHITE); display::line(0,479,799,479,WHITE);
    display::line(0,0,0,479,WHITE); display::line(799,0,799,479,WHITE);
    display::line(50,240,750,240,0x4208); display::line(400,50,400,430,0x4208);
    display::text(220,28,"800X480 LANDSCAPE",WHITE);
    display::text(16,16,"TL1",0xf800); display::text(744,16,"TR2",0x07e0);
    display::text(744,445,"BR3",0x001f); display::text(16,445,"BL4",0xffe0);
    display::circle(400,240,16,WHITE);
}
inline void update(unsigned frame,const touch::State &point,bool heavy=false) {
    if(heavy) {
        display::fill(frame&1?0x1082:BG);
        for(int y=0;y<480;y+=40) display::rect(0,y,800,20,uint16_t((frame*173+y*31)&0xffff));
        display::text(220,28,"800X480 HEAVY",WHITE);
    }
    // Changing numeric fields, 8 meters and a moving playhead at every update.
    display::rect(100,180,600,50,BG);
    char text[64]; snprintf(text,sizeof(text),"FRAME %u X %u Y %u %s",frame,point.x,point.y,point.pressed?"DOWN":"UP");
    display::text(110,192,text,WHITE,2);
    display::rect(60,280,680,130,BG);
    for(unsigned i=0;i<8;++i) display::rect(90+i*80,390-(frame*7+i*17)%100,32,(frame*7+i*17)%100,0x07e0);
    display::rect(60+frame*7%680,280,3,125,WHITE);
    // A visible short trail avoids re-rendering the full background on each poll.
    if(point.pressed) { display::circle(point.x,point.y,10,WHITE); }
}
}
