#include "controle_bomba.h"
#include <Arduino.h>

// --- Lógica de acionamento (active-low) ---
const int ESTADO_LIGADO = LOW;
const int ESTADO_DESLIGADO = HIGH;

// --- Variáveis privadas do módulo para guardar os pinos ---
static int pinoBomba1_priv;
static int pinoBomba2_priv;

void setupBombas(int pinoBomba1, int pinoBomba2) {
  // Guarda os pinos nas variáveis privadas
  pinoBomba1_priv = pinoBomba1;
  pinoBomba2_priv = pinoBomba2;

  // Configura os pinos como saída
  pinMode(pinoBomba1_priv, OUTPUT);
  pinMode(pinoBomba2_priv, OUTPUT);

  // Garante que ambas as bombas comecem desligadas
  digitalWrite(pinoBomba1_priv, ESTADO_DESLIGADO);
  digitalWrite(pinoBomba2_priv, ESTADO_DESLIGADO);
}

// Função para ligar ou desligar uma bomba específica
// numeroBomba: 1 ou 2
// ligar: true para ligar, false para desligar
void controlarBomba(int numeroBomba, bool ligar) {
  int pino = 0;
  int estado = ligar ? ESTADO_LIGADO : ESTADO_DESLIGADO;

  if (numeroBomba == 1) {
    pino = pinoBomba1_priv;
  } else if (numeroBomba == 2) {
    pino = pinoBomba2_priv;
  } else {
    return; // Se o número da bomba for inválido, não faz nada
  }

  digitalWrite(pino, estado);
  
  // Feedback visual no Monitor Serial
  Serial.print("Bomba ");
  Serial.print(numeroBomba);
  Serial.println(ligar ? " LIGADA" : " DESLIGADA");
}