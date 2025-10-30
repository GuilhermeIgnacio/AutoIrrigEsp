#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>

#include "sensor_dht.h"
#include "sensor_umidade_solo.h"
#include "controle_bomba.h"

// --- Configurações de Rede ---
const char* SSID = "guilherme_2G";
const char* PASSWORD = "789453216a";
const char* SERVER_URL = "https://auto-irrig-default-rtdb.firebaseio.com";

// --- Intervalos Otimizados ---
const unsigned long TELEMETRIA_INTERVAL = 10000; // 10s em vez de 5s (reduz tráfego)
const unsigned long COMANDO_INTERVAL = 3000;     // 3s para comandos (mais responsivo)
const unsigned long LOGICA_INTERVAL = 2000;      // 2s para lógica local

// --- Controle de Estado ---
struct SystemState {
    int min_umidade[2] = {30, 30};
    int max_umidade[2] = {60, 60};
    bool bomba_ligada[2] = {false, false};
    bool manual_irrigation_active[2] = {false, false};
    unsigned long manual_irrigation_start_time[2] = {0, 0};
    
    // Cache de telemetria para evitar envios duplicados
    float last_temperatura = -999;
    float last_umidadeAr = -999;
    int last_umidadeSolo[2] = {-1, -1};
    
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

// --- Protótipos ---
void setup_wifi();
bool enviar_telemetria_otimizada();
void verificar_comandos_otimizado();
void handle_manual_irrigation();
void logica_irrigacao();
bool dados_telemetria_mudaram();

void setup() {
    Serial.begin(115200);
    setupSensorDHT();
    setupSensorUmidadeSolo(PINO_UMIDADE_SOLO_1);
    setupSensorUmidadeSolo(PINO_UMIDADE_SOLO_2);
    setupBombas(PINO_BOMBA_1, PINO_BOMBA_2);
    setup_wifi();
}

void loop() {
    static unsigned long last_telemetry_time = 0;
    static unsigned long last_command_time = 0;
    static unsigned long last_logic_time = 0;

    unsigned long now = millis();

    // Verifica comandos com intervalo menor (mais responsivo)
    if (now - last_command_time > COMANDO_INTERVAL) {
        last_command_time = now;
        if (WiFi.status() == WL_CONNECTED) {
            verificar_comandos_otimizado();
        } else {
            Serial.println("WiFi desconectado. Tentando reconectar...");
            setup_wifi();
        }
    }

    // Envia telemetria apenas se houver mudanças significativas
    if (now - last_telemetry_time > TELEMETRIA_INTERVAL) {
        last_telemetry_time = now;
        if (WiFi.status() == WL_CONNECTED && dados_telemetria_mudaram()) {
            enviar_telemetria_otimizada();
        }
    }

    // Lógica local roda com mais frequência
    if (now - last_logic_time > LOGICA_INTERVAL) {
        last_logic_time = now;
        handle_manual_irrigation();
        logica_irrigacao();
    }
}

// --- Verifica se dados mudaram significativamente ---
bool dados_telemetria_mudaram() {
    float temp = getTemperature();
    float umid_ar = getAirHumidity();
    int umid_solo1 = lerUmidadePercentual(PINO_UMIDADE_SOLO_1);
    int umid_solo2 = lerUmidadePercentual(PINO_UMIDADE_SOLO_2);
    
    // Margem de 1% para temperatura/umidade ar, 2% para solo
    bool mudou = (abs(temp - state.last_temperatura) > 1.0) ||
                 (abs(umid_ar - state.last_umidadeAr) > 1.0) ||
                 (abs(umid_solo1 - state.last_umidadeSolo[0]) > 2) ||
                 (abs(umid_solo2 - state.last_umidadeSolo[1]) > 2);
    
    if (mudou) {
        state.last_temperatura = temp;
        state.last_umidadeAr = umid_ar;
        state.last_umidadeSolo[0] = umid_solo1;
        state.last_umidadeSolo[1] = umid_solo2;
    }
    
    return mudou;
}

// --- Telemetria Otimizada (PATCH em vez de PUT) ---
bool enviar_telemetria_otimizada() {
    HTTPClient http;
    String serverPath = String(SERVER_URL) + "/telemetry.json";
    http.begin(serverPath);
    http.addHeader("Content-Type", "application/json");

    JsonDocument doc;
    doc["temperatura"] = state.last_temperatura;
    doc["umidadeAr"] = state.last_umidadeAr;
    JsonArray umidadeSoloArray = doc["umidadeSolo"].to<JsonArray>();
    umidadeSoloArray.add(state.last_umidadeSolo[0]);
    umidadeSoloArray.add(state.last_umidadeSolo[1]);
    doc["timestamp"] = millis();
    
    // Adiciona status das bombas
    JsonArray bombasArray = doc["bombas"].to<JsonArray>();
    bombasArray.add(state.bomba_ligada[0]);
    bombasArray.add(state.bomba_ligada[1]);

    String output;
    serializeJson(doc, output);

    int httpResponseCode = http.PATCH(output); // PATCH é mais eficiente
    
    if (httpResponseCode > 0) {
        Serial.println("Telemetria atualizada!");
        http.end();
        return true;
    } else {
        Serial.printf("Erro telemetria: %s\n", http.errorToString(httpResponseCode).c_str());
        http.end();
        return false;
    }
}

// --- Verificação de Comandos Otimizada ---
void verificar_comandos_otimizado() {
    HTTPClient http;
    String serverPath = String(SERVER_URL) + "/commands.json";
    http.begin(serverPath);

    int httpResponseCode = http.GET();
    if (httpResponseCode != 200) {
        http.end();
        return;
    }

    String payload = http.getString();
    http.end();

    if (payload == "null" || payload.length() == 0) {
        return;
    }

    JsonDocument doc;
    if (deserializeJson(doc, payload)) {
        Serial.println("Erro ao parsear comando");
        return;
    }

    // Verifica se é um comando novo usando timestamp ou ID
    String command_id = doc["id"] | "";
    if (command_id.length() > 0 && command_id == state.last_command_id) {
        return; // Comando já processado
    }
    
    const char* type = doc["type"];
    if (!type) return;

    Serial.printf("Processando comando: %s\n", type);

    if (strcmp(type, "config") == 0) {
        // Atualiza configurações
        JsonArray minArray = doc["payload"]["min_umidade"].as<JsonArray>();
        JsonArray maxArray = doc["payload"]["max_umidade"].as<JsonArray>();
        
        if (minArray.size() >= 2 && maxArray.size() >= 2) {
            state.min_umidade[0] = minArray[0];
            state.max_umidade[0] = maxArray[0];
            state.min_umidade[1] = minArray[1];
            state.max_umidade[1] = maxArray[1];
            Serial.println("Config atualizada!");
        }

    } else if (strcmp(type, "manual_irrigate") == 0) {
        int bomba = doc["bomba"];
        if (bomba >= 1 && bomba <= 2) {
            int index = bomba - 1;
            if (!state.manual_irrigation_active[index]) {
                Serial.printf("Irrigacao manual bomba %d\n", bomba);
                state.manual_irrigation_active[index] = true;
                state.manual_irrigation_start_time[index] = millis();
                controlarBomba(bomba, true);
                state.bomba_ligada[index] = true;
            }
        }
    }

    // Marca comando como processado
    state.last_command_id = command_id;
    
    // Limpa comando do Firebase usando DELETE
    http.begin(serverPath);
    http.sendRequest("DELETE");
    http.end();
}

// --- Gerenciamento de Irrigação Manual ---
void handle_manual_irrigation() {
    for (int i = 0; i < 2; i++) {
        if (state.manual_irrigation_active[i]) {
            if (millis() - state.manual_irrigation_start_time[i] >= MANUAL_IRRIGATION_DURATION) {
                int bomba_num = i + 1;
                Serial.printf("Fim irrigacao manual bomba %d\n", bomba_num);
                controlarBomba(bomba_num, false);
                state.bomba_ligada[i] = false;
                state.manual_irrigation_active[i] = false;
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
        
        // LOG DETALHADO PARA DEBUG
        Serial.printf("=== BOMBA %d ===\n", bomba_num);
        Serial.printf("Umidade atual: %d%%\n", umidade_atual[i]);
        Serial.printf("Min configurado: %d%%\n", state.min_umidade[i]);
        Serial.printf("Max configurado: %d%%\n", state.max_umidade[i]);
        Serial.printf("Manual ativo: %s\n", state.manual_irrigation_active[i] ? "SIM" : "NAO");
        Serial.printf("Bomba ligada: %s\n", state.bomba_ligada[i] ? "SIM" : "NAO");
        
        if (!state.manual_irrigation_active[i]) {
            // Verifica se precisa ligar
            if (umidade_atual[i] < state.min_umidade[i] && !state.bomba_ligada[i]) {
                Serial.printf(">>> LIGANDO bomba %d (umidade %d%% < min %d%%)\n", 
                             bomba_num, umidade_atual[i], state.min_umidade[i]);
                controlarBomba(bomba_num, true);
                state.bomba_ligada[i] = true;
            } 
            // Verifica se precisa desligar
            else if (umidade_atual[i] >= state.max_umidade[i] && state.bomba_ligada[i]) {
                Serial.printf(">>> DESLIGANDO bomba %d (umidade %d%% >= max %d%%)\n", 
                             bomba_num, umidade_atual[i], state.max_umidade[i]);
                controlarBomba(bomba_num, false);
                state.bomba_ligada[i] = false;
            }
            else {
                Serial.println(">>> Nenhuma acao necessaria");
            }
        } else {
            Serial.println(">>> MODO MANUAL - logica automatica pausada");
        }
        Serial.println();
    }
}

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