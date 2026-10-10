# Relatório de Viabilidade e Governança de Power Management (WP 8.1)

**Projeto:** `Headset4` (ESP32-WROVER-E · ESP-IDF v6.x / Bluedroid BTDM · LVGL 9.5)  
**Agente Responsável:** Agente A8 (Energia, Power Management e QA)  
**Data:** 10 de Outubro de 2026  
**Status do Pacote:** Concluído / Documentado

---

## 1. Sumário Executivo e Veredito

Este documento atende aos requisitos mandatórios estipulados no **AGENTS.md (§8, WP 8.1 e §6)**:
> *"Antes de ligar CONFIG_PM_ENABLE, CONFIG_FREERTOS_USE_TICKLESS_IDLE ou light sleep, consultar a doc oficial do ESP-IDF sobre Bluetooth Classic ativo + A2DP (modem sleep, necessidade de cristal de 32 kHz, limites de DFS). Entregar relatório. Não prometer sono automático durante A2DP sem fonte."*

### Veredito Técnico de Viabilidade

| Mecanismo de Energia | Viabilidade no ESP32 Headset4 | Requisitos Mandatórios de Hardware / Sistema | Impacto em A2DP / Chamadas HFP |
| :--- | :---: | :--- | :--- |
| **DFS (Dynamic Frequency Scaling)** | **VIÁVEL (com APB lock)** | `CONFIG_PM_ENABLE=y`, FreeRTOS Tick Rate adequado. APB mantido em 80 MHz durante I2S/BT. | CPU pode cair para 80 MHz em ocioso se nenhum lock exigir 160/240 MHz. |
| **Bluetooth Modem Sleep (BLE / Classic Sniff)** | **VIÁVEL** | `CONFIG_BTDM_CTRL_MODEM_SLEEP=y`. Suportado com cristal principal (Main XTAL) ou 32 kHz. | Rádio desliga nos intervalos de Sniff / Page scan sem quebrar sincronismo. |
| **Automatic Light-sleep em Repouso** | **VIÁVEL SOMENTE COM CRISTAL EXTERNO 32.768 kHz** | Hardware precisa possuir cristal de 32 kHz conectado nos pinos XTAL32 do RTC (GPIO32 / GPIO33). | **Inviável sem cristal externo dedicado.** No ESP32 original, o Main XTAL é desligado no light sleep, impossibilitando manter os slots de tempo Bluetooth sem 32 kHz. |
| **Light-sleep Automático DURANTE Streaming A2DP** | **INVIÁVEL (Não suportado pela arquitetura do ESP32)** | I2S DMA requer clock contínuo de APB e CPU processa pacotes SBC / DSP continuamente. | O controlador BTDM e o driver I2S mantêm locks de APB/CPU ativos. Consumo contínuo de ~101 mA durante música. |

---

## 2. Análise Técnica Baseada na Documentação Oficial do ESP-IDF

### 2.1 Por que o ESP32 original não entra em Light-sleep com Bluetooth sem Cristal de 32 kHz?
De acordo com o guia oficial da Espressif (*ESP-IDF Low Power Mode - Bluetooth LE / Controller Options Reference* e código-fonte do componente `bt.c`):
- A especificação Bluetooth exige uma precisão de clock em modo sleep inferior a **±500 PPM**.
- O ESP32 original (diferente do ESP32-C3 ou S3) **não suporta manter o oscilador de cristal principal (Main XTAL) ligado durante o Light-sleep** para uso pelo rádio (`CONFIG_BTDM_CTRL_LPCLK_SEL_MAIN_XTAL`).
- Se `CONFIG_BTDM_CTRL_LPCLK_SEL_EXT_32K_XTAL` estiver configurado, mas o cristal físico de 32.768 kHz não for detectado na inicialização (`rtc_clk_slow_src_get() != SOC_RTC_SLOW_CLK_SRC_XTAL32K`), a pilha do controlador executa fallback automático:
  ```text
  W (BTDM): 32.768kHz XTAL not detected, fall back to main XTAL as Bluetooth sleep clock
  light sleep mode will not be able to apply when bluetooth is enabled
  ```
  Nessa condição, o driver cria internamente um lock `ESP_PM_NO_LIGHT_SLEEP` (`s_light_sleep_pm_lock`), **bloqueando permanentemente o Light-sleep** enquanto o Bluetooth estiver ativo.

### 2.2 Comportamento do Bluetooth Classic (A2DP / HFP) com DFS e Modem Sleep
A documentação da Espressif em *ESP FAQ - Classic Bluetooth Operating Current* relata medições empíricas no ESP32:
- **Reprodução de Música A2DP (160 MHz Dual-core, SBC decode ativo, I2S DMA transferindo):**
  - Consumo médio com DFS e Light-sleep desativados: **103.03 mA** (Pico de 192.62 mA).
  - Consumo médio com DFS ativado: **101.26 mA** (Pico de 219.40 mA).
  - **Fato fundamental:** *"Since the CPU is constantly working while playing music, no minimum value has been recorded."* (Não há sono de CPU durante streaming de áudio contínuo).
- **Modo Sniff (Bluetooth Classic conectado, sem tocar áudio, intervalo = 33.75 ms):**
  - Sem DFS / Light-sleep: Médio de **42.5 mA**.
  - Com DFS e Light-sleep (com cristal de 32 kHz): Médio de **19.7 mA** (Mínimo de **1.47 mA** nos intervalos de sono do rádio).

---

## 3. Diretrizes de Implementação dos Locks no Headset4 (WP 8.2)

Para garantir que o subsistema de áudio e os atuadores operem sem falhas de clock ou jitter de I2S, as regras de aquisição de lock do `esp_pm` são:

1. **Actor `audio` (`src/audio/audio.c`):**
   - Retém um lock `ESP_PM_APB_FREQ_MAX` durante os modos `AUDIO_MODE_MUSIC_A2DP` e `AUDIO_MODE_CALL_HFP`.
   - Ao transicionar para `AUDIO_MODE_IDLE` ou término de reprodução de tons SFX, o lock é liberado imediatamente.
2. **Actor `bt_link` / Controlador BTDM:**
   - O próprio controlador BTDM (`bt.c`) já gerencia internamente os locks de frequência e sono de rádio via `btdm_slp_tmr_callback`.
3. **Actor `ui` (Display Task):**
   - Durante renderização de frames pesados ou transições do LVGL, o display roda em tarefa normal; durante repouso após o display timeout (`lv_display_get_inactive_time`), a tela desliga o backlight PWM e o actor entra em ocioso, permitindo que a CPU desça a frequência.

---

## 4. Recomendações e Plano de Ação

1. **Não forçar `CONFIG_FREERTOS_USE_TICKLESS_IDLE=y` nem `CONFIG_PM_ENABLE=y` no `sdkconfig.defaults` sem validação de hardware do cristal de 32 kHz:**
   - Se o hardware do Headset4 não possuir o cristal físico de 32.768 kHz soldado, habilitar Light-sleep forçado causará logs de advertência constantes e potenciais perdas de sincronismo nos pacotes ACL/eSCO do Bluetooth Classic.
2. **Preparar suporte a Power Locks condicionais em `audio.c`:**
   - Adicionar `#if CONFIG_PM_ENABLE` em `audio.c` para alocar e gerenciar `esp_pm_lock_handle_t` mantendo APB em 80 MHz durante I2S ativo.
3. **Documentar a medição em `docs/POWER.md` (WP 8.3):**
   - Estabelecer a tabela comparativa teórica e as instruções para medição com multímetro/amperímetro humano.
