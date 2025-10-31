#include <Arduino.h>
#include <WiFi.h>
#include <PubSubClient.h> // Biblioteca MQTT
#include <WiFiClientSecure.h> // Adicionado para TLS
#include <ArduinoJson.h>

#include "sensor_dht.h"
#include "sensor_umidade_solo.h"
#include "controle_bomba.h"

// --- Configurações de Rede ---
const char* SSID = "guilherme_2G";
const char* PASSWORD = "789453216a";
const char* MQTT_BROKER = "d98fa3b64a654694be6a0cc7c5fa1799.s1.eu.hivemq.cloud"; // Exemplo: substitua pelo seu broker
const int MQTT_PORT = 8883; // Porta padrão para MQTT não seguro
const char* MQTT_CLIENT_ID = "ESP32_Irrigacao_01"; // Identificador único
const char* MQTT_USER = "hivemq.webclient.1761853881390"; // Usuário (se necessário)
const char* MQTT_PASS = "7G4#QF*ud&fey9S3:oYL"; // Senha (se necessário)

// --- Tópicos MQTT ---
const char* TOPIC_TELEMETRIA = "irrigacao/telemetria";
const char* TOPIC_COMANDOS = "irrigacao/commands";
const char* TOPIC_BOMBAS = "irrigacao/bombas";

// --- Intervalos Otimizados ---
const unsigned long TELEMETRIA_INTERVAL = 10000; // 10s
const unsigned long COMANDO_INTERVAL = 3000;     // 3s
const unsigned long LOGICA_INTERVAL = 2000;      // 2s

// --- Controle de Estado ---
struct SystemState {
    int min_umidade[2] = {30, 30};
    int max_umidade[2] = {60, 60};
    bool bomba_ligada[2] = {false, false};
    bool manual_irrigation_active[2] = {false, false};
    unsigned long manual_irrigation_start_time[2] = {0, 0};
    
    // Cache de telemetria
    float last_temperatura = -999;
    float last_umidadeAr = -999;
    int last_umidadeSolo[2] = {-1, -1};
    
    // Cache de status das bombas
    bool last_bomba_ligada[2] = {false, false};
    
    // Timestamp do último comando processado
    String last_command_id = "";
};

SystemState state;

const unsigned long MANUAL_IRRIGATION_DURATION = 3000;

// --- Mapeamento de Pinos ---
const int PINO_UMIDADE_SOLO_1 = 34;
const int PINO_UMIDADE_SOLO_2 = 35;
const int PINO_BOMBA_1 = 26;
const int PINO_BOMBA_2 = 27;

// --- Cliente MQTT ---
WiFiClientSecure espClient;
PubSubClient mqttClient(espClient);

// --- Protótipos ---
void setup_wifi();
void setup_mqtt();
void reconnect_mqtt();
bool enviar_telemetria_otimizada();
void mqtt_callback(char* topic, byte* payload, unsigned int length);
void verificar_comandos_otimizado();
void handle_manual_irrigation();
void logica_irrigacao();
bool dados_telemetria_mudaram();
bool enviar_status_bomba_rapido(int bomba_index);

void setup() {
    Serial.begin(115200);
    setupSensorDHT();
    setupSensorUmidadeSolo(PINO_UMIDADE_SOLO_1);
    setupSensorUmidadeSolo(PINO_UMIDADE_SOLO_2);
    setupBombas(PINO_BOMBA_1, PINO_BOMBA_2);
    setup_wifi();
    setup_mqtt();
}

void loop() {
    static unsigned long last_telemetry_time = 0;
    static unsigned long last_command_time = 0;
    static unsigned long last_logic_time = 0;

    unsigned long now = millis();

    // Mantém conexão MQTT
    if (!mqttClient.connected()) {
        reconnect_mqtt();
    }
    mqttClient.loop(); // Processa mensagens MQTT recebidas

    // Envia telemetria se houver mudanças
    if (now - last_telemetry_time > TELEMETRIA_INTERVAL) {
        last_telemetry_time = now;
        if (WiFi.status() == WL_CONNECTED && dados_telemetria_mudaram()) {
            enviar_telemetria_otimizada();
        }
    }

    // Verifica comandos (mantido para compatibilidade, mas agora usa MQTT callback)
    if (now - last_command_time > COMANDO_INTERVAL) {
        last_command_time = now;
        if (WiFi.status() != WL_CONNECTED) {
            Serial.println("WiFi desconectado. Tentando reconectar...");
            setup_wifi();
        }
    }

    // Lógica local
    if (now - last_logic_time > LOGICA_INTERVAL) {
        last_logic_time = now;
        handle_manual_irrigation();
        logica_irrigacao();
    }
}

// --- Configuração WiFi ---
void setup_wifi() {
    delay(10);
    Serial.println("\nConectando WiFi...");
    WiFi.begin(SSID, PASSWORD);
    
    int tentativas = 0;
    while (WiFi.status() != WL_CONNECTED && tentativas < 20) {
        delay(500);
        Serial.print(".");
        tentativas++;
    }
    
    if (WiFi.status() == WL_CONNECTED) {
        Serial.println("\nWiFi OK!");
        Serial.println(WiFi.localIP());
    } else {
        Serial.println("\nFalha WiFi!");
    }
}

// --- Configuração MQTT ---
void setup_mqtt() {
    espClient.setInsecure(); // Para teste (remova em produção)
    mqttClient.setServer(MQTT_BROKER, MQTT_PORT);
    mqttClient.setCallback(mqtt_callback);
    reconnect_mqtt();
}

// --- Reconexão MQTT ---
void reconnect_mqtt() {
    while (!mqttClient.connected()) {
        Serial.println("Conectando ao MQTT Broker...");
        Serial.printf("Tentando conectar a %s:%d com Client ID: %s\n", MQTT_BROKER, MQTT_PORT, MQTT_CLIENT_ID);
        if (mqttClient.connect(MQTT_CLIENT_ID, MQTT_USER, MQTT_PASS)) {
            Serial.println("MQTT Conectado!");
            mqttClient.subscribe(TOPIC_COMANDOS);
        } else {
            Serial.print("Falha, rc=");
            Serial.print(mqttClient.state());
            Serial.println(" Tentando novamente em 5s...");
            delay(5000);
        }
    }
}

// --- Verifica se dados mudaram significativamente ---
bool dados_telemetria_mudaram() {
    float temp = getTemperature();
    float umid_ar = getAirHumidity();
    int umid_solo1 = lerUmidadePercentual(PINO_UMIDADE_SOLO_1);
    int umid_solo2 = lerUmidadePercentual(PINO_UMIDADE_SOLO_2);
    
    bool mudou_sensores = (abs(temp - state.last_temperatura) > 1.0) ||
                          (abs(umid_ar - state.last_umidadeAr) > 1.0) ||
                          (abs(umid_solo1 - state.last_umidadeSolo[0]) > 2) ||
                          (abs(umid_solo2 - state.last_umidadeSolo[1]) > 2);
    
    bool mudou_bombas = (state.bomba_ligada[0] != state.last_bomba_ligada[0]) ||
                        (state.bomba_ligada[1] != state.last_bomba_ligada[1]);
    
    bool mudou = mudou_sensores || mudou_bombas;
    
    if (mudou) {
        state.last_temperatura = temp;
        state.last_umidadeAr = umid_ar;
        state.last_umidadeSolo[0] = umid_solo1;
        state.last_umidadeSolo[1] = umid_solo2;
        state.last_bomba_ligada[0] = state.bomba_ligada[0];
        state.last_bomba_ligada[1] = state.bomba_ligada[1];
    }
    
    return mudou;
}

// --- Enviar Telemetria via MQTT ---
bool enviar_telemetria_otimizada() {
    if (!mqttClient.connected()) {
        Serial.println("MQTT desconectado - telemetria não enviada");
        return false;
    }

    JsonDocument doc;
    doc["temperatura"] = state.last_temperatura;
    doc["umidadeAr"] = state.last_umidadeAr;
    JsonArray umidadeSoloArray = doc["umidadeSolo"].to<JsonArray>();
    umidadeSoloArray.add(state.last_umidadeSolo[0]);
    umidadeSoloArray.add(state.last_umidadeSolo[1]);
    doc["timestamp"] = millis();
    
    JsonArray bombasArray = doc["bombas"].to<JsonArray>();
    bombasArray.add(state.bomba_ligada[0]);
    bombasArray.add(state.bomba_ligada[1]);

    String output;
    serializeJson(doc, output);

    bool success = mqttClient.publish(TOPIC_TELEMETRIA, output.c_str());
    if (success) {
        Serial.println("Telemetria enviada via MQTT!");
    } else {
        Serial.println("Erro ao enviar telemetria via MQTT");
    }
    return success;
}

// --- Enviar Status da Bomba via MQTT ---
bool enviar_status_bomba_rapido(int bomba_index) {
    if (!mqttClient.connected()) {
        Serial.println("MQTT desconectado - status bomba não enviado");
        return false;
    }
    
    JsonDocument doc;
    JsonArray bombasArray = doc.to<JsonArray>();
    bombasArray.add(state.bomba_ligada[0]);
    bombasArray.add(state.bomba_ligada[1]);

    String output;
    serializeJson(doc, output);

    bool success = mqttClient.publish(TOPIC_BOMBAS, output.c_str());
    if (success) {
        Serial.printf("Status bomba %d enviado: %s\n", bomba_index + 1, state.bomba_ligada[bomba_index] ? "LIGADA" : "DESLIGADA");
    } else {
        Serial.printf("Erro envio status bomba %d\n", bomba_index + 1);
    }
    return success;
}

// --- Callback para Mensagens MQTT ---
void mqtt_callback(char* topic, byte* payload, unsigned int length) {
    if (strcmp(topic, TOPIC_COMANDOS) != 0) return;

    String message;
    for (unsigned int i = 0; i < length; i++) {
        message += (char)payload[i];
    }

    JsonDocument doc;
    if (deserializeJson(doc, message)) {
        Serial.println("Erro ao parsear comando MQTT");
        return;
    }

    String command_id = doc["id"] | "";
    if (command_id.length() > 0 && command_id == state.last_command_id) {
        return; // Comando já processado
    }

    const char* type = doc["type"];
    if (!type) return;

    Serial.printf("Processando comando MQTT: %s\n", type);

    if (strcmp(type, "config") == 0) {
        JsonArray minArray = doc["payload"]["min_umidade"].as<JsonArray>();
        JsonArray maxArray = doc["payload"]["max_umidade"].as<JsonArray>();
        
        if (minArray.size() >= 2 && maxArray.size() >= 2) {
            state.min_umidade[0] = minArray[0];
            state.max_umidade[0] = maxArray[0];
            state.min_umidade[1] = minArray[1];
            state.max_umidade[1] = maxArray[1];
            Serial.println("Config atualizada via MQTT!");
        }

    } else if (strcmp(type, "manual_irrigate") == 0) {
        int bomba = doc["bomba"];
        if (bomba >= 1 && bomba <= 2) {
            int index = bomba - 1;
            if (!state.manual_irrigation_active[index]) {
                Serial.printf("Irrigação manual bomba %d via MQTT\n", bomba);
                state.manual_irrigation_active[index] = true;
                state.manual_irrigation_start_time[index] = millis();
                controlarBomba(bomba, true);
                state.bomba_ligada[index] = true;
                enviar_status_bomba_rapido(index);
            }
        }
    }

    state.last_command_id = command_id;
}

// --- Gerenciamento de Irrigação Manual ---
void handle_manual_irrigation() {
    for (int i = 0; i < 2; i++) {
        if (state.manual_irrigation_active[i]) {
            if (millis() - state.manual_irrigation_start_time[i] >= MANUAL_IRRIGATION_DURATION) {
                int bomba_num = i + 1;
                Serial.printf("Fim irrigação manual bomba %d\n", bomba_num);
                controlarBomba(bomba_num, false);
                state.bomba_ligada[i] = false;
                state.manual_irrigation_active[i] = false;
                enviar_status_bomba_rapido(i);
            }
        }
    }
}

// --- Lógica de Irrigação Automática ---
void logica_irrigacao() {
    int umidade_atual[] = {
        lerUmidadePercentual(PINO_UMIDADE_SOLO_1),
        lerUmidadePercentual(PINO_UMIDADE_SOLO_2)
    };
    
    for (int i = 0; i < 2; i++) {
        int bomba_num = i + 1;
        
        Serial.printf("=== BOMBA %d ===\n", bomba_num);
        Serial.printf("Umidade atual: %d%%\n", umidade_atual[i]);
        Serial.printf("Min configurado: %d%%\n", state.min_umidade[i]);
        Serial.printf("Max configurado: %d%%\n", state.max_umidade[i]);
        Serial.printf("Manual ativo: %s\n", state.manual_irrigation_active[i] ? "SIM" : "NÃO");
        Serial.printf("Bomba ligada: %s\n", state.bomba_ligada[i] ? "SIM" : "NÃO");
        
        if (!state.manual_irrigation_active[i]) {
            if (umidade_atual[i] < state.min_umidade[i] && !state.bomba_ligada[i]) {
                Serial.printf(">>> LIGANDO bomba %d (umidade %d%% < min %d%%)\n", 
                             bomba_num, umidade_atual[i], state.min_umidade[i]);
                controlarBomba(bomba_num, true);
                state.bomba_ligada[i] = true;
                enviar_status_bomba_rapido(i);
            } 
            else if (umidade_atual[i] >= state.max_umidade[i] && state.bomba_ligada[i]) {
                Serial.printf(">>> DESLIGANDO bomba %d (umidade %d%% >= max %d%%)\n", 
                             bomba_num, umidade_atual[i], state.max_umidade[i]);
                controlarBomba(bomba_num, false);
                state.bomba_ligada[i] = false;
                enviar_status_bomba_rapido(i);
            }
            else {
                Serial.println(">>> Nenhuma ação necessária");
            }
        } else {
            Serial.println(">>> MODO MANUAL - lógica automática pausada");
        }
        Serial.println();
    }
}