# Documentação Técnica da API BLE GATT — HeadSet4
## Guia de Integração e Especificação para Aplicativo Android Companion

> **Versão do Firmware Alvo:** ESP-IDF v6.x · FreeRTOS SMP · LVGL 9  
> **Dispositivo:** ESP32-WROVER-E (Dual-Core Xtensa LX6 + 8 MB PSRAM)  
> **Última Atualização:** Outubro de 2026  
> **Objetivo:** Fornecer especificação exaustiva de todos os serviços GATT, características, protocolo JSON, eventos de telemetria publicados e mapeamento de comandos para espelhamento total das 11 telas e funções do display físico do fone.

---

## 1. Visão Geral da Arquitetura de Comunicação BLE

O **HeadSet4** opera simultaneamente em modo **Dual-Mode** (Bluetooth Classic para streaming de áudio estéreo A2DP e chamadas HFP; Bluetooth Low Energy para telemetria, pareamento rápido e controle remoto).

```
+-------------------------------------------------------------------------+
|                  APLICATIVO ANDROID (COMPANION APP)                    |
+------------------------------------+------------------------------------+
                                     | BLE GATT (Link Criptografado SMP)
+====================================V====================================+
| SERVIÇOS GATT EXPOSTOS NO FIRMWARE                                      |
|                                                                         |
| 1. Serviço de Controle & Telemetria  (UUID Custom: 5E0A1C00-...)        |
|    ├── Char RX (Comandos JSON - Write Encrypted)                        |
|    └── Char TX (Respostas & Telemetria - Notify + CCCD)                 |
|                                                                         |
| 2. Serviço de Equalizador            (UUID Custom: 5E0A1B00-...)        |
|    ├── Char Preset (Read / Write: 0 a 3)                                |
|    └── Char Band Gains (Read / Write: 5 bandas x int8, passos 0.5 dB)   |
|                                                                         |
| 3. Serviço de Bateria Padrão SIG     (UUID 0x180F)                      |
|    └── Char Battery Level (UUID 0x2A19: Read / Notify 0..100%)          |
|                                                                         |
| 4. Google Fast Pair Service          (UUID 0xFE2C)                      |
|    └── Descoberta por Pop-up nativo do Google Play Services             |
+====================================+====================================+
                                     | Despacho Interno (hs_actor_send)
+====================================V====================================+
| ATORES E SUBSISTEMAS DO ESP32                                          |
|  [act_phone_ctl] ──> [act_audio] | [act_bt_link] | [settings] | [actuators] |
+-------------------------------------------------------------------------+
```

### 1.1 Parâmetros de Conexão e Descoberta (Advertising)
- **Nome Anunciado (Complete Local Name):** `"HeadSet4"` (ou configurado no NVS via `SETTINGS_KEY_BT_NAME`).
- **Intervalos de Anúncio Dinâmicos:**
  - *Modo Pareamento (Pairing):* 80 ms a 100 ms (anúncio rápido para descoberta instantânea).
  - *Modo Ativo / Conectado (Active):* 500 ms a 620 ms (otimização de canal de rádio).
  - *Modo Streaming A2DP:* 2,0 s (preservação de throughput dos pacotes de áudio SBC).
  - *Modo Repouso (Sleep):* 1,28 s (economia profunda de bateria).
- **Negociação de MTU:** O firmware aceita e recomenda MTU estendido de **512 bytes** (`gatt.requestMtu(512)` no Android). Isso permite a transmissão de snapshots completos e comandos sem necessidade de fragmentação de pacotes.

### 1.2 Segurança Criptográfica Mandatória (SMP / Bonding)
- A característica de comando RX possui permissão restrita: **`ESP_GATT_PERM_WRITE_ENCRYPTED`**.
- Se o smartphone tentar enviar comandos sem conexão autenticada, o ESP32 rejeitará o pacote retornando status de erro GATT **`0x0F` (`ESP_GATT_INSUF_ENCRYPTION`)**.
- **Regra de Implementação no Android:**
  1. O aplicativo deve disparar o pareamento seguro chamando `device.createBond()` ou tratar o erro de insuficiência iniciando a autenticação.
  2. O ESP32 responde automaticamente a `ESP_GAP_BLE_SEC_REQ_EVT` aceitando o vínculo com `ESP_BLE_SM_AUTH_GEN_BOND` e IO Capability `None` (Just Works) ou com confirmação no display.
  3. Somente após o estado de vínculo mudar para `BOND_BONDED`, as escritas serão aceitas.

---

## 2. Mapa Completo de Serviços e Características GATT

Todos os UUIDs de 128-bits utilizam a base proprietária `5E0A1Cxx-4D2F-4A7B-9C31-0E5F2A6B7C8D`.

| Serviço | UUID | Descrição |
| :--- | :--- | :--- |
| **Controle & Telemetria** | `5E0A1C00-4D2F-4A7B-9C31-0E5F2A6B7C8D` | Canal principal bidirecional para comandos JSON e telemetria |
| **Equalizador Paramétrico** | `5E0A1B00-4D2F-4A7B-9C31-0E5F2A6B7C8D` | Controle direto de hardware do EQ de 5 bandas do codec WM8960 |
| **Battery Service (BAS)** | `0000180F-0000-1000-8000-00805F9B34FB` (`0x180F`) | Serviço padrão Bluetooth SIG para telemetria nativa do Android |
| **Google Fast Pair (GFPS)** | `0000FE2C-0000-1000-8000-00805F9B34FB` (`0xFE2C`) | Serviço para pop-up automático de pareamento rápido Android |

---

### 2.1 Detalhamento das Características

#### Serviço 1: Controle e Telemetria (`5E0A1C00-...`)

| Característica | UUID | Propriedades | Permissões | Payload & Descrição |
| :--- | :--- | :--- | :--- | :--- |
| **Comando (RX)** | `5E0A1C01-4D2F-4A7B-9C31-0E5F2A6B7C8D` | `WRITE` | `PERM_WRITE_ENCRYPTED` | Strings em formato **JSON** (até 512 bytes). Comandos enviados do App para o fone. |
| **Telemetria/Resposta (TX)**| `5E0A1C02-4D2F-4A7B-9C31-0E5F2A6B7C8D` | `NOTIFY`, `READ` | `PERM_READ` | Strings em formato **JSON**. Respostas aos comandos e notificações de eventos do fone. |
| **CCCD da Telemetria** | `00002902-0000-1000-8000-00805F9B34FB` (`0x2902`) | `READ`, `WRITE` | `PERM_READ \| WRITE` | Escrever `0x0001` (Little-Endian: `[0x01, 0x00]`) para habilitar notificações. |

#### Serviço 2: Equalizador Paramétrico (`5E0A1B00-...`)

| Característica | UUID | Propriedades | Formato de Dados |
| :--- | :--- | :--- | :--- |
| **Preset de EQ** | `5E0A1B01-4D2F-4A7B-9C31-0E5F2A6B7C8D` | `READ`, `WRITE` | **1 byte (`uint8_t`):**<br>`0`: Flat (Desativado/Neutro)<br>`1`: Bass Boost (Reforço de graves)<br>`2`: Vocal Boost (Reforço de médios/voz)<br>`3`: Treble Boost (Reforço de agudos) |
| **Band Gains (5 Bandas)** | `5E0A1B02-4D2F-4A7B-9C31-0E5F2A6B7C8D` | `READ`, `WRITE` | **5 bytes (`int8_t[5]`):**<br>Ganhos das 5 bandas em passos de **0,5 dB** (-24 a +24, correspondente a **-12 dB a +12 dB**).<br>Ao escrever nesta característica, o preset muda automaticamente para Custom (`0xFF`). |

**Frequências Centrais das 5 Bandas do Hardware WM8960:**
1. **Banda 1 (Graves):** Frequência de corte baixa ~100 Hz (Low Shelf)
2. **Banda 2 (Médios-Graves):** ~300 Hz (Peak Filter)
3. **Banda 3 (Médios):** ~1 kHz (Peak Filter)
4. **Banda 4 (Médios-Altos):** ~3 kHz (Peak Filter)
5. **Banda 5 (Agudos):** ~8 kHz a 12 kHz (High Shelf)

#### Serviço 3: Battery Service Padrão SIG (`0x180F`)

| Característica | UUID | Propriedades | Formato de Dados |
| :--- | :--- | :--- | :--- |
| **Battery Level** | `00002A19-0000-1000-8000-00805F9B34FB` (`0x2A19`) | `READ`, `NOTIFY` | **1 byte (`uint8_t`):** Nível percentual de 0 a 100%. Exibido nativamente no Android. |

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
Em caso de falha de validação ou erro interno:
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

O firmware implementa *throttling* adaptativo de 500 ms para rajadas de atualizações, assegurando fluidez sem perda de pacotes no enlace BLE.

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
Publicado periodicamente ou a cada degrau de medição do conversor ADC1:
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
- `pct`: Nível de carga da bateria (0 a 100%).
- `mv`: Tensão real da célula Li-Ion em milivolts (ex: 3980 mV = 3,98 V).
- `chg`: Indicador booleano de cabo/carregador conectado (`true` carregando, `false` descarregando).

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
  - `1`: MUSIC_A2DP (Streaming estéreo de música de alta qualidade).
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
Chaves conhecidas: `disp_bright`, `disp_to`, `sens_prox_en`, `vib_en`, `vib_intens`, `ears_angle`, `bt_autosw`.

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
| screen_settings    | Equalizador 5 Bandas         | Serviço GATT 5E0A1B00-...     |
| screen_settings    | Google Fast Pair Model ID    | cmd: "get/set_model_id"       |
+--------------------+------------------------------+-------------------------------+
```

---

## 6. Catálogo Detalhado de Comandos JSON (App -> HeadSet4)

### 6.1 Consulta Global de Estado: `get_all_status`
Solicita instantaneamente um snapshot completo de todas as variáveis do modelo de dados (`ui_model`). Ideal para chamar assim que o app conecta.

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

### 6.2 Controle de Áudio & Volume

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
Permite que o aplicativo Android dispare envelopes de vibração no fone quando o smartphone receber mensagens (WhatsApp, SMS, Chamada, Alarme):
```json
{
  "cmd": "trigger_vibrate",
  "duration_ms": 300,
  "intensity": 90
}\n
```
- `duration_ms`: Duração do pulso em milissegundos (ex: 200 ms para clique/notificação, 800 ms para alarme).
- `intensity`: Intensidade PWM (`0` a `100`%). Se `0`, utiliza a intensidade padrão salva nas configurações.

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

### 6.8 Personalização Visual (Wallpapers e Animações)

#### 1. Selecionar Papel de Parede Estático: `set_wallpaper_idx`
```json
{"cmd": "set_wallpaper_idx", "val": 2}\n
```
- `val`: Índice da imagem na galeria (0 a N-1).

#### 2. Selecionar GIF de Standby: `set_gif_idx`
```json
{"cmd": "set_gif_idx", "val": 1}\n
```
- `val`: Índice do GIF animado na galeria (0 a N-1).

---

### 6.9 Configuração do Google Fast Pair (Model ID)

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

## 7. Guia Prático de Implementação Android (Kotlin)

Abaixo está o código-fonte de referência pronto para produção no Android, encapsulando conexão BLE, negociação segura SMP, leitura de bateria e despacho de comandos JSON.

### 7.1 Constantes de Atributos GATT (`HeadSetGattAttributes.kt`)
```kotlin
package com.bruno.headset4.ble

import java.util.UUID

object HeadSetGattAttributes {
    // Serviço Principal de Controle e Telemetria
    val UUID_SERVICE_CONTROL: UUID = UUID.fromString("5E0A1C00-4D2F-4A7B-9C31-0E5F2A6B7C8D")
    val UUID_CHAR_COMMAND_RX: UUID = UUID.fromString("5E0A1C01-4D2F-4A7B-9C31-0E5F2A6B7C8D")
    val UUID_CHAR_NOTIFY_TX: UUID  = UUID.fromString("5E0A1C02-4D2F-4A7B-9C31-0E5F2A6B7C8D")
    
    // Serviço de Equalizador de Hardware
    val UUID_SERVICE_EQ: UUID      = UUID.fromString("5E0A1B00-4D2F-4A7B-9C31-0E5F2A6B7C8D")
    val UUID_CHAR_EQ_PRESET: UUID  = UUID.fromString("5E0A1B01-4D2F-4A7B-9C31-0E5F2A6B7C8D")
    val UUID_CHAR_EQ_GAINS: UUID   = UUID.fromString("5E0A1B02-4D2F-4A7B-9C31-0E5F2A6B7C8D")

    // Serviço Padrão Bluetooth SIG Battery Service (BAS)
    val UUID_SERVICE_BATTERY: UUID = UUID.fromString("0000180F-0000-1000-8000-00805F9B34FB")
    val UUID_CHAR_BATTERY_LEVEL: UUID = UUID.fromString("00002A19-0000-1000-8000-00805F9B34FB")

    // Descritor CCCD Padrão
    val UUID_DESCRIPTOR_CCCD: UUID = UUID.fromString("00002902-0000-1000-8000-00805F9B34FB")
}
```

---

### 7.2 Gerenciador de Comunicação BLE (`HeadSetBleManager.kt`)
```kotlin
package com.bruno.headset4.ble

import android.annotation.SuppressLint
import android.bluetooth.*
import android.content.Context
import android.util.Log
import kotlinx.coroutines.flow.MutableSharedFlow
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.asSharedFlow
import kotlinx.coroutines.flow.asStateFlow
import org.json.JSONObject
import java.nio.charset.StandardCharsets

@SuppressLint("MissingPermission")
class HeadSetBleManager(private val context: Context) {

    private val TAG = "HeadSetBleManager"

    private var bluetoothGatt: BluetoothGatt? = null
    private var cmdRxCharacteristic: BluetoothGattCharacteristic? = null
    private var eqGainsCharacteristic: BluetoothGattCharacteristic? = null

    // Estados e fluxos reativos expostos para a UI Jetpack Compose / ViewModel
    private val _isConnected = MutableStateFlow(false)
    val isConnected = _isConnected.asStateFlow()

    private val _batteryLevel = MutableStateFlow(0)
    val batteryLevel = _batteryLevel.asStateFlow()

    private val _telemetryEvents = MutableSharedFlow<String>(extraBufferCapacity = 64)
    val telemetryEvents = _telemetryEvents.asSharedFlow()

    private val gattCallback = object : BluetoothGattCallback() {

        override fun onConnectionStateChange(gatt: BluetoothGatt, status: Int, newState: Int) {
            if (newState == BluetoothProfile.STATE_CONNECTED) {
                Log.i(TAG, "Conectado ao HeadSet4! Solicitando MTU de 512 bytes...")
                _isConnected.value = true
                bluetoothGatt = gatt
                // Solicita MTU alto para transmitir JSONs em pacotes únicos
                gatt.requestMtu(512)
            } else if (newState == BluetoothProfile.STATE_DISCONNECTED) {
                Log.w(TAG, "HeadSet4 desconectado.")
                _isConnected.value = false
                bluetoothGatt = null
                cmdRxCharacteristic = null
            }
        }

        override fun onMtuChanged(gatt: BluetoothGatt, mtu: Int, status: Int) {
            Log.i(TAG, "MTU negociado com sucesso: $mtu bytes. Descobrindo serviços...")
            gatt.discoverServices()
        }

        override fun onServicesDiscovered(gatt: BluetoothGatt, status: Int) {
            if (status != BluetoothGatt.GATT_SUCCESS) {
                Log.e(TAG, "Falha na descoberta de serviços GATT: $status")
                return
            }

            // Localiza Serviço de Controle
            val ctlService = gatt.getService(HeadSetGattAttributes.UUID_SERVICE_CONTROL)
            if (ctlService != null) {
                cmdRxCharacteristic = ctlService.getCharacteristic(HeadSetGattAttributes.UUID_CHAR_COMMAND_RX)
                val notifyTx = ctlService.getCharacteristic(HeadSetGattAttributes.UUID_CHAR_NOTIFY_TX)

                // Habilita Notificações no TX de Telemetria
                if (notifyTx != null) {
                    enableNotifications(gatt, notifyTx)
                }
            }

            // Localiza Serviço de Equalizador
            val eqService = gatt.getService(HeadSetGattAttributes.UUID_SERVICE_EQ)
            if (eqService != null) {
                eqGainsCharacteristic = eqService.getCharacteristic(HeadSetGattAttributes.UUID_CHAR_EQ_GAINS)
            }

            // Lê Bateria Inicial
            val batService = gatt.getService(HeadSetGattAttributes.UUID_SERVICE_BATTERY)
            val batLevelChar = batService?.getCharacteristic(HeadSetGattAttributes.UUID_CHAR_BATTERY_LEVEL)
            if (batLevelChar != null) {
                gatt.readCharacteristic(batLevelChar)
            }

            // Solicita Status Global Imediato
            getAllStatus()
        }

        override fun onCharacteristicChanged(
            gatt: BluetoothGatt,
            characteristic: BluetoothGattCharacteristic,
            value: ByteArray
        ) {
            val jsonStr = String(value, StandardCharsets.UTF_8).trim()
            Log.d(TAG, "Notificação recebida do HeadSet4: $jsonStr")

            // Emite para o fluxo compartilhado para a ViewModel tratar
            _telemetryEvents.tryEmit(jsonStr)

            // Trata eventos críticos de forma direta
            try {
                val json = JSONObject(jsonStr)
                if (json.has("evt")) {
                    val evt = json.getString("evt")
                    if (evt == "sensor.battery") {
                        val data = json.getJSONObject("data")
                        _batteryLevel.value = data.getInt("pct")
                    }
                }
            } catch (e: Exception) {
                Log.w(TAG, "Aviso ao analisar JSON de evento: ${e.message}")
            }
        }

        override fun onCharacteristicRead(
            gatt: BluetoothGatt,
            characteristic: BluetoothGattCharacteristic,
            value: ByteArray,
            status: Int
        ) {
            if (status == BluetoothGatt.GATT_SUCCESS &&
                characteristic.uuid == HeadSetGattAttributes.UUID_CHAR_BATTERY_LEVEL &&
                value.isNotEmpty()
            ) {
                _batteryLevel.value = value[0].toInt() and 0xFF
                Log.i(TAG, "Leitura de Bateria inicial: ${_batteryLevel.value}%")
            }
        }
    }

    /**
     * Habilita Notificações GATT configurando o descritor CCCD (0x2902).
     */
    private fun enableNotifications(gatt: BluetoothGatt, characteristic: BluetoothGattCharacteristic) {
        gatt.setCharacteristicNotification(characteristic, true)
        val descriptor = characteristic.getDescriptor(HeadSetGattAttributes.UUID_DESCRIPTOR_CCCD)
        if (descriptor != null) {
            descriptor.value = BluetoothGattDescriptor.ENABLE_NOTIFICATION_VALUE
            gatt.writeDescriptor(descriptor)
            Log.i(TAG, "CCCD habilitado para ${characteristic.uuid}")
        }
    }

    /**
     * Envia comando JSON para a característica RX com delimitação '\n'.
     */
    fun sendJsonCommand(jsonPayload: String): Boolean {
        val gatt = bluetoothGatt ?: return false
        val charRx = cmdRxCharacteristic ?: return false

        // Garante que o payload termine com \n
        val formattedMsg = if (jsonPayload.endsWith("\n")) jsonPayload else "$jsonPayload\n"
        val bytes = formattedMsg.toByteArray(StandardCharsets.UTF_8)

        charRx.writeType = BluetoothGattCharacteristic.WRITE_TYPE_DEFAULT
        charRx.value = bytes

        val success = gatt.writeCharacteristic(charRx)
        if (!success) {
            Log.e(TAG, "Erro ao enfileirar escrita de comando no Android!")
        }
        return success
    }

    // ========================================================================
    // MÉTODOS DE CONVENIÊNCIA (API ALTO NÍVEL PARA VIEWMODELS)
    // ========================================================================

    fun getAllStatus() = sendJsonCommand("""{"cmd":"get_all_status"}""")

    fun setVolume(volume0to100: Int) =
        sendJsonCommand("""{"cmd":"set_volume","val":$volume0to100}""")

    fun setBrightness(brightness0to100: Int) =
        sendJsonCommand("""{"cmd":"set_brightness","val":$brightness0to100}""")

    fun setDisplayState(turnOn: Boolean) =
        sendJsonCommand("""{"cmd":"set_display_state","enable":$turnOn}""")

    fun setProximityEnable(enable: Boolean) =
        sendJsonCommand("""{"cmd":"set_prox_enable","enable":$enable}""")

    fun setProximitySensitivity(threshold0to1023: Int) =
        sendJsonCommand("""{"cmd":"set_prox_sensitivity","val":$threshold0to1023}""")

    fun setVibracall(enable: Boolean, intensity0to100: Int) =
        sendJsonCommand("""{"cmd":"set_vibracall","enable":$enable,"val":$intensity0to100}""")

    fun triggerVibrationNotification(durationMs: Int = 300, intensity: Int = 85) =
        sendJsonCommand("""{"cmd":"trigger_vibrate","duration_ms":$durationMs,"intensity":$intensity}""")

    fun setEarsAngle(angle0to180: Int) =
        sendJsonCommand("""{"cmd":"set_ears_angle","val":$angle0to180}""")

    fun selectBtSlot(slot: Int) =
        sendJsonCommand("""{"cmd":"select_slot","slot":$slot}""")

    fun disconnectBtSlot(slot: Int) =
        sendJsonCommand("""{"cmd":"disconnect_slot","slot":$slot}""")

    fun startPairing() =
        sendJsonCommand("""{"cmd":"start_pairing"}""")

    /**
     * Ajusta os ganhos das 5 bandas do equalizador diretamente no serviço de hardware.
     * @param gainsDb Array com 5 ganhos em dB (-12 a +12 dB).
     */
    fun setEqualizerGains(gainsDb: IntArray): Boolean {
        if (gainsDb.size != 5) return false
        val gatt = bluetoothGatt ?: return false
        val charGains = eqGainsCharacteristic ?: return false

        // Converte cada ganho em dB (-12 a +12) para passos de 0.5 dB (-24 a +24)
        val payload = ByteArray(5)
        for (i in 0 until 5) {
            val steps = (gainsDb[i] * 2).coerceIn(-24, 24)
            payload[i] = steps.toByte()
        }

        charGains.writeType = BluetoothGattCharacteristic.WRITE_TYPE_DEFAULT
        charGains.value = payload
        return gatt.writeCharacteristic(charGains)
    }
}
```

---

## 8. Considerações de Performance, Latência e Segurança

1. **Economia de Bateria e Vida Útil do Rádio:**
   - O ator `phone_ctl` no firmware ESP32 é **sob demanda** com desligamento por inatividade após 30 segundos (`PHONE_CTL_IDLE_MS = 30000`). Ao manter o aplicativo Android aberto em segundo plano sem necessidade de telemetria, pause o envio contínuo de mensagens para permitir que o ESP32 reduza frequência de clock dinamicamente (PM/DFS) e desligue a task auxiliar.
2. **Coalescência de Sliders na Interface Móvel:**
   - Ao criar Sliders em Jetpack Compose para ajuste de volume, brilho ou orelhas animadas, utilize operadores de debounce de **100 ms a 150 ms** (`flow.debounce(100)`). Isso impede que o arrasto contínuo do dedo no smartphone gere rajadas de pacotes BLE que possam saturar a fila do microcontrolador.
3. **Persistência Confiável no NVS Flash:**
   - Comandos que alteram configurações (ex: volume, brilho, sensibilidade e orelhas) são automaticamente consolidados pelo ator `act_settings` com um atraso de debounce de **1500 ms** antes de gravar na memória flash física, preservando os blocos da memória flash contra desgaste prematuro.
