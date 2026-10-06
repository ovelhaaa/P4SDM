#pragma once
#include "hal/display_hal.h"
#include "hal/touch_hal.h"
#include "diagnostics/ui_pattern.h"
#include "esp_heap_caps.h"
#include <atomic>

namespace m41 {
constexpr unsigned LONG_BLOCKS=(SAMPLE_RATE*60+DMA_BUF_LEN-1)/DMA_BUF_LEN;
constexpr unsigned SHORT_CASES=14, ALL_CASES=15;
static bool long_only;
static std::atomic<int> phase{-1},mode{0}; // -1 parked, -2 stop; 0 static,1 light,2 heavy,3 page
static std::atomic<bool> ui_busy{false};
static std::atomic<uint32_t> packed_touch{0},done{0},touch_epoch{0};
static_assert(std::atomic<uint32_t>::is_always_lock_free,"cross-core touch state must be lock-free");
struct Metric {uint64_t sum=0;uint32_t max=0;void add(uint32_t x){sum+=x;max=std::max(max,x);}};
struct Stats {
    uint32_t *raw=nullptr;
    unsigned blocks=0,misses=0,write_errors=0,timeouts=0;
    unsigned active_min=16,active_max=0,nonzero=0,silent=0,rails=0;
    int32_t peak_l=0,peak_r=0;
    uint32_t touch_blocks=0,touch_misses=0;
    uint32_t frames=0,skips=0,display_errors=0,polls=0,touch_errors=0,presses=0,releases=0,drags=0;
    uint32_t pages=0,idle_seen=0,inflight_at_request=0;
    Metric repair,draw,ppa,cache,submit,wait,present,total,dirty_bytes,repair_bytes;
    int64_t start=0,end=0,transition_us=0;
    unsigned transition_block=100;
    size_t ps_before=0,ps_after=0,in_before=0,in_after=0,largest_before=0,largest_after=0;
};
static Stats stats[ALL_CASES];
struct Event {uint32_t ms;uint16_t x,y,raw_x,raw_y;uint8_t kind,phase;};
static Event events[512];
static unsigned event_count=0,event_overflow=0,coverage=0;
static StaticTask_t ui_tcb,touch_tcb;
static StackType_t ui_stack[6000],touch_stack[4000];
static size_t ps_free(){return heap_caps_get_free_size(MALLOC_CAP_SPIRAM);}
static size_t in_free(){return heap_caps_get_free_size(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT);}
static size_t largest(){return heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM);}
static unsigned requested_fps(int m){return m==0?0:m==2?10:30;}
static display::Pipeline strategy(unsigned p) {
    return p<3?display::Pipeline::FullPpa:p<6?display::Pipeline::NativeFull:display::Pipeline::NativeDirty;
}
static const char *name(unsigned p) {
    static const char *names[]={"PPA_STATIC","PPA_LIGHT","PPA_HEAVY","NATIVE_FULL_STATIC","NATIVE_FULL_LIGHT","NATIVE_FULL_HEAVY",
        "DIRTY_STATIC","DIRTY_LIGHT","DIRTY_HEAVY","STATIC_TO_LIGHT","LIGHT_TO_STATIC","LIGHT_TO_HEAVY","HEAVY_TO_LIGHT","PAGE_TO_LIGHT","LONG_LIGHT"};
    return names[p];
}
static int initial_mode(unsigned p) {
    if(p<9) return p%3;
    if(p==9) return 0;
    if(p==10 || p==11) return 1;
    if(p==12) return 2;
    return 1;
}
static int target_mode(unsigned p) {return p==10?0:p==11?2:p==13?3:1;}
static void touch_task(void *) {
    touch::State previous;TickType_t next=xTaskGetTickCount();int64_t last_drag=0;
    while(phase.load(std::memory_order_acquire)!=-2) {
        const int p=phase.load(std::memory_order_acquire);touch::State point;
        const esp_err_t error=touch::poll(point);
        if(p>=0 && p<int(ALL_CASES)) {
            auto &s=stats[p];++s.polls;
            if(error!=ESP_OK) ++s.touch_errors;
            else if(point.updated) {
                const bool changed=point.pressed!=previous.pressed;
                const bool moved=point.pressed && previous.pressed && (point.x!=previous.x || point.y!=previous.y);
                if(changed) point.pressed?++s.presses:++s.releases;
                if(moved) ++s.drags;
                if(changed || moved) touch_epoch.fetch_add(1,std::memory_order_release);
                if(changed || (moved && esp_timer_get_time()-last_drag>=50000)) {
                    if(event_count<512) events[event_count++]={uint32_t(millis()),point.x,point.y,point.raw_x,point.raw_y,uint8_t(changed?(point.pressed?1:0):2),uint8_t(p)};
                    else ++event_overflow;
                    last_drag=esp_timer_get_time();
                }
                if(point.pressed) {
                    if(point.x<80 && point.y<48) coverage|=1;
                    if(point.x>719 && point.y<48) coverage|=2;
                    if(point.x>719 && point.y>431) coverage|=4;
                    if(point.x<80 && point.y>431) coverage|=8;
                    if(std::abs(int(point.x)-400)<50 && std::abs(int(point.y)-240)<50) coverage|=16;
                }
            }
        }
        if(error==ESP_OK) {previous=point;packed_touch.store(point.x|(uint32_t(point.y)<<10)|(uint32_t(point.pressed)<<19),std::memory_order_release);}
        vTaskDelayUntil(&next,pdMS_TO_TICKS(10));
    }
    done.fetch_or(2,std::memory_order_release);vTaskSuspend(nullptr);
}
static void background(unsigned p) {
    ui_pattern::base();
    display::rect(95,177,610,51,ui_pattern::BG);
    display::text(110,182,p==14?"60S TOUCH CORNERS CENTER DRAG TAP":"PIPELINE TEST",0xffff,2);
    for(unsigned i=0;i<8;++i) display::rect(90+i*80,292,24,98,ui_pattern::BG);
    display::rect(60,415,680,24,ui_pattern::BG);
    for(unsigned i=0;i<16;++i) display::rect(70+i*41,420,28,14,0x3186);
}
static void light(unsigned p,unsigned frame,unsigned old_frame,const touch::State &point,const touch::State &old) {
    if(old.pressed) {
        display::clip(int(old.x)-12,int(old.y)-12,25,25); background(p);display::reset_clip();
    }
    display::rect(110,204,330,14,ui_pattern::BG);
    char value[48];snprintf(value,sizeof(value),"BPM 120 STEP %u VALUE %u",frame%16,frame%128);
    display::text(110,205,value,0xffff,2);
    for(unsigned i=0;i<8;++i) {
        const unsigned h=(frame*7+i*17)%90;
        display::rect(90+i*80,292,24,98,ui_pattern::BG);
        display::rect(90+i*80,390-h,24,h,0x07e0);
    }
    display::rect(70+(old_frame%16)*41,420,28,14,0x3186);
    display::rect(70+(frame%16)*41,420,28,14,0xffe0);
    display::rect(60+old_frame*7%680,275,3,14,ui_pattern::BG);
    display::rect(60+frame*7%680,275,3,14,0xffff);
    if(point.pressed) display::circle(point.x,point.y,10,0xffff);
}
static void ui_task(void *) {
    int prior_phase=-1,prior_mode=-1;unsigned frame=0;int64_t due=0;
    touch::State previous;
    while(true) {
        const int p=phase.load(std::memory_order_acquire);if(p==-2) break;
        const int m=mode.load(std::memory_order_acquire);
        if(p<0) {delay(1);continue;}
        auto &s=stats[p];
        if(p!=prior_phase) {
            if(display::set_pipeline(strategy(p))!=ESP_OK) {++s.display_errors;break;}
            prior_phase=p;prior_mode=-1;frame=0;previous={};due=esp_timer_get_time();
        }
        if(m==0) {if(display::idle()) ++s.idle_seen;delay(1);continue;}
        const int64_t now=esp_timer_get_time();if(now<due) {delay(1);continue;}
        ui_busy.store(true,std::memory_order_release);
        const int64_t begin=esp_timer_get_time();
        if(display::begin_frame()!=ESP_OK) {++s.display_errors;break;}
        const int64_t repaired=esp_timer_get_time();
        const uint32_t packed=packed_touch.load(std::memory_order_acquire);
        touch::State point;point.x=packed&1023;point.y=(packed>>10)&511;point.pressed=(packed>>19)&1;
        const unsigned old_frame=frame;++frame;
        const bool page=m==3 || (p==14 && frame%150==0);
        if(prior_mode==-1 || (prior_mode==2 && m!=2) || page) {background(p);++s.pages;}
        if(m==2) ui_pattern::update(frame,point,true);else light(p,frame,old_frame,point,previous);
        const int64_t drawn=esp_timer_get_time();
        const esp_err_t error=display::present();const auto t=display::telemetry();
        const int64_t end=esp_timer_get_time();
        s.repair.add(repaired-begin);s.draw.add(drawn-repaired);s.ppa.add(t.ppa_us);s.cache.add(t.cache_us);
        s.submit.add(t.submit_us);s.wait.add(t.wait_us);s.present.add(t.present_us);s.total.add(end-begin);
        s.dirty_bytes.add(t.dirty_bytes);s.repair_bytes.add(t.repair_bytes);
        if(error!=ESP_OK) {++s.display_errors;break;}
        ++s.frames;previous=point;prior_mode=m;
        ui_busy.store(false,std::memory_order_release);
        if(m==3) {int expected=3;mode.compare_exchange_strong(expected,1);}
        const unsigned interval=1000000/requested_fps(m);
        due+=interval;
        if(end>due) {const unsigned skip=(end-due)/interval;s.skips+=skip;due+=int64_t(skip)*interval;}
    }
    ui_busy.store(false,std::memory_order_release);done.fetch_or(1,std::memory_order_release);vTaskSuspend(nullptr);
}
static void notes() {for(unsigned v=0;v<16;++v) {PCW[v]=0;synthESP32_TRIGGER_P(v,48+v);}}
static void metric_report(const char *label,const Metric &m,unsigned n) {
    reportf("[M41] part=%s avg=%.2f max=%u\n",label,n?double(m.sum)/n:0.,m.max);
}
static void audio_task(void *) {
    const unsigned first=long_only?14:0,last=long_only?15:14;
    const size_t ps_start=ps_free(),in_start=in_free(),largest_start=largest();
    bool valid=true;esp_err_t error=ESP_OK;
    vTaskPrioritySet(nullptr,configMAX_PRIORITIES-1);
    for(unsigned p=first;p<last;++p) {
        auto &s=stats[p];const unsigned count=p==14?LONG_BLOCKS:STAGE_BLOCKS;
        notes();s.ps_before=ps_free();s.in_before=in_free();s.largest_before=largest();
        s.start=esp_timer_get_time();mode.store(initial_mode(p),std::memory_order_release);phase.store(p,std::memory_order_release);
        uint32_t prior_epoch=touch_epoch.load(std::memory_order_acquire);
        for(unsigned b=0;b<count;++b) {
            if(p>=9 && p<=13 && b==s.transition_block) {
                s.transition_us=esp_timer_get_time();s.inflight_at_request=ui_busy.load();mode.store(target_mode(p),std::memory_order_release);
            }
            if(p==14 && b && b%STAGE_BLOCKS==0) notes(); // same original envelope, retrigger before its five-second expiry
            s.active_min=std::min(s.active_min,active_voices());s.active_max=std::max(s.active_max,active_voices());
            const uint32_t epoch=touch_epoch.load(std::memory_order_acquire);const bool touched=epoch!=prior_epoch;prior_epoch=epoch;
            const int64_t start=esp_timer_get_time();render_buffer();const uint32_t us=esp_timer_get_time()-start;
            s.raw[b]=us;if(us>=BUDGET_US) ++s.misses;if(touched) {++s.touch_blocks;if(us>=BUDGET_US) ++s.touch_misses;}
            s.active_min=std::min(s.active_min,active_voices());s.active_max=std::max(s.active_max,active_voices());
            unsigned nonzero=0;
            for(unsigned i=0;i<DMA_BUF_LEN;++i) {
                const int32_t l=out_buf[2*i],r=out_buf[2*i+1];s.peak_l=std::max(s.peak_l,std::abs(l));s.peak_r=std::max(s.peak_r,std::abs(r));
                if((l+r)/2) ++nonzero;
                if(l==-32768 || l==32767 || r==-32768 || r==32767) ++s.rails;
            }
            s.nonzero+=nonzero;if(!nonzero) ++s.silent;
            error=audio::write(out_buf,DMA_BUF_LEN);++s.blocks;
            if(error!=ESP_OK) {++s.write_errors;if(error==ESP_ERR_TIMEOUT) ++s.timeouts;break;}
        }
        s.end=esp_timer_get_time();s.ps_after=ps_free();s.in_after=in_free();s.largest_after=largest();
        valid &= s.blocks==count && s.active_min==16 && s.active_max==16 && !s.misses && !s.write_errors && !s.rails && !s.silent;
        if(error!=ESP_OK) break;
    }
    phase.store(-2,std::memory_order_release);
    memset(out_buf,0,sizeof(out_buf));unsigned drain_errors=0;
    if(error==ESP_OK) for(unsigned i=0;i<audio::DMA_DESCRIPTORS+2;++i) if(audio::write(out_buf,DMA_BUF_LEN)!=ESP_OK) ++drain_errors;
    vTaskPrioritySet(nullptr,1);
    const int64_t deadline=esp_timer_get_time()+1000000;while(done.load()!=3 && esp_timer_get_time()<deadline) delay(2);
    const long ps_loss=long(ps_start)-long(ps_free()),in_loss=long(in_start)-long(in_free());
    guition::memory_report("M41 after stress before audio shutdown");
    const esp_err_t shutdown=audio::end();
    for(unsigned p=first;p<last;++p) {
        auto &s=stats[p];const double seconds=double(s.end-s.start)/1e6;
        reportf("[M41] phase=%u name=%s audio_core=0 audio_priority=24 ui_core=0 ui_priority=2 touch_core=1 touch_priority=3 window_s=%.6f\n",p,name(p),seconds);
        reportf("[M41] blocks=%u active_min=%u active_max=%u render_misses=%u write_errors=%u timeouts=%u nonzero=%u silent=%u rails=%u peak_L=%ld peak_R=%ld\n",s.blocks,s.active_min,s.active_max,s.misses,s.write_errors,s.timeouts,s.nonzero,s.silent,s.rails,long(s.peak_l),long(s.peak_r));
        for(unsigned b=0;b<s.blocks;b+=16) {
            char row[256];int used=snprintf(row,sizeof(row),"[M41] raw phase=%u block=%u us=",p,b);
            for(unsigned j=b;j<std::min(b+16,s.blocks);++j) used+=snprintf(row+used,sizeof(row)-used,"%s%u",j==b?"":",",s.raw[j]);
            reportf("%s\n",row);
        }
        if(p>=9 && p<=13) {
            uint32_t maximum=0;unsigned misses=0;for(unsigned b=80;b<std::min(180u,s.blocks);++b){maximum=std::max(maximum,s.raw[b]);misses+=s.raw[b]>=BUDGET_US;}
            reportf("[M41] transition request_block=%u inflight=%u around_blocks=80..179 max_us=%u misses=%u idle_observations=%u\n",s.transition_block,s.inflight_at_request,maximum,misses,s.idle_seen);
        }
        uint64_t sum=0;for(unsigned b=0;b<s.blocks;++b) sum+=s.raw[b];std::sort(s.raw,s.raw+s.blocks);
        if(s.blocks) reportf("[M41] render min=%u avg=%.2f p50=%u p95=%u p99=%u max=%u headroom_worst_us=%.3f\n",s.raw[0],double(sum)/s.blocks,s.raw[(s.blocks*50+99)/100-1],s.raw[(s.blocks*95+99)/100-1],s.raw[(s.blocks*99+99)/100-1],s.raw[s.blocks-1],BUDGET_US-s.raw[s.blocks-1]);
        reportf("[M41] UI requested_fps=%u frames=%u achieved_fps=%.3f skipped=%u errors=%u pages=%u\n",requested_fps(initial_mode(p)),s.frames,seconds>0?s.frames/seconds:0.,s.skips,s.display_errors,s.pages);
        metric_report("repair",s.repair,s.frames);metric_report("draw",s.draw,s.frames);metric_report("ppa",s.ppa,s.frames);metric_report("cache",s.cache,s.frames);
        metric_report("submit",s.submit,s.frames);metric_report("wait",s.wait,s.frames);metric_report("present",s.present,s.frames);metric_report("total",s.total,s.frames);
        metric_report("dirty_bytes",s.dirty_bytes,s.frames);metric_report("repair_bytes",s.repair_bytes,s.frames);
        reportf("[M41] touch polls=%u hz=%.3f errors=%u presses=%u releases=%u drags=%u activity_blocks=%u activity_misses=%u\n",s.polls,seconds>0?s.polls/seconds:0.,s.touch_errors,s.presses,s.releases,s.drags,s.touch_blocks,s.touch_misses);
        reportf("[M41] memory ps_before=%u ps_after=%u largest_before=%u largest_after=%u internal_before=%u internal_after=%u\n",unsigned(s.ps_before),unsigned(s.ps_after),unsigned(s.largest_before),unsigned(s.largest_after),unsigned(s.in_before),unsigned(s.in_after));
        valid &= !s.display_errors && !s.touch_errors;
    }
    for(unsigned i=0;i<event_count;++i) {const auto &e=events[i];reportf("[M41] event phase=%u ms=%u kind=%u raw=%u,%u logical=%u,%u\n",e.phase,e.ms,e.kind,e.raw_x,e.raw_y,e.x,e.y);}
    reportf("[M41] touch_coverage_bits=%u event_count=%u overflow=%u shared_codec=%s shared_touch=%s\n",coverage,event_count,event_overflow,esp_err_to_name(i2c_master_probe(guition::i2c_bus(),0x18,100)),esp_err_to_name(i2c_master_probe(guition::i2c_bus(),touch::address(),100)));
    reportf("[M41] heap_loss psram=%ld internal=%ld largest_before=%u largest_after=%u workers=%u drain_errors=%u shutdown=%s result=%s\n",ps_loss,in_loss,unsigned(largest_start),unsigned(largest()),done.load(),drain_errors,esp_err_to_name(shutdown),valid && !ps_loss && !in_loss && !drain_errors && done.load()==3 && shutdown==ESP_OK?"PASS":"FAIL");
    guition::memory_report("M41 after audio shutdown");vTaskDelete(nullptr);
}
static void initialize(void *) {
    guition::memory_report("M41 before display");
    const auto pipeline=long_only?display::Pipeline::NativeDirty:display::Pipeline::FullPpa;
    esp_err_t error=display::begin(pipeline);reportf("[M41] display_begin=%s pipeline=%s fence=2\n",esp_err_to_name(error),long_only?"NativeDirty":"FullPpa");if(error!=ESP_OK) {vTaskDelete(nullptr);return;}
    guition::memory_report("M41 after display");background(long_only?14:0);error=display::present();if(error!=ESP_OK){reportf("[M41 INIT_FAIL] present=%s\n",esp_err_to_name(error));vTaskDelete(nullptr);return;}
    error=touch::begin();reportf("[M41] touch=%s address=0x%02x id=%s\n",esp_err_to_name(error),touch::address(),touch::product_id());if(error!=ESP_OK){vTaskDelete(nullptr);return;}
    guition::memory_report("M41 after touch");
    synthESP32_begin();initADSR();
    for(byte v=0;v<16;++v){ROTvalue[v][16]=1;ROTvalue[v][1]=v%WT_COUNT;ROTvalue[v][9]=3;ROTvalue[v][13]=-120+v*16;ROTvalue[v][10]=127;ROTvalue[v][11]=64;ROTvalue[v][14]=80;ROTvalue[v][15]=0;addnextsnd[v]=1;detune[v]=0;}
    setSoundALL();synthESP32_setMVol(60);synthESP32_setMFilter(0);
    if(!myDelay.init(88200)){reportf("[M41 INIT_FAIL] Delay\n");vTaskDelete(nullptr);return;}
    myDelay.setTime(12000);myDelay.setFeedback(120);myDelay.setInputLevel(160);is_delay=true;delays=0xffff;level_delay=100;
    guition::memory_report("M41 after Delay");
    const unsigned first=long_only?14:0,last=long_only?15:14;
    const unsigned samples=long_only?LONG_BLOCKS:SHORT_CASES*STAGE_BLOCKS;
    auto *raw=static_cast<uint32_t *>(heap_caps_malloc(samples*sizeof(uint32_t),MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT));
    if(!raw){reportf("[M41 INIT_FAIL] fixed timing storage\n");vTaskDelete(nullptr);return;}
    for(unsigned p=first;p<last;++p) stats[p].raw=raw+(p-first)*STAGE_BLOCKS;
    const auto bus=guition::i2c_bus();error=audio::begin(SAMPLE_RATE);
    reportf("[M41] audio_begin=%s shared_bus_unchanged=%u voices=16 Delay=88200 rate=44100 frames=256\n",esp_err_to_name(error),bus==guition::i2c_bus());
    if(error!=ESP_OK){vTaskDelete(nullptr);return;}
    xTaskCreateStaticPinnedToCore(ui_task,"m41ui",sizeof(ui_stack),nullptr,2,ui_stack,&ui_tcb,0);
    xTaskCreateStaticPinnedToCore(touch_task,"m41touch",sizeof(touch_stack),nullptr,3,touch_stack,&touch_tcb,1);
    guition::memory_report("M41 tasks transport and capture storage live");
    reportf("[M41 READY] %s audio starts after this message; no serial output until stopped\n",long_only?"LONG 60 seconds: touch four corners, center, drag and rapid taps":"SHORT 14 windows: three pipelines plus five transitions");
    if(xTaskCreatePinnedToCore(audio_task,"m41audio",8000,nullptr,1,nullptr,0)!=pdPASS){phase.store(-2);audio::end();reportf("[M41 INIT_FAIL] audio task\n");}
    vTaskSuspend(nullptr); // retain startup task storage during all heap comparisons
}
}
void setup() {
    gpio_set_level(guition::PA_ENABLE,0);gpio_set_direction(guition::PA_ENABLE,GPIO_MODE_OUTPUT);
    Serial.setTxBufferSize(4096);Serial.begin(115200);Serial.setTxTimeoutMs(1000);
    const uint32_t start=millis();while(!Serial && millis()-start<3000) delay(10);
    reportf("[M41 SELECT] send L for 60-second NativeDirty run, otherwise short reference/alternatives/transitions; selection window 5 seconds\n");
    const uint32_t selection=millis();while(millis()-selection<5000){if(Serial.available()){const int c=Serial.read();if(c=='L'){m41::long_only=true;break;}if(c=='S') break;}delay(10);}
    if(!psramFound() || esp_psram_get_size()!=32u*1024u*1024u){reportf("[M41 INIT_FAIL] PSRAM\n");return;}
    xTaskCreatePinnedToCore(m41::initialize,"m41init",8000,nullptr,1,nullptr,0);
}
void loop(){delay(1000);}
