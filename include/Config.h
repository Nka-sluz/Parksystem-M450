#pragma once
// =============================================================================
//  Zentrale Konfiguration des Parksystems (gilt für beide Picos)
//
//  Alle Werte, die der Parkhausbesitzer festlegt (Kapazität, Tarif, Timeouts),
//  stehen ausschliesslich hier. Nach einer Änderung beide Picos neu flashen.
//
//  PARK_TEST_CONFIG = 1 → Testkonfiguration aus dem Testkonzept (Abschnitt 3)
//  PARK_TEST_CONFIG = 0 → Produktivkonfiguration
// =============================================================================

#include <stdint.h>

#ifndef PARK_TEST_CONFIG
#define PARK_TEST_CONFIG 1
#endif

namespace cfg {

// Geldbeträge werden durchgehend in Rappen (ganzzahlig) geführt.
using Money = int32_t;

constexpr uint32_t MINUTE_MS = 60UL * 1000UL;
constexpr uint32_t HOUR_MS = 60UL * MINUTE_MS;

#if PARK_TEST_CONFIG
// ---------------------------- Testkonfiguration ------------------------------
constexpr uint16_t CAPACITY = 3;                    // Stellplätze
constexpr uint32_t TARIFF_UNIT_MS = 1 * MINUTE_MS;  // Zeiteinheit
constexpr Money TARIFF_PRICE_PER_UNIT = 100;        // Preis pro angefangene Einheit (CHF 1.00)
constexpr uint32_t TARIFF_FREE_MS = 1 * MINUTE_MS;  // Freigrenze
constexpr Money TARIFF_DAILY_MAX = 5 * TARIFF_PRICE_PER_UNIT;  // Tageshöchstsatz
constexpr uint32_t PAYMENT_GRACE_MS = 2 * MINUTE_MS;  // Karenzzeit nach Bezahlung
constexpr uint16_t LOG_CAPACITY = 10;                 // Protokoll-Obergrenze
#else
// --------------------------- Produktivkonfiguration --------------------------
constexpr uint16_t CAPACITY = 50;
constexpr uint32_t TARIFF_UNIT_MS = 1 * HOUR_MS;
constexpr Money TARIFF_PRICE_PER_UNIT = 300;  // CHF 3.00 pro angefangene Stunde
constexpr uint32_t TARIFF_FREE_MS = 15 * MINUTE_MS;
constexpr Money TARIFF_DAILY_MAX = 2500;  // CHF 25.00
constexpr uint32_t PAYMENT_GRACE_MS = 15 * MINUTE_MS;
constexpr uint16_t LOG_CAPACITY = 200;
#endif

// Ein Tag für den Tageshöchstsatz (bewusst nicht an die Testkonfiguration gekoppelt).
constexpr uint32_t TARIFF_DAY_MS = 24UL * HOUR_MS;

// Schranke schliesst automatisch, wenn keine Durchfahrt bestätigt wird.
constexpr uint32_t GATE_OPEN_TIMEOUT_MS = 30UL * 1000UL;

// Zusätzliche Registerplätze für bereits ausgefahrene Fahrzeuge (Anzeige
// "ausgefahren"). Ein ausgefahrener Eintrag wird bei Platzmangel überschrieben.
constexpr uint16_t REGISTER_HISTORY = 10;
constexpr uint16_t REGISTER_SIZE = CAPACITY + REGISTER_HISTORY;

// Kennzeichen: max. Länge eines gültigen Kennzeichens (ohne Ersatz-ID).
constexpr uint8_t PLATE_MAX_LEN = 10;
// Speichergrösse für ein Kennzeichen / einen Ersatz-Identifikator inkl. '\0'.
constexpr uint8_t ID_BUF_LEN = 16;
constexpr const char* MANUAL_PREFIX = "MANUAL-";

// ------------------------------- Netzwerk -----------------------------------
// Pico 1 (Kasse) spannt ein eigenes WLAN auf, Pico 2 und der Laptop/das
// Smartphone des Bedieners verbinden sich damit. Kein Internet nötig.
constexpr const char* WIFI_SSID = "Parkhaus-M450";
constexpr const char* WIFI_PASSWORD = "parkhaus450";  // mind. 8 Zeichen

constexpr uint8_t KASSE_IP[4] = {192, 168, 42, 1};
constexpr uint8_t SCHRANKEN_IP[4] = {192, 168, 42, 2};
constexpr uint8_t NETMASK[4] = {255, 255, 255, 0};
constexpr uint16_t HTTP_PORT = 80;

// Pico 2 → Pico 1: maximale Wartezeit auf eine Antwort. Bleibt sie aus,
// bleibt die Schranke geschlossen (Fail-safe). Muss < 2 s sein (Reaktionszeit).
constexpr uint16_t KASSE_HTTP_TIMEOUT_MS = 1500;
// Pico 2 fragt die Belegung periodisch ab und zeigt den Verbindungsstatus an.
constexpr uint32_t STATUS_POLL_INTERVAL_MS = 5000;
// Pico 2 versucht in diesem Abstand, die WLAN-Verbindung wiederherzustellen.
constexpr uint32_t WIFI_RECONNECT_INTERVAL_MS = 10000;

// ------------------------------- Hardware (Pico 2) ---------------------------
constexpr uint8_t SERVO_ENTRY_PIN = 8;  // GP8 (Pin 11) – Einfahrtsschranke
constexpr uint8_t SERVO_EXIT_PIN = 9;   // GP9 (Pin 12) – Ausfahrtsschranke
constexpr int SERVO_CLOSED_DEG = 0;     // Schranke zu (Balken waagrecht)
constexpr int SERVO_OPEN_DEG = 90;      // Schranke offen (Balken senkrecht)
constexpr int SERVO_MIN_US = 500;       // SG90 Pulsbreite 0°
constexpr int SERVO_MAX_US = 2400;      // SG90 Pulsbreite 180°

}  // namespace cfg
