# Documentação Técnica da API BLE GATT — HeadSet4
## Guia de Integração e Especificação para Aplicativo Android Companion

> **Versão do Firmware Alvo:** ESP-IDF v6.x · FreeRTOS SMP · LVGL 9  
> **Dispositivo:** ESP32-WROVER-E (Dual-Core Xtensa LX6 + 8 MB PSRAM)  
> **Módulo Firmware Responsável:** [phone_ctl.c](file:///a:/BackupDesktopFileBruno/Estudos/ESP/ESP32/headSet4/main/src/bt/phone_ctl.c) · [bt_ctl_service.c](file:///a:/BackupDesktopFileBruno/Estudos/ESP/ESP32/headSet4/main/src/bt/bt_ctl_service.c) · [bt_eq_service.c](file:///a:/BackupDesktopFileBruno/Estudos/ESP/ESP32/headSet4/main/src/bt/bt_eq_service.c) · [bt_bas_service.c](file:///a:/BackupDesktopFileBruno/Estudos/ESP/ESP32/headSet4/main/src/bt/bt_bas_service.c)  
> **Última Atualização:** Outubro de 2026  
> **Objetivo:** Especificação técnica e exaustiva de todos os serviços GATT, características, protocolo JSON bidirecional, dicionário de eventos de telemetria, mapeamento 1:1 com as 11 telas do display e diretrizes de concorrência e segurança para o desenvolvimento do app Android companion.

---

## 1. Visão Geral da Arquitetura de Comunicação BLE

O **HeadSet4** opera simultaneamente em topologia **Dual-Mode**:
1. **Bluetooth Classic (BR/EDR):** Streaming estéreo de alta fidelidade A2DP Sink (SBC), controle de mídia AVRCP Controller/Target (Absolute Volume, Passthrough, Cover Art BIP) e chamadas telefônicas HFP Hands-Free (CVSD/mSBC com Voice NR).
2. **Bluetooth Low Energy (BLE):** Telemetria em tempo real, controle remoto bidirecional via JSON, equalização de hardware de 5 bandas, serviço de bateria SIG e suporte a pareamento rápido (Google Fast Pair e Microsoft Swift Pair).

```
+-------------------------------------------------------------------------------+
|                      APLICATIVO ANDROID (COMPANION APP)                       |
+---------------------------------------+---------------------------------------+
                                        | BLE GATT (Link Criptografado SMP)
+=======================================V=======================================+
| SERVIÇOS GATT EXPOSTOS NO FIRMWARE                                            |
|                                                                               |
| 1. Serviço de Controle & Telemetria     (UUID Custom: 5E0A1C00-4D2F-...)      |
|    ├── Char RX (Comandos JSON - Write Encrypted, buffer 512B)                 |
|    └── Char TX (Respostas & Telemetria - Notify + CCCD 0x2902)                |
|                                                                               |
| 2. Serviço de Equalizador de Hardware   (UUID Custom: 5E0A1B00-4D2F-...)      |
|    ├── Char Preset (Read / Write: 0=Flat, 1=Bass, 2=Vocal, 3=Treble)          |
|    └── Char Band Gains (Read / Write: 5 bandas x int8, passos de 0.5 dB)      |
|                                                                               |
| 3. Serviço de Bateria Padrão SIG        (UUID 0x180F)                         |
|    └── Char Battery Level (UUID 0x2A19: Read / Notify 0..100%, 2 conexões)    |
|                                                                               |
| 4. Google Fast Pair Service (GFPS)     (UUID 0xFE2C)                         |
|    └── Descoberta por Pop-up nativo do Google Play Services (Key-based / SSP) |
+=======================================+=======================================+
                                        | Despacho Interno (hs_actor_send)
+=======================================V=======================================+
| ATORES E SUBSISTEMAS DO ESP32 (Arquitetura Event-Driven & Actor Model)        |
|  [act_phone_ctl] ──> [act_audio] | [act_bt_link] | [act_settings] | [actuators]  |
+-------------------------------------------------------------------------------+
```

### 1.1 Parâmetros de Conexão e Descoberta (Advertising Dinâmico)
O firmware ajusta os parâmetros de anúncio BLE dinamicamente com base no estado operacional do headset ([bt_ble.c](file:///a:/BackupDesktopFileBruno/Estudos/ESP/ESP32/headSet4/main/src/bt/bt_ble.c)):
- **Nome Anunciado (Complete / Shortened Local Name):** `"HeadSet4"` (ou configurado pelo usuário na Flash NVS).
- **Intervalos de Anúncio por Perfil:**
  - *Modo Pareamento (`BT_BLE_ADV_PAIRING`):* 80 ms a 100 ms (janela de 120 s para descoberta instantânea).
  - *Modo Ativo / Conectado (`BT_BLE_ADV_ACTIVE`):* 500 ms a 620 ms.
  - *Modo Streaming A2DP:* 2,0 s (preservação do throughput dos pacotes de áudio SBC no rádio compartilhado).
  - *Modo Repouso / Standby (`BT_BLE_ADV_SLEEP`):* 1,28 s (economia profunda de energia).
- **Negociação de MTU:** O firmware aceita MTU estendido de até **512 bytes**. O app Android deve obrigatoriamente chamar `gatt.requestMtu(512)` logo após conectar (`onConnectionStateChange`) para viabilizar envio e recebimento de payloads JSON em pacote único sem fragmentação.

### 1.2 Segurança Criptográfica Mandatória (SMP / Bonding)
- A característica de comando RX possui permissão restrita: **`ESP_GATT_PERM_WRITE_ENCRYPTED`**.
- Se o smartphone tentar enviar comandos sem link autenticado, o ESP32 rejeitará a operação retornando o erro GATT **`0x0F` (`ESP_GATT_INSUF_ENCRYPTION`)**.
- **Comportamento e Fluxo Obrigatório no Android:**
  1. O aplicativo deve iniciar o pareamento seguro chamando `device.createBond()` ou tratar o erro `GATT_INSUFFICIENT_ENCRYPTION` iniciando a autenticação.
  2. O ESP32 responde automaticamente aos eventos de segurança aceitando o vínculo com `ESP_BLE_SM_AUTH_GEN_BOND` e IO Capability `None` (Just Works) ou com confirmação no display.
  3. Somente após a confirmação do vínculo (`BluetoothDevice.BOND_BONDED`), as requisições de escrita serão aceitas pelo microcontrolador.

---

## 2. Mapa Completo de Serviços e Características GATT

Todos os UUIDs de 128-bits utilizam a base proprietária `5E0A1Cxx-4D2F-4A7B-9C31-0E5F2A6B7C8D`.

| Serviço | UUID | Descrição | Implementação C |
| :--- | :--- | :--- | :--- |
| **Controle & Telemetria** | `5E0A1C00-4D2F-4A7B-9C31-0E5F2A6B7C8D` | Canal bidirecional de comandos JSON e telemetria push | [bt_ctl_service.c](file:///a:/BackupDesktopFileBruno/Estudos/ESP/ESP32/headSet4/main/src/bt/bt_ctl_service.c) |
| **Equalizador Paramétrico** | `5E0A1B00-4D2F-4A7B-9C31-0E5F2A6B7C8D` | Controle direto de hardware das 5 bandas do codec WM8960 | [bt_eq_service.c](file:///a:/BackupDesktopFileBruno/Estudos/ESP/ESP32/headSet4/main/src/bt/bt_eq_service.c) |
| **Battery Service (BAS)** | `0000180F-0000-1000-8000-00805F9B34FB` (`0x180F`) | Serviço padrão Bluetooth SIG para indicador de bateria nativo | [bt_bas_service.c](file:///a:/BackupDesktopFileBruno/Estudos/ESP/ESP32/headSet4/main/src/bt/bt_bas_service.c) |
| **Google Fast Pair (GFPS)** | `0000FE2C-0000-1000-8000-00805F9B34FB` (`0xFE2C`) | Serviço para pop-up de pareamento rápido e Account Keys | [bt_fastpair.c](file:///a:/BackupDesktopFileBruno/Estudos/ESP/ESP32/headSet4/main/src/bt/bt_fastpair.c) |

---

### 2.1 Detalhamento das Características

#### Serviço 1: Controle e Telemetria (`5E0A1C00-...`)

| Característica | UUID | Propriedades | Permissões | Detalhes & Formato |
| :--- | :--- | :--- | :--- | :--- |
| **Comando (RX)** | `5E0A1C01-4D2F-4A7B-9C31-0E5F2A6B7C8D` | `WRITE` | `PERM_WRITE_ENCRYPTED` | Strings em formato **JSON** (até 512 bytes). Comandos enviados do App para o fone. Suporta Long Write. |
| **Telemetria/Resposta (TX)**| `5E0A1C02-4D2F-4A7B-9C31-0E5F2A6B7C8D` | `NOTIFY`, `READ` | `PERM_READ` | Strings em formato **JSON**. Respostas aos comandos e notificações assíncronas do fone. |
| **CCCD da Telemetria** | `00002902-0000-1000-8000-00805F9B34FB` (`0x2902`) | `READ`, `WRITE` | `PERM_READ \| WRITE` | Escrever `0x0001` (`[0x01, 0x00]`) para habilitar notificações. |

#### Serviço 2: Equalizador Paramétrico de Hardware (`5E0A1B00-...`)

| Característica | UUID | Propriedades | Permissões | Formato de Dados |
| :--- | :--- | :--- | :--- | :--- |
| **Preset de EQ** | `5E0A1B01-4D2F-4A7B-9C31-0E5F2A6B7C8D` | `READ`, `WRITE` | `READ \| WRITE` | **1 byte (`uint8_t`):**<br>`0`: Flat (Neutro/Desativado)<br>`1`: Bass Boost (Reforço de graves)<br>`2`: Vocal Boost (Reforço de médios)<br>`3`: Treble Boost (Reforço de agudos)<br>`255` (`0xFF`): Modo Customizado |
| **Band Gains (5 Bandas)** | `5E0A1B02-4D2F-4A7B-9C31-0E5F2A6B7C8D` | `READ`, `WRITE` | `READ \| WRITE` | **5 bytes (`int8_t[5]`):**<br>Ganhos das 5 bandas em passos de **0,5 dB** (-24 a +24, correspondente a **-12 dB a +12 dB**).<br>Ao escrever nesta característica, o preset muda automaticamente para Custom (`0xFF`). |

**Topologia das 5 Bandas do Hardware WM8960:**
1. **Banda 1 (Low Shelf):** Corte ~100 Hz (Graves profundos / Sub-bass).
2. **Banda 2 (Peak):** Central ~300 Hz (Corpo vocal / Médios-graves).
3. **Banda 3 (Peak):** Central ~1 kHz (Clareza de voz / Médios centrais).
4. **Banda 4 (Peak):** Central ~3 kHz (Articulação / Médios-altos).
5. **Banda 5 (High Shelf):** Corte ~8 kHz a 12 kHz (Brilho / Ar / Agudos).

#### Serviço 3: Battery Service Padrão SIG (`0x180F`)

| Característica | UUID | Propriedades | Permissões | Formato de Dados |
| :--- | :--- | :--- | :--- | :--- |
| **Battery Level** | `00002A19-0000-1000-8000-00805F9B34FB` (`0x2A19`) | `READ`, `NOTIFY` | `PERM_READ` | **1 byte (`uint8_t`):** Nível de 0 a 100%. Integrado nativamente ao painel do Android. Suporta até 2 conexões simultâneas (`BAS_MAX_CONN = 2`). |
| **CCCD da Bateria** | `00002902-0000-1000-8000-00805F9B34FB` (`0x2902`) | `READ`, `WRITE` | `PERM_READ \| WRITE` | Escrever `0x0001` para receber notificações a cada atualização do ADC. |

---

## 3. Protocolo JSON de Mensagens

Todas as comunicações no canal de Controle (`5E0A1C01` / `5E0A1C02`) utilizam JSON delimitado por quebra de linha (`\n`) ou pelo fechamento de chaves (`}`).

### 3.1 Padrão de Requisição (`App -> Fone`)
```json
{"cmd": "<nome_do_comando>", "<param1>": <valor>, ...}\n
```

### 3.2 Padrão de Resposta Imediata (`Fone -> App`)
```json
{"status": "ok", "cmd": "<nome_do_comando>", ...}
```
Em caso de falha de validação ou parâmetro ausente/inválido:
```json
{"error": "motivo_do_erro", "cmd": "<nome_do_comando>"}
```

### 3.3 Padrão de Evento de Telemetria Push (`Fone -> App`)
Transmitido automaticamente via GATT Notify quando ocorrem alterações físicas ou de estado no fone:
```json
{"evt": "<categoria>.<nome_do_evento>", "data": { ... }}
```

---

## 4. Dicionário Completo de Telemetria (Eventos Publicados)

O firmware implementa *throttling* adaptativo de 500 ms (`NOTIFY_THROTTLE_MS = 500`) para rajadas de atualizações, assegurando estabilidade do canal de rádio BLE.

### 4.1 Eventos de Sensores e Hardware (`evt: "sensor.*"`)

#### 1. Fone Colocado na Cabeça (`sensor.worn`)
Publicado instantaneamente pelo sensor óptico infravermelho APDS-9930 quando o usuário coloca o headset:
```json
{
  "evt": "sensor.worn",
  "data": {
    "worn": true
  }
}
```

#### 2. Fone Retirado da Cabeça (`sensor.removed`)
Publicado quando o headset é retirado (dispara auto-pause de áudio no fone e corte de backlight):
```json
{
  "evt": "sensor.removed",
  "data": {
    "worn": false
  }
}
```

#### 3. Telemetria Detalhada de Bateria (`sensor.battery`)
Publicado periodicamente ou a cada alteração de degrau medido no conversor ADC1 com calibração por curva Li-Ion:
```json
{
  "evt": "sensor.battery",
  "data": {
    "pct": 87,
    "mv": 3980,
    "chg": false
  }
}
```
- `pct`: Nível percentual de carga (0 a 100%).
- `mv`: Tensão real da célula Li-Ion em milivolts (ex: 3980 mV = 3,98 V).
- `chg`: Booleano indicador de carregador USB conectado (`true` carregando, `false` descarregando).

---

### 4.2 Eventos de Bluetooth & Multiponto (`evt: "bt.*"`)

#### 1. Conexão Estabelecida (`bt.link_up`)
```json
{
  "evt": "bt.link_up",
  "data": {
    "slot": 0,
    "status": "connected"
  }
}
```
- `slot`: Índice do slot conectado (`0` = Dispositivo 1, `1` = Dispositivo 2).

#### 2. Desconexão de Link (`bt.link_down`)
```json
{
  "evt": "bt.link_down",
  "data": {
    "slot": 1,
    "status": "disconnected"
  }
}
```

#### 3. Mudança de Slot Ativo (`bt.slot_changed`)
Publicado quando o fone comuta o dispositivo que tem prioridade de áudio:
```json
{
  "evt": "bt.slot_changed",
  "data": {
    "active_slot": 0
  }
}
```

#### 4. Nome do Dispositivo Conectado (`bt.peer_name`)
Transmitido assim que o protocolo GAP / SDP resolve o nome amigável do smartphone ou computador pareado:
```json
{
  "evt": "bt.peer_name",
  "data": {
    "slot": 0,
    "name": "Galaxy S23 Ultra"
  }
}
```

---

### 4.3 Eventos de Áudio & Volume (`evt: "audio.*"`)

#### 1. Volume Master Alterado (`audio.volume`)
Disparado se o volume for ajustado no display touch, pelo botão físico ou pelo smartphone via AVRCP:
```json
{
  "evt": "audio.volume",
  "data": {
    "vol": 72,
    "mute": false
  }
}
```
- `vol`: Volume em escala linear perceptual de 0 a 100%.
- `mute`: `true` se áudio mutado no codec de hardware.

#### 2. Mudança de Modo de Áudio (`audio.mode`)
```json
{
  "evt": "audio.mode",
  "data": {
    "mode": 1,
    "rate": 44100
  }
}
```
- `mode`: Modo operacional do DSP de áudio:
  - `0`: IDLE (Repouso, codec em baixo consumo).
  - `1`: MUSIC_A2DP (Streaming estéreo de música).
  - `2`: CALL_HFP (Chamada telefônica de voz bidirecional com Voice NR ativado).
  - `3`: TONE_SFX (Efeito sonoro do sistema ou bipe).
- `rate`: Frequência de amostragem do barramento I2S em Hz (ex: 44100 Hz, 48000 Hz ou 16000 Hz em chamadas HD mSBC).

---

### 4.4 Eventos de Configuração Persistida (`evt: "cfg.*"`)

#### Parâmetro Alterado no NVS Flash (`cfg.changed`)
Publicado sempre que uma preferência do sistema é salva pelo motor de configurações:
```json
{
  "evt": "cfg.changed",
  "data": {
    "key": "disp_bright",
    "val": 75
  }
}
```
Chaves conhecidas: `disp_bright`, `disp_to`, `disp_img_idx`, `disp_gif_idx`, `sens_prox_en`, `vib_en`, `vib_intens`, `ears_angle`, `bt_autosw`.

---

### 4.5 Eventos de Seleção de Mídia e Galeria (`evt: "display.*"`)

#### 1. Papel de Parede Alterado no Headset (`display.wallpaper_changed`)
Publicado sempre que o usuário toca em um dos botões da galeria na tela [screen_galeria_imagens](file:///a:/BackupDesktopFileBruno/Estudos/ESP/ESP32/headSet4/components/ui/screens/screen_galeria_imagens.xml):
```json
{
  "evt": "display.wallpaper_changed",
  "data": {
    "active_idx": 2,
    "name": "Cyberpunk"
  }
}
```

#### 2. GIF Animado Alterado no Headset (`display.gif_changed`)
Publicado sempre que o usuário toca em um dos botões da galeria na tela [screen_galeria_gifs](file:///a:/BackupDesktopFileBruno/Estudos/ESP/ESP32/headSet4/components/ui/screens/screen_galeria_gifs.xml):
```json
{
  "evt": "display.gif_changed",
  "data": {
    "active_idx": 1,
    "name": "Pulso Eletrico"
  }
}
```


---

## 5. Mapeamento 1:1 entre as 11 Telas do Display e a API do App

Toda tela navegável no display LCD TFT de 2,8" possui comandos correspondentes no protocolo JSON para garantir paridade absoluta entre a interface do fone e o app Android.

```
+-----------------------------------------------------------------------------------+
| MAPEAMENTO: DISPLAY DO HEADSET  <=======>  API BLE DO APLICATIVO ANDROID         |
+--------------------+------------------------------+-------------------------------+
| Tela do Display    | Informação / Controle Físico | Comando JSON / Método GATT    |
+--------------------+------------------------------+-------------------------------+
| screen_status      | Snapshot completo do sistema | cmd: "get_all_status"         |
| screen_bluetooth   | Status slots 1 e 2           | Telemetria bt.link_up/down    |
| screen_bluetooth   | Desconectar Dispositivo 1    | cmd: "disconnect_slot",slot:0 |
| screen_bluetooth   | Desconectar Dispositivo 2    | cmd: "disconnect_slot",slot:1 |
| screen_bluetooth   | Alternar Slot de Áudio Ativo | cmd: "select_slot",slot:0/1   |
| screen_bluetooth   | Abrir Pareamento (120s)      | cmd: "start_pairing"          |
| screen_proximidade | Ativar/Desativar APDS-9930   | cmd: "set_prox_enable"        |
| screen_proximidade | Slider Sensibilidade (0-1023)| cmd: "set_prox_sensitivity"   |
| screen_vibracall   | Ativar/Desativar Háptica     | cmd: "set_vibracall"          |
| screen_vibracall   | Slider Intensidade (0-100%)  | cmd: "set_vibracall",val:XX   |
| screen_vibracall   | Notificação / Teste Vibração | cmd: "trigger_vibrate"        |
| screen_orelhas     | Ativar/Desativar Servomotores| cmd: "set_ears_enable"        |
| screen_orelhas     | Slider Ângulo (0 a 180 graus)| cmd: "set_ears_angle",val:XX  |
| screen_display     | Ligar/Desligar LCD (Sleep)   | cmd: "set_display_state"      |
| screen_display     | Slider Brilho Backlight PWM  | cmd: "set_brightness",val:XX  |
| screen_display     | Timeout de Tela (segundos)   | cmd: "set_display_timeout"    |
| screen_galeria_img | Papel de Parede Ativo (Idx)  | cmd: "set_wallpaper_idx"      |
| screen_galeria_gif | Animação GIF Ativa (Idx)     | cmd: "set_gif_idx"            |
| screen_settings    | Volume Master (0 a 100%)     | cmd: "set_volume",val:XX      |
| screen_settings    | Mudo do Codec (true/false)   | cmd: "set_mute",mute:XX       |
| screen_settings    | Equalizador 5 Bandas         | cmd: "set_eq_preset"/"gains"  |
| screen_settings    | Google Fast Pair Model ID    | cmd: "get/set_model_id"       |
+--------------------+------------------------------+-------------------------------+
```

---

## 6. Catálogo Detalhado de Comandos JSON (App -> HeadSet4)

### 6.1 Consulta Global de Estado: `get_all_status`
Solicita instantaneamente um snapshot completo de todas as variáveis do modelo de dados (`ui_model`). Ideal para chamar assim que o app completa a negociação de MTU e descoberta de serviços.

**Envio do App:**
```json
{"cmd": "get_all_status"}\n
```

**Resposta do Headset:**
```json
{
  "bt1": "Galaxy S23 Ultra",
  "bt2": "Notebook Dell",
  "st1": "Conectado",
  "st2": "Desconectado",
  "alt": true,
  "prox": "worn",
  "bat": 92,
  "disp": true,
  "brilho": 80,
  "vol": 65
}
```

---

### 6.2 Controle de Áudio, Volume e Mídia (AVRCP)

#### 1. Ajustar Volume Master: `set_volume`
Ajusta linearmente o ganho do codec WM8960 e atualiza o display:
```json
{"cmd": "set_volume", "val": 75}\n
```
- `val`: Número inteiro de `0` a `100`.

#### 2. Mutar ou Desmutar Áudio: `set_mute`
```json
{"cmd": "set_mute", "mute": true}\n
```
- `mute`: `true` para mutar o codec, `false` para desmutar.

#### 3. Controle de Reprodução Multimídia (AVRCP Passthrough)
Permite ao app Android enviar ordens de transporte de mídia que são despachadas via AVRCP para o reprodutor ativo do smartphone ou computador conectado via Bluetooth Classic:

- **Play/Pause Toggle:**
  ```json
  {"cmd": "media_play_pause"}\n
  ```
- **Play (Iniciar Reprodução):**
  ```json
  {"cmd": "media_play"}\n
  ```
- **Pause (Pausar Reprodução):**
  ```json
  {"cmd": "media_pause"}\n
  ```
- **Próxima Faixa (Next Track):**
  ```json
  {"cmd": "media_next"}\n
  ```
- **Faixa Anterior (Previous Track):**
  ```json
  {"cmd": "media_prev"}\n
  ```

---

### 6.3 Controle de Conexões Bluetooth Multiponto

#### 1. Selecionar Slot de Áudio Ativo: `select_slot`
Define qual dos dispositivos pareados enviará áudio para o fone:
```json
{"cmd": "select_slot", "slot": 0}\n
```
- `slot`: `0` para Slot 1, `1` para Slot 2.

#### 2. Desconectar Slot Manualmente: `disconnect_slot`
Equivalente a tocar no botão "Desconectar" na tela `screen_bluetooth`:
```json
{"cmd": "disconnect_slot", "slot": 1}\n
```

#### 3. Iniciar Modo de Pareamento: `start_pairing`
Abre visibilidade e descoberta Bluetooth Classic e BLE por 120 segundos para conectar novo dispositivo:
```json
{"cmd": "start_pairing"}\n
```

#### 4. Alternar Troca Automática Inteligente: `set_auto_switch`
Ativa ou desativa o recurso que comuta o slot automaticamente ao receber chamadas telefônicas:
```json
{"cmd": "set_auto_switch", "enable": true}\n
```

---

### 6.4 Controle de Display & Iluminação

#### 1. Ajustar Brilho do Display: `set_brightness`
Ajusta o ciclo de trabalho do PWM LEDC de 5 kHz no GPIO 13 com curva perceptual quadrática:
```json
{"cmd": "set_brightness", "val": 70}\n
```
- `val`: `0` a `100`%.

#### 2. Ligar ou Desligar Display (Modo Sleep): `set_display_state`
Suspende a renderização do display e desliga o backlight para economia máxima de bateria:
```json
{"cmd": "set_display_state", "enable": false}\n
```

#### 3. Tempo de Inatividade de Tela: `set_display_timeout`
Configura o tempo em segundos até a tela diminuir o brilho e suspender:
```json
{"cmd": "set_display_timeout", "val": 30}\n
```
- `val`: Segundos (`0` = tela sempre acesa, `15`, `30`, `60`, etc.).

---

### 6.5 Controle do Sensor de Proximidade (Uso na Cabeça)

#### 1. Habilitar Detecção no Ouvido: `set_prox_enable`
```json
{"cmd": "set_prox_enable", "enable": true}\n
```

#### 2. Ajustar Limiar / Sensibilidade: `set_prox_sensitivity`
Define a contagem de reflexão infravermelha necessária para detectar contato com a orelha:
```json
{"cmd": "set_prox_sensitivity", "val": 160}\n
```
- `val`: Valor numérico de `0` a `1023` (Padrão de calibração: `150` a `200`). O firmware aplica automaticamente histerese dinâmica de 40% ($th_{\text{off}} = 0{,}6 \cdot th_{\text{on}}$).

---

### 6.6 Atuador Háptico (Vibracall) & Notificações do Smartphone

#### 1. Configurar Habilitação e Intensidade Padrão: `set_vibracall`
```json
{"cmd": "set_vibracall", "enable": true, "val": 85}\n
```
- `enable`: `true` ou `false`.
- `val`: Intensidade PWM de `0` a `100`%.

#### 2. Disparar Vibração por Notificação Remota: `trigger_vibrate` ou `notification`
Permite que o aplicativo Android dispare envelopes de vibração no fone quando o smartphone receber mensagens ou alertas (WhatsApp, SMS, Chamada, Alarme):
```json
{
  "cmd": "trigger_vibrate",
  "duration_ms": 300,
  "intensity": 90
}\n
```
- `duration_ms`: Duração do pulso em milissegundos (ex: 200 ms para clique/notificação, 800 ms para alarme).
- `intensity`: Intensidade PWM (`0` a `100`%). Se `0`, utiliza a intensidade padrão salva nas configurações.
- **Resposta:** `{"status":"ok","cmd":"trigger_vibrate","vibrated":true}`

---

### 6.7 Servomotores das Orelhas Animatrônicas

#### 1. Habilitar / Desabilitar Movimento das Orelhas: `set_ears_enable`
```json
{"cmd": "set_ears_enable", "enable": true}\n
```

#### 2. Ajustar Ângulo Máximo de Expressividade: `set_ears_angle`
```json
{"cmd": "set_ears_angle", "val": 90}\n
```
- `val`: Ângulo em graus de `0` a `180`.

---

### 6.8 Personalização Visual (Wallpapers, Animações e Upload de Mídia)

> Para especificação detalhada de empacotamento, formatos RGB565/GIF89a e código Kotlin, consulte:  
> 🔗 [PROTOCOLO_TRANSFERENCIA_MIDIA_BLE.md](file:///a:/BackupDesktopFileBruno/Estudos/ESP/ESP32/headSet4/docs/PROTOCOLO_TRANSFERENCIA_MIDIA_BLE.md)

#### 1. Selecionar Papel de Parede Ativo (Principal): `set_wallpaper_idx`
```json
{"cmd": "set_wallpaper_idx", "val": 2}\n
```
- `val`: Índice da imagem na galeria (`0` a `5`). Persiste na Flash NVS e atualiza imediatamente a tela [screen_main](file:///a:/BackupDesktopFileBruno/Estudos/ESP/ESP32/headSet4/components/ui/screens/screen_main.xml).

#### 2. Selecionar GIF de Standby Ativo (Principal): `set_gif_idx`
```json
{"cmd": "set_gif_idx", "val": 1}\n
```
- `val`: Índice do GIF animado na galeria (`0` a `5`). Persiste na Flash NVS e atualiza a tela [screen_main_gif](file:///a:/BackupDesktopFileBruno/Estudos/ESP/ESP32/headSet4/components/ui/screens/screen_main_gif.xml).

#### 3. Consultar Informações da Galeria: `get_media_info`
```json
{"cmd": "get_media_info"}\n
```
- Retorna os slots ativos (`active_img`, `active_gif`) e o array detalhado dos 6 slots de imagem e 6 slots de GIF com nome, tamanho e flag `has_data`.

#### 4. Iniciar Upload de Novo Arquivo: `media_upload_start`
```json
{"cmd": "media_upload_start", "type": "image", "slot": 2, "size": 80000, "crc32": "D4E5F6A7", "name": "Cyberpunk"}\n
```
- Prepara o buffer de recepção na PSRAM e valida tamanho máximo permitido.

#### 5. Enviar Pedaço de Dados (Chunk): `media_chunk`
```json
{"cmd": "media_chunk", "seq": 0, "data": "<base64_string>"}\n
```

#### 6. Finalizar e Gravar na Memória NVS: `media_upload_finish`
```json
{"cmd": "media_upload_finish", "slot": 2}\n
```
- Valida o checksum CRC32 e comete o blob de mídia na Flash de forma definitiva.

#### 7. Apagar Mídia de um Slot: `delete_media_slot`
```json
{"cmd": "delete_media_slot", "type": "image", "slot": 2}\n
```


---

### 6.9 Configuração do Equalizador Paramétrico (Codec WM8960)

Além de escrever diretamente nas características binárias do Serviço `5E0A1B00-...`, o app pode utilizar comandos JSON:

#### 1. Selecionar Preset de EQ: `set_eq_preset`
```json
{"cmd": "set_eq_preset", "preset": 1}\n
```
- `preset`: `0` = Flat, `1` = Bass Boost, `2` = Vocal Boost, `3` = Treble Boost.

#### 2. Ajustar Ganhos das 5 Bandas via JSON: `set_eq_gains`
```json
{"cmd": "set_eq_gains", "gains": [4, 2, 0, -2, 6]}\n
```
- `gains`: Array com exatamente 5 números inteiros representando os ganhos em passos de 0.5 dB (-24 a +24).

---

### 6.10 Configuração do Google Fast Pair (Model ID)

#### 1. Consultar Model ID Atual: `get_model_id`
```json
{"cmd": "get_model_id"}\n
```
Resposta:
```json
{"status": "ok", "cmd": "get_model_id", "model_id": "F52A3B"}
```

#### 2. Atualizar Model ID: `set_model_id`
```json
{"cmd": "set_model_id", "val": "F52A3B"}\n
```
- `val`: String com 6 dígitos hexadecimais (3 bytes) do modelo cadastrado no Google Console.

---

### 6.11 Comando de Diagnóstico e Keepalive: `ping`

```json
{"cmd": "ping"}\n
```
Resposta:
```json
{"status": "ok", "cmd": "ping", "pong": true}
```

---

## 7. Considerações de Performance, Latência e Ciclo de Vida do Rádio

1. **Ciclo de Vida sob Demanda do Ator `phone_ctl`:**
   - O ator `phone_ctl` encerra sua task FreeRTOS por inatividade após 30 segundos sem cliente (`PHONE_CTL_IDLE_MS = 30000`), liberando 3584 bytes de memória RAM. Ao fechar o app, o ESP32 retorna ao modo de repouso dinâmico (PM/DFS).
2. **Coalescência de Sliders na Interface Móvel:**
   - Ao mover Sliders em Jetpack Compose (volume, brilho, equalizador, orelhas), aplique operadores de debounce de **100 ms a 150 ms** (`flow.debounce(100)`). Isso impede que rajadas de pacotes BLE saturem o buffer `s_incoming_buf` de 512 bytes e a fila do microcontrolador.
3. **Persistência Confiável no NVS Flash:**
   - O ator `act_settings` consolida escritas com debounce de **1500 ms** antes de gravar na memória Flash física, garantindo vida útil prolongada aos blocos da memória flash do ESP32.
4. **Fila Sequencial de Operações GATT no Android:**
   - A API nativa `BluetoothGatt` do Android não aceita operações paralelas concorrentes (disparar uma escrita antes da confirmação da anterior resulta em silêncio ou erro `GATT_FAILURE 133`). O app deve adotar uma **fila serializada de comandos GATT** (GattCommandQueue).
