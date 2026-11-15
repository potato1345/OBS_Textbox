/*
 * textbox-log.hpp — zentrale Debug-Log-Helfer des Textbox-OBS-Plugins (v3, plugin/).
 *
 * Zweck:
 *   - tb_log(msg): ausführliche Diagnosezeilen (pro Feld, pro HTTP-Header o. ä.).
 *     Wird NUR ausgegeben, wenn die Umgebungsvariable TEXTBOX_DEBUG=1 gesetzt ist.
 *   - Normale Lifecycle-Ereignisse (Plugin load/unload, Dock registriert, State
 *     geladen/gespeichert, Import, Szene-Push, Quellen-Migration) loggen die
 *     aufrufenden Dateien UNBEDINGT direkt per blog(LOG_INFO, "[textbox] ...").
 *
 * Zusammenspiel / Einschränkungen:
 *   - Benötigt libobs (blog). Darf deshalb NUR in Übersetzungseinheiten verwendet
 *     werden, die ausschließlich ins Plugin-Modul gelinkt werden (plugin-main.cpp,
 *     textbox-dock.cpp, textbox-scene.cpp, textbox-card-source.cpp).
 *   - Die Qt-/OBS-freien Test-Targets (textbox-tests, -import-, -ai-, -textfmt-,
 *     -theme-icons-, -render-tests) linken kein libobs; die dort kompilierten
 *     Dateien (textbox-state/import/ai/html/render.cpp sowie alle Header) dürfen
 *     diesen Header NICHT einbinden.
 *   - Der Header ist header-only und muss nicht in CMakeLists.txt eingetragen werden.
 */

#pragma once

#include <obs.h>

#include <cstdlib>
#include <cstring>
#include <string>

/*
 * Liefert true, wenn TEXTBOX_DEBUG=1 in der Umgebung gesetzt ist.
 * Die Variable wird beim ersten Aufruf gelesen und danach gecacht (Function-local
 * static → thread-sichere Initialisierung seit C++11). Änderungen an der Umgebung
 * zur Laufzeit werden bewusst ignoriert; OBS muss mit gesetzter Variable starten.
 */
inline bool TextboxDebugEnabled()
{
	static const bool enabled = [] {
#ifdef _MSC_VER
		// MSVC markiert getenv als unsicher (C4996, mit /WX ein Fehler) →
		// _dupenv_s ist das sichere getenv-Äquivalent; Puffer muss freigegeben werden.
		char *value = nullptr;
		size_t len = 0;
		bool on = _dupenv_s(&value, &len, "TEXTBOX_DEBUG") == 0 && value != nullptr &&
			  std::strcmp(value, "1") == 0;
		std::free(value);
		return on;
#else
		const char *value = std::getenv("TEXTBOX_DEBUG");
		return value != nullptr && std::strcmp(value, "1") == 0;
#endif
	}();
	return enabled;
}

/*
 * Verbose-Debugzeile mit Präfix "[textbox]" ins OBS-Log — nur bei TEXTBOX_DEBUG=1.
 * Ohne gesetzte Variable ist der Aufruf ein reiner Bool-Check (kein Seiteneffekt).
 * Hinweis: den String am Aufrufort möglichst billig bauen, da er auch bei
 * deaktiviertem Debug konstruiert wird.
 */
inline void tb_log(const std::string &msg)
{
	if (TextboxDebugEnabled())
		blog(LOG_INFO, "[textbox] %s", msg.c_str());
}
