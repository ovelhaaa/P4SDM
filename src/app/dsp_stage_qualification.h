#pragma once
// Included after the real engine/filter/Delay definitions. Startup only.
// Grouped component costs are diagnostic attribution, never additive estimates
// of a full render. Each group has one timer pair and yields outside timing.
inline void qualify_dsp_stages(sampler::Sample *samples) {
  for(unsigned t=0;t<16;++t) if(!samples[t].data || samples[t].frames<512) {
    Serial.println("[M213 DSP] FAIL resident PCM"); return;
  }
  static volatile int16_t input[256];
  static sampler::PcmReadCache caches[16];
  sampler::Voice voices[16];
  LowPassFilter filters[16];
  SimpleDelay diagnostic_delay;
  if(!diagnostic_delay.init(88200)) return;
  diagnostic_delay.setTime(12000);diagnostic_delay.setFeedback(120);
  diagnostic_delay.setInputLevel(160);
  for(unsigned n=0;n<256;++n) input[n]=samples[0].data[n];
  for(unsigned t=0;t<16;++t) {
    filters[t].setCutoffFreq(p4tone::cutoff(30+t*4));
    filters[t].setResonance(p4tone::resonance(30+t*4,96));
    voices[t].assign(&samples[t]);
  }
  const char *names[]={"input_control","psram_lookup","cache_lookup_fill",
    "linear32","linear_magnitude","voice_nearest","voice_linear",
    "filter","velocity_pan_mix_send","delay","output_conversion",
    "fade_reference","fade_identity"};
  volatile uint32_t sink=0;
  volatile uint8_t fade=32;
  uint64_t timer_us=0;
  for(unsigned rep=0;rep<32;++rep) {
    const auto start=esp_timer_get_time();
    timer_us+=esp_timer_get_time()-start;
    vTaskDelay(1);
  }
  Serial.printf("[M213 DSP timers] pairs=32 us=%llu per_sample_timers=0\n",(unsigned long long)timer_us);
  for(unsigned stage=0;stage<13;++stage) {
    uint64_t total=0; uint32_t worst=0,checksum=0;
    for(unsigned rep=0;rep<32;++rep) {
      for(unsigned t=0;t<16;++t) {
        caches[t].invalidate();
        voices[t].trigger(sampler::pitch_increment(59));
      }
      const auto start=esp_timer_get_time();
      for(unsigned n=0;n<256;++n) for(unsigned t=0;t<16;++t) {
        const int16_t x=input[n];
        int32_t result=0;
        switch(stage) {
        case 0: result=x; break;
        case 1: result=static_cast<volatile int16_t *>(samples[t].data)[n]; break;
        case 2: result=caches[t].read(samples[t].data,{0,samples[t].frames},n); break;
        case 3: result=sampler::linear_fixed32(x,input[(n+1)&255],uint16_t(n*251+rep)); break;
        case 4: result=sampler::linear_fixed32_magnitude(x,input[(n+1)&255],uint16_t(n*251+rep)); break;
        case 5: result=voices[t].next(sampler::Interpolation::Nearest,&caches[t]); break;
        case 6: result=voices[t].next(sampler::Interpolation::Linear,&caches[t]); break;
        case 7: result=filters[t].next(x); break;
        case 8: {
          const int16_t velocity=app::scale_velocity(x,uint16_t(100*(60+t*4)));
          const int32_t l=(int32_t(velocity)*int(32+t*8)*255)>>16;
          const int32_t r=(int32_t(velocity)*int(240-t*8)*255)>>16;
          result=l+r+p4tone::resolved_delay_send(l,uint8_t(30+t*4))+
                     p4tone::resolved_delay_send(r,uint8_t(30+t*4)); break;
        }
        case 9: { int32_t l,r; diagnostic_delay.process(x,x,l,r); result=l+r; break; }
        case 10: result=int16_t(soft_clip((int32_t(x)*60)>>8)); break;
        case 11: { const unsigned divisor=fade, gain=n<divisor ? n : divisor; result=int32_t(x)*int(gain)/int(divisor); break; }
        case 12: { const unsigned divisor=fade, gain=n<divisor ? n : divisor; result=gain==divisor ? x : int32_t(x)*int(gain)/int(divisor); break; }
        }
        checksum+=uint32_t(result);
      }
      const uint32_t us=uint32_t(esp_timer_get_time()-start);
      total+=us; if(us>worst) worst=us;
      sink=checksum; vTaskDelay(1);
    }
    Serial.printf("[M213 DSP stage] id=%u name=%s groups=32 operations=131072 us=%llu max=%u checksum=%u\n",
      stage,names[stage],(unsigned long long)total,worst,checksum);
    delay(20);
  }
  Serial.printf("[M213 DSP complete] startup_only=1 selected_magnitude=%u sink=%u additive=0\n",
    unsigned(P4SDM_LINEAR_MAGNITUDE),unsigned(sink));
}
