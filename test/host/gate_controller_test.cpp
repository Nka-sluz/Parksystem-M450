// Komponententests der Schranken-Logik (Pico 2, park::GateController) auf dem
// PC – ohne Servo, LED, WLAN und ohne Warten.
//
//  Teil 1: GateController isoliert. Die Kasse ist ein KasseStub mit
//          vorgegebenen Antworten, die Schranke ein BarrierMock, die LED ein
//          LampSpy, die Zeit eine FakeClock.
//  Teil 2: Abläufe aus dem Testkonzept mit KasseFake: GateController (Pico 2)
//          und ParkingSystem (Pico 1) arbeiten zusammen, nur Netzwerk und
//          Hardware sind ersetzt.

#include <string.h>

#include "GateController.h"
#include "ParkingSystem.h"
#include "TestDoubles.h"
#include "TestFramework.h"

namespace {

using park::GateController;
using park::Reason;

// Prüft die Erwartungen eines BarrierMock und zeigt bei einem Fehler beide Abläufe.
#define CHECK_MOCK(mock)                                                                     \
    do {                                                                                     \
        ++checks;                                                                            \
        if (!(mock).verify()) {                                                              \
            ++failures;                                                                      \
            printf("  FEHLER %s:%d: %s: Aufrufe \"%s\", erwartet \"%s\"\n", __FILE__, __LINE__, \
                   #mock, (mock).calls(), (mock).expected());                                \
        }                                                                                    \
    } while (0)

bool contains(const char* text, const char* part) { return strstr(text, part) != nullptr; }

// Zwei getrennte Schranken (cfg::SERVO_COUNT = 2).
struct TwoBarriers {
    FakeClock clock;
    BarrierMock entryBarrier;
    BarrierMock exitBarrier;
    LampSpy lamp;
    KasseStub kasse;
    GateController gates{entryBarrier, exitBarrier, lamp, kasse, clock};
};

// Eine gemeinsame Schranke für Ein- und Ausfahrt (cfg::SERVO_COUNT = 1).
struct SharedBarrier {
    FakeClock clock;
    BarrierMock barrier;
    LampSpy lamp;
    KasseStub kasse;
    GateController gates{barrier, barrier, lamp, kasse, clock};
};

// ----------------------------------------------------------------------------
//  Teil 1: GateController mit KasseStub, BarrierMock, LampSpy, FakeClock
// ----------------------------------------------------------------------------

void testEntryOpensAndCloses() {
    puts("Einfahrt öffnen und schliessen (TC-01, TC-04)");
    TwoBarriers t;
    t.kasse.reply = KasseStub::accept("ZH-100001");
    t.entryBarrier.expect("open,close");
    t.exitBarrier.expect("");

    t.gates.onEntryPlate("ZH-100001");  // TC-01
    CHECK(t.gates.entryGate().isOpen() && t.lamp.isOn());
    CHECK_STR(t.gates.entryGate().message(), "Willkommen ZH-100001 – Einfahrt offen");

    t.gates.onEntryPassed();  // TC-04
    CHECK(!t.gates.entryGate().isOpen() && !t.lamp.isOn());
    CHECK_STR(t.gates.entryGate().message(), "Durchfahrt ZH-100001 bestätigt – Einfahrt zu");
    CHECK_MOCK(t.entryBarrier);
    CHECK_MOCK(t.exitBarrier);
    CHECK(t.lamp.switches() == 2);
}

void testEntryTimeout() {
    puts("Einfahrt schliesst nach 30 s ohne Durchfahrt (TC-05)");
    TwoBarriers t;
    t.kasse.reply = KasseStub::accept("ZH-100003");
    t.entryBarrier.expect("open,close");
    t.clock.set(10 * S);
    t.gates.onEntryPlate("ZH-100003");

    t.clock.advance(30 * S - 1);
    t.gates.checkTimeouts();
    CHECK(t.gates.entryGate().isOpen() && t.lamp.isOn());

    t.clock.advance(1);
    t.gates.checkTimeouts();
    CHECK(!t.gates.entryGate().isOpen() && !t.lamp.isOn());
    CHECK_STR(t.gates.entryGate().message(), "Keine Durchfahrt innerhalb von 30 s – Einfahrt geschlossen");
    CHECK_MOCK(t.entryBarrier);
    CHECK(t.kasse.aborts == 0);  // Einfahrt: keine Meldung an die Kasse
}

void testEntryDenied() {
    puts("Einfahrt verweigert – Schranke bleibt zu (TC-03, TC-06..TC-09)");
    TwoBarriers t;
    t.entryBarrier.expect("");

    t.kasse.reply = KasseStub::deny(Reason::Duplicate, "ZH-100001");  // TC-03
    t.gates.onEntryPlate("ZH-100001");
    CHECK_STR(t.gates.entryGate().message(), "ZH-100001: bereits eingefahren – Schranke bleibt geschlossen");
    CHECK_STR(t.gates.entryGate().messageClass(), "err");

    t.kasse.reply = KasseStub::deny(Reason::Invalid, "ZH-!!??");  // TC-06/07
    t.gates.onEntryPlate("ZH-!!??");
    CHECK(contains(t.gates.entryGate().message(), "Kennzeichen ungültig"));

    t.kasse.reply = KasseStub::deny(Reason::Full, "ZH-100004");  // TC-08
    t.gates.onEntryPlate("ZH-100004");
    CHECK_STR(t.gates.entryGate().message(), "Parkhaus voll – Schranke bleibt geschlossen");

    t.kasse.reply = KasseStub::deny(Reason::Full, "MANUAL");  // TC-09
    t.gates.onEntryManual();
    CHECK(contains(t.gates.entryGate().message(), "Parkhaus voll"));

    CHECK(!t.gates.entryGate().isOpen());
    CHECK_MOCK(t.entryBarrier);
    CHECK(t.lamp.switches() == 0);
    CHECK(t.kasse.requests == 4);
}

void testManualEntry() {
    puts("Manuelle Abfertigung an der Einfahrt (TC-34)");
    TwoBarriers t;
    t.kasse.reply = KasseStub::accept("MANUAL-1");
    t.entryBarrier.expect("open");
    t.gates.onEntryManual();
    CHECK(t.gates.entryGate().isOpen() && t.gates.entryGate().manual() && t.lamp.isOn());
    CHECK_STR(t.gates.entryGate().message(),
              "Manuelle Abfertigung: Ersatz-ID MANUAL-1 (bitte notieren) – Einfahrt offen");
    CHECK_MOCK(t.entryBarrier);
}

void testExit() {
    puts("Ausfahrt (TC-22..TC-26)");
    TwoBarriers t;
    t.exitBarrier.expect("open,close");

    t.kasse.reply = KasseStub::deny(Reason::Unpaid, "ZH-100002", 300);  // TC-24
    t.gates.onExitPlate("ZH-100002");
    CHECK_STR(t.gates.exitGate().message(), "Bitte zuerst bezahlen – offener Betrag CHF 3.00 (ZH-100002)");

    t.kasse.reply = KasseStub::deny(Reason::Unknown, "XX-999999");  // TC-25
    t.gates.onExitPlate("XX-999999");
    CHECK(contains(t.gates.exitGate().message(), "manuelle Abfertigung"));
    CHECK(!t.gates.exitGate().isOpen() && !t.lamp.isOn());

    t.kasse.reply = KasseStub::accept("ZH-100001");  // TC-22
    t.gates.onExitPlate("ZH-100001");
    CHECK(t.gates.exitGate().isOpen() && t.lamp.isOn());
    CHECK_STR(t.gates.exitGate().message(), "Gute Fahrt ZH-100001 – Ausfahrt offen");

    t.gates.onExitPassed();  // TC-23
    CHECK(!t.gates.exitGate().isOpen() && !t.lamp.isOn());
    CHECK(t.kasse.confirms == 1);
    CHECK_STR(t.gates.exitGate().message(), "Durchfahrt ZH-100001 bestätigt – Ausfahrt zu");
    CHECK_MOCK(t.exitBarrier);

    t.gates.onExitManual("");  // manuelle Ausfahrt ohne Ersatz-ID
    CHECK(contains(t.gates.exitGate().message(), "bitte Ersatz-ID"));
}

void testExitTimeout() {
    puts("Ausfahrt schliesst nach 30 s, Fahrzeug bleibt drin (TC-26)");
    TwoBarriers t;
    t.kasse.reply = KasseStub::accept("ZH-100003");
    t.exitBarrier.expect("open,close");
    t.gates.onExitPlate("ZH-100003");
    t.clock.advance(30 * S);
    t.gates.checkTimeouts();
    CHECK(!t.gates.exitGate().isOpen() && !t.lamp.isOn());
    CHECK(contains(t.gates.exitGate().message(), "Fahrzeug bleibt im Parkhaus"));
    CHECK(t.kasse.aborts == 1 && t.kasse.confirms == 0);
    CHECK_MOCK(t.exitBarrier);
}

void testFailSafe() {
    puts("Fail-safe: Kasse nicht erreichbar → Schranke bleibt zu (TC-38)");
    TwoBarriers t;
    t.kasse.reachable = false;
    t.kasse.reply = KasseStub::accept("ZH-100001");  // würde bei Verbindung öffnen
    t.entryBarrier.expect("");
    t.exitBarrier.expect("");

    t.gates.onExitPlate("ZH-100001");
    CHECK(!t.gates.exitGate().isOpen());
    CHECK(contains(t.gates.exitGate().message(), "Keine Verbindung zur Kasse – Ausfahrt bleibt geschlossen"));
    t.gates.onEntryPlate("ZH-100002");
    CHECK(!t.gates.entryGate().isOpen());
    CHECK(contains(t.gates.entryGate().message(), "Keine Verbindung zur Kasse"));

    t.clock.advance(10 * S);  // TC-38: 10 s beobachten
    t.gates.checkTimeouts();
    CHECK_MOCK(t.entryBarrier);
    CHECK_MOCK(t.exitBarrier);
    CHECK(t.lamp.switches() == 0);
}

void testPendingExitRetried() {
    puts("Ausfahrt nachmelden, wenn die Kasse kurz weg ist");
    FakeClock clock;
    BarrierMock entryBarrier;
    BarrierMock exitBarrier;
    LampDummy lamp;  // LED spielt hier keine Rolle
    KasseStub kasse;
    GateController gates(entryBarrier, exitBarrier, lamp, kasse, clock);
    gates.setBootId(0x2a);
    kasse.reply = KasseStub::accept("ZH-100001");
    gates.onExitPlate("ZH-100001");

    kasse.reachable = false;
    gates.onExitPassed();
    CHECK(!gates.exitGate().isOpen());  // Fahrzeug ist durch, Schranke trotzdem zu
    CHECK_STR(gates.pendingExit(), "ZH-100001");
    CHECK_STR(gates.exitGate().messageClass(), "warn");
    char firstRid[32];
    snprintf(firstRid, sizeof(firstRid), "%s", kasse.lastConfirmRid);
    CHECK_STR(firstRid, "p2-0000002a-2");

    CHECK(!gates.retryPendingExit());  // Kasse weiterhin weg
    kasse.reachable = true;
    CHECK(gates.retryPendingExit());
    CHECK_STR(gates.pendingExit(), "");
    CHECK(kasse.confirms == 3);
    CHECK_STR(kasse.lastConfirmRid, firstRid);  // dieselbe Request-ID → keine Doppelbuchung
    CHECK(!gates.retryPendingExit());           // nichts mehr offen
}

void testDoubleConfirm() {
    puts("Durchfahrt doppelt bestätigt (TC-40)");
    TwoBarriers t;
    t.kasse.reply = KasseStub::accept("ZH-100001");
    t.gates.onExitPlate("ZH-100001");
    t.gates.onExitPassed();
    t.gates.onExitPassed();  // zweiter Browser-Tab
    CHECK(t.kasse.confirms == 1);
    CHECK_STR(t.gates.exitGate().message(), "Schranke ist nicht offen – nichts zu bestätigen");
}

void testSharedBarrier() {
    puts("Gemeinsame Schranke: nur eine Richtung gleichzeitig");
    SharedBarrier t;
    CHECK(t.gates.sharedBarrier());
    t.barrier.expect("open,close,open");

    t.kasse.reply = KasseStub::accept("ZH-100001");
    t.gates.onEntryPlate("ZH-100001");
    t.gates.onExitPlate("ZH-100002");  // Einfahrt noch offen → warten
    CHECK(!t.gates.exitGate().isOpen());
    CHECK_STR(t.gates.exitGate().message(),
              "Schranke gerade für die Einfahrt von ZH-100001 offen – bitte warten, bis dort die "
              "Durchfahrt bestätigt ist");
    CHECK(t.kasse.requests == 1);  // Ausfahrt gar nicht erst bei der Kasse angefragt

    t.gates.onEntryPassed();
    t.kasse.reply = KasseStub::accept("ZH-100002");
    t.gates.onExitPlate("ZH-100002");
    CHECK(t.gates.exitGate().isOpen() && t.lamp.isOn());
    CHECK_MOCK(t.barrier);
}

void testTwoBarriersIndependent() {
    puts("Zwei Schranken: Ein- und Ausfahrt gleichzeitig offen");
    TwoBarriers t;
    CHECK(!t.gates.sharedBarrier());
    t.entryBarrier.expect("open,close");
    t.exitBarrier.expect("open,close");
    t.kasse.reply = KasseStub::accept("ZH-100001");
    t.gates.onEntryPlate("ZH-100001");
    t.kasse.reply = KasseStub::accept("ZH-100002");
    t.gates.onExitPlate("ZH-100002");
    CHECK(t.gates.entryGate().isOpen() && t.gates.exitGate().isOpen());

    t.gates.onEntryPassed();
    CHECK(t.lamp.isOn());  // Ausfahrt noch offen
    t.gates.onExitPassed();
    CHECK(!t.lamp.isOn());
    CHECK_MOCK(t.entryBarrier);
    CHECK_MOCK(t.exitBarrier);
}

// ----------------------------------------------------------------------------
//  Teil 2: Abläufe mit KasseFake (Pico 2 + Pico 1 ohne Netzwerk)
// ----------------------------------------------------------------------------

struct System {
    FakeClock clock;
    park::ParkingSystem parking{clock};
    KasseFake kasse{parking};
    BarrierMock barrier;  // eine gemeinsame Schranke wie auf dem Pico
    LampSpy lamp;
    GateController gates{barrier, barrier, lamp, kasse, clock};

    void enterAndPass(const char* plate) {
        gates.onEntryPlate(plate);
        CHECK(gates.entryGate().isOpen());
        gates.onEntryPassed();
    }
    const park::Vehicle* vehicle(const char* id) const {
        for (size_t i = 0; i < parking.registerSize(); ++i) {
            const park::Vehicle& v = parking.vehicleAt(i);
            if (v.used && strcmp(v.id, id) == 0) return &v;
        }
        return nullptr;
    }
};

void testScenarioParkAndPay() {
    puts("Ablauf: einfahren, Gebühr, bezahlen, ausfahren (TC-01/02, TC-22..TC-24, TC-28)");
    System s;
    s.barrier.expect("open,close,open,close");

    s.gates.onEntryPlate("zh-100001");  // TC-01 (Kleinbuchstaben werden normalisiert)
    CHECK(s.gates.entryGate().isOpen() && s.lamp.isOn());
    const park::Vehicle* v = s.vehicle("ZH-100001");  // TC-02
    CHECK(v != nullptr && !v->hasPaid);
    CHECK(s.parking.freeSpaces() == 2);
    s.gates.onEntryPassed();

    s.clock.advance(2 * MIN + 10 * S);  // TC-24
    s.gates.onExitPlate("ZH-100001");
    CHECK(!s.gates.exitGate().isOpen());
    CHECK_STR(s.gates.exitGate().message(), "Bitte zuerst bezahlen – offener Betrag CHF 3.00 (ZH-100001)");
    CHECK(s.parking.queryFee("ZH-100001").due == 300);  // gleicher Betrag wie an der Kasse

    CHECK(s.parking.pay("ZH-100001", false, 0).ok);  // an der Kasse bezahlen
    s.clock.advance(20 * S);
    s.gates.onExitPlate("ZH-100001");  // TC-22
    CHECK(s.gates.exitGate().isOpen() && s.lamp.isOn());
    s.gates.onExitPassed();  // TC-23
    CHECK(!s.lamp.isOn());
    CHECK(s.vehicle("ZH-100001")->state == park::VehicleState::Exited);
    CHECK(s.parking.freeSpaces() == 3);  // TC-28
    CHECK_MOCK(s.barrier);
}

void testScenarioFull() {
    puts("Ablauf: Parkhaus voll (TC-08, TC-09, TC-29)");
    System s;
    s.enterAndPass("ZH-100001");
    s.enterAndPass("ZH-100002");
    s.enterAndPass("ZH-100003");
    CHECK(s.parking.freeSpaces() == 0);
    s.gates.onEntryPlate("ZH-100004");
    CHECK(!s.gates.entryGate().isOpen());
    CHECK_STR(s.gates.entryGate().message(), "Parkhaus voll – Schranke bleibt geschlossen");
    s.gates.onEntryManual();
    CHECK(!s.gates.entryGate().isOpen() && s.vehicle("MANUAL-1") == nullptr);
    CHECK(s.parking.freeSpaces() == 0 && s.parking.occupied() == 3);
}

void testScenarioExitTimeout() {
    puts("Ablauf: Ausfahrt ohne Durchfahrt (TC-26)");
    System s;
    s.enterAndPass("ZH-100003");
    s.parking.pay("ZH-100003", false, 0);
    s.gates.onExitPlate("ZH-100003");
    CHECK(s.gates.exitGate().isOpen());
    s.clock.advance(30 * S);
    s.gates.checkTimeouts();
    CHECK(!s.gates.exitGate().isOpen() && !s.lamp.isOn());
    CHECK(s.vehicle("ZH-100003")->state == park::VehicleState::Inside);
    const park::EventLog& log = s.parking.log();
    CHECK(log.at(log.size() - 1).type == park::EventType::ExitAborted);
}

void testScenarioManual() {
    puts("Ablauf: manuelle Abfertigung (TC-34, TC-36, TC-37)");
    System s;
    s.gates.onEntryManual();  // TC-34
    CHECK(s.gates.entryGate().isOpen());
    CHECK_STR(s.gates.entryGate().id(), "MANUAL-1");
    s.gates.onEntryPassed();

    s.clock.advance(2 * MIN + 5 * S);  // TC-36
    s.gates.onExitManual("MANUAL-1");
    CHECK(!s.gates.exitGate().isOpen());
    CHECK(contains(s.gates.exitGate().message(), "offener Betrag CHF 3.00"));
    CHECK(s.parking.pay("MANUAL-1", false, 0).ok);
    s.gates.onExitManual("MANUAL-1");
    CHECK(s.gates.exitGate().isOpen());
    s.gates.onExitPassed();
    CHECK(s.vehicle("MANUAL-1")->state == park::VehicleState::Exited);

    bool entryManual = false;  // TC-37
    bool exitManual = false;
    const park::EventLog& log = s.parking.log();
    for (size_t i = 0; i < log.size(); ++i) {
        if (strcmp(log.at(i).id, "MANUAL-1") != 0) continue;
        if (log.at(i).type == park::EventType::Entry) entryManual = log.at(i).manual;
        if (log.at(i).type == park::EventType::Exit) exitManual = log.at(i).manual;
    }
    CHECK(entryManual && exitManual);
}

void testScenarioFailSafe() {
    puts("Ablauf: Verbindungsausfall bei bezahltem Fahrzeug (TC-38)");
    System s;
    s.barrier.expect("open,close,open");
    s.enterAndPass("ZH-100001");
    s.parking.pay("ZH-100001", false, 0);

    s.kasse.reachable = false;  // Pico 1 vom Strom
    s.gates.onExitPlate("ZH-100001");
    CHECK(!s.gates.exitGate().isOpen() && !s.lamp.isOn());

    s.kasse.reachable = true;  // wieder verbunden
    s.gates.onExitPlate("ZH-100001");
    CHECK(s.gates.exitGate().isOpen());
    CHECK_MOCK(s.barrier);
}

void testScenarioReconnect() {
    puts("Ablauf: Kasse fällt aus und kommt zurück (TC-42)");
    System s;
    s.barrier.expect("open");
    s.kasse.reachable = false;  // Pico 1 ausgesteckt
    s.gates.onEntryPlate("ZH-100001");
    CHECK(!s.gates.entryGate().isOpen());
    CHECK(contains(s.gates.entryGate().message(), "Keine Verbindung zur Kasse"));

    s.clock.advance(10 * S);
    s.kasse.reachable = true;  // wieder eingesteckt; derselbe GateController, kein Neustart
    s.gates.onEntryPlate("ZH-100001");
    CHECK(s.gates.entryGate().isOpen() && s.lamp.isOn());
    CHECK(s.vehicle("ZH-100001") != nullptr);
    CHECK_MOCK(s.barrier);
}

void testScenarioRobustInput() {
    puts("Ablauf: sehr lange und unzulässige Eingaben (TC-41)");
    System s;
    char longInput[501];
    memset(longInput, 'A', 500);
    longInput[500] = '\0';
    s.gates.onEntryPlate(longInput);
    CHECK(contains(s.gates.entryGate().message(), "Kennzeichen ungültig"));
    s.gates.onEntryPlate("<b>ZH</b>");
    CHECK(contains(s.gates.entryGate().message(), "Kennzeichen ungültig"));
    s.gates.onExitPlate(longInput);
    CHECK(contains(s.gates.exitGate().message(), "Kennzeichen ungültig"));
    CHECK(!s.gates.anyOpen() && s.parking.occupied() == 0);
}

}  // namespace

void runGateControllerTests() {
    testEntryOpensAndCloses();
    testEntryTimeout();
    testEntryDenied();
    testManualEntry();
    testExit();
    testExitTimeout();
    testFailSafe();
    testPendingExitRetried();
    testDoubleConfirm();
    testSharedBarrier();
    testTwoBarriersIndependent();
    testScenarioParkAndPay();
    testScenarioFull();
    testScenarioExitTimeout();
    testScenarioManual();
    testScenarioFailSafe();
    testScenarioReconnect();
    testScenarioRobustInput();
}
