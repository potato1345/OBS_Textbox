/*
 * plugin-main.cpp — Haupteinstiegspunkt für das Textbox OBS Studio Plugin (v3).
 *
 * Zweck:
 *   - Registriert das OBS-Modul (OBS_DECLARE_MODULE, obs_module_load, obs_module_unload).
 *   - Registriert die native Kartenquelle.
 *   - Erzeugt und bindet das benutzerdefinierte QDockWidget (TextboxDock) über die
 *     OBS-Frontend-API ein. The dock inherits the OBS theme natively (PTZ-style).
 */

#include <obs-module.h>
#include <plugin-support.h>
#include <util/bmem.h>
#include "textbox-log.hpp"
#include "textbox-state.hpp"
#include "textbox-scene.hpp"

#include <atomic>
#include <chrono>
#include <cstdlib>
#include <thread>

// Zähler für aktuell laufende Hintergrund-Threads im Plugin.
std::atomic<int> g_active_jobs = 0;

#ifdef ENABLE_FRONTEND_API
#include <obs-frontend-api.h>
#endif

/*
OBS_DECLARE_MODULE: erzwingt die von libobs verlangten C-Exporte
(obs_module_set_pointer, obs_module_ver, obs_module_load mit extern "C").
Ohne diesen Makro findet OBS die DLL zwar, ueberspringt sie aber
stille (fehlende Exporte).
*/
OBS_DECLARE_MODULE()

#ifdef ENABLE_FRONTEND_API
#ifdef ENABLE_QT
#include "textbox-dock.hpp"
#include <QPointer>
#include <string>

/* QPointer: wird automatisch null, wenn OBS das Main-Window (Eltern des Docks)
 * zerstört. OBSApp::~OBSApp zerstört das Fenster VOR obs_shutdown ->
 * obs_module_unload; ein rohes delete wäre dann ein Double-Free (AV in
 * qt6widgets, Crash beim Fenster-Schließen). */
static QPointer<TextboxDock> g_textbox_dock;

/* Shutdown-Callback: räumt NUR Plugin-eigene Timer/Callbacks auf.
 * Browser-Sources (Typ browser_source) sind eingebaut und können ohne
 * Plugin weiterlaufen — sie beim Shutdown zu löschen wäre Data-Loss
 * (User-Szene-Quellen gingen bei jedem OBS-Beenden verloren). */
static void textbox_frontend_event(obs_frontend_event event, void *)
{
	switch (event) {
	case OBS_FRONTEND_EVENT_SCRIPTING_SHUTDOWN:
		blog(LOG_INFO, "[textbox] Shutdown-Event empfangen — Timer/Callbacks werden beendet");
		break;
	case OBS_FRONTEND_EVENT_THEME_CHANGED:
		/* Defer to let layout changes settle, like obs-ptz does */
		if (g_textbox_dock)
			QTimer::singleShot(0, g_textbox_dock, &TextboxDock::refreshTheme);
		break;
	default:
		break;
	}
}

#endif /* ENABLE_QT */
#endif /* ENABLE_FRONTEND_API */

/**
 * Initialisiert das Plugin beim Laden durch OBS Studio.
 * Registriert Quellen und Dock-Widgets.
 *
 * @returns true bei erfolgreicher Initialisierung.
 */
bool obs_module_load(void)
{
	// GPU-Process-Konflikt mit OBS-GFX/NVIDIA Overlay führt zu Crashes;
	// Software-Render für Text-Karten ist genug.
#ifdef _WIN32
	_putenv_s("QTWEBENGINE_CHROMIUM_FLAGS", "--disable-gpu --disable-gpu-compositing --in-process-gpu");
#else
	setenv("QTWEBENGINE_CHROMIUM_FLAGS", "--disable-gpu --disable-gpu-compositing --in-process-gpu", 1);
#endif

	blog(LOG_INFO, "[textbox] Plugin erfolgreich geladen (Version %s)", PLUGIN_VERSION);

	// Optionale Referenztabelle im Plugin-Data-Verzeichnis via obs_module_file auflösen
	// (nur wenn der Nutzer dort eine referenz.md ablegt; sonst bleibt der Pfad leer).
	char *mod_referenz = obs_module_file("referenz.md");
	if (mod_referenz) {
		textbox_set_module_referenz_path(mod_referenz);
		bfree(mod_referenz);
	}
#ifdef ENABLE_FRONTEND_API
#ifdef ENABLE_QT
	if (obs_frontend_get_main_window() && !g_textbox_dock) {
		char *cfg_path = obs_module_config_path("textbox-state.json");
		std::string state_path = cfg_path ? cfg_path : "";
		bfree(cfg_path);

		/* Parent = Main-Window: Widget gehört zur OBS-Windows-Hierarchie.
		 * Like obs-ptz ptz-controls.cpp line 60: use obs_frontend_add_dock_by_id
		 * so OBS wraps the widget in its theme-styled DockWidget (gives the
		 * #272A33 background and rounded bottom corners). */
		QWidget *mw = static_cast<QWidget *>(obs_frontend_get_main_window());
		auto *dock_widget = new TextboxDock(state_path, mw);

		if (!obs_frontend_add_dock_by_id("textbox-studio-dock", "Textbox Studio", dock_widget))
			blog(LOG_ERROR, "[textbox] failed to add dock via obs_frontend_add_dock_by_id");
		else
			g_textbox_dock = dock_widget;

		blog(LOG_INFO, "[textbox] Textbox-Dock registriert");
	}
	// Theme-change + shutdown-Event-Callback
	obs_frontend_add_event_callback(textbox_frontend_event, nullptr);
#endif
#endif
	return true;
}

/**
 * Bereinigt Ressourcen beim Entladen des Plugins durch OBS Studio.
 */
void obs_module_unload(void)
{
#ifdef ENABLE_FRONTEND_API
	obs_frontend_remove_event_callback(textbox_frontend_event, nullptr);
	/* KEIN remove_all_sources(): Browser-Sources sind eingebaut und bleiben
	 * in der Szene erhalten (Data-Loss-Fix — Quellen durften beim Entladen
	 * nicht verschwinden). */
#ifdef ENABLE_QT
	g_textbox_dock = nullptr; /* QPointer — OBS-owned, no manual delete */
#endif
#endif
	// Bis zu 5 Sekunden (in 100-ms-Schritten) warten, bis alle aktiven Hintergrund-Jobs beendet sind.
	for (int i = 0; i < 50 && g_active_jobs.load() > 0; ++i) {
		std::this_thread::sleep_for(std::chrono::milliseconds(100));
	}
#ifdef ENABLE_QT
	/*
	libobs (32.2.2, in .deps/include/obs-source.h und obs.h verifiziert)
	deklariert kein obs_unregister_source; registrierte Source-Typen werden
	analog zu image-source.c und obs-text.cpp durch obs_shutdown() freigegeben.
	*/
#endif
	blog(LOG_INFO, "[textbox] Plugin entladen");
}