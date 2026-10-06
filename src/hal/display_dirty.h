#pragma once
#include "display_geometry.h"
#include <algorithm>
#include <array>

namespace display {
// Native 32x32 tiles: fixed storage, never a growing rectangle queue.
constexpr int TILE=32, TILE_COLS=NATIVE_WIDTH/TILE, TILE_ROWS=NATIVE_HEIGHT/TILE;
struct DirtyMask {
    std::array<uint16_t,TILE_ROWS> rows{};
    bool empty() const {for(auto row:rows) if(row) return false; return true;}
    void clear() {rows.fill(0);}
    void all() {rows.fill((1u<<TILE_COLS)-1);}
    void merge(const DirtyMask &other) {for(int r=0;r<TILE_ROWS;++r) rows[r]|=other.rows[r];}
    void native_rect(int x,int y,int w,int h) {
        if(w<=0 || h<=0) return;
        const int x0=std::max(x,0),y0=std::max(y,0);
        const int x1=int(std::min<int64_t>(int64_t(x)+w,NATIVE_WIDTH));
        const int y1=int(std::min<int64_t>(int64_t(y)+h,NATIVE_HEIGHT));
        if(x0>=x1 || y0>=y1) return;
        const unsigned left=x0/TILE,right=(x1-1)/TILE;
        const uint16_t mask=((1u<<(right-left+1))-1)<<left;
        for(int r=y0/TILE;r<=(y1-1)/TILE;++r) rows[r]|=mask;
    }
    void logical_rect(int x,int y,int w,int h) {
        if(w<=0 || h<=0) return;
        const int x0=std::max(x,0),y0=std::max(y,0);
        const int x1=int(std::min<int64_t>(int64_t(x)+w,WIDTH));
        const int y1=int(std::min<int64_t>(int64_t(y)+h,HEIGHT));
        if(x0<x1 && y0<y1) native_rect(NATIVE_WIDTH-y1,x0,y1-y0,x1-x0);
    }
    uint32_t bytes() const {
        unsigned tiles=0;
        for(auto row:rows) for(int c=0;c<TILE_COLS;++c) tiles+=(row>>c)&1;
        return tiles*TILE*TILE*2;
    }
};
struct DirtyHistory {
    DirtyMask stale[2];
    unsigned front=0,back=1;
    void reset() {front=0;back=1;stale[0].clear();stale[1].all();}
    void repaired() {stale[back].clear();}
    void committed(const DirtyMask &changed) {
        stale[front].merge(changed);
        front=back; back^=1;
    }
};
}
