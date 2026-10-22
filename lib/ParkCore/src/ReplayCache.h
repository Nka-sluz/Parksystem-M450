#pragma once
// Merkt sich die Antworten der letzten Anfragen anhand ihrer Request-ID.
//
// Wird eine Anfrage wegen einer instabilen Verbindung wiederholt (gleiche ID),
// liefert der Empfänger die gespeicherte Antwort erneut, statt die Aktion ein
// zweites Mal auszuführen (z. B. keine doppelte Registrierung oder Zahlung).
// Fester Speicher: N Einträge mit je max. BodyLen-1 Zeichen.

#include <stddef.h>
#include <string.h>

namespace park {

template <size_t N, size_t BodyLen>
class ReplayCache {
public:
    static constexpr size_t ID_LEN = 32;

    // Gespeicherte Antwort zur ID oder nullptr. Leere IDs werden nie gefunden.
    const char* find(const char* rid) const {
        if (rid == nullptr || rid[0] == '\0') return nullptr;
        for (const auto& e : entries_) {
            if (e.used && strncmp(e.rid, rid, ID_LEN - 1) == 0) return e.body;
        }
        return nullptr;
    }

    // Speichert die Antwort; überschreibt bei Bedarf den ältesten Eintrag.
    void store(const char* rid, const char* body) {
        if (rid == nullptr || rid[0] == '\0') return;
        Entry& e = entries_[next_];
        next_ = (next_ + 1) % N;
        e.used = true;
        copy(e.rid, rid, ID_LEN);
        copy(e.body, body, BodyLen);
    }

private:
    struct Entry {
        bool used;
        char rid[ID_LEN];
        char body[BodyLen];
    };

    static void copy(char* dst, const char* src, size_t len) {
        strncpy(dst, src != nullptr ? src : "", len - 1);
        dst[len - 1] = '\0';
    }

    Entry entries_[N] = {};
    size_t next_ = 0;
};

}  // namespace park
