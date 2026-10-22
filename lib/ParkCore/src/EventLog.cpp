#include "EventLog.h"

#include <stdio.h>
#include <string.h>

#include "Format.h"

namespace park {

const char* eventTypeText(EventType t) {
    switch (t) {
        case EventType::Entry: return "EINFAHRT";
        case EventType::EntryDenied: return "EINFAHRT_VERWEIGERT";
        case EventType::Payment: return "ZAHLUNG";
        case EventType::Exit: return "AUSFAHRT";
        case EventType::ExitDenied: return "AUSFAHRT_VERWEIGERT";
        case EventType::ExitAborted: return "AUSFAHRT_ABGEBROCHEN";
    }
    return "?";
}

const char* reasonText(Reason r) {
    switch (r) {
        case Reason::None: return "";
        case Reason::Invalid: return "ungültig";
        case Reason::Duplicate: return "bereits eingefahren";
        case Reason::Full: return "voll";
        case Reason::Unpaid: return "unbezahlt";
        case Reason::Unknown: return "unbekannt";
        case Reason::Exited: return "bereits ausgefahren";
        case Reason::NothingDue: return "nichts zu bezahlen";
        case Reason::Insufficient: return "Betrag nicht ausreichend";
        case Reason::Timeout: return "keine Durchfahrt (Timeout)";
        case Reason::NotOpen: return "Schranke nicht offen";
    }
    return "";
}

const char* reasonMessage(Reason r) {
    switch (r) {
        case Reason::None: return "";
        case Reason::Invalid: return "Kennzeichen ungültig";
        case Reason::Duplicate: return "Kennzeichen bereits eingefahren";
        case Reason::Full: return "Parkhaus voll";
        case Reason::Unpaid: return "Bitte zuerst bezahlen";
        case Reason::Unknown: return "Kennzeichen unbekannt";
        case Reason::Exited: return "Kennzeichen bereits ausgefahren";
        case Reason::NothingDue: return "nichts zu bezahlen";
        case Reason::Insufficient: return "Betrag nicht ausreichend";
        case Reason::Timeout: return "Keine Durchfahrt – Schranke geschlossen";
        case Reason::NotOpen: return "Schranke ist nicht offen";
    }
    return "";
}

namespace {

struct TokenMap {
    Reason reason;
    const char* token;
};

constexpr TokenMap TOKENS[] = {
    {Reason::None, "NONE"},           {Reason::Invalid, "INVALID"},
    {Reason::Duplicate, "DUPLICATE"}, {Reason::Full, "FULL"},
    {Reason::Unpaid, "UNPAID"},       {Reason::Unknown, "UNKNOWN"},
    {Reason::Exited, "EXITED"},       {Reason::NothingDue, "NOTHING_DUE"},
    {Reason::Insufficient, "INSUFFICIENT"}, {Reason::Timeout, "TIMEOUT"},
    {Reason::NotOpen, "NOT_OPEN"},
};

}  // namespace

const char* reasonToken(Reason r) {
    for (const auto& t : TOKENS) {
        if (t.reason == r) return t.token;
    }
    return "NONE";
}

Reason reasonFromToken(const char* token) {
    for (const auto& t : TOKENS) {
        if (strcmp(t.token, token) == 0) return t.reason;
    }
    return Reason::None;
}

void EventLog::clear() {
    head_ = 0;
    count_ = 0;
    nextSeq_ = 1;
}

void EventLog::add(uint32_t nowMs, EventType type, const char* id, Reason reason, bool manual,
                   bool hasAmount, cfg::Money amount) {
    size_t index;
    if (count_ < cfg::LOG_CAPACITY) {
        index = (head_ + count_) % cfg::LOG_CAPACITY;
        ++count_;
    } else {
        // Voll: ältesten Eintrag überschreiben.
        index = head_;
        head_ = (head_ + 1) % cfg::LOG_CAPACITY;
    }

    LogEntry& e = entries_[index];
    e.seq = nextSeq_++;
    e.timeMs = nowMs;
    e.type = type;
    e.reason = reason;
    e.manual = manual;
    e.hasAmount = hasAmount;
    e.amount = amount;
    strncpy(e.id, id != nullptr ? id : "", sizeof(e.id) - 1);
    e.id[sizeof(e.id) - 1] = '\0';
}

const LogEntry& EventLog::at(size_t i) const { return entries_[(head_ + i) % cfg::LOG_CAPACITY]; }

void EventLog::formatCsv(const LogEntry& e, char* out, size_t outLen) {
    char time[16];
    formatClock(e.timeMs, time, sizeof(time));
    char amount[16] = "";
    if (e.hasAmount) formatMoney(e.amount, amount, sizeof(amount));

    snprintf(out, outLen, "%lu;%s;%s;%s;%s;%s;%s", static_cast<unsigned long>(e.seq), time,
             eventTypeText(e.type), e.id, amount, reasonText(e.reason), e.manual ? "ja" : "nein");
}

}  // namespace park
