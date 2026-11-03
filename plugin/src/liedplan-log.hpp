/*
 * liedplan-log.hpp — zentrale Debug-Log-Helfer des Liedplan-OBS-Plugins (v3, plugin/).
 *
 * Zweck:
 *   - lp_log(msg): ausführliche Diagnosezeilen (pro Feld, pro HTTP-Header o. ä.).
 *     Wird NUR ausgegeben, wenn die Umgebungsvariable LIEDPLAN_DEBUG=1 gesetzt ist.
 *   - Normale Lifecycle-Ereignisse (Plugin load/unload, Dock registriert, State
 *     geladen/gespeichert, Import, Szene-Push, Quellen-Migration) loggen die
 *     aufrufenden Dateien UNBEDINGT direkt per blog(LOG_INFO, "[liedplan] ...").
 *
 * Zusammenspiel / Einschränkungen:
 *   - Benötigt libobs (blog). Darf deshalb NUR in Übersetzungseinheiten verwendet
 *     werden, die ausschließlich ins Plugin-Modul gelinkt werden (plugin-main.cpp,
 *     liedplan-dock.cpp, liedplan-scene.cpp, liedplan-card-source.cpp).
 *   - Die Qt-/OBS-freien Test-Targets (liedplan-tests, -import-, -ai-, -textfmt-,
 *     -theme-icons-, -render-tests) linken kein libobs; die dort kompilierten
 *     Dateien (liedplan-state/import/ai/html/render.cpp sowie alle Header) dürfen
 *     diesen Header NICHT einbinden.
 *   - Der Header ist header-only und muss nicht in CMakeLists.txt eingetragen werden.
 */

#pragma once

#include <obs.h>

#include <cstdlib>
#include <cstring>
#include <string>

/*
 * Liefert true, wenn LIEDPLAN_DEBUG=1 in der Umgebung gesetzt ist.
 * Die Variable wird beim ersten Aufruf gelesen und danach gecacht (Function-local
 * static → thread-sichere Initialisierung seit C++11). Änderungen an der Umgebung
 * zur Laufzeit werden bewusst ignoriert; OBS muss mit gesetzter Variable starten.
 */
inline bool LiedplanDebugEnabled()
{
	static const bool enabled = [] {
#ifdef _MSC_VER
		// MSVC markiert getenv als unsicher (C4996, mit /WX ein Fehler) →
		// _dupenv_s ist das sichere getenv-Äquivalent; Puffer muss freigegeben werden.
		char *value = nullptr;
		size_t len = 0;
		bool on = _dupenv_s(&value, &len, "LIEDPLAN_DEBUG") == 0 && value != nullptr &&
			  std::strcmp(value, "1") == 0;
		std::free(value);
		return on;
#else
		const char *value = std::getenv("LIEDPLAN_DEBUG");
		return value != nullptr && std::strcmp(value, "1") == 0;
#endif
	}();
	return enabled;
}

/*
 * Verbose-Debugzeile mit Präfix "[liedplan]" ins OBS-Log — nur bei LIEDPLAN_DEBUG=1.
 * Ohne gesetzte Variable ist der Aufruf ein reiner Bool-Check (kein Seiteneffekt).
 * Hinweis: den String am Aufrufort möglichst billig bauen, da er auch bei
 * deaktiviertem Debug konstruiert wird.
 */
inline void lp_log(const std::string &msg)
{
	if (LiedplanDebugEnabled())
		blog(LOG_INFO, "[liedplan] %s", msg.c_str());
}
