#include "../src/app/wav.h"
#include "../src/app/project.h"
#include "../src/app/voice_state.h"
#include "../src/app/m212_fixture.h"
#include <array>
#include <cassert>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

struct Record { unsigned block,type,part,kind,track,pattern,step,pitch_source;int value;std::string bank; };
void unhex(const std::string &s,void *destination,size_t bytes) {
  assert(s.size()==bytes*2); auto *out=static_cast<uint8_t *>(destination);
  for(size_t i=0;i<bytes;++i) out[i]=uint8_t(std::stoul(s.substr(i*2,2),nullptr,16));
}
int main(int argc,char **argv) {
  assert(argc==3);
  std::ifstream input(argv[1]);
  unsigned engine_bytes,count,blocks,expected_hash,expected_events;
  input>>engine_bytes>>count>>blocks>>expected_hash>>expected_events;
  assert(engine_bytes==sizeof(app::Engine) && blocks==13782 && count>1000);
  app::Engine e;std::string initial;input>>initial;unhex(initial,&e,sizeof(e));
  std::array<uint32_t,16> expected_durations;
  for(auto &n:expected_durations) input>>n;
  std::vector<Record> records(count);
  for(auto &r:records) input>>r.block>>r.type>>r.part>>r.kind>>r.track>>r.value>>r.pattern>>r.step>>r.pitch_source>>r.bank;
  assert(input && records.back().block<blocks);
  static std::array<std::array<int16_t,65536>,16> pcm;
  sampler::Sample sources[16]; sampler::Voice voices[16]; app::VoiceRouting routing;
  sampler::PlaybackMetrics metrics;
  app::VoiceState voice_state[16]{};
  auto update_track=[&](unsigned t) {
    if(routing.pcm(t)) voices[t].set_increment(sampler::pitch_increment(
      voice_state[t].effective(e.tracks[t],routing.pcm(t)).pitch));
  };
  for(unsigned t=0;t<16;++t) {
    for(unsigned n=0;n<65536;++n) pcm[t][n]=int16_t(((t*65536+n)%97)*160-7680);
    sources[t].data=pcm[t].data();sources[t].frames=65536;
    voices[t].assign(&sources[t]);
  }
  project::State staged; std::array<uint8_t,project::file_bytes> bytes;
  m212::Trace trace;
  std::ofstream canonical(argv[2],std::ios::binary);
  auto add=[&](uint32_t value) { canonical.write(reinterpret_cast<const char *>(&value),4); };
  unsigned block=0,frame=0,project_transactions=0,bank_applications=0,record=0;
  auto trigger=[&](app::TriggerEvent event,bool ratchet=false,int source=-1) {
    const unsigned t=event.track;const bool sample=source<0 ? e.tracks[t].sample : source!=0;
    trace.event(event,ratchet,sample,e.rng);
    add(0x45564e54);add(block*256+frame);
    for(uint32_t value:{uint32_t(event.track),uint32_t(event.pitch),uint32_t(event.velocity),
        uint32_t(event.volume),uint32_t(uint8_t(event.pan)),uint32_t(event.wave),uint32_t(event.locked_mask),
        uint32_t(event.filter_cutoff),uint32_t(event.filter_resonance),uint32_t(event.delay_send),
        uint32_t(event.slice),uint32_t(event.playback.start),uint32_t(event.playback.end),
        uint32_t(event.playback.reverse),uint32_t(event.playback.mode),uint32_t(event.sequenced),
        uint32_t(ratchet),uint32_t(sample),e.rng}) add(value);
    routing.trigger(t,sample);
    voice_state[t].event=event;
    if(sample) sampler::trigger_voice(voices,t,sampler::pitch_increment(event.pitch),
                                      event.playback,event.sequenced,ratchet,metrics);
  };
  for(block=0;block<blocks;++block) {
    frame=0;
    while(record<count && records[record].block==block) {
      const auto &r=records[record++];
      app::Command c{app::Kind(r.kind),uint8_t(r.track),r.value};
      c.pattern=uint8_t(r.pattern);c.step=uint8_t(r.step);
      c.pitch_source=app::Command::PitchSource(r.pitch_source);
      add(0x434d4420);add(block);add(r.type);add(r.part);add(r.kind);add(r.track);
      add(uint32_t(r.value));add(r.pattern);add(r.step);
      add(r.pitch_source);
      if(r.type==1) {
        e.apply(c);
        if(c.kind==app::Kind::Source) { routing.trigger(c.track,e.tracks[c.track].sample);voice_state[c.track].event={};voices[c.track].stop(); }
        if(c.kind==app::Kind::SliceAudition && e.tracks[c.track].sample)
          trigger(app::resolve_event(c.track,e.tracks[c.track],127,{},c.step));
        else if(c.kind==app::Kind::Trigger) e.audition(c.track,[&](app::TriggerEvent event){trigger(event);});
        else if(c.kind==app::Kind::GateRelease) { if(!voices[c.track].sequenced) voices[c.track].release(); }
        else if(c.kind==app::Kind::Play && !c.value) {
          for(auto &v:voices) if(v.sequenced) v.release();
        }
        else if(c.kind>=app::Kind::Volume && c.kind<=app::Kind::Wave) update_track(c.track);
      } else if(r.type==8) e.apply(c);
      else if(r.type==2) {
        project::snapshot_part(e,staged,r.part);
        if(r.part==16) {
          assert(project::encode(staged,1,bytes.data(),bytes.size()));
          canonical.write(reinterpret_cast<const char *>(bytes.data()),bytes.size());
          assert(project::decode(bytes.data(),bytes.size(),staged)==project::Error::Ok);
          ++project_transactions;
        }
      } else if(r.type==3) {
        project::apply_part(e,staged,r.part);
        if(!r.part) for(unsigned t=0;t<16;++t) { voices[t].stop();voice_state[t]={};routing.trigger(t,e.tracks[t].sample);update_track(t); }
      } else if(r.type==4) {
        unhex(r.bank,&e.tracks[r.part].slices,sizeof(sampler::SliceBank));
        e.tracks[r.part].slice_enabled=true;++bank_applications;
      } else if(r.type==5) e.selected_pattern=e.playing_pattern=15;
      else if(r.type==6) {
        // Exact direct mutations in the inherited Repeat fixture, between the
        // recorded RepeatStop and PerfClearMix commands. No new event schedule.
        e.bpm=240;e.swing=50;e.delay=true;
        auto &pattern=e.patterns[15];pattern={};pattern.length=1;
        for(unsigned t=0;t<16;++t) {
          pattern.track_steps[t]=1;pattern.meta[t][0]={100,100,4};
          pattern.locks[t][0]={255,60,60,int8_t(int(t)*8-60),uint8_t(t),64,40,127,uint8_t(t)};
          auto &track=e.tracks[t];track.muted=false;track.playback={};
          track.playback.mode=t%2 ? sampler::Mode::Gate : sampler::Mode::OneShot;
          track.slices.divide(track.playback,16);track.slice_enabled=true;
        }
        e.solos=0;
      } else if(r.type==7) {
        if(c.kind==app::Kind::GateRelease) { if(!voices[c.track].sequenced) voices[c.track].release(); }
        else e.audition(c.track,[&](app::TriggerEvent event){trigger(event);});
      } else assert(false);
    }
    for(frame=0;frame<256;++frame) {
      e.sample([&](app::TriggerEvent event,bool ratchet,bool sample){trigger(event,ratchet,sample);},
               [&](int t){if(voices[t].sequenced) voices[t].release();});
      for(unsigned t=0;t<16;++t) if(routing.pcm(t)) {
        trace.active_frames[t]+=voices[t].active && !voices[t].releasing;
        voices[t].next();
      }
    }
    trace.block(e,voices);
    add(0x424c4f43);add(block);add(e.rng);add(e.playing_pattern);add(e.chain_entry);
    add(e.chain_repeat);add(e.chain_loops);add(e.performance.repeat.active);
    add(uint32_t(e.phase));add(uint32_t(e.phase>>32));add(uint32_t(e.step));
    add(e.playing);add(e.first);add(uint32_t(e.mode));add(e.selected_pattern);
    add(uint32_t(e.queued_pattern));add(uint32_t(e.duration));add(uint32_t(e.duration>>32));
    add(uint32_t(e.next_ratchet));add(uint32_t(e.next_ratchet>>32));add(e.pair_swing);
    for(unsigned t=0;t<16;++t) {
      const auto &v=voices[t];
      add(trace.active_frames[t]);
      for(uint32_t value:{uint32_t(routing.pcm(t)),uint32_t(v.remaining),uint32_t(v.attack),
          uint32_t(v.release_left),uint32_t(v.releasing),uint32_t(v.sequenced)}) { trace.add(value);add(value); }
      for(uint32_t value:{uint32_t(v.active),uint32_t(v.increment),uint32_t(v.increment>>32),
          uint32_t(v.position),uint32_t(v.position>>32),v.region.start,v.region.end,
          uint32_t(v.playback.reverse),uint32_t(v.playback.mode)}) add(value);
    }
  }
  assert(record==count && project_transactions==3 && bank_applications>0);
  for(unsigned t=0;t<16;++t) assert(trace.active_frames[t]==expected_durations[t]);
  printf("original A journal replay hash=%u expected=%u events=%u expected_events=%u records=%u projects=%u transient_banks=%u rng=%u chain=%u x8=%u\n",
         trace.hash,expected_hash,trace.events,expected_events,count,project_transactions,
         bank_applications,trace.final_rng,trace.final_chain,trace.final_repeat);
  std::fflush(stdout);
  assert(trace.hash==expected_hash && trace.events==expected_events && canonical);
}
