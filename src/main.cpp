#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "driver/i2s_std.h"
#include "esp_err.h"
#include "esp_heap_caps.h"
#include "esp_littlefs.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/stream_buffer.h"
#include "freertos/task.h"
#include "minimp3.h"

static const char* TAG = "MP3_PLAYER";

#define I2S_BCLK_GPIO GPIO_NUM_12
#define I2S_LRCLK_GPIO GPIO_NUM_13
#define I2S_DOUT_GPIO GPIO_NUM_11

#define MP3_FILE "/littlefs/004.mp3"
#define MP3_IN_BUF_SIZE (32 * 1024)
#define MP3_TASK_STACK_SIZE 32768
#define I2S_TASK_STACK_SIZE 4096

#define PCM_RING_BYTES (24 * 1024)
#define PCM_PREFILL_BYTES (12 * 1024)
#define PCM_CHUNK_BYTES 2048

// 0..100. Small speakers usually need 15–30.
#define VOLUME_PERCENT 50

static i2s_chan_handle_t tx_handle = nullptr;
static int s_i2s_hz = 0;
static volatile bool s_i2s_configured = false;

static StaticStreamBuffer_t s_pcm_sb_struct;
static uint8_t s_pcm_storage[PCM_RING_BYTES];
static StreamBufferHandle_t s_pcm_sb = nullptr;

static void littlefs_init(void) {
  ESP_LOGI(TAG, "Mounting LittleFS...");

  esp_vfs_littlefs_conf_t conf = {
      .base_path = "/littlefs",
      .partition_label = "littlefs",
      .partition = nullptr,
      .blockdev = nullptr,
      .format_if_mount_failed = true,
      .read_only = false,
      .dont_mount = false,
      .grow_on_mount = false,
  };

  ESP_ERROR_CHECK(esp_vfs_littlefs_register(&conf));

  size_t total = 0;
  size_t used = 0;
  ESP_ERROR_CHECK(esp_littlefs_info("littlefs", &total, &used));
  ESP_LOGI(TAG, "LittleFS: total=%u, used=%u", (unsigned)total, (unsigned)used);
}

static void littlefs_list(void) {
  DIR* dir = opendir("/littlefs");
  if (dir == nullptr) {
    ESP_LOGE(TAG, "Cannot open /littlefs");
    return;
  }

  ESP_LOGI(TAG, "Files in /littlefs:");
  while (dirent* entry = readdir(dir)) {
    ESP_LOGI(TAG, "  %s", entry->d_name);
  }
  closedir(dir);
}

static void i2s_create_channel(void) {
  i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
  chan_cfg.dma_desc_num = 8;
  chan_cfg.dma_frame_num = 256;
  chan_cfg.auto_clear_after_cb = true;
  ESP_ERROR_CHECK(i2s_new_channel(&chan_cfg, &tx_handle, nullptr));
}

static void i2s_configure_sample_rate(int hz) {
  if (hz <= 0 || hz == s_i2s_hz) {
    return;
  }

  i2s_std_clk_config_t clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG((uint32_t)hz);

  if (s_i2s_hz == 0) {
    i2s_std_slot_config_t slot_cfg =
        I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO);

    i2s_std_config_t std_cfg = {
        .clk_cfg = clk_cfg,
        .slot_cfg = slot_cfg,
        .gpio_cfg =
            {
                .mclk = I2S_GPIO_UNUSED,
                .bclk = I2S_BCLK_GPIO,
                .ws = I2S_LRCLK_GPIO,
                .dout = I2S_DOUT_GPIO,
                .din = I2S_GPIO_UNUSED,
                .invert_flags =
                    {
                        .mclk_inv = false,
                        .bclk_inv = false,
                        .ws_inv = false,
                    },
            },
    };

    ESP_ERROR_CHECK(i2s_channel_init_std_mode(tx_handle, &std_cfg));
  } else {
    ESP_ERROR_CHECK(i2s_channel_disable(tx_handle));
    ESP_ERROR_CHECK(i2s_channel_reconfig_std_clock(tx_handle, &clk_cfg));
    ESP_ERROR_CHECK(i2s_channel_enable(tx_handle));
  }

  s_i2s_hz = hz;
  s_i2s_configured = true;
  ESP_LOGI(TAG, "I2S sample rate: %d Hz", hz);
}

static void skip_id3v2(FILE* file) {
  uint8_t hdr[10];
  if (fread(hdr, 1, sizeof(hdr), file) != sizeof(hdr)) {
    fseek(file, 0, SEEK_SET);
    return;
  }

  if (memcmp(hdr, "ID3", 3) != 0) {
    fseek(file, 0, SEEK_SET);
    return;
  }

  uint32_t size = ((uint32_t)(hdr[6] & 0x7f) << 21) | ((uint32_t)(hdr[7] & 0x7f) << 14) |
                  ((uint32_t)(hdr[8] & 0x7f) << 7) | (uint32_t)(hdr[9] & 0x7f);

  fseek(file, (long)(10 + size), SEEK_SET);
  ESP_LOGI(TAG, "Skipped ID3v2 tag: %u bytes", (unsigned)(10 + size));
}

static void refill_mp3_buf(FILE* file, uint8_t* buf, size_t buf_size, size_t* level, size_t* off) {
  if (*off > 0) {
    memmove(buf, buf + *off, *level - *off);
    *level -= *off;
    *off = 0;
  }

  if (*level < buf_size) {
    *level += fread(buf + *level, 1, buf_size - *level, file);
  }
}

static void apply_volume(int16_t* samples, int count) {
  for (int i = 0; i < count; ++i) {
    int32_t sample = ((int32_t)samples[i] * VOLUME_PERCENT) / 100;
    if (sample > 32767) {
      sample = 32767;
    } else if (sample < -32768) {
      sample = -32768;
    }
    samples[i] = (int16_t)sample;
  }
}

static void push_pcm_stereo(const int16_t* pcm, int samples, int channels, int16_t* stereo) {
  int out_count = 0;

  if (channels == 1) {
    for (int i = 0; i < samples; ++i) {
      stereo[i * 2] = pcm[i];
      stereo[i * 2 + 1] = pcm[i];
    }
    out_count = samples * 2;
  } else {
    memcpy(stereo, pcm, (size_t)samples * (size_t)channels * sizeof(int16_t));
    out_count = samples * channels;
  }

  apply_volume(stereo, out_count);

  size_t bytes = (size_t)out_count * sizeof(int16_t);
  size_t sent = xStreamBufferSend(s_pcm_sb, stereo, bytes, portMAX_DELAY);
  if (sent != bytes) {
    ESP_LOGW(TAG, "PCM ring short write: %u/%u", (unsigned)sent, (unsigned)bytes);
  }
}

static void play_mp3_file(const char* path) {
  FILE* file = fopen(path, "rb");
  if (file == nullptr) {
    ESP_LOGE(TAG, "Cannot open %s", path);
    littlefs_list();
    return;
  }

  fseek(file, 0, SEEK_END);
  long file_size = ftell(file);
  fseek(file, 0, SEEK_SET);
  ESP_LOGI(TAG, "Playing %s (%ld bytes)", path, file_size);

  mp3dec_t* dec =
      (mp3dec_t*)heap_caps_malloc(sizeof(mp3dec_t), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  uint8_t* in_buf =
      (uint8_t*)heap_caps_malloc(MP3_IN_BUF_SIZE, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);

  if (dec == nullptr || in_buf == nullptr) {
    ESP_LOGE(TAG, "Out of memory");
    free(dec);
    free(in_buf);
    fclose(file);
    return;
  }

  int16_t pcm[MINIMP3_MAX_SAMPLES_PER_FRAME];
  int16_t stereo[MINIMP3_MAX_SAMPLES_PER_FRAME];
  bool logged_format = false;

  while (true) {
    mp3dec_init(dec);
    skip_id3v2(file);

    size_t level = 0;
    size_t off = 0;
    refill_mp3_buf(file, in_buf, MP3_IN_BUF_SIZE, &level, &off);

    if (level == 0) {
      ESP_LOGE(TAG, "File is empty");
      break;
    }

    while (true) {
      const size_t pcm_available = xStreamBufferBytesAvailable(s_pcm_sb);
      if ((level - off < MP3_IN_BUF_SIZE / 2 && pcm_available > PCM_PREFILL_BYTES / 2) ||
          (level - off < 2048)) {
        refill_mp3_buf(file, in_buf, MP3_IN_BUF_SIZE, &level, &off);
      }

      const size_t available = level - off;
      if (available == 0) {
        break;
      }

      mp3dec_frame_info_t info = {};
      int samples = mp3dec_decode_frame(dec, in_buf + off, (int)available, pcm, &info);

      if (info.frame_bytes > 0) {
        off += (size_t)info.frame_bytes;
      } else {
        off += 1;
        continue;
      }

      if (samples <= 0) {
        continue;
      }

      if (!logged_format) {
        ESP_LOGI(TAG, "MP3: %d Hz, %d ch, %d kbps, layer %d", info.hz, info.channels,
                 info.bitrate_kbps, info.layer);
        logged_format = true;
      }

      i2s_configure_sample_rate(info.hz);
      push_pcm_stereo(pcm, samples, info.channels, stereo);
    }

    ESP_LOGI(TAG, "Track finished, restarting");
    fseek(file, 0, SEEK_SET);
  }

  free(dec);
  free(in_buf);
  fclose(file);
}

static void i2s_write_task(void* /*arg*/) {
  int16_t* chunk =
      (int16_t*)heap_caps_aligned_alloc(4, PCM_CHUNK_BYTES, MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA);
  if (chunk == nullptr) {
    ESP_LOGE(TAG, "No DMA memory for I2S chunk");
    vTaskDelete(nullptr);
    return;
  }

  while (!s_i2s_configured || xStreamBufferBytesAvailable(s_pcm_sb) < PCM_PREFILL_BYTES) {
    vTaskDelay(pdMS_TO_TICKS(10));
  }

  ESP_ERROR_CHECK(i2s_channel_enable(tx_handle));
  ESP_LOGI(TAG, "I2S started, PCM prefilled");

  while (true) {
    size_t got = xStreamBufferReceive(s_pcm_sb, chunk, PCM_CHUNK_BYTES, pdMS_TO_TICKS(50));
    if (got == 0) {
      ESP_LOGW(TAG, "PCM underrun");
      continue;
    }

    size_t written = 0;
    esp_err_t err = i2s_channel_write(tx_handle, chunk, got, &written, pdMS_TO_TICKS(200));
    if (err != ESP_OK) {
      ESP_LOGE(TAG, "I2S write error: %s", esp_err_to_name(err));
    }
  }
}

static void mp3_play_task(void* /*arg*/) {
  play_mp3_file(MP3_FILE);
  vTaskDelete(nullptr);
}

extern "C" void app_main(void) {
  ESP_LOGI(TAG, "==========================");
  ESP_LOGI(TAG, "ESP32 MP3 PLAYER");
  ESP_LOGI(TAG, "Volume: %d%%", VOLUME_PERCENT);
  ESP_LOGI(TAG, "==========================");

  littlefs_init();
  littlefs_list();

  s_pcm_sb = xStreamBufferCreateStatic(PCM_RING_BYTES, 256, s_pcm_storage, &s_pcm_sb_struct);
  if (s_pcm_sb == nullptr) {
    ESP_LOGE(TAG, "Failed to create PCM ring");
    return;
  }

  i2s_create_channel();

  BaseType_t ok =
      xTaskCreatePinnedToCore(i2s_write_task, "i2s", I2S_TASK_STACK_SIZE, nullptr, 6, nullptr, 0);
  if (ok != pdPASS) {
    ESP_LOGE(TAG, "Failed to create I2S task");
    return;
  }

  ok = xTaskCreatePinnedToCore(mp3_play_task, "mp3", MP3_TASK_STACK_SIZE, nullptr, 5, nullptr, 1);
  if (ok != pdPASS) {
    ESP_LOGE(TAG, "Failed to create MP3 task");
  }
}
