// =============================================================================
//  Pico 1 – "Kasse" (Leitstelle)
//
//  - spannt das WLAN "Parkhaus-M450" auf (Access Point, 192.168.42.1)
//  - führt Fahrzeugregister und Protokoll (park::ParkingSystem)
//  - Weboberfläche Kasse/Zahlautomat:  http://192.168.42.1/
//  - Protokoll:                        http://192.168.42.1/log  (CSV: /log.csv)
//  - HTTP-API für Pico 2 (Schranken):  /api/...  (siehe docs/HTTP-Protokoll.md)
// =============================================================================

#include <Arduino.h>
#include <WebServer.h>
#include <WiFi.h>

#include "Config.h"
#include "EventLog.h"
#include "Format.h"
#include "ParkingSystem.h"
#include "Protocol.h"
#include "WebOut.h"
#include "WebStyle.h"

namespace {

WebServer server(cfg::HTTP_PORT);
park::ParkingSystem parking;

uint32_t nowMs() { return millis(); }

IPAddress toIp(const uint8_t (&a)[4]) { return IPAddress(a[0], a[1], a[2], a[3]); }

// ----------------------------------------------------------------------------
//  Hilfsfunktionen für die Anzeige
// ----------------------------------------------------------------------------

const char* vehicleStatus(const park::Vehicle& v, uint32_t now) {
    if (v.state == park::VehicleState::Exited) return "ausgefahren";
    const cfg::Money due = parking.dueFor(v, now);
    if (!v.hasPaid) return "im Parkhaus, unbezahlt";
    if (due > 0) return "im Parkhaus, Nachzahlung fällig";
    return "im Parkhaus, bezahlt";
}

void printMoney(WebOut& out, cfg::Money m) {
    char buf[16];
    park::formatMoney(m, buf, sizeof(buf));
    out.raw(buf);
}

void printClock(WebOut& out, uint32_t ms) {
    char buf[16];
    park::formatClock(ms, buf, sizeof(buf));
    out.raw(buf);
}

void pageHeader(WebOut& out, const char* title) {
    out.begin(200, "text/html; charset=utf-8");
    out.raw("<!doctype html><html lang=de><head><meta charset=utf-8>"
            "<meta name=viewport content='width=device-width,initial-scale=1'><title>");
    out.text(title);
    out.raw("</title><style>").raw(WEB_STYLE).raw("</style></head><body><header><h1>");
    out.text(title);
    out.raw("</h1><a href='/'>Kasse</a><a href='/log'>Protokoll</a>");
    out.printf("<a href='http://%u.%u.%u.%u/'>Kamera-Simulation (Pico 2)</a>", cfg::SCHRANKEN_IP[0],
               cfg::SCHRANKEN_IP[1], cfg::SCHRANKEN_IP[2], cfg::SCHRANKEN_IP[3]);
    out.raw("<span class=muted>Zeit seit Start: ");
    printClock(out, nowMs());
    out.raw("</span></header><main>");
}

void pageFooter(WebOut& out) {
    out.raw("</main></body></html>");
    out.end();
}

void occupancySection(WebOut& out) {
    out.raw("<section><h2>Belegung</h2><div class=stats>");
    out.printf("<div class=stat><b>%u</b><span>Kapazität</span></div>", parking.capacity());
    out.printf("<div class=stat><b>%u</b><span>belegt</span></div>", parking.occupied());
    out.printf("<div class=stat><b id=free>%u</b><span>frei</span></div>", parking.freeSpaces());
    out.raw("<div class=stat><b>");
    printMoney(out, parking.revenue());
    out.raw("</b><span>Umsatz CHF</span></div></div>");
    if (parking.freeSpaces() == 0) out.raw("<div class='msg warn'>Parkhaus voll</div>");
    out.raw("</section>");
}

void registerSection(WebOut& out) {
    const uint32_t now = nowMs();
    out.raw("<section><h2>Fahrzeugregister</h2><div class=table><table><tr><th>Kennzeichen</th>"
            "<th>Einfahrt</th><th>Parkdauer</th><th>Status</th><th class=num>bezahlt CHF</th>"
            "<th class=num>offen CHF</th></tr>");
    bool any = false;
    // Zuerst Fahrzeuge im Parkhaus, danach ausgefahrene.
    for (int pass = 0; pass < 2; ++pass) {
        const auto wanted = pass == 0 ? park::VehicleState::Inside : park::VehicleState::Exited;
        for (size_t i = 0; i < parking.registerSize(); ++i) {
            const park::Vehicle& v = parking.vehicleAt(i);
            if (!v.used || v.state != wanted) continue;
            any = true;
            out.raw("<tr><td>").text(v.id).raw(v.manual ? " <span class=muted>(manuell)</span>" : "");
            out.raw("</td><td>");
            printClock(out, v.entryMs);
            out.raw("</td><td>");
            printClock(out, (v.state == park::VehicleState::Inside ? now : v.exitMs) - v.entryMs);
            out.raw("</td><td>").raw(vehicleStatus(v, now)).raw("</td><td class=num>");
            printMoney(out, v.paidTotal);
            out.raw("</td><td class=num>");
            printMoney(out, parking.dueFor(v, now));
            out.raw("</td></tr>");
        }
    }
    if (!any) out.raw("<tr><td colspan=6 class=muted>Register leer</td></tr>");
    out.raw("</table></div></section>");
}

// Ergebnis einer Gebührenabfrage oder Zahlung.
struct Notice {
    const char* cls = nullptr;  // "ok" | "err" | "warn"
    char text[160] = "";
};

void feeDetails(WebOut& out, const park::FeeResult& fee) {
    out.raw("<div class=table><table>");
    out.raw("<tr><th>Kennzeichen</th><td>").text(fee.id).raw("</td></tr>");
    out.raw("<tr><th>Parkdauer</th><td>");
    printClock(out, fee.durationMs);
    out.raw("</td></tr><tr><th>Gebühr gesamt</th><td>CHF ");
    printMoney(out, fee.fee);
    out.raw("</td></tr><tr><th>bereits bezahlt</th><td>CHF ");
    printMoney(out, fee.paid);
    out.raw("</td></tr><tr><th>offener Betrag</th><td><b>CHF ");
    printMoney(out, fee.due);
    out.raw("</b></td></tr>");
    if (fee.inGrace) {
        out.raw("<tr><th>Ausfahrt ohne Nachzahlung bis</th><td>noch ");
        printClock(out, fee.graceLeftMs);
        out.raw("</td></tr>");
    }
    out.raw("</table></div>");
}

void tariffInfo(WebOut& out) {
    const park::Tariff& t = parking.tariff();
    out.raw("<p class=muted>Tarif: CHF ");
    printMoney(out, t.pricePerUnit);
    out.raw(" pro angefangene ");
    printClock(out, t.unitMs);
    out.raw(" (hh:mm:ss) · Freigrenze ");
    printClock(out, t.freeMs);
    out.raw(" · Tageshöchstsatz CHF ");
    printMoney(out, t.dailyMax);
    out.raw(" · Karenzzeit nach Bezahlung ");
    printClock(out, cfg::PAYMENT_GRACE_MS);
    out.raw("</p>");
}

void renderKasse(const char* plate, const char* amount, const Notice& notice,
                 const park::FeeResult* fee) {
    WebOut out(server);
    pageHeader(out, "Parkhaus – Kasse / Zahlautomat");

    out.raw("<div class=grid><section><h2>Parkgebühr anzeigen &amp; bezahlen</h2>"
            "<form method=post action='/kasse'>"
            "<label for=plate>Kennzeichen oder Ersatz-ID (MANUAL-…)</label>"
            "<input type=text id=plate name=plate autocomplete=off autocapitalize=characters value='");
    out.text(plate);
    out.raw("'><label for=amount>Eingeworfener Betrag CHF (leer = passend bezahlen)</label>"
            "<input type=text id=amount name=amount inputmode=decimal autocomplete=off value='");
    out.text(amount);
    out.raw("'><div class=buttons><button name=action value=fee class=secondary>Gebühr anzeigen</button>"
            "<button name=action value=pay>Bezahlen</button></div></form>");

    if (notice.cls != nullptr) {
        out.printf("<div class='msg %s'>", notice.cls);
        out.text(notice.text);
        out.raw("</div>");
    }
    if (fee != nullptr && fee->ok) feeDetails(out, *fee);
    tariffInfo(out);
    out.raw("</section>");

    occupancySection(out);
    out.raw("</div>");
    registerSection(out);
    pageFooter(out);
}

// ----------------------------------------------------------------------------
//  Weboberfläche Kasse
// ----------------------------------------------------------------------------

void handleKasse() {
    char plate[48];
    char amountText[24];
    char action[8];
    readArg(server, "plate", plate, sizeof(plate));
    readArg(server, "amount", amountText, sizeof(amountText));
    readArg(server, "action", action, sizeof(action));

    Notice notice;
    const uint32_t now = nowMs();

    if (strcmp(action, "fee") == 0) {
        const park::FeeResult fee = parking.queryFee(plate, now);
        if (!fee.ok) {
            notice.cls = "err";
            snprintf(notice.text, sizeof(notice.text), "%s", park::reasonMessage(fee.reason));
        } else if (fee.due == 0) {
            notice.cls = "ok";
            snprintf(notice.text, sizeof(notice.text), "Offener Betrag: CHF 0.00 – nichts zu bezahlen");
        } else {
            char due[16];
            park::formatMoney(fee.due, due, sizeof(due));
            notice.cls = "warn";
            snprintf(notice.text, sizeof(notice.text), "Offener Betrag: CHF %s", due);
        }
        renderKasse(plate, "", notice, &fee);
        return;
    }

    if (strcmp(action, "pay") == 0) {
        cfg::Money tendered = 0;
        const bool hasTendered = amountText[0] != '\0';
        if (hasTendered && !park::parseMoney(amountText, tendered)) {
            notice.cls = "err";
            snprintf(notice.text, sizeof(notice.text), "Betrag ungültig");
            renderKasse(plate, amountText, notice, nullptr);
            return;
        }

        const park::PaymentResult pay = parking.pay(plate, hasTendered, tendered, now);
        char a[16];
        char c[16];
        park::formatMoney(pay.amount, a, sizeof(a));
        park::formatMoney(pay.change, c, sizeof(c));

        if (pay.ok && pay.reason == park::Reason::NothingDue) {
            notice.cls = "ok";
            snprintf(notice.text, sizeof(notice.text), "nichts zu bezahlen – Status: bezahlt");
        } else if (pay.ok) {
            notice.cls = "ok";
            snprintf(notice.text, sizeof(notice.text), "Bezahlt: CHF %s%s%s – Status: bezahlt", a,
                     pay.change > 0 ? ", Rückgeld CHF " : "", pay.change > 0 ? c : "");
        } else if (pay.reason == park::Reason::Insufficient) {
            notice.cls = "err";
            snprintf(notice.text, sizeof(notice.text), "Betrag nicht ausreichend – offen: CHF %s", a);
        } else {
            notice.cls = "err";
            snprintf(notice.text, sizeof(notice.text), "Zahlung abgelehnt: %s",
                     park::reasonMessage(pay.reason));
        }
        const park::FeeResult fee = parking.queryFee(plate, now);
        renderKasse(plate, "", notice, pay.ok ? &fee : nullptr);
        return;
    }

    renderKasse(plate, "", notice, nullptr);
}

// ----------------------------------------------------------------------------
//  Protokoll
// ----------------------------------------------------------------------------

void handleLog() {
    const park::EventLog& log = parking.log();
    WebOut out(server);
    pageHeader(out, "Parkhaus – Protokoll");
    out.raw("<section><h2>Protokoll aller Ein-/Ausfahrten und Zahlungen</h2>");
    out.printf("<p class=muted>%u von max. %u Einträgen · älteste zuerst · "
               "<a href='/log.csv'>als CSV herunterladen</a></p>",
               static_cast<unsigned>(log.size()), static_cast<unsigned>(cfg::LOG_CAPACITY));
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
        out.printf("<tr><td class=num>%lu</td><td>", static_cast<unsigned long>(e.seq));
        printClock(out, e.timeMs);
        out.raw("</td><td>").raw(park::eventTypeText(e.type)).raw("</td><td>").text(e.id);
        out.raw("</td><td class=num>");
        if (e.hasAmount) printMoney(out, e.amount);
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
    out.raw("</pre></section>");
    pageFooter(out);
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
    out.end();
}

// ----------------------------------------------------------------------------
//  API für Pico 2 (Schranken)
// ----------------------------------------------------------------------------

void sendApi(int code, const char* body) { server.send(code, "text/plain; charset=utf-8", body); }

bool isManualRequest() { return server.hasArg(proto::P_MANUAL) && server.arg(proto::P_MANUAL) == "1"; }

void handleApiEntry() {
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
    sendApi(200, body);
}

void handleApiExitCheck() {
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
    sendApi(200, body);
}

void handleApiExitConfirm() {
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
    sendApi(200, body);
}

void handleApiExitAbort() {
    if (!server.hasArg(proto::P_PLATE)) {
        sendApi(400, "result=ERROR\nreason=MISSING_PLATE\n");
        return;
    }
    char plate[48];
    readArg(server, proto::P_PLATE, plate, sizeof(plate));
    parking.abortExit(plate, isManualRequest(), nowMs());
    sendApi(200, "result=OK\n");
}

void handleApiStatus() {
    char body[96];
    snprintf(body, sizeof(body), "%s=%s\n%s=%u\n%s=%u\n%s=%u\n", proto::F_RESULT, proto::R_OK,
             proto::F_CAPACITY, parking.capacity(), proto::F_OCCUPIED, parking.occupied(),
             proto::F_FREE, parking.freeSpaces());
    sendApi(200, body);
}

void handleNotFound() { server.send(404, "text/plain; charset=utf-8", "Nicht gefunden\n"); }

}  // namespace

void setup() {
    Serial.begin(115200);
    pinMode(LED_BUILTIN, OUTPUT);
    digitalWrite(LED_BUILTIN, LOW);

    WiFi.mode(WIFI_AP);
    WiFi.softAPConfig(toIp(cfg::KASSE_IP), toIp(cfg::KASSE_IP), toIp(cfg::NETMASK));
    const bool apOk = WiFi.softAP(cfg::WIFI_SSID, cfg::WIFI_PASSWORD);

    server.on("/", HTTP_GET, handleKasse);
    server.on("/kasse", handleKasse);
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
