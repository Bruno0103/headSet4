#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "display.h"
#include "audio.h"
#include "bluetooth.h"

// #include "sensores.h"
// #include "atuadores.h"

void app_main(void)
{
  // --- Display / LVGL ---
  display_init();
  xTaskCreate(display_task, "display_task", 8192, NULL, 5, NULL);

  // --- Audio (WM8960 + I2S) - deve vir ANTES do Bluetooth ---
  ESP_ERROR_CHECK(audio_init());

  // --- Bluetooth (A2DP + AVRCP + HFP) ---
  bluetooth_init();
  xTaskCreate(bluetooth_task, "bluetooth_task", 4096, NULL, 6, NULL);

  // --- Futuros modulos ---
  // sensores_init();
  // xTaskCreate(sensores_task, "sensores_task", 4096, NULL, 5, NULL);
  //
  // atuadores_init();
  // xTaskCreate(atuadores_task, "atuadores_task", 4096, NULL, 5, NULL);
}
