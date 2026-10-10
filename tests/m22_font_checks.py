"""Compare the real coalesced font against M21.4 at clipped pixel boundaries."""
from pathlib import Path
import subprocess
import tempfile

def extract(source):
    start=source.index('void text(int x,')
    return source[start:source.index('\n}\n}',start)+2]

old=extract(subprocess.check_output(['git','show','72a9178:src/hal/display_hal.cpp'],text=True))
new=extract(Path('src/hal/display_hal.cpp').read_text())
prefix=r'''
#include <algorithm>
#include <cassert>
#include <cstdint>
#include <iostream>
uint16_t pixels[80*80]{}; unsigned calls=0; int edge=80;
void rect(int x,int y,int w,int h,uint16_t color) {
  ++calls;
  for(int r=std::max(0,y);r<std::min(edge,y+h);++r)
    for(int c=std::max(0,x);c<std::min(edge,x+w);++c) pixels[r*80+c]=color;
}
'''
suffix=r'''
int main() {
  unsigned before=0,after=0;
  for(int scale:{1,2,3,4,8}) for(int clip:{10,37,80})
    for(int offset:{-4,0,7,33}) for(int c=32;c<127;++c) {
      edge=clip; char s[]={char(c),0}; uint16_t expected[80*80];
      std::fill(std::begin(pixels),std::end(pixels),0); calls=0;
      reference(offset,offset,s,123,scale); before+=calls;
      std::copy(std::begin(pixels),std::end(pixels),expected);
      std::fill(std::begin(pixels),std::end(pixels),0); calls=0;
      text(offset,offset,s,123,scale); after+=calls;
      assert(std::equal(std::begin(pixels),std::end(pixels),expected));
    }
  assert(after<before);
  std::cout<<"M22 exact production font bitmap/clipping PASS; rectangle calls "<<before<<" -> "<<after<<"\n";
}
'''
with tempfile.TemporaryDirectory() as folder:
    source=Path(folder)/'font.cpp'; binary=Path(folder)/'font.exe'
    source.write_text(prefix+old.replace('void text(','void reference(',1)+new+suffix)
    subprocess.run(['g++','-std=c++17','-O2','-Wall','-Wextra','-Werror',str(source),'-o',str(binary)],check=True)
    subprocess.run([str(binary)],check=True)
