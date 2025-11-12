# Projeto AutoIrrig - Sistema de Irrigação Inteligente com ESP32

## Visão Geral

O projeto **AutoIrrig** é um sistema de irrigação inteligente e automatizado desenvolvido para a plataforma ESP32, utilizando o framework Arduino e o ambiente de desenvolvimento PlatformIO. Ele foi projetado para monitorar as condições ambientais e do solo, controlando de forma autônoma ou manual duas bombas d'água para otimizar a irrigação de duas zonas distintas.

O sistema se integra com a nuvem através do protocolo MQTT (utilizando HiveMQ Cloud), permitindo monitoramento remoto e controle em tempo real.

## Funcionalidades Principais

*   **Monitoramento Ambiental:** Leitura de temperatura e umidade do ar através de um sensor DHT.
*   **Monitoramento de Umidade do Solo:** Leitura da porcentagem de umidade do solo para duas zonas independentes, utilizando dois sensores de umidade de solo.
*   **Controle de Bombas:** Acionamento e desacionamento independente de duas bombas d'água.
*   **Lógica de Irrigação Automatizada:** As bombas são controladas automaticamente com base em limiares configuráveis de umidade mínima e máxima do solo para cada zona.
*   **Controle Manual Remoto:** Capacidade de iniciar ou parar a irrigação manualmente para uma ou ambas as bombas, com duração configurável, via comandos MQTT.
*   **Comunicação MQTT Segura:** Conexão com um broker MQTT (HiveMQ Cloud) utilizando `WiFiClientSecure` para comunicação criptografada.
*   **Publicação de Telemetria:** Envio periódico de dados completos dos sensores, status das bombas e configurações do sistema para a nuvem.
*   **Publicação Rápida de Sensores:** Envio de atualizações rápidas dos dados dos sensores quando há mudanças significativas.
*   **Relatório de Status do Dispositivo:** Publicação regular do status do ESP32, incluindo tempo de atividade (uptime), intensidade do sinal WiFi (RSSI), uso de memória (heap) e contagem de mensagens MQTT.
*   **Last Will Testament (LWT):** Utilização de LWT para informar o status online/offline do dispositivo no broker MQTT.
*   **Configuração Remota:** Possibilidade de atualizar os limiares de umidade mínima e máxima do solo remotamente via comandos MQTT.
*   **Reconexão Automática:** Lógica robusta para reconexão automática ao WiFi e ao broker MQTT em caso de perda de conexão.
*   **Serialização de Dados:** Uso da biblioteca `ArduinoJson` para formatar os payloads das mensagens MQTT em JSON.

## Componentes de Hardware (Exemplo de Pinos)

*   **Sensores de Umidade do Solo:**
    *   Zona 1: GPIO 34
    *   Zona 2: GPIO 35
*   **Bombas d'Água:**
    *   Bomba 1: GPIO 26
    *   Bomba 2: GPIO 27
*   **Sensor DHT:** (Pino não especificado no `main.cpp`, mas configurado via `setupSensorDHT()`)

## Configuração

Todas as configurações principais do firmware estão centralizadas no início do arquivo `src/main.cpp`.

### Rede WiFi

Para conectar o ESP32 à sua rede, altere as seguintes constantes:

```cpp
const char* SSID = "SEU_WIFI_SSID";
const char* PASSWORD = "SUA_SENHA_WIFI";
```

### Broker MQTT

O projeto está pré-configurado para usar o HiveMQ Cloud, mas pode ser adaptado para qualquer broker MQTT.

```cpp
const char* MQTT_BROKER = "SEU_BROKER_URL";
const int MQTT_PORT = 8883; // Porta padrão para MQTT seguro
const char* MQTT_CLIENT_ID = "ESP32_Irrigacao_01";
const char* MQTT_USER = "SEU_USUARIO_MQTT";
const char* MQTT_PASS = "SUA_SENHA_MQTT";
```

### Tópicos MQTT

Os tópicos seguem uma estrutura hierárquica e podem ser ajustados conforme necessário:

```cpp
const char* TOPIC_TELEMETRIA = "irrigacao/telemetria";
const char* TOPIC_COMANDOS = "irrigacao/commands";
const char* TOPIC_BOMBAS = "irrigacao/pump/status";
const char* TOPIC_SENSORS = "irrigacao/sensor";
const char* TOPIC_STATUS = "irrigacao/device/status";
const char* TOPIC_LWT = "irrigacao/lwt"; // Last Will Testament
```

### Intervalos e Limiares

Você pode ajustar a frequência das leituras e publicações, bem como os limiares de sensibilidade para o envio de dados.

```cpp
// Intervalos em milissegundos
const unsigned long SENSOR_READ_INTERVAL = 1000;       // Leitura de sensores
const unsigned long TELEMETRIA_INTERVAL = 5000;        // Envio de telemetria completa
const unsigned long HEARTBEAT_INTERVAL = 30000;        // Envio de status do dispositivo

// Limiares de mudança para envio rápido de dados
const float TEMP_THRESHOLD = 0.5;
const float HUMIDITY_AIR_THRESHOLD = 2.0;
const int HUMIDITY_SOIL_THRESHOLD = 2;
```

## Estrutura do Projeto

*   **`platformio.ini`**: Arquivo de configuração do PlatformIO, definindo a plataforma (ESP32), framework (Arduino) e dependências de bibliotecas (DHT sensor library, Adafruit Unified Sensor, ArduinoJson, PubSubClient).
*   **`src/main.cpp`**: O código principal do firmware, contendo a lógica de inicialização, loop principal, funções de leitura de sensores, controle de bombas, comunicação WiFi/MQTT e tratamento de comandos.
*   **`lib/ControleBomba`**: Biblioteca para controle das bombas d'água.
    *   `controle_bomba.h`
    *   `controle_bomba.cpp`
*   **`lib/SensorDHT`**: Biblioteca para leitura do sensor DHT (temperatura e umidade do ar).
    *   `sensor_dht.h`
    *   `sensor_dht.cpp`
*   **`lib/SensorUmidadeSolo`**: Biblioteca para leitura dos sensores de umidade do solo.
    *   `sensor_umidade_solo.h`
    *   `sensor_umidade_solo.cpp`

## Como Funciona

1.  **Inicialização:** O ESP32 se conecta à rede WiFi configurada e, em seguida, ao broker MQTT da HiveMQ Cloud. Ele publica seu status "online" e subscreve-se ao tópico de comandos.
2.  **Leitura de Sensores:** Periodicamente, o sistema lê os dados do sensor DHT e dos dois sensores de umidade do solo.
3.  **Publicação de Dados:**
    *   Dados de sensores são publicados rapidamente se houver mudanças significativas.
    *   Uma telemetria completa (sensores, bombas, configuração) é publicada em intervalos maiores.
    *   O status do dispositivo (uptime, WiFi, memória) é publicado periodicamente.
4.  **Lógica de Irrigação:**
    *   Se a umidade do solo de uma zona cair abaixo do `min_umidade` configurado, a bomba correspondente é ligada.
    *   Se a umidade do solo atingir ou exceder o `max_umidade` configurado, a bomba é desligada.
    *   A irrigação manual tem precedência sobre a lógica automática.
5.  **Processamento de Comandos:** O sistema escuta o tópico `irrigacao/commands` para receber instruções, como:
    *   `config`: Atualizar os limiares `min_umidade` e `max_umidade`.
    *   `manual_irrigate`: Ligar uma ou ambas as bombas por um tempo determinado.
    *   `stop`: Desligar uma ou ambas as bombas.
    *   `ping`: Solicitar um relatório de status do sistema.
6.  **Gerenciamento de Conexão:** Em caso de desconexão WiFi ou MQTT, o sistema tenta se reconectar automaticamente para garantir a continuidade da operação.

Este projeto oferece uma solução robusta e flexível para automação de irrigação, ideal para aplicações em agricultura inteligente, jardinagem ou monitoramento de plantas.
