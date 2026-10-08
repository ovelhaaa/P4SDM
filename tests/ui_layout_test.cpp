#include "../src/app/ui_layout.h"
#include "../src/app/project_ui.h"
#include "../src/hal/display_dirty.h"
#include <cassert>
#include <iostream>
using namespace app;
bool intersects(Rect a,Rect b) {
  return a.x<b.x+b.w && b.x<a.x+a.w && a.y<b.y+b.h && b.y<a.y+a.h;
}
bool inside(Rect a,Rect b) {
  return a.x>=b.x && a.y>=b.y && a.x+a.w<=b.x+b.w && a.y+a.h<=b.y+b.h;
}
int main() {
  unsigned widgets=0,edges=0,pages=0;
  for(int page=0;page<=int(Page::SliceTools);++page) {
    const auto p=Page(page); const auto &l=ui20::layout(p); ++pages;
    assert(l.count<=40);
    for(unsigned i=0;i<l.count;++i) {
      const auto &w=l.widgets[i]; const auto r=w.rect; ++widgets;
      assert(r.w>0 && r.h>0);
      if(w.id>=300 && w.id<=305) assert(inside(r,ui20::bottom));
      else if(w.id==306||w.id==307||w.id==29||w.id==178||w.id==34||w.id==35) assert(inside(r,ui20::status));
      else { assert(inside(r,ui20::content)); assert(!intersects(r,ui20::diagnostics)); }
      assert(!intersects(r,ui20::context_status) && !intersects(r,ui20::tempo_status));
      assert(ui20::text_width(w.label,ui20::control_scale)+16<=r.w);
      for(unsigned j=i+1;j<l.count;++j) {
        assert(w.id!=l.widgets[j].id);
        // Borders/playheads/proposals are drawn inside their owner; no separate
        // control rectangles are whitelisted to overlap.
        assert(!intersects(r,l.widgets[j].rect));
      }
      if(!w.actionable) continue;
      assert(r.h>=52 && r.w>=52);
      assert(ui20::hit(p,r.x,r.y)==w.id);
      assert(ui20::hit(p,r.x+r.w-1,r.y+r.h-1)==w.id);
      assert(ui20::hit(p,r.x-1,r.y+r.h/2)!=w.id);
      assert(ui20::hit(p,r.x+r.w,r.y+r.h/2)!=w.id);
      assert(ui20::hit(p,r.x+r.w/2,r.y-1)!=w.id);
      assert(ui20::hit(p,r.x+r.w/2,r.y+r.h)!=w.id); edges+=6;
    }
    for(int top=300;top<=305;++top) {
      Ui u; u.page=p; u.selected=15; u.selected_step=15;
      u.tool_pending=u.chain_clear_pending=true; u.copy_source=u.clear_pattern=7;
      assert(ui20::navigate(u,top)); assert(int(ui20::section(u.page))==top-300);
      assert(u.selected==15 && u.selected_step==15);
      assert(!u.tool_pending && !u.chain_clear_pending && u.copy_source<0 && u.clear_pattern<0);
    }
    const auto section=ui20::section(p);
    int tabs=section==ui20::Section::Track?2:section==ui20::Section::Seq||section==ui20::Section::Project?0:4;
    for(int t=0;t<tabs;++t) {
      Ui u;u.page=p;u.selected=15; assert(ui20::navigate(u,308+t));
      assert(ui20::active_tab(u)==t && ui20::section(u.page)==section);
      assert(u.selected==15);
      if(section==ui20::Section::Pattern && (t==1||t==2)) assert(u.selected_step==0);
    }
  }
  for(const char *text:{"TRACK 16 SAMPLE","PATTERN 16 QUEUED EDITING","400 BPM","CHOKE 8","SLICE 16/16 ACTIVE 16","PROJECT_9999 *","MISSING SAMPLE / UNSUPPORTED DEPTH","CHAIN REPEAT 16","WAVE - SYNTH ONLY","CUTOFF 127 LOCKED","PITCH 127 LOCKED","TUNE +67 st","LONG_PROJECT_NAME_123456789012345678901234567890"}) {
    for(int width:{52,120,184,240,504,760}) for(int scale:{1,2,3,4}) {
      char bounded[128]; ui20::bounded_text(bounded,sizeof(bounded),text,width-16,2);
      ui20::bounded_text(bounded,sizeof(bounded),text,width-16,scale);
      assert(ui20::text_width(bounded,scale)+16<=width);
      if(ui20::text_width(text,scale)<=width-16) assert(!std::strcmp(text,bounded));
    }
  }
  Engine e; Ui u; u.page=Page::Locks; u.selected=15; u.selected_step=15;
  e.selected_pattern=15;
  Command c{}; assert(u.lock_action(97,0,true,e,c)); e.apply(c);
  auto r=page_widget(u.page,96);
  assert(u.lock_action(96,r.x,true,e,c)&&c.value==0&&c.track==15&&c.pattern==15&&c.step==15);
  assert(u.lock_action(96,r.x+r.w-1,false,e,c)&&c.value==127);
  u.page=Page::SampleLocks; assert(!u.slice_lock_action(149,true,e,c));
  e.tracks[15].sample=true; assert(u.slice_lock_action(149,true,e,c));
  // Project cancellation clears the captured operation, including navigation
  // away from confirmation; a later confirm cannot use hidden pending state.
  project::Workflow workflow;
  assert(!workflow.confirm(projects::Operation::Load,"PROJECT_9999",true));
  assert(workflow.mode==project::Workflow::Mode::Confirm);
  workflow.cancel(); assert(workflow.mode==project::Workflow::Mode::Home && workflow.pending==projects::Operation::None);
  ui20::Confirmation confirm;
  c={Kind::SliceDelete,15,0}; c.step=15;
  assert(!confirm.accept(141,c,1));
  assert(!confirm.accept(141,c,2)); // replacing PCM invalidates old approval
  c.track=14; assert(!confirm.accept(141,c,2));
  assert(confirm.accept(141,c,2) && confirm.widget==-1);
  assert(!confirm.accept(141,c,2)); confirm.cancel(); assert(confirm.widget==-1);
  ui20::Context context{15,15,true,1,1},next=context;
  assert(!ui20::full_invalidation(context,next,Page::Track)); // continuous edit
  next.track=14; assert(ui20::full_invalidation(context,next,Page::Track));
  next=context; next.sample=false; assert(ui20::full_invalidation(context,next,Page::SamplePlayback));
  next=context; ++next.sample_revision; assert(ui20::full_invalidation(context,next,Page::SampleSlice));
  next=context; ++next.project_revision; assert(ui20::full_invalidation(context,next,Page::Locks));
  next=context; next.pattern=14; assert(ui20::full_invalidation(context,next,Page::Step));
  assert(!ui20::full_invalidation(context,next,Page::Pattern)); // old/new pads only
  ui20::Notification notice;
  notice.show("QUEUE FULL",100);
  assert(!notice.expire(4599) && notice.active);
  assert(notice.expire(4600) && !notice.active && !notice.expire(4601));
  notice.show("PROJECT SAVED",UINT32_MAX-100);
  assert(!notice.expire(2000) && notice.expire(5000));
  for(auto pair:{std::pair<Page,int>{Page::Track,41},{Page::SamplePlayback,126},{Page::SamplePlayback,127},{Page::Sequence,178},{Page::Performance,201},{Page::AutoSlice,215}}) {
    const auto r=page_widget(pair.first,pair.second);
    const int count=pair.second==215?3:2;
    for(int n=0;n<count;++n) {
      const auto cell=ui20::segment(r,n,count);
      assert(cell.w>=52 && cell.h>=52 && inside(cell,r));
      assert(ui20::segment_choice(pair.first,pair.second,cell.x,count)==n);
      assert(ui20::segment_choice(pair.first,pair.second,cell.x+cell.w-1,count)==n);
    }
  }
  for(int category=0;category<3;++category) for(bool pending:{false,true}) {
    Ui tools; tools.page=Page::Tools;tools.tool_section=category;tools.tool_pending=pending;
    tools.pending_tool.kind=Kind::CopyTrack;
    for(int id=83;id<=94;++id) {
      const auto r=page_widget(tools.page,id);
      assert(ui20::hit(tools,r.x,r.y)==(ui20::actionable(tools,id)?id:-1));
    }
  }
  // Dirty-region updates remain bounded to a widget (including its overlays).
  display::DirtyMask slider,head,full;
  r=page_widget(Page::Track,24); slider.logical_rect(r.x,r.y,r.w,r.h);
  r=page_widget(Page::Sequence,0); head.logical_rect(r.x,r.y,r.w,r.h);
  r=page_widget(Page::Sequence,1); head.logical_rect(r.x,r.y,r.w,r.h);
  full.all(); assert(slider.bytes()<full.bytes()/8 && head.bytes()<full.bytes()/8);
  static_assert(project::version==2 && project::file_bytes==52704);
  std::cout<<"M20 layout PASS pages="<<pages<<" widgets="<<widgets<<" hit_edges="<<edges
      <<" overlaps=0 text_fit=PASS navigation=PASS slider_dirty="<<slider.bytes()<<" playhead_dirty="<<head.bytes()<<"\n";
}
