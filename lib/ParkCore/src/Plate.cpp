#include "Plate.h"

#include <ctype.h>
#include <string.h>

namespace park {

namespace {

bool isUpperAlpha(char c) { return c >= 'A' && c <= 'Z'; }
bool isDigit(char c) { return c >= '0' && c <= '9'; }

bool isSpace(char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n'; }

}  // namespace

bool isManualId(const char* s) {
    const size_t prefixLen = strlen(cfg::MANUAL_PREFIX);
    if (strncmp(s, cfg::MANUAL_PREFIX, prefixLen) != 0) return false;
    const char* digits = s + prefixLen;
    size_t n = strlen(digits);
    if (n < 1 || n > 5) return false;
    for (size_t i = 0; i < n; ++i) {
        if (!isDigit(digits[i])) return false;
    }
    return true;
}

PlateCheck normalizeId(const char* raw, char* out, size_t outLen, bool allowManualId) {
    if (outLen == 0) return PlateCheck::BadFormat;
    out[0] = '\0';
    if (raw == nullptr) return PlateCheck::Empty;

    // Leerzeichen am Rand ignorieren.
    while (isSpace(*raw)) ++raw;
    size_t len = strlen(raw);
    while (len > 0 && isSpace(raw[len - 1])) --len;

    if (len == 0) return PlateCheck::Empty;
    if (len >= outLen) return PlateCheck::TooLong;

    char buf[cfg::ID_BUF_LEN];
    if (len >= sizeof(buf)) return PlateCheck::TooLong;
    for (size_t i = 0; i < len; ++i) {
        buf[i] = static_cast<char>(toupper(static_cast<unsigned char>(raw[i])));
    }
    buf[len] = '\0';

    if (isManualId(buf)) {
        if (!allowManualId) return PlateCheck::Reserved;
        memcpy(out, buf, len + 1);
        return PlateCheck::Ok;
    }

    if (len > cfg::PLATE_MAX_LEN) return PlateCheck::TooLong;
    if (len < 2) return PlateCheck::BadFormat;
    if (!isUpperAlpha(buf[0]) || buf[len - 1] == '-') return PlateCheck::BadFormat;

    bool hasDigit = false;
    for (size_t i = 0; i < len; ++i) {
        const char c = buf[i];
        if (isDigit(c)) {
            hasDigit = true;
        } else if (!isUpperAlpha(c) && c != '-') {
            return PlateCheck::BadFormat;
        }
    }
    if (!hasDigit) return PlateCheck::BadFormat;

    memcpy(out, buf, len + 1);
    return PlateCheck::Ok;
}

void sanitizeForLog(const char* raw, char* out, size_t outLen) {
    if (outLen == 0) return;
    if (raw == nullptr) raw = "";
    while (isSpace(*raw)) ++raw;

    size_t o = 0;
    size_t i = 0;
    for (; raw[i] != '\0' && o + 1 < outLen; ++i) {
        const unsigned char c = static_cast<unsigned char>(raw[i]);
        const bool printable = c >= 0x20 && c <= 0x7E && c != ';' && c != '"' && c != '\\';
        out[o++] = printable ? static_cast<char>(c) : '?';
    }
    // Abgeschnittene Eingaben mit '~' am Ende kennzeichnen.
    if (raw[i] != '\0' && o > 0) out[o - 1] = '~';
    while (o > 0 && out[o - 1] == ' ') --o;
    out[o] = '\0';

    if (o == 0) {
        strncpy(out, "(leer)", outLen - 1);
        out[outLen - 1] = '\0';
    }
}

}  // namespace park
