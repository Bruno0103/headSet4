#include "apds9930.h"

#include "esp_check.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "board_i2c.h"
#include "headset_events.h"
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

static void publish(bool worn)
{
    s_worn = worn;
    ESP_LOGW(TAG, "Fone %s", worn ? "COLOCADO na cabeca" : "RETIRADO da cabeca");
    headset_event_post(worn ? HEADSET_EVT_WORN : HEADSET_EVT_REMOVED, NULL, 0);
}

static void apds_task(void *arg)
{
    (void)arg;
    const int on_thr  = CONFIG_HEADSET_APDS_ON_THRESHOLD;
    const int off_thr = CONFIG_HEADSET_APDS_OFF_THRESHOLD;
    const int need    = (CONFIG_HEADSET_APDS_DEBOUNCE_MS + CONFIG_HEADSET_APDS_POLL_MS - 1) /
                        CONFIG_HEADSET_APDS_POLL_MS;

    bool have_state = false;
    bool state = false;       /* estado ja publicado */
    int  agree = 0;           /* amostras consecutivas que discordam do estado publicado */
    int  fails = 0;

    for (;;) {
        uint16_t prox;
        if (read_proximity(&prox) != ESP_OK) {
            if (++fails == 10) {
                ESP_LOGE(TAG, "10 falhas I2C seguidas ao ler o APDS-9930");
            }
            vTaskDelay(pdMS_TO_TICKS(CONFIG_HEADSET_APDS_POLL_MS));
            continue;
        }
        fails = 0;

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
            ESP_LOGI(TAG, "Leitura inicial: prox=%u", prox);
            publish(state);
        } else if (candidate != state) {
            if (++agree >= need) {   /* debounce: mantem o novo estado por ~DEBOUNCE_MS */
                state = candidate;
                agree = 0;
                ESP_LOGI(TAG, "prox=%u", prox);
                publish(state);
            }
        } else {
            agree = 0;
        }
        vTaskDelay(pdMS_TO_TICKS(CONFIG_HEADSET_APDS_POLL_MS));
    }
}

esp_err_t apds9930_start(void)
{
    esp_err_t err = board_i2c_add_device(BOARD_APDS9930_I2C_ADDR, BOARD_I2C_HZ, &s_dev);
    uint8_t id = 0;
    if (err == ESP_OK) {
        err = read_reg(REG_ID, &id);
    }
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "APDS-9930 nao respondeu (%s). Assumindo fone em uso.", esp_err_to_name(err));
        publish(true);
        return err;
    }
    if (id != 0x39 && id != 0x29) {
        ESP_LOGW(TAG, "ID inesperado 0x%02X (esperado 0x39); seguindo mesmo assim", id);
    }

    /* Desliga, configura e liga somente o bloco de proximidade (PON | PEN) */
    ESP_RETURN_ON_ERROR(write_reg(REG_ENABLE, 0x00), TAG, "enable=0");
    ESP_RETURN_ON_ERROR(write_reg(REG_PTIME, 0xFF), TAG, "ptime");      /* 2.73 ms, 10 bits */
    ESP_RETURN_ON_ERROR(write_reg(REG_WTIME, 0xFF), TAG, "wtime");
    ESP_RETURN_ON_ERROR(write_reg(REG_PPCOUNT, 8), TAG, "ppcount");
    ESP_RETURN_ON_ERROR(write_reg(REG_CONTROL, CONTROL_PDIODE_CH1), TAG, "control");
    ESP_RETURN_ON_ERROR(write_reg(REG_ENABLE, ENABLE_PON | ENABLE_PEN), TAG, "enable");
    vTaskDelay(pdMS_TO_TICKS(10));

    if (xTaskCreatePinnedToCore(apds_task, "apds", 3072, NULL, 4, NULL, 0) != pdPASS) {
        return ESP_ERR_NO_MEM;
    }
    ESP_LOGI(TAG, "APDS-9930 ativo (ID 0x%02X, on>=%d off<=%d, debounce %d ms)", id,
             CONFIG_HEADSET_APDS_ON_THRESHOLD, CONFIG_HEADSET_APDS_OFF_THRESHOLD,
             CONFIG_HEADSET_APDS_DEBOUNCE_MS);
    return ESP_OK;
}

bool apds9930_is_worn(void)
{
    return s_worn;
}
