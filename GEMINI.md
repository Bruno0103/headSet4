# headSet4 — contexto permanente (mantenha curto: é carregado em toda sessão)

Headset Bluetooth Classic (A2DP sink + AVRCP + HFP) em ESP32-WROOM 4 MB, ESP-IDF v6.0.2,
codec SparkFun WM8960 (I2C 0x1A + I2S). Projeto pessoal: sem homologação.
Escopo agora: **SBC / mSBC / CVSD**. AAC é futuro (WROVER + IDF >= 6.1): só preparar interfaces.
Idioma: comentários e docs em português; identificadores em inglês/snake_case, como já no repo.

## Regras invioláveis
1. **Verifique antes de usar.** Toda API do IDF/Bluedroid deve ser conferida nos headers de
   `$IDF_PATH/components/...` (nunca de memória). Registre em `docs/API_VERIFIED.md`.
   Consulte esse arquivo (grep pelo símbolo) antes de reabrir headers do IDF.
2. **Sem hardware aqui.** Nunca diga "testado". Marque `// NAO TESTADO EM HARDWARE` e
   acrescente o teste de bancada correspondente em `docs/TESTPLAN.md`.
3. **Callbacks da pilha BT** (dados A2DP/HFP, eventos): nunca bloqueiam, alocam, nem logam em loop.
4. **Task BT_APP** não faz I2C/I2S nem espera mutex longo: só posta comandos/eventos em fila.
5. **audio_io é dono único** de I2S + codec. Outros módulos só enviam `audio_cmd_t`.
6. **Sem `ESP_ERROR_CHECK` em caminho de runtime** (só em init de boot irrecuperável, comentado).
7. **Escopo estrito.** Faça só o que o seu agente pede. Outro problema achado -> `docs/BACKLOG.md`.
8. **Context pack.** Leia só o que o prompt do agente lista. Nunca leia `main/lv_conf.h` (3000 linhas).
9. Commits pequenos, convencionais: `feat(audio): ...`, `fix(bt): ...`, `refactor(...)`.
10. Dúvida não bloqueante: decida, registre em "Decisões" do handoff, siga em frente.
    Dúvida bloqueante: escreva `docs/handoff/BLOCKED.md` (o quê, por quê, opções) e pare.

## Protocolo de agente (todos seguem)
1. Leia `docs/handoff/<anterior>-done.md`, `docs/ARCHITECTURE.md` (seções relevantes),
   os headers de contrato citados e o context pack do prompt.
2. Escreva o plano (<= 15 linhas) em `docs/handoff/NN-plan.md` ANTES de editar código.
3. Implemente em passos pequenos; após cada passo: `idf.py build` (e `make -C test_host test` se existir).
4. Escreva `docs/handoff/NN-done.md` (<= 60 linhas): Feito / Não feito / Decisões /
   Contratos alterados / Riscos / "O próximo agente precisa saber".
5. Commite tudo e rode `tools/gate.sh NN`. Só ele cria a tag `agent-NN-done`.
6. **Pare.** Não inicie o próximo agente.

## Mapa do código (atual)
- `main/main.c` orquestra tasks. `main/src/bluetooth.c` sobe a pilha.
- `main/src/bt/` bt_a2dp, bt_avrcp, bt_hfp. `main/src/audio/` audio_io, audio_codec, wm8960, voice_nr, board_config.h.
- `main/src/display/` (LVGL, desligado no build). Pinos centralizados em `board_config.h`.
- Ground truth de problemas: `docs/FINDINGS.md`. Arquitetura e contratos: `docs/ARCHITECTURE.md`, headers de contrato.
