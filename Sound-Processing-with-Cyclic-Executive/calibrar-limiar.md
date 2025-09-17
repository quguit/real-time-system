
### Guia Prático: Como Ligar e Calibrar o Microfone

#### **Passo 1: Conexão do Hardware**

Conecte seu microfone I2S (como o INMP441) ao seu ESP32 de 38 pinos.

| Pino do Microfone I2S | Conectar ao Pino do ESP32 | Descrição |
| :--- | :--- | :--- |
| **VCC** (ou VDD) | `3V3` | Alimentação de 3.3 Volts |
| **GND** | `GND` | Terra (Ground) |
| **SCK** (ou BCLK) | `GPIO 14` | Serial Clock |
| **WS** (ou LRCLK) | `GPIO 15` | Word Select |
| **SD** (ou DOUT) | `GPIO 32` | Serial Data |

*O LED usado será o da placa, conectado ao GPIO 2.*

#### **Passo 2: Medindo o Limiar de Ruído (`NOISE_THRESHOLD`)**

Este é o passo mais importante para que o seu detector funcione bem.

1.  **Modifique o Código para Depuração:**
    Adicione uma linha de `printf` na função `process_audio_for_vad` para que você possa ver a potência do sinal em tempo real.

    ```c
    void process_audio_for_vad(int16_t* audio_buffer) {
        long long power = 0;
        for (int i = 0; i < BUFFER_SAMPLES; i++) {
            power += (long long)audio_buffer[i] * audio_buffer[i];
        }

        // Adicione esta linha para ver os valores no monitor serial
        printf("Potencia do Sinal: %lld\n", power);

        if (power > NOISE_THRESHOLD) {
            gpio_set_level(LED_PIN, 1);
        } else {
            gpio_set_level(LED_PIN, 0);
        }
    }
    ```

2.  **Compile e Grave:** Envie este código modificado para o seu ESP32.

3.  **Abra o Monitor Serial:** No VS Code, abra o monitor serial para ver a saída do `printf`.

4.  **Meça o Ruído de Fundo:**

      * Coloque o microfone em seu ambiente e **fique em silêncio por alguns segundos**.
      * Observe os valores de "Potencia do Sinal" que aparecem. Eles devem ser relativamente baixos e estáveis. Anote a média desses valores. **Exemplo: `2.000.000`**.

5.  **Meça o Sinal da Voz:**

      * **Fale normalmente** a uma distância razoável do microfone.
      * Observe os novos valores de potência. Eles devem ser significativamente mais altos. **Exemplo: `25.000.000`**.

6.  **Escolha o Limiar:**

      * Escolha um valor para o `NOISE_THRESHOLD` que fique confortavelmente entre o valor do silêncio e o valor da sua voz.
      * Usando nosso exemplo, um bom valor seria `10000000`.

#### **Passo 3: Testando o Sistema Final**

1.  **Remova o `printf`:** Volte ao código e **apague ou comente** a linha `printf("Potencia do Sinal: %lld\n", power);`. Isso garante que o sistema rode com performance máxima.

2.  **Atualize o Limiar:** Mude o valor de `#define NOISE_THRESHOLD` para o número que você calibrou.

    ```c
    #define NOISE_THRESHOLD 10000000 // Use o seu valor aqui!
    ```

3.  **Compile e Grave Novamente:** Envie a versão final e limpa do código para o ESP32.

Agora, seu sistema está pronto\! O LED azul da placa deve permanecer apagado em silêncio e acender claramente assim que você começar a falar.