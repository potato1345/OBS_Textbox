/*
 * textbox-render.hpp — QPainter-basiertes Rendering von Textbox-Karten in QImages (v3).
 *
 * Zweck:
 *   - Definiert die Abmessungen der Karten (kTextboxCardWidth = 800, kTextboxCardHeight = 400).
 *   - Deklariert render_card zum Zeichnen von Karten mit Texten, Farbverläufen und Ecken.
 *
 * Hinweis: libobs-frei; wird auch in Standalone-Tests (test-render) verwendet.
 */

#pragma once

#include "textbox-state.hpp"

#include <QImage>
#include <QtGlobal>

/** Standardbreite einer nativ gerenderten Textbox-Karte in Pixeln. */
inline constexpr int kTextboxCardWidth = 800;

/** Standardhöhe einer nativ gerenderten Textbox-Karte in Pixeln. */
inline constexpr int kTextboxCardHeight = 400;

/**
 * Rendert eine TextboxCard als ARGB32-QImage mit transparentem Hintergrund.
 *
 * @param card - Die Kartendaten (Texte, Farben, Deckkraft).
 * @param time_seconds - Zeitstempel für optionale Animationen (z. B. Farbrotation).
 * @returns Das gerenderte QImage.
 */
QImage render_card(const TextboxCard &card, qreal time_seconds = 0.0);
QImage render_card(const TextboxCard &card, int width, int height, qreal time_seconds);
