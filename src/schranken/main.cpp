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
#include "ReplayCache.h"
#include "WebOut.h"
#include "WebScript.h"
#include "WebStyle.h"

namespace {

WebServer server(cfg::HTTP_PORT);
Gate entryGate("Einfahrt", cfg::SERVO_ENTRY_PIN);
Gate exitGate("Ausfahrt", cfg::SERVO_EXIT_PIN);

// Antworten auf wiederholte Browser-Anfragen (gleiche Request-ID).
park::ReplayCache<6, 320> actionReplay;

// Zuletzt von der Kasse gemeldete Belegung (nur Anzeige).
bool freeKnown = false;
unsigned freeSpaces = 0;
unsigned capacity = cfg::CAPACITY;
bool kasseOnline = false;

// Meldung im Bereich "Status" (Schranken-Test, Servo-Fehler).
const char* sysCls = "ok";
char sysMessage[200] = "";

// Bestätigte Ausfahrt, die die Kasse noch nicht quittiert hat (wird wiederholt).
char pendingExit[cfg::ID_BUF_LEN] = "";
bool pendingExitManual = false;
char pendingRid[park::ReplayCache<1, 1>::ID_LEN] = "";

uint32_t bootId = 0;
uint32_t ridCounter = 0;
uint32_t lastStatusPollMs = 0;
uint32_t lastWifiAttemptMs = 0;
bool ledOn = false;

IPAddress toIp(const uint8_t (&a)[4]) { return IPAddress(a[0], a[1], a[2], a[3]); }

void setSysMessage(const char* cls, const char* text) {
    sysCls = cls;
    strncpy(sysMessage, text, sizeof(sysMessage) - 1);
    sysMessage[sizeof(sysMessage) - 1] = '\0';
}

void updateLed() {
    const bool on = entryGate.isOpen() || exitGate.isOpen();
    if (on != ledOn) {
        ledOn = on;
        digitalWrite(LED_BUILTIN, on ? HIGH : LOW);
    }
}

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

void newRid(char* out, size_t len) {
    snprintf(out, len, "p2-%08lx-%lu", static_cast<unsigned long>(bootId),
             static_cast<unsigned long>(++ridCounter));
}

// Ein einzelner HTTP-Versuch. `code` < 0 = Verbindungsfehler/Timeout.
bool callKasseOnce(const char* path, const char* formBody, char* resp, size_t respLen, int& code) {
    char url[64];
    snprintf(url, sizeof(url), "http://%u.%u.%u.%u%s", cfg::KASSE_IP[0], cfg::KASSE_IP[1],
             cfg::KASSE_IP[2], cfg::KASSE_IP[3], path);

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
        const String body = http.getString();
        strncpy(resp, body.c_str(), respLen - 1);
        resp[respLen - 1] = '\0';
        // Eine Antwort ohne result-Feld gilt als abgebrochen.
        char result[16];
        ok = proto::getField(resp, proto::F_RESULT, result, sizeof(result));
    }
    http.end();
    return ok;
}

// Sendet eine Anfrage an die Kasse, bei Verbindungsfehlern mit Wiederholungen
// (der Body enthält eine Request-ID, die Kasse führt die Aktion nur einmal aus).
// Liefert false, wenn keine gültige Antwort kam – die Schranke bleibt dann zu.
bool callKasse(const char* path, const char* formBody, char* resp, size_t respLen,
               uint8_t attempts = cfg::KASSE_HTTP_ATTEMPTS) {
    resp[0] = '\0';
    int code = -1;
    for (uint8_t i = 0; i < attempts; ++i) {
        if (WiFi.status() != WL_CONNECTED) break;
        if (callKasseOnce(path, formBody, resp, respLen, code)) {
            kasseOnline = true;
            char value[12];
            if (proto::getField(resp, proto::F_FREE, value, sizeof(value))) {
                freeSpaces = static_cast<unsigned>(atoi(value));
                freeKnown = true;
            }
            if (i > 0) Serial.printf("[Schranken] %s: Erfolg im %u. Versuch\n", path, i + 1);
            return true;
        }
        Serial.printf("[Schranken] %s: Versuch %u fehlgeschlagen (Code %d)\n", path, i + 1, code);
        if (code >= 400) break;  // fachlich/formal abgelehnt – Wiederholen hilft nicht
        if (i + 1 < attempts) delay(cfg::KASSE_HTTP_RETRY_DELAY_MS);
    }
    kasseOnline = false;
    return false;
}

void buildBody(char* out, size_t outLen, const char* plate, bool manual, const char* rid) {
    if (plate == nullptr) {
        snprintf(out, outLen, "%s=1&%s=%s", proto::P_MANUAL, proto::P_RID, rid);
        return;
    }
    char encoded[160];
    urlEncode(plate, encoded, sizeof(encoded));
    snprintf(out, outLen, "%s=%s%s&%s=%s", proto::P_PLATE, encoded, manual ? "&manual=1" : "",
             proto::P_RID, rid);
}

park::Reason reasonOf(const char* resp) {
    char token[24];
    proto::getField(resp, proto::F_REASON, token, sizeof(token));
    return park::reasonFromToken(token);
}

void pollStatus() {
    char resp[128];
    // Nur ein Versuch: die Abfrage wiederholt sich ohnehin alle paar Sekunden.
    if (!callKasse(proto::PATH_STATUS, nullptr, resp, sizeof(resp), 1)) return;
    char value[12];
    if (proto::getField(resp, proto::F_CAPACITY, value, sizeof(value))) {
        capacity = static_cast<unsigned>(atoi(value));
    }
}

bool sendExitConfirm(const char* id, bool manual, const char* rid) {
    char body[220];
    char resp[128];
    buildBody(body, sizeof(body), id, manual, rid);
    // Auch eine fachliche Ablehnung (z. B. bereits ausgefahren) gilt als quittiert.
    return callKasse(proto::PATH_EXIT_CONFIRM, body, resp, sizeof(resp));
}

// ----------------------------------------------------------------------------
//  Aktionen (werden von der Weboberfläche über /api/action ausgelöst)
// ----------------------------------------------------------------------------

void doEntry(bool manual, const char* plate) {
    if (entryGate.isOpen()) {
        entryGate.setMessage("warn", "Einfahrt ist noch offen – zuerst Durchfahrt bestätigen");
        return;
    }

    char rid[park::ReplayCache<1, 1>::ID_LEN];
    char body[220];
    char resp[160];
    newRid(rid, sizeof(rid));
    buildBody(body, sizeof(body), manual ? nullptr : plate, false, rid);

    if (!callKasse(proto::PATH_ENTRY, body, resp, sizeof(resp))) {
        entryGate.setMessage("err", "Keine Verbindung zur Kasse – Schranke bleibt geschlossen. "
                                    "Bitte erneut versuchen.");
        return;
    }

    char result[16];
    char id[cfg::ID_BUF_LEN];
    char msg[160];
    proto::getField(resp, proto::F_RESULT, result, sizeof(result));
    proto::getField(resp, proto::F_ID, id, sizeof(id));

    if (strcmp(result, proto::R_GRANTED) == 0) {
        entryGate.open(id, manual, millis());
        updateLed();
        if (manual) {
            snprintf(msg, sizeof(msg),
                     "Manuelle Abfertigung: Ersatz-ID %s (bitte notieren) – Einfahrt offen", id);
        } else {
            snprintf(msg, sizeof(msg), "Willkommen %s – Einfahrt offen", id);
        }
        entryGate.setMessage("ok", msg);
        return;
    }

    const park::Reason reason = reasonOf(resp);
    switch (reason) {
        case park::Reason::Invalid:
            snprintf(msg, sizeof(msg),
                     "Kennzeichen ungültig – Schranke bleibt geschlossen. Manuelle Abfertigung möglich.");
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

void doEntryPass() {
    if (!entryGate.isOpen()) {
        entryGate.setMessage("warn", "Schranke ist nicht offen – nichts zu bestätigen");
        return;
    }
    char msg[96];
    snprintf(msg, sizeof(msg), "Durchfahrt %s bestätigt – Einfahrt zu", entryGate.id());
    entryGate.close();
    updateLed();
    entryGate.setMessage("ok", msg);
}

void doExit(bool manual, const char* plate) {
    if (exitGate.isOpen()) {
        exitGate.setMessage("warn", "Ausfahrt ist noch offen – zuerst Durchfahrt bestätigen");
        return;
    }
    if (manual && plate[0] == '\0') {
        exitGate.setMessage("err", "Manuelle Abfertigung: bitte Ersatz-ID (z. B. MANUAL-1) eingeben");
        return;
    }

    char rid[park::ReplayCache<1, 1>::ID_LEN];
    char body[220];
    char resp[160];
    newRid(rid, sizeof(rid));
    buildBody(body, sizeof(body), plate, manual, rid);

    if (!callKasse(proto::PATH_EXIT_CHECK, body, resp, sizeof(resp))) {
        exitGate.setMessage("err", "Keine Verbindung zur Kasse – Ausfahrt bleibt geschlossen "
                                   "(Fail-safe). Bitte erneut versuchen.");
        return;
    }

    char result[16];
    char id[cfg::ID_BUF_LEN];
    char dueText[16];
    char msg[160];
    proto::getField(resp, proto::F_RESULT, result, sizeof(result));
    proto::getField(resp, proto::F_ID, id, sizeof(id));
    proto::getField(resp, proto::F_DUE, dueText, sizeof(dueText));

    if (strcmp(result, proto::R_OPEN) == 0) {
        exitGate.open(id, manual, millis());
        updateLed();
        snprintf(msg, sizeof(msg), "Gute Fahrt %s – Ausfahrt offen", id);
        exitGate.setMessage("ok", msg);
        return;
    }

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

void doExitPass() {
    if (!exitGate.isOpen()) {
        exitGate.setMessage("warn", "Schranke ist nicht offen – nichts zu bestätigen");
        return;
    }

    char id[cfg::ID_BUF_LEN];
    strncpy(id, exitGate.id(), sizeof(id));
    const bool manual = exitGate.manual();
    exitGate.close();
    updateLed();

    char rid[sizeof(pendingRid)];
    newRid(rid, sizeof(rid));
    char msg[160];
    if (sendExitConfirm(id, manual, rid)) {
        snprintf(msg, sizeof(msg), "Durchfahrt %s bestätigt – Ausfahrt zu", id);
        exitGate.setMessage("ok", msg);
    } else {
        // Fahrzeug ist bereits durch – Meldung an die Kasse wird mit derselben
        // Request-ID nachgeholt.
        strncpy(pendingExit, id, sizeof(pendingExit));
        strncpy(pendingRid, rid, sizeof(pendingRid));
        pendingExitManual = manual;
        snprintf(msg, sizeof(msg),
                 "Durchfahrt %s bestätigt – Ausfahrt zu. Kasse nicht erreichbar, Meldung wird wiederholt.",
                 id);
        exitGate.setMessage("warn", msg);
    }
}

// Öffnet und schliesst beide Schranken einmal (Verdrahtung prüfen).
void runServoTest() {
    if (entryGate.isOpen() || exitGate.isOpen()) {
        setSysMessage("warn", "Test nicht möglich: eine Schranke ist gerade offen.");
        return;
    }
    if (!entryGate.servoOk() || !exitGate.servoOk()) {
        setSysMessage("err", "Servo-Ausgang konnte nicht eingerichtet werden – Pico 2 neu starten.");
        return;
    }
    Serial.println("[Schranken] Schranken-Test: oeffnen ...");
    digitalWrite(LED_BUILTIN, HIGH);
    entryGate.moveServo(true);
    exitGate.moveServo(true);
    delay(1200);
    entryGate.moveServo(false);
    exitGate.moveServo(false);
    delay(600);
    digitalWrite(LED_BUILTIN, LOW);
    ledOn = false;
    Serial.println("[Schranken] Schranken-Test: geschlossen");

    char msg[200];
    snprintf(msg, sizeof(msg),
             "Test ausgeführt: beide Schranken wurden geöffnet und wieder geschlossen "
             "(Einfahrt GP%u, Ausfahrt GP%u). Hat sich ein Servo nicht bewegt, Verdrahtung prüfen.",
             entryGate.pin(), exitGate.pin());
    setSysMessage("ok", msg);
}

// ----------------------------------------------------------------------------
//  HTTP-Handler
// ----------------------------------------------------------------------------

void handleAction() {
    char rid[park::ReplayCache<1, 1>::ID_LEN];
    readArg(server, proto::P_RID, rid, sizeof(rid));
    if (const char* cached = actionReplay.find(rid)) {
        sendAndClose(server, 200, "application/json", cached);
        return;
    }

    char action[16];
    char plate[48];
    readArg(server, "action", action, sizeof(action));
    readArg(server, proto::P_PLATE, plate, sizeof(plate));

    const char* cls = "err";
    const char* msg = "Unbekannte Aktion";
    if (strcmp(action, "entry") == 0 || strcmp(action, "entryManual") == 0 ||
        strcmp(action, "entryPass") == 0) {
        if (strcmp(action, "entryPass") == 0) {
            doEntryPass();
        } else {
            doEntry(strcmp(action, "entryManual") == 0, plate);
        }
        cls = entryGate.messageClass();
        msg = entryGate.message();
    } else if (strcmp(action, "exit") == 0 || strcmp(action, "exitManual") == 0 ||
               strcmp(action, "exitPass") == 0) {
        if (strcmp(action, "exitPass") == 0) {
            doExitPass();
        } else {
            doExit(strcmp(action, "exitManual") == 0, plate);
        }
        cls = exitGate.messageClass();
        msg = exitGate.message();
    } else if (strcmp(action, "servoTest") == 0) {
        runServoTest();
        cls = sysCls;
        msg = sysMessage;
    }

    char escaped[240];
    park::jsonEscape(msg, escaped, sizeof(escaped));
    char body[320];
    snprintf(body, sizeof(body), "{\"ok\":%s,\"cls\":\"%s\",\"msg\":\"%s\"}",
             strcmp(cls, "ok") == 0 ? "true" : "false", cls, escaped);
    actionReplay.store(rid, body);
    sendAndClose(server, 200, "application/json", body);
}

constexpr const char* PAGE_BODY = R"HTML(
<div class=grid>
<section id=entrySec>
  <h2>Einfahrt</h2>
  <div class=gate><span class=lamp id=entryLamp></span><span id=entryState>Einfahrt zu</span></div>
  <div class=muted id=entryInfo>&nbsp;</div>
  <form onsubmit="act('entry','entry');return false">
    <label for=entryPlate>Kennzeichen (Kamera)</label>
    <input type=text id=entryPlate autocomplete=off autocapitalize=characters autofocus>
    <div class=buttons>
      <button type=submit>Kennzeichen erfassen</button>
      <button type=button class=secondary onclick="act('entry','entryManual')">Manuelle Abfertigung</button>
      <button type=button class=secondary onclick="act('entry','entryPass')">Durchfahrt bestätigen</button>
    </div>
  </form>
  <div class='msg out' hidden></div>
</section>
<section id=exitSec>
  <h2>Ausfahrt</h2>
  <div class=gate><span class=lamp id=exitLamp></span><span id=exitState>Ausfahrt zu</span></div>
  <div class=muted id=exitInfo>&nbsp;</div>
  <form onsubmit="act('exit','exit');return false">
    <label for=exitPlate>Kennzeichen (Kamera) bzw. Ersatz-ID bei manueller Abfertigung</label>
    <input type=text id=exitPlate autocomplete=off autocapitalize=characters>
    <div class=buttons>
      <button type=submit>Kennzeichen erfassen</button>
      <button type=button class=secondary onclick="act('exit','exitManual')">Manuelle Abfertigung</button>
      <button type=button class=secondary onclick="act('exit','exitPass')">Durchfahrt bestätigen</button>
    </div>
  </form>
  <div class='msg out' hidden></div>
</section>
</div>
<section id=sysSec>
  <h2>Status</h2>
  <div class=stats>
    <div class=stat><b id=free>–</b><span>freie Plätze</span></div>
    <div class=stat><b id=cap>–</b><span>Kapazität</span></div>
    <div class=stat><b id=kasse>–</b><span>Verbindung Pico 2 → Kasse</span></div>
  </div>
  <div id=pending class='msg warn' hidden></div>
  <div class=buttons><button type=button class=secondary onclick="act('sys','servoTest')">Schranken testen</button></div>
  <p class=muted>Öffnet und schliesst beide Schranken einmal – zum Prüfen der Servo-Verdrahtung.</p>
  <div class='msg out' hidden></div>
</section>
<script>
const SEC={entry:'entrySec',exit:'exitSec',sys:'sysSec'};
const BUSY={entry:'Kennzeichen wird an die Kasse gemeldet …',entryManual:'Ersatz-ID wird angefordert …',
  entryPass:'Durchfahrt wird bestätigt …',exit:'Bezahlstatus wird bei der Kasse geprüft …',
  exitManual:'Bezahlstatus wird bei der Kasse geprüft …',exitPass:'Ausfahrt wird an die Kasse gemeldet …',
  servoTest:'Schranken werden getestet …'};
async function act(target,action){
  const sec=$(SEC[target]); const input=sec.querySelector('input[type=text]');
  const data={action,rid:newRid()}; if(input) data.plate=input.value;
  busy(sec,true,BUSY[action]);
  let r;
  try{ r=await post('/api/action',data); }
  catch(e){ r={ok:false,cls:'err',msg:'Keine Antwort von Pico 2 – bitte erneut versuchen'}; }
  busy(sec,false); show(sec.querySelector('.out'),r.cls,r.msg);
  if(r.ok&&input&&(action==='entry'||action==='exit')) input.value='';
  refresh();
}
function gate(p,g){
  const name=p==='entry'?'Einfahrt':'Ausfahrt';
  $(p+'Lamp').className='lamp'+(g.open?' on':'');
  $(p+'State').textContent=name+(g.open?' offen':' zu');
  $(p+'Info').textContent=g.open?(g.id+' · schliesst automatisch in '+g.left+' s'):(g.servo?' ':'Servo-Ausgang nicht bereit!');
  const sec=$(SEC[p]); if(!sec.dataset.busy) show(sec.querySelector('.out'),g.cls,g.msg);
}
let polling=false;
async function refresh(){
  if(polling) return; polling=true;
  try{
    const s=await req('/state',{},1,4000);
    gate('entry',s.entry); gate('exit',s.exit);
    $('free').textContent=s.freeKnown?s.free:'?'; $('cap').textContent=s.capacity;
    $('kasse').textContent=s.kasse?'OK':'getrennt';
    const p=$('pending'); p.hidden=!s.pending;
    p.textContent='Ausfahrt '+s.pending+' noch nicht an die Kasse gemeldet – wird automatisch wiederholt';
    if(!$('sysSec').dataset.busy) show($('sysSec').querySelector('.out'),s.sys.cls,s.sys.msg);
  }catch(e){}
  finally{ polling=false; }
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
        .raw("</style><script>")
        .raw(WEB_SCRIPT_COMMON)
        .raw("</script></head><body><header><h1>Parkhaus – Schranken / Kamera-Simulation</h1>");
    out.printf("<a href='http://%u.%u.%u.%u/'>Kasse (Pico 1)</a>", cfg::KASSE_IP[0], cfg::KASSE_IP[1],
               cfg::KASSE_IP[2], cfg::KASSE_IP[3]);
    out.printf("<a href='http://%u.%u.%u.%u/log'>Protokoll</a>", cfg::KASSE_IP[0], cfg::KASSE_IP[1],
               cfg::KASSE_IP[2], cfg::KASSE_IP[3]);
    out.raw("<span class=spacer></span><div class=head-actions>"
            "<span class=muted>Browser ↔ Pico 2:</span><span id=link class='pill ok'>verbunden</span>"
            "<button class=secondary onclick='refresh()'>Aktualisieren</button></div>"
            "</header><main>")
        .raw(PAGE_BODY)
        .raw("</main></body></html>");
}

void gateJson(WebOut& out, const Gate& g, uint32_t now) {
    out.printf("{\"open\":%s,\"servo\":%s,\"left\":%lu", g.isOpen() ? "true" : "false",
               g.servoOk() ? "true" : "false",
               static_cast<unsigned long>((g.remainingMs(now) + 999) / 1000));
    out.field("id", g.id()).field("msg", g.message()).field("cls", g.messageClass()).raw("}");
}

void handleState() {
    const uint32_t now = millis();
    WebOut out(server);
    out.begin(200, "application/json");
    out.raw("{\"entry\":");
    gateJson(out, entryGate, now);
    out.raw(",\"exit\":");
    gateJson(out, exitGate, now);
    out.printf(",\"free\":%u,\"freeKnown\":%s,\"capacity\":%u,\"kasse\":%s", freeSpaces,
               freeKnown ? "true" : "false", capacity, kasseOnline ? "true" : "false");
    out.field("pending", pendingExit);
    out.raw(",\"sys\":{").field("msg", sysMessage, true).field("cls", sysCls).raw("}}");
}

void handleNotFound() { sendAndClose(server, 404, "text/plain; charset=utf-8", "Nicht gefunden\n"); }

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
        char rid[park::ReplayCache<1, 1>::ID_LEN];
        char body[220];
        char resp[64];
        newRid(rid, sizeof(rid));
        buildBody(body, sizeof(body), id, manual, rid);
        callKasse(proto::PATH_EXIT_ABORT, body, resp, sizeof(resp));
    }
}

}  // namespace

void setup() {
    Serial.begin(115200);
    pinMode(LED_BUILTIN, OUTPUT);
    digitalWrite(LED_BUILTIN, LOW);
    bootId = rp2040.hwrand32();

    const bool entryOk = entryGate.begin();
    const bool exitOk = exitGate.begin();
    Serial.printf("[Schranken] Servo Einfahrt GP%u: %s, Ausfahrt GP%u: %s\n", entryGate.pin(),
                  entryOk ? "bereit" : "FEHLER", exitGate.pin(), exitOk ? "bereit" : "FEHLER");
    // Selbsttest beim Start: beide Schranken öffnen und schliessen einmal.
    runServoTest();
    setSysMessage(entryOk && exitOk ? "ok" : "err",
                  entryOk && exitOk ? "Selbsttest beim Start ausgeführt (Schranken kurz geöffnet)."
                                    : "Servo-Ausgang konnte nicht eingerichtet werden.");

    WiFi.mode(WIFI_STA);
    startWifi();

    server.on("/", HTTP_GET, handleRoot);
    server.on("/state", HTTP_GET, handleState);
    server.on("/api/action", HTTP_POST, handleAction);
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
        if (pendingExit[0] != '\0' && sendExitConfirm(pendingExit, pendingExitManual, pendingRid)) {
            Serial.printf("[Schranken] Ausfahrt %s nachgemeldet\n", pendingExit);
            pendingExit[0] = '\0';
        }
        pollStatus();
    }
}
