/**
 * @file battery.h
 * @brief Definições e funções para leitura da tensão da bateria.
 */

#ifndef BATTERY_H
#define BATTERY_H

/**
 * @brief Inicializa os pinos e o ADC para a leitura da bateria.
 * 
 * Configura o GPIO14 como saída para controle do MOSFET e o
 * GPIO34 como entrada analógica (ADC1) para a leitura da tensão.
 */
void battery_init(void);

/**
 * @brief Lê a tensão da bateria, converte para Volts reais e exibe no terminal.
 * 
 * Liga o MOSFET através do GPIO14, realiza a leitura analógica no GPIO34,
 * multiplica o valor obtido por 3 (devido ao divisor de tensão 200K/100K) e 
 * exibe o resultado no terminal. Após a leitura, desliga o MOSFET.
 */
void battery_read_and_print(void);

/**
 * @brief Tarefa FreeRTOS para monitorar a bateria periodicamente.
 * 
 * @param pvParameters Parâmetros da tarefa (não utilizado).
 */
void battery_task(void *pvParameters);

#endif // BATTERY_H
