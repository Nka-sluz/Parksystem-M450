// Komponententests der hardwareunabhängigen Logik (lib/ParkCore) auf dem PC.
// Die Szenarien folgen den Testfällen des Testkonzepts (Testkonfiguration:
// 3 Plätze, Einheit/Freigrenze 1 Min., CHF 1.00/Min., Höchstsatz CHF 5.00,
// Karenzzeit 2 Min., Protokoll 10 Einträge).
//
// Build & Run: siehe test/host/README.md

#include <stdio.h>
#include <string.h>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#endif

#include "Config.h"
#include "EventLog.h"
#include "Format.h"
#include "ParkingSystem.h"
#include "Plate.h"
#include "Protocol.h"
#include "ReplayCache.h"
#include "Tariff.h"

static_assert(PARK_TEST_CONFIG == 1, "Tests erwarten die Testkonfiguration");

namespace {

int failures = 0;
int checks = 0;

#define CHECK(cond)                                                             \
    do {                                                                        \
        ++checks;                                                               \
        if (!(cond)) {                                                          \
            ++failures;                                                         \
            printf("  FEHLER %s:%d: %s\n", __FILE__, __LINE__, #cond);          \
        }                                                                       \
    } while (0)

constexpr uint32_t S = 1000;
constexpr uint32_t MIN = 60 * S;
constexpr cfg::Money TARIF = cfg::TARIFF_PRICE_PER_UNIT;

using park::EventType;
using park::ParkingSystem;
using park::Reason;

const park::Vehicle* findVehicle(const ParkingSystem& p, const char* id) {
    for (size_t i = 0; i < p.registerSize(); ++i) {
        const park::Vehicle& v = p.vehicleAt(i);
        if (v.used && strcmp(v.id, id) == 0) return &v;
    }
    return nullptr;
}

const park::LogEntry& lastLog(const ParkingSystem& p) { return p.log().at(p.log().size() - 1); }

void enter(ParkingSystem& p, const char* plate, uint32_t t) {
    const auto r = p.requestEntry(plate, t);
    CHECK(r.granted);
}

void testPlates() {
    puts("Kennzeichenprüfung (Testdaten, TC-06/07/41)");
    char out[cfg::ID_BUF_LEN];
    CHECK(park::normalizeId("ZH-100001", out, sizeof(out), false) == park::PlateCheck::Ok);
    CHECK(park::normalizeId("  zh-100002 ", out, sizeof(out), false) == park::PlateCheck::Ok);
    CHECK(strcmp(out, "ZH-100002") == 0);
    CHECK(park::normalizeId("XX-999999", out, sizeof(out), false) == park::PlateCheck::Ok);
    CHECK(park::normalizeId("", out, sizeof(out), false) == park::PlateCheck::Empty);
    CHECK(park::normalizeId("   ", out, sizeof(out), false) == park::PlateCheck::Empty);
    CHECK(park::normalizeId("ZH-!!??", out, sizeof(out), false) == park::PlateCheck::BadFormat);
    CHECK(park::normalizeId("ZH-12345678901", out, sizeof(out), false) == park::PlateCheck::TooLong);
    CHECK(park::normalizeId("<b>ZH</b>", out, sizeof(out), false) == park::PlateCheck::BadFormat);
    CHECK(park::normalizeId("ZHZH", out, sizeof(out), false) == park::PlateCheck::BadFormat);
    char longInput[501];
    memset(longInput, 'A', 500);
    longInput[500] = '\0';
    CHECK(park::normalizeId(longInput, out, sizeof(out), true) == park::PlateCheck::TooLong);
    CHECK(park::normalizeId("MANUAL-1", out, sizeof(out), false) == park::PlateCheck::Reserved);
    CHECK(park::normalizeId("manual-1", out, sizeof(out), true) == park::PlateCheck::Ok);
    CHECK(strcmp(out, "MANUAL-1") == 0);

    char log[cfg::ID_BUF_LEN];
    park::sanitizeForLog("", log, sizeof(log));
    CHECK(strcmp(log, "(leer)") == 0);
    park::sanitizeForLog("a;b\"c", log, sizeof(log));
    CHECK(strcmp(log, "a?b?c") == 0);
    park::sanitizeForLog(longInput, log, sizeof(log));
    CHECK(strlen(log) == cfg::ID_BUF_LEN - 1 && log[cfg::ID_BUF_LEN - 2] == '~');
}

void testTariff() {
    puts("Tarif (US3, TC-11/12/13)");
    const park::Tariff t = park::Tariff::fromConfig();
    CHECK(t.feeFor(0) == 0);
    CHECK(t.feeFor(30 * S) == 0);             // TC-12
    CHECK(t.feeFor(MIN - 1) == 0);            // knapp unter Freigrenze
    CHECK(t.feeFor(MIN) == 1 * TARIF);        // genau 1 Einheit
    CHECK(t.feeFor(MIN + 1) == 2 * TARIF);    // angefangene Einheit
    CHECK(t.feeFor(3 * MIN + 20 * S) == 4 * TARIF);  // TC-11
    CHECK(t.feeFor(2 * MIN + 10 * S) == 3 * TARIF);  // TC-15 (Basis)
    CHECK(t.feeFor(7 * MIN) == 5 * TARIF);           // TC-13 Höchstsatz
    CHECK(t.feeFor(cfg::TARIFF_DAY_MS + 1 * MIN) == cfg::TARIFF_DAILY_MAX + 1 * TARIF);
}

void testUs1Us2() {
    puts("US1/US2 Einfahrt (TC-01..TC-10)");
    ParkingSystem p;
    auto r = p.requestEntry("ZH-100001", 1 * S);  // TC-01
    CHECK(r.granted && strcmp(r.id, "ZH-100001") == 0);
    const park::Vehicle* v = findVehicle(p, "ZH-100001");  // TC-02
    CHECK(v != nullptr && v->state == park::VehicleState::Inside && !v->hasPaid && v->entryMs == 1 * S);
    CHECK(p.freeSpaces() == 2);
    CHECK(lastLog(p).type == EventType::Entry);

    r = p.requestEntry("ZH-100001", 2 * S);  // TC-03
    CHECK(!r.granted && r.reason == Reason::Duplicate);
    CHECK(p.freeSpaces() == 2);

    // Grundzustand für US2
    p.reset();
    CHECK(p.requestEntry("", 1 * S).reason == Reason::Invalid);                // TC-06
    CHECK(p.requestEntry("ZH-!!??", 2 * S).reason == Reason::Invalid);         // TC-07
    CHECK(p.requestEntry("ZH-12345678901", 3 * S).reason == Reason::Invalid);  // TC-07
    enter(p, "ZH-100001", 4 * S);
    enter(p, "ZH-100002", 5 * S);
    enter(p, "ZH-100003", 6 * S);
    CHECK(p.freeSpaces() == 0);
    CHECK(p.requestEntry("ZH-100004", 7 * S).reason == Reason::Full);  // TC-08
    CHECK(p.requestManualEntry(8 * S).reason == Reason::Full);         // TC-09
    CHECK(findVehicle(p, "ZH-100004") == nullptr);                     // TC-10
    CHECK(findVehicle(p, "MANUAL-1") == nullptr);
    CHECK(p.freeSpaces() == 0);

    int denied = 0;
    for (size_t i = 0; i < p.log().size(); ++i) {
        if (p.log().at(i).type == EventType::EntryDenied) ++denied;
    }
    CHECK(denied == 5);
}

void testUs3Us4() {
    puts("US3/US4 Gebühr & Bezahlung (TC-11..TC-21)");
    ParkingSystem p;
    enter(p, "ZH-100001", 0);
    auto fee = p.queryFee("ZH-100001", 3 * MIN + 20 * S);  // TC-11
    CHECK(fee.ok && fee.fee == 4 * TARIF && fee.due == 4 * TARIF);

    enter(p, "ZH-100002", 4 * MIN);
    CHECK(p.queryFee("ZH-100002", 4 * MIN + 25 * S).due == 0);  // TC-12
    CHECK(p.queryFee("ZH-100001", 7 * MIN).fee == 5 * TARIF);   // TC-13

    fee = p.queryFee("XX-999999", 7 * MIN);  // TC-14
    CHECK(!fee.ok && fee.reason == Reason::Unknown);

    // TC-16..18
    p.reset();
    enter(p, "ZH-100001", 0);
    auto pay = p.pay("ZH-100001", false, 0, 2 * MIN + 10 * S);
    CHECK(pay.ok && pay.amount == 3 * TARIF);
    fee = p.queryFee("ZH-100001", 2 * MIN + 11 * S);
    CHECK(fee.due == 0 && fee.inGrace);
    const park::LogEntry& e = lastLog(p);  // TC-17
    CHECK(e.type == EventType::Payment && e.hasAmount && e.amount == 3 * TARIF);
    CHECK(strcmp(e.id, "ZH-100001") == 0);
    // innerhalb der Karenzzeit: Ausfahrt frei
    CHECK(p.requestExit("ZH-100001", false, 4 * MIN).open);
    // TC-18: 3 Min. später (Karenzzeit 2 Min. abgelaufen) → Nachzahlung
    fee = p.queryFee("ZH-100001", 5 * MIN + 10 * S);
    CHECK(fee.due == 5 * TARIF - 3 * TARIF);
    auto ex = p.requestExit("ZH-100001", false, 5 * MIN + 10 * S);
    CHECK(!ex.open && ex.reason == Reason::Unpaid && ex.due == 2 * TARIF);
    // Nachzahlung
    pay = p.pay("ZH-100001", false, 0, 5 * MIN + 20 * S);
    CHECK(pay.ok && pay.amount == 2 * TARIF);
    CHECK(p.revenue() == 5 * TARIF);

    // Betrag nicht ausreichend / Rückgeld
    enter(p, "ZH-100005", 6 * MIN);
    pay = p.pay("ZH-100005", true, 100, 8 * MIN + 30 * S);  // fällig 3.00
    CHECK(!pay.ok && pay.reason == Reason::Insufficient && pay.amount == 3 * TARIF);
    pay = p.pay("ZH-100005", true, 500, 8 * MIN + 30 * S);
    CHECK(pay.ok && pay.amount == 3 * TARIF && pay.change == 200);

    CHECK(p.pay("XX-999999", false, 0, 9 * MIN).reason == Reason::Unknown);  // TC-19

    // TC-20
    enter(p, "ZH-100002", 9 * MIN);
    CHECK(p.requestExit("ZH-100002", false, 9 * MIN + 10 * S).open);  // Freigrenze
    CHECK(p.confirmExit("ZH-100002", false, 9 * MIN + 12 * S) == Reason::None);
    pay = p.pay("ZH-100002", false, 0, 9 * MIN + 20 * S);
    CHECK(!pay.ok && pay.reason == Reason::Exited);

    // TC-21
    enter(p, "ZH-100003", 10 * MIN);
    pay = p.pay("ZH-100003", false, 0, 10 * MIN + 20 * S);
    CHECK(pay.ok && pay.reason == Reason::NothingDue);
    const park::Vehicle* v = findVehicle(p, "ZH-100003");
    CHECK(v != nullptr && v->hasPaid && p.dueFor(*v, 10 * MIN + 21 * S) == 0);
    // erneutes Bezahlen verlängert die Karenzzeit nicht
    const size_t logSize = p.log().size();
    p.pay("ZH-100003", false, 0, 11 * MIN);
    CHECK(p.log().size() == logSize);
    CHECK(findVehicle(p, "ZH-100003")->paidAtMs == 10 * MIN + 20 * S);
}

void testUs5Us6() {
    puts("US5/US6 Ausfahrt & Belegung (TC-22..TC-29, TC-40)");
    ParkingSystem p;
    CHECK(p.capacity() == 3 && p.occupied() == 0 && p.freeSpaces() == 3);  // TC-27
    enter(p, "ZH-100001", 0);
    p.pay("ZH-100001", false, 0, 2 * MIN);
    CHECK(p.requestExit("ZH-100001", false, 2 * MIN + 5 * S).open);  // TC-22
    CHECK(p.confirmExit("ZH-100001", false, 2 * MIN + 8 * S) == Reason::None);  // TC-23
    CHECK(findVehicle(p, "ZH-100001")->state == park::VehicleState::Exited);
    CHECK(p.freeSpaces() == 3);
    // TC-40: zweite Bestätigung ändert nichts
    CHECK(p.confirmExit("ZH-100001", false, 2 * MIN + 9 * S) == Reason::Exited);
    CHECK(p.freeSpaces() == 3);

    enter(p, "ZH-100002", 3 * MIN);  // TC-24
    auto ex = p.requestExit("ZH-100002", false, 5 * MIN + 10 * S);
    CHECK(!ex.open && ex.reason == Reason::Unpaid);
    CHECK(ex.due == p.queryFee("ZH-100002", 5 * MIN + 10 * S).due);

    ex = p.requestExit("XX-999999", false, 6 * MIN);  // TC-25
    CHECK(!ex.open && ex.reason == Reason::Unknown);

    enter(p, "ZH-100003", 6 * MIN);  // TC-26
    p.pay("ZH-100003", false, 0, 6 * MIN + 10 * S);
    CHECK(p.requestExit("ZH-100003", false, 6 * MIN + 20 * S).open);
    p.abortExit("ZH-100003", false, 6 * MIN + 50 * S);
    CHECK(findVehicle(p, "ZH-100003")->state == park::VehicleState::Inside);
    CHECK(lastLog(p).type == EventType::ExitAborted);

    // TC-29: Grenzwert
    enter(p, "ZH-100004", 7 * MIN);
    CHECK(p.freeSpaces() == 0);
    CHECK(p.requestEntry("ZH-100001", 7 * MIN + 5 * S).reason == Reason::Full);
    CHECK(p.freeSpaces() == 0 && p.occupied() == 3);

    // Wiedereinfahrt eines ausgefahrenen Kennzeichens nutzt denselben Eintrag
    p.reset();
    enter(p, "ZH-100001", 0);
    p.confirmExit("ZH-100001", false, 10 * S);
    enter(p, "ZH-100001", 20 * S);
    int count = 0;
    for (size_t i = 0; i < p.registerSize(); ++i) {
        if (p.vehicleAt(i).used && strcmp(p.vehicleAt(i).id, "ZH-100001") == 0) ++count;
    }
    CHECK(count == 1);
    CHECK(findVehicle(p, "ZH-100001")->paidTotal == 0);

    // Register läuft mit ausgefahrenen Fahrzeugen nicht voll
    p.reset();
    char plate[16];
    for (int i = 0; i < 40; ++i) {
        snprintf(plate, sizeof(plate), "ZH-%d", 200 + i);
        enter(p, plate, i * S);
        CHECK(p.confirmExit(plate, false, i * S + 500) == Reason::None);
    }
    CHECK(p.freeSpaces() == 3);
}

void testUs7() {
    puts("US7 Protokoll (TC-30..TC-33)");
    ParkingSystem p;
    enter(p, "ZH-100001", 0);                                   // 1
    p.requestEntry("ZH-!!??", 5 * S);                           // 2
    p.requestExit("ZH-100001", false, 2 * MIN + 5 * S);         // 3 (unbezahlt)
    p.pay("ZH-100001", false, 0, 2 * MIN + 30 * S);             // 4
    CHECK(p.requestExit("ZH-100001", false, 2 * MIN + 40 * S).open);
    p.confirmExit("ZH-100001", false, 2 * MIN + 45 * S);        // 5
    const park::EventLog& log = p.log();
    CHECK(log.size() == 5);  // TC-30
    CHECK(log.at(0).type == EventType::Entry);
    CHECK(log.at(1).type == EventType::EntryDenied && log.at(1).reason == Reason::Invalid);
    CHECK(strcmp(log.at(1).id, "ZH-!!??") == 0);
    CHECK(log.at(2).type == EventType::ExitDenied && log.at(2).reason == Reason::Unpaid);
    CHECK(log.at(3).type == EventType::Payment && log.at(3).amount == 3 * TARIF);
    CHECK(log.at(4).type == EventType::Exit);
    for (size_t i = 1; i < log.size(); ++i) {  // TC-31
        CHECK(log.at(i).timeMs >= log.at(i - 1).timeMs);
        CHECK(log.at(i).seq == log.at(i - 1).seq + 1);
    }

    // TC-32: Überschreiben
    p.reset();
    for (int i = 0; i < 10; ++i) p.requestEntry("ZH-!!??", i * S);
    CHECK(p.log().size() == 10 && p.log().overwrittenCount() == 0);
    const uint32_t firstSeq = p.log().at(0).seq;
    enter(p, "ZH-100001", 20 * S);
    CHECK(p.log().size() == 10 && p.log().overwrittenCount() == 1);
    CHECK(p.log().at(0).seq == firstSeq + 1);
    CHECK(lastLog(p).type == EventType::Entry);

    // TC-33: gleiche Anzahl Felder pro CSV-Zeile
    auto fields = [](const char* line) {
        int n = 1;
        for (; *line != '\0'; ++line) n += *line == ';';
        return n;
    };
    int headerFields = fields(park::EventLog::CSV_HEADER);
    CHECK(headerFields == 7);
    char line[128];
    for (size_t i = 0; i < p.log().size(); ++i) {
        park::EventLog::formatCsv(p.log().at(i), line, sizeof(line));
        CHECK(fields(line) == headerFields);
    }
    park::EventLog::formatCsv(lastLog(p), line, sizeof(line));
    CHECK(strcmp(line, "11;00:00:20;EINFAHRT;ZH-100001;;;nein") == 0);
}

void testUs8() {
    puts("US8 Manuelle Abfertigung (TC-34..TC-37)");
    ParkingSystem p;
    auto r = p.requestManualEntry(0);  // TC-34
    CHECK(r.granted && strcmp(r.id, "MANUAL-1") == 0);
    CHECK(findVehicle(p, "MANUAL-1") != nullptr);
    enter(p, "ZH-100001", 1 * S);
    enter(p, "ZH-100002", 2 * S);
    r = p.requestManualEntry(3 * S);  // TC-35
    CHECK(!r.granted && r.reason == Reason::Full);
    CHECK(findVehicle(p, "MANUAL-2") == nullptr);

    auto ex = p.requestExit("MANUAL-1", true, 2 * MIN + 5 * S);  // TC-36
    CHECK(!ex.open && ex.reason == Reason::Unpaid && ex.due == 3 * TARIF);
    CHECK(p.pay("MANUAL-1", false, 0, 2 * MIN + 20 * S).ok);
    CHECK(p.requestExit("MANUAL-1", true, 2 * MIN + 30 * S).open);
    CHECK(p.confirmExit("MANUAL-1", true, 2 * MIN + 35 * S) == Reason::None);
    CHECK(findVehicle(p, "MANUAL-1")->state == park::VehicleState::Exited);

    bool entryManual = false;  // TC-37
    bool exitManual = false;
    for (size_t i = 0; i < p.log().size(); ++i) {
        const park::LogEntry& e = p.log().at(i);
        if (strcmp(e.id, "MANUAL-1") != 0) continue;
        if (e.type == EventType::Entry) entryManual = e.manual;
        if (e.type == EventType::Exit) exitManual = e.manual;
    }
    CHECK(entryManual && exitManual);

    // Nach Ausfahrt ist wieder Platz – nächste Ersatz-ID ist MANUAL-2
    r = p.requestManualEntry(3 * MIN);
    CHECK(r.granted && strcmp(r.id, "MANUAL-2") == 0);
}

void testHelpers() {
    puts("Hilfsfunktionen (Format, Protokoll)");
    char buf[32];
    park::formatMoney(350, buf, sizeof(buf));
    CHECK(strcmp(buf, "3.50") == 0);
    park::formatMoney(5, buf, sizeof(buf));
    CHECK(strcmp(buf, "0.05") == 0);
    park::formatClock(3 * MIN + 20 * S, buf, sizeof(buf));
    CHECK(strcmp(buf, "00:03:20") == 0);

    cfg::Money m = 0;
    CHECK(park::parseMoney("5", m) && m == 500);
    CHECK(park::parseMoney("5.5", m) && m == 550);
    CHECK(park::parseMoney("5,05", m) && m == 505);
    CHECK(park::parseMoney(".50", m) && m == 50);
    CHECK(!park::parseMoney("abc", m));
    CHECK(!park::parseMoney("1.234", m));
    CHECK(!park::parseMoney("", m));

    const char* body = "result=DENIED\nreason=UNPAID\nid=ZH-100002\ndue=300\nfree=1\n";
    CHECK(proto::getField(body, "result", buf, sizeof(buf)) && strcmp(buf, "DENIED") == 0);
    CHECK(proto::getField(body, "due", buf, sizeof(buf)) && strcmp(buf, "300") == 0);
    CHECK(proto::getField(body, "free", buf, sizeof(buf)) && strcmp(buf, "1") == 0);
    CHECK(!proto::getField(body, "fre", buf, sizeof(buf)));
    CHECK(!proto::getField(body, "missing", buf, sizeof(buf)));
    CHECK(park::reasonFromToken(park::reasonToken(Reason::Unpaid)) == Reason::Unpaid);
}

void testReplay() {
    puts("Wiederholte Anfragen (Request-ID, ReplayCache)");
    park::ReplayCache<2, 32> cache;
    CHECK(cache.find("a") == nullptr);
    CHECK(cache.find("") == nullptr);
    cache.store("", "leer");  // leere IDs werden nie gespeichert
    CHECK(cache.find("") == nullptr);
    cache.store("a", "result=GRANTED");
    CHECK(cache.find("a") != nullptr && strcmp(cache.find("a"), "result=GRANTED") == 0);
    cache.store("b", "B");
    cache.store("c", "C");  // verdrängt "a" (ältester Eintrag)
    CHECK(cache.find("a") == nullptr);
    CHECK(strcmp(cache.find("b"), "B") == 0 && strcmp(cache.find("c"), "C") == 0);

    // Szenario: Antwort der Kasse geht verloren, Pico 2 wiederholt die Einfahrt
    // mit derselben ID → gleiche Antwort, keine "bereits eingefahren"-Ablehnung.
    ParkingSystem p;
    park::ReplayCache<8, 192> api;
    auto handleEntry = [&](const char* rid, const char* plate, uint32_t t) -> const char* {
        if (const char* cached = api.find(rid)) return cached;
        static char body[192];
        const auto r = p.requestEntry(plate, t);
        snprintf(body, sizeof(body), "result=%s\nreason=%s\n", r.granted ? "GRANTED" : "DENIED",
                 park::reasonToken(r.reason));
        api.store(rid, body);
        return api.find(rid);
    };
    char first[192];
    snprintf(first, sizeof(first), "%s", handleEntry("p2-1", "ZH-100001", 0));
    CHECK(strstr(first, "GRANTED") != nullptr);
    CHECK(strcmp(handleEntry("p2-1", "ZH-100001", 500), first) == 0);  // Wiederholung
    CHECK(p.occupied() == 1 && p.log().size() == 1);
    CHECK(strstr(handleEntry("p2-2", "ZH-100001", 900), "DUPLICATE") != nullptr);  // neue Anfrage

    char esc[64];
    park::jsonEscape("a\"b\\c\n", esc, sizeof(esc));
    CHECK(strcmp(esc, "a\\\"b\\\\c\\u000a") == 0);
    park::jsonEscape("\"\"\"", esc, 6);  // kürzt, ohne eine Sequenz zu zerschneiden
    CHECK(strcmp(esc, "\\\"\\\"") == 0);
}

}  // namespace

int main() {
#ifdef _WIN32
    // Ausgaben sind UTF-8 (Umlaute); die Windows-Konsole erwartet sonst CP850.
    SetConsoleOutputCP(CP_UTF8);
#endif
    testPlates();
    testTariff();
    testUs1Us2();
    testUs3Us4();
    testUs5Us6();
    testUs7();
    testUs8();
    testHelpers();
    testReplay();
    printf("\n%d Prüfungen, %d Fehler\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
