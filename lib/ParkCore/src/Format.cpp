#include "Format.h"

#include <stdio.h>

namespace park {

void formatMoney(cfg::Money rappen, char* out, size_t outLen) {
    const char* sign = rappen < 0 ? "-" : "";
    const long abs = rappen < 0 ? -static_cast<long>(rappen) : static_cast<long>(rappen);
    snprintf(out, outLen, "%s%ld.%02ld", sign, abs / 100, abs % 100);
}

void formatClock(uint32_t ms, char* out, size_t outLen) {
    const unsigned long s = ms / 1000UL;
    snprintf(out, outLen, "%02lu:%02lu:%02lu", s / 3600UL, (s / 60UL) % 60UL, s % 60UL);
}

bool parseMoney(const char* text, cfg::Money& out) {
    if (text == nullptr) return false;
    while (*text == ' ') ++text;

    long francs = 0;
    long cents = 0;
    int digits = 0;
    int centDigits = 0;
    bool inCents = false;

    for (const char* p = text; *p != '\0'; ++p) {
        const char c = *p;
        if (c >= '0' && c <= '9') {
            if (inCents) {
                if (++centDigits > 2) return false;
                cents = cents * 10 + (c - '0');
            } else {
                if (++digits > 6) return false;  // max. CHF 999'999
                francs = francs * 10 + (c - '0');
            }
        } else if ((c == '.' || c == ',') && !inCents) {
            inCents = true;
        } else if (c == ' ') {
            // nur am Ende erlaubt
            for (const char* q = p; *q != '\0'; ++q) {
                if (*q != ' ') return false;
            }
            break;
        } else {
            return false;
        }
    }
    if (digits == 0 && centDigits == 0) return false;
    if (centDigits == 1) cents *= 10;

    out = static_cast<cfg::Money>(francs * 100 + cents);
    return true;
}

void jsonEscape(const char* in, char* out, size_t outLen) {
    if (outLen == 0) return;
    size_t o = 0;
    for (; in != nullptr && *in != '\0'; ++in) {
        const unsigned char c = static_cast<unsigned char>(*in);
        char seq[8];
        size_t n;
        if (c == '"' || c == '\\') {
            seq[0] = '\\';
            seq[1] = static_cast<char>(c);
            n = 2;
        } else if (c < 0x20) {
            n = static_cast<size_t>(snprintf(seq, sizeof(seq), "\\u%04x", c));
        } else {
            seq[0] = static_cast<char>(c);
            n = 1;
        }
        if (o + n >= outLen) break;
        for (size_t i = 0; i < n; ++i) out[o++] = seq[i];
    }
    out[o] = '\0';
}

}  // namespace park
