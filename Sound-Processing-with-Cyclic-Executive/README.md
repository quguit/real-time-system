Com certeza. Com base na sua nova especificação, preparei um `README.md` detalhado e estruturado em um passo a passo para guiar as duas etapas iniciais do desenvolvimento: a criação da base com o Executivo Cíclico e a implementação da coleta de áudio e detecção de atividade.

Este README pode ser usado diretamente no seu repositório do GitHub.

-----

# Executivo Cíclico para Processamento de Áudio no ESP32

## Objetivo do Projeto

Este projeto implementa um sistema de tempo real no ESP32 para análise de áudio. Utilizando a arquitetura de um **Executivo Cíclico**, o sistema captura áudio de um microfone I2S de forma contínua e realiza a detecção de atividade sonora (VAD - Voice Activity Detection).

A principal característica da arquitetura é o uso de buffers em esquema **ping-pong** gerenciados por um escalonador determinístico, garantindo que nenhuma amostra de áudio seja perdida e que o processamento ocorra em intervalos de tempo previsíveis.

### Conceitos Chave

  * **Executivo Cíclico:** Um escalonador simples e robusto que executa tarefas em intervalos de tempo fixos, definidos por um "Ciclo Menor". Ideal para sistemas que exigem alta previsibilidade.
  * **Ping-Pong Buffers:** Um esquema com dois buffers para desacoplar a aquisição de dados do processamento. Enquanto o hardware (via DMA) preenche um buffer ("ping"), o software processa o outro buffer ("pong"), que já está cheio. Ao final do ciclo, os papéis se invertem.
  * **I2S com DMA:** O periférico I2S do ESP32 utiliza Acesso Direto à Memória (DMA) para transferir o áudio do microfone para a RAM sem a intervenção da CPU, liberando o processador para outras tarefas.

### Hardware Necessário

  * 1x Placa de desenvolvimento ESP32
  * 1x Microfone I2S (ex: INMP441, SPH0645)
  * 1x LED de qualquer cor
  * 1x Resistor (ex: 220Ω)
  * Protoboard e jumpers

## Tarefas de Implementação

A seguir, apresento o passo a passo para a construção do sistema.

### **Tarefa 1: Configuração do Projeto e Conexões Físicas**

O primeiro passo é preparar o ambiente de desenvolvimento e conectar os componentes.

1.  **Criação do Projeto:** Inicie um novo projeto no ESP-IDF.
2.  **Conexões do Hardware:**
      * **Microfone I2S:**
          * `SCK` (Serial Clock) -\> `GPIO 14`
          * `WS` (Word Select) -\> `GPIO 15`
          * `SD` (Serial Data) -\> `GPIO 32`
          * `VCC` -\> `3.3V`
          * `GND` -\> `GND`
      * **LED:**
          * Anodo (perna maior) -\> Resistor -\> `GPIO 25`
          * Catodo (perna menor) -\> `GND`

*(Nota: Você pode alterar os pinos de GPIO no código se necessário.)*

### **Tarefa 2: Implementação da Estrutura do Executivo Cíclico**

Agora, vamos criar a base do nosso escalonador. O "tick" do nosso sistema (Ciclo Menor) será definido pelo tempo necessário para preencher um buffer de áudio.

1.  **Definição do Ciclo Menor:**

      * Taxa de amostragem: 16 kHz (16000 amostras/segundo)
      * Tamanho do buffer: 256 amostras
      * Tempo para preencher o buffer: `256 / 16000 = 0.016 segundos = 16 ms`.
      * Portanto, nosso **Ciclo Menor será de 16 ms**.

2.  **Código Base do Executivo:**
    Implementamos a estrutura já conhecida com um timer de hardware configurado para disparar uma interrupção a cada 16 ms. A ISR (Rotina de Serviço de Interrupção) apenas sinaliza o loop principal para iniciar um novo ciclo de processamento.

### **Tarefa 3: Configuração do I2S e dos Buffers Ping-Pong**

Nesta etapa, configuramos o periférico de áudio e declaramos os buffers que serão usados.

1.  **Inicialização do I2S:** Utilizamos as funções do driver do ESP-IDF para configurar o I2S no modo mestre, com comunicação via DMA e os pinos definidos na Tarefa 1.
2.  **Declaração dos Buffers:** Criamos duas áreas de memória (os buffers "ping" e "pong") para armazenar as amostras de áudio. Também criamos um ponteiro volátil que indicará qual dos dois buffers está pronto para ser processado.

### **Tarefa 4: Implementação da Detecção de Atividade Sonora (VAD)**

Esta é a lógica de processamento do áudio.

1.  **Cálculo da Potência:** Criamos uma função que recebe um buffer de áudio. Dentro dela, calculamos a energia do sinal somando o quadrado de cada amostra.
    `Potência ≈ Σ(amostra²)`
2.  **Lógica de Detecção:** Comparamos a potência calculada com um limiar de ruído (`NOISE_THRESHOLD`).
      * Se `Potência > NOISE_THRESHOLD`, significa que um som foi detectado, e acendemos o LED.
      * Caso contrário, mantemos o LED apagado.
3.  **Calibração:** O valor de `NOISE_THRESHOLD` é empírico. Ele deve ser ajustado observando os valores de potência medidos quando o ambiente está em silêncio.

### Código Fonte Completo

Abaixo está o código `main.c` que une todas as tarefas descritas.

```c
#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/timer.h"
#include "driver/i2s.h"
#include "driver/gpio.h"
#include "esp_rom_sys.h"

// =================================================================
// 1. DEFINIÇÕES DO SISTEMA
// =================================================================

// --- Configurações do Áudio ---
#define SAMPLE_RATE         16000
#define BUFFER_SAMPLES      256
#define I2S_PORT            I2S_NUM_0
#define I2S_SCK_PIN         14
#define I2S_WS_PIN          15
#define I2S_SD_PIN          32
#define LED_PIN             25

// --- Configurações do Executivo Cíclico ---
#define MINOR_CYCLE_MS      16 // 256 amostras / 16000 Hz = 16ms
#define MINOR_CYCLE_US      (MINOR_CYCLE_MS * 1000)
volatile int timer_interrupt_flag = 0;

// --- Buffers Ping-Pong ---
int16_t i2s_buffer_ping[BUFFER_SAMPLES];
int16_t i2s_buffer_pong[BUFFER_SAMPLES];
volatile int16_t* buffer_to_process = NULL;

// --- Detecção de Atividade ---
// ESTE VALOR DEVE SER AJUSTADO EMPIRICAMENTE!
#define NOISE_THRESHOLD     5000000 

// =================================================================
// 2. ESTRUTURA DO EXECUTIVO CÍCLICO
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
// 3. FUNÇÕES DE PROCESSAMENTO
// =================================================================

void process_audio(int16_t* audio_buffer) {
    long long power = 0;
    for (int i = 0; i < BUFFER_SAMPLES; i++) {
        // Acumula a energia do sinal (amostra ao quadrado)
        power += (long long)audio_buffer[i] * audio_buffer[i];
    }
    // A média da potência pode ser usada, mas para VAD a energia total é suficiente.
    // power /= BUFFER_SAMPLES;

    if (power > NOISE_THRESHOLD) {
        gpio_set_level(LED_PIN, 1); // Atividade detectada
    } else {
        gpio_set_level(LED_PIN, 0); // Silêncio
    }
}

// =================================================================
// 4. FUNÇÃO PRINCIPAL
// =================================================================
void app_main(void) {
    // --- Configuração do LED ---
    gpio_reset_pin(LED_PIN);
    gpio_set_direction(LED_PIN, GPIO_MODE_OUTPUT);

    // --- Configuração do Timer (Executivo Cíclico) ---
    timer_config_t timer_config = {
        .divider = 80, .counter_dir = TIMER_COUNT_UP, .counter_en = TIMER_PAUSE,
        .alarm_en = TIMER_ALARM_EN, .auto_reload = true,
    };
    timer_init(TIMER_GROUP_0, TIMER_0, &timer_config);
    timer_set_counter_value(TIMER_GROUP_0, TIMER_0, 0);
    timer_set_alarm_value(TIMER_GROUP_0, TIMER_0, MINOR_CYCLE_US);
    timer_enable_intr(TIMER_GROUP_0, TIMER_0);
    timer_isr_register(TIMER_GROUP_0, TIMER_0, timer_isr_callback, NULL, ESP_INTR_FLAG_IRAM, NULL);
    timer_start(TIMER_GROUP_0, TIMER_0);
    
    // --- Configuração do I2S ---
    i2s_config_t i2s_config = {
        .mode = I2S_MODE_MASTER | I2S_MODE_RX,
        .sample_rate = SAMPLE_RATE,
        .bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT,
        .channel_format = I2S_CHANNEL_FMT_ONLY_LEFT,
        .communication_format = I2S_COMM_FORMAT_STAND_I2S,
        .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
        .dma_buf_count = 2, // 2 buffers para o DMA -> Ping e Pong
        .dma_buf_len = BUFFER_SAMPLES,
        .use_apll = false
    };
    i2s_pin_config_t pin_config = {
        .bck_io_num = I2S_SCK_PIN,
        .ws_io_num = I2S_WS_PIN,
        .data_in_num = I2S_SD_PIN,
        .data_out_num = I2S_PIN_NO_CHANGE
    };
    i2s_driver_install(I2S_PORT, &i2s_config, 0, NULL);
    i2s_set_pin(I2S_PORT, &pin_config);

    size_t bytes_read = 0;
    int16_t* current_buffer = i2s_buffer_ping; // Começamos preenchendo o buffer ping

    // --- Superloop do Executivo Cíclico ---
    while (1) {
        // O loop principal está livre, mas o DMA está preenchendo um buffer em background.
        // Vamos ler o buffer que o DMA acabou de preencher. Esta chamada é bloqueante.
        i2s_read(I2S_PORT, current_buffer, sizeof(int16_t) * BUFFER_SAMPLES, &bytes_read, portMAX_DELAY);
        
        // Sinaliza qual buffer deve ser processado
        buffer_to_process = current_buffer;

        // Troca para o próximo buffer para a próxima leitura do DMA (lógica ping-pong)
        if (current_buffer == i2s_buffer_ping) {
            current_buffer = i2s_buffer_pong;
        } else {
            current_buffer = i2s_buffer_ping;
        }

        // Sincroniza com o "tick" de 16ms do nosso sistema
        wait_for_interrupt();

        // No início do ciclo, verificamos se há um buffer pronto
        if (buffer_to_process != NULL) {
            process_audio((int16_t*)buffer_to_process);
            buffer_to_process = NULL; // Limpa a flag
        }
    }
}
```