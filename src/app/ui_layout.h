#pragma once
#include "model.h"
#include <cstring>

namespace app::ui20 {
// Numeric values below 222 are the preserved command-handler adapter only.
// Geometry, visibility, rendering and touch share this page-local registry.
enum class Section { Seq, Track, Sample, Pattern, Perform, Project };
enum class Navigation : int {
  Seq = 300, Track, Sample, Pattern, Perform, Project,
  PreviousTrack, NextTrack, Tab0, Tab1, Tab2, Tab3,
  BasicLocks, ToneLocks, SampleLocks, SliceOperations
};
constexpr int number(Navigation n) { return int(n); }
enum class SequenceWidget : int { Step0=0, Step15=15, Track0=16, Track7=23, Bank=33, EditStep=68, Swing=69 };
enum class TrackWidget : int { Volume=24, Pan, Pitch, Length, Wave, Mute=37, Source=41, Sample=42, Solo=43, Cutoff=107, Resonance, Send };
enum class SampleWidget : int { Previous=38, Next, Load, Source, Start=124, End, Reverse, Mode, Choke, Reset, Waveform=132, PreviousSlice, NextSlice, Activate, Enable, SliceStart, SliceEnd, Divide, Add, Delete, ResetSlices, Audition, Notice=147, Operations=315 };
enum class PatternWidget : int { FirstPad=46, LastPad=61, LengthMinus=62, LengthValue, LengthPlus, Copy, Clear, Inspect, Step=70, Velocity, Accent, Probability, Ratchet1, Ratchet2, Ratchet3, Ratchet4 };
enum class PerformanceWidget : int { FirstPad=184, LastPad=199, Mode=201, CancelOverride, CancelFill, Status=205, RepeatX2=207, RepeatX4, RepeatX8 };
struct Confirmation {
  int widget=-1; Command intent{}; unsigned revision=0;
  void cancel() { widget=-1; }
  bool accept(int id,Command c,unsigned generation) {
    if(widget==id && revision==generation && intent.kind==c.kind &&
       intent.track==c.track && intent.pattern==c.pattern && intent.step==c.step && intent.value==c.value) {
      cancel(); return true;
    }
    widget=id; intent=c; revision=generation; return false;
  }
};
constexpr Rect status{0, 0, 800, 56}, content{0, 56, 800, 360},
    bottom{0, 416, 800, 64}, diagnostics{0, 56, 800, 4};
constexpr Rect context_status{0,0,220,56}, tempo_status{604,0,136,56};
namespace theme {
constexpr uint16_t background=0x1082, surface=0x2104, primary=0x07d3,
    secondary=0x528a, warning=0xffe0, danger=0xf800, disabled=0x4208,
    text=0xffff, muted=0x8410, current=0x03c0, queued=0x820f,
    boundary=0x6318, proposal=0xf81f;
}
struct Notification {
  char text[96]{}; uint32_t deadline=0; bool active=false;
  void show(const char *message,uint32_t now) {
    std::strncpy(text,message,sizeof(text)-1); text[sizeof(text)-1]=0;
    deadline=now+4500; active=text[0]!=0;
  }
  bool expire(uint32_t now) {
    if(active && int32_t(now-deadline)>=0) { active=false; return true; }
    return false;
  }
};
constexpr int title_scale=4, value_scale=4, control_scale=3, status_scale=2;
inline Section section(Page p) {
  switch(p) {
  case Page::Sequence: return Section::Seq;
  case Page::Track: case Page::Tone: case Page::Fx: return Section::Track;
  case Page::Sample: case Page::SamplePlayback: case Page::SampleSlice:
  case Page::AutoSlice: case Page::SliceTools: return Section::Sample;
  case Page::Pattern: case Page::Step: case Page::Tools: case Page::Locks:
  case Page::ToneLocks: case Page::SampleLocks: return Section::Pattern;
  case Page::Project: return Section::Project;
  default: return Section::Perform;
  }
}
inline bool locks(Page p) { return p==Page::Locks || p==Page::ToneLocks || p==Page::SampleLocks; }
struct Context {
  int track=-1,pattern=-1; bool sample=false;
  unsigned sample_revision=0,project_revision=0;
};
inline bool full_invalidation(Context old,Context next,Page page) {
  return old.track!=next.track || old.sample!=next.sample ||
      old.sample_revision!=next.sample_revision || old.project_revision!=next.project_revision ||
      (old.pattern!=next.pattern && page!=Page::Pattern);
}
inline const char *tab_label(Section s, int tab) {
  static constexpr const char *sample[]{"BROWSER","PLAYBACK","SLICE","AUTO"};
  static constexpr const char *pattern[]{"SELECT","STEP","LOCKS","TOOLS"};
  static constexpr const char *perform[]{"PATTERNS","MIX","REPEAT","CHAIN"};
  if(s==Section::Sample) return sample[tab];
  if(s==Section::Pattern) return pattern[tab];
  if(s==Section::Perform) return perform[tab];
  return tab==0 ? "MAIN" : "TONE";
}
inline int active_tab(const Ui &u) {
  switch(u.page) {
  case Page::Tone: case Page::SamplePlayback: case Page::Step: return 1;
  case Page::SampleSlice: case Page::SliceTools: case Page::PerformanceRepeat: return 2;
  case Page::Locks: case Page::ToneLocks: case Page::SampleLocks: return 2;
  case Page::AutoSlice: case Page::Tools: case Page::Chain: return 3;
  case Page::Performance: return u.perf_mixer ? 1 : 0;
  default: return 0;
  }
}
// Cancels only UI intents; musical context and captured release owners persist.
inline bool navigate(Ui &u, int id) {
  Page destination=u.page;
  if(id>=300 && id<=305) {
    constexpr Page home[]{Page::Sequence,Page::Track,Page::Sample,Page::Pattern,Page::Performance,Page::Project};
    destination=home[id-300];
    if(id==304) u.perf_mixer=false;
  } else if(id>=308 && id<=311) {
    const int t=id-308;
    switch(section(u.page)) {
    case Section::Track: destination=t==0 ? Page::Track : Page::Tone; break;
    case Section::Sample: { constexpr Page pages[]{Page::Sample,Page::SamplePlayback,Page::SampleSlice,Page::AutoSlice}; destination=pages[t]; break; }
    case Section::Pattern: { constexpr Page pages[]{Page::Pattern,Page::Step,Page::Locks,Page::Tools}; destination=pages[t]; break; }
    case Section::Perform: { constexpr Page pages[]{Page::Performance,Page::Performance,Page::PerformanceRepeat,Page::Chain}; destination=pages[t]; u.perf_mixer=t==1; break; }
    default: return false;
    }
  } else if(id>=312 && id<=314) {
    constexpr Page pages[]{Page::Locks,Page::ToneLocks,Page::SampleLocks}; destination=pages[id-312];
  } else if(id==315) destination=Page::SliceTools;
  else return false;
  u.cancel_tool(); u.cancel_pattern_action(); u.chain_clear_pending=false;
  u.page=destination; u.capture=-1;
  if((destination==Page::Step || locks(destination)) && u.selected_step<0) u.selected_step=0;
  return true;
}
struct Widget { int id; Rect rect; bool actionable; const char *label; };
struct Layout {
  Widget widgets[40]{}; unsigned count=0;
  void add(int id, Rect r, const char *label="", bool actionable=true) {
    widgets[count++]={id,r,actionable,label};
  }
  void row(int first,int last,int y,int columns=4,int h=52) {
    const int width=768/columns;
    for(int id=first;id<=last;++id)
      add(id,{16+(id-first)%columns*width,y+(id-first)/columns*(h+4),width-8,h});
  }
};
inline Layout build_layout(Page p) {
  Layout l;
  for(int n=0;n<6;++n) { static constexpr const char *names[]{"SEQ","TRACK","SAMPLE","PATTERN","PERFORM","PROJECT"}; l.add(300+n,{n*800/6,416,(n+1)*800/6-n*800/6-2,64},names[n]); }
  l.add(306,{224,2,52,52},"< T"); l.add(307,{280,2,52,52},"T >");
  l.add(29,{336,2,92,52},"PLAY"); l.add(178,{432,2,112,52},"PATTERN");
  l.add(34,{548,2,52,52},"-"); l.add(35,{744,2,52,52},"+");
  const auto s=section(p);
  if(s!=Section::Seq && s!=Section::Project) {
    int tabs=s==Section::Track ? 2 : 4;
    for(int t=0;t<tabs;++t) l.add(308+t,{16+t*768/tabs,60,768/tabs-8,52},tab_label(s,t));
  }
  switch(p) {
  case Page::Sequence:
    l.row(int(SequenceWidget::Step0),int(SequenceWidget::Step15),64,8,52);
    l.row(int(SequenceWidget::Track0),int(SequenceWidget::Track7),232,4,56);
    l.add(68,{16,176,240,52},"EDIT STEP"); l.add(69,{272,176,248,52},"SWING");
    l.add(33,{536,176,240,52},"BANK"); break;
  case Page::Track:
    l.row(int(TrackWidget::Volume),int(TrackWidget::Wave),120,2,52);
    l.add(41,{400,232,376,52},"SOURCE"); l.add(37,{16,288,376,52},"MUTE");
    l.add(43,{400,288,376,52},"SOLO"); l.add(42,{16,344,760,52},"EDIT SAMPLE"); break;
  case Page::Tone: case Page::Fx:
    l.row(107,109,120,1,64); l.add(36,{16,324,760,64},"DELAY"); break;
  case Page::Sample:
    l.row(int(SampleWidget::Previous),int(SampleWidget::Source),120,2,64); l.add(222,{16,268,760,136},"",false); break;
  case Page::SamplePlayback:
    l.row(124,125,120,1,64); l.row(126,128,256,3,64);
    l.add(129,{16,328,760,64},"RESET REGION"); break;
  case Page::SampleSlice:
    l.add(132,{40,116,720,80},"",false); l.row(133,136,200);
    l.row(137,138,256,2); l.add(143,{16,312,376,52},"AUDITION");
    l.add(315,{400,312,376,52},"SLICE TOOLS"); l.add(147,{16,368,760,40},"",false); break;
  case Page::SliceTools:
    l.row(139,142,120,2,80); l.row(133,136,292,4,64);
    l.add(147,{16,364,760,44},"",false); break;
  case Page::AutoSlice:
    l.add(132,{40,116,720,80},"",false); l.row(211,214,200);
    l.add(215,{16,256,760,52},"SENSITIVITY"); l.row(217,219,312,3);
    l.add(221,{16,368,760,40},"",false); break;
  case Page::Pattern:
    l.row(int(PatternWidget::FirstPad),int(PatternWidget::LastPad),120,4);
    l.row(int(PatternWidget::LengthMinus),int(PatternWidget::Inspect),344,6); break;
  case Page::Step:
    l.add(70,{16,120,760,52},"STEP"); l.add(71,{16,180,504,52},"VELOCITY");
    l.add(72,{536,180,240,52},"ACCENT"); l.add(73,{16,240,760,64},"PROBABILITY");
    l.row(74,77,320,4,72); break;
  case Page::Locks: case Page::ToneLocks: case Page::SampleLocks:
    l.add(312,{16,116,184,52},"BASIC"); l.add(313,{208,116,184,52},"TONE");
    l.add(314,{400,116,184,52},"SAMPLE"); l.add(p==Page::ToneLocks ? 120 : 104,{592,116,184,52},"CLEAR LOCKS");
    if(p==Page::SampleLocks) { l.add(148,{16,180,760,52},"",false); l.add(149,{16,240,760,64},"SLICE LOCK"); l.row(150,151,320,2,64); }
    else { const int first=p==Page::Locks ? 96 : 114, rows=p==Page::Locks ? 4 : 3;
      for(int r=0;r<rows;++r) { l.add(first+r*2,{16,176+r*56,504,52},"VALUE"); l.add(first+r*2+1,{536,176+r*56,240,52},"LOCK"); }
    } break;
  case Page::Tools:
    l.row(80,82,116,3); l.row(83,94,176,4,72); break;
  case Page::Performance:
    l.row(int(PerformanceWidget::FirstPad),int(PerformanceWidget::LastPad),120,4); l.add(201,{16,344,248,52},"MODE");
    l.add(202,{272,344,248,52},"CANCEL O"); l.add(203,{528,344,248,52},"CANCEL F"); break;
  case Page::PerformanceRepeat:
    l.row(int(PerformanceWidget::RepeatX2),int(PerformanceWidget::RepeatX8),144,3,220); l.add(205,{16,372,760,36},"",false); break;
  case Page::Chain:
    for(int n=0;n<5;++n) l.add(163+n,{16,116+n*56,376,52},"ENTRY");
    for(int n=0;n<10;++n) l.add(n==9 ? 179 : 169+n,{408+n%2*188,116+n/2*56,180,52},"EDIT");
    break;
  case Page::Project:
    l.add(223,{16,64,760,88},"",false); l.row(154,157,160,2,64);
    l.add(160,{16,304,760,52},"CANCEL / BACK"); l.add(161,{16,364,760,44},"",false); break;
  }
  return l;
}
// Initialized before audio/task startup. Returning immutable references avoids
// nesting multiple 1 KB layout copies on the physical UI task's fixed stack.
struct LayoutStore {
  Layout pages[19]{};
  LayoutStore() { for(unsigned n=0;n<19;++n) pages[n]=build_layout(Page(n)); }
};
inline const LayoutStore layouts;
inline const Layout &layout(Page p) { return layouts.pages[unsigned(p)]; }
inline Rect geometry(Page p,int id) {
  const auto &l=layout(p);
  for(unsigned n=0;n<l.count;++n) if(l.widgets[n].id==id) return l.widgets[n].rect;
  return {0,0,0,0};
}
inline bool visible(Page p,int id) { return geometry(p,id).w!=0; }
inline Rect segment(Rect r,int index,int count) {
  return {r.x+index*r.w/count,r.y,(index+1)*r.w/count-index*r.w/count-4,r.h};
}
inline int segment_choice(Page p,int id,int x,int count) {
  const auto r=geometry(p,id);
  return r.w ? clamp(((x-r.x)*count+count-1)/r.w,0,count-1) : 0;
}
inline int hit(Page p,int x,int y) {
  const auto &l=layout(p);
  for(unsigned n=0;n<l.count;++n) if(l.widgets[n].actionable && l.widgets[n].rect.contains(x,y)) return l.widgets[n].id;
  return -1;
}
inline bool actionable(const Ui &u,int id) {
  if(u.page!=Page::Tools || id<83 || id>94) return true;
  if(u.tool_pending) return id==90 || id==91 ||
      (u.pending_tool.kind==Kind::CopyTrack && (id==88 || id==89));
  if(u.tool_section==0) return id>=83 && id<=87;
  if(u.tool_section==1) return id<=93;
  return id>=83 && id<=86;
}
inline int hit(const Ui &u,int x,int y) {
  const int id=app::ui20::hit(u.page,x,y);
  return actionable(u,id) ? id : -1;
}
inline int text_width(const char *s,int scale) { return int(std::strlen(s))*4*scale; }
inline void bounded_text(char *out,unsigned capacity,const char *s,int width,int scale) {
  const unsigned limit=unsigned(std::max(0,width)/(4*scale));
  const unsigned length=unsigned(std::strlen(s));
  const unsigned count=std::min(std::min(limit,capacity-1),length);
  std::memcpy(out,s,count); out[count]=0;
  if(count<length && count>=3) std::memcpy(out+count-3,"...",3);
}
inline bool slider(const Ui &u,int id) {
  return (id>=24 && id<=28) || (id>=107 && id<=109) || id==69 || id==71 || id==73 ||
      id==124 || id==125 || id==137 || id==138 ||
      (id>=96 && id<=102 && id%2==0) || (id>=114 && id<=118 && id%2==0) ||
      (u.page==Page::Tools && u.tool_section==1 && ((id>=83 && id<=88)||id==93));
}
} // namespace app::ui20
namespace app {
inline Rect page_widget(Page page,int id) { return ui20::geometry(page,id); }
inline int page_drag(Page page,int id,int x) {
  const auto r=page_widget(page,id);
  if(r.w<2) return drag(id,x); // legacy scripted qualification paths
  const int v=clamp(x-r.x,0,r.w-1)*127/(r.w-1);
  return id==25 ? v*2-127 : v;
}
}
