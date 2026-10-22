// =============================================================================
//  Pico 2 – "Schranken"
//
//  - verbindet sich mit dem WLAN der Kasse (statische IP 192.168.42.2)
//  - steuert Einfahrts- (GP8) und Ausfahrtsschranke (GP9) über SG90-Servos;
//    Onboard-LED leuchtet, solange eine Schranke offen ist
//  - Weboberfläche "Kamera-Simulation": http://192.168.42.2/
//  - jede Entscheidung trifft die Kasse (Pico 1). Ist sie nicht erreichbar,
//    bleiben beide Schranken geschlossen (Fail-safe).
// =============================================================================

#include <Arduino.h>
#include <HTTPClient.h>
#include <WebServer.h>
#include <WiFi.h>

#include "Config.h"
#include "EventLog.h"
#include "Format.h"
#include "Gate.h"
#include "Protocol.h"
#include "WebOut.h"
#include "WebStyle.h"

namespace {

WebServer server(cfg::HTTP_PORT);
Gate entryGate("Einfahrt", cfg::SERVO_ENTRY_PIN);
Gate exitGate("Ausfahrt", cfg::SERVO_EXIT_PIN);

// Zuletzt von der Kasse gemeldete Belegung (nur Anzeige).
bool freeKnown = false;
unsigned freeSpaces = 0;
unsigned capacity = cfg::CAPACITY;
bool kasseOnline = false;

// Bestätigte Ausfahrt, die die Kasse noch nicht quittiert hat (wird wiederholt).
char pendingExit[cfg::ID_BUF_LEN] = "";
bool pendingExitManual = false;

uint32_t lastStatusPollMs = 0;
uint32_t lastWifiAttemptMs = 0;
bool ledOn = false;

IPAddress toIp(const uint8_t (&a)[4]) { return IPAddress(a[0], a[1], a[2], a[3]); }

// ----------------------------------------------------------------------------
//  Kommunikation mit der Kasse
// ----------------------------------------------------------------------------

void urlEncode(const char* in, char* out, size_t outLen) {
    static const char hex[] = "0123456789ABCDEF";
    size_t o = 0;
    for (; *in != '\0'; ++in) {
        const unsigned char c = static_cast<unsigned char>(*in);
        const bool plain = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') ||
                           c == '-' || c == '_' || c == '.';
        if (plain) {
            if (o + 1 >= outLen) break;
            out[o++] = static_cast<char>(c);
        } else {
            if (o + 3 >= outLen) break;
            out[o++] = '%';
            out[o++] = hex[c >> 4];
            out[o++] = hex[c & 0x0F];
        }
    }
    out[o] = '\0';
}

// Sendet eine Anfrage an die Kasse. Liefert false bei jedem Verbindungs- oder
// Protokollfehler – der Aufrufer lässt die Schranke dann geschlossen.
bool callKasse(const char* path, const char* formBody, char* resp, size_t respLen) {
    resp[0] = '\0';
    if (WiFi.status() != WL_CONNECTED) {
        kasseOnline = false;
        return false;
    }

    char url[64];
    snprintf(url, sizeof(url), "http://%u.%u.%u.%u%s", cfg::KASSE_IP[0], cfg::KASSE_IP[1],
             cfg::KASSE_IP[2], cfg::KASSE_IP[3], path);

    WiFiClient client;
    HTTPClient http;
    http.setTimeout(cfg::KASSE_HTTP_TIMEOUT_MS);
    if (!http.begin(client, url)) {
        kasseOnline = false;
        return false;
    }

    int code;
    if (formBody != nullptr) {
        http.addHeader("Content-Type", "application/x-www-form-urlencoded");
        code = http.POST(reinterpret_cast<uint8_t*>(const_cast<char*>(formBody)), strlen(formBody));
    } else {
        code = http.GET();
    }

    bool ok = false;
    if (code == 200) {
        const String body = http.getString();
        strncpy(resp, body.c_str(), respLen - 1);
        resp[respLen - 1] = '\0';
        ok = true;
    }
    http.end();

    kasseOnline = ok;
    if (ok) {
        char value[12];
        if (proto::getField(resp, proto::F_FREE, value, sizeof(value))) {
            freeSpaces = static_cast<unsigned>(atoi(value));
            freeKnown = true;
        }
    } else {
        Serial.printf("[Schranken] Kasse nicht erreichbar (%s, Code %d)\n", path, code);
    }
    return ok;
}

void buildBody(char* out, size_t outLen, const char* plate, bool manual) {
    char encoded[160];
    urlEncode(plate != nullptr ? plate : "", encoded, sizeof(encoded));
    if (plate != nullptr) {
        snprintf(out, outLen, "%s=%s%s", proto::P_PLATE, encoded, manual ? "&manual=1" : "");
    } else {
        snprintf(out, outLen, "%s=1", proto::P_MANUAL);
    }
}

park::Reason reasonOf(const char* resp) {
    char token[24];
    proto::getField(resp, proto::F_REASON, token, sizeof(token));
    return park::reasonFromToken(token);
}

void pollStatus() {
    char resp[128];
    if (!callKasse(proto::PATH_STATUS, nullptr, resp, sizeof(resp))) return;
    char value[12];
    if (proto::getField(resp, proto::F_CAPACITY, value, sizeof(value))) {
        capacity = static_cast<unsigned>(atoi(value));
    }
}

bool sendExitConfirm(const char* id, bool manual) {
    char body[200];
    char resp[128];
    buildBody(body, sizeof(body), id, manual);
    if (!callKasse(proto::PATH_EXIT_CONFIRM, body, resp, sizeof(resp))) return false;
    // Auch eine fachliche Ablehnung (z. B. bereits ausgefahren) gilt als quittiert.
    return true;
}

// ----------------------------------------------------------------------------
//  Einfahrt
// ----------------------------------------------------------------------------

void handleEntry(bool manual) {
    if (entryGate.isOpen()) {
        entryGate.setMessage("warn", "Einfahrt ist noch offen – zuerst Durchfahrt bestätigen");
        redirectTo(server, "/");
        return;
    }

    char plate[48];
    readArg(server, proto::P_PLATE, plate, sizeof(plate));

    char body[200];
    char resp[160];
    buildBody(body, sizeof(body), manual ? nullptr : plate, false);

    char msg[160];
    if (!callKasse(proto::PATH_ENTRY, body, resp, sizeof(resp))) {
        entryGate.setMessage("err", "Keine Verbindung zur Kasse – Schranke bleibt geschlossen");
        redirectTo(server, "/");
        return;
    }

    char result[16];
    char id[cfg::ID_BUF_LEN];
    proto::getField(resp, proto::F_RESULT, result, sizeof(result));
    proto::getField(resp, proto::F_ID, id, sizeof(id));

    if (strcmp(result, proto::R_GRANTED) == 0) {
        entryGate.open(id, manual, millis());
        if (manual) {
            snprintf(msg, sizeof(msg),
                     "Manuelle Abfertigung: Ersatz-ID %s (bitte notieren) – Einfahrt offen", id);
        } else {
            snprintf(msg, sizeof(msg), "Willkommen %s – Einfahrt offen", id);
        }
        entryGate.setMessage("ok", msg);
    } else {
        const park::Reason reason = reasonOf(resp);
        switch (reason) {
            case park::Reason::Invalid:
                snprintf(msg, sizeof(msg),
                         "Kennzeichen ungültig – Schranke bleibt geschlossen. "
                         "Manuelle Abfertigung möglich.");
                break;
            case park::Reason::Duplicate:
                snprintf(msg, sizeof(msg), "%s: bereits eingefahren – Schranke bleibt geschlossen", id);
                break;
            case park::Reason::Full:
                snprintf(msg, sizeof(msg), "Parkhaus voll – Schranke bleibt geschlossen");
                break;
            default:
                snprintf(msg, sizeof(msg), "Einfahrt verweigert: %s", park::reasonMessage(reason));
        }
        entryGate.setMessage("err", msg);
    }
    redirectTo(server, "/");
}

void handleEntryPass() {
    if (!entryGate.isOpen()) {
        entryGate.setMessage("warn", "Schranke ist nicht offen – nichts zu bestätigen");
    } else {
        char msg[96];
        snprintf(msg, sizeof(msg), "Durchfahrt %s bestätigt – Einfahrt zu", entryGate.id());
        entryGate.close();
        entryGate.setMessage("ok", msg);
    }
    redirectTo(server, "/");
}

// ----------------------------------------------------------------------------
//  Ausfahrt
// ----------------------------------------------------------------------------

void handleExit(bool manual) {
    if (exitGate.isOpen()) {
        exitGate.setMessage("warn", "Ausfahrt ist noch offen – zuerst Durchfahrt bestätigen");
        redirectTo(server, "/");
        return;
    }

    char plate[48];
    readArg(server, proto::P_PLATE, plate, sizeof(plate));
    if (manual && plate[0] == '\0') {
        exitGate.setMessage("err", "Manuelle Abfertigung: bitte Ersatz-ID (z. B. MANUAL-1) eingeben");
        redirectTo(server, "/");
        return;
    }

    char body[200];
    char resp[160];
    buildBody(body, sizeof(body), plate, manual);

    char msg[160];
    if (!callKasse(proto::PATH_EXIT_CHECK, body, resp, sizeof(resp))) {
        exitGate.setMessage("err", "Keine Verbindung zur Kasse – Ausfahrt bleibt geschlossen (Fail-safe)");
        redirectTo(server, "/");
        return;
    }

    char result[16];
    char id[cfg::ID_BUF_LEN];
    char dueText[16];
    proto::getField(resp, proto::F_RESULT, result, sizeof(result));
    proto::getField(resp, proto::F_ID, id, sizeof(id));
    proto::getField(resp, proto::F_DUE, dueText, sizeof(dueText));

    if (strcmp(result, proto::R_OPEN) == 0) {
        exitGate.open(id, manual, millis());
        snprintf(msg, sizeof(msg), "Gute Fahrt %s – Ausfahrt offen", id);
        exitGate.setMessage("ok", msg);
    } else {
        const park::Reason reason = reasonOf(resp);
        char due[16];
        park::formatMoney(static_cast<cfg::Money>(atol(dueText)), due, sizeof(due));
        switch (reason) {
            case park::Reason::Unpaid:
                snprintf(msg, sizeof(msg), "Bitte zuerst bezahlen – offener Betrag CHF %s (%s)", due, id);
                break;
            case park::Reason::Unknown:
                snprintf(msg, sizeof(msg),
                         "%s ist nicht im Register – bitte manuelle Abfertigung "
                         "(Ersatz-ID eingeben oder Personal rufen)",
                         id);
                break;
            case park::Reason::Exited:
                snprintf(msg, sizeof(msg), "%s ist bereits ausgefahren – bitte manuelle Abfertigung", id);
                break;
            case park::Reason::Invalid:
                snprintf(msg, sizeof(msg), "Kennzeichen ungültig – bitte manuelle Abfertigung");
                break;
            default:
                snprintf(msg, sizeof(msg), "Ausfahrt verweigert: %s", park::reasonMessage(reason));
        }
        exitGate.setMessage("err", msg);
    }
    redirectTo(server, "/");
}

void handleExitPass() {
    if (!exitGate.isOpen()) {
        exitGate.setMessage("warn", "Schranke ist nicht offen – nichts zu bestätigen");
        redirectTo(server, "/");
        return;
    }

    char id[cfg::ID_BUF_LEN];
    strncpy(id, exitGate.id(), sizeof(id));
    const bool manual = exitGate.manual();
    exitGate.close();

    char msg[160];
    if (sendExitConfirm(id, manual)) {
        snprintf(msg, sizeof(msg), "Durchfahrt %s bestätigt – Ausfahrt zu", id);
        exitGate.setMessage("ok", msg);
    } else {
        // Fahrzeug ist bereits durch – Meldung an die Kasse wird nachgeholt.
        strncpy(pendingExit, id, sizeof(pendingExit));
        pendingExitManual = manual;
        snprintf(msg, sizeof(msg),
                 "Durchfahrt %s bestätigt – Ausfahrt zu. Kasse nicht erreichbar, Meldung wird wiederholt.",
                 id);
        exitGate.setMessage("warn", msg);
    }
    redirectTo(server, "/");
}

// ----------------------------------------------------------------------------
//  Weboberfläche "Kamera-Simulation"
// ----------------------------------------------------------------------------

constexpr const char* PAGE_BODY = R"HTML(
<div class=grid>
<section>
  <h2>Einfahrt</h2>
  <div class=gate><span class=lamp id=entryLamp></span><span id=entryState>Einfahrt zu</span></div>
  <div class=muted id=entryInfo>&nbsp;</div>
  <form method=post action='/entry'>
    <label for=entryPlate>Kennzeichen (Kamera)</label>
    <input type=text id=entryPlate name=plate autocomplete=off autocapitalize=characters autofocus>
    <div class=buttons>
      <button>Kennzeichen erfassen</button>
      <button formaction='/entry/manual' class=secondary>Manuelle Abfertigung</button>
    </div>
  </form>
  <form method=post action='/entry/pass'><div class=buttons><button class=secondary>Durchfahrt bestätigen</button></div></form>
  <div id=entryMsg class=msg hidden></div>
</section>
<section>
  <h2>Ausfahrt</h2>
  <div class=gate><span class=lamp id=exitLamp></span><span id=exitState>Ausfahrt zu</span></div>
  <div class=muted id=exitInfo>&nbsp;</div>
  <form method=post action='/exit'>
    <label for=exitPlate>Kennzeichen (Kamera) bzw. Ersatz-ID bei manueller Abfertigung</label>
    <input type=text id=exitPlate name=plate autocomplete=off autocapitalize=characters>
    <div class=buttons>
      <button>Kennzeichen erfassen</button>
      <button formaction='/exit/manual' class=secondary>Manuelle Abfertigung</button>
    </div>
  </form>
  <form method=post action='/exit/pass'><div class=buttons><button class=secondary>Durchfahrt bestätigen</button></div></form>
  <div id=exitMsg class=msg hidden></div>
</section>
</div>
<section>
  <h2>Status</h2>
  <div class=stats>
    <div class=stat><b id=free>–</b><span>freie Plätze</span></div>
    <div class=stat><b id=cap>–</b><span>Kapazität</span></div>
    <div class=stat><b id=kasse>–</b><span>Verbindung zur Kasse</span></div>
  </div>
  <div id=pending class='msg warn' hidden></div>
</section>
<script>
function gate(p,g){
  const name=p==='entry'?'Einfahrt':'Ausfahrt';
  document.getElementById(p+'Lamp').className='lamp'+(g.open?' on':'');
  document.getElementById(p+'State').textContent=name+(g.open?' offen':' zu');
  document.getElementById(p+'Info').textContent=g.open?(g.id+' · schliesst automatisch in '+g.left+' s'):' ';
  const m=document.getElementById(p+'Msg');
  m.hidden=!g.msg; m.textContent=g.msg; m.className='msg '+g.cls;
}
async function refresh(){
  try{
    const s=await (await fetch('/state',{cache:'no-store'})).json();
    gate('entry',s.entry); gate('exit',s.exit);
    document.getElementById('free').textContent=s.freeKnown?s.free:'?';
    document.getElementById('cap').textContent=s.capacity;
    document.getElementById('kasse').textContent=s.kasse?'OK':'getrennt';
    const p=document.getElementById('pending');
    p.hidden=!s.pending; p.textContent='Ausfahrt '+s.pending+' noch nicht an die Kasse gemeldet – wird wiederholt';
  }catch(e){}
}
refresh(); setInterval(refresh,1000);
</script>
)HTML";

void handleRoot() {
    WebOut out(server);
    out.begin(200, "text/html; charset=utf-8");
    out.raw("<!doctype html><html lang=de><head><meta charset=utf-8>"
            "<meta name=viewport content='width=device-width,initial-scale=1'>"
            "<title>Parkhaus – Kamera-Simulation</title><style>")
        .raw(WEB_STYLE)
        .raw("</style></head><body><header><h1>Parkhaus – Schranken / Kamera-Simulation</h1>");
    out.printf("<a href='http://%u.%u.%u.%u/'>Kasse (Pico 1)</a>", cfg::KASSE_IP[0], cfg::KASSE_IP[1],
               cfg::KASSE_IP[2], cfg::KASSE_IP[3]);
    out.printf("<a href='http://%u.%u.%u.%u/log'>Protokoll</a>", cfg::KASSE_IP[0], cfg::KASSE_IP[1],
               cfg::KASSE_IP[2], cfg::KASSE_IP[3]);
    out.raw("</header><main>").raw(PAGE_BODY).raw("</main></body></html>");
    out.end();
}

void jsonString(WebOut& out, const char* s) {
    out.raw("\"");
    char esc[8];
    for (; *s != '\0'; ++s) {
        const unsigned char c = static_cast<unsigned char>(*s);
        if (c == '"' || c == '\\') {
            esc[0] = '\\';
            esc[1] = static_cast<char>(c);
            esc[2] = '\0';
            out.raw(esc);
        } else if (c < 0x20) {
            snprintf(esc, sizeof(esc), "\\u%04x", c);
            out.raw(esc);
        } else {
            esc[0] = static_cast<char>(c);
            esc[1] = '\0';
            out.raw(esc);
        }
    }
    out.raw("\"");
}

void gateJson(WebOut& out, const Gate& g, uint32_t now) {
    out.printf("{\"open\":%s,\"left\":%lu,\"id\":", g.isOpen() ? "true" : "false",
               static_cast<unsigned long>((g.remainingMs(now) + 999) / 1000));
    jsonString(out, g.id());
    out.raw(",\"msg\":");
    jsonString(out, g.message());
    out.raw(",\"cls\":");
    jsonString(out, g.messageClass());
    out.raw("}");
}

void handleState() {
    const uint32_t now = millis();
    WebOut out(server);
    out.begin(200, "application/json");
    out.raw("{\"entry\":");
    gateJson(out, entryGate, now);
    out.raw(",\"exit\":");
    gateJson(out, exitGate, now);
    out.printf(",\"free\":%u,\"freeKnown\":%s,\"capacity\":%u,\"kasse\":%s,\"pending\":", freeSpaces,
               freeKnown ? "true" : "false", capacity, kasseOnline ? "true" : "false");
    jsonString(out, pendingExit);
    out.raw("}");
    out.end();
}

void handleNotFound() { server.send(404, "text/plain; charset=utf-8", "Nicht gefunden\n"); }

// ----------------------------------------------------------------------------
//  Hintergrundaufgaben
// ----------------------------------------------------------------------------

void startWifi() {
    WiFi.config(toIp(cfg::SCHRANKEN_IP), toIp(cfg::KASSE_IP), toIp(cfg::KASSE_IP), toIp(cfg::NETMASK));
    WiFi.beginNoBlock(cfg::WIFI_SSID, cfg::WIFI_PASSWORD);
    lastWifiAttemptMs = millis();
}

void checkGateTimeouts(uint32_t now) {
    if (entryGate.timedOut(now)) {
        entryGate.close();
        entryGate.setMessage("warn", "Keine Durchfahrt innerhalb von 30 s – Einfahrt geschlossen");
    }
    if (exitGate.timedOut(now)) {
        char id[cfg::ID_BUF_LEN];
        strncpy(id, exitGate.id(), sizeof(id));
        const bool manual = exitGate.manual();
        exitGate.close();
        exitGate.setMessage("warn",
                            "Keine Durchfahrt innerhalb von 30 s – Ausfahrt geschlossen, "
                            "Fahrzeug bleibt im Parkhaus");
        // Nur fürs Protokoll; schlägt die Meldung fehl, ändert sich am Register nichts.
        char body[200];
        char resp[64];
        buildBody(body, sizeof(body), id, manual);
        callKasse(proto::PATH_EXIT_ABORT, body, resp, sizeof(resp));
    }
}

void updateLed() {
    const bool on = entryGate.isOpen() || exitGate.isOpen();
    if (on != ledOn) {
        ledOn = on;
        digitalWrite(LED_BUILTIN, on ? HIGH : LOW);
    }
}

}  // namespace

void setup() {
    Serial.begin(115200);
    pinMode(LED_BUILTIN, OUTPUT);
    digitalWrite(LED_BUILTIN, LOW);

    entryGate.begin();
    exitGate.begin();

    WiFi.mode(WIFI_STA);
    startWifi();

    server.on("/", HTTP_GET, handleRoot);
    server.on("/state", HTTP_GET, handleState);
    server.on("/entry", HTTP_POST, [] { handleEntry(false); });
    server.on("/entry/manual", HTTP_POST, [] { handleEntry(true); });
    server.on("/entry/pass", HTTP_POST, handleEntryPass);
    server.on("/exit", HTTP_POST, [] { handleExit(false); });
    server.on("/exit/manual", HTTP_POST, [] { handleExit(true); });
    server.on("/exit/pass", HTTP_POST, handleExitPass);
    server.onNotFound(handleNotFound);
    server.begin();

    Serial.printf("[Schranken] verbinde mit WLAN '%s' ...\n", cfg::WIFI_SSID);
}

void loop() {
    server.handleClient();

    const uint32_t now = millis();
    checkGateTimeouts(now);
    updateLed();

    const bool connected = WiFi.status() == WL_CONNECTED;
    if (!connected) {
        kasseOnline = false;
        if (now - lastWifiAttemptMs >= cfg::WIFI_RECONNECT_INTERVAL_MS) {
            Serial.println("[Schranken] WLAN getrennt – neuer Verbindungsversuch");
            startWifi();
        }
        return;
    }

    if (now - lastStatusPollMs >= cfg::STATUS_POLL_INTERVAL_MS) {
        lastStatusPollMs = now;
        if (pendingExit[0] != '\0' && sendExitConfirm(pendingExit, pendingExitManual)) {
            Serial.printf("[Schranken] Ausfahrt %s nachgemeldet\n", pendingExit);
            pendingExit[0] = '\0';
        }
        pollStatus();
    }
}
