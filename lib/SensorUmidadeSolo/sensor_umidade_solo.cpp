#include "sensor_umidade_solo.h"
#include <Arduino.h>

const int VALOR_SENSOR_SECO = 2850;   // Exemplo seco
const int VALOR_SENSOR_MOLHADO = 1250; // Exemplo molhado


void setupSensorUmidadeSolo(int pino) {
  // Configura o pino como entrada
  pinMode(pino, INPUT);
}

int lerUmidadePercentual(int pino) {
  // Lê o valor analógico bruto do sensor
  int valorRaw = analogRead(pino);

  // Converte (mapeia) a faixa de leitura do sensor (de seco para molhado)
  // para uma porcentagem (de 0 a 100).
  // Note que VALOR_SENSOR_SECO vem primeiro, pois corresponde a 0% de umidade.
  int percentual = map(valorRaw, VALOR_SENSOR_SECO, VALOR_SENSOR_MOLHADO, 0, 100);

  // Garante que o resultado fique sempre entre 0 e 100, mesmo que a leitura
  // saia um pouco da faixa calibrada.
  percentual = constrain(percentual, 0, 100);

  return percentual;
}