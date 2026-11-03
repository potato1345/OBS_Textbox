/*
 * plugin-main.cpp — Haupteinstiegspunkt für das Liedplan OBS Studio Plugin (v3).
 *
 * Zweck:
 *   - Registriert das OBS-Modul (OBS_DECLARE_MODULE, obs_module_load, obs_module_unload).
 *   - Registriert die native Liedplan-Kartenquelle (liedplan_card_source_info).
 *   - Erzeugt und bindet das benutzerdefinierte QDockWidget (LiedplanDock) über die
 *     OBS-Frontend-API ein.
 *   - Unterstützt abgerundete Ecken im Floating-Fensterzustand via Event-Filter (LiedplanFloatRoundedHelper).
 */

#include <obs-module.h>
#include <plugin-support.h>
#include <util/bmem.h>
#include "liedplan-log.hpp"

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
#include "liedplan-dock.hpp"
#include <QDockWidget>
#include <QEvent>
#include <QRect>
#include <string>

/*
Eigener QDockWidget statt obs_frontend_add_dock_by_id:
add_dock_by_id legt den Dock versteckt + floating an (setVisible(false))
und er wird nur ueber das Docks-Menue sichtbar. M0 soll den Test-Dock
unmittelbar in der OBS-Oberflaeche zeigen (rechts, neben "controls").
*/
static QDockWidget *g_dock = nullptr;

/*
M7h: Runde untere Fensterecken im Floating-Zustand.
Ein floatender QDockWidget ist ein opak-rechteckiges Top-Level-Fenster;
ein QSS border-radius am Kind (LiedplanDock) rundet nur die gemalte
Flaeche — hinter den Ecken bleibt die undurchsichtige Fensterregion.
Fuellig transparente Ecken brauchen daher ein transluzentes +
framelesses Fenster, damit der abgerundete Kind-Hintergrund bis an
den Fensterrand reicht. Beides wird beim ersten Show des
Floating-Zustands genau einmal gesetzt (Guard: Attribut-Check).
*/
struct LiedplanFloatRoundedHelper : public QObject
{
	QDockWidget *dock;

	explicit LiedplanFloatRoundedHelper(QDockWidget *d) : QObject(d), dock(d)
	{
		dock->installEventFilter(this);
	}

	bool eventFilter(QObject *obj, QEvent *ev) override
	{
		if (obj == dock &&
		    (ev->type() == QEvent::Show || ev->type() == QEvent::WindowStateChange)) {
			if (dock->isWindow() &&
			    !(dock->windowState() &
			      (Qt::WindowMinimized | Qt::WindowFullScreen))) {
				if (!dock->testAttribute(Qt::WA_TranslucentBackground)) {
					const QRect geom = dock->geometry();
					dock->setAttribute(Qt::WA_TranslucentBackground, true);
					dock->setWindowFlags(
						dock->windowFlags() | Qt::FramelessWindowHint);
					dock->setGeometry(geom);
					dock->hide();
					dock->show();
				}
			}
		}
		return QObject::eventFilter(obj, ev);
	}
};
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
	blog(LOG_INFO, "[liedplan] Plugin erfolgreich geladen (Version %s)", PLUGIN_VERSION);
#ifdef ENABLE_FRONTEND_API
#ifdef ENABLE_QT
	if (obs_frontend_get_main_window() && !g_dock) {
		char *cfg_path = obs_module_config_path("liedplan-state.json");
		std::string state_path = cfg_path ? cfg_path : "";
		bfree(cfg_path);

		/* Parent = Main-Window: Widget gehoert zur OBS-Windows-Hierarchie */
		QWidget *mw = static_cast<QWidget *>(obs_frontend_get_main_window());
		g_dock = new QDockWidget(mw);
		g_dock->setObjectName(QStringLiteral("liedplan"));
		g_dock->setWindowTitle(QStringLiteral("Liedplan"));
		g_dock->setWidget(new LiedplanDock(state_path, g_dock));
		/* M7h: runde untere Ecken im Floating-Zustand (liefert sich selbst) */
		new LiedplanFloatRoundedHelper(g_dock);
		bool ok = obs_frontend_add_custom_qdock("liedplan", g_dock);
		if (!ok) {
			/* Registrierung fehlgeschlagen: Widget nicht hängen lassen */
			delete g_dock;
			g_dock = nullptr;
		}
		blog(LOG_INFO, "[liedplan] Liedplan-Dock registriert (Erfolg=%s)", ok ? "ja" : "nein");
	}
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
#ifdef ENABLE_QT
	if (g_dock) {
		/*
		obs_frontend_remove_dock loest den Custom-Dock nur aus dem
		QMainWindow-Layout (extraCustomDocks = rohe Zeiger, kein shared_ptr).
		Deshalb muss das QDockWidget-Objekt hier selbst gelöscht werden,
		sonst entsteht ein Leak (Review agy, Achse 4).
		*/
		obs_frontend_remove_dock("liedplan");
		delete g_dock;
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
	blog(LOG_INFO, "[liedplan] Plugin entladen");
}
