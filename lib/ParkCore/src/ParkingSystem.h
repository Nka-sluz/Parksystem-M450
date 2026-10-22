#pragma once
// Fahrzeugregister und Geschäftslogik der Leitstelle (läuft auf Pico 1 "Kasse").
//
// Die Klasse ist hardwareunabhängig: Die aktuelle Zeit wird als Parameter
// (Millisekunden seit Start) übergeben. Es wird kein dynamischer Speicher
// verwendet; Register und Protokoll haben feste Obergrenzen (Config.h).

#include <stddef.h>
#include <stdint.h>

#include "Config.h"
#include "EventLog.h"
#include "Tariff.h"

namespace park {

enum class VehicleState : unsigned char { Inside, Exited };

struct Vehicle {
    bool used;
    VehicleState state;
    bool manual;   // per manueller Abfertigung (Ersatz-Identifikator) eingefahren
    bool hasPaid;  // mind. einmal bezahlt (Karenzzeit läuft ab paidAtMs)
    char id[cfg::ID_BUF_LEN];
    uint32_t entryMs;
    uint32_t exitMs;
    uint32_t paidAtMs;
    cfg::Money paidTotal;
};

struct EntryResult {
    bool granted;
    Reason reason;
    char id[cfg::ID_BUF_LEN];  // registriertes Kennzeichen bzw. MANUAL-<n>
};

struct FeeResult {
    bool ok;
    Reason reason;
    char id[cfg::ID_BUF_LEN];
    uint32_t durationMs;
    cfg::Money fee;   // Gebühr für die gesamte bisherige Parkdauer
    cfg::Money paid;  // bereits bezahlt
    cfg::Money due;   // jetzt offener Betrag
    bool inGrace;     // bezahlt und Karenzzeit noch nicht abgelaufen
    uint32_t graceLeftMs;
};

struct PaymentResult {
    bool ok;
    Reason reason;  // None, NothingDue, Insufficient, Unknown, Exited, Invalid
    char id[cfg::ID_BUF_LEN];
    cfg::Money amount;  // bezahlt (ok) bzw. fällig (Insufficient)
    cfg::Money change;  // Rückgeld
};

struct ExitResult {
    bool open;
    Reason reason;  // None, Unpaid, Unknown, Exited, Invalid
    char id[cfg::ID_BUF_LEN];
    cfg::Money due;
};

class ParkingSystem {
public:
    explicit ParkingSystem(const Tariff& tariff = Tariff::fromConfig());

    void reset();

    // US1/US2: automatische Einfahrt per Kennzeichen.
    EntryResult requestEntry(const char* rawPlate, uint32_t nowMs);
    // US8: manuelle Einfahrt, vergibt MANUAL-<n>.
    EntryResult requestManualEntry(uint32_t nowMs);

    // US3: Gebühr abfragen (verändert nichts).
    FeeResult queryFee(const char* rawId, uint32_t nowMs) const;

    // US4: Bezahlen. Ohne `hasTendered` wird genau der offene Betrag bezahlt.
    PaymentResult pay(const char* rawId, bool hasTendered, cfg::Money tendered, uint32_t nowMs);

    // US5: Ausfahrt anfragen. Gibt frei, wenn der offene Betrag 0 ist.
    // Verweigerungen werden protokolliert, die Freigabe erst bei confirmExit.
    ExitResult requestExit(const char* rawId, bool manualRequest, uint32_t nowMs);
    // Durchfahrt bestätigt → Status "ausgefahren", Platz frei. Reason::None = ok.
    Reason confirmExit(const char* rawId, bool manualRequest, uint32_t nowMs);
    // Freigegebene Ausfahrt ohne Durchfahrt (Timeout) – nur Protokolleintrag.
    void abortExit(const char* rawId, bool manualRequest, uint32_t nowMs);

    // US6: Belegung.
    uint16_t capacity() const { return cfg::CAPACITY; }
    uint16_t occupied() const;
    uint16_t freeSpaces() const { return static_cast<uint16_t>(capacity() - occupied()); }

    cfg::Money revenue() const { return revenue_; }

    // Register für die Anzeige.
    size_t registerSize() const { return cfg::REGISTER_SIZE; }
    const Vehicle& vehicleAt(size_t i) const { return vehicles_[i]; }
    cfg::Money dueFor(const Vehicle& v, uint32_t nowMs) const;

    const EventLog& log() const { return log_; }
    const Tariff& tariff() const { return tariff_; }

private:
    Vehicle* find(const char* id);
    const Vehicle* find(const char* id) const;
    Vehicle* allocate(const char* id);
    void registerVehicle(Vehicle& v, const char* id, bool manual, uint32_t nowMs);

    Tariff tariff_;
    Vehicle vehicles_[cfg::REGISTER_SIZE] = {};
    EventLog log_;
    uint32_t manualCounter_ = 0;
    cfg::Money revenue_ = 0;
};

}  // namespace park
