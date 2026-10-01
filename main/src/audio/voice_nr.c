/*
 * O que faz:
 *  1) passa-altas de 1a ordem (~120 Hz): remove rumble, vento e DC;
 *  2) piso de ruido adaptativo: desce rapido, sobe devagar (estatistica de minimo);
 *  3) expansor: abaixo de (piso x OPEN_RATIO) atenua ~15 dB, com ataque/liberacao suaves.
 *
 * O que NAO faz: nao remove ruido durante a fala (isso exigiria reducao espectral,
 * ex.: ESP-SR / RNNoise). A interface (init/process) permite trocar o algoritmo
 * sem mexer no resto do projeto.
 */
#include "voice_nr.h"

#include <math.h>

#define HPF_CUTOFF_HZ    120.0f
#define ENV_ATTACK_MS    2.0f
#define ENV_RELEASE_MS   60.0f
#define GAIN_ATTACK_MS   5.0f
#define GAIN_RELEASE_MS  120.0f
#define FLOOR_RISE_S     4.0f      /* constante de tempo com que o piso sobe */
#define WARMUP_S         0.6f      /* inicio: aprende o piso rapido (assume so ruido) */
#define WARMUP_RISE_S    0.1f
#define FLOOR_INIT       0.004f
#define FLOOR_MAX        0.01f     /* ~ -40 dBFS: acima disso nao e tratado como ruido de fundo */
#define OPEN_RATIO       3.0f      /* limiar = piso x 3 (~ +9.5 dB) */
#define LEVEL_MIN        0.0005f   /* ~ -66 dBFS */
#define MIN_GAIN         0.18f     /* ~ -15 dB entre falas */

static struct {
    float hp_a, x1, y1;
    float env, floor, gain;
    float env_att, env_rel, g_att, g_rel, floor_up, warm_up;
    size_t warm_left;
} s;

static float coef(float ms, float fs) { return 1.0f - expf(-1.0f / (ms * 0.001f * fs)); }

void voice_nr_init(uint32_t fs_hz)
{
    const float fs = (float)fs_hz;
    s.hp_a     = fs / (fs + 2.0f * 3.14159265f * HPF_CUTOFF_HZ);
    s.x1 = s.y1 = 0.0f;
    s.env      = 0.0f;
    s.floor    = FLOOR_INIT;
    s.gain     = 1.0f;
    s.env_att  = coef(ENV_ATTACK_MS, fs);
    s.env_rel  = coef(ENV_RELEASE_MS, fs);
    s.g_att    = coef(GAIN_ATTACK_MS, fs);
    s.g_rel    = coef(GAIN_RELEASE_MS, fs);
    s.floor_up = 1.0f / (FLOOR_RISE_S * fs);
    s.warm_up  = 1.0f / (WARMUP_RISE_S * fs);
    s.warm_left = (size_t)(WARMUP_S * fs);
}

void voice_nr_process(int16_t *pcm, size_t n)
{
    for (size_t i = 0; i < n; i++) {
        float x = pcm[i] * (1.0f / 32768.0f);

        float y = s.hp_a * (s.y1 + x - s.x1);            /* 1) passa-altas */
        s.x1 = x;
        s.y1 = y;

        float a = fabsf(y);                              /* 2) envoltoria e piso */
        s.env += (a > s.env ? s.env_att : s.env_rel) * (a - s.env);
        if (s.env < s.floor) {
            s.floor = s.env;
        } else if (s.warm_left) {                        /* aprendizado inicial */
            s.floor += s.warm_up * (s.env - s.floor);
        } else {
            s.floor += s.floor_up * (s.env - s.floor);
        }
        if (s.warm_left) s.warm_left--;
        if (s.floor > FLOOR_MAX) s.floor = FLOOR_MAX;

        float thr    = s.floor * OPEN_RATIO + LEVEL_MIN; /* 3) expansor */
        float target = (s.env > thr) ? 1.0f : MIN_GAIN;
        s.gain += (target > s.gain ? s.g_att : s.g_rel) * (target - s.gain);

        float out = y * s.gain * 32768.0f;
        pcm[i] = (int16_t)(out > 32767.0f ? 32767.0f : (out < -32768.0f ? -32768.0f : out));
    }
}
