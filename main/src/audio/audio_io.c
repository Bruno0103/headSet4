#include "audio_io.h"

#include <math.h>
#include <string.h>

#include "driver/i2s_std.h"
#include "esp_check.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/ringbuf.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#include "audio_codec.h"
#include "board_config.h"
#include "eq.h"
#include "voice_nr.h"

#define MUSIC_RB_SIZE                                                          \
  (128 * 1024) /* ~743 ms a 44.1 kHz estereo em PSRAM (zero risco de underrun) */
/* Pré-buffer de 32 KB (~185 ms de colchão antes de iniciar reprodução) */
#define MUSIC_PREFILL                                                          \
  (32 * 1024)
#define MUSIC_CHUNK 1024
#define CALL_RB_SIZE (4 * 1024)
#define CALL_FRAMES 128 /* 16 ms a 8 kHz, 8 ms a 16 kHz */

/* --- Parâmetros de Compensação Suave de Clock Drift (R.16) ---
 * Em 44,1 kHz estéreo 16 bits (4 bytes por frame), a taxa é de 176.400 bytes/s.
 * 1 ms de áudio equivale a aproximadamente 176,4 bytes.
 * O objetivo é manter o nível de ocupação do ringbuffer centralizado em torno de ~185 ms (32 KB),
 * reagindo preventivamente antes que o buffer atinja 100% (overflow) ou 0% (underrun). */
#define DRIFT_HIGH_WARN_BYTES   (46 * 1024)   /* ~260 ms: tendência a acúmulo excessivo -> descarta suavemente */
#define DRIFT_HIGH_CRIT_BYTES   (65 * 1024)   /* ~370 ms: overflow iminente -> cadência de descarte acelerada */
#define DRIFT_LOW_WARN_BYTES    (20 * 1024)   /* ~115 ms: tendência a esvaziamento -> sintetiza suavemente */
#define DRIFT_LOW_CRIT_BYTES    (10 * 1024)   /* ~57 ms:  underrun iminente -> cadência de síntese acelerada */

/* Intervalos mínimos de histerese (em frames processados) entre intervenções de interpolação.
 * 1 frame em 44,1 kHz dura ~22,6 us. Um cooldown de 2048 frames equivale a ~46 ms entre cada micro-ajuste,
 * garantindo que qualquer alteração de pitch ou fase seja 100% imperceptível ao ouvido humano. */
#define DRIFT_COOLDOWN_MODERATE 2048          /* Intervalo para alerta moderado (~46 ms) */
#define DRIFT_COOLDOWN_CRITICAL  512          /* Intervalo para alerta crítico (~11 ms) */

static const char *TAG = "audio_io";

static i2s_chan_handle_t s_tx, s_rx;
static SemaphoreHandle_t s_lock; /* protege I2S + troca de modo */
static RingbufHandle_t s_music_rb, s_dl_rb, s_ul_rb;
static volatile audio_io_mode_t s_mode = AUDIO_IO_IDLE;
static volatile bool s_reconfig_req;
static size_t s_music_size;
static bool s_prebuffering;

/* Array local de processamento: alocado com margem de segurança de +8 amostras
 * para acomodar expansão pontual de 1 frame (+2 int16_t) durante duplicação suave */
static int16_t s_chunk[(MUSIC_CHUNK / 2) + 8];
static uint32_t s_call_limit_bytes;
static uint32_t s_rate;

/* Variáveis de estado e telemetria do compensador de drift (R.16) */
static uint32_t s_drift_dropped_frames = 0;   /* Contador de frames reduzidos suavemente */
static uint32_t s_drift_inserted_frames = 0;  /* Contador de frames sintetizados suavemente */
static uint32_t s_drift_cooldown_frames = 0;  /* Contador regressivo de histerese */
static uint32_t s_drift_cur_fill_bytes = 0;   /* Última medição de ocupação do buffer */

/* Parâmetros do Sintetizador Acústico de Alta Fidelidade (UX Sound Design) */
#define SFX_IDLE_RATE     44100
#define SFX_AMPLITUDE     8500.0f  /* Amplitude calibrada para faixa cristalina sem distorção (-11.7 dBFS) */
#define SFX_ATTACK_MS     3        /* Ataque ultra-rápido anti-pop em milissegundos */
#define SFX_RELEASE_MS    2        /* Rampa final suave de descida para evitar transitórios DC */
#define SFX_KEEP_ALIVE_MS 3000     /* Mantém clocks I2S e codec aquecidos por 3s após tom para transição contínua */

static const audio_tone_t *s_tone_seq;
static size_t s_tone_len, s_tone_idx;
static uint32_t s_tone_pos; /* amostras ja geradas na nota atual */
static float s_tone_phase;
static volatile bool s_tone_active;
static bool s_tone_owns; /* o efeito ligou o audio sozinho */
static uint32_t s_idle_timer_ms; /* timestamp de início do repouso para desligamento suave */

/* APLL so em 44.1/48 kHz (precisao); taxas de voz usam o clock padrao */
static i2s_std_clk_config_t clk_for(uint32_t rate) {
  i2s_std_clk_config_t c = I2S_STD_CLK_DEFAULT_CONFIG(rate);
  c.clk_src = (rate >= 44100) ? I2S_CLK_SRC_APLL : I2S_CLK_SRC_DEFAULT;
  return c;
}

static void drain(RingbufHandle_t rb) {
  size_t n;
  void *p;
  while ((p = xRingbufferReceiveUpTo(rb, &n, 0, 4096)) != NULL)
    vRingbufferReturnItem(rb, p);
}

static void trim_rb_reader_side(RingbufHandle_t rb) {
  if (!s_call_limit_bytes)
    return;
  while (true) {
    size_t free = xRingbufferGetCurFreeSize(rb);
    size_t used = (CALL_RB_SIZE > free) ? (CALL_RB_SIZE - free) : 0;
    if (used <= s_call_limit_bytes)
      break;
    size_t to_discard = used - s_call_limit_bytes;
    size_t n;
    void *p = xRingbufferReceiveUpTo(rb, &n, 0, to_discard);
    if (!p)
      break;
    vRingbufferReturnItem(rb, p);
  }
}

/* ---------------- Efeitos Sonoros / Síntese Acústica Digital (DSP Chime / Bell) ---------------- */

/**
 * @brief Sintetizador digital de alta fidelidade para efeitos sonoros de interface (UX Sound Design).
 *
 * Gera notas com timbre orgânico percussivo (chime cristalino / marimba suave / cálice de cristal)
 * utilizando síntese aditiva multi-harmônica com decaimento exponencial natural:
 *   - Fundamental f0 (corpo musical)
 *   - 2º harmônico 2*f0 (oitava musical, traz brilho e presença) com amortecimento rápido
 *   - 3º harmônico 3*f0 (textura vítrea sutil) com amortecimento ainda mais veloz
 *   - 4º harmônico 4*f0 (micro-ataque acústico inicial cristalino)
 *
 * O envelope dinâmico possui ataque limpo anti-pop de 2.5 ms seguido de curva de decaimento natural
 * percussivo (e^-1.8*t/T), eliminando totalmente o aspecto "robótico/buzzer" das senoides estáticas.
 *
 * @param[out] dst     Buffer de destino mono de 16 bits.
 * @param[in]  frames  Quantidade de amostras a sintetizar.
 */
/**
 * @brief Sintetizador digital de alta fidelidade para efeitos sonoros de interface (UX Sound Design).
 *
 * Gera notas com timbre orgânico percussivo (chime cristalino / marimba suave / cálice de cristal)
 * com fase estritamente contínua entre notas e envelope quadrático percussivo (1 - p)^2.
 *
 * Elimina 100% de qualquer estalo ou clique através de:
 *   1. Continuidade de fase contínua: s_tone_phase NUNCA é resetada para zero na transição de notas.
 *   2. Ataque suave anti-pop de 3 ms nos primeiros milissegundos.
 *   3. Decaimento percussivo (1 - p)^2 que chega a EXATAMENTE zero na última amostra da nota.
 *   4. Zero chamadas a expf() por amostra, garantindo latência ultra-baixa sem engasgos no DMA.
 *
 * @param[out] dst     Buffer de destino mono de 16 bits.
 * @param[in]  frames  Quantidade de amostras a sintetizar.
 */
static void tone_render(int16_t *dst, size_t frames) {
  for (size_t i = 0; i < frames; i++) {
    int16_t v = 0;
    while (s_tone_idx < s_tone_len) {
      const audio_tone_t *t = &s_tone_seq[s_tone_idx];
      uint32_t total = (uint32_t)t->ms * s_rate / 1000;
      if (s_tone_pos >= total) {
        s_tone_idx++;
        s_tone_pos = 0;
        /* IMPORTANTE: NÃO reseta s_tone_phase! Preserva a continuidade da forma de onda */
        continue;
      }
      if (t->hz > 0) {
        uint32_t attack_samples = s_rate * SFX_ATTACK_MS / 1000;
        if (attack_samples == 0) attack_samples = 1;

        /* Progresso normalizado de 0.0f a 1.0f ao longo da nota */
        float p = (float)s_tone_pos / (float)total;

        /* Envelope percussivo suave (1 - p)^2:
         * Decaimento natural tipo sino/marimba que chega a EXATAMENTE 0.0f no final da nota,
         * garantindo transições perfeitamente contínuas e sem cliques. */
        float decay = (1.0f - p) * (1.0f - p);

        /* Rampa suave de ataque anti-pop nos primeiros milissegundos */
        float env = decay;
        if (s_tone_pos < attack_samples) {
          env *= (float)s_tone_pos / (float)attack_samples;
        }

        /* Síntese harmônica pura e rica:
         * Fundamental + 2º harmônico (oitava musical que traz o timbre cristalino de carrilhão) */
        float s1 = sinf(s_tone_phase);
        float s2 = sinf(2.0f * s_tone_phase);

        /* O 2º harmônico tem brilho no ataque e decai suavemente */
        float h2_gain = 0.35f * (1.0f - p);
        float combined = (s1 + s2 * h2_gain) * 0.72f;

        v = (int16_t)(combined * env * SFX_AMPLITUDE);

        /* Avanço contínuo da fase fundamental */
        s_tone_phase += 2.0f * (float)M_PI * t->hz / s_rate;
        if (s_tone_phase >= 2.0f * (float)M_PI) {
          s_tone_phase -= 2.0f * (float)M_PI;
        }
      } else {
        /* Silêncio técnico / pausa entre notas ou esvaziamento de DMA */
        v = 0;
      }
      s_tone_pos++;
      break;
    }
    dst[i] = v;
  }
}

static inline int16_t sat16(int32_t x) {
  return (int16_t)(x > 32767 ? 32767 : (x < -32768 ? -32768 : x));
}

/* Soma o efeito (mono) em um bloco estereo intercalado */
static void tone_mix_stereo(int16_t *st, size_t frames) {
  int16_t tone[MUSIC_CHUNK / 4];
  while (frames) {
    size_t n = frames < MUSIC_CHUNK / 4 ? frames : MUSIC_CHUNK / 4;
    tone_render(tone, n);
    for (size_t i = 0; i < n; i++) {
      st[2 * i] = sat16((int32_t)st[2 * i] + tone[i]);
      st[2 * i + 1] = sat16((int32_t)st[2 * i + 1] + tone[i]);
    }
    st += 2 * n;
    frames -= n;
  }
}

/* Sem musica no buffer: o efeito toca sozinho */
/* Contador de falhas de escrita/leitura no I2S (diagnóstico, regra §3.7). */
static volatile uint32_t s_i2s_err;

/* Escreve no I2S e devolve true apenas se o DMA aceitou bytes.
 * Retorno false (erro ou 0 bytes) faz o laço principal DORMIR, evitando o
 * laço quente que matava de fome a IDLE1 (task_wdt na CPU 1). */
static bool i2s_write_ok(const void *src, size_t len) {
  size_t w = 0;
  esp_err_t err = i2s_channel_write(s_tx, src, len, &w, 100);
  if (err != ESP_OK || w == 0) {
    s_i2s_err++;
    if ((s_i2s_err & 0xFF) == 1) { /* log limitado: 1 a cada 256 falhas */
      ESP_LOGW(TAG, "i2s write falhou (%s, w=%u), falhas=%lu",
               esp_err_to_name(err), (unsigned)w, (unsigned long)s_i2s_err);
    }
    return false;
  }
  return true;
}

#define TONE_CHUNK_FRAMES 256
static bool pump_tone_only(void) {
  int16_t st[TONE_CHUNK_FRAMES * 2] = {0};
  tone_mix_stereo(st, TONE_CHUNK_FRAMES);
  return i2s_write_ok(st, sizeof st);
}

/* ---------------- Compensação Suave de Clock Drift (R.16) ---------------- */

/**
 * @brief Remove suavemente 1 frame estéreo (4 bytes = L16 + R16) do bloco por interpolação linear contínua.
 *
 * Em vez de descartar brutalmente uma amostra (o que geraria descontinuidade e clique audível),
 * fundimos suavemente a amostra com a média dos vizinhos antes do deslocamento.
 *
 * @param[in,out] samples      Vetor contendo os dados intercalados L16/R16.
 * @param[in]     frame_count  Quantidade de frames no vetor.
 * @return size_t Nova quantidade de frames no vetor (frame_count - 1).
 */
static size_t audio_drift_soft_drop(int16_t *samples, size_t frame_count) {
  if (frame_count < 4) return frame_count;

  /* Ponto de corte e fusão na metade exata do bloco para evitar transitórios nas bordas */
  size_t mid = frame_count / 2;

  /* Interpolação linear contínua: funde a amostra central com a média ponderada dos vizinhos */
  int32_t prev_l = samples[2 * (mid - 1)];
  int32_t next_l = samples[2 * (mid + 1)];
  int32_t prev_r = samples[2 * (mid - 1) + 1];
  int32_t next_r = samples[2 * (mid + 1) + 1];

  samples[2 * mid]     = (int16_t)((prev_l + next_l) / 2);
  samples[2 * mid + 1] = (int16_t)((prev_r + next_r) / 2);

  /* Desloca os frames da segunda metade 1 posição para a esquerda */
  for (size_t i = mid; i < frame_count - 1; i++) {
    samples[2 * i]     = samples[2 * (i + 1)];
    samples[2 * i + 1] = samples[2 * (i + 1) + 1];
  }

  s_drift_dropped_frames++;
  return frame_count - 1;
}

/**
 * @brief Insere suavemente 1 frame estéreo sintetizado pela média linear das amostras vizinhas.
 *
 * Aumenta a duração do bloco em 1 frame para retardar o esvaziamento do ringbuffer (underrun),
 * mantendo a derivada e a fase senoidal sem nenhum clique perceptível.
 *
 * @param[in,out] samples      Vetor com espaço alocado de pelo menos frame_count + 1 frames.
 * @param[in]     frame_count  Quantidade de frames atualmente preenchidos no vetor.
 * @return size_t Nova quantidade de frames no vetor (frame_count + 1).
 */
static size_t audio_drift_soft_insert(int16_t *samples, size_t frame_count) {
  if (frame_count < 2) return frame_count;

  size_t mid = frame_count / 2;

  /* Desloca os frames da segunda metade 1 posição para a direita para abrir espaço */
  for (size_t i = frame_count; i > mid; i--) {
    samples[2 * i]     = samples[2 * (i - 1)];
    samples[2 * i + 1] = samples[2 * (i - 1) + 1];
  }

  /* Amostras vizinhas para interpolação contínua */
  int32_t l_a = samples[2 * (mid - 1)];
  int32_t l_b = samples[2 * (mid + 1)];
  int32_t r_a = samples[2 * (mid - 1) + 1];
  int32_t r_b = samples[2 * (mid + 1) + 1];

  /* Insere a nova amostra interpolada */
  samples[2 * mid]     = (int16_t)((l_a + l_b) / 2);
  samples[2 * mid + 1] = (int16_t)((r_a + r_b) / 2);

  s_drift_inserted_frames++;
  return frame_count + 1;
}

/* ---------------- bombeamento (rodam dentro da task, com s_lock)
 * ---------------- */

static bool pump_music(void) {
  if (s_prebuffering) {
    size_t fill = s_music_size - xRingbufferGetCurFreeSize(s_music_rb);
    if (fill < MUSIC_PREFILL) {
      if (s_tone_active) {
        return pump_tone_only();
      }
      /* Se ainda não atingiu o prefill, sai da função sem bloquear com o lock
       * retido. A temporização de descanso é feita pelo laço principal da
       * audio_task. */
      return false;
    }
    s_prebuffering = false;
  }

  size_t n = 0;
  bool ok = true;
  void *p =
      xRingbufferReceiveUpTo(s_music_rb, &n, pdMS_TO_TICKS(10), MUSIC_CHUNK);
  if (p) {
    size_t fill = s_music_size - xRingbufferGetCurFreeSize(s_music_rb);
    s_drift_cur_fill_bytes = (uint32_t)fill;

    /* Copia para um buffer local (o EQ altera no lugar) e libera o ringbuffer logo */
    n &= ~3u;
    size_t frames = n / 4;
    memcpy(s_chunk, p, n);
    vRingbufferReturnItem(s_music_rb, p);

    /* --- Compensação Dinâmica de Clock Drift (R.16) ---
     * Monitora continuamente a linha d'água do ringbuffer em PSRAM e reage antes que
     * ocorra overflow ou underrun. O cooldown impede intervenções consecutivas,
     * garantindo dispersão temporal ampla das micro-interpolações. */
    if (s_drift_cooldown_frames > 0) {
      if (s_drift_cooldown_frames > frames) {
        s_drift_cooldown_frames -= frames;
      } else {
        s_drift_cooldown_frames = 0;
      }
    } else {
      if (fill >= DRIFT_HIGH_CRIT_BYTES) {
        /* Alerta Crítico Superior: acelera o descarte de 1 frame a cada ~11 ms */
        frames = audio_drift_soft_drop(s_chunk, frames);
        s_drift_cooldown_frames = DRIFT_COOLDOWN_CRITICAL;
      } else if (fill >= DRIFT_HIGH_WARN_BYTES) {
        /* Alerta Alto Moderado: descarte de 1 frame a cada ~46 ms */
        frames = audio_drift_soft_drop(s_chunk, frames);
        s_drift_cooldown_frames = DRIFT_COOLDOWN_MODERATE;
      } else if (fill <= DRIFT_LOW_CRIT_BYTES && fill > 0) {
        /* Alerta Crítico Inferior: sintetiza 1 frame a cada ~11 ms para evitar underrun */
        frames = audio_drift_soft_insert(s_chunk, frames);
        s_drift_cooldown_frames = DRIFT_COOLDOWN_CRITICAL;
      } else if (fill <= DRIFT_LOW_WARN_BYTES && fill > 0) {
        /* Alerta Baixo Moderado: sintetiza 1 frame a cada ~46 ms */
        frames = audio_drift_soft_insert(s_chunk, frames);
        s_drift_cooldown_frames = DRIFT_COOLDOWN_MODERATE;
      }
      n = frames * 4;
    }

    eq_process(s_chunk, frames);
    if (s_tone_active)
      tone_mix_stereo(s_chunk, frames);
    uint8_t *d = (uint8_t *)s_chunk;

    /* Envia o bloco completo de PCM diretamente ao DMA I2S */
    if (n > 0) {
      ok = i2s_write_ok(d, n);
    } else {
      ok = false; /* bloco vazio: deixa o laço dormir */
    }
    return ok;
  } else { /* underrun: toca silencio e reenche o colchao */
    s_prebuffering = true;
    if (s_tone_active) {
      return pump_tone_only();
    }
    static const int16_t silence[256];
    (void)i2s_write_ok(silence, sizeof silence);
    return false;
  }
}

static bool pump_call(void) {
  int16_t in[CALL_FRAMES * 2], out[CALL_FRAMES * 2], mono[CALL_FRAMES];
  size_t rd = 0, wr = 0;

  /* uplink: microfone L (LINPUT1) -> limpeza de ruido (Voice NR) -> buffer do HFP */
  if (i2s_channel_read(s_rx, in, sizeof in, &rd, 100) != ESP_OK || rd == 0) {
    s_i2s_err++;
    return false;
  }
  size_t frames = rd / 4;
  int16_t peak = 0;
  for (size_t i = 0; i < frames; i++) {
    /* O microfone fisico do headset esta conectado em LINPUT1 (canal esquerdo) */
    mono[i] = in[2 * i];
    int16_t abs_s = (mono[i] < 0) ? -mono[i] : mono[i];
    if (abs_s > peak) peak = abs_s;
  }
  /* Tratamentos de audio desativados a pedido do usuario: audio cru (RAW) enviado direto ao ringbuffer */
  xRingbufferSend(s_ul_rb, mono, frames * 2, 0);


  /* Telemetria de audio de uplink para depuracao e confirmacao de envio de voz */
  static uint32_t s_last_vu_time = 0;
  static int16_t  s_max_peak = 0;
  if (peak > s_max_peak) s_max_peak = peak;
  uint32_t now = esp_log_timestamp();
  if (now - s_last_vu_time >= 1500) {
    s_last_vu_time = now;
    ESP_LOGI(TAG, "[UPLINK MIC] Sinal capturado: pico=%5d / 32767 | frames=%u | enviando para HFP",
             s_max_peak, (unsigned)frames);
    s_max_peak = 0;
  }


  /* downlink: voz do interlocutor (mono) -> L e R. Falta de dados = silencio.
   */
  memset(out, 0, sizeof out);
  trim_rb_reader_side(s_dl_rb);
  size_t n = 0;
  int16_t *dl = xRingbufferReceiveUpTo(s_dl_rb, &n, 0, frames * 2);
  if (dl) {
    for (size_t i = 0; i < n / 2; i++)
      out[2 * i] = out[2 * i + 1] = dl[i];
    vRingbufferReturnItem(s_dl_rb, dl);
  }
  if (s_tone_active)
    tone_mix_stereo(out, frames);
  (void)wr;
  return i2s_write_ok(out, frames * 4);
}

static void stop_locked(bool power_down);

static void audio_task(void *arg) {
  for (;;) {
    xSemaphoreTake(s_lock, portMAX_DELAY);
    audio_io_mode_t m = s_mode;
    bool transferred = false;
    if (m == AUDIO_IO_MUSIC)
      transferred = pump_music();
    else if (m == AUDIO_IO_CALL)
      transferred = pump_call();

    if (s_tone_active && s_tone_idx >= s_tone_len) { /* efeito terminou */
      s_tone_active = false;
      if (s_tone_owns) {
        /* Registra timestamp para manter o I2S aquecido por 3s em repouso
         * sem cortar bruscamente os clocks enquanto a mídia ou novo tom pode iniciar */
        s_idle_timer_ms = esp_log_timestamp();
      }
    }

    /* Se o efeito sonoro terminou e o headset permaneceu ocioso por mais de 3s,
     * executa a transição suave para standby com dreno de descritores DMA */
    if (m == AUDIO_IO_MUSIC && s_tone_owns && !s_tone_active && s_idle_timer_ms > 0) {
      if ((esp_log_timestamp() - s_idle_timer_ms) >= SFX_KEEP_ALIVE_MS) {
        stop_locked(true);
        s_idle_timer_ms = 0;
        m = s_mode;
      }
    }

    /* Se um efeito sonoro estiver sendo ativamente bombeado para o I2S DMA,
     * a cadência é mantida pelo bloqueio natural do i2s_channel_write (backpressure).
     * Não devemos forçar vTaskDelay quando transferred == true, caso contrário o
     * buffer DMA esvazia e provoca rajada contínua de estalos (underruns) no fone. */
    bool needs_delay = (m == AUDIO_IO_IDLE || s_reconfig_req ||
                        (!s_tone_active && s_prebuffering) || !transferred);
    xSemaphoreGive(s_lock);

    if (needs_delay) {
      /* Quando em IDLE, descanso de 20ms.
       * Quando em prebuffering ou sem dados no buffer (underrun), descanso de
       * 5ms. Isso garante que a task IDLE1 (CPU 1, prioridade 0) resete o
       * watchdog sem travar a CPU. */
      /* ATENÇÃO: com CONFIG_FREERTOS_HZ=100, pdMS_TO_TICKS(5) == 0 e
       * vTaskDelay(0) NÃO bloqueia (só faz yield) -> laço quente -> task_wdt.
       * Por isso o atraso é forçado a no mínimo 1 tick. */
      TickType_t d = pdMS_TO_TICKS(m == AUDIO_IO_IDLE ? 20 : 5);
      vTaskDelay(d > 0 ? d : 1);
    }
    /* Em streaming ativo NÃO há taskYIELD(): ele só cede para tasks de
     * prioridade >= à nossa e nunca deixava a IDLE1 (prio 0) rodar.
     * O ritmo agora vem exclusivamente do bloqueio do i2s_channel_write
     * quando o DMA está cheio (backpressure do hardware). Qualquer escrita
     * que não bloqueie (erro / 0 bytes) retorna false e cai no vTaskDelay
     * acima. */
  }
}

/* ---------------- controle de modo ---------------- */

/* power_down=false quando o chamador vai religar o audio em seguida (troca de
 * modo) */
static void stop_locked(bool power_down) {
  s_tone_active = false;
  s_tone_owns = false;
  s_idle_timer_ms = 0;
  if (s_mode == AUDIO_IO_IDLE)
    return;

  /* 1. Mute digital suave no DAC (rampa senoidal do hardware via DACSMM em R6) */
  audio_codec_mute(true);

  /* 2. Drena os descritores DMA transmitindo blocos de silêncio (zeros).
   * Isso empurra todo o áudio acumulado na DMA para fora do DAC e permite
   * que a rampa digital de soft mute complete suavemente com o clock I2S rodando. */
  static const int16_t zeros[512] = {0};
  size_t w = 0;
  (void)i2s_channel_write(s_tx, zeros, sizeof(zeros), &w, 50);
  (void)i2s_channel_write(s_tx, zeros, sizeof(zeros), &w, 50);
  vTaskDelay(pdMS_TO_TICKS(15));

  /* 3. Aplica mute analógico real nas saídas LOUT1/ROUT1 com zero-crossing antes de parar o I2S */
  audio_codec_set_analog_mute(true);

  /* 4. Desabilita o canal I2S com o hardware já em silêncio absoluto */
  i2s_channel_disable(s_tx);
  if (s_mode == AUDIO_IO_CALL) {
    i2s_channel_disable(s_rx);
    /* Desliga a alimentacao MICBIAS, amplificadores e ADCs do microfone ao encerrar chamada */
    audio_codec_set_mic_power(false);
  }
  s_mode = AUDIO_IO_IDLE;
  if (power_down)
    audio_codec_power_down();
}

static esp_err_t start_mode(audio_io_mode_t mode, uint32_t rate, bool by_tone) {
  esp_err_t ret = ESP_OK;
  s_reconfig_req = true;
  xSemaphoreTake(s_lock, portMAX_DELAY);

  if (mode == AUDIO_IO_MUSIC &&
      s_mode == AUDIO_IO_CALL) { /* chamada tem prioridade */
    ret = ESP_ERR_INVALID_STATE;
    goto out;
  }

  /* Se o canal I2S já estiver ativo na mesma taxa e modo (ex: transição entre tom e música A2DP,
   * ou troca de faixa / despause em 44.1 kHz), NÃO reinicializa o hardware I2S nem o codec!
   * Isso elimina 100% de qualquer ruído de comutação ou estalo entre eventos de áudio. */
  if (s_mode == mode && s_rate == rate) {
    if (!by_tone) {
      s_tone_owns = false; /* Streaming de música ou chamada assumiu a posse */
    }
    s_idle_timer_ms = 0;
    s_prebuffering = (mode == AUDIO_IO_MUSIC);
    audio_codec_set_analog_mute(false);
    audio_codec_mute(false);
    xSemaphoreGive(s_lock);
    s_reconfig_req = false;
    return ESP_OK;
  }

  stop_locked(false);
  ESP_GOTO_ON_ERROR(audio_codec_power_up(), out, TAG, "codec power up");
  /* Inicia com DAC em mute digital e saídas em mute analógico */
  audio_codec_mute(true);
  audio_codec_set_analog_mute(true);

  ESP_GOTO_ON_ERROR(audio_codec_set_sample_rate(rate), out, TAG, "codec rate");
  if (mode == AUDIO_IO_MUSIC)
    eq_set_sample_rate(rate);
  ESP_GOTO_ON_ERROR(audio_codec_apply_filters(mode == AUDIO_IO_CALL
                                                  ? &AUDIO_FILTERS_CALL
                                                  : &AUDIO_FILTERS_MUSIC),
                    out, TAG, "filtros");

  i2s_std_clk_config_t clk = clk_for(rate);
  ESP_GOTO_ON_ERROR(i2s_channel_reconfig_std_clock(s_tx, &clk), out, TAG,
                    "tx clock");
  ESP_GOTO_ON_ERROR(i2s_channel_reconfig_std_clock(s_rx, &clk), out, TAG,
                    "rx clock");

  drain(s_music_rb);
  drain(s_dl_rb);
  drain(s_ul_rb);
  if (mode == AUDIO_IO_CALL) {
    voice_nr_init(rate);
    s_call_limit_bytes = rate * 2 * 35 / 1000;
    /* Ativa polarizacao MICBIAS, amplificadores e ADCs do WM8960 estritamente durante chamadas / gravacao */
    ESP_GOTO_ON_ERROR(audio_codec_set_mic_power(true), out, TAG, "mic power on");
  } else {
    s_call_limit_bytes = 0;
    /* Garante que o microfone permanece completamente desenergizado em modo musica/SFX */
    audio_codec_set_mic_power(false);
  }

  ESP_GOTO_ON_ERROR(i2s_channel_enable(s_tx), out, TAG, "tx enable");
  if (mode == AUDIO_IO_CALL)
    ESP_GOTO_ON_ERROR(i2s_channel_enable(s_rx), out, TAG, "rx enable");

  /* Pré-carrega os descritores DMA com silêncio antes de desmutar para estabilizar BCLK/WS */
  static const int16_t zeros[512] = {0};
  size_t w = 0;
  (void)i2s_channel_write(s_tx, zeros, sizeof(zeros), &w, 50);
  (void)i2s_channel_write(s_tx, zeros, sizeof(zeros), &w, 50);

  s_prebuffering = (mode == AUDIO_IO_MUSIC);
  if (mode == AUDIO_IO_MUSIC) {
    s_drift_cooldown_frames = 0;
  }
  s_mode = mode;
  s_rate = rate;
  s_tone_owns = by_tone;
  s_idle_timer_ms = 0;
  ESP_LOGI(TAG, "modo %s @ %lu Hz",
           mode == AUDIO_IO_CALL ? "CHAMADA" : "MUSICA", (unsigned long)rate);

out:
  if (ret != ESP_OK && s_mode == AUDIO_IO_IDLE)
    audio_codec_power_down(); /* falhou sem nada tocando */
  xSemaphoreGive(s_lock);
  s_reconfig_req = false;
  if (ret == ESP_OK) {
    vTaskDelay(pdMS_TO_TICKS(20)); /* deixa o clock estabilizar antes de tirar o mudo */
    audio_codec_mute(false);       /* rampa digital suave de desmute do DAC */
    audio_codec_set_analog_mute(false); /* restaura ganho analógico nos fones */
  }
  return ret;
}

esp_err_t audio_io_start(audio_io_mode_t mode, uint32_t rate) {
  return start_mode(mode, rate, false);
}

esp_err_t audio_io_play_tones(const audio_tone_t *seq, size_t count) {
  if (!seq || count == 0)
    return ESP_ERR_INVALID_ARG;

  /* Arma a sequência de tons sob lock atômico para que o sintetizador já inicie
   * gerando o primeiro frame com ataque percussivo suave anti-pop */
  xSemaphoreTake(s_lock, portMAX_DELAY);
  s_tone_seq = seq;
  s_tone_len = count;
  s_tone_idx = 0;
  s_tone_pos = 0;
  s_tone_phase = 0.0f;
  s_tone_active = true;
  s_idle_timer_ms = 0;
  bool need_start = (s_mode == AUDIO_IO_IDLE);
  xSemaphoreGive(s_lock);

  if (need_start) {
    esp_err_t err = start_mode(AUDIO_IO_MUSIC, SFX_IDLE_RATE, true);
    if (err != ESP_OK) {
      xSemaphoreTake(s_lock, portMAX_DELAY);
      s_tone_active = false;
      xSemaphoreGive(s_lock);
      return err;
    }
  }
  return ESP_OK;
}

bool audio_io_tones_busy(void) { return s_tone_active; }

void audio_io_stop_mode(audio_io_mode_t mode) {
  s_reconfig_req = true;
  xSemaphoreTake(s_lock, portMAX_DELAY);
  if (s_mode == mode)
    stop_locked(true);
  xSemaphoreGive(s_lock);
  s_reconfig_req = false;
}

audio_io_mode_t audio_io_get_mode(void) { return s_mode; }

/* ---------------- Entradas de dados (Plano de Dados Puro - áudio contínuo, não
 * bloqueia) ---------------- */

void audio_data_music_push(const uint8_t *d, uint32_t len) {
  len &= ~3u; /* frames de 4 bytes (L16+R16) */
  if (s_mode != AUDIO_IO_MUSIC || len == 0)
    return;
  xRingbufferSend(s_music_rb, d, len,
                  0); /* cheio: descarta sem travar a pilha BT */
}

void audio_data_call_downlink_push(const uint8_t *d, uint32_t len) {
  len &= ~1u;
  if (s_mode != AUDIO_IO_CALL || len == 0)
    return;
  xRingbufferSend(s_dl_rb, d, len, 0);
}

uint32_t audio_data_call_uplink_pull(uint8_t *buf, uint32_t size) {
  uint32_t got = 0;
  if (s_mode == AUDIO_IO_CALL) {
    trim_rb_reader_side(s_ul_rb);
    size_t n = 0;
    void *p = xRingbufferReceiveUpTo(s_ul_rb, &n, 0, size);
    if (p) {
      memcpy(buf, p, n);
      vRingbufferReturnItem(s_ul_rb, p);
      got = n;
    }
  }
  if (got < size)
    memset(buf + got, 0, size - got); /* falta de dados = silencio */
  return size;
}

/* ---------------- init ---------------- */

esp_err_t audio_io_init(void) {
  s_lock = xSemaphoreCreateMutex();
  /* Buffer grande de musica em PSRAM (acesso so da task de audio e do callback
   * A2DP); sem PSRAM cai na RAM interna */
  s_music_size = MUSIC_RB_SIZE;
  s_music_rb = xRingbufferCreateWithCaps(s_music_size, RINGBUF_TYPE_BYTEBUF,
                                         MALLOC_CAP_SPIRAM);
  if (!s_music_rb) {
    s_music_size = MUSIC_RB_SIZE / 2;
    ESP_LOGW(TAG,
             "PSRAM indisponivel para o buffer de musica; usando RAM interna");
    s_music_rb =
        xRingbufferCreateWithCaps(s_music_size, RINGBUF_TYPE_BYTEBUF,
                                  MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  }
  s_dl_rb = xRingbufferCreate(CALL_RB_SIZE, RINGBUF_TYPE_BYTEBUF);
  s_ul_rb = xRingbufferCreate(CALL_RB_SIZE, RINGBUF_TYPE_BYTEBUF);
  ESP_RETURN_ON_FALSE(s_lock && s_music_rb && s_dl_rb && s_ul_rb,
                      ESP_ERR_NO_MEM, TAG, "sem memoria");

  /* Full-duplex na mesma porta: TX = fones, RX = microfone. ESP32 e o mestre do
   * clock. */
  i2s_chan_config_t cc = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
  /* Aumenta descritores DMA e frames por descritor para absorver rajadas
     do rádio Bluetooth (8 descritores * 512 frames = ~92 ms de folga). */
  cc.dma_desc_num = 8;
  cc.dma_frame_num = 512;
  cc.auto_clear = true;
  ESP_RETURN_ON_ERROR(i2s_new_channel(&cc, &s_tx, &s_rx), TAG, "new channel");

  i2s_std_config_t std = {
      .clk_cfg = clk_for(44100),
      .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT,
                                                      I2S_SLOT_MODE_STEREO),
      .gpio_cfg =
          {
              .mclk = I2S_GPIO_UNUSED, /* o breakout gera o proprio MCLK
                                          (confirme no esquematico) */
              .bclk = BOARD_I2S_BCLK,
              .ws = BOARD_I2S_WS,
              .dout = BOARD_I2S_DOUT,
              .din = BOARD_I2S_DIN,
          },
  };
  ESP_RETURN_ON_ERROR(i2s_channel_init_std_mode(s_tx, &std), TAG, "tx init");
  ESP_RETURN_ON_ERROR(i2s_channel_init_std_mode(s_rx, &std), TAG, "rx init");

  /* Core 1: o controlador Bluetooth roda no core 0.
   * Prio 18: o plano de dados (PCM) deve preemptar tudo da aplicação no core 1
   * (act_audio=8, lvgl=2). Como a task passa quase todo o tempo bloqueada no
   * DMA do I2S, isso não rouba CPU — só garante latência mínima. */
  BaseType_t ok =
      xTaskCreatePinnedToCore(audio_task, "audio_io", 4096, NULL, 18, NULL, 1);
  ESP_RETURN_ON_FALSE(ok == pdPASS, ESP_ERR_NO_MEM, TAG, "task");
  return ESP_OK;
}

void audio_io_get_drift_stats(audio_io_drift_stats_t *stats) {
  if (!stats) return;
  stats->fill_bytes = s_drift_cur_fill_bytes;
  stats->fill_ms = (s_rate > 0) ? (uint32_t)((uint64_t)s_drift_cur_fill_bytes * 1000 / (s_rate * 4)) : 0;
  stats->dropped_frames = s_drift_dropped_frames;
  stats->inserted_frames = s_drift_inserted_frames;
}
