// =============================================================================
//  Pico 1 – "Kasse" (Leitstelle)
//
//  - spannt das WLAN "Parkhaus-M450" auf (Access Point, 192.168.42.1)
//  - führt Fahrzeugregister und Protokoll (park::ParkingSystem)
//  - Weboberfläche Kasse/Zahlautomat:  http://192.168.42.1/
//  - Protokoll:                        http://192.168.42.1/log  (CSV: /log.csv)
//  - HTTP-API für Pico 2 (Schranken):  /api/...  (siehe docs/HTTP-Protokoll.md)
//  - JSON für die eigene Weboberfläche: /ui/...
// =============================================================================

#include <Arduino.h>
#include <WebServer.h>
#include <WiFi.h>

#include "Config.h"
#include "EventLog.h"
#include "Format.h"
#include "ParkingSystem.h"
#include "Protocol.h"
#include "ReplayCache.h"
#include "WebOut.h"
#include "WebScript.h"
#include "WebStyle.h"

namespace {

WebServer server(cfg::HTTP_PORT);
park::ParkingSystem parking;

// Antworten auf wiederholte Anfragen (gleiche Request-ID) erneut ausliefern.
park::ReplayCache<8, 192> apiReplay;  // Pico 2 → Kasse
park::ReplayCache<4, 640> payReplay;  // Browser → Bezahlen

uint32_t nowMs() { return millis(); }

IPAddress toIp(const uint8_t (&a)[4]) { return IPAddress(a[0], a[1], a[2], a[3]); }

// ----------------------------------------------------------------------------
//  Hilfsfunktionen
// ----------------------------------------------------------------------------

struct Text16 {
    char s[16];
};

Text16 money(cfg::Money m) {
    Text16 t;
    park::formatMoney(m, t.s, sizeof(t.s));
    return t;
}

Text16 hms(uint32_t ms) {
    Text16 t;
    park::formatClock(ms, t.s, sizeof(t.s));
    return t;
}

const char* vehicleStatus(const park::Vehicle& v, uint32_t now) {
    if (v.state == park::VehicleState::Exited) return "ausgefahren";
    if (!v.hasPaid) return "im Parkhaus, unbezahlt";
    if (parking.dueFor(v, now) > 0) return "im Parkhaus, Nachzahlung fällig";
    return "im Parkhaus, bezahlt";
}

void tariffText(char* out, size_t len) {
    const park::Tariff& t = parking.tariff();
    snprintf(out, len,
             "Tarif: CHF %s pro angefangene %s (hh:mm:ss) · Freigrenze %s · Tageshöchstsatz CHF %s · "
             "Karenzzeit nach Bezahlung %s",
             money(t.pricePerUnit).s, hms(t.unitMs).s, hms(t.freeMs).s, money(t.dailyMax).s,
             hms(cfg::PAYMENT_GRACE_MS).s);
}

void pageStart(WebOut& out, const char* title, const char* refreshAction) {
    out.begin(200, "text/html; charset=utf-8");
    out.raw("<!doctype html><html lang=de><head><meta charset=utf-8>"
            "<meta name=viewport content='width=device-width,initial-scale=1'><title>");
    out.text(title);
    out.raw("</title><style>").raw(WEB_STYLE).raw("</style></head><body><header><h1>");
    out.text(title);
    out.raw("</h1><a href='/'>Kasse</a><a href='/log'>Protokoll</a>");
    out.printf("<a href='http://%u.%u.%u.%u/'>Kamera-Simulation (Pico 2)</a>", cfg::SCHRANKEN_IP[0],
               cfg::SCHRANKEN_IP[1], cfg::SCHRANKEN_IP[2], cfg::SCHRANKEN_IP[3]);
    out.raw("<span class=spacer></span><div class=head-actions>"
            "<span class=muted>Zeit seit Start: <span id=uptime>");
    out.raw(hms(nowMs()).s);
    out.raw("</span></span><span id=link class='pill ok'>verbunden</span>");
    out.printf("<button class=secondary onclick=\"%s\">Aktualisieren</button>", refreshAction);
    out.raw("</div></header><main>");
}

// ----------------------------------------------------------------------------
//  Weboberfläche Kasse / Zahlautomat
// ----------------------------------------------------------------------------

constexpr const char* KASSE_BODY = R"HTML(
<div class=grid>
<section id=paySec>
  <h2>Parkgebühr anzeigen &amp; bezahlen</h2>
  <form id=payForm>
    <label for=plate>Kennzeichen oder Ersatz-ID (MANUAL-…)</label>
    <input type=text id=plate autocomplete=off autocapitalize=characters autofocus>
    <label for=amount>Eingeworfener Betrag CHF (leer = passend bezahlen)</label>
    <input type=text id=amount inputmode=decimal autocomplete=off>
    <div class=buttons>
      <button type=submit class=secondary>Gebühr anzeigen</button>
      <button type=button id=payBtn>Bezahlen</button>
    </div>
  </form>
  <div id=payMsg class='msg out' hidden></div>
  <div id=details class=table hidden><table><tbody id=detailRows></tbody></table></div>
  <p class=muted id=tariff></p>
</section>
<section>
  <h2>Belegung</h2>
  <div class=stats>
    <div class=stat><b id=cap>–</b><span>Kapazität</span></div>
    <div class=stat><b id=occ>–</b><span>belegt</span></div>
    <div class=stat><b id=free>–</b><span>frei</span></div>
    <div class=stat><b id=rev>–</b><span>Umsatz CHF</span></div>
  </div>
  <div id=full class='msg warn' hidden>Parkhaus voll</div>
</section>
</div>
<section>
  <h2>Fahrzeugregister</h2>
  <p class=muted>Wird alle 3 s aktualisiert · <span id=updated></span> · Zeile anklicken übernimmt das Kennzeichen</p>
  <div class=table><table>
    <thead><tr><th>Kennzeichen</th><th>Einfahrt</th><th>Parkdauer</th><th>Status</th><th class=num>bezahlt CHF</th><th class=num>offen CHF</th></tr></thead>
    <tbody id=reg><tr><td colspan=6 class=muted>wird geladen …</td></tr></tbody>
  </table></div>
</section>
<script>
function cell(tr,text,cls){const td=document.createElement('td');td.textContent=text;if(cls)td.className=cls;tr.append(td);}
function renderState(s){
  $('uptime').textContent=s.uptime;
  $('cap').textContent=s.capacity; $('occ').textContent=s.occupied; $('free').textContent=s.free;
  $('rev').textContent=s.revenue; $('full').hidden=s.free>0; $('tariff').textContent=s.tariff;
  const tb=$('reg'); tb.replaceChildren();
  if(!s.vehicles.length){const tr=document.createElement('tr');cell(tr,'Register leer','muted');tr.firstChild.colSpan=6;tb.append(tr);}
  for(const v of s.vehicles){
    const tr=document.createElement('tr'); tr.style.cursor='pointer';
    cell(tr,v.id+(v.manual?' (manuell)':'')); cell(tr,v.entry); cell(tr,v.duration); cell(tr,v.status);
    cell(tr,v.paid,'num'); cell(tr,v.due,'num');
    tr.onclick=()=>{$('plate').value=v.id;$('plate').focus();}; tb.append(tr);
  }
  $('updated').textContent='zuletzt '+new Date().toLocaleTimeString();
}
let loading=false;
async function loadState(){
  if(loading) return; loading=true;
  try{ renderState(await req('/ui/state',{},1,5000)); }catch(e){}
  finally{ loading=false; }
}
function renderFee(f){
  const box=$('details'), tb=$('detailRows'); tb.replaceChildren(); box.hidden=!f; if(!f) return;
  const rows=[['Kennzeichen',f.id],['Parkdauer',f.duration],['Gebühr gesamt','CHF '+f.fee],
              ['bereits bezahlt','CHF '+f.paid],['offener Betrag','CHF '+f.due]];
  if(f.grace) rows.push(['Ausfahrt ohne Nachzahlung noch',f.grace]);
  for(const [k,v] of rows){const tr=document.createElement('tr');const th=document.createElement('th');th.textContent=k;tr.append(th);cell(tr,v);tb.append(tr);}
}
async function kasseAction(kind){
  const sec=$('paySec'); const data={plate:$('plate').value};
  if(kind==='pay'){ data.amount=$('amount').value; data.rid=newRid(); }
  busy(sec,true,kind==='pay'?'Zahlung wird verarbeitet …':'Gebühr wird berechnet …');
  try{
    const r=await post('/ui/'+kind,data);
    busy(sec,false); show($('payMsg'),r.cls,r.msg); renderFee(r.fee);
    if(kind==='pay'&&r.ok) $('amount').value='';
  }catch(e){
    busy(sec,false); show($('payMsg'),'err','Keine Antwort von der Kasse – bitte erneut versuchen'); renderFee(null);
  }
  loadState();
}
// Kein onclick-Attribut im Formular: dort würde "action" auf form.action (Text) zeigen.
$('payForm').addEventListener('submit',e=>{e.preventDefault();kasseAction('fee');});
$('payBtn').addEventListener('click',()=>kasseAction('pay'));
loadState(); setInterval(loadState,3000);
</script>
)HTML";

void handleRoot() {
    WebOut out(server);
    pageStart(out, "Parkhaus – Kasse / Zahlautomat", "loadState()");
    out.raw("<script>").raw(WEB_SCRIPT_COMMON).raw("</script>");
    out.raw(KASSE_BODY).raw("</main></body></html>");
}

void handleUiState() {
    const uint32_t now = nowMs();
    char tariff[200];
    tariffText(tariff, sizeof(tariff));

    WebOut out(server);
    out.begin(200, "application/json");
    out.raw("{").field("uptime", hms(now).s, true);
    out.printf(",\"capacity\":%u,\"occupied\":%u,\"free\":%u", parking.capacity(), parking.occupied(),
               parking.freeSpaces());
    out.field("revenue", money(parking.revenue()).s).field("tariff", tariff);
    out.raw(",\"vehicles\":[");
    bool first = true;
    // Zuerst Fahrzeuge im Parkhaus, danach ausgefahrene.
    for (int pass = 0; pass < 2; ++pass) {
        const auto wanted = pass == 0 ? park::VehicleState::Inside : park::VehicleState::Exited;
        for (size_t i = 0; i < parking.registerSize(); ++i) {
            const park::Vehicle& v = parking.vehicleAt(i);
            if (!v.used || v.state != wanted) continue;
            const uint32_t end = v.state == park::VehicleState::Inside ? now : v.exitMs;
            out.raw(first ? "{" : ",{");
            first = false;
            out.field("id", v.id, true);
            out.printf(",\"manual\":%s", v.manual ? "true" : "false");
            out.field("entry", hms(v.entryMs).s).field("duration", hms(end - v.entryMs).s);
            out.field("status", vehicleStatus(v, now)).field("paid", money(v.paidTotal).s);
            out.field("due", money(parking.dueFor(v, now)).s);
            out.raw("}");
        }
    }
    out.raw("]}");
}

// Baut die JSON-Antwort für Gebührenabfrage und Zahlung.
void feeJson(char* out, size_t len, bool ok, const char* cls, const char* msg,
             const park::FeeResult* fee) {
    char m[200];
    park::jsonEscape(msg, m, sizeof(m));
    int n = snprintf(out, len, "{\"ok\":%s,\"cls\":\"%s\",\"msg\":\"%s\",\"fee\":", ok ? "true" : "false",
                     cls, m);
    if (n < 0 || static_cast<size_t>(n) >= len) return;
    if (fee == nullptr || !fee->ok) {
        snprintf(out + n, len - n, "null}");
        return;
    }
    snprintf(out + n, len - n,
             "{\"id\":\"%s\",\"duration\":\"%s\",\"fee\":\"%s\",\"paid\":\"%s\",\"due\":\"%s\","
             "\"grace\":\"%s\"}}",
             fee->id, hms(fee->durationMs).s, money(fee->fee).s, money(fee->paid).s,
             money(fee->due).s, fee->inGrace ? hms(fee->graceLeftMs).s : "");
}

void handleUiFee() {
    char plate[48];
    readArg(server, "plate", plate, sizeof(plate));
    const park::FeeResult fee = parking.queryFee(plate, nowMs());

    char msg[120];
    const char* cls;
    if (!fee.ok) {
        cls = "err";
        snprintf(msg, sizeof(msg), "%s", park::reasonMessage(fee.reason));
    } else if (fee.due == 0) {
        cls = "ok";
        snprintf(msg, sizeof(msg), "Offener Betrag: CHF 0.00 – nichts zu bezahlen");
    } else {
        cls = "warn";
        snprintf(msg, sizeof(msg), "Offener Betrag: CHF %s", money(fee.due).s);
    }

    char body[640];
    feeJson(body, sizeof(body), fee.ok, cls, msg, &fee);
    sendAndClose(server, 200, "application/json", body);
}

void handleUiPay() {
    char rid[park::ReplayCache<1, 1>::ID_LEN];
    readArg(server, proto::P_RID, rid, sizeof(rid));
    if (const char* cached = payReplay.find(rid)) {
        sendAndClose(server, 200, "application/json", cached);
        return;
    }

    char plate[48];
    char amountText[24];
    readArg(server, "plate", plate, sizeof(plate));
    readArg(server, "amount", amountText, sizeof(amountText));
    const uint32_t now = nowMs();

    char body[640];
    cfg::Money tendered = 0;
    const bool hasTendered = amountText[0] != '\0';
    if (hasTendered && !park::parseMoney(amountText, tendered)) {
        feeJson(body, sizeof(body), false, "err", "Betrag ungültig", nullptr);
        sendAndClose(server, 200, "application/json", body);
        return;
    }

    const park::PaymentResult pay = parking.pay(plate, hasTendered, tendered, now);
    char msg[120];
    const char* cls = pay.ok ? "ok" : "err";
    if (pay.ok && pay.reason == park::Reason::NothingDue) {
        snprintf(msg, sizeof(msg), "nichts zu bezahlen – Status: bezahlt");
    } else if (pay.ok) {
        snprintf(msg, sizeof(msg), "Bezahlt: CHF %s%s%s – Status: bezahlt", money(pay.amount).s,
                 pay.change > 0 ? ", Rückgeld CHF " : "", pay.change > 0 ? money(pay.change).s : "");
    } else if (pay.reason == park::Reason::Insufficient) {
        snprintf(msg, sizeof(msg), "Betrag nicht ausreichend – offen: CHF %s", money(pay.amount).s);
    } else {
        snprintf(msg, sizeof(msg), "Zahlung abgelehnt: %s", park::reasonMessage(pay.reason));
    }

    const park::FeeResult fee = parking.queryFee(plate, now);
    feeJson(body, sizeof(body), pay.ok, cls, msg, &fee);
    payReplay.store(rid, body);
    sendAndClose(server, 200, "application/json", body);
}

// ----------------------------------------------------------------------------
//  Protokoll
// ----------------------------------------------------------------------------

void handleLog() {
    const park::EventLog& log = parking.log();
    WebOut out(server);
    pageStart(out, "Parkhaus – Protokoll", "location.reload()");
    out.raw("<section><h2>Protokoll aller Ein-/Ausfahrten und Zahlungen</h2>");
    out.printf("<p class=muted>%u von max. %u Einträgen · älteste zuerst · Stand %s · "
               "<a href='/log.csv'>als CSV herunterladen</a></p>",
               static_cast<unsigned>(log.size()), static_cast<unsigned>(cfg::LOG_CAPACITY),
               hms(nowMs()).s);
    if (log.overwrittenCount() > 0) {
        out.printf("<div class='msg warn'>Obergrenze erreicht: %lu ältere Einträge wurden "
                   "überschrieben (Nr. 1–%lu fehlen).</div>",
                   static_cast<unsigned long>(log.overwrittenCount()),
                   static_cast<unsigned long>(log.overwrittenCount()));
    }
    out.raw("<div class=table><table><tr><th class=num>Nr.</th><th>Zeit</th><th>Ereignis</th>"
            "<th>Kennzeichen</th><th class=num>Betrag CHF</th><th>Grund</th><th>manuell</th></tr>");
    for (size_t i = 0; i < log.size(); ++i) {
        const park::LogEntry& e = log.at(i);
        out.printf("<tr><td class=num>%lu</td><td>%s</td><td>", static_cast<unsigned long>(e.seq),
                   hms(e.timeMs).s);
        out.raw(park::eventTypeText(e.type)).raw("</td><td>").text(e.id).raw("</td><td class=num>");
        if (e.hasAmount) out.raw(money(e.amount).s);
        out.raw("</td><td>").raw(park::reasonText(e.reason)).raw("</td><td>");
        out.raw(e.manual ? "ja" : "nein").raw("</td></tr>");
    }
    if (log.size() == 0) out.raw("<tr><td colspan=7 class=muted>Protokoll leer</td></tr>");
    out.raw("</table></div>");

    out.raw("<h2 style='margin-top:16px'>CSV</h2><pre>");
    out.raw(park::EventLog::CSV_HEADER).raw("\n");
    char line[128];
    for (size_t i = 0; i < log.size(); ++i) {
        park::EventLog::formatCsv(log.at(i), line, sizeof(line));
        out.text(line).raw("\n");
    }
    out.raw("</pre></section></main></body></html>");
}

void handleLogCsv() {
    const park::EventLog& log = parking.log();
    char overwritten[12];
    snprintf(overwritten, sizeof(overwritten), "%lu", static_cast<unsigned long>(log.overwrittenCount()));
    server.sendHeader("X-Log-Overwritten", overwritten);
    server.sendHeader("Content-Disposition", "inline; filename=parkhaus-protokoll.csv");

    WebOut out(server);
    out.begin(200, "text/csv; charset=utf-8");
    out.raw(park::EventLog::CSV_HEADER).raw("\n");
    char line[128];
    for (size_t i = 0; i < log.size(); ++i) {
        park::EventLog::formatCsv(log.at(i), line, sizeof(line));
        out.raw(line).raw("\n");
    }
}

// ----------------------------------------------------------------------------
//  API für Pico 2 (Schranken)
// ----------------------------------------------------------------------------

void sendApi(int code, const char* body) { sendAndClose(server, code, "text/plain; charset=utf-8", body); }

bool isManualRequest() { return server.hasArg(proto::P_MANUAL) && server.arg(proto::P_MANUAL) == "1"; }

// Wiederholte Anfrage? Dann gespeicherte Antwort senden und true liefern.
bool replayIfKnown(char* rid, size_t ridLen) {
    readArg(server, proto::P_RID, rid, ridLen);
    if (const char* cached = apiReplay.find(rid)) {
        sendApi(200, cached);
        return true;
    }
    return false;
}

void answerApi(const char* rid, const char* body) {
    apiReplay.store(rid, body);
    sendApi(200, body);
}

void handleApiEntry() {
    char rid[park::ReplayCache<1, 1>::ID_LEN];
    if (replayIfKnown(rid, sizeof(rid))) return;

    const uint32_t now = nowMs();
    park::EntryResult r;
    if (isManualRequest()) {
        r = parking.requestManualEntry(now);
    } else if (server.hasArg(proto::P_PLATE)) {
        char plate[48];
        readArg(server, proto::P_PLATE, plate, sizeof(plate));
        r = parking.requestEntry(plate, now);
    } else {
        sendApi(400, "result=ERROR\nreason=MISSING_PLATE\n");
        return;
    }

    char body[160];
    snprintf(body, sizeof(body), "%s=%s\n%s=%s\n%s=%s\n%s=%u\n", proto::F_RESULT,
             r.granted ? proto::R_GRANTED : proto::R_DENIED, proto::F_REASON,
             park::reasonToken(r.reason), proto::F_ID, r.id, proto::F_FREE, parking.freeSpaces());
    answerApi(rid, body);
}

void handleApiExitCheck() {
    char rid[park::ReplayCache<1, 1>::ID_LEN];
    if (replayIfKnown(rid, sizeof(rid))) return;
    if (!server.hasArg(proto::P_PLATE)) {
        sendApi(400, "result=ERROR\nreason=MISSING_PLATE\n");
        return;
    }
    char plate[48];
    readArg(server, proto::P_PLATE, plate, sizeof(plate));
    const park::ExitResult r = parking.requestExit(plate, isManualRequest(), nowMs());

    char body[160];
    snprintf(body, sizeof(body), "%s=%s\n%s=%s\n%s=%s\n%s=%ld\n%s=%u\n", proto::F_RESULT,
             r.open ? proto::R_OPEN : proto::R_DENIED, proto::F_REASON, park::reasonToken(r.reason),
             proto::F_ID, r.id, proto::F_DUE, static_cast<long>(r.due), proto::F_FREE,
             parking.freeSpaces());
    answerApi(rid, body);
}

void handleApiExitConfirm() {
    char rid[park::ReplayCache<1, 1>::ID_LEN];
    if (replayIfKnown(rid, sizeof(rid))) return;
    if (!server.hasArg(proto::P_PLATE)) {
        sendApi(400, "result=ERROR\nreason=MISSING_PLATE\n");
        return;
    }
    char plate[48];
    readArg(server, proto::P_PLATE, plate, sizeof(plate));
    const park::Reason reason = parking.confirmExit(plate, isManualRequest(), nowMs());

    char body[128];
    snprintf(body, sizeof(body), "%s=%s\n%s=%s\n%s=%u\n", proto::F_RESULT,
             reason == park::Reason::None ? proto::R_OK : proto::R_ERROR, proto::F_REASON,
             park::reasonToken(reason), proto::F_FREE, parking.freeSpaces());
    answerApi(rid, body);
}

void handleApiExitAbort() {
    char rid[park::ReplayCache<1, 1>::ID_LEN];
    if (replayIfKnown(rid, sizeof(rid))) return;
    if (!server.hasArg(proto::P_PLATE)) {
        sendApi(400, "result=ERROR\nreason=MISSING_PLATE\n");
        return;
    }
    char plate[48];
    readArg(server, proto::P_PLATE, plate, sizeof(plate));
    parking.abortExit(plate, isManualRequest(), nowMs());
    answerApi(rid, "result=OK\n");
}

void handleApiStatus() {
    char body[96];
    snprintf(body, sizeof(body), "%s=%s\n%s=%u\n%s=%u\n%s=%u\n", proto::F_RESULT, proto::R_OK,
             proto::F_CAPACITY, parking.capacity(), proto::F_OCCUPIED, parking.occupied(),
             proto::F_FREE, parking.freeSpaces());
    sendApi(200, body);
}

void handleNotFound() { sendAndClose(server, 404, "text/plain; charset=utf-8", "Nicht gefunden\n"); }

}  // namespace

void setup() {
    Serial.begin(115200);
    pinMode(LED_BUILTIN, OUTPUT);
    digitalWrite(LED_BUILTIN, LOW);

    WiFi.mode(WIFI_AP);
    WiFi.softAPConfig(toIp(cfg::KASSE_IP), toIp(cfg::KASSE_IP), toIp(cfg::NETMASK));
    const bool apOk = WiFi.softAP(cfg::WIFI_SSID, cfg::WIFI_PASSWORD);

    server.on("/", HTTP_GET, handleRoot);
    server.on("/ui/state", HTTP_GET, handleUiState);
    server.on("/ui/fee", HTTP_POST, handleUiFee);
    server.on("/ui/pay", HTTP_POST, handleUiPay);
    server.on("/log", HTTP_GET, handleLog);
    server.on("/log.csv", HTTP_GET, handleLogCsv);
    server.on(proto::PATH_ENTRY, HTTP_POST, handleApiEntry);
    server.on(proto::PATH_EXIT_CHECK, HTTP_POST, handleApiExitCheck);
    server.on(proto::PATH_EXIT_CONFIRM, HTTP_POST, handleApiExitConfirm);
    server.on(proto::PATH_EXIT_ABORT, HTTP_POST, handleApiExitAbort);
    server.on(proto::PATH_STATUS, HTTP_GET, handleApiStatus);
    server.onNotFound(handleNotFound);
    server.begin();

    // Onboard-LED an = WLAN/Leitstelle bereit.
    digitalWrite(LED_BUILTIN, apOk ? HIGH : LOW);
    Serial.printf("[Kasse] WLAN '%s' %s, Weboberflaeche: http://%s/\n", cfg::WIFI_SSID,
                  apOk ? "aktiv" : "FEHLER", WiFi.softAPIP().toString().c_str());
}

void loop() { server.handleClient(); }
