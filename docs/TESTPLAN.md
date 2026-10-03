# Plano de Testes (headSet4)

Este documento define como validar o headSet4 sem depender de aprovações empíricas duvidosas. Todo novo agente (A2-A6) deve assegurar que a respectiva camada passe nos testes aqui listados.

## 1. Testes de Host (Mock)
Executáveis locais para lógica pura (`make -C test_host test`), isolados do IDF.

- **DSP / Jitter Buffer (A2):**
  - **Plano:** Injetar PCM simulando A2DP (blocos variados) em um ritmo diferente da leitura DMA (simulando drift).
  - **Expectativa:** O PLC (Packet Loss Concealment) suaviza perdas. O drop/dup insere no máximo 1 sample sem pop. Zero falhas de segmentação.
- **State Machine de Conexão (A4):**
  - **Plano:** Chamar a FSM com sequências: init -> BT_READY -> timeout. init -> BT_READY -> CONNECTED -> DISCONNECT.
  - **Expectativa:** Estados progridem conforme arquitetura, NVS mocks são chamados no momento certo.
- **HFP Deep Copy (A6):**
  - **Plano:** Passar string de caller ID, despachar para fila simulada e sobrescrever string original antes da fila ser processada.
  - **Expectativa:** A task de destino processa a string correta (cópia profunda funcionou).

## 2. Testes de Bancada (Hardware)
Deve ser executado no hardware com a board WM8960 conectada aos pinos configurados.

- **Música (SBC):** 
  - Reproduzir faixa contínua por 60 min. Verificar se ocorre drift (falhas de áudio por over/underrun).
- **Música -> Pausa / Play:** 
  - Usar celular para pausar e voltar. Não deve gerar estalos ("pop/click") e AVRCP deve reagir < 200 ms.
- **Chamada (CVSD / mSBC):**
  - Fazer uma ligação. Validar se a voz bidirecional funciona sem ruído metálico ou repetições devido ao DMA `auto_clear`.
- **Transição Música -> Chamada:**
  - Tocar música e ligar pro telefone pareado. A música deve pausar, tocar o ringtone local/HFP, e ao desligar a chamada, voltar para a música suavemente (rampas atuando).
- **Recuperação de OOM / Conexão:**
  - Ligar e desligar o celular 10 vezes em 2 minutos. O ESP não deve dar Kernel Panic nem exaurir a RAM.
