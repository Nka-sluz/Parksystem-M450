#pragma once
// Gibt eine HTTP-Antwort stückweise (chunked) aus einem festen Puffer aus,
// damit HTML-/JSON-Antworten ohne dynamisch wachsende Strings erzeugt werden.

#include <WebServer.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

// Schliesst die Verbindung nach einer Antwort sofort. Der WebServer bedient
// nur eine Verbindung gleichzeitig und würde sonst bis zu 5 s warten, bis der
// Browser die Verbindung schliesst – in dieser Zeit wären alle anderen
// Anfragen (auch die von Pico 2) blockiert.
inline void closeConnection(WebServer& server) { server.client().stop(500); }

class WebOut {
public:
    explicit WebOut(WebServer& server) : server_(server) {}
    WebOut(const WebOut&) = delete;
    WebOut& operator=(const WebOut&) = delete;
    ~WebOut() { end(); }

    void begin(int code, const char* contentType) {
        server_.sendHeader("Cache-Control", "no-store");
        server_.setContentLength(CONTENT_LENGTH_UNKNOWN);
        server_.send(code, contentType, "");
        open_ = true;
    }

    // Unveränderter Text (HTML-Gerüst, bereits sichere Werte).
    WebOut& raw(const char* s) {
        while (*s != '\0') put(*s++);
        return *this;
    }

    WebOut& printf(const char* fmt, ...) __attribute__((format(printf, 2, 3))) {
        char tmp[256];
        va_list args;
        va_start(args, fmt);
        vsnprintf(tmp, sizeof(tmp), fmt, args);
        va_end(args);
        return raw(tmp);
    }

    // Text mit HTML-Escaping (für alle Benutzereingaben).
    WebOut& text(const char* s) {
        for (; *s != '\0'; ++s) {
            switch (*s) {
                case '<': raw("&lt;"); break;
                case '>': raw("&gt;"); break;
                case '&': raw("&amp;"); break;
                case '"': raw("&quot;"); break;
                case '\'': raw("&#39;"); break;
                default: put(*s);
            }
        }
        return *this;
    }

    // JSON-String inkl. Anführungszeichen.
    WebOut& json(const char* s) {
        put('"');
        for (; *s != '\0'; ++s) {
            const unsigned char c = static_cast<unsigned char>(*s);
            if (c == '"' || c == '\\') {
                put('\\');
                put(static_cast<char>(c));
            } else if (c < 0x20) {
                printf("\\u%04x", c);
            } else {
                put(static_cast<char>(c));
            }
        }
        put('"');
        return *this;
    }

    // "key":"value" bzw. ,"key":"value"
    WebOut& field(const char* key, const char* value, bool first = false) {
        if (!first) put(',');
        json(key);
        put(':');
        return json(value);
    }

    void end() {
        if (!open_) return;
        flush();
        server_.sendContent(static_cast<const char*>(""), 0);
        open_ = false;
        closeConnection(server_);
    }

private:
    void put(char c) {
        if (len_ == sizeof(buf_)) flush();
        buf_[len_++] = c;
    }

    void flush() {
        if (len_ == 0) return;
        server_.sendContent(buf_, len_);
        len_ = 0;
    }

    WebServer& server_;
    char buf_[512];
    size_t len_ = 0;
    bool open_ = false;
};

// Einfache Antwort mit festem Inhalt, Verbindung danach schliessen.
inline void sendAndClose(WebServer& server, int code, const char* contentType, const char* body) {
    server.sendHeader("Cache-Control", "no-store");
    server.send(code, contentType, body);
    closeConnection(server);
}

// Liest ein Formular-/Query-Argument in einen festen Puffer (gekürzt).
inline void readArg(WebServer& server, const char* name, char* out, size_t outLen) {
    if (outLen == 0) return;
    out[0] = '\0';
    if (!server.hasArg(name)) return;
    const String v = server.arg(name);
    strncpy(out, v.c_str(), outLen - 1);
    out[outLen - 1] = '\0';
}
