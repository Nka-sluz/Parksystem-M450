// Komponententests der Kassen-Logik (Pico 1) aus lib/ParkCore auf dem PC.
// Die Zeit kommt aus einer FakeClock. Die Szenarien folgen den Testfällen des
// Testkonzepts (Testkonfiguration: 3 Plätze, Einheit/Freigrenze 1 Min.,
// CHF 1.00/Min., Höchstsatz CHF 5.00, Karenzzeit 2 Min., Protokoll 10 Einträge).
//
// Build & Run: siehe test/host/README.md

#include <stdio.h>
#include <string.h>

#include "Config.h"
#include "EventLog.h"
#include "Format.h"
#include "ParkingSystem.h"
#include "Plate.h"
#include "Protocol.h"
#include "ReplayCache.h"
#include "Tariff.h"
#include "TestDoubles.h"
#include "TestFramework.h"

static_assert(PARK_TEST_CONFIG == 1, "Tests erwarten die Testkonfiguration");

namespace {

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

void enter(FakeClock& clock, ParkingSystem& p, const char* plate, uint32_t t) {
    clock.set(t);
    const auto r = p.requestEntry(plate);
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

    // Der Tarif lässt sich auch über den Konstruktor übergeben.
    FakeClock clock;
    park::Tariff doubled = park::Tariff::fromConfig();
    doubled.pricePerUnit *= 2;
    doubled.dailyMax *= 2;
    ParkingSystem p(clock, doubled);
    enter(clock, p, "ZH-100001", 0);
    clock.set(2 * MIN + 10 * S);
    CHECK(p.queryFee("ZH-100001").fee == 3 * 2 * TARIF);
}

#ifdef TC15_PRICE
// TC-15 (Konfigurierbarkeit): run-tests.bat baut die Tests ein zweites Mal mit
// einer Kopie von Config.h, in der nur TARIFF_PRICE_PER_UNIT verdoppelt ist.
// Die Gebühr nach 2 Min. 10 s muss 3 × neuer Tarif sein (CHF 6.00), der
// Höchstsatz folgt ohne weitere Änderung.
void testTc15() {
    puts("TC-15 Konfigurierbarkeit: neu kompiliert mit verdoppelter Konstante");
    CHECK(cfg::TARIFF_PRICE_PER_UNIT == TC15_PRICE);
    CHECK(cfg::TARIFF_DAILY_MAX == 5 * TC15_PRICE);
    FakeClock clock;
    ParkingSystem p(clock);  // Tarif aus Config.h
    enter(clock, p, "ZH-100001", 0);
    clock.set(2 * MIN + 10 * S);
    CHECK(p.queryFee("ZH-100001").fee == 3 * TC15_PRICE);
}
#endif

void testUs1Us2() {
    puts("US1/US2 Einfahrt (TC-01..TC-10)");
    FakeClock clock;
    ParkingSystem p(clock);
    clock.set(1 * S);
    auto r = p.requestEntry("ZH-100001");  // TC-01
    CHECK(r.granted && strcmp(r.id, "ZH-100001") == 0);
    const park::Vehicle* v = findVehicle(p, "ZH-100001");  // TC-02
    CHECK(v != nullptr && v->state == park::VehicleState::Inside && !v->hasPaid && v->entryMs == 1 * S);
    CHECK(p.freeSpaces() == 2);
    CHECK(lastLog(p).type == EventType::Entry);

    clock.set(2 * S);
    r = p.requestEntry("ZH-100001");  // TC-03
    CHECK(!r.granted && r.reason == Reason::Duplicate);
    CHECK(p.freeSpaces() == 2);

    // Grundzustand für US2
    p.reset();
    clock.set(1 * S);
    CHECK(p.requestEntry("").reason == Reason::Invalid);                // TC-06
    CHECK(p.requestEntry("ZH-!!??").reason == Reason::Invalid);         // TC-07
    CHECK(p.requestEntry("ZH-12345678901").reason == Reason::Invalid);  // TC-07
    enter(clock, p, "ZH-100001", 4 * S);
    enter(clock, p, "ZH-100002", 5 * S);
    enter(clock, p, "ZH-100003", 6 * S);
    CHECK(p.freeSpaces() == 0);
    clock.set(7 * S);
    CHECK(p.requestEntry("ZH-100004").reason == Reason::Full);  // TC-08
    CHECK(p.requestManualEntry().reason == Reason::Full);       // TC-09
    CHECK(findVehicle(p, "ZH-100004") == nullptr);              // TC-10
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
    FakeClock clock;
    ParkingSystem p(clock);
    enter(clock, p, "ZH-100001", 0);
    clock.set(3 * MIN + 20 * S);
    auto fee = p.queryFee("ZH-100001");  // TC-11
    CHECK(fee.ok && fee.fee == 4 * TARIF && fee.due == 4 * TARIF);

    enter(clock, p, "ZH-100002", 4 * MIN);
    clock.set(4 * MIN + 25 * S);
    CHECK(p.queryFee("ZH-100002").due == 0);  // TC-12
    clock.set(7 * MIN);
    CHECK(p.queryFee("ZH-100001").fee == 5 * TARIF);  // TC-13

    fee = p.queryFee("XX-999999");  // TC-14
    CHECK(!fee.ok && fee.reason == Reason::Unknown);

    // TC-16..18
    p.reset();
    enter(clock, p, "ZH-100001", 0);
    clock.set(2 * MIN + 10 * S);
    auto pay = p.pay("ZH-100001", false, 0);
    CHECK(pay.ok && pay.amount == 3 * TARIF);
    clock.set(2 * MIN + 11 * S);
    fee = p.queryFee("ZH-100001");
    CHECK(fee.due == 0 && fee.inGrace);
    const park::LogEntry& e = lastLog(p);  // TC-17
    CHECK(e.type == EventType::Payment && e.hasAmount && e.amount == 3 * TARIF);
    CHECK(strcmp(e.id, "ZH-100001") == 0);
    // innerhalb der Karenzzeit: Ausfahrt frei
    clock.set(4 * MIN);
    CHECK(p.requestExit("ZH-100001", false).open);
    // TC-18: 3 Min. später (Karenzzeit 2 Min. abgelaufen) → Nachzahlung
    clock.set(5 * MIN + 10 * S);
    fee = p.queryFee("ZH-100001");
    CHECK(fee.due == 5 * TARIF - 3 * TARIF);
    auto ex = p.requestExit("ZH-100001", false);
    CHECK(!ex.open && ex.reason == Reason::Unpaid && ex.due == 2 * TARIF);
    // Nachzahlung
    clock.set(5 * MIN + 20 * S);
    pay = p.pay("ZH-100001", false, 0);
    CHECK(pay.ok && pay.amount == 2 * TARIF);
    CHECK(p.revenue() == 5 * TARIF);

    // Betrag nicht ausreichend / Rückgeld
    enter(clock, p, "ZH-100005", 6 * MIN);
    clock.set(8 * MIN + 30 * S);
    pay = p.pay("ZH-100005", true, 100);  // fällig 3.00
    CHECK(!pay.ok && pay.reason == Reason::Insufficient && pay.amount == 3 * TARIF);
    pay = p.pay("ZH-100005", true, 500);
    CHECK(pay.ok && pay.amount == 3 * TARIF && pay.change == 200);

    clock.set(9 * MIN);
    CHECK(p.pay("XX-999999", false, 0).reason == Reason::Unknown);  // TC-19

    // TC-20
    enter(clock, p, "ZH-100002", 9 * MIN);
    clock.set(9 * MIN + 10 * S);
    CHECK(p.requestExit("ZH-100002", false).open);  // Freigrenze
    clock.set(9 * MIN + 12 * S);
    CHECK(p.confirmExit("ZH-100002", false) == Reason::None);
    clock.set(9 * MIN + 20 * S);
    pay = p.pay("ZH-100002", false, 0);
    CHECK(!pay.ok && pay.reason == Reason::Exited);

    // TC-21
    enter(clock, p, "ZH-100003", 10 * MIN);
    clock.set(10 * MIN + 20 * S);
    pay = p.pay("ZH-100003", false, 0);
    CHECK(pay.ok && pay.reason == Reason::NothingDue);
    const park::Vehicle* v = findVehicle(p, "ZH-100003");
    clock.set(10 * MIN + 21 * S);
    CHECK(v != nullptr && v->hasPaid && p.dueFor(*v) == 0);
    // erneutes Bezahlen verlängert die Karenzzeit nicht
    const size_t logSize = p.log().size();
    clock.set(11 * MIN);
    p.pay("ZH-100003", false, 0);
    CHECK(p.log().size() == logSize);
    CHECK(findVehicle(p, "ZH-100003")->paidAtMs == 10 * MIN + 20 * S);
}

void testUs5Us6() {
    puts("US5/US6 Ausfahrt & Belegung (TC-22..TC-29, TC-40)");
    FakeClock clock;
    ParkingSystem p(clock);
    CHECK(p.capacity() == 3 && p.occupied() == 0 && p.freeSpaces() == 3);  // TC-27
    enter(clock, p, "ZH-100001", 0);
    clock.set(2 * MIN);
    p.pay("ZH-100001", false, 0);
    clock.set(2 * MIN + 5 * S);
    CHECK(p.requestExit("ZH-100001", false).open);  // TC-22
    clock.set(2 * MIN + 8 * S);
    CHECK(p.confirmExit("ZH-100001", false) == Reason::None);  // TC-23
    CHECK(findVehicle(p, "ZH-100001")->state == park::VehicleState::Exited);
    CHECK(p.freeSpaces() == 3);
    // TC-40: zweite Bestätigung ändert nichts
    clock.set(2 * MIN + 9 * S);
    CHECK(p.confirmExit("ZH-100001", false) == Reason::Exited);
    CHECK(p.freeSpaces() == 3);

    enter(clock, p, "ZH-100002", 3 * MIN);  // TC-24
    clock.set(5 * MIN + 10 * S);
    auto ex = p.requestExit("ZH-100002", false);
    CHECK(!ex.open && ex.reason == Reason::Unpaid);
    CHECK(ex.due == p.queryFee("ZH-100002").due);

    clock.set(6 * MIN);
    ex = p.requestExit("XX-999999", false);  // TC-25
    CHECK(!ex.open && ex.reason == Reason::Unknown);

    enter(clock, p, "ZH-100003", 6 * MIN);  // TC-26
    clock.set(6 * MIN + 10 * S);
    p.pay("ZH-100003", false, 0);
    clock.set(6 * MIN + 20 * S);
    CHECK(p.requestExit("ZH-100003", false).open);
    clock.set(6 * MIN + 50 * S);
    p.abortExit("ZH-100003", false);
    CHECK(findVehicle(p, "ZH-100003")->state == park::VehicleState::Inside);
    CHECK(lastLog(p).type == EventType::ExitAborted);

    // TC-29: Grenzwert
    enter(clock, p, "ZH-100004", 7 * MIN);
    CHECK(p.freeSpaces() == 0);
    clock.set(7 * MIN + 5 * S);
    CHECK(p.requestEntry("ZH-100001").reason == Reason::Full);
    CHECK(p.freeSpaces() == 0 && p.occupied() == 3);

    // Wiedereinfahrt eines ausgefahrenen Kennzeichens nutzt denselben Eintrag
    p.reset();
    enter(clock, p, "ZH-100001", 0);
    clock.set(10 * S);
    p.confirmExit("ZH-100001", false);
    enter(clock, p, "ZH-100001", 20 * S);
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
        enter(clock, p, plate, i * S);
        clock.set(i * S + 500);
        CHECK(p.confirmExit(plate, false) == Reason::None);
    }
    CHECK(p.freeSpaces() == 3);
}

void testUs7() {
    puts("US7 Protokoll (TC-30..TC-33)");
    FakeClock clock;
    ParkingSystem p(clock);
    enter(clock, p, "ZH-100001", 0);  // 1
    clock.set(5 * S);
    p.requestEntry("ZH-!!??");  // 2
    clock.set(2 * MIN + 5 * S);
    p.requestExit("ZH-100001", false);  // 3 (unbezahlt)
    clock.set(2 * MIN + 30 * S);
    p.pay("ZH-100001", false, 0);  // 4
    clock.set(2 * MIN + 40 * S);
    CHECK(p.requestExit("ZH-100001", false).open);
    clock.set(2 * MIN + 45 * S);
    p.confirmExit("ZH-100001", false);  // 5
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
    for (int i = 0; i < 10; ++i) {
        clock.set(i * S);
        p.requestEntry("ZH-!!??");
    }
    CHECK(p.log().size() == 10 && p.log().overwrittenCount() == 0);
    const uint32_t firstSeq = p.log().at(0).seq;
    enter(clock, p, "ZH-100001", 20 * S);
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
    FakeClock clock;
    ParkingSystem p(clock);
    auto r = p.requestManualEntry();  // TC-34
    CHECK(r.granted && strcmp(r.id, "MANUAL-1") == 0);
    CHECK(findVehicle(p, "MANUAL-1") != nullptr);
    enter(clock, p, "ZH-100001", 1 * S);
    enter(clock, p, "ZH-100002", 2 * S);
    clock.set(3 * S);
    r = p.requestManualEntry();  // TC-35
    CHECK(!r.granted && r.reason == Reason::Full);
    CHECK(findVehicle(p, "MANUAL-2") == nullptr);

    clock.set(2 * MIN + 5 * S);
    auto ex = p.requestExit("MANUAL-1", true);  // TC-36
    CHECK(!ex.open && ex.reason == Reason::Unpaid && ex.due == 3 * TARIF);
    clock.set(2 * MIN + 20 * S);
    CHECK(p.pay("MANUAL-1", false, 0).ok);
    clock.set(2 * MIN + 30 * S);
    CHECK(p.requestExit("MANUAL-1", true).open);
    clock.set(2 * MIN + 35 * S);
    CHECK(p.confirmExit("MANUAL-1", true) == Reason::None);
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
    clock.set(3 * MIN);
    r = p.requestManualEntry();
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

    // Anfragen von Pico 2 an die Kasse (Formularparameter, URL-kodiert)
    char form[96];
    proto::formBody(form, sizeof(form), "ZH 1/ä", false, "p2-1");
    CHECK_STR(form, "plate=ZH%201%2F%C3%A4&rid=p2-1");
    proto::formBody(form, sizeof(form), "MANUAL-1", true, "p2-2");
    CHECK_STR(form, "plate=MANUAL-1&manual=1&rid=p2-2");
    proto::formBody(form, sizeof(form), nullptr, false, "p2-3");
    CHECK_STR(form, "manual=1&rid=p2-3");

    // Antworten der Kasse lesen
    park::KasseReply reply;
    CHECK(proto::parseReply(body, reply));
    CHECK(!reply.accepted && reply.reason == Reason::Unpaid && reply.due == 300);
    CHECK_STR(reply.id, "ZH-100002");
    CHECK(proto::parseReply("result=OPEN\nreason=NONE\nid=ZH-100001\ndue=0\nfree=2\n", reply));
    CHECK(reply.accepted && reply.reason == Reason::None);
    CHECK(proto::parseReply("result=GRANTED\nreason=NONE\nid=MANUAL-1\nfree=2\n", reply));
    CHECK(reply.accepted);
    CHECK_STR(reply.id, "MANUAL-1");
    CHECK(!proto::parseReply("reason=NONE\nid=ZH-1\n", reply));  // abgebrochen: ohne result
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
    FakeClock clock;
    ParkingSystem p(clock);
    park::ReplayCache<8, 192> api;
    auto handleEntry = [&](const char* rid, const char* plate, uint32_t t) -> const char* {
        if (const char* cached = api.find(rid)) return cached;
        static char body[192];
        clock.set(t);
        const auto r = p.requestEntry(plate);
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

void runParkCoreTests() {
    testPlates();
    testTariff();
    testUs1Us2();
    testUs3Us4();
    testUs5Us6();
    testUs7();
    testUs8();
    testHelpers();
    testReplay();
}

#ifdef TC15_PRICE
void runTc15Test() { testTc15(); }
#endif
