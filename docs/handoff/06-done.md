# Agente 06 Done

## Feito
- Corrigido o crash do `ESP_ERROR_CHECK` durante a inicialização do GAP Bluetooth (`E (2329) BT_GAP: esp_bt_gap_set_security_param is not supported when ssp_en in esp_bluedroid_config_t is disabled!`).
  - Em `bt_gap.c`, a configuração de SSP IOCAP mode estava sendo aplicada incondicionalmente, mesmo quando `CONFIG_EXAMPLE_SSP_ENABLED` estava falso ou desabilitado, resultando em erro.
  - O código foi envolvido em uma verificação `#if (CONFIG_EXAMPLE_SSP_ENABLED == true)`, resolvendo o problema de dependência do Kconfig e evitando a chamada falha.

## Decisões
- O SSP agora é adequadamente ignorado na inicialização local do módulo GAP (`bt_gap.c`) quando não ativado no `menuconfig`, em conformidade com o que o wrapper `bredr_app_common_utils` já faz na configuração geral da `esp_bluedroid_config_t`.

## O próximo agente precisa saber
- A inicialização deve seguir sem interrupções nos serviços de áudio (resolvido antes) e agora no início da stack BT (GAP). Se mais erros de suporte a hardware ou flags aparecerem, verifique as configurações em `sdkconfig` (ou `menuconfig`) comparadas ao código invocado em `bluetooth.c` ou submódulos.
