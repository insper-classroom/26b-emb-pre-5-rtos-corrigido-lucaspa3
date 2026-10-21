#include <FreeRTOS.h>   // Núcleo do FreeRTOS
#include <task.h>       // Tasks
#include <semphr.h>     // Semáforos
#include <queue.h>      // Filas

#include "pico/stdlib.h"
#include <stdio.h>

// Pinos (const: fixos)
const int BTN_PIN_R = 28;
const int BTN_PIN_G = 26;

const int LED_PIN_R = 4;
const int LED_PIN_G = 6;

// Recursos do RTOS (únicas globais permitidas, Rule 4.4)
QueueHandle_t xQueueButId;      // BTN task R -> LED task R (leva o delay)
QueueHandle_t xQueueButId_g;    // BTN task G -> LED task G (leva o delay)
QueueHandle_t xQueueIrqG;       // ISR -> BTN task G (leva o gpio que disparou)
SemaphoreHandle_t xSemaphore_r; // ISR -> BTN task R (só avisa "apertou")

// CALLBACK ÚNICO de GPIO, compartilhado pelos dois botões.
// Roda em contexto de interrupção: tem que ser curto e só usar a API FromISR (Rule 4.1).
void btn_callback(uint gpio, uint32_t events) {
    // events é bitmask: testa o bit com &, nunca com == (regra do lab de ISR)
    if (events & GPIO_IRQ_EDGE_FALL) {
        // O parâmetro gpio é a única forma de saber qual botão disparou
        if (gpio == BTN_PIN_R) {
            // R: só avisa que aconteceu (semáforo)
            xSemaphoreGiveFromISR(xSemaphore_r, 0);
        } else if (gpio == BTN_PIN_G) {
            // G: envia um VALOR (o número do pino) pela fila.
            // O último argumento (0) é o "acordou task de maior prioridade?",
            // que não usamos aqui (mesmo padrão dos guias).
            xQueueSendFromISR(xQueueIrqG, &gpio, 0);
        }
    }
}

// TASK DO LED R (consumidora da fila R), igual ao exe3
void led_1_task(void *p) {
    gpio_init(LED_PIN_R);
    gpio_set_dir(LED_PIN_R, GPIO_OUT);

    int delay = 0;   // último delay recebido; 0 = LED parado

    while (true) {
        // Timeout 0: não bloqueia; sem dado novo, mantém o delay antigo
        if (xQueueReceive(xQueueButId, &delay, 0)) {
            printf("%d\n", delay);
        }

        if (delay > 0) {
            gpio_put(LED_PIN_R, 1);
            vTaskDelay(pdMS_TO_TICKS(delay));
            gpio_put(LED_PIN_R, 0);
            vTaskDelay(pdMS_TO_TICKS(delay));
        }
    }
}

// TASK DO LED G: mesma lógica, lendo a fila G
void led_2_task(void *p) {
    gpio_init(LED_PIN_G);
    gpio_set_dir(LED_PIN_G, GPIO_OUT);

    int delay = 0;

    while (true) {
        if (xQueueReceive(xQueueButId_g, &delay, 0)) {
            printf("%d\n", delay);
        }

        if (delay > 0) {
            gpio_put(LED_PIN_G, 1);
            vTaskDelay(pdMS_TO_TICKS(delay));
            gpio_put(LED_PIN_G, 0);
            vTaskDelay(pdMS_TO_TICKS(delay));
        }
    }
}

// TASK DO BOTÃO R: espera o semáforo da ISR (não faz mais polling)
void btn_1_task(void *p) {
    gpio_init(BTN_PIN_R);
    gpio_set_dir(BTN_PIN_R, GPIO_IN);
    gpio_pull_up(BTN_PIN_R);
    // Instala o callback e habilita a IRQ de borda de descida (apertou)
    gpio_set_irq_enabled_with_callback(BTN_PIN_R, GPIO_IRQ_EDGE_FALL, true,
                                       &btn_callback);

    int delay = 0;

    while (true) {
        // Dorme até a ISR dar o semáforo (ou 500 ms de timeout)
        if (xSemaphoreTake(xSemaphore_r, pdMS_TO_TICKS(500)) == pdTRUE) {
            // Ciclo 100, 200, ..., 1000, 100, ...
            if (delay < 1000) {
                delay += 100;
            } else {
                delay = 100;
            }
            printf("delay btn %d \n", delay);
            // Task -> task: API sem FromISR (Rule 4.2)
            xQueueSend(xQueueButId, &delay, 0);
        }
    }
}

// TASK DO BOTÃO G: espera a fila da ISR
void btn_2_task(void *p) {
    gpio_init(BTN_PIN_G);
    gpio_set_dir(BTN_PIN_G, GPIO_IN);
    gpio_pull_up(BTN_PIN_G);
    // Mesma chamada com callback (ver explicação abaixo do código)
    gpio_set_irq_enabled_with_callback(BTN_PIN_G, GPIO_IRQ_EDGE_FALL, true,
                                       &btn_callback);

    int delay = 0;
    uint gpio_evento = 0;   // recebe o valor que a ISR colocou na fila

    while (true) {
        // Dorme até a ISR enviar algo (ou 500 ms de timeout)
        if (xQueueReceive(xQueueIrqG, &gpio_evento, pdMS_TO_TICKS(500)) == pdTRUE) {
            if (delay < 1000) {
                delay += 100;
            } else {
                delay = 100;
            }
            printf("delay btn %u: %d \n", gpio_evento, delay);
            xQueueSend(xQueueButId_g, &delay, 0);
        }
    }
}

int main() {
    stdio_init_all();
    printf("Start RTOS \n");

    // Cria TODOS os recursos antes do scheduler.
    // sizeof deve bater com o tipo enviado: delay é int, gpio é uint.
    xQueueButId   = xQueueCreate(32, sizeof(int));
    xQueueButId_g = xQueueCreate(32, sizeof(int));
    xQueueIrqG    = xQueueCreate(32, sizeof(uint));
    xSemaphore_r  = xSemaphoreCreateBinary();

    xTaskCreate(led_1_task, "LED_Task 1", 256, NULL, 1, NULL);
    xTaskCreate(btn_1_task, "BTN_Task 1", 256, NULL, 1, NULL);
    xTaskCreate(led_2_task, "LED_Task 2", 256, NULL, 1, NULL);
    xTaskCreate(btn_2_task, "BTN_Task 2", 256, NULL, 1, NULL);

    vTaskStartScheduler();

    while (true) {}
}