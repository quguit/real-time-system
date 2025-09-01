#include <stdio.h>
#include "freertos/FreeRTOS.h"   // Apenas para usar delay e printf, não para o escalonador
#include "freertos/task.h"       // Apenas para usar delay e printf
#include "driver/timer.h"        // API do driver de Timer do ESP-IDF
#include "esp_rom_sys.h"         // Para esp_rom_delay_us, que simula trabalho da CPU

// =================================================================
// 1. DEFINIÇÕES DO NOSSO SISTEMA
// =================================================================

// --- Definições do Timer ---
#define TIMER_DIVIDER         80      // Clock de 80MHz / 80 = 1MHz. Cada tick do timer vale 1 microssegundo (us).
#define TIMER_SCALE           (TIMER_BASE_CLK / TIMER_DIVIDER) // Não usado diretamente, mas bom para referência
#define MINOR_CYCLE_MS        25      // Duração do nosso Ciclo Menor em milissegundos.
#define MINOR_CYCLE_US        (MINOR_CYCLE_MS * 1000) // Ciclo Menor em microssegundos para o timer.

// --- Tempos de Computação (Ci) das Tarefas em microssegundos (us) ---
// Usaremos estes valores para simular o tempo que cada tarefa leva para executar.
#define T1_COMPUTATION_US 10000 // 10 ms
#define T2_COMPUTATION_US 8000  // 8 ms
#define T3_COMPUTATION_US 5000  // 5 ms
#define T4_COMPUTATION_US 4000  // 4 ms
#define T5_COMPUTATION_US 2000  // 2 ms

// Flag global para sinalizar a ocorrência da interrupção do timer.
// 'volatile' é crucial para garantir que o compilador não otimize o acesso
// a esta variável, já que ela é modificada em uma ISR e lida no loop principal.
volatile int timer_interrupt_flag = 0;

// =================================================================
// 2. ROTINA DE SERVIÇO DE INTERRUPÇÃO (ISR) DO TIMER
// =================================================================

// Esta função é chamada automaticamente a cada 25ms pelo hardware do timer.
// O atributo IRAM_ATTR garante que o código da ISR seja colocado na RAM interna,
// tornando sua execução mais rápida e previsível.
static void IRAM_ATTR timer_isr_callback(void *arg) {
    // Limpa o status da interrupção para que ela possa ser acionada novamente.
    timer_group_clr_intr_status_in_isr(TIMER_GROUP_0, TIMER_0);
    
    // O timer está configurado com 'auto_reload = true', então ele já reiniciou
    // a contagem para o próximo alarme de 25ms. Não precisamos reconfigurá-lo aqui.
    timer_group_enable_alarm_in_isr(TIMER_GROUP_0, TIMER_0);

    // Sinaliza para o superloop no app_main que o próximo ciclo menor pode começar.
    timer_interrupt_flag = 1;
}

// =================================================================
// 3. FUNÇÃO DE ESPERA SÍNCRONA
// =================================================================

// Esta função coloca a CPU em modo de baixo consumo até que a próxima
// interrupção (a do nosso timer) ocorra. É muito mais eficiente do que um
// loop vazio (while(timer_interrupt_flag == 0);).
void wait_for_interrupt() {
    while (timer_interrupt_flag == 0) {
        // Instrução Assembly 'wait for interrupt'. Pausa a CPU até uma interrupção.
        __asm__ __volatile__("waiti 0");
    }
    // Uma vez que a interrupção ocorreu e a flag foi setada,
    // limpamos a flag para estarmos prontos para o próximo ciclo.
    timer_interrupt_flag = 0;
}

// =================================================================
// 4. IMPLEMENTAÇÃO DAS TAREFAS
// =================================================================

// Cada função simula uma tarefa. Ela imprime uma mensagem para indicar
// que está executando e depois chama esp_rom_delay_us para consumir
// um tempo de CPU equivalente ao seu Tempo de Computação (Ci).

void tarefa_t1() { 
    printf("Executando T1 (10ms)... "); 
    esp_rom_delay_us(T1_COMPUTATION_US); 
    printf("OK\n"); 
}
void tarefa_t2() { 
    printf("Executando T2 (8ms)... ");  
    esp_rom_delay_us(T2_COMPUTATION_US);  
    printf("OK\n"); 
}
void tarefa_t3() { 
    printf("Executando T3 (5ms)... ");  
    esp_rom_delay_us(T3_COMPUTATION_US);  
    printf("OK\n"); 
}
void tarefa_t4() { 
    printf("Executando T4 (4ms)... ");  
    esp_rom_delay_us(T4_COMPUTATION_US);  
    printf("OK\n"); 
}
void tarefa_t5() { 
    printf("Executando T5 (2ms)... ");  
    esp_rom_delay_us(T5_COMPUTATION_US);  
    printf("OK\n"); 
}

// =================================================================
// 5. FUNÇÃO PRINCIPAL (app_main)
// =================================================================

void app_main(void) {
    printf("Iniciando Executivo Ciclico.\n");
    printf("Ciclo Menor (f) = %d ms\n", MINOR_CYCLE_MS);
    printf("Hiperperiodo (Ciclo Maior) = 100 ms (4 ciclos menores)\n\n");
    
    // --- Configuração do Timer de Hardware ---
    timer_config_t config = {
        .divider = TIMER_DIVIDER,      // Define o prescaler para 80, resultando em ticks de 1us.
        .counter_dir = TIMER_COUNT_UP, // O contador irá incrementar.
        .counter_en = TIMER_PAUSE,     // Não iniciar o timer ainda.
        .alarm_en = TIMER_ALARM_EN,    // Habilitar o evento de alarme.
        .auto_reload = true,           // Recarregar o contador automaticamente após o alarme. Essencial para nosso ciclo.
    };
    timer_init(TIMER_GROUP_0, TIMER_0, &config);

    // Define o valor inicial do contador como 0.
    timer_set_counter_value(TIMER_GROUP_0, TIMER_0, 0);

    // Define o valor do alarme. A ISR será chamada quando o contador atingir este valor.
    timer_set_alarm_value(TIMER_GROUP_0, TIMER_0, MINOR_CYCLE_US);
    
    // Habilita a interrupção no nível do timer.
    timer_enable_intr(TIMER_GROUP_0, TIMER_0);
    
    // Registra nossa função de callback (ISR) para a interrupção do timer.
    timer_isr_register(TIMER_GROUP_0, TIMER_0, timer_isr_callback, NULL, ESP_INTR_FLAG_IRAM, NULL);

    // Inicia o timer. A contagem começa agora!
    timer_start(TIMER_GROUP_0, TIMER_0);

    // --- Superloop do Executivo Cíclico ---
    int minor_cycle_count = 0;
    while (1) {
        // 1. Sincroniza com o início do próximo Ciclo Menor. A CPU fica em idle aqui.
        wait_for_interrupt();
        
        printf("\n--- INICIO DO CICLO MENOR %d ---\n", minor_cycle_count + 1);

        // 2. Executa o conjunto de tarefas definido para o ciclo menor atual.
        //    Esta é a tabela de escalonamento (schedule).
        switch (minor_cycle_count) {
            case 0: // Frame 1 (0-25ms)
                tarefa_t1();
                tarefa_t2();
                tarefa_t3();
                break;
            case 1: // Frame 2 (25-50ms)
                tarefa_t1();
                tarefa_t2();
                tarefa_t4();
                tarefa_t5();
                break;
            case 2: // Frame 3 (50-75ms)
                tarefa_t1();
                tarefa_t2();
                tarefa_t3();
                break;
            case 3: // Frame 4 (75-100ms)
                tarefa_t1();
                tarefa_t2();
                tarefa_t4();
                break;
        }

        // 3. Avança para o próximo ciclo menor, reiniciando após o 4º ciclo.
        minor_cycle_count = (minor_cycle_count + 1) % 4;
    }
}