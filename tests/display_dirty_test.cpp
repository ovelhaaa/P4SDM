#include "../src/hal/display_dirty.h"
#include <cassert>
#include <climits>
#include <vector>
#include <iostream>
using namespace display;
int main() {
    DirtyMask mask; assert(mask.empty()); mask.logical_rect(-10,-10,20,20);
    assert(mask.bytes()==2048); assert(mask.rows[0]==(1u<<14));
    mask.clear(); mask.logical_rect(799,479,10,10); assert(mask.rows[24]==1);
    mask.clear(); mask.logical_rect(INT_MAX,0,INT_MAX,50); assert(mask.empty());
    mask.logical_rect(-50,-50,20,20); assert(mask.empty());
    mask.logical_rect(0,0,800,480); assert(mask.bytes()==FRAME_BYTES);
    DirtyHistory history; history.reset(); assert(history.stale[1].bytes()==FRAME_BYTES);
    std::vector<uint16_t> buffers[2]={std::vector<uint16_t>(384000),std::vector<uint16_t>(384000)};
    std::vector<uint16_t> expected(384000);
    auto repair=[&]() {
        const auto pending=history.stale[history.back];
        for(int r=0;r<TILE_ROWS;++r) for(int c=0;c<TILE_COLS;++c) if(pending.rows[r]&(1u<<c))
            for(int y=r*TILE;y<(r+1)*TILE;++y) for(int x=c*TILE;x<(c+1)*TILE;++x)
                buffers[history.back][y*NATIVE_WIDTH+x]=buffers[history.front][y*NATIVE_WIDTH+x];
        history.repaired();
    };
    auto draw=[&](DirtyMask &changed,int x,int y,int w,int h,uint16_t value) {
        changed.logical_rect(x,y,w,h);
        for(int yy=std::max(y,0);yy<std::min(y+h,HEIGHT);++yy)
            for(int xx=std::max(x,0);xx<std::min(x+w,WIDTH);++xx) {
                auto p=to_native(xx,yy); const int index=p.y*NATIVE_WIDTH+p.x;
                buffers[history.back][index]=expected[index]=value;
            }
    };
    for(unsigned frame=0;frame<100;++frame) {
        repair(); assert(buffers[history.back]==expected); DirtyMask changed;
        if(frame==0 || frame==51) draw(changed,0,0,800,480,frame+1);
        else {
            draw(changed,int(frame*7%780)-10,int(frame*13%460)-10,60,48,frame+1);
            draw(changed,200,240,35,20,frame+2);
            draw(changed,215,230,40,40,frame+3); // overlap
            draw(changed,798,478,20,20,frame+4); // clipped edge
        }
        history.committed(changed); assert(buffers[history.front]==expected);
    }
    repair(); assert(buffers[history.back]==expected);
    std::cout<<"Dirty history: first frame, alternating buffers, disjoint/overlapping regions, clipping and full page transitions PASS\n";
}
