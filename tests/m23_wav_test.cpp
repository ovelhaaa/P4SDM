#include "../src/app/wav.h"
#include <filesystem>
#include <fstream>
#include <vector>
#include <cassert>
#include <cstdio>
int main(int argc,char **argv) {
  assert(argc==2);unsigned count=0;
  for(const auto &p:std::filesystem::directory_iterator(argv[1])) {
    if(p.path().extension()!=".wav")continue;
    std::ifstream file(p.path(),std::ios::binary);
    std::vector<uint8_t> data((std::istreambuf_iterator<char>(file)),{});
    sampler::Wav wav;
    auto read=[&](uint64_t offset,uint8_t *out,unsigned n) {
      if(offset>data.size() || n>data.size()-offset)return false;
      memcpy(out,data.data()+offset,n);return true;
    };
    assert(!sampler::parse(data.size(),read,wav));
    assert(wav.rate==44100 && wav.channels==1 && wav.frames*2==wav.bytes && wav.bytes<=4194304);
    ++count;
  }
  assert(count==42);std::puts("M23 all 42 WAVs accepted by production RIFF parser PASS");
}
