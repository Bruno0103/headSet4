# FINDINGS — achados da revisão (ground truth)

Status: **[CÓDIGO]** = visível no código lido. **[VERIFICAR]** = afirmação feita de memória sobre o IDF;
o agente A0 deve confirmar/refutar em `$IDF_PATH` e atualizar o status com evidência (header:linha).
Revisão feita sem hardware.

## Decisões de escopo (fixas)
- Sem homologação (Anatel/SIG/FCC fora do plano).
- IDF 6.0.2: A2DP sink só com SBC. AAC exige IDF >= 6.1 e PSRAM (WROVER) + decoder externo -> fora de escopo agora;
  apenas preparar interfaces (`audio_decoder`, parsing de codec ativo, Kconfig `HEADSET_AAC` desligado).
- Placa atual WROOM sem PSRAM; ainda assim, os buffers de DMA/áudio ficam em RAM interna.

## Críticos
- **F-01 [CÓDIGO]** `LCD_GPIO_MOSI=21` (display.h) conflita com `BOARD_I2C_SDA=21` (board_config.h). O comentário diz "pinos livres".
- **F-02 [CONFIRMADO]** `sdkconfig.defaults` tem comentário na mesma linha (`CONFIG_BT_HFP_WBS_ENABLE=y  # ...`). Pode invalidar a linha
  e deixar mSBC desligado. Verificado que na v6.0 as configs são `CONFIG_BT_A2DP_ENABLE`, `CONFIG_BT_HFP_CLIENT_ENABLE`, `CONFIG_BT_HFP_WBS_ENABLE`.
  As flags de SPI flash e DAC interno (`CONFIG_DAC_DMA_AUTO_16BIT_ALIGN`) foram deprecadas/removidas na v5/v6.
- **F-03 [CÓDIGO]** `pump_call`: BT entrega blocos pequenos (~120 B / 7,5 ms), o pump consome 256 B/ciclo e completa com zeros -> estalos.
  Falta jitter buffer (pré-enchimento, frames inteiros, PLC).
- **F-04 [CONFIRMADO]** `I2S_CHANNEL_DEFAULT_CONFIG` usa 6 descritores x 240 frames (~180 ms a 8 kHz só no DMA TX).
  `i2s_channel_reconfig_std_clock` não altera o tamanho do DMA, apenas clock. Para alterar DMA em tempo de execução, é preciso deletar e recriar o canal (`i2s_del_channel`/`i2s_new_channel`).
- **F-05 [CONFIRMADO]** `auto_clear` não está ligado: em underrun o DMA repete o último buffer (zumbido).
  Atenção: o campo `auto_clear` em `i2s_chan_config_t` atua como alias para `auto_clear_after_cb` (junto com `auto_clear_before_cb`) na v6.0.2 (`driver/i2s_common.h`).

## Importantes
- **F-06 [CÓDIGO]** `audio_io_start/stop` rodam na BT_APP: PLL (20 ms), delay 30 ms, I2C 100 kHz, e espera pelo mutex enquanto o
  `audio_task` pode segurar o lock em `i2s_channel_write` (até 100 ms). Atrasa eventos AVRCP/HFP. -> audio_io dono único + fila de comandos.
- **F-07 [CÓDIGO]** Sem tratamento de drift (celular x APLL do ESP32 x cristal do breakout). Underrun -> silêncio + prefill de 6 KB (drop audível).
  Buffer de 16 KB ~ 93 ms. Falta controle de nível (descartar/duplicar 1 amostra com interpolação).
- **F-08 [CÓDIGO]** `audio_io_music_push` descarta dados enquanto `s_mode != MUSIC`; o start leva ~100 ms -> início de faixa cortado.
  Também falta retorno antecipado em `audio_io_start` quando modo e taxa são iguais (clicks em eventos repetidos).
- **F-09 [CÓDIGO]** `stop_locked`: mute e `i2s_channel_disable` imediatos -> pop. Usar rampa de ganho por software antes de parar/ligar.
- **F-10 [CÓDIGO]** `audio_codec_set_sample_rate` desliga/reprograma o PLL sem trocar antes o CLKSEL para MCLK (ver datasheet WM8960 v4.2).
- **F-11 [CÓDIGO]** `bt_avrcp.c`: `s_volume` e `s_notify_registered` acessados pela BT_APP e por `bt_avrcp_set_volume` (outra task). Corrida.
- **F-12 [CÓDIGO]** `ESP_ERROR_CHECK` em `stack_up_hdl` (abort -> boot-loop). Tratar erro, logar, tentar de novo.
- **F-13 [CÓDIGO]** `idf_component.yml` depende de `${IDF_PATH}/examples/...` (não distribuível/estável). Vendorizar em `components/` com licenças.
- **F-14 [CÓDIGO]** `bluetooth_task` só despacha um evento e se deleta. Chamar direto em `bluetooth_init`.
- **F-15 [CONFIRMADO/PARCIAL]** `esp_bt_gap_set_device_name` ainda existe em `esp_gap_bt_api.h:1147` no v6.0.2 (NÃO removida). A função `esp_bt_dev_set_device_name` NÃO existe em `esp_bt_device.h` v6.0.2. A API correta a usar é `esp_bt_gap_set_device_name`. O warning de depreciação era de versões anteriores.
- **F-16 [CÓDIGO]** Volume não persiste (sempre 40). Faltam NVS e último dispositivo.
- **F-17 [CÓDIGO]** `BYPASS_B2O` (mic ambiente -> saída) fica ligado em chamadas: sidetone/eco não controlado.
- **F-18 [CÓDIGO]** `bt_a2dp` `rate_from_cfg` devolve 44100 para qualquer codec não-SBC (bug silencioso quando houver AAC).
- **F-19 [CÓDIGO]** Conexão HFP só é pedida quando o A2DP conecta; não há o inverso, nem reconexão ao boot nem após perda de link.

## Funcionalidades ausentes (FEAT)
- **FEAT-01** Gerenciador de conexão (`bt_conn`): reconectar ao último bond, lista de bonds, recuperação de link, descoberta só sem bond/botão + timeout, apagar bonds.
- **FEAT-02** Class of Device (`esp_bt_gap_set_cod`), SSP IOCAP NoInputNoOutput, nome do dispositivo.
- **FEAT-03** AVRCP CT: play/pause/next/prev (passthrough) + metadados + status de reprodução.
- **FEAT-04** HFP: atender/rejeitar/desligar, CLIP, indicadores de chamada (CIEV), volume de chamada, transferência de áudio, bateria (HFP + XAPL iOS).
  Strings recebidas: callbacks via `bt_app_work_dispatch` só copiam a struct -> ponteiros ficam inválidos -> deep copy.
- **FEAT-05** A2DP delay reporting; registro explícito do SEP SBC (bitpool alto); interface `audio_decoder`.
- **FEAT-06** NVS (último peer, volume); tabela de partições com 2 slots OTA em 4 MB.
- **FEAT-07** Barramento de eventos entre tasks do `main.c` (display/sensores/atuadores) + métricas (underruns, heap mínimo, stack HWM).
- **FEAT-08** Limite de volume máximo dos fones.
- **FEAT-09 (opcional)** Redução espectral/AEC na voz; revisão do sidetone.
