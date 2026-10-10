# Colinha de Prompts de Execução — Passo a Passo da Migração

Este arquivo contém os prompts prontos (prontos para copiar e colar) para guiar o agente único através de cada etapa do [MIGRATION_PLAN.md](file:///a:/BackupDesktopFileBruno/Estudos/ESP/ESP32/headSet4/docs/MIGRATION_PLAN.md) e [AGENTS.md](file:///a:/BackupDesktopFileBruno/Estudos/ESP/ESP32/headSet4/.agents/AGENTS.md).

Copie um bloco por vez, cole no chat e execute a validação antes de avançar para o próximo.

---

## 📋 Pré-Etapa: Diagnóstico e Validação Inicial de Compilação
> Use este prompt para garantir que o ambiente está limpo antes de tocar no código.

```markdown
Atue como Agente A0 (@hs-agent-architect).
Faça um diagnóstico rápido de compilação do projeto com `idf.py build`.
Se houver falha de indexação no bootloader (libefuse.a) ou artefatos antigos, indique ou execute a limpeza necessária para que tenhamos uma baseline limpa.
```

---

## 🚀 Passo 1 (WP 2.3): Concluir Isolamento Total do NVS
> **Papel:** Agente A2 (`@hs-agent-settings`)  
> **Objetivo:** Eliminar os últimos acessos a `nvs_open` em `src/bt/bt_fastpair.c` e centralizá-los no actor `settings`.

```markdown
Atue como Agente A2 (@hs-agent-settings) em conformidade com o AGENTS.md (WP 2.3).
Objetivo: Concluir o isolamento de NVS no projeto.
1. Em `main/src/bt/bt_fastpair.c`, remova os acessos diretos de `nvs_open`, `nvs_get_blob` e `nvs_set_blob` em `keys_load()` e `keys_save_locked()`.
2. Encapsule o carregamento/salvamento das chaves de Fast Pair usando o actor `settings` (adicionando a chave correspondente em settings se necessário) ou funções helper em settings.
3. Garanta que `grep -r "nvs_open" main/` retorne unicamente ocorrências dentro de `main/src/core/settings.c`.
4. Documente o código em português e verifique se o build permanece íntegro.
```

---

## 🎧 Passo 2 (WP 3.1 & 3.3): Actor de Áudio e Desacoplamento de SFX
> **Papel:** Agente A3 (`@hs-agent-audio`)  
> **Objetivo:** Criar a fila e o processamento do actor `audio`, encapsulando WM8960, I2S e equalização, e tornar `sfx` assinante de eventos.

```markdown
Atue como Agente A3 (@hs-agent-audio) em conformidade com o AGENTS.md (WP 3.1 e WP 3.3).
Objetivo: Isolar o domínio de áudio no Actor `audio` e desacoplar o SFX.
1. Crie a estrutura e fila do Actor `audio` (Core 1, prioridade 10) usando a infraestrutura `hs_actor.h`.
2. Implemente o processamento dos comandos de `hs_cmds.h`: `AUDIO_CMD_START_MUSIC`, `AUDIO_CMD_START_CALL`, `AUDIO_CMD_STOP`, `AUDIO_CMD_SET_VOLUME`, `AUDIO_CMD_SET_EQ` e `AUDIO_CMD_PLAY_TONE`.
3. Encapsule o controle do codec WM8960 e I2S dentro do actor de áudio.
4. Transforme `main/src/audio/sfx.c` em um assinante puro dos eventos `WORN`, `REMOVED`, `LINK_UP` e `LINK_DOWN`.
5. Remova `#include "sfx.h"` de `main/src/bt/bt_link_mgr.c`.
6. Mantenha os ringbuffers PCM de `audio_data.h` no plano de dados sem passar por filas de comandos.
7. Garanta comentários completos em português e teste de compilação sem warnings.
```

---

## 📶 Passo 3 (WP 3.2): Remover Acoplamento de Áudio da Pilha Bluetooth
> **Papel:** Agente A3 (`@hs-agent-audio`) + Agente A4 (`@hs-agent-bt`)  
> **Objetivo:** Fazer A2DP, HFP e AVRCP controlarem áudio apenas por comandos ou publicando fatos.

```markdown
Atue como Agente A3 (@hs-agent-audio) e Agente A4 (@hs-agent-bt) em conformidade com o AGENTS.md (WP 3.2).
Objetivo: Eliminar includes e chamadas de controle de áudio de dentro da pilha Bluetooth.
1. Em `main/src/bt/bt_a2dp.c`, `main/src/bt/bt_hfp.c` e `main/src/bt/bt_avrcp.c`:
   - Remova includes de `audio_io.h` e `audio_codec.h`.
   - Substitua chamadas diretas de controle por envio de comandos via `hs_actor_send()` para o actor de áudio ou publicação de fatos (`STREAMING`, `CALL_STATE`).
2. Mantenha apenas `audio_data.h` onde for estritamente necessário para o ringbuffer PCM (plano de dados).
3. Verifique com `grep -r "audio_io\|audio_codec" main/src/bt/` que nenhum arquivo de BT inclui esses headers.
```

---

## 📱 Passo 4 (WP 4.1 & 4.5): Actor Bluetooth (`bt_link`) e Remoção de Locks
> **Papel:** Agente A4 (`@hs-agent-bt`)  
> **Objetivo:** Transformar `bt_link_mgr` em um actor sobre FreeRTOS Queue e limpar bloqueios do loop de eventos.

```markdown
Atue como Agente A4 (@hs-agent-bt) em conformidade com o AGENTS.md (WP 4.1 e WP 4.5).
Objetivo: Transformar o `bt_link_mgr` em Actor com fila de comandos privada e eliminar mutex recursivo.
1. Migre o `bt_link_mgr` para operar sobre o motor `hs_actor.h` (Core 0, prioridade 6).
2. Elimine o mutex recursivo `s_mtx`: operações concorrentes (comandos de troca de slot, pareamento, desconexão) devem ser serializadas na fila de mensagens do actor.
3. No handler `on_headset_event`: remova qualquer chamada bloqueante à pilha BT e chamadas lentas. Ele deve apenas converter o evento em comando/mensagem para a fila do actor.
4. Timers (`s_vol_tmr`, `s_connect_tmr`, etc.) devem disparar comandos assíncronos para a fila do ator em vez de executar lógica pesada com lock no callback.
5. Valide que o tempo de execução do handler de evento fique abaixo de 2 ms e com zero `portMAX_DELAY`.
```

---

## 👁️ Passo 5 (WP 5.1): Isolamento de Sensores (APDS-9930)
> **Papel:** Agente A5 (`@hs-agent-sensors`)  
> **Objetivo:** Isolar o sensor de proximidade como publicador puro de eventos, desacoplando-o da UI.

```markdown
Atue como Agente A5 (@hs-agent-sensors) em conformidade com o AGENTS.md (WP 5.1).
Objetivo: Isolar o sensor APDS-9930.
1. O driver `main/src/sensor/apds9930.c` deve atuar exclusivamente como publicador de fatos no barramento: publica `SENSOR_EVT_WORN` e `SENSOR_EVT_REMOVED` via `esp_event_post()`.
2. Garanta que o sensor obtenha limiares e configuração do actor `settings`.
3. Garanta que a task do sensor respeite o duty-cycle correto para economia de bateria e nunca faça chamadas bloqueantes em outros domínios.
```

---

## 🖥️ Passo 6 (WP 6.1 & 6.2): Desacoplamento da UI (`ui_bridge` e `ui_model`)
> **Papel:** Agente A6 (`@hs-agent-ui`)  
> **Objetivo:** Remover todos os includes diretos de hardware, BT e sensor em `ui_bridge.c`.

```markdown
Atue como Agente A6 (@hs-agent-ui) em conformidade com o AGENTS.md (WP 6.1 e WP 6.2).
Objetivo: Desacoplar a interface gráfica de todos os módulos de domínio.
1. Crie ou estruture `main/src/display/ui_model.c` com o estado reativo da interface (atualizado unicamente por assinaturas de `esp_event` ou na task do display).
2. Refatore `main/src/display/ui_bridge.c`:
   - Remova `#include "bt_link_mgr.h"`, `#include "bt_gap.h"`, `#include "apds9930.h"` e chamadas de `gpio_set_level`.
   - *Getters da UI:* leem unicamente do modelo reativo em memória sem tomar locks longos.
   - *Ações do Usuário (Cliques/Sliders):* enviam comandos via `hs_actor_send()` para os respectivos atores (`bt_link`, `audio`, etc.).
3. Apenas a task do LVGL deve tocar nos objetos visuais do LVGL.
```

---

## 🧹 Passo 7 (Fase 9): Exclusão do Legado e Auditoria Final
> **Papel:** Agente A0 (`@hs-agent-architect`) + Agente A8 (`@hs-agent-power-qa`)  
> **Objetivo:** Deletar `headset_events.*`, remover todos os shims e auditar isolamento completo.

```markdown
Atue como Agente A0 (@hs-agent-architect) e Agente A8 (@hs-agent-power-qa) em conformidade com o AGENTS.md (Fase 9).
Objetivo: Limpeza definitiva do meio legado e auditoria estrita.
1. Remova os arquivos legados `main/src/core/headset_events.h` e `main/src/core/headset_events.c`.
2. Substitua qualquer uso remanescente de `HEADSET_EVENT` pelas bases oficiais: `SENSOR_EVT`, `BT_EVT`, `AUDIO_EVT`, `CFG_EVT`.
3. Realize a auditoria final:
   - Verifique ausência de `#include` cruzados entre domínios.
   - Verifique que `nvs_open` existe somente em `settings.c`.
   - Verifique que não há `portMAX_DELAY` em handlers de `esp_event`.
   - Verifique que todos os payloads são <= 64 bytes.
4. Execute `idf.py build` e certifique-se de compilação 100% verde sem warnings.
```
