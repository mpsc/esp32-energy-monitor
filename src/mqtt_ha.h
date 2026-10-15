// ============================================================
// ESP32 Energy Monitor - MQTT + Home Assistant (auto-discovery)
// Publica disponibilidade (LWT), 8 sensores via discovery
// retido no prefixo homeassistant/ e estado em JSON.
// ============================================================
#pragma once
#include <Arduino.h>
#include "energy.h"

struct MqttCfg {
    String  host;
    uint16_t port;
    String  user;
    String  pass;
    String  base;      // topico base (default: energia)
    bool    haDisc;    // enviar auto-discovery para o HA
};

void mqttBegin(const MqttCfg &cfg);
void mqttTick(const EnergyReading &r, float cost);   // chamar no loop()
bool mqttIsConnected();
