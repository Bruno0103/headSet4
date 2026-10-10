# POWER.md — Análise de Consumo e Tabela de Power Management Locks

**Projeto:** `Headset4` (ESP32-WROVER-E · ESP-IDF v6.x / Bluedroid BTDM · LVGL 9.5)  
**Agente Responsável:** Agente A8 (Energia, Power Management e QA)  
**Data:** 10 de Outubro de 2026  
**Status:** WP 8.2 & WP 8.3 Concluídos

---

## 1. Tabela de Governança de Locks (`esp_pm_lock`) por Actor (WP 8.2)

A infraestrutura de gerenciamento de energia (`esp_pm`) no ESP32 utiliza locks cooperativos. Quando qualquer componente retém um lock, a frequência da CPU ou do barramento APB não cai abaixo do limiar estipulado pelo tipo de lock:

| Actor / Subsistema | Tipo de Lock (`esp_pm_lock_type_t`) | Identificador | Condição de Retenção (Acquire) | Condição de Liberação (Release) | Justificativa Técnica |
| :--- | :--- | :--- | :--- | :--- | :--- |
| **Actor `audio`** (`src/audio/audio.c`) | `ESP_PM_APB_FREQ_MAX` (80 MHz APB) | `"audio_i2s"` | Enquanto `current_mode == AUDIO_MODE_MUSIC_A2DP` ou `AUDIO_MODE_CALL_HFP` ou reproduzindo SFX | Ao transicionar para `AUDIO_MODE_IDLE` (`AUDIO_CMD_STOP` ou fim do tom) | O periférico I2S e o PLL de áudio exigem clock mestre e APB estáveis a 80 MHz para evitar estalos, jitter e underrun de DMA. |
| **Pilha Bluetooth (BTDM Controller)** (`components/bt/.../bt.c`) | `ESP_PM_APB_FREQ_MAX` / `ESP_PM_NO_LIGHT_SLEEP` | `"bt"` / `"btLS"` | Durante TX/RX de pacotes de rádio, scanning BLE ou pareamento ativo | Durante intervalos de Sniff, janelas de inatividade de Page/Inquiry scan | O controlador BTDM nativo do ESP-IDF gerencia seus próprios locks internos. Libera automaticamente entre os slots Bluetooth. |
| **Actor `ui`** (`src/display/display.c`) | *Sem lock permanente* (CPU normal) | N/A | Durante renderização ativa do LVGL (Core livre) | Quando o display entra em descanso (`lv_display_get_inactive_time` > timeout) | O display ILI9341 com backlight PWM desligado não requer lock; a task dorme em `vTaskDelay`. |
| **Actor `actuators`** (`src/actuators/`) | *Sem lock permanente* | N/A | Durante pulsos de vibração (LEDC ativo) | Task encerra por inatividade (`idle_ms = 2000`) | O LEDC opera alimentado pelo clock APB disponível; encerra a task quando inativo. |
| **Actor `settings`** (`src/core/settings.c`) | *Sem lock permanente* | N/A | Durante escrita de páginas NVS | Bloqueado na FreeRTOS Queue aguardando comandos | Permanece bloqueado na fila a maior parte do tempo. |

---

## 2. Medições e Estimativas de Corrente por Estado do Fone (WP 8.3)

Valores de referência compilados com base na documentação oficial da Espressif (*ESP FAQ - Classic Bluetooth Operating Current*, *ESP-IDF Low Power Guides*) e nas características do hardware periférico (Codec WM8960, Display ILI9341, LEDs, APDS-9930).

> **Aviso de Validação (§6 e §8):** As medições exatas em miliamperes dependem das características de cada lote do hardware físico e do circuito de alimentação e devem ser medidas em bancada por operador humano. A tabela abaixo fornece as faixas esperadas para auditoria.

### 2.1 Matriz de Estados de Energia

| Estado do Headset | Descrição Funcional | Frequência CPU | Display / Backlight | Bluetooth Status | Áudio Codec / I2S | Corrente Estimada Média (mA) |
| :--- | :--- | :---: | :---: | :---: | :---: | :---: |
| **1. Desligado / Standby (Off Head)** | Fone retirado da cabeça (`SENSOR_EVT_REMOVED`), display apagado, sem BT conectado | 80 / 160 MHz | Desligado (0 mA) | Advertising BLE lento | Codec em Power-Down | **~15 – 25 mA** *(~2 mA se Light-sleep com XTAL32K ativo)* |
| **2. Ativo em Repouso (Idle Worn)** | Fone na cabeça (`SENSOR_EVT_WORN`), tela ligada com menu inicial, sem música | 160 MHz | Ligado (Backlight ~40 mA) | Conectado (Sniff mode 33.75 ms) | Codec em Standby | **~75 – 95 mA** |
| **3. Ativo Tela Apagada (Idle Connected)** | Fone na cabeça, conectado ao smartphone, tela apagada por inatividade | 80 / 160 MHz | Desligado (Backlight 0 mA) | Conectado (Sniff mode) | Codec em Standby | **~35 – 45 mA** |
| **4. Reprodução de Música (A2DP Streaming)** | Tocando áudio via Bluetooth Classic A2DP a 44.1 kHz, EQ ativo, I2S DMA pleno | 160 MHz | Apagado (ou ligado conforme uso) | A2DP Sink (ACL ativo contínuo) | Codec WM8960 tocando (Class D / Fone) | **~115 – 135 mA** *(Tela apagada)*<br>**~155 – 175 mA** *(Tela ligada)* |
| **5. Chamada de Voz (HFP Call Active)** | Chamada telefônica ativa a 8 kHz / 16 kHz (mSBC), microfone ativo com Voice NR | 160 MHz | Tela ligada / status da chamada | HFP SCO/eSCO bidirecional contínuo | Codec full-duplex (TX fone + RX mic) | **~125 – 150 mA** |

---

## 3. Roteiro de Teste e Medição em Bancada (Para o Humano)

Para preencher as medições definitivas com instrumentos de precisão:

1. **Equipamento:** Fonte de bancada DC regulada em 3.70 V (ou multímetro digital em série com a entrada de bateria VBAT) com resolução de 0.1 mA.
2. **Procedimento de Teste:**
   - **Passo 1:** Ligar o fone na bancada. Anotar corrente de boot.
   - **Passo 2:** Deixar o fone sem uso e aguardar 30 segundos até o display apagar por inatividade. Registrar corrente do **Estado 1 e 3**.
   - **Passo 3:** Conectar ao smartphone Android/iOS via Bluetooth. Iniciar reprodução de música no Spotify/YouTube em volume 70%. Registrar corrente do **Estado 4**.
   - **Passo 4:** Efetuar uma ligação telefônica e aceitar no fone. Registrar corrente do **Estado 5**.
3. **Preenchimento dos Resultados:** Atualizar este arquivo na seção correspondente após o ensaio prático.
