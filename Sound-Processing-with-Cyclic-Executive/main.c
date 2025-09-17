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

// --- Configuração do Hardware ---
// Alterado para o LED onboard do ESP32 de 38 pinos
#define LED_PIN             2 

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
// Comece com um valor alto e diminua até o LED acender com sons, mas não com o silêncio.
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
// 3. FUNÇÕES DE PROCESSAMENTO (TAREFA 2)
// =================================================================

void process_audio_for_vad(int16_t* audio_buffer) {
    long long power = 0;
    for (int i = 0; i < BUFFER_SAMPLES; i++) {
        // Acumula a energia do sinal (amostra ao quadrado)
        power += (long long)audio_buffer[i] * audio_buffer[i];
    }

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
    
    // --- Configuração do I2S (TAREFA 1 - Coleta de Áudio) ---
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
    int16_t* current_buffer_to_fill = i2s_buffer_ping; // DMA começa a preencher o buffer "ping"

    // --- Superloop do Executivo Cíclico ---
    while (1) {
        // Esta chamada é bloqueante. O DMA preenche o buffer em background.
        // O código fica aqui até o DMA terminar de preencher um buffer completo.
        i2s_read(I2S_PORT, current_buffer_to_fill, sizeof(int16_t) * BUFFER_SAMPLES, &bytes_read, portMAX_DELAY);
        
        // Assim que i2s_read retorna, o buffer está cheio.
        // Sinalizamos que este buffer está pronto para o processamento.
        buffer_to_process = current_buffer_to_fill;

        // Trocamos o ponteiro para que o DMA preencha o outro buffer na próxima iteração (lógica ping-pong)
        if (current_buffer_to_fill == i2s_buffer_ping) {
            current_buffer_to_fill = i2s_buffer_pong;
        } else {
            current_buffer_to_fill = i2s_buffer_ping;
        }

        // Sincroniza com o "tick" de 16ms do nosso sistema.
        // Isso garante que o processamento só aconteça em intervalos regulares.
        wait_for_interrupt();

        // No início de cada ciclo menor, verificamos se há um buffer para processar.
        if (buffer_to_process != NULL) {
            // Executamos a TAREFA 2 - Detecção de Atividade Sonora
            process_audio_for_vad((int16_t*)buffer_to_process);
            buffer_to_process = NULL; // Limpa a "flag" para o próximo ciclo
        }
    }
}