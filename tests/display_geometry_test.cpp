#include <cassert>
#include "../src/hal/display_geometry.h"
int main() {
    using namespace display;
    static_assert(FRAME_BYTES==768000);
    const auto tl=to_native(0,0),tr=to_native(799,0),br=to_native(799,479),bl=to_native(0,479);
    assert(tl.x==479 && tl.y==0); assert(tr.x==479 && tr.y==799);
    assert(br.x==0 && br.y==799); assert(bl.x==0 && bl.y==0);
    for(int y=0;y<HEIGHT;++y) for(int x=0;x<WIDTH;++x) {
        const auto native=to_native(x,y);
        assert(native.x>=0 && native.x<NATIVE_WIDTH && native.y>=0 && native.y<NATIVE_HEIGHT);
        const auto logical=to_logical(native.x,native.y);
        assert(logical.x==x && logical.y==y);
    }
}
