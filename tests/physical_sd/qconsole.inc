std::atomic<int> qcontrol{0};
std::atomic<bool> qrunning{false};
std::atomic<unsigned> qrepeat_accepts{0}, qrepeat_hits{0}, qchain_loops{0};
uint32_t qtimes[16384], qcount=0, qmax=0, qmiss=0, qfail=0, qtimeout=0, qnonzero=0, qoverlap=0, qovermax=0, qactive_max=0;
void qmeasure(unsigned us, esp_err_t err, bool analyzing) {
  qrepeat_accepts.store(engine.repeat_metrics.accepts);
  qrepeat_hits.store(engine.repeat_metrics.hits);
  qchain_loops.store(engine.chain_loops);
  int control=qcontrol.exchange(0);
  if(control==1) { qcount=qmax=qmiss=qfail=qtimeout=qnonzero=qoverlap=qovermax=qactive_max=0; qrunning.store(true); }
  if(control==2) { qrunning.store(false,std::memory_order_release); return; }
  if(!qrunning.load()) return;
  if(qcount<16384) qtimes[qcount]=us;
  ++qcount; qmax=std::max<uint32_t>(qmax,us); qmiss+=us>=5805; qfail+=err!=ESP_OK; qtimeout+=err==ESP_ERR_TIMEOUT;
  if(analyzing) { ++qoverlap; qovermax=std::max<uint32_t>(qovermax,us); }
  unsigned active=0; for(auto &v:samples::voices) active+=v.active;
  qactive_max=std::max<uint32_t>(qactive_max,active);
  for(auto v:out_buf) qnonzero+=v!=0;
}
void qconsole() {
  static char line[128]; static unsigned length=0;
  while(Serial.available()) {
    char ch=Serial.read(); if(ch=='\r') continue;
    if(ch!='\n') { if(length<127) line[length++]=ch; continue; }
    line[length]=0; length=0;
    Serial.printf("[Q COMMAND] %s\n",line);
    char name[40]={}; int kind=0,t=0,value=0,pattern=0,step=0;
    if(sscanf(line,"CMD %d %d %d %d %d",&kind,&t,&value,&pattern,&step)==5 && kind>=0 && kind<int(app::Kind::Count) && t>=0 && t<16 && pattern>=0 && pattern<16 && step>=0 && step<16) {
      app::Command c{app::Kind(kind),uint8_t(t),value}; c.pattern=pattern;c.step=step;
      Serial.printf("[Q ACK] sent=%d\n",send(c));
    } else if(sscanf(line,"PAGE %d",&value)==1 && value>=0 && value<=17) { ui.page=app::Page(value); ui.selected=0;full=true; Serial.printf("[Q ACK] page=%d\n",value); }
    else if(sscanf(line,"UI %d %d %d",&t,&value,&step)==3 && t>=0 && t<=221) { interact(t,value,step); Serial.println("[Q ACK] ui=1"); }
    else if(sscanf(line,"LOAD %d %d",&t,&value)==2) Serial.printf("[Q ACK] load=%d\n",samples::request(t,value));
    else if(sscanf(line,"STORE %d",&value)==1) { samples::qrequest(value); }
    else if(sscanf(line,"SAVE %31s",name)==1) Serial.printf("[Q ACK] save=%d\n",projects::request(projects::Operation::SaveAs,name));
    else if(sscanf(line,"PROJECT %31s",name)==1) Serial.printf("[Q ACK] project=%d\n",projects::request(projects::Operation::Load,name));
    else if(sscanf(line,"MEASURE %d",&value)==1) qcontrol.store(value?1:2);
    else if(!strcmp(line,"METRICS")) {
      if(qrunning.load() || qcontrol.load()) Serial.println("[Q METRICS] pending=1");
      else {
        unsigned n=std::min<uint32_t>(qcount,16384u); std::sort(qtimes,qtimes+n);
        Serial.printf("[Q METRICS] blocks=%u stored=%u p99=%u max=%u misses=%u failures=%u timeouts=%u nonzero=%u overlap=%u overlap_max=%u active_max=%u\n",qcount,n,n?qtimes[n*99/100]:0,qmax,qmiss,qfail,qtimeout,qnonzero,qoverlap,qovermax,qactive_max);
      }
    } else if(!strcmp(line,"TRANSPORT")) {
      const unsigned transport=perf_transport.load(), repeat=perf_repeat.load();
      summary_printf("[Q TRANSPORT] entry=%u repeat=%u mode=%u playing=%u pattern=%u chain_loops=%u repeat_requested=%u repeat_active=%u capture_count=%u\n",transport&63,(transport>>6)&31,(transport>>11)&1,(transport>>12)&1,(transport>>13)&15,qchain_loops.load(),repeat&15,(repeat>>4)&15,(repeat>>12)&31);
    } else if(!strcmp(line,"STATE")) {
      char status[128], sample_status[128]; projects::status(status,128); samples::message(sample_status,128);
      Serial.printf("[Q STATE] bpm=%u swing=%u playing=%d mode=%u chain_length=%u chain_loop=%d project_busy=%d restoring=%d sample_busy=%d project_status=%s sample_status=%s\n",view.bpm,view.swing,view.playing,unsigned(view.mode),view.chain.length,view.chain.loop,projects::busy(),projects::restoring.load(),samples::busy(),status,sample_status);
      for(unsigned i=0;i<view.chain.length;i++) Serial.printf("[Q CHAIN] index=%u pattern=%u repeats=%u\n",i,view.chain.entries[i].pattern,view.chain.entries[i].repeats);
      for(unsigned i=0;i<4;i++) {
        auto &track=view.tracks[i]; samples::Preview preview; samples::preview(i,preview);
        uint32_t waveform=2166136261u; auto *raw=reinterpret_cast<const uint8_t*>(&preview.waveform);
        for(unsigned j=0;j<sizeof(preview.waveform);j++) waveform=(waveform^raw[j])*16777619;
        Serial.printf("[Q TRACK] track=%u source=%d pitch=%d synth_pitch=%d sample_pitch=%d mute=%d solo=%d start=%u end=%u reverse=%d mode=%u choke=%u cutoff=%u resonance=%u send=%u slices=%u enabled=%d selected=%u frames=%u waveform=%08x name=%s\n",i,track.sample,track.pitch,track.synth_pitch,track.sample_pitch,track.muted,bool(view.solos & (1u<<i)),track.playback.start,track.playback.end,track.playback.reverse,unsigned(track.playback.mode),track.playback.choke,track.filter_cutoff,track.filter_resonance,track.delay_send,track.slices.count,track.slice_enabled,track.slices.selected,preview.frames,waveform,preview.name);
        for(unsigned j=0;j<track.slices.count;j++) Serial.printf("[Q SLICE] track=%u index=%u start=%u end=%u\n",i,j,track.slices.slices[j].start,track.slices.slices[j].end);
      }
      for(unsigned p=0;p<4;p++) { Serial.printf("[Q PATTERN] index=%u length=%u masks=%04x,%04x,%04x\n",p,view.patterns[p].length,view.patterns[p].track_steps[0],view.patterns[p].track_steps[1],view.patterns[p].track_steps[2]); for(unsigned s=0;s<4;s++) Serial.printf("[Q LOCK] pattern=%u step=%u mask=%u slice=%u\n",p,s,view.patterns[p].locks[0][s].mask,view.patterns[p].locks[0][s].slice); }
      transients::Status state; transients::status(state); transients::Metrics metrics; transients::metrics(metrics);
      Serial.printf("[Q TRANSIENT] state=%u request=%u progress=%u count=%u stale_discarded=%u apply=%u cancel=%u duration_max_us=%u chunk_max_us=%u\n",unsigned(state.state),state.request_id,state.progress,state.proposal.bank.count,metrics.stale_discarded,metrics.apply,metrics.cancel,metrics.duration_max,metrics.chunk_max);
      for(unsigned j=0;j<state.proposal.bank.count;j++) Serial.printf("[Q PROPOSAL] index=%u start=%u end=%u\n",j,state.proposal.bank.slices[j].start,state.proposal.bank.slices[j].end);
      Serial.printf("[Q HEAP] tag=state internal=%u free=%u largest=%u\n",unsigned(heap_caps_get_free_size(MALLOC_CAP_INTERNAL)),unsigned(heap_caps_get_free_size(MALLOC_CAP_SPIRAM)),unsigned(heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM)));
      Serial.printf("[Q PLAYBACK] triggers=%u reverse=%u gate=%u releases=%u chokes=%u retriggers=%u repeat_accepts=%u repeat_hits=%u chain_loops=%u\n",playback_metrics.triggers,playback_metrics.reverse,playback_metrics.gate,playback_metrics.releases,playback_metrics.choke_ops,playback_metrics.retriggers,qrepeat_accepts.load(),qrepeat_hits.load(),qchain_loops.load());
    } else Serial.println("[Q ACK] invalid=1");
  }
}
