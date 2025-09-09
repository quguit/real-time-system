
# Executivo Cíclico para ESP32

Olá! Este é o meu projeto para a implementação de um **Executivo Cíclico** no microcontrolador ESP32, utilizando o framework ESP-IDF. O objetivo principal foi criar um escalonador de tarefas determinístico e previsível, ideal para sistemas de tempo real onde o cumprimento de prazos é crítico.

Neste repositório, apresento todo o processo de análise, projeto e implementação do escalonador, desde a base teórica até o código final e funcional.

## Tarefas do Projeto

Dividi o desenvolvimento deste projeto nas seguintes tarefas:

### **Tarefa 1: Análise da Teoria e Planejamento do Escalonamento**

A primeira etapa foi consolidar a base teórica e aplicá-la ao conjunto de tarefas proposto. Para isso, segui os seguintes passos:

1.  **Definição das Tarefas:**
    Parti de um conjunto de 5 tarefas com períodos (`Pi`) e tempos de computação (`Ci`) pré-definidos:

| Tarefa | Período (Pi) | Tempo de Computação (Ci) |
| :--- | :--- | :--- |
| T1 | 25 ms | 10 ms |
| T2 | 25 ms | 8 ms |
| T3 | 50 ms | 5 ms |
| T4 | 50 ms | 4 ms |
| T5 | 100 ms | 2 ms |

2.  **Cálculos dos Ciclos do Sistema:**
    Com base na teoria de executivos cíclicos, calculei os parâmetros fundamentais para o nosso escalonador:
    * **Ciclo Menor (f):** Utilizei o Máximo Divisor Comum (MDC) dos períodos, resultando em **25 ms**. Este valor define a frequência do "tick" do nosso sistema, garantindo que todos os períodos são múltiplos inteiros dele.
    * **Ciclo Maior (Hiperperíodo):** Calculei o Mínimo Múltiplo Comum (MMC) dos períodos, chegando a **100 ms**. Este é o tempo total após o qual o padrão completo de execução das tarefas se repetirá.

3.  **Montagem e Validação da Tabela de Escalonamento (Schedule):**
    Com um hiperperíodo de 100 ms e um ciclo menor de 25 ms, o escalonamento foi dividido em 4 frames (ciclos menores). Para cada frame, determinei quais tarefas deveriam ser executadas e verifiquei a viabilidade, garantindo que a soma dos tempos de computação nunca excedesse os 25 ms disponíveis.

| Ciclo Menor | Tarefas Executadas | Tempo de CPU Necessário | Viabilidade |
| :--- | :--- | :--- | :--- |
| **1 (0-25ms)** | T1, T2, T3 | 23 ms | **OK** (`23ms <= 25ms`) |
| **2 (25-50ms)** | T1, T2, T4, T5 | 24 ms | **OK** (`24ms <= 25ms`) |
| **3 (50-75ms)** | T1, T2, T3 | 23 ms | **OK** (`23ms <= 25ms`) |
| **4 (75-100ms)** | T1, T2, T4 | 22 ms | **OK** (`22ms <= 25ms`) |

Com o escalonamento validado, o sistema se provou **escalonável e viável**.

### **Tarefa 2: Análise Crítica de Implementações Iniciais**

Antes de chegar à versão final, analisei duas abordagens de código preliminares (`simplificado.c` e `completo.c`).

* A primeira, baseada em tarefas e filas do FreeRTOS, foi descartada por não seguir o paradigma de um executivo cíclico clássico (que evita um escalonador preemptivo como o do FreeRTOS para a sua lógica principal).
* A segunda estava mais próxima, usando um "superloop" e uma interrupção de timer, mas sua lógica de escalonamento era falha e não determinística.

Esta análise foi crucial para definir a arquitetura correta para a implementação final.

### **Tarefa 3: Implementação Final e Corrigida**

Com base no planejamento e nas lições aprendidas, desenvolvi o código final contido neste repositório. A implementação segue as melhores práticas para um executivo cíclico:

1.  **Estrutura Clara:** O código foi organizado em seções lógicas: definições, ISR, função de espera síncrona, implementação das tarefas e a função principal (`app_main`).
2.  **Simulação Realista:** Utilizei `esp_rom_delay_us()` para simular com precisão o tempo de computação (Ci) de cada tarefa, permitindo uma validação realista do escalonamento.
3.  **Eficiência Energética:** Implementei a função `wait_for_interrupt()` com a instrução assembly `waiti 0`, que coloca o processador em estado de baixo consumo enquanto aguarda a próxima interrupção do timer, em vez de desperdiçar ciclos com busy-waiting.
4.  **Lógica de Escalonamento Explícita:** O coração do sistema, o "superloop" dentro do `app_main`, utiliza uma estrutura `switch-case` que implementa de forma clara e direta a tabela de escalonamento validada na Tarefa 1.
5.  **Determinismo:** A interrupção do timer é configurada para recarregar automaticamente (`auto_reload = true`), garantindo um "tick" preciso e constante de 25 ms, que é a base para o comportamento determinístico de todo o sistema.
