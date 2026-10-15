// ============================================================
// ESP32 Energy Monitor - Implementacao da medicao
// Amostra V e I via ADC calibrado de fabrica (analogReadMilliVolts),
// calcula RMS e potencia real por janela de ~1s.
// Offset removido pela media da propria janela (1s = ciclos
// inteiros de 60Hz => media ~= offset DC exato).
// ============================================================
#include "energy.h"
#include <Preferences.h>
#include <math.h>
#include <time.h>
#include "config.h"

// ---------------- Estado ----------------
static float  gCalV = CAL_V_DEFAULT, gCalI = CAL_I_DEFAULT;
static uint8_t gPeakStart = PEAK_START_DEFAULT, gPeakEnd = PEAK_END_DEFAULT;
static double gWhT = 0, gWhP = 0, gWhO = 0;
static EnergyReading gRead = {};
static portMUX_TYPE gMux = portMUX_INITIALIZER_UNLOCKED;
static Preferences  gPrefs;
static uint32_t     gLastSaveMs = 0;

// ---------------- Hora local (tarifa) ----------------
bool energyLocalTime(struct tm &out) {
    time_t now = time(nullptr);
    if (now < 1600000000) return false;   // epoch absurdo = sem NTP/hora manual
    localtime_r(&now, &out);
    return true;
}

static bool inPeak(int h) {
    return (gPeakStart <= gPeakEnd)
        ? (h >= gPeakStart && h <  gPeakEnd)   // pico no mesmo dia
        : (h >= gPeakStart || h <  gPeakEnd);  // pico vira a meia-noite
}

// ---------------- Task de amostragem (core 0) ----------------
static void samplerTask(void *) {
    uint64_t sv = 0, si = 0, sv2 = 0, si2 = 0, svi = 0;
    uint32_t n = 0;
    uint32_t winStart = micros();

    for (;;) {
        // ~1000 pares de amostras por bloco (~250ms), depois cede o core
        // (menos de 5s => WDT do IDLE0 nao dispara)
        for (uint32_t k = 0; k < 1000; k++) {
            uint32_t mv = analogReadMilliVolts(PIN_VOLTAGE);
            uint32_t mi = analogReadMilliVolts(PIN_CURRENT);
            sv  += mv;
            si  += mi;
            sv2 += (uint64_t)mv * mv;
            si2 += (uint64_t)mi * mi;
            svi += (uint64_t)mv * mi;
            n++;
        }

        uint32_t elapsedUs = (uint32_t)(micros() - winStart);
        if (elapsedUs >= 1000000UL / 4) {   // fecha janela a cada ~1s
            double midV = (double)sv / n;
            double midI = (double)si / n;
            double varV = (double)sv2 / n - midV * midV;   // E[x^2]-E[x]^2
            double varI = (double)si2 / n - midI * midI;
            double crs  = (double)svi / n - midV * midI;   // potencia real
            if (varV < 0) varV = 0;
            if (varI < 0) varI = 0;

            float vRms = (float)sqrt(varV) * gCalV;
            float iRms = (float)sqrt(varI) * gCalI;
            float p    = (float)crs * gCalV * gCalI;
            if (p < 0) p = -p;   // sinal invertido = clamp no sentido errado
            float pf = (vRms > 1.0f && iRms > 0.001f)
                     ? constrain(p / (vRms * iRms), 0.0f, 1.0f) : 0.0f;

            // acumula energia com duracao real da janela
            double elapsedS = (double)elapsedUs / 1e6;
            double wh = (double)p * elapsedS / 3600.0;

            struct tm t;
            bool peak = energyLocalTime(t) ? inPeak(t.tm_hour) : false;
            gWhT += wh;
            if (peak) gWhP += wh; else gWhO += wh;

            EnergyReading r;
            r.vRms = vRms; r.iRms = iRms; r.power = p; r.pf = pf;
            r.vm = (float)midV; r.im = (float)midI;
            r.whTotal = gWhT; r.whPeak = gWhP; r.whOff = gWhO;
            r.peakNow = peak;

            portENTER_CRITICAL(&gMux);
            gRead = r;
            portEXIT_CRITICAL(&gMux);

            // reinicia janela
            sv = si = sv2 = si2 = svi = 0;
            n = 0;
            winStart = micros();
            vTaskDelay(10);   // ~10ms de folga: IDLE0 respira sem quebrar a janela
        }
    }
}

// ---------------- API publica ----------------
void energyBegin(float calV, float calI) {
    gCalV = calV; gCalI = calI;
    gPrefs.begin("energy", false);
    gWhT = gPrefs.getDouble("whT", 0);
    gWhP = gPrefs.getDouble("whP", 0);
    gWhO = gPrefs.getDouble("whO", 0);
    gPrefs.end();
    xTaskCreatePinnedToCore(samplerTask, "sampler", 8192, nullptr, 1, nullptr, 0);
}

void energySetCalibration(float calV, float calI) { gCalV = calV; gCalI = calI; }
void energySetTariff(uint8_t peakStart, uint8_t peakEnd) { gPeakStart = peakStart; gPeakEnd = peakEnd; }

EnergyReading energySnapshot() {
    EnergyReading r;
    portENTER_CRITICAL(&gMux);
    r = gRead;
    portEXIT_CRITICAL(&gMux);
    return r;
}

void energyReset() {
    gWhT = gWhP = gWhO = 0;
    gPrefs.begin("energy", false);
    gPrefs.putDouble("whT", 0.0);
    gPrefs.putDouble("whP", 0.0);
    gPrefs.putDouble("whO", 0.0);
    gPrefs.end();
}

void energyTick() {
    // persiste a cada 60s
    if (millis() - gLastSaveMs < 60000) return;
    gLastSaveMs = millis();
    gPrefs.begin("energy", false);
    gPrefs.putDouble("whT", gWhT);
    gPrefs.putDouble("whP", gWhP);
    gPrefs.putDouble("whO", gWhO);
    gPrefs.end();
}
