#pragma once
#include "hal/display_hal.h"
#include "hal/touch_hal.h"
#include "diagnostics/ui_pattern.h"
#include "esp_heap_caps.h"
#include <atomic>

// No engine mutex. Only UI/touch share a lock-free 32-bit touch snapshot.
static std::atomic<int> ui_phase{-1};
static std::atomic<uint32_t> touch_snapshot{0}, workers_done{0};
static_assert(std::atomic<uint32_t>::is_always_lock_free,"snapshot must be lock-free");
struct UiStats {
    uint32_t frames=0,skipped=0,errors=0,polls=0,poll_errors=0,presses=0,releases=0,updates=0;
    uint64_t frame_sum=0; uint32_t frame_max=0;
    int64_t start=0,end=0;
    uint32_t core_observed=99;
    size_t psram_start=0,internal_start=0,largest_start=0,psram_end=0,internal_end=0,largest_end=0;
};
static UiStats ui_stats[6];
struct TouchEvent { uint32_t ms; uint16_t x,y,raw_x,raw_y; uint8_t down,count; };
static TouchEvent touch_events[128];
static uint32_t touch_event_count=0,touch_event_overflow=0,touch_coverage=0;
static TaskHandle_t ui_handles[2],touch_handle;
static StaticTask_t ui_tcbs[2],touch_tcb;
static StackType_t ui_stacks[2][5000],touch_stack[4000];
static size_t ps_free() {return heap_caps_get_free_size(MALLOC_CAP_SPIRAM);}
static size_t in_free() {return heap_caps_get_free_size(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT);}
static size_t ps_largest() {return heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM);}
static void touch_worker(void *) {
    touch::State last;
    TickType_t next=xTaskGetTickCount();
    while(ui_phase.load(std::memory_order_acquire)!=-2) {
        const int phase=ui_phase.load(std::memory_order_acquire);
        touch::State point;
        const esp_err_t error=touch::poll(point);
        if(phase>=0 && phase<6) {
            UiStats &stats=ui_stats[phase]; ++stats.polls;
            if(error!=ESP_OK) ++stats.poll_errors;
            else if(point.updated) {
                ++stats.updates;
                if(point.pressed!=last.pressed) {
                    point.pressed?++stats.presses:++stats.releases;
                    if(touch_event_count<128) touch_events[touch_event_count++]={uint32_t(millis()),point.x,point.y,point.raw_x,point.raw_y,uint8_t(point.pressed),point.count};
                    else ++touch_event_overflow;
                }
                if(point.pressed) {
                    if(point.x<80 && point.y<48) touch_coverage|=1;
                    if(point.x>719 && point.y<48) touch_coverage|=2;
                    if(point.x>719 && point.y>431) touch_coverage|=4;
                    if(point.x<80 && point.y>431) touch_coverage|=8;
                    if(std::abs(int(point.x)-400)<50 && std::abs(int(point.y)-240)<50) touch_coverage|=16;
                }
            }
        }
        if(error==ESP_OK) {
            last=point;
            touch_snapshot.store(point.x|(uint32_t(point.y)<<10)|(uint32_t(point.pressed)<<19)|(uint32_t(point.count)<<20),std::memory_order_release);
        }
        vTaskDelayUntil(&next,pdMS_TO_TICKS(10));
    }
    workers_done.fetch_or(4,std::memory_order_release); vTaskSuspend(nullptr);
}
static void ui_worker(void *argument) {
    const unsigned core=reinterpret_cast<uintptr_t>(argument);
    int old_phase=-1;
    unsigned frame=0;
    int64_t due=0;
    while(true) {
        const int phase=ui_phase.load(std::memory_order_acquire);
        if(phase==-2) break;
        // First 3 modes: UI core1. Second 3 modes: UI core0, audio always core0.
        if(phase<0 || phase>=6 || (phase<3?1u:0u)!=core || phase%3==0) {delay(2);continue;}
        UiStats &stats=ui_stats[phase];
        if(phase!=old_phase) {old_phase=phase; frame=0; due=esp_timer_get_time();}
        const int64_t now=esp_timer_get_time();
        if(now<due) {delay(1);continue;}
        const int64_t begin=esp_timer_get_time();
        if(frame==0) ui_pattern::base();
        const uint32_t packed=touch_snapshot.load(std::memory_order_acquire);
        touch::State point; point.x=packed&1023; point.y=(packed>>10)&511; point.pressed=(packed>>19)&1; point.count=(packed>>20)&7;
        ui_pattern::update(++frame,point,phase%3==2);
        const esp_err_t error=display::present();
        const int64_t end=esp_timer_get_time();
        stats.core_observed=xPortGetCoreID();
        ++stats.frames; stats.frame_sum+=end-begin; stats.frame_max=std::max(stats.frame_max,uint32_t(end-begin));
        if(error!=ESP_OK) {++stats.errors; ui_phase.store(-2,std::memory_order_release);break;}
        due+=33333; // requested 30 FPS; skip late UI slots, never block audio.
        if(end>due) {
            const unsigned skip=(end-due)/33333;
            stats.skipped+=skip; due+=int64_t(skip)*33333;
        }
    }
    workers_done.fetch_or(1u<<core,std::memory_order_release); vTaskSuspend(nullptr);
}
static void ui_audio_task(void *) {
    vTaskPrioritySet(nullptr,1);
    guition::memory_report("combined running before measurement"); delay(100);
    const size_t ps_before=ps_free(),in_before=in_free();
    esp_err_t last_error=ESP_OK;
    bool valid=true;
    for(unsigned phase=0;phase<6;++phase) {
        if(ui_phase.load()==-2) {valid=false;break;}
        Stage &s=stages[phase]; s.voices=16;
        UiStats &u=ui_stats[phase];
        // Envelope trigger and note/phase setup are outside timed rendering;
        // delay history stays live across modes, with no allocation or clearing.
        for(unsigned v=0;v<16;++v) {PCW[v]=0; synthESP32_TRIGGER_P(v,48+v);}
        u.psram_start=ps_free(); u.internal_start=in_free(); u.largest_start=ps_largest();
        u.start=esp_timer_get_time();
        ui_phase.store(phase,std::memory_order_release);
        vTaskPrioritySet(nullptr,configMAX_PRIORITIES-1);
        for(unsigned block=0;block<STAGE_BLOCKS;++block) {
            const int64_t start=esp_timer_get_time(); observe_active(s);
            const int64_t render_start=esp_timer_get_time(); render_buffer();
            const int64_t rendered=esp_timer_get_time(); observe_active(s);
            unsigned nonzero=0;
            for(unsigned i=0;i<DMA_BUF_LEN;++i) {
                const int32_t l=out_buf[2*i],r=out_buf[2*i+1],mono=(l+r)/2;
                s.peak_l=std::max(s.peak_l,std::abs(l)); s.peak_r=std::max(s.peak_r,std::abs(r)); s.peak_mono=std::max(s.peak_mono,std::abs(mono));
                if(mono) ++nonzero;
                if(l==-32768 || l==32767 || r==-32768 || r==32767 || mono==-32768 || mono==32767) ++s.rails;
            }
            s.nonzero+=nonzero; if(!nonzero) ++s.silent_blocks;
            const int64_t write_start=esp_timer_get_time(); last_error=audio::write(out_buf,DMA_BUF_LEN);
            const int64_t end=esp_timer_get_time();
            s.render.values[block]=rendered-render_start; s.write.values[block]=end-write_start; s.cycle.values[block]=end-start;
            if(s.render.values[block]>=BUDGET_US) ++s.render_misses;
            if(s.cycle.values[block]>=BUDGET_US) ++s.cycle_misses;
            ++s.blocks;
            if(last_error!=ESP_OK) {++s.failures; if(last_error==ESP_ERR_TIMEOUT) ++s.timeouts; break;}
        }
        vTaskPrioritySet(nullptr,1);
        u.end=esp_timer_get_time();
        u.psram_end=ps_free(); u.internal_end=in_free(); u.largest_end=ps_largest();
        valid &= s.blocks==STAGE_BLOCKS && s.active_min==16 && s.active_max==16 && s.nonzero && !s.rails && !s.silent_blocks && !s.failures && !s.render_misses;
        if(last_error!=ESP_OK) break;
    }
    ui_phase.store(-2,std::memory_order_release);
    memset(out_buf,0,sizeof(out_buf));
    unsigned drain_errors=0;
    if(last_error==ESP_OK) for(unsigned i=0;i<audio::DMA_DESCRIPTORS+2;++i)
        if(audio::write(out_buf,DMA_BUF_LEN)!=ESP_OK) {++drain_errors;break;}
    const int64_t stop_deadline=esp_timer_get_time()+1000000;
    while(workers_done.load(std::memory_order_acquire)!=7 && esp_timer_get_time()<stop_deadline) delay(5);
    guition::memory_report("after UI audio stress before shutdown"); delay(100);
    const long ps_loss=long(ps_before)-long(ps_free()),in_loss=long(in_before)-long(in_free());
    const esp_err_t shutdown=audio::end();
    reportf("[M4] heap_loss psram=%ld internal=%ld workers_done=%u audio_shutdown=%s drain_errors=%u\n",ps_loss,in_loss,workers_done.load(),esp_err_to_name(shutdown),drain_errors);
    for(unsigned phase=0;phase<6;++phase) {
        Stage &s=stages[phase]; UiStats &u=ui_stats[phase];
        const char *mode=phase%3==0?"A_STATIC":phase%3==1?"B_LIGHT":"C_HEAVY";
        const unsigned ui_core=phase<3?1:0;
        reportf("[M4] stage=%u mode=%s audio_core=0 audio_priority=%u ui_core=%u ui_priority=2 touch_core=1 touch_priority=3\n",phase,mode,configMAX_PRIORITIES-1,ui_core);
        reportf("[M4] blocks=%u active_min=%u active_max=%u render_misses=%u cycle_misses=%u write_failures=%u timeouts=%u\n",s.blocks,s.active_min,s.active_max,s.render_misses,s.cycle_misses,s.failures,s.timeouts);
        for(unsigned b=0;b<s.blocks;b+=16) {
            char row[256]; int used=snprintf(row,sizeof(row),"[M4] raw stage=%u block=%u us=",phase,b);
            for(unsigned j=b;j<std::min(b+16,s.blocks);++j) used+=snprintf(row+used,sizeof(row)-used,"%s%u",j==b?"":",",s.render.values[j]);
            reportf("%s\n",row);
        }
        print_timing("render",s.render,s.blocks); print_timing("write",s.write,s.blocks); print_timing("cycle",s.cycle,s.blocks);
        if(s.blocks) {
            const unsigned p99=s.render.values[(s.blocks*99+99)/100-1],maximum=s.render.values[s.blocks-1];
            reportf("[M4] delta_vs_M32_delay p99=%ld max=%ld headroom_p99_us=%.3f headroom_max_us=%.3f category=%s\n",long(p99)-2898,long(maximum)-3592,BUDGET_US-p99,BUDGET_US-maximum,s.render_misses?"FAIL":maximum<=BUDGET_US*.8?"SAFE":"TIGHT");
        }
        reportf("[M4] PCM peak_L=%ld peak_R=%ld peak_mono=%ld nonzero_frames=%u silent_blocks=%u rail_frames=%u\n",long(s.peak_l),long(s.peak_r),long(s.peak_mono),s.nonzero,s.silent_blocks,s.rails);
        const double seconds=double(u.end-u.start)/1e6;
        reportf("[M4] UI requested_fps=%u frames=%u achieved_fps=%.3f update_avg_us=%.2f update_max_us=%u skipped=%u errors=%u core_observed=%u window_s=%.6f\n",phase%3?30:0,u.frames,seconds>0?u.frames/seconds:0.,u.frames?double(u.frame_sum)/u.frames:0.,u.frame_max,u.skipped,u.errors,u.core_observed,seconds);
        reportf("[M4] touch requested_hz=100 polls=%u achieved_hz=%.3f errors=%u fresh_updates=%u presses=%u releases=%u\n",u.polls,seconds>0?u.polls/seconds:0.,u.poll_errors,u.updates,u.presses,u.releases);
        reportf("[M4] memory_start psram_free=%u largest=%u internal_free=%u\n",unsigned(u.psram_start),unsigned(u.largest_start),unsigned(u.internal_start));
        reportf("[M4] memory_end psram_free=%u largest=%u internal_free=%u\n",unsigned(u.psram_end),unsigned(u.largest_end),unsigned(u.internal_end));
        valid &= !u.errors && !u.poll_errors && (phase%3==0 || u.frames>0);
    }
    for(unsigned i=0;i<touch_event_count;++i) {
        const auto &event=touch_events[i]; reportf("[M4] touch_event ms=%u down=%u raw=%u,%u logical=%u,%u count=%u\n",event.ms,event.down,event.raw_x,event.raw_y,event.x,event.y,event.count);
    }
    reportf("[M4] touch_coverage_bits=%u event_overflow=%u codec_probe=%s touch_probe=%s\n",touch_coverage,touch_event_overflow,
        esp_err_to_name(i2c_master_probe(guition::i2c_bus(),0x18,100)),esp_err_to_name(i2c_master_probe(guition::i2c_bus(),touch::address(),100)));
    reportf("[M4] result=%s audio_PCM_render_and_transport_only audible_unverified DMA_starvation_metric_unavailable\n",valid && !ps_loss && !in_loss && !drain_errors && workers_done.load()==7 && shutdown==ESP_OK?"PASS":"FAIL");
    guition::memory_report("after audio shutdown display remains active"); delay(100);
    vTaskDelete(nullptr);
}
void setup() {
    gpio_set_level(guition::PA_ENABLE,0); gpio_set_direction(guition::PA_ENABLE,GPIO_MODE_OUTPUT);
    Serial.setTxBufferSize(4096); Serial.begin(115200); Serial.setTxTimeoutMs(1000);
    const uint32_t start=millis(); while(!Serial && millis()-start<3000) delay(10);
    reportf("[M4] UI audio CPU=%u MHz native=480x800 logical=800x480 PPA_CCW=270 RGB565 native_fbs=2 logical_fbs=1\n",ESP.getCpuFreqMHz());
    if(!psramFound() || esp_psram_get_size()!=32u*1024u*1024u) {reportf("[M4 INIT_FAIL] PSRAM\n");return;}
    guition::memory_report("before display"); delay(100);
    esp_err_t error=display::begin(); if(error!=ESP_OK) {reportf("[M4 INIT_FAIL] display=%s\n",esp_err_to_name(error));return;}
    guition::memory_report("after display"); delay(100);
    ui_pattern::base(); error=display::present(); if(error!=ESP_OK) {reportf("[M4 INIT_FAIL] present=%s\n",esp_err_to_name(error));return;}
    error=touch::begin();
    reportf("[M4] touch_begin=%s address=0x%02x id=%s native=%ux%u\n",esp_err_to_name(error),touch::address(),touch::product_id(),touch::native_width(),touch::native_height());
    if(error!=ESP_OK) return;
    guition::memory_report("after touch"); delay(100);
    is_reverb=is_delay=is_chorus=is_flanger=is_tremolo=is_ringmod=is_distortion=is_bitcrusher=false;
    synthESP32_begin(); initADSR();
    for(byte voice=0;voice<16;++voice) {
        ROTvalue[voice][16]=1; ROTvalue[voice][1]=voice%WT_COUNT; ROTvalue[voice][9]=3; ROTvalue[voice][13]=-120+voice*16;
        ROTvalue[voice][10]=127; ROTvalue[voice][11]=64; ROTvalue[voice][14]=80; ROTvalue[voice][15]=0; addnextsnd[voice]=1; detune[voice]=0;
    }
    setSoundALL(); synthESP32_setMVol(60); synthESP32_setMFilter(0);
    if(!myDelay.init(88200)) {reportf("[M4 INIT_FAIL] delay\n");return;}
    myDelay.setTime(12000); myDelay.setFeedback(120); myDelay.setInputLevel(160);
    is_delay=true; delays=0xffff; level_delay=100;
    guition::memory_report("after Delay FX initialization"); delay(100);
    const auto original_bus=guition::i2c_bus();
    error=audio::begin(SAMPLE_RATE);
    reportf("[M4] audio_begin=%s shared_bus_unchanged=%u codec_probe=%s touch_probe=%s\n",esp_err_to_name(error),original_bus==guition::i2c_bus(),
        esp_err_to_name(i2c_master_probe(guition::i2c_bus(),0x18,100)),esp_err_to_name(i2c_master_probe(guition::i2c_bus(),touch::address(),100)));
    if(error!=ESP_OK) return;
    ui_handles[0]=xTaskCreateStaticPinnedToCore(ui_worker,"ui0",sizeof(ui_stacks[0]),reinterpret_cast<void *>(0),2,ui_stacks[0],&ui_tcbs[0],0);
    ui_handles[1]=xTaskCreateStaticPinnedToCore(ui_worker,"ui1",sizeof(ui_stacks[1]),reinterpret_cast<void *>(1),2,ui_stacks[1],&ui_tcbs[1],1);
    touch_handle=xTaskCreateStaticPinnedToCore(touch_worker,"gt911",sizeof(touch_stack),nullptr,3,touch_stack,&touch_tcb,1);
    if(!ui_handles[0] || !ui_handles[1] || !touch_handle) {ui_phase.store(-2);audio::end();reportf("[M4 INIT_FAIL] workers\n");return;}
    reportf("[M4 READY] 6 stages A/B/C UI core1 then core0; audio core0 priority24; 16 voices plus original Delay; 517 blocks each; touch100Hz UI30FPS\n");
    if(xTaskCreatePinnedToCore(ui_audio_task,"audio",8000,nullptr,configMAX_PRIORITIES-1,nullptr,0)!=pdPASS) {
        ui_phase.store(-2); audio::end(); reportf("[M4 INIT_FAIL] audio_task\n");
    }
}
void loop() {delay(1000);}
