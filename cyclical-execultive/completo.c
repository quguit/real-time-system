#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/timer.h"
#include "esp_rom_sys.h"
#include "driver/gpio.h"

// =================================================================
// 1. DEFINIÇÕES DO SISTEMA PERIÓDICO
// =================================================================

#define TIMER_DIVIDER         80      // 1 tick = 1 microsegundo (us)
#define MINOR_CYCLE_MS        5       // Ciclo Menor (MDC dos períodos)
#define MINOR_CYCLE_US        (MINOR_CYCLE_MS * 1000)

// --- Tempos de Computação (Ci) ajustados (-0,5ms cada) ---
#define T1_COMPUTATION_US 4500   // 4,5 ms
#define T2_COMPUTATION_US 7500   // 7,5 ms
#define T3_COMPUTATION_US 4500   // 4,5 ms
#define T4_COMPUTATION_US 3500   // 3,5 ms
#define T5_COMPUTATION_US 9500   // 9,5 ms

#define OSCILLOSCOPE_PIN      GPIO_NUM_14

volatile int timer_interrupt_flag = 0;

// =================================================================
// 2. ISR E FUNÇÃO DE ESPERA
// =================================================================

static void IRAM_ATTR timer_isr_callback(void *arg) {
    timer_group_clr_intr_status_in_isr(TIMER_GROUP_0, TIMER_0);
    timer_group_enable_alarm_in_isr(TIMER_GROUP_0, TIMER_0);
    timer_interrupt_flag = 1;
}

void wait_for_interrupt() {
    while (timer_interrupt_flag == 0) {
        __asm__ __volatile__("waiti 0");
    }
    timer_interrupt_flag = 0;
}

// =================================================================
// 3. IMPLEMENTAÇÃO DAS TAREFAS
// =================================================================
void tarefa_t1() { 
    gpio_set_level(OSCILLOSCOPE_PIN, 1);
    esp_rom_delay_us(T1_COMPUTATION_US); 
    gpio_set_level(OSCILLOSCOPE_PIN, 0);
}

void tarefa_t2() { 
    gpio_set_level(OSCILLOSCOPE_PIN, 1);
    esp_rom_delay_us(T2_COMPUTATION_US); 
    gpio_set_level(OSCILLOSCOPE_PIN, 0);
}

void tarefa_t3() { 
    gpio_set_level(OSCILLOSCOPE_PIN, 1);
    esp_rom_delay_us(T3_COMPUTATION_US); 
    gpio_set_level(OSCILLOSCOPE_PIN, 0);
}

void tarefa_t4() { 
    gpio_set_level(OSCILLOSCOPE_PIN, 1);
    esp_rom_delay_us(T4_COMPUTATION_US); 
    gpio_set_level(OSCILLOSCOPE_PIN, 0);
}

void tarefa_t5() { 
    gpio_set_level(OSCILLOSCOPE_PIN, 1);
    esp_rom_delay_us(T5_COMPUTATION_US); 
    gpio_set_level(OSCILLOSCOPE_PIN, 0);
}

void app_main(void) {
    // --- Configurações Iniciais ---
    gpio_reset_pin(OSCILLOSCOPE_PIN);
    gpio_set_direction(OSCILLOSCOPE_PIN, GPIO_MODE_OUTPUT);
    
    timer_config_t config = {
        .divider = TIMER_DIVIDER, .counter_dir = TIMER_COUNT_UP, .counter_en = TIMER_PAUSE,
        .alarm_en = TIMER_ALARM_EN, .auto_reload = true,
    };
    timer_init(TIMER_GROUP_0, TIMER_0, &config);
    timer_set_counter_value(TIMER_GROUP_0, TIMER_0, 0);
    timer_set_alarm_value(TIMER_GROUP_0, TIMER_0, MINOR_CYCLE_US);
    timer_enable_intr(TIMER_GROUP_0, TIMER_0);
    timer_isr_register(TIMER_GROUP_0, TIMER_0, timer_isr_callback, NULL, ESP_INTR_FLAG_IRAM, NULL);
    timer_start(TIMER_GROUP_0, TIMER_0);

    // --- Superloop do Executivo Cíclico ---
    int minor_cycle_count = 0;
    while (1) {
        wait_for_interrupt();
        tarefa_t1(); tarefa_t2(); tarefa_t2(); tarefa_t3();
        wait_for_interrupt();
        tarefa_t1(); tarefa_t2(); tarefa_t2(); tarefa_t4();
        wait_for_interrupt();
        tarefa_t1(); esp_rom_delay_us(4500); tarefa_t2(); tarefa_t2();
        wait_for_interrupt();
        tarefa_t1(); tarefa_t3(); tarefa_t5(); tarefa_t5();
        wait_for_interrupt();
        tarefa_t1(); tarefa_t2(); tarefa_t2(); tarefa_t4();
        wait_for_interrupt();
    }
}
