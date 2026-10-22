#include "ParkingSystem.h"

#include <stdio.h>
#include <string.h>

#include "Plate.h"

namespace park {

namespace {

void copyId(char* dst, const char* src) {
    strncpy(dst, src, cfg::ID_BUF_LEN - 1);
    dst[cfg::ID_BUF_LEN - 1] = '\0';
}

// Für Protokolleinträge zu ungültigen Eingaben.
void logIdFor(const char* raw, PlateCheck check, const char* normalized, char* out) {
    if (check == PlateCheck::Ok) {
        copyId(out, normalized);
    } else {
        sanitizeForLog(raw, out, cfg::ID_BUF_LEN);
    }
}

}  // namespace

ParkingSystem::ParkingSystem(const Tariff& tariff) : tariff_(tariff) {}

void ParkingSystem::reset() {
    memset(vehicles_, 0, sizeof(vehicles_));
    log_.clear();
    manualCounter_ = 0;
    revenue_ = 0;
}

uint16_t ParkingSystem::occupied() const {
    uint16_t n = 0;
    for (const auto& v : vehicles_) {
        if (v.used && v.state == VehicleState::Inside) ++n;
    }
    return n;
}

Vehicle* ParkingSystem::find(const char* id) {
    for (auto& v : vehicles_) {
        if (v.used && strcmp(v.id, id) == 0) return &v;
    }
    return nullptr;
}

const Vehicle* ParkingSystem::find(const char* id) const {
    for (const auto& v : vehicles_) {
        if (v.used && strcmp(v.id, id) == 0) return &v;
    }
    return nullptr;
}

Vehicle* ParkingSystem::allocate(const char* id) {
    // Ein bereits ausgefahrenes Fahrzeug mit demselben Kennzeichen wird
    // wiederverwendet – so gibt es jedes Kennzeichen höchstens einmal im Register.
    if (Vehicle* existing = find(id)) return existing;

    for (auto& v : vehicles_) {
        if (!v.used) return &v;
    }
    // Sonst den am längsten ausgefahrenen Eintrag überschreiben.
    Vehicle* oldest = nullptr;
    for (auto& v : vehicles_) {
        if (v.state == VehicleState::Exited &&
            (oldest == nullptr || static_cast<int32_t>(v.exitMs - oldest->exitMs) < 0)) {
            oldest = &v;
        }
    }
    return oldest;
}

void ParkingSystem::registerVehicle(Vehicle& v, const char* id, bool manual, uint32_t nowMs) {
    memset(&v, 0, sizeof(v));
    v.used = true;
    v.state = VehicleState::Inside;
    v.manual = manual;
    copyId(v.id, id);
    v.entryMs = nowMs;
}

EntryResult ParkingSystem::requestEntry(const char* rawPlate, uint32_t nowMs) {
    EntryResult r{};
    char plate[cfg::ID_BUF_LEN];
    const PlateCheck check = normalizeId(rawPlate, plate, sizeof(plate), false);
    logIdFor(rawPlate, check, plate, r.id);

    if (check != PlateCheck::Ok) {
        r.reason = Reason::Invalid;
    } else {
        const Vehicle* existing = find(plate);
        if (existing != nullptr && existing->state == VehicleState::Inside) {
            r.reason = Reason::Duplicate;
        } else if (freeSpaces() == 0) {
            r.reason = Reason::Full;
        }
    }

    if (r.reason != Reason::None) {
        log_.add(nowMs, EventType::EntryDenied, r.id, r.reason);
        return r;
    }

    Vehicle* slot = allocate(plate);
    if (slot == nullptr) {  // kann wegen REGISTER_SIZE > CAPACITY nicht auftreten
        r.reason = Reason::Full;
        log_.add(nowMs, EventType::EntryDenied, r.id, r.reason);
        return r;
    }
    registerVehicle(*slot, plate, false, nowMs);
    r.granted = true;
    log_.add(nowMs, EventType::Entry, r.id);
    return r;
}

EntryResult ParkingSystem::requestManualEntry(uint32_t nowMs) {
    EntryResult r{};
    if (freeSpaces() == 0) {
        r.reason = Reason::Full;
        copyId(r.id, "MANUAL");
        log_.add(nowMs, EventType::EntryDenied, r.id, r.reason, true);
        return r;
    }

    char id[cfg::ID_BUF_LEN];
    snprintf(id, sizeof(id), "%s%lu", cfg::MANUAL_PREFIX,
             static_cast<unsigned long>(manualCounter_ + 1));
    Vehicle* slot = allocate(id);
    if (slot == nullptr) {
        r.reason = Reason::Full;
        copyId(r.id, "MANUAL");
        log_.add(nowMs, EventType::EntryDenied, r.id, r.reason, true);
        return r;
    }
    ++manualCounter_;
    registerVehicle(*slot, id, true, nowMs);
    copyId(r.id, id);
    r.granted = true;
    log_.add(nowMs, EventType::Entry, r.id, Reason::None, true);
    return r;
}

cfg::Money ParkingSystem::dueFor(const Vehicle& v, uint32_t nowMs) const {
    if (v.state != VehicleState::Inside) return 0;
    if (v.hasPaid && nowMs - v.paidAtMs <= cfg::PAYMENT_GRACE_MS) return 0;
    const cfg::Money due = tariff_.feeFor(nowMs - v.entryMs) - v.paidTotal;
    return due > 0 ? due : 0;
}

FeeResult ParkingSystem::queryFee(const char* rawId, uint32_t nowMs) const {
    FeeResult r{};
    char id[cfg::ID_BUF_LEN];
    if (normalizeId(rawId, id, sizeof(id), true) != PlateCheck::Ok) {
        r.reason = Reason::Invalid;
        return r;
    }
    copyId(r.id, id);

    const Vehicle* v = find(id);
    if (v == nullptr) {
        r.reason = Reason::Unknown;
        return r;
    }
    if (v->state == VehicleState::Exited) {
        r.reason = Reason::Exited;
        return r;
    }

    r.ok = true;
    r.durationMs = nowMs - v->entryMs;
    r.fee = tariff_.feeFor(r.durationMs);
    r.paid = v->paidTotal;
    r.due = dueFor(*v, nowMs);
    const uint32_t sincePaid = nowMs - v->paidAtMs;
    r.inGrace = v->hasPaid && sincePaid <= cfg::PAYMENT_GRACE_MS;
    r.graceLeftMs = r.inGrace ? cfg::PAYMENT_GRACE_MS - sincePaid : 0;
    return r;
}

PaymentResult ParkingSystem::pay(const char* rawId, bool hasTendered, cfg::Money tendered,
                                 uint32_t nowMs) {
    PaymentResult r{};
    char id[cfg::ID_BUF_LEN];
    if (normalizeId(rawId, id, sizeof(id), true) != PlateCheck::Ok) {
        r.reason = Reason::Invalid;
        return r;
    }
    copyId(r.id, id);

    Vehicle* v = find(id);
    if (v == nullptr) {
        r.reason = Reason::Unknown;
        return r;
    }
    if (v->state == VehicleState::Exited) {
        r.reason = Reason::Exited;
        return r;
    }

    const cfg::Money due = dueFor(*v, nowMs);
    if (due == 0) {
        r.ok = true;
        r.reason = Reason::NothingDue;
        r.change = hasTendered ? tendered : 0;
        // Erstmalig "bezahlt" setzen (Freigrenze). Eine laufende Karenzzeit
        // wird dadurch nicht verlängert.
        const bool inGrace = v->hasPaid && nowMs - v->paidAtMs <= cfg::PAYMENT_GRACE_MS;
        if (!inGrace) {
            v->hasPaid = true;
            v->paidAtMs = nowMs;
            log_.add(nowMs, EventType::Payment, v->id, Reason::NothingDue, v->manual, true, 0);
        }
        return r;
    }

    if (hasTendered && tendered < due) {
        r.reason = Reason::Insufficient;
        r.amount = due;
        return r;
    }

    v->paidTotal += due;
    v->hasPaid = true;
    v->paidAtMs = nowMs;
    revenue_ += due;

    r.ok = true;
    r.amount = due;
    r.change = hasTendered ? tendered - due : 0;
    log_.add(nowMs, EventType::Payment, v->id, Reason::None, v->manual, true, due);
    return r;
}

ExitResult ParkingSystem::requestExit(const char* rawId, bool manualRequest, uint32_t nowMs) {
    ExitResult r{};
    char id[cfg::ID_BUF_LEN];
    const PlateCheck check = normalizeId(rawId, id, sizeof(id), true);
    logIdFor(rawId, check, id, r.id);

    const Vehicle* v = check == PlateCheck::Ok ? find(id) : nullptr;
    if (check != PlateCheck::Ok) {
        r.reason = Reason::Invalid;
    } else if (v == nullptr) {
        r.reason = Reason::Unknown;
    } else if (v->state == VehicleState::Exited) {
        r.reason = Reason::Exited;
    } else {
        r.due = dueFor(*v, nowMs);
        if (r.due > 0) r.reason = Reason::Unpaid;
    }

    const bool manual = manualRequest || (v != nullptr && v->manual);
    if (r.reason != Reason::None) {
        log_.add(nowMs, EventType::ExitDenied, r.id, r.reason, manual, r.reason == Reason::Unpaid,
                 r.due);
        return r;
    }
    r.open = true;
    return r;
}

Reason ParkingSystem::confirmExit(const char* rawId, bool manualRequest, uint32_t nowMs) {
    char id[cfg::ID_BUF_LEN];
    if (normalizeId(rawId, id, sizeof(id), true) != PlateCheck::Ok) return Reason::Invalid;

    Vehicle* v = find(id);
    if (v == nullptr) return Reason::Unknown;
    if (v->state == VehicleState::Exited) return Reason::Exited;

    v->state = VehicleState::Exited;
    v->exitMs = nowMs;
    log_.add(nowMs, EventType::Exit, v->id, Reason::None, manualRequest || v->manual, true,
             v->paidTotal);
    return Reason::None;
}

void ParkingSystem::abortExit(const char* rawId, bool manualRequest, uint32_t nowMs) {
    char id[cfg::ID_BUF_LEN];
    if (normalizeId(rawId, id, sizeof(id), true) != PlateCheck::Ok) return;
    const Vehicle* v = find(id);
    if (v == nullptr || v->state != VehicleState::Inside) return;
    log_.add(nowMs, EventType::ExitAborted, v->id, Reason::Timeout, manualRequest || v->manual);
}

}  // namespace park
