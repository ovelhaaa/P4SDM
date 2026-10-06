#pragma once
#include "display_geometry.h"
#include <algorithm>
#include <array>

namespace display {
// Native 16x16 tiles: fixed storage, never a growing rectangle queue.
constexpr int TILE=16, TILE_COLS=NATIVE_WIDTH/TILE, TILE_ROWS=NATIVE_HEIGHT/TILE;
struct DirtyMask {
    std::array<uint32_t,TILE_ROWS> rows{};
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
        const uint32_t mask=((1u<<(right-left+1))-1)<<left;
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
template<unsigned Count> struct BufferHistory {
    static_assert(Count==2 || Count==3,"only bounded double/triple buffering");
    DirtyMask stale[Count];
    unsigned front=0,back=1;
    void reset(unsigned selected=0) {
        front=selected;back=(front+1)%Count;
        for(unsigned i=0;i<Count;++i) {stale[i].clear();if(i!=front) stale[i].all();}
    }
    void repaired() {stale[back].clear();}
    void committed(const DirtyMask &changed) {
        for(unsigned i=0;i<Count;++i) if(i!=back) stale[i].merge(changed);
        front=back; back=(back+1)%Count;
    }
};
using DirtyHistory=BufferHistory<2>;
using TripleHistory=BufferHistory<3>;
struct RefreshFence {
    bool pending=false;
    uint32_t submitted_at=0;
    void submit(uint32_t refresh) {pending=true;submitted_at=refresh;}
    bool complete(uint32_t refresh) const {return pending && uint32_t(refresh-submitted_at)>=2;}
};
}
