// ============================================================
// ESP32 Energy Monitor - Configuracao global
// Pinos, constantes e padroes editaveis
// ============================================================
#pragma once

#define FW_VERSION "1.1.0"

// ---------------- PINOS (APENAS ADC1: GPIO32-39) ----------------
// GPIO34/35 sao input-only (nao possuem pull-up interno)
#define PIN_VOLTAGE 34   // ZMPT101B  - tensao da rede (via divisor/meio-tensao)
#define PIN_CURRENT 35   // SCT-013-030 - corrente (com bias de meio-tensao)

// ---------------- AMOSTRAGEM ----------------
#define SAMPLE_RATE_HZ 4000   // ~4kHz por canal (janela de 1s = 60 ciclos exatos em 60Hz)

// ---------------- WI-FI ----------------
#define AP_SSID   "Energia-Monitor"
#define AP_PASS   "12345678"      // minimo 8 caracteres
#define HOSTNAME  "energia"       // mDNS: http://energia.local

// ---------------- MQTT ----------------
#define MQTT_HOST_DEFAULT ""          // vazio = MQTT desativado
#define MQTT_PORT_DEFAULT 1883
#define MQTT_BASE_DEFAULT "energia"
#define MQTT_USER_DEFAULT ""
#define MQTT_PASS_DEFAULT ""
#define HA_DISCOVERY_PREFIX "homeassistant"

// ---------------- TEMPO (tarifa precisa de hora valida) ----------------
#define NTP1 "pool.ntp.org"
#define NTP2 "a.ntp.br"
#define TZ_DEFAULT "America/Sao_Paulo"

// ---------------- PADROES DE CALIBRACAO ----------------
// calV/calI sao fatores multiplicativos: cal = valor_real / valor_medido
#define CAL_V_DEFAULT 1.0f
#define CAL_I_DEFAULT 1.0f

// ---------------- PADROES DE TARIFA (R$/kWh) ----------------
#define PRICE_OFF_DEFAULT  0.85f   // fora do horario de pico
#define PRICE_PEAK_DEFAULT 1.10f   // horario de pico
#define PEAK_START_DEFAULT 18      // 18h
#define PEAK_END_DEFAULT   21      // ate 21h (suporta virada de noite)

// ---------------- OTA (ArduinoOTA) ----------------
// Senha pedida pelo upload OTA. Vazio = sem senha (NAO recomendado em rede
// compartilhada). Pode ser trocada pela pagina web (persistida na NVS).
#define OTA_PASSWORD_DEFAULT ""    // ex: "minhaSenhaForte"
#define OTA_PORT             3232  // porta UDP padrao do ArduinoOTA

// ---------------- HISTORICO (export CSV) ----------------
// Buffer circular em RAM: 240 amostras a cada 30s = 2h de historico.
// (RAM-only: zera no reboot. Ver README > "Exportacao CSV".)
#define HIST_MAX        240
#define HIST_PERIOD_MS  30000UL
