// ============================================================
// ESP32 Energy Monitor - Implementacao MQTT + HA discovery
// ============================================================
#include "mqtt_ha.h"
#include <WiFi.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include "config.h"

static WiFiClient   gNet;
static PubSubClient gMqtt(gNet);
static MqttCfg      gCfg;
static uint32_t     gLastTry = 0, gLastPub = 0;
static String       gDeviceId;

// Topicos derivados
static String tAvail()  { return gCfg.base + "/availability"; }
static String tState()  { return gCfg.base + "/state"; }
static String tVal(const char *k) { return gCfg.base + "/" + k; }

// ---------------- Auto-discovery HA ----------------
// Envia uma mensagem retida em homeassistant/sensor/<base>/<key>/config
// O HA cria o automaticamente ao conectar no broker.
static void publishDiscovery(const char *key, const char *name,
                             const char *unit, const char *devClass,
                             const char *statClass) {
    JsonDocument d;
    d["name"]     = name;
    d["uniq_id"]  = "energia_" + String(key);
    d["stat_t"]   = tVal(key);
    d["avty_t"]   = tAvail();
    d["frc_upd"]  = true;          // atualiza mesmo sem mudanca de valor
    if (unit)     d["unit_of_meas"] = unit;
    if (devClass) d["dev_cla"]      = devClass;
    if (statClass)d["stat_cla"]     = statClass;

    JsonObject dev = d["dev"].to<JsonObject>();
    dev["ids"][0] = gDeviceId;
    dev["name"]   = "Monitor de Energia";
    dev["mf"]     = "DIY";
    dev["mdl"]    = "ESP32 + SCT-013";
    dev["sw"]     = FW_VERSION;
    dev["cu"]     = "http://" + WiFi.localIP().toString();

    String out;
    serializeJson(d, out);
    String topic = String(HA_DISCOVERY_PREFIX) + "/sensor/" + gCfg.base + "/" + key + "/config";
    gMqtt.publish(topic.c_str(), out.c_str(), true);
}

static void publishAllDiscovery() {
    publishDiscovery("power",        "Potencia",       "W",   "power",         "measurement");
    publishDiscovery("voltage",      "Tensao",         "V",   "voltage",       "measurement");
    publishDiscovery("current",      "Corrente",       "A",   "current",       "measurement");
    publishDiscovery("power_factor", "Fator de Potencia", "%", "power_factor",  "measurement");
    publishDiscovery("energy",       "Energia Total",  "kWh", "energy",        "total_increasing");
    publishDiscovery("energy_peak",  "Energia Hora Pico",  "kWh", "energy",    "total_increasing");
    publishDiscovery("energy_off",   "Energia Fora Pico",  "kWh", "energy",    "total_increasing");
    publishDiscovery("cost",         "Custo Estimado", "R$",  nullptr,         "measurement");
}

// ---------------- Estado ----------------
static void publishValues(const EnergyReading &r, float cost) {
    char b[24];
    dtostrf(r.power, 0, 1, b);  gMqtt.publish(tVal("power").c_str(), b, true);
    dtostrf(r.vRms,  0, 1, b);  gMqtt.publish(tVal("voltage").c_str(), b, true);
    dtostrf(r.iRms,  0, 3, b);  gMqtt.publish(tVal("current").c_str(), b, true);
    dtostrf(r.pf * 100, 0, 1, b); gMqtt.publish(tVal("power_factor").c_str(), b, true);
    dtostrf(r.whTotal / 1000.0, 0, 4, b); gMqtt.publish(tVal("energy").c_str(), b, true);
    dtostrf(r.whPeak  / 1000.0, 0, 4, b); gMqtt.publish(tVal("energy_peak").c_str(), b, true);
    dtostrf(r.whOff   / 1000.0, 0, 4, b); gMqtt.publish(tVal("energy_off").c_str(), b, true);
    dtostrf(cost, 0, 2, b);    gMqtt.publish(tVal("cost").c_str(), b, true);

    // JSON agregado (util para Node-RED / automacoes)
    JsonDocument d;
    d["p"]  = roundf(r.power * 10) / 10.0;
    d["v"]  = roundf(r.vRms * 10) / 10.0;
    d["i"]  = roundf(r.iRms * 1000) / 1000.0;
    d["pf"] = roundf(r.pf * 100) / 100.0;
    d["kt"] = roundf(r.whTotal * 10) / 10000.0;
    d["kp"] = roundf(r.whPeak * 10) / 10000.0;
    d["ko"] = roundf(r.whOff * 10) / 10000.0;
    d["cost"] = roundf(cost * 100) / 100.0;
    String out;
    serializeJson(d, out);
    gMqtt.publish(tState().c_str(), out.c_str(), false);
}

// ---------------- Ciclo de vida ----------------
void mqttBegin(const MqttCfg &cfg) {
    gCfg = cfg;
    gDeviceId = "energia-" + String((uint32_t)ESP.getEfuseMac(), HEX);
    gMqtt.setClient(gNet);
    gMqtt.setServer(gCfg.host.c_str(), gCfg.port);
    gMqtt.setBufferSize(1536);   // payloads de discovery maiores que o padrao 256
}

bool mqttIsConnected() { return gMqtt.connected(); }

void mqttTick(const EnergyReading &r, float cost) {
    if (gCfg.host.length() == 0) return;   // MQTT nao configurado

    if (!gMqtt.connected()) {
        if (millis() - gLastTry < 5000) return;   // backoff de 5s
        gLastTry = millis();
        Serial.printf("[MQTT] Conectando a %s:%u...\n", gCfg.host.c_str(), gCfg.port);
        const char *user = gCfg.user.length() ? gCfg.user.c_str() : nullptr;
        const char *pass = gCfg.pass.length() ? gCfg.pass.c_str() : nullptr;
        // LWT: broker marca offline se o ESP32 cair
        if (gMqtt.connect(gDeviceId.c_str(), user, pass,
                          tAvail().c_str(), 1, true, "offline")) {
            Serial.println("[MQTT] Conectado.");
            gMqtt.publish(tAvail().c_str(), "online", true);
            if (gCfg.haDisc) publishAllDiscovery();
            publishValues(r, cost);
        } else {
            Serial.printf("[MQTT] Falha rc=%d\n", gMqtt.state());
        }
        return;
    }

    gMqtt.loop();
    if (millis() - gLastPub >= 5000) {
        gLastPub = millis();
        publishValues(r, cost);
    }
}
