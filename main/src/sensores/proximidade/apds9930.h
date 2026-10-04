#ifndef APDS9930_H
#define APDS9930_H

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Inicializa o sensor de proximidade APDS-9930 e inicia a task de monitoramento.
 *
 * @return esp_err_t ESP_OK em caso de sucesso.
 */
esp_err_t apds9930_init(void);

/**
 * @brief Retorna o status atual do sensor de presenca.
 *
 * @return true se o fone estiver na cabeca (presenca detectada).
 * @return false se o fone estiver fora da cabeca.
 */
bool apds9930_is_headset_on(void);

/**
 * @brief Task do sensor de proximidade.
 * 
 * @param pvParameters Parâmetros da task.
 */
void apds9930_task(void *pvParameters);

#ifdef __cplusplus
}
#endif

#endif // APDS9930_H
