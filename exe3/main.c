#include <FreeRTOS.h>   // Núcleo do FreeRTOS
#include <task.h>       // Tasks: xTaskCreate, vTaskDelay, vTaskStartScheduler
#include <semphr.h>     // Semáforos (não usados neste exercício)
#include <queue.h>      // Filas: xQueueCreate, xQueueSend, xQueueReceive

#include "pico/stdlib.h"
#include <stdio.h>

// Pinos (const: fixos)
const int BTN_PIN_R = 28;
const int BTN_PIN_G = 26;

const int LED_PIN_R = 4;
const int LED_PIN_G = 6;

// Filas: são o "semáforo que carrega um valor".
// Cada par botão -> LED tem a sua própria fila (dois canais independentes).
// Globais porque duas tasks precisam enxergá-las. Rule 4.4: recurso do RTOS.
QueueHandle_t xQueueButId;     // canal BTN R -> LED R
QueueHandle_t xQueueButId_g;   // canal BTN G -> LED G

// TASK DO LED R (consumidora da fila R)
void led_1_task(void *p) {
    gpio_init(LED_PIN_R);
    gpio_set_dir(LED_PIN_R, GPIO_OUT);

    // Guarda o último delay recebido. Começa em 0 = LED parado.
    int delay = 0;

    while (true) {
        // Tenta tirar um valor da fila SEM esperar (timeout 0).
        //  - Se havia dado: ele é COPIADO para "delay" (por isso o &) e entra no if.
        //  - Se a fila estava vazia: retorna na hora e "delay" mantém o valor antigo.
        // Resultado: o LED continua piscando no último ritmo até chegar um novo.
        if (xQueueReceive(xQueueButId, &delay, 0)) {
            printf("%d\n", delay);
        }

        // Só pisca depois que o primeiro valor chegou.
        // Meio período aceso, meio apagado: o delay define a frequência.
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

// TASK DO BOTÃO R (produtora da fila R)
void btn_1_task(void *p) {
    gpio_init(BTN_PIN_R);
    gpio_set_dir(BTN_PIN_R, GPIO_IN);
    gpio_pull_up(BTN_PIN_R);   // Pull-up: solto = 1, apertado = 0

    // Delay atual. Variável LOCAL da task: não precisa ser global
    // nem volatile, porque só esta task a usa.
    int delay = 0;

    while (true) {
        // Detecta "apertou e soltou" (mesmo padrão do exe2)
        if (!gpio_get(BTN_PIN_R)) {
            while (!gpio_get(BTN_PIN_R)) {
                vTaskDelay(pdMS_TO_TICKS(1));
            }

            // Cada aperto soma 100 ms. Passou de 1000, volta para 100.
            // Sequência: 100, 200, ..., 1000, 100, 200, ...
            if (delay < 1000) {
                delay += 100;
            } else {
                delay = 100;
            }
            printf("delay btn %d \n", delay);

            // Envia o VALOR para a fila (a fila copia a partir de &delay).
            // Timeout 0: se a fila estiver cheia, desiste na hora.
            // Sem FromISR porque estamos numa task (Rule 4.2).
            xQueueSend(xQueueButId, &delay, 0);
        }
    }
}

// TASK DO BOTÃO G: mesma lógica, escrevendo na fila G
void btn_2_task(void *p) {
    gpio_init(BTN_PIN_G);
    gpio_set_dir(BTN_PIN_G, GPIO_IN);
    gpio_pull_up(BTN_PIN_G);

    int delay = 0;

    while (true) {
        if (!gpio_get(BTN_PIN_G)) {
            while (!gpio_get(BTN_PIN_G)) {
                vTaskDelay(pdMS_TO_TICKS(1));
            }

            if (delay < 1000) {
                delay += 100;
            } else {
                delay = 100;
            }
            printf("delay btn %d \n", delay);

            xQueueSend(xQueueButId_g, &delay, 0);
        }
    }
}

int main() {
    stdio_init_all();
    printf("Start RTOS \n");

    // Cria as filas ANTES do scheduler: 32 posições, cada uma do tamanho de um int.
    // O sizeof precisa bater com o tipo enviado e recebido (int delay).
    xQueueButId   = xQueueCreate(32, sizeof(int));
    xQueueButId_g = xQueueCreate(32, sizeof(int));

    // Registra as 4 tasks, todas com a mesma prioridade
    xTaskCreate(led_1_task, "LED_Task 1", 256, NULL, 1, NULL);
    xTaskCreate(btn_1_task, "BTN_Task 1", 256, NULL, 1, NULL);
    xTaskCreate(led_2_task, "LED_Task 2", 256, NULL, 1, NULL);
    xTaskCreate(btn_2_task, "BTN_Task 2", 256, NULL, 1, NULL);

    // Entrega o controle ao FreeRTOS. Nunca deve retornar.
    vTaskStartScheduler();

    // Rede de segurança
    while (true) {}
}