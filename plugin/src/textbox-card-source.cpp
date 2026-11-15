/*
 * textbox-card-source.cpp — Native Videoquelle für Textbox-Karten in OBS Studio (v3).
 *
 * Zweck:
 *   - Implementiert obs_source_info für die Video-Quelle "textbox_card".
 *   - Konvertiert Einstellungen (obs_data_t) in TextboxCard-Strukturen.
 *   - Rendert die Karte per QPainter in eine Textur (render_card) und zeichnet
 *     diese im OBS-Grafik-Thread (gs_draw_sprite).
 */

#include "textbox-card-source.hpp"
#include "textbox-render.hpp"
#include "textbox-state.hpp"
#include "textbox-log.hpp"

#include <graphics/graphics.h>

#include <obs.h>

#include <algorithm>
#include <cstdarg>
#include <atomic>
#include <cstdio>

namespace {

/**
 * Ausführliche Diagnosezeilen für die Kartenquelle (nur bei gesetztem TEXTBOX_DEBUG=1).
 */
// Schreibt Debug-Nachrichten in den OBS-Log (stderr-Kanal).
void debug_log(const char *fmt, ...)
{
	if (!TextboxDebugEnabled())
		return;
	char buf[512];
	va_list args;
	va_start(args, fmt);
	vsnprintf(buf, sizeof(buf), fmt, args);
	va_end(args);
	blog(LOG_INFO, "[textbox] %s", buf);
}

/**
 * Interner Kontext der nativen Kartenquelle.
 */
// Laufzeit-Kontext einer Karten-Quelle: OBS-Source, Settings und renderter Stand.
struct TextboxCardSource {
	obs_source_t *source = nullptr;
	gs_texture_t *texture = nullptr;
	TextboxCard card;
	int render_calls = 0;
	int tick_calls = 0;
	// IMP-T: Texture-Rebuild wird in den Video-Thread verschoben — rebuild im
	// update-Pfad (UI-Thread) zerstörte eine Textur, die video_render parallel
	// zeichnete (Use-after-free bei Settings-Update während Wiedergabe).
	std::atomic<bool> tex_dirty{false};
};

/**
 * Liest die Karteneinstellungen aus dem OBS-Datenobjekt (obs_data_t) aus.
 */
TextboxCard settings_to_card(obs_data_t *settings)
{
	TextboxCard card;
	if (!settings)
		return card;

	const char *card_id = obs_data_get_string(settings, "card_id");
	const char *zeile1 = obs_data_get_string(settings, "zeile1");
	const char *zeile2 = obs_data_get_string(settings, "zeile2");
	const char *zeile3 = obs_data_get_string(settings, "zeile3");
	const char *profile = obs_data_get_string(settings, "profile");
	const char *text_color = obs_data_get_string(settings, "text_color");
	const char *box_color = obs_data_get_string(settings, "box_color");

	card.id = card_id ? card_id : "";
	card.zeile1 = zeile1 ? zeile1 : "";
	card.zeile2 = zeile2 ? zeile2 : "";
	card.zeile3 = zeile3 ? zeile3 : "";
	if (profile && *profile)
		card.profile = profile;
	if (text_color && *text_color)
		card.text_color = text_color;
	if (box_color && *box_color)
		card.box_color = box_color;

	card.text_opacity = std::clamp(static_cast<int>(obs_data_get_int(settings, "text_opacity")), 0, 255);
	card.box_opacity = std::clamp(static_cast<int>(obs_data_get_int(settings, "box_opacity")), 0, 255);

	return card;
}

/**
 * Rendert die Karte neu und aktualisiert die GPU-Textur (gs_texture_t).
 */
// Rendert die Karte neu in die gs_texture (QPainter-Render-Modul, Skaling).
void rebuild_texture(TextboxCardSource *ctx)
{
	if (!ctx)
		return;

	QImage img = render_card(ctx->card, 0.0);
	if (img.format() != QImage::Format_ARGB32)
		img = img.convertToFormat(QImage::Format_ARGB32);

	const uint8_t *bits = img.constBits();
	const uint32_t w = static_cast<uint32_t>(img.width());
	const uint32_t h = static_cast<uint32_t>(img.height());

	obs_enter_graphics();
	if (ctx->texture) {
		gs_texture_destroy(ctx->texture);
		ctx->texture = nullptr;
	}
	if (bits && w > 0 && h > 0) {
		ctx->texture = gs_texture_create(w, h, GS_BGRA, 1, &bits, 0);
	}
	obs_leave_graphics();

	debug_log("rebuild_texture: id='%s' z1='%s' img=%ux%u fmt=%d texture=%p",
	        ctx->card.id.c_str(), ctx->card.zeile1.c_str(), w, h, (int)img.format(),
	        (void *)ctx->texture);
}

const char *textbox_card_get_name(void *type_data)
{
	(void)type_data;
	return "Textbox Karte";
}

// Default-Settings der Quelle (Karten-ID und Feld-Defaults).
void textbox_card_get_defaults(obs_data_t *settings)
{
	if (!settings)
		return;

	obs_data_set_default_string(settings, "card_id", "");
	obs_data_set_default_string(settings, "zeile1", "");
	obs_data_set_default_string(settings, "zeile2", "");
	obs_data_set_default_string(settings, "zeile3", "");
	obs_data_set_default_string(settings, "profile", "OBS Dark");
	obs_data_set_default_string(settings, "text_color", "#000000");
	obs_data_set_default_int(settings, "text_opacity", 255);
	obs_data_set_default_string(settings, "box_color", "#ffffff");
	obs_data_set_default_int(settings, "box_opacity", 255);
}

// Erzeugt den Quellen-Kontext und baut die initiale Textur.
void *textbox_card_create(obs_data_t *settings, obs_source_t *source)
{
	auto *ctx = new TextboxCardSource();
	ctx->source = source;
	ctx->card = settings_to_card(settings);
	blog(LOG_INFO, "[textbox] Kartenquelle erstellt (ID='%s')", ctx->card.id.c_str());
	rebuild_texture(ctx);
	return ctx;
}

// Gibt Kontext und gs_texture frei.
void textbox_card_destroy(void *data)
{
	auto *ctx = static_cast<TextboxCardSource *>(data);
	if (!ctx)
		return;

	blog(LOG_INFO, "[textbox] Kartenquelle freigegeben (ID='%s')", ctx->card.id.c_str());

	if (ctx->texture) {
		obs_enter_graphics();
		gs_texture_destroy(ctx->texture);
		ctx->texture = nullptr;
		obs_leave_graphics();
	}

	delete ctx;
}

// übernimmt neue obs_data-Settings und re-baut die Textur.
void textbox_card_update(void *data, obs_data_t *settings)
{
	auto *ctx = static_cast<TextboxCardSource *>(data);
	if (!ctx)
		return;

	ctx->card = settings_to_card(settings);
	tb_log("Kartenquelle aktualisiert (ID='" + ctx->card.id + "')");
	ctx->tex_dirty = true; // Rebuild geschieht im video_tick (Render-Thread)
}

// --- Temporäre E2E-Debug-Callbacks (M2a): Wo bricht die Render-Kette ab? ---
void textbox_card_activate(void *data)
{
	auto *ctx = static_cast<TextboxCardSource *>(data);
	debug_log("activate: ctx=%p id='%s'", (void *)ctx, ctx ? ctx->card.id.c_str() : "?");
}

// Bei Sichtbarkeit: Textur re-bauen, falls noch leer.
void textbox_card_show(void *data)
{
	auto *ctx = static_cast<TextboxCardSource *>(data);
	debug_log("show: ctx=%p id='%s'", (void *)ctx, ctx ? ctx->card.id.c_str() : "?");
}

// Tick: re-baut die Textur nur bei geänderten Settings (Dirty-Check).
void textbox_card_video_tick(void *data, float seconds)
{
	auto *ctx = static_cast<TextboxCardSource *>(data);
	if (!ctx)
		return;
	(void)seconds;
	if (ctx->tick_calls < 3)
		debug_log("video_tick #%d: ctx=%p (Video-Thread lebt, Source wird getickt)",
		            ctx->tick_calls, (void *)ctx);
	ctx->tick_calls++;
	if (ctx->tex_dirty.exchange(false))
		rebuild_texture(ctx);
}

// Aktuelle Kartenbreite für das OBS-Layout.
uint32_t textbox_card_get_width(void *data)
{
	(void)data;
	return static_cast<uint32_t>(kTextboxCardWidth);
}

// Aktuelle Kartenhöhe für das OBS-Layout.
uint32_t textbox_card_get_height(void *data)
{
	(void)data;
	return static_cast<uint32_t>(kTextboxCardHeight);
}

// Zeichnet die Karten-Textur mit dem gs_effect in den Render-Output.
void textbox_card_video_render(void *data, gs_effect_t *effect)
{
	auto *ctx = static_cast<TextboxCardSource *>(data);
	if (!ctx || !ctx->texture) {
		if (ctx && ctx->render_calls < 3) {
			debug_log("video_render EARLY-RETURN: ctx=%p texture=%p (no texture!) width_cb=%u",
			        (void *)ctx, (void *)ctx->texture, textbox_card_get_width(data));
		}
		return;
	}

	if (!effect)
		effect = obs_get_base_effect(OBS_EFFECT_DEFAULT);

	if (ctx->render_calls < 3)
		debug_log("video_render call#%d: texture=%p effect=%p W=%d H=%d", ctx->render_calls,
		        (void *)ctx->texture, (void *)effect, kTextboxCardWidth, kTextboxCardHeight);
	ctx->render_calls++;

	const bool previous = gs_framebuffer_srgb_enabled();
	gs_enable_framebuffer_srgb(true);

	gs_blend_state_push();
	gs_blend_function(GS_BLEND_SRCALPHA, GS_BLEND_INVSRCALPHA);

	gs_eparam_t *const param = gs_effect_get_param_by_name(effect, "image");
	gs_effect_set_texture_srgb(param, ctx->texture);

	gs_draw_sprite(ctx->texture, 0, static_cast<uint32_t>(kTextboxCardWidth),
		       static_cast<uint32_t>(kTextboxCardHeight));

	gs_blend_state_pop();

	gs_enable_framebuffer_srgb(previous);
}

} // namespace

struct obs_source_info textbox_card_source_info = {
	"textbox_card",            // id
	OBS_SOURCE_TYPE_INPUT,      // type
	OBS_SOURCE_VIDEO,           // output_flags
	textbox_card_get_name,     // get_name
	textbox_card_create,       // create
	textbox_card_destroy,      // destroy
	textbox_card_get_width,    // get_width
	textbox_card_get_height,   // get_height
	textbox_card_get_defaults, // get_defaults
	nullptr,                    // get_properties
	textbox_card_update,       // update
	textbox_card_activate,     // activate (E2E-Debug M2a)
	nullptr,                    // deactivate
	textbox_card_show,         // show (E2E-Debug M2a)
	nullptr,                    // hide
	textbox_card_video_tick,   // video_tick (E2E-Debug M2a)
	textbox_card_video_render, // video_render
};
