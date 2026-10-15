# HeadSet4 — Smart Cyberpunk Hi-Fi Wireless Headset

> **Headset Bluetooth Inteligente de Alta Fidelidade com Interface Gráfica Touchscreen Integrada, Detecção de Uso e Expressividade Háptica.**  
> Projetado para o SoC **ESP32-WROVER-E** utilizando **ESP-IDF v6.x**, **FreeRTOS SMP Dual-Core** e **LVGL 9**.

---

## 🎧 O que é o HeadSet4?

O **HeadSet4** é um dispositivo de áudio vestível (*wearable*) autônomo e de código aberto que une a robustez de fones de ouvido comerciais topo de linha (como Apple AirPods Max ou Sony WH-1000XM) com recursos exclusivos de hardware e interação física:

- **Interface Visual Direta no Fone:** Display LCD TFT colorido de 2,8" sensível ao toque ([ILI9341](file:///a:/BackupDesktopFileBruno/Estudos/ESP/ESP32/headSet4/main/src/display/display.c)) com 11 telas interativas renderizadas em tempo real com **LVGL 9**.
- **Áudio Hi-Fi Dedicado:** Codec estéreo de alta fidelidade [WM8960](file:///a:/BackupDesktopFileBruno/Estudos/ESP/ESP32/headSet4/main/src/audio/wm8960.c) (Cirrus Logic/Wolfson) com oscilador local a cristal de 24 MHz, amplificador de fones de ouvido e entrada de microfone analógico.
- **Equalizador Paramétrico de 5 Bandas:** Ajuste fino de curvas sonoras (80 Hz a 8 kHz) em passos de 0,5 dB diretamente pela tela do fone ou pelo app do celular.
- **Detecção de Uso na Cabeça (*Wear Detection*):** Sensor óptico infravermelho [APDS-9930](file:///a:/BackupDesktopFileBruno/Estudos/ESP/ESP32/headSet4/main/src/sensor/apds9930.c) com histerese dinâmica inteligente ($th_{\text{off}} = 0{,}6 \cdot th_{\text{on}}$). Pausa a mídia e apaga o display ao retirar o fone; retoma e acende ao recolocar.
- **Conectividade Multiponto Dual-Slot:** Mantém conexão simultânea com dois dispositivos (ex: Notebook + Smartphone) com comutação automática de prioridade para chamadas telefônicas.
- **Pareamento Moderno em 1 Toque:** Suporte nativo a **Google Fast Pair** (com pop-up instantâneo no Android e Account Keys) e **Microsoft Swift Pair** (Windows 10/11).
- **Expressividade Háptica & Animatrônica:** Motor de vibração háptico (Vibracall) com envelopes de clique e alerta de chamadas, além de suporte a servomotores para movimento de "orelhas" animadas no arco.
- **App Companion BLE:** Canal de controle remoto bidirecional via protocolo JSON sobre Bluetooth Low Energy ([phone_ctl.c](file:///a:/BackupDesktopFileBruno/Estudos/ESP/ESP32/headSet4/main/src/bt/phone_ctl.c)).

---

## 🎯 Finalidade e Objetivos do Projeto

O objetivo do projeto é demonstrar a viabilidade de um fone inteligente de alta complexidade construído sobre silício acessível e firmware modular profissional, eliminando as limitações dos fones comerciais proprietários (firmwares fechados, ausência de tela, baterias descartáveis sem telemetria aberta).

| Recurso | Fones Comerciais Comuns | HeadSet4 (Este Projeto) |
| :--- | :---: | :---: |
| **Interface de Usuário** | Apenas LEDs e bips confusos | **Tela Colorida LCD de 2,8" Touchscreen com 11 telas LVGL 9** |
| **Arquitetura de Áudio** | Chip proprietário fechado | **Codec Cirrus WM8960 + I2S DMA + Compensação de Clock Drift** |
| **Equalização** | Presets estáticos no app | **5 bandas paramétricas (-12 a +12 dB) em passos de 0,5 dB na tela e via BLE** |
| **Sensor de Uso** | Apenas em modelos caros | **Sensor óptico APDS-9930 com histerese dinâmica anti-repiques** |
| **Multiponto (2 Slots)** | Difícil de alternar | **Auto-switching inteligente: chamada no celular pausa o PC automaticamente** |
| **Fast Pair & Swift Pair**| Exclusivo de grandes marcas | **Implementado com anti-spoofing ECDH/AES-128 e ADV dinâmico** |
| **Atuadores Físicos** | Somente bips de áudio | **Motor vibracall háptico com envelopes + controle de orelhas animadas** |
| **Código e Governança** | 100% proprietário e fechado | **Open-source com governança MAS (Multi-Agent Systems) e HIL via MCP** |

---

## 🛠️ Arquitetura de Hardware

```
                                  ESP32-WROVER-E
                       (Dual-Core Xtensa LX6 @ 240 MHz + 8MB PSRAM)
                                        │
        ┌───────────────────────────────┼───────────────────────────────┐
        ▼                               ▼                               ▼
[ Barramento I2S0 + I2C0 ]      [ Barramento SPI3 (VSPI) ]      [ Sensores & Atuadores ]
  - Codec WM8960 (I2C 0x1A)       - Display ILI9341 (40 MHz)      - APDS-9930 (I2C 0x39)
  - Cristal local 24 MHz          - Touch XPT2046 (1.5 MHz)       - Bateria (ADC1_CH6 + MOSFET)
  - Áudio Estéreo SBC / mSBC      - Backlight PWM (LEDC GPIO13)   - Vibracall Háptico (GPIO15)
  - Microfone com Voice NR                                        - Botão Multifunção (GPIO0)
```

Consulte o mapa completo de conexões e restrições elétricas em [main/src/board/pinout.h](file:///a:/BackupDesktopFileBruno/Estudos/ESP/ESP32/headSet4/main/src/board/pinout.h) e na skill dedicada [hardware-pinout](file:///a:/BackupDesktopFileBruno/Estudos/ESP/ESP32/headSet4/.agents/skills/hardware-pinout/SKILL.md).

---

## 🏛️ Arquitetura de Firmware & Princípios de Design

O firmware é regido pelas regras documentadas na fonte única de verdade [.agents/AGENTS.md](file:///a:/BackupDesktopFileBruno/Estudos/ESP/ESP32/headSet4/.agents/AGENTS.md):

1. **Separação Rígida em Dois Planos (Dual-Plane):**
   - **Plano de Controle:** Barramento assíncrono Publish/Subscribe [hs_events](file:///a:/BackupDesktopFileBruno/Estudos/ESP/ESP32/headSet4/main/src/core/hs_events.h) (`esp_event` loop `hs_ctrl_evt`) e comandos diretos 1-para-1 via micro-atores [hs_actor](file:///a:/BackupDesktopFileBruno/Estudos/ESP/ESP32/headSet4/main/src/core/hs_actor.h). Handlers executam sempre em menos de 2 milissegundos sem operações bloqueantes.
   - **Plano de Dados:** Áudio contínuo PCM I2S DMA e transferências SPI de display. **Nunca passa pelo barramento de eventos**, garantindo zero latência e imunidade contra estalos (*pops/clicks*).
2. **Isolamento Dual-Core SMP:**
   - **Core 0:** Controle, interface gráfica LVGL 9, pilha Bluetooth Bluedroid, Flash NVS e sensores.
   - **Core 1:** Processamento digital de sinais em tempo real (`audio_io` DMA com prioridade 18 e `act_audio` com prioridade 8).
3. **Dono Único por Recurso (Actor Model):**
   - NVS Flash é acessada exclusivamente pelo ator `act_settings` (com coalescência de escrita em RAM e *debounce* de 1,5 s para proteger a flash).
   - Codec WM8960 é controlado exclusivamente por `act_audio`.
   - LVGL 9 é manipulado exclusivamente pela tarefa `lvgl_task` com thread-safety estrito (`LV_USE_OS=NONE`).

---

## 📱 Telas da Interface Gráfica (LVGL 9)

A interface touchscreen possui 11 telas interativas organizadas em [components/ui/screens/](file:///a:/BackupDesktopFileBruno/Estudos/ESP/ESP32/headSet4/components/ui/screens/):

1. **`screen_main`**: Tela inicial com relógio, status de conexão Bluetooth, nível de bateria e controles de mídia (Play/Pause/Faixa).
2. **`screen_bluetooth`**: Gerenciador visual de slots (Slot 0 e Slot 1), alternância de dispositivo e botão de pareamento.
3. **`screen_volume`**: Controle visual de volume absoluto sincronizado de 0 a 127.
4. **`screen_equalizador`**: 5 sliders táteis para ganho paramétrico (-12 a +12 dB) e seleção de presets.
5. **`screen_display`**: Controle de brilho do backlight via PWM LEDC e temporizador de tela ociosa.
6. **`screen_sensores`**: Calibração da sensibilidade do sensor de presença APDS-9930.
7. **`screen_atuadores`**: Ajuste de intensidade do motor háptico e teste de vibração.
8. **`screen_bateria`**: Gráfico de descarga, tensão analógica em milivolts e telemetria de carga.
9. **`screen_galeria_imagens`**: Exibição de imagens em memória flash.
10. **`screen_galeria_gifs`**: Animações contextuais e expressividade visual.
11. **`screen_sobre`**: Versão do firmware, uptime e endereços MAC das interfaces de rádio.

---

## 🤖 Governança Multi-Agente & Skills

O desenvolvimento deste projeto é orquestrado sob o paradigma de **Sistemas Multi-Agentes (MAS)** pelo **Agente Orquestrador (A0)**, que utiliza **26 Skills Especializadas** em [.agents/skills/](file:///a:/BackupDesktopFileBruno/Estudos/ESP/ESP32/headSet4/.agents/skills/):

- **Áudio:** `audio-wm8960`, `audio-dsp-pipeline`
- **Bluetooth:** `bt-a2dp-sink`, `bt-avrcp-media`, `bt-hfp-hf`, `bluetooth-link-mgr`, `bluetooth-fastpair-ble`, `bt-ble-gap`, `ble-gatt-services`
- **Sensores & Atuadores:** `sensor-apds9930`, `actuator-vibracall`, `battery-adc`, `board-button`, `power-mgmt-dfs`
- **Interface Gráfica:** `lvgl-core-task`, `lvgl-reactive-model`, `lvgl-ui-bridge`, `lvgl-screens-nav`, `display-ili9341-touch`
- **Núcleo & Concorrência:** `core-hs-actor`, `core-hs-events`, `core-hs-cmds`, `freertos-smp`, `nvs-settings`, `hardware-pinout`
- **Validação de Bancada:** `hil-mcp-tooling` (Hardware-in-the-Loop via Model Context Protocol)

---

## 🚀 Como Compilar e Gravar

### Pré-requisitos
- **ESP-IDF v6.x** instalado e configurado no PATH do sistema.
- Placa ESP32-WROVER-E conectada via porta USB/Serial.

### Comandos de Build
```bash
# 1. Configurar o target para ESP32
idf.py set-target esp32

# 2. Compilar o firmware
idf.py build

# 3. Gravar na placa e abrir o monitor serial
idf.py -p COMx flash monitor
```

---

## 📄 Licença e Governança

Consulte o arquivo [.agents/AGENTS.md](file:///a:/BackupDesktopFileBruno/Estudos/ESP/ESP32/headSet4/.agents/AGENTS.md) para consultar o backlog de engenharia, decisões arquiteturais (ADRs) e regras de contribuição.
