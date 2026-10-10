#include "apds9930.h"

#include "esp_check.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "board_i2c.h"
#include "hs_events.h"
#include "settings.h"
#include "pinout.h"

/* Bit de comando do APDS-9930: 0x80 | (0x20 = auto-incremento) | registrador */
#define CMD_BYTE        0x80
#define CMD_AUTO_INC    0xA0

#define REG_ENABLE      0x00
#define REG_PTIME       0x02
#define REG_WTIME       0x03
#define REG_PPCOUNT     0x0E
#define REG_CONTROL     0x0F
#define REG_ID          0x12
#define REG_PDATAL      0x18

#define ENABLE_PON      (1 << 0)
#define ENABLE_PEN      (1 << 2)

#define CONTROL_PDIODE_CH1 (0x20)   /* diodo de proximidade = CH1, ganho 1x, LED 100 mA */

static const char *TAG = "apds9930";

static i2c_master_dev_handle_t s_dev;
static volatile bool s_worn = true;

static esp_err_t write_reg(uint8_t reg, uint8_t val)
{
    const uint8_t buf[2] = { CMD_BYTE | reg, val };
    return i2c_master_transmit(s_dev, buf, sizeof buf, 50);
}

static esp_err_t read_reg(uint8_t reg, uint8_t *val)
{
    const uint8_t cmd = CMD_BYTE | reg;
    return i2c_master_transmit_receive(s_dev, &cmd, 1, val, 1, 50);
}

static esp_err_t read_proximity(uint16_t *out)
{
    const uint8_t cmd = CMD_AUTO_INC | REG_PDATAL;
    uint8_t d[2];
    ESP_RETURN_ON_ERROR(i2c_master_transmit_receive(s_dev, &cmd, 1, d, 2, 50), TAG, "PDATA");
    *out = (uint16_t)(d[1] << 8) | d[0];
    return ESP_OK;
}

#define PWR_GPIO        BOARD_APDS9930_PWR_GPIO
#define MEDIAN_SAMPLES  10

static void power_set(bool on)
{
#if PWR_GPIO >= 0
    gpio_set_level((gpio_num_t)PWR_GPIO, on);
#else
    (void)on;
#endif
}

/* Escreve a configuracao de proximidade e liga o bloco (PON | PEN). Necessario apos cada energização do VDD. */
static esp_err_t apds_program(void)
{
    ESP_RETURN_ON_ERROR(write_reg(REG_ENABLE, 0x00), TAG, "enable=0");
    ESP_RETURN_ON_ERROR(write_reg(REG_PTIME, 0xFF), TAG, "ptime");      /* 2.73 ms, 10 bits */
    ESP_RETURN_ON_ERROR(write_reg(REG_WTIME, 0xFF), TAG, "wtime");
    ESP_RETURN_ON_ERROR(write_reg(REG_PPCOUNT, 8), TAG, "ppcount");
    ESP_RETURN_ON_ERROR(write_reg(REG_CONTROL, CONTROL_PDIODE_CH1), TAG, "control");
    return write_reg(REG_ENABLE, ENABLE_PON | ENABLE_PEN);
}

/**
 * @brief Calcula a mediana de um vetor de leituras de 16 bits não assinaladas.
 * 
 * Utiliza o algoritmo Insertion Sort (adequado e rápido para pequenas quantidades de amostras)
 * para ordenar o vetor 'v'.
 * - Para N ímpar: seleciona o valor central exato.
 * - Para N par (ex: 10 amostras): calcula a média entre os dois valores centrais.
 * 
 * A mediana elimina ruídos pontuais de reflexão óptica, interferência de luz externa
 * e variações espúrias durante a amostragem do sensor de proximidade.
 * 
 * @param v Vetor com os dados brutos lidos do sensor.
 * @param n Quantidade de amostras (definido como 10 pelo MEDIAN_SAMPLES).
 * @return uint16_t Valor mediano filtrado.
 */
static uint16_t median_u16(uint16_t *v, int n)
{
    // Algoritmo de ordenação por inserção para ordenar as amostras
    for (int i = 1; i < n; i++) {
        uint16_t k = v[i];
        int j = i - 1;
        while (j >= 0 && v[j] > k) {
            v[j + 1] = v[j];
            j--;
        }
        v[j + 1] = k;
    }
    // Se o número de elementos for ímpar pega o elemento central, se for par calcula a média dos dois elementos centrais
    return (n & 1) ? v[n / 2] : (uint16_t)(((int)v[n / 2 - 1] + v[n / 2]) / 2);
}

/**
 * @brief Amostra a proximidade do sensor APDS-9930 utilizando a mediana de 10 medições.
 * 
 * Sequência de operação:
 * 1. Energiza o sensor (ou ativa registradores PON/PEN).
 * 2. Aguarda estabilização térmica e óptica inicial (15ms).
 * 3. Coleta consecutivamente 10 leituras do registrador PDATA com pequeno intervalo entre elas.
 * 4. Aplica o filtro de mediana sobre o conjunto de 10 amostras.
 * 5. Coloca o sensor em modo de baixo consumo (sleep).
 * 
 * @param[out] out Ponteiro para armazenar a mediana calculada.
 * @return esp_err_t ESP_OK em caso de sucesso ou código de erro I2C.
 */
static esp_err_t sample_proximity(uint16_t *out)
{
    esp_err_t err = ESP_OK;
#if PWR_GPIO >= 0
    power_set(true);
    vTaskDelay(pdMS_TO_TICKS(20));
    err = apds_program();
#elif CONFIG_HEADSET_APDS_DUTY_CYCLE
    err = write_reg(REG_ENABLE, ENABLE_PON | ENABLE_PEN);
#endif
    if (err == ESP_OK) {
        // Buffer local contendo exatamente as 10 leituras para cálculo da mediana
        uint16_t s[MEDIAN_SAMPLES];
        vTaskDelay(pdMS_TO_TICKS(15));
        
        // Laço para colher as 10 amostras consecutivas
        for (int i = 0; i < MEDIAN_SAMPLES && err == ESP_OK; i++) {
            err = read_proximity(&s[i]);
            if (i < MEDIAN_SAMPLES - 1) {
                // Intervalo mínimo entre ciclos internos do conversor óptico
                vTaskDelay(pdMS_TO_TICKS(6));
            }
        }
        
        // Se todas as 10 leituras foram efetuadas com sucesso, obtém a mediana
        if (err == ESP_OK) {
            *out = median_u16(s, MEDIAN_SAMPLES);
        }
    }
#if PWR_GPIO >= 0
    power_set(false);
#elif CONFIG_HEADSET_APDS_DUTY_CYCLE
    write_reg(REG_ENABLE, 0x00);
#endif
    return err;
}


static void publish(bool worn)
{
    s_worn = worn;
    ESP_LOGW(TAG, "Fone %s", worn ? "COLOCADO na cabeca" : "RETIRADO da cabeca");

    /* Publica no barramento central hs_events */
    hs_event_post(SENSOR_EVT, worn ? SENSOR_EVT_WORN : SENSOR_EVT_REMOVED, NULL, 0);
}

/* Le o ID e programa so o bloco de proximidade (PON | PEN). Pode ser repetida a qualquer momento. */
static esp_err_t apds_configure(uint8_t *id)
{
    power_set(true);
    vTaskDelay(pdMS_TO_TICKS(20));
    ESP_RETURN_ON_ERROR(read_reg(REG_ID, id), TAG, "ID");
    if (*id != 0x39 && *id != 0x29) {
        ESP_LOGW(TAG, "ID inesperado 0x%02X (esperado 0x39); seguindo mesmo assim", *id);
    }
    ESP_RETURN_ON_ERROR(apds_program(), TAG, "program");
    vTaskDelay(pdMS_TO_TICKS(10));
#if PWR_GPIO >= 0
    power_set(false);
#elif CONFIG_HEADSET_APDS_DUTY_CYCLE
    ESP_RETURN_ON_ERROR(write_reg(REG_ENABLE, 0x00), TAG, "sleep");
#endif
    return ESP_OK;
}

static void apds_task(void *arg)
{
    /* Carrega limiares do actor settings se persistidos, mantendo Kconfig como fallback */
    uint16_t cfg_th_on = CONFIG_HEADSET_APDS_ON_THRESHOLD;
    uint16_t cfg_th_off = CONFIG_HEADSET_APDS_OFF_THRESHOLD;
    settings_get_u16(SETTINGS_KEY_SENS_THRESH_ON, &cfg_th_on, CONFIG_HEADSET_APDS_ON_THRESHOLD);
    settings_get_u16(SETTINGS_KEY_SENS_THRESH_OFF, &cfg_th_off, CONFIG_HEADSET_APDS_OFF_THRESHOLD);

    const int on_delta  = (int)cfg_th_on;
    const int off_delta = (int)cfg_th_off;
    const int need      = (CONFIG_HEADSET_APDS_DEBOUNCE_MS + CONFIG_HEADSET_APDS_POLL_MS - 1) /
                          CONFIG_HEADSET_APDS_POLL_MS;

    bool ready = false;        /* sensor configurado e respondendo */
    bool fallback_sent = false;
    bool have_state = false;
    bool state = false;        /* estado ja publicado */
    int  agree = 0;            /* amostras consecutivas que discordam do estado publicado */
    int  fails = 0;
    int  base = 0;             /* nivel "sem nada perto" (luz ambiente); so e atualizado com o fone retirado */

    for (;;) {
        if (!ready) {
            uint8_t id = 0;
            esp_err_t err = apds_configure(&id);
            if (err != ESP_OK) {
                ESP_LOGE(TAG, "APDS-9930 nao respondeu (%s); nova tentativa em 2 s", esp_err_to_name(err));
                if (!fallback_sent) {   /* sem sensor, assume em uso para nao deixar o fone mudo */
                    fallback_sent = true;
                    publish(true);
                }
                vTaskDelay(pdMS_TO_TICKS(2000));
                continue;
            }
            ESP_LOGI(TAG, "APDS-9930 ativo (ID 0x%02X, colocado: base+%d, retirado: base+%d, debounce %d ms)", id,
                     on_delta, off_delta, CONFIG_HEADSET_APDS_DEBOUNCE_MS);
            ready = true;
            fails = 0;
            have_state = false;
        }

        uint16_t prox = 0;
        if (sample_proximity(&prox) != ESP_OK) {
            if (++fails % 10 == 0) {
                ESP_LOGE(TAG, "%d falhas I2C seguidas ao ler o APDS-9930", fails);
            }
            if (fails >= 30) {   /* provavel reset/queda de tensao do sensor: reprograma */
                ESP_LOGW(TAG, "Reconfigurando o APDS-9930");
                ready = false;
            }
            vTaskDelay(pdMS_TO_TICKS(CONFIG_HEADSET_APDS_POLL_MS));
            continue;
        }
        fails = 0;

        const int on_thr = base + on_delta;
        const int off_thr = base + off_delta;

        /* Histerese: so "candidata" a mudar quando cruza o limiar do lado oposto */
        bool candidate = state;
        if (!have_state) {
            candidate = prox >= on_thr;
        } else if (!state && prox >= on_thr) {
            candidate = true;
        } else if (state && prox <= off_thr) {
            candidate = false;
        }

        if (!have_state) {
            have_state = true;
            state = candidate;
            publish(state);
        } else if (candidate != state) {
            if (++agree >= need) {   /* debounce: mantem o novo estado por ~DEBOUNCE_MS */
                state = candidate;
                agree = 0;
                publish(state);
            }
        } else {
            agree = 0;
        }

        /* Acompanha a luz ambiente so enquanto nada esta perto (media movel 1/16) */
        if (have_state && !state && agree == 0) {
            base += ((int)prox - base) / 16;
        }

        vTaskDelay(pdMS_TO_TICKS(CONFIG_HEADSET_APDS_POLL_MS));
    }
}

esp_err_t apds9930_start(void)
{
#if PWR_GPIO >= 0
    const gpio_config_t pwr = {
        .pin_bit_mask = 1ULL << PWR_GPIO,
        .mode         = GPIO_MODE_OUTPUT,
    };
    ESP_RETURN_ON_ERROR(gpio_config(&pwr), TAG, "gpio de energia");
    power_set(false);
#endif
    esp_err_t err = board_i2c_add_device(BOARD_APDS9930_I2C_ADDR, BOARD_I2C_HZ, &s_dev);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Sem acesso ao I2C para o APDS-9930 (%s). Assumindo fone em uso.", esp_err_to_name(err));
        publish(true);
        return err;
    }
    /* A configuracao e as tentativas de reconexao ficam na propria task */
    if (xTaskCreatePinnedToCore(apds_task, "apds", 3072, NULL, 4, NULL, 0) != pdPASS) {
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

bool apds9930_is_worn(void)
{
    return s_worn;
}
