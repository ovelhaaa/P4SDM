"""Render the actual firmware drawing functions with fixed host fixtures.

No hand-drawn mockup: extract production draw/header functions and the actual
3x5 glyph renderer. Mock storage/timers and framebuffer writes only. PPM dumps
are deliberately kept outside Git in .pio; --baseline renders accepted M19.
"""
import argparse
from pathlib import Path
import subprocess
import tempfile

p=argparse.ArgumentParser(); p.add_argument('--baseline',action='store_true'); a=p.parse_args()
root=Path.cwd(); output=root/'.pio'/('m20-before' if a.baseline else 'm20-after'); output.mkdir(exist_ok=True)
source=subprocess.check_output(['git','show','66fff46:src/app_main.cpp']).decode() if a.baseline else Path('src/app_main.cpp').read_text()
draw=source[source.index('void draw_waveform()'):source.index('void interact(')]
if a.baseline:
    draw=draw.replace('void draw(int id) {','void draw(int id) {\n display::reset_clip();',1)
font=subprocess.check_output(['git','show','66fff46:src/hal/display_hal.cpp']).decode() if a.baseline else Path('src/hal/display_hal.cpp').read_text()
start=font.index('void text(int x,'); end=font.index('\n}\n}',start)+2; font=font[start:end]
includes='\n'.join(f'#include "{(root/path).as_posix()}"' for path in ['src/app/model.h','src/app/project_ui.h','src/app/samples.h','src/app/transient_service.h'])
prefix=r'''
#include <algorithm>
#include <cassert>
#include <climits>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>
size_t render_allocations=0;
void *operator new(size_t n) { ++render_allocations; if(auto p=std::malloc(n)) return p; throw std::bad_alloc(); }
void operator delete(void *p) noexcept { std::free(p); }
void operator delete(void *p,size_t) noexcept { std::free(p); }
#define P4SDM_APP_DIAGNOSTICS 1
unsigned millis() { return 1000; }
unsigned esp_timer_get_time() { static unsigned n=0; return ++n; }
size_t strlcpy(char *out,const char *s,size_t size) { size_t n=std::strlen(s); if(size) { size_t count=std::min(size-1,n); std::memcpy(out,s,count); out[count]=0; } return n; }
namespace display {
app::Rect clip_rect{0,0,800,480};
std::vector<uint16_t> pixels(800*480);
void clip(int x,int y,int w,int h) { clip_rect={x,y,w,h}; }
void reset_clip() { clip_rect={0,0,800,480}; }
void rect(int x,int y,int w,int h,uint16_t color) {
  for(int row=std::max(y,clip_rect.y);row<std::min({480,y+h,clip_rect.y+clip_rect.h});++row)
    for(int col=std::max(x,clip_rect.x);col<std::min({800,x+w,clip_rect.x+clip_rect.w});++col) pixels[row*800+col]=color;
}
void fill(uint16_t c) { rect(0,0,800,480,c); }
}
namespace samples {
unsigned revision() { return 0; }
void preview(unsigned,Preview &out) {
  out.frames=44100; strlcpy(out.name,"BREAKBEAT_16BIT.WAV",sizeof(out.name));
  for(unsigned n=0;n<sampler::waveform_columns;++n) out.waveform.columns[n]={int16_t(-int(n%17)*1800),int16_t(int(n%13)*2200)};
}
void describe(int,char *out,unsigned size) { strlcpy(out,"16/16 BREAKBEAT_16BIT.WAV",size); }
void track_name(unsigned,char *out,unsigned size) { strlcpy(out,"ASSIGNED: BREAKBEAT_16BIT.WAV",size); }
void message(char *out,unsigned size) { strlcpy(out,"WAV LOADED / 44100 HZ / 16 BIT",size); }
}
namespace projects {
bool dirty() { return true; }
void current(char *out,unsigned size) { strlcpy(out,"PROJECT_9999",size); }
void status(char *out,unsigned size) { strlcpy(out,"READY / PROJECT V2 / SD OK",size); }
void describe(unsigned,char *out,unsigned size) { strlcpy(out,"PROJECT_9999",size); }
}
app::Engine view; app::Ui ui; project::Workflow project_ui;
app::ui20::Confirmation ui_confirmation;
app::ui20::Notification ui_notification;
transients::Status transient_view;
unsigned transient_mode=16,transient_sensitivity=1;
samples::Preview wave_preview;
unsigned wave_revision=UINT_MAX; int wave_track=-1;
uint32_t waveform_redraw_max=0,flash_until[16]{};
int old_head=3,sample_index=15;
char slice_notice[64]="SLICE 16/16 / ACTIVE 2";
constexpr uint16_t bg=app::ui20::theme::background,panel=app::ui20::theme::surface,accent=app::ui20::theme::primary,white=app::ui20::theme::text;
std::atomic<uint32_t> diagnostic_p99{1},diagnostic_max{2},diagnostic_misses{0};
uint32_t frames=1,dirty_bytes=1;
#define MALLOC_CAP_SPIRAM 1
unsigned heap_caps_get_free_size(int) { return 1000000; }
'''
suffix=r'''
void save(const std::string &name) {
  std::ofstream out(name,std::ios::binary); out<<"P6\n800 480\n255\n";
  for(auto c:display::pixels) { char rgb[]={char(((c>>11)&31)*255/31),char(((c>>5)&63)*255/63),char((c&31)*255/31)}; out.write(rgb,3); }
}
int main(int argc,char **argv) {
  assert(argc==2); view.selected_pattern=15; view.playing_pattern=2; view.queued_pattern=7; view.bpm=400; view.playing=true;
  ui.selected=15;ui.selected_step=15;
  for(auto &t:view.tracks) { t.assigned(); t.slices.divide({},16); t.slice_enabled=true; }
  view.patterns[15].track_steps[15]=0xaaaa;
  view.patterns[15].locks[15][15]={255,127,127,127,15,127,127,127,15};
  view.chain.length=16; for(unsigned n=0;n<16;++n) view.chain.entries[n]={uint8_t(n),16};
  view.performance.override_target=15;view.performance.override_active=2;view.performance.fill_pending=7;
  transient_view.state=transients::State::Ready;transient_view.proposal.owner.track=15;transient_view.proposal.bank.divide({},16);
  for(int page=0;page<PAGE_COUNT;++page) {
    const auto allocations_before=render_allocations;
    ui.page=app::Page(page);display::reset_clip();display::fill(bg);header();
    RENDER_PAGE
    overlay(); assert(render_allocations==allocations_before);
    save(std::string(argv[1])+"/page_"+std::to_string(page)+".ppm");
  }
  EXTRA_VARIANTS
  std::cout<<"Actual firmware UI host framebuffer dumps and zero render allocations PASS (800x480, fixed fixtures; not device photographs)\n";
}
'''
if a.baseline:
    render='''
    if(ui.page==app::Page::Sequence) { for(int n=0;n<24;++n) draw(n);draw(68);draw(69);draw(78); }
    else if(ui.page==app::Page::Track) { for(int n=24;n<29;++n) draw(n);draw(37);draw(42);draw(43);draw(113); }
    else if(ui.page==app::Page::SampleSlice) { for(int n=132;n<=147;++n) draw(n);draw(210); }
    else if(ui.page==app::Page::Pattern) { for(int n=46;n<=67;++n) draw(n); }
    else if(ui.page==app::Page::Locks) { for(int n=96;n<=105;++n) draw(n);draw(121); }
    else if(ui.page==app::Page::Performance) { for(int n=184;n<=206;++n) draw(n); }
    else if(ui.page==app::Page::Project) { for(int n=154;n<=161;++n) draw(n); }
    else continue;
    if(ui.page!=app::Page::Performance && ui.page!=app::Page::SampleSlice && ui.page!=app::Page::Project) for(int n=29;n<=33;++n) draw(n);
    '''
    extra=''
else:
    render='render_page();'
    extra='''
  ui.page=app::Page::Performance;ui.perf_mixer=true;display::fill(bg);header();render_page();save(std::string(argv[1])+"/mix.ppm");
  ui.page=app::Page::Tools;ui.tool_section=1;display::fill(bg);header();render_page();save(std::string(argv[1])+"/generate.ppm");
  ui.page=app::Page::Tools;ui.tool_section=2;display::fill(bg);header();render_page();save(std::string(argv[1])+"/pattern_tools.ppm");
  ui.page=app::Page::Project;project_ui.mode=project::Workflow::Mode::Confirm;strlcpy(project_ui.target,"PROJECT_9999",sizeof(project_ui.target));display::fill(bg);header();render_page();save(std::string(argv[1])+"/confirm.ppm");
  ui.page=app::Page::SamplePlayback;view.tracks[15].sample=false;display::fill(bg);header();render_page();save(std::string(argv[1])+"/disabled_sample.ppm");
    '''
suffix=suffix.replace('PAGE_COUNT','18' if a.baseline else '19').replace('RENDER_PAGE',render).replace('EXTRA_VARIANTS',extra)
with tempfile.TemporaryDirectory() as folder:
    path=Path(folder)/'render.cpp'; path.write_text(includes+prefix+'\nnamespace display {\n'+font+'\n}\n'+draw+suffix)
    binary=Path(folder)/'render.exe'
    subprocess.run(['g++','-std=c++17','-O2','-Wall','-Wextra','-Werror',str(path),'-o',str(binary)],check=True)
    subprocess.run([str(binary),str(output)],check=True)
