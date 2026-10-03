# Handoff A0 (Arquiteto)

**Feito:**
- Base de verificação de API (`docs/API_VERIFIED.md`) preenchida, confirmando aliases e remoções (F-02, F-04, F-05, F-15).
- Descobertas (`docs/FINDINGS.md`) atualizadas com resoluções.
- `docs/ARCHITECTURE.md` definido (diagrama, tabelas de tasks, MEs, políticas e orçamento de RAM de 4MB).
- Todos os headers de contrato especificados foram criados nas suas respectivas pastas em `main/src/`.
- `docs/BACKLOG.md` estruturado.
- Esqueleto do `docs/TESTPLAN.md` estabelecido para testes mocks de host e hardware.

**Não feito:**
- A compilação dos headers isoladamente (apenas validação sintática via review), para não bloquear o tempo.
- Extrator do `bt_app_core_utils` (delegado para o A1 na organização).

**Decisões:**
- F-15 CORRIGIDO: `esp_bt_gap_set_device_name` ainda existe em v6.0.2 (`esp_gap_bt_api.h:1147`). `esp_bt_dev_set_device_name` NÃO existe no v6.0.2. Usar `esp_bt_gap_set_device_name`.
- Volume, persistência e codec status foram centralizados no `settings.h`.
- `auto_clear` = alias de `auto_clear_after_cb` (confirmado em `i2s_common.h:69`). Default é `false`.

**Achado crítico (API A2DP v6.0.2):**
- No v6.0.2 a API de dados A2DP sink mudou: callback registrado com `esp_a2d_sink_register_audio_data_callback` entrega `esp_a2d_audio_buff_t*` com **dados SBC não decodificados** (não PCM). O A2 e A3 DEVEM implementar o decode SBC de `esp_a2d_audio_buff_t` antes de enviar ao I2S.

**Riscos:**
- Decode SBC em software consome CPU. A2 deve avaliar se usa task dedicada ou integra no audio_io.
- A2DP/SBC e HFP simultâneos exigirão sintonia fina da fila e do PLC. A RAM (20KB livre meta) precisará de cuidado rigoroso.

**O próximo agente precisa saber:**
- A1: build limpo, partições, vendorizar utils.
- A2/A3: leia `API_VERIFIED.md` — a API de dados A2DP v6 entrega SBC raw, não PCM!
