/*
 * liedplan-scene.cpp — OBS-Szenen-Integration für Textbox-Karten (v3).
 *
 * Zweck:
 *   - Verwaltet OBS-Browserquellen für Textbox-Karten in aktuellen oder allen Szenen.
 *   - Erzeugt die Quellen mit CEF-freundlichen Standardparametern (Breite, Höhe, FPS).
 *   - Behandelt automatische Migration alter Quellentypen und Präfixe (Liedplan -> Textbox).
 *   - Aktualisiert Karten-Inhalte (HTML/CSS) oder blendet diese bei Leeren/Löschen aus.
 */

#include "liedplan-scene.hpp"
#include "liedplan-html.hpp"
#include "liedplan-render.hpp"
#include "liedplan-log.hpp"

#include <plugin-support.h>

#ifdef ENABLE_FRONTEND_API
#include <obs-frontend-api.h>
#endif

#include <cstring>
#include <vector>

namespace liedplan_scene {

namespace {

constexpr const char *kBrowserSourceId = "browser_source";
constexpr int kBrowserCardHeight = 600;
constexpr int kBrowserFps = 30;

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

std::string legacy_source_name(const std::string &card_id)
{
	return "Liedplan " + card_id;
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
			blog(LOG_INFO, "[liedplan] Quelle '%s' umbenannt in '%s'",
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

obs_data_t *make_settings(const LiedplanCard &card, const std::string &local_file)
{
	(void)card;
	obs_data_t *settings = obs_data_create();

	obs_data_set_bool(settings, "is_local_file", true);
	obs_data_set_string(settings, "local_file", local_file.c_str());
	obs_data_set_int(settings, "width", kLiedplanCardWidth);
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

obs_data_t *make_settings(const LiedplanCard &card)
{
	const std::string dir = liedplan_html::cards_dir();
	const std::string local_file =
		dir.empty() ? (liedplan_html::sanitize_card_id(card.id) + ".html")
			: (dir + "/" + liedplan_html::sanitize_card_id(card.id) + ".html");
	return make_settings(card, local_file);
}

void send_to_current_scene(const LiedplanCard &card, const LiedplanSettings &settings)
{
#ifdef ENABLE_FRONTEND_API
	obs_source_t *scene_src = obs_frontend_get_current_scene();
	if (!scene_src) {
		blog(LOG_WARNING, "[liedplan] Keine aktuelle Szene verfügbar");
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
				blog(LOG_INFO, "[liedplan] events_listen für '%s' aktiviert", name.c_str());
			}
			if (sd)
				obs_data_release(sd);
			std::string html_path;
			if (liedplan_html::write_card(card, html_path, settings)) {
				lp_log("HTML geschrieben: " + html_path);
			}
			liedplan_html::push_update(src, card, false, settings,
						 liedplan_html::cards_dir());
		} else {
			const char *old_id = src ? obs_source_get_id(src) : "";
			blog(LOG_INFO, "[liedplan] Migriere %s (alter Typ '%s' -> %s)",
				name.c_str(), old_id ? old_id : "", kBrowserSourceId);
			obs_sceneitem_remove(item);
			item = nullptr;
		}
	}

	if (!item) {
		std::string html_path;
		if (liedplan_html::write_card(card, html_path, settings)) {
			lp_log("HTML geschrieben: " + html_path);
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
			blog(LOG_INFO, "[liedplan] Browser-Quelle angelegt: %s (%s)",
				name.c_str(), html_path.c_str());
		}
	}

	const char *scene_name = obs_source_get_name(obs_scene_get_source(scene));
	blog(LOG_INFO, "[liedplan] Sende Karte '%s' an Szene '%s'", name.c_str(), scene_name ? scene_name : "");

	obs_source_release(scene_src);
#else
	(void)card;
	(void)settings;
#endif
}

void update_everywhere(const LiedplanCard &card, bool clear, const LiedplanSettings &settings)
{
	LiedplanCard disk_card = card;
	if (clear) {
		disk_card.zeile1.clear();
		disk_card.zeile2.clear();
		disk_card.zeile3.clear();
		// IMP-T: rows mit-leeren — effective_rows() bevorzugt rows, sonst behält
		// die Disk-HTML den Text und zeigt ihn nach jedem Reload wieder.
		disk_card.rows.clear();
	}

	std::string html_path;
	if (liedplan_html::write_card(disk_card, html_path, settings)) {
		lp_log("HTML geschrieben: " + html_path);
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

		liedplan_html::push_update(src, card, clear, settings,
					   liedplan_html::cards_dir());
	}
}

void push_all_scenes(const LiedplanCard &card, const LiedplanSettings &settings)
{
	// IMP-S: Text-/Farbänderungen live ohne Reload in allen Szenen pushen.
	const std::string name = source_name(card.id);
	const std::vector<obs_scene_t *> scenes = collect_all_scenes();
	const std::string dir = liedplan_html::cards_dir();
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
			blog(LOG_INFO, "[liedplan] events_listen für '%s' aktiviert", name.c_str());
		}
		if (sd)
			obs_data_release(sd);
		liedplan_html::push_update(src, card, false, settings, dir);
		blog(LOG_INFO, "[liedplan] Live-Push für '%s'", name.c_str());
	}
}

void reload_everywhere(const LiedplanCard &card, const LiedplanSettings &settings)
{
	std::string html_path;
	if (liedplan_html::write_card(card, html_path, settings)) {
		lp_log("HTML geschrieben (Template-Reload): " + html_path);
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
		if (liedplan_html::reload_browser_source(src))
			++reloaded;
	}
	blog(LOG_INFO, "[liedplan] Template-Reload für '%s': %d Browser-Quelle(n)",
	     name.c_str(), reloaded);
}

void remove_everywhere(const std::string &card_id)
{
	const std::string name = source_name(card_id);
	const std::string legacy = legacy_source_name(card_id);
	blog(LOG_INFO, "[liedplan] Entferne Quelle '%s' aus allen Szenen", name.c_str());
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

} // namespace liedplan_scene
