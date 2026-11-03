/*
 * liedplan-render.hpp — QPainter-basiertes Rendering von Liedplan-Karten in QImages (v3).
 *
 * Zweck:
 *   - Definiert die Abmessungen der Karten (kLiedplanCardWidth = 800, kLiedplanCardHeight = 400).
 *   - Deklariert render_card zum Zeichnen von Karten mit Texten, Farbverläufen und Ecken.
 *
 * Hinweis: libobs-frei; wird auch in Standalone-Tests (test-render) verwendet.
 */

#pragma once

#include "liedplan-state.hpp"

#include <QImage>
#include <QtGlobal>

/** Standardbreite einer nativ gerenderten Liedplan-Karte in Pixeln. */
inline constexpr int kLiedplanCardWidth = 800;

/** Standardhöhe einer nativ gerenderten Liedplan-Karte in Pixeln. */
inline constexpr int kLiedplanCardHeight = 400;

/**
 * Rendert eine LiedplanCard als ARGB32-QImage mit transparentem Hintergrund.
 *
 * @param card - Die Kartendaten (Texte, Farben, Deckkraft).
 * @param time_seconds - Zeitstempel für optionale Animationen (z. B. Farbrotation).
 * @returns Das gerenderte QImage.
 */
QImage render_card(const LiedplanCard &card, qreal time_seconds = 0.0);
QImage render_card(const LiedplanCard &card, int width, int height, qreal time_seconds);
