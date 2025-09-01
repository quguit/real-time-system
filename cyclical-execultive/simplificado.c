#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "driver/timer.h"
#include "esp_system.h"


// Configuração do Timer

#define TIMER_BASE_CLK   80000000   // Clock base: 80 MHz
#define TIMER_DIVIDER    80         // Divide -> 1 MHz (1 tick = 1 us)
#define TIMER_SCALE      (TIMER_BASE_CLK / TIMER_DIVIDER)
#define TIMER_INTERVAL_MS 1000      // Intervalo (1s)
#define TIMER_GROUP      TIMER_GROUP_0
#define TIMER_INDEX      TIMER_0

// fila de eventos 
static QueueHandle_t timer_evt_queue;


// evento enviado pela ISR

typedef struct {
    int timer_idx;
    uint64_t timer_counter_value;
} timer_event_t;

// Interrupção timer

static bool IRAM_ATTR timer_isr_callback(void *args) {
    timer_event_t evt;
    evt.timer_idx = (int) args;
    timer_get_counter_value(TIMER_GROUP, TIMER_INDEX, &evt.timer_counter_value);
    xQueueSendFromISR(timer_evt_queue, &evt, NULL);
    return pdFALSE;
}

// tarefas
void função_t1(){
  for(volatile int x = 0; x < 100000; x++){
    printf("T1\n");
  }
}


// Task que processa o timer

void timer_task(void *arg) {
    timer_event_t evt;
    int estado = 0;
    static int intervalo_ms = TIMER_INTERVAL_MS;

    while (1) {
        if (xQueueReceive(timer_evt_queue, &evt, portMAX_DELAY)) {
            função_t1();

            // Ajusta o intervalo para aumentar a carga
            intervalo_ms = (intervalo_ms > 50) ? intervalo_ms - 500 : 50;  
            // mínimo = 50ms
            timer_set_alarm_value(TIMER_GROUP, TIMER_INDEX, intervalo_ms * 1000);
        }
    }
}

// ===============================
// Função principal
// ===============================
void app_main(void) {
    timer_evt_queue = xQueueCreate(10, sizeof(timer_event_t));

    timer_config_t config = {
        .divider = TIMER_DIVIDER,
        .counter_dir = TIMER_COUNT_UP,
        .counter_en = TIMER_PAUSE,
        .alarm_en = TIMER_ALARM_EN,
        .auto_reload = true,
    };

    timer_init(TIMER_GROUP, TIMER_INDEX, &config);
    timer_set_counter_value(TIMER_GROUP, TIMER_INDEX, 0);
    //ajuste de intervalo
    timer_set_alarm_value(TIMER_GROUP, TIMER_INDEX, TIMER_INTERVAL_MS * 1000);
    timer_enable_intr(TIMER_GROUP, TIMER_INDEX);

    timer_isr_callback_add(TIMER_GROUP, TIMER_INDEX, timer_isr_callback, (void *) TIMER_INDEX, 0);
    timer_start(TIMER_GROUP, TIMER_INDEX);

    xTaskCreate(timer_task, "timer_task", 4096, NULL, 5, NULL);
}