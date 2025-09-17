#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/timer.h"
#include "driver/i2s.h"
#include "driver/gpio.h"
#include "esp_rom_sys.h"


// 1. DEFINIÇÕES E VARIÁVEIS GLOBAIS

// --- Configurações do Áudio e Hardware ---
#define SAMPLE_RATE         16000
#define BUFFER_SAMPLES      256
#define I2S_PORT            I2S_NUM_0
#define I2S_SCK_PIN         14
#define I2S_WS_PIN          27
#define I2S_SD_PIN          32
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
#define NOISE_THRESHOLD     5000000 // Valor limiar

// =================================================================
// 2. FUNÇÕES DO EXECUTIVO CÍCLICO
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

/*
    3. PROCESSAMENTO DE ÁUDIO (TAREFA 2) - OBJETIVO 2
    receber um buffer de áudio e decidir se há som ou não. Para isso, eu percorro todas as
    amostras, calculo a energia do sinal (somando o quadrado de cada uma) e, no final, comparo
    essa energia total com o meu limiar de ruído para acender ou apagar o LED.
*/
void process_audio_for_vad(int16_t* audio_buffer) {
    long long power = 0;
    for (int i = 0; i < BUFFER_SAMPLES; i++) {
        power += (long long)audio_buffer[i] * audio_buffer[i];
    }
    
    // fazer um vetor de power onde a cada nova medida ele subistiu pela mais antiga, em um mecanismo circular, tamanho
    // do vetor pode ser parametrizado.
    printf("Potencia do Sinal: %lld\n", power);
    if (power > NOISE_THRESHOLD) {
        gpio_set_level(LED_PIN, 1);
    } else {
        gpio_set_level(LED_PIN, 0);
    }
}

// 4. FUNÇÕES DE CONFIGURAÇÃO (SETUP)

/*
    CONFIGURAÇÃO - definir o GPIO 2 (o LED da placa) como uma saída digital.
*/
void setup_gpio() {
    gpio_reset_pin(LED_PIN);
    gpio_set_direction(LED_PIN, GPIO_MODE_OUTPUT);
}

/*
    CONFIGURAÇÃO - I2S para taxa de 16kHz:
    Nesta estrutura eu defino a taxa de amostragem (16kHz), os bits por amostra e,
    crucialmente, informo que usarei 2 buffers de DMA para o esquema ping-pong. 
    Associo os pinos físicos (14, 15, 32) a essa configuração
    Inicio o driver.
*/
void setup_i2s_microphone() {
    i2s_config_t i2s_config = {
        .mode = I2S_MODE_MASTER | I2S_MODE_RX,
        .sample_rate = SAMPLE_RATE,
        .bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT,
        .channel_format = I2S_CHANNEL_FMT_ONLY_LEFT,
        .communication_format = I2S_COMM_FORMAT_STAND_I2S,
        .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
        .dma_buf_count = 2,
        .dma_buf_len = BUFFER_SAMPLES,
        .use_apll = false
    };
    i2s_pin_config_t pin_config = {
        .bck_io_num = I2S_SCK_PIN, .ws_io_num = I2S_WS_PIN,
        .data_in_num = I2S_SD_PIN, .data_out_num = I2S_PIN_NO_CHANGE
    };
    i2s_driver_install(I2S_PORT, &i2s_config, 0, NULL);
    i2s_set_pin(I2S_PORT, &pin_config);
}

/*
    CONFIGURAÇÃO - Execultivo Cíclico
    Configuro um timer de hardware para gerar uma interrupção a cada 16ms. 
    Associo a função 'timer_isr_callback' a essa interrupção e inicio o timer. 
    A partir daqui, o sistema terá um "pulso de relógio" constante e previsível.
*/
void setup_cyclic_executive_timer() {
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
}

//  LOOP PRINCIPAL

/*
    EXECUÇÃO - LOOP que faz a troca de buffers (ping-pong) e a sincronização
    com o executivo cíclico.
*/
void execute_audio_processing_loop() {
    size_t bytes_read = 0;
    int16_t* current_buffer_to_fill = i2s_buffer_ping;

    while (1) {
        // --- TAREFA 1: COLETA ---
        // Aguarda o (`i2s_read`) enquanto o hardware do DMA preenche um buffer
        i2s_read(I2S_PORT, current_buffer_to_fill, sizeof(int16_t) * BUFFER_SAMPLES, &bytes_read, portMAX_DELAY);
        
        // o buffer cheio, é marcado como 'pronto para processar'.
        buffer_to_process = current_buffer_to_fill;

        // Aqui eu aponto para o outro buffer, instruindo
        // o DMA a preenchê-lo na próxima vez, enquanto eu processo o que acabou de chegar.
        if (current_buffer_to_fill == i2s_buffer_ping) {
            current_buffer_to_fill = i2s_buffer_pong;
        } else {
            current_buffer_to_fill = i2s_buffer_ping;
        }

        // --- SINCRONIZAÇÃO DO EXECUTIVO CÍCLICO ---
        //  espera pelo 'tick' de 16ms do meu executivo cíclico.
        wait_for_interrupt();

        // --- TAREFA 2: PROCESSAMENTO (REALIZAÇÃO) ---
        // No início de cada ciclo, é verificado se á algum  buffermarcado como pronto.
        // Se sim, eu chamo a minha função de processamento para analisar o áudio e controlar o LED.
        if (buffer_to_process != NULL) {
            process_audio_for_vad((int16_t*)buffer_to_process);
            buffer_to_process = NULL;
        }
    }
}

// =================================================================
// 6. FUNÇÃO APP_MAIN
// =================================================================

void app_main(void) {
    // Aqui é o ponto de partida
    // Minha primeiro chamo todas as minhas funções de configuração para inicializar
    // o hardware (LED, microfone) e o software (timer do executivo cíclico).
    setup_gpio();
    setup_i2s_microphone();
    setup_cyclic_executive_timer();

    // Depois que tudo está configurado, eu entro no loop principal de execução,
    execute_audio_processing_loop();
}