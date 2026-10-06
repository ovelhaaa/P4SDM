#pragma once
#include <cstdlib>
#include <set>
#include <cassert>
constexpr int MALLOC_CAP_SPIRAM=1, MALLOC_CAP_8BIT=2;
inline int fx_fail_at=-1, fx_alloc_calls=0;
inline std::set<void *> fx_live;
inline void *heap_caps_calloc(size_t n, size_t size, int caps) {
    assert(caps & MALLOC_CAP_SPIRAM);
    if (fx_alloc_calls++ == fx_fail_at) return nullptr;
    void *p=std::calloc(n,size);
    if(p) fx_live.insert(p);
    return p;
}
inline void fx_test_free(void *p) { if(p) { assert(fx_live.erase(p)==1); std::free(p); } }
