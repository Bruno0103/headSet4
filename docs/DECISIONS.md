# DECISIONS.md — Registro de Decisões de Arquitetura (ADR)

Projeto: `Headset4` (ESP32-WROVER-E · ESP-IDF v6.x)  
Governança: Agente A0 (@hs-agent-architect) & Agente A2 (@hs-agent-settings)

---

## ADR 001 — Persistência e Criptografia das Chaves de Conta do Google Fast Pair (WP 2.3)

### Contexto
O protocolo **Google Fast Pair Service (GFPS)** armazena na memória não-volátil (NVS) até 5 chaves de conta (Account Keys de 16 bytes cada, totalizando 80 bytes).
Essas chaves permitem ao usuário conectar seu headset automaticamente a qualquer dispositivo Android vinculado à sua Conta Google, sem necessidade de novo pareamento Bluetooth manual.

No código legado (`main/src/bt/bt_fastpair.c`):
- O módulo acessava diretamente `nvs_open("fastpair", ...)` e realizava `nvs_get_blob` / `nvs_set_blob`.
- Isso violava o princípio arquitetural: **"Um recurso, um dono único: o Actor `settings` é o único dono do NVS"** (AGENTS.md §3 e §4.2).
- Além disso, as chaves estavam gravadas em texto plano (plaintext) na partição NVS padrão.

### Desafios Identificados
1. **Tamanho do Blob vs Payload Inline de Atores:**
   - As mensagens de atores (`hs_msg_t`) possuem 48 bytes de payload inline por restrição de barramento.
   - 5 chaves de conta ocupam 80 bytes (5 x 16).
   - O array pode crescer caso novos Seeker accounts sejam suportados.
2. **Criptografia NVS (NVS Encryption) do ESP-IDF:**
   - O ESP-IDF suporta *NVS Encryption* usando chaves AES-XTS armazenadas em partição dedicada (`nvs_key`) ou eFuse.
   - Habilitar criptografia de hardware requer gravação de eFuses permanentes (`SECURE_BOOT` / `FLASH_ENCRYPTION`), o que inviabiliza gravações de depuração em bancada sem chip de desenvolvimento dedicado.
   - Alternativa por software: cifrar o blob de 80 bytes usando uma chave de dispositivo derivada via `mbedtls` ou PSA Crypto antes de entregar ao `settings`.

### Decisão Proposta e Plano de Ação
1. **Fase Atual (Isolamento Arquitetural via Actor `settings`):**
   - Centralizar a chave no namespace gerenciado por `settings` com a chave `SETTINGS_KEY_FP_KEYS` (`"fp_keys"`).
   - O actor `settings` fornece API dedicada para persistência desse blob seguro.
2. **Criptografia NVS (Aguardando Decisão Humana formal):**
   - **Opção 1 (Plaintext no NVS padrão):** Mantém o armazenamento das chaves no NVS pelo `settings` sem criptografia adicional de software até a homologação final de hardware.
   - **Opção 2 (Cifra de Software com PSA Crypto):** Cifrar o array com chave simétrica derivada localmente pelo PSA Crypto / mbedTLS antes de persistir no NVS.
   - **Opção 3 (NVS Encryption via eFuse):** Exige eFuse burn (irreversível no hardware de produção).
   - **Status:** Conforme definido no AGENTS.md WP 2.3, **NÃO implementar criptografia por eFuse sem decisão humana expressa**.

---

## ADR 002 — Migração dos Módulos para o Barramento de Atores
- `audio`: Encapsula I2S e WM8960; ringbuffers PCM ficam separados em `audio_data.h`.
- `bt_link`: Gerencia slots Bluetooth e conexões em fila FreeRTOS no Core 0 sem mutex recursivo.
- `ui`: Executa exclusivamente na task de display do LVGL, comunicando-se por mensagens de modelo e comandos.
