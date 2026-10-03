# Arquitetura do Sistema (headSet4)

## 1. Módulos e Fluxo de Dados

```ascii
                      +-------------------+
                      |   Bluetooth Stack |
                      |    (BT_APP task)  |
                      +---------+---------+
                                |
    [A2DP/HFP Data]             | [Eventos / Comandos]
           |                    |
           v                    v
    +-------------+      +-------------+
    | bt_a2dp /   |      |   bt_conn   |
    | bt_hfp      |      | (Conn. Mngr)|
    +------+------+      +------+------+
           |                    |
[audio_cmd]|                    | [headset_evt]
           v                    v
    +-------------+      +-------------+      +-------------+
    |  audio_io   +----->| event_bus   |<-----+ UI/Display  |
    |  (Task)     |      | (main task) |      | (Opcional)  |
    +------+------+      +------+------+      +-------------+
           |
      [I2S/I2C]
           v
    +-------------+
    | WM8960 Codec|
    +-------------+
```

## 2. Tabela de Tasks

| Task | Core | Prioridade | Stack | Fila de Entrada | Função |
|---|---|---|---|---|---|
| `audio_io` | 1 | Alta (20) | 4096 B | `audio_cmd_queue` | Configura Codec/I2S e repassa dados DMA. |
| `BT_APP` | 0 | Média (10) | 8192 B | Interna do IDF | Callbacks da pilha BT (A2DP, HFP, AVRCP). Não pode bloquear. |
| `bt_conn` | 0 | Baixa (5) | 3072 B | Chamadas diretas/Timer | Gerencia reconexão, state machine de pareamento. |
| `main_task` | 0/1 | Baixa (5) | 4096 B | `event_bus_queue` | Inicializa sistema e despacha eventos UI. |

## 3. Máquinas de Estado

### audio_io
| Estado | Evento | Ação | Novo Estado |
|---|---|---|---|
| IDLE | START(MUSIC) | Rampa ganho, config I2S, play | MUSIC |
| IDLE | START(CALL) | Rampa ganho, config I2S, play | CALL |
| MUSIC | START(CALL) | Stop I2S/codec, config CALL, play | CALL |
| CALL | START(MUSIC)| Ignora ou agenda para depois | CALL |
| MUSIC | STOP | Rampa down, stop I2S | IDLE |

### bt_conn
| Estado | Evento | Ação | Novo Estado |
|---|---|---|---|
| INIT | BT_READY | Tenta conectar last_peer | RECONNECTING |
| RECONNECTING | CONNECTED | Atualiza NVS | CONNECTED |
| RECONNECTING | TIMEOUT | Habilita scan (pair mode) | PAIRING |
| PAIRING | CONNECTED | Atualiza NVS | CONNECTED |
| CONNECTED | DISCONNECT | Tenta reconectar (se não foi intencional)| RECONNECTING |

## 4. Políticas
- **Chamada > Música:** Áudio de HFP sempre preempata A2DP. A música deve ser pausada (AVRCP) ao iniciar chamada.
- **Dono único do Codec/I2S:** Só a `audio_io_task` fala com I2C e I2S.
- **Callbacks não bloqueiam:** Funções chamadas pela `BT_APP` apenas preenchem structs e enviam p/ filas ou timers.

## 5. Orçamento de RAM (ESP32-WROOM 4 MB Flash)
- **BT Stack (BSS + Heap):** ~60-80 KB.
- **Buffers de Áudio:** Jitter buffer (16 KB) + DMA TX/RX (10 KB) = ~26 KB.
- **Tasks & OS:** ~30 KB.
- **Heap Livre Mínimo Meta:** >= 20 KB para estabilidade, evitando OOM durante reconexões.
