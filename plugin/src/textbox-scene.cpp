/*
 * textbox-scene.cpp — OBS-Szenen-Integration für Textbox-Karten (v3).
 *
 * Zweck:
 *   - Verwaltet OBS-Browserquellen für Textbox-Karten in aktuellen oder allen Szenen.
 *   - Erzeugt die Quellen mit CEF-freundlichen Standardparametern (Breite, Höhe, FPS).
 *   - Behandelt automatische Migration alter Quellentypen und Präfixe (Textbox -> Textbox).
 *   - Aktualisiert Karten-Inhalte (HTML/CSS) oder blendet diese bei Leeren/Löschen aus.
 */

#include "textbox-scene.hpp"
#include "textbox-html.hpp"
#include "textbox-render.hpp"
#include "textbox-log.hpp"

#include <plugin-support.h>

#ifdef ENABLE_FRONTEND_API
#include <obs-frontend-api.h>
#endif

#include <cstring>
#include <vector>

namespace textbox_scene {

namespace {

constexpr const char *kBrowserSourceId = "browser_source";
constexpr int kBrowserCardHeight = 600;
constexpr int kBrowserFps = 30;

// erkennt OBS-Browser-Sources (Typ "browser_source").
bool is_browser_source(const obs_source_t *src)
{
	if (!src)
		return false;
	const char *id = obs_source_get_unversioned_id(src);
	if (!id)
		id = obs_source_get_id(src);
	return id && std::strcmp(id, kBrowserSourceId) == 0;
}

// Callback für obs_enum_sources / obs_enum_scenes: sammelt alle Szenen-Sources.
bool collect_scene_cb(void *param, obs_source_t *source)
{
	if (source && obs_source_is_scene(source)) {
		auto *scenes = static_cast<std::vector<obs_scene_t *> *>(param);
		if (obs_scene_t *scene = obs_scene_from_source(source))
			scenes->push_back(scene);
	}
	return true;
}

// Sammelt alle Szenen über obs_enum_sources (und zusätzlich obs_enum_scenes, falls Szenen separat verwaltet werden).
std::vector<obs_scene_t *> collect_all_scenes()
{
	std::vector<obs_scene_t *> scenes;
	obs_enum_sources(collect_scene_cb, &scenes);
	if (scenes.empty())
		obs_enum_scenes(collect_scene_cb, &scenes);
	return scenes;
}

// historischer Quellen-Name einer Karte (Fallback für Namens-Matching aus v1).
std::string legacy_source_name(const std::string &card_id)
{
	return "Textbox " + card_id;
}

obs_sceneitem_t *find_card_sceneitem(obs_scene_t *scene, const std::string &card_id,
				     bool migrate_name = true)
{
	const std::string name = source_name(card_id);
	obs_sceneitem_t *item = obs_scene_find_source(scene, name.c_str());
	if (item)
		return item;
	const std::string legacy = legacy_source_name(card_id);
	item = obs_scene_find_source(scene, legacy.c_str());
	if (item && migrate_name) {
		obs_source_t *src = obs_sceneitem_get_source(item);
		if (src && is_browser_source(src)) {
			obs_source_set_name(src, name.c_str());
			blog(LOG_INFO, "[textbox] Quelle '%s' umbenannt in '%s'",
			     legacy.c_str(), name.c_str());
		}
	}
	return item;
}

} // namespace

std::string source_name(const std::string &card_id)
{
	return "Textbox " + card_id;
}

obs_data_t *make_settings(const TextboxCard &card, const std::string &local_file)
{
	(void)card;
	obs_data_t *settings = obs_data_create();

	obs_data_set_bool(settings, "is_local_file", true);
	obs_data_set_string(settings, "local_file", local_file.c_str());
	obs_data_set_int(settings, "width", kTextboxCardWidth);
	obs_data_set_int(settings, "height", kBrowserCardHeight);
	obs_data_set_int(settings, "fps", kBrowserFps);
	// IMP-S: "Listen for events" — ohne dieses Setting ist der
	// javascript_event-Proc ein No-Op und Live-Pushes erreichen den Render nie.
	obs_data_set_bool(settings, "events_listen", true);
	// WICHTIG: kein "shutdown"-Setting setzen — obs-browser interpretiert
	// shutdown:true als "CEF-Source angehalten" → schwarze Box.
	// Default (nicht gesetzt / false) = Source startet und rendert.

	return settings;
}

// legt die Karten-Browser-Source in der aktiven Szene an oder updated sie live.
void send_to_current_scene(const TextboxCard &card, const TextboxSettings &settings)
{
#ifdef ENABLE_FRONTEND_API
	obs_source_t *scene_src = obs_frontend_get_current_scene();
	if (!scene_src) {
		blog(LOG_WARNING, "[textbox] Keine aktuelle Szene verfügbar");
		return;
	}

	obs_scene_t *scene = obs_scene_from_source(scene_src);
	if (!scene) {
		obs_source_release(scene_src);
		return;
	}

	const std::string name = source_name(card.id);
	obs_sceneitem_t *item = find_card_sceneitem(scene, card.id, true);
	if (item) {
		obs_source_t *src = obs_sceneitem_get_source(item);
		if (is_browser_source(src)) {
			// IMP-S: ältere Quellen ohne "Listen for events" selbst-heilen,
			// sonst bleibt der Live-Push (javascript_event) ein No-Op.
			obs_data_t *sd = obs_source_get_settings(src);
			if (sd && !obs_data_get_bool(sd, "events_listen")) {
				obs_data_set_bool(sd, "events_listen", true);
				obs_source_update(src, sd);
				blog(LOG_INFO, "[textbox] events_listen für '%s' aktiviert", name.c_str());
			}
			if (sd)
				obs_data_release(sd);
			std::string html_path;
			if (textbox_html::write_card(card, html_path, settings)) {
				tb_log("HTML geschrieben: " + html_path);
			}
			textbox_html::push_update(src, card, false, settings,
						 textbox_html::cards_dir());
		} else {
			const char *old_id = src ? obs_source_get_id(src) : "";
			blog(LOG_INFO, "[textbox] Migriere %s (alter Typ '%s' -> %s)",
				name.c_str(), old_id ? old_id : "", kBrowserSourceId);
			obs_sceneitem_remove(item);
			item = nullptr;
		}
	}

	if (!item) {
		std::string html_path;
		if (textbox_html::write_card(card, html_path, settings)) {
			tb_log("HTML geschrieben: " + html_path);
		}

		obs_data_t *bs_settings = make_settings(card, html_path);
		obs_source_t *src =
			obs_source_create(kBrowserSourceId, name.c_str(), bs_settings, nullptr);
		obs_data_release(bs_settings);

		if (src) {
			item = obs_scene_add(scene, src);
			if (item) {
				struct vec2 pos = {0.0f, 0.0f};
				obs_sceneitem_set_pos(item, &pos);
			}
			obs_source_release(src);
			blog(LOG_INFO, "[textbox] Browser-Quelle angelegt: %s (%s)",
				name.c_str(), html_path.c_str());
		}
	}

	const char *scene_name = obs_source_get_name(obs_scene_get_source(scene));
	blog(LOG_INFO, "[textbox] Sende Karte '%s' an Szene '%s'", name.c_str(), scene_name ? scene_name : "");

	obs_source_release(scene_src);
#else
	(void)card;
	(void)settings;
#endif
}

// updated bzw. cleared die Karte in allen Szenen, in denen ihre Source existiert.
void update_everywhere(const TextboxCard &card, bool clear, const TextboxSettings &settings)
{
	TextboxCard disk_card = card;
	if (clear) {
		disk_card.zeile1.clear();
		disk_card.zeile2.clear();
		disk_card.zeile3.clear();
		// IMP-T: rows mit-leeren — effective_rows() bevorzugt rows, sonst behält
		// die Disk-HTML den Text und zeigt ihn nach jedem Reload wieder.
		disk_card.rows.clear();
	}

	std::string html_path;
	if (textbox_html::write_card(disk_card, html_path, settings)) {
		tb_log("HTML geschrieben: " + html_path);
	}

	const std::string name = source_name(card.id);
	const std::vector<obs_scene_t *> scenes = collect_all_scenes();

	for (obs_scene_t *scene : scenes) {
		obs_sceneitem_t *item = find_card_sceneitem(scene, card.id, true);
		if (!item)
			continue;

		obs_source_t *src = obs_sceneitem_get_source(item);
		if (!is_browser_source(src))
			continue;

		textbox_html::push_update(src, card, clear, settings,
					   textbox_html::cards_dir());
	}
}

// pushed Kartendaten (CSS-Vars + Event) in alle Szenen mit der Source.
void push_all_scenes(const TextboxCard &card, const TextboxSettings &settings)
{
	// IMP-S: Text-/Farbänderungen live ohne Reload in allen Szenen pushen.
	const std::string name = source_name(card.id);
	const std::vector<obs_scene_t *> scenes = collect_all_scenes();
	const std::string dir = textbox_html::cards_dir();
	for (obs_scene_t *scene : scenes) {
		obs_sceneitem_t *item = find_card_sceneitem(scene, card.id, true);
		if (!item)
			continue;
		obs_source_t *src = obs_sceneitem_get_source(item);
		if (!is_browser_source(src))
			continue;
		obs_data_t *sd = obs_source_get_settings(src);
		if (sd && !obs_data_get_bool(sd, "events_listen")) {
			obs_data_set_bool(sd, "events_listen", true);
			obs_source_update(src, sd);
			blog(LOG_INFO, "[textbox] events_listen für '%s' aktiviert", name.c_str());
		}
		if (sd)
			obs_data_release(sd);
		textbox_html::push_update(src, card, false, settings, dir);
		blog(LOG_INFO, "[textbox] Live-Push für '%s'", name.c_str());
	}
}

// triggert Reload der Browser-Sources in allen Szenen (neu injiziertes HTML).
void reload_everywhere(const TextboxCard &card, const TextboxSettings &settings)
{
	std::string html_path;
	if (textbox_html::write_card(card, html_path, settings)) {
		tb_log("HTML geschrieben (Template-Reload): " + html_path);
	}

	const std::string name = source_name(card.id);
	const std::vector<obs_scene_t *> scenes = collect_all_scenes();

	int reloaded = 0;
	for (obs_scene_t *scene : scenes) {
		obs_sceneitem_t *item = find_card_sceneitem(scene, card.id, true);
		if (!item)
			continue;
		obs_source_t *src = obs_sceneitem_get_source(item);
		if (!is_browser_source(src))
			continue;
		if (textbox_html::reload_browser_source(src))
			++reloaded;
	}
	blog(LOG_INFO, "[textbox] Template-Reload für '%s': %d Browser-Quelle(n)",
	     name.c_str(), reloaded);
}

// entfernt die Karten-Source aus allen Szenen.
void remove_everywhere(const std::string &card_id)
{
	const std::string name = source_name(card_id);
	const std::string legacy = legacy_source_name(card_id);
	blog(LOG_INFO, "[textbox] Entferne Quelle '%s' aus allen Szenen", name.c_str());
	const std::vector<obs_scene_t *> scenes = collect_all_scenes();

	for (obs_scene_t *scene : scenes) {
		// WICHTIG (32.2.2, in .deps verifiziert): obs_scene_find_source und
		// obs_sceneitem_get_source geben BORROWTE Pointer zurueck (kein addref).
		// obs_sceneitem_remove entlaedt das Item (ref 1->0) und das Item gibt
		// beim Destroys seine Source-Ref frei. Eine weitere obs_source_release
		// hier waere ein Double-Release (Crash). Daher: kein Release.
		if (obs_sceneitem_t *item = obs_scene_find_source(scene, name.c_str()))
			obs_sceneitem_remove(item);
		if (obs_sceneitem_t *legacy_item = obs_scene_find_source(scene, legacy.c_str()))
			obs_sceneitem_remove(legacy_item);
	}
}

// true, wenn die Karten-Source in irgendeiner Szene existiert.
bool source_exists(const std::string &card_id)
{
	const std::string name = source_name(card_id);
	const std::string legacy = legacy_source_name(card_id);
	const std::vector<obs_scene_t *> scenes = collect_all_scenes();
	for (obs_scene_t *scene : scenes) {
		obs_sceneitem_t *item = obs_scene_find_source(scene, name.c_str());
		if (item && is_browser_source(obs_sceneitem_get_source(item)))
			return true;
		obs_sceneitem_t *legacy_item = obs_scene_find_source(scene, legacy.c_str());
		if (legacy_item && is_browser_source(obs_sceneitem_get_source(legacy_item)))
			return true;
	}
	return false;
}

} // namespace textbox_scene
