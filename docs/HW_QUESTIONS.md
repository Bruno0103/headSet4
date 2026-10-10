# HW_QUESTIONS.md — Pontos de Atenção de Hardware e Pinagem

> **Status:** Aberto / Aguardando Decisão Humana  
> **Governança:** Agente A0 (@hs-agent-architect) — Work Package 0.5  
> **Regra:** Nenhuma pinagem em `main/src/board/pinout.h` pode ser alterada sem aprovação humana formal registrada.

Este documento consolida inconsistências, riscos elétricos e discrepâncias entre esquemático, firmware e pinagem do projeto **HeadSet4** (ESP32-WROVER-E).

---

## 1. MCLK do Codec de Áudio WM8960 (`GPIO0` vs `I2S_GPIO_UNUSED`)

### Contexto e Discrepância
- Em [pinout.h](file:///a:/BackupDesktopFileBruno/Estudos/ESP/ESP32/headSet4/main/src/board/pinout.h#L52-L54):
  ```c
  #define BOARD_I2S_MCLK 0  /* -> MCLK do WM8960 (CLK_OUT1) */
  ```
- Em [audio_io.c](file:///a:/BackupDesktopFileBruno/Estudos/ESP/ESP32/headSet4/main/src/audio/audio_io.c#L405):
  ```c
  .gpio_cfg = {
      .mclk = I2S_GPIO_UNUSED,  /* o breakout gera o proprio MCLK (confirme no esquematico) */
      ...
  }
  ```
- Em [audio_codec.c](file:///a:/BackupDesktopFileBruno/Estudos/ESP/ESP32/headSet4/main/src/audio/audio_codec.c#L66):
  O driver configura os registradores do PLL do WM8960 assumindo um clock de entrada `MCLK = 24 MHz` (gerando 12 MHz internamente pós prescaler /2).

### Riscos e Implicações
1. **Se o módulo/breakout NÃO tiver oscilador de cristal local próprio:** O WM8960 requer MCLK ativo para alimentar seu núcleo digital e PLL. Ao configurar `I2S_GPIO_UNUSED`, o ESP32 não gera sinal de clock de saída pelo periférico I2S e o codec não processa áudio, resultando em codec mudo.
2. **Se o ESP32 gerar MCLK no GPIO0:** O GPIO0 é um pino crítico de strapping do ESP32 (modo Boot/Download). Se for configurado como saída de clock de alta frequência (ex: 12 MHz ou 24 MHz via `CLK_OUT1` / I2S MCLK), deve-se garantir que durante o reset o pino esteja em nível lógico alto para inicialização normal da flash SPI. Além disso, se o breakout já possuir um oscilador onboard injetando sinal na mesma linha, haverá conflito de barramento (bus contention) e risco de danos aos drivers de saída.

### Opções para Decisão
- [ ] **Opção A (Módulo com oscilador próprio):** O módulo WM8960 possui oscilador de cristal dedicado de 24 MHz / 12 MHz soldado na placa. O pino `BOARD_I2S_MCLK` deve permanecer desabilitado em software (`I2S_GPIO_UNUSED`) e o pino GPIO0 do ESP32 fica liberado.
- [ ] **Opção B (ESP32 gera MCLK via GPIO0):** O módulo WM8960 não possui oscilador onboard e depende do ESP32 injetar MCLK. O driver em `audio_io.c` deve ser alterado de `I2S_GPIO_UNUSED` para `BOARD_I2S_MCLK` (GPIO0).
- [ ] **Opção C (Codec em modo Master derivando MCLK do BCLK/PLL):** Reconfigurar registradores internos do WM8960 para sintetizar internamente a partir de BCLK ou outra fonte (sujeito a limitações de jitter e suporte do chip).

---

## 2. GPIO15 (Strapping Pin) Usado como Linha de Alimentação (`VDD`) do APDS-9930

### Contexto e Discrepância
- Em [pinout.h](file:///a:/BackupDesktopFileBruno/Estudos/ESP/ESP32/headSet4/main/src/board/pinout.h#L79):
  ```c
  #define BOARD_APDS9930_PWR_GPIO 15 /* alimenta o sensor; VL fixo em 3,3V */
  ```
- Em [apds9930.c](file:///a:/BackupDesktopFileBruno/Estudos/ESP/ESP32/headSet4/main/src/sensor/apds9930.c#L56-L66):
  O firmware comuta o pino GPIO15 em nível HIGH/LOW (`power_set(bool on)`) para ligar/desligar a alimentação VDD do sensor de proximidade como estratégia de economia de energia.

### Riscos e Implicações
1. **Pino de Strapping (MTDO):**
   - No ESP32, o GPIO15 controla a saída de mensagens de boot da ROM (silencia logs da ROM se estiver em nível LOW no reset).
   - Possui resistor interno de pull-up fraco ativado durante o boot.
   - Qualquer carga capacitiva ou resistiva conectada ao pino que force nível lógico baixo indesejado no boot pode alterar o comportamento de inicialização do SoC.
2. **Capacidade de Corrente dos GPIOs do ESP32:**
   - Um pino GPIO suporta até 12 mA a 40 mA de corrente máxima recomendada. O APDS-9930 consome poucos microampères em standby, porém o pico de corrente durante a emissão dos pulsos do LED IR (se conectado ao mesmo barramento VDD) pode atingir picos de até 100 mA (dependendo do circuito do breakout). Se o LED IR for alimentado pelo mesmo pino, provocará queda severa de tensão no pino, brownout local ou dano permanente ao GPIO.
3. **Comportamento em Deep Sleep / Reset:**
   - Ao reiniciar, o pino fica flutuando até a configuração do GPIO, desenergizando o sensor de forma abrupta.

### Opções para Decisão
- [ ] **Opção A (Controle via Transistor/MOSFET High-Side):** Utilizar o GPIO15 (ou outro GPIO livre) apenas para controlar a gate de um MOSFET canal P que chaveia os 3.3V reais da fonte para o VDD do sensor.
- [ ] **Opção B (Alimentação Direta nos 3.3V e Power-Down via Software I2C):** Manter o VDD do APDS-9930 conectado continuamente ao barramento de 3.3V. A economia de energia é realizada via comandos nos registradores do sensor (desligando `PON`/`PEN` no registrador `ENABLE`), onde o consumo cai para ~1 µA (Sleep Mode). O GPIO15 fica livre ou sem conexão.
- [ ] **Opção C (Manter GPIO15 direto com restrições):** Validar se o pino `VL` (ânodo do LED emissor IR) está ligado isoladamente aos 3.3V fixos e apenas o chip (VDD de controle) no GPIO15, garantindo corrente < 5 mA e medindo a integridade do boot do ESP32.

---

## 3. Comentário Incorreto sobre Pull-up de MISO e GPIO26 em `display.c`

### Contexto e Discrepância
- Em [pinout.h](file:///a:/BackupDesktopFileBruno/Estudos/ESP/ESP32/headSet4/main/src/board/pinout.h#L27-L34):
  ```c
  #define LCD_GPIO_MISO 19 // T_DO (XPT2046)
  #define TOUCH_GPIO_CS 26 // T_CS
  ```
- Em [display.c](file:///a:/BackupDesktopFileBruno/Estudos/ESP/ESP32/headSet4/main/src/display/display.c#L136-L141):
  ```c
  /*
   * Habilita pull-up interno no pino MISO (T_DO).
   * No GPIO 26 o ESP32 possui resistor de pull-up interno de ~45k,
   * evitando que a linha flutue para nível indeterminado quando o chip XPT2046
   * entra em modo tri-state entre transmissões SPI.
   */
  gpio_set_pull_mode(LCD_GPIO_MISO, GPIO_PULLUP_ONLY);
  ```

### Análise do Problema
1. O comentário cita textualmente: *"No GPIO 26 o ESP32 possui resistor de pull-up..."*, porém a linha de código logo abaixo aplica o pull-up em `LCD_GPIO_MISO`, que é o **GPIO 19**!
2. O **GPIO 26** é na verdade o `TOUCH_GPIO_CS` (Chip Select do touch XPT2046).
3. Essa discrepância gera confusão de depuração, pois aparenta haver confusão entre a linha de dados (MISO - GPIO 19) e a linha de seleção (CS - GPIO 26).

### Ação Necessária
- O código funcional (`gpio_set_pull_mode(LCD_GPIO_MISO, GPIO_PULLUP_ONLY);`) está aplicando pull-up no pino correto (GPIO 19, linha T_DO).
- O comentário em `display.c` precisa ser corrigido pelo Agente A6 / A0 para explicitar que o pull-up é aplicado no **GPIO 19 (MISO / T_DO)** e não no GPIO 26.

---

## Tabela de Resumo para Decisão

| Ponto | Componente | Pinos Envolvidos | Status | Ação Pendente |
|---|---|---|---|---|
| **#1** | Codec WM8960 | GPIO0 vs Unused | ⚠️ Risco de áudio mudo / curto de clock | Validar no esquemático se o breakout tem oscilador próprio de 24MHz |
| **#2** | Sensor APDS-9930 | GPIO15 (Strapping / VDD) | ⚠️ Risco de boot fail / pico de corrente | Decidir se VDD fica direto em 3.3V com sleep via I2C |
| **#3** | Display / Touch SPI | GPIO19 (MISO) vs GPIO26 (CS) | ✅ Resolvido em `display.c` | Comentário corrigido para indicar GPIO19 (T_DO) |
