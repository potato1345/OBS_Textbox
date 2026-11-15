/*
 * textbox-card-source.hpp — Deklaration der nativen OBS-Quellen-Definition für Textbox-Karten (v3).
 *
 * Zweck:
 *   - Deklariert die externe obs_source_info Struktur textbox_card_source_info.
 *   - Bindet die gerenderten Textbox-Karten direkt in den OBS-Szenengraph ein.
 */

#pragma once

#include <obs.h>

/**
 * Definition der nativen Textbox-Kartenquelle für libobs (obs_register_source).
 */
extern struct obs_source_info textbox_card_source_info;
