/**
 * Copyright (c) 2020 Raspberry Pi (Trading) Ltd.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <FreeRTOS.h>
#include <task.h>
#include <semphr.h>
#include <queue.h>

#include <stdio.h>
#include <string.h>
#include "pico/stdlib.h"
#include "hardware/gpio.h"
#include "hardware/timer.h"
#include "hardware/irq.h"

// Pinos (const: fixos)
const int BTN_PIN_R = 28;
const int BTN_PIN_Y = 21;

const int LED_PIN_R = 5;
const int LED_PIN_Y = 10;

// Recursos do RTOS: as ÚNICAS globais permitidas pelo enunciado
QueueHandle_t xQueueBtn;           // callback -> btn_task (leva QUAL botão)
SemaphoreHandle_t xSemaphoreLedR;  // btn_task -> led_r_task ("alterne")
SemaphoreHandle_t xSemaphoreLedY;  // btn_task -> led_y_task ("alterne")

// CALLBACK ÚNICO dos dois botões (contexto de interrupção)
// Curto: só anota qual pino disparou e envia pela fila. Só API FromISR (Rule 4.1).
void btn_callback(uint gpio, uint32_t events) {
    if (events & GPIO_IRQ_EDGE_FALL) {          // bitmask: testar com &
        xQueueSendFromISR(xQueueBtn, &gpio, 0); // envia o número do pino
    }
}

// TASK DOS BOTÕES: traduz "qual botão" em "qual LED alterna"
void btn_task(void *p) {
    // Configura os DOIS botões aqui, na mesma task.
    // Assim o callback e as duas IRQs ficam no mesmo core.
    gpio_init(BTN_PIN_R);
    gpio_set_dir(BTN_PIN_R, GPIO_IN);
    gpio_pull_up(BTN_PIN_R);

    gpio_init(BTN_PIN_Y);
    gpio_set_dir(BTN_PIN_Y, GPIO_IN);
    gpio_pull_up(BTN_PIN_Y);

    // 1º pino: instala o callback. 2º pino: só habilita (callback é único).
    gpio_set_irq_enabled_with_callback(BTN_PIN_R, GPIO_IRQ_EDGE_FALL, true,
                                       &btn_callback);
    gpio_set_irq_enabled(BTN_PIN_Y, GPIO_IRQ_EDGE_FALL, true);

    uint gpio = 0;   // variável LOCAL que recebe o dado da fila

    while (true) {
        // Dorme até a ISR enviar algo (timeout de 500 ms; se estourar, só repete)
        if (xQueueReceive(xQueueBtn, &gpio, pdMS_TO_TICKS(500)) == pdTRUE) {
            // Task -> task: API sem FromISR (Rule 4.2)
            if (gpio == BTN_PIN_R) {
                xSemaphoreGive(xSemaphoreLedR);
            } else if (gpio == BTN_PIN_Y) {
                xSemaphoreGive(xSemaphoreLedY);
            }
        }
    }
}

// TASK DO LED VERMELHO
void led_r_task(void *p) {
    gpio_init(LED_PIN_R);
    gpio_set_dir(LED_PIN_R, GPIO_OUT);
    gpio_put(LED_PIN_R, 0);

    int delay = 100;    // meio período do pisca (ms), exigido pelo enunciado
    int piscando = 0;   // estado do LED: variável LOCAL (nada de global)

    while (true) {
        if (!piscando) {
            // MODO PARADO: dorme esperando um aperto (não gasta CPU)
            if (xSemaphoreTake(xSemaphoreLedR, pdMS_TO_TICKS(500)) == pdTRUE) {
                piscando = 1;
            }
        } else {
            // MODO PISCANDO: confere o semáforo SEM esperar (timeout 0)
            if (xSemaphoreTake(xSemaphoreLedR, 0) == pdTRUE) {
                // Novo aperto: para e garante o LED apagado
                piscando = 0;
                gpio_put(LED_PIN_R, 0);
            } else {
                // Nenhum aperto: pisca uma vez e volta para conferir de novo
                gpio_put(LED_PIN_R, 1);
                vTaskDelay(pdMS_TO_TICKS(delay));
                gpio_put(LED_PIN_R, 0);
                vTaskDelay(pdMS_TO_TICKS(delay));
            }
        }
    }
}

// TASK DO LED AMARELO: mesma lógica, com o semáforo Y
void led_y_task(void *p) {
    gpio_init(LED_PIN_Y);
    gpio_set_dir(LED_PIN_Y, GPIO_OUT);
    gpio_put(LED_PIN_Y, 0);

    int delay = 100;
    int piscando = 0;

    while (true) {
        if (!piscando) {
            if (xSemaphoreTake(xSemaphoreLedY, pdMS_TO_TICKS(500)) == pdTRUE) {
                piscando = 1;
            }
        } else {
            if (xSemaphoreTake(xSemaphoreLedY, 0) == pdTRUE) {
                piscando = 0;
                gpio_put(LED_PIN_Y, 0);
            } else {
                gpio_put(LED_PIN_Y, 1);
                vTaskDelay(pdMS_TO_TICKS(delay));
                gpio_put(LED_PIN_Y, 0);
                vTaskDelay(pdMS_TO_TICKS(delay));
            }
        }
    }
}

int main() {
    stdio_init_all();

    // Cria TODOS os recursos antes do scheduler.
    // A fila guarda o número do pino: sizeof(uint), mesmo tipo do parâmetro gpio.
    xQueueBtn      = xQueueCreate(32, sizeof(uint));
    xSemaphoreLedR = xSemaphoreCreateBinary();
    xSemaphoreLedY = xSemaphoreCreateBinary();

    // As 3 tasks do diagrama
    xTaskCreate(btn_task,   "BTN_Task",   256, NULL, 1, NULL);
    xTaskCreate(led_r_task, "LED_R_Task", 256, NULL, 1, NULL);
    xTaskCreate(led_y_task, "LED_Y_Task", 256, NULL, 1, NULL);

    vTaskStartScheduler();

    while (1) {}

    return 0;
}