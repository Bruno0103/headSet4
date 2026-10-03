# BACKLOG - headSet4

| ID | Descrição | Agente | Critério de Aceite |
|---|---|---|---|
| F-01 | Conflito I2C e SPI pinout. | A1 | `board_config.h` e `display.h` sem sobreposição de pinos. Build OK. |
| F-02 | Revisar SDK config para mSBC. | A1 | `sdkconfig.defaults` contém flags HFP WBS habilitadas corretas da v6.0.2. |
| F-13 | Extrair utils de examples e vendorizar. | A1 | Utils em `components/bt_app_utils` e `idf_component.yml` limpo. |
| F-03, F-07 | Jitter buffer PLC, tratamento drift. | A2 | Teste de host para drift e PLC passa sem erros e garante áudio contínuo. |
| F-04, F-05 | DMA auto_clear e I2S config p/ HFP. | A3 | `i2s_new_channel` usado; auto_clear ativado. |
| F-06, F-08, F-09, F-10 | audio_io task única, rampa ganho, clock MCLK. | A3 | Task isolada, s/ clicks de transição (ramp por soft), clock seguro. |
| F-12, F-14 | Ajustes task do bt_app e startup. | A4 | Initialization limpa sem retry cego (ESP_ERROR_CHECK removido). |
| FEAT-01 | Gerenciador de Conexões e reconexão. | A4 | Reconecta automático, limpa bonds no boot longo. |
| F-18, FEAT-05 | Suporte a múltiplas rates (SBC), delay report | A5 | A2DP delay reporting funcionando e delay dinâmico reportado. |
| F-19, FEAT-04, F-11 | AVRCP e HFP estado e deep copy evt. | A6 | Chamadas preempatam música sem falha; dados em filas s/ corrimento. |
| FEAT-02 | Configurar CoD, e IOCAP NoInputNoOutput. | A6 | O fone aparece como "Headset" no celular. |
| F-16, FEAT-06 | Gravar volume/peer em NVS. | A6 | Volume lembra estado passado após reboot. |
| FEAT-07 | Barramento de eventos unificado. | A7 | Eventos transitam via queue de `headset_events.h`. |
| F-17 | Microfone/sidetone e bypass ajustado. | A8 | Bypass de MUX verificado e desativado adequadamente. |
| FEAT-09 | Docs finais, TESTPLAN. | A9 | `README.md` e `TESTPLAN.md` refletem projeto real. |
