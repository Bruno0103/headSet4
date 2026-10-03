# Plano do Agente 06 (Bugfix BT_GAP SSP)
1. Analisar crash no boot devido a `esp_bt_gap_set_security_param` falhando com `ESP_ERR_NOT_SUPPORTED`.
2. Identificar que `bt_gap_start()` em `bt_gap.c` tentava configurar parâmetros de SSP sem verificar se SSP estava habilitado no menuconfig (`CONFIG_EXAMPLE_SSP_ENABLED`).
3. Envolver a chamada de `esp_bt_gap_set_security_param` com `#if (CONFIG_EXAMPLE_SSP_ENABLED == true)`.
4. Compilar e aprovar no `06-done.md`.
