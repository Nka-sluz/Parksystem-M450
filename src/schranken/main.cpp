// =============================================================================
//  Pico 2 – "Schranken"
//
//  - verbindet sich mit dem WLAN der Kasse (statische IP 192.168.42.2)
//  - steuert Einfahrts- und Ausfahrtsschranke: standardmässig ein gemeinsamer
//    SG90-Servo an GP8, optional zwei Servos (GP8/GP9, cfg::SERVO_COUNT);
//    Onboard-LED leuchtet, solange eine Schranke offen ist
//  - Weboberfläche "Kamera-Simulation": http://192.168.42.2/
//  - jede Entscheidung trifft die Kasse (Pico 1). Ist sie nicht erreichbar,
//    bleiben beide Schranken geschlossen (Fail-safe).
//
//  Die Logik steckt in park::GateController (lib/ParkCore). Diese Datei
//  verbindet sie mit der echten Hardware (Servo, LED, millis(), HTTP/WLAN)
//  und der Weboberfläche.
// =============================================================================

#include <Arduino.h>
#include <WebServer.h>
#include <WiFi.h>
#include <cyw43.h>

#include "Config.h"
#include "Format.h"
#include "GateController.h"
#include "HttpKasse.h"
#include "PicoClock.h"
#include "PicoOutputs.h"
#include "ReplayCache.h"
#include "WebOut.h"
#include "WebScript.h"
#include "WebStyle.h"

namespace {

WebServer server(cfg::HTTP_PORT);

// Servo 0 = Einfahrt bzw. gemeinsame Schranke, Servo 1 = Ausfahrt (nur bei 2 Servos).
constexpr bool SHARED_BARRIER = cfg::SERVO_COUNT == 1;
constexpr uint8_t SERVO_PINS[2] = {cfg::SERVO_ENTRY_PIN, cfg::SERVO_EXIT_PIN};
ServoBarrier barriers[cfg::SERVO_COUNT];
LedLamp led;
HttpKasse kasse;
PicoClock picoClock;
// Bei einem Servo zeigen beide Fahrspuren auf dieselbe Schranke.
park::GateController gates(barriers[0], barriers[cfg::SERVO_COUNT - 1], led, kasse, picoClock);

// Antworten auf wiederholte Browser-Anfragen (gleiche Request-ID).
park::ReplayCache<6, 320> actionReplay;

// Meldung im Bereich "Status" (Schranken-Test, Servo-Fehler).
const char* sysCls = "ok";
char sysMessage[200] = "";

uint32_t lastStatusPollMs = 0;
uint32_t lastWifiAttemptMs = 0;
uint32_t linkLostSinceMs = 0;  // 0 = WLAN-Link vorhanden

IPAddress toIp(const uint8_t (&a)[4]) { return IPAddress(a[0], a[1], a[2], a[3]); }

void setSysMessage(const char* cls, const char* text) {
    sysCls = cls;
    strncpy(sysMessage, text, sizeof(sysMessage) - 1);
    sysMessage[sizeof(sysMessage) - 1] = '\0';
}

// Servo der Fahrspur (für die Anzeige "Servo-Ausgang bereit").
bool laneServoOk(const park::Gate& g) {
    return barriers[SHARED_BARRIER || &g == &gates.entryGate() ? 0 : 1].ok();
}

// Öffnet und schliesst alle Schranken einmal (Verdrahtung prüfen).
void runServoTest() {
    if (gates.anyOpen()) {
        setSysMessage("warn", "Test nicht möglich: eine Schranke ist gerade offen.");
        return;
    }
    for (const ServoBarrier& b : barriers) {
        if (!b.ok()) {
            setSysMessage("err", "Servo-Ausgang konnte nicht eingerichtet werden – Pico 2 neu starten.");
            return;
        }
    }
    Serial.println("[Schranken] Schranken-Test: oeffnen ...");
    led.set(true);
    for (ServoBarrier& b : barriers) b.open();
    delay(1200);
    for (ServoBarrier& b : barriers) b.close();
    delay(600);
    led.set(false);
    Serial.println("[Schranken] Schranken-Test: geschlossen");

    char msg[200];
    if (SHARED_BARRIER) {
        snprintf(msg, sizeof(msg),
                 "Test ausgeführt: die gemeinsame Schranke (GP%u) wurde geöffnet und wieder "
                 "geschlossen. Hat sich der Servo nicht bewegt, Verdrahtung prüfen.",
                 SERVO_PINS[0]);
    } else {
        snprintf(msg, sizeof(msg),
                 "Test ausgeführt: beide Schranken wurden geöffnet und wieder geschlossen "
                 "(Einfahrt GP%u, Ausfahrt GP%u). Hat sich ein Servo nicht bewegt, Verdrahtung prüfen.",
                 SERVO_PINS[0], SERVO_PINS[1]);
    }
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
    const park::Gate* lane = nullptr;
    if (strcmp(action, "entry") == 0) {
        gates.onEntryPlate(plate);
        lane = &gates.entryGate();
    } else if (strcmp(action, "entryManual") == 0) {
        gates.onEntryManual();
        lane = &gates.entryGate();
    } else if (strcmp(action, "entryPass") == 0) {
        gates.onEntryPassed();
        lane = &gates.entryGate();
    } else if (strcmp(action, "exit") == 0) {
        gates.onExitPlate(plate);
        lane = &gates.exitGate();
    } else if (strcmp(action, "exitManual") == 0) {
        gates.onExitManual(plate);
        lane = &gates.exitGate();
    } else if (strcmp(action, "exitPass") == 0) {
        gates.onExitPassed();
        lane = &gates.exitGate();
    } else if (strcmp(action, "servoTest") == 0) {
        runServoTest();
        cls = sysCls;
        msg = sysMessage;
    }
    if (lane != nullptr) {
        cls = lane->messageClass();
        msg = lane->message();
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
  <form data-act="entry:entry">
    <label for=entryPlate>Kennzeichen (Kamera)</label>
    <input type=text id=entryPlate autocomplete=off autocapitalize=characters autofocus>
    <div class=buttons>
      <button type=submit>Kennzeichen erfassen</button>
      <button type=button class=secondary data-act="entry:entryManual">Manuelle Abfertigung</button>
      <button type=button class=secondary data-act="entry:entryPass">Durchfahrt bestätigen</button>
    </div>
  </form>
  <div class='msg out' hidden></div>
</section>
<section id=exitSec>
  <h2>Ausfahrt</h2>
  <div class=gate><span class=lamp id=exitLamp></span><span id=exitState>Ausfahrt zu</span></div>
  <div class=muted id=exitInfo>&nbsp;</div>
  <form data-act="exit:exit">
    <label for=exitPlate>Kennzeichen (Kamera) bzw. Ersatz-ID bei manueller Abfertigung</label>
    <input type=text id=exitPlate autocomplete=off autocapitalize=characters>
    <div class=buttons>
      <button type=submit>Kennzeichen erfassen</button>
      <button type=button class=secondary data-act="exit:exitManual">Manuelle Abfertigung</button>
      <button type=button class=secondary data-act="exit:exitPass">Durchfahrt bestätigen</button>
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
  <div class=buttons><button type=button class=secondary data-act="sys:servoTest">Schranken testen</button></div>
  <p class=muted>Öffnet und schliesst die Schranke(n) einmal – zum Prüfen der Servo-Verdrahtung.</p>
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
// Ereignisse per addEventListener statt onclick-Attribut: Inline-Handler in
// einem <form> suchen Namen zuerst im Formular (z. B. form.action).
document.querySelectorAll('[data-act]').forEach(el=>{
  const [target,action]=el.dataset.act.split(':');
  el.addEventListener(el.tagName==='FORM'?'submit':'click',e=>{e.preventDefault();act(target,action);});
});
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
    if (SHARED_BARRIER) {
        out.printf("<span class=muted>Eine gemeinsame Schranke (GP%u) für Ein- und Ausfahrt</span>",
                   SERVO_PINS[0]);
    } else {
        out.printf("<span class=muted>Schranken: Einfahrt GP%u, Ausfahrt GP%u</span>", SERVO_PINS[0],
                   SERVO_PINS[1]);
    }
    out.raw("<span class=spacer></span><div class=head-actions>"
            "<span class=muted>Browser ↔ Pico 2:</span><span id=link class='pill ok'>verbunden</span>"
            "<button class=secondary onclick='refresh()'>Aktualisieren</button></div>"
            "</header><main>");
    if (PARK_FAILSAFE_TEST) {
        out.raw("<div class='msg err'>Test-Firmware für TC-38 (Fail-safe): Pico 2 erreicht die Kasse "
                "absichtlich nicht. Nach dem Test wieder schranken.uf2 aufspielen!</div>");
    }
    out.raw(PAGE_BODY)
        .raw("</main></body></html>");
}

void gateJson(WebOut& out, const park::Gate& g, uint32_t now) {
    out.printf("{\"open\":%s,\"servo\":%s,\"left\":%lu", g.isOpen() ? "true" : "false",
               laneServoOk(g) ? "true" : "false",
               static_cast<unsigned long>((g.remainingMs(now) + 999) / 1000));
    out.field("id", g.id()).field("msg", g.message()).field("cls", g.messageClass()).raw("}");
}

void handleState() {
    const uint32_t now = millis();
    WebOut out(server);
    out.begin(200, "application/json");
    out.raw("{\"entry\":");
    gateJson(out, gates.entryGate(), now);
    out.raw(",\"exit\":");
    gateJson(out, gates.exitGate(), now);
    out.printf(",\"free\":%u,\"freeKnown\":%s,\"capacity\":%u,\"kasse\":%s", kasse.freeSpaces(),
               kasse.freeKnown() ? "true" : "false", kasse.capacity(), kasse.online() ? "true" : "false");
    out.field("pending", gates.pendingExit());
    out.raw(",\"sys\":{").field("msg", sysMessage, true).field("cls", sysCls).raw("}}");
}

void handleNotFound() { sendAndClose(server, 404, "text/plain; charset=utf-8", "Nicht gefunden\n"); }

// ----------------------------------------------------------------------------
//  WLAN-Verbindung zur Kasse
// ----------------------------------------------------------------------------

void startWifi() {
    WiFi.config(toIp(cfg::SCHRANKEN_IP), toIp(cfg::KASSE_IP), toIp(cfg::KASSE_IP), toIp(cfg::NETMASK));
    // Blockiert trotz "NoBlock" bis zum Verbindungsaufbau, höchstens WIFI_JOIN_TIMEOUT_MS.
    WiFi.beginNoBlock(cfg::WIFI_SSID, cfg::WIFI_PASSWORD);
    lastWifiAttemptMs = millis();
    linkLostSinceMs = 0;
    kasse.resetLastOk(lastWifiAttemptMs);
}

// Mit statischer IP meldet WiFi.status() auch nach einem Abbruch der
// WLAN-Verbindung (z. B. Neustart der Kasse) weiterhin WL_CONNECTED, weil nur
// die IP-Adresse geprüft wird. Der tatsächliche Link-Zustand kommt vom
// WLAN-Chip; der Treiber setzt ihn beim Verbindungsverlust zurück.
bool wifiLinkUp() {
    return WiFi.status() == WL_CONNECTED &&
           cyw43_wifi_link_status(&cyw43_state, CYW43_ITF_STA) == CYW43_LINK_JOIN;
}

// Liefert true, wenn die WLAN-Verbindung neu aufgebaut werden muss: Link seit
// WIFI_LINK_LOST_MS weg oder die Kasse antwortet seit KASSE_SILENT_RECONNECT_MS
// nicht mehr, obwohl der Link scheinbar steht.
bool wifiNeedsReconnect(uint32_t now) {
    if (!wifiLinkUp()) {
        if (linkLostSinceMs == 0) {
            linkLostSinceMs = now == 0 ? 1 : now;
            Serial.println("[Schranken] WLAN-Link verloren");
        }
        return now - linkLostSinceMs >= cfg::WIFI_LINK_LOST_MS;
    }
    linkLostSinceMs = 0;
    return now - kasse.lastOkMs() >= cfg::KASSE_SILENT_RECONNECT_MS;
}

}  // namespace

void setup() {
    Serial.begin(115200);
    pinMode(LED_BUILTIN, OUTPUT);
    led.set(false);
    gates.setBootId(rp2040.hwrand32());

    bool allServosOk = true;
    for (uint8_t i = 0; i < cfg::SERVO_COUNT; ++i) {
        const bool ok = barriers[i].begin(SERVO_PINS[i]);
        allServosOk = allServosOk && ok;
        Serial.printf("[Schranken] Servo %s GP%u: %s\n",
                      SHARED_BARRIER ? "Ein-/Ausfahrt (gemeinsam)" : (i == 0 ? "Einfahrt" : "Ausfahrt"),
                      SERVO_PINS[i], ok ? "bereit" : "FEHLER");
    }
    // Selbsttest beim Start: Schranke(n) öffnen und schliessen einmal.
    runServoTest();
    setSysMessage(allServosOk ? "ok" : "err",
                  allServosOk ? "Selbsttest beim Start ausgeführt (Schranke kurz geöffnet)."
                              : "Servo-Ausgang konnte nicht eingerichtet werden.");

    WiFi.mode(WIFI_STA);
    WiFi.setTimeout(cfg::WIFI_JOIN_TIMEOUT_MS);
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
    gates.checkTimeouts();

    if (wifiNeedsReconnect(now)) {
        kasse.setOffline();
        if (now - lastWifiAttemptMs >= cfg::WIFI_RECONNECT_INTERVAL_MS) {
            Serial.println("[Schranken] WLAN getrennt bzw. Kasse stumm – neuer Verbindungsversuch");
            startWifi();
            Serial.printf("[Schranken] WLAN %s\n", wifiLinkUp() ? "wieder verbunden" : "noch nicht verbunden");
        }
        return;
    }
    if (!wifiLinkUp()) {
        kasse.setOffline();
        return;
    }

    if (now - lastStatusPollMs >= cfg::STATUS_POLL_INTERVAL_MS) {
        lastStatusPollMs = now;
        if (gates.retryPendingExit()) Serial.println("[Schranken] Ausfahrt nachgemeldet");
        kasse.pollStatus();
    }
}
