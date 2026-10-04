/**
 * @file battery.c
 * @brief Implementação das funções de leitura da bateria do ESP32.
 * 
 * Este arquivo contém o código responsável por ler a tensão no pino ADC (GPIO34),
 * utilizando o controle de energia pelo MOSFET (GPIO14). O valor lido é convertido
 * e multiplicado pelo fator do divisor de tensão para exibir a tensão real da bateria.
 */

#include "battery.h"
#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"

static const char *TAG = "BATTERY";

#include "pinout.h"

// O GPIO34 corresponde ao Canal 6 do ADC1 no ESP32
#define BAT_ADC_UNIT        ADC_UNIT_1
#define BAT_ADC_CHANNEL     ADC_CHANNEL_6

static adc_oneshot_unit_handle_t adc1_handle;
static adc_cali_handle_t adc1_cali_handle = NULL;
static bool do_calibration = false;

void battery_init(void)
{
    ESP_LOGI(TAG, "Inicializando medidor de bateria...");

    // 1. Configurar o pino de controle (GPIO14) como saída
    // Isso é feito para controlar o MOSFET e ligar/desligar o divisor de tensão
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << BAT_CTRL_PIN),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&io_conf);
    
    // Iniciar em LOW (0) para manter o divisor desligado e economizar bateria
    gpio_set_level(BAT_CTRL_PIN, 0);

    // 2. Configurar o driver do ADC Oneshot para a unidade ADC1
    adc_oneshot_unit_init_cfg_t init_config1 = {
        .unit_id = BAT_ADC_UNIT,
    };
    ESP_ERROR_CHECK(adc_oneshot_new_unit(&init_config1, &adc1_handle));

    // Configurar o canal 6 do ADC. 
    // Usamos atenuação de 12dB (antigo 11dB) para ler tensões até ~3.3V
    adc_oneshot_chan_cfg_t config = {
        .bitwidth = ADC_BITWIDTH_DEFAULT,
        .atten = ADC_ATTEN_DB_12, 
    };
    ESP_ERROR_CHECK(adc_oneshot_config_channel(adc1_handle, BAT_ADC_CHANNEL, &config));

    // 3. Configurar a Calibração do ADC (Garante maior precisão na conversão raw -> Volts)
    adc_cali_line_fitting_config_t cali_config = {
        .unit_id = BAT_ADC_UNIT,
        .atten = ADC_ATTEN_DB_12,
        .bitwidth = ADC_BITWIDTH_DEFAULT,
    };
    esp_err_t ret = adc_cali_create_scheme_line_fitting(&cali_config, &adc1_cali_handle);
    if (ret == ESP_OK) {
        do_calibration = true;
        ESP_LOGI(TAG, "Calibração do ADC inicializada com sucesso.");
    } else {
        ESP_LOGW(TAG, "Falha ao inicializar a calibração: %s (utilizando fallback matemático)", esp_err_to_name(ret));
    }
}

void battery_read_and_print(void)
{
    // Ligar o divisor de tensão colocando o GPIO14 em nível ALTO
    gpio_set_level(BAT_CTRL_PIN, 1);
    
    // Aguardar 10ms para garantir a estabilização da tensão antes de medir
    vTaskDelay(pdMS_TO_TICKS(10));

    int adc_raw = 0;
    // Efetuar a leitura do valor bruto no ADC
    ESP_ERROR_CHECK(adc_oneshot_read(adc1_handle, BAT_ADC_CHANNEL, &adc_raw));

    // Imediatamente após a leitura, desligar o divisor colocando o GPIO14 em BAIXO
    gpio_set_level(BAT_CTRL_PIN, 0);

    int voltage_mv = 0;
    // Converter o valor bruto (raw) para milivolts
    if (do_calibration) {
        ESP_ERROR_CHECK(adc_cali_raw_to_voltage(adc1_cali_handle, adc_raw, &voltage_mv));
    } else {
        // Fallback básico caso a calibração do hardware falhe 
        // 4095 é o max de 12bits, e 3300mV é aprox o fundo de escala com atenuação de 12dB
        voltage_mv = (adc_raw * 3300) / 4095;
    }

    // O divisor (200K / 100K) fornece 1/3 da tensão total da bateria no pino
    // Multiplicamos por 3 para obter o valor real (3.7V a 4.2V típico)
    int battery_voltage_mv = voltage_mv * 3;
    float battery_voltage_v = battery_voltage_mv / 1000.0f;

    // Exibe no terminal a tensão
    ESP_LOGI(TAG, "[BATERIA] Tensão lida no pino: %d mV | Tensão real: %.2f V", voltage_mv, battery_voltage_v);
}

void battery_task(void *pvParameters)
{
    // Inicializar os pinos e ADC
    battery_init();

    while(1) {
        // Realizar leitura e imprimir
        battery_read_and_print();
        
        // Fazer a leitura a cada 5 segundos para não poluir muito o terminal
        vTaskDelay(pdMS_TO_TICKS(5000));
    }
}
