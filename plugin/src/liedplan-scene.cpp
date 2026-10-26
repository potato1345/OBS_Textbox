/*
Liedplan — Standalone OBS-Plugin (T7)
Copyright (C) 2026 Johann

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License along
with this program. If not, see <https://www.gnu.org/licenses/>
*/

#include "liedplan-scene.hpp"
#include "liedplan-html.hpp"
#include "liedplan-render.hpp"

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

} // namespace

std::string source_name(const std::string &card_id)
{
	return "Liedplan " + card_id;
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
	// WICHTIG: kein "shutdown"-Setting setzen — obs-browser interpretiert
	// shutdown:true als "CEF-Source angehalten" → schwarze Box.
	// Default (nicht gesetzt / false) = Source startet und rendert.

	return settings;
}

obs_data_t *make_settings(const LiedplanCard &card)
{
	const std::string dir = liedplan_html::cards_dir();
	const std::string local_file =
		dir.empty() ? (card.id + ".html") : (dir + "/" + card.id + ".html");
	return make_settings(card, local_file);
}

void send_to_current_scene(const LiedplanCard &card)
{
#ifdef ENABLE_FRONTEND_API
	obs_source_t *scene_src = obs_frontend_get_current_scene();
	if (!scene_src) {
		obs_log(LOG_WARNING, "Liedplan: keine aktuelle Szene verfügbar");
		return;
	}

	obs_scene_t *scene = obs_scene_from_source(scene_src);
	if (!scene) {
		obs_source_release(scene_src);
		return;
	}

	const std::string name = source_name(card.id);
	obs_sceneitem_t *item = obs_scene_find_source(scene, name.c_str());
	if (item) {
		obs_source_t *src = obs_sceneitem_get_source(item);
		if (is_browser_source(src)) {
			std::string html_path;
			if (liedplan_html::write_card(card, html_path)) {
				obs_log(LOG_INFO, "Liedplan: HTML geschrieben: %s", html_path.c_str());
			}
			liedplan_html::push_update(src, card, false);
		} else {
			const char *old_id = src ? obs_source_get_id(src) : "";
			obs_log(LOG_INFO, "Liedplan: migriere %s (alter Typ '%s' -> %s)",
				name.c_str(), old_id ? old_id : "", kBrowserSourceId);
			obs_sceneitem_remove(item);
			item = nullptr;
		}
	}

	if (!item) {
		std::string html_path;
		if (liedplan_html::write_card(card, html_path)) {
			obs_log(LOG_INFO, "Liedplan: HTML geschrieben: %s", html_path.c_str());
		}

		obs_data_t *settings = make_settings(card, html_path);
		obs_source_t *src =
			obs_source_create(kBrowserSourceId, name.c_str(), settings, nullptr);
		obs_data_release(settings);

		if (src) {
			item = obs_scene_add(scene, src);
			if (item) {
				struct vec2 pos = {0.0f, 0.0f};
				obs_sceneitem_set_pos(item, &pos);
			}
			obs_source_release(src);
			obs_log(LOG_INFO, "Liedplan: Browser-Source angelegt: %s (%s)",
				name.c_str(), html_path.c_str());
		}
	}

	const char *scene_name = obs_source_get_name(obs_scene_get_source(scene));
	obs_log(LOG_INFO, "Liedplan: %s -> Szene %s", name.c_str(), scene_name ? scene_name : "");

	obs_source_release(scene_src);
#else
	(void)card;
#endif
}

void update_everywhere(const LiedplanCard &card, bool clear)
{
	LiedplanCard disk_card = card;
	if (clear) {
		disk_card.zeile1.clear();
		disk_card.zeile2.clear();
		disk_card.zeile3.clear();
	}

	std::string html_path;
	if (liedplan_html::write_card(disk_card, html_path)) {
		obs_log(LOG_INFO, "Liedplan: HTML geschrieben: %s", html_path.c_str());
	}

	const std::string name = source_name(card.id);
	const std::vector<obs_scene_t *> scenes = collect_all_scenes();

	for (obs_scene_t *scene : scenes) {
		obs_sceneitem_t *item = obs_scene_find_source(scene, name.c_str());
		if (!item)
			continue;

		obs_source_t *src = obs_sceneitem_get_source(item);
		if (!is_browser_source(src))
			continue;

		liedplan_html::push_update(src, card, clear);
	}
}

void remove_everywhere(const std::string &card_id)
{
	const std::string name = source_name(card_id);
	const std::vector<obs_scene_t *> scenes = collect_all_scenes();

	for (obs_scene_t *scene : scenes) {
		obs_sceneitem_t *item = obs_scene_find_source(scene, name.c_str());
		if (!item)
			continue;

		// WICHTIG (32.2.2, in .deps verifiziert): obs_scene_find_source und
		// obs_sceneitem_get_source geben BORROWTE Pointer zurueck (kein addref).
		// obs_sceneitem_remove entlaedt das Item (ref 1->0) und das Item gibt
		// beim Destroys seine Source-Ref frei. Eine weitere obs_source_release
		// hier waere ein Double-Release (Crash). Daher: kein Release.
		obs_sceneitem_remove(item);
	}
}

} // namespace liedplan_scene
