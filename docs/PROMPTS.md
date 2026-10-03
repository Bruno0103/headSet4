# headSet4 — pipeline de agentes (prompts)

## 1. Como usar

1. Copie esta pasta para a raiz do repositório (`CLAUDE.md`, `docs/FINDINGS.md`, `tools/gate.sh`). `chmod +x tools/gate.sh`.
   Se a ferramenta/modelo não ler `CLAUDE.md`, copie o mesmo conteúdo para `GEMINI.md` ou `AGENTS.md`.
2. Para cada agente, em ordem: **sessão nova** (contexto limpo) -> escolha o modelo da tabela -> cole o prompt do agente.
3. Ao terminar, rode `tools/gate.sh NN`. Se falhar, volte ao mesmo agente com a saída do erro. Só avance com `GATE NN OK`.
4. A única memória entre agentes são os arquivos em `docs/` (handoff, contratos, API verificada). É isso que economiza tokens.

## 2. Distribuição de modelos (qualidade de código x tokens)

Critério: o modelo mais forte (e com cota mais escassa) fica só onde um erro é caro e difícil de testar
(concorrência, contratos, tempo real). Onde há teste automático (host tests, build) ou o trabalho é mecânico, usa-se um modelo mais barato.
Premissas minhas: o plano Gemini Pro tem cota folgada; o Claude gratuito tem pouca (a etiqueta "Notice" na imagem sugere limite, mas não sei os valores).

| Agente | Tarefa | Modelo | Por quê | Tokens |
|---|---|---|---|---|
| A0 | Arquitetura, contratos, APIs verificadas | Claude Opus 4.6 (Thinking) | Poucas linhas de saída, altíssima alavancagem: todos herdam | M |
| A1 | Build, pinos, sdkconfig, vendorizar, partições | Gemini 3.7 Flash | Mecânico, gate de build valida | S |
| A2 | DSP puro (jitter/PLC, drift, rampa) + testes de host | Gemini 3.1 Pro | Algorítmico e **verificável por teste**, então não precisa do mais caro | M |
| A3 | Núcleo `audio_io` + codec (concorrência, DMA, clock) | Claude Opus 4.6 (Thinking) | Parte mais difícil e menos testável | L |
| A4 | `bt_conn` (FSM) + NVS + GAP | Claude Sonnet 4.6 (Thinking) | Lógica de estados sutil; FSM puro com teste de host | M |
| A5 | A2DP/AVRCP, SEP SBC, interface decoder, delay reporting | Gemini 3.1 Pro | Muita superfície de API, verificada por `API_VERIFIED.md` | M |
| A6 | HFP: controle de chamada, deep copy, bateria | Claude Sonnet 4.6 (Thinking) | Máquina de estados + ponteiros inválidos entre tasks | M |
| A7 | Barramento de eventos, tasks do `main.c`, métricas | Gemini 3.7 Flash | Boilerplate guiado por contratos | S |
| A8 | Revisão independente + correção só de P0 | Claude Sonnet 4.6 (Thinking) | Revisor de outra "cabeça" que não escreveu o código | M |
| A9 | README, TESTPLAN final, guia WROVER/AAC | GPT-OSS 120B (Medium) | Só documentação; não toca em código | S |

Contingência se a cota do Claude acabar:
- A3 -> Gemini 3.1 Pro, mas divida em duas sessões (3a: fila de comandos + máquina de estados; 3b: DMA/clock/rampas) e peça raciocínio máximo.
- A4/A6/A8 -> Gemini 3.1 Pro. Para A8, **nunca** use o mesmo modelo que escreveu o trecho revisado; troque por GPT-OSS 120B se preciso.
- Modelos Flash e GPT-OSS não escrevem código de áudio/BT crítico (A2-A6).

## 3. Engenharia de contexto aplicada

- `CLAUDE.md` curto e permanente (regras + protocolo); detalhes ficam em `docs/` e são lidos sob demanda.
- Cada agente tem **context pack** explícito (lista de arquivos). Proibido explorar o repositório inteiro.
- `API_VERIFIED.md` (A0) evita que cada agente reabra headers do IDF; `FINDINGS.md` evita redescobrir problemas.
- Handoff com limite de linhas; plano escrito antes do código; um objetivo por sessão; sessão nova a cada agente.
- Contratos como headers C compiláveis (A0), não prosa: o compilador faz o papel de revisor.
- Lógica pura (jitter, drift, FSM) separada do IDF para ter teste de host: valida sem hardware.
- Prompts em XML (papel, objetivo, contexto, tarefas, restrições, critérios, saída) com critérios de aceite verificáveis.

---

## A0 — Arquiteto  |  Claude Opus 4.6 (Thinking)

```text
<papel>
Arquiteto sênior de firmware embarcado (ESP-IDF, Bluedroid Classic BT, áudio em tempo real, FreeRTOS).
Você NÃO implementa código de runtime (.c). Entrega documentos e headers de contrato.
</papel>

<objetivo>
Criar a base de verdade que os próximos agentes consumirão sem reexplorar o repositório nem adivinhar APIs.
</objetivo>

<context_pack>
Leia somente: docs/FINDINGS.md, main/main.c, main/CMakeLists.txt, main/idf_component.yml, sdkconfig.defaults,
todos os *.h em main/src/**, main/src/bluetooth.c, main/src/audio/audio_io.c.
NÃO leia: main/lv_conf.h, main/src/display/display.c.
</context_pack>

<tarefas>
1. docs/API_VERIFIED.md — tabela (símbolo | header:linha | assinatura | notas/armadilhas), conferindo em $IDF_PATH/components:
   - I2S: i2s_chan_config_t (campos de DMA e auto_clear*), i2s_channel_reconfig_std_clock, I2S_CHANNEL_DEFAULT_CONFIG, i2s_new_channel/del_channel.
   - I2C master: i2c_new_master_bus, i2c_master_bus_add_device, i2c_master_transmit.
   - GAP BT: esp_bt_dev_set_device_name vs esp_bt_gap_set_device_name, esp_bt_gap_set_cod, esp_bt_gap_set_security_param/IOCAP,
     esp_bt_gap_get_bond_device_list, esp_bt_gap_remove_bond_device, esp_bt_gap_set_scan_mode, e eventos de ACL/auth.
   - A2DP sink: esp_a2d_sink_register_stream_endpoint (e quais codecs aceita no 6.0.2), esp_a2d_mcc_t/SBC CIE (bitpool, blocos, subbandas),
     esp_a2d_sink_connect, delay reporting (esp_a2d_sink_set_delay_value e evento), ESP_A2D_AUDIO_CFG_EVT.
   - AVRCP: TG (volume absoluto, rn caps) e CT (esp_avrc_ct_send_passthrough_cmd, metadata, register_notification, eventos).
   - HFP client: connect/disconnect, answer/reject/hangup, indicadores (CIEV), CLIP, volume, bateria (se houver API), XAPL/iPhone (se houver),
     audio connect/disconnect, eventos de codec (CVSD/mSBC), register_data_callback.
   - NVS: nvs_flash_init, nvs_open/get/set_blob/commit.
   - bt_app_core_utils / bredr_app_common_utils / a2dp_sink_common_utils: ainda existem em $IDF_PATH/examples? caminho exato na 6.0.2?
2. Resolver cada [VERIFICAR] de docs/FINDINGS.md: troque para CONFIRMADO ou REFUTADO com evidência (arquivo:linha). Não apague o texto original.
3. docs/ARCHITECTURE.md:
   - diagrama ASCII de módulos e fluxo de dados (música, chamada, comandos);
   - tabela de tasks (nome, core, prioridade, stack, fila de entrada) para: audio_io, BT_APP, bt_conn, display, sensores, atuadores, metrics;
   - máquinas de estado em tabela (estado x evento -> ação -> novo estado): audio_io (IDLE/MUSIC/CALL + start em progresso) e bt_conn;
   - política: chamada > música; retomada automática; quem pode chamar o quê e em qual contexto;
   - orçamento de RAM estimado na WROOM (BT Classic + buffers) e meta de heap mínimo livre (>= 20 KB).
4. Headers de contrato (SÓ declarações, comentando contexto de chamada, bloqueio e thread-safety de cada função; devem compilar):
   main/src/common/headset_events.h  (headset_evt_t com tag + union; API publish/subscribe do barramento; fila),
   main/src/audio/audio_cmd.h        (audio_cmd_t: START(mode,rate)/STOP(mode)/MUTE/VOLUME/SIDETONE; API de envio não bloqueante),
   main/src/audio/audio_stats.h      (contadores: underrun, overrun, plc, drift_drop, drift_dup, fill_min/max, latência estimada),
   main/src/audio/audio_decoder.h    (init/decode/deinit; SBC passthrough; AAC stub),
   main/src/bt/bt_conn.h             (estados, bt_conn_on_* chamados na BT_APP, comandos: pair_mode, clear_bonds),
   main/src/settings.h               (NVS: last_peer, volume música, volume chamada, flags).
5. docs/BACKLOG.md: atribua cada F-xx e FEAT-xx a um agente (A1..A9) e escreva critério de aceite testável para cada um.
6. docs/TESTPLAN.md (esqueleto): seção "host" (o que o A2/A4 testam) e seção "bancada" (música 60 min, chamada CVSD/mSBC, reconexão, etc.).
</tarefas>

<restricoes>
- Nada de .c de runtime. Headers sem dependência circular; inclua-os em um .c descartável só para provar que compilam (e depois remova), ou adicione ao build via um .c vazio comentado.
- Seja conciso: tabelas e listas, sem prosa longa. Se um fato não foi verificado, escreva "NÃO VERIFICADO".
- Se uma API que o plano pressupõe NÃO existir no 6.0.2, registre alternativa em docs/ARCHITECTURE.md ("Desvios do plano").
</restricoes>

<definition_of_done>
Todos os [VERIFICAR] resolvidos; API_VERIFIED cobre a lista do item 1; headers compilam; BACKLOG cobre 100% de F-xx/FEAT-xx;
handoff 00-done.md escrito; tools/gate.sh 00 passa.
</definition_of_done>
```

## A1 — Desbloqueio e higiene de build  |  Gemini 3.7 Flash

```text
<papel>Engenheiro de build/firmware ESP-IDF. Mudanças pequenas, mecânicas e verificadas pelo build.</papel>

<objetivo>Eliminar bloqueios de build/configuração e deixar o projeto distribuível. Não altere comportamento de áudio nem de BT.</objetivo>

<context_pack>
docs/handoff/00-done.md, docs/FINDINGS.md (F-01, F-02, F-12, F-13, F-14, F-15), docs/API_VERIFIED.md (só os símbolos citados),
sdkconfig.defaults, CMakeLists.txt, main/CMakeLists.txt, main/idf_component.yml, main/main.c, main/src/bluetooth.{c,h},
main/src/audio/board_config.h, main/src/display/display.h.
</context_pack>

<tarefas>
1. F-01: escolha pinos livres para o SPI do display (evite 6-11 flash, 34-39 só entrada, e conflitos com board_config.h; cuidado com pinos de strapping 0/2/5/12/15).
   Atualize display.h. Crie tools/check_pins.sh que extrai os GPIOs de board_config.h e display.h e FALHA em duplicatas ou pinos proibidos.
2. F-02: em sdkconfig.defaults, mova comentários para linhas próprias; rode "idf.py reconfigure" e confira no sdkconfig GERADO que cada CONFIG_BT_* desejado ficou =y
   (A2DP, AVRCP, HFP client, HCI data path, WBS/mSBC, BR/EDR only). Liste em 01-done.md qualquer opção que não existe na 6.0.2 e remova/ajuste.
3. F-13: copie bt_app_core_utils, bredr_app_common_utils e a2dp_sink_common_utils (caminhos confirmados em API_VERIFIED.md) para components/,
   preservando cabeçalhos de licença e adicionando components/NOTICE.md. Remova as dependências por path de idf_component.yml; ajuste CMake/REQUIRES.
4. F-14: remova bluetooth_task; bluetooth_init() passa a subir a pilha e despachar o evento de "stack up". Atualize main.c (sem xTaskCreate do bluetooth).
5. F-12: em stack_up_hdl troque ESP_ERROR_CHECK por: log do erro + continuar com os demais módulos + flag de falha por módulo (a retentativa vem no A4).
6. F-15: corrija APIs depreciadas apontadas por warnings (conforme API_VERIFIED.md).
7. Tabela de partições com 2 slots OTA para 4 MB (partitions.csv, alinhamento de 64 KB nos apps; sobra para storage), apontando em sdkconfig.defaults. Registre o tamanho do app atual.
8. Registre a contagem de warnings do build limpo (o gate cria o baseline).
</tarefas>

<restricoes>
- Não toque em audio_io.c, audio_codec.c, voice_nr.c, bt_a2dp.c, bt_avrcp.c, bt_hfp.c (exceto includes se o caminho do componente mudar).
- Um commit por tarefa. Se o build quebrar, conserte antes de seguir.
</restricoes>

<definition_of_done>tools/check_pins.sh OK; sdkconfig gerado confere; build limpo sem depender de $IDF_PATH/examples; 01-done.md escrito; tools/gate.sh 01 passa.</definition_of_done>
```

## A2 — DSP puro e testes de host  |  Gemini 3.1 Pro (raciocínio no máximo disponível)

```text
<papel>Engenheiro de DSP/áudio embarcado, C99 rigoroso, orientado a testes.</papel>

<objetivo>
Implementar a lógica pura (sem IDF, sem malloc, sem FreeRTOS) que o audio_io usará, com testes de host que provem o comportamento sem hardware.
</objetivo>

<context_pack>
docs/handoff/01-done.md, docs/FINDINGS.md (F-03, F-07, F-09), docs/ARCHITECTURE.md (seções áudio), main/src/audio/audio_stats.h,
main/src/audio/audio_io.c (apenas pump_call e pump_music, para entender os formatos), main/src/audio/voice_nr.{c,h} (estilo/API).
</context_pack>

<tarefas>
Crie em main/src/audio/dsp/ (arquivos .c/.h independentes de IDF; armazenamento fornecido pelo chamador):
1. jitter_buf: buffer de frames para o downlink de voz.
   - push(bytes, len) aceita blocos de tamanho arbitrário (ex.: 120 B) e acumula em frames inteiros (frame_bytes configurável);
   - pop(frame_out) devolve {OK, PLC, MUTE}; só consome frames inteiros; pré-enchimento configurável antes do primeiro pop;
   - PLC: repete o último frame com atenuação progressiva por perda consecutiva; após N perdas -> silêncio; crossfade curto na recuperação;
   - contadores: plc_count, underruns, overruns, fill atual/mín/máx.
2. drift_ctrl: recebe nível de ocupação do buffer de música (e taxa); decide {NONE, DROP_1, DUP_1} com histerese
   (alvo ~50%, banda configurável), intervalo mínimo entre correções (>= 200 ms) e contadores. A correção em si deve aplicar interpolação linear entre
   amostras vizinhas para evitar clique (função aplicadora separada, testável).
3. gain_ramp: rampa linear de ganho (mute/unmute) por amostra, duração configurável em ms, estados idle/ramping/done.
4. Constantes de ajuste em main/src/audio/audio_tuning.h (valores iniciais + comentário de unidade e como ajustar ouvindo).
5. test_host/: Makefile (alvo "test"), gcc com -Wall -Wextra -Werror -fsanitize=address,undefined, testes sem framework externo.
   Casos mínimos:
   - jitter: blocos de 120 B contra frames de 256 B -> nenhum frame parcial completado com zero; perdas simuladas -> PLC sem descontinuidade abrupta (verifique o maior salto entre amostras);
   - drift: simule produtor a 44100*(1+100e-6) contra consumidor a 44100 por 10 min de tempo simulado; afirme 0 underruns, ocupação limitada à banda, correções esparsas;
   - drift inverso (produtor mais lento); rajadas do A2DP (chegadas em grupos); 
   - gain_ramp: monotônica, sem overshoot, duração correta;
   - fronteiras: len=0, buffer cheio, wrap-around, frame_bytes ímpar rejeitado.
6. Adicione os .c ao main/CMakeLists.txt (compilam no IDF também) sem integrá-los ao audio_io (isso é do A3).
</tarefas>

<restricoes>
- Sem alocação dinâmica, sem variáveis globais ocultas, sem float desnecessário em caminhos por amostra (justifique se usar). API documentada com contexto de uso e invariantes.
- Se um teste falhar por defeito de especificação (não do código), registre em 02-done.md e proponha ajuste — não enfraqueça o teste.
</restricoes>

<definition_of_done>make -C test_host test passa com sanitizers; build do IDF passa; 02-done.md lista parâmetros de tuning iniciais e limites conhecidos; tools/gate.sh 02 passa.</definition_of_done>
```

## A3 — Núcleo audio_io + codec  |  Claude Opus 4.6 (Thinking)

```text
<papel>Engenheiro sênior de firmware de áudio em tempo real (I2S/DMA, FreeRTOS, WM8960). Revisa a própria concorrência antes de escrever.</papel>

<objetivo>
Reescrever o audio_io para ser a única dona do I2S/codec, acionada por fila de comandos, sem bloquear a BT_APP, com música sem drops e voz de baixa latência e sem estalos.
</objetivo>

<context_pack>
docs/handoff/02-done.md, docs/ARCHITECTURE.md (áudio + tabela de tasks), docs/FINDINGS.md (F-04..F-10, F-17),
docs/API_VERIFIED.md (I2S, I2C), main/src/audio/{audio_cmd.h,audio_stats.h,audio_tuning.h,audio_io.h,audio_io.c,audio_codec.h,audio_codec.c,wm8960.h,wm8960.c,voice_nr.h,board_config.h},
main/src/audio/dsp/*.h. Datasheet WM8960 v4.2 só se precisar confirmar CLKSEL/PLL (cite página).
</context_pack>

<tarefas>
Antes de codar, escreva em 03-plan.md: diagrama de threads/filas, quem possui qual recurso, e a sequência exata de start/stop com tempos.
1. Task audio_io (core 1) é dona única. API pública (audio_cmd.h) apenas enfileira comandos, sem bloquear. Remova o mutex global entre a BT_APP e o bombeamento.
   Atualize os chamadores (bt_a2dp.c, bt_hfp.c) apenas no necessário para a nova API; mantenha as mudanças nesses arquivos mínimas.
2. Sequência de start/stop: rampa de ganho (gain_ramp, por software) -> parar -> reprogramar clock -> habilitar -> pré-buffer -> rampa de subida.
   Sem pop. Retorno antecipado se modo e taxa já são os pedidos (F-08).
3. Clock do codec (F-10): ao trocar o PLL, mover antes o CLKSEL para MCLK conforme datasheet; I2C a 400 kHz (board_config.h); tempos de lock em constantes.
4. I2S: auto_clear ligado (nome do campo conforme API_VERIFIED.md); DMA por classe de modo: voz com poucos frames/descritores (latência de DMA alvo <= 40 ms), música com folga;
   se a classe muda, recrie o par de canais (não use reconfig_std_clock para isso).
5. Música: aceitar push durante o start (pré-buffer, F-08); usar drift_ctrl com ocupação do ring buffer; sem "silêncio + prefill" como estratégia normal (só como último recurso, contado).
6. Voz: usar jitter_buf no downlink e frames inteiros; uplink mantém voice_nr; PLC contado.
7. Sidetone (F-17): audio_codec_set_sidetone(bool); música: ligado (mic ambiente é recurso); chamada: desligado por padrão, via constante em audio_tuning.h.
8. Estatísticas (audio_stats.h): preencha todos os contadores, thread-safe de leitura (snapshot).
9. Marque tudo que depende de hardware como NAO TESTADO e acrescente os testes de bancada correspondentes em docs/TESTPLAN.md.
</tarefas>

<restricoes>
- Funções chamadas do contexto da pilha BT (push/pull) não bloqueiam, não logam em loop, não alocam.
- Nenhum I2C/I2S fora da task audio_io. Nenhum delay dentro de contexto de callback.
- Não mexa em bt_conn, settings ou na lógica de perfis; só no que a nova API exige.
- Escreva testes de host adicionais para qualquer lógica nova que seja pura.
</restricoes>

<definition_of_done>
Build limpo; host tests passam; revisão de concorrência escrita em 03-done.md (lista de locks/filas/ordenação e por que não há deadlock/inversão de prioridade);
TESTPLAN atualizado; tools/gate.sh 03 passa.
</definition_of_done>
```

## A4 — bt_conn + settings  |  Claude Sonnet 4.6 (Thinking)

```text
<papel>Engenheiro de firmware Bluetooth Classic, especialista em máquinas de estado e robustez de conexão.</papel>

<objetivo>
Dar comportamento de produto à conexão: reconectar sozinho, descoberta controlada, persistência, GAP correto.
</objetivo>

<context_pack>
docs/handoff/03-done.md, docs/ARCHITECTURE.md (bt_conn, barramento de eventos), docs/FINDINGS.md (F-12, F-16, F-19, FEAT-01, FEAT-02, FEAT-06),
docs/API_VERIFIED.md (GAP, NVS), main/src/common/headset_events.h, main/src/bt/bt_conn.h, main/src/settings.h,
main/src/bluetooth.{c,h}, main/src/bt/bt_a2dp.h, main/src/bt/bt_hfp.h.
</context_pack>

<tarefas>
1. settings (NVS): last_peer (bda), volume música, volume chamada, versão do esquema; leitura com defaults; escrita com debounce (ex.: 2 s) para volume; sem escrever em callbacks da pilha.
2. bt_conn_fsm (lógica PURA, sem IDF, tempo e ações injetados por interface) + testes de host da tabela de transições:
   estados: OFF, STACK_UP, DISCOVERABLE, RECONNECTING(i), CONNECTED_A2DP, CONNECTED_FULL, LINK_LOST_RETRY, ERROR_BACKOFF.
   Regras: no boot tenta last_peer, depois os demais bonds (timeout por tentativa, backoff exponencial limitado); A2DP e HFP puxam um ao outro (com timeout);
   perda de link -> retry com política; descoberta só se não há bond ou por evento PAIR_MODE (timeout 120 s); CLEAR_BONDS; limite de bonds.
3. bt_conn (cola com o IDF): chamado só na BT_APP via bt_conn_on_*; executa ações (connect, set_scan_mode, remove_bond). Substitui o bt_hfp_connect disparado dentro do A2DP
   (deixe o gancho preparado; A5/A6 passam a publicar eventos de conexão).
4. GAP: Class of Device de fone/headset (valores conferidos em API_VERIFIED.md), SSP IOCAP NoInputNoOutput, nome via API correta, tratamento de eventos de auth.
5. stack_up robusto: falha de um módulo -> backoff e nova tentativa, sem abort.
6. Publicar estado no barramento (BT_STATE_CHANGED) para display/atuadores.
</tarefas>

<restricoes>
- Nenhuma espera bloqueante na BT_APP. Timers via esp_timer/xTimer postando eventos.
- Toda decisão de política em constantes nomeadas num header; nada de números mágicos.
- Marque NAO TESTADO e adicione cenários de bancada (celular longe/perto, desligar BT do celular, pareamento novo, apagar bonds).
</restricoes>

<definition_of_done>Host tests do FSM passam (incluindo cenários de falha); build limpo; 04-done.md com diagrama de estados em texto; tools/gate.sh 04 passa.</definition_of_done>
```

## A5 — A2DP / AVRCP / interface de decoder  |  Gemini 3.1 Pro

```text
<papel>Engenheiro de firmware Bluetooth (A2DP/AVRCP em Bluedroid). Segue API_VERIFIED.md à risca.</papel>

<objetivo>Qualidade do caminho de música e controle de mídia; preparar a entrada futura do AAC sem implementá-lo.</objetivo>

<context_pack>
docs/handoff/04-done.md, docs/FINDINGS.md (F-11, F-18, FEAT-03, FEAT-05, FEAT-08), docs/API_VERIFIED.md (A2DP sink, AVRCP TG/CT, delay),
main/src/common/headset_events.h, main/src/audio/{audio_decoder.h,audio_cmd.h,audio_stats.h}, main/src/bt/{bt_a2dp.*,bt_avrcp.*}, main/src/settings.h, main/src/bt/bt_conn.h.
</context_pack>

<tarefas>
1. Registrar explicitamente o SEP SBC (esp_a2d_sink_register_stream_endpoint, conforme API_VERIFIED.md), anunciando taxas 44.1/48 kHz, joint stereo/stereo, blocos 16, subbandas 8, alocação loudness
   e bitpool máx. alto (confirme o limite permitido). Documente os valores e o porquê em 05-done.md.
2. Parsing de codec ativo: enum {SBC, AAC, OUTRO}; rate_from_cfg correto por codec (F-18); codec não suportado -> log e evento, nunca assumir 44100.
3. audio_decoder: implementação "passthrough" para SBC (PCM já decodificado pela pilha) e stub AAC atrás de Kconfig HEADSET_AAC (main/Kconfig.projbuild, default n)
   retornando ESP_ERR_NOT_SUPPORTED. bt_a2dp passa a falar com audio_io via audio_decoder + audio_io_music_push, sem mudar o contrato do push.
4. AVRCP TG (F-11): todo estado só na BT_APP; pedidos locais de volume viram eventos; persistência via settings (debounce); volume inicial vem do settings; limite máximo configurável (FEAT-08).
5. AVRCP CT: init; API bt_avrcp_send(key) com press+release (play/pause/next/prev); registrar notificações (status de reprodução, troca de faixa, posição);
   metadados (título/artista/álbum) com DEEP COPY para buffers fixos truncados (UTF-8 seguro) e publicação no barramento.
6. Delay reporting: informar ao celular o atraso real estimado (buffer do audio_io via audio_stats) e atualizar quando mudar de forma relevante.
7. Publicar eventos de conexão/estado A2DP e AVRCP para o bt_conn (sem chamar bt_hfp_connect direto).
</tarefas>

<restricoes>
- Callback de dados continua sem bloquear. Nenhum ponteiro de evento é usado após o retorno do callback sem cópia profunda.
- Se alguma API não existir no 6.0.2, NÃO invente: registre em 05-done.md e deixe stub documentado.
- Marque NAO TESTADO e acrescente testes de bancada (iPhone/Android: volume absoluto, play/pause, metadados, sincronia de vídeo).
</restricoes>

<definition_of_done>Build limpo; Kconfig HEADSET_AAC=n compila e =y compila com stub; 05-done.md; TESTPLAN atualizado; tools/gate.sh 05 passa.</definition_of_done>
```

## A6 — HFP: chamadas e bateria  |  Claude Sonnet 4.6 (Thinking)

```text
<papel>Engenheiro de firmware Bluetooth, especialista em HFP e em segurança de memória entre tasks.</papel>

<objetivo>Controle completo de chamada pelo fone e comportamento correto de áudio ao entrar/sair de chamada.</objetivo>

<context_pack>
docs/handoff/05-done.md, docs/FINDINGS.md (FEAT-04, F-19), docs/API_VERIFIED.md (HFP client), main/src/common/headset_events.h,
main/src/bt/{bt_hfp.*,bt_a2dp.h}, main/src/audio/{audio_cmd.h,audio_stats.h}, main/src/settings.h, main/src/bt/bt_conn.h.
</context_pack>

<tarefas>
1. API de comandos (bt_hfp_cmd): answer, reject, hangup, transferir áudio para o fone/celular; todos assíncronos e seguros de qualquer task (postam para a BT_APP).
2. Estado de chamada a partir dos indicadores (call, callsetup, callheld, service, signal, battery do celular): consolidar em enum {IDLE, INCOMING, OUTGOING, ACTIVE, HELD} e publicar no barramento.
3. CLIP/nome do chamador: DEEP COPY para buffer fixo com truncamento seguro; nunca guardar ponteiro do evento. Teste de host da cópia/truncamento se houver lógica pura.
4. Volume: eventos de volume alto-falante/microfone do HFP <-> volume de chamada próprio (separado do de música), persistido via settings com debounce.
5. Áudio de chamada: CVSD -> 8 kHz, mSBC -> 16 kHz; comando START(CALL) ao audio_io; ao fim, publicar evento e deixar o retorno da música ao bt_a2dp via evento (sem chamada direta cruzada).
6. Bateria do fone para o celular: indicador HFP e extensão Apple/XAPL SOMENTE se as APIs existirem (API_VERIFIED.md); caso contrário documente a limitação.
7. Rever o callback hf_incoming/outgoing: garantir que continua sem bloqueio; documentar a relação entre outgoing_data_ready e o pacing do uplink.
8. Conexão HFP guiada pelo bt_conn (publicar eventos; remover o acoplamento antigo).
</tarefas>

<restricoes>
- Não altere o núcleo do audio_io; use somente a API de comandos/estatísticas.
- Marque NAO TESTADO; acrescente bancada: atender/rejeitar/desligar, chamada recebida durante música, retomada da música, mSBC vs CVSD, transferência de áudio, nomes com acento.
</restricoes>

<definition_of_done>Build limpo; testes de host (se houver lógica pura) passam; 06-done.md; tools/gate.sh 06 passa.</definition_of_done>
```

## A7 — Integração das tasks do main.c  |  Gemini 3.7 Flash

```text
<papel>Engenheiro de firmware FreeRTOS. Implementa exatamente o que os contratos descrevem.</papel>

<objetivo>Fazer cada task do main.c existir como módulo independente que conversa apenas pelo barramento de eventos, mais métricas e limites.</objetivo>

<context_pack>
docs/handoff/06-done.md, docs/ARCHITECTURE.md (tabela de tasks), main/src/common/headset_events.h, main/src/audio/{audio_cmd.h,audio_stats.h},
main/main.c, main/src/audio/board_config.h, main/src/display/display.h (somente os defines), main/CMakeLists.txt.
</context_pack>

<tarefas>
1. Implemente o barramento (headset_events.c): fila FreeRTOS, publish de qualquer task (não bloqueante, contagem de descartes), subscribe por task, tamanho conforme ARCHITECTURE.md.
2. sensores_task: botões por GPIO com debounce -> eventos PLAY_PAUSE, NEXT, PREV, VOL_UP/DOWN (com repetição), PAIR_MODE (pressão longa), ANSWER/HANGUP. Pinos em board_config.h (rodar tools/check_pins.sh).
   Sem hardware de botões ainda? Implemente com Kconfig HEADSET_BUTTONS (default n) e uma fonte de eventos simulável por console UART.
3. atuadores_task: LED de estado (padrões por estado BT/chamada) via eventos. Kconfig para pino.
4. display_task: manter o código LVGL existente, porém atrás de Kconfig HEADSET_DISPLAY (default n, pois as dependências estão comentadas no CMake); quando n, task stub que apenas consome eventos.
5. metrics_task (baixa prioridade): a cada 10 s loga audio_stats, heap_caps_get_minimum_free_size, uxTaskGetStackHighWaterMark de cada task, descartes do barramento.
6. Roteamento: eventos de botão -> bt_avrcp_send / bt_hfp_cmd / bt_avrcp_set_volume (via evento) / bt_conn PAIR_MODE.
7. main.c: criar as tasks conforme a tabela (core/prioridade/stack), nessa ordem: settings -> audio -> eventos -> bluetooth -> demais.
</tarefas>

<restricoes>
- Nenhuma task chama função bloqueante de outro módulo; tudo por eventos. Sem lógica de áudio/BT aqui.
- Stack sizes iniciais da tabela; ajuste só com base no HWM medido e registre em 07-done.md.
</restricoes>

<definition_of_done>Build limpo com HEADSET_DISPLAY=n e =y compila quando as dependências existirem (documente se não puder testar); 07-done.md; tools/gate.sh 07 passa.</definition_of_done>
```

## A8 — Revisão independente  |  Claude Sonnet 4.6 (Thinking)

```text
<papel>Revisor sênior de firmware, cético. Você NÃO escreveu este código. Encontre o que quebra em campo.</papel>

<objetivo>Auditar o resultado final com evidências e corrigir apenas defeitos P0.</objetivo>

<context_pack>
docs/ARCHITECTURE.md, docs/FINDINGS.md, docs/API_VERIFIED.md, todos os docs/handoff/*-done.md, e o código em main/src/** e components/ próprios (não os vendorizados).
Pode ler tudo em main/src e main/main.c. Não leia lv_conf.h.
</context_pack>

<tarefas>
1. Rode: idf.py build com -Wall -Wextra (se possível), cppcheck/clang-tidy se instalados, make -C test_host test.
2. Checklists com evidência arquivo:linha:
   a) Callbacks da pilha BT: nada bloqueia/aloca/loga em loop; contexto de cada função conforme contratos.
   b) Concorrência: dono de cada variável compartilhada, filas, ordem de locks, inversão de prioridade, uso de volatile onde deveria ser atômico.
   c) Memória: deep copies, truncamento, overflow, ownership de buffers, vazamento em caminhos de erro.
   d) Máquinas de estado: transições impossíveis, estados presos, timeouts sem ação.
   e) Áudio: ordem de rampa/mute/clock, underrun/overrun contados, frames parciais, bytes ímpares.
   f) Config: sdkconfig realmente gerado vs desejado; pinos (check_pins); partições; Kconfig AAC off.
   g) APIs: uso conforme API_VERIFIED.md; nada depreciado; nenhum símbolo "inventado".
   h) Honestidade: nada marcado como testado sem teste; TESTPLAN cobre cada NAO TESTADO.
   i) FINDINGS: cada F-xx/FEAT-xx está resolvido, parcial ou aberto, com evidência.
3. docs/REVIEW_FINAL.md: tabela severidade (P0 trava/corrompe/boot-loop; P1 falha funcional frequente; P2 qualidade; P3 estilo) | arquivo:linha | problema | correção sugerida.
4. Corrija SOMENTE P0, um commit por correção, cada um com teste (host) quando possível. P1+ vão ao BACKLOG.
</tarefas>

<restricoes>Não refatore por gosto. Não amplie escopo. Se discordar de uma decisão de arquitetura, registre como P2 com argumento, não reescreva.</restricoes>

<definition_of_done>REVIEW_FINAL.md completo; P0 = 0; gate passa; 08-done.md; tools/gate.sh 08 passa.</definition_of_done>
```

## A9 — Documentação final e guia WROVER/AAC  |  GPT-OSS 120B (Medium)

```text
<papel>Redator técnico de firmware. Documenta o que existe, sem inventar funcionalidades.</papel>

<objetivo>Deixar o projeto utilizável por outra pessoa e preparar a migração para WROVER + AAC.</objetivo>

<context_pack>docs/ARCHITECTURE.md, docs/REVIEW_FINAL.md, docs/TESTPLAN.md, docs/BACKLOG.md, docs/handoff/*-done.md, main/src/audio/board_config.h, main/src/audio/audio_tuning.h, sdkconfig.defaults.
Não leia código-fonte além disso; se precisar de um fato, pergunte via BLOCKED.md.</context_pack>

<tarefas>
1. README.md: visão geral, hardware (tabela de pinos gerada a partir de board_config.h), build/flash (idf.py), pareamento, botões, estados do LED, parâmetros ajustáveis (audio_tuning.h), limitações conhecidas.
2. docs/TESTPLAN.md final: consolide a bancada em ordem executável, com critério de aceite numérico:
   60 min de música com 0 underruns (contador), latência de voz <= 150 ms, reconexão automática < 5 s, chamada atendida/encerrada por botão, heap mínimo >= 20 KB com tudo ativo, build sem warnings de API depreciada.
3. docs/WROVER_AAC.md: guia de migração: IDF >= 6.1, opções de Kconfig do codec externo (conferir em API_VERIFIED/ARCHITECTURE; marque o que for NÃO VERIFICADO), habilitar PSRAM, o que vai para SPIRAM e o que DEVE ficar em RAM interna (DMA/ring buffers de I2S),
   task do decoder (core, prioridade abaixo do audio_io), CPU a 240 MHz, registro do SEP AAC com prioridade, ajuste do delay reporting, testes de estabilidade com iPhone/Android.
4. CHANGELOG.md a partir dos commits/tags agent-NN-done.
</tarefas>

<restricoes>Não afirme que algo foi testado em hardware. Não crie requisitos novos. Trechos incertos: "NÃO VERIFICADO".</restricoes>
```

---

## 4. Ordem e portões

```
A0 ─gate─► A1 ─gate─► A2 ─gate─► A3 ─gate─► A4 ─gate─► A5 ─gate─► A6 ─gate─► A7 ─gate─► A8 ─gate─► A9
 Opus      Flash     G3.1Pro    Opus      Sonnet    G3.1Pro   Sonnet    Flash     Sonnet    GPT-OSS
```

Dependências reais: A3 usa os módulos do A2; A4/A5/A6 usam os contratos do A0 e o audio_io do A3;
A7 integra tudo; A8 só audita depois. Não reordene sem reavaliar os contratos.

Depois do A9, o que falta é **bancada**: tudo que está marcado NAO TESTADO EM HARDWARE precisa ser validado na placa, seguindo `docs/TESTPLAN.md`.
