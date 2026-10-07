std::atomic<int> qjob{0};
template<class... A> __attribute__((noinline)) void qprintf(const char* format,A... args) {
  char line[512]; int n=snprintf(line,sizeof(line),format,args...);
  for(int at=0;at<n && at<511;at+=48) { Serial.write(reinterpret_cast<uint8_t*>(line+at),std::min(48,std::min(n,511)-at)); vTaskDelay(1); }
}
void qheap(const char* tag) {
  Serial.printf("[Q HEAP] tag=%s internal=%u free=%u largest=%u payload=%u\n",tag,
    unsigned(heap_caps_get_free_size(MALLOC_CAP_INTERNAL)),unsigned(heap_caps_get_free_size(MALLOC_CAP_SPIRAM)),
    unsigned(heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM)),unsigned(payload));
}
__attribute__((noinline)) void qinventory(const char* root, unsigned depth=0) {
  File dir=storage::open(root);
  if(!dir || !dir.isDirectory()) { Serial.printf("[Q DIR] path=%s accessible=0\n",root); return; }
  Serial.printf("[Q DIR] path=%s accessible=1\n",root);
  for(;;) {
    File f=dir.openNextFile(); if(!f) break;
    char path[192]; snprintf(path,sizeof(path),"%s/%s",root,f.name());
    Serial.printf("[Q FILE] path=%s bytes=%u directory=%d\n",path,unsigned(f.size()),f.isDirectory());
    if(f.isDirectory() && depth<3) { f.close(); qinventory(path,depth+1); }
    else if(!f.isDirectory() && String(path).endsWith(".wav")) {
      sampler::Wav wav;
      auto read=[&](uint64_t p,uint8_t* b,unsigned n) { return p<=f.size() && n<=f.size()-p && f.seek(p) && f.read(b,n)==n; };
      const char* error=sampler::parse(f.size(),read,wav);
      Serial.printf("[Q WAV] name=%s accepted_format=%d reason=%s frames=%u channels=%u rate=%u pcm_bytes=%u\n",f.name(),!error,error?error:"PCM16",wav.frames,wav.channels,wav.rate,wav.bytes);
    } else if(!f.isDirectory() && String(path).endsWith(".P4P") && f.size()==project::file_bytes) {
      auto* bytes=static_cast<uint8_t*>(heap_caps_malloc(project::file_bytes,MALLOC_CAP_SPIRAM));
      auto* state=static_cast<project::State*>(heap_caps_malloc(sizeof(project::State),MALLOC_CAP_SPIRAM));
      if(bytes && state) {
        new(state) project::State;
        bool read=f.read(bytes,project::file_bytes)==project::file_bytes;
        auto error=read?project::decode(bytes,project::file_bytes,*state):project::Error::Io;
        qprintf("[Q SLOT] path=%s version=%u generation=%u crc=%08x valid=%d reason=%s bpm=%u swing=%u chain_length=%u mode=%u ref0=%s ref1=%s\n",path,project::u16(bytes+4),project::u32(bytes+12),project::u32(bytes+16),error==project::Error::Ok,project::message(error),state->bpm,state->swing,state->chain.length,unsigned(state->mode),state->references[0],state->references[1]);
      }
      heap_caps_free(state); heap_caps_free(bytes);
    }
    vTaskDelay(1);
  }
}
__attribute__((noinline)) void qseed() {
  SD_MMC.mkdir("/P4SDM"); SD_MMC.mkdir("/P4SDM/SAMPLES"); SD_MMC.mkdir("/P4SDM/PROJECTS");
  const char* names[]={"m19p_impulses.wav","m19p_kick.wav","m19p_snare.wav","m19p_mixed.wav","m19p_stereo.wav","m19p_large.wav","m19p_bad_depth.wav","m19p_truncated.wav","m19p_oneshot.wav"};
  for(unsigned k=0;k<9;k++) {
    char path[128]; snprintf(path,sizeof(path),"/P4SDM/SAMPLES/%s",names[k]);
    if(SD_MMC.exists(path)) { Serial.printf("[Q SEED] name=%s existing=1 preserved=1\n",names[k]); continue; }
    const uint32_t frames=k==5?2097152:k==8?22050:176400;
    const unsigned channels=k==4?2:1;
    uint8_t h[44]={}; memcpy(h,"RIFF",4); memcpy(h+8,"WAVEfmt ",8); memcpy(h+36,"data",4);
    auto put_header=[&](unsigned at,uint32_t value,unsigned n) { for(unsigned j=0;j<n;j++) h[at+j]=value>>(8*j); };
    const unsigned bytes=frames*channels*2;
    put_header(4,bytes+36,4); put_header(16,16,4); put_header(20,1,2); put_header(22,channels,2); put_header(24,44100,4);
    put_header(28,44100*channels*2,4); put_header(32,channels*2,2); put_header(34,k==6?24:16,2); put_header(40,bytes,4);
    File f=SD_MMC.open(path,FILE_WRITE); bool ok=f && f.write(h,44)==44;
    static uint8_t block[4096]; uint32_t rng=0x19491949;
    for(unsigned at=0;ok && at<frames;) {
      unsigned n=std::min<unsigned>(1024,frames-at);
      for(unsigned i=0;i<n;i++) {
        unsigned frame=at+i, phase=frame%11025, kind=k==3 || k==4 || k==5 ? (frame/11025)%3 : k;
        if(k==8) phase=frame;
        rng=rng*1664525u+1013904223u;
        int sample=0;
        if(kind==0) sample=phase==0?28000:0;
        else if(kind==1 || k==8) { if(phase<8000) sample=(int(phase%700)-350)*(8000-int(phase))/100; }
        else { if(phase<3500) sample=int(int16_t(rng>>16))*(3500-int(phase))/4200; }
        sample=app::clamp(sample,-30000,30000);
        for(unsigned c=0;c<channels;c++) {
          int16_t value=c?sample/2:sample;
          const unsigned offset=(i*channels+c)*2; block[offset]=value; block[offset+1]=uint16_t(value)>>8;
        }
      }
      unsigned count=n*channels*2; if(k==7 && at+n==frames) count-=3;
      ok=f.write(block,count)==count; at+=n; vTaskDelay(1);
    }
    f.flush(); f.close(); Serial.printf("[Q SEED] name=%s created=%d\n",names[k],ok);
  }
  indexed.store(0); scan();
}
__attribute__((noinline)) void qstore_poll() {
  int op=qjob.exchange(0); if(!op) return;
  if(op==1) qseed();
  if(op==2 || op==7) { Serial.printf("[Q FS] total=%llu used=%llu\n",SD_MMC.totalBytes(),SD_MMC.usedBytes()); qinventory("/P4SDM"); }
  if(op==3 || op==4) {
    const char* a="/P4SDM/SAMPLES/m19p_snare.wav"; const char* b="/P4SDM/SAMPLES/m19p_snare.qmissing";
    if(op==4) std::swap(a,b);
    bool ok=SD_MMC.exists(a) && !SD_MMC.exists(b) && SD_MMC.rename(a,b);
    Serial.printf("[Q RENAME] restore=%d ok=%d\n",op==4,ok);
  }
  if(op==5 || op==6) {
    const char* paths[]={"/P4SDM/PROJECTS/M19P_SD_QUAL/A.P4P","/P4SDM/PROJECTS/M19P_SD_QUAL/B.P4P"};
    const char* backup="/P4SDM/PROJECTS/M19P_SD_QUAL/QBACKUP.BIN";
    if(op==5 && !SD_MMC.exists(backup)) {
      uint32_t generation[2]={};
      for(unsigned i=0;i<2;i++) { File f=storage::open(paths[i]); uint8_t header[20]; if(f && f.read(header,20)==20) generation[i]=project::u32(header+12); }
      const unsigned slot=generation[1]>generation[0];
      File source=storage::open(paths[slot]); File copy=SD_MMC.open(backup,FILE_WRITE);
      uint8_t block[512]; unsigned total=0; bool ok=source && copy;
      while(ok && source.available()) { unsigned n=source.read(block,512); ok=n && copy.write(block,n)==n; total+=n; }
      copy.flush(); copy.close(); source.close();
      if(ok && total==project::file_bytes) {
        File f=SD_MMC.open(paths[slot],"r+"); uint8_t byte=0;
        ok=f && f.seek(40) && f.read(&byte,1)==1 && f.seek(40); byte^=0x55;
        ok=ok && f.write(&byte,1)==1; f.flush(); f.close();
      }
      Serial.printf("[Q CORRUPT] slot=%u generation=%u backup_bytes=%u ok=%d\n",slot,generation[slot],total,ok);
    } else if(op==6 && SD_MMC.exists(backup)) {
      File source=storage::open(backup); uint8_t header[20]; bool ok=source && source.read(header,20)==20;
      unsigned slot=0; uint32_t gen=project::u32(header+12);
      for(unsigned i=0;i<2;i++) { File f=storage::open(paths[i]); uint8_t h[20]; if(f && f.read(h,20)==20 && project::u32(h+12)==gen) slot=i; }
      File f=SD_MMC.open(paths[slot],FILE_WRITE); ok=ok && f && source.seek(0); uint8_t block[512];
      while(ok && source.available()) { unsigned n=source.read(block,512); ok=n && f.write(block,n)==n; }
      f.flush(); f.close(); source.close();
      if(ok) SD_MMC.remove(backup);
      Serial.printf("[Q CORRUPT RESTORE] slot=%u ok=%d\n",slot,ok);
    } else Serial.printf("[Q CORRUPT] op=%d refused=1\n",op);
  }
  qheap("store_done"); Serial.printf("[Q STORE DONE] op=%d\n",op);
}
