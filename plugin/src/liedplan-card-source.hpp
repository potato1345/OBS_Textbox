/*
 * liedplan-card-source.hpp — Deklaration der nativen OBS-Quellen-Definition für Liedplan-Karten (v3).
 *
 * Zweck:
 *   - Deklariert die externe obs_source_info Struktur liedplan_card_source_info.
 *   - Bindet die gerenderten Liedplan-Karten direkt in den OBS-Szenengraph ein.
 */

#pragma once

#include <obs.h>

/**
 * Definition der nativen Liedplan-Kartenquelle für libobs (obs_register_source).
 */
extern struct obs_source_info liedplan_card_source_info;
