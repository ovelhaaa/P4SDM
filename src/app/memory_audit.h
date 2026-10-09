#pragma once
#include <Arduino.h>
#include "esp_chip_info.h"
#include "esp_psram.h"
#include "esp_clk_tree.h"
#include "soc/hp_sys_clkrst_struct.h"
#include "soc/spi_mem_s_struct.h"

// Read-only P4 ES snapshot. Field meanings cross-checked against the installed
// IDF 5.5 psram_ctrlr_ll.h. Never writes timing/calibration/clock registers.
inline void memory_audit() {
  esp_chip_info_t chip{};
  esp_chip_info(&chip);
  hp_sys_clkrst_peri_clk_ctrl00_reg_t clock{};
  clock.val = HP_SYS_CLKRST.peri_clk_ctrl00.val;
  const unsigned select = clock.reg_psram_clk_src_sel;
  const soc_module_clk_t sources[] = {SOC_MOD_CLK_XTAL, SOC_MOD_CLK_MPLL,
                                     SOC_MOD_CLK_SPLL, SOC_MOD_CLK_CPLL};
  uint32_t hz = 0;
  const auto result = esp_clk_tree_src_get_freq_hz(sources[select],
      ESP_CLK_TREE_SRC_FREQ_PRECISION_CACHED, &hz);
  const unsigned core_div = clock.reg_psram_core_clk_div_num+1;
  const unsigned bus_div = SPIMEM2.mem_sram_clk.mem_sclk_equ_sysclk ? 1 :
                           SPIMEM2.mem_sram_clk.mem_sclkcnt_n+1;
  Serial.printf("[M212 memory] revision=%u cpu_mhz=%u psram_bytes=%u sdk_speed_mhz=%u l2_bytes=%u line_bytes=%u\n",
      chip.revision, getCpuFrequencyMhz(), unsigned(esp_psram_get_size()),
      CONFIG_SPIRAM_SPEED, CONFIG_CACHE_L2_CACHE_SIZE, CONFIG_CACHE_L2_CACHE_LINE_SIZE);
  delay(20);
  Serial.printf("[M212 registers] source=%u source_hz=%lu source_status=%d core_div=%u bus_div=%u effective_hz=%lu hex_read=%u hex_write=%u ddr=%u ecc=%u\n",
      select, (unsigned long)hz, int(result), core_div, bus_div,
      (unsigned long)(result == ESP_OK ? hz/core_div/bus_div : 0),
      SPIMEM2.mem_sram_cmd.mem_sdin_hex, SPIMEM2.mem_sram_cmd.mem_sdout_hex,
      SPIMEM2.smem_ddr.smem_ddr_en, SPIMEM2.smem_ac.smem_ecc_16to18_byte_en);
  // Effective clock is derived from live divider registers and SDK-reported
  // source frequency; electrical frequency is not independently measured.
}
