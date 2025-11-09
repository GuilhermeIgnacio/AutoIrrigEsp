#include <Arduino.h>
#include <WiFi.h>
#include <PubSubClient.h>
#include <WiFiClientSecure.h>
#include <ArduinoJson.h>

#include "sensor_dht.h"
#include "sensor_umidade_solo.h"
#include "controle_bomba.h"

// --- Configurações de Rede ---
const char* SSID = "guilherme_2G";
const char* PASSWORD = "789453216a";
const char* MQTT_BROKER = "d98fa3b64a654694be6a0cc7c5fa1799.s1.eu.hivemq.cloud";
const int MQTT_PORT = 8883;
const char* MQTT_CLIENT_ID = "ESP32_Irrigacao_01";
const char* MQTT_USER = "hivemq.webclient.1761853881390";
const char* MQTT_PASS = "7G4#QF*ud&fey9S3:oYL";

// --- Tópicos MQTT Otimizados para HiveMQ Cloud ---
// Usando estrutura hierárquica eficiente
const char* TOPIC_TELEMETRIA = "irrigacao/telemetria";
const char* TOPIC_COMANDOS = "irrigacao/commands";
const char* TOPIC_BOMBAS = "irrigacao/pump/status";
const char* TOPIC_SENSORS = "irrigacao/sensor";
const char* TOPIC_STATUS = "irrigacao/device/status";
const char* TOPIC_LWT = "irrigacao/lwt"; // Last Will Testament

// --- Intervalos Otimizados para HiveMQ Cloud ---
const unsigned long SENSOR_READ_INTERVAL = 1000;       // 1s
const unsigned long TELEMETRIA_INTERVAL = 5000;        // 5s
const unsigned long SENSOR_FAST_INTERVAL = 2000;       // 2s
const unsigned long LOGICA_INTERVAL = 1500;            // 1.5s
const unsigned long HEARTBEAT_INTERVAL = 30000;        // 30s
const unsigned long MQTT_RECONNECT_DELAY = 5000;       // 5s

// --- Limiares de Mudança ---
const float TEMP_THRESHOLD = 0.5;
const float HUMIDITY_AIR_THRESHOLD = 2.0;
const int HUMIDITY_SOIL_THRESHOLD = 2;

// --- Configurações HiveMQ Cloud ---
const int HIVE_MQTT_KEEPALIVE = 60;
const int HIVE_MQTT_SOCKET_TIMEOUT = 15;
const int MQTT_BUFFER_SIZE = 512;
const int MAX_RECONNECT_ATTEMPTS = 3;
const int QOS_TELEMETRY = 0;
const int QOS_COMMANDS = 1;
const int QOS_STATUS = 1;
const bool RETAIN_STATUS = true;

// --- Controle de Estado ---
struct SystemState {
    int min_umidade[2] = {0, 0};
    int max_umidade[2] = {0, 0};
    bool bomba_ligada[2] = {false, false};
    bool manual_irrigation_active[2] = {false, false};
    unsigned long manual_irrigation_start_time[2] = {0, 0};
    unsigned long manual_irrigation_duration[2] = {3000, 3000};
    
    float last_temperatura = -999;
    float last_umidadeAr = -999;
    int last_umidadeSolo[2] = {-1, -1};
    
    float current_temperatura = 0;
    float current_umidadeAr = 0;
    int current_umidadeSolo[2] = {0, 0};
    
    bool last_bomba_ligada[2] = {false, false};
    
    String last_command_id = "";
    
    unsigned long telemetry_count = 0;
    unsigned long command_count = 0;
    unsigned long mqtt_reconnects = 0;
    unsigned long last_mqtt_error = 0;
    
    bool mqtt_connected = false;
    bool wifi_connected = false;
    unsigned long connection_start_time = 0;
};

SystemState state;

const unsigned long MANUAL_IRRIGATION_DURATION_DEFAULT = 3000;

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
void read_sensors();
bool enviar_telemetria_completa();
bool enviar_sensores_rapido();
bool enviar_status_sistema();
void mqtt_callback(char* topic, byte* payload, unsigned int length);
void handle_manual_irrigation();
void logica_irrigacao();
bool sensor_data_changed_significantly();
bool enviar_status_bomba_rapido(int bomba_index);
void publish_lwt_online();

void setup() {
    Serial.begin(115200);
    delay(1000);
    Serial.println("\n╔════════════════════════════════════════╗");
    Serial.println("║  ESP32 Sistema de Irrigação HiveMQ    ║");
    Serial.println("╚════════════════════════════════════════╝");
    
    setupSensorDHT();
    setupSensorUmidadeSolo(PINO_UMIDADE_SOLO_1);
    setupSensorUmidadeSolo(PINO_UMIDADE_SOLO_2);
    setupBombas(PINO_BOMBA_1, PINO_BOMBA_2);
    
    setup_wifi();
    setup_mqtt();
    
    read_sensors();
    state.connection_start_time = millis();
    Serial.println("✓ Sistema inicializado!\n");
}

void loop() {
    static unsigned long last_sensor_read = 0;
    static unsigned long last_telemetry_time = 0;
    static unsigned long last_sensor_send = 0;
    static unsigned long last_logic_time = 0;
    static unsigned long last_heartbeat = 0;
    static unsigned long last_reconnect_attempt = 0;

    unsigned long now = millis();

    if (!mqttClient.connected()) {
        if (now - last_reconnect_attempt >= MQTT_RECONNECT_DELAY) {
            last_reconnect_attempt = now;
            reconnect_mqtt();
        }
    } else {
        mqttClient.loop();
    }

    if (now - last_sensor_read >= SENSOR_READ_INTERVAL) {
        last_sensor_read = now;
        read_sensors();
    }

    if (now - last_sensor_send >= SENSOR_FAST_INTERVAL) {
        last_sensor_send = now;
        if (state.mqtt_connected && sensor_data_changed_significantly()) {
            enviar_sensores_rapido();
        }
    }

    if (now - last_telemetry_time >= TELEMETRIA_INTERVAL) {
        last_telemetry_time = now;
        if (state.mqtt_connected) {
            enviar_telemetria_completa();
        }
    }

    if (WiFi.status() != WL_CONNECTED && state.wifi_connected) {
        Serial.println("⚠ WiFi desconectado!");
        state.wifi_connected = false;
        state.mqtt_connected = false;
        setup_wifi();
    }

    if (now - last_logic_time >= LOGICA_INTERVAL) {
        last_logic_time = now;
        handle_manual_irrigation();
        logica_irrigacao();
    }

    if (now - last_heartbeat >= HEARTBEAT_INTERVAL) {
        last_heartbeat = now;
        if (state.mqtt_connected) {
            enviar_status_sistema();
        }
    }

    delay(10);
}

void read_sensors() {
    state.current_temperatura = getTemperature();
    state.current_umidadeAr = getAirHumidity();
    state.current_umidadeSolo[0] = lerUmidadePercentual(PINO_UMIDADE_SOLO_1);
    state.current_umidadeSolo[1] = lerUmidadePercentual(PINO_UMIDADE_SOLO_2);
}

bool sensor_data_changed_significantly() {
    bool temp_changed = abs(state.current_temperatura - state.last_temperatura) >= TEMP_THRESHOLD;
    bool air_hum_changed = abs(state.current_umidadeAr - state.last_umidadeAr) >= HUMIDITY_AIR_THRESHOLD;
    bool soil1_changed = abs(state.current_umidadeSolo[0] - state.last_umidadeSolo[0]) >= HUMIDITY_SOIL_THRESHOLD;
    bool soil2_changed = abs(state.current_umidadeSolo[1] - state.last_umidadeSolo[1]) >= HUMIDITY_SOIL_THRESHOLD;
    
    return temp_changed || air_hum_changed || soil1_changed || soil2_changed;
}

void setup_wifi() {
    delay(10);
    Serial.println("🔌 Conectando WiFi...");
    WiFi.mode(WIFI_STA);
    WiFi.begin(SSID, PASSWORD);
    WiFi.setAutoReconnect(true);
    WiFi.setAutoConnect(true);
    
    int tentativas = 0;
    while (WiFi.status() != WL_CONNECTED && tentativas < 30) {
        delay(500);
        Serial.print(".");
        tentativas++;
    }
    
    if (WiFi.status() == WL_CONNECTED) {
        Serial.println("\n✓ WiFi Conectado!");
        Serial.printf("  IP: %s\n", WiFi.localIP().toString().c_str());
        Serial.printf("  RSSI: %d dBm\n", WiFi.RSSI());
        state.wifi_connected = true;
    } else {
        Serial.println("\n✗ Falha ao conectar WiFi!");
        state.wifi_connected = false;
    }
}

void setup_mqtt() {
    espClient.setInsecure();
    
    mqttClient.setServer(MQTT_BROKER, MQTT_PORT);
    mqttClient.setCallback(mqtt_callback);
    mqttClient.setKeepAlive(HIVE_MQTT_KEEPALIVE);
    mqttClient.setSocketTimeout(HIVE_MQTT_SOCKET_TIMEOUT);
    mqttClient.setBufferSize(MQTT_BUFFER_SIZE);
    
    Serial.println("📡 Configuração MQTT concluída");
    reconnect_mqtt();
}

void reconnect_mqtt() {
    if (!state.wifi_connected) return;
    
    int tentativas = 0;
    while (!mqttClient.connected() && tentativas < MAX_RECONNECT_ATTEMPTS) {
        Serial.printf("🔄 Conectando HiveMQ Cloud (tentativa %d/%d)...\n", 
                     tentativas + 1, MAX_RECONNECT_ATTEMPTS);
        
        if (mqttClient.connect(MQTT_CLIENT_ID, MQTT_USER, MQTT_PASS,
                              TOPIC_LWT, QOS_STATUS, RETAIN_STATUS, "{\"status\":\"offline\"}")) {
            Serial.println("✓ MQTT Conectado!");
            state.mqtt_connected = true;
            state.mqtt_reconnects++;
            
            mqttClient.subscribe(TOPIC_COMANDOS, QOS_COMMANDS);
            Serial.printf("✓ Subscrito em: %s\n", TOPIC_COMANDOS);
            
            publish_lwt_online();
            enviar_status_sistema();
            enviar_telemetria_completa();
            
            return;
        } else {
            int rc = mqttClient.state();
            Serial.printf("✗ Falha MQTT, rc=%d\n", rc);
            state.last_mqtt_error = rc;
            
            if (rc == 5) {
                Serial.println("  → Erro de autenticação. Verifique credenciais!");
                break;
            }
            
            tentativas++;
            if (tentativas < MAX_RECONNECT_ATTEMPTS) {
                delay(2000);
            }
        }
    }
    
    if (!mqttClient.connected()) {
        state.mqtt_connected = false;
    }
}

void publish_lwt_online() {
    JsonDocument doc;
    doc["status"] = "online";
    doc["ip"] = WiFi.localIP().toString();
    doc["rssi"] = WiFi.RSSI();
    doc["uptime"] = millis() / 1000;
    
    String output;
    serializeJson(doc, output);
    
    mqttClient.publish(TOPIC_LWT, output.c_str(), RETAIN_STATUS);
}

bool enviar_sensores_rapido() {
    if (!mqttClient.connected()) return false;

    JsonDocument doc;
    doc["t"] = round(state.current_temperatura * 10) / 10.0;
    doc["h"] = round(state.current_umidadeAr);
    JsonArray s = doc["s"].to<JsonArray>();
    s.add(state.current_umidadeSolo[0]);
    s.add(state.current_umidadeSolo[1]);
    doc["ts"] = millis() / 1000;

    String output;
    serializeJson(doc, output);

    bool success = mqttClient.publish(TOPIC_SENSORS, output.c_str(), false);
    if (success) {
        state.last_temperatura = state.current_temperatura;
        state.last_umidadeAr = state.current_umidadeAr;
        state.last_umidadeSolo[0] = state.current_umidadeSolo[0];
        state.last_umidadeSolo[1] = state.current_umidadeSolo[1];
    }
    return success;
}

bool enviar_telemetria_completa() {
    if (!mqttClient.connected()) return false;

    JsonDocument doc;
    
    JsonObject sensor = doc["sensor"].to<JsonObject>();
    sensor["temp"] = round(state.current_temperatura * 10) / 10.0;
    sensor["airHum"] = round(state.current_umidadeAr);
    JsonArray soil = sensor["soil"].to<JsonArray>();
    soil.add(state.current_umidadeSolo[0]);
    soil.add(state.current_umidadeSolo[1]);
    
    JsonArray pumps = doc["pumps"].to<JsonArray>();
    pumps.add(state.bomba_ligada[0]);
    pumps.add(state.bomba_ligada[1]);
    
    JsonObject cfg = doc["config"].to<JsonObject>();
    JsonArray cMin = cfg["min"].to<JsonArray>();
    cMin.add(state.min_umidade[0]);
    cMin.add(state.min_umidade[1]);
    JsonArray cMax = cfg["max"].to<JsonArray>();
    cMax.add(state.max_umidade[0]);
    cMax.add(state.max_umidade[1]);
    
    doc["seq"] = state.telemetry_count++;
    doc["uptime"] = millis() / 1000;

    String output;
    serializeJson(doc, output);

    return mqttClient.publish(TOPIC_TELEMETRIA, output.c_str(), false);
}

bool enviar_status_sistema() {
    if (!mqttClient.connected()) return false;

    JsonDocument doc;
    doc["online"] = true;
    doc["uptime"] = millis() / 1000;
    doc["wifi"] = WiFi.RSSI();
    doc["heap"] = ESP.getFreeHeap();
    doc["msgSent"] = state.telemetry_count;
    doc["msgRecv"] = state.command_count;
    doc["reconnects"] = state.mqtt_reconnects;

    String output;
    serializeJson(doc, output);

    return mqttClient.publish(TOPIC_STATUS, output.c_str(), RETAIN_STATUS);
}

bool enviar_status_bomba_rapido(int bomba_index) {
    if (!mqttClient.connected()) return false;
    
    JsonDocument doc;
    JsonArray pumps = doc["pumps"].to<JsonArray>();
    pumps.add(state.bomba_ligada[0]);
    pumps.add(state.bomba_ligada[1]);
    doc["changed"] = bomba_index;
    doc["ts"] = millis() / 1000;

    String output;
    serializeJson(doc, output);

    bool success = mqttClient.publish(TOPIC_BOMBAS, output.c_str(), false);
    if (success) {
        Serial.printf("✓ Bomba %d: %s\n", bomba_index + 1, 
                     state.bomba_ligada[bomba_index] ? "LIGADA" : "DESLIGADA");
    }
    return success;
}

void mqtt_callback(char* topic, byte* payload, unsigned int length) {
    if (strcmp(topic, TOPIC_COMANDOS) != 0) return;

    state.command_count++;
    
    String message;
    message.reserve(length);
    for (unsigned int i = 0; i < length; i++) {
        message += (char)payload[i];
    }

    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, message);
    if (error) {
        Serial.printf("✗ Erro JSON: %s\n", error.c_str());
        return;
    }

    String command_id = doc["id"] | "";
    if (command_id.length() > 0 && command_id == state.last_command_id) {
        return;
    }

    const char* type = doc["type"];
    if (!type) return;

    Serial.printf("→ CMD [%s]: %s\n", command_id.c_str(), type);

    if (strcmp(type, "config") == 0) {
        JsonArray minArray = doc["payload"]["min_umidade"].as<JsonArray>();
        JsonArray maxArray = doc["payload"]["max_umidade"].as<JsonArray>();
        
        if (minArray.size() >= 2 && maxArray.size() >= 2) {
            state.min_umidade[0] = minArray[0];
            state.max_umidade[0] = maxArray[0];
            state.min_umidade[1] = minArray[1];
            state.max_umidade[1] = maxArray[1];
            Serial.println("  ✓ Config atualizada");
            enviar_telemetria_completa();
        }

    } else if (strcmp(type, "manual_irrigate") == 0) {
        bool hasIndividual = doc.containsKey("bomba");
        bool hasMultiple = doc.containsKey("bombas");
        
        unsigned long duration = MANUAL_IRRIGATION_DURATION_DEFAULT;
        if (doc.containsKey("duration")) {
            duration = doc["duration"].as<unsigned long>();
            Serial.printf("  Duração especificada: %lu ms\n", duration);
        }
        
        if (hasIndividual) {
            int bomba = doc["bomba"];
            if (bomba >= 1 && bomba <= 2) {
                int index = bomba - 1;
                if (!state.manual_irrigation_active[index]) {
                    Serial.printf("  ✓ Irrigação manual B%d por %lu ms\n", bomba, duration);
                    state.manual_irrigation_active[index] = true;
                    state.manual_irrigation_start_time[index] = millis();
                    state.manual_irrigation_duration[index] = duration;
                    controlarBomba(bomba, true);
                    state.bomba_ligada[index] = true;
                    enviar_status_bomba_rapido(index);
                }
            }
        }
        
        if (hasMultiple) {
            JsonArray bombasArray = doc["bombas"].as<JsonArray>();
            Serial.printf("  ✓ Irrigação manual múltipla por %lu ms: ", duration);
            
            for (JsonVariant v : bombasArray) {
                int bomba = v.as<int>();
                if (bomba >= 1 && bomba <= 2) {
                    int index = bomba - 1;
                    Serial.printf("B%d ", bomba);
                    
                    if (!state.manual_irrigation_active[index]) {
                        state.manual_irrigation_active[index] = true;
                        state.manual_irrigation_start_time[index] = millis();
                        state.manual_irrigation_duration[index] = duration;
                        controlarBomba(bomba, true);
                        state.bomba_ligada[index] = true;
                    }
                }
            }
            Serial.println();
            
            for (int i = 0; i < 2; i++) {
                if (state.bomba_ligada[i]) {
                    enviar_status_bomba_rapido(i);
                    delay(10);
                }
            }
        }
        
    } else if (strcmp(type, "stop") == 0) {
        bool hasIndividual = doc.containsKey("bomba");
        bool hasMultiple = doc.containsKey("bombas");
        
        if (hasIndividual) {
            int bomba = doc["bomba"];
            if (bomba >= 1 && bomba <= 2) {
                int index = bomba - 1;
                controlarBomba(bomba, false);
                state.bomba_ligada[index] = false;
                state.manual_irrigation_active[index] = false;
                Serial.printf("  ✓ Bomba %d parada\n", bomba);
                enviar_status_bomba_rapido(index);
            }
        }
        
        if (hasMultiple) {
            JsonArray bombasArray = doc["bombas"].as<JsonArray>();
            Serial.printf("  ✓ Parada múltipla: ");
            
            for (JsonVariant v : bombasArray) {
                int bomba = v.as<int>();
                if (bomba >= 1 && bomba <= 2) {
                    int index = bomba - 1;
                    Serial.printf("B%d ", bomba);
                    
                    controlarBomba(bomba, false);
                    state.bomba_ligada[index] = false;
                    state.manual_irrigation_active[index] = false;
                }
            }
            Serial.println();
            
            for (int i = 0; i < 2; i++) {
                enviar_status_bomba_rapido(i);
                delay(10);
            }
        }
        
    } else if (strcmp(type, "ping") == 0) {
        enviar_status_sistema();
    }

    state.last_command_id = command_id;
}

void handle_manual_irrigation() {
    for (int i = 0; i < 2; i++) {
        if (state.manual_irrigation_active[i]) {
            unsigned long elapsed = millis() - state.manual_irrigation_start_time[i];
            unsigned long duration = state.manual_irrigation_duration[i];
            
            if (elapsed >= duration) {
                int bomba_num = i + 1;
                Serial.printf("⏱ Fim irrigação manual B%d (duração: %lu ms)\n", bomba_num, duration);
                controlarBomba(bomba_num, false);
                state.bomba_ligada[i] = false;
                state.manual_irrigation_active[i] = false;
                enviar_status_bomba_rapido(i);
            } else {
                static unsigned long last_debug[2] = {0, 0};
                if (millis() - last_debug[i] >= 1000) {
                    last_debug[i] = millis();
                    unsigned long remaining = duration - elapsed;
                    Serial.printf("⏳ B%d: %lu ms restantes\n", i + 1, remaining);
                }
            }
        }
    }
}

void logica_irrigacao() {
    for (int i = 0; i < 2; i++) {
        if (state.manual_irrigation_active[i]) continue;
        
        int bomba_num = i + 1;
        int umidade = state.current_umidadeSolo[i];
        
        bool should_turn_on = umidade < state.min_umidade[i] && !state.bomba_ligada[i];
        bool should_turn_off = umidade >= state.max_umidade[i] && state.bomba_ligada[i];
        
        if (should_turn_on) {
            Serial.printf("🔵 LIGA B%d (%d%% < %d%%)\n", 
                         bomba_num, umidade, state.min_umidade[i]);
            controlarBomba(bomba_num, true);
            state.bomba_ligada[i] = true;
            enviar_status_bomba_rapido(i);
        } 
        else if (should_turn_off) {
            Serial.printf("🔴 DESLIGA B%d (%d%% >= %d%%)\n", 
                         bomba_num, umidade, state.max_umidade[i]);
            controlarBomba(bomba_num, false);
            state.bomba_ligada[i] = false;
            enviar_status_bomba_rapido(i);
        }
    }
}