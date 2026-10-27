#pragma once

#include <stdint.h>

typedef struct {
    const char *id;          // Eindeutige Kennung
    const char *icon;        // Emoji / Symbol (z.B. "🍕")
    const char *title;       // Kurztitel auf Button
    const char *message;     // Ausführlicher Text beim Empfänger
    uint32_t color_hex;      // Akzentfarbe (Hex)
    uint8_t led_code; // RGB bit mask: bit0 red, bit1 green, bit2 blue
} cyd_preset_t;

// Katalog der vorgefertigten Kurznachrichten
static const cyd_preset_t CYD_PRESETS[] = {
    {
        .id = "hunger",
        .icon = "PIZZA",
        .title = "Hunger!",
        .message = "Ich habe Hunger! Gibt es Essen?",
        .color_hex = 0xFF8800, // Orange
        .led_code = 3          // Red + green (orange)
    },
    {
        .id = "alles_gut",
        .icon = ":-)",
        .title = "Alles gut",
        .message = "Mir geht es gut!",
        .color_hex = 0x22C55E, // Grün
        .led_code = 2          // Grün
    },
    {
        .id = "hab_dich_lieb",
        .icon = "<3",
        .title = "Hab dich lieb",
        .message = "Ich mag dich sehr! <3",
        .color_hex = 0xEF4444, // Rot/Pink
        .led_code = 1          // Rot
    },
    {
        .id = "schlafen",
        .icon = "Zzz",
        .title = "Schlafen",
        .message = "Ich gehe jetzt schlafen. Gute Nacht!",
        .color_hex = 0x6366F1, // Indigo/Blau
        .led_code = 4          // Blue
    },
    {
        .id = "kaffee",
        .icon = "COFFEE",
        .title = "Kaffee?",
        .message = "Lust auf eine Kaffeepause?",
        .color_hex = 0xD97706, // Bernstein/Braun
        .led_code = 3          // Red + green (amber)
    },
    {
        .id = "beschaeftigt",
        .icon = "STOP",
        .title = "Bitte Ruhe",
        .message = "Bin gerade beschaeftigt / im Call!",
        .color_hex = 0xDC2626, // Dunkelrot
        .led_code = 1          // Rot
    },
    {
        .id = "gleich_da",
        .icon = "-->",
        .title = "Gleich da",
        .message = "Bin in 5 Minuten da!",
        .color_hex = 0x06B6D4, // Cyan
        .led_code = 6          // Green + blue (cyan)
    },
    {
        .id = "feierabend",
        .icon = "PARTY",
        .title = "Feierabend!",
        .message = "Geschafft fuer heute! Feierabend!",
        .color_hex = 0xF59E0B, // Gold
        .led_code = 3          // Grün
    }
};

#define CYD_PRESETS_COUNT (sizeof(CYD_PRESETS) / sizeof(CYD_PRESETS[0]))
