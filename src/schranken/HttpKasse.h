#pragma once
// Echte Verbindung zur Kasse: HTTP-Anfragen über das WLAN an Pico 1
// (Protokoll: docs/HTTP-Protokoll.md). Fehlgeschlagene Anfragen werden bis zu
// cfg::KASSE_HTTP_ATTEMPTS-mal mit derselben Request-ID wiederholt.
//
// Merkt sich zusätzlich für die Anzeige, ob die Kasse erreichbar ist und
// wie viele Plätze sie zuletzt als frei gemeldet hat.

#include <Arduino.h>
#include <HTTPClient.h>
#include <WiFi.h>
#include <stdlib.h>

#include "Config.h"
#include "Kasse.h"
#include "Protocol.h"

class HttpKasse : public park::Kasse {
public:
    bool requestEntry(const char* plate, const char* rid, park::KasseReply& reply) override {
        return post(proto::PATH_ENTRY, plate, false, rid, reply);
    }
    bool requestManualEntry(const char* rid, park::KasseReply& reply) override {
        return post(proto::PATH_ENTRY, nullptr, false, rid, reply);
    }
    bool checkExit(const char* id, bool manual, const char* rid, park::KasseReply& reply) override {
        return post(proto::PATH_EXIT_CHECK, id, manual, rid, reply);
    }
    bool confirmExit(const char* id, bool manual, const char* rid) override {
        park::KasseReply reply;
        return post(proto::PATH_EXIT_CONFIRM, id, manual, rid, reply);
    }
    bool abortExit(const char* id, bool manual, const char* rid) override {
        park::KasseReply reply;
        return post(proto::PATH_EXIT_ABORT, id, manual, rid, reply);
    }

    // Belegung abfragen (nur Anzeige). Nur ein Versuch: die Abfrage
    // wiederholt sich ohnehin alle paar Sekunden.
    void pollStatus() {
        char resp[128];
        if (!call(proto::PATH_STATUS, nullptr, resp, sizeof(resp), 1)) return;
        char value[12];
        if (proto::getField(resp, proto::F_CAPACITY, value, sizeof(value))) {
            capacity_ = static_cast<unsigned>(atoi(value));
        }
    }

    bool online() const { return online_; }
    void setOffline() { online_ = false; }
    bool freeKnown() const { return freeKnown_; }
    unsigned freeSpaces() const { return freeSpaces_; }
    unsigned capacity() const { return capacity_; }
    // Zeitpunkt der letzten gültigen Antwort (für die WLAN-Überwachung).
    uint32_t lastOkMs() const { return lastOkMs_; }
    void resetLastOk(uint32_t nowMs) { lastOkMs_ = nowMs; }

private:
    bool post(const char* path, const char* plate, bool manual, const char* rid, park::KasseReply& reply) {
        char body[220];
        char resp[160];
        proto::formBody(body, sizeof(body), plate, manual, rid);
        if (!call(path, body, resp, sizeof(resp), cfg::KASSE_HTTP_ATTEMPTS)) return false;
        return proto::parseReply(resp, reply);
    }

    // Ein einzelner HTTP-Versuch. `code` < 0 = Verbindungsfehler/Timeout.
    bool callOnce(const char* path, const char* formBody, char* resp, size_t respLen, int& code) {
        char url[64];
        snprintf(url, sizeof(url), "http://%u.%u.%u.%u%s", cfg::KASSE_API_IP[0], cfg::KASSE_API_IP[1],
                 cfg::KASSE_API_IP[2], cfg::KASSE_API_IP[3], path);

        WiFiClient client;
        HTTPClient http;
        http.setTimeout(cfg::KASSE_HTTP_TIMEOUT_MS);
        http.setReuse(false);
        code = -1;
        if (!http.begin(client, url)) return false;

        if (formBody != nullptr) {
            http.addHeader("Content-Type", "application/x-www-form-urlencoded");
            code = http.POST(reinterpret_cast<uint8_t*>(const_cast<char*>(formBody)), strlen(formBody));
        } else {
            code = http.GET();
        }

        bool ok = false;
        if (code == 200) {
            const String text = http.getString();
            strncpy(resp, text.c_str(), respLen - 1);
            resp[respLen - 1] = '\0';
            // Eine Antwort ohne result-Feld gilt als abgebrochen.
            char result[16];
            ok = proto::getField(resp, proto::F_RESULT, result, sizeof(result));
        }
        http.end();
        return ok;
    }

    // Liefert false, wenn keine gültige Antwort kam – die Schranke bleibt dann zu.
    bool call(const char* path, const char* formBody, char* resp, size_t respLen, uint8_t attempts) {
        resp[0] = '\0';
        int code = -1;
        for (uint8_t i = 0; i < attempts; ++i) {
            if (WiFi.status() != WL_CONNECTED) break;
            if (callOnce(path, formBody, resp, respLen, code)) {
                online_ = true;
                lastOkMs_ = millis();
                char value[12];
                if (proto::getField(resp, proto::F_FREE, value, sizeof(value))) {
                    freeSpaces_ = static_cast<unsigned>(atoi(value));
                    freeKnown_ = true;
                }
                if (i > 0) Serial.printf("[Schranken] %s: Erfolg im %u. Versuch\n", path, i + 1);
                return true;
            }
            Serial.printf("[Schranken] %s: Versuch %u fehlgeschlagen (Code %d)\n", path, i + 1, code);
            if (code >= 400) break;  // fachlich/formal abgelehnt – Wiederholen hilft nicht
            if (i + 1 < attempts) delay(cfg::KASSE_HTTP_RETRY_DELAY_MS);
        }
        online_ = false;
        return false;
    }

    bool online_ = false;
    bool freeKnown_ = false;
    unsigned freeSpaces_ = 0;
    unsigned capacity_ = cfg::CAPACITY;
    uint32_t lastOkMs_ = 0;
};
