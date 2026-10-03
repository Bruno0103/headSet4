# Agente 05 Done

## Feito
- Corrigido o crash do `ESP_ERROR_CHECK` durante o boot na inicialização de áudio:
  - O log do monitor indicava `E (1638) wm8960: update em reg 0x04 nunca escrito por inteiro`.
  - Isso acontecia porque `audio_codec_set_sample_rate` faz um update parcial (`wm8960_update`) no registrador de clock (`WM8960_R_CLOCK1`, que é o 0x04) sem antes ter havido um `wm8960_write` completo. O nosso driver bloqueia updates se a shadow copy for inválida.
  - Adicionado `W(WM8960_R_CLOCK1, 0x000)` em `audio_codec_init` antes de chamar `audio_codec_set_sample_rate`, resolvendo a dependência.

## Decisões
- Inicializei `WM8960_R_CLOCK1` com `0x000`, que é o valor padrão (reset) do componente, garantindo que o shadow register passe a ser considerado "conhecido" sem alterar o comportamento esperado do chip naquele momento, permitindo que a posterior chamada de atualização de sample rate consiga alterar bits isolados do clock corretamente.

## O próximo agente precisa saber
- O sistema já deve inicializar o codec sem dar crash nesse registrador. A monitoração deve prosseguir normal. Caso apresente falhas em outros registradores, usar a mesma lógica (escrever por inteiro antes do update).
