/**
 * @file audio.c
 * @brief Implementação do Actor Audio e orquestração do subsistema de áudio (Agente A3).
 *
 * Arquitetura Event-Driven & Actor Model (AGENTS.md §4.2, WP 3.1):
 * - Centraliza o controle e a máquina de estados do codec WM8960, I2S DMA, EQ e SFX.
 * - Elimina condições de corrida ao serializar comandos na fila privada do ator.
 * - Publica eventos de mudança de estado em AUDIO_EVT via esp_event.
 * - Mantém os buffers contínuos PCM completamente separados no plano de dados.
 */

#include "audio.h"

#include <string.h>

#include "esp_check.h"
#include "esp_log.h"
#include "esp_event.h"

#include "audio_codec.h"
#include "audio_io.h"
#include "eq.h"
#include "sfx.h"
#include "hs_events.h"

#ifdef CONFIG_PM_ENABLE
#include "esp_pm.h"
#endif

static const char *TAG = "act_audio";

/**
 * @brief Contexto interno e estado privado do Actor Audio.
 */
typedef struct {
    hs_actor_t       *actor;             /**< Handle do ator FreeRTOS. */
    audio_mode_t      current_mode;      /**< Modo atual (IDLE, MUSIC_A2DP, CALL_HFP, TONE_SFX). */
    uint32_t          current_rate;      /**< Taxa de amostragem configurada no momento. */
    uint8_t           current_vol_pct;   /**< Volume master atual (0 a 100%). */
    bool              is_muted;          /**< Estado de mute do canal master. */
    uint8_t           current_eq_preset; /**< Preset ativo do equalizador. */
#ifdef CONFIG_PM_ENABLE
    esp_pm_lock_handle_t pm_lock;        /**< Power management lock para manter APB/CPU em alta frequência durante I2S. */
    bool                 pm_lock_held;   /**< Flag indicando se o lock de energia está atualmente retido. */
#endif
} audio_actor_ctx_t;

static audio_actor_ctx_t s_audio_ctx = {
    .actor             = NULL,
    .current_mode      = AUDIO_MODE_IDLE,
    .current_rate      = 44100,
    .current_vol_pct   = 100,
    .is_muted          = false,
    .current_eq_preset = 0,
#ifdef CONFIG_PM_ENABLE
    .pm_lock           = NULL,
    .pm_lock_held      = false,
#endif
};

/* ============================================================================
 * FUNÇÕES AUXILIARES DE PUBLICAÇÃO DE EVENTOS (AUDIO_EVT)
 * ============================================================================ */

/**
 * @brief Publica no barramento esp_event o evento AUDIO_EVT_MODE_CHANGED.
 */
static void post_mode_changed(audio_mode_t mode, uint32_t sample_rate)
{
    audio_mode_evt_t evt = {
        .mode        = mode,
        .sample_rate = sample_rate,
    };
    esp_err_t err = hs_event_post(AUDIO_EVT, AUDIO_EVT_MODE_CHANGED, &evt, sizeof(evt));
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Falha ao postar AUDIO_EVT_MODE_CHANGED: %s", esp_err_to_name(err));
    }
}

/**
 * @brief Publica no barramento esp_event o evento AUDIO_EVT_VOLUME_CHANGED.
 */
static void post_volume_changed(uint8_t vol_pct, bool muted)
{
    audio_volume_evt_t evt = {
        .volume_percent = vol_pct,
        .muted          = muted,
    };
    esp_err_t err = hs_event_post(AUDIO_EVT, AUDIO_EVT_VOLUME_CHANGED, &evt, sizeof(evt));
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Falha ao postar AUDIO_EVT_VOLUME_CHANGED: %s", esp_err_to_name(err));
    }
}

/**
 * @brief Publica no barramento esp_event o evento AUDIO_EVT_EQ_CHANGED.
 */
static void post_eq_changed(uint8_t preset_id, const int8_t gains_db[5])
{
    audio_eq_evt_t evt = {
        .preset_index = preset_id,
    };
    if (gains_db) {
        memcpy(evt.gains_db, gains_db, sizeof(evt.gains_db));
    } else {
        memset(evt.gains_db, 0, sizeof(evt.gains_db));
    }
    esp_err_t err = hs_event_post(AUDIO_EVT, AUDIO_EVT_EQ_CHANGED, &evt, sizeof(evt));
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Falha ao postar AUDIO_EVT_EQ_CHANGED: %s", esp_err_to_name(err));
    }
}

/* ============================================================================
 * GERENCIAMENTO DE POWER MANAGEMENT LOCK (esp_pm_lock) - WP 3.4 & WP 8.2
 * Mantém APB a 80 MHz durante reprodução I2S para evitar jitter/underrun de DMA
 * ============================================================================ */

static void audio_pm_lock_acquire(void)
{
#ifdef CONFIG_PM_ENABLE
    if (s_audio_ctx.pm_lock && !s_audio_ctx.pm_lock_held) {
        esp_err_t err = esp_pm_lock_acquire(s_audio_ctx.pm_lock);
        if (err == ESP_OK) {
            s_audio_ctx.pm_lock_held = true;
            ESP_LOGI(TAG, "PM Lock retido (APB_MAX para I2S ativo)");
        } else {
            ESP_LOGW(TAG, "Falha ao reter PM Lock: %s", esp_err_to_name(err));
        }
    }
#endif
}

static void audio_pm_lock_release(void)
{
#ifdef CONFIG_PM_ENABLE
    if (s_audio_ctx.pm_lock && s_audio_ctx.pm_lock_held) {
        esp_err_t err = esp_pm_lock_release(s_audio_ctx.pm_lock);
        if (err == ESP_OK) {
            s_audio_ctx.pm_lock_held = false;
            ESP_LOGI(TAG, "PM Lock liberado (retorno ao estado ocioso/DFS)");
        } else {
            ESP_LOGW(TAG, "Falha ao liberar PM Lock: %s", esp_err_to_name(err));
        }
    }
#endif
}

/* ============================================================================
 * HANDLER PRINCIPAL DE MENSAGENS DO ACTOR AUDIO
 * Roda exclusivamente na task do Actor (Core 1, Prio 10)
 * ============================================================================ */

static void audio_actor_msg_handler(hs_actor_t *self, const hs_msg_t *msg)
{
    (void)self;
    if (!msg) {
        return;
    }

    switch (msg->cmd) {
    /* ------------------------------------------------------------------------
     * 1. INICIAR MÚSICA (A2DP)
     * ------------------------------------------------------------------------ */
    case AUDIO_CMD_START_MUSIC: {
        const audio_cmd_start_music_t *cmd_music = (const audio_cmd_start_music_t *)msg->data;
        uint32_t rate = (cmd_music && cmd_music->sample_rate > 0) ? cmd_music->sample_rate : 44100;

        ESP_LOGI(TAG, "[CMD] AUDIO_CMD_START_MUSIC: taxa=%lu Hz", (unsigned long)rate);

        /* Se já estava em chamada, a chamada tem prioridade segundo as regras de telecom */
        if (s_audio_ctx.current_mode == AUDIO_MODE_CALL_HFP) {
            ESP_LOGW(TAG, "Chamada HFP em andamento; ignorando inicio de musica");
            break;
        }

        esp_err_t err = audio_io_start(AUDIO_IO_MUSIC, rate);
        if (err == ESP_OK) {
            audio_pm_lock_acquire();
            s_audio_ctx.current_mode = AUDIO_MODE_MUSIC_A2DP;
            s_audio_ctx.current_rate = rate;
            post_mode_changed(AUDIO_MODE_MUSIC_A2DP, rate);
        } else {
            ESP_LOGE(TAG, "Falha ao iniciar I2S/codec para musica: %s", esp_err_to_name(err));
        }
        break;
    }

    /* ------------------------------------------------------------------------
     * 2. INICIAR CHAMADA TELEFÔNICA (HFP)
     * ------------------------------------------------------------------------ */
    case AUDIO_CMD_START_CALL: {
        const audio_cmd_start_call_t *cmd_call = (const audio_cmd_start_call_t *)msg->data;
        uint32_t rate = (cmd_call && cmd_call->sample_rate > 0) ? cmd_call->sample_rate : 8000;

        ESP_LOGI(TAG, "[CMD] AUDIO_CMD_START_CALL: taxa=%lu Hz, NR=%d", (unsigned long)rate,
                 cmd_call ? cmd_call->enable_nr : 1);

        /* audio_io_start interrompe qualquer musica ativa comutando o I2S full-duplex e filtros */
        esp_err_t err = audio_io_start(AUDIO_IO_CALL, rate);
        if (err == ESP_OK) {
            audio_pm_lock_acquire();
            s_audio_ctx.current_mode = AUDIO_MODE_CALL_HFP;
            s_audio_ctx.current_rate = rate;
            post_mode_changed(AUDIO_MODE_CALL_HFP, rate);
        } else {
            ESP_LOGE(TAG, "Falha ao iniciar I2S/codec para chamada: %s", esp_err_to_name(err));
        }
        break;
    }

    /* ------------------------------------------------------------------------
     * 3. PARAR ÁUDIO ATIVO
     * ------------------------------------------------------------------------ */
    case AUDIO_CMD_STOP: {
        const audio_cmd_stop_t *cmd_stop = (const audio_cmd_stop_t *)msg->data;
        uint8_t mask = cmd_stop ? cmd_stop->mode_mask : 0xFF;

        ESP_LOGI(TAG, "[CMD] AUDIO_CMD_STOP: mask=0x%02X, modo_atual=%d", mask, s_audio_ctx.current_mode);

        /* Se mask == 0xFF ou corresponder ao modo ativo atual, desliga o streaming */
        if (mask == 0xFF ||
            (mask == 1 && s_audio_ctx.current_mode == AUDIO_MODE_MUSIC_A2DP) ||
            (mask == 2 && s_audio_ctx.current_mode == AUDIO_MODE_CALL_HFP)) {

            if (s_audio_ctx.current_mode == AUDIO_MODE_MUSIC_A2DP) {
                audio_io_stop_mode(AUDIO_IO_MUSIC);
            } else if (s_audio_ctx.current_mode == AUDIO_MODE_CALL_HFP) {
                audio_io_stop_mode(AUDIO_IO_CALL);
            }

            s_audio_ctx.current_mode = AUDIO_MODE_IDLE;
            audio_pm_lock_release();
            post_mode_changed(AUDIO_MODE_IDLE, s_audio_ctx.current_rate);
        }
        break;
    }

    /* ------------------------------------------------------------------------
     * 4. REPRODUZIR TOM / SFX
     * ------------------------------------------------------------------------ */
    case AUDIO_CMD_PLAY_TONE: {
        const audio_cmd_play_tone_t *cmd_tone = (const audio_cmd_play_tone_t *)msg->data;
        if (cmd_tone) {
            ESP_LOGI(TAG, "[CMD] AUDIO_CMD_PLAY_TONE: tone_id=%u", cmd_tone->tone_id);
            /* O módulo sfx.h gerencia a sequência de frequências sintetizadas */
            sfx_play((sfx_id_t)cmd_tone->tone_id);
        }
        break;
    }

    /* ------------------------------------------------------------------------
     * 5. AJUSTAR VOLUME GERAL DO CODEC
     * ------------------------------------------------------------------------ */
    case AUDIO_CMD_SET_VOLUME: {
        const audio_cmd_set_volume_t *cmd_vol = (const audio_cmd_set_volume_t *)msg->data;
        uint8_t vol_pct = cmd_vol ? cmd_vol->volume_percent : 100;
        if (vol_pct > 100) {
            vol_pct = 100;
        }

        /* Converte escala percentual (0 a 100%) para escala do hardware WM8960 / AVRCP (0 a 127) */
        uint8_t avrcp_val = (uint8_t)((uint32_t)vol_pct * 127 / 100);

        ESP_LOGI(TAG, "[CMD] AUDIO_CMD_SET_VOLUME: %u%% (hardware=%u/127)", vol_pct, avrcp_val);

        esp_err_t err = audio_codec_set_volume(avrcp_val);
        if (err == ESP_OK) {
            s_audio_ctx.current_vol_pct = vol_pct;
            post_volume_changed(vol_pct, s_audio_ctx.is_muted);
        } else {
            ESP_LOGE(TAG, "Falha ao aplicar volume no hardware codec: %s", esp_err_to_name(err));
        }
        break;
    }

    /* ------------------------------------------------------------------------
     * 6. CONFIGURAR EQUALIZADOR
     * ------------------------------------------------------------------------ */
    case AUDIO_CMD_SET_EQ: {
        const audio_cmd_set_eq_t *cmd_eq = (const audio_cmd_set_eq_t *)msg->data;
        if (!cmd_eq) {
            break;
        }

        ESP_LOGI(TAG, "[CMD] AUDIO_CMD_SET_EQ: preset=%u", cmd_eq->preset_id);

        if (cmd_eq->preset_id == 0xFF) {
            /* Bandas customizadas: cada ganho em dB de -12 a +12 é convertido em passos de 0.5dB (-24 a +24) */
            int8_t raw_gains[EQ_NUM_BANDS];
            for (int i = 0; i < EQ_NUM_BANDS; i++) {
                int8_t db = cmd_eq->gains_db[i];
                if (db < -12) db = -12;
                if (db > 12)  db = 12;
                raw_gains[i] = (int8_t)(db * 2); /* 0.5 dB por passo */
            }
            eq_set_gains(raw_gains);
            s_audio_ctx.current_eq_preset = EQ_PRESET_CUSTOM;
            post_eq_changed(EQ_PRESET_CUSTOM, cmd_eq->gains_db);
        } else if (cmd_eq->preset_id < EQ_PRESET_COUNT) {
            eq_set_preset((eq_preset_t)cmd_eq->preset_id);
            s_audio_ctx.current_eq_preset = cmd_eq->preset_id;
            int8_t gains[EQ_NUM_BANDS];
            eq_get_gains(gains);
            int8_t gains_db[5];
            for (int i = 0; i < 5; i++) {
                gains_db[i] = gains[i] / 2;
            }
            post_eq_changed(cmd_eq->preset_id, gains_db);
        }
        break;
    }

    /* ------------------------------------------------------------------------
     * 7. MUTE / UNMUTE DO HARDWARE CODEC
     * ------------------------------------------------------------------------ */
    case AUDIO_CMD_SET_MUTE: {
        const audio_cmd_set_mute_t *cmd_mute = (const audio_cmd_set_mute_t *)msg->data;
        bool mute = cmd_mute ? cmd_mute->mute : false;

        ESP_LOGI(TAG, "[CMD] AUDIO_CMD_SET_MUTE: mute=%d", mute);

        esp_err_t err = audio_codec_mute(mute);
        if (err == ESP_OK) {
            s_audio_ctx.is_muted = mute;
            post_volume_changed(s_audio_ctx.current_vol_pct, mute);
        } else {
            ESP_LOGE(TAG, "Falha ao acionar mute no codec: %s", esp_err_to_name(err));
        }
        break;
    }

    default:
        ESP_LOGW(TAG, "Comando desconhecido recebido pelo ator de áudio: 0x%04X", msg->cmd);
        break;
    }
}

/* ============================================================================
 * CALLBACKS DE CICLO DE VIDA DO ACTOR AUDIO
 * ============================================================================ */

static void audio_actor_on_start(void *ctx)
{
    (void)ctx;
    ESP_LOGI(TAG, "Task do Actor Audio iniciada com sucesso (Core 1, Prio 10)");
}

static void audio_actor_on_stop(void *ctx)
{
    (void)ctx;
    ESP_LOGI(TAG, "Task do Actor Audio sendo finalizada; desligando I2S e Codec...");
    audio_io_stop_mode(AUDIO_IO_MUSIC);
    audio_io_stop_mode(AUDIO_IO_CALL);
    audio_codec_power_down();
    audio_pm_lock_release();
}

/* ============================================================================
 * INTERFACE PÚBLICA DE INICIALIZAÇÃO E CONTROLE
 * ============================================================================ */

esp_err_t audio_init(void)
{
    ESP_LOGI(TAG, "Inicializando subsistema de hardware de áudio...");

    /* 1. Inicializa o hardware físico: WM8960 Codec via I2C */
    ESP_RETURN_ON_ERROR(audio_codec_init(), TAG, "codec");

    /* 2. Inicializa o equalizador RBJ de software */
    ESP_RETURN_ON_ERROR(eq_init(), TAG, "eq");

    /* 3. Inicializa I2S DMA, canais e buffers de baixa latência */
    ESP_RETURN_ON_ERROR(audio_io_init(), TAG, "io");

#ifdef CONFIG_PM_ENABLE
    /* Inicializa o Power Management Lock para manter APB em frequência máxima (80 MHz) durante I2S */
    if (s_audio_ctx.pm_lock == NULL) {
        esp_err_t pm_err = esp_pm_lock_create(ESP_PM_APB_FREQ_MAX, 0, "audio_i2s", &s_audio_ctx.pm_lock);
        if (pm_err != ESP_OK) {
            ESP_LOGW(TAG, "Não foi possível criar esp_pm_lock para áudio: %s", esp_err_to_name(pm_err));
        } else {
            ESP_LOGI(TAG, "Power Management Lock (audio_i2s) registrado com sucesso");
        }
    }
#endif

    /* 4. Configuração e criação do Actor Audio (Core 1, Fixo).
     * Prio 8 (era 10): o controle NÃO pode preemptar o plano de dados
     * (audio_io, prio 18), senão comandos de volume/EQ causam micro-cortes. */
    const hs_actor_cfg_t actor_cfg = {
        .name      = "act_audio",
        .stack     = 4096,
        .prio      = 8,
        .core      = 1,
        .queue_len = 16,
        .idle_ms   = 0, /* Fixo: permanece ativo */
        .on_msg    = audio_actor_msg_handler,
        .on_start  = audio_actor_on_start,
        .on_stop   = audio_actor_on_stop,
        .ctx       = &s_audio_ctx,
    };

    esp_err_t err = hs_actor_create(&actor_cfg, &s_audio_ctx.actor);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Falha ao criar hs_actor para audio: %s", esp_err_to_name(err));
        return err;
    }

    ESP_LOGI(TAG, "Actor Audio criado com sucesso e pronto para receber comandos");
    return ESP_OK;
}

esp_err_t audio_deinit(void)
{
    if (!s_audio_ctx.actor) {
        return ESP_OK;
    }

    esp_err_t err = hs_actor_stop(s_audio_ctx.actor, pdMS_TO_TICKS(1000));
    hs_actor_destroy(s_audio_ctx.actor);
    s_audio_ctx.actor = NULL;

#ifdef CONFIG_PM_ENABLE
    if (s_audio_ctx.pm_lock) {
        audio_pm_lock_release();
        esp_pm_lock_delete(s_audio_ctx.pm_lock);
        s_audio_ctx.pm_lock = NULL;
    }
#endif

    return err;
}

hs_actor_t *audio_actor_get(void)
{
    return s_audio_ctx.actor;
}

/* ============================================================================
 * HELPERS CONVENIENTES PARA ENVIO ASSÍNCRONO DE COMANDOS
 * ============================================================================ */

esp_err_t audio_cmd_start_music_send(uint32_t sample_rate)
{
    if (!s_audio_ctx.actor) return ESP_ERR_INVALID_STATE;
    audio_cmd_start_music_t payload = {
        .sample_rate     = sample_rate,
        .channels        = 2,
        .bits_per_sample = 16,
    };
    return hs_actor_send(s_audio_ctx.actor, AUDIO_CMD_START_MUSIC, &payload, sizeof(payload), pdMS_TO_TICKS(50));
}

esp_err_t audio_cmd_start_call_send(uint32_t sample_rate, bool enable_nr)
{
    if (!s_audio_ctx.actor) return ESP_ERR_INVALID_STATE;
    audio_cmd_start_call_t payload = {
        .sample_rate = sample_rate,
        .enable_nr   = enable_nr,
    };
    return hs_actor_send(s_audio_ctx.actor, AUDIO_CMD_START_CALL, &payload, sizeof(payload), pdMS_TO_TICKS(50));
}

esp_err_t audio_cmd_stop_send(uint8_t mode_mask)
{
    if (!s_audio_ctx.actor) return ESP_ERR_INVALID_STATE;
    audio_cmd_stop_t payload = {
        .mode_mask = mode_mask,
    };
    return hs_actor_send(s_audio_ctx.actor, AUDIO_CMD_STOP, &payload, sizeof(payload), pdMS_TO_TICKS(50));
}

esp_err_t audio_cmd_set_volume_send(uint8_t volume_percent)
{
    if (!s_audio_ctx.actor) return ESP_ERR_INVALID_STATE;
    audio_cmd_set_volume_t payload = {
        .volume_percent = volume_percent,
    };
    return hs_actor_send(s_audio_ctx.actor, AUDIO_CMD_SET_VOLUME, &payload, sizeof(payload), pdMS_TO_TICKS(50));
}

esp_err_t audio_cmd_play_tone_send(uint16_t tone_id, bool interrupt_current)
{
    if (!s_audio_ctx.actor) return ESP_ERR_INVALID_STATE;
    audio_cmd_play_tone_t payload = {
        .tone_id           = tone_id,
        .interrupt_current = interrupt_current,
    };
    return hs_actor_send(s_audio_ctx.actor, AUDIO_CMD_PLAY_TONE, &payload, sizeof(payload), pdMS_TO_TICKS(50));
}

esp_err_t audio_cmd_set_mute_send(bool mute)
{
    if (!s_audio_ctx.actor) return ESP_ERR_INVALID_STATE;
    audio_cmd_set_mute_t payload = {
        .mute = mute,
    };
    return hs_actor_send(s_audio_ctx.actor, AUDIO_CMD_SET_MUTE, &payload, sizeof(payload), pdMS_TO_TICKS(50));
}

esp_err_t audio_cmd_set_eq_send(uint8_t preset_id, const int8_t gains_db[5])
{
    if (!s_audio_ctx.actor) return ESP_ERR_INVALID_STATE;
    audio_cmd_set_eq_t payload = {
        .preset_id = preset_id,
    };
    if (gains_db) {
        memcpy(payload.gains_db, gains_db, sizeof(payload.gains_db));
    } else {
        memset(payload.gains_db, 0, sizeof(payload.gains_db));
    }
    return hs_actor_send(s_audio_ctx.actor, AUDIO_CMD_SET_EQ, &payload, sizeof(payload), pdMS_TO_TICKS(50));
}

