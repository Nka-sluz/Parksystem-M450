// Startet alle Komponententests auf dem PC. Build & Run: test/host/README.md

#include <stdio.h>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#endif

#include "Config.h"

static_assert(PARK_TEST_CONFIG == 1, "Tests erwarten die Testkonfiguration");

int checks = 0;
int failures = 0;

void runParkCoreTests();
void runGateControllerTests();
void runTc15Test();

int main() {
#ifdef _WIN32
    // Ausgaben sind UTF-8 (Umlaute); die Windows-Konsole erwartet sonst CP850.
    SetConsoleOutputCP(CP_UTF8);
#endif
#ifdef TC15_PRICE
    // Zweiter Build von run-tests.bat: nur TC-15. Die übrigen Tests rechnen
    // mit den Beträgen der Testkonfiguration (CHF 1.00 pro Minute).
    runTc15Test();
    printf("\nTC-15: %d Prüfungen, %d Fehler\n", checks, failures);
    return failures == 0 ? 0 : 1;
#endif
    puts("== Kasse (Pico 1): ParkingSystem mit FakeClock ==");
    runParkCoreTests();
    puts("\n== Schranken (Pico 2): GateController mit Test Doubles ==");
    runGateControllerTests();
    printf("\n%d Prüfungen, %d Fehler\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
