#include "Protocol.h"

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

}  // namespace proto
