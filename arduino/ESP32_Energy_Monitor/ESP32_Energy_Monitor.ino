// ============================================================
// ESP32 Energy Monitor - Sketch para ARDUINO IDE
//
// ⚠️ ESTE ARQUIVO E UM ESPELHO de src/main.cpp (projeto PlatformIO).
// A fonte de verdade do repo e o projeto PlatformIO (tem CI compilando).
// Apos editar o firmware no PlatformIO, regenere o espelho com:
//     python tools/sync_arduino.py   (copia src/ e include/ para esta pasta)
//     python tools/gen_web_page.py   (regenera o web_page.h nos 2 lugares)
//
// Bibliotecas (Library Manager): "ArduinoJson" (Benoit Blanchon, v7),
//                                "PubSubClient" (Nick O'Leary, v2.8)
// Bibliotecas por ZIP (fora do Library Manager):
//    https://github.com/me-no-dev/AsyncTCP/archive/refs/heads/master.zip
//    https://github.com/me-no-dev/ESPAsyncWebServer/archive/refs/heads/master.zip
// Placa: "ESP32 Dev Module" (Gerenciador de Placas -> ESP32 by Espressif)
// ============================================================
#include <Arduino.h>
#include <WiFi.h>
#include <ESPmDNS.h>
#include <ArduinoOTA.h>
#include <DNSServer.h>
#include <ESPAsyncWebServer.h>
#include <ArduinoJson.h>
#include <Preferences.h>
#include <time.h>
#include <sys/time.h>
#include "config.h"
#include "web_page.h"
#include "energy.h"
#include "mqtt_ha.h"

// ---------------- Configuracao (NVS) ----------------
struct Cfg {
    String  staSsid, staPass;
    String  mqttHost, mqttUser, mqttPass, mqttBase, tz;
    String  otaPass;                 // senha do upload OTA (vazio = sem senha)
    uint16_t mqttPort;
    bool    haDisc;
    float   calV, calI;
    float   pricePeak, priceOff;
    uint8_t peakStart, peakEnd;
};
static Cfg       cfg;
static Preferences prefs;

void loadCfg() {
    prefs.begin("cfg", true);
    cfg.staSsid   = prefs.getString("ssid", "");
    cfg.staPass   = prefs.getString("spass", "");
    cfg.mqttHost  = prefs.getString("mhost", MQTT_HOST_DEFAULT);
    cfg.mqttPort  = prefs.getUShort("mport", MQTT_PORT_DEFAULT);
    cfg.mqttUser  = prefs.getString("muser", MQTT_USER_DEFAULT);
    cfg.mqttPass  = prefs.getString("mpass", MQTT_PASS_DEFAULT);
    cfg.mqttBase  = prefs.getString("mbase", MQTT_BASE_DEFAULT);
    cfg.haDisc    = prefs.getBool("hdisc", true);
    cfg.calV      = prefs.getFloat("calV", CAL_V_DEFAULT);
    cfg.calI      = prefs.getFloat("calI", CAL_I_DEFAULT);
    cfg.pricePeak = prefs.getFloat("pPeak", PRICE_PEAK_DEFAULT);
    cfg.priceOff  = prefs.getFloat("pOff",  PRICE_OFF_DEFAULT);
    cfg.peakStart = prefs.getUChar("pkS", PEAK_START_DEFAULT);
    cfg.peakEnd   = prefs.getUChar("pkE", PEAK_END_DEFAULT);
    cfg.tz        = prefs.getString("tz", TZ_DEFAULT);
    cfg.otaPass   = prefs.getString("otaPass", OTA_PASSWORD_DEFAULT);
    prefs.end();
}

void saveCfg() {
    prefs.begin("cfg", false);
    prefs.putString("ssid", cfg.staSsid);
    prefs.putString("spass", cfg.staPass);
    prefs.putString("mhost", cfg.mqttHost);
    prefs.putUShort("mport", cfg.mqttPort);
    prefs.putString("muser", cfg.mqttUser);
    prefs.putString("mpass", cfg.mqttPass);
    prefs.putString("mbase", cfg.mqttBase);
    prefs.putBool("hdisc", cfg.haDisc);
    prefs.putFloat("calV", cfg.calV);
    prefs.putFloat("calI", cfg.calI);
    prefs.putFloat("pPeak", cfg.pricePeak);
    prefs.putFloat("pOff", cfg.priceOff);
    prefs.putUChar("pkS", cfg.peakStart);
    prefs.putUChar("pkE", cfg.peakEnd);
    prefs.putString("tz", cfg.tz);
    prefs.putString("otaPass", cfg.otaPass);
    prefs.end();
}

// ---------------- Servidor ----------------
static AsyncWebServer server(80);
static AsyncWebSocket ws("/ws");
static DNSServer      dns;
static bool           rebootPending = false;
static uint32_t       rebootAt = 0;

float totalCost(const EnergyReading &r) {
    return (float)(r.whPeak / 1000.0) * cfg.pricePeak
         + (float)(r.whOff  / 1000.0) * cfg.priceOff;
}

// ---------------- Historico (buffer circular em RAM) ----------------
// Guarda HIST_MAX amostras (uma a cada HIST_PERIOD_MS) para exportacao CSV.
// RAM-only: zera no reboot. Acesso ao indice protegido por secao critica
// (o handler HTTP roda na task do AsyncWebServer, o loop em outra).
struct HistSample {
    uint32_t t;         // epoch (se hasClock) ou uptime em segundos
    float    p, v, i, pf, cost;
    double   kwh;
    uint8_t  peak;
    uint8_t  hasClock;
};
static HistSample  hist[HIST_MAX];
static uint16_t    histCount  = 0;   // amostras validas (<= HIST_MAX)
static uint16_t    histHead   = 0;   // proxima posicao de escrita
static uint32_t    histLastMs = 0;
static portMUX_TYPE histMux = portMUX_INITIALIZER_UNLOCKED;

void histPush() {
    EnergyReading r = energySnapshot();
    time_t now = time(nullptr);
    bool clock = now > 1600000000;

    HistSample h;
    h.t        = clock ? (uint32_t)now : (uint32_t)(millis() / 1000);
    h.hasClock = clock ? 1 : 0;
    h.p = r.power; h.v = r.vRms; h.i = r.iRms; h.pf = r.pf;
    h.kwh  = r.whTotal / 1000.0;
    h.cost = totalCost(r);
    h.peak = r.peakNow ? 1 : 0;

    portENTER_CRITICAL(&histMux);
    hist[histHead] = h;
    histHead = (histHead + 1) % HIST_MAX;
    if (histCount < HIST_MAX) histCount++;
    portEXIT_CRITICAL(&histMux);
}

String histCsv() {
    uint16_t count, head;
    portENTER_CRITICAL(&histMux);
    count = histCount; head = histHead;
    portEXIT_CRITICAL(&histMux);

    String out;
    out.reserve(64 + (size_t)count * 72);
    out += "timestamp,p_w,v_v,i_a,pf,energy_kwh,cost_brl,peak\n";

    uint16_t first = (uint16_t)((head + HIST_MAX - count) % HIST_MAX);
    for (uint16_t k = 0; k < count; k++) {
        const HistSample &h = hist[(first + k) % HIST_MAX];
        char ts[32];
        if (h.hasClock) {
            time_t e = (time_t)h.t;
            struct tm t;
            localtime_r(&e, &t);
            strftime(ts, sizeof(ts), "%Y-%m-%d %H:%M:%S", &t);
        } else {
            snprintf(ts, sizeof(ts), "t+%lus", (unsigned long)h.t);
        }
        char line[112];
        snprintf(line, sizeof(line), "%s,%.1f,%.1f,%.3f,%.2f,%.5f,%.2f,%u\n",
                 ts, h.p, h.v, h.i, h.pf, h.kwh, h.cost, (unsigned)h.peak);
        out += line;
    }
    return out;
}

// ---------------- OTA (ArduinoOTA) ----------------
void otaSetup() {
    ArduinoOTA.setHostname(HOSTNAME);
    ArduinoOTA.setPort(OTA_PORT);
    if (cfg.otaPass.length()) ArduinoOTA.setPassword(cfg.otaPass.c_str());

    ArduinoOTA.onStart([]() {
        const char *tipo = (ArduinoOTA.getCommand() == U_FLASH) ? "firmware" : "filesystem";
        Serial.printf("\n[OTA] Iniciando gravacao (%s). NAO desligue!\n", tipo);
    });
    ArduinoOTA.onEnd([]() { Serial.println("\n[OTA] Concluido. Reiniciando..."); });
    ArduinoOTA.onProgress([](unsigned int p, unsigned int total) {
        Serial.printf("[OTA] %3u%%\r", total ? (p * 100 / total) : 0);
    });
    ArduinoOTA.onError([](ota_error_t e) {
        Serial.printf("[OTA] Erro[%u]: ", (unsigned)e);
        if (e == OTA_AUTH_ERROR)         Serial.println("senha incorreta");
        else if (e == OTA_BEGIN_ERROR)   Serial.println("falha ao iniciar");
        else if (e == OTA_CONNECT_ERROR) Serial.println("falha de conexao");
        else if (e == OTA_RECEIVE_ERROR) Serial.println("falha de recepcao");
        else if (e == OTA_END_ERROR)     Serial.println("falha ao finalizar");
        else                             Serial.println("desconhecido");
    });
    ArduinoOTA.begin();
    Serial.printf("[OTA] Pronto em %s.local:%d (IP %s)%s\n",
                  HOSTNAME, OTA_PORT, WiFi.localIP().toString().c_str(),
                  cfg.otaPass.length() ? " [com senha]" : " [SEM senha]");
}

// JSON enviado para a pagina (WebSocket e /api/state)
String stateJson() {
    EnergyReading r = energySnapshot();
    JsonDocument d;
    d["p"]  = roundf(r.power * 10) / 10.0;
    d["v"]  = roundf(r.vRms * 10) / 10.0;
    d["i"]  = roundf(r.iRms * 1000) / 1000.0;
    d["pf"] = roundf(r.pf * 100) / 100.0;
    d["kt"] = roundf(r.whTotal * 10) / 10000.0;   // kWh
    d["kp"] = roundf(r.whPeak * 10) / 10000.0;
    d["ko"] = roundf(r.whOff * 10) / 10000.0;
    d["cost"] = roundf(totalCost(r) * 100) / 100.0;
    d["peak"] = r.peakNow;
    d["vm"] = (int)r.vm;   // offsets em mV (calibracao ao vivo)
    d["im"] = (int)r.im;
    d["up"] = millis() / 1000;
    d["hn"] = histCount;   // amostras no historico (export CSV)

    char hh[8] = "--:--";
    struct tm t;
    if (energyLocalTime(t)) snprintf(hh, sizeof(hh), "%02d:%02d", t.tm_hour, t.tm_min);
    d["t"] = hh;
    d["ntp"] = time(nullptr) > 1600000000;

    String out;
    serializeJson(d, out);
    return out;
}

MqttCfg cfgToMqtt() {
    MqttCfg mc;
    mc.host = cfg.mqttHost; mc.port = cfg.mqttPort;
    mc.user = cfg.mqttUser; mc.pass = cfg.mqttPass;
    mc.base = cfg.mqttBase; mc.haDisc = cfg.haDisc;
    return mc;
}

// ---------------- Setup ----------------
void setup() {
    Serial.begin(115200);
    Serial.printf("\n\n=== ESP32 Energy Monitor v%s ===\n", FW_VERSION);

    loadCfg();

    // Fuso horario primeiro: vale para NTP e para hora manual
    setenv("TZ", cfg.tz.c_str(), 1);
    tzset();

    energyBegin(cfg.calV, cfg.calI);
    energySetTariff(cfg.peakStart, cfg.peakEnd);

    // Wi-Fi: AP sempre ativo (portal cativo) + STA opcional
    WiFi.setHostname(HOSTNAME);
    WiFi.mode(WIFI_AP_STA);
    WiFi.softAP(AP_SSID, AP_PASS);
    Serial.printf("[WiFi] AP '%s' no IP %s\n", AP_SSID, WiFi.softAPIP().toString().c_str());

    if (cfg.staSsid.length()) {
        Serial.printf("[WiFi] Conectando a '%s'...\n", cfg.staSsid.c_str());
        WiFi.begin(cfg.staSsid.c_str(), cfg.staPass.c_str());
        uint32_t t0 = millis();
        while (WiFi.status() != WL_CONNECTED && millis() - t0 < 15000) delay(250);
        if (WiFi.status() == WL_CONNECTED) {
            Serial.printf("[WiFi] STA conectado: %s (mDNS: %s.local)\n",
                          WiFi.localIP().toString().c_str(), HOSTNAME);
            configTime(0, 0, NTP1, NTP2);   // NTP para tarifa horaria
            if (MDNS.begin(HOSTNAME)) MDNS.addService("http", "tcp", 80);
        } else {
            Serial.println("[WiFi] STA falhou - seguindo so no AP.");
        }
    }

    // OTA (ArduinoOTA): mantem o caminho de atualizacao vivo apos o boot
    otaSetup();

    // Portal cativo: DNS redireciona TUDO para o IP do AP
    dns.start(53, "*", WiFi.softAPIP());

    // Rota principal: dashboard embutido em PROGMEM
    server.on("/", HTTP_GET, [](AsyncWebServerRequest *req) {
        req->send_P(200, "text/html", PAGE_HTML);
    });
    server.on("/favicon.ico", HTTP_GET, [](AsyncWebServerRequest *req) {
        req->send(204);
    });

    // Estado atual (fallback para quem nao usa WebSocket)
    server.on("/api/state", HTTP_GET, [](AsyncWebServerRequest *req) {
        req->send(200, "application/json", stateJson());
    });

    // Exportacao do historico em CSV (download direto do navegador)
    server.on("/api/history.csv", HTTP_GET, [](AsyncWebServerRequest *req) {
        AsyncWebServerResponse *resp = req->beginResponse(200, "text/csv", histCsv());
        resp->addHeader("Content-Disposition", "attachment; filename=\"energia.csv\"");
        resp->addHeader("Cache-Control", "no-store");
        req->send(resp);
    });

    // Config atual (sem senhas) para preencher o formulario
    server.on("/api/cfg", HTTP_GET, [](AsyncWebServerRequest *req) {
        JsonDocument d;
        d["staSsid"]   = cfg.staSsid;
        d["mqttHost"]  = cfg.mqttHost;
        d["mqttPort"]  = cfg.mqttPort;
        d["mqttUser"]  = cfg.mqttUser;
        d["mqttBase"]  = cfg.mqttBase;
        d["haDisc"]    = cfg.haDisc;
        d["calV"]      = cfg.calV;
        d["calI"]      = cfg.calI;
        d["pricePeak"] = cfg.pricePeak;
        d["priceOff"]  = cfg.priceOff;
        d["peakStart"] = cfg.peakStart;
        d["peakEnd"]   = cfg.peakEnd;
        d["tz"]        = cfg.tz;
        String out;
        serializeJson(d, out);
        req->send(200, "application/json", out);
    });

    // Salva configuracao (Wi-Fi, MQTT, tarifa, calibracao)
    server.on("/cfg", HTTP_POST, [](AsyncWebServerRequest *req) {
        if (req->hasParam("staSsid", true))   cfg.staSsid   = req->getParam("staSsid", true)->value();
        if (req->hasParam("staPass", true)) { String p = req->getParam("staPass", true)->value();
                                              if (p.length()) cfg.staPass = p; }   // branco = mantem
        if (req->hasParam("mqttHost", true)) cfg.mqttHost  = req->getParam("mqttHost", true)->value();
        if (req->hasParam("mqttPort", true)) cfg.mqttPort  = constrain(req->getParam("mqttPort", true)->value().toInt(), 1, 65535);
        if (req->hasParam("mqttUser", true)) cfg.mqttUser  = req->getParam("mqttUser", true)->value();
        if (req->hasParam("mqttPass", true)) { String p = req->getParam("mqttPass", true)->value();
                                               if (p.length()) cfg.mqttPass = p; }
        if (req->hasParam("mqttBase", true)) { String b = req->getParam("mqttBase", true)->value();
                                               cfg.mqttBase = b.length() ? b : MQTT_BASE_DEFAULT; }
        cfg.haDisc = req->hasParam("haDisc", true);
        if (req->hasParam("calV", true))     cfg.calV      = req->getParam("calV", true)->value().toFloat();
        if (req->hasParam("calI", true))     cfg.calI      = req->getParam("calI", true)->value().toFloat();
        if (req->hasParam("pricePeak", true))cfg.pricePeak = req->getParam("pricePeak", true)->value().toFloat();
        if (req->hasParam("priceOff", true)) cfg.priceOff  = req->getParam("priceOff", true)->value().toFloat();
        if (req->hasParam("peakStart", true))cfg.peakStart = constrain(req->getParam("peakStart", true)->value().toInt(), 0, 23);
        if (req->hasParam("peakEnd", true))  cfg.peakEnd   = constrain(req->getParam("peakEnd", true)->value().toInt(), 0, 23);
        if (req->hasParam("tz", true)) { String z = req->getParam("tz", true)->value();
                                         if (z.length()) cfg.tz = z; }
        if (req->hasParam("otaPass", true)) { String p = req->getParam("otaPass", true)->value();
                                              if (p.length()) cfg.otaPass = p; }   // branco = mantem
        saveCfg();
        energySetCalibration(cfg.calV, cfg.calI);
        energySetTariff(cfg.peakStart, cfg.peakEnd);
        setenv("TZ", cfg.tz.c_str(), 1);
        tzset();
        mqttBegin(cfgToMqtt());
        req->send_P(200, "text/html",
            "<meta charset='utf-8'><body style='background:#0a0a12;color:#0f8;"
            "font-family:sans-serif;display:flex;align-items:center;justify-content:center;"
            "height:100vh'><h2>Salvo! Reiniciando...</h2></body>");
        rebootPending = true;
        rebootAt = millis() + 2000;
    });

    // Zera contadores de energia
    server.on("/api/reset-energy", HTTP_POST, [](AsyncWebServerRequest *req) {
        energyReset();
        req->send(200, "application/json", "{\"ok\":true}");
    });

    // Define hora manualmente (para tarifa sem NTP/internet)
    server.on("/api/time", HTTP_POST, [](AsyncWebServerRequest *req) {
        if (req->hasParam("epoch", true)) {
            time_t e = (time_t)req->getParam("epoch", true)->value().toInt();
            struct timeval tv = { e, 0 };
            settimeofday(&tv, nullptr);
        }
        req->send(200, "application/json", "{\"ok\":true}");
    });

    // Portal cativo: qualquer URL desconhecida redireciona ao dashboard
    server.onNotFound([](AsyncWebServerRequest *req) {
        req->redirect("http://" + WiFi.softAPIP().toString() + "/");
    });

    ws.onEvent([](AsyncWebSocket *, AsyncWebSocketClient *client, AwsEventType type,
                  void *, uint8_t *, size_t) {
        if (type == WS_EVT_CONNECT) client->text(stateJson());
    });
    server.addHandler(&ws);
    server.begin();

    mqttBegin(cfgToMqtt());

    Serial.println("[HTTP] Dashboard em http://192.168.4.1/ (portal cativo ativo)");
}

// ---------------- Loop principal (core 1) ----------------
void loop() {
    dns.processNextRequest();
    ArduinoOTA.handle();   // atende uploads OTA (core 1)
    energyTick();

    EnergyReading r = energySnapshot();
    mqttTick(r, totalCost(r));

    // Amostra do historico (export CSV)
    if (millis() - histLastMs >= HIST_PERIOD_MS) {
        histLastMs = millis();
        histPush();
    }

    // Envia estado para as paginas abertas a cada 2s
    static uint32_t lastWs = 0;
    if (millis() - lastWs >= 2000) {
        lastWs = millis();
        if (ws.count() > 0) ws.textAll(stateJson());
    }

    if (rebootPending && millis() >= rebootAt) ESP.restart();
}
