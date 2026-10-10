#include "usb_midi_host.h"
#if P4SDM_USB_MIDI
#include "midi_parser.h"
#include "usb_descriptors.h"
#include <Arduino.h>
#include "esp_timer.h"
#include "usb/usb_host.h"
namespace midi {
Queue<128> input;
std::atomic<uint32_t> generation{1};
Diagnostics diagnostics;
namespace {
usb_host_client_handle_t client=nullptr;
usb_device_handle_t device=nullptr;
usb_transfer_t *receive=nullptr;
Parser parser;
uint8_t address=0, endpoint=0, interface_number=0;
bool open_pending=false, closing=false, inflight=false, claimed=false;
std::atomic<uint32_t> errors{0};
uint32_t connects=0, disconnects=0, rejected=0;
void fail() { ++errors; if(!closing) { closing=true; ++generation; } }
void submit() {
  receive->actual_num_bytes=0;
  if(usb_host_transfer_submit(receive)==ESP_OK) inflight=true;
  else fail();
}
void received(usb_transfer_t *t) {
  inflight=false;
  if(closing) return;
  if(t->status!=USB_TRANSFER_STATUS_COMPLETED) { fail(); return; }
  const auto now=uint64_t(esp_timer_get_time());
  parser.transfer(t->data_buffer,t->actual_num_bytes,generation.load(),now,[](Event e) {
    e.parsed_us=esp_timer_get_time(); e.queued_us=esp_timer_get_time();
    if(!input.push(e)) ++generation; // invalidate queued gates after loss
  });
  submit();
}
void client_event(const usb_host_client_event_msg_t *e,void *) {
  if(e->event==USB_HOST_CLIENT_EVENT_NEW_DEV) {
    if(device || open_pending || closing) { ++rejected; return; }
    address=e->new_dev.address; open_pending=true;
  } else if(e->event==USB_HOST_CLIENT_EVENT_DEV_GONE && e->dev_gone.dev_hdl==device) {
    ++disconnects; closing=true; ++generation;
  }
}
void open_device() {
  open_pending=false;
  if(usb_host_device_open(client,address,&device)!=ESP_OK) { device=nullptr; ++errors; return; }
  ++generation;
  const usb_device_desc_t *dd=nullptr;
  const usb_config_desc_t *cfg=nullptr;
  if(usb_host_get_device_descriptor(device,&dd)!=ESP_OK || usb_host_get_active_config_descriptor(device,&cfg)!=ESP_OK) { fail(); return; }
  Serial.printf("[M23 MIDI device] vid=%04x pid=%04x bcd=%04x address=%u\n",dd->idVendor,dd->idProduct,dd->bcdUSB,address);
  const uint8_t *p=reinterpret_cast<const uint8_t *>(cfg);
  Endpoint selected;
  if(!legacy_endpoint(p,cfg->wTotalLength,selected)) { ++rejected; Serial.println("[M23 MIDI] unsupported: no MIDI 1.0 bulk-IN (UMP not supported)"); closing=true; ++generation; return; }
  interface_number=selected.interface_number;endpoint=selected.address;
  const auto alt=selected.alt;const unsigned max_packet=selected.packet;
  if(usb_host_interface_claim(client,device,interface_number,alt)!=ESP_OK) { fail(); return; }
  claimed=true;
  if(usb_host_transfer_alloc(max_packet,0,&receive)!=ESP_OK) { fail(); return; }
  receive->device_handle=device; receive->bEndpointAddress=endpoint;
  receive->num_bytes=max_packet; receive->callback=received; receive->context=nullptr;
  ++connects;
  Serial.printf("[M23 MIDI IN] interface=%u alt=%u endpoint=%02x max_packet=%u generation=%u\n",interface_number,alt,endpoint,max_packet,unsigned(generation.load()));
  submit();
}
void teardown() {
  // Halt/flush schedules completion callbacks. Never free an in-flight buffer.
  if(inflight) {
    if(usb_host_endpoint_halt(device,endpoint)!=ESP_OK) ++errors;
    if(usb_host_endpoint_flush(device,endpoint)!=ESP_OK) ++errors;
    return;
  }
  if(receive) { if(usb_host_transfer_free(receive)!=ESP_OK) { ++errors; return; } receive=nullptr; }
  if(claimed) { if(usb_host_interface_release(client,device,interface_number)!=ESP_OK) { ++errors; return; } claimed=false; }
  if(device) { if(usb_host_device_close(client,device)!=ESP_OK) { ++errors; return; } device=nullptr; }
  closing=false; endpoint=0;
}
void library_task(void *) {
  for(;;) { uint32_t flags=0; auto result=usb_host_lib_handle_events(pdMS_TO_TICKS(20),&flags);
    if(result!=ESP_OK && result!=ESP_ERR_TIMEOUT) ++errors;
    if(flags&USB_HOST_LIB_EVENT_FLAGS_NO_CLIENTS) usb_host_device_free_all();
  }
}
void client_task(void *) {
  uint32_t report=0;
  for(;;) {
    auto result=usb_host_client_handle_events(client,pdMS_TO_TICKS(10));
    if(result!=ESP_OK && result!=ESP_ERR_TIMEOUT) fail();
    if(closing) teardown(); else if(open_pending) open_device();
    if(millis()-report>10000) {
      report=millis();
      Serial.printf("[M23 MIDI host] connected=%u connects=%u disconnects=%u errors=%u rejected=%u overflow=%u malformed=%u unsupported=%u partial=%u\n",unsigned(device && !closing),connects,disconnects,errors.load(),rejected,input.overflows.load(),parser.malformed,parser.unsupported,parser.partial);
      Serial.printf("[M23 MIDI timing] accepted=%u rx_parse_max=%u parse_queue_max=%u queue_block_max=%u bpm_milli=%u clock_jitter_us=%u missing=%u stale=%u\n",diagnostics.accepted.load(),diagnostics.rx_parse_max.load(),diagnostics.parse_queue_max.load(),diagnostics.queue_block_max.load(),diagnostics.bpm_milli.load(),diagnostics.clock_jitter.load(),diagnostics.missing.load(),diagnostics.stale.load());
    }
    vTaskDelay(1);
  }
}
} // namespace
bool start_host() {
  usb_host_config_t config={}; config.intr_flags=ESP_INTR_FLAG_LEVEL1;
  config.peripheral_map=1; // IDF 5.5 P4 bit0 = HS; bit1 = FS Serial/JTAG PHY.
  auto result=usb_host_install(&config);
  if(result!=ESP_OK) { Serial.printf("[M23 MIDI] install failed=%d\n",result); return false; }
  usb_host_client_config_t cc={}; cc.max_num_event_msg=8;
  cc.async.client_event_callback=client_event;
  if(usb_host_client_register(&cc,&client)!=ESP_OK) { usb_host_uninstall(); return false; }
  TaskHandle_t daemon=nullptr;
  if(xTaskCreatePinnedToCore(library_task,"midi_library",4096,nullptr,2,&daemon,1)!=pdPASS) {
    usb_host_client_deregister(client); usb_host_uninstall(); return false;
  }
  if(xTaskCreatePinnedToCore(client_task,"midi_client",6144,nullptr,2,nullptr,1)!=pdPASS) {
    vTaskDelete(daemon); usb_host_client_deregister(client); usb_host_uninstall(); return false;
  }
  return true;
}
} // namespace midi
#endif
