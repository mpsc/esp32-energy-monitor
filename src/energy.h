// ============================================================
// ESP32 Energy Monitor - Modulo de medicao (task no core 0)
// RMS de tensao/corrente, potencia real, fator de potencia,
// acumulacao de energia (Wh) com divisao por tarifa
// ============================================================
#pragma once
#include <Arduino.h>

struct EnergyReading {
    float  vRms;       // V
    float  iRms;       // A
    float  power;      // W (potencia real)
    float  pf;         // fator de potencia 0..1
    float  vm;         // tensao media (offset) em mV - ajuda na calibracao
    float  im;         // corrente media (offset) em mV
    double whTotal;    // energia acumulada total (Wh)
    double whPeak;     // Wh no horario de pico
    double whOff;      // Wh fora do horario de pico
    bool   peakNow;    // se agora esta no horario de pico
};

void        energyBegin(float calV, float calI);
void        energySetCalibration(float calV, float calI);
void        energySetTariff(uint8_t peakStart, uint8_t peakEnd);
EnergyReading energySnapshot();
void        energyReset();      // zera contadores de energia
void        energyTick();       // chamado no loop(): persistencia na NVS
bool        energyLocalTime(struct tm &out);  // hora local valida?
