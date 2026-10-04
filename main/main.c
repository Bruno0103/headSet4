#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
// #include "display.h"
#include "audio.h"
#include "bluetooth.h"
#include "bt_multipoint.h" // Inclui o gerenciador Multipoint

// #include "sensores.h"
// #include "atuadores.h"
#include "apds9930.h"
#include "battery.h"


void app_main(void) {
  // --- Display / LVGL ---
  // display_init();
  // xTaskCreate(display_task, "display_task", 8192, NULL, 5, NULL);

  // --- Audio (WM8960 + I2S) - deve vir ANTES do Bluetooth ---
  ESP_ERROR_CHECK(audio_init());

  // --- Gerenciador Multipoint Inteligente ---
  bt_multipoint_init();

  // --- Bluetooth (A2DP + AVRCP + HFP) ---
  if (bluetooth_init() != ESP_OK) {
      printf("Bluetooth init failed\n");
  }

  // --- Bateria ---
  xTaskCreate(battery_task, "battery_task", 4096, NULL, 5, NULL);

  // --- Sensor APDS-9930 ---
  apds9930_init();
  xTaskCreate(apds9930_task, "apds9930_task", 2048, NULL, 5, NULL);
  //
  // atuadores_init();
  // xTaskCreate(atuadores_task, "atuadores_task", 4096, NULL, 5, NULL);
}
