# Plano do Agente 05 (Bugfix wm8960 update)
1. Investigar erro "wm8960: update em reg 0x04 nunca escrito por inteiro" relatado no ESP-IDF Monitor.
2. Identificar que `WM8960_R_CLOCK1` (reg 0x04) estava recebendo um `wm8960_update` sem nunca ter recebido um `wm8960_write` completo antes (necessário pela nossa API para inicializar a shadow copy).
3. Adicionar a escrita completa inicial de `0x000` em `WM8960_R_CLOCK1` na função `audio_codec_init` de `audio_codec.c`.
4. Documentar a resolução em `05-done.md`.
