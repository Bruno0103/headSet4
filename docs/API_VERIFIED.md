# API Verified (ESP-IDF v6.0.2 / Bluedroid)
> Evidências verificadas diretamente nos headers em `A:\Program\esp\.espressif\v6.0.2\esp-idf\components\`

| Símbolo | Header : Linha | Assinatura exata | Notas / Armadilhas |
|---|---|---|---|
| **I2S** (`esp_driver_i2s`) | | | |
| `I2S_CHANNEL_DEFAULT_CONFIG` | `driver/i2s_common.h:25-28` | Macro com `.dma_desc_num=6`, `.dma_frame_num=240`, `.auto_clear_after_cb=false` | **Padrão: 6x240 frames ~= 180 ms a 8 kHz (latência brutal p/ HFP)**. `auto_clear` desligado por padrão. |
| `i2s_chan_config_t` | `driver/i2s_common.h:60-90` | `typedef struct { int id; i2s_role_t role; uint32_t dma_desc_num; uint32_t dma_frame_num; union { bool auto_clear; bool auto_clear_after_cb; }; bool auto_clear_before_cb; ... }` | `auto_clear` = alias de `auto_clear_after_cb`. Zera buffer TX após callback `on_sent`. Para evitar zumbido em underrun, usar `.auto_clear=true`. |
| `i2s_channel_reconfig_std_clock` | `driver/i2s_std.h` | `esp_err_t i2s_channel_reconfig_std_clock(i2s_chan_handle_t handle, const i2s_std_clk_config_t *clk_cfg)` | **Só altera clock (frequência). NÃO altera tamanho de DMA alocado.** Para trocar DMA, use `i2s_del_channel` + `i2s_new_channel`. |
| `i2s_new_channel` | `driver/i2s_common.h` | `esp_err_t i2s_new_channel(const i2s_chan_config_t *chan_cfg, i2s_chan_handle_t *ret_tx_handle, i2s_chan_handle_t *ret_rx_handle)` | Aloca recursos DMA. Chamar antes de configurar modo (std/pdm). |
| `i2s_del_channel` | `driver/i2s_common.h` | `esp_err_t i2s_del_channel(i2s_chan_handle_t handle)` | Libera canal e buffers DMA. |
| **I2C Master** (`esp_driver_i2c`) | | | |
| `i2c_new_master_bus` | `driver/i2c_master.h` | `esp_err_t i2c_new_master_bus(const i2c_master_bus_config_t *bus_config, i2c_master_bus_handle_t *ret_bus_handle)` | API nova v5+/v6. Substitui `i2c_driver_install`. |
| `i2c_master_bus_add_device` | `driver/i2c_master.h` | `esp_err_t i2c_master_bus_add_device(i2c_master_bus_handle_t bus_handle, const i2c_device_config_t *dev_config, i2c_master_dev_handle_t *ret_handle)` | Registra o WM8960 (0x1A) no barramento. |
| `i2c_master_transmit` | `driver/i2c_master.h` | `esp_err_t i2c_master_transmit(i2c_master_dev_handle_t i2c_dev, const uint8_t *write_buffer, size_t write_size, int xfer_timeout_ms)` | Bloqueante. Não chamar de ISR ou de callback de áudio. |
| **GAP BT** (`esp_gap_bt_api.h`) | | | |
| `esp_bt_gap_set_device_name` | `esp_gap_bt_api.h:1147` | `esp_err_t esp_bt_gap_set_device_name(const char *name)` | **Ainda existe no v6.0.2, NÃO foi removida.** `esp_bt_dev_set_device_name` NÃO existe nessa versão. Usar `esp_bt_gap_set_device_name`. |
| `esp_bt_gap_set_scan_mode` | `esp_gap_bt_api.h:688` | `esp_err_t esp_bt_gap_set_scan_mode(esp_bt_connection_mode_t c_mode, esp_bt_discovery_mode_t d_mode)` | Controla conectabilidade + descoberta separadamente. |
| `esp_bt_gap_set_cod` | `esp_gap_bt_api.h:794` | `esp_err_t esp_bt_gap_set_cod(esp_bt_cod_t cod, esp_bt_cod_mode_t mode)` | Deve ser chamado **após** inicializar perfis (A2DP/HFP), senão os perfis sobrescrevem o CoD. |
| `esp_bt_gap_set_security_param` | `esp_gap_bt_api.h` | `esp_err_t esp_bt_gap_set_security_param(esp_bt_sp_param_t param_type, void *value, uint8_t len)` | Para IOCAP: `param_type=ESP_BT_SP_IOCAP_MODE`, `value=&ESP_BT_IO_CAP_NONE`. |
| `esp_bt_gap_get_bond_device_list` | `esp_gap_bt_api.h` | `esp_err_t esp_bt_gap_get_bond_device_list(int *dev_num, esp_bd_addr_t *dev_list)` | Limite implícito de bonds determinado pelo NVS stack. |
| `esp_bt_gap_remove_bond_device` | `esp_gap_bt_api.h` | `esp_err_t esp_bt_gap_remove_bond_device(esp_bd_addr_t bd_addr)` | Remove bond específico; dispara `ESP_BT_GAP_REMOVE_BOND_DEV_COMPLETE_EVT`. |
| **A2DP Sink** (`esp_a2dp_api.h`) | | | |
| `esp_a2d_sink_register_stream_endpoint` | `esp_a2dp_api.h:453` | `esp_err_t esp_a2d_sink_register_stream_endpoint(uint8_t seid, const esp_a2d_mcc_t *mcc)` | Apenas **SBC** suportado no v6.0.2 como sink. Chamar após `esp_a2d_sink_init()` e antes de conectar. `seid` começa em 0 e max é `ESP_A2D_MAX_SEPS=1`. |
| `esp_a2d_mcc_t` / SBC CIE | `esp_a2dp_api.h:65-137` | `esp_a2d_cie_sbc_t` contém: `ch_mode`, `samp_freq`, `alloc_mthd`, `num_subbands`, `block_len`, `min_bitpool`, `max_bitpool` | Para forçar bitpool alto: setar `min_bitpool=53`, `max_bitpool=53` no SEP. |
| `esp_a2d_sink_connect` | `esp_a2dp_api.h:482` | `esp_err_t esp_a2d_sink_connect(esp_bd_addr_t remote_bda)` | Inicia conexão A2DP outgoing. |
| `esp_a2d_sink_set_delay_value` | `esp_a2dp_api.h:514` | `esp_err_t esp_a2d_sink_set_delay_value(uint16_t delay_value)` | Valor em **1/10 ms**. Mínimo é 120 ms (default). Informa ao source o delay do buffer. |
| `ESP_A2D_AUDIO_CFG_EVT` | `esp_a2dp_api.h:225` | Event `esp_a2d_cb_event_t` | Evento onde `param->audio_cfg.mcc` traz o codec+taxa negociados. Usar para programar I2S. |
| `esp_a2d_sink_register_audio_data_callback` | `esp_a2dp_api.h:419` | `esp_err_t esp_a2d_sink_register_audio_data_callback(esp_a2d_sink_audio_data_cb_t callback)` | **API nova v6**: entrega `esp_a2d_audio_buff_t*` com dados **não decodificados**. Chamar `esp_a2d_audio_buff_free` após consumir. **NÃO há mais callback de PCM direto no v6.** |
| **HFP Client** (`esp_hf_client_api.h`) | | | |
| `esp_hf_client_connect` | `esp_hf_client_api.h:368` | `esp_err_t esp_hf_client_connect(esp_bd_addr_t remote_bda)` | Inicia conexão HFP. |
| `esp_hf_client_connect_audio` | `esp_hf_client_api.h:397` | `esp_err_t esp_hf_client_connect_audio(esp_bd_addr_t remote_bda)` | Inicia áudio SCO/eSCO separadamente. |
| `esp_hf_client_answer_call` | `esp_hf_client_api.h:529` | `esp_err_t esp_hf_client_answer_call(void)` | Atende chamada. |
| `esp_hf_client_reject_call` | `esp_hf_client_api.h:542` | `esp_err_t esp_hf_client_reject_call(void)` | Rejeita chamada. |
| **Eventos HFP** | `esp_hf_client_api.h:87-116` | enum `esp_hf_client_cb_event_t` | Inclui: `AUDIO_STATE_EVT` (CVSD/mSBC via `ESP_HF_CLIENT_AUDIO_STATE_CONNECTED_MSBC`), `CLIP_EVT`, `CIND_*` para indicadores. |
| **Common Utils** | (N/A) | | Em v6.0.2, `bt_app_core_utils`, `a2dp_sink_common_utils` etc. saíram dos components IDF e ficam espalhados em `examples/bluetooth/bluedroid/classic_bt/`. **Devem ser copiados e geridos em `main/` ou `components/`** (F-13). |
