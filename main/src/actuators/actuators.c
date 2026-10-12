/**
 * @file actuators.c
 * @brief Implementação do Actor Actuators sob demanda para controle do Vibracall via LEDC PWM.
 *
 * Em conformidade com AGENTS.md (WP 5.2, WP 5.3):
 * - Dono único do canal LEDC PWM para o motor háptico (Vibracall).
 * - Pino: BOARD_VIBRACALL_GPIO (GPIO 15 aprovado).
 * - Task sob demanda no Core 0, prioridade 3, idle-stop de 5 segundos.
 * - Assina eventos do barramento central (SENSOR_EVT, BT_EVT):
 *     * BT_EVT_CALL_STATE (Chamada recebida tocando / INCOMING).
 *     * BT_EVT_LINK_UP / LINK_DOWN (Conexão ou desconexão do link Bluetooth).
 *     * SENSOR_EVT_BATTERY (Bateria baixa <= 15%).
 * - Responde aos comandos ACTUATOR_CMD_VIBRATE enviados pela UI, phone_ctl ou barramento.
 */

#include "actuators.h"

#include <string.h>
#include "esp_log.h"
#include "esp_timer.h"
#include "driver/ledc.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "pinout.h"
#include "hs_actor.h"
#include "hs_cmds.h"
#include "hs_events.h"
#include "settings.h"
#include "ui_model.h"

static const char *TAG = "actuators";

/* --- Configuração do Periférico LEDC --- */
#define VIB_LEDC_TIMER          LEDC_TIMER_1
#define VIB_LEDC_MODE           LEDC_LOW_SPEED_MODE
#define VIB_LEDC_CHANNEL        LEDC_CHANNEL_1
#define VIB_LEDC_DUTY_RES       LEDC_TIMER_10_BIT /* 0 a 1023 */
#define VIB_LEDC_FREQ_HZ        150               /* 150 Hz: frequência ideal para motores de massa excêntrica (ERM) */
#define VIB_DUTY_MAX            1023

/* --- Configuração do Actor Sob Demanda --- */
#define ACTUATORS_STACK_SIZE    3072
#define ACTUATORS_PRIO          3
#define ACTUATORS_CORE          0
#define ACTUATORS_QUEUE_LEN     8
#define ACTUATORS_IDLE_MS       5000 /* Encerra a task após 5 segundos ociosa */

static hs_actor_t *s_actuators_actor = NULL;
static bool s_ledc_initialized = false;

/* ============================================================================
 * CONTROLE DE HARDWARE LEDC (MOTOR HÁPTICO)
 * ============================================================================ */

static void vibracall_hardware_init(void)
{
    if (s_ledc_initialized) {
        return;
    }

#if defined(BOARD_VIBRACALL_GPIO) && (BOARD_VIBRACALL_GPIO >= 0)
    /* 1. Configura o Timer LEDC */
    ledc_timer_config_t timer_conf = {
        .speed_mode       = VIB_LEDC_MODE,
        .duty_resolution  = VIB_LEDC_DUTY_RES,
        .timer_num        = VIB_LEDC_TIMER,
        .freq_hz          = VIB_LEDC_FREQ_HZ,
        .clk_cfg          = LEDC_AUTO_CLK,
    };
    esp_err_t err = ledc_timer_config(&timer_conf);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Falha ao configurar timer LEDC do vibracall: %s", esp_err_to_name(err));
        return;
    }

    /* 2. Configura o Canal LEDC no pino atribuído */
    ledc_channel_config_t chan_conf = {
        .gpio_num       = BOARD_VIBRACALL_GPIO,
        .speed_mode     = VIB_LEDC_MODE,
        .channel        = VIB_LEDC_CHANNEL,
        .intr_type      = LEDC_INTR_DISABLE,
        .timer_sel      = VIB_LEDC_TIMER,
        .duty           = 0,
        .hpoint         = 0,
        .flags.output_invert = 0,
    };
    err = ledc_channel_config(&chan_conf);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Falha ao configurar canal LEDC do vibracall no GPIO %d: %s",
                 BOARD_VIBRACALL_GPIO, esp_err_to_name(err));
        return;
    }

    s_ledc_initialized = true;
    ESP_LOGI(TAG, "Hardware LEDC do Vibracall configurado com sucesso no GPIO %d (freq %d Hz)",
             BOARD_VIBRACALL_GPIO, VIB_LEDC_FREQ_HZ);
#else
    ESP_LOGW(TAG, "BOARD_VIBRACALL_GPIO nao definido ou desativado (-1)");
#endif
}

static void vibracall_set_pwm(uint8_t intensity_pct)
{
    if (!s_ledc_initialized) {
        vibracall_hardware_init();
    }

#if defined(BOARD_VIBRACALL_GPIO) && (BOARD_VIBRACALL_GPIO >= 0)
    if (intensity_pct > 100) {
        intensity_pct = 100;
    }
    uint32_t duty = (uint32_t)((intensity_pct * VIB_DUTY_MAX) / 100);
    ledc_set_duty(VIB_LEDC_MODE, VIB_LEDC_CHANNEL, duty);
    ledc_update_duty(VIB_LEDC_MODE, VIB_LEDC_CHANNEL);
#else
    (void)intensity_pct;
#endif
}

static void vibracall_hardware_stop(void)
{
#if defined(BOARD_VIBRACALL_GPIO) && (BOARD_VIBRACALL_GPIO >= 0)
    if (s_ledc_initialized) {
        ledc_stop(VIB_LEDC_MODE, VIB_LEDC_CHANNEL, 0);
    }
#endif
}

/* ============================================================================
 * CALLBACKS DO CICLO DE VIDA DO ACTOR
 * ============================================================================ */

static void actuators_on_start(void *ctx)
{
    (void)ctx;
    ESP_LOGI(TAG, "Actor actuators iniciado sob demanda");
    vibracall_hardware_init();
}

static void actuators_on_stop(void *ctx)
{
    (void)ctx;
    ESP_LOGI(TAG, "Actor actuators ocioso: desligando atuadores e liberando recursos");
    vibracall_hardware_stop();
}

static void notify_ui_vibrating_state(bool active)
{
    ui_model_update_msg_t msg = {
        .type = UI_UPDATE_VIBRACALL_STATE,
        .payload.vib_state = { .active = active }
    };
    ui_model_post_update(&msg);
}

static void execute_vibration(uint16_t duration_ms, uint8_t intensity_pct)
{
    /* 1. Checa se o recurso está ativado nas configurações do fone */
    uint8_t enabled = 1;
    settings_get_u8(SETTINGS_KEY_HAPTIC_ENABLE, &enabled, 1);
    if (!enabled) {
        ESP_LOGD(TAG, "Vibracall ignorado: funcionalidade desativada pelo usuario");
        return;
    }

    /* 2. Se a intensidade informada for 0, busca intensidade salva */
    if (intensity_pct == 0) {
        uint8_t saved_intens = 80;
        settings_get_u8(SETTINGS_KEY_HAPTIC_INTENS, &saved_intens, 80);
        intensity_pct = saved_intens;
    }

    if (duration_ms == 0) {
        duration_ms = 250;
    }

    ESP_LOGI(TAG, "Executando vibracall: %u ms com intensidade %u%%",
             (unsigned int)duration_ms, (unsigned int)intensity_pct);

    /* Atualiza modelo de UI para exibir estado "Vibrando" */
    notify_ui_vibrating_state(true);

    /* Aciona o motor */
    vibracall_set_pwm(intensity_pct);

    /* Bloqueia com vTaskDelay apenas dentro do contexto seguro da task do ator */
    vTaskDelay(pdMS_TO_TICKS(duration_ms));

    /* Desliga o motor */
    vibracall_set_pwm(0);

    /* Restaura estado na UI para "Pronto" */
    notify_ui_vibrating_state(false);
}

static void actuators_on_msg(hs_actor_t *self, const hs_msg_t *msg)
{
    (void)self;
    switch (msg->cmd) {
    case ACTUATOR_CMD_VIBRATE: {
        if (msg->len >= sizeof(actuator_cmd_vibrate_t)) {
            const actuator_cmd_vibrate_t *cmd = (const actuator_cmd_vibrate_t *)msg->data;
            execute_vibration(cmd->duration_ms, cmd->intensity_percent);
        } else {
            /* Vibração curta padrão */
            execute_vibration(250, 0);
        }
        break;
    }

    case ACTUATOR_CMD_SET_BACKLIGHT: {
        if (msg->len >= sizeof(actuator_cmd_backlight_t)) {
            const actuator_cmd_backlight_t *cmd = (const actuator_cmd_backlight_t *)msg->data;
            ESP_LOGD(TAG, "Comando backlight recebido: %u%%", cmd->duty_percent);
        }
        break;
    }

    case ACTUATOR_CMD_SET_EARS: {
        if (msg->len >= sizeof(actuator_cmd_ears_t)) {
            const actuator_cmd_ears_t *cmd = (const actuator_cmd_ears_t *)msg->data;
            ESP_LOGD(TAG, "Comando orelhas: E=%d, D=%d", cmd->angle_left_deg, cmd->angle_right_deg);
        }
        break;
    }

    default:
        ESP_LOGW(TAG, "Comando desconhecido recebido: 0x%04X", msg->cmd);
        break;
    }
}

/* ============================================================================
 * SUBSCRIBERS DO BARRAMENTO DE EVENTOS (esp_event)
 * ============================================================================ */

static void actuators_event_handler(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg;

    /* Apenas despacha comando não-bloqueante para a fila do ator actuators */
    if (base == BT_EVT) {
        switch (id) {
        case BT_EVT_CALL_STATE: {
            if (data) {
                const bt_call_state_evt_t *call = (const bt_call_state_evt_t *)data;
                if (call->state == BT_CALL_INCOMING) {
                    /* Padrão para chamada recebida: vibração mais longa (600 ms) */
                    ESP_LOGI(TAG, "Chamada telefonica tocando: acionando vibracall");
                    actuators_vibrate(600, 0);
                }
            }
            break;
        }

        case BT_EVT_LINK_UP: {
            /* Conexão estabelecida: duplo pulso curto (200 ms) */
            ESP_LOGI(TAG, "Dispositivo conectado: acionando feedback haptico");
            actuators_vibrate(200, 0);
            break;
        }

        case BT_EVT_LINK_DOWN: {
            /* Desconexão: pulso de alerta (350 ms) */
            ESP_LOGI(TAG, "Dispositivo desconectado: acionando feedback haptico");
            actuators_vibrate(350, 0);
            break;
        }

        default:
            break;
        }
    } else if (base == SENSOR_EVT) {
        switch (id) {
        case SENSOR_EVT_BATTERY: {
            if (data) {
                const sensor_battery_evt_t *bat = (const sensor_battery_evt_t *)data;
                /* Alerta de bateria fraca crítica: percentual <= 15% */
                static bool s_low_bat_warned = false;
                if (bat->percent <= 15 && !bat->is_charging) {
                    if (!s_low_bat_warned) {
                        s_low_bat_warned = true;
                        ESP_LOGW(TAG, "Bateria baixa (%u%%): emitindo alerta de vibracall", bat->percent);
                        actuators_vibrate(400, 0);
                    }
                } else if (bat->percent > 20) {
                    s_low_bat_warned = false;
                }
            }
            break;
        }

        default:
            break;
        }
    }
}

/* ============================================================================
 * INTERFACE PÚBLICA
 * ============================================================================ */

esp_err_t actuators_init(void)
{
    if (s_actuators_actor) {
        return ESP_OK; /* Já inicializado */
    }

    hs_actor_cfg_t cfg = {
        .name       = "act_actuators",
        .stack      = ACTUATORS_STACK_SIZE,
        .prio       = ACTUATORS_PRIO,
        .core       = ACTUATORS_CORE,
        .queue_len  = ACTUATORS_QUEUE_LEN,
        .idle_ms    = ACTUATORS_IDLE_MS,
        .on_msg     = actuators_on_msg,
        .on_start   = actuators_on_start,
        .on_stop    = actuators_on_stop,
        .ctx        = NULL,
    };

    esp_err_t err = hs_actor_create(&cfg, &s_actuators_actor);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Falha ao criar ator actuators: %s", esp_err_to_name(err));
        return err;
    }

    /* Registra nos eventos do barramento central */
    hs_event_register(BT_EVT, BT_EVT_CALL_STATE, actuators_event_handler, NULL);
    hs_event_register(BT_EVT, BT_EVT_LINK_UP, actuators_event_handler, NULL);
    hs_event_register(BT_EVT, BT_EVT_LINK_DOWN, actuators_event_handler, NULL);
    hs_event_register(SENSOR_EVT, SENSOR_EVT_BATTERY, actuators_event_handler, NULL);

    ESP_LOGI(TAG, "Actor actuators criado e registrado no barramento de eventos (idle_ms=%d)", ACTUATORS_IDLE_MS);
    return ESP_OK;
}

hs_actor_t *actuators_get_actor(void)
{
    return s_actuators_actor;
}

esp_err_t actuators_vibrate(uint16_t duration_ms, uint8_t intensity_percent)
{
    if (!s_actuators_actor) {
        return ESP_ERR_INVALID_STATE;
    }

    actuator_cmd_vibrate_t cmd = {
        .duration_ms = duration_ms,
        .intensity_percent = intensity_percent,
    };

    /* Envio não bloqueante para a fila do ator sob demanda */
    return hs_actor_send(s_actuators_actor, ACTUATOR_CMD_VIBRATE, &cmd, sizeof(cmd), pdMS_TO_TICKS(10));
}
