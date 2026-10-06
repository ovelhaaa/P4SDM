#if P4SDM_DISPLAY
// Hardware recipe adapted from ultramcu/guition-jc4880p4-bsp, MIT,
// commit 324970bade0d1f4e52880fe8016580368bc1e06e. See vendor/SOURCE.md.
#include "display_hal.h"
#include "display_dirty.h"
#include "guition_board.h"
#include "driver/gpio.h"
#include "driver/ppa.h"
#include "esp_ldo_regulator.h"
#include "esp_lcd_mipi_dsi.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_heap_caps.h"
#include "esp_memory_utils.h"
#include "esp_cache.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include <algorithm>
#include <atomic>
#include <cstdlib>
#include <cstring>
#include "vendor/board_p4_st7701_init.h"

namespace display {
static esp_ldo_channel_handle_t ldo;
static esp_lcd_dsi_bus_handle_t bus;
static esp_lcd_panel_io_handle_t io;
static esp_lcd_panel_handle_t panel;
static ppa_client_handle_t ppa;
static uint16_t *logical, *native[3];
static int back=1;
static bool initialized;
static bool frame_open=false,faulted=false;
static Pipeline pipeline;
static DirtyHistory history;
static TripleHistory triple;
static RefreshFence fence;
static std::atomic<uint32_t> completions{0};
uint32_t completed_presentations() {return completions.load(std::memory_order_acquire);}
static DirtyMask changed,flushed;
static int clip_x0=0,clip_y0=0,clip_x1=WIDTH,clip_y1=HEIGHT;
void reset_clip() {clip_x0=clip_y0=0;clip_x1=WIDTH;clip_y1=HEIGHT;}
void clip(int x,int y,int w,int h) {
    clip_x0=std::max(x,0);clip_y0=std::max(y,0);
    clip_x1=int(std::min<int64_t>(int64_t(x)+std::max(w,0),WIDTH));
    clip_y1=int(std::min<int64_t>(int64_t(y)+std::max(h,0),HEIGHT));
}
static Telemetry last_telemetry;
Telemetry telemetry() {return last_telemetry;}
static StaticSemaphore_t refresh_storage;
static SemaphoreHandle_t refresh_sem;
static std::atomic<uint32_t> refreshes{0};
static_assert(std::atomic<uint32_t>::is_always_lock_free,"ISR counter must be lock-free");
static bool IRAM_ATTR refreshed(esp_lcd_panel_handle_t,esp_lcd_dpi_panel_event_data_t *,void *) {
    refreshes.fetch_add(1,std::memory_order_relaxed);
    BaseType_t wake=pdFALSE;
    xSemaphoreGiveFromISR(refresh_sem,&wake);
    return wake==pdTRUE;
}
uint32_t refresh_count() { return refreshes.load(std::memory_order_relaxed); }
bool ready() { return initialized; }
void backlight(bool on) { gpio_set_level(guition::LCD_BACKLIGHT,on?1:0); }
esp_err_t end() {
    initialized=false;
    backlight(false);
    esp_err_t first=ESP_OK;
    auto check=[&](esp_err_t e) { if(first==ESP_OK && e!=ESP_OK) first=e; };
    if(ppa) { check(ppa_unregister_client(ppa)); ppa=nullptr; }
    if(panel) { check(esp_lcd_panel_del(panel)); panel=nullptr; }
    if(io) { check(esp_lcd_panel_io_del(io)); io=nullptr; }
    if(bus) { check(esp_lcd_del_dsi_bus(bus)); bus=nullptr; }
    if(ldo) { check(esp_ldo_release_channel(ldo)); ldo=nullptr; }
    free(logical); logical=nullptr; native[0]=native[1]=native[2]=nullptr;
    return first;
}
esp_err_t begin(Pipeline requested,bool reserve_third) {
    if(initialized) return ESP_OK;
    gpio_set_direction(guition::LCD_BACKLIGHT,GPIO_MODE_OUTPUT);
    backlight(false);
    refresh_sem=xSemaphoreCreateBinaryStatic(&refresh_storage);
    esp_err_t error=ESP_OK;
    auto check=[&](esp_err_t e) { error=e; if(e!=ESP_OK) end(); return e==ESP_OK; };
    esp_ldo_channel_config_t power={}; power.chan_id=3; power.voltage_mv=2500;
    if(!check(esp_ldo_acquire_channel(&power,&ldo))) return error;
    esp_lcd_dsi_bus_config_t dsi={}; dsi.bus_id=0; dsi.num_data_lanes=2;
    dsi.phy_clk_src=MIPI_DSI_PHY_CLK_SRC_DEFAULT; dsi.lane_bit_rate_mbps=500;
    if(!check(esp_lcd_new_dsi_bus(&dsi,&bus))) return error;
    esp_lcd_dbi_io_config_t dbi={}; dbi.virtual_channel=0; dbi.lcd_cmd_bits=8; dbi.lcd_param_bits=8;
    if(!check(esp_lcd_new_panel_io_dbi(bus,&dbi,&io))) return error;
    esp_lcd_dpi_panel_config_t dpi={}; dpi.virtual_channel=0;
    dpi.dpi_clk_src=MIPI_DSI_DPI_CLK_SRC_DEFAULT; dpi.dpi_clock_freq_mhz=34;
    dpi.pixel_format=LCD_COLOR_PIXEL_FORMAT_RGB565;
    dpi.in_color_format=dpi.out_color_format=LCD_COLOR_FMT_RGB565;
    dpi.num_fbs=reserve_third || requested==Pipeline::NativeQueued?3:2;
    dpi.video_timing.h_size=NATIVE_WIDTH; dpi.video_timing.v_size=NATIVE_HEIGHT;
    dpi.video_timing.hsync_pulse_width=12; dpi.video_timing.hsync_back_porch=42; dpi.video_timing.hsync_front_porch=42;
    dpi.video_timing.vsync_pulse_width=2; dpi.video_timing.vsync_back_porch=8; dpi.video_timing.vsync_front_porch=166;
    dpi.flags.use_dma2d=1;
    if(!check(esp_lcd_new_panel_dpi(bus,&dpi,&panel))) return error;
    gpio_set_direction(guition::LCD_RESET,GPIO_MODE_OUTPUT);
    gpio_set_level(guition::LCD_RESET,0); vTaskDelay(pdMS_TO_TICKS(20));
    gpio_set_level(guition::LCD_RESET,1); vTaskDelay(pdMS_TO_TICKS(120));
    for(const auto &command : board_p4_panel_init_cmds) {
        if(!check(esp_lcd_panel_io_tx_param(io,command.cmd,command.data,command.len))) return error;
        if(command.delay_ms) vTaskDelay(pdMS_TO_TICKS(command.delay_ms));
    }
    esp_lcd_dpi_panel_event_callbacks_t callbacks={}; callbacks.on_refresh_done=refreshed;
    if(!check(esp_lcd_dpi_panel_register_event_callbacks(panel,&callbacks,nullptr))) return error;
    if(!check(esp_lcd_panel_init(panel))) return error;
    void *a=nullptr,*b=nullptr,*c=nullptr;
    if(!check(esp_lcd_dpi_panel_get_frame_buffer(panel,dpi.num_fbs,&a,&b,&c))) return error;
    native[0]=static_cast<uint16_t *>(a); native[1]=static_cast<uint16_t *>(b);
    native[2]=static_cast<uint16_t *>(c);
    if(requested==Pipeline::FullPpa) logical=static_cast<uint16_t *>(heap_caps_aligned_calloc(64,1,FRAME_BYTES,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT));
    if((requested==Pipeline::FullPpa && !logical) || !esp_ptr_external_ram(native[0]) || !esp_ptr_external_ram(native[1]) || (dpi.num_fbs==3 && !esp_ptr_external_ram(native[2]))) {
        check(ESP_ERR_NO_MEM); return error;
    }
    ppa_client_config_t accelerator={}; accelerator.oper_type=PPA_OPERATION_SRM; accelerator.max_pending_trans_num=1;
    if(requested==Pipeline::FullPpa && !check(ppa_register_client(&accelerator,&ppa))) return error;
    back=1; initialized=true; faulted=false; frame_open=false; pipeline=requested; reset_clip();
    history.reset();triple.reset();fence={};completions.store(0);
    backlight(true);
    return ESP_OK;
}
static esp_err_t wait_pending() {
    const int64_t start=esp_timer_get_time();
    while(fence.pending && !fence.complete(refresh_count())) {
        if(xSemaphoreTake(refresh_sem,pdMS_TO_TICKS(100))!=pdTRUE) {faulted=true;return ESP_ERR_TIMEOUT;}
    }
    if(fence.pending) {fence.pending=false;completions.fetch_add(1,std::memory_order_release);}
    last_telemetry.wait_us+=esp_timer_get_time()-start;
    return ESP_OK;
}
bool idle() {return initialized && !frame_open && !faulted && !fence.pending;}
esp_err_t wait_idle() {
    if(!initialized || faulted || frame_open) return ESP_ERR_INVALID_STATE;
    return wait_pending();
}
esp_err_t set_pipeline(Pipeline requested) {
    if(requested==pipeline) return faulted?ESP_ERR_INVALID_STATE:ESP_OK;
    if(wait_idle()!=ESP_OK || (requested==Pipeline::FullPpa && (!logical || !ppa)) || (requested==Pipeline::NativeQueued && !native[2])) return ESP_ERR_INVALID_STATE;
    if(pipeline==Pipeline::NativeQueued && requested!=pipeline) return ESP_ERR_NOT_SUPPORTED; // end/rebegin to return to a double-buffer strategy
    if(requested==Pipeline::NativeQueued) {triple.reset(history.front);back=triple.back;}
    pipeline=requested;
    if(requested!=Pipeline::NativeQueued) history.stale[back].all(); // reconstruct the next safe backbuffer
    return ESP_OK;
}
// All work is preemptible in the UI task. Only the selected safe backbuffer
// is written. Front pixels are copied solely for tiles missed by that buffer.
esp_err_t begin_frame() {
    if(!initialized || faulted) return ESP_ERR_INVALID_STATE;
    if(frame_open) return ESP_OK;
    last_telemetry={}; changed.clear(); flushed.clear();
    const int64_t start=esp_timer_get_time();
    if(pipeline!=Pipeline::FullPpa) {
        const unsigned front=pipeline==Pipeline::NativeQueued?triple.front:history.front;
        DirtyMask repair=pipeline==Pipeline::NativeQueued?triple.stale[back]:history.stale[back];
        if(pipeline==Pipeline::NativeFull) repair.all();
        for(int r=0;r<TILE_ROWS;++r) for(int c=0;c<TILE_COLS;) {
            if(!(repair.rows[r]&(1u<<c))) {++c;continue;}
            const int first=c;
            while(c<TILE_COLS && (repair.rows[r]&(1u<<c))) ++c;
            for(int y=r*TILE;y<(r+1)*TILE;++y) {
                const unsigned offset=y*NATIVE_WIDTH+first*TILE;
                memcpy(native[back]+offset,native[front]+offset,(c-first)*TILE*2);
            }
        }
        flushed.merge(repair); last_telemetry.repair_bytes=repair.bytes();
        if(pipeline==Pipeline::NativeQueued) triple.repaired();else history.repaired();
    }
    last_telemetry.repair_us=esp_timer_get_time()-start; frame_open=true;
    return ESP_OK;
}
esp_err_t present() {
    if(!initialized || faulted) return ESP_ERR_INVALID_STATE;
    if(!frame_open) return ESP_OK;
    const int64_t t0=esp_timer_get_time();
    esp_err_t error=ESP_OK;
    if(pipeline==Pipeline::FullPpa) {
    ppa_srm_oper_config_t rotation={};
    rotation.in.buffer=logical; rotation.in.pic_w=WIDTH; rotation.in.pic_h=HEIGHT;
    rotation.in.block_w=WIDTH; rotation.in.block_h=HEIGHT; rotation.in.srm_cm=PPA_SRM_COLOR_MODE_RGB565;
    rotation.out.buffer=native[back]; rotation.out.buffer_size=FRAME_BYTES;
    rotation.out.pic_w=NATIVE_WIDTH; rotation.out.pic_h=NATIVE_HEIGHT; rotation.out.srm_cm=PPA_SRM_COLOR_MODE_RGB565;
    rotation.rotation_angle=PPA_SRM_ROTATION_ANGLE_270;
    rotation.scale_x=rotation.scale_y=1.0f; rotation.mode=PPA_TRANS_MODE_BLOCKING;
    error=ppa_do_scale_rotate_mirror(ppa,&rotation);
    const int64_t t1=esp_timer_get_time(); last_telemetry.ppa_us=t1-t0;
    if(error!=ESP_OK) {faulted=true;return error;}
    last_telemetry.dirty_bytes=FRAME_BYTES;
    } else {
        flushed.merge(changed);
        const int64_t cache_start=esp_timer_get_time();
        // Coalesce cache address ranges per dirty tile band, including gaps
        // between its rows. Clean cache lines in those gaps cause no image
        // copy; only modified cache lines write back. Report span separately
        // from dirty/repair payload so this cost is not hidden.
        for(int r=0;r<TILE_ROWS;++r) {
            if(!flushed.rows[r]) continue;
            int first=0,last=TILE_COLS-1;
            while(!(flushed.rows[r]&(1u<<first))) ++first;
            while(!(flushed.rows[r]&(1u<<last))) --last;
            const unsigned offset=r*TILE*NATIVE_WIDTH+first*TILE;
            const unsigned bytes=((TILE-1)*NATIVE_WIDTH+(last-first+1)*TILE)*2;
            error=esp_cache_msync(native[back]+offset,bytes,ESP_CACHE_MSYNC_FLAG_DIR_C2M|ESP_CACHE_MSYNC_FLAG_UNALIGNED);
            if(error!=ESP_OK) {faulted=true;return error;}
            last_telemetry.cache_span_bytes+=bytes;
        }
        last_telemetry.cache_us=esp_timer_get_time()-cache_start;
        last_telemetry.dirty_bytes=changed.bytes();
    }
    // With three buffers, this fence retires the previous submission *after*
    // drawing into the already safe spare. CPU work overlaps the old wait.
    if(pipeline==Pipeline::NativeQueued && wait_pending()!=ESP_OK) return ESP_ERR_TIMEOUT;
    const int64_t submit_start=esp_timer_get_time();
    // The pinned IDF native-FB branch selects the entire buffer after flushing
    // the supplied rows. Native paths already flushed every repaired/drawn tile;
    // submit one valid row to select the buffer (an extra 960-byte cache range).
    error=esp_lcd_panel_draw_bitmap(panel,0,0,NATIVE_WIDTH,pipeline==Pipeline::FullPpa?NATIVE_HEIGHT:1,native[back]);
    const int64_t t2=esp_timer_get_time(); last_telemetry.submit_us=t2-submit_start;
    if(error!=ESP_OK) {faulted=true;return error;}
    // Bundled f56bea3d1f has independent DMA completion and bridge VSYNC ISRs,
    // without buffer identity in refresh callbacks. One VSYNC cannot prove
    // an old DMA list is retired. Retain two callbacks; only UI waits.
    fence.submit(refresh_count());
    if(pipeline!=Pipeline::NativeQueued && wait_pending()!=ESP_OK) return ESP_ERR_TIMEOUT;
    if(pipeline==Pipeline::FullPpa) changed.all();
    if(pipeline==Pipeline::NativeQueued) {triple.committed(changed);back=triple.back;}
    else {history.committed(changed);back=history.back;}
    frame_open=false;
    last_telemetry.present_us=esp_timer_get_time()-t0;
    return ESP_OK;
}
void rect(int x,int y,int w,int h,uint16_t color) {
    if(!initialized || faulted || w<=0 || h<=0) return;
    const int x0=std::max(x,clip_x0),y0=std::max(y,clip_y0);
    const int x1=int(std::min<int64_t>(int64_t(x)+w,clip_x1)),y1=int(std::min<int64_t>(int64_t(y)+h,clip_y1));
    if(x1<=x0 || y1<=y0) return;
    if(begin_frame()!=ESP_OK) return;
    if(pipeline==Pipeline::FullPpa) {
        for(int row=y0;row<y1;++row) std::fill(logical+row*WIDTH+x0,logical+row*WIDTH+x1,color);
    } else {
        const int nx0=NATIVE_WIDTH-y1,nx1=NATIVE_WIDTH-y0;
        for(int row=x0;row<x1;++row) std::fill(native[back]+row*NATIVE_WIDTH+nx0,native[back]+row*NATIVE_WIDTH+nx1,color);
        changed.logical_rect(x0,y0,x1-x0,y1-y0);
    }
}
void fill(uint16_t color) { rect(0,0,WIDTH,HEIGHT,color); }
void line(int x0,int y0,int x1,int y1,uint16_t color) {
    const int dx=std::abs(x1-x0),sx=x0<x1?1:-1,dy=-std::abs(y1-y0),sy=y0<y1?1:-1;
    int error=dx+dy;
    for(;;) {
        rect(x0,y0,1,1,color); if(x0==x1 && y0==y1) break;
        const int twice=2*error;
        if(twice>=dy) {error+=dy; x0+=sx;} if(twice<=dx) {error+=dx; y0+=sy;}
    }
}
void circle(int x,int y,int radius,uint16_t color) {
    int a=radius,b=0,error=1-radius;
    while(a>=b) {
        rect(x+a,y+b,1,1,color); rect(x+b,y+a,1,1,color); rect(x-b,y+a,1,1,color); rect(x-a,y+b,1,1,color);
        rect(x-a,y-b,1,1,color); rect(x-b,y-a,1,1,color); rect(x+b,y-a,1,1,color); rect(x+a,y-b,1,1,color);
        ++b; if(error<0) error+=2*b+1; else {--a; error+=2*(b-a)+1;}
    }
}
void text(int x,int y,const char *value,uint16_t color,int scale) {
    // Small original 3x5 diagnostic alphabet; no graphics/UI dependency.
    static const uint16_t glyphs[]={
        0x7b6f,0x2492,0x73e7,0x73cf,0x5bc9,0x79cf,0x79ef,0x7249,0x7bef,0x7bcf,
        0x2bed,0x6bae,0x7927,0x6b6e,0x79e7,0x79e4,0x796f,0x5bed,0x7497,0x124e,
        0x5bad,0x4927,0x5fed,0x5f6d,0x7b6f,0x7be4,0x7b7b,0x7bad,0x79cf,0x7492,
        0x5b6f,0x5b6a,0x5bfd,0x5aad,0x5a92,0x72a7
    };
    for(;*value;++value,x+=4*scale) {
        const int index=*value>='0'&&*value<='9'?*value-'0':*value>='A'&&*value<='Z'?10+*value-'A':-1;
        if(index<0) continue;
        for(int row=0;row<5;++row) for(int col=0;col<3;++col)
            if(glyphs[index]&(1u<<(14-row*3-col))) rect(x+col*scale,y+row*scale,scale,scale,color);
    }
}
}
#endif
