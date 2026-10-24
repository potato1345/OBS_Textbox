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

#include <obs-module.h>
#include <plugin-support.h>

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
#include <QLabel>
#include <QVBoxLayout>
#include <QDockWidget>

struct LiedplanContent : public QWidget {
	LiedplanContent(QWidget *parent = nullptr) : QWidget(parent)
	{
		auto *layout = new QVBoxLayout(this);
		layout->addWidget(new QLabel(QStringLiteral("Liedplan (M0 Test-Dock)")));
		layout->addWidget(
			new QLabel(QStringLiteral("Plugin-Grundgeruest ok. M1-M5 folgen.")));
		setMinimumSize(240, 100);
	}
};

/*
Eigener QDockWidget statt obs_frontend_add_dock_by_id:
add_dock_by_id legt den Dock versteckt + floating an (setVisible(false))
und er wird nur ueber das Docks-Menue sichtbar. M0 soll den Test-Dock
unmittelbar in der OBS-Oberflaeche zeigen (rechts, neben "controls").
*/
static QDockWidget *g_dock = nullptr;
#endif /* ENABLE_QT */
#endif /* ENABLE_FRONTEND_API */

bool obs_module_load(void)
{
	obs_log(LOG_INFO, "plugin loaded successfully (version %s)", PLUGIN_VERSION);
#ifdef ENABLE_FRONTEND_API
#ifdef ENABLE_QT
	if (obs_frontend_get_main_window() && !g_dock)
	{
		/* Parent = Main-Window: Widget gehoert zur OBS-Windows-Hierarchie */
		QWidget *mw = static_cast<QWidget *>(obs_frontend_get_main_window());
		g_dock = new QDockWidget(mw);
		g_dock->setObjectName(QStringLiteral("liedplan"));
		g_dock->setWindowTitle(QStringLiteral("Liedplan (Test)"));
		g_dock->setWidget(new LiedplanContent());
		bool ok = obs_frontend_add_custom_qdock("liedplan", g_dock);
		if (!ok)
		{
			/* Registrierung fehlgeschlagen: Widget nicht hängen lassen */
			delete g_dock;
			g_dock = nullptr;
		}
		obs_log(LOG_INFO, "Liedplan dock registered (ok=%d)", ok ? 1 : 0);
	}
#endif
#endif
	return true;
}

void obs_module_unload(void)
{
#ifdef ENABLE_FRONTEND_API
#ifdef ENABLE_QT
	if (g_dock)
	{
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
	obs_log(LOG_INFO, "plugin unloaded");
}
