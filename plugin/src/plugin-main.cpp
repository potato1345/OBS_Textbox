/*
 * plugin-main.cpp — Haupteinstiegspunkt für das Textbox OBS Studio Plugin (v3).
 *
 * Zweck:
 *   - Registriert das OBS-Modul (OBS_DECLARE_MODULE, obs_module_load, obs_module_unload).
 *   - Registriert die native Kartenquelle.
 *   - Erzeugt und bindet das benutzerdefinierte QDockWidget (TextboxDock) über die
 *     OBS-Frontend-API ein.
 *   - Floating-Docks werden als native OS-Fenster belassen (kein FramelessWindowHint).
 */

#include <obs-module.h>
#include <plugin-support.h>
#include <util/bmem.h>
#include "textbox-log.hpp"
#include "textbox-state.hpp"
#include "textbox-scene.hpp"

#include <atomic>
#include <chrono>
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
#include <QDockWidget>
#include <QPointer>
#include <QGuiApplication>
#include <QRect>
#include <QScreen>
#include <QTimer>
#include <string>

/*
Eigener QDockWidget statt obs_frontend_add_dock_by_id:
add_dock_by_id legt den Dock versteckt + floating an (setVisible(false))
und er wird nur ueber das Docks-Menue sichtbar. M0 soll den Test-Dock
unmittelbar in der OBS-Oberflaeche zeigen (rechts, neben "controls").
*/
/* QPointer: wird automatisch null, wenn OBS das Main-Window (Eltern des Docks)
 * zerstört. OBSApp::~OBSApp zerstört das Fenster VOR obs_shutdown ->
 * obs_module_unload; ein rohes delete wäre dann ein Double-Free (AV in
 * qt6widgets, Crash beim Fenster-Schließen). */
static QPointer<QDockWidget> g_dock;

// Shutdown-Callback: entfernt alle Browser-Sources und stoppt Timer, damit OBS sauber schließt.
static void textbox_frontend_event(obs_frontend_event event, void *)
{
	if (event != OBS_FRONTEND_EVENT_SCRIPTING_SHUTDOWN)
		return;
	blog(LOG_INFO, "[textbox] Shutdown-Event empfangen — räume Browser-Sources auf");
	textbox_scene::remove_all_sources();
}

/*
IMP-L2: schwebende Docks koennen hoeher als der Bildschirm werden; dann liegt
die untere Werkzeugleiste (Gear/Undo/Export) unterhalb des Viewports und ist
unerreichbar. Auf die sichtbare Screen-Hoehe beschneiden. Wird beim Load und
im Spaet-Timer gebraucht (war vorher zweimal kopiert).
*/
static void clamp_dock_to_screen(QDockWidget *d)
{
	if (!d->isFloating())
		return;
	const QRect avail = QGuiApplication::primaryScreen()->availableGeometry();
	QRect geom = d->geometry();
	if (geom.bottom() > avail.bottom()) {
		geom.setBottom(avail.bottom());
		d->setGeometry(geom);
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
	if (obs_frontend_get_main_window() && !g_dock) {
		char *cfg_path = obs_module_config_path("textbox-state.json");
		std::string state_path = cfg_path ? cfg_path : "";
		bfree(cfg_path);

		/* Parent = Main-Window: Widget gehoert zur OBS-Windows-Hierarchie */
		QWidget *mw = static_cast<QWidget *>(obs_frontend_get_main_window());
		g_dock = new QDockWidget(mw);
		g_dock->setObjectName(QStringLiteral("textbox"));
		g_dock->setWindowTitle(QStringLiteral("Textbox Studio"));
		g_dock->setWidget(new TextboxDock(state_path, g_dock));
		bool ok = obs_frontend_add_custom_qdock("textbox", g_dock);
		if (!ok) {
			/* Registrierung fehlgeschlagen: Widget nicht hängen lassen */
			delete g_dock.data();
			g_dock = nullptr;
		} else {
			/* IMP-H8: OBS 32 versteckt Custom-QDockWidget per Default und
			 * legt keinen Docks-Menueeintrag an. Floating-Zustand wieder
			 * herstellen (Dock ist als schwebendes Fenster registriert). */
			g_dock->setFloating(true);
			/* IMP-L: geschlossene Docks brauchen einen Wiederoeffnungs-
			 * weg; OBS legt fuer custom-Docks keinen Menueeintrag an. */
			obs_frontend_add_tools_menu_item(
				"Textbox-Dock anzeigen",
				[](void *param) {
					auto *d = static_cast<QDockWidget *>(param);
					d->show();
					d->raise();
					d->activateWindow();
				},
				g_dock);
			g_dock->show();
			g_dock->raise();
			/* IMP-L2: schwebende Docks koennen hoeher als der Bildschirm
			 * werden; dann liegt die untere Werkzeugleiste (Gear/Undo/
			 * Export) unterhalb des Viewports und ist unerreichbar.
			 * Auf die sichtbare Screen-Hoehe beschneiden. */
			clamp_dock_to_screen(g_dock);
			/* OBS versteckt Custom-Docks evtl. verzoegert nach dem Load;
			 * Show erneut nach 1s und 3s erzwingen. QPointer statt Roh-Pointer:
			 * nach obs_plugin_unload ist g_dock geloescht, die Timer duerfen
			 * dann nicht mehr dereferenziert werden. */
			const QPointer<QDockWidget> show_dock = g_dock;
			QTimer::singleShot(1000, g_dock, [show_dock] {
				if (show_dock) {
					show_dock->show();
					show_dock->raise();
				}
			});
			QTimer::singleShot(3000, g_dock, [show_dock] {
				QDockWidget *d = show_dock.data();
				if (!d)
					return;
				d->show();
				d->raise();
				clamp_dock_to_screen(d);
			});
		}
		blog(LOG_INFO, "[textbox] Textbox-Dock registriert (Erfolg=%s)", ok ? "ja" : "nein");
	}
	// Shutdown-Event-Callback für saubere Browser-Source-Bereinigung
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
	textbox_scene::remove_all_sources();
#ifdef ENABLE_QT
	if (g_dock) {
		/*
		obs_frontend_remove_dock loest den Custom-Dock aus dem
		QMainWindow-Layout. Das QDockWidget wird nur hier selbst geloescht,
		wenn es noch lebt (QPointer != null). Ist OBSs Main-Window bereits
		zerstört (normaler Shutdown-Pfad), hat Qt das Dock schon geloescht;
		ein weiteres delete waere ein Double-Free und crasht beim Schliessen.
		*/
		obs_frontend_remove_dock("textbox");
		delete g_dock.data();
		g_dock = nullptr;
	}
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
