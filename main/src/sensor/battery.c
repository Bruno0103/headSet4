/**
 * @file battery.c
 * @brief Implementação das funções de leitura da bateria do ESP32.
 * 
 * Este arquivo contém o código responsável por ler a tensão no pino ADC (GPIO34),
 * utilizando o controle de energia pelo MOSFET (GPIO14). O valor lido é convertido
 * e multiplicado pelo fator do divisor de tensão para exibir a tensão real da bateria.
 */

#include "battery.h"
#include <stdint.h>
#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"

static const char *TAG = "BATTERY";

#include "headset_events.h"
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

/* Curva de descarga tipica de uma celula Li-ion/LiPo 1S (em repouso), interpolada linearmente */
static uint8_t battery_percent_from_mv(int mv)
{
    static const struct { uint16_t mv; uint8_t pct; } curve[] = {
        {4200, 100}, {4150, 95}, {4110, 90}, {4080, 85}, {4020, 80}, {3980, 75}, {3950, 70},
        {3910, 65},  {3870, 60}, {3850, 55}, {3840, 50}, {3820, 45}, {3800, 40}, {3790, 35},
        {3770, 30},  {3750, 25}, {3730, 20}, {3710, 15}, {3690, 10}, {3610, 5},  {3400, 0},
    };
    const int n = sizeof curve / sizeof curve[0];

    if (mv >= curve[0].mv) return 100;
    if (mv <= curve[n - 1].mv) return 0;
    for (int i = 1; i < n; i++) {
        if (mv >= curve[i].mv) {
            int span_mv  = curve[i - 1].mv - curve[i].mv;
            int span_pct = curve[i - 1].pct - curve[i].pct;
            return (uint8_t)(curve[i].pct + (mv - curve[i].mv) * span_pct / span_mv);
        }
    }
    return 0;
}


/**
 * @brief Função auxiliar para ordenar vetor de inteiros (Bubble Sort simples para N pequeno).
 * 
 * Necessário para calcular a mediana de um conjunto de valores.
 * 
 * @param array Ponteiro para o array a ser ordenado.
 * @param n Quantidade de elementos no array.
 */
static void sort_samples(int *array, int n)
{
    for (int i = 0; i < n - 1; i++) {
        for (int j = 0; j < n - i - 1; j++) {
            if (array[j] > array[j + 1]) {
                int temp = array[j];
                array[j] = array[j + 1];
                array[j + 1] = temp;
            }
        }
    }
}

/**
 * @brief Calcula a mediana de um conjunto de leituras do ADC.
 * 
 * A mediana é muito mais robusta contra ruídos esporádicos, picos de consumo
 * e flutuações pontuais de RF do que a média aritmética.
 * 
 * @param samples Array contendo as amostras brutas.
 * @param count Quantidade de amostras (espera-se 10).
 * @return int Valor mediano calculado.
 */
static int calculate_median(int *samples, int count)
{
    if (count <= 0) return 0;
    
    // Cria uma cópia local para não alterar o array original durante a ordenação
    int sorted[count];
    for (int i = 0; i < count; i++) {
        sorted[i] = samples[i];
    }

    // Ordena as amostras em ordem crescente
    sort_samples(sorted, count);

    // Para número par de elementos (ex: 10), a mediana é a média entre os dois valores centrais
    if (count % 2 == 0) {
        return (sorted[(count / 2) - 1] + sorted[count / 2]) / 2;
    } else {
        return sorted[count / 2];
    }
}

void battery_task(void *pvParameters)
{
    // Inicializar os pinos e ADC
    battery_init();

    int prev_avg = -1;
    int prev_percent = -1;

    #define BATTERY_MEDIAN_SAMPLES 10

    while(1) {
        int samples_raw[BATTERY_MEDIAN_SAMPLES];
        
        // Coleta 10 leituras com intervalo de estabilização para calcular a mediana
        for(int i = 0; i < BATTERY_MEDIAN_SAMPLES; i++) {
            // Ligar o divisor de tensão colocando o GPIO14 em nível ALTO através do MOSFET
            gpio_set_level(BAT_CTRL_PIN, 1);
            
            // Aguardar 10ms para garantir a estabilização completa do circuito RC / divisor de tensão
            vTaskDelay(pdMS_TO_TICKS(10));

            int adc_raw = 0;
            // Efetuar a leitura do valor bruto no ADC1 canal 6
            ESP_ERROR_CHECK(adc_oneshot_read(adc1_handle, BAT_ADC_CHANNEL, &adc_raw));

            // Imediatamente após a medição, desligar o divisor colocando o pino em nível BAIXO para economizar carga
            gpio_set_level(BAT_CTRL_PIN, 0);

            samples_raw[i] = adc_raw;
            vTaskDelay(pdMS_TO_TICKS(10)); // Pequeno atraso entre amostras subsequentes
        }
        
        // Calcula a mediana das 10 medidas brutas coletadas
        int median_raw = calculate_median(samples_raw, BATTERY_MEDIAN_SAMPLES);
        int voltage_mv = 0;
        
        // Converter o valor mediano bruto (raw) para milivolts utilizando a calibração do ESP32
        if (do_calibration) {
            ESP_ERROR_CHECK(adc_cali_raw_to_voltage(adc1_cali_handle, median_raw, &voltage_mv));
        } else {
            // Fallback básico caso a calibração de fábrica não esteja disponível:
            // 4095 é o fundo de escala de 12 bits do ADC, e ~3300mV é o range com atenuação de 12dB
            voltage_mv = (median_raw * 3300) / 4095;
        }

        // Se for a primeira leitura, ou se houver variação na tensão medida
        if (prev_avg == -1 || prev_avg != voltage_mv) {
            // O divisor de tensão resistivo (200K / 100K) atenua a tensão da bateria em 1/3.
            // Portanto, multiplicamos a tensão do pino por 3 para obter a tensão real da bateria.
            int battery_voltage_mv = voltage_mv * 3;
            float battery_voltage_v = battery_voltage_mv / 1000.0f;

            ESP_LOGI(TAG, "=============================================");
            ESP_LOGI(TAG, "[BATERIA] MEDIANA DE 10 LEITURAS CALCULADA");
            ESP_LOGI(TAG, "[BATERIA] Tensão no pino: %d mV | Tensão real da bateria: %.2f V", voltage_mv, battery_voltage_v);
            ESP_LOGI(TAG, "=============================================");
            
            prev_avg = voltage_mv;

            // Converte a tensão real em porcentagem linearizada (0 a 100%)
            uint8_t percent = battery_percent_from_mv(battery_voltage_mv);
            if (percent != prev_percent) {
                prev_percent = percent;
                headset_battery_evt_t ev = { .percent = percent, .millivolts = (uint16_t)battery_voltage_mv };
                ESP_LOGI(TAG, "[BATERIA] Nivel real da bateria: %u%%", percent);
                // Notifica todo o sistema (display, link manager, etc) com o percentual e tensão real
                headset_event_post(HEADSET_EVT_BATTERY, &ev, sizeof ev);
            }
        }

        // Fazer o ciclo de monitoramento a cada 15 segundos
        vTaskDelay(pdMS_TO_TICKS(15000));
    }
}

