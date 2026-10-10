# Comandos BLE que Exigem Confirmação Física no HeadSet4

Este documento descreve as diretrizes de segurança e a lista de comandos remotos que necessitam de confirmação física direta no hardware antes de sua execução (conforme requisito **WP 7.4** do `AGENTS.md`).

---

## 1. Princípio de Segurança e Acesso Físico

Comandos recebidos via conexão BLE de smartphone possuem controle de acesso estrito:
1. **Criptografia Obrigatória:** As características de comando utilizam `PERM_WRITE_ENCRYPTED`, impedindo injeção de comandos em conexões abertas não autenticadas.
2. **Defesa em Profundidade:** Determinadas operações possuem impacto crítico na privacidade, integridade das ligações de rádio ou segurança auditiva. Para tais comandos, uma ação puramente remota **não deve** ter efeito imediato e irrevogável sem um handshake de presença/confirmação física do usuário no próprio headset.

---

## 2. Matriz de Comandos e Níveis de Exigência

| Comando JSON / Operação | Nível de Risco | Exige Confirmação Física? | Mecanismo de Confirmação |
| :--- | :--- | :--- | :--- |
| `get_all_status` / Consultas de telemetria | Baixo | ❌ Não | Execução e retorno imediatos via GATT Notify. |
| `set_volume` (Ajuste normal de volume) | Médio | ❌ Não | Limitado pelo teto configurado de volume de segurança. |
| `set_volume` (> 85% - Volume Excessivo) | Alto | ✅ **Sim** | Exige clique duplo no botão do headset para liberar teto > 85 dB. |
| `select_slot` / `disconnect_slot` | Baixo | ❌ Não | Comutação de slot de áudio entre dispositivos vinculados. |
| `start_pairing` (Abertura de pareamento) | Alto | ✅ **Sim** (se fone em uso) | Caso o fone esteja na cabeça e pareado, novos pareamentos exigem toque longo de 3 s no botão físico. |
| `factory_reset` (Apagar todas configs/bonds) | Crítico | ✅ **Sim** | Exige manter o botão físico pressionado por 10 s ou sequência de confirmação na UI touch. |
| `delete_all_bonds` (Remover pareamentos BT) | Alto | ✅ **Sim** | Notificação na tela LVGL solicitando confirmação do toque físico. |
| `fw_update_enter_dfu` (Entrar em modo OTA/DFU) | Crítico | ✅ **Sim** | Headset precisa estar conectado à alimentação (carregamento) e fora da cabeça (`SENSOR_EVT_REMOVED`). |

---

## 3. Fluxo de Confirmação Física

1. O app do smartphone envia o comando protegido por BLE (ex: `{"cmd":"factory_reset"}`).
2. O ator `phone_ctl` valida o comando e identifica a exigência de confirmação física.
3. É gerada uma notificação BLE de status pendente: `{"status":"pending_physical_confirm","action":"factory_reset","timeout_sec":30}`.
4. O headset emite um alerta sonoro (`sfx`) e/ou modal no display LVGL indicando a necessidade de pressionar o botão físico.
5. Se o botão for pressionado dentro do timeout, o evento `SENSOR_EVT_BUTTON_*` é correlacionado e a ação é executada. Caso contrário, a requisição expira.
