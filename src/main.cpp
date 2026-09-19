#include <stdio.h>

#include "esp_flash.h"
#include "esp_psram.h"

extern "C" void app_main() {
  uint32_t flash_size = 0;

  esp_flash_get_size(nullptr, &flash_size);

  printf("\n");
  printf("========== MEMORY ==========\n");
  printf("Flash : %lu MB\n", flash_size / (1024 * 1024));

  printf("PSRAM : %u bytes (%.2f MB)\n", esp_psram_get_size(),
         (float)esp_psram_get_size() / (1024 * 1024));

  printf("============================\n");
}
