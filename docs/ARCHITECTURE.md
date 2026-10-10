# Arquitetura do Firmware Headset4

**Projeto:** Headset4 — Sistema de Áudio Bluetooth Inteligente com Interface Gráfica  
**Target:** ESP32-WROVER-E (16 MB Flash, 8 MB PSRAM)  
**Framework:** ESP-IDF v6.x (Bluedroid BTDM) + LVGL v9.5  
**Paradigma:** Orientado a Eventos (*Event-Driven*) com Atores sob Demanda (*Actor Model* sobre FreeRTOS Queues)

---

## 1. Visão Geral e Princípios Fundamentais

A arquitetura do Headset4 substitui o modelo clássico de acoplamento direto e chamadas entre módulos com locks recursivos por uma divisão estrita em dois planos operacionais:

1. **Dois Planos de Comunicação:**
   - **Plano de Controle:**
     - **Fatos (Eventos):** Transmitidos via barramento de eventos centralizado (`esp_event`). Qualquer módulo pode publicar fatos passados (`SENSOR_EVT_WORN`, `BT_EVT_LINK_UP`). Handlers são não-bloqueantes e executam em menos de 2 ms.
     - **Ordens (Comandos):** Enviados para a fila privada (`FreeRTOS Queue`) do único dono do recurso (Actor). Exemplo: apenas o ator `audio` manipula I2S e WM8960; apenas o ator `bt_link` gerencia a máquina de estados Bluetooth.
   - **Plano de Dados (Streaming Contínuo):**
     - O tráfego contínuo de áudio PCM (A2DP SBC Decode, HFP mSBC/CVSD, ringbuffers e I2S DMA) e o pipeline de desenho/flush do LVGL operam em canais diretos de alta velocidade, **fora** do barramento de eventos.

2. **Um Recurso, Um Dono:**
   - **Áudio / Codec:** Exclusividade do ator `audio`.
   - **Pilha Bluetooth e Slots:** Exclusividade do ator `bt_link`.
   - **NVS (Flash Não-Volátil):** Exclusividade do ator `settings`. Nenhuma outra parte do código chama `nvs_open()`.
   - **Display / Objetos LVGL:** Exclusividade da task `display_task` através do modelo reativo `ui_model.c`.
   - **Atuadores (LEDC / PWM / Haptic):** Exclusividade do ator `actuators`.

3. **Ciclo de Vida sob Demanda:**
   - Tarefas raras ou pontuais (módulo criptográfico do Fast Pair, controle BLE via aplicativo de smartphone, atuadores vibracall/servos) nascem no primeiro comando e são finalizadas automaticamente após tempo de inatividade (`idle_ms`), poupando RAM e ciclos de CPU.

---

## 2. Catálogo Oficial de Eventos (`esp_event`)

Os eventos trafegam pelo laço padrão do sistema (`hs_event_loop`) com payloads limitados a $\le 64$ bytes:

| Base de Evento | ID do Evento | Estrutura de Payload | Publicador | Assinantes Típicos |
|---|---|---|---|---|
| **`SENSOR_EVT`** | `SENSOR_EVT_WORN` | N/A | `apds9930` | `bt_link`, `sfx`, `ui_model`, `phone_ctl` |
| | `SENSOR_EVT_REMOVED` | N/A | `apds9930` | `bt_link`, `sfx`, `ui_model`, `phone_ctl` |
| | `SENSOR_EVT_BUTTON_SHORT` | N/A | `board_button` | `bt_link`, `ui_model` |
| | `SENSOR_EVT_BUTTON_LONG` | N/A | `board_button` | `bt_link` (emparelhamento) |
| | `SENSOR_EVT_BATTERY` | `sensor_battery_evt_t` (mV, %, charging) | `battery` | `ui_model`, `phone_ctl`, `actuators` |
| **`BT_EVT`** | `BT_EVT_LINK_UP` | `bt_link_evt_t` (slot, bda, name, profiles) | `bt_a2dp`, `bt_hfp` | `bt_link`, `sfx`, `ui_model`, `phone_ctl` |
| | `BT_EVT_LINK_DOWN` | `bt_link_evt_t` (slot, bda, name, profiles) | `bt_a2dp`, `bt_hfp` | `bt_link`, `sfx`, `ui_model`, `phone_ctl` |
| | `BT_EVT_STREAMING` | `bt_streaming_evt_t` (slot, active) | `bt_a2dp` | `audio`, `ui_model`, `phone_ctl` |
| | `BT_EVT_CALL_STATE` | `bt_call_evt_t` (slot, state, number) | `bt_hfp` | `audio`, `ui_model`, `actuators` |
| | `BT_EVT_PAIRING_MODE` | `bt_pairing_evt_t` (active) | `bt_link` | `fp_crypto`, `ui_model` |
| | `BT_EVT_SLOT_CHANGED` | `bt_slot_changed_evt_t` (active_slot) | `bt_link` | `ui_model`, `phone_ctl` |
| | `BT_EVT_PEER_NAME` | `bt_peer_name_evt_t` (slot, name) | `bt_gap` | `ui_model`, `settings` |
| **`AUDIO_EVT`** | `AUDIO_EVT_VOLUME_CHANGED` | `audio_volume_evt_t` (vol_pct) | `audio` | `ui_model`, `phone_ctl`, `settings` |
| | `AUDIO_EVT_EQ_CHANGED` | `audio_eq_evt_t` (preset, gains[5]) | `audio` | `ui_model`, `phone_ctl`, `settings` |
| | `AUDIO_EVT_MODE_CHANGED` | `audio_mode_evt_t` (idle, a2dp, hfp) | `audio` | `ui_model`, `esp_pm` |
| | `AUDIO_EVT_TONE_DONE` | `audio_tone_evt_t` (tone_id) | `audio` | Subsistemas interessados |
| **`CFG_EVT`** | `CFG_EVT_SETTING_CHANGED` | `cfg_setting_changed_evt_t` (key) | `settings` | `audio`, `ui_model`, `actuators` |

---

## 3. Catálogo de Comandos por Actor (`hs_actor_send`)

Os comandos são ordens diretas enviadas à caixa de entrada do ator responsável:

### 3.1 Actor `audio` (Fixo · Core 1 · Prioridade 10)
- `AUDIO_CMD_START_MUSIC`: Inicia pipeline I2S DMA estéreo em 44.1 kHz para reprodução de música A2DP.
- `AUDIO_CMD_START_CALL`: Configura canal bidirecional I2S em 8/16 kHz para voz, ativando filtro NR e cancelamento de eco.
- `AUDIO_CMD_STOP`: Desativa I2S DMA e coloca o codec WM8960 em modo de economia de energia.
- `AUDIO_CMD_SET_VOLUME`: Ajusta ganho mestre dos amplificadores de saída.
- `AUDIO_CMD_SET_EQ`: Configura as bandas do equalizador por software.
- `AUDIO_CMD_PLAY_TONE`: Toca efeito sonoro sintetizado em buffer de alta prioridade.

### 3.2 Actor `bt_link` (Fixo · Core 0 · Prioridade 6)
- `BT_CMD_SELECT_SLOT`: Comuta o slot de conexão ativo (Multipoint A2DP/HFP).
- `BT_CMD_DISCONNECT`: Solicita o encerramento da conexão Bluetooth de um slot específico.
- `BT_CMD_START_PAIRING`: Coloca o dispositivo em modo visível e descobrível por janela temporizada.
- `BT_CMD_SET_AUTO_SWITCH`: Habilita/desabilita a troca automática de rota de áudio inteligente.

### 3.3 Actor `settings` (Fixo · Core 0 · Prioridade 3)
- `SETTINGS_CMD_SET`: Grava parâmetro tipado em NVS com mecanismo de *debounce* para poupar ciclos de escrita na Flash.
- `SETTINGS_CMD_GET`: Solicitação síncrona (request/reply via Direct Task Notification) para recuperar chave.
- `SETTINGS_CMD_COMMIT`: Força a persistência imediata das chaves modificadas.

### 3.4 Ator `actuators` (Sob Demanda · Core 0 · Prioridade 3 · `idle_ms = 2000`)
- `ACTUATOR_CMD_VIBRATE`: Executa padrão de vibração tátil no motor vibracall.
- `ACTUATOR_CMD_SET_BACKLIGHT`: Ajusta duty-cycle do backlight LCD via LEDC PWM.
- `ACTUATOR_CMD_SERVO_ANGLE`: Define o ângulo de atuação dos servos mecânicos das orelhas.

### 3.5 Ator `phone_ctl` (Sob Demanda · Core 0 · Prioridade 5 · `idle_ms = 60000`)
- Processa frames JSON recebidos do serviço GATT BLE do aplicativo oficial, decodifica a intenção e a converte nos comandos formais para os atores `audio`, `bt_link` e `settings`.

---

## 4. Gerenciamento de Energia e Power Management (`esp_pm`)

1. **DFS e Tickless Idle:**
   - O projeto suporta Dynamic Frequency Scaling e suspensão de ticks FreeRTOS.
2. **Tabela de Locks de Energia:**
   - O ator `audio` segura um lock `ESP_PM_APB_FREQ_MAX` (`audio_i2s`) durante streaming ou chamada ativa para garantir a estabilidade do clock APB necessário ao barramento DMA I2S.
   - Quando o áudio cessa, o lock é liberado e a CPU reduz sua frequência para 80 MHz / 40 MHz ou entra em sono leve.
3. **Bluetooth em Modo de Repouso:**
   - Para retenção de conexões Bluetooth em Light-sleep, o hardware requer o oscilador externo de 32.768 kHz no barramento RTC. Durante streaming contínuo de A2DP, a decodificação de pacotes e o I2S mantêm o sistema operando em modo ativo (~101 mA).

---

## 5. Estrutura de Diretórios Atualizada

```text
main/
├── CMakeLists.txt
├── Kconfig.projbuild
├── main.c
└── src/
    ├── audio/          # Actor audio, I2S DMA, codec WM8960, SFX e EQ
    ├── board/          # Inicialização de botões físicos e periféricos de placa
    ├── bt/             # Actor bt_link, A2DP, HFP, AVRCP, Fast Pair, BLE Ctl
    ├── core/           # Motor hs_actor, hs_events, hs_cmds, hs_bus_stats, settings
    ├── display/        # ui_model (LVGL Subjects), ui_bridge e display_task
    └── sensor/         # apds9930 (proximidade) e battery (ADC)
```
