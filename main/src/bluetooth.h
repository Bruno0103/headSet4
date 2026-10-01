#pragma once

#include <stdint.h>

/** Sobe a pilha Bluetooth Classic (nao inicia perfis ainda). */
void bluetooth_init(void);

/** Task de uma execucao: ativa A2DP, AVRCP e HFP e torna o dispositivo visivel. */
void bluetooth_task(void *arg);
