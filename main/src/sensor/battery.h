/**
 * @file battery.h
 * @brief Definições e funções para leitura da tensão da bateria.
 */

#ifndef BATTERY_H
#define BATTERY_H

#include "esp_err.h"

/**
 * @brief Inicializa os pinos de controle e o periférico ADC1 para medição da bateria.
 * 
 * Configura o pino BAT_CTRL_PIN (GPIO14) como saída para controle do MOSFET do divisor de tensão,
 * e inicializa a unidade ADC1 (canal 6 / GPIO34) com calibração de fábrica por line fitting.
 * 
 * Esta função deve ser chamada na inicialização do sistema (antes de instanciar battery_task).
 * Não utiliza ESP_ERROR_CHECK internamente, retornando os códigos de erro apropriados.
 * 
 * @return esp_err_t ESP_OK em caso de sucesso na inicialização do hardware,
 *                   ou código de erro retornado pelos drivers de GPIO ou ADC.
 */
esp_err_t battery_init(void);

/**
 * @brief Tarefa FreeRTOS para monitorar a bateria periodicamente com resiliência.
 * 
 * Executa a amostragem com filtro de mediana, conversão de tensão, cálculo de percentual
 * e publicação de eventos HEADSET_EVT_BATTERY. Caso ocorram erros transitórios no ADC,
 * aplica retentativas com backoff exponencial sem causar panic ou crash no sistema.
 * 
 * @param pvParameters Parâmetros da tarefa FreeRTOS (não utilizado).
 */
void battery_task(void *pvParameters);

#endif // BATTERY_H

