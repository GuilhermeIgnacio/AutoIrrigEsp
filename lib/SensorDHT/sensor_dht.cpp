#include "sensor_dht.h"
#include <DHT.h>

// --- Definições privadas do módulo ---
#define DHTPIN 23
#define DHTTYPE DHT11

DHT dht(DHTPIN, DHTTYPE);

void setupSensorDHT() {
  dht.begin();
}

float getTemperature() {
  float temperatura = dht.readTemperature();
  // Se a leitura falhar (isnan = "is not a number"), retorna um valor de erro
  if (isnan(temperatura)) {
    return -999.0;
  }
  return temperatura;
}

float getAirHumidity() {
  float umidade = dht.readHumidity();
  if (isnan(umidade)) {
    return -999.0;
  }
  return umidade;
}