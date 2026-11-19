#include "Protocol.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

namespace proto {

bool getField(const char* body, const char* key, char* out, size_t outLen) {
    if (outLen == 0) return false;
    out[0] = '\0';
    if (body == nullptr) return false;

    const size_t keyLen = strlen(key);
    const char* line = body;
    while (*line != '\0') {
        const char* end = strchr(line, '\n');
        if (end == nullptr) end = line + strlen(line);

        if (static_cast<size_t>(end - line) > keyLen && strncmp(line, key, keyLen) == 0 &&
            line[keyLen] == '=') {
            const char* value = line + keyLen + 1;
            size_t n = static_cast<size_t>(end - value);
            if (n > 0 && value[n - 1] == '\r') --n;
            if (n >= outLen) n = outLen - 1;
            memcpy(out, value, n);
            out[n] = '\0';
            return true;
        }
        line = *end == '\n' ? end + 1 : end;
    }
    return false;
}

namespace {

void urlEncode(const char* in, char* out, size_t outLen) {
    static const char hex[] = "0123456789ABCDEF";
    size_t o = 0;
    for (; *in != '\0'; ++in) {
        const unsigned char c = static_cast<unsigned char>(*in);
        const bool plain = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') ||
                           c == '-' || c == '_' || c == '.';
        if (plain) {
            if (o + 1 >= outLen) break;
            out[o++] = static_cast<char>(c);
        } else {
            if (o + 3 >= outLen) break;
            out[o++] = '%';
            out[o++] = hex[c >> 4];
            out[o++] = hex[c & 0x0F];
        }
    }
    out[o] = '\0';
}

}  // namespace

void formBody(char* out, size_t outLen, const char* plate, bool manual, const char* rid) {
    if (plate == nullptr) {
        snprintf(out, outLen, "%s=1&%s=%s", P_MANUAL, P_RID, rid);
        return;
    }
    char encoded[160];
    urlEncode(plate, encoded, sizeof(encoded));
    snprintf(out, outLen, "%s=%s%s&%s=%s", P_PLATE, encoded, manual ? "&manual=1" : "", P_RID, rid);
}

bool parseReply(const char* body, park::KasseReply& out) {
    out = park::KasseReply{};
    char result[16];
    if (!getField(body, F_RESULT, result, sizeof(result))) return false;
    out.accepted = strcmp(result, R_GRANTED) == 0 || strcmp(result, R_OPEN) == 0 ||
                   strcmp(result, R_OK) == 0;

    char token[24];
    getField(body, F_REASON, token, sizeof(token));
    out.reason = park::reasonFromToken(token);
    getField(body, F_ID, out.id, sizeof(out.id));
    char due[16];
    if (getField(body, F_DUE, due, sizeof(due))) out.due = static_cast<cfg::Money>(atol(due));
    return true;
}

}  // namespace proto
