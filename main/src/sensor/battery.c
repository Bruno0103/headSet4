/**
 * @file battery.c
 * @brief Implementação resiliente do monitoramento de bateria do ESP32 (Agente A5 - Sensores).
 * 
 * Este módulo realiza a leitura periódica analógica do nível de tensão da bateria (Li-ion/LiPo 1S)
 * conectada ao divisor resistivo (200k / 100k) no pino GPIO34 (ADC1_CHANNEL_6).
 * 
 * Diretrizes arquiteturais cumpridas (WP 0.2 / AGENTS.md):
 * 1. Sem `ESP_ERROR_CHECK`: Erros de inicialização ou leitura do ADC não causam panic nem travam o sistema.
 * 2. Retentativa com Backoff Exponencial: Se uma leitura do ADC ou calibração falhar, o sistema aplica
 *    tentativas com backoff (ex: 50ms, 100ms, 200ms, 400ms...) e registra logs claros de diagnóstico.
 * 3. Inicialização desacoplada: `battery_init()` é executada antes da criação da tarefa FreeRTOS,
 *    garantindo que os periféricos estejam configurados antes do scheduler iniciar o loop contínuo.
 * 4. Remoção de dead code: Funções não utilizadas (como `battery_read_and_print`) foram removidas.
 * 5. Filtro de Mediana: 10 amostras brutas são coletadas para filtrar ruídos eletromagnéticos e picos de RF.
 */

#include "battery.h"
#include <stdint.h>
#include <stdio.h>
#include <stdbool.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"

#include "headset_events.h"
#include "hs_events.h"
#include "pinout.h"

static const char *TAG = "BATTERY";

/* O GPIO34 corresponde ao Canal 6 do ADC1 no ESP32 */
#define BAT_ADC_UNIT        ADC_UNIT_1
#define BAT_ADC_CHANNEL     ADC_CHANNEL_6

/* Quantidade de amostras para o filtro de mediana */
#define BATTERY_MEDIAN_SAMPLES 10

/* Handle global da unidade ADC1 Oneshot */
static adc_oneshot_unit_handle_t s_adc1_handle = NULL;

/* Handle da calibração de fábrica por line fitting */
static adc_cali_handle_t s_adc1_cali_handle = NULL;

/* Flag indicadora se o esquema de calibração foi instanciado com sucesso */
static bool s_calibration_enabled = false;

/* Flag indicadora se a inicialização de hardware foi bem-sucedida */
static bool s_battery_initialized = false;

/**
 * @brief Inicializa o pino MOSFET e a unidade ADC1 para monitoramento de bateria.
 * 
 * Configura o GPIO de chaveamento do divisor resistivo e prepara o driver ADC1 Oneshot
 * e calibração por hardware sem usar ESP_ERROR_CHECK, reportando quaisquer erros.
 * 
 * @return esp_err_t ESP_OK em caso de sucesso, ou código de falha da inicialização.
 */
esp_err_t battery_init(void)
{
    ESP_LOGI(TAG, "Inicializando subsistema de monitoramento de bateria...");

    /* 1. Configurar o pino de controle (BAT_CTRL_PIN / GPIO14) como saída para o MOSFET */
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << BAT_CTRL_PIN),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    esp_err_t err = gpio_config(&io_conf);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Falha ao configurar GPIO de controle do MOSFET (pino %d): %s",
                 BAT_CTRL_PIN, esp_err_to_name(err));
        return err;
    }
    
    /* Manter o divisor desligado em LOW para poupar energia em repouso */
    gpio_set_level(BAT_CTRL_PIN, 0);

    /* 2. Inicializar o driver do ADC Oneshot para ADC1 */
    adc_oneshot_unit_init_cfg_t init_config1 = {
        .unit_id = BAT_ADC_UNIT,
    };
    err = adc_oneshot_new_unit(&init_config1, &s_adc1_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Falha ao inicializar unidade ADC1 Oneshot: %s", esp_err_to_name(err));
        s_adc1_handle = NULL;
        return err;
    }

    /* 3. Configurar canal do ADC com atenuação de 12dB (escala até ~3.3V) */
    adc_oneshot_chan_cfg_t chan_config = {
        .bitwidth = ADC_BITWIDTH_DEFAULT,
        .atten = ADC_ATTEN_DB_12, 
    };
    err = adc_oneshot_config_channel(s_adc1_handle, BAT_ADC_CHANNEL, &chan_config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Falha ao configurar canal %d no ADC1: %s", BAT_ADC_CHANNEL, esp_err_to_name(err));
        adc_oneshot_del_unit(s_adc1_handle);
        s_adc1_handle = NULL;
        return err;
    }

    /* 4. Configurar Calibração de Fábrica do ADC (Line Fitting) */
    adc_cali_line_fitting_config_t cali_config = {
        .unit_id = BAT_ADC_UNIT,
        .atten = ADC_ATTEN_DB_12,
        .bitwidth = ADC_BITWIDTH_DEFAULT,
    };
    err = adc_cali_create_scheme_line_fitting(&cali_config, &s_adc1_cali_handle);
    if (err == ESP_OK) {
        s_calibration_enabled = true;
        ESP_LOGI(TAG, "Calibração de linha do ADC ativada com sucesso.");
    } else {
        s_calibration_enabled = false;
        s_adc1_cali_handle = NULL;
        ESP_LOGW(TAG, "Calibração de linha do ADC não disponível (%s); utilizando aproximação padrão.",
                 esp_err_to_name(err));
    }

    s_battery_initialized = true;
    ESP_LOGI(TAG, "Subsistema de bateria inicializado com sucesso.");
    return ESP_OK;
}

/**
 * @brief Tabela de linearização da curva de descarga típica para célula Li-ion/LiPo 1S em repouso.
 * 
 * Converte a tensão em repouso (milivolts) em porcentagem de capacidade restante (0 a 100%).
 * 
 * @param mv Tensão medida da célula em milivolts.
 * @return uint8_t Porcentagem de carga estimada (0..100%).
 */
static uint8_t battery_percent_from_mv(int mv)
{
    static const struct { uint16_t mv; uint8_t pct; } curve[] = {
        {4200, 100}, {4150, 95}, {4110, 90}, {4080, 85}, {4020, 80}, {3980, 75}, {3950, 70},
        {3910, 65},  {3870, 60}, {3850, 55}, {3840, 50}, {3820, 45}, {3800, 40}, {3790, 35},
        {3770, 30},  {3750, 25}, {3730, 20}, {3710, 15}, {3690, 10}, {3610, 5},  {3400, 0},
    };
    const int n = sizeof(curve) / sizeof(curve[0]);

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
 * @brief Ordena array de inteiros (Bubble sort para N pequeno).
 * 
 * @param array Ponteiro para o array.
 * @param n Número de itens no array.
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
 * @brief Calcula a mediana das amostras do ADC para imunidade contra ruídos de RF e picos de chaveamento.
 * 
 * @param samples Array de amostras brutas.
 * @param count Quantidade de amostras.
 * @return int Valor mediano calculado.
 */
static int calculate_median(int *samples, int count)
{
    if (count <= 0) return 0;
    
    int sorted[BATTERY_MEDIAN_SAMPLES];
    int limit = (count > BATTERY_MEDIAN_SAMPLES) ? BATTERY_MEDIAN_SAMPLES : count;
    for (int i = 0; i < limit; i++) {
        sorted[i] = samples[i];
    }

    sort_samples(sorted, limit);

    if (limit % 2 == 0) {
        return (sorted[(limit / 2) - 1] + sorted[limit / 2]) / 2;
    } else {
        return sorted[limit / 2];
    }
}

/**
 * @brief Realiza a leitura de uma única amostra com retry e backoff exponencial.
 * 
 * Chaveia o divisor através do MOSFET, aguarda 10ms de estabilização do circuito RC,
 * lê o ADC com suporte a até 4 tentativas com espera progressiva em caso de falha do driver,
 * e desliga imediatamente o divisor para economia de energia.
 * 
 * @param[out] out_raw Ponteiro onde o valor bruto lido será armazenado.
 * @return esp_err_t ESP_OK em caso de sucesso, ou último código de erro se todas as tentativas falharem.
 */
static esp_err_t read_single_sample_with_retry(int *out_raw)
{
    const int max_retries = 4;
    uint32_t backoff_ms = 20;
    esp_err_t last_err = ESP_FAIL;

    if (!s_adc1_handle) {
        return ESP_ERR_INVALID_STATE;
    }

    for (int attempt = 1; attempt <= max_retries; attempt++) {
        /* Liga divisor resistivo */
        gpio_set_level(BAT_CTRL_PIN, 1);
        vTaskDelay(pdMS_TO_TICKS(10)); /* Tempo de estabilização do circuito RC */

        int raw = 0;
        last_err = adc_oneshot_read(s_adc1_handle, BAT_ADC_CHANNEL, &raw);

        /* Desliga divisor imediatamente após a leitura para evitar dreno da bateria */
        gpio_set_level(BAT_CTRL_PIN, 0);

        if (last_err == ESP_OK) {
            *out_raw = raw;
            return ESP_OK;
        }

        ESP_LOGW(TAG, "Tentativa %d/%d de leitura do ADC falhou (%s). Retentando em %lu ms...",
                 attempt, max_retries, esp_err_to_name(last_err), (unsigned long)backoff_ms);
        vTaskDelay(pdMS_TO_TICKS(backoff_ms));
        backoff_ms *= 2; /* Backoff exponencial: 20ms -> 40ms -> 80ms -> 160ms */
    }

    ESP_LOGE(TAG, "Todas as %d tentativas de leitura do ADC falharam. Erro final: %s",
             max_retries, esp_err_to_name(last_err));
    return last_err;
}

/**
 * @brief Tarefa FreeRTOS para monitorar a bateria periodicamente com resiliência.
 * 
 * Esta tarefa assume que battery_init() já foi chamado na inicialização do sistema.
 * Não utiliza ESP_ERROR_CHECK. Caso ocorram falhas contínuas de hardware, ela aplica
 * backoff longo e tenta recuperar sem travar nem causar abort do ESP32.
 * 
 * @param pvParameters Parâmetros da tarefa FreeRTOS (não utilizado).
 */
void battery_task(void *pvParameters)
{
    (void)pvParameters;
    ESP_LOGI(TAG, "Tarefa de monitoramento de bateria iniciada (Core %d)", xPortGetCoreID());

    int prev_avg = -1;
    int prev_percent = -1;

    while (1) {
        /* Caso o módulo não tenha sido inicializado antes, tenta inicializar com backoff */
        if (!s_battery_initialized || !s_adc1_handle) {
            ESP_LOGW(TAG, "Módulo de bateria não pronto; tentando inicializar agora...");
            esp_err_t init_err = battery_init();
            if (init_err != ESP_OK) {
                ESP_LOGE(TAG, "Falha na inicialização da bateria (%s). Próxima tentativa em 5 segundos.",
                         esp_err_to_name(init_err));
                vTaskDelay(pdMS_TO_TICKS(5000));
                continue;
            }
        }

        int samples_raw[BATTERY_MEDIAN_SAMPLES];
        bool samples_ok = true;

        /* Coleta N amostras com proteção de retry individual */
        for (int i = 0; i < BATTERY_MEDIAN_SAMPLES; i++) {
            int raw_val = 0;
            esp_err_t sample_err = read_single_sample_with_retry(&raw_val);
            if (sample_err != ESP_OK) {
                ESP_LOGE(TAG, "Falha ao obter amostra de bateria %d/%d (%s). Abortando ciclo.",
                         i + 1, BATTERY_MEDIAN_SAMPLES, esp_err_to_name(sample_err));
                samples_ok = false;
                break;
            }
            samples_raw[i] = raw_val;
            vTaskDelay(pdMS_TO_TICKS(10)); /* Intervalo seguro entre medições */
        }

        /* Se falhou a coleta das amostras, espera 2s e tenta novamente no próximo ciclo */
        if (!samples_ok) {
            ESP_LOGW(TAG, "Ciclo de medição interrompido por erro de leitura. Aguardando 2s...");
            vTaskDelay(pdMS_TO_TICKS(2000));
            continue;
        }

        /* 1. Cálculo da mediana das amostras */
        int median_raw = calculate_median(samples_raw, BATTERY_MEDIAN_SAMPLES);
        int voltage_mv = 0;

        /* 2. Conversão para milivolts (utilizando calibração com fallback) */
        if (s_calibration_enabled && s_adc1_cali_handle) {
            esp_err_t cali_err = adc_cali_raw_to_voltage(s_adc1_cali_handle, median_raw, &voltage_mv);
            if (cali_err != ESP_OK) {
                ESP_LOGW(TAG, "Falha na conversão calibrada (%s). Aplicando aproximação direta.",
                         esp_err_to_name(cali_err));
                voltage_mv = (median_raw * 3300) / 4095;
            }
        } else {
            /* Fallback matemático padrão: 12 bits (4095) com fundo de escala ~3300mV */
            voltage_mv = (median_raw * 3300) / 4095;
        }

        /* 3. Cálculo da tensão real da bateria levando em conta o divisor resistivo 200k/100k (x3) */
        int battery_voltage_mv = voltage_mv * 3;
        float battery_voltage_v = battery_voltage_mv / 1000.0f;

        /* Se houver alteração significativa ou se for a primeira leitura */
        if (prev_avg == -1 || prev_avg != voltage_mv) {
            ESP_LOGI(TAG, "=============================================");
            ESP_LOGI(TAG, "[BATERIA] Mediana: raw=%d | Pino: %d mV | Tensão real: %.2f V",
                     median_raw, voltage_mv, battery_voltage_v);
            ESP_LOGI(TAG, "=============================================");
            
            prev_avg = voltage_mv;

            /* 4. Converte para porcentagem da curva de descarga */
            uint8_t percent = battery_percent_from_mv(battery_voltage_mv);
            if (percent != prev_percent) {
                prev_percent = percent;
                headset_battery_evt_t ev = {
                    .percent = percent,
                    .millivolts = (uint16_t)battery_voltage_mv
                };
                ESP_LOGI(TAG, "[BATERIA] Publicando evento HEADSET_EVT_BATTERY: %u%% (%u mV)",
                         percent, battery_voltage_mv);
                headset_event_post(HEADSET_EVT_BATTERY, &ev, sizeof(ev));

                /* Publica no barramento central hs_events (SENSOR_EVT) */
                sensor_battery_evt_t s_ev = {
                    .percent = percent,
                    .millivolts = (uint16_t)battery_voltage_mv,
                    .is_charging = false,
                };
                hs_event_post(SENSOR_EVT, SENSOR_EVT_BATTERY, &s_ev, sizeof(s_ev));
            }
        }

        /* Ciclo de amostragem padrão: 15 segundos entre cada medição completa */
        vTaskDelay(pdMS_TO_TICKS(15000));
    }
}
