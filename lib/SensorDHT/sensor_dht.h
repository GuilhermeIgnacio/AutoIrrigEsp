#pragma once

#include <Arduino.h>

// Declara as funções que retornam os dados brutos
void setupSensorDHT();
float getTemperature();
float getAirHumidity();