// Diagnostic copy only. Allocate before audio; publish an immutable journal at
// the inherited capture boundary, then dump from loop(), never from audio.
struct M214Record {
  uint32_t block;
  unsigned type, part;
  app::Command command;
  sampler::SliceBank bank;
};
M214Record *m214_records=nullptr;
app::Engine *m214_initial=nullptr;
unsigned m214_count=0, m214_overflow=0;
bool m214_recording=false;
std::atomic<bool> m214_ui_complete{false};
m212::Trace m214_trace;
void m214_record(unsigned type,unsigned part=0,app::Command c={}) {
  if(!m214_recording || captured.load(std::memory_order_relaxed)>=capture_blocks) return;
  if(m214_count>=32768) { ++m214_overflow; return; }
  auto &r=m214_records[m214_count++]; r.block=captured.load();
  r.type=type;r.part=part;r.command=c;
  if(type==4) r.bank=engine.tracks[part].slices;
}
void m214_journal_apply(app::Command c) { m214_record(8,0,c);engine.apply(c); }
void m214_journal_begin() {
  static_assert(std::is_trivially_copyable<app::Engine>::value);
  m214_initial=static_cast<app::Engine *>(heap_caps_malloc(sizeof(app::Engine),MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT));
  m214_records=static_cast<M214Record *>(heap_caps_malloc(sizeof(M214Record)*32768,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT));
  if(!m214_initial || !m214_records) { Serial.println("[M214 journal] FAIL allocation");return; }
  new (m214_initial) app::Engine;
  for(unsigned i=0;i<32768;++i) new (m214_records+i) M214Record;
  memcpy(m214_initial,&engine,sizeof(engine));m214_recording=true;
}
void m214_journal_dump() {
  while(!m214_ui_complete.load(std::memory_order_acquire)) delay(10);
  summary_printf("[M214 journal] engine_bytes=%u records=%u overflow=%u blocks=%u hash=%u events=%u rng=%u chain=%u x8=%u\n",
    unsigned(sizeof(app::Engine)),m214_count,m214_overflow,unsigned(capture_blocks),
    m214_trace.hash,m214_trace.events,m214_trace.final_rng,m214_trace.final_chain,m214_trace.final_repeat);
  for(unsigned t=0;t<16;++t)
    summary_printf("[M214 duration] track=%u active_frames=%u\n",t,m214_trace.active_frames[t]);
  const auto *raw=reinterpret_cast<const uint8_t *>(m214_initial);
  for(unsigned at=0;at<sizeof(app::Engine);at+=64) {
    char hex[129];unsigned n=std::min(64u,unsigned(sizeof(app::Engine))-at);
    for(unsigned i=0;i<n;++i) snprintf(hex+i*2,3,"%02x",raw[at+i]);
    summary_printf("[M214 initial] offset=%u data=%s\n",at,hex);
  }
  for(unsigned i=0;i<m214_count;++i) {
    const auto &r=m214_records[i];const auto &c=r.command;
    char hex[133]={};
    if(r.type==4) {
      const auto *bank=reinterpret_cast<const uint8_t *>(&r.bank);
      for(unsigned b=0;b<sizeof(r.bank);++b) snprintf(hex+b*2,3,"%02x",bank[b]);
    }
    summary_printf("[M214 record] index=%u block=%u type=%u part=%u kind=%u track=%u value=%d pattern=%u step=%u pitch_source=%u bank=%s\n",
      i,r.block,r.type,r.part,unsigned(c.kind),unsigned(c.track),c.value,unsigned(c.pattern),unsigned(c.step),unsigned(c.pitch_source),hex);
  }
  summary_printf("[M214 journal] END records=%u\n",m214_count);
}
