#include "../src/app/sample_interpolation.h"
#include <cstdlib>
#include <fstream>
#include <vector>
int main(int argc, char **argv) {
  if(argc!=7) return 1;
  std::ifstream input(argv[1],std::ios::binary|std::ios::ate);
  if(!input) return 2;
  std::vector<int16_t> pcm(size_t(input.tellg())/2);
  input.seekg(0); input.read(reinterpret_cast<char*>(pcm.data()),std::streamsize(pcm.size()*2));
  const uint64_t step=std::strtoull(argv[3],nullptr,10), start=std::strtoull(argv[4],nullptr,10);
  const unsigned count=unsigned(std::strtoul(argv[5],nullptr,10));
  const bool reverse=std::strtoul(argv[6],nullptr,10)!=0;
  std::ofstream output(argv[2],std::ios::binary);
  for(unsigned mode=0;mode<3;++mode)
    for(unsigned n=0;n<count;++n) {
      const int16_t y=sampler::lookup(pcm.data(),{0,uint32_t(pcm.size())},reverse,
                                     start+n*step,sampler::Interpolation(mode));
      output.write(reinterpret_cast<const char*>(&y),sizeof(y));
    }
  return output?0:3;
}
