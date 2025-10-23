#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>

#include "sensor_dht.h"
#include "sensor_umidade_solo.h"
#include "controle_bomba.h"

// --- Configurações de Rede ---
const char* SSID = "NOME_REDE_WIFI"; // Nome do Wifi a contectar (mudar)
const char* PASSWORD = "SENHA_REDE_WIFI"; // Senha o Wifi a contectar mudar)
const char* SERVER_URL = "http://Saeko.pythonanywhere.com"; // Apontamento pythonanywhere.com (mudar)

// --- NOVAS VARIÁVEIS PARA CONTROLE MANUAL ---
bool manual_irrigation_active[] = {false, false};
unsigned long manual_irrigation_start_time[] = {0, 0};
const unsigned long MANUAL_IRRIGATION_DURATION = 3000; // 3s para acionar a bomba manualmente

// --- Variáveis de Lógica de Irrigação ---
int min_umidade[] = {30, 30};
int max_umidade[] = {60, 60};
bool bomba_ligada[] = {false, false};

// --- Mapeamento de Pinos ---
const int PINO_UMIDADE_SOLO_1 = 34;
const int PINO_UMIDADE_SOLO_2 = 35;
const int PINO_BOMBA_1 = 26;
const int PINO_BOMBA_2 = 27;

// --- Protótipos de Funções ---
void setup_wifi();
void enviar_telemetria();
void verificar_comandos();
void handle_manual_irrigation(); // <-- NOVA FUNÇÃO
void logica_irrigacao();

void setup() {
    Serial.begin(115200);
    setupSensorDHT();
    setupSensorUmidadeSolo(PINO_UMIDADE_SOLO_1);
    setupSensorUmidadeSolo(PINO_UMIDADE_SOLO_2);
    setupBombas(PINO_BOMBA_1, PINO_BOMBA_2);
    setup_wifi();
}

void loop() {
    static unsigned long last_sync_time = 0;
    static unsigned long last_logic_time = 0;

    // A cada 5 segundos, sincroniza com o servidor
    if (millis() - last_sync_time > 5000) {
        last_sync_time = millis();
        if (WiFi.status() == WL_CONNECTED) {
            enviar_telemetria();
            verificar_comandos();
        } else {
            Serial.println("WiFi desconectado. Tentando reconectar...");
            setup_wifi();
        }
    }

    // A cada 1 segundo, verifica a lógica (automática e manual)
    if (millis() - last_logic_time > 1000) {
        last_logic_time = millis();
        handle_manual_irrigation(); 
        logica_irrigacao();         
    }
}

// --- Implementação das Funções ---

void verificar_comandos() {
    HTTPClient http;
    String serverPath = String(SERVER_URL) + "/get_command";
    http.begin(serverPath);

    int httpResponseCode = http.GET();
    if (httpResponseCode == 200) {
        String payload = http.getString();
        Serial.print("Comando recebido: ");
        Serial.println(payload);

        JsonDocument doc;
        deserializeJson(doc, payload);

        if (!doc.isNull() && doc["type"]) {
            const char* type = doc["type"];
            if (strcmp(type, "config") == 0) {
                // Lógica de configuração (sem alterações)
                min_umidade[0] = doc["payload"]["min_umidade"][0];
                max_umidade[0] = doc["payload"]["max_umidade"][0];
                min_umidade[1] = doc["payload"]["min_umidade"][1];
                max_umidade[1] = doc["payload"]["max_umidade"][1];
                Serial.println("Configurações de umidade atualizadas!");

            } else if (strcmp(type, "manual_irrigate") == 0) {
                // --- LÓGICA MANUAL ATUALIZADA ---
                int bomba = doc["payload"];
                if (bomba == 1 || bomba == 2) {
                    int index = bomba - 1;
                    // Só inicia se não houver um ciclo manual já ativo para essa bomba
                    if (!manual_irrigation_active[index]) {
                        Serial.printf("Iniciando irrigacao manual para bomba %d por 3 segundos.\n", bomba);
                        manual_irrigation_active[index] = true;
                        manual_irrigation_start_time[index] = millis();
                        controlarBomba(bomba, true);
                        bomba_ligada[index] = true;
                    }
                }
            }
        }
    } else {
        Serial.printf("Erro ao verificar comandos. Código HTTP: %d\n", httpResponseCode);
    }
    http.end();
}

// --- NOVA FUNÇÃO PARA GERENCIAR O TEMPORIZADOR MANUAL ---
void handle_manual_irrigation() {
    for (int i = 0; i < 2; i++) {
        // Se a bomba 'i' está em modo manual...
        if (manual_irrigation_active[i]) {
            // E se já passaram 3 segundos...
            if (millis() - manual_irrigation_start_time[i] >= MANUAL_IRRIGATION_DURATION) {
                int bomba_num = i + 1;
                Serial.printf("Irrigacao manual para bomba %d finalizada.\n", bomba_num);
                controlarBomba(bomba_num, false); // Desliga a bomba
                bomba_ligada[i] = false;           // Sincroniza o estado
                manual_irrigation_active[i] = false; // Retorna ao modo automático
            }
        }
    }
}

// --- LÓGICA DE IRRIGAÇÃO AUTOMÁTICA ---
void logica_irrigacao() {
    int umidade_atual[] = {lerUmidadePercentual(PINO_UMIDADE_SOLO_1), lerUmidadePercentual(PINO_UMIDADE_SOLO_2)};
    for (int i = 0; i < 2; i++) {
        // SÓ executa a lógica automática se a bomba NÃO estiver em modo manual
        if (!manual_irrigation_active[i]) {
            int bomba_num = i + 1;
            // Se a umidade está abaixo do mínimo E a bomba está desligada...
            if (umidade_atual[i] < min_umidade[i] && !bomba_ligada[i]) {
                controlarBomba(bomba_num, true);
                bomba_ligada[i] = true;
            } 
            // Se a umidade está acima do máximo E a bomba está ligada...
            else if (umidade_atual[i] >= max_umidade[i] && bomba_ligada[i]) {
                controlarBomba(bomba_num, false);
                bomba_ligada[i] = false;
            }
        }
    }
}


void enviar_telemetria() {
    HTTPClient http;
    String serverPath = String(SERVER_URL) + "/post_data";
    http.begin(serverPath);
    http.addHeader("Content-Type", "application/json");

    JsonDocument doc;
    doc["temperatura"] = getTemperature();
    doc["umidadeAr"] = getAirHumidity();
    JsonArray umidadeSoloArray = doc["umidadeSolo"].to<JsonArray>();
    umidadeSoloArray.add(lerUmidadePercentual(PINO_UMIDADE_SOLO_1));
    umidadeSoloArray.add(lerUmidadePercentual(PINO_UMIDADE_SOLO_2));

    String output;
    serializeJson(doc, output);

    int httpResponseCode = http.POST(output);
    Serial.print("Enviando telemetria... Código de resposta HTTP: ");
    Serial.println(httpResponseCode);
    http.end();
}

void setup_wifi() {
    delay(10);
    Serial.println();
    Serial.print("Conectando a ");
    Serial.println(SSID);
    WiFi.begin(SSID, PASSWORD);
    while (WiFi.status() != WL_CONNECTED) {
        delay(500);
        Serial.print(".");
    }
    Serial.println("\nWiFi conectado!");
    Serial.println(WiFi.localIP());
}