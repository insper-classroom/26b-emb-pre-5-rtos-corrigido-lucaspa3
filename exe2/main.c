#include <FreeRTOS.h>   // Núcleo do FreeRTOS (tipos e macros básicos)
#include <task.h>       // Tasks: xTaskCreate, vTaskDelay, vTaskStartScheduler
#include <semphr.h>     // Semáforos: xSemaphoreCreateBinary, Give, Take
#include <queue.h>      // Filas (ainda não usadas neste exercício)

#include "pico/stdlib.h"
#include <stdio.h>

// Pinos (const: são fixos e nunca mudam em tempo de execução)
const int BTN_PIN_R = 28;
const int BTN_PIN_G = 26;

const int LED_PIN_R = 4;
const int LED_PIN_G = 6;

// Semáforos: substituem as flags do bare-metal.
// Ficam globais porque duas tasks diferentes precisam enxergá-los
// (a do botão dá, a do LED pega). Rule 4.4: global só se for recurso do RTOS.
SemaphoreHandle_t xSemaphore_r;
SemaphoreHandle_t xSemaphore_g;

// TASK DO LED R (consumidora do semáforo R)
// Toda task tem a assinatura void nome(void *p). O "p" recebe o 4º argumento
// do xTaskCreate (aqui é NULL, então não usamos).
void led_1_task(void *p) {
  // Configuração roda uma única vez, antes do laço
  gpio_init(LED_PIN_R);
  gpio_set_dir(LED_PIN_R, GPIO_OUT);

  int delay = 250;

  // Task nunca retorna: laço infinito
  while (true) {

    // Espera o semáforo por até 500 ms. Enquanto espera, a task DORME
    // (estado Blocked) e a CPU fica livre para as outras tasks.
    //  - pdTRUE: alguém deu Give -> pisca o LED. O Take já "zera a flag".
    //  - pdFALSE: deu timeout -> não entra no if, e o laço espera de novo.
    // pdMS_TO_TICKS converte milissegundos em ticks (a unidade do RTOS).
    if (xSemaphoreTake(xSemaphore_r, pdMS_TO_TICKS(500)) == pdTRUE) {
      gpio_put(LED_PIN_R, 1);
      vTaskDelay(pdMS_TO_TICKS(delay));   // dorme, liberando a CPU
      gpio_put(LED_PIN_R, 0);
      vTaskDelay(pdMS_TO_TICKS(delay));
    }
  }
}

// TASK DO LED G: mesma lógica, com o semáforo G
void led_2_task(void *p) {
  gpio_init(LED_PIN_G);
  gpio_set_dir(LED_PIN_G, GPIO_OUT);

  int delay = 250;

  while (true) {
    if (xSemaphoreTake(xSemaphore_g, pdMS_TO_TICKS(500)) == pdTRUE) {
      gpio_put(LED_PIN_G, 1);
      vTaskDelay(pdMS_TO_TICKS(delay));
      gpio_put(LED_PIN_G, 0);
      vTaskDelay(pdMS_TO_TICKS(delay));
    }
  }
}

// TASK DO BOTÃO R (produtora do semáforo R)
void btn_1_task(void *p) {
  gpio_init(BTN_PIN_R);
  gpio_set_dir(BTN_PIN_R, GPIO_IN);
  gpio_pull_up(BTN_PIN_R);   // Pull-up: solto = 1, apertado = 0

  while (true) {
    // Polling: lê o NÍVEL do pino. !gpio_get = botão apertado (nível 0)
    if (!gpio_get(BTN_PIN_R)) {
      // Espera o botão ser SOLTO, dormindo 1 ms a cada verificação.
      // Assim o evento é "apertou e soltou", e não "está apertado".
      while (!gpio_get(BTN_PIN_R)) {
        vTaskDelay(pdMS_TO_TICKS(1));
      }
      // Equivale a "flag = 1": acorda a led_1_task.
      // Sem FromISR porque estamos numa task, não numa ISR (Rule 4.2).
      xSemaphoreGive(xSemaphore_r);
    }
  }
}

// TASK DO BOTÃO G: mesma lógica, com o semáforo G
void btn_2_task(void *p) {
  gpio_init(BTN_PIN_G);
  gpio_set_dir(BTN_PIN_G, GPIO_IN);
  gpio_pull_up(BTN_PIN_G);

  while (true) {
    if (!gpio_get(BTN_PIN_G)) {
      while (!gpio_get(BTN_PIN_G)) {
        vTaskDelay(pdMS_TO_TICKS(1));
      }
      xSemaphoreGive(xSemaphore_g);
    }
  }
}

int main() {
  stdio_init_all();
  printf("Start RTOS \n");

  // Cria os semáforos ANTES de qualquer task rodar.
  // Usar um semáforo não criado trava o firmware.
  // O semáforo binário nasce "vazio": o primeiro Take só passa depois de um Give.
  xSemaphore_r = xSemaphoreCreateBinary();
  xSemaphore_g = xSemaphoreCreateBinary();

  // Registra as tasks no FreeRTOS. Argumentos:
  // (função, nome para debug, tamanho da stack em palavras,
  //  parâmetro passado em "p", prioridade, handle de saída)
  // Todas com prioridade 1: o RTOS alterna entre elas.
  xTaskCreate(led_1_task, "LED_Task 1", 256, NULL, 1, NULL);
  xTaskCreate(btn_1_task, "BTN_Task 1", 256, NULL, 1, NULL);
  xTaskCreate(led_2_task, "LED_Task 2", 256, NULL, 1, NULL);
  xTaskCreate(btn_2_task, "BTN_Task 2", 256, NULL, 1, NULL);

  // Entrega o controle do core ao FreeRTOS. Se der certo, nunca retorna.
  vTaskStartScheduler();

  // Rede de segurança: só é alcançada se o scheduler falhar ao iniciar
  while (true) {}
}