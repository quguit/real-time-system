
### 1\. Análise da Teoria e Planejamento do Escalonamento

Você forneceu a base teórica correta. Vamos aplicar os dados do seu exemplo para construir a tabela de escalonamento.

**Dados das Tarefas:**

| Tarefa | Período (Pi) | Tempo de Computação (Ci) |
| :--- | :--- | :--- |
| T1 | 25 ms | 10 ms |
| T2 | 25 ms | 8 ms |
| T3 | 50 ms | 5 ms |
| T4 | 50 ms | 4 ms |
| T5 | 100 ms | 2 ms |

**Cálculos Fundamentais:**

1.  **Ciclo Menor (f):** É o "tick" do nosso sistema. Ele precisa ser um divisor comum de todos os períodos para que a condição `⌊ Pi / f ⌋ - Pi / f = 0` (ou seja, `Pi % f == 0`) seja satisfeita.

      * Máximo Divisor Comum (MDC) dos períodos (25, 50, 100) = **25 ms**.
      * Vamos definir nosso **Ciclo Menor (f) = 25 ms**.

2.  **Ciclo Maior (Hiperperíodo):** É o tempo total do ciclo de escalonamento, após o qual o padrão de execução se repete.

      * Mínimo Múltiplo Comum (MMC) dos períodos (25, 50, 100) = **100 ms**.

3.  **Estrutura do Escalonador:** O Ciclo Maior de 100 ms será dividido em `100ms / 25ms = 4` Ciclos Menores. Nosso `while(1)` (superloop) executará uma dessas 4 "fatias" de 25 ms a cada interrupção do timer.

**Montando a Tabela de Escalonamento (Schedule):**

Vamos usar o cronograma que você sugeriu e verificar sua viabilidade.

  * **Ciclo Menor 1 (0ms - 25ms):** Executar T1, T2, T3.
      * Tempo de CPU necessário: `C1 + C2 + C3 = 10 + 8 + 5 = 23 ms`.
      * Viabilidade: `23ms <= 25ms`. **OK.**
  * **Ciclo Menor 2 (25ms - 50ms):** Executar T1, T2, T4, T5.
      * Tempo de CPU necessário: `C1 + C2 + C4 + C5 = 10 + 8 + 4 + 2 = 24 ms`.
      * Viabilidade: `24ms <= 25ms`. **OK.**
  * **Ciclo Menor 3 (50ms - 75ms):** Executar T1, T2, T3.
      * Tempo de CPU necessário: `C1 + C2 + C3 = 10 + 8 + 5 = 23 ms`.
      * Viabilidade: `23ms <= 25ms`. **OK.**
  * **Ciclo Menor 4 (75ms - 100ms):** Executar T1, T2, T4.
      * Tempo de CPU necessário: `C1 + C2 + C4 = 10 + 8 + 4 = 22 ms`.
      * Viabilidade: `22ms <= 25ms`. **OK.**

O escalonamento proposto é **válido e viável**. Agora, vamos ao código.

### 2\. Análise Crítica dos Códigos Fornecidos

  * **`simplificado.c`**: Este código **não implementa um executivo cíclico**. Ele utiliza o FreeRTOS para criar uma tarefa (`timer_task`) que é desbloqueada por uma fila de eventos do timer. O executivo cíclico clássico evita o uso de um escalonador preemptivo (como o do FreeRTOS) para a sua lógica principal, preferindo um "superloop" controlado por uma única interrupção de timer. Além disso, a lógica de diminuir o intervalo do timer é o oposto do comportamento determinístico que buscamos.

  * **`completo.c`**: A arquitetura deste código está **muito mais próxima** do correto. Ele usa um superloop (`while(1)`), uma interrupção de timer para definir uma flag e uma função `wait_for_interrupt`. **Contudo, a implementação da lógica de escalonamento está incorreta e confusa.** A flag `long_time_interrupt_flag` e a mudança do período do alarme dentro da ISR violam o princípio de um Ciclo Menor fixo e previsível. O agendamento das tarefas no `while(1)` não segue a estrutura clara de 4 ciclos que planejamos.

### 3\. Implementação Didática e Corrigida

Abaixo está uma versão única e corrigida que implementa o executivo cíclico conforme o planejado. O código é extensivamente comentado para fins didáticos.

```c
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
```

### 4\. Cálculo do Uso da CPU por Ciclo Menor

A utilização da CPU (U) em um determinado ciclo é a razão entre o tempo total de computação naquele ciclo e a duração do ciclo.

**Fórmula:** `U = (Σ Ci) / f`

| Ciclo Menor | Tarefas Executadas | Tempo Total de Computação (Σ Ci) | Duração do Ciclo (f) | Uso da CPU |
| :--- | :--- | :--- | :--- | :--- |
| **1** | T1, T2, T3 | 10 + 8 + 5 = **23 ms** | 25 ms | 23 / 25 = **92.0%** |
| **2** | T1, T2, T4, T5 | 10 + 8 + 4 + 2 = **24 ms** | 25 ms | 24 / 25 = **96.0%** |
| **3** | T1, T2, T3 | 10 + 8 + 5 = **23 ms** | 25 ms | 23 / 25 = **92.0%** |
| **4** | T1, T2, T4 | 10 + 8 + 4 = **22 ms** | 25 ms | 22 / 25 = **88.0%** |

Como podemos ver, o uso da CPU é alto e varia a cada ciclo, mas o mais importante é que em nenhum momento ele ultrapassa 100%. Isso significa que o sistema é **escalonável** e os prazos (implícitos no final de cada ciclo menor) serão cumpridos.

### Resumo das Melhorias e Didática

1.  **Estrutura Clara**: O código está dividido em seções numeradas (Definições, ISR, Funções, `app_main`) para facilitar a compreensão.
2.  **Constantes Mnemônicas**: Em vez de "números mágicos", usamos `#define` para todos os valores importantes (`MINOR_CYCLE_MS`, `T1_COMPUTATION_US`, etc.), o que torna o código legível e fácil de modificar.
3.  **ISR Mínima**: A rotina de interrupção faz o mínimo necessário: sinaliza o loop principal. Isso é uma boa prática para manter as ISRs rápidas e eficientes.
4.  **Espera Eficiente**: A função `wait_for_interrupt` com `waiti` é a maneira correta e mais eficiente de sincronizar o loop principal com o timer, economizando energia.
5.  **Simulação Realista**: O uso de `esp_rom_delay_us` para simular o tempo de computação é uma excelente forma de testar e validar a lógica do escalonador sem ter o código real das tarefas.
6.  **Lógica de Escalonamento Correta**: O `switch-case` dentro do `while(1)` implementa a tabela de escalonamento de forma explícita e fácil de entender, que é o coração do executivo cíclico.

Este código final é  implementado  para rodar no ESP32 com o ESP-IDF.