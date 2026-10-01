#pragma once
/* Limpeza de ruido da voz no caminho de uplink (passo 5).
 * Filtro passa-altas + gate/expansor adaptativo. Roda em mono 16 bits. */
#include <stddef.h>
#include <stdint.h>

void voice_nr_init(uint32_t sample_rate_hz);
void voice_nr_process(int16_t *pcm, size_t samples);
