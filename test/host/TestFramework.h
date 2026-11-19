#pragma once
// Minimales Test-Framework für die Komponententests auf dem PC.

#include <stdint.h>
#include <stdio.h>
#include <string.h>

extern int checks;
extern int failures;

#define CHECK(cond)                                                    \
    do {                                                               \
        ++checks;                                                      \
        if (!(cond)) {                                                 \
            ++failures;                                                \
            printf("  FEHLER %s:%d: %s\n", __FILE__, __LINE__, #cond); \
        }                                                              \
    } while (0)

// Vergleicht zwei Texte und zeigt bei einem Fehler beide an.
#define CHECK_STR(actual, expected)                                                       \
    do {                                                                                  \
        ++checks;                                                                         \
        const char* a_ = (actual);                                                        \
        const char* e_ = (expected);                                                      \
        if (strcmp(a_, e_) != 0) {                                                        \
            ++failures;                                                                   \
            printf("  FEHLER %s:%d: \"%s\", erwartet \"%s\"\n", __FILE__, __LINE__, a_, e_); \
        }                                                                                 \
    } while (0)

constexpr uint32_t S = 1000;
constexpr uint32_t MIN = 60 * S;
